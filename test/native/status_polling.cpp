#include <cassert>
#include <cstdio>
#include <string>
#include <ArduinoJson.h>

#include "config.h"
#include "src/diagnostics/Diagnostics.h"
#include "src/FirmwareInfo.h"

namespace {
unsigned imageVerifications = 0;
unsigned freeSpaceReads = 0;
uint32_t freeHeap = 200000;
unsigned responses = 0;
std::string lastResponse;
struct esp_partition_t { uint32_t address; uint32_t size; };
struct esp_partition_pos_t { uint32_t offset; uint32_t size; };
struct esp_image_metadata_t { uint32_t start_addr; uint32_t image_len; };
enum sketchSize_t { SKETCH_SIZE_TOTAL, SKETCH_SIZE_FREE };
constexpr int ESP_IMAGE_VERIFY = 0;
const esp_partition_t* esp_ota_get_running_partition() {
  static const esp_partition_t running{65536, 1966080};
  return &running;
}
void esp_image_verify(int, const esp_partition_pos_t*, esp_image_metadata_t* image) {
  ++imageVerifications;
  image->image_len = 1325024;
  // Simulate expensive flash access. The assertion concerns verification calls,
  // not a claimed measurement of its execution time on the physical device.
  testClockUs += 160000;
}
class EspClass {
 public:
  uint32_t getSketchSize();
  uint32_t getFreeSketchSpace() { ++freeSpaceReads; return 1966080; }
  uint32_t getFreeHeap() { return freeHeap; }
  uint32_t getMinFreeHeap() { return 180000; }
} ESP;
#include "sdk_sketch_size.inc"
#include "firmware_image_types.inc"
#include "firmware_image_read.inc"

int esp_reset_reason() { return 0; }
const char* resetReasonName(int) { return "test"; }
struct { unsigned softAPgetStationNum() { return 1; } } WiFi;
struct ConfigProvider {
  LightingConfig working = DEFAULT_LIGHTING_CONFIG;
  const LightingConfig& get() const { return working; }
};
struct Setup {
  bool isStripTestActive() const { return false; }
  bool isCalibrationActive() const { return false; }
  uint16_t cursor() const { return 0; }
  uint16_t currentStripStart() const { return 0; }
  uint16_t markedBoundaryCount() const { return 0; }
  uint16_t markedBoundary(uint8_t) const { return 0; }
};
struct OperatingMode { uint32_t configModeRemainingMs(uint32_t) const { return 60000; } };
struct Pir { bool active = false; bool isActive() const { return active; } };
struct Light { uint8_t address() const { return 0x23; } };
void sendDocument(int&, uint16_t status, JsonDocument& document) {
  assert(status == 200);
  ++responses;
  lastResponse.clear();
  serializeJson(document, lastResponse);
}
class WebConfigServer {
 public:
  void handleStatus();
  void primeMetadata() {
#include "firmware_image_prime.inc"
  }
  bool requireConfigMode() const { return allowed; }
  bool allowed = true;
  FirmwareImageInfo firmwareImageInfo_ = {};
  uint32_t firmwareImageInfoReads_ = 0;
  ConfigProvider configuration_;
  Setup setup_;
  OperatingMode operatingMode_;
  Pir leftPir_, rightPir_;
  Light lightSensor_;
  WebConfigSettings settings_{MAX_LOGICAL_PIXELS, 1000, 1000};
  bool lightSensorAvailable_ = false;
  bool hasLuxReading_ = false;
  float currentLux_ = 0;
  bool animationTestActive_ = true;
  bool diagnosticActive_ = true;
  int server_ = 0;
};
#include "status_handler.inc"
}

int main() {
  WebConfigServer server;
  server.primeMetadata();
  server.primeMetadata();
  assert(imageVerifications == 1 && freeSpaceReads == 1);
  assert(server.firmwareImageInfo_.readDurationUs == 160000);
  const auto before = imageVerifications;
  server.configuration_.working.stripCount = 5;
  for (uint8_t index = 0; index < 5; ++index) {
    server.configuration_.working.strips[index] = {static_cast<uint16_t>(index % 2 ? 36 : 54), true};
  }
  server.leftPir_.active = true;
  freeHeap = 190000;
  for (uint32_t request = 0; request < 100; ++request) {
    testClockMs = request * 1000;
    server.handleStatus();
    JsonDocument response;
    assert(!deserializeJson(response, lastResponse));
    assert(response["stripCount"].as<int>() == 5 && response["totalPixels"].as<int>() == 234);
    assert(response["sensors"]["leftPirActive"].as<bool>());
    assert(response["device"]["freeHeapBytes"].as<int>() == 190000);
    assert(response["device"]["uptimeMs"].as<uint32_t>() == testClockMs);
    assert(response["device"]["sketchSizeBytes"].as<int>() == 1325024);
    assert(response["device"]["freeSketchSpaceBytes"].as<int>() == 1966080);
    assert(response["animationTestActive"].as<bool>());
  }
  std::printf("100 status requests: %u full image verifications after warm-up\n", imageVerifications - before);
  if (imageVerifications != before) return 1;
  assert(freeSpaceReads == 1 && server.firmwareImageInfoReads_ == 1);
  server.allowed = false;
  server.handleStatus();
  assert(responses == 100);
  return 0;
}
