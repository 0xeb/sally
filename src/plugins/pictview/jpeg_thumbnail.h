// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// The thumbnail stored inside a JPEG file, rewritten without touching the image: the EXIF
// thumbnail (IFD1 of the APP1 "Exif" segment) or, for a file without EXIF, a JFIF extension
// (APP0 "JFXX") thumbnail. Every other segment and the entropy-coded image are copied byte for
// byte.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pictview
{

enum class JpegThumbnailKind
{
    NotJpeg, // no JPEG header, or the segments before the image cannot be read
    Exif,    // has an APP1 "Exif" segment
    NoExif,
};

enum class JpegRewriteStatus
{
    Ok,
    NotJpeg,
    Corrupt,  // the EXIF block cannot be read
    TooLarge, // the rewritten segment would pass the 64 KB JPEG segment limit
};

JpegThumbnailKind ClassifyJpegForThumbnail(const std::vector<uint8_t>& file);

// EXIF: IFD1 becomes a JPEG thumbnail (Compression 6, 72 dpi, JPEGInterchangeFormat and
// ...Length). The old IFD1 and its thumbnail are dropped when they are the tail of the EXIF block
// (as cameras write it); otherwise they stay as unused bytes, so no other offset moves.
JpegRewriteStatus ReplaceExifThumbnail(const std::vector<uint8_t>& file, const std::vector<uint8_t>& thumbnail,
                                       std::vector<uint8_t>& out);
// No EXIF: an APP0 JFXX segment (extension code 0x10, JPEG) after the leading APP0 segments
// (JFIF); an older JFXX segment is removed.
JpegRewriteStatus InsertJfxxThumbnail(const std::vector<uint8_t>& file, const std::vector<uint8_t>& thumbnail,
                                      std::vector<uint8_t>& out);

// A JPEG without its APPn and COM segments: an embedded thumbnail carries none.
bool StripJpegApplicationSegments(const std::vector<uint8_t>& jpeg, std::vector<uint8_t>& out);

// The thumbnails as stored, empty when there is none.
std::vector<uint8_t> ExifThumbnailBytes(const std::vector<uint8_t>& file);
std::vector<uint8_t> JfxxThumbnailBytes(const std::vector<uint8_t>& file);
// Where the image data starts (the SOS segment); 0 when the file has no readable header.
size_t JpegScanOffset(const std::vector<uint8_t>& file);

} // namespace pictview
