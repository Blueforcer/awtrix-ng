#include "platform/host/app/macos/StatusIcon.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "platform/host/app/TrayIcon.h"

namespace awtrix::hostapp {

NSImage* statusIcon(int points) {
  NSImage* image = [[NSImage alloc] initWithSize:NSMakeSize(points, points)];
  for (const int scale : {1, 2}) {
    const int pixels = points * scale;
    const std::vector<uint8_t> alpha = iconAlpha(pixels);
    if (alpha.empty()) return nil;
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                                    pixelsWide:pixels
                                                                    pixelsHigh:pixels
                                                                 bitsPerSample:8
                                                               samplesPerPixel:4
                                                                      hasAlpha:YES
                                                                      isPlanar:NO
                                                                colorSpaceName:NSDeviceRGBColorSpace
                                                                   bytesPerRow:pixels * 4
                                                                  bitsPerPixel:32];
    if (!rep) return nil;
    unsigned char* data = rep.bitmapData;
    for (std::size_t i = 0; i < alpha.size(); ++i) {
      data[i * 4] = data[i * 4 + 1] = data[i * 4 + 2] = 0;
      data[i * 4 + 3] = alpha[i];
    }
    rep.size = NSMakeSize(points, points);
    [image addRepresentation:rep];
  }
  [image setTemplate:YES];
  image.accessibilityDescription = @"AWTRIX Script Host";
  return image;
}

}
