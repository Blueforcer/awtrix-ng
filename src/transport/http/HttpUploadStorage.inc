// ESP image header: byte 0 is the magic, bytes 12 and 13 hold the chip id, little endian. An app
// image carries an esp_app_desc_t at offset 32; a usb-*.bin install image starts with the
// bootloader, which has none - and on the ESP32, whose bootloader sits at 0x1000, with erased
// padding before it.
constexpr size_t kEspImageHeaderBytes = 36;
constexpr size_t kEspImageChipIdOffset = 12;
constexpr size_t kEspAppDescOffset = 32;
constexpr uint8_t kEspImageMagic = 0xE9;
constexpr uint8_t kEspFlashErasedByte = 0xFF;
constexpr uint32_t kEspAppDescMagic = 0xABCD5432;
#if defined(AWTRIX_SOC_ESP32S3)
constexpr uint16_t kExpectedChipId = 0x0009;
#else
constexpr uint16_t kExpectedChipId = 0x0000;
#endif

// The S3 ships as two images - octal PSRAM and quad - and the header above cannot tell them apart:
// same chip id, same app descriptor. Getting it wrong is not symmetric. The octal image on a quad
// board boots with its PSRAM invisible, but the quad image on an octal board fails its PSRAM init
// and aborts, so the device boot-loops until someone reaches it with a USB cable.
//
// Every build therefore carries this marker in its .rodata, and an upload is scanned for it. The
// variant follows the prefix in the same string so exactly one copy of the prefix exists in the
// binary: a second one would be found first about half the time, with whatever bytes happen to
// follow it read as the variant.
#if defined(AWTRIX_SOC_ESP32S3)
#if defined(CONFIG_SPIRAM_MODE_QUAD)
#define AWTRIX_IMAGE_VARIANT "esp32s3-psram-quad"
#else
#define AWTRIX_IMAGE_VARIANT "esp32s3-psram-octal"
#endif
#else
#define AWTRIX_IMAGE_VARIANT "esp32"
#endif
#define AWTRIX_IMAGE_MARKER_PREFIX "AWTRIX-NG-IMAGE/"
// Referenced by the scan below, which is what keeps it in the binary; `used` covers the day that
// reference moves behind a build flag. The prefix length comes from a sizeof, which emits nothing:
// this array is the only copy of those bytes in the image.
__attribute__((used)) const char kImageMarker[] =
    AWTRIX_IMAGE_MARKER_PREFIX AWTRIX_IMAGE_VARIANT;
constexpr size_t kImageMarkerPrefixLen = sizeof(AWTRIX_IMAGE_MARKER_PREFIX) - 1;
constexpr const char* kImageVariant = kImageMarker + kImageMarkerPrefixLen;
constexpr size_t kImageVariantMaxLen = 31;

// A friendlier name for the variant an image announces, for the message the upload is refused with.
const char* variantName(const std::string& variant) {
  if (variant == "esp32") return "ESP32";
  if (variant == "esp32s3-psram-octal") return "ESP32-S3 octal";
  if (variant == "esp32s3-psram-quad") return "ESP32-S3 quad";
  return "unknown board";
}

const char* chipIdName(uint16_t id) {
  switch (id) {
    case 0x0000: return "ESP32";
    case 0x0002: return "ESP32-S2";
    case 0x0005: return "ESP32-C3";
    case 0x0009: return "ESP32-S3";
    case 0x000D: return "ESP32-C6";
    case 0x0010: return "ESP32-H2";
    default:     return "an unknown chip";
  }
}


class LittleFsRestoreSink : public backup::FsRestoreSink {
 public:
  LittleFsRestoreSink(DeviceConfig& live, StateStore* state, std::function<void()> onConfigChanged,
                      std::function<void(const std::string&)> release)
      : backup::FsRestoreSink(live, state, std::move(onConfigChanged)),
        release_(std::move(release)) {}
  bool beginFile(const std::string& path, std::string& err) override {
    abortFile();
    const String p(path.c_str());
    const int slash = p.lastIndexOf('/');
    if (slash > 0) {
      const String dir = p.substring(0, slash);
      if (!LittleFS.exists(dir) && !LittleFS.mkdir(dir)) {
        err = "mkdir failed";
        return false;
      }
    }
    // A rejected content/CRC check removes staging only. The previous asset remains
    // available until this entry has been completely validated by RestoreApplier.
    file_ = LittleFS.open(kTemporary, "w");
    curPath_ = path;
    if (!file_) {
      err = "open failed";
      abortFile();
      return false;
    }
    return true;
  }
  bool writeFile(const uint8_t* data, std::size_t n) override {
    return file_ && file_.write(data, n) == n;
  }
  bool endFile() override {
    const bool opened = static_cast<bool>(file_);
    if (file_) file_.close();
    if (opened && release_) release_(curPath_);
    const bool published = opened && LittleFS.rename(kTemporary, curPath_.c_str());
    if (!published) LittleFS.remove(kTemporary);
    curPath_.clear();
    return published;
  }
  void abortFile() override {
    if (file_) file_.close();
    LittleFS.remove(kTemporary);
    curPath_.clear();
  }

 private:
  static constexpr const char* kTemporary = "/.http-restore.tmp";
  std::function<void(const std::string&)> release_;
  File file_;
  std::string curPath_;
};
