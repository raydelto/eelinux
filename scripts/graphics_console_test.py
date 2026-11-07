"""Package native PS2 GS tests for a console; never write a physical device."""
from pathlib import Path
import argparse, hashlib, json, re, shutil, subprocess, tarfile

ROOT=Path(__file__).resolve().parents[1]

def sha(p):
    with p.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()

RUN=r'''#!/bin/bash
set -euo pipefail
kit=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$kit"
sha256sum --check SHA256SUMS >/dev/null
[[ $(id -u) == 0 ]] || { echo 'Run from the PS2 root console login.' >&2; exit 1; }
[[ $(uname -m) == mips* ]] || { echo 'This test runs on the PlayStation 2, not this host.' >&2; exit 1; }
[[ -c /dev/ps2-gs && -c /dev/fb0 ]] || { echo 'Boot the matching PS2 GS kernel first.' >&2; exit 1; }
tty -s || { echo 'Run from the local console with a USB keyboard.' >&2; exit 1; }
if pgrep -x Xorg >/dev/null; then echo 'Exit the current Xorg session before testing.' >&2; exit 1; fi
mode=${1:-all}
unset PS2_GS_SHARD_INDEX PS2_GS_SHARD_COUNT
[[ $# -le 1 && ( $mode == all || $mode == primitives || $mode == exact-multiply || $mode == divide ||
    $mode == window-shapes || $mode == alpha-maps || $mode == alpha-windows || $mode == alpha-aliases || $mode == saturate || $mode == packed-saturate || $mode == bilinear-saturate || $mode == coverage-saturate || $mode == solid-saturate || $mode == map-saturate || $mode == shifted-maps || $mode == convolution-saturate || $mode == format-aliases || $mode == native15-aliases || $mode == screen || $mode == exclusion || $mode == multiply || $mode == order-blends || $mode == order-arithmetic || $mode == piecewise || $mode == piecewise-arithmetic || $mode == pd-families || $mode == pd-aliases || $mode == disjoint-over || $mode == pd-ratios || $mode == dodge-burn || $mode == softlight || $mode == hsl || $mode == gradients || $mode == packed10 || $mode == packed10-ops || $mode == packed10-convolution || $mode == packed10-alpha-read || $mode == packed10-alpha-write || $mode == packed10-mixed || $mode == depth30 || $mode == formats-filters || $mode == front-formats || $mode == resources || $mode == window-sampling || $mode == render-glyphs || $mode == render-geometry || $mode == cursor-argb || $mode == cursor-arithmetic || $mode == wide-saturate ) ]] || {
    echo 'Usage: ./run-console-test.sh [all|window-shapes|primitives|exact-multiply|divide|alpha-maps|alpha-windows|alpha-aliases|saturate|packed-saturate|bilinear-saturate|coverage-saturate|solid-saturate|map-saturate|shifted-maps|convolution-saturate|format-aliases|native15-aliases|screen|exclusion|multiply|order-blends|order-arithmetic|piecewise|piecewise-arithmetic|pd-families|pd-aliases|disjoint-over|pd-ratios|dodge-burn|softlight|hsl|gradients|packed10|packed10-ops|packed10-convolution|packed10-alpha-read|packed10-alpha-write|packed10-mixed|depth30|formats-filters|front-formats|resources|window-sampling|render-glyphs|render-geometry|cursor-argb|cursor-arithmetic|wide-saturate]' >&2; exit 2;
}
export PS2_GS_TEST_KIT="$kit"
export PS2_GS_TEST_LOGS="$kit/logs/$(date -u +%Y%m%dT%H%M%SZ)-$$"
mkdir -p "$PS2_GS_TEST_LOGS"
cp manifest.json "$PS2_GS_TEST_LOGS/manifest.json"
uname -a > "$PS2_GS_TEST_LOGS/uname.txt"
cat /proc/cmdline > "$PS2_GS_TEST_LOGS/cmdline.txt"
cat /proc/meminfo > "$PS2_GS_TEST_LOGS/meminfo-before.txt"
finish() {
    result=$?
    trap - EXIT
    cat /proc/meminfo > "$PS2_GS_TEST_LOGS/meminfo-after.txt"
    dmesg > "$PS2_GS_TEST_LOGS/dmesg.txt" || true
    printf 'mode=%s\nexit=%s\n' "$mode" "$result" > "$PS2_GS_TEST_LOGS/result.txt"
    sync
    printf '\nSaved console logs: %s\n' "$PS2_GS_TEST_LOGS"
    exit "$result"
}
trap finish EXIT
run_exact() {
    echo 'Testing exact RGB multiplication on the PS2 GS.'
    stdbuf -oL "$kit/bin/ps2-gs-exact-multiply-test" 2>&1 | tee "$PS2_GS_TEST_LOGS/exact-multiply.log"
    grep -q '^ps2-exact-multiply: pixel_checks=196608 exit=0$' "$PS2_GS_TEST_LOGS/exact-multiply.log"
}
run_order() {
    [[ -x "$kit/bin/ps2-gs-order-blends-test" ]] || { echo 'This kit does not include ordered-blend arithmetic.' >&2; exit 2; }
    stdbuf -oL "$kit/bin/ps2-gs-order-blends-test" 2>&1 | tee "$PS2_GS_TEST_LOGS/order-arithmetic.log"
    grep -q '^ps2-order-blends: pixel_checks=626688 exit=0$' "$PS2_GS_TEST_LOGS/order-arithmetic.log"
}
run_piecewise() {
    [[ -x "$kit/bin/ps2-gs-piecewise-test" ]] || { echo 'This kit does not include piecewise-blend arithmetic.' >&2; exit 2; }
    stdbuf -oL "$kit/bin/ps2-gs-piecewise-test" 2>&1 | tee "$PS2_GS_TEST_LOGS/piecewise-arithmetic.log"
    grep -q '^ps2-piecewise: pixel_checks=417792 exit=0$' "$PS2_GS_TEST_LOGS/piecewise-arithmetic.log"
}
run_divide() {
    [[ -x "$kit/bin/ps2-gs-divide-test" ]] || { echo 'This kit does not include the division diagnostic.' >&2; exit 2; }
    stdbuf -oL "$kit/bin/ps2-gs-divide-test" 2>&1 | tee "$PS2_GS_TEST_LOGS/divide.log"
    grep -q '^ps2-divide: pixel_checks=24576 exit=0$' "$PS2_GS_TEST_LOGS/divide.log"
}
run_saturate() {
    [[ -x "$kit/bin/ps2-gs-saturate-rgba8-test" ]] || { echo 'This kit does not include the wide Saturate probe.' >&2; exit 2; }
    stdbuf -oL "$kit/bin/ps2-gs-saturate-rgba8-test" 2>&1 | tee "$PS2_GS_TEST_LOGS/wide-saturate.log"
    grep -q '^ps2-saturate-rgba8: pixel_checks=32768 exit=0$' "$PS2_GS_TEST_LOGS/wide-saturate.log"
}
run_render() {
    local selected=$1 expected=$2
    export PS2_GS_CASE=$selected
    echo "Testing native Xorg/GS: $selected."
    "$kit/bin/ps2-xorg-prepare" "$kit/xorg.conf" "$PS2_GS_TEST_LOGS/xorg.conf"
    xinit "$kit/xclient.sh" -- "$kit/xserver.sh" :0 -logfile "$PS2_GS_TEST_LOGS/Xorg-$selected.log" -verbose 3 2>&1 | tee "$PS2_GS_TEST_LOGS/client-$selected.log"
    grep -q "completed pixel_checks=$expected errors=0" "$PS2_GS_TEST_LOGS/client-$selected.log"
    grep -q 'Server terminated successfully (0)' "$PS2_GS_TEST_LOGS/Xorg-$selected.log"
}
run_cursor() {
    [[ -x "$kit/bin/ps2-gs-cursor-argb-test" ]] || { echo 'This kit has no ARGB cursor probe.' >&2; exit 2; }
    stdbuf -oL "$kit/bin/ps2-gs-cursor-argb-test" 2>&1 | tee "$PS2_GS_TEST_LOGS/cursor-arithmetic.log"
    grep -q '^ps2-cursor-argb: pixel_checks=45056 image_checks=22 exit=0$' "$PS2_GS_TEST_LOGS/cursor-arithmetic.log"
}
if [[ $mode == cursor-arithmetic ]]; then run_cursor; exit 0; fi
if [[ $mode == exact-multiply ]]; then
    run_exact
    exit 0
fi
if [[ $mode == order-arithmetic ]]; then run_order; exit 0; fi
if [[ $mode == piecewise-arithmetic ]]; then run_piecewise; exit 0; fi
if [[ $mode == divide ]]; then run_divide; exit 0; fi
if [[ $mode == wide-saturate ]]; then run_saturate; exit 0; fi
case $mode in
    window-shapes) [[ @SHAPECHECKS@ != 0 ]] || { echo 'This kit has no bitmap shape/clip checks.' >&2; exit 2; }
        run_render window-shapes @SHAPECHECKS@; exit 0;;
    window-sampling) [[ @WINDOWSAMPLECHECKS@ != 0 ]] || { echo "This kit has no Window sampling checks." >&2; exit 2; }
        run_render window-sampling @WINDOWSAMPLECHECKS@; exit 0;;
    render-glyphs) [[ @GLYPHCHECKS@ != 0 ]] || { echo "This kit has no Render glyph checks." >&2; exit 2; }
        run_render render-glyphs @GLYPHCHECKS@; exit 0;;
    render-geometry) [[ @GEOMETRYCHECKS@ != 0 ]] || { echo "This kit has no Render geometry checks." >&2; exit 2; }
        run_render render-geometry @GEOMETRYCHECKS@; exit 0;;
    cursor-argb) [[ @CURSORARGBCHECKS@ != 0 ]] || { echo 'This kit has no ARGB cursor checks.' >&2; exit 2; }
        run_cursor; run_render cursor-argb @CURSORARGBCHECKS@; exit 0;;
    resources) [[ @RESOURCECHECKS@ != 0 ]] || { echo "This kit does not include capacity recovery checks." >&2; exit 2; }
        run_render resources @RESOURCECHECKS@; exit 0;;
    front-formats) [[ @FRONTFORMATCHECKS@ != 0 ]] || { echo "This kit does not include display format tests." >&2; exit 2; }
        run_render front-formats @FRONTFORMATCHECKS@; exit 0;;
    depth30) [[ @DEPTH30CHECKS@ != 0 ]] || { echo "This kit does not include depth30." >&2; exit 2; }
        run_render depth30 @DEPTH30CHECKS@; exit 0;;
    formats-filters) [[ @FORMATFILTERCHECKS@ != 0 ]] || { echo "This kit does not include public formats/filters." >&2; exit 2; }
        run_render formats-filters @FORMATFILTERCHECKS@; exit 0;;
    packed10-mixed) [[ @PACKED10MIXEDCHECKS@ != 0 ]] || { echo "This kit does not include mixed ten-bit formats." >&2; exit 2; }
        run_render packed10-mixed @PACKED10MIXEDCHECKS@; exit 0;;
    packed10-alpha-write) [[ @PACKED10ALPHAWRITECHECKS@ != 0 ]] || { echo "This kit does not include ten-bit writable alpha-map qualification." >&2; exit 2; }
        run_render packed10-alpha-write @PACKED10ALPHAWRITECHECKS@; exit 0;;
    packed10-alpha-read) [[ @PACKED10ALPHAREADCHECKS@ != 0 ]] || { echo "This kit does not include ten-bit alpha-map read qualification." >&2; exit 2; }
        run_render packed10-alpha-read @PACKED10ALPHAREADCHECKS@; exit 0;;
    packed10-convolution) [[ @PACKED10CONVOLUTIONCHECKS@ != 0 ]] || { echo "This kit does not include ten-bit convolution qualification." >&2; exit 2; }
        run_render packed10-convolution @PACKED10CONVOLUTIONCHECKS@; exit 0;;
    packed10-ops) [[ @PACKED10OPSCHECKS@ != 0 ]] || { echo "This kit does not include ten-bit operator qualification." >&2; exit 2; }
        run_render packed10-ops @PACKED10OPSCHECKS@; exit 0;;
    packed10) [[ @PACKED10CHECKS@ != 0 ]] || { echo "This kit does not include ten-bit qualification." >&2; exit 2; }
        run_render packed10 @PACKED10CHECKS@; exit 0;;
    gradients) [[ @GRADIENTCHECKS@ != 0 ]] || { echo 'This kit does not include gradients.' >&2; exit 2; }
        run_render gradients @GRADIENTCHECKS@; exit 0;;
    hsl) [[ @HSLCHECKS@ != 0 ]] || { echo 'This kit does not include HSL qualification.' >&2; exit 2; }
        run_render hsl @HSLCHECKS@; exit 0;;
    softlight) [[ @SOFTLIGHTCHECKS@ != 0 ]] || { echo 'This kit does not include SoftLight qualification.' >&2; exit 2; }
        run_render softlight @SOFTLIGHTCHECKS@; exit 0;;
    dodge-burn) [[ @DODGEBURNCHECKS@ != 0 ]] || { echo 'This kit does not include ColorDodge/ColorBurn qualification.' >&2; exit 2; }
        run_render dodge-burn @DODGEBURNCHECKS@; exit 0;;
    pd-ratios) [[ @PDRATIOSCHECKS@ != 0 ]] || { echo 'This kit does not include remaining disjoint/conjoint qualification.' >&2; exit 2; }
        run_render pd-ratios @PDRATIOSCHECKS@; exit 0;;
    pd-families) [[ @PDFAMILIESCHECKS@ != 0 ]] || { echo 'This kit does not include disjoint/conjoint qualification.' >&2; exit 2; }
        run_render pd-families @PDFAMILIESCHECKS@; exit 0;;
    pd-aliases) [[ @PDALIASESCHECKS@ != 0 ]] || { echo 'This kit does not include disjoint/conjoint qualification.' >&2; exit 2; }
        run_render pd-aliases @PDALIASESCHECKS@; exit 0;;
    disjoint-over) [[ @DISJOINTOVERCHECKS@ != 0 ]] || { echo 'This kit does not include DisjointOverReverse qualification.' >&2; exit 2; }
        run_render disjoint-over @DISJOINTOVERCHECKS@; exit 0;;
    screen) [[ @SCREENCHECKS@ != 0 ]] || { echo 'This kit does not include Screen qualification.' >&2; exit 2; }
        run_render screen @SCREENCHECKS@; exit 0;;
    exclusion) [[ @EXCLUSIONCHECKS@ != 0 ]] || { echo 'This kit does not include Exclusion qualification.' >&2; exit 2; }
        run_render exclusion @EXCLUSIONCHECKS@; exit 0;;
    multiply) [[ @MULTIPLYCHECKS@ != 0 ]] || { echo 'This kit does not include Multiply qualification.' >&2; exit 2; }
        run_render multiply @MULTIPLYCHECKS@; exit 0;;
    piecewise) [[ @PIECEWISECHECKS@ != 0 ]] || { echo 'This kit does not include Overlay/HardLight qualification.' >&2; exit 2; }
        run_render piecewise @PIECEWISECHECKS@; exit 0;;
    order-blends) [[ @ORDERBLENDSCHECKS@ != 0 ]] || { echo 'This kit does not include Darken/Lighten/Difference qualification.' >&2; exit 2; }
        run_render order-blends @ORDERBLENDSCHECKS@; exit 0;;
    alpha-maps) run_render alpha-maps @MAPCHECKS@; exit 0;;
    alpha-windows) run_render alpha-windows @WINDOWCHECKS@; exit 0;;
    alpha-aliases) run_render alpha-aliases @ALIASCHECKS@; exit 0;;
    saturate) [[ @SATURATECHECKS@ != 0 ]] || { echo 'This kit does not include Xorg Saturate qualification.' >&2; exit 2; }
        run_render saturate @SATURATECHECKS@; exit 0;;
    packed-saturate) [[ @PACKEDCHECKS@ != 0 ]] || { echo 'This kit does not include packed-format Saturate qualification.' >&2; exit 2; }
        run_render packed-saturate @PACKEDCHECKS@; exit 0;;
    bilinear-saturate) [[ @BILINEARCHECKS@ != 0 ]] || { echo 'This kit does not include bilinear Saturate qualification.' >&2; exit 2; }
        run_render bilinear-saturate @BILINEARCHECKS@; exit 0;;
    coverage-saturate) [[ @COVERAGECHECKS@ != 0 ]] || { echo 'This kit does not include coverage Saturate qualification.' >&2; exit 2; }
        run_render coverage-saturate @COVERAGECHECKS@; exit 0;;
    solid-saturate) [[ @SOLIDCHECKS@ != 0 ]] || { echo 'This kit does not include solid Saturate qualification.' >&2; exit 2; }
        run_render solid-saturate @SOLIDCHECKS@; exit 0;;
    map-saturate) [[ @MAPSATURATECHECKS@ != 0 ]] || { echo 'This kit does not include alpha-map Saturate qualification.' >&2; exit 2; }
        run_render map-saturate @MAPSATURATECHECKS@; exit 0;;
    shifted-maps) [[ @SHIFTEDCHECKS@ != 0 ]] || { echo 'This kit does not include shifted writable map qualification.' >&2; exit 2; }
        run_render shifted-maps @SHIFTEDCHECKS@; exit 0;;
    native15-aliases) [[ @NATIVE15ALIASCHECKS@ != 0 ]] || { echo 'This kit does not include native depth-15 writable aliases.' >&2; exit 2; }
        run_render native15-aliases @NATIVE15ALIASCHECKS@; exit 0;;
    format-aliases) [[ @FORMATALIASCHECKS@ != 0 ]] || { echo 'This kit does not include different-format writable aliases.' >&2; exit 2; }
        run_render format-aliases @FORMATALIASCHECKS@; exit 0;;
    convolution-saturate) [[ @CONVOLUTIONCHECKS@ != 0 ]] || { echo 'This kit does not include wide convolution qualification.' >&2; exit 2; }
        run_render convolution-saturate @CONVOLUTIONCHECKS@; exit 0;;
esac
echo 'Testing native GS lookup, RGBA transfer, masks, and solid Boolean operations.'
stdbuf -oL "$kit/bin/ps2-gs-lut-test" 2>&1 | tee "$PS2_GS_TEST_LOGS/primitives.log"
for expected in 'lut: pixel_checks=90112' 'map: pixel_checks=294912' \
    'map-blit: pixel_checks=1105920' 'rgba-solid: pixel_checks=491520' \
    'alpha-read: pixel_checks=5063' 'rgba-copy: pixel_checks=276480' 'rgba-multiply: pixel_checks=737280'; do
    grep -q "^ps2-$expected exit=0$" "$PS2_GS_TEST_LOGS/primitives.log"
done
[[ $mode == all ]] || exit 0
run_exact
if [[ -x "$kit/bin/ps2-gs-order-blends-test" ]]; then run_order; fi
if [[ -x "$kit/bin/ps2-gs-piecewise-test" ]]; then run_piecewise; fi
if [[ -x "$kit/bin/ps2-gs-divide-test" ]]; then run_divide; fi
if [[ -x "$kit/bin/ps2-gs-saturate-rgba8-test" ]]; then run_saturate; fi
unset PS2_GS_CASE
echo 'Testing native Xorg/GS pixels, formats, filters, transforms, and repeat modes.'
"$kit/bin/ps2-xorg-prepare" "$kit/xorg.conf" "$PS2_GS_TEST_LOGS/xorg.conf"
xinit "$kit/xclient.sh" -- "$kit/xserver.sh" :0 -logfile "$PS2_GS_TEST_LOGS/Xorg.log" -verbose 3 \
    2>&1 | tee "$PS2_GS_TEST_LOGS/client.log"
grep -q 'completed pixel_checks=@CHECKS@ errors=0' "$PS2_GS_TEST_LOGS/client.log"
grep -q 'Server terminated successfully (0)' "$PS2_GS_TEST_LOGS/Xorg.log"
if [[ @SHAPECHECKS@ != 0 ]]; then run_render window-shapes @SHAPECHECKS@; fi
run_render alpha-windows @WINDOWCHECKS@
run_render alpha-aliases @ALIASCHECKS@
if [[ @SATURATECHECKS@ != 0 ]]; then run_render saturate @SATURATECHECKS@; fi
if [[ @PACKEDCHECKS@ != 0 ]]; then run_render packed-saturate @PACKEDCHECKS@; fi
if [[ @BILINEARCHECKS@ != 0 ]]; then run_render bilinear-saturate @BILINEARCHECKS@; fi
if [[ @COVERAGECHECKS@ != 0 ]]; then run_render coverage-saturate @COVERAGECHECKS@; fi
if [[ @SOLIDCHECKS@ != 0 ]]; then run_render solid-saturate @SOLIDCHECKS@; fi
if [[ @MAPSATURATECHECKS@ != 0 ]]; then run_render map-saturate @MAPSATURATECHECKS@; fi
if [[ @SHIFTEDCHECKS@ != 0 ]]; then run_render shifted-maps @SHIFTEDCHECKS@; fi
if [[ @CONVOLUTIONCHECKS@ != 0 ]]; then run_render convolution-saturate @CONVOLUTIONCHECKS@; fi
if [[ @FORMATALIASCHECKS@ != 0 ]]; then run_render format-aliases @FORMATALIASCHECKS@; fi
if [[ @NATIVE15ALIASCHECKS@ != 0 ]]; then run_render native15-aliases @NATIVE15ALIASCHECKS@; fi
if [[ @SCREENCHECKS@ != 0 ]]; then run_render screen @SCREENCHECKS@; fi
if [[ @EXCLUSIONCHECKS@ != 0 ]]; then run_render exclusion @EXCLUSIONCHECKS@; fi
if [[ @MULTIPLYCHECKS@ != 0 ]]; then run_render multiply @MULTIPLYCHECKS@; fi
if [[ @ORDERBLENDSCHECKS@ != 0 ]]; then run_render order-blends @ORDERBLENDSCHECKS@; fi
if [[ @PIECEWISECHECKS@ != 0 ]]; then run_render piecewise @PIECEWISECHECKS@; fi
if [[ @PDFAMILIESCHECKS@ != 0 ]]; then run_render pd-families @PDFAMILIESCHECKS@; fi
if [[ @PDRATIOSCHECKS@ != 0 ]]; then run_render pd-ratios @PDRATIOSCHECKS@; fi
if [[ @DODGEBURNCHECKS@ != 0 ]]; then run_render dodge-burn @DODGEBURNCHECKS@; fi
if [[ @SOFTLIGHTCHECKS@ != 0 ]]; then run_render softlight @SOFTLIGHTCHECKS@; fi
if [[ @HSLCHECKS@ != 0 ]]; then run_render hsl @HSLCHECKS@; fi
if [[ @GRADIENTCHECKS@ != 0 ]]; then run_render gradients @GRADIENTCHECKS@; fi
if [[ @PACKED10CHECKS@ != 0 ]]; then run_render packed10 @PACKED10CHECKS@; fi
if [[ @PACKED10OPSCHECKS@ != 0 ]]; then run_render packed10-ops @PACKED10OPSCHECKS@; fi
if [[ @PACKED10CONVOLUTIONCHECKS@ != 0 ]]; then run_render packed10-convolution @PACKED10CONVOLUTIONCHECKS@; fi
if [[ @PACKED10ALPHAREADCHECKS@ != 0 ]]; then run_render packed10-alpha-read @PACKED10ALPHAREADCHECKS@; fi
if [[ @PACKED10ALPHAWRITECHECKS@ != 0 ]]; then run_render packed10-alpha-write @PACKED10ALPHAWRITECHECKS@; fi
if [[ @PACKED10MIXEDCHECKS@ != 0 ]]; then run_render packed10-mixed @PACKED10MIXEDCHECKS@; fi
if [[ @DEPTH30CHECKS@ != 0 ]]; then run_render depth30 @DEPTH30CHECKS@; fi
if [[ @FORMATFILTERCHECKS@ != 0 ]]; then run_render formats-filters @FORMATFILTERCHECKS@; fi
if [[ @FRONTFORMATCHECKS@ != 0 ]]; then run_render front-formats @FRONTFORMATCHECKS@; fi
if [[ @RESOURCECHECKS@ != 0 ]]; then run_render resources @RESOURCECHECKS@; fi
if [[ @CURSORARGBCHECKS@ != 0 ]]; then run_cursor; run_render cursor-argb @CURSORARGBCHECKS@; fi
if [[ @WINDOWSAMPLECHECKS@ != 0 ]]; then run_render window-sampling @WINDOWSAMPLECHECKS@; fi
if [[ @GLYPHCHECKS@ != 0 ]]; then run_render render-glyphs @GLYPHCHECKS@; fi
if [[ @GEOMETRYCHECKS@ != 0 ]]; then run_render render-geometry @GEOMETRYCHECKS@; fi
echo 'All packaged GS/Xorg console checks passed.'
'''

SERVER=r'''#!/bin/bash
set -eu
exec /usr/bin/Xorg "$@" -config "$PS2_GS_TEST_LOGS/xorg.conf" \
    -modulepath "$PS2_GS_TEST_KIT/modules,/usr/lib/xorg/modules" \
    -fp /usr/share/fonts/X11/misc -nolisten tcp -noreset
'''
CLIENT=r'''#!/bin/bash
set -eu
if [[ ${PS2_GS_CASE:-} == window-shapes ]]; then
    /usr/bin/stdbuf -oL "$PS2_GS_TEST_KIT/bin/ps2-xorg-shape-test"
    echo 'ps2-gpu2d: completed pixel_checks=@SHAPECHECKS@ errors=0 PS2_GS_required=1'
    exit 0
fi
exec /usr/bin/stdbuf -oL "$PS2_GS_TEST_KIT/bin/ps2-xorg-gpu2d-test"
'''

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('native','staging','kernel','client','primitive','multiply','output'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--checks',type=int,default=18326)
    p.add_argument('--shapes',type=Path,help='source-verified native bitmap Shape and GC clip diagnostic')
    p.add_argument('--divide',type=Path,help='source-verified native wide-integer division diagnostic')
    p.add_argument('--saturate',type=Path,help='source-verified native UNORM8 GS Saturate diagnostic')
    p.add_argument('--packed-saturate',action='store_true',help='include the focused nearest packed-format Saturate client')
    p.add_argument('--bilinear-saturate',action='store_true',help='include the focused full-16-fraction bilinear Saturate client')
    p.add_argument('--coverage-saturate',action='store_true',help='include A1/A4/A8 bilinear and component-alpha Saturate')
    p.add_argument('--solid-saturate',action='store_true',help='include original RGBA16 solid source/mask Saturate')
    p.add_argument('--map-saturate',action='store_true',help='include nearest/bilinear alpha-map Saturate')
    p.add_argument('--shifted-maps',action='store_true',help='include shifted same-format writable alpha-map aliases')
    p.add_argument('--native15-aliases',action='store_true',help='include native depth-15 RGB555/RGB444 writable aliases')
    p.add_argument('--screen',action='store_true',help='include GS Screen across native/packed/coverage formats and native15 aliases')
    p.add_argument('--exclusion',action='store_true',help='include GS Exclusion with one final rounding across both integer products')
    p.add_argument('--render-multiply',action='store_true',help='include GS XRender Multiply with separately rounded integer products')
    p.add_argument('--order-blends',action='store_true',help='include GS Darken/Lighten/Difference across supported picture layouts')
    p.add_argument('--piecewise',action='store_true',help='include GS Overlay and HardLight')
    p.add_argument('--pd-families',action='store_true',help='include disjoint/conjoint Clear/Src/Dst and DisjointOverReverse')
    p.add_argument('--pd-ratios',action='store_true',help='include all remaining disjoint/conjoint operators in six protocol profiles')
    p.add_argument('--packed10-ops',action='store_true',help='include all defined Render operators for raw ten-bit pictures')
    p.add_argument('--packed10-convolution',action='store_true',help='include original-precision ten-bit signed convolution and aliases')
    p.add_argument('--packed10-alpha-read',action='store_true',help='include ten-bit source/mask alpha-map reads before filtering')
    p.add_argument('--packed10-alpha-write',action='store_true',help='include ten-bit writable maps and independent GS quantization')
    p.add_argument('--packed10-mixed',action='store_true',help='include mixed ten-bit, coverage and byte/word/native-16 formats')
    p.add_argument('--depth30',action='store_true',help='include GS raw30 pixmaps and parent/map aliases')
    p.add_argument('--formats-filters',action='store_true',help='enumerate every advertised format with all six filter names')
    p.add_argument('--front-formats',action='store_true',help='check raw ten-bit sources against the actual display framebuffer')
    p.add_argument('--window-sampling',action='store_true',help='include transformed and repeating Window sources and masks')
    p.add_argument('--render-glyphs',action='store_true',help='include all28 glyph-admissible formats, individual and aggregate masks')
    p.add_argument('--render-geometry',action='store_true',help='include GS trapezoids, triangles, strips, fans and direct traps')
    p.add_argument('--cursor-argb',action='store_true',help='include GS ARGB cursor and native Xorg lifecycle checks')
    p.add_argument('--cursor-primitive',type=Path,help='source-verified GS ARGB cursor pixel probe')
    p.add_argument('--resources',action='store_true',help='verify native drawing continues after VRAM and handle allocation rejection')
    p.add_argument('--packed10',action='store_true',help='include four raw ten-bit formats with basic operators and nearest/bilinear filtering')
    p.add_argument('--gradients',action='store_true',help='include GS linear/radial/conical gradient sources and masks')
    p.add_argument('--hsl',action='store_true',help='include GS HSL modes, clipping boundaries and component-alpha Dst checks')
    p.add_argument('--softlight',action='store_true',help='include GS SoftLight, exact square root and branch boundaries')
    p.add_argument('--dodge-burn',action='store_true',help='include GS ColorDodge/ColorBurn, original solid precision and branch boundaries')
    p.add_argument('--piecewise-primitive',type=Path,help='source-verified native piecewise blend diagnostic')
    p.add_argument('--order-primitive',type=Path,help='source-verified native exact 17-bit ordered-blend diagnostic')
    p.add_argument('--format-aliases',action='store_true',help='include different byte/word-format writable alpha-map aliases')
    p.add_argument('--convolution-saturate',action='store_true',help='include R5900 wide signed convolution Saturate')
    args=p.parse_args()
    if args.packed_saturate and not args.saturate:p.error('--packed-saturate requires --saturate')
    if args.bilinear_saturate and not args.saturate:p.error('--bilinear-saturate requires --saturate')
    if args.coverage_saturate and not args.bilinear_saturate:p.error('--coverage-saturate requires --bilinear-saturate')
    if args.solid_saturate and not args.coverage_saturate:p.error('--solid-saturate requires --coverage-saturate')
    if args.map_saturate and not args.solid_saturate:p.error('--map-saturate requires --solid-saturate')
    if args.shifted_maps and not args.map_saturate:p.error('--shifted-maps requires --map-saturate')
    if args.convolution_saturate and not args.shifted_maps:p.error('--convolution-saturate requires --shifted-maps')
    if args.format_aliases and not args.convolution_saturate:p.error('--format-aliases requires --convolution-saturate')
    if args.native15_aliases and not args.format_aliases:p.error('--native15-aliases requires --format-aliases')
    if args.screen and not args.native15_aliases:p.error('--screen requires --native15-aliases')
    if args.exclusion and not args.screen:p.error('--exclusion requires --screen')
    if args.render_multiply and not args.exclusion:p.error('--render-multiply requires --exclusion')
    if args.order_blends and not args.render_multiply:p.error('--order-blends requires --render-multiply')
    if args.order_blends != bool(args.order_primitive):p.error('--order-blends and --order-primitive must be supplied together')
    if args.piecewise and not args.order_blends:p.error('--piecewise requires --order-blends')
    if args.pd_families and not args.piecewise:p.error('--pd-families requires --piecewise')
    if args.pd_ratios and not args.pd_families:p.error('--pd-ratios requires --pd-families')
    if args.packed10_ops and not args.packed10:p.error('--packed10-ops requires --packed10')
    if args.packed10_convolution and not args.packed10_ops:p.error('--packed10-convolution requires --packed10-ops')
    if args.packed10_alpha_read and not args.packed10_convolution:p.error('--packed10-alpha-read requires --packed10-convolution')
    if args.packed10_alpha_write and not args.packed10_alpha_read:p.error('--packed10-alpha-write requires --packed10-alpha-read')
    if args.packed10_mixed and not args.packed10_alpha_write:p.error('--packed10-mixed requires --packed10-alpha-write')
    if args.depth30 and not args.packed10_mixed:p.error('--depth30 requires --packed10-mixed')
    if args.formats_filters and not args.depth30:p.error('--formats-filters requires --depth30')
    if args.front_formats and not args.formats_filters:p.error('--front-formats requires --formats-filters')
    if args.cursor_argb != bool(args.cursor_primitive):p.error('--cursor-argb and --cursor-primitive must be supplied together')
    if args.resources and not args.front_formats:p.error('--resources requires --front-formats')
    if args.packed10 and not args.gradients:p.error('--packed10 requires --gradients')
    if args.gradients and not args.hsl:p.error('--gradients requires --hsl')
    if args.hsl and not args.softlight:p.error('--hsl requires --softlight')
    if args.softlight and not args.dodge_burn:p.error('--softlight requires --dodge-burn')
    if args.dodge_burn and not args.pd_ratios:p.error('--dodge-burn requires --pd-ratios')
    if args.piecewise != bool(args.piecewise_primitive):p.error('--piecewise and --piecewise-primitive must be supplied together')
    if args.output.exists():raise RuntimeError('use a new output directory')
    build=json.loads((args.native/'build.json').read_text())
    kernel=json.loads((args.kernel/'kernel.json').read_text())
    if build['diagnostic_logging']:raise RuntimeError('use the production module')
    for path,digest in build['inputs'].items():
        if sha(ROOT/path)!=digest:raise RuntimeError('source changed after native build: '+path)
    if sha(args.native/'ps2_drv.so')!=build['module_sha256'] or \
        sha(args.kernel/'kernel/vmlinuz')!=kernel['kernel_sha256']:
        raise RuntimeError('kernel/module identity mismatch')
    client=json.loads((args.client.parent/'client.json').read_text())
    multiply=json.loads((args.multiply.parent/'check.json').read_text())
    if client['result']!='built' or client['binary_sha256']!=sha(args.client) or \
        client['source_sha256']!=sha(ROOT/'desktop/ps2-xorg-gpu2d-test.c') or \
        client['pixel_checks_per_depth']!=args.checks:
        raise RuntimeError('native client identity or expected count mismatch')
    for path,digest in client.get('source_inputs',{}).items():
        if sha(ROOT/path)!=digest:raise RuntimeError('native client source changed: '+path)
    if not multiply['result'].startswith('native diagnostic built') or \
        multiply['binary_sha256']!=sha(args.multiply):
        raise RuntimeError('native multiplication diagnostic identity mismatch')
    for path,digest in multiply['source_inputs'].items():
        if sha(ROOT/path)!=digest:raise RuntimeError('multiplication source changed: '+path)
    if args.divide:
        divide=json.loads((args.divide.parent/'build.json').read_text())
        if divide['binary_sha256']!=sha(args.divide):raise RuntimeError('division diagnostic identity mismatch')
        for path,digest in divide['source_inputs'].items():
            if sha(ROOT/path)!=digest:raise RuntimeError('division source changed: '+path)
    if args.saturate:
        saturate=json.loads((args.saturate.parent/'build.json').read_text())
        if saturate['binary_sha256']!=sha(args.saturate) or saturate.get('arithmetic_case')!='saturate-rgba8':
            raise RuntimeError('Saturate diagnostic identity mismatch')
        for path,digest in saturate['source_inputs'].items():
            if sha(ROOT/path)!=digest:raise RuntimeError('Saturate source changed: '+path)
    if args.order_primitive:
        ordered=json.loads((args.order_primitive.parent/'build.json').read_text())
        if ordered['binary_sha256']!=sha(args.order_primitive) or ordered.get('arithmetic_case')!='order-blends' or ordered['expected_byte_channel_checks']!=626688:
            raise RuntimeError('ordered-blend diagnostic identity mismatch')
        for path,digest in ordered['source_inputs'].items():
            if sha(ROOT/path)!=digest:raise RuntimeError('ordered-blend source changed: '+path)
    if args.piecewise_primitive:
        ordered=json.loads((args.piecewise_primitive.parent/'build.json').read_text())
        if ordered['binary_sha256']!=sha(args.piecewise_primitive) or ordered.get('arithmetic_case')!='piecewise' or ordered['expected_byte_channel_checks']!=417792:
            raise RuntimeError('piecewise-blend diagnostic identity mismatch')
        for path,digest in ordered['source_inputs'].items():
            if sha(ROOT/path)!=digest:raise RuntimeError('piecewise-blend source changed: '+path)
    if args.cursor_primitive:
        cursor=json.loads((args.cursor_primitive.parent/'build.json').read_text())
        if cursor['binary_sha256']!=sha(args.cursor_primitive) or cursor['expected_pixel_checks']!=45056 or cursor['expected_image_checks']!=22 or cursor['module_sha256']!=build['module_sha256']:
            raise RuntimeError('ARGB cursor diagnostic identity/count mismatch')
        for path,digest in cursor['source_inputs'].items():
            if sha(ROOT/path)!=digest:raise RuntimeError('ARGB cursor source changed: '+path)
    primitive=json.loads((args.primitive.parent/'build.json').read_text())
    if primitive['binary_sha256']!=sha(args.primitive):raise RuntimeError('combined primitive diagnostic identity mismatch')
    expected_primitives={'lut':90112,'map':294912,'map-blit':1105920,'rgba-solid':491520,'alpha-read':5063,'rgba-copy':276480,'rgba-multiply':737280}
    if primitive.get('expected_checks')!=expected_primitives:raise RuntimeError('primitive probe does not cover every runner-required check')
    for path,digest in primitive['source_inputs'].items():
        if sha(ROOT/path)!=digest:raise RuntimeError('primitive source changed: '+path)
    focused={}
    focused['SHAPE']=0
    if args.shapes:
        shapes=json.loads((args.shapes.parent/'shape-client.json').read_text())
        if shapes['binary_sha256']!=sha(args.shapes) or shapes['expected_checks']!=128:
            raise RuntimeError('Shape diagnostic identity or expected count mismatch')
        for path,digest in shapes['source_inputs'].items():
            if sha(ROOT/path)!=digest:raise RuntimeError('Shape diagnostic source changed: '+path)
        focused['SHAPE']=128
    source=(ROOT/'desktop/ps2-xorg-gpu2d-test.c').read_text()
    for label in ('MAP','WINDOW','ALIAS'):
        match=re.search(r'#define PS2_GPU2D_ALPHA_'+label+r'_CHECKS (\d+)',source)
        if not match:raise RuntimeError('missing focused client count: '+label)
        focused[label]=int(match[1])
    match=re.search(r'#define PS2_GPU2D_SATURATE_CHECKS (\d+)',source)
    if args.saturate and not match:raise RuntimeError('missing focused Saturate count')
    focused['SATURATE']=int(match[1]) if args.saturate else 0
    match=re.search(r'#define PS2_GPU2D_PACKED_SATURATE_CHECKS (\d+)',source)
    if args.packed_saturate and not match:raise RuntimeError('missing packed Saturate count')
    focused['PACKED']=int(match[1]) if args.packed_saturate else 0
    match=re.search(r'#define PS2_GPU2D_BILINEAR_SATURATE_CHECKS (\d+)',source)
    if args.bilinear_saturate and not match:raise RuntimeError('missing bilinear Saturate count')
    focused['BILINEAR']=int(match[1]) if args.bilinear_saturate else 0
    match=re.search(r'#define PS2_GPU2D_COVERAGE_SATURATE_CHECKS (\d+)',source)
    if args.coverage_saturate and not match:raise RuntimeError('missing coverage Saturate count')
    focused['COVERAGE']=int(match[1]) if args.coverage_saturate else 0
    match=re.search(r'#define PS2_GPU2D_SOLID_SATURATE_CHECKS (\d+)',source)
    if args.solid_saturate and not match:raise RuntimeError('missing solid Saturate count')
    focused['SOLID']=int(match[1]) if args.solid_saturate else 0
    match=re.search(r'#define PS2_GPU2D_MAP_SATURATE_CHECKS (\d+)',source)
    if args.map_saturate and not match:raise RuntimeError('missing alpha-map Saturate count')
    focused['MAPSATURATE']=int(match[1]) if args.map_saturate else 0
    match=re.search(r'#define PS2_GPU2D_SHIFTED_MAP_CHECKS (\d+)',source)
    if args.shifted_maps and not match:raise RuntimeError('missing shifted writable map count')
    focused['SHIFTED']=int(match[1]) if args.shifted_maps else 0
    match=re.search(r'#define PS2_GPU2D_CONVOLUTION_SATURATE_CHECKS (\d+)',source)
    if args.convolution_saturate and not match:raise RuntimeError('missing wide convolution count')
    focused['CONVOLUTION']=int(match[1]) if args.convolution_saturate else 0
    match=re.search(r'#define PS2_GPU2D_FORMAT_ALIAS_CHECKS (\d+)',source)
    if args.format_aliases and not match:raise RuntimeError('missing different-format alias count')
    focused['FORMATALIAS']=int(match[1]) if args.format_aliases else 0
    match=re.search(r'#define PS2_GPU2D_NATIVE15_ALIAS_CHECKS (\d+)',source)
    if args.native15_aliases and not match:raise RuntimeError('missing native depth-15 alias count')
    focused['NATIVE15ALIAS']=int(match[1]) if args.native15_aliases else 0
    match=re.search(r'#define PS2_GPU2D_SCREEN_CHECKS (\d+)',source)
    if args.screen and not match:raise RuntimeError('missing Screen count')
    focused['SCREEN']=int(match[1]) if args.screen else 0
    match=re.search(r'#define PS2_GPU2D_EXCLUSION_CHECKS (\d+)',source)
    if args.exclusion and not match:raise RuntimeError('missing Exclusion count')
    focused['EXCLUSION']=int(match[1]) if args.exclusion else 0
    match=re.search(r'#define PS2_GPU2D_MULTIPLY_CHECKS (\d+)',source)
    if args.render_multiply and not match:raise RuntimeError('missing Multiply count')
    focused['MULTIPLY']=int(match[1]) if args.render_multiply else 0
    match=re.search(r'#define PS2_GPU2D_ORDER_BLENDS_CHECKS (\d+)',source)
    if args.order_blends and not match:raise RuntimeError('missing ordered-blend count')
    focused['ORDERBLENDS']=int(match[1]) if args.order_blends else 0
    match=re.search(r'#define PS2_GPU2D_PIECEWISE_CHECKS (\d+)',source)
    if args.piecewise and not match:raise RuntimeError('missing piecewise count')
    focused['PIECEWISE']=int(match[1]) if args.piecewise else 0
    for key,macro in [('PDFAMILIES','PD_FAMILIES'),('PDALIASES','PD_ALIASES'),('DISJOINTOVER','DISJOINT_OVER')]:
        match=re.search(r'#define PS2_GPU2D_'+macro+r'_CHECKS (\d+)',source)
        if args.pd_families and not match:raise RuntimeError('missing '+macro+' count')
        focused[key]=int(match[1]) if args.pd_families else 0
    match=re.search(r'#define PS2_GPU2D_PD_RATIOS_CHECKS (\d+)',source)
    if args.pd_ratios and not match:raise RuntimeError('missing PD ratio count')
    focused['PDRATIOS']=int(match[1]) if args.pd_ratios else 0
    match=re.search(r'#define PS2_GPU2D_DODGE_BURN_CHECKS (\d+)',source)
    if args.dodge_burn and not match:raise RuntimeError('missing ColorDodge/ColorBurn count')
    focused['DODGEBURN']=int(match[1]) if args.dodge_burn else 0
    match=re.search(r'#define PS2_GPU2D_SOFTLIGHT_CHECKS (\d+)',source)
    if args.softlight and not match:raise RuntimeError('missing SoftLight count')
    focused['SOFTLIGHT']=int(match[1]) if args.softlight else 0
    match=re.search(r'#define PS2_GPU2D_HSL_CHECKS (\d+)',source)
    if args.hsl and not match:raise RuntimeError('missing HSL count')
    focused['HSL']=int(match[1]) if args.hsl else 0
    match=re.search(r'#define PS2_GPU2D_GRADIENT_CHECKS (\d+)',source)
    if args.gradients and not match:raise RuntimeError('missing gradient count')
    focused['GRADIENT']=int(match[1]) if args.gradients else 0
    match=re.search(r'#define PS2_GPU2D_PACKED10_CHECKS (\d+)',source)
    if args.packed10 and not match:raise RuntimeError('missing packed10 count')
    focused['PACKED10']=int(match[1]) if args.packed10 else 0
    match=re.search(r'#define PS2_GPU2D_PACKED10_OPS_CHECKS (\d+)',source)
    if args.packed10_ops and not match:raise RuntimeError('missing packed10 operator count')
    focused['PACKED10OPS']=int(match[1]) if args.packed10_ops else 0
    match=re.search(r'#define PS2_GPU2D_PACKED10_CONVOLUTION_CHECKS (\d+)',source)
    if args.packed10_convolution and not match:raise RuntimeError('missing packed10 convolution count')
    focused['PACKED10CONVOLUTION']=int(match[1]) if args.packed10_convolution else 0
    match=re.search(r'#define PS2_GPU2D_PACKED10_ALPHA_READ_CHECKS (\d+)',source)
    if args.packed10_alpha_read and not match:raise RuntimeError('missing packed10 alpha-read count')
    focused['PACKED10ALPHAREAD']=int(match[1]) if args.packed10_alpha_read else 0
    match=re.search(r'#define PS2_GPU2D_PACKED10_ALPHA_WRITE_CHECKS (\d+)',source)
    if args.packed10_alpha_write and not match:raise RuntimeError('missing packed10 alpha-write count')
    focused['PACKED10ALPHAWRITE']=int(match[1]) if args.packed10_alpha_write else 0
    match=re.search(r'#define PS2_GPU2D_PACKED10_MIXED_CHECKS (\d+)',source)
    if args.packed10_mixed and not match:raise RuntimeError('missing packed10 mixed count')
    focused['PACKED10MIXED']=int(match[1]) if args.packed10_mixed else 0
    for flag,key,macro in [(args.depth30,'DEPTH30','PS2_GPU2D_DEPTH30_CHECKS'),(args.formats_filters,'FORMATFILTER','PS2_GPU2D_FORMAT_FILTER_CHECKS'),(args.front_formats,'FRONTFORMAT','PS2_GPU2D_FRONT_FORMAT_CHECKS'),(args.resources,'RESOURCE','PS2_GPU2D_RESOURCE_CHECKS'),(args.cursor_argb,'CURSORARGB','PS2_GPU2D_CURSOR_ARGB_CHECKS'),(args.render_geometry,'GEOMETRY','PS2_GPU2D_RENDER_GEOMETRY_CHECKS'),(args.window_sampling,'WINDOWSAMPLE','PS2_GPU2D_WINDOW_SAMPLE_CHECKS'),(args.render_glyphs,'GLYPH','PS2_GPU2D_RENDER_GLYPH_CHECKS')]:
        match=re.search(r'#define '+macro+r' (\d+)',source)
        if flag and not match:raise RuntimeError('missing '+macro)
        focused[key]=int(match[1]) if flag else 0
    remaining_operators=('broader qualification of accepted operator/format combinations' if args.hsl else
        '4 blend operator IDs: HSLHue, HSLSaturation, HSLColor, HSLLuminosity' if args.softlight else
        '5 blend operator IDs: SoftLight, HSLHue, HSLSaturation, HSLColor, HSLLuminosity' if args.dodge_burn else
        '7 blend operator IDs: ColorDodge, ColorBurn, SoftLight, HSLHue, HSLSaturation, HSLColor, HSLLuminosity' if args.pd_ratios else
        '24 operator IDs, including remaining disjoint/conjoint and blend modes' if args.pd_families else
        'extended operators other than Screen, Exclusion, Multiply, Darken, Lighten, Difference, Overlay and HardLight' if args.piecewise else
        'extended operators other than Screen, Exclusion, Multiply, Darken, Lighten and Difference' if args.order_blends else
        'extended operators other than Screen, Exclusion and Multiply' if args.render_multiply else
        'extended operators other than Screen and Exclusion' if args.exclusion else
        'extended operators other than Screen' if args.screen else 'extended operators')
    remaining_saturate=('broader memory/performance and physical qualification' if args.formats_filters else
        'public format/filter qualification' if args.depth30 else
        'native depth30 and broader format/alias qualification' if args.packed10_mixed else
        'ten-bit coverage/16-bit combinations and native depth30' if args.packed10_alpha_write else
        'ten-bit writable destination alpha maps, coverage/16-bit combinations and native depth30' if args.packed10_alpha_read else
        'ten-bit alpha maps, coverage/16-bit combinations and native depth30' if args.packed10_convolution else
        'ten-bit image convolution, alpha maps, coverage/16-bit combinations and native depth30'
        if args.packed10_ops else 'more-than-eight-bit image Saturate' if args.convolution_saturate else
        'wide-convolution and more-than-eight-bit image Saturate'
        if args.map_saturate else 'wide-convolution and more-than-eight-bit image Saturate, alpha-map Saturate'
        if args.solid_saturate else 'wide-convolution and more-than-eight-bit ARGB Saturate, solid-fill and alpha-map Saturate'
        if args.bilinear_saturate else 'filtered and more-than-eight-bit ARGB Saturate, solid-fill and alpha-map Saturate'
        if args.packed_saturate else 'wider/filtered ARGB Saturate, solid-fill and alpha-map Saturate'
        if args.saturate else 'ARGB Saturate')
    args.output.mkdir(parents=True)
    kit=args.output/'ps2-gs-console-test';kit.mkdir()
    files={
        'EELINUX-GS.ELF':args.kernel/'kernel/vmlinuz',
        'bin/ps2-gs-lut-test':args.primitive,
        'bin/ps2-gs-exact-multiply-test':args.multiply,
        'bin/ps2-xorg-gpu2d-test':args.client,
        'bin/ps2-xorg-prepare':args.staging/'usr/lib/eelinux/ps2-xorg-prepare',
        'modules/drivers/ps2_drv.so':args.native/'ps2_drv.so',
        'xorg.conf':ROOT/'rootfs/etc/X11/xorg.conf',
        'evidence/module-build.json':args.native/'build.json',
        'evidence/primitive-build.json':args.primitive.parent/'build.json',
        'evidence/module-build.log':args.native/'build.log',
        'evidence/kernel-build.json':args.kernel/'kernel.json',
        'evidence/kernel-config':args.kernel/'kernel/.config',
        'evidence/client-build.json':args.client.parent/'client.json',
        'evidence/exact-multiply-build.json':args.multiply.parent/'check.json',
    }
    if args.divide:
        files.update({'bin/ps2-gs-divide-test':args.divide,'evidence/divide-build.json':args.divide.parent/'build.json'})
    if args.saturate:
        files.update({'bin/ps2-gs-saturate-rgba8-test':args.saturate,
            'evidence/saturate-build.json':args.saturate.parent/'build.json'})
    if args.order_primitive:
        files.update({'bin/ps2-gs-order-blends-test':args.order_primitive,'evidence/order-blends-build.json':args.order_primitive.parent/'build.json'})
    if args.piecewise_primitive:
        files.update({'bin/ps2-gs-piecewise-test':args.piecewise_primitive,'evidence/piecewise-build.json':args.piecewise_primitive.parent/'build.json'})
    if args.cursor_primitive:
        files.update({'bin/ps2-gs-cursor-argb-test':args.cursor_primitive,'evidence/cursor-argb-build.json':args.cursor_primitive.parent/'build.json'})
    if args.shapes:
        files.update({'bin/ps2-xorg-shape-test':args.shapes,'evidence/shape-client.json':args.shapes.parent/'shape-client.json'})
    for target,source in files.items():
        dest=kit/target;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,dest)
        dest.chmod(0o755 if target.startswith('bin/') else 0o644)
    run=RUN.replace('@CHECKS@',str(args.checks))
    for label,count in focused.items():run=run.replace('@'+label+'CHECKS@',str(count))
    for target,content in (('run-console-test.sh',run),
                           ('xclient.sh',CLIENT.replace('@SHAPECHECKS@',str(focused['SHAPE']))),('xserver.sh',SERVER)):
        dest=kit/target;dest.write_text(content,newline='\n');dest.chmod(0o755)
        subprocess.run(['bash','-n',str(dest)],check=True)
    manifest={'target':'PlayStation 2 Graphics Synthesizer',
        'result':'console test candidate; physical results pending',
        'full_2D_complete':False,'physical_hardware_verified':False,
        'guest_CPU_colour_renderer':False,'emulator_required':False,
        'CPU_coverage_pixel_arithmetic':False,'component_alpha_integrated':True,
        'CPU_geometry_planning':True,'owned_client_byte_storage':True,
        'unsupported_rendering':'rejected; no software fallback',
        'known_unsupported':([] if args.convolution_saturate else
            ['wide-convolution coverage Saturate' if args.coverage_saturate else 'filtered coverage Saturate'])+[
            remaining_saturate,
            *([] if args.formats_filters else ['additional Render formats']),*([] if args.gradients else ['gradients']),remaining_operators,
            *([] if args.native15_aliases else ['native 15-bit differing converted-format writable destination-map aliases' if args.format_aliases else
            'differing converted-format writable destination-map aliases'])],
        'expected_Xorg_checks':args.checks,'expected_exact_multiply_channel_checks':196608,
        'expected_focused_Xorg_checks':focused,
        'expected_all_Xorg_checks':args.checks+focused['SHAPE']+focused['WINDOW']+focused['ALIAS']+focused['SATURATE']+focused['PACKED']+focused['BILINEAR']+focused['COVERAGE']+focused['SOLID']+focused['MAPSATURATE']+focused['SHIFTED']+focused['CONVOLUTION']+focused['FORMATALIAS']+focused['NATIVE15ALIAS']+focused['SCREEN']+focused['EXCLUSION']+focused['MULTIPLY']+focused['ORDERBLENDS']+focused['PIECEWISE']+focused['PDFAMILIES']+focused['PDRATIOS']+focused['DODGEBURN']+focused['SOFTLIGHT']+focused['HSL']+focused['GRADIENT']+focused['PACKED10']+focused['PACKED10OPS']+focused['PACKED10CONVOLUTION']+focused['PACKED10ALPHAREAD']+focused['PACKED10ALPHAWRITE']+focused['PACKED10MIXED']+focused['DEPTH30']+focused['FORMATFILTER']+focused['FRONTFORMAT']+focused['RESOURCE']+focused['CURSORARGB']+focused['GEOMETRY']+focused['WINDOWSAMPLE']+focused['GLYPH'],
        'expected_Saturate_probe_channel_checks':32768 if args.saturate else None,
        'native_Xorg_Saturate_qualification':'pending' if args.saturate else 'not included',
        'native_Xorg_packed_Saturate_qualification':'pending' if args.packed_saturate else 'not included',
        'native_Xorg_bilinear_Saturate_qualification':'pending' if args.bilinear_saturate else 'not included',
        'native_Xorg_coverage_Saturate_qualification':'pending' if args.coverage_saturate else 'not included',
        'native_Xorg_solid_Saturate_qualification':'pending' if args.solid_saturate else 'not included',
        'native_Xorg_map_Saturate_qualification':'pending' if args.map_saturate else 'not included',
        'native_Xorg_shifted_map_qualification':'pending' if args.shifted_maps else 'not included',
        'native_Xorg_native15_alias_qualification':'pending' if args.native15_aliases else 'not included',
        'native_Xorg_format_alias_qualification':'pending' if args.format_aliases else 'not included',
        'native_Xorg_convolution_Saturate_qualification':'pending' if args.convolution_saturate else 'not included',
        'native_Xorg_Screen_qualification':'pending' if args.screen else 'not included',
        'native_Xorg_Exclusion_qualification':'pending' if args.exclusion else 'not included',
        'native_Xorg_Multiply_qualification':'pending' if args.render_multiply else 'not included',
        'native_Xorg_ordered_blend_qualification':'pending' if args.order_blends else 'not included',
        'expected_order_blends_channel_checks':626688 if args.order_primitive else None,
        'expected_piecewise_channel_checks':417792 if args.piecewise_primitive else None,
        'native_Xorg_piecewise_qualification':'pending' if args.piecewise else 'not included',
        'native_Xorg_pd_families_qualification':'pending' if args.pd_families else 'not included',
        'native_Xorg_pd_ratios_qualification':'pending' if args.pd_ratios else 'not included',
        'native_Xorg_dodge_burn_qualification':'pending' if args.dodge_burn else 'not included',
        'native_Xorg_HSL_qualification':'pending' if args.hsl else 'not included',
        'native_Xorg_packed10_operator_qualification':'pending' if args.packed10_ops else 'not included',
        'native_Xorg_packed10_convolution_qualification':'pending' if args.packed10_convolution else 'not included',
        'native_Xorg_packed10_alpha_read_qualification':'pending' if args.packed10_alpha_read else 'not included',
        'packed10_alpha_read_scope':'Source/mask alpha replaced before nearest/bilinear/convolution using original precision; raw A2, decoded A1/A4/A8 and existing colour maps, map-only ten-bit routing and one shared GS alias snapshot' if args.packed10_alpha_read else 'not included',
        'packed10_convolution_scope':'All53 IDs, original-precision RGB10/A2 and mixed32-bit source/mask convolution, signed kernels and R5900 per-tap casts on GS, repeat/transform and owned alias snapshots' if args.packed10_convolution else 'not included',
        'packed10_operator_scope':('All53 defined operator IDs, component-alpha HSL canonical Dst; source/mask/target, nearest/bilinear'+('/convolution' if args.packed10_convolution else '')+'32-bit colour, solids and gradients. '+('' if args.packed10_convolution else 'Image convolution, ')+'alpha maps, coverage/16-bit combinations and native depth30 remain unsupported.') if args.packed10_ops else 'basic operators only',
        'native_Xorg_packed10_qualification':'pending' if args.packed10 else 'not included',
        'packed10_scope':(('ARGB/ABGR2101010 and XRGB/XBGR2101010 at depth32; all53 operator IDs, nearest/bilinear'+('/convolution' if args.packed10_convolution else '')+'32-bit colour, solids/gradients; '+('' if args.packed10_convolution else 'image convolution, ')+'alpha maps, coverage/16-bit combinations and depth30 remain unsupported') if args.packed10_ops else 'ARGB/ABGR2101010 and XRGB/XBGR2101010 at depth32; basic Clear..Add, nearest/bilinear, 32-bit colour pictures, solids/gradients; advanced operators, image convolution, alpha maps and coverage/16-bit combinations remain unsupported') if args.packed10 else 'not included',
        'native_Xorg_gradient_qualification':'pending' if args.gradients else 'not included',
        'gradient_geometry':'CPU coordinate planning, radial roots and conical angle; GS colour interpolation and premultiplication' if args.gradients else 'not included',
        'HSL_component_alpha':'Dst, matching pinned pixman' if args.hsl else 'not included',
        'native_redirected_window_backing_alias_qualification':'pending',
        'xorg_server_configuration':build.get('xorg_server_configuration'),
        'expected_divide_channel_checks':24576 if args.divide else None,
        'module_sha256':build['module_sha256'],'kernel_sha256':kernel['kernel_sha256'],
        'source_inputs':build['inputs'],'artifact_sha256':{name:sha(kit/name) for name in files}}
    if args.packed10_alpha_read:
        manifest['packed10_scope']='ARGB/ABGR2101010 and XRGB/XBGR2101010 at depth32; all53 operator IDs, nearest/bilinear/convolution, source/mask alpha maps, solids/gradients and raw snapshots. Writable destination maps, coverage/16-bit combinations and depth30 remain unsupported.'
        manifest['packed10_operator_scope']='All53 IDs with source/mask alpha maps before original-precision filtering and final quantization; component-alpha HSL is canonical Dst. Writable destination maps, coverage/16-bit combinations and depth30 remain unsupported.'
    if args.packed10_alpha_write:
        manifest['native_Xorg_packed10_alpha_write_qualification']='pending'
        manifest['packed10_alpha_write_scope']='Independently quantized writable maps, original destination-alpha scanlines and parent-before-map row publication on GS; raw RGB10/BGR10/A2, decoded coverage/colour and native RGB555 maps. HSL component-alpha retains iterator stores; explicit Dst is a no-op.'
        manifest['packed10_scope']='ARGB/ABGR2101010 and XRGB/XBGR2101010 at depth32; all53 IDs, nearest/bilinear/convolution, source/mask and writable destination alpha maps, solids/gradients and raw snapshots. Coverage/16-bit parents/masks/targets and native depth30 remain unsupported.'
        manifest['packed10_operator_scope']='All53 IDs with original-precision sampling and independent parent/map quantization. Component-alpha HSL retains mapped iterator stores; explicit Dst is a no-op. Coverage/16-bit combinations and native depth30 remain unsupported.'
    if args.packed10_mixed:
        manifest['native_Xorg_packed10_mixed_qualification']='pending'
        manifest['packed10_scope']='All53 IDs at depth32 with ten-bit, A1/A4/A8, byte/word and native depth15 RGB555/RGB444 parents/masks/targets; original-precision nearest/bilinear/convolution, source/mask and writable maps, solids/gradients. Native depth30 and additional formats/filters remain unsupported.'
        manifest['packed10_mixed_scope']='GS original-code normalization, independent output quantization and GS codec storage; shifted different-format byte/word/native15 map aliases preserve parent-before-map row stores. Read-only upload scratch is released before blending.'
        manifest['packed10_operator_scope']='All53 IDs with original-precision sampling and independent parent/map quantization, including mixed coverage/16-bit formats. Component-alpha HSL retains mapped iterator stores; explicit Dst is a no-op.'
    if args.depth30:
        manifest['native_depth30_scope']='GS CT32 backing with logical30-bit plane masks; XRGB/XBGR10 pixmaps, RGB8 views, all53 operators, filtering and writable-map aliases. Scanout stays in the configured native display format.'
        manifest['native_Xorg_depth30_qualification']='pending'
        manifest['packed10_scope']='Depth32 ARGB/ABGR2101010 and XRGB/XBGR2101010, plus native depth30 XRGB/XBGR2101010; all53 IDs, original-precision nearest/bilinear/convolution and mixed coverage/byte/word/native15 formats, solids, gradients and read/write maps.'
    if args.formats_filters:
        manifest['public_format_filter_scope']='Enumerate all38 native-driver formats. Compare every format as source, mask and destination under nearest, bilinear, convolution, fast, good and best. Fast resolves to nearest; good/best to bilinear in the pinned server. No advertised format is skipped.'
    if args.resources:
        manifest['resource_recovery_scope']='VRAM and handle exhaustion return BadAlloc while subsequent GS fill/XOR/copy/readback remains usable. A no-effect kernel health rejection distinguishes pre-effect capacity from sticky hardware failure; creation is never replayed.'
    if args.front_formats:
        manifest['display_format_scope']='Raw RGB10/A2 and native30 X2 sources to the actual display; bounded GS display captures and native16 texel/row conversion without a display-sized decoded allocation. No CPU pixel arithmetic.'
        manifest['known_unsupported']=[]
        manifest['implementation_scope']='Core X11 and all38 advertised Direct XRender formats, all53 operator IDs, all six public filter names, pictures and writable alpha maps; GS-only pixel processing.'
        manifest['qualification_scope']='This package defines the console qualification workload. Runtime evidence is recorded separately; physical PS2 qualification remains pending.'
        manifest['native_Xorg_public_format_filter_qualification']='pending'
        manifest['all_advertised_XRender_formats_implemented']=True
        manifest['all_advertised_XRender_filters_implemented']=True
    if args.cursor_argb:
        manifest['ARGB_Render_cursors_implemented']=True
        manifest['expected_cursor_GS_pixel_checks']=45056
        manifest['expected_cursor_GS_image_checks']=22
        manifest['cursor_scope']='Premultiplied ARGB words uploaded unchanged; GS conversion and Over blending, exact save-under, clipped motion, suspension, replacement and animated Xorg cursors; maximum64x64.'
    if args.window_sampling:
        manifest['Window_picture_sampling_implemented']=True
        manifest['window_sampling_scope']='Window source/mask backing coordinates, affine/projective transforms, repeat and all six filter names; GS pixel sampling and output.'
    if args.render_glyphs:
        manifest['Render_glyph_masks_included']=True
        manifest['Render_glyph_format_count']=28
        manifest['glyph_probe_dimensions']='7x5; packed10 uses2x1 integration masks, with wider arithmetic qualified separately.'
        manifest['glyph_scope']='All28 advertised glyph-admissible formats at depths1/4/8/16/32, including byte/word colour; read-only upload, individual and aggregate masks, overlap and translucent sources; GS-only raster.'
    if args.render_geometry:
        manifest['Render_geometric_coverage_implemented']=True
        manifest['geometry_scope']='GS sample counting and additive A1/A4/A8 coverage; trapezoids, triangles, strips, fans, direct traps, source anchors, clipping, combined/per-shape masks and zero-mask operator extents. CPU edge stepping emits sample-space geometry; GS computes and packs coverage.'
        manifest['geometry_request_limits']={'trapezoids':8192,'triangles':4096,'mask_max_dimension':2048,'sample_work_budget':16777216}
        manifest['implementation_scope']=manifest.get('implementation_scope','Native GS 2D pixel processing.')+' Render geometric coverage is generated on GS.'
    if not args.cursor_argb:manifest['known_unsupported'].append('ARGB Render cursors are not included in this kit')
    if not args.render_geometry:manifest['known_unsupported'].append('Render geometric coverage is not included in this kit')
    (kit/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (kit/'README.txt').write_text(
        'PS2 GS console test candidate\n\n'
        'This package uses the PlayStation 2 Graphics Synthesizer. It contains no\n'
        'NVIDIA, D3D12, softpipe, llvmpipe, or swrast rendering driver. The small\n'
        'test clients use CPU calculations only as independent pixel oracles.\n'
        'GS executes colour/alpha sampling, filtering, multiplication and writes,\n'
        'including component-alpha masks, A1/A4/A8 and 16-bit core/Render drawing.\n'
        'CPU work\n'
        'includes geometry planning and raw client-byte transfers. Raster access\n'
        'on CPU is rejected for every pixmap depth; unsupported work stops.\n\n'
        'Requirements: the matching eeLinux GNU/Xorg root on USB partition 2\n'
        '(/dev/sda2), a USB keyboard, and a root console login. The kernel retains\n'
        'the existing 720x576p@50 timing. This is not a standalone root filesystem.\n\n'
        '1. Copy EELINUX-GS.ELF to your existing boot FAT partition alongside your\n'
        '   usual kernel, keeping the usual kernel. Launch this new ELF through\n'
        '   your existing wLaunchELF/Tuna boot method. It uses /dev/sda2 as root.\n'
        '2. On the eeLinux root filesystem, extract this archive into a writable\n'
        '   directory, e.g. /root. Log in on the PS2 console and exit Xorg.\n'
        '3. cd /root/ps2-gs-console-test\n'
        '   ./run-console-test.sh\n'
        '4. To run only the production multiplication primitive:\n'
        '   ./run-console-test.sh exact-multiply\n'
        '5. Focused GS/Xorg modes: alpha-maps, alpha-windows, alpha-aliases.\n'
        '   The divide mode runs the native integer arithmetic diagnostic when included.\n'
        '   screen checks 27 target layouts, three filters and scalar/component-alpha\n'
        '   masks, plus native15 differing-format writable aliases, when included.\n'
        '   Run ./run-console-test.sh screen to select that matrix.\n'
        '   The exclusion mode checks the same layouts and aliases with Exclusion.\n'
        '   The multiply mode checks the same layouts and aliases with Multiply.\n'
        '   order-blends checks Darken, Lighten and Difference over that matrix.\n'
        '   piecewise checks Overlay and HardLight (396 Xorg requests);\n'
        '   piecewise-arithmetic checks 417792 raw GS channel results.\n'
        '   pd-families checks disjoint/conjoint Clear/Src/Dst and DisjointOverReverse\n'
        '   (1412 requests, including RGBA16 solids and two rounding counterexamples).\n'
        '   pd-aliases and disjoint-over select subsets of 1188 and 224 requests.\n'
        '   pd-ratios checks the remaining 17 disjoint/conjoint operators (104 requests including two opaque Saturate regressions).\n'
        '   dodge-burn checks ColorDodge/ColorBurn (30 requests including branch boundaries).\n'
        '   softlight checks SoftLight (20 requests including exact thresholds and raw pixels).\n'
        '   hsl checks all four HSL modes (66 requests including clipping, channel ordering and component-alpha Dst).\n'
        '   packed10-ops checks34 additional operators across68 focused native requests.\n'
        '   packed10-convolution checks64 signed-kernel requests with original RGB10/A2, masks and aliases.\n'
        '   packed10-alpha-read checks32 requests with source/mask maps, original A2, decoded coverage, map-only ten-bit routing and aliases.\n'
        '   depth30 checks64 parent/map images, including shifted raw30 aliases.\n'
        '   formats-filters checks684 requests:38 advertised formats x6 public filter names x3 operand roles.\n'
        '   front-formats checks36 requests against the actual display with ten-bit sources and all three sampling filters.\n'
        '   resources checks8 images after two expected BadAlloc rejections (VRAM and handles).\n'
        '   packed10-mixed checks126 parent/map images across63 mixed coverage/colour requests.\n'
        '   packed10-alpha-write checks64 parent/map images across32 requests, including shifted aliases and HSL component-alpha stores.\n'
        '   packed10 checks four ten-bit layouts, thirteen basic operators, bilinear, masks, aliases and procedural pictures (72 requests).\n'
        '   gradients checks linear, radial and conical sources/masks (72 requests including poles, packed targets and writable maps).\n'
        '   order-arithmetic checks 626,688 GS byte channels, overflow and in-place outputs.\n'
        '   The default all mode includes the window/alias tests.\n'
        '   When included, saturate runs the focused Xorg matrix and\n'
        '   wide-saturate runs the separate GS Saturate probe.\n'
        '   packed-saturate checks both conversion directions and component-alpha\n'
        '   masks for four byte and ten word colour layouts with nearest sampling.\n'
        '   bilinear-saturate checks the full-16-fraction source/mask path,\n'
        '   including component alpha, channel ordering and a source alias.\n'
        '   map-saturate checks source/mask maps and independently encoded\n'
        '   colour/alpha destination maps with nearest and bilinear sampling.\n'
        '   shifted-maps checks scanline-ordered same-format parent/map aliases\n'
        '   with basic operators and nearest/bilinear Saturate.\n'
        '   native15-aliases checks native RGB555/RGB444 shared backing,\n'
        '   with GS-only CT16/packed transfers and per-scanline view refresh.\n'
        '   format-aliases checks shared raw byte/word backing, padding bits,\n'
        '   scanline store order, shifted origins and filtered Saturate.\n'
        '   convolution-saturate checks signed source/mask kernels, packed colour,\n'
        '   A1/A4/A8 targets and shifted writable alpha-map aliases.\n'
        '   window-sampling checks transformed and repeating Window sources and masks.\n'
        '   render-glyphs checks all28 advertised glyph-admissible formats and aggregate masks.\n'
        '   render-geometry checks GS trapezoids, triangles, strips, fans and direct traps.\n'
        '   cursor-argb checks colour, converted, transparent and animated cursors.\n'
        '   cursor-arithmetic compares 45,056 visible GS pixels against pixman.\n'
        '6. Return the logs/ directory. Each run records its own results.\n\n'
        'The script verifies file hashes and runs primitives before Xorg. It\n'
        'loads the candidate Xorg module from this directory; no system module\n'
        'is replaced. A failed check stops the test and preserves its logs.\n'
        'Reboot your usual kernel to return to your existing setup.\n\n'
        f'Expected Xorg result: completed pixel_checks={args.checks} errors=0.\n'
        f"Additional focused window/alias results: {focused['WINDOW']} and {focused['ALIAS']} checks.\n"
        f"Additional focused Saturate result: {focused['SATURATE']} checks (0 means not included).\n"
        f"Additional focused packed Saturate result: {focused['PACKED']} checks (0 means not included).\n"
        f"Additional focused bilinear Saturate result: {focused['BILINEAR']} checks (0 means not included).\n"
        f"Additional focused coverage Saturate result: {focused['COVERAGE']} checks (0 means not included).\n"
        f"Additional focused solid Saturate result: {focused['SOLID']} checks (0 means not included).\n"
        f"Additional focused alpha-map Saturate result: {focused['MAPSATURATE']} checks (0 means not included).\n"
        f"Additional focused shifted writable map result: {focused['SHIFTED']} checks (0 means not included).\n"
        f"Additional focused convolution Saturate result: {focused['CONVOLUTION']} checks (0 means not included).\n"
        f"Additional focused Screen result: {focused['SCREEN']} checks (0 means not included).\n"
        f"Additional focused Exclusion result: {focused['EXCLUSION']} checks (0 means not included).\n"
        f"Additional focused Multiply result: {focused['MULTIPLY']} checks (0 means not included).\n"
        f"Additional focused Darken/Lighten/Difference result: {focused['ORDERBLENDS']} checks (0 means not included).\n"
        f"Additional focused disjoint/conjoint result: {focused['PDFAMILIES']} checks (0 means not included).\n"
        f"Additional remaining disjoint/conjoint result: {focused['PDRATIOS']} checks (0 means not included).\n"
        f"Additional ColorDodge/ColorBurn result: {focused['DODGEBURN']} checks (0 means not included).\n"
        f"Additional SoftLight result: {focused['SOFTLIGHT']} checks (0 means not included).\n"
        f"Additional HSL result: {focused['HSL']} checks (0 means not included).\n"
        f"Additional ten-bit operator result: {focused['PACKED10OPS']} checks (0 means not included).\n"
        f"Additional ten-bit convolution result: {focused['PACKED10CONVOLUTION']} checks (0 means not included).\n"
        f"Additional ten-bit alpha-map read result: {focused['PACKED10ALPHAREAD']} checks (0 means not included).\n"
        f"Additional depth30 result: {focused['DEPTH30']} checks (0 means not included).\n"
        f"Additional public format/filter result: {focused['FORMATFILTER']} checks (0 means not included).\n"
        f"Additional display format result: {focused['FRONTFORMAT']} checks (0 means not included).\n"
        f"Additional Window sampling result: {focused['WINDOWSAMPLE']} checks (0 means not included).\n"
        f"Additional Render glyph result: {focused['GLYPH']} checks (0 means not included).\n"
        f"Additional Render geometry result: {focused['GEOMETRY']} checks (0 means not included).\n"
        f"Additional resource recovery result: {focused['RESOURCE']} checks (0 means not included).\n"
        f"Additional mixed ten-bit result: {focused['PACKED10MIXED']} checks (0 means not included).\n"
        f"Additional ten-bit alpha-map write result: {focused['PACKED10ALPHAWRITE']} checks (0 means not included).\n"
        f"Additional ten-bit result: {focused['PACKED10']} checks (0 means not included).\n"
        f"Additional gradient result: {focused['GRADIENT']} checks (0 means not included).\n"
        f"Expected all-mode Xorg total: {manifest['expected_all_Xorg_checks']} checks.\n"
        'Xorg must terminate successfully (0); primitive counts are checked too.\n'
        + ('Core X11 and all38 advertised Direct XRender formats have GS implementations,\n'
           'including all53 operators and all six public filter names. Runtime qualification\n'
           'evidence is recorded separately from this console workload package.\n'
           if args.front_formats else
           'Full all-format XRender 2D acceleration is not complete. Additional\n'
           f'formats, {remaining_operators},\n'
           f'{remaining_saturate} remain outstanding.\n')
        + ('Gradient geometry runs on CPU; pixel interpolation, premultiplication and conversion run on GS.\n' if args.gradients else 'Gradients remain unsupported.\n')
        + ('Wide convolution uses the R5900 per-tap unsigned conversion and signed final clamp.\n' if args.convolution_saturate else
           'Coverage convolution remains unsupported;\n')
        + ('coverage bilinear and component-alpha Saturate have a separate focused client.\n' if args.coverage_saturate else
           'filtered coverage Saturate is not included in this kit.\n') +
        ('Differing byte/word and native depth-15 writable aliases use GS raw-word synchronization.\n'
           'Window/backing identity uses validated storage; native redirected-alias qualification is pending.\n'
           if args.native15_aliases else 'Differing byte/word writable aliases use GS raw-word synchronization.\n'
           'Native depth-15 differing aliases are not included.\n' if args.format_aliases else
           'Differing converted-format writable aliases remain unsupported.\n') +
        'Same-format shifted writable aliases use GS scanline staging.\n'
        'The main matrix omits 40 filtered coverage Saturate checks. The focused\n'
        'coverage mode checks bilinear inputs separately when included.\n'
        'No passing physical PS2 result is claimed by packaging these files.\n')
    sums={str(f.relative_to(kit)).replace('\\','/'):sha(f) for f in sorted(kit.rglob('*')) if f.is_file()}
    (kit/'SHA256SUMS').write_text(''.join(d+'  '+name+'\n' for name,d in sums.items()))
    subprocess.run(['sha256sum','--check','SHA256SUMS'],cwd=kit,check=True,stdout=subprocess.DEVNULL)
    archive=args.output/'ps2-gs-console-test.tar.gz'
    with tarfile.open(archive,'w:gz') as tar:tar.add(kit,arcname=kit.name)
    with tarfile.open(archive) as tar:
        for member in tar.getmembers():
            if not member.isfile():continue
            relative=Path(member.name).relative_to(kit.name).as_posix()
            actual=hashlib.sha256(tar.extractfile(member).read()).hexdigest()
            if actual!=sha(kit/relative):raise RuntimeError('archive mismatch: '+relative)
    record={'result':'packaged and hashes verified; console execution pending',
        'archive_sha256':sha(archive),'files_verified':len(sums),'full_2D_complete':False,
        'physical_hardware_verified':False,'target':'PlayStation 2 Graphics Synthesizer'}
    (args.output/'package.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record,indent=2))

if __name__=='__main__':main()
