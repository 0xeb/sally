// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

struct CFTPDiskWork;

BOOL FTPPrepareCreateAndWriteFileDiskWork(CFTPDiskWork& work, int socketMsg, int socketUID,
                                          DWORD msgID, const wchar_t* targetFileName,
                                          HANDLE workFile, char* flushDataBuffer,
                                          int validBytesInFlushDataBuffer) noexcept;
void FTPExecuteCreateAndWriteFileDiskWork(CFTPDiskWork& localWork, BOOL& needCopyBack,
                                          BOOL& workDone);
