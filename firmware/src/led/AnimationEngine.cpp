#include "AnimationEngine.h"

#include <stdio.h>

#include "../diagnostics/Diagnostics.h"

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
  transitionTo(State::OFF, nowMs, "reset/test/config");
  testStripNumber_ = 0;
  resumeWaterfallFadeOut_ = false;
  lastFadeOutElapsedMs_ = 0;
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

bool AnimationEngine::selectStripForTest(const uint8_t stripNumber) {
  if (!isOff() || stripNumber == 0 || stripNumber > config_.stripCount) {
    return false;
  }
  testStripNumber_ = stripNumber;
  return true;
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
    case State::WAITING_TO_START:
      if (elapsedMs >= config_.startDelayMs) {
        startCascade(direction_, nowMs);
      }
      return;
    case State::FADING_IN:
      if (elapsedMs >= cascadeDurationMs_) {
        enterOn(nowMs);
        return;
      }
      renderFadeIn(elapsedMs);
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
      if (elapsedMs >= config_.fadeOutMs) {
        enterOff(nowMs);
        return;
      }
      renderFadeOut(elapsedMs);
      return;
  }
}

void AnimationEngine::trigger(const Direction direction,
                              const uint32_t nowMs) {
  char event[96];
  snprintf(event, sizeof(event), "trigger=%s state=%s", directionName(direction), stateName(state_));
  diagnostics.record({nowMs, DiagnosticKind::MOTION, event});
  switch (state_) {
    case State::OFF:
      if (config_.startDelayMs == 0) {
        startCascade(direction, nowMs);
        return;
      }
      direction_ = direction;
      transitionTo(State::WAITING_TO_START, nowMs, "motion");
      Serial.print("Motion ");
      Serial.print(directionName(direction_));
      Serial.print(" -> animation starts in ");
      Serial.print(config_.startDelayMs);
      Serial.println(" ms");
      return;
    case State::WAITING_TO_START:
      // The first accepted edge owns the deadline and travel direction.
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
      // Resume from the last DISPLAYED levels. Advancing the fade-out here
      // would introduce a downward step just as motion reverses the fade.
      startCascade(direction, nowMs);
      return;
  }
}

bool AnimationEngine::isOff() const { return state_ == State::OFF; }

void AnimationEngine::startCascade(const Direction direction,
                                   const uint32_t nowMs) {
  // A Waterfall fade-out leaves different levels within each strip. Keep its
  // last rendered timeline as a per-pixel baseline when motion reverses it.
  resumeWaterfallFadeOut_ = state_ == State::FADING_OUT &&
                            config_.fadeOutStyle == FadeOutStyle::WATERFALL;
  direction_ = direction;
  transitionTo(State::FADING_IN, nowMs,
               state_ == State::FADING_OUT ? "PIR trigger"
                                           : "motion/start delay completed");
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
    if (config_.stripStartMode == StripStartMode::CASCADE) {
      nextStripStartMs += static_cast<uint32_t>(
          durationMs * config_.nextStripStartProgress + 0.5F);
    }
  }

  Serial.print("Motion ");
  Serial.print(directionName(direction_));
  if (config_.stripStartMode == StripStartMode::SIMULTANEOUS) {
    Serial.print(" -> all strips simultaneously");
  } else {
    Serial.print(" -> cascade ");
    printCascadeName();
  }
  Serial.println();
  logNewlyStartedStrips(0);
}

void AnimationEngine::startFadeOut(const uint32_t nowMs) {
  if (state_ == State::FADING_OUT) {
    Serial.println("Animation error: fade-out restart rejected");
    return;
  }
  for (uint8_t stripIndex = 0; stripIndex < config_.stripCount;
       ++stripIndex) {
    fadeOutStartLevels_[stripIndex] = stripLevels_[stripIndex];
  }
  fadeOutDirection_ = config_.reverseFadeOutOrder
                          ? (direction_ == Direction::LEFT_TO_RIGHT
                                 ? Direction::RIGHT_TO_LEFT : Direction::LEFT_TO_RIGHT)
                          : direction_;
  resumeWaterfallFadeOut_ = false;
  lastFadeOutElapsedMs_ = 0;
  if (config_.fadeOutStyle == FadeOutStyle::WATERFALL) prepareWaterfallFadeOut();

  transitionTo(State::FADING_OUT, nowMs, "hold elapsed; both PIRs LOW/test");
  if (config_.fadeOutStyle == FadeOutStyle::GLOBAL) {
    Serial.println("Global fade-out started");
    return;
  }
  Serial.print(config_.fadeOutStyle == FadeOutStyle::WATERFALL
                   ? "Waterfall fade-out started " : "Cascade fade-out started ");
  for (uint8_t order = 0; order < config_.stripCount; ++order) {
    if (order != 0) Serial.print("->");
    Serial.print(fadeOutStripIndexAt(order) + 1);
  }
  Serial.println();
}

void AnimationEngine::enterOn(const uint32_t nowMs) {
  resumeWaterfallFadeOut_ = false;
  transitionTo(State::ON, nowMs, "fade-in complete");

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
  for (uint8_t stripIndex = 0; stripIndex < config_.stripCount;
       ++stripIndex) {
    stripLevels_[stripIndex] = 0;
  }

  // Blacken the complete chain, including currently unused capacity. The
  // PlatformIO RMT5 backport completes this transfer before show() returns.
  leds_.clear();
  leds_.show();
  testStripNumber_ = 0;
  transitionTo(State::OFF, nowMs, "fade-out complete");
  Serial.print("LED OFF frame: bufferMax=");
  Serial.println(leds_.maxBufferLevel());
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

  if (config_.enableWaterfall || resumeWaterfallFadeOut_) {
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
    if (!isStripVisible(stripIndex)) {
      leds_.setRange(stripStartPixel, strip.pixelCount, 0);
      stripStartPixel += strip.pixelCount;
      continue;
    }
    const uint8_t startLevel = fadeInStartLevels_[stripIndex];
    const uint32_t startsAtMs = stripFadeInStartedAtMs_[stripIndex];
    const uint32_t durationMs = stripFadeInDurationsMs_[stripIndex];

    if (elapsedMs < startsAtMs && !resumeWaterfallFadeOut_) {
      leds_.setRange(stripStartPixel, strip.pixelCount, startLevel);
      stripStartPixel += strip.pixelCount;
      continue;
    }

    const uint32_t stripElapsedMs = elapsedMs < startsAtMs ? 0 : elapsedMs - startsAtMs;
    const uint32_t pixelFadeDurationMs = config_.enableWaterfall
        ? min(durationMs, scaledDuration(waterfallPixelFadeMs(), FULL_LEVEL - startLevel))
        : durationMs;
    const uint32_t travelDurationMs = durationMs - pixelFadeDurationMs;
    const bool reverseLogicalOrder = strip.reversed != bottomToTop;

    for (uint16_t order = 0; order < strip.pixelCount; ++order) {
      const uint32_t pixelStartsAtMs =
          strip.pixelCount <= 1
              ? 0
              : (static_cast<uint64_t>(travelDurationMs) * order) /
                    (strip.pixelCount - 1);
      const uint8_t progress =
          elapsedMs < startsAtMs || stripElapsedMs < pixelStartsAtMs
              ? 0
              : (config_.enableWaterfall
                     ? waterfallFadeLevel(stripElapsedMs - pixelStartsAtMs, pixelFadeDurationMs)
                     : fadeLevel(stripElapsedMs - pixelStartsAtMs, pixelFadeDurationMs));
      const uint16_t logicalOffset =
          reverseLogicalOrder ? strip.pixelCount - 1 - order : order;
      const uint8_t pixelStartLevel = resumeWaterfallFadeOut_
          ? waterfallFadeOutPixelLevel(stripIndex, logicalOffset, lastFadeOutElapsedMs_)
          : startLevel;
      const uint16_t levelDistance = FULL_LEVEL - pixelStartLevel;
      const uint8_t level =
          pixelStartLevel +
          (levelDistance * progress + FULL_LEVEL / 2) / FULL_LEVEL;
      leds_.setLogicalPixel(stripStartPixel + logicalOffset, level);
    }
    stripStartPixel += strip.pixelCount;
  }

  leds_.show();
}

void AnimationEngine::renderFadeOut(const uint32_t elapsedMs) {
  if (config_.fadeOutStyle == FadeOutStyle::WATERFALL) {
    renderWaterfallFadeOut(elapsedMs);
    lastFadeOutElapsedMs_ = elapsedMs;
    return;
  }
  updateFadeOutLevels(elapsedMs);
  renderLevels();
}

void AnimationEngine::prepareWaterfallFadeOut() {
  uint32_t nextStartMs = 0;
  uint32_t naturalDurationMs = 1;
  for (uint8_t order = 0; order < config_.stripCount; ++order) {
    const uint8_t index = fadeOutStripIndexAt(order);
    const uint32_t duration = waterfallStripDurationMs(config_.strips[index].pixelCount);
    stripFadeOutStartedAtMs_[index] = nextStartMs;
    stripFadeOutDurationsMs_[index] = duration;
    naturalDurationMs = max(naturalDurationMs, nextStartMs + duration);
    if (config_.stripStartMode == StripStartMode::CASCADE) {
      nextStartMs += static_cast<uint32_t>(duration * config_.nextStripStartProgress + 0.5F);
    }
  }
  // Preserve the configured total fade-out time while retaining relative
  // strip lengths, overlap and the width of each Waterfall transition.
  for (uint8_t index = 0; index < config_.stripCount; ++index) {
    const uint32_t naturalStripMs = stripFadeOutDurationsMs_[index];
    stripFadeOutStartedAtMs_[index] =
        static_cast<uint64_t>(stripFadeOutStartedAtMs_[index]) * config_.fadeOutMs / naturalDurationMs;
    const uint32_t scaledStripMs =
        static_cast<uint64_t>(naturalStripMs) * config_.fadeOutMs / naturalDurationMs;
    const uint32_t duration = max(uint32_t{1}, scaledStripMs);
    stripFadeOutDurationsMs_[index] = duration;
    const uint32_t scaledPixelMs =
        (static_cast<uint64_t>(waterfallPixelFadeMs()) * duration + naturalStripMs - 1) / naturalStripMs;
    stripFadeOutPixelFadeMs_[index] = min(duration, max(uint32_t{1}, scaledPixelMs));
  }
}

uint8_t AnimationEngine::waterfallFadeOutPixelLevel(const uint8_t stripIndex,
                                                   const uint16_t logicalOffset,
                                                   const uint32_t elapsedMs) const {
  const auto& strip = config_.strips[stripIndex];
  const uint32_t startsAtMs = stripFadeOutStartedAtMs_[stripIndex];
  if (elapsedMs < startsAtMs) return fadeOutStartLevels_[stripIndex];
  const bool bottomToTop = config_.fadeOutWaterfallDirection == WaterfallDirection::BOTTOM_TO_TOP;
  const uint16_t order = strip.reversed != bottomToTop
      ? strip.pixelCount - 1 - logicalOffset : logicalOffset;
  const uint32_t travelMs = stripFadeOutDurationsMs_[stripIndex] - stripFadeOutPixelFadeMs_[stripIndex];
  const uint32_t pixelStartsAtMs = strip.pixelCount <= 1 ? 0
      : static_cast<uint64_t>(travelMs) * order / (strip.pixelCount - 1);
  const uint32_t stripElapsedMs = elapsedMs - startsAtMs;
  const uint8_t progress = stripElapsedMs < pixelStartsAtMs ? 0
      : waterfallFadeLevel(stripElapsedMs - pixelStartsAtMs, stripFadeOutPixelFadeMs_[stripIndex]);
  return (fadeOutStartLevels_[stripIndex] * (FULL_LEVEL - progress) + FULL_LEVEL / 2) / FULL_LEVEL;
}

void AnimationEngine::renderWaterfallFadeOut(const uint32_t elapsedMs) {
  uint16_t startPixel = 0;
  for (uint8_t index = 0; index < config_.stripCount; ++index) {
    const auto& strip = config_.strips[index];
    const bool bottomToTop = config_.fadeOutWaterfallDirection == WaterfallDirection::BOTTOM_TO_TOP;
    const uint16_t firstOffset = strip.reversed != bottomToTop ? strip.pixelCount - 1 : 0;
    // The first section to fade is the strip's minimum displayed level.
    stripLevels_[index] = waterfallFadeOutPixelLevel(index, firstOffset, elapsedMs);
    if (!isStripVisible(index)) {
      leds_.setRange(startPixel, strip.pixelCount, 0);
      startPixel += strip.pixelCount;
      continue;
    }
    for (uint16_t offset = 0; offset < strip.pixelCount; ++offset) {
      leds_.setLogicalPixel(startPixel + offset, waterfallFadeOutPixelLevel(index, offset, elapsedMs));
    }
    startPixel += strip.pixelCount;
  }
  leds_.show();
}

uint8_t AnimationEngine::waterfallFadeLevel(const uint32_t elapsedMs,
                                          const uint32_t durationMs) const {
  if (config_.waterfallTransitionMs == 0) return fadeLevel(elapsedMs, durationMs);
  const uint32_t progress = fadeLevel(elapsedMs, durationMs);
  // Integer smoothstep is monotonic, including at quantization boundaries.
  const uint32_t denominator = FULL_LEVEL * FULL_LEVEL;
  const uint32_t numerator = progress * progress * (3 * FULL_LEVEL - 2 * progress);
  return (numerator + denominator / 2) / denominator;
}

void AnimationEngine::updateFadeOutLevels(const uint32_t elapsedMs) {
  if (config_.fadeOutStyle == FadeOutStyle::GLOBAL) {
    const uint8_t fadedAmount = fadeLevel(elapsedMs, config_.fadeOutMs);
    for (uint8_t stripIndex = 0; stripIndex < config_.stripCount;
         ++stripIndex) {
      const uint16_t remaining =
          fadeOutStartLevels_[stripIndex] * (FULL_LEVEL - fadedAmount);
      stripLevels_[stripIndex] =
          (remaining + FULL_LEVEL / 2) / FULL_LEVEL;
    }
    return;
  }

  const float durationUnits =
      1.0F + (config_.stripCount - 1) * config_.nextStripStartProgress;
  const uint32_t stripDurationMs =
      max(1UL, static_cast<uint32_t>(config_.fadeOutMs / durationUnits));
  const uint32_t stripStartStepMs = max(
      1UL, static_cast<uint32_t>(
               stripDurationMs * config_.nextStripStartProgress + 0.5F));
  for (uint8_t orderPosition = 0; orderPosition < config_.stripCount;
       ++orderPosition) {
    const uint8_t stripIndex = fadeOutStripIndexAt(orderPosition);
    const uint32_t startsAtMs = stripStartStepMs * orderPosition;
    const uint8_t fadedAmount =
        elapsedMs <= startsAtMs
            ? 0
            : fadeLevel(elapsedMs - startsAtMs, stripDurationMs);
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
    const uint8_t level = isStripVisible(stripIndex) ? stripLevels_[stripIndex] : 0;
    leds_.setRange(startPixel, strip.pixelCount, level);
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
    if (orderPosition > 0 &&
        config_.stripStartMode == StripStartMode::CASCADE) {
      Serial.print(" at ");
      Serial.print(config_.nextStripStartProgress * 100.0F, 0);
      Serial.print("% overlap");
    } else if (orderPosition > 0) {
      Serial.print(" simultaneously");
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

bool AnimationEngine::isStripVisible(const uint8_t stripIndex) const {
  // Preserve full geometry, physical offsets and animation timing. Only the
  // displayed output is filtered during an explicitly selected service test.
  return testStripNumber_ == 0 || stripIndex + 1 == testStripNumber_;
}

uint8_t AnimationEngine::fadeOutStripIndexAt(const uint8_t orderPosition) const {
  return fadeOutDirection_ == Direction::LEFT_TO_RIGHT
      ? orderPosition : config_.stripCount - 1 - orderPosition;
}

uint32_t AnimationEngine::waterfallPixelStepMs() const {
  return max(1UL, (1000UL + config_.waterfallSpeedPps - 1) /
                      config_.waterfallSpeedPps);
}

uint32_t AnimationEngine::waterfallPixelFadeMs() const {
  if (config_.waterfallTransitionMs != 0) return config_.waterfallTransitionMs;
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

void AnimationEngine::transitionTo(const State nextState,
                                   const uint32_t nowMs,
                                   const char* reason) {
  diagnostics.transition({stateName(state_), stateName(nextState), reason, nowMs});
  if (state_ != nextState) {
    Serial.print("Animation @");
    Serial.print(nowMs);
    Serial.print(" ms: ");
    Serial.print(stateName(state_));
    Serial.print(" -> ");
    Serial.print(stateName(nextState));
    Serial.print(" (");
    Serial.print(reason);
    Serial.println(")");
  }
  state_ = nextState;
  stateStartedAtMs_ = nowMs;
}

const char* AnimationEngine::stateName(const State state) {
  switch (state) {
    case State::OFF: return "OFF";
    case State::WAITING_TO_START: return "WAITING_TO_START";
    case State::FADING_IN: return "FADING_IN";
    case State::ON: return "ON";
    case State::FADING_OUT: return "FADING_OUT";
  }
  return "UNKNOWN";
}
