#pragma once

#include <atomic>

namespace httplib { struct Request; struct Response; }

namespace awtrix {

class Tc002Provisioning {
 public:
  explicit Tc002Provisioning(unsigned port) : port_(port) {}
  void setActive(bool active) { active_.store(active); }
  bool active() const { return active_.load(); }
  bool admit(const httplib::Request& request, httplib::Response& response) const;

 private:
  std::atomic<bool> active_{true};
  unsigned port_;
};

}
