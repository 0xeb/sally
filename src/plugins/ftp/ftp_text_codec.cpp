// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ftp_text_codec.h"

#include "../../common/Win32TextCodec.h"

#include <cstring>
#include <cwchar>
#include <limits>

namespace
{
constexpr DWORD NoNetworkAddress = 0xffffffffUL;

bool IsSafeAsciiHost(std::string_view host) noexcept
{
    if (host.empty() || host.size() > 255)
        return false;
    for (unsigned char ch : host)
    {
        if (ch <= 0x20 || ch >= 0x7f)
            return false;
    }
    return true;
}

void AppendNetworkWord(std::string& bytes, unsigned short value)
{
    bytes.push_back(static_cast<char>(value >> 8));
    bytes.push_back(static_cast<char>(value & 0xff));
}

void AppendNetworkAddress(std::string& bytes, DWORD value)
{
    bytes.push_back(static_cast<char>(value & 0xff));
    bytes.push_back(static_cast<char>((value >> 8) & 0xff));
    bytes.push_back(static_cast<char>((value >> 16) & 0xff));
    bytes.push_back(static_cast<char>((value >> 24) & 0xff));
}

void WipeBytes(std::string& bytes) noexcept
{
    if (!bytes.empty())
        SecureZeroMemory(bytes.data(), bytes.size());
    bytes.clear();
}

DWORD ProxyCredentialConversionError(const Win32TextConversionResult& result) noexcept
{
    switch (result.Error)
    {
    case Win32TextConversionError::OutOfMemory:
        return ERROR_NOT_ENOUGH_MEMORY;
    case Win32TextConversionError::InputTooLarge:
        return ERROR_ARITHMETIC_OVERFLOW;
    case Win32TextConversionError::UnrepresentableCharacter:
    case Win32TextConversionError::InvalidInput:
        return ERROR_NO_UNICODE_TRANSLATION;
    default:
        return result.Win32Error != ERROR_SUCCESS ? result.Win32Error : ERROR_INVALID_DATA;
    }
}

BOOL EncodeBase64(std::string_view bytes, std::string& encoded) noexcept
{
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    try
    {
        if (bytes.size() > ((std::numeric_limits<size_t>::max)() - 2) / 3)
            return FALSE;
        const size_t groups = (bytes.size() + 2) / 3;
        if (groups > (std::numeric_limits<size_t>::max)() / 4)
            return FALSE;

        std::string staged(groups * 4, '=');
        size_t source = 0;
        size_t target = 0;
        while (source + 3 <= bytes.size())
        {
            const unsigned first = static_cast<unsigned char>(bytes[source++]);
            const unsigned second = static_cast<unsigned char>(bytes[source++]);
            const unsigned third = static_cast<unsigned char>(bytes[source++]);
            staged[target++] = table[first >> 2];
            staged[target++] = table[((first & 0x03) << 4) | (second >> 4)];
            staged[target++] = table[((second & 0x0f) << 2) | (third >> 6)];
            staged[target++] = table[third & 0x3f];
        }
        if (source < bytes.size())
        {
            const unsigned first = static_cast<unsigned char>(bytes[source++]);
            staged[target++] = table[first >> 2];
            if (source < bytes.size())
            {
                const unsigned second = static_cast<unsigned char>(bytes[source]);
                staged[target++] = table[((first & 0x03) << 4) | (second >> 4)];
                staged[target] = table[(second & 0x0f) << 2];
            }
            else
                staged[target] = table[(first & 0x03) << 4];
        }
        encoded.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}
} // namespace

static void AppendServerTypeColumnField(std::string& record, const char* value, BOOL addComma)
{
    if (value == NULL)
        record += "\\0";
    else
    {
        while (*value != 0)
        {
            if (*value == ',' || *value == '\\')
                record.push_back('\\');
            record.push_back(*value++);
        }
    }
    if (addComma)
        record.push_back(',');
}

BOOL FtpSerializeServerTypeColumnRecord(BOOL visible, const char* id, int nameID,
                                        const char* name, int descriptionID,
                                        const char* description, int type,
                                        const char* emptyValue, BOOL leftAlignment,
                                        DWORD fixedWidth, int width, BOOL ignoreWidths,
                                        std::string& record) noexcept
{
    try
    {
        std::string staged;
        staged.push_back(visible ? '1' : '0');
        staged.push_back(',');
        AppendServerTypeColumnField(staged, id, TRUE);
        staged += std::to_string(nameID);
        staged.push_back(',');
        AppendServerTypeColumnField(staged, name, TRUE);
        staged += std::to_string(descriptionID);
        staged.push_back(',');
        AppendServerTypeColumnField(staged, description, TRUE);
        staged += std::to_string(type);
        staged.push_back(',');
        AppendServerTypeColumnField(staged, emptyValue, FALSE);
        staged.push_back(',');
        staged.push_back(leftAlignment ? '1' : '0');
        if (!ignoreWidths)
        {
            staged.push_back(',');
            staged.push_back('0' + (LOWORD(fixedWidth) != 0 ? 1 : 0) +
                             (HIWORD(fixedWidth) != 0 ? 2 : 0));
            staged.push_back(',');
            staged += std::to_string(width);
        }
        record.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL FtpDecodeLocalText(const char* bytes, std::wstring& text) noexcept
{
    const char* value = bytes != NULL ? bytes : "";
    return FtpDecodeLocalText(std::string_view(value, strlen(value)), text);
}

UINT FtpLocalTextCodePage() noexcept
{
    return GetACP();
}

CFtpTextCodec FtpLocalTextCodec() noexcept
{
    return CFtpTextCodec(FALSE, FtpLocalTextCodePage());
}

CFtpTextCodec FtpUtf8TextCodec() noexcept
{
    return CFtpTextCodec(TRUE, CP_UTF8);
}

BOOL FtpDecodeLocalText(std::string_view bytes, std::wstring& text) noexcept
{
    return Win32DecodeText(CP_ACP, bytes.empty() ? "" : bytes.data(),
                           bytes.size(), text).Succeeded();
}

BOOL FtpEncodeLocalText(const wchar_t* text, std::string& bytes) noexcept
{
    const wchar_t* value = text != NULL ? text : L"";
    return Win32EncodeText(CP_ACP, value, wcslen(value), bytes).Succeeded();
}

BOOL FtpEncodeLocalTextForByteLog(const wchar_t* text, std::string_view fallback,
                                  std::string& bytes) noexcept
{
    std::string staged;
    if (!FtpEncodeLocalText(text, staged) && !FtpStoreLocalTextBytes(fallback, staged))
        return FALSE;
    bytes.swap(staged);
    return TRUE;
}

BOOL FtpLocalByteOffsetToUtf16(std::string_view bytes, size_t byteOffset,
                               size_t& utf16Offset) noexcept
{
    if (byteOffset > bytes.size())
        return FALSE;
    std::wstring prefix;
    if (!FtpDecodeLocalText(bytes.substr(0, byteOffset), prefix))
        return FALSE;
    utf16Offset = prefix.size();
    return TRUE;
}

void FtpTrimIncompleteListingBytes(std::string& listing) noexcept
{
    const size_t lastLineEnd = listing.find_last_of('\n');
    const size_t retained = lastLineEnd == std::string::npos ? 0 : lastLineEnd + 1;
    if (retained < listing.size())
        SecureZeroMemory(listing.data() + retained, listing.size() - retained);
    listing.resize(retained);
}

BOOL FtpEncodeNetworkHost(const wchar_t* host, std::string& asciiHost) noexcept
{
    if (host == NULL || *host == 0)
        return FALSE;
    try
    {
        const size_t length = wcslen(host);
        bool asciiOnly = true;
        for (size_t i = 0; i < length; i++)
        {
            if (host[i] > 0x7f)
            {
                asciiOnly = false;
                break;
            }
        }
        if (asciiOnly)
        {
            if (length > 255) // DNS wire-format maximum; refuse instead of truncating
                return FALSE;
            std::string staged;
            staged.reserve(length);
            for (size_t i = 0; i < length; i++)
            {
                if (host[i] <= 0x20 || host[i] >= 0x7f)
                    return FALSE;
                staged.push_back(static_cast<char>(host[i]));
            }
            asciiHost.swap(staged);
            return TRUE;
        }
        if (length > static_cast<size_t>((std::numeric_limits<int>::max)()))
            return FALSE;
        const int required = IdnToAscii(IDN_USE_STD3_ASCII_RULES, host,
                                        static_cast<int>(length), NULL, 0);
        if (required <= 0)
            return FALSE;
        std::wstring idn(static_cast<size_t>(required), L'\0');
        if (IdnToAscii(IDN_USE_STD3_ASCII_RULES, host, static_cast<int>(length),
                       idn.data(), required) != required)
            return FALSE;
        if (idn.size() > 255)
            return FALSE;
        std::string staged;
        staged.reserve(idn.size());
        for (wchar_t ch : idn)
        {
            if (ch > 0x7f)
                return FALSE;
            staged.push_back(static_cast<char>(ch));
        }
        asciiHost.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL FtpEncodeProxyCredentials(const wchar_t* user, const wchar_t* password,
                               BOOL enforceSocks5Limit,
                               std::string& userBytes,
                               std::string& passwordBytes,
                               DWORD& error) noexcept
{
    const wchar_t* normalizedUser = user != NULL ? user : L"";
    const wchar_t* normalizedPassword = password != NULL ? password : L"";
    std::string stagedUser;
    std::string stagedPassword;

    const Win32TextConversionResult userResult = Win32EncodeText(
        CP_ACP, normalizedUser, wcslen(normalizedUser), stagedUser);
    if (!userResult.Succeeded())
    {
        error = ProxyCredentialConversionError(userResult);
        return FALSE;
    }
    const Win32TextConversionResult passwordResult = Win32EncodeText(
        CP_ACP, normalizedPassword, wcslen(normalizedPassword), stagedPassword);
    if (!passwordResult.Succeeded())
    {
        WipeBytes(stagedPassword);
        error = ProxyCredentialConversionError(passwordResult);
        return FALSE;
    }
    if (enforceSocks5Limit &&
        (stagedUser.size() > 255 || stagedPassword.size() > 255))
    {
        WipeBytes(stagedPassword);
        error = ERROR_INVALID_DATA;
        return FALSE;
    }

    userBytes.swap(stagedUser);
    passwordBytes.swap(stagedPassword);
    WipeBytes(stagedPassword);
    error = NO_ERROR;
    return TRUE;
}

BOOL FtpBuildSocks4Request(BYTE request, DWORD hostIP, unsigned short hostPort,
                           std::string_view asciiHost, std::string_view user,
                           BOOL socks4A, std::string& requestBytes) noexcept
{
    const bool sendHost = socks4A && hostIP == NoNetworkAddress;
    if (sendHost && !IsSafeAsciiHost(asciiHost))
        return FALSE;
    try
    {
        const size_t hostSize = sendHost ? asciiHost.size() + 1 : 0;
        if (user.size() > (std::numeric_limits<size_t>::max)() - 9 - hostSize)
            return FALSE;
        const size_t requestSize = 9 + user.size() + hostSize;
        if (requestSize > static_cast<size_t>((std::numeric_limits<int>::max)()))
            return FALSE;

        std::string staged;
        staged.reserve(requestSize);
        staged.push_back(4);
        staged.push_back(static_cast<char>(request));
        AppendNetworkWord(staged, hostPort);
        AppendNetworkAddress(staged, sendHost ? 0x01000000 : hostIP);
        if (!user.empty())
            staged.append(user.data(), user.size());
        staged.push_back('\0');
        if (sendHost)
        {
            staged.append(asciiHost.data(), asciiHost.size());
            staged.push_back('\0');
        }
        requestBytes.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL FtpBuildSocks5Login(std::string_view user, std::string_view password,
                         std::string& requestBytes) noexcept
{
    if (user.size() > 255 || password.size() > 255)
        return FALSE;
    try
    {
        std::string staged;
        staged.reserve(3 + user.size() + password.size());
        staged.push_back(1);
        staged.push_back(static_cast<char>(user.size()));
        if (!user.empty())
            staged.append(user.data(), user.size());
        staged.push_back(static_cast<char>(password.size()));
        if (!password.empty())
            staged.append(password.data(), password.size());
        requestBytes.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL FtpBuildSocks5Request(BYTE request, DWORD hostIP, unsigned short hostPort,
                           std::string_view asciiHost, std::string& requestBytes) noexcept
{
    const bool sendHost = hostIP == NoNetworkAddress;
    if (sendHost && !IsSafeAsciiHost(asciiHost))
        return FALSE;
    try
    {
        std::string staged;
        staged.reserve(sendHost ? 7 + asciiHost.size() : 10);
        staged.push_back(5);
        staged.push_back(static_cast<char>(request));
        staged.push_back(0);
        staged.push_back(sendHost ? 3 : 1);
        if (sendHost)
        {
            staged.push_back(static_cast<char>(asciiHost.size()));
            staged.append(asciiHost.data(), asciiHost.size());
        }
        else
            AppendNetworkAddress(staged, hostIP);
        AppendNetworkWord(staged, hostPort);
        requestBytes.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL FtpBuildHttpConnectRequest(std::string_view asciiHost, unsigned short hostPort,
                                std::string_view user, std::string_view password,
                                BOOL includeCredentials, std::string& requestBytes) noexcept
{
    if (!IsSafeAsciiHost(asciiHost))
        return FALSE;
    std::string login;
    std::string encodedLogin;
    std::string authorization;
    std::string staged;
    try
    {
        std::string port = std::to_string(hostPort);
        if (includeCredentials)
        {
            if (user.size() > (std::numeric_limits<size_t>::max)() - password.size() - 1)
                return FALSE;
            login.reserve(user.size() + password.size() + 1);
            if (!user.empty())
                login.append(user.data(), user.size());
            login.push_back(':');
            if (!password.empty())
                login.append(password.data(), password.size());
            if (!EncodeBase64(login, encodedLogin))
            {
                WipeBytes(login);
                return FALSE;
            }
            authorization.reserve(encodedLogin.size() * 2 + 64);
            authorization.append("Authorization: Basic ");
            authorization.append(encodedLogin);
            authorization.append("\r\nProxy-Authorization: Basic ");
            authorization.append(encodedLogin);
            authorization.append("\r\n");
        }

        staged.reserve(asciiHost.size() * 2 + port.size() * 2 + authorization.size() + 40);
        staged.append("CONNECT ");
        staged.append(asciiHost.data(), asciiHost.size());
        staged.push_back(':');
        staged.append(port);
        staged.append(" HTTP/1.1\r\nHost: ");
        staged.append(asciiHost.data(), asciiHost.size());
        staged.push_back(':');
        staged.append(port);
        staged.append("\r\n");
        staged.append(authorization);
        staged.append("\r\n");
        if (staged.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        {
            WipeBytes(login);
            WipeBytes(encodedLogin);
            WipeBytes(authorization);
            WipeBytes(staged);
            return FALSE;
        }
        requestBytes.swap(staged);
        WipeBytes(login);
        WipeBytes(encodedLogin);
        WipeBytes(authorization);
        return TRUE;
    }
    catch (...)
    {
        WipeBytes(login);
        WipeBytes(encodedLogin);
        WipeBytes(authorization);
        WipeBytes(staged);
        return FALSE;
    }
}

BOOL FtpStoreProtocolBytes(std::string_view bytes, std::string& output) noexcept
{
    try
    {
        std::string staged(bytes);
        output.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL FtpStoreLocalTextBytes(std::string_view bytes, std::string& output) noexcept
{
    return FtpStoreProtocolBytes(bytes, output);
}

BOOL FtpEqualLocalTextNoCase(std::string_view first, std::string_view second) noexcept
{
    return FtpCompareLocalTextNoCase(first, second) == CFtpTextCompareStatus::Equal;
}

CFtpTextCompareStatus FtpCompareWideText(std::wstring_view first,
                                         std::wstring_view second,
                                         BOOL ignoreCase) noexcept
{
    if (first.size() > static_cast<size_t>(INT_MAX) ||
        second.size() > static_cast<size_t>(INT_MAX))
        return CFtpTextCompareStatus::Failure;
    const int result = CompareStringOrdinal(
        first.empty() ? L"" : first.data(), static_cast<int>(first.size()),
        second.empty() ? L"" : second.data(), static_cast<int>(second.size()),
        ignoreCase);
    if (result == 0)
        return CFtpTextCompareStatus::Failure;
    return result == CSTR_EQUAL ? CFtpTextCompareStatus::Equal
                               : CFtpTextCompareStatus::NotEqual;
}

CFtpTextCompareStatus FtpContainsWideTextNoCase(std::wstring_view text,
                                                std::wstring_view sample) noexcept
{
    if (sample.empty())
        return CFtpTextCompareStatus::Equal;
    if (sample.size() > text.size())
        return CFtpTextCompareStatus::NotEqual;
    for (size_t offset = 0; offset <= text.size() - sample.size(); ++offset)
    {
        const CFtpTextCompareStatus comparison = FtpCompareWideText(
            text.substr(offset, sample.size()), sample, TRUE);
        if (comparison == CFtpTextCompareStatus::Equal ||
            comparison == CFtpTextCompareStatus::Failure)
            return comparison;
    }
    return CFtpTextCompareStatus::NotEqual;
}

CFtpTextCompareStatus FtpEndsWithWideTextNoCase(std::wstring_view text,
                                                std::wstring_view suffix) noexcept
{
    if (suffix.size() > text.size())
        return CFtpTextCompareStatus::NotEqual;
    return FtpCompareWideText(text.substr(text.size() - suffix.size()), suffix, TRUE);
}

CFtpTextCompareStatus FtpMatchEncodedPrefixNoCase(const CFtpTextCodec& codec,
                                                   std::string_view bytes,
                                                   std::wstring_view prefix,
                                                   size_t& matchedBytes) noexcept
{
    if (prefix.empty())
    {
        matchedBytes = 0;
        return CFtpTextCompareStatus::Equal;
    }
    if (bytes.empty())
        return CFtpTextCompareStatus::NotEqual;

    std::wstring decoded;
    bool decodedAny = false;
    for (size_t length = 1; length <= bytes.size(); ++length)
    {
        const CFtpTextDecodeStatus status = codec.DecodeForComparison(
            bytes.data(), length, decoded);
        if (status == CFtpTextDecodeStatus::InvalidInput)
            continue;
        if (status == CFtpTextDecodeStatus::Failure)
            return CFtpTextCompareStatus::Failure;
        decodedAny = true;
        if (decoded.size() < prefix.size())
            continue;
        if (decoded.size() > prefix.size())
            return CFtpTextCompareStatus::NotEqual;

        const CFtpTextCompareStatus comparison =
            FtpCompareWideText(decoded, prefix, TRUE);
        if (comparison == CFtpTextCompareStatus::Equal)
            matchedBytes = length;
        return comparison;
    }
    return decodedAny ? CFtpTextCompareStatus::NotEqual
                      : CFtpTextCompareStatus::Failure;
}

static BOOL IsSingleUnicodeCharacter(std::wstring_view text) noexcept
{
    if (text.size() == 1)
        return text.front() < 0xD800 || text.front() > 0xDFFF;
    return text.size() == 2 &&
           text[0] >= 0xD800 && text[0] <= 0xDBFF &&
           text[1] >= 0xDC00 && text[1] <= 0xDFFF;
}

BOOL FtpEncodedCharacterByteLength(const CFtpTextCodec& codec,
                                   std::string_view bytes,
                                   size_t& characterBytes) noexcept
{
    if (bytes.empty())
        return FALSE;

    std::wstring decoded;
    for (size_t length = 1; length <= bytes.size(); ++length)
    {
        const CFtpTextDecodeStatus status = codec.DecodeForComparison(
            bytes.data(), length, decoded);
        if (status == CFtpTextDecodeStatus::InvalidInput)
            continue;
        if (status == CFtpTextDecodeStatus::Failure ||
            !IsSingleUnicodeCharacter(decoded))
            return FALSE;
        characterBytes = length;
        return TRUE;
    }
    return FALSE;
}

BOOL FtpAdvanceEncodedCharacters(const CFtpTextCodec& codec,
                                 std::string_view bytes,
                                 size_t characterCount,
                                 size_t& consumedBytes) noexcept
{
    size_t offset = 0;
    for (size_t character = 0; character < characterCount; ++character)
    {
        size_t length = 0;
        if (!FtpEncodedCharacterByteLength(codec, bytes.substr(offset), length))
            return FALSE;
        offset += length;
    }
    consumedBytes = offset;
    return TRUE;
}

BOOL FtpRetreatEncodedCharacters(const CFtpTextCodec& codec,
                                 std::string_view bytesBeforeCursor,
                                 size_t characterCount,
                                 size_t& remainingBytes) noexcept
{
    if (characterCount == 0)
    {
        remainingBytes = bytesBeforeCursor.size();
        return TRUE;
    }

    size_t offset = 0;
    size_t totalCharacters = 0;
    while (offset < bytesBeforeCursor.size())
    {
        size_t length = 0;
        if (!FtpEncodedCharacterByteLength(codec, bytesBeforeCursor.substr(offset), length))
            return FALSE;
        offset += length;
        ++totalCharacters;
    }
    if (totalCharacters < characterCount)
        return FALSE;
    return FtpAdvanceEncodedCharacters(codec, bytesBeforeCursor,
                                       totalCharacters - characterCount,
                                       remainingBytes);
}

CFtpTextCompareStatus FtpFindEncodedTextNoCase(const CFtpTextCodec& codec,
                                               std::string_view bytes,
                                               std::wstring_view sample,
                                               size_t& matchOffset,
                                               size_t& matchBytes) noexcept
{
    if (sample.empty())
    {
        matchOffset = 0;
        matchBytes = 0;
        return CFtpTextCompareStatus::Equal;
    }

    size_t offset = 0;
    while (offset < bytes.size())
    {
        size_t candidateBytes = 0;
        const CFtpTextCompareStatus comparison = FtpMatchEncodedPrefixNoCase(
            codec, bytes.substr(offset), sample, candidateBytes);
        if (comparison == CFtpTextCompareStatus::Equal)
        {
            matchOffset = offset;
            matchBytes = candidateBytes;
            return comparison;
        }
        if (comparison == CFtpTextCompareStatus::Failure)
            return comparison;

        size_t characterBytes = 0;
        if (!FtpEncodedCharacterByteLength(codec, bytes.substr(offset), characterBytes))
            return CFtpTextCompareStatus::Failure;
        offset += characterBytes;
    }
    return CFtpTextCompareStatus::NotEqual;
}

static BOOL FtpEncodedTextStartsWithCharacterClass(const CFtpTextCodec& codec,
                                                   std::string_view bytes,
                                                   BOOL alphaNumeric,
                                                   BOOL& matches) noexcept
{
    if (bytes.empty())
    {
        matches = FALSE;
        return TRUE;
    }

    size_t characterBytes = 0;
    if (!FtpEncodedCharacterByteLength(codec, bytes, characterBytes))
        return FALSE;

    std::wstring decoded;
    if (!codec.Decode(bytes.data(), characterBytes, decoded) || decoded.empty())
        return FALSE;
    matches = alphaNumeric ? IsCharAlphaNumericW(decoded.front())
                           : IsCharAlphaW(decoded.front());
    return TRUE;
}

BOOL FtpEncodedTextStartsWithAlpha(const CFtpTextCodec& codec,
                                   std::string_view bytes,
                                   BOOL& startsWithAlpha) noexcept
{
    return FtpEncodedTextStartsWithCharacterClass(codec, bytes, FALSE,
                                                  startsWithAlpha);
}

BOOL FtpEncodedTextStartsWithAlphaNumeric(const CFtpTextCodec& codec,
                                          std::string_view bytes,
                                          BOOL& startsWithAlphaNumeric) noexcept
{
    return FtpEncodedTextStartsWithCharacterClass(codec, bytes, TRUE,
                                                  startsWithAlphaNumeric);
}

BOOL FtpEqualAsciiTokenNoCase(std::string_view first,
                              std::string_view second) noexcept
{
    if (first.size() != second.size())
        return FALSE;
    for (size_t i = 0; i < first.size(); ++i)
    {
        unsigned char left = static_cast<unsigned char>(first[i]);
        unsigned char right = static_cast<unsigned char>(second[i]);
        if (left >= 'A' && left <= 'Z')
            left = static_cast<unsigned char>(left + ('a' - 'A'));
        if (right >= 'A' && right <= 'Z')
            right = static_cast<unsigned char>(right + ('a' - 'A'));
        if (left != right)
            return FALSE;
    }
    return TRUE;
}

CFtpTextCompareStatus FtpCompareLocalTextNoCase(std::string_view first,
                                                std::string_view second) noexcept
{
    std::wstring firstText;
    std::wstring secondText;
    if (!FtpDecodeLocalText(first, firstText) || !FtpDecodeLocalText(second, secondText))
        return CFtpTextCompareStatus::Failure;
    return FtpCompareWideText(firstText, secondText, TRUE);
}

BOOL FtpFormatLocalTypeName(std::string_view encodedName, std::wstring_view userDefinedSuffix,
                            std::wstring& text) noexcept
{
    try
    {
        const BOOL userDefined = !encodedName.empty() && encodedName.front() == '*';
        if (userDefined)
            encodedName.remove_prefix(1);
        std::wstring staged;
        if (!FtpDecodeLocalText(encodedName, staged))
            return FALSE;
        if (userDefined)
        {
            staged.push_back(L' ');
            staged.append(userDefinedSuffix);
        }
        text.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL FtpStoreWideText(std::wstring_view text, std::wstring& output) noexcept
{
    try
    {
        std::wstring staged(text);
        output.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL FtpStoreReplyTextBytes(std::string_view bytes, std::string& output) noexcept
{
    try
    {
        std::string staged;
        staged.reserve(bytes.size());
        for (size_t i = 0; i < bytes.size() && bytes[i] != '\0'; ++i)
        {
            if (bytes[i] == '\n' && (i == 0 || bytes[i - 1] != '\r'))
                staged.push_back('\r');
            staged.push_back(bytes[i]);
        }
        output.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL FtpNormalizeLogLine(std::string& text) noexcept
{
    try
    {
        std::string staged(text);
        while (!staged.empty() && (staged.back() == '\n' || staged.back() == '\r'))
            staged.pop_back();
        staged.append("\r\n");
        text.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

void FtpNormalizeWorkerError(std::wstring& text) noexcept
{
    for (wchar_t& ch : text)
    {
        if (ch == L'\r' || ch == L'\n')
            ch = L' ';
    }
    while (!text.empty() && (text.back() == L'.' || text.back() == L' '))
        text.pop_back();
}

BOOL FtpFormatWideText(const wchar_t* format, std::wstring_view argument,
                       std::wstring& text) noexcept
{
    if (format == NULL)
        return FALSE;
    try
    {
        const std::wstring value(argument);
        const int length = _scwprintf(format, value.c_str());
        if (length < 0)
            return FALSE;
        std::wstring staged(static_cast<size_t>(length) + 1, L'\0');
        const int written = _snwprintf_s(staged.data(), staged.size(), _TRUNCATE,
                                         format, value.c_str());
        if (written != length)
            return FALSE;
        staged.resize(static_cast<size_t>(written));
        text.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

CFtpTextCodec::CFtpTextCodec(BOOL utf8Negotiated, UINT legacyCodePage)
    : Utf8Negotiated(utf8Negotiated), LegacyCodePage(legacyCodePage)
{
}

UINT CFtpTextCodec::GetCodePage() const
{
    return Utf8Negotiated ? CP_UTF8 : LegacyCodePage;
}

BOOL CFtpTextCodec::IsValid() const
{
    const UINT codePage = GetCodePage();
    return codePage != 0 && codePage != CP_UTF7 && IsValidCodePage(codePage);
}

BOOL CFtpTextCodec::Decode(const char* bytes, size_t length, std::wstring& text) const noexcept
{
    return IsValid() && Win32DecodeText(GetCodePage(), bytes, length, text).Succeeded();
}

void CFtpTextCodec::DecodeName(const char* bytes, size_t length, std::wstring& text) const noexcept
{
    // GetCodePage() unconditionally: if the codec is not valid the strict attempt inside
    // Win32DecodeTextLenient simply fails and the ISO-8859-1 fallback takes over, so there is no
    // need to name an ambient code page here.
    Win32DecodeTextLenient(GetCodePage(), bytes, length, text);
}

CFtpTextDecodeStatus CFtpTextCodec::DecodeForComparison(const char* bytes, size_t length,
                                                        std::wstring& text) const noexcept{
    if (!IsValid())
        return CFtpTextDecodeStatus::Failure;
    const Win32TextConversionResult result = Win32DecodeText(GetCodePage(), bytes, length, text);
    if (result.Succeeded())
        return CFtpTextDecodeStatus::Success;
    return result.Error == Win32TextConversionError::InvalidInput
               ? CFtpTextDecodeStatus::InvalidInput
               : CFtpTextDecodeStatus::Failure;
}

BOOL CFtpTextCodec::Encode(const wchar_t* text, size_t length, std::string& bytes) const noexcept
{
    return EncodeWithStatus(text, length, bytes) == CFtpTextEncodeStatus::Success;
}

CFtpTextEncodeStatus CFtpTextCodec::EncodeWithStatus(const wchar_t* text, size_t length,
                                                     std::string& bytes) const noexcept
{
    if (!IsValid())
        return CFtpTextEncodeStatus::Failure;
    const Win32TextConversionResult result = Win32EncodeText(GetCodePage(), text, length, bytes);
    if (result.Succeeded())
        return CFtpTextEncodeStatus::Success;
    return result.Error == Win32TextConversionError::InvalidInput ||
                   result.Error == Win32TextConversionError::UnrepresentableCharacter
               ? CFtpTextEncodeStatus::InvalidInput
               : CFtpTextEncodeStatus::Failure;
}

BOOL CFtpTextCodec::EncodeUploadName(const wchar_t* text, size_t length,
                                     std::string& bytes) const noexcept
{
    if (!IsValid())
        return FALSE;
    if (Win32EncodeText(GetCodePage(), text, length, bytes).Succeeded())
        return TRUE;
    // Best-fit rather than refuse: the queue has no per-item channel for this decision,
    // so failing here discarded every other file the user selected.
    return static_cast<bool>(Win32EncodeTextLossy(GetCodePage(), text, length, bytes)) ? TRUE : FALSE;
}

BOOL FtpDecodeServerTextForPresentation(const CFtpTextCodec& codec,
                                        std::string_view bytes,
                                        std::wstring& text) noexcept
{
    try
    {
        std::wstring staged;
        if (!codec.Decode(bytes.empty() ? "" : bytes.data(), bytes.size(), staged))
            staged = L"<invalid server text>";
        text.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL FtpFormatServerReplyMessage(const CFtpTextCodec& codec, const wchar_t* format,
                                 std::string_view subjectBytes,
                                 std::string_view replyBytes,
                                 std::wstring& message) noexcept
{
    if (format == NULL)
        return FALSE;
    try
    {
        std::wstring subject;
        std::wstring reply;
        if (!FtpDecodeServerTextForPresentation(codec, subjectBytes, subject) ||
            !FtpDecodeServerTextForPresentation(codec, replyBytes, reply))
            return FALSE;

        const int length = _scwprintf(format, subject.c_str(), reply.c_str());
        if (length < 0)
            return FALSE;
        std::wstring staged(static_cast<size_t>(length) + 1, L'\0');
        const int written = _snwprintf_s(staged.data(), staged.size(), _TRUNCATE,
                                         format, subject.c_str(), reply.c_str());
        if (written != length)
            return FALSE;
        staged.resize(static_cast<size_t>(written));
        message.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

CFtpSessionTextPolicy::CFtpSessionTextPolicy(UINT legacyCodePage)
    : Utf8Negotiated(FALSE), LegacyCodePage(legacyCodePage)
{
}

void CFtpSessionTextPolicy::ResetForConnection()
{
    Utf8Negotiated = FALSE;
}

void CFtpSessionTextPolicy::ApplyUtf8OptionsReply(int replyCode)
{
    Utf8Negotiated = replyCode >= 200 && replyCode < 300;
}

BOOL CFtpSessionTextPolicy::IsUtf8Negotiated() const
{
    return Utf8Negotiated;
}

UINT CFtpSessionTextPolicy::GetLegacyCodePage() const
{
    return LegacyCodePage;
}

CFtpTextCodec CFtpSessionTextPolicy::GetCodec() const
{
    return CFtpTextCodec(Utf8Negotiated, LegacyCodePage);
}

static size_t Utf8CodePointSize(const wchar_t* text, size_t length, size_t index)
{
    const unsigned value = text[index];
    if (value < 0x80)
        return 1;
    if (value < 0x800)
        return 2;
    if (value >= 0xD800 && value <= 0xDBFF && index + 1 < length &&
        text[index + 1] >= 0xDC00 && text[index + 1] <= 0xDFFF)
        return 4;
    return 3;
}

size_t FtpUtf8Length(const wchar_t* text, size_t length)
{
    size_t total = 0;
    for (size_t i = 0; i < length; i++)
    {
        total += Utf8CodePointSize(text, length, i);
        if (text[i] >= 0xD800 && text[i] <= 0xDBFF && i + 1 < length &&
            text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF)
            i++;
    }
    return total;
}

size_t FtpFindLogTrimPrefix(const wchar_t* text, size_t length, size_t maxUtf8Bytes,
                            size_t* skippedLines)
{
    if (skippedLines != NULL)
        *skippedLines = 0;
    const size_t totalBytes = FtpUtf8Length(text, length);
    if (totalBytes <= maxUtf8Bytes)
        return 0;

    const size_t bytesToSkip = totalBytes - maxUtf8Bytes;
    size_t skippedBytes = 0;
    size_t skipChars = 0;
    size_t lines = 0;
    while (skipChars < length)
    {
        skippedBytes += Utf8CodePointSize(text, length, skipChars);
        if (text[skipChars] >= 0xD800 && text[skipChars] <= 0xDBFF &&
            skipChars + 1 < length && text[skipChars + 1] >= 0xDC00 &&
            text[skipChars + 1] <= 0xDFFF)
            skipChars += 2;
        else
            skipChars++;

        if (text[skipChars - 1] == L'\n')
        {
            lines++;
            if (skippedBytes >= bytesToSkip)
                break;
        }
    }

    if (skippedLines != NULL)
        *skippedLines = lines;
    return skipChars;
}

static BOOL IsHexCharW(wchar_t c, int* val)
{
    if (c >= L'0' && c <= L'9')
        *val = c - L'0';
    else if (c >= L'a' && c <= L'f')
        *val = 10 + (c - L'a');
    else if (c >= L'A' && c <= L'F')
        *val = 10 + (c - L'A');
    else
        return FALSE;
    return TRUE;
}

BOOL FTPConvertHexEscapeSequencesW(std::wstring& text) noexcept
{
    try
    {
        std::wstring staged;
        staged.reserve(text.size());
        size_t source = 0;
        while (source < text.size())
        {
            int high = 0;
            int low = 0;
            if (text[source] != L'%' || source + 2 >= text.size() ||
                !IsHexCharW(text[source + 1], &high) ||
                !IsHexCharW(text[source + 2], &low))
            {
                staged.push_back(text[source++]);
                continue;
            }

            std::string bytes;
            do
            {
                bytes.push_back(static_cast<char>((high << 4) | low));
                source += 3;
            } while (source + 2 < text.size() && text[source] == L'%' &&
                     IsHexCharW(text[source + 1], &high) &&
                     IsHexCharW(text[source + 2], &low));

            std::wstring decoded;
            if (!FtpDecodeLocalText(std::string_view(bytes.data(), bytes.size()), decoded))
                return FALSE;
            staged.append(decoded);
        }
        text.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL FTPAddHexEscapeSequencesW(std::wstring& text) noexcept
{
    // Mirrors FTPAddHexEscapeSequences exactly: a '%' that introduces what would read as a
    // hex escape is doubled into "%25", so the later decode gives the literal '%' back.
    // Every character involved is ASCII, so this needs no code page and cannot refuse.
    try
    {
        std::wstring staged;
        staged.reserve(text.size());
        for (size_t source = 0; source < text.size(); ++source)
        {
            int high = 0;
            int low = 0;
            staged.push_back(text[source]);
            if (text[source] == L'%' && source + 2 < text.size() &&
                IsHexCharW(text[source + 1], &high) && IsHexCharW(text[source + 2], &low))
            {
                staged.append(L"25");
            }
        }
        text.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}
