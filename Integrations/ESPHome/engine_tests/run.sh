#!/bin/sh
# The engine's host tests (design 7.9), in one command:
#
#   Integrations/ESPHome/engine_tests/run.sh [test name ...]
#
# 1. copies LVGL 9.5.0 once (tools/fetch_lvgl.sh, nothing is downloaded)
# 2. builds LVGL twice: optimised, and with AddressSanitizer
# 3. builds the test program against engine/ with each, warnings as errors;
#    the second one with AddressSanitizer and UBSan
# 4. builds the screen rotation test (tests/test_fast_display.cpp, the
#    rotation of fast_display.h without LVGL or ESPHome) the same two ways
# 5. builds the screen flush test (tests/test_flush_cb.cpp, the whole of
#    fast_display.h against the stand-in headers in host/fast_display_sim)
#    the same two ways
# 6. runs all six; the optimised engine run writes the screenshots and the
#    cut-text report
#
# Folders (all git-ignored defaults, override to keep the repo clean):
#   TAB_LVGL_DIR   LVGL copy        default engine_tests/.lvgl
#   TAB_BUILD_DIR  objects, libs    default engine_tests/.build
#   TAB_OUT_DIR    report           default engine_tests/out
#   TAB_SHOTS_DIR  screenshots      default $TAB_OUT_DIR/shots
#
# A test name runs only that test; each program skips the names it lacks.
#
# Exit code 0 only when every run passes every check.
set -u
T="$(cd "$(dirname "$0")" && pwd)"
FW="$(cd "$T/.." && pwd)"
LV="${TAB_LVGL_DIR:-$T/.lvgl}"
WORK="${TAB_BUILD_DIR:-$T/.build}"
OUT="${TAB_OUT_DIR:-$T/out}"
SHOTS="${TAB_SHOTS_DIR:-$OUT/shots}"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
CC="${CC:-cc}"
CXX="${CXX:-c++}"
TEXT_TTF="$FW/fonts/Montserrat-Medium.ttf"
ICON_TTF="$FW/fonts/materialdesignicons-webfont.ttf"
COMMON="-DLV_CONF_INCLUDE_SIMPLE -DLV_KCONFIG_IGNORE -I$T/host -I$LV -I$LV/src"
O2_FLAGS="-O2 -g"
# LVGL itself is built with AddressSanitizer only: its tiny_ttf glyph cache does
# misaligned reads that UBSan reports and that are not the engine's.
ASAN_LIB_FLAGS="-O1 -g -fsanitize=address -fno-omit-frame-pointer"
ASAN_FLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"

hash_of() { if command -v md5 > /dev/null; then md5 -q "$@"; else md5sum "$@" | cut -d' ' -f1; fi; }
hash_text() { if command -v md5 > /dev/null; then md5 -q; else md5sum | cut -d' ' -f1; fi; }
# One hash of the contents of every LVGL source and header (with their paths),
# so a file changed in place counts too, not only an added or removed one.
hash_lvgl() {
  if command -v md5 > /dev/null; then
    find "$LV/src" "$LV/lvgl.h" "$LV/lv_version.h" -type f \( -name '*.c' -o -name '*.h' \) -exec md5 -r {} +
  else
    find "$LV/src" "$LV/lvgl.h" "$LV/lv_version.h" -type f \( -name '*.c' -o -name '*.h' \) -exec md5sum {} +
  fi | LC_ALL=C sort | hash_text
}
# Which compiler: the CC command and what it says it is.
hash_cc() { { echo "$CC"; $CC --version 2>&1; } | hash_text; }

"$T/tools/fetch_lvgl.sh" "$LV" || exit 1
for f in "$TEXT_TTF" "$ICON_TTF"; do
  [ -f "$f" ] || { echo "missing font $f"; exit 1; }
done
mkdir -p "$WORK" "$OUT" "$SHOTS"

# One static LVGL per flag set. It is built again when any of these change: the
# flags, lv_conf.h, the list of LVGL files built, the contents of any LVGL
# source or header, or the compiler.
LV_SUM="$(hash_lvgl)"
CC_SUM="$(hash_cc)"
build_lib() {
  name="$1"; flags="$2"
  lib="$WORK/liblvgl_$name.a"; obj="$WORK/obj_$name"; stamp="$WORK/stamp_$name"; list="$WORK/files.txt"
  find "$LV/src" -name '*.c' \
    ! -path '*/drivers/*' ! -path '*/libs/thorvg/*' ! -path '*/libs/vg_lite_driver/*' \
    ! -path '*/libs/gltf/*' ! -path '*/libs/nanovg/*' ! -path '*/draw/nxp/*' \
    ! -path '*/draw/renesas/*' ! -path '*/draw/espressif/*' ! -path '*/draw/nema_gfx/*' \
    ! -path '*/draw/vg_lite/*' ! -path '*/draw/sdl/*' ! -path '*/draw/opengles/*' \
    ! -path '*/draw/dma2d/*' ! -path '*/draw/eve/*' ! -path '*/draw/snapshot/*' \
    ! -name 'lv_font_montserrat_*' ! -name 'lv_font_simsun*' ! -name 'lv_font_source*' \
    ! -name 'lv_font_dejavu*' ! -name 'lv_font_unscii*' | sort > "$list"
  echo "$LV/src/font/lv_font_montserrat_16.c" >> "$list"
  want="$(echo "$flags $COMMON" | hash_text) $(hash_of "$T/host/lv_conf.h") $(hash_of "$list") $LV_SUM $CC_SUM"
  if [ -f "$lib" ] && [ "$(cat "$stamp" 2>/dev/null)" = "$want" ]; then
    echo "liblvgl_$name.a is up to date"
    return 0
  fi
  echo "building liblvgl_$name.a ($(wc -l < "$list" | tr -d ' ') files, $JOBS jobs)"
  rm -rf "$obj" "$lib"
  mkdir -p "$obj"
  LVSRC="$LV/src"
  export CC COMMON obj flags LVSRC
  # Object names keep the path so files with the same name never collide.
  # shellcheck disable=SC2016
  xargs -P "$JOBS" -n 1 sh -c '
    o="$obj/$(echo "${1#"$LVSRC"/}" | tr "/" "_").o"
    $CC $flags $COMMON -w -c "$1" -o "$o" || { echo "failed: $1"; exit 255; }
  ' sh < "$list" || return 1
  ar rcs "$lib" "$obj"/*.o || return 1
  rm -rf "$obj"
  echo "$want" > "$stamp"
}

build_test() {
  name="$1"; flags="$2"
  echo "compiling test_engine_$name"
  # shellcheck disable=SC2086
  $CXX -std=c++17 $flags -Wall -Wextra -Werror -Wno-unused-function -Wno-missing-field-initializers \
    $COMMON -I"$FW/engine" "$T/tests/test_engine.cpp" "$WORK/liblvgl_$name.a" -lz -o "$WORK/test_engine_$name"
}

# The rotation in fast_display.h is plain C++: no LVGL, no ESPHome.
build_display_test() {
  name="$1"; flags="$2"
  echo "compiling test_fast_display_$name"
  # shellcheck disable=SC2086
  $CXX -std=c++17 $flags -Wall -Wextra -Werror -I"$FW" "$T/tests/test_fast_display.cpp" \
    -o "$WORK/test_fast_display_$name"
}

# flush_cb, setup and set_enabled of fast_display.h need ESPHome, ESP-IDF and
# LVGL names: host/fast_display_sim has a stand-in for each header it includes.
build_flush_test() {
  name="$1"; flags="$2"
  echo "compiling test_flush_cb_$name"
  # shellcheck disable=SC2086
  $CXX -std=c++17 $flags -Wall -Wextra -Werror -I"$T/host/fast_display_sim" -I"$FW" "$T/tests/test_flush_cb.cpp" \
    -o "$WORK/test_flush_cb_$name"
}

build_lib o2 "$O2_FLAGS" || { echo "LVGL build failed (o2)"; exit 1; }
build_lib asan "$ASAN_LIB_FLAGS" || { echo "LVGL build failed (asan)"; exit 1; }
build_test o2 "$O2_FLAGS" || { echo "test build failed (o2)"; exit 1; }
build_test asan "$ASAN_FLAGS" || { echo "test build failed (asan)"; exit 1; }
build_display_test o2 "$O2_FLAGS" || { echo "screen rotation test build failed (o2)"; exit 1; }
build_display_test asan "$ASAN_FLAGS" || { echo "screen rotation test build failed (asan)"; exit 1; }
build_flush_test o2 "$O2_FLAGS" || { echo "screen flush test build failed (o2)"; exit 1; }
build_flush_test asan "$ASAN_FLAGS" || { echo "screen flush test build failed (asan)"; exit 1; }

status=0
echo
echo "== optimised build"
TAB_SHOTS_DIR="$SHOTS" "$WORK/test_engine_o2" "$T/contract" "$TEXT_TTF" "$ICON_TTF" "$OUT" "$@" || status=1
echo
echo "== AddressSanitizer and UBSan build"
mkdir -p "$WORK/asan_out"
ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=0:halt_on_error=1}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1}" \
  TAB_SHOTS_DIR="$WORK/asan_out/shots" \
  "$WORK/test_engine_asan" "$T/contract" "$TEXT_TTF" "$ICON_TTF" "$WORK/asan_out" --quick "$@" || status=1
echo
echo "== screen rotation, optimised build"
"$WORK/test_fast_display_o2" "$@" || status=1
echo
echo "== screen rotation, AddressSanitizer and UBSan build"
ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=0:halt_on_error=1}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1}" \
  "$WORK/test_fast_display_asan" --quick "$@" || status=1
echo
echo "== screen flush, optimised build"
"$WORK/test_flush_cb_o2" "$@" || status=1
echo
echo "== screen flush, AddressSanitizer and UBSan build"
# Short enough to run in full here too.
ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=0:halt_on_error=1}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1}" \
  "$WORK/test_flush_cb_asan" "$@" || status=1
echo
if [ "$status" = 0 ]; then echo "ALL PASSED"; else echo "FAILED"; fi
exit "$status"
