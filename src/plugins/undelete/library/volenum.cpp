// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "..\undelete.rh2"
#include "snapshot.h"
#include "miscstr.h"
#include "os.h"

#include "volenum.h"

BOOL GetDiskFreeSpace95Aux(const wchar_t* path, LPDWORD lpSectorsPerCluster,
                           LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters,
                           LPDWORD lpTotalNumberOfClusters)
{
    // no unicode support on Win95
    return FALSE;
}
BOOL GetDiskFreeSpace95(const wchar_t* path, LPDWORD lpSectorsPerCluster,
                        LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters,
                        LPDWORD lpTotalNumberOfClusters)
{
    // no unicode support on Win95
    return FALSE;
}
