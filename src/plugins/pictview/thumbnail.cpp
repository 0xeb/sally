// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "thumbnail.h"

#include "engine/wic_engine.h"

#include <algorithm>
#include <limits>
#include <new>
#include <vector>

namespace pictview
{
namespace
{

bool IsUnsupportedOpenFailure(const ImageError& error)
{
    return error.Code == ImageErrorCode::UnsupportedFormat ||
           error.Code == ImageErrorCode::InvalidArgument;
}

bool FrameExceedsThumbnailSourceLimit(const ImageFrameInfo& frame, uint32_t maxMegapixels)
{
    const uint64_t framePixels = static_cast<uint64_t>(frame.Width) * frame.Height;
    const uint64_t maxPixels = static_cast<uint64_t>(maxMegapixels) * 1024ull * 1024ull;
    return framePixels > maxPixels;
}

bool SyncCancellation(CSalamanderThumbnailMakerAbstract* thumbMaker, CancellationSource& cancellation)
{
    if (thumbMaker != nullptr && thumbMaker->GetCancelProcessing())
    {
        cancellation.Cancel();
        return true;
    }

    return false;
}

} // namespace

DWORD BlendThumbnailPixel(const unsigned char* bgra, COLORREF background)
{
    const unsigned alpha = bgra[3];
    if (alpha == 255)
        return RGB(bgra[2], bgra[1], bgra[0]);
    auto mix = [alpha](unsigned value, unsigned under) { return static_cast<BYTE>((value * alpha + under * (255 - alpha) + 127) / 255); };
    return RGB(mix(bgra[2], GetRValue(background)), mix(bgra[1], GetGValue(background)), mix(bgra[0], GetBValue(background)));
}

bool ThumbnailIsOnlyPreview(bool fromFullFrame, uint32_t thumbWidth, uint32_t thumbHeight, uint32_t boxWidth,
                            uint32_t boxHeight, uint32_t imageWidth, uint32_t imageHeight)
{
    if (fromFullFrame)
        return false;
    // Smaller than the box on both sides, although the image could fill it on one of them.
    const bool smallerThanBox = thumbWidth < boxWidth && thumbHeight < boxHeight;
    const bool imageCouldFill = imageWidth > thumbWidth || imageHeight > thumbHeight;
    return smallerThanBox && imageCouldFill;
}

BOOL LoadThumbnailFromPath(const wchar_t* path, int thumbWidth, int thumbHeight,
                           CSalamanderThumbnailMakerAbstract* thumbMaker,
                           BOOL fastThumbnail)
{
    return LoadThumbnailFromPath(path, thumbWidth, thumbHeight, thumbMaker, fastThumbnail, ThumbnailLoadOptions());
}

BOOL LoadThumbnailFromPath(const wchar_t* path, int thumbWidth, int thumbHeight,
                           CSalamanderThumbnailMakerAbstract* thumbMaker,
                           BOOL fastThumbnail,
                           const ThumbnailLoadOptions& thumbnailOptions)
{
    if (path == nullptr || path[0] == L'\0' || thumbMaker == nullptr)
        return FALSE;

    CancellationSource cancellation;
    if (SyncCancellation(thumbMaker, cancellation))
        return TRUE;

    try
    {
        ImageEngine engine;
        if (!engine.IsAvailable())
        {
            thumbMaker->SetError();
            return TRUE;
        }

        OpenDocumentResult open = engine.OpenPath(path, cancellation.Token());
        if (!open.Succeeded())
        {
            if (IsUnsupportedOpenFailure(open.Error))
                return FALSE;

            thumbMaker->SetError();
            return TRUE;
        }

        if (open.Document->Metadata().Frames.empty())
        {
            thumbMaker->SetError();
            return TRUE;
        }

        if (FrameExceedsThumbnailSourceLimit(open.Document->Metadata().Frames[0],
                                             thumbnailOptions.MaxSourceMegapixels))
        {
            return FALSE;
        }

        if (SyncCancellation(thumbMaker, cancellation))
            return TRUE;

        DecodeOptions options;
        // The configured "maximum image size for thumbnails" (megapixels) bounds the decode too.
        const uint64_t megapixels = (std::max)(static_cast<uint64_t>(thumbnailOptions.MaxSourceMegapixels), 90ull);
        options.Limits.MaxPixels = megapixels * 1024ull * 1024ull;
        options.Limits.MaxBytes = options.Limits.MaxPixels * 4ull + 64ull * 1024ull * 1024ull;
        options.ApplyOrientation = thumbnailOptions.ApplyExifOrientation;

        const uint32_t maxThumbWidth = thumbWidth > 0 ? static_cast<uint32_t>(thumbWidth) : 256u;
        const uint32_t maxThumbHeight = thumbHeight > 0 ? static_cast<uint32_t>(thumbHeight) : 256u;
        ThumbnailDecodeOptions decodeOptions;
        // The quality round (fastThumbnail FALSE) always decodes the image itself.
        decodeOptions.UseEmbeddedThumbnailSources = !thumbnailOptions.IgnoreEmbeddedThumbnails && fastThumbnail != FALSE;
        DecodeFrameResult decoded = open.Document->DecodeFrameThumbnail(0,
                                                                        maxThumbWidth,
                                                                        maxThumbHeight,
                                                                        decodeOptions,
                                                                        options,
                                                                        cancellation.Token());
        if (!decoded.Succeeded())
        {
            if (decoded.Error.Code != ImageErrorCode::Canceled)
                thumbMaker->SetError();
            return TRUE;
        }

        const ImageSurface& surface = decoded.Surface;
        if (surface.Width == 0 || surface.Height == 0 || surface.Stride < surface.Width * 4u)
        {
            thumbMaker->SetError();
            return TRUE;
        }

        if (surface.Width > static_cast<unsigned int>((std::numeric_limits<int>::max)()) ||
            surface.Height > static_cast<unsigned int>((std::numeric_limits<int>::max)()))
        {
            thumbMaker->SetError();
            return TRUE;
        }

        const ImageFrameInfo& frame = open.Document->Metadata().Frames[0];
        const DWORD flags = fastThumbnail != FALSE &&
                                    ThumbnailIsOnlyPreview(decoded.Source == DecodeFrameResult::SourceKind::FullFrame, surface.Width,
                                                           surface.Height, maxThumbWidth, maxThumbHeight, frame.Width, frame.Height)
                                ? SSTHUMB_ONLY_PREVIEW
                                : 0;
        if (!thumbMaker->SetParameters(static_cast<int>(surface.Width),
                                       static_cast<int>(surface.Height),
                                       flags))
        {
            return TRUE;
        }

        const unsigned int rowsPerChunk = (std::min)(surface.Height, 64u);
        std::vector<DWORD> colorRows;
        colorRows.resize(static_cast<size_t>(surface.Width) * rowsPerChunk);

        for (unsigned int y = 0; y < surface.Height;)
        {
            if (SyncCancellation(thumbMaker, cancellation))
                return TRUE;

            const unsigned int rows = (std::min)(rowsPerChunk, surface.Height - y);
            for (unsigned int row = 0; row < rows; ++row)
            {
                const unsigned char* src = surface.Pixels.data() +
                                           static_cast<size_t>(y + row) * surface.Stride;
                DWORD* dst = colorRows.data() + static_cast<size_t>(row) * surface.Width;
                for (unsigned int x = 0; x < surface.Width; ++x)
                    dst[x] = BlendThumbnailPixel(src + static_cast<size_t>(x) * 4, thumbnailOptions.Background);
            }

            if (!thumbMaker->ProcessBuffer(colorRows.data(), static_cast<int>(rows)))
                return TRUE;

            y += rows;
        }
    }
    catch (const std::bad_alloc&)
    {
        thumbMaker->SetError();
        return TRUE;
    }

    return TRUE;
}

} // namespace pictview
