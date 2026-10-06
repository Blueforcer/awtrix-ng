#include "../../support.h"
/* The speaker helper core on a fake clock over the fake awtrix_pcm device: the protocol codec,
 * the volume curve, the socket protocol and what the driver adds. */
#include "platform/tc002/audio/pcm/pcm_backend.h"
#include "platform/tc002/audio/helper/audio_process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fake_pcm_device.h"

static void (*const check)(int, const char*) = awtrix_test_require;

enum { OUT_CAPACITY = 1 << 20 };

typedef struct {
  fake_pcm device;
  int64_t now;
  unsigned statuses, hellos;
  tc002_audio_status last;
  uint32_t last_generation;
  int send_blocked, send_broken;
} rig;

static rig *active;
static ap_helper helper;

static int64_t fake_clock(fake_pcm *f) {
  (void)f;
  return active->now;
}

static int64_t now_op(void *context) { return ((rig *)context)->now; }

static int send_op(void *context, const uint8_t *data, size_t size) {
  rig *r = context;
  tc002_audio_frame frame;
  if (r->send_broken) return -1;
  if (r->send_blocked) return 0;
  check(tc002_audio_parse(data, size, &frame), "helper sends valid messages");
  if (frame.type == TC002_AUDIO_HELLO) {
    tc002_audio_hello hello;
    check(tc002_audio_read_hello(&frame, &hello) && hello.device_rate == 44100 && hello.device_channels == 1 &&
              hello.window_bytes == TC002_AUDIO_WINDOW_BYTES && hello.min_db == -60 && hello.max_db == -3 &&
              hello.max_pcm_bytes == TC002_AUDIO_MAX_PCM_BYTES,
          "hello: 44.1 kHz mono, -60..-3 dB, 16 KiB window");
    ++r->hellos;
  } else {
    check(frame.type == TC002_AUDIO_STATUS && tc002_audio_read_status(&frame, &r->last), "status decodes");
    r->last_generation = frame.generation;
    ++r->statuses;
  }
  return 1;
}

static int ioctl_op(void *context, unsigned request, void *argument) {
  rig *r = context;
  if (request == AWTRIX_PCM_SET_GAIN) {
    int32_t db;
    memcpy(&db, argument, sizeof db);
    check(db >= TC002_AUDIO_MIN_DB && db <= TC002_AUDIO_MAX_DB, "gain stays within -60..-3 dB");
  }
  check(request != AWTRIX_PCM_DRAIN, "the blocking DRAIN ioctl is never used");
  return fake_pcm_ioctl(&r->device, request, argument);
}

static ssize_t write_op(void *context, const void *data, size_t size) {
  rig *r = context;
  check(size && size <= AH_BLOCK_BYTES && size % 16 == 0, "writes are whole MIU words of at most one block");
  return fake_pcm_write(&r->device, data, size);
}

static const ap_ops ops = {NULL, now_op, send_op, ioctl_op, write_op};
static ap_ops bound;

static rig *fresh(void) {
  static rig r;
  static int16_t out[OUT_CAPACITY];
  memset(&r, 0, sizeof r);
  r.now = 1000;
  active = &r;
  fake_pcm_reset(&r.device, fake_clock, out, OUT_CAPACITY);
  bound = ops;
  bound.context = &r;
  return &r;
}

static ah_state *core(void) { return &helper.core; }

static int start(rig *r) {
  int okay = ap_start(&helper, &bound);
  (void)r;
  ah_flush(core());
  return okay;
}

static void deliver(const uint8_t *message, size_t size) { ah_receive(core(), message, size, active->now); }

static void open_stream(uint32_t generation, uint32_t rate, uint8_t channels) {
  uint8_t m[TC002_AUDIO_MAX_MESSAGE];
  deliver(m, tc002_audio_encode_open(m, sizeof m, generation, rate, channels));
}
static void pcm(uint32_t generation, const int16_t *samples, size_t count) {
  uint8_t m[TC002_AUDIO_MAX_MESSAGE];
  const size_t size = tc002_audio_encode_pcm(m, sizeof m, generation, samples, count);
  check(size != 0, "pcm encodes");
  deliver(m, size);
}
static void simple(uint8_t type, uint32_t generation) {
  uint8_t m[TC002_AUDIO_MAX_MESSAGE];
  deliver(m, tc002_audio_encode_empty(m, sizeof m, type, generation));
}
static void volume(uint8_t percent) {
  uint8_t m[TC002_AUDIO_MAX_MESSAGE];
  deliver(m, tc002_audio_encode_volume(m, sizeof m, percent));
}

static void run_for(int64_t ms) {
  const int64_t end = active->now + ms;
  while (active->now < end && core()->state != TC002_AUDIO_FAILED) {
    int wait = ah_service(core(), active->now);
    ah_flush(core());
    if (wait < 0 || wait > 10) wait = 10;
    if (wait == 0) wait = 1;
    active->now += wait;
  }
}

static int run_until_blocked(int64_t ms) {
  const int64_t end = active->now + ms;
  while (active->now < end && core()->state != TC002_AUDIO_FAILED) {
    const int wait = ah_service(core(), active->now);
    ah_flush(core());
    if (wait < 0) return 1;
    active->now += wait ? wait : 1;
  }
  return 0;
}

static void feed(uint32_t generation, uint8_t channels, unsigned frames, unsigned *sent_frames,
                 int16_t (*sample)(unsigned)) {
  int16_t block[2048];
  unsigned i, at = 0;
  while (at < frames && core()->state != TC002_AUDIO_FAILED) {
    unsigned n = frames - at;
    uint32_t in_flight;
    if (n > 2048u / channels) n = 2048u / channels;
    for (i = 0; i < n; ++i) {
      block[i * channels] = sample(at + i);
      if (channels == 2) block[i * channels + 1] = sample(at + i);
    }
    in_flight = (*sent_frames) * 2u * channels -
                (active->last_generation == generation ? active->last.consumed_bytes : 0);
    if (in_flight + n * 2u * channels > TC002_AUDIO_WINDOW_BYTES) {
      run_for(10);
      continue;
    }
    pcm(generation, block, n * channels);
    *sent_frames += n;
    at += n;
    run_for(1);
  }
}

static int16_t square(unsigned index) { return (int16_t)((index % 100) < 50 ? 4000 : -4000); }
static int16_t ramp(unsigned index) { return (int16_t)(index % 30000 + 1); }

static void feed_tone(uint32_t generation, uint8_t channels, unsigned frames, unsigned *sent_frames) {
  feed(generation, channels, frames, sent_frames, square);
}

static size_t first_sound(const fake_pcm *f) {
  size_t first = 0;
  while (first < f->out_count && !f->out[first]) ++first;
  return first;
}

static void test_volume_curve(void) {
  unsigned p;
  check(ah_volume_db(0) == -60 && ah_volume_db(100) == -3 && ah_volume_db(255) == -3, "curve ends");
  check(ah_volume_db(50) == -14 && ah_volume_db(17) == -30, "35*log10(40v)-60 curve");
  for (p = 1; p <= 100; ++p) check(ah_volume_db((uint8_t)p) >= ah_volume_db((uint8_t)(p - 1)), "monotonic");
}

static void test_protocol_codec(void) {
  uint8_t m[TC002_AUDIO_MAX_MESSAGE + 1];
  tc002_audio_frame frame;
  tc002_audio_status in, out = {0};
  uint32_t rate;
  uint8_t channels;
  int16_t samples[3] = {-1, 32767, -32768};
  size_t n = tc002_audio_encode_open(m, sizeof m, 77, 32000, 2);
  check(n == 16 && tc002_audio_parse(m, n, &frame) && frame.generation == 77, "open header");
  check(tc002_audio_read_open(&frame, &rate, &channels) && rate == 32000 && channels == 2, "open payload");
  m[12] = 3;
  check(!tc002_audio_read_open(&frame, &rate, &channels), "three channels rejected");
  n = tc002_audio_encode_open(m, sizeof m, 77, 96000, 1);
  check(tc002_audio_parse(m, n, &frame) && !tc002_audio_read_open(&frame, &rate, &channels), "rate bound");
  n = tc002_audio_encode_pcm(m, sizeof m, 5, samples, 3);
  check(n == 14 && m[8] == 0xff && m[9] == 0xff && m[10] == 0xff && m[11] == 0x7f && m[12] == 0 && m[13] == 0x80,
        "pcm is little-endian s16");
  check(!tc002_audio_encode_pcm(m, sizeof m, 5, samples, 2049), "pcm payload is bounded");
  check(!tc002_audio_encode_pcm(m, 13, 5, samples, 3), "capacity is honoured");
  check(!tc002_audio_parse(m, 13, &frame), "truncated datagram rejected");
  m[0] = 2;
  check(!tc002_audio_parse(m, 14, &frame), "unknown version rejected");
  memset(&in, 0, sizeof in);
  in.state = TC002_AUDIO_PLAYING;
  in.volume = 42;
  in.consumed_bytes = 0xdeadbeef;
  in.vendor_result = -123456789;
  in.completed_generation = 9;
  n = tc002_audio_encode_status(m, sizeof m, 10, &in);
  check(n == 36 && tc002_audio_parse(m, n, &frame) && tc002_audio_read_status(&frame, &out), "status round trip");
  check(out.consumed_bytes == 0xdeadbeef && out.vendor_result == -123456789 && out.completed_generation == 9 &&
            out.volume == 42,
        "status fields");
  check(!tc002_audio_encode_empty(m, sizeof m, TC002_AUDIO_PCM, 1), "empty only for drain and stop");
  n = tc002_audio_encode_empty(m, sizeof m, TC002_AUDIO_STOP, 1);
  m[2] = 1;
  check(!tc002_audio_parse(m, n, &frame), "length must match the datagram");
}

static void test_start_and_shutdown(void) {
  rig *r = fresh();
  check(start(r), "clean start");
  check(core()->state == TC002_AUDIO_IDLE && r->device.muted && core()->muted, "idle and muted");
  check(r->device.gain_db == ah_volume_db(0), "starts at the curve's lowest gain");
  check(r->hellos == 1 && r->statuses == 1 && r->last.state == TC002_AUDIO_IDLE, "hello then idle status");
  check(!r->device.writes, "no PCM before a stream opens");
  check(ah_service(core(), r->now) == -1, "an idle helper waits for the socket only");
  check(!ap_shutdown(&helper) && r->device.muted && r->device.stops == 1, "shutdown mutes and stops");
  check(r->device.state == AWTRIX_PCM_PREPARED && !fake_pcm_level(&r->device),
        "close() will not wait: nothing is prepared");
}

static void test_start_failures_are_contained(void) {
  rig *r = fresh();
  r->device.fail_request = AWTRIX_PCM_GET_STATUS;
  r->device.fail_after = 1;
  r->device.fail_errno = ENODEV;
  check(!start(r) && r->last.state == TC002_AUDIO_FAILED && r->last.error == TC002_AUDIO_ERROR_VENDOR &&
            r->last.vendor_result == -ENODEV,
        "an unreadable status fails the start with -errno");

  r = fresh();
  r->device.state = AWTRIX_PCM_RUNNING;
  check(!start(r) && r->last.error == TC002_AUDIO_ERROR_QUEUE, "a device that is not freshly prepared is refused");

  r = fresh();
  r->device.fail_request = AWTRIX_PCM_SET_MUTE;
  r->device.fail_after = 1;
  r->device.fail_errno = EIO;
  r->device.hal_error = -2;
  check(!start(r) && r->last.vendor_result == -2, "a HAL error is reported as the vendor result");

  r = fresh();
  r->device.fail_request = AWTRIX_PCM_SET_GAIN;
  r->device.fail_after = 1;
  r->device.fail_errno = EFAULT;
  check(!start(r) && r->last.vendor_result == -EFAULT, "a refused gain fails the start");

  r = fresh();
  r->device.fail_request = AWTRIX_PCM_GET_STATUS;
  r->device.fail_after = 2;
  r->device.fail_errno = EIO;
  check(!start(r) && r->last.state == TC002_AUDIO_FAILED, "a failed readback fails the start");
  check(!ap_shutdown(&helper) && r->device.muted, "a partial start is still muted and stopped");
}

static void test_tone_plays_and_drains(void) {
  rig *r = fresh();
  unsigned sent = 0;
  check(start(r), "start");
  volume(50);
  check(r->device.gain_db == ah_volume_db(50) && r->device.muted, "volume applies while muted");
  open_stream(1, 44100, 1);
  check(core()->state == TC002_AUDIO_PRIMING, "open begins with a silent primer");
  feed_tone(1, 1, 44100, &sent);
  simple(TC002_AUDIO_DRAIN, 1);
  run_for(2000);
  check(core()->state == TC002_AUDIO_IDLE && r->device.muted, "drained generation ends muted and idle");
  check(r->last.completed_generation == 1 && r->last.consumed_bytes == 88200,
        "status reports completion and consumption");
  check(r->device.unmutes == 1, "one unmute per generation");
  check(!r->device.lost_sound && !r->device.muted_sound, "every sample was played, none muted away");
  check(r->device.nonsilent_writes >= 43 && r->device.nonsilent_writes <= 44,
        "one second of 44.1 kHz audio in 2048-byte blocks");
  check(!r->last.underruns && !r->device.underruns, "a paced feed does not underrun");
  check(r->device.stops == 1 && r->device.state == AWTRIX_PCM_PREPARED && !fake_pcm_level(&r->device) &&
            !r->device.dry_runs,
        "the stream ends with STOP once only its silent tail is left, before the ring runs dry");
  ap_shutdown(&helper);
}

static void test_resamples_and_downmixes(void) {
  rig *r = fresh();
  int16_t samples[2048];
  unsigned i;
  size_t first;
  check(start(r), "start");
  volume(40);
  open_stream(2, 22050, 2);
  for (i = 0; i < 1024; ++i) {
    samples[2 * i] = (int16_t)(i * 10);
    samples[2 * i + 1] = (int16_t)(i * 10 + 20);
  }
  pcm(2, samples, 2048);
  simple(TC002_AUDIO_DRAIN, 2);
  run_for(1000);
  first = first_sound(&r->device);
  check(r->device.out_count - first >= 2040, "22.05 kHz doubles to about 2048 output samples");
  for (i = 0; i < 2000; i += 2) {
    const int32_t expected = (int32_t)(i / 2) * 10 + 10;
    check(r->device.out[first + i] == expected, "even outputs hit the downmixed input samples");
    check(r->device.out[first + i + 1] == expected + 5, "odd outputs interpolate halfway");
  }
  check(core()->state == TC002_AUDIO_IDLE && r->last.completed_generation == 2, "short stream drains");
  ap_shutdown(&helper);
}

static void test_short_streams_start(void) {
  rig *r = fresh();
  int16_t samples[100];
  unsigned i;
  size_t first;
  check(start(r), "start");
  volume(30);
  open_stream(3, 44100, 1);
  for (i = 0; i < 100; ++i) samples[i] = (int16_t)(i + 1);
  pcm(3, samples, 100);
  simple(TC002_AUDIO_DRAIN, 3);
  check(run_until_blocked(1000), "a stream below the start threshold completes by itself");
  first = first_sound(&r->device);
  for (i = 0; i < 100; ++i) check(r->device.out[first + i] == (int16_t)(i + 1), "all of it reaches the device");
  check(core()->state == TC002_AUDIO_IDLE && r->last.completed_generation == 3 && !r->device.lost_sound,
        "and all of it plays before the stop");

  open_stream(4, 44100, 1);
  simple(TC002_AUDIO_DRAIN, 4);
  check(run_until_blocked(1000) && r->last.completed_generation == 4, "an empty stream completes");
  check(!r->device.underruns && !r->device.dry_runs, "neither leaves the DMA to run dry");
  ap_shutdown(&helper);
}

static void test_stop_is_immediate(void) {
  rig *r = fresh();
  unsigned sent = 0, stops;
  check(start(r), "start");
  volume(80);
  open_stream(3, 48000, 2);
  feed_tone(3, 2, 12000, &sent);
  check(core()->state == TC002_AUDIO_PLAYING && !r->device.muted, "playing unmuted");
  stops = r->device.stops;
  r->device.order_count = 0;
  simple(TC002_AUDIO_STOP, 3);
  check(r->device.muted && r->device.stops == stops + 1 && r->device.order[0] == AWTRIX_PCM_SET_MUTE &&
            r->device.order[1] == AWTRIX_PCM_STOP,
        "stop mutes, then stops, with no further PCM");
  ah_flush(core());
  check(core()->state == TC002_AUDIO_IDLE && r->last.completed_generation == 3 && !r->last.queued_bytes,
        "stopped generation is complete and discarded");
  check(!fake_pcm_level(&r->device), "the ring is empty");
  pcm(3, (const int16_t[]){1, 2, 3, 4}, 4);
  check(core()->count == 0, "late PCM of a stopped generation is ignored");
  ap_shutdown(&helper);
}

static void test_open_preempts(void) {
  rig *r = fresh();
  unsigned sent = 0;
  check(start(r), "start");
  volume(30);
  open_stream(4, 44100, 1);
  feed_tone(4, 1, 8000, &sent);
  open_stream(5, 44100, 1);
  check(r->device.muted && core()->generation == 5 && core()->completed == 4,
        "a new open stops the old generation muted");
  sent = 0;
  feed_tone(5, 1, 4000, &sent);
  simple(TC002_AUDIO_DRAIN, 5);
  run_for(1000);
  check(r->last.completed_generation == 5 && r->device.muted, "second generation completes");
  ap_shutdown(&helper);
}

static void test_starvation_pauses_and_resumes(void) {
  rig *r = fresh();
  unsigned sent = 0;
  check(start(r), "start");
  volume(60);
  open_stream(6, 44100, 1);
  feed_tone(6, 1, 4096, &sent);
  run_for(300);
  check(r->last.underruns == 1 && core()->state == TC002_AUDIO_PLAYING, "starving keeps the DMA fed with silence");
  run_for(1500);
  check(core()->state == TC002_AUDIO_PAUSED && r->device.muted, "a long gap mutes");
  check(r->device.state == AWTRIX_PCM_PREPARED && !fake_pcm_level(&r->device) && !r->device.underruns,
        "and stops the stream before the DMA runs dry");
  check(!r->device.dry_runs, "the silence fed while starving kept the ring from running dry");
  feed_tone(6, 1, 4096, &sent);
  run_for(50);
  check(core()->state == TC002_AUDIO_PLAYING && !r->device.muted && r->device.unmutes == 2,
        "new audio re-primes and unmutes");
  simple(TC002_AUDIO_DRAIN, 6);
  run_for(1000);
  check(core()->state == TC002_AUDIO_IDLE && r->last.completed_generation == 6, "drain after resume completes");
  check(!r->device.lost_sound, "nothing audible was cut off");
  ap_shutdown(&helper);
}

static void starve_until_settling(rig *r, uint32_t generation) {
  unsigned sent = 0;
  check(start(r), "start");
  volume(60);
  open_stream(generation, 44100, 1);
  feed_tone(generation, 1, 4096, &sent);
  while (core()->state != TC002_AUDIO_SETTLING && core()->state != TC002_AUDIO_FAILED) run_for(1);
  check(core()->state == TC002_AUDIO_SETTLING, "a long gap starts settling towards a pause");
}

static void test_audio_during_settle_is_not_stranded(void) {
  rig *r = fresh();
  int16_t block[2048];
  unsigned i;
  for (i = 0; i < 2048; ++i) block[i] = (int16_t)(i % 100 < 50 ? 3000 : -3000);
  starve_until_settling(r, 14);
  for (i = 0; i < 4; ++i) pcm(14, block, 2048);
  check(core()->count * 2u == TC002_AUDIO_WINDOW_BYTES, "the runtime's whole window is queued");
  check(!run_until_blocked(300), "a full window queued while settling never leaves the helper asleep");
  check(core()->state == TC002_AUDIO_PLAYING && !r->device.muted && r->device.unmutes == 2, "it resumes by itself");
  simple(TC002_AUDIO_STOP, 14);
  ap_shutdown(&helper);

  r = fresh();
  starve_until_settling(r, 15);
  pcm(15, block, 1024);
  simple(TC002_AUDIO_DRAIN, 15);
  run_until_blocked(1000);
  check(core()->state == TC002_AUDIO_IDLE && r->last.completed_generation == 15 &&
            r->last.consumed_bytes == (4096 + 1024) * 2,
        "a tail and drain sent while settling play out");
  ap_shutdown(&helper);

  r = fresh();
  starve_until_settling(r, 16);
  simple(TC002_AUDIO_DRAIN, 16);
  run_until_blocked(1000);
  check(core()->state == TC002_AUDIO_IDLE && r->last.completed_generation == 16, "a bare drain while settling completes");
  ap_shutdown(&helper);
}

static void test_volume_zero_stays_muted(void) {
  rig *r = fresh();
  unsigned sent = 0;
  size_t i;
  check(start(r), "start");
  volume(0);
  open_stream(7, 44100, 1);
  feed_tone(7, 1, 8192, &sent);
  check(r->device.muted && !r->device.unmutes, "zero volume never unmutes");
  for (i = 0; i < r->device.out_count; ++i) check(!r->device.out[i], "the device only ever receives silence");
  volume(20);
  check(!r->device.muted && r->device.gain_db == ah_volume_db(20), "raising the volume unmutes a playing stream");
  volume(0);
  check(r->device.muted, "volume zero mutes a playing stream");
  simple(TC002_AUDIO_STOP, 7);
  ap_shutdown(&helper);
}

static void test_protocol_errors(void) {
  rig *r = fresh();
  uint8_t m[TC002_AUDIO_MAX_MESSAGE];
  int16_t big[2048] = {0};
  unsigned i;
  check(start(r), "start");
  deliver((const uint8_t *)"junk", 4);
  ah_flush(core());
  check(r->last.error == TC002_AUDIO_ERROR_BAD_MESSAGE && core()->state == TC002_AUDIO_IDLE, "junk is reported, not fatal");
  open_stream(8, 44100, 2);
  pcm(8, big, 3);
  ah_flush(core());
  check(r->last.error == TC002_AUDIO_ERROR_BAD_MESSAGE, "a partial stereo frame is rejected");
  for (i = 0; i < 5; ++i) pcm(8, big, 2048);
  ah_flush(core());
  check(r->last.error == TC002_AUDIO_ERROR_OVERFLOW && core()->count == 4096, "the window is enforced");
  tc002_audio_encode_volume(m, sizeof m, 10);
  m[8] = 101;
  deliver(m, 12);
  check(core()->volume != 101, "an out-of-range volume is ignored");
  open_stream(0, 44100, 1);
  check(core()->generation == 8, "generation zero cannot open");
  simple(TC002_AUDIO_STOP, 8);
  ap_shutdown(&helper);
}

static void play_ramp(rig *r, uint32_t generation, unsigned frames) {
  unsigned sent = 0, i;
  size_t first;
  volume(50);
  open_stream(generation, 44100, 1);
  feed(generation, 1, frames, &sent, ramp);
  simple(TC002_AUDIO_DRAIN, generation);
  run_for(1500);
  first = first_sound(&r->device);
  check(r->device.out_count - first >= frames, "the whole ramp reached the device");
  for (i = 0; i < frames; ++i)
    if (r->device.out[first + i] != ramp(i)) check(0, "44.1 kHz mono passes through sample for sample");
  check(core()->state == TC002_AUDIO_IDLE && r->last.completed_generation == generation && !r->device.lost_sound,
        "and plays out completely");
}

static void test_device_backpressure(void) {
  rig *r = fresh();
  check(start(r), "start");
  r->device.refuse = 3;
  play_ramp(r, 20, 20000);
  check(!r->device.refuse, "refused writes are retried, not lost");
  ap_shutdown(&helper);

  r = fresh();
  check(start(r), "start");
  r->device.accept_limit = 1008;
  play_ramp(r, 21, 20000);
  check(!helper.carry_length, "partial writes are completed from the carry, in order");
  ap_shutdown(&helper);

  r = fresh();
  check(start(r), "start");
  volume(50);
  open_stream(22, 44100, 1);
  run_for(40);
  {
    int16_t block[2048];
    unsigned i;
    for (i = 0; i < 2048; ++i) block[i] = 100;
    for (i = 0; i < 4; ++i) pcm(22, block, 2048);
  }
  r->device.refuse = 100000;
  run_for(2000);
  check(core()->state == TC002_AUDIO_FAILED && r->last.vendor_result == AH_DEVICE_FULL,
        "a device that never takes data fails the helper");
  ap_shutdown(&helper);
}

static void test_driver_failures(void) {
  rig *r = fresh();
  unsigned sent = 0;
  check(start(r), "start");
  volume(50);
  open_stream(30, 44100, 1);
  feed_tone(30, 1, 8192, &sent);
  r->device.state = AWTRIX_PCM_FAILED;
  r->device.hal_error = -3;
  run_for(100);
  check(core()->state == TC002_AUDIO_FAILED && r->last.state == TC002_AUDIO_FAILED &&
            r->last.error == TC002_AUDIO_ERROR_VENDOR && r->last.vendor_result == -3,
        "a failed driver fails the helper with its HAL error");
  check(ap_shutdown(&helper) == 1 && r->device.muted, "shutdown still mutes; the failed STOP is counted");

  r = fresh();
  sent = 0;
  check(start(r), "start");
  volume(50);
  open_stream(31, 44100, 1);
  feed_tone(31, 1, 8192, &sent);
  r->device.stuck = 1;
  run_for(1500);
  check(core()->state == TC002_AUDIO_FAILED && r->last.vendor_result == -ETIMEDOUT,
        "a DMA level that stops moving fails the helper");
  ap_shutdown(&helper);

  r = fresh();
  check(start(r), "start");
  r->device.state = AWTRIX_PCM_FAILED;
  r->device.hal_error = -7;
  ap_device_failed(core());
  ah_flush(core());
  check(r->last.state == TC002_AUDIO_FAILED && r->last.vendor_result == -7, "POLLERR reports the HAL error");
  ap_shutdown(&helper);

  r = fresh();
  check(start(r), "start");
  volume(50);
  open_stream(32, 44100, 1);
  r->device.fail_request = AWTRIX_PCM_STOP;
  r->device.fail_after = 1;
  r->device.fail_errno = EIO;
  simple(TC002_AUDIO_STOP, 32);
  ah_flush(core());
  check(r->last.state == TC002_AUDIO_FAILED, "a refused STOP is fatal");
  ap_shutdown(&helper);
}

static void test_driver_underruns_are_reported(void) {
  rig *r = fresh();
  int16_t block[2048];
  unsigned i;
  for (i = 0; i < 2048; ++i) block[i] = square(i);
  check(start(r), "start");
  volume(50);
  open_stream(40, 44100, 1);
  for (i = 0; i < 4; ++i) pcm(40, block, 2048);
  run_for(50);
  check(core()->state == TC002_AUDIO_PLAYING && r->device.state == AWTRIX_PCM_RUNNING, "running");
  check(core()->count >= 2100, "with two blocks still queued in the helper");
  r->now += 300;
  run_for(20);
  check(r->device.underruns == 1 && r->last.underruns == 1, "a DMA underrun of a late helper reaches the status");
  check(r->device.state == AWTRIX_PCM_RUNNING, "and the driver restarted the stream");
  simple(TC002_AUDIO_DRAIN, 40);
  run_for(1000);
  check(core()->state == TC002_AUDIO_IDLE && r->last.completed_generation == 40 && r->last.underruns == 1,
        "the stream still completes");
  ap_shutdown(&helper);
}

static void test_status_reports_the_device(void) {
  rig *r = fresh();
  unsigned sent = 0;
  check(start(r), "start");
  volume(50);
  open_stream(50, 44100, 1);
  feed_tone(50, 1, 20000, &sent);
  check(r->last.device_busy_bytes > 0 && r->last.device_busy_bytes <= AH_TARGET_BUSY_BYTES,
        "busy is the driver's level, kept at most at the target");
  check(r->last.queued_bytes <= TC002_AUDIO_WINDOW_BYTES, "queued is the helper's own input queue");
  r->send_blocked = 1;
  volume(10);
  ah_flush(core());
  check(core()->status_dirty, "a full socket keeps the status pending");
  r->send_blocked = 0;
  ah_flush(core());
  check(!core()->status_dirty && r->last.volume == 10, "the pending status goes out later");
  r->send_broken = 1;
  volume(11);
  ah_flush(core());
  check(core()->broken, "a broken socket is reported to the loop");
  ap_shutdown(&helper);
}

typedef struct { int channel, ready; } loop_context;

static int loop_send(void *context, const uint8_t *data, size_t size) {
  loop_context *loop = context;
  const int result = ah_send_datagram(loop->channel, data, size);
  if (result == 0 && loop->ready >= 0) {
    const char byte = 1;
    if (write(loop->ready, &byte, 1) != 1) _exit(2);
    close(loop->ready);
    loop->ready = -1;
  }
  return result;
}

static void test_idle_process_retries_pending_status(void) {
  int pair[2], ready[2], size = 4096, child_status = 0;
  pid_t child;
  uint8_t message[TC002_AUDIO_MAX_MESSAGE] = {0};
  int found = 0;
  check(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) == 0, "process socket pair");
  check(pipe(ready) == 0, "process readiness pipe");
  check(setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &size, sizeof size) == 0, "small helper send buffer");
  while (send(pair[0], message, 1, MSG_DONTWAIT | MSG_NOSIGNAL) > 0) {}
  check(errno == EAGAIN || errno == EWOULDBLOCK, "helper output is backpressured");
  child = fork();
  check(child >= 0, "process fixture forks");
  if (child == 0) {
    ah_state state;
    loop_context context = {pair[0], ready[1]};
    const ah_ops loop_ops = {&context, NULL, NULL, loop_send};
    close(pair[1]);
    close(ready[0]);
    ah_init(&state, &loop_ops);
    state.state = TC002_AUDIO_IDLE;
    state.hello_pending = 0;
    state.status_dirty = 1;
    state.completed = 123;
    _exit(ah_run(&state, pair[0], -1, NULL, NULL));
  }
  close(pair[0]);
  close(ready[1]);
  {
    struct pollfd p = {ready[0], POLLIN, 0};
    check(poll(&p, 1, 1000) > 0 && read(ready[0], message, 1) == 1,
          "idle helper attempted the pending status");
  }
  close(ready[0]);
  {
    const int64_t deadline = ah_monotonic_ms() + 2000;
    while (!found && ah_monotonic_ms() < deadline) {
      struct pollfd p = {pair[1], POLLIN, 0};
      tc002_audio_frame frame;
      tc002_audio_status status;
      ssize_t received;
      if (poll(&p, 1, 50) <= 0) continue;
      received = recv(pair[1], message, sizeof message, 0);
      if (received <= 0) break;
      found = tc002_audio_parse(message, (size_t)received, &frame) &&
              tc002_audio_read_status(&frame, &status) && status.completed_generation == 123;
    }
  }
  shutdown(pair[1], SHUT_RDWR);
  close(pair[1]);
  check(waitpid(child, &child_status, 0) == child && WIFEXITED(child_status) &&
            WEXITSTATUS(child_status) == AH_EXIT_CLEAN, "idle helper exits after its peer closes");
  check(found, "completed status retries after output drains without another control message");
}

int main(void) {
  test_protocol_codec();
  test_volume_curve();
  test_start_and_shutdown();
  test_start_failures_are_contained();
  test_tone_plays_and_drains();
  test_resamples_and_downmixes();
  test_short_streams_start();
  test_stop_is_immediate();
  test_open_preempts();
  test_starvation_pauses_and_resumes();
  test_audio_during_settle_is_not_stranded();
  test_volume_zero_stays_muted();
  test_protocol_errors();
  test_device_backpressure();
  test_driver_failures();
  test_driver_underruns_are_reported();
  test_status_reports_the_device();
  test_idle_process_retries_pending_status();
  printf("pcm helper contracts: %u passed\n", awtrix_test_passed);
  return 0;
}
