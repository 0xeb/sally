// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"

//
//*****************************************************************************

// Since Windows XP, there is StrCmpLogicalW in the system, which Explorer uses for this comparison
int StrCmpLogicalEx(const char* s1, int l1, const char* s2, int l2, BOOL* numericalyEqual, BOOL ignoreCase)
{
    const char* strEnd1 = s1 + l1; // end of string 's1'
    const char* beg1 = s1;         // beginning of segment (text or number)
    const char* end1 = s1;         // end of segment (text or number)
    const char* strEnd2 = s2 + l2; // end of string 's2'
    const char* beg2 = s2;         // beginning of segment (text or number)
    const char* end2 = s2;         // end of segment (text or number)
    int suggestion = 0;            // "suggestion" for result (0 / -1 / 1 = nothing / s1<s2 / s1>s2) - e.g. "001" < "01"

    BOOL findDots = WindowsVistaAndLater && !SystemPolicies.GetNoDotBreakInLogicalCompare(); // TRUE = names are split also by dots (not only by numbers)

    while (1)
    {
        const char* numBeg1 = NULL; // position of first non-zero digit
        BOOL isStr1 = (end1 >= strEnd1 || *end1 < '0' || *end1 > '9');
        if (isStr1) // text (even empty) or dot
        {
            if (findDots && end1 < strEnd1 && *end1 == '.')
                end1++; // dot: if we are looking for them, take one at a time
            else        // text (even empty)
            {
                while (end1 < strEnd1 && (*end1 < '0' || *end1 > '9') && (!findDots || *end1 != '.'))
                    end1++;
            }
        }
        else // number
        {
            while (end1 < strEnd1 && *end1 >= '0' && *end1 <= '9')
            {
                if (numBeg1 == NULL && *end1 != '0')
                    numBeg1 = end1;
                end1++;
            }
        }
        const char* numBeg2 = NULL; // position of first non-zero digit
        BOOL isStr2 = (end2 >= strEnd2 || *end2 < '0' || *end2 > '9');
        if (isStr2) // text (even empty) or dot
        {
            if (findDots && end2 < strEnd2 && *end2 == '.')
                end2++; // dot: if we are looking for them, take one at a time
            else        // text (even empty)
            {
                while (end2 < strEnd2 && (*end2 < '0' || *end2 > '9') && (!findDots || *end2 != '.'))
                    end2++;
            }
        }
        else // number
        {
            while (end2 < strEnd2 && *end2 >= '0' && *end2 <= '9')
            {
                if (numBeg2 == NULL && *end2 != '0')
                    numBeg2 = end2;
                end2++;
            }
        }

        if (isStr1 || isStr2) // comparison of text, dots or combined pairs of text, dots or numbers (everything except two numbers is compared as strings)
        {
            int ret;
            if (Configuration.SortUsesLocale)
            {
                ret = CompareString(LOCALE_USER_DEFAULT, ignoreCase ? NORM_IGNORECASE : 0,
                                    beg1, (int)(end1 - beg1), beg2, (int)(end2 - beg2)) -
                      CSTR_EQUAL;
            }
            else
            {
                if (ignoreCase)
                    ret = StrICmpEx(beg1, (int)(end1 - beg1), beg2, (int)(end2 - beg2));
                else
                    ret = StrCmpEx(beg1, (int)(end1 - beg1), beg2, (int)(end2 - beg2));
            }
            if (ret != 0)
            {
                if (numericalyEqual != NULL)
                    *numericalyEqual = FALSE;
                return ret;
            }
        }
        else // comparison of two numbers
        {
            if (numBeg1 == NULL)
            {
                if (numBeg2 == NULL) // both numbers are zero
                {
                    if (suggestion == 0) // we are only interested in the first "suggestion" for result
                    {
                        if (end1 - beg1 > end2 - beg2)
                            suggestion = -1; // "000" < "00"
                        else if (end1 - beg1 < end2 - beg2)
                            suggestion = 1; // "00" > "000"
                    }
                }
                else // first number is zero, second number is not zero
                {
                    if (numericalyEqual != NULL)
                        *numericalyEqual = FALSE;
                    return -1; // "00" < "1"
                }
            }
            else
            {
                if (numBeg2 == NULL) // first number is not zero, second number is zero
                {
                    if (numericalyEqual != NULL)
                        *numericalyEqual = FALSE;
                    return 1; // "1" > "00"
                }
                else // both numbers are non-zero
                {
                    if (end1 - numBeg1 > end2 - numBeg2) // first number has more digits than second
                    {
                        if (numericalyEqual != NULL)
                            *numericalyEqual = FALSE;
                        return 1; // "100" > "99"
                    }
                    else
                    {
                        if (end1 - numBeg1 < end2 - numBeg2) // second number has more digits than first
                        {
                            if (numericalyEqual != NULL)
                                *numericalyEqual = FALSE;
                            return -1; // "99" < "100"
                        }
                        else // numbers have the same number of digits, compare them by value (equivalent to string comparison)
                        {
                            int ret = StrCmpEx(numBeg1, (int)(end1 - numBeg1), numBeg2, (int)(end2 - numBeg2));
                            if (ret != 0) // values are not equal
                            {
                                if (numericalyEqual != NULL)
                                    *numericalyEqual = FALSE;
                                return ret;
                            }
                            else // number values are the same, if they differ in the number of zeros in prefix, adopt this into "suggestion" for result
                            {
                                if (suggestion == 0) // we are only interested in the first "suggestion" for result
                                {
                                    if (end1 - beg1 > end2 - beg2)
                                        suggestion = -1; // "0001" < "001"
                                    else if (end1 - beg1 < end2 - beg2)
                                        suggestion = 1; // "001" > "0001"
                                }
                            }
                        }
                    }
                }
            }
        }

        if (end1 >= strEnd1 && end2 >= strEnd2)
            break; // end of comparison
        beg1 = end1;
        beg2 = end2;
    }

    if (numericalyEqual != NULL)
        *numericalyEqual = TRUE; // s1 and s2 are equal or numerically equal
    return suggestion;           // on equality or numerical equality we return the "suggested" result
}

//
//*****************************************************************************

int RegSetStrICmp(const char* s1, const char* s2)
{
    if (Configuration.SortDetectNumbers)
    {
        return StrCmpLogicalEx(s1, (int)strlen(s1), s2, (int)strlen(s2), NULL, TRUE);
    }
    else
    {
        if (Configuration.SortUsesLocale)
        {
            return CompareString(LOCALE_USER_DEFAULT, NORM_IGNORECASE, s1, -1, s2, -1) - CSTR_EQUAL;
        }
        else
        {
            return StrICmp(s1, s2);
        }
    }
}

int RegSetStrICmpEx(const char* s1, int l1, const char* s2, int l2, BOOL* numericalyEqual)
{
    if (Configuration.SortDetectNumbers)
    {
        return StrCmpLogicalEx(s1, l1, s2, l2, numericalyEqual, TRUE);
    }
    else
    {
        int ret;
        if (Configuration.SortUsesLocale)
        {
            ret = CompareString(LOCALE_USER_DEFAULT, NORM_IGNORECASE, s1, l1, s2, l2) - CSTR_EQUAL;
        }
        else
        {
            ret = StrICmpEx(s1, l1, s2, l2);
        }
        if (numericalyEqual != NULL)
            *numericalyEqual = ret == 0;
        return ret;
    }
}

int RegSetStrCmp(const char* s1, const char* s2)
{
    if (Configuration.SortDetectNumbers)
    {
        return StrCmpLogicalEx(s1, (int)strlen(s1), s2, (int)strlen(s2), NULL, FALSE);
    }
    else
    {
        if (Configuration.SortUsesLocale)
        {
            return CompareString(LOCALE_USER_DEFAULT, 0, s1, -1, s2, -1) - CSTR_EQUAL;
        }
        else
        {
            return strcmp(s1, s2);
        }
    }
}

int RegSetStrCmpEx(const char* s1, int l1, const char* s2, int l2, BOOL* numericalyEqual)
{
    if (Configuration.SortDetectNumbers)
    {
        return StrCmpLogicalEx(s1, l1, s2, l2, numericalyEqual, FALSE);
    }
    else
    {
        int ret;
        if (Configuration.SortUsesLocale)
        {
            ret = CompareString(LOCALE_USER_DEFAULT, 0, s1, l1, s2, l2) - CSTR_EQUAL;
        }
        else
        {
            ret = StrCmpEx(s1, l1, s2, l2);
        }
        if (numericalyEqual != NULL)
            *numericalyEqual = ret == 0;
        return ret;
    }
}

//
//*****************************************************************************
// QuickSort   1.key Name, 2.key Ext
//

int CmpNameExtIgnCase(const CFileData& f1, const CFileData& f2)
{
    /*
//--- first by Name
  BOOL numericalyEqual1;
  int res1 = RegSetStrICmpEx(f1.Name, (*f1.Ext != 0) ? (f1.Ext - 1 - f1.Name) : f1.NameLen,
                             f2.Name, (*f2.Ext != 0) ? (f2.Ext - 1 - f2.Name) : f2.NameLen,
                             &numericalyEqual1);
  if (!numericalyEqual1) return res1;   // names differ (are not equal nor numerically equal)
//--- by Name they are equal, Ext decides
  BOOL numericalyEqual2;
  int res2 = RegSetStrICmpEx(f1.Ext, f1.NameLen - (f1.Ext - f1.Name),
                             f2.Ext, f2.NameLen - (f2.Ext - f2.Name),
                             &numericalyEqual2);
  if (numericalyEqual2 && res1 != 0) return res1; // extensions are equal or numerically equal and names are only numerically equal (name comparison has priority)
  else return res2;
*/
    //--- we compare the whole Name (including Ext), like Explorer
    return RegSetStrICmpEx(f1.Name, f1.NameLen, f2.Name, f2.NameLen, NULL);
}

int CmpNameExt(const CFileData& f1, const CFileData& f2)
{
    /*  // old variant: we compare name and extension separately
//--- first by Name
  BOOL numericalyEqual1;
  int res1 = RegSetStrICmpEx(f1.Name, (*f1.Ext != 0) ? (f1.Ext - 1 - f1.Name) : f1.NameLen,
                             f2.Name, (*f2.Ext != 0) ? (f2.Ext - 1 - f2.Name) : f2.NameLen,
                             &numericalyEqual1);
  if (!numericalyEqual1) return res1;   // names differ (are not equal nor numerically equal)
//--- by Name they are equal, Ext decides
  BOOL numericalyEqual2;
  int res2 = RegSetStrICmpEx(f1.Ext, f1.NameLen - (f1.Ext - f1.Name),
                             f2.Ext, f2.NameLen - (f2.Ext - f2.Name),
                             &numericalyEqual2);
  if (numericalyEqual2 && res1 != 0) return res1; // extensions are equal or numerically equal and names are only numerically equal (name comparison has priority)
  else
  {
    if (res2 != 0 || f1.Name == f2.Name) return res2; // if addresses are the same, they must be equal
  }
//--- equal names (archives or FS) - try if they differ at least in letter case
  res1 = RegSetStrCmpEx(f1.Name, (*f1.Ext != 0) ? (f1.Ext - 1 - f1.Name) : f1.NameLen,
                        f2.Name, (*f2.Ext != 0) ? (f2.Ext - 1 - f2.Name) : f2.NameLen,
                        &numericalyEqual1);
  if (!numericalyEqual1) return res1;   // names differ (are not equal nor numerically equal)
//--- by Name they are again equal, Ext decides
  res2 = RegSetStrCmpEx(f1.Ext, f1.NameLen - (f1.Ext - f1.Name),
                        f2.Ext, f2.NameLen - (f2.Ext - f2.Name),
                        &numericalyEqual2);
  if (numericalyEqual2 && res1 != 0) return res1; // extensions are equal or numerically equal and names are only numerically equal (name comparison has priority)
  else return res2;
*/
    //--- we compare the whole Name (including Ext), like Explorer
    int res = RegSetStrICmpEx(f1.Name, f1.NameLen, f2.Name, f2.NameLen, NULL);
    if (res != 0 || f1.Name == f2.Name)
        return res; // if addresses are the same, they must be equal
                    //--- equal names (archives or FS) - try if they differ at least in letter case
    return RegSetStrCmpEx(f1.Name, f1.NameLen, f2.Name, f2.NameLen, NULL);
}

BOOL LessNameExt(const CFileData& f1, const CFileData& f2, BOOL reverse)
{
    int res = CmpNameExt(f1, f2);
    return reverse ? res > 0 : res < 0;
}

BOOL LessNameExtIgnCase(const CFileData& f1, const CFileData& f2, BOOL reverse)
{
    int res = CmpNameExtIgnCase(f1, f2);
    return reverse ? res > 0 : res < 0;
}

void SortNameExtAux(CFilesArray& files, int left, int right, BOOL reverse)
{

LABEL_SortNameExtAux:

    int i = left, j = right;
    CFileData pivot = files[(i + j) / 2];

    do
    {
        while (LessNameExt(files[i], pivot, reverse) && i < right)
            i++;
        while (LessNameExt(pivot, files[j], reverse) && j > left)
            j--;

        if (i <= j)
        {
            CFileData swap = files[i];
            files[i] = files[j];
            files[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // the following "nice" code was replaced by code significantly saving stack (max. log(N) recursion depth)
    //  if (left < j) SortNameExtAux(files, left, j, reverse);
    //  if (i < right) SortNameExtAux(files, i, right, reverse);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // need to sort both "halves", so we send the smaller one to recursion, process the other via "goto"
            {
                SortNameExtAux(files, left, j, reverse);
                left = i;
                goto LABEL_SortNameExtAux;
            }
            else
            {
                SortNameExtAux(files, i, right, reverse);
                right = j;
                goto LABEL_SortNameExtAux;
            }
        }
        else
        {
            right = j;
            goto LABEL_SortNameExtAux;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_SortNameExtAux;
        }
    }
}

void SortNameExt(CFilesArray& files, int left, int right, BOOL reverse)
{
    SortNameExtAux(files, left, right, reverse);
}

//
//*****************************************************************************
// QuickSort   1.key Ext, 2.key Name
//

BOOL LessExtName(const CFileData& f1, const CFileData& f2, BOOL reverse)
{
    //--- first by Ext
    BOOL numericalyEqual1;
    int res1 = RegSetStrICmpEx(f1.Ext, f1.NameLen - (int)(f1.Ext - f1.Name),
                               f2.Ext, f2.NameLen - (int)(f2.Ext - f2.Name),
                               &numericalyEqual1);
    if (!numericalyEqual1)
        return reverse ? res1 > 0 : res1 < 0; // extensions differ (are not equal nor numerically equal)
                                              //--- by Ext they are equal, Name decides
    BOOL numericalyEqual2;
    int res2 = RegSetStrICmpEx(f1.Name, (*f1.Ext != 0) ? (int)(f1.Ext - 1 - f1.Name) : f1.NameLen,
                               f2.Name, (*f2.Ext != 0) ? (int)(f2.Ext - 1 - f2.Name) : f2.NameLen,
                               &numericalyEqual2);
    if (numericalyEqual2 && res1 != 0)
        return reverse ? res1 > 0 : res1 < 0; // extensions are equal or numerically equal and names are only numerically equal (name comparison has priority)
    else
    {
        if (res2 == 0 && f1.Name != f2.Name) // equal names (archives or FS) - try if they differ at least in letter case
        {
            res1 = RegSetStrCmpEx(f1.Ext, f1.NameLen - (int)(f1.Ext - f1.Name),
                                  f2.Ext, f2.NameLen - (int)(f2.Ext - f2.Name),
                                  &numericalyEqual1);
            if (!numericalyEqual1)
                return reverse ? res1 > 0 : res1 < 0; // extensions differ (are not equal nor numerically equal)
            //--- by Ext they are again equal, Name decides
            res2 = RegSetStrCmpEx(f1.Name, (*f1.Ext != 0) ? (int)(f1.Ext - 1 - f1.Name) : f1.NameLen,
                                  f2.Name, (*f2.Ext != 0) ? (int)(f2.Ext - 1 - f2.Name) : f2.NameLen,
                                  &numericalyEqual2);
            if (numericalyEqual2 && res1 != 0)
                return reverse ? res1 > 0 : res1 < 0; // names are equal or numerically equal and extensions are only numerically equal (extension comparison has priority)
        }
        return reverse ? res2 > 0 : res2 < 0;
    }
}

void SortExtNameAux(CFilesArray& files, int left, int right, BOOL reverse)
{

LABEL_SortExtNameAux:

    int i = left, j = right;
    CFileData pivot = files[(i + j) / 2];

    do
    {
        while (LessExtName(files[i], pivot, reverse) && i < right)
            i++;
        while (LessExtName(pivot, files[j], reverse) && j > left)
            j--;

        if (i <= j)
        {
            CFileData swap = files[i];
            files[i] = files[j];
            files[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // the following "nice" code was replaced by code significantly saving stack (max. log(N) recursion depth)
    //  if (left < j) SortExtNameAux(files, left, j, reverse);
    //  if (i < right) SortExtNameAux(files, i, right, reverse);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // need to sort both "halves", so we send the smaller one to recursion, process the other via "goto"
            {
                SortExtNameAux(files, left, j, reverse);
                left = i;
                goto LABEL_SortExtNameAux;
            }
            else
            {
                SortExtNameAux(files, i, right, reverse);
                right = j;
                goto LABEL_SortExtNameAux;
            }
        }
        else
        {
            right = j;
            goto LABEL_SortExtNameAux;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_SortExtNameAux;
        }
    }
}

void SortExtName(CFilesArray& files, int left, int right, BOOL reverse)
{
    SortExtNameAux(files, left, right, reverse);
}

//
//*****************************************************************************
// QuickSort   1.key Time 2.key Name, 3.key Ext
//

BOOL LessTimeNameExt(const CFileData& f1, const CFileData& f2, BOOL reverse)
{
    //--- first by Time
    int res = CompareFileTime(&f1.LastWrite, &f2.LastWrite);
    if (res != 0)
        return (reverse ^ Configuration.SortNewerOnTop) ? res > 0 : res < 0;
    //--- by Time they are equal, next Name
    res = CmpNameExt(f1, f2);
    return reverse ? res > 0 : res < 0;
}

void SortTimeNameExtAux(CFilesArray& files, int left, int right, BOOL reverse)
{

LABEL_SortTimeNameExtAux:

    int i = left, j = right;
    CFileData pivot = files[(i + j) / 2];

    do
    {
        while (LessTimeNameExt(files[i], pivot, reverse) && i < right)
            i++;
        while (LessTimeNameExt(pivot, files[j], reverse) && j > left)
            j--;

        if (i <= j)
        {
            CFileData swap = files[i];
            files[i] = files[j];
            files[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // the following "nice" code was replaced by code significantly saving stack (max. log(N) recursion depth)
    //  if (left < j) SortTimeNameExtAux(files, left, j, reverse);
    //  if (i < right) SortTimeNameExtAux(files, i, right, reverse);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // need to sort both "halves", so we send the smaller one to recursion, process the other via "goto"
            {
                SortTimeNameExtAux(files, left, j, reverse);
                left = i;
                goto LABEL_SortTimeNameExtAux;
            }
            else
            {
                SortTimeNameExtAux(files, i, right, reverse);
                right = j;
                goto LABEL_SortTimeNameExtAux;
            }
        }
        else
        {
            right = j;
            goto LABEL_SortTimeNameExtAux;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_SortTimeNameExtAux;
        }
    }
}

void SortTimeNameExt(CFilesArray& files, int left, int right, BOOL reverse)
{
    SortTimeNameExtAux(files, left, right, reverse);
}

//
//*****************************************************************************
// QuickSort   1.key Size 2.key Name, 3.key Ext
//

BOOL LessSizeNameExt(const CFileData& f1, const CFileData& f2, BOOL reverse)
{
    //--- first by Size
    if (f1.Size != f2.Size)
        return reverse ? f1.Size > f2.Size : f1.Size < f2.Size; // first file = largest file
                                                                //--- by Size they are equal, next Name
    int res = CmpNameExt(f1, f2);
    return reverse ? res > 0 : res < 0;
}

void SortSizeNameExtAux(CFilesArray& files, int left, int right, BOOL reverse)
{

LABEL_SortSizeNameExtAux:

    int i = left, j = right;
    CFileData pivot = files[(i + j) / 2];

    do
    {
        while (LessSizeNameExt(files[i], pivot, reverse) && i < right)
            i++;
        while (LessSizeNameExt(pivot, files[j], reverse) && j > left)
            j--;

        if (i <= j)
        {
            CFileData swap = files[i];
            files[i] = files[j];
            files[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // the following "nice" code was replaced by code significantly saving stack (max. log(N) recursion depth)
    //  if (left < j) SortSizeNameExtAux(files, left, j, reverse);
    //  if (i < right) SortSizeNameExtAux(files, i, right, reverse);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // need to sort both "halves", so we send the smaller one to recursion, process the other via "goto"
            {
                SortSizeNameExtAux(files, left, j, reverse);
                left = i;
                goto LABEL_SortSizeNameExtAux;
            }
            else
            {
                SortSizeNameExtAux(files, i, right, reverse);
                right = j;
                goto LABEL_SortSizeNameExtAux;
            }
        }
        else
        {
            right = j;
            goto LABEL_SortSizeNameExtAux;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_SortSizeNameExtAux;
        }
    }
}

void SortSizeNameExt(CFilesArray& files, int left, int right, BOOL reverse)
{
    SortSizeNameExtAux(files, left, right, reverse);
}

//
//*****************************************************************************
// QuickSort   1.key Attr 2.key Name, 3.key Ext
//

BOOL LessAttrNameExt(const CFileData& f1, const CFileData& f2, BOOL reverse)
{
    // copy FILE_ATTRIBUTE_READONLY to the most significant bit
    //  DWORD f1Attr = f1.Attr;
    //  DWORD f2Attr = f2.Attr;
    //  if (f1.Attr & FILE_ATTRIBUTE_READONLY) f1Attr |= 0x80000000;
    //  if (f2.Attr & FILE_ATTRIBUTE_READONLY) f2Attr |= 0x80000000;

    // if we support displaying another attribute,
    // need to extend the DISPLAYED_ATTRIBUTES mask

    // we switch to alphabetical sorting, as explorer and speed commander have
    DWORD f1Attr = 0;
    DWORD f2Attr = 0;
    if (f1.Attr & FILE_ATTRIBUTE_ARCHIVE)
        f1Attr |= 0x00000001;
    if (f1.Attr & FILE_ATTRIBUTE_COMPRESSED)
        f1Attr |= 0x00000002;
    if (f1.Attr & FILE_ATTRIBUTE_ENCRYPTED)
        f1Attr |= 0x00000004;
    if (f1.Attr & FILE_ATTRIBUTE_HIDDEN)
        f1Attr |= 0x00000008;
    if (f1.Attr & FILE_ATTRIBUTE_READONLY)
        f1Attr |= 0x00000010;
    if (f1.Attr & FILE_ATTRIBUTE_SYSTEM)
        f1Attr |= 0x00000020;
    if (f1.Attr & FILE_ATTRIBUTE_TEMPORARY)
        f1Attr |= 0x00000040;

    if (f2.Attr & FILE_ATTRIBUTE_ARCHIVE)
        f2Attr |= 0x00000001;
    if (f2.Attr & FILE_ATTRIBUTE_COMPRESSED)
        f2Attr |= 0x00000002;
    if (f2.Attr & FILE_ATTRIBUTE_ENCRYPTED)
        f2Attr |= 0x00000004;
    if (f2.Attr & FILE_ATTRIBUTE_HIDDEN)
        f2Attr |= 0x00000008;
    if (f2.Attr & FILE_ATTRIBUTE_READONLY)
        f2Attr |= 0x00000010;
    if (f2.Attr & FILE_ATTRIBUTE_SYSTEM)
        f2Attr |= 0x00000020;
    if (f2.Attr & FILE_ATTRIBUTE_TEMPORARY)
        f2Attr |= 0x00000040;

    //--- first by Attr
    if (f1Attr != f2Attr)
        return reverse ? f1Attr > f2Attr : f1Attr < f2Attr;
    //--- by Attr they are equal, next Name
    int res = CmpNameExt(f1, f2);
    return reverse ? res > 0 : res < 0;
}

void SortAttrNameExtAux(CFilesArray& files, int left, int right, BOOL reverse)
{

LABEL_SortAttrNameExtAux:

    int i = left, j = right;
    CFileData pivot = files[(i + j) / 2];

    do
    {
        while (LessAttrNameExt(files[i], pivot, reverse) && i < right)
            i++;
        while (LessAttrNameExt(pivot, files[j], reverse) && j > left)
            j--;

        if (i <= j)
        {
            CFileData swap = files[i];
            files[i] = files[j];
            files[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // the following "nice" code was replaced by code significantly saving stack (max. log(N) recursion depth)
    //  if (left < j) SortAttrNameExtAux(files, left, j, reverse);
    //  if (i < right) SortAttrNameExtAux(files, i, right, reverse);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // need to sort both "halves", so we send the smaller one to recursion, process the other via "goto"
            {
                SortAttrNameExtAux(files, left, j, reverse);
                left = i;
                goto LABEL_SortAttrNameExtAux;
            }
            else
            {
                SortAttrNameExtAux(files, i, right, reverse);
                right = j;
                goto LABEL_SortAttrNameExtAux;
            }
        }
        else
        {
            right = j;
            goto LABEL_SortAttrNameExtAux;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_SortAttrNameExtAux;
        }
    }
}

void SortAttrNameExt(CFilesArray& files, int left, int right, BOOL reverse)
{
    SortAttrNameExtAux(files, left, right, reverse);
}

//
//*****************************************************************************
// QuickSort for integer
//

void IntSort(int array[], int left, int right)
{

LABEL_IntSort:

    int i = left, j = right;
    int pivot = array[(i + j) / 2];

    do
    {
        while (array[i] < pivot && i < right)
            i++;
        while (pivot < array[j] && j > left)
            j--;

        if (i <= j)
        {
            int swap = array[i];
            array[i] = array[j];
            array[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // the following "nice" code was replaced by code significantly saving stack (max. log(N) recursion depth)
    //  if (left < j) IntSort(array, left, j);
    //  if (i < right) IntSort(array, i, right);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // need to sort both "halves", so we send the smaller one to recursion, process the other via "goto"
            {
                IntSort(array, left, j);
                left = i;
                goto LABEL_IntSort;
            }
            else
            {
                IntSort(array, i, right);
                right = j;
                goto LABEL_IntSort;
            }
        }
        else
        {
            right = j;
            goto LABEL_IntSort;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_IntSort;
        }
    }
}
