#pragma once

#include <cstdint>
#include <vector>
#include <Arduino.h>

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
  uint8_t brightness = 255;
  uint8_t ditherMode = 1;
  CRGB* pixels = nullptr;
  bool captureFrames = false;
  size_t showCount = 0;
  std::vector<CRGB> transmitted;
  uint32_t transmitUs = 7000;

  template <int, uint8_t, int>
  void addLeds(CRGB* data, uint16_t count) {
    pixels = data;
    controllerCount = count;
  }
  void setDither(uint8_t mode) { ditherMode = mode; }
  uint8_t getDither() const { return ditherMode; }
  uint8_t getBrightness() const { return brightness; }
  TestFastLED& operator[](int) { return *this; }
  void setBrightness(uint8_t level) { brightness = level; }
  void show() {
    testClockUs += transmitUs;
    if (!captureFrames) return;
    ++showCount;
    transmitted.clear();
    // FastLED scale8 with FASTLED_SCALE8_FIXED=1; actual PixelController is
    // exercised separately by test_fastled_output.py, including dithering.
    const auto scale = [this](uint8_t value) {
      return static_cast<uint8_t>((value * (brightness + 1)) >> 8);
    };
    for (uint16_t index = 0; index < controllerCount; ++index) {
      transmitted.emplace_back(scale(pixels[index].r), scale(pixels[index].g),
                               scale(pixels[index].b));
    }
  }
};

inline TestFastLED FastLED;
