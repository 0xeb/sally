// When an idle connection needs attention: a keepalive for the panel session, closing a detached FS. Pure functions
// of two time points, so tests can use made-up times instead of waiting. C#: static methods taking DateTime values.
#pragma once

#include <chrono>

using IdleClock = std::chrono::steady_clock;

// True when the connection has been quiet for at least keepaliveSeconds. 0 or less = keepalive is off.
bool KeepaliveDue(IdleClock::time_point lastTraffic, IdleClock::time_point now, int keepaliveSeconds);

// True when a detached FS has not been used for at least idleMinutes. 0 or less = never.
bool DetachedIdleExpired(IdleClock::time_point lastUsed, IdleClock::time_point now, int idleMinutes);
