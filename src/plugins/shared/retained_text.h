// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <list>
#include <string>
#include <utility>

// Keeps a loaded string alive for a plugin's pointer-returning LoadStr(int).
//
// SalamanderGeneral->LoadStr once returned a pointer into a 10000-character buffer "used
// cyclically" (spl_gen.h): the pointer stayed good until that much later text had been loaded,
// and the storage cost was flat forever. A plugin that still hands out raw pointers keeps both
// halves of that promise here. Each thread retains its strings in a list and drops the oldest once
// the retained total passes the same budget. std::list never relocates a retained element, so
// every pointer inside the budget stays valid. The floor keeps one expression that passes several
// LoadStr results as arguments safe even when the strings are enormous.
template <class Char>
Char* RetainBoundedText(std::basic_string<Char>&& text)
{
    static thread_local std::list<std::basic_string<Char>> values;
    static thread_local size_t retained = 0;
    retained += text.size() + 1;
    values.emplace_back(std::move(text));

    const size_t retentionBudget = 10000;
    const size_t minRetained = 16;
    while (retained > retentionBudget && values.size() > minRetained)
    {
        retained -= values.front().size() + 1;
        values.pop_front();
    }
    return values.back().data();
}
