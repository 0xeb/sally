// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "Supporter.h"
#include "IRegistry.h"

namespace Sally::Supporter
{
    std::wstring ResolveProfileRoot();
    Preferences ReadSupporterState(const std::wstring& profile, IRegistry& registry);
    enum StateField : unsigned
    {
        CertificateField = 1,
        TitleField = 2,
        InvitationsField = 4,
        DismissedField = 8,
        AllFields = 15
    };
    RegistryResult WriteSupporterState(const std::wstring& profile, IRegistry& registry, const Preferences& state, unsigned fields = AllFields);
    uint64_t NowUtc();
} // namespace Sally::Supporter
