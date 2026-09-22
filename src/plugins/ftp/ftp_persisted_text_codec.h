// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

// RegEdit cannot import strings with embedded line endings. These helpers
// encode/decode the FTP plug-in's historical |/!/$/\\ persistence format.
// Dynamic outputs publish only a complete result.
bool FtpEncodePersistedText(const char* text, std::string& encoded) noexcept;
bool FtpDecodePersistedText(const char* encoded, std::string& text) noexcept;
