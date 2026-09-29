#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "src/led/AnimationEngine.h"
#include "src/led/LedController.h"
#include "src/sensors/Bh1750Sensor.h"
#include "src/sensors/PirSensor.h"

namespace {
CRGB leds[hardware::WS2811_CONTROLLER_COUNT];

LedController ledController(
    leds,
    hardware::WS2811_CONTROLLER_COUNT,
    hardware::LOGICAL_PIXEL_COUNT);
AnimationEngine animation(ledController, lightingConfig);
PirSensor leftPir(hardware::LEFT_PIR_PIN);
PirSensor rightPir(hardware::RIGHT_PIR_PIN);
Bh1750Sensor lightSensor(hardware::BH1750_LOW_ADDRESS,
                         hardware::BH1750_HIGH_ADDRESS,
                         hardware::LUX_LOG_INTERVAL_MS);

uint32_t pirStabilizationStartedAtMs = 0;
uint32_t lastLightSensorDetectionAtMs = 0;
bool pirSensingActive = false;
bool lightSensorAvailable = false;
bool i2cAvailable = false;
bool hasLuxReading = false;
float currentLux = 0.0F;

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

void detectLightSensor(const uint32_t nowMs) {
  lastLightSensorDetectionAtMs = nowMs;
  if (!i2cAvailable) {
    Serial.println("I2C bus initialization failed (SDA GPIO2, SCL GPIO3)");
    return;
  }

  lightSensorAvailable = lightSensor.begin(nowMs);

  if (lightSensorAvailable) {
    Serial.print("BH1750 detected at ");
    printHexAddress(lightSensor.address());
    Serial.println();
  } else {
    Serial.println("BH1750 not detected at 0x23 or 0x5C");
    scanI2cBus();
  }
}

void logLux(const uint32_t nowMs) {
  if (!lightSensorAvailable) {
    if (nowMs - lastLightSensorDetectionAtMs >=
        hardware::BH1750_RETRY_INTERVAL_MS) {
      detectLightSensor(nowMs);
    }
    return;
  }

  float lux = 0.0F;
  if (!lightSensor.update(nowMs, lux)) {
    return;
  }

  currentLux = lux;
  hasLuxReading = true;
  Serial.print("Lux: ");
  Serial.println(lux, 1);
}

void triggerLightingFromMotion(const AnimationEngine::Direction direction,
                               const uint32_t nowMs) {
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
}  // namespace

void setup() {
  Serial.begin(hardware::SERIAL_BAUD_RATE);

  ledController.begin(lightingConfig.maxBrightness, lightingConfig.gamma);
  leftPir.begin();
  rightPir.begin();
  i2cAvailable =
      Wire.begin(hardware::BH1750_SDA_PIN, hardware::BH1750_SCL_PIN);

  const uint32_t nowMs = millis();
  animation.begin(nowMs);
  pirStabilizationStartedAtMs = nowMs;
  detectLightSensor(nowMs);

  Serial.print("LED controllers: ");
  Serial.print(hardware::WS2811_CONTROLLER_COUNT);
  Serial.print(", logical monochrome pixels: ");
  Serial.println(hardware::LOGICAL_PIXEL_COUNT);
  Serial.println(
      "Strip geometry from DIN: #1=18, #2=54, #3=54, #4=54 (total 180)");
  Serial.println(lightingConfig.enableLuxGate
                     ? "Lux gate enabled"
                     : "Lux gate disabled; BH1750 is diagnostic only");
}

void loop() {
  const uint32_t nowMs = millis();
  logLux(nowMs);

  if (!pirSensingActive) {
    // Keep sampling during warm-up so a HIGH startup pulse cannot become a
    // synthetic rising edge when sensing is enabled.
    leftPir.update();
    rightPir.update();

    if (nowMs - pirStabilizationStartedAtMs >=
        hardware::PIR_STABILIZATION_MS) {
      pirSensingActive = true;
      Serial.println("PIR motion sensing active");
    }

    animation.update(nowMs);
    return;
  }

  const bool leftTriggered = leftPir.update();
  const bool rightTriggered = rightPir.update();

  if (leftTriggered) {
    triggerLightingFromMotion(AnimationEngine::Direction::LEFT_TO_RIGHT,
                              nowMs);
  }

  if (rightTriggered) {
    triggerLightingFromMotion(AnimationEngine::Direction::RIGHT_TO_LEFT,
                              nowMs);
  }

  animation.update(nowMs);
}
