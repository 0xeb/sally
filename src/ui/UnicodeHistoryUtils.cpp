// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "UnicodeHistoryUtils.h"

#include <stdlib.h>
#include <string.h>
#include <string>

namespace
{
wchar_t* DupWideStr(const wchar_t* value)
{
    if (value == NULL)
        return NULL;

    size_t len = wcslen(value);
    wchar_t* text = (wchar_t*)malloc((len + 1) * sizeof(wchar_t));
    if (text == NULL)
        return NULL;
    memcpy(text, value, (len + 1) * sizeof(wchar_t));
    return text;
}

} // namespace

BOOL IsWideHistoryEmpty(wchar_t* history[], int count)
{
    for (int i = 0; i < count; i++)
    {
        if (history[i] != NULL && history[i][0] != L'\0')
            return FALSE;
    }
    return TRUE;
}

void AddValueToWideHistory(wchar_t** historyArr, int historyItemsCount,
                           const wchar_t* value, BOOL caseSensitiveValue)
{
    if (historyItemsCount <= 0 || historyArr == NULL || value == NULL)
        return;

    int from = -1;
    for (int i = 0; i < historyItemsCount; i++)
    {
        if (historyArr[i] != NULL &&
            ((!caseSensitiveValue && _wcsicmp(historyArr[i], value) == 0) ||
             (caseSensitiveValue && wcscmp(historyArr[i], value) == 0)))
        {
            from = i;
            break;
        }
    }

    if (from == -1 || from > 0)
    {
        if (from == -1)
            from = historyItemsCount - 1;

        wchar_t* text = DupWideStr(value);
        if (text != NULL)
        {
            free(historyArr[from]);
            for (int i = from - 1; i >= 0; i--)
                historyArr[i + 1] = historyArr[i];
            historyArr[0] = text;
        }
    }
}

BOOL ShouldUseWidePair(BOOL gotWideName, BOOL gotWidePath)
{
    // EITHER half is enough - see the header for why requiring both would demote
    // a legitimately half-filled entry to its lossy ANSI mirror.
    return (gotWideName || gotWidePath) ? TRUE : FALSE;
}

void EscapeHotPathDollars(std::wstring& text)
{
    size_t pos = 0;
    while ((pos = text.find(L'$', pos)) != std::wstring::npos)
    {
        text.insert(pos, 1, L'$');
        pos += 2;
    }
}

BOOL AppendUserMenuArgument(std::wstring& list, const wchar_t* name, size_t nameLength,
                            size_t maxLength)
{
    if (name == NULL)
        return FALSE;

    const bool quote = wmemchr(name, L' ', nameLength) != NULL;
    const size_t required = (list.empty() ? 0 : 1) + (quote ? 2 : 0) + nameLength;
    if (required > maxLength || list.size() > maxLength - required)
        return FALSE;

    if (!list.empty())
        list.push_back(L' ');
    if (quote)
        list.push_back(L'"');
    list.append(name, nameLength);
    if (quote)
        list.push_back(L'"');
    return TRUE;
}
