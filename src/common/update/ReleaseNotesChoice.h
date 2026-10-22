// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace Sally::Update
{
// The reading target is independent of the offered installation target.
// A failed/restarted check always has the installed notes as its safe fallback.
class ReleaseNotesChoice
{
    bool LatestAvailable = false;
    bool Installed = true;

public:
    void BeginCheck()
    {
        LatestAvailable = false;
        Installed = true;
    }
    void CompleteCheck(bool newer)
    {
        LatestAvailable = newer;
        Installed = !newer;
    }
    void SelectInstalled(bool installed) { Installed = installed || !LatestAvailable; }
    bool IsInstalled() const { return Installed; }
    bool CanSelectLatest() const { return LatestAvailable; }
};
} // namespace Sally::Update
