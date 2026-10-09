// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace Sally
{

// The "\\?\" form of an absolute, already normalised path, so that file APIs accept it at any
// length. Other paths are returned unchanged.
inline std::wstring ToExtendedLengthPath(const std::wstring& path)
{
    if (path.compare(0, 4, L"\\\\?\\") == 0)
        return path;
    if (path.compare(0, 2, L"\\\\") == 0)
        return L"\\\\?\\UNC\\" + path.substr(2);
    if (path.size() >= 3 && path[1] == L':' && path[2] == L'\\')
        return L"\\\\?\\" + path;
    return path;
}

} // namespace Sally
