#pragma once

#include <Arduino.h>
#include <FastLED.h>

#include "../config/LightingConfig.h"
#include "RmtDiagnosticsHooks.h"

constexpr uint16_t DIAGNOSTIC_LOG_CAPACITY = 128;
constexpr uint16_t DIAGNOSTIC_MESSAGE_SIZE = 144;
constexpr uint8_t DIAGNOSTIC_PHASE_CAPACITY = 32;
constexpr uint8_t DIAGNOSTIC_HISTOGRAM_BINS = 8;

enum class DiagnosticKind : uint8_t { SESSION, STATE, MOTION, PIR, FRAME, ERROR, MARK, WEB, TIMING };

enum class DiagnosticWork : uint8_t { HTTP, SERIAL_CLI, LUX };

struct DiagnosticOperation {
  DiagnosticWork work;
  uint32_t atMs;
  uint32_t durationUs;
};

enum class OutputDiagnosticStage : uint8_t {
  OFF, HOLD_HIGH, REPEAT_HIGH, REPEAT_HIGH_50, HOLD_LOW, REPEAT_LOW, REPEAT_LOW_50,
  LOW_HOLD, LOW_REPEAT, FADE
};

struct DiagnosticOutput {
  const CRGB* pixels;
  uint16_t controllerCount;
  uint8_t brightness;
};

struct DiagnosticFinish {
  uint32_t atMs;
  const char* outcome;
};

struct DiagnosticPhaseStats {
  char name[24] = {};
  uint32_t startedAtMs = 0;
  uint32_t endedAtMs = 0;
  uint32_t frames = 0;
  uint32_t markers = 0;
  uint32_t outputChanges = 0;
  uint32_t fadeOutIncreaseFrames = 0;
  uint32_t txMinUs = UINT32_MAX;
  uint32_t txMaxUs = 0;
  uint64_t txTotalUs = 0;
  uint32_t gapMaxMs = 0;
  uint32_t gapHistogram[DIAGNOSTIC_HISTOGRAM_BINS] = {};
  uint32_t txHistogram[DIAGNOSTIC_HISTOGRAM_BINS] = {};
  uint32_t firstHash = 0;
  uint32_t lastHash = 0;
  uint16_t lastLitPixels = 0;
  uint8_t lastEstimatedMax = 0;
  uint32_t rmtMismatchFrames = 0;
  uint32_t rmtApiErrors = 0;
  uint32_t httpMaxUs = 0;
  uint32_t serialMaxUs = 0;
  uint32_t luxMaxUs = 0;
};

struct DiagnosticRmtStats {
  uint32_t preparedFrames = 0;
  uint32_t submittedFrames = 0;
  uint32_t completedFrames = 0;
  uint32_t missingCallbacks = 0;
  uint32_t unexpectedCallbacks = 0;
  uint32_t mismatchFrames = 0;
  uint32_t bufferChangedDuringTransfer = 0;
  uint32_t apiErrors = 0;
  int lastApiError = 0;
  uint8_t lastErrorOperation = 0;
  uint32_t lastPayloadBytes = 0;
  uint16_t expectedPayloadBytes = 0;
  uint32_t lastSubmittedHash = 0;
  uint32_t lastCompletedHash = 0;
  uint8_t lastPayloadMax = 0;
  uint32_t firstMismatchAtMs = 0;
  uint16_t firstMismatchPixel = 0;
  uint8_t firstMismatchExpected = 0;
  uint8_t firstMismatchActual = 0;
  uint16_t mismatchSnapshotBytes = 0;
  uint16_t mismatchExpectedBytes = 0;
  bool firstMismatchCaptured = false;
  bool firstMismatchAfterWait = false;
  uint32_t prepareMaxUs = 0;
  uint32_t hookMaxUs = 0;
};

struct OutputDiagnosticStart {
  const LightingConfig& config;
  uint32_t atMs;
};

struct DiagnosticEntry {
  uint32_t sequence;
  uint32_t atMs;
  DiagnosticKind kind;
  char message[DIAGNOSTIC_MESSAGE_SIZE];
};

struct DiagnosticEvent {
  uint32_t atMs;
  DiagnosticKind kind;
  const char* message;
};

struct DiagnosticSession {
  const LightingConfig& config;
  const char* context;
  uint32_t atMs;
  uint8_t stripNumber = 0;  // 0 captures the complete configured chain.
};

struct DiagnosticTransition {
  const char* from;
  const char* to;
  const char* reason;
  uint32_t atMs;
};

struct DiagnosticFrame {
  const CRGB* pixels;
  uint16_t controllerCount;
  uint8_t brightness;
  bool dithering;
  uint32_t atMs;
  uint32_t transmitUs;
};

struct DiagnosticWriteError {
  uint16_t start;
  uint16_t count;
  uint16_t capacity;
};

struct DiagnosticFrameStats {
  uint32_t frames = 0;
  uint32_t zeroFrames = 0;
  uint32_t repeatedFrames = 0;
  uint32_t fadeOutIncreaseFrames = 0;
  uint32_t firstIncreaseAtMs = 0;
  uint16_t firstIncreasePixel = 0;
  uint8_t firstIncreaseFrom = 0;
  uint8_t firstIncreaseTo = 0;
  uint32_t invalidWrites = 0;
  uint32_t invalidFrames = 0;
  uint32_t ditheredFrames = 0;
  uint32_t constantFrameChanges = 0;
  uint32_t txMinUs = UINT32_MAX;
  uint32_t txMaxUs = 0;
  uint64_t txTotalUs = 0;
  uint32_t analysisMaxUs = 0;
  uint64_t analysisTotalUs = 0;
  uint32_t activeFrameGapMaxMs = 0;
  uint32_t httpMaxUs = 0;
  uint32_t serialMaxUs = 0;
  uint32_t luxMaxUs = 0;
  uint32_t lastFrameAtMs = 0;
  uint32_t hash = 0;
  uint16_t litPixels = 0;
  uint8_t rawMin = 0;
  uint8_t rawMax = 0;
  uint8_t estimatedMin = 0;
  uint8_t estimatedMax = 0;
  uint8_t brightness = 0;
};
