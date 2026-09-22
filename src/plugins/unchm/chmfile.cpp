// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "dbg.h"

#include "chmlib/types.h"
//#include "lzx.h"
#include "unchm.h"
#include "chmfile.h"
#include "unchm_text.h"

#include "unchm.rh"
#include "unchm.rh2"
#include "lang\lang.rh"

static std::wstring FormatLocaleTime(const SYSTEMTIME& time)
{
    const int required = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &time, NULL, NULL, 0);
    if (required > 0)
    {
        std::wstring value(static_cast<size_t>(required), L'\0');
        const int written = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &time, NULL,
                                           value.data(), required);
        if (written > 0)
        {
            value.resize(static_cast<size_t>(written - 1));
            return value;
        }
    }
    return SPLFormatStringOwned(L"%u:%02u:%02u", time.wHour, time.wMinute,
                                time.wSecond);
}

static std::wstring FormatLocaleDate(const SYSTEMTIME& time)
{
    const int required = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time,
                                        NULL, NULL, 0);
    if (required > 0)
    {
        std::wstring value(static_cast<size_t>(required), L'\0');
        const int written = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time,
                                           NULL, value.data(), required);
        if (written > 0)
        {
            value.resize(static_cast<size_t>(written - 1));
            return value;
        }
    }
    return SPLFormatStringOwned(L"%u.%u.%u", time.wDay, time.wMonth, time.wYear);
}

std::wstring GetInfo(const FILETIME* lastWrite, CQuadWord size)
{
    CALL_STACK_MESSAGE2("GetInfo(, , 0x%I64X)", size.Value);

    SYSTEMTIME st;
    FILETIME ft;
    FileTimeToLocalFileTime(lastWrite, &ft);
    FileTimeToSystemTime(&ft, &st);

    const std::wstring date = FormatLocaleDate(st);
    const std::wstring time = FormatLocaleTime(st);
    const std::wstring number = SPLNumberToStrOwned(SalamanderGeneral, size);
    return SPLFormatStringOwned(L"%ls, %ls, %ls", number.c_str(), date.c_str(),
                                time.c_str());
}

BOOL SafeWriteFile(HANDLE hFile, LPVOID lpBuffer, DWORD nBytesToWrite, DWORD* pnBytesWritten, const wchar_t* fileName, HWND parent)
{
    while (!WriteFile(hFile, lpBuffer, nBytesToWrite, pnBytesWritten, NULL))
    {
        int lastErr = GetLastError();
        const std::wstring error = SPLGetErrorTextOwned(SalamanderGeneral, lastErr);
        if (SalamanderGeneral->DialogError(parent == NULL ? SalamanderGeneral->GetMsgBoxParent() : parent, BUTTONS_RETRYCANCEL,
                                           fileName, error.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_WRITEERROR).c_str()) != DIALOG_RETRY)
            return FALSE;
    }
    return TRUE;
}

// ****************************************************************************
//
// CCHMFile
//

CCHMFile::CCHMFile()
{
    CALL_STACK_MESSAGE1("CCHMFile::CCHMFile()");

    CHM = NULL;

    ChmOpen = NULL;
    ChmClose = NULL;
    ChmEnumerate = NULL;
    ChmRetrieveObject = NULL;

}

CCHMFile::~CCHMFile()
{
    CALL_STACK_MESSAGE1("CCHMFile::~CCHMFile()");
    Close();

}

BOOL CCHMFile::Open(const wchar_t* fileName, BOOL quiet /* = FALSE*/)
{
    CALL_STACK_MESSAGE3("CCHMFile::Open(%ls, %d)", fileName, quiet);

    std::wstring dllPath;
    if (!SPLGetModuleFileNameOwned(DLLInstance, dllPath) ||
        !SPLCutDirectoryOwned(SalamanderGeneral, dllPath))
        return FALSE;
    SPLSalPathAppendOwned(dllPath, L"chmlib.dll");

    HMODULE hDLL = LoadLibraryW(dllPath.c_str());
    if (hDLL != NULL)
    {
        SalamanderDebug->AddModuleWithPossibleMemoryLeaks(dllPath.c_str());
        ChmOpen = (CHM_OPEN_PROC)GetProcAddress(hDLL, "chm_open_w");
        ChmClose = (CHM_CLOSE_PROC)GetProcAddress(hDLL, "chm_close");
        ChmEnumerate = (CHM_ENUMERATE_PROC)GetProcAddress(hDLL, "chm_enumerate");
        ChmRetrieveObject = (CHM_RETRIEVE_OBJECT_PROC)GetProcAddress(hDLL, "chm_retrieve_object");

        // get filetime (must go before chm_open)
        HANDLE hCHM = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hCHM != INVALID_HANDLE_VALUE)
        {
            GetFileTime(hCHM, NULL, NULL, &FileTime);
            CloseHandle(hCHM);
        }
        else
        {
            // can not obtain last write, use current time
            SYSTEMTIME st;
            GetLocalTime(&st);
            SystemTimeToFileTime(&st, &FileTime);
        }

        CHM = ChmOpen(fileName);
        if (CHM == NULL)
            return Error(IDS_CANT_OPEN_FILE, quiet);
        else
        {
            FileName = fileName;
            return TRUE;
        }
    }
    else
        return FALSE;
}

BOOL CCHMFile::Close()
{
    CALL_STACK_MESSAGE1("CCHMFile::Close(, , ,)");

    if (CHM != NULL)
        ChmClose(CHM);

    return TRUE;
}

BOOL CCHMFile::AddFileDir(struct chmUnitInfo* ui, CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract*& pluginData)
{
    CALL_STACK_MESSAGE1("CCHMFile::AddFileDir(, , ,)");

    CFileData fd;

    char* path = ui->path;
    // convert '/' to '\'
    char* ch = path;
    while (*ch != '\0')
    {
        if (*ch == '/')
            *ch = '\\';
        ch++;
    }

    char* fileName = NULL;
    char* bs = strrchr(path, '\\');
    if (bs != NULL)
    {
        *bs = '\0';
        fileName = bs + 1;
    }
    else
    {
        fileName = ui->path;
    }

    // ignore empty files
    if (strlen(fileName) == 0)
        return TRUE;

    if (path[0] == '\\' && path <= bs)
        path++;

    std::wstring fileNameW;
    std::wstring pathW;
    if (!DecodeChmPathUtf8(fileName, fileNameW) || !DecodeChmPathUtf8(path, pathW))
        return Error(IDS_ERROR);
    fd.Name = SalamanderGeneral->DupStr(fileNameW.c_str());
    if (fd.Name == NULL)
    {
        Error(IDS_INSUFFICIENT_MEMORY);
        return FALSE;
    } // if

    fd.NameLen = static_cast<int>(wcslen(fd.Name));
    wchar_t* s = wcsrchr(fd.Name, L'.');
    if (s != NULL)
        fd.Ext = s + 1; // ".cvspass" is an extension in Windows
    else
        fd.Ext = fd.Name + fd.NameLen;

    fd.LastWrite = FileTime;

    struct chmUnitInfo* data = new chmUnitInfo;
    if (data == NULL)
    {
        free(fd.Name);
        Error(IDS_INSUFFICIENT_MEMORY);
        return FALSE;
    } // if

    *data = *ui;
    fd.PluginData = (DWORD_PTR)data;

    fd.DosName = NULL;

    fd.Attr = FILE_ATTRIBUTE_READONLY; // everything is read-only by default
    fd.Hidden = 0;

    fd.Size.SetUI64(ui->length);

    // file
    fd.IsLink = SalamanderGeneral->IsFileLink(fd.Ext);
    fd.IsOffline = 0;
    if (dir && !dir->AddFile(pathW.c_str(), fd, pluginData))
    {
        free(fd.Name);
        dir->Clear(pluginData);
        return Error(IDS_ERROR);
    }

    return TRUE;
}

struct SEnumObjHelper
{
    CCHMFile* File;
    CSalamanderDirectoryAbstract* Dir;
    CPluginDataInterfaceAbstract*& PluginData;

    SEnumObjHelper(CCHMFile* file, CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract*& pluginData) : PluginData(pluginData)
    {
        File = file;
        Dir = dir;
    }
};

int ChmEnumObjectsCallBack(struct chmFile* CHM, struct chmUnitInfo* ui, void* context)
{
    SEnumObjHelper* helper = (SEnumObjHelper*)context;
    if (helper->File->AddFileDir(ui, helper->Dir, helper->PluginData))
    {
        return CHM_ENUMERATOR_CONTINUE;
    }
    else
        return CHM_ENUMERATOR_FAILURE;
}

BOOL CCHMFile::EnumObjects(CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract*& pluginData)
{
    CALL_STACK_MESSAGE1("CCHMFile::EnumObjects( , ,)");

    SEnumObjHelper* helper = new SEnumObjHelper(this, dir, pluginData);
    ChmEnumerate(CHM, CHM_ENUMERATE_NORMAL, ChmEnumObjectsCallBack, (void*)helper);
    // for DEBUG purposes
    //  ChmEnumerate(CHM, CHM_ENUMERATE_ALL, ChmEnumObjectsCallBack, (void *) helper);
    delete helper;

    return TRUE;
}

int CCHMFile::ExtractObject(CSalamanderForOperationsAbstract* salamander, const wchar_t* srcPath, const wchar_t* path,
                            const CFileData* fileData, DWORD& silent, BOOL& toSkip)
{
    CALL_STACK_MESSAGE5("CCHMFile::ExtractObject( , %ls, %ls, , %u, %d)", srcPath, path, silent, toSkip);

    std::wstring nameInArc(FileName);
    SPLSalPathAppendOwned(nameInArc, srcPath);
    SPLSalPathAppendOwned(nameInArc, fileData->Name);

    ///
    std::wstring name(path);
    SPLSalPathAppendOwned(name, fileData->Name);

    FILETIME ft = fileData->LastWrite;
    const std::wstring fileInfo = GetInfo(&ft, fileData->Size);

    DWORD attrs = fileData->Attr;

    HANDLE file = SalamanderSafeFile->SafeFileCreate(name.c_str(), GENERIC_WRITE, FILE_SHARE_READ, attrs, FALSE,
                                                     SalamanderGeneral->GetMainWindowHWND(), nameInArc.c_str(), fileInfo.c_str(),
                                                     &silent, TRUE, &toSkip, NULL, 0, NULL, NULL);

    // set file time
    SetFileTime(file, &ft, &ft, &ft);

    // the overall operation can continue; skip only
    if (toSkip)
        return UNPACK_ERROR;

    // the overall operation cannot continue; cancel
    if (file == INVALID_HANDLE_VALUE)
        return UNPACK_CANCEL;

    BOOL whole = TRUE;

    BOOL ret = UNPACK_OK;
    chmUnitInfo* ui = (chmUnitInfo*)fileData->PluginData;
    CQuadWord remain = fileData->Size;
    DWORD bufferSize = 8192;
    BYTE* buffer = new BYTE[bufferSize];
    if (!buffer)
    {
        Error(IDS_INSUFFICIENT_MEMORY);
        return UNPACK_CANCEL;
    }

    LONGUINT64 offset = 0;
    while (remain.Value > 0)
    {
        LONGINT64 len = ChmRetrieveObject(CHM, ui, buffer, offset, bufferSize);
        if (len > 0)
        {
            ULONG written;
            SafeWriteFile(file, buffer, (DWORD)len, &written, name.c_str(), SalamanderGeneral->GetMainWindowHWND());
            offset += len;
            remain.Value -= len;
        }
        else
        {
            if (silent == 0)
            {
                int userAction = SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), ButtonFlags,
                                                                fileData->Name, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ERROR_UNPACKING).c_str(),
                                                                SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_READERROR).c_str());

                switch (userAction)
                {
                case DIALOG_CANCEL:
                    ret = UNPACK_CANCEL;
                    break;
                case DIALOG_SKIP:
                    ret = UNPACK_ERROR;
                    break;
                case DIALOG_SKIPALL:
                    ret = UNPACK_ERROR;
                    silent = 1;
                    break;
                }
            }

            whole = FALSE;
            break;
        }

        if (!salamander->ProgressAddSize((int)len, TRUE)) // delayedPaint==TRUE so we do not slow down
        {
            salamander->ProgressDialogAddText(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_CANCELOPER).c_str(), FALSE);
            salamander->ProgressEnableCancel(FALSE);

            ret = UNPACK_CANCEL;
            whole = FALSE;
            break; // interrupt the action
        }
    } // while

    delete[] buffer;

    CloseHandle(file);

    if (!whole)
    {
        if (ret == UNPACK_OK)
            ret = UNPACK_CANCEL;

        // because it is created with the read-only attribute, we must clear the R attribute
        // to allow the file to be deleted
        attrs &= ~FILE_ATTRIBUTE_READONLY;
        if (!SetFileAttributesW(name.c_str(), attrs))
            Error(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_CANT_SET_ATTRS).c_str(), GetLastError());

        // the user canceled the operation
        // delete the incomplete file afterwards
        if (!DeleteFileW(name.c_str()))
            Error(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_CANT_DELETE_TEMP_FILE).c_str(), GetLastError());
    }

    return ret;
    //  return UNPACK_CANCEL;
}

BOOL CCHMFile::UnpackDir(const wchar_t* dirName, const CFileData* fileData)
{
    CALL_STACK_MESSAGE3("CCHMFile::UnpackDir(%ls, %p)", dirName, fileData);

    if (!SalamanderGeneral->CheckAndCreateDirectory(dirName))
        return UNPACK_ERROR;

    /*
  DWORD attrs = fileData->Attr;

  // set attrs to dir
  if (Options.ClearReadOnly)
    // set ReadOnly Attribute
    attrs &= ~FILE_ATTRIBUTE_READONLY;

  if (!SetFileAttributes(dirName, attrs))
    Error(LangStr(IDS_CANT_SET_ATTRS).c_str(), GetLastError());
*/

    return UNPACK_OK;
}

int CCHMFile::ExtractAllObjects(CSalamanderForOperationsAbstract* salamander, std::wstring& srcPath,
                                CSalamanderDirectoryAbstract const* dir, const wchar_t* mask,
                                std::wstring& path, DWORD& silent, BOOL& toSkip)
{
    CALL_STACK_MESSAGE6("CCHMFile::ExtractAllObjects(, %ls, %ls, %ls, %u, %d)", srcPath.c_str(), mask, path.c_str(), silent, toSkip);

    int count = dir->GetFilesCount();
    int i;
    for (i = 0; i < count; i++)
    {
        CFileData const* file = dir->GetFile(i);
        salamander->ProgressDialogAddText(file->Name, TRUE); // delayedPaint==TRUE so we do not slow down

        salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE);
        salamander->ProgressSetTotalSize(file->Size + CQuadWord(1, 0), CQuadWord(-1, -1));

        if (SalamanderGeneral->AgreeMask(file->Name, mask, file->Ext[0] != 0))
        {
            if (ExtractObject(salamander, srcPath.c_str(), path.c_str(), file, silent, toSkip) == UNPACK_CANCEL ||
                !salamander->ProgressAddSize(1, TRUE))
                return UNPACK_CANCEL;
        }
    } // for

    count = dir->GetDirsCount();
    const size_t pathLen = path.size();
    const size_t srcPathLen = srcPath.size();
    for (i = 0; i < count; i++)
    {
        CFileData const* file = dir->GetDir(i);
        SPLSalPathAppendOwned(path, file->Name);
        if (UnpackDir(path.c_str(), file) == UNPACK_CANCEL)
            return UNPACK_CANCEL;

        CSalamanderDirectoryAbstract const* subDir = dir->GetSalDir(i);
        SPLSalPathAppendOwned(srcPath, file->Name);
        if (ExtractAllObjects(salamander, srcPath, subDir, mask, path, silent, toSkip) == UNPACK_CANCEL)
            return UNPACK_CANCEL;

        srcPath.resize(srcPathLen);
        path.resize(pathLen);
    }

    return UNPACK_OK;
}
