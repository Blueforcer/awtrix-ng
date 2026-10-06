class RawWebServer : public WebServer {
 public:
  using WebServer::WebServer;
  void setRawReadTimeout(unsigned long ms) { _currentClient.Stream::setTimeout(ms); }

  // WebServer::send() for a std::string body.
  void sendBody(int status, const char* contentType, const std::string& body) {
    String header;
    _prepareHeader(header, status, contentType, body.size());
    _currentClientWrite(header.c_str(), header.length());
    if (!body.empty()) sendContent(body.data(), body.size());
  }

  // WebServer serves one client at a time. Browsers like to open a socket and send nothing, which
  // would stall every other request, so drop a silent client as soon as someone else is waiting.
  void handleClient() override {
    if (_currentStatus == HC_WAIT_READ && !_currentClient.available() &&
        millis() - _statusChange > kSilentClientGraceMs && _server.hasClient()) {
      _currentClient.stop();
    }
    WebServer::handleClient();
  }
};

const char* methodName(HTTPMethod m) {
  switch (m) {
    case HTTP_GET: return "GET";
    case HTTP_POST: return "POST";
    case HTTP_PUT: return "PUT";
    case HTTP_PATCH: return "PATCH";
    case HTTP_DELETE: return "DELETE";
    case HTTP_HEAD: return "HEAD";
    case HTTP_OPTIONS: return "OPTIONS";
    default: return "ANY";
  }
}

bool folderEmpty(const std::string& dir) {
  DIR* folder = ::opendir(fs::vfsPath(dir).c_str());
  if (!folder) return false;
  bool empty = true;
  while (const dirent* entry = ::readdir(folder)) {
    if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) continue;
    empty = false;
    break;
  }
  ::closedir(folder);
  return empty;
}

// SHA-256 of a file in 256-byte reads; size is what was read.
__attribute__((noinline)) bool sha256File(const std::string& path, std::string& hex, uint32_t& size) {
  const int fd = fs::openRead(path);
  if (fd < 0) return false;
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  bool ok = mbedtls_sha256_starts_ret(&sha, 0) == 0;
  uint8_t buf[256];
  size = 0;
  while (ok) {
    const ssize_t n = ::read(fd, buf, sizeof(buf));
    if (n <= 0) {
      ok = n == 0;
      break;
    }
    ok = mbedtls_sha256_update_ret(&sha, buf, static_cast<size_t>(n)) == 0;
    size += static_cast<uint32_t>(n);
  }
  uint8_t digest[32];
  ok = ok && mbedtls_sha256_finish_ret(&sha, digest) == 0;
  mbedtls_sha256_free(&sha);
  ::close(fd);
  if (!ok) return false;
  static const char kHex[] = "0123456789abcdef";
  hex.clear();
  hex.reserve(2 * sizeof(digest));
  for (const uint8_t b : digest) {
    hex += kHex[b >> 4];
    hex += kHex[b & 0x0f];
  }
  return true;
}


constexpr const char* kAssetUploadTemporary = "/.http-asset.tmp";

bool makeDirectory(const std::string& directory, bool& created) {
  created = false;
  if (LittleFS.exists(directory.c_str())) return true;
  created = LittleFS.mkdir(directory.c_str());
  return created;
}

void pruneDirectory(const std::string& directory) {
  if (folderEmpty(directory)) LittleFS.rmdir(directory.c_str());
}

class EspFiles final : public api::Files {
 public:
  EspFiles(const api::scriptsounds::InstalledScripts& installed,
           const std::function<void(const std::string&)>& release,
           const std::function<void()>& changed)
      : Files(installed, iconorigins::storage()), release_(release), changed_(changed) {}
  void list(const std::string& directory, const Visit& visit) override {
    if (directory.empty()) return;
    if (DIR* folder = ::opendir(fs::vfsPath(directory).c_str())) {
      while (const dirent* entry = ::readdir(folder)) {
        if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
        const bool isDirectory = entry->d_type == DT_DIR;
        const long size = isDirectory ? 0 : fs::fileSize(directory + "/" + entry->d_name);
        if (size >= 0) visit({entry->d_name, static_cast<uint64_t>(size), isDirectory});
      }
      ::closedir(folder);
    }
  }
  bool exists(const std::string& path) override { return fs::isFile(path); }
  bool read(const std::string& path, std::string& content) override {
    media::PodBuffer<uint8_t> bytes;
    if (!media::readAsset(path, bytes)) return false;
    content.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
  }
  bool write(const std::string& path, const std::string& bytes) override {
    return document::write(path.c_str(), bytes);
  }
  bool remove(const std::string& path) override { return LittleFS.remove(path.c_str()); }
  bool rename(const std::string& from, const std::string& to) override {
    return LittleFS.rename(from.c_str(), to.c_str());
  }
  bool mkdir(const std::string& directory, bool& created) override { return makeDirectory(directory, created); }
  void prune(const std::string& directory) override { pruneDirectory(directory); }
  void usage(uint64_t& used, uint64_t& total) override {
    std::size_t totalBytes = 0, usedBytes = 0;
    fs::usage(totalBytes, usedBytes);
    used = usedBytes; total = totalBytes;
  }
  bool etag(const std::string& path, std::string& value) override {
    const int fd = fs::openRead(path);
    if (fd < 0) return false;
    api::Etag tag;
    uint8_t buffer[256];
    ssize_t size;
    while ((size = ::read(fd, buffer, sizeof(buffer))) > 0) tag.append(buffer, size);
    ::close(fd);
    if (size < 0) return false;
    value = tag.value();
    return true;
  }
  bool sha256(const std::string& path, std::string& digest, uint64_t& size) override {
    uint32_t bytes = 0;
    const bool ok = sha256File(path, digest, bytes);
    size = bytes;
    if (!ok) logf("http: could not read %s", path.c_str());
    return ok;
  }
  void release(const std::string& path) override { if (release_) release_(path); }
  void changed() override { if (changed_) changed_(); }
 private:
  const std::function<void(const std::string&)>& release_;
  const std::function<void()>& changed_;
};

class EspReply final : public api::Reply {
 public:
  explicit EspReply(WebServer& server) : server_(server) {}
  void send(const api::HttpResult& result, bool streaming = false) override {
    if (streaming) {
      server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
      server_.send(result.status, result.contentType, "");
    } else static_cast<RawWebServer&>(server_).sendBody(result.status, result.contentType, result.body);
  }
  void header(const char* name, const std::string& value) override { server_.sendHeader(name, value.c_str()); }
  void chunk(const char* data, std::size_t size) override {
    // Batch listing entries: an individual sendContent costs about 9 ms on the device.
    if (batch_.capacity() < kListBatchBytes) batch_.reserve(kListBatchBytes + kListEntryReserveBytes);
    batch_.append(data, size);
    if (size == 0 || batch_.size() >= kListBatchBytes) {
      if (!batch_.empty()) server_.sendContent(batch_.data(), batch_.size());
      batch_.clear();
    }
    if (size == 0) server_.sendContent("");
  }
  bool sendFile(const std::string& path, const char* contentType) override {
    File file = LittleFS.open(path.c_str(), "r");
    if (!file || file.isDirectory()) { if (file) file.close(); return false; }
    server_.streamFile(file, contentType);
    file.close();
    return true;
  }
 private:
  WebServer& server_;
  std::string batch_;
};
