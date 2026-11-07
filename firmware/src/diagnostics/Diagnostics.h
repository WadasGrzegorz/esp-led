#pragma once

#include "DiagnosticsTypes.h"

// Bounded RAM only. This recorder is called from the main loop, never an ISR.
// Mutation avoids allocations and copying a full log/pixel buffer per frame.
class Diagnostics {
 public:
  void reset(const DiagnosticSession& session);
  void record(const DiagnosticEvent& event);
  void transition(const DiagnosticTransition& transition);
  void frame(const DiagnosticFrame& frame);
  void invalidWrite(const DiagnosticWriteError& error);
  void operation(const DiagnosticOperation& operation);
  void prepareOutput(const DiagnosticOutput& output);
  void completeOutput();
  void rmtChannel(const LedboxRmtChannel& channel);
  void rmtTimings(const LedboxRmtTimings& timings);
  void rmtPayload(const LedboxRmtPayload& payload);
  void rmtResult(const LedboxRmtResult& result);
  void finish(const DiagnosticFinish& finish);

  const DiagnosticEntry* entry(uint16_t index) const;
  uint16_t count() const;
  uint32_t overwritten() const;
  uint32_t captureId() const;
  uint32_t startedAtMs() const;
  uint8_t stripNumber() const;
  const char* context() const;
  const char* phase() const;
  const LightingConfig& config() const;
  const DiagnosticFrameStats& stats() const;
  const DiagnosticRmtStats& rmtStats() const;
  const LedboxRmtChannel& channel() const;
  const LedboxRmtTimings& timings() const;
  bool channelObserved() const;
  bool timingsObserved() const;
  const uint8_t* mismatchExpected() const;
  const uint8_t* mismatchActual() const;
  uint8_t phaseCount() const;
  uint32_t droppedPhases() const;
  const DiagnosticPhaseStats* phaseStats(uint8_t index) const;
  bool frozen() const;
  uint32_t finishedAtMs() const;
  const char* outcome() const;
  static const char* kindName(DiagnosticKind kind);

 private:
  DiagnosticEntry entries_[DIAGNOSTIC_LOG_CAPACITY] = {};
  uint8_t previousOutput_[MAX_LOGICAL_PIXELS] = {};
  LightingConfig config_ = {};
  DiagnosticFrameStats stats_;
  DiagnosticRmtStats rmtStats_;
  LedboxRmtChannel channel_ = {};
  LedboxRmtTimings timings_ = {};
  bool channelObserved_ = false;
  bool timingsObserved_ = false;
  uint8_t expectedOutput_[MAX_LOGICAL_PIXELS] = {};
  uint8_t submittedOutput_[MAX_LOGICAL_PIXELS] = {};
  uint8_t mismatchExpected_[MAX_LOGICAL_PIXELS] = {};
  uint8_t mismatchActual_[MAX_LOGICAL_PIXELS] = {};
  uint16_t expectedSize_ = 0;
  uint8_t callbacksSeen_ = 0;
  bool outputArmed_ = false;
  DiagnosticPhaseStats phases_[DIAGNOSTIC_PHASE_CAPACITY] = {};
  uint8_t phaseCount_ = 0;
  uint32_t droppedPhases_ = 0;
  bool phaseTracked_ = false;
  bool frozen_ = false;
  uint32_t finishedAtMs_ = 0;
  char outcome_[16] = "recording";
  char context_[24] = "boot";
  char phase_[24] = "OFF";
  char previousPhase_[24] = "";
  uint16_t writeIndex_ = 0;
  uint16_t count_ = 0;
  uint32_t sequence_ = 0;
  uint32_t overwritten_ = 0;
  uint32_t captureId_ = 0;
  uint32_t startedAtMs_ = 0;
  uint8_t stripNumber_ = 0;
  uint32_t sampledAtMs_ = 0;
  uint32_t errorAtMs_ = 0;
  bool previousValid_ = false;
  bool reportedError_ = false;
  DiagnosticPhaseStats* currentPhase();
  void openPhase(const DiagnosticTransition& transition);
};

extern Diagnostics diagnostics;

LightingConfig animationDiagnosticConfig(const LightingConfig& source);
