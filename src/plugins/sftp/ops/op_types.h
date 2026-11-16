// Small shared types of the operations layer (plain enums, no Sally, no libssh2).
#pragma once

// What to do when the target of a transfer already exists (the "file exists" policy of an operation).
enum class ConflictPolicy
{
    Ask, // park the item (UserInputNeeded, Problem::TargetExists) and let the user decide
    Overwrite,
    Resume, // continue after the size the target already has
    Skip,
    OverwriteIfOlder,
    Rename // pick a new free name automatically
};

// Why an item is in an error state. Shown to the user and used by "apply to all items with this problem".
enum class Problem
{
    None,
    TargetExists,     // the target exists and the policy is Ask
    InvalidLocalName, // the remote name cannot be a Windows file name
    InvalidRemoteName, // the local name cannot be written in the site's name encoding
    TransferFailed,   // read/write error in the middle of a file
    ConnectionLost,   // the worker could not reconnect
    PermissionDenied,
    NotFound,         // the source vanished
    DirCreateFailed,
    DeleteFailed,
    ChmodFailed,
    PermissionsUnknown, // the server does not report the current permissions, so a partial change cannot be applied
    SymlinkLoop,
    LocalIoError,     // a local file could not be opened, read or written
    DiskFull,
    Other
};

// The user's answer to a parked item (see CQueue::Solve).
enum class SolveAction
{
    Retry, // try again with the operation's policy; also the value of "no forced action" in CQueueItem
    Skip,
    Overwrite,
    Resume,
    Rename // use the new name that goes with it
};
