// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "source_selection.h"

namespace pictview
{

namespace
{

bool HasCurrentFile(const ViewerSourceNavigation& source)
{
    return source.SourceUID != -1 && !source.CurrentPath.empty();
}

} // namespace

void SourceSelectionState::Refresh(const ViewerSourceNavigation& source)
{
    if (source.QuerySelection == nullptr || !HasCurrentFile(source))
    {
        m_selected = false;
        return;
    }

    bool selected = false;
    bool sourceBusy = false;
    if (source.QuerySelection(source.Context, source.SourceUID, source.CurrentIndex, source.CurrentPath.c_str(),
                              selected, sourceBusy))
        m_selected = selected;
    else if (!sourceBusy)
        m_selected = false;
}

bool SourceSelectionState::Toggle(const ViewerSourceNavigation& source, bool& sourceBusy)
{
    sourceBusy = false;
    if (source.ToggleSelection == nullptr || !HasCurrentFile(source))
        return false;

    bool selected = false;
    if (!source.ToggleSelection(source.Context, source.SourceUID, source.CurrentIndex, source.CurrentPath.c_str(),
                                selected, sourceBusy))
        return false;
    m_selected = selected;
    return true;
}

} // namespace pictview
