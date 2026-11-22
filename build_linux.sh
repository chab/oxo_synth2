#!/usr/bin/env bash
# Linux VST3 (x86-64 / aarch64): builds Linux_Build/build/VST3/Release/OxoSynth2.vst3
# and installs it into ~/.vst3 (the per-user VST3 folder hosts scan).
#
#   ./build_linux.sh               build + install
#   ./build_linux.sh --no-install  build only
#   ./build_linux.sh --headless    no editor (hosts show their generic parameter UI)
#
# Needs CMake 3.25+, a C++20 compiler (GCC 12+ or Clang 15+), pkg-config and the
# X11, cairo, fontconfig and zlib development packages; see Linux_Build/README.md.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/Linux_Build"
BUILD_DIR="$PROJECT_DIR/build"
PLUGIN_DST_DIR="$HOME/.vst3"

INSTALL=1
EDITOR=ON
for arg in "$@"; do
  case "$arg" in
    --no-install) INSTALL=0 ;;
    --headless) EDITOR=OFF ;;
    *) echo "Unknown option: $arg" >&2; exit 2 ;;
  esac
done

[[ -f "$SCRIPT_DIR/third_party/vst3sdk/CMakeLists.txt" ]] || {
  echo "VST3 SDK missing: git submodule update --init --recursive" >&2; exit 1; }

echo "==> Configuring"
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DOXO_LINUX_EDITOR="$EDITOR" \
  > "$BUILD_DIR.configure.log" 2>&1 || { tail -30 "$BUILD_DIR.configure.log"; exit 1; }

# The resource copy only adds: start the bundled banks from scratch so a
# patch removed from Shared/Resources leaves the bundle too.
PLUGIN_SRC="$BUILD_DIR/VST3/Release/OxoSynth2.vst3"
rm -rf "$PLUGIN_SRC/Contents/Resources/Synth2 Factory" "$PLUGIN_SRC/Contents/Resources/Synth2 Starter Banks"

echo "==> Building (Release)"
cmake --build "$BUILD_DIR" --target OxoSynth2VST3 -j"$(nproc)" > "$BUILD_DIR.build.log" 2>&1 \
  || { grep -aE "error|Error" "$BUILD_DIR.build.log" | head -30; exit 1; }

[[ -d "$PLUGIN_SRC" ]] || { echo "ERROR: built plugin not found at $PLUGIN_SRC" >&2; exit 1; }
if [[ $INSTALL -eq 0 ]]; then
  echo "==> Built $PLUGIN_SRC"
  exit 0
fi
mkdir -p "$PLUGIN_DST_DIR"
rm -rf "$PLUGIN_DST_DIR/OxoSynth2.vst3"
cp -r "$PLUGIN_SRC" "$PLUGIN_DST_DIR/"
echo "==> Installed $PLUGIN_DST_DIR/OxoSynth2.vst3"
