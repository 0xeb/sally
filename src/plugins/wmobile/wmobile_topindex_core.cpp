// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "wmobile_topindex_core.h"

#include <cwchar>

namespace wmobile
{

namespace
{

// Length of 'path' ignoring one trailing separator, so "\a\b" and "\a\b\" measure the same.
size_t LengthWithoutTrailingSeparator(const wchar_t* path)
{
    size_t len = wcslen(path);
    if (len > 0 && path[len - 1] == L'\\')
        len--;
    return len;
}

// Offset of the last separator that starts the final component, ignoring a trailing one.
// For "\a\b" this is 2 (the separator before "b"); for "\a" it is 0.
size_t ParentLength(const wchar_t* path)
{
    const wchar_t* s = path + wcslen(path);
    if (s > path && *(s - 1) == L'\\')
        s--;
    if (s == path)
        return 0;
    if (s > path && *s == L'\\')
        s--;
    while (s > path && *s != L'\\')
        s--;
    return (size_t)(s - path);
}

} // namespace

TopIndexMemory::TopIndexMemory(PathCompareN compare)
    : m_compare(compare), m_count(0)
{
}

void TopIndexMemory::Clear()
{
    m_path.clear();
    m_count = 0;
}

void TopIndexMemory::Push(const wchar_t* path, int topIndex)
{
    if (path == nullptr)
        return;

    // Is 'path' a direct child of what we remember - i.e. path == m_path + "\<name>"?
    //
    // The root is a legitimate parent. ParentLength("\Windows") is 0 and the remembered length of
    // "\" is also 0, and that pair must MATCH: the original compared the two lengths and then ran
    // a zero-length StrNICmp, which trivially succeeded. Demanding a non-zero parent instead made
    // every first-level directory restart the sequence, so the root's scroll position was thrown
    // away on the way back down and never restored on the way back up.
    bool isChild = false;
    size_t length = wcslen(path);
    if (length > 0 && path[length - 1] == L'\\')
        length--;
    if (length != 0) // "" and "\" have no parent of their own to continue from
    {
        const size_t parent = ParentLength(path);
        const size_t remembered = LengthWithoutTrailingSeparator(m_path.c_str());
        isChild = parent == remembered &&
                  (parent == 0 || m_compare(path, m_path.c_str(), parent) == 0);
    }

    if (isChild)
    {
        if (m_count == kTopIndexMemSize)
        {
            // Full: drop the OLDEST level so a deep descent keeps the most recent ones.
            for (int i = 0; i < kTopIndexMemSize - 1; i++)
                m_indexes[i] = m_indexes[i + 1];
            m_count--;
        }
        m_indexes[m_count++] = topIndex;
    }
    else
    {
        m_count = 1;
        m_indexes[0] = topIndex;
    }

    m_path = path;
}

bool TopIndexMemory::FindAndPop(const wchar_t* path, int& topIndex)
{
    if (path == nullptr)
    {
        Clear();
        return false;
    }

    const size_t asked = LengthWithoutTrailingSeparator(path);
    const size_t remembered = LengthWithoutTrailingSeparator(m_path.c_str());

    if (asked != remembered || (asked != 0 && m_compare(path, m_path.c_str(), asked) != 0))
    {
        // A different path means the user jumped rather than stepped up; the sequence is
        // meaningless from here.
        Clear();
        return false;
    }

    if (m_count <= 0)
    {
        Clear();
        return false;
    }

    // Step the remembered path up one level, so the next pop matches the parent.
    m_path.resize(ParentLength(m_path.c_str()));
    topIndex = m_indexes[--m_count];
    return true;
}

} // namespace wmobile
