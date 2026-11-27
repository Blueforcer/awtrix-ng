#include "platform/host/app/macos/LaunchAgentLoginItem.h"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace awtrix::hostapp {
namespace {

std::string escaped(const std::string& text) {
  std::string out;
  for (const char c : text) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&apos;"; break;
      default: out += c;
    }
  }
  return out;
}

}

std::string launchAgentPlist(const std::string& label, const std::string& app) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
         "<plist version=\"1.0\">\n"
         "<dict>\n"
         "  <key>Label</key>\n"
         "  <string>" + escaped(label) + "</string>\n"
         "  <key>ProgramArguments</key>\n"
         "  <array>\n"
         "    <string>/usr/bin/open</string>\n"
         "    <string>" + escaped(app) + "</string>\n"
         "  </array>\n"
         "  <key>RunAtLoad</key>\n"
         "  <true/>\n"
         "  <key>LimitLoadToSessionType</key>\n"
         "  <string>Aqua</string>\n"
         "</dict>\n"
         "</plist>\n";
}

LaunchAgentLoginItem::LaunchAgentLoginItem(std::string folder, std::string label, std::string app)
    : folder_(std::move(folder)), label_(std::move(label)), app_(std::move(app)) {}

std::string LaunchAgentLoginItem::path() const { return folder_ + "/" + label_ + ".plist"; }

bool LaunchAgentLoginItem::enabled() const {
  std::error_code error;
  return fs::is_regular_file(path(), error);
}

bool LaunchAgentLoginItem::enable(bool on) {
  std::error_code error;
  if (!on) {
    fs::remove(path(), error);
    return !error && !enabled();
  }
  fs::create_directories(folder_, error);
  if (error) return false;
  const std::string partial = path() + ".partial";
  {
    std::ofstream out(partial, std::ios::binary | std::ios::trunc);
    out << launchAgentPlist(label_, app_);
    if (!out.flush()) {
      out.close();
      fs::remove(partial, error);
      return false;
    }
  }
  fs::permissions(partial, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read,
                  error);
  if (!error) fs::rename(partial, path(), error);
  if (error) {
    std::error_code ignored;
    fs::remove(partial, ignored);
    return false;
  }
  return true;
}

}
