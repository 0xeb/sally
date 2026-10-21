// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "precomp.h"
#include "7zip.h"
#include "7zclient.h"
#include "7zip.rh2"
#include "lang/lang.rh"

struct CSortItem
{
    const UString* String;
    int Index;
};

static int __cdecl CompareStrings(const void* a1, const void* a2)
{
    const UString& s1 = *((CSortItem*)a1)->String;
    const UString& s2 = *((CSortItem*)a2)->String;
    return MyStringCompareNoCase(s1, s2);
}

static void
IndirectSort(UStringVector& strings, CIntVector& indexes)
{
    indexes.Clear();
    if (strings.IsEmpty())
        return;
    int numItems = strings.Size();
    //CPointerVector pointers;
    //pointers.Reserve(numItems);
    indexes.Reserve(numItems);
    TDirectArray<CSortItem> pointers(numItems, 1);
    int i;
    for (i = 0; i < numItems; i++)
    {
        CSortItem si;
        si.String = &strings[i];
        si.Index = i;
        pointers.Add(si);
    }
    qsort(&pointers[0], numItems, sizeof(CSortItem), CompareStrings);
    for (i = 0; i < numItems; i++)
    {
        indexes.Add(pointers[i].Index);
    }
}

static int
AddFileUpdateInfo(TIndirectArray<CUpdateInfo>* updateList, int fileIdx)
{
    CUpdateInfo* ui = new CUpdateInfo;
    if (ui == NULL)
    {
        Error(IDS_INSUFFICIENT_MEMORY);
        return OPER_CANCEL;
    }

    ui->NewData = true;
    ui->NewProperties = true;
    ui->ExistsInArchive = false;
    ui->FileItemIndex = fileIdx;
    ui->ExistsOnDisk = true;
    ui->IsAnti = false;
    ui->ExistsInArchive = false;
    updateList->Add(ui);

    return OPER_OK;
}

static int
AddArchiveUpdateInfo(TIndirectArray<CUpdateInfo>* updateList, int archiveIdx)
{
    CUpdateInfo* ui = new CUpdateInfo;
    if (ui == NULL)
    {
        Error(IDS_INSUFFICIENT_MEMORY);
        return OPER_CANCEL;
    }
    ui->ExistsInArchive = true;
    ui->ArchiveItemIndex = archiveIdx;
    ui->ExistsOnDisk = true;
    ui->FileItemIndex = -1;
    ui->IsAnti = false;
    ui->NewData = ui->NewProperties = false;
    updateList->Add(ui);

    return OPER_OK;
}

int C7zClient::UpdateMakeUpdateList(TIndirectArray<CFileItem>* fileList, TIndirectArray<CArchiveItem>* archiveItems,
                                    TIndirectArray<CUpdateInfo>* updateList)
{
    int i;

    // variables for the overwrite dialog
    BOOL overwriteSilent = FALSE;
    enum EOperationMode
    {
        Ask,
        Overwrite,
        Skip
    } overwriteMode = Ask;

    // logically sort fileList
    CIntVector fileIndexes;
    UStringVector fileNames;
    int fileItemCount = fileList->Count;
    for (i = 0; i < fileItemCount; i++)
        fileNames.Add((*fileList)[i]->Name);
    IndirectSort(fileNames, fileIndexes);

    // logically sort archiveList
    CIntVector archiveIndexes;
    UStringVector archiveNames;
    int archiveItemCount = archiveItems->Count;
    for (i = 0; i < archiveItemCount; i++)
        archiveNames.Add((*archiveItems)[i]->Name);
    IndirectSort(archiveNames, archiveIndexes);

    int fileIterIndex = 0;
    int archiveIterIndex = 0;
    while (fileIterIndex < fileItemCount && archiveIterIndex < archiveItemCount)
    {
        int fileIdx = fileIndexes[fileIterIndex];
        int archiveIdx = archiveIndexes[archiveIterIndex];

        CFileItem* fi = (*fileList)[fileIdx];
        const CArchiveItem* ai = (*archiveItems)[archiveIdx];
        int cmpRes = MyStringCompareNoCase(fi->Name, ai->Name);

        if (cmpRes < 0)
        {
            if (AddFileUpdateInfo(updateList, fileIdx) == OPER_CANCEL)
                return OPER_CANCEL;
            fileIterIndex++;
        }
        else if (cmpRes > 0)
        {
            if (AddArchiveUpdateInfo(updateList, archiveIdx) == OPER_CANCEL)
                return OPER_CANCEL;
            archiveIterIndex++;
        }
        else // cmpRes == 0
        {
            if (!ai->IsDir)
            {
                // overwriting only matters for files
                EOperationMode mode = overwriteMode;

                if (mode == Ask)
                {
                    const std::wstring fifd = GetInfoW(&fi->LastWriteTime, fi->Size);
                    const std::wstring aifd = GetInfoW(&ai->LastWrite, ai->Size);

                    int userAct = SalamanderGeneral->DialogOverwrite(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_YESALLSKIPCANCEL,
                                                                     ai->Name, aifd.c_str(), fi->FullPath, fifd.c_str());

                    switch (userAct)
                    {
                    case DIALOG_YES:
                        mode = Overwrite;
                        break;
                    case DIALOG_ALL:
                        mode = Overwrite;
                        overwriteSilent = TRUE;
                        break;
                    case DIALOG_SKIP:
                        mode = Skip;
                        break;
                    case DIALOG_SKIPALL:
                        mode = Skip;
                        overwriteSilent = TRUE;
                        break;
                    case DIALOG_CANCEL:
                        return OPER_CANCEL;
                    }

                    if (overwriteSilent)
                        overwriteMode = mode;
                }

                switch (mode)
                {
                case Overwrite:
                    if (AddFileUpdateInfo(updateList, fileIdx) == OPER_CANCEL)
                        return OPER_CANCEL;
                    fi->CanDelete = TRUE;
                    break;

                case Skip:
                    if (AddArchiveUpdateInfo(updateList, archiveIdx) == OPER_CANCEL)
                        return OPER_CANCEL;
                    fi->CanDelete = FALSE;
                    break;
                } // switch
            }
            else if (AddFileUpdateInfo(updateList, fileIdx) == OPER_CANCEL)
                return OPER_CANCEL;

            fileIterIndex++;
            archiveIterIndex++;
        }
    }
    // finish building the list
    for (; fileIterIndex < fileItemCount; fileIterIndex++)
    {
        if (AddFileUpdateInfo(updateList, fileIndexes[fileIterIndex]) == OPER_CANCEL)
            return OPER_CANCEL;
    }

    for (; archiveIterIndex < archiveItemCount; archiveIterIndex++)
    {
        if (AddArchiveUpdateInfo(updateList, archiveIndexes[archiveIterIndex]) == OPER_CANCEL)
            return OPER_CANCEL;
    }

    return OPER_OK;
} /* C7zClient::UpdateMakeUpdateList */
