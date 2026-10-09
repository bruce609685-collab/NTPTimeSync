#include "ntp.h"
#include "util.h"
#include "i18n.h"

using i18n::T;
using i18n::Tc;
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>

namespace ntp {

static const i64 NTP_TO_FILETIME_SECS = 9435484800LL;   // 1900-01-01 -> 1601-01-01, in seconds
static LARGE_INTEGER g_freq;
typedef VOID (WINAPI *PFN_PRECISE)(LPFILETIME);
static PFN_PRECISE g_precise = NULL;
static i64 (*g_nowHook)() = NULL;
static bool (*g_setHook)(i64, std::wstring*) = NULL;
static i64 g_minSane = 0, g_maxSane = 0;

static i64 ToI64(const FILETIME& f) { return ((i64)f.dwHighDateTime << 32) | (i64)f.dwLowDateTime; }

static i64 YearStart(int year)
{
    SYSTEMTIME st;
    memset(&st, 0, sizeof(st));
    st.wYear = (WORD)year; st.wMonth = 1; st.wDay = 1;
    FILETIME f;
    SystemTimeToFileTime(&st, &f);
    return ToI64(f);
}

void Init()
{
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    QueryPerformanceFrequency(&g_freq);
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    g_precise = (PFN_PRECISE)(void*)GetProcAddress(k, "GetSystemTimePreciseAsFileTime");
    g_minSane = YearStart(2024);   // any server claiming an earlier date is broken
    g_maxSane = YearStart(2100);
}

void SetClockHooks(i64 (*now)(), bool (*set)(i64, std::wstring*))
{
    g_nowHook = now;
    g_setHook = set;
}

static i64 QpcNow()
{
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

static i64 QpcToTicks(i64 dq) { return dq * SEC / g_freq.QuadPart; }

// Local UTC time plus the QPC reading taken at the same instant.  Windows 7 has no precise
// clock, and its system clock advances in ~15.6 ms steps, so wait for a step edge there.
static i64 ClockNow(i64* qpc)
{
    if (g_nowHook) { *qpc = QpcNow(); return g_nowHook(); }
    FILETIME f;
    if (g_precise) {
        *qpc = QpcNow();
        g_precise(&f);
        return ToI64(f);
    }
    FILETIME a;
    GetSystemTimeAsFileTime(&a);
    i64 start = QpcNow();
    for (;;) {
        GetSystemTimeAsFileTime(&f);
        *qpc = QpcNow();
        if (f.dwLowDateTime != a.dwLowDateTime || f.dwHighDateTime != a.dwHighDateTime) break;
        if (*qpc - start > g_freq.QuadPart / 20) break;
    }
    return ToI64(f);
}

i64 NowFileTime()
{
    i64 q;
    return ClockNow(&q);
}

i64 NtpToFileTime(uint32_t sec, uint32_t frac)
{
    // Seconds are only 32 bits (era 0 ends in 2036).  MSB set -> 1968..2036, clear -> 2036..2104.
    i64 s = (i64)sec;
    if (!(sec & 0x80000000u)) s += 4294967296LL;
    i64 ticks = (i64)(((uint64_t)frac * 10000000ULL) >> 32);
    return (s + NTP_TO_FILETIME_SECS) * SEC + ticks;
}

void FileTimeToNtp(i64 ft, uint32_t* sec, uint32_t* frac)
{
    i64 s = ft / SEC - NTP_TO_FILETIME_SECS;
    i64 rem = ft % SEC;
    *sec = (uint32_t)(s & 0xFFFFFFFFLL);
    *frac = (uint32_t)((((uint64_t)rem) << 32) / 10000000ULL);
}

static uint32_t Rd32(const unsigned char* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void Wr32(unsigned char* p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

static uint64_t MakeNonce()
{
    static volatile LONG ctr = 0;
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    FILETIME f;
    GetSystemTimeAsFileTime(&f);
    uint64_t x = (uint64_t)q.QuadPart ^ ((uint64_t)f.dwLowDateTime << 32) ^ ((uint64_t)GetCurrentThreadId() << 20);
    x += (uint64_t)InterlockedIncrement(&ctr) * 0x9E3779B97F4A7C15ULL;
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27; x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return x ? x : 1;
}

static bool SplitHostPort(const std::wstring& in, std::wstring* host, std::wstring* port)
{
    std::wstring s = Trim(in);
    *port = L"123";
    if (s.empty()) return false;
    if (s[0] == L'[') {
        size_t e = s.find(L']');
        if (e == std::wstring::npos) return false;
        *host = s.substr(1, e - 1);
        if (e + 1 < s.size()) {
            if (s[e + 1] != L':') return false;
            *port = s.substr(e + 2);
        }
    } else {
        size_t first = s.find(L':');
        if (first != std::wstring::npos && s.find(L':', first + 1) == std::wstring::npos) {
            *host = s.substr(0, first);
            *port = s.substr(first + 1);
        } else {
            *host = s;             // plain name, or a bare IPv6 literal
        }
    }
    if (host->empty() || port->empty()) return false;
    for (size_t i = 0; i < port->size(); ++i)
        if ((*port)[i] < L'0' || (*port)[i] > L'9') return false;
    int p = _wtoi(port->c_str());
    return p >= 1 && p <= 65535;
}

static Status OneExchange(SOCKET s, int timeoutMs, Sample* out, std::wstring* detail)
{
    unsigned char req[48];
    memset(req, 0, sizeof(req));
    req[0] = 0x23;                                  // LI=0, VN=4, Mode=3 (client)
    uint64_t nonce = MakeNonce();
    uint32_t nSec = (uint32_t)(nonce >> 32), nFrac = (uint32_t)nonce;
    Wr32(req + 40, nSec);
    Wr32(req + 44, nFrac);

    i64 q0;
    i64 t1 = ClockNow(&q0);
    if (send(s, (const char*)req, 48, 0) != 48) {
        *detail = Tf(L"ntp.sendFail", WSAGetLastError(), WSAGetLastError());
        return ST_NETERR;
    }

    bool sawForeign = false;
    for (;;) {
        i64 waited = QpcToTicks(QpcNow() - q0) / MS;
        i64 left = timeoutMs - waited;
        if (left <= 0) {
            if (sawForeign) { *detail = Tc(L"ntp.replyMismatch"); return ST_BADRESP; }
            return ST_TIMEOUT;
        }
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(s, &rd);
        timeval tv;
        tv.tv_sec = (long)(left / 1000);
        tv.tv_usec = (long)((left % 1000) * 1000);
        int sr = select(0, &rd, NULL, NULL, &tv);
        i64 q1 = QpcNow();
        if (sr == 0) continue;                       // re-evaluate the deadline
        if (sr < 0) { *detail = Tf(L"ntp.selectFail", WSAGetLastError(), WSAGetLastError()); return ST_NETERR; }

        unsigned char buf[512];
        int n = recv(s, (char*)buf, sizeof(buf), 0);
        if (n < 0) {
            int e = WSAGetLastError();
            *detail = (e == WSAECONNRESET) ? Tc(L"ntp.portUnreachable") : Tf(L"ntp.recvFail", e);
            return ST_NETERR;
        }
        if (n < 48) { *detail = Tf(L"ntp.replyShort", n); return ST_BADRESP; }

        int li = buf[0] >> 6, mode = buf[0] & 7, stratum = buf[1];
        if (Rd32(buf + 24) != nSec || Rd32(buf + 28) != nFrac) { sawForeign = true; continue; }
        if (mode != 4 && mode != 5) { *detail = Tf(L"ntp.replyMode", mode); return ST_BADRESP; }
        if (stratum == 0) {
            char ref[5] = { (char)buf[12], (char)buf[13], (char)buf[14], (char)buf[15], 0 };
            *detail = std::wstring(Tc(L"ntp.kodReason")) + Utf8ToW(ref);
            return ST_KOD;
        }
        if (li == 3 || stratum > 15) { *detail = Tc(L"ntp.kodUnsync"); return ST_UNSYNC; }
        if ((Rd32(buf + 32) | Rd32(buf + 36)) == 0 || (Rd32(buf + 40) | Rd32(buf + 44)) == 0) {
            *detail = Tc(L"ntp.replyNoStamp");
            return ST_BADRESP;
        }
        i64 t2 = NtpToFileTime(Rd32(buf + 32), Rd32(buf + 36));
        i64 t3 = NtpToFileTime(Rd32(buf + 40), Rd32(buf + 44));
        if (t3 < g_minSane || t3 > g_maxSane) { *detail = Tc(L"ntp.replyDate"); return ST_INSANE; }

        i64 t4 = t1 + QpcToTicks(q1 - q0);
        i64 delay = (t4 - t1) - (t3 - t2);
        if (delay < 0) delay = 0;
        out->offset = ((t2 - t1) + (t3 - t4)) / 2;
        out->delay = delay;
        out->stratum = stratum;
        out->serverTime = t3;
        return ST_OK;
    }
}

Result Query(const std::wstring& hostPort, int samples, int timeoutMs, int gapMs)
{
    Result r;
    std::wstring host, port;
    if (!SplitHostPort(hostPort, &host, &port)) {
        r.status = ST_DNS;
        r.detail = Tc(L"ntp.badAddress");
        return r;
    }
    ADDRINFOW hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    ADDRINFOW* res = NULL;
    if (GetAddrInfoW(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
        r.status = ST_DNS;
        return r;
    }
    ADDRINFOW* pick = NULL;
    for (ADDRINFOW* p = res; p; p = p->ai_next)
        if (p->ai_family == AF_INET) { pick = p; break; }
    if (!pick)
        for (ADDRINFOW* p = res; p; p = p->ai_next)
            if (p->ai_family == AF_INET6) { pick = p; break; }
    if (!pick) { FreeAddrInfoW(res); r.status = ST_DNS; return r; }

    wchar_t ip[80] = L"";
    GetNameInfoW(pick->ai_addr, (socklen_t)pick->ai_addrlen, ip, 80, NULL, 0, NI_NUMERICHOST);
    r.ip = ip;

    SOCKET s = socket(pick->ai_family, SOCK_DGRAM, IPPROTO_UDP);
    bool connected = (s != INVALID_SOCKET) && connect(s, pick->ai_addr, (int)pick->ai_addrlen) == 0;
    FreeAddrInfoW(res);
    if (!connected) {
        r.status = ST_NETERR;
        r.detail = Tf(L"ntp.socketFail", WSAGetLastError(), WSAGetLastError());
        if (s != INVALID_SOCKET) closesocket(s);
        return r;
    }

    Status lastBad = ST_TIMEOUT;
    int fails = 0;
    for (int i = 0; i < samples; ++i) {
        if (i > 0 && gapMs > 0) Sleep(gapMs);
        Sample sm;
        std::wstring detail;
        Status st = OneExchange(s, timeoutMs, &sm, &detail);
        ++r.sent;
        if (st == ST_OK) {
            ++r.got;
            fails = 0;
            if (r.got == 1 || sm.delay < r.best.delay) r.best = sm;
        } else {
            ++fails;
            if (st != ST_TIMEOUT) { lastBad = st; r.detail = detail; }
            else if (lastBad == ST_TIMEOUT && r.detail.empty()) lastBad = ST_TIMEOUT;
            if (r.got == 0 && (st != ST_TIMEOUT || fails >= 2)) break;   // dead or refusing: stop early
        }
    }
    closesocket(s);
    r.status = r.got > 0 ? ST_OK : lastBad;
    return r;
}

static bool EnableSystemTimePrivilege()
{
    HANDLE tok;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return false;
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = false;
    if (LookupPrivilegeValueW(NULL, SE_SYSTEMTIME_NAME, &tp.Privileges[0].Luid)) {
        AdjustTokenPrivileges(tok, FALSE, &tp, sizeof(tp), NULL, NULL);
        ok = (GetLastError() == ERROR_SUCCESS);
    }
    CloseHandle(tok);
    return ok;
}

bool CanSetClock(std::wstring* why)
{
    if (g_setHook) return true;
    if (EnableSystemTimePrivilege()) return true;
    if (why) *why = Tc(L"ntp.noPermission");
    return false;
}

bool SetClockUtc(i64 target, std::wstring* err)
{
    if (g_setHook) return g_setHook(target, err);
    if (!EnableSystemTimePrivilege()) {
        if (err) *err = Tc(L"ntp.noPermissionShort");
        return false;
    }
    FILETIME f;
    f.dwLowDateTime = (DWORD)(target & 0xFFFFFFFF);
    f.dwHighDateTime = (DWORD)((target >> 32) & 0xFFFFFFFF);
    SYSTEMTIME st;
    if (!FileTimeToSystemTime(&f, &st)) {
        if (err) *err = Tc(L"ntp.targetRange");
        return false;
    }
    if (!SetSystemTime(&st)) {
        DWORD e = GetLastError();
        if (err) *err = (e == ERROR_PRIVILEGE_NOT_HELD) ? Tc(L"ntp.noPermissionShort")
                                                        : Tf(L"ntp.setSystemTime", (unsigned long)e);
        return false;
    }
    return true;
}

bool ApplyOffset(i64 offset, std::wstring* err)
{
    // Read the clock and write it back in one go so the time spent between "measured" and
    // "applied" is microseconds, not the length of a network exchange.
    i64 q;
    i64 target = ClockNow(&q) + offset;
    return SetClockUtc(target, err);
}

std::wstring StatusText(const Result& r)
{
    switch (r.status) {
    case ST_OK:      return Tc(L"ntp.ok");
    case ST_DNS:     return r.detail.empty() ? Tc(L"ntp.dns") : r.detail;
    case ST_TIMEOUT: return Tc(L"ntp.timeout");
    case ST_NETERR:  return r.detail.empty() ? Tc(L"ntp.neterr") : r.detail;
    case ST_BADRESP: return Tc(L"ntp.badresp");
    case ST_KOD:     return Tc(L"ntp.kod");
    case ST_UNSYNC:  return Tc(L"ntp.unsync");
    case ST_INSANE:  return Tc(L"ntp.insane");
    }
    return L"";
}

std::wstring FormatOffset(i64 off)
{
    const wchar_t* sign = off < 0 ? L"-" : L"+";
    i64 a = off < 0 ? -off : off;
    if (a < SEC) return Fmt(Tc(L"ntp.offsetMs"), sign, (double)a / 10000.0);
    if (a < 60 * SEC) return Fmt(Tc(L"ntp.offsetSecs"), sign, (double)a / 10000000.0);
    i64 s = a / SEC;
    int d = (int)(s / 86400), h = (int)((s % 86400) / 3600), m = (int)((s % 3600) / 60), sec = (int)(s % 60);
    if (d > 0) return Fmt(Tc(L"ntp.offsetDays"), sign, d, h, m, sec);
    return Fmt(Tc(L"ntp.offsetHMS"), sign, h, m, sec);
}

std::wstring FormatDelay(i64 d)
{
    return Fmt(L"%d ms", (int)((d + MS / 2) / MS));
}

} // namespace ntp
