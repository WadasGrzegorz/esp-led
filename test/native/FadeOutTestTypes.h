#pragma once

#include "src/config/LightingConfig.h"

struct FadeOutCase {
  bool waterfall;
  uint8_t brightness;
  uint32_t durationMs;
  float gamma;
  FadeOutStyle style;
  StripStartMode startMode;
  WaterfallDirection waterfallDirection;
  bool continuousHigh;
  uint32_t startMs;
  bool fullBuffer;
  uint32_t transitionMs = 0;
  WaterfallDirection fadeOutDirection = WaterfallDirection::TOP_TO_BOTTOM;
  bool reverseFadeOutOrder = false;
};
