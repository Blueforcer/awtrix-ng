#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "core/mirror/MirrorFilter.h"
#include "core/mirror/MirrorReceiver.h"
#include "core/mirror/MirrorSource.h"
#include "core/mirror/MirrorStatus.h"
#include "core/net/Datagram.h"
#include "core/render/PageInfo.h"

namespace awtrix {
namespace mirror {

struct Config {
  bool share = false;
  std::string shareApps = "*";
  bool shareNotifications = true;
  // Host name or address of the clock to show; empty follows none.
  std::string follow;
  std::string followApps = "*";
  bool followNotifications = true;
};

// Display mirroring between clocks of the same panel size. A clock can share its display, follow
// another one, or both. The followed display takes the place of the app rotation only while it
// shows something the filters admit; notifications of this clock still interrupt it. Everything
// arrives and leaves as datagrams through the platform, which also looks the followed host up.
class Mirror final : public IExternalPage, public IContentSink {
 public:
  explicit Mirror(net::IDatagramSink& sink) : sink_(sink) {}

  // The panel size, and where the status is kept for the API. Nothing is followed before this.
  void begin(int width, int height, Status& status);
  void configure(const Config& config);
  // Whether the platform should keep the mirroring port open.
  bool listening() const { return config_.share || !config_.follow.empty(); }

  void setOnline(bool online);
  // Where the followed clock was found, or why it is not known: Resolving or NotFound.
  void setSource(const net::Endpoint& source);
  void setLookup(FollowState state);

  void receive(const net::Endpoint& from, const uint8_t* data, std::size_t length, int64_t nowMs);
  void tick(int64_t nowMs);

  bool active() const override { return receiver_ && receiver_->showing(); }
  void draw(Canvas& canvas) const override;
  void content(const Canvas& frame, const PageInfo* page, int64_t nowMs) override;

  const Status& status() const { return *status_; }

 private:
  void applyShare();
  void dropReceiver(bool tellSource);
  void updateStatus();

  net::IDatagramSink& sink_;
  Status ownStatus_;
  Status* status_ = &ownStatus_;
  int width_ = 0;
  int height_ = 0;
  Config config_;
  Filter followFilter_;
  bool online_ = false;
  FollowState lookup_ = FollowState::Resolving;
  std::unique_ptr<Source> source_;
  std::unique_ptr<Receiver> receiver_;
};

}
}
