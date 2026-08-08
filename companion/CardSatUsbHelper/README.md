# CardSatUsbHelper

A second USB host for CardSat, on the end of a Grove cable. Runs on an
**M5StickS3** (ESP32-S3-PICO-1-N8R8, 8 MB flash, 8 MB PSRAM, native USB OTG).

## Why

The ESP32-S3 has **8 USB host channels for the entire bus**, one per open pipe
including every device's default control pipe:

| device | channels |
| --- | --- |
| hub | 2 |
| CDC radio (TH-D75, IC-705's CDC function) | 3 |
| vendor-serial adapter (Prolific) | 4 |
| FTDI FT232R | 3 |

The **IC-705 contains its own internal TI TUSB2046 hub**, so the radio costs **5**
on its own:

```
hub + IC-705            =  7   works
hub + TH-D75 + FTDI     =  8   works, no headroom
hub + TH-D75 + IC-705   = 10   cannot be made to fit
```

`usb_host_interface_claim()` takes every endpoint of an interface or none, so there
is no software arrangement of eight channels that holds two USB radios when one is
an IC-705. A second microcontroller brings its own eight.

The other case it solves is smaller but just as real: a USB radio **plus** a USB
rotator on the Cardputer is `hub + 3 + 3 = 8` — the absolute ceiling, nothing
spare. Moving the rotator here leaves the radio a bus to itself.

## What it does, and does not

**Does:** enumerate USB serial devices, open one, set its line coding and CDC
control lines, and move bytes to and from CardSat over Grove.

**Does not:** speak any CAT dialect, know any radio model, do anything with Doppler
or VFOs or rotator grammar, or store anything across reboots. CardSat owns all of
that, exactly as it does for an adapter plugged into the Cardputer directly.

Being stateless is also the recovery story: there is nothing to lose, so rebooting
is always safe — and a reboot is the only thing that reliably clears a wedged USB
host stack.

## Wiring

```
Cardputer G2 (TX) ------> Stick GPIO9  (Grove RX)
Cardputer G1 (RX) <------ Stick GPIO10 (Grove TX)
Cardputer GND     ------- Stick GND
```

Both ends are 3.3 V, so no level shifter. **If nothing is ever received, swap the
two signals** — a reversed pair is silent, not noisy, so it looks exactly like a
dead cable.

The radio or adapter plugs into a **self-powered USB hub**, and the hub into the
Stick's USB-C port.

## Power — read this before wiring 5 V anywhere

The Stick does **not** source USB VBUS. M5Stack frame its USB-C port as a power
*input*, and the 5 V boost feeds the Grove / Hat2 EXT_5V rail instead. **A
self-powered hub is required for the radio**, exactly as it is on the Cardputer.

The Stick itself can be fed 5 V from the Cardputer's Grove rail over the same cable
that carries the data. The firmware clears `cfg.output_power` before `M5.begin()`
and calls `M5.Power.setExtOutput(false)`, forcing EXT_5V to **input**, so the Stick
can only ever *receive* 5 V on Grove — safe no matter how the Cardputer's rail is
configured.

**Never call `setExtOutput(true)` while anything feeds that pin.** Two supplies
fighting on one wire is the short-circuit case M5Stack explicitly warn about.

## Using it from CardSat

Settings → Radio → **USB helper >** opens the helper screen: link state, firmware
version, the devices the Stick can see, and the link baud. Entering the screen
brings the link up even before a transport is selected, so you can see what is
plugged in before choosing.

Then pick one of:

* CAT type **USB helper (Grove)** — a single radio on the helper.
* Dual-rig leg bus **Helper** — one leg on the helper, the other on local USB or LAN.
* Rotator wire **USB helper** — the rotator on the helper, radio elsewhere.

**One device at a time.** The helper carries a single USB device, so CardSat refuses
a second claimant rather than letting them race for the port.

Because the helper owns the Grove UART (G1/G2), it is mutually exclusive with wired
CI-V, the Grove GPS, a Grove rotator, `rigctl (Grove)` and any Grove dual-rig leg.

## The screen

Off by default — the Stick runs from a 250 mAh cell and this may sit on a desk for
a whole pass with nothing anyone needs to watch. **Button A** wakes it for 12
seconds: link rate and lock state, the open port, USB and frame counters, CRC/COBS/
overrun errors (red only when non-zero, so a clean link is visibly clean), device
count and free heap. Press again to dismiss.

## Build

**Two preparation steps, exactly as for CardSat itself, and both are silent if
skipped:**

1. Copy `third_party/EspUsbHost/` over the installed EspUsbHost library. The stock
   Library Manager copy compiles and appears to work, then strands the USB stack the
   first time a radio stops answering.
2. Run CardSat's `./tools/vendor_usb_host.sh`. This sketch `#include <UsbHostSrc.h>`,
   so without it arduino-cli **silently skips the library** and links Arduino's
   prebuilt `libusb.a` — losing the enumeration retry, the reset timings and the
   enumeration filter callback, with no error to say so.

Then **delete the sketch build cache.** `build_opt.h` is not a dependency of any
object file, so a changed flag otherwise yields a byte-identical binary that looks
exactly like the flag having no effect.

```
arduino-cli compile \
  --fqbn "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default,FlashSize=8M,PartitionScheme=default_8MB,PSRAM=enabled,DebugLevel=error" \
  --build-property "compiler.cpp.extra_flags=-DCORE_DEBUG_LEVEL=1" \
  CardSatUsbHelper
```

`ESP_USB_HOST_MAX_DEVICES` and the USB host tunables live in this folder's
`build_opt.h` (a copy of CardSat's), because they must reach the LIBRARY's own
translation units, not just the sketch. `CORE_DEBUG_LEVEL` stays in
`compiler.cpp.extra_flags`, which **appends** — `build.extra_flags` replaces,
wiping the core's own value and reintroducing an EspUsbHost `TAG` compile error.

**Verify the vendoring took**, in the map: `libraries/UsbHostSrc` in the hundreds
and `libusb.a(` at zero.

In the IDE: board *ESP32S3 Dev Module*, USB Mode *Hardware CDC and JTAG*, Flash
8 MB, PSRAM enabled, Partition *8M with spiffs*, Core Debug Level *Error*.

Prebuilt binaries are in [`firmware/`](firmware/).

## Hardware path: CoreS3-SE as the companion (no VBUS injection at all)

Researched 2026-08: the M5Stack **CoreS3 / CoreS3-SE** is the one M5 board in
this class whose USB-C can SOURCE VBUS under firmware control. Its AXP2101 PMU +
AW9523B expander expose a documented `USB_OTG_EN` power path, and M5Unified's
`M5.Power.setUsbOutput(true)` is implemented for exactly this board family (it
is a no-op on the StickS3 — verified in the library source). Same ESP32-S3, so
the entire helper firmware carries over: same EspUsbHost stack and patches, same
CSUH protocol, same channel budget. 16 MB flash / 8 MB PSRAM (more than the
Stick on both counts), 2" 320×240 touch IPS, SD slot.

Port effort is small and mechanical: `setUsbOutput(true)` at boot (keeping
`setExtOutput(false)` for Grove safety), Grove-link pin constants for Port A,
the status sprite at 320×240, and the 16 MB partition table. BtnA/B/C map to
touch zones through M5Unified unchanged.

Trade-offs: physically much larger than the Stick (it is a Core, not a stick);
the SE variant (~cheaper, no camera/IMU/battery) has NO internal battery, so in
the CardSat topology it runs Grove-powered from the Cardputer exactly like the
Stick does — verify once with a meter that Port A 5 V-in powers it cleanly.
Keep the C-plug→A-socket adapter topology for the radio regardless, which
sidesteps any CC question. Two things to bench-verify before committing: VBUS
actually present on the C port with `setUsbOutput(true)` (one meter check), and
the AXP2101 boost budget carrying the intended device load.

Not viable, for the record: AtomS3 family (device-role port, same problem as
the Stick), Core2/Station (older ESP32, no native USB host), Tab5 (ESP32-P4 —
different USB architecture, not a drop-in for this S3 stack), and the MAX3421E
USB Module (SPI host controller — wrong software stack entirely).

## If nothing ever enumerates

The first bench session hit exactly this — no device, powered hub or not — so the
status screen now answers the first question itself. Press **Button A** and read
the bottom lines:

1. **`USB HOST FAILED`** → the host stack did not install. Firmware/PHY problem;
   capture the boot log.
2. **`attach 0`** with a device plugged in → no attach interrupt has EVER fired.
   This is **power or wiring, not firmware** — in order of likelihood:
   * **No VBUS.** Put a meter on the hub's *upstream* connector: a hub waits for
     host VBUS before it attaches, and most devices will not raise D+ without
     seeing 5 V either — which is why a powered hub changes nothing. The StickS3
     cannot enable USB VBUS in firmware through M5Unified (`setUsbOutput()` is a
     no-op for this board). If the meter reads ~0 V, either inject 5 V into the
     cable's VBUS (a USB "power injector"/OTG Y-cable), or run the bench
     experiment below.
   * **USB-C CC lines.** A C-to-C cable to the hub may never signal a host role.
     Prefer a C-plug-to-A-socket OTG adapter and an A cable to the hub.
3. **`attach N, seen M, usable 0`** → enumeration WORKS; the serial claim is what
   fails. That is firmware territory — note the `last` vid:pid line and capture
   the log.

### VBUS: settled by the schematic — injection is the only way

The K150 StickS3 schematic (V0.6) and M5's docs close the question the first
bench sessions raised:

* **USB-C VBUS is input-only.** It runs through an AW32901 OVP load switch into
  the LGS4056 battery charger. There is no boost converter behind that pin, no
  OTG mode, no register — no firmware on this board can ever source VBUS.
* **`EXT_5V_EN` (the PM1 boost) feeds the Grove port, the Hat `EXT_5V` pin and
  the IR pair only.** `-DCSUH_FORCE_EXT_OUTPUT` therefore does nothing for USB
  and is retired for that purpose (it survives for the Grove/IR case, hazard
  warnings unchanged).
* **The CC pins carry fixed 5.1 kΩ pull-downs** — the connector is permanently
  device-role at the CC level. Host mode works because the ESP32-S3 PHY does the
  data role regardless, which is also why the working topology goes through a
  USB-A socket (no CC) rather than C-to-C.

### Field power: replacing the Y-cable + battery

The radio-side VBUS must be injected, so the goal is one clean harness instead
of a Y-cable and a second battery. Ranked:

1. **Try the backfeed lottery first (free, one minute).** Some powered hubs
   backfeed upstream VBUS even though they should not. Powered hub, no Y-cable,
   radio behind it: if `attach` ticks, that hub model IS the field solution.
2. **The Grove-powered loom (recommended build).** The Cardputer's Grove cable
   already carries 5 V to the Stick — tap it for VBUS instead of carrying a
   battery. One captive harness: Grove plug (Cardputer) → Grove plug (Stick),
   with 5 V and GND tapped and continued into the VBUS/GND of a compact
   C-plug→A-socket OTG adapter on the Stick's USB-C; the radio's own A→micro-B
   cable plugs into the A socket. Result: zero extra batteries, one loom, the
   whole station powered from the Cardputer. Wiring: Grove pin 2 (5 V) → USB-A
   pin 1; Grove pin 1 (GND) → USB-A pin 4; D+/D− pass through the OTG adapter
   untouched.
3. **No-solder equivalent:** a "USB-C OTG adapter with power port" (TV-stick
   accessory: C-male + A-female + power inlet) fed by a short Grove-to-USB
   power pigtail. Two small pieces, same topology.
4. **Smallest-battery variant (researched 2026-08):** pick an OTG adapter whose
   power inlet is a FEMALE USB-C, and dock a plug-style mini bank straight into
   it -- the bank and adapter become one rigid pod on the Stick, zero cables.
   The load is only the radio's USB PHY (the Stick stays Grove-powered), likely
   20-100 mA, which is BELOW many banks' auto-shutoff threshold -- so the one
   spec that matters is a documented **low-current / trickle mode**:
   * **iWALK LinkPod family** (lipstick, ~4,800-5,500 mAh, ~100 g, male USB-C
     plug): LinkPod X and LinkPod Style document Low Current Mode explicitly
     (double-click the button, LED flashes); the LinkPod 5 manual confirms the
     default behaviour is auto-off after 30 s below threshold, which is exactly
     what the mode bypasses. Best fit found.
   * **Rolling Square TAU 2** (2,000 mAh keychain, integrated C cable) is the
     absolute smallest but documents no low-current mode -- a gamble.
   * Anker's trickle mode exists on select models only; verify per model.
   Field check for ANY candidate: enable low-current mode, connect, and confirm
   the Stick's `attach`/`usb rx` stays alive for 10+ minutes -- and once
   through a full pass, since some banks exit trickle mode on a timer. Runtime
   is a non-issue: at this load even 2,000 mAh is a full day of passes.

**Verify the budget once with a meter:** the Cardputer's Grove 5 V then carries
the Stick (~100–250 mA typical) plus the radio's USB interface (small — the
IC-705 and TH-D75 are self-powered; VBUS only wakes their USB PHYs). If the
Cardputer's Grove budget cannot carry both, keep the same loom but feed its 5 V
from the one power bank already powering the Cardputer — still a single battery
for the whole station.

## Debugging

`tools/helper_probe.py` (in the CardSat repo) speaks the protocol from a Mac or PC
over a 3.3 V USB-serial adapter on the Grove pins — **with CardSat out of the
loop**, which is the fastest way to separate a firmware problem from a wiring one:

```
./helper_probe.py --port /dev/cu.usbserial-XXXX            # auto-baud, then status
./helper_probe.py --port ... --enum                        # what the Stick can see
./helper_probe.py --port ... --open '' --civ A4            # open + CI-V read-freq
./helper_probe.py --port ... --stats                       # error counters
./helper_probe.py --port ... --monitor                     # dump every frame
```

The serial console on the Stick is **not** a debugging route: USB host takes the
S3's one internal USB PHY, which is the same PHY the HWCDC console sits behind.
Console output is compiled out by default (`CSUH_DEBUG 0`) for that reason.

## Status

Compiles clean, zero warnings. The link layer is verified host-side, including the
shipped CardSat client run against a mock helper. **Not yet run on a real
M5StickS3 with a radio attached.** Bench findings welcome.

## Protocol

`../../docs/interfaces/CSUH_PROTOCOL.md`, and `csuh_proto.h` — which is shared
byte-identically with CardSat's `src/csuh_proto.h`. If you edit one, copy it to the
other; `tools/check_csuh_parity.py` will tell you if you forgot.
