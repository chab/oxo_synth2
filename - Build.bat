@echo off
setlocal EnableExtensions DisableDelayedExpansion
pushd "%~dp0"
if errorlevel 1 exit /b 1

echo Synth2 - Windows x64 VST3 integration build (STAGE 2)
echo ======================================================
echo This stage adds the first native Windows editor to the ORIGINAL sound engine.
echo It does not install a plugin or create system symlinks.
echo.

echo Closing PolyHostInterface.exe if it is running...
taskkill.exe /F /IM "PolyHostInterface.exe" >nul 2>&1
if errorlevel 1 (
    echo       No running instance found or unable to close
) else (
    echo       CLOSED
)
echo.

set "CM=%~dp0..\_Tools\cmake\_4.4.4\bin\cmake.exe"
set "SDK=%~dp0third_party\vst3sdk"
set "ZLIB_SOURCE=%~dp0Windows_Engine_Test\zlib-1.3.1"
set "ZLIB=%~dp0Windows_Build\Dependencies\zlib-1.3.1"
set "BUILD=%~dp0Windows_Build\build"
set "LOG=%~dp0Windows_Build\Build.log"
if not exist "Windows_Build" mkdir "Windows_Build"
>"%LOG%" echo Synth2 Windows VST3 - Stage 2 build log
if not exist "%CM%" goto fail_cmake
if not exist "%SDK%\CMakeLists.txt" goto fail_sdk
if not exist "%SDK%\public.sdk\source\vst\vstaudioeffect.cpp" goto fail_sdk
if not exist "%ZLIB%\zlib.h" (
    if not exist "%ZLIB_SOURCE%\zlib.h" goto fail_zlib
    echo [INFO] Copying existing zlib source into Windows_Build for a self-contained build.
    xcopy "%ZLIB_SOURCE%\*" "%ZLIB%\" /E /I /Q /H /Y >nul
    if errorlevel 1 goto fail_zlib
)

echo [1/3] Configuring MSVC x64...
"%CM%" -S "%~dp0Windows_Build" -B "%BUILD%" -G "Visual Studio 18 2026" -A x64 -DCMAKE_POLICY_VERSION_MINIMUM=3.5 >>"%LOG%" 2>&1
if errorlevel 1 goto fail_config

echo [2/3] Building Synth2 VST3 and its native Windows editor...
"%CM%" --build "%BUILD%" --config Release --target OxoSynth2VST3 synth2_storage_tests --parallel 2 >>"%LOG%" 2>&1
if errorlevel 1 goto fail_build

echo [3/3] Testing automatic normal/portable folder selection...
if not exist "%BUILD%\bin\Release\synth2_storage_tests.exe" goto fail_tests
"%BUILD%\bin\Release\synth2_storage_tests.exe" >>"%LOG%" 2>&1
if errorlevel 1 goto fail_tests

echo. 
echo [PASS] VST3 processor and controller compile: check Build.log for output path.
echo [PASS] Portable data path tests.
echo [PASS] Native Windows editor compiled into the VST3 binary.
echo [PENDING] Editor display and interaction need to be checked in a VST3 host.
echo [PENDING] DAW open/display test, GUI parity, validator, and reference audio comparisons.
>>"%LOG%" echo [PASS] Stage 2 VST3 compilation and portable path tests
>>"%LOG%" echo [PENDING] DAW GUI verification, remaining editor features and reference audio validation
set "EXIT_CODE=0"
goto done

:fail_cmake
set "REASON=Shared CMake missing at ..\_Tools\cmake\_4.4.4\bin\cmake.exe"
goto fail
:fail_sdk
set "REASON=VST3 SDK missing or incomplete at third_party\vst3sdk"
goto fail
:fail_zlib
set "REASON=zlib 1.3.1 missing from Windows_Engine_Test or copy failed"
goto fail
:fail_config
set "REASON=CMake configuration failed"
goto fail
:fail_build
set "REASON=VST3 build failed"
goto fail
:fail_tests
set "REASON=Portable path test failed to build or run"
goto fail
:fail
echo.
echo [FAIL] %REASON%
echo [INFO] Read Windows_Build\Build.log for the actual error.
>>"%LOG%" echo [FAIL] %REASON%
set "EXIT_CODE=1"
:done
echo.
echo Results: Windows_Build\Build.log
echo No plugin was installed or linked into your VST3 system folder.
echo.
pause
popd
exit /b %EXIT_CODE%
