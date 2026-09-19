#pragma once

#include <cstdint>

namespace awtrix {

namespace api {
class JsonStream;
}

void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

void logdbg(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

namespace logbuf {

void streamJsonAfter(uint32_t after, api::JsonStream& out);

void setVerbose(bool on);
bool verbose();

}
}
