// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Update Thumbnail (plugin menu): for each selected JPEG, a new 160 x 120 thumbnail is made from
// the image and written into the file (EXIF IFD1, or a JFXX thumbnail for a file without EXIF)
// without recompressing the image. The file is rewritten through a temporary file next to it and
// swapped in with ReplaceFileW, which keeps its attributes, creation time and security.
//
// The questions (skip, all, read-only, ...) are decided here; the host only shows them.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pictview
{

class ImageEngine;

enum class ThumbnailUpdateQuestion
{
    OpenFailed,  // not an image, unreadable: Skip / Skip all / Cancel
    NotJpeg,     // an image, not JPEG: Skip / Skip all / Cancel
    NoExif,      // create a JFXX thumbnail instead? Yes / All / Skip (No) / Skip all / Cancel
    ReadOnly,    // modify anyway? Yes / All / Skip / Skip all / Cancel
    WriteFailed, // the new file could not be written; the old one is unchanged: Skip / Skip all / Cancel
};

enum class ThumbnailUpdateAnswer
{
    Yes,
    All,
    Skip,
    SkipAll,
    Cancel,
};

struct ThumbnailUpdateHost
{
    // 'detail' is the system or decoder error text, when there is one.
    std::function<ThumbnailUpdateAnswer(ThumbnailUpdateQuestion question, const std::wstring& path, const std::wstring& detail)> Ask;
    // Before each file (index from 0); false cancels.
    std::function<bool(size_t index, size_t count, const std::wstring& path)> Progress;
    // Once per folder in which a file changed.
    std::function<void(const std::wstring& directory)> PathChanged;
};

struct ThumbnailUpdateSummary
{
    size_t Total = 0;
    size_t Updated = 0;
    bool Canceled = false;
};

constexpr uint32_t UpdatedThumbnailWidth = 160;
constexpr uint32_t UpdatedThumbnailHeight = 120;

// The thumbnail JPEG for 'file' (the image at most 160 x 120, in the file's own orientation, no
// metadata segments). Empty with 'error' set when the file cannot be decoded.
std::vector<uint8_t> MakeJpegThumbnail(const ImageEngine& engine, const std::vector<uint8_t>& file, bool& isJpeg,
                                       std::wstring& error);

ThumbnailUpdateSummary UpdateJpegThumbnails(const std::vector<std::wstring>& paths, const ThumbnailUpdateHost& host);

} // namespace pictview
