// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "worker.h"
#include "common/BuildScript.h"

BOOL BuildScriptLegacyADSProbe(const wchar_t* sourceName,
                               BOOL isDir,
                               DWORD bytesPerCluster,
                               CBuildADSProbeResult* result,
                               void* context)
{
    (void)context;
    if (sourceName == NULL || result == NULL)
        return FALSE;

    CQuadWord adsSize(0, 0);
    CQuadWord adsOccupiedSpace(0, 0);
    DWORD winError = NO_ERROR;
    BOOL onlyDiscardableStreams = FALSE;
    const BOOL hasADS = CheckFileOrDirADS(
        sourceName, isDir, &adsSize, NULL, NULL, &winError,
        bytesPerCluster, &adsOccupiedSpace, &onlyDiscardableStreams);

    result->HasADS = hasADS != FALSE;
    result->HasProbeError = winError != NO_ERROR;
    result->Size = adsSize.Value;
    result->OccupiedSpace = adsOccupiedSpace.Value;
    result->WinError = winError;
    result->OnlyDiscardableStreams = onlyDiscardableStreams;
    return TRUE;
}
