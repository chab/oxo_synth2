#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/OxoSynth2VST3"
BUILD_DIR="$PROJECT_DIR/build"
# Bundle names from Identity.xcconfig (the local override wins, like in CMake).
IDENTITY_FILES=("$SCRIPT_DIR/Identity.xcconfig")
[[ -f "$SCRIPT_DIR/Identity.local.xcconfig" ]] && IDENTITY_FILES+=("$SCRIPT_DIR/Identity.local.xcconfig")
PRODUCT_NAME="$(sed -n 's/^OXO_PRODUCT_NAME[[:space:]]*=[[:space:]]*//p' "${IDENTITY_FILES[@]}" \
  | tail -1 | sed 's/[[:space:]]*$//')"
PLUGIN_SRC="$BUILD_DIR/VST3/Release/${PRODUCT_NAME// /}.vst3"
PLUGIN_DST_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
# AUv2: the SDK's smtg_target_add_auv2 wraps this same VST3 binary. Its own
# post-build copy hardcodes a literal "~" that /bin/cp does not expand, so
# install it ourselves (same as oxo_simple).
AUV2_SRC="$BUILD_DIR/VST3/Release/$PRODUCT_NAME.component"
AUV2_DST_DIR="$HOME/Library/Audio/Plug-Ins/Components"
export DEVELOPER_DIR="/Applications/Xcode.app/Contents/Developer"

# --no-install: build only, leave ~/Library alone (tools/prepare_mac_installer:
# a ~/Library copy beside the pkg's /Library one = two plug-ins, same UIDs).
INSTALL=1
for arg in "$@"; do
  case "$arg" in
    --no-install) INSTALL=0 ;;
    *) echo "Unknown option: $arg" >&2; exit 2 ;;
  esac
done
# Should the SDK's post-build AUv2 copy ever reach ~/Library (it does in Oxo
# Drums' setup), --no-install undoes it when there was no copy before.
AUV2_DST="$AUV2_DST_DIR/$PRODUCT_NAME.component"
AUV2_WAS_INSTALLED=0
[[ -e "$AUV2_DST" ]] && AUV2_WAS_INSTALLED=1

echo "==> Configuring (Xcode generator)"
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -GXcode -DSMTG_CREATE_PLUGIN_LINK=OFF > "$BUILD_DIR.configure.log" 2>&1 \
  || { tail -30 "$BUILD_DIR.configure.log"; exit 1; }

# Xcode's copy phase only adds: start the bundled banks from scratch so a
# patch removed from Shared/Resources/Synth2 Factory (or Synth2 Starter
# Banks) leaves the bundle too.
rm -rf "$PLUGIN_SRC/Contents/Resources/Synth2 Factory" "$PLUGIN_SRC/Contents/Resources/Synth2 Starter Banks"

echo "==> Building (Release)"
cmake --build "$BUILD_DIR" --config Release -j 8 > "$BUILD_DIR.build.log" 2>&1 \
  || { grep -aE "error:|BUILD FAILED" "$BUILD_DIR.build.log" | head -30; exit 1; }

[[ -d "$PLUGIN_SRC" ]] || { echo "ERROR: built plugin not found at $PLUGIN_SRC" >&2; exit 1; }
if [[ $INSTALL -eq 0 ]]; then
  [[ $AUV2_WAS_INSTALLED -eq 0 && -e "$AUV2_DST" ]] && rm -rf "$AUV2_DST"
  echo "==> Built (--no-install: nothing copied to ~/Library)"
  exit 0
fi
mkdir -p "$PLUGIN_DST_DIR"
rsync -a --delete "$PLUGIN_SRC" "$PLUGIN_DST_DIR/"
echo "==> Installed $PLUGIN_DST_DIR/$(basename "$PLUGIN_SRC")"

if [[ -d "$AUV2_SRC" ]]; then
  mkdir -p "$AUV2_DST_DIR"
  rsync -a --delete "$AUV2_SRC" "$AUV2_DST_DIR/"
  # AUv2 loads its DSP from Contents/Resources/plugin.vst3, a SYMLINK into the
  # build tree. rsync copies the link as is, so the installed AU would break
  # the moment this checkout moves: replace it with the real bundle (what
  # tools/prepare_mac_installer does for the pkg, and what the code seal wants).
  AUV2_INNER="$AUV2_DST/Contents/Resources/plugin.vst3"
  if [[ -L "$AUV2_INNER" ]]; then
    rm -f "$AUV2_INNER"
    rsync -a --exclude '.DS_Store' "$PLUGIN_SRC/" "$AUV2_INNER/"
    # Sealing an added resource invalidates the component's signature, so sign
    # inner-out again (adhoc: a dev install is not a release), like
    # tools/prepare_mac_installer does with the real identity.
    codesign --force --sign - "$AUV2_INNER"
    codesign --force --sign - "$AUV2_DST"
    echo "==> Replaced the AU's plugin.vst3 symlink with a real, signed copy"
  fi
  echo "==> Installed $AUV2_DST"
fi
