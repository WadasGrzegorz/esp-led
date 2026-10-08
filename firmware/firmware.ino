#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "src/config/ConfigurationManager.h"
#include "src/led/AnimationEngine.h"
#include "src/led/LedController.h"
#include "src/mode/OperatingModeController.h"
#include "src/sensors/Bh1750Sensor.h"
#include "src/sensors/PirSensor.h"
#include "src/serial/SerialCli.h"
#include "src/setup/SetupController.h"
#include "src/web/WebConfigServer.h"

namespace {
CRGB leds[hardware::WS2811_CONTROLLER_COUNT];

ConfigurationManager configuration(DEFAULT_LIGHTING_CONFIG,
                                   hardware::LOGICAL_PIXEL_COUNT);
LedController ledController(
    leds,
    hardware::WS2811_CONTROLLER_COUNT,
    hardware::LOGICAL_PIXEL_COUNT);
AnimationEngine animation(ledController, configuration.get());
SetupController setupController(ledController, configuration,
                                hardware::LOGICAL_PIXEL_COUNT);
OperatingModeController operatingMode(hardware::CONFIG_BUTTON_PIN,
                                      hardware::CONFIG_BUTTON_HOLD_MS,
                                      hardware::CONFIG_MODE_TIMEOUT_MS);
SerialCli serialCli(configuration, setupController, operatingMode);
PirSensor leftPir(hardware::LEFT_PIR_PIN, hardware::PIR_DEBOUNCE_MS,
                  hardware::PIR_MIN_RETRIGGER_MS);
PirSensor rightPir(hardware::RIGHT_PIR_PIN, hardware::PIR_DEBOUNCE_MS,
                   hardware::PIR_MIN_RETRIGGER_MS);
Bh1750Sensor lightSensor(hardware::BH1750_LOW_ADDRESS,
                         hardware::BH1750_HIGH_ADDRESS,
                         hardware::LUX_LOG_INTERVAL_MS);

uint32_t pirStabilizationStartedAtMs = 0;
uint32_t lastLightSensorDetectionAtMs = 0;
uint32_t lastLedActivityAtMs = 0;
bool pirSensingActive = false;
bool lightSensorAvailable = false;
bool lightSensorFailureReported = false;
bool i2cAvailable = false;
bool i2cResetRequired = false;
bool hasLuxReading = false;
float currentLux = 0.0F;
bool serviceModeWasActive = false;
bool configModeWasActive = false;
bool normalRestartPrepared = false;

const WebConfigDependencies webConfigDependencies = {
    configuration,         setupController, operatingMode, animation,
    ledController,         lightSensorAvailable, hasLuxReading, currentLux};
const WebConfigSettings webConfigSettings = {
    hardware::LOGICAL_PIXEL_COUNT,
    hardware::CONFIG_STRIP_TEST_TIMEOUT_MS,
    hardware::CONFIG_SAVE_RESTART_DELAY_MS,
};
WebConfigServer webConfig(webConfigDependencies, webConfigSettings);

void printHexAddress(const uint8_t address) {
  Serial.print("0x");
  if (address < 0x10) {
    Serial.print('0');
  }
  Serial.print(address, HEX);
}

void scanI2cBus() {
  uint8_t foundCount = 0;
  Serial.print("I2C scan:");

  for (uint8_t address = 1; address < 0x7F; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() != 0) {
      continue;
    }

    Serial.print(' ');
    printHexAddress(address);
    ++foundCount;
  }

  if (foundCount == 0) {
    Serial.print(" no devices found");
  }
  Serial.println();
}

bool startI2cBus() {
  if (i2cResetRequired) {
    Wire.end();
    i2cResetRequired = false;
  }

  i2cAvailable = Wire.begin(hardware::BH1750_SDA_PIN,
                            hardware::BH1750_SCL_PIN, 100000);
  if (i2cAvailable) {
    Wire.setTimeOut(25);
  }
  return i2cAvailable;
}

void detectLightSensor(const uint32_t nowMs) {
  lastLightSensorDetectionAtMs = nowMs;
  if (!i2cAvailable && !startI2cBus()) {
    Serial.println("I2C bus initialization failed (SDA GPIO2, SCL GPIO3)");
    return;
  }

  lightSensorAvailable = lightSensor.begin(nowMs);

  if (lightSensorAvailable) {
    Serial.print(lightSensorFailureReported ? "BH1750 recovered at "
                                            : "BH1750 detected at ");
    printHexAddress(lightSensor.address());
    Serial.println();
    lightSensorFailureReported = false;
    return;
  }

  if (!lightSensorFailureReported) {
    Serial.println("BH1750 unavailable; I2C scan follows");
    scanI2cBus();
    lightSensorFailureReported = true;
  }
  i2cAvailable = false;
  i2cResetRequired = true;
}

void logLux(const uint32_t nowMs) {
  const bool ledsActive = !animation.isOff() ||
                          setupController.isServiceModeActive();
  if (ledsActive) {
    lastLedActivityAtMs = nowMs;
    return;
  }
  if (nowMs - lastLedActivityAtMs < hardware::LUX_AFTER_LED_SETTLE_MS) {
    return;
  }

  if (!lightSensorAvailable) {
    if (nowMs - lastLightSensorDetectionAtMs >=
        hardware::BH1750_RETRY_INTERVAL_MS) {
      detectLightSensor(nowMs);
    }
    return;
  }

  float lux = 0.0F;
  const Bh1750UpdateResult result = lightSensor.update(nowMs, lux);
  if (result == Bh1750UpdateResult::WAITING) {
    return;
  }
  if (result == Bh1750UpdateResult::IO_ERROR) {
    lightSensorAvailable = false;
    hasLuxReading = false;
    i2cAvailable = false;
    i2cResetRequired = true;
    lastLightSensorDetectionAtMs = nowMs;
    if (!lightSensorFailureReported) {
      Serial.println(
          "BH1750 read failed; I2C reset and sensor retry scheduled");
      lightSensorFailureReported = true;
    }
    return;
  }

  currentLux = lux;
  hasLuxReading = true;
  Serial.print("Lux: ");
  Serial.println(lux, 1);
}

void triggerLightingFromMotion(const AnimationEngine::Direction direction,
                               const uint32_t nowMs) {
  const LightingConfig& lightingConfig = configuration.get();
  if (!animation.isOff()) {
    animation.trigger(direction, nowMs);
    return;
  }

  if (!lightingConfig.enableLuxGate) {
    animation.trigger(direction, nowMs);
    return;
  }

  if (!hasLuxReading) {
    Serial.println("Motion ignored: lux unavailable");
    return;
  }

  if (currentLux < lightingConfig.luxThreshold) {
    animation.trigger(direction, nowMs);
    return;
  }

  Serial.print("Motion ignored: ");
  Serial.print(currentLux, 1);
  Serial.print(" lux (threshold < ");
  Serial.print(lightingConfig.luxThreshold, 1);
  Serial.println(")");
}

void applyActiveConfiguration(const uint32_t nowMs,
                              const bool useSafeTestBrightness) {
  const LightingConfig& lightingConfig = configuration.get();
  const uint8_t brightness =
      useSafeTestBrightness
          ? min(lightingConfig.maxBrightness,
                hardware::CONFIG_TEST_MAX_BRIGHTNESS)
          : lightingConfig.maxBrightness;
  ledController.configure(brightness, lightingConfig.gamma);
  animation.begin(nowMs);
  Serial.print("Active geometry: ");
  Serial.print(lightingConfig.stripCount);
  Serial.print(" strips, ");
  Serial.print(configuredPixelCount(lightingConfig));
  Serial.println(" logical pixels");
}

bool restartNormalIfRequested() {
  if (!operatingMode.shouldRestartNormal()) {
    return false;
  }

  if (!normalRestartPrepared) {
    webConfig.end();
    ledController.clear();
    ledController.show();
    normalRestartPrepared = true;
    if (!operatingMode.isButtonReleased()) {
      Serial.println("Release BOOT to restart safely into NORMAL");
    }
  }

  // GPIO9 LOW during reset selects the ESP32-C6 ROM downloader. Waiting for
  // release guarantees that a CONFIG MODE exit returns to the application.
  if (!operatingMode.isButtonReleased()) {
    return true;
  }

  Serial.flush();
  delay(20);
  ESP.restart();
  return true;
}
}  // namespace

void setup() {
  Serial.begin(hardware::SERIAL_BAUD_RATE);
  webConfig.ensureWirelessOff();
  const uint32_t setupStartedAtMs = millis();
  operatingMode.begin(setupStartedAtMs);

  configuration.begin();
  const LightingConfig& lightingConfig = configuration.get();
  ledController.begin(lightingConfig.maxBrightness, lightingConfig.gamma);
  leftPir.begin(setupStartedAtMs);
  rightPir.begin(setupStartedAtMs);
  startI2cBus();

  const uint32_t nowMs = millis();
  animation.begin(nowMs);
  pirStabilizationStartedAtMs = nowMs;
  detectLightSensor(nowMs);

  Serial.print("LED controllers: ");
  Serial.print(hardware::WS2811_CONTROLLER_COUNT);
  Serial.print(", logical monochrome pixels: ");
  Serial.println(hardware::LOGICAL_PIXEL_COUNT);
  Serial.print("Active geometry: ");
  Serial.print(lightingConfig.stripCount);
  Serial.print(" strips, ");
  Serial.print(configuredPixelCount(lightingConfig));
  Serial.println(" logical pixels (use 'config show' for details)");
  Serial.println(lightingConfig.enableLuxGate
                     ? "Lux gate enabled"
                     : "Lux gate disabled; BH1750 is diagnostic only");
  serialCli.printHelp();
}

void loop() {
  const uint32_t nowMs = millis();
  operatingMode.update(nowMs);
  if (restartNormalIfRequested()) {
    return;
  }

  const bool configModeActive = operatingMode.isConfigMode();
  if (!configModeWasActive && configModeActive) {
    applyActiveConfiguration(nowMs, true);
    Serial.println("PIR and lux/BH1750 triggers ignored in CONFIG MODE");
    if (!webConfig.begin(nowMs)) {
      Serial.println(
          "Web Config unavailable; Serial service fallback remains active");
    }
    serialCli.printHelp();
  }
  configModeWasActive = configModeActive;

  serialCli.update();
  webConfig.update(nowMs);
  if (restartNormalIfRequested()) {
    return;
  }

  const uint32_t runtimeNowMs = millis();
  const bool configurationChanged =
      serialCli.consumeConfigurationChanged() ||
      webConfig.consumeConfigurationChanged();
  const bool serviceModeActive = setupController.isServiceModeActive();
  if (configurationChanged) {
    applyActiveConfiguration(runtimeNowMs, configModeActive);
    if (serviceModeActive) {
      setupController.refreshDisplay();
    }
  }

  if (serviceModeWasActive && !serviceModeActive && !configurationChanged &&
      !webConfig.isAnimationTestActive()) {
    animation.begin(runtimeNowMs);
  }
  serviceModeWasActive = serviceModeActive;

  logLux(runtimeNowMs);

  const bool leftTriggered = leftPir.update(runtimeNowMs);
  const bool rightTriggered = rightPir.update(runtimeNowMs);

  if (!pirSensingActive) {
    // Keep sampling during warm-up so a HIGH startup pulse cannot become a
    // synthetic rising edge when sensing is enabled.
    if (runtimeNowMs - pirStabilizationStartedAtMs >=
        hardware::PIR_STABILIZATION_MS) {
      pirSensingActive = true;
      Serial.println("PIR motion sensing active");
    }

    if (configModeActive && webConfig.isAnimationTestActive() &&
        !serviceModeActive) {
      animation.update(runtimeNowMs, false);
    } else if (!configModeActive && !serviceModeActive) {
      animation.update(runtimeNowMs, false);
    }
    return;
  }

  if (configModeActive) {
    if (webConfig.isAnimationTestActive() && !serviceModeActive) {
      animation.update(runtimeNowMs, false);
    }
    return;
  }

  if (serviceModeActive) {
    return;
  }

  if (leftTriggered) {
    triggerLightingFromMotion(AnimationEngine::Direction::LEFT_TO_RIGHT,
                              runtimeNowMs);
  }

  if (rightTriggered) {
    triggerLightingFromMotion(AnimationEngine::Direction::RIGHT_TO_LEFT,
                              runtimeNowMs);
  }

  const bool motionActive = leftPir.isActive() || rightPir.isActive();
  animation.update(runtimeNowMs, motionActive);
}
