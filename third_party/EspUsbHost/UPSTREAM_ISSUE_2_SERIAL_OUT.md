# Upstream bug report — ready to file

File at <https://github.com/tanakamasayuki/EspUsbHost/issues>. Everything below the
line is the report; paste it as-is.

---

**Title:** Shutdown never drains the CDC serial OUT endpoint, so a device that stops
reading strands the USB host until reboot

### Summary

`drainClientTransfers()` halts and flushes the **audio OUT** and **vendor OUT**
endpoints by address before shutdown, but not the **CDC serial OUT** endpoint.

`sendSerial()` allocates a transfer with `usb_host_transfer_alloc()`, submits it, and
does not record it anywhere — it is not in `endpoints_`, so nothing that iterates that
table can see it. If the peer stops reading its port (a device switched off, or one
whose application is not draining its endpoint FIFO), the submitted transfer stays
enqueued in the pipe indefinitely.

`end()` then fails in a chain that is hard to diagnose from the outside:

1. `usb_host_interface_release()` refuses the interface owning that endpoint with
   `ESP_ERR_INVALID_STATE` ("interface currently can not be freed").
2. `releaseClientResources()` therefore never closes the device.
3. `usb_host_client_deregister()` returns `ESP_ERR_INVALID_STATE` (259), because the
   client still has a device open.
4. The client stays registered, so `usb_host_uninstall()` is never reached and the host
   stays installed.
5. Every subsequent `usb_host_install()` returns 259 for the rest of the boot.

From the application's point of view the USB stack is simply gone until a power cycle,
with no indication of why.

### Version

2.7.0 (also present in 2.5.2). ESP32-S3, arduino-esp32 3.2.1 (IDF 5.4).

### Reproduction

Any CDC-ACM device that stops servicing its bulk OUT endpoint while a write is
outstanding. We hit it with a Kenwood TH-D75 handheld: after one CAT session the
radio's application stops reading, its endpoint FIFO accepts exactly two more packets
(double buffered) and NAKs the rest.

1. `begin()`, bind a CDC serial device, write to it successfully.
2. Get the device into a state where it stops reading (switch it off, or use a device
   whose firmware does not restart its serial application after re-enumeration).
3. Write a few times — the first one or two transfers complete, the rest queue.
4. `end()`.
5. Observe `usb_host_interface_release()` → 259 for the data interface, and the host
   left installed.

### Instrumented evidence

Endpoint 0x01 is the CDC data OUT endpoint; interface 1 is the CDC data interface.
Interface 0 (CDC control) releases cleanly on every run — only the interface owning the
stuck OUT endpoint fails:

```
-> OUT xfer #7  ep=0x01 status=0 bytes=7      completed
-> OUT xfer #8  ep=0x01 status=0 bytes=7      completed
   (four further writes submitted, none complete)
...end()...
-> OUT xfer #9..#12 ep=0x01 status=3 bytes=0  CANCELED only by teardown
iface_release 1 -> 259
device_close   -> 259
client_deregister -> 259
```

For contrast, when the same device is reading normally every OUT transfer completes and
`end()` finishes in 3–11 ms with the host fully uninstalled and all memory returned.

### Suggested fix

Drain the serial OUT endpoint the same way audio and vendor OUT already are, in
`drainClientTransfers()`:

```c
for (DeviceState &device : devices_)
{
  if (device.inUse && device.handle && device.hasSerialOutEndpoint)
  {
    usb_host_endpoint_halt(device.handle, device.serialOutEndpointAddress);
    usb_host_endpoint_flush(device.handle, device.serialOutEndpointAddress);
  }
}
```

That alone fixed it for us: five consecutive engage/disengage cycles with the device
deliberately silent, every one releasing the host completely with all heap returned.

### Two related observations, offered separately

**1. Do not `usb_host_endpoint_clear()` before releasing an interface.** We tried
clearing halts during teardown on the assumption that release wants active pipes. It
makes things worse, and the documentation says why: *"If the endpoint has any queued up
transfers, clearing a halt will resume their execution."* Clearing re-activates the
pipe and can resubmit the transfer that was just cancelled, so the interface release
then refuses. Halted-and-flushed is the releasable state. Current upstream code is
correct here; this is a warning for anyone tempted to "fix" it as we did.

**2. `lastError_` is sticky.** It is cleared only in `begin()`, so any timeout anywhere
in a session is still reported afterwards. Callers testing `lastError()` after `end()`
to decide whether teardown succeeded get false positives from unrelated earlier
failures — we chased a nonexistent teardown bug for some time because of this. A public
`clearLastError()`, or clearing at the start of `end()`, would make the value mean what
callers assume.

### Environment

- arduino-cli 1.5.1, ESP32 core 3.2.1 (IDF 5.4)
- FQBN `esp32:esp32:esp32s3:PartitionScheme=huge_app,CDCOnBoot=cdc`
- M5Stack Cardputer ADV (ESP32-S3FN8, no PSRAM)
- Device: Kenwood TH-D75 (VID 0x2166, PID 0x9023) — CDC-ACM + USB Audio composite

Happy to test a fix against the same hardware.
