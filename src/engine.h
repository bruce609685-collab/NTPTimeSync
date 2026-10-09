#pragma once
#include "ntp.h"
#include "config.h"
#include <vector>

namespace engine {

// Called from worker threads, once per finished server.
typedef void (*ProbeCb)(void* ctx, size_t index, const ntp::Result& r);

// Probes every address concurrently (bounded pool) and returns results in the input order.
std::vector<ntp::Result> ProbeAll(const std::vector<Server>& servers, int samples, int timeoutMs,
                                  ProbeCb cb, void* ctx);

// Index of the server to trust, or -1.  Lowest delay wins, unless its time disagrees with what
// the majority of the responding servers say (a wrong server would otherwise "win" by being close).
// Available servers ordered by delay, minus those whose time disagrees with the majority.
std::vector<int> RankTrusted(const std::vector<ntp::Result>& rs, const std::vector<Server>& servers, bool verbose);

int PickBest(const std::vector<ntp::Result>& rs, const std::vector<Server>& servers);

struct Request {
    Request() : manualIndex(-1), automatic(false), thresholdMs(0), probeSamples(3) {}
    std::vector<Server> servers;
    int manualIndex;      // >= 0: use only this server;  -1: probe all and choose
    bool automatic;       // true: leave the clock alone when the deviation is below thresholdMs
    int thresholdMs;
    int probeSamples;
};

enum Kind { K_SYNCED, K_SKIPPED, K_FAILED };

struct Outcome {
    Outcome() : kind(K_FAILED), usedIndex(-1), offset(0), residual(0), verified(false) {}
    Kind kind;
    int usedIndex;
    std::wstring serverName, serverAddr;
    ntp::i64 offset;      // deviation found before correcting (server - local)
    ntp::i64 residual;    // deviation measured after correcting
    bool verified;
    std::wstring message;
};

// Blocking; runs on a worker thread.  cb (optional) reports probe results as they arrive.
Outcome RunSync(const Request& req, ProbeCb cb, void* ctx);

} // namespace engine
