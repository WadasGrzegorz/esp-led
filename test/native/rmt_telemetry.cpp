#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "src/diagnostics/DiagnosticsJson.h"

namespace {
using Pixels = std::array<CRGB, MAX_LOGICAL_PIXELS / 3>;
using Bytes = std::array<uint8_t, MAX_LOGICAL_PIXELS>;

Pixels source() {
  Pixels result;
  result.fill(CRGB(255, 255, 255));
  result.back().b = 128;
  return result;
}

Bytes output() {
  Bytes result;
  result.fill(80);
  result.back() = 40;
  return result;
}

void submit(const Bytes& bytes) {
  const LedboxRmtPayload payload{bytes.data(), bytes.size(), 0};
  ledbox_rmt_payload(&payload);
}

void complete(const Bytes& bytes) {
  const LedboxRmtPayload payload{bytes.data(), bytes.size(), 1};
  ledbox_rmt_payload(&payload);
}

void start(const Pixels& pixels) {
  diagnostics.prepareOutput({pixels.data(), static_cast<uint16_t>(pixels.size()), 80});
}

void payloadAndSnapshots() {
  const auto pixels = source();
  const auto expected = output();
  const LedboxRmtChannel channel{4, 10000000, 48, 4, 3, 0, 0};
  const LedboxRmtTimings timing{10000000, 3, 9, 6, 6, 2800};
  ledbox_rmt_channel(&channel);
  ledbox_rmt_timings(&timing);
  diagnostics.reset({DEFAULT_LIGHTING_CONFIG, "backend", 0});
  assert(diagnostics.channelObserved() && diagnostics.timingsObserved());
  start(pixels);
  submit(expected);
  complete(expected);
  diagnostics.completeOutput();
  assert(diagnostics.rmtStats().preparedFrames == 1);
  assert(diagnostics.rmtStats().submittedFrames == 1 && diagnostics.rmtStats().completedFrames == 1);
  assert(diagnostics.rmtStats().missingCallbacks == 0);
  assert(diagnostics.rmtStats().mismatchFrames == 0);
  assert(diagnostics.rmtStats().lastSubmittedHash == diagnostics.rmtStats().lastCompletedHash);
  assert(diagnostics.rmtStats().lastPayloadBytes == MAX_LOGICAL_PIXELS);
  start(pixels);
  auto broken = expected;
  broken.back() = 255;
  submit(broken);
  complete(broken);
  diagnostics.completeOutput();
  assert(diagnostics.rmtStats().mismatchFrames == 1);
  assert(diagnostics.rmtStats().firstMismatchPixel == MAX_LOGICAL_PIXELS - 1);
  assert(diagnostics.rmtStats().firstMismatchExpected == 40);
  assert(diagnostics.rmtStats().firstMismatchActual == 255);
  assert(!diagnostics.rmtStats().firstMismatchAfterWait);
  for (size_t index = 0; index < expected.size(); ++index) {
    assert(diagnostics.mismatchExpected()[index] == expected[index]);
    assert(diagnostics.mismatchActual()[index] == broken[index]);
  }
  // A later error cannot destroy the first failed frame.
  broken.front() = 100;
  start(pixels);
  submit(broken);
  complete(broken);
  diagnostics.completeOutput();
  assert(diagnostics.rmtStats().mismatchFrames == 2);
  assert(diagnostics.mismatchActual()[0] == 80);

  JsonDocument report;
  writeDiagnosticDetails(report, diagnostics);
  assert(report["rmt"]["channelObserved"].as<bool>());
  assert(report["rmt"]["resetTicks"].as<int>() == 2800);
  assert(report["rmt"]["configuredZeroPeriodNs"].as<int>() == 1200);
  assert(report["rmt"]["configuredOnePeriodNs"].as<int>() == 1200);
  assert(report["rmt"]["configuredInterruptPriority"].as<int>() == 3);
  assert(report["rmt"]["firstMismatchActualBytes"].size() == MAX_LOGICAL_PIXELS);
  assert(report["rmt"]["firstMismatchActualBytes"][719].as<int>() == 255);
  assert(report["coverage"]["allPreparedFramesObserved"].as<bool>());
  assert(strstr(report["coverage"]["notMeasured"].as<const char*>(), "underruns"));
}

void mutationsMissingAndErrors() {
  const auto pixels = source();
  auto bytes = output();
  diagnostics.reset({DEFAULT_LIGHTING_CONFIG, "mutation", 10});
  start(pixels);
  submit(bytes);
  bytes[300] = 79;
  complete(bytes);
  diagnostics.completeOutput();
  assert(diagnostics.rmtStats().bufferChangedDuringTransfer == 1);
  assert(diagnostics.rmtStats().mismatchFrames == 0);
  assert(diagnostics.rmtStats().firstMismatchAfterWait);
  assert(diagnostics.rmtStats().firstMismatchPixel == 300);

  start(pixels);
  const LedboxRmtResult error{2, 258};
  ledbox_rmt_result(&error);
  diagnostics.completeOutput();
  assert(diagnostics.rmtStats().apiErrors == 1);
  assert(diagnostics.rmtStats().lastApiError == 258);
  assert(diagnostics.rmtStats().missingCallbacks == 1);
  start(pixels);
  submit(bytes);
  diagnostics.completeOutput();
  assert(diagnostics.rmtStats().missingCallbacks == 2);
  start(pixels);
  complete(bytes);  // Completion without submission must not look valid.
  diagnostics.completeOutput();
  assert(diagnostics.rmtStats().unexpectedCallbacks == 1);
  JsonDocument report;
  writeDiagnosticDetails(report, diagnostics);
  assert(!report["coverage"]["allPreparedFramesObserved"].as<bool>());
  assert(report["rmt"]["firstMismatchAfterWait"].as<bool>());

  diagnostics.reset({DEFAULT_LIGHTING_CONFIG, "length", 20});
  start(pixels);
  const LedboxRmtPayload truncated{bytes.data(), 10, 0};
  ledbox_rmt_payload(&truncated);
  assert(diagnostics.rmtStats().mismatchFrames == 1);
  assert(diagnostics.rmtStats().mismatchSnapshotBytes == 10);
  assert(diagnostics.rmtStats().mismatchExpectedBytes == MAX_LOGICAL_PIXELS);

  diagnostics.reset({DEFAULT_LIGHTING_CONFIG, "duplicates", 30});
  start(pixels);
  submit(bytes);
  submit(bytes);
  complete(bytes);
  complete(bytes);
  diagnostics.completeOutput();
  assert(diagnostics.rmtStats().unexpectedCallbacks == 2);
  JsonDocument duplicateReport;
  writeDiagnosticDetails(duplicateReport, diagnostics);
  assert(!duplicateReport["coverage"]["allPreparedFramesObserved"].as<bool>());
}

void phasesAndFreeze() {
  diagnostics.reset({DEFAULT_LIGHTING_CONFIG, "phases", 0});
  diagnostics.transition({"OFF", "REPEAT_HIGH", "test", 10});
  const auto pixels = source();
  diagnostics.frame({pixels.data(), static_cast<uint16_t>(pixels.size()), 80, false, 10, 7500});
  diagnostics.frame({pixels.data(), static_cast<uint16_t>(pixels.size()), 80, false, 30, 8500});
  diagnostics.frame({pixels.data(), static_cast<uint16_t>(pixels.size()), 80, false, 197, 16000});
  diagnostics.operation({DiagnosticWork::HTTP, 196, 160000});
  diagnostics.record({197, DiagnosticKind::MARK, "flicker"});
  const auto* phase = diagnostics.phaseStats(1);
  assert(phase && phase->frames == 3 && phase->markers == 1);
  assert(phase->gapHistogram[3] == 1 && phase->gapHistogram[6] == 1);
  assert(phase->txHistogram[0] == 1 && phase->txHistogram[1] == 1 && phase->txHistogram[2] == 1);
  assert(phase->gapMaxMs == 167 && phase->httpMaxUs == 160000);
  diagnostics.transition({"REPEAT_HIGH", "OFF", "done", 200});
  diagnostics.finish({201, "completed"});
  const auto count = diagnostics.count();
  const auto frames = diagnostics.stats().frames;
  for (uint32_t at = 202; at < 2000; ++at) {
    diagnostics.operation({DiagnosticWork::HTTP, at, 200000});
    diagnostics.record({at, DiagnosticKind::PIR, "idle sensor change"});
    diagnostics.frame({pixels.data(), static_cast<uint16_t>(pixels.size()), 80, false, at, 7500});
  }
  assert(diagnostics.count() == count && diagnostics.stats().frames == frames);
  assert(diagnostics.frozen() && diagnostics.finishedAtMs() == 201);
  assert(strcmp(diagnostics.outcome(), "completed") == 0);
  diagnostics.reset({DEFAULT_LIGHTING_CONFIG, "new", 3000});
  assert(!diagnostics.frozen() && diagnostics.phaseCount() == 1);
  for (uint8_t index = 0; index < DIAGNOSTIC_PHASE_CAPACITY + 4; ++index) {
    diagnostics.transition({"", index % 2 ? "ON" : "OFF", "cycling", 3001U + index});
  }
  assert(diagnostics.phaseCount() == DIAGNOSTIC_PHASE_CAPACITY);
  assert(diagnostics.droppedPhases() > 0);
  assert(diagnostics.phaseStats(DIAGNOSTIC_PHASE_CAPACITY) == nullptr);
}
}

int main() {
  payloadAndSnapshots();
  mutationsMissingAndErrors();
  phasesAndFreeze();
  std::puts("RMT telemetry: payload comparison, mutation, missing callbacks, error/JSON capture, phase histograms and freeze passed");
}
