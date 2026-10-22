// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "SupporterState.h"
#include "Supporter.rh"

namespace Sally::Supporter
{
    std::wstring SupporterText(UINT id, LANGID language);
    std::wstring SupporterSummary(const Certificate& certificate, LANGID language);
    bool ShowSupporterDialog(HWND parent, LANGID language, const std::wstring& profile, IRegistry& registry, bool enterKey = false);
    void OpenSupportPage(HWND parent);
    bool ShowSupportHub(HWND parent, LANGID language, const std::wstring& profile, IRegistry& registry);
    std::wstring SupporterTitleSuffix(const std::wstring& profile, IRegistry& registry, LANGID language);
} // namespace Sally::Supporter
