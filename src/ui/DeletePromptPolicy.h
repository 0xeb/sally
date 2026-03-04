// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#include "IPrompter.h"

namespace DeletePromptPolicy
{
UINT ConfirmDeleteMessageBoxFlags();
PromptResult MapConfirmDeleteResult(int dialogResult);
} // namespace DeletePromptPolicy

