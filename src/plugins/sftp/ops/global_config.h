// Plugin-wide settings (the Configuration dialog). Sally-free model: the registry code is in fs/site_config.cpp, the
// dialog in ui/config_dialog.cpp. The settings that also exist per site (workers, timeout) are only the defaults of a
// NEW site; saved sites keep their own values.
#pragma once

#include <mutex>

#include "site.h"

enum class SessionLogLevel
{
    Off = 0,
    Errors = 1,
    Normal = 2,
    Verbose = 3
};

// Ranges (the dialog validates against the same numbers; Clamp() forces them on whatever was loaded).
const int MinDefaultWorkers = 1;
const int MaxDefaultWorkers = 8;
const int MinDefaultTimeout = 1;
const int MaxDefaultTimeout = 3600;
const int MinKeepaliveSeconds = 10; // 0 = off, otherwise MinKeepaliveSeconds..MaxKeepaliveSeconds
const int MaxKeepaliveSeconds = 3600;
const int MinCacheAgeSeconds = 1;
const int MaxCacheAgeSeconds = 86400;
const int MaxDetachedIdleMinutes = 1440; // 0 = never
const int MinLogKiB = 16;
const int MaxLogKiB = 16384;
const int MaxClosedLogsKept = 100;
const int MinWorkersTotal = 1;
const int MaxWorkersTotal = 32;

struct CGlobalConfig
{
    int defaultWorkers = 2;           // new sites
    int defaultTimeoutSeconds = 30;   // new sites
    int keepaliveSeconds = 60;        // 0 = off
    bool listingCacheEnabled = true;
    int listingCacheMaxAgeSeconds = 300;
    int detachedIdleMinutes = 30;     // a detached connection is closed after this idle time; 0 = never
    SessionLogLevel logLevel = SessionLogLevel::Normal;
    int logMaxKiB = 512;              // per connection log
    int closedLogsKept = 10;          // logs of closed connections that stay readable
    int maxWorkersTotal = 6;          // transfer connections of all operations together

    // Forces every value into its range (a keepalive of 1..9 becomes 10, 0 stays "off").
    void Clamp();
};

// A new site (New button, quick connect) with the global defaults.
CSite MakeNewSite(const CGlobalConfig& config);

// The one global configuration. Read by the panel thread, the workers and the main thread, written by the main thread
// (Load, the Configuration dialog), so every access goes through a lock. C#: a lock around a field, handing out copies.
class CGlobalConfigStore
{
public:
    CGlobalConfig Get() const; // snapshot: later changes do not affect the copy
    void Set(const CGlobalConfig& config); // clamps

private:
    mutable std::mutex lock_;
    CGlobalConfig config_;
};

// The plugin's instance (a function-local static: no start-up order problems).
CGlobalConfigStore& GlobalConfigStore();
