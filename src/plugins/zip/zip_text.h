// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "../../common/Win32TextCodec.h"
#include "../shared/plugin_window_text.h"

constexpr UINT ZIP_LEGACY_CODE_PAGE = CP_ACP;

inline bool DecodeZipBytes(UINT codePage, const char* bytes, size_t length,
                           std::wstring& text)
{
    return Win32DecodeText(codePage, bytes, length, text).Succeeded();
}

inline bool EncodeZipBytes(UINT codePage, const wchar_t* text, size_t length,
                           std::string& bytes)
{
    return Win32EncodeText(codePage, text, length, bytes).Succeeded();
}

// Entry NAMES specifically. An entry name is the only handle the user has on that member, so a
// byte sequence the active code page dislikes must degrade to a usable name rather than remove
// the entry from the listing entirely. See Win32DecodeTextLenient.
inline void DecodeZipNameBytes(UINT codePage, const char* bytes, size_t length,
                               std::wstring& text)
{
    Win32DecodeTextLenient(codePage, bytes, length, text);
}

// ZIP comments, passwords, and legacy configuration fields are byte-owned at the archive
// boundary. Keep that ACP projection named and exact while dialog controls remain UTF-16.
inline bool DecodeZipLegacyText(const char* bytes, std::wstring& text)
{
    if (bytes == NULL)
    {
        std::wstring empty;
        text.swap(empty);
        return true;
    }
    return DecodeZipBytes(ZIP_LEGACY_CODE_PAGE, bytes, strlen(bytes), text);
}

inline bool EncodeZipLegacyTextExact(const std::wstring& text, std::string& bytes)
{
    return EncodeZipBytes(ZIP_LEGACY_CODE_PAGE, text.data(), text.size(), bytes);
}

inline bool SetDlgItemZipLegacyText(HWND dialog, int item, const char* bytes)
{
    std::wstring text;
    return DecodeZipLegacyText(bytes, text) &&
           SetWindowTextW(GetDlgItem(dialog, item), text.c_str()) != FALSE;
}

inline bool ReadDlgItemZipLegacyTextExact(HWND dialog, int item, std::string& bytes)
{
    std::wstring text;
    return ReadWindowTextOwnedW(GetDlgItem(dialog, item), text) &&
           EncodeZipLegacyTextExact(text, bytes);
}

// ZIP's legacy parser remains byte-oriented, but application-owned text in that parser is
// UTF-8. CP_ACP is accepted only when importing configuration or old in-memory archive names.
inline std::wstring ZipTextToWide(const char* text, int length = -1)
{
    if (text == NULL)
        return std::wstring();
    const size_t inputLength = length < 0 ? strlen(text) : static_cast<size_t>(length);
    std::wstring result;
    if (!DecodeZipBytes(CP_UTF8, text, inputLength, result) &&
        !DecodeZipBytes(ZIP_LEGACY_CODE_PAGE, text, inputLength, result))
        return std::wstring();
    return result;
}

inline std::string WideToZipText(const wchar_t* text, int length = -1)
{
    if (text == NULL)
        return std::string();
    const size_t inputLength = length < 0 ? wcslen(text) : static_cast<size_t>(length);
    std::string result;
    if (!EncodeZipBytes(CP_UTF8, text, inputLength, result))
        return std::string();
    return result;
}

inline bool TryWideToZipText(const wchar_t* text, std::string& result, int length = -1)
{
    result.clear();
    if (text == NULL)
        return true;
    const size_t inputLength = length < 0 ? wcslen(text) : static_cast<size_t>(length);
    return EncodeZipBytes(CP_UTF8, text, inputLength, result);
}

inline bool CopyWideToZipText(const wchar_t* text, char* target, int targetSize)
{
    if (target == NULL || targetSize <= 0)
        return false;
    std::string encoded;
    if (!TryWideToZipText(text, encoded))
    {
        target[0] = 0;
        return false;
    }
    if (encoded.size() >= (size_t)targetSize)
    {
        target[0] = 0;
        return false;
    }
    memcpy(target, encoded.c_str(), encoded.size() + 1);
    return true;
}

// Volume paths are local filesystem identities. Keep them UTF-16 and dynamically owned;
// only archive member records cross the ZIP byte codec.
inline std::wstring RenumberZipVolumeName(int number, const wchar_t* oldName,
                                          bool lastFile, bool winzip)
{
    const std::wstring input = oldName != NULL ? oldName : L"";
    const size_t slash = input.find_last_of(L'\\');
    const size_t archiveStart = slash == std::wstring::npos ? 0 : slash + 1;
    size_t extension = input.find_last_of(L'.');
    if (extension != std::wstring::npos && extension < archiveStart)
        extension = std::wstring::npos;

    size_t numberEnd = std::wstring::npos;
    size_t numberStart = archiveStart;
    for (size_t pos = input.size(); pos > archiveStart + 1;)
    {
        --pos;
        const bool digit = input[pos] >= L'0' && input[pos] <= L'9';
        if (numberEnd == std::wstring::npos && digit)
            numberEnd = pos + 1;
        if (numberEnd != std::string::npos && !digit)
        {
            numberStart = pos + 1;
            break;
        }
    }

    const auto paddedNumber = [number](size_t width) {
        std::wstring value = std::to_wstring(number);
        if (number >= 0 && value.size() < width)
            value.insert(0, width - value.size(), L'0');
        return value;
    };

    if (numberEnd != std::wstring::npos && !winzip)
    {
        if (lastFile && extension != std::wstring::npos && extension < numberStart)
            return input.substr(0, extension) + L".zip";
        return input.substr(0, numberStart) +
               paddedNumber(numberEnd - numberStart) + input.substr(numberEnd);
    }

    if (extension != std::wstring::npos)
        return input.substr(0, extension) +
               (lastFile ? L".zip" : L".z" + paddedNumber(2));
    return input + paddedNumber(2);
}

inline std::wstring MakeZipVolumeFileName(int number, bool sequentialNames,
                                          const wchar_t* archive, bool winZipNames)
{
    const std::wstring input = archive != NULL ? archive : L"";
    if (!sequentialNames)
        return input;

    const size_t slash = input.find_last_of(L'\\');
    const size_t archiveStart = slash == std::wstring::npos ? 0 : slash + 1;
    size_t extension = input.find_last_of(L'.');
    if (extension == std::wstring::npos || extension < archiveStart)
        extension = input.size();

    std::wstring numberText = std::to_wstring(number);
    if (number >= 0 && numberText.size() < 2)
        numberText.insert(0, 2 - numberText.size(), L'0');
    if (winZipNames)
        return input.substr(0, extension) + L".z" + numberText;

    const bool digitBeforeExtension = extension > 0 &&
                                      input[extension - 1] >= L'0' &&
                                      input[extension - 1] <= L'9';
    return input.substr(0, extension) + (digitBeforeExtension ? L"_" : L"") +
           numberText + input.substr(extension);
}

inline std::wstring ReplaceZipPathExtension(const std::wstring& path,
                                            const wchar_t* extension)
{
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    const size_t end = dot != std::wstring::npos &&
                               (slash == std::wstring::npos || dot > slash)
                           ? dot
                           : path.size();
    return path.substr(0, end) + (extension != NULL ? extension : L"");
}

inline std::wstring JoinZipLocalPath(const wchar_t* directory, const wchar_t* child)
{
    std::wstring result = directory != NULL ? directory : L"";
    if (child == NULL || *child == 0)
        return result;
    if (!result.empty() && result.back() != L'\\')
        result.push_back(L'\\');
    result.append(child);
    return result;
}

struct ZipExtractionPaths
{
    std::wstring Directory;
    std::wstring Output;
};

inline ZipExtractionPaths MakeZipExtractionPaths(const wchar_t* targetDirectory,
                                                  const std::wstring& normalizedMemberName,
                                                  const wchar_t* overrideFileName = NULL)
{
    const size_t slash = normalizedMemberName.find_last_of(L'\\');
    const std::wstring relativeDirectory = slash == std::wstring::npos
                                               ? std::wstring()
                                               : normalizedMemberName.substr(0, slash);
    const wchar_t* fileName = overrideFileName != NULL
                                  ? overrideFileName
                                  : (slash == std::wstring::npos
                                         ? normalizedMemberName.c_str()
                                         : normalizedMemberName.c_str() + slash + 1);

    ZipExtractionPaths paths;
    paths.Directory = JoinZipLocalPath(targetDirectory, relativeDirectory.c_str());
    paths.Output = JoinZipLocalPath(paths.Directory.c_str(), fileName);
    return paths;
}

inline std::vector<std::string> SplitZipMaskList(const char* masks)
{
    std::vector<std::string> result;
    const char* source = masks != NULL ? masks : "";
    while (*source)
    {
        std::string mask;
        while (*source)
        {
            if (*source == ';')
            {
                if (source[1] == ';')
                    ++source;
                else
                    break;
            }
            mask.push_back(*source++);
        }
        size_t first = 0;
        while (first < mask.size() && static_cast<unsigned char>(mask[first]) <= ' ')
            ++first;
        size_t last = mask.size();
        while (last > first && static_cast<unsigned char>(mask[last - 1]) <= ' ')
            --last;
        if (last > first)
            result.emplace_back(mask.substr(first, last - first));
        if (*source)
            ++source;
    }
    return result;
}

inline int CompareZipText(const char* left, int leftLength, const char* right, int rightLength,
                          DWORD flags)
{
    const std::wstring leftWide = ZipTextToWide(left, leftLength);
    const std::wstring rightWide = ZipTextToWide(right, rightLength);
    if (leftWide.empty() || rightWide.empty())
    {
        if (leftWide.size() == rightWide.size())
            return CSTR_EQUAL;
        return leftWide.empty() ? CSTR_LESS_THAN : CSTR_GREATER_THAN;
    }
    return CompareStringW(LOCALE_USER_DEFAULT, flags,
                          leftWide.c_str(), (int)leftWide.size(),
                          rightWide.c_str(), (int)rightWide.size());
}
