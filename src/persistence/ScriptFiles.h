#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include "core/FileNames.h"

namespace awtrix::scriptfiles {

// Stored filenames allow 1..64 bytes, excluding separators, colon, NUL and traversal.
inline bool nameIsSafe(std::string_view name) {
  if (name.empty() || name.size() > 64) return false;
  char previous = 0;
  for (const char c : name) {
    if (!c || c == '/' || c == '\\' || c == ':' || (c == '.' && previous == '.')) return false;
    previous = c;
  }
  return true;
}

inline std::string path(const std::string& name, const char* extension) {
  std::string out = "/SCRIPTS/";
  out += name;
  out += extension;
  return out;
}
inline std::string sourcePath(const std::string& name) { return path(name, ".ax"); }
inline std::string storePath(const std::string& name) { return path(name, ".store.json"); }
using NameVisitor = FileNameVisitor;

// Backends without upload buffering or a system-state reserve carry no extra state.
struct DirectWrites {
  static constexpr bool keepPendingWhenFull = false;
  static std::size_t pendingBytes(const std::string& = {}) { return 0; }
  static bool hasPending() { return false; }
  static bool allowStore(const std::string&, const std::string&, std::size_t) { return true; }
  static bool waitsForRoom(const std::string&, std::size_t, std::size_t) { return false; }
  static void storeWritten(const std::string&, bool, std::size_t, std::size_t) {}
  static void forget(const std::string&) {}
  static void addPendingNames(std::vector<std::string>&) {}
  static void flushSources() {}
};

struct LittleFsFiles : DirectWrites {
  static bool read(const std::string& path, std::string& out);
  static bool readSource(const std::string& name, std::string& out) { return read(sourcePath(name), out); }
  static bool write(const std::string& path, const std::string& body);
  static void remove(const std::string& path);
  static void forEachName(const NameVisitor& visit);
  static std::size_t freeBytes();
  static bool acceptSource(const std::string& name, const std::string& source, std::size_t pending);
  static void saveSource(const std::string& name, const std::string& source);
};

}
