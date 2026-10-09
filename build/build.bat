@echo off
rem NTP time sync tool - build script (32-bit, MinGW-w64). Keep this file pure ASCII with CRLF line endings.
rem Usage: build\build.bat                 -> dist\NTPTimeSync.exe            (public edition, requests administrator)
rem        build\build.bat intranet        -> dist\NTPTimeSync_Intranet.exe   (intranet edition, requests administrator)
rem        build\build.bat dev             -> build\dev\NTPTimeSync_dev.exe   (asInvoker manifest, for testing)
rem        build\build.bat intranet dev    -> build\dev\NTPTimeSync_Intranet_dev.exe
pushd "%~dp0.."
set "GPP=C:\msys64\mingw32\bin\g++.exe"
set "WINDRES=C:\msys64\mingw32\bin\windres.exe"
if not exist "%GPP%" goto :notool
set "PATH=C:\msys64\mingw32\bin;%PATH%"
if not exist build\obj mkdir build\obj
if not exist dist mkdir dist
if not exist src\resource\app.ico python build\make_icon.py src\resource\app.ico
set "DEFS="
set "OUT=dist\NTPTimeSync.exe"
set "DEVFLAG=0"
set "INTRA=0"
for %%A in (%*) do (
  if /i "%%A"=="dev" set "DEVFLAG=1"
  if /i "%%A"=="intranet" set "INTRA=1"
)
if "%INTRA%"=="1" set "DEFS=%DEFS% -DINTRANET_BUILD"
if "%INTRA%"=="1" set "OUT=dist\NTPTimeSync_Intranet.exe"
if "%DEVFLAG%"=="1" set "DEFS=%DEFS% -DDEV_BUILD"
if "%DEVFLAG%"=="1" if not exist build\dev mkdir build\dev
if "%DEVFLAG%"=="1" set "OUT=build\dev\NTPTimeSync_dev.exe"
if "%DEVFLAG%"=="1" if "%INTRA%"=="1" set "OUT=build\dev\NTPTimeSync_Intranet_dev.exe"
"%WINDRES%" --codepage=65001 -Isrc -Isrc\resource %DEFS% -i src\resource\app.rc -o build\obj\app_res.o
if errorlevel 1 goto :fail
"%GPP%" -std=c++17 -Os -DNDEBUG -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 %DEFS% ^
 -finput-charset=UTF-8 -fexec-charset=UTF-8 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type ^
 -Isrc -municode -mwindows -static -s -ffunction-sections -fdata-sections -Wl,--gc-sections ^
 -Wl,--major-subsystem-version=6 -Wl,--minor-subsystem-version=1 ^
 -o "%OUT%" src\main.cpp src\util.cpp src\ntp.cpp src\config.cpp src\engine.cpp src\autostart.cpp src\i18n.cpp build\obj\app_res.o ^
 -lws2_32 -lcomctl32 -lshell32 -luser32 -lgdi32 -ladvapi32 -lkernel32
if errorlevel 1 goto :fail
echo BUILD OK: %OUT%
popd
exit /b 0
:notool
echo MinGW 32-bit toolchain not found at C:\msys64\mingw32
popd
exit /b 2
:fail
echo BUILD FAILED
popd
exit /b 1
