// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "clipboard/HDropSelection.h"
#include "common/Win32TextCodec.h"

#include <climits>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <utility>

namespace
{
bool DecodeAnsiPath(const char* path, std::size_t length, std::wstring& decoded)
{
    return static_cast<bool>(Win32DecodeTextPermissive(GetACP(), path, length, decoded));
}

bool DecodeStringList(const void* data, std::size_t dataSize, bool wide,
                      std::vector<std::wstring>& paths)
{
    if (data == NULL || dataSize == 0)
        return false;

    std::vector<std::wstring> decodedPaths;
    const BYTE* bytes = static_cast<const BYTE*>(data);
    if (wide)
    {
        if ((reinterpret_cast<std::uintptr_t>(data) % alignof(wchar_t)) != 0 ||
            dataSize % sizeof(wchar_t) != 0)
            return false;

        const wchar_t* current = reinterpret_cast<const wchar_t*>(bytes);
        const wchar_t* limit = current + dataSize / sizeof(wchar_t);
        while (current < limit)
        {
            const wchar_t* end = current;
            while (end < limit && *end != L'\0')
                ++end;
            if (end == limit)
                return false;
            if (end == current)
            {
                paths.swap(decodedPaths);
                return true;
            }
            decodedPaths.emplace_back(current, end);
            current = end + 1;
        }
        return false;
    }

    const char* current = reinterpret_cast<const char*>(bytes);
    const char* limit = current + dataSize;
    while (current < limit)
    {
        const char* end = current;
        while (end < limit && *end != '\0')
            ++end;
        if (end == limit)
            return false;
        if (end == current)
        {
            paths.swap(decodedPaths);
            return true;
        }

        std::wstring decoded;
        if (!DecodeAnsiPath(current, static_cast<std::size_t>(end - current), decoded))
            return false;
        decodedPaths.push_back(std::move(decoded));
        current = end + 1;
    }
    return false;
}

bool SamePath(const std::wstring& left, const std::wstring& right)
{
    // Source directories are Windows paths. Use an invariant ordinal comparison
    // rather than the caller's UI locale so every consumer sees the same group.
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}
} // namespace

namespace sally::clipboard
{
bool TryDecodeClipboardStringList(const void* data, std::size_t dataSize, bool wide,
                                  std::vector<std::wstring>& strings) noexcept
{
    try
    {
        return DecodeStringList(data, dataSize, wide, strings);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    catch (const std::length_error&)
    {
        return false;
    }
}

bool TryDecodeHDropPaths(const DROPFILES* data, std::size_t dataSize,
                         std::vector<std::wstring>& paths) noexcept
{
    if (data == NULL || dataSize < sizeof(DROPFILES) ||
        data->pFiles < sizeof(DROPFILES) || data->pFiles >= dataSize)
        return false;

    // DROPFILES::pFiles is a byte offset in both format arms. Keep that unit
    // until after the offset has been applied, then select the encoded element.
    const BYTE* bytes = reinterpret_cast<const BYTE*>(data) + data->pFiles;
    const std::size_t remaining = dataSize - data->pFiles;
    if (data->fWide && data->pFiles % alignof(wchar_t) != 0)
        return false;
    return TryDecodeClipboardStringList(bytes, remaining, data->fWide != FALSE, paths);
}

bool TryParseHDropSelection(const DROPFILES* data, std::size_t dataSize,
                            HDropSelection& selection) noexcept
{
    try
    {
        std::vector<std::wstring> paths;
        if (!TryDecodeHDropPaths(data, dataSize, paths))
            return false;

        HDropSelection parsed;
        for (const std::wstring& path : paths)
        {
            std::size_t nameEnd = path.length();
            if (nameEnd != 0 && path[nameEnd - 1] == L'\\')
                --nameEnd;
            if (nameEnd == 0)
                return false;

            const std::size_t separator = path.rfind(L'\\', nameEnd - 1);
            if (separator == std::wstring::npos)
                return false;

            const std::wstring sourcePath = path.substr(0, separator);
            if (parsed.Names.empty())
                parsed.SourcePath = sourcePath;
            else if (!SamePath(parsed.SourcePath, sourcePath))
                return false;

            parsed.Names.push_back(path.substr(separator + 1, nameEnd - separator - 1));
        }

        if (!parsed.Names.empty() && parsed.SourcePath.length() < 3)
            parsed.SourcePath += L'\\';

        selection = std::move(parsed);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    catch (const std::length_error&)
    {
        return false;
    }
}

bool TryGetSingleHDropPath(const DROPFILES* data, std::size_t dataSize,
                           std::wstring& path) noexcept
{
    try
    {
        // Decode the list directly rather than going through TryParseHDropSelection.
        //
        // That parser exists to split a multi-item drop into one source directory plus
        // names, so it refuses any path it cannot split - and a volume root ("C:\") or a
        // share root ("\\server\share") has no parent directory to split off. Borrowing
        // it here inherited that refusal for the single-item case, where no split is
        // wanted: dropping a drive root on the command line, the status bar or the user
        // menu toolbar silently did nothing. Pre-unicode read the one name and used it
        // verbatim, and that is the whole job here.
        std::vector<std::wstring> paths;
        if (!TryDecodeHDropPaths(data, dataSize, paths) || paths.size() != 1 ||
            paths.front().empty())
            return false;

        path.swap(paths.front());
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    catch (const std::length_error&)
    {
        return false;
    }
}
} // namespace sally::clipboard
