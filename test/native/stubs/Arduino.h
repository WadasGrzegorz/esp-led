#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

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
  template <typename... Args>
  void print(Args...) {}
  template <typename... Args>
  void println(Args...) {}
};

inline TestSerial Serial;
