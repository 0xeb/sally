// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/ConfigProfileStore.h"
#include <algorithm>

namespace sally::profile
{
namespace
{
struct Key
{
    IRegistry& registry;
    HKEY handle = nullptr;
    ~Key() { if (handle != nullptr) registry.CloseKey(handle); }
};
}
std::wstring Store::Root(const std::wstring& name) const
{
    return name.empty() ? UsualRoot : ProfileRegistryRoot(UsualRoot, name);
}
std::wstring Store::Catalog() const
{
    const auto slash = UsualRoot.find_last_of(L'\\');
    return UsualRoot.substr(0, slash) + L"\\Profiles";
}
std::wstring Store::StartupKey() const
{
    // Keep Debug/preview choices separate, and outside the selected profile.
    return UsualRoot + L" Startup";
}
bool Store::Exists(const std::wstring& name)
{
    return name.empty() || (IsValidProfileName(name) && Registry.KeyExists(HKEY_CURRENT_USER, Root(name).c_str()));
}
RegistryResult Store::List(std::vector<std::wstring>& names)
{
    names.clear();
    Key key{Registry};
    auto result = Registry.OpenKeyRead(HKEY_CURRENT_USER, Catalog().c_str(), key.handle);
    if (result.notFound()) return RegistryResult::Ok();
    if (!result.success) return result;
    std::vector<std::wstring> all;
    result = Registry.EnumSubKeys(key.handle, all);
    if (!result.success) return result;
    for (const auto& name : all)
    {
        if (!IsValidProfileName(name)) continue;
        Key profile{Registry};
        result = Registry.OpenKeyRead(HKEY_CURRENT_USER, Root(name).c_str(), profile.handle);
        if (result.notFound()) continue;
        if (!result.success) return result;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end(), [](const auto& a, const auto& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    return RegistryResult::Ok();
}
RegistryResult Store::ReadStartup(std::wstring& name)
{
    name.clear();
    Key key{Registry};
    auto result = Registry.OpenKeyRead(HKEY_CURRENT_USER, StartupKey().c_str(), key.handle);
    if (result.notFound()) return RegistryResult::Ok();
    if (!result.success) return result;
    result = Registry.GetString(key.handle, L"Profile", name);
    if (result.notFound()) return RegistryResult::Ok();
    if (!result.success) { name.clear(); return result; }
    if (!name.empty())
    {
        if (!IsValidProfileName(name)) { name.clear(); return RegistryResult::Ok(); }
        Key profile{Registry};
        result = Registry.OpenKeyRead(HKEY_CURRENT_USER, Root(name).c_str(), profile.handle);
        if (result.notFound()) { name.clear(); return RegistryResult::Ok(); }
        if (!result.success) { name.clear(); return result; }
    }
    return RegistryResult::Ok();
}
RegistryResult Store::WriteStartup(const std::wstring& name)
{
    if (!name.empty())
    {
        if (!IsValidProfileName(name)) return RegistryResult::Error(ERROR_INVALID_NAME);
        Key profile{Registry};
        const auto result = Registry.OpenKeyRead(HKEY_CURRENT_USER, Root(name).c_str(), profile.handle);
        if (!result.success) return result;
    }
    Key key{Registry};
    auto result = Registry.CreateKey(HKEY_CURRENT_USER, StartupKey().c_str(), key.handle);
    return result.success ? Registry.SetString(key.handle, L"Profile", name.c_str()) : result;
}
RegistryResult Store::Create(const std::wstring& name)
{
    if (!IsValidProfileName(name)) return RegistryResult::Error(ERROR_INVALID_NAME);
    Key destination{Registry};
    return Registry.CreateKeyExclusive(HKEY_CURRENT_USER, Root(name).c_str(), destination.handle);
}
RegistryResult Store::Remove(const std::wstring& name, const std::wstring& active)
{
    if (!IsValidProfileName(name)) return RegistryResult::Error(ERROR_INVALID_NAME);
    if (_wcsicmp(name.c_str(), active.c_str()) == 0) return RegistryResult::Error(ERROR_BUSY);
    std::wstring startup;
    auto result = ReadStartup(startup);
    if (!result.success) return result;
    // Require the caller to choose another startup profile explicitly before deletion.
    if (_wcsicmp(name.c_str(), startup.c_str()) == 0) return RegistryResult::Error(ERROR_BUSY);
    return Registry.DeleteKeyRecursive(HKEY_CURRENT_USER, Root(name).c_str());
}
} // namespace sally::profile
