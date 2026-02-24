// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Installs a handler for situations when memory is exhausted during operator new
// or the malloc family (calloc, realloc, etc.). It guarantees that neither
// operator new nor malloc will return NULL without the user's knowledge. It
// displays an "insufficient memory" error message and allows the user to free
// memory (e.g., by closing other applications) and retry the allocation. The user
// can also terminate the process or let the allocation error propagate to the
// application (operator new or malloc returns NULL; allocations of large memory
// blocks should be prepared for this, otherwise the application may crash — the
// user is warned about this).

// Configure localized wording for the out‑of‑memory message and related warnings
// (pass NULL to keep a string unchanged). Expected content:
// message:
// Insufficient memory to allocate %u bytes. Try to release some memory (e.g.
// close some running application) and click Retry. If it does not help, you can
// click Ignore to pass memory allocation error to this application or click Abort
// to terminate this application.
// title: (used for both the "message" and the "warning")
// we recommend using the application name so the user knows which app complains
// warningIgnore:
// Do you really want to pass memory allocation error to this application?\n\n
// WARNING: Application may crash and then all unsaved data will be lost!\n
// HINT: We recommend risking this only if the application is trying to allocate
// an extra‑large block of memory (i.e., more than 500 MB).
// warningAbort:
// Do you really want to terminate this application?\n\nWARNING: All unsaved data will be lost!
void SetAllocHandlerMessage(const TCHAR* message, const TCHAR* title,
                            const TCHAR* warningIgnore, const TCHAR* warningAbort);
