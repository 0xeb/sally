// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/ISecureRandom.h"
#include <bcrypt.h>
#include <ntsecapi.h>
#include <limits>

namespace
{
    class Win32SecureRandom final : public ISecureRandom
    {
    public:
        DWORD Fill(void* buffer, size_t size) override
        {
            if (size == 0)
                return ERROR_SUCCESS;
            if (buffer == nullptr)
                return ERROR_INVALID_PARAMETER;
            if (size > (std::numeric_limits<ULONG>::max)())
                return ERROR_ARITHMETIC_OVERFLOW;
            const NTSTATUS status = BCryptGenRandom(nullptr, static_cast<PUCHAR>(buffer),
                                                    static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            if (status >= 0)
                return ERROR_SUCCESS;
            SecureZeroMemory(buffer, size);
            const DWORD error = LsaNtStatusToWinError(status);
            return error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
        }
    };
} // namespace

ISecureRandom* GetWin32SecureRandom()
{
    static Win32SecureRandom random;
    return &random;
}
