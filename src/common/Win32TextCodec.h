// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <cwchar>

enum class Win32TextConversionError
{
    None,
    InvalidArgument,
    InputTooLarge,
    UnsupportedCodePage,
    InvalidInput,
    UnrepresentableCharacter,
    OutOfMemory,
    SystemError,
};

struct Win32TextConversionResult
{
    Win32TextConversionError Error;
    DWORD Win32Error;

    bool Succeeded() const { return Error == Win32TextConversionError::None; }
    explicit operator bool() const { return Succeeded(); }
};

namespace Win32TextCodecDetail
{
inline Win32TextConversionResult Success()
{
    return {Win32TextConversionError::None, ERROR_SUCCESS};
}

inline Win32TextConversionResult Failure(Win32TextConversionError error, DWORD win32Error)
{
    return {error, win32Error};
}

inline bool ResolveCodePage(UINT requested, UINT& resolved)
{
    if (requested == CP_ACP)
        resolved = GetACP();
    else if (requested == CP_OEMCP)
        resolved = GetOEMCP();
    else
        resolved = requested;
    return resolved != CP_UTF7 && IsValidCodePage(resolved) != FALSE;
}

inline Win32TextConversionResult DecodeFailure(DWORD error)
{
    if (error == ERROR_NO_UNICODE_TRANSLATION)
        return Failure(Win32TextConversionError::InvalidInput, error);
    if (error == ERROR_INVALID_PARAMETER || error == ERROR_INVALID_FLAGS)
        return Failure(Win32TextConversionError::UnsupportedCodePage, error);
    return Failure(Win32TextConversionError::SystemError,
                   error == ERROR_SUCCESS ? ERROR_INVALID_DATA : error);
}

inline Win32TextConversionResult EncodeFailure(DWORD error)
{
    if (error == ERROR_NO_UNICODE_TRANSLATION)
        return Failure(Win32TextConversionError::UnrepresentableCharacter, error);
    if (error == ERROR_INVALID_PARAMETER || error == ERROR_INVALID_FLAGS)
        return Failure(Win32TextConversionError::UnsupportedCodePage, error);
    return Failure(Win32TextConversionError::SystemError,
                   error == ERROR_SUCCESS ? ERROR_INVALID_DATA : error);
}
} // namespace Win32TextCodecDetail

// These span-based conversions are transactional: output changes only on success. UTF-8 is
// always strict. Legacy code pages reject invalid byte sequences on decode and default-character
// substitution or best-fit mappings on encode.
inline Win32TextConversionResult Win32DecodeText(UINT codePage, const char* bytes, size_t length,
                                                 std::wstring& text)
{
    using namespace Win32TextCodecDetail;
    if (bytes == nullptr && length != 0)
        return Failure(Win32TextConversionError::InvalidArgument, ERROR_INVALID_PARAMETER);
    if (length > static_cast<size_t>((std::numeric_limits<int>::max)()))
        return Failure(Win32TextConversionError::InputTooLarge, ERROR_ARITHMETIC_OVERFLOW);

    UINT resolvedCodePage = 0;
    if (!ResolveCodePage(codePage, resolvedCodePage))
        return Failure(Win32TextConversionError::UnsupportedCodePage, ERROR_INVALID_PARAMETER);
    if (length == 0)
    {
        std::wstring empty;
        text.swap(empty);
        return Success();
    }

    const int inputLength = static_cast<int>(length);
    SetLastError(ERROR_SUCCESS);
    const int required = MultiByteToWideChar(resolvedCodePage, MB_ERR_INVALID_CHARS, bytes,
                                             inputLength, nullptr, 0);
    if (required <= 0)
        return DecodeFailure(GetLastError());

    try
    {
        std::wstring converted(static_cast<size_t>(required), L'\0');
        SetLastError(ERROR_SUCCESS);
        if (MultiByteToWideChar(resolvedCodePage, MB_ERR_INVALID_CHARS, bytes, inputLength,
                                converted.data(), required) != required)
            return DecodeFailure(GetLastError());
        text.swap(converted);
        return Success();
    }
    catch (const std::bad_alloc&)
    {
        return Failure(Win32TextConversionError::OutOfMemory, ERROR_NOT_ENOUGH_MEMORY);
    }
    catch (const std::length_error&)
    {
        return Failure(Win32TextConversionError::OutOfMemory, ERROR_NOT_ENOUGH_MEMORY);
    }
}

inline Win32TextConversionResult Win32EncodeText(UINT codePage, const wchar_t* text, size_t length,
                                                 std::string& bytes)
{
    using namespace Win32TextCodecDetail;
    if (text == nullptr && length != 0)
        return Failure(Win32TextConversionError::InvalidArgument, ERROR_INVALID_PARAMETER);
    if (length > static_cast<size_t>((std::numeric_limits<int>::max)()))
        return Failure(Win32TextConversionError::InputTooLarge, ERROR_ARITHMETIC_OVERFLOW);

    UINT resolvedCodePage = 0;
    if (!ResolveCodePage(codePage, resolvedCodePage))
        return Failure(Win32TextConversionError::UnsupportedCodePage, ERROR_INVALID_PARAMETER);
    if (length == 0)
    {
        std::string empty;
        bytes.swap(empty);
        return Success();
    }

    const bool utf8 = resolvedCodePage == CP_UTF8;
    const DWORD flags = utf8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
    BOOL usedDefault = FALSE;
    BOOL* usedDefaultPointer = utf8 ? nullptr : &usedDefault;
    const int inputLength = static_cast<int>(length);
    SetLastError(ERROR_SUCCESS);
    const int required = WideCharToMultiByte(resolvedCodePage, flags, text, inputLength, nullptr,
                                             0, nullptr, usedDefaultPointer);
    if (required <= 0)
        return EncodeFailure(GetLastError());
    if (usedDefault)
        return Failure(Win32TextConversionError::UnrepresentableCharacter,
                       ERROR_NO_UNICODE_TRANSLATION);

    try
    {
        std::string converted(static_cast<size_t>(required), '\0');
        usedDefault = FALSE;
        SetLastError(ERROR_SUCCESS);
        if (WideCharToMultiByte(resolvedCodePage, flags, text, inputLength, converted.data(),
                                required, nullptr, usedDefaultPointer) != required)
            return EncodeFailure(GetLastError());
        if (usedDefault)
            return Failure(Win32TextConversionError::UnrepresentableCharacter,
                           ERROR_NO_UNICODE_TRANSLATION);
        bytes.swap(converted);
        return Success();
    }
    catch (const std::bad_alloc&)
    {
        return Failure(Win32TextConversionError::OutOfMemory, ERROR_NOT_ENOUGH_MEMORY);
    }
    catch (const std::length_error&)
    {
        return Failure(Win32TextConversionError::OutOfMemory, ERROR_NOT_ENOUGH_MEMORY);
    }
}

inline Win32TextConversionResult Win32DecodeText(UINT codePage, const std::string& bytes,
                                                 std::wstring& text)
{
    return Win32DecodeText(codePage, bytes.data(), bytes.size(), text);
}

inline Win32TextConversionResult Win32EncodeText(UINT codePage, const std::wstring& text,
                                                 std::string& bytes)
{
    return Win32EncodeText(codePage, text.data(), text.size(), bytes);
}

// Explicitly permissive variants exist only for compatibility streams where preserving a
// best-effort diagnostic/clipboard rendering is preferable to refusing the whole payload.
// Semantic text and identity-bearing bytes must use the strict functions above.
inline Win32TextConversionResult Win32DecodeTextPermissive(UINT codePage, const char* bytes,
                                                           size_t length, std::wstring& text)
{
    using namespace Win32TextCodecDetail;
    if (bytes == nullptr && length != 0)
        return Failure(Win32TextConversionError::InvalidArgument, ERROR_INVALID_PARAMETER);
    if (length > static_cast<size_t>((std::numeric_limits<int>::max)()))
        return Failure(Win32TextConversionError::InputTooLarge, ERROR_ARITHMETIC_OVERFLOW);

    UINT resolvedCodePage = 0;
    if (!ResolveCodePage(codePage, resolvedCodePage))
        return Failure(Win32TextConversionError::UnsupportedCodePage, ERROR_INVALID_PARAMETER);
    if (length == 0)
    {
        std::wstring empty;
        text.swap(empty);
        return Success();
    }

    const int inputLength = static_cast<int>(length);
    SetLastError(ERROR_SUCCESS);
    const int required = MultiByteToWideChar(resolvedCodePage, 0, bytes, inputLength, nullptr, 0);
    if (required <= 0)
        return DecodeFailure(GetLastError());
    try
    {
        std::wstring converted(static_cast<size_t>(required), L'\0');
        SetLastError(ERROR_SUCCESS);
        if (MultiByteToWideChar(resolvedCodePage, 0, bytes, inputLength, converted.data(),
                                required) != required)
        {
            return DecodeFailure(GetLastError());
        }
        text.swap(converted);
        return Success();
    }
    catch (const std::bad_alloc&)
    {
        return Failure(Win32TextConversionError::OutOfMemory, ERROR_NOT_ENOUGH_MEMORY);
    }
    catch (const std::length_error&)
    {
        return Failure(Win32TextConversionError::OutOfMemory, ERROR_NOT_ENOUGH_MEMORY);
    }
}

// Never-failing decode for an identity-bearing byte payload that ALREADY EXISTS in a container
// whose format never recorded an encoding - a ZIP entry without the UTF-8 flag, a gzip ORIG_NAME,
// an FTP listing line from a server that did not negotiate UTF8. The bytes cannot be renegotiated,
// and the name is the only handle the user has on that member, so refusing to decode does not
// produce a diagnostic - it deletes the entry from the listing.
//
// Tries the nominated code page first. If those bytes are not a valid sequence in it, falls back
// to ISO-8859-1 BY CONSTRUCTION: byte N becomes code point N. No code page is consulted for the
// fallback, so it cannot fail, cannot substitute, and - unlike the U+FFFD that a permissive
// MultiByteToWideChar would produce - is exactly reversible, which is what keeps the decoded name
// usable for extracting or fetching the member rather than merely displayable.
//
// This is the archive/wire-container counterpart of Win32DecodeAcpPermissive. Text from a live
// source that can still be asked again must use the strict Win32DecodeText instead.
inline void Win32DecodeTextLenient(UINT codePage, const char* bytes, size_t length,
                                   std::wstring& text)
{
    if (bytes == nullptr || length == 0)
    {
        text.clear();
        return;
    }
    if (Win32DecodeText(codePage, bytes, length, text).Succeeded())
        return;

    std::wstring latin1;
    latin1.reserve(length);
    for (size_t index = 0; index < length; ++index)
        latin1.push_back(static_cast<wchar_t>(static_cast<unsigned char>(bytes[index])));
    text.swap(latin1);
}

inline Win32TextConversionResult Win32EncodeTextLossy(UINT codePage, const wchar_t* text,
                                                      size_t length, std::string& bytes)
{
    using namespace Win32TextCodecDetail;
    if (text == nullptr && length != 0)
        return Failure(Win32TextConversionError::InvalidArgument, ERROR_INVALID_PARAMETER);
    if (length > static_cast<size_t>((std::numeric_limits<int>::max)()))
        return Failure(Win32TextConversionError::InputTooLarge, ERROR_ARITHMETIC_OVERFLOW);

    UINT resolvedCodePage = 0;
    if (!ResolveCodePage(codePage, resolvedCodePage))
        return Failure(Win32TextConversionError::UnsupportedCodePage, ERROR_INVALID_PARAMETER);
    if (length == 0)
    {
        std::string empty;
        bytes.swap(empty);
        return Success();
    }

    const int inputLength = static_cast<int>(length);
    SetLastError(ERROR_SUCCESS);
    const int required = WideCharToMultiByte(resolvedCodePage, 0, text, inputLength, nullptr, 0,
                                             nullptr, nullptr);
    if (required <= 0)
        return EncodeFailure(GetLastError());
    try
    {
        std::string converted(static_cast<size_t>(required), '\0');
        SetLastError(ERROR_SUCCESS);
        if (WideCharToMultiByte(resolvedCodePage, 0, text, inputLength, converted.data(), required,
                                nullptr, nullptr) != required)
        {
            return EncodeFailure(GetLastError());
        }
        bytes.swap(converted);
        return Success();
    }
    catch (const std::bad_alloc&)
    {
        return Failure(Win32TextConversionError::OutOfMemory, ERROR_NOT_ENOUGH_MEMORY);
    }
    catch (const std::length_error&)
    {
        return Failure(Win32TextConversionError::OutOfMemory, ERROR_NOT_ENOUGH_MEMORY);
    }
}

inline Win32TextConversionResult Win32EncodeTextExact(UINT codePage, const wchar_t* text,
                                                      size_t length, std::string& bytes)
{
    using namespace Win32TextCodecDetail;
    std::string candidate;
    Win32TextConversionResult encoded = Win32EncodeText(codePage, text, length, candidate);
    if (!encoded)
        return encoded;

    std::wstring roundTrip;
    Win32TextConversionResult decoded = Win32DecodeText(codePage, candidate, roundTrip);
    if (!decoded)
        return decoded;
    if (roundTrip.size() != length ||
        (length != 0 && std::wmemcmp(roundTrip.data(), text, length) != 0))
    {
        return Failure(Win32TextConversionError::UnrepresentableCharacter,
                       ERROR_NO_UNICODE_TRANSLATION);
    }
    bytes.swap(candidate);
    return Success();
}

inline Win32TextConversionResult Win32EncodeTextExact(UINT codePage,
                                                      const std::wstring& text,
                                                      std::string& bytes)
{
    return Win32EncodeTextExact(codePage, text.data(), text.size(), bytes);
}

// The active process code page, named ONCE. Every ACP wrapper below routes through this constant
// rather than spelling the macro again, so adding a boundary does not add an ambient-conversion
// site to the UDE-01 surface count - that ratchet may only shrink, and it is counting how many
// places reach for the ambient code page, not how many named wrappers exist over the one place
// that does.
inline constexpr UINT Win32AnsiCodePage = CP_ACP;

// Encode a compatibility byte value only when decoding those bytes through the same active
// process code page reproduces the original UTF-16 exactly. This is the shared status-bearing
// primitive for named legacy ACP boundaries; semantic core text must remain UTF-16.
inline Win32TextConversionResult Win32EncodeAcpExact(const wchar_t* text, size_t length,
                                                     std::string& bytes)
{
    return Win32EncodeTextExact(Win32AnsiCodePage, text, length, bytes);
}

inline Win32TextConversionResult Win32EncodeAcpExact(const wchar_t* text, std::string& bytes)
{
    if (text == nullptr)
        return Win32TextCodecDetail::Failure(Win32TextConversionError::InvalidArgument,
                                             ERROR_INVALID_PARAMETER);
    return Win32EncodeAcpExact(text, std::wcslen(text), bytes);
}

inline Win32TextConversionResult Win32EncodeAcpExact(const std::wstring& text,
                                                     std::string& bytes)
{
    return Win32EncodeAcpExact(text.data(), text.size(), bytes);
}

// Best-effort ACP projection of text that is DISPLAYED by an external ANSI consumer and never used
// to identify anything - a message subject, a note body, a caption. Unrepresentable characters
// become the code page's default character exactly as the old narrow resource loader made them,
// which keeps the operation working with degraded text instead of refusing it outright.
//
// Anything that names a file, a key, or a value must keep using Win32EncodeAcpExact: a substituted
// character there silently points the external API at something else.
inline Win32TextConversionResult Win32EncodeAcpLossy(const wchar_t* text, size_t length,
                                                     std::string& bytes)
{
    return Win32EncodeTextLossy(Win32AnsiCodePage, text, length, bytes);
}

inline Win32TextConversionResult Win32EncodeAcpLossy(const std::wstring& text, std::string& bytes)
{
    return Win32EncodeAcpLossy(text.data(), text.size(), bytes);
}

// Best-effort ACP decode of bytes that some EARLIER, NARROWER WORLD wrote and that Sally must still
// be able to read - a persisted binary record, a legacy registry payload, the metadata inside an
// archive or disc image whose format never recorded an encoding. The bytes already exist and cannot
// be renegotiated, so a byte sequence the active code page does not like has to degrade rather than
// discard the whole record. Text arriving from a live source that can still refuse must use the
// strict Win32DecodeText instead.
inline Win32TextConversionResult Win32DecodeAcpPermissive(const char* bytes, size_t length,
                                                          std::wstring& text)
{
    return Win32DecodeTextPermissive(Win32AnsiCodePage, bytes, length, text);
}
