#pragma once

#include <cstddef>
#include <string>

namespace awtrix::hostd {

inline constexpr char kSupervisorAddress[] = "172.30.32.2";

struct CodeRequest {
  std::string peer;
  std::string host;
  std::size_t hostCount = 0;
  std::string origin;
  std::size_t originCount = 0;
  std::string fetchSite;
  std::size_t fetchSiteCount = 0;
  std::string ingressPath;
  std::size_t ingressPathCount = 0;
};

bool showsCode(const CodeRequest& request, int port, bool haIngress);

}
