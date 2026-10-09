#include "config.h"
#include "util.h"
#include "i18n.h"

using i18n::T;
using i18n::Tc;
#include "variant.h"
#include <shlobj.h>
#include <stdlib.h>

std::vector<Server> DefaultServers()
{
#ifdef INTRANET_BUILD
    static const wchar_t* tbl[][2] = {
        { L"长春分行服务器", L"10.178.2.161" },
        { L"生态东街NTP", L"10.176.213.1" },
        { L"总行NTP1", L"215.12.0.24" },
        { L"总行NTP2", L"215.12.0.25" },
        { L"总行NTP3", L"215.44.0.24" },
        { L"总行NTP4", L"215.44.0.25" },
    };
#else
    static const wchar_t* tbl[][2] = {
        { i18n::Tc(L"ntpName.aliyun"), L"ntp.aliyun.com" },
        { i18n::Tc(L"ntpName.aliyun1"), L"ntp1.aliyun.com" },
        { i18n::Tc(L"ntpName.aliyun2"), L"ntp2.aliyun.com" },
        { i18n::Tc(L"ntpName.aliyun3"), L"ntp3.aliyun.com" },
        { i18n::Tc(L"ntpName.tencent"), L"ntp.tencent.com" },
        { i18n::Tc(L"ntpName.tencent1"), L"ntp1.tencent.com" },
        { i18n::Tc(L"ntpName.tencent2"), L"ntp2.tencent.com" },
        { i18n::Tc(L"ntpName.tencent3"), L"ntp3.tencent.com" },
        { i18n::Tc(L"ntpName.nim1"), L"ntp1.nim.ac.cn" },
        { i18n::Tc(L"ntpName.nim2"), L"ntp2.nim.ac.cn" },
        { i18n::Tc(L"ntpName.ntsc"), L"ntp.ntsc.ac.cn" },
        { i18n::Tc(L"ntpName.cnntp"), L"cn.ntp.org.cn" },
        { i18n::Tc(L"ntpName.timeedu"), L"time.edu.cn" },
        { i18n::Tc(L"ntpName.neu"), L"ntp.neu.edu.cn" },
        { i18n::Tc(L"ntpName.bupt"), L"ntp.bupt.edu.cn" },
        { i18n::Tc(L"ntpName.fudan"), L"ntp.fudan.edu.cn" },
        { i18n::Tc(L"ntpName.sjtu"), L"ntp.sjtu.edu.cn" },
        { i18n::Tc(L"ntpName.tuna"), L"ntp.tuna.tsinghua.edu.cn" },
        { i18n::Tc(L"ntpName.ustc"), L"ntp.ustc.edu.cn" },
        { i18n::Tc(L"ntpName.cstnet"), L"ntp.cstnet.cn" },
        { i18n::Tc(L"ntpName.pool0"), L"0.cn.pool.ntp.org" },
        { i18n::Tc(L"ntpName.pool1"), L"1.cn.pool.ntp.org" },
        { i18n::Tc(L"ntpName.pool2"), L"2.cn.pool.ntp.org" },
        { i18n::Tc(L"ntpName.pool3"), L"3.cn.pool.ntp.org" },
        // ---- international
        { i18n::Tc(L"ntpName.intPool"), L"pool.ntp.org" },
        { i18n::Tc(L"ntpName.intPoolUs"), L"us.pool.ntp.org" },
        { i18n::Tc(L"ntpName.intPoolEu"), L"europe.pool.ntp.org" },
        { i18n::Tc(L"ntpName.intPoolAs"), L"asia.pool.ntp.org" },
        { i18n::Tc(L"ntpName.intCloudflare"), L"time.cloudflare.com" },
        { i18n::Tc(L"ntpName.intGoogle"), L"time.google.com" },
        { i18n::Tc(L"ntpName.intApple"), L"time.apple.com" },
        { i18n::Tc(L"ntpName.intWindows"), L"time.windows.com" },
        { i18n::Tc(L"ntpName.intFacebook"), L"time.facebook.com" },
        { i18n::Tc(L"ntpName.intUbuntu"), L"ntp.ubuntu.com" },
        { i18n::Tc(L"ntpName.intNist"), L"time.nist.gov" },
        { i18n::Tc(L"ntpName.intAws"), L"time.aws.com" },
    };
#endif
    std::vector<Server> v;
    for (size_t i = 0; i < sizeof(tbl) / sizeof(tbl[0]); ++i) {
        Server s;
        s.name = tbl[i][0];
        s.addr = tbl[i][1];
        v.push_back(s);
    }
    return v;
}

std::wstring ValidateAddress(const std::wstring& in)
{
    std::wstring a = Trim(in);
    if (a.empty()) return Tc(L"cfg.emptyAddress");
    if (a.size() > 253) return Tc(L"cfg.tooLong");
    for (size_t i = 0; i < a.size(); ++i) {
        wchar_t c = a[i];
        bool ok = (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                  c == L'.' || c == L'-' || c == L'_' || c == L':' || c == L'[' || c == L']';
        if (!ok) return Tc(L"cfg.badChars");
    }
    return L"";
}

static bool DirWritable(const std::wstring& dir)
{
    std::wstring t = dir + L"\\~ntpsync_w.tmp";
    HANDLE h = CreateFileW(t.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

// Portable first: settings live beside the exe.  If that folder is read-only (Program Files,
// a CD, a locked share) fall back to %APPDATA%\NTPSync.
std::wstring ConfigPath()
{
    static std::wstring cached;
    if (!cached.empty()) return cached;
    std::wstring dir = ExeDir();
    std::wstring beside = dir + L"\\" V_FILE_STEM L".ini";
    if (GetFileAttributesW(beside.c_str()) != INVALID_FILE_ATTRIBUTES || DirWritable(dir)) {
        cached = beside;
        return cached;
    }
    wchar_t app[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, app))) {
        std::wstring d = std::wstring(app) + L"\\" V_FILE_STEM;
        CreateDirectoryW(d.c_str(), NULL);
        cached = d + L"\\" V_FILE_STEM L".ini";
        SetLogPath(d + L"\\" V_FILE_STEM L".log");
        return cached;
    }
    cached = beside;
    return cached;
}

static bool ReadAll(const std::wstring& path, std::string* out)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz > 4 * 1024 * 1024) { CloseHandle(h); return false; }
    out->resize(sz);
    DWORD got = 0;
    bool ok = sz == 0 || ReadFile(h, &(*out)[0], sz, &got, NULL);
    CloseHandle(h);
    out->resize(got);
    return ok;
}

static int ToInt(const std::wstring& v, int def, int lo, int hi)
{
    if (v.empty()) return def;
    wchar_t* end = NULL;
    long n = wcstol(v.c_str(), &end, 10);
    if (end == v.c_str()) return def;
    if (n < lo) n = lo;
    if (n > hi) n = hi;
    return (int)n;
}

bool ConfigLoad(Config* c)
{
    c->path = ConfigPath();
    c->st = Settings();
    c->servers.clear();

    std::string raw;
    bool haveFile = ReadAll(c->path, &raw);
    bool haveServers = false;
    if (haveFile) {
        if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF)
            raw.erase(0, 3);
        std::wstring text = Utf8ToW(raw);
        std::wstring section;
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t e = text.find(L'\n', pos);
            if (e == std::wstring::npos) e = text.size();
            std::wstring line = Trim(text.substr(pos, e - pos));
            pos = e + 1;
            if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
            if (line[0] == L'[') {
                size_t r = line.find(L']');
                section = Lower(line.substr(1, r == std::wstring::npos ? std::wstring::npos : r - 1));
                if (section == L"servers") haveServers = true;
                continue;
            }
            size_t eq = line.find(L'=');
            if (eq == std::wstring::npos) continue;
            std::wstring k = Lower(Trim(line.substr(0, eq)));
            std::wstring v = Trim(line.substr(eq + 1));
            if (section == L"settings") {
                if (k == L"synconstart") c->st.syncOnStart = v != L"0";
                else if (k == L"timedsync") c->st.timedSync = v == L"1";
                else if (k == L"intervalminutes") c->st.intervalMin = ToInt(v, 60, 1, 60 * 24 * 30);
                else if (k == L"thresholdms") c->st.thresholdMs = ToInt(v, 1000, 0, 3600 * 1000);
                else if (k == L"autostartpath") c->st.autoStartPath = v;
                else if (k == L"lang") c->st.lang = ToInt(v, 0, -1, 64);
            } else if (section == L"servers") {
                size_t bar = v.find(L'|');
                Server s;
                if (bar == std::wstring::npos) { s.addr = Trim(v); s.name = s.addr; }
                else { s.name = Trim(v.substr(0, bar)); s.addr = Trim(v.substr(bar + 1)); }
                if (s.name.empty()) s.name = s.addr;
                if (ValidateAddress(s.addr).empty()) c->servers.push_back(s);
            }
        }
    }
    if (!haveServers) c->servers = DefaultServers();
    return haveFile;
}

bool ConfigSave(const Config& c)
{
    std::wstring t;
    t += std::wstring(i18n::Tc(L"cfg.header")) + L"\r\n";
    t += L"[Settings]\r\n";
    t += Fmt(L"SyncOnStart=%d\r\n", c.st.syncOnStart ? 1 : 0);
    t += Fmt(L"TimedSync=%d\r\n", c.st.timedSync ? 1 : 0);
    t += Fmt(L"IntervalMinutes=%d\r\n", c.st.intervalMin);
    t += Fmt(L"ThresholdMs=%d\r\n", c.st.thresholdMs);
    t += L"AutoStartPath=" + c.st.autoStartPath + L"\r\n";
    t += Fmt(L"Lang=%d\r\n", c.st.lang);
    t += L"\r\n[Servers]\r\n";
    for (size_t i = 0; i < c.servers.size(); ++i)
        t += Fmt(L"S%d=", (int)i + 1) + c.servers[i].name + L"|" + c.servers[i].addr + L"\r\n";

    std::string u = "\xEF\xBB\xBF" + WToUtf8(t);
    std::wstring tmp = c.path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0;
    bool ok = WriteFile(h, u.data(), (DWORD)u.size(), &w, NULL) && w == u.size();
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    return MoveFileExW(tmp.c_str(), c.path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
