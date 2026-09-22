// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>

#include <windows.h>

bool EncodePeviewerReportText(std::wstring_view text, std::string& bytes) noexcept;

// Same boundary, degrading instead of refusing: every character the active code page
// cannot spell becomes '?', one for one, and the rest of the text survives.
//
// Use this wherever the alternative is publishing NOTHING. The report stream is that
// case: CFileStream::WriteWide latches a stream error on refusal, DumpFileInfo answers
// FALSE, and PEViewer throws away a report whose headers, sections, imports and exports
// were all produced correctly. Use the strict form above where a wrong identity would be
// worse than no identity.
bool EncodePeviewerReportTextLossy(std::wstring_view text, std::string& bytes) noexcept;

bool EncodePeviewerResourceName(std::wstring_view name, std::string& bytes) noexcept;
bool GetPeviewerLocaleLanguageText(LANGID language, std::string& text) noexcept;
bool FormatPeviewerTimestamp(DWORD timestamp, std::string& text) noexcept;
