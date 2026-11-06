// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// The DPI arithmetic Sally uses, without Windows, so it can later be fed a per-window DPI
// instead of the one system DPI. Behaviour is unchanged from the code it replaced.

#pragma once

namespace sally::dpi
{

// The scale step Sally uses for a DPI, in percent: Windows' standard steps, rounding up.
//   <=96 100, <=120 125, <=144 150, <=192 200, <=240 250, <=288 300, <=384 400, otherwise 500.
// (175% = 168 DPI therefore draws at 200%.)
constexpr int ScalePercentForDpi(int dpi)
{
    return dpi <= 96    ? 100
           : dpi <= 120 ? 125
           : dpi <= 144 ? 150
           : dpi <= 192 ? 200
           : dpi <= 240 ? 250
           : dpi <= 288 ? 300
           : dpi <= 384 ? 400
                        : 500;
}

// Base icon sizes at 100%, in the order of CIconSizeEnum (16, 32, 48).
constexpr int kBaseIconSizes[3] = {16, 32, 48};

// The pixel size of icon size 'sizeIndex' (0..2) at 'dpi'; 16 for an unknown index.
constexpr int IconSizeForDpi(int sizeIndex, int dpi)
{
    return sizeIndex < 0 || sizeIndex >= 3 ? 16 : kBaseIconSizes[sizeIndex] * ScalePercentForDpi(dpi) / 100;
}

// A font height for 'points' at 'dpi' as LOGFONT wants it (negative: character height), truncated
// as the viewer has always computed it: 10 pt is -13 at 96 DPI, -16 at 120, -20 at 144.
constexpr int FontHeightForDpi(int points, int dpi)
{
    return -(points * dpi) / 72;
}

} // namespace sally::dpi
