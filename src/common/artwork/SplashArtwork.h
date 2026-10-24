// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Sally::Artwork
{
    struct Rect
    {
        int Left, Top, Right, Bottom;
        int Width() const { return Right - Left; }
        int Height() const { return Bottom - Top; }
    };
    struct SplashLayout
    {
        Rect Mascot, Tagline, Version, Copyright, Status;
        int TaglineFont, RuntimeFont, CopyrightFont, ShadowRadius, ShadowOffset;
    };
    inline SplashLayout Layout(int width, int height)
    {
        const double scale = (std::min)((std::max)(width, 1) / 1672.0, (std::max)(height, 1) / 941.0);
        auto px = [scale](double value)
        { return static_cast<int>(std::lround(value * scale)); };
        auto rect = [&](int x, int y, int right, int bottom)
        { return Rect{px(x), px(y), px(right), px(bottom)}; };
        return {rect(855, 124, 1672, 941), rect(64, 449, 810, 537), rect(64, 586, 810, 660),
                rect(64, 770, 810, 856), rect(64, 865, 810, 925),
                (std::max)(1, px(52)), (std::max)(1, px(36)), (std::max)(1, px(28)),
                (std::max)(1, px(8)), px(6)};
    }

    // This adapter accepts only our single embedded PNG contract, not arbitrary SVG.
    // Decode into owned storage and validate chunk framing before the legacy PNG
    // decoder, whose callback assumes every requested byte exists in its input.
    inline bool EmbeddedPng(std::string_view svg, std::vector<unsigned char>& output)
    {
        constexpr std::string_view prefix = "data:image/png;base64,";
        const size_t start = svg.find(prefix);
        if (start == std::string_view::npos || svg.find(prefix, start + prefix.size()) != std::string_view::npos)
            return false;
        const size_t begin = start + prefix.size(), end = svg.find('"', begin);
        if (end == std::string_view::npos || end == begin || (end - begin) % 4 != 0)
            return false;
        auto value = [](char ch) -> int
        {
            if (ch >= 'A' && ch <= 'Z')
                return ch - 'A';
            if (ch >= 'a' && ch <= 'z')
                return ch - 'a' + 26;
            if (ch >= '0' && ch <= '9')
                return ch - '0' + 52;
            if (ch == '+')
                return 62;
            if (ch == '/')
                return 63;
            return -1;
        };
        std::vector<unsigned char> decoded;
        decoded.reserve((end - begin) / 4 * 3);
        for (size_t i = begin; i < end; i += 4)
        {
            const int a = value(svg[i]), b = value(svg[i + 1]);
            const bool pad2 = svg[i + 2] == '=', pad3 = svg[i + 3] == '=';
            const int c = pad2 ? 0 : value(svg[i + 2]), d = pad3 ? 0 : value(svg[i + 3]);
            if (a < 0 || b < 0 || c < 0 || d < 0 || (pad2 && !pad3) ||
                ((pad2 || pad3) && i + 4 != end) || (pad2 && (b & 15)) || (!pad2 && pad3 && (c & 3)))
                return false;
            decoded.push_back(static_cast<unsigned char>((a << 2) | (b >> 4)));
            if (!pad2)
                decoded.push_back(static_cast<unsigned char>((b << 4) | (c >> 2)));
            if (!pad3)
                decoded.push_back(static_cast<unsigned char>((c << 6) | d));
        }
        const unsigned char signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
        if (decoded.size() < 33 || !std::equal(signature, signature + 8, decoded.begin()))
            return false;
        auto u32 = [&](size_t offset)
        {
            return (uint32_t(decoded[offset]) << 24) | (uint32_t(decoded[offset + 1]) << 16) |
                   (uint32_t(decoded[offset + 2]) << 8) | uint32_t(decoded[offset + 3]);
        };
        bool header = false, data = false, done = false;
        for (size_t pos = 8; pos < decoded.size();)
        {
            if (decoded.size() - pos < 12)
                return false;
            const uint32_t length = u32(pos), type = u32(pos + 4);
            if (length > decoded.size() - pos - 12)
                return false;
            if (!header)
            {
                if (type != 0x49484452 || length != 13 || !u32(pos + 8) || !u32(pos + 12) ||
                    u32(pos + 8) > 4096 || u32(pos + 12) > 4096 || decoded[pos + 16] != 8 ||
                    decoded[pos + 17] != 6 || decoded[pos + 18] != 0 || decoded[pos + 19] != 0 || decoded[pos + 20] != 0)
                    return false;
                header = true;
            }
            else if (type == 0x49484452)
                return false;
            if (type == 0x49444154)
                data = true;
            if (type == 0x49454e44)
            {
                if (length != 0 || pos + 12 != decoded.size() || !data)
                    return false;
                done = true;
            }
            pos += size_t(length) + 12;
        }
        if (!done)
            return false;
        output.swap(decoded);
        return true;
    }
} // namespace Sally::Artwork
