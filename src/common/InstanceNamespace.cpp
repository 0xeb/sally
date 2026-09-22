// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/InstanceNamespace.h"

#include "common/IEnvironment.h"
#include "common/SharedObjectNames.h"

#include <windows.h>
#include <vector>

namespace sally::instance
{
namespace
{
std::string NarrowForObjectName(const std::wstring& value)
{
    std::string result;
    result.reserve(value.length());
    for (wchar_t ch : value)
        result.push_back(ch >= 0 && ch <= 0x7f ? static_cast<wchar_t>(ch) : '_');
    return result;
}

std::string GetInstanceIdFromProcessEnvironment()
{
    DWORD needed = GetEnvironmentVariableW(kInstanceIdEnvironmentVariableW, nullptr, 0);
    if (needed == 0)
    {
#ifdef _DEBUG
        return "Debug";
#else
        return {};
#endif
    }

    std::vector<wchar_t> value(needed);
    DWORD written = GetEnvironmentVariableW(kInstanceIdEnvironmentVariableW, value.data(), needed);
    if (written == 0 || written >= needed)
        return {};

    return NarrowForObjectName(std::wstring(value.data(), written));
}
} // namespace

std::string SanitizeInstanceIdForObjectName(const std::string& instanceId)
{
    return sally::shared_object_names::SanitizeInstanceIdForObjectName(instanceId);
}

std::string GetInstanceIdFromEnvironment(IEnvironment* environment)
{
    if (environment == nullptr)
        return GetInstanceIdFromProcessEnvironment();

    std::wstring value;
    EnvResult result = environment->GetVariable(kInstanceIdEnvironmentVariableW, value);
    if (!result.success)
        return {};

    return NarrowForObjectName(value);
}

std::string BuildSharedObjectName(const char* baseName, const std::string& instanceId)
{
    return sally::shared_object_names::BuildSharedObjectName(baseName, instanceId);
}

std::string BuildSharedObjectNameForCurrentInstance(const char* baseName)
{
    return BuildSharedObjectName(baseName, GetInstanceIdFromEnvironment());
}

bool WidenSharedObjectName(const std::string& name, std::wstring& wideName) noexcept
{
    try
    {
        std::wstring candidate;
        candidate.reserve(name.size());
        for (unsigned char ch : name)
        {
            if (ch > 0x7f)
                return false;
            candidate.push_back(static_cast<wchar_t>(ch));
        }
        wideName.swap(candidate);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool IsInstanceIsolationEnabled(IEnvironment* environment)
{
    return !SanitizeInstanceIdForObjectName(GetInstanceIdFromEnvironment(environment)).empty();
}
} // namespace sally::instance
