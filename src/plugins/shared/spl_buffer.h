// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>

#ifdef _MSC_VER
#pragma pack(push, enter_include_spl_buffer)
#pragma pack(4)
#endif

#include <limits>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

// Call-scoped writable UTF-16 storage for the live plugin ABI. The caller owns
// the allocation and supplies Reserve; the callee may update only Data,
// Capacity (through Reserve), Length, and the characters in Data. Capacity
// includes the terminating NUL and Length excludes it. A failed Reserve must
// leave the complete record and its allocation unchanged.
struct CSalamanderStringBuffer;
typedef BOOL(WINAPI* FSalamanderStringBufferReserve)(CSalamanderStringBuffer* buffer,
                                                      DWORD minimumCapacity);

struct CSalamanderStringBuffer
{
    wchar_t* Data;
    DWORD Length;
    DWORD Capacity;
    void* OwnerContext;
    FSalamanderStringBufferReserve Reserve;
};

// Call-scoped writable storage for UTF-16 text ranges. Offsets and lengths are
// measured in wchar_t elements and are never packed into 16-bit halves. The
// caller owns the allocation exactly as for CSalamanderStringBuffer.
struct CSalamanderTextRange
{
    DWORD Offset;
    DWORD Length;
};

struct CSalamanderTextRangeBuffer;
typedef BOOL(WINAPI* FSalamanderTextRangeBufferReserve)(
    CSalamanderTextRangeBuffer* buffer, DWORD minimumCapacity);

struct CSalamanderTextRangeBuffer
{
    CSalamanderTextRange* Data;
    DWORD Count;
    DWORD Capacity;
    void* OwnerContext;
    FSalamanderTextRangeBufferReserve Reserve;
};

#define SAL_STRING_BUFFER_NPOS 0xFFFFFFFFu

namespace sally::plugin_abi
{
inline bool IsValidStringBuffer(const CSalamanderStringBuffer& buffer,
                                bool requireReserve = true) noexcept
{
    if (requireReserve && buffer.Reserve == NULL)
        return false;
    if (buffer.Capacity == 0)
        return buffer.Data == NULL && buffer.Length == 0;
    return buffer.Data != NULL && buffer.Length < buffer.Capacity &&
           buffer.Data[buffer.Length] == L'\0';
}

inline bool ReadStringBuffer(const CSalamanderStringBuffer& buffer,
                             std::wstring& value) noexcept
{
    if (!IsValidStringBuffer(buffer))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    try
    {
        std::wstring staged(buffer.Data != NULL ? buffer.Data : L"", buffer.Length);
        value.swap(staged);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
}

inline bool ReserveStringBuffer(CSalamanderStringBuffer& buffer,
                                DWORD minimumCapacity) noexcept
{
    if (!IsValidStringBuffer(buffer) || minimumCapacity == 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    if (buffer.Capacity >= minimumCapacity)
        return true;

    wchar_t* const oldData = buffer.Data;
    const DWORD oldLength = buffer.Length;
    const DWORD oldCapacity = buffer.Capacity;
    void* const oldContext = buffer.OwnerContext;
    FSalamanderStringBufferReserve const oldReserve = buffer.Reserve;

    SetLastError(ERROR_SUCCESS);
    if (!oldReserve(&buffer, minimumCapacity))
    {
        if (buffer.Data != oldData || buffer.Length != oldLength ||
            buffer.Capacity != oldCapacity || buffer.OwnerContext != oldContext ||
            buffer.Reserve != oldReserve)
            SetLastError(ERROR_INVALID_DATA);
        else if (GetLastError() == ERROR_SUCCESS)
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }

    if (buffer.OwnerContext != oldContext || buffer.Reserve != oldReserve ||
        buffer.Length != oldLength || !IsValidStringBuffer(buffer) ||
        buffer.Capacity < minimumCapacity)
    {
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }
    return true;
}

inline bool WriteStringBuffer(CSalamanderStringBuffer& buffer,
                              const std::wstring& value) noexcept
{
    if (value.size() >= (std::numeric_limits<DWORD>::max)())
    {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return false;
    }
    const DWORD required = static_cast<DWORD>(value.size() + 1);
    if (!ReserveStringBuffer(buffer, required))
        return false;
    std::wmemmove(buffer.Data, value.c_str(), value.size() + 1);
    buffer.Length = static_cast<DWORD>(value.size());
    return true;
}

inline bool IsValidTextRangeBuffer(
    const CSalamanderTextRangeBuffer& buffer,
    bool requireReserve = true) noexcept
{
    if (requireReserve && buffer.Reserve == NULL)
        return false;
    if (buffer.Capacity == 0)
        return buffer.Data == NULL && buffer.Count == 0;
    return buffer.Data != NULL && buffer.Count <= buffer.Capacity;
}

inline bool ReserveTextRangeBuffer(CSalamanderTextRangeBuffer& buffer,
                                   DWORD minimumCapacity) noexcept
{
    if (!IsValidTextRangeBuffer(buffer))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    if (buffer.Capacity >= minimumCapacity)
        return true;

    CSalamanderTextRange* const oldData = buffer.Data;
    const DWORD oldCount = buffer.Count;
    const DWORD oldCapacity = buffer.Capacity;
    void* const oldContext = buffer.OwnerContext;
    FSalamanderTextRangeBufferReserve const oldReserve = buffer.Reserve;

    SetLastError(ERROR_SUCCESS);
    if (!oldReserve(&buffer, minimumCapacity))
    {
        if (buffer.Data != oldData || buffer.Count != oldCount ||
            buffer.Capacity != oldCapacity || buffer.OwnerContext != oldContext ||
            buffer.Reserve != oldReserve)
            SetLastError(ERROR_INVALID_DATA);
        else if (GetLastError() == ERROR_SUCCESS)
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }

    if (buffer.OwnerContext != oldContext || buffer.Reserve != oldReserve ||
        buffer.Count != oldCount || !IsValidTextRangeBuffer(buffer) ||
        buffer.Capacity < minimumCapacity)
    {
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }
    return true;
}

inline bool WriteTextAndRanges(CSalamanderStringBuffer& textBuffer,
                               CSalamanderTextRangeBuffer& rangeBuffer,
                               const std::wstring& text,
                               const CSalamanderTextRange* ranges,
                               size_t rangeCount) noexcept
{
    if (!IsValidStringBuffer(textBuffer) ||
        !IsValidTextRangeBuffer(rangeBuffer) ||
        text.size() >= (std::numeric_limits<DWORD>::max)() ||
        rangeCount > (std::numeric_limits<DWORD>::max)() ||
        (rangeCount != 0 && ranges == NULL))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    for (size_t index = 0; index < rangeCount; ++index)
    {
        if (ranges[index].Offset > text.size() ||
            ranges[index].Length > text.size() - ranges[index].Offset)
        {
            SetLastError(ERROR_INVALID_DATA);
            return false;
        }
    }

    const DWORD textCapacity = static_cast<DWORD>(text.size() + 1);
    const DWORD publishedRangeCount = static_cast<DWORD>(rangeCount);
    if (!ReserveStringBuffer(textBuffer, textCapacity) ||
        !ReserveTextRangeBuffer(rangeBuffer, publishedRangeCount))
        return false;

    std::wmemmove(textBuffer.Data, text.c_str(), text.size() + 1);
    if (publishedRangeCount != 0)
        std::memmove(rangeBuffer.Data, ranges,
                     rangeCount * sizeof(CSalamanderTextRange));
    textBuffer.Length = static_cast<DWORD>(text.size());
    rangeBuffer.Count = publishedRangeCount;
    return true;
}

inline bool WriteTextAndRanges(
    CSalamanderStringBuffer& textBuffer,
    CSalamanderTextRangeBuffer& rangeBuffer,
    const std::wstring& text,
    const std::vector<CSalamanderTextRange>& ranges) noexcept
{
    return WriteTextAndRanges(textBuffer, rangeBuffer, text,
                              ranges.data(), ranges.size());
}
} // namespace sally::plugin_abi

#ifdef _MSC_VER
// spl_buffer.h is commonly included while the SDK's ABI records are packed to
// four bytes. Keep the C++ convenience owner at the compiler's normal class
// alignment; only the record that crosses the DLL boundary is pack(4).
#pragma pack(push, enter_owner_spl_buffer)
#pragma pack(8)
#endif

// Convenience owner for native v108 callers. Its storage remains entirely in
// the caller's module; only the POD record and Reserve callback cross the DLL
// boundary. Do not retain Buffer() after the API call returns.
class CSalamanderStringBufferOwner
{
public:
    explicit CSalamanderStringBufferOwner(const wchar_t* initialValue = L"") noexcept
        : BufferValue{}, Valid(false)
    {
        BufferValue.OwnerContext = this;
        BufferValue.Reserve = ReserveStorage;
        Valid = Assign(initialValue != NULL ? initialValue : L"");
    }

    explicit CSalamanderStringBufferOwner(const std::wstring& initialValue) noexcept
        : BufferValue{}, Valid(false)
    {
        BufferValue.OwnerContext = this;
        BufferValue.Reserve = ReserveStorage;
        Valid = Assign(initialValue.c_str(), initialValue.size());
    }

    CSalamanderStringBufferOwner(const CSalamanderStringBufferOwner&) = delete;
    CSalamanderStringBufferOwner& operator=(const CSalamanderStringBufferOwner&) = delete;
    CSalamanderStringBufferOwner(CSalamanderStringBufferOwner&&) = delete;
    CSalamanderStringBufferOwner& operator=(CSalamanderStringBufferOwner&&) = delete;

    bool IsValid() const noexcept { return Valid; }
    CSalamanderStringBuffer* Buffer() noexcept { return Valid ? &BufferValue : NULL; }
    const CSalamanderStringBuffer* Buffer() const noexcept { return Valid ? &BufferValue : NULL; }

    bool GetValue(std::wstring& value) const noexcept
    {
        if (!Valid || BufferValue.Data == NULL || BufferValue.Capacity == 0 ||
            BufferValue.Length >= BufferValue.Capacity ||
            BufferValue.Data[BufferValue.Length] != L'\0')
            return false;
        try
        {
            std::wstring staged(BufferValue.Data, BufferValue.Length);
            value.swap(staged);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        catch (const std::length_error&)
        {
            return false;
        }
    }

private:
    static BOOL WINAPI ReserveStorage(CSalamanderStringBuffer* buffer,
                                      DWORD minimumCapacity) noexcept
    {
        if (buffer == NULL || buffer->OwnerContext == NULL)
            return FALSE;
        CSalamanderStringBufferOwner* owner =
            static_cast<CSalamanderStringBufferOwner*>(buffer->OwnerContext);
        if (!owner->Valid || buffer != &owner->BufferValue ||
            buffer->Reserve != ReserveStorage || buffer->Data != owner->Storage.data() ||
            buffer->Capacity != owner->Storage.size() ||
            buffer->Length >= buffer->Capacity ||
            buffer->Data[buffer->Length] != L'\0')
            return FALSE;
        if (minimumCapacity <= buffer->Capacity)
            return TRUE;
        try
        {
            owner->Storage.resize(minimumCapacity, L'\0');
            buffer->Data = owner->Storage.data();
            buffer->Capacity = minimumCapacity;
            return TRUE;
        }
        catch (const std::bad_alloc&)
        {
            return FALSE;
        }
        catch (const std::length_error&)
        {
            return FALSE;
        }
    }

    bool Assign(const wchar_t* value) noexcept
    {
        return Assign(value, wcslen(value));
    }

    bool Assign(const wchar_t* value, size_t length) noexcept
    {
        if (length >= (std::numeric_limits<DWORD>::max)())
            return false;
        try
        {
            Storage.assign(value, value + length);
            Storage.push_back(L'\0');
            BufferValue.Data = Storage.data();
            BufferValue.Length = static_cast<DWORD>(length);
            BufferValue.Capacity = static_cast<DWORD>(Storage.size());
            return true;
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        catch (const std::length_error&)
        {
            return false;
        }
    }

    CSalamanderStringBuffer BufferValue;
    std::vector<wchar_t> Storage;
    bool Valid;
};

class CSalamanderTextRangeBufferOwner
{
public:
    CSalamanderTextRangeBufferOwner() noexcept : BufferValue{}, Valid(true)
    {
        BufferValue.OwnerContext = this;
        BufferValue.Reserve = ReserveStorage;
    }

    CSalamanderTextRangeBufferOwner(const CSalamanderTextRangeBufferOwner&) = delete;
    CSalamanderTextRangeBufferOwner& operator=(const CSalamanderTextRangeBufferOwner&) = delete;
    CSalamanderTextRangeBufferOwner(CSalamanderTextRangeBufferOwner&&) = delete;
    CSalamanderTextRangeBufferOwner& operator=(CSalamanderTextRangeBufferOwner&&) = delete;

    bool IsValid() const noexcept { return Valid; }
    CSalamanderTextRangeBuffer* Buffer() noexcept
    {
        return Valid ? &BufferValue : NULL;
    }
    const CSalamanderTextRangeBuffer* Buffer() const noexcept
    {
        return Valid ? &BufferValue : NULL;
    }

    bool GetValue(std::vector<CSalamanderTextRange>& value) const noexcept
    {
        if (!Valid || !sally::plugin_abi::IsValidTextRangeBuffer(BufferValue))
            return false;
        try
        {
            if (BufferValue.Count == 0)
            {
                value.clear();
                return true;
            }
            std::vector<CSalamanderTextRange> staged(
                BufferValue.Data, BufferValue.Data + BufferValue.Count);
            value.swap(staged);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        catch (const std::length_error&)
        {
            return false;
        }
    }

private:
    static BOOL WINAPI ReserveStorage(CSalamanderTextRangeBuffer* buffer,
                                      DWORD minimumCapacity) noexcept
    {
        if (buffer == NULL || buffer->OwnerContext == NULL)
            return FALSE;
        CSalamanderTextRangeBufferOwner* owner =
            static_cast<CSalamanderTextRangeBufferOwner*>(buffer->OwnerContext);
        if (!owner->Valid || buffer != &owner->BufferValue ||
            buffer->Reserve != ReserveStorage ||
            buffer->Data != owner->Storage.data() ||
            buffer->Capacity != owner->Storage.size() ||
            buffer->Count > buffer->Capacity)
            return FALSE;
        if (minimumCapacity <= buffer->Capacity)
            return TRUE;
        try
        {
            owner->Storage.resize(minimumCapacity);
            buffer->Data = owner->Storage.data();
            buffer->Capacity = minimumCapacity;
            return TRUE;
        }
        catch (const std::bad_alloc&)
        {
            return FALSE;
        }
        catch (const std::length_error&)
        {
            return FALSE;
        }
    }

    CSalamanderTextRangeBuffer BufferValue;
    std::vector<CSalamanderTextRange> Storage;
    bool Valid;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_owner_spl_buffer)
#pragma pack(pop, enter_include_spl_buffer)
#endif
