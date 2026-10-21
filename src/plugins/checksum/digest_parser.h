// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>

typedef enum eHASH_TYPE
{
    HT_CRC,
    HT_MD5,
    HT_SHA1,
    HT_SHA256,
    HT_SHA512,
    HT_COUNT // Not a hash type but # of known hash types
} eHASH_TYPE;

namespace checksum
{
    // Content must match every data row. A recognized suffix only breaks valid ties;
    // unknown/no suffix retains the historical SFV-first fallback.
    eHASH_TYPE AnalyzeDigestFormat(std::string_view text, eHASH_TYPE preferred = HT_COUNT);

    bool IsDigestText(eHASH_TYPE type, std::wstring_view text);

    void ParseCrcDigest(char* line, std::string& fileName, char* digest);
    void ParseGenericDigest(char* line, int idLength, int digestLength,
                            std::string& fileName, char* digest);
}
