// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "common/text/WideCollation.h"

// The collation core takes its switches as parameters; this is the
// one place that reads them out of Configuration.
static CWideCollationOptions CurrentCollationOptions()
{
    CWideCollationOptions o;
    o.UsesLocale = Configuration.SortUsesLocale;
    o.DetectNumbers = Configuration.SortDetectNumbers;
    o.BreakOnDots = WindowsVistaAndLater && !SystemPolicies.GetNoDotBreakInLogicalCompare();
    return o;
}

//
//*****************************************************************************

// The narrow StrCmpLogicalEx/RegSetStrICmp/RegSetStrICmpEx/RegSetStrCmp/
// RegSetStrCmpEx quintet that used to live here is gone - deleted, not widened. Confirmed
// zero live callers anywhere in the tree (core or plugins): every real caller already goes
// through the ...W twins below (RegSetStrICmpW/RegSetStrICmpExW/RegSetStrCmpW/RegSetStrCmpExW,
// which call StrCmpLogicalExW in common/text/WideCollation.cpp/.h, a separate, more complete
// implementation with its own collation-options parameter - not a width-retyped copy of the
// function removed here). The freefn-width-scan flagged StrCmpLogicalEx's header declaration
// (already wide, unlike its narrow definition) as a possible link-time-masked mismatch, but
// tracing every caller found the narrow definition itself was orphaned, not lagging - the same
// "deletion is cheaper than choosing a width for something nobody calls" call this codebase
// already made for AddDoubleQuotesIfNeeded/DupStrEx at P1.7f. This section's only survivors
// (RegSetStrICmpW and its Ex/RegSetStrCmpW/Ex siblings, and StrCmpLogicalExW itself) are
// unchanged below.

int RegSetStrICmpW(const wchar_t* s1, const wchar_t* s2)
{
    if (Configuration.SortDetectNumbers)
    {
        return StrCmpLogicalExW(s1, (int)wcslen(s1), s2, (int)wcslen(s2), NULL, TRUE, CurrentCollationOptions());
    }
    else
    {
        if (Configuration.SortUsesLocale)
        {
            return CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, s1, -1, s2, -1) - CSTR_EQUAL;
        }
        else
        {
            return WideICmpEx(s1, (int)wcslen(s1), s2, (int)wcslen(s2));
        }
    }
}

// l1/l2 accept -1 = wcslen, so a caller that converted a narrow
// string with ToWideArg(s, len) can pass the temporary without measuring it.
int RegSetStrICmpExW(const wchar_t* s1, int l1, const wchar_t* s2, int l2, BOOL* numericalyEqual)
{
    if (l1 < 0)
        l1 = s1 != NULL ? (int)wcslen(s1) : 0;
    if (l2 < 0)
        l2 = s2 != NULL ? (int)wcslen(s2) : 0;
    if (Configuration.SortDetectNumbers)
    {
        return StrCmpLogicalExW(s1, l1, s2, l2, numericalyEqual, TRUE, CurrentCollationOptions());
    }
    else
    {
        int ret;
        if (Configuration.SortUsesLocale)
        {
            ret = CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, s1, l1, s2, l2) - CSTR_EQUAL;
        }
        else
        {
            ret = WideICmpEx(s1, l1, s2, l2);
        }
        if (numericalyEqual != NULL)
            *numericalyEqual = ret == 0;
        return ret;
    }
}

int RegSetStrCmpW(const wchar_t* s1, const wchar_t* s2)
{
    if (Configuration.SortDetectNumbers)
    {
        return StrCmpLogicalExW(s1, (int)wcslen(s1), s2, (int)wcslen(s2), NULL, FALSE, CurrentCollationOptions());
    }
    else
    {
        if (Configuration.SortUsesLocale)
        {
            return CompareStringW(LOCALE_USER_DEFAULT, 0, s1, -1, s2, -1) - CSTR_EQUAL;
        }
        else
        {
            return WideCmpEx(s1, (int)wcslen(s1), s2, (int)wcslen(s2));
        }
    }
}

// l1/l2 accept -1 = wcslen, so a caller that converted a narrow
// string with ToWideArg(s, len) can pass the temporary without measuring it.
int RegSetStrCmpExW(const wchar_t* s1, int l1, const wchar_t* s2, int l2, BOOL* numericalyEqual)
{
    if (l1 < 0)
        l1 = s1 != NULL ? (int)wcslen(s1) : 0;
    if (l2 < 0)
        l2 = s2 != NULL ? (int)wcslen(s2) : 0;
    if (Configuration.SortDetectNumbers)
    {
        return StrCmpLogicalExW(s1, l1, s2, l2, numericalyEqual, FALSE, CurrentCollationOptions());
    }
    else
    {
        int ret;
        if (Configuration.SortUsesLocale)
        {
            ret = CompareStringW(LOCALE_USER_DEFAULT, 0, s1, l1, s2, l2) - CSTR_EQUAL;
        }
        else
        {
            ret = WideCmpEx(s1, l1, s2, l2);
        }
        if (numericalyEqual != NULL)
            *numericalyEqual = ret == 0;
        return ret;
    }
}


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
    return RegSetStrICmpExW(f1.Name, f1.NameLen, f2.Name, f2.NameLen, NULL);
}

int CmpNameExt(const CFileData& f1, const CFileData& f2)
{
    // The comparison below is WIDE, and that is the point.
    //
    // Comparing CP_ACP mirrors made two files with different non-representable names
    // compare EQUAL - both mirrors were the same run of '?'. The panel sort is a
    // quicksort, so "equal" left their order to whatever partitioning did: the listing
    // was nondeterministic and could differ between two reads of an unchanged directory.
    // CFileData::Name is now wchar_t* and NameLen counts WCHARs, so the
    // fallback below IS the wide comparison; the separate NameW branch that used to sit
    // here computed the identical result and is gone.
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
    int res = RegSetStrICmpExW(f1.Name, f1.NameLen, f2.Name, f2.NameLen, NULL);
    if (res != 0 || f1.Name == f2.Name)
        return res; // if addresses are the same, they must be equal
                    //--- equal names (archives or FS) - try if they differ at least in letter case
    return RegSetStrCmpExW(f1.Name, f1.NameLen, f2.Name, f2.NameLen, NULL);
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
    int res1 = RegSetStrICmpExW(f1.Ext, f1.NameLen - (int)(f1.Ext - f1.Name),
                               f2.Ext, f2.NameLen - (int)(f2.Ext - f2.Name),
                               &numericalyEqual1);
    if (!numericalyEqual1)
        return reverse ? res1 > 0 : res1 < 0; // extensions differ (are not equal nor numerically equal)
                                              //--- by Ext they are equal, Name decides
    BOOL numericalyEqual2;
    int res2 = RegSetStrICmpExW(f1.Name, (*f1.Ext != 0) ? (int)(f1.Ext - 1 - f1.Name) : f1.NameLen,
                               f2.Name, (*f2.Ext != 0) ? (int)(f2.Ext - 1 - f2.Name) : f2.NameLen,
                               &numericalyEqual2);
    if (numericalyEqual2 && res1 != 0)
        return reverse ? res1 > 0 : res1 < 0; // extensions are equal or numerically equal and names are only numerically equal (name comparison has priority)
    else
    {
        if (res2 == 0 && f1.Name != f2.Name) // equal names (archives or FS) - try if they differ at least in letter case
        {
            res1 = RegSetStrCmpExW(f1.Ext, f1.NameLen - (int)(f1.Ext - f1.Name),
                                  f2.Ext, f2.NameLen - (int)(f2.Ext - f2.Name),
                                  &numericalyEqual1);
            if (!numericalyEqual1)
                return reverse ? res1 > 0 : res1 < 0; // extensions differ (are not equal nor numerically equal)
            //--- by Ext they are again equal, Name decides
            res2 = RegSetStrCmpExW(f1.Name, (*f1.Ext != 0) ? (int)(f1.Ext - 1 - f1.Name) : f1.NameLen,
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
