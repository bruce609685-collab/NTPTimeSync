#include <winsock2.h>
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <vector>
#include <string>
#include "resource/resource.h"
#include "util.h"
#include "ntp.h"
#include "config.h"
#include "engine.h"
#include "autostart.h"
#include "variant.h"
#include "i18n.h"

static std::wstring UiTitle() { return std::wstring(i18n::Tc(L"ui.title")) + V_TITLE_SUFFIX; }
#define WND_CLASS V_WND_CLASS
#define PROJECT_URL L"https://github.com/bruce609685-collab/NTPTimeSync"

enum {
    IDC_BTN_AUTO = 101, IDC_BTN_SEL, IDC_BTN_PROBE, IDC_BTN_ADD, IDC_BTN_DEL, IDC_BTN_RESTORE, IDC_BTN_UPDATE,
    IDC_LIST, IDC_LOG, IDC_STATUS, IDC_TIME, IDC_LOGLABEL, IDC_GROUP,
    IDC_CHK_AUTOSTART, IDC_CHK_SYNCSTART, IDC_CHK_TIMED,
    IDC_LBL_EVERY, IDC_EDT_INTERVAL, IDC_UD_INTERVAL, IDC_LBL_MIN,
    IDC_LBL_THR, IDC_EDT_THR, IDC_UD_THR, IDC_LBL_MS, IDC_LBL_HINT,
    IDC_CMB_LANG,
    IDM_TRAY_SHOW = 301, IDM_TRAY_SYNC, IDM_TRAY_EXIT
};

enum { TM_CLOCK = 1, TM_SCHED, TM_SAVE };
enum { WM_PROBE_ITEM = WM_APP + 1, WM_JOB_DONE, WM_LOGLINE, WM_TRAY, WM_AUTOSTART_STATE, WM_SHOW_MAIN };
enum { OP_PROBE = 1, OP_SYNC = 2 };

struct Row {
    Server s;
    ntp::Result r;
    bool probed;
    int mark;          // 0 none, 1 fastest, 2 used for last sync
    Row() : probed(false), mark(0) {}
};

struct Job {
    HWND hwnd;
    int op;
    std::vector<Server> servers;
    std::vector<int> idx;       // OP_PROBE: which entries of `servers` to probe
    engine::Request req;        // OP_SYNC
    bool automatic;
};

struct JobDone {
    int op;
    bool automatic;
    engine::Outcome out;
};

struct App {
    HINSTANCE inst;
    HWND hwnd, hList, hStatus, hLog, hTime, hLogLabel;
    RECT grpRect;
    HWND hBtnAuto, hBtnSel, hBtnProbe, hBtnAdd, hBtnDel, hBtnRestore, hBtnUpdate;
    HWND hChkAutoStart, hChkSyncStart, hChkTimed, hLblEvery, hEdtInterval, hUdInterval, hLblMin;
    HWND hLblThr, hEdtThr, hUdThr, hLblMs, hLblHint;
    HFONT font;
    int dpi;
    Config cfg;
    std::vector<Row> rows;
    bool loading, busy, silent, startupPhase, canSetClock, allowEdit, everShown, balloonShown, quitting;
    int startupAttempt;
    ULONGLONG nextAuto;
    int sortCol; bool sortAsc;
    std::wstring lastSync;
    HWND hCmbLang;
    NOTIFYICONDATAW nid;
    UINT taskbarCreated;
    int exitCode;
    App() : inst(NULL), hwnd(NULL), font(NULL), dpi(96), loading(false), busy(false), silent(false), startupPhase(false),
            canSetClock(true), allowEdit(false), everShown(false), balloonShown(false), quitting(false),
            startupAttempt(0), nextAuto(0), sortCol(-1), sortAsc(true), taskbarCreated(0), exitCode(0) {}
};

static App g;

static int S(int v) { return MulDiv(v, g.dpi, 96); }

// ---------------------------------------------------------------- helpers

// Width of `text` in pixels for the UI font (used so button widths follow the translation).
static int TextW(const std::wstring& text)
{
    HDC dc = GetDC(NULL);
    HGDIOBJ old = SelectObject(dc, g.font);
    SIZE sz = { 0, 0 };
    GetTextExtentPoint32W(dc, text.c_str(), (int)text.size(), &sz);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    return sz.cx;
}

static void SetCtlText(HWND h, const wchar_t* key)
{
    if (h) SetWindowTextW(h, i18n::Tc(key));
}

// (control, translation key) pairs for everything that can be re-labelled when the language changes.
static void ApplyTexts();


static void SetStatus(const std::wstring& t)
{
    SendMessageW(g.hStatus, SB_SETTEXTW, 0, (LPARAM)t.c_str());
}

static void UpdateServerCount()
{
    SendMessageW(g.hStatus, SB_SETTEXTW, 1, (LPARAM)Tf(L"ui.serverCount", (int)g.rows.size()).c_str());
}

static void LogSinkFn(const std::wstring& line)
{
    if (!g.hwnd) return;
    std::wstring* p = new std::wstring(line);
    if (!PostMessageW(g.hwnd, WM_LOGLINE, 0, (LPARAM)p)) delete p;
}

static void AppendLog(const std::wstring& line)
{
    int len = GetWindowTextLengthW(g.hLog);
    if (len > 60000) {                         // keep the box bounded: drop the oldest half
        SendMessageW(g.hLog, EM_SETSEL, 0, 30000);
        SendMessageW(g.hLog, EM_REPLACESEL, FALSE, (LPARAM)L"");
        len = GetWindowTextLengthW(g.hLog);
    }
    SendMessageW(g.hLog, EM_SETSEL, len, len);
    SendMessageW(g.hLog, EM_REPLACESEL, FALSE, (LPARAM)(line + L"\r\n").c_str());
    SendMessageW(g.hLog, EM_SCROLLCARET, 0, 0);
}

static std::wstring StatusOf(const Row& r)
{
    if (!r.probed) return i18n::Tc(L"ui.status.notProbed");
    if (r.r.status != ntp::ST_OK) return ntp::StatusText(r.r);
    if (r.mark == 2) return i18n::Tc(L"ui.status.used");
    if (r.mark == 1) return i18n::Tc(L"ui.status.fast");
    return i18n::Tc(L"ui.status.ok");
}

static void FillRow(int item, size_t i)
{
    const Row& r = g.rows[i];
    bool ok = r.probed && r.r.status == ntp::ST_OK;
    ListView_SetItemText(g.hList, item, 0, (LPWSTR)r.s.name.c_str());
    ListView_SetItemText(g.hList, item, 1, (LPWSTR)r.s.addr.c_str());
    ListView_SetItemText(g.hList, item, 2, (LPWSTR)StatusOf(r).c_str());
    std::wstring d = ok ? ntp::FormatDelay(r.r.best.delay) : L"-";
    std::wstring st = ok ? Fmt(L"%d", r.r.best.stratum) : L"-";
    std::wstring of = ok ? ntp::FormatOffset(r.r.best.offset) : L"-";
    ListView_SetItemText(g.hList, item, 3, (LPWSTR)d.c_str());
    ListView_SetItemText(g.hList, item, 4, (LPWSTR)st.c_str());
    ListView_SetItemText(g.hList, item, 5, (LPWSTR)of.c_str());
}

static int FindItem(size_t idx)
{
    LVFINDINFOW fi;
    memset(&fi, 0, sizeof(fi));
    fi.flags = LVFI_PARAM;
    fi.lParam = (LPARAM)idx;
    return ListView_FindItem(g.hList, -1, &fi);
}

static void RefreshRow(size_t idx)
{
    int item = FindItem(idx);
    if (item >= 0) { FillRow(item, idx); ListView_RedrawItems(g.hList, item, item); }
}

static int CALLBACK CompareFn(LPARAM a, LPARAM b, LPARAM)
{
    const Row& x = g.rows[(size_t)a];
    const Row& y = g.rows[(size_t)b];
    int dir = g.sortAsc ? 1 : -1;
    if (g.sortCol == 0 || g.sortCol == 1) {
        const std::wstring& p = g.sortCol == 0 ? x.s.name : x.s.addr;
        const std::wstring& q = g.sortCol == 0 ? y.s.name : y.s.addr;
        int c = CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, p.c_str(), -1, q.c_str(), -1) - 2;
        return c * dir;
    }
    bool xo = x.probed && x.r.status == ntp::ST_OK, yo = y.probed && y.r.status == ntp::ST_OK;
    if (xo != yo) return xo ? -1 : 1;                 // unavailable servers always go last
    if (!xo) return 0;
    ntp::i64 p = 0, q = 0;
    if (g.sortCol == 3) { p = x.r.best.delay; q = y.r.best.delay; }
    else if (g.sortCol == 4) { p = x.r.best.stratum; q = y.r.best.stratum; }
    else if (g.sortCol == 5) { p = x.r.best.offset < 0 ? -x.r.best.offset : x.r.best.offset;
                               q = y.r.best.offset < 0 ? -y.r.best.offset : y.r.best.offset; }
    if (p == q) return 0;
    return (p < q ? -1 : 1) * dir;
}

static void ApplySort()
{
    if (g.sortCol >= 0) ListView_SortItems(g.hList, CompareFn, 0);
}

static void UpdateSortArrow()
{
    HWND hh = ListView_GetHeader(g.hList);
    for (int i = 0; i < 6; ++i) {
        HDITEMW h;
        memset(&h, 0, sizeof(h));
        h.mask = HDI_FORMAT;
        Header_GetItem(hh, i, &h);
        h.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == g.sortCol) h.fmt |= g.sortAsc ? HDF_SORTUP : HDF_SORTDOWN;
        Header_SetItem(hh, i, &h);
    }
}

static void RebuildList()
{
    SendMessageW(g.hList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g.hList);
    for (size_t i = 0; i < g.rows.size(); ++i) {
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = (int)i;
        it.pszText = (LPWSTR)g.rows[i].s.name.c_str();
        it.lParam = (LPARAM)i;
        int item = ListView_InsertItem(g.hList, &it);
        FillRow(item, i);
    }
    ApplySort();
    SendMessageW(g.hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.hList, NULL, TRUE);
    UpdateServerCount();
}

static std::vector<int> Selected()
{
    std::vector<int> v;
    int i = -1;
    while ((i = ListView_GetNextItem(g.hList, i, LVNI_SELECTED)) >= 0) {
        LVITEMW it;
        memset(&it, 0, sizeof(it));
        it.mask = LVIF_PARAM;
        it.iItem = i;
        ListView_GetItem(g.hList, &it);
        v.push_back((int)it.lParam);
    }
    return v;
}

static void UpdateButtons()
{
    size_t sel = Selected().size();
    EnableWindow(g.hBtnAuto, !g.busy);
    EnableWindow(g.hBtnProbe, !g.busy);
    EnableWindow(g.hBtnSel, !g.busy && sel == 1);
    EnableWindow(g.hBtnAdd, !g.busy);
    EnableWindow(g.hBtnDel, !g.busy && sel >= 1);
    EnableWindow(g.hBtnRestore, !g.busy);
}

// ---------------------------------------------------------------- settings <-> controls

static int ReadInt(HWND edit, int def, int lo, int hi)
{
    wchar_t b[32];
    GetWindowTextW(edit, b, 32);
    if (!b[0]) return def;
    long v = wcstol(b, NULL, 10);
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return (int)v;
}

static void ReadControls()
{
    g.cfg.st.syncOnStart = SendMessageW(g.hChkSyncStart, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g.cfg.st.timedSync = SendMessageW(g.hChkTimed, BM_GETCHECK, 0, 0) == BST_CHECKED;
    g.cfg.st.intervalMin = ReadInt(g.hEdtInterval, g.cfg.st.intervalMin, 1, 10080);
    g.cfg.st.thresholdMs = ReadInt(g.hEdtThr, g.cfg.st.thresholdMs, 0, 3600000);
}

static void SaveCfg()
{
    if (!ConfigSave(g.cfg)) Log(std::wstring(i18n::Tc(L"log.warning")) + Tf(L"log.configWriteFailed", g.cfg.path.c_str()));
}

static ULONGLONG NowTick() { return GetTickCount64(); }

static void ScheduleNext(bool afterFailure)
{
    if (!g.cfg.st.timedSync) { g.nextAuto = 0; return; }
    ULONGLONG iv = (ULONGLONG)g.cfg.st.intervalMin * 60000ULL;
    if (afterFailure && iv > 300000ULL) iv = 300000ULL;
    g.nextAuto = NowTick() + iv;
}

// ---------------------------------------------------------------- tray

static void TrayAdd()
{
    memset(&g.nid, 0, sizeof(g.nid));
    g.nid.cbSize = sizeof(g.nid);
    g.nid.hWnd = g.hwnd;
    g.nid.uID = 1;
    g.nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g.nid.uCallbackMessage = WM_TRAY;
    g.nid.hIcon = (HICON)LoadImageW(g.inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    lstrcpynW(g.nid.szTip, i18n::Tc(L"ui.tray.tip"), 128);
    Shell_NotifyIconW(NIM_ADD, &g.nid);
}

static void TrayTip(const std::wstring& t)
{
    g.nid.uFlags = NIF_TIP;
    lstrcpynW(g.nid.szTip, (i18n::Tc(L"ui.tray.tip") + std::wstring(L"\n") + t).c_str(), 128);
    Shell_NotifyIconW(NIM_MODIFY, &g.nid);
}

static void TrayRemove() { Shell_NotifyIconW(NIM_DELETE, &g.nid); }

static void ShowMain()
{
    ShowWindow(g.hwnd, IsIconic(g.hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g.hwnd);
    g.everShown = true;
}

// ---------------------------------------------------------------- jobs

static void ProbeCbFn(void* ctx, size_t index, const ntp::Result& r)
{
    Job* j = (Job*)ctx;
    ntp::Result* copy = new ntp::Result(r);
    size_t real = (j->op == OP_PROBE && index < j->idx.size()) ? (size_t)j->idx[index] : index;
    if (!PostMessageW(j->hwnd, WM_PROBE_ITEM, (WPARAM)real, (LPARAM)copy)) delete copy;
}

static DWORD WINAPI JobThread(LPVOID p)
{
    Job* j = (Job*)p;
    JobDone* d = new JobDone();
    d->op = j->op;
    d->automatic = j->automatic;
    if (j->op == OP_PROBE) {
        std::vector<Server> sub;
        for (size_t i = 0; i < j->idx.size(); ++i) sub.push_back(j->servers[j->idx[i]]);
        Log(Tf(L"log.probeStart", (int)sub.size()));
        engine::ProbeAll(sub, 4, 2000, ProbeCbFn, j);
        d->out.kind = engine::K_SKIPPED;
    } else {
        d->out = engine::RunSync(j->req, ProbeCbFn, j);
    }
    HWND h = j->hwnd;
    delete j;
    if (!PostMessageW(h, WM_JOB_DONE, 0, (LPARAM)d)) delete d;
    return 0;
}

static void StartJob(Job* j)
{
    g.busy = true;
    UpdateButtons();
    HANDLE h = CreateThread(NULL, 0, JobThread, j, 0, NULL);
    if (!h) {
        g.busy = false;
        UpdateButtons();
        delete j;
        SetStatus(i18n::Tc(L"ui.status.threadFailed"));
        return;
    }
    CloseHandle(h);
}

static void ClearMarks()
{
    for (size_t i = 0; i < g.rows.size(); ++i) g.rows[i].mark = 0;
}

static std::vector<Server> CurrentServers()
{
    std::vector<Server> v;
    for (size_t i = 0; i < g.rows.size(); ++i) v.push_back(g.rows[i].s);
    return v;
}

static void DoProbe(const std::vector<int>& which)
{
    if (g.busy || which.empty()) return;
    for (size_t i = 0; i < which.size(); ++i) { g.rows[which[i]].probed = false; g.rows[which[i]].mark = 0; }
    RebuildList();
    Job* j = new Job();
    j->hwnd = g.hwnd; j->op = OP_PROBE; j->servers = CurrentServers(); j->idx = which; j->automatic = false;
    SetStatus(i18n::Tc(L"ui.status.probing"));
    StartJob(j);
}

static void DoProbeAll()
{
    std::vector<int> all;
    for (size_t i = 0; i < g.rows.size(); ++i) all.push_back((int)i);
    DoProbe(all);
}

static void DoSync(int manualIndex, bool automatic)
{
    if (g.busy) return;
    if (g.rows.empty()) { SetStatus(i18n::Tc(L"ui.status.listEmpty")); return; }
    ReadControls();
    ClearMarks();
    for (size_t i = 0; i < g.rows.size(); ++i) if (manualIndex < 0) g.rows[i].probed = false;
    if (manualIndex < 0) RebuildList();
    Job* j = new Job();
    j->hwnd = g.hwnd; j->op = OP_SYNC; j->automatic = automatic; j->servers = CurrentServers();
    j->req.servers = j->servers;
    j->req.manualIndex = manualIndex;
    j->req.automatic = automatic;
    j->req.thresholdMs = g.cfg.st.thresholdMs;
    if (manualIndex >= 0) Log(Tf(L"log.manualSync", g.rows[manualIndex].s.name.c_str()));
    else Log(automatic ? i18n::Tc(L"log.autoSync") : i18n::Tc(L"log.clickSync"));
    SetStatus(manualIndex >= 0 ? i18n::Tc(L"ui.status.syncing") : i18n::Tc(L"ui.status.probingAndSyncing"));
    StartJob(j);
}

static void MarkFastest()
{
    std::vector<ntp::Result> rs;
    std::vector<Server> sv;
    for (size_t i = 0; i < g.rows.size(); ++i) {
        rs.push_back(g.rows[i].probed ? g.rows[i].r : ntp::Result());
        sv.push_back(g.rows[i].s);
    }
    int best = engine::PickBest(rs, sv);
    for (size_t i = 0; i < g.rows.size(); ++i) if (g.rows[i].mark == 1) g.rows[i].mark = 0;
    if (best >= 0 && g.rows[best].mark == 0) g.rows[best].mark = 1;
}

static void OnJobDone(JobDone* d)
{
    g.busy = false;
    if (d->op == OP_PROBE) {
        MarkFastest();
        RebuildList();
        int okc = 0, best = -1;
        for (size_t i = 0; i < g.rows.size(); ++i) {
            if (g.rows[i].probed && g.rows[i].r.status == ntp::ST_OK) ++okc;
            if (g.rows[i].mark == 1) best = (int)i;
        }
        std::wstring msg = Tf(L"ui.status.probeDone", okc, (int)g.rows.size(),
                               best >= 0 ? g.rows[best].s.name.c_str() : L"",
                               best >= 0 ? ntp::FormatDelay(g.rows[best].r.best.delay).c_str() : L"");
        Log(msg);
        SetStatus(msg);
        UpdateButtons();
        return;
    }

    const engine::Outcome& o = d->out;
    MarkFastest();
    if (o.usedIndex >= 0 && (size_t)o.usedIndex < g.rows.size() && o.kind != engine::K_FAILED) {
        for (size_t i = 0; i < g.rows.size(); ++i) g.rows[i].mark = 0;
        g.rows[o.usedIndex].mark = 2;
    }
    RebuildList();

    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstring hhmm = Fmt(L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    std::wstring who = o.serverName.empty() ? L"" : Tf(L"ui.serverIn", o.serverName.c_str());
    if (o.kind == engine::K_SYNCED) {
        g.lastSync = Tf(L"ui.last.synced", hhmm.c_str(), who.c_str(), ntp::FormatOffset(o.offset).c_str());
        Log(Tf(L"log.syncOK", who.c_str(), o.message.c_str()));
        SetStatus(Tf(L"ui.status.syncOK", o.message.c_str()));
    } else if (o.kind == engine::K_SKIPPED) {
        g.lastSync = Tf(L"ui.last.checked", hhmm.c_str(), who.c_str(), ntp::FormatOffset(o.offset).c_str());
        Log(Tf(L"log.syncSkip", who.c_str(), o.message.c_str()));
        SetStatus(Tf(L"ui.status.noChange", o.message.c_str()));
    } else {
        g.lastSync = Tf(L"ui.last.failed", hhmm.c_str());
        Log(Tf(L"log.syncFail", o.message.c_str()));
        SetStatus(Tf(L"ui.status.syncFail", o.message.c_str()));
    }
    TrayTip(g.lastSync);
    UpdateButtons();

    if (!d->automatic) {
        if (o.kind == engine::K_FAILED)
            MessageBoxW(g.hwnd, o.message.c_str(), UiTitle().c_str(), MB_OK | MB_ICONWARNING);
        ScheduleNext(false);
        return;
    }

    // ----- automatic sync bookkeeping (start-up retries, periodic schedule, silent exit)
    bool failed = o.kind == engine::K_FAILED;
    if (g.startupPhase) {
        static const DWORD retrySec[] = { 20, 45, 90, 180, 300 };
        if (failed && g.startupAttempt < (int)(sizeof(retrySec) / sizeof(retrySec[0]))) {
            DWORD s = retrySec[g.startupAttempt++];
            g.nextAuto = NowTick() + (ULONGLONG)s * 1000ULL;
            Log(Tf(L"log.retryIn", (unsigned long)s, g.startupAttempt));
            return;
        }
        g.startupPhase = false;
        if (failed) g.exitCode = 1;
        if (g.silent && !g.cfg.st.timedSync) {
            Log(i18n::Tc(L"log.silentDone"));
            PostMessageW(g.hwnd, WM_CLOSE, 1, 0);
            return;
        }
    }
    ScheduleNext(failed);
}

// ---------------------------------------------------------------- layout

static void MoveCtl(HWND h, int x, int y, int w, int hgt)
{
    SetWindowPos(h, NULL, x, y, w, hgt, SWP_NOZORDER | SWP_NOACTIVATE);
}

static void DoLayout()
{
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    SendMessageW(g.hStatus, WM_SIZE, 0, 0);
    RECT sr;
    GetWindowRect(g.hStatus, &sr);
    int sbh = sr.bottom - sr.top;
    int parts[2] = { W - S(150), -1 };
    SendMessageW(g.hStatus, SB_SETPARTS, 2, (LPARAM)parts);
    H -= sbh;

    int m = S(10);
    int contentW = W - 2 * m;

    // language picker sits in the top band, right-aligned (no caption: it costs UI space)
    int comboW = S(96), pad = S(6);   // native names only; reserve top-band space for future controls
    int comboX = W - m - comboW, langX = comboX - pad;
    MoveCtl(g.hCmbLang, comboX, S(9), comboW, S(600));
    SendMessageW(g.hCmbLang, CB_SETDROPPEDWIDTH, comboW, 0);
    MoveCtl(g.hTime, m, S(10), langX - m, S(20));

    // buttons are as wide as their translation
    int bh = S(28), by = S(38), x = m;
    struct { HWND h; const wchar_t* key; int gapAfter; } bt[] = {
        { g.hBtnAuto, L"ui.btn.auto", S(8) }, { g.hBtnSel, L"ui.btn.sel", S(8) },
        { g.hBtnProbe, L"ui.btn.probe", S(16) }, { g.hBtnAdd, L"ui.btn.add", S(8) },
        { g.hBtnDel, L"ui.btn.del", S(8) }, { g.hBtnRestore, L"ui.btn.restore", 0 } };
    for (size_t i = 0; i < sizeof(bt) / sizeof(bt[0]); ++i) {
        int w = TextW(i18n::Tc(bt[i].key)) + S(24);
        MoveCtl(bt[i].h, x, by, w, bh);
        x += w + bt[i].gapAfter;
    }

    int updW = TextW(i18n::Tc(L"ui.btn.update")) + S(24);
    MoveCtl(g.hBtnUpdate, W - m - updW, by, updW, bh);

    int logH = S(96);
    int logBottom = H - m;
    int logTop = logBottom - logH;
    int labelTop = logTop - S(18);
    int grpH = S(100);
    int grpTop = labelTop - S(6) - grpH;
    int listTop = by + bh + S(10);
    int listBottom = grpTop - S(8);
    if (listBottom < listTop + S(80)) listBottom = listTop + S(80);

    MoveCtl(g.hList, m, listTop, contentW, listBottom - listTop);
    RECT oldGrp = g.grpRect;
    SetRect(&g.grpRect, m, grpTop, m + contentW, grpTop + grpH);
    if (!EqualRect(&oldGrp, &g.grpRect)) InvalidateRect(g.hwnd, NULL, TRUE);
    MoveCtl(g.hLogLabel, m, labelTop, contentW, S(16));
    MoveCtl(g.hLog, m, logTop, contentW, logBottom - logTop);

    int gx = m + S(14), r1 = grpTop + S(22), rh = S(24);
    MoveCtl(g.hChkAutoStart, gx, r1, S(250), rh - 2);
    MoveCtl(g.hChkSyncStart, gx + S(270), r1, S(300), rh - 2);
    int r2 = r1 + rh + S(2);
    // Row 2 is laid out from measured text widths: a translation in French/Russian/Japanese
    // must not collide with the fixed slots other languages happen to need.  Every label keeps
    // its own measured box, so no overlap is possible regardless of the language length.
    int x2 = gx;
    int wTimed = TextW(i18n::Tc(L"ui.chk.timed")) + S(22);
    MoveCtl(g.hChkTimed, x2, r2, wTimed, rh - 2);
    x2 += wTimed + S(4);
    MoveCtl(g.hLblEvery, x2, r2 + S(3), TextW(i18n::Tc(L"ui.every")) + S(4), S(18));
    x2 += TextW(i18n::Tc(L"ui.every")) + S(8);
    MoveCtl(g.hEdtInterval, x2, r2, S(64), S(22));
    SendMessageW(g.hUdInterval, UDM_SETBUDDY, (WPARAM)g.hEdtInterval, 0);
    x2 += S(70);
    MoveCtl(g.hLblMin, x2, r2 + S(3), TextW(i18n::Tc(L"ui.minutes")) + S(8), S(18));
    x2 += TextW(i18n::Tc(L"ui.minutes")) + S(18);
    MoveCtl(g.hLblThr, x2, r2 + S(3), TextW(i18n::Tc(L"ui.belowThreshold")) + S(6), S(18));
    x2 += TextW(i18n::Tc(L"ui.belowThreshold")) + S(12);
    MoveCtl(g.hEdtThr, x2, r2, S(78), S(22));
    SendMessageW(g.hUdThr, UDM_SETBUDDY, (WPARAM)g.hEdtThr, 0);
    x2 += S(84);
    MoveCtl(g.hLblMs, x2, r2 + S(3), TextW(i18n::Tc(L"ui.ms")) + S(8), S(18));
    int r3 = r2 + rh + S(2);
    MoveCtl(g.hLblHint, gx, r3, contentW - S(28), S(18));
    // warn in the log when a label no longer fits its row (translation too long)
    {
        int avail = gx + contentW - S(28) - gx;
        int need = x2 - gx + TextW(i18n::Tc(L"ui.ms")) + S(4);
        static bool warned = false;
        if (!warned && need > avail) {
            warned = true;
            Log(Tf(L"log.layoutTight", need, avail));
        }
    }

    // Address column takes whatever width the other columns leave.
    RECT lr;
    GetClientRect(g.hList, &lr);
    int used = 0;
    for (int i = 0; i < 6; ++i) if (i != 1) used += ListView_GetColumnWidth(g.hList, i);
    int w = lr.right - used;
    if (w < S(120)) w = S(120);
    ListView_SetColumnWidth(g.hList, 1, w);
}

// ---------------------------------------------------------------- creation

static HWND Mk(const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex, int id)
{
    HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g.hwnd,
                             (HMENU)(INT_PTR)id, g.inst, NULL);
    SendMessageW(h, WM_SETFONT, (WPARAM)g.font, TRUE);
    return h;
}

static void AddColumns()
{
    const wchar_t* names[] = { i18n::Tc(L"ui.col.name"), i18n::Tc(L"ui.col.server"), i18n::Tc(L"ui.col.status"),
                           i18n::Tc(L"ui.col.delay"), i18n::Tc(L"ui.col.stratum"), i18n::Tc(L"ui.col.offset") };
    int widths[] = { S(270), S(190), S(110), S(70), S(50), S(100) };  // name wider, address (elastic) yields the same amount
    for (int i = 0; i < 6; ++i) {
        LVCOLUMNW c;
        memset(&c, 0, sizeof(c));
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        c.fmt = (i >= 3) ? LVCFMT_RIGHT : LVCFMT_LEFT;
        c.pszText = (LPWSTR)names[i];
        c.cx = widths[i];
        ListView_InsertColumn(g.hList, i, &c);
    }
}

static void PopulateLangCombo()
{
    if (!g.hCmbLang) return;
    g.loading = true;
    int n = 0;
    const i18n::LangEntry* ls = i18n::Languages(&n);
    SendMessageW(g.hCmbLang, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < n; ++i) {
        int at = (int)SendMessageW(g.hCmbLang, CB_ADDSTRING, 0, (LPARAM)ls[i].native);
        SendMessageW(g.hCmbLang, CB_SETITEMDATA, at, i);
    }
    SendMessageW(g.hCmbLang, CB_SETCURSEL, (WPARAM)i18n::Current(), 0);
    g.loading = false;
}

static void CreateControls()
{
    NONCLIENTMETRICSW ncm;
    memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);

    g.hTime = Mk(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0, IDC_TIME);
    g.hBtnAuto = Mk(L"BUTTON", i18n::Tc(L"ui.btn.auto"), BS_DEFPUSHBUTTON | WS_TABSTOP, 0, IDC_BTN_AUTO);
    g.hBtnSel = Mk(L"BUTTON", i18n::Tc(L"ui.btn.sel"), BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_BTN_SEL);
    g.hBtnProbe = Mk(L"BUTTON", i18n::Tc(L"ui.btn.probe"), BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_BTN_PROBE);
    g.hBtnAdd = Mk(L"BUTTON", i18n::Tc(L"ui.btn.add"), BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_BTN_ADD);
    g.hBtnDel = Mk(L"BUTTON", i18n::Tc(L"ui.btn.del"), BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_BTN_DEL);
    g.hBtnRestore = Mk(L"BUTTON", i18n::Tc(L"ui.btn.restore"), BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_BTN_RESTORE);
    g.hBtnUpdate = Mk(L"BUTTON", i18n::Tc(L"ui.btn.update"), BS_PUSHBUTTON | WS_TABSTOP, 0, IDC_BTN_UPDATE);

    g.hCmbLang = Mk(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, IDC_CMB_LANG);
    SendMessageW(g.hCmbLang, CB_SETITEMHEIGHT, (WPARAM)-1, (LPARAM)S(18));
    SendMessageW(g.hCmbLang, CB_SETITEMHEIGHT, 0, (LPARAM)S(20));

    g.hList = Mk(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SHOWSELALWAYS | WS_TABSTOP, WS_EX_CLIENTEDGE, IDC_LIST);
    ListView_SetExtendedListViewStyle(g.hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    AddColumns();

    g.hChkAutoStart = Mk(L"BUTTON", i18n::Tc(L"ui.chk.autostart"), BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_CHK_AUTOSTART);
    g.hChkSyncStart = Mk(L"BUTTON", i18n::Tc(L"ui.chk.syncOnStart"), BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_CHK_SYNCSTART);
    g.hChkTimed = Mk(L"BUTTON", i18n::Tc(L"ui.chk.timed"), BS_AUTOCHECKBOX | WS_TABSTOP, 0, IDC_CHK_TIMED);
    g.hLblEvery = Mk(L"STATIC", i18n::Tc(L"ui.every"), SS_LEFT, 0, IDC_LBL_EVERY);
    g.hEdtInterval = Mk(L"EDIT", L"", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, IDC_EDT_INTERVAL);
    g.hUdInterval = CreateWindowExW(0, UPDOWN_CLASSW, L"", WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT |
                                    UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0, 0, 0, 0, g.hwnd, (HMENU)(INT_PTR)IDC_UD_INTERVAL, g.inst, NULL);
    SendMessageW(g.hUdInterval, UDM_SETBUDDY, (WPARAM)g.hEdtInterval, 0);
    SendMessageW(g.hUdInterval, UDM_SETRANGE32, 1, 10080);
    g.hLblMin = Mk(L"STATIC", i18n::Tc(L"ui.minutes"), SS_LEFT, 0, IDC_LBL_MIN);
    g.hLblThr = Mk(L"STATIC", i18n::Tc(L"ui.belowThreshold"), SS_LEFT, 0, IDC_LBL_THR);
    g.hEdtThr = Mk(L"EDIT", L"", ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, IDC_EDT_THR);
    g.hUdThr = CreateWindowExW(0, UPDOWN_CLASSW, L"", WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT |
                               UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0, 0, 0, 0, g.hwnd, (HMENU)(INT_PTR)IDC_UD_THR, g.inst, NULL);
    SendMessageW(g.hUdThr, UDM_SETBUDDY, (WPARAM)g.hEdtThr, 0);
    SendMessageW(g.hUdThr, UDM_SETRANGE32, 0, 3600000);
    g.hLblMs = Mk(L"STATIC", i18n::Tc(L"ui.ms"), SS_LEFT, 0, IDC_LBL_MS);
    g.hLblHint = Mk(L"STATIC", i18n::Tc(L"ui.hint"),
                    SS_LEFT | SS_NOPREFIX, 0, IDC_LBL_HINT);

    g.hLogLabel = Mk(L"STATIC", i18n::Tc(L"ui.log.label"), SS_LEFT, 0, IDC_LOGLABEL);
    g.hLog = Mk(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, WS_EX_CLIENTEDGE, IDC_LOG);
    SendMessageW(g.hLog, EM_SETLIMITTEXT, 200000, 0);

    g.hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, g.hwnd,
                                (HMENU)(INT_PTR)IDC_STATUS, g.inst, NULL);
    SendMessageW(g.hStatus, WM_SETFONT, (WPARAM)g.font, TRUE);
    PopulateLangCombo();
}

// ---------------------------------------------------------------- language picker
static void OnLanguageChange()
{
    int at = (int)SendMessageW(g.hCmbLang, CB_GETCURSEL, 0, 0);
    if (at == CB_ERR) return;
    int idx = (int)SendMessageW(g.hCmbLang, CB_GETITEMDATA, at, 0);
    if (idx == i18n::Current()) return;
    std::wstring err;
    if (!i18n::Select(idx, &err)) {
        i18n::Select(0, NULL);                        // English always exists
        Log(std::wstring(i18n::Tc(L"log.warning")) + err);
    }
    g.cfg.st.lang = i18n::Current();
    SaveCfg();
    PopulateLangCombo();
    ApplyTexts();
    DoLayout();
    InvalidateRect(g.hwnd, NULL, TRUE);
    Log(Tf(L"log.langSwitched", i18n::LangCode(i18n::Current())));
    SetStatus(Tf(L"ui.status.lang", i18n::LangCode(i18n::Current())));
}

// Everything that carries a label, so a language switch can relabel the window in place.
static void ApplyTexts()
{
    static const struct { HWND* h; const wchar_t* key; } items[] = {
        { &g.hBtnAuto, L"ui.btn.auto" }, { &g.hBtnSel, L"ui.btn.sel" }, { &g.hBtnProbe, L"ui.btn.probe" },
        { &g.hBtnAdd, L"ui.btn.add" }, { &g.hBtnDel, L"ui.btn.del" }, { &g.hBtnRestore, L"ui.btn.restore" },
        { &g.hBtnUpdate, L"ui.btn.update" },
        { &g.hChkAutoStart, L"ui.chk.autostart" }, { &g.hChkSyncStart, L"ui.chk.syncOnStart" },
        { &g.hChkTimed, L"ui.chk.timed" },
        { &g.hLblEvery, L"ui.every" }, { &g.hLblMin, L"ui.minutes" }, { &g.hLblThr, L"ui.belowThreshold" },
        { &g.hLblMs, L"ui.ms" }, { &g.hLblHint, L"ui.hint" }, { &g.hLogLabel, L"ui.log.label" },
    };
    for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); ++i)
        SetCtlText(*items[i].h, items[i].key);

    // column headers
    static const wchar_t* cols[] = { L"ui.col.name", L"ui.col.server", L"ui.col.status",
                                     L"ui.col.delay", L"ui.col.stratum", L"ui.col.offset" };
    HWND hh = ListView_GetHeader(g.hList);
    for (int i = 0; i < 6; ++i) {
        HDITEMW hi;
        memset(&hi, 0, sizeof(hi));
        hi.mask = HDI_TEXT;
        std::wstring t = i18n::Tc(cols[i]);
        hi.pszText = &t[0];
        hi.cchTextMax = (int)t.size();
        Header_SetItem(hh, i, &hi);
    }
    SetWindowTextW(g.hwnd, UiTitle().c_str());
    if (g.nid.hWnd) Shell_NotifyIconW(NIM_DELETE, &g.nid), TrayAdd();
    RebuildList();
}

static void ApplySettingsToControls()
{
    g.loading = true;
    SendMessageW(g.hChkSyncStart, BM_SETCHECK, g.cfg.st.syncOnStart ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.hChkTimed, BM_SETCHECK, g.cfg.st.timedSync ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(g.hEdtInterval, Fmt(L"%d", g.cfg.st.intervalMin).c_str());
    SetWindowTextW(g.hEdtThr, Fmt(L"%d", g.cfg.st.thresholdMs).c_str());
    g.loading = false;
}

static void UpdateClockLabel()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstring t = Tf(L"ui.localTime", st.wYear, st.wMonth, st.wDay,
                         st.wHour, st.wMinute, st.wSecond);
    if (!g.lastSync.empty()) t += L"        " + g.lastSync;
    SetWindowTextW(g.hTime, t.c_str());
}

// ---------------------------------------------------------------- add-server dialog

static std::wstring g_addName, g_addAddr;

static INT_PTR CALLBACK AddDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM)
{
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemTextW(dlg, IDC_ADD_NAME, i18n::Tc(L"ui.add.name"));
        SetDlgItemTextW(dlg, IDC_ADD_ADDR, i18n::Tc(L"ui.add.addr"));
        SetDlgItemTextW(dlg, IDC_ADD_HINT, i18n::Tc(L"ui.add.hint"));
        SetDlgItemTextW(dlg, IDOK, i18n::Tc(L"ui.add.ok"));
        SetDlgItemTextW(dlg, IDCANCEL, i18n::Tc(L"ui.add.cancel"));
        SetWindowTextW(dlg, i18n::Tc(L"ui.add.title"));
                SendDlgItemMessageW(dlg, IDC_ADD_NAME, EM_LIMITTEXT, 40, 0);
        SendDlgItemMessageW(dlg, IDC_ADD_ADDR, EM_LIMITTEXT, 200, 0);
        SetFocus(GetDlgItem(dlg, IDC_ADD_ADDR));
        return FALSE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
        if (LOWORD(wp) == IDOK) {
            wchar_t n[64], a[256];
            GetDlgItemTextW(dlg, IDC_ADD_NAME, n, 64);
            GetDlgItemTextW(dlg, IDC_ADD_ADDR, a, 256);
            std::wstring addr = Trim(a);
            std::wstring why = ValidateAddress(addr);
            if (why.empty()) {
                for (size_t i = 0; i < g.rows.size(); ++i)
                    if (Lower(g.rows[i].s.addr) == Lower(addr)) { why = Tf(L"ui.msg.dupServer", g.rows[i].s.name.c_str()); break; }
            }
            if (!why.empty()) {
                MessageBoxW(dlg, why.c_str(), i18n::Tc(L"ui.add.title"), MB_OK | MB_ICONINFORMATION);
                SetFocus(GetDlgItem(dlg, IDC_ADD_ADDR));
                return TRUE;
            }
            g_addAddr = addr;
            g_addName = Trim(n);
            if (g_addName.empty()) g_addName = addr;
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void DoAdd()
{
    if (g.busy) return;
    if (DialogBoxParamW(g.inst, MAKEINTRESOURCEW(IDD_ADD), g.hwnd, AddDlgProc, 0) != IDOK) return;
    Row r;
    r.s.name = g_addName;
    r.s.addr = g_addAddr;
    g.rows.push_back(r);
    g.cfg.servers = CurrentServers();
    SaveCfg();
    Log(Tf(L"log.added", r.s.name.c_str(), r.s.addr.c_str()));
    RebuildList();
    int idx = (int)g.rows.size() - 1;
    int item = FindItem((size_t)idx);
    if (item >= 0) {
        ListView_SetItemState(g.hList, -1, 0, LVIS_SELECTED);
        ListView_SetItemState(g.hList, item, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(g.hList, item, FALSE);
    }
    std::vector<int> one(1, idx);
    DoProbe(one);
}

static void DoDelete()
{
    if (g.busy) return;
    std::vector<int> sel = Selected();
    if (sel.empty()) return;
    std::wstring q = sel.size() == 1 ? Tf(L"ui.msg.deleteOne", g.rows[sel[0]].s.name.c_str())
                                     : Tf(L"ui.msg.deleteMany", (int)sel.size());
    if (MessageBoxW(g.hwnd, q.c_str(), UiTitle().c_str(), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    std::sort(sel.begin(), sel.end());
    for (size_t k = sel.size(); k-- > 0;) {
        Log(Tf(L"log.deleted", g.rows[sel[k]].s.name.c_str(), g.rows[sel[k]].s.addr.c_str()));
        g.rows.erase(g.rows.begin() + sel[k]);
    }
    g.cfg.servers = CurrentServers();
    SaveCfg();
    RebuildList();
    UpdateButtons();
}

static void DoCheckUpdate()
{
    HINSTANCE r = ShellExecuteW(g.hwnd, L"open", PROJECT_URL, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        Log(Tf(L"log.browserFailed", L"" PROJECT_URL));
        MessageBoxW(g.hwnd, (std::wstring(i18n::Tc(L"ui.msg.browserFailed")) + L"\n" PROJECT_URL).c_str(),
                UiTitle().c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }
    Log(Tf(L"log.browserOpened", L"" PROJECT_URL));
    SetStatus(i18n::Tc(L"ui.status.openPage"));
}

static void DoRestore()
{
    if (g.busy) return;
    std::vector<Server> def = DefaultServers();
    int added = 0;
    for (size_t i = 0; i < def.size(); ++i) {
        bool have = false;
        for (size_t k = 0; k < g.rows.size(); ++k) if (Lower(g.rows[k].s.addr) == Lower(def[i].addr)) { have = true; break; }
        if (!have) { Row r; r.s = def[i]; g.rows.push_back(r); ++added; }
    }
    g.cfg.servers = CurrentServers();
    SaveCfg();
    RebuildList();
    std::wstring m = added ? Tf(L"log.restored", added) : std::wstring(i18n::Tc(L"log.allPresent"));
    Log(m);
    SetStatus(m);
}

// ---------------------------------------------------------------- window procedure

static void DoExit()
{
    g.quitting = true;
    KillTimer(g.hwnd, TM_CLOCK);
    KillTimer(g.hwnd, TM_SCHED);
    ReadControls();
    SaveCfg();
    TrayRemove();
    DestroyWindow(g.hwnd);
}

static void ToggleAutoStart()
{
    bool want = SendMessageW(g.hChkAutoStart, BM_GETCHECK, 0, 0) == BST_CHECKED;
    std::wstring err;
    SetCursor(LoadCursorW(NULL, IDC_WAIT));
    bool ok = want ? autostart::Enable(ExePath(), &err) : autostart::Disable(&err);
    if (ok) {
        g.cfg.st.autoStartPath = want ? ExePath() : L"";
        SaveCfg();
        Log(want ? i18n::Tc(L"log.autostartOn") : i18n::Tc(L"log.autostartOff"));
        if (want) Log(i18n::Tc(L"log.autostartMoveHint"));
    } else {
        SendMessageW(g.hChkAutoStart, BM_SETCHECK, want ? BST_UNCHECKED : BST_CHECKED, 0);
        Log(Tf(L"log.autostartFailed", err.c_str()));
        MessageBoxW(g.hwnd, err.c_str(), UiTitle().c_str(), MB_OK | MB_ICONWARNING);
    }
    SetCursor(LoadCursorW(NULL, IDC_ARROW));
}

static DWORD WINAPI AutoStartCheckThread(LPVOID p)
{
    HWND h = (HWND)p;
    bool exists = autostart::Exists();
    LPARAM healed = 0;
    if (exists && !g.cfg.st.autoStartPath.empty() && Lower(g.cfg.st.autoStartPath) != Lower(ExePath())) {
        std::wstring err;
        if (autostart::Enable(ExePath(), &err)) healed = 1;
    } else if (exists && g.cfg.st.autoStartPath.empty()) {
        healed = 2;
    }
    PostMessageW(h, WM_AUTOSTART_STATE, exists ? 1 : 0, healed);
    return 0;
}

static LRESULT OnCustomDraw(NMLVCUSTOMDRAW* cd)
{
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
        return CDRF_NOTIFYSUBITEMDRAW;
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
        size_t idx = (size_t)cd->nmcd.lItemlParam;
        cd->clrText = GetSysColor(COLOR_WINDOWTEXT);
        if (idx < g.rows.size() && cd->iSubItem == 2) {
            const Row& r = g.rows[idx];
            if (r.probed) cd->clrText = (r.r.status == ntp::ST_OK) ? RGB(0, 128, 0) : RGB(192, 0, 0);
        }
        return CDRF_NEWFONT;
    }
    }
    return CDRF_DODEFAULT;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == g.taskbarCreated && g.taskbarCreated) { TrayAdd(); return 0; }
    switch (msg) {
    case WM_CREATE: {
        g.hwnd = hwnd;
        HDC dc = GetDC(NULL);
        g.dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(NULL, dc);
        CreateControls();
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        const wchar_t* cap = i18n::Tc(L"ui.group.settings");
        HGDIOBJ oldFont = SelectObject(dc, g.font);
        SIZE ts;
        GetTextExtentPoint32W(dc, cap, (int)wcslen(cap), &ts);
        RECT gr = g.grpRect;
        gr.top += ts.cy / 2;
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(220, 220, 220));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBr = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, gr.left, gr.top, gr.right, gr.bottom);
        SelectObject(dc, oldBr);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        RECT tr = { g.grpRect.left + S(9), g.grpRect.top, g.grpRect.left + S(9) + ts.cx + S(6), g.grpRect.top + ts.cy };
        FillRect(dc, &tr, GetSysColorBrush(COLOR_BTNFACE));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        TextOutW(dc, tr.left + S(3), tr.top, cap, (int)wcslen(cap));
        SelectObject(dc, oldFont);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED && g.hStatus) DoLayout();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mi = (MINMAXINFO*)lp;
        RECT r = { 0, 0, S(800), S(520) };
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        mi->ptMinTrackSize.x = r.right - r.left;
        mi->ptMinTrackSize.y = r.bottom - r.top;
        return 0;
    }
    case WM_TIMER:
        if (wp == TM_CLOCK) {
            if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) UpdateClockLabel();
        } else if (wp == TM_SCHED) {
            if (!g.busy && g.nextAuto && NowTick() >= g.nextAuto) {
                g.nextAuto = 0;
                if (!g.startupPhase) Log(i18n::Tc(L"log.timedFired"));
                DoSync(-1, true);
            }
        } else if (wp == TM_SAVE) {
            KillTimer(hwnd, TM_SAVE);
            ReadControls();
            SaveCfg();
            if (!g.startupPhase) { if (g.cfg.st.timedSync) ScheduleNext(false); else g.nextAuto = 0; }
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BTN_AUTO: DoSync(-1, false); break;
        case IDC_BTN_SEL: {
            std::vector<int> s = Selected();
            if (s.size() == 1) DoSync(s[0], false);
            break;
        }
        case IDC_BTN_PROBE: DoProbeAll(); break;
        case IDC_BTN_ADD: DoAdd(); break;
        case IDC_BTN_DEL: DoDelete(); break;
        case IDC_BTN_RESTORE: DoRestore(); break;
        case IDC_BTN_UPDATE: DoCheckUpdate(); break;
        case IDC_CMB_LANG:
            if (HIWORD(wp) == CBN_SELCHANGE && !g.loading) OnLanguageChange();
            break;
        case IDC_CHK_AUTOSTART: ToggleAutoStart(); break;
        case IDC_CHK_SYNCSTART:
        case IDC_CHK_TIMED:
            ReadControls();
            SaveCfg();
            if (LOWORD(wp) == IDC_CHK_TIMED) {
                if (g.cfg.st.timedSync) {
                    ScheduleNext(false);
                    Log(Tf(L"log.timedEnabled", g.cfg.st.intervalMin));
                } else { g.nextAuto = 0; Log(i18n::Tc(L"log.timedDisabled")); }
            }
            break;
        case IDC_EDT_INTERVAL:
        case IDC_EDT_THR:
            if (HIWORD(wp) == EN_CHANGE && !g.loading) SetTimer(hwnd, TM_SAVE, 700, NULL);
            break;
        case IDM_TRAY_SHOW: ShowMain(); break;
        case IDM_TRAY_SYNC: if (!g.busy) DoSync(-1, false); break;
        case IDM_TRAY_EXIT: DoExit(); break;
        }
        return 0;
    case WM_NOTIFY: {
        NMHDR* nh = (NMHDR*)lp;
        if (nh->idFrom == IDC_LIST) {
            switch (nh->code) {
            case NM_CUSTOMDRAW: return OnCustomDraw((NMLVCUSTOMDRAW*)lp);
            case LVN_ITEMCHANGED: UpdateButtons(); break;
            case NM_DBLCLK: {
                std::vector<int> s = Selected();
                if (s.size() == 1 && !g.busy) DoSync(s[0], false);
                break;
            }
            case LVN_COLUMNCLICK: {
                int c = ((NMLISTVIEW*)lp)->iSubItem;
                if (c == g.sortCol) g.sortAsc = !g.sortAsc; else { g.sortCol = c; g.sortAsc = true; }
                ApplySort();
                UpdateSortArrow();
                break;
            }
            case LVN_KEYDOWN:
                if (((NMLVKEYDOWN*)lp)->wVKey == VK_DELETE) DoDelete();
                break;
            }
        }
        return 0;
    }
    case WM_PROBE_ITEM: {
        ntp::Result* r = (ntp::Result*)lp;
        size_t i = (size_t)wp;
        if (i < g.rows.size()) {
            g.rows[i].r = *r;
            g.rows[i].probed = true;
            RefreshRow(i);
        }
        delete r;
        return 0;
    }
    case WM_JOB_DONE: {
        JobDone* d = (JobDone*)lp;
        OnJobDone(d);
        delete d;
        return 0;
    }
    case WM_LOGLINE: {
        std::wstring* s = (std::wstring*)lp;
        AppendLog(*s);
        delete s;
        return 0;
    }
    case WM_AUTOSTART_STATE:
        SendMessageW(g.hChkAutoStart, BM_SETCHECK, wp ? BST_CHECKED : BST_UNCHECKED, 0);
        if (lp == 1) { g.cfg.st.autoStartPath = ExePath(); SaveCfg(); Log(i18n::Tc(L"log.autostartPathFix")); }
        else if (lp == 2) { g.cfg.st.autoStartPath = ExePath(); SaveCfg(); }
        return 0;
    case WM_SHOW_MAIN:
        ShowMain();
        return 0;
    case WM_TRAY:
        if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) {
            if (IsWindowVisible(hwnd) && !IsIconic(hwnd)) ShowWindow(hwnd, SW_HIDE); else ShowMain();
        } else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, IDM_TRAY_SHOW, i18n::Tc(L"ui.tray.show"));
            AppendMenuW(m, MF_STRING | (g.busy ? MF_GRAYED : 0), IDM_TRAY_SYNC, i18n::Tc(L"ui.tray.syncNow"));
            AppendMenuW(m, MF_SEPARATOR, 0, NULL);
            AppendMenuW(m, MF_STRING, IDM_TRAY_EXIT, i18n::Tc(L"ui.tray.exit"));
            SetForegroundWindow(hwnd);
            TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            PostMessageW(hwnd, WM_NULL, 0, 0);
            DestroyMenu(m);
        }
        return 0;
    case WM_POWERBROADCAST:
        if ((wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) && g.cfg.st.timedSync) {
            g.nextAuto = NowTick() + 15000;          // clocks drift during sleep: re-check soon after wake-up
            Log(i18n::Tc(L"log.resumeSync"));
        }
        return TRUE;
    case WM_CLOSE:
        // Silent start-up finished (wp == 1) or the user chose to leave: really exit.
        if (wp != 1 && g.cfg.st.timedSync && !g.quitting) {
            ShowWindow(hwnd, SW_HIDE);
            if (!g.balloonShown) {
                g.balloonShown = true;
                g.nid.uFlags = NIF_INFO;
                lstrcpynW(g.nid.szInfoTitle, i18n::Tc(L"ui.tray.tip"), 64);
                lstrcpynW(g.nid.szInfo, i18n::Tc(L"ui.tray.bgHint"), 256);
                g.nid.dwInfoFlags = NIIF_INFO;
                Shell_NotifyIconW(NIM_MODIFY, &g.nid);
            }
            return 0;
        }
        DoExit();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(g.exitCode);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- entry

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int)
{
    g.inst = inst;
    std::wstring cmd = Lower(GetCommandLineW());
    g.silent = cmd.find(L"--silent") != std::wstring::npos;

    HANDLE mutex = CreateMutexW(NULL, TRUE, V_MUTEX_NAME);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(WND_CLASS, NULL);
        if (other && !g.silent) PostMessageW(other, WM_SHOW_MAIN, 0, 0);
        return 0;
    }

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    ntp::Init();
    i18n::Init(NULL);
    LogInit(g.silent ? i18n::Tc(L"log.tag.silent") : i18n::Tc(L"log.tag.gui"));

    if (cmd.find(L"--export-strings") != std::wstring::npos) {
        std::wstring e;
        if (i18n::ExportDefaults(&e)) {
            Log(Tf(L"log.exported", i18n::StringCount()));
        } else {
            Log(std::wstring(i18n::Tc(L"log.warning")) + e);
        }
        return 0;
    }

    ConfigLoad(&g.cfg);
    if (g.cfg.st.lang < 0) g.cfg.st.lang = 0;              // first run: English
    if (i18n::Current() != g.cfg.st.lang) i18n::Select(g.cfg.st.lang, NULL);
    g.rows.resize(g.cfg.servers.size());
    for (size_t i = 0; i < g.cfg.servers.size(); ++i) g.rows[i].s = g.cfg.servers[i];

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = WND_CLASS;
    RegisterClassExW(&wc);

    g.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    HDC dc = GetDC(NULL);
    int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    RECT r = { 0, 0, MulDiv(820, dpi, 96), MulDiv(640, dpi, 96) };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    if (ww > work.right - work.left) ww = work.right - work.left;
    if (wh > work.bottom - work.top) wh = work.bottom - work.top;
    int x = work.left + (work.right - work.left - ww) / 2;
    int y = work.top + (work.bottom - work.top - wh) / 2;

    HWND hwnd = CreateWindowExW(0, WND_CLASS, UiTitle().c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, ww, wh,
                                NULL, NULL, inst, NULL);
    if (!hwnd) return 1;

    SetLogSink(LogSinkFn);
    ApplyTexts();
    ApplySettingsToControls();
    RebuildList();
    DoLayout();
    UpdateClockLabel();
    UpdateButtons();
    UpdateServerCount();
    SetStatus(i18n::Tc(L"ui.status.ready"));
    TrayAdd();

    std::wstring why;
    g.canSetClock = ntp::CanSetClock(&why);
    Log(g.silent ? i18n::Tc(L"log.startedSilent") : i18n::Tc(L"log.started"));
    if (!g.canSetClock) {
        Log(std::wstring(i18n::Tc(L"log.warning")) + why);
        SetStatus(std::wstring(i18n::Tc(L"log.warning")) + why);
    }

    SetTimer(hwnd, TM_CLOCK, 1000, NULL);
    SetTimer(hwnd, TM_SCHED, 1000, NULL);

    HANDLE ash = CreateThread(NULL, 0, AutoStartCheckThread, hwnd, 0, NULL);
    if (ash) CloseHandle(ash);

    if (!g.silent) { ShowWindow(hwnd, SW_SHOW); UpdateWindow(hwnd); g.everShown = true; }

    if (g.cfg.st.syncOnStart) {
        g.startupPhase = true;
        g.nextAuto = NowTick() + (g.silent ? 500 : 1200);
    } else if (g.silent && !g.cfg.st.timedSync) {
        Log(i18n::Tc(L"log.silentExit"));
        PostMessageW(hwnd, WM_CLOSE, 1, 0);
    } else {
        ScheduleNext(false);
    }

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (IsDialogMessageW(hwnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (mutex) CloseHandle(mutex);
    return (int)m.wParam;
}
