// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "exif_text.h"

#include "viewer.h"
#include "pictview.rh2"

#include <cstdint>

namespace pictview
{
namespace
{

struct ExifTitle
{
    uint16_t Tag;
    UINT TextId;
};

struct ExifValueWord
{
    bool Gps;
    uint16_t Tag;
    int64_t Value;
    UINT TextId;
};

#include "exif_text_table.inc"

constexpr uint16_t FlashTag = 0x9209;

bool IsGps(const ImageExifEntry& entry)
{
    return entry.Group == L"GPS";
}

// EXIF 2.32 Flash: bit 0 fired, bits 1-2 strobe return, bits 3-4 mode, bit 5 no flash function,
// bit 6 red-eye reduction.
std::wstring DescribeFlash(int64_t value)
{
    if ((value & 0x20) != 0)
        return ViewerText(IDS_EXIF_VALUE_FLASH_NO_FUNCTION);
    std::wstring text = ViewerText((value & 1) != 0 ? IDS_EXIF_VALUE_FLASH_FIRED : IDS_EXIF_VALUE_FLASH_NOT_FIRED);
    auto append = [&text](UINT id) {
        text += L", ";
        text += ViewerText(id);
    };
    switch ((value >> 3) & 3)
    {
    case 1:
        append(IDS_EXIF_VALUE_FLASH_COMPULSORY);
        break;
    case 2:
        append(IDS_EXIF_VALUE_FLASH_SUPPRESSED);
        break;
    case 3:
        append(IDS_EXIF_VALUE_FLASH_AUTO);
        break;
    }
    switch ((value >> 1) & 3)
    {
    case 2:
        append(IDS_EXIF_VALUE_FLASH_RETURN_NOT_DETECTED);
        break;
    case 3:
        append(IDS_EXIF_VALUE_FLASH_RETURN_DETECTED);
        break;
    }
    if ((value & 0x40) != 0)
        append(IDS_EXIF_VALUE_FLASH_RED_EYE);
    return text;
}

} // namespace

std::wstring ExifTagTitle(const ImageExifEntry& entry)
{
    const bool gps = IsGps(entry);
    const ExifTitle* begin = gps ? std::begin(GpsTitles) : std::begin(ExifTitles);
    const ExifTitle* end = gps ? std::end(GpsTitles) : std::end(ExifTitles);
    for (const ExifTitle* title = begin; title != end; ++title)
    {
        if (title->Tag == entry.Tag)
        {
            const wchar_t* text = ViewerText(title->TextId);
            return text != nullptr && text[0] != L'\0' ? std::wstring(text) : entry.Name;
        }
    }
    return entry.Name;
}

std::wstring ExifValueText(const ImageExifEntry& entry)
{
    if (!entry.HasNumber)
        return entry.Value;
    const bool gps = IsGps(entry);
    if (!gps && entry.Tag == FlashTag)
        return DescribeFlash(entry.Number);
    for (const ExifValueWord& value : ExifValueTexts)
    {
        if (value.Gps == gps && value.Tag == entry.Tag && value.Value == entry.Number)
            return ViewerText(value.TextId);
    }
    return entry.Value;
}

} // namespace pictview
