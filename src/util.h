#pragma once
#include <windows.h>
#include <stdint.h>
#include <string>

std::wstring Fmt(const wchar_t* fmt, ...);
std::wstring Utf8ToW(const std::string& s);
std::string WToUtf8(const std::wstring& s);
std::wstring Trim(const std::wstring& s);
std::wstring Lower(const std::wstring& s);

std::wstring ExePath();
std::wstring ExeDir();

// Log lines go to NTPSync.log next to the exe and to an optional sink (the GUI log box).
typedef void (*LogSink)(const std::wstring& line);
void LogInit(const wchar_t* tag);
void SetLogSink(LogSink sink);
void SetLogPath(const std::wstring& path);
void Log(const std::wstring& msg);
std::wstring LogPath();
