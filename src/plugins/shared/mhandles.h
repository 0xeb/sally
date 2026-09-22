// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The legacy monitored-handle implementation was never enabled by Sally's build
// and depended on a narrow diagnostic pipeline. Keep only the source-compatible
// pass-through macros used by plugins; there is no alternate ownership mode.
#ifdef MHANDLES_ENABLE
#error "The retired MHANDLES monitor cannot be enabled"
#endif

#define NOHANDLES(function) function

inline void __HandlesEmptyFunction() {}

#define HANDLES_CAN_USE_TRACE() __HandlesEmptyFunction()
#define HANDLES(function) ::function
#define HANDLES_Q(function) ::function
#define HANDLES_COUNTS_TO_TRACE() __HandlesEmptyFunction()
#define HANDLES_LIST_TO_TRACE() __HandlesEmptyFunction()
#define HANDLES_ADD(type, origin, handle) __HandlesEmptyFunction()
#define HANDLES_ADD_EX(outputType, success, type, origin, handle, error, synchronize) __HandlesEmptyFunction()
#define HANDLES_REMOVE(handle, expType, function) __HandlesEmptyFunction()
