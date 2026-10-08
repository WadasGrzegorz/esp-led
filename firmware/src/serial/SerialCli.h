#pragma once

#include <Arduino.h>

#include "../config/ConfigurationManager.h"
#include "../mode/OperatingModeController.h"
#include "../setup/SetupController.h"

class SerialCli {
 public:
  SerialCli(ConfigurationManager& configuration, SetupController& setup,
            OperatingModeController& operatingMode);

  void update();
  void printHelp() const;
  bool consumeConfigurationChanged();

 private:
  void execute(const char* command);
  void printConfig() const;
  void printCalibrationState() const;
  void printSetupError(SetupResult result) const;
  void handleCalibrationCommand(const char* command);
  void handleStripCommand(const char* command);
  bool requireConfigMode() const;

  ConfigurationManager& configuration_;
  SetupController& setup_;
  OperatingModeController& operatingMode_;
  char input_[96] = {};
  uint8_t inputLength_ = 0;
  bool discardingInput_ = false;
  bool configurationChanged_ = false;
};
