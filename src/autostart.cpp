#include "autostart.h"
#include "util.h"
#include <windows.h>
#include <stdio.h>

namespace autostart {

static const wchar_t* TASK_NAME = L"NTPTimeSyncTool";

bool IsElevated()
{
    HANDLE tok;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
    TOKEN_ELEVATION e;
    DWORD n = 0;
    bool r = false;
    if (GetTokenInformation(tok, TokenElevation, &e, sizeof(e), &n)) r = e.TokenIsElevated != 0;
    CloseHandle(tok);
    return r;
}

// Runs schtasks.exe hidden and waits.  Output is discarded: only the exit code matters, so the
// result does not depend on the system language.
static bool RunSchtasks(const std::wstring& args, DWORD* exitCode)
{
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(sys) + L"\\schtasks.exe\" " + args;
    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));
    std::wstring buf = cmd;
    if (!CreateProcessW(NULL, &buf[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return false;
    DWORD w = WaitForSingleObject(pi.hProcess, 20000);
    DWORD code = 1;
    if (w == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    else TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    *exitCode = code;
    return w == WAIT_OBJECT_0;
}

bool Exists()
{
    DWORD c = 1;
    return RunSchtasks(std::wstring(L"/Query /TN \"") + TASK_NAME + L"\"", &c) && c == 0;
}

static std::wstring XmlEsc(const std::wstring& s)
{
    std::wstring r;
    for (size_t i = 0; i < s.size(); ++i) {
        switch (s[i]) {
        case L'&': r += L"&amp;"; break;
        case L'<': r += L"&lt;"; break;
        case L'>': r += L"&gt;"; break;
        case L'"': r += L"&quot;"; break;
        default: r += s[i];
        }
    }
    return r;
}

static std::wstring CurrentUser()
{
    wchar_t u[256], d[256];
    DWORD nu = GetEnvironmentVariableW(L"USERNAME", u, 256);
    DWORD nd = GetEnvironmentVariableW(L"USERDOMAIN", d, 256);
    if (nu == 0 || nu >= 256) return L"";
    if (nd == 0 || nd >= 256) return u;
    return std::wstring(d) + L"\\" + u;
}

bool Enable(const std::wstring& exePath, std::wstring* err)
{
    std::wstring user = XmlEsc(CurrentUser());
    // Highest run level needs an elevated creator; otherwise register a normal-level task so the
    // start-up itself still works (sync then reports the missing privilege in the log).
    const wchar_t* level = IsElevated() ? L"HighestAvailable" : L"LeastPrivilege";
    std::wstring x;
    x += L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n";
    x += L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n";
    x += L"<RegistrationInfo><Description>NTP time synchronisation at logon</Description></RegistrationInfo>\r\n";
    x += L"<Triggers><LogonTrigger><Enabled>true</Enabled>";
    if (!user.empty()) x += L"<UserId>" + user + L"</UserId>";
    x += L"<Delay>PT15S</Delay></LogonTrigger></Triggers>\r\n";
    x += L"<Principals><Principal id=\"Author\">";
    if (!user.empty()) x += L"<UserId>" + user + L"</UserId>";
    x += L"<LogonType>InteractiveToken</LogonType><RunLevel>" + std::wstring(level) + L"</RunLevel></Principal></Principals>\r\n";
    x += L"<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
         L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
         L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
         L"<AllowHardTerminate>true</AllowHardTerminate><StartWhenAvailable>true</StartWhenAvailable>"
         L"<AllowStartOnDemand>true</AllowStartOnDemand><Enabled>true</Enabled><Hidden>false</Hidden>"
         L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>7</Priority></Settings>\r\n";
    x += L"<Actions Context=\"Author\"><Exec><Command>" + XmlEsc(L"\"" + exePath + L"\"") +
         L"</Command><Arguments>--silent</Arguments><WorkingDirectory>" +
         XmlEsc(exePath.substr(0, exePath.find_last_of(L'\\'))) + L"</WorkingDirectory></Exec></Actions>\r\n";
    x += L"</Task>\r\n";

    wchar_t tmpDir[MAX_PATH], tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpDir);
    GetTempFileNameW(tmpDir, L"nts", 0, tmp);
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (h == INVALID_HANDLE_VALUE) { if (err) *err = L"无法写入临时文件"; return false; }
    DWORD w;
    const WORD bom = 0xFEFF;
    WriteFile(h, &bom, 2, &w, NULL);
    WriteFile(h, x.data(), (DWORD)(x.size() * sizeof(wchar_t)), &w, NULL);
    CloseHandle(h);

    DWORD code = 1;
    bool ran = RunSchtasks(std::wstring(L"/Create /TN \"") + TASK_NAME + L"\" /XML \"" + tmp + L"\" /F", &code);
    DeleteFileW(tmp);
    if (!ran || code != 0) {
        if (err) *err = IsElevated() ? L"计划任务创建失败" : L"创建开机启动任务需要管理员权限";
        return false;
    }
    return true;
}

bool Disable(std::wstring* err)
{
    if (!Exists()) return true;
    DWORD code = 1;
    bool ran = RunSchtasks(std::wstring(L"/Delete /TN \"") + TASK_NAME + L"\" /F", &code);
    if (!ran || code != 0) {
        if (err) *err = L"删除计划任务失败（可能需要管理员权限）";
        return false;
    }
    return true;
}

} // namespace autostart
