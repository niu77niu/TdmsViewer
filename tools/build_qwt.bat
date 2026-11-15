@echo off
rem ---------------------------------------------------------------------------
rem Builds the bundled Qwt 7 (third_party/QWT) and installs it to
rem third_party/qwt-install so that the viewer can find_package(qwt).
rem Usage: tools\build_qwt.bat [Release|Debug] [rebuild]
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
if not defined QT_DIR set "QT_DIR=C:\Qt\6.11.2\msvc2022_64"
if not defined CMAKE_EXE set "CMAKE_EXE=C:\Qt\Tools\CMake_64\bin\cmake.exe"
if not exist "!CMAKE_EXE!" set "CMAKE_EXE=cmake"
if not defined NINJA_EXE set "NINJA_EXE=C:\Qt\Tools\Ninja\ninja.exe"
set "CFG=Release"
if /i "%~1"=="Debug" set "CFG=Debug"

if not defined VSCMD_VER (
    call "%ROOT%\tools\env.bat"
    if errorlevel 1 (
        echo [ERROR] Failed to configure the MSVC environment.
        exit /b 1
    )
)

set "SRC=%ROOT%\third_party\QWT"
set "BUILD=%ROOT%\third_party\QWT-build"
set "INSTALL=%ROOT%\third_party\qwt-install"

if /i "%~2"=="rebuild" if exist "%BUILD%" rmdir /s /q "%BUILD%"

echo [INFO] Configuring Qwt 7 (%CFG%) ...
"!CMAKE_EXE!" -S "%SRC%" -B "%BUILD%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=%CFG% ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
    -DCMAKE_MAKE_PROGRAM="!NINJA_EXE!" ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL%" ^
    -DQWT_CONFIG_BUILD_EXAMPLE=OFF ^
    -DQWT_CONFIG_BUILD_PLAYGROUND=OFF ^
    -DQWT_CONFIG_BUILD_STATIC_EXAMPLE=OFF ^
    -DQWT_CONFIG_BUILD_TESTS=OFF ^
    -DQWT_CONFIG_BUILD_BINDINGS=OFF
if errorlevel 1 exit /b 1

echo [INFO] Building Qwt 7 ...
"!CMAKE_EXE!" --build "%BUILD%" --parallel
if errorlevel 1 exit /b 1

echo [INFO] Installing to %INSTALL% ...
"!CMAKE_EXE!" --install "%BUILD%"
if errorlevel 1 exit /b 1

echo [OK] Qwt 7 installed: %INSTALL%
endlocal
