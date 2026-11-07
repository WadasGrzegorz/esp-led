#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <vector>

#include "idf5_rmt.h"
#include "strip_rmt.h"

namespace {
size_t writesWhileTransmitting = 0;
bool pending = false;
std::vector<uint8_t> lastTransmitted;

class TestStrip : public fl::IRmtStrip {
 public:
  explicit TestStrip(uint32_t count) : count_(count), buffer_(count * 3) {}
  void setPixel(uint32_t index, uint8_t red, uint8_t green, uint8_t blue) override {
    assert(index < count_);
    if (busy_) ++writesWhileTransmitting;
    buffer_[index * 3] = red;
    buffer_[index * 3 + 1] = green;
    buffer_[index * 3 + 2] = blue;
  }
  void setPixelRGBW(uint32_t index, uint8_t red, uint8_t green, uint8_t blue, uint8_t) override {
    setPixel(index, red, green, blue);
  }
  void drawAsync() override {
    // Same late synchronization as the pinned RmtStrip::drawAsync().
    if (busy_) waitDone();
    busy_ = true;
    pending = true;
  }
  void waitDone() override {
    if (busy_) lastTransmitted = buffer_;
    busy_ = false;
    pending = false;
  }
  bool isDrawing() override { return busy_; }
  void fill(uint8_t, uint8_t, uint8_t) override {}
  void fillRGBW(uint8_t, uint8_t, uint8_t, uint8_t) override {}
  fl::u32 numPixels() override { return count_; }

 private:
  const uint32_t count_;
  bool busy_ = false;
  std::vector<uint8_t> buffer_;
};
}  // namespace

fl::IRmtStrip* fl::IRmtStrip::Create(
    int, uint32_t count, bool, uint32_t, uint32_t, uint32_t, uint32_t,
    uint32_t, DmaMode, uint8_t, uint8_t*) {
  return new TestStrip(count);
}

int main() {
  fl::RmtController5 driver(4, 320, 320, 640, fl::RmtController5::DMA_AUTO);
  for (const uint8_t value : {255, 200, 100, 1, 0}) {
    fl::PixelIterator pixels(240, value);
    driver.loadPixelData(pixels);
    driver.showPixels();
  }
  if (writesWhileTransmitting != 0) {
    std::fprintf(stderr, "RMT buffer overwritten during transmission: %zu writes\n", writesWhileTransmitting);
    return 1;
  }
  if (pending) {
    std::fputs("Final black frame still pending after showPixels() returned\n", stderr);
    return 2;
  }
  assert(lastTransmitted.size() == 720);
  for (const auto value : lastTransmitted) assert(value == 0);
  std::puts("Real RMT5 driver: buffer ownership safe; final all-zero frame completed");
}
