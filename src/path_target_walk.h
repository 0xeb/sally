// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

// The existence walk behind SalParsePathW: starting from the full target path, step back one
// path component at a time until a prefix exists, then decide what the target is. The path is
// never modified; every result is an offset into it.
//
// #116: an existing archive file followed by a backslash is a path INTO that archive and must
// be returned as such. The walk used to record that and then fall through to the generic
// Windows-path result, so F5/F6 into an open archive reported "Target path for this operation
// cannot contain path to a file".

enum class PathPrefixState
{
    Missing,   // the prefix does not exist (or names an invalid path) - keep walking back
    File,      // the prefix is an existing file
    Directory, // the prefix is an existing directory
    Failed     // the probe failed with an unexpected error - report it
};

enum class PathTargetKind
{
    WindowsPath,   // an existing directory prefix (possibly the root) plus an optional new part
    FileOverwrite, // the whole target names an existing, non-archive file
    Archive,       // an existing archive file, then a path inside it
    NotArchive,    // an existing file followed by a path, but the file is not an archive
    Failed         // a prefix probe failed; see 'errorCode'
};

struct PathTargetWalk
{
    PathTargetKind kind;
    size_t boundary;            // end of the existing prefix (the archive file for Archive)
    bool isDir;                 // the existing prefix is a directory
    unsigned long errorCode;    // for Failed
};

// path            - the full target path (a trimmed mask may follow the last backslash)
// afterRoot       - offset just past the root (e.g. "C:\" or "\\server\share\")
// end             - where the walk starts: path.size(), or the backslash before a mask
// lastChar        - L'\\' when the walk starts before a mask, else 0
// backslashAtEnd  - the target as entered ended with a backslash
// mustBePath      - the target is a bare drive ("C:")
// probe(prefix, errorCode) -> PathPrefixState   (sets errorCode for Failed)
// isArchive(prefix) -> bool
template <class Probe, class IsArchive>
PathTargetWalk WalkPathTarget(const std::wstring& path, size_t afterRoot, size_t end, wchar_t lastChar,
                              bool backslashAtEnd, bool mustBePath, Probe probe, IsArchive isArchive)
{
    PathTargetWalk walk = {PathTargetKind::WindowsPath, end, true, 0};
    while (end > afterRoot)
    {
        if (path[end - 1] != L'\\')
        {
            const std::wstring prefix(path, 0, end);
            unsigned long errorCode = 0;
            const PathPrefixState state = probe(prefix, errorCode);
            if (state == PathPrefixState::Directory)
                break;
            if (state == PathPrefixState::File)
            {
                if (lastChar != 0 || backslashAtEnd || mustBePath) // a backslash follows the file name
                {
                    walk.kind = isArchive(prefix) ? PathTargetKind::Archive : PathTargetKind::NotArchive;
                    walk.boundary = end;
                    walk.isDir = false;
                    return walk;
                }
                walk.kind = PathTargetKind::FileOverwrite;
                walk.boundary = path.rfind(L'\\', end - 1); // the existing path must not contain the file name
                walk.isDir = false;
                return walk;
            }
            if (state == PathPrefixState::Failed)
            {
                walk.kind = PathTargetKind::Failed;
                walk.boundary = end;
                walk.errorCode = errorCode;
                return walk;
            }
        }
        end = path.rfind(L'\\', end - 1); // a backslash always follows the root
        lastChar = L'\\';
    }
    walk.boundary = end;
    return walk;
}
