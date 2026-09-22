// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "common/text/ContentSearcher.h"
#include "common/text/EncodingDetector.h"
#include "common/text/LegacySearchTextEncoding.h"
#include "common/text/Utf16RegexBridge.h"

#include "cfgdlg.h"
#include "find.h"
#include "md5.h"
#include "common/IRegistry.h"
#include "common/unicode/helpers.h"
#include "common/unicode/PanelPathPolicy.h"
#include "common/widepath.h"
#include "common/fsutil.h" // SkipRootW

// find.h has declared these wchar_t*[] for some time; the definitions were
// still char*[]. Every other TU indexing them through the header saw wide pointers over
// narrow storage - class 6, and only visible here because both forms meet in this file.
wchar_t* FindNamedHistory[FIND_NAMED_HISTORY_SIZE];
wchar_t* FindLookInHistory[FIND_LOOKIN_HISTORY_SIZE];
wchar_t* FindGrepHistory[FIND_GREP_HISTORY_SIZE];

CFindOptions FindOptions;
CFindIgnore FindIgnore;
CFindDialogQueue FindDialogQueue(L"Find Dialogs");

HANDLE FindDialogContinue = NULL;

HACCEL FindDialogAccelTable = NULL;

const wchar_t* FINDOPTIONSITEM_ITEMNAME_REG = L"ItemName";
const wchar_t* FINDOPTIONSITEM_SUBDIRS_REG = L"SubDirectories";
const wchar_t* FINDOPTIONSITEM_WHOLEWORDS_REG = L"WholeWords";
const wchar_t* FINDOPTIONSITEM_CASESENSITIVE_REG = L"CaseSensitive";
const wchar_t* FINDOPTIONSITEM_HEXMODE_REG = L"HexMode";
const wchar_t* FINDOPTIONSITEM_REGULAR_REG = L"RegularExpresions";
const wchar_t* FINDOPTIONSITEM_FILETYPEMODE_REG = L"FileTypeMode";
const wchar_t* FINDOPTIONSITEM_AUTOLOAD_REG = L"AutoLoad";
const wchar_t* FINDOPTIONSITEM_NAMED_REG = L"Named";
// Read-only aliases written by interim Unicode builds. Save() writes the canonical values only.
const wchar_t* FINDOPTIONSITEM_NAMEDW_REG = L"NamedTextW";
const wchar_t* FINDOPTIONSITEM_LOOKIN_REG = L"LookIn";
const wchar_t* FINDOPTIONSITEM_LOOKINW_REG = L"LookInW";
const wchar_t* FINDOPTIONSITEM_GREPW_REG = L"GrepTextW";
const wchar_t* FINDOPTIONSITEM_GREP_REG = L"Grep";

// wide: a registry VALUE NAME addresses the same value through either form,
// so this is a rename of the C++ constant only - no migration.
const wchar_t* FINDIGNOREITEM_PATH_REG = L"Path";
const wchar_t* FINDIGNOREITEM_ENABLED_REG = L"Enabled";

// following variable was used up to Altap Salamander 2.5,
// where we switched to CFilterCriteria with its Save/Load
const wchar_t* OLD_FINDOPTIONSITEM_EXCLUDEMASK_REG = L"ExcludeMask";

//*********************************************************************************
//
// InitializeFind, ReleaseFind
//

BOOL InitializeFind()
{
    int i;
    for (i = 0; i < FIND_NAMED_HISTORY_SIZE; i++)
        FindNamedHistory[i] = NULL;
    for (i = 0; i < FIND_LOOKIN_HISTORY_SIZE; i++)
        FindLookInHistory[i] = NULL;
    for (i = 0; i < FIND_GREP_HISTORY_SIZE; i++)
        FindGrepHistory[i] = NULL;

    FindDialogContinue = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL));
    if (FindDialogContinue == NULL)
    {
        TRACE_E("Unable to create FindDialogContinue event.");
        return FALSE;
    }

    FindDialogAccelTable = HANDLES(LoadAccelerators(HInstance, MAKEINTRESOURCE(IDA_FINDDIALOGACCELS)));
    if (FindDialogAccelTable == NULL)
    {
        TRACE_E("Unable to load accelerators for Find dialog.");
        return FALSE;
    }
    return TRUE;
}

void ClearFindHistory(BOOL dataOnly)
{
    int i;
    for (i = 0; i < FIND_NAMED_HISTORY_SIZE; i++)
    {
        if (FindNamedHistory[i] != NULL)
        {
            free(FindNamedHistory[i]);
            FindNamedHistory[i] = NULL;
        }
    }

    for (i = 0; i < FIND_LOOKIN_HISTORY_SIZE; i++)
    {
        if (FindLookInHistory[i] != NULL)
        {
            free(FindLookInHistory[i]);
            FindLookInHistory[i] = NULL;
        }
    }

    for (i = 0; i < FIND_GREP_HISTORY_SIZE; i++)
    {
        if (FindGrepHistory[i] != NULL)
        {
            free(FindGrepHistory[i]);
            FindGrepHistory[i] = NULL;
        }
    }

    // we should also clear the comboboxes of open windows
    if (!dataOnly)
    {
        FindDialogQueue.BroadcastMessage(WM_USER_CLEARHISTORY, 0, 0);
    }
}

void ReleaseFind()
{
    ClearFindHistory(TRUE); // we only release data
    if (FindDialogContinue != NULL)
        HANDLES(CloseHandle(FindDialogContinue));
}

//*********************************************************************************
//
// CFindOptionsItem
//

CFindOptionsItem::CFindOptionsItem()
{
    // Internal
    ItemName.clear();

    // Find dialog
    SubDirectories = TRUE;
    WholeWords = FALSE;
    CaseSensitive = FALSE;
    HexMode = FALSE;
    RegularExpresions = FALSE;
    FileTypeMode = fftmAll;

    AutoLoad = FALSE;

    NamedText.clear();
    LookInText.clear();
    GrepText.clear();
}

CFindOptionsItem&
CFindOptionsItem::operator=(const CFindOptionsItem& s)
{
    // Internal
    ItemName = s.ItemName;

    memmove(&Criteria, &s.Criteria, sizeof(Criteria));

    // Find dialog
    SubDirectories = s.SubDirectories;
    WholeWords = s.WholeWords;
    CaseSensitive = s.CaseSensitive;
    HexMode = s.HexMode;
    RegularExpresions = s.RegularExpresions;
    FileTypeMode = s.FileTypeMode;

    AutoLoad = s.AutoLoad;

    NamedText = s.NamedText;
    LookInText = s.LookInText;
    GrepText = s.GrepText;

    return *this;
}

void CFindOptionsItem::BuildItemName()
{
    ItemName = FormatStrW(L"\"%s\" %s \"%s\"", NamedText.c_str(),
                          LoadStrW(IDS_FF_IN), LookInText.c_str());
}

BOOL CFindOptionsItem::Save(HKEY hKey)
{
    // optimize registry size by storing only non-default values;
    // before saving, we need to clear the key we’re going to save into
    CFindOptionsItem def;

    if (ItemName != def.ItemName)
        gRegistry->SetString(hKey, FINDOPTIONSITEM_ITEMNAME_REG, ItemName.c_str());
    if (SubDirectories != def.SubDirectories)
        SetValue(hKey, FINDOPTIONSITEM_SUBDIRS_REG, REG_DWORD, &SubDirectories, sizeof(DWORD));
    if (WholeWords != def.WholeWords)
        SetValue(hKey, FINDOPTIONSITEM_WHOLEWORDS_REG, REG_DWORD, &WholeWords, sizeof(DWORD));
    if (CaseSensitive != def.CaseSensitive)
        SetValue(hKey, FINDOPTIONSITEM_CASESENSITIVE_REG, REG_DWORD, &CaseSensitive, sizeof(DWORD));
    if (HexMode != def.HexMode)
        SetValue(hKey, FINDOPTIONSITEM_HEXMODE_REG, REG_DWORD, &HexMode, sizeof(DWORD));
    if (RegularExpresions != def.RegularExpresions)
        SetValue(hKey, FINDOPTIONSITEM_REGULAR_REG, REG_DWORD, &RegularExpresions, sizeof(DWORD));
    if (FileTypeMode != def.FileTypeMode)
        SetValue(hKey, FINDOPTIONSITEM_FILETYPEMODE_REG, REG_DWORD, &FileTypeMode, sizeof(DWORD));
    if (AutoLoad != def.AutoLoad)
        SetValue(hKey, FINDOPTIONSITEM_AUTOLOAD_REG, REG_DWORD, &AutoLoad, sizeof(DWORD));
    if (NamedText != def.NamedText)
        gRegistry->SetString(hKey, FINDOPTIONSITEM_NAMED_REG, NamedText.c_str());
    if (LookInText != def.LookInText)
        gRegistry->SetString(hKey, FINDOPTIONSITEM_LOOKIN_REG, LookInText.c_str());
    if (GrepText != def.GrepText)
        SetValueW(hKey, FINDOPTIONSITEM_GREP_REG, REG_SZ, GrepText.c_str(), -1);

    // advanced options
    Criteria.Save(hKey);
    return TRUE;
}

BOOL CFindOptionsItem::Load(HKEY hKey, DWORD cfgVersion)
{
    GetStringValueW(hKey, FINDOPTIONSITEM_ITEMNAME_REG, ItemName);
    GetValue(hKey, FINDOPTIONSITEM_SUBDIRS_REG, REG_DWORD, &SubDirectories, sizeof(DWORD));
    GetValue(hKey, FINDOPTIONSITEM_WHOLEWORDS_REG, REG_DWORD, &WholeWords, sizeof(DWORD));
    GetValue(hKey, FINDOPTIONSITEM_CASESENSITIVE_REG, REG_DWORD, &CaseSensitive, sizeof(DWORD));
    GetValue(hKey, FINDOPTIONSITEM_HEXMODE_REG, REG_DWORD, &HexMode, sizeof(DWORD));
    GetValue(hKey, FINDOPTIONSITEM_REGULAR_REG, REG_DWORD, &RegularExpresions, sizeof(DWORD));
    GetValue(hKey, FINDOPTIONSITEM_FILETYPEMODE_REG, REG_DWORD, &FileTypeMode, sizeof(DWORD));
    if (FileTypeMode < fftmAll || FileTypeMode > fftmFolders)
        FileTypeMode = fftmAll;
    GetValue(hKey, FINDOPTIONSITEM_AUTOLOAD_REG, REG_DWORD, &AutoLoad, sizeof(DWORD));
    GetStringValueW(hKey, FINDOPTIONSITEM_NAMED_REG, NamedText);
    GetStringValueW(hKey, FINDOPTIONSITEM_LOOKIN_REG, LookInText);
    GetStringValueW(hKey, FINDOPTIONSITEM_GREP_REG, GrepText);
    // Import the temporary sidecar values emitted during the Unicode migration. They win when
    // present because an older canonical value may contain an ACP-damaged projection.
    std::wstring migratedValue;
    if (gRegistry->GetString(hKey, FINDOPTIONSITEM_NAMEDW_REG, migratedValue).success &&
        !migratedValue.empty())
        NamedText = migratedValue;
    migratedValue.clear();
    if (gRegistry->GetString(hKey, FINDOPTIONSITEM_LOOKINW_REG, migratedValue).success &&
        !migratedValue.empty())
        LookInText = migratedValue;
    migratedValue.clear();
    if (gRegistry->GetString(hKey, FINDOPTIONSITEM_GREPW_REG, migratedValue).success &&
        !migratedValue.empty())
        GrepText = migratedValue;

    if (cfgVersion <= 13)
    {
        // conversion of old values

        // exclude mask
        BOOL excludeMask = FALSE;
        GetValue(hKey, OLD_FINDOPTIONSITEM_EXCLUDEMASK_REG, REG_DWORD, &excludeMask, sizeof(DWORD));
        if (excludeMask)
        {
            NamedText.insert(NamedText.begin(), L'|');
        }

        Criteria.LoadOld(hKey);
    }
    else
        Criteria.Load(hKey);

    return TRUE;
}

//*********************************************************************************
//
// CFindOptions
//

CFindOptions::CFindOptions()
    : Items(20, 10)
{
}

BOOL CFindOptions::Save(HKEY hKey)
{
    ClearKey(hKey);

    HKEY subKey;
    wchar_t buf[30];
    int i;
    for (i = 0; i < Items.Count; i++)
    {
        _itow_s(i + 1, buf, _countof(buf), 10);
        if (CreateKey(hKey, buf, subKey))
        {
            Items[i]->Save(subKey);
            CloseKey(subKey);
        }
        else
            break;
    }
    return TRUE;
}

BOOL CFindOptions::Load(HKEY hKey, DWORD cfgVersion)
{
    HKEY subKey;
    wchar_t buf[30];
    int i = 1;
    wcscpy_s(buf, L"1");
    Items.DestroyMembers();
    while (OpenKey(hKey, buf, subKey))
    {
        CFindOptionsItem* item = new CFindOptionsItem();
        if (item == NULL)
        {
            TRACE_E(LOW_MEMORY);
            break;
        }
        item->Load(subKey, cfgVersion);
        Items.Add(item);
        if (!Items.IsGood())
        {
            Items.ResetState();
            delete item;
            break;
        }
        _itow_s(++i, buf, _countof(buf), 10);
        CloseKey(subKey);
    }

    return TRUE;
}

BOOL CFindOptions::Load(CFindOptions& source)
{
    CFindOptionsItem* item;
    Items.DestroyMembers();
    int i;
    for (i = 0; i < source.Items.Count; i++)
    {
        item = new CFindOptionsItem();
        if (item == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
        *item = *source.Items[i];
        Items.Add(item);
        if (!Items.IsGood())
        {
            delete item;
            Items.ResetState();
            return FALSE;
        }
    }
    return TRUE;
}

BOOL CFindOptions::Add(CFindOptionsItem* item)
{
    Items.Add(item);
    if (!Items.IsGood())
    {
        Items.ResetState();
        return FALSE;
    }
    return TRUE;
}

//*********************************************************************************
//
// CFindIgnoreItem
//

CFindIgnoreItem::CFindIgnoreItem()
{
    Enabled = TRUE;
    Type = fiitUnknow;
}

CFindIgnoreItem::~CFindIgnoreItem()
{
}

//*********************************************************************************
//
// CFindIgnore
//

CFindIgnore::CFindIgnore()
    : Items(5, 5)
{
    Reset();
}

void CFindIgnore::Reset()
{
    Items.DestroyMembers();

    Add(TRUE, L"\\System Volume Information");
    Add(FALSE, L"Local Settings\\Temporary Internet Files");
}

BOOL CFindIgnore::Save(HKEY hKey)
{
    ClearKey(hKey);

    HKEY subKey;
    wchar_t buf[30];
    int i;
    for (i = 0; i < Items.Count; i++)
    {
        _itow_s(i + 1, buf, _countof(buf), 10);
        if (CreateKey(hKey, buf, subKey))
        {
            // SetValueW ignores dataSize for REG_SZ; the narrow SetValue's -1 meant strlen(),
            // which on a wide string measures one character.
            SetValueW(subKey, FINDIGNOREITEM_PATH_REG, REG_SZ, Items[i]->Path.c_str(), -1);
            if (!Items[i]->Enabled) // save only if it is FALSE
                SetValue(subKey, FINDIGNOREITEM_ENABLED_REG, REG_DWORD, &Items[i]->Enabled, sizeof(DWORD));
            CloseKey(subKey);
        }
        else
            break;
    }
    return TRUE;
}

BOOL CFindIgnore::Load(HKEY hKey, DWORD cfgVersion)
{
    HKEY subKey;
    wchar_t buf[30];
    int i = 1;
    wcscpy_s(buf, L"1");
    Items.DestroyMembers();
    while (OpenKey(hKey, buf, subKey))
    {
        CFindIgnoreItem* item = new CFindIgnoreItem;
        if (item == NULL)
        {
            TRACE_E(LOW_MEMORY);
            break;
        }
        std::wstring path;
        if (!gRegistry->GetString(subKey, FINDIGNOREITEM_PATH_REG, path).success)
            path.clear();
        item->Path = std::move(path);
        if (!GetValue(subKey, FINDIGNOREITEM_ENABLED_REG, REG_DWORD, &item->Enabled, sizeof(DWORD)))
            item->Enabled = TRUE; // saved only if it is FALSE
        if (Configuration.ConfigVersion < 32)
        {
            // users were confused that this folder was not searched
            // so we keep it listed but uncheck the checkbox
            // anyone interested can manually enable it
            if (item->Path == L"Local Settings\\Temporary Internet Files")
                item->Enabled = FALSE;
        }
        Items.Add(item);
        if (!Items.IsGood())
        {
            Items.ResetState();
            delete item;
            break;
        }
        _itow_s(++i, buf, _countof(buf), 10);
        CloseKey(subKey);
    }

    return TRUE;
}

BOOL CFindIgnore::Load(CFindIgnore* source)
{
    Items.DestroyMembers();
    int i;
    for (i = 0; i < source->Items.Count; i++)
    {
        CFindIgnoreItem* item = source->At(i);
        if (!Add(item->Enabled, item->Path.c_str()))
            return FALSE;
    }
    return TRUE;
}

BOOL CFindIgnore::Prepare(CFindIgnore* source)
{
    Items.DestroyMembers();
    int i;
    for (i = 0; i < source->Items.Count; i++)
    {
        CFindIgnoreItem* item = source->At(i);
        if (item->Enabled) // we are only interested in enabled items
        {
            const wchar_t* path = item->Path.c_str();
            while (*path == L' ')
                path++;
            CFindIgnoreItemType type = fiitRelative;
            if (path[0] == L'\\' && path[1] != L'\\')
                type = fiitRooted;
            // was LowerCase[path[0]] - a 256-entry narrow table indexed by a
            // path character. A drive letter is ASCII, so test the ranges directly instead.
            else if ((path[0] == L'\\' && path[1] == L'\\') ||
                     ((path[0] >= L'a' && path[0] <= L'z') || (path[0] >= L'A' && path[0] <= L'Z')) && path[1] == L':')
                type = fiitFull;

            std::wstring preparedPath;
            if (type != fiitFull && path[0] != L'\\')
                preparedPath.push_back(L'\\');
            preparedPath.append(path);
            if (preparedPath.back() != L'\\')
                preparedPath.push_back(L'\\');
            if (!Add(TRUE, preparedPath.c_str()))
                return FALSE;
            item = Items[Items.Count - 1];
            item->Type = type;
            item->Len = (int)preparedPath.length();
        }
    }
    return TRUE;
}

BOOL CFindIgnore::Contains(const wchar_t* path, int startPathLen)
{
    // full path
    int i;
    for (i = 0; i < Items.Count; i++)
    {
        // startPathLen is the path length entered in the Find dialog (search root);
        // only its subpaths are ignored, see https://forum.altap.cz/viewtopic.php?f=7&t=7434
        CFindIgnoreItem* item = Items[i];
        switch (item->Type)
        {
        case fiitFull:
        {
            if (item->Len > startPathLen && StrNICmpW(path, item->Path.c_str(), item->Len) == 0)
                return TRUE;
            break;
        }

        case fiitRooted:
        {
            const wchar_t* noRoot = SkipRootW(path);
            if ((noRoot - path) + item->Len > startPathLen && StrNICmpW(noRoot, item->Path.c_str(), item->Len) == 0)
                return TRUE;
            break;
        }

        case fiitRelative:
        {
            const wchar_t* m = path;
            while (m != NULL)
            {
                m = StrIStr(m, item->Path.c_str());
                if (m != NULL) // found
                {
                    if ((m - path) + item->Len > startPathLen) // is it a subpath? then ignore it
                        return TRUE;
                    m++; // look for another occurrence, maybe it will be in a subpath
                }
            }
            break;
        }
        }
    }
    return FALSE;
}

BOOL CFindIgnore::Move(int srcIndex, int dstIndex)
{
    CFindIgnoreItem* tmp = Items[srcIndex];
    if (srcIndex < dstIndex)
    {
        int i;
        for (i = srcIndex; i < dstIndex; i++)
            Items[i] = Items[i + 1];
    }
    else
    {
        int i;
        for (i = srcIndex; i > dstIndex; i--)
            Items[i] = Items[i - 1];
    }
    Items[dstIndex] = tmp;
    return TRUE;
}

BOOL CFindIgnore::Add(BOOL enabled, const wchar_t* path)
{
    CFindIgnoreItem* item = new CFindIgnoreItem;
    if (item == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }
    item->Enabled = enabled;
    item->Path = path ? path : L"";
    Items.Add(item);
    if (!Items.IsGood())
    {
        Items.ResetState();
        delete item;
        return FALSE;
    }
    return TRUE;
}

BOOL CFindIgnore::AddUnique(BOOL enabled, const wchar_t* path)
{
    int len = (int)wcslen(path);
    if (len < 1)
        return FALSE;
    if (path[len - 1] == L'\\') // compare without trailing backslashes
        len--;
    int i;
    for (i = 0; i < Items.Count; i++)
    {
        CFindIgnoreItem* item = Items[i];
        int itemLen = (int)item->Path.length();
        if (itemLen < 1)
            continue;
        if (item->Path[itemLen - 1] == L'\\') // compare without a trailing backslash
            itemLen--;
        if (len != itemLen)
            continue;
        if (StrNICmpW(path, item->Path.c_str(), len) == 0)
        {
            item->Enabled = TRUE; // always enable this item
            return TRUE;
        }
    }
    // not found -- add it
    return Add(enabled, path);
}

BOOL CFindIgnore::Set(int index, BOOL enabled, const wchar_t* path)
{
    if (index < 0 || index >= Items.Count)
    {
        TRACE_E("Index is out of range");
        return FALSE;
    }
    CFindIgnoreItem* item = Items[index];
    item->Path = path ? path : L"";
    item->Enabled = enabled;
    return TRUE;
}

//*********************************************************************************
//
// CDuplicateCandidates
//
// Container for CFoundFilesData when searching for duplicate files.
// 1) In the first phase, all files matching the Find criteria are added
//    to the CDuplicateCandidates object using the Add method.
// 2) Then the Examine() method is called which sorts the array using data->FindDupFlags criteria. If file contents
//    are compared, MD5 digests are calculated for potentially identical files.
//    Then, the array is sorted again and single files are removed so
//    Only files that appear at least twice remain in the array.
//    These get a Group variable so that sets can be distinguished in the result window.
//

class CDuplicateCandidates : public TIndirectArray<CFoundFilesData>
{
public:
    CDuplicateCandidates() : TIndirectArray<CFoundFilesData>(2000, 4000) {}

    // - loading/calculating MD5 digests
    // - removing single files
    // - setting the Group variable
    // - setting the Different flag
    void Examine(CGrepData* data);

protected:
    // compares two records using criteria byName, bySize and byMD5
    // byPath is a criterium with the lowest priority and is used only for clearer output
    int CompareFunc(CFoundFilesData* f1, CFoundFilesData* f2, BOOL byName, BOOL bySize, BOOL byMD5, BOOL byPath);

    // sort stored files by byName, bySize and byMD5 criteria
    void QuickSort(int left, int right, BOOL byName, BOOL bySize, BOOL byMD5);

    // goes through all stored items and uses CompareFunc to identify those that
    // appear only once; those are then removed from the array
    // before calling this method, the array must be sorted with QuickSort
    void RemoveSingleFiles(BOOL byName, BOOL bySize, BOOL byMD5);

    // goes through all stored items and uses CompareFunc assign them
    // to groups; Alternates the Different bit for the groups  (0, 1, 0, 1, 0, 1, ...)
    // before calling this method, the array must be sorted with QuickSort
    void SetDifferentFlag(BOOL byName, BOOL bySize, BOOL byMD5);

    // goes through all stored items and uses the Different flag to assign
    // Group values; groups are numbered increasingly (0, 1, 2, 3, 4, 5, ...)
    void SetGroupByDifferentFlag();

    // compute MD5 from the file 'file'
    // 'progress' is the numeric value shown in the status bar (optimized so we do not
    // update the same porgress repeatedly)
    // 'readSize' holds the number of bytes read so far across all files
    // 'totalSize' is the total number of bytes of all files for which the MD5 digest
    // will be determined
    // the method returns TRUE, if the MD5 value was successfully read; the digest is stored at
    // (BYTE*)data->Group
    // the method returns FALSE on read errors or when the user aborts the operation
    // (then, the variable data->StopSearch is set to TRUE)
    BOOL GetMD5Digest(CGrepData* data, CFoundFilesData* file,
                      int* progress, CQuadWord* readSize, const CQuadWord* totalSize);
};

// was a two-stage comparator: RegSetStrICmp on the CP_ACP mirrors, and when
// those tied while the wide names disagreed, a real signed wide comparison to preserve
// antisymmetry (QuickSort depends on it). The mirrors are gone, so the tie-break IS the
// comparison - the guard died with what it guarded.
static int CompareFoundStringsWide(const std::wstring& s1W, const std::wstring& s2W)
{
    return RegSetStrICmpW(s1W.c_str(), s2W.c_str());
}

int CDuplicateCandidates::CompareFunc(CFoundFilesData* f1, CFoundFilesData* f2,
                                      BOOL byName, BOOL bySize, BOOL byMD5, BOOL byPath)
{
    int res;
    if (bySize)
    {
        if (byName)
            res = CompareFoundStringsWide(f1->NameW, f2->NameW);
        else
            res = 0;
        if (res == 0)
        {
            if (f1->Size < f2->Size)
                res = -1;
            else
            {
                if (f1->Size == f2->Size)
                {
                    if (!byMD5 || f1->Size == CQuadWord(0, 0))
                        res = 0;
                    else
                        res = memcmp((void*)f1->Group, (void*)f2->Group, MD5_DIGEST_SIZE);
                }
                else
                    res = 1;
            }
        }
    }
    else
    {
        // byName && !bySize
        res = CompareFoundStringsWide(f1->NameW, f2->NameW);
    }
    if (byPath && res == 0)
        res = CompareFoundStringsWide(f1->PathW, f2->PathW);
    return res;
}

void CDuplicateCandidates::QuickSort(int left, int right, BOOL byName, BOOL bySize, BOOL byMD5)
{

LABEL_QuickSort:

    int i = left, j = right;
    CFoundFilesData* pivot = At((i + j) / 2);

    do
    {
        while (CompareFunc(At(i), pivot, byName, bySize, byMD5, TRUE) < 0 && i < right)
            i++;
        while (CompareFunc(pivot, At(j), byName, bySize, byMD5, TRUE) < 0 && j > left)
            j--;

        if (i <= j)
        {
            CFoundFilesData* swap = At(i);
            At(i) = At(j);
            At(j) = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // the following "nice" code was replaced by a version that saves stack space (max. log(N) recursion depth)
    //  if (left < j) QuickSort(left, j, byName, bySize, byMD5);
    //  if (i < right) QuickSort(i, right, byName, bySize, byMD5);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // both halves must be sorted: send the smaller half to recursion and handle the other via "goto"
            {
                QuickSort(left, j, byName, bySize, byMD5);
                left = i;
                goto LABEL_QuickSort;
            }
            else
            {
                QuickSort(i, right, byName, bySize, byMD5);
                right = j;
                goto LABEL_QuickSort;
            }
        }
        else
        {
            right = j;
            goto LABEL_QuickSort;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_QuickSort;
        }
    }
}

#define DUPLICATES_BUFFER_SIZE 16384 // buffer size for MD5 calculation

BOOL CDuplicateCandidates::GetMD5Digest(CGrepData* data, CFoundFilesData* file,
                                        int* progress, CQuadWord* readSize, const CQuadWord* totalSize)
{
    // build full path to the file
    // this used to build the same path twice - once from the (now deleted) narrow
    // Name/Path, and once as a wstring with an AnsiToWide fallback. GetFullNameW()
    // is that construction, once.
    std::wstring fullPathW = file->GetFullNameW();

    data->SearchingText->Set(fullPathW.c_str()); // set the current file

    // open the file for reading with sequential access
    HANDLE hFile = SalCreateFileH(fullPathW.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        BYTE buffer[DUPLICATES_BUFFER_SIZE];
        MD5 context;
        DWORD read; // number of bytes that were actually read
        while (TRUE)
        {
            // read a segment from a file 'file' into 'buffer'
            if (!ReadFile(hFile, buffer, DUPLICATES_BUFFER_SIZE, &read, NULL))
            {
                // error reading the file
                DWORD err = GetLastError();
                HANDLES(CloseHandle(hFile));

                std::wstring message = FormatStrW(LoadStrW(IDS_ERROR_READING_FILE2), GetErrorTextOwned(err).c_str());
                FIND_LOG_ITEM log;
                log.Flags = FLI_ERROR;
                log.Text = message.c_str();
                log.Path = fullPathW.c_str();
                SendMessage(data->HWindow, WM_USER_ADDLOG, (WPARAM)&log, 0);

                return FALSE;
            }

            // does the user want to stop the operation?
            if (data->StopSearch)
            {
                HANDLES(CloseHandle(hFile));
                return FALSE;
            }

            // if anything was read, update the MD5
            if (read > 0)
            {
                context.update(buffer, read);

                // compute and display progress (if the 'progress' value changed)
                *readSize += CQuadWord(read, 0);
                // NOT text: the reader does `int pos = buf[0]` - a 0..100 progress value
                // smuggled through the string API. Widening keeps that intact.
                wchar_t buff[100];
                int newProgress = *readSize >= *totalSize ? (totalSize->Value == 0 ? 0 : 100) : (int)((*readSize * CQuadWord(100, 0)) / *totalSize).Value;
                if (newProgress != *progress)
                {
                    *progress = newProgress;
                    buff[0] = (wchar_t)newProgress; // pass the numeric value directly instead of a string
                    buff[1] = 0;
                    data->SearchingText2->Set(buff); // update the total progress
                }
            }

            // if fewer bytes were read than the buffer size, we are done
            if (read != DUPLICATES_BUFFER_SIZE)
                break;
        }
        HANDLES(CloseHandle(hFile));

        context.finalize();
        memcpy((BYTE*)file->Group, context.digest, MD5_DIGEST_SIZE);

        return TRUE;
    }
    else
    {
        // error occured while opening the file
        DWORD err = GetLastError();

        std::wstring message = FormatStrW(LoadStrW(IDS_ERROR_OPENING_FILE2), GetErrorTextOwned(err).c_str());
        FIND_LOG_ITEM log;
        log.Flags = FLI_ERROR;
        log.Text = message.c_str();
        log.Path = fullPathW.c_str();
        SendMessage(data->HWindow, WM_USER_ADDLOG, (WPARAM)&log, 0);

        return FALSE;
    }
}

void CDuplicateCandidates::RemoveSingleFiles(BOOL byName, BOOL bySize, BOOL byMD5)
{
    if (Count == 0)
        return;

    CFoundFilesData* lastData = Count > 0 ? At(Count - 1) : NULL;
    int lastDataIndex = Count - 1;
    BOOL lastIsSingle = TRUE;
    int i;
    for (i = Count - 2; i >= 0; i--)
    {
        if (CompareFunc(At(i), lastData, byName, bySize, byMD5, FALSE) == 0)
        {
            lastIsSingle = FALSE;
        }
        else
        {
            if (lastIsSingle)
            {
                // lastData points to an item that occurs only once; remove it
                Delete(lastDataIndex);
            }
            lastDataIndex = i;
            lastData = At(i);
            lastIsSingle = TRUE;
        }
    }
    if (lastIsSingle)
    {
        // lastData points to an item that occurs only once; remove it
        Delete(lastDataIndex);
    }
}

void CDuplicateCandidates::SetDifferentFlag(BOOL byName, BOOL bySize, BOOL byMD5)
{
    if (Count == 0)
        return;

    CFoundFilesData* lastData = NULL;
    int different = 0;
    if (Count > 0)
    {
        lastData = At(0);
        lastData->Different = different;
    }
    int i;
    for (i = 1; i < Count; i++)
    {
        CFoundFilesData* data = At(i);
        if (CompareFunc(data, lastData, byName, bySize, byMD5, FALSE) == 0)
        {
            data->Different = different;
        }
        else
        {
            different++;
            if (different > 1)
                different = 0;
            lastData = data;
            lastData->Different = different;
        }
    }
}

void CDuplicateCandidates::SetGroupByDifferentFlag()
{
    if (Count == 0)
        return;

    CFoundFilesData* lastData = NULL;
    DWORD custom = 0;
    if (Count > 0)
    {
        lastData = At(0);
        lastData->Group = custom;
    }
    int i;
    for (i = 1; i < Count; i++)
    {
        CFoundFilesData* data = At(i);
        if (data->Different == lastData->Different)
        {
            data->Group = custom;
        }
        else
        {
            custom++;
            lastData = data;
            lastData->Group = custom;
        }
    }
}

void CDuplicateCandidates::Examine(CGrepData* data)
{
    if (Count == 0)
        return;

    // we received a list of found files matching the criteria

    // extract criteria for duplicate search
    BOOL byName = (data->FindDupFlags & FIND_DUPLICATES_NAME) != 0;
    BOOL bySize = (data->FindDupFlags & FIND_DUPLICATES_SIZE) != 0;
    BOOL byContent = bySize && (data->FindDupFlags & FIND_DUPLICATES_CONTENT) != 0;

    // search completed, preparing results (MD5 computation may still follow)
    data->SearchingText->Set(LoadStrW(IDS_FIND_DUPS_RESULTS));

    // sort them according to selected criteria
    QuickSort(0, Count - 1, byName, bySize, FALSE);

    // remove items that occur only once
    RemoveSingleFiles(byName, bySize, FALSE);

    CMD5Digest* digest = NULL;
    if (byContent)
    {
        // for files larger than 0 bytes we'll compute MD5
        // allocate memory for MD5 digests at once

        // determine the number of files with size greater than 0 bytes
        DWORD count = 0;
        int i;
        for (i = 0; i < Count; i++)
        {
            CFoundFilesData* file = At(i);
            if (file->Size > CQuadWord(0, 0))
                count++;
        }

        if (count > 0)
        {
            // allocate memory for MD5 digests in one array
            digest = (CMD5Digest*)malloc(count * sizeof(CMD5Digest));
            if (digest == NULL)
            {
                TRACE_E(LOW_MEMORY);
                return;
            }

            // set up the pointers
            CMD5Digest* iterator = digest;
            for (i = 0; i < Count; i++)
            {
                CFoundFilesData* file = At(i);
                if (file->Size > CQuadWord(0, 0))
                {
                    file->Group = (DWORD_PTR)iterator;
                    iterator++;
                }
                else
                    file->Group = 0;
            }

            // determine total file size for progress
            CQuadWord totalSize(0, 0);
            for (i = 0; i < Count; i++)
                totalSize += At(i)->Size;

            // retrieve the MD5 digest of files
            CQuadWord readSize(0, 0);
            int progress = -1;
            for (i = Count - 1; i >= 0; i--)
            {
                CFoundFilesData* file = At(i);
                if (file->Size > CQuadWord(0, 0))
                {
                    if (!GetMD5Digest(data, file, &progress, &readSize, &totalSize))
                    {
                        if (data->StopSearch)
                        {
                            // the user wants to stop searching
                            // trim the unprocessed items
                            int j;
                            for (j = 0; j <= i; j++)
                                Delete(0);
                            break; // show at least the duplicates that have been already found
                        }
                        // an error occurred during reading the file but the user wants to continue
                        // exclude the file from candidates
                        Delete(i);
                    }
                }
            }

            // search finished, preparing results
            data->SearchingText->Set(LoadStrW(IDS_FIND_DUPS_RESULTS));

            // sort the files again
            if (Count > 0)
                QuickSort(0, Count - 1, byName, bySize, TRUE);

            // remove items that occur only once
            RemoveSingleFiles(byName, bySize, TRUE);
        }
    }

    // set the Different bit
    SetDifferentFlag(byName, bySize, byContent);

    if (digest != NULL)
        free(digest);

    // assign numbers starting from 0 to the Group variable
    // files with the same Different bit will share the same value
    SetGroupByDifferentFlag();

    // add them to the listview
    int i;
    for (i = 0; i < Count; i++)
    {
        data->FoundFilesListView->Add(At(i));
        if (!data->FoundFilesListView->IsGood())
        {
            TRACE_E(LOW_MEMORY);
            data->FoundFilesListView->ResetState();
            // cut off items that were not added
            int j;
            for (j = Count - 1; j >= i; j--)
                Delete(j);
            // detach the already added ones
            DetachMembers();
            return;
        }
    }
    DetachMembers();
    return;
}

//*********************************************************************************
//
// CSearchForData
//

void CSearchForData::Set(const wchar_t* dirW, const wchar_t* masksGroupW, BOOL includeSubDirs)
{
    // one overload. The other two had no caller, and all three stored a narrow
    // Dir beside DirW with an AnsiToWide() fallback between them.
    DirW = dirW != NULL ? dirW : L"";
    MasksGroup.SetMasksString(masksGroupW);
    IncludeSubDirs = includeSubDirs;
}

//*********************************************************************************
//
// Search engine
//

#define SEARCH_SIZE 10000 // must be greater than the maximum string length

int SearchForward(CGrepData* data, char* txt, int size, int off)
{
    if (size < 0)
        return -1;
    int curOff = off, curSize = min(SEARCH_SIZE, size - curOff);
    int found;
    while (!data->StopSearch)
    {
        if (curSize >= data->SearchData.GetLength())
            found = data->SearchData.SearchForward(txt + curOff, curSize, 0); // find
        else
            break; // not found
        if (found == -1)
        {
            curOff += curSize - data->SearchData.GetLength() + 1;
            curSize = min(SEARCH_SIZE, size - curOff);
        }
        else
            return curOff + found;
    }
    return -1;
}

//
// ****************************************************************************

// TestFileContentAux uses __try/__except (SEH), which under /EHsc cannot
// coexist in the same function with a C++ object that needs unwind-driven destruction
// (MSVC C2712). The decoded std::wstring result must not live inline in the regexp-error
// branch below; isolating its construction and use in this helper keeps that object's
// lifetime entirely outside TestFileContentAux's own SEH-bearing scope.
void SendRegExpErrorLog(HWND hWindow, const char* lastErrorText)
{
    std::wstring lastErrW = sally::legacy_search::DecodeEngineAcp(lastErrorText);
    FIND_LOG_ITEM log;
    log.Flags = FLI_ERROR;
    log.Text = lastErrW.c_str();
    log.Path = NULL;
    SendMessage(hWindow, WM_USER_ADDLOG, (WPARAM)&log, 0);
}

// Regular-expression search over UTF-16 content: the completion of
// the "regex arm" the engine swap staged. The byte engine's CR/LF line splitter
// assumes ASCII-transparent bytes, which UTF-16 is not (a stray 0x0A/0x0D low
// byte inside an unrelated code unit would look like a line break) - so unlike
// UTF-8-native content (where only the PATTERN was wrong), the CONTENT itself
// must be re-encoded first. common/text/Utf16RegexBridge does that; this
// function reuses data->RegExpUtf8 (the same pattern the UTF-8-native arm
// already uses) against the re-encoded bytes, mirroring TestFileContentAux's
// own line-splitting/GREP_LINE_LEN/WholeWords logic exactly so the two arms stay
// behaviorally consistent (including the same byte-table WholeWords limitation
// on non-ASCII boundaries the UTF-8-native arm already has - not fixed here,
// out of scope for this task same as there).
//
// Deliberately its OWN function, not inlined into TestFileContentAux: that
// function uses __try/__except, which under /EHsc cannot host a local variable
// with a non-trivial destructor (MSVC C2712, see SendRegExpErrorLog above) -
// this function's std::string/std::vector decode buffers must live outside it.
// Windows SEH still protects this function's own memory access to 'txt': an
// exception here unwinds normally up to TestFileContentAux's __except, exactly
// like any other plain call made from within that __try block.
BOOL TestUtf16RegexContent(BOOL& ok, CQuadWord& fileOffset, const CQuadWord& totalSize,
                           DWORD viewSize, char* txt, CGrepData* data,
                           const sally::text::DetectionResult& enc)
{
    if (!data->RegExpUtf8.IsGood())
        return TRUE; // no usable UTF-8 pattern - regex over UTF-16 behaves as before: no match

    const std::uint8_t* body = (const std::uint8_t*)txt + enc.textOffset;
    const std::size_t bodySize = (std::size_t)viewSize - (std::size_t)enc.textOffset;
    const bool bigEndian = enc.encoding == sally::text::Encoding::Utf16Be;

    std::string decoded;
    std::vector<std::int64_t> offsets;
    const std::size_t rawConsumed =
        sally::text::DecodeUtf16ToUtf8(body, bodySize, bigEndian, decoded, offsets);

    // Whether the file has no more content beyond what THIS view could offer, and
    // whether the decoder actually consumed every byte it was given (false only
    // for a trailing lone byte / unpaired high surrogate right at the window edge).
    const bool isLastView = fileOffset + CQuadWord(viewSize, 0) >= totalSize;
    const bool decodedEverything = rawConsumed == bodySize;

    const char* dbeg = decoded.empty() ? NULL : decoded.data();
    const char* dtotalEnd = dbeg + decoded.size();
    const char* dcur = dbeg;
    BOOL EOL_CR = data->EOL_CR;
    BOOL EOL_LF = data->EOL_LF;
    BOOL EOL_CRLF = data->EOL_CRLF;

    while (!data->StopSearch && dcur < dtotalEnd)
    {
        const char* lend = dcur;
        const char* lineLimit = dcur + GREP_LINE_LEN;
        if (lineLimit > dtotalEnd)
            lineLimit = dtotalEnd;
        const char* nextLineBeg = NULL;
        do
        {
            if (*lend > '\r')
                lend++;
            else
            {
                if (*lend == '\r')
                {
                    if (lend + 1 < dtotalEnd && *(lend + 1) == '\n' && EOL_CRLF)
                    {
                        nextLineBeg = lend + 2;
                        break;
                    }
                    else
                    {
                        if (EOL_CR &&
                            (lend + 1 < dtotalEnd ||
                             !EOL_CRLF ||
                             (isLastView && decodedEverything)))
                        {
                            nextLineBeg = lend + 1;
                            break;
                        }
                    }
                }
                else
                {
                    if (*lend == '\n' && EOL_LF || *lend == 0)
                    {
                        nextLineBeg = lend + 1;
                        break;
                    }
                }
                lend++;
            }
        } while (lend < lineLimit);
        if (nextLineBeg == NULL)
            nextLineBeg = lend;

        if (lend == lineLimit && !(isLastView && decodedEverything))
        {
            // Incomplete line at the edge of what this view could decode, and the
            // file has more content still to come: resume at the RAW offset where
            // THIS line started (mapped through the offset table), exactly like
            // TestFileContentAux's own byte-domain continuation.
            const std::size_t startIdx = (std::size_t)(dcur - dbeg);
            const std::int64_t rawLineStart =
                startIdx < offsets.size() ? offsets[startIdx] : (std::int64_t)rawConsumed;
            fileOffset += CQuadWord((DWORD)((std::int64_t)enc.textOffset + rawLineStart), 0);
            return TRUE;
        }

        if (data->RegExpUtf8.SetLine(dcur, lend))
        {
            int foundLen, start = 0;
        UTF16_REGEXP_NEXT:
            int found = data->RegExpUtf8.SearchForward(start, foundLen);
            if (found != -1)
            {
                if (data->WholeWords)
                {
                    if ((found == 0 || *(dcur + found - 1) != '_' && IsNotAlphaNorNum[*(dcur + found - 1)]) &&
                        (found + foundLen == (lend - dcur) ||
                         *(dcur + found + foundLen) != '_' && IsNotAlphaNorNum[*(dcur + found + foundLen)]))
                    {
                        ok = TRUE;
                        return TRUE;
                    }
                    start = found + 1;
                    if (start < lend - dcur)
                        goto UTF16_REGEXP_NEXT;
                }
                else
                {
                    ok = TRUE;
                    return TRUE;
                }
            }
        }
        else
        {
            SendRegExpErrorLog(data->HWindow, data->RegExpUtf8.GetLastErrorText());
            return FALSE;
        }

        dcur = nextLineBeg;
    }
    // reached the end of what this view decoded without a match: advance past the
    // whole raw view, mirroring TestFileContentAux's own "beg >= totalEnd" case.
    fileOffset += CQuadWord(viewSize, 0);
    return TRUE;
}

// CORRECTION to P1.5w, which claimed 'path' was unreferenced and deleted it:
// it IS used, once, by the __except handler below (log.Path). P1.5w's grep stopped short of
// the handler. The parameter is back, wide - which is what FIND_LOG_ITEM::Path wants anyway,
// so TestFileContentW still has no reason to carry a narrow mirror.
BOOL TestFileContentAux(BOOL& ok, CQuadWord& fileOffset, const CQuadWord& totalSize,
                        DWORD viewSize, const wchar_t* path, char* txt, CGrepData* data)
{
    __try
    {
        if (data->Regular)
        {
            char *beg, *end, *nextBeg, *totalEnd, *endLimit;
            //      BOOL EOL_NULL = TRUE;
            BOOL EOL_CR = data->EOL_CR;
            BOOL EOL_LF = data->EOL_LF;
            BOOL EOL_CRLF = data->EOL_CRLF;

            // Pick the expression that matches THIS file's encoding.
            //
            // The pattern is compiled from UTF-8 and, when exactly representable,
            // from ACP too (see CGrepData::RegExpUtf8). UTF-8 content uses the
            // lossless UTF-8 expression; legacy content uses only the exact ACP twin.
            //
            // Nothing else has to change for UTF-8. The encoding is
            // ASCII-transparent, so a CR or LF byte never occurs inside a
            // multi-byte sequence: the line splitter below and the byte engine are
            // already correct over UTF-8 bytes. Only the pattern was wrong.
            //
            // Detection runs per view segment, matching what the literal path
            // already does. That is safe here even though a BOM only appears in the
            // first segment: a segment that detects as LegacyBytes is one with no
            // multi-byte sequences in it, i.e. pure ASCII, and both compilations
            // agree on pure ASCII.
            //
            // UTF-16: the byte engine cannot run over it directly for the same
            // reason as above, but its bytes are NOT a superset of ASCII, so
            // (unlike UTF-8) the CONTENT needs transforming too, not just the
            // pattern. common/text/Utf16RegexBridge re-encodes each view's UTF-16
            // bytes to UTF-8 — ASCII-transparent, so the line splitter and byte
            // engine below become correct over it, exactly as they already are
            // for UTF-8-native content — with a raw-offset table so a truncated
            // trailing line can be resumed from the right place. Handled in its
            // own block below (returns before reaching the byte-domain loop).
            CRegularExpression* regExp = &data->RegExp;
            sally::text::DetectionResult enc;
            if (data->RegExpUtf8.IsGood())
            {
                enc = sally::text::Detect((const std::uint8_t*)txt, viewSize);
                if (enc.encoding == sally::text::Encoding::Utf8)
                    regExp = &data->RegExpUtf8;
                else if (enc.encoding == sally::text::Encoding::Utf16Le ||
                        enc.encoding == sally::text::Encoding::Utf16Be)
                {
                    return TestUtf16RegexContent(ok, fileOffset, totalSize, viewSize,
                                                 txt, data, enc);
                }
            }
            if (!regExp->IsGood())
            {
                fileOffset += CQuadWord(viewSize, 0);
                return TRUE;
            }
            beg = txt;
            totalEnd = txt + viewSize;

            while (!data->StopSearch && beg < totalEnd)
            {
                end = beg;
                endLimit = beg + GREP_LINE_LEN;
                if (endLimit > totalEnd)
                    endLimit = totalEnd;
                nextBeg = NULL;
                do
                {
                    if (*end > '\r')
                        end++;
                    else
                    {
                        if (*end == '\r')
                        {
                            if (end + 1 < totalEnd && *(end + 1) == '\n' && EOL_CRLF)
                            {
                                nextBeg = end + 2;
                                break;
                            }
                            else
                            {
                                if (EOL_CR &&
                                    (end + 1 < totalEnd ||                              // it was able to test that there is no LF there
                                     !EOL_CRLF ||                                       // LF should not be considered an EOL
                                     fileOffset + CQuadWord(viewSize, 0) >= totalSize)) // it is the end of the file
                                {
                                    nextBeg = end + 1;
                                    break;
                                }
                            }
                        }
                        else
                        {
                            if (*end == '\n' && EOL_LF || *end == 0 /*&& EOL_NULL*/)
                            {
                                nextBeg = end + 1;
                                break;
                            }
                        }
                        end++;
                    }
                } while (end < endLimit);
                if (nextBeg == NULL)
                    nextBeg = end;

                if (end == endLimit &&                               // if no line ending character was found
                    fileOffset + CQuadWord(viewSize, 0) < totalSize) // the end of the file is not in the file view
                {                                                    // the line can continue beyond the boundary of the current view of the file
                    fileOffset += CQuadWord(DWORD(beg - txt), 0);
                    return TRUE; // continue with the next view segment
                }

                // line beg->end
                if (regExp->SetLine(beg, end))
                {
                    int foundLen, start = 0;

                GREP_REGEXP_NEXT:

                    int found = regExp->SearchForward(start, foundLen);
                    if (found != -1)
                    {
                        if (data->WholeWords)
                        {
                            if ((found == 0 || *(beg + found - 1) != '_' && IsNotAlphaNorNum[*(beg + found - 1)]) &&
                                (found + foundLen == (end - beg) ||
                                 *(beg + found + foundLen) != '_' && IsNotAlphaNorNum[*(beg + found + foundLen)]))
                            {
                                ok = TRUE; // found
                                break;
                            }
                            start = found + 1;
                            if (start < end - beg)
                                goto GREP_REGEXP_NEXT;
                        }
                        else
                        {
                            ok = TRUE; // found
                            break;
                        }
                    }
                }
                else
                {
                    // See SendRegExpErrorLog above: its std::wstring must not
                    // live inside this __try-bearing function (MSVC C2712).
                    SendRegExpErrorLog(data->HWindow, regExp->GetLastErrorText());
                    return FALSE; // do not search this file further
                }

                beg = nextBeg;
            }
            // line ends exactly at the end of the view segment (may also be the end of the file)
            if (beg >= totalEnd)
                fileOffset += CQuadWord(viewSize, 0); // advance the offset to continue searching
        }
        else if (!data->GrepText.empty())
        {
            // ENCODING-AWARE LITERAL SEARCH.
            //
            // The byte grep below can only find the needle when the file happens
            // to be in the active code page. A UTF-8 or UTF-16 file simply does
            // not contain those bytes, so content search silently reported
            // nothing — and with the needle itself previously captured through
            // GetDlgItemTextA from a Unicode dialog, a CJK search was meaningless
            // rather than merely lossy.
            //
            // SearchContent detects the content's encoding and matches wide
            // against wide, falling back to the byte path for legacy content whose
            // needle round-trips exactly. Its third outcome matters here:
            // NoMatchPossible means this file's encoding cannot represent the
            // needle at all, which is a different fact from "not found" and must
            // not be reported as a miss on a file we simply could not ask about.
            sally::text::SearchOptions options;
            options.caseSensitive = data->GrepCaseSensitive;
            options.wholeWords = data->WholeWords;
            const sally::text::SearchResult result =
                sally::text::SearchContent((const std::uint8_t*)txt, viewSize,
                                           data->GrepText, options);
            if (result.found())
                ok = TRUE;
            // NotFound and NoMatchPossible both leave 'ok' alone: the file is not
            // reported. They are distinguished for the caller's benefit, not for
            // this decision.
            else if (!data->StopSearch)
            {
                // This branch used to leave fileOffset untouched on a miss, so
                // the caller's `while (fileOffset < totalSize)` loop remapped and
                // re-searched the SAME view forever on any file that did not
                // contain the needle - Find hung on essentially the first file.
                //
                // SearchContent's own contract (ContentSearcher.h) is that a
                // match straddling the end of this view is not reported, so the
                // next view must overlap by at least one needle's worth of
                // bytes. The needle is wide; size the overlap for the widest
                // encoding this module can search (UTF-8, up to 4 bytes per wide
                // code unit) so a needle spanning the boundary is never missed.
                const CQuadWord overlap(
                    (DWORD)(data->GrepText.length() * 4 + 4), 0);
                if (fileOffset + CQuadWord(viewSize, 0) < totalSize &&
                    overlap < CQuadWord(viewSize, 0))
                {
                    fileOffset = fileOffset + CQuadWord(viewSize, 0) - overlap;
                }
                else
                    fileOffset = totalSize; // the rest of the file fit in this view
            }
        }
        else
        {
            int off = 0;
            while (1)
            {
                off = SearchForward(data, txt, viewSize, off);
                if (off != -1)
                {
                    if (data->WholeWords)
                    {
                        if ((fileOffset + CQuadWord(off, 0) == CQuadWord(0, 0) ||                                        // beginning of the file
                             off > 0 && txt[off - 1] != '_' && IsNotAlphaNorNum[txt[off - 1]]) &&                        // not at the start of the buffer and no letter or digit before the pattern
                            (fileOffset + CQuadWord(off, 0) + CQuadWord(data->SearchData.GetLength(), 0) >= totalSize || // end of the file
                             (DWORD)(off + data->SearchData.GetLength()) < viewSize &&                                   // not at the end of the buffer
                                 txt[off + data->SearchData.GetLength()] != '_' &&
                                 IsNotAlphaNorNum[txt[off + data->SearchData.GetLength()]])) // no letter or digit after the pattern
                        {
                            ok = TRUE; // found
                            break;
                        }
                        off++;
                    }
                    else
                    {
                        ok = TRUE; // found
                        break;
                    }
                }
                else
                    break; // not found or terminated
            }
            if (!ok && !data->StopSearch) // not found and not interrupted
            {
                if (fileOffset + CQuadWord(viewSize, 0) < totalSize &&
                    CQuadWord(data->SearchData.GetLength() + 1, 0) < CQuadWord(viewSize, 0))
                {
                    fileOffset = fileOffset + CQuadWord(viewSize, 0) - CQuadWord(data->SearchData.GetLength() + 1, 0);
                }
                else
                    fileOffset = totalSize; // the pattern cannot be in the file anymore
            }
        }
        return TRUE; // continue searching (unless we are at the end of the file)
    }
    __except (HandleFileException(GetExceptionInformation(), txt, viewSize))
    {
        // file error
        FIND_LOG_ITEM log;
        log.Flags = FLI_ERROR;
        log.Text = LoadStrW(IDS_FILEREADERROR2);
        log.Path = path;
        SendMessage(data->HWindow, WM_USER_ADDLOG, (WPARAM)&log, 0);
        ok = FALSE;   // not found
        return FALSE; // do not continue searching
    }
}

BOOL TestFileContentW(DWORD sizeLow, DWORD sizeHigh, const wchar_t* pathW,
                      CGrepData* data, BOOL isLink)
{
    CQuadWord totalSize(sizeLow, sizeHigh);
    CQuadWord fileOffset(0, 0);
    DWORD viewSize = 0;

    BOOL ok = FALSE;
    if (totalSize > CQuadWord(0, 0) || isLink)
    {
        DWORD err = ERROR_SUCCESS;
        data->SearchingText->Set(pathW); // set the current file
        HANDLE hFile = SalCreateFileH(pathW, GENERIC_READ,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                          OPEN_EXISTING,
                                          FILE_FLAG_SEQUENTIAL_SCAN,
                                          NULL);
        BOOL getLinkFileSizeErr = FALSE;
        if (hFile != INVALID_HANDLE_VALUE)
        {
            // links have zero file size; size of the target file must be obtained separately
            if (!isLink || SalGetFileSize(hFile, totalSize, err))
            {
                HANDLE mFile = HANDLES(CreateFileMapping(hFile, NULL, PAGE_READONLY,
                                                         0, 0, NULL));
                if (mFile != NULL)
                {
                    CQuadWord allocGran(AllocationGranularity, 0);
                    while (!data->StopSearch && fileOffset < totalSize)
                    {
                        // ensure the offset matches the granularity
                        CQuadWord mapFileOffset(fileOffset);
                        mapFileOffset = (mapFileOffset / allocGran) * allocGran;

                        // calculate the size of the view segment
                        if (CQuadWord(VOF_VIEW_SIZE, 0) <= totalSize - mapFileOffset)
                            viewSize = VOF_VIEW_SIZE;
                        else
                            viewSize = (DWORD)(totalSize - mapFileOffset).Value;

                        // map the file view
                        char* txt = (char*)HANDLES(MapViewOfFile(mFile, FILE_MAP_READ,
                                                                 mapFileOffset.HiDWord, mapFileOffset.LoDWord,
                                                                 viewSize));
                        if (txt != NULL)
                        {
                            // let the file view be examined
                            DWORD diff = (DWORD)(fileOffset - mapFileOffset).Value;
                            BOOL err2 = !TestFileContentAux(ok, fileOffset, totalSize, viewSize - diff,
                                                            pathW, txt + diff, data);
                            HANDLES(UnmapViewOfFile(txt));
                            if (err2 || ok)
                                break;
                        }
                        else
                            err = GetLastError();
                    }

                    HANDLES(CloseHandle(mFile));
                }
                else
                    err = GetLastError();
            }
            else
                getLinkFileSizeErr = TRUE;
            HANDLES(CloseHandle(hFile));
        }
        else
            err = GetLastError();

        if (err != ERROR_SUCCESS || getLinkFileSizeErr)
        {
            std::wstring message = FormatStrW(LoadStrW(getLinkFileSizeErr ? IDS_GETLINKTGTFILESIZEERROR : IDS_ERROR_OPENING_FILE2),
                                               GetErrorTextOwned(err).c_str());
            FIND_LOG_ITEM log;
            log.Flags = FLI_ERROR;
            log.Text = message.c_str();
            log.Path = pathW;
            SendMessage(data->HWindow, WM_USER_ADDLOG, (WPARAM)&log, 0);
        }
    }

    return ok;
}

BOOL AddFoundItem(const wchar_t* path, const wchar_t* name,
                  DWORD sizeLow, DWORD sizeHigh, DWORD attr, const FILETIME* lastWrite, BOOL isDir,
                  CGrepData* data, CDuplicateCandidates* duplicateCandidates)
{
    if (duplicateCandidates != NULL && isDir) // directories are irrelevant to us when searching for duplicates
        return TRUE;

    CFoundFilesData* foundData = new CFoundFilesData;
    if (foundData != NULL)
    {
        BOOL good = foundData->Set(path, name,
                                   CQuadWord(sizeLow, sizeHigh),
                                   attr, lastWrite, isDir);
        if (good)
        {
            if (duplicateCandidates == NULL)
            {
                // duplicateCandidates == NULL, adding the item to data->FoundFilesListView
                data->FoundFilesListView->Add(foundData);
                if (!data->FoundFilesListView->IsGood())
                {
                    data->FoundFilesListView->ResetState();
                    delete foundData;
                    foundData = NULL;
                }
                else
                {
                    // request a listview redraw after every 100 added items
                    // also after 0.5 seconds has passed since the last redraw
                    // we also call update for each item when grepping
                    if (data->FoundFilesListView->GetCount() >= data->FoundVisibleCount + 100 ||
                        GetTickCount() - data->FoundVisibleTick >= 500
                        /* || data->Grep*/) // a half-second update interval is enough even for grepping
                    {
                        SendMessage(data->HWindow, WM_USER_ADDFILE, 0, 0);
                    }
                    else
                        data->NeedRefresh = TRUE; // we will redraw at latest after 0.5 second
                }
            }
            else
            {
                // duplicateCandidates != NULL, adding the item to duplicateCandidates
                duplicateCandidates->Add(foundData);
                if (!duplicateCandidates->IsGood())
                {
                    duplicateCandidates->ResetState();
                    delete foundData;
                    foundData = NULL;
                }
            }
        }
        else
        {
            delete foundData;
            foundData = NULL;
        }
    }
    if (foundData == NULL)
    {
        FIND_LOG_ITEM log;
        log.Flags = FLI_ERROR;
        log.Text = LoadStrW(IDS_CANTSHOWRESULTS);
        log.Path = NULL;
        SendMessage(data->HWindow, WM_USER_ADDLOG, (WPARAM)&log, 0);

        data->StopSearch = TRUE;
        return FALSE;
    }
    return TRUE;
}

static std::wstring GetDirectoryWithoutSearchBackslashW(const std::wstring& pathW, size_t endIndex)
{
    std::wstring result(pathW, 0, endIndex);
    if (result.length() > 3 && (result[result.length() - 1] == L'\\' || result[result.length() - 1] == L'/'))
        result.resize(result.length() - 1);
    return result;
}

// 'dirStack' stores directories for late grepping. Otherwise,
// during searching in the current directory, recursive searching in subdirectories would occur. With this
// trick all files and directories matching the criteria are found first and
// then this function is called for all discovered directories.
// 'dirStack' only grows. When items are removed from it, they are just destroyed but
// not removed from the array, therefore the variable 'dirStackCount' holds the
// actual number of items in the array (always less than or equal to dirStack->Count).
// If memory is low or subdirectories are not searched,
// 'dirStack' is NULL.
// If 'duplicateCandidates' != NULL, found items will be added to this array
// instead of data->FoundFilesListView
void SearchDirectoryW(std::wstring& pathW, size_t endIndex, int startPathLen,
                      CMaskGroup* masksGroup, BOOL includeSubDirs, CGrepData* data,
                      TDirectArray<wchar_t*>* dirStack, int dirStackCount,
                      CDuplicateCandidates* duplicateCandidates,
                      CFindIgnore* ignoreList)
{
    std::wstring pathWithSlashW(pathW, 0, endIndex);
    // pathWithSlashA was a CP_ACP mirror of pathWithSlashW used for the trace,
    // CFindIgnore::Contains and FIND_LOG_ITEM::Path - all three take wide now.
    SLOW_CALL_STACK_MESSAGE6("SearchDirectoryW(%ls, , %d, %ls, %d, , , %d, , )",
                             pathWithSlashW.c_str(), startPathLen, masksGroup->GetMasksString(),
                             includeSubDirs, dirStackCount);

    if (ignoreList != NULL && ignoreList->Contains(pathWithSlashW.c_str(), startPathLen))
    {
        FIND_LOG_ITEM log;
        log.Flags = FLI_INFO;
        log.Text = LoadStrW(IDS_FINDLOG_SKIP);
        log.Path = pathWithSlashW.c_str();
        SendMessage(data->HWindow, WM_USER_ADDLOG, (WPARAM)&log, 0);
        return;
    }

    WIN32_FIND_DATAW file;
    std::wstring findMaskW = pathWithSlashW;
    findMaskW.push_back(L'*');
    HANDLE find = SalFindFirstFileHW(findMaskW.c_str(), &file);
    if (find != INVALID_HANDLE_VALUE)
    {
        std::wstring displayDirW = GetDirectoryWithoutSearchBackslashW(pathW, endIndex);
        data->SearchingText->Set(displayDirW.c_str()); // set the current path

        int dirStackEnterCount = 0; // number of items before starting the search at this level
        if (dirStack != NULL)
            dirStackEnterCount = dirStackCount;
        BOOL testFindNextErr = TRUE;

        do
        {
            BOOL isDir = (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            BOOL ignoreDir = isDir && (wcscmp(file.cFileName, L".") == 0 || wcscmp(file.cFileName, L"..") == 0);
            size_t fileNameLenW = wcslen(file.cFileName);
            if (ignoreDir || file.cFileName[0] != 0)
            {
                // after finding an item without displaying it and once 0.5 s have passed since the last redraw,
                // we request the listview to redraw
                if (data->NeedRefresh && GetTickCount() - data->FoundVisibleTick >= 500)
                {
                    SendMessage(data->HWindow, WM_USER_ADDFILE, 0, 0);
                    data->NeedRefresh = FALSE;
                }

                // file.cFileName[0] != 0 replaces the old cFileNameA[0] != 0 check -
                // that was a CP_ACP mirror (WideFileNameToAnsi, best-fit ON, unchecked) computed
                // solely to test non-emptiness, the same defect shape as this file's own
                // the earlier fixes (real logic already migrated to the wide name;
                // the mirror was vestigial and could spuriously read empty on a non-representable
                // name, silently dropping a real find result). Win32 never returns an empty
                // cFileName for a found entry, so this is always true - deleted the round-trip
                // instead of guarding it.
                if (file.cFileName[0] != 0 && !ignoreDir)
                {
                    // add all files and directories except "." and ".."
                    std::wstring currentItemW = pathWithSlashW;
                    currentItemW.append(file.cFileName);
                    data->SearchingText->Set(currentItemW.c_str()); // set the current item

                    // test the criteria attributes, size, date and time
                    CQuadWord size(file.nFileSizeLow, file.nFileSizeHigh);
                    BOOL fileTypeOK = data->FileTypeMode == fftmAll ||
                                      (data->FileTypeMode == fftmFiles && !isDir) ||
                                      (data->FileTypeMode == fftmFolders && isDir);
                    if (fileTypeOK && data->Criteria.Test(file.dwFileAttributes, &size, &file.ftLastWriteTime))
                    {
                        // file name
                        // let the extension be resolved if ext==NULL
                        // wide: match the genuine wide name directly instead of the
                        // CP_ACP mirror - the same defect as RefineData's fix in this file.
                        if (masksGroup->AgreeMasks(file.cFileName, NULL)) // mask is OK
                        {
                            BOOL ok;
                            if (data->Grep)
                            {
                                // content
                                if (isDir)
                                    ok = FALSE; // a directory cannot be grepped
                                else
                                {
                                    std::wstring fullPathW = pathWithSlashW;
                                    fullPathW.append(file.cFileName);
                                    // links: file.nFileSizeLow == 0 && file.nFileSizeHigh == 0, the file size
                                    // must be additionally obtained via SalGetFileSize()
                                    BOOL isLink = (file.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                                    ok = TestFileContentW(file.nFileSizeLow, file.nFileSizeHigh,
                                                          fullPathW.c_str(), data, isLink);
                                }
                            }
                            else
                                ok = TRUE;

                            // if the item matches all criteria,
                            // add it to the list of found items
                            if (ok)
                            {
                                AddFoundItem(displayDirW.c_str(), file.cFileName,
                                              file.nFileSizeLow, file.nFileSizeHigh,
                                              file.dwFileAttributes, &file.ftLastWriteTime, isDir, data,
                                              duplicateCandidates);
                            }
                        }
                    }
                }
                if (isDir && includeSubDirs && !ignoreDir) // directory + not "." or ".."
                {
                    BOOL searchNow = TRUE;

                    if (dirStack != NULL)
                    {
                        // just store for later search
                        wchar_t* newFileName = new wchar_t[fileNameLenW + 1];
                        if (newFileName != NULL)
                        {
                            wcscpy_s(newFileName, fileNameLenW + 1, file.cFileName);
                            if (dirStackCount < dirStack->Count)
                            {
                                // no need to assign an item - we have space
                                dirStack->At(dirStackCount) = newFileName;
                                dirStackCount++;
                                searchNow = FALSE;
                            }
                            else
                            {
                                // we must allocate a new item in the array
                                dirStack->Add(newFileName);
                                if (dirStack->IsGood())
                                {
                                    dirStackCount++;
                                    searchNow = FALSE;
                                }
                                else
                                {
                                    dirStack->ResetState();
                                    delete[] newFileName;
                                }
                            }
                        }
                    }

                    if (searchNow)
                    {
                        // out of memory - we will not use dirStack
                        pathW.resize(endIndex);
                        pathW.append(file.cFileName);
                        pathW.push_back(L'\\');
                        SearchDirectoryW(pathW, pathW.length(), startPathLen, masksGroup, includeSubDirs, data,
                                         NULL, 0, duplicateCandidates, ignoreList);
                        pathW.resize(endIndex);
                    }
                }
            }
            if (data->StopSearch)
            {
                testFindNextErr = FALSE;
                break;
            }
        } while (SalLPFindNextFile(find, &file));
        DWORD err = GetLastError();
        SalLPFindClose(find);

        if (testFindNextErr && err != ERROR_NO_MORE_FILES)
        {
            std::wstring message = FormatStrW(LoadStrW(IDS_DIRERRORFORMAT), GetErrorTextOwned(err).c_str());
            FIND_LOG_ITEM log;
            log.Flags = FLI_ERROR;
            log.Text = message.c_str();
            log.Path = displayDirW.c_str();
            SendMessage(data->HWindow, WM_USER_ADDLOG, (WPARAM)&log, 0);
        }

        // search through directories
        if (dirStack != NULL)
        {
            int i;
            for (i = dirStackEnterCount; i < dirStackCount; i++)
            {
                wchar_t* newFileName = (wchar_t*)dirStack->At(i);
                if (!data->StopSearch) // may be set during SearchDirectoryW
                {
                    pathW.resize(endIndex);
                    pathW.append(newFileName);
                    pathW.push_back(L'\\');
                    SearchDirectoryW(pathW, pathW.length(), startPathLen, masksGroup, includeSubDirs, data,
                                     dirStack, dirStackCount, duplicateCandidates, ignoreList);
                    pathW.resize(endIndex);
                }
            }
            // and release data from this level
            for (i = dirStackEnterCount; i < dirStackCount; i++)
                delete[] dirStack->At(i);
        }
    }
    else
    {
        DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_NO_MORE_FILES)
        {
            std::wstring displayDirW = GetDirectoryWithoutSearchBackslashW(pathW, endIndex);
            std::wstring message = FormatStrW(LoadStrW(IDS_DIRERRORFORMAT), GetErrorTextOwned(err).c_str());

            FIND_LOG_ITEM log;
            log.Flags = FLI_ERROR | FLI_IGNORE;
            log.Text = message.c_str();
            log.Path = displayDirW.c_str();
            SendMessage(data->HWindow, WM_USER_ADDLOG, (WPARAM)&log, 0);
        }
    }
    pathW.resize(endIndex);
}

void RefineData(CMaskGroup* masksGroup, CGrepData* data)
{
    int refineCount = data->FoundFilesListView->GetDataForRefineCount();
    int oldProgress = -1;

    int i;
    for (i = 0; i < refineCount && !data->StopSearch; i++)
    {
        if (!data->Grep)
        {
            // if grepping is disabled, show progress in percent
            int progress = (int)((double)i / (double)refineCount * 100.0);
            if (progress != oldProgress)
            {
                wchar_t buf[20];
                _snwprintf_s(buf, _TRUNCATE, L"%d%%", progress);
                data->SearchingText->Set(buf); // set the current path
                oldProgress = progress;
            }
        }

        CFoundFilesData* refineData = data->FoundFilesListView->GetDataForRefine(i);

        // test the criteria
        BOOL ok = TRUE;

        if (ok && data->FileTypeMode == fftmFiles && refineData->IsDir)
            ok = FALSE;
        if (ok && data->FileTypeMode == fftmFolders && !refineData->IsDir)
            ok = FALSE;

        // attributes, size, date, time
        if (ok && !data->Criteria.Test(refineData->Attr, &refineData->Size, &refineData->LastWrite))
            ok = FALSE;

        // file name (let the extension be resolved if ext==NULL)
        // wide: AgreeMasks(char*) round-tripped the already-lossy CP_ACP mirror
        // through AnsiToWide internally; compare the genuine wide name directly instead.
        if (ok && !masksGroup->AgreeMasks(refineData->NameW.c_str(), NULL))
            ok = FALSE;

        // content
        if (ok && data->Grep)
        {
            if (refineData->IsDir)
                ok = FALSE; // a directory cannot be grepped
            else
            {
                std::wstring fullPathW = refineData->GetFullNameW(); // was the same double construction
                // links: refineData->Size == 0, the file size must be additionally obtained via SalGetFileSize()
                BOOL isLink = (refineData->Attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0; // size == 0, the file size must be obtained via SalGetFileSize()
                ok = TestFileContentW(refineData->Size.LoDWord, refineData->Size.HiDWord,
                                      fullPathW.c_str(), data, isLink);
            }
        }

        // if refine==1 (intersect) and the item matches, add it
        // if refine==2 (subtract) and the item does not match, add it
        if (data->Refine == 1 && ok ||
            data->Refine == 2 && !ok)
        {
            AddFoundItem(refineData->PathW.c_str(), refineData->NameW.c_str(),
                          refineData->Size.LoDWord, refineData->Size.HiDWord,
                          refineData->Attr, &refineData->LastWrite,
                          refineData->IsDir, data, NULL);
        }
    }
}

unsigned GrepThreadFBody(void* ptr)
{
    CALL_STACK_MESSAGE1("GrepThreadFBody()");

    SetThreadNameInVCAndTrace(L"Grep");
    TRACE_I("Begin");
    //  Sleep(200);  // give the dialog a moment to redraw...
    CGrepData* data = (CGrepData*)ptr;
    data->NeedRefresh = FALSE;
    data->Criteria.PrepareForTest();
    if (data->Refine != 0)
    {
        if (data->Data->Count > 0)
        {
            CMaskGroup* mg = &data->Data->At(0)->MasksGroup;
            int errorPos;
            if (mg->PrepareMasks(errorPos))
            {
                RefineData(mg, data);
            }
            else
            {
                TRACE_E("PrepareMasks failed errorPos=" << errorPos);
                data->StopSearch = TRUE;
            }
        }
    }
    else
    {
        // if we search for duplicates, data are primarily placed into this array
        // after scanning all directories, the array is sorted (by name or by size)
        // if content is checked, MD5 is calculated for ambiguous cases
        // afterwards the data are passed to FoundFilesListView
        CDuplicateCandidates* duplicateCandidates = NULL;
        if (data->FindDuplicates)
        {
            duplicateCandidates = new CDuplicateCandidates;
            if (duplicateCandidates == NULL)
            {
                TRACE_E(LOW_MEMORY); // the algorithm will run even without the stack
                data->StopSearch = TRUE;
            }
        }

        if (!data->StopSearch)
        {
            int i;
            for (i = 0; i < data->Data->Count; i++)
            {
                CSearchForData* searchData = data->Data->At(i);
                // DirW is the only representation now, so the AnsiToWide()
                // fallback is gone. 'pathA' existed only to compute startPathLen from the ANSI
                // mirror; CFindIgnore compares against WIDE lengths, so that is pathW.length().
                std::wstring pathW = searchData->DirW;
                if (!pathW.empty() && pathW[pathW.length() - 1] != L'\\' && pathW[pathW.length() - 1] != L'/')
                    pathW.push_back(L'\\');
                const int len = (int)pathW.length();
                CMaskGroup* mg = &searchData->MasksGroup;
                int errorPos;
                if (!mg->PrepareMasks(errorPos))
                {
                    TRACE_E("PrepareMasks failed errorPos=" << errorPos);
                    data->StopSearch = TRUE;
                    break;
                }

                BOOL includeSubDirs = searchData->IncludeSubDirs;
                TDirectArray<wchar_t*>* dirStack = NULL; // see description at SearchDirectoryW
                if (includeSubDirs)
                {
                    dirStack = new TDirectArray<wchar_t*>(1000, 1000);
                    if (dirStack == NULL)
                        TRACE_E(LOW_MEMORY); // the algorithm will run even without the stack
                }

                // create a local copy of the ignore list since it has to be processed anyway
                // and as a bonus the user can edit the ignore list while searching
                CFindIgnore* ignoreList = new CFindIgnore;
                if (ignoreList == NULL)
                    TRACE_E(LOW_MEMORY); // the algorithm will run even without the ignore list
                else
                {
                    if (!ignoreList->Prepare(&FindIgnore))
                    {
                        delete ignoreList;
                        ignoreList = NULL;
                    }
                }

                SearchDirectoryW(pathW, pathW.length(), len, mg, includeSubDirs, data, dirStack, 0,
                                 duplicateCandidates, ignoreList);

                if (ignoreList != NULL)
                    delete ignoreList;

                if (dirStack != NULL)
                    delete dirStack;
                if (data->StopSearch)
                    break;
            }
        }
        if (duplicateCandidates != NULL)
        {
            if (!data->StopSearch)
                duplicateCandidates->Examine(data);
            delete duplicateCandidates;
        }
    }

    data->SearchStopped = data->StopSearch;
    SendMessage(data->HWindow, WM_USER_ADDFILE, 0, 0); // update the listview
    PostMessage(data->HWindow, WM_COMMAND, IDC_FIND_STOP, 0);
    TRACE_I("End");
    return 0;
}

unsigned GrepThreadFEH(void* param)
{
#ifndef CALLSTK_DISABLE
    __try
    {
#endif // CALLSTK_DISABLE
        return GrepThreadFBody(param);
#ifndef CALLSTK_DISABLE
    }
    __except (CCallStack::HandleException(GetExceptionInformation()))
    {
        TRACE_I("Thread Grep: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // harder exit (this call still performs some operations)
        return 1;
    }
#endif // CALLSTK_DISABLE
}

DWORD WINAPI GrepThreadF(void* param)
{
#ifndef CALLSTK_DISABLE
    CCallStack stack;
#endif // CALLSTK_DISABLE
    return GrepThreadFEH(param);
}

//*********************************************************************************
//
// CSearchingString
//

CSearchingString::CSearchingString()
{
    HANDLES(InitializeCriticalSection(&Section));
    BaseLen = 0;
    Dirty = FALSE;
}

CSearchingString::~CSearchingString()
{
    HANDLES(DeleteCriticalSection(&Section));
}

void CSearchingString::SetBase(const wchar_t* buf)
{
    HANDLES(EnterCriticalSection(&Section));
    Buffer = buf != NULL ? buf : L"";
    BaseLen = Buffer.length();
    HANDLES(LeaveCriticalSection(&Section));
}

void CSearchingString::Set(const wchar_t* buf)
{
    HANDLES(EnterCriticalSection(&Section));
    if (Buffer.length() > BaseLen)
        Buffer.resize(BaseLen);
    Buffer.append(buf != NULL ? buf : L"");
    Dirty = TRUE;
    HANDLES(LeaveCriticalSection(&Section));
}

void CSearchingString::Get(wchar_t* buf, int bufSize)
{
    HANDLES(EnterCriticalSection(&Section));
    lstrcpynW(buf, Buffer.c_str(), bufSize);
    HANDLES(LeaveCriticalSection(&Section));
}

std::wstring CSearchingString::GetWString()
{
    HANDLES(EnterCriticalSection(&Section));
    std::wstring text = Buffer;
    HANDLES(LeaveCriticalSection(&Section));
    return text;
}

void CSearchingString::SetDirty(BOOL dirty)
{
    HANDLES(EnterCriticalSection(&Section));
    Dirty = dirty;
    HANDLES(LeaveCriticalSection(&Section));
}

BOOL CSearchingString::GetDirty()
{
    BOOL r;
    HANDLES(EnterCriticalSection(&Section));
    r = Dirty;
    HANDLES(LeaveCriticalSection(&Section));
    return r;
}

//*********************************************************************************
//
// Find Dialog Thread functions
//

struct CTFDData
{
    CFindDialog* FindDialog;
    BOOL Success;
};

unsigned ThreadFindDialogMessageLoopBody(void* parameter)
{
    CALL_STACK_MESSAGE1("ThreadFindDialogMessageLoopBody()");
    BOOL ok;

    { // this block ensures destructors are called properly before calling _end_thread() (see below)
        SetThreadNameInVCAndTrace(L"FindDialog");
        TRACE_I("Begin");
        CTFDData* data = (CTFDData*)parameter;
        CFindDialog* findDialog = data->FindDialog;
        findDialog->SetZeroOnDestroy(&findDialog); // on WM_DESTROY the pointer is zeroed
                                                   // protection against accessing an invalid pointer
                                                   // from the message loop after the window is destroyed

        data->Success = TRUE;

        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

        if (findDialog->Create() != NULL)
        {
            SetForegroundWindow(findDialog->HWindow);
            UpdateWindow(findDialog->HWindow);
        }
        else
            data->Success = FALSE;

        ok = data->Success;
        data = NULL;                  // no longer valid afterwards
        SetEvent(FindDialogContinue); // let the main thread continue

        if (ok) // if the window was created, run the message loop
        {
            CALL_STACK_MESSAGE1("ThreadFindDialogMessageLoopBody::message_loop");

            MSG msg;
            HWND findDialogHWindow = findDialog->HWindow; // because of WM_QUIT, when the window will no longer be allocated
            BOOL haveMSG = FALSE;                         // FALSE means GetMessageW() should be called in the loop condition
            while (haveMSG || GetMessageW(&msg, NULL, 0, 0))
            {
                haveMSG = FALSE;
                if ((msg.message == WM_SYSKEYDOWN || msg.message == WM_KEYDOWN) &&
                    msg.wParam != VK_MENU && msg.wParam != VK_CONTROL && msg.wParam != VK_SHIFT)
                    SetCurrentToolTip(NULL, 0); // turn off the tooltip
                // ensure messages reach our menu (avoids the need for a keyboard hook)
                if (findDialog == NULL || !findDialog->IsMenuBarMessage(&msg))
                {
                    if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && findDialog != NULL)
                        findDialog->SetProcessingEscape(TRUE);
                    if (findDialog == NULL ||
                        (!TranslateAccelerator(findDialogHWindow, FindDialogAccelTable, &msg)) &&
                            (!findDialog->ManageHiddenShortcuts(&msg)) &&
                            (!IsDialogMessage(findDialogHWindow, &msg)))
                    {
                        TranslateMessage(&msg); // prevent generating WM_CHAR -> would cause a beep on Cancel
                        DispatchMessageW(&msg);
                    }
                    if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && findDialog != NULL)
                        findDialog->SetProcessingEscape(FALSE);
                }

                if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
                {
                    if (msg.message == WM_QUIT)
                        break;      // equivalent to the situation when GetMessageW() is returning FALSE
                    haveMSG = TRUE; // a message is pending; process it without calling GetMessageW()
                }
                else // if there is no message in the queue, perform Idle processing
                {
                    if (findDialog != NULL)
                        findDialog->OnEnterIdle();
                }
            }
        }

        TRACE_I("End");
    }

#ifndef CALLSTK_DISABLE
    CCallStack::ReleaseBeforeExitThread(); // before exiting the thread, we must release call-stack data (still in protected section - generating our bug report)
#endif                                     // CALLSTK_DISABLE
    _endthreadex(ok ? 0 : 1);
    return ok ? 0 : 1; // dead code to keep the compiler happy
}

unsigned ThreadFindDialogMessageLoopEH(void* param)
{
#ifndef CALLSTK_DISABLE
    __try
    {
#endif // CALLSTK_DISABLE
        return ThreadFindDialogMessageLoopBody(param);
#ifndef CALLSTK_DISABLE
    }
    __except (CCallStack::HandleException(GetExceptionInformation()))
    {
        TRACE_I("Thread FindDialogMessageLoop: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // harder exit (this call still performs some operations)
        return 1;
    }
#endif // CALLSTK_DISABLE
}

DWORD WINAPI ThreadFindDialogMessageLoop(void* param)
{
    CCallStack stack;
    return ThreadFindDialogMessageLoopEH(param);
}

BOOL OpenFindDialog(HWND hCenterAgainst, const wchar_t* initPath)
{
    CALL_STACK_MESSAGE3("OpenFindDialog(0x%p, %ls)", hCenterAgainst, initPath);

    HCURSOR hOldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

    CFindDialog* findDlg = new CFindDialog(hCenterAgainst, initPath);
    if (findDlg != NULL && findDlg->IsGood())
    {
        CTFDData data;
        data.FindDialog = findDlg;

        DWORD ThreadID;
        HANDLE loop = HANDLES(CreateThread(NULL, 0, ThreadFindDialogMessageLoop, &data, 0, &ThreadID));
        if (loop == NULL)
        {
            TRACE_E("Unable to start ThreadFindDialogMessageLoop thread.");
            goto ERROR_TFD_CREATE;
        }

        WaitForSingleObject(FindDialogContinue, INFINITE); // wait until the thread starts
        if (!data.Success)
        {
            HANDLES(CloseHandle(loop));
            goto ERROR_TFD_CREATE;
        }
        AddAuxThread(loop); // add the thread among existing viewers (killed on exit)
        SetCursor(hOldCur);
        return TRUE;
    }
    else
    {
        TRACE_E(LOW_MEMORY);

    ERROR_TFD_CREATE:

        if (findDlg != NULL)
            delete findDlg;

        SetCursor(hOldCur);
        return FALSE;
    }
    SetCursor(hOldCur);
    return TRUE;
}
