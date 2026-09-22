// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression

#include "texts.rh2"
#include "array2.h"
#include "pakiface.h"
#include "pak_dll.h"

#ifdef PAK_DLL
// ****************************************************************************

HINSTANCE DLLInstance = NULL; //dll instance handle

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DLLInstance = hinstDLL;
        break;
    }

    case DLL_PROCESS_DETACH:
    {
        break;
    }
    }
    return TRUE; // DLL can be loaded
}

// Wide. This DLL owns the PAK format and parts of its interface are deliberately
// byte-domain (archive-internal entry names), but resource strings are not archive data - they
// are error text that the SPL displays through DialogError, so being narrow here only meant
// being mojibake-capable there.
std::wstring LangStr(int resID)
{
#ifdef _DEBUG
    // make sure nobody calls us before the resource handle is initialized
    if (DLLInstance == NULL)
        TRACE_E("LangStr: DLLInstance == NULL");
#endif
    const wchar_t* value = NULL;
    const int size = LoadStringW(DLLInstance, resID,
                                 reinterpret_cast<LPWSTR>(&value), 0);
    if (size > 0 && value != NULL)
        return std::wstring(value, static_cast<size_t>(size));
    TRACE_E("Error in LangStr(" << resID << ").");
    return L"ERROR LOADING STRING";
}

#endif //PAK_DLL

CPakIfaceAbstract* WINAPI PAKGetIFace()
{
    try
    {
        return new CPakIface;
    }
    catch (...)
    {
        return nullptr;
    }
}

void WINAPI PAKReleaseIFace(CPakIfaceAbstract* pakIFace)
{
    delete pakIFace;
    return;
}

// ****************************************************************************
//
// CPakIface
//

CPakIface::CPakIface() : DelRegions(256), ZeroSizedFiles(64)
{
    Callbacks = NULL;
    PakFile = INVALID_HANDLE_VALUE;
    PakDir = NULL;
}

/*
CPakIface::~CPakIface()
{
  if (PakDir) free(PakDir);
  if (PakFile != INVALID_HANDLE_VALUE) CloseHandle(PakFile);
  return TRUE
}
*/

BOOL CPakIface::Init(CPakCallbacksAbstract* callbacks)
{
    Callbacks = callbacks;
    return TRUE;
}

BOOL CPakIface::OpenPak(const wchar_t* fileName, DWORD mode)
{
    if (PakFile != INVALID_HANDLE_VALUE)
        return HandleError(0, IDS_PAK_ERROPEN2);

    while (PakFile == INVALID_HANDLE_VALUE)
    {
        PakFile = CreateFileW(fileName, mode, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
        if (PakFile != INVALID_HANDLE_VALUE)
            break;
        const std::wstring error = LastErrorString(GetLastError());
        if (!HandleError(HE_RETRY, IDS_PAK_ERROPEN, error.c_str()))
            return FALSE;
    }

    PakSize = GetFileSize(PakFile, NULL);
    if (PakSize == 0xFFFFFFFF)
    {
        const DWORD errorCode = GetLastError();
        ClosePak();
        const std::wstring error = LastErrorString(errorCode);
        return HandleError(0, IDS_PAK_ERRGETFILESIZE, error.c_str());
    }

    DirSize = 0;
    EmptyPak = FALSE;
    if (PakSize == 0)
    {
        EmptyPak = TRUE;
        return TRUE;
    }

    if (PakSize < sizeof(CPackHeader))
    {
        ClosePak();
        return HandleError(0, IDS_PAK_INVLAIDDATA);
    }

    if (!SafeRead(PakFile, &Header, sizeof(CPackHeader)))
    {
        ClosePak();
        return FALSE;
    }

    if (Header.Pack != 0x4b434150 || Header.DirOffset + Header.DirSize > PakSize ||
        Header.DirSize % sizeof(CPackEntry) != 0)
    {
        ClosePak();
        return HandleError(0, IDS_PAK_INVLAIDDATA);
    }

    DirSize = Header.DirSize / sizeof(CPackEntry);
    if (Header.DirSize == 0)
    {
        EmptyPak = TRUE;
        return TRUE;
    }

    PakDir = (CPackEntry*)malloc(Header.DirSize);
    if (!PakDir)
    {
        ClosePak();
        return HandleError(0, IDS_PAK_LOWMEMORY);
    }

    if (!SafeSeek(PakFile, Header.DirOffset) ||
        !SafeRead(PakFile, PakDir, Header.DirSize))
    {
        ClosePak();
        return FALSE;
    }

    return TRUE;
}

BOOL CPakIface::ClosePak()
{
    if (PakDir)
        free(PakDir);
    PakDir = NULL;
    DirSize = 0;
    if (PakFile != INVALID_HANDLE_VALUE)
        CloseHandle(PakFile);
    PakFile = INVALID_HANDLE_VALUE;
    return TRUE;
}

BOOL CPakIface::GetPakTime(FILETIME* lastWrite)
{
    GetFileTime(PakFile, NULL, NULL, lastWrite);
    int i = GetLastError();
    return TRUE;
}

BOOL CPakIface::GetName(const char* nameInPak, std::string& outName)
{
    const size_t inputLength = strnlen(nameInPak, sizeof(PakDir[DirPos].FileName));
    std::string staged;
    try
    {
        staged.reserve(inputLength);
        for (size_t i = 0; i < inputLength; ++i)
        {
            if (nameInPak[i] == '/')
            {
                staged.push_back('\\');
                continue;
            }
            if (nameInPak[i] == '.' &&
                (i == 0 || nameInPak[i - 1] == '/') && i + 2 < inputLength &&
                nameInPak[i + 1] == '.' && nameInPak[i + 2] == '/')
            {
                staged += PAK_UPDIR;
                i += 1;
                continue;
            }
            staged.push_back(nameInPak[i]);
        }
        if (staged.size() >= PAK_MAXPATH)
            return HandleError(0, IDS_PAK_TOOLONGNAME);
        outName.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return HandleError(0, IDS_PAK_LOWMEMORY);
    }
}

BOOL CPakIface::GetFirstFile(char* fileName, DWORD* size)
{
    DirPos = 0;
    if (EmptyPak)
    {
        fileName[0] = 0;
        return TRUE;
    }
    std::string name;
    if (!GetName(PakDir[DirPos].FileName, name))
        return FALSE;
    memcpy(fileName, name.c_str(), name.size() + 1);
    *size = PakDir[DirPos].Size;
    return TRUE;
}

BOOL CPakIface::GetNextFile(char* fileName, DWORD* size)
{
    DirPos++;
    if (DirPos >= DirSize)
    {
        fileName[0] = 0;
        return TRUE;
    }
    std::string name;
    if (!GetName(PakDir[DirPos].FileName, name))
        return FALSE;
    memcpy(fileName, name.c_str(), name.size() + 1);
    *size = PakDir[DirPos].Size;
    return TRUE;
}

BOOL CPakIface::FindFile(const char* fileName, DWORD* size)
{
    DWORD s = -1;
    std::string name;
    unsigned i;
    for (i = 0; i < DirSize; i++)
    {
        if (!GetName(PakDir[(int)i].FileName, name))
            return FALSE;
        if (CompareStringA(LOCALE_USER_DEFAULT, NORM_IGNORECASE, fileName, -1, name.c_str(), -1) == CSTR_EQUAL)
        {
            DirPos = i;
            s = PakDir[DirPos].Size;
            break;
        }
    }
    *size = s;
    return TRUE;
}

BOOL CPakIface::ExtractFile()
{
    char buffer[IOBUFSIZE];
    if (!SafeSeek(PakFile, PakDir[DirPos].Offset))
        return FALSE;
    DWORD left = PakDir[DirPos].Size;
    int read;
    while (left)
    {
        read = left > IOBUFSIZE ? IOBUFSIZE : left;
        if (!SafeRead(PakFile, buffer, read))
            return FALSE;
        if (!Callbacks->Write(buffer, read))
            return FALSE;
        if (!Callbacks->AddProgress(read))
            return FALSE;
        left -= read;
    }
    return TRUE;
}

BOOL CPakIface::HandleError(DWORD flags, int errorID, ...)
{
    va_list arglist;
    va_start(arglist, errorID);
    BOOL ret = Callbacks->HandleError(flags, errorID, arglist);
    va_end(arglist);
    return ret;
}

std::wstring CPakIface::LastErrorString(DWORD lastError) noexcept
{
    wchar_t* buffer = nullptr;
    const DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, lastError, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&buffer), 0, NULL);
    if (length == 0 || buffer == nullptr)
        return std::wstring();
    try
    {
        std::wstring result(buffer, static_cast<size_t>(length));
        LocalFree(buffer);
        return result;
    }
    catch (...)
    {
        LocalFree(buffer);
        return std::wstring();
    }
}

BOOL CPakIface::SafeSeek(HANDLE file, DWORD position)
{
    while (1)
    {
        if (SetFilePointer(file, position, NULL, FILE_BEGIN) != 0xFFFFFFFF)
            return TRUE;
        const std::wstring error = LastErrorString(GetLastError());
        if (!HandleError(HE_RETRY, IDS_PAK_ERRSETFILEPTR, error.c_str()))
            return FALSE;
    }
}

BOOL CPakIface::SafeRead(HANDLE file, void* buffer, DWORD size)
{
    if (size == 0)
        return TRUE;
    DWORD pos;
    while (1)
    {
        pos = SetFilePointer(file, 0, NULL, FILE_CURRENT);
        if (pos != 0xFFFFFFFF)
            break;
        const std::wstring error = LastErrorString(GetLastError());
        if (!HandleError(HE_RETRY, IDS_PAK_ERRGETFILEPTR, error.c_str()))
            return FALSE;
    }
    DWORD read;
    while (1)
    {
        if (ReadFile(file, buffer, size, &read, NULL) && read == size)
            return TRUE;
        const std::wstring error = LastErrorString(GetLastError());
        if (!HandleError(HE_RETRY, IDS_PAK_ERRREADFILE, error.c_str()))
            return FALSE;
        if (!SafeSeek(file, pos))
            return FALSE;
    }
}

BOOL CPakIface::SafeWrite(HANDLE file, void* buffer, DWORD size)
{
    if (size == 0)
        return TRUE;
    DWORD pos;
    while (1)
    {
        pos = SetFilePointer(file, 0, NULL, FILE_CURRENT);
        if (pos != 0xFFFFFFFF)
            break;
        const std::wstring error = LastErrorString(GetLastError());
        if (!HandleError(HE_RETRY, IDS_PAK_ERRGETFILEPTR, error.c_str()))
            return FALSE;
    }
    DWORD written;
    while (1)
    {
        if (WriteFile(file, buffer, size, &written, NULL))
            return TRUE;
        const std::wstring error = LastErrorString(GetLastError());
        if (!HandleError(HE_RETRY, IDS_PAK_ERRWRITEFILE, error.c_str()))
            return FALSE;
        if (!SafeSeek(file, pos))
            return FALSE;
    }
}
