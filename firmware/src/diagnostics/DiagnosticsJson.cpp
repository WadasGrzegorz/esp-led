#include "DiagnosticsJson.h"

void writeDiagnosticDetails(JsonDocument& document, const Diagnostics& diagnostics) {
  document["testStripNumber"] = diagnostics.stripNumber();
  JsonObject rmt = document["rmt"].to<JsonObject>();
  rmt["measurementScope"] = "Actual bytes at rmt_transmit(), rechecked after wait-done; configured encoder ticks. Not a GPIO waveform measurement; RMT underruns are not independently detected.";
  rmt["channelObserved"] = diagnostics.channelObserved();
  rmt["timingsObserved"] = diagnostics.timingsObserved();
  const auto& channel = diagnostics.channel();
  rmt["pin"] = channel.pin;
  rmt["resolutionHz"] = channel.resolution_hz;
  rmt["memorySymbols"] = channel.memory_symbols;
  rmt["queueDepth"] = channel.queue_depth;
  rmt["configuredInterruptPriority"] = channel.configured_priority;
  rmt["dma"] = static_cast<bool>(channel.dma);
  rmt["inverted"] = static_cast<bool>(channel.inverted);
  const auto& timing = diagnostics.timings();
  rmt["encoderResolutionHz"] = timing.resolution_hz;
  rmt["zeroHighTicks"] = timing.zero_high_ticks;
  rmt["zeroLowTicks"] = timing.zero_low_ticks;
  rmt["oneHighTicks"] = timing.one_high_ticks;
  rmt["oneLowTicks"] = timing.one_low_ticks;
  rmt["resetTicks"] = timing.reset_ticks;
  if (timing.resolution_hz != 0) {
    rmt["configuredZeroPeriodNs"] = (static_cast<uint64_t>(timing.zero_high_ticks) + timing.zero_low_ticks) * 1000000000ULL / timing.resolution_hz;
    rmt["configuredOnePeriodNs"] = (static_cast<uint64_t>(timing.one_high_ticks) + timing.one_low_ticks) * 1000000000ULL / timing.resolution_hz;
  }
  const auto& backend = diagnostics.rmtStats();
  rmt["preparedFrames"] = backend.preparedFrames;
  rmt["submittedFrames"] = backend.submittedFrames;
  rmt["completedFrames"] = backend.completedFrames;
  rmt["missingCallbacks"] = backend.missingCallbacks;
  rmt["unexpectedCallbacks"] = backend.unexpectedCallbacks;
  rmt["mismatchFrames"] = backend.mismatchFrames;
  rmt["bufferChangedDuringTransfer"] = backend.bufferChangedDuringTransfer;
  rmt["apiErrors"] = backend.apiErrors;
  rmt["lastApiError"] = backend.lastApiError;
  rmt["lastErrorOperation"] = backend.lastErrorOperation;
  rmt["errorOperationCodes"] = "1=enable, 2=transmit, 3=wait-done, 4=disable";
  rmt["expectedPayloadBytes"] = backend.expectedPayloadBytes;
  rmt["lastPayloadBytes"] = backend.lastPayloadBytes;
  rmt["lastSubmittedHash"] = backend.lastSubmittedHash;
  rmt["lastCompletedHash"] = backend.lastCompletedHash;
  rmt["lastSubmittedMax"] = backend.lastPayloadMax;
  rmt["prepareMaxUs"] = backend.prepareMaxUs;
  rmt["hookMaxUs"] = backend.hookMaxUs;
  if (backend.firstMismatchCaptured) {
    rmt["firstMismatchAfterWait"] = backend.firstMismatchAfterWait;
    rmt["firstMismatchAtMs"] = backend.firstMismatchAtMs;
    rmt["firstMismatchPixel"] = backend.firstMismatchPixel;
    rmt["firstMismatchExpected"] = backend.firstMismatchExpected;
    rmt["firstMismatchActual"] = backend.firstMismatchActual;
    JsonArray expected = rmt["firstMismatchExpectedBytes"].to<JsonArray>();
    JsonArray actual = rmt["firstMismatchActualBytes"].to<JsonArray>();
    for (uint16_t index = 0; index < backend.mismatchExpectedBytes; ++index) {
      expected.add(diagnostics.mismatchExpected()[index]);
    }
    for (uint16_t index = 0; index < backend.mismatchSnapshotBytes; ++index) {
      actual.add(diagnostics.mismatchActual()[index]);
    }
  }

  document["phaseCapacity"] = DIAGNOSTIC_PHASE_CAPACITY;
  document["droppedPhaseSummaries"] = diagnostics.droppedPhases();
  document["histogramBoundsMs"] = "<8, 8..<12, 12..<20, 20..<40, 40..<80, 80..<160, 160..<320, >=320";
  JsonArray phases = document["phases"].to<JsonArray>();
  for (uint8_t index = 0; index < diagnostics.phaseCount(); ++index) {
    const auto& phase = *diagnostics.phaseStats(index);
    JsonObject item = phases.add<JsonObject>();
    item["phase"] = phase.name;
    item["startedAtMs"] = phase.startedAtMs;
    item["endedAtMs"] = phase.endedAtMs;
    item["frames"] = phase.frames;
    item["markers"] = phase.markers;
    item["outputChanges"] = phase.outputChanges;
    item["fadeOutIncreaseFrames"] = phase.fadeOutIncreaseFrames;
    item["transmitMinUs"] = phase.frames ? phase.txMinUs : 0;
    item["transmitMaxUs"] = phase.txMaxUs;
    item["transmitAverageUs"] = phase.frames ? phase.txTotalUs / phase.frames : 0;
    item["gapMaxMs"] = phase.gapMaxMs;
    item["firstHash"] = phase.firstHash;
    item["lastHash"] = phase.lastHash;
    item["lastLitPixels"] = phase.lastLitPixels;
    item["lastEstimatedMax"] = phase.lastEstimatedMax;
    item["rmtMismatchFrames"] = phase.rmtMismatchFrames;
    item["rmtApiErrors"] = phase.rmtApiErrors;
    item["httpMaxUs"] = phase.httpMaxUs;
    item["serialCliMaxUs"] = phase.serialMaxUs;
    item["luxI2cMaxUs"] = phase.luxMaxUs;
    JsonArray gaps = item["frameGapHistogram"].to<JsonArray>();
    JsonArray durations = item["transmitHistogram"].to<JsonArray>();
    for (uint8_t bin = 0; bin < DIAGNOSTIC_HISTOGRAM_BINS; ++bin) {
      gaps.add(phase.gapHistogram[bin]);
      durations.add(phase.txHistogram[bin]);
    }
  }
  JsonObject coverage = document["coverage"].to<JsonObject>();
  coverage["rmtTelemetryPresent"] = diagnostics.channelObserved() && diagnostics.timingsObserved();
  coverage["allPreparedFramesObserved"] = backend.preparedFrames != 0 && backend.missingCallbacks == 0 &&
      backend.unexpectedCallbacks == 0 && backend.submittedFrames == backend.preparedFrames &&
      backend.completedFrames == backend.preparedFrames;
  coverage["phaseSummariesTruncated"] = diagnostics.droppedPhases() != 0;
  coverage["eventLogTruncated"] = diagnostics.overwritten() != 0;
  coverage["notMeasured"] = "Electrical DATA waveform, receiver decoding, RMT underruns, 24V/5V/3.3V rails, ground quality, actual optical flicker. Capture lives in RAM and is lost on reboot/power loss; downloaded/browser-cached copies survive independently.";
}
