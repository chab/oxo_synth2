#!/bin/bash
# Compile check of the Windows VST3 sources on a mac, with mingw-w64
# (`brew install mingw-w64`). Not a Windows build (that is clang-cl,
# build_windows.bat): it only catches compile errors / warnings in our code
# before the code reaches a Windows machine. Objects go to build/win_check.
#   tools/win_check/check.sh            our files, warnings = errors
#   VST3SDK=<path> tools/win_check/check.sh   SDK outside third_party/
set -euo pipefail
cd "$(dirname "$0")/../.."
CXX=x86_64-w64-mingw32-g++
command -v "$CXX" >/dev/null || { echo "missing $CXX (brew install mingw-w64)"; exit 1; }
SDK=${VST3SDK:-third_party/vst3sdk}
[ -f "$SDK/pluginterfaces/base/funknown.h" ] || { echo "no VST3 SDK at $SDK (git submodule update --init, or VST3SDK=...)"; exit 1; }
OUT=build/win_check
mkdir -p "$OUT/gen"

# The generated headers the CMake build would write: identity + zconf.h.
value() { sed -n "s/^$1[[:space:]]*=[[:space:]]*//p" Identity.xcconfig | head -1; }
words() { value "$1" | sed -E 's/([0-9A-Fa-f]+)/0x\1,/g; s/,$//; s/ //g'; }
sed -e "s|@OXO_PRODUCT_NAME@|$(value OXO_PRODUCT_NAME)|" -e "s|@OXO_VENDOR@|$(value OXO_VENDOR)|" \
    -e "s|@OXO_VENDOR_WEBSITE@|$(value OXO_VENDOR_WEBSITE)|" -e "s|@OXO_VENDOR_EMAIL@|$(value OXO_VENDOR_EMAIL)|" \
    -e "s|@OXO_MARKETING_VERSION@|$(value MARKETING_VERSION)|" -e "s|@OXO_BUILD_NUMBER@|$(value CURRENT_PROJECT_VERSION)|" \
    -e "s|@OXO_UPDATE_PRODUCT@|$(value OXO_UPDATE_PRODUCT)|" \
    -e "s|@OXO_VST3_PROCESSOR_UID_WORDS@|$(words OXO_VST3_PROCESSOR_UID)|" \
    -e "s|@OXO_VST3_CONTROLLER_UID_WORDS@|$(words OXO_VST3_CONTROLLER_UID)|" \
    -e "s|@OXO_DATA_FOLDER@|$(value OXO_DATA_FOLDER)|" -e "s|@OXO_LIBRARY_FOLDER@|$(value OXO_LIBRARY_FOLDER)|" \
    OxoSynth2VST3/source/OxoSynth2Identity.h.in > "$OUT/gen/OxoSynth2Identity.h"
ZLIB=Windows_Build/Dependencies/zlib-1.3.1
sed -e 's|#cmakedefine \(.*\)|/* #undef \1 */|' "$ZLIB/zconf.h.cmakein" > "$OUT/gen/zconf.h"

FLAGS=(-std=c++20 -c -Wall -Wextra -Werror -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN
       -D_USE_MATH_DEFINES -DRELEASE=1 -I. -I"$OUT/gen" -IWindows_Build/Source -isystem "$SDK" -isystem "$ZLIB")
fail=0
for f in Windows_Build/Source/*.cpp OxoSynth2VST3/source/OxoSynth2Controller.cpp \
         OxoSynth2VST3/source/OxoSynth2Processor.cpp OxoSynth2VST3/source/OxoSynth2Entry.cpp \
         Windows_Build/Tests/*.cpp tools/bench/main.cpp; do
    if "$CXX" "${FLAGS[@]}" "$f" -o "$OUT/$(basename "$f" .cpp).o" 2> "$OUT/$(basename "$f" .cpp).log"; then
        echo "ok   $f"
    else
        echo "FAIL $f"; rg -m 20 '(error|warning):' "$OUT/$(basename "$f" .cpp).log" || true; fail=1
    fi
done
exit $fail
