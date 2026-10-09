#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>

#include "AccessPointCredentials.h"
#include "WebConfigTypes.h"

class WebConfigServer {
 public:
  WebConfigServer(const WebConfigDependencies& dependencies,
                  const WebConfigSettings& settings);

  void ensureWirelessOff();
  bool begin(uint32_t nowMs);
  void update(uint32_t nowMs);
  void end();

  bool isRunning() const;
  bool isAnimationTestActive() const;
  bool consumeConfigurationChanged();

 private:
  void registerRoutes();
  void handleStatus();
  void handleGetConfig();
  void handleExportConfig();
  void handleImportConfig();
  void handlePutConfig();
  void handleSave();
  void handleReset();
  void handleStripTest();
  void handleAnimationTest();
  void handleStopTest();
  void handleCalibrationStart();
  void handleCalibrationMove();
  void handleCalibrationAction(const char* action);
  void handleFirmwareUpload();
  void handleFirmwareUploadComplete();
  void failFirmwareUpload(const String& message);
  void handleNotFound();

  bool requireConfigMode();
  bool parseRequest(JsonDocument& jsonDocument);
  bool updateWorkingConfig(const JsonDocument& jsonDocument);
  void sendConfig(uint16_t statusCode = 200);
  void sendOk(const char* message);
  void sendError(uint16_t statusCode, const String& message);
  void sendSetupError(SetupResult result);
  void stopVisualActions(uint32_t nowMs, bool includeCalibration);
  void noteActivity(uint32_t nowMs);

  ConfigurationManager& configuration_;
  SetupController& setup_;
  OperatingModeController& operatingMode_;
  AnimationEngine& animation_;
  LedController& leds_;
  PirSensor& leftPir_;
  PirSensor& rightPir_;
  Bh1750Sensor& lightSensor_;
  const bool& lightSensorAvailable_;
  const bool& hasLuxReading_;
  const float& currentLux_;
  const WebConfigSettings settings_;
  AccessPointCredentials credentials_;
  WebServer server_{80};
  bool routesRegistered_ = false;
  bool running_ = false;
  bool mdnsRunning_ = false;
  bool configurationChanged_ = false;
  bool animationTestActive_ = false;
  bool stripTestActive_ = false;
  bool firmwareUploadStarted_ = false;
  bool firmwareUploadFailed_ = false;
  bool restartScheduled_ = false;
  size_t firmwareUploadBytes_ = 0;
  String firmwareUploadError_;
  uint32_t stripTestEndsAtMs_ = 0;
  uint32_t restartAtMs_ = 0;
};
