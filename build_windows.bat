@echo off
rem Builds the Windows x64 VST3 (MSVC, or clang-cl) into Windows_Build\build and
rem runs the Windows storage tests. Installs nothing, touches nothing outside the
rem repo.
rem   build_windows.bat [--clang] [--bench] [--no-pause]
rem --clang: clang-cl (Visual Studio's ClangCL toolset, components "C++ Clang
rem Compiler for Windows" + "MSBuild support for LLVM (clang-cl) toolset")
rem instead of MSVC, into Windows_Build\build-clang (to compare the engine's
rem CPU load with the MSVC build's).
rem --bench: also builds and runs the engine CPU bench (tools\bench, the
rem plugin's compiler and options): realtime factors for a few scenarios and
rem a keyboard scrub of the bundled Madame Eljay 010 patch, one core. Run it
rem with and without --clang, on a quiet machine.
rem Needs Visual Studio 2022 or later (workload "Desktop development with
rem C++") and the submodules: git submodule update --init --recursive
rem CMake: %CMAKE% if set, else cmake on PATH, else the one bundled with Visual
rem Studio. Generator: CMake's default (the newest Visual Studio), or the
rem CMAKE_GENERATOR environment variable (CMake reads it).
setlocal EnableExtensions DisableDelayedExpansion
pushd "%~dp0" || exit /b 1
set "NOPAUSE="
set "CLANG="
set "BENCH="
:args
if "%~1"=="" goto args_done
if /i "%~1"=="--no-pause" (set "NOPAUSE=1") else if /i "%~1"=="--clang" (set "CLANG=1") else if /i "%~1"=="--bench" (set "BENCH=1") else (set "REASON=Unknown option %~1" & goto fail)
shift
goto args
:args_done
set "BUILD=%~dp0Windows_Build\build"
if defined CLANG set "BUILD=%~dp0Windows_Build\build-clang"
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
set "TOOLSET="
if defined CLANG set "TOOLSET=-T ClangCL"

echo [1/3] Configuring (%CMAKE%)...
"%CMAKE%" -S "%~dp0Windows_Build" -B "%BUILD%" %ARCH% %TOOLSET% -DCMAKE_POLICY_VERSION_MINIMUM=3.5 >>"%LOG%" 2>&1
if errorlevel 1 (set "REASON=CMake configuration failed" & goto fail)

echo [2/3] Building OxoSynth2.vst3...
"%CMAKE%" --build "%BUILD%" --config Release --target OxoSynth2VST3 synth2_storage_tests --parallel >>"%LOG%" 2>&1
if errorlevel 1 (set "REASON=Build failed" & goto fail)

echo [3/3] Storage tests...
set "TESTS=%BUILD%\bin\Release\synth2_storage_tests.exe"
if not exist "%TESTS%" (set "REASON=Storage tests not built" & goto fail)
"%TESTS%" >>"%LOG%" 2>&1
if errorlevel 1 (set "REASON=Storage tests failed" & goto fail)

if not defined BENCH goto built
echo [bench] Building and running the engine CPU bench...
"%CMAKE%" --build "%BUILD%" --config Release --target synth2_bench --parallel >>"%LOG%" 2>&1
if errorlevel 1 (set "REASON=Bench build failed" & goto fail)
set "BENCHEXE=%BUILD%\bin\Release\synth2_bench.exe"
if not exist "%BENCHEXE%" (set "REASON=Bench not built" & goto fail)
"%BENCHEXE%"
"%BENCHEXE%" "%~dp0Shared\Resources\Synth2 Starter Banks\Madame Eljay\010.sy1"

:built
echo.
echo [PASS] Built %BUILD%\VST3\Release\OxoSynth2.vst3 (not installed).
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
