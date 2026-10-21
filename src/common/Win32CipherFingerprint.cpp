// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/security/ICredentialChangeHost.h"
#include <bcrypt.h>
#include <ntsecapi.h>
#include <limits>

DWORD sally::security::FingerprintCipher(const BYTE* data, size_t size, CipherFingerprint& fingerprint)
{
    fingerprint.fill(0);
    if (data == nullptr || size == 0)
        return ERROR_INVALID_PARAMETER;
    if (size > (std::numeric_limits<ULONG>::max)())
        return ERROR_ARITHMETIC_OVERFLOW;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status >= 0)
        status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (status >= 0)
        status = BCryptHashData(hash, const_cast<BYTE*>(data), static_cast<ULONG>(size), 0);
    if (status >= 0)
        status = BCryptFinishHash(hash, fingerprint.data(), static_cast<ULONG>(fingerprint.size()), 0);
    if (hash != nullptr)
        BCryptDestroyHash(hash);
    if (algorithm != nullptr)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status >= 0)
        return ERROR_SUCCESS;
    fingerprint.fill(0);
    const DWORD error = LsaNtStatusToWinError(status);
    return error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
}
