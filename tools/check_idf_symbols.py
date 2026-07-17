#!/usr/bin/env python3
"""IDF symbol gate: every IDF symbol we use must EXIST for the ESP32-S3 in IDF v5.4.

Why this exists. Three compile errors reached the bench in one session, all the same
shape: a symbol that is real, is documented, and does not exist in THIS build.

  1. `enum Stage` added to usbserial.h, not mirrored to the .ino  (caught by check_parity)
  2. ESP_TIMER_ISR   -- only declared when CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD
                        is set. It is `default n`, and Arduino ships a FIXED sdkconfig.
  3. rtc_wdt.h       -- the entire API is `#if CONFIG_IDF_TARGET_ESP32 ||
                        CONFIG_IDF_TARGET_ESP32S2`. The S3 is excluded.

Reading the IDF docs cannot catch 2 or 3; only reading the HEADER's preprocessor
guards can. This gate does that mechanically against a real IDF v5.4 checkout.

Set CARDSAT_IDF=/path/to/esp-idf (v5.4). Skips cleanly if absent, so it never blocks
a build machine that has no IDF checkout.
"""
import os, re, sys, glob

IDF = os.environ.get('CARDSAT_IDF', '/home/claude/idfcheck/esp-idf')
if not os.path.isdir(IDF):
    print(f'  IDF symbol gate: SKIPPED (no IDF checkout at {IDF}; set CARDSAT_IDF)')
    sys.exit(0)

# Symbols we rely on -> the header that must declare them for the S3.
CHECKS = [
    ('esp_task_wdt_add_user',    'components/esp_system/include/esp_task_wdt.h'),
    ('esp_task_wdt_reset_user',  'components/esp_system/include/esp_task_wdt.h'),
    ('esp_task_wdt_delete_user', 'components/esp_system/include/esp_task_wdt.h'),
    ('esp_task_wdt_user_handle_t','components/esp_system/include/esp_task_wdt.h'),
    ('esp_reset_reason',         'components/esp_system/include/esp_system.h'),
    ('RTC_NOINIT_ATTR',          'components/esp_common/include/esp_attr.h'),
    # Coredump: reachable only because arduino-esp32's lib-builder defconfig sets
    # BOTH CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y and _DATA_FORMAT_ELF=y (the summary
    # API is #if'd on the pair), and huge_app.csv carries a coredump partition.
    # Verified 2026-07-16; re-check on any core upgrade.
    ('esp_core_dump_image_get',  'components/espcoredump/include/esp_core_dump.h'),
    ('esp_core_dump_get_summary','components/espcoredump/include/esp_core_dump.h'),
    ('esp_core_dump_image_erase','components/espcoredump/include/esp_core_dump.h'),
    # usb_host_lib_unblock: the escape hatch EspUsbHost never calls. Its lib task
    # blocks on usb_host_lib_handle_events(portMAX_DELAY), so running_=false is
    # never observed and end() force-deletes it mid-IDF-call. We unblock it first.
    ('usb_host_lib_unblock',     'components/usb/include/usb/usb_host.h'),
    # Stack measurement: xTaskGetHandle needs CONFIG_FREERTOS_USE_TRACE_FACILITY,
    # which arduino-esp32's lib-builder defconfig sets =y. Verified 2026-07-16.
    ('uxTaskGetStackHighWaterMark','components/freertos/FreeRTOS-Kernel-SMP/include/freertos/task.h'),
    ('xTaskGetHandle',            'components/freertos/FreeRTOS-Kernel-SMP/include/freertos/task.h'),
]

# Symbols known to be UNAVAILABLE on the S3 / in the Arduino sdkconfig. Using any of
# these is an error no matter how correct it looks in the docs.
FORBIDDEN = {
    'ESP_TIMER_ISR':  'gated by CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD (default n; '
                      'Arduino ships a fixed sdkconfig). Use esp_task_wdt_add_user().',
    'rtc_wdt_enable': 'rtc_wdt.h is #if CONFIG_IDF_TARGET_ESP32 || ESP32S2 -- S3 excluded.',
    'rtc_wdt_set_stage': 'rtc_wdt.h is ESP32/S2 only -- S3 excluded.',
    'rtc_wdt_protect_off': 'rtc_wdt.h is ESP32/S2 only -- S3 excluded.',
}

OURS = ['src/usbserial.cpp', 'src/usbserial.h', 'CardSat.ino', 'src/app.cpp']
fails = 0

# ---- 1. forbidden symbols must not appear in CODE (comments are fine: we explain them) ----
for f in OURS:
    if not os.path.exists(f): continue
    for i, line in enumerate(open(f, encoding='utf-8', errors='replace'), 1):
        code = line.split('//')[0]
        for sym, why in FORBIDDEN.items():
            if re.search(r'\b' + re.escape(sym) + r'\b', code):
                print(f'FORBIDDEN SYMBOL: {f}:{i}: {sym}\n    {why}')
                fails += 1

# ---- 2. symbols we use must be declared, and reachable for the S3 ----
def guards_exclude_s3(text, symbol):
    """True if `symbol` sits inside an #if that excludes the S3."""
    idx = text.find(symbol)
    if idx < 0: return False
    depth_stack = []
    for m in re.finditer(r'^\s*#\s*(if|ifdef|ifndef|else|elif|endif)([^\n]*)$', text[:idx], re.M):
        kind, rest = m.group(1), m.group(2)
        if kind in ('if', 'ifdef', 'ifndef'): depth_stack.append(rest)
        elif kind == 'endif' and depth_stack: depth_stack.pop()
    for cond in depth_stack:
        targets = re.findall(r'CONFIG_IDF_TARGET_(\w+)', cond)
        if targets and 'ESP32S3' not in targets:
            return True
    return False

for sym, hdr in CHECKS:
    path = os.path.join(IDF, hdr)
    if not os.path.exists(path):
        print(f'MISSING HEADER: {hdr} (IDF layout changed?)'); fails += 1; continue
    text = open(path, encoding='utf-8', errors='replace').read()
    if not re.search(r'\b' + re.escape(sym) + r'\b', text):
        print(f'SYMBOL NOT IN HEADER: {sym} not declared in {hdr}'); fails += 1; continue
    if guards_exclude_s3(text, sym):
        print(f'SYMBOL NOT REACHABLE ON S3: {sym} in {hdr} is behind a target guard that excludes ESP32S3')
        fails += 1

print('  IDF symbol gate: all symbols exist and are reachable on the S3.' if not fails
      else f'  IDF symbol gate: {fails} problem(s).')
sys.exit(1 if fails else 0)
