#!/bin/sh
# Installs the VST3 plug-in next to this script.
#
#   ./install.sh               for you: ~/.vst3 (no root needed)
#   sudo ./install.sh --system for every user: /usr/lib/vst3
#   ./install.sh --uninstall   removes it (add --system for the system copy)
#
# Your banks and settings are never touched, also not by --uninstall.
set -eu

here="$(cd "$(dirname "$0")" && pwd)"
dest="$HOME/.vst3"
uninstall=0
for arg in "$@"; do
    case "$arg" in
        --system) dest=/usr/lib/vst3 ;;
        --uninstall) uninstall=1 ;;
        *) sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 2 ;;
    esac
done

bundle=""
for b in "$here"/*.vst3; do [ -d "$b" ] && bundle="$b"; done
[ -n "$bundle" ] || { echo "No .vst3 bundle next to this script." >&2; exit 1; }
name="$(basename "$bundle")"

if [ "$uninstall" -eq 1 ]; then
    if [ -d "$dest/$name" ]; then rm -rf "${dest:?}/$name" && echo "Removed $dest/$name"
    else echo "Not installed in $dest"; fi
    exit 0
fi

arch="$(uname -m)"
so="$(ls "$bundle/Contents/$arch-linux/"*.so 2>/dev/null | head -1 || true)"
[ -n "$so" ] || { echo "This package has no build for your processor ($arch)." >&2; exit 1; }

mkdir -p "$dest"
rm -rf "${dest:?}/$name"
cp -R "$bundle" "$dest/"
echo "Installed $dest/$name"

missing="$(ldd "$dest/$name/Contents/$arch-linux/$(basename "$so")" 2>&1 | grep -E "not found|GLIBC" || true)"
if [ -n "$missing" ]; then
    echo
    echo "WARNING: the plug-in will not load yet:"
    echo "$missing" | sed 's/^/    /'
    echo "Install the missing libraries (Debian / Ubuntu: sudo apt install libx11-6 libcairo2 libfontconfig1 zlib1g)."
    echo "A 'GLIBC' line means this Linux is older than the plug-in supports (see README.txt)."
fi
if ! command -v zenity > /dev/null 2>&1 && ! command -v kdialog > /dev/null 2>&1; then
    echo
    echo "Tip: install zenity (or kdialog on KDE) for the open / save / colour dialogs."
fi
echo
echo "Rescan plug-ins in your host (REAPER, Bitwig, Ardour, Carla...)."
