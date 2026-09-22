// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

#ifndef QWORD
typedef unsigned __int64 QWORD;
typedef QWORD* LPQWORD;
#define LODWORD(qw) ((DWORD)qw)
#define HIDWORD(qw) ((DWORD)(qw >> 32))
#define MAKEQWORD(lo, hi) (((QWORD)hi << 32) + lo)
#endif

WCHAR* DupStr(const WCHAR* str);

BOOL RegOperationError(int lastError, int error, int title, int keyRoot,
                       const wchar_t* keyName,
                       LPBOOL skip, LPBOOL skipAllErrors);

void LoadHistory(HKEY regKey, const wchar_t* keyPattern, std::vector<std::wstring>& history,
                 CSalamanderRegistryAbstract* registry);
void SaveHistory(HKEY regKey, const wchar_t* keyPattern, const std::vector<std::wstring>& history,
                 CSalamanderRegistryAbstract* registry);

BOOL TestForCancel();

BOOL ParseFullPath(WCHAR* path, WCHAR*& keyName, int& keyRoot);

BOOL ValidateHexString(LPWSTR text);

// ****************************************************************************

class CBuffer
{
public:
    CBuffer()
    {
        Buffer = NULL;
        Allocated = 0;
    }
    ~CBuffer() { Release(); }
    BOOL Reserve(int size);
    void Release()
    {
        if (Buffer)
            free(Buffer);
        Buffer = NULL;
        Allocated = 0;
    }
    int GetSize() { return Allocated; }

protected:
    void* Buffer;
    int Allocated;
};

template <class DATA_TYPE>
class TBuffer : public CBuffer
{
public:
    BOOL Reserve(int size) { return CBuffer::Reserve(size * sizeof(DATA_TYPE)); }
    DATA_TYPE* Get() { return (DATA_TYPE*)Buffer; }
};

// ****************************************************************************

BOOL ShowOpenFileDialog(HWND parent, const wchar_t* title, const wchar_t* filter,
                        std::wstring& fileName, BOOL save = FALSE);

BOOL RemoveFSNameFromPath(LPWSTR path);
// std::wstring overload. The raw form shifts the buffer left with memmove, which
// shortens the C string but leaves std::wstring::size() at the ORIGINAL length -
// so the owner kept an embedded NUL plus stale tail characters, size()/empty()
// answered about the old content, and the NUL later truncated the reg.exe
// command line built from it. Always prefer this form for a wstring owner.
BOOL RemoveFSNameFromPath(std::wstring& path);

wchar_t* ReplaceUnsafeCharacters(wchar_t* string);

//LPDLGTEMPLATE LoadDlgTemplate(int id, DWORD &size);
//LPDLGTEMPLATE ReplaceDlgTemplateFont(LPDLGTEMPLATE dlgTemplate, DWORD &size, LPCWSTR newFont);
