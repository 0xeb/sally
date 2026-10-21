// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "precomp.h"
#include "cfgdlg.h"
#include <stdexcept>
#include "plugins.h"
#include "common/security/ICredentialChangeHost.h"

BOOL SaveCredentialConfiguration(HWND parent, LONG* error);
extern const wchar_t* SALAMANDER_PWDMNGR_REG;

namespace sally::security
{
    namespace
    {
        constexpr const wchar_t* JournalValue = L"Credential Rewrite Journal";
        class ProfileLock
        {
            bool Held;

        public:
            ProfileLock() : Held(LoadSaveToRegistryMutex.TryEnter() != FALSE) {}
            ~ProfileLock()
            {
                if (Held)
                    LoadSaveToRegistryMutex.Leave();
            }
            explicit operator bool() const { return Held; }
        };
        struct RegistryKeyOwner
        {
            IRegistry* Registry;
            HKEY Key;
            ~RegistryKeyOwner() { Registry->CloseKey(Key); }
        };
        class CredentialChangeHost final : public ICredentialChangeHost
        {
            IRegistry* Registry() const { return gRegistry != nullptr ? gRegistry : GetWin32Registry(); }
            RegistryResult ReadUnlocked(CredentialJournalSnapshot& snapshot)
            {
                if (SALAMANDER_ROOT_REG == nullptr)
                    return RegistryResult::Error(ERROR_INVALID_STATE);
                HKEY key = nullptr;
                const std::wstring path = std::wstring(SALAMANDER_ROOT_REG) + L"\\" + SALAMANDER_PWDMNGR_REG;
                auto result = Registry()->OpenKeyRead(HKEY_CURRENT_USER, path.c_str(), key);
                if (result.notFound())
                {
                    snapshot = {};
                    return RegistryResult::Ok();
                }
                if (!result.success)
                    return result;
                RegistryKeyOwner owner{Registry(), key};
                CredentialJournalSnapshot candidate;
                RegValueType type = RegValueType::None;
                DWORD size = 0;
                result = Registry()->ReadValue(key, JournalValue, type, nullptr, size);
                if (result.notFound())
                    result = RegistryResult::Ok();
                else if (result.success && type != RegValueType::Binary)
                    result = RegistryResult::Error(ERROR_INVALID_DATA);
                else if (result.success)
                {
                    candidate.Present = true;
                    for (;;)
                    {
                        candidate.Bytes.resize(size);
                        DWORD actual = size;
                        result = Registry()->ReadValue(key, JournalValue, type, candidate.Bytes.data(), actual);
                        if (result.errorCode == ERROR_MORE_DATA && actual > size)
                        {
                            size = actual;
                            continue;
                        }
                        if (result.success && (type != RegValueType::Binary || actual != size))
                            result = RegistryResult::Error(ERROR_INVALID_DATA);
                        break;
                    }
                }
                if (result.success)
                    snapshot = std::move(candidate);
                return result;
            }

        public:
            RegistryResult Acquire() override
            {
                return LoadSaveToRegistryMutex.TryEnter() ? RegistryResult::Ok() : RegistryResult::Error(GetLastError());
            }
            void Release() override { LoadSaveToRegistryMutex.Leave(); }
            RegistryResult Read(CredentialJournalSnapshot& snapshot) override
            {
                try
                {
                    ProfileLock lock;
                    if (!lock)
                        return RegistryResult::Error(GetLastError());
                    return ReadUnlocked(snapshot);
                }
                catch (const std::bad_alloc&)
                {
                    return RegistryResult::Error(ERROR_NOT_ENOUGH_MEMORY);
                }
                catch (const std::length_error&)
                {
                    return RegistryResult::Error(ERROR_FILE_TOO_LARGE);
                }
            }
            RegistryResult CompareAndWrite(const CredentialJournalSnapshot& expected, const CredentialBytes& replacement) override
            {
                try
                {
                    ProfileLock lock;
                    if (!lock)
                        return RegistryResult::Error(GetLastError());
                    CredentialJournalSnapshot observed;
                    auto result = ReadUnlocked(observed);
                    if (!result.success)
                        return result;
                    if (observed.Present != expected.Present || observed.Bytes != expected.Bytes)
                        return RegistryResult::Error(ERROR_REVISION_MISMATCH);
                    HKEY key = nullptr;
                    const std::wstring path = std::wstring(SALAMANDER_ROOT_REG) + L"\\" + SALAMANDER_PWDMNGR_REG;
                    result = Registry()->CreateKey(HKEY_CURRENT_USER, path.c_str(), key);
                    if (!result.success)
                        return result;
                    RegistryKeyOwner owner{Registry(), key};
                    result = Registry()->SetBinary(key, JournalValue, replacement.data(), replacement.size());
                    if (result.success)
                        result = Registry()->FlushKey(key);
                    return result;
                }
                catch (const std::bad_alloc&)
                {
                    return RegistryResult::Error(ERROR_NOT_ENOUGH_MEMORY);
                }
                catch (const std::length_error&)
                {
                    return RegistryResult::Error(ERROR_FILE_TOO_LARGE);
                }
            }
            bool Notify(HWND parent, int event) override { return Plugins.PasswordManagerEvent(parent, event) != FALSE; }
            RegistryResult SaveConfiguration(HWND parent) override
            {
                LONG error = ERROR_SUCCESS;
                return SaveCredentialConfiguration(parent, &error) ? RegistryResult::Ok()
                                                                   : RegistryResult::Error(error != ERROR_SUCCESS ? error : ERROR_WRITE_FAULT);
            }
        };
    } // namespace
    ICredentialChangeHost* GetCredentialChangeHost()
    {
        static CredentialChangeHost host;
        return &host;
    }
} // namespace sally::security
