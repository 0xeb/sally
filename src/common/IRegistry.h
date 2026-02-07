// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <windows.h>

// Result of registry operations
struct RegistryResult
{
    bool success;
    LONG errorCode;  // Win32 error code (ERROR_SUCCESS on success)

    static RegistryResult Ok() { return {true, ERROR_SUCCESS}; }
    static RegistryResult Error(LONG err) { return {false, err}; }

    // Convenience: check if key/value not found (common case)
    bool notFound() const { return errorCode == ERROR_FILE_NOT_FOUND; }
};

// Registry value types (maps to REG_* constants)
enum class RegValueType : DWORD
{
    None = REG_NONE,
    String = REG_SZ,
    ExpandString = REG_EXPAND_SZ,
    Binary = REG_BINARY,
    DWord = REG_DWORD,
    QWord = REG_QWORD,
    MultiString = REG_MULTI_SZ
};

// Abstract interface for registry operations
// Enables mocking for tests and potential future abstraction (e.g., INI file backend)
class IRegistry
{
public:
    virtual ~IRegistry() = default;

    // Key operations
    // Opens existing key for reading
    virtual RegistryResult OpenKeyRead(HKEY root, const wchar_t* subKey, HKEY& outKey) = 0;

    // Opens existing key for read/write
    virtual RegistryResult OpenKeyReadWrite(HKEY root, const wchar_t* subKey, HKEY& outKey) = 0;

    // Creates key (or opens if exists)
    virtual RegistryResult CreateKey(HKEY root, const wchar_t* subKey, HKEY& outKey) = 0;

    // Closes key handle
    virtual void CloseKey(HKEY key) = 0;

    // Deletes a key (must be empty on some Windows versions)
    virtual RegistryResult DeleteKey(HKEY root, const wchar_t* subKey) = 0;

    // Recursively deletes a key and all subkeys/values
    virtual RegistryResult DeleteKeyRecursive(HKEY root, const wchar_t* subKey) = 0;

    // Value operations - Read
    virtual RegistryResult GetString(HKEY key, const wchar_t* valueName, std::wstring& value) = 0;
    virtual RegistryResult GetDWord(HKEY key, const wchar_t* valueName, DWORD& value) = 0;
    virtual RegistryResult GetQWord(HKEY key, const wchar_t* valueName, uint64_t& value) = 0;
    virtual RegistryResult GetBinary(HKEY key, const wchar_t* valueName, std::vector<uint8_t>& value) = 0;

    // Generic read - returns type and raw data
    virtual RegistryResult GetValue(HKEY key, const wchar_t* valueName,
                                    RegValueType& type, std::vector<uint8_t>& data) = 0;

    // Value operations - Write
    virtual RegistryResult SetString(HKEY key, const wchar_t* valueName, const wchar_t* value) = 0;
    virtual RegistryResult SetDWord(HKEY key, const wchar_t* valueName, DWORD value) = 0;
    virtual RegistryResult SetQWord(HKEY key, const wchar_t* valueName, uint64_t value) = 0;
    virtual RegistryResult SetBinary(HKEY key, const wchar_t* valueName,
                                     const void* data, size_t size) = 0;

    // Delete value
    virtual RegistryResult DeleteValue(HKEY key, const wchar_t* valueName) = 0;

    // Enumeration
    virtual RegistryResult EnumSubKeys(HKEY key, std::vector<std::wstring>& subKeys) = 0;
    virtual RegistryResult EnumValues(HKEY key, std::vector<std::wstring>& valueNames) = 0;

    // Query if key/value exists
    virtual bool KeyExists(HKEY root, const wchar_t* subKey) = 0;
    virtual bool ValueExists(HKEY key, const wchar_t* valueName) = 0;
};

// Global registry instance - default is Win32 implementation
extern IRegistry* gRegistry;

// Returns the default Win32 implementation
IRegistry* GetWin32Registry();

// ANSI helpers for migrating existing code
inline std::wstring AnsiToWideReg(const char* str)
{
    if (!str || !*str) return L"";
    int len = MultiByteToWideChar(CP_ACP, 0, str, -1, nullptr, 0);
    if (len == 0) return L"";
    std::wstring wide;
    wide.resize(len);
    MultiByteToWideChar(CP_ACP, 0, str, -1, &wide[0], len);
    wide.resize(len - 1);
    return wide;
}

// ANSI helper: Open key for reading
inline RegistryResult OpenKeyReadA(IRegistry* reg, HKEY root, const char* subKey, HKEY& outKey)
{
    return reg->OpenKeyRead(root, AnsiToWideReg(subKey).c_str(), outKey);
}

// ANSI helper: Create key
inline RegistryResult CreateKeyA(IRegistry* reg, HKEY root, const char* subKey, HKEY& outKey)
{
    return reg->CreateKey(root, AnsiToWideReg(subKey).c_str(), outKey);
}

// ANSI helper: Get string value (returns ANSI string)
inline RegistryResult GetStringA(IRegistry* reg, HKEY key, const char* valueName,
                                 char* buffer, DWORD bufferSize)
{
    std::wstring wideValue;
    auto result = reg->GetString(key, AnsiToWideReg(valueName).c_str(), wideValue);
    if (result.success && buffer && bufferSize > 0)
    {
        int written = WideCharToMultiByte(CP_ACP, 0, wideValue.c_str(), -1,
                                          buffer, bufferSize, nullptr, nullptr);
        if (written == 0 && !wideValue.empty())
            return RegistryResult::Error(GetLastError());
    }
    return result;
}

// ANSI helper: Get DWORD value
inline RegistryResult GetDWordA(IRegistry* reg, HKEY key, const char* valueName, DWORD& value)
{
    return reg->GetDWord(key, AnsiToWideReg(valueName).c_str(), value);
}

// ANSI helper: Set string value
inline RegistryResult SetStringA(IRegistry* reg, HKEY key, const char* valueName, const char* value)
{
    return reg->SetString(key, AnsiToWideReg(valueName).c_str(),
                          AnsiToWideReg(value).c_str());
}

// ANSI helper: Set DWORD value
inline RegistryResult SetDWordA(IRegistry* reg, HKEY key, const char* valueName, DWORD value)
{
    return reg->SetDWord(key, AnsiToWideReg(valueName).c_str(), value);
}
