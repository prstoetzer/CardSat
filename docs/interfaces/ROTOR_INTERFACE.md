# Directly interfacing a Yaesu az/el rotator to CardSat

A buildable guide to wiring a **Yaesu G‑5500 / G‑5400 / G‑5600** (and similar)
azimuth‑elevation controller **straight to CardSat** — no GS‑232 box in between.
CardSat reads the controller's two position‑feedback voltages and drives its four
direction lines, closing the pointing loop itself. This is the **"Yaesu (direct)"**
rotator backend (`ROT_YAESU`).

> ## ⚠️ UNTESTED — BUILD AND USE AT YOUR OWN RISK
> **None of this has been verified against a physical rotator.** It is a paper
> design derived from the controller's published behavior and the CardSat
> firmware. You are connecting low‑voltage electronics to a mains‑powered
> controller that drives heavy motors. Mistakes can destroy the ADC, the Cardputer,
> or the rotator controller, drive an antenna into its mechanical stops, or worse.
> **Proceed entirely at your own risk. The author accepts no liability for any
> damage to equipment, antennas, property, or anything else arising from following
> this document.** Verify every pin and voltage against *your* controller's manual
> and bench‑test with the motors disconnected before trusting it.

> ## Scope: direct connection only
> This guide covers **only** the direct‑to‑rotator wiring. It deliberately does
> **not** cover putting a GS‑232 box *and* a directly‑wired rotator on the I²C bus
> at the same time. The GS‑232 backend and the direct backend are **mutually
> exclusive** — pick one. Build the direct interface on a bus that does **not** also
> carry the GS‑232 I²C→UART bridge.

---

## 1. What CardSat does in this mode

In `ROT_YAESU` mode CardSat runs the closed loop itself, about ten times a second:

1. read the **azimuth** and **elevation** feedback voltages through an I²C ADC;
2. convert counts → degrees using a stored per‑axis **calibration**;
3. compare to the commanded target and, if outside the **deadband**, energise the
   correct **direction line** (CW/CCW/Up/Down) through an I²C output expander;
4. stop the axis once it is inside the deadband.

It also enforces **soft limits** (never drive past the calibrated travel) and a
**stall watchdog** (if it commands motion but the feedback isn't changing, it cuts
the outputs). These are safety nets, **not** a substitute for bench testing.

## 2. The I²C bus

Both modules sit on **Wire1**, the same expansion I²C bus the GS‑232 bridge would
use (Cardputer **G8 = SDA, G9 = SCL**, e.g. the Cap LoRa Port.A header), at 3.3 V
logic. Power the ADS1115 / PCF8574 at **3.3 V** so the I²C lines match the ESP32's
levels. (The Cardputer's **Grove** power pin, by contrast, is **5 V**, not 3.3 V —
if any part of your build taps Grove power, drop it to 3.3 V with an LDO and never
feed 5 V to the ESP32's I/O or beyond the ADC's input range.)
Default device addresses (must not collide; they don't, in this build):

| Device | Role | Default address |
| --- | --- | --- |
| ADS1115 (16‑bit I²C ADC) | reads az + el feedback voltages | **0x48** (ADDR→GND) |
| PCF8574 (8‑bit I²C output) | drives 4 direction lines | **0x20** (A2..A0→GND) |

These match the firmware defaults (`YAESU_ADC_ADDR`, `YAESU_OUT_ADDR` in
`config.h`). If you change a module's address straps, change the defines to match.

## 3. Modules you need

- **ADS1115 breakout** — a 4‑channel, 16‑bit I²C ADC. We use **AIN0 = azimuth**,
  **AIN1 = elevation**. (The Cardputer's own ADC is noisy; a dedicated ADS1115 is
  worth it.)
- **PCF8574 breakout** — an 8‑bit I²C output expander. We use the low 4 bits for the
  direction lines.
- **A 4‑channel relay board** driven by the PCF8574 (recommended over transistors —
  see §5). Opto‑isolated relay boards are preferred.
- A couple of **resistor dividers** for the feedback voltages (see §4), and a length
  of shielded multi‑core cable to the controller's **8‑pin DIN External Control**
  jack.

## 4. Reading the position feedback (the analog side)

The G‑5500‑class controller's External Control jack provides, among other pins,
**GND**, a **+13 V** reference, an **azimuth feedback voltage**, and an **elevation
feedback voltage**. Each feedback voltage rises from ≈ **0 V at the 0° end** toward
a maximum at full travel; the controller has internal **OUT‑VOL adjustment screws**
that set that maximum.

> **Confirm the exact DIN pin numbers against your controller's manual.** There are
> AC and DC versions of the G‑5500, and the G‑5400/G‑5600 differ. Do not assume a
> pin table from the internet — measure.

Keep the ADC inputs in range:

- The firmware reads the ADS1115 at PGA **±4.096 V**. The feedback at full travel
  must stay **below 4.096 V** — and must **never exceed the ADS1115's supply +0.3 V**.
- Best practice: **trim the controller's OUT‑VOL screws** so full travel reads a
  known value (e.g. ~3.0 V) **and** add a modest **resistor divider** as insurance.
  A divider also lets you scale a higher full‑scale down into range.
- Tie the ADS1115 **ground to the controller's feedback ground** so the voltages share
  a reference. Do **not** route the **+13 V** rail into the Cardputer or the ADC.

Wire azimuth feedback → **AIN0**, elevation feedback → **AIN1**.

## 5. Driving the direction lines (the output side)

The controller's four directions (LEFT/RIGHT/UP/DOWN) are normally activated by the
front‑panel momentary switches. The cleanest, most controller‑agnostic way to drive
them from CardSat is to **parallel those switch contacts with relays**:

- A **dry relay contact** simply closes the same connection the panel switch closes.
  It doesn't care about the control‑line voltage or polarity, which is exactly why
  relays are safer here than open‑collector/opto outputs that assume a direction and
  level.
- **Measure across each panel switch** (controller off) to learn which two points
  each switch bridges, and wire each relay across the corresponding pair.

PCF8574 bit → relay → direction, matching the firmware (`YAESU_BIT_*` in `config.h`):

| PCF8574 bit | Relay drives | Firmware constant |
| --- | --- | --- |
| 0 | Azimuth **CW** (Right) | `YAESU_BIT_CW` |
| 1 | Azimuth **CCW** (Left) | `YAESU_BIT_CCW` |
| 2 | Elevation **Up** | `YAESU_BIT_UP` |
| 3 | Elevation **Down** | `YAESU_BIT_DOWN` |

The firmware treats the outputs as **active‑low** (`YAESU_OUT_ACTIVE_LOW = true`),
which matches typical low‑trigger relay boards (a `0` on the PCF8574 pin energises the
relay). On power‑up the PCF8574 pins float high → all relays off → **motors off**,
which is the safe default. If your relay board is active‑high, flip the define.

Power the relay coils from their own 5 V (an opto‑isolated board keeps that supply and
the controller side isolated from the Cardputer). **Never** let the motor/controller
side back‑feed the Cardputer's 3.3 V.

## 6. Configure and calibrate

1. **Settings → Rotator → Rotator: on.**
2. **Rot type →** cycle to **"Yaesu (direct)"**.
3. **Rot az range →** set **0..450** for a G‑5500 (it has 450° of azimuth travel).
4. **Settings → Rotator → Rotator: manual control.** With the Yaesu backend active
   this screen shows the **live ADC counts** for each axis and offers four capture
   keys. Using the **controller's own front‑panel buttons**, drive to each mechanical
   endpoint and capture:

   | Drive the axis to… | Press | Stores |
   | --- | --- | --- |
   | azimuth fully CCW (0°) | **1** | az‑zero counts |
   | azimuth fully CW (full scale, 450°) | **2** | az‑full counts |
   | elevation fully down (0°) | **3** | el‑zero counts |
   | elevation fully up (180°) | **4** | el‑full counts |

   CardSat then maps counts → degrees linearly and rebuilds the backend so the new
   calibration takes effect immediately. The four values persist across reboots.

## 7. Bench‑test BEFORE connecting motors

This cannot be overstated given the design is untested:

- **Simulate the feedback** with two trim‑pots (wiper 0 → ~3 V) into AIN0/AIN1, and put
  **LEDs (or a meter) on the relay outputs** instead of the controller. Confirm that:
  commanding a higher azimuth lights **CW**, lower lights **CCW**, higher elevation
  lights **Up**, lower lights **Down**; that motion stops inside the deadband; that the
  **soft limits** hold at the calibrated ends; and that the **stall watchdog** cuts the
  output when the simulated feedback stops changing.
- Only once the logic is proven, connect to the controller with the **rotator motors
  unplugged** first, verify the right relay closes the right switch contact, then —
  understanding the risk — connect the motors.

---

*Reminder: every connection in this document is **untested** and provided **as‑is**.
You assume all risk; the author is **not liable** for any resulting damage.*
