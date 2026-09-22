// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "regedt_registry_enum.h"

#include "common/Win32TextCodec.h"

#include <algorithm>
#include <cstring>

LONG RegedtEnumerateSubKeyOwned(HKEY key, DWORD index, DWORD initialCapacity,
                                std::wstring& name, FILETIME& time)
{
    try
    {
        size_t capacity = static_cast<size_t>(initialCapacity) + 1;
        for (;;)
        {
            if (capacity == 0 || capacity > MAXDWORD)
                return ERROR_ARITHMETIC_OVERFLOW;
            std::vector<wchar_t> buffer(capacity, L'\0');
            DWORD length = static_cast<DWORD>(buffer.size());
            FILETIME stagedTime{};
            const LONG result = RegEnumKeyExW(key, index, buffer.data(), &length, nullptr,
                                              nullptr, nullptr, &stagedTime);
            if (result == ERROR_SUCCESS)
            {
                std::wstring stagedName(buffer.data(), length);
                name.swap(stagedName);
                time = stagedTime;
                return ERROR_SUCCESS;
            }
            if (result != ERROR_MORE_DATA)
                return result;
            const size_t doubled = capacity <= MAXDWORD / 2 ? capacity * 2 : MAXDWORD;
            const size_t grown = (std::max)(doubled, static_cast<size_t>(length) + 1);
            if (grown <= capacity)
                return ERROR_ARITHMETIC_OVERFLOW;
            capacity = grown;
        }
    }
    catch (...)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}

LONG RegedtEnumerateValueOwned(HKEY key, DWORD index, DWORD initialNameCapacity,
                               DWORD initialDataCapacity, std::wstring& name,
                               DWORD& type, std::vector<BYTE>& data, DWORD& dataSize,
                               bool readData)
{
    try
    {
        size_t nameCapacity = static_cast<size_t>(initialNameCapacity) + 1;
        size_t valueCapacity = readData ? (std::max)(static_cast<size_t>(initialDataCapacity), size_t{1}) : 0;
        for (;;)
        {
            if (nameCapacity == 0 || nameCapacity > MAXDWORD || valueCapacity > MAXDWORD)
                return ERROR_ARITHMETIC_OVERFLOW;
            std::vector<wchar_t> nameBuffer(nameCapacity, L'\0');
            std::vector<BYTE> valueBuffer(valueCapacity);
            DWORD nameLength = static_cast<DWORD>(nameBuffer.size());
            DWORD valueLength = static_cast<DWORD>(valueBuffer.size());
            DWORD stagedType = 0;
            const LONG result = RegEnumValueW(key, index, nameBuffer.data(), &nameLength,
                                              nullptr, &stagedType,
                                              readData ? valueBuffer.data() : nullptr, &valueLength);
            if (result == ERROR_SUCCESS)
            {
                std::wstring stagedName(nameBuffer.data(), nameLength);
                if (readData)
                    valueBuffer.resize(valueLength);
                else
                    valueBuffer.clear();
                name.swap(stagedName);
                data.swap(valueBuffer);
                type = stagedType;
                dataSize = valueLength;
                return ERROR_SUCCESS;
            }
            if (result != ERROR_MORE_DATA)
                return result;
            const size_t doubledName = nameCapacity <= MAXDWORD / 2 ? nameCapacity * 2 : MAXDWORD;
            const size_t doubledValue = valueCapacity <= MAXDWORD / 2 ? valueCapacity * 2 : MAXDWORD;
            const size_t grownName = (std::max)(doubledName, static_cast<size_t>(nameLength) + 1);
            const size_t grownValue = readData ? (std::max)(doubledValue, static_cast<size_t>(valueLength)) : 0;
            if (grownName <= nameCapacity && (!readData || grownValue <= valueCapacity))
                return ERROR_ARITHMETIC_OVERFLOW;
            nameCapacity = grownName;
            valueCapacity = grownValue;
        }
    }
    catch (...)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}

LONG RegedtQueryKeyInfoOwned(HKEY key, std::wstring* className,
                             DWORD* maximumValueData, FILETIME* lastWriteTime) noexcept
{
    try
    {
        DWORD required = 0;
        DWORD stagedMaximumData = 0;
        FILETIME stagedTime{};
        LONG result = RegQueryInfoKeyW(key, nullptr, className ? &required : nullptr,
                                       nullptr, nullptr, nullptr,
                                       nullptr, nullptr, nullptr,
                                       maximumValueData ? &stagedMaximumData : nullptr,
                                       nullptr, lastWriteTime ? &stagedTime : nullptr);
        if (result != ERROR_SUCCESS)
            return result;

        std::wstring stagedClass;
        if (className)
        {
            size_t capacity = static_cast<size_t>(required) + 1;
            for (;;)
            {
                if (capacity == 0 || capacity > MAXDWORD)
                    return ERROR_ARITHMETIC_OVERFLOW;
                std::vector<wchar_t> buffer(capacity, L'\0');
                DWORD length = static_cast<DWORD>(buffer.size());
                result = RegQueryInfoKeyW(key, buffer.data(), &length, nullptr, nullptr,
                                          nullptr, nullptr, nullptr, nullptr,
                                          maximumValueData ? &stagedMaximumData : nullptr,
                                          nullptr, lastWriteTime ? &stagedTime : nullptr);
                if (result == ERROR_SUCCESS)
                {
                    stagedClass.assign(buffer.data(), length);
                    break;
                }
                if (result != ERROR_MORE_DATA)
                    return result;
                const size_t doubled = capacity <= MAXDWORD / 2 ? capacity * 2 : MAXDWORD;
                const size_t grown = (std::max)(doubled, static_cast<size_t>(length) + 1);
                if (grown <= capacity)
                    return ERROR_ARITHMETIC_OVERFLOW;
                capacity = grown;
            }
        }
        if (className)
            className->swap(stagedClass);
        if (maximumValueData)
            *maximumValueData = stagedMaximumData;
        if (lastWriteTime)
            *lastWriteTime = stagedTime;
        return ERROR_SUCCESS;
    }
    catch (...)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}

LONG RegedtQueryValueOwned(HKEY key, const wchar_t* valueName, DWORD initialCapacity,
                           DWORD& type, std::vector<BYTE>& data) noexcept
{
    try
    {
        DWORD required = 0;
        DWORD stagedType = 0;
        LONG result = RegQueryValueExW(key, valueName, nullptr, &stagedType, nullptr, &required);
        if (result != ERROR_SUCCESS)
            return result;

        size_t capacity = (std::max)(static_cast<size_t>(initialCapacity),
                                     static_cast<size_t>(required));
        for (;;)
        {
            if (capacity > MAXDWORD)
                return ERROR_ARITHMETIC_OVERFLOW;

            std::vector<BYTE> stagedData(capacity);
            DWORD stagedSize = static_cast<DWORD>(stagedData.size());
            DWORD attemptType = 0;
            result = RegQueryValueExW(key, valueName, nullptr, &attemptType,
                                      stagedData.empty() ? nullptr : stagedData.data(),
                                      &stagedSize);
            if (result == ERROR_SUCCESS)
            {
                stagedData.resize(stagedSize);
                data.swap(stagedData);
                type = attemptType;
                return ERROR_SUCCESS;
            }
            if (result != ERROR_MORE_DATA)
                return result;

            const size_t doubled = capacity == 0 ? 1 :
                                       (capacity <= MAXDWORD / 2 ? capacity * 2 : MAXDWORD);
            const size_t grown = (std::max)(doubled, static_cast<size_t>(stagedSize));
            if (grown <= capacity)
                return ERROR_ARITHMETIC_OVERFLOW;
            capacity = grown;
        }
    }
    catch (...)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
}

bool RegedtPrepareViewerBytes(DWORD type, const std::vector<BYTE>& registryData,
                              bool preferAnsi, UINT codePage,
                              std::vector<BYTE>& viewerData) noexcept
{
    try
    {
        std::vector<BYTE> staged(registryData);
        const bool stringValue = type == REG_SZ || type == REG_EXPAND_SZ ||
                                 type == REG_MULTI_SZ;
        if (preferAnsi && stringValue &&
            registryData.size() % sizeof(wchar_t) == 0)
        {
            std::wstring text(registryData.size() / sizeof(wchar_t), L'\0');
            if (!registryData.empty())
                memcpy(text.data(), registryData.data(), registryData.size());

            std::string encoded;
            if (Win32EncodeText(codePage, text, encoded).Succeeded())
                staged.assign(encoded.begin(), encoded.end());
        }
        viewerData.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
