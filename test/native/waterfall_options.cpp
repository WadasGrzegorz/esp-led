#include <array>
#include <cassert>
#include <cstdio>

#include "src/diagnostics/Diagnostics.h"
#include "src/led/AnimationEngine.h"

namespace {
using Direction = AnimationEngine::Direction;

uint8_t section(const CRGB* pixels, const uint16_t index) {
  const auto& pixel = pixels[index / 3];
  return index % 3 == 0 ? pixel.g : index % 3 == 1 ? pixel.r : pixel.b;
}

void directionCase(const bool reverseOrder, const bool reversedStrip,
                   const WaterfallDirection pixelDirection) {
  LightingConfig config = DEFAULT_LIGHTING_CONFIG;
  config.stripCount = 2;
  config.maxBrightness = 255;
  config.gamma = 1;
  config.enableWaterfall = false;
  config.stripFadeInMs = 50;
  config.holdMs = 10;
  config.fadeOutMs = 2000;
  config.nextStripStartProgress = 1;
  config.fadeOutStyle = FadeOutStyle::WATERFALL;
  config.reverseFadeOutOrder = reverseOrder;
  config.fadeOutWaterfallDirection = pixelDirection;
  config.strips[0] = {3, reversedStrip};
  config.strips[1] = {3, reversedStrip};
  std::array<CRGB, hardware::WS2811_CONTROLLER_COUNT> pixels{};
  LedController leds(pixels.data(), pixels.size(), MAX_LOGICAL_PIXELS);
  leds.begin(config.maxBrightness, config.gamma);
  AnimationEngine animation(leds, config);
  animation.begin(0);
  animation.trigger(Direction::LEFT_TO_RIGHT, 0);
  animation.update(100, false);
  animation.update(110, false);
  animation.update(210, false);
  const uint8_t firstStrip = reverseOrder ? 1 : 0;
  const uint16_t firstOffset = reversedStrip != (pixelDirection == WaterfallDirection::BOTTOM_TO_TOP) ? 2 : 0;
  assert(section(pixels.data(), firstStrip * 3 + firstOffset) < 255);
  assert(section(pixels.data(), firstStrip * 3 + (2 - firstOffset)) == 255);
  for (uint16_t offset = 0; offset < 3; ++offset) {
    assert(section(pixels.data(), (1 - firstStrip) * 3 + offset) == 255);
  }
  animation.update(2110, false);
  assert(animation.isOff() && leds.maxBufferLevel() == 0);
}

uint8_t firstSectionAt(const uint32_t transitionMs, const uint32_t elapsedMs) {
  LightingConfig config = DEFAULT_LIGHTING_CONFIG;
  config.stripCount = 1;
  config.strips[0] = {20, false};
  config.maxBrightness = 255;
  config.gamma = 1;
  config.waterfallSpeedPps = 40;
  config.enableWaterfall = true;
  config.waterfallTransitionMs = transitionMs;
  std::array<CRGB, hardware::WS2811_CONTROLLER_COUNT> pixels{};
  LedController leds(pixels.data(), pixels.size(), MAX_LOGICAL_PIXELS);
  leds.begin(config.maxBrightness, config.gamma);
  AnimationEngine animation(leds, config);
  animation.begin(0);
  animation.trigger(Direction::LEFT_TO_RIGHT, 0);
  animation.update(elapsedMs, false);
  return section(pixels.data(), 0);
}
void legacyFramesUnchanged() {

 uint32_t hash=2166136261U;
 size_t cases=0;
 FastLED.captureFrames=true;
 for(const bool waterfall:{false,true}) for(const uint8_t brightness:{20,80})
 for(const float gamma:{1.0F,2.2F}) for(const auto direction:{AnimationEngine::Direction::LEFT_TO_RIGHT,AnimationEngine::Direction::RIGHT_TO_LEFT})
 for(const auto style:{FadeOutStyle::GLOBAL,FadeOutStyle::CASCADE})
 for(const auto mode:{StripStartMode::CASCADE,StripStartMode::SIMULTANEOUS}){
  LightingConfig config=DEFAULT_LIGHTING_CONFIG;
  config.stripCount=5;config.enableWaterfall=waterfall;config.maxBrightness=brightness;config.gamma=gamma;
  config.stripFadeInMs=1680;config.nextStripStartProgress=0.6F;config.holdMs=100;config.fadeOutMs=5000;
  config.fadeOutStyle=style;config.stripStartMode=mode;config.waterfallSpeedPps=53;
  for(uint8_t i=0;i<5;++i) config.strips[i]={static_cast<uint16_t>(i%2?36:54),i%2==0};
  std::array<CRGB,hardware::WS2811_CONTROLLER_COUNT> pixels{};
  LedController leds(pixels.data(),pixels.size(),MAX_LOGICAL_PIXELS);
  leds.begin(brightness,gamma);
  AnimationEngine animation(leds,config);
  diagnostics.reset({config,"legacy-comparison",0});
  animation.begin(0);animation.trigger(direction,0);
  for(uint32_t at=0;at<16000;at+=17){
   animation.update(at,false);
   for(const auto& pixel:FastLED.transmitted){
    for(const auto value:{pixel.g,pixel.r,pixel.b})hash=(hash^value)*16777619U;
   }
  }
  assert(animation.isOff());
  ++cases;
 }
 // Golden checksum generated from stable 0.2.8 (PR #2, bb1ca9e).
 assert(cases==64 && hash==0x5726A8C5U);
 printf("Legacy 0.2.8: 64 frame sequences unchanged (hash=%08X)\n",hash);
 FastLED.captureFrames=false;

}
}  // namespace

int main() {
  legacyFramesUnchanged();
  for (const bool reverse : {false, true}) {
    for (const bool reversed : {false, true}) {
      for (const auto direction : {WaterfallDirection::TOP_TO_BOTTOM, WaterfallDirection::BOTTOM_TO_TOP}) {
        directionCase(reverse, reversed, direction);
      }
    }
  }
  assert(firstSectionAt(0, 200) == 255);  // Original 8-section / 40pps transition.
  assert(firstSectionAt(600, 200) > 0 && firstSectionAt(600, 200) < 255);
  assert(firstSectionAt(600, 150) < 64);  // Eased first quarter, not a hard linear front.
  assert(firstSectionAt(600, 600) == 255);
  std::puts("Waterfall options: independent strip order, pixel direction, reversed strips and optional wider/eased transition passed");
}
