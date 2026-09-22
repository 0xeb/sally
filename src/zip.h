// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

extern HWND ProgressDialogActivateDrop;

//
// ****************************************************************************
// CZIPUnpackProgress
//

#define ZIP_UNPACK_NUMLINES 5

class CProgressBar;
class CStaticText;

class CZIPUnpackProgress : public CCommonDialog
{
protected:
    const wchar_t* RemapNameFrom; // mapping of names from the tmp directory
    const wchar_t* RemapNameTo;   // to the name of the archive we are unpacking from
    BOOL FileProgress;         // for the single-progress variant: TRUE="File:", FALSE="Total:"

public:
    CZIPUnpackProgress();
    CZIPUnpackProgress(const wchar_t* title, HWND parent, const CQuadWord& totalSize, CITaskBarList3* taskBarList3);

    void Init();

    void Set(const wchar_t* title, HWND parent, const CQuadWord& totalSize, BOOL fileProgress);
    void Set(const wchar_t* title, HWND parent, const CQuadWord& totalSize1, const CQuadWord& totalSize2);
    void SetTotal(const CQuadWord& total1, const CQuadWord& total2); // CQuadWord(-1, -1) means do not set

    int AddSize(int size, BOOL delayedPaint);                                       // returns "continue?"
    int SetSize(const CQuadWord& size1, const CQuadWord& size2, BOOL delayedPaint); // returns "continue?"; size == CQuadWord(-1, -1) means "do not set"

    void NewLine(const wchar_t* txt, BOOL delayedPaint);
    void EnableCancel(BOOL enable);

    void SetRemapNames(const wchar_t* nameFrom, const wchar_t* nameTo);
    void DoRemapNames(wchar_t* txt, int bufLen);

    void SetTaskBarList3(CITaskBarList3* taskBarList3);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    BOOL HasTwoProgress(); // TRUE if dual-progress; otherwise FALSE

    void DispatchMessages(); // dispatches queued messages, giving the UI a chance to repaint and handle button clicks

    void FlushDataToControls(); // pushes dirty data into the controls (texts, progress bars)

    const wchar_t* Title;   // caption - pointer inside the LoadStr buffer (do not keep for long)
    BOOL Cancel;         // user canceled the operation; the dialog should end as soon as possible
    DWORD LastTickCount; // used to detect when it is time to repaint the changed data

    // note: Summary, Summary2, and Lines will be NULL if memory is low; account for that in the dialog code
    CProgressBar* Summary;
    CProgressBar* Summary2;
    CStaticText* Lines[ZIP_UNPACK_NUMLINES];

    wchar_t LinesCache[ZIP_UNPACK_NUMLINES][300]; // queued texts waiting to be displayed later
    int CacheIndex;                            // index to LinesCache array to be filled by the next line
    BOOL CacheIsDirty;                         // does the cache need to be sent to the screen?

    CQuadWord TotalSize,
        ActualSize;
    CQuadWord TotalSize2,
        ActualSize2;
    BOOL SizeIsDirty;  // does Size need to be pushed to the screen?
    BOOL Size2IsDirty; // does Size2 need to be pushed to the screen?

    CITaskBarList3* TaskBarList3; // pointer to the interface owned by Salamander's main window
};

//
// ****************************************************************************
// CSalamanderForOperations
//

class CSalamanderForOperations : public CSalamanderForOperationsAbstract
{
protected:
    CFilesWindow* Panel;
    CZIPUnpackProgress UnpackProgress; // UnpackProgress dialog
    BOOL ProgressDialog2;              // TRUE = dual-progress, FALSE = single-progress dialog
    HWND FocusWnd;

    // control mechanism
    DWORD ThreadID; // they may call us only through the thread in which our pointer was passed
    BOOL Destroyed; // if TRUE, the object has already been destroyed

public:
    CSalamanderForOperations(CFilesWindow* panel);
    ~CSalamanderForOperations();

    // PROGRESS DIALOG: the dialog contains one or two progress meters (depending on 'twoProgressBars' FALSE/TRUE)
    // opens the progress dialog with the title 'title'; 'parent' is the parent window of the progress dialog (if
    // NULL, the main window is used); if it contains only one progress meter, it can be labeled
    // as "File" ('fileProgress' is TRUE) or "Total" ('fileProgress' is FALSE)
    virtual void WINAPI OpenProgressDialog(const wchar_t* title, BOOL twoProgressBars, HWND parent, BOOL fileProgress);
    // prints the text 'txt' (even multiple lines - splits to lines) into the progress dialog
    virtual void WINAPI ProgressDialogAddText(const wchar_t* txt, BOOL delayedPaint);
    // if 'totalSize1' is not CQuadWord(-1, -1), sets 'totalSize1' as 100 percent of the first progress meter,
    // if 'totalSize2' is not CQuadWord(-1, -1), sets 'totalSize2' as 100 percent of the second progress meter
    // (for a progress dialog with a single progress meter, 'totalSize2' must be CQuadWord(-1, -1))
    virtual void WINAPI ProgressSetTotalSize(const CQuadWord& totalSize1, const CQuadWord& totalSize2);
    // if 'size1' is not CQuadWord(-1, -1), sets the value of 'size1' (size1/total1*100 percent) on the first progress meter,
    // if 'size2' is not CQuadWord(-1, -1), sets the value of 'size2' (size2/total2*100 percent) on the second progress meter
    // (for a progress dialog with a single progress meter, 'size2' must be CQuadWord(-1, -1)); returns whether the action
    // should continue (FALSE = end)
    virtual BOOL WINAPI ProgressSetSize(const CQuadWord& size1, const CQuadWord& size2, BOOL delayedPaint);
    // adds the size 'size' (optionally to both progress meters) (size/total*100 percent progress),
    // returns whether the action should continue (FALSE = end)
    virtual BOOL WINAPI ProgressAddSize(int size, BOOL delayedPaint);
    // enables/disables the Cancel button
    virtual void WINAPI ProgressEnableCancel(BOOL enable);
    // returns the progress dialog HWND (useful for displaying errors and prompts while the progress dialog is open)
    virtual HWND WINAPI ProgressGetHWND() { return UnpackProgress.HWindow; }
    // closes the progress dialog
    virtual void WINAPI CloseProgressDialog();

    // moves all files from the 'source' directory to the 'target' directory,
    // additionally remaps the prefixes of displayed names ('remapNameFrom' -> 'remapNameTo')
    // returns whether the operation succeeded
    virtual BOOL WINAPI MoveFiles(const wchar_t* source, const wchar_t* target, const wchar_t* remapNameFrom,
                                  const wchar_t* remapNameTo);
};

//
// ****************************************************************************
// CSalamanderDirectory
//

class CSalamanderDirectory;

// CSalamanderDirectoryAddCache is used to optimize adding files
// to CSalamanderDirectory (AddFile method)
struct CSalamanderDirectoryAddCache
{
    std::wstring Path;         // cached path
    CSalamanderDirectory* Dir; // pointer to the CSalamanderDirectory to which files and directories with the 'Path' path are being added
};

class CSalamanderDirectory : public CSalamanderDirectoryAbstract
{
protected:
    CFilesArray Dirs;                              // names of subdirectories (contents stored in SalamDirs at the same index)
    TDirectArray<CSalamanderDirectory*> SalamDirs; // pointers to CSalamanderDirectory (pointers are NULL until first access, then the objects are allocated)
    CFilesArray Files;                             // file names
    DWORD ValidData;                               // validity mask for data from CFileData
    DWORD Flags;                                   // object flags (see SALDIRFLAG_XXX)
    BOOL IsForFS;                                  // TRUE if this is a sal-dir for FS, FALSE if it is a sal-dir for archives
    CSalamanderDirectoryAddCache* AddCache;        // if not NULL, used to optimize adding files via AddFile; otherwise unused

public:
    CSalamanderDirectory(BOOL isForFS, DWORD validData = VALID_DATA_ALL_FS_ARC, DWORD flags = -1 /* set according to isForFS */);

    ~CSalamanderDirectory();

    // *********************************************************************************
    // methods of the CSalamanderDirectoryAbstract interface
    // *********************************************************************************
    virtual void WINAPI Clear(CPluginDataInterfaceAbstract* pluginData);
    virtual void WINAPI SetValidData(DWORD validData);
    virtual void WINAPI SetFlags(DWORD flags);
    virtual BOOL WINAPI AddFile(const wchar_t* path, CFileData& file, CPluginDataInterfaceAbstract* pluginData);
    virtual BOOL WINAPI AddDir(const wchar_t* path, CFileData& dir, CPluginDataInterfaceAbstract* pluginData);

    virtual int WINAPI GetFilesCount() const;
    virtual int WINAPI GetDirsCount() const;
    virtual CFileData const* WINAPI GetFile(int i) const;
    virtual CFileData const* WINAPI GetDir(int i) const;
    virtual CSalamanderDirectoryAbstract const* WINAPI GetSalDir(int i) const;
    virtual void WINAPI SetApproximateCount(int files, int dirs);

    // *********************************************************************************
    // helper methods (inaccessible from plugins)
    // *********************************************************************************

    // for optimizing the AddFile method
    void AllocAddCache();
    void FreeAddCache();

    // depending on Flags either StrICmp or strcmp (StrCmpEx) - selects case sensitive/insensitive comparison
    int SalDirStrCmp(const wchar_t* s1, const wchar_t* s2);
    int SalDirStrCmpEx(const wchar_t* s1, int l1, const wchar_t* s2, int l2);

    // calls 'pluginData'.ReleaseFilesOrDirs (releasing plug-in data) for all files (if 'releaseFiles' is TRUE)
    // and all directories (if 'releaseDirs' is TRUE)
    void ReleasePluginData(CPluginDataInterfaceEncapsulation& pluginData, BOOL releaseFiles,
                           BOOL releaseDirs);

    // returns directories from the specified path (relative to this Salamander directory)
    CFilesArray* GetDirs(const wchar_t* path);
    // returns files from the specified path (relative to this Salamander directory)
    CFilesArray* GetFiles(const wchar_t* path);

    // returns the parent directory for the path 'path' (returns NULL for root and unknown paths)
    const CFileData* GetUpperDir(const wchar_t* path);

    // returns the sum of sizes of all contained files; note: counters must be reset beforehand
    CQuadWord GetSize(int* dirsCount = NULL, int* filesCount = NULL, TDirectArray<CQuadWord>* sizes = NULL);
    // returns the directory size - sum of all files in it; note: counters must be reset beforehand
    CQuadWord GetDirSize(const wchar_t* path, const wchar_t* dirName, int* dirsCount = NULL,
                         int* filesCount = NULL, TDirectArray<CQuadWord>* sizes = NULL);
    // returns the salamander-dir for the specified directory; if 'readOnly' is TRUE,
    // the returned salamander-dir object must not be modified
    CSalamanderDirectory* GetSalamanderDir(const wchar_t* path, BOOL readOnly);
    // returns the salamander-dir for the specified directory index;
    // the returned salamander-dir object must not be modified
    CSalamanderDirectory* GetSalamanderDir(int i);
    // returns the index of the directory specified by name
    int GetIndex(const wchar_t* dir);
    // is there a directory at this index?
    BOOL IsDirectory(int i) { return i >= 0 && i < Dirs.Count; }
    // is there a file at this index?
    BOOL IsFile(int i) { return i >= Dirs.Count && i < Dirs.Count + Files.Count; }
    // returns the file for the specified index
    CFileData* GetFileEx(int i)
    {
        if (i >= Dirs.Count && i < Dirs.Count + Files.Count)
            return &Files[i - Dirs.Count];
        else
            return NULL;
    }
    // returns the directory for the specified index
    CFileData* GetDirEx(int i)
    {
        if (i >= 0 && i < Dirs.Count)
            return &Dirs[i];
        else
            return NULL;
    }

    DWORD GetValidData() { return ValidData; }

    DWORD GetFlags() { return Flags; }

protected:
    // helper method: allocates a salamander-dir object at index 'index' in the SalamDirs array,
    // returns a pointer to the object (or NULL on error)
    CSalamanderDirectory* AllocSalamDir(int index);

    BOOL FindDir(const wchar_t* path, const wchar_t*& s, int& i, const CFileData& file,
                 CPluginDataInterfaceAbstract* pluginData, const wchar_t* archivePath);

    // the AddFileInt and AddDirInt methods return a pointer to CSalamanderDirectory on success,
    // into which the item was added; otherwise they return NULL
    CSalamanderDirectory* AddFileInt(const wchar_t* path, CFileData& file,
                                     CPluginDataInterfaceAbstract* pluginData,
                                     const wchar_t* archivePath);
    CSalamanderDirectory* AddDirInt(const wchar_t* path, CFileData& dir,
                                    CPluginDataInterfaceAbstract* pluginData,
                                    const wchar_t* archivePath);
};

// checks the free space at path 'path' and, if it is >= totalSize, asks the user whether to continue
// wide - the title's AnsiToWide is deleted, not moved.
BOOL TestFreeSpace(HWND parent, const wchar_t* path, const CQuadWord& totalSize, const wchar_t* messageTitle);

//
// ****************************************************************************
// CPackerConfig
//

// item type in the custom packers table
struct SPackCustomPacker
{
    const wchar_t* CopyArgs[2];
    const wchar_t* MoveArgs[2];
    int Title[2];
    const wchar_t* Ext;
    BOOL SupLN;
    BOOL Ansi;
    const wchar_t* Exe;
};

// item type in the custom unpackers table
struct SPackCustomUnpacker
{
    const wchar_t* Args;
    int Title;
    const wchar_t* Ext;
    BOOL SupLN;
    BOOL Ansi;
    const wchar_t* Exe;
};

// custom packer tables
extern SPackCustomPacker CustomPackers[];
extern SPackCustomUnpacker CustomUnpackers[];

#define CUSTOMPACKER_EXTERNAL 0

class CPackerConfigData
{
public:
    std::wstring Title; // name shown to the user
    std::wstring Ext;   // standard extension (without the dot)
    int Type;          // internal (-1, -2, ...; see CPlugins for details) / external (0; additional fields apply)
                       // note: see OldType below

    // data for external packers
    std::wstring CmdExecCopy;
    std::wstring CmdArgsCopy;
    BOOL SupportMove;
    std::wstring CmdExecMove;
    std::wstring CmdArgsMove;
    BOOL SupportLongNames;
    BOOL NeedANSIListFile;

    // helper flag to detect the data layout - TRUE = legacy -> 'Type' (0 ZIP, 1 external, 2 TAR, 3 PAK)
    BOOL OldType;

public:
    CPackerConfigData()
    {
        Empty();
    }

    ~CPackerConfigData()
    {
        Destroy();
    }

    void Destroy()
    {
        Title.clear();
        Ext.clear();
        if (OldType && Type == 1 ||
            !OldType && Type == CUSTOMPACKER_EXTERNAL)
        {
            CmdExecCopy.clear();
            CmdArgsCopy.clear();
            CmdExecMove.clear();
            CmdArgsMove.clear();
        }
        Empty();
    }

    void Empty()
    {
        OldType = FALSE;
        Title.clear();
        Ext.clear();
        Type = 1;
        CmdExecCopy.clear();
        CmdArgsCopy.clear();
        SupportMove = FALSE;
        CmdExecMove.clear();
        CmdArgsMove.clear();
        SupportLongNames = FALSE;
        NeedANSIListFile = FALSE;
    }

    BOOL IsValid()
    {
        if (Title.empty() || Ext.empty())
            return FALSE;
        if ((OldType && Type == 1 || !OldType && Type == CUSTOMPACKER_EXTERNAL) &&
            (CmdExecCopy.empty() || CmdArgsCopy.empty()))
            return FALSE;
        if (SupportMove && (CmdExecMove.empty() || CmdArgsMove.empty()))
            return FALSE;
        return TRUE;
    }
};

class CPackerConfig
{
public:
    BOOL Move; // move or copy into the archive?

protected:
    int PreferedPacker;
    TIndirectArray<CPackerConfigData> Packers; // array of packer information, elements of type (CPackerConfigData *)

public:
    CPackerConfig(/*BOOL disableDefaultValues = FALSE*/);
    void InitializeDefaultValues(); // j.r. replaces the original constructor call
    BOOL Load(CPackerConfig& src);

    void DeleteAllPackers() { Packers.DestroyMembers(); }

    int AddPacker(BOOL toFirstIndex = FALSE); // returns the index of the created item or -1 on error
    void AddDefault(int SalamVersion);        // adds archivers introduced since SalamVersion

    // sets attributes; if something goes wrong, removes the item from the array, destroys it, and returns FALSE
    // old == TRUE -> 'type' uses the old convention (0 ZIP, 1 external, 2 TAR, 3 PAK)
    BOOL SetPacker(int index, int type, const wchar_t* title, const wchar_t* ext, BOOL old,
                   BOOL supportLongNames = FALSE, BOOL supportMove = FALSE,
                   const wchar_t* cmdExecCopy = NULL, const wchar_t* cmdArgsCopy = NULL,
                   const wchar_t* cmdExecMove = NULL, const wchar_t* cmdArgsMove = NULL,
                   BOOL needANSIListFile = FALSE);
    BOOL SetPackerTitle(int index, const wchar_t* title);
    void SetPackerType(int index, int type) { Packers[index]->Type = type; }
    void SetPackerOldType(int index, BOOL oldType) { Packers[index]->OldType = oldType; }
    void SetPackerSupMove(int index, BOOL supMove) { Packers[index]->SupportMove = supMove; }
    int GetPackersCount() { return Packers.Count; } // returns the number of items in the array
                                                    //    BOOL SwapPackers(int index1, int index2);         // swaps two items in the array
    BOOL MovePacker(int srcIndex, int dstIndex);    // moves an item
    void DeletePacker(int index);
    void SetPackerCmdExecCopy(int index, const wchar_t* cmd)
    {
        Packers[index]->CmdExecCopy = cmd;
    }
    void SetPackerCmdExecMove(int index, const wchar_t* cmd)
    {
        Packers[index]->CmdExecMove = cmd;
    }

    int GetPackerType(int index) { return Packers[index]->Type; }
    BOOL GetPackerOldType(int index) { return Packers[index]->OldType; }
    const wchar_t* GetPackerTitle(int index) { return Packers[index]->Title.c_str(); }
    const wchar_t* GetPackerExt(int index) { return Packers[index]->Ext.c_str(); }
    BOOL GetPackerSupLongNames(int index) { return Packers[index]->SupportLongNames; }
    BOOL GetPackerSupMove(int index) { return Packers[index]->SupportMove; }
    const wchar_t* GetPackerCmdExecCopy(int index) { return Packers[index]->CmdExecCopy.c_str(); }
    const wchar_t* GetPackerCmdArgsCopy(int index) { return Packers[index]->CmdArgsCopy.c_str(); }
    const wchar_t* GetPackerCmdExecMove(int index) { return Packers[index]->CmdExecMove.c_str(); }
    const wchar_t* GetPackerCmdArgsMove(int index) { return Packers[index]->CmdArgsMove.c_str(); }
    BOOL GetPackerNeedANSIListFile(int index) { return Packers[index]->NeedANSIListFile; }

    BOOL Save(int index, HKEY hKey);
    BOOL Load(HKEY hKey);

    int GetPreferedPacker() // returns -1 if no preferred one exists
    {
        return (PreferedPacker < Packers.Count) ? PreferedPacker : -1;
    }
    void SetPreferedPacker(int i) { PreferedPacker = i; }

    // 'lastNameW' gives the wide form of each name 'next' returns, or NULL when the
    // caller has no wide names - see SalEnumLastNameW in pack.h. Repeating the typedef
    // rather than including pack.h here keeps the header order as it was.
    BOOL ExecutePacker(CFilesWindow* panel, const wchar_t* zipFile, BOOL move,
                       const wchar_t* sourcePath, SalEnumSelection2 next, void* param,
                       const wchar_t*(WINAPI* lastNameW)(void* param) = NULL);
};

//
// ****************************************************************************
// CUnpackerConfig
//

#define CUSTOMUNPACKER_EXTERNAL 0

class CUnpackerConfigData
{
public:
    std::wstring Title; // name shown to the user
    std::wstring Ext;   // list of standard extensions separated by semicolons
    int Type;          // internal (-1, -2, ...; see CPlugins for details) / external (0; additional fields apply)
                       // note: see OldType below

    // data for external packers
    std::wstring CmdExecExtract;
    std::wstring CmdArgsExtract;
    BOOL SupportLongNames;
    BOOL NeedANSIListFile;

    // helper flag to detect the data layout - TRUE = legacy -> 'Type' (0 ZIP, 1 external, 2 TAR, 3 PAK)
    BOOL OldType;

public:
    CUnpackerConfigData()
    {
        Empty();
    }

    ~CUnpackerConfigData()
    {
        Destroy();
    }

    void Destroy()
    {
        Title.clear();
        Ext.clear();
        if (OldType && Type == 1 ||
            !OldType && Type == CUSTOMUNPACKER_EXTERNAL)
        {
            CmdExecExtract.clear();
            CmdArgsExtract.clear();
        }
        Empty();
    }

    void Empty()
    {
        OldType = FALSE;
        Title.clear();
        Ext.clear();
        Type = 1;
        CmdExecExtract.clear();
        CmdArgsExtract.clear();
        SupportLongNames = FALSE;
        NeedANSIListFile = FALSE;
    }

    BOOL IsValid()
    {
        if (Title.empty() || Ext.empty())
            return FALSE;
        if ((OldType && Type == 1 || !OldType && Type == CUSTOMUNPACKER_EXTERNAL) &&
            (CmdExecExtract.empty() || CmdArgsExtract.empty()))
            return FALSE;
        return TRUE;
    }
};

class CUnpackerConfig
{
protected:
    int PreferedUnpacker;
    TIndirectArray<CUnpackerConfigData> Unpackers; // array of packer information, elements of type (CUnpackerConfigData *)

public:
    CUnpackerConfig(/*BOOL disableDefaultValues = FALSE*/);
    void InitializeDefaultValues(); // j.r. replaces the original constructor call
    BOOL Load(CUnpackerConfig& src);

    void DeleteAllUnpackers() { Unpackers.DestroyMembers(); }

    int AddUnpacker(BOOL toFirstIndex = FALSE); // returns the index of the created item or -1 on error
    void AddDefault(int SalamVersion);          // adds archivers introduced since SalamVersion

    // sets attributes; if something goes wrong, removes the item from the array, destroys it, and returns FALSE
    // old == TRUE -> 'type' uses the old convention (0 ZIP, 1 external, 2 TAR, 3 PAK)
    BOOL SetUnpacker(int index, int type, const wchar_t* title, const wchar_t* ext, BOOL old,
                     BOOL supportLongNames = FALSE,
                     const wchar_t* cmdExecExtract = NULL, const wchar_t* cmdArgsExtract = NULL,
                     BOOL needANSIListFile = FALSE);
    BOOL SetUnpackerTitle(int index, const wchar_t* title);
    void SetUnpackerType(int index, int type) { Unpackers[index]->Type = type; }
    void SetUnpackerOldType(int index, BOOL oldType) { Unpackers[index]->OldType = oldType; }
    int GetUnpackersCount() { return Unpackers.Count; } // returns the number of items in the array
                                                        //    BOOL SwapUnpackers(int index1, int index2);         // swaps two items in the array
    BOOL MoveUnpacker(int srcIndex, int dstIndex);      // moves an item

    void DeleteUnpacker(int index);

    int GetUnpackerType(int index) { return Unpackers[index]->Type; }
    BOOL GetUnpackerOldType(int index) { return Unpackers[index]->OldType; }
    const wchar_t* GetUnpackerTitle(int index) { return Unpackers[index]->Title.c_str(); }
    const wchar_t* GetUnpackerExt(int index) { return Unpackers[index]->Ext.c_str(); }
    BOOL GetUnpackerSupLongNames(int index) { return Unpackers[index]->SupportLongNames; }
    const wchar_t* GetUnpackerCmdExecExtract(int index) { return Unpackers[index]->CmdExecExtract.c_str(); }
    const wchar_t* GetUnpackerCmdArgsExtract(int index) { return Unpackers[index]->CmdArgsExtract.c_str(); }
    BOOL GetUnpackerNeedANSIListFile(int index) { return Unpackers[index]->NeedANSIListFile; }

    BOOL Save(int index, HKEY hKey);
    BOOL Load(HKEY hKey);

    int GetPreferedUnpacker() // returns -1 if no preferred one exists
    {
        return (PreferedUnpacker < Unpackers.Count) ? PreferedUnpacker : -1;
    }
    void SetPreferedUnpacker(int i) { PreferedUnpacker = i; }

    BOOL ExecuteUnpacker(HWND parent, CFilesWindow* panel, const wchar_t* zipFile, const wchar_t* mask,
                         const wchar_t* targetDir, BOOL delArchiveWhenDone, CDynamicString* archiveVolumes);
};

extern CPackerConfig PackerConfig;
extern CUnpackerConfig UnpackerConfig;

// error and title are WIDE - they come from LoadStrW, and the
// dialog behind them shows a wide file name already.
// fileName is wide too now; the ABI widened, so nothing narrows on
// the way in. It is handed to CFileErrorDlg's 'fileW' slot, which that dialog
// has always preferred over its narrow twin.
int DialogError(HWND parent, DWORD flags, const wchar_t* fileName,
                const wchar_t* error, const wchar_t* title);
// all four are WIDE. The names go to COverwriteDlg's sourceNameW/
// targetNameW slots, which it has always preferred over its narrow twins.
int DialogOverwrite(HWND parent, DWORD flags, const wchar_t* fileName1, const wchar_t* fileData1,
                    const wchar_t* fileName2, const wchar_t* fileData2);
// fileName is WIDE too now - the ABI widened, so nothing narrows
// on the way in any more.
int DialogQuestion(HWND parent, DWORD flags, const wchar_t* fileName,
                   const wchar_t* question, const wchar_t* title);

BOOL ViewFileInPluginViewerW(const wchar_t* sourceFileName, const wchar_t* pluginSPL,
                             CSalamanderPluginViewerData* pluginData,
                             BOOL useCache, const wchar_t* rootTmpPath,
                             const wchar_t* fileNameInCache, int& error);
