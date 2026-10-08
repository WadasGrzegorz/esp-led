# LedBox — ESP32-C6 configurable WS2811 corridor lighting

Non-blocking motion-triggered corridor lighting for an ESP32-C6, two AM312 PIR
sensors, a diagnostic BH1750 lux sensor, and a 24 V monochrome COB strip driven
by WS2811 controllers. The firmware keeps the verified GPIO4, SN74AHCT125 level
shifter, WS2811/GRB protocol, 60 controller count, and a buffer for 180
independently addressable monochrome sections.

The number and lengths of the physical vertical strips ("sople") are runtime
configuration. The safe factory fallback remains the verified four-strip chain
`18/54/54/54`, but a calibrated geometry can be saved to ESP32 NVS and used on
later boots without recompiling the firmware.

## Behavior

The strips remain off until either PIR produces a LOW-to-HIGH transition. LEFT
starts a cascade from strip `1` to `stripCount`; RIGHT starts it from
`stripCount` back to `1`:

```text
motion
  |
  v
OFF -> CASCADE_IN -> ON -> GLOBAL_FADE_OUT -> OFF
                       ^         |
        motion refreshes         | motion reverses smoothly
        the hold timer            +----> FADING_IN
```

With waterfall enabled, every strip also fills pixel by pixel while fading
smoothly: either from top to bottom or from bottom to top. Waterfall speed is
configured in logical pixels per second, so a short strip completes sooner
than a long strip while the visible movement speed remains consistent. The
per-strip `reversed` flag maps that logical direction to physical wiring, so
strips wired in opposite directions still show the same visual flow. With
waterfall disabled, each complete strip uses the configured whole-strip fade
time. The next strip starts when the previous one reaches the configured
progress (factory default 80%), giving a small overlap. Motion during fade-in
leaves the active direction stable.
Each new PIR LOW-to-HIGH motion edge while the light is ON restarts the hold
countdown. While either PIR remains HIGH, the countdown stays paused; `holdMs`
starts only after both inputs report LOW, so the light cannot fade while a
sensor still reports presence. A new motion edge during the global fade-out
starts a new directional cascade from the current brightness, without jumping
to zero or full brightness. Fade-out always affects all configured strips
together.

The state machine and PIR edge detection use `millis()` and do not block the
main loop. Brightness is gamma-corrected. Serial output reports state changes,
sensor readiness, and one message per rising edge:

```text
PIR motion sensing active
Motion LEFT -> cascade 1->2->...->N
Strip 1 fade-in started
Strip 2 fade-in started at 80% overlap
...
All N strips ON
Hold extended by motion
Global fade-out started
All N strips OFF
```

After boot, PIR changes are sampled but ignored for 10 seconds so AM312 startup
noise does not switch on the light. A PIR that is already HIGH when sensing
becomes active must return LOW before its next trigger can be detected.
Each AM312 output is read as a high-impedance ESP32 input because the module
actively drives LOW and HIGH. An 80 ms stability filter rejects short spikes,
and a stable LOW edge arms the input again. A separate 2 second minimum
interval between accepted triggers rejects repeated noise without requiring a
long continuous LOW period from the sensor. Serial logs every stable GPIO5/6
transition; animation logs include LEFT or RIGHT to identify the input.

- `firmware/config.h` contains hardware constants and the safe factory config.
- `firmware/src/config/*` owns the versioned model, validation, and NVS.
- `firmware/src/setup/*` owns calibration and strip testing independently of
  transport, so web and Serial use the same underlying methods.
- `firmware/src/mode/*` owns the physical BOOT hold, NORMAL/CONFIG_MODE state,
  activity timeout, and absolute session limit.
- `firmware/src/web/*` owns per-device AP credentials, the WPA2 SoftAP, HTTP
  API, and the local responsive UI stored in firmware.
- `firmware/src/serial/*` is the debug/service fallback transport.
- `firmware/src/led/LedController.*` owns the verified FastLED hardware setup
  and gamma correction.
- `firmware/src/led/AnimationEngine.*` owns the directional cascade state
  machine.
- `firmware/src/sensors/PirSensor.*` owns non-blocking rising-edge detection.
- `firmware/src/sensors/Bh1750Sensor.*` reads lux without controlling the LEDs.
- `firmware/firmware.ino` connects setup, sensors, and animation.

## Why there is no true auto-discovery

WS2811 data flows in one direction only:

```text
ESP32 -> WS2811 -> WS2811 -> WS2811 -> ...
```

The controllers do not return an acknowledgement, length, address, or any
other data. Sending 180 or 500 values looks identical to the ESP32 even if the
physical chain ends much earlier. Firmware therefore cannot discover the
number of sections or the physical strip boundaries without feedback hardware.
The calibration wizard is deliberately semi-automatic: it moves a visible
marker and asks the installer to confirm each physical boundary.

## Persistent configuration

`LightingConfig` contains:

- schema `configVersion`;
- `stripCount` and up to `MAX_STRIPS` (currently 16) entries with `pixelCount`
  and `reversed`;
- `maxBrightness`, `stripFadeInMs`, `nextStripStartProgress`, `holdMs`,
  `fadeOutMs`, and `gamma`;
- `enableWaterfall`, `waterfallDirection` (`top-to-bottom` or
  `bottom-to-top`), and `waterfallSpeedPps`;
- `enableLuxGate` and `luxThreshold`.

`startPixel` is never persisted. It is calculated as the cumulative sum of the
preceding `pixelCount` values, avoiding two sources of truth. Every strip must
contain at least one logical section and the total may not exceed the 180-entry
LED buffer. Config v1 and v2 data is migrated in RAM to v3, using 40 pixels per
second as the initial waterfall speed. Other invalid, missing, wrong-sized, or
unsupported-version NVS data is rejected and the firmware starts with the
factory `18/54/54/54` geometry.

The factory configuration is:

```text
Config v3: 4 strips, 180 logical pixels
  #1: start=0,   count=18, end=17
  #2: start=18,  count=54, end=71
  #3: start=72,  count=54, end=125
  #4: start=126, count=54, end=179
brightness=80, fadeInMs=1680, nextStart=80%
holdMs=3000, fadeOutMs=2640, gamma=2.2
waterfall=top-to-bottom, speed=40 px/s
luxGate=off, luxThreshold=15.0
```

The lux gate remains off by default during animation tuning. BH1750 detection,
retries, readings, and logging remain active. Enabling the flag later restores
the threshold check without changing the sensor or animation code.

## Security / Config Mode

Normal boot always enters `NORMAL`. It loads the valid NVS configuration (or
the safe factory fallback), starts normal PIR/animation behavior, and leaves
all configuration commands locked. It does not start Wi-Fi, an access point,
an HTTP server, or a captive portal. There is no automatic post-reboot window
in which the configurator is exposed.

The project targets Espressif `ESP32-C6-DevKitM-1`. Its physical BOOT button is
active-low on GPIO9. GPIO9 is also the chip's download-mode strapping pin:
holding BOOT while resetting or powering the board asks the ROM to enter the
serial downloader, so application firmware cannot safely use that gesture.
CONFIG MODE therefore uses the safe alternative:

```text
normal boot -> NORMAL
                 |
                 +-- hold physical BOOT for 3 seconds after startup
                                    |
                                    v
                              CONFIG_MODE
                     WPA2 SoftAP + local HTTP UI
                     PIR/lux-triggered animation off
                                    |
                    save / exit / session timeout
                                    |
                                    v
                         restart -> NORMAL
```

A short BOOT press does nothing. The button must first be observed released,
then held continuously for `CONFIG_BUTTON_HOLD_MS` (default 3 seconds).
`CONFIG_MODE_TIMEOUT_MS` defaults to 5 minutes of inactivity. Meaningful UI
activity refreshes that timer, but an absolute 15-minute limit prevents a
forgotten page from keeping the AP alive indefinitely. PIR inputs continue to
be sampled so stale edges are not replayed. BH1750 readings remain available
for diagnostics, but PIR and lux-gate triggers cannot start normal animation
inside CONFIG MODE. A successful save, explicit Serial exit, inactivity
timeout, or absolute limit stops HTTP and the AP before restarting into NORMAL.

The AP uses WPA2-PSK with CCMP. On the first CONFIG MODE entry, firmware creates
a random password in the readable `XXXX-XXXX-XX` format and stores it in a
separate NVS namespace. The password is stable across restarts and unique per
device; there is no shared fleet credential. The SSID uses an eFuse-derived suffix such as
`LedBox-A3F2`. During development, Serial prints the SSID, password, and
local URL. This credential can later be put on a device label or encoded in a
QR code without changing the AP or HTTP protocol. If NVS cannot persist the
credential, the AP fails closed and remains off.

## Web Config user flow

1. Boot normally, release BOOT, then hold BOOT for about 3 seconds.
2. Wait for `CONFIG MODE` on Serial (during development) and connect a phone or
   laptop to the printed `LedBox-XXXX` SSID using its per-device password.
3. Open `http://ledbox.local` in a browser. If the client does not support
   mDNS, use the fallback address `http://192.168.4.1`.
4. Configure strip count/lengths, orientation, animation, brightness, gamma,
   waterfall mode/direction/speed, and lux gating. Use strip and directional
   tests as needed.
5. Optionally run **Calibrate geometry**, move the visible marker, mark each
   physical boundary, and finish on the last logical pixel.
6. Select **Save & restart**. The firmware validates the full configuration,
   writes it to NVS, acknowledges success, then stops the AP and restarts into
   NORMAL with Wi-Fi disabled.

Form edits and calibration results are working configuration in RAM. They are
not written to flash until **Save & restart**. **Reset to defaults** likewise
loads `18/54/54/54` into the working copy and requires a save to persist it.
Strip tests stop automatically after 30 seconds or via **Stop test**. All web
tests are capped at a safe brightness even when the configured runtime
brightness is higher.

The UI is a single responsive HTML/CSS/JavaScript document stored in firmware;
it uses no framework, CDN, internet service, station mode, or home network.
The friendly `ledbox.local` name is advertised with mDNS only during CONFIG
MODE and disappears together with the AP/server.
The JSON API is intentionally small:

| Method and path | Purpose |
|---|---|
| `GET /api/status` | Session time, live lux, geometry summary, and calibration/test state |
| `GET /api/config` | Read the current working configuration |
| `PUT /api/config` | Validate and replace the working configuration in RAM |
| `POST /api/save` | Validate, persist to NVS, acknowledge, and schedule NORMAL restart |
| `POST /api/reset` | Load factory defaults into RAM without writing NVS |
| `POST /api/strip/test` | Start or stop a safe single-strip test |
| `POST /api/animation/test` | Start a LEFT or RIGHT cascade from the working configuration |
| `POST /api/test/stop` | Stop the active visual test |
| `POST /api/calibration/{start,move,mark,undo,finish,cancel}` | Drive the shared calibration controller |
| `POST /api/session/activity` | Refresh the idle timer for real UI interaction |

All limits are validated again in firmware; browser validation is only for
convenience. JSON bodies over 4 KiB are rejected. The server is available only
on the physically activated SoftAP and stops at CONFIG MODE exit.

## Serial service fallback

Open the serial monitor at 115200 baud. `config show` and `help` are available
in NORMAL; all mutating, calibration, and strip-test commands require physical
entry into CONFIG MODE first. Commands are newline-terminated:

| Command | Effect |
|---|---|
| `config show` | Print active geometry, timing, brightness, gamma, and lux settings |
| `calibration start` | Start calibration at logical section 0 within CONFIG MODE |
| `calibration next` / `calibration +` | Move the marker forward by one section |
| `calibration prev` / `calibration -` | Move the marker backward by one section, without crossing the current strip start |
| `calibration + <n>` / `calibration - <n>` | Move by several sections |
| `calibration goto <index>` | Jump directly to a zero-based logical section |
| `calibration mark` | Mark the current section as the inclusive end of this strip and begin the next one |
| `calibration undo` | Remove the most recent boundary |
| `calibration finish` | Close the last strip at the current section, validate, apply in RAM, and leave calibration |
| `calibration cancel` | Leave calibration without changing the active config |
| `config save` | Persist the active config to NVS, close CONFIG MODE, and restart into NORMAL |
| `config reset` | Remove the saved NVS config and immediately restore `18/54/54/54` |
| `config exit` | Leave CONFIG MODE without saving and restart into NORMAL |
| `strip test <n>` | Light only configured strip `n` at a safe test level inside CONFIG MODE |
| `strip test off` | End the current strip test while remaining in CONFIG MODE |
| `help` | Print the command summary |

During calibration, already marked ranges are dim, the current candidate strip
is brighter, and the current logical section is brightest. This makes the
exact cursor visible while keeping the accumulated range easy to recognize.
Both PIRs continue to be sampled so stale edges are not replayed later, but
their triggers and the lux gate are ignored until CONFIG MODE ends.

Example recreating the factory boundaries:

```text
calibration start
calibration goto 17
calibration mark
calibration goto 71
calibration mark
calibration goto 125
calibration mark
calibration goto 179
calibration finish
config show
strip test 1
strip test off
config save
```

`calibration finish` intentionally does not write flash. The new geometry runs
immediately, which allows testing each strip first. `config save` is the
explicit durability step. Until it succeeds, a reboot returns to the last
valid NVS configuration (or factory defaults when none exists).

Serial remains useful for development and recovery, but it is not the primary
configuration experience. There is no captive portal, Matter, cloud, external
Wi-Fi client mode, or remote access.

## Tuning and sensors

The timing and sensor defaults remain in `DEFAULT_LIGHTING_CONFIG` in
`firmware/config.h`; the active runtime object is loaded from NVS. Hardware
pins, the startup stabilization period, and the one-second lux log interval
also remain in that file.

The firmware checks both BH1750 addresses (`0x23` and `0x5C`). If neither
responds, it prints an I2C scan and retries every five seconds. Once the
connection starts responding, lux logging begins automatically without another
restart. A failed sample invalidates the old lux value, stops repeated reads,
and schedules an I2C bus reset plus sensor detection retry after five seconds.
This keeps a temporary bus fault from flooding Serial or leaving the lux gate
dependent on a stale measurement. Lux reads pause while LEDs are active and
for 300 ms after they switch off, because the fixture's own light is not a
useful ambient measurement and LED load changes can add noise to the I2C bus.
Sensor initialization retries the documented power-on, reset, and continuous
high-resolution command sequence before reporting the device unavailable.

If PIR triggers still appear without movement, the Serial message identifies
the affected side. Check that the AM312 output is connected to GPIO5 (LEFT) or
GPIO6 (RIGHT), that all modules share ground, and that the sensor/power wires
are kept away from the LED power path. For persistent BH1750 errors, verify SDA
on GPIO2, SCL on GPIO3, 3.3 V-compatible pull-ups, and the address shown by the
I2C scan.

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
| Config entry | GPIO9 | Onboard active-low `BOOT` button; hold only after startup | Board-defined |
| Sensor power | 3V3 | both AM312 `VCC` and BH1750 `VCC` | Verified |
| Common ground | GND | PSU, LED, ELEKING, AHCT125 and all sensors | Verified |

GPIO8 and GPIO9 are deliberately not used for I2C. On the
ESP32-C6-DevKitM-1, GPIO8 also drives the onboard RGB LED and both pins are
strapping pins. GPIO9 is read only after startup for physical CONFIG MODE
entry; holding it during reset selects the ROM downloader instead.

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
