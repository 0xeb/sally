// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Renamer's expression engine remains byte-oriented. Its byte encoding is UTF-8; Windows
// filesystem boundaries decode it to UTF-16. Keeping this helper SDK-free makes the boundary
// directly testable without duplicating the mature rename parser in a second string model.

#include <string>

#include "renamer_text.h"

inline std::string EncodeFileNameUtf8(const wchar_t* wideName)
{
    std::string out;
    if (!TryWideToRenamerText(wideName, out))
        out.clear();
    return out;
}
