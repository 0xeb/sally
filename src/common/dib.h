// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// loads bitmap hRsrc from resources (obtained from FindResource(...)),
// remaps mapCount colors: mapColor[i] -> toColor[i]
// and creates a bitmap compatible with the desktop

HBITMAP LoadBitmapAndMapColors(HINSTANCE hInst, HRSRC hRsrc, int mapCount,
                               COLORREF* mapColor, COLORREF* toColor);

// A DIB is a BINARY BLOB, not text - dib.cpp:214/:228 define
// these as LPSTR. The sweep widened the DECLARATIONS only; because these are
// FREE functions the mismatch produced no C2511 and never entered the orphan
// column. Found by scripts/freefn-width-scan.py.
DWORD DIBHeight(LPSTR lpDIB);
DWORD DIBWidth(LPSTR lpDIB);
HBITMAP DIBToBitmap(HANDLE hDIB, HPALETTE hPal);
