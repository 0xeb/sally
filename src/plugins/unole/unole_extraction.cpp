// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "precomp.h"
#include "unole_plugin.h"
#include "unole2.rh2"

BOOL OpenStorage(const wchar_t* fileName, LPSTORAGE* storage, LPMALLOC* allocator);
constexpr size_t UnoleCopyBufferBytes = 65536;

BOOL CPluginInterfaceForArchiver::UnpackArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                CPluginDataInterfaceAbstract* pluginData, const wchar_t* targetDir,
                                                const wchar_t* archiveRoot, SalEnumSelection next, void* nextParam)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackArchive(, %ls, , %ls, %ls, ,)", fileName, targetDir, archiveRoot);

    // Batch extraction is not implemented. Reporting success here could make
    // the host delete the only archive without producing any output.
    return Error(E_NOTIMPL, IDS_EXTRPROGTITLE);
}

BOOL CPluginInterfaceForArchiver::UnpackOneFile(CSalamanderForOperationsAbstract* salamander,
                                                const wchar_t* fileName, CPluginDataInterfaceAbstract* pluginData,
                                                const wchar_t* nameInArchive, const CFileData* fileData,
                                                const wchar_t* targetDir, const wchar_t* newFileName,
                                                BOOL* renamingNotSupported)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackOneFile(, %ls, , %ls, , %ls, ,)", fileName,
                        nameInArchive, targetDir);

    if (newFileName != NULL)
    {
        *renamingNotSupported = TRUE;
        return FALSE;
    }

    LPSTORAGE pStorage;
    LPMALLOC pIMalloc;
    BOOL ret = TRUE;
    LPSTREAM pStream;

    if (!OpenStorage(fileName, &pStorage, &pIMalloc))
    {
        return FALSE;
    }
    // extract the file
    if (!wcschr(nameInArchive, L'\\'))
    {
        HRESULT hr;
        std::wstring targetFileName(targetDir);
        char* pBuf;
        HANDLE hFile;

        pBuf = (char*)malloc(UnoleCopyBufferBytes);
        if (pBuf)
        {
            hr = pStorage->OpenStream(nameInArchive, NULL, STGM_DIRECT | STGM_READ | STGM_SHARE_EXCLUSIVE, NULL, &pStream);
            if (SUCCEEDED(hr))
            {
                SPLSalPathAppendOwned(targetFileName, nameInArchive);
                hFile = CreateFileW(targetFileName.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW, 0, NULL);
                if (hFile != INVALID_HANDLE_VALUE)
                {
                    for (;;)
                    {
                        ULONG nBytesRead;
                        DWORD nBytesWritten;

                        hr = pStream->Read(pBuf, UnoleCopyBufferBytes, &nBytesRead);
                        if (FAILED(hr))
                        {
                            Error(hr, IDS_CANNOT_READ, targetFileName.c_str());
                            ret = FALSE;
                            break;
                        }
                        if (!nBytesRead)
                            break;
                        WriteFile(hFile, pBuf, nBytesRead, &nBytesWritten, NULL);
                        if (nBytesRead != nBytesWritten)
                        {
                            Error(GetLastError(), IDS_CANNOT_WRITE);
                            ret = FALSE;
                            break;
                        }
                    }
                    CloseHandle(hFile);
                }
                else
                {
                    Error(GetLastError(), IDS_CANNOT_CREATE_FILE, targetFileName.c_str());
                    ret = FALSE;
                }
                pStream->Release();
            }
            else
            {
                Error(hr, IDS_CANNOT_OPEN_STREAM, nameInArchive);
                ret = FALSE;
            }
        }
        else
        {
            SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LOWMEM).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            ret = FALSE;
        }
        if (pBuf)
            free(pBuf);
    }
    else
    {
        ret = Error(E_NOTIMPL, IDS_EXTRPROGTITLE);
    }
    pStorage->Release();
    pIMalloc->Release();

    return ret;
}

BOOL CPluginInterfaceForArchiver::UnpackWholeArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                     const wchar_t* mask, const wchar_t* targetDir, BOOL delArchiveWhenDone,
                                                     CDynamicString* archiveVolumes)
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForArchiver::UnpackWholeArchive(, %ls, %ls, %ls, %d,)",
                        fileName, mask, targetDir, delArchiveWhenDone);

    // Batch extraction is not implemented. Reporting success here could make
    // the host delete the only archive without producing any output.
    return Error(E_NOTIMPL, IDS_EXTRPROGTITLE);
}
