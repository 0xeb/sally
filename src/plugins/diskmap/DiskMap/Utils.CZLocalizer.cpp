// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "Utils.CZLocalizer.h"

CZLocalizer* CZResourceString::s_localizer = NULL;

CZLocalizer::CZLocalizer(HINSTANCE HModule)
{
    //first populate the error message
    //this->_errlen = ARRAYSIZE(szStrLoadError);
    //_tcscpy(_buffer, szStrLoadError);
    //load all strings - we cannot delay it because thread synchronization would be required
    this->_freepos = this->_buffer;
    // ARRAYSIZE, not sizeof: this is wchar_t* arithmetic, so a byte count would put _bufferend
    // twice as far out as the buffer reaches and LoadStringW would be told it has double the real
    // capacity - writing past _buffer into _start[]/_length[]/_freepos once the strings outgrow it.
    this->_bufferend = this->_buffer + ARRAYSIZE(this->_buffer);
    for (int i = IDS_DISKMAP_FIRST; i <= IDS_DISKMAP_LAST; i++)
    {
        // Room for at least one character plus its terminator. LoadStringW treats cchBufferMax == 0
        // as a request for a read-only pointer and writes an LPWSTR through lpBuffer, so a full
        // buffer must take the fallback rather than call with 0.
        const ptrdiff_t remaining = this->_bufferend - this->_freepos;
        int size = (remaining >= 2)
                       ? LoadStringW(HModule, i, this->_freepos, (int)remaining)
                       : 0;
        if (size == 0) //not found, or no room left
        {
            this->_start[i - IDS_DISKMAP_FIRST] = szStrLoadError;
            this->_length[i - IDS_DISKMAP_FIRST] = ARRAYSIZE(szStrLoadError);
        }
        else
        {
            this->_start[i - IDS_DISKMAP_FIRST] = this->_freepos;
            this->_length[i - IDS_DISKMAP_FIRST] = size;
            this->_freepos += size + 1;
        }
    }
    CZResourceString::s_localizer = this;
    //		CZLocalizer::s_instance = this;
}