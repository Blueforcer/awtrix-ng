#include "platform/tc002/update/PackageVerifier.h"

#include "platform/posix/Files.h"
#include "platform/tc002/contract/release_slot.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace awtrix {
namespace tc002 {
namespace update {
namespace {

class Failure : public std::runtime_error {
 public:
  Failure(const char* code, const std::string& message) : std::runtime_error(message), code_(code) {}
  const char* code() const { return code_; }
 private:
  const char* code_;
};

[[noreturn]] void fail(const char* code, const char* message) { throw Failure(code, message); }
[[noreturn]] void fail(const FormatProblem& problem) { throw Failure(problem.code, problem.message); }

bool safePath(const std::string& path) {
  return !path.empty() && path.find('\0') == std::string::npos;
}

void readExact(int fd, unsigned char* data, std::size_t size) {
  if (!posix::readAll(fd, data, size)) fail("format", "package truncated or unreadable");
}

void writeExact(int fd, const unsigned char* data, std::size_t size) {
  if (!posix::writeAll(fd, data, size)) fail("stage", "staging write failed");
}

using Digest = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

class Stage {
 public:
  explicit Stage(const std::string& path)
      : file_(-1) {
    if (path.empty()) return;
    if (!safePath(path)) fail("stage", "invalid staging directory");
    directory_ = posix::openPrivateDirectoryAt(AT_FDCWD, path, false);
    if (!directory_.valid())
      fail("stage", "staging directory must be caller-owned with mode 0700");
    std::array<unsigned char, 16> random {};
    if (RAND_bytes(random.data(), static_cast<int>(random.size())) != 1)
      fail("stage", "cannot generate staging file name");
    temporary_ = ".pending-" + posix::hexBytes(random.data(), random.size());
    file_ = ::openat(directory_.get(), temporary_.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (file_ < 0) fail("stage", "cannot create staging file");
  }
  ~Stage() {
    if (file_ >= 0) ::close(file_);
    if (!temporary_.empty()) ::unlinkat(directory_.get(), temporary_.c_str(), 0);
  }
  Stage(const Stage&) = delete;
  Stage& operator=(const Stage&) = delete;
  void write(const unsigned char* bytes, std::size_t size) {
    if (file_ >= 0) writeExact(file_, bytes, size);
  }
  bool enabled() const { return file_ >= 0; }
  void commit(const std::string& name) {
    if (::fsync(file_) != 0) fail("stage", "staging file sync failed");
    // linkat publishes a complete inode atomically and refuses any existing
    // destination (including symlinks). No overwrite race or extraction path.
    if (::linkat(directory_.get(), temporary_.c_str(), directory_.get(), name.c_str(), 0) != 0)
      fail("stage", "staging publication failed; destination may already exist");
    if (::fsync(directory_.get()) != 0)
      fail("stage", "staging published but directory durability is uncertain");
    if (::unlinkat(directory_.get(), temporary_.c_str(), 0) != 0)
      fail("stage", "staging published but temporary cleanup failed");
    temporary_.clear();
    if (::fsync(directory_.get()) != 0)
      fail("stage", "staging published but cleanup durability is uncertain");
  }
 private:
  posix::UniqueFd directory_;
  int file_;
  std::string temporary_;
};
}  // namespace

Result verify(const std::string& package, const Policy& policy, const std::string& stageDirectory) {
  Result result;
  try {
    if (!allowedTarget(policy.expectedTarget))
      fail("policy", "only awtrix-ng:tc002 or an explicit experimental target is supported");
    if (policy.maxPayloadBytes == 0 || policy.maxPayloadBytes > kMaxPayloadBytes)
      fail("policy", "invalid payload resource limit");
    if (!safePath(package) || (!stageDirectory.empty() && !safePath(stageDirectory)))
      fail("path", "invalid file path");
    posix::UniqueFd input(::open(package.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    struct stat status {};
    if (input.get() < 0 || ::fstat(input.get(), &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_size < static_cast<off_t>(kHeaderBytes + kManifestDigestBytes))
      fail("path", "package must be a non-symlink regular file");
    CheckedManifest manifest;
    FormatProblem problem;
    if (!readCheckedManifest(input.get(), static_cast<std::uint64_t>(status.st_size),
                             policy.maxPayloadBytes, manifest, problem)) fail(problem);
    const PackageHeader& header = manifest.header;
    if (header.counter <= policy.currentCounter) fail("counter", "release counter is not newer");
    if (!allowedTarget(header.target) || header.target != policy.expectedTarget)
      fail("target", "package target does not match the target policy");
    if (::lseek(input.get(), static_cast<off_t>(header.payloadOffset), SEEK_SET) < 0)
      fail("format", "package truncated or unreadable");
    Digest payloadContext(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!payloadContext || EVP_DigestInit_ex(payloadContext.get(), EVP_sha256(), nullptr) != 1)
      fail("digest", "cannot initialize payload digest");
    Stage stage(stageDirectory);
    stage.write(manifest.bytes.data(), static_cast<std::size_t>(header.payloadOffset));
    std::array<unsigned char, 64 * 1024> buffer {};
    std::uint64_t remaining = header.payloadBytes;
    bool first = true;
    while (remaining) {
      const auto amount = static_cast<std::size_t>(remaining < buffer.size() ? remaining : buffer.size());
      readExact(input.get(), buffer.data(), amount);
      if (first && productionTarget(header.target) &&
          (amount < RELEASE_SLOT_SUPERBLOCK_BYTES ||
           !release_slot_image_valid(buffer.data(), header.payloadBytes)))
        fail("format", "the payload is not a release image");
      first = false;
      if (EVP_DigestUpdate(payloadContext.get(), buffer.data(), amount) != 1)
        fail("digest", "payload digest failed");
      stage.write(buffer.data(), amount);
      remaining -= amount;
    }
    unsigned char extra = 0;
    ssize_t eof;
    do { eof = ::read(input.get(), &extra, 1); } while (eof < 0 && errno == EINTR);
    if (eof != 0) fail("format", "trailing data or read failure");
    std::array<unsigned char, 32> digest {};
    unsigned int written = 0;
    if (EVP_DigestFinal_ex(payloadContext.get(), digest.data(), &written) != 1 || written != digest.size() ||
        CRYPTO_memcmp(digest.data(), manifest.bytes.data() + 36, digest.size()) != 0)
      fail("digest", "payload digest mismatch");
    result.target = header.target;
    result.release = header.release;
    result.counter = header.counter;
    result.payloadBytes = header.payloadBytes;
    result.payloadSha256 = header.payloadSha256;
    if (stage.enabled()) {
      const auto name = std::to_string(header.counter) + "-" + result.payloadSha256 + ".awup";
      stage.commit(name);
      result.stagedFile = stageDirectory + "/" + name;
    }
    result.ok = true;
  } catch (const Failure& error) {
    result = Result{};
    result.code = error.code();
    result.error = error.what();
  } catch (const std::exception& error) {
    result = Result{};
    result.code = "stage";
    result.error = error.what();
  }
  return result;
}

}
}
}
