// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace sally::bytes
{
// Formats content that is intentionally byte-owned (serialized scripts,
// archive metadata, and byte-oriented engines). UI and filesystem text must
// use the UTF-16 formatter instead.
inline std::string Format(const char* format, ...)
{
    if (format == nullptr)
        return std::string();

    va_list args;
    va_start(args, format);
    const int length = _vscprintf(format, args);
    va_end(args);
    if (length < 0)
        return std::string();

    std::vector<char> buffer(static_cast<size_t>(length) + 1, '\0');
    va_start(args, format);
    const int written = _vsnprintf_s(buffer.data(), buffer.size(), _TRUNCATE,
                                     format, args);
    va_end(args);
    return written < 0 ? std::string() : std::string(buffer.data(), written);
}
} // namespace sally::bytes
