#include "SerialCli.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

SerialCli::SerialCli(ConfigurationManager& configuration,
                     SetupController& setup,
                     OperatingModeController& operatingMode)
    : configuration_(configuration),
      setup_(setup),
      operatingMode_(operatingMode) {}

void SerialCli::update() {
  while (Serial.available() > 0) {
    const char next = static_cast<char>(Serial.read());
    if (next == '\r') {
      continue;
    }
    if (next == '\n') {
      if (discardingInput_) {
        discardingInput_ = false;
        inputLength_ = 0;
        continue;
      }
      input_[inputLength_] = '\0';
      if (inputLength_ > 0) {
        execute(input_);
      }
      inputLength_ = 0;
      continue;
    }
    if (discardingInput_) {
      continue;
    }
    if (inputLength_ >= sizeof(input_) - 1) {
      inputLength_ = 0;
      discardingInput_ = true;
      Serial.println("Command too long; input discarded");
      continue;
    }
    input_[inputLength_] = next;
    ++inputLength_;
  }
}

void SerialCli::printHelp() const {
  Serial.println("Commands:");
  Serial.println("  config show");
  if (!operatingMode_.isConfigMode()) {
    Serial.println(
        "  Hold physical BOOT until CONFIG MODE is reported to unlock setup");
    return;
  }

  Serial.println("  config save | reset | exit");
  Serial.println("  calibration start | next | prev | + [n] | - [n]");
  Serial.println("  calibration goto <pixel> | mark | undo | finish | cancel");
  Serial.println("  strip test <n> | strip test off");
  Serial.println("  help");
}

bool SerialCli::consumeConfigurationChanged() {
  const bool changed = configurationChanged_;
  configurationChanged_ = false;
  return changed;
}

void SerialCli::execute(const char* command) {
  if (strcmp(command, "help") == 0) {
    printHelp();
    return;
  }
  if (strcmp(command, "config show") == 0) {
    printConfig();
    return;
  }
  if (strcmp(command, "config save") == 0) {
    if (!requireConfigMode()) {
      return;
    }
    if (setup_.isCalibrationActive()) {
      Serial.println("Finish or cancel calibration before saving");
      return;
    }
    if (!configuration_.save()) {
      Serial.println("Config save failed; CONFIG MODE remains active");
      return;
    }

    Serial.println("Config saved to NVS -> restarting NORMAL");
    operatingMode_.exitConfigMode(ConfigModeExitReason::SAVED);
    return;
  }
  if (strcmp(command, "config reset") == 0) {
    if (!requireConfigMode()) {
      return;
    }
    if (setup_.isCalibrationActive()) {
      setup_.cancelCalibration();
    }
    if (setup_.isServiceModeActive()) {
      setup_.stopStripTest();
    }
    const bool nvsReset = configuration_.reset();
    configurationChanged_ = true;
    Serial.println(nvsReset
                       ? "Config reset to factory defaults; saved NVS config removed"
                       : "Config reset in RAM, but removing saved NVS config failed");
    printConfig();
    return;
  }
  if (strcmp(command, "config exit") == 0) {
    if (!requireConfigMode()) {
      return;
    }
    Serial.println("Leaving CONFIG MODE without saving -> restarting NORMAL");
    operatingMode_.exitConfigMode(ConfigModeExitReason::USER_REQUESTED);
    return;
  }
  if (strncmp(command, "calibration ", 12) == 0) {
    if (!requireConfigMode()) {
      return;
    }
    handleCalibrationCommand(command + 12);
    return;
  }
  if (strncmp(command, "strip test ", 11) == 0) {
    if (!requireConfigMode()) {
      return;
    }
    handleStripCommand(command + 11);
    return;
  }

  Serial.println("Unknown command. Type 'help'.");
}

bool SerialCli::requireConfigMode() const {
  if (operatingMode_.isConfigMode()) {
    return true;
  }

  Serial.println(
      "Command locked: hold physical BOOT until CONFIG MODE is reported");
  return false;
}

void SerialCli::printConfig() const {
  const LightingConfig& config = configuration_.get();
  const ConfigValidationResult validation = configuration_.validate(config);
  Serial.print("Config v");
  Serial.print(config.configVersion);
  Serial.print(": ");
  Serial.print(config.stripCount);
  Serial.print(" strips, ");
  Serial.print(configuredPixelCount(config));
  Serial.println(" logical pixels");

  uint16_t startPixel = 0;
  for (uint8_t stripIndex = 0; stripIndex < config.stripCount; ++stripIndex) {
    const StripConfig& strip = config.strips[stripIndex];
    Serial.print("  #");
    Serial.print(stripIndex + 1);
    Serial.print(": start=");
    Serial.print(startPixel);
    Serial.print(", count=");
    Serial.print(strip.pixelCount);
    Serial.print(", end=");
    Serial.print(startPixel + strip.pixelCount - 1);
    Serial.print(", reversed=");
    Serial.println(strip.reversed ? "yes" : "no");
    startPixel += strip.pixelCount;
  }

  Serial.print("  brightness=");
  Serial.print(config.maxBrightness);
  Serial.print(", fadeInMs=");
  Serial.print(config.stripFadeInMs);
  Serial.print(", nextStart=");
  Serial.print(config.nextStripStartProgress * 100.0F, 0);
  Serial.print("%, holdMs=");
  Serial.print(config.holdMs);
  Serial.print(", fadeOutMs=");
  Serial.println(config.fadeOutMs);
  Serial.print("  gamma=");
  Serial.print(config.gamma, 2);
  Serial.print(", waterfall=");
  Serial.print(config.enableWaterfall
                   ? waterfallDirectionName(config.waterfallDirection)
                   : "off");
  Serial.print(", waterfallSpeed=");
  Serial.print(config.waterfallSpeedPps);
  Serial.print(" px/s");
  Serial.print(", luxGate=");
  Serial.print(config.enableLuxGate ? "on" : "off");
  Serial.print(", luxThreshold=");
  Serial.println(config.luxThreshold, 1);
  Serial.print("  validation: ");
  Serial.println(configValidationErrorName(validation.error));
}

void SerialCli::printCalibrationState() const {
  Serial.print("Calibration: pixel ");
  Serial.print(setup_.cursor());
  Serial.print(", current strip #");
  Serial.print(setup_.markedBoundaryCount() + 1);
  Serial.print(" starts at ");
  Serial.print(setup_.currentStripStart());
  Serial.print(" (candidate count ");
  Serial.print(setup_.cursor() - setup_.currentStripStart() + 1);
  Serial.println(")");

  if (setup_.markedBoundaryCount() == 0) {
    Serial.println("  no boundaries marked");
    return;
  }

  Serial.print("  marked ends:");
  for (uint8_t index = 0; index < setup_.markedBoundaryCount(); ++index) {
    Serial.print(' ');
    Serial.print(setup_.markedBoundary(index));
  }
  Serial.println();
}

void SerialCli::printSetupError(const SetupResult result) const {
  switch (result) {
    case SetupResult::OK:
      return;
    case SetupResult::ALREADY_ACTIVE:
      Serial.println("Calibration is already active");
      return;
    case SetupResult::NOT_ACTIVE:
      Serial.println("Requested setup mode is not active");
      return;
    case SetupResult::OUT_OF_RANGE:
      Serial.println("Requested pixel or strip is outside the allowed range");
      return;
    case SetupResult::TOO_MANY_STRIPS:
      Serial.println("Maximum strip count reached");
      return;
    case SetupResult::NOTHING_TO_UNDO:
      Serial.println("No boundary to undo");
      return;
    case SetupResult::INVALID_CONFIG:
      Serial.print("Calibration result is invalid: ");
      Serial.println(
          configValidationErrorName(setup_.lastValidation().error));
      return;
  }
}

void SerialCli::handleCalibrationCommand(const char* command) {
  if (strcmp(command, "start") == 0) {
    const SetupResult result = setup_.startCalibration();
    if (result != SetupResult::OK) {
      printSetupError(result);
      return;
    }
    Serial.println(
        "Calibration started; completed ranges are dim, current range is "
        "brighter, cursor is brightest");
    printCalibrationState();
    return;
  }
  if (strcmp(command, "cancel") == 0) {
    const SetupResult result = setup_.cancelCalibration();
    if (result != SetupResult::OK) {
      printSetupError(result);
      return;
    }
    Serial.println("Calibration cancelled; active config unchanged");
    return;
  }
  if (strcmp(command, "mark") == 0) {
    const SetupResult result = setup_.markBoundary();
    if (result != SetupResult::OK) {
      printSetupError(result);
      return;
    }
    Serial.println("Boundary marked");
    printCalibrationState();
    return;
  }
  if (strcmp(command, "undo") == 0) {
    const SetupResult result = setup_.undoBoundary();
    if (result != SetupResult::OK) {
      printSetupError(result);
      return;
    }
    Serial.println("Last boundary removed");
    printCalibrationState();
    return;
  }
  if (strcmp(command, "finish") == 0) {
    const SetupResult result = setup_.finishCalibration();
    if (result != SetupResult::OK) {
      printSetupError(result);
      return;
    }
    configurationChanged_ = true;
    Serial.println(
        "Calibration applied in RAM; run 'config save' to persist it");
    printConfig();
    return;
  }

  unsigned int value = 0;
  SetupResult result = SetupResult::OUT_OF_RANGE;
  if (strcmp(command, "next") == 0 || strcmp(command, "+") == 0) {
    result = setup_.moveCursor(1);
  } else if (strcmp(command, "prev") == 0 || strcmp(command, "-") == 0) {
    result = setup_.moveCursor(-1);
  } else if (sscanf(command, "+ %u", &value) == 1 && value <= INT16_MAX) {
    result = setup_.moveCursor(static_cast<int16_t>(value));
  } else if (sscanf(command, "- %u", &value) == 1 && value <= INT16_MAX) {
    result = setup_.moveCursor(-static_cast<int16_t>(value));
  } else if (sscanf(command, "goto %u", &value) == 1 && value <= UINT16_MAX) {
    result = setup_.goToPixel(static_cast<uint16_t>(value));
  } else {
    Serial.println("Unknown calibration command. Type 'help'.");
    return;
  }

  if (result != SetupResult::OK) {
    printSetupError(result);
    return;
  }
  printCalibrationState();
}

void SerialCli::handleStripCommand(const char* command) {
  if (strcmp(command, "off") == 0) {
    const SetupResult result = setup_.stopStripTest();
    if (result != SetupResult::OK) {
      printSetupError(result);
      return;
    }
    Serial.println("Strip test stopped");
    return;
  }
  if (setup_.isCalibrationActive()) {
    Serial.println("Finish or cancel calibration before testing a strip");
    return;
  }

  unsigned int stripNumber = 0;
  if (sscanf(command, "%u", &stripNumber) != 1 || stripNumber > UINT8_MAX) {
    Serial.println("Usage: strip test <n> | strip test off");
    return;
  }

  const SetupResult result =
      setup_.testStrip(static_cast<uint8_t>(stripNumber));
  if (result != SetupResult::OK) {
    printSetupError(result);
    return;
  }
  Serial.print("Testing strip #");
  Serial.print(stripNumber);
  Serial.println("; run 'strip test off' to resume normal operation");
}
