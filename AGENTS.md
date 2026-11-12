# Synth2 (Oxo Synth2) — catch-up notes

What a new contributor or AI coding agent must know to work on this repo:
rules, architecture contracts, build / test commands, and the MEASURED facts
about the original Synth1 (do not rediscover them). History lives in git;
keep this file current, not chronological.

## Rules

- CLEAN ROOM: the original Synth1 is studied ONLY through its audio output
  and its published AU interface (`tools/s1ref`). Never disassemble /
  decompile the binary, never copy its artwork (e.g. its panel bitmap
  `s1_07.png`).
- PATCHES: no third-party preset FILES in the repo or the products
  (`reference/presets/` is gitignored, local only). Bundling a
  third-party bank needs the designer's explicit permission (sites listing
  banks, e.g. rekkerd.org, do not license them). Patch / bank NAMES are fine
  where they carry technical info, not as decoration (mocks, screenshots:
  neutral names). `EngineCoreTests/legacy_fixture.txt` (values of real
  legacy patches, no names) is technical test data.
- `README.md`'s "Relationship to Synth1" section is agreed text: keep it.
- `Shared/` must stay iOS-clean (mac-only code under `#if os(macOS)`). Fast
  check: `xcrun --sdk iphonesimulator swiftc -typecheck -target
  arm64-apple-ios17.0-simulator -import-objc-header EditorCore/OxoSynth2Bridge.h
  Shared/*.swift` (the snapshot tool needs the same `-import-objc-header`).
- `third_party/` submodules are never edited: SDK fixes are string patches
  on GENERATED copies in `OxoSynth2VST3/CMakeLists.txt` (FATAL_ERROR if the
  anchor moves).
- Commit at every significant step; messages = subject + body, no AI
  attribution trailers. Filter build / test output, never dump full logs.

## What this is

A faithful recreation of **Synth1** (Daichi Laboratory, freeware, modelled
on the Clavia Nord Lead 2; abandoned, its mac AU is x86_64-only and dies
with Rosetta). Same 99 parameters (same indices), loads `.sy1` patches and
zip banks, SOUNDS THE SAME (tuned against renders of the original). GUI =
Synth1's layout, redrawn. MIT, free. Formats: mac VST3 + the SDK's AUv2
wrapper, iPad AUv3 (App Store). Windows / Linux: welcome as ports.
Stack: Steinberg VST3 SDK + header-only C++ engine + SwiftUI editor (no
JUCE). Official builds ("Oxo Synth2", oxo-music.com / App Store) come from
tagged commits of this repo.

## Identity

ONE SOURCE: `Identity.xcconfig` (repo root; forks change it, see README
"Forks"). Gitignored `Identity.local.xcconfig` overrides any key (signing
team, fork test). Xcode = project base configuration (targets use
`$(OXO_BUNDLE_ID)`, Info.plists / entitlements `$(OXO_*)`, Swift reads
`OxoAppGroup` from Info.plist). CMake parses it (`KEY = value` lines, no
trailing comments, `//` cuts values → no URL schemes) → generated
`OxoSynth2Identity.h` (VST3 FUIDs, names, folders) + configured
`au-info.plist`; `build_vst3.sh` and the installer read it too. Code-visible
strings go through `EditorCore/Identity.h` (generated header on the mac
VST3, sandbox fallback elsewhere). NOT identity (never change, compatibility):
state tag `'OXS2'`, `.sy1`, the `com.oxo.music.synth1-patch` imported UTI.
Dev tools (au_harness, ui_snapshot/auhost) hardcode OXS2.

Official values: product `Oxo Synth2`; VST3 `OxoSynth2.vst3`
(`com.oxo.music.oxosynth2.vst3`); AUv2 `Oxo Synth2.component`
(`com.oxo.music.oxosynth2.audiounit`), `aumu` `OXS2` `OXOM`, name "Oxo: Oxo
Synth2"; iPad (iOS 17, iPad only) app `com.oxo.music.oxosynth2`, appex
`com.oxo.music.oxosynth2.AudioUnit`, same AU codes, app group
`group.com.oxo.music.oxosynth2`. The panel title reads "Synth2".
Per-user settings: mac `~/Library/Application Support/Oxo Music/Oxo Synth2`
(`midi_mappings.json`, `themes.json`, `settings.json`), iPad the appex's
container, Windows `%APPDATA%\Oxo Music\Oxo Synth2` (+
`preset_index.bin`, theme files `Themes\*.json`: one flat hex map each,
listed after the saved themes, never written). Bank library: mac `~/Music/Oxo Music/Oxo Synth2`, iPad
`<app group>/Synth2`, Windows `Documents\Oxo Music\Oxo Synth2`. Windows
portable mode: a `Synth2_Data` folder next to the `.vst3` bundle holds all
of it (banks in `Synth2_Data\Presets`).
VERSION: MARKETING_VERSION / CURRENT_PROJECT_VERSION in `Identity.xcconfig`
+ the two plists' AudioComponents integer `version` (by hand, 65536 = 1.0).

## Layout

- `EngineCore/` header-only `oxo::s2`: `Params.hpp` (the 99 params, native
  ranges / defaults = the original's init patch), `Sy1Codec.hpp` (+ legacy
  conversion), `Synth.hpp` / `Voice.hpp` / `Filter.hpp` / `Envelope.hpp` /
  `Lfo.hpp` / `Effects.hpp` / `WaveTables.hpp` (engine), `MeasuredTables.hpp`
  + `LegacyTables.hpp` (GENERATED from `reference/meta`), `InitPatch.hpp`,
  `ParamFormat.hpp` (value labels; `DisplayName` = LCD name, effect knobs
  per effect type), `MidiLearn.hpp` (learn table, CC scaling, JSON),
  `PatchLibrary.hpp` (banks, zip reader, import, `PatchManager`),
  `ProgramChange.hpp` (MIDI program change: current bank snapshot,
  `RtMailbox`, `SwitchProgram`), `TextEncoding.hpp` (legacy names: Shift-JIS / Windows-1252 → UTF-8),
  `Jobs.hpp` (s1ref job format → our engine, WAV writer).
- `EditorCore/` portable editor services: `OxoSynth2Bridge.h` (C hooks
  between C++ and the Swift editor), `SynthEditorCore.hpp` (base of the
  VST3 controller and the AUv3 instance), `State.hpp` (state chunk),
  `Storage.hpp` (per-user files, mac library / factory paths), `Trash.hpp`,
  `Json.hpp` (small strict JSON reader), `Settings.hpp` (settings.json:
  independent top-level keys, a save edits only its value in place; Windows
  zoom, program change; `EngineCoreTests/SettingsTests.cpp`), `Themes.hpp` (the C++ mirror of
  `Shared/SynthTheme.swift` for C++ editors: 13 keys, factory themes,
  lenient hex map, `stateText`, the `themes.json` store;
  `EngineCoreTests/ThemesTests.cpp` PARSES the Swift file, so a key or a
  factory theme changed on one side only fails the tests).
- `EngineCoreTests/` — `make -C EngineCoreTests test`.
- `OxoSynth2VST3/` — processor / controller / entry / editor view, CMake
  (SDK + AUv2 wrapper with the auwrapper.mm patches, Swift
  static lib `OxoSynth2Editor`, links zlib).
- `Shared/` — SwiftUI editor used by both formats: `SynthModel.swift`
  (`SynthHost` protocol + model polling the host at 30 Hz; MIDI learn
  state, LCD readout, wheel gestures, browser keys), `HooksSynthHost.swift`
  (the C hooks as a `SynthHost`), `SynthControls.swift` (widgets: KnobView,
  ToggleButton (LED), CycleButton, Badge, LedRadio, WaveRadio / WaveCycle,
  ValueDisplay, MenuDisplay, SectionBox, `.learnable` hotspots),
  `SynthPanelView.swift` (the panel on a 770x560 canvas, absolute `.place`
  / `.position` layout, BottomBar = LCD + write / menu),
  `SynthLibraryViews.swift` (browser, write dialog, iPad file actions),
  `SynthLibrary.swift` (iPad library / factory paths), `SynthTheme.swift`
  (themes, ThemeDialog, LogoMenu), `SynthWheel.swift` (wheel monitor,
  `WheelFeel`), `SynthTooltip.swift`, `SynthUpdateCheck.swift`,
  `Resources/` (bundled banks, see Patch library).
- `OxoSynth2.xcodeproj` (synchronized folders): `OxoSynth2/` = iPad app,
  ONLY a screen saying "open it in a host" + version, compiles only its
  own folder: no engine, no library (the plugin's browser manages it; an
  app-side import confused users), no app group, no "Open in" document
  types; it keeps the imported `.sy1` UTI declaration +
  `ITSAppUsesNonExemptEncryption` NO. App and appex each carry a
  `PrivacyInfo.xcprivacy` (appex: UserDefaults CA92.1 — declare any new
  required-reason API there). `OxoSynth2AudioUnit/` = AUv3
  appex (`AudioUnit/OxoSynth2AudioUnit.swift` AUAudioUnit shell,
  `AudioUnitViewController.swift` factory + UI, `DSP/Synth2Instance.hpp`
  engine + mailboxes + learn + SynthEditorCore, `DSP/Synth2Bridge.{h,mm}`
  C API incl. the render loop) + `Shared/`.
- `tools/au_harness/main.mm` — mac check of the AUv3 C bridge (command in
  the file): render, MIDI 1 + 2 (UMP), learn, state, library hooks.
- `tools/ui_snapshot/` — `main.swift` renders the panel with a mock host to
  PNG (modes: logo, update, …); `auhost.swift` opens the INSTALLED AUv2
  in-process and snapshots the real editor. Build commands in the files.
- `tools/app_icon/render.swift` — draws the app icon (command in the file).
- `tools/prepare_app_store_screenshots.sh` — raw iPad captures dropped in
  `tools/app_store_screenshots/` (gitignored) → the two App Store sizes +
  the website's `gallery_N.jpg`.
- `tools/unify2sy1.py <file|folder>... -o <dir>` — extracts Synth1 patches
  from Unify presets (JUCE "VC2!" XML → base64 `stateInformation` → the
  "Synth1 VST Chunk Data" blob, same word layout as the AU chunk); name =
  layer title, version and raw values kept (converted / clamped on load).
  One bank per library, `NNN_<preset>.sy1`, > 128 patches split "<bank>
  (1)", "(2)".
- `tools/s1ref/` — black-box harness for the ORIGINAL + measurement / fit
  scripts (its README: chunk layout, param mapping, process-wide state).
- `tools/s2render/` — renders the same jobs through our engine.
- `tools/prepare_mac_installer`, `tools/mac_installer/` — pkg / dmg.
- `reference/meta/` — committed measurements (params.json, nativemap.txt,
  the tables' sources). `reference/renders/` + `reference/derived/`
  (download, README "Reference data") and `reference/presets/` are
  gitignored.
- `Windows_Build/` — Windows x64 VST3 port (contributed, PR #3): CMake
  project (parses `Identity.xcconfig` like the mac one, vendored zlib
  1.3.1 = byte-identical upstream), `Source/` = native Win32 / GDI+ editor
  (`WindowsEditor.cpp`: the SwiftUI panel's layout redrawn; static layer
  cached in a bitmap, dynamic parts redrawn in the invalid rect only,
  `TextCache` for outlines, parameter values polled like SynthModel),
  `WindowsThemeEditor.hpp` (ThemeDialog as a window), `WindowsModule.hpp`
  (window classes on OUR module, unregistered with the last window),
  `WindowsTrash.cpp` (Recycle Bin; false where Windows cannot recycle),
  `WindowsStorage.hpp` / `StorageSelection.hpp` (folders, portable mode),
  `WindowsPresetIndex.hpp` (bank index cache, rescanned in the background
  on every editor / browser open), zoom, `WindowsPrompt.hpp`. The browser
  is the mac's (no favourites: removed), incl. drag and drop.
  Windows-only code in shared files stays under `#if defined(_WIN32)` /
  `SMTG_OS_WINDOWS`.
- `docs/images/` — README pictures (`synth2_preview.jpg`).
- `third_party/` — submodules `vst3sdk` v3.8.1_build_84 (MIT) +
  `AudioUnitSDK` 1.4.0 (Apache 2.0); remaining patches: auwrapper hidden
  params + program count (SDK bug: read from parameter 0) + AudioUnitSDK
  deployment target.

## Builds and tests

- `./build_vst3.sh` → builds and installs `OxoSynth2.vst3` + `Oxo
  Synth2.component` into ~/Library/Audio/Plug-Ins. `--no-install` = build
  only. Validator 47/47 (`OxoSynth2VST3/build/bin/Release/validator
  <bundle>`; run it with `HOME=$(mktemp -d)` to keep your library out of
  it), `auval -v aumu OXS2 OXOM`.
- iPad: `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
  xcodebuild -project OxoSynth2.xcodeproj -scheme OxoSynth2AudioUnit
  -destination 'generic/platform=iOS Simulator' CODE_SIGNING_ALLOWED=NO
  build` (0 warnings in our code; `CLANG_WARN_COMMA = NO` — the engine's
  compact comma style). Then `tools/au_harness`.
- `make -C EngineCoreTests test`, `make -C tools/s2render`, CPU bench
  `clang++ -std=c++20 -O2 tools/bench/main.cpp -o build/bench && ./build/bench`
  (16 voices, one core, Apple M4: LP12 x135, LP24 x109, LPDL x35,
  detune+sub x88, all fx x90 realtime (it runs the a.d.1: x83 with its tabulated curves, x74 with the exact ones' pows, same session); `./build/bench <x.sy1>...` = a
  keyboard scrub, ~31 voices: Madame Eljay 010 x46, the heaviest case
  reported),
  `make -C tools/s1ref` (x86_64, needs the original AU).
  VS THE ORIGINAL: time both as x86_64 under Rosetta (the only Synth1 we
  can run; its ratio, 3.4x before, was close to a real Windows PC's DAW
  meter, 4.4x; same session only, the machine's speed drifts ~10 %):
  `./build/bench --job x.sy1 /tmp/s.wav > /tmp/s.txt` (the scrub as a
  job), `S1REF_TIME=1 build/s1ref/s1ref batch /tmp/s.txt` (original), a
  `-arch x86_64` bench `./build/bench_x64 /tmp/s.txt` (ours; any job
  file). 2026-11-10, Madame Eljay 010 (osc1 pulse + detune, sine sub an
  octave down, osc2 pulse, BP12, 4-voice unison): Synth1 x33, ours x23
  (x17 before the copies' table lookups, x13 before the per-case voice
  loop, x9 before the SIMD work); without its detune x40 / x31, without
  detune, sub, LFO and fx x46 / x37. The detune copies: ours 12 ms of
  CPU per second of audio (31 voices; 27 as interpolated reads), the
  original 5. Rosetta can't show denormal costs (ARM hardware), MSVC's
  code or a PC's caches / ports: time a PC with `build_windows.bat
  --bench`. Instruction counts per run: macOS `/usr/bin/time -l`
  ("instructions retired"; the voice runs at IPC ~6 on M4, so cost ~
  instruction count: per voice-sample 245 for two oscillators + SVF, 310
  for that patch without detune, 490 with; arm64 counts every double
  literal the loop rematerializes as 5). Line-level profile: `xcrun
  xctrace record --template 'Time Profiler' --launch -- <bin>`, export
  the time-profile table, leaf addresses through `atos -i` (no perf on
  mac; samples skid onto long dependency chains: trust instruction-count
  A/B over the line profile). Timing the ORIGINAL per feature tells how
  it works (its pulse copies cost 2x its saw copies = two reads; its sub
  copies cost the same for the four sub shapes = one read each).
  TRIED, NO GAIN (2026-11-10): linear interpolation for the copies (-2.5
  ..5 % instructions: an interpolated read costs its gather, not its
  order); skipping the filter's coefficient lookup when the cutoff did
  not move; `[[unlikely]]` on the SVF limiter's pow.
  ENGINE CPU CONTRACTS: wave tables store each point's Hermite cubic (one
  load per read, 11 MB at 44.1 kHz shared per process) for osc1, osc2
  and the sub; osc1's and the sub's DETUNE COPIES are plain lookups, as
  in the original (Osc below): `WaveTableSet::CopyTable`, 2048 points
  per note and shape (+ 3 MB), the phase's top 11 bits, no
  interpolation, summed by `Voice::CopiesSum` (a third of an
  interpolated read's cost). Their 64-bit fixed-point phases
  (`Phase64`, free wrap) are ONE array shared by osc1's and the sub's
  copies (`copyPhase_`, sub octave = half rate, osc1's = `<< 1`),
  stepped through `EngineCore/Simd.hpp` `P4` (SSE2 / NEON / plain C++,
  `-DOXO_SIMD_SCALAR` forces the latter: clang builds that from scalar
  source, MSVC does not). THE VOICE LOOP IS COMPILED PER CASE: `Voice::RenderChunk` picks
  one of 18 `RenderChunkAs<kOsc, kDetune, kFilter>` (osc static / moving
  / cross = FM, ring, sync; detune; SVF / ladder / diode), same
  statements in each (`if constexpr`), so they render bit-identically.
  With every feature as a run-time test the loop carried ~1.6x the
  instructions. The oscillators' modulation (steps, pulse width, FM
  index: mod env, LFOs on pitch / pw / FM) is computed AHEAD per chunk
  into arrays, each part in its own loop and only when routed (the mod
  env runs ahead there: restored when the voice ends mid-chunk);
  envelopes, filter and the LFOs on cutoff / amp / pan stay in the loop.
  NO LIBRARY CALL SITE in the voice loop unless its case needs it: a
  never-taken `pow` (the pan LFO's cube) cost every voice ~40
  instructions per sample (live values spilled around it); floor / trunc
  are calls on x64 SSE2 (MSVC), so `Wrap` calls floor only out of range,
  a pulse's second read wraps by compare (`WrapNext`), pitch steps
  truncate by cast. Delay / chorus indices wrap by compare (`% size` = a
  64-bit division). Everything the loop calls is `OXO_INLINE` (forced).
  Per-chunk exps are cached (`Adsr::Configure`, detune ratios).
  `Synth::Render` flushes denormals (FTZ / DAZ, x86). Keep the voice in
  ONE per-sample loop (splitting the filter into its own pass made it
  latency-bound). A value the original plays as exactly 0 must be 0, not
  a fit's residue (FM 0, LFO depth 0): the residue is inaudible but
  keeps the slow path on. Engine changes: render every factory /
  starter / library patch before / after and compare (the per-case loop
  was bit-identical on all 747, arm64 and x86_64, two phrases incl.
  staccato notes reusing voice slots; the x86_64 and arm64 renders
  differ slightly from each other: FMA). Give such a tool ABSOLUTE
  patch paths and make it fail on an unreadable file: a first run
  compared the init patch 346 times (the counts in the commits of
  e237a62 / 62ef63c are from it; right ones: LFO depth 0 changes 102
  patches, FM 0 319).
- Python: `.venv` (numpy, scipy) for `tools/s1ref` scripts.
- WINDOWS: `build_windows.bat [--msvc] [--bench] [--no-pause]` on Windows (Visual Studio 2022+
  C++ workload + components "C++ Clang Compiler for Windows" and "MSBuild
  support for LLVM (clang-cl) toolset"; CMake from `%CMAKE%`, PATH or VS's
  own; generator = CMake's default or `CMAKE_GENERATOR`) →
  `Windows_Build\build\VST3\Release\OxoSynth2.vst3` + storage tests,
  installs nothing. Compiler = CLANG-CL (VS's ClangCL toolset), THE
  RELEASE'S, so dev and release builds match (2026-11-11; it was MSVC by
  default, clang-cl opt-in). `--msvc` = MSVC into
  `Windows_Build\build-msvc` (no clang components; compare the code). A
  build dir whose cache holds the other toolset is reconfigured with
  CMake `--fresh` (an old MSVC `build`); `--bench`
  = also build and run `tools/bench` (CMake target `synth2_bench`, the
  plugin's compiler and options: the scenarios + the bundled Madame Eljay
  010 scrub + `--chords 3`), THE way to time the engine on a PC: Rosetta on
  an M4 shows neither MSVC's code nor a PC's caches / ports, and runs AVX2
  2x slower than SSE2). `bin\Release\synth2_bench.exe` has a static CRT:
  copy it alone to any PC; `synth2_bench.exe --chords [runs]` = 32 voices
  rotating (unison 4, detune + sub, long release), best / median % of one
  core. MSVC vs CLANG-CL (2026-11-10, `--chords`; measured on ARM64
  Windows, where the x64 build runs EMULATED: confirm on a real x86 PC):
  before d89340c MSVC 10.6 %, clang-cl 7.7 %; at 2c1356d (the per-case
  voice loop) 9.1 % / 5.5 %, scrub x12 / x19. MSVC gains far less from
  that loop; clang-cl is ~1.7x faster on everything but LPDL (x28 both).
  On the mac,
  `tools/win_check/check.sh` compile-checks the Windows sources with
  mingw-w64 (`brew install mingw-w64`; warnings = errors; the worktree's
  SDK or `VST3SDK=<path>`). `.bat` files are CRLF (`.gitattributes`).
- WINDOWS INSTALLER: `tools\prepare_win_installer.bat [--skip-build]
  [--skip-validator] [--msvc] [--dynamic-crt] [--no-pause]` on Windows (+
  Inno Setup 6.3+, `winget install JRSoftware.InnoSetup`) →
  `dist\win\<Product>-<ver>-<build>-Windows-x64.{exe,zip}` (zip = exe +
  `tools/win_installer/README.pdf` (SmartScreen walkthrough) + the raw
  bundle). Built with CLANG-CL (`-T ClangCL`, the engine's CPU above;
  `--msvc` = MSVC, not for a release). Own build dir
  `Windows_Build\build-dist-clang` (`--msvc`: `build-dist`; every
  `Windows_Build/build-*` is gitignored), STATIC CRT (/MT, no
  VC++ redist; checked with dumpbin). Validator runs on the staged bundle
  in PORTABLE mode (temporary `stage\Synth2_Data`), so the dev's library
  stays out. Installer `tools/win_installer/synth2.iss`: elevated, fixed
  `{commoncf64}\VST3\<bundle>` (no dir page), AppId = `<OXO_BUNDLE_ID>.vst3`
  (never change), wipes the bundle's `Contents` before copying (mirror) +
  our bundle from the other VST3 folders (stale copies), never touches
  `Synth2_Data` or user folders, `CloseApplications=no` (never kill a DAW).
  Identity / version from `Identity.xcconfig` (+ local), passed as `/D`.
  Signing optional, env `OXO_WIN_SIGN_SHA1` or `OXO_WIN_SIGN_PFX` (+
  `_PASSWORD`), `OXO_WIN_SIGN_TIMESTAMP_URL`; dll signed, then the exe.
- MAC INSTALLER: `tools/prepare_mac_installer [--skip-build]
  [--skip-notarize]` → `dist/mac/OxoSynth2-<ver>-<build>-macOS.{pkg,dmg}`
  (gitignored). Components `auv2` + `vst3` (pkg ids
  `<OXO_BUNDLE_ID>.{auv2,vst3}.pkg`), macOS 14+, universal, bundles NOT
  RELOCATABLE, the AU's nested plugin.vst3 staged as a real copy (in the
  build tree it is a symlink). Never touches home folders (runs as root).
  Templates `tools/mac_installer/distribution.xml` +
  `resources/{welcome,conclusion}.html` (`__PRODUCT__` etc.). Signing env
  `OXO_SIGN_ID_APP` / `OXO_SIGN_ID_INSTALLER` / `OXO_NOTARY_PROFILE`;
  notarization is the default. Under a sandbox `pkgbuild` prints `write:
  Permission denied` lines but succeeds. While testing the pkg, remove the
  ~/Library dev copies (same UIDs twice).
- UPDATE CHECK (mac + Windows): menu > "Check for updates…" (after a
  divider) fetches `https://<OXO_VENDOR_WEBSITE>/releases.json` (`{
  products: { <slug>: { page, mac: { version, build, url }, win: … } } }`),
  looks up `OXO_UPDATE_PRODUCT` (empty = no menu entry: forks, iPad, tools)
  + its platform (`mac` / `win`), compares with this build: newer = higher
  BUILD only (the version is only shown), absent entry = latest. Mac: Swift
  `UpdateCheck` (CFBundleShortVersionString / CFBundleVersion), in-panel
  `UpdateDialog`. Windows: `EditorCore/UpdateCheck.hpp` (same rules and
  texts, `UpdateCheckTests` compares them with the Swift file) +
  `Windows_Build/Source/WindowsHttp.hpp` (WinHTTP GET on a worker that
  closing the editor cancels and joins: no thread outlives the plugin), LCD
  "Checking…", then a message box ("Open the download page?").
  RELEASES: one version line for every platform (Identity.xcconfig; one
  build number = one commit, tag it, never reused); releasing a platform =
  set THAT platform's entry (`oxo_synth2: mac:` / `win:`) in the
  oxo-music.com site's `config/releases.yml`, leave the others: platforms
  may sit on different builds, each check reads only its own entry.

## Parameters and state

- IDs 0..98 = Synth1 index = `.sy1` line index = original AU param ID.
  Values are NATIVE integers (`.sy1` values). VST3 normalization uses the
  SDK's discrete convention (`FromNormalized`).
- 99 = master VOLUME (Synth1's "vol" left of the LCD, `kVolumeParamId`):
  host param (automatable) but NOT a patch param: never in a .sy1, patch
  loads leave it, the state chunk keeps it. 0..127, MEASURED law (Synth1
  chunk word 651, 64 = unity, 32 = -6.9 dB, 127 = +10.9 dB, 0 = silence) =
  `tables::kVolumeGain`. Job line `w <word> <value>` = raw chunk word. Not
  learnable.
- 94 = polyphony (1..32). 86–89 = MIDI ctrl routing (hidden, -1..65535
  like the original AU, -1 = none: src = status<<8|cc e.g. 0xB001, 0xE000
  bend; assign = param index; the menu offers Synth1's 51 targets in its
  order). Naming quirk: params 50 / 51 are called speed / amount sens in
  Params.hpp but 50 drives assign1 (default lfo1 DEPTH); keys kept for
  saved MIDI-learn maps.
- Hidden proxies: pitch bend 1000, channel pressure 1001, CC n = 1100 + n
  (IMidiMapping). Hidden read-only OUTPUT params: 1300 = MIDI learn "last
  CC" token; 1301..1304 = voice activity for the panel's 2x16 grid (8
  voices x 2 bits each, `Synth::VoiceStates`: 0 idle, 1 releasing, 2 held;
  sent on change; AUv3: atomic after each render, hook `voiceStates`).
- MIDI PROGRAM CHANGE (any channel; no bank select): program 0..127 =
  slot 001..128 of the CURRENT bank = the last library program's bank
  (Init / a patch file keep it; fresh instance: the startup bank); an
  empty slot is ignored. `SynthEditorCore::SyncProgramBank` reads that
  bank folder (`ReadProgramBank`, same slots as the browser) when the bank
  or the library revision changes and hands the values to the DSP (VST3
  message "OxoProgramBank", resent on `connect`; AUv3 in-process), which
  switches by itself (`SwitchProgram` = a browser load: delay cleared, fade,
  notes keep playing; editor closed too) and reports the slot (VST3: hidden
  read-only token 1401 + output changes of the changed params; AUv3: atomic
  polled by the hooks / EncodeState) → `AdoptProgramChange` (name, LCD,
  session program). VST3 plumbing (hosts' standard): root unit with program
  list 1400 ("NNN:name", "----" = empty, renamed on bank change +
  notifyProgramListChange), its kIsProgramChange parameter 1400 (not
  automatable, like the SDK sample), `getUnitByBus` = root for every
  channel. The AUv2 wrapper turns that list into AU factory presets (names
  as read at instantiation). AUv3: MIDI 1 0xC0 + UMP MIDI 2.0 program change.
  MENU > PROGRAM CHANGE (checkmark, every platform): off = program
  changes ignored by the DSP. Per-user preference `"programChange"`
  (default FALSE: ignored until checked) in settings.json
  (`storage::LoadProgramChange`), read by each instance when it starts (VST3 processor + controller, AUv3); the
  toggle saves it and tells this instance's DSP (VST3 message
  "OxoProgramChangeOn"), other open instances keep theirs until reloaded.
  Hooks `programChange` / `setProgramChange`, LCD "program change: on/off".
  Checks: `EngineCoreTests/ProgramChangeTests.cpp`, au_harness.
- STATE CHUNK (`EditorCore/State.hpp` `PluginState`; AUv3 fullState key
  "OXS2" = same bytes): `'OXS2'` + u32 version 4 + u32 master volume + i32
  program slot + u32 len + bank folder (relative to the library root) +
  u32 len + theme (the instance's palette, JSON hex map, opaque to C++, ""
  = never set) + `.sy1` text. v1 / v2 restore at volume 64, no program;
  v1..v3 no theme.
- STARTUP PROGRAM (Synth1: program 001 of bankfolder0): a fresh instance
  plays the first patch of the first bank holding one (`FirstProgram`,
  scans only up to that bank; the factory bank comes first; VST3 processor
  + controller and the AUv3 instance each compute it; session state
  overrides). The program location (`SynthEditorCore::Program()`, hook
  `loadedProgram`, VST3 message "OxoProgram") drives the LCD / browser
  selection, survives editor reopen and sessions; Init / a patch file
  clear it.
- LEGACY `.sy1` CONVERSION (MEASURED, `Sy1Codec.hpp` ConvertLegacy +
  `tools/s1ref/sy1.py` convert_legacy): the original converts on load by
  EXACT version: no ver line = 100, 100, 105 / 106, 107 / 108; any other
  ver (incl. 101..104, 109+) is untouched. Tables `reference/meta/legacy.json`
  (`measure_legacy.py`: chunk version word 140) → `gen_legacy.py` →
  `LegacyTables.hpp`. 100 / 105 / 106: decay / release times, filter amount
  (unipolar → bipolar), delay level → d/w, chorus time / rate / feedback;
  100 also attack, sustain, gain, velocity, LFO speed, osc2 fine; params
  the version lacked are FORCED, routing 86..89 → init wheel routing.
  Specials: wheel sens 50 / 51 = 64 + floor(s·(127−b)·63/127²), b = RAW
  lfo1 depth 44 / speed 43; unison voices 93 = 2 poly / 4 mono (38 ≠ 0),
  detune 75 = 48 / 70 (100–106) or a per-mode table (107 / 108). Arp beat
  33 / delay time 35 are NOT converted.
  OUT-OF-RANGE VALUES (`measure_out_of_range.py`): both loaders keep them,
  the original PLAYS each as the nearest in-range value (bit-identical:
  osc2 pitch, filter type 5 / 6, filter amount 128, arp type 0 / 5, arp oct
  4, arp beat 19..64, delay 20..40 = 19, lfo dest 0 = dest 1 (not off),
  chorus type 0 / 5, mod env dest 3 / 4, unison pitch 49..85; booleans 2 /
  3 = on) → plain `ClampValue` in ParseSy1.
  Check: `check_legacy.py [--fixture] [bank folders]` (legacy patches +
  random ones of each version vs the original, 0 mismatches) →
  `EngineCoreTests/legacy_fixture.txt`.

## Patch library (`EngineCore/PatchLibrary.hpp`, tests PatchLibraryTests.cpp)

- ONE library folder we own, no references to outside folders, no zip
  banks read in place. First run creates the root + an empty "User" bank.
- Bank = 128 SLOTS; slot n = file `NNN.sy1` (Synth1 naming; a leading
  number 1..128 counts: "012_Lead.sy1"), other names fill the free slots in
  name order. Every folder with .sy1 files (or empty) is a bank,
  sub-folders are banks "parent / sub" (a folder holding only folders =
  container); loose .sy1 in the root = bank named after the root. Hidden
  names are skipped.
- FACTORY BANK: `Shared/Resources/Synth2 Factory/*.sy1` → mac VST3 bundle
  `Contents/Resources/Synth2 Factory` (CMake resources; the AUv2 nests the
  VST3), iPad appex resources (Xcode explicit folder, excluded from the
  app). Found at run time: mac `storage::FactoryRoot()` (dladdr of our own
  binary), iPad `SynthLibrary.factoryRoot` → `Synth2Create(lib, factory)`.
  Bank "Synth2 Factory" listed FIRST, `readOnly` (Write / Move refuse; no
  delete / drag; write dialog skips it). Updated with the plugin. Program
  bank token `:factory` (`kFactoryProgramBank`), not a path. Its 001 = the
  INIT patch.
- STARTER BANKS (shipped: "Madame Eljay"): `Shared/Resources/Synth2 Starter
  Banks/<bank>/*.sy1` → bundle `Resources/Synth2 Starter Banks` (sibling
  of the factory folder; CMake loop per bank, Xcode explicit folder).
  `PatchManager::Configure(root, factory, build)` → `SeedStarterBanks`:
  ONCE PER BUILD (CFBundleVersion: VST3 `OXO_BUILD_NUMBER` from the
  generated identity header, AUv3 the appex's Info.plist via
  `Synth2Create`), recorded as `build N` in `<library>/.starter_banks`
  (an older name-list file = another build). A new build copies every
  starter bank whose name is free in the library root (hidden temp folder
  + rename, race-safe) as a normal user bank: a deleted one comes back
  with the next install / update, never within the same build. A name
  already taken (the user's bank, even empty, a file, a link) is NEVER
  written into; the final rename is `RenameNoReplace` (RENAME_EXCL: POSIX
  rename would replace an empty folder created meanwhile). Existing copies
  are never updated. Happens on plugin load (the installer never touches
  homes).
- `build_vst3.sh` empties both bundled folders before building (Xcode's
  copy phase never deletes a removed patch).
- A ZIP FOUND IN THE LIBRARY (any depth) on a rescan (load, browser open,
  Refresh) = an import: extracted next to itself, then moved to the Trash
  (`Trash.hpp`; iOS → deleted); LCD shows (10 s) "X.zip
  imported and moved to Trash" (`TakeNotice` → hook `libraryNotice`). A zip
  without .sy1 stays, ignored.
- IMPORT (`ImportItems`, hook `importItems`, the browser's Import…):
  COPIES into the root. Folders copied
  (only .sy1), zips EXTRACTED into a folder of their name (one wrapping
  folder dropped, __MACOSX / dot files skipped, non-UTF-8 names cleaned),
  a Synth1 `zipbank/` folder merges into its parent, loose .sy1 →
  "Imported" bank's next free slots; never clobbers ("Bank 2"). No export
  (Finder). Sessions store the patch, never a bank ref.
- WRITE (Synth1): dialog = banks + 128-slot grid + name field + "New
  bank…" (the only in-app way to make one); writes `NNN.sy1` (replaces a
  differently named file that held the slot), "Overwrite NNN" when
  occupied; default target = the loaded slot if writing back into its
  bank, else the first empty slot. Default BANK = the loaded program's if
  writable; out of a read-only bank (factory) or no bank: "User"
  (`kUserBankName`, the new library's bank). Deleted? The dialog still
  lists "--:User  new" last (all slots free) and the write makes the
  folder (`newBank` then write; Swift `write(newBank:)`, Windows
  `kNewUserBank`). Snapshot modes `write-factory`, `write-new-user`.
- MOVE (browser drag and drop): renames every patch of the bank to its
  `NNN.sy1` in two phases (hidden temp names), an occupied target swaps.
  The browser stays on the bank it shows (so does a rescan landing; only
  opening the browser or loading a program selects the loaded bank).
- COPY (drag a patch onto a BANK row, "+" cursor = `.copy` proposal):
  `PatchManager::Copy` → byte copy into the bank's first free slot as
  `NNN.sy1` (its own bank = a duplicate); hook `copyPatch` → slot, -1 =
  full (LCD: "Bank is full (128 patches): cannot add a patch"), -2 =
  refused. The browser stays on the source bank (favorites workflow). A
  FULL bank refuses the drop up front: forbidden cursor (mac `.forbidden` +
  `NSCursor.operationNotAllowed`, Windows IDC_NO), no highlight, the full
  message on the LCD when the drag enters it.
  Factory patches are draggable for copying only (no move / drop inside
  the factory bank, never a copy into it). Windows: the same in its own
  window (`dragMove` / `dragEnd`: a patch click acts on RELEASE so a press
  can become a drag past SM_CXDRAG; label under the pointer; cursors =
  arrow for a move, arrow + "+" (`makeCopyCursor`, drawn: no stock one) for
  a copy, IDC_NO elsewhere; Esc / capture loss cancel; indices found again
  by bank source at the drop).
- DELETE (`PatchManager::DeleteBank`, hook `deleteBank`; patches: Swift
  `deleteSlot`): mac moves to the Trash, iPad deletes (`Trash.hpp` returns
  false on iOS: a container Trash nobody can empty). A bank = its folder,
  except a folder that also holds sub-folders (other banks) or the root's
  own bank: only its .sy1 files. Never the factory bank, never outside the
  root; the loaded program is forgotten if its bank goes (the sound plays
  on). Bank delete asks first (in-browser `DeleteBankDialog`, Esc cancels).
- Browser header: Import… / Open library folder / Refresh / Close (mac);
  iPad: Import folder… (picker on `[.folder]` ALONE: mixed with file types
  iOS navigates into folders, no Open button) / Import files… (zip / .sy1,
  multiple) / Refresh / Close. Rescans on every open. Bank / slot menu
  (`libraryMenu`): mac right click = [Export bank…,] Show in Finder[,
  Delete / Delete bank…]; iPad long press = [Export bank…,] [Delete /
  Delete bank…] (no delete on the factory bank); Windows right click = the
  mac's menu with "Show in Explorer" (`showLibraryMenu`; bank delete =
  MessageBox confirmation, Recycle Bin). Rename banks =
  Finder (mac only).
- SCAN SPEED (measured on 150 banks x 128 = 19,200 .sy1): `PatchNameCache`
  (process-wide, path + size + mtime) → only new / changed files are
  opened for their name (rescan 60 ms vs 0.5–1.5 s uncached);
  `FolderEntries` lists each folder once and keeps the listing's file type
  (no stat per is_directory). Browser open / Refresh / editor open =
  `PatchManager::StartRescan` (worker thread, joined in the destructor;
  zip expansion under a process-wide mutex) → hooks `startLibraryScan` /
  `libraryRevision` (polled at 30 Hz, adopts the finished scan) /
  `libraryScanning`. The browser opens at once: "Loading…" with no list
  yet, else the previous list + "loading…" in the header until the new one
  swaps in. A library change on the UI thread meanwhile (write, move,
  delete, import, sync rescan: `generation_`) wins, the worker's list is
  dropped. The editor never scans synchronously at open (`syncProgram`
  waits for a list); mutations still rescan synchronously (fast with the
  cache).
- EXPORT BANK (`PatchManager::ExportBank`, hook `exportBank`, FileAction
  `.exportBank(id)`): pick a target folder (mac NSOpenPanel folders only,
  iPad picker on `[.folder]`); a folder named after the bank's (" 2" when
  taken) is created there with the bank's .sy1 files, names kept (slots
  stay), sub-banks left out. Any bank, the factory one too.

## Editor (behaviour contracts)

- Plumbing: editor → C hooks (`OxoSynth2Bridge.h`) → `SynthEditorCore`
  (patch name / serial, patch files, library, `MakeHooks`; subclassed by
  the VST3 controller and the AUv3 instance); Swift side `HooksSynthHost`.
  A patch load = begin/perform/endEdit on all 99 params (host-visible)
  inside one group edit (`SynthEditorCore::GroupEdit`: one undo step in
  hosts implementing IComponentHandler2; AU hosts may still record 99
  steps, no AU equivalent) + `OxoPatchName` message (the processor keeps the name for the state
  chunk); `patchSerial` tells the UI to refresh. Before its edits a load
  calls `PatchLoading()` (VST3 message "OxoPatchLoad", AUv3 atomic; host
  setState / fullState too) → `Synth::ClearDelay`: the delay's echoes fade
  out (15 ms), the line empties (O(1) age check, no fill), its input fades
  back in, so the switch's transient never rings in a long feedback.
  Chorus untouched.
- Panel = Synth1's layout and colours (khaki panel, dark title strips,
  black knobs, green button LEDs, red radio LEDs, wave icon grids, cyan
  displays; Osc / LFO / Wheel-MIDI left, Amp / Filter / Arp middle, Effect
  / EQ / Delay / Chorus / Voice right). Knobs: vertical drag (Shift fine),
  double-click = INIT value, wheel (full range = 1440 pt, Shift 6x finer;
  displays / radios 33 pt per step; one host gesture per burst, ends after
  0.4 s idle). Type / dst / range buttons: click = next, Shift-click =
  back. ctrl src / assign = pop-up menus. Effect type = in-panel 2 x 5 grid
  (`EffectTypeMenu`); phaser level reads feedback %. First click acts in
  an unfocused window (`acceptsFirstMouse`).
- INIT = `EngineCore/InitPatch.hpp` (`kInitPatchValues`, name INIT):
  Initialize patch and the double-click value. Params.hpp defaults stay the
  ORIGINAL's init (.sy1 missing-line fallback, legacy loading, VST3
  defaults).
- Bottom LCD: line 1 "NNN:name" (click = browser) + ◀ ▶ (next / previous
  NON-EMPTY slot, rolls over banks), line 2 = bank "NN:name" or a 1.5 s
  readout ("filter freq: 64", learn / library messages). Windows: the
  same (`WindowsEditor::show` / `flash`: panel edits only, learn, write,
  delete, export, import, all notes off, the library notice 10 s, worded
  "Recycle Bin"). write = write
  dialog; menu = Initialize patch | Load / Save patch file… | MIDI Learn |
  Program change (checkmark) | All notes off | Themes… | Check for
  updates… (mac). The version box in the title band
  opens the GitHub project (`SynthPanelView.projectURL`).
- BROWSER KEYS (mac, `SynthModel.browserKey`): arrows load at once (up /
  down = ◀ ▶, left / right = next column, nearest non-empty slot), Return /
  Enter / Esc close (write dialog: Esc only), Esc closes the logo menu.
  Routed by `Synth2HostingView`: local NSEvent monitor + own keyDown; Logic
  fallback = editor is the front window. Windows: `WindowsEditor::keyDown`
  (WM_KEYDOWN + `IPlugView::onKeyDown`, WM_GETDLGCODE wants all keys; Ctrl
  / Alt / Win = the host's): the same keys, plus Return / Enter in the
  write dialog = Write (the name field forwards Return / Esc); < > and up /
  down roll over banks in list order. Both: the bottom bar's write clicked
  again while the write dialog is open = its Write (save in place: click,
  click).
- MIDI LEARN: menu > MIDI Learn → every control wears a hotspot (dim =
  unlearned, bright = armed, blue + CC badge = learned); tap arms,
  double-tap clears, the next CC (any channel, 1..127) is assigned, one CC
  may drive several params; "Exit MIDI Learn" chip; never survives the
  editor. Table = PLUGIN-GLOBAL `midi_mappings.json` ({param key: cc}),
  empty by default. The controller owns + saves it and sends "OxoMidiLearn"
  (99 x int32) to the processor, which scales a learned CC onto the param
  (`ScaledValueForCC`) and reports an OUTPUT param change; every CC also
  emits the token on param 1300. The CC keeps its engine role (wheel,
  routing). AUv3: handled in the render, same law.
- THEMES: menu > Themes… = in-panel `ThemeDialog` (AUv3 can't open
  windows), dragged by its title bar, own colour editor (no system panel),
  `WheelShield` keeps wheel events off the knobs below. 13 colours
  (`ThemeColor`); the rest derives (`Theme` struct, `@Environment(\.theme)`).
  Factory themes `ThemePalette.factory` (Synth2 = default, Blue, Gray Dark,
  Gray Light, Red, Warm, JP-8000, JP-8000 Dark, Synth1), then user themes (Save / Delete). The
  palette is PER INSTANCE, stored in the host session (state chunk v4,
  hooks `themeText` / `setThemeText`, VST3 message "OxoTheme"), never in a
  .sy1; changing it also becomes the per-user default for new instances
  (`themes.json` "current"); an instance without a session theme adopts
  that default.
- LOGO MENU: click on the "Synth2" title = in-panel menu (`LogoMenu`): UI
  size 75..200 % of 700 pt (= Synth1 at its 150 %; the 770 pt canvas is
  only the drawing grid; shown only when `model.requestEditorWidth` is set
  = mac, the iPad gets themes only), themes. Resize = C callback
  passed to `OxoSynth2EditorCreate` → `plugFrame->resizeView`. Window
  aspect-locked, width in NSUserDefaults `oxo.synth2.editorWidth`.
- TOOLTIPS: only `.oxoTruncationTooltip` on cut patch / bank names in the
  browser (first 0.5 s, then instant while warm); no help tips, no
  `.help()`; hover via `onContinuousHover`.
- iPad: same panel scaled to fit, centred (HEIGHT MODE). UI WIDTH MODE
  (toggle left of the version box, `UIModeToggle`, lit arrow = the leading
  side): the width leads minus two 40 pt invisible side strips
  (`PanelScrollStrip`, 1:1 drag + flick, no rubber band; the panel itself
  never scrolls, knobs own vertical drags), the panel scrolls vertically;
  it falls back to the fit layout when that fits anyway and while the
  browser / write, Themes or Update dialog is open (scroll kept). Mode per
  instance = AU fullState key `OxoUIWidthMode` beside the chunk (no chunk
  change, the VST3 has no mode), last choice seeds new instances
  (UserDefaults `oxo.synth2.uiWidthMode`); `SynthModel.uiWidthModeStorage`
  nil = no toggle (mac). Snapshot modes `ipad` / `ipad-wide`. Hosting
  controller `safeAreaRegions = []` + `.ignoresSafeArea(.keyboard)`; every
  text field sits at the TOP of its dialog so the keyboard never hides it.
- AUv3 plumbing: the AU parameter tree (99
  params, address = index, NATIVE values) is the host truth; observer →
  `Synth2Instance::SetFromHost` (atomic + dirty bit, applied at the next
  render); UI edits → hooks → instance → AUParameter.setValue (.touch /
  .value / .release); render-time automation is sample-accurate. MIDI 1 +
  UMP (MIDI 2.0 scaled to 7 bit). Tempo from host blocks.

## Engine facts (MEASURED on the original — do not rediscover)

Measurement scripts: `tools/s1ref/measure_*.py`, `fit_*.py` → `reference/meta/*.json`
→ `tools/s1ref/gen_tables.py` → `EngineCore/MeasuredTables.hpp` (generated).
Compare: `tools/s1ref/compare.py <group> [substr]` (groups: presets, sweeps,
pitch, filter, velocity; rebuild s2render first). Any .sy1 folder:
`compare_sy1.py <folder> [--per-bank=N] [--seed=S] [--wav=DIR]` (needs the
original AU; keeps both renders for listening).

- Levels: osc full scale 1 internally; output = x * 0.2 * gain[g] * vel.
  Sine/saw/tri amplitude 0.2 at gain 64 vel 127; pulse = (saw(p)-saw(p+d))/2
  (half amplitude), d = pw/254 (127 = square); the FINAL d (after LFO /
  mod env) is clamped to [1.25/254, 1 − 1.375/254]: pw 0 = pw 1, never
  silent. NOISE (`measure_noise.py`): a 1-bit sequence s[n] = s[n-13] ^
  s[n-18] ^ s[n-19] ^ s[n-20] (from 16 zeros, 1, 0, 1, 0; `kNoiseSeed`),
  ±0.6504; per voice slot, carried across notes (frozen while idle) like
  the oscillators; a synced osc1 wrap restarts it (the timbre of synced
  noise = that bit pattern).
- Osc: per-note band-limited tables (EVERY harmonic below Nyquist, alias
  free); osc silent when its note index > 127 (OPEN: osc2 at key 72 with
  pitch +60 = note 132, 16.7 kHz, DOES play in the original, notes 144 /
  156 do not: the limit looks like Nyquist rather than 127 there).
  PURITY (`measure_purity.py`: what a held note carries besides its
  expected lines; LP24, resonance 0): the original's saw aliases a
  little, lines at the harmonics next to k x fs (n ~ fs / f0, folded
  beside DC and the fundamental) at -75..-80 dB re the fundamental,
  falling ~1/f away from k x fs; its pulse 15..30 dB lower; ours -100 dB
  or less. With osc1 DETUNE the original carries INTERMODULATION ours
  has not on LP24: a detuned sine shows a 3rd harmonic (-48 dB at key
  36, detune 26) and lines at odd multiples (9, 11, 17, 21 x) of the
  copy spacing at -30..-47 dB, power outside the 9 expected lines -31..
  -57 dB (ours -140 dB; saw / pulse -45..-53 dB, ours -80..-130). With
  osc1 at 4 % of the mix it falls to -55..-65 dB: level dependent, so
  mostly after the mix, a nonlinearity on the loud detune sum that our
  ladder (linear at resonance 0) does not model. TODO(measure).
  DETUNE COPIES (`measure_copies.py`, osc1 played quietly so the lines
  stand clear of that intermodulation): the original reads osc1's and
  the sub's 8 copies from a 2048-POINT TABLE of the note's band-limited
  wave with the phase TRUNCATED, no interpolation (osc1 itself and the
  sub are interpolated: a sine's image at 2047 x f is at -133 dB =
  linear on 2048 points). Every harmonic h of a copy at f leaves lines
  at (k x 2048 +- h) x f, 1 / (k x 2048) of the copy's fundamental
  whatever h (-66.2, -72.2 dB...: a sawtooth series), none beyond the
  wave's top harmonic; seen to 0.1 Hz / 0.5 dB for sine, saw, triangle,
  pulse (two points of the saw a WHOLE number of points apart) and the
  sub's shapes. Ours reads them the same way (`WaveTableSet::CopyTable`)
  and shows the same series. At 96 kHz the original's saw still stops
  at 22 kHz (key 96: 10 harmonics at both rates; ours fills to Nyquist).
  Fundamentals of all shapes in
  phase (saw falling +1 at phase 0, tri peak at 0.25). osc1 shapes 0 sine 1
  saw 2 pulse 3 tri; osc2 1 saw 2 pulse 3 tri 4 noise; sub 0 sine 1 tri 2 saw
  3 square, sub phase-locked to osc1, auto level w = g/(g+32). Mix
  (`measure_osc_mix.py`, m = mix/127): osc1 (1-w)(1-m), sub w(1-m), osc2
  (1-w)m, all / (1-wm) (sub off = linear crossfade).
  osc2 pitch = integer-semitone table (±60), fine/tune 1 cent/step dead zone
  62..66. kbd track off = fixed 220 Hz. Osc1 detune: main + 8 copies at
  ±1,3,5,7 × d cents, d = 20 v/127, each ×0.297 (sine 0.288). Under FM
  every copy steps ratio·inc + osc1's FM DEVIATION (added as is, NOT
  ratio·(inc + dev)): seen with sync + FM, where the synced osc2's mean
  shifts the pitch (key 36, FM 30: the main at ~90 Hz, the copies spread
  over the UNMODULATED 65 Hz spread, 10.6 Hz, not 14.7; peaks match to
  the bin since 2026-11-11). Without sync it is inaudible (index ±1 %).
  FM: inst f1 = f1 + beta[v]·261.63 Hz·(sr/44100)·osc2_norm (table measured
  at key 60, to ~102): the deviation is a FIXED phase step per sample
  (`measure_fm_key.py`): index halves per octave up (keys and key shift),
  grows with the sample rate. Whole table = deviation step (cycles /
  sample) (1001^(v/127) − 1) / 2000 to ~6e-4 relative: 0.5 = NYQUIST at
  127, the carrier then simply folds (no clamp). Entries 124..127 were
  wrong fits until 2026-11-11 (83.6..102 instead of 71.6..84.3: the
  sideband error oscillates in beta, the fit now grids first). FM 0 = NO FM (sidebands at the -178 dB
  floor; the table's fitted entry 0, 6.6e-6, is noise: `Voice::FmBeta`;
  `measure_zero_knobs.py`, with the LFO depth 0 probe).
  Ring: osc2 ×
  osc1_norm. Ring on = NO FM (any FM knob / route: osc1 unchanged), BUT
  with an FM source in the patch the product takes osc2's PULSE at full
  scale (×2, saw(p) − saw(p+d)) and NOISE at ±1 (saw / tri unchanged):
  FM knob > 0, an LFO on FM (depth > 0, its trough too), mod env on FM
  amount ≥ 67 all note long; amount 65 / 66 only until its FM offset
  reaches 0 (9.2 taus; ours at 7.6, where our mod env lands on 0). It is
  a flag, not the FM value (FM 1 = FM 127). `check_fm.py --ring`: within
  0.5 dB; OPEN: with osc1 sine / tri the original's ×2 is ×1.89 (pulse
  64, residual −24 dB vs 2 × the FM 0 render; pulse / saw osc1 exact).
  Was 6 dB low on pulse until 2026-11-12 (57 library patches, mean spec
  2.57 → 1.38 dB). Ring BUG kept on purpose (`measure_ring_detune.py`): ring +
  osc1 detune + sub on = the sub copies skip the 0.297 gain; no effect
  without detune. That loud sum is NOT bounded in the osc section: the
  bound sits AFTER the filter, before the amp (filtered quietly, cutoff
  35: no compression; amp gain / velocity: none; LP12 = LP24 at cutoff
  127). It is a level-riding gain, not a waveshaper (lines near the
  carriers, almost no 3rd harmonic): the filters' output limiter (Filter
  section) is it (Fairlight ARR Voice, BP12: 2.87 → 0.07 dB). Until 2026-11-12 a 1.7·tanh
  on osc1 + sub modelled it and SATURATED audibly (87 ring + detune + sub
  library patches: mean spec 1.55 → 1.06, 31 better > 0.5 dB, none
  worse). The filters' output limiter (Filter section) bounds it on LP24
  too (ARR on LP24 127: +2.6 → +0.15 dB). Sync hard: NAIVE reset of osc2 to phase 0 (same alias energy
  as the original to 0.1 dB; do not band-limit). SYNC + FM: a sync fires
  whenever osc1's phase DROPS = a forward wrap OR any sample FM drives it
  backward without wrapping (a backward wrap 0 → 1 does not). Synced
  NOISE restarts on −0.65, so once FM step × 0.65 > the pitch step the
  voice locks on a constant (silence after a click; key 60: FM ≥ 54,
  −12.7 FM steps per octave down; nulls −87 dB vs the original).
  Pitched modulators match by level / spectrum, not sample-exact (chaotic).
  OSC1 PHASE = whole 2^-27 cycles (2048-point index + 16-bit fraction),
  each step TRUNCATED (`PhaseStep`; FM steps per sample): measured from
  synced-noise reset times over 3-4 s at keys 30..72 (period fits match
  to 7e-5 samples; exact doubles drift a sample every few dozen cycles,
  and a synced noise cycle shifted by one sample nulls at 0 dB). Osc2
  stays exact (truncated, the FM nulls get 9 dB worse).
  `check_fm.py [--sync]`: osc1 x osc2 shape x FM grid vs the original.
  No sync: every pair within 0.6 dB at every FM (2026-11-11). Sync + FM:
  off by 1..30 dB from FM ~60 (key 48) with saw / pulse / tri osc2. The
  original's osc2 reads +0.02..0.06 HIGHER than ours on the sample right
  after a reset (saw: ours 0 = the band-limited jump's midpoint; triangle
  none), deconvolved from the no-FM sync residual through the chain IR
  (identical-noise fit). Under FM that sample decides whether osc1 steps
  forward or backward (e.g. pulse 64, FM 60, key 48: original runs at
  the plateau rate, ours locks near phase 0). Not resolved: resetting to
  c·incB overshoots even at c = 0.1, a sub-sample reset (overshoot / step
  × incB) is 10x too much, "previous o2 drives FM" is worse. 2026-11-11
  (2): per reset (deconvolved, `h` = the chain IR from the identical-noise
  fit) the extra value varies 0.026..0.069 from one reset to the next
  with no link to osc1's overshoot; it depends on osc1's key (0.053 key
  36, ~0.045 key 48, ~0.02 key 60), not on osc1's shape; a triangle shows
  none (= a tiny positive reset phase, ~1e-5..1e-4 cycle). c = 0.02 (the
  mean) fixes every deterministic plateau case and the triangle column
  but is NEUTRAL on the library (one patch +3 dB better, User/006 3.5 dB
  worse): not shipped. 2048-point linear tables (what the purity /
  copies measurements suggest the original has): no change here.
  2026-11-11 (3), SHIPPED: a forward-wrap sync restarts osc2 at PHASE
  `kSyncResetPhase` = 7e-5 cycle (a phase, not a value: key-dependent
  through the saw jump's slope, ~0 on a triangle). Fitted open loop (FM 0,
  osc2 alone, residual energy): -6 dB at keys 36..72, osc2 saw / pulse /
  tri, pitch ±12 (best 4.5e-5..8e-5 per case; an overshoot term adds
  nothing). A backward-step sync keeps phase 0: the offset there is worse
  above FM ~110. Closed loop (check_fm --sync key 48): FM 40..80 cells
  much better (sine/pulse 60: 29 → 11 dB, tri/pulse 60: 34 → 7), saw
  osc2 at FM ~120 worse (7 → 20 saw/saw, chaotic islands); held-out
  keys 42/54/66: mid FM 2.28 → 1.96 dB, all 3.78 → 3.63. Library (180
  sync patches): mean -0.05 dB, 5 better > 0.5 (Kujashi Deep Other Worlds
  12.4 → 6.5, User/006 8.5 → 7.5), 1 worse (Long Horn Atmo +1.2).
  TRIED, NO GAIN: sync on the 2048-point INDEX dropping (fixes one
  plateau cell, worse on the grid), offset × or ÷ |fm| (the ÷ √|fm| fit
  did not hold on held-out keys), the offset on backward syncs too. The
  scores move ±0.5 dB from tiny changes above FM ~90: judge on held-out
  grids + the library, not one grid. Sample-lock harness: a note's first
  samples are deterministic, so "first sample where |ref − ours| > 2 %"
  shows where a rule diverges (e.g. the backward lock right after a
  reset, sample 64 at key 48 sine/pulse FM 60). Locked
  noise sync (osc1 running backward): the original's backward pitch is
  0.055 % lower = noise as an FM SOURCE is ±0.6500 (audio ±0.65039, the
  locked DC nulls at -87 dB only with that): `kNoiseFm`. With it the
  backward pitch matches to 2e-5 and check_fm's noise columns are within
  0.4 dB (sync, bar 3 cells at 1.7..2.8) / 0.06 dB (no sync).
- The "fixed output lowpass" (bilinear one-pole, analog 22 kHz, NO
  prewarp) is NOT per voice: it is the EQ TONE stage at 64 (see EQ).
- AMP ENV IS A CONTROL-RATE, 11-BIT GAIN outside the attack (measured on
  a locked synced-noise DC, the cleanest envelope probe there is): at every
  256-sample boundary of the HOST block grid it takes floor(2048·env)/2048
  and ramps linearly to the next one (`Voice::AmpGain`); the decay /
  release are then PLAIN exponentials stopping within 1/2048 of their
  target (`Adsr::SetQuantized`; the last step holds 1/2048 for ln 2 tau).
  The first block after a note on is finer (kinks at +64, +107, +193,
  +255 samples from the note's sub-block, ~1 ms steps: modelled per
  sample). OPEN: s1's knot sequence is up to 2 % above a pure exponential
  mid-decay and below it later (decay 62: +2.2 % at -20 dB, -7 % at
  -43 dB): not a fixed tau; the exact law is unknown. A d.d. with high
  drive amplifies these -60..-90 dB steps by ~40 dB (Kaiyoti 027's tail).
- Amp env: linear attack 0.5 ms·2^(v/8.05); decay/release exponential
  tau table (same law); sustain = curved table. Filter env = same times,
  LINEAR sustain. Velocity: dB = -15 (sens/64)(127-vel)/126.
- Cutoff knob GLIDE (`measure_cutoff_glide.py`, job line `pt <s> <id>
  <norm>` = timed AU param): a new cutoff value ramps LINEARLY over 50 ms
  whatever the step (`CutoffRamp`, snaps while silent). Other knobs: gain
  ~instant, res / pw / mix not measured cleanly yet.
- Filter: LP12/HP12/BP12 = Chamberlin SVF ×2 iterations (input held),
  f = 2 sin(pi fc/2fs), fc = 16.7·2^(c/12.624) (c ≤ 125), d = 1-r,
  in gain 1-0.7r. LP24 = 4 bilinear one-poles + unit-delay feedback, fc
  table, k = (0.0297 res - 0.076)·comp[c], in-loop AGC on k (peak
  follower 1.2 ms / 0.2 s, (T/e)^0.883 above T 5.5). OUTPUT LIMITER after
  both (`measure_filter_limiter.py`, 2026-11-12): before the saturation
  and the amp (filtered quietly it does nothing; amp gain / velocity do
  not move it; LP12 = LP24 at cutoff 127): gain (1 + (e/1.038)^11.54)^
  (−0.856/11.54) on a follower of |y| (3.7 ms up, 6.9 ms down), 1 below
  e 0.39 (table `FilterTables::LimitGain`). Level-riding, not a
  waveshaper: lines next to the carriers, H3 −49 dB at 0.6 dB of
  compression. A full-scale sine at key 12 stays within 0.08 dB (the
  original: untouched; softer knees / mean-abs followers cost 0.1..0.25).
  It replaced the SVF's AGC (T 1.427, 1.2 ms / 0.2 s, fitted on
  resonance: its slow release over-compressed loud sums) and the ladder
  AGC's T 1.6 (now 5.5: the res 127 self-oscillation through the limiter
  = the original's 1.72 FS at cutoff 56..96, 1.66..1.11 above, within
  0.6..5 %; it was −3.6 dB). Library (686 patches): 446 change, none
  worse > 0.5 dB, mean spec 0.875 → 0.868; filter corpus 1.52 → 1.36;
  +3 % instructions (bench). OPEN (LP24 res ≥ 120): the original's
  resonance rises faster above res ~115 (self-oscillates from res ~123 at
  cutoff 88, 127 at 48 / 120; ours stalls or starts elsewhere at cutoff ≤
  48), oscillates 1..2 % higher, and while oscillating suppresses the
  input noise ~21 dB (ours ~10): t1_r127 spec still 9..30 dB. LPDL
  (`fit_lpdl.py`, ~0.5 dB): linear 303-style coupled ladder, feedback
  high-passed at 100 Hz, out = (y3+y4)/2, w/k/g tables per res×cutoff
  (fitted on SMALL-signal TFs: `measure_filter.py --types 4 --mix 10 --out
  filter_tf_lpdl_small.npz`, `fit_lpdl.py --in ...`), 2× oversampled
  trapezoidal. NONLINEAR (`measure_lpdl_nl.py`): input x·1.3/g → tanh;
  feedback k·f → tanh(2kf)/2 (NOT a per-stage tanh nor an AGC on k).
  Saturation AFTER filter: y = (1+a)x/(1+a|x|), a table. Order: osc →
  filter → sat → amp. Cutoff mod in knob units: kbd track table pivot key
  48; FILTER ENV (`measure_filter_env.py`) sweeps linearly from base − 2.1
  (env 0) to the env-1 TARGET clamp(base + (amt-64)·1.984·velscale, −2,
  125) (base = knob + kbd track; the TARGET is clamped, not the result: an
  amount past the top still sweeps over the whole attack / decay; amount
  linear down to 0), vel switch scale table.
- MIDI ctrl routing (`measure_wheel.py`, exact on 418 points): src word
  (0xB0nn CC, 0xE000 bend, 0xD000 pressure) ADDS round(v * d * 128 / (63 *
  127)) knob units to the assign param (live, never written to the patch),
  d = sens - 64 above 64, sens - 63 below 63, 63 / 64 = off; clamped.
  Engine keeps `base_` (patch) and `values_` (played). Init patch: CC1 ->
  lfo1 depth / speed at sens 74. Pressure 0..127 like a CC; pitch bend is
  BIPOLAR v = (bend−8192)/64. Targets whose range is not 0..127 (key shift,
  bend range, unison pitch) are NOT routed.
- PITCH MOD RESOLUTION (`measure_pitch_mod.py`): the pitch bend uses its
  7-bit MSB only (trunc((v−8192)/128), ±63 = ±range); bend + LFO pitch
  cents are TRUNCATED toward 0 to 1/128-octave steps (9.375 cents: slow
  bends are staircases). An LFO ON PITCH (`measure_lfo_pitch.py`, LFO1
  on pitch + the same LFO2 on amp = the LFO value at every sample) plays
  whole steps of ITS OWN, added to the bend's truncated steps (a bend
  between two steps leaves the LFO's thresholds where they were) and to
  the other LFO's: N = the square's steps at that depth
  (`Lfo::PitchSteps` = kLfoPitchCents / 9.375, whole numbers), the law
  PER WAVEFORM (v = -1..1), same at every speed, depth, destination:
  saw = 2N + 1 equal bands of v; triangle = rising floor(N v), falling
  ceil(N v) (a step is taken when the wave reaches it: depth 1 sits on -1
  until the wave crosses 0); square ±N; sine = sign(v) floor(N |v| +
  1/4); S&H / smooth random floor(N v) (-N .. N - 1). Computed once per
  LFO in `Lfo::Render` (steps buffer, only when routed to pitch), summed
  in the voice's pitch pre-pass (`PitchRatio` takes steps). Until
  2026-11-11 ours truncated bend + LFO cents together: a triangle / sine
  at depth 1 hardly moved (fixed; every patch without an LFO that can
  reach pitch renders bit-identically). Fine tune and the mod env's pitch
  are not quantized; the portamento has its own steps (ceil, relative to
  the target). An LFO at DEPTH 0 is no route at all (kLfoPitchCents[0]
  is fit noise). OPEN: the original's first smooth-random cycle after the
  instance starts ramps down from ~+13 steps whatever the depth (a start
  value outside -1..1?), not modelled.
- Mono / legato: a new note retriggers in mono (new velocity), glides in
  legato; releasing it falls back to the previous held key, mono
  RETRIGGERS there (last velocity), legato does not. A mono retrigger also
  restarts the osc phases (osc phase > 0).
- EVENT TIMING: the original starts / ends notes at the start of their
  16-sample sub-block (host-block relative: up to 15 samples early); bend /
  CC are not snapped. Ours too (`NoteEventOffset`: VST3 processor, AUv3
  render loop, s2render).
- PORTAMENTO (`measure_porta.py` → `kPortaTau`): every 16 samples the
  distance r ← max(r·a − E·(1−a), 0), a = exp(−16/(tau·sr)) (tau in s, same
  at any rate), E = 1.35 % of the glide's starting distance past the
  target (lands in finite time, ~310 ms at 64 whatever the interval); played pitch = target + ceil(r / 9.375
  cents) steps (1/128 octave: slow glides end in audible steps). A voice
  glides from ITS OWN last pitch (never used: MIDI note 0, so the first
  note after loading sweeps up from far below); mono / legato always use
  voice slot 0 (unison 0..N-1, also while the last note releases); auto
  mode: mono legato notes only, never poly.
- Voice allocation: ROUND ROBIN (next idle voice after the last started).
  Stealing: the oldest RELEASED voice first, then the oldest held one
  (age = start time in samples). A stolen voice still sounding keeps its
  filter / output state and a free-running sub (resetting them clicked).
  Poly: a key pressed again while held releases its voice (stack), the
  note starts on a fresh one.
- VOICE END (`measure_noise.py`, noise position = slot sample counter):
  decay / release aim 1/2048 BELOW their target and stop on it
  (`measure_env_tail.py`: the tail falls faster than a pure exponential,
  -60 dB ~7 % earlier; a decay to sustain 0 = silent but still held), the
  release end = the env end, the voice at the first
  256-sample boundary after (host block relative, `Synth::BeginBlock(frames)`)
  or the host block's end if sooner (NOT measured, assumed; required:
  without it host blocks < 256, e.g. AUM at 64 / 128, never end a voice).
  The original then leaves a -108 dB residue for 8960 samples (not
  modelled).
- Patch changes under HELD KEYS keep the notes playing (hold a note, browse
  patches). A patch LOAD (editor / browser: `PatchLoading` → VST3
  "OxoPatchLoad", AUv3 flag) also fades the voice mix (pre effects): 25 ms
  out, the edits made meanwhile land at silence (voice filters cleared, no
  cutoff glide), hold while edits keep coming (10 ms quiet, 150 ms max),
  250 ms in (`Synth::BeginPatchChange`; host automation is not faded). Stuck-free rules (fuzz test
  `RandomPatchSwitchesUnderHeldKeysLeaveNoStuckNote`): the mono key stack
  follows every key in EVERY play mode; All Notes Off / Reset clear it and
  the arp keys; arp on takes the held keys, arp off gives them voices back.
- Sustain pedal CC 64 (>= 64 down): released keys held (arp keys too)
  until pedal up; CC 120 / 123 clear held keys. Not measured.
- LFO: global, free-running from instance creation; types 0 saw↓ 1 tri
  (-1 at 0) 2 square (+ first) 3 S&H 4 smooth 5 sine; Hz = 2^(s/12.0367)/12 (`measure_lfo_speed.py`, 0.02 %);
  dests 1 osc2 pitch 2 osc1+2 pitch 3 cutoff 4 amp 5 pw 6 FM 7 pan.
  RANDOM TYPES (`measure_lfo_random_keysync.py`, LFO on amp at depth 127
  = the envelope is the LFO; cutoff = a saw's spectral centroid; pitch;
  pan; pw): one random r per cycle; S&H plays r, smooth random ramps
  LINEARLY from the PREVIOUS cycle's r to r (reached at the cycle's end:
  continuous, kinks at the cycle starts) on pitch, cutoff, pw, pan. On
  AMP ONLY it ramps from -r to minus the previous r and jumps at every
  cycle start (`Lfo::Render(..., amp)`). A KEY-SYNC restart (no key held:
  overlapping notes keep the value, poly and mono) draws a new r like a
  cycle start, so every restarted note gets a new value (smooth: from the
  last r toward it; ours kept the old one until 2026-11-11). ARP STEPS
  NEVER RESTART the LFO: only the key starting the arp phrase does (no
  arp key held), tempo sync on or off (measured in poly; legato by ear,
  032a.sy1).
  Patches without a random LFO on render bit-identically across that
  change. FM not checked (assumed like pitch).
  Tempo sync: 30 steps, step = floor(s·30/128), cycle = `Lfo::kSyncBeats`
  16 bars (s 0) .. 1/64T (127) = Synth1's display list; NO host-beat phase
  lock (synced LFOs free-run, the arp starts at the key press).
  Depths (key-synced slow square probes): pitch = bipolar table
  (kLfoPitchCents, 6000 ct at 127); cutoff / amp / pan UNIPOLAR, u =
  (lfo+1)/2: cutoff += (125 - cutoff)·depth/127·u; amp gain = 1 -
  depth/127·(1-u); pan q = lfo·(1-(1-depth/127)^3), gain toward side
  1+|q|/2, away 1-|q|. PW (time-domain duty per cycle): duty w moves toward
  1 (lfo > 0) / 0 (lfo < 0) by depth/127·|lfo| of the room left, never
  wraps. FM: knob += (127 - knob)·depth/127·u, fractional knob, beta
  interpolated (probe saturates above knob ~100).
  BOTH LFOs ON ONE DEST (`measure_lfo_sum.py`): pitch = sum of cents;
  cutoff / FM / pw / pan = the law on the depth-weighted SUM (Σ d·u for
  cutoff / FM, Σ d·lfo for pw / pan; pan unclamped, pw clamps); amp =
  PRODUCT of the per-LFO gains.
- Phase knobs: osc phase (91) 0 = FREE: a voice SLOT's oscillators are
  never reset, they carry on from the slot's last note (frozen while idle)
  from per-slot values set at instance creation (`measure_slot_phase.py` →
  `kSlotOsc1Phase` / `kSlotOsc2Phase`, 32 slots; slots 0..7 osc1 =
  kPhaseDeg; detune copies `kSlotCopyPhase`; osc1 REFINED to ~2e-5 cycle
  by `measure_slot_osc1.py` from synced-noise reset samples: the fits were
  off by up to 2e-4, enough to shift a synced-noise restart). A note's
  oscillators start at its 16-sample sub-block's start. > 0 = osc1
  starts at 0, osc2 at (v-1)/126·180°, detune copy i at
  kCopyPhaseDeg[i]·(v-1)/126 (Voice.hpp); a MONO RETRIGGER restarts them
  too. Unison phase (92, osc phase > 0 only, `measure_unison_shift.py`):
  EVERY oscillator of the k-th stack voice started (k, not its slot: a 2nd
  note on slots 4..7 nulls only so) is shifted by ITS OWN slot table at
  slot k × v/127, the table value 0..1 cycle UNWRAPPED (it scales with the
  knob): osc1 kSlotOsc1Phase (= the old kPhaseDeg, ~0 for k = 0), osc2
  kSlotOsc2Phase (k = 0 too: 203° at 127), each detune copy
  kSlotCopyPhase. An octave-down sub's copy starts at HALF its osc1 copy's
  unwrapped start (past one cycle = +half a sub cycle). Until 2026-11-11
  ours shifted osc2 and the copies by osc1's angle: unison phase > 0 with
  osc2 or detune nulled at +2..+13 dB ("Synth1's attack is softer"), now
  -15..-40 dB (osc2 -37..-40, detune back to its phase-0 residual); 78
  library patches: 2.76 → 2.21 dB, 37 better, 3 worse by ≤ 0.7 (noise +
  sync patches).
- Mod env amount m = (amt-64)/63 above 64, /64 below. Pitch = ±60
  semis·m·env. PW = the LFO's room-left law with m·env. FM = room-left in
  FM-knob units with m·g: g = env during the attack, during the DECAY g = 1
  + ln(env)/ln(1e4) (FM offset falls LINEARLY to 0 at 9.2 taus).
- Unison (`check_unison.py`: every knob + interactions vs the original,
  nulls ≤ -20 dB where phases are deterministic): a note takes min(voices,
  polyphony) voices (round robin). POLY NOTES OF ONE 16-SAMPLE SUB-BLOCK
  TAKE THEIR VOICES IN REVERSE ORDER (free-phase chords null at -50 dB
  only so; `Synth::NoteOn` queues them, flushed reversed at the next
  render / event); mono / legato keep the arrival order (last note wins).
  Over the polyphony: a poly note never steals a voice started in the same
  sub-block (dropped = the chord's FIRST notes, processed last), a unison
  stack DOES (oldest first), so a unison chord keeps its first notes too,
  in the slots its last ones took first; a voice's PLACE p is FIXED by its slot,
  p = slot % voices (a stack may hold a place twice): detune t·D (t =
  2p/(N-1) - 1, D table), pan spread t·(v-64)/64 (below 64 the LOWEST
  voice goes RIGHT), odd p + (pitch-24) semis; no level compensation. The
  stack voices share osc phases only through the knobs above (osc phase 0:
  each slot's own phases; coherent copies were +12 dB and phasing). Each
  stack voice glides from its own last pitch (PORTAMENTO).
- Pan: patch pan q = (v-64)/64 below the centre, /63 above; balance gains
  (centre 1/1, hard side 1/0); patch pan × unison spread gains MULTIPLY.
  The patch pan AND the LFOs on pan act AFTER THE EFFECT (`Synth::Pan`),
  before the delay / chorus (their crossing types carry a hard-left pan
  to the right); only the unison spread pans per voice, before it. Every
  effect type: L and R = the effect's output x the pan gains exactly
  (2026-11-11; before, a d.d. folded a 3 % pan difference into two
  uncorrelated channels: Kaiyoti 027 / 069 spec 4..5 dB -> 0.5).
- Delay TIMES are WHOLE samples (single-tap IRs, identical-noise trick,
  44.1 / 96 kHz, tempos 60..200): division × beat rounded DOWN to a
  multiple of 4 samples, + the spread offset floored, at least
  floor(0.1 ms · sr): time 0 = 4 samples at 44.1 kHz (a comb; it was 29,
  interpolated). A new time glides exponentially, tau 0.215 s.
- Delay: spread = one side later by 100 ms·10^((|v-64|-63)/21) (0 at 64);
  delay tone (`measure_delay_tone.py`, `kDelayToneHz`): a bilinear one-pole
  in the echo path, 64 = none, < 64 LP 200 Hz·110^(t/63) (22 kHz at 63),
  > 64 HP 10 Hz .. 8 kHz. Types (hard-panned noise bursts, echo envelopes
  per channel): 0 independent; 1 X = each input enters the OTHER side's
  line and the lines feed each other (a left input echoes right, left,
  right...; ours echoed left first until 2026-11-11); 2 PP = (L+R)/2 into
  the left line, left line -> right line, right feeds back left.
- Arp gate 0 = silent.
- Chain order (`measure_fx_order.py`): voices → effect → pan → EQ → delay →
  chorus.
- EQ (`check_eq.py`: every tone / freq / level / Q case within 0.06 dB):
  TONE (`measure_eq_tone.py`, `kEqToneHz`) = one bilinear one-pole, no
  prewarp, ALWAYS on: ≤ 64 LP 200 Hz·110^(t/64) (64 = 22 kHz, the output
  lowpass), > 64 HP 11 Hz .. 7.2 kHz with no lowpass left (tone 65 is ~4
  dB brighter at 16 kHz than 64). PEAK (`measure_eq.py`): f0 =
  50·320^(freq/127), (level−64)·50/127 dB, analog prototype Q
  3.18·2^((q−64)/38.16) (cut = inverse of the boost), poles AND zeros by
  impulse invariance, gain set at f0 (a boost near the top loses up to 3
  dB at DC).
- CHORUS (`measure_chorus.py`): per-channel lines, voice delay = base·(1 +
  D·sin(ωt − θ)), D = (100^(depth/127) − 1)/99, one sine LFO free-running
  from creation, 2.94 Hz·2^((rate−64)/8.89), rate 0 = 0.01 Hz; types 1–3
  L→L / R→R (R θ 0° type 1, 90° types 2 = 3), type 4 L→L 0°, R→L 90°, L→R
  180°, R→R 270°; comb feedback 0.99·(fb−64)/64; wet = level/127, dry
  unity. Base delay = WHOLE samples (`Chorus::TimeSamples`, measured
  table at 44.1 kHz, 30 ms at 127; 96 kHz = the same delays rounded).
  The comb adds the CURRENT tap (echoes every base delay exactly).
  DELAY + CHORUS both on: the delay's echoes never reach the output
  directly; they run through the chorus voices only, at UNITY whatever
  the chorus level (level 0 = echoes chorus-delayed, still full):
  out = dry + level·chorus(dry) + chorus(echoes) (separate lines, same
  LFO / taps / feedback). Delay dry = (127 − dw)/63 above 64.
- EFFECT SECTION (`measure_effect.py`, `measure_effect_time.py`,
  `reference/meta/effect.json`; laws in the EffectUnit comment):
  distortions = drive table → curve → RBJ HP → RBJ LP 400 Hz·40^(ctl2/127)
  Q3 → gain (level+2)/129, no dry (a.d.1: below); d.d. = F(x) = P·tri(S·x/P), slope S and
  peak P PER CTL1 (`measure_dd.py` → kDdSlope / kDdPeak: P 1.64 at ctl1 0
  falling to 0.5778 from ~64 up, S 0.895 .. 1000; nulls -50..-78 dB on the
  effect's exact input = the effect-off render with the EQ tone stage
  undone at the probe sine's frequency: the 22 kHz one-pole after the
  effect shifts the input enough to wreck a 1000-fold fit). With a
  thousand folds the output amplifies any input difference ~40 dB, so a
  patch's d.d. output only nulls as deep as its voice does (-63 dB voice
  -> ~-15 dB folded); deci = hold ctl1+1 samples, floor quantiser,
  14 kHz one-pole; r.m. = sine 8000^(ctl1/127) Hz; comp = one-pole |x|
  detector PER CHANNEL (unison spread input: per-channel gains within
  0.02 dB; linked until 2026-11-11) + soft-knee leveller; phasers = 2/4/8/12 allpasses, LEVEL =
  FEEDBACK (0.99·(lv−64)/64), ctl1 = sweep depth, ctl2 = rate, a bouncing
  log-frequency ramp around 1271 Hz (starts at 254.6 Hz).
- A.D.1 (effect type 0) STATUS 2026-11-12: ctl1 is EXACT, the system S is
  a FITTED model. CTL1 LAW (`measure_ad1_law.py` → effect.json
  `ad1_law`): the original is out(ctl1)·S(d(ctl1)·x) for ONE fixed S (a
  784 Hz sine's compression curve, 36 levels, and its H2 / H3 at every
  ctl1 = the ctl1 64 curves shifted along log input and scaled, within
  0.01 dB); d = d64·10^(3.5·(ctl1−64)/127) (70 dB), out measured per
  ctl1 (x8.0 at 0, x1.52 at 16, x1.03 at 32, 1.000 from ~48 up);
  kAd1Drive / kAd1Out. S (`fit_ad1.py --fit`, all ctl1 0, 8, .., 127
  at once; `ad1_model.c` = S in C, exact curves): z = f1(d·x − G·lp(z) +
  b1) − f1(b1), 95.4 Hz one-pole loop on the previous z, one-pole HP
  263.5 Hz, s = f2(g2·h + b2) − f2(b2), one-pole HP 30.3 Hz, RBJ HP 96.3
  Hz Q 2.37, the RBJ LP Q 3; f(w) = w/(1+|w/L|^k)^(1/k) per side. The
  engine reads f1 / f2 from tables (`Ad1Curve`: value + slope, cubic
  Hermite on u = w/(|w|+1), max error -114 dB; ours vs the C model -99..
  -141 dB median, worst -76 dB at ctl1 127 where a float input flips a
  switching sample). Median sample error -22.2 (ctl1 0) .. -24.1 dB (127),
  worst tests -13..-16 dB (the single-curve model of 2026-11-11: -18.7 /
  -9..-15). Library a.d.1 patches (5 + Desktop ad1.sy1): level |mean|
  0.51 -> 0.27 dB, spec 1.13 -> 0.99 dB (Kaiyoti 020's 3.2 dB is its
  voice: 3.5 with the effect off; Kaiyoti 035 got worse, 0.48 -> 0.74:
  ctl2 73, level 64, not in the fit set). All fx bench x74 -> x83.
  MEASURED 2026-11-12 (scratch probes, reuse): at ctl1 127 the curve
  switches like a per-sample hard clip (folded harmonics at the same
  level as a naive clip: no oversampling), at the input's own zero
  crossings (no pre-filter phase), with thresholds in input units
  −1.45e-3 mean and an up / down spread falling with frequency (4.6e-3 at
  49 Hz, 0.9e-3 at 554 Hz, amplitude-independent: feedback from the
  output state, lowpassed); pulse trains (key 12, pw 2..127) at ctl1 127:
  the output jump at an up edge after a low pulse of g samples over the
  isolated jump = 1.36 (g 33), 1.58 (58), 1.38 (97), 1.22 (140), 1.09
  (214), 1.02 (341), 0.96 (680), 0.98 (1349): NOT linear after the
  switch; a one-pole HP ~71 Hz then an asymmetric curve fits that alone
  (rms 0.017), the model above gives 1.40 / 1.30 / 1.05 / 0.98 (decays
  too fast). Small signal (rational fit): a real pole ~600 Hz, an RBJ-
  like pair ~104 Hz, zeros near DC. Lead: the loop's lowpass may ALSO
  form the highpass into f2 (v = z − K·lp: HP at the loop's 71 Hz when
  saturated, at ωc·(1 + G·f1') ~ 600 Hz at small signal); a first fit of
  that (+ a 140 Hz HP) stuck at -20.2 dB.
  MEASURED FACTS: the LP is exactly the other distortions' RBJ LP (Q 3,
  c2 law, 0.01 dB); level = (level+2)/129; at small signal ctl1 only
  scales the response (same shape, phase within 1°), the gain is NOT
  monotonic (2.40 at 0, min 1.26 at 16, 17.5 at 64, x2.78 per 16 above:
  probe: low-level noise, H at 1..4 kHz); the response falls ~24 dB/oct
  below 100 Hz (4th order, phase ~+360° at 16 Hz), +9 dB from 200 Hz to
  3 kHz, -2 dB from 3 to 20 kHz; strong EVEN harmonics already at small
  levels (H2 -10 dB at 392 Hz, input 0.024), a square-ish wave with
  exponential decays when driven hard; compression starts at input ~0.7 /
  |small-signal gain| for f >= 200 Hz (the 200 Hz..3 kHz rise acts BEFORE
  the limiting), later below. NO static curve between linear filters fits
  (every split of the small-signal response tried: harmonic-domain linear
  fit of a free curve, -4..-6 dB residual), so the nonlinearity has memory.
  Tried on the single-curve model, no gain: a free 41-knot curve, a bias
  per ctl1, a pre highpass / shelf, a one-pole smoother before or after
  the curve, the loop without the sample delay, feedback taken after the
  first highpass. At ctl1 127 (hard switch + free curve after a highpass,
  curve values by linear LS): -25 dB median at best, LF sines / saws /
  pulses worst (-15..-20): a highpass -> static curve -> filters after
  the switch is not enough either.

## Measurement methods (reuse them)

- Reference = the original AU via `build/s1ref/s1ref` (x86_64, Rosetta).
  Simultaneous events keep the job's order (stable sort since 2026-11-03;
  older corpus renders may have chord notes / offs-vs-ons reordered).
  ONE PROCESS PER RENDER (`s1.render_many` does it, parallel): a 2nd
  instance in a process starts with other osc phases / PRNG; the 1st is
  bit-exact repeatable. The 0.25 s pre-roll counts (LFOs free-run in it).
  A fresh Synth1 instance loads program 001 of bankfolder0 (its
  `jp.daichi.synth1` prefs; scripts back up / restore them).
- Python API (`tools/s1ref/s1.py`): `s1.patch(p0=.., p19=..)` = neutral DRY
  patch (osc1 saw, LP12 open, no LFO/fx) + overrides; `render_many(specs,
  ours=False|True)` (ours = s2render), `tone_amp`, `harmonics`, `key_hz`.
  Helpers: `envelope.py`, `pitch.py`, `instfreq.py`, `cutoff_probe.py`
  (effective cutoff in LP12 knob units; calibrate it with the SAME timing
  on the SAME engine), `filtermodels.py`, `fit_*.py`.
- IDENTICAL-NOISE TRICK: osc2 noise is the same sequence in every fresh
  process (ours too), so H(f) = Pxy/Pxx against a flat reference render (HP12 cut 0)
  gives exact complex transfer functions (filters, EQ, delay tone, ...).
  Pulse pw 0 is NOT silence (narrow pulse, it excites LPDL
  self-oscillation): silence osc1 with mix 127 instead.
- Level tricks: lower an oscillator with mix.
- Old manuals: EN https://daichilab.sakura.ne.jp/softsynth/synmanu/readmeeng.html,
  JA (more complete, Shift-JIS) .../synmanu/readme.html; the unofficial
  v1.12 manual (Z. Nikolic 2011) has the version history + per-version
  param table (the lead for the legacy conversion).
- Native value ranges and the AU normalized mapping: reference/meta/
  nativemap.txt; state chunk word layout: tools/s1ref/README.md.
- Reference corpus (`reference/renders`, download in README): regenerate
  with `tools/s1ref/corpus.py` while Rosetta exists; pack with
  `tools/s1ref/pack_reference.sh`. The presets group is anonymized
  (`patch0001`…).

## Status and open items

- Preset-wide diff (`compare.py presets`, 416 renders): level |mean| 1.00
  dB, spec 2.40 dB, env 3.01 dB; `compare.py filter` 0.87 / 2.05 / 2.83.
- Approximations marked `TODO(measure)` in code: a.d.1 exact structure (status above),
  arp random sequence (+ S&H / smooth LFO: not the original's PRNG), LPDL
  resonance frequency near the top cutoffs.
- Random sequences (arp random, S&H / smooth LFO): not identified (the
  random LFOs' STRUCTURE is, see LFO above). Leads:
  S&H read through LFO -> pitch is quantized to whole steps (useless), through
  LFO -> amp it sits on a 1/1024 grid; the two destinations gave
  DIFFERENT sequences from fresh processes (unexplained); not mac rand(),
  not common 32-bit LCGs, not windows of the noise LFSR. Presets using
  them (patch0368: random arp) move in the scores whenever our RNG draw
  order changes.
- Portamento: glides of 2+ octaves land ~30 ms earlier than the
  original's (the last steps; mid-glide matches). Decay knobs > 88 run
  ~0.5-1 % fast (a release then ends one 256-sample block early).
- Worst presets: patch0106 (+21 dB), patch0025, patch0163 (sync + FM + arp
  + fx combos). Isolated: sync + osc1 FM with osc2 pulse is chaotic (osc2
  resets steer osc1 through the FM): at key 36 FM 32 the original LATCHES
  (osc1 wraps before osc2's pulse ever goes low: a constant), ours does
  not; FM with sync off, and sync without FM, match. Tried: sync on
  negative-going wraps, FM >= 0, pulse FM input x2 / bipolar: no gain.
  patch0010 (LPDL + smooth random LFO). A resonant LP24 pulled shut by a
  strongly negative env with a slow attack stays louder in the original.
  PW: within ~1 / 254 of the original.
