#include "AnimationEngine.h"

namespace {
constexpr uint8_t FULL_LEVEL = 255;
}

AnimationEngine::AnimationEngine(LedController& leds,
                                 const LightingConfig& config)
    : leds_(leds), config_(config) {}

void AnimationEngine::begin(const uint32_t nowMs) {
  stateStartedAtMs_ = nowMs;
  leds_.clear();
  leds_.show();
  Serial.println("All 4 strips OFF");
}

void AnimationEngine::update(const uint32_t nowMs) {
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
      Serial.println(cascadeName(direction_));
      return;
    case State::ON:
      stateStartedAtMs_ = nowMs;
      Serial.println("Hold extended by motion");
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
  for (uint8_t orderPosition = 0; orderPosition < hardware::STRIP_COUNT;
       ++orderPosition) {
    const uint8_t stripIndex = stripIndexAt(orderPosition);
    const uint8_t startLevel = stripLevels_[stripIndex];
    const uint32_t durationMs =
        scaledDuration(config_.stripFadeInMs, FULL_LEVEL - startLevel);

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
  Serial.println(cascadeName(direction_));
  logNewlyStartedStrips(0);
}

void AnimationEngine::startFadeOut(const uint32_t nowMs) {
  for (uint8_t stripIndex = 0; stripIndex < hardware::STRIP_COUNT;
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

  for (uint8_t stripIndex = 0; stripIndex < hardware::STRIP_COUNT;
       ++stripIndex) {
    stripLevels_[stripIndex] = FULL_LEVEL;
  }

  renderLevels();
  Serial.println("All 4 strips ON");
}

void AnimationEngine::enterOff(const uint32_t nowMs) {
  state_ = State::OFF;
  stateStartedAtMs_ = nowMs;

  for (uint8_t stripIndex = 0; stripIndex < hardware::STRIP_COUNT;
       ++stripIndex) {
    stripLevels_[stripIndex] = 0;
  }

  renderLevels();
  Serial.println("All 4 strips OFF");
}

void AnimationEngine::renderFadeIn(const uint32_t elapsedMs) {
  logNewlyStartedStrips(elapsedMs);

  for (uint8_t stripIndex = 0; stripIndex < hardware::STRIP_COUNT;
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

  renderLevels();
}

void AnimationEngine::renderFadeOut(const uint32_t elapsedMs) {
  updateFadeOutLevels(elapsedMs);
  renderLevels();
}

void AnimationEngine::updateFadeOutLevels(const uint32_t elapsedMs) {
  const uint8_t fadedAmount = fadeLevel(elapsedMs, config_.fadeOutMs);

  for (uint8_t stripIndex = 0; stripIndex < hardware::STRIP_COUNT;
       ++stripIndex) {
    const uint16_t remaining =
        fadeOutStartLevels_[stripIndex] * (FULL_LEVEL - fadedAmount);
    stripLevels_[stripIndex] =
        (remaining + FULL_LEVEL / 2) / FULL_LEVEL;
  }
}

void AnimationEngine::renderLevels() {
  for (uint8_t stripIndex = 0; stripIndex < hardware::STRIP_COUNT;
       ++stripIndex) {
    leds_.setStrip(config_.strips[stripIndex], stripLevels_[stripIndex]);
  }
  leds_.show();
}

void AnimationEngine::logNewlyStartedStrips(const uint32_t elapsedMs) {
  for (uint8_t orderPosition = 0; orderPosition < hardware::STRIP_COUNT;
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
  return hardware::STRIP_COUNT - 1 - orderPosition;
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

const char* AnimationEngine::cascadeName(const Direction direction) {
  return direction == Direction::LEFT_TO_RIGHT ? "1->2->3->4"
                                                : "4->3->2->1";
}
