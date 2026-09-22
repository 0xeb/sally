// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#include <cstddef>
#include <string>
#include <string_view>

class CPakIfaceAbstract;

enum class PakTextStatus
{
    Success,
    InvalidInput,
    Unrepresentable,
    TooLong,
    OutOfMemory,
};

// PAK's SPL/DLL interface predates Unicode and writes archive-entry bytes into a
// caller-owned 256-byte record. Keep that frozen capacity confined to this adapter.
constexpr size_t PAK_INTERFACE_NAME_CAPACITY = 256;

PakTextStatus EncodePakText(std::wstring_view text, std::string& bytes) noexcept;
PakTextStatus EncodePakEntryName(std::wstring_view text, std::string& bytes) noexcept;
PakTextStatus DecodePakText(std::string_view bytes, std::wstring& text) noexcept;
PakTextStatus JoinPakEntryName(std::string_view root, std::wstring_view leaf,
                               std::string& joined) noexcept;

bool ReadFirstPakEntry(CPakIfaceAbstract* pak, std::string& name, DWORD& size) noexcept;
bool ReadNextPakEntry(CPakIfaceAbstract* pak, std::string& name, DWORD& size) noexcept;
