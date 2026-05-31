#pragma once
// ===========================================================================
//  civ_usbhost.h  -  USB-host serial adapter for CI-V
//
//  Presents a USB->serial / USB->CI-V dongle (CH34x, CP210x, FTDI, or true
//  CDC-ACM) as an Arduino Stream, so CivRadio's existing frame builder can use
//  it exactly like a HardwareSerial. The ESP32-S3 acts as the USB *host*.
//
//  The real implementation is compiled ONLY when CIV_ENABLE_USB_HOST is
//  defined (an ESP-IDF / PlatformIO build that pulls Espressif's USB-host VCP
//  components). The Arduino IDE core cannot act as a USB host, so there the
//  stub below is used and begin() simply returns false.
// ===========================================================================
#include <Arduino.h>

class UsbHostSerial : public Stream {
public:
  // Installs the USB host stack, registers the CH34x/CP210x/FTDI VCP drivers
  // plus CDC-ACM, and opens the first matching device. Returns true once a
  // device is open. (With CIV_ENABLE_USB_HOST off, always returns false.)
  bool begin(uint32_t baud);
  bool ready() const { return _ready; }

  // Stream / Print interface
  int    available() override;
  int    read() override;
  int    peek() override;
  void   flush() override;
  size_t write(uint8_t b) override;
  size_t write(const uint8_t* buf, size_t len) override;

  // Called from the USB RX callback / disconnect event (real impl only).
  void _pushRx(const uint8_t* data, size_t len);
  void _setDisconnected();

private:
  volatile bool _ready = false;
  static constexpr size_t RXBUF = 512;
  uint8_t          _rx[RXBUF];
  volatile size_t  _head = 0, _tail = 0;
  void*            _vcp = nullptr;   // CdcAcmDevice* (opaque to keep header light)
};
