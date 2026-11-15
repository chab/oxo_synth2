# Linux build (VST3)

A 1:1 port of the Windows VST3: same engine, parameters, state, banks and
editor. The editor is shared with Windows (`DesktopEditor/`): a panel
change is made once and both get it. Here is only its X11 window:

| File | What it is |
|---|---|
| `Source/LinuxEditor.cpp` | The editor's X11 window: a child of the host's window, events and the 50 ms timer from the host's `Linux::IRunLoop`, the cairo double buffer, mouse / wheel / keys, the write dialog's name field (drawn in the panel; Windows uses an EDIT control). |
| `Source/LinuxGdiplus.hpp` | The part of GDI+ the panel draws with, on cairo. Fonts: Segoe UI / Consolas when installed, else the first of Noto Sans, Open Sans, Liberation Sans, DejaVu Sans (and Liberation Mono, DejaVu Sans Mono...), scaled horizontally to the Windows fonts' widths so labels and LCD columns fit as designed. |
| `Source/LinuxPlatform.hpp` | The Win32 names the panel calls (menu building, constants), message boxes / file, folder and colour pickers (`zenity`, else `kdialog`), `xdg-open`, "Show in file manager" (FileManager1 over D-Bus), `curl` for "Check for updates". No X11: the shared header includes it. |
| `Source/LinuxX11.hpp` | The popup menu (X11, modal like `TrackPopupMenu`). |
| `Source/LinuxThemeEditor.hpp` | The Windows theme editor window, drawn with cairo. |
| `Source/LinuxTrash.cpp` | Deletes go to the freedesktop.org Trash (`~/.local/share/Trash`) when the item is on the same filesystem, else they are deleted (as Windows without a Recycle Bin). |

## Building

Debian 12 / Ubuntu 24.04 or later (GCC 12+, CMake 3.25+):

```sh
sudo apt install build-essential cmake pkg-config git \
     libx11-dev libcairo2-dev libfontconfig-dev zlib1g-dev
git clone --recursive https://github.com/chab/oxo_synth2.git
cd oxo_synth2
./build_linux.sh            # builds and installs ~/.vst3/OxoSynth2.vst3
```

`--no-install` builds only; `--headless` builds without the editor (the host
shows its generic parameter panel). Build on the distribution you run it
on: the binary needs the glibc it was built against (the release builds
in a Debian 12 container, see `tools/prepare_linux_installer`).

At run time the editor uses, when present: `zenity` or `kdialog` (dialogs),
`xdg-open` (links, folders), `dbus-send` (show a file in the file manager)
and `curl` (update check). Without a dialog tool, the LCD says so.

## Where the Linux version keeps your files

- Settings and themes: `~/.config/Oxo Music/Oxo Synth2/` (`$XDG_CONFIG_HOME`
  if set), with `themes.json` and a `Themes/` folder as on Windows.
- Bank library: `Oxo Music/Oxo Synth2/` in your Documents folder, the one
  your desktop names in `~/.config/user-dirs.dirs` (`~/Dokumente`,
  `~/Documentos`...), else `~/Documents`.

## Hosts

The editor needs the host's `Linux::IRunLoop` (REAPER, Bitwig, Ardour,
Carla and other current VST3 hosts provide it). It runs on X11 and on
Wayland desktops through XWayland, as hosts embed plug-ins with X11.
