#pragma once
#include <string>

// Boot-time start is a Task Scheduler logon task ("run with highest privileges"), which is the only
// documented way for a program that must change the system clock to start at logon without a UAC
// prompt.  Creating such a task needs administrator rights, which the elevated program has.
namespace autostart {

bool IsElevated();
bool Exists();
bool Enable(const std::wstring& exePath, std::wstring* err);
bool Disable(std::wstring* err);

} // namespace autostart
