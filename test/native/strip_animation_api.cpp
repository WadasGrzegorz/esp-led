#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "src/config/ConfigurationManager.h"
#include "src/diagnostics/DiagnosticsJson.h"
#include "src/diagnostics/OutputDiagnostic.h"
#include "src/setup/SetupController.h"

// Arduino String and network transport are replaced; handlers and their LED,
// animation, setup, configuration and diagnostic dependencies remain real.
using String = std::string;

namespace {
struct Dependencies {
  ConfigurationManager& configuration;
  SetupController& setup;
  AnimationEngine& animation;
  LedController& leds;
};
struct Mode {
  bool active = true;
  bool isConfigMode() const { return active; }
};
struct Settings {
  uint32_t stripTestTimeoutMs = 30000;
  uint16_t logicalPixelLimit = MAX_LOGICAL_PIXELS;
};

class WebConfigServer {
 public:
  explicit WebConfigServer(const Dependencies& dependencies)
      : configuration_(dependencies.configuration), setup_(dependencies.setup),
        animation_(dependencies.animation), leds_(dependencies.leds),
        outputDiagnostic_(dependencies.leds) {}
  ConfigurationManager& configuration_;
  SetupController& setup_;
  AnimationEngine& animation_;
  LedController& leds_;
  OutputDiagnostic outputDiagnostic_;
  Mode operatingMode_;
  Settings settings_;
  bool running_ = true;
  bool animationTestActive_ = false;
  bool diagnosticActive_ = false;
  bool diagnosticCompleted_ = false;
  bool stripTestActive_ = false;
  uint32_t stripTestEndsAtMs_ = 0;
  JsonDocument request;
  int responseStatus = 0;
  bool requireConfigMode() {
    if (running_ && operatingMode_.isConfigMode()) return true;
    sendError(403, "CONFIG MODE is not active");
    return false;
  }
  bool parseRequest(JsonDocument& document) {
    document.set(request.as<JsonVariantConst>());
    return true;
  }
  void sendError(int status, const char*) { responseStatus = status; }
  void sendError(int status, const String&) { responseStatus = status; }
  void sendSetupError(SetupResult) { assert(false); }
  void noteActivity(uint32_t) {}
  void sendOk(const char*) { responseStatus = 200; }
  void handleAnimationTest();
  void handleStripTest();
  void handleDiagnosticRun();
  void handleStopTest();
  void updateAnimationTest(uint32_t nowMs);
  void stopVisualActions(uint32_t nowMs, bool includeCalibration);
  bool updateWorkingConfig(const JsonDocument& document);
};
#include "web_strip_handlers.inc"

std::vector<uint8_t> savedConfig() {
  Preferences preferences;
  preferences.begin("lighting", true);
  std::vector<uint8_t> bytes(preferences.getBytesLength("config"));
  preferences.getBytes("config", bytes.data(), bytes.size());
  preferences.end();
  return bytes;
}

void requestAnimation(WebConfigServer& server, const char* json) {
  server.request.clear();
  assert(!deserializeJson(server.request, json));
  server.handleAnimationTest();
}
}  // namespace

int main() {
  LightingConfig working = DEFAULT_LIGHTING_CONFIG;
  working.maxBrightness = 20;
  working.stripCount = 5;
  working.startDelayMs = 35;
  working.stripFadeInMs = 100;
  working.holdMs = 100;
  working.fadeOutMs = 1000;
  working.enableWaterfall = false;
  for (uint8_t index = 0; index < working.stripCount; ++index) {
    working.strips[index] = {static_cast<uint16_t>(index % 2 ? 36 : 54), true};
  }
  ConfigurationManager configuration(working, MAX_LOGICAL_PIXELS);
  configuration.begin();
  assert(configuration.save());
  const auto before = savedConfig();
  std::array<CRGB, hardware::WS2811_CONTROLLER_COUNT> buffer{};
  LedController leds(buffer.data(), buffer.size(), MAX_LOGICAL_PIXELS);
  leds.begin(working.maxBrightness, working.gamma);
  AnimationEngine animation(leds, configuration.get());
  SetupController setup(leds, configuration, MAX_LOGICAL_PIXELS);
  WebConfigServer server({configuration, setup, animation, leds});
  FastLED.captureFrames = true;

  requestAnimation(server, R"({"direction":"left","strip":5})");
  assert(server.responseStatus == 200 && server.animationTestActive_);
  assert(diagnostics.stripNumber() == 5);
  assert(strcmp(diagnostics.context(), "web-strip-5-left") == 0);
  for (uint32_t at = 0; at < 3000; at += 17) {
    testClockMs = at;
    server.updateAnimationTest(at);
  }
  assert(!server.animationTestActive_ && diagnostics.frozen());
  assert(strcmp(diagnostics.outcome(), "completed") == 0);
  assert(leds.maxBufferLevel() == 0);
  JsonDocument report;
  writeDiagnosticDetails(report, diagnostics);
  assert(report["testStripNumber"].as<int>() == 5);
  assert(diagnostics.config().stripCount == working.stripCount);
  assert(configuredPixelCount(diagnostics.config()) == configuredPixelCount(working));

  // Legacy left/right requests still address the complete chain.
  requestAnimation(server, R"({"direction":"right"})");
  assert(server.responseStatus == 200 && diagnostics.stripNumber() == 0);
  assert(strcmp(diagnostics.context(), "web-test-right") == 0);
  const auto capture = diagnostics.captureId();
  const auto shows = FastLED.showCount;
  for (const char* json : {
           R"({"direction":"left","strip":0})", R"({"direction":"left","strip":6})",
           R"({"direction":"left","strip":256})", R"({"direction":"left","strip":4294967295})"}) {
    requestAnimation(server, json);
    assert(server.responseStatus == 422);
    assert(diagnostics.captureId() == capture && FastLED.showCount == shows);
  }
  for (const char* json : {
           R"({"direction":"left","strip":null})", R"({"direction":"left","strip":-1})",
           R"({"direction":"left","strip":1.5})", R"({"direction":"left","strip":"1"})",
           R"({"direction":"left","strip":true})", R"({"direction":"invalid","strip":1})"}) {
    requestAnimation(server, json);
    assert(server.responseStatus == 400);
    assert(diagnostics.captureId() == capture && FastLED.showCount == shows);
  }
  server.operatingMode_.active = false;
  requestAnimation(server, R"({"direction":"left","strip":1})");
  assert(server.responseStatus == 403 && diagnostics.captureId() == capture);
  server.operatingMode_.active = true;

  requestAnimation(server, R"({"direction":"right","strip":1})");
  assert(server.responseStatus == 200 && diagnostics.stripNumber() == 1);
  server.handleStopTest();
  assert(!server.animationTestActive_ && diagnostics.frozen());
  assert(strcmp(diagnostics.outcome(), "stopped") == 0);
  assert(leds.maxBufferLevel() == 0);

  server.handleDiagnosticRun();
  assert(server.diagnosticActive_ && FastLED.getBrightness() == 80);
  requestAnimation(server, R"({"direction":"left","strip":3})");
  assert(server.responseStatus == 200 && !server.diagnosticActive_);
  assert(server.animationTestActive_ && FastLED.getBrightness() == working.maxBrightness);
  assert(diagnostics.stripNumber() == 3);

  server.request.clear();
  server.request["strip"] = 2;
  server.request["active"] = true;
  server.handleStripTest();
  assert(server.responseStatus == 200 && !server.animationTestActive_);
  assert(setup.isStripTestActive() && diagnostics.frozen());
  assert(strcmp(diagnostics.outcome(), "stopped") == 0);
  server.handleDiagnosticRun();
  server.handleStripTest();
  assert(!server.diagnosticActive_ && !server.animationTestActive_);
  assert(FastLED.getBrightness() == working.maxBrightness);
  assert(!server.outputDiagnostic_.isActive());
  server.stopVisualActions(millis(), false);
  setup.startCalibration();
  const auto calibrationShows = FastLED.showCount;
  requestAnimation(server, R"({"direction":"left","strip":1})");
  assert(server.responseStatus == 409 && FastLED.showCount == calibrationShows);
  assert(savedConfig() == before);
  assert(configuration.get().stripCount == working.stripCount);
  assert(configuration.get().maxBrightness == working.maxBrightness);
  assert(configuration.get().gamma == working.gamma);
  JsonDocument settings;
  writeConfig(settings, working);
  settings["fadeOutStyle"] = "waterfall";
  settings["waterfallTransitionMs"] = 850;
  settings["fadeOutWaterfallDirection"] = "bottom-to-top";
  settings["reverseFadeOutOrder"] = true;
  assert(server.updateWorkingConfig(settings));
  assert(configuration.get().fadeOutStyle == FadeOutStyle::WATERFALL);
  assert(configuration.get().waterfallTransitionMs == 850 && configuration.get().reverseFadeOutOrder);
  assert(configuration.get().fadeOutWaterfallDirection == WaterfallDirection::BOTTOM_TO_TOP);
  for (const uint32_t invalid : {1U, 119U, 3001U, UINT32_MAX}) {
    settings["waterfallTransitionMs"] = invalid;
    assert(!server.updateWorkingConfig(settings) && server.responseStatus == 422);
    assert(configuration.get().waterfallTransitionMs == 850);
  }
  settings["waterfallTransitionMs"] = 600;
  settings["fadeOutWaterfallDirection"] = "invalid";
  assert(!server.updateWorkingConfig(settings) && server.responseStatus == 422);
  settings["fadeOutWaterfallDirection"] = "top-to-bottom";
  settings.remove("reverseFadeOutOrder");
  assert(!server.updateWorkingConfig(settings) && server.responseStatus == 400);
  // Old exports retain their original behavior when imported by v6.
  settings["configVersion"] = 5;
  settings["fadeOutStyle"] = "cascade";
  settings["waterfallDirection"] = "bottom-to-top";
  settings.remove("waterfallTransitionMs");
  settings.remove("fadeOutWaterfallDirection");
  assert(server.updateWorkingConfig(settings));
  assert(configuration.get().waterfallTransitionMs == 0 && !configuration.get().reverseFadeOutOrder);
  assert(configuration.get().fadeOutWaterfallDirection == WaterfallDirection::BOTTOM_TO_TOP);
  settings["configVersion"] = 4;
  settings.remove("startDelayMs");
  assert(server.updateWorkingConfig(settings) && configuration.get().startDelayMs == 0);
  assert(savedConfig() == before);
  std::puts("Strip animation API: selected/global requests, validation, CONFIG guard, completion, stop, diagnostic/static switching, capture metadata and unchanged NVS passed");
}
