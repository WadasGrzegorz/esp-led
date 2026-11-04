#pragma once

#include <cstdint>

struct CRGB {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;

  CRGB() = default;
  CRGB(uint8_t red, uint8_t green, uint8_t blue)
      : r(red), g(green), b(blue) {}
  static const CRGB Black;
};

inline const CRGB CRGB::Black{};
constexpr int WS2811 = 0;
constexpr int GRB = 0;

inline void fill_solid(CRGB* pixels, uint16_t count, const CRGB& color) {
  for (uint16_t i = 0; i < count; ++i) {
    pixels[i] = color;
  }
}

class TestFastLED {
 public:
  uint16_t controllerCount = 0;

  template <int, uint8_t, int>
  void addLeds(CRGB*, uint16_t count) { controllerCount = count; }
  void setBrightness(uint8_t) {}
  void show() {}
};

inline TestFastLED FastLED;
