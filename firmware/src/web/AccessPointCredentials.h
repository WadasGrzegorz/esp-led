#pragma once

#include <Arduino.h>

class AccessPointCredentials {
 public:
  bool begin();

  const String& ssid() const;
  const String& password() const;

 private:
  String createSsid() const;
  String createPassword() const;
  bool isValidPassword(const String& value) const;

  String ssid_;
  String password_;
};
