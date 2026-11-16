#include "cancel_policy.h"

CancelAction DecideOnCancel(CancelPoint where, bool opening, bool haveCache)
{
    if (opening)
        return CancelAction::FailOpen;
    if (!haveCache)
        return CancelAction::LegacyShorten;
    return where == CancelPoint::InChangePath ? CancelAction::ReturnCachedNow : CancelAction::ReturnFalseThenCache;
}

ChangePathEntry DecideChangePathEntry(bool reestablishPending, bool haveCache)
{
    return reestablishPending && haveCache ? ChangePathEntry::ServeCachedPath : ChangePathEntry::Normal;
}
