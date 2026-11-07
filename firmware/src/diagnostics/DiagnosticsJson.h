#pragma once

#include <ArduinoJson.h>
#include "Diagnostics.h"

void writeDiagnosticDetails(JsonDocument& document, const Diagnostics& diagnostics);
