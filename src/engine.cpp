#include "engine.h"
#include "util.h"
#include "i18n.h"

using i18n::T;
using i18n::Tc;
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
            Log(Tf(L"eng.onlyTwo", ntp::FormatOffset(d).c_str()));
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
            Log(Tf(L"eng.excluded", servers[ok[i]].name.c_str(),
                ntp::FormatOffset(rs[ok[i]].best.offset - median).c_str()));
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
    if (req.servers.empty()) { o.message = Tc(L"eng.emptyList"); return o; }

    std::vector<int> order;                // candidates, best first
    if (req.manualIndex >= 0) {
        if ((size_t)req.manualIndex >= req.servers.size()) { o.message = Tc(L"eng.noSelection"); return o; }
        order.push_back(req.manualIndex);
    } else {
        Log(Tf(L"eng.probeProgress", (int)req.servers.size()));
        std::vector<ntp::Result> rs = ProbeAll(req.servers, req.probeSamples, 2000, cb, ctx);
        int okc = 0;
        for (size_t i = 0; i < rs.size(); ++i) if (rs[i].status == ntp::ST_OK) ++okc;
        std::vector<int> trusted = RankTrusted(rs, req.servers, true);
        if (trusted.empty()) {
            o.message = Tf(L"eng.allUnreachable", (int)rs.size());
            return o;
        }
        int best = trusted[0];
        Log(Tf(L"eng.chosen", okc, (int)rs.size(), req.servers[best].name.c_str(), ntp::FormatDelay(rs[best].best.delay).c_str()));
        for (size_t i = 0; i < trusted.size() && order.size() < 3; ++i) order.push_back(trusted[i]);
    }

    std::wstring lastErr;
    for (size_t k = 0; k < order.size(); ++k) {
        const Server& sv = req.servers[order[k]];
        ntp::Result fresh = ntp::Query(sv.addr, 5, 3000, 80);
        if (fresh.status != ntp::ST_OK) {
            lastErr = sv.name + L"：" + ntp::StatusText(fresh);
            Log(Tf(L"eng.fetchFailed", lastErr.c_str()));
            continue;
        }
        i64 off = fresh.best.offset;
        o.serverName = sv.name; o.serverAddr = sv.addr; o.usedIndex = order[k]; o.offset = off;
        Log(Tf(L"eng.source", sv.name.c_str(), fresh.ip.c_str(), fresh.best.stratum,
            ntp::FormatDelay(fresh.best.delay).c_str(), ntp::FormatOffset(off).c_str()));

        if (req.automatic && Abs(off) < (i64)req.thresholdMs * ntp::MS) {
            o.kind = K_SKIPPED;
            o.message = Tf(L"eng.belowThreshold", ntp::FormatOffset(off).c_str(), req.thresholdMs);
            return o;
        }

        std::wstring err;
        if (!ntp::ApplyOffset(off, &err)) {
            o.kind = K_FAILED;
            o.message = Tf(L"eng.setFailed", err.c_str());
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
            Log(Tf(L"eng.residualAgain", ntp::FormatOffset(o.residual).c_str()));
            if (!ntp::ApplyOffset(o.residual, &err)) { o.message = Tf(L"eng.resyncFailed", err.c_str()); o.kind = K_FAILED; return o; }
        }
        o.message = std::wstring(Tc(L"eng.corrected")) + ntp::FormatOffset(off);
        if (o.verified) o.message += std::wstring(Tc(L"eng.residualAfter")) + ntp::FormatOffset(o.residual);
        else o.message += Tc(L"eng.notVerified");
        return o;
    }
    o.kind = K_FAILED;
    o.message = lastErr.empty() ? Tf(L"eng.failPlain") : Tf(L"eng.failWithDetail", lastErr.c_str());
    return o;
}

} // namespace engine
