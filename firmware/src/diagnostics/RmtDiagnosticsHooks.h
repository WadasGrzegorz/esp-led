#pragma once

// C ABI shared with the pinned FastLED driver. All hooks run in task context;
// no work is added to the streaming encoder's ISR.
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int pin;
  uint32_t resolution_hz;
  uint32_t memory_symbols;
  uint32_t queue_depth;
  int configured_priority;
  uint8_t dma;
  uint8_t inverted;
} LedboxRmtChannel;

typedef struct {
  uint32_t resolution_hz;
  uint32_t zero_high_ticks;
  uint32_t zero_low_ticks;
  uint32_t one_high_ticks;
  uint32_t one_low_ticks;
  uint32_t reset_ticks;
} LedboxRmtTimings;

typedef struct {
  const uint8_t* bytes;
  size_t size;
  uint8_t completed;
} LedboxRmtPayload;

typedef struct {
  // 1=enable, 2=transmit, 3=wait-done, 4=disable.
  uint8_t operation;
  int code;
} LedboxRmtResult;

void ledbox_rmt_channel(const LedboxRmtChannel* channel) __attribute__((weak));
void ledbox_rmt_timings(const LedboxRmtTimings* timings) __attribute__((weak));
void ledbox_rmt_payload(const LedboxRmtPayload* payload) __attribute__((weak));
void ledbox_rmt_result(const LedboxRmtResult* result) __attribute__((weak));

#ifdef __cplusplus
}
#endif
