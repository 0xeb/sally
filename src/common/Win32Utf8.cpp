// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef SALLY_WINDOWS_TERMINAL_STANDALONE
#include "precomp.h"
#else
#include <windows.h>
#endif

#include "Win32Utf8.h"
#include "Win32TextCodec.h"

bool Win32StrictUtf8ToWide(const char* text, size_t length, std::wstring& wide)
{
    return Win32DecodeText(CP_UTF8, text, length, wide).Succeeded();
}
