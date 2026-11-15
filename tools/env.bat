@echo off
rem ---------------------------------------------------------------------------
rem Configures a MSVC x64 command line environment WITHOUT calling reg.exe
rem (vcvars64.bat relies on registry queries which may be blocked in sandboxes).
rem Usage:  call env.bat
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
if exist "!VSWHERE!" (
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
)
if not defined VSPATH (
    if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community" set "VSPATH=%ProgramFiles%\Microsoft Visual Studio\18\Community"
)

if not defined VSPATH (
    echo [ERROR] Visual Studio with C++ tools not found.
    exit /b 1
)

rem --- newest MSVC toolset ---
set "MSVC="
for /f "delims=" %%d in ('dir /b /ad /o-n "!VSPATH!\VC\Tools\MSVC" 2^>nul') do (
    if not defined MSVC set "MSVC=!VSPATH!\VC\Tools\MSVC\%%d"
)
if not defined MSVC (
    echo [ERROR] No MSVC toolset under !VSPATH!\VC\Tools\MSVC
    exit /b 1
)

rem --- newest Windows 10 SDK ---
set "SDKROOT=%ProgramFiles(x86)%\Windows Kits\10"
set "SDKVER="
for /f "delims=" %%d in ('dir /b /ad /o-n "!SDKROOT!\Include" 2^>nul') do (
    if not defined SDKVER set "SDKVER=%%d"
)
if not defined SDKVER (
    echo [ERROR] Windows SDK not found.
    exit /b 1
)

set "PATH=!MSVC!\bin\Hostx64\x64;!SDKROOT!\bin\!SDKVER!\x64;!PATH!"
set "INCLUDE=!MSVC!\include;!SDKROOT!\Include\!SDKVER!\ucrt;!SDKROOT!\Include\!SDKVER!\um;!SDKROOT!\Include\!SDKVER!\shared;!SDKROOT!\Include\!SDKVER!\winrt;!SDKROOT!\Include\!SDKVER!\cppwinrt"
set "LIB=!MSVC!\lib\x64;!SDKROOT!\Lib\!SDKVER!\ucrt\x64;!SDKROOT!\Lib\!SDKVER!\um\x64"
set "VSCMD_VER=manual"

endlocal & (
    set "PATH=%PATH%"
    set "INCLUDE=%INCLUDE%"
    set "LIB=%LIB%"
    set "MSVC=%MSVC%"
    set "VSPATH=%VSPATH%"
    set "VSCMD_VER=manual"
)
