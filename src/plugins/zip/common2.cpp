// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <crtdbg.h>
#include <ostream>
#include <stdio.h>
#include <commctrl.h>

#include "spl_com.h"
#include "spl_base.h"
#include "spl_gen.h"
#include "spl_arc.h"
#include "spl_menu.h"
#include "dbg.h"

#include "array2.h"

#include "selfextr\\comdefs.h"
#include "config.h"
#include "typecons.h"
//#include "resource.h"
#include "zip.rh2"

#include "chicon.h"
#include "common.h"
#include "add_del.h"
#include "dialogs.h"
#include "sfxmake\\sfxmake.h"

int CZipCommon::ChangeDisk()
{
    CALL_STACK_MESSAGE1("CZipCommon::ChangeDisk()");
    int ret;
    bool retry = false;

    // small test to detect WinZip names
    if (CHDiskFlags & (CHD_FIRST | CHD_SEQNAMES))
    {
        std::wstring candidate = RenumberZipVolumeName(DiskNum + 1, ZipName.c_str(),
                                                       DiskNum == EOCentrDir.DiskNum,
                                                       (CHDiskFlags & CHD_WINZIP) != 0);
        if (SalamanderGeneral->SalGetFileAttributes(candidate.c_str()) == 0xFFFFFFFF)
        {
            // the file with the next number is not on the disk; try whether it might appear there
            // s invertovanym winzip flagem
            candidate = RenumberZipVolumeName(DiskNum + 1, ZipName.c_str(),
                                              DiskNum == EOCentrDir.DiskNum,
                                              (CHDiskFlags & CHD_WINZIP) == 0);
            if (SalamanderGeneral->SalGetFileAttributes(candidate.c_str()) != 0xFFFFFFFF &&
                CompareStringOrdinal(ZipName.c_str(), -1, candidate.c_str(), -1, TRUE) != CSTR_EQUAL)
                CHDiskFlags ^= CHD_WINZIP;
        }
    }

    bool useReadCache = false;
    bool bigFile = false;
    if (ZipFile)
    {
        useReadCache = ZipFile->InputBuffer != NULL;
        bigFile = ZipFile->BigFile != 0;
    }
    do
    {
        if (!retry && (CHDiskFlags & CHD_ALL ||
                       !Removable && Config.AutoExpandMV))
        {
            if (CHDiskFlags & CHD_SEQNAMES)
            {
                ZipName = RenumberZipVolumeName(DiskNum + 1, ZipName.c_str(),
                                                DiskNum == EOCentrDir.DiskNum,
                                                (CHDiskFlags & CHD_WINZIP) != 0);
            }
        }
        else
        {
            if (ChangeDiskDialog3(SalamanderGeneral->GetMsgBoxParent(),
                                  DiskNum + 1, DiskNum == EOCentrDir.DiskNum,
                                  ZipName, &CHDiskFlags) != IDOK)
            {
                return IDS_NODISPLAY;
            }
        }
        retry = false;
        if (ZipFile)
            CloseCFile(ZipFile);
        ZipFile = NULL;
        ret = CreateCFile(&ZipFile, ZipName.c_str(), GENERIC_READ, FILE_SHARE_READ,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, PE_NOSKIP, NULL,
                          bigFile, useReadCache);
        if (ret)
        {
            if (ret == ERR_LOWMEM)
                return IDS_LOWMEM;
            else
                retry = true;
        }
    } while (retry);
    CHDiskFlags &= ~CHD_FIRST;
    //if (*OriginalCurrentDir)  SetCurrentDirToZipPath();
    if (ArchiveVolumes != NULL)
        ArchiveVolumes->push_back(ZipName);
    return 0;
}

std::wstring CZipCommon::FindLastFile()
{
    CALL_STACK_MESSAGE1("CZipCommon::FindLastFile()");
    const size_t slash = ZipName.find_last_of(L'\\');
    const size_t nameStart = slash == std::wstring::npos ? 0 : slash + 1;
    const std::wstring directory = ZipName.substr(0, nameStart);
    const size_t dot = ZipName.find_last_of(L'.');
    const size_t extensionStart = dot != std::wstring::npos && dot >= nameStart ? dot : ZipName.size();
    const std::wstring name = ZipName.substr(nameStart, extensionStart - nameStart);
    const std::wstring extension = ZipName.substr(extensionStart);
    if (name.empty())
        return {};

    size_t digitStart = name.size();
    while (digitStart > 0 && name[digitStart - 1] >= L'0' && name[digitStart - 1] <= L'9')
        --digitStart;
    if (digitStart == 0 || digitStart == name.size())
        return {};

    const std::wstring mask = directory + name.substr(0, digitStart) + L"*" + extension;
    WIN32_FIND_DATAW data;
    HANDLE search = FindFirstFileW(mask.c_str(), &data);
    if (search == INVALID_HANDLE_VALUE)
        return {};

    int biggest = 0;
    std::wstring result;
    do
    {
        const std::wstring foundName = data.cFileName;
        const size_t foundDot = foundName.find_last_of(L'.');
        const size_t foundNameEnd = foundDot != std::wstring::npos ? foundDot : foundName.size();
        size_t foundDigitStart = foundNameEnd;
        while (foundDigitStart > 0 && foundName[foundDigitStart - 1] >= L'0' &&
               foundName[foundDigitStart - 1] <= L'9')
            --foundDigitStart;
        if (foundDigitStart > 0 && foundDigitStart < foundNameEnd)
        {
            const int number = _wtoi(foundName.c_str() + foundDigitStart);
            if (number > biggest && !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                biggest = number;
                result = directory + foundName;
            }
        }
    } while (FindNextFileW(search, &data));
    if (GetLastError() != ERROR_NO_MORE_FILES)
        result.clear();
    FindClose(search);
    return result;
}
