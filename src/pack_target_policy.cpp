// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// SALLY_PACK_TARGET_POLICY_STANDALONE lets the headless test compile this real
// translation unit without pulling in Sally's precompiled header.
#ifndef SALLY_PACK_TARGET_POLICY_STANDALONE
#include "precomp.h"
#endif

#include "pack_target_policy.h"
#include "common/Win32TextCodec.h"

bool ShouldRerouteCopyTargetToArchive(bool isCopyOrMove,
                                      bool targetHadTrailingBackslash,
                                      bool targetExists,
                                      bool targetIsDirectory,
                                      int packIsArchiveFormat,
                                      bool packerUsableForFormat)
{
    // Reroute to the pack flow ONLY when every one of these holds; otherwise keep the
    // classic behavior (create/overwrite a plain file, or the existing archive-container
    // handling for a trailing backslash):
    //  - it is a Copy or Move,
    //  - the user did not already end the path with a backslash (that path is already
    //    handled as an archive container by the splitter),
    //  - the target already EXISTS (never turn a brand-new "name.zip" into a pack — that
    //    would change the long-standing "create a file" behavior),
    //  - it is a file, not a directory,
    //  - it is a recognized archive format whose packer can update it.
    return isCopyOrMove &&
           !targetHadTrailingBackslash &&
           targetExists &&
           !targetIsDirectory &&
           packIsArchiveFormat != 0 &&
           packerUsableForFormat;
}

bool BuildArchiveProbeNameFromWidePath(const wchar_t* fullWidePath, std::string& probeName)
{
    probeName.clear();
    if (fullWidePath == NULL || fullWidePath[0] == 0)
        return false;

    const std::wstring full(fullWidePath);

    // Only the final component can carry the association; a dot in a parent directory name is
    // not an extension.
    const std::wstring::size_type sep = full.find_last_of(L"\\/");
    const std::wstring component = (sep == std::wstring::npos) ? full : full.substr(sep + 1);
    if (component.empty())
        return false;

    // Longest suffix of the component that survives an exact ANSI round-trip.
    //
    // NOT just the text after the last dot. PackIsArchive supports multi-part associations, and
    // it stores each one REVERSED WITH A TRAILING DOT: "tar.gz" becomes "g.rat.", so a match
    // requires the literal ".tar" ahead of the ".gz". Probing only the final extension therefore
    // could never match "tar.gz" - it would match a plain "gz" association instead, or nothing
    // at all, and returning nothing sends F5/F6 back to the destructive ordinary-overwrite path
    // that #94 exists to prevent.
    //
    // Truncation can only ever cost a match, never invent one: a pattern longer than the probe
    // runs off the front and fails the bounds check. So the longest representable suffix is both
    // the most accurate answer available and the safe direction to err in.
    for (std::wstring::size_type start = 0; start < component.size(); ++start)
    {
        std::string ansi;
        if (Win32EncodeAcpExact(component.substr(start), ansi))
        {
            if (ansi.empty())
                return false;
            probeName = ansi;
            return true;
        }
    }

    // Nothing in the component is exactly representable - refuse rather than guess.
    return false;
}
