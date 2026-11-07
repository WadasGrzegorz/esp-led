#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "src/diagnostics/Diagnostics.h"
#include "src/diagnostics/OutputDiagnostic.h"
#include "src/led/AnimationEngine.h"

namespace {
std::array<CRGB, 2> pixels(const uint8_t value) {
  return {CRGB(value, value, value), CRGB(value, value, value)};
}

void ringAndReset() {
  Diagnostics log;
  log.reset({DEFAULT_LIGHTING_CONFIG, "capture", 10});
  assert(log.captureId() == 1 && log.count() == 1);
  for (uint32_t index = 0; index < DIAGNOSTIC_LOG_CAPACITY + 5; ++index) {
    log.record({index, DiagnosticKind::MARK, "marker"});
  }
  assert(log.count() == DIAGNOSTIC_LOG_CAPACITY);
  assert(log.overwritten() == 6);
  assert(log.entry(0)->sequence == 7);
  assert(log.entry(log.count() - 1)->sequence == DIAGNOSTIC_LOG_CAPACITY + 6);
  assert(log.entry(log.count()) == nullptr);
  const auto previous = log.entry(0)->sequence;
  assert(log.entry(1)->sequence == previous + 1);
  char longMessage[512];
  memset(longMessage, 'X', sizeof(longMessage) - 1);
  longMessage[sizeof(longMessage) - 1] = 0;
  log.record({100, DiagnosticKind::WEB, longMessage});
  assert(strlen(log.entry(log.count() - 1)->message) == DIAGNOSTIC_MESSAGE_SIZE - 1);
  log.reset({DEFAULT_LIGHTING_CONFIG, "fresh", 200});
  assert(log.captureId() == 2 && log.count() == 1 && log.overwritten() == 0);
  assert(log.stats().frames == 0 && log.stats().invalidWrites == 0);
  assert(strcmp(log.context(), "fresh") == 0);
}

void frameAnalysis() {
  Diagnostics log;
  log.reset({DEFAULT_LIGHTING_CONFIG, "fade", UINT32_MAX - 50});
  log.transition({"OFF", "ON", "ready", UINT32_MAX - 50});
  const auto full = pixels(200);
  log.frame({full.data(), 2, 80, false, UINT32_MAX - 50, 7100});
  assert(log.stats().estimatedMax == 63 && log.stats().litPixels == 6);
  const auto firstHash = log.stats().hash;
  log.transition({"ON", "FADING_OUT", "hold", UINT32_MAX - 40});
  const auto lower = pixels(100);
  log.frame({lower.data(), 2, 80, false, UINT32_MAX - 40, 7200});
  assert(log.stats().fadeOutIncreaseFrames == 0);
  assert(log.stats().hash != firstHash);
  // A real upward buffer step is flagged even in a frame that is not sampled.
  log.frame({full.data(), 2, 80, false, 10, 7300});
  assert(log.stats().fadeOutIncreaseFrames == 1);
  assert(log.stats().firstIncreasePixel == 0);
  assert(log.stats().firstIncreaseFrom == 31 && log.stats().firstIncreaseTo == 63);
  assert(log.stats().activeFrameGapMaxMs == 51);
  assert(log.stats().txMinUs == 7100 && log.stats().txMaxUs == 7300);
  assert(log.stats().txTotalUs == 21600);
  assert(strcmp(Diagnostics::kindName(log.entry(log.count() - 1)->kind), "ERROR") == 0);
  const auto black = pixels(0);
  log.frame({black.data(), 2, 80, false, 11, 7000});
  assert(log.stats().estimatedMax == 0 && log.stats().litPixels == 0);
  assert(log.stats().zeroFrames == 1);
  log.frame({black.data(), 2, 80, false, 12, 7000});
  assert(log.stats().repeatedFrames == 1);
  log.transition({"FADING_OUT", "FADING_IN", "new motion", 13});
  log.frame({full.data(), 2, 80, false, 13, 7000});
  assert(log.stats().fadeOutIncreaseFrames == 1);  // Legitimate reversal exempted.
  log.frame({full.data(), 2, 80, true, 14, 7000});
  assert(log.stats().ditheredFrames == 1);
  log.frame({full.data(), static_cast<uint16_t>(MAX_LOGICAL_PIXELS / 3 + 1), 80, false, 15, 7000});
  assert(log.stats().invalidFrames == 1);  // Reject BEFORE touching the buffer.
  for (uint16_t index = 0; index < 10; ++index) log.invalidWrite({720, 1, 720});
  assert(log.stats().invalidWrites == 10);
}

void lastPixelAndSampling() {
  Diagnostics log;
  log.reset({DEFAULT_LIGHTING_CONFIG, "last-pixel", 0});
  std::array<CRGB, hardware::WS2811_CONTROLLER_COUNT> data{};
  log.transition({"ON", "FADING_OUT", "hold", 0});
  log.frame({data.data(), hardware::WS2811_CONTROLLER_COUNT, 255, false, 0, 7000});
  const auto before = log.count();
  for (uint32_t at = 1; at < 500; ++at) {
    log.frame({data.data(), hardware::WS2811_CONTROLLER_COUNT, 255, false, at, 7000});
  }
  assert(log.count() == before);  // No per-frame log flood.
  data.back().b = 50;
  log.frame({data.data(), hardware::WS2811_CONTROLLER_COUNT, 255, false, 500, 7000});
  assert(log.stats().fadeOutIncreaseFrames == 1);
  assert(log.stats().litPixels == 1);
  bool found = false;
  for (uint16_t index = 0; index < log.count(); ++index) {
    found = found || strstr(log.entry(index)->message, "firstPixel=719") != nullptr;
  }
  assert(found);
}

void diagnosticPlanAndDriverTiming() {
  const LightingConfig original = {
      LIGHTING_CONFIG_VERSION, 5, 20, 1680, 0.8F, StripStartMode::CASCADE,
      4000, 2640, FadeOutStyle::CASCADE, 2.2F, true,
      WaterfallDirection::BOTTOM_TO_TOP, 40, true, 15.0F,
      {{18, false}, {54, true}, {54, false}, {54, true}, {1, false}}, 2000};
  const auto plan = animationDiagnosticConfig(original);
  assert(validateLightingConfig(plan, MAX_LOGICAL_PIXELS).valid());
  assert(plan.stripCount == original.stripCount);
  for (uint8_t index = 0; index < MAX_STRIPS; ++index) {
    assert(plan.strips[index].pixelCount == original.strips[index].pixelCount);
    assert(plan.strips[index].reversed == original.strips[index].reversed);
  }
  assert(original.maxBrightness == 20 && original.gamma == 2.2F && original.enableWaterfall);
  assert(plan.maxBrightness == 80 && plan.gamma == 1.0F);
  assert(plan.startDelayMs == 0 && !plan.enableWaterfall && !plan.enableLuxGate);
  assert(plan.stripFadeInMs == 3000 && plan.holdMs == 2000 && plan.fadeOutMs == 5000);
  assert(plan.stripStartMode == StripStartMode::SIMULTANEOUS && plan.fadeOutStyle == FadeOutStyle::GLOBAL);

  std::array<CRGB, hardware::WS2811_CONTROLLER_COUNT> data{};
  diagnostics.reset({plan, "one-click-diagnostic", 0});
  LedController leds(data.data(), hardware::WS2811_CONTROLLER_COUNT, MAX_LOGICAL_PIXELS);
  testClockUs = UINT32_MAX - 3000;
  FastLED.transmitUs = 7200;
  leds.begin(plan.maxBrightness, plan.gamma);
  assert(diagnostics.stats().txMinUs == 7200);  // micros rollover is safe.
  AnimationEngine animation(leds, plan);
  animation.begin(0);
  animation.trigger(AnimationEngine::Direction::LEFT_TO_RIGHT, 0);
  for (uint32_t now = 1; now <= 10000; now += 10) {
    testClockMs = now;
    animation.update(now, false);
  }
  testClockMs = 10001;
  animation.update(10001, false);
  assert(animation.isOff());
  assert(diagnostics.stats().fadeOutIncreaseFrames == 0);
  assert(diagnostics.stats().estimatedMax == 0);
  assert(diagnostics.stats().invalidWrites == 0);
  assert(diagnostics.overwritten() == 0);
  assert(configuredPixelCount(diagnostics.config()) == configuredPixelCount(original));
}

void assertBuffer(const std::array<CRGB, hardware::WS2811_CONTROLLER_COUNT>& data,
                  const uint16_t used, const uint8_t raw) {
  for (uint16_t index = 0; index < MAX_LOGICAL_PIXELS; ++index) {
    const auto& controller = data[index / 3];
    const uint8_t value = index % 3 == 0 ? controller.g : index % 3 == 1 ? controller.r : controller.b;
    assert(value == (index < used ? raw : 0));
  }
}

void constantComparison(const LightingConfig& config) {
  std::array<CRGB, hardware::WS2811_CONTROLLER_COUNT> data{};
  LedController leds(data.data(), hardware::WS2811_CONTROLLER_COUNT, MAX_LOGICAL_PIXELS);
  leds.begin(20, 2.2F);
  FastLED.captureFrames = true;
  FastLED.showCount = 0;
  OutputDiagnostic test(leds);
  const uint32_t start = UINT32_MAX - 2000;  // Cross millis wrap during HOLD_HIGH.
  testClockMs = start;
  test.begin({config, start});
  const auto used = configuredPixelCount(config);
  assert(test.stage() == OutputDiagnosticStage::HOLD_HIGH);
  assertBuffer(data, used, 255);
  assert(diagnostics.stats().litPixels == used && diagnostics.stats().estimatedMax == 80);
  const auto holdFrames = FastLED.showCount;
  for (uint32_t elapsed = 10; elapsed < 4000; elapsed += 10) {
    testClockMs = start + elapsed;
    test.update(testClockMs);
  }
  assert(FastLED.showCount == holdFrames);  // Absolutely no retransmit during hold.
  testClockMs = start + 4000;
  test.update(testClockMs);
  assert(test.stage() == OutputDiagnosticStage::REPEAT_HIGH);
  const auto highHash = diagnostics.stats().hash;
  for (uint32_t elapsed = 4010; elapsed < 8000; elapsed += 10) {
    testClockMs = start + elapsed;
    test.update(testClockMs);
    assertBuffer(data, used, 255);
    assert(diagnostics.stats().hash == highHash);
  }
  assert(FastLED.showCount == holdFrames + 400);
  const auto highFastFrames = FastLED.showCount;
  for (uint32_t elapsed = 8000; elapsed < 12000; elapsed += 10) {
    testClockMs = start + elapsed;
    test.update(testClockMs);
    assert(test.stage() == OutputDiagnosticStage::REPEAT_HIGH_50);
    assertBuffer(data, used, 255);
    assert(diagnostics.stats().hash == highHash);
  }
  assert(FastLED.showCount == highFastFrames + 200);
  testClockMs = start + 12000;
  test.update(testClockMs);
  assert(test.stage() == OutputDiagnosticStage::HOLD_LOW);
  assertBuffer(data, used, 26);
  assert(diagnostics.stats().estimatedMax == 8);
  const auto lowFrames = FastLED.showCount;
  const auto lowHash = diagnostics.stats().hash;
  for (uint32_t elapsed = 12010; elapsed < 16000; elapsed += 10) {
    testClockMs = start + elapsed;
    test.update(testClockMs);
  }
  assert(FastLED.showCount == lowFrames);
  for (uint32_t elapsed = 16000; elapsed < 20000; elapsed += 10) {
    testClockMs = start + elapsed;
    test.update(testClockMs);
    assert(test.stage() == OutputDiagnosticStage::REPEAT_LOW);
    assertBuffer(data, used, 26);
    assert(diagnostics.stats().hash == lowHash);
  }
  assert(FastLED.showCount == lowFrames + 400);
  const auto lowFastFrames = FastLED.showCount;
  for (uint32_t elapsed = 20000; elapsed < 24000; elapsed += 10) {
    testClockMs = start + elapsed;
    test.update(testClockMs);
    assert(test.stage() == OutputDiagnosticStage::REPEAT_LOW_50);
    assertBuffer(data, used, 26);
    assert(diagnostics.stats().hash == lowHash);
  }
  assert(FastLED.showCount == lowFastFrames + 200);
  for (uint8_t level = 1; level <= 7; ++level) {
    const uint32_t stageStart = 24000 + (level - 1) * 2000;
    testClockMs = start + stageStart;
    test.update(testClockMs);
    assert(test.stage() == OutputDiagnosticStage::LOW_HOLD);
    const auto raw = static_cast<uint8_t>((level * 256U + 80U) / 81U);
    assertBuffer(data, used, raw);
    assert(diagnostics.stats().estimatedMax == level);
    const auto held = FastLED.showCount;
    testClockMs = start + stageStart + 999;
    test.update(testClockMs);
    assert(FastLED.showCount == held);
    for (uint32_t elapsed = 1000; elapsed < 2000; elapsed += 10) {
      testClockMs = start + stageStart + elapsed;
      test.update(testClockMs);
      assert(test.stage() == OutputDiagnosticStage::LOW_REPEAT);
      assert(diagnostics.stats().estimatedMax == level);
      assertBuffer(data, used, raw);
    }
    assert(FastLED.showCount == held + 100);
  }
  testClockMs = start + 38000;
  test.update(testClockMs);
  assert(test.stage() == OutputDiagnosticStage::FADE);
  assertBuffer(data, 0, 0);
  for (uint32_t elapsed = 38001; elapsed <= 48001; elapsed += 10) {
    testClockMs = start + elapsed;
    test.update(testClockMs);
  }
  assert(!test.isActive());
  assert(strcmp(diagnostics.phase(), "OFF") == 0);
  assertBuffer(data, 0, 0);
  assert(diagnostics.stats().constantFrameChanges == 0);
  assert(diagnostics.stats().fadeOutIncreaseFrames == 0);
  assert(diagnostics.stats().invalidWrites == 0);
  assert(diagnostics.overwritten() == 0);
  assert(diagnostics.stats().activeFrameGapMaxMs <= 20);  // Excludes intentional 4s no-refresh holds.
  FastLED.captureFrames = false;
}

void comparisonAndStopping() {
  const LightingConfig mixed = {
      LIGHTING_CONFIG_VERSION, 5, 20, 1680, 0.6F, StripStartMode::CASCADE,
      3000, 5000, FadeOutStyle::GLOBAL, 2.2F, true,
      WaterfallDirection::TOP_TO_BOTTOM, 53, false, 15.0F,
      {{54, true}, {36, true}, {54, true}, {36, true}, {54, true}}, 0};
  constantComparison(mixed);  // Actual reported geometry: 5 reversed strips, 234 sections.
  assert(mixed.maxBrightness == 20 && mixed.gamma == 2.2F && mixed.enableWaterfall);
  const LightingConfig full = {
      LIGHTING_CONFIG_VERSION, 8, 255, 1000, 0.6F, StripStartMode::CASCADE,
      1000, 1000, FadeOutStyle::GLOBAL, 2.2F, true,
      WaterfallDirection::BOTTOM_TO_TOP, 53, false, 15.0F,
      {{90, true}, {90, false}, {90, true}, {90, false},
       {90, true}, {90, false}, {90, true}, {90, false}}, 0};
  constantComparison(full);  // Includes section 719.

  for (const uint32_t stopAt : {100U, 4100U, 8100U, 12100U, 16100U, 20100U,
                               24100U, 25100U, 30000U, 38100U, 44000U}) {
    std::array<CRGB, hardware::WS2811_CONTROLLER_COUNT> data{};
    LedController leds(data.data(), hardware::WS2811_CONTROLLER_COUNT, MAX_LOGICAL_PIXELS);
    leds.begin(80, 1.0F);
    OutputDiagnostic test(leds);
    testClockMs = 0;
    test.begin({mixed, 0});
    for (uint32_t at = 10; at <= stopAt; at += 10) {
      testClockMs = at;
      test.update(at);
    }
    assert(test.isActive());
    test.stop(stopAt + 1);
    assert(!test.isActive());
    assertBuffer(data, 0, 0);
    const auto frameCount = diagnostics.stats().frames;
    test.update(stopAt + 100000);
    assert(diagnostics.stats().frames == frameCount);
  }
}

void constantChangesAndWorkTiming() {
  Diagnostics log;
  log.reset({DEFAULT_LIGHTING_CONFIG, "constant", 0});
  log.transition({"OFF", "REPEAT_HIGH", "test", 0});
  const auto high = pixels(255);
  const auto lower = pixels(254);
  log.frame({high.data(), 2, 255, false, 0, 7000});
  log.frame({lower.data(), 2, 255, false, 10, 7000});
  assert(log.stats().constantFrameChanges == 1);
  assert(log.stats().fadeOutIncreaseFrames == 0);
  log.transition({"REPEAT_HIGH", "HOLD_LOW", "expected change", 20});
  const auto low = pixels(26);
  log.frame({low.data(), 2, 255, false, 20, 7000});
  assert(log.stats().constantFrameChanges == 1);
  const auto before = log.count();
  log.operation({DiagnosticWork::HTTP, 30, 160000});
  log.operation({DiagnosticWork::SERIAL_CLI, 31, 100});
  log.operation({DiagnosticWork::LUX, 32, 25000});
  assert(log.stats().httpMaxUs == 160000);
  assert(log.stats().serialMaxUs == 100 && log.stats().luxMaxUs == 25000);
  assert(log.count() == before + 1);
  assert(log.entry(log.count() - 1)->kind == DiagnosticKind::TIMING);
  assert(strstr(log.entry(log.count() - 1)->message, "HTTP took 160000 us") != nullptr);
  log.reset({DEFAULT_LIGHTING_CONFIG, "fresh", 40});
  assert(log.stats().httpMaxUs == 0 && log.stats().constantFrameChanges == 0);
}
}  // namespace

int main() {
  ringAndReset();
  frameAnalysis();
  lastPixelAndSampling();
  diagnosticPlanAndDriverTiming();
  comparisonAndStopping();
  constantChangesAndWorkTiming();
  std::puts("Diagnostics: bounded log, frame checks, timing, no-refresh/repeat comparison, stopping and fades passed");
}
