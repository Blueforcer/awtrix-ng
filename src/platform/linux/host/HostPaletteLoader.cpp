#include "platform/linux/host/HostPaletteLoader.h"

#include <filesystem>

#include "core/render/PaletteFile.h"
#include "platform/linux/host/HostStore.h"

namespace awtrix::host {

bool loadPalette(const std::string& name, render::Palette& out) {
  return render::loadPaletteFile(name, out, [](const std::string& leaf, std::string& text) {
    return readFile(hostPath("/PALETTES/" + leaf), text);
  }, [](const render::PaletteNameVisitor& visit) {
    namespace stdfs = std::filesystem;
    std::error_code error;
    for (stdfs::directory_iterator it(stdfs::u8path(hostPath("/PALETTES")), error), end;
         !error && it != end; it.increment(error))
      if (visit(it->path().filename().u8string())) break;
  });
}

}
