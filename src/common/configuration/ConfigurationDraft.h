// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <type_traits>
#include <vector>

namespace sally::configuration
{
    // Semantic values only: no object bytes, addresses, list selection, focus or
    // validation side effects. Page adapters append their owned draft models.
    class DraftState
    {
        std::vector<std::wstring> Values;

    public:
        void Add(const std::wstring& value) { Values.push_back(value); }
        void Add(const wchar_t* value) { Values.emplace_back(value != nullptr ? value : L""); }
        template <class T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, int> = 0>
        void Add(T value) { Values.push_back(std::to_wstring(static_cast<long long>(value))); }
        template <class... T>
        void Append(const T&... values) { (Add(values), ...); }
        bool operator==(const DraftState& other) const { return Values == other.Values; }
        bool operator!=(const DraftState& other) const { return !(*this == other); }
    };
} // namespace sally::configuration
