// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "jpeg_thumbnail.h"

#include <algorithm>
#include <cstring>
#include <set>

namespace pictview
{
namespace
{

constexpr uint8_t MarkerApp0 = 0xE0;
constexpr uint8_t MarkerApp1 = 0xE1;
constexpr uint8_t MarkerApp15 = 0xEF;
constexpr uint8_t MarkerCom = 0xFE;
constexpr uint8_t MarkerSos = 0xDA;
constexpr uint8_t MarkerEoi = 0xD9;
constexpr size_t MaxSegmentPayload = 65533; // the length field (65535) counts itself
const uint8_t ExifSignature[6] = {'E', 'x', 'i', 'f', 0, 0};
const uint8_t JfxxSignature[5] = {'J', 'F', 'X', 'X', 0};
constexpr uint8_t JfxxJpegExtension = 0x10;

struct Segment
{
    uint8_t Marker = 0;
    size_t Start = 0;  // the segment's 0xFF
    size_t Length = 0; // marker, length field and payload
    size_t PayloadStart() const { return Start + 4; }
    size_t PayloadLength() const { return Length >= 4 ? Length - 4 : 0; }
};

// The segments from SOI up to and including SOS; 'scan' is where SOS starts.
bool ParseHeader(const std::vector<uint8_t>& file, std::vector<Segment>& segments, size_t& scan)
{
    segments.clear();
    scan = 0;
    const size_t size = file.size();
    if (size < 4 || file[0] != 0xFF || file[1] != 0xD8)
        return false;
    size_t pos = 2;
    while (pos + 1 < size)
    {
        if (file[pos] != 0xFF)
            return false;
        while (pos + 1 < size && file[pos + 1] == 0xFF) // fill bytes
            ++pos;
        if (pos + 1 >= size)
            return false;
        const uint8_t marker = file[pos + 1];
        if (marker == MarkerEoi)
            return false;
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) // no length field
        {
            segments.push_back({marker, pos, 2});
            pos += 2;
            continue;
        }
        if (pos + 4 > size)
            return false;
        const size_t length = (static_cast<size_t>(file[pos + 2]) << 8) | file[pos + 3];
        if (length < 2 || pos + 2 + length > size)
            return false;
        segments.push_back({marker, pos, 2 + length});
        if (marker == MarkerSos)
        {
            scan = pos;
            return true;
        }
        pos += 2 + length;
    }
    return false;
}

bool HasSignature(const std::vector<uint8_t>& file, const Segment& segment, const uint8_t* signature, size_t length)
{
    return segment.PayloadLength() >= length && std::memcmp(file.data() + segment.PayloadStart(), signature, length) == 0;
}

bool IsExifSegment(const std::vector<uint8_t>& file, const Segment& segment)
{
    return segment.Marker == MarkerApp1 && HasSignature(file, segment, ExifSignature, sizeof(ExifSignature));
}

bool IsJfxxSegment(const std::vector<uint8_t>& file, const Segment& segment)
{
    return segment.Marker == MarkerApp0 && HasSignature(file, segment, JfxxSignature, sizeof(JfxxSignature));
}

void AppendSegment(std::vector<uint8_t>& out, uint8_t marker, const std::vector<uint8_t>& payload)
{
    const size_t length = payload.size() + 2;
    out.push_back(0xFF);
    out.push_back(marker);
    out.push_back(static_cast<uint8_t>(length >> 8));
    out.push_back(static_cast<uint8_t>(length & 0xFF));
    out.insert(out.end(), payload.begin(), payload.end());
}

void AppendRange(std::vector<uint8_t>& out, const std::vector<uint8_t>& file, size_t start, size_t length)
{
    out.insert(out.end(), file.begin() + static_cast<std::ptrdiff_t>(start),
               file.begin() + static_cast<std::ptrdiff_t>(start + length));
}

// A TIFF block (the EXIF payload after "Exif\0\0").
class Tiff
{
public:
    Tiff(const uint8_t* data, size_t size) : m_data(data), m_size(size)
    {
        m_valid = size >= 8 && ((data[0] == 'I' && data[1] == 'I') || (data[0] == 'M' && data[1] == 'M'));
        m_little = m_valid && data[0] == 'I';
        m_valid = m_valid && U16(2) == 42;
    }

    bool Valid() const { return m_valid; }
    bool Little() const { return m_little; }
    size_t Size() const { return m_size; }
    bool Has(uint64_t offset, uint64_t length) const { return offset <= m_size && length <= m_size - offset; }

    uint16_t U16(size_t offset) const
    {
        if (!Has(offset, 2))
            return 0;
        const uint8_t* p = m_data + offset;
        return m_little ? static_cast<uint16_t>(p[0] | (p[1] << 8)) : static_cast<uint16_t>((p[0] << 8) | p[1]);
    }

    uint32_t U32(size_t offset) const
    {
        if (!Has(offset, 4))
            return 0;
        const uint8_t* p = m_data + offset;
        return m_little ? static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
                              (static_cast<uint32_t>(p[3]) << 24)
                        : (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                              (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
    }

    // The bytes an IFD at 'offset' covers: its entries, next pointer and out-of-line values,
    // following the Exif, GPS and Interoperability sub-IFDs. False when the IFD is out of range.
    bool IfdExtent(uint32_t offset, size_t& end, std::set<uint32_t>& visited, int depth) const
    {
        if (depth > 4 || !visited.insert(offset).second || !Has(offset, 2))
            return false;
        const size_t count = U16(offset);
        if (!Has(offset + 2ull, count * 12ull + 4))
            return false;
        end = std::max(end, static_cast<size_t>(offset) + 2 + count * 12 + 4);
        for (size_t i = 0; i < count; ++i)
        {
            const size_t entry = offset + 2 + i * 12;
            const uint16_t tag = U16(entry);
            const uint64_t bytes = static_cast<uint64_t>(TypeSize(U16(entry + 2))) * U32(entry + 4);
            if (bytes > 4)
            {
                const uint32_t value = U32(entry + 8);
                if (Has(value, bytes))
                    end = std::max(end, static_cast<size_t>(value + bytes));
            }
            if ((tag == 0x8769 || tag == 0x8825 || tag == 0xA005) && bytes == 4)
                IfdExtent(U32(entry + 8), end, visited, depth + 1); // a broken sub-IFD is skipped
        }
        return true;
    }

    // Where the IFD at 'offset' and every value it refers to begin (thumbnail and strips
    // included), or Size() when it refers to nothing in range.
    size_t IfdStart(uint32_t offset) const
    {
        size_t start = offset;
        const size_t count = U16(offset);
        for (size_t i = 0; i < count && Has(offset + 2ull + i * 12, 12); ++i)
        {
            const size_t entry = offset + 2 + i * 12;
            const uint16_t tag = U16(entry);
            const uint16_t type = U16(entry + 2);
            const uint64_t bytes = static_cast<uint64_t>(TypeSize(type)) * U32(entry + 4);
            if (bytes > 4 && Has(U32(entry + 8), bytes))
                start = std::min<size_t>(start, U32(entry + 8));
            if ((tag == 0x0201 || tag == 0x0111) && U32(entry + 4) >= 1) // the thumbnail, the first strip
            {
                const uint32_t data = bytes > 4 ? U32(U32(entry + 8)) : (type == 3 ? U16(entry + 8) : U32(entry + 8));
                if (data < m_size)
                    start = std::min<size_t>(start, data);
            }
        }
        return start;
    }

    // The value of a SHORT or LONG entry 'tag' of the IFD at 'offset'.
    bool FindNumber(uint32_t offset, uint16_t tag, uint32_t& number) const
    {
        const size_t count = U16(offset);
        for (size_t i = 0; i < count && Has(offset + 2ull + i * 12, 12); ++i)
        {
            const size_t entry = offset + 2 + i * 12;
            if (U16(entry) != tag)
                continue;
            const uint16_t type = U16(entry + 2);
            number = type == 3 ? U16(entry + 8) : U32(entry + 8);
            return type == 3 || type == 4;
        }
        return false;
    }

    static uint32_t TypeSize(uint16_t type)
    {
        switch (type)
        {
        case 1: case 2: case 6: case 7: return 1;
        case 3: case 8: return 2;
        case 4: case 9: case 11: case 13: return 4;
        case 5: case 10: case 12: return 8;
        default: return 0;
        }
    }

private:
    const uint8_t* m_data;
    size_t m_size;
    bool m_valid = false;
    bool m_little = true;
};

class TiffWriter
{
public:
    TiffWriter(std::vector<uint8_t>& bytes, bool little) : m_bytes(bytes), m_little(little) {}

    void Put16(size_t offset, uint16_t value)
    {
        m_bytes[offset + (m_little ? 0 : 1)] = static_cast<uint8_t>(value & 0xFF);
        m_bytes[offset + (m_little ? 1 : 0)] = static_cast<uint8_t>(value >> 8);
    }

    void Put32(size_t offset, uint32_t value)
    {
        for (int i = 0; i < 4; ++i)
            m_bytes[offset + (m_little ? i : 3 - i)] = static_cast<uint8_t>((value >> (8 * i)) & 0xFF);
    }

    // One 12-byte IFD entry; a SHORT value sits in the first two bytes of the value field.
    void Entry(size_t offset, uint16_t tag, uint16_t type, uint32_t count, uint32_t value)
    {
        Put16(offset, tag);
        Put16(offset + 2, type);
        Put32(offset + 4, count);
        Put32(offset + 8, 0);
        if (type == 3)
            Put16(offset + 8, static_cast<uint16_t>(value));
        else
            Put32(offset + 8, value);
    }

private:
    std::vector<uint8_t>& m_bytes;
    bool m_little;
};

// The EXIF thumbnail's IFD: Compression = 6 (JPEG), 72 x 72 dpi, the thumbnail's offset and length.
constexpr size_t NewIfd1Entries = 6;
constexpr size_t NewIfd1Size = 2 + NewIfd1Entries * 12 + 4;

} // namespace

JpegThumbnailKind ClassifyJpegForThumbnail(const std::vector<uint8_t>& file)
{
    std::vector<Segment> segments;
    size_t scan = 0;
    if (!ParseHeader(file, segments, scan))
        return JpegThumbnailKind::NotJpeg;
    for (const Segment& segment : segments)
    {
        if (IsExifSegment(file, segment))
            return JpegThumbnailKind::Exif;
    }
    return JpegThumbnailKind::NoExif;
}

JpegRewriteStatus ReplaceExifThumbnail(const std::vector<uint8_t>& file, const std::vector<uint8_t>& thumbnail,
                                       std::vector<uint8_t>& out)
{
    out.clear();
    std::vector<Segment> segments;
    size_t scan = 0;
    if (!ParseHeader(file, segments, scan))
        return JpegRewriteStatus::NotJpeg;
    auto exif = std::find_if(segments.begin(), segments.end(), [&file](const Segment& s) { return IsExifSegment(file, s); });
    if (exif == segments.end())
        return JpegRewriteStatus::Corrupt;

    const size_t tiffStart = exif->PayloadStart() + sizeof(ExifSignature);
    const Tiff tiff(file.data() + tiffStart, exif->PayloadLength() - sizeof(ExifSignature));
    if (!tiff.Valid())
        return JpegRewriteStatus::Corrupt;
    const uint32_t ifd0 = tiff.U32(4);
    size_t otherEnd = 8;
    std::set<uint32_t> visited;
    if (!tiff.IfdExtent(ifd0, otherEnd, visited, 0))
        return JpegRewriteStatus::Corrupt;
    const size_t nextPointer = ifd0 + 2 + static_cast<size_t>(tiff.U16(ifd0)) * 12;
    const uint32_t ifd1 = tiff.U32(nextPointer);

    // Keep everything up to the old IFD1 when nothing else lies past it; without an IFD1 the
    // new one is appended (bytes past the known tags may belong to a maker note).
    size_t keep = tiff.Size();
    if (ifd1 != 0 && ifd1 < tiff.Size())
    {
        const size_t start = tiff.IfdStart(ifd1);
        if (start >= otherEnd)
            keep = start;
    }

    std::vector<uint8_t> block(file.begin() + static_cast<std::ptrdiff_t>(tiffStart),
                               file.begin() + static_cast<std::ptrdiff_t>(tiffStart + keep));
    if (block.size() % 2 != 0)
        block.push_back(0); // TIFF offsets are even
    const size_t ifdOffset = block.size();
    const size_t xResolution = ifdOffset + NewIfd1Size;
    const size_t yResolution = xResolution + 8;
    const size_t thumbnailOffset = yResolution + 8;
    const size_t total = thumbnailOffset + thumbnail.size();
    if (sizeof(ExifSignature) + total > MaxSegmentPayload)
        return JpegRewriteStatus::TooLarge;
    block.resize(total, 0);

    TiffWriter writer(block, tiff.Little());
    writer.Put32(nextPointer, static_cast<uint32_t>(ifdOffset));
    writer.Put16(ifdOffset, static_cast<uint16_t>(NewIfd1Entries));
    size_t entry = ifdOffset + 2;
    writer.Entry(entry, 0x0103, 3, 1, 6); // Compression: JPEG
    writer.Entry(entry += 12, 0x011A, 5, 1, static_cast<uint32_t>(xResolution));
    writer.Entry(entry += 12, 0x011B, 5, 1, static_cast<uint32_t>(yResolution));
    writer.Entry(entry += 12, 0x0128, 3, 1, 2); // ResolutionUnit: inch
    writer.Entry(entry += 12, 0x0201, 4, 1, static_cast<uint32_t>(thumbnailOffset));
    writer.Entry(entry += 12, 0x0202, 4, 1, static_cast<uint32_t>(thumbnail.size()));
    writer.Put32(entry + 12, 0); // no further IFD
    for (const size_t rational : {xResolution, yResolution})
    {
        writer.Put32(rational, 72);
        writer.Put32(rational + 4, 1);
    }
    std::copy(thumbnail.begin(), thumbnail.end(), block.begin() + static_cast<std::ptrdiff_t>(thumbnailOffset));

    std::vector<uint8_t> payload(ExifSignature, ExifSignature + sizeof(ExifSignature));
    payload.insert(payload.end(), block.begin(), block.end());
    out.reserve(file.size() + thumbnail.size());
    AppendRange(out, file, 0, 2);
    for (const Segment& segment : segments)
    {
        if (segment.Start == scan)
            break;
        if (&segment == &*exif)
            AppendSegment(out, MarkerApp1, payload);
        else
            AppendRange(out, file, segment.Start, segment.Length);
    }
    AppendRange(out, file, scan, file.size() - scan);
    return JpegRewriteStatus::Ok;
}

JpegRewriteStatus InsertJfxxThumbnail(const std::vector<uint8_t>& file, const std::vector<uint8_t>& thumbnail,
                                      std::vector<uint8_t>& out)
{
    out.clear();
    std::vector<Segment> segments;
    size_t scan = 0;
    if (!ParseHeader(file, segments, scan))
        return JpegRewriteStatus::NotJpeg;
    std::vector<uint8_t> payload(JfxxSignature, JfxxSignature + sizeof(JfxxSignature));
    payload.push_back(JfxxJpegExtension);
    payload.insert(payload.end(), thumbnail.begin(), thumbnail.end());
    if (payload.size() > MaxSegmentPayload)
        return JpegRewriteStatus::TooLarge;

    out.reserve(file.size() + payload.size() + 4);
    AppendRange(out, file, 0, 2);
    bool inserted = false;
    for (const Segment& segment : segments)
    {
        if (IsJfxxSegment(file, segment))
            continue; // replaced
        if (!inserted && segment.Marker != MarkerApp0)
        {
            AppendSegment(out, MarkerApp0, payload);
            inserted = true;
        }
        if (segment.Start == scan)
            break;
        AppendRange(out, file, segment.Start, segment.Length);
    }
    AppendRange(out, file, scan, file.size() - scan);
    return JpegRewriteStatus::Ok;
}

bool StripJpegApplicationSegments(const std::vector<uint8_t>& jpeg, std::vector<uint8_t>& out)
{
    out.clear();
    std::vector<Segment> segments;
    size_t scan = 0;
    if (!ParseHeader(jpeg, segments, scan))
        return false;
    AppendRange(out, jpeg, 0, 2);
    for (const Segment& segment : segments)
    {
        if (segment.Start == scan)
            break;
        if ((segment.Marker >= MarkerApp0 && segment.Marker <= MarkerApp15) || segment.Marker == MarkerCom)
            continue;
        AppendRange(out, jpeg, segment.Start, segment.Length);
    }
    AppendRange(out, jpeg, scan, jpeg.size() - scan);
    return true;
}

std::vector<uint8_t> ExifThumbnailBytes(const std::vector<uint8_t>& file)
{
    std::vector<Segment> segments;
    size_t scan = 0;
    if (!ParseHeader(file, segments, scan))
        return {};
    for (const Segment& segment : segments)
    {
        if (!IsExifSegment(file, segment))
            continue;
        const size_t tiffStart = segment.PayloadStart() + sizeof(ExifSignature);
        const Tiff tiff(file.data() + tiffStart, segment.PayloadLength() - sizeof(ExifSignature));
        if (!tiff.Valid())
            return {};
        const uint32_t ifd0 = tiff.U32(4);
        if (!tiff.Has(ifd0, 2))
            return {};
        const uint32_t ifd1 = tiff.U32(ifd0 + 2 + static_cast<size_t>(tiff.U16(ifd0)) * 12);
        uint32_t offset = 0;
        uint32_t length = 0;
        if (ifd1 == 0 || !tiff.FindNumber(ifd1, 0x0201, offset) || !tiff.FindNumber(ifd1, 0x0202, length) ||
            !tiff.Has(offset, length))
            return {};
        const auto begin = file.begin() + static_cast<std::ptrdiff_t>(tiffStart + offset);
        return std::vector<uint8_t>(begin, begin + static_cast<std::ptrdiff_t>(length));
    }
    return {};
}

std::vector<uint8_t> JfxxThumbnailBytes(const std::vector<uint8_t>& file)
{
    std::vector<Segment> segments;
    size_t scan = 0;
    if (!ParseHeader(file, segments, scan))
        return {};
    for (const Segment& segment : segments)
    {
        const size_t header = sizeof(JfxxSignature) + 1;
        if (IsJfxxSegment(file, segment) && segment.PayloadLength() > header &&
            file[segment.PayloadStart() + sizeof(JfxxSignature)] == JfxxJpegExtension)
        {
            const auto begin = file.begin() + static_cast<std::ptrdiff_t>(segment.PayloadStart() + header);
            return std::vector<uint8_t>(begin, begin + static_cast<std::ptrdiff_t>(segment.PayloadLength() - header));
        }
    }
    return {};
}

size_t JpegScanOffset(const std::vector<uint8_t>& file)
{
    std::vector<Segment> segments;
    size_t scan = 0;
    return ParseHeader(file, segments, scan) ? scan : 0;
}

} // namespace pictview
