// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The FTP plugin still contains several Open Salamander-era call sites that
// relied on fixed char arrays being accepted by CRT/WinAPI wrappers. Keep the
// compatibility scope local to the plugin while preserving explicit sizes.

template <size_t N>
inline LPSTR _sal_lstrcpynA(char (&dst)[N], LPCSTR src)
{
    return _sal_lstrcpynA(dst, src, static_cast<int>(N));
}
