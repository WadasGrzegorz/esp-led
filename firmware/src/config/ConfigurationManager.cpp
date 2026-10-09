#include "ConfigurationManager.h"

#include <Preferences.h>
#include <string.h>

namespace {
constexpr char NVS_NAMESPACE[] = "lighting";
constexpr char NVS_CONFIG_KEY[] = "config";
constexpr uint16_t DEFAULT_WATERFALL_SPEED_PPS = 40;

struct LegacyLightingConfigV1 {
  uint16_t configVersion;
  uint8_t stripCount;
  uint8_t maxBrightness;
  uint32_t stripFadeInMs;
  float nextStripStartProgress;
  uint32_t holdMs;
  uint32_t fadeOutMs;
  float gamma;
  bool enableLuxGate;
  float luxThreshold;
  StripConfig strips[MAX_STRIPS];
};

struct LegacyLightingConfigV2 {
  uint16_t configVersion;
  uint8_t stripCount;
  uint8_t maxBrightness;
  uint32_t stripFadeInMs;
  float nextStripStartProgress;
  uint32_t holdMs;
  uint32_t fadeOutMs;
  float gamma;
  bool enableWaterfall;
  WaterfallDirection waterfallDirection;
  bool enableLuxGate;
  float luxThreshold;
  StripConfig strips[MAX_STRIPS];
};

struct LegacyLightingConfigV3 {
  uint16_t configVersion;
  uint8_t stripCount;
  uint8_t maxBrightness;
  uint32_t stripFadeInMs;
  float nextStripStartProgress;
  uint32_t holdMs;
  uint32_t fadeOutMs;
  float gamma;
  bool enableWaterfall;
  WaterfallDirection waterfallDirection;
  uint16_t waterfallSpeedPps;
  bool enableLuxGate;
  float luxThreshold;
  StripConfig strips[MAX_STRIPS];
};

LightingConfig migrateLegacyConfigV1(const LegacyLightingConfigV1& legacy) {
  LightingConfig migrated = {};
  migrated.configVersion = LIGHTING_CONFIG_VERSION;
  migrated.stripCount = legacy.stripCount;
  migrated.maxBrightness = legacy.maxBrightness;
  migrated.stripFadeInMs = legacy.stripFadeInMs;
  migrated.nextStripStartProgress = legacy.nextStripStartProgress;
  migrated.stripStartMode = StripStartMode::CASCADE;
  migrated.holdMs = legacy.holdMs;
  migrated.fadeOutMs = legacy.fadeOutMs;
  migrated.fadeOutStyle = FadeOutStyle::GLOBAL;
  migrated.gamma = legacy.gamma;
  migrated.enableWaterfall = true;
  migrated.waterfallDirection = WaterfallDirection::TOP_TO_BOTTOM;
  migrated.waterfallSpeedPps = DEFAULT_WATERFALL_SPEED_PPS;
  migrated.enableLuxGate = legacy.enableLuxGate;
  migrated.luxThreshold = legacy.luxThreshold;
  for (uint8_t index = 0; index < MAX_STRIPS; ++index) {
    migrated.strips[index] = legacy.strips[index];
  }
  return migrated;
}

LightingConfig migrateLegacyConfigV2(const LegacyLightingConfigV2& legacy) {
  LightingConfig migrated = {};
  migrated.configVersion = LIGHTING_CONFIG_VERSION;
  migrated.stripCount = legacy.stripCount;
  migrated.maxBrightness = legacy.maxBrightness;
  migrated.stripFadeInMs = legacy.stripFadeInMs;
  migrated.nextStripStartProgress = legacy.nextStripStartProgress;
  migrated.stripStartMode = StripStartMode::CASCADE;
  migrated.holdMs = legacy.holdMs;
  migrated.fadeOutMs = legacy.fadeOutMs;
  migrated.fadeOutStyle = FadeOutStyle::GLOBAL;
  migrated.gamma = legacy.gamma;
  migrated.enableWaterfall = legacy.enableWaterfall;
  migrated.waterfallDirection = legacy.waterfallDirection;
  migrated.waterfallSpeedPps = DEFAULT_WATERFALL_SPEED_PPS;
  migrated.enableLuxGate = legacy.enableLuxGate;
  migrated.luxThreshold = legacy.luxThreshold;
  for (uint8_t index = 0; index < MAX_STRIPS; ++index) {
    migrated.strips[index] = legacy.strips[index];
  }
  return migrated;
}

LightingConfig migrateLegacyConfigV3(const LegacyLightingConfigV3& legacy) {
  LightingConfig migrated = {};
  migrated.configVersion = LIGHTING_CONFIG_VERSION;
  migrated.stripCount = legacy.stripCount;
  migrated.maxBrightness = legacy.maxBrightness;
  migrated.stripFadeInMs = legacy.stripFadeInMs;
  migrated.nextStripStartProgress = legacy.nextStripStartProgress;
  migrated.stripStartMode = StripStartMode::CASCADE;
  migrated.holdMs = legacy.holdMs;
  migrated.fadeOutMs = legacy.fadeOutMs;
  migrated.fadeOutStyle = FadeOutStyle::GLOBAL;
  migrated.gamma = legacy.gamma;
  migrated.enableWaterfall = legacy.enableWaterfall;
  migrated.waterfallDirection = legacy.waterfallDirection;
  migrated.waterfallSpeedPps = legacy.waterfallSpeedPps;
  migrated.enableLuxGate = legacy.enableLuxGate;
  migrated.luxThreshold = legacy.luxThreshold;
  for (uint8_t index = 0; index < MAX_STRIPS; ++index) {
    migrated.strips[index] = legacy.strips[index];
  }
  return migrated;
}
}

ConfigurationManager::ConfigurationManager(
    const LightingConfig& factoryConfig, const uint16_t logicalPixelLimit)
    : factoryConfig_(factoryConfig), logicalPixelLimit_(logicalPixelLimit) {}

void ConfigurationManager::begin() {
  config_ = factoryConfig_;

  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, true)) {
    Serial.println("Config: NVS unavailable, using factory defaults");
    return;
  }

  const size_t storedSize = preferences.getBytesLength(NVS_CONFIG_KEY);
  if (storedSize != sizeof(LightingConfig) &&
      storedSize != sizeof(LegacyLightingConfigV1) &&
      storedSize != sizeof(LegacyLightingConfigV2) &&
      storedSize != sizeof(LegacyLightingConfigV3)) {
    preferences.end();
    Serial.println("Config: no compatible saved config, using factory defaults");
    return;
  }

  constexpr size_t MAX_LEGACY_CONFIG_SIZE_V1_V2 =
      sizeof(LegacyLightingConfigV1) > sizeof(LegacyLightingConfigV2)
          ? sizeof(LegacyLightingConfigV1)
          : sizeof(LegacyLightingConfigV2);
  constexpr size_t MAX_LEGACY_CONFIG_SIZE =
      MAX_LEGACY_CONFIG_SIZE_V1_V2 > sizeof(LegacyLightingConfigV3)
          ? MAX_LEGACY_CONFIG_SIZE_V1_V2
          : sizeof(LegacyLightingConfigV3);
  constexpr size_t MAX_STORED_CONFIG_SIZE =
      sizeof(LightingConfig) > MAX_LEGACY_CONFIG_SIZE
          ? sizeof(LightingConfig)
          : MAX_LEGACY_CONFIG_SIZE;
  uint8_t storedBytes[MAX_STORED_CONFIG_SIZE] = {};
  const size_t readSize = preferences.getBytes(
      NVS_CONFIG_KEY, storedBytes, storedSize);
  preferences.end();

  if (readSize != storedSize) {
    Serial.println("Config: incomplete NVS read, using factory defaults");
    return;
  }

  uint16_t storedVersion = 0;
  memcpy(&storedVersion, storedBytes, sizeof(storedVersion));
  LightingConfig storedConfig = {};
  uint16_t migratedFromVersion = 0;
  if (storedVersion == 1 && storedSize == sizeof(LegacyLightingConfigV1)) {
    LegacyLightingConfigV1 legacy = {};
    memcpy(&legacy, storedBytes, sizeof(legacy));
    storedConfig = migrateLegacyConfigV1(legacy);
    migratedFromVersion = 1;
  } else if (storedVersion == 2 &&
             storedSize == sizeof(LegacyLightingConfigV2)) {
    LegacyLightingConfigV2 legacy = {};
    memcpy(&legacy, storedBytes, sizeof(legacy));
    storedConfig = migrateLegacyConfigV2(legacy);
    migratedFromVersion = 2;
  } else if (storedVersion == 3 &&
             storedSize == sizeof(LegacyLightingConfigV3)) {
    LegacyLightingConfigV3 legacy = {};
    memcpy(&legacy, storedBytes, sizeof(legacy));
    storedConfig = migrateLegacyConfigV3(legacy);
    migratedFromVersion = 3;
  } else if (storedVersion == LIGHTING_CONFIG_VERSION &&
             storedSize == sizeof(LightingConfig)) {
    memcpy(&storedConfig, storedBytes, sizeof(storedConfig));
  } else {
    Serial.println("Config: unsupported saved config, using factory defaults");
    return;
  }

  const ConfigValidationResult validation =
      validateLightingConfig(storedConfig, logicalPixelLimit_);
  if (!validation.valid()) {
    Serial.print("Config: saved config invalid (");
    Serial.print(configValidationErrorName(validation.error));
    Serial.println("), using factory defaults");
    return;
  }

  config_ = storedConfig;
  if (migratedFromVersion == 0) {
    Serial.println("Config: loaded from NVS");
    return;
  }
  Serial.print("Config: migrated v");
  Serial.print(migratedFromVersion);
  Serial.print(" from NVS in RAM; save to persist v");
  Serial.println(LIGHTING_CONFIG_VERSION);
}

const LightingConfig& ConfigurationManager::get() const { return config_; }

ConfigValidationResult ConfigurationManager::validate(
    const LightingConfig& candidate) const {
  return validateLightingConfig(candidate, logicalPixelLimit_);
}

ConfigValidationResult ConfigurationManager::update(
    const LightingConfig& candidate) {
  const ConfigValidationResult validation = validate(candidate);
  if (validation.valid()) {
    config_ = candidate;
  }
  return validation;
}

ConfigValidationResult ConfigurationManager::restoreFactoryDefaults() {
  return update(factoryConfig_);
}

bool ConfigurationManager::save() {
  const ConfigValidationResult validation = validate(config_);
  if (!validation.valid()) {
    return false;
  }

  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) {
    return false;
  }

  const size_t writtenSize =
      preferences.putBytes(NVS_CONFIG_KEY, &config_, sizeof(config_));
  preferences.end();
  return writtenSize == sizeof(config_);
}

bool ConfigurationManager::reset() {
  config_ = factoryConfig_;

  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) {
    return false;
  }

  const bool removed = !preferences.isKey(NVS_CONFIG_KEY) ||
                       preferences.remove(NVS_CONFIG_KEY);
  preferences.end();
  return removed;
}
