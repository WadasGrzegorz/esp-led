#include "AccessPointCredentials.h"

#include <Preferences.h>
#include <esp_random.h>
#include <string.h>

namespace {
constexpr char NVS_NAMESPACE[] = "webcfg";
constexpr char NVS_PASSWORD_KEY[] = "apSecret";
constexpr char PASSWORD_ALPHABET[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
constexpr uint8_t PASSWORD_RANDOM_CHARACTER_COUNT = 10;
constexpr uint8_t PASSWORD_LENGTH = 12;
constexpr char PASSWORD_SEPARATOR = '-';
}

bool AccessPointCredentials::begin() {
  ssid_ = createSsid();

  Preferences preferences;
  if (!preferences.begin(NVS_NAMESPACE, false)) {
    Serial.println("Web Config: cannot open credential storage; AP stays off");
    return false;
  }

  const String storedPassword = preferences.getString(NVS_PASSWORD_KEY, "");
  if (isValidPassword(storedPassword)) {
    password_ = storedPassword;
    preferences.end();
    return true;
  }

  const String generatedPassword = createPassword();
  const size_t writtenLength =
      preferences.putString(NVS_PASSWORD_KEY, generatedPassword);
  preferences.end();
  if (writtenLength != generatedPassword.length()) {
    Serial.println("Web Config: cannot persist device password; AP stays off");
    return false;
  }

  password_ = generatedPassword;
  Serial.println("Web Config: created persistent per-device AP password");
  return true;
}

const String& AccessPointCredentials::ssid() const { return ssid_; }

const String& AccessPointCredentials::password() const { return password_; }

String AccessPointCredentials::createSsid() const {
  const uint16_t suffix = static_cast<uint16_t>(ESP.getEfuseMac() & 0xFFFFU);
  char value[24] = {};
  snprintf(value, sizeof(value), "LedBox-%04X", suffix);
  return String(value);
}

String AccessPointCredentials::createPassword() const {
  String value;
  value.reserve(PASSWORD_LENGTH);
  for (uint8_t index = 0; index < PASSWORD_RANDOM_CHARACTER_COUNT; ++index) {
    if (index == 4 || index == 8) {
      value += PASSWORD_SEPARATOR;
    }
    const uint32_t randomValue = esp_random();
    value += PASSWORD_ALPHABET[randomValue % (sizeof(PASSWORD_ALPHABET) - 1)];
  }
  return value;
}

bool AccessPointCredentials::isValidPassword(const String& value) const {
  if (value.length() != PASSWORD_LENGTH) {
    return false;
  }

  for (uint8_t index = 0; index < value.length(); ++index) {
    const bool separatorPosition = index == 4 || index == 9;
    if (separatorPosition) {
      if (value[index] != PASSWORD_SEPARATOR) {
        return false;
      }
      continue;
    }

    if (strchr(PASSWORD_ALPHABET, value[index]) == nullptr) {
      return false;
    }
  }
  return true;
}
