#include "Diagnostics.h"

#include <stdio.h>
#include <string.h>
#include <utility>

namespace {
uint8_t histogramBin(const uint32_t value) {
  constexpr uint32_t limits[] = {8, 12, 20, 40, 80, 160, 320};
  for (uint8_t index = 0; index < DIAGNOSTIC_HISTOGRAM_BINS - 1; ++index) {
    if (value < limits[index]) return index;
  }
  return DIAGNOSTIC_HISTOGRAM_BINS - 1;
}
template <size_t... Index>
LightingConfig diagnosticConfig(const LightingConfig& source,
                                 std::index_sequence<Index...>) {
  return {source.configVersion, source.stripCount, 80, 3000,
          source.nextStripStartProgress, StripStartMode::SIMULTANEOUS,
          2000, 5000, FadeOutStyle::GLOBAL, 1.0F, false,
          source.waterfallDirection, source.waterfallSpeedPps, false,
          source.luxThreshold, {source.strips[Index]...}, 0};
}
}  // namespace

LightingConfig animationDiagnosticConfig(const LightingConfig& source) {
  return diagnosticConfig(source, std::make_index_sequence<MAX_STRIPS>{});
}

Diagnostics diagnostics;

void Diagnostics::reset(const DiagnosticSession& session) {
  config_ = session.config;
  stripNumber_ = session.stripNumber;
  stats_ = {};
  rmtStats_ = {};
  frozen_ = false;
  finishedAtMs_ = 0;
  snprintf(outcome_, sizeof(outcome_), "recording");
  outputArmed_ = false;
  phaseCount_ = 0;
  droppedPhases_ = 0;
  phaseTracked_ = false;
  snprintf(context_, sizeof(context_), "%s", session.context);
  writeIndex_ = 0;
  count_ = 0;
  sequence_ = 0;
  overwritten_ = 0;
  ++captureId_;
  startedAtMs_ = session.atMs;
  sampledAtMs_ = session.atMs;
  previousValid_ = false;
  reportedError_ = false;
  openPhase({"", phase_, "capture started", session.atMs});
  record({session.atMs, DiagnosticKind::SESSION, context_});
}

void Diagnostics::record(const DiagnosticEvent& event) {
  if (frozen_ && event.kind != DiagnosticKind::MARK) return;
  if (event.kind == DiagnosticKind::MARK && currentPhase()) ++currentPhase()->markers;
  // Fixed-size overwrite ring, with explicit loss accounting in the report.
  DiagnosticEntry& entry = entries_[writeIndex_];
  entry.sequence = ++sequence_;
  entry.atMs = event.atMs;
  entry.kind = event.kind;
  snprintf(entry.message, sizeof(entry.message), "%s", event.message);
  writeIndex_ = (writeIndex_ + 1) % DIAGNOSTIC_LOG_CAPACITY;
  if (count_ < DIAGNOSTIC_LOG_CAPACITY) {
    ++count_;
    return;
  }
  ++overwritten_;
}

void Diagnostics::transition(const DiagnosticTransition& transition) {
  if (frozen_) return;
  if (strcmp(phase_, transition.to) != 0) openPhase(transition);
  snprintf(phase_, sizeof(phase_), "%s", transition.to);
  if (strcmp(transition.from, transition.to) == 0) return;
  char message[DIAGNOSTIC_MESSAGE_SIZE];
  snprintf(message, sizeof(message), "%s -> %s (%s)", transition.from,
           transition.to, transition.reason);
  record({transition.atMs, DiagnosticKind::STATE, message});
}

void Diagnostics::frame(const DiagnosticFrame& frame) {
  if (frozen_) return;
  const uint32_t analysisStartedUs = micros();
  const uint32_t pixels = static_cast<uint32_t>(frame.controllerCount) * 3;
  if (pixels == 0 || pixels > MAX_LOGICAL_PIXELS) {
    ++stats_.invalidFrames;
    if (stats_.invalidFrames == 1) {
      record({frame.atMs, DiagnosticKind::ERROR, "Frame exceeds diagnostic/firmware buffer capacity"});
    }
    return;
  }

  const bool samePhase = strcmp(previousPhase_, phase_) == 0;
  const bool constantPhase = strncmp(phase_, "HOLD_", 5) == 0 ||
                             strncmp(phase_, "REPEAT_", 7) == 0;
  const bool fadingOut = strcmp(phase_, "FADING_OUT") == 0;
  const bool previousMayFade = strcmp(previousPhase_, "ON") == 0 ||
                               strcmp(previousPhase_, "FADING_OUT") == 0;
  const bool compare = previousValid_ && fadingOut && previousMayFade && !frame.dithering;
  uint8_t rawMin = 255;
  uint8_t rawMax = 0;
  uint8_t scaledMin = 255;
  uint8_t scaledMax = 0;
  uint16_t lit = 0;
  uint32_t hash = 2166136261U;
  uint16_t increases = 0;
  uint16_t firstIncrease = 0;
  uint8_t increaseFrom = 0;
  uint8_t increaseTo = 0;
  for (uint16_t controller = 0; controller < frame.controllerCount; ++controller) {
    const CRGB& pixel = frame.pixels[controller];
    const uint8_t bytes[] = {pixel.g, pixel.r, pixel.b};  // Logical GRB section order.
    for (uint8_t channel = 0; channel < 3; ++channel) {
      const uint16_t index = controller * 3 + channel;
      const uint8_t raw = bytes[channel];
      // Estimate only: default FastLED correction + scale8 fixed, no dithering.
      const uint8_t scaled = (raw * (frame.brightness + 1U)) >> 8;
      rawMin = min(rawMin, raw);
      rawMax = max(rawMax, raw);
      scaledMin = min(scaledMin, scaled);
      scaledMax = max(scaledMax, scaled);
      lit += scaled != 0;
      hash = (hash ^ scaled) * 16777619U;
      if (compare && scaled > previousOutput_[index]) {
        if (increases == 0) {
          firstIncrease = index;
          increaseFrom = previousOutput_[index];
          increaseTo = scaled;
        }
        ++increases;
      }
      previousOutput_[index] = scaled;
    }
  }

  const bool zeroChanged = previousValid_ && (scaledMax == 0) != (stats_.estimatedMax == 0);
  if (previousValid_ && samePhase &&
      (fadingOut || strcmp(phase_, "FADING_IN") == 0 || strncmp(phase_, "REPEAT_", 7) == 0)) {
    const uint32_t gap = frame.atMs - stats_.lastFrameAtMs;
    if (gap > stats_.activeFrameGapMaxMs && gap >= 40) {
      char message[DIAGNOSTIC_MESSAGE_SIZE];
      snprintf(message, sizeof(message), "New largest active frame gap: %lu ms in %s",
               static_cast<unsigned long>(gap), phase_);
      record({frame.atMs, DiagnosticKind::TIMING, message});
    }
    stats_.activeFrameGapMaxMs = max(stats_.activeFrameGapMaxMs, gap);
  }
  if (previousValid_ && stats_.hash == hash) ++stats_.repeatedFrames;
  if (previousValid_ && samePhase && constantPhase && stats_.hash != hash) {
    ++stats_.constantFrameChanges;
    if (stats_.constantFrameChanges == 1) {
      record({frame.atMs, DiagnosticKind::ERROR,
              "Software output changed within a constant-frame stage"});
    }
  }
  if (scaledMax == 0) ++stats_.zeroFrames;
  if (increases != 0) {
    if (stats_.fadeOutIncreaseFrames == 0) {
      stats_.firstIncreaseAtMs = frame.atMs;
      stats_.firstIncreasePixel = firstIncrease;
      stats_.firstIncreaseFrom = increaseFrom;
      stats_.firstIncreaseTo = increaseTo;
    }
    ++stats_.fadeOutIncreaseFrames;
  }
  if (frame.dithering) ++stats_.ditheredFrames;
  ++stats_.frames;
  stats_.txMinUs = min(stats_.txMinUs, frame.transmitUs);
  stats_.txMaxUs = max(stats_.txMaxUs, frame.transmitUs);
  stats_.txTotalUs += frame.transmitUs;
  stats_.lastFrameAtMs = frame.atMs;
  stats_.hash = hash;
  stats_.litPixels = lit;
  stats_.rawMin = rawMin;
  stats_.rawMax = rawMax;
  stats_.estimatedMin = scaledMin;
  stats_.estimatedMax = scaledMax;
  stats_.brightness = frame.brightness;
  if (auto* phase = currentPhase()) {
    if (phase->frames == 0) phase->firstHash = hash;
    if (phase->frames != 0 && phase->lastHash != hash) ++phase->outputChanges;
    if (phase->frames != 0 && samePhase) {
      const uint32_t gap = frame.atMs - phase->endedAtMs;
      phase->gapMaxMs = max(phase->gapMaxMs, gap);
      ++phase->gapHistogram[histogramBin(gap)];
    }
    ++phase->frames;
    if (increases != 0) ++phase->fadeOutIncreaseFrames;
    phase->txMinUs = min(phase->txMinUs, frame.transmitUs);
    phase->txMaxUs = max(phase->txMaxUs, frame.transmitUs);
    phase->txTotalUs += frame.transmitUs;
    ++phase->txHistogram[histogramBin(frame.transmitUs / 1000)];
    phase->lastHash = hash;
    phase->lastLitPixels = lit;
    phase->lastEstimatedMax = scaledMax;
    phase->endedAtMs = frame.atMs;
  }

  if ((increases != 0 || frame.dithering) &&
      (!reportedError_ || frame.atMs - errorAtMs_ >= 500)) {
    char message[DIAGNOSTIC_MESSAGE_SIZE];
    snprintf(message, sizeof(message), "phase=%s increases=%u firstPixel=%u %u->%u dither=%u",
             phase_, increases, firstIncrease, increaseFrom, increaseTo, frame.dithering);
    record({frame.atMs, DiagnosticKind::ERROR, message});
    errorAtMs_ = frame.atMs;
    reportedError_ = true;
  }
  if (!previousValid_ || !samePhase || zeroChanged || frame.atMs - sampledAtMs_ >= 500) {
    char message[DIAGNOSTIC_MESSAGE_SIZE];
    snprintf(message, sizeof(message),
             "%s frame=%lu raw=%u..%u est=%u..%u lit=%u txUs=%lu hash=%08lX",
             phase_, static_cast<unsigned long>(stats_.frames), rawMin, rawMax,
             scaledMin, scaledMax, lit, static_cast<unsigned long>(frame.transmitUs),
             static_cast<unsigned long>(hash));
    record({frame.atMs, DiagnosticKind::FRAME, message});
    sampledAtMs_ = frame.atMs;
  }
  snprintf(previousPhase_, sizeof(previousPhase_), "%s", phase_);
  previousValid_ = true;
  const uint32_t analysisUs = micros() - analysisStartedUs;
  stats_.analysisMaxUs = max(stats_.analysisMaxUs, analysisUs);
  stats_.analysisTotalUs += analysisUs;
}

void Diagnostics::invalidWrite(const DiagnosticWriteError& error) {
  if (frozen_) return;
  ++stats_.invalidWrites;
  if (stats_.invalidWrites > 4) return;
  char message[DIAGNOSTIC_MESSAGE_SIZE];
  snprintf(message, sizeof(message), "LED range clipped/rejected: start=%u count=%u capacity=%u",
           error.start, error.count, error.capacity);
  record({millis(), DiagnosticKind::ERROR, message});
}

void Diagnostics::operation(const DiagnosticOperation& operation) {
  if (frozen_) return;
  const char* name = "unknown";
  switch (operation.work) {
    case DiagnosticWork::HTTP:
      stats_.httpMaxUs = max(stats_.httpMaxUs, operation.durationUs);
      if (currentPhase()) currentPhase()->httpMaxUs = max(currentPhase()->httpMaxUs, operation.durationUs);
      name = "HTTP";
      break;
    case DiagnosticWork::SERIAL_CLI:
      stats_.serialMaxUs = max(stats_.serialMaxUs, operation.durationUs);
      if (currentPhase()) currentPhase()->serialMaxUs = max(currentPhase()->serialMaxUs, operation.durationUs);
      name = "Serial CLI";
      break;
    case DiagnosticWork::LUX:
      stats_.luxMaxUs = max(stats_.luxMaxUs, operation.durationUs);
      if (currentPhase()) currentPhase()->luxMaxUs = max(currentPhase()->luxMaxUs, operation.durationUs);
      name = "Lux/I2C";
      break;
  }
  if (operation.durationUs < 40000) return;
  char message[DIAGNOSTIC_MESSAGE_SIZE];
  snprintf(message, sizeof(message), "%s took %lu us (phase=%s)", name,
           static_cast<unsigned long>(operation.durationUs), phase_);
  record({operation.atMs, DiagnosticKind::TIMING, message});
}

void Diagnostics::openPhase(const DiagnosticTransition& transition) {
  if (currentPhase()) currentPhase()->endedAtMs = transition.atMs;
  if (phaseCount_ == DIAGNOSTIC_PHASE_CAPACITY) {
    ++droppedPhases_;
    phaseTracked_ = false;
    return;
  }
  DiagnosticPhaseStats& phase = phases_[phaseCount_++];
  phase = {};
  snprintf(phase.name, sizeof(phase.name), "%s", transition.to);
  phase.startedAtMs = transition.atMs;
  phase.endedAtMs = transition.atMs;
  phaseTracked_ = true;
}

DiagnosticPhaseStats* Diagnostics::currentPhase() {
  return phaseTracked_ && phaseCount_ != 0 ? &phases_[phaseCount_ - 1] : nullptr;
}

void Diagnostics::finish(const DiagnosticFinish& finish) {
  if (frozen_) return;
  snprintf(outcome_, sizeof(outcome_), "%s", finish.outcome);
  finishedAtMs_ = finish.atMs;
  if (currentPhase()) currentPhase()->endedAtMs = finish.atMs;
  record({finish.atMs, DiagnosticKind::SESSION, outcome_});
  outputArmed_ = false;
  frozen_ = true;
}

void Diagnostics::prepareOutput(const DiagnosticOutput& output) {
  if (frozen_) return;
  const uint32_t startedUs = micros();
  const uint32_t size = static_cast<uint32_t>(output.controllerCount) * 3;
  if (size == 0 || size > MAX_LOGICAL_PIXELS) {
    outputArmed_ = false;
    return;
  }
  expectedSize_ = min(size, static_cast<uint32_t>(MAX_LOGICAL_PIXELS));
  rmtStats_.expectedPayloadBytes = expectedSize_;
  for (uint16_t index = 0; index < expectedSize_; ++index) {
    const auto& controller = output.pixels[index / 3];
    const uint8_t raw = index % 3 == 0 ? controller.g : index % 3 == 1 ? controller.r : controller.b;
    expectedOutput_[index] = (raw * (output.brightness + 1U)) >> 8;
  }
  ++rmtStats_.preparedFrames;
  callbacksSeen_ = 0;
  outputArmed_ = true;
  rmtStats_.prepareMaxUs = max(rmtStats_.prepareMaxUs, micros() - startedUs);
}

void Diagnostics::completeOutput() {
  if (!outputArmed_) return;
  if (callbacksSeen_ != 3) {
    ++rmtStats_.missingCallbacks;
    if (rmtStats_.missingCallbacks == 1) {
      record({millis(), DiagnosticKind::ERROR, "RMT submit/completion telemetry missing for LED show"});
    }
  }
  outputArmed_ = false;
}

void Diagnostics::rmtChannel(const LedboxRmtChannel& channel) {
  channel_ = channel;
  channelObserved_ = true;
}

void Diagnostics::rmtTimings(const LedboxRmtTimings& timings) {
  timings_ = timings;
  timingsObserved_ = true;
}

void Diagnostics::rmtPayload(const LedboxRmtPayload& payload) {
  if (!outputArmed_ || frozen_) return;
  const uint32_t startedUs = micros();
  if ((!payload.completed && (callbacksSeen_ & 1)) ||
      (payload.completed && ((callbacksSeen_ & 1) == 0 || (callbacksSeen_ & 2)))) {
    ++rmtStats_.unexpectedCallbacks;
  }
  const bool invalidLength = payload.size != expectedSize_ || payload.size > MAX_LOGICAL_PIXELS;
  const uint16_t size = min(payload.size, static_cast<size_t>(MAX_LOGICAL_PIXELS));
  uint32_t hash = 2166136261U;
  uint8_t maximum = 0;
  uint16_t firstMismatch = 0;
  bool mismatch = invalidLength;
  bool modified = false;
  for (uint16_t index = 0; index < size; ++index) {
    const uint8_t value = payload.bytes[index];
    maximum = max(maximum, value);
    hash = (hash ^ value) * 16777619U;
    if (index < expectedSize_ && value != expectedOutput_[index]) {
      if (!mismatch) firstMismatch = index;
      mismatch = true;
    }
    if (payload.completed && (callbacksSeen_ & 1)) {
      if (value != submittedOutput_[index]) modified = true;
    } else if (!payload.completed) {
      submittedOutput_[index] = value;
    }
  }
  if (payload.completed) {
    ++rmtStats_.completedFrames;
    callbacksSeen_ |= 2;
    rmtStats_.lastCompletedHash = hash;
    if (modified) {
      ++rmtStats_.bufferChangedDuringTransfer;
      if (rmtStats_.bufferChangedDuringTransfer == 1) {
        record({millis(), DiagnosticKind::ERROR, "RMT payload buffer changed before wait-done returned"});
      }
    }
  } else {
    ++rmtStats_.submittedFrames;
    callbacksSeen_ |= 1;
    rmtStats_.lastSubmittedHash = hash;
    rmtStats_.lastPayloadBytes = payload.size;
    rmtStats_.lastPayloadMax = maximum;
  }
  // Count one bad submitted frame; a change after submission has its own counter.
  if (mismatch) {
    if (!rmtStats_.firstMismatchCaptured) {
      rmtStats_.firstMismatchCaptured = true;
      rmtStats_.firstMismatchAfterWait = payload.completed;
      rmtStats_.firstMismatchAtMs = millis();
      rmtStats_.firstMismatchPixel = firstMismatch;
      rmtStats_.firstMismatchExpected = expectedOutput_[firstMismatch];
      rmtStats_.firstMismatchActual = size ? payload.bytes[firstMismatch] : 0;
      rmtStats_.mismatchSnapshotBytes = size;
      rmtStats_.mismatchExpectedBytes = expectedSize_;
      memset(mismatchExpected_, 0, sizeof(mismatchExpected_));
      memset(mismatchActual_, 0, sizeof(mismatchActual_));
      memcpy(mismatchExpected_, expectedOutput_, expectedSize_);
      memcpy(mismatchActual_, payload.bytes, size);
      record({millis(), DiagnosticKind::ERROR, "Actual RMT payload differs from expected software output; snapshot saved"});
    }
    if (!payload.completed) {
      ++rmtStats_.mismatchFrames;
      if (currentPhase()) ++currentPhase()->rmtMismatchFrames;
    }
  }
  rmtStats_.hookMaxUs = max(rmtStats_.hookMaxUs, micros() - startedUs);
}

void Diagnostics::rmtResult(const LedboxRmtResult& result) {
  if (!outputArmed_ || frozen_ || result.code == 0) return;
  ++rmtStats_.apiErrors;
  rmtStats_.lastApiError = result.code;
  rmtStats_.lastErrorOperation = result.operation;
  if (currentPhase()) ++currentPhase()->rmtApiErrors;
  char message[DIAGNOSTIC_MESSAGE_SIZE];
  snprintf(message, sizeof(message), "RMT API error operation=%u code=%d", result.operation, result.code);
  record({millis(), DiagnosticKind::ERROR, message});
}

const DiagnosticEntry* Diagnostics::entry(const uint16_t index) const {
  if (index >= count_) return nullptr;
  const uint16_t start = (writeIndex_ + DIAGNOSTIC_LOG_CAPACITY - count_) % DIAGNOSTIC_LOG_CAPACITY;
  return &entries_[(start + index) % DIAGNOSTIC_LOG_CAPACITY];
}

uint16_t Diagnostics::count() const { return count_; }
uint32_t Diagnostics::overwritten() const { return overwritten_; }
uint32_t Diagnostics::captureId() const { return captureId_; }
uint32_t Diagnostics::startedAtMs() const { return startedAtMs_; }
uint8_t Diagnostics::stripNumber() const { return stripNumber_; }
const char* Diagnostics::context() const { return context_; }
const char* Diagnostics::phase() const { return phase_; }
const LightingConfig& Diagnostics::config() const { return config_; }
const DiagnosticFrameStats& Diagnostics::stats() const { return stats_; }
const DiagnosticRmtStats& Diagnostics::rmtStats() const { return rmtStats_; }
const LedboxRmtChannel& Diagnostics::channel() const { return channel_; }
const LedboxRmtTimings& Diagnostics::timings() const { return timings_; }
bool Diagnostics::channelObserved() const { return channelObserved_; }
bool Diagnostics::timingsObserved() const { return timingsObserved_; }
const uint8_t* Diagnostics::mismatchExpected() const { return mismatchExpected_; }
const uint8_t* Diagnostics::mismatchActual() const { return mismatchActual_; }
uint8_t Diagnostics::phaseCount() const { return phaseCount_; }
uint32_t Diagnostics::droppedPhases() const { return droppedPhases_; }
const DiagnosticPhaseStats* Diagnostics::phaseStats(const uint8_t index) const {
  return index < phaseCount_ ? &phases_[index] : nullptr;
}
bool Diagnostics::frozen() const { return frozen_; }
uint32_t Diagnostics::finishedAtMs() const { return finishedAtMs_; }
const char* Diagnostics::outcome() const { return outcome_; }

const char* Diagnostics::kindName(const DiagnosticKind kind) {
  switch (kind) {
    case DiagnosticKind::SESSION: return "SESSION";
    case DiagnosticKind::STATE: return "STATE";
    case DiagnosticKind::MOTION: return "MOTION";
    case DiagnosticKind::PIR: return "PIR";
    case DiagnosticKind::FRAME: return "FRAME";
    case DiagnosticKind::ERROR: return "ERROR";
    case DiagnosticKind::MARK: return "MARK";
    case DiagnosticKind::WEB: return "WEB";
    case DiagnosticKind::TIMING: return "TIMING";
  }
  return "UNKNOWN";
}
