#include <cassert>
#include <cmath>
#include <cstdio>

#include "FastLED.h"

int main() {
  for (const uint8_t brightness : {20, 80, 255}) {
    for (const float gamma : {0.1F, 1.0F, 2.2F, 5.0F}) {
      uint8_t previous = 255;
      for (int linear = 255; linear >= 0; --linear) {
        const auto corrected = static_cast<uint8_t>(
            std::pow(linear / 255.0F, gamma) * 255.0F + 0.5F);
        const CRGB pixel(corrected, corrected, corrected);
        ColorAdjustment adjustment{};
        adjustment.premixed = CRGB(brightness, brightness, brightness);
        for (int repeat = 0; repeat < 8; ++repeat) {
          PixelController<GRB> output(&pixel, 1, adjustment, DISABLE_DITHER);
          const auto current = output.loadAndScale0();
          assert(current <= previous);
          previous = current;
        }
      }
      assert(previous == 0);
    }
  }
  // This is the upstream algorithm, not the project's FastLED stub. A constant
  // low input must NOT increase between frames, yet binary dithering does.
  const CRGB pixel(2, 2, 2);
  ColorAdjustment adjustment{};
  adjustment.premixed = CRGB(80, 80, 80);
  uint8_t previous = 255;
  size_t increases = 0;
  for (int frame = 0; frame < 16; ++frame) {
    PixelController<GRB> output(&pixel, 1, adjustment, BINARY_DITHER);
    const auto current = output.loadAndScale0();
    if (current > previous) ++increases;
    previous = current;
  }
  assert(increases > 0);
  std::printf("Real PixelController: dithering reproduces %zu upward steps; disabled output is monotonic\n", increases);
}
