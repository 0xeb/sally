// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

namespace Sally::Artwork
{
struct IdentityStyle
{
    int Palette = 2; // Green, Red, Blue, Orange; independent of the application icon.
    int Shade = 75;
    int Lettering = 0; // Original vector, Candara, Bahnschrift, Corbel, Segoe UI, Georgia.
    int TextFont = 0; // Segoe UI, Corbel, Candara, Tahoma.
    int Tone = 0; // Coordinated, Slate, Black.
};
inline void Normalize(IdentityStyle& style)
{
    const IdentityStyle defaults;
    if (style.Palette < 0 || style.Palette > 3) style.Palette = defaults.Palette;
    if (style.Shade < 0 || style.Shade > 100) style.Shade = defaults.Shade;
    if (style.Lettering < 0 || style.Lettering > 5) style.Lettering = defaults.Lettering;
    if (style.TextFont < 0 || style.TextFont > 3) style.TextFont = defaults.TextFont;
    if (style.Tone < 0 || style.Tone > 2) style.Tone = defaults.Tone;
}
inline auto IdentitySettings(IdentityStyle& style)
{
    return std::array<std::pair<const wchar_t*, int*>, 5>{{
        {L"Identity palette", &style.Palette}, {L"Identity shade", &style.Shade},
        {L"Identity lettering", &style.Lettering}, {L"Identity text font", &style.TextFont},
        {L"Identity text tone", &style.Tone}}};
}
inline const wchar_t* TextFace(const IdentityStyle& style)
{
    constexpr const wchar_t* faces[] = {L"Segoe UI", L"Corbel", L"Candara", L"Tahoma"};
    return faces[(std::clamp)(style.TextFont, 0, 3)];
}
inline const wchar_t* LetterFace(const IdentityStyle& style)
{
    constexpr const wchar_t* faces[] = {L"Segoe UI", L"Candara", L"Bahnschrift", L"Corbel", L"Segoe UI", L"Georgia"};
    return faces[(std::clamp)(style.Lettering, 0, 5)];
}
inline uint32_t Hsl(double h, double s, double l)
{
    s /= 100; l /= 100;
    const double a = s * (std::min)(l, 1 - l);
    auto channel = [&](int n) {
        const double k = std::fmod(n + h / 30, 12);
        return uint32_t(std::lround(255 * (l - a * (std::max)(-1.0, (std::min)({k - 3, 9 - k, 1.0})))));
    };
    return channel(0) | (channel(8) << 8) | (channel(4) << 16);
}
inline double Luminance(uint32_t color)
{
    auto linear = [](unsigned channel) {
        const double x = channel / 255.0;
        return x <= .04045 ? x / 12.92 : std::pow((x + .055) / 1.055, 2.4);
    };
    return .2126 * linear(color & 255) + .7152 * linear((color >> 8) & 255) + .0722 * linear((color >> 16) & 255);
}
struct IdentityColors { uint32_t Paper, Band, BandLight, Brand, Muted, Link, Line, Wash, Credit; };
inline IdentityColors Colors(IdentityStyle style)
{
    Normalize(style);
    constexpr double hues[] = {102, 355, 211, 29}, saturations[] = {46, 64, 61, 72};
    const double h = hues[style.Palette], s = saturations[style.Palette];
    const auto band = Hsl(h, s, 67 - style.Shade * .37);
    const double lum = Luminance(band);
    const uint32_t dark = Hsl(h, 32, 10);
    const double whiteContrast = 1.05 / (lum + .05), darkContrast = (lum + .05) / (Luminance(dark) + .05);
    uint32_t credit = whiteContrast >= darkContrast ? 0xffffff : dark;
    if ((std::max)(whiteContrast, darkContrast) < 4.5) credit = lum > .179 ? 0 : 0xffffff;
    return {Hsl(h, 12, 98), band, Hsl(h, s * .45, 94 - style.Shade * .035),
            style.Tone == 2 ? 0x201810u : style.Tone == 1 ? 0x524635u : Hsl(h, 34, 29 - style.Shade * .09),
            style.Tone == 2 ? 0x7a7468u : style.Tone == 1 ? 0x7b7165u : Hsl(h, 13, 38),
            Hsl(h, (std::min)(s, 58.0), 29), Hsl(h, 20, 77), Hsl(h, 24, 95), credit};
}
inline constexpr const wchar_t* IdentityCopyright =
    L"Sally © 2025–2026 Elias Bachaalany\nOpen Salamander © 1997–2023 Open Salamander Authors";
inline constexpr double AboutCanvasWidth = 880.0 * 660.0 / 720.0;
inline constexpr int SplashLogicalWidth = 640, SplashLogicalHeight = 314;
inline double CanvasWidth(bool about) { return about ? AboutCanvasWidth : 880.0; }
struct IdentityArtPlacement { int Left, Top, Side; };
inline IdentityArtPlacement ArtPlacement(int width, bool about)
{
    const double scale = width / CanvasWidth(about);
    const int side = static_cast<int>(std::lround((about ? 300 : 429.97) * scale));
    const int top = static_cast<int>(std::lround(16 * scale)) - static_cast<int>(std::lround(side * 56.0 / 1254));
    return {width - side, top, side};
}
} // namespace Sally::Artwork
