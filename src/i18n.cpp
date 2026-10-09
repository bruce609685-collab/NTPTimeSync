#include "i18n.h"
#include "util.h"
#include "resource/resource.h"
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <vector>

// ---------------------------------------------------------------- English defaults
// The single source of truth for the shipped wording.  Translators override by key, never by index.
namespace {

struct D { const wchar_t* k; const wchar_t* v; };

const D kDefaults[] = {
// ---- window, toolbar
{ L"ui.title",              L"NTP Time Sync Tool v2.0" },
{ L"ui.btn.auto",           L"Auto-Sync (Best)" },
{ L"ui.btn.sel",            L"Sync Selected" },
{ L"ui.btn.probe",          L"Probe All Servers" },
{ L"ui.btn.add",            L"Add…" },
{ L"ui.btn.del",            L"Delete" },
{ L"ui.btn.restore",        L"Restore Built-ins" },
{ L"ui.btn.update",         L"Check for Updates" },
// ---- list view
{ L"ui.col.name",           L"Name" },
{ L"ui.col.server",         L"Server" },
{ L"ui.col.status",         L"Status" },
{ L"ui.col.delay",          L"Latency" },
{ L"ui.col.stratum",        L"Tier" },
{ L"ui.col.offset",         L"Time Offset" },
{ L"ui.status.notProbed",   L"Not Probed" },
{ L"ui.status.ok",          L"Available" },
{ L"ui.status.fast",        L"Available (fastest)" },
{ L"ui.status.used",        L"Available (used)" },
{ L"ui.serverCount",        L"%d servers" },
// ---- settings group
{ L"ui.group.settings",     L"Automatic Sync" },
{ L"ui.chk.autostart",      L"Start at logon (silent)" },
{ L"ui.chk.syncOnStart",    L"Sync once on every start" },
{ L"ui.chk.timed",          L"Timed sync" },
{ L"ui.every",              L"Every" },
{ L"ui.minutes",            L"min" },
{ L"ui.belowThreshold",     L"Leave clock alone below" },
{ L"ui.ms",                 L"ms" },
{ L"ui.hint",               L"Autostart runs via a scheduled task about 15 s after logon. Manual sync always applies, regardless of the threshold." },
{ L"ui.log.label",          L"Activity Log" },
// ---- clock line / status bar
{ L"ui.localTime",          L"System time: %04d-%02d-%02d  %02d:%02d:%02d" },
{ L"ui.status.ready",       L"Ready" },
{ L"ui.status.probing",     L"Probing servers…" },
{ L"ui.status.syncing",     L"Syncing…" },
{ L"ui.status.probingAndSyncing", L"Probing and syncing…" },
{ L"ui.status.threadFailed",L"Could not create a worker thread" },
{ L"ui.status.listEmpty",   L"The server list is empty; add a server first" },
{ L"ui.status.openPage",    L"Project page opened in the browser; the latest release is listed there" },
{ L"ui.status.probeDone",   L"Probe done: %d/%d available, fastest: %s (%s)" },
// ---- add-server dialog
{ L"ui.add.title",          L"Add NTP Server" },
{ L"ui.add.name",           L"Name (optional):" },
{ L"ui.add.addr",           L"Server address:" },
{ L"ui.add.hint",           L"Domain name or IP address. Port defaults to 123; write host:port if needed." },
{ L"ui.add.ok",             L"OK" },
{ L"ui.add.cancel",         L"Cancel" },
// ---- message boxes
{ L"ui.msg.dupServer",      L"Address already in the list: %s" },
{ L"ui.msg.deleteOne",      L"Remove server \"%s\" from the list?" },
{ L"ui.msg.deleteMany",     L"Remove %d selected servers from the list?" },
{ L"ui.msg.browserFailed",  L"Could not open the browser. Please open it manually and go to:" },
// ---- tray
{ L"ui.tray.tip",           L"NTP Time Sync Tool" },
{ L"ui.tray.bgHint",        L"NTP Time Sync Tool\nStill running in the background; it will sync on schedule. Right-click this icon to exit." },
{ L"ui.tray.show",          L"Show Main Window" },
{ L"ui.tray.syncNow",       L"Sync Now" },
{ L"ui.tray.exit",          L"Exit" },
// ---- log lines (written to NTPSync.log and shown in the log box)
{ L"log.tag.silent",        L"Silent" },
{ L"log.tag.gui",           L"GUI" },
{ L"log.started",           L"Program started" },
{ L"log.startedSilent",     L"Program started silently" },
{ L"log.warning",           L"Warning: " },
{ L"log.probeStart",        L"Probing %d servers…" },
{ L"log.manualSync",        L"Manual sync using server: %s" },
{ L"log.autoSync",          L"Auto sync: picking the lowest-latency server" },
{ L"log.clickSync",         L"One-click sync: picking the lowest-latency server" },
{ L"log.syncOK",            L"Synced via %s: %s" },
{ L"log.syncSkip",          L"Within threshold via %s: %s" },
{ L"log.syncFail",          L"Sync failed: %s" },
{ L"log.retryIn",           L"Start-up sync failed; retrying in %lu s (attempt %d)" },
{ L"log.silentDone",        L"Silent run finished; exiting" },
{ L"log.timedEnabled",      L"Timed sync enabled: every %d min; the window will minimize to the tray instead of closing" },
{ L"log.timedDisabled",     L"Timed sync disabled" },
{ L"log.timedFired",        L"Timed sync fired" },
{ L"log.resumeSync",        L"Resumed from sleep; syncing in 15 s" },
{ L"log.configWriteFailed", L"Could not write the config file: %s" },
{ L"log.autostartOn",       L"Autostart enabled (scheduled task, runs silently about 15 s after logon)" },
{ L"log.autostartOff",      L"Autostart disabled" },
{ L"log.autostartMoveHint", L"Do not move this program; if you do, open it once to fix the autostart path" },
{ L"log.autostartPathFix",  L"Program moved; autostart path updated" },
{ L"log.autostartFailed",   L"Could not configure autostart: %s" },
{ L"log.added",             L"Added server: %s (%s)" },
{ L"log.deleted",           L"Removed server: %s (%s)" },
{ L"log.restored",          L"Restored %d built-in server(s) that had been removed" },
{ L"log.allPresent",        L"All built-in servers are already in the list" },
{ L"log.browserOpened",     L"Opened the project page: %s" },
{ L"log.browserFailed",     L"Could not open the browser; please visit manually: %s" },
// ---- engine
{ L"eng.onlyTwo",           L"Warning: only two servers answered and they differ by %s; using the lower-latency one" },
{ L"eng.excluded",          L"Excluded %s, which disagrees with the majority by %s" },
{ L"eng.emptyList",         L"The server list is empty" },
{ L"eng.noSelection",       L"No server selected" },
{ L"eng.allUnreachable",    L"All %d servers are unreachable. Check your network or firewall (UDP port 123)" },
{ L"eng.probeProgress",     L"Probing %d servers…" },
{ L"eng.chosen",            L"%d/%d available; selected %s (latency %s)" },
{ L"eng.fetchFailed",       L"Time query failed - %s" },
{ L"eng.source",            L"Time source %s (%s, tier %d, latency %s), local offset %s" },
{ L"eng.belowThreshold",    L"Offset %s is within the %d ms threshold; no correction needed" },
{ L"eng.setFailed",         L"Could not set the system clock: %s" },
{ L"eng.resyncFailed",      L"Correction retry failed: %s" },
// ---- NTP status and errors
{ L"ntp.ok",                L"Available" },
{ L"ntp.dns",               L"Name did not resolve" },
{ L"ntp.timeout",           L"No response" },
{ L"ntp.neterr",            L"Network error" },
{ L"ntp.badresp",           L"Invalid reply" },
{ L"ntp.kod",               L"Refused" },
{ L"ntp.unsync",            L"Server not synced" },
{ L"ntp.insane",            L"Untrusted date" },
{ L"ntp.sendFail",          L"Send failed (error %d)" },
{ L"ntp.selectFail",        L"select failed (error %d)" },
{ L"ntp.recvFail",          L"Receive failed (error %d)" },
{ L"ntp.portUnreachable",   L"Port unreachable" },
{ L"ntp.replyShort",        L"Reply too short (%d bytes)" },
{ L"ntp.replyMode",         L"Unexpected reply mode (%d)" },
{ L"ntp.replyMismatch",     L"Reply does not match the request" },
{ L"ntp.replyNoStamp",      L"Reply carries no timestamps" },
{ L"ntp.replyDate",         L"Server returned an implausible date" },
{ L"ntp.kodReason",         L"Server refused the request: " },
{ L"ntp.kodUnsync",         L"The server itself is not synchronized to a time source" },
{ L"ntp.badAddress",        L"Invalid address format" },
{ L"ntp.socketFail",        L"Could not create a socket (error %d)" },
{ L"ntp.noPermission",      L"Run as administrator to change the system time" },
{ L"ntp.noPermissionShort", L"No permission to change the system time (administrator required)" },
{ L"ntp.targetRange",       L"Target time is outside the supported range" },
{ L"ntp.setSystemTime",     L"SetSystemTime failed (error %lu)" },
{ L"ntp.offsetDays",        L"%s%dd %02d:%02d:%02d" },
{ L"ntp.offsetHMS",         L"%s%02d:%02d:%02d" },
{ L"ntp.offsetSecs",        L"%s%.3f s" },
{ L"ntp.offsetMs",          L"%s%.1f ms" },
// ---- config validation
{ L"cfg.emptyAddress",      L"Address must not be empty" },
{ L"cfg.tooLong",           L"Address is too long" },
{ L"cfg.badChars",          L"Only letters, digits, dot, hyphen and an optional :port are allowed" },
{ L"cfg.header",            L"; NTP Time Sync Tool config (edit with Notepad, save as UTF-8)" },
// ---- scheduled task
{ L"as.tempFile",           L"Could not create the temporary file" },
{ L"as.createFailed",       L"Could not create the scheduled task" },
{ L"as.createNeedsAdmin",   L"Creating the autostart task requires administrator rights" },
{ L"as.deleteFailed",       L"Could not delete the scheduled task (administrator rights may be required)" },
// ---- language picker


// ---- engine result strings (used by main.cpp to compose outcome text)
{ L"eng.corrected",         L"Corrected " },
{ L"eng.residualAfter",     L", residual " },
{ L"eng.notVerified",       L" (verification unavailable)" },
{ L"eng.failPlain",         L"Sync failed" },
{ L"eng.failWithDetail",    L"Sync failed: %s" },
{ L"eng.residualAgain",     L"Residual offset still %s; correcting again" },
// ---- GUI outcome lines
{ L"ui.last.synced",        L"Last sync: %s%s corrected %s" },
{ L"ui.last.checked",       L"Last check: %s%s offset %s" },
{ L"ui.last.failed",        L"Last sync failed: %s" },
{ L"ui.status.syncOK",      L"Sync succeeded: %s" },
{ L"ui.status.noChange",    L"No correction needed: %s" },
{ L"ui.status.syncFail",    L"Sync failed: %s" },
{ L"ui.serverIn",          L" (%s) " },
{ L"ui.status.lang",        L"Language: %s" },
{ L"log.silentExit",        L"Auto-sync on start is off and timed sync is disabled; the silent process exits" },
{ L"log.exported",          L"Exported %d translation keys to strings.default.ini" },
{ L"log.langSwitched",      L"Language switched to %s" },
{ L"log.layoutTight",      L"Settings row too narrow for this language: need %d px, have %d px (increase button/label spacing or shorten ui.belowThreshold)" },
// ---- built-in server names (translated by a pack; the addresses never change)
{ L"ntpName.aliyun",        L"Alibaba Cloud NTP" },
{ L"ntpName.aliyun1",       L"Alibaba Cloud NTP1" },
{ L"ntpName.aliyun2",       L"Alibaba Cloud NTP2" },
{ L"ntpName.aliyun3",       L"Alibaba Cloud NTP3" },
{ L"ntpName.tencent",       L"Tencent Cloud NTP" },
{ L"ntpName.tencent1",      L"Tencent Cloud NTP1" },
{ L"ntpName.tencent2",      L"Tencent Cloud NTP2" },
{ L"ntpName.tencent3",      L"Tencent Cloud NTP3" },
{ L"ntpName.nim1",          L"National Institute of Metrology 1" },
{ L"ntpName.nim2",          L"National Institute of Metrology 2" },
{ L"ntpName.ntsc",          L"National Time Service Center" },
{ L"ntpName.cnntp",         L"CN NTP Pool" },
{ L"ntpName.timeedu",       L"CERNET Time Service" },
{ L"ntpName.neu",           L"Northeastern University" },
{ L"ntpName.bupt",          L"Beijing University of Posts and Telecom" },
{ L"ntpName.fudan",         L"Fudan University" },
{ L"ntpName.sjtu",          L"Shanghai Jiao Tong University" },
{ L"ntpName.tuna",          L"Tsinghua TUNA" },
{ L"ntpName.ustc",          L"USTC" },
{ L"ntpName.cstnet",        L"CASTNet" },
{ L"ntpName.pool0",         L"NTP Pool CN 0" },
{ L"ntpName.pool1",         L"NTP Pool CN 1" },
{ L"ntpName.pool2",         L"NTP Pool CN 2" },
{ L"ntpName.pool3",         L"NTP Pool CN 3" },
// ---- international servers (user-facing name; the address itself never changes)
{ L"ntpName.intPool",       L"NTP Pool" },
{ L"ntpName.intPoolUs",     L"NTP Pool US" },
{ L"ntpName.intPoolEu",     L"NTP Pool Europe" },
{ L"ntpName.intPoolAs",     L"NTP Pool Asia" },
{ L"ntpName.intCloudflare", L"Cloudflare Time" },
{ L"ntpName.intGoogle",     L"Google Time" },
{ L"ntpName.intApple",      L"Apple Time" },
{ L"ntpName.intWindows",    L"Microsoft Time" },
{ L"ntpName.intFacebook",   L"Meta Time" },
{ L"ntpName.intUbuntu",     L"Ubuntu NTP" },
{ L"ntpName.intNist",       L"NIST (US)" },
{ L"ntpName.intAws",        L"Amazon Time" },
};

const size_t kDefaultCount = sizeof(kDefaults) / sizeof(kDefaults[0]);

std::map<std::wstring, std::wstring> g_override;   // from lang\<code>.ini
std::vector<wchar_t> g_values;                     // pointers handed out by Tc() stay valid here
std::map<std::wstring, const wchar_t*> g_bound;
int g_current = -1;

struct Lang2 { const wchar_t* code; const wchar_t* native; const wchar_t* english; };
const Lang2 kLangs[] = {
    { L"en",    L"English",                             L"English" },
        { L"zh-CN", L"\u7b80\u4f53\u4e2d\u6587", L"Simplified Chinese" },
    { L"zh-TW", L"\u7e41\u9ad4\u4e2d\u6587", L"Traditional Chinese" },
    { L"fr-FR", L"Fran\u00e7ais",           L"French" },
    { L"it-IT", L"Italiano",                 L"Italian" },
    { L"ru-RU", L"\u0420\u0443\u0441\u0441\u043a\u0438\u0439", L"Russian" },
    { L"ja-JP", L"\u65e5\u672c\u8a9e",       L"Japanese" },
};
const int kLangCount = sizeof(kLangs) / sizeof(kLangs[0]);
// RT_RCDATA id per pack; must match the order in kLangs[] and resource.h.
const int kLangIds[] = { 0, IDR_LANG_ZHCNT, IDR_LANG_ZHTW, IDR_LANG_FR, IDR_LANG_IT, IDR_LANG_RU, IDR_LANG_JA };
// module holding the packs; NULL = the exe itself (the tests link their own)
HMODULE g_resModule = NULL;


// A pack is a UTF-8 key=value file compiled into the exe as RT_RCDATA (see app.rc).
// Loading never touches the filesystem, so the exe carries all its translations.
bool LoadPack(int index, std::wstring* err)
{
    g_override.clear();
    if (index <= 0) return true;                       // English is built in
    HRSRC res = FindResourceW(g_resModule, MAKEINTRESOURCEW(kLangIds[index]), RT_RCDATA);
    if (!res) {
        if (err) *err = L"Embedded language resource not found";
        return false;
    }
    HGLOBAL h = LoadResource(g_resModule, res);
    const char* data = (const char*)LockResource(h);
    DWORD size = SizeofResource(g_resModule, res);
    if (!data || !size || size > 4 * 1024 * 1024) {
        if (err) *err = L"Embedded language resource is empty";
        return false;
    }
    std::string raw(data, data + size);
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF)
        raw.erase(0, 3);
    std::wstring text = Utf8ToW(raw);
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find(L'\n', pos);
        if (e == std::wstring::npos) e = text.size();
        std::wstring line = Trim(text.substr(pos, e - pos));
        pos = e + 1;
        if (line.empty() || line[0] == L';' || line[0] == L'#' || line[0] == L'[') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = Trim(line.substr(0, eq)), v = Trim(line.substr(eq + 1));
        if (!k.empty()) g_override[k] = v;
    }
    return true;}

const wchar_t* FindDefault(const wchar_t* key)
{
    for (size_t i = 0; i < kDefaultCount; ++i)
        if (wcscmp(kDefaults[i].k, key) == 0) return kDefaults[i].v;
    return NULL;
}

void Rebind()
{
    g_values.clear();
    g_bound.clear();
    for (size_t i = 0; i < kDefaultCount; ++i) {
        std::map<std::wstring, std::wstring>::iterator it = g_override.find(kDefaults[i].k);
        const wchar_t* use = (it != g_override.end()) ? it->second.c_str() : kDefaults[i].v;
        std::wstring hold = use;
        g_values.insert(g_values.end(), hold.begin(), hold.end());
        g_values.push_back(0);
    }
    // Second pass: only now that no further insert can reallocate the buffer are the
    // pointers stable.  Pointing into the vector while it still grows would leave dangling
    // pointers behind (the bug that showed raw keys in the UI).
    size_t pos = 0;
    for (size_t i = 0; i < kDefaultCount; ++i) {
        g_bound[kDefaults[i].k] = &g_values[pos];
        while (pos < g_values.size() && g_values[pos] != 0) ++pos;
        ++pos;
    }
}

} // namespace

namespace i18n {

const LangEntry* Languages(int* count)
{
    static LangEntry out[kLangCount];
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i < kLangCount; ++i) {
            wcsncpy(out[i].code, kLangs[i].code, 11); out[i].code[11] = 0;
            wcsncpy(out[i].native, kLangs[i].native, 47); out[i].native[47] = 0;
        }
        ready = true;
    }
    *count = kLangCount;
    return out;
}

const wchar_t* LangCode(int i)   { return (i >= 0 && i < kLangCount) ? kLangs[i].code : L"en"; }
const wchar_t* LangNative(int i) { return (i >= 0 && i < kLangCount) ? kLangs[i].native : L"English"; }
int StringCount()                { return (int)kDefaultCount; }

int Current() { return g_current < 0 ? 0 : g_current; }

int FindCode(const wchar_t* code)
{
    for (int i = 0; i < kLangCount; ++i)
        if (_wcsicmp(kLangs[i].code, code) == 0) return i;
    return 0;
}

bool PackExists(int index)
{
    if (index <= 0) return true;                       // English is compiled into the code
    return kLangIds[index] != 0 && FindResourceW(g_resModule, MAKEINTRESOURCEW(kLangIds[index]),
                                                 RT_RCDATA) != NULL;
}

int DetectSystemLang()
{
    wchar_t loc[16];
    int n = GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SISO639LANGNAME, loc, 16);
    if (n <= 0) return 0;
    std::wstring lang(loc);
    if (lang == L"zh") {
        wchar_t ct[16];
        if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SISO3166CTRYNAME, ct, 16) > 0) {
            std::wstring c(ct);
            if (c == L"TW" || c == L"HK" || c == L"MO") return 2;    // zh-TW
            return 1;                                                  // zh-CN
        }
        return 1;
    }
    if (lang == L"fr") return 3;
    if (lang == L"it") return 4;
    if (lang == L"ru") return 5;
    if (lang == L"ja") return 6;
    return 0;                                                          // English
}

// index is stored by the caller (Config), here we just swap the pack in
bool Select(int index, std::wstring* err)
{
    if (index < 0 || index >= kLangCount) index = 0;
    if (!LoadPack(index, err)) return false;
    g_current = index;
    Rebind();
    return true;
}

bool Init(int* detectedIndex)
{
    if (detectedIndex) *detectedIndex = DetectSystemLang();
    return Select(0, NULL);          // English first; the caller may Select() the saved language
}

std::wstring T(const wchar_t* key)
{
    std::map<std::wstring, const wchar_t*>::iterator it = g_bound.find(key);
    if (it != g_bound.end()) return it->second;
    const wchar_t* d = FindDefault(key);
    return d ? d : key;
}

const wchar_t* Tc(const wchar_t* key)
{
    std::map<std::wstring, const wchar_t*>::iterator it = g_bound.find(key);
    if (it != g_bound.end()) return it->second;
    const wchar_t* d = FindDefault(key);
    return d ? d : key;
}

std::wstring Tf(const wchar_t* key, ...)
{
    const wchar_t* fmt = Tc(key);
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, key);
    _vsnwprintf(buf, 1023, fmt, ap);
    va_end(ap);
    buf[1023] = 0;
    return buf;
}

// For translators: complete key/English file, ready to be copied and translated.
bool ExportDefaults(std::wstring* err)
{
    std::wstring path = ExeDir() + L"\\strings.default.ini";
    std::string out = "; NTP Time Sync Tool - all translation keys with their English text (v2.0)\n"
                      "; Copy to lang\\<code>.ini, translate the part after \"=\", save as UTF-8.\n"
                      "; Missing keys fall back to English, so a partial file is fine.\n\n[ui]\n";
    std::string body;
    for (size_t i = 0; i < kDefaultCount; ++i)
        body += WToUtf8(std::wstring(kDefaults[i].k)) + "=" + WToUtf8(kDefaults[i].v) + "\n";
    out += body;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { if (err) *err = L"Could not write " + path; return false; }
    DWORD w;
    WriteFile(h, out.data(), (DWORD)out.size(), &w, NULL);
    CloseHandle(h);
    return true;
}

} // namespace i18n

std::wstring Tf(const wchar_t* key, ...)
{
    const wchar_t* fmt = i18n::Tc(key);
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, key);
    _vsnwprintf(buf, 1023, fmt, ap);
    va_end(ap);
    buf[1023] = 0;
    return buf;
}
