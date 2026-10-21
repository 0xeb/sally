// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "selfextr/comdefs.h"
#include "typecons.h"
#include "chicon.h"
#include "common.h"
#include "add_del.h"
#include "zip.rh"
#include "zip.rh2"
#include "lang\lang.rh"

int CZipPack::SaveComment()
{
    CALL_STACK_MESSAGE1("CZipPack::SaveComment()");
    if (ZipAttr & FILE_ATTRIBUTE_READONLY)
        return IDS_READONLY;
    // open it for writing
    if (ZipFile != NULL)
        CloseCFile(ZipFile);
    ZipFile = NULL;
    int ret = CreateCFile(&ZipFile, ZipName.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, PE_NOSKIP, NULL,
                          true, false);
    if (ret)
    {
        if (ret == ERR_LOWMEM)
            ErrorID = IDS_LOWMEM;
        else
            ErrorID = IDS_NODISPLAY;
        return ErrorID;
    }

    EONewCentrDir.CommentLen = (__UINT16)strlen(Comment);
    ZipFile->FilePointer = EOCentrDirOffs;
    if (Write(ZipFile, &EONewCentrDir, sizeof(CEOCentrDirRecord), NULL))
    {
        return IDS_NODISPLAY;
    }
    if (Write(ZipFile, Comment, EONewCentrDir.CommentLen, NULL))
    {
        return IDS_NODISPLAY;
    }
    if (Flush(ZipFile, ZipFile->OutputBuffer, ZipFile->BufferPosition, NULL))
    {
        return IDS_NODISPLAY;
    }
    SetEndOfFile(ZipFile->File);
    return ErrorID = 0;
}
