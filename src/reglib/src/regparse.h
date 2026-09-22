// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

typedef enum eRPE_ERROR
{
    RPE_OK,
    RPE_INVALID_FORMAT,
    RPE_NOT_REG_FILE,
    RPE_ROOT_INVALID_KEY,
    RPE_OUT_OF_MEMORY,
    RPE_INVALID_KEY,
    RPE_KEY_OPEN,
    RPE_KEY_CREATE,
    RPE_VALUE_GET_SIZE,
    RPE_VALUE_MISSING_QUOTE,
    RPE_VALUE_MISSING_ASSIG,
    RPE_VALUE_INVALID_TYPE,
    RPE_VALUE_GET,
    RPE_VALUE_SET,
    RPE_VALUE_DWORD,
    RPE_VALUE_STRING,
    RPE_VALUE_HEX,
    RPE_INVALID_MBCS,
} eRPE_ERROR;

#ifdef INSIDE_SALAMANDER
class CSalamanderRegistryExAbstractW : public CSalamanderRegistryAbstract
#else
class CSalamanderRegistryExAbstractW : public CSalamanderRegistryAbstractW
#endif
{
public: // Methods added by Jan Patera
    // Returns the subKeyIndex-th subkey name with dynamic UTF-16 ownership.
    virtual BOOL WINAPI EnumKey(HKEY key, DWORD subKeyIndex, std::wstring& name) = 0;

    // Returns the valIndex-th value name and optional value metadata/data.
    virtual BOOL WINAPI EnumValue(HKEY key, DWORD valIndex, std::wstring& name, LPDWORD valType, LPBYTE data, LPDWORD dataSize) = 0;

    // Remove keys and values with ".hidden" in name
    virtual void WINAPI RemoveHiddenKeysAndValues() = 0;

    // clears the key 'key' of all subkeys and values; if
    // 'doNotDeleteHiddenKeysAndValues' is TRUE, it does not delete keys and
    // values whose names end with ".hidden"; returns success
    virtual BOOL WINAPI ClearKeyEx(HKEY key, BOOL doNotDeleteHiddenKeysAndValues, BOOL* keyIsNotEmpty) = 0;

    virtual void WINAPI Release() = 0;
    // Serializes to an already-open byte stream. Filesystem path ownership and
    // long-path decoration remain with the caller's filesystem adapter.
    virtual BOOL WINAPI Dump(HANDLE outputFile, const wchar_t* clearKeyName) = 0;
};

CSalamanderRegistryExAbstractW* REG_SysRegistryFactoryW();
CSalamanderRegistryExAbstractW* REG_MemRegistryFactoryW();

eRPE_ERROR ParseRegistryFileW(wchar_t* buf, CSalamanderRegistryExAbstractW* registry, BOOL doNotDeleteHiddenKeysAndValues);
eRPE_ERROR CopyRegistryBranchW(const wchar_t* branch, CSalamanderRegistryExAbstractW* inRegistry, CSalamanderRegistryExAbstractW* outRegistry);

// Converts the byte-oriented REGEDIT4 input format from the current Windows ANSI
// code page. Native Registry Editor 5 input is already UTF-16 and is not transcoded.
eRPE_ERROR ConvertRegistryFileToUtf16(wchar_t** buffer, DWORD byteSize, DWORD& utf16ByteSize);

#define SizeOf(x) (sizeof(x) / sizeof(x[0]))
