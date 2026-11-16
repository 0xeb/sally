#include "solve_offers.h"

CSolveOffers SolveOffersFor(Problem problem, ItemType type)
{
    CSolveOffers offers;
    offers.skip = true;
    offers.applyToAll = true;
    const bool transfer = IsTransferType(type);

    switch (problem)
    {
    case Problem::TargetExists:
        offers.retry = true;
        offers.overwrite = transfer;
        offers.resume = transfer;
        offers.rename = transfer;
        break;

    case Problem::InvalidLocalName:
    case Problem::InvalidRemoteName:
        // the same name fails again: a new name or skip (retry stays for the case the user fixed the encoding setting)
        offers.retry = true;
        offers.rename = transfer || IsExploreType(type); // a directory that cannot be named can get a name too
        break;

    case Problem::TransferFailed:
    case Problem::ConnectionLost:
        offers.retry = true;
        offers.resume = transfer; // the partial target is kept
        break;

    case Problem::SymlinkLoop:
        break; // only skip: retrying walks the same loop

    default: // permission, not found, mkdir/delete failures, disk, local I/O, other
        offers.retry = true;
        break;
    }
    return offers;
}

bool IsOffered(const CSolveOffers& offers, SolveAction action)
{
    switch (action)
    {
    case SolveAction::Retry:
        return offers.retry;
    case SolveAction::Skip:
        return offers.skip;
    case SolveAction::Overwrite:
        return offers.overwrite;
    case SolveAction::Resume:
        return offers.resume;
    case SolveAction::Rename:
        return offers.rename;
    }
    return false;
}
