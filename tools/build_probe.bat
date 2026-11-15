@echo off
rem Standalone build of the TDM SDK probe.
setlocal
set "ROOT=%~dp0"
call "%~dp0env.bat"
if errorlevel 1 exit /b 1

cd /d "%ROOT%"
if not exist out mkdir out
cl /nologo /EHsc /std:c++17 /utf-8 /W3 /I "%ROOT%..\TDM C DLL\dev\include" ^
   probe.cpp ^
   /Fe:out\probe.exe ^
   /link "%ROOT%..\TDM C DLL\dev\lib\64-bit\msvc64\nilibddc.lib"
if errorlevel 1 exit /b 1

xcopy /Y /E /I /Q "%ROOT%..\TDM C DLL\dev\bin\64-bit" "out" >nul
if not exist out\probe_files mkdir out\probe_files
cd out
probe.exe
endlocal
