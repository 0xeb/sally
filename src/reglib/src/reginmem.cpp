// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "regparse.h"

BOOL StrEndsWith(const wchar_t* txt, const wchar_t* pattern, size_t patternLen);

namespace RegLib
{

    typedef TIndirectArray<class CKey> TKeyList;
    typedef TDirectArray<class CValue> TValueList;

#ifndef REG_QWORD
// Original MSVC6 SDK
#define REG_QWORD 11
#endif

    typedef enum eVT_VALUE_TYPE
    {
        VT_STRING = REG_SZ,
        VT_DWORD = REG_DWORD,
        VT_BINARY = REG_BINARY,
        VT_QWORD = REG_QWORD,
        VT_MULTI_SZ = REG_MULTI_SZ,
        VT_EXPAND_SZ = REG_EXPAND_SZ,
        VT_INVALID = -1
    } eVT_VALUE_TYPE;

    class CValue
    {
    public:
        wchar_t* Name;
        eVT_VALUE_TYPE Type;
        union
        {
            wchar_t* str;
            DWORD dw;
            LPBYTE data;
        } Value;
        DWORD Size; // For other types than VT_DWORD and VT_STRING

        CValue(const wchar_t* name, eVT_VALUE_TYPE type, LPCVOID data, DWORD size);
        ~CValue();

        // Uses the default copy constructor (memcpy). After adding the object to the array,
        // call Invalidate(); otherwise the object's data will also be freed for the copy stored
        // in the array.

        BOOL IsOK();

        void Invalidate();
    };

    class CKey
    {
    public:
        wchar_t* Name;
        CKey* pParent;
        int nCount;
        TKeyList SubKeys;
        TValueList Values;

        CKey(CKey* parent, const wchar_t* name);
        ~CKey();
        int AddRef();
        int Release();
        BOOL IsOK() const;

        BOOL RemoveKey(CKey* pChild);
        CKey* GetKey(const wchar_t* name);

        BOOL RemoveValue(CValue* pChild);
        CValue* GetValue(const wchar_t* name);

        BOOL Clear();

        virtual BOOL Dump(HANDLE hFile, const std::wstring& fullKeyName);
        virtual BOOL RemoveHiddenKeysAndValues();
    };

    class CRootKey : public CKey
    {
    public:
        CRootKey() : CKey(NULL, L"") {}

        virtual BOOL Dump(HANDLE hFile, const std::wstring& fullKeyName);
    };

    class CMemoryRegistry : public CSalamanderRegistryExAbstractW
    {
    public:
        CMemoryRegistry();
        virtual ~CMemoryRegistry() {}

        virtual BOOL WINAPI ClearKey(HKEY key);
        virtual BOOL WINAPI CreateKey(HKEY key, const wchar_t* name, HKEY& createdKey);
        virtual BOOL WINAPI OpenKey(HKEY key, const wchar_t* name, HKEY& openedKey);
        virtual void WINAPI CloseKey(HKEY key);
        virtual BOOL WINAPI DeleteKey(HKEY key, const wchar_t* name);
        virtual BOOL WINAPI GetValue(HKEY key, const wchar_t* name, DWORD type, LPVOID data, DWORD dataSize);
        virtual BOOL WINAPI SetValue(HKEY key, const wchar_t* name, DWORD type, LPCVOID data, DWORD dataSize);
        virtual BOOL WINAPI DeleteValue(HKEY key, const wchar_t* name);
        virtual BOOL WINAPI GetSize(HKEY key, const wchar_t* name, DWORD type, DWORD& bufferSize);

        virtual BOOL WINAPI EnumKey(HKEY key, DWORD subKeyIndex, std::wstring& name);
        virtual BOOL WINAPI EnumValue(HKEY key, DWORD valIndex, std::wstring& name, LPDWORD valType, LPBYTE data, LPDWORD dataSize);

        virtual void WINAPI RemoveHiddenKeysAndValues();
        virtual BOOL WINAPI ClearKeyEx(HKEY key, BOOL /*doNotDeleteHiddenKeysAndValues*/, BOOL* /*keyIsNotEmpty*/) { return ClearKey(key); }

        virtual void WINAPI Release();
        virtual BOOL WINAPI Dump(HANDLE outputFile, const wchar_t* clearKeyName);

    private:
        CRootKey RootKey;

        virtual BOOL CreateOpenKey(HKEY key, const wchar_t* name, HKEY& openedKey, BOOL bCreate);
    };

    static BOOL WriteWide(HANDLE hFile, std::wstring_view text)
    {
        if (text.size() > SIZE_MAX / sizeof(wchar_t))
            return FALSE;
        const BYTE* next = reinterpret_cast<const BYTE*>(text.data());
        size_t remaining = text.size() * sizeof(wchar_t);
        const DWORD maxAlignedWrite = MAXDWORD - MAXDWORD % sizeof(wchar_t);
        while (remaining != 0)
        {
            const DWORD requested = static_cast<DWORD>(std::min<size_t>(remaining, maxAlignedWrite));
            DWORD written = 0;
            if (!WriteFile(hFile, next, requested, &written, NULL) || written == 0 ||
                written > requested || written % sizeof(wchar_t) != 0)
                return FALSE;
            next += written;
            remaining -= written;
        }
        return TRUE;
    }

    static void AppendEscaped(std::wstring& output, const wchar_t* text)
    {
        while (*text)
        {
            switch (*text)
            {
            case L'\\':
                output += L"\\\\";
                break;
            case L'\"':
                output += L"\\\"";
                break;
            case L'\r':
                output += L"\\r";
                break;
            case L'\n':
                output += L"\\n";
                break;
            default:
                output += *text;
            }
            text++;
        }
    }

    static BOOL WriteString(HANDLE hFile, const wchar_t* text)
    {
        std::wstring escaped = L"\"";
        AppendEscaped(escaped, text);
        escaped += L'\"';
        return WriteWide(hFile, escaped);
    }

    static void AppendHex(std::wstring& output, DWORD value, size_t minimumDigits)
    {
        static constexpr wchar_t hex[] = L"0123456789abcdef";
        wchar_t digits[sizeof(value) * 2];
        size_t count = 0;
        do
        {
            digits[count++] = hex[value & 0x0f];
            value >>= 4;
        } while (value != 0);
        while (count < minimumDigits)
            digits[count++] = L'0';
        while (count != 0)
            output += digits[--count];
    }

    //////////////////////////// CValue ////////////////////////////

    CValue::CValue(const wchar_t* name, eVT_VALUE_TYPE type, LPCVOID data, DWORD size)
    {
        Name = _wcsdup(name);
        Type = type;
        Value.data = NULL;
        Size = 0;
        switch (type)
        {
        case VT_DWORD:
            if (data == NULL || size != sizeof(DWORD))
            {
                Type = VT_INVALID;
                break;
            }
            Value.dw = *(DWORD*)data;
            break;
        case VT_STRING:
            if ((DWORD)-1 == size)
            {
                if (data == NULL)
                {
                    Type = VT_INVALID;
                    break;
                }
                const size_t length = wcslen((const wchar_t*)data);
                if (length > (MAXDWORD / sizeof(wchar_t)) - 1)
                {
                    Type = VT_INVALID;
                    break;
                }
                size = static_cast<DWORD>((length + 1) * sizeof(wchar_t));
            }
            if (size % sizeof(wchar_t) != 0 || (size != 0 && data == NULL))
            {
                Type = VT_INVALID;
                break;
            }
            if (!size || ((const wchar_t*)data)[size / sizeof(wchar_t) - 1] != L'\0')
            {
                if (size > MAXDWORD - sizeof(wchar_t))
                {
                    Type = VT_INVALID;
                    break;
                }
                size += sizeof(wchar_t); // Ensure NUL termination
            }
            Value.str = (wchar_t*)malloc(size);
            if (Value.str)
            {
                if (size > sizeof(wchar_t))
                    memcpy(Value.str, data, size - sizeof(wchar_t));
                Value.str[size / sizeof(wchar_t) - 1] = 0;
            }
            // NOTE: failed malloc checked by calling IsOK()
            Size = size;
            break;
        case VT_INVALID:
            Value.data = NULL;
            break;
        case VT_BINARY:
        default:
            if (size != 0 && data == NULL)
            {
                Type = VT_INVALID;
                break;
            }
            Value.data = (LPBYTE)malloc(size);
            Size = size;
            if (Value.data && size != 0)
                memcpy(Value.data, data, size);
            // NOTE: failed malloc checked by calling IsOK()
            break;
        }
    }

    CValue::~CValue()
    {
        if (Name)
        {
            free(Name);
            Name = NULL;
        }

        switch (Type)
        {
        case VT_STRING:
            if (Value.str)
                free(Value.str);
            break;
        case VT_INVALID:
        case VT_DWORD:
            break;
        case VT_BINARY:
        default:
            if (Value.data)
                free(Value.data);
            break;
        }
    }

    void CValue::Invalidate()
    {
        // Avoid freeing data
        Name = NULL;
        Type = VT_INVALID;
    }

    BOOL CValue::IsOK()
    {
        if (!Name)
            return FALSE;
        if (VT_STRING == Type)
            return Value.str != NULL;
        if ((VT_DWORD != Type) && !Value.data)
            return FALSE;
        return TRUE;
    }

    //////////////////////////// CKey ////////////////////////////
    CKey::CKey(CKey* parent, const wchar_t* name) : SubKeys(1, 2, dtNoDelete), Values(1, 2)
    {
        Name = _wcsdup(name);
        pParent = parent;
        if (pParent)
        {
            pParent->SubKeys.Add(this);
        }
        nCount = 1;
    }

    CKey::~CKey()
    {
        Clear();
        if (Name)
            free(Name);
        if (pParent)
            pParent->RemoveKey(this);
    }

    int CKey::AddRef()
    {
        return nCount++;
    }

    BOOL CKey::IsOK() const
    {
        return Name != NULL && SubKeys.IsGood() && Values.IsGood() &&
               (pParent == NULL || pParent->SubKeys.IsGood());
    }

    int CKey::Release()
    {
        int old = nCount--;

        if (!nCount)
            delete this;
        return old;
    }

    BOOL CKey::RemoveKey(CKey* pChild)
    {
        int i;
        for (i = 0; i < SubKeys.Count; i++)
        {
            if (SubKeys[i] == pChild)
            {
                SubKeys.Detach(i);
                return TRUE;
            }
        }
        return FALSE;
    }

    CKey* CKey::GetKey(const wchar_t* name)
    {
        int i;
        for (i = 0; i < SubKeys.Count; i++)
        {
            if (!_wcsicmp(SubKeys[i]->Name, name))
            {
                return SubKeys[i];
            }
        }
        return NULL;
    }

    BOOL CKey::RemoveValue(CValue* pChild)
    {
        int i;
        for (i = 0; i < Values.Count; i++)
        {
            if (&Values[i] == pChild)
            {
                Values.Delete(i);
                return TRUE;
            }
        }
        return FALSE;
    }

    CValue* CKey::GetValue(const wchar_t* name)
    {
        int i;
        for (i = 0; i < Values.Count; i++)
        {
            if (!_wcsicmp(Values[i].Name, name))
            {
                return &Values[i];
            }
        }
        return NULL;
    }

    BOOL CKey::Clear()
    {
        int i;
        for (i = SubKeys.Count - 1; i >= 0; i--)
        {
            SubKeys[i]->Release();
        }
        Values.DestroyMembers();
        return TRUE;
    }

    BOOL CKey::Dump(HANDLE hFile, const std::wstring& fullKeyName)
    {
        std::wstring currentKeyName = fullKeyName;
        if (!currentKeyName.empty())
            currentKeyName += L'\\';
        AppendEscaped(currentKeyName, Name);

        if (pParent->pParent)
        {
            // Do not write out root keys, regedit cannot parse such .REG files...
            std::wstring heading = L"\r\n[";
            heading += currentKeyName;
            heading += L"]\r\n";
            if (!WriteWide(hFile, heading))
                return FALSE;
        }
        int i;
        for (i = 0; i < Values.Count; i++)
        {
            CValue* pValue = &Values[i];

            if (*pValue->Name)
            {
                if (!WriteString(hFile, pValue->Name))
                {
                    return FALSE;
                }
            }
            else
            {
                // The (Default) value of the key
                if (!WriteWide(hFile, L"@"))
                    return FALSE;
            }
            if (!WriteWide(hFile, L"="))
                return FALSE;

            switch (pValue->Type)
            {
            case VT_DWORD:
            {
                std::wstring line = L"dword:";
                AppendHex(line, pValue->Value.dw, 8);
                line += L"\r\n";
                if (!WriteWide(hFile, line))
                    return FALSE;
                break;
            }
            case VT_STRING:
                if (!WriteString(hFile, pValue->Value.str))
                    return FALSE;
                if (!WriteWide(hFile, L"\r\n"))
                    return FALSE;
                break;
            case VT_INVALID:
                break;
            case VT_BINARY:
            default:
            {
                std::wstring line;
                if (pValue->Type == VT_BINARY)
                    line = L"hex:";
                else
                {
                    line = L"hex(";
                    AppendHex(line, static_cast<DWORD>(pValue->Type), 1);
                    line += L"):";
                }
                size_t lineBudget = 82;
                const size_t overhead = line.size() + wcslen(pValue->Name) + 3;
                lineBudget = overhead < lineBudget ? lineBudget - overhead : 6;
                LPBYTE src = pValue->Value.data;
                size_t srclen = pValue->Size;
                while (srclen--)
                {
                    static constexpr wchar_t hex[] = L"0123456789abcdef";
                    line += hex[*src >> 4];
                    line += hex[*src++ & 0x0f];
                    if (srclen)
                    {
                        line += L',';
                        lineBudget -= std::min<size_t>(lineBudget, 3);
                        if (lineBudget < 6)
                        {
                            line += L"\\\r\n";
                            if (!WriteWide(hFile, line))
                                return FALSE;
                            line = L"  ";
                            lineBudget = 80;
                        }
                    }
                }
                line += L"\r\n";
                if (!WriteWide(hFile, line))
                    return FALSE;
                break;
            }
            }
        }
        int j;
        for (j = 0; j < SubKeys.Count; j++)
        {
            if (!SubKeys[j]->Dump(hFile, currentKeyName))
                return FALSE;
        }
        return TRUE;
    } /* CKey::Dump */

    BOOL CRootKey::Dump(HANDLE hFile, const std::wstring& fullKeyName)
    {
        int j;
        for (j = 0; j < SubKeys.Count; j++)
        {
            if (!SubKeys[j]->Dump(hFile, fullKeyName))
                return FALSE;
        }
        return TRUE;
    } /* CRootKey::Dump */

    BOOL CKey::RemoveHiddenKeysAndValues()
    {
        if (::StrEndsWith(Name, L".hidden", SizeOf(L".hidden") - 1))
        {
            return TRUE; // delete this key
        }
        int j;
        for (j = 0; j < SubKeys.Count; j++)
        {
            if (SubKeys[j]->RemoveHiddenKeysAndValues())
            {
                SubKeys[j]->Release();
                j--;
            }
        }
        int i;
        for (i = 0; i < Values.Count; i++)
        {
            if (::StrEndsWith(Values[i].Name, L".hidden", SizeOf(L".hidden") - 1))
            {
                Values.Delete(i);
                i--;
            }
        }
        return FALSE;
    }

    //////////////////////////// CMemoryRegistry //////////////////////////

    CMemoryRegistry::CMemoryRegistry()
    {
    }

    void CMemoryRegistry::RemoveHiddenKeysAndValues()
    {
        RootKey.RemoveHiddenKeysAndValues();
    }

    void CMemoryRegistry::Release()
    {
        delete this;
    }

    BOOL CMemoryRegistry::Dump(HANDLE hFile, const wchar_t* clearKeyName)
    {
        if (hFile != NULL && hFile != INVALID_HANDLE_VALUE)
        {
            BOOL ret = FALSE;
            try
            {
                ret = WriteWide(hFile, L"\xFEFFWindows Registry Editor Version 5.00\r\n");
                if (clearKeyName != NULL && ret)
                {
                    std::wstring clearDirective = L"\r\n[-";
                    clearDirective += clearKeyName;
                    clearDirective += L"]\r\n";
                    ret = WriteWide(hFile, clearDirective);
                }
                if (ret)
                    ret = RootKey.Dump(hFile, std::wstring());
            }
            catch (...)
            {
                ret = FALSE;
            }
            return ret;
        }
        return FALSE;
    }

    BOOL CMemoryRegistry::ClearKey(HKEY key)
    {
        CKey* pKey = (CKey*)key;

        if (!pKey)
            return FALSE;

        return pKey->Clear();
    }

    BOOL CMemoryRegistry::CreateOpenKey(HKEY key, const wchar_t* name, HKEY& createdKey, BOOL bCreate)
    {
        try
        {
        CKey* pParentKey = NULL;
        const wchar_t* pRootName = NULL;

        // Convert switch to if-else for Clang compatibility (HKEY_* are not constant expressions)
        if (key == HKEY_CLASSES_ROOT)
            pRootName = L"HKEY_CLASSES_ROOT";
        else if (key == HKEY_CURRENT_USER)
            pRootName = L"HKEY_CURRENT_USER";
        else if (key == HKEY_LOCAL_MACHINE)
            pRootName = L"HKEY_LOCAL_MACHINE";
        else if (key == HKEY_USERS)
            pRootName = L"HKEY_USERS";
        else if (key == HKEY_CURRENT_CONFIG)
            pRootName = L"HKEY_CURRENT_CONFIG";
        else if (key == HKEY_DYN_DATA)
            pRootName = L"HKEY_DYN_DATA";
        else if (key == HKEY_PERFORMANCE_DATA)
            pRootName = L"HKEY_PERFORMANCE_DATA";
        else if (key == NULL)
            return FALSE; // Invalid parent key
        else
            pParentKey = (CKey*)key;
        if (pRootName)
        {
            pParentKey = RootKey.GetKey(pRootName);
            if (!pParentKey)
            {
                if (!bCreate)
                {
                    return FALSE;
                }
                pParentKey = new CKey(&RootKey, pRootName);
                if (!pParentKey || !pParentKey->IsOK())
                {
                    delete pParentKey;
                    // OOM :-(
                    return FALSE;
                }
            }
        }

        if (*name == '\\')
        {
            // invalid key name (it cannot start with '\\')
            return FALSE;
        }
        while (*name)
        {
            const wchar_t* separator = wcschr(name, L'\\');
            const size_t segmentLength = separator != NULL ? static_cast<size_t>(separator - name) : wcslen(name);
            std::wstring keyName(name, segmentLength);
            name += segmentLength;
            if (*name == L'\\')
                name++;
            if (!keyName.empty())
            { // ignore doubled '\\' in 'name'
                CKey* pKey = pParentKey->GetKey(keyName.c_str());
                if (!pKey)
                {
                    if (!bCreate)
                    {
                        return FALSE;
                    }
                    pKey = new CKey(pParentKey, keyName.c_str());
                    if (!pKey || !pKey->IsOK())
                    {
                        delete pKey;
                        // OOM :-(
                        return FALSE;
                    }
                }
                pParentKey = pKey;
            }
        }
        pParentKey->AddRef();
        createdKey = (HKEY)pParentKey;

        return TRUE;
        }
        catch (...)
        {
            return FALSE;
        }
    }

    BOOL CMemoryRegistry::CreateKey(HKEY key, const wchar_t* name, HKEY& createdKey)
    {
        return CreateOpenKey(key, name, createdKey, TRUE);
    }

    BOOL CMemoryRegistry::OpenKey(HKEY key, const wchar_t* name, HKEY& openedKey)
    {
        return CreateOpenKey(key, name, openedKey, FALSE);
    }

    void CMemoryRegistry::CloseKey(HKEY key)
    {
        CKey* pKey = (CKey*)key;

        if (!pKey)
            return;
        pKey->Release();
    }

    BOOL CMemoryRegistry::DeleteKey(HKEY key, const wchar_t* name)
    {
        CKey* pKey = (CKey*)key;

        if (!pKey)
            return FALSE;

        // Get the subkey
        pKey = pKey->GetKey(name);
        if (!pKey)
            return FALSE;

        // The key will be deleted when its counter reaches zero when all handles are closed
        pKey->Release();

        return TRUE;
    }

    BOOL CMemoryRegistry::GetValue(HKEY key, const wchar_t* name, DWORD type, LPVOID buffer, DWORD bufferSize)
    {
        CKey* pKey = (CKey*)key;

        if (!pKey)
            return FALSE;

        CValue* pValue = pKey->GetValue(name);
        if (pValue)
        {
            if (pValue->Type != (eVT_VALUE_TYPE)type)
                return FALSE;
            switch (pValue->Type)
            {
            case VT_DWORD:
                if (bufferSize != sizeof(DWORD))
                    return FALSE;
                *(DWORD*)buffer = pValue->Value.dw;
                break;
            case VT_STRING:
                if (bufferSize < pValue->Size)
                    return FALSE;
                wcscpy((wchar_t*)buffer, pValue->Value.str);
                break;
            case VT_INVALID:
                return FALSE;
            case VT_BINARY:
            default:
                if (bufferSize < pValue->Size)
                    return FALSE;
                memcpy(buffer, pValue->Value.data, pValue->Size);
                break;
            }
            return TRUE;
        }

        return FALSE;
    }

    BOOL CMemoryRegistry::SetValue(HKEY key, const wchar_t* name, DWORD type, LPCVOID data, DWORD dataSize)
    {
        CKey* pKey = (CKey*)key;

        if (!pKey)
            return FALSE;

        int oldValueIndex = -1;
        for (int i = 0; i < pKey->Values.Count; i++)
            if (!_wcsicmp(pKey->Values[i].Name, name))
            {
                oldValueIndex = i;
                break;
            }

        CValue value(name, (eVT_VALUE_TYPE)type, data, dataSize);
        if (value.IsOK())
        {
            if (pKey->Values.Add(value) == ULONG_MAX)
                return FALSE;
            value.Invalidate(); // All pointers were copied in Add() -> do not free them now
            if (oldValueIndex != -1)
                pKey->Values.Delete(oldValueIndex);
            return TRUE;
        }
        // Out of memory :-(

        return FALSE;
    }

    BOOL CMemoryRegistry::DeleteValue(HKEY key, const wchar_t* name)
    {
        CKey* pKey = (CKey*)key;

        if (!pKey)
            return FALSE;

        CValue* pValue = pKey->GetValue(name);
        if (pValue)
        {
            pKey->RemoveValue(pValue);
            return TRUE;
        }

        return FALSE;
    }

    BOOL CMemoryRegistry::GetSize(HKEY key, const wchar_t* name, DWORD type, DWORD& bufferSize)
    {
        CKey* pKey = (CKey*)key;

        if (!pKey)
            return FALSE;

        CValue* pValue = pKey->GetValue(name);
        if (pValue)
        {
            if (pValue->Type != (eVT_VALUE_TYPE)type)
                return FALSE;
            switch (pValue->Type)
            {
            case VT_DWORD:
                bufferSize = sizeof(DWORD);
                break;
            case VT_INVALID:
                return FALSE;
            case VT_STRING:
            case VT_BINARY:
            default:
                bufferSize = pValue->Size;
                break;
            }
            return TRUE;
        }

        return FALSE;
    }

    BOOL CMemoryRegistry::EnumKey(HKEY key, DWORD subKeyIndex, std::wstring& name)
    {
        CKey* pKey = (CKey*)key;

        if (!pKey)
            return FALSE;

        if ((int)subKeyIndex >= pKey->SubKeys.Count)
            return FALSE;

        CKey* pSubKey = pKey->SubKeys[subKeyIndex];

        try
        {
            std::wstring result(pSubKey->Name);
            name.swap(result);
        }
        catch (...)
        {
            return FALSE;
        }

        return TRUE;
    }

    BOOL CMemoryRegistry::EnumValue(HKEY key, DWORD valIndex, std::wstring& name, LPDWORD valType, LPBYTE data, LPDWORD dataSize)
    {
        CKey* pKey = (CKey*)key;

        if (!pKey)
            return FALSE;

        if ((int)valIndex >= pKey->Values.Count)
            return FALSE;

        CValue* pValue = &pKey->Values[valIndex];

        if (data && dataSize)
        {
            DWORD size;
            LPCVOID srcData;

            switch (pValue->Type)
            {
            case VT_DWORD:
                size = sizeof(DWORD);
                srcData = &pValue->Value.dw;
                break;
            case VT_INVALID:
                size = 0;
                srcData = NULL;
                break;
            case VT_STRING:
                size = pValue->Size;
                srcData = pValue->Value.str;
                break;
            case VT_BINARY:
            default:
                size = pValue->Size;
                srcData = pValue->Value.data;
                break;
            }

            if (*dataSize < size)
                return FALSE;
            memcpy(data, srcData, size);
            *dataSize = size;
        }

        try
        {
            std::wstring result(pValue->Name);
            name.swap(result);
        }
        catch (...)
        {
            return FALSE;
        }

        if (valType)
            *valType = pValue->Type;
        return TRUE;
    }

} // namespace RegLib

BOOL StrEndsWith(const wchar_t* txt, const wchar_t* pattern, size_t patternLen)
{
    if (txt == NULL || pattern == NULL)
        return FALSE;
    size_t txtLen = wcslen(txt);
    return txtLen >= patternLen && _wcsicmp(txt + txtLen - patternLen, pattern) == 0;
}

CSalamanderRegistryExAbstractW* REG_MemRegistryFactoryW()
{
    try
    {
        return new RegLib::CMemoryRegistry();
    }
    catch (...)
    {
        return NULL;
    }
}
