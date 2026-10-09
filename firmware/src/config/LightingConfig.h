#pragma once

#include <Arduino.h>

constexpr uint16_t LIGHTING_CONFIG_VERSION = 4;
constexpr uint8_t MAX_STRIPS = 16;
constexpr uint16_t MIN_WATERFALL_SPEED_PPS = 5;
constexpr uint16_t MAX_WATERFALL_SPEED_PPS = 120;

enum class WaterfallDirection : uint8_t {
  TOP_TO_BOTTOM = 0,
  BOTTOM_TO_TOP = 1,
};

enum class FadeOutStyle : uint8_t {
  GLOBAL = 0,
  CASCADE = 1,
};

enum class StripStartMode : uint8_t {
  CASCADE = 0,
  SIMULTANEOUS = 1,
};

struct StripConfig {
  uint16_t pixelCount;
  bool reversed;
};

struct LightingConfig {
  uint16_t configVersion;
  uint8_t stripCount;
  uint8_t maxBrightness;
  uint32_t stripFadeInMs;
  float nextStripStartProgress;
  StripStartMode stripStartMode;
  uint32_t holdMs;
  uint32_t fadeOutMs;
  FadeOutStyle fadeOutStyle;
  float gamma;
  bool enableWaterfall;
  WaterfallDirection waterfallDirection;
  uint16_t waterfallSpeedPps;
  bool enableLuxGate;
  float luxThreshold;
  StripConfig strips[MAX_STRIPS];
};

enum class ConfigValidationError : uint8_t {
  NONE,
  VERSION,
  STRIP_COUNT,
  EMPTY_STRIP,
  PIXEL_LIMIT,
  BRIGHTNESS,
  FADE_IN,
  OVERLAP,
  STRIP_START_MODE,
  HOLD,
  FADE_OUT,
  FADE_OUT_STYLE,
  GAMMA,
  WATERFALL_DIRECTION,
  WATERFALL_SPEED,
  LUX_THRESHOLD,
};

struct ConfigValidationResult {
  ConfigValidationError error;
  uint8_t stripIndex;
  uint16_t totalPixelCount;

  bool valid() const { return error == ConfigValidationError::NONE; }
};

ConfigValidationResult validateLightingConfig(const LightingConfig& config,
                                              uint16_t logicalPixelLimit);
const char* configValidationErrorName(ConfigValidationError error);
const char* waterfallDirectionName(WaterfallDirection direction);
const char* fadeOutStyleName(FadeOutStyle style);
const char* stripStartModeName(StripStartMode mode);
uint16_t configuredPixelCount(const LightingConfig& config);
