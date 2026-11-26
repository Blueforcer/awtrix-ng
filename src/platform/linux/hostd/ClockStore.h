#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace awtrix::hostd {

struct ClockRecord {
  std::string uid;
  std::string name;
  // The clock's web address, http://ip[:port]; the clock's process talks to its API there.
  std::string clockUrl;
  int width = 32;
  int height = 8;
  // The TCP and UDP port of the clock's process on this host.
  int port = 0;
  std::string keyHex;
};

// The paired clocks on disk: STATE/clocks/<uid>/ holds clock.json, the key (0600) and the data of
// the clock's process. The private stores of a process live next to its data folder, so every
// clock has its own.
class ClockStore {
 public:
  static constexpr int kFirstPort = 8301;
  static constexpr int kLastPort = 8399;

  explicit ClockStore(std::filesystem::path root) : root_(std::move(root)) {}

  bool load(std::string& error);
  const std::vector<ClockRecord>& clocks() const { return clocks_; }
  const ClockRecord* find(const std::string& uid) const;

  // Stores the clock with a new key; a clock already known keeps its port. portFree is asked for
  // ports no clock here uses yet.
  bool pair(ClockRecord record, const std::string& keyHex, const std::function<bool(int)>& portFree,
            ClockRecord& stored, std::string& error);
  // Forgets the clock; with keepData its folder stays, without its key.
  bool remove(const std::string& uid, bool keepData);
  // Whether an Authorization header carries this clock's key.
  bool keyOk(const std::string& uid, const std::string& authorization) const;

  std::filesystem::path clockDir(const std::string& uid) const { return root_ / "clocks" / uid; }
  std::filesystem::path dataDir(const std::string& uid) const { return clockDir(uid) / "data"; }
  std::filesystem::path keyFile(const std::string& uid) const { return clockDir(uid) / "key"; }

  static bool validUid(const std::string& uid);

 private:
  bool save(const ClockRecord& record, std::string& error) const;

  std::filesystem::path root_;
  std::vector<ClockRecord> clocks_;
};

}
