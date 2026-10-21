// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <windows.h>

// A cryptographic entropy source. ERROR_SUCCESS means the whole buffer was
// filled; callers must reject errors instead of using a weaker fallback.
class ISecureRandom
{
public:
    virtual ~ISecureRandom() = default;
    virtual DWORD Fill(void* buffer, size_t size) = 0;
};

ISecureRandom* GetWin32SecureRandom();
