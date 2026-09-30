@echo off
rem Builds and runs the console self-test (fake NTP servers on loopback, virtual clock). Pure ASCII, CRLF.
pushd "%~dp0.."
set "PATH=C:\msys64\mingw32\bin;%PATH%"
if not exist build\dev mkdir build\dev
g++ -std=c++17 -O1 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -finput-charset=UTF-8 -fexec-charset=UTF-8 ^
 -Wall -Wextra -Wno-unused-parameter -Isrc -municode -static -o build\dev\selftest.exe ^
 src\selftest.cpp src\util.cpp src\ntp.cpp src\config.cpp src\engine.cpp ^
 -lws2_32 -lshell32 -luser32 -ladvapi32
if errorlevel 1 goto :fail
build\dev\selftest.exe > build\dev\selftest_out.txt 2>&1
set RC=%errorlevel%
type build\dev\selftest_out.txt
popd
exit /b %RC%
:fail
echo TEST BUILD FAILED
popd
exit /b 1
