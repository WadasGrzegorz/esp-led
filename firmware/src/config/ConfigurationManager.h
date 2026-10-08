#pragma once

#include <Arduino.h>

#include "LightingConfig.h"

class ConfigurationManager {
 public:
  ConfigurationManager(const LightingConfig& factoryConfig,
                       uint16_t logicalPixelLimit);

  void begin();
  const LightingConfig& get() const;
  ConfigValidationResult validate(const LightingConfig& candidate) const;
  ConfigValidationResult update(const LightingConfig& candidate);
  ConfigValidationResult restoreFactoryDefaults();
  bool save();
  bool reset();

 private:
  const LightingConfig& factoryConfig_;
  const uint16_t logicalPixelLimit_;
  LightingConfig config_ = {};
};
