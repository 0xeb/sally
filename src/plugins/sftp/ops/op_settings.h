// What one operation (a copy or move run) is asked to do. Plain data, shared by COperation and its workers.
#pragma once

#include <string>
#include <vector>

#include "file_mode.h"
#include "op_types.h"
#include "site.h"

enum class TransferDirection
{
    Download, // remote -> local
    Upload,   // local -> remote
    Delete,   // delete on the server (no local side)
    Chmod     // change the permissions on the server (no local side)
};

struct COperationSettings
{
    TransferDirection direction = TransferDirection::Download;
    bool move = false;                           // delete the source after a successful transfer
    ConflictPolicy policy = ConflictPolicy::Ask; // what to do when the target exists
    bool preserveTime = true;                    // the target gets the source modification time
    bool safeUpload = true;                      // upload to "<name>.filepart", rename when complete
    bool setUploadPermissions = false;           // chmod uploaded files to uploadPermissions
    unsigned uploadPermissions = 0644;
    int workers = 2;                             // parallel connections, 1..8
    std::wstring localRoot;                      // where a download puts the selection / where an upload reads from
    std::string remoteRoot;                      // raw bytes: where an upload puts the selection
    // (the source paths themselves are the root items of the queue)

    // Chmod (direction Chmod): the change applied to each item's mode. With 'chmodRecursive' a selected directory is
    // explored; then files get the change only when chmodFiles, directories (the selected one and those below) only when
    // chmodDirs. Without it the selected items themselves all get the change.
    CModeChange modeChange;
    bool chmodRecursive = false;
    bool chmodFiles = true;
    bool chmodDirs = true;

    // A worker that lost its connection waits these many milliseconds before each new attempt (so the list length is the
    // number of reconnect attempts), then parks the item. Tests use short delays.
    std::vector<unsigned> reconnectDelaysMs = {1000, 3000, 10000};
    // true: a host key or prompt the worker cannot answer itself is published to the observer (the window asks the
    // user). false (or no observer): such a login is rejected.
    bool interactive = true;
};

// The transfer options of a site (workers, safe upload, preserve time, upload permissions) copied into new settings.
COperationSettings SettingsFromSite(const CSite& site, TransferDirection direction, bool move);
