#include "system/RadioAudioEsp32.h"

#if defined(AWTRIX_SOC_ESP32S3)

#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include <cstdlib>
#include <memory>
#include <vector>

#include "core/radio/PlaylistParser.h"
#include "core/radio/RadioDisplay.h"
#include "core/script/ScriptServices.h"
#include "system/HeapCaps.h"
#include "system/HeapProbe.h"
#include "system/Log.h"

namespace awtrix {

namespace {

constexpr uint32_t kTaskStackBytes = 12288;
constexpr UBaseType_t kTaskPriority = 2;
constexpr BaseType_t kTaskCore = 1;

constexpr int kDmaBufferCount = 8;
constexpr int kDmaBufferFrames = 512;

constexpr std::size_t kNetworkChunkBytes = 1024;

// Compressed audio held ahead of the decoder. It lands in PSRAM, which is why 64 KB is affordable;
// it is the whole defence against a station that delivers in bursts.
constexpr std::size_t kInputBufferBytes = 64 * 1024;
// Wait for this much before the first frame goes out, otherwise playback starts and immediately
// stutters while the buffer fills.
constexpr std::size_t kPrerollBytes = 16 * 1024;
constexpr std::size_t kUndecodableAfterBytes = 32 * 1024;
// Decoding is capped per pass so the loop keeps reading from the socket; going flat out here is
// what starves the input buffer.
constexpr int kFramesPerPass = 2;
constexpr std::size_t kCompactAtBytes = 32 * 1024;

constexpr int kMaxRedirects = 3;
constexpr uint32_t kConnectTimeoutMs = 8000;
constexpr uint32_t kReadTimeoutMs = 8000;
constexpr uint32_t kBackoffMs[] = {2000, 5000, 15000};

const char* kUserAgent = "AWTRIX-NG";

}

// No PSRAM means no room for the input buffer, so the whole service is left out rather than
// shipping something that stutters.
bool RadioAudioEsp32::usable(int pinBclk, int pinLrclk, int pinDout) {
  if (pinBclk < 0 || pinLrclk < 0 || pinDout < 0) return false;
  return psramFound();
}

// The object itself is pinned to internal RAM: it holds the decoder state that the audio task hits
// on every frame, and PSRAM latency there costs real decode time.
void* RadioAudioEsp32::operator new(std::size_t bytes) {
  if (void* p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)) return p;
  return std::malloc(bytes);
}

void RadioAudioEsp32::operator delete(void* p) { std::free(p); }

RadioAudioEsp32::RadioAudioEsp32(CoreEngine& engine, int pinBclk, int pinLrclk, int pinDout)
    : engine_(engine), pinBclk_(pinBclk), pinLrclk_(pinLrclk), pinDout_(pinDout) {
  lock_ = xSemaphoreCreateMutex();
}

RadioAudioEsp32::~RadioAudioEsp32() {
  stop();
  if (task_) vTaskDelete(task_);
  if (lock_) vSemaphoreDelete(lock_);
}

DispatchResult RadioAudioEsp32::play(const std::string& url, const std::string& label,
                                     DispatchDetail& detail) {
  radio::Url parsed;
  if (!radio::parseUrl(url, parsed)) {
    detail = {"url", "not a usable http or https URL"};
    return DispatchResult::ValidationError;
  }

  // A TLS session needs tens of kilobytes of contiguous internal RAM. Refuse up front instead of
  // letting the audio task fail the handshake and retry against a heap that is still too tight.
  if (parsed.tls) {
    const std::size_t free = heap_caps_get_free_size(kGuardHeapCaps);
    const std::size_t largest = heap_caps_get_largest_free_block(kGuardHeapCaps);
    if (!script::fetchFits(true, free, largest)) {
      detail = {"url", "not enough free memory for a TLS connection right now"};
      return DispatchResult::Busy;
    }
  }

  if (xSemaphoreTake(lock_, portMAX_DELAY) == pdTRUE) {
    pendingUrl_ = url;
    pendingLabel_ = label;
    pendingError_.clear();
    // Bumping the sequence is the switch-station signal: the audio task compares it every pass and
    // abandons the current stream the moment it changes.
    urlSeq_.fetch_add(1);
    xSemaphoreGive(lock_);
  }
  stopRequested_.store(false);

  if (!task_) {
    if (xTaskCreatePinnedToCore(taskEntry, "radio", kTaskStackBytes, this, kTaskPriority, &task_,
                                kTaskCore) != pdPASS) {
      detail = {"", "could not start the audio task"};
      return DispatchResult::Failed;
    }
  }
  return DispatchResult::Ok;
}

void RadioAudioEsp32::stop() {
  stopRequested_.store(true);
  playing_.store(false);
}

void RadioAudioEsp32::setVolume(int percent) {
  volume_.store(percent < 0 ? 0 : (percent > 100 ? 100 : percent));
}

void RadioAudioEsp32::publishTitle(const std::string& title) {
  if (xSemaphoreTake(lock_, portMAX_DELAY) != pdTRUE) return;
  pendingTitle_ = title;
  xSemaphoreGive(lock_);
  handoffSeq_.fetch_add(1);
}

void RadioAudioEsp32::publishError(const std::string& message) {
  if (xSemaphoreTake(lock_, portMAX_DELAY) != pdTRUE) return;
  pendingError_ = message;
  xSemaphoreGive(lock_);
  handoffSeq_.fetch_add(1);
}

// Main-loop end of the handoff. The audio task must never touch the engine or push notifications
// itself, so it parks a title or an error and this picks them up.
void RadioAudioEsp32::tick(int64_t nowMs) {
  const uint32_t seq = handoffSeq_.load();
  if (seq == seenSeq_) return;
  seenSeq_ = seq;

  std::string title;
  std::string error;
  // Never block the main loop on the audio task's lock; rewind the seen counter so the next tick
  // picks the handoff up again.
  if (xSemaphoreTake(lock_, 0) != pdTRUE) {
    seenSeq_ = seq - 1;
    return;
  }
  title.swap(pendingTitle_);
  error.swap(pendingError_);
  xSemaphoreGive(lock_);

  RuntimeState& runtime = engine_.state().runtime();
  if (!error.empty()) {
    runtime.radioError = error;
    runtime.radioPlaying = false;
    engine_.state().emit(StateEvent::RadioChanged);
    return;
  }
  if (title.empty()) return;

  runtime.radioTitle = title;
  runtime.radioPlaying = playing_.load();
  engine_.state().emit(StateEvent::RadioChanged);

  if (!engine_.state().settings().radioMeta) return;
  AppSpec spec;
  if (radio::buildAnnouncement(title, radio::Announcement::Title, spec))
    engine_.notifications().push(spec, nowMs);
}

void RadioAudioEsp32::taskEntry(void* self) {
  static_cast<RadioAudioEsp32*>(self)->run();
}

// The audio task. Runs on core 1 at priority 2 and never returns: it connects, streams, decodes and
// pushes PCM to I2S, and reconnects on its own. Nothing here may touch the engine directly.
void RadioAudioEsp32::run() {
  std::vector<uint8_t> input;
  input.reserve(kInputBufferBytes + kCompactAtBytes);
  std::vector<int16_t> pcm(mp3::kMaxPcmPerFrame);
  std::unique_ptr<WiFiClient> plain;
  std::unique_ptr<WiFiClientSecure> secure;
  Client* client = nullptr;
  int attempt = 0;

  for (;;) {
    std::string url;
    uint32_t seq = 0;
    if (xSemaphoreTake(lock_, portMAX_DELAY) == pdTRUE) {
      url = pendingUrl_;
      seq = urlSeq_.load();
      xSemaphoreGive(lock_);
    }

    if (stopRequested_.load() || url.empty()) {
      closeStream();
      plain.reset();
      secure.reset();
      client = nullptr;
      playing_.store(false);
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    radio::Url target;
    std::string current = url;
    radio::ResponseHead head;
    bool connected = false;

#ifdef AWTRIX_HEAP_PROBE
    const std::size_t watchFree = heap_caps_get_free_size(kGuardHeapCaps);
    probe::watchBegin();
#endif

    for (int redirect = 0; redirect <= kMaxRedirects && !connected; ++redirect) {
      if (!radio::parseUrl(current, target)) break;

      if (target.tls) {
        secure.reset(new WiFiClientSecure());
        secure->setInsecure();
        secure->setTimeout(kConnectTimeoutMs / 1000);
        client = secure.get();
      } else {
        plain.reset(new WiFiClient());
        plain->setTimeout(kConnectTimeoutMs / 1000);
        client = plain.get();
      }

      if (!client->connect(target.host.c_str(), target.port)) break;
      const std::string request = radio::buildRequest(target, kUserAgent);
      client->write(reinterpret_cast<const uint8_t*>(request.data()), request.size());

      std::string raw;
      const uint32_t started = millis();
      while (millis() - started < kReadTimeoutMs && raw.find("\r\n\r\n") == std::string::npos &&
             raw.size() < 4096) {
        if (!client->available()) {
          vTaskDelay(pdMS_TO_TICKS(10));
          continue;
        }
        raw.push_back(static_cast<char>(client->read()));
      }

      head = radio::ResponseHead{};
      if (!radio::parseResponseHead(raw, head)) break;
      if (head.status >= 300 && head.status < 400 && !head.location.empty()) {
        current = radio::resolveRedirect(target, head.location);
        client->stop();
        continue;
      }
      if (head.status != 200) break;
      connected = true;
    }

#ifdef AWTRIX_HEAP_PROBE
    {
      const probe::Watch w = probe::watchPeek();
      logf("probe radio conn %s: b %u low %u max %u ok %d", target.host.c_str(),
           static_cast<unsigned>(watchFree), static_cast<unsigned>(w.lowWater),
           static_cast<unsigned>(w.maxAlloc), connected ? 1 : 0);
    }
#endif

    if (!connected) {
      const uint32_t wait = kBackoffMs[attempt < 3 ? attempt : 2];
      if (attempt < 3) ++attempt;
      publishError("could not connect to the station");
      vTaskDelay(pdMS_TO_TICKS(wait));
      continue;
    }

    // Station directories hand out .m3u/.pls rather than audio; pull the first usable entry out and
    // loop round to connect to that instead.
    if (head.contentType.find("audio/x-mpegurl") != std::string::npos ||
        head.contentType.find("audio/x-scpls") != std::string::npos) {
      std::string body;
      const uint32_t started = millis();
      while (millis() - started < kReadTimeoutMs && body.size() < 4096 && client->connected()) {
        if (!client->available()) {
          vTaskDelay(pdMS_TO_TICKS(10));
          continue;
        }
        body.push_back(static_cast<char>(client->read()));
      }
      client->stop();
      std::string resolved;
      if (radio::parsePlaylist(body, resolved)) {
        if (xSemaphoreTake(lock_, portMAX_DELAY) == pdTRUE) {
          pendingUrl_ = resolved;
          xSemaphoreGive(lock_);
        }
      } else {
        publishError("the station URL is a playlist with no usable entry");
        vTaskDelay(pdMS_TO_TICKS(kBackoffMs[2]));
      }
      continue;
    }

    attempt = 0;
    std::size_t bytesSeen = 0;
    bool decodedAnything = false;
    bool prerolled = false;
    uint32_t playStartMs = 0;
    int64_t deliveredSamples = 0;
    splitter_.reset(head.metaInt);
    tracker_.reset();
    decoder_.reset();
    input.clear();
    std::size_t consumed = 0;
    playing_.store(true);

    uint8_t chunk[kNetworkChunkBytes];
    uint32_t lastData = millis();
#ifdef AWTRIX_HEAP_PROBE
    uint32_t lastWatchMs = millis();
#endif
    while (!stopRequested_.load() && urlSeq_.load() == seq) {
#ifdef AWTRIX_HEAP_PROBE
      if (millis() - lastWatchMs >= 30000) {
        lastWatchMs = millis();
        const probe::Watch w = probe::watchPeek();
        logf("probe radio str: f %u lg %u low %u max %u n %u sv %u",
             static_cast<unsigned>(heap_caps_get_free_size(kGuardHeapCaps)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(kGuardHeapCaps)),
             static_cast<unsigned>(w.lowWater), static_cast<unsigned>(w.maxAlloc),
             static_cast<unsigned>(w.count), static_cast<unsigned>(starvedMs_.load()));
      }
#endif
      // Refill first, decode second. The splitter peels the interleaved ICY metadata blocks out of
      // the byte stream, so only real audio reaches the input buffer.
      bool received = false;
      while (input.size() - consumed < kInputBufferBytes) {
        const int available = client->available();
        if (available <= 0) break;
        const int want = available > static_cast<int>(sizeof(chunk))
                             ? static_cast<int>(sizeof(chunk))
                             : available;
        const int got = client->read(chunk, want);
        if (got <= 0) break;
        lastData = millis();
        received = true;

        bytesSeen += static_cast<std::size_t>(got);
        splitter_.feed(
            chunk, static_cast<std::size_t>(got),
            [&](const uint8_t* data, std::size_t bytes) {
              input.insert(input.end(), data, data + bytes);
            },
            [&](const std::string& block) {
              if (tracker_.update(block)) publishTitle(tracker_.title());
            });
      }

      if (!received) {
        if (!client->connected() || millis() - lastData > kReadTimeoutMs) break;
        if (input.size() == consumed) {
          starvedMs_.fetch_add(5);
          vTaskDelay(pdMS_TO_TICKS(5));
          continue;
        }
      }

      if (!prerolled) {
        if (input.size() - consumed < kPrerollBytes) {
          if (!received) vTaskDelay(pdMS_TO_TICKS(5));
          continue;
        }
        prerolled = true;
      }

      std::size_t offset = consumed;
      int decodedFrames = 0;
      while (offset < input.size() && decodedFrames < kFramesPerPass) {
        const int64_t decodeStart = esp_timer_get_time();
        const mp3::DecodeResult result =
            decoder_.decode(input.data() + offset, input.size() - offset, pcm.data());
        if (result.status == mp3::DecodeStatus::Ok) {
          const uint32_t took = static_cast<uint32_t>(esp_timer_get_time() - decodeStart);
          const uint32_t previous = decodeUs_.load();
          decodeUs_.store(previous ? (previous * 7 + took) / 8 : took);
        }
        if (result.bytesConsumed == 0) break;
        offset += result.bytesConsumed;
        if (result.status == mp3::DecodeStatus::NeedMoreData) break;
        if (result.status != mp3::DecodeStatus::Ok) continue;
        ++decodedFrames;

        // I2S is set up from the first decoded frame, not from the headers, and torn down and
        // rebuilt if the station changes rate or channel count mid-stream.
        if (result.sampleRateHz != sampleRateHz_ || result.channels != channels_) {
          closeStream();
          sampleRateHz_ = result.sampleRateHz;
          channels_ = result.channels;
          i2s_config_t config = {};
          config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
          config.sample_rate = static_cast<uint32_t>(sampleRateHz_);
          config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
          config.channel_format = channels_ == 1 ? I2S_CHANNEL_FMT_ONLY_LEFT
                                                 : I2S_CHANNEL_FMT_RIGHT_LEFT;
          config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
          config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
          config.dma_buf_count = kDmaBufferCount;
          config.dma_buf_len = kDmaBufferFrames;
          config.use_apll = false;
          if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) == ESP_OK) {
            i2s_pin_config_t pins = {};
            // mck_io_num is the first field, so zero-initialising it means GPIO 0, not "unused":
            // the driver only skips the MCLK routing for -1. Left at 0 the S3 clocks its boot
            // strapping pin for as long as the radio plays.
            pins.mck_io_num = I2S_PIN_NO_CHANGE;
            pins.bck_io_num = pinBclk_;
            pins.ws_io_num = pinLrclk_;
            pins.data_out_num = pinDout_;
            pins.data_in_num = I2S_PIN_NO_CHANGE;
            i2s_set_pin(I2S_NUM_0, &pins);
            i2sStarted_ = true;
          } else {
            publishError("could not start the I2S output");
            stopRequested_.store(true);
            break;
          }
        }

        const int gain = volume_.load();
        if (gain < 100) {
          const int count = result.samples * result.channels;
          for (int i = 0; i < count; ++i)
            pcm[i] = static_cast<int16_t>((static_cast<int32_t>(pcm[i]) * gain) / 100);
        }
        // Blocks until the DMA queue has room, which is what paces the whole loop to real time.
        std::size_t written = 0;
        i2s_write(I2S_NUM_0, pcm.data(),
                  static_cast<std::size_t>(result.samples) * result.channels * sizeof(int16_t),
                  &written, portMAX_DELAY);

        if (playStartMs == 0) playStartMs = millis();
        deliveredSamples += result.samples;
        const int64_t deliveredMs = sampleRateHz_ > 0
                                        ? (deliveredSamples * 1000) / sampleRateHz_
                                        : 0;
        const int64_t elapsedMs = static_cast<int64_t>(millis() - playStartMs);
        const int64_t slackMs = (kDmaBufferCount * kDmaBufferFrames * 1000LL) /
                                (sampleRateHz_ > 0 ? sampleRateHz_ : 44100);
        // Wall clock has run ahead of the audio we handed over by more than the DMA queue holds, so
        // the speaker must have gone silent. Count it and restart the comparison.
        if (elapsedMs - deliveredMs > slackMs) {
          underruns_.fetch_add(1);
          playStartMs = millis();
          deliveredSamples = 0;
        }
      }
      if (offset > consumed) {
        decodedAnything = true;
        consumed = offset;
      }
      // Decoded bytes are only dropped in large batches; erasing from the front after every frame
      // would memmove tens of kilobytes per frame.
      if (consumed >= kCompactAtBytes) {
        input.erase(input.begin(), input.begin() + consumed);
        consumed = 0;
      }
      bufferBytes_.store(static_cast<uint32_t>(input.size() - consumed));
      if (!decodedAnything && bytesSeen > kUndecodableAfterBytes) {
        publishError("this stream is not MPEG-1 Layer III audio");
        stopRequested_.store(true);
      }
      // Decoder fell behind the network: skip forward and lose audio rather than let the buffer
      // grow without bound.
      if (input.size() - consumed > kInputBufferBytes)
        consumed = input.size() - kInputBufferBytes;
    }

#ifdef AWTRIX_HEAP_PROBE
    {
      const probe::Watch w = probe::watchEnd();
      logf("probe radio end: low %u max %u n %u", static_cast<unsigned>(w.lowWater),
           static_cast<unsigned>(w.maxAlloc), static_cast<unsigned>(w.count));
    }
#endif

    const bool switched = urlSeq_.load() != seq;
    closeStream();
    if (client) client->stop();
    playing_.store(false);
    if (!stopRequested_.load() && !switched) {
      vTaskDelay(pdMS_TO_TICKS(kBackoffMs[0]));
    }
  }
}

void RadioAudioEsp32::closeStream() {
  if (!i2sStarted_) return;
  i2s_driver_uninstall(I2S_NUM_0);
  i2sStarted_ = false;
  sampleRateHz_ = 0;
  channels_ = 0;
}

}

#endif
