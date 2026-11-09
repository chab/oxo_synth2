; Inno Setup script for the WINDOWS installer: the VST3 plug-in only (there
; is no Windows AU).
;
; Never run this by hand: tools\prepare_win_installer.bat builds the plug-in,
; stages the bundle and passes the /D defines below, all read from
; Identity.xcconfig (+ Identity.local.xcconfig). The defaults only let the
; script open in the Inno Setup IDE without erroring.
;
; The .vst3 is a BUNDLE FOLDER on Windows too (Contents\x86_64-win\<dll> +
; Contents\Resources with the factory and starter banks), so {app} is that
; folder inside the shared VST3 directory, not a program directory. Hosts
; scan the folder, nothing is registered.

#if VER < EncodeVer(6,3,0)
  #error Inno Setup 6.3 or newer is required (ArchitecturesAllowed=x64compatible)
#endif

#ifndef OxoProduct
  #define OxoProduct "Oxo Synth2"
#endif
#ifndef OxoVendor
  #define OxoVendor "Oxo"
#endif
#ifndef OxoWebsite
  #define OxoWebsite "oxo-music.com"
#endif
#ifndef OxoBundleId
  #define OxoBundleId "com.oxo.music.oxosynth2"
#endif
#ifndef OxoBundle
  #define OxoBundle "OxoSynth2.vst3"
#endif
#ifndef OxoDataFolder
  #define OxoDataFolder "Oxo Music\Oxo Synth2"
#endif
#ifndef OxoLibraryFolder
  #define OxoLibraryFolder "Oxo Music\Oxo Synth2"
#endif
#ifndef OxoVersion
  #define OxoVersion "0.0"
#endif
#ifndef OxoBuild
  #define OxoBuild "0"
#endif
#ifndef OxoStageDir
  #define OxoStageDir "..\..\dist\win\stage"
#endif
#ifndef OxoOutDir
  #define OxoOutDir "..\..\dist\win"
#endif
#ifndef OxoOutName
  #define OxoOutName "OxoSynth2-Windows-x64"
#endif

[Setup]
; Stable AppId: the uninstall entry and every upgrade key off it. Derived
; from the bundle id, so a fork (its own OXO_BUNDLE_ID) never upgrades or
; uninstalls the official build. NEVER change it for a given identity: a new
; id makes an update install alongside the old entry.
AppId={#OxoBundleId}.vst3
AppName={#OxoProduct}
AppVersion={#OxoVersion}
AppVerName={#OxoProduct} {#OxoVersion}
VersionInfoVersion={#OxoVersion}.{#OxoBuild}
VersionInfoProductTextVersion={#OxoVersion} ({#OxoBuild})
AppPublisher={#OxoVendor}
AppPublisherURL=https://{#OxoWebsite}/
AppSupportURL=https://{#OxoWebsite}/
AppUpdatesURL=https://{#OxoWebsite}/

; The plug-in folder is FIXED: hosts only scan the standard locations, so a
; user-chosen directory would be an install no DAW can see.
DefaultDirName={commoncf64}\VST3\{#OxoBundle}
DisableDirPage=yes
DisableProgramGroupPage=yes
; Inno 6 hides the welcome page by default. Keep it: it is the only place
; that says "close your DAW first" and where the plug-in goes.
DisableWelcomePage=no
UsePreviousAppDir=no
; Common Files needs elevation. No per-user fallback: a per-user VST3 folder
; is scanned by far fewer hosts.
PrivilegesRequired=admin
; x64compatible (not the deprecated "x64") also covers Windows on ARM, whose
; hosts load x64 VST3s under emulation. Needs Inno Setup 6.3+.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
; [InstallDelete] reaches into {localappdata} ON PURPOSE (stale copies).
UsedUserAreasWarning=no

Uninstallable=yes
UninstallDisplayName={#OxoProduct} {#OxoVersion} (VST3)

OutputDir={#OxoOutDir}
OutputBaseFilename={#OxoOutName}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes

; NEVER close the user's DAW for them: Restart Manager would kill a session
; with unsaved work. The welcome page asks them to close it; a locked file
; still gets Inno's Retry prompt.
CloseApplications=no
RestartApplications=no

[Messages]
WelcomeLabel2=This installs the {#OxoProduct} VST3 plug-in (64-bit) into the standard VST3 folder:%n%n      Program Files\Common Files\VST3\{#OxoBundle}%n%nPlease CLOSE YOUR DAW before continuing: Windows cannot replace a plug-in that a running host has loaded.
FinishedLabel={#OxoProduct} is installed.%n%nStart your DAW and, if it does not find the plug-in, let it rescan its VST3 folders. It appears as an instrument from the manufacturer {#OxoVendor}.%n%nPatch library: Documents\{#OxoLibraryFolder}, created the first time you open {#OxoProduct} (with its starter banks). Add Synth1 banks with Import... in the patch browser, or drop folders and zip files into that folder.%n%nSettings and themes: AppData\Roaming\{#OxoDataFolder}.

[Files]
; ignoreversion: the dll's file version does not move with every build, and
; the banks are plain data: always overwrite.
Source: "{#OxoStageDir}\{#OxoBundle}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[InstallDelete]
; MIRROR, do not merge: a factory / starter patch removed in the repo must
; not survive in an upgraded bundle (build_vst3.sh empties the same folders
; on the mac). The plug-in never writes inside its bundle, so all of
; Contents goes before the [Files] copy. A Synth2_Data folder (portable
; mode) sits NEXT TO the bundle and is never touched.
Type: filesandordirs; Name: "{app}\Contents"

; STALE-COPY TRAP: hosts scan several VST3 folders and duplicate class IDs
; resolve unpredictably (one machine keeps loading an old copy). Only ever
; OUR bundle name, only in the other VST3 folders.
Type: filesandordirs; Name: "{localappdata}\Programs\Common\VST3\{#OxoBundle}"
Type: filesandordirs; Name: "{commoncf32}\VST3\{#OxoBundle}"

[UninstallDelete]
; The bundle folder itself: [Files] only records what it copied, and an
; empty leftover tree would still show up in a host's scan.
Type: filesandordirs; Name: "{app}"
