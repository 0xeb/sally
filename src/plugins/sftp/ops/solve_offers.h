// Which answers make sense for a parked or failed item (the solve-error dialog enables only these buttons).
// Pure decision, no Sally and no threads, so it has unit tests.
#pragma once

#include "op_queue.h"
#include "op_types.h"

struct CSolveOffers
{
    bool retry = false;
    bool skip = false;
    bool overwrite = false;
    bool resume = false;
    bool rename = false;
    bool applyToAll = false; // "apply to all items with this problem" is meaningful (never for Rename: one name, many items)
};

// 'problem' and 'type' come from the item. Skip is always offered, so the user can always get out.
CSolveOffers SolveOffersFor(Problem problem, ItemType type);
inline CSolveOffers SolveOffersFor(const CQueueItem& item)
{
    return SolveOffersFor(item.problem, item.type);
}

// True when 'action' is one of the offered answers.
bool IsOffered(const CSolveOffers& offers, SolveAction action);
