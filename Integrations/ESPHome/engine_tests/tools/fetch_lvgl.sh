#!/bin/sh
# Copies the LVGL 9.5.0 sources the firmware builds with into a folder for the
# host tests (default engine_tests/.lvgl, git-ignored). Nothing is downloaded.
#
# Sources, in this order:
#   1. the folder named in TAB_LVGL_SRC
#   2. the repo's ESPHome build: .esphome/build/apollo-tab-1/managed_components/lvgl__lvgl
#
# The copy is made once. A later clean rebuild of the firmware cannot take it
# away. Delete the folder to copy again.
#
# Pinned by commit (cards spec B.9): LVGL 9.5.0 is commit LVGL_COMMIT below,
# the one ESPHome 2026.10.2's component manager puts into the firmware build
# (its idf_component.yml names it). A source that names another commit, in
# that file or as the HEAD of a git checkout (CI checks lvgl/lvgl out at this
# commit), is refused. A source that names none is taken by its version, with
# a warning. The copy keeps the commit in .lvgl_commit.
#
# Usage: tools/fetch_lvgl.sh [dest]
set -eu
T="$(cd "$(dirname "$0")/.." && pwd)"
FW="$(cd "$T/.." && pwd)"
DEST="${1:-$T/.lvgl}"
LVGL_COMMIT=85aa60d18b3d5e5588d7b247abf90198f07c8a63

version_of() {
  f="$1/lv_version.h"
  [ -f "$f" ] || { echo ""; return; }
  ma=$(awk '/#define LVGL_VERSION_MAJOR/ {print $3}' "$f")
  mi=$(awk '/#define LVGL_VERSION_MINOR/ {print $3}' "$f")
  pa=$(awk '/#define LVGL_VERSION_PATCH/ {print $3}' "$f")
  echo "$ma.$mi.$pa"
}

# The commit a source names: the component manager's idf_component.yml, or
# the HEAD of a git checkout. Empty when it names none.
commit_of() {
  f="$1/idf_component.yml"
  if [ -f "$f" ]; then
    c=$(awk '$1 == "commit_sha:" {print $2; exit}' "$f")
    if [ -n "$c" ]; then echo "$c"; return; fi
  fi
  if [ -e "$1/.git" ] && command -v git > /dev/null; then
    git -C "$1" rev-parse HEAD 2>/dev/null || true
    return
  fi
  echo ""
}

if [ -f "$DEST/lv_version.h" ] && [ -d "$DEST/src" ]; then
  v=$(version_of "$DEST")
  c=$(cat "$DEST/.lvgl_commit" 2>/dev/null || true)
  if [ "$v" = "9.5.0" ] && { [ -z "$c" ] || [ "$c" = "$LVGL_COMMIT" ] || [ "$c" = "unknown" ]; }; then
    echo "LVGL $v already copied to $DEST (commit ${c:-not recorded})"
    exit 0
  fi
  if [ "$v" = "9.5.0" ]; then
    echo "LVGL copy in $DEST is commit $c, not $LVGL_COMMIT. Delete it and run again." >&2
    exit 1
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
  echo "No LVGL sources found. Set TAB_LVGL_SRC to an LVGL 9.5.0 folder, or compile the firmware once." >&2
  exit 1
fi

v=$(version_of "$SRC")
if [ "$v" != "9.5.0" ]; then
  echo "LVGL in $SRC is version '$v'; the host tests need 9.5.0 (the firmware's version)." >&2
  exit 1
fi

c=$(commit_of "$SRC")
if [ -n "$c" ] && [ "$c" != "$LVGL_COMMIT" ]; then
  echo "LVGL in $SRC is commit $c; the host tests need $LVGL_COMMIT (9.5.0, the firmware's)." >&2
  exit 1
fi
if [ -z "$c" ]; then
  echo "warning: $SRC names no commit; taken as LVGL $v by its version only" >&2
fi

TMP="$DEST.part"
rm -rf "$TMP"
mkdir -p "$TMP/scripts/built_in_font"
cp -R "$SRC/src" "$TMP/src"
cp "$SRC/lvgl.h" "$SRC/lv_version.h" "$TMP/"
if [ -f "$SRC/scripts/built_in_font/Montserrat-Medium.ttf" ]; then
  cp "$SRC/scripts/built_in_font/Montserrat-Medium.ttf" "$TMP/scripts/built_in_font/"
fi
echo "${c:-unknown}" > "$TMP/.lvgl_commit"
rm -rf "$DEST"
mv "$TMP" "$DEST"
echo "Copied LVGL $v (commit ${c:-unknown}) from $SRC to $DEST"
