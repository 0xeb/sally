// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string_view>

bool ParseRegedtUnsignedDecimal(std::string_view text, std::uint64_t& value) noexcept;
bool ParseRegedtUnsignedDecimal(std::wstring_view text, std::uint64_t& value) noexcept;
bool ParseRegedtUnsignedHex(std::string_view text, std::uint64_t& value) noexcept;
bool ParseRegedtUnsignedHex(std::wstring_view text, std::uint64_t& value) noexcept;
