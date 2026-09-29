# ESP32-C6 WS2811 corridor lighting

Non-blocking motion-triggered four-strip lighting for an ESP32-C6, two AM312
PIR sensors, a diagnostic BH1750 lux sensor, and a 24 V monochrome COB strip
driven by WS2811 controllers. The firmware keeps the verified GPIO4,
SN74AHCT125 level shifter, WS2811/GRB protocol, 60 controller count, and 180
independently addressable monochrome sections.

## Behavior

One approximately 0.5 m cut and three physical 1.5 m cuts are treated as four
logical vertical strips in one serial data chain. From the controller/DIN end,
the chain is `#1=18, #2=54, #3=54, #4=54` logical sections. They remain off
until either PIR produces a LOW-to-HIGH transition. LEFT starts the
`1 -> 2 -> 3 -> 4` cascade from the short strip, and RIGHT starts
`4 -> 3 -> 2 -> 1`:

```text
motion
  |
  v
OFF -> CASCADE_IN -> ON -> GLOBAL_FADE_OUT -> OFF
                       ^         |
        motion refreshes         | motion reverses smoothly
        the hold timer            +----> FADING_IN
```

Each complete strip fades as one unit. The next strip starts when the previous
one reaches 80% of its fade-in, giving a small overlap. Motion during fade-in
leaves the active direction stable. Each new PIR LOW-to-HIGH motion edge while
the light is ON restarts the hold countdown; a continuously HIGH PIR signal
does not prevent fade-out forever. A new motion edge during the global fade-out
starts a new directional cascade from the current brightness, without jumping
to zero or full brightness. Fade-out always affects all four strips together.

The state machine and PIR edge detection use `millis()` and do not block the
main loop. Brightness is gamma-corrected. Serial output reports state changes,
sensor readiness, and one message per rising edge:

```text
PIR motion sensing active
Motion LEFT -> cascade 1->2->3->4
Strip 1 fade-in started
Strip 2 fade-in started at 80% overlap
Strip 3 fade-in started at 80% overlap
Strip 4 fade-in started at 80% overlap
All 4 strips ON
Hold extended by motion
Global fade-out started
All 4 strips OFF
```

After boot, PIR changes are sampled but ignored for 10 seconds so AM312 startup
noise does not switch on the light. A PIR that is already HIGH when sensing
becomes active must return LOW before its next trigger can be detected.

- `firmware/config.h` is the single tuning location.
- `firmware/src/led/LedController.*` owns the verified FastLED hardware setup
  and gamma correction.
- `firmware/src/led/AnimationEngine.*` owns the directional cascade state
  machine.
- `firmware/src/sensors/PirSensor.*` owns non-blocking rising-edge detection.
- `firmware/src/sensors/Bh1750Sensor.*` reads lux without controlling the LEDs.
- `firmware/firmware.ino` connects sensor events to the animation trigger.

## Tuning

Edit `lightingConfig` in `firmware/config.h`:

- `maxBrightness` (0-255), currently 80;
- `stripFadeInMs`, currently 1680;
- `nextStripStartProgress`, currently 0.80;
- `holdMs`, currently 3000;
- `fadeOutMs`, currently 2640;
- `gamma`, currently 2.2;
- `enableLuxGate`, temporarily `false` while tuning the animation;
- `luxThreshold`, currently 15.0 lux.

The four `StripConfig` entries in the same file define `startPixel`,
`pixelCount`, and `reversed`. The verified roll has 180 logical monochrome
sections over 5 m (36/m). In physical order from the controller/DIN, the short
strip is first: `#1 [0..17]` (18 sections), `#2 [18..71]` (54),
`#3 [72..125]` (54), and `#4 [126..179]` (54). Starts are derived automatically
and a compile-time check requires the four strips to cover exactly the
180-section buffer. No guessed value is hidden in the animation engine.

PIR pins, I2C pins, the startup stabilization period, and the one-second lux
log interval are also in `firmware/config.h`. The lux gate is temporarily
disabled, so either PIR can turn the light on regardless of the current BH1750
reading. BH1750 initialization, retries, and diagnostic lux logging remain
active. Set `enableLuxGate` to `true` to restore the `luxThreshold` check with
one configuration change. There is no per-pixel animation, Wi-Fi, Matter, web
UI, or persistent storage in this milestone.

The firmware checks both BH1750 addresses (`0x23` and `0x5C`). If neither
responds, it prints an I2C scan and retries every five seconds. Once the
connection starts responding, lux logging begins automatically without another
restart.

## Wiring

### V1 hardware freeze

The wiring in this section is the **V1 hardware freeze** for moving the working
prototype from breadboard to perfboard. The GPIO assignment and signal path
below have been verified on the breadboard. Do not move these signals to other
GPIOs when assembling V1.

The LED power path does not pass through the control box or perfboard. The
24 V PSU is the power-distribution point: one branch supplies the LED strip
directly and a separate branch supplies the ELEKING converter in the control
box. Only the LED data signal and its nearby ground reference leave the control
box through `LED SIGNAL`.

```text
                                      V1 SYSTEM WIRING

       230 VAC
          |
          v
  +------------------+
  | 24 VDC main PSU  |
  +----+---------+---+
       |         |
       |         +24 V/GND: dedicated LED power branch (several amps)
       |         |
       |         +-----------------------------> LED +24 V
       |         +-----------------------------> LED GND
       |                                            |
       |                               1000 uF / 35 V
       |                                + to +24 V, - to GND
       |
       | +24 V/GND: separate control-box branch
       v
  +-----------------------------------------------------------------------+
  | CONTROL BOX / PERFBOARD                                               |
  |                                                                       |
  | POWER IN (2)                                                          |
  | +24 V, GND ---> +----------------+                                    |
  |                 | ELEKING       |                                    |
  |                 | 24 V -> 5 V   |                                    |
  |                 +---+--------+---+                                    |
  |                     | 5 V    | GND                                    |
  |          +----------+        +------------------ common GND --------+ |
  |          |                                                           | |
  |          +----> ESP32-C6 5V                                          | |
  |          |       |                                                    | |
  |          |       +-- 3V3 --> SENSORS 3V3                              | |
  |          |       +-- GPIO5 <--- SENSORS LPIR                          | |
  |          |       +-- GPIO6 <--- SENSORS RPIR                          | |
  |          |       +-- GPIO2 <---> SENSORS SDA                          | |
  |          |       +-- GPIO3  ---> SENSORS SCL                          | |
  |          |       +-- GPIO4  ---> SN74AHCT125 pin 2 (1A)               | |
  |          |                                                           | |
  |          +----------------> SN74AHCT125 pin 14 (VCC)                  | |
  |                              pin 1 (/1OE) --------> common GND -------+ |
  |                              pin 7 (GND) ---------> common GND -------+ |
  |                              pin 3 (1Y) -> 220 R -> LED SIGNAL DATA     |
  |                              100 nF between pins 14 and 7, nearby       |
  |                                                                       |
  | SENSORS (6): 3V3, GND, LPIR, RPIR, SDA, SCL                           |
  | LED SIGNAL (2): DATA, GND reference only                              |
  +----------------------+-------------------------------+----------------+
                         |                               |
              +----------+----------+                    +--> WS2811 DIN
              |          |          |                    +--> LED GND ref.
              v          v          v                         (no LED load
        AM312 LEFT  AM312 RIGHT    BH1750                       current)
        VCC: 3V3    VCC: 3V3      VCC: 3V3
        GND: common GND: common    GND: common
        OUT: LPIR   OUT: RPIR      SDA: GPIO2
                                   SCL: GPIO3
                                   ADDR: GND (0x23)

  All GND points shown are one common electrical potential:
  PSU V-, LED GND, ELEKING GND, ESP32 GND, AHCT125 GND and sensor GND.
```

The `GND` conductor in `LED SIGNAL` is a logic reference placed beside
`DATA`; it must not carry the strip's main current. The several-amp LED load
flows directly from the PSU to the strip using appropriately sized power
wiring.

### ESP32-C6 pin assignment

| V1 function | ESP32-C6 pin | Connection | Breadboard status |
|---|---:|---|---|
| BH1750 I2C data | GPIO2 | BH1750 `SDA` | Verified |
| BH1750 I2C clock | GPIO3 | BH1750 `SCL` | Verified |
| LED data | GPIO4 | SN74AHCT125 pin 2 (`1A`) | Verified |
| Left motion | GPIO5 | LEFT AM312 `OUT` | Verified |
| Right motion | GPIO6 | RIGHT AM312 `OUT` | Verified |
| Sensor power | 3V3 | both AM312 `VCC` and BH1750 `VCC` | Verified |
| Common ground | GND | PSU, LED, ELEKING, AHCT125 and all sensors | Verified |

GPIO8 and GPIO9 are deliberately not used for I2C. On the
ESP32-C6-DevKitM-1, GPIO8 also drives the onboard RGB LED and both pins are
strapping pins.

### Power distribution

```text
24 V power supply
  +24 V  ───────────────> LED strip +24 V
    |
    +──> 24 V-to-5 V converter input

  GND ───┬──────────────> LED strip GND
         ├──────────────> converter input GND
         ├──────────────> ESP32-C6 GND
         ├──────────────> SN74AHCT125 pin 7
         ├──────────────> LEFT/RIGHT AM312 GND
         └──────────────> BH1750 GND

5 V converter output
  +5 V ──┬──────────────> ESP32-C6 5 V/VBUS input
         └──────────────> SN74AHCT125 pin 14

ESP32-C6 3V3
         ├──────────────> LEFT AM312 VCC
         ├──────────────> RIGHT AM312 VCC
         └──────────────> BH1750 VCC
```

All grounds must be connected together. Never connect 24 V to the ESP32-C6,
the sensors, or the SN74AHCT125. Do not power the LED strip from the ESP32-C6,
ELEKING output, control box, or perfboard; its `+24 V` and load-carrying `GND`
come directly from the PSU.

### Planned control-box interfaces

| Interface | Conductors | Purpose |
|---|---|---|
| `POWER IN` (2) | `+24V`, `GND` | Separate PSU branch feeding ELEKING |
| `SENSORS` (6) | `3V3`, `GND`, `LPIR`, `RPIR`, `SDA`, `SCL` | Power and signals for both PIRs and BH1750 |
| `LED SIGNAL` (2) | `DATA`, `GND` | Buffered WS2811 data plus logic ground reference; no LED load current |

### LED data and level shifter

The first gate of the SN74AHCT125 converts the ESP32-C6 3.3 V data signal to a
5 V signal for the WS2811 input:

```text
ESP32-C6 GPIO4
      |
      v
SN74AHCT125 pin 2 (1A)
SN74AHCT125 pin 3 (1Y)
      |
    220 ohm
      |
      v
LED strip DIN
```

| SN74AHCT125 pin | Connection |
|---:|---|
| 1 (`/1OE`) | GND; output enable is active-low |
| 2 (`1A`) | ESP32-C6 GPIO4 |
| 3 (`1Y`) | 220 ohm series resistor, then LED `DIN` |
| 7 (`GND`) | Common GND |
| 14 (`VCC`) | Regulated 5 V |

Place a non-polarized 100 nF ceramic decoupling capacitor between pins 14 and
7, physically close to the SN74AHCT125. For the final installation, disable
unused gates and do not leave their CMOS inputs floating.

The data direction printed on the strip must point away from the controller:

```text
SN74AHCT125 -> 220 ohm -> DIN [ LED strip ] DOUT ->
```

### AM312 motion sensors

```text
LEFT AM312                 RIGHT AM312
VCC -> ESP32-C6 3V3        VCC -> ESP32-C6 3V3
GND -> common GND          GND -> common GND
OUT -> GPIO5               OUT -> GPIO6
```

The AM312 outputs are connected directly to the ESP32-C6. They do not require
the SN74AHCT125 or another level shifter.

### BH1750 light sensor

```text
BH1750      ESP32-C6
VCC    ---> 3V3
GND    ---> GND
SDA    ---> GPIO2
SCL    ---> GPIO3
ADDR   ---> GND
```

Connecting `ADDR` to GND selects I2C address `0x23`. The firmware also checks
`0x5C` to make diagnostics easier. With correct wiring, Serial should show:

```text
BH1750 detected at 0x23
Lux: 12.4
```

### Recommended supply capacitors

- 100 nF ceramic between SN74AHCT125 pins 14 and 7; it has no polarity.
- 1000 uF / 35 V electrolytic across LED `+24 V` and `GND`, close to the strip
  power input. Connect capacitor `+` to `+24 V` and capacitor `-` to GND. This
  capacitor belongs to the LED power branch, not the perfboard logic path.

Disconnect mains and low-voltage power before changing the wiring. Check the
electrolytic capacitor polarity before applying power.

## PlatformIO: compile, flash, and monitor

Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html),
then run:

```sh
make compile
```

This builds without touching a connected device. `make upload`, `make monitor`,
and `make run` remain available for intentional device use.

## Arduino CLI

After installing the ESP32 core and FastLED, compile from the repository root:

```sh
arduino-cli lib install FastLED
arduino-cli compile --fqbn esp32:esp32:esp32c6 firmware
```

Connect the board, find its exact port with `arduino-cli board list`, then flash
only when intended:

```sh
arduino-cli upload --port <PORT> --fqbn esp32:esp32:esp32c6 firmware
```

If automatic upload does not enter the bootloader, hold **BOOT**, tap **RESET**,
release **BOOT**, and run the upload command again.
