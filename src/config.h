#pragma once
#include <string>
#include <vector>

struct Server {
    std::wstring name;
    std::wstring addr;   // host, host:port, IPv4, [IPv6]:port
};

struct Settings {
    Settings() : syncOnStart(true), timedSync(false), intervalMin(60), thresholdMs(1000) {}
    bool syncOnStart;        // sync automatically every time the program starts
    bool timedSync;          // periodic sync while the program is running
    int intervalMin;         // period in minutes
    int thresholdMs;         // automatic sync leaves the clock alone when off by less than this
    std::wstring autoStartPath;   // exe path the boot task was registered with
};

struct Config {
    Settings st;
    std::vector<Server> servers;
    std::wstring path;
};

std::vector<Server> DefaultServers();

bool ConfigLoad(Config* c);          // fills defaults when nothing is stored
bool ConfigSave(const Config& c);
std::wstring ConfigPath();

// Returns an empty string when acceptable, otherwise the reason.
std::wstring ValidateAddress(const std::wstring& addr);
