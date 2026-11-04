#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

// In-memory replacement for the external NVS API.
class Preferences {
 public:
  bool begin(const char*, bool readOnly) {
    readOnly_ = readOnly;
    return true;
  }
  void end() {}
  size_t getBytesLength(const char*) const { return bytes_.size(); }
  size_t getBytes(const char*, void* destination, size_t length) const {
    const size_t count = std::min(length, bytes_.size());
    std::memcpy(destination, bytes_.data(), count);
    return count;
  }
  size_t putBytes(const char*, const void* source, size_t length) {
    assert(!readOnly_);
    const auto* first = static_cast<const uint8_t*>(source);
    bytes_.assign(first, first + length);
    return length;
  }
  bool isKey(const char*) const { return !bytes_.empty(); }
  bool remove(const char*) {
    assert(!readOnly_);
    bytes_.clear();
    return true;
  }

 private:
  bool readOnly_ = false;
  inline static std::vector<uint8_t> bytes_;
};
