// "The target already exists" decisions of a transfer, automatic renaming and safe-upload names.
// Pure functions: no threads, no network, no Sally.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "op_types.h"

// What is known about one side of a transfer (from Stat or a directory listing).
struct CTransferSide
{
    bool exists = false;
    bool isDir = false;
    uint64_t size = 0;
    int64_t mtime = 0; // unix seconds
};

enum class ConflictAction
{
    Transfer, // copy the file, starting at resumeOffset (0 = from the beginning, overwriting)
    Skip,     // leave the target alone, the item counts as done
    Park,     // ask the user (item goes to UserInputNeeded with `problem`)
    Rename    // choose a free name (AutoRenameLocal/AutoRenameRemote) and transfer there
};

struct CConflictDecision
{
    ConflictAction action = ConflictAction::Transfer;
    uint64_t resumeOffset = 0;
    Problem problem = Problem::None;
};

// Rules, in this order:
//  1. target missing                       -> Transfer(0).
//  2. both are directories                 -> Transfer(0): the directory is merged, nothing is overwritten.
//  3. file vs directory (either way)       -> Park(TargetExists) for every policy and for forced Overwrite/Resume;
//                                             a forced Skip or Rename still works, so the user can always get out.
//  4. forceAction (the user's solve choice, Retry = none) wins over the policy:
//       Overwrite -> Transfer(0); Skip -> Skip; Rename -> Rename; Resume -> the resume rule.
//  5. otherwise the policy:
//       Ask -> Park(TargetExists); Overwrite -> Transfer(0); Skip -> Skip; Rename -> Rename;
//       Resume -> the resume rule; OverwriteIfOlder -> Transfer(0) when source mtime > target mtime, else Skip.
// The resume rule (target size t, source size s):
//       t == 0        -> Transfer(0) (nothing to continue);
//       0 < t < s     -> Transfer(t);
//       t == s        -> Skip (the file is already complete);
//       t > s         -> Park(TargetExists): the target is not a partial copy of the source, so do not guess.
CConflictDecision DecideConflict(ConflictPolicy policy, SolveAction forceAction, const CTransferSide& source,
                                 const CTransferSide& target);

// "name.ext" -> "name (2).ext", "name (3).ext", ... until `exists` says the name is free. The extension is the part
// after the last dot, so "a.tar.gz" -> "a.tar (2).gz". A leading dot is not an extension: ".bashrc" -> ".bashrc (2)".
// Returns an empty string after 9999 tries.
std::wstring AutoRenameLocal(const std::wstring& name, std::function<bool(const std::wstring&)> exists);
// Same for raw remote names (bytes; the " (n)" suffix is ASCII, so any encoding stays intact).
std::string AutoRenameRemote(const std::string& rawName, std::function<bool(const std::string&)> exists);

// The temporary name an upload is written to before it is renamed over the final name: "name" -> "name.filepart".
std::string SafeUploadName(const std::string& rawName);
// True for a name SafeUploadName could have produced (ends with ".filepart" and has something before it).
bool IsSafeUploadName(const std::string& rawName);
