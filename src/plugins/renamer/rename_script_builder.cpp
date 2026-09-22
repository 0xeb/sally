// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <string>
#include <vector>

const char* VarOriginalName = "OriginalName";
const char* VarDrive = "Drive";
const char* VarPath = "Path";
const char* VarRelativePath = "RelativePath";
const char* VarName = "Name";
const char* VarNamePart = "NamePart";
const char* VarExtPart = "ExtPart";
const char* VarSize = "Size";
const char* VarTime = "Time";
const char* VarDate = "Date";
const char* VarCounter = "Counter";

class CVariableEx : public CVarString::CVariable
{
public:
    // serves only to split arguments, calls SetArgument()
    virtual BOOL SetArguments(const char* argStart, const char* argEnd,
                              int& error, const char*& errorPos1, const char*& errorPos2)
    {
        const char* start = argStart;
        const char* end;
        int state = 0;
        while (start < argEnd)
        {
            end = StrQChr(start, argEnd, '\'', ',');
            if (end - start > 0)
            {
                if (!SetArgument(start, end, error, errorPos1, errorPos2, state))
                    return FALSE;
            }
            start = end + 1; // skip the comma
        }
        return TRUE;
    };

    virtual BOOL SetArgument(const char* argStart, const char* argEnd,
                             int& error, const char*& errorPos1, const char*& errorPos2,
                             int& state) = 0;
};

class CVarAlterableString : public CVariableEx
{
    int CutStart, CountStartFrom, CutEnd, CountEndFrom;
    CChangeCase Case;

public:
    CVarAlterableString()
    {
        CutStart = CutEnd = 0;
        CountStartFrom = 1;
        CountEndFrom = -1;
        Case = ccDontChange;
    }

    virtual BOOL SetArgument(const char* argStart, const char* argEnd,
                             int& error, const char*& errorPos1, const char*& errorPos2,
                             int& state)
    {
        if (state == 0)
        {
            if (SG->StrICmpEx(RenamerTextToWide(argStart, (int)(argEnd - argStart)).c_str(), -1, RenamerTextToWide("lower", sizeof("lower") - 1).c_str(), -1) == 0)
            {
                Case = ccLower;
                return TRUE;
            }
            if (SG->StrICmpEx(RenamerTextToWide(argStart, (int)(argEnd - argStart)).c_str(), -1, RenamerTextToWide("upper", sizeof("upper") - 1).c_str(), -1) == 0)
            {
                Case = ccUpper;
                return TRUE;
            }
            if (SG->StrICmpEx(RenamerTextToWide(argStart, (int)(argEnd - argStart)).c_str(), -1, RenamerTextToWide("mixed", sizeof("mixed") - 1).c_str(), -1) == 0)
            {
                Case = ccMixed;
                return TRUE;
            }
            if (SG->StrICmpEx(RenamerTextToWide(argStart, (int)(argEnd - argStart)).c_str(), -1, RenamerTextToWide("stripdia", sizeof("stripdia") - 1).c_str(), -1) == 0)
            {
                Case = ccStripDia;
                return TRUE;
            }
        }

        int offs, from = 1;
        if (*argStart == '-')
        {
            from = -1;
            argStart++;
        }
        if (IsValidInt(argStart, argEnd, FALSE))
        {
            offs = atoi(argStart);
            if (state == 0)
            {
                CutStart = offs;
                CountStartFrom = from;
                state = 1;
            }
            else
            {
                CutEnd = offs;
                CountEndFrom = from;
                state = 0;
            }
            return TRUE;
        }
        error = state == 0 ? IDS_EXP_EXPECTCUTORCASE : IDS_EXP_EXPECTENDCUT;
        errorPos1 = argStart;
        errorPos2 = argEnd;
        return FALSE;
    }

    virtual int DoExpand(char*& string, char* end,
                         const char* srcStart, const char* srcEnd)
    {
        const char *s, *e;
        s = CountStartFrom > 0 ? srcStart + CutStart : srcEnd - CutStart;
        if (s < srcStart)
            s = srcStart;
        e = CountEndFrom > 0 ? srcStart + CutEnd : srcEnd - CutEnd;
        if (e > srcEnd)
            e = srcEnd;
        int l = (int)(e - s);
        if (l > 0)
        {
            if (Case == ccDontChange)
            {
                if (end - string < l)
                    return -1;
                memcpy(string, s, l);
            }
            else
            {
                const std::string changed = ChangeCase(Case, s, e);
                if (end - string < (int)changed.size())
                    return -1;
                memcpy(string, changed.data(), changed.size());
                l = (int)changed.size();
            }
            string += l;
            return l;
        }
        return 0;
    }
};

class CVarOriginalName : public CVarAlterableString
{
public:
    static CVarString::CVariable* Alloc() { return new CVarOriginalName(); }

    virtual int Expand(char*& string, char* end, LPVOID param)
    {
        CExecuteNewNameParam* p = (CExecuteNewNameParam*)param;
        char *strStart, *strEnd;
        switch (p->Spec)
        {
        case rsFileName:
            strStart = p->EngineFullName.data() + p->EngineNameOffset;
            break;
        case rsRelativePath:
            strStart = p->EngineFullName.data() + p->EngineRootLen;
            break;
        case rsFullPath:
            strStart = p->EngineFullName.data();
            break;
        }
        strEnd = p->EngineFullName.data() + p->EngineFullName.size();
        return DoExpand(string, end, strStart, strEnd);
    }
};

class CVarDrive : public CVarAlterableString
{
public:
    static CVarString::CVariable* Alloc() { return new CVarDrive(); }

    virtual int Expand(char*& string, char* end, LPVOID param)
    {
        CExecuteNewNameParam* p = (CExecuteNewNameParam*)param;

        if (p->Spec == rsFileName || p->Spec == rsRelativePath)
            return 0;

        char *strStart, *strEnd;
        strStart = p->EngineFullName.data();
        if (strStart[0] == '\\' && strStart[1] == '\\') // UNC
        {
            strEnd = strStart + 2;
            while (*strEnd != 0 && *strEnd != '\\')
                strEnd++;
            if (*strEnd != 0)
                strEnd++; // '\\'
            while (*strEnd != 0 && *strEnd != '\\')
                strEnd++;
        }
        else
            strEnd = strStart + 2;

        return DoExpand(string, end, strStart, strEnd);
    }
};

class CVarPath : public CVarAlterableString
{
public:
    static CVarString::CVariable* Alloc() { return new CVarPath(); }

    virtual int Expand(char*& string, char* end, LPVOID param)
    {
        CExecuteNewNameParam* p = (CExecuteNewNameParam*)param;

        if (p->Spec == rsFileName || p->Spec == rsRelativePath)
            return 0;

        char *strStart, *strEnd;
        strStart = p->EngineFullName.data();
        if (strStart[0] == '\\' && strStart[1] == '\\') // UNC
        {
            strStart += 2;
            while (*strStart != 0 && *strStart != '\\')
                strStart++;
            if (*strStart != 0)
                strStart++; // '\\'
            while (*strStart != 0 && *strStart != '\\')
                strStart++;
        }
        else
            strStart += 2;

        strEnd = p->EngineFullName.data() + p->EngineNameOffset;

        return DoExpand(string, end, strStart, strEnd);
    }
};

class CVarRelativePath : public CVarAlterableString
{
public:
    static CVarString::CVariable* Alloc() { return new CVarRelativePath(); }

    virtual int Expand(char*& string, char* end, LPVOID param)
    {
        CExecuteNewNameParam* p = (CExecuteNewNameParam*)param;

        if (p->Spec == rsFileName)
            return 0;

        char *strStart, *strEnd;
        strStart = p->EngineFullName.data() + p->EngineRootLen;
        strEnd = p->EngineFullName.data() + p->EngineNameOffset;

        return DoExpand(string, end, strStart, strEnd);
    }
};

class CVarName : public CVarAlterableString
{
public:
    static CVarString::CVariable* Alloc() { return new CVarName(); }

    virtual int Expand(char*& string, char* end, LPVOID param)
    {
        CExecuteNewNameParam* p = (CExecuteNewNameParam*)param;

        char *strStart, *strEnd;
        strStart = p->EngineFullName.data() + p->EngineNameOffset;
        strEnd = p->EngineFullName.data() + p->EngineFullName.size();

        return DoExpand(string, end, strStart, strEnd);
    }
};

class CVarNamePart : public CVarAlterableString
{
public:
    static CVarString::CVariable* Alloc() { return new CVarNamePart(); }

    virtual int Expand(char*& string, char* end, LPVOID param)
    {
        CExecuteNewNameParam* p = (CExecuteNewNameParam*)param;

        char *strStart, *strEnd;
        strStart = p->EngineFullName.data() + p->EngineNameOffset;
        strEnd = p->EngineFullName.data() + p->EngineExtOffset;
        if (*strEnd != 0)
            strEnd--; // ext points past the dot

        return DoExpand(string, end, strStart, strEnd);
    }
};

class CVarExtPart : public CVarAlterableString
{
public:
    static CVarString::CVariable* Alloc() { return new CVarExtPart(); }

    virtual int Expand(char*& string, char* end, LPVOID param)
    {
        CExecuteNewNameParam* p = (CExecuteNewNameParam*)param;

        char *strStart, *strEnd;
        strStart = p->EngineFullName.data() + p->EngineExtOffset;
        strEnd = p->EngineFullName.data() + p->EngineFullName.size();

        return DoExpand(string, end, strStart, strEnd);
    }
};

class CVarSize : public CVarString::CVariable
{
public:
    static CVarString::CVariable* Alloc() { return new CVarSize(); }

    virtual int Expand(char*& string, char* end, LPVOID param)
    {
        CExecuteNewNameParam* p = (CExecuteNewNameParam*)param;

        if (p->File->IsDir)
            return 0;

        int l = SalPrintf(string, (int)(end - string), "%I64u", p->File->Size.Value);
        if (l > 0)
            string += l;
        return l;
    }
};

// $(Date:fmt) and $(Time:fmt), formatted wide and re-encoded as the UTF-8 the engine
// buffer holds.
//
// These two used the TCHAR GetDateFormat/GetTimeFormat, which resolved to the ANSI form
// while the plugin was built without UNICODE; pinning them to the explicit ...A kept that
// behaviour once UNICODE went build-wide. But the engine buffer is no longer ACP - it is
// UTF-8, and TryRenamerTextToWide decodes it as strict UTF-8 with no fallback. So the ACP
// bytes the ANSI formatter writes for any locale whose date or time names are not plain
// ASCII - Czech, Russian, Greek, Japanese - failed that decode and the whole rename was
// refused, where before the same bytes had simply become the ACP file name.
//
// The format string travels the same road in reverse: it comes from the user's expression,
// so it is UTF-8 and has to be widened before the W formatter sees it.
static bool FormatWideDateTime(bool date, const std::wstring& format, const SYSTEMTIME& st,
                               std::string& utf8)
{
    utf8.clear();
    const int needed = date ? GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, format.c_str(), NULL, 0)
                            : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, format.c_str(), NULL, 0);
    if (needed <= 0)
        return false;
    std::vector<wchar_t> buffer(static_cast<size_t>(needed), L'\0');
    const int written = date ? GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, format.c_str(),
                                              buffer.data(), needed)
                             : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, format.c_str(),
                                              buffer.data(), needed);
    if (written <= 0)
        return false;
    // 'written' counts the terminator.
    return TryWideToRenamerText(buffer.data(), utf8, written - 1);
}

// Shared by both Expand overrides: format, then copy into the caller's remaining space.
// The CVariable contract is "negative means no", and a result that does not fit is one of
// the things the grow-and-retry loop in CVarString::ExecuteOwned exists to answer.
static int ExpandWideDateTime(bool date, const std::wstring& format, const FILETIME& when,
                              char*& string, char* end)
{
    SYSTEMTIME st;
    if (!FileTimeToSystemTime(&when, &st))
        return -1;
    std::string utf8;
    if (!FormatWideDateTime(date, format, st, utf8))
        return -1;
    if (utf8.size() >= static_cast<size_t>(end - string))
        return -1;
    memcpy(string, utf8.data(), utf8.size());
    string += utf8.size();
    return static_cast<int>(utf8.size());
}

class CVarTime : public CVariableEx
{
    std::wstring Format;

public:
    static CVarString::CVariable* Alloc() { return new CVarTime(); }

    virtual BOOL SetArguments(const char* argStart, const char* argEnd,
                              int& error, const char*& errorPos1, const char*& errorPos2)
    {
        if (argStart < argEnd)
            return CVariableEx::SetArguments(
                argStart, argEnd, error, errorPos1, errorPos2);

        error = IDS_EXP_MISTIMEFORMAT;
        errorPos1 = argStart;
        errorPos2 = argEnd;
        return FALSE;
    }

    virtual BOOL SetArgument(const char* argStart, const char* argEnd,
                             int& error, const char*& errorPos1, const char*& errorPos2,
                             int& state)
    {
        if (state == 1)
        {
            error = IDS_EXP_UNEXPECTEDARGUMENT;
            errorPos1 = argStart;
            errorPos2 = argEnd;
            return FALSE;
        }

        state = 1;

        std::wstring fmt;
        if (!TryRenamerTextToWide(argStart, fmt, static_cast<int>(argEnd - argStart)))
        {
            error = IDS_EXP_INVALIDTIMEFMT;
            errorPos1 = argStart;
            errorPos2 = argEnd;
            return FALSE;
        }

        SYSTEMTIME st;
        std::string probe;
        GetSystemTime(&st);
        if (!FormatWideDateTime(false, fmt, st, probe))
        {
            error = IDS_EXP_INVALIDTIMEFMT;
            errorPos1 = argStart;
            errorPos2 = argEnd;
            return FALSE;
        }

        Format = std::move(fmt);
        return TRUE;
    }

    virtual int Expand(char*& string, char* end, LPVOID param)
    {
        CExecuteNewNameParam* p = (CExecuteNewNameParam*)param;
        return ExpandWideDateTime(false, Format, p->File->LastWrite, string, end);
    }
};

class CVarDate : public CVariableEx
{
    std::wstring Format;

public:
    static CVarString::CVariable* Alloc() { return new CVarDate(); }

    virtual BOOL SetArguments(const char* argStart, const char* argEnd,
                              int& error, const char*& errorPos1, const char*& errorPos2)
    {
        if (argStart < argEnd)
            return CVariableEx::SetArguments(
                argStart, argEnd, error, errorPos1, errorPos2);

        error = IDS_EXP_MISDATEFORMAT;
        errorPos1 = argStart;
        errorPos2 = argEnd;
        return FALSE;
    }

    virtual BOOL SetArgument(const char* argStart, const char* argEnd,
                             int& error, const char*& errorPos1, const char*& errorPos2,
                             int& state)
    {
        if (state == 1)
        {
            error = IDS_EXP_TOOMUCHARGUMENTS;
            errorPos1 = argStart;
            errorPos2 = argEnd;
            return FALSE;
        }

        state = 1;

        std::wstring fmt;
        if (!TryRenamerTextToWide(argStart, fmt, static_cast<int>(argEnd - argStart)))
        {
            error = IDS_EXP_INVALIDDATEFMT;
            errorPos1 = argStart;
            errorPos2 = argEnd;
            return FALSE;
        }

        SYSTEMTIME st;
        std::string probe;
        GetSystemTime(&st);
        if (!FormatWideDateTime(true, fmt, st, probe))
        {
            error = IDS_EXP_INVALIDDATEFMT;
            errorPos1 = argStart;
            errorPos2 = argEnd;
            return FALSE;
        }

        Format = std::move(fmt);
        return TRUE;
    }

    virtual int Expand(char*& string, char* end, LPVOID param)
    {
        CExecuteNewNameParam* p = (CExecuteNewNameParam*)param;
        return ExpandWideDateTime(true, Format, p->File->LastWrite, string, end);
    }
};

class CVarCounter : public CVariableEx
{
    int Start;
    double Step;
    char Format[3];
    BOOL LeftAlign;
    int Width;
    char Fill;

public:
    CVarCounter()
    {
        Start = 0;
        Step = 1;
        strcpy(Format, "%d");
        LeftAlign = FALSE;
        Width = 0;
        Fill = ' ';
    }

    static CVarString::CVariable* Alloc() { return new CVarCounter(); }

    virtual BOOL SetArgument(const char* argStart, const char* argEnd,
                             int& error, const char*& errorPos1, const char*& errorPos2,
                             int& state)
    {
        int oldState = state;
        switch (state)
        {
        // expecting 'start'
        case 0:
        {
            if (!IsValidInt(argStart, argEnd, TRUE))
            {
                error = IDS_EXP_EXPECTSTART;
                errorPos1 = argStart;
                errorPos2 = argEnd;
                return FALSE;
            }

            Start = atoi(argStart);
            break;
        }

        // expecting 'step'
        case 1:
        {
            if (!IsValidFloat(argStart, argEnd))
            {
                error = IDS_EXP_EXPECTSTEP;
                errorPos1 = argStart;
                errorPos2 = argEnd;
                return FALSE;
            }

            Step = atof(argStart);
            break;
        }

        // expecting 'base', 'left-align' or 'width'
        case 2:
        {
            if (argEnd - argStart == 1 && strchr("dxX", *argStart))
            {
                Format[1] = *argStart;
                break;
            }
            state++;
        }

        // expecting 'left-align' or 'width'
        case 3:
        {
            if (argEnd - argStart == 1 && *argStart == 'l')
            {
                LeftAlign = TRUE;
                break;
            }
            state++;
        }

        // expecting 'width'
        case 4:
        {
            if (!IsValidInt(argStart, argEnd, FALSE))
            {
                switch (oldState)
                {
                case 2:
                    error = IDS_EXP_EXPECTBASEALIGNWIDTH;
                case 3:
                    error = IDS_EXP_EXPECTALIGNWIDTH;
                case 4:
                    error = IDS_EXP_EXPECTWIDTH;
                }
                errorPos1 = argStart;
                errorPos2 = argEnd;
                return FALSE;
            }

            Width = atoi(argStart);
            break;
        }

        // expecting 'fill'
        case 5:
        {
            if (argEnd - argStart != 1)
            {
                error = IDS_EXP_EXPECTFILL;
                errorPos1 = argStart;
                errorPos2 = argEnd;
                return FALSE;
            }

            Fill = *argStart;
            break;
        }

        default:
        {
            error = IDS_EXP_UNEXPECTEDARGUMENT;
            errorPos1 = argStart;
            errorPos2 = argEnd;
            return FALSE;
        }
        }

        state++;
        return TRUE;
    }

    virtual int Expand(char*& string, char* end, LPVOID param)
    {
        CExecuteNewNameParam* p = (CExecuteNewNameParam*)param;

        int l, fill;
        if (Width <= 1 || LeftAlign)
        {
            l = SalPrintf(string, (int)(end - string), Format, (int)(Start + Step * p->Counter));
            if (l < 0)
                return -1;
            fill = Width - l;
            if (fill > 0)
            {
                if (Width > end - string)
                    return -1;
                memset(string + l, Fill, fill);
                l = Width;
            }
        }
        else
        {
            char buffer[50];
            l = SalPrintf(buffer, 50, Format, (int)(Start + Step * p->Counter));
            fill = Width - l;
            if (fill > 0)
            {
                if (Width > end - string)
                    return -1;
                memset(string, Fill, fill);
                memcpy(string + fill, buffer, l);
                l = Width;
            }
            else
            {
                if (l > end - string)
                    return -1;
                memcpy(string, buffer, l);
            }
        }
        string += l;
        return l;
    }
};

CVarString::CVariableEntry NewNameVariables[] =
    {
        VarOriginalName, CVarOriginalName::Alloc,
        VarDrive, CVarDrive::Alloc,
        VarPath, CVarPath::Alloc,
        VarRelativePath, CVarRelativePath::Alloc,
        VarName, CVarName::Alloc,
        VarNamePart, CVarNamePart::Alloc,
        VarExtPart, CVarExtPart::Alloc,
        VarSize, CVarSize::Alloc,
        VarTime, CVarTime::Alloc,
        VarDate, CVarDate::Alloc,
        VarCounter, CVarCounter::Alloc,
        NULL, NULL};
