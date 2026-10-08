#include "AnimationEngine.h"

namespace {
constexpr uint8_t FULL_LEVEL = 255;
constexpr uint32_t MIN_WATERFALL_PIXEL_FADE_MS = 120;
constexpr uint32_t MAX_WATERFALL_PIXEL_FADE_MS = 400;
constexpr uint8_t WATERFALL_FADE_WIDTH_PIXELS = 8;
}

AnimationEngine::AnimationEngine(LedController& leds,
                                 const LightingConfig& config)
    : leds_(leds), config_(config) {}

void AnimationEngine::begin(const uint32_t nowMs) {
  state_ = State::OFF;
  stateStartedAtMs_ = nowMs;
  lastRenderedAtMs_ = UINT32_MAX;
  for (uint8_t stripIndex = 0; stripIndex < MAX_STRIPS; ++stripIndex) {
    stripLevels_[stripIndex] = 0;
    fadeInStartLevels_[stripIndex] = 0;
    fadeOutStartLevels_[stripIndex] = 0;
    stripFadeInStartedAtMs_[stripIndex] = 0;
    stripFadeInDurationsMs_[stripIndex] = 0;
    stripStartLogged_[stripIndex] = false;
  }
  leds_.clear();
  leds_.show();
  Serial.print("All ");
  Serial.print(config_.stripCount);
  Serial.println(" strips OFF");
}

void AnimationEngine::update(const uint32_t nowMs, const bool motionActive) {
  if (lastRenderedAtMs_ == nowMs) {
    return;
  }

  lastRenderedAtMs_ = nowMs;
  const uint32_t elapsedMs = nowMs - stateStartedAtMs_;

  switch (state_) {
    case State::OFF:
      return;
    case State::FADING_IN:
      renderFadeIn(elapsedMs);
      if (elapsedMs >= cascadeDurationMs_) {
        enterOn(nowMs);
      }
      return;
    case State::ON:
      if (motionActive) {
        // The hold timer represents time since the PIRs last reported
        // presence, not time since the cascade completed.
        stateStartedAtMs_ = nowMs;
        return;
      }
      if (elapsedMs >= config_.holdMs) {
        startFadeOut(nowMs);
      }
      return;
    case State::FADING_OUT:
      renderFadeOut(elapsedMs);
      if (elapsedMs >= config_.fadeOutMs) {
        enterOff(nowMs);
      }
      return;
  }
}

void AnimationEngine::trigger(const Direction direction,
                              const uint32_t nowMs) {
  switch (state_) {
    case State::OFF:
      startCascade(direction, nowMs);
      return;
    case State::FADING_IN:
      Serial.print("Motion ");
      Serial.print(directionName(direction));
      Serial.print(" during cascade -> continuing ");
      printCascadeName();
      Serial.println();
      return;
    case State::ON:
      stateStartedAtMs_ = nowMs;
      Serial.print("Hold extended by motion ");
      Serial.println(directionName(direction));
      return;
    case State::FADING_OUT:
      updateFadeOutLevels(nowMs - stateStartedAtMs_);
      startCascade(direction, nowMs);
      return;
  }
}

bool AnimationEngine::isOff() const { return state_ == State::OFF; }

void AnimationEngine::startCascade(const Direction direction,
                                   const uint32_t nowMs) {
  direction_ = direction;
  state_ = State::FADING_IN;
  stateStartedAtMs_ = nowMs;
  cascadeDurationMs_ = 0;

  uint32_t nextStripStartMs = 0;
  for (uint8_t orderPosition = 0; orderPosition < config_.stripCount;
       ++orderPosition) {
    const uint8_t stripIndex = stripIndexAt(orderPosition);
    const uint8_t startLevel = stripLevels_[stripIndex];
    const uint32_t fullDurationMs =
        config_.enableWaterfall
            ? waterfallStripDurationMs(config_.strips[stripIndex].pixelCount)
            : config_.stripFadeInMs;
    const uint32_t durationMs =
        scaledDuration(fullDurationMs, FULL_LEVEL - startLevel);

    fadeInStartLevels_[stripIndex] = startLevel;
    stripFadeInStartedAtMs_[stripIndex] = nextStripStartMs;
    stripFadeInDurationsMs_[stripIndex] = durationMs;
    stripStartLogged_[stripIndex] = false;
    cascadeDurationMs_ = max(cascadeDurationMs_, nextStripStartMs + durationMs);
    nextStripStartMs += static_cast<uint32_t>(
        durationMs * config_.nextStripStartProgress + 0.5F);
  }

  Serial.print("Motion ");
  Serial.print(directionName(direction_));
  Serial.print(" -> cascade ");
  printCascadeName();
  Serial.println();
  logNewlyStartedStrips(0);
}

void AnimationEngine::startFadeOut(const uint32_t nowMs) {
  for (uint8_t stripIndex = 0; stripIndex < config_.stripCount;
       ++stripIndex) {
    fadeOutStartLevels_[stripIndex] = stripLevels_[stripIndex];
  }

  state_ = State::FADING_OUT;
  stateStartedAtMs_ = nowMs;
  Serial.println("Global fade-out started");
}

void AnimationEngine::enterOn(const uint32_t nowMs) {
  state_ = State::ON;
  stateStartedAtMs_ = nowMs;

  for (uint8_t stripIndex = 0; stripIndex < config_.stripCount;
       ++stripIndex) {
    stripLevels_[stripIndex] = FULL_LEVEL;
  }

  renderLevels();
  Serial.print("All ");
  Serial.print(config_.stripCount);
  Serial.println(" strips ON");
}

void AnimationEngine::enterOff(const uint32_t nowMs) {
  state_ = State::OFF;
  stateStartedAtMs_ = nowMs;

  for (uint8_t stripIndex = 0; stripIndex < config_.stripCount;
       ++stripIndex) {
    stripLevels_[stripIndex] = 0;
  }

  renderLevels();
  Serial.print("All ");
  Serial.print(config_.stripCount);
  Serial.println(" strips OFF");
}

void AnimationEngine::renderFadeIn(const uint32_t elapsedMs) {
  logNewlyStartedStrips(elapsedMs);

  for (uint8_t stripIndex = 0; stripIndex < config_.stripCount;
       ++stripIndex) {
    const uint32_t startsAtMs = stripFadeInStartedAtMs_[stripIndex];
    if (elapsedMs < startsAtMs) {
      stripLevels_[stripIndex] = fadeInStartLevels_[stripIndex];
      continue;
    }

    const uint8_t progress = fadeLevel(
        elapsedMs - startsAtMs, stripFadeInDurationsMs_[stripIndex]);
    const uint16_t levelDistance =
        FULL_LEVEL - fadeInStartLevels_[stripIndex];
    stripLevels_[stripIndex] =
        fadeInStartLevels_[stripIndex] +
        (levelDistance * progress + FULL_LEVEL / 2) / FULL_LEVEL;
  }

  if (config_.enableWaterfall) {
    renderWaterfallFadeIn(elapsedMs);
  } else {
    renderLevels();
  }
}

void AnimationEngine::renderWaterfallFadeIn(const uint32_t elapsedMs) {
  uint16_t stripStartPixel = 0;
  const bool bottomToTop =
      config_.waterfallDirection == WaterfallDirection::BOTTOM_TO_TOP;

  for (uint8_t stripIndex = 0; stripIndex < config_.stripCount;
       ++stripIndex) {
    const StripConfig& strip = config_.strips[stripIndex];
    const uint8_t startLevel = fadeInStartLevels_[stripIndex];
    const uint32_t startsAtMs = stripFadeInStartedAtMs_[stripIndex];
    const uint32_t durationMs = stripFadeInDurationsMs_[stripIndex];

    if (elapsedMs < startsAtMs) {
      leds_.setRange(stripStartPixel, strip.pixelCount, startLevel);
      stripStartPixel += strip.pixelCount;
      continue;
    }

    const uint32_t stripElapsedMs = elapsedMs - startsAtMs;
    const uint32_t pixelFadeDurationMs = min(
        durationMs,
        scaledDuration(waterfallPixelFadeMs(), FULL_LEVEL - startLevel));
    const uint32_t travelDurationMs = durationMs - pixelFadeDurationMs;
    const bool reverseLogicalOrder = strip.reversed != bottomToTop;

    for (uint16_t order = 0; order < strip.pixelCount; ++order) {
      const uint32_t pixelStartsAtMs =
          strip.pixelCount <= 1
              ? 0
              : (static_cast<uint64_t>(travelDurationMs) * order) /
                    (strip.pixelCount - 1);
      const uint8_t progress =
          stripElapsedMs < pixelStartsAtMs
              ? 0
              : fadeLevel(stripElapsedMs - pixelStartsAtMs,
                          pixelFadeDurationMs);
      const uint16_t levelDistance = FULL_LEVEL - startLevel;
      const uint8_t level =
          startLevel +
          (levelDistance * progress + FULL_LEVEL / 2) / FULL_LEVEL;
      const uint16_t logicalOffset =
          reverseLogicalOrder ? strip.pixelCount - 1 - order : order;
      leds_.setLogicalPixel(stripStartPixel + logicalOffset, level);
    }
    stripStartPixel += strip.pixelCount;
  }

  leds_.show();
}

void AnimationEngine::renderFadeOut(const uint32_t elapsedMs) {
  updateFadeOutLevels(elapsedMs);
  renderLevels();
}

void AnimationEngine::updateFadeOutLevels(const uint32_t elapsedMs) {
  const uint8_t fadedAmount = fadeLevel(elapsedMs, config_.fadeOutMs);

  for (uint8_t stripIndex = 0; stripIndex < config_.stripCount;
       ++stripIndex) {
    const uint16_t remaining =
        fadeOutStartLevels_[stripIndex] * (FULL_LEVEL - fadedAmount);
    stripLevels_[stripIndex] =
        (remaining + FULL_LEVEL / 2) / FULL_LEVEL;
  }
}

void AnimationEngine::renderLevels() {
  uint16_t startPixel = 0;
  for (uint8_t stripIndex = 0; stripIndex < config_.stripCount;
       ++stripIndex) {
    const StripConfig& strip = config_.strips[stripIndex];
    leds_.setRange(startPixel, strip.pixelCount, stripLevels_[stripIndex]);
    startPixel += strip.pixelCount;
  }
  leds_.show();
}

void AnimationEngine::logNewlyStartedStrips(const uint32_t elapsedMs) {
  for (uint8_t orderPosition = 0; orderPosition < config_.stripCount;
       ++orderPosition) {
    const uint8_t stripIndex = stripIndexAt(orderPosition);
    if (stripStartLogged_[stripIndex] ||
        elapsedMs < stripFadeInStartedAtMs_[stripIndex]) {
      continue;
    }

    stripStartLogged_[stripIndex] = true;
    Serial.print("Strip ");
    Serial.print(stripIndex + 1);
    Serial.print(" fade-in started");
    if (orderPosition > 0) {
      Serial.print(" at ");
      Serial.print(config_.nextStripStartProgress * 100.0F, 0);
      Serial.print("% overlap");
    }
    Serial.println();
  }
}

uint8_t AnimationEngine::stripIndexAt(const uint8_t orderPosition) const {
  if (direction_ == Direction::LEFT_TO_RIGHT) {
    return orderPosition;
  }
  return config_.stripCount - 1 - orderPosition;
}

uint32_t AnimationEngine::waterfallPixelStepMs() const {
  return max(1UL, (1000UL + config_.waterfallSpeedPps - 1) /
                      config_.waterfallSpeedPps);
}

uint32_t AnimationEngine::waterfallPixelFadeMs() const {
  return constrain(waterfallPixelStepMs() * WATERFALL_FADE_WIDTH_PIXELS,
                   MIN_WATERFALL_PIXEL_FADE_MS,
                   MAX_WATERFALL_PIXEL_FADE_MS);
}

uint32_t AnimationEngine::waterfallStripDurationMs(
    const uint16_t pixelCount) const {
  const uint32_t travelDurationMs =
      pixelCount <= 1
          ? 0
          : (1000ULL * (pixelCount - 1) + config_.waterfallSpeedPps - 1) /
                config_.waterfallSpeedPps;
  return travelDurationMs + waterfallPixelFadeMs();
}

uint32_t AnimationEngine::scaledDuration(const uint32_t fullDurationMs,
                                         const uint8_t levelDistance) {
  return (static_cast<uint64_t>(fullDurationMs) * levelDistance +
          FULL_LEVEL - 1) /
         FULL_LEVEL;
}

uint8_t AnimationEngine::fadeLevel(const uint32_t elapsedMs,
                                   const uint32_t durationMs) {
  if (durationMs == 0 || elapsedMs >= durationMs) {
    return FULL_LEVEL;
  }

  const float progress = static_cast<float>(elapsedMs) / durationMs;
  return static_cast<uint8_t>(progress * FULL_LEVEL + 0.5F);
}

const char* AnimationEngine::directionName(const Direction direction) {
  return direction == Direction::LEFT_TO_RIGHT ? "LEFT" : "RIGHT";
}

void AnimationEngine::printCascadeName() const {
  for (uint8_t orderPosition = 0; orderPosition < config_.stripCount;
       ++orderPosition) {
    if (orderPosition > 0) {
      Serial.print("->");
    }
    Serial.print(stripIndexAt(orderPosition) + 1);
  }
}
