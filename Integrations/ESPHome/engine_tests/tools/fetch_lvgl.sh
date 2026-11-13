#!/bin/sh
# Copies the LVGL 9.5.0 sources the firmware builds with into a folder for the
# host tests (default engine_tests/.lvgl, git-ignored). Nothing is downloaded.
#
# Sources, in this order:
#   1. the folder named in TAB_LVGL_SRC
#   2. the repo's ESPHome build: .esphome/build/apollo-tab-1/managed_components/lvgl__lvgl
#   3. the showcase harness copy: <scratchpad>/showcase/host/lvgl
#
# The copy is made once. A later clean rebuild of the firmware cannot take it
# away. Delete the folder to copy again.
#
# Usage: tools/fetch_lvgl.sh [dest]
set -eu
T="$(cd "$(dirname "$0")/.." && pwd)"
FW="$(cd "$T/.." && pwd)"
DEST="${1:-$T/.lvgl}"

version_of() {
  f="$1/lv_version.h"
  [ -f "$f" ] || { echo ""; return; }
  ma=$(awk '/#define LVGL_VERSION_MAJOR/ {print $3}' "$f")
  mi=$(awk '/#define LVGL_VERSION_MINOR/ {print $3}' "$f")
  pa=$(awk '/#define LVGL_VERSION_PATCH/ {print $3}' "$f")
  echo "$ma.$mi.$pa"
}

if [ -f "$DEST/lv_version.h" ] && [ -d "$DEST/src" ]; then
  v=$(version_of "$DEST")
  if [ "$v" = "9.5.0" ]; then
    echo "LVGL $v already copied to $DEST"
    exit 0
  fi
  echo "LVGL copy in $DEST is version '$v', not 9.5.0. Delete it and run again." >&2
  exit 1
fi

SRC=""
for c in "${TAB_LVGL_SRC:-}" "$FW/.esphome/build/apollo-tab-1/managed_components/lvgl__lvgl"; do
  if [ -n "$c" ] && [ -f "$c/lv_version.h" ] && [ -d "$c/src" ]; then
    SRC="$c"
    break
  fi
done
if [ -z "$SRC" ]; then
  for c in /private/tmp/*/*/*/scratchpad/showcase/host/lvgl; do
    if [ -f "$c/lv_version.h" ] && [ -d "$c/src" ]; then
      SRC="$c"
      break
    fi
  done
fi
if [ -z "$SRC" ]; then
  echo "No LVGL sources found. Set TAB_LVGL_SRC to an LVGL 9.5.0 folder, or compile the firmware once." >&2
  exit 1
fi

v=$(version_of "$SRC")
if [ "$v" != "9.5.0" ]; then
  echo "LVGL in $SRC is version '$v'; the host tests need 9.5.0 (the firmware's version)." >&2
  exit 1
fi

TMP="$DEST.part"
rm -rf "$TMP"
mkdir -p "$TMP/scripts/built_in_font"
cp -R "$SRC/src" "$TMP/src"
cp "$SRC/lvgl.h" "$SRC/lv_version.h" "$TMP/"
if [ -f "$SRC/scripts/built_in_font/Montserrat-Medium.ttf" ]; then
  cp "$SRC/scripts/built_in_font/Montserrat-Medium.ttf" "$TMP/scripts/built_in_font/"
fi
rm -rf "$DEST"
mv "$TMP" "$DEST"
echo "Copied LVGL $v from $SRC to $DEST"
