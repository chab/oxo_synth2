@echo off
rem Builds the Windows x64 VST3 (MSVC) into Windows_Build\build and runs the
rem Windows storage tests. Installs nothing, touches nothing outside the repo.
rem   build_windows.bat [--no-pause]
rem Needs Visual Studio 2022 or later (workload "Desktop development with
rem C++") and the submodules: git submodule update --init --recursive
rem CMake: %CMAKE% if set, else cmake on PATH, else the one bundled with Visual
rem Studio. Generator: CMake's default (the newest Visual Studio), or the
rem CMAKE_GENERATOR environment variable (CMake reads it).
setlocal EnableExtensions DisableDelayedExpansion
pushd "%~dp0" || exit /b 1
set "NOPAUSE="
if /i "%~1"=="--no-pause" set "NOPAUSE=1"
set "BUILD=%~dp0Windows_Build\build"
set "LOG=%BUILD%\build.log"
set "EXIT_CODE=1"

if defined CMAKE goto have_cmake
for /f "delims=" %%i in ('where cmake 2^>nul') do if not defined CMAKE set "CMAKE=%%i"
if defined CMAKE goto have_cmake
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "delims=" %%i in ('"%VSWHERE%" -latest -products * -find Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe') do if not defined CMAKE set "CMAKE=%%i"
if not defined CMAKE (set "REASON=CMake not found: install Visual Studio's C++ workload or set CMAKE" & goto fail)
:have_cmake
if not exist "third_party\vst3sdk\public.sdk\source\vst\vstaudioeffect.cpp" (set "REASON=VST3 SDK missing: git submodule update --init --recursive" & goto fail)

if not exist "%BUILD%" mkdir "%BUILD%"
>"%LOG%" echo Synth2 Windows VST3 build log
set "ARCH=-A x64"
if defined CMAKE_GENERATOR set "ARCH="

echo [1/3] Configuring (%CMAKE%)...
"%CMAKE%" -S "%~dp0Windows_Build" -B "%BUILD%" %ARCH% -DCMAKE_POLICY_VERSION_MINIMUM=3.5 >>"%LOG%" 2>&1
if errorlevel 1 (set "REASON=CMake configuration failed" & goto fail)

echo [2/3] Building OxoSynth2.vst3...
"%CMAKE%" --build "%BUILD%" --config Release --target OxoSynth2VST3 synth2_storage_tests --parallel >>"%LOG%" 2>&1
if errorlevel 1 (set "REASON=Build failed" & goto fail)

echo [3/3] Storage tests...
set "TESTS=%BUILD%\bin\Release\synth2_storage_tests.exe"
if not exist "%TESTS%" (set "REASON=Storage tests not built" & goto fail)
"%TESTS%" >>"%LOG%" 2>&1
if errorlevel 1 (set "REASON=Storage tests failed" & goto fail)

echo.
echo [PASS] Built Windows_Build\build\VST3\Release\OxoSynth2.vst3 (not installed).
set "EXIT_CODE=0"
goto done

:fail
echo.
echo [FAIL] %REASON%
if exist "%LOG%" echo        Details: %LOG%
:done
popd
if not defined NOPAUSE pause
exit /b %EXIT_CODE%
