// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "unchm_text.h"

#include "common/Win32TextCodec.h"

#include <cstring>

bool DecodeChmPathUtf8(const char* pathBytes, std::wstring& path)
{
    return pathBytes != nullptr &&
           Win32DecodeText(CP_UTF8, pathBytes, std::strlen(pathBytes), path).Succeeded();
}
