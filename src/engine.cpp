#include "engine.h"
#include "util.h"
#include <algorithm>

namespace engine {

using ntp::i64;

static i64 Abs(i64 v) { return v < 0 ? -v : v; }

struct PoolCtx {
    const std::vector<Server>* servers;
    std::vector<ntp::Result>* out;
    int samples, timeoutMs;
    ProbeCb cb;
    void* cbCtx;
    volatile LONG next;
};

static DWORD WINAPI PoolThread(LPVOID p)
{
    PoolCtx* c = (PoolCtx*)p;
    for (;;) {
        LONG i = InterlockedIncrement(&c->next) - 1;
        if (i >= (LONG)c->servers->size()) break;
        (*c->out)[i] = ntp::Query((*c->servers)[i].addr, c->samples, c->timeoutMs, 100);
        if (c->cb) c->cb(c->cbCtx, (size_t)i, (*c->out)[i]);
    }
    return 0;
}

std::vector<ntp::Result> ProbeAll(const std::vector<Server>& servers, int samples, int timeoutMs,
                                  ProbeCb cb, void* ctx)
{
    std::vector<ntp::Result> out(servers.size());
    if (servers.empty()) return out;
    PoolCtx c;
    c.servers = &servers; c.out = &out; c.samples = samples; c.timeoutMs = timeoutMs;
    c.cb = cb; c.cbCtx = ctx; c.next = 0;
    size_t n = servers.size() < 16 ? servers.size() : 16;
    HANDLE th[16];
    size_t started = 0;
    for (size_t i = 0; i < n; ++i) {
        HANDLE h = CreateThread(NULL, 256 * 1024, PoolThread, &c, 0, NULL);
        if (h) th[started++] = h;
    }
    if (started == 0) PoolThread(&c);          // could not create threads: do it inline
    else {
        WaitForMultipleObjects((DWORD)started, th, TRUE, INFINITE);
        for (size_t i = 0; i < started; ++i) CloseHandle(th[i]);
    }
    return out;
}

std::vector<int> RankTrusted(const std::vector<ntp::Result>& rs, const std::vector<Server>& servers, bool verbose)
{
    std::vector<int> ok;
    for (size_t i = 0; i < rs.size(); ++i)
        if (rs[i].status == ntp::ST_OK) ok.push_back((int)i);
    std::sort(ok.begin(), ok.end(), [&](int a, int b) {
        if (rs[a].best.delay != rs[b].best.delay) return rs[a].best.delay < rs[b].best.delay;
        return a < b;
    });
    if (ok.size() > 7) ok.resize(7);
    if (ok.size() <= 1) return ok;

    if (ok.size() == 2) {
        i64 d = Abs(rs[ok[0]].best.offset - rs[ok[1]].best.offset);
        if (verbose && d > 2 * ntp::SEC)
            Log(L"警告：仅有两台服务器应答，且它们的时间相差 " + ntp::FormatOffset(d) + L"，无法判断哪台正确，将采用延迟较低者");
        return ok;
    }

    std::vector<i64> offs;
    for (size_t i = 0; i < ok.size(); ++i) offs.push_back(rs[ok[i]].best.offset);
    std::sort(offs.begin(), offs.end());
    i64 median = offs[offs.size() / 2];
    const i64 tol = 500 * ntp::MS;

    std::vector<int> good;
    for (size_t i = 0; i < ok.size(); ++i) {
        if (Abs(rs[ok[i]].best.offset - median) <= tol) good.push_back(ok[i]);
        else if (verbose)
            Log(L"排除时间与多数服务器不一致的 " + servers[ok[i]].name + L"（相差 " +
                ntp::FormatOffset(rs[ok[i]].best.offset - median) + L"）");
    }
    return good;
}

int PickBest(const std::vector<ntp::Result>& rs, const std::vector<Server>& servers)
{
    std::vector<int> t = RankTrusted(rs, servers, false);
    return t.empty() ? -1 : t[0];
}

Outcome RunSync(const Request& req, ProbeCb cb, void* ctx)
{
    Outcome o;
    if (req.servers.empty()) { o.message = L"服务器列表为空"; return o; }

    std::vector<int> order;                // candidates, best first
    if (req.manualIndex >= 0) {
        if ((size_t)req.manualIndex >= req.servers.size()) { o.message = L"未选择服务器"; return o; }
        order.push_back(req.manualIndex);
    } else {
        Log(Fmt(L"正在检测 %d 台服务器…", (int)req.servers.size()));
        std::vector<ntp::Result> rs = ProbeAll(req.servers, req.probeSamples, 2000, cb, ctx);
        int okc = 0;
        for (size_t i = 0; i < rs.size(); ++i) if (rs[i].status == ntp::ST_OK) ++okc;
        std::vector<int> trusted = RankTrusted(rs, req.servers, true);
        if (trusted.empty()) {
            o.message = Fmt(L"所有 %d 台服务器均不可用，请检查网络连接或防火墙（UDP 123 端口）", (int)rs.size());
            return o;
        }
        int best = trusted[0];
        Log(Fmt(L"可用 %d/%d 台；选定 %ls（延迟 %ls）", okc, (int)rs.size(), req.servers[best].name.c_str(),
                ntp::FormatDelay(rs[best].best.delay).c_str()));
        for (size_t i = 0; i < trusted.size() && order.size() < 3; ++i) order.push_back(trusted[i]);
    }

    std::wstring lastErr;
    for (size_t k = 0; k < order.size(); ++k) {
        const Server& sv = req.servers[order[k]];
        ntp::Result fresh = ntp::Query(sv.addr, 5, 3000, 80);
        if (fresh.status != ntp::ST_OK) {
            lastErr = sv.name + L"：" + ntp::StatusText(fresh);
            Log(L"取时失败 - " + lastErr);
            continue;
        }
        i64 off = fresh.best.offset;
        o.serverName = sv.name; o.serverAddr = sv.addr; o.usedIndex = order[k]; o.offset = off;
        Log(L"时间源 " + sv.name + L"（" + fresh.ip + L"，层级 " + Fmt(L"%d", fresh.best.stratum) +
            L"，延迟 " + ntp::FormatDelay(fresh.best.delay) + L"），本机偏差 " + ntp::FormatOffset(off));

        if (req.automatic && Abs(off) < (i64)req.thresholdMs * ntp::MS) {
            o.kind = K_SKIPPED;
            o.message = L"偏差 " + ntp::FormatOffset(off) + Fmt(L" 小于阈值 %d ms，无需校正", req.thresholdMs);
            return o;
        }

        std::wstring err;
        if (!ntp::ApplyOffset(off, &err)) {
            o.kind = K_FAILED;
            o.message = L"写入系统时间失败：" + err;
            return o;
        }

        // Re-measure: a clock that was years off should now read ~0, and a bad write is caught here.
        o.kind = K_SYNCED;
        for (int pass = 0; pass < 3; ++pass) {
            ntp::Result chk = ntp::Query(sv.addr, 3, 2500, 60);
            if (chk.status != ntp::ST_OK) { o.verified = false; break; }
            o.verified = true;
            o.residual = chk.best.offset;
            if (Abs(o.residual) <= 200 * ntp::MS || pass == 2) break;
            Log(L"复核偏差仍有 " + ntp::FormatOffset(o.residual) + L"，再次校正");
            if (!ntp::ApplyOffset(o.residual, &err)) { o.message = L"复核后再次写入失败：" + err; o.kind = K_FAILED; return o; }
        }
        o.message = L"已校正 " + ntp::FormatOffset(off);
        if (o.verified) o.message += L"，复核剩余偏差 " + ntp::FormatOffset(o.residual);
        else o.message += L"（未能复核）";
        return o;
    }
    o.kind = K_FAILED;
    o.message = lastErr.empty() ? L"同步失败" : L"同步失败：" + lastErr;
    return o;
}

} // namespace engine
