# Upstream bug report — ready to file

File at <https://github.com/tanakamasayuki/EspUsbHost/issues>. Everything below the
line is the report; paste it as-is.

---

**Title:** CDC-ACM: control interface is latched from the *last* ACM function, so
line coding and DTR/RTS go to the wrong interface on dual-CDC devices

### Summary

`EspUsbHost::...` latches the CDC-ACM **control** interface without checking whether
one has already been found, while the matching **data** interface latch *is*
guarded. On a device that exposes two CDC-ACM functions the two end up on different
functions: data endpoints come from function 0, but `cdcControlInterfaceNumber` holds
the last control interface parsed. Since `SET_LINE_CODING` and
`SET_CONTROL_LINE_STATE` both address `cdcControlInterfaceNumber`, the baud rate and
DTR/RTS are applied to a function whose endpoints are never used.

The visible result is a device that enumerates cleanly, reports `connected()`, and
then never communicates — because the port that carries the data was never given its
line coding and never had DTR asserted.

### Version

2.5.2 (also present in 2.3.x). ESP32-S3, arduino-esp32 3.2.1 (IDF 5.4).

### Where

`src/EspUsbHost.cpp`, in the interface descriptor walk:

```cpp
const bool isCdcAcmControlInterface = currentInterfaceClass_ == USB_CLASS_CDC_CONTROL_VALUE &&
                                      currentInterfaceSubClass_ == USB_CDC_SUBCLASS_ACM;

const bool isCdcAcmDataInterface = currentInterfaceClass_ == USB_CLASS_CDC_DATA_VALUE &&
                                   device->hasCdcControlInterface &&
                                   !device->hasCdcDataInterface;   // <-- guarded
```

and then:

```cpp
if (isCdcAcmControlInterface)
{
  device->hasCdcControlInterface = true;
  device->cdcControlInterfaceNumber = currentInterfaceNumber_;   // overwritten each time
  configureCdcAcm(*device);
}
else if (isCdcAcmDataInterface)
{
  device->hasCdcDataInterface = true;
  device->cdcDataInterfaceNumber = currentInterfaceNumber_;      // first one wins
}
```

Both control transfers use the overwritten value:

* `SET_LINE_CODING` — `setup->wIndex = device.cdcControlInterfaceNumber;`
* `SET_CONTROL_LINE_STATE` — `setup->wIndex = device.cdcControlInterfaceNumber;`

For comparison, the keyboard latch a few lines above *does* guard itself:

```cpp
if (... && !device->hasKeyboardInterface)
{
  device->hasKeyboardInterface = true;
  ...
}
```

which is why this looks like an oversight rather than a deliberate "last wins".

### Steps to reproduce

1. Attach a USB device exposing **two** CDC-ACM functions — many dual-port USB-serial
   bridges, composite dev boards and instruments behave this way. (Note: a device with
   a single CDC-ACM function is unaffected; the bug needs two.)
2. Bind it with `EspUsbHostCdcSerial`, `begin(baud)`, `setDtr(true)`, `setRts(true)`.
3. `connected()` returns true and the enumeration log looks correct.
4. Nothing is received, and transmitted bytes have no effect.

With `CORE_DEBUG_LEVEL` at info the log shows the asymmetry directly — two
`CDC control interface ready: iface=N` lines with different numbers, and a single
`CDC data interface ready: iface=M` from the first function.

### Suggested minimal fix

```diff
 const bool isCdcAcmControlInterface = currentInterfaceClass_ == USB_CLASS_CDC_CONTROL_VALUE &&
-                                      currentInterfaceSubClass_ == USB_CDC_SUBCLASS_ACM;
+                                      currentInterfaceSubClass_ == USB_CDC_SUBCLASS_ACM &&
+                                      !device->hasCdcControlInterface;
```

This makes the control latch consistent with the data latch and with the keyboard
latch: the first CDC-ACM function wins, whole.

### The larger request

The minimal fix makes the library **coherent**, but it always selects function 0. On
a dual-CDC device the function the application wants is not always the first — on
some radios the CAT port is the second function and the first is a TNC or GPS
stream. Would you consider exposing the CDC function/interface choice, e.g.:

* `EspUsbHostCdcSerial::setInterface(uint8_t cdcFunctionIndex)` (default 0), or
* reporting the discovered CDC functions through `EspUsbHostDeviceInfo` so the
  application can pick before binding?

That would let a host application talk to either port on these devices instead of
being limited to whichever one appears first.

Happy to test any patch against real hardware (ESP32-S3 USB host, several USB-serial
bridges and a dual-CDC handheld radio) and to open a PR for the one-line fix if that
is useful.
