#include "util.h"
#include "variant.h"
#include <stdarg.h>
#include <stdio.h>

std::wstring Fmt(const wchar_t* fmt, ...)
{
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 2047, fmt, ap);
    va_end(ap);
    buf[2047] = 0;
    return buf;
}

std::wstring Utf8ToW(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string WToUtf8(const std::wstring& w)
{
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}

std::wstring Trim(const std::wstring& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) ++a;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n')) --b;
    return s.substr(a, b - a);
}

std::wstring Lower(const std::wstring& s)
{
    std::wstring r = s;
    for (size_t i = 0; i < r.size(); ++i)
        if (r[i] >= L'A' && r[i] <= L'Z') r[i] = (wchar_t)(r[i] + 32);
    return r;
}

std::wstring ExePath()
{
    wchar_t b[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(NULL, b, MAX_PATH * 2);
    return std::wstring(b, n);
}

std::wstring ExeDir()
{
    std::wstring p = ExePath();
    size_t i = p.find_last_of(L'\\');
    return i == std::wstring::npos ? p : p.substr(0, i);
}

static CRITICAL_SECTION g_cs;
static bool g_csInit = false;
static std::wstring g_tag;
static std::wstring g_logPath;
static LogSink g_sink = NULL;

void LogInit(const wchar_t* tag)
{
    if (!g_csInit) { InitializeCriticalSection(&g_cs); g_csInit = true; }
    g_tag = tag;
}

void SetLogSink(LogSink sink) { g_sink = sink; }
void SetLogPath(const std::wstring& p) { g_logPath = p; }
std::wstring LogPath() { return g_logPath.empty() ? ExeDir() + L"\\" V_FILE_STEM L".log" : g_logPath; }

static void AppendLine(const std::wstring& line)
{
    std::wstring path = LogPath();
    for (int pass = 0; pass < 2; ++pass) {
        HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return;
        DWORD size = GetFileSize(h, NULL);
        if (size > 256 * 1024 && pass == 0) {
            CloseHandle(h);
            MoveFileExW(path.c_str(), (path + L".old").c_str(), MOVEFILE_REPLACE_EXISTING);
            continue;
        }
        DWORD w;
        if (size == 0) { const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF }; WriteFile(h, bom, 3, &w, NULL); }
        std::string u = WToUtf8(line + L"\r\n");
        WriteFile(h, u.data(), (DWORD)u.size(), &w, NULL);
        CloseHandle(h);
        return;
    }
}

void Log(const std::wstring& msg)
{
    if (!g_csInit) LogInit(L"");
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstring line = Fmt(L"%04d-%02d-%02d %02d:%02d:%02d [%ls] %ls",
                            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                            g_tag.c_str(), msg.c_str());
    EnterCriticalSection(&g_cs);
    AppendLine(line);
    LeaveCriticalSection(&g_cs);
    LogSink s = g_sink;
    if (s) s(line);
}
