// Console self-test: drives the real ntp/engine code against fake NTP servers on loopback, with a
// virtual clock so the machine's real clock is never touched.
#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <vector>
#include "util.h"
#include "ntp.h"
#include "config.h"
#include "engine.h"

using ntp::i64;

static int g_pass = 0, g_fail = 0;
static void Out(const std::wstring& s) { std::string u = WToUtf8(s + L"\n"); fputs(u.c_str(), stdout); fflush(stdout); }
static void Check(bool ok, const std::wstring& what)
{
    if (ok) ++g_pass; else ++g_fail;
    Out(std::wstring(ok ? L"  PASS  " : L"  FAIL  ") + what);
}

// ---- virtual clock
static i64 g_virtOff = 0;                       // virtual = real + g_virtOff
static i64 RealNow() { FILETIME f; GetSystemTimeAsFileTime(&f); return ((i64)f.dwHighDateTime << 32) | f.dwLowDateTime; }
static i64 VNow() { return RealNow() + g_virtOff; }
static int g_setCalls = 0;
static bool g_failSet = false;
static bool VSet(i64 target, std::wstring* err)
{
    ++g_setCalls;
    if (g_failSet) { *err = L"模拟：没有权限"; return false; }
    g_virtOff = target - RealNow();
    return true;
}
static i64 Abs(i64 v) { return v < 0 ? -v : v; }
static const i64 YEAR = 365LL * 86400 * ntp::SEC;
static const i64 DAY = 86400LL * ntp::SEC;

// ---- fake server
enum Mode { M_OK, M_SILENT, M_WRONG_ORIGIN, M_SHORT, M_KOD, M_UNSYNC, M_BADMODE, M_ZERO_TS, M_OLD_DATE };
struct Fake {
    SOCKET s;
    unsigned short port;
    volatile LONG mode;
    volatile LONG delayMs;
    volatile LONG dieAfter;        // answer this many requests, then go silent (-1: never)
    volatile LONG served;
    i64 offset;                    // server clock = real + offset
    volatile LONG stop;
    HANDLE th;
    int stratum;
};

static void Wr32(unsigned char* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (unsigned char)v; }

static DWORD WINAPI FakeThread(LPVOID p)
{
    Fake* f = (Fake*)p;
    while (!f->stop) {
        fd_set rd; FD_ZERO(&rd); FD_SET(f->s, &rd);
        timeval tv = { 0, 100000 };
        if (select(0, &rd, NULL, NULL, &tv) <= 0) continue;
        unsigned char req[512];
        sockaddr_in from; int fl = sizeof(from);
        int n = recvfrom(f->s, (char*)req, sizeof(req), 0, (sockaddr*)&from, &fl);
        if (n < 48) continue;
        LONG served = InterlockedIncrement(&f->served);
        if (f->dieAfter >= 0 && served > f->dieAfter) continue;
        int mode = f->mode;
        if (mode == M_SILENT) continue;
        if (f->delayMs > 0) Sleep(f->delayMs);
        unsigned char r[48];
        memset(r, 0, sizeof(r));
        r[0] = (0 << 6) | (4 << 3) | 4;
        r[1] = (unsigned char)f->stratum;
        if (mode == M_UNSYNC) r[0] |= (3 << 6);
        if (mode == M_KOD) { r[1] = 0; memcpy(r + 12, "RATE", 4); }
        if (mode == M_BADMODE) r[0] = (0 << 6) | (4 << 3) | 3;
        memcpy(r + 24, req + 40, 8);                           // origin = client's transmit
        if (mode == M_WRONG_ORIGIN) { r[24] ^= 0x55; r[29] ^= 0xAA; }
        i64 now = RealNow() + f->offset;
        if (mode == M_OLD_DATE) now = 116444736000000000LL;     // 1970-01-01
        uint32_t sec, frac;
        ntp::FileTimeToNtp(now, &sec, &frac);
        if (mode != M_ZERO_TS) {
            Wr32(r + 32, sec); Wr32(r + 36, frac);
            Wr32(r + 40, sec); Wr32(r + 44, frac);
        }
        sendto(f->s, (const char*)r, mode == M_SHORT ? 20 : 48, 0, (sockaddr*)&from, fl);
    }
    return 0;
}

static Fake* MakeFake(i64 offset, int delayMs = 0, int mode = M_OK, int stratum = 2)
{
    Fake* f = new Fake();
    f->s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    bind(f->s, (sockaddr*)&a, sizeof(a));
    int l = sizeof(a); getsockname(f->s, (sockaddr*)&a, &l);
    f->port = ntohs(a.sin_port);
    f->mode = mode; f->delayMs = delayMs; f->dieAfter = -1; f->served = 0; f->offset = offset; f->stop = 0;
    f->stratum = stratum;
    f->th = CreateThread(NULL, 0, FakeThread, f, 0, NULL);
    return f;
}
static Server SrvOf(Fake* f, const wchar_t* name)
{
    Server s; s.name = name; s.addr = Fmt(L"127.0.0.1:%d", f->port); return s;
}
static void Kill(Fake* f) { f->stop = 1; WaitForSingleObject(f->th, 2000); closesocket(f->s); CloseHandle(f->th); delete f; }

static void ProbeCbNop(void*, size_t, const ntp::Result&) {}

static engine::Request OneReq(Fake* f, bool automatic, int thrMs)
{
    engine::Request q;
    q.servers.push_back(SrvOf(f, L"fake"));
    q.manualIndex = 0; q.automatic = automatic; q.thresholdMs = thrMs;
    return q;
}

// ------------------------------------------------------------------ tests

static void TestConversions()
{
    Out(L"[1] NTP <-> FILETIME 换算");
    i64 now = RealNow();
    uint32_t s, f;
    ntp::FileTimeToNtp(now, &s, &f);
    Check(Abs(ntp::NtpToFileTime(s, f) - now) < 2, L"当前时间往返误差 < 0.2 微秒");
    SYSTEMTIME st; memset(&st, 0, sizeof(st));
    st.wYear = 2035; st.wMonth = 6; st.wDay = 1; FILETIME ft;
    SystemTimeToFileTime(&st, &ft);
    i64 t35 = ((i64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    ntp::FileTimeToNtp(t35, &s, &f);
    Check(Abs(ntp::NtpToFileTime(s, f) - t35) < 2, L"2035 年（纪元 0 末期）往返正确");
    st.wYear = 2040;
    SystemTimeToFileTime(&st, &ft);
    i64 t40 = ((i64)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    ntp::FileTimeToNtp(t40, &s, &f);
    Check(Abs(ntp::NtpToFileTime(s, f) - t40) < 2, L"2040 年（NTP 秒数回绕后，纪元 1）往返正确");
}

static void TestBigOffsets()
{
    Out(L"[2] 系统时间严重偏离时的同步（手动指定服务器）");
    struct { const wchar_t* name; i64 off; } cases[] = {
        { L"本机快 10 年", +10 * YEAR }, { L"本机慢 10 年", -10 * YEAR }, { L"本机慢 26 年（2000 年）", -26 * YEAR },
        { L"本机快 12 年（2038 年）", +12 * YEAR }, { L"本机快 400 天", +400 * DAY }, { L"本机慢 3 天", -3 * DAY },
        { L"本机快 45 秒", +45 * ntp::SEC }, { L"本机慢 2.5 秒", -25 * ntp::SEC / 10 } };
    Fake* f = MakeFake(0);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        g_virtOff = cases[i].off;
        g_setCalls = 0;
        engine::Outcome o = engine::RunSync(OneReq(f, false, 0), ProbeCbNop, NULL);
        i64 err = Abs(VNow() - RealNow() - 0);
        Check(o.kind == engine::K_SYNCED && Abs(VNow() - RealNow()) < 100 * ntp::MS,
              Fmt(L"%ls：同步后残差 %ls，复核=%ls，写入 %d 次", cases[i].name, ntp::FormatOffset(err).c_str(),
                  o.verified ? L"是" : L"否", g_setCalls));
        Check(o.verified && Abs(o.residual) < 100 * ntp::MS, Fmt(L"%ls：复核偏差 %ls", cases[i].name, ntp::FormatOffset(o.residual).c_str()));
    }
    Kill(f);
}

static void TestThreshold()
{
    Out(L"[3] 自动同步的偏差阈值");
    Fake* f = MakeFake(0);
    g_virtOff = 300 * ntp::MS; g_setCalls = 0;
    engine::Outcome o = engine::RunSync(OneReq(f, true, 1000), ProbeCbNop, NULL);
    Check(o.kind == engine::K_SKIPPED && g_setCalls == 0 && g_virtOff == 300 * ntp::MS, L"偏差 0.3 秒 < 阈值 1000 ms：不校正，系统时间未被改动");
    g_virtOff = 3 * ntp::SEC; g_setCalls = 0;
    o = engine::RunSync(OneReq(f, true, 1000), ProbeCbNop, NULL);
    Check(o.kind == engine::K_SYNCED && Abs(g_virtOff) < 100 * ntp::MS, L"偏差 3 秒 > 阈值：自动校正");
    g_virtOff = 300 * ntp::MS; g_setCalls = 0;
    o = engine::RunSync(OneReq(f, false, 1000), ProbeCbNop, NULL);
    Check(o.kind == engine::K_SYNCED && g_setCalls >= 1, L"手动同步不受阈值限制，0.3 秒偏差也会校正");
    g_virtOff = -20 * YEAR; g_setCalls = 0;
    o = engine::RunSync(OneReq(f, true, 60000), ProbeCbNop, NULL);
    Check(o.kind == engine::K_SYNCED, L"自动同步遇到 20 年偏差（远超阈值）会校正");
    Kill(f);
}

static void TestBadServers()
{
    Out(L"[4] 异常服务器 / 异常应答");
    struct { const wchar_t* name; int mode; ntp::Status want; } cases[] = {
        { L"完全不应答", M_SILENT, ntp::ST_TIMEOUT }, { L"应答与请求不匹配（伪造/串包）", M_WRONG_ORIGIN, ntp::ST_BADRESP },
        { L"应答过短", M_SHORT, ntp::ST_BADRESP }, { L"拒绝服务（KoD RATE）", M_KOD, ntp::ST_KOD },
        { L"服务器自身未同步", M_UNSYNC, ntp::ST_UNSYNC }, { L"模式字段错误", M_BADMODE, ntp::ST_BADRESP },
        { L"缺少时间戳", M_ZERO_TS, ntp::ST_BADRESP }, { L"返回 1970 年日期", M_OLD_DATE, ntp::ST_INSANE } };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        Fake* f = MakeFake(0, 0, cases[i].mode);
        g_virtOff = 5 * YEAR; g_setCalls = 0;
        ntp::Result r = ntp::Query(SrvOf(f, L"x").addr, 2, 800, 20);
        Check(r.status == cases[i].want, Fmt(L"%ls：判定为“%ls”", cases[i].name, ntp::StatusText(r).c_str()));
        engine::Outcome o = engine::RunSync(OneReq(f, false, 0), ProbeCbNop, NULL);
        Check(o.kind == engine::K_FAILED && g_setCalls == 0 && g_virtOff == 5 * YEAR,
              Fmt(L"%ls：同步失败且系统时间未被改动（%ls）", cases[i].name, o.message.c_str()));
        Kill(f);
    }
    ntp::Result r = ntp::Query(L"no-such-host.invalid", 1, 500, 0);
    Check(r.status == ntp::ST_DNS, L"域名无法解析 -> ST_DNS");
    r = ntp::Query(L"", 1, 500, 0);
    Check(r.status == ntp::ST_DNS, L"空地址 -> 无效");
    r = ntp::Query(L"127.0.0.1:99999", 1, 500, 0);
    Check(r.status == ntp::ST_DNS, L"端口越界 -> 无效");
    r = ntp::Query(L"127.0.0.1:9", 1, 800, 0);
    Check(r.status != ntp::ST_OK, L"无人监听的端口 -> 不可用（端口不可达或超时）");

    Fake* f = MakeFake(0);
    g_virtOff = 2 * YEAR; g_failSet = true;
    engine::Outcome o = engine::RunSync(OneReq(f, false, 0), ProbeCbNop, NULL);
    Check(o.kind == engine::K_FAILED && o.message.find(L"写入系统时间失败") != std::wstring::npos, L"没有权限修改系统时间 -> 明确报错");
    g_failSet = false;
    Kill(f);
}

static void TestAutoPick()
{
    Out(L"[5] 一键自动选择延迟最低的服务器");
    Fake* a = MakeFake(0, 150);                                    // correct, slow
    Fake* b = MakeFake(0, 30);                                     // correct, fastest of the correct ones
    Fake* c = MakeFake(3600 * ntp::SEC, 0);                        // lowest delay but its clock is 1 h wrong
    Fake* d = MakeFake(0, 0, M_SILENT);                            // dead
    Fake* e = MakeFake(0, 0, M_KOD);                               // refuses
    engine::Request q;
    q.servers.push_back(SrvOf(a, L"A 慢但准"));
    q.servers.push_back(SrvOf(b, L"B 快且准"));
    q.servers.push_back(SrvOf(c, L"C 最快但时间错 1 小时"));
    q.servers.push_back(SrvOf(d, L"D 无响应"));
    q.servers.push_back(SrvOf(e, L"E 拒绝服务"));
    q.manualIndex = -1;

    std::vector<ntp::Result> rs = engine::ProbeAll(q.servers, 3, 1500, ProbeCbNop, NULL);
    Check(rs[0].status == ntp::ST_OK && rs[1].status == ntp::ST_OK && rs[2].status == ntp::ST_OK, L"A/B/C 探测为可用");
    Check(rs[3].status == ntp::ST_TIMEOUT && rs[4].status == ntp::ST_KOD, L"D 判为无响应，E 判为拒绝服务");
    Check(rs[2].best.delay < rs[1].best.delay && rs[1].best.delay < rs[0].best.delay,
          Fmt(L"测得延迟 C=%ls < B=%ls < A=%ls", ntp::FormatDelay(rs[2].best.delay).c_str(),
              ntp::FormatDelay(rs[1].best.delay).c_str(), ntp::FormatDelay(rs[0].best.delay).c_str()));
    int best = engine::PickBest(rs, q.servers);
    Check(best == 1, Fmt(L"择优结果为 B（跳过时间与多数不一致的 C），实际 = %d", best));

    g_virtOff = -7 * YEAR;
    engine::Outcome o = engine::RunSync(q, ProbeCbNop, NULL);
    Check(o.kind == engine::K_SYNCED && o.usedIndex == 1 && Abs(VNow() - RealNow()) < 100 * ntp::MS,
          Fmt(L"一键自动同步（本机慢 7 年）：使用 %ls，残差 %ls", o.serverName.c_str(), ntp::FormatOffset(VNow() - RealNow()).c_str()));

    // best server dies right after the probe phase -> falls back to the next one
    InterlockedExchange(&b->served, 0);
    b->dieAfter = 3;
    g_virtOff = 9 * YEAR;
    o = engine::RunSync(q, ProbeCbNop, NULL);
    Check(o.kind == engine::K_SYNCED && o.usedIndex == 0 && Abs(VNow() - RealNow()) < 100 * ntp::MS,
          Fmt(L"最佳服务器在探测后失联：自动改用备选 %ls", o.serverName.c_str()));
    Kill(a); Kill(b); Kill(c); Kill(d); Kill(e);

    Fake* x = MakeFake(0, 0, M_SILENT);
    Fake* y = MakeFake(0, 0, M_KOD);
    engine::Request q2;
    q2.servers.push_back(SrvOf(x, L"X")); q2.servers.push_back(SrvOf(y, L"Y")); q2.manualIndex = -1;
    g_virtOff = YEAR; g_setCalls = 0;
    o = engine::RunSync(q2, ProbeCbNop, NULL);
    Check(o.kind == engine::K_FAILED && g_setCalls == 0, L"所有服务器都不可用：失败并保持系统时间不变");
    Kill(x); Kill(y);
}

static void TestConfig()
{
    Out(L"[6] 配置读写与服务器地址校验");
    Check(ValidateAddress(L"ntp.aliyun.com").empty(), L"域名合法");
    Check(ValidateAddress(L"192.168.1.1:123").empty(), L"IP:端口合法");
    Check(!ValidateAddress(L"bad host!").empty(), L"含空格/特殊字符被拒绝");
    Check(!ValidateAddress(L"   ").empty(), L"空白被拒绝");
    Check(DefaultServers().size() >= 20, Fmt(L"内置服务器 %d 台", (int)DefaultServers().size()));
}

int wmain(int argc, wchar_t** argv)
{
    (void)argc; (void)argv;
    ntp::Init();
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    SetLogPath(std::wstring(tmp) + L"ntpsync_selftest.log");
    DeleteFileW(LogPath().c_str());
    LogInit(L"selftest");
    ntp::SetClockHooks(VNow, VSet);

    TestConversions();
    TestBigOffsets();
    TestThreshold();
    TestBadServers();
    TestAutoPick();
    TestConfig();
    Out(Fmt(L"\n@@RSLT@@ %ls  通过 %d  失败 %d", g_fail ? L"FAIL" : L"PASS", g_pass, g_fail));
    return g_fail ? 1 : 0;
}
