#include "WebConfigServer.h"

#include <ESPmDNS.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_system.h>

#include "../FirmwareInfo.h"
#include "../diagnostics/Diagnostics.h"
#include "../diagnostics/DiagnosticsJson.h"
#include "WebUi.h"

namespace {
FirmwareImageInfo readFirmwareImageInfo() {
  const uint32_t startedUs = micros();
  const uint32_t sketchSizeBytes = ESP.getSketchSize();
  const uint32_t freeSketchSpaceBytes = ESP.getFreeSketchSpace();
  return {sketchSizeBytes, freeSketchSpaceBytes, micros() - startedUs};
}

String setupResultMessage(const SetupResult result) {
  switch (result) {
    case SetupResult::OK:
      return "ok";
    case SetupResult::ALREADY_ACTIVE:
      return "calibration is already active";
    case SetupResult::NOT_ACTIVE:
      return "requested setup action is not active";
    case SetupResult::OUT_OF_RANGE:
      return "pixel or strip is outside the allowed range";
    case SetupResult::TOO_MANY_STRIPS:
      return "maximum strip count reached";
    case SetupResult::NOTHING_TO_UNDO:
      return "there is no boundary to undo";
    case SetupResult::INVALID_CONFIG:
      return "calibration produced an invalid configuration";
  }
  return "unknown setup error";
}

void writeConfig(JsonDocument& document, const LightingConfig& config) {
  document["configVersion"] = config.configVersion;
  document["stripCount"] = config.stripCount;
  document["maxBrightness"] = config.maxBrightness;
  document["stripFadeInMs"] = config.stripFadeInMs;
  document["nextStripStartProgress"] = config.nextStripStartProgress;
  document["stripStartMode"] = stripStartModeName(config.stripStartMode);
  document["startDelayMs"] = config.startDelayMs;
  document["holdMs"] = config.holdMs;
  document["fadeOutMs"] = config.fadeOutMs;
  document["fadeOutStyle"] = fadeOutStyleName(config.fadeOutStyle);
  document["gamma"] = config.gamma;
  document["enableWaterfall"] = config.enableWaterfall;
  document["waterfallDirection"] =
      waterfallDirectionName(config.waterfallDirection);
  document["waterfallSpeedPps"] = config.waterfallSpeedPps;
  document["waterfallTransitionMs"] = config.waterfallTransitionMs;
  document["fadeOutWaterfallDirection"] = waterfallDirectionName(config.fadeOutWaterfallDirection);
  document["reverseFadeOutOrder"] = config.reverseFadeOutOrder;
  document["enableLuxGate"] = config.enableLuxGate;
  document["luxThreshold"] = config.luxThreshold;

  JsonArray strips = document["strips"].to<JsonArray>();
  for (uint8_t index = 0; index < config.stripCount; ++index) {
    JsonObject strip = strips.add<JsonObject>();
    strip["pixelCount"] = config.strips[index].pixelCount;
    strip["reversed"] = config.strips[index].reversed;
  }
}

void sendDocument(WebServer& server, const uint16_t statusCode,
                  JsonDocument& document) {
  String body;
  serializeJson(document, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(statusCode, "application/json", body);
}

bool deadlineReached(const uint32_t nowMs, const uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

const char* resetReasonName(const esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_EXT:
      return "external-reset";
    case ESP_RST_SW:
      return "software-restart";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt-watchdog";
    case ESP_RST_TASK_WDT:
      return "task-watchdog";
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep-sleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    case ESP_RST_USB:
      return "usb";
    case ESP_RST_JTAG:
      return "jtag";
    case ESP_RST_EFUSE:
      return "efuse";
    case ESP_RST_PWR_GLITCH:
      return "power-glitch";
    case ESP_RST_CPU_LOCKUP:
      return "cpu-lockup";
    case ESP_RST_UNKNOWN:
      return "unknown";
  }
  return "unknown";
}
}  // namespace

WebConfigServer::WebConfigServer(
    const WebConfigDependencies& dependencies,
    const WebConfigSettings& settings)
    : configuration_(dependencies.configuration),
      setup_(dependencies.setup),
      operatingMode_(dependencies.operatingMode),
      animation_(dependencies.animation),
      leds_(dependencies.leds),
      leftPir_(dependencies.leftPir),
      rightPir_(dependencies.rightPir),
      lightSensor_(dependencies.lightSensor),
      lightSensorAvailable_(dependencies.lightSensorAvailable),
      hasLuxReading_(dependencies.hasLuxReading),
      currentLux_(dependencies.currentLux),
      settings_(settings),
      outputDiagnostic_(dependencies.leds) {}

void WebConfigServer::ensureWirelessOff() {
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
}

bool WebConfigServer::begin(const uint32_t nowMs) {
  if (running_) {
    return true;
  }
  if (!operatingMode_.isConfigMode() || !credentials_.begin()) {
    return false;
  }

  // Arduino 3.3.9 getSketchSize() verifies the entire flash image. Do this
  // once, before the AP accepts animation/status requests, never per poll.
  if (firmwareImageInfoReads_ == 0) {
    firmwareImageInfo_ = readFirmwareImageInfo();
    ++firmwareImageInfoReads_;
  }

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(credentials_.ssid().c_str(),
                   credentials_.password().c_str(), 1, false, 4, false,
                   WIFI_AUTH_WPA2_PSK, WIFI_CIPHER_TYPE_CCMP)) {
    Serial.println("Web Config: SoftAP startup failed");
    ensureWirelessOff();
    return false;
  }

  if (!routesRegistered_) {
    registerRoutes();
    routesRegistered_ = true;
  }
  server_.begin();
  running_ = true;
  restartScheduled_ = false;
  noteActivity(nowMs);

  mdnsRunning_ = MDNS.begin("ledbox");
  if (mdnsRunning_) {
    MDNS.addService("http", "tcp", 80);
  } else {
    Serial.println("Web Config: mDNS unavailable; use the AP IP address");
  }

  Serial.println("Web Config: WPA2 SoftAP started");
  Serial.print("Web Config SSID: ");
  Serial.println(credentials_.ssid());
  Serial.print("Web Config password (development): ");
  Serial.println(credentials_.password());
  if (mdnsRunning_) {
    Serial.println("Web Config URL: http://ledbox.local");
  }
  Serial.print("Web Config fallback URL: http://");
  Serial.println(WiFi.softAPIP());
  return true;
}

void WebConfigServer::update(const uint32_t nowMs) {
  if (!running_) {
    return;
  }

  server_.handleClient();

  if (stripTestActive_ && deadlineReached(nowMs, stripTestEndsAtMs_)) {
    setup_.stopStripTest();
    stripTestActive_ = false;
    Serial.println("Web Config: strip test stopped automatically");
  }

  if (restartScheduled_ && deadlineReached(nowMs, restartAtMs_)) {
    Serial.println("Web Config: stopping AP/server and restarting NORMAL");
    end();
    operatingMode_.exitConfigMode(ConfigModeExitReason::SAVED);
  }
}

void WebConfigServer::end() {
  if (Update.isRunning()) {
    Update.abort();
  }
  firmwareUploadStarted_ = false;
  firmwareUploadFailed_ = false;
  firmwareUploadBytes_ = 0;
  firmwareUploadError_ = "";

  if (setup_.isCalibrationActive()) {
    setup_.cancelCalibration();
  } else if (setup_.isStripTestActive()) {
    setup_.stopStripTest();
  }
  animationTestActive_ = false;
  stripTestActive_ = false;

  if (running_) {
    if (mdnsRunning_) {
      MDNS.end();
      mdnsRunning_ = false;
    }
    server_.stop();
    WiFi.softAPdisconnect(true);
    Serial.println("Web Config: HTTP server and SoftAP stopped");
  }
  ensureWirelessOff();
  running_ = false;
}

bool WebConfigServer::isRunning() const { return running_; }

bool WebConfigServer::isAnimationTestActive() const {
  return animationTestActive_;
}

void WebConfigServer::updateAnimationTest(const uint32_t nowMs) {
  if (!running_ || !operatingMode_.isConfigMode() || !animationTestActive_) return;
  if (diagnosticActive_) {
    outputDiagnostic_.update(nowMs);
    if (outputDiagnostic_.isActive()) return;
  } else {
    animation_.update(nowMs, false);  // PIR and lux never drive a CONFIG MODE test.
    if (!animation_.isOff()) return;
  }
  animationTestActive_ = false;
  if (!diagnosticActive_) {
    if (diagnostics.stripNumber() != 0) {
      diagnostics.finish({millis(), "completed"});
    }
    return;
  }
  diagnosticActive_ = false;
  diagnosticCompleted_ = true;
  const LightingConfig& working = configuration_.get();
  leds_.configure(min(working.maxBrightness, hardware::CONFIG_TEST_MAX_BRIGHTNESS), working.gamma);
  diagnostics.record({nowMs, DiagnosticKind::WEB,
                      "Diagnostic complete; working brightness/gamma restored; NVS unchanged"});
  diagnostics.finish({millis(), "completed"});
}

bool WebConfigServer::consumeConfigurationChanged() {
  const bool changed = configurationChanged_;
  configurationChanged_ = false;
  return changed;
}

void WebConfigServer::registerRoutes() {
  server_.on("/", HTTP_GET, [this]() {
    if (!requireConfigMode()) {
      return;
    }
    server_.sendHeader("Cache-Control", "no-store");
    server_.send_P(200, "text/html; charset=utf-8", WEB_CONFIG_UI);
  });
  server_.on("/api/session/activity", HTTP_POST, [this]() {
    if (!requireConfigMode()) {
      return;
    }
    noteActivity(millis());
    sendOk("session activity recorded");
  });
  server_.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
  server_.on("/api/diagnostics", HTTP_GET, [this]() { handleDiagnostics(); });
  server_.on("/api/diagnostics/clear", HTTP_POST, [this]() { handleDiagnosticsClear(); });
  server_.on("/api/diagnostics/mark", HTTP_POST, [this]() { handleDiagnosticsMark(); });
  server_.on("/api/diagnostics/run", HTTP_POST, [this]() { handleDiagnosticRun(); });
  server_.on("/api/config", HTTP_GET, [this]() { handleGetConfig(); });
  server_.on("/api/config", HTTP_PUT, [this]() { handlePutConfig(); });
  server_.on("/api/config/export", HTTP_GET,
             [this]() { handleExportConfig(); });
  server_.on("/api/config/import", HTTP_POST,
             [this]() { handleImportConfig(); });
  server_.on("/api/save", HTTP_POST, [this]() { handleSave(); });
  server_.on("/api/reset", HTTP_POST, [this]() { handleReset(); });
  server_.on("/api/strip/test", HTTP_POST, [this]() { handleStripTest(); });
  server_.on("/api/animation/test", HTTP_POST,
             [this]() { handleAnimationTest(); });
  server_.on("/api/test/stop", HTTP_POST, [this]() { handleStopTest(); });
  server_.on("/api/calibration/start", HTTP_POST,
             [this]() { handleCalibrationStart(); });
  server_.on("/api/calibration/move", HTTP_POST,
             [this]() { handleCalibrationMove(); });
  server_.on("/api/calibration/mark", HTTP_POST,
             [this]() { handleCalibrationAction("mark"); });
  server_.on("/api/calibration/undo", HTTP_POST,
             [this]() { handleCalibrationAction("undo"); });
  server_.on("/api/calibration/finish", HTTP_POST,
             [this]() { handleCalibrationAction("finish"); });
  server_.on("/api/calibration/cancel", HTTP_POST,
             [this]() { handleCalibrationAction("cancel"); });
  server_.on("/api/firmware", HTTP_POST,
             [this]() { handleFirmwareUploadComplete(); },
             [this]() { handleFirmwareUpload(); });
  server_.onNotFound([this]() { handleNotFound(); });
}

void WebConfigServer::handleStatus() {
  if (!requireConfigMode()) {
    return;
  }

  const uint32_t nowMs = millis();
  const LightingConfig& config = configuration_.get();
  JsonDocument document;
  document["mode"] = "CONFIG_MODE";
  document["sessionRemainingMs"] =
      operatingMode_.configModeRemainingMs(nowMs);
  document["lightSensorAvailable"] = lightSensorAvailable_;
  document["hasLuxReading"] = hasLuxReading_;
  if (hasLuxReading_) {
    document["currentLux"] = currentLux_;
  } else {
    document["currentLux"] = nullptr;
  }
  document["stripCount"] = config.stripCount;
  document["totalPixels"] = configuredPixelCount(config);
  document["maxStrips"] = MAX_STRIPS;
  document["maxPixels"] = settings_.logicalPixelLimit;
  document["animationTestActive"] = animationTestActive_;
  document["diagnosticActive"] = diagnosticActive_;
  document["stripTestActive"] = setup_.isStripTestActive();
  JsonObject capture = document["diagnostics"].to<JsonObject>();
  capture["captureId"] = diagnostics.captureId();
  capture["frozen"] = diagnostics.frozen();
  capture["phase"] = diagnostics.phase();
  capture["testStripNumber"] = diagnostics.stripNumber();
  capture["frames"] = diagnostics.stats().frames;
  capture["errors"] = diagnostics.stats().fadeOutIncreaseFrames +
                      diagnostics.stats().invalidFrames + diagnostics.stats().invalidWrites +
                      diagnostics.stats().ditheredFrames + diagnostics.stats().constantFrameChanges +
                      diagnostics.rmtStats().mismatchFrames + diagnostics.rmtStats().missingCallbacks +
                      diagnostics.rmtStats().unexpectedCallbacks +
                      diagnostics.rmtStats().bufferChangedDuringTransfer + diagnostics.rmtStats().apiErrors;

  JsonObject device = document["device"].to<JsonObject>();
  device["firmwareVersion"] = LEDBOX_FIRMWARE_VERSION;
  device["buildTimestamp"] = LEDBOX_BUILD_TIMESTAMP;
  device["resetReason"] = resetReasonName(esp_reset_reason());
  device["uptimeMs"] = nowMs;
  device["freeHeapBytes"] = ESP.getFreeHeap();
  device["minimumFreeHeapBytes"] = ESP.getMinFreeHeap();
  device["sketchSizeBytes"] = firmwareImageInfo_.sketchSizeBytes;
  device["freeSketchSpaceBytes"] = firmwareImageInfo_.freeSketchSpaceBytes;
  device["sketchInfoReadUs"] = firmwareImageInfo_.readDurationUs;
  device["sketchInfoReads"] = firmwareImageInfoReads_;
  device["apStations"] = WiFi.softAPgetStationNum();

  JsonObject sensors = document["sensors"].to<JsonObject>();
  sensors["leftPirActive"] = leftPir_.isActive();
  sensors["rightPirActive"] = rightPir_.isActive();
  sensors["bh1750Available"] = lightSensorAvailable_;
  if (lightSensorAvailable_) {
    char address[7] = {};
    snprintf(address, sizeof(address), "0x%02X", lightSensor_.address());
    sensors["bh1750Address"] = address;
  } else {
    sensors["bh1750Address"] = nullptr;
  }

  JsonObject calibration = document["calibration"].to<JsonObject>();
  calibration["active"] = setup_.isCalibrationActive();
  calibration["cursor"] = setup_.cursor();
  calibration["currentStart"] = setup_.currentStripStart();
  JsonArray boundaries = calibration["boundaries"].to<JsonArray>();
  JsonArray lengths = calibration["lengths"].to<JsonArray>();
  uint16_t previousEnd = UINT16_MAX;
  for (uint8_t index = 0; index < setup_.markedBoundaryCount(); ++index) {
    const uint16_t boundary = setup_.markedBoundary(index);
    boundaries.add(boundary);
    const uint16_t start = previousEnd == UINT16_MAX ? 0 : previousEnd + 1;
    lengths.add(boundary - start + 1);
    previousEnd = boundary;
  }
  if (setup_.isCalibrationActive()) {
    lengths.add(setup_.cursor() - setup_.currentStripStart() + 1);
  }
  sendDocument(server_, 200, document);
}

void WebConfigServer::handleDiagnostics() {
  if (!requireConfigMode()) return;
  if (animationTestActive_) {
    sendError(409, "finish or stop the animation before loading the full report");
    return;
  }
  JsonDocument document;
  document["schemaVersion"] = 1;
  document["firmwareVersion"] = LEDBOX_FIRMWARE_VERSION;
  document["firmwareMd5"] = ESP.getSketchMD5();
  document["sketchSizeBytes"] = firmwareImageInfo_.sketchSizeBytes;
  document["freeSketchSpaceBytes"] = firmwareImageInfo_.freeSketchSpaceBytes;
  document["sketchInfoReadUs"] = firmwareImageInfo_.readDurationUs;
  document["sketchInfoReads"] = firmwareImageInfoReads_;
  document["chipModel"] = ESP.getChipModel();
  document["chipRevision"] = ESP.getChipRevision();
  document["cpuFrequencyMHz"] = ESP.getCpuFreqMHz();
  document["idfVersion"] = ESP.getSdkVersion();
  document["apStations"] = WiFi.softAPgetStationNum();
  document["buildTimestamp"] = LEDBOX_BUILD_TIMESTAMP;
  document["uptimeMs"] = millis();
  document["resetReason"] = resetReasonName(esp_reset_reason());
  document["freeHeapBeforeReportBytes"] = ESP.getFreeHeap();
  document["minimumFreeHeapBytes"] = ESP.getMinFreeHeap();
  document["mode"] = "CONFIG_MODE";
  document["animationTestActive"] = animationTestActive_;
  document["diagnosticActive"] = diagnosticActive_;
  document["diagnosticCompleted"] = diagnosticCompleted_;
  document["diagnosticPlan"] = strcmp(diagnostics.context(), "one-click-diagnostic") == 0
      ? "output-comparison-v4: high=80 and low=8 no-refresh/fast/50Hz (4s each); levels 1..7 hold 1s / repeat 1s each; then 3s in / 2s hold / 5s out; gamma=1; no waterfall/PIR; about 48s"
      : "custom/normal: see captureConfig";
  document["calibrationActive"] = setup_.isCalibrationActive();
  document["stripTestActive"] = setup_.isStripTestActive();
  document["leftPirActive"] = leftPir_.isActive();
  document["rightPirActive"] = rightPir_.isActive();
  document["pirIgnoredInConfigMode"] = true;
  document["hasLuxReading"] = hasLuxReading_;
  if (hasLuxReading_) document["currentLux"] = currentLux_;
  document["bh1750Available"] = lightSensorAvailable_;
  document["maxLogicalPixels"] = MAX_LOGICAL_PIXELS;
  document["controllerCount"] = hardware::WS2811_CONTROLLER_COUNT;
  document["dataPin"] = hardware::DATA_PIN;
  document["fastLedVersion"] = FASTLED_VERSION;
  document["outputBackend"] = "RMT5 synchronous; requested priority 3; encoder in IRAM";
  document["wireOrder"] = "GRB; three monochrome sections per CRGB controller";
  document["currentOutputBrightness"] = FastLED.getBrightness();
  document["currentOutputDitherMode"] = FastLED[0].getDither();
  document["captureId"] = diagnostics.captureId();
  document["captureStartedAtMs"] = diagnostics.startedAtMs();
  document["captureFinishedAtMs"] = diagnostics.finishedAtMs();
  document["captureFrozen"] = diagnostics.frozen();
  document["captureOutcome"] = diagnostics.outcome();
  document["context"] = diagnostics.context();
  document["phase"] = diagnostics.phase();
  document["logCapacity"] = DIAGNOSTIC_LOG_CAPACITY;
  document["overwrittenEntries"] = diagnostics.overwritten();
  document["wireMeasured"] = false;
  document["interpretation"] =
      "Software buffer, actual RMT input bytes and task timings only. Estimated output assumes default FastLED correction. "
      "No measurement of DATA at the strip, supply voltage, or actual LED light. "
      "Zero software errors do not prove that wiring or the RMT waveform is correct.";
  document["storage"] = "RAM only; cleared by reboot or a new animation test";
  document["sampleIntervalMs"] = 500;
  document["frameAnalysis"] = "Every frame checked; FRAME log rows sampled";
  JsonDocument captureConfig;
  writeConfig(captureConfig, diagnostics.config());
  document["captureConfig"] = captureConfig.as<JsonVariantConst>();
  JsonDocument workingConfig;
  writeConfig(workingConfig, configuration_.get());
  document["currentWorkingConfig"] = workingConfig.as<JsonVariantConst>();

  const DiagnosticFrameStats& stats = diagnostics.stats();
  JsonObject frames = document["frames"].to<JsonObject>();
  frames["count"] = stats.frames;
  frames["zeroFrames"] = stats.zeroFrames;
  frames["repeatedFrames"] = stats.repeatedFrames;
  frames["fadeOutIncreaseFrames"] = stats.fadeOutIncreaseFrames;
  frames["firstIncreaseAtMs"] = stats.firstIncreaseAtMs;
  frames["firstIncreasePixel"] = stats.firstIncreasePixel;
  frames["firstIncreaseFrom"] = stats.firstIncreaseFrom;
  frames["firstIncreaseTo"] = stats.firstIncreaseTo;
  frames["invalidWrites"] = stats.invalidWrites;
  frames["invalidFrames"] = stats.invalidFrames;
  frames["ditheredFrames"] = stats.ditheredFrames;
  frames["constantFrameChanges"] = stats.constantFrameChanges;
  frames["transmitMinUs"] = stats.frames ? stats.txMinUs : 0;
  frames["transmitMaxUs"] = stats.txMaxUs;
  frames["transmitAverageUs"] = stats.frames ? stats.txTotalUs / stats.frames : 0;
  frames["analysisMaxUs"] = stats.analysisMaxUs;
  frames["analysisAverageUs"] = stats.frames ? stats.analysisTotalUs / stats.frames : 0;
  frames["activeFrameGapMaxMs"] = stats.activeFrameGapMaxMs;
  JsonObject work = document["loopWorkMaxUs"].to<JsonObject>();
  work["http"] = stats.httpMaxUs;
  work["serialCli"] = stats.serialMaxUs;
  work["luxI2c"] = stats.luxMaxUs;
  frames["lastFrameAtMs"] = stats.lastFrameAtMs;
  frames["lastRawMin"] = stats.rawMin;
  frames["lastRawMax"] = stats.rawMax;
  frames["lastEstimatedMin"] = stats.estimatedMin;
  frames["lastEstimatedMax"] = stats.estimatedMax;
  frames["lastEstimatedLitPixels"] = stats.litPixels;
  frames["lastEffectiveBrightness"] = stats.brightness;
  frames["lastEstimatedHash"] = stats.hash;
  frames["hashAlgorithm"] = "FNV-1a 32-bit over estimated output bytes in logical GRB order";
  writeDiagnosticDetails(document, diagnostics);

  JsonArray entries = document["events"].to<JsonArray>();
  for (uint16_t index = 0; index < diagnostics.count(); ++index) {
    const DiagnosticEntry& entry = *diagnostics.entry(index);
    JsonObject item = entries.add<JsonObject>();
    item["sequence"] = entry.sequence;
    item["atMs"] = entry.atMs;
    item["kind"] = Diagnostics::kindName(entry.kind);
    item["message"] = entry.message;
  }
  sendDocument(server_, 200, document);
}

void WebConfigServer::handleDiagnosticsClear() {
  if (!requireConfigMode()) return;
  if (animationTestActive_) {
    sendError(409, "stop the animation before clearing its report");
    return;
  }
  diagnosticCompleted_ = false;
  diagnostics.reset({configuration_.get(), "manual-clear", millis()});
  noteActivity(millis());
  sendOk("diagnostic log cleared in RAM");
}

void WebConfigServer::handleDiagnosticsMark() {
  if (!requireConfigMode()) return;
  diagnostics.record({millis(), DiagnosticKind::MARK,
                      "User observed flicker (timestamp is device request receipt time)"});
  noteActivity(millis());
  sendOk("flicker marker recorded");
}

void WebConfigServer::handleDiagnosticRun() {
  if (!requireConfigMode()) return;
  if (setup_.isCalibrationActive()) {
    sendError(409, "finish or cancel calibration before running diagnostics");
    return;
  }
  const uint32_t nowMs = millis();
  stopVisualActions(nowMs, false);
  const auto plan = animationDiagnosticConfig(configuration_.get());
  const auto validation = configuration_.validate(plan);
  if (!validation.valid()) {
    sendError(422, configValidationErrorName(validation.error));
    return;
  }
  diagnosticCompleted_ = false;
  diagnosticActive_ = true;
  outputDiagnostic_.begin({configuration_.get(), nowMs});
  animationTestActive_ = true;
  noteActivity(nowMs);
  sendOk("diagnostic started: static/repeat, low levels 1..7, then fade; about 48s; NVS unchanged");
}

void WebConfigServer::handleGetConfig() {
  if (!requireConfigMode()) {
    return;
  }
  sendConfig();
}

void WebConfigServer::handleExportConfig() {
  if (!requireConfigMode()) {
    return;
  }

  JsonDocument document;
  writeConfig(document, configuration_.get());
  String body;
  serializeJsonPretty(document, body);
  server_.sendHeader("Cache-Control", "no-store");
  server_.sendHeader("Content-Disposition",
                     "attachment; filename=ledbox-config.json");
  server_.send(200, "application/json", body);
  noteActivity(millis());
}

void WebConfigServer::handleImportConfig() {
  if (!requireConfigMode()) {
    return;
  }
  if (setup_.isCalibrationActive()) {
    sendError(409, "finish or cancel calibration before importing config");
    return;
  }

  JsonDocument document;
  if (!parseRequest(document) || !updateWorkingConfig(document)) {
    return;
  }
  stopVisualActions(millis(), false);
  configurationChanged_ = true;
  noteActivity(millis());
  sendConfig();
}

void WebConfigServer::handlePutConfig() {
  if (!requireConfigMode()) {
    return;
  }
  if (diagnosticActive_) {
    sendError(409, "stop the diagnostic before changing working settings");
    return;
  }
  if (setup_.isCalibrationActive()) {
    sendError(409, "finish or cancel calibration before changing config");
    return;
  }

  JsonDocument document;
  if (!parseRequest(document) || !updateWorkingConfig(document)) {
    return;
  }
  if (animationTestActive_ && diagnostics.stripNumber() != 0) {
    stopVisualActions(millis(), false);
  }
  configurationChanged_ = true;
  noteActivity(millis());
  sendConfig();
}

void WebConfigServer::handleSave() {
  if (!requireConfigMode()) {
    return;
  }
  if (setup_.isCalibrationActive()) {
    sendError(409, "finish or cancel calibration before saving");
    return;
  }
  const ConfigValidationResult validation =
      configuration_.validate(configuration_.get());
  if (!validation.valid()) {
    sendError(422, configValidationErrorName(validation.error));
    return;
  }
  if (!configuration_.save()) {
    sendError(500, "failed to save configuration to NVS");
    return;
  }

  noteActivity(millis());
  sendOk("configuration saved; restarting into NORMAL mode");
  restartScheduled_ = true;
  restartAtMs_ = millis() + settings_.restartDelayMs;
  Serial.println("Web Config: configuration saved to NVS");
}

void WebConfigServer::handleReset() {
  if (!requireConfigMode()) {
    return;
  }
  stopVisualActions(millis(), true);
  const ConfigValidationResult validation =
      configuration_.restoreFactoryDefaults();
  if (!validation.valid()) {
    sendError(500, "factory configuration is invalid");
    return;
  }
  configurationChanged_ = true;
  noteActivity(millis());
  sendConfig();
}

void WebConfigServer::handleStripTest() {
  if (!requireConfigMode()) {
    return;
  }
  if (setup_.isCalibrationActive()) {
    sendError(409, "finish or cancel calibration before testing a strip");
    return;
  }

  JsonDocument document;
  if (!parseRequest(document)) {
    return;
  }
  const JsonObjectConst root = document.as<JsonObjectConst>();
  if (!root["strip"].is<uint32_t>() || !root["active"].is<bool>()) {
    sendError(400, "expected strip number and active flag");
    return;
  }

  const bool active = root["active"].as<bool>();
  const uint32_t stripNumber = root["strip"].as<uint32_t>();
  if (stripNumber == 0 || stripNumber > configuration_.get().stripCount) {
    sendError(422, "strip number is outside the configured geometry");
    return;
  }
  SetupResult result = SetupResult::OK;
  stopVisualActions(millis(), false);
  if (active) {
    result = setup_.testStrip(static_cast<uint8_t>(stripNumber));
    if (result == SetupResult::OK) {
      stripTestActive_ = true;
      stripTestEndsAtMs_ = millis() + settings_.stripTestTimeoutMs;
    }
  } else if (setup_.isStripTestActive()) {
    result = setup_.stopStripTest();
    stripTestActive_ = false;
  }
  if (result != SetupResult::OK) {
    sendSetupError(result);
    return;
  }
  noteActivity(millis());
  sendOk(active ? "strip test started" : "strip test stopped");
}

void WebConfigServer::handleAnimationTest() {
  if (!requireConfigMode()) {
    return;
  }
  if (setup_.isCalibrationActive()) {
    sendError(409, "finish or cancel calibration before testing animation");
    return;
  }

  JsonDocument document;
  if (!parseRequest(document)) {
    return;
  }
  const String direction = document["direction"].as<String>();
  if (direction != "left" && direction != "right") {
    sendError(400, "direction must be left or right");
    return;
  }
  const JsonVariantConst stripValue = document["strip"];
  const bool hasStrip = !stripValue.isUnbound();
  if (hasStrip && !stripValue.is<uint32_t>()) {
    sendError(400, "strip must be an integer strip number");
    return;
  }
  const uint32_t requestedStrip = hasStrip ? stripValue.as<uint32_t>() : 0;
  if (hasStrip && (requestedStrip == 0 || requestedStrip > configuration_.get().stripCount)) {
    sendError(422, "strip number is outside the configured geometry");
    return;
  }
  const uint8_t stripNumber = static_cast<uint8_t>(requestedStrip);

  if (setup_.isStripTestActive()) {
    setup_.stopStripTest();
    stripTestActive_ = false;
  }
  const uint32_t nowMs = millis();
  if (diagnosticActive_) stopVisualActions(nowMs, false);
  diagnosticCompleted_ = false;
  char context[24];
  if (hasStrip) {
    snprintf(context, sizeof(context), "web-strip-%u-%s", stripNumber, direction.c_str());
  } else {
    snprintf(context, sizeof(context), "web-test-%s", direction.c_str());
  }
  diagnostics.reset({configuration_.get(), context, nowMs, stripNumber});
  animation_.begin(nowMs);
  if (hasStrip && !animation_.selectStripForTest(stripNumber)) {
    sendError(500, "cannot select strip for animation test");
    return;
  }
  animation_.trigger(direction == "left"
                         ? AnimationEngine::Direction::LEFT_TO_RIGHT
                         : AnimationEngine::Direction::RIGHT_TO_LEFT,
                     nowMs);
  animationTestActive_ = true;
  noteActivity(nowMs);
  sendOk(hasStrip ? "single-strip animation started; other strips stay off; configured timing preserved"
                  : "animation test started");
}

void WebConfigServer::handleStopTest() {
  if (!requireConfigMode()) {
    return;
  }
  if (setup_.isCalibrationActive()) {
    sendError(409, "cancel or finish calibration instead of stopping a test");
    return;
  }
  diagnostics.record({millis(), DiagnosticKind::WEB, "User stopped test"});
  stopVisualActions(millis(), false);
  if (strcmp(diagnostics.context(), "one-click-diagnostic") == 0 || diagnostics.stripNumber() != 0) {
    diagnostics.finish({millis(), "stopped"});
  }
  noteActivity(millis());
  sendOk("visual test stopped");
}

void WebConfigServer::handleCalibrationStart() {
  if (!requireConfigMode()) {
    return;
  }
  if (setup_.isCalibrationActive()) {
    sendError(409, "calibration is already active");
    return;
  }
  stopVisualActions(millis(), false);
  const SetupResult result = setup_.startCalibration();
  if (result != SetupResult::OK) {
    sendSetupError(result);
    return;
  }
  noteActivity(millis());
  sendOk("calibration started");
}

void WebConfigServer::handleCalibrationMove() {
  if (!requireConfigMode()) {
    return;
  }
  JsonDocument document;
  if (!parseRequest(document)) {
    return;
  }
  const JsonObjectConst root = document.as<JsonObjectConst>();
  if (!root["delta"].is<int>()) {
    sendError(400, "expected integer delta");
    return;
  }
  const int delta = root["delta"].as<int>();
  if (delta < INT16_MIN || delta > INT16_MAX) {
    sendError(400, "delta is outside the supported range");
    return;
  }
  const SetupResult result = setup_.moveCursor(static_cast<int16_t>(delta));
  if (result != SetupResult::OK) {
    sendSetupError(result);
    return;
  }
  noteActivity(millis());
  sendOk("calibration cursor moved");
}

void WebConfigServer::handleCalibrationAction(const char* action) {
  if (!requireConfigMode()) {
    return;
  }

  SetupResult result = SetupResult::NOT_ACTIVE;
  if (strcmp(action, "mark") == 0) {
    result = setup_.markBoundary();
  } else if (strcmp(action, "undo") == 0) {
    result = setup_.undoBoundary();
  } else if (strcmp(action, "finish") == 0) {
    result = setup_.finishCalibration();
  } else if (strcmp(action, "cancel") == 0) {
    result = setup_.cancelCalibration();
  }
  if (result != SetupResult::OK) {
    sendSetupError(result);
    return;
  }

  noteActivity(millis());
  if (strcmp(action, "finish") == 0) {
    configurationChanged_ = true;
    sendConfig();
    return;
  }
  sendOk("calibration action completed");
}

void WebConfigServer::handleFirmwareUpload() {
  HTTPUpload& upload = server_.upload();

  if (upload.status == UPLOAD_FILE_START) {
    firmwareUploadStarted_ = false;
    firmwareUploadFailed_ = false;
    firmwareUploadBytes_ = 0;
    firmwareUploadError_ = "";

    if (!running_ || !operatingMode_.isConfigMode()) {
      failFirmwareUpload("CONFIG MODE is not active");
      return;
    }
    if (upload.filename.isEmpty()) {
      failFirmwareUpload("firmware filename is empty");
      return;
    }

    stopVisualActions(millis(), true);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      failFirmwareUpload(String("cannot start OTA: ") +
                         Update.errorString());
      return;
    }

    firmwareUploadStarted_ = true;
    noteActivity(millis());
    Serial.print("Web Config: OTA upload started: ");
    Serial.println(upload.filename);
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (!firmwareUploadStarted_ || firmwareUploadFailed_) {
      return;
    }
    const size_t written = Update.write(upload.buf, upload.currentSize);
    if (written != upload.currentSize) {
      failFirmwareUpload(String("OTA write failed: ") +
                         Update.errorString());
      return;
    }
    firmwareUploadBytes_ += written;
    noteActivity(millis());
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (!firmwareUploadStarted_ || firmwareUploadFailed_) {
      return;
    }
    if (firmwareUploadBytes_ == 0 || !Update.end(true)) {
      failFirmwareUpload(String("OTA validation failed: ") +
                         Update.errorString());
      return;
    }
    noteActivity(millis());
    Serial.print("Web Config: OTA image accepted, bytes: ");
    Serial.println(firmwareUploadBytes_);
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    failFirmwareUpload("OTA upload aborted by client");
  }
}

void WebConfigServer::handleFirmwareUploadComplete() {
  if (!requireConfigMode()) {
    return;
  }

  if (!firmwareUploadStarted_ || firmwareUploadFailed_) {
    const String message = firmwareUploadError_.isEmpty()
                               ? "no valid firmware image was uploaded"
                               : firmwareUploadError_;
    firmwareUploadStarted_ = false;
    firmwareUploadFailed_ = false;
    firmwareUploadBytes_ = 0;
    firmwareUploadError_ = "";
    sendError(422, message);
    return;
  }

  noteActivity(millis());
  sendOk("firmware accepted; restarting into updated NORMAL mode");
  restartScheduled_ = true;
  restartAtMs_ = millis() + settings_.restartDelayMs;
  Serial.println("Web Config: OTA successful; restart scheduled");
}

void WebConfigServer::failFirmwareUpload(const String& message) {
  if (Update.isRunning()) {
    Update.abort();
  }
  firmwareUploadFailed_ = true;
  firmwareUploadError_ = message;
  Serial.print("Web Config: ");
  Serial.println(message);
}

void WebConfigServer::handleNotFound() {
  sendError(404, "not found");
}

bool WebConfigServer::requireConfigMode() {
  if (running_ && operatingMode_.isConfigMode()) {
    return true;
  }
  sendError(403, "CONFIG MODE is not active");
  return false;
}

bool WebConfigServer::parseRequest(JsonDocument& document) {
  if (!server_.hasArg("plain")) {
    sendError(400, "JSON request body is required");
    return false;
  }
  if (server_.arg("plain").length() > 4096) {
    sendError(413, "JSON request body is too large");
    return false;
  }
  const DeserializationError error =
      deserializeJson(document, server_.arg("plain"));
  if (error) {
    sendError(400, String("invalid JSON: ") + error.c_str());
    return false;
  }
  return true;
}

bool WebConfigServer::updateWorkingConfig(const JsonDocument& document) {
  const JsonObjectConst root = document.as<JsonObjectConst>();
  const JsonArrayConst strips = root["strips"].as<JsonArrayConst>();
  const bool legacyV4 = root["configVersion"].is<uint32_t>() &&
                        root["configVersion"].as<uint32_t>() == 4;
  const bool legacyV5 = root["configVersion"].is<uint32_t>() &&
                        root["configVersion"].as<uint32_t>() == 5;
  const bool legacyWaterfallSettings = legacyV4 || legacyV5;
  const bool missingLegacyDelay = legacyV4 && root["startDelayMs"].isNull();
  if (!root["configVersion"].is<uint32_t>() ||
      !root["stripCount"].is<uint32_t>() ||
      !root["maxBrightness"].is<uint32_t>() ||
      !root["stripFadeInMs"].is<uint32_t>() ||
      !root["nextStripStartProgress"].is<float>() ||
      !root["stripStartMode"].is<const char*>() ||
      (!missingLegacyDelay && !root["startDelayMs"].is<uint32_t>()) ||
      !root["holdMs"].is<uint32_t>() ||
      !root["fadeOutMs"].is<uint32_t>() ||
      !root["fadeOutStyle"].is<const char*>() ||
      !root["gamma"].is<float>() ||
      !root["enableWaterfall"].is<bool>() ||
      !root["waterfallDirection"].is<const char*>() ||
      !root["waterfallSpeedPps"].is<uint32_t>() ||
      !root["enableLuxGate"].is<bool>() ||
      !root["luxThreshold"].is<float>() || strips.isNull()) {
    sendError(400, "configuration contains missing or invalid field types");
    return false;
  }
  if (!legacyWaterfallSettings &&
      (!root["waterfallTransitionMs"].is<uint32_t>() ||
       !root["fadeOutWaterfallDirection"].is<const char*>() ||
       !root["reverseFadeOutOrder"].is<bool>())) {
    sendError(400, "configuration requires waterfallTransitionMs, fadeOutWaterfallDirection and reverseFadeOutOrder");
    return false;
  }

  const uint32_t stripCount = root["stripCount"].as<uint32_t>();
  const uint32_t configVersion = root["configVersion"].as<uint32_t>();
  const uint32_t maxBrightness = root["maxBrightness"].as<uint32_t>();
  const uint32_t waterfallSpeedPps =
      root["waterfallSpeedPps"].as<uint32_t>();
  const String waterfallDirection =
      root["waterfallDirection"].as<String>();
  const String stripStartMode = root["stripStartMode"].as<String>();
  const String fadeOutStyle = root["fadeOutStyle"].as<String>();
  if (stripCount == 0 || stripCount > MAX_STRIPS ||
      strips.size() != stripCount) {
    sendError(422, "strip count must match 1..MAX_STRIPS entries");
    return false;
  }
  if (configVersion != LIGHTING_CONFIG_VERSION && !legacyWaterfallSettings) {
    sendError(422, "unsupported config version");
    return false;
  }
  if (maxBrightness == 0 || maxBrightness > UINT8_MAX) {
    sendError(422, "brightness must be within 1..255");
    return false;
  }
  if (stripStartMode != "cascade" &&
      stripStartMode != "simultaneous") {
    sendError(422, "unsupported strip start mode");
    return false;
  }
  if (fadeOutStyle != "global" && fadeOutStyle != "cascade" && fadeOutStyle != "waterfall") {
    sendError(422, "unsupported fade-out style");
    return false;
  }
  if (waterfallDirection != "top-to-bottom" &&
      waterfallDirection != "bottom-to-top") {
    sendError(422, "unsupported waterfall direction");
    return false;
  }
  const String fadeOutWaterfallDirection = legacyWaterfallSettings
      ? waterfallDirection : root["fadeOutWaterfallDirection"].as<String>();
  if (fadeOutWaterfallDirection != "top-to-bottom" && fadeOutWaterfallDirection != "bottom-to-top") {
    sendError(422, "unsupported fade-out waterfall direction");
    return false;
  }
  if (waterfallSpeedPps < MIN_WATERFALL_SPEED_PPS ||
      waterfallSpeedPps > MAX_WATERFALL_SPEED_PPS) {
    sendError(422, "waterfall speed is outside the supported range");
    return false;
  }

  LightingConfig candidate = {};
  candidate.configVersion = LIGHTING_CONFIG_VERSION;
  candidate.stripCount = static_cast<uint8_t>(stripCount);
  candidate.maxBrightness = static_cast<uint8_t>(maxBrightness);
  candidate.stripFadeInMs = root["stripFadeInMs"].as<uint32_t>();
  candidate.nextStripStartProgress =
      root["nextStripStartProgress"].as<float>();
  candidate.stripStartMode =
      stripStartMode == "simultaneous"
          ? StripStartMode::SIMULTANEOUS
          : StripStartMode::CASCADE;
  candidate.holdMs = root["holdMs"].as<uint32_t>();
  candidate.startDelayMs = missingLegacyDelay
                               ? 0
                               : root["startDelayMs"].as<uint32_t>();
  candidate.fadeOutMs = root["fadeOutMs"].as<uint32_t>();
  candidate.fadeOutStyle = fadeOutStyle == "waterfall" ? FadeOutStyle::WATERFALL
      : fadeOutStyle == "cascade" ? FadeOutStyle::CASCADE : FadeOutStyle::GLOBAL;
  candidate.gamma = root["gamma"].as<float>();
  candidate.enableWaterfall = root["enableWaterfall"].as<bool>();
  candidate.waterfallDirection =
      waterfallDirection == "top-to-bottom"
          ? WaterfallDirection::TOP_TO_BOTTOM
          : WaterfallDirection::BOTTOM_TO_TOP;
  candidate.waterfallSpeedPps =
      static_cast<uint16_t>(waterfallSpeedPps);
  candidate.waterfallTransitionMs = legacyWaterfallSettings ? 0 : root["waterfallTransitionMs"].as<uint32_t>();
  candidate.fadeOutWaterfallDirection = fadeOutWaterfallDirection == "top-to-bottom"
      ? WaterfallDirection::TOP_TO_BOTTOM : WaterfallDirection::BOTTOM_TO_TOP;
  candidate.reverseFadeOutOrder = !legacyWaterfallSettings && root["reverseFadeOutOrder"].as<bool>();
  candidate.enableLuxGate = root["enableLuxGate"].as<bool>();
  candidate.luxThreshold = root["luxThreshold"].as<float>();

  for (uint8_t index = 0; index < candidate.stripCount; ++index) {
    const JsonObjectConst strip = strips[index].as<JsonObjectConst>();
    if (!strip["pixelCount"].is<uint32_t>() ||
        !strip["reversed"].is<bool>()) {
      sendError(400, "each strip requires pixelCount and reversed");
      return false;
    }
    const uint32_t pixelCount = strip["pixelCount"].as<uint32_t>();
    if (pixelCount == 0 || pixelCount > settings_.logicalPixelLimit) {
      sendError(422, String("strip #") + String(index + 1) +
                         " pixelCount must be 1.." +
                         String(settings_.logicalPixelLimit));
      return false;
    }
    candidate.strips[index] = {static_cast<uint16_t>(pixelCount),
                               strip["reversed"].as<bool>()};
  }

  const ConfigValidationResult validation = configuration_.update(candidate);
  if (!validation.valid()) {
    String message = configValidationErrorName(validation.error);
    if (validation.error == ConfigValidationError::EMPTY_STRIP ||
        validation.error == ConfigValidationError::PIXEL_LIMIT) {
      message += " at strip #" + String(validation.stripIndex + 1);
    }
    if (validation.error == ConfigValidationError::PIXEL_LIMIT) {
      message += " (capacity: " + String(settings_.logicalPixelLimit) +
                 " logical pixels)";
    }
    sendError(422, message);
    return false;
  }
  return true;
}

void WebConfigServer::sendConfig(const uint16_t statusCode) {
  JsonDocument document;
  writeConfig(document, configuration_.get());
  sendDocument(server_, statusCode, document);
}

void WebConfigServer::sendOk(const char* message) {
  JsonDocument document;
  document["ok"] = true;
  document["message"] = message;
  sendDocument(server_, 200, document);
}

void WebConfigServer::sendError(const uint16_t statusCode,
                                const String& message) {
  char event[DIAGNOSTIC_MESSAGE_SIZE];
  snprintf(event, sizeof(event), "HTTP %u: %s", statusCode, message.c_str());
  diagnostics.record({millis(), DiagnosticKind::ERROR, event});
  JsonDocument document;
  document["ok"] = false;
  document["error"] = message;
  sendDocument(server_, statusCode, document);
}

void WebConfigServer::sendSetupError(const SetupResult result) {
  const String message = setupResultMessage(result);
  sendError(result == SetupResult::NOT_ACTIVE ? 409 : 422, message);
}

void WebConfigServer::stopVisualActions(const uint32_t nowMs,
                                        const bool includeCalibration) {
  const bool stopStripAnimation = animationTestActive_ && diagnostics.stripNumber() != 0;
  if (diagnosticActive_) {
    outputDiagnostic_.stop(nowMs);
    diagnosticActive_ = false;
    const LightingConfig& working = configuration_.get();
    leds_.configure(min(working.maxBrightness, hardware::CONFIG_TEST_MAX_BRIGHTNESS), working.gamma);
  }
  if (includeCalibration && setup_.isCalibrationActive()) {
    setup_.cancelCalibration();
  } else if (setup_.isStripTestActive()) {
    setup_.stopStripTest();
  }
  stripTestActive_ = false;
  animationTestActive_ = false;
  animation_.begin(nowMs);
  leds_.clear();
  leds_.show();
  if (stopStripAnimation) diagnostics.finish({millis(), "stopped"});
}

void WebConfigServer::noteActivity(const uint32_t nowMs) {
  operatingMode_.noteConfigActivity(nowMs);
}
