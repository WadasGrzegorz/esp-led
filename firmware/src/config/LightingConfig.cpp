#include "LightingConfig.h"

#include <math.h>

namespace {
constexpr uint32_t MAX_FADE_MS = 10UL * 60UL * 1000UL;
constexpr uint32_t MAX_HOLD_MS = 60UL * 60UL * 1000UL;
constexpr float MIN_GAMMA = 0.1F;
constexpr float MAX_GAMMA = 5.0F;
constexpr float MAX_LUX_THRESHOLD = 100000.0F;

ConfigValidationResult invalid(const ConfigValidationError error,
                               const uint8_t stripIndex,
                               const uint16_t totalPixelCount) {
  return {error, stripIndex, totalPixelCount};
}
}  // namespace

ConfigValidationResult validateLightingConfig(const LightingConfig& config,
                                              const uint16_t logicalPixelLimit) {
  if (config.configVersion != LIGHTING_CONFIG_VERSION) {
    return invalid(ConfigValidationError::VERSION, 0, 0);
  }

  if (config.stripCount == 0 || config.stripCount > MAX_STRIPS) {
    return invalid(ConfigValidationError::STRIP_COUNT, 0, 0);
  }

  uint32_t totalPixelCount = 0;
  for (uint8_t stripIndex = 0; stripIndex < config.stripCount; ++stripIndex) {
    if (config.strips[stripIndex].pixelCount == 0) {
      return invalid(ConfigValidationError::EMPTY_STRIP, stripIndex,
                     totalPixelCount);
    }

    totalPixelCount += config.strips[stripIndex].pixelCount;
    if (totalPixelCount > logicalPixelLimit) {
      return invalid(ConfigValidationError::PIXEL_LIMIT, stripIndex,
                     totalPixelCount);
    }
  }

  if (config.maxBrightness == 0) {
    return invalid(ConfigValidationError::BRIGHTNESS, 0, totalPixelCount);
  }
  if (config.stripFadeInMs == 0 || config.stripFadeInMs > MAX_FADE_MS) {
    return invalid(ConfigValidationError::FADE_IN, 0, totalPixelCount);
  }
  if (!isfinite(config.nextStripStartProgress) ||
      config.nextStripStartProgress <= 0.0F ||
      config.nextStripStartProgress > 1.0F) {
    return invalid(ConfigValidationError::OVERLAP, 0, totalPixelCount);
  }
  if (config.stripStartMode != StripStartMode::CASCADE &&
      config.stripStartMode != StripStartMode::SIMULTANEOUS) {
    return invalid(ConfigValidationError::STRIP_START_MODE, 0,
                   totalPixelCount);
  }
  if (config.holdMs == 0 || config.holdMs > MAX_HOLD_MS) {
    return invalid(ConfigValidationError::HOLD, 0, totalPixelCount);
  }
  if (config.fadeOutMs == 0 || config.fadeOutMs > MAX_FADE_MS) {
    return invalid(ConfigValidationError::FADE_OUT, 0, totalPixelCount);
  }
  if (config.fadeOutStyle != FadeOutStyle::GLOBAL &&
      config.fadeOutStyle != FadeOutStyle::CASCADE) {
    return invalid(ConfigValidationError::FADE_OUT_STYLE, 0,
                   totalPixelCount);
  }
  if (!isfinite(config.gamma) || config.gamma < MIN_GAMMA ||
      config.gamma > MAX_GAMMA) {
    return invalid(ConfigValidationError::GAMMA, 0, totalPixelCount);
  }
  if (config.waterfallDirection != WaterfallDirection::TOP_TO_BOTTOM &&
      config.waterfallDirection != WaterfallDirection::BOTTOM_TO_TOP) {
    return invalid(ConfigValidationError::WATERFALL_DIRECTION, 0,
                   totalPixelCount);
  }
  if (config.waterfallSpeedPps < MIN_WATERFALL_SPEED_PPS ||
      config.waterfallSpeedPps > MAX_WATERFALL_SPEED_PPS) {
    return invalid(ConfigValidationError::WATERFALL_SPEED, 0,
                   totalPixelCount);
  }
  if (!isfinite(config.luxThreshold) || config.luxThreshold < 0.0F ||
      config.luxThreshold > MAX_LUX_THRESHOLD) {
    return invalid(ConfigValidationError::LUX_THRESHOLD, 0, totalPixelCount);
  }

  return {ConfigValidationError::NONE, 0,
          static_cast<uint16_t>(totalPixelCount)};
}

const char* configValidationErrorName(const ConfigValidationError error) {
  switch (error) {
    case ConfigValidationError::NONE:
      return "valid";
    case ConfigValidationError::VERSION:
      return "unsupported config version";
    case ConfigValidationError::STRIP_COUNT:
      return "strip count must be 1..MAX_STRIPS";
    case ConfigValidationError::EMPTY_STRIP:
      return "every strip must contain at least one pixel";
    case ConfigValidationError::PIXEL_LIMIT:
      return "configured pixels exceed the LED buffer";
    case ConfigValidationError::BRIGHTNESS:
      return "brightness must be greater than zero";
    case ConfigValidationError::FADE_IN:
      return "fade-in duration is outside the supported range";
    case ConfigValidationError::OVERLAP:
      return "next-strip progress must be greater than 0 and at most 1";
    case ConfigValidationError::STRIP_START_MODE:
      return "unsupported strip start mode";
    case ConfigValidationError::HOLD:
      return "hold duration must be greater than zero and within the supported range";
    case ConfigValidationError::FADE_OUT:
      return "fade-out duration is outside the supported range";
    case ConfigValidationError::FADE_OUT_STYLE:
      return "unsupported fade-out style";
    case ConfigValidationError::GAMMA:
      return "gamma must be between 0.1 and 5.0";
    case ConfigValidationError::WATERFALL_DIRECTION:
      return "unsupported waterfall direction";
    case ConfigValidationError::WATERFALL_SPEED:
      return "waterfall speed is outside the supported range";
    case ConfigValidationError::LUX_THRESHOLD:
      return "lux threshold is outside the supported range";
  }

  return "unknown validation error";
}

const char* waterfallDirectionName(const WaterfallDirection direction) {
  return direction == WaterfallDirection::TOP_TO_BOTTOM ? "top-to-bottom"
                                                         : "bottom-to-top";
}

const char* fadeOutStyleName(const FadeOutStyle style) {
  return style == FadeOutStyle::CASCADE ? "cascade" : "global";
}

const char* stripStartModeName(const StripStartMode mode) {
  return mode == StripStartMode::SIMULTANEOUS ? "simultaneous" : "cascade";
}

uint16_t configuredPixelCount(const LightingConfig& config) {
  uint16_t totalPixelCount = 0;
  for (uint8_t stripIndex = 0; stripIndex < config.stripCount; ++stripIndex) {
    totalPixelCount += config.strips[stripIndex].pixelCount;
  }
  return totalPixelCount;
}
