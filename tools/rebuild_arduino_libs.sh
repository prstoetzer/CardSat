#!/usr/bin/env bash
#
# CardSat 0.9.72 - rebuild Arduino's prebuilt ESP-IDF libraries for esp32s3
#
# Restores the USB-stack DEBUG logging that Arduino strips, raises the downstream-hub
# reset attempt count, and enables the enumeration filter callback. See
# LIBBUILDER-RECIPE.md for the evidence behind each.
#
# RUN THIS ON THE MAC. It needs ~12 GB free and several hours on one core.
#
#   ./rebuild_arduino_libs.sh            # clone, configure, build, verify (no install)
#   ./rebuild_arduino_libs.sh --install  # ... and install over the Arduino core
#   ./rebuild_arduino_libs.sh --verify   # just check what is currently installed
#   ./rebuild_arduino_libs.sh --restore  # put the backup back
#
# Deliberately NOT set -e: this script checks outputs, never exit codes. build.sh has
# too many moving parts to trust $? from, and a pipeline's $? is the last command's.

set -uo pipefail

# ---------------------------------------------------------------- configuration ----
LB_TAG="idf-release_v5.4"        # matches esp32-arduino-libs idf-release_v5.4-858a988d-v1
TARGET="esp32s3"
WORKDIR="${CARDSAT_LB_WORKDIR:-$HOME/cardsat-libbuilder}"
NEED_GB=12

# Where the Arduino core keeps the prebuilt libraries. Overridable for a non-default
# sketchbook or a different core version.
CORE_GLOB_MAC="$HOME/Library/Arduino15/packages/esp32/tools/esp32-arduino-libs"
CORE_GLOB_LINUX="$HOME/.arduino15/packages/esp32/tools/esp32-arduino-libs"

REQUIRED_SETTINGS=(
  "CONFIG_LOG_MAXIMUM_LEVEL=4"
  "CONFIG_USB_HOST_EXT_PORT_RESET_ATTEMPTS=3"
  "CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK=y"
)

DEFCONFIG_MARKER="# ---- CardSat 0.9.72 USB investigation ----"
read -r -d '' DEFCONFIG_BLOCK <<'EOF'

# ---- CardSat 0.9.72 USB investigation ----
# Keep the RUNTIME default at ERROR so nothing extra is emitted by default, but retain
# DEBUG call sites in the binary so esp_log_level_set() can unmask them per tag.
# Arduino ships MAXIMUM_LEVEL=1, which strips every ESP_LOGD in the USB stack - the
# port-event narration needed to diagnose an enumeration failure.
CONFIG_LOG_DEFAULT_LEVEL_ERROR=y
CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y
# Downstream hub ports get exactly one reset attempt upstream (invisible option behind
# IDF_EXPERIMENTAL_FEATURES; Espressif ticket IDF-11283). An FT232R needing a second
# attempt is disabled immediately: "EXT_PORT: Port disabled, reset attempts=1".
CONFIG_IDF_EXPERIMENTAL_FEATURES=y
CONFIG_USB_HOST_EXT_PORT_RESET_ATTEMPTS=3
# Allows rejecting a device before its configuration descriptor is fetched - the only
# way to ignore the IC-705's internal Burr-Brown CODEC, whose descriptor is 1191 bytes
# against IDF's 256-byte limit.
CONFIG_USB_HOST_ENABLE_ENUM_FILTER_CALLBACK=y
EOF

# ---------------------------------------------------------------------- helpers ----
say()  { printf '\n\033[1m== %s\033[0m\n' "$*"; }
info() { printf '   %s\n' "$*"; }
die()  { printf '\n\033[31mFAIL: %s\033[0m\n' "$*" >&2; exit 1; }

find_core_dir() {
  local base
  for base in "$CORE_GLOB_MAC" "$CORE_GLOB_LINUX"; do
    [ -d "$base" ] || continue
    local d
    d=$(find "$base" -maxdepth 2 -type d -name "$TARGET" 2>/dev/null | head -1)
    [ -n "$d" ] && { printf '%s' "$d"; return 0; }
  done
  return 1
}

# Check a sdkconfig for every required setting. Prints each verdict; returns non-zero
# if ANY is missing. This is the gate the whole script exists to satisfy - a build that
# "succeeded" without these did nothing useful.
verify_sdkconfig() {
  local sdk="$1" missing=0 want
  [ -f "$sdk" ] || { info "no sdkconfig at $sdk"; return 1; }
  for want in "${REQUIRED_SETTINGS[@]}"; do
    if grep -qxF "$want" "$sdk"; then
      printf '   [ok]      %s\n' "$want"
    else
      printf '   [MISSING] %s   (found: %s)\n' "$want" \
             "$(grep -E "^${want%%=*}=" "$sdk" 2>/dev/null || echo 'not set')"
      missing=1
    fi
  done
  return $missing
}

# ------------------------------------------------------------------- modes ---------
MODE="build"
case "${1:-}" in
  --install) MODE="build_install" ;;
  --verify)  MODE="verify" ;;
  --restore) MODE="restore" ;;
  --help|-h) sed -n '2,20p' "$0"; exit 0 ;;
  "")        ;;
  *)         die "unknown option: $1" ;;
esac

CORE_S3="$(find_core_dir)" || CORE_S3=""
[ -n "$CORE_S3" ] && info "Arduino core libs: $CORE_S3"

if [ "$MODE" = "verify" ]; then
  say "Verifying the INSTALLED libraries"
  [ -n "$CORE_S3" ] || die "could not locate esp32-arduino-libs/*/$TARGET"
  if verify_sdkconfig "$CORE_S3/sdkconfig"; then
    say "Installed libraries already carry all required settings."
  else
    say "Installed libraries are STOCK (or partial). Run without --verify to rebuild."
    exit 1
  fi
  exit 0
fi

if [ "$MODE" = "restore" ]; then
  say "Restoring the backup"
  [ -n "$CORE_S3" ] || die "could not locate the core"
  BACKUP="${CORE_S3}.stock-backup"
  [ -d "$BACKUP" ] || die "no backup at $BACKUP"
  rm -rf "$CORE_S3" && cp -R "$BACKUP" "$CORE_S3" || die "restore failed"
  verify_sdkconfig "$CORE_S3/sdkconfig" >/dev/null
  info "restored from $BACKUP"
  say "Done. Rebuild CardSat to go back to stock behaviour."
  exit 0
fi

# ------------------------------------------------------------------ preflight ------
say "1/6  Preflight"
for t in git python3; do
  command -v "$t" >/dev/null || die "$t not found"
done
info "git      $(git --version | awk '{print $3}')"
info "python3  $(python3 -V 2>&1 | awk '{print $2}')"

mkdir -p "$(dirname "$WORKDIR")" 2>/dev/null
AVAIL_KB=$(df -k "$(dirname "$WORKDIR")" | awk 'NR==2{print $4}')
AVAIL_GB=$((AVAIL_KB / 1024 / 1024))
info "free space at $(dirname "$WORKDIR"): ${AVAIL_GB} GB"
if [ "$AVAIL_GB" -lt "$NEED_GB" ]; then
  die "need ~${NEED_GB} GB, have ${AVAIL_GB} GB. lib-builder does full (non-shallow)
     submodule clones of ESP-IDF, then installs ~3.3 GB of toolchain, then builds."
fi

# ------------------------------------------------------------------- clone ---------
say "2/6  lib-builder @ $LB_TAG"
if [ -d "$WORKDIR/.git" ]; then
  info "reusing existing clone at $WORKDIR"
else
  git clone --depth 1 -b "$LB_TAG" \
      https://github.com/espressif/esp32-arduino-lib-builder.git "$WORKDIR" \
      || die "clone failed"
fi
[ -f "$WORKDIR/build.sh" ] || die "$WORKDIR does not look like lib-builder (no build.sh)"
cd "$WORKDIR" || die "cannot cd $WORKDIR"

# ------------------------------------------------------------------ configure ------
say "3/6  Applying the config fragment"
DEFCONFIG="configs/defconfig.common"
[ -f "$DEFCONFIG" ] || die "$DEFCONFIG not found - has lib-builder's layout changed?"
if grep -qF "$DEFCONFIG_MARKER" "$DEFCONFIG"; then
  info "already applied (marker present) - not appending twice"
else
  printf '%s\n' "$DEFCONFIG_BLOCK" >> "$DEFCONFIG" || die "could not append to $DEFCONFIG"
  info "appended to $DEFCONFIG"
fi
# defconfig.common applies to every variant, so this survives whatever -D is used.
grep -c "^CONFIG_" "$DEFCONFIG" | xargs -I{} echo "   $DEFCONFIG now has {} settings"

# ---------------------------------------------------------------------- build ------
say "4/6  Building for $TARGET  (hours, on one core; log: $WORKDIR/build.log)"
info "started $(date '+%H:%M:%S')"
./build.sh -t "$TARGET" > "$WORKDIR/build.log" 2>&1
BUILD_RC=$?
info "finished $(date '+%H:%M:%S')  (build.sh returned $BUILD_RC)"

# Do not trust BUILD_RC. Find the artifact.
OUT_SDK=$(find "$WORKDIR/out" -maxdepth 4 -path "*/$TARGET/sdkconfig" 2>/dev/null | head -1)
if [ -z "$OUT_SDK" ]; then
  printf '\n--- last 30 lines of build.log ---\n'
  tail -30 "$WORKDIR/build.log"
  die "no $TARGET/sdkconfig under $WORKDIR/out - the build did not produce libraries"
fi
OUT_DIR=$(dirname "$OUT_SDK")
info "built: $OUT_DIR"
info "libs:  $(ls "$OUT_DIR/lib" 2>/dev/null | wc -l | tr -d ' ') archives"

# --------------------------------------------------------------------- verify ------
say "5/6  Verifying the BUILT sdkconfig"
verify_sdkconfig "$OUT_SDK" \
  || die "the build completed but the settings did not take. The fragment is in
     $DEFCONFIG but did not reach the output - check build.log for a defconfig
     being overridden, and do NOT install this."

# -------------------------------------------------------------------- install ------
if [ "$MODE" != "build_install" ]; then
  say "6/6  Built and verified. NOT installed (re-run with --install)."
  info "output: $OUT_DIR"
  exit 0
fi

say "6/6  Installing over the Arduino core"
[ -n "$CORE_S3" ] || die "could not locate esp32-arduino-libs/*/$TARGET to install into"
BACKUP="${CORE_S3}.stock-backup"
if [ -d "$BACKUP" ]; then
  info "backup already exists, keeping it: $BACKUP"
else
  cp -R "$CORE_S3" "$BACKUP" || die "backup failed - refusing to overwrite without one"
  info "backed up to $BACKUP"
fi

rm -rf "$CORE_S3" && cp -R "$OUT_DIR" "$CORE_S3" || die "install failed - restore with --restore"

say "Verifying the INSTALLED sdkconfig"
verify_sdkconfig "$CORE_S3/sdkconfig" \
  || die "installed tree does not carry the settings. Run --restore."

cat <<NEXT

Installed. Next:

  1. Rebuild CardSat with -DCARDSAT_USB_DIAG=1 in build_opt.h.
  2. Run tools/check_app_fits.py on the result. MAXIMUM_LEVEL=4 restores format
     strings across the whole IDF, not just USB, so the binary WILL grow. If it no
     longer fits, swap CONFIG_LOG_MAXIMUM_LEVEL_DEBUG for _INFO in
     $DEFCONFIG and rebuild: that keeps ESP_LOGW, which alone would have
     explained several failures this cycle.
  3. usbDiagInstall() already raises HUB, EXT_HUB, EXT_PORT, ENUM and HCD DWC to
     ESP_LOG_DEBUG, so the narration turns on with no further firmware change.
  4. Bench: hub cold-boot, IC-705 standalone, FTDI behind the hub.

  Roll back at any time with:  $0 --restore

NEXT
