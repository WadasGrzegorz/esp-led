#include "Diagnostics.h"

extern "C" void ledbox_rmt_channel(const LedboxRmtChannel* channel) {
  diagnostics.rmtChannel(*channel);
}

extern "C" void ledbox_rmt_timings(const LedboxRmtTimings* timings) {
  diagnostics.rmtTimings(*timings);
}

extern "C" void ledbox_rmt_payload(const LedboxRmtPayload* payload) {
  diagnostics.rmtPayload(*payload);
}

extern "C" void ledbox_rmt_result(const LedboxRmtResult* result) {
  diagnostics.rmtResult(*result);
}
