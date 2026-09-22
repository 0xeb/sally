// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <string>

#include "tar.rh"
#include "tar.rh2"
#include "lang\lang.rh"

#include "../dlldefs.h"
#include "../fileio.h"
#include "deb.h"
#include "../../shared/plugin_local_path.h"

#define DEB_STREAM_NAME_CONTROL_W L"control"
#define DEB_STREAM_NAME_DATA_W L"data"

void ShowError(int errorID)
{
    int err = GetLastError();
    std::wstring txtbuf = LangStr(errorID).c_str();
    txtbuf += SPLGetErrorTextOwned(SalamanderGeneral, err);
    SalamanderGeneral->ShowMessageBox(txtbuf.c_str(), LangStr(IDS_GZERR_TITLE).c_str(), MSGBOX_ERROR);
}

CDEBArchive::CDEBArchive(const wchar_t* fileName, CSalamanderForOperationsAbstract* salamander)
{
    CALL_STACK_MESSAGE2("CDEBArchive::CDEBArchive(%ls)", fileName);

    controlArchive = dataArchive = NULL;
    bOK = FALSE;

    // Open input file
    std::wstring ioPath;
    HANDLE file = PreparePluginLocalPathForIo(fileName, ioPath)
                      ? CreateFileW(ioPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                    FILE_FLAG_SEQUENTIAL_SCAN, NULL)
                      : INVALID_HANDLE_VALUE;
    if (file == INVALID_HANDLE_VALUE)
    {
        ShowError(IDS_GZERR_FOPEN);
        return;
    }
    // Read file header
    char buffer[8];
    DWORD read;

    if (!ReadFile(file, buffer, sizeof(buffer), &read, NULL) || (read != sizeof(buffer)))
    {
        ShowError(IDS_ERR_FREAD);
        CloseHandle(file);
        return;
    }
    if (memcmp(buffer, "!<arch>\x0a", sizeof(buffer)) != 0)
    { // Not an "ar" archive
        CloseHandle(file);
        return;
    }

    // There should be 3 items: debian-binary, control.tar.gz, data.tar.gz
    SARBlock ARBlock;
    if (!ReadFile(file, &ARBlock, sizeof(ARBlock), &read, NULL) || (read != sizeof(ARBlock)))
    {
        ShowError(IDS_ERR_FREAD);
        CloseHandle(file);
        return;
    }

    // Check that the first item is valid, is named "debian-binary", and has 4 bytes
    DWORD SubArchiveSize;
    sscanf(ARBlock.FileSize, "%u", &SubArchiveSize);
    if ((ARBlock.FileMagic != DEB_AR_MAGIC) || _strnicmp(ARBlock.FileName, "debian-binary", sizeof("debian-binary") - 1) || (SubArchiveSize != 4))
    {
        // No -> reject the file
        CloseHandle(file);
        return;
    }
    // Skip the 4 data bytes that should contain version number "2.0\x0A"
    DWORD pos = SetFilePointer(file, SubArchiveSize, NULL, FILE_CURRENT);
    if (pos == INVALID_SET_FILE_POINTER)
    {
        ShowError(IDS_GZERR_SEEK);
        CloseHandle(file);
        return;
    }
    // Read the next item and check it is valid
    if (!ReadFile(file, &ARBlock, sizeof(ARBlock), &read, NULL) || (read != sizeof(ARBlock)))
    {
        ShowError(IDS_ERR_FREAD);
        CloseHandle(file);
        return;
    }
    if (ARBlock.FileMagic != DEB_AR_MAGIC)
    {
        CloseHandle(file);
        return;
    }
    sscanf(ARBlock.FileSize, "%u", &SubArchiveSize);
    pos += sizeof(ARBlock);

    // Open the fist subarchive
    controlArchive = new CArchive(fileName, salamander, pos, CQuadWord(SubArchiveSize, 0));
    if (!controlArchive->IsOk())
    {
        delete controlArchive;
        controlArchive = NULL;
        return;
    }
    // We are happy if at least one subarchive was recognized
    bOK = TRUE;

    // Skip the subarchive and read the 3rd and last subarchive
    if (SubArchiveSize & 1)
        SubArchiveSize++; // ARBlock starts on even positions
    pos = SetFilePointer(file, SubArchiveSize, NULL, FILE_CURRENT);
    if (pos == INVALID_SET_FILE_POINTER)
    {
        CloseHandle(file);
        return;
    }
    if (!ReadFile(file, &ARBlock, sizeof(ARBlock), &read, NULL) || (read != sizeof(ARBlock)))
    {
        ShowError(IDS_ERR_FREAD);
        CloseHandle(file);
        return;
    }
    if (ARBlock.FileMagic != DEB_AR_MAGIC)
    {
        CloseHandle(file);
        return;
    }
    sscanf(ARBlock.FileSize, "%u", &SubArchiveSize);
    dataArchive = new CArchive(fileName, salamander, pos + sizeof(ARBlock), CQuadWord(0, 0));
    if (!dataArchive->IsOk())
    {
        delete dataArchive;
        dataArchive = NULL;
    }
    CloseHandle(file);
}

CDEBArchive::~CDEBArchive(void)
{
    delete controlArchive;
    delete dataArchive;
}

BOOL CDEBArchive::ListArchive(const wchar_t* prefix, CSalamanderDirectoryAbstract* dir)
{
    BOOL ret = controlArchive->ListArchive(DEB_STREAM_NAME_CONTROL_W L"\\", dir);
    if (!dataArchive)
        return ret;
    return ret | dataArchive->ListArchive(DEB_STREAM_NAME_DATA_W L"\\", dir);
}

BOOL CDEBArchive::UnpackOneFile(const wchar_t* nameInArchive, const CFileData* fileData,
                                const wchar_t* targetPath, const wchar_t* newFileName)
{
    CALL_STACK_MESSAGE4("CDEBArchive::UnpackOneFile(%ls, , %ls, , %ls)", nameInArchive, targetPath, newFileName);
    const size_t controlPrefix = wcslen(DEB_STREAM_NAME_CONTROL_W L"\\");
    const size_t dataPrefix = wcslen(DEB_STREAM_NAME_DATA_W L"\\");
    if (!wcsncmp(nameInArchive, DEB_STREAM_NAME_CONTROL_W L"\\", controlPrefix))
    {
        nameInArchive += controlPrefix;
        return controlArchive->UnpackOneFile(nameInArchive, fileData, targetPath, newFileName);
    }
    if (!wcsncmp(nameInArchive, DEB_STREAM_NAME_DATA_W L"\\", dataPrefix))
    {
        nameInArchive += dataPrefix;
        return dataArchive->UnpackOneFile(nameInArchive, fileData, targetPath, newFileName);
    }
    _ASSERT(0);
    return FALSE;
}

BOOL CDEBArchive::UnpackArchive(const wchar_t* targetPath, const wchar_t* archiveRoot,
                                SalEnumSelection next, void* param)
{
    CALL_STACK_MESSAGE3("CDEBArchive::UnpackArchive(%ls, %ls, , )", targetPath, archiveRoot);
    const size_t controlPrefix = wcslen(DEB_STREAM_NAME_CONTROL_W);
    const size_t dataPrefix = wcslen(DEB_STREAM_NAME_DATA_W);
    if (!wcsncmp(archiveRoot, DEB_STREAM_NAME_CONTROL_W, controlPrefix))
    {
        archiveRoot += controlPrefix;
        if (archiveRoot[0] == L'\\')
            archiveRoot++;
        return controlArchive->UnpackArchive(targetPath, archiveRoot, next, param);
    }
    if (!wcsncmp(archiveRoot, DEB_STREAM_NAME_DATA_W, dataPrefix))
    {
        archiveRoot += dataPrefix;
        if (archiveRoot[0] == L'\\')
            archiveRoot++;
        return dataArchive->UnpackArchive(targetPath, archiveRoot, next, param);
    }
    _ASSERT(!*archiveRoot);

    // Looks like either entire control or entire data or both folders are to be extracted
    // Split names according the 2 subarchives
    const wchar_t* curNameW;
    BOOL isDir, isData, isControl;
    CQuadWord size;
    CQuadWord totalSize(0, 0);
    CNames dataNames, controlNames;
    isData = isControl = FALSE;
    while ((curNameW = next(NULL, 0, &isDir, &size, NULL, param, NULL)) != NULL)
    {
        const wchar_t* curName = curNameW;
        if (!wcsncmp(curName, DEB_STREAM_NAME_CONTROL_W, controlPrefix))
        {
            curName += controlPrefix;
            if (!*curName)
                curName = L"*";
            controlNames.AddName(curName, isDir, NULL, NULL);
            isControl = TRUE;
        }
        else if (!wcsncmp(curName, DEB_STREAM_NAME_DATA_W, dataPrefix))
        {
            curName += dataPrefix;
            if (!*curName)
                curName = L"*";
            dataNames.AddName(curName, isDir, NULL, NULL);
            isData = TRUE;
        }
        else
            _ASSERT(0); // Unexpected folder

        totalSize = totalSize + size;
    }
    // check free space; assume TestFreeSpace reports an appropriate message
    if (!SalamanderGeneral->TestFreeSpace(SalamanderGeneral->GetMsgBoxParent(),
                                          targetPath, totalSize, LangStr(IDS_TARERR_HEADER).c_str()))
        return FALSE;

    // and perform the actual extraction by name
    BOOL ret = FALSE;
    {
        std::wstring path;
        if (isControl)
        {
            path = targetPath;
            if (!path.empty() && path.back() != L'\\')
                path += L'\\';
            path += DEB_STREAM_NAME_CONTROL_W;
            ret = controlArchive->DoUnpackArchive(path.c_str(), archiveRoot, controlNames);
        }

        if (isData)
        {
            path = targetPath;
            if (!path.empty() && path.back() != L'\\')
                path += L'\\';
            path += DEB_STREAM_NAME_DATA_W;
            ret = dataArchive->DoUnpackArchive(path.c_str(), archiveRoot, dataNames) && (isControl ? ret : TRUE);
        }
    }
    return ret;
}

BOOL CDEBArchive::UnpackWholeArchive(const wchar_t* mask, const wchar_t* targetPath)
{
    BOOL isData = FALSE, isControl = FALSE;
    if (mask)
    {
        const size_t controlPrefix = wcslen(DEB_STREAM_NAME_CONTROL_W L"\\");
        const size_t dataPrefix = wcslen(DEB_STREAM_NAME_DATA_W L"\\");
        if (!_wcsnicmp(mask, DEB_STREAM_NAME_CONTROL_W L"\\", controlPrefix))
        {
            mask += controlPrefix;
            isControl = TRUE;
        }
        else if (!_wcsnicmp(mask, DEB_STREAM_NAME_DATA_W L"\\", dataPrefix))
        {
            mask += dataPrefix;
            isData = TRUE;
        }
        else if (mask[0] == L'*')
        {
            isControl = isData = TRUE;
        }
        else
        {
            return FALSE; // invalid mask
        }
    }
    else
    {
        isControl = isData = TRUE;
    }

    BOOL ret = FALSE;
    {
        std::wstring path;
        // controlArchive is always present
        if (isControl)
        {
            path = targetPath;
            if (!path.empty() && path.back() != L'\\')
                path += L'\\';
            path += DEB_STREAM_NAME_CONTROL_W;
            ret = controlArchive->UnpackWholeArchive(mask, path.c_str());
        }

        if (isData && dataArchive)
        {
            path = targetPath;
            if (!path.empty() && path.back() != L'\\')
                path += L'\\';
            path += DEB_STREAM_NAME_DATA_W;
            ret = dataArchive->UnpackWholeArchive(mask, path.c_str()) && (isControl ? ret : TRUE);
        }
    }
    return ret;
}
