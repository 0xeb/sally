// What a panel listing does when the user presses ESC while it waits (pure decisions; no Sally, no UI).
//
// The panel must stay as it was: the last listing that was loaded for the current path is shown again and the
// connection stays usable (the next real request reconnects). The cached listing is served once, right after the
// cancel, and never for any later request.
#pragma once

// Where the cancelled list request was running.
enum class CancelPoint
{
    InChangePath,     // the list inside ChangePath (refresh or navigation)
    InListCurrentPath // the list inside ListCurrentPath
};

enum class CancelAction
{
    FailOpen,           // first open (or no cached listing and nothing else to do): ChangePath returns FALSE
    ReturnCachedNow,    // ChangePath: return TRUE for the current path, ListCurrentPath serves the cached listing
    ReturnFalseThenCache, // ListCurrentPath: return FALSE; the ChangePath that follows re-establishes the cached path
    LegacyShorten,      // no cached listing: previous behaviour (ChangePath goes to the parent / ListCurrentPath
                        // reports an inaccessible path)
};

// 'opening': the first ChangePath of a new FS object. 'haveCache': a listing of the current path was loaded before.
CancelAction DecideOnCancel(CancelPoint where, bool opening, bool haveCache);

enum class ChangePathEntry
{
    Normal,         // do the path change as requested
    ServeCachedPath // the post-cancel re-establish: TRUE for the current path, no network traffic
};

// 'reestablishPending': a ListCurrentPath was cancelled and no ChangePath has come since. 'haveCache' as above.
ChangePathEntry DecideChangePathEntry(bool reestablishPending, bool haveCache);
