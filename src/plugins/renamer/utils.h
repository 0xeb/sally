// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifndef QWORD
typedef unsigned __int64 QWORD;
typedef QWORD* LPQWORD;
#define LODWORD(qw) ((DWORD)qw)
#define HIDWORD(qw) ((DWORD)(qw >> 32))
#define MAKEQWORD(lo, hi) (((QWORD)hi << 32) + lo)
#endif

void LoadHistory(HKEY regKey, const char* keyPattern, char** history,
                 CSalamanderRegistryAbstract* registry);
void SaveHistory(HKEY regKey, const char* keyPattern, char** history,
                 CSalamanderRegistryAbstract* registry);

// Bridges a narrow (char*) config field through the shared
// registry facade's wide-only REG_SZ contract (see reg_sz_narrow_bridge.h /
// ftp.cpp's SetValueSZ/GetValueSZ for the original of this pattern). The
// field itself stays narrow by design (feeds a still-narrow dialog control).
BOOL SetValueSZ(CSalamanderRegistryAbstract* registry, HKEY regKey, const wchar_t* name, const char* narrowValue);
BOOL GetValueSZ(CSalamanderRegistryAbstract* registry, HKEY regKey, const wchar_t* name, char* narrowBuf, int narrowBufSize);
BOOL GetValueSZ(CSalamanderRegistryAbstract* registry, HKEY regKey, const wchar_t* name, std::string& value);

BOOL FileError(HWND parent, const wchar_t* fileName, int error,
               BOOL retry, BOOL* skip, BOOL* skipAll, int title);

BOOL FileOverwrite(HWND parent, const wchar_t* fileName1, const wchar_t* fileData1,
                   const wchar_t* fileName2, const wchar_t* fileData2, DWORD attr,
                   int shquestion, int shtitle, BOOL* skip, DWORD* silent);

// ****************************************************************************

class CBuffer
{
public:
    CBuffer(BOOL persistent = FALSE)
    {
        Buffer = NULL;
        Allocated = 0;
        Persistent = persistent;
    }
    ~CBuffer() { Release(); }
    BOOL Reserve(size_t size);
    void Release()
    {
        if (Buffer)
            free(Buffer);
        Buffer = NULL;
        Allocated = 0;
    }
    size_t GetSize() { return Allocated; }

protected:
    void* Buffer;
    size_t Allocated;
    BOOL Persistent;
};

template <class DATA_TYPE>
class TBuffer : public CBuffer
{
public:
    TBuffer(BOOL persistent = FALSE) : CBuffer(persistent) { ; }
    BOOL Reserve(size_t size) { return CBuffer::Reserve(size * sizeof(DATA_TYPE)); }
    DATA_TYPE* Get() { return (DATA_TYPE*)Buffer; }
    size_t GetSize() { return Allocated / sizeof(DATA_TYPE); }
};

// ****************************************************************************

struct CVarStrHelpMenuItem
{
    int MenuItemStringID;
    const char* VariableName;
    BOOL (*FParameterGetValue)(HWND parent, char* buffer);
    CVarStrHelpMenuItem* SubMenu;
};

BOOL SelectVarStrVariable(HWND parent, int x, int y,
                          CVarStrHelpMenuItem* helpMenu, char* buffer, BOOL varStr);

// ****************************************************************************

const char* StrQChr(const char* start, const char* end, char q, char c);
BOOL IsValidInt(const char* begin, const char* end, BOOL isSigned);
BOOL IsValidFloat(const char* begin, const char* end);
int GetRegExpErrorID(CRegExpErrors err);
wchar_t* StripRoot(wchar_t* path, size_t rootLen);
enum CRenameSpec;
BOOL ValidateFileName(const wchar_t* name, int len, CRenameSpec spec,
                      BOOL* skip, BOOL* skipAll);
int CutTrailingDots(wchar_t* name, int len, CRenameSpec spec);
inline const wchar_t* GetNextPathComponent(const wchar_t* name)
{
    while (*name != L'\\' && *name != 0)
        name++;
    return name;
}
BOOL ShowOpenFileDialog(HWND parent, const wchar_t* title, const wchar_t* filter,
                        std::wstring& fileName);
