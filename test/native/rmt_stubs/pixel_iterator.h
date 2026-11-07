#pragma once
#include <cstdint>

namespace fl {
class TestRgbw {
 public:
  bool active() const { return false; }
};

// Only supplies input to the REAL FastLED RMT5 implementation. Buffer
// ownership and transfer order are not reimplemented in this iterator.
class PixelIterator {
 public:
  PixelIterator(uint16_t count, uint8_t value)
      : count_(count), remaining_(count), value_(value) {}
  TestRgbw get_rgbw() const { return {}; }
  uint16_t size() const { return count_; }
  bool has(int count) const { return remaining_ >= count; }
  void loadAndScaleRGB(uint8_t* red, uint8_t* green, uint8_t* blue) {
    *red = value_;
    *green = value_;
    *blue = value_;
  }
  void loadAndScaleRGBW(uint8_t* red, uint8_t* green, uint8_t* blue, uint8_t* white) {
    loadAndScaleRGB(red, green, blue);
    *white = 0;
  }
  void advanceData() { --remaining_; }
  void stepDithering() {}

 private:
  const uint16_t count_;
  uint16_t remaining_;
  const uint8_t value_;
};
}  // namespace fl
