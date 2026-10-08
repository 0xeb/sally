// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

// ****************************************************************************
//
// CSynchronizedCounter
//

CSynchronizedCounter::CSynchronizedCounter()
{
    CALL_STACK_MESSAGE_NONE
    Counter = 0;
    ChangeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
}

CSynchronizedCounter::~CSynchronizedCounter()
{
    CALL_STACK_MESSAGE_NONE
    CloseHandle(ChangeEvent);
}

int CSynchronizedCounter::Up()
{
    CALL_STACK_MESSAGE_NONE
    CS.Enter();
    int ret = ++Counter;
    SetEvent(ChangeEvent);
    CS.Leave();
    return ret;
}

int CSynchronizedCounter::Down()
{
    CALL_STACK_MESSAGE_NONE
    CS.Enter();
    int ret = --Counter;
    SetEvent(ChangeEvent);
    CS.Leave();
    return ret;
}

int CSynchronizedCounter::Value()
{
    CALL_STACK_MESSAGE_NONE
    CS.Enter();
    int ret = Counter;
    ResetEvent(ChangeEvent);
    CS.Leave();
    return ret;
}

DWORD
CSynchronizedCounter::WaitForChange()
{
    CALL_STACK_MESSAGE_NONE
    return WaitForSingleObject(ChangeEvent, INFINITE);
}

// ****************************************************************************
//
// CArgv
//

CArgv::CArgv(const char* commandLine) : TIndirectArray<char>(8, 8)
{
    CALL_STACK_MESSAGE2("CArgv::CArgv(%s)", commandLine);
    const char* start = commandLine;
    while (*start)
    {
        // trim whitespace at the beginning
        while (*start && IsSpace(*start))
            start++;
        if (!*start)
            break;
        const char* end = start;
        // find the end of the token
        while (*end && !IsSpace(*end))
        {
            if (*end++ == '"')
            {
                end = strchr(end, '"');
                if (end)
                    end++;
                else
                    end = start + strlen(start);
            }
        }
        // add token to the array
        char* str = DupStr(start, end);
        RemoveCharacters(str, str, "\"");
        Add(str);
        start = end;
    }
}

// ****************************************************************************

char* DupStr(const char* begin, const char* end)
{
    CALL_STACK_MESSAGE_NONE
    size_t len = end - begin;
    char* ret = (char*)malloc(len + 1);
    memcpy(ret, begin, len);
    ret[len] = 0;
    return ret;
}

int RemoveCharacters(char* dest, const char* source, const char* charSet)
{
    CALL_STACK_MESSAGE_NONE
    int d = 0, s = 0;
    while (source[s])
    {
        if (strchr(charSet, source[s]))
            s++;
        else
            dest[d++] = source[s++];
    }
    dest[d] = 0;
    return d;
}
