// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "SplashArtwork.h"

namespace Sally::Artwork
{
    // Area filtering preserves premultiplied alpha when reducing detailed artwork.
    inline std::vector<unsigned char> ScalePixels(const unsigned char* source, int sw, int sh, int width, int height)
    {
        if (!source || sw < 1 || sh < 1 || width < 1 || height < 1)
            return {};
        std::vector<unsigned char> pixels(size_t(width) * height * 4);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const double x0 = x * double(sw) / width, x1 = (x + 1) * double(sw) / width;
                const double y0 = y * double(sh) / height, y1 = (y + 1) * double(sh) / height;
                double sum[4] = {}, total = 0;
                for (int sy = int(y0); sy < (std::min)(sh, int(std::ceil(y1))); ++sy)
                    for (int sx = int(x0); sx < (std::min)(sw, int(std::ceil(x1))); ++sx)
                    {
                        const double weight = ((std::min)(x1, sx + 1.0) - (std::max)(x0, double(sx))) *
                                              ((std::min)(y1, sy + 1.0) - (std::max)(y0, double(sy)));
                        for (int c = 0; c < 4; ++c)
                            sum[c] += source[(size_t(sy) * sw + sx) * 4 + c] * weight;
                        total += weight;
                    }
                for (int c = 0; c < 4; ++c)
                    pixels[(size_t(y) * width + x) * 4 + c] = static_cast<unsigned char>(std::lround(sum[c] / total));
            }
        return pixels;
    }

    inline void BlurAlpha(std::vector<unsigned char>& alpha, int width, int height, int radius)
    {
        std::vector<unsigned char> temporary(alpha.size());
        for (int pass = 0; pass < 3; ++pass)
        {
            for (int y = 0; y < height; ++y)
            {
                int sum = 0;
                for (int x = -radius; x < width + radius; ++x)
                {
                    if (x + radius < width)
                        sum += alpha[size_t(y) * width + x + radius];
                    if (x - radius - 1 >= 0)
                        sum -= alpha[size_t(y) * width + x - radius - 1];
                    if (x >= 0 && x < width)
                        temporary[size_t(y) * width + x] = static_cast<unsigned char>(sum / (2 * radius + 1));
                }
            }
            for (int x = 0; x < width; ++x)
            {
                int sum = 0;
                for (int y = -radius; y < height + radius; ++y)
                {
                    if (y + radius < height)
                        sum += temporary[size_t(y + radius) * width + x];
                    if (y - radius - 1 >= 0)
                        sum -= temporary[size_t(y - radius - 1) * width + x];
                    if (y >= 0 && y < height)
                        alpha[size_t(y) * width + x] = static_cast<unsigned char>(sum / (2 * radius + 1));
                }
            }
        }
    }

    inline std::vector<unsigned char> MascotOverlay(const unsigned char* source, int sw, int sh, int width, int height, const Rect* placement = nullptr)
    {
        if (width < 1 || height < 1)
            return {};
        const auto layout = Layout(width, height);
        const auto r = placement ? *placement : layout.Mascot;
        auto scaled = ScalePixels(source, sw, sh, r.Width(), r.Height());
        if (scaled.empty())
            return {};
        std::vector<unsigned char> alpha(size_t(width) * height, 0);
        for (int y = 0; y < r.Height() && y + r.Top + layout.ShadowOffset < height; ++y)
            for (int x = 0; x < r.Width() && x + r.Left < width; ++x)
                if (y + r.Top + layout.ShadowOffset >= 0 && x + r.Left >= 0)
                    alpha[size_t(y + r.Top + layout.ShadowOffset) * width + x + r.Left] = scaled[(size_t(y) * r.Width() + x) * 4 + 3];
        BlurAlpha(alpha, width, height, layout.ShadowRadius);
        std::vector<unsigned char> pixels(size_t(width) * height * 4, 0);
        for (size_t i = 0; i < alpha.size(); ++i)
            pixels[i * 4 + 3] = static_cast<unsigned char>((alpha[i] * 12 + 50) / 100);
        for (int y = 0; y < r.Height() && y + r.Top < height; ++y)
            for (int x = 0; x < r.Width() && x + r.Left < width; ++x)
            {
                if (y + r.Top < 0 || x + r.Left < 0)
                    continue;
                const auto src = &scaled[(size_t(y) * r.Width() + x) * 4];
                auto dst = &pixels[(size_t(y + r.Top) * width + x + r.Left) * 4];
                dst[3] = static_cast<unsigned char>(src[3] + (dst[3] * (255 - src[3]) + 127) / 255);
                for (int c = 0; c < 3; ++c)
                    dst[c] = src[c];
            }
        return pixels;
    }
} // namespace Sally::Artwork
