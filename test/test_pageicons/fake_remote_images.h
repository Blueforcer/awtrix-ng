#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "media/RemoteImages.h"

// Hands out whatever picture and state a test sets, and records what it was asked.
struct FakeRemoteImages : awtrix::media::IRemoteImages {
  awtrix::media::RemoteState state = awtrix::media::RemoteState::kPending;
  awtrix::media::RemoteImage image;
  uint32_t gen = 7;
  int asks = 0;
  std::string url;
  int width = 0;
  int height = 0;

  uint32_t generation() const override { return gen; }
  awtrix::media::RemoteState get(std::string_view u, int w, int h,
                                 awtrix::media::RemoteImage& out) override {
    ++asks;
    url.assign(u.data(), u.size());
    width = w;
    height = h;
    if (state == awtrix::media::RemoteState::kReady) out.copyFrom(image);
    return state;
  }
  void settle(awtrix::media::RemoteState next) {
    state = next;
    ++gen;
  }
  void still(int w, int h, uint32_t color) {
    image = awtrix::media::RemoteImage{};
    image.pixels.resize(static_cast<std::size_t>(w) * h);
    for (std::size_t i = 0; i < image.pixels.size(); ++i) image.pixels[i] = color;
    image.width = w;
    image.height = h;
  }
  void gif(const std::vector<uint8_t>& bytes, int w, int h) {
    image = awtrix::media::RemoteImage{};
    image.gif.resize(bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) image.gif[i] = bytes[i];
    image.width = w;
    image.height = h;
  }
};
