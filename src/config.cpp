#include "config.h"
#include "util.h"
#include <shlobj.h>
#include <stdlib.h>

std::vector<Server> DefaultServers()
{
    static const wchar_t* tbl[][2] = {
        { L"阿里云 NTP", L"ntp.aliyun.com" },
        { L"阿里云 NTP1", L"ntp1.aliyun.com" },
        { L"阿里云 NTP2", L"ntp2.aliyun.com" },
        { L"阿里云 NTP3", L"ntp3.aliyun.com" },
        { L"腾讯云 NTP", L"ntp.tencent.com" },
        { L"腾讯云 NTP1", L"ntp1.tencent.com" },
        { L"腾讯云 NTP2", L"ntp2.tencent.com" },
        { L"腾讯云 NTP3", L"ntp3.tencent.com" },
        { L"中国计量院 1", L"ntp1.nim.ac.cn" },
        { L"中国计量院 2", L"ntp2.nim.ac.cn" },
        { L"国家授时中心", L"ntp.ntsc.ac.cn" },
        { L"国家授时中心 中国池", L"cn.ntp.org.cn" },
        { L"教育网 时间服务", L"time.edu.cn" },
        { L"东北大学", L"ntp.neu.edu.cn" },
        { L"北京邮电大学", L"ntp.bupt.edu.cn" },
        { L"复旦大学", L"ntp.fudan.edu.cn" },
        { L"上海交通大学", L"ntp.sjtu.edu.cn" },
        { L"清华大学 TUNA", L"ntp.tuna.tsinghua.edu.cn" },
        { L"中国科大", L"ntp.ustc.edu.cn" },
        { L"中科院网络中心", L"ntp.cstnet.cn" },
        { L"NTP Pool 中国 0", L"0.cn.pool.ntp.org" },
        { L"NTP Pool 中国 1", L"1.cn.pool.ntp.org" },
        { L"NTP Pool 中国 2", L"2.cn.pool.ntp.org" },
        { L"NTP Pool 中国 3", L"3.cn.pool.ntp.org" },
    };
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
    if (a.empty()) return L"地址不能为空";
    if (a.size() > 253) return L"地址过长";
    for (size_t i = 0; i < a.size(); ++i) {
        wchar_t c = a[i];
        bool ok = (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                  c == L'.' || c == L'-' || c == L'_' || c == L':' || c == L'[' || c == L']';
        if (!ok) return L"地址只能包含字母、数字、点、连字符，以及可选的 :端口";
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
    std::wstring beside = dir + L"\\NTPSync.ini";
    if (GetFileAttributesW(beside.c_str()) != INVALID_FILE_ATTRIBUTES || DirWritable(dir)) {
        cached = beside;
        return cached;
    }
    wchar_t app[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, app))) {
        std::wstring d = std::wstring(app) + L"\\NTPSync";
        CreateDirectoryW(d.c_str(), NULL);
        cached = d + L"\\NTPSync.ini";
        SetLogPath(d + L"\\NTPSync.log");
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
    t += L"; NTP 时间同步校准工具配置（可直接用记事本编辑，需以 UTF-8 保存）\r\n";
    t += L"[Settings]\r\n";
    t += Fmt(L"SyncOnStart=%d\r\n", c.st.syncOnStart ? 1 : 0);
    t += Fmt(L"TimedSync=%d\r\n", c.st.timedSync ? 1 : 0);
    t += Fmt(L"IntervalMinutes=%d\r\n", c.st.intervalMin);
    t += Fmt(L"ThresholdMs=%d\r\n", c.st.thresholdMs);
    t += L"AutoStartPath=" + c.st.autoStartPath + L"\r\n";
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
