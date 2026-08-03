#pragma once

#include <cstdint>
#include <string>

namespace awtrix {

void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

void logdbg(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

namespace logbuf {

std::string jsonAfter(uint32_t after);

void setVerbose(bool on);
bool verbose();

}
}
