// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace checksum
{
void ParseCrcDigest(char* line, std::string& fileName, char* digest);
void ParseGenericDigest(char* line, int idLength, int digestLength,
                        std::string& fileName, char* digest);
}
