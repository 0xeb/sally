// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/Win32TextCodec.h"

#include <string>

namespace checksum
{

inline bool EncodeFileNameUtf8(const std::wstring& name, std::string& bytes)
{
    return Win32EncodeText(CP_UTF8, name, bytes).Succeeded();
}

inline bool DecodeFileName(const std::string& bytes, std::wstring& name)
{
    if (Win32DecodeText(CP_UTF8, bytes, name).Succeeded())
        return true;
    return Win32DecodeText(CP_ACP, bytes, name).Succeeded();
}

} // namespace checksum
