#!/usr/bin/env bash
#
# CardSat 0.9.72 - vendor the ESP-IDF USB Host component into the Arduino build
#
# Compiles the USB Host stack FROM SOURCE alongside the sketch, so its CONFIG_*
# options and ESP_LOGD narration can be chosen at sketch build time. Arduino's
# prebuilt libusb.a is then never pulled by the linker.
#
# This is the alternative to esp32-arduino-lib-builder: minutes instead of hours,
# no 12 GB of toolchain, and it leaves the Arduino core untouched. The cost is a
# version pin that must be revisited whenever the core is bumped (see PIN below).
#
#   ./vendor_usb_host.sh            # install the library
#   ./vendor_usb_host.sh --verify   # check an existing install
#   ./vendor_usb_host.sh --remove   # revert to the prebuilt libusb.a
#
# After installing, add to build_opt.h:
#     -DCONFIG_USB_HOST_EXT_PORT_RESET_ATTEMPTS=3
#     -DCONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK=1
#     -DLOG_LOCAL_LEVEL=5
# and add to the sketch, before <Arduino.h>:
#     #include <UsbHostSrc.h>
#
# Then DELETE THE SKETCH BUILD CACHE before rebuilding - see WHY below.

set -uo pipefail

# Resolve this NOW: the script cd's into the work dir later, so a relative
# $(dirname "$0") stops finding the patch that sits beside it.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# PIN: must match the esp-idf commit the installed core's libraries were built
# from, because usb_dwc_hal.c lives in libhal.a and CANNOT be overridden by this
# component. A mismatch shows up as "implicit declaration of
# usb_dwc_hal_set_fifo_config" - that is the registry's current release expecting
# a newer HAL than the core ships. Read the correct value from the core:
#     cat .../esp32-arduino-libs/*/versions.txt | grep esp-idf
IDF_COMMIT="${CARDSAT_IDF_COMMIT:-858a988d6e}"

LIBNAME="UsbHostSrc"
SRCS="hub.c ext_hub.c ext_port.c enum.c usbh.c hcd_dwc.c usb_host.c usb_helpers.c usb_private.c usb_phy.c"
PRIV="hcd.h hub.h usbh.h ext_hub.h ext_port.h enum.h usb_private.h"
PUB="usb_host.h usb_helpers.h usb_types_ch9.h usb_types_ch11.h usb_types_stack.h"

say()  { printf '\n== %s\n' "$*"; }
info() { printf '   %s\n' "$*"; }
die()  { printf '\nFAIL: %s\n' "$*" >&2; exit 1; }

for base in "$HOME/Documents/Arduino/libraries" "$HOME/Arduino/libraries"; do
  [ -d "$(dirname "$base")" ] && { SKETCH_LIBS="$base"; break; }
done
SKETCH_LIBS="${CARDSAT_LIBS:-${SKETCH_LIBS:-$HOME/Arduino/libraries}}"
L="$SKETCH_LIBS/$LIBNAME"

case "${1:-}" in
  --remove)
    say "Removing $L"
    [ -d "$L" ] || die "not installed"
    rm -rf "$L" && info "removed"
    cat <<'EOT'

   Also revert build_opt.h and remove the #include <UsbHostSrc.h> line, then
   DELETE THE SKETCH BUILD CACHE. The prebuilt libusb.a takes over again.
EOT
    exit 0 ;;
  --verify)
    say "Verifying $L"
    [ -d "$L/src" ] || die "not installed at $L"
    n=$(ls "$L/src"/*.c 2>/dev/null | wc -l | tr -d ' ')
    info "$n source files"
    [ "$n" -eq 10 ] || die "expected 10 sources, found $n"
    info "pinned commit: $(cat "$L/.idf-commit" 2>/dev/null || echo unknown)"
    say "Present. Confirm it is ACTUALLY LINKED after a build with:"
    cat <<'EOT'
     grep -c 'libraries/UsbHostSrc' build/*.map    # expect hundreds
     grep -c 'libusb\.a('          build/*.map     # expect 0
EOT
    exit 0 ;;
  "") ;;
  *) die "unknown option: $1" ;;
esac

say "Installing $LIBNAME from esp-idf @ $IDF_COMMIT"
command -v curl >/dev/null || die "curl not found"
B="https://raw.githubusercontent.com/espressif/esp-idf/$IDF_COMMIT/components/usb"

rm -rf "$L"; mkdir -p "$L/src/usb" || die "cannot create $L"
fail=0
for f in $SRCS;  do c=$(curl -s -m 30 -o "$L/src/$f"     -w "%{http_code}" "$B/$f");                 [ "$c" = 200 ] || { echo "  miss $f ($c)"; fail=1; }; done
for f in $PRIV;  do c=$(curl -s -m 30 -o "$L/src/$f"     -w "%{http_code}" "$B/private_include/$f"); [ "$c" = 200 ] || { echo "  miss $f ($c)"; fail=1; }; done
for f in $PUB;   do c=$(curl -s -m 30 -o "$L/src/usb/$f" -w "%{http_code}" "$B/include/usb/$f");     [ "$c" = 200 ] || { echo "  miss $f ($c)"; fail=1; }; done
[ "$fail" = 0 ] || die "some files did not download - check the commit hash"

echo "$IDF_COMMIT" > "$L/.idf-commit"

# WHY this header exists: arduino-cli discovers a library by matching #include
# directives against its headers. "usb/usb_host.h" is ALREADY satisfied by the
# platform include path, so the resolver never attributes it here and every source
# above is silently skipped - the build succeeds, the binary is a few hundred bytes
# different, and nothing has changed. A uniquely-named header forces discovery.
cat > "$L/src/UsbHostSrc.h" <<'EOT'
#pragma once
#include "usb/usb_host.h"
EOT

cat > "$L/library.properties" <<EOT
name=$LIBNAME
version=idf-$IDF_COMMIT
author=Espressif
maintainer=CardSat
sentence=ESP-IDF USB Host component compiled from source, overriding Arduino's prebuilt libusb.a
paragraph=Pinned to the esp-idf commit the installed core's libraries were built from so usb_dwc_hal in libhal.a matches. Lets CONFIG_USB_HOST_* and ESP_LOGD be set at sketch build time.
category=Communication
architectures=esp32
EOT

# ---- CardSat edits ------------------------------------------------------------
# Three changes live on top of the pristine IDF sources. Applied from a patch rather
# than hand-edited so a re-install is reproducible and the delta stays reviewable.
#
#  enum.c     CARDSAT_ENUM_STAGE_RETRIES  - re-issue a failed control request instead
#                                           of abandoning the device on first error
#             CARDSAT_ENUM_INITIAL_MPS_FS - initial EP0 packet size (unproven; 64)
#  hcd_dwc.c  CARDSAT_USB_RESET_HOLD_MS / _RECOVERY_MS - root-port reset timings,
#                                           which sdkconfig.h fixes at 30 ms each
#
# Plus cardsat_usb_log.h, which re-points the sub-ERROR log macros in these files at
# ESP_LOG_ERROR. Arduino's prebuilt liblog.a is built at CONFIG_LOG_MAXIMUM_LEVEL=1,
# so esp_log_level_set() cannot raise a tag above ERROR and the narration is generated
# then dropped. Opt in with -DCARDSAT_USB_VERBOSE=1.
PATCH="$SCRIPT_DIR/usb-host-cardsat.patch"
LOGHDR="$SCRIPT_DIR/cardsat_usb_log.h"
if [ -f "$PATCH" ] && [ -f "$LOGHDR" ]; then
  cp "$LOGHDR" "$L/src/" || die "could not install cardsat_usb_log.h"
  ( cd "$L" && patch -p1 --forward < "$PATCH" ) || die "patch failed - the pinned commit may have moved"
  for f in $SRCS; do
    grep -q 'cardsat_usb_log.h' "$L/src/$f" || sed -i.bak '1i #include "cardsat_usb_log.h"' "$L/src/$f"
    rm -f "$L/src/$f.bak"
  done
  info "CardSat edits applied (patch + log header)"
else
  info "WARNING: usb-host-cardsat.patch / cardsat_usb_log.h not found beside this"
  info "         script - installing PRISTINE sources. The enum retry, the reset"
  info "         timing overrides and the DEBUG narration will all be absent."
fi

info "installed: $L"
info "$(ls "$L/src"/*.c | wc -l | tr -d ' ') sources, $(ls "$L/src"/*.h | wc -l | tr -d ' ') private headers, $(ls "$L/src/usb"/*.h | wc -l | tr -d ' ') public headers"

cat <<'NEXT'

Next:

  1. build_opt.h:
       -DCONFIG_USB_HOST_EXT_PORT_RESET_ATTEMPTS=3
       -DCONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK=1
       -DLOG_LOCAL_LEVEL=5
  2. Sketch, before <Arduino.h>:
       #include <UsbHostSrc.h>
  3. DELETE THE SKETCH BUILD CACHE, then rebuild:
       rm -rf ~/Library/Caches/arduino/sketches/*      (macOS)
     build_opt.h is NOT a dependency of each .o, so changing it does NOT
     invalidate cached objects. Skipping this produces a byte-identical binary
     and looks exactly like "the flags had no effect" - it cost a full debug
     cycle to work that out.
  4. Verify the override actually happened, in the .map:
       grep -c 'libraries/UsbHostSrc' *.map    # hundreds
       grep -c 'libusb\.a('          *.map     # 0
     and that DEBUG narration is present:
       strings *.bin | grep -c 'Port still in reset'   # 1
  5. Re-run tools/check_app_fits.py - the DEBUG strings add roughly 8 KB.

NEXT
