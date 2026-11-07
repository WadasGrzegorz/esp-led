#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <array>
#include <iomanip>
#include <sstream>
#include <string>

constexpr int INPUT = 0;
constexpr int LOW = 0;
constexpr int HIGH = 1;
inline std::array<int, 64> testPinLevels{};
inline uint32_t testClockMs = 0;
inline uint32_t testClockUs = 0;
inline uint32_t millis() { return testClockMs; }
inline uint32_t micros() { return testClockUs; }
inline void pinMode(uint8_t, int) {}
inline int digitalRead(uint8_t pin) { return testPinLevels.at(pin); }

template <typename A, typename B>
constexpr std::common_type_t<A, B> min(A a, B b) {
  return a < b ? a : b;
}

template <typename A, typename B>
constexpr std::common_type_t<A, B> max(A a, B b) {
  return a > b ? a : b;
}

template <typename T>
constexpr T constrain(T value, T low, T high) {
  return min(max(value, low), high);
}

class TestSerial {
 public:
  template <typename T>
  void print(const T& value) {
    std::ostringstream text;
    if constexpr (std::is_integral_v<T>) text << +value;
    else text << value;
    output_ += text.str();
  }
  template <typename T>
  void print(T value, int precision) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(precision) << value;
    output_ += text.str();
  }
  template <typename T>
  void println(const T& value) { print(value); println(); }
  template <typename T>
  void println(T value, int precision) { print(value, precision); println(); }
  void println() { output_ += '\n'; }
  const std::string& output() const { return output_; }
  void clear() { output_.clear(); }

 private:
  std::string output_;
};

inline TestSerial Serial;
