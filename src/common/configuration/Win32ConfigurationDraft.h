// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>
#include "ConfigurationDraft.h"

namespace sally::configuration
{
    // Model pages supply semantic values themselves; only an active in-place
    // label edit is then read from controls. Ordinary scalar pages use controls.
    void CaptureDraftControls(HWND page, DraftState& output, bool modelPage);
    // Keep invalid pending width edits actionable without treating a valid
    // alternate numeric spelling as a model change.
    void CaptureInvalidColumnWidthControls(HWND page, DraftState& output, int leftControl, int rightControl);
    void CaptureDraftFont(const LOGFONTW& font, DraftState& output);
} // namespace sally::configuration
