#pragma once

#include <string_view>

namespace awtrix {

// A directory scan borrows its visitor; returning true stops the scan.
struct FileNameVisitor {
  void* context;
  bool (*visit)(void*, std::string_view);
  bool operator()(std::string_view name) const { return visit(context, name); }
};

}
