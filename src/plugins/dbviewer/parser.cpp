// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "unicode/helpers.h"
#include "const.h"
#include "display_text.h"

#include "csvlib/csvlib.h"
#include "dbflib/dbflib.h"
#include "parser.h"
#include "data.h"
#include "renderer.h"
#include "dialogs.h"
#include "dbviewer.h"
#include "dbviewer.rh"
#include "dbviewer.rh2"
#include "lang\lang.rh"

#define UTF8_DETECT_BUF_SIZE 65536

#define LongSwap(x) ((UINT32)((((UINT32)((x) & 0x000000FFL)) << 24) | \
                              (((UINT32)((x) & 0x0000FF00L)) << 8) | \
                              (((UINT32)((x) & 0x00FF0000L)) >> 8) | \
                              (((UINT32)((x) & 0xFF000000L)) >> 24)))

bool IsUTF8Encoded(const char* s, int cnt)
{
    int nUTF8 = 0;

    while (cnt-- > 0)
    {
        if (*s & 0x80)
        {
            if ((*s & 0xe0) == 0xc0)
            {
                if (!s[1])
                {
                    if (cnt)
                    { // incomplete 2-byte sequence
                        nUTF8 = 0;
                    }
                    break;
                }
                else if ((s[1] & 0xc0) != 0x80)
                {
                    nUTF8 = 0;
                    break; // not in UCS2
                }
                else
                {
                    nUTF8++;
                    s += 2;
                    cnt--;
                }
            }
            else if ((*s & 0xf0) == 0xe0)
            {
                if (!s[1] || !s[2])
                {
                    if (cnt > 1)
                    { // incomplete 3-byte sequence
                        nUTF8 = 0;
                    }
                    break;
                }
                else if ((s[1] & 0xc0) != 0x80 || (s[2] & 0xc0) != 0x80)
                {
                    nUTF8 = 0;
                    break; // not in UCS2
                }
                else
                {
                    nUTF8++;
                    s += 3;
                    cnt -= 2;
                }
            }
            else
            {
                nUTF8 = 0;
                break; // not in UCS2
            }
        }
        else
        {
            s++;
        }
    }

    if (nUTF8 > 0)
    { // At least one 2-or-more-bytes UTF8 sequence found and no invalid sequences found
        return true;
    }
    return false;
}

void CParserInterfaceAbstract::ShowParserError(HWND hParent, CParserStatusEnum status)
{
    if (this != NULL)
    { // this is NULL when called from CDatabase::Open when opening the file failed
        if (bShowingError)
        {
            // Avoid recursive error showing on WM_PAINT
            return;
        }
        bShowingError = true;
    }

    int strID = -1;
    std::wstring text;

    switch (status)
    {
    case psOK:
        return;
    case psOOM:
        strID = IDS_DBFE_OOM;
        break;
    case psUnknownFile:
        strID = IDS_DBFE_NOT_XBASE;
        break;
    case psFileNotFound:
        strID = IDS_DBFE_FILE_NOT_FOUND;
        break;
    case psReadError:
        strID = IDS_DBFE_READ_ERROR;
        break;
    case psWriteError:
        strID = IDS_DBFE_WRITE_ERROR;
        break;
    case psSeekError:
        strID = IDS_DBFE_SEEK_ERROR;
        break;
    case psNoMemoFile:
        strID = IDS_DBFE_NO_MEMO_FILE;
        break;
    case psCorruptedMemo:
        strID = IDS_DBFE_INVALID_BLOCK;
        break;
    }
    if (strID == -1)
    {
        text = L"Unknown error"; // default

        if ((status & psMask) == psSystemError)
        {
            // try to get error description from the system

            status = (CParserStatusEnum)(status & ~psMask);
            text = SPLGetErrorTextOwned(SalGeneral, status);
        }
    }
    else
    {
        text = SPLLoadStrOwned(SalGeneral, HLanguage, strID).c_str();
    }
    SalGeneral->SalMessageBox(hParent, text.c_str(), SPLLoadStrOwned(SalGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
    if (this != NULL)
        bShowingError = false;
}

//****************************************************************************
//
// CParserInterfaceDBF
//

CParserInterfaceDBF::CParserInterfaceDBF()
{
    Dbf = NULL;
    DbfHdr = NULL;
    DbfFields = NULL;
    Record = NULL;
    FileName.clear();
}

CParserStatusEnum
CParserInterfaceDBF::OpenFile(const wchar_t* fileName)
{
    CParserStatusEnum status = psOK;

    if (Dbf != NULL)
        CloseFile();

    // open the file in read-only mode
    Dbf = new cDBF(fileName, TRUE);
    if (Dbf != NULL)
    {
        if (Dbf->GetStatus() == DBFE_OK)
        {
            // populate the column array
            DBF_HEADER* hdr = Dbf->GetHeader();
            DBF_FIELD* field = Dbf->GetFields();

            DbfHdr = hdr;
            DbfFields = field;

            // prepare a buffer for receiving data
            if (Record != NULL)
            {
                free(Record);
                Record = NULL;
            }
            Record = (char*)malloc(hdr->recordSize);
            if (Record == NULL)
            {
                CloseFile();
                status = psOOM;
            }
        }
        else
        {
            status = TranslateDBFStatus(Dbf->GetStatus());
            CloseFile(); // after Close Dbf will be NULL
        }
    }
    else
    {
        status = psOOM;
    }

    if (status == psOK)
        FileName = fileName;

    return status;
}

void CParserInterfaceDBF::CloseFile()
{
    if (Dbf != NULL)
    {
        delete Dbf;
        Dbf = NULL;
    }
    if (Record != NULL)
    {
        free(Record);
        Record = NULL;
    }
    DbfHdr = NULL;
    DbfFields = NULL;
    FileName.clear();
}

namespace
{
void AppendFileInfoText(HWND edit, const std::wstring& text)
{
    SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
}

bool FormatFileTimestamp(const FILETIME& utc, std::wstring& text)
{
    FILETIME local;
    SYSTEMTIME system;
    if (!FileTimeToLocalFileTime(&utc, &local) || !FileTimeToSystemTime(&local, &system))
        return false;

    const int dateLength = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_LONGDATE, &system, NULL, NULL, 0);
    const int timeLength = GetTimeFormatW(LOCALE_USER_DEFAULT, LOCALE_NOUSEROVERRIDE, &system, NULL, NULL, 0);
    if (dateLength <= 0 || timeLength <= 0)
        return false;

    std::wstring date(static_cast<size_t>(dateLength), L'\0');
    std::wstring clock(static_cast<size_t>(timeLength), L'\0');
    if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_LONGDATE, &system, NULL, date.data(), dateLength) <= 0 ||
        GetTimeFormatW(LOCALE_USER_DEFAULT, LOCALE_NOUSEROVERRIDE, &system, NULL, clock.data(), timeLength) <= 0)
        return false;
    date.resize(static_cast<size_t>(dateLength - 1));
    clock.resize(static_cast<size_t>(timeLength - 1));
    text = date + L", " + clock;
    return true;
}

void AppendCommonFileInfo(HWND edit, const std::wstring& fileName)
{
    AppendFileInfoText(edit, fileName + L"\r\n\r\n");

    HANDLE file = CreateFileW(fileName.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;

    FILETIME fileTime = {};
    CQuadWord fileSize(0, 0);
    GetFileTime(file, NULL, NULL, &fileTime);
    DWORD err;
    const BOOL haveSize = SalGeneral->SalGetFileSize(file, fileSize, err);
    CloseHandle(file);

    std::wstring timestamp;
    if (FormatFileTimestamp(fileTime, timestamp))
        AppendFileInfoText(edit, SPLFormatStringOwned(L"%ls:\t%ls\r\n", LangStr(IDS_FINFO_MODIFIED).c_str(), timestamp.c_str()));
    if (haveSize)
    {
        const std::wstring size = SPLPrintDiskSizeOwned(SalGeneral, fileSize, 1);
        AppendFileInfoText(edit, SPLFormatStringOwned(L"%ls:\t%ls\r\n", LangStr(IDS_FINFO_SIZE).c_str(), size.c_str()));
    }
}
} // namespace

BOOL CParserInterfaceDBF::GetFileInfo(HWND hEdit)
{
    if (Dbf == NULL)
    {
        TRACE_E("Invalid call to CParserInterfaceDBF::GetFileInfo: Dbf == NULL");
        return FALSE;
    }

    SetWindowTextW(hEdit, L"");
    DWORD tab = 80;
    SendMessage(hEdit, EM_SETTABSTOPS, 1, (LPARAM)&tab);
    AppendCommonFileInfo(hEdit, FileName);

    int verStrID;
    switch (DbfHdr->version)
    {
    case DBF_DBASE2:
        verStrID = IDS_FILEVER_DBASE2;
        break;
    case DBF_DBASE3:
        verStrID = IDS_FILEVER_DBASE3;
        break;
    case DBF_DBASE4:
        verStrID = IDS_FILEVER_DBASE4;
        break;
    case DBF_DBASE5:
        verStrID = IDS_FILEVER_DBASE5;
        break;
    case DBF_FOXPRO:
        verStrID = IDS_FILEVER_FOXPRO;
        break;
    case DBF_VISUALDBASE7:
        verStrID = IDS_FILEVER_VISUALDBASE7;
        break;
    default:
        verStrID = IDS_FILEVER_UNKNOWN;
        break;
    }

    AppendFileInfoText(hEdit, SPLFormatStringOwned(L"%ls:\t%ls\r\n", LangStr(IDS_FINFO_VERSION).c_str(), LangStr(verStrID).c_str()));
    AppendFileInfoText(hEdit, SPLFormatStringOwned(L"%ls:\t%u\r\n", LangStr(IDS_FINFO_RECCOUNT).c_str(), DbfHdr->recordsCnt));
    AppendFileInfoText(hEdit, SPLFormatStringOwned(L"%ls:\t%u\r\n", LangStr(IDS_FINFO_FIELDCOUNT).c_str(), DbfHdr->fieldsCnt));
    AppendFileInfoText(hEdit, SPLFormatStringOwned(L"%ls:\t%u\r\n", LangStr(IDS_FINFO_MEMOCOUNT).c_str(), DbfHdr->memoFieldsCnt));

    std::wstring sizeText = SPLPrintDiskSizeOwned(SalGeneral, CQuadWord(DbfHdr->headerSize, 0), 2);
    AppendFileInfoText(hEdit, SPLFormatStringOwned(L"%ls:\t%ls\r\n", LangStr(IDS_FINFO_HDRSIZE).c_str(), sizeText.c_str()));
    sizeText = SPLPrintDiskSizeOwned(SalGeneral, CQuadWord(DbfHdr->recordSize, 0), 2);
    AppendFileInfoText(hEdit, SPLFormatStringOwned(L"%ls:\t%ls\r\n", LangStr(IDS_FINFO_RECSIZE).c_str(), sizeText.c_str()));
    AppendFileInfoText(hEdit, SPLFormatStringOwned(L"%ls:\t%u\r\n", LangStr(IDS_FINFO_CODEPAGE).c_str(), DbfHdr->codePage));

    return TRUE;
}

DWORD
CParserInterfaceDBF::GetRecordCount()
{
    if (Dbf == NULL)
    {
        TRACE_E("Invalid call to CParserInterfaceDBF::GetRecordCount: Dbf == NULL");
        return 0;
    }
    return DbfHdr->recordsCnt;
}

DWORD
CParserInterfaceDBF::GetFieldCount()
{
    if (Dbf == NULL)
    {
        TRACE_E("Invalid call to CParserInterfaceDBF::GetFieldCount: Dbf == NULL");
        return 0;
    }
    return DbfHdr->fieldsCnt;
}

BOOL CParserInterfaceDBF::GetFieldInfo(DWORD index, CFieldInfo* info)
{
    if (Dbf == NULL || info == NULL || index >= DbfHdr->fieldsCnt)
    {
        TRACE_E("Invalid call to CParserInterfaceDBF::GetFieldInfo");
        return FALSE;
    }

    DBF_FIELD* field = DbfFields + index;

    if (info->Name == NULL)
        info->NameMax = (int)strlen((char*)field->name) + 1;
    else
        lstrcpynA(info->Name, (char*)field->name, info->NameMax);
    if (field->type == DBF_FTYPE_NUM ||
        field->type == DBF_FTYPE_DATE ||
        field->type == DBF_FTYPE_FLOAT ||
        field->type == DBF_FTYPE_INT ||
        field->type == DBF_FTYPE_INT_V7 ||
        field->type == DBF_FTYPE_TSTAMP ||
        field->type == DBF_FTYPE_DTIME)
        info->LeftAlign = FALSE;
    else
        info->LeftAlign = TRUE;

    info->TextMax = info->FieldLen = field->len;
    if (field->type == DBF_FTYPE_LOGIC)
    {
        // A character count for column sizing - wide length is the correct measure.
        info->TextMax = (int)max(wcslen(LangStr(IDS_TRUE).c_str()), wcslen(LangStr(IDS_FALSE).c_str()));
    }
    else if (field->type == DBF_FTYPE_DATE)
    {
        info->TextMax += 2; // dd.mm.yyyy
    }
    else if ((field->type == DBF_FTYPE_TSTAMP) || (field->type == DBF_FTYPE_DTIME))
    {
        info->TextMax = 2 + 1 + 2 + 1 + 4 + 1 + 2 + 1 + 2 + 1 + 2; // dd.mm.yyyy hh:mm:ss
    }

    if (info->Type != NULL)
    {
        int textResID;
        switch (field->type)
        {
        case DBF_FTYPE_CHAR:
            textResID = IDS_FTYPE_CHAR;
            break;
        case DBF_FTYPE_NUM:
            textResID = IDS_FTYPE_NUM;
            break;
        case DBF_FTYPE_LOGIC:
            textResID = IDS_FTYPE_LOGICAL;
            break;
        case DBF_FTYPE_DATE:
            textResID = IDS_FTYPE_DATE;
            break;
        case DBF_FTYPE_MEMO:
            textResID = IDS_FTYPE_MEMO;
            break;
        case DBF_FTYPE_FLOAT:
            textResID = IDS_FTYPE_FLOAT;
            break;
        case DBF_FTYPE_BIN:
        {
            if (DbfHdr->version == DBF_FOXPRO)
                textResID = IDS_FTYPE_DOUBLE;
            else
                textResID = IDS_FTYPE_BIN;
            break;
        }
        case DBF_FTYPE_GEN:
            textResID = IDS_FTYPE_GEN;
            break;
        case DBF_FTYPE_PIC:
            textResID = IDS_FTYPE_PIC;
            break;
        case DBF_FTYPE_CUR:
            textResID = IDS_FTYPE_CUR;
            break;
        case DBF_FTYPE_DTIME:
            textResID = IDS_FTYPE_DTIME;
            break;
        case DBF_FTYPE_INT_V7:
        case DBF_FTYPE_INT:
            textResID = IDS_FTYPE_INT;
            break;
        case DBF_FTYPE_DOUBLE:
            textResID = IDS_FTYPE_DOUBLE;
            break;
        case DBF_FTYPE_TSTAMP:
            textResID = IDS_FTYPE_TSTAMP;
            break;
        case DBF_FTYPE_AUTOINC:
            textResID = IDS_FTYPE_AUTOINC;
            break;
        default:
            textResID = IDS_FTYPE_UNKOWN;
            break;
        }
        *info->Type = LangStr(textResID);
    }

    if (field->type == DBF_FTYPE_NUM)
        info->Decimals = field->decimals;
    else
        info->Decimals = -1; // show nothing

    return TRUE;
}

CParserStatusEnum
CParserInterfaceDBF::FetchRecord(DWORD index)
{
    if (Dbf == NULL || index >= DbfHdr->recordsCnt)
    {
        TRACE_E("Invalid call to CParserInterfaceDBF::FetchRecord");
        return psCount;
    }

    return TranslateDBFStatus(Dbf->GetRecord(index, Record));
}

char* Int64ToCurrency(char* buffer, __int64 number)
{
    /*
  int DecimalSeparatorLen;
  char DecimalSeparator[5];

  if ((DecimalSeparatorLen = GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, DecimalSeparator, 5)) == 0 ||
      DecimalSeparatorLen > 5)
  {
    strcpy(DecimalSeparator, ".");
    DecimalSeparatorLen = 1;
  }
  else
  {
    DecimalSeparatorLen--;
    DecimalSeparator[DecimalSeparatorLen] = 0;  // ensure a zero terminator at the end
  }
  */
    // other DBF values use '.' as the decimal separator, so we do not use the system one
    int DecimalSeparatorLen = 1;
    char DecimalSeparator[1] = {'.'};

    _i64toa(number, buffer, 10);
    int l = lstrlenA(buffer);
    if (l > 4)
    {
        char* s = buffer + l - 4;
        memmove(s + DecimalSeparatorLen, s, 5);
        memcpy(s, DecimalSeparator, DecimalSeparatorLen);
    }
    return buffer;
}

void TimeStampToDate(int jdays, int* pDay, int* pMon, int* pYear, BOOL bJulianDays)
{
    BOOL bLeap;
    int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    *pYear = 1600;
    *pDay = *pMon = 1;
    if (!bJulianDays)
    {
        // According to BCB 5.00 693594 is the number of days between 1/1/1 and 31/12/1899
        jdays -= 693594 - 2 * 100 * 365 - 24 - 25 - 99 * 365 - 24 - 364;
    }
    else
    {
        // old code assuming it was number of Julian days (since 1.1.4713 BC)
        // 1.1.1993 is claimed to be Julian Day 2448989
        // get the number of days since 1.1.1600 (jday == 1 means 1.1.1600)
        if (jdays < 2448989 - 93 * 365 - 23 - 3 * 100 * 365 - 2 * 24 - 25)
        {
            // Before 1.1. 1600. We do no support Julian calendar introduced AFAIR around 1584
            return;
        }
        // get the number of days since 1.1.1600 (jday == 1 means 1.1.1600
        jdays -= 2448989 - 1 - 93 * 365 - 23 - 3 * 100 * 365 - 2 * 24 - 25;
    }
    while (jdays > 365 + (bLeap = (!(*pYear % 4) && ((*pYear % 100) || !(*pYear % 400))) ? 1 : 0))
    {
        (*pYear)++;
        jdays -= 365 + bLeap;
    }
    if (bLeap)
    {
        // leap year -> 29 days in February
        mdays[1]++;
    }

    while (jdays > mdays[*pMon - 1])
    {
        jdays -= mdays[(*pMon)++ - 1];
    }
    *pDay = jdays;
} /* TimeStampToDate */

const char*
CParserInterfaceDBF::GetCellText(DWORD index, size_t* textLen)
{
    if (Dbf == NULL || textLen == NULL || index >= DbfHdr->fieldsCnt)
    {
        TRACE_E("Invalid call to CParserInterfaceDBF::GetCellText");
        if (textLen != NULL)
            *textLen = 0;
        return "";
    }

    static char Buffer[100];
    DBF_FIELD* field = DbfFields + index;
    const char* text = Record + field->posInRecord;
    switch (field->type)
    {
    case DBF_FTYPE_DATE:
    {
        size_t buffLen = 0;
        SYSTEMTIME st;
        ZeroMemory(&st, sizeof(st));
        lstrcpynA(Buffer, text, 5);
        st.wYear = atoi(Buffer);
        lstrcpynA(Buffer, text + 4, 3);
        st.wMonth = atoi(Buffer);
        lstrcpynA(Buffer, text + 6, 3);
        st.wDay = atoi(Buffer);
        if (st.wYear != 0 && st.wMonth != 0 && st.wDay != 0)
        {
            buffLen = GetDateFormatA(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, Buffer, 99);
            if (buffLen == 0)
            {
                sprintf(Buffer, "%u.%u.%u", st.wDay, st.wMonth, st.wYear);
                buffLen = strlen(Buffer);
            }
            else
                buffLen--;
        }
        *textLen = buffLen;
        return Buffer;
    }

    case DBF_FTYPE_LOGIC:
    {
        // The localized word, not an English literal. Painting takes the wide
        // TryGetLocalizedCellText path, but Find (renmain.cpp) and Copy-to-clipboard read the cell
        // through HERE, so hardcoding "True"/"False" meant search no longer matched what the user
        // could see and Copy pasted a different word than the panel showed. GetFieldInfo already
        // sizes the column from LangStr(IDS_TRUE)/LangStr(IDS_FALSE).
        //
        // This is the byte-oriented interface, so the word is projected onto the active code page
        // the way the narrow LoadStr used to hand it over. Lossy, because a degraded word still
        // lets the cell be searched and copied, where a refusal would blank it.
        const sally::dbviewer::LogicalCellValue value =
            sally::dbviewer::ClassifyLogicalCellByte(*text);
        Buffer[0] = 0; // Not initialised (default)
        if (value != sally::dbviewer::LogicalCellValue::Unknown)
        {
            std::string encoded;
            if (Win32EncodeAcpLossy(LangStr(value == sally::dbviewer::LogicalCellValue::True ? IDS_TRUE : IDS_FALSE),
                                    encoded)
                    .Succeeded())
                lstrcpynA(Buffer, encoded.c_str(), (int)sizeof(Buffer));
        }
        *textLen = strlen(Buffer);
        return Buffer;
    }

    case DBF_FTYPE_INT:
    {
        // Patera 2003.06.05: Why was there short int?
        // FoxPro is supposed to use this type as 32bit int!
        sprintf(Buffer, "%d", *((int*)text));
        *textLen = strlen(Buffer);
        return Buffer;
    }

    case DBF_FTYPE_AUTOINC:
    case DBF_FTYPE_INT_V7:
    { // Used by Visual dBase 7:
        //  The DOC: left-most bit used for sign, 0 means negative
        //  Observation: Seems to be big-endian.
        //  Wow! Got it! Its for faster string-based comparison (for indexing)!
        UINT32 val = LongSwap(*(int*)text);

        sprintf(Buffer, "%u", val ? (val ^ 0x80000000) : 0);
        *textLen = strlen(Buffer);
        return Buffer;
    }

    case DBF_FTYPE_CUR:
    {
        Int64ToCurrency(Buffer, *((__int64*)text));
        *textLen = strlen(Buffer);
        return Buffer;
    }

    case DBF_FTYPE_BIN:
    {
        if (DbfHdr->version == DBF_FOXPRO)
        {
            sprintf(Buffer, "%f", *((double*)text));
            *textLen = strlen(Buffer);
            return Buffer;
        }
        sprintf(Buffer, "memo");
        *textLen = strlen(Buffer);
        return Buffer;
    }

    case DBF_FTYPE_DOUBLE:
    {
        UINT32 tmp[2];
        // convert from big-endian to little-endian and again, as for long & autoinc, flip the sign
        tmp[1] = LongSwap(*(UINT32*)text);
        tmp[0] = LongSwap(((UINT32*)text)[1]);
        if (tmp[0] | tmp[1])
            tmp[1] ^= 0x80000000;
        sprintf(Buffer, "%8.8f", *((double*)tmp));
        *textLen = strlen(Buffer);
        return Buffer;
    }

    case DBF_FTYPE_DTIME:
    { // Visual FoxPro DateTime field: 8BYTE type:
        // Lower 4 bytes:  Julian day (since 1/1/4713 BC)
        // Higher 4 bytes: time in miliseconds
        int day, mon, year, jd;
        int hour, min, sec;
        __int64 tmp64;

        tmp64 = *(__int64*)text;
        jd = (int)(tmp64 & 0xffffffff);

        TimeStampToDate(jd, &day, &mon, &year, TRUE);

        jd = (int)(tmp64 >> 32);
        hour = jd / (60 * 60 * 1000);
        min = (jd / (60 * 1000)) % 60;
        sec = (jd / 1000) % 60;
        sprintf(Buffer, "%d.%d.%d %2d:%#02d:%#02d", day, mon, year, hour, min, sec);
        *textLen = strlen(Buffer);
        return Buffer;
    }

    case DBF_FTYPE_GEN:
    case DBF_FTYPE_MEMO:
    {
        strcpy(Buffer, "memo");
        *textLen = strlen(Buffer);
        return Buffer;
    }
    case DBF_FTYPE_CHAR:
    {
        const void* zero = memchr(text, 0, field->len);
        // ignore terminating binary zeros
        *textLen = zero ? ((char*)zero - text) : field->len;
        return text;
    }
    case DBF_FTYPE_TSTAMP:
    {
        int day, mon, year, jd;
        int hour, min, sec;
        __int64 tmp64;
        UINT32 tmp[2];

        // according to dBase doc, this type consists of two 32bits, number of days since 1/1/4713BC and miliseconds
        // VCL takes it as double whose integer part is number of miliseconds since 1/1/1

        // convert from big-endian to little-endian and again, as for long & autoinc, flip the sign
        tmp[1] = LongSwap(*(UINT32*)text);
        tmp[0] = LongSwap(((UINT32*)text)[1]);
        tmp64 = (__int64)(*(double*)tmp);
        jd = (int)(tmp64 / (24 * 60 * 60 * 1000));

        TimeStampToDate(jd, &day, &mon, &year, FALSE);

        jd = (int)(tmp64 % (__int64)(24 * 60 * 60 * 1000));
        hour = jd / (60 * 60 * 1000);
        min = (jd / (60 * 1000)) % 60;
        sec = (jd / 1000) % 60;
        sprintf(Buffer, "%d.%d.%d %2d:%#02d:%#02d", day, mon, year, hour, min, sec);
        *textLen = strlen(Buffer);
        return Buffer;
    }
    }
    *textLen = field->len;
    return text;
}

const wchar_t*
CParserInterfaceDBF::GetCellTextW(DWORD index, size_t* textLen)
{
    TRACE_E("Invalid call to CParserInterfaceDBF::GetCellTextW");
    if (textLen != NULL)
        *textLen = 0;
    return L"";
}

bool CParserInterfaceDBF::TryGetLocalizedCellText(DWORD index,
                                                  std::wstring& text)
{
    if (Dbf == NULL || index >= DbfHdr->fieldsCnt || Record == NULL)
        return false;
    const _dbf_field* field = DbfFields + index;
    if (field->type != DBF_FTYPE_LOGIC)
        return false;

    const sally::dbviewer::LogicalCellValue value =
        sally::dbviewer::ClassifyLogicalCellByte(Record[field->posInRecord]);
    int resource = 0;
    if (value == sally::dbviewer::LogicalCellValue::True)
        resource = IDS_TRUE;
    else if (value == sally::dbviewer::LogicalCellValue::False)
        resource = IDS_FALSE;
    else
        return false;
    try
    {
        std::wstring staged = LangStr(resource);
        text.swap(staged);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

CParserStatusEnum
CParserInterfaceDBF::TranslateDBFStatus(eDBFStatus status)
{
    CParserStatusEnum out = psCount;
    switch (status)
    {
    case DBFE_OK:
        out = psOK;
        break;
    case DBFE_OOM:
        out = psOOM;
        break;
    case DBFE_NOT_XBASE:
        out = psUnknownFile;
        break;
    case DBFE_FILE_NOT_FOUND:
        out = psFileNotFound;
        break;
    case DBFE_READ_ERROR:
        out = psReadError;
        break;
    case DBFE_WRITE_ERROR:
        out = psWriteError;
        break;
    case DBFE_SEEK_ERROR:
        out = psSeekError;
        break;
    case DBFE_NO_MEMO_FILE:
        out = psNoMemoFile;
        break;
    case DBFE_INVALID_BLOCK:
        out = psCorruptedMemo;
        break;
    }
    return out;
}

BOOL CParserInterfaceDBF::IsRecordDeleted()
{
    if (Dbf == NULL)
    {
        TRACE_E("Invalid call to CParserInterfaceDBF::IsRecordDeleted()");
        return FALSE;
    }
    return Record[0] == '*';
}

//****************************************************************************
//
// CParserInterfaceCSV
//

CParserInterfaceCSV::CParserInterfaceCSV(CCSVConfig* config)
{
    Csv = NULL;
    FileName.clear();
    Config = config;
    IsUTF8 = IsUnicode = FALSE;
}

CParserStatusEnum
CParserInterfaceCSV::OpenFile(const wchar_t* fileName)
{
    CParserStatusEnum status = psOK;

    if (Csv != NULL)
        CloseFile();

    // open the file
    char separator = ','; // default value if auto-detection fails
    BOOL autoSeparator = FALSE;
    switch (Config->ValueSeparator)
    {
    case 1:
        separator = '\t';
        break;
    case 2:
        separator = ';';
        break;
    case 3:
        separator = ',';
        break;
    case 4:
        separator = ' ';
        break;
    case 5:
        separator = Config->ValueSeparatorChar;
        break;
    default:
        autoSeparator = TRUE;
        break;
    }

    CCSVParserTextQualifier qualifier = CSVTQ_QUOTE; // default value if auto-detection fails
    BOOL autoQualifier = FALSE;
    switch (Config->TextQualifier)
    {
    case 1:
        qualifier = CSVTQ_QUOTE;
        break;
    case 2:
        qualifier = CSVTQ_SINGLEQUOTE;
        break;
    case 3:
        qualifier = CSVTQ_NONE;
        break;
    default:
        autoQualifier = TRUE;
        break;
    }

    BOOL firstRowAsName = FALSE;
    BOOL autoFirstRowAsName = FALSE;
    switch (Config->FirstRowAsName)
    {
    case 1:
        firstRowAsName = TRUE;
        break;
    case 2:
        firstRowAsName = FALSE;
        break;
    default:
        autoFirstRowAsName = TRUE;
        break;
    }

    FILE* f = _wfopen(fileName, L"r");
    if (f)
    {
        WORD w;
        fread(&w, 1, 2, f);
        IsUnicode = (w == 0xFEFF) || (w == 0xFFFE); // UTFE16 LE & BE BOM's
        // CP_UTF8
        if (w == 0xBBEF)
        {
            BYTE b;
            fread(&b, 1, 1, f);
            IsUnicode = IsUTF8 = b == 0xBF;
        }
        if (!IsUnicode)
        {
            char* buf = (char*)malloc(UTF8_DETECT_BUF_SIZE);
            if (buf)
            {
                fseek(f, 0, SEEK_SET);
                size_t len = fread(buf, 1, UTF8_DETECT_BUF_SIZE, f);
                if (len > 0)
                    IsUnicode = IsUTF8 = IsUTF8Encoded(buf, (int)len);
                free(buf);
            }
        }
        fclose(f);
    }

    if (!IsUnicode)
        Csv = new CCSVParser<char>(fileName,
                                   autoSeparator, separator,
                                   autoQualifier, qualifier,
                                   autoFirstRowAsName, firstRowAsName);
    else
    {
        WCHAR separatorW = separator;
        std::wstring decodedSeparator;
        if (Win32DecodeText(CP_ACP, &separator, 1, decodedSeparator) && decodedSeparator.size() == 1)
            separatorW = decodedSeparator.front();
        if (IsUTF8)
            Csv = new CCSVParserUTF8(fileName,
                                     autoSeparator, separator,
                                     autoQualifier, qualifier,
                                     autoFirstRowAsName, firstRowAsName);
        else
            Csv = new CCSVParser<wchar_t>(fileName,
                                          autoSeparator, separatorW,
                                          autoQualifier, qualifier,
                                          autoFirstRowAsName, firstRowAsName);
    }
    if (Csv != NULL)
    {
        if (Csv->GetStatus() != CSVE_OK)
        {
            status = TranslateCSVStatus(Csv->GetStatus());
            CloseFile(); // after Close, Csv will be NULL
        }
    }
    else
    {
        status = psOOM;
    }

    if (status == psOK)
        FileName = fileName;

    return status;
}

void CParserInterfaceCSV::CloseFile()
{
    if (Csv != NULL)
    {
        delete Csv;
        Csv = NULL;
    }
    FileName.clear();
}

BOOL CParserInterfaceCSV::GetFileInfo(HWND hEdit)
{
    if (Csv == NULL)
    {
        TRACE_E("Invalid call to CParserInterfaceCSV::GetFileInfo: Csv == NULL");
        return FALSE;
    }

    SetWindowTextW(hEdit, L"");
    DWORD tab = 80;
    SendMessage(hEdit, EM_SETTABSTOPS, 1, (LPARAM)&tab);
    AppendCommonFileInfo(hEdit, FileName);
    AppendFileInfoText(hEdit, SPLFormatStringOwned(L"%ls:\t%u\r\n", LangStr(IDS_FINFO_RECCOUNT).c_str(), GetRecordCount()));
    AppendFileInfoText(hEdit, SPLFormatStringOwned(L"%ls:\t%u\r\n", LangStr(IDS_FINFO_FIELDCOUNT).c_str(), GetFieldCount()));

    return TRUE;
}

DWORD
CParserInterfaceCSV::GetRecordCount()
{
    if (Csv == NULL)
    {
        TRACE_E("Invalid call to CParserInterfaceCSV::GetRecordCount: Csv == NULL");
        return 0;
    }
    return Csv->GetRecordsCnt();
}

DWORD
CParserInterfaceCSV::GetFieldCount()
{
    if (Csv == NULL)
    {
        TRACE_E("Invalid call to CParserInterfaceCSV::GetFieldCount: Csv == NULL");
        return 0;
    }

    return Csv->GetColumnsCnt();
}

BOOL CParserInterfaceCSV::GetFieldInfo(DWORD index, CFieldInfo* info)
{
    if (Csv == NULL || index >= Csv->GetColumnsCnt())
    {
        TRACE_E("Invalid call to CParserInterfaceCSV::GetFieldInfo");
        return FALSE;
    }

    char buff[30];
    union
    {
        LPCSTR s;
        LPCWSTR sw;
    } colName;

    colName.s = Csv->GetColumnName(index);

    if (colName.s == NULL)
    {
        if (!IsUnicode)
            sprintf(buff, "%u", index);
        else
            swprintf((LPWSTR)buff, sizeof(buff) / sizeof(WCHAR), L"%d", index);
        colName.s = buff;
    }

    if (!IsUnicode)
    {
        if (info->Name == NULL)
            info->NameMax = (int)strlen(colName.s) + 1;
        else
            lstrcpynA(info->Name, colName.s, info->NameMax);
    }
    else
    {
        if (info->Name == NULL)
            info->NameMax = sizeof(WCHAR) * ((int)wcslen(colName.sw) + 1);
        else
        {
            wcsncpy((LPWSTR)info->Name, colName.sw, info->NameMax / sizeof(WCHAR));
            ((LPWSTR)info->Name)[info->NameMax / sizeof(WCHAR) - 1] = 0;
        }
    }
    info->LeftAlign = TRUE;
    info->TextMax = Csv->GetColumnMaxLen(index);

    if (info->Type != NULL)
        *info->Type = LangStr(IDS_FTYPE_CHAR);

    info->FieldLen = info->Decimals = -1;

    return TRUE;
}

CParserStatusEnum
CParserInterfaceCSV::FetchRecord(DWORD index)
{
    if (Csv == NULL || index >= Csv->GetRecordsCnt())
    {
        TRACE_E("Invalid call to CParserInterfaceCSV::FetchRecord");
        return psCount;
    }
    return TranslateCSVStatus(Csv->FetchRecord(index));
}

const char*
CParserInterfaceCSV::GetCellText(DWORD index, size_t* textLen)
{
    if (IsUnicode || Csv == NULL || index >= Csv->GetColumnsCnt())
    {
        TRACE_E("Invalid call to CParserInterfaceCSV::GetCellText");
        *textLen = 0;
        return "";
    }
    return (char*)Csv->GetCellText(index, textLen);
}

const wchar_t*
CParserInterfaceCSV::GetCellTextW(DWORD index, size_t* textLen)
{
    if (!IsUnicode || Csv == NULL || index >= Csv->GetColumnsCnt())
    {
        TRACE_E("Invalid call to CParserInterfaceCSV::GetCellTextW");
        *textLen = 0;
        return L"";
    }
    return (wchar_t*)Csv->GetCellText(index, textLen);
}

CParserStatusEnum
CParserInterfaceCSV::TranslateCSVStatus(CCSVParserStatus status)
{
    CParserStatusEnum out = psCount;

    switch (status)
    {
    case CSVE_OK:
        out = psOK;
        break;
    case CSVE_OOM:
        out = psOOM;
        break;
    case CSVE_NOT_CSV:
        out = psUnknownFile;
        break;
    case CSVE_FILE_NOT_FOUND:
        out = psFileNotFound;
        break;
    case CSVE_READ_ERROR:
        out = psReadError;
        break;
    case CSVE_SEEK_ERROR:
        out = psSeekError;
        break;
    default:
        if ((status & CSVE_MASK) == CSVE_SYSTEM_ERROR)
        {
            // OS error obtained via GetLastError
            out = (CParserStatusEnum)((status & ~CSVE_MASK) | psSystemError);
        }
    }
    return out;
}

BOOL CParserInterfaceCSV::IsRecordDeleted()
{
    // the CSV format does not support this state
    return FALSE;
}
