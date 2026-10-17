// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "supporter/SupporterState.h"
#include "registry_names.h"
#include <ctime>

namespace Sally::Supporter
{
    namespace
    {
        std::string Ascii(const std::wstring& text)
        {
            std::string result;
            for (wchar_t c : text)
            {
                if (c <= 0 || c > 127)
                    return "invalid";
                result += static_cast<char>(c);
            }
            return result;
        }
    } // namespace
    std::wstring ResolveProfileRoot()
    {
        DWORD needed = GetEnvironmentVariableW(SAL_ENV_CONFIG_ROOT_W, nullptr, 0);
        while (needed)
        {
            std::wstring buffer(needed, L'\0');
            DWORD actual = GetEnvironmentVariableW(SAL_ENV_CONFIG_ROOT_W, buffer.data(), needed);
            if (!actual)
                break;
            if (actual < needed)
            {
                buffer.resize(actual);
                return buffer;
            }
            needed = actual;
        }
        return SAL_REG_ROOT_SALLY_CURRENT_W;
    }
    Preferences ReadSupporterState(const std::wstring& profile, IRegistry& registry)
    {
        Preferences state;
        HKEY key = nullptr;
        if (!registry.OpenKeyRead(HKEY_CURRENT_USER, (profile + L"\\" SAL_REG_SUPPORTER_W).c_str(), key).success)
            return state;
        std::wstring text;
        if (registry.GetString(key, SAL_REG_SUPPORTER_CERTIFICATE_W, text).success && text.size() <= 4096)
        {
            state.Token = Ascii(text);
        }
        DWORD flag = 0;
        if (registry.GetDWord(key, SAL_REG_SUPPORTER_TITLE_W, flag).success && flag <= 1)
            state.ShowNameInTitle = flag == 1;
        flag = 1;
        if (registry.GetDWord(key, SAL_REG_SUPPORTER_INVITATIONS_W, flag).success && flag <= 1)
            state.InvitationsEnabled = flag == 1;
        if (registry.GetString(key, SAL_REG_SUPPORTER_DISMISSED_W, text).success && text.size() < 64)
            state.DismissedRelease = CanonicalVersion(Ascii(text));
        registry.CloseKey(key);
        return state;
    }
    RegistryResult WriteSupporterState(const std::wstring& profile, IRegistry& registry, const Preferences& state, unsigned fields)
    {
        const auto previous = ReadSupporterState(profile, registry);
        HKEY key = nullptr;
        auto result = registry.CreateKey(HKEY_CURRENT_USER, (profile + L"\\" SAL_REG_SUPPORTER_W).c_str(), key);
        if (!result.success)
            return result;
        auto store = [&](const Preferences& value)
        {
            const std::wstring token(value.Token.begin(), value.Token.end());
            const auto normalized = CanonicalVersion(value.DismissedRelease);
            const std::wstring version(normalized.begin(), normalized.end());
            RegistryResult written = RegistryResult::Ok();
            if (fields & CertificateField)
                written = registry.SetString(key, SAL_REG_SUPPORTER_CERTIFICATE_W, token.c_str());
            if (written.success && (fields & TitleField))
                written = registry.SetDWord(key, SAL_REG_SUPPORTER_TITLE_W, value.ShowNameInTitle ? 1 : 0);
            if (written.success && (fields & InvitationsField))
                written = registry.SetDWord(key, SAL_REG_SUPPORTER_INVITATIONS_W, value.InvitationsEnabled ? 1 : 0);
            if (written.success && (fields & DismissedField))
                written = registry.SetString(key, SAL_REG_SUPPORTER_DISMISSED_W, version.c_str());
            return written;
        };
        result = store(state);
        if (!result.success)
            store(previous);
        registry.CloseKey(key);
        return result;
    }
    uint64_t NowUtc() { return static_cast<uint64_t>(std::time(nullptr)); }
} // namespace Sally::Supporter
