#pragma once

#include <cstdint>

#include <atomic>
#include <cctype>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <filesystem>

#include "core/script/HttpBodyFilter.h"
#include "core/script/ScriptServices.h"
#include "sim/SimStore.h"
#include "system/Log.h"
#include "vendor/httplib.h"

namespace awtrix {
namespace sim {


// Stands in for the device's ScriptHttpWorker task: every script request gets its own detached
// thread, at most 8 in flight, and the result callback fires from that thread rather than the loop.
class SimScriptHttp : public script::IScriptHttp {
 public:
  using ResultFn = std::function<void(script::HttpResult)>;

  void begin(ResultFn onResult) { onResult_ = std::move(onResult); }

  bool request(const script::HttpRequest& req) override {
    if (!onResult_) return false;
    if (pending_.load() >= kMaxPending) return false;

    std::string origin, target;
    if (!split(req.url, origin, target)) {
      logf("sim script http: unsupported url %s", req.url.c_str());
      return false;
    }
    const std::size_t maxBytes = req.maxBytes ? req.maxBytes : script::kMaxHttpBody;
    pending_.fetch_add(1);
    std::thread([this, req, origin, target, maxBytes] {
      script::HttpResult r;
      r.id = req.id;
      httplib::Client cli(origin);
      cli.set_connection_timeout(5, 0);
      cli.set_read_timeout(10, 0);
      cli.set_follow_location(true);

      httplib::Headers headers;
      for (const auto& h : req.headers) headers.emplace(h.first, h.second);
      const std::string type = contentType(req.headers);

      httplib::Result res = send(cli, req.method, target, headers, req.body, type);
      if (res) {
        r.status = res->status;
        script::HttpBodyFilter filter;
        filter.begin(req.find, req.keep, maxBytes);
        filter.feed(res->body.data(), res->body.size());
        r.ok = filter.matched();
        if (r.ok) r.body = std::move(filter.body());
      }
      pending_.fetch_sub(1);
      onResult_(std::move(r));
    }).detach();
    return true;
  }

 private:
  static constexpr unsigned kMaxPending = 8;

  static std::string contentType(const script::HttpHeaders& headers) {
    for (const auto& h : headers) {
      if (h.first.size() != 12) continue;
      std::string lower;
      for (const char c : h.first) lower.push_back(static_cast<char>(std::tolower(c)));
      if (lower == "content-type") return h.second;
    }
    return "application/octet-stream";
  }

  static httplib::Result send(httplib::Client& cli, const std::string& method,
                              const std::string& target, const httplib::Headers& headers,
                              const std::string& body, const std::string& type) {
    if (method == "POST") return cli.Post(target, headers, body, type);
    if (method == "PUT") return cli.Put(target, headers, body, type);
    if (method == "PATCH") return cli.Patch(target, headers, body, type);
    if (method == "DELETE") return cli.Delete(target, headers, body, type);
    return cli.Get(target, headers);
  }

  // Plain http:// only, split into an origin httplib::Client can take and the request target;
  // there is no TLS client on the host, so https URLs are rejected outright.
  static bool split(const std::string& url, std::string& origin, std::string& target) {
    const std::string scheme = "http://";
    if (url.compare(0, scheme.size(), scheme) != 0) return false;
    const std::size_t slash = url.find('/', scheme.size());
    if (slash == std::string::npos) {
      origin = url;
      target = "/";
    } else {
      origin = url.substr(0, slash);
      target = url.substr(slash);
    }
    return origin.size() > scheme.size();
  }

  ResultFn onResult_;
  std::atomic<unsigned> pending_{0};
};


// Script sources live in <data>/SCRIPTS/<name>.ax, their persisted state next to them as
// <name>.store.json. Store writes are buffered and flushed every 5 s, as they are on flash.
class SimScriptStore : public script::IScriptStoreSink {
 public:
  using LoadFn = std::function<void(const std::string& name, const std::string& source,
                                    const std::string& storeJson)>;

  void save(const std::string& name, const std::string& source) {
    if (!nameIsSafe(name)) return;
    if (source.size() > script::kMaxSourceCeilingBytes) return;
    flush();
    writeFile(sourcePath(name), source);
  }

  void remove(const std::string& name) {
    if (!nameIsSafe(name)) return;
    dirty_.erase(name);
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(sourcePath(name)), ec);
    std::filesystem::remove(std::filesystem::u8path(storePath(name)), ec);
    flush();
  }

  bool readSource(const std::string& name, std::string& out) const {
    if (!nameIsSafe(name)) return false;
    return sim::readFile(sourcePath(name), out) && !out.empty();
  }

  // Buffered state wins over the file, otherwise a script that just wrote its store would read a
  // stale copy back until the next flush.
  bool readStore(const std::string& name, std::string& out) const {
    if (!nameIsSafe(name)) return false;
    auto it = dirty_.find(name);
    if (it != dirty_.end()) {
      out = it->second;
      return !out.empty();
    }
    return sim::readFile(storePath(name), out) && !out.empty();
  }

  std::vector<std::string> names() const {
    namespace stdfs = std::filesystem;
    std::error_code ec;
    std::vector<std::string> out;
    for (const auto& e : stdfs::directory_iterator(stdfs::u8path(dir()), ec)) {
      const std::string fn = e.path().filename().u8string();
      if (fn.size() <= 3 || fn.compare(fn.size() - 3, 3, ".ax") != 0) continue;
      const std::string stem = fn.substr(0, fn.size() - 3);
      if (!nameIsSafe(stem)) continue;
      out.push_back(stem);
    }
    return out;
  }

  void loadAll(const LoadFn& cb) {
    if (!cb) return;
    for (const auto& name : names()) {
      std::string source;
      if (!sim::readFile(sourcePath(name), source) || source.empty()) {
        logf("scripts: skipped empty %s.ax", name.c_str());
        continue;
      }
      std::string store;
      sim::readFile(storePath(name), store);
      cb(name, source, store);
    }
  }

  void storeChanged(const std::string& script, const std::string& json) override {
    if (!nameIsSafe(script)) return;
    if (json.size() > script::kMaxStoreBytes) return;
    dirty_[script] = json;
  }

  void tick(int64_t nowMs) {
    if (dirty_.empty()) {
      lastFlushMs_ = nowMs;
      return;
    }
    if (nowMs - lastFlushMs_ < kFlushIntervalMs) return;
    flush();
    lastFlushMs_ = nowMs;
  }

  void flush() {
    if (dirty_.empty()) return;
    const std::map<std::string, std::string> pending = std::move(dirty_);
    dirty_.clear();
    for (const auto& kv : pending) writeFile(storePath(kv.first), kv.second);
  }

 private:
  static constexpr long kFlushIntervalMs = 5000;

  static bool nameIsSafe(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    if (name.find('/') != std::string::npos) return false;
    if (name.find('\\') != std::string::npos) return false;
    return name.find("..") == std::string::npos;
  }

  static std::string dir() { return sim::hostPath("/SCRIPTS"); }
  static std::string sourcePath(const std::string& n) { return dir() + "/" + n + ".ax"; }
  static std::string storePath(const std::string& n) { return dir() + "/" + n + ".store.json"; }

  static void writeFile(const std::string& path, const std::string& body) {
    if (!sim::writeFile(path, body)) logf("scripts: cannot write %s", path.c_str());
  }

  std::map<std::string, std::string> dirty_;
  int64_t lastFlushMs_ = 0;
};

}
}
