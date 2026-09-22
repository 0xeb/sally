// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "pak_text.h"

#include "../dll/pakiface.h"

#include "common/Win32TextCodec.h"

#include <array>
#include <cstring>
#include <new>
#include <stdexcept>

static_assert(PAK_MAXPATH == PAK_INTERFACE_NAME_CAPACITY,
              "The named adapter must match the frozen PAK interface record");

static PakTextStatus MapConversionError(Win32TextConversionError error) noexcept
{
    switch (error)
    {
    case Win32TextConversionError::None:
        return PakTextStatus::Success;
    case Win32TextConversionError::OutOfMemory:
        return PakTextStatus::OutOfMemory;
    case Win32TextConversionError::UnrepresentableCharacter:
        return PakTextStatus::Unrepresentable;
    default:
        return PakTextStatus::InvalidInput;
    }
}

PakTextStatus EncodePakText(std::wstring_view text, std::string& bytes) noexcept
{
    return MapConversionError(Win32EncodeText(CP_ACP, text.data(), text.size(), bytes).Error);
}

PakTextStatus EncodePakEntryName(std::wstring_view text, std::string& bytes) noexcept
{
    std::string staged;
    const PakTextStatus status = EncodePakText(text, staged);
    if (status != PakTextStatus::Success)
        return status;
    if (staged.size() >= PAK_INTERFACE_NAME_CAPACITY)
        return PakTextStatus::TooLong;
    bytes.swap(staged);
    return PakTextStatus::Success;
}

PakTextStatus DecodePakText(std::string_view bytes, std::wstring& text) noexcept
{
    try
    {
        Win32DecodeTextLenient(CP_ACP, bytes.data(), bytes.size(), text);
        return PakTextStatus::Success;
    }
    catch (const std::bad_alloc&)
    {
        return PakTextStatus::OutOfMemory;
    }
    catch (const std::length_error&)
    {
        return PakTextStatus::OutOfMemory;
    }
}

PakTextStatus JoinPakEntryName(std::string_view root, std::wstring_view leaf,
                               std::string& joined) noexcept
{
    if (root.find('\0') != std::string_view::npos)
        return PakTextStatus::InvalidInput;

    std::string encodedLeaf;
    const PakTextStatus status = EncodePakText(leaf, encodedLeaf);
    if (status != PakTextStatus::Success)
        return status;

    const bool separator = !root.empty() && root.back() != '\\';
    if (root.size() + static_cast<size_t>(separator) + encodedLeaf.size() >=
        PAK_INTERFACE_NAME_CAPACITY)
        return PakTextStatus::TooLong;

    try
    {
        std::string staged(root);
        if (separator)
            staged.push_back('\\');
        staged += encodedLeaf;
        joined.swap(staged);
        return PakTextStatus::Success;
    }
    catch (const std::bad_alloc&)
    {
        return PakTextStatus::OutOfMemory;
    }
    catch (const std::length_error&)
    {
        return PakTextStatus::OutOfMemory;
    }
}

static bool ReadPakEntry(CPakIfaceAbstract* pak, bool first, std::string& name,
                         DWORD& size) noexcept
{
    if (pak == nullptr)
        return false;
    try
    {
        std::array<char, PAK_INTERFACE_NAME_CAPACITY> buffer{};
        DWORD stagedSize = 0;
        if (!(first ? pak->GetFirstFile(buffer.data(), &stagedSize)
                    : pak->GetNextFile(buffer.data(), &stagedSize)))
            return false;

        const void* terminator = std::memchr(buffer.data(), '\0', buffer.size());
        if (terminator == nullptr)
            return false;
        const size_t length = static_cast<const char*>(terminator) - buffer.data();
        std::string staged(buffer.data(), length);
        name.swap(staged);
        size = stagedSize;
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool ReadFirstPakEntry(CPakIfaceAbstract* pak, std::string& name, DWORD& size) noexcept
{
    return ReadPakEntry(pak, true, name, size);
}

bool ReadNextPakEntry(CPakIfaceAbstract* pak, std::string& name, DWORD& size) noexcept
{
    return ReadPakEntry(pak, false, name, size);
}
