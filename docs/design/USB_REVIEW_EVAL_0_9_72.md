# Evaluation — 0.9.72 USB reliability recommendations

*Each recommendation checked against the 0.9.73 source rather than accepted on
trust, then re-checked against `companion/CardSatUsbHelper` under the standing rule
that a USB fix in CardSat probably needs applying to the helper too.*

**Overall: a good review.** Every high-priority finding is real, and two of them are
load-bearing. One is overstated in a way worth correcting. The most useful thing to
come out of checking it, though, is not on its list at all — see §0.

---

## 0. The finding the review could not have made

**The helper links Arduino's prebuilt `libusb.a`, not the vendored USB host stack.**

```
CardSat.ino    #include <UsbHostSrc.h>   ->  map: 1269 UsbHostSrc refs, 0 libusb.a
CardSatUsbHelper                          ->  map:    0 UsbHostSrc refs, 1263 libusb.a
```

The helper therefore has **none** of 0.9.72's USB host work: no enumeration-stage
retry, no reset-hold/recovery timing, no `CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK`,
no `ESP_LOGD` narration. It runs stock IDF.

That is backwards. The whole reason the helper exists is to host the device the
Cardputer cannot — the IC-705, with its internal hub — and it is doing so with the
*weaker* of the two USB stacks available. Every hub-path enumeration problem
0.9.72 chased will be re-encountered there, with no diagnostics.

The review's §6 endorses the helper architecture, which is right, but it treats the
helper as future work. It is shipping now, and it needs the vendoring.

**Action:** `tools/vendor_usb_host.sh` already installs `UsbHostSrc` as a library;
the helper only needs `#include <UsbHostSrc.h>` before `<Arduino.h>` and the same
`build_opt.h` flags. Not free — the sketch grows and the build gains the same
"delete the cache or it silently does nothing" trap — but it is a small change
against a large asymmetry.

---

## 1. High-priority findings

### 1.1 Do not delete the host object after a shutdown timeout — **CONFIRMED, fix first**

Verified in the library, not inferred:

```
EspUsbHost.cpp:1766
  ESP_LOGW(TAG, "USB Host shutdown timed out; tasks were left alive to avoid
                 freeing in-flight transfers");
  setLastError(ESP_ERR_TIMEOUT);
```

So a surviving task holds `this`. CardSat has **three** teardown sites and they do
not agree:

| site | on `ESP_ERR_TIMEOUT` |
| --- | --- |
| `releaseHostNow()` ~line 650 | retains, no `consoleUp()` — **correct** |
| `end()` ~line 1122 | retains, no `consoleUp()` — **correct** |
| `begin()` failure path ~line 900 | `delete s_cdc; delete s_host; consoleUp();` — **wrong** |

The review is right, and the inconsistency between three copies of the same
sequence is itself the finding: two were fixed and one was missed.

One correction to the review. It says the guard is absent; it is not.
`s_hostReleased = freed` is set, and `begin()` (line 767) and
`hostUpForRotator()` (line 1248) both check it. But that guard is **one-shot** —
line 769 "spends the latch on this attempt" — so the next engage allocates a *new*
`EspUsbHost` while the old object's tasks may still be running against freed
memory. The latch limits how often the hazard is reached; it does not remove it.

The `consoleUp()` half is arguably worse than the delete: it calls
`Serial.begin()`, reclaiming the USB PHY, while the IDF host stack may still own
it. That is a two-owner PHY, and the symptom would be a wedge that looks like
anything but a teardown bug.

**Helper:** immune, and by design rather than luck. It never calls `gUsb.end()` —
`CSUH_T_RESCAN` reboots instead, precisely because a reboot is the one recovery
that cannot itself get stuck. Nothing to port.

### 1.2 Bound serial OUT transfers — **CONFIRMED, and the helper is worse**

`EspUsbHost::sendSerial()` (line 3890) allocates a `usb_transfer_t` per call,
submits it, and frees it only in the completion callback. Nothing counts what is
outstanding.

The review frames this as a CardSat risk. It is a bigger helper risk:

* CardSat writes CAT at the Doppler rate — a handful of writes per second, floored
  by `CAT_BYTES_PER_UPDATE`. Accumulation against a stalled radio is slow.
* The helper's `serviceLinkToUsb()` drains its ring in a `while` loop, calling
  `sendSerial()` as fast as bytes arrive. `sendSerial()` returns true when the
  *submit* succeeds, which it does even if the device never drains it — so the ring
  empties, `grantCredit()` opens the window again, and the host is invited to send
  more. **The credit scheme bounds the link and does not bound the transfers.**

That is a hole in my own design and I would not have found it from the CSUH side:
credit was reasoned about as link back-pressure, and the USB side was assumed to
apply its own. It does not.

**Fix, both sides:** one preallocated OUT transfer per device with a pending flag,
`sendSerial()` returning false while busy, and a submission timestamp so an overdue
transfer is treated as a stalled endpoint rather than a slow one. On the helper,
`false` should stop the drain loop and leave the bytes in the ring — which then
correctly withholds credit, and the back-pressure chain finally runs end to end.

The review's "keep the newest, drop the backlog" point is right for CAT frequency
updates and wrong for the helper, which carries opaque bytes and must not
selectively discard them. Newest-wins is a CardSat-layer policy, not a transport
one.

### 1.3 Propagate physical disconnects — **CONFIRMED for CardSat; the helper already does it**

`onGone()` (line 574) sets `s_serDev[i].dead = 1` and nothing else. It does not
touch `s_active`, `s_bound`, `s_cat2Active` or `s_rotActive`. And:

```cpp
bool active()     { return s_active && s_bound; }        // line 1151
bool cat2Active() { return s_cat2Active && s_cdc2; }     // line 1391
bool rotActive()  { return s_rotActive && s_rotCdc; }    // line 1573
```

None consults connectivity, although `EspUsbHostCdcSerial::connected()` exists
(`EspUsbHost.h:1880`) and is never called. The loop reconciler gates on
`catUsesUsb() && radioOut && rig` versus `UsbSerial::active()`, so a stale-true
`active()` means the reconciler sees nothing to do and a replug never rebinds.

**This is the most valuable operational fix on the list** and I agree with the
review's ranking of it.

Worth noting: **the helper path already behaves the way the review wants.**
`onUsbDisconnected()` clears the port and raises `CSUH_EV_PORTLOST`;
`servicePortHealth()` drops a stale binding after 4 s and re-finds the device by
key; CardSat's client clears `s_open` on `PORTLOST` and re-issues the `OPEN` on a
1.2 s retry. The newer code got this right and the older local-USB path is what is
behind — so the port here is CSUH → usbserial, not the other way round.

### 1.4 Only serial-capable devices in the picker — **CONFIRMED, and cheaper than proposed**

`onDev()` excludes hubs and nothing else. Anything that enumerates lands in the
selectable adapter list.

The review proposes adding a `serialCapable` field to the library's device-info
struct. **That is not necessary.** `EspUsbHost::serialReady(address)` is already
public and resolves to `findSerialDevice()`, which tests `hasSerialOutEndpoint`
(line 9778) — exactly the property wanted. Filtering at the picker (`scanAdapters`
/ `serialDeviceLive`) rather than at insert also sidesteps a question I could not
settle by reading: whether the OUT endpoint is recorded before
`dispatchDeviceConnected()` fires. At picker time, enumeration has settled and the
answer is definitive.

**Open question for the bench, not for the code.** The review's motivating example
is the IC-705's audio CODEC appearing as a selectable adapter. Whether it does
depends on something the handoff does not settle: the channel arithmetic says
IC-705 = 5 = internal hub (2) + CDC (3), which implies the CODEC is an *unclaimed
interface* on the CDC device and never raises its own connect callback. If instead
it enumerates as its own device, then it costs a sixth channel *and* — more
seriously — it breaks the helper's auto-select, because `findDevice("")` requires
exactly one non-hub device and would return `CSUH_ERR_AMBIG` for a helper carrying
nothing but an IC-705. That is the flagship use case failing on the default
setting.

One command settles it:

```
tools/helper_probe.py --port ... --enum
```

If two entries appear for one radio, 1.4 is urgent on both sides and the helper's
`findDevice()` needs the same `serialReady()` filter.

---

## 2. Secondary items

### 2.1 Registry synchronisation — **CONFIRMED, low severity, cheap fix**

`src/usbserial.cpp` has **3 release fences and 0 acquire fences**. The writer
publishes correctly; the reader has `volatile` and nothing else. The review is
formally right that this is not a complete acquire/release pair.

Severity is lower than presented: the ESP32-S3's two cores are cache-coherent for
internal SRAM, so the realistic exposure is compiler reordering on the read side,
not hardware visibility. It has never been observed. But the fix is three lines and
the failure it prevents — reading a count before the record it refers to — would
present as a phantom adapter, which is exactly the class of symptom nobody traces
back to a fence.

**Helper:** already correct. Its registry uses `portMUX` plus snapshot-before-read,
and its rings use `std::atomic` with explicit acquire/release. Built that way
deliberately; the reasoning is in the source. Again the port direction is
helper → CardSat.

### 2.2 Per-attempt enumeration retry state — **CONFIRMED, narrow**

`UsbHostSrc/src/enum.c:1107` uses function-static `s_retry_stage` /
`s_retry_count`, reset only when the *stage* changes. Two devices failing at the
same stage in sequence share a budget. Real, but the window is narrow and the
consequence is one fewer retry, not corruption.

The review's more interesting point is buried: retrying a *state-changing* request
(address assignment, `SET_CONFIGURATION`) by stepping back one stage is not
obviously safe, and restarting from port reset would be. That deserves a decision
on its own merits, separately from where the counter lives.

**Helper:** not applicable until §0 is fixed — it does not have this patch at all.

### 2.3 Downstream-port reset-poll backoff — **already on the books**

Carried from the 0.9.72 memo §4 and into `SNAPSHOT.md` as an open item. The
review's contribution is the test matrix, which is worth having: baseline / 2 ms /
5 ms / 5 ms + 50 ms / 5 ms + 100 ms, 50–100 cold cycles each.

Its most important sentence is the methodological one — **re-test the FT232R under
unmodified 0.9.72 first.** The memo already flags that late-enumeration watching may
have covered cases that previously looked like hard failures, so measuring the
baseline before changing anything is the difference between a fix and a
coincidence.

### 2.4 Batch CDC configuration — **CONFIRMED, minor as efficiency, real as correctness**

The `setConfig` / `setDtr` / `setRts` triple appears at four sites (CAT-A twice,
CAT-B, rotator), so three EP0 control transfers where two would do. Minor.

The second half of the recommendation matters more: marking the device configured
even when a submission fails means the firmware believes a line rate it never
successfully set. That is a silent-wrong, which by this project's standards
outranks the transfer count.

**Helper:** same pattern in `handleOpen()` — `applyLineCoding()` then
`setControlLines()`, and `applyLineCoding()`'s return is discarded. Should be
checked and reported through `CSUH_ERR_*` rather than reporting a successful
`OPENED`.

---

## 3. Diagnostics and process

### 3.1 Channel-exhaustion diagnostics — **agree, high value for the effort**

Nothing currently distinguishes "out of channels" from "allocation failed" or
"transfer error", and the 0.9.72 cycle burned bench time on exactly that confusion
(the FIFO theory). A claim failure that logged address, VID:PID, interface,
endpoint, open-pipe count and largest free block would have shortened it.

The user-facing half matters as much: an operator who plugs in hub + TH-D75 +
IC-705 should be told the combination cannot work on one controller — and, now,
that the helper is the answer.

### 3.2 Consolidate CAT-A / CAT-B / rotator binding — **agree, and §1.1 is the proof**

Three near-identical sequences, one of which was missed when the other two were
fixed. That is not a hypothetical maintenance argument; it is this review's
top finding. Worth doing *after* 1.1–1.3, so the consolidation captures the
corrected behaviour rather than enshrining three variants of the old one.

### 3.3 Automated release verification — **agree, and I have the list**

I performed these by hand for 0.9.73 and every one of them is scriptable:

| check | 0.9.73 result |
| --- | --- |
| `UsbHostSrc` objects in the map | 1269 |
| `libusb.a(` contribution zero | 0 |
| partition table = repo's `partitions.csv` | 4 MB app0 / 1.5 MB spiffs / coredump ✓ |
| zipped `.ino` md5 == compiled `.ino` md5 | `25f59bf8…` both |
| binary md5s == the values in the READMEs | ✓ |

Two more belong on the list that the review does not name: **the helper's map must
show the same `UsbHostSrc` result** once §0 is fixed, and `check_csuh_parity.py`
must pass so the two protocol headers cannot drift.

---

## 4. Where I disagree

* **1.1's "the guard is missing".** It is not; `s_hostReleased` exists and is
  checked. The defect is the delete and the `consoleUp()`, not the absence of a
  latch. Worth stating precisely so the fix does not get misdirected into adding a
  flag that is already there.
* **1.4's library change.** Unnecessary; `serialReady()` already exposes the
  needed property.
* **2.1's severity.** Formally correct, practically low on a cache-coherent
  ESP32-S3. Fix it because it is cheap, not because it is suspected in any current
  symptom.
* **The review's ordering.** It puts registry filtering (1.4) fourth and
  synchronisation (2.1) fifth. I would put filtering earlier if the bench confirms
  the IC-705 raises two devices, because that would make the helper's default
  configuration fail, and demote 2.1 to whenever the file is next open.

---

## 5. Recommended order for 0.9.74

1. **Vendor the USB host stack into the helper** (§0). Everything else in the
   helper is built on sand until this is done.
2. **Fix the `begin()`-failure teardown path** (1.1) — retain, no `consoleUp()`,
   latch it. Three lines, and it is a live use-after-free.
3. **Bound serial OUT transfers** (1.2), both sides, with the helper's drain loop
   honouring the busy return so back-pressure runs end to end.
4. **Disconnect → main loop → rebind** (1.3), porting the shape the CSUH path
   already uses.
5. **`serialReady()` filter on both pickers** (1.4) — promote to (2) if
   `helper_probe.py --enum` shows two devices for one IC-705.
6. **Check `applyLineCoding()`'s return** (2.4, correctness half only).
7. **Channel-exhaustion diagnostics** (3.1).
8. **Acquire fences** (2.1) and **per-attempt retry state** (2.2).
9. **Consolidate the three binding paths** (3.2), capturing 2–5.
10. **Script the release checks** (3.3).
11. **The `ext_port.c` backoff experiment** (2.3) — *after* re-baselining the
    FT232R on unmodified 0.9.72.

Items 1–5 are the release. The rest is hygiene that can follow.

## 6. The standing rule, restated

Three of the four high-priority findings needed porting in a direction the review
did not anticipate:

| finding | direction |
| --- | --- |
| 1.1 host delete | CardSat only — the helper's stateless reboot design sidesteps it |
| 1.2 OUT transfers | **both**, and the helper is more exposed |
| 1.3 disconnects | **helper → CardSat** — the newer code is the correct one |
| 2.1 synchronisation | **helper → CardSat**, same reason |

So the rule holds, but it is not one-way. The helper was written after these
lessons were learned and encodes some of them; the local USB path predates them.
Any USB change should be checked against *both* implementations, and whichever is
already right is the template.
