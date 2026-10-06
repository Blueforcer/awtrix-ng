#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include "platform/linux/host/vendor/httplib.h"

namespace awtrix::host_http {

class ConnectionTrackingServer final : public httplib::Server {
 public:
  void disconnectAll(std::chrono::milliseconds answerGrace) {
    std::unique_lock<std::mutex> lock(connectionsMutex_);
    disconnecting_ = true;
    connectionsEnded_.wait_for(lock, answerGrace, [&] { return connections_.empty(); });
    for (const socket_t connection : connections_) httplib::detail::shutdown_socket(connection);
  }

 private:
  bool process_and_close_socket(socket_t sock) override {
    bool served = false;
    if (track(sock)) {
      std::string remoteAddress, localAddress;
      int remotePort = 0, localPort = 0;
      httplib::detail::get_remote_ip_and_port(sock, remoteAddress, remotePort);
      httplib::detail::get_local_ip_and_port(sock, localAddress, localPort);
      served = httplib::detail::process_server_socket(
          svr_sock_, sock, keep_alive_max_count_, keep_alive_timeout_sec_, read_timeout_sec_,
          read_timeout_usec_, write_timeout_sec_, write_timeout_usec_,
          [&](httplib::Stream& stream, bool closeConnection, bool& connectionClosed) {
            return process_request(stream, remoteAddress, remotePort, localAddress, localPort,
                                   closeConnection, connectionClosed, nullptr);
          });
      untrack(sock);
    }
    httplib::detail::shutdown_socket(sock);
    httplib::detail::close_socket(sock);
    return served;
  }

  bool track(socket_t sock) {
    std::lock_guard<std::mutex> g(connectionsMutex_);
    if (disconnecting_) return false;
    connections_.push_back(sock);
    return true;
  }

  void untrack(socket_t sock) {
    std::lock_guard<std::mutex> g(connectionsMutex_);
    connections_.erase(std::find(connections_.begin(), connections_.end(), sock));
    connectionsEnded_.notify_all();
  }

  std::mutex connectionsMutex_;
  std::condition_variable connectionsEnded_;
  std::vector<socket_t> connections_;
  bool disconnecting_ = false;
};

}
