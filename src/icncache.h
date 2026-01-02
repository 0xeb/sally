// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

//
// ****************************************************************************
// CIconData
//

class CIconData
{
public:
    char* NameAndData;           // allocated by DWORD, ends null-terminated (for comparison);
                                 // for Flag==3 (and for ==1, if followed by ==3) additionally appended icon-location string;
                                 // for Flag==4,5,6 additionally appended file tag (CQuadWord Size + FILETIME LastWrite)
                                 //   and list of CPluginInterfaceForThumbLoaderEncapsulation interfaces
                                 //   of all plugins that can create thumbnail for file 'NameAndData', list is terminated
                                 //   with NULL)
    const CFileData* FSFileData; // pointer to CFileData file (only for FS with icon type pitFromPlugin), otherwise NULL

private:
    DWORD Index : 28;      // >= 0 index into icon cache or thumbnail cache (index must be < 134217728); -1 -> not loaded;
                           //   for Flag==0,1,2,3 it's an index into icon cache;
                           //   for Flag==4,5,6 it's an index into thumbnail cache
    DWORD ReadingDone : 1; // 1 = we have already tried to load (even if unsuccessful), 0 = we haven't tried yet
    DWORD Flag : 3;        // flag for this type, in CIconCache:
                           //   icons: 0 - not loaded, 1 - ok, 2 - old version, 3 - icon specified via icon-location
                           //   thumbnails: 4 - not loaded, 5 - ok, 6 - old version (or poor quality/smaller)

public:
    int GetIndex()
    {
        int index = Index;
        if (index & 0x08000000)
            index |= 0xF0000000; // cannot convert 28-bit int to 32-bit int ...
        return index;
    }

    int SetIndex(int index)
    {
        return Index = index;
    }

    DWORD GetFlag() { return Flag; }
    DWORD SetFlag(DWORD f) { return Flag = f; }

    DWORD GetReadingDone() { return ReadingDone; }
    DWORD SetReadingDone(DWORD r) { return ReadingDone = r; }

    const CFileData* GetFSFileData() { return FSFileData; }
};

//
// ****************************************************************************
// CThumbnailData
//

//
// represents one thumbnail in CIconCache::ThumbnailsCache
// because with a large number of bitmap handles, the process slows down,
// it's better to keep bitmaps as RAW data
//
struct CThumbnailData
{
    WORD Width; // thumbnail dimensions
    WORD Height;
    WORD Planes;       // determine "geometry" of data (we could omit these two parameters,
    WORD BitsPerPixel; // but it would risk issues when switching color depth)
    DWORD* Bits;       // raw data device dependent bitmap; format unknown
};

//
// ****************************************************************************
// CIconCache
//

class CIconCache : public TDirectArray<CIconData>
{
protected:
    //
    // Icons
    //
    TIndirectArray<CIconList> IconsCache; // array of bitmaps serving as cache for icons
    int IconsCount;                       // number of filled slots in bitmaps (icons)
    CIconSizeEnum IconSize;               // what icon size do we hold?

    //
    // Thumbnails
    //
    TDirectArray<CThumbnailData> ThumbnailsCache; // array of bitmaps serving as cache for thumbnails

    CPluginDataInterfaceEncapsulation* DataIfaceForFS; // for internal use only in SortArray()

public:
    // 'forAssociations' is used to size the array (base/delta); for associations larger size is expected
    CIconCache();
    ~CIconCache();

    void Release(); // release entire array + invalidate cache
    void Destroy(); // release entire array + cache

    // sorts array for fast searching; 'dataIface' is NULL except when it's
    // ptPluginFS with icons of type pitFromPlugin
    void SortArray(int left, int right, CPluginDataInterfaceEncapsulation* dataIface);

    // returns "found?" and index of item or where to insert (sorted array);
    // 'name' must be aligned by DWORD (used only if 'dataIface' is NULL);
    // 'file' is file-data of file/directory 'name' (used only if 'dataIface'
    // is not NULL); 'dataIface' is NULL except when it's ptPluginFS with icons of type
    // pitFromPlugin
    BOOL GetIndex(const char* name, int& index, CPluginDataInterfaceEncapsulation* dataIface,
                  const CFileData* file);

    // copies known icons and thumbnails (old and new cache must be sorted!)
    // in case of thumbnail passes geometry and raw image data (CThumbnailData::Bits)
    // to new cache; in old one sets Bits=NULL to prevent deallocation on destruction;
    // 'dataIface' is NULL except when both old and new cache are ptPluginFS
    // with icons of type pitFromPlugin
    void GetIconsAndThumbsFrom(CIconCache* icons, CPluginDataInterfaceEncapsulation* dataIface,
                               BOOL transferIconsAndThumbnailsAsNew = FALSE,
                               BOOL forceReloadThumbnails = FALSE);

    // must redraw basic icon set with new background
    void ColorsChanged();

    ////////////////
    //
    // Icons methods
    //

    // allocates space for icon; returns its index or -1 on error
    // variables 'iconList' and 'iconListIndex' can be NULL (then they are not set)
    // otherwise 'iconList' returns pointer to CIconList that holds the icon and 'iconListIndex'
    // is the index within this imagelist.
    int AllocIcon(CIconList** iconList, int* imageIconIndex);

    // returns in 'iconList' pointer to IconList and in 'iconListIndex' position of icon
    // 'iconIndex' (returned from AllocIcon);
    BOOL GetIcon(int iconIndex, CIconList** iconList, int* iconListIndex);

    ////////////////
    //
    // Thumbnails methods
    //

    // allocates space for thumbnail at the end of ThumbnailsCache array
    // if everything is OK, returns the index corresponding to the thumbnail
    // on error returns -1
    int AllocThumbnail();

    // returns in 'thumbnailData' pointer to item
    // 'index' (returned from AllocThumbnail);
    BOOL GetThumbnail(int index, CThumbnailData** thumbnailData);

    void SetIconSize(CIconSizeEnum iconSize);
    CIconSizeEnum GetIconSize() { return IconSize; }

protected:
    // for internal use only
    void SortArrayInt(int left, int right);
    // for internal use only
    void SortArrayForFSInt(int left, int right);
};

//
// ****************************************************************************
// CAssociationData
//

struct CAssociationIndexAndFlag
{
    DWORD Index : 31; // >= 0 index; -1 not loaded; -2 dynamic (icon in file); -3 loaded (-1 -> -3)
    DWORD Flag : 1;   // can *.ExtensionAndData be opened?
};

class CAssociationData
{
public:
    char* ExtensionAndData; // allocated by DWORD, ends null-terminated (for comparison);
                            // extension + additionally appended icon-location string;
    char* Type;             // file-type string; instead of "" is NULL (to save memory)

private:
    // for each icon size we need a pair of Index+Flag
    CAssociationIndexAndFlag IndexAndFlag[ICONSIZE_COUNT];

public:
    int GetIndex(CIconSizeEnum iconSize)
    {
        if (iconSize >= ICONSIZE_COUNT)
        {
            TRACE_E("CAssociationData::GetIndex() unexpected iconSize=" << iconSize);
            iconSize = ICONSIZE_16;
        }
        DWORD index = IndexAndFlag[iconSize].Index;
        if (index & 0x40000000)
            index |= 0x80000000; // cannot convert 31-bit int to 32-bit int ...
        return index;
    }

    int SetIndex(int index, CIconSizeEnum iconSize)
    {
        if (iconSize >= ICONSIZE_COUNT)
        {
            TRACE_E("CAssociationData::SetIndex() unexpected iconSize=" << iconSize);
            iconSize = ICONSIZE_16;
        }
        return IndexAndFlag[iconSize].Index = index;
    }

    int SetIndexAll(int index)
    {
        int i;
        for (i = 0; i < ICONSIZE_COUNT; i++)
            IndexAndFlag[i].Index = index;
        return index;
    }

    DWORD GetFlag() { return IndexAndFlag[0].Flag; }
    DWORD SetFlag(DWORD f) { return IndexAndFlag[0].Flag = f; }
};

//
// ****************************************************************************
// CAssociations
//

#define ASSOC_ICON_NO_ASSOC 0 // fixed icons in cache-bitmap CAssociations
#define ASSOC_ICON_SOME_FILE 1
#define ASSOC_ICON_SOME_EXE 2
#define ASSOC_ICON_SOME_DIR 3
#define ASSOC_ICON_COUNT 4

struct CAssociationsIcons
{
public:
    TIndirectArray<CIconList> IconsCache; // array of bitmaps serving as cache for icons
    int IconsCount;                       // number of filled slots in bitmaps (icons)

public:
    CAssociationsIcons() : IconsCache(10, 5)
    {
        IconsCount = 0;
    }
};

class CAssociations : public TDirectArray<CAssociationData>
{
protected:
    CAssociationsIcons Icons[ICONSIZE_COUNT];

public:
    CAssociations();
    ~CAssociations();

    void Release(); // release entire array + invalidate cache
    void Destroy(); // release entire array + cache

    // all -3 -> -1
    //    void SetAllReadingToUnread();

    // sorts array for fast searching
    void SortArray(int left, int right);

    // returns "found?" and index of item or where to insert (sorted array);
    // 'name' must be aligned by DWORD ;
    BOOL GetIndex(const char* name, int& index);

    // allocates space for icon; returns its index or -1 on error
    // variables 'iconList' and 'iconListIndex' can be NULL (then they are not set)
    // otherwise 'iconList' returns pointer to CIconList that holds the icon and 'iconListIndex'
    // is the index within this imagelist.
    int AllocIcon(CIconList** iconList, int* imageIconIndex, CIconSizeEnum iconSize);

    // returns in 'iconList' pointer to IconList and in 'iconListIndex' position of icon
    // 'iconIndex' (returned from AllocIcon);
    BOOL GetIcon(int iconIndex, CIconList** iconList, int* iconListIndex, CIconSizeEnum iconSize);

    // must redraw basic icon set with new background
    void ColorsChanged();

    void ReadAssociations(BOOL showWaitWnd);

    // ext must be aligned by DWORD
    BOOL IsAssociated(char* ext, BOOL& addtoIconCache, CIconSizeEnum iconSize);
    BOOL IsAssociatedStatic(char* ext, const char*& iconLocation, CIconSizeEnum iconSize);
    BOOL IsAssociated(char* ext);

protected:
    // helper method
    void InsertData(const char* origin, int index, BOOL overwriteItem, char* e, char* s,
                    CAssociationData& data, LONG& size, const char* iconLocation, const char* type);
};

extern CAssociations Associations; // here are stored the loaded associations
