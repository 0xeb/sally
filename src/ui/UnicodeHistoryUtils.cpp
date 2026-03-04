// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "UnicodeHistoryUtils.h"

#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

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

std::wstring AnsiToWideLocal(const char* text)
{
    if (text == NULL || text[0] == 0)
        return std::wstring();

    int needed = MultiByteToWideChar(CP_ACP, 0, text, -1, NULL, 0);
    if (needed <= 0)
        return std::wstring();

    std::vector<wchar_t> wide((size_t)needed);
    if (MultiByteToWideChar(CP_ACP, 0, text, -1, wide.data(), needed) <= 0)
        return std::wstring();

    return std::wstring(wide.data());
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

void SeedWideHistoryFromAnsi(wchar_t* historyW[], int historyWCount, char* historyA[], int historyACount)
{
    int count = historyWCount < historyACount ? historyWCount : historyACount;
    for (int i = 0; i < count; i++)
    {
        if (historyW[i] == NULL && historyA[i] != NULL && historyA[i][0] != 0)
        {
            std::wstring wide = AnsiToWideLocal(historyA[i]);
            if (!wide.empty())
                historyW[i] = DupWideStr(wide.c_str());
        }
    }
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
