#include "global_config.h"
#include "site_list.h"

namespace
{
int ClampInt(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}
} // namespace

void CGlobalConfig::Clamp()
{
    defaultWorkers = ClampInt(defaultWorkers, MinDefaultWorkers, MaxDefaultWorkers);
    defaultTimeoutSeconds = ClampInt(defaultTimeoutSeconds, MinDefaultTimeout, MaxDefaultTimeout);
    keepaliveSeconds = keepaliveSeconds <= 0 ? 0 : ClampInt(keepaliveSeconds, MinKeepaliveSeconds, MaxKeepaliveSeconds);
    listingCacheMaxAgeSeconds = ClampInt(listingCacheMaxAgeSeconds, MinCacheAgeSeconds, MaxCacheAgeSeconds);
    detachedIdleMinutes = ClampInt(detachedIdleMinutes, 0, MaxDetachedIdleMinutes);
    const int level = static_cast<int>(logLevel);
    logLevel = static_cast<SessionLogLevel>(ClampInt(level, static_cast<int>(SessionLogLevel::Off),
                                                     static_cast<int>(SessionLogLevel::Verbose)));
    logMaxKiB = ClampInt(logMaxKiB, MinLogKiB, MaxLogKiB);
    closedLogsKept = ClampInt(closedLogsKept, 0, MaxClosedLogsKept);
    maxWorkersTotal = ClampInt(maxWorkersTotal, MinWorkersTotal, MaxWorkersTotal);
}

CSite MakeNewSite(const CGlobalConfig& config)
{
    CSite site;
    site.workers = ClampInt(config.defaultWorkers, 1, MaxSiteWorkers);
    site.timeoutSeconds = ClampInt(config.defaultTimeoutSeconds, 1, MaxTimeoutSeconds);
    return site;
}

CGlobalConfig CGlobalConfigStore::Get() const
{
    std::lock_guard<std::mutex> guard(lock_);
    return config_;
}

void CGlobalConfigStore::Set(const CGlobalConfig& config)
{
    CGlobalConfig clamped = config;
    clamped.Clamp();
    std::lock_guard<std::mutex> guard(lock_);
    config_ = clamped;
}

CGlobalConfigStore& GlobalConfigStore()
{
    static CGlobalConfigStore store;
    return store;
}
