#include "../support.h"
#include "../Sockets.h"

#include <fcntl.h>
#include <io.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

#include "platform/host/Files.h"
#include "platform/host/Log.h"
#include "platform/host/Memory.h"
#include "platform/host/Process.h"
#include "platform/host/System.h"
#include "platform/host/Wake.h"

using awtrix::test::check;
using awtrix::test::require;
namespace fs = std::filesystem;
namespace os = awtrix::os;

namespace {

std::string self() {
  wchar_t path[MAX_PATH] = {};
  ::GetModuleFileNameW(nullptr, path, MAX_PATH);
  return fs::path(path).u8string();
}

std::string slurp(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), {});
}

int child(const std::string& mode, const std::string& marker) {
  if (mode == "exit7") return 7;
  if (mode == "log") {
    std::printf("hello-log\n");
    std::fflush(stdout);
    return 0;
  }
  if (mode == "logger") {
    std::printf("runner-out\n");
    std::fflush(stdout);
    if (!os::logToFile(marker, 64)) return 4;
    std::printf("logger-out\n");
    std::fprintf(stderr, "logger-err\n");
    return 0;
  }
  if (mode == "graceful") {
    std::atomic<bool> stop{false};
    os::stopOnRequest(stop);
    std::ofstream(marker) << "running";
    while (!stop) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::ofstream(marker) << "stopped";
    return 3;
  }
  if (mode == "hang") {
    std::ofstream(marker) << ::GetCurrentProcessId();
    for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  if (mode == "spawner") {
    static os::ChildProcess grandchild;
    if (!grandchild.start({self(), "--child", "hang", marker}, marker + ".log")) return 1;
    for (int i = 0; i < 300 && slurp(marker).empty(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::_Exit(0);
  }
  return 2;
}

DWORD allowedTo(const fs::path& path, WELL_KNOWN_SID_TYPE who, BYTE* flags = nullptr) {
  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (::GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl,
                              nullptr, &descriptor) != ERROR_SUCCESS)
    return 0;
  DWORD mask = 0;
  for (DWORD i = 0; dacl && i < dacl->AceCount; ++i) {
    void* ace = nullptr;
    if (!::GetAce(dacl, i, &ace)) break;
    const auto* allowed = static_cast<const ACCESS_ALLOWED_ACE*>(ace);
    if (allowed->Header.AceType != ACCESS_ALLOWED_ACE_TYPE ||
        !::IsWellKnownSid(const_cast<DWORD*>(&allowed->SidStart), who))
      continue;
    mask |= allowed->Mask;
    if (flags) *flags |= allowed->Header.AceFlags;
  }
  ::LocalFree(descriptor);
  return mask;
}

bool waitFor(const std::function<bool()>& done, int ms) {
  for (int waited = 0; waited < ms; waited += 10) {
    if (done()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return done();
}

bool ownAccountMayUse(const fs::path& path) {
  const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  ::CloseHandle(file);
  return true;
}

bool ownerOnly(const fs::path& path) {
  return ownAccountMayUse(path) && allowedTo(path, WinBuiltinAdministratorsSid) == FILE_ALL_ACCESS &&
         allowedTo(path, WinLocalSystemSid) == FILE_ALL_ACCESS && allowedTo(path, WinBuiltinUsersSid) == 0 &&
         allowedTo(path, WinAuthenticatedUserSid) == 0 && allowedTo(path, WinWorldSid) == 0 &&
         allowedTo(path, WinInteractiveSid) == 0;
}

bool runLogger(const fs::path& log, const fs::path& runtime) {
  os::ChildProcess logger;
  int status = -1;
  return logger.start({self(), "--child", "logger", log.u8string()}, runtime.u8string()) &&
         waitFor([&] { return logger.ended(status); }, 5000) && status == 0;
}

void stateFolder(const fs::path& root) {
  const fs::path state = root / "state";
  require(os::ensurePrivateDirectory(state.u8string()), "a private state folder");
  const fs::path clock = state / "clocks" / "abc";
  fs::create_directories(clock / "data");
  require(os::replaceFile((clock / "clock.json").u8string(), "{}"), "a clock record");
  const fs::path log = state / "awtrix-host.log";
  const fs::path older = state / "awtrix-host.log.1";
  const fs::path runtime = clock / "runtime.log";

  check(runLogger(log, runtime), "a child logs through logToFile, as awtrix-host --log does");
  const std::string first = slurp(log);
  check(first.find("logger-out") != std::string::npos && first.find("logger-err") != std::string::npos,
        "stdout and stderr land in awtrix-host.log");
  check(slurp(runtime).find("runner-out") != std::string::npos, "what the child printed before lands in runtime.log");
  check(ownerOnly(log), "the owner, SYSTEM and the Administrators may use awtrix-host.log, other users not");
  check(ownerOnly(runtime), "the same holds for the runtime.log of a runner");
  check(ownerOnly(clock) && ownerOnly(clock / "data"), "and for the folders of a clock");
  os::OutFile script = os::OutFile::create((clock / "data" / "script.be").u8string(), true);
  check(script.valid() && script.write("x", 1) && script.close(), "a runner writes a script, store or image file");
  {
    os::FileLock lock;
    check(lock.acquire((clock / "data" / ".lock").u8string()), "and takes its lock");
  }
  check(ownerOnly(clock / "data" / "script.be") && ownerOnly(clock / "data" / ".lock") &&
            ownerOnly(clock / "clock.json"),
        "those and the clock record, written like its key, have the same access");

  std::ofstream(log, std::ios::app) << std::string(100, '.') << '\n';
  const std::string full = slurp(log);
  check(runLogger(log, runtime), "the logger starts again");
  check(slurp(older) == full && slurp(log).find("logger-out") != std::string::npos,
        "a log over its limit moves to awtrix-host.log.1, a new one starts");
  check(ownerOnly(older) && ownerOnly(log), "both keep that access");
}

void files(const fs::path& root) {
  const fs::path file = root / "record.json";
  check(os::replaceFile(file.u8string(), "first"), "a file is written beside and moved over");
  check(os::replaceFile(file.u8string(), "second"), "and replaced while it exists");
  std::string text;
  check(os::readFile(file.u8string(), text) && text == "second", "a reader finds the new text");
  check(awtrix::test::privateMode(file, 0600), "only the owner, SYSTEM and the Administrators may open it");
  errno = 0;
  check(!os::readFile(file.u8string(), text, 3) && errno == EFBIG, "a file over the maximum sets EFBIG");
  errno = 0;
  check(!os::readFile((root / "missing").u8string(), text) && errno == ENOENT, "a missing file sets ENOENT");
  std::size_t leftovers = 0;
  for (const auto& entry : fs::directory_iterator(root))
    if (entry.path().filename().u8string().find(".tmp.") != std::string::npos) ++leftovers;
  check(leftovers == 0, "no temporary file stays behind");

  const fs::path made = root / "private";
  check(os::ensurePrivateDirectory(made.u8string()) && awtrix::test::privateMode(made, 0700),
        "a new private directory gets a protected owner-only ACL");
  BYTE inherit = 0;
  check(allowedTo(made, WinBuiltinAdministratorsSid, &inherit) == FILE_ALL_ACCESS &&
            (inherit & (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)) == (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE) &&
            allowedTo(made, WinLocalSystemSid) == FILE_ALL_ACCESS,
        "the Administrators and SYSTEM keep full access to it and to what it will hold");
  const fs::path log = made / "awtrix-host.log";
  FILE* appended = ::_wfopen(log.c_str(), L"a");
  check(appended && std::fputs("line\n", appended) >= 0 && std::fclose(appended) == 0, "a log is appended inside");
  check(allowedTo(log, WinBuiltinAdministratorsSid) == FILE_ALL_ACCESS && allowedTo(log, WinBuiltinUsersSid) == 0 &&
            allowedTo(log, WinAuthenticatedUserSid) == 0 && allowedTo(log, WinWorldSid) == 0 &&
            allowedTo(log, WinInteractiveSid) == 0,
        "an administrator may read and delete that log, other users may not open it");
  check(allowedTo(file, WinBuiltinAdministratorsSid) == FILE_ALL_ACCESS, "the same holds for a replaced file");
  const fs::path open = root / "open";
  fs::create_directories(open);
  check(!awtrix::test::privateMode(open, 0700), "a plain directory inherits more than that");
  check(os::ensurePrivateDirectory(open.u8string()) && awtrix::test::privateMode(open, 0700),
        "an existing directory is tightened");
  check(!os::ensurePrivateDirectory(file.u8string()), "a file is no private directory");
  check(os::syncDirectory(root.u8string()) && !os::syncDirectory(file.u8string()), "only directories sync");

  const fs::path out = root / "out.bin";
  os::OutFile first = os::OutFile::create(out.u8string(), true);
  check(first.valid() && first.write("abc", 3) && first.sync() && first.close(), "an exclusive file is written");
  os::OutFile second = os::OutFile::create(out.u8string(), true);
  check(!second.valid() && second.exists(), "a second exclusive create reports the file exists");
  os::OutFile truncated = os::OutFile::create(out.u8string(), false);
  check(truncated.valid() && truncated.write("z", 1) && truncated.close() && slurp(out) == "z",
        "a plain create truncates");

  const int fd = os::openForReading(out.u8string());
  char byte = 0;
  check(fd >= 0 && ::_read(fd, &byte, 1) == 1 && byte == 'z', "a file opens as a read-only descriptor");
  if (fd >= 0) ::_close(fd);
  errno = 0;
  check(os::openForReading(root.u8string()) < 0, "a directory does not open for reading");

  {
    os::FileLock held;
    check(held.acquire((root / ".lock").u8string()), "the first lock is taken");
    os::FileLock again;
    check(!again.acquire((root / ".lock").u8string()), "a second lock on the same file fails");
  }
  os::FileLock later;
  check(later.acquire((root / ".lock").u8string()), "the lock is free once its holder is gone");
  check(os::removeFile(out.u8string()) && !fs::exists(out), "a file is removed");
}

void sockets() {
  require(os::startSockets(), "Winsock starts");
  const os::Socket listener = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  socklen_t size = sizeof address;
  require(::bind(listener, reinterpret_cast<sockaddr*>(&address), size) == 0 && ::listen(listener, 4) == 0 &&
              ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0,
          "a loopback listener");

  os::UniqueSocket client(os::openStream(AF_INET));
  require(client.valid(), "a stream socket opens");
  const int error = os::connectSocket(client.get(), reinterpret_cast<const sockaddr*>(&address), sizeof address);
  check(error == 0 || os::connecting(error), "a non-blocking connect is pending, WSAEWOULDBLOCK included");
  check(os::waitWritable(client.get(), 2000) && os::pendingError(client.get()) == 0, "and completes");
  os::UniqueSocket peer(::accept(listener, nullptr, nullptr));
  require(peer.valid(), "the listener accepts");
  char buffer[8];
  check(os::receiveBytes(client.get(), buffer, sizeof buffer) < 0 && os::wouldBlock(os::socketError()),
        "reading an empty non-blocking socket would block");
  check(os::sendBytes(peer.get(), "hi", 2) == 2, "the peer sends");
  os::PollEntry ready{client.get(), POLLIN, 0};
  check(os::pollSockets(&ready, 1, 2000) == 1 && (ready.revents & POLLIN), "poll sees the bytes");
  check(os::pendingBytes(client.get()) == 2, "two bytes wait");
  check(os::receiveBytes(client.get(), buffer, 1, true) == 1 && os::receiveBytes(client.get(), buffer, 2) == 2 &&
            std::memcmp(buffer, "hi", 2) == 0,
        "a peek leaves the bytes for the read");

  const os::Socket copy = os::duplicateSocket(peer.get());
  check(copy != os::kNoSocket, "a socket is duplicated");
  os::shutdownSocket(copy);
  os::closeSocket(copy);
  check(os::waitWritable(client.get(), 100), "the original stays usable");
  ready = {client.get(), POLLIN, 0};
  check(os::pollSockets(&ready, 1, 2000) == 1 && os::receiveBytes(client.get(), buffer, sizeof buffer) == 0,
        "shutting the duplicate down ends the connection");
  os::closeSocket(listener);

  const int closed = awtrix::test::freePort(SOCK_STREAM);
  sockaddr_in nowhere = address;
  nowhere.sin_port = htons(static_cast<uint16_t>(closed));
  os::UniqueSocket refused(os::openStream(AF_INET));
  const int pending = os::connectSocket(refused.get(), reinterpret_cast<const sockaddr*>(&nowhere), sizeof nowhere);
  os::PollEntry failed{refused.get(), POLLOUT, 0};
  check(os::connecting(pending) && os::pollSockets(&failed, 1, 3000) == 1, "a refused connect wakes the poll");
  check(os::failureOf(os::pendingError(refused.get())) == os::SocketFailure::Refused, "and reads as refused");

  const int port = awtrix::test::freePort(SOCK_DGRAM);
  check(os::localPortFree(static_cast<uint16_t>(port)), "a free port is free");
  const os::Socket udp = os::openUdp(static_cast<uint16_t>(port), true);
  require(udp != os::kNoSocket, "a UDP port opens");
  check(!os::localPortFree(static_cast<uint16_t>(port)), "and is no longer free");
  check(os::openUdp(static_cast<uint16_t>(port)) == os::kNoSocket, "nobody else binds it");
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  to.sin_port = htons(static_cast<uint16_t>(port));
  const std::string big(300, 'x');
  check(os::sendDatagram(udp, big.data(), big.size(), to) == 300, "a datagram goes out");
  sockaddr_in from{};
  char small[100];
  check(waitFor([&] { return os::pendingBytes(udp) > 0; }, 2000) &&
            os::receiveDatagram(udp, small, sizeof small, from) == static_cast<long>(sizeof small) &&
            from.sin_port == to.sin_port,
        "a datagram larger than the buffer arrives cut, as on Linux");
  to.sin_port = htons(static_cast<uint16_t>(awtrix::test::freePort(SOCK_DGRAM)));
  os::sendDatagram(udp, "x", 1, to);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  check(os::receiveDatagram(udp, small, sizeof small, from) < 0 && os::wouldBlock(os::socketError()),
        "a closed port's ICMP answer does not show up as a reset");
  os::closeSocket(udp);
  unsigned char ip[16];
  check(os::parseIp(AF_INET, "192.168.1.5", ip) && os::parseIp(AF_INET6, "fe80::1", ip) &&
            !os::parseIp(AF_INET, "nas.local", ip),
        "IP literals parse");
}

void wake() {
  os::Wake wake;
  require(wake.valid(), "a wake handle opens");
  os::PollEntry entry{wake.handle(), POLLIN, 0};
  check(os::pollSockets(&entry, 1, 0) == 0, "nothing is pending at first");
  wake.signal();
  wake.signal();
  entry = {wake.handle(), POLLIN, 0};
  check(os::pollSockets(&entry, 1, 1000) == 1 && (entry.revents & POLLIN), "a signal wakes the poll");
  wake.drain();
  entry = {wake.handle(), POLLIN, 0};
  check(os::pollSockets(&entry, 1, 0) == 0, "drain empties it");
  std::thread other([&wake] { wake.signal(); });
  entry = {wake.handle(), POLLIN, 0};
  check(os::pollSockets(&entry, 1, 2000) == 1, "another thread wakes it too");
  other.join();
}

void processes(const fs::path& root) {
  const std::string program = self();
  int status = -1;
  {
    os::ChildProcess process;
    require(process.start({program, "--child", "exit7", ""}, (root / "exit.log").u8string()), "a child starts");
    check(process.running() && process.id() > 0, "it runs with an id");
    check(waitFor([&] { return process.ended(status); }, 5000) && status == 7 && !process.running(),
          "its exit status is reported once it ends");
  }
  {
    os::ChildProcess process;
    require(process.start({program, "--child", "log", ""}, (root / "child log.log").u8string()), "a logging child");
    check(waitFor([&] { return process.ended(status); }, 5000) &&
              slurp(root / "child log.log").find("hello-log") != std::string::npos,
          "stdout lands in the log file, also with a space in its name");
  }
  {
    const fs::path marker = root / "graceful.txt";
    os::ChildProcess process;
    require(process.start({program, "--child", "graceful", marker.u8string()}, (root / "graceful.log").u8string()),
            "a child that waits for its stop");
    check(waitFor([&] { return slurp(marker) == "running"; }, 5000), "it runs");
    const auto start = std::chrono::steady_clock::now();
    process.stop(5000);
    check(slurp(marker) == "stopped" && !process.running(), "stop asks it to end, and it ends on its own");
    check(std::chrono::steady_clock::now() - start < std::chrono::seconds(3), "without waiting out the grace time");
  }
  {
    const fs::path marker = root / "hang.txt";
    os::ChildProcess process;
    require(process.start({program, "--child", "hang", marker.u8string()}, (root / "hang.log").u8string()),
            "a child that ignores its stop");
    check(waitFor([&] { return !slurp(marker).empty(); }, 5000), "it runs");
    const auto start = std::chrono::steady_clock::now();
    process.stop(300);
    check(!process.running() && std::chrono::steady_clock::now() - start < std::chrono::seconds(3),
          "it is killed once the grace time is up");
  }
  {
    const fs::path marker = root / "orphan.txt";
    os::ChildProcess spawner;
    require(spawner.start({program, "--child", "spawner", marker.u8string()}, (root / "spawner.log").u8string()),
            "a child that starts its own child and crashes");
    check(waitFor([&] { return spawner.ended(status); }, 10000), "the spawner ends");
    const DWORD pid = static_cast<DWORD>(std::strtoul(slurp(marker).c_str(), nullptr, 10));
    const HANDLE orphan = pid ? ::OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
    check(pid != 0 && (!orphan || ::WaitForSingleObject(orphan, 5000) == WAIT_OBJECT_0),
          "its child dies with it through the job object");
    if (orphan) ::CloseHandle(orphan);
  }
  os::ChildProcess none;
  check(!none.start({}, (root / "none.log").u8string()) && !none.start({(root / "missing.exe").u8string()}, ""),
        "nothing or a missing program does not start");
  check(!none.running(), "and nothing runs");
}

void facts() {
  std::uint64_t bytes = 0;
  check(os::availableMemory(bytes) && bytes > 0, "available memory is reported");
  check(!os::hostName().empty(), "the computer has a name");
  check(os::machineName() == "x86_64" || os::machineName() == "aarch64", "the architecture is named like uname -m");
  check(os::secondsSinceBoot() > 0, "uptime counts from the boot");
  check(!os::firstIpv4Address().empty(), "an IPv4 address is reported");
}

}

int main(int argc, char** argv) {
  if (argc == 4 && std::string(argv[1]) == "--child") return child(argv[2], argv[3]);
  const fs::path root = fs::temp_directory_path() / ("awtrix-win32-" + std::to_string(::GetCurrentProcessId()));
  fs::remove_all(root);
  fs::create_directories(root);
  files(root);
  stateFolder(root);
  sockets();
  wake();
  processes(root);
  facts();
  std::error_code ignored;
  fs::remove_all(root, ignored);
  return awtrix::test::finish("win32 backends");
}
