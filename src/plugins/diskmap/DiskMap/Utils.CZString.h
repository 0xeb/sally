// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "TreeMap.FileData.CZFile.h"

class CStringFormatter;

class CZString
{
protected:
    wchar_t* _s;
    size_t _l;

public:
    explicit CZString(wchar_t const* s)
    {
        this->_l = wcslen(s);
        this->_s = (wchar_t*)malloc((this->_l + 1) * sizeof(wchar_t));
        wcscpy(this->_s, s);
    }

    explicit CZString(CZFile* file)
    {
        std::wstring buff;
        this->_l = file->GetFullName(buff);
        this->_s = (wchar_t*)malloc((this->_l + 1) * sizeof(wchar_t));
        wcscpy(this->_s, buff.c_str());
    }

    ~CZString()
    {
        free(this->_s);
        this->_s = NULL;
        this->_l = 0;
    }
    wchar_t const* GetString() const { return this->_s; }
    size_t GetLength() const { return this->_l; }
};

class CZStringBuffer
{
    friend class CStringFormatter;

protected:
    wchar_t* _s;
    size_t _l; //length of the string
    size_t _c; //buffer size in wchar_ts
public:
    explicit CZStringBuffer(wchar_t const* s)
    {
        this->_l = wcslen(s);
        this->_c = this->_l + 1;
        this->_s = (wchar_t*)malloc(this->_c * sizeof(wchar_t));
    }
    explicit CZStringBuffer(size_t size)
    {
        this->_l = 0;
        this->_c = size;
        this->_s = (wchar_t*)malloc(this->_c * sizeof(wchar_t));
        *this->_s = L'\0';
    }
    ~CZStringBuffer()
    {
        free(this->_s);
        this->_s = NULL;
        this->_c = 0;
        this->_l = 0;
    }
    wchar_t const* GetString() const { return this->_s; }
    size_t GetLength() const { return this->_l; }
    size_t GetBuffSize() const { return this->_c; }

    BOOL EndsWith(wchar_t chr) const
    {
        if (this->_l == 0)
            return FALSE;
        return (this->_s[this->_l - 1] == chr);
    }
    BOOL StartsWith(wchar_t chr) const
    {
        if (this->_l == 0)
            return FALSE;
        return (this->_s[0] == chr);
    }
    BOOL IsCharAt(wchar_t chr, size_t pos) const
    {
        if (this->_l < pos)
            return FALSE;
        return (this->_s[pos] == chr);
    }

    CZStringBuffer* Append(wchar_t const* s)
    {
        size_t len = wcslen(s);

        return this->AppendAt(s, len, this->_l);
    }

    CZStringBuffer* Append(wchar_t const* s, size_t len)
    {
        return this->AppendAt(s, len, this->_l);
    }

    CZStringBuffer* Append(wchar_t c)
    {
        return this->AppendAt(c, this->_l);
    }

    CZStringBuffer* AppendAt(wchar_t const* s, int pos)
    {
        size_t len = wcslen(s);

        return this->AppendAt(s, len, pos);
    }
    CZStringBuffer* AppendAt(wchar_t const* s, size_t len, size_t pos)
    {
        int p = (int)min(pos, this->_l);
        if (len + p > this->_c)
            return this; //TODO!

        //len = this->_c - this->_l - 1;
        wcscpy(&this->_s[p], s);
        //memcpy(this->_s[this->_l], s, len * sizeof(wchar_t));

        this->_l = len + p;
        this->_s[this->_l] = L'\0';

        return this;
    }
    CZStringBuffer* AppendAt(wchar_t c, size_t pos)
    {
        size_t p = min(pos, this->_l);
        if (p >= this->_c)
            return this; //TODO!

        this->_s[p] = c;

        this->_l = p + 1;
        this->_s[this->_l] = L'\0';

        return this;
    }
    CZStringBuffer* Append(CZString const* s)
    {
        return this->AppendAt(s->GetString(), s->GetLength(), this->_l);
    }
    CZStringBuffer* AppendAt(CZString const* s, int pos)
    {
        return this->AppendAt(s->GetString(), s->GetLength(), pos);
    }

    CZStringBuffer* Left(size_t len)
    {
        if (len > this->_l)
            len = this->_l;

        this->_l = len;
        this->_s[this->_l] = L'\0';

        return this;
    }
    size_t GetSubString(size_t pos, size_t length, std::wstring& output)
    {
        if (pos >= this->_l)
        {
            output.clear();
            return 0;
        }

        if (pos + length > this->_l)
            length = this->_l - pos;
        output.assign(&this->_s[pos], length);
        return length;
    }
};
