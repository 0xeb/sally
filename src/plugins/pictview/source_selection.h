// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "source_navigation.h"

namespace pictview
{

// Whether the viewed file is selected in its source, as the "Select Source File" button and
// menu item show it. The source is asked again whenever the answer can have changed: a file
// was opened from it, the viewer became active (the selection may have changed in Sally
// meanwhile), or the file was renamed.
class SourceSelectionState
{
public:
    // Asks the source about its current file. When Sally is busy the last answer is kept;
    // without a source, or when the file is no longer in it, the file counts as unselected.
    void Refresh(const ViewerSourceNavigation& source);

    // Selects or unselects the current file in the source. Returns false, leaving the state
    // as it was, when the source cannot do it now (sourceBusy) or no longer has the file.
    bool Toggle(const ViewerSourceNavigation& source, bool& sourceBusy);

    // The viewer no longer comes from a source.
    void Clear() { m_selected = false; }

    bool Selected() const { return m_selected; }

private:
    bool m_selected = false;
};

} // namespace pictview
