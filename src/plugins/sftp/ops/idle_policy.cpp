#include "idle_policy.h"

namespace
{
bool IdleFor(IdleClock::time_point last, IdleClock::time_point now, std::chrono::seconds limit)
{
    return limit.count() > 0 && now - last >= limit;
}
} // namespace

bool KeepaliveDue(IdleClock::time_point lastTraffic, IdleClock::time_point now, int keepaliveSeconds)
{
    return IdleFor(lastTraffic, now, std::chrono::seconds(keepaliveSeconds));
}

bool DetachedIdleExpired(IdleClock::time_point lastUsed, IdleClock::time_point now, int idleMinutes)
{
    return IdleFor(lastUsed, now, std::chrono::minutes(idleMinutes));
}
