// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wmobile_fileops_core.h"

#include <windows.h>

namespace wmobile
{

unsigned long BlockingDeleteAttributes()
{
    return FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY;
}

bool DeleteOneItem(DeviceFileOps& ops,
                   const wchar_t* path,
                   unsigned long attributes,
                   bool isDirectory)
{
    if (path == nullptr || *path == 0)
        return false;

    // Clear first, delete second. The other order fails on anything read-only, and the failure
    // surfaces as a device error rather than as anything pointing at the attributes.
    if (attributes & BlockingDeleteAttributes())
    {
        // A failure here is deliberately NOT fatal: the delete below may still succeed, and the
        // original code ignored this return value too. Preserved rather than tightened, because
        // tightening it would start refusing deletes that work today.
        ops.SetAttributesOnDevice(path, FILE_ATTRIBUTE_ARCHIVE);
    }

    return isDirectory ? ops.RemoveDirectoryOnDevice(path) : ops.DeleteFileOnDevice(path);
}

} // namespace wmobile
