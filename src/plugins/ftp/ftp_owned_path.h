// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>

// Updates the remembered directory from a selected filesystem path. The
// existing value is preserved when no directory is present or allocation fails.
bool FtpRememberSelectedDirectory(std::wstring_view fileName,
                                  std::wstring& initDir) noexcept;

// Parses the decimal run at the end of a remote autorename stem. Values that
// cannot be incremented as a signed int are rejected transactionally.
bool FtpParseTrailingPositiveIndex(std::string_view stem,
                                   size_t& digitsBegin, int& value) noexcept;
