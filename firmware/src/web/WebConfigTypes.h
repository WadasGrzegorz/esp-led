#pragma once

#include <Arduino.h>

#include "../config/ConfigurationManager.h"
#include "../led/AnimationEngine.h"
#include "../led/LedController.h"
#include "../mode/OperatingModeController.h"
#include "../sensors/Bh1750Sensor.h"
#include "../sensors/PirSensor.h"
#include "../setup/SetupController.h"

struct WebConfigDependencies {
  ConfigurationManager& configuration;
  SetupController& setup;
  OperatingModeController& operatingMode;
  AnimationEngine& animation;
  LedController& leds;
  PirSensor& leftPir;
  PirSensor& rightPir;
  Bh1750Sensor& lightSensor;
  const bool& lightSensorAvailable;
  const bool& hasLuxReading;
  const float& currentLux;
};

struct WebConfigSettings {
  uint16_t logicalPixelLimit;
  uint32_t stripTestTimeoutMs;
  uint32_t restartDelayMs;
};
