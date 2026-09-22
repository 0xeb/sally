// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

extern HINSTANCE DLLInstance; // handle for SPL - language independent resources
extern HINSTANCE HLanguage;   // handle for SLG - language dependent resources

extern CSalamanderGeneralAbstract* SalamanderGeneral;
extern CSalamanderDebugAbstract* SalamanderDebug;
extern CSalamanderSafeFileAbstract* SalamanderSafeFile;
extern CSalamanderGUIAbstract* SalamanderGUI;

BOOL InitFS();
void ReleaseFS();

// FS-name assigned by Salamander after plugin is loaded
extern std::wstring AssignedFSName;

// image-list for simple FS icons
extern HIMAGELIST DFSImageList;

// configuration
extern BOOL ConfigAlwaysReuseScanInfo;
extern BOOL ConfigScanVacantClusters;
extern BOOL ConfigShowExistingFiles;
extern BOOL ConfigShowZeroFiles;
extern BOOL ConfigShowEmptyDirs;
extern BOOL ConfigShowMetafiles;
extern BOOL ConfigEstimateDamage;
extern std::wstring ConfigTempPath;
extern BOOL ConfigDontShowEncryptedWarning;
extern BOOL ConfigDontShowSamePartitionWarning;

// commands
#define CMD_UNDELETE 1
#define CMD_RESTORE_ENCRYPTED 2
#ifdef _DEBUG
#define CMD_VIEWINFO 3
#define CMD_DUMPFILES 4
#define CMD_COMPAREFILES 5
#define CMD_FRAGMENTFILE 6
#define CMD_SETVALIDDATAFILE 7
#define CMD_SETSPARSEFILE 8
#endif //_DEBUG
#define CMD_SALCMD_OFFSET 5000

extern CLUSTER_MAP_I cluster_map;

//
// ****************************************************************************
// CPluginFSDataInterface
//

struct CSnapshotState
{
    int RefCount; // references count to this object (together fro FS object and from listings)
    BOOL Valid;   // FALSE = FS already released this snapshot, data are invalid

    CSnapshotState()
    {
        Valid = TRUE;
        RefCount = 0;
    }
    ~CSnapshotState()
    {
        if (RefCount != 0)
            TRACE_E("~CSnapshotState(): RefCount != 0");
    }

    BOOL IsValid() { return Valid; }
    void Invalidate() { Valid = FALSE; }

    void AddRef() { RefCount++; }
    void Release()
    {
        if (--RefCount <= 0)
            delete this;
    }
};

class CPluginFSDataInterface : public CPluginDataInterfaceAbstract
{
protected:
    CSnapshotState* SnapshotState;

public:
    CPluginFSDataInterface(CSnapshotState* snapshotState)
    {
        snapshotState->AddRef();
        SnapshotState = snapshotState;
    }
    ~CPluginFSDataInterface() { SnapshotState->Release(); }

    BOOL IsSnapshotValid() { return SnapshotState->IsValid(); }

    virtual BOOL WINAPI CallReleaseForFiles() { return FALSE; }
    virtual BOOL WINAPI CallReleaseForDirs() { return FALSE; }
    virtual void WINAPI ReleasePluginData(CFileData& file, BOOL isDir) {}

    virtual void WINAPI GetFileDataForUpDir(const wchar_t* archivePath, CFileData& upDir)
    {
        // plugin has no custom data in CFileData, nothing to change/allocate here
    }
    virtual BOOL WINAPI GetFileDataForNewDir(const wchar_t* dirName, CFileData& dir)
    {
        // plugin has no custom data in CFileData, nothing to change/allocate here
        return TRUE; // return success
    }

    virtual HIMAGELIST WINAPI GetSimplePluginIcons(int iconSize) { return NULL; }
    virtual BOOL WINAPI HasSimplePluginIcon(CFileData& file, BOOL isDir) { return TRUE; }
    virtual HICON WINAPI GetPluginIcon(const CFileData* file, int iconSize, BOOL& destroyIcon) { return NULL; }
    virtual int WINAPI CompareFilesFromFS(const CFileData* file1, const CFileData* file2) { return 0; }

    virtual void WINAPI SetupView(BOOL leftPanel, CSalamanderViewAbstract* view, const wchar_t* archivePath,
                                  const CFileData* upperDir);
    virtual void WINAPI ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth);
    virtual void WINAPI ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column, int newWidth);

    virtual BOOL WINAPI GetInfoLineContent(int panel, const CFileData* file, BOOL isDir, int selectedFiles,
                                           int selectedDirs, BOOL displaySize, const CQuadWord& selectedSize,
                                           CSalamanderStringBuffer* buffer, CSalamanderTextRangeBuffer* hotTexts) { return FALSE; }

    virtual BOOL WINAPI CanBeCopiedToClipboard() { return TRUE; }

    virtual BOOL WINAPI GetByteSize(const CFileData* file, BOOL isDir, CQuadWord* size) { return FALSE; }
    virtual BOOL WINAPI GetLastWriteDate(const CFileData* file, BOOL isDir, SYSTEMTIME* date) { return FALSE; }
    virtual BOOL WINAPI GetLastWriteTime(const CFileData* file, BOOL isDir, SYSTEMTIME* time) { return FALSE; }
};

//****************************************************************************
//
// CTopIndexMem
//
// panel listing top index memory - using CPluginFSInterface for correct behavior
// of ExecuteOnFS (persistent top-index while entering / leaving directory)

#define TOP_INDEX_MEM_SIZE 50 // count of stored top-index (levels), minimal 1

class CTopIndexMem
{
protected:
    // path for last stored top-index
    std::wstring Path;
    int TopIndexes[TOP_INDEX_MEM_SIZE]; // stored top-index list
    int TopIndexesCount;                // count of stored top-index

public:
    CTopIndexMem() { Clear(); }
    void Clear()
    {
        Path.clear();
        TopIndexesCount = 0;
    } // clear memory
    void Push(const wchar_t* path, int topIndex);        // store top-index for given path
    BOOL FindAndPop(const wchar_t* path, int& topIndex); // search top-index for given path, FALSE->not found
};

class CPluginInterfaceForFS : public CPluginInterfaceForFSAbstract
{
protected:
    int ActiveFSCount; // number of active FS interfaces (just for deallocation check)

public:
    CPluginInterfaceForFS() { ActiveFSCount = 0; }
    int GetActiveFSCount() { return ActiveFSCount; }

    virtual CPluginFSInterfaceAbstract* WINAPI OpenFS(const wchar_t* fsName, int fsNameIndex);
    virtual void WINAPI CloseFS(CPluginFSInterfaceAbstract* fs);

    virtual void WINAPI ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* pluginFS,
                                    const wchar_t* pluginFSName, int pluginFSNameIndex,
                                    CFileData& file, int isDir);
    virtual BOOL WINAPI DisconnectFS(HWND parent, BOOL isInPanel, int panel,
                                     CPluginFSInterfaceAbstract* pluginFS,
                                     const wchar_t* pluginFSName, int pluginFSNameIndex);

    virtual BOOL WINAPI ConvertPathToInternal(const wchar_t* fsName, int fsNameIndex,
                                              CSalamanderStringBuffer* fsUserPart) { return fsUserPart != NULL && sally::plugin_abi::IsValidStringBuffer(*fsUserPart); }
    virtual BOOL WINAPI ConvertPathToExternal(const wchar_t* fsName, int fsNameIndex,
                                              CSalamanderStringBuffer* fsUserPart) { return fsUserPart != NULL && sally::plugin_abi::IsValidStringBuffer(*fsUserPart); }

    virtual void WINAPI EnsureShareExistsOnServer(int panel, const wchar_t* server, const wchar_t* share) {}

    virtual void WINAPI ExecuteChangeDriveMenuItem(int panel);
    virtual BOOL WINAPI ChangeDriveMenuItemContextMenu(HWND parent, int panel, int x, int y,
                                                       CPluginFSInterfaceAbstract* pluginFS,
                                                       const wchar_t* pluginFSName, int pluginFSNameIndex,
                                                       BOOL isDetachedFS, BOOL& refreshMenu,
                                                       BOOL& closeMenu, int& postCmd, void*& postCmdParam) { return FALSE; }
    virtual void WINAPI ExecuteChangeDrivePostCommand(int panel, int postCmd, void* postCmdParam) {}
};

class CPluginInterfaceForMenuExt : public CPluginInterfaceForMenuExtAbstract
{
public:
    virtual DWORD WINAPI GetMenuItemState(int id, DWORD eventMask) { return 0; }
    virtual BOOL WINAPI ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander, HWND parent,
                                        int id, DWORD eventMask);
    virtual BOOL WINAPI HelpForMenuItem(HWND parent, int id);
    virtual void WINAPI BuildMenu(HWND parent, CSalamanderBuildMenuAbstract* salamander) {}
};

class CPluginInterface : public CPluginInterfaceAbstract
{
public:
    virtual void WINAPI About(HWND parent);

    virtual BOOL WINAPI Release(HWND parent, BOOL force);

    virtual void WINAPI LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry);
    virtual void WINAPI SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry);
    virtual void WINAPI Configuration(HWND parent);

    virtual void WINAPI Connect(HWND parent, CSalamanderConnectAbstract* salamander);

    virtual void WINAPI ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData);

    virtual CPluginInterfaceForArchiverAbstract* WINAPI GetInterfaceForArchiver() { return NULL; }
    virtual CPluginInterfaceForViewerAbstract* WINAPI GetInterfaceForViewer() { return NULL; }
    virtual CPluginInterfaceForMenuExtAbstract* WINAPI GetInterfaceForMenuExt();
    virtual CPluginInterfaceForFSAbstract* WINAPI GetInterfaceForFS();
    virtual CPluginInterfaceForThumbLoaderAbstract* WINAPI GetInterfaceForThumbLoader() { return NULL; }

    virtual void WINAPI Event(int event, DWORD param);
    virtual void WINAPI ClearHistory(HWND parent) {}
    virtual void WINAPI AcceptChangeOnPathNotification(const wchar_t* path, BOOL includingSubdirs) {}

    virtual void WINAPI PasswordManagerEvent(HWND parent, int event) {}
};

class CFileList;

class CPluginFSInterface : public CPluginFSInterfaceAbstract
{
protected:
    friend BOOL WINAPI EncryptedProgress(int inc, void* ctx);
    friend void WINAPI CPluginInterfaceForFS::ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* pluginFS,
                                                          const wchar_t* pluginFSName, int pluginFSNameIndex,
                                                          CFileData& file, int isDir);

    std::wstring Path; // current path
    std::wstring Root; // root of volume
    FILE_RECORD_I<wchar_t>* CurrentDir;

    BOOL FatalError;          // TRUE when ListCurrentPath failed (fatal error), ChangePath will be called
    CTopIndexMem TopIndexMem; // top-index array for ExecuteOnFS()

    BOOL IsSnapshotValid;
    CVolume<wchar_t> Volume;
    CSnapshot<wchar_t>* Snapshot;
    CSnapshotState* SnapshotState;
    DWORD Flags;

    HWND hErrParent;
    BOOL SkipAllLongPaths;
    DWORD SilentMask;
    QWORD FileProgress, TotalProgress, FileTotal, GrandTotal;
    CCopyProgressDlg* Progress;
    std::wstring SourcePath;
    std::wstring DamagedName;
    wchar_t AllSubstChar;
    BOOL BackupEncryptedFiles;

    // step 12: Path/Root/SourcePath and the FILE_RECORD_I<CHAR>
    // engine widened together (they were proven not separable: CVolume<wchar_t>::Open()
    // needs a wide Root, and ListCurrentPath's DupStr(di->FileName->FNName) needs a wide
    // FNName - see 24-intree-plugins-wide.md step 11). CopyFile renamed CopyFileRecord:
    // "CopyFile" collides with the Win32 CopyFile macro (_MBCS build, expands to
    // CopyFileA) once this class's own member no longer matches CopyFileA's exact
    // narrow shape.
    BOOL RootPathFromFull(const wchar_t* pluginFullPath, std::wstring& rootPath);
    BOOL AppendPath(std::wstring& buffer, const wchar_t* path, const wchar_t* name, BOOL* ret);
    BOOL CopyFileRecord(FILE_RECORD_I<wchar_t>* record, wchar_t* filename, const wchar_t* targetPath, BOOL view, int* incompleteFileAnswer);
    BOOL CopyDir(FILE_RECORD_I<wchar_t>* record, wchar_t* filename, const wchar_t* targetPath);
    QWORD GetFileSize(FILE_RECORD_I<wchar_t>* record, BOOL* encrypted);
    QWORD GetDirSize(FILE_RECORD_I<wchar_t>* record, int plus, BOOL* encrypted);
    QWORD GetTotalProgress(int panel, BOOL focused, int plus, BOOL* encrypted);
    void UpdateProgress();
    void Replace0xE5(wchar_t* filename);
    const wchar_t* FixDamagedName(wchar_t* name);
    BOOL CopyFileList(CFileList& list, const wchar_t* targetPath);
    BOOL PrepareRawAPI(wchar_t* targetPath, BOOL allowBackup);

public:
    CPluginFSInterface();
    ~CPluginFSInterface() {}

    virtual BOOL WINAPI GetCurrentPath(CSalamanderStringBuffer* userPart) override;
    virtual BOOL WINAPI GetFullName(CFileData& file, int isDir, CSalamanderStringBuffer* fullName) override;
    virtual BOOL WINAPI GetFullFSPath(HWND parent, const wchar_t* fsName, CSalamanderStringBuffer* path, BOOL& success) override;
    virtual BOOL WINAPI GetRootPath(CSalamanderStringBuffer* userPart) override;
    virtual BOOL WINAPI IsCurrentPath(int currentFSNameIndex, int fsNameIndex, const wchar_t* userPart) override;
    virtual BOOL WINAPI IsOurPath(int currentFSNameIndex, int fsNameIndex, const wchar_t* userPart) override;
    virtual BOOL WINAPI ChangePath(int currentFSNameIndex, CSalamanderStringBuffer* fsName, int fsNameIndex,
                                   const wchar_t* userPart, CSalamanderStringBuffer* cutFileName,
                                   BOOL* pathWasCut, BOOL forceRefresh, int mode) override;

    virtual BOOL WINAPI ListCurrentPath(CSalamanderDirectoryAbstract* dir,
                                        CPluginDataInterfaceAbstract*& pluginData,
                                        int& iconsType, BOOL forceRefresh);

    virtual BOOL WINAPI TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason);

    virtual void WINAPI Event(int event, DWORD param);

    virtual void WINAPI ReleaseObject(HWND parent);

    virtual DWORD WINAPI GetSupportedServices();

    // step 12: CPluginFSInterfaceAbstract's remaining methods (no
    // narrow/wide dual form exists for these in the current SDK - they're wide-only, unlike
    // the 7 GetCurrentPath-family methods above) widened to match the abstract base exactly.
    virtual BOOL WINAPI GetChangeDriveOrDisconnectItem(const wchar_t* fsName, wchar_t*& title, HICON& icon, BOOL& destroyIcon) { return FALSE; }
    virtual HICON WINAPI GetFSIcon(BOOL& destroyIcon);
    virtual void WINAPI GetDropEffect(const wchar_t* srcFSPath, const wchar_t* tgtFSPath,
                                      DWORD allowedEffects, DWORD keyState,
                                      DWORD* dropEffect) {}
    virtual void WINAPI GetFSFreeSpace(CQuadWord* retValue) {}
    virtual BOOL WINAPI GetNextDirectoryLineHotPath(const wchar_t* text, int pathLen, int& offset);
    virtual BOOL WINAPI CompleteDirectoryLineHotPath(CSalamanderStringBuffer* path) { return TRUE; }
    virtual BOOL WINAPI GetPathForMainWindowTitle(const wchar_t* fsName, int mode, CSalamanderStringBuffer* buf) { return FALSE; }
    virtual void WINAPI ShowInfoDialog(const wchar_t* fsName, HWND parent) {}
    virtual BOOL WINAPI ExecuteCommandLine(HWND parent, CSalamanderStringBuffer* command, int& selFrom, int& selTo) { return FALSE; }
    virtual BOOL WINAPI QuickRename(const wchar_t* fsName, int mode, HWND parent, CFileData& file, BOOL isDir,
                                    CSalamanderStringBuffer* newName, BOOL& cancel) { return FALSE; }
    virtual void WINAPI AcceptChangeOnPathNotification(const wchar_t* fsName, const wchar_t* path, BOOL includingSubdirs) {}
    virtual BOOL WINAPI CreateDir(const wchar_t* fsName, int mode, HWND parent, CSalamanderStringBuffer* newName, BOOL& cancel) { return FALSE; }
    virtual void WINAPI ViewFile(const wchar_t* fsName, HWND parent,
                                 CSalamanderForViewFileOnFSAbstract* salamander,
                                 CFileData& file);
    virtual BOOL WINAPI Delete(const wchar_t* fsName, int mode, HWND parent, int panel,
                               int selectedFiles, int selectedDirs, BOOL& cancelOrError) { return FALSE; }
    virtual BOOL WINAPI CopyOrMoveFromFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                                         int panel, int selectedFiles, int selectedDirs,
                                         CSalamanderStringBuffer* targetPath, BOOL& operationMask,
                                         BOOL& cancelOrHandlePath, HWND dropTarget);
    virtual BOOL WINAPI CopyOrMoveFromDiskToFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                                               const wchar_t* sourcePath, SalEnumSelection2 next,
                                               void* nextParam, int sourceFiles, int sourceDirs,
                                               CSalamanderStringBuffer* targetPath, BOOL* invalidPathOrCancel) { return FALSE; }
    virtual BOOL WINAPI ChangeAttributes(const wchar_t* fsName, HWND parent, int panel,
                                         int selectedFiles, int selectedDirs) { return FALSE; }
    virtual void WINAPI ShowProperties(const wchar_t* fsName, HWND parent, int panel,
                                       int selectedFiles, int selectedDirs) {}
    virtual void WINAPI ContextMenu(const wchar_t* fsName, HWND parent, int menuX, int menuY, int type,
                                    int panel, int selectedFiles, int selectedDirs);
    virtual BOOL WINAPI OpenFindDialog(const wchar_t* fsName, int panel) { return FALSE; }
    virtual void WINAPI OpenActiveFolder(const wchar_t* fsName, HWND parent) {}
    virtual void WINAPI GetAllowedDropEffects(int mode, const wchar_t* tgtFSPath, DWORD* allowedEffects) {}
    virtual BOOL WINAPI HandleMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult) { return FALSE; }
    virtual BOOL WINAPI GetNoItemsInPanelText(CSalamanderStringBuffer* textBuf) { return FALSE; }
    virtual void WINAPI ShowSecurityInfo(HWND parent) {}

    BOOL GetTempDirOutsideRoot(HWND parent, const wchar_t** ret);
#if defined(_DEBUG) && _WIN32_WINNT >= 0x0501 // some debug feauters works from Windows XP (NTFS 5.0)
    // NOTE: following functions are just quitch, dirty, and minimal tests (no error handling, etc)
    // included only in debug build
    // step 12 follow-up: these debug-only helpers were the one corner
    // step 12's Path/Root/FILE_RECORD_I<CHAR> widening never reached (Debug + WinXP-NTFS5-only,
    // easy to miss). Widened to match - Snapshot->Root is FILE_RECORD_I<wchar_t>* today, so
    // these were link-time-masked mismatches, not a deliberate narrow boundary.
    void DumpSpecifiedFiles(FILE* file, FILE_RECORD_I<wchar_t>* dir, std::wstring& path);
    void DumpDirItemInfo(FILE* file, const DIR_ITEM_I<wchar_t>* di);
    BOOL TestUndeleteOnExistingFile(FILE* file, FILE_RECORD_I<wchar_t>* record, std::wstring& path);
    void TestUndeleteOnExistingFiles(FILE* file, FILE_RECORD_I<wchar_t>* dir, std::wstring& path, DWORD* count);
    void DumpDebugInformation(HWND parent, const DIR_ITEM_I<wchar_t>* di, DWORD mode);
    void FragmentFile(HWND parent, const DIR_ITEM_I<wchar_t>* di, const wchar_t* fullPath);
    void SetFileValidData(HWND parent, const DIR_ITEM_I<wchar_t>* di, const wchar_t* fullPath);
    void SetFileSparse(HWND parent, const DIR_ITEM_I<wchar_t>* di, const wchar_t* fullPath);
#endif //_DEBUG
};

void UndeleteGetResolvedRootPath(const wchar_t* path, std::wstring& resolvedPath);
