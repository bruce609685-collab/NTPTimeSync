#pragma once
#include <winsock2.h>
#include <windows.h>
#include <stdint.h>
#include <string>

// All times are FILETIME units (100 ns ticks since 1601-01-01 UTC) held in int64.
namespace ntp {

typedef int64_t i64;
static const i64 MS = 10000LL;
static const i64 SEC = 10000000LL;

enum Status {
    ST_OK = 0,
    ST_DNS,       // name did not resolve
    ST_TIMEOUT,   // no reply
    ST_NETERR,    // socket error / port unreachable
    ST_BADRESP,   // reply failed validation (length, mode, origin timestamp)
    ST_KOD,       // stratum 0 "kiss of death"
    ST_UNSYNC,    // server itself is not synchronised
    ST_INSANE     // server time is outside any believable range
};

struct Sample {
    i64 offset;      // amount to ADD to the local clock (server - local)
    i64 delay;       // round-trip network delay
    int stratum;
    i64 serverTime;  // server transmit time
};

struct Result {
    Result() : status(ST_TIMEOUT), sent(0), got(0)
    {
        best.offset = 0; best.delay = 0; best.stratum = 0; best.serverTime = 0;
    }
    Status status;
    std::wstring ip;
    std::wstring detail;
    int sent, got;
    Sample best;     // sample with the lowest delay; valid when status == ST_OK
};

void Init();                       // Winsock + clock helpers; call once, before threads start
i64 NowFileTime();                 // local UTC clock (hookable for tests)

i64 NtpToFileTime(uint32_t sec, uint32_t frac);
void FileTimeToNtp(i64 ft, uint32_t* sec, uint32_t* frac);

// "host", "host:port", "1.2.3.4", "[::1]:123". Sends `samples` requests and keeps the lowest-delay one.
Result Query(const std::wstring& hostPort, int samples, int timeoutMs, int gapMs);

bool CanSetClock(std::wstring* why);
bool SetClockUtc(i64 targetFileTime, std::wstring* err);
bool ApplyOffset(i64 offset, std::wstring* err);  // now + offset, read and applied back to back
void SetClockHooks(i64 (*now)(), bool (*set)(i64, std::wstring*));

std::wstring StatusText(const Result& r);
std::wstring FormatOffset(i64 off);   // "+12.3 ms", "-1.234 s", "+3天 02:10:05"
std::wstring FormatDelay(i64 d);      // "61 ms"

} // namespace ntp
