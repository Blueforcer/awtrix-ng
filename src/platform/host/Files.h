#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace awtrix::os {

bool readFile(const std::string& path, std::string& out, std::size_t maximum = 4096);
bool replaceFile(const std::string& path, std::string_view text);
bool syncDirectory(const std::string& path);
bool ensurePrivateDirectory(const std::string& path);
std::size_t restoreFolderAccess(const std::string& directory);
bool removeFile(const std::string& path);
void keepNewFilesPrivate();
int openForReading(const std::string& path);

class OutFile {
 public:
  OutFile() = default;
  ~OutFile();
  OutFile(OutFile&& other) noexcept;
  OutFile& operator=(OutFile&& other) noexcept;
  OutFile(const OutFile&) = delete;
  OutFile& operator=(const OutFile&) = delete;

  static OutFile create(const std::string& path, bool exclusive);
  bool valid() const { return handle_ >= 0; }
  bool exists() const { return exists_; }
  bool full() const { return full_; }
  bool write(const void* data, std::size_t size);
  bool sync();
  bool close();

 private:
  std::intptr_t handle_ = -1;
  bool exists_ = false;
  bool full_ = false;
};

class FileLock {
 public:
  FileLock() = default;
  ~FileLock();
  FileLock(const FileLock&) = delete;
  FileLock& operator=(const FileLock&) = delete;

  bool acquire(const std::string& path);

 private:
  std::intptr_t handle_ = -1;
};

}
