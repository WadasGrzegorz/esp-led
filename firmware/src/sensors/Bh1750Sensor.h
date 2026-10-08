#pragma once

#include <Arduino.h>

enum class Bh1750UpdateResult : uint8_t {
  WAITING,
  SAMPLE_READY,
  IO_ERROR,
};

class Bh1750Sensor {
 public:
  Bh1750Sensor(uint8_t lowAddress, uint8_t highAddress,
               uint32_t readIntervalMs);

  bool begin(uint32_t nowMs);
  Bh1750UpdateResult update(uint32_t nowMs, float& lux);
  uint8_t address() const;

 private:
  bool initializeAddress(uint8_t address);
  bool probe(uint8_t address) const;
  bool sendCommand(uint8_t command) const;

  const uint8_t lowAddress_;
  const uint8_t highAddress_;
  const uint32_t readIntervalMs_;
  uint8_t address_ = 0;
  bool available_ = false;
  bool readAttempted_ = false;
  uint32_t measurementStartedAtMs_ = 0;
  uint32_t lastReadAtMs_ = 0;
};
