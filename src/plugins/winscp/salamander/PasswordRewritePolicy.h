// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace sally_winscp
{
    enum PasswordRewriteAction
    {
        KeepPassword,
        EncryptWithMaster,
        ScrambleWithoutMaster
    };
    inline PasswordRewriteAction PasswordRewritePolicy(bool externalEncrypted, bool masterEntered,
                                                       bool useMaster, bool changingMaster)
    {
        if (!changingMaster && (externalEncrypted || !masterEntered))
            return KeepPassword;
        return useMaster ? EncryptWithMaster : ScrambleWithoutMaster;
    }
} // namespace sally_winscp
