// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

struct CCSVConfig
{
    // 0: Auto-Select
    // 1: '\t'
    // 2: ';'
    // 3: ','
    // 4: ' '
    // 5: ValueSeparatorChar
    int ValueSeparator;
    char ValueSeparatorChar;

    // 0: Auto-Select
    // 1: '\"'
    // 2: '\''
    // 3: None
    int TextQualifier;

    // 0: Auto-Select
    // 1: As Column Name
    // 2: As Data
    int FirstRowAsName;
};

inline bool IsCsvSeparatorValid(const CCSVConfig& config)
{
    return config.ValueSeparator != 5 || config.ValueSeparatorChar != '\0';
}

inline CCSVConfig NormalizeCsvConfig(CCSVConfig config)
{
    // Recover older persisted Other-with-no-character settings by detecting
    // the actual file separator instead of treating NUL as a delimiter.
    if (!IsCsvSeparatorValid(config))
        config.ValueSeparator = 0;
    return config;
}
