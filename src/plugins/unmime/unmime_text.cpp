// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "unmime_text.h"

#include "common/Win32TextCodec.h"

bool DecodeUnmimeNameBytes(const std::string& bytes, std::wstring& name)
{
    // The attachment name the parser assembled is a byte string; a MIME header may declare its
    // charset, but plenty do not, and the decoder falls back to the active ANSI code page. Refusing
    // an ill-formed sequence aborted the listing or the unpack of the ENTIRE message with no message
    // at all, where pre-unicode simply showed the bytes. Degrade the name; keep the mail readable.
    // The strict DecodeUnmimeAsciiToken below still governs protocol tokens.
    return Win32DecodeAcpPermissive(bytes.data(), bytes.size(), name).Succeeded();
}

bool EqualUnmimeNameBytesAndWide(const std::string& bytes, const wchar_t* name)
{
    std::wstring decoded;
    return name != nullptr && DecodeUnmimeNameBytes(bytes, decoded) &&
           CompareStringOrdinal(decoded.c_str(), -1, name, -1, TRUE) == CSTR_EQUAL;
}

bool DecodeUnmimeAsciiToken(const std::string& bytes, std::wstring& token)
{
    std::wstring decoded;
    decoded.reserve(bytes.size());
    for (const unsigned char value : bytes)
    {
        if (value > 0x7f)
            return false;
        decoded.push_back(static_cast<wchar_t>(value));
    }
    token.swap(decoded);
    return true;
}

bool EncodeUnmimeAsciiToken(const std::wstring& token, std::string& bytes)
{
    std::string encoded;
    encoded.reserve(token.size());
    for (const wchar_t value : token)
    {
        if (value > 0x7f)
            return false;
        encoded.push_back(static_cast<char>(value));
    }
    bytes.swap(encoded);
    return true;
}
