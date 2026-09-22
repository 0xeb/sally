// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

// MIME header decoders normalize attachment names into the active Windows code-page byte
// domain. Project those bytes exactly once before publishing or using filesystem names.
bool DecodeUnmimeNameBytes(const std::string& bytes, std::wstring& name);
bool EqualUnmimeNameBytesAndWide(const std::string& bytes, const wchar_t* name);

// MIME charset/code-page identifiers are ASCII protocol tokens, not locale text.
bool DecodeUnmimeAsciiToken(const std::string& bytes, std::wstring& token);
bool EncodeUnmimeAsciiToken(const std::wstring& token, std::string& bytes);
