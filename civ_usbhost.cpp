// ===========================================================================
//  civ_usbhost.cpp
// ===========================================================================
#include "civ_usbhost.h"

#if defined(CIV_ENABLE_USB_HOST)
// ---------------------------------------------------------------------------
//  Real USB-host implementation (ESP-IDF / PlatformIO).
//
//  Requires these Espressif managed components (see idf_component.yml):
//      espressif/usb_host_cdc_acm
//      espressif/usb_host_vcp
//      espressif/usb_host_ch34x_vcp
//      espressif/usb_host_cp210x_vcp
//      espressif/usb_host_ftdi_vcp
//
//  NOTE: the VCP component API has shifted slightly across releases. This is
//  written against the documented esp_usb::VCP interface; if your component
//  versions differ you may need to adjust the open()/line_coding calls. The
//  TTL path is unaffected and remains the recommended default.
// ---------------------------------------------------------------------------
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"
#include "usb/vcp_ch34x.hpp"
#include "usb/vcp_cp210x.hpp"
#include "usb/vcp_ftdi.hpp"
#include "usb/vcp.hpp"

using namespace esp_usb;

static UsbHostSerial* g_self = nullptr;

// USB host library event pump.
static void usb_lib_task(void*) {
  while (true) {
    uint32_t flags;
    usb_host_lib_handle_events(portMAX_DELAY, &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
  }
}

static bool rx_cb(const uint8_t* data, size_t len, void* arg) {
  if (g_self) g_self->_pushRx(data, len);
  return true;  // data consumed
}

static void ev_cb(const cdc_acm_host_dev_event_data_t* ev, void* arg) {
  if (ev->type == CDC_ACM_HOST_DEVICE_DISCONNECTED && g_self)
    g_self->_setDisconnected();
}

bool UsbHostSerial::begin(uint32_t baud) {
  g_self = this;

  usb_host_config_t hc = {};
  hc.skip_phy_setup = false;
  hc.intr_flags     = ESP_INTR_FLAG_LEVEL1;
  if (usb_host_install(&hc) != ESP_OK) return false;

  xTaskCreate(usb_lib_task, "usb_lib", 4096, nullptr, 10, nullptr);
  cdc_acm_host_install(nullptr);

  // Register vendor VCP drivers so common dongles are recognised.
  VCP::register_driver<FT23x>();
  VCP::register_driver<CP210x>();
  VCP::register_driver<CH34x>();

  cdc_acm_host_device_config_t dc = {};
  dc.connection_timeout_ms = 5000;
  dc.out_buffer_size       = 256;
  dc.in_buffer_size        = 256;
  dc.event_cb              = ev_cb;
  dc.data_cb               = rx_cb;
  dc.user_arg              = this;

  CdcAcmDevice* dev = VCP::open(&dc);   // auto-detects CH34x/CP210x/FTDI/CDC-ACM
  if (!dev) return false;

  cdc_acm_line_coding_t lc = {};
  lc.dwDTERate   = baud;
  lc.bCharFormat = 0;   // 1 stop bit
  lc.bParityType = 0;   // no parity
  lc.bDataBits   = 8;
  dev->line_coding_set(&lc);

  _vcp   = dev;
  _ready = true;
  return true;
}

size_t UsbHostSerial::write(uint8_t b) { return write(&b, 1); }

size_t UsbHostSerial::write(const uint8_t* buf, size_t len) {
  if (!_ready || !_vcp) return 0;
  CdcAcmDevice* dev = static_cast<CdcAcmDevice*>(_vcp);
  return (dev->tx_blocking((uint8_t*)buf, len) == ESP_OK) ? len : 0;
}

void UsbHostSerial::_pushRx(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    size_t nh = (_head + 1) % RXBUF;
    if (nh == _tail) break;            // buffer full: drop the rest
    _rx[_head] = data[i];
    _head = nh;
  }
}

void UsbHostSerial::_setDisconnected() { _ready = false; _vcp = nullptr; }

int  UsbHostSerial::available() { return (int)((_head + RXBUF - _tail) % RXBUF); }
int  UsbHostSerial::read()  { if (_head == _tail) return -1;
                              uint8_t b = _rx[_tail]; _tail = (_tail + 1) % RXBUF; return b; }
int  UsbHostSerial::peek()  { if (_head == _tail) return -1; return _rx[_tail]; }
void UsbHostSerial::flush() {}

#else
// ---------------------------------------------------------------------------
//  Stub used by the Arduino IDE build (no USB host available).
// ---------------------------------------------------------------------------
bool   UsbHostSerial::begin(uint32_t) { return false; }
int    UsbHostSerial::available() { return 0; }
int    UsbHostSerial::read()  { return -1; }
int    UsbHostSerial::peek()  { return -1; }
void   UsbHostSerial::flush() {}
size_t UsbHostSerial::write(uint8_t) { return 0; }
size_t UsbHostSerial::write(const uint8_t*, size_t) { return 0; }
void   UsbHostSerial::_pushRx(const uint8_t*, size_t) {}
void   UsbHostSerial::_setDisconnected() {}
#endif
