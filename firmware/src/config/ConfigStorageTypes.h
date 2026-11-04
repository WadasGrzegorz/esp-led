#pragma once

#include "LightingConfig.h"

// Preserve the exact NVS layouts used before startDelayMs was introduced.
struct LegacyLightingConfigV1 {
  uint16_t configVersion;
  uint8_t stripCount;
  uint8_t maxBrightness;
  uint32_t stripFadeInMs;
  float nextStripStartProgress;
  uint32_t holdMs;
  uint32_t fadeOutMs;
  float gamma;
  bool enableLuxGate;
  float luxThreshold;
  StripConfig strips[MAX_STRIPS];
};

struct LegacyLightingConfigV2 {
  uint16_t configVersion;
  uint8_t stripCount;
  uint8_t maxBrightness;
  uint32_t stripFadeInMs;
  float nextStripStartProgress;
  uint32_t holdMs;
  uint32_t fadeOutMs;
  float gamma;
  bool enableWaterfall;
  WaterfallDirection waterfallDirection;
  bool enableLuxGate;
  float luxThreshold;
  StripConfig strips[MAX_STRIPS];
};

struct LegacyLightingConfigV3 {
  uint16_t configVersion;
  uint8_t stripCount;
  uint8_t maxBrightness;
  uint32_t stripFadeInMs;
  float nextStripStartProgress;
  uint32_t holdMs;
  uint32_t fadeOutMs;
  float gamma;
  bool enableWaterfall;
  WaterfallDirection waterfallDirection;
  uint16_t waterfallSpeedPps;
  bool enableLuxGate;
  float luxThreshold;
  StripConfig strips[MAX_STRIPS];
};

struct LegacyLightingConfigV4 {
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
