// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

//****************************************************************************
//
// Copyright (c) 2023 Open Salamander Authors
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#pragma once

#ifdef _MSC_VER
#pragma pack(push, enter_include_spl_com) // to make structures independent of the configured alignment
#pragma pack(4)
#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression
#endif                    // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

// in the plugin you need to define variable SalamanderVersion (int) and initialize it
// in SalamanderPluginEntry:
// SalamanderVersion = salamander->GetVersion();

// global variable with the version of Salamander in which this plugin is loaded
extern int SalamanderVersion;

//
// ****************************************************************************
// CSalamanderDirectoryAbstract
//
// class represents a directory structure - files and directories on requested paths, root path is "",
// path separators are backslashes ('\\')
//

// CQuadWord - 64-bit unsigned integer for file sizes
// tips:
//  -faster passing of input parameter of type CQuadWord: const CQuadWord &
//  -assign 64-bit integer: quadWord.Value = XXX;
//  -calculate size ratio: quadWord1.GetDouble() / quadWord2.GetDouble()  // precision loss before division is minimal (max. 1e-15)
//  -truncate to DWORD: (DWORD)quadWord.Value
//  -convert (unsigned) __int64 to CQuadWord: CQuadWord().SetUI64(XXX)

struct CQuadWord
{
    union
    {
        struct
        {
            DWORD LoDWord;
            DWORD HiDWord;
        };
        unsigned __int64 Value;
    };

    // WARNING: assignment operator or constructor for single DWORD must not be added here,
    //          otherwise usage of 8-byte numbers will be completely uncontrollable (C++ will
    //          mutually convert everything, which may not always be desired)

    CQuadWord() {}
    CQuadWord(DWORD lo, DWORD hi)
    {
        LoDWord = lo;
        HiDWord = hi;
    }
    CQuadWord(const CQuadWord& qw)
    {
        LoDWord = qw.LoDWord;
        HiDWord = qw.HiDWord;
    }

    CQuadWord& Set(DWORD lo, DWORD hi)
    {
        LoDWord = lo;
        HiDWord = hi;
        return *this;
    }
    CQuadWord& SetUI64(unsigned __int64 val)
    {
        Value = val;
        return *this;
    }
    CQuadWord& SetDouble(double val)
    {
        Value = (unsigned __int64)val;
        return *this;
    }

    CQuadWord& operator++()
    {
        ++Value;
        return *this;
    } // prefix ++
    CQuadWord& operator--()
    {
        --Value;
        return *this;
    } // prefix --

    CQuadWord operator+(const CQuadWord& qw) const
    {
        CQuadWord qwr;
        qwr.Value = Value + qw.Value;
        return qwr;
    }
    CQuadWord operator-(const CQuadWord& qw) const
    {
        CQuadWord qwr;
        qwr.Value = Value - qw.Value;
        return qwr;
    }
    CQuadWord operator*(const CQuadWord& qw) const
    {
        CQuadWord qwr;
        qwr.Value = Value * qw.Value;
        return qwr;
    }
    CQuadWord operator/(const CQuadWord& qw) const
    {
        CQuadWord qwr;
        qwr.Value = Value / qw.Value;
        return qwr;
    }
    CQuadWord operator%(const CQuadWord& qw) const
    {
        CQuadWord qwr;
        qwr.Value = Value % qw.Value;
        return qwr;
    }
    CQuadWord operator<<(const int num) const
    {
        CQuadWord qwr;
        qwr.Value = Value << num;
        return qwr;
    }
    CQuadWord operator>>(const int num) const
    {
        CQuadWord qwr;
        qwr.Value = Value >> num;
        return qwr;
    }

    CQuadWord& operator+=(const CQuadWord& qw)
    {
        Value += qw.Value;
        return *this;
    }
    CQuadWord& operator-=(const CQuadWord& qw)
    {
        Value -= qw.Value;
        return *this;
    }
    CQuadWord& operator*=(const CQuadWord& qw)
    {
        Value *= qw.Value;
        return *this;
    }
    CQuadWord& operator/=(const CQuadWord& qw)
    {
        Value /= qw.Value;
        return *this;
    }
    CQuadWord& operator%=(const CQuadWord& qw)
    {
        Value %= qw.Value;
        return *this;
    }
    CQuadWord& operator<<=(const int num)
    {
        Value <<= num;
        return *this;
    }
    CQuadWord& operator>>=(const int num)
    {
        Value >>= num;
        return *this;
    }

    BOOL operator==(const CQuadWord& qw) const { return Value == qw.Value; }
    BOOL operator!=(const CQuadWord& qw) const { return Value != qw.Value; }
    BOOL operator<(const CQuadWord& qw) const { return Value < qw.Value; }
    BOOL operator>(const CQuadWord& qw) const { return Value > qw.Value; }
    BOOL operator<=(const CQuadWord& qw) const { return Value <= qw.Value; }
    BOOL operator>=(const CQuadWord& qw) const { return Value >= qw.Value; }

    // convert to double (beware of precision loss for large numbers - double has only 15 significant digits)
    double GetDouble() const
    { // MSVC cannot convert unsigned __int64 to double, so we have to help ourselves
        if (Value < CQuadWord(0, 0x80000000).Value)
            return (double)(__int64)Value; // positive number
        else
            return 9223372036854775808.0 + (double)(__int64)(Value - CQuadWord(0, 0x80000000).Value);
    }
};

#define QW_MAX CQuadWord(0xFFFFFFFF, 0xFFFFFFFF)

#define ICONOVERLAYINDEX_NOTUSED 15 // value for CFileData::IconOverlayIndex when the icon has no overlay

// record of each file and directory in Salamander (basic data about file/directory)
struct CFileData // destructor must not be added here!
{
    char* Name;                    // allocated file name (without path), must be allocated on Salamander's
                                   // heap (see CSalamanderGeneralAbstract::Alloc/Realloc/Free)
    char* Ext;                     // pointer into Name after the first dot from the right (including dot at the beginning of name,
                                   // on Windows it is considered as extension, unlike on UNIX) or to the end of
                                   // Name if extension does not exist; if FALSE is set in configuration
                                   // for SALCFG_SORTBYEXTDIRSASFILES, Ext for directories points to the end of
                                   // Name (directories have no extensions)
    CQuadWord Size;                // file size in bytes
    DWORD Attr;                    // file attributes - ORed constants FILE_ATTRIBUTE_XXX
    FILETIME LastWrite;            // last write time to file (UTC-based time)
    char* DosName;                 // allocated DOS 8.3 file name, NULL if not needed, must be
                                   // allocated on Salamander's heap (see CSalamanderGeneralAbstract::Alloc/Realloc/Free)
    DWORD_PTR PluginData;          // used by plugin through CPluginDataInterfaceAbstract, Salamander ignores it
    unsigned NameLen : 9;          // length of Name string (strlen(Name)) - WARNING: maximum name length is (MAX_PATH - 5)
    unsigned Hidden : 1;           // is hidden? (if 1, icon is 50% more transparent - ghosted)
    unsigned IsLink : 1;           // is link? (if 1, icon has link overlay) - standard filling see CSalamanderGeneralAbstract::IsFileLink(CFileData::Ext), takes precedence over IsOffline when displayed, but IconOverlayIndex takes precedence
    unsigned IsOffline : 1;        // is offline? (if 1, icon has offline overlay - black clock), both IsLink and IconOverlayIndex take precedence when displayed
    unsigned IconOverlayIndex : 4; // icon-overlay index (if icon has no overlay, value is ICONOVERLAYINDEX_NOTUSED), takes precedence over IsLink and IsOffline when displayed

    // flags for internal use in Salamander: cleared when added to CSalamanderDirectoryAbstract
    unsigned Association : 1;     // meaning only for 'simple icons' display - icon of associated file, otherwise 0
    unsigned Selected : 1;        // read-only selection flag (0 - item not selected, 1 - item selected)
    unsigned Shared : 1;          // is directory shared? not used for files
    unsigned Archive : 1;         // is it an archive? used for displaying archive icon in panel
    unsigned SizeValid : 1;       // is the directory size calculated?
    unsigned Dirty : 1;           // does this item need to be redrawn? (temporary validity only; message queue must not be pumped between setting the bit and redrawing the panel, otherwise icon redraw (icon reader) may reset the bit! consequently the item won't be redrawn)
    unsigned CutToClip : 1;       // is CUT to clipboard? (if 1, icon is 50% more transparent - ghosted)
    unsigned IconOverlayDone : 1; // only for icon-reader-thread needs: are we getting or have we already gotten icon-overlay? (0 - no, 1 - yes)
};

// constants determining validity of data that is directly stored in CFileData (size, extension, etc.)
// or is generated from directly stored data automatically (file-type is generated from extension);
// Name + NameLen are mandatory (must always be valid); plugin manages PluginData validity itself
// (Salamander ignores this attribute)
#define VALID_DATA_EXTENSION 0x0001   // extension is stored in Ext (without: all Ext = end of Name)
#define VALID_DATA_DOSNAME 0x0002     // DOS name is stored in DosName (without: all DosName = NULL)
#define VALID_DATA_SIZE 0x0004        // size in bytes is stored in Size (without: all Size = 0)
#define VALID_DATA_TYPE 0x0008        // file-type can be generated from Ext (without: not generated)
#define VALID_DATA_DATE 0x0010        // modification date (UTC-based) is stored in LastWrite (without: all dates in LastWrite are 1.1.1602 in local time)
#define VALID_DATA_TIME 0x0020        // modification time (UTC-based) is stored in LastWrite (without: all times in LastWrite are 0:00:00 in local time)
#define VALID_DATA_ATTRIBUTES 0x0040  // attributes are stored in Attr (ORed Win32 API constants FILE_ATTRIBUTE_XXX) (without: all Attr = 0)
#define VALID_DATA_HIDDEN 0x0080      // "ghosted" icon flag is stored in Hidden (without: all Hidden = 0)
#define VALID_DATA_ISLINK 0x0100      // IsLink contains 1 if it's a link, icon has link overlay (without: all IsLink = 0)
#define VALID_DATA_ISOFFLINE 0x0200   // IsOffline contains 1 if it's an offline file/directory, icon has offline overlay (without: all IsOffline = 0)
#define VALID_DATA_PL_SIZE 0x0400     // makes sense only without using VALID_DATA_SIZE: plugin has size in bytes stored for at least some files/directories (somewhere in PluginData), to get this size Salamander calls CPluginDataInterfaceAbstract::GetByteSize()
#define VALID_DATA_PL_DATE 0x0800     // makes sense only without using VALID_DATA_DATE: plugin has modification date stored for at least some files/directories (somewhere in PluginData), to get this date Salamander calls CPluginDataInterfaceAbstract::GetLastWriteDate()
#define VALID_DATA_PL_TIME 0x1000     // makes sense only without using VALID_DATA_TIME: plugin has modification time stored for at least some files/directories (somewhere in PluginData), to get this time Salamander calls CPluginDataInterfaceAbstract::GetLastWriteTime()
#define VALID_DATA_ICONOVERLAY 0x2000 // IconOverlayIndex is the icon-overlay index (no overlay = value ICONOVERLAYINDEX_NOTUSED) (without: all IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED), icon specification see CSalamanderGeneralAbstract::SetPluginIconOverlays

#define VALID_DATA_NONE 0 // helper constant - only Name and NameLen are valid

#ifdef INSIDE_SALAMANDER
// VALID_DATA_ALL and VALID_DATA_ALL_FS_ARC are only for internal use in Salamander (core),
// plugins only OR together constants corresponding to data provided by the plugin (this prevents problems
// when introducing new constants and their corresponding data)
#define VALID_DATA_ALL 0xFFFF
#define VALID_DATA_ALL_FS_ARC (0xFFFF & ~VALID_DATA_ICONOVERLAY) // for FS and archives: everything except icon-overlays
#endif                                                           // INSIDE_SALAMANDER

// If hiding of hidden and system files and directories is enabled, items with
// Hidden==1 and Attr containing FILE_ATTRIBUTE_HIDDEN and/or FILE_ATTRIBUTE_SYSTEM are not displayed in panels.

// flag constants for CSalamanderDirectoryAbstract:
// file and directory names (including in paths) should be compared case-sensitive (without this flag
// comparison is case-insensitive - standard Windows behavior)
#define SALDIRFLAG_CASESENSITIVE 0x0001
// subdirectory names within each directory will not be tested for duplicates (this
// test is time-consuming and is only necessary in archives when adding items not only
// to root - so that e.g. adding "file1" to "dir1" followed by adding
// "dir1" works - "dir1" is added by the first operation (non-existent path is added automatically),
// second operation only updates data about "dir1" (must not add it again))
#define SALDIRFLAG_IGNOREDUPDIRS 0x0002

class CPluginDataInterfaceAbstract;

class CSalamanderDirectoryAbstract
{
public:
    // clears the entire object, prepares it for further use; if 'pluginData' is not NULL, it is used
    // for files and directories to release plugin-specific data (CFileData::PluginData);
    // sets the standard value of valid data mask (sum of all VALID_DATA_XXX except
    // VALID_DATA_ICONOVERLAY) and object flags (see SetFlags method)
    virtual void WINAPI Clear(CPluginDataInterfaceAbstract* pluginData) = 0;

    // specifies valid data mask, which determines which data from CFileData is valid
    // and which should only be "zeroed" (see comment for VALID_DATA_XXX); 'validData' mask
    // contains ORed VALID_DATA_XXX values; standard mask value is sum of all
    // VALID_DATA_XXX except VALID_DATA_ICONOVERLAY; valid data mask needs to be set
    // before calling AddFile/AddDir
    virtual void WINAPI SetValidData(DWORD validData) = 0;

    // sets flags for this object; 'flags' is a combination of ORed SALDIRFLAG_XXX flags,
    // standard flag value is zero for archivers (no flag is set)
    // and SALDIRFLAG_IGNOREDUPDIRS for file-systems (only adding to root is allowed, duplicate
    // directory test is unnecessary)
    virtual void WINAPI SetFlags(DWORD flags) = 0;

    // adds a file to the specified path (relative to this "salamander-directory"), returns success
    // string path is used only inside the function, content of file structure is used outside the function
    // (do not free memory allocated for variables inside the structure)
    // in case of failure, the content of file structure must be freed;
    // 'pluginData' parameter is not NULL only for archives (FS use only empty 'path' (==NULL));
    // if 'pluginData' is not NULL, it is used when creating new directories (if
    // 'path' does not exist), see CPluginDataInterfaceAbstract::GetFileDataForNewDir;
    // uniqueness check for file name on 'path' is not performed
    virtual BOOL WINAPI AddFile(const char* path, CFileData& file, CPluginDataInterfaceAbstract* pluginData) = 0;

    // adds a directory to the specified path (relative to this "salamander-directory"), returns success
    // string path is used only inside the function, content of file structure is used outside the function
    // (do not free memory allocated for variables inside the structure)
    // in case of failure, the content of file structure must be freed;
    // 'pluginData' parameter is not NULL only for archives (FS use only empty 'path' (==NULL));
    // if 'pluginData' is not NULL, it is used when creating new directories (if 'path' does not exist),
    // see CPluginDataInterfaceAbstract::GetFileDataForNewDir;
    // uniqueness check for directory name on 'path' is performed, if an already existing
    // directory is being added, original data is freed (if 'pluginData' is not NULL,
    // CPluginDataInterfaceAbstract::ReleasePluginData is also called to free data) and data from 'dir' is stored
    // (necessary for updating data of directories that are automatically created when 'path' does not exist);
    // special case for FS (or object allocated via CSalamanderGeneralAbstract::AllocSalamanderDirectory
    // with 'isForFS'==TRUE): if dir.Name is "..", directory is added as up-dir (there can be only one,
    // always displayed at the beginning of listing with special icon)
    virtual BOOL WINAPI AddDir(const char* path, CFileData& dir, CPluginDataInterfaceAbstract* pluginData) = 0;

    // returns the number of files in the object
    virtual int WINAPI GetFilesCount() const = 0;

    // returns the number of directories in the object
    virtual int WINAPI GetDirsCount() const = 0;

    // returns file at index 'index', returned data can be used only for reading
    virtual CFileData const* WINAPI GetFile(int index) const = 0;

    // returns directory at index 'index', returned data can be used only for reading
    virtual CFileData const* WINAPI GetDir(int index) const = 0;

    // returns CSalamanderDirectory object for directory at index 'index', returned object can be
    // used only for reading (objects for empty directories are not allocated, one global
    // empty object is returned - changing this object would have global effect)
    virtual CSalamanderDirectoryAbstract const* WINAPI GetSalDir(int index) const = 0;

    // Allows plugin to specify in advance the expected number of files and directories in this directory.
    // Salamander will adjust reallocation strategy so that adding elements doesn't slow down too much.
    // Makes sense to call for directories containing thousands of files or directories. In case of tens of
    // thousands, calling this method is almost mandatory, otherwise reallocations will take several seconds.
    // 'files' and 'dirs' thus express the approximate total number of files and directories.
    // If either value is -1, Salamander will ignore it.
    // This method makes sense to call only if directory is empty, i.e. AddFile or AddDir was not called.
    virtual void WINAPI SetApproximateCount(int files, int dirs) = 0;
};

//
// ****************************************************************************
// SalEnumSelection a SalEnumSelection2
//

// constants returned from SalEnumSelection and SalEnumSelection2 in 'errorOccured' parameter
#define SALENUM_SUCCESS 0 // no error occurred
#define SALENUM_ERROR 1   // error occurred and user wants to continue operation (only erroneous files/directories were skipped)
#define SALENUM_CANCEL 2  // error occurred and user wants to cancel operation

// enumerator, returns file names, ends by returning NULL;
// 'enumFiles' == -1 -> reset enumeration (after this call enumeration starts from beginning again), all
//                      other parameters (except 'param') are ignored, has no return values (sets
//                      everything to zero)
// 'enumFiles' == 0 -> enumeration of files and subdirectories only from root
// 'enumFiles' == 1 -> enumeration of all files and subdirectories
// 'enumFiles' == 2 -> enumeration of all subdirectories, files only from root;
// error can occur only when 'enumFiles' == 1 or 'enumFiles' == 2 ('enumFiles' == 0 doesn't complete
// names and paths); 'parent' is parent of any error messageboxes (NULL means don't show
// errors); 'isDir' (if not NULL) returns TRUE if it's a directory; 'size' (if not NULL) returns
// file size (for directories size is returned only when 'enumFiles' == 0 - otherwise it's zero);
// if 'fileData' is not NULL, it returns pointer to CFileData structure of returned
// file/directory (if enumerator returns NULL, 'fileData' also returns NULL);
// 'param' is the 'nextParam' parameter passed together with pointer to function of this
// type; 'errorOccured' (if not NULL) returns SALENUM_ERROR if a too long name was encountered
// while building returned names and user decided to skip only erroneous files/directories,
// WARNING: error doesn't concern the currently returned name, that one is OK; 'errorOccured' (if not NULL) returns
// SALENUM_CANCEL if user decided to cancel operation on error (cancel), at the same time
// enumerator returns NULL (ends); 'errorOccured' (if not NULL) returns SALENUM_SUCCESS if
// no error occurred
typedef const char*(WINAPI* SalEnumSelection)(HWND parent, int enumFiles, BOOL* isDir, CQuadWord* size,
                                              const CFileData** fileData, void* param, int* errorOccured);

// enumerator, returns file names, ends by returning NULL;
// 'enumFiles' == -1 -> reset enumeration (after this call enumeration starts from beginning again), all
//                      other parameters (except 'param') are ignored, has no return values (sets
//                      everything to zero)
// 'enumFiles' == 0 -> enumeration of files and subdirectories only from root
// 'enumFiles' == 1 -> enumeration of all files and subdirectories
// 'enumFiles' == 2 -> enumeration of all subdirectories, files only from root;
// 'enumFiles' == 3 -> enumeration of all files and subdirectories + symbolic links to files have
//                     size of target file (with 'enumFiles' == 1 they have link size, which is probably
//                     always zero); WARNING: 'enumFiles' must remain 3 for all enumerator calls;
// error can occur only when 'enumFiles' == 1, 2 or 3 ('enumFiles' == 0 doesn't work
// with disk at all nor completes names and paths); 'parent' is parent of any messageboxes
// with errors (NULL means don't show errors); 'dosName' (if not NULL) returns DOS name
// (8.3; only if it exists, otherwise NULL); 'isDir' (if not NULL) returns TRUE if it's a directory;
// 'size' (if not NULL) returns file size (zero for directories); 'attr' (if not NULL)
// returns file/directory attributes; 'lastWrite' (if not NULL) returns last write time
// to file/directory; 'param' is the 'nextParam' parameter passed together with pointer to function
// of this type; 'errorOccured' (if not NULL) returns SALENUM_ERROR if an error occurred during reading
// data from disk or a too long name was encountered while building returned names
// and user decided to skip only erroneous files/directories, WARNING: error doesn't concern the currently
// returned name, that one is OK; 'errorOccured' (if not NULL) returns SALENUM_CANCEL if
// user decided to cancel operation on error (cancel), at the same time enumerator returns NULL (ends);
// 'errorOccured' (if not NULL) returns SALENUM_SUCCESS if no error occurred
typedef const char*(WINAPI* SalEnumSelection2)(HWND parent, int enumFiles, const char** dosName,
                                               BOOL* isDir, CQuadWord* size, DWORD* attr,
                                               FILETIME* lastWrite, void* param, int* errorOccured);

//
// ****************************************************************************
// CSalamanderViewAbstract
//
// set of Salamander methods for working with columns in panel (disabling/enabling/adding/setting)

// panel view modes
#define VIEW_MODE_TREE 1
#define VIEW_MODE_BRIEF 2
#define VIEW_MODE_DETAILED 3
#define VIEW_MODE_ICONS 4
#define VIEW_MODE_THUMBNAILS 5
#define VIEW_MODE_TILES 6

#define TRANSFER_BUFFER_MAX 1024 // buffer size for transferring column content from plugin to Salamander
#define COLUMN_NAME_MAX 30
#define COLUMN_DESCRIPTION_MAX 100

// Column identifiers. Columns inserted by plugin have ID==COLUMN_ID_CUSTOM.
// Standard Salamander columns have other IDs.
#define COLUMN_ID_CUSTOM 0 // column is provided by plugin - plugin takes care of storing its data
#define COLUMN_ID_NAME 1   // left-aligned, supports FixedWidth
// left-aligned, supports FixedWidth; separate "Ext" column, can only be at index==1;
// if column doesn't exist and VALID_DATA_EXTENSION is set in panel data (see CSalamanderDirectoryAbstract::SetValidData()),
// the "Ext" column is displayed in the "Name" column
#define COLUMN_ID_EXTENSION 2
#define COLUMN_ID_DOSNAME 3     // left-aligned
#define COLUMN_ID_SIZE 4        // right-aligned
#define COLUMN_ID_TYPE 5        // left-aligned, supports FixedWidth
#define COLUMN_ID_DATE 6        // right-aligned
#define COLUMN_ID_TIME 7        // right-aligned
#define COLUMN_ID_ATTRIBUTES 8  // right-aligned
#define COLUMN_ID_DESCRIPTION 9 // left-aligned, supports FixedWidth

// Callback to fill buffer with characters to be displayed in the respective column.
// For optimization purposes, the function doesn't receive/return variables through parameters,
// but through global variables (CSalamanderViewAbstract::GetTransferVariables).
typedef void(WINAPI* FColumnGetText)();

// Callback to get index of simple icons for FS with custom icons (pitFromPlugin).
// For optimization purposes, the function doesn't receive/return variables through parameters,
// but through global variables (CSalamanderViewAbstract::GetTransferVariables).
// From global variables the callback uses only TransferFileData and TransferIsDir.
typedef int(WINAPI* FGetPluginIconIndex)();

// column can be created in two ways:
// 1) Column was created by Salamander based on current view template.
//    In this case 'GetText' pointer (to filling function) points to Salamander
//    and gets texts from CFileData in standard way.
//    Value of 'ID' variable is different from COLUMN_ID_CUSTOM.
//
// 2) Column was added by plugin based on its needs.
//    'GetText' points to plugin and 'ID' equals COLUMN_ID_CUSTOM.

struct CColumn
{
    char Name[COLUMN_NAME_MAX]; // "Name", "Ext", "Size", ... column name under
                                // which the column appears in view and in menu
                                // Must not contain empty string.
                                // WARNING: May contain (after first null-terminator)
                                // also name of "Ext" column - this happens when
                                // separate "Ext" column doesn't exist and VALID_DATA_EXTENSION
                                // is set in panel data (see CSalamanderDirectoryAbstract::SetValidData()).
                                // For joining two strings use CSalamanderGeneralAbstract::AddStrToStr().

    char Description[COLUMN_DESCRIPTION_MAX]; // Tooltip in header line
                                              // Must not contain empty string.
                                              // WARNING: May contain (after first null-terminator)
                                              // also description of "Ext" column - this happens when
                                              // separate "Ext" column doesn't exist and VALID_DATA_EXTENSION
                                              // is set in panel data (see CSalamanderDirectoryAbstract::SetValidData()).
                                              // For joining two strings use CSalamanderGeneralAbstract::AddStrToStr().

    FColumnGetText GetText; // callback to get text (description at FColumnGetText type declaration)

    // FIXME_X64 - too small for pointer, is it ever needed?
    DWORD CustomData; // Not used by Salamander; plugin can use it
                      // to distinguish its added columns.

    unsigned SupportSorting : 1; // can the column be sorted?

    unsigned LeftAlignment : 1; // if TRUE column is left-aligned; otherwise right-aligned

    unsigned ID : 4; // column identifier
                     // For standard columns provided by Salamander
                     // contains values different from COLUMN_ID_CUSTOM.
                     // For columns added by plugin always contains
                     // value COLUMN_ID_CUSTOM.

    // Variables Width and FixedWidth can be changed by user while working with panel.
    // Standard columns provided by Salamander have saving/loading of these values ensured.
    // Values of these variables for columns provided by plugin need to be saved/loaded
    // within the plugin.
    // Columns whose width is calculated by Salamander based on content and user cannot
    // change it are called 'elastic'. Columns for which user can set width are called
    // 'fixed'.
    unsigned Width : 16;     // Column width when in fixed (adjustable) width mode.
    unsigned FixedWidth : 1; // Is column in fixed (adjustable) width mode?

    // working variables (not saved anywhere and don't need to be initialized)
    // are intended for Salamander's internal needs and plugins ignore them,
    // because their content is not guaranteed when plugin is called
    unsigned MinWidth : 16; // Minimum width to which column can be shrunk.
                            // Calculated based on column name and its sortability
                            // so that column header is always visible
};

// Through this interface plugin can change display mode in panel when path changes.
// All column work concerns only all detailed modes
// (Detailed + Types + three optional modes Alt+8/9/0). When path changes, plugin
// receives standard set of columns generated based on current view template.
// Plugin can modify this set. Modification is not permanent
// and on next path change plugin will receive standard set of columns again. It can thus
// for example remove some of the standard columns. Before new filling with standard columns
// plugin gets opportunity to save information about its columns (COLUMN_ID_CUSTOM).
// It can thus save their 'Width' and 'FixedWidth', which user could have set in panel
// (see ColumnFixedWidthShouldChange() and ColumnWidthWasChanged() in
// CPluginDataInterfaceAbstract interface). If plugin changes view mode, the change is permanent
// (e.g. switching to Thumbnails mode remains even after leaving plugin path).

class CSalamanderViewAbstract
{
public:
    // -------------- panel ----------------

    // returns mode in which panel is displayed (tree/brief/detailed/icons/thumbnails/tiles)
    // returns one of VIEW_MODE_xxxx values (Detailed, Types and three optional modes are
    // all VIEW_MODE_DETAILED)
    virtual DWORD WINAPI GetViewMode() = 0;

    // Sets panel mode to 'viewMode'. If it's one of detailed modes, it may
    // remove some standard columns (see 'validData'). Therefore it's advisable to call this
    // function first - before other functions from this interface that modify columns.
    //
    // 'viewMode' is one of VIEW_MODE_xxxx values
    // Panel mode cannot be changed to Types nor to any of three optional detailed modes
    // (all are represented by VIEW_MODE_DETAILED constant used for Detailed panel mode).
    // However if one of these four modes is currently selected in panel and 'viewMode' is
    // VIEW_MODE_DETAILED, this mode remains selected (i.e. doesn't switch to Detailed mode).
    // Change of panel mode is permanent (persists even after leaving plugin path).
    //
    // 'validData' informs about what data plugin wants to display in detailed mode, value
    // is ANDed with valid data mask specified via CSalamanderDirectoryAbstract::SetValidData
    // (it doesn't make sense to display columns with "zeroed" values).
    virtual void WINAPI SetViewMode(DWORD viewMode, DWORD validData) = 0;

    // Retrieves from Salamander location of variables that replace CColumn::GetText callback
    // parameters. On Salamander's side these are global variables. Plugin stores
    // pointers to them in its own global variables.
    //
    // variables:
    //   transferFileData        [IN]     data based on which the item should be drawn
    //   transferIsDir           [IN]     equals 0 if it's a file (located in Files array),
    //                                    equals 1 if it's a directory (located in Dirs array),
    //                                    equals 2 if it's up-dir symbol
    //   transferBuffer          [OUT]    data is poured here, maximum TRANSFER_BUFFER_MAX characters
    //                                    no need to null-terminate
    //   transferLen             [OUT]    before returning from callback this variable is set to
    //                                    number of filled characters without terminator (terminator
    //                                    doesn't need to be written to buffer)
    //   transferRowData         [IN/OUT] points to DWORD which is always zeroed before drawing columns
    //                                    for each row; can be used for optimizations
    //                                    Salamander has reserved bits 0x00000001 to 0x00000008.
    //                                    Other bits are available for plugin.
    //   transferPluginDataIface [IN]     plugin-data-interface of panel to which the item
    //                                    is being drawn (belongs to (*transferFileData)->PluginData)
    //   transferActCustomData   [IN]     CustomData of column for which text is being obtained (for which
    //                                    callback is called)
    virtual void WINAPI GetTransferVariables(const CFileData**& transferFileData,
                                             int*& transferIsDir,
                                             char*& transferBuffer,
                                             int*& transferLen,
                                             DWORD*& transferRowData,
                                             CPluginDataInterfaceAbstract**& transferPluginDataIface,
                                             DWORD*& transferActCustomData) = 0;

    // only for FS with custom icons (pitFromPlugin):
    // Sets callback for getting simple icon index (see
    // CPluginDataInterfaceAbstract::GetSimplePluginIcons). If plugin doesn't set this callback,
    // only icon from index 0 will always be drawn.
    // From global variables the callback uses only TransferFileData and TransferIsDir.
    virtual void WINAPI SetPluginSimpleIconCallback(FGetPluginIconIndex callback) = 0;

    // ------------- columns ---------------

    // returns number of columns in panel (always at least one, because name is always displayed)
    virtual int WINAPI GetColumnsCount() = 0;

    // returns pointer to column (read-only)
    // 'index' specifies which column will be returned; if column 'index' doesn't exist, returns NULL
    virtual const CColumn* WINAPI GetColumn(int index) = 0;

    // Inserts column at position 'index'. Position 0 always contains Name column,
    // if Ext column is displayed, it will be at position 1. Otherwise column can be placed
    // anywhere. Structure 'column' will be copied to Salamander's internal structures.
    // Returns TRUE if column was inserted.
    virtual BOOL WINAPI InsertColumn(int index, const CColumn* column) = 0;

    // Inserts standard column with ID 'id' at position 'index'. Position 0 always
    // contains Name column, if Ext column is being inserted, it must be at position 1.
    // Otherwise column can be placed anywhere. 'id' is one of COLUMN_ID_xxxx values,
    // except COLUMN_ID_CUSTOM and COLUMN_ID_NAME.
    virtual BOOL WINAPI InsertStandardColumn(int index, DWORD id) = 0;

    // Sets column name and description (must not be empty strings or NULL). String lengths
    // are limited to COLUMN_NAME_MAX and COLUMN_DESCRIPTION_MAX. Returns success.
    // WARNING: Name and description of "Name" column may contain (always after first
    // null-terminator) also name and description of "Ext" column - this happens when
    // separate "Ext" column doesn't exist and VALID_DATA_EXTENSION is set in panel data
    // (see CSalamanderDirectoryAbstract::SetValidData()).
    // In this case double strings (with two null-terminators) need to be set -
    // see CSalamanderGeneralAbstract::AddStrToStr().
    virtual BOOL WINAPI SetColumnName(int index, const char* name, const char* description) = 0;

    // Removes column at position 'index'. Both columns added by plugin and standard
    // Salamander columns can be removed. 'Name' column, which is always at index 0,
    // cannot be removed. Beware when removing 'Ext' column, if VALID_DATA_EXTENSION is
    // in plugin data (see CSalamanderDirectoryAbstract::SetValidData()),
    // name+description of 'Ext' column must appear at 'Name' column.
    virtual BOOL WINAPI DeleteColumn(int index) = 0;
};

//
// ****************************************************************************
// CPluginDataInterfaceAbstract
//
// set of plugin methods that Salamander needs to get plugin-specific data
// into columns added by plugin (works with CFileData::PluginData)

class CPluginInterfaceAbstract;

class CPluginDataInterfaceAbstract
{
#ifdef INSIDE_SALAMANDER
private: // protection against incorrect direct method calls (see CPluginDataInterfaceEncapsulation)
    friend class CPluginDataInterfaceEncapsulation;
#else  // INSIDE_SALAMANDER
public:
#endif // INSIDE_SALAMANDER

    // returns TRUE if ReleasePluginData method should be called for all files bound
    // to this interface, otherwise returns FALSE
    virtual BOOL WINAPI CallReleaseForFiles() = 0;

    // returns TRUE if ReleasePluginData method should be called for all directories bound
    // to this interface, otherwise returns FALSE
    virtual BOOL WINAPI CallReleaseForDirs() = 0;

    // releases plugin-specific data (CFileData::PluginData) for 'file' (file or
    // directory - 'isDir' FALSE or TRUE; structure inserted into CSalamanderDirectoryAbstract
    // when listing archive or FS); called for all files if CallReleaseForFiles
    // returns TRUE, and for all directories if CallReleaseForDirs returns TRUE
    virtual void WINAPI ReleasePluginData(CFileData& file, BOOL isDir) = 0;

    // only for archive data (for FS up-dir symbol is not added):
    // modifies proposed content of up-dir symbol (".." at top of panel); 'archivePath'
    // is path in archive for which the symbol is intended; 'upDir' receives proposed
    // symbol data: name ".." (don't change), date&time of archive, rest zeroed;
    // 'upDir' outputs plugin changes, mainly 'upDir.PluginData' should be changed,
    // which will be used on up-dir symbol when getting content of added columns;
    // ReleasePluginData won't be called for 'upDir', any needed release
    // can be performed at next GetFileDataForUpDir call or when releasing
    // the entire interface (in its destructor - called from
    // CPluginInterfaceAbstract::ReleasePluginDataInterface)
    virtual void WINAPI GetFileDataForUpDir(const char* archivePath, CFileData& upDir) = 0;

    // only for archive data (FS uses only root path in CSalamanderDirectoryAbstract):
    // when adding file/directory to CSalamanderDirectoryAbstract it may happen that
    // specified path doesn't exist and needs to be created, individual directories of this
    // path are created automatically and this method allows plugin to add its specific
    // data (for its columns) to these created directories; 'dirName' is full path
    // of added directory in archive; 'dir' receives proposed data: directory name
    // (allocated on Salamander's heap), date&time taken from added file/directory,
    // rest zeroed; 'dir' outputs plugin changes, mainly 'dir.PluginData' should be changed;
    // returns TRUE if adding plugin data succeeded, otherwise FALSE;
    // if returns TRUE, 'dir' will be released through standard path (Salamander part +
    // ReleasePluginData) either when completely releasing listing or still during
    // its creation if the same directory is added via
    // CSalamanderDirectoryAbstract::AddDir (overwriting automatic creation with later
    // normal addition); if returns FALSE, only Salamander part will be released from 'dir'
    virtual BOOL WINAPI GetFileDataForNewDir(const char* dirName, CFileData& dir) = 0;

    // only for FS with custom icons (pitFromPlugin):
    // returns image-list with simple icons, during drawing items in panel
    // icon-index into this image-list is obtained via callback; called always after
    // obtaining new listing (after calling CPluginFSInterfaceAbstract::ListCurrentPath),
    // so image-list can be rebuilt for each new listing;
    // 'iconSize' specifies requested icon size and is one of SALICONSIZE_xxx values
    // plugin ensures image-list destruction at next GetSimplePluginIcons call
    // or when releasing the entire interface (in its destructor - called from
    // CPluginInterfaceAbstract::ReleasePluginDataInterface)
    // if image-list cannot be created, returns NULL and current plugin-icons-type
    // degrades to pitSimple
    virtual HIMAGELIST WINAPI GetSimplePluginIcons(int iconSize) = 0;

    // only for FS with custom icons (pitFromPlugin):
    // returns TRUE if simple icon should be used for given file/directory ('isDir' FALSE/TRUE) 'file';
    // returns FALSE if GetPluginIcon method should be called from icon loading thread to get icon
    // (loading icon "in background");
    // also in this method icon-index for simple icon can be precomputed
    // (for icons read "in background" simple icons are also used until loaded)
    // and stored in CFileData (most likely in CFileData::PluginData);
    // restriction: from CSalamanderGeneralAbstract only methods that can be called
    // from any thread can be used (methods independent of panel state)
    virtual BOOL WINAPI HasSimplePluginIcon(CFileData& file, BOOL isDir) = 0;

    // only for FS with custom icons (pitFromPlugin):
    // returns icon for file or directory 'file' or NULL if icon cannot be obtained; if
    // 'destroyIcon' returns TRUE, Win32 API function DestroyIcon is called to release returned icon;
    // 'iconSize' specifies size of requested icon and is one of SALICONSIZE_xxx values
    // restriction: since called from icon loading thread (not main thread), only methods from
    // CSalamanderGeneralAbstract that can be called from any thread can be used
    virtual HICON WINAPI GetPluginIcon(const CFileData* file, int iconSize, BOOL& destroyIcon) = 0;

    // only for FS with custom icons (pitFromPlugin):
    // compares 'file1' (can be file or directory) and 'file2' (can be file or directory),
    // must not return that any two items in listing are equal (ensures unique
    // assignment of custom icon to file/directory); if duplicate names in path listing
    // are not possible (common case), can be simply implemented as:
    // {return strcmp(file1->Name, file2->Name);}
    // returns number less than zero if 'file1' < 'file2', zero if 'file1' == 'file2' and
    // number greater than zero if 'file1' > 'file2';
    // restriction: since called also from icon loading thread (not only main thread),
    // only methods from CSalamanderGeneralAbstract that can be called from any thread can be used
    virtual int WINAPI CompareFilesFromFS(const CFileData* file1, const CFileData* file2) = 0;

    // used to set view parameters, this method is always called before displaying new
    // panel content (when path changes) and when current view changes (including manual width
    // change of column); 'leftPanel' is TRUE if it's left panel (FALSE if it's right panel);
    // 'view' is interface for view modification (mode setting, working with
    // columns); if it's archive data, 'archivePath' contains current path in archive,
    // for FS data 'archivePath' is NULL; if it's archive data, 'upperDir' is pointer to
    // parent directory (if current path is archive root, 'upperDir' is NULL), for FS data
    // it's always NULL;
    // WARNING: panel must not be redrawn during this method call (icon size may change here,
    //          etc.), so no message loops (no dialogs, etc.)!
    // restriction: from CSalamanderGeneralAbstract only methods that can be called
    //              from any thread can be used (methods independent of panel state)
    virtual void WINAPI SetupView(BOOL leftPanel, CSalamanderViewAbstract* view,
                                  const char* archivePath, const CFileData* upperDir) = 0;

    // setting new value of "column->FixedWidth" - user used context menu
    // on plugin-added column in header-line > "Automatic Column Width"; plugin
    // should save new value column->FixedWidth stored in 'newFixedWidth'
    // (it's always negation of column->FixedWidth), so in subsequent SetupView() calls
    // it can add column with correctly set FixedWidth; also if fixed width is being enabled,
    // plugin should set current value of "column->Width" (so that
    // enabling fixed width doesn't change column width) - ideal is to call
    // "ColumnWidthWasChanged(leftPanel, column, column->Width)"; 'column' identifies
    // column to be changed; 'leftPanel' is TRUE if it's column from left
    // panel (FALSE if it's column from right panel)
    virtual void WINAPI ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column,
                                                     int newFixedWidth) = 0;

    // setting new value of "column->Width" - user changed width of plugin-added column
    // in header-line with mouse; plugin should save new value column->Width (also stored
    // in 'newWidth'), so in subsequent SetupView() calls it can add column with
    // correctly set Width; 'column' identifies column that changed; 'leftPanel'
    // is TRUE if it's column from left panel (FALSE if it's column from right panel)
    virtual void WINAPI ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column,
                                              int newWidth) = 0;

    // gets Information Line content for file/directory ('isDir' TRUE/FALSE) 'file'
    // or selected files and directories ('file' is NULL and counts of selected files/directories
    // are in 'selectedFiles'/'selectedDirs') in panel ('panel' is one of PANEL_XXX);
    // also called for empty listing (concerns only FS, cannot happen for archives, 'file' is NULL,
    // 'selectedFiles' and 'selectedDirs' are 0); if 'displaySize' is TRUE, size of
    // all selected directories is known (see CFileData::SizeValid; if nothing is selected, this is
    // TRUE); 'selectedSize' contains sum of CFileData::Size numbers of selected files and directories
    // (if nothing is selected, this is zero); 'buffer' is buffer for returned text (size
    // 1000 bytes); 'hotTexts' is array (size 100 DWORDs), in which hot-text position information
    // is returned, lower WORD always contains hot-text position in 'buffer', upper WORD contains
    // hot-text length; 'hotTextsCount' contains size of 'hotTexts' array (100) and returns number of
    // written hot-texts in 'hotTexts' array; returns TRUE if 'buffer' + 'hotTexts' +
    // 'hotTextsCount' is set, returns FALSE if Information Line should be filled in standard
    // way (as on disk)
    virtual BOOL WINAPI GetInfoLineContent(int panel, const CFileData* file, BOOL isDir, int selectedFiles,
                                           int selectedDirs, BOOL displaySize, const CQuadWord& selectedSize,
                                           char* buffer, DWORD* hotTexts, int& hotTextsCount) = 0;

    // only for archives: user saved files/directories from archive to clipboard, now closing
    // archive in panel: if method returns TRUE, this object remains open (optimization
    // for possible Paste from clipboard - archive is already listed), if method returns FALSE,
    // this object is released (possible Paste from clipboard will cause archive listing, then
    // extraction of selected files/directories will occur); NOTE: if archive file is open
    // for object's lifetime, method should return FALSE, otherwise archive file will be
    // open for entire duration of data "staying" on clipboard (cannot be deleted, etc.)
    virtual BOOL WINAPI CanBeCopiedToClipboard() = 0;

    // only when VALID_DATA_PL_SIZE is specified to CSalamanderDirectoryAbstract::SetValidData():
    // returns TRUE if size of file/directory ('isDir' TRUE/FALSE) 'file' is known,
    // otherwise returns FALSE; returns size in 'size'
    virtual BOOL WINAPI GetByteSize(const CFileData* file, BOOL isDir, CQuadWord* size) = 0;

    // only when VALID_DATA_PL_DATE is specified to CSalamanderDirectoryAbstract::SetValidData():
    // returns TRUE if date of file/directory ('isDir' TRUE/FALSE) 'file' is known,
    // otherwise returns FALSE; returns date in "date" part of 'date' structure ("time" part
    // should remain untouched)
    virtual BOOL WINAPI GetLastWriteDate(const CFileData* file, BOOL isDir, SYSTEMTIME* date) = 0;

    // only when VALID_DATA_PL_TIME is specified to CSalamanderDirectoryAbstract::SetValidData():
    // returns TRUE if time of file/directory ('isDir' TRUE/FALSE) 'file' is known,
    // otherwise returns FALSE; returns time in "time" part of 'time' structure ("date" part
    // should remain untouched)
    virtual BOOL WINAPI GetLastWriteTime(const CFileData* file, BOOL isDir, SYSTEMTIME* time) = 0;
};

//
// ****************************************************************************
// CSalamanderForOperationsAbstract
//
// set of methods from Salamander to support operation execution, interface validity is
// limited to method to which interface is passed as parameter; thus can only be called
// from this thread and in this method (object is on stack, so it ceases to exist after return)

class CSalamanderForOperationsAbstract
{
public:
    // PROGRESS DIALOG: dialog obsahuje jeden/dva ('twoProgressBars' FALSE/TRUE) progress-metry
    // otevre progress-dialog s titulkem 'title'; 'parent' je parent okno progress-dialogu (je-li
    // NULL, pouzije se hlavni okno); pokud obsahuje jen jeden progress-metr, muze byt popsan
    // jako "File" ('fileProgress' je TRUE) nebo "Total" ('fileProgress' je FALSE)
    //
    // dialog nebezi ve vlastnim threadu; pro jeho fungovani (tlacitko Cancel + vnitrni timer)
    // je treba obcas vyprazdni message queue; to zajistuji metody ProgressDialogAddText,
    // ProgressAddSize a ProgressSetSize
    //
    // protoze real-time zobrazovani textu a zmen v progress bare silne zdrzuje, maji
    // metody ProgressDialogAddText, ProgressAddSize a ProgressSetSize parametr
    // 'delayedPaint'; ten by mel byt TRUE pro vsechny rychle se menici texty a hodnoty;
    // metody si pak ulozi texty a zobrazi je az po doruceni vnitrniho timeru dialogu;
    // 'delayedPaint' nastavime na FALSE pro inicializacni/koncove texty typu "preparing data..."
    // nebo "canceling operation...", po jejiz zobrazeni nedame dialogu prilezitost k distribuci
    // zprav (timeru); pokud je u takove operace pravdepodobne, ze bude trvat dlouho, meli
    // bychom behem teto doby dialog "obcerstvovat" volanim ProgressAddSize(CQuadWord(0, 0), TRUE)
    // a podle jeji navratove hodnoty akci pripadne predcasne ukoncit
    virtual void WINAPI OpenProgressDialog(const char* title, BOOL twoProgressBars,
                                           HWND parent, BOOL fileProgress) = 0;
    // vypise text 'txt' (i nekolik radku - provadi se rozpad na radky) do progress-dialogu
    virtual void WINAPI ProgressDialogAddText(const char* txt, BOOL delayedPaint) = 0;
    // neni-li 'totalSize1' CQuadWord(-1, -1), nastavi 'totalSize1' jako 100 procent prvniho progress-metru,
    // neni-li 'totalSize2' CQuadWord(-1, -1), nastavi 'totalSize2' jako 100 procent druheho progress-metru
    // (pro progress-dialog s jednim progress-metrem je povinne 'totalSize2' CQuadWord(-1, -1))
    virtual void WINAPI ProgressSetTotalSize(const CQuadWord& totalSize1, const CQuadWord& totalSize2) = 0;
    // neni-li 'size1' CQuadWord(-1, -1), nastavi velikost 'size1' (size1/total1*100 procent) na prvnim progress-metru,
    // neni-li 'size2' CQuadWord(-1, -1), nastavi velikost 'size2' (size2/total2*100 procent) na druhem progress-metru
    // (pro progress-dialog s jednim progress-metrem je povinne 'size2' CQuadWord(-1, -1)), vraci informaci jestli ma
    // akce pokracovat (FALSE = konec)
    virtual BOOL WINAPI ProgressSetSize(const CQuadWord& size1, const CQuadWord& size2, BOOL delayedPaint) = 0;
    // prida (pripadne k oboum progress-metrum) velikost 'size' (size/total*100 procent progressu),
    // vraci informaci jestli ma akce pokracovat (FALSE = konec)
    virtual BOOL WINAPI ProgressAddSize(int size, BOOL delayedPaint) = 0;
    // enabluje/disabluje tlacitko Cancel
    virtual void WINAPI ProgressEnableCancel(BOOL enable) = 0;
    // vraci HWND dialogu progressu (hodi se pri vypisu chyb a dotazu pri otevrenem progress-dialogu)
    virtual HWND WINAPI ProgressGetHWND() = 0;
    // zavre progress-dialog
    virtual void WINAPI CloseProgressDialog() = 0;

    // presune vsechny soubory ze 'source' adresare do 'target' adresare,
    // navic premapovava predpony zobrazovanych jmen ('remapNameFrom' -> 'remapNameTo')
    // vraci uspech operace
    virtual BOOL WINAPI MoveFiles(const char* source, const char* target, const char* remapNameFrom,
                                  const char* remapNameTo) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_com)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__
