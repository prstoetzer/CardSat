#pragma once
// CardSat: route the USB stack's sub-ERROR logs through the one level that survives.
//
// Arduino's prebuilt liblog.a is compiled at CONFIG_LOG_MAXIMUM_LEVEL=1 (ERROR).
// Vendoring the usb component put the ESP_LOGD/W call sites back into the binary, but
// esp_log_level_set() lives in that prebuilt liblog.a and does not raise a tag above
// the level the library was built for - so the narration is generated and then dropped.
// Bench: LOG_LOCAL_LEVEL=5 produced the strings in the .bin and not one D line at
// runtime, and no W lines either.
//
// Rather than vendor the whole log component (which every other component links
// against), re-point the sub-ERROR macros in THESE FILES ONLY at ESP_LOG_ERROR, which
// demonstrably reaches the vprintf hook. The level marker is kept in the text so the
// original severity is still readable.
//
// Build with -DCARDSAT_USB_VERBOSE=1 to enable. Off by default: DEBUG across hub.c,
// ext_hub.c, ext_port.c, enum.c and hcd_dwc.c is a lot of text for a 6 KB ring.
#include "esp_log.h"

#if defined(CARDSAT_USB_VERBOSE) && CARDSAT_USB_VERBOSE
#undef ESP_LOGW
#undef ESP_LOGI
#undef ESP_LOGD
#undef ESP_LOGV
#define CARDSAT_USB_LOG_(mark, tag, fmt, ...) \
    esp_log_write(ESP_LOG_ERROR, tag, mark " (%lu) %s: " fmt "\n", \
                  (unsigned long)esp_log_timestamp(), tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) CARDSAT_USB_LOG_("W", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) CARDSAT_USB_LOG_("I", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) CARDSAT_USB_LOG_("D", tag, fmt, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) CARDSAT_USB_LOG_("V", tag, fmt, ##__VA_ARGS__)
#endif
