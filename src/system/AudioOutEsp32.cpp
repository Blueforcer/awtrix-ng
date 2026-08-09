#include "system/AudioOutEsp32.h"

#if defined(AWTRIX_SOC_ESP32S3)

#include <LittleFS.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#include <cstdlib>
#include <memory>
#include <vector>

#include "core/audio/Mp3FileDecoder.h"
#include "core/radio/PlaylistParser.h"
#include "core/sound/SoundMp3.h"
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

// Held in PSRAM, which is what makes 64 KB affordable against a bursty station.
constexpr std::size_t kInputBufferBytes = 64 * 1024;
// Without a preroll, playback starts and immediately stutters while the buffer fills.
constexpr std::size_t kPrerollBytes = 16 * 1024;
constexpr std::size_t kUndecodableAfterBytes = 32 * 1024;
// Capped per pass so the loop keeps reading the socket; flat out here starves the input.
constexpr int kFramesPerPass = 2;
constexpr std::size_t kCompactAtBytes = 32 * 1024;

constexpr int kMaxRedirects = 3;
constexpr uint32_t kConnectTimeoutMs = 8000;
constexpr uint32_t kReadTimeoutMs = 8000;
constexpr uint32_t kBackoffMs[] = {2000, 5000, 15000};

const char* kUserAgent = "AWTRIX-NG";

}

bool AudioOutEsp32::usable(int pinBclk, int pinLrclk, int pinDout) {
  if (pinBclk < 0 || pinLrclk < 0 || pinDout < 0) return false;
  return psramFound();
}

// Pinned to internal RAM: PSRAM latency on the per-frame decoder state costs decode time.
void* AudioOutEsp32::operator new(std::size_t bytes) {
  if (void* p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)) return p;
  return std::malloc(bytes);
}

void AudioOutEsp32::operator delete(void* p) { std::free(p); }

AudioOutEsp32::AudioOutEsp32(CoreEngine& engine, int pinBclk, int pinLrclk, int pinDout)
    : engine_(engine), pinBclk_(pinBclk), pinLrclk_(pinLrclk), pinDout_(pinDout) {
  lock_ = xSemaphoreCreateMutex();
}

AudioOutEsp32::~AudioOutEsp32() {
  stopStream();
  if (task_) vTaskDelete(task_);
  if (lock_) vSemaphoreDelete(lock_);
}

bool AudioOutEsp32::ensureTask() {
  if (task_) return true;
  return xTaskCreatePinnedToCore(taskEntry, "audio", kTaskStackBytes, this, kTaskPriority, &task_,
                                 kTaskCore) == pdPASS;
}

DispatchResult AudioOutEsp32::playStream(const std::string& url, const std::string& label,
                                         DispatchDetail& detail) {
  radio::Url parsed;
  if (!radio::parseUrl(url, parsed)) {
    detail = {"url", "not a usable http or https URL"};
    return DispatchResult::ValidationError;
  }

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
    // The switch-station signal: the task compares it every pass.
    urlSeq_.fetch_add(1);
    xSemaphoreGive(lock_);
  }
  stopRequested_.store(false);

  if (!ensureTask()) {
    detail = {"", "could not start the audio task"};
    return DispatchResult::Failed;
  }
  return DispatchResult::Ok;
}

bool AudioOutEsp32::playMp3(const std::string& path) {
  if (xSemaphoreTake(lock_, portMAX_DELAY) == pdTRUE) {
    pendingMp3_ = path;
    pendingMp3Name_ = sound::mp3NameFor(path);
    mp3Stop_.store(false);
    mp3Seq_.fetch_add(1);
    xSemaphoreGive(lock_);
  }
  return ensureTask();
}

void AudioOutEsp32::stopStream() {
  stopRequested_.store(true);
  playing_.store(false);
}

void AudioOutEsp32::setSoundVolume(uint8_t percent) {
  soundVolume_.store(percent > 100 ? 100 : percent);
}

void AudioOutEsp32::setStreamVolume(uint8_t percent) {
  streamVolume_.store(percent > 100 ? 100 : percent);
}

void AudioOutEsp32::publishTitle(const std::string& title) {
  if (xSemaphoreTake(lock_, portMAX_DELAY) != pdTRUE) return;
  pendingTitle_ = title;
  xSemaphoreGive(lock_);
  handoffSeq_.fetch_add(1);
}

void AudioOutEsp32::publishError(const std::string& message) {
  if (xSemaphoreTake(lock_, portMAX_DELAY) != pdTRUE) return;
  pendingError_ = message;
  xSemaphoreGive(lock_);
  handoffSeq_.fetch_add(1);
}

void AudioOutEsp32::publishMp3Started(const std::string& name) {
  if (xSemaphoreTake(lock_, portMAX_DELAY) != pdTRUE) return;
  pendingMp3Started_ = name;
  pendingMp3Ended_ = false;
  xSemaphoreGive(lock_);
  handoffSeq_.fetch_add(1);
}

void AudioOutEsp32::publishMp3Ended() {
  if (xSemaphoreTake(lock_, portMAX_DELAY) != pdTRUE) return;
  pendingMp3Ended_ = true;
  xSemaphoreGive(lock_);
  handoffSeq_.fetch_add(1);
}

// The audio task parks state here rather than touch the engine.
void AudioOutEsp32::tick(int64_t nowMs) {
  const uint32_t seq = handoffSeq_.load();
  if (seq == seenSeq_) return;
  seenSeq_ = seq;

  std::string title;
  std::string error;
  std::string mp3Started;
  bool mp3Ended = false;
  // Never block the main loop on the audio task's lock; rewind so the next tick retries.
  if (xSemaphoreTake(lock_, 0) != pdTRUE) {
    seenSeq_ = seq - 1;
    return;
  }
  title.swap(pendingTitle_);
  error.swap(pendingError_);
  mp3Started.swap(pendingMp3Started_);
  mp3Ended = pendingMp3Ended_;
  pendingMp3Ended_ = false;
  xSemaphoreGive(lock_);

  RuntimeState& runtime = engine_.state().runtime();

  if (!mp3Started.empty() || mp3Ended) {
    runtime.mp3Playing = !mp3Ended && !mp3Started.empty();
    runtime.mp3Name = runtime.mp3Playing ? mp3Started : "";
    engine_.state().emit(StateEvent::RadioChanged);
  }

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

void AudioOutEsp32::taskEntry(void* self) {
  static_cast<AudioOutEsp32*>(self)->run();
}

bool AudioOutEsp32::writeDecodedFrame(const mp3::DecodeResult& result, int16_t* pcm) {
  if (result.sampleRateHz != sampleRateHz_ || result.channels != channels_) {
    closeStream();
    i2s_config_t config = {};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    config.sample_rate = static_cast<uint32_t>(result.sampleRateHz);
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = result.channels == 1 ? I2S_CHANNEL_FMT_ONLY_LEFT
                                                 : I2S_CHANNEL_FMT_RIGHT_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = kDmaBufferCount;
    config.dma_buf_len = kDmaBufferFrames;
    config.use_apll = false;
    // Set only once the driver is up, or a failed install would let later frames write into
    // a driver that is not there.
    if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) != ESP_OK) return false;
    i2s_pin_config_t pins = {};
    pins.bck_io_num = pinBclk_;
    pins.ws_io_num = pinLrclk_;
    pins.data_out_num = pinDout_;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    i2s_set_pin(I2S_NUM_0, &pins);
    sampleRateHz_ = result.sampleRateHz;
    channels_ = result.channels;
    i2sStarted_ = true;
  }

  const int gain = mp3Playing_.load() ? soundVolume_.load() : streamVolume_.load();
  if (gain < 100) {
    const int count = result.samples * result.channels;
    for (int i = 0; i < count; ++i)
      pcm[i] = static_cast<int16_t>((static_cast<int32_t>(pcm[i]) * gain) / 100);
  }
  // Blocks until the DMA queue has room, which paces the whole loop to real time.
  std::size_t written = 0;
  i2s_write(I2S_NUM_0, pcm,
            static_cast<std::size_t>(result.samples) * result.channels * sizeof(int16_t),
            &written, portMAX_DELAY);
  return true;
}

namespace {
int readMp3Bytes(void* ctx, uint8_t* dst, std::size_t max) {
  return static_cast<File*>(ctx)->read(dst, max);
}
}

void AudioOutEsp32::playMp3File(const std::string& path, const std::string& name, int16_t* pcm) {
  File file = LittleFS.open(path.c_str(), "r");
  if (!file) {
    logf("MP3 %s disappeared before playback", path.c_str());
    return;
  }

  publishMp3Started(name);
  mp3Playing_.store(true);
  decoder_.reset();
  mp3::Mp3FileDecoder walk(decoder_);
  mp3::DecodeResult result;
  const uint32_t startSeq = mp3Seq_.load();
  bool decodeFailed = false;
  bool i2sFailed = false;
  bool finished = false;

  while (!mp3Stop_.load() && mp3Seq_.load() == startSeq) {
    const mp3::Mp3FileDecoder::Step step = walk.next(readMp3Bytes, &file, pcm, result);
    if (step == mp3::Mp3FileDecoder::Step::Done) {
      finished = true;
      break;
    }
    if (step == mp3::Mp3FileDecoder::Step::Error) {
      decodeFailed = true;
      break;
    }
    if (!writeDecodedFrame(result, pcm)) {
      i2sFailed = true;
      break;
    }
  }

  file.close();
  decoder_.reset();
  // A finished MP3 still has up to a DMA queue in flight.
  if (finished && i2sStarted_ && sampleRateHz_ > 0)
    vTaskDelay(pdMS_TO_TICKS((kDmaBufferCount * kDmaBufferFrames * 1000) / sampleRateHz_));
  // A starved I2S does not go quiet: the driver keeps clocking its descriptors, so the tail
  // would repeat for the whole reconnect.
  closeStream();
  mp3Playing_.store(false);
  if (decodeFailed) logf("MP3 %s is not playable MPEG-1 Layer III audio", path.c_str());
  if (i2sFailed) logf("could not start the I2S output for MP3 %s", path.c_str());
  publishMp3Ended();
}

// The audio task: core 1, priority 2, never returns. Nothing here may touch the engine.
void AudioOutEsp32::run() {
  std::vector<uint8_t> input;
  input.reserve(kInputBufferBytes + kCompactAtBytes);
  std::vector<int16_t> pcm(mp3::kMaxPcmPerFrame);
  std::unique_ptr<WiFiClient> plain;
  std::unique_ptr<WiFiClientSecure> secure;
  Client* client = nullptr;
  int attempt = 0;

  auto waitMs = [this](uint32_t ms) {
    for (uint32_t waited = 0; waited < ms && mp3Seq_.load() == mp3SeenSeq_; waited += 100)
      vTaskDelay(pdMS_TO_TICKS(100));
  };

  for (;;) {
    if (mp3Seq_.load() != mp3SeenSeq_) {
      std::string path;
      std::string name;
      if (xSemaphoreTake(lock_, portMAX_DELAY) == pdTRUE) {
        path = pendingMp3_;
        name = pendingMp3Name_;
        mp3SeenSeq_ = mp3Seq_.load();
        xSemaphoreGive(lock_);
      }
      if (!path.empty()) playMp3File(path, name, pcm.data());
      continue;
    }

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

    for (int redirect = 0;
         redirect <= kMaxRedirects && !connected && mp3Seq_.load() == mp3SeenSeq_; ++redirect) {
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

    if (mp3Seq_.load() != mp3SeenSeq_) {
      if (client) client->stop();
      continue;
    }

    if (!connected) {
      const uint32_t wait = kBackoffMs[attempt < 3 ? attempt : 2];
      if (attempt < 3) ++attempt;
      publishError("could not connect to the station");
      waitMs(wait);
      continue;
    }

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
        waitMs(kBackoffMs[2]);
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
    while (!stopRequested_.load() && urlSeq_.load() == seq &&
           mp3Seq_.load() == mp3SeenSeq_) {
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
      // A read is cut to the room left, not to the chunk size: overshooting kInputBufferBytes could
      // only be undone by dropping compressed bytes the decoder has not seen, which punches holes
      // into the bitstream. What does not fit stays in the socket.
      bool received = false;
      while (input.size() - consumed < kInputBufferBytes) {
        const std::size_t room = kInputBufferBytes - (input.size() - consumed);
        const int available = client->available();
        if (available <= 0) break;
        int want = available > static_cast<int>(sizeof(chunk)) ? static_cast<int>(sizeof(chunk))
                                                               : available;
        if (static_cast<std::size_t>(want) > room) want = static_cast<int>(room);
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

        if (!writeDecodedFrame(result, pcm.data())) {
          publishError("could not start the I2S output");
          stopRequested_.store(true);
          break;
        }

        if (playStartMs == 0) playStartMs = millis();
        deliveredSamples += result.samples;
        const int64_t deliveredMs = sampleRateHz_ > 0
                                        ? (deliveredSamples * 1000) / sampleRateHz_
                                        : 0;
        const int64_t elapsedMs = static_cast<int64_t>(millis() - playStartMs);
        const int64_t slackMs = (kDmaBufferCount * kDmaBufferFrames * 1000LL) /
                                (sampleRateHz_ > 0 ? sampleRateHz_ : 44100);
        // Wall clock ahead of the audio handed over by more than the queue holds: the speaker went
        // silent.
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
      // Dropped in batches; erasing after every frame would memmove tens of kilobytes per frame.
      if (consumed >= kCompactAtBytes) {
        input.erase(input.begin(), input.begin() + consumed);
        consumed = 0;
      }
      bufferBytes_.store(static_cast<uint32_t>(input.size() - consumed));
      if (!decodedAnything && bytesSeen > kUndecodableAfterBytes) {
        publishError("this stream is not MPEG-1 Layer III audio");
        stopRequested_.store(true);
      }
    }

#ifdef AWTRIX_HEAP_PROBE
    {
      const probe::Watch w = probe::watchEnd();
      logf("probe radio end: low %u max %u n %u", static_cast<unsigned>(w.lowWater),
           static_cast<unsigned>(w.maxAlloc), static_cast<unsigned>(w.count));
    }
#endif

    const bool switched = urlSeq_.load() != seq || mp3Seq_.load() != mp3SeenSeq_;
    closeStream();
    if (client) client->stop();
    playing_.store(false);
    if (!stopRequested_.load() && !switched) {
      waitMs(kBackoffMs[0]);
    }
  }
}

void AudioOutEsp32::closeStream() {
  if (!i2sStarted_) return;
  i2s_driver_uninstall(I2S_NUM_0);
  i2sStarted_ = false;
  sampleRateHz_ = 0;
  channels_ = 0;
}

}

#endif
