#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "core/render/Canvas.h"
#include "core/render/Font.h"
#include "platform/linux/LinuxDeviceFacts.h"
#include "platform/tc002/contract/SupervisorProtocol.h"

namespace httplib { struct Request; struct Response; class ContentReader; }

namespace awtrix {

struct Tc002UpdateOptions {
  std::string statePath;
  std::string releaseRoot;
  // Where packages are staged; awtrix-tc002d passes its own, the directory it installs from.
  std::string workDirectory;
};

std::uint64_t tc002ReleaseCounter(const std::string& releaseRoot);
std::uint64_t tc002AcceptedCounter(const std::string& statePath, bool& readable);

// The TC002 web update in the runtime (docs/developers/tc002/index.md, Web update, flow steps 1-3
// and 8).
// upload() runs on a listener thread; every other call belongs to the main loop. As a device facts
// source it names the update image and adds the "update" member.
class Tc002Update : public DeviceFactsSource {
 public:
  static constexpr const char* kImageName = "awtrix-ng-tc002.awup";
  static constexpr const char* kPackageName = "package.awup";
  static constexpr std::uint64_t kMaxPackageBytes = 8ULL << 20;
  static constexpr std::uint64_t kMaxRequestBytes = kMaxPackageBytes + (64ULL << 10);
  static constexpr std::uint64_t kRamReserveBytes = 4ULL << 20;
  static constexpr int64_t kHandoffTimeoutMs = 60000;

  explicit Tc002Update(Tc002UpdateOptions options);
  ~Tc002Update();
  Tc002Update(const Tc002Update&) = delete;
  Tc002Update& operator=(const Tc002Update&) = delete;

  const std::string& packagePath() const { return packagePath_; }

  void upload(const httplib::Request& request, httplib::Response& response, const httplib::ContentReader& content);

  bool ownsPanel() const;
  void draw(Canvas& canvas, const GfxFont& font) const;
  // Returns the UpdateReady datagram once the reply went out and the panel showed the frame.
  std::string poll(int64_t nowMs, bool frameShown);
  void applyHello(const tc002::SupervisorMessage& hello);
  void addFacts(DeviceFacts& facts) const override;
  void writeMembers(api::JsonWriter& json) const override;

 private:
  struct Refusal {
    int status = 0;
    const char* code = "";
    std::string message;
  };
  bool receive(const httplib::Request& request, const httplib::ContentReader& content, Refusal& refusal,
               bool& bodyRead);
  bool verifyPackage(Refusal& refusal, tc002::UpdateReady& ready);
  void note(std::string line);
  void releaseLock(int& lock);

  Tc002UpdateOptions options_;
  std::string packagePath_;
  mutable std::mutex mutex_;
  bool staged_ = false;
  bool answered_ = false;
  bool sent_ = false;
  int64_t sentAtMs_ = 0;
  int lock_ = -1;
  tc002::UpdateReady ready_;
  std::string error_;
  bool statusKnown_ = false;
  tc002::UpdateStatus status_;
  std::vector<std::string> notes_;
};

}
