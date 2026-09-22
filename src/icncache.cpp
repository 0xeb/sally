// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "ui/IPrompter.h"
#include "common/unicode/helpers.h"
#include "common/text/CaseFolding.h"
#include "common/IEnvironment.h"
#include "common/IRegistry.h"
#include "dialogs.h"
#include "mainwnd.h"
#include "plugins.h"
#include "geticon.h"
#include "logo.h"

// should be a multiple of IL_ITEMS_IN_ROW value
// to fully utilize the space in the bitmap
#define ICONS_IN_LIST 100

//
// ****************************************************************************
// CIconCache
//

CIconCache::CIconCache()
    : TDirectArray<CIconData>(50, 30),
      IconsCache(10, 5),     // one item is CIconList holding ICONS_IN_LIST icons
      ThumbnailsCache(1, 20) // getting thumbnails is slow, relocation is a trifle
{
    IconsCount = 0;
    IconSize = ICONSIZE_COUNT; // not set yet; attempt to add icon without calling SetIconSize() first will cause TRACE_E
    DataIfaceForFS = NULL;
}

CIconCache::~CIconCache()
{
    Destroy();
}

// Raw DWORD-wise comparator over the packed sort keys. It is NOT
// lexicographic (it compares four bytes at a time as a little-endian DWORD) -- it is a
// consistent arbitrary total order, which is all the sort and the binary search need,
// provided both use it. The keys (CIconData::NameAndData, CAssociationData::
// ExtensionAndData) are wchar_t* and DWORD-aligned with zero padding, so the parameters
// are void* and 'lengthBytes' is a BYTE span -- the old name said DWORDs but the code
// always used it as a byte offset, and every caller now derives it from wcslen().
inline int CompareDWORDS(const void* p1, const void* p2, int lengthBytes)
{
    const char* s1 = (const char*)p1;
    const char* s2 = (const char*)p2;
    const char* end = s1 + lengthBytes;
    while (s1 <= end)
    {
        //    if ((res = *(DWORD *)s1 - *(DWORD *)s2) != 0) return res;  // this doesn't work (try 0x8 and 0x0 in 4-bit numbers)
        if (*(DWORD*)s1 > *(DWORD*)s2)
            return 1;
        else
        {
            if (*(DWORD*)s1 < *(DWORD*)s2)
                return -1;
        }
        s1 += sizeof(DWORD);
        s2 += sizeof(DWORD);
    }
    return 0;
}

void CIconCache::SortArray(int left, int right, CPluginDataInterfaceEncapsulation* dataIface)
{
    if (dataIface != NULL) // this is pitFromPlugin: let the plugin compare items itself (it must be comparison
    {                      // with no ties between any two listing items)
        DataIfaceForFS = dataIface;
        BOOL ok = TRUE;
        int i;
        for (i = left; i <= right; i++) // one paranoid test
        {
            if (Data[i].GetFSFileData() == NULL)
            {
                TRACE_EW(L"CIconCache::SortArray(): unexpected error: Icon Cache doesn't contain FSFileData for item: " << Data[i].NameAndData);
                ok = FALSE;
                break;
            }
        }
        if (ok)
            SortArrayForFSInt(left, right);
        DataIfaceForFS = NULL;
    }
    else // classic sorting by name
    {
        SortArrayInt(left, right);
    }
}

void CIconCache::SortArrayInt(int left, int right)
{

LABEL_SortArrayInt:

    int i = left, j = right;
    wchar_t* pivot = Data[(i + j) / 2].NameAndData;
    int length = (int)(wcslen(pivot) * sizeof(wchar_t));

    do
    {
        while (CompareDWORDS(Data[i].NameAndData, pivot, length) < 0 && i < right)
            i++;
        while (CompareDWORDS(pivot, Data[j].NameAndData, length) < 0 && j > left)
            j--;

        if (i <= j)
        {
            CIconData swap = Data[i];
            Data[i] = Data[j];
            Data[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // the following "nice" code was replaced with code that significantly saves stack space (max. log(N) recursion nesting)
    //  if (left < j) SortArrayInt(left, j);
    //  if (i < right) SortArrayInt(i, right);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // need to sort both "halves", so we send the smaller one to recursion, process the other via "goto"
            {
                SortArrayInt(left, j);
                left = i;
                goto LABEL_SortArrayInt;
            }
            else
            {
                SortArrayInt(i, right);
                right = j;
                goto LABEL_SortArrayInt;
            }
        }
        else
        {
            right = j;
            goto LABEL_SortArrayInt;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_SortArrayInt;
        }
    }
}

void CIconCache::SortArrayForFSInt(int left, int right)
{

LABEL_SortArrayForFSInt:

    int i = left, j = right;
    const CFileData* pivot = Data[(i + j) / 2].FSFileData;

    do
    {
        while (DataIfaceForFS->CompareFilesFromFS(Data[i].FSFileData, pivot) < 0 && i < right)
            i++;
        while (DataIfaceForFS->CompareFilesFromFS(pivot, Data[j].FSFileData) < 0 && j > left)
            j--;

        if (i <= j)
        {
            CIconData swap = Data[i];
            Data[i] = Data[j];
            Data[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // the following "nice" code was replaced with code that significantly saves stack space (max. log(N) recursion nesting)
    //  if (left < j) SortArrayForFSInt(left, j);
    //  if (i < right) SortArrayForFSInt(i, right);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // need to sort both "halves", so we send the smaller one to recursion, process the other via "goto"
            {
                SortArrayForFSInt(left, j);
                left = i;
                goto LABEL_SortArrayForFSInt;
            }
            else
            {
                SortArrayForFSInt(i, right);
                right = j;
                goto LABEL_SortArrayForFSInt;
            }
        }
        else
        {
            right = j;
            goto LABEL_SortArrayForFSInt;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_SortArrayForFSInt;
        }
    }
}

BOOL CIconCache::GetIndex(const wchar_t* name, int& index, CPluginDataInterfaceEncapsulation* dataIface,
                          const CFileData* file)
{
    if (Count == 0 || dataIface != NULL && file == NULL) // verify validity of 'file''
    {
        if (dataIface != NULL && file == NULL)
            TRACE_EW(L"CIconCache::GetIndex(): 'file' may not be NULL when 'dataIface' is not NULL! item=" << name);
        index = 0;
        return FALSE;
    }

    if (dataIface != NULL) // this is pitFromPlugin: let the plugin compare items itself (it must be comparison
    {                      // with no ties between any two listing items)
        int l = 0, r = Count - 1, m;
        int res;
        while (1)
        {
            m = (l + r) / 2;
            const CFileData* fileM = At(m).GetFSFileData();
            if (fileM != NULL)
                res = dataIface->CompareFilesFromFS(fileM, file);
            else
            {
                TRACE_EW(L"CIconCache::GetIndex(): unexpected error: Icon Cache doesn't contain FSFileData "
                        L"for item: "
                        << At(m).NameAndData);
                index = 0;
                return FALSE; // error -> return maybe: not found, insert at beginning of array
            }
            if (res == 0) // found
            {
                index = m;
                return TRUE;
            }
            else if (res > 0)
            {
                if (l == r || l > m - 1) // not found
                {
                    index = m; // should be at this position
                    return FALSE;
                }
                r = m - 1;
            }
            else
            {
                if (l == r) // not found
                {
                    index = m + 1; // should be after this position
                    return FALSE;
                }
                l = m + 1;
            }
        }
    }
    else // classic search by name
    {
        int length = (int)(wcslen(name) * sizeof(wchar_t));
        int l = 0, r = Count - 1, m;
        int res;
        while (1)
        {
            m = (l + r) / 2;
            res = CompareDWORDS(At(m).NameAndData, name, length);
            if (res == 0) // found
            {
                index = m;
                return TRUE;
            }
            else if (res > 0)
            {
                if (l == r || l > m - 1) // not found
                {
                    index = m; // should be at this position
                    return FALSE;
                }
                r = m - 1;
            }
            else
            {
                if (l == r) // not found
                {
                    index = m + 1; // should be after this position
                    return FALSE;
                }
                l = m + 1;
            }
        }
    }
}

void CIconCache::Release()
{
    int i;
    for (i = 0; i < Count; i++)
    {
        CIconData* data = &At(i);
        if (data->NameAndData != NULL)
            free(data->NameAndData);
    }
    DestroyMembers();
    IconsCount = 0;

    // destruction of raw data from ThumbnailsCache
    for (i = 0; i < ThumbnailsCache.Count; i++)
    {
        CThumbnailData* data = &ThumbnailsCache[i];
        if (data->Bits != NULL)
            free(data->Bits); // allocated in CSalamanderThumbnailMaker::RenderToThumbnailData()
    }
    ThumbnailsCache.DestroyMembers();
}

void CIconCache::Destroy()
{
    // free allocated data and array itself
    Release();

    // destruction of image lists from IconsCache
    IconsCache.DestroyMembers();
}

void CIconCache::ColorsChanged()
{
    CALL_STACK_MESSAGE1("CIconCache::ColorsChanged()");
    // this function is called when colors or screen color depth change
    // the second case is not handled -- would need to reconstruct bitmaps
    // in image lists for current color depth
    COLORREF bkColor = GetCOLORREF(CurrentColors[ITEM_BK_NORMAL]);
    int i;
    for (i = 0; i < IconsCache.Count; i++)
    {
        CIconList* il = IconsCache[i];
        if (il != NULL)
            il->SetBkColor(bkColor);
    }

    // thumbnails need to be redrawn if background color changed, icons with transparent
    // parts will be drawn with new background color
    for (i = 0; i < Count; i++)
    {
        CIconData* icon = &At(i);
        if (icon->GetFlag() == 5 /* o.k. thumbnail */)
            icon->SetFlag(6 /* old thumbnail version */);
    }
}

int CIconCache::AllocIcon(CIconList** iconList, int* iconListIndex)
{
    SLOW_CALL_STACK_MESSAGE1("CIconCache::AllocIcon()");
    int cache = IconsCount / ICONS_IN_LIST; // cache
    int index = IconsCount % ICONS_IN_LIST; // index within this cache
    if (cache >= IconsCache.Count)
    {
        if (cache > IconsCache.Count)
        {
            TRACE_E("Unexpected situation in CIconCache::AllocIcon.");
            return -1;
        }

        int iconWidth = 16;
        if (IconSize == ICONSIZE_COUNT)
            TRACE_E("CIconCache::AllocIcon() IconSize == ICONSIZE_COUNT, you must call SetIconSize() first!");
        else
            iconWidth = IconSizes[IconSize];

        CIconList* il = new CIconList();
        if (il == NULL)
        {
            TRACE_E("Unable to create icon-list cache of icons.");
            return -1;
        }
        if (!il->Create(iconWidth, iconWidth, ICONS_IN_LIST))
        {
            TRACE_E("Unable to create icon-list cache of icons.");
            delete il;
            return -1;
        }
        il->SetBkColor(GetCOLORREF(CurrentColors[ITEM_BK_NORMAL]));

        IconsCache.Add(il);
        if (!IconsCache.IsGood())
        {
            delete il;
            IconsCache.ResetState();
            return -1;
        }
        if (iconList != NULL)
            *iconList = il;
    }
    else
    {
        if (iconList != NULL)
            *iconList = IconsCache[cache];
    }
    if (iconListIndex != NULL)
        *iconListIndex = index;
    return IconsCount++;
}

int CIconCache::AllocThumbnail()
{
    CALL_STACK_MESSAGE1("CIconCache::AllocThumbnail()");

    // structure for holding thumbnail
    CThumbnailData data;
    memset(&data, 0, sizeof(CThumbnailData));

    // add it to the list
    int index = ThumbnailsCache.Add(data);
    if (!ThumbnailsCache.IsGood())
    {
        ThumbnailsCache.ResetState();
        return -1;
    }

    // return index of added element
    return index;
}

BOOL CIconCache::GetThumbnail(int index, CThumbnailData** thumbnailData)
{
    CALL_STACK_MESSAGE2("CIconCache::GetThumbnail(%d, , )", index);
    if (index >= 0 && index < ThumbnailsCache.Count)
    {
        *thumbnailData = &ThumbnailsCache[index];
        return TRUE;
    }
    else
    {
        /*if (echo) */ TRACE_E("Incorrect call to CIconCache::GetThumbnail.");
        return FALSE;
    }
}

BOOL CIconCache::GetIcon(int iconIndex, CIconList** iconList, int* iconListIndex)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("CIconCache::GetIcon(%d, , )", iconIndex);
    if (iconIndex >= 0 && iconIndex < IconsCount)
    {
        int cache = iconIndex / ICONS_IN_LIST; // cache
        int index = iconIndex % ICONS_IN_LIST; // index within cache
        if (cache < IconsCache.Count)
        {
            *iconList = IconsCache[cache];
            *iconListIndex = index;
            return TRUE;
        }
        else
        {
            /*if (echo) */ TRACE_E("Unexpected situation in CIconCache::GetIcon.");
            return FALSE;
        }
    }
    else
    {
        /*if (echo) */ TRACE_E("Incorrect call to CIconCache::GetIcon.");
        return FALSE;
    }
}

void CIconCache::GetIconsAndThumbsFrom(CIconCache* icons, CPluginDataInterfaceEncapsulation* dataIface,
                                       BOOL transferIconsAndThumbnailsAsNew, BOOL forceReloadThumbnails)
{
    CALL_STACK_MESSAGE1("CIconCache::GetIconsAndThumbsFrom()");
    int index1 = 0;
    int index2 = 0;

    if (dataIface != NULL) // this is pitFromPlugin: let the plugin compare items itself (it must be comparison
    {                      // with no ties between any two listing items)
        const CFileData *file1, *file2;

        if (index1 < Count)
        {
            file1 = At(index1).GetFSFileData();
            if (file1 == NULL)
            {
                TRACE_EW(L"CIconCache::GetIconsAndThumbsFrom(): unexpected error: Icon Cache doesn't contain FSFileData "
                        L"for item: "
                        << At(index1).NameAndData);
                return;
            }
        }
        else
            return; // nothing to merge

        if (index2 < icons->Count)
        {
            file2 = icons->At(index2).GetFSFileData();
            if (file2 == NULL)
            {
                TRACE_EW(L"CIconCache::GetIconsAndThumbsFrom(): unexpected error: Icon Cache doesn't contain FSFileData "
                        L"for item: "
                        << At(index2).NameAndData);
                return;
            }
        }
        else
            return; // nothing to merge
        int res;
        while (1)
        {
            res = dataIface->CompareFilesFromFS(file1, file2);
            if (res == 0) // match -> perform copy (icon and mask)
            {
                CIconList* srcIconList;
                int srcIconListIndex;

                CIconList* dstIconList;
                int dstIconListIndex;

                DWORD flag = icons->At(index2).GetFlag();

                if ((flag == 1 || flag == 2) &&  // valid or old icon
                    At(index1).GetFlag() == 0 && // we need an icon (if switched to thumbnail, we don't need the old icon)
                    GetIcon(At(index1).GetIndex(), &dstIconList, &dstIconListIndex) &&
                    icons->GetIcon(icons->At(index2).GetIndex(), &srcIconList, &srcIconListIndex))
                {
                    dstIconList->Copy(dstIconListIndex, srcIconList, srcIconListIndex);
                    At(index1).SetFlag((flag == 1 && transferIconsAndThumbnailsAsNew) ? 1 : 2); // now it's an old (or valid/new) icon version
                }
            }

            if (res == 0 || res < 0) // index1++
            {
                if (++index1 < Count)
                {
                    file1 = At(index1).GetFSFileData();
                    if (file1 == NULL)
                    {
                        TRACE_EW(L"CIconCache::GetIconsAndThumbsFrom(): unexpected error: Icon Cache doesn't contain FSFileData "
                                L"for item: "
                                << At(index1).NameAndData);
                        return;
                    }
                }
                else
                    break; // nothing more to merge
            }

            if (res == 0 || res > 0) // index2++
            {
                if (++index2 < icons->Count)
                {
                    file2 = icons->At(index2).GetFSFileData();
                    if (file2 == NULL)
                    {
                        TRACE_EW(L"CIconCache::GetIconsAndThumbsFrom(): unexpected error: Icon Cache doesn't contain FSFileData "
                                L"for item: "
                                << At(index2).NameAndData);
                        return;
                    }
                }
                else
                    break; // nothing more to merge
            }
        }
    }
    else
    {
        int length;
        wchar_t *name1, *name2;

        if (index1 < Count)
        {
            name1 = At(index1).NameAndData;
            length = (int)(wcslen(name1) * sizeof(wchar_t));
        }
        else
            return; // nothing to merge

        if (index2 < icons->Count)
            name2 = icons->At(index2).NameAndData;
        else
            return; // nothing to merge
        int res;
        while (1)
        {
            res = CompareDWORDS(name1, name2, length);
            if (res == 0) // match -> perform copy (icon and mask) || thumbnail
            {
                CIconList* srcIconList;
                int srcIconListIndex;

                CIconList* dstIconList;
                int dstIconListIndex;

                DWORD flag = icons->At(index2).GetFlag();

                if ((flag == 1 || flag == 2) &&  // valid or old icon
                    At(index1).GetFlag() == 0 && // we need an icon (if switched to thumbnail, we don't need the old icon)
                    GetIcon(At(index1).GetIndex(), &dstIconList, &dstIconListIndex) &&
                    icons->GetIcon(icons->At(index2).GetIndex(), &srcIconList, &srcIconListIndex))
                {
                    dstIconList->Copy(dstIconListIndex, srcIconList, srcIconListIndex);
                    At(index1).SetFlag((flag == 1 && transferIconsAndThumbnailsAsNew) ? 1 : 2); // now it's an old (or valid/new) icon version
                }
                else
                {
                    CThumbnailData* srcThumbnailData;
                    CThumbnailData* tgtThumbnailData;

                    if ((flag == 5 || flag == 6) &&  // valid or old thumbnail
                        At(index1).GetFlag() == 4 && // we need thumbnail (if switched to icon, we don't need the old thumbnail)
                        GetThumbnail(At(index1).GetIndex(), &tgtThumbnailData) &&
                        icons->GetThumbnail(icons->At(index2).GetIndex(), &srcThumbnailData))
                    {
                        // old thumbnail doesn't need to be copied -- just pass its
                        // geometry and raw data to the target thumbnail
                        *tgtThumbnailData = *srcThumbnailData; // pass old data to new cache
                        srcThumbnailData->Bits = NULL;         // old cache is destroyed and data must not be deallocated

                        int newFlag = 6;
                        // if copying a valid thumbnail, we check the file stamp (size+date), possibly
                        // mark the copied thumbnail as valid right away (threat of file change without change
                        // in size+date is negligible and speed gain is huge)
                        if (flag == 5 && !forceReloadThumbnails)
                        {
                            if (transferIconsAndThumbnailsAsNew)
                                newFlag = 5;
                            else
                            {
                                int offset = length + 4;
                                offset -= (offset & 0x3); // offset % 4  (zarovnani po ctyrech bytech)
                                // name1/name2 are wchar_t* and 'offset' is a BYTE offset
                                // (length is wcslen()*sizeof(wchar_t)) - adding it directly to a
                                // wchar_t* silently doubles the advance (pointer arithmetic scales by
                                // sizeof(wchar_t)), reading past the CQuadWord/FILETIME tag and into
                                // whatever follows it (out-of-bounds for short names). Offset through
                                // an explicit BYTE pointer instead, matching how this packed record is
                                // built (files_window_directory_read.cpp's 'raw' BYTE* writer).
                                BYTE* raw1 = (BYTE*)name1;
                                BYTE* raw2 = (BYTE*)name2;
                                if (*(CQuadWord*)(raw1 + offset) == *(CQuadWord*)(raw2 + offset) &&
                                    CompareFileTime((FILETIME*)(raw1 + offset + sizeof(CQuadWord)),
                                                    (FILETIME*)(raw2 + offset + sizeof(CQuadWord))) == 0)
                                {
                                    newFlag = 5;
                                }
                            }
                        }
                        At(index1).SetFlag(newFlag); // now it's an old (or valid/new) thumbnail version
                    }
                }
            }

            if (res == 0 || res < 0) // index1++
            {
                if (++index1 < Count)
                {
                    name1 = At(index1).NameAndData;
                    length = (int)(wcslen(name1) * sizeof(wchar_t));
                }
                else
                    break; // nothing more to merge
            }

            if (res == 0 || res > 0) // index2++
            {
                if (++index2 < icons->Count)
                    name2 = icons->At(index2).NameAndData;
                else
                    break; // nothing more to merge
            }
        }
    }
}

void CIconCache::SetIconSize(CIconSizeEnum iconSize)
{
    if (iconSize == ICONSIZE_COUNT)
    {
        TRACE_E("CIconCache::SetIconSize() unexpected iconSize==ICONSIZE_COUNT");
        return;
    }
    if (iconSize == IconSize) // if size doesn't change, nothing to do
        return;

    // discard current icons
    int i;
    for (i = 0; i < Count; i++)
    {
        CIconData* data = &At(i);
        data->SetFlag(0);
        data->SetIndex(-1);
    }
    IconsCache.DestroyMembers();
    IconsCount = 0;

    IconSize = iconSize;
}

//
// ****************************************************************************
// CAssociations
//

// SEH wrapper: shell calls may crash due to buggy shell extensions.
// Kept in a separate function because SEH prevents C++ object unwinding.
static BOOL ReadDirectoryIconAndTypeSEH(const wchar_t* systemDir, CIconList* iconList, int index, CIconSizeEnum iconSize)
{
    SHFILEINFOW shi;
    HICON hIcon;
    __try
    {
        if (GetFileIcon(systemDir, &hIcon, iconSize, TRUE, TRUE))
        {
            iconList->ReplaceIcon(index, hIcon);
            NOHANDLES(DestroyIcon(hIcon));
        }
        if (SHGetFileInfoW(systemDir, 0, &shi, sizeof(shi), SHGFI_TYPENAME) != 0)
        {
            lstrcpynW(FolderTypeName, shi.szTypeName, _countof(FolderTypeName));
            FolderTypeNameLen = (int)wcslen(FolderTypeName);
        }
        return TRUE;
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 10))
    {
        FGIExceptionHasOccured++;
    }
    return FALSE;
}

BOOL ReadDirectoryIconAndTypeAux(CIconList* iconList, int index, CIconSizeEnum iconSize)
{
    std::wstring systemDirW;
    if (!gEnvironment->GetSystemDirectory(systemDirW).success)
        return FALSE;
    return ReadDirectoryIconAndTypeSEH(systemDirW.c_str(), iconList, index, iconSize);
}

static RegistryResult ReadAssociationString(HKEY key, const wchar_t* valueName,
                                            std::wstring& value, RegValueType* valueType = NULL)
{
    RegValueType type = RegValueType::None;
    std::vector<uint8_t> bytes;
    RegistryResult result = gRegistry->GetValue(key, valueName, type, bytes);
    if (!result.success)
        return result;
    if (type != RegValueType::String && type != RegValueType::ExpandString)
        return RegistryResult::Error(ERROR_INVALID_DATATYPE);
    if (bytes.size() % sizeof(wchar_t) != 0)
        return RegistryResult::Error(ERROR_INVALID_DATA);

    const wchar_t* text = reinterpret_cast<const wchar_t*>(bytes.data());
    size_t chars = bytes.size() / sizeof(wchar_t);
    while (chars > 0 && text[chars - 1] == L'\0')
        --chars;
    value.assign(text, chars);
    if (valueType != NULL)
        *valueType = type;
    return RegistryResult::Ok();
}

BOOL GetIconFromAssocAux(BOOL initFlagAndIndexes, HKEY root, const wchar_t* keyName,
                         CAssociationData& data, std::wstring& iconLocation, std::wstring* type)
{
    BOOL found = FALSE;
    if (initFlagAndIndexes)
    {
        data.SetFlag(0);
        data.SetIndexAll(-1);
    }
    iconLocation.clear();
    const std::wstring baseKey = keyName;
    HKEY openKey = NULL;

    if (type != NULL)
    {
        type->clear();

        // file-type string obtained as value "" of subkey keyName
        if (gRegistry->OpenKeyRead(root, baseKey.c_str(), openKey).success)
        {
            ReadAssociationString(openKey, NULL, *type);
            gRegistry->CloseKey(openKey);
        }
    }

    std::wstring keyNameBuf = baseKey + SAL_REG_SUBKEY_SHELL_W;
    if (gRegistry->OpenKeyRead(root, keyNameBuf.c_str(), openKey).success)
    {
        std::vector<std::wstring> commands;
        if (gRegistry->EnumSubKeys(openKey, commands).success && !commands.empty())
            data.SetFlag(1);
        gRegistry->CloseKey(openKey);
    }

    keyNameBuf = baseKey + SAL_REG_SUBKEY_SHELLEX_ICON_HANDLER_W;
    if (gRegistry->OpenKeyRead(root, keyNameBuf.c_str(), openKey).success)
    {
        // if contains "\\ShellEx\\IconHandler", must be extracted from file
        found = TRUE;
        gRegistry->CloseKey(openKey);
        data.SetIndexAll(-2);
    }

    keyNameBuf = baseKey + SAL_REG_SUBKEY_DEFAULT_ICON_W;
    if (!found && gRegistry->OpenKeyRead(root, keyNameBuf.c_str(), openKey).success)
    {
        std::wstring iconText;
        RegValueType iconType = RegValueType::None;
        if (ReadAssociationString(openKey, NULL, iconText, &iconType).success && !iconText.empty())
        {
            found = TRUE;

            if (iconType == RegValueType::ExpandString)
            {
                std::wstring expanded;
                if (gEnvironment->ExpandEnvironmentStrings(iconText.c_str(), expanded).success)
                    iconLocation = std::move(expanded);
                else
                {
                    TRACE_E("ExpandEnvironmentStrings failed.");
                    iconLocation = iconText;
                }
            }
            else
                iconLocation = iconText;

            // remove quotes in case "\"filename\",icon_number" (e.g. "\"C:\\Program Files\\VideoLAN\\VLC\\vlc.exe\",0")
            const size_t comma = iconLocation.rfind(L',');
            if (comma != std::wstring::npos)
            {
                size_t number = comma + 1;
                while (number < iconLocation.length() && iconLocation[number] == L' ')
                    ++number;
                if (number < iconLocation.length() && iconLocation[number] == L'-')
                    ++number;
                if (number < iconLocation.length() && iconLocation[number] == L'+')
                    ++number;
                const size_t digits = number;
                while (number < iconLocation.length() && iconLocation[number] >= L'0' && iconLocation[number] <= L'9')
                    ++number;
                if (digits < number && number == iconLocation.length() &&
                    iconLocation.front() == L'"' && comma > 1 && iconLocation[comma - 1] == L'"')
                {
                    iconLocation.erase(comma - 1, 1);
                    iconLocation.erase(0, 1);
                }
            }

            const wchar_t* s = iconText.c_str(); // distinguish type "%1" from "...%variable%..."
            while (*s != 0)
            {
                if (*s == L'%')
                {
                    s++;
                    if (*s != L'%')
                    {
                        while (*s != 0 && *s != L' ' && *s != L'%')
                            s++;
                        if (*s != '%') // not an env. variable -> dynamic type
                        {
                            data.SetIndexAll(-2);
                            break;
                        }
                    }
                }
                s++;
            }
        }
        gRegistry->CloseKey(openKey);
    }
    return found;
}

CAssociations::CAssociations()
    : TDirectArray<CAssociationData>(500, 300)
{
}

CAssociations::~CAssociations()
{
    Destroy();
}

void CAssociations::Release()
{
    int i;
    for (i = 0; i < Count; i++)
    {
        CAssociationData* data = &At(i);
        if (data->ExtensionAndData != NULL)
            free(data->ExtensionAndData);
        if (data->Type != NULL)
            free(data->Type);
    }
    DestroyMembers();
    for (i = 0; i < ICONSIZE_COUNT; i++)
        Icons[i].IconsCount = 0;
}

void CAssociations::Destroy()
{
    // free allocated data and array itself
    Release();

    // destruction of image lists from IconsCache
    int i;
    for (i = 0; i < ICONSIZE_COUNT; i++)
        Icons[i].IconsCache.DestroyMembers();
}

void CAssociations::ColorsChanged()
{
    CALL_STACK_MESSAGE1("CAssociations::ColorsChanged()");
    // this function is called when colors or screen color depth change
    // the second case is not handled -- would need to reconstruct bitmaps
    // in image lists for current color depth
    COLORREF bkColor = GetCOLORREF(CurrentColors[ITEM_BK_NORMAL]);
    int j;
    for (j = 0; j < ICONSIZE_COUNT; j++)
    {
        int i;
        for (i = 0; i < Icons[j].IconsCache.Count; i++)
        {
            CIconList* il = Icons[j].IconsCache[i];
            if (il != NULL)
                il->SetBkColor(bkColor);
        }
    }
    // FIXME: it would be enough to set background only for the relevant iconlist
    int i;
    for (i = 0; i < ICONSIZE_COUNT; i++)
        SimpleIconLists[i]->SetBkColor(bkColor);
}

BOOL CAssociations::GetIndex(const wchar_t* name, int& index)
{
    if (Count == 0)
    {
        index = 0;
        return FALSE;
    }

    int length = (int)(wcslen(name) * sizeof(wchar_t));
    int l = 0, r = Count - 1, m;
    int res;
    while (1)
    {
        m = (l + r) / 2;
        res = CompareDWORDS(At(m).ExtensionAndData, name, length);
        if (res == 0) // found
        {
            index = m;
            return TRUE;
        }
        else if (res > 0)
        {
            if (l == r || l > m - 1) // not found
            {
                index = m; // should be at this position
                return FALSE;
            }
            r = m - 1;
        }
        else
        {
            if (l == r) // not found
            {
                index = m + 1; // should be after this position
                return FALSE;
            }
            l = m + 1;
        }
    }
}

int CAssociations::AllocIcon(CIconList** iconList, int* iconListIndex, CIconSizeEnum iconSize)
{
    CALL_STACK_MESSAGE1("CAssociations::AllocIcon()");
    int cache = Icons[iconSize].IconsCount / ICONS_IN_LIST; // cache
    int index = Icons[iconSize].IconsCount % ICONS_IN_LIST; // index within this cache
    if (cache >= Icons[iconSize].IconsCache.Count)
    {
        if (cache > Icons[iconSize].IconsCache.Count)
        {
            TRACE_E("Unexpected situation in CAssociations::AllocIcon.");
            return -1;
        }

        int iconWidth = IconSizes[iconSize];

        CIconList* il = new CIconList();
        if (il == NULL)
        {
            TRACE_E("Unable to create icon-list cache of icons.");
            return -1;
        }
        if (!il->Create(iconWidth, iconWidth, ICONS_IN_LIST))
        {
            TRACE_E("Unable to create icon-list cache of icons.");
            delete il;
            return -1;
        }
        il->SetBkColor(GetCOLORREF(CurrentColors[ITEM_BK_NORMAL]));

        Icons[iconSize].IconsCache.Add(il);
        if (!Icons[iconSize].IconsCache.IsGood())
        {
            delete il;
            Icons[iconSize].IconsCache.ResetState();
            return -1;
        }
        if (iconList != NULL)
            *iconList = il;
    }
    else
    {
        if (iconList != NULL)
            *iconList = Icons[iconSize].IconsCache[cache];
    }
    if (iconListIndex != NULL)
        *iconListIndex = index;
    return Icons[iconSize].IconsCount++;
}

BOOL CAssociations::GetIcon(int iconIndex, CIconList** iconList, int* iconListIndex, CIconSizeEnum iconSize)
{
    CALL_STACK_MESSAGE2("CAssociations::GetIcon(%d, , )", iconIndex);
    if (iconIndex >= 0 && iconIndex < Icons[iconSize].IconsCount)
    {
        int cache = iconIndex / ICONS_IN_LIST; // cache
        int index = iconIndex % ICONS_IN_LIST; // index within cache
        if (cache < Icons[iconSize].IconsCache.Count)
        {
            *iconList = Icons[iconSize].IconsCache[cache];
            *iconListIndex = index;
            return TRUE;
        }
        else
        {
            TRACE_E("Unexpected situation in CAssociations::GetIcon.");
            return FALSE;
        }
    }
    else
    {
        TRACE_E("Incorrect call to CAssociations::GetIcon.");
        return FALSE;
    }
}

void CAssociations::SortArray(int left, int right)
{

LABEL_SortArray:

    int i = left, j = right;
    wchar_t* pivot = Data[(i + j) / 2].ExtensionAndData;
    int length = (int)(wcslen(pivot) * sizeof(wchar_t));

    do
    {
        while (CompareDWORDS(Data[i].ExtensionAndData, pivot, length) < 0 && i < right)
            i++;
        while (CompareDWORDS(pivot, Data[j].ExtensionAndData, length) < 0 && j > left)
            j--;

        if (i <= j)
        {
            CAssociationData swap = Data[i];
            Data[i] = Data[j];
            Data[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // the following "nice" code was replaced with code that significantly saves stack space (max. log(N) recursion nesting)
    //  if (left < j) SortArray(left, j);
    //  if (i < right) SortArray(i, right);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // need to sort both "halves", so we send the smaller one to recursion, process the other via "goto"
            {
                SortArray(left, j);
                left = i;
                goto LABEL_SortArray;
            }
            else
            {
                SortArray(i, right);
                right = j;
                goto LABEL_SortArray;
            }
        }
        else
        {
            right = j;
            goto LABEL_SortArray;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_SortArray;
        }
    }
}

void CAssociations::InsertData(const wchar_t* /*origin*/, int index, BOOL overwriteItem, wchar_t* e, wchar_t* s, CAssociationData& data, LONG& size,
                               const wchar_t* iconLocation, const wchar_t* type)
{
    //  TRACE_I(origin << (overwriteItem ? "overwriting existing record by: " : "") << "file association: ext=" << e <<
    //          ": index=" << data.GetIndex(ICONSIZE_16) << ": flag=" << data.GetFlag() << ": icon-location=" << iconLocation <<
    //          ": type=" << (type == NULL ? "" : type));

    // The signature now matches icncache.h:293, which was already wide - the
    // C2511 that mismatch produced is also what turned every At()/Insert() below into a C2352.
    //
    // `s - e` is a CHARACTER count, so the extension's byte span including its terminator is
    // (chars + 1) * sizeof(wchar_t). The narrow original wrote the same round-up-to-4 as
    // "+ 4 then mask off the low bits"; `(x + 3) & ~3` is that operation stated directly.
    // The 4-byte alignment is required: CompareDWORDS reads the key in DWORD steps and runs
    // PAST lengthBytes, which is also why the caller's *(DWORD*)s = 0 pad must stay.
    size = (LONG)(((s - e) + 1) * sizeof(wchar_t));
    size = (size + 3) & ~3;                                   // align to four bytes
    int iLen = (int)((wcslen(iconLocation) + 1) * sizeof(wchar_t));
    data.ExtensionAndData = (wchar_t*)malloc(size + iLen);
    memcpy(data.ExtensionAndData, e, size); // extension + zero padding +
    // size is a BYTE offset and ExtensionAndData is wchar_t*, so the old
    // `+ size` scaled it by two and wrote the icon-location an extension too far. size is a
    // multiple of 4, so the division is exact and keeps the maths in the pointer's own units.
    memcpy(data.ExtensionAndData + size / sizeof(wchar_t), iconLocation, iLen); // icon-location
    if (type[0] != 0)
        data.Type = DupStr(type); // error -> file-type just won't be shown
    else
        data.Type = NULL;
    if (overwriteItem)
    {
        if (At(index).ExtensionAndData != NULL)
            free(At(index).ExtensionAndData);
        if (At(index).Type != NULL)
            free(At(index).Type);
        At(index) = data;
    }
    else
        Insert(index, data);
}

void CAssociations::ReadAssociations(BOOL showWaitWnd)
{
    //---  show wait dialog + hourglass
    HCURSOR oldCur;
    HWND parent = (MainWindow != NULL) ? MainWindow->HWindow : NULL;
    // wait window was causing problems:
    // if I do Open With over a file and choose for example NOTEPAD, it opens
    // then a notification about association change SHCNE_ASSOCCHANGED is sent
    // as a result this function is called, which displays the window and pulls it up
    // with it pulls up all of Salamander, so I temporarily disable it
    CWaitWindow waitWnd(parent, IDS_READINGASSOCIATIONS, FALSE, ooStatic);
    BOOL closeDialog = FALSE;
    if (!ExistSplashScreen())
    {
        if (showWaitWnd)
            waitWnd.Create(); //j.r. for debugging shortcuts from desktop
        // LoadCursorW: the unsuffixed form is a UNICODE-flip lever and was the
        // SECOND textapi needle in this file (the first is :817's ExpandEnvironmentStrings).
        // ⚠ The (PCWSTR) cast is REQUIRED, not cosmetic: IDC_WAIT expands to MAKEINTRESOURCEA
        // without UNICODE, i.e. an LPSTR, so LoadCursorW rejects it outright. The id packs an
        // integer into a pointer, so the cast is width-neutral - this is the same idiom the
        // tree already uses for (PCWSTR)IDI_EXCLAMATION at find_dialog_actions.cpp:429.
        oldCur = SetCursor(LoadCursorW(NULL, (PCWSTR)IDC_WAIT));
        closeDialog = TRUE;
    }
    else
        IfExistSetSplashScreenText(LoadStrW(IDS_STARTUP_ASSOCIATIONS));
    //---  clear array + cache
    Release();
    //---  iterate through registry records about classes (extensions)
    std::vector<wchar_t> ext;
    wchar_t *s, *e;

    std::wstring iconLocation;
    std::wstring type;
    HKEY extKey = NULL, openKey = NULL;
    LONG size;
    CAssociationData data;

    HKEY systemFileAssoc = NULL;
    if (!gRegistry->OpenKeyRead(HKEY_CLASSES_ROOT, SAL_REG_KEY_SYSTEM_FILE_ASSOCIATIONS_W,
                                systemFileAssoc).success)
        systemFileAssoc = NULL;

    // Windows 2000 and newer also have "Open With..." associations stored for each user separately
    // in key HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts
    HKEY explorerFileExts = NULL;
    if (!gRegistry->OpenKeyRead(HKEY_CURRENT_USER, SAL_REG_KEY_EXPLORER_FILEEXTS_W,
                                explorerFileExts).success)
        explorerFileExts = NULL;

    auto prepareExtension = [&](const std::wstring& name)
    {
        const std::wstring folded = sally::text::Fold(name.substr(1));
        ext.assign(folded.begin(), folded.end());
        ext.resize(folded.size() + 2, L'\0'); // InsertData reads through DWORD alignment.
        e = ext.data();
        s = e + folded.size();
    };

    std::vector<std::wstring> classNames;
    RegistryResult enumResult = gRegistry->EnumSubKeys(HKEY_CLASSES_ROOT, classNames);
    if (!enumResult.success)
    {
        std::wstring msg = FormatStrW(LoadStrW(IDS_UNABLETOGETASSOC),
                                      GetErrorTextOwned(enumResult.errorCode).c_str());
        gPrompter->ShowError(LoadStrW(IDS_UNABLETOGETASSOCTITLE), msg.c_str());
    }
    else
    {
        for (const std::wstring& className : classNames)
        {
            if (!className.empty() && className[0] == L'.' &&
                gRegistry->OpenKeyRead(HKEY_CLASSES_ROOT, className.c_str(), extKey).success)
            {
                iconLocation.clear();
                data.SetFlag(0);
                data.SetIndexAll(-1);
                type.clear();
                BOOL tryPerceivedType = FALSE;
                std::wstring extType;
                BOOL addExt = ReadAssociationString(extKey, NULL, extType).success && !extType.empty();
                if (addExt)
                {
                    tryPerceivedType = !GetIconFromAssocAux(FALSE, HKEY_CLASSES_ROOT,
                                                            extType.c_str(), data, iconLocation, &type);
                }
                else
                    tryPerceivedType = TRUE;
                if (tryPerceivedType && systemFileAssoc != NULL)
                {
                    // first try to find 'ext' under the SystemFileAssociations key
                    if (GetIconFromAssocAux(FALSE, systemFileAssoc, className.c_str(),
                                            data, iconLocation, NULL))
                        addExt = TRUE;
                    else
                    { // also try the key from the PerceivedType value (if defined)
                        if (ReadAssociationString(extKey, SAL_REG_VALUE_PERCEIVED_TYPE_W,
                                                  extType).success && !extType.empty())
                        {
                            if (GetIconFromAssocAux(FALSE, systemFileAssoc, extType.c_str(),
                                                    data, iconLocation, NULL))
                                addExt = TRUE;
                        }
                    }
                }
                if (addExt)
                {
                    prepareExtension(className);
                    InsertData(L"", Count, FALSE, e, s, data, size, iconLocation.c_str(), type.c_str());
                }
                gRegistry->CloseKey(extKey);
            }
        }
    }
    if (Count > 1)
        SortArray(0, Count - 1);

    // Windows XP has associations (see PerceivedType) also stored in key HKEY_CLASSES_ROOT\SystemFileAssociations,
    // we load extensions not yet known from this key
    if (systemFileAssoc != NULL)
    {
        std::vector<std::wstring> systemNames;
        enumResult = gRegistry->EnumSubKeys(systemFileAssoc, systemNames);
        if (!enumResult.success)
        {
            std::wstring msg = FormatStrW(LoadStrW(IDS_UNABLETOGETASSOC),
                                          GetErrorTextOwned(enumResult.errorCode).c_str());
            gPrompter->ShowError(LoadStrW(IDS_UNABLETOGETASSOCTITLE), msg.c_str());
        }
        else
        {
            for (const std::wstring& systemName : systemNames)
            {
                if (!systemName.empty() && systemName[0] == L'.')
                {
                    prepareExtension(systemName);

                    int index;
                    if (!GetIndex(e, index)) // not found, makes sense to examine + possibly add
                    {
                        if (GetIconFromAssocAux(TRUE, systemFileAssoc, systemName.c_str(),
                                                data, iconLocation, NULL))
                        {
                            InsertData(L"SystemFileAssociations: ", index, FALSE, e, s, data, size, iconLocation.c_str(), L"");
                        }
                    }
                }
            }
        }
    }

    if (explorerFileExts != NULL)
    {
        std::vector<std::wstring> userExtensions;
        if (gRegistry->EnumSubKeys(explorerFileExts, userExtensions).success)
        {
            for (const std::wstring& userExtension : userExtensions)
            {
                if (!userExtension.empty() && userExtension[0] == L'.' &&
                    gRegistry->OpenKeyRead(explorerFileExts, userExtension.c_str(), extKey).success)
                {
                    prepareExtension(userExtension);

                    int index;
                    BOOL found = GetIndex(e, index);
                    if (WindowsVistaAndLater &&
                        gRegistry->OpenKeyRead(extKey, SAL_REG_SUBKEY_USER_CHOICE_W, openKey).success)
                    {                    // try if associated via UserChoice key, if so, it's the highest priority record, so we possibly overwrite the existing association
                        std::wstring extType;
                        if (ReadAssociationString(openKey, SAL_REG_VALUE_PROGID_W, extType).success &&
                            !extType.empty())
                        {
                            if (GetIconFromAssocAux(TRUE, HKEY_CLASSES_ROOT, extType.c_str(),
                                                    data, iconLocation, &type))
                            {
                                InsertData(L"UserChoice: ", index, found, e, s, data, size, iconLocation.c_str(), type.c_str()); // found==TRUE means overwrite found association with the one from UserChoice
                                found = TRUE;
                            }
                        }
                        gRegistry->CloseKey(openKey);
                    }
                    if (!found) // also try if associated via OpenWithProgids key
                    {
                        if (WindowsVistaAndLater &&
                            gRegistry->OpenKeyRead(extKey, SAL_REG_SUBKEY_OPEN_WITH_PROGIDS_W,
                                                   openKey).success)
                        {
                            std::vector<std::wstring> progIds;
                            if (gRegistry->EnumValues(openKey, progIds).success)
                            { // sequentially enumerate all association types
                                for (const std::wstring& extType : progIds)
                                {
                                    if (!extType.empty() &&
                                        GetIconFromAssocAux(TRUE, HKEY_CLASSES_ROOT,
                                                            extType.c_str(), data, iconLocation, &type))
                                    {
                                        InsertData(L"OpenWithProgids: ", index, FALSE, e, s, data, size, iconLocation.c_str(), type.c_str());
                                        found = TRUE;
                                        break;
                                    }
                                }
                            }
                            gRegistry->CloseKey(openKey);
                        }
                    }

                    if (gRegistry->ValueExists(extKey, SAL_REG_VALUE_APPLICATION_W))
                    {
                        if (found) // found, set that it has association
                        {
                            CAssociationData* iconData = &(At(index));
                            iconData->SetFlag(1); // files with this extension can be opened
                                                  // iconData->SetIndexAll(-1);  // switching to static icon causes problems with CDR and CPT Corel files with previews in icons, better leave icon in settings from HKEY_CLASSES_ROOT
                        }
                        else // not found, insert as static icon
                        {
                            data.SetFlag(1); // files with this extension can be opened
                            data.SetIndexAll(-1);

                            InsertData(L"FileExts: Application: ", index, FALSE, e, s, data, size, L"", L"");
                        }
                    }
                    gRegistry->CloseKey(extKey);
                }
            }
        }
        gRegistry->CloseKey(explorerFileExts);
    }

    // adding fixed icons of all sizes to cache-bitmap CAssociations
    int iconSize;
    for (iconSize = 0; iconSize < ICONSIZE_COUNT; iconSize++)
    {
        int resID, vistaResID;
        CIconList* iconList;
        int iconListIndex;
        int j;
        for (j = 0; j < 4; j++)
        {
            if (AllocIcon(&iconList, &iconListIndex, (CIconSizeEnum)iconSize) != -1)
            {
                switch (j)
                {
                case ASSOC_ICON_SOME_DIR:
                {
                    if (!ReadDirectoryIconAndTypeAux(iconList, iconListIndex, (CIconSizeEnum)iconSize))
                        TRACE_E("ReadDirectoryIconAndTypeAux() failed!");
                    continue;
                }

                case ASSOC_ICON_SOME_FILE:
                    resID = 2;
                    vistaResID = 90;
                    break;
                case ASSOC_ICON_SOME_EXE:
                    resID = 3;
                    vistaResID = 15;
                    break;
                default:
                    resID = 1;
                    vistaResID = 2;
                    break;
                }
                int iconWidth = IconSizes[iconSize];
                HICON smallIcon = SalLoadImage(vistaResID, resID, iconWidth, iconWidth, IconLRFlags);
                if (smallIcon != NULL)
                {
                    iconList->ReplaceIcon(iconListIndex, smallIcon);
                    HANDLES(DestroyIcon(smallIcon));
                }
            }
        }
        if (Icons[iconSize].IconsCount != ASSOC_ICON_COUNT)
            TRACE_E("ICON_COUNT and number of icons in cache are not the same!");
    }

    if (systemFileAssoc != NULL)
        gRegistry->CloseKey(systemFileAssoc);
    if (closeDialog)
    {
        SetCursor(oldCur);
        if (waitWnd.HWindow != NULL)
            DestroyWindow(waitWnd.HWindow);
    }
}

BOOL CAssociations::IsAssociated(const wchar_t* ext, BOOL& addtoIconCache, CIconSizeEnum iconSize)
{
    int index;
    if (GetIndex(ext, index))
    {
        int i = At(index).GetIndex(iconSize);
        if (i == -1)
            At(index).SetIndex(-3, iconSize);  // not loaded -> loading
        addtoIconCache = (i == -1 || i == -2); // dynamic or not loaded/loading static
        return At(index).GetFlag() != 0;
    }
    else
    {
        addtoIconCache = FALSE;
        return FALSE;
    }
}

BOOL CAssociations::IsAssociatedStatic(const wchar_t* ext, const wchar_t*& iconLocation, CIconSizeEnum iconSize)
{
    int index;
    if (GetIndex(ext, index))
    {
        int i = At(index).GetIndex(iconSize);
        if (i == -1)
        {
            At(index).SetIndex(-3, iconSize);          // not loaded -> loading
            iconLocation = At(index).ExtensionAndData; // not loaded/loading static
        }
        else
            iconLocation = NULL;
        return At(index).GetFlag() != 0;
    }
    else
    {
        iconLocation = NULL;
        return FALSE;
    }
}

BOOL CAssociations::IsAssociated(const wchar_t* ext)
{
    int index;
    if (GetIndex(ext, index))
        return At(index).GetFlag() != 0;
    else
        return FALSE;
}
