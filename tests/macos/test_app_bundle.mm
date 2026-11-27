#include "../support.h"

#import <Foundation/Foundation.h>

#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <mach/machine.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "platform/host/Process.h"

using awtrix::test::check;
using awtrix::test::require;

namespace {

uint32_t big(uint32_t value) { return OSSwapBigToHostInt32(value); }

std::set<cpu_type_t> architectures(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  uint32_t magic = 0;
  if (!in.read(reinterpret_cast<char*>(&magic), sizeof magic)) return {};
  if (magic == MH_MAGIC_64) {
    mach_header_64 header{};
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(&header), sizeof header)) return {};
    return {header.cputype};
  }
  if (magic != FAT_CIGAM) return {};
  fat_header header{};
  in.seekg(0);
  if (!in.read(reinterpret_cast<char*>(&header), sizeof header)) return {};
  std::set<cpu_type_t> types;
  for (uint32_t i = 0; i < big(header.nfat_arch) && i < 8; ++i) {
    fat_arch arch{};
    if (!in.read(reinterpret_cast<char*>(&arch), sizeof arch)) return {};
    types.insert(static_cast<cpu_type_t>(big(static_cast<uint32_t>(arch.cputype))));
  }
  return types;
}

std::set<cpu_type_t> expected(const std::string& list) {
  std::set<cpu_type_t> types;
  std::stringstream names(list);
  for (std::string name; std::getline(names, name, ',');) {
    if (name == "arm64") types.insert(CPU_TYPE_ARM64);
    else if (name == "x86_64") types.insert(CPU_TYPE_X86_64);
    else if (!name.empty()) types.insert(-1);
  }
  if (types.empty()) types.insert(TARGET_CPU_ARM64 ? CPU_TYPE_ARM64 : CPU_TYPE_X86_64);
  return types;
}

bool executable(NSString* path) {
  struct stat st {};
  return ::stat(path.fileSystemRepresentation, &st) == 0 && S_ISREG(st.st_mode) && ::access(path.fileSystemRepresentation, X_OK) == 0;
}

}

int main(int argc, char** argv) {
  require(argc == 3 || argc == 4, "usage: macos-app-bundle-test BUNDLE VERSION [ARCH,ARCH]");
  @autoreleasepool {
    NSBundle* bundle = [NSBundle bundleWithPath:@(argv[1])];
    require(bundle != nil, "the app bundle opens");
    NSDictionary* info = bundle.infoDictionary;
    check([info[@"CFBundleIdentifier"] isEqualToString:@"de.blueforcer.awtrix.script-host"], "its identifier");
    check([info[@"CFBundleName"] isEqualToString:@"AWTRIX Script Host"] &&
              [info[@"CFBundleExecutable"] isEqualToString:@"AWTRIX Script Host"],
          "its name and program");
    check([info[@"CFBundleShortVersionString"] isEqualToString:@(argv[2])] &&
              [info[@"CFBundleVersion"] isEqualToString:@(argv[2])],
          "the version of the repository");
    check([info[@"LSUIElement"] boolValue], "it lives in the menu bar, without a Dock icon");
    check([info[@"LSMultipleInstancesProhibited"] boolValue], "one instance at a time");
    check([info[@"LSMinimumSystemVersion"] isEqualToString:@"11.0"], "macOS 11 or later");
    NSString* network = info[@"NSLocalNetworkUsageDescription"];
    check(network.length > 10 && network.length < 120, "a short reason for the local network prompt");
    NSString* icon = [bundle pathForResource:info[@"CFBundleIconFile"] ofType:@"icns"];
    check(icon && [NSData dataWithContentsOfFile:icon].length > 1000, "the AWTRIX icon");
    NSString* page = [bundle pathForResource:@"index" ofType:@"html"];
    check(page && [NSData dataWithContentsOfFile:page].length > 10000, "the web UI for the runners");

    const std::set<cpu_type_t> wanted = expected(argc == 4 ? argv[3] : "");
    NSString* programs = bundle.executablePath.stringByDeletingLastPathComponent;
    for (NSString* name in @[ @"AWTRIX Script Host", @"awtrix-host", @"awtrix-script-runner" ]) {
      NSString* path = [programs stringByAppendingPathComponent:name];
      const std::string label = std::string(name.UTF8String);
      check(executable(path), (label + " is an executable in Contents/MacOS").c_str());
      check(architectures(path.fileSystemRepresentation) == wanted, (label + " has the configured architectures").c_str());
    }

    awtrix::os::TiedProcess host;
    int status = -1;
    check(host.start({[programs stringByAppendingPathComponent:@"awtrix-host"].fileSystemRepresentation, "--help"}) &&
              host.wait(15000, status) && status == 0,
          "the awtrix-host in the bundle runs");
  }
  return awtrix::test::finish("macOS app bundle");
}
