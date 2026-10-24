// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "common/ConfigProfile.h"
#include "common/IRegistry.h"

namespace sally::profile
{
// Registry transport is injected; no windows, prompts or process launch in this layer.
// An empty name denotes the usual configuration, never a named profile called Default.
class Store
{
public:
    Store(IRegistry& registry, std::wstring usualRoot) : Registry(registry), UsualRoot(std::move(usualRoot)) {}
    std::wstring Root(const std::wstring& name) const;
    std::wstring Catalog() const;
    std::wstring StartupKey() const;
    RegistryResult List(std::vector<std::wstring>& names);
    RegistryResult ReadStartup(std::wstring& name);
    RegistryResult WriteStartup(const std::wstring& name);
    RegistryResult Create(const std::wstring& name);
    RegistryResult Remove(const std::wstring& name, const std::wstring& active);
    bool Exists(const std::wstring& name);

private:
    IRegistry& Registry;
    std::wstring UsualRoot;
};

inline std::wstring ResolveStartup(const std::wstring& explicitName, bool specified,
                                   const std::wstring& remembered, bool rememberedExists)
{
    if (specified)
        return explicitName.empty() || IsValidProfileName(explicitName) ? explicitName : L"";
    return rememberedExists && IsValidProfileName(remembered) ? remembered : L"";
}
} // namespace sally::profile
