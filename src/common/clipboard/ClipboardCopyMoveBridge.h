// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class CCopyMoveData;

namespace sally
{
    namespace clipboard
    {
        struct ClipboardFileTransfer;

        bool CreateCopyMoveData(const ClipboardFileTransfer& transfer,
                                CCopyMoveData** data) noexcept;
    } // namespace clipboard
} // namespace sally
