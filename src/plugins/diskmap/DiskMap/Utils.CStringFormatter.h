// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class CStringFormatter
{
protected:
#ifndef OPENSAL_VERSION
#pragma region LocaleAPIHandler
#endif // OPENSAL_VERSION
    UINT GroupingStrToUint(PCWSTR szGrouping)
    {
        PCWSTR szCurr = szGrouping;
        UINT ret = 0;

        /*
		LOCALE_SGROUPING | Grouping | Sample     | Culture 
		3;0             |  3       |  1,234,567 |  United States 
		3;2;0           |  32      |  12,34,567 |  India 
                3               |  30      |   1234,567 |  (custom grouping example)
		*/

        for (;;)
        {
            ret *= 10;
            if (*szCurr == L'\0')
                break; //end of the string due to terminating null

            wchar_t* pch;
            ret += wcstol(szCurr, &pch, 10); //add the number

            if (wcscmp(pch, L";0") == 0)
                break; //end

            szCurr = pch + 1; //pointer past the delimiter
        }

        return ret;
    }
    // Fills the default NUMBERFMT structure for a given locale.
    BOOL LoadDefaultFormat()
    {
        wchar_t szBuf[80];

        int ret = ::GetLocaleInfoW(this->_lcid, LOCALE_IDIGITS, szBuf, ARRAYSIZE(szBuf));
        if (ret == 0)
            return FALSE;
        this->_defformat.NumDigits = wcstol(szBuf, NULL, 10);

        ret = ::GetLocaleInfoW(this->_lcid, LOCALE_ILZERO, szBuf, ARRAYSIZE(szBuf));
        if (ret == 0)
            return FALSE;
        this->_defformat.LeadingZero = wcstol(szBuf, NULL, 10);

        ret = ::GetLocaleInfoW(this->_lcid, LOCALE_SGROUPING, szBuf, ARRAYSIZE(szBuf));
        if (ret == 0)
            return FALSE;
        this->_defformat.Grouping = GroupingStrToUint(szBuf);

        ret = ::GetLocaleInfoW(this->_lcid, LOCALE_SDECIMAL, this->_defformat.lpDecimalSep, 5);
        if (ret == 0)
            return FALSE;

        ret = ::GetLocaleInfoW(this->_lcid, LOCALE_STHOUSAND, this->_defformat.lpThousandSep, 5);
        if (ret == 0)
            return FALSE;

        ret = ::GetLocaleInfoW(this->_lcid, LOCALE_INEGNUMBER, szBuf, ARRAYSIZE(szBuf));
        if (ret == 0)
            return FALSE;
        this->_defformat.NegativeOrder = wcstol(szBuf, NULL, 10);

        return TRUE;
    }
#ifndef OPENSAL_VERSION
#pragma endregion LocaleAPIHandler
#endif // OPENSAL_VERSION

    LCID _lcid;
    NUMBERFMTW _defformat;
    wchar_t _decimalSep[5];
    wchar_t _thousandSep[5];

    NUMBERFMTW _intformat;

public:
    CStringFormatter(LCID lcid)
    {
        this->_lcid = lcid;
        this->_defformat.lpDecimalSep = this->_decimalSep;
        this->_defformat.lpThousandSep = this->_thousandSep;
        this->LoadDefaultFormat();
        this->_intformat = this->_defformat;
    }
    ~CStringFormatter()
    {
    }
    size_t FormatLongDate(wchar_t* s, size_t slen, SYSTEMTIME* st)
    {
        size_t len = 0;
        size_t l = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_LONGDATE, st, NULL, s, (int)slen);
        if (l == 0)
        {
            if (slen >= 2 + 1 + 2 + 1 + 4)
            {
                l = swprintf(s, slen, L"%u.%u.%u", st->wDay, st->wMonth, st->wYear);
            }
            if (l > 0)
            {
                len = l;
            }
            else
            {
                //TODO: Error...
            }
        }
        else
        {
            len = l - 1; //without the null terminator
        }
        return len;
    }
    size_t FormatTime(wchar_t* s, size_t slen, SYSTEMTIME* st)
    {
        size_t len = 0;
        size_t l = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, st, NULL, s, (int)slen);
        if (l == 0)
        {
            if (slen >= 2 + 1 + 2 + 1 + 2)
            {
                l = swprintf(s, slen, L"%u:%02u:%02u", st->wHour, st->wMinute, st->wSecond);
            }
            if (l > 0)
            {
                len = l;
            }
            else
            {
                //TODO: Error...
            }
        }
        else
        {
            len = l - 1; //without the null terminator
        }
        return len;
    }
    size_t FormatLongFileDate(wchar_t* s, size_t slen, FILETIME* ft)
    {
        SYSTEMTIME st;
        FILETIME lft;
        size_t len = 0;
        if (FileTimeToLocalFileTime(ft, &lft) && FileTimeToSystemTime(&lft, &st))
        {
            size_t l = FormatLongDate(s, slen, &st);
            if (l > 0)
            {
                s += l;
            }
            *s++ = L',';
            l++;
            *s++ = L' ';
            l++;
            *s = L'\0'; //you never know when it might fail...
            len = l;
            slen -= len;
            l = FormatTime(s, slen, &st);
            if (l > 0)
            {
                len += l;
            }
        }
        return len;
    }
    size_t FormatInteger(wchar_t* s, size_t slen, UINT64 val)
    {
        //%I64u -- 0 - 18446744073709551615
        wchar_t buff[30];
        size_t len = 0;
        size_t l1 = swprintf(buff, 30, L"%I64u", val);
        this->_intformat.NumDigits = 0;
        size_t l2 = GetNumberFormatW(LOCALE_USER_DEFAULT, 0, buff, &this->_intformat, s, (int)slen);
        if (l2 == 0)
        {
            if (slen > l1)
            {
                wcscpy(s, buff);
                len = l1;
            }
            else
            {
                //TODO: Error...
            }
        }
        else
        {
            len = l2 - 1;
        }
        return len;
    }
    size_t FormatReal(wchar_t* s, size_t slen, double val, int digits = 2)
    {
        //%1.3f --
        wchar_t buff[30];
        size_t len = 0;
        size_t l1 = swprintf(buff, 30, L"%1.3f", val);
        this->_intformat.NumDigits = digits;
        size_t l2 = GetNumberFormatW(LOCALE_USER_DEFAULT, 0, buff, &this->_intformat, s, (int)slen);
        if (l2 == 0)
        {
            if (slen > l1)
            {
                wcscpy(s, buff);
                len = l1;
            }
            else
            {
                //TODO: Error...
            }
        }
        else
        {
            len = l2 - 1;
        }
        return len;
    }

    size_t FormatLongFileSize(wchar_t* s, size_t slen, UINT64 size)
    {
        size_t len = 0;
        size_t l = FormatInteger(s, slen, size);
        if (l > 0)
        {
            s += l;
            len += l;
            slen -= l;
            l = 0;
            UINT bytestrid = IDS_DISKMAP_FORMAT_BYTE0;
            if (size > 0)
            {
                if (size > 1)
                {
                    bytestrid = IDS_DISKMAP_FORMAT_BYTEN;
                }
                else
                {
                    bytestrid = IDS_DISKMAP_FORMAT_BYTE1;
                }
            }
            size_t bytelen = CZResourceString::GetLength(bytestrid);
            if (slen > bytelen)
            {
                *s++ = L' ';
                l++;
                wchar_t const* bytestr = CZResourceString::GetString(bytestrid);
                while (bytelen-- > 0)
                {
                    *s++ = *bytestr++;
                    l++;
                }
            }
            *s = L'\0'; //TODO: verify we do not overwrite beyond the end
            len += l;
        }
        return len;
    }
    size_t FormatShortFileSize(wchar_t* s, size_t slen, UINT64 size)
    {
        static const wchar_t expch[] = L" KMGTPEZY";
        static const int maxexp = sizeof(expch) / sizeof(wchar_t) - 2;
        size_t len = 0;
        int exp = 0;
        //C4244 ok: we only need the first few digits because we compute the short form "1.45TB"
        double rs = (double)(__int64)size;
        while (rs >= 1024 && exp < maxexp)
        {
            rs /= 1024;
            exp++;
        }
        int dec = 2;
        //if (rs < 10) dec = 3;
        //if (rs > 100) dec = 1;
        size_t l = FormatReal(s, slen - 3, rs, dec);
        if (l > 0)
        {
            s += l;
            *s++ = L' ';
            l++;
            if (exp > 0)
            {
                *s++ = expch[exp];
                l++;
            }
            *s++ = L'B';
            l++;
            *s = L'\0';

            len = l;
        }
        return len;
    }
    size_t FormatHumanFileSize(wchar_t* s, size_t slen, UINT64 size)
    {
        //%I64u -- 0 - 18446744073709551615
        size_t len = 0;
        size_t l = FormatLongFileSize(s, slen, size);

        if (l > 0)
        {
            if (size >= 1024)
            {
                //large value, try to add the short notation
                s += l;
                wchar_t* si = s;
                len += l;
                slen -= l;
                l = 0;
                if (slen > 5)
                {
                    *si++ = L' ';
                    l++;
                    *si++ = L'(';
                    l++;
                    //*si = L'\0'; //just in case

                    int li = (int)FormatShortFileSize(si, slen - 3, size);
                    if (li > 0)
                    {
                        l += li;
                        si += li;
                        *si++ = L')';
                        l++;
                        *si = L'\0'; //end
                        len += l;
                    }
                    else
                    {
                        //failed to append the inner text -> remove the parentheses
                        *s = L'\0';
                    }
                }
            }
            else
            {
                len = l;
            }
        }
        else
        {
            //no room for the bytes string, try the short notation
            len = FormatShortFileSize(s, slen, size);
        }
        return len;
    }
    size_t FormatExplorerFileSize(wchar_t* s, size_t slen, UINT64 size)
    {
        //%I64u -- 0 - 18446744073709551615
        size_t len = 0;
        size_t l = FormatShortFileSize(s, slen, size);

        if (l > 0)
        {
            if (size >= 1024)
            {
                //large value, try to add the short notation
                s += l;
                wchar_t* si = s;
                len += l;
                slen -= l;
                l = 0;
                if (slen > 5)
                {
                    *si++ = L' ';
                    l++;
                    *si++ = L'(';
                    l++;
                    //*si = L'\0'; //just in case

                    int li = (int)FormatLongFileSize(si, slen - 3, size);
                    if (li > 0)
                    {
                        l += li;
                        si += li;
                        *si++ = L')';
                        l++;
                        *si = L'\0'; //end
                        len += l;
                    }
                    else
                    {
                        //failed to append the inner text -> remove the parentheses
                        *s = L'\0';
                    }
                }
            }
            else
            {
                len = l;
            }
        }
        else
        {
            //no room for the bytes string, try the short notation
            len = FormatShortFileSize(s, slen, size);
        }
        return len;
    }
    void FormatLongFileDate(CZStringBuffer* s, FILETIME* ft)
    {
        s->_l = FormatLongFileDate(s->_s, s->_c, ft);
    }
    void FormatShortFileSize(CZStringBuffer* s, UINT64 size)
    {
        s->_l = FormatShortFileSize(s->_s, s->_c, size);
    }
    void FormatHumanFileSize(CZStringBuffer* s, UINT64 size)
    {
        s->_l = FormatHumanFileSize(s->_s, s->_c, size);
    }
    void FormatExplorerFileSize(CZStringBuffer* s, UINT64 size)
    {
        s->_l = FormatExplorerFileSize(s->_s, s->_c, size);
    }
    void FormatInteger(CZStringBuffer* s, UINT64 val)
    {
        s->_l = FormatInteger(s->_s, s->_c, val);
    }
};
