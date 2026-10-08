#include "Bh1750Sensor.h"

#include <Wire.h>

namespace {
constexpr uint8_t POWER_ON_COMMAND = 0x01;
constexpr uint8_t RESET_COMMAND = 0x07;
constexpr uint8_t CONTINUOUS_HIGH_RESOLUTION_MODE = 0x10;
constexpr uint8_t INITIALIZATION_ATTEMPTS = 2;
constexpr uint32_t INITIALIZATION_RETRY_DELAY_MS = 2;
constexpr uint32_t FIRST_MEASUREMENT_DELAY_MS = 180;
constexpr float RAW_TO_LUX_DIVISOR = 1.2F;
}  // namespace

Bh1750Sensor::Bh1750Sensor(const uint8_t lowAddress,
                           const uint8_t highAddress,
                           const uint32_t readIntervalMs)
    : lowAddress_(lowAddress),
      highAddress_(highAddress),
      readIntervalMs_(readIntervalMs) {}

bool Bh1750Sensor::begin(const uint32_t nowMs) {
  available_ = initializeAddress(lowAddress_) ||
               initializeAddress(highAddress_);
  if (!available_) {
    address_ = 0;
    readAttempted_ = false;
    return false;
  }

  measurementStartedAtMs_ = nowMs;
  readAttempted_ = false;
  return true;
}

bool Bh1750Sensor::initializeAddress(const uint8_t address) {
  for (uint8_t attempt = 0; attempt < INITIALIZATION_ATTEMPTS; ++attempt) {
    if (!probe(address)) {
      delay(INITIALIZATION_RETRY_DELAY_MS);
      continue;
    }

    address_ = address;
    const bool initialized = sendCommand(POWER_ON_COMMAND) &&
                             sendCommand(RESET_COMMAND) &&
                             sendCommand(CONTINUOUS_HIGH_RESOLUTION_MODE);
    if (initialized) {
      return true;
    }

    delay(INITIALIZATION_RETRY_DELAY_MS);
  }

  if (address_ == address) {
    address_ = 0;
  }
  return false;
}

Bh1750UpdateResult Bh1750Sensor::update(const uint32_t nowMs, float& lux) {
  if (!available_ ||
      nowMs - measurementStartedAtMs_ < FIRST_MEASUREMENT_DELAY_MS) {
    return Bh1750UpdateResult::WAITING;
  }

  if (readAttempted_ && nowMs - lastReadAtMs_ < readIntervalMs_) {
    return Bh1750UpdateResult::WAITING;
  }

  readAttempted_ = true;
  lastReadAtMs_ = nowMs;

  if (Wire.requestFrom(address_, static_cast<uint8_t>(2)) != 2) {
    while (Wire.available() > 0) {
      Wire.read();
    }
    available_ = false;
    address_ = 0;
    return Bh1750UpdateResult::IO_ERROR;
  }

  const uint16_t rawValue =
      (static_cast<uint16_t>(Wire.read()) << 8) | Wire.read();
  lux = rawValue / RAW_TO_LUX_DIVISOR;
  return Bh1750UpdateResult::SAMPLE_READY;
}

uint8_t Bh1750Sensor::address() const { return address_; }

bool Bh1750Sensor::probe(const uint8_t address) const {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool Bh1750Sensor::sendCommand(const uint8_t command) const {
  Wire.beginTransmission(address_);
  Wire.write(command);
  return Wire.endTransmission() == 0;
}
