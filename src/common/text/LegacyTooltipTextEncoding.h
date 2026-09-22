// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/Win32TextCodec.h"

#include <string>

namespace sally::legacy_tooltip
{

// WM_USER_TTGETTEXT is the frozen v107 message contract: the sender receives no capacity
// parameter and must honor TOOLTIP_TEXT_MAX. Validate termination inside the supplied contract
// storage, then decode only the bounded payload. New/live senders use WM_USER_TTGETTEXTW.
inline bool DecodeV107Payload(const char* storage, size_t capacity, std::wstring& text) noexcept
{
    if (storage == nullptr || capacity == 0)
        return false;
    size_t length = 0;
    while (length < capacity && storage[length] != '\0')
        ++length;
    if (length == capacity)
        return false;
    return static_cast<bool>(Win32DecodeTextPermissive(GetACP(), storage, length, text));
}

} // namespace sally::legacy_tooltip
