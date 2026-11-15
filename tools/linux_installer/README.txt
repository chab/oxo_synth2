Oxo Synth2 for Linux (VST3)
===========================

A free, faithful recreation of the Synth1 software synthesizer.
https://oxo-music.com


Requirements
------------

- 64-bit Intel / AMD (x86_64) or ARM (aarch64) Linux.
- glibc 2.36 or newer: Debian 12+, Ubuntu 24.04+, Linux Mint 22+,
  Fedora 37+, Arch and most current distributions.
- A VST3 host with Linux editor support: REAPER, Bitwig Studio, Ardour,
  Carla...
- X11 or Wayland (the editor runs through XWayland there).

The plug-in uses libX11, cairo, fontconfig and zlib, which every desktop
already has. Recommended, for the open / save / colour dialogs: zenity
(or kdialog on KDE), plus xdg-utils and curl.


Install
-------

In a terminal, in this folder:

    ./install.sh

This copies OxoSynth2.vst3 into ~/.vst3, the per-user VST3 folder hosts
scan. No root needed. Then rescan the plug-ins in your host: it appears
as "Oxo Synth2" by "Oxo".

For every user of the computer instead (into /usr/lib/vst3):

    sudo ./install.sh --system

Or by hand: copy the OxoSynth2.vst3 folder into ~/.vst3.


Uninstall
---------

    ./install.sh --uninstall     (or: sudo ./install.sh --uninstall --system)

Your banks and settings stay where they are:

- Banks:    ~/Documents/Oxo Music/Oxo Synth2
- Settings: ~/.config/Oxo Music/Oxo Synth2


Tips
----

- REAPER: if typing in the editor does not work (patch names), enable
  "Send all keyboard input to plug-in" in the FX window's menu.
- Updates: menu > "Check for updates…" in the plug-in, or the website.
