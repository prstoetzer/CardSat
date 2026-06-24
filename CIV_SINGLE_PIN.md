# CardSat — Single-Pin CI-V Wiring (Icom only)

CardSat can drive an Icom transceiver's CI-V bus over a **single shared GPIO pin**
instead of the usual separate transmit and receive wires. This matches the way CI-V
actually works electrically — it is a one-wire, half-duplex bus — and it halves the
number of signal wires to the radio.

> ⚠️ **This mode is UNVERIFIED.** The normal separate **TX/RX** path is the
> recommended, more reliable option and is what the author runs on the bench. Single-pin
> mode is provided for experimentation. It depends on open-drain pin behavior and an
> external pull-up that this firmware cannot guarantee, and it has not been confirmed on
> hardware. **Use the separate TX/RX path unless you specifically want to try this and
> are comfortable verifying the wiring with a meter.** Wrong wiring can damage a GPIO.

This applies to **wired Icom CI-V only.** It has no effect on Yaesu, Kenwood, or the
IC-9700 RS-BA1 LAN backend.

---

## Choosing the mode

**Settings → Radio → "CI-V wiring"** cycles between three options (left/right to
change):

| Setting | Meaning |
|---|---|
| **TX/RX (G2/G1)** | *(default, recommended)* Separate wires — **G2 = TX**, **G1 = RX**. The normal path. |
| **1-pin G2** | One shared open-drain wire on **G2**; G1 is unused. |
| **1-pin G1** | One shared open-drain wire on **G1**; G2 is unused. |

Changing the setting re-initializes the CI-V port immediately. The choice is saved and
restored across reboots.

### What the firmware does in single-pin mode

It routes the UART's transmit and receive to the **same** GPIO, and re-configures that
pin as **open-drain with a weak internal pull-up**. Open-drain means CardSat can only
pull the line **low**; it never drives it high. The line is returned to its idle (high)
state by a pull-up — primarily the radio's own internal CI-V pull-up (and any pull-up in
a level-shifter you add). Because everything is on one wire, CardSat hears its own
transmission echoed back; the CI-V protocol layer already expects and discards that
echo, exactly as on a real multi-device CI-V bus.

---

## ⚠️ Electrical safety — read before wiring

The Cardputer's GPIOs (**G1/GPIO1, G2/GPIO2**) are **3.3 V and NOT 5 V tolerant.** Most
Icom CI-V buses idle near **5 V.** Connecting a 5 V CI-V line to one of these pins —
even through this single-pin mode — **can destroy the GPIO.** The firmware setting does
**not** make the pin 5 V tolerant; it only changes how the pin is driven.

**The Grove port's power pin is 5 V**, not 3.3 V — do not power 3.3 V external circuitry
from it without a regulator.

### Required procedure

1. **Measure the radio's CI-V idle voltage first.** With the radio on and nothing else
   connected, meter the CI-V jack (tip to sleeve). This single reading decides the
   circuit.
2. **If it reads ~5 V (assume so unless proven otherwise):** you **must** use a
   level shifter between the GPIO and the radio. The clean choice for a single wire is a
   **passive, bidirectional open-drain (BSS138-style) level-shifter** — open-drain on
   both sides is exactly what a one-wire CI-V bus wants. Do **not** connect the GPIO to
   the bus directly.
3. **If it reads ~3.3 V or below:** a near-direct connection is possible (shared pin to
   tip, ground to sleeve), but keep a small series resistor (a few hundred ohms) as
   insurance.
4. **Provide a pull-up.** Open-drain only pulls low. The radio's internal CI-V pull-up
   normally suffices; a level-shifter typically adds its own. Confirm the idle line
   actually sits high once wired.
5. **Common ground.** Cardputer ground, any level-shifter ground, and the radio's CI-V
   sleeve must all be the same node.
6. **The non-negotiable check, BEFORE trusting the GPIO:** with everything wired and the
   radio on but idle, meter the **low-voltage node that connects to the Cardputer pin**
   and confirm it never exceeds ~3.3 V. If it ever approaches 5 V, **disconnect
   immediately** — the level translation is not working and connecting the pin would
   damage it.

### Radio-side settings

In the radio's CI-V menu: set the **CI-V address** and **baud** to match CardSat
(Settings → Radio), and turn **CI-V Transceive OFF** (unsolicited Transceive frames
complicate the shared-wire timing). A mismatch here looks identical to a wiring fault.

---

## Verifying it works

1. Set **CI-V wiring** to your chosen single-pin option.
2. Raise **CAT Rate** (Settings) to about **1 second** so each transaction is slow
   enough to watch.
3. Engage radio control in **One True Rule (full-knob)** mode, which polls the radio
   every cycle. On the shared wire you should see the command go out, the echo return,
   and the radio's reply follow — all on the one pin.
4. If nothing is received: re-check the idle voltage and pull-up, confirm the radio's
   address/baud/Transceive settings, and confirm the level shifter (if any) is passing
   the bus bidirectionally. If single-pin will not behave, **switch back to TX/RX** —
   that path is more forgiving and is the recommended configuration.

---

## Why the separate path is more reliable

With separate TX and RX wires, the transmit pin is a normal push-pull output and the
receive pin a normal input — there is no dependence on open-drain pad behavior or an
external pull-up holding the line high, and no echo sharing a single conductor. Single-
pin mode is elegant and true to CI-V's nature, but it concentrates everything onto one
pin and one pull-up, so any wiring or level-translation mistake has less margin. For a
dependable station, prefer **TX/RX (G2/G1)**.

---

*All CI-V interfacing on the Cardputer is at your own risk. The GPIOs are not 5 V
tolerant; verify voltages with a meter before connecting a radio. This single-pin mode
in particular is unverified on hardware.*
