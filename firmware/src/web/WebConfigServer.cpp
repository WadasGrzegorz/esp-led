#include "WebConfigServer.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "WebUi.h"

namespace {
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
  document["holdMs"] = config.holdMs;
  document["fadeOutMs"] = config.fadeOutMs;
  document["gamma"] = config.gamma;
  document["enableWaterfall"] = config.enableWaterfall;
  document["waterfallDirection"] =
      waterfallDirectionName(config.waterfallDirection);
  document["waterfallSpeedPps"] = config.waterfallSpeedPps;
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
}  // namespace

WebConfigServer::WebConfigServer(
    const WebConfigDependencies& dependencies,
    const WebConfigSettings& settings)
    : configuration_(dependencies.configuration),
      setup_(dependencies.setup),
      operatingMode_(dependencies.operatingMode),
      animation_(dependencies.animation),
      leds_(dependencies.leds),
      lightSensorAvailable_(dependencies.lightSensorAvailable),
      hasLuxReading_(dependencies.hasLuxReading),
      currentLux_(dependencies.currentLux),
      settings_(settings) {}

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

  if (animationTestActive_ && animation_.isOff()) {
    animationTestActive_ = false;
  }

  if (restartScheduled_ && deadlineReached(nowMs, restartAtMs_)) {
    Serial.println("Web Config: stopping AP/server and restarting NORMAL");
    end();
    operatingMode_.exitConfigMode(ConfigModeExitReason::SAVED);
  }
}

void WebConfigServer::end() {
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
  server_.on("/api/config", HTTP_GET, [this]() { handleGetConfig(); });
  server_.on("/api/config", HTTP_PUT, [this]() { handlePutConfig(); });
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
  document["stripTestActive"] = setup_.isStripTestActive();

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

void WebConfigServer::handleGetConfig() {
  if (!requireConfigMode()) {
    return;
  }
  sendConfig();
}

void WebConfigServer::handlePutConfig() {
  if (!requireConfigMode()) {
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
  animation_.begin(millis());
  animationTestActive_ = false;
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

  if (setup_.isStripTestActive()) {
    setup_.stopStripTest();
    stripTestActive_ = false;
  }
  const uint32_t nowMs = millis();
  animation_.begin(nowMs);
  animation_.trigger(direction == "left"
                         ? AnimationEngine::Direction::LEFT_TO_RIGHT
                         : AnimationEngine::Direction::RIGHT_TO_LEFT,
                     nowMs);
  animationTestActive_ = true;
  noteActivity(nowMs);
  sendOk("animation test started");
}

void WebConfigServer::handleStopTest() {
  if (!requireConfigMode()) {
    return;
  }
  if (setup_.isCalibrationActive()) {
    sendError(409, "cancel or finish calibration instead of stopping a test");
    return;
  }
  stopVisualActions(millis(), false);
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
  if (!root["configVersion"].is<uint32_t>() ||
      !root["stripCount"].is<uint32_t>() ||
      !root["maxBrightness"].is<uint32_t>() ||
      !root["stripFadeInMs"].is<uint32_t>() ||
      !root["nextStripStartProgress"].is<float>() ||
      !root["holdMs"].is<uint32_t>() ||
      !root["fadeOutMs"].is<uint32_t>() || !root["gamma"].is<float>() ||
      !root["enableWaterfall"].is<bool>() ||
      !root["waterfallDirection"].is<const char*>() ||
      !root["waterfallSpeedPps"].is<uint32_t>() ||
      !root["enableLuxGate"].is<bool>() ||
      !root["luxThreshold"].is<float>() || strips.isNull()) {
    sendError(400, "configuration contains missing or invalid field types");
    return false;
  }

  const uint32_t stripCount = root["stripCount"].as<uint32_t>();
  const uint32_t configVersion = root["configVersion"].as<uint32_t>();
  const uint32_t maxBrightness = root["maxBrightness"].as<uint32_t>();
  const uint32_t waterfallSpeedPps =
      root["waterfallSpeedPps"].as<uint32_t>();
  const String waterfallDirection =
      root["waterfallDirection"].as<String>();
  if (stripCount == 0 || stripCount > MAX_STRIPS ||
      strips.size() != stripCount) {
    sendError(422, "strip count must match 1..MAX_STRIPS entries");
    return false;
  }
  if (configVersion != LIGHTING_CONFIG_VERSION) {
    sendError(422, "unsupported config version");
    return false;
  }
  if (maxBrightness == 0 || maxBrightness > UINT8_MAX) {
    sendError(422, "brightness must be within 1..255");
    return false;
  }
  if (waterfallDirection != "top-to-bottom" &&
      waterfallDirection != "bottom-to-top") {
    sendError(422, "unsupported waterfall direction");
    return false;
  }
  if (waterfallSpeedPps < MIN_WATERFALL_SPEED_PPS ||
      waterfallSpeedPps > MAX_WATERFALL_SPEED_PPS) {
    sendError(422, "waterfall speed is outside the supported range");
    return false;
  }

  LightingConfig candidate = {};
  candidate.configVersion = static_cast<uint16_t>(configVersion);
  candidate.stripCount = static_cast<uint8_t>(stripCount);
  candidate.maxBrightness = static_cast<uint8_t>(maxBrightness);
  candidate.stripFadeInMs = root["stripFadeInMs"].as<uint32_t>();
  candidate.nextStripStartProgress =
      root["nextStripStartProgress"].as<float>();
  candidate.holdMs = root["holdMs"].as<uint32_t>();
  candidate.fadeOutMs = root["fadeOutMs"].as<uint32_t>();
  candidate.gamma = root["gamma"].as<float>();
  candidate.enableWaterfall = root["enableWaterfall"].as<bool>();
  candidate.waterfallDirection =
      waterfallDirection == "top-to-bottom"
          ? WaterfallDirection::TOP_TO_BOTTOM
          : WaterfallDirection::BOTTOM_TO_TOP;
  candidate.waterfallSpeedPps =
      static_cast<uint16_t>(waterfallSpeedPps);
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
      sendError(422, "strip pixelCount is outside the supported range");
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
}

void WebConfigServer::noteActivity(const uint32_t nowMs) {
  operatingMode_.noteConfigActivity(nowMs);
}
