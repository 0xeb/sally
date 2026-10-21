// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "common/security/CredentialRewriteJournal.h"

namespace sally::security
{
    // Internal host coordination; the public password-manager ABI stays frozen.
    class ICredentialChangeHost : public ICredentialRewriteStore
    {
    public:
        virtual RegistryResult Acquire() = 0;
        virtual void Release() = 0;
        virtual bool Notify(HWND parent, int event) = 0;
        virtual RegistryResult SaveConfiguration(HWND parent) = 0;
    };
    DWORD FingerprintCipher(const BYTE* data, size_t size, CipherFingerprint& fingerprint);
    ICredentialChangeHost* GetCredentialChangeHost();
} // namespace sally::security
