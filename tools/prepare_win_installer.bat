@echo off
rem ===========================================================================
rem  Builds the Windows distribution: the VST3 bundle in one Inno Setup
rem  installer .exe, plus the .zip that actually gets hosted (that exe +
rem  README.pdf + the raw bundle). On the Windows machine, from anywhere:
rem
rem    tools\prepare_win_installer.bat [--skip-build] [--skip-validator]
rem                                    [--msvc] [--dynamic-crt] [--no-pause]
rem                                    [--help]
rem
rem  Output: dist\win\<Product>-<version>-<build>-Windows-x64.{exe,zip}
rem  (gitignored). VST3 only, x64.
rem
rem  Steps: 1. build Release x64 (CMake + Visual Studio's clang-cl, like
rem  build_windows.bat --clang) into Windows_Build\build-dist-clang + run the
rem  storage tests,
rem  2. stage the bundle and check it is COMPLETE (dll, factory bank, starter
rem  banks, no VC++ runtime import), 3. Steinberg's validator on the staged
rem  bundle, 4. sign the dll, 5. ISCC tools\win_installer\synth2.iss, 6. sign
rem  the exe, 7. zip.
rem
rem  BUILT WITH CLANG-CL (Visual Studio's ClangCL toolset): the engine runs
rem  ~1.7x faster than MSVC's code (build_windows.bat --bench: a 32-voice
rem  load takes 5.5 %% of one core against 9.1 %%). --msvc builds with MSVC
rem  instead, into Windows_Build\build-dist (a PC without the clang
rem  components; not for a release).
rem
rem  OWN BUILD DIR because the release build uses the STATIC CRT (/MT): the
rem  dll then needs no VC++ 2015-2022 redistributable on the user's PC (a
rem  clean machine fails to load a /MD build with no useful message), and a
rem  hand-copied bundle from the zip works too. Sharing Windows_Build\build
rem  with the dev loop would rebuild everything at every switch.
rem  --dynamic-crt reproduces a dev-identical /MD binary.
rem
rem  THE VALIDATOR RUNS IN PORTABLE MODE: a Synth2_Data folder next to the
rem  staged bundle (removed afterwards) catches the settings and the starter
rem  banks the plug-in creates on load, so your own Documents library and
rem  AppData folder stay out of it.
rem
rem  NO USER FOLDERS ARE CREATED. The installer runs elevated; the library
rem  (Documents\<OXO_LIBRARY_FOLDER>) and the settings folder
rem  (%%APPDATA%%\<OXO_DATA_FOLDER>) are made by the plug-in on first use.
rem
rem  IDENTITY + VERSION come from Identity.xcconfig (+ the gitignored
rem  Identity.local.xcconfig, which wins), like CMake and the mac installer:
rem  product name (bundle = name without spaces), vendor, website, bundle id
rem  (the installer's AppId), folders, MARKETING_VERSION /
rem  CURRENT_PROJECT_VERSION.
rem
rem  SIGNING is env-driven and OPTIONAL (Windows has no notarization; the
rem  equivalent is Authenticode). Unsigned, SmartScreen shows "Windows
rem  protected your PC" (More info > Run anyway, see README.pdf):
rem
rem    set OXO_WIN_SIGN_SHA1=<cert thumbprint in the Windows cert store>
rem      ...or...
rem    set OXO_WIN_SIGN_PFX=C:\path\to\cert.pfx
rem    set OXO_WIN_SIGN_PFX_PASSWORD=<password>
rem    set OXO_WIN_SIGN_TIMESTAMP_URL=http://timestamp.digicert.com  (default)
rem
rem  The dll is signed before it is packed into the exe, then the exe.
rem
rem  Needs: Visual Studio 2022+ (C++ workload, includes CMake; individual
rem  components "C++ Clang Compiler for Windows" and "MSBuild support for
rem  LLVM (clang-cl) toolset"), the
rem  submodules (git submodule update --init --recursive), Inno Setup 6.3+
rem  (winget install JRSoftware.InnoSetup). CMake: %%CMAKE%% if set, else
rem  cmake on PATH, else Visual Studio's; generator: CMake's default or
rem  %%CMAKE_GENERATOR%%.
rem ===========================================================================
setlocal EnableExtensions DisableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_DIR=%%~fI"
set "RES_DIR=%SCRIPT_DIR%win_installer"
set "SRC_DIR=%REPO_DIR%\Windows_Build"
set "BUILD_DIR=%SRC_DIR%\build-dist-clang"
set "TOOLSET=-T ClangCL"
set "COMPILER=clang-cl"
set "DIST_DIR=%REPO_DIR%\dist\win"
set "STAGE_DIR=%DIST_DIR%\stage"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

set "SKIP_BUILD=0"
set "SKIP_VALIDATOR=0"
set "STATIC_CRT=ON"
set "NOPAUSE="

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="--skip-build" (set "SKIP_BUILD=1" & shift & goto parse_args)
if /i "%~1"=="--skip-validator" (set "SKIP_VALIDATOR=1" & shift & goto parse_args)
if /i "%~1"=="--msvc" (set "BUILD_DIR=%SRC_DIR%\build-dist" & set "TOOLSET=" & set "COMPILER=MSVC" & shift & goto parse_args)
if /i "%~1"=="--dynamic-crt" (set "STATIC_CRT=OFF" & shift & goto parse_args)
if /i "%~1"=="--no-pause" (set "NOPAUSE=1" & shift & goto parse_args)
if /i "%~1"=="--help" goto usage
echo ERROR: unknown option: %~1 1>&2
goto usage
:args_done
set "LOG=%BUILD_DIR%\build.log"

rem ---------------------------------------------------------- 0. identity
call :read_identity "%REPO_DIR%\Identity.xcconfig"
call :read_identity "%REPO_DIR%\Identity.local.xcconfig"
for %%K in (OXO_PRODUCT_NAME OXO_VENDOR OXO_VENDOR_WEBSITE OXO_BUNDLE_ID OXO_DATA_FOLDER OXO_LIBRARY_FOLDER MARKETING_VERSION CURRENT_PROJECT_VERSION) do (
    if not defined ID_%%K (
        set "REASON=%%K missing in Identity.xcconfig"
        goto fail
    )
)
set "PRODUCT=%ID_OXO_PRODUCT_NAME%"
set "PRODUCT_FILE=%PRODUCT: =%"
set "BUNDLE=%PRODUCT_FILE%.vst3"
set "VERSION=%ID_MARKETING_VERSION%"
set "BUILD_NUM=%ID_CURRENT_PROJECT_VERSION%"
set "DATA_FOLDER=%ID_OXO_DATA_FOLDER:/=\%"
set "LIBRARY_FOLDER=%ID_OXO_LIBRARY_FOLDER:/=\%"
rem The build number is in the name: two exes of one marketing version are
rem otherwise indistinguishable in a downloads folder.
set "OUT_NAME=%PRODUCT_FILE%-%VERSION%-%BUILD_NUM%-Windows-x64"
set "VST3_SRC=%BUILD_DIR%\VST3\Release\%BUNDLE%"
set "STAGED=%STAGE_DIR%\%BUNDLE%"
set "DLL=%STAGED%\Contents\x86_64-win\%BUNDLE%"

echo ==^> %PRODUCT% %VERSION% (%BUILD_NUM%) Windows installer (VST3, x64, %COMPILER%)
if "%COMPILER%"=="MSVC" echo     !!! --msvc: the engine takes ~1.7x the CPU of the clang-cl build
if "%STATIC_CRT%"=="ON" echo     static CRT: no VC++ redistributable needed on the user's PC
if "%STATIC_CRT%"=="OFF" echo     !!! --dynamic-crt: the user's PC NEEDS the VC++ 2015-2022 x64 redistributable

rem ------------------------------------------------------------- 1. build
if "%SKIP_BUILD%"=="1" (
    echo ==^> Skipping the build ^(--skip-build^)
    goto build_done
)
if defined CMAKE goto have_cmake
for /f "delims=" %%i in ('where cmake 2^>nul') do if not defined CMAKE set "CMAKE=%%i"
if defined CMAKE goto have_cmake
if exist "%VSWHERE%" for /f "delims=" %%i in ('"%VSWHERE%" -latest -products * -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe') do if not defined CMAKE set "CMAKE=%%i"
if not defined CMAKE (set "REASON=CMake not found: install Visual Studio's C++ workload or set CMAKE" & goto fail)
:have_cmake
if not exist "%REPO_DIR%\third_party\vst3sdk\public.sdk\source\vst\vstaudioeffect.cpp" (set "REASON=VST3 SDK missing: git submodule update --init --recursive" & goto fail)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
>"%LOG%" echo %PRODUCT% Windows installer build log
set "ARCH=-A x64"
if defined CMAKE_GENERATOR set "ARCH="
rem Both switches: SMTG_USE_STATIC_CRT adds /MT to the SDK's and our
rem compile options, CMAKE_MSVC_RUNTIME_LIBRARY keeps CMake's own runtime
rem setting (policy CMP0091) from asking for /MD at the same time. Vendored
rem zlib follows the same variable.
set "CRT_ARGS=-DSMTG_USE_STATIC_CRT=ON -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
if "%STATIC_CRT%"=="OFF" set "CRT_ARGS=-DSMTG_USE_STATIC_CRT=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL"

echo ==^> Configuring (%CMAKE%)
"%CMAKE%" -S "%SRC_DIR%" -B "%BUILD_DIR%" %ARCH% %TOOLSET% -DCMAKE_POLICY_VERSION_MINIMUM=3.5 %CRT_ARGS% >>"%LOG%" 2>&1
if not errorlevel 1 goto configured
set "REASON=CMake configuration failed"
if defined TOOLSET set "REASON=CMake configuration failed (clang-cl needs Visual Studio's components C++ Clang Compiler for Windows + MSBuild support for LLVM; --msvc builds without them)"
goto fail_log
:configured
echo ==^> Building %BUNDLE%, validator, storage tests
"%CMAKE%" --build "%BUILD_DIR%" --config Release --target OxoSynth2VST3 validator synth2_storage_tests --parallel >>"%LOG%" 2>&1
if errorlevel 1 (set "REASON=Build failed" & goto fail_log)
echo ==^> Storage tests
if not exist "%BUILD_DIR%\bin\Release\synth2_storage_tests.exe" (set "REASON=Storage tests not built" & goto fail_log)
"%BUILD_DIR%\bin\Release\synth2_storage_tests.exe" >>"%LOG%" 2>&1
if errorlevel 1 (set "REASON=Storage tests failed" & goto fail_log)
:build_done

rem ------------------------------------------------------------- 2. stage
if not exist "%VST3_SRC%\Contents\x86_64-win\%BUNDLE%" (set "REASON=missing build product %VST3_SRC% (run without --skip-build)" & goto fail)
echo ==^> Staging the bundle
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%STAGE_DIR%" || (set "REASON=cannot create %STAGE_DIR%" & goto fail)
rem /MIR into a fresh folder; .DS_Store rides along from a mac checkout.
robocopy "%VST3_SRC%" "%STAGED%" /MIR /XF .DS_Store /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (set "REASON=staging copy failed" & goto fail)

rem An INCOMPLETE bundle still loads (no factory bank, no starter banks):
rem exactly what ships unnoticed. Check what the CMake resources should fill.
if not exist "%DLL%" goto incomplete
dir /b "%STAGED%\Contents\Resources\Synth2 Factory\*.sy1" >nul 2>nul || goto incomplete
dir /b /ad "%STAGED%\Contents\Resources\Synth2 Starter Banks\*" >nul 2>nul || goto incomplete
if "%STATIC_CRT%"=="OFF" goto crt_done
rem A /MD dll imports vcruntime140 / msvcp140 / api-ms-win-crt-*: it would
rem not load on a PC without the redistributable.
set "DUMPBIN="
if exist "%VSWHERE%" for /f "delims=" %%i in ('"%VSWHERE%" -latest -products * -find VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe') do if not defined DUMPBIN set "DUMPBIN=%%i"
if not defined DUMPBIN (echo     !!! dumpbin.exe not found: static CRT NOT verified & goto crt_done)
"%DUMPBIN%" /nologo /dependents "%DLL%" | findstr /i "vcruntime msvcp api-ms-win-crt" && (set "REASON=the dll imports the shared VC++ runtime although the build is static (stale cache? delete %BUILD_DIR%)" & goto fail)
echo     static CRT verified (no VC++ runtime import)
:crt_done

rem --------------------------------------------------------- 3. validator
if "%SKIP_VALIDATOR%"=="1" goto validated
set "VALIDATOR=%BUILD_DIR%\bin\Release\validator.exe"
if not exist "%VALIDATOR%" (set "REASON=validator.exe not found in %BUILD_DIR%\bin\Release (--skip-validator packages anyway)" & goto fail)
echo ==^> Steinberg validator (portable mode, sandboxed in %STAGE_DIR%\Synth2_Data)
mkdir "%STAGE_DIR%\Synth2_Data"
"%VALIDATOR%" "%STAGED%" >"%DIST_DIR%\validator.log" 2>&1
set "VALIDATOR_RC=%ERRORLEVEL%"
rmdir /s /q "%STAGE_DIR%\Synth2_Data"
if not "%VALIDATOR_RC%"=="0" (
    powershell -NoProfile -Command "Get-Content -Tail 20 (Join-Path $env:DIST_DIR 'validator.log')"
    set "REASON=validator failed, full log: %DIST_DIR%\validator.log"
    goto fail
)
powershell -NoProfile -Command "Get-Content -Tail 3 (Join-Path $env:DIST_DIR 'validator.log')"
:validated

rem ---------------------------------------------------------- 4. sign dll
call :sign "%DLL%" || goto fail

rem ----------------------------------------------------- 5. installer exe
set "ISCC="
for /f "delims=" %%i in ('where iscc 2^>nul') do if not defined ISCC set "ISCC=%%i"
if not defined ISCC if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not defined ISCC (set "REASON=Inno Setup 6.3+ not found: winget install JRSoftware.InnoSetup" & goto fail)
echo ==^> Building the installer
"%ISCC%" /Q "/DOxoProduct=%PRODUCT%" "/DOxoVendor=%ID_OXO_VENDOR%" "/DOxoWebsite=%ID_OXO_VENDOR_WEBSITE%" "/DOxoBundleId=%ID_OXO_BUNDLE_ID%" "/DOxoBundle=%BUNDLE%" "/DOxoDataFolder=%DATA_FOLDER%" "/DOxoLibraryFolder=%LIBRARY_FOLDER%" "/DOxoVersion=%VERSION%" "/DOxoBuild=%BUILD_NUM%" "/DOxoStageDir=%STAGE_DIR%" "/DOxoOutDir=%DIST_DIR%" "/DOxoOutName=%OUT_NAME%" "%RES_DIR%\synth2.iss"
if errorlevel 1 (set "REASON=ISCC failed" & goto fail)
set "INSTALLER=%DIST_DIR%\%OUT_NAME%.exe"
if not exist "%INSTALLER%" (set "REASON=installer not produced: %INSTALLER%" & goto fail)

rem ---------------------------------------------------------- 6. sign exe
call :sign "%INSTALLER%" || goto fail

rem --------------------------------------------------------------- 7. zip
rem BOTH ship: the exe is what the user runs, the zip is the download. It
rem wraps the exe for the places that refuse a bare .exe (mail gateways,
rem stricter browser warnings) and carries what an exe cannot: README.pdf
rem (walks through the SmartScreen screen of an unsigned exe) and the raw
rem bundle for a manual install (copying a folder never triggers
rem SmartScreen; works thanks to the static CRT). Zipped AFTER signing.
if not exist "%RES_DIR%\README.pdf" (set "REASON=missing %RES_DIR%\README.pdf (it ships in the zip)" & goto fail)
set "ZIP=%DIST_DIR%\%OUT_NAME%.zip"
set "PAYLOAD=%DIST_DIR%\%OUT_NAME%"
echo ==^> Zipping installer + README.pdf + %BUNDLE%
mkdir "%PAYLOAD%" || (set "REASON=cannot create %PAYLOAD%" & goto fail)
copy /y "%INSTALLER%" "%PAYLOAD%\" >nul || (set "REASON=zip payload copy failed (exe)" & goto fail)
copy /y "%RES_DIR%\README.pdf" "%PAYLOAD%\" >nul || (set "REASON=zip payload copy failed (pdf)" & goto fail)
rem From the STAGED bundle: what the validator passed and the installer
rem packs, with the signed dll.
robocopy "%STAGED%" "%PAYLOAD%\%BUNDLE%" /MIR /XF .DS_Store /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (set "REASON=zip payload copy failed (bundle)" & goto fail)
rem CreateFromDirectory with includeBaseDirectory: everything sits under one
rem <OUT_NAME>\ folder, an extract never sprays three items into Downloads.
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression.FileSystem; [IO.Compression.ZipFile]::CreateFromDirectory($env:PAYLOAD, $env:ZIP, [IO.Compression.CompressionLevel]::Optimal, $true)"
if errorlevel 1 (set "REASON=zip failed" & goto fail)
if not exist "%ZIP%" (set "REASON=zip not produced: %ZIP%" & goto fail)
rmdir /s /q "%PAYLOAD%"

echo.
echo [PASS] installer: %INSTALLER%
echo        zip:       %ZIP%
if not defined OXO_WIN_SIGN_SHA1 if not defined OXO_WIN_SIGN_PFX (
    echo.
    echo !!! UNSIGNED: SmartScreen warns on other PCs ^(More info -^> Run anyway^).
    echo !!! Set OXO_WIN_SIGN_SHA1 or OXO_WIN_SIGN_PFX for a signed release.
)
set "EXIT_CODE=0"
goto done

rem ===========================================================================
rem  helpers
rem ===========================================================================

rem KEY = value lines of an xcconfig -> ID_KEY (a later file wins). Only lines
rem starting with a letter: no // comments, no #include.
:read_identity
if not exist "%~1" exit /b 0
for /f "usebackq tokens=1,* delims== " %%A in (`findstr /r /b "[A-Z]" "%~1"`) do set "ID_%%A=%%B"
exit /b 0

rem Authenticode-sign one file, or nothing when no certificate is set.
rem Always timestamped: without it the signature dies with the certificate.
:sign
if not defined OXO_WIN_SIGN_SHA1 if not defined OXO_WIN_SIGN_PFX exit /b 0
if not defined SIGNTOOL for /f "delims=" %%i in ('where signtool 2^>nul') do if not defined SIGNTOOL set "SIGNTOOL=%%i"
rem The Windows SDK has one per version and arch: the last x64 match wins.
if not defined SIGNTOOL for /f "delims=" %%F in ('dir /b /s "%ProgramFiles(x86)%\Windows Kits\10\bin\signtool.exe" 2^>nul ^| findstr /i "\\x64\\"') do set "SIGNTOOL=%%F"
if not defined SIGNTOOL (set "REASON=signtool.exe not found (install the Windows SDK)" & exit /b 1)
set "TS_URL=%OXO_WIN_SIGN_TIMESTAMP_URL%"
if not defined TS_URL set "TS_URL=http://timestamp.digicert.com"
echo ==^> Signing %~nx1
if defined OXO_WIN_SIGN_PFX (
    "%SIGNTOOL%" sign /fd SHA256 /tr "%TS_URL%" /td SHA256 /f "%OXO_WIN_SIGN_PFX%" /p "%OXO_WIN_SIGN_PFX_PASSWORD%" "%~1"
) else (
    "%SIGNTOOL%" sign /fd SHA256 /tr "%TS_URL%" /td SHA256 /sha1 "%OXO_WIN_SIGN_SHA1%" "%~1"
)
if errorlevel 1 (set "REASON=signing failed for %~nx1" & exit /b 1)
exit /b 0

:incomplete
set "REASON=the staged bundle is incomplete (dll, Synth2 Factory or Synth2 Starter Banks missing): check the resources in Windows_Build\CMakeLists.txt"
goto fail

:usage
echo.
echo Usage: tools\prepare_win_installer.bat [options]
echo.
echo   --skip-build       package what is already built (Windows_Build\build-dist-clang,
echo                      with --msvc Windows_Build\build-dist)
echo   --skip-validator   do not run Steinberg's validator
echo   --msvc             build with MSVC instead of clang-cl: no clang components
echo                      needed, but the engine takes ~1.7x the CPU (not for a release)
echo   --dynamic-crt      shared MSVC runtime: the user's PC then NEEDS the
echo                      VC++ 2015-2022 x64 redistributable
echo   --no-pause         no final pause
echo   --help             this text
echo.
set "EXIT_CODE=2"
goto done

:fail_log
if exist "%LOG%" powershell -NoProfile -Command "Select-String -Path $env:LOG -Pattern ': (fatal )?error|CMake Error' | Select-Object -Last 15 | ForEach-Object { $_.Line.Trim() }"
:fail
echo.
echo [FAIL] %REASON%
if exist "%LOG%" echo        Build log: %LOG%
set "EXIT_CODE=1"
:done
if not defined NOPAUSE pause
exit /b %EXIT_CODE%
