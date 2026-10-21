// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "precomp.h"
#include "typecons.h"
#include "chicon.h"
#include "common.h"
#include "extract.h"
#include "add_del.h"
#include "zipdll.h"
#include "main.h"
#include "zip.rh2"

BOOL CPluginInterfaceForArchiver::UnpackArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                CPluginDataInterfaceAbstract* pluginData, const wchar_t* targetDir,
                                                const wchar_t* archiveRoot, SalEnumSelection next, void* nextParam)
try
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackArchive(, %ls, , %ls, %ls, ,)", fileName, targetDir, archiveRoot);

    std::string archiveRootA;
    if (archiveRoot != NULL && !TryWideToZipText(archiveRoot, archiveRootA))
    {
        return FALSE;
    }

    CRT_MEM_CHECKPOINT

    {
        const char* arcRoot = archiveRoot != NULL ? archiveRootA.c_str() : "";
        if (*arcRoot == '\\')
            arcRoot++;
        CZipUnpack unpack(fileName, arcRoot, salamander, NULL);
        if (unpack.ErrorID || unpack.UnpackArchive(targetDir, next, nextParam))
        {
            if (unpack.ErrorID != IDS_NODISPLAY)
                SalamanderGeneral->ShowMessageBox(LangStr(unpack.ErrorID).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            return FALSE;
        }
        if (unpack.UserBreak || unpack.Fatal)
            return FALSE;
    }

    CRT_MEM_DUMP_ALL_OBJECTS_SINCE

    return TRUE;
}
catch (...)
{
    return FALSE;
}

BOOL CPluginInterfaceForArchiver::PackToArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                const wchar_t* archiveRoot, BOOL move, const wchar_t* sourcePath,
                                                SalEnumSelection2 next, void* nextParam)
try
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForArchiver::PackToArchive(, %ls, %ls, %d, %ls, ,)", fileName,
                        archiveRoot, move, sourcePath);

    std::string archiveRootA;
    if (archiveRoot != NULL && !TryWideToZipText(archiveRoot, archiveRootA))
    {
        return FALSE;
    }

    CRT_MEM_CHECKPOINT

    {
        const char* arcRoot = archiveRoot != NULL ? archiveRootA.c_str() : "";
        if (*arcRoot == '\\')
            arcRoot++;
        CZipPack pack(fileName, arcRoot, salamander);

        if (pack.ErrorID || pack.PackToArchive(move, sourcePath, next, nextParam))
        {
            if (pack.ErrorID != IDS_NODISPLAY)
                SalamanderGeneral->ShowMessageBox(LangStr(pack.ErrorID).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            if (!pack.RecoverOK)
                SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERRRECOVER).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            return FALSE;
        }
        if (pack.UserBreak || pack.Fatal)
            return FALSE;
    }

    CRT_MEM_DUMP_ALL_OBJECTS_SINCE

    return TRUE;
}
catch (...)
{
    return FALSE;
}

BOOL CPluginInterfaceForArchiver::DeleteFromArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                    CPluginDataInterfaceAbstract* pluginData, const wchar_t* archiveRoot,
                                                    SalEnumSelection next, void* nextParam)
try
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForArchiver::DeleteFromArchive(, %ls, , %ls, ,)", fileName, archiveRoot);

    std::string archiveRootA;
    if (archiveRoot != NULL && !TryWideToZipText(archiveRoot, archiveRootA))
    {
        return FALSE;
    }

    CRT_MEM_CHECKPOINT

    {
        const char* arcRoot = archiveRoot != NULL ? archiveRootA.c_str() : "";
        if (*arcRoot == '\\')
            arcRoot++;
        CZipPack pack(fileName, arcRoot, salamander);

        if (pack.ErrorID || pack.DeleteFromArchive(next, nextParam))
        {
            if (pack.ErrorID != IDS_NODISPLAY)
                SalamanderGeneral->ShowMessageBox(LangStr(pack.ErrorID).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            if (!pack.RecoverOK)
                SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERRRECOVER).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);

            return FALSE;
        }
        if (pack.UserBreak || pack.Fatal)
            return FALSE;
    }

    CRT_MEM_DUMP_ALL_OBJECTS_SINCE

    return TRUE;
}
catch (...)
{
    return FALSE;
}
