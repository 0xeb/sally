// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "common/unicode/helpers.h"
#include <limits>
#include <stdexcept>

using sally::unicode::FoldCharW;

//
//*****************************************************************************
// Functions for wildcard operations
//

void PrepareMask(wchar_t* mask, const wchar_t* src)
{
    SLOW_CALL_STACK_MESSAGE2("PrepareMask(, %S)", src);
    wchar_t* begMask = mask;
    wchar_t lastChar = 0;
    // remove spaces at the beginning of the mask
    while (*src == L' ')
        src++;
    while (*src != 0)
    {
        if (*src == L'*' && lastChar == L'*')
            src++;                               // "**" -> "*"
        else if (*src == L'?' && lastChar == L'*') // "*?" -> "?*"
        {
            *(mask - 1) = L'?';
            *mask++ = L'*';
            src++;
        }
        else
            *mask++ = (lastChar = *src++);
    }
    // trim spaces at the end of the mask
    while (mask > begMask && *(mask - 1) == L' ')
        mask--;
    *mask = 0;
}


wchar_t* MaskName(wchar_t* buffer, int bufSize, const wchar_t* name, const wchar_t* mask)
{
    SLOW_CALL_STACK_MESSAGE1("MaskName()");
    if (buffer == NULL || bufSize <= 0 || name == NULL)
        return NULL;
    if (mask == NULL)
    {
        lstrcpynW(buffer, name, bufSize);
        return buffer;
    }

    // the first dot in the mask separates two operational parts: the first for the name,
    // the second for the extension (example: "a.b.c.d" + "*.*.old": "a.b.c" + "*" = "a.b.c";
    // "d" + "*.old" = "d.old" -> the result is the combination "a.b.c.d.old")

    int ignPoints = 0; // how many dots the name contains (this section corresponds to the text of the mask from the start to the first dot); the rest matches the extension (after the fisrt dot)
    const wchar_t* n = name;
    while (*n != 0)
        if (*n++ == '.')
            ignPoints++;
    const wchar_t* s = mask;
    while (*s != 0)
        if (*s++ == '.')
        {
            ignPoints--;
            break;
        } // in the mask, only one dot is meaningful (name.ext); the part after the second dot will be expanded at the end of the name
    //  while (*s != 0) if (*s++ == '.') {ignPoints--;}  // in this variant, sections in the name and mask between dots are matched from the end ("a.b.c.d" + "*.*.old": "a.b" + "*"; "c" + "*"; "d" + "old" -> "a.b.c.old")
    if (ignPoints < 0)
        ignPoints = 0;
    //  if (ignPoints == 0 && *name == '.') ignPoints++;   // dot at the start of the name should be ignored (not an extension); fix: ".cvspass" is an extension in Windows...

    n = name;
    wchar_t* d = buffer;
    wchar_t* endBuf = buffer + bufSize - 1;
    s = mask;
    while (*s != 0 && d < endBuf)
    {
        switch (*s)
        {
        case '*': // copy the rest of the name section (up to the "ignPoints+1"-th dot)
        {
            while (*n != 0)
            {
                if (*n == '.')
                {
                    if (ignPoints > 0)
                        ignPoints--; // this dot is part of the section, continue
                    else
                        break;
                }
                *d++ = *n++;
                if (d >= endBuf)
                    break;
            }
            break;
        }

        case '?': // copy one character unless it is the end of the name section (up to the "ignPoints+1"-th dot)
        {
            if (*n != 0)
            {
                if (*n == '.')
                {
                    if (ignPoints > 0)
                    {
                        ignPoints--; // this dot is part of the section, continue
                        *d++ = *n++;
                    }
                }
                else
                    *d++ = *n++;
            }
            break;
        }

        case '.': // end of the current name section (jump to the next section in the name)
        {
            *d++ = '.';
            while (*n != 0)
            {
                if (*n == '.')
                {
                    if (ignPoints > 0)
                        ignPoints--; // this dot is part of the section, continue
                    else
                        break;
                }
                n++;
            }
            if (*n == '.')
                n++;
            break;
        }

        default:
        {
            *d++ = *s; // regular character - just copy it
            if (*n != 0)
            {
                if (*n != '.')
                    n++; // if it isn't a '.', skip one character from the name
                else
                {
                    if (ignPoints > 0)
                    {
                        ignPoints--;
                        n++; // if the dot is part of the section, skip it as well
                    }
                }
            }
            break;
        }
        }
        s++;
    }
    while (--d >= buffer && *d == '.')
        ; // the result must be trimmed of trailing '.'
    *++d = 0;
    return buffer;
}

std::wstring MaskNameOwnedW(const wchar_t* name, const wchar_t* mask)
{
    if (name == NULL)
        return {};
    if (mask == NULL)
        return name;

    // MaskName emits no more than every input-name character plus every mask
    // character. Give the legacy algorithm that exact dynamic upper bound.
    const size_t nameLength = wcslen(name);
    const size_t maskLength = wcslen(mask);
    const size_t maxCapacity = static_cast<size_t>((std::numeric_limits<int>::max)());
    if (nameLength >= maxCapacity || maskLength >= maxCapacity - nameLength)
        throw std::length_error("masked name exceeds the supported string size");
    const size_t capacity = nameLength + maskLength + 1;
    std::wstring result(capacity, L'\0');
    MaskName(result.data(), static_cast<int>(result.size()), name, mask);
    result.resize(wcslen(result.c_str()));
    return result;
}

//
//*****************************************************************************
// Functions for quick-search
// the '/' character represents any number of characters (like '*' in a standard mask)

// returns TRUE if it is a wildcard character replacing '*'
// it must be a character not allowed in file names
// and at the same time, it should be easy to type (see the '<' on the German keyboard;
// for the backslash on the German keyboard you must press AltGr+\)
BOOL IsQSWildChar(wchar_t ch)
{
    return (ch == '/' || ch == '\\' || ch == '<');
}

std::wstring PrepareQSMask(const wchar_t* src)
{
    CALL_STACK_MESSAGE2("PrepareQSMask(%S)", src);
    std::wstring mask;
    mask.reserve(wcslen(src));
    wchar_t lastChar = 0;
    while (*src != 0)
    {
        if (IsQSWildChar(*src))
        {
            if (lastChar == '/')
                src++;
            else
            {
                // convert other wild characters to '/'
                src++;
                mask.push_back(lastChar = L'/');
            }
        }
        else
        {
            lastChar = *src++;
            mask.push_back(lastChar);
        }
    }
    // trim '/' at the end of the mask (it has no meaning here)
    if (!mask.empty() && mask.back() == L'/')
        mask.pop_back();
    return mask;
}

// masks.h declares AgreeQSMask wide (quick-search is a
// panel-name feature, not file content), but the definition stayed narrow, folding through the
// CP_ACP LowerCase[] table -- the same link-time-masked defect P1.6i already fixed for
// PrepareMask/AgreeMask above, just never applied here. Folds with FoldCharW per character, the
// same choice AgreeMask already made and for the same reason (a character-by-character matcher,
// not a whole-string compare).
BOOL AgreeQSMaskAux(const wchar_t* filename, BOOL hasExtension, const wchar_t* filenameBase, const wchar_t* mask, BOOL wholeString, int& offset)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE6("AgreeQSMaskAux(%S, %d, %S, %S, %d,)", filename, hasExtension, filenameBase, mask, wholeString);
    while (*filename != 0)
    {
        if (!wholeString && *mask == 0)
        {
            offset = (int)(filename - filenameBase);
            return TRUE; // end of mask, 'offset' = how far it reaches into the file name
        }
        if (FoldCharW(*filename) == FoldCharW(*mask))
        {
            filename++;
            mask++;
        }
        else if (*mask == L'/') // '/' stands for a sequence of characters (can be empty)
        {
            mask++;
            while (*filename != 0)
            {
                if (AgreeQSMaskAux(filename, hasExtension, filenameBase, mask, wholeString, offset))
                    return TRUE; // the rest of the mask matches
                filename++;
            }
            break; // end of filename...
        }
        else
            return FALSE;
    }
    if (*mask == 0 ||
        !hasExtension && *mask == L'.' && *(mask + 1) == 0) // a dot at the end of the mask is tolerated for names without an extension ('/' at the end is trimmed, not handled)
    {
        offset = (int)(filename - filenameBase);
        return TRUE; // mask matched the entire name -> 'offset' = length of the file name
    }
    else
        return FALSE;
}

BOOL AgreeQSMask(const wchar_t* filename, BOOL hasExtension, const wchar_t* mask, BOOL wholeString, int& offset)
{
    SLOW_CALL_STACK_MESSAGE5("AgreeQSMask(%S, %d, %S, %d,)", filename, hasExtension, mask, wholeString);
    offset = 0;
    return AgreeQSMaskAux(filename, hasExtension, filename, mask, wholeString, offset);
}

//*****************************************************************************
//
// CMaskGroup
//

CMaskGroup::CMaskGroup()
    : PreparedMasks(10, 10)
{
    MasksString.clear();
    NeedPrepare = FALSE;
    ExtendedMode = FALSE;
    MasksHashArray = NULL;
    MasksHashArraySize = 0;
}

CMaskGroup::CMaskGroup(const wchar_t* masks, BOOL extendedMode)
    : PreparedMasks(10, 10)
{
    MasksHashArray = NULL;
    MasksHashArraySize = 0;
    SetMasksString(masks, extendedMode);
}

CMaskGroup::~CMaskGroup()
{
    Release();
}

void CMaskGroup::Release()
{
    int i;
    for (i = 0; i < PreparedMasks.Count; i++)
    {
        if (PreparedMasks[i] != NULL)
        {
            free(PreparedMasks[i]);
            PreparedMasks[i] = NULL;
        }
    }
    PreparedMasks.DestroyMembers();
    ReleaseMasksHashArray();
}

CMaskGroup&
CMaskGroup::operator=(const CMaskGroup& s)
{
    Release();

    // Still re-PREPARES rather than copying prepared state. A
    // memcpy here would share heap pointers between two groups and double-free.
    MasksString = s.MasksString;
    ExtendedMode = s.ExtendedMode;

    NeedPrepare = TRUE;
    if (!s.NeedPrepare)
    {
        int errpos = 0;
        if (!PrepareMasks(errpos)) // an error shouldn't occur, the source mask is valid
            TRACE_E("CMaskGroup::operator= Internal error, PrepareMasks() failed.");
    }
    return *this;
}

void CMaskGroup::ReleaseMasksHashArray()
{
    if (MasksHashArray != NULL)
    {
        int i;
        for (i = 0; i < MasksHashArraySize; i++)
        {
            if (MasksHashArray[i].Mask != NULL) // if this array element is not empty
            {
                free(MasksHashArray[i].Mask);
                CMasksHashEntry* next = MasksHashArray[i].Next;
                while (next != NULL) // if there are multiple masks at the same entry (hash)
                {
                    CMasksHashEntry* nextNext = next->Next;
                    if (next->Mask != NULL)
                        free(next->Mask);
                    free(next);
                    next = nextNext;
                }
            }
        }
        free(MasksHashArray); // so destructors of objects in the array aren't called
        MasksHashArray = NULL;
        MasksHashArraySize = 0;
    }
}

void CMaskGroup::SetMasksString(const wchar_t* masks, BOOL extendedMode)
{
    std::wstring newMasks = masks != NULL ? masks : L"";
    MasksString = std::move(newMasks);

    NeedPrepare = TRUE;
    ExtendedMode = extendedMode;
}

// The lossy CP_ACP rendering is gone. masks.h has declared this
// returning const wchar_t*, so no caller could ever have received
// the narrow buffer -- the definition differed only by return type (C2556) and had no
// matching declaration. Returning the storage directly also retires MasksStringNarrow.
const wchar_t*
CMaskGroup::GetMasksString()
{
    return MasksString.c_str();
}

BOOL CMaskGroup::GetExtendedMode()
{
    return ExtendedMode;
}

// Wide hash. MUST produce the same bucket for the same extension
// as the linear scan expects, or the hash array and the scan disagree and
// matches vanish silently. Folds with FoldCharW for the same reason AgreeMask
// does - the narrow form used LowerCase[], the CP_ACP table.
#define COMPUTEMASKGROUPHASH(hash, exten)     {         const wchar_t* __CMGH_ext = (exten);         if (*__CMGH_ext != 0)         {             hash = (DWORD)(sally::unicode::FoldCharW(*__CMGH_ext) - L'a');             if (*++__CMGH_ext != 0)             {                 hash += 3 * (DWORD)(sally::unicode::FoldCharW(*__CMGH_ext) - L'a');                 if (*++__CMGH_ext != 0)                     hash += 7 * (DWORD)(sally::unicode::FoldCharW(*__CMGH_ext) - L'a');             }         }         hash = hash % MasksHashArraySize;     }

BOOL CMaskGroup::PrepareMasks(int& errorPos, const wchar_t* masksString)
{
    CALL_STACK_MESSAGE1("CMaskGroup::PrepareMasks(,)");
    if (masksString == NULL && !NeedPrepare)
        return TRUE;

    int i;
    for (i = 0; i < PreparedMasks.Count; i++)
        if (PreparedMasks[i] != NULL)
            free(PreparedMasks[i]);
    PreparedMasks.DestroyMembers();
    ReleaseMasksHashArray();

    const wchar_t* useMasksString = masksString == NULL ? MasksString.c_str() : masksString;
    const wchar_t* s = useMasksString;
    const size_t scratchSize = std::wcslen(s) + 1;
    std::wstring bufStore(scratchSize, L'\0');
    std::wstring maskStore(scratchSize, L'\0');
    wchar_t* buf = &bufStore[0];
    wchar_t* maskBuf = &maskStore[0];
    int excludePos = -1;   // if not -1, all following masks are exclude type
                           // and will be inserted at the beginning of the array
    int hashableMasks = 0; // number of masks that can be hashed (MASK_OPTIMIZE_EXTENSION + CMaskItemFlags::Exclude==0)

    // to avoid unnecessary reallocations for longer arrays, set a reasonable delta
    int masksLen = (int)wcslen(s);
    PreparedMasks.SetDelta(max(10, (masksLen / 6) / 2)); // "*.xxx;" is 6 characters, use half the extensions

    while (1)
    {
        wchar_t* mask = maskBuf;
        while (*s != 0 && *s > 31 && *s != '\\' && *s != L'/' &&
               *s != L'<' && *s != L'>' && *s != L':' && *s != L'"')
        {
            if (*s == L'|')
                break;
            if (*s == L';')
            {
                if (*(s + 1) == L';')
                    s++;
                else
                    break;
            }
            *mask++ = *s++;
        }
        *mask = 0;
        if (*s != 0 && *s != L';' && (*s != L'|' || excludePos != -1)) // the exclude character L'|' may appear only once in the mask
        {
            errorPos = (int)(s - useMasksString);
            return FALSE;
        }

        while (--mask >= maskBuf && *mask <= L' ')
            ;
        *(mask + 1) = 0;
        mask = maskBuf;
        while (*mask != 0 && *mask <= L' ')
            mask++;

        if (*mask != 0)
        {
            PrepareMask(buf, mask); // call PrepareMask for extendedMode as well
            if (buf[0] != 0)
            {
                int l = (int)wcslen(buf) + 1;
                wchar_t* newMask = (wchar_t*)malloc((1 + l) * sizeof(wchar_t));
                if (newMask != NULL)
                {
                    unsigned optimize = MASK_OPTIMIZE_NONE;
                    // determine whether one of the optimizations can be used
                    if (wcscmp(buf, L"*") == 0 || wcscmp(buf, L"*.*") == 0)
                        optimize = MASK_OPTIMIZE_ALL; // *.* or *
                    else
                    {
                        if (l > 3 && buf[0] == L'*' && buf[1] == L'.') // *.xxxx
                        {
                            const wchar_t* iter = buf + 2;
                            if (ExtendedMode)
                            {
                                while (*iter != 0 && *iter != L'*' && *iter != L'?' && *iter != L'#' && *iter != L'.')
                                    iter++;
                            }
                            else
                            {
                                while (*iter != 0 && *iter != L'*' && *iter != L'?' && *iter != L'.')
                                    iter++;
                            }
                            if (*iter == 0)
                            {
                                optimize = MASK_OPTIMIZE_EXTENSION;
                                if (excludePos == -1)
                                    hashableMasks++;
                            }
                        }
                    }
                    SetMaskItemFlags(newMask, optimize, excludePos != -1 ? 1 : 0);

                    memmove(newMask + 1, buf, l * sizeof(wchar_t));
                    if (excludePos != -1)
                        PreparedMasks.Insert(0, newMask); // insert exclude masks at the beginning
                    else
                        PreparedMasks.Add(newMask); // append include masks at the end
                    if (!PreparedMasks.IsGood())
                    {
                        free(newMask);
                        PreparedMasks.ResetState();
                        errorPos = 0;
                        return FALSE;
                    }
                }
                else
                {
                    TRACE_E(LOW_MEMORY);
                    errorPos = 0;
                    return FALSE;
                }
            }
        }

        if (*s == 0)
        {
            if (excludePos != -1 && (PreparedMasks.Count == 0 ||
                                     MaskItemExclude(PreparedMasks[0]) == 0))
            {
                // if the L'|' character is not followed by another mask, the syntax is invalid
                errorPos = excludePos;
                return FALSE;
            }
            break;
        }
        if (*s == L'|')
        {
            if (PreparedMasks.Count == 0)
            {
                // the user specified a sequence starting with L'|', we must append an implicit * at the end
                wchar_t* newMask = (wchar_t*)malloc((1 + 2) * sizeof(wchar_t));
                if (newMask != NULL)
                {
                    SetMaskItemFlags(newMask, MASK_OPTIMIZE_ALL, 0);
                    newMask[1] = L'*';
                    newMask[2] = 0;
                    PreparedMasks.Add(newMask);
                    if (!PreparedMasks.IsGood())
                    {
                        free(newMask);
                        PreparedMasks.ResetState();
                        errorPos = 0;
                        return FALSE;
                    }
                }
                else
                {
                    TRACE_E(LOW_MEMORY);
                    errorPos = 0;
                    return FALSE;
                }
            }
            excludePos = (int)(s - useMasksString); // the next mask will be of the exclude type
        }
        s++;
    }

    if (hashableMasks >= 10) // to be worthwhile there should be at least 10
    {
        MasksHashArraySize = 2 * hashableMasks;
        MasksHashArray = (CMasksHashEntry*)malloc(MasksHashArraySize * sizeof(CMasksHashEntry));
        if (MasksHashArray != NULL)
        {
            memset(MasksHashArray, 0, MasksHashArraySize * sizeof(CMasksHashEntry));
            int i2;
            for (i2 = PreparedMasks.Count - 1; i2 >= 0; i2--)
            {
                wchar_t* mask = PreparedMasks[i2];
                if (MaskItemOptimize(mask) == MASK_OPTIMIZE_EXTENSION &&
                    MaskItemExclude(mask) == 0)
                { // this mask can be hashed; add it to the hash array
                    DWORD hash = 0;
                    COMPUTEMASKGROUPHASH(hash, mask + 3);
                    if (MasksHashArray[hash].Mask == NULL)
                    {
                        MasksHashArray[hash].Mask = mask;
                        PreparedMasks.Detach(i2);
                        if (!PreparedMasks.IsGood())
                            PreparedMasks.ResetState(); // Detach always succeeds (at most the array won't shift, which is fine)
                    }
                    else
                    {
                        CMasksHashEntry* next = &MasksHashArray[hash];
                        while (next->Next != NULL)
                        {
                            next = next->Next;
                        }
                        next->Next = (CMasksHashEntry*)malloc(sizeof(CMasksHashEntry));
                        if (next->Next != NULL)
                        {
                            next->Next->Mask = mask;
                            next->Next->Next = NULL;
                            PreparedMasks.Detach(i2);
                            if (!PreparedMasks.IsGood())
                                PreparedMasks.ResetState(); // Detach always succeeds (at most the array won't shift, which is fine)
                        }
                        else // out of memory -> nothing happens, we skip this one mask
                            TRACE_E(LOW_MEMORY);
                    }
                }
            }
            /*
#ifdef _DEBUG
      int maxDepth = 0;
      int squareOfDepths = 0;
      int usedIndexes = 0;
      for (i = 0; i < MasksHashArraySize; i++)
      {
        CMasksHashEntry *next = &MasksHashArray[i];
        if (next->Mask != NULL)
        {
          int depth = 1;
          while (next->Next != NULL)
          {
            next = next->Next;
            depth++;
          }
          if (depth > maxDepth) maxDepth = depth;
          if (depth > 1) squareOfDepths += depth * depth;
          usedIndexes++;
        }
      }
      TRACE_I("CMaskGroup::PrepareMasks(): maxHashDepth=" << maxDepth << ", count=" <<
              hashableMasks << ", squareOfDepths=" << squareOfDepths << ", usedIndexes=" << usedIndexes);
#endif // _DEBUG
*/
        }
        else // out of memory -> we simply won't accelerate searching in masks
        {
            TRACE_E(LOW_MEMORY);
            MasksHashArraySize = 0;
        }
    }
    NeedPrepare = FALSE;
    return TRUE;
}

BOOL CMaskGroup::AgreeMasks(const wchar_t* fileName, const wchar_t* fileExt)
{
    if (NeedPrepare)
        TRACE_E("CMaskGroup::AgreeMasks: PrepareMasks must be called before AgreeMasks!");

    SLOW_CALL_STACK_MESSAGE3("CMaskGroup::AgreeMasks(%S, %S)", fileName, fileExt);
    if (fileExt == NULL)
    {
        int tmpLen = (int)wcslen(fileName);
        fileExt = fileName + tmpLen;
        while (--fileExt >= fileName && *fileExt != L'.')
            ;
        if (fileExt < fileName)
            fileExt = fileName + tmpLen; // ".cvspass" in Windows is an extension ...
        else
            fileExt++;
    }
    const wchar_t* ext = fileExt;
    if (*ext == 0 && *fileName == L'.' && *(ext - 1) != L'.') // may be the ".cvspass" case; ".." has no extension
    {
        TRACE_E("CMaskGroup::AgreeMasks: fileName starts with a dot but fileExt points to the end of the name");
        ext = fileName + 1;
    }
    int i;
    for (i = 0; i < PreparedMasks.Count; i++)
    {
        wchar_t* mask = PreparedMasks[i];
        if (mask != NULL)
        {
            if (MaskItemExclude(mask) == 1)
            {
                if (MaskItemOptimize(mask) == MASK_OPTIMIZE_ALL) // *.*; *
                    return FALSE;
                if (MaskItemOptimize(mask) == MASK_OPTIMIZE_EXTENSION) // *.xxxx
                {
                    if (StrICmpW(ext, mask + 3) == 0)
                        return FALSE;
                    else
                        continue;
                }
                mask++;
                if (AgreeMask(fileName, mask, *fileExt != 0, ExtendedMode))
                    return FALSE;
            }
            else
            {
                if (MaskItemOptimize(mask) == MASK_OPTIMIZE_ALL) // *.*; *
                    return TRUE;
                if (MaskItemOptimize(mask) == MASK_OPTIMIZE_EXTENSION) // *.xxxx
                {
                    if (StrICmpW(ext, mask + 3) == 0)
                        return TRUE;
                    else
                        continue;
                }
                mask++;
                if (AgreeMask(fileName, mask, *fileExt != 0, ExtendedMode))
                    return TRUE;
            }
        }
    }
    if (MasksHashArray != NULL) // there are still some masks in the hash array
    {
        DWORD hash = 0;
        COMPUTEMASKGROUPHASH(hash, ext);
        CMasksHashEntry* item = &MasksHashArray[hash];
        if (item->Mask != NULL)
        {
            do
            {
                if (StrICmpW(item->Mask + 3, ext) == 0)
                    return TRUE;
                item = item->Next;
            } while (item != NULL);
        }
    }
    return FALSE;
}

// ****************************************************************************
// Wide mask matching.
//
// The narrow AgreeMask above folds with `LowerCase[]`, a 256-entry table built
// from CharLower under the active code page. Two characters that differ only
// outside CP_ACP therefore fold to the same entry, so a mask matches files it
// should not - and, more visibly, a mask containing non-ANSI characters matches
// nothing at all once the filename has been narrowed to '?'.
//
// FOLDING CHOICE. This is a character-by-character matcher, so
// sally::text::CompareFolded (which folds whole strings) does not fit, and
// towlower() is wrong: under MSVC's default C locale it folds ASCII only, which
// would silently reproduce the very limitation being removed. CharLowerW's
// single-character form is the exact wide analogue of how LowerCase[] itself was
// built, so the semantics carry over rather than being re-invented (the FoldCharW `using` itself
// is declared once, at the top of this file, since AgreeQSMaskAux above needs it too).
BOOL AgreeMask(const wchar_t* filename, const wchar_t* mask, BOOL hasExtension, BOOL extendedMode)
{
    CALL_STACK_MESSAGE_NONE;
    while (*filename != 0)
    {
        if (*mask == 0)
            return FALSE; // mask is too short
        BOOL agree;
        if (extendedMode)
            agree = (FoldCharW(*filename) == FoldCharW(*mask) || *mask == L'?' ||
                     (*mask == L'#' && *filename >= L'0' && *filename <= L'9'));
        else
            agree = (FoldCharW(*filename) == FoldCharW(*mask) || *mask == L'?');
        if (agree)
        {
            filename++;
            mask++;
        }
        else if (*mask == L'*') // '*' represents a sequence of characters (possibly empty)
        {
            mask++;
            while (*filename != 0)
            {
                if (AgreeMask(filename, mask, hasExtension, extendedMode))
                    return TRUE; // the rest of the mask matches
                filename++;
            }
            break; // end of filename...
        }
        else
            return FALSE;
    }
    if (*mask == L'*')
        mask++;                         // asterisk '*' afterwards -> represents "" -> everything is ok
    if (!hasExtension && *mask == L'.') // without extension mask "*.*" must still match...
        return *(mask + 1) == 0 || (*(mask + 1) == L'*' && *(mask + 2) == 0);
    else
        return *mask == 0;
}
