// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "IShell.h"
#include <sherrors.h>

namespace sally::shell
{
inline ShellResult FileOperationResult(HRESULT result, bool aborted, HRESULT firstItemError)
{
    // IFileOperation can return user cancellation as a failing HRESULT instead of
    // S_OK plus GetAnyOperationsAborted. Keep the interface's one cancellation code.
    if (result == COPYENGINE_E_USER_CANCELLED || result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return ShellResult::Error(ERROR_CANCELLED);
    if (FAILED(result))
        return ShellResult::Error(static_cast<DWORD>(result));
    if (aborted)
        return ShellResult::Error(ERROR_CANCELLED);
    // Unresolved items never reached the shell, so their failures still need reporting.
    if (FAILED(firstItemError))
        return ShellResult::Error(static_cast<DWORD>(firstItemError));
    return ShellResult::Ok();
}
} // namespace sally::shell
