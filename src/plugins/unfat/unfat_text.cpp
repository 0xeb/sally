// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "unfat_text.h"

#include "common/Win32TextCodec.h"

#include <cstring>

bool DecodeFatOemName(const char* bytes, std::wstring& name)
{
    // The 8.3 name is raw on-disk FAT bytes in the OEM code page, and the format itself plants
    // sequences no code page can pair up - ConvertFATName rewrites a leading 0x05 back to 0xE5,
    // which is a lead byte on a DBCS OEM code page and is routinely followed by plain ASCII.
    // Refusing aborted the listing of the WHOLE image over one cosmetic DOS-name column, even
    // where the VFAT long name beside it had already been read losslessly. Degrade the name.
    // A null pointer is still refused: that is a caller bug, not disc content.
    return bytes != nullptr &&
           Win32DecodeTextPermissive(CP_OEMCP, bytes, std::strlen(bytes), name).Succeeded();
}
