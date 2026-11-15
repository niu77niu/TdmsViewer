@echo off
rem ---------------------------------------------------------------------------
rem Build helper for TdmsViewer (MSVC x64 + Qt 6 + QWT + Ninja)
rem Usage:  build.bat [Release|Debug] [-probe]
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
rem All tool locations can be overridden from the environment so the released
rem sources build on machines with a different Qt version / install layout.
if not defined QT_DIR set "QT_DIR=C:\Qt\6.11.2\msvc2022_64"
if not defined CMAKE_EXE set "CMAKE_EXE=C:\Qt\Tools\CMake_64\bin\cmake.exe"
if not exist "!CMAKE_EXE!" set "CMAKE_EXE=cmake"
if not defined NINJA_EXE set "NINJA_EXE=C:\Qt\Tools\Ninja\ninja.exe"
set "CFG=Release"
set "PROBE=OFF"

if /i "%~1"=="Debug" set "CFG=Debug"
if /i "%~2"=="-probe" set "PROBE=ON"

if not defined VSCMD_VER (
    call "%ROOT%tools\env.bat"
    if errorlevel 1 (
        echo [ERROR] Failed to configure the MSVC environment.
        exit /b 1
    )
    echo [INFO] MSVC toolset: !MSVC!
)


rem Qwt 7 is bundled as source (third_party\QWT) and must be built once.
if not exist "%ROOT%third_party\qwt-install\lib\cmake\qwt\qwtConfig.cmake" (
    echo [INFO] Qwt 7 is not built yet - building it first ...
    call "%ROOT%tools\build_qwt.bat" %CFG%
    if errorlevel 1 exit /b 1
)

echo [INFO] Configuring (%CFG%, probe=%PROBE%) ...
"!CMAKE_EXE!" -S "%ROOT%." -B "%ROOT%build" -G Ninja ^
    -DCMAKE_BUILD_TYPE=%CFG% ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
    -DCMAKE_MAKE_PROGRAM="!NINJA_EXE!" ^
    -DBUILD_PROBE=%PROBE%
if errorlevel 1 exit /b 1

echo [INFO] Building ...
"!CMAKE_EXE!" --build "%ROOT%build" --parallel
if errorlevel 1 exit /b 1

echo [INFO] Deploying Qt runtime ...
"%QT_DIR%\bin\windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler ^
    "%ROOT%build\TdmsViewer.exe" >nul 2>&1

echo [OK] Output: %ROOT%build\TdmsViewer.exe
endlocal
