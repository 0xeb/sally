// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// global data
extern DWORD DWord;
extern HINSTANCE DLLInstance; // handle to SPL - language-independent resources
extern HINSTANCE HLanguage;   // handle to SLG - language-dependent resources

// Salamander general interface - valid from startup until the plugin terminates
extern CSalamanderGeneralAbstract* SalamanderGeneral;

// interface providing customized Windows controls used in Salamander
extern CSalamanderGUIAbstract* SalamanderGUI;

BOOL InitFS();
void ReleaseFS();

// FS name assigned by Salamander after the plugin loads
extern std::wstring AssignedFSName;

// pointers to lower/upper case mapping tables
extern unsigned char* LowerCase;
extern unsigned char* UpperCase;

// global variables storing pointers to Salamander's global variables
// shared by both the archiver and FS
extern const CFileData** TransferFileData;
extern int* TransferIsDir;
extern char* TransferBuffer;
extern int* TransferLen;
extern DWORD* TransferRowData;
extern CPluginDataInterfaceAbstract** TransferPluginDataIface;
extern DWORD* TransferActCustomData;

// global data
extern int Number;
extern int Selection; // "second" in configuration dialog
extern BOOL CheckBox;
extern int RadioBox;                       // radio 2 in configuration dialog
extern BOOL CfgSavePosition;               // save window position/align with the main window
extern WINDOWPLACEMENT CfgWindowPlacement; // invalid when CfgSavePosition != TRUE

extern DWORD LastCfgPage; // start page (sheet) in the configuration dialog

std::wstring LangStr(int resID);

extern const wchar_t* TitleWMobile;
extern const wchar_t* TitleWMobileError;
extern const wchar_t* TitleWMobileQuestion;

//
// ****************************************************************************
// CPluginInterface
//

class CPluginInterfaceForFS : public CPluginInterfaceForFSAbstract
{
protected:
    int ActiveFSCount; // number of active FS interfaces (for deallocation checks only)

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
                                                       BOOL& closeMenu, int& postCmd, void*& postCmdParam);
    virtual void WINAPI ExecuteChangeDrivePostCommand(int panel, int postCmd, void* postCmdParam);
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
    virtual CPluginInterfaceForViewerAbstract* WINAPI GetInterfaceForViewer() { return NULL; };
    virtual CPluginInterfaceForMenuExtAbstract* WINAPI GetInterfaceForMenuExt() { return NULL; };
    virtual CPluginInterfaceForFSAbstract* WINAPI GetInterfaceForFS();
    virtual CPluginInterfaceForThumbLoaderAbstract* WINAPI GetInterfaceForThumbLoader() { return NULL; };

    virtual void WINAPI Event(int event, DWORD param) {}
    virtual void WINAPI ClearHistory(HWND parent);
    virtual void WINAPI AcceptChangeOnPathNotification(const wchar_t* path, BOOL includingSubdirs) {}

    virtual void WINAPI PasswordManagerEvent(HWND parent, int event) {}
};

//
// ****************************************************************************
// FILE-SYSTEM
//

//****************************************************************************
//
// CTopIndexMem
//
// top-index memory for the panel list box - used by CPluginFSInterface to keep
// ExecuteOnFS behavior consistent (preserves the top index when entering and leaving a subdirectory)

#include "wmobile_topindex_core.h"

#define TOP_INDEX_MEM_SIZE 50 // number of remembered top indexes (levels), at least 1

// The logic lives in wmobile_topindex_core.cpp so it can be exercised without a
// device - see gtest_wmobile_topindex_core. This is the same object, not a copy: the plugin
// runs the tested code. Salamander's own case-folding is preserved by injecting StrNICmp.
class CTopIndexMem
{
protected:
    wmobile::TopIndexMemory Memory;

public:
    CTopIndexMem();
    void Clear() { Memory.Clear(); }                  // clear the memory
    void Push(const wchar_t* path, int topIndex);        // store the top index for the given path
    BOOL FindAndPop(const wchar_t* path, int& topIndex); // find the top index for the given path, FALSE -> not found
};

//
// ****************************************************************************
// CPluginFSInterface
//
// set of plugin methods that Salamander needs to work with the file system

class CPluginFSInterface : public CPluginFSInterfaceAbstract
{
public:
    std::wstring Path;        // current device path
    BOOL PathError;           // TRUE if ListCurrentPath failed (path error); triggers ChangePath
    BOOL FatalError;          // TRUE if ListCurrentPath failed (fatal error); triggers ChangePath
    CTopIndexMem TopIndexMem; // stored top indexes for ExecuteOnFS()

public:
    CPluginFSInterface();
    ~CPluginFSInterface() {}

    virtual BOOL WINAPI GetCurrentPath(CSalamanderStringBuffer* userPart);
    virtual BOOL WINAPI GetFullName(CFileData& file, int isDir, CSalamanderStringBuffer* fullName);
    virtual BOOL WINAPI GetFullFSPath(HWND parent, const wchar_t* fsName,
                                      CSalamanderStringBuffer* path, BOOL& success);
    virtual BOOL WINAPI GetRootPath(CSalamanderStringBuffer* userPart);
    virtual BOOL WINAPI IsCurrentPath(int currentFSNameIndex, int fsNameIndex, const wchar_t* userPart);
    virtual BOOL WINAPI IsOurPath(int currentFSNameIndex, int fsNameIndex, const wchar_t* userPart);
    virtual BOOL WINAPI ChangePath(int currentFSNameIndex, CSalamanderStringBuffer* fsName,
                                   int fsNameIndex, const wchar_t* userPart,
                                   CSalamanderStringBuffer* cutFileName, BOOL* pathWasCut,
                                   BOOL forceRefresh, int mode);
    virtual BOOL WINAPI ListCurrentPath(CSalamanderDirectoryAbstract* dir,
                                        CPluginDataInterfaceAbstract*& pluginData,
                                        int& iconsType, BOOL forceRefresh);

    virtual BOOL WINAPI TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason);

    virtual void WINAPI Event(int event, DWORD param);

    virtual void WINAPI ReleaseObject(HWND parent);

    virtual DWORD WINAPI GetSupportedServices();

    virtual BOOL WINAPI GetChangeDriveOrDisconnectItem(const wchar_t* fsName, wchar_t*& title, HICON& icon, BOOL& destroyIcon) override { return FALSE; }
    virtual HICON WINAPI GetFSIcon(BOOL& destroyIcon);
    virtual void WINAPI GetDropEffect(const wchar_t* srcFSPath, const wchar_t* tgtFSPath,
                                      DWORD allowedEffects, DWORD keyState,
                                      DWORD* dropEffect) override {}
    virtual void WINAPI GetFSFreeSpace(CQuadWord* retValue);
    virtual BOOL WINAPI GetNextDirectoryLineHotPath(const wchar_t* text, int pathLen, int& offset) override;
    virtual BOOL WINAPI CompleteDirectoryLineHotPath(CSalamanderStringBuffer* path) override { return TRUE; }
    virtual BOOL WINAPI GetPathForMainWindowTitle(const wchar_t* fsName, int mode, CSalamanderStringBuffer* buf) override { return FALSE; }
    virtual void WINAPI ShowInfoDialog(const wchar_t* fsName, HWND parent) override;
    virtual BOOL WINAPI ExecuteCommandLine(HWND parent, CSalamanderStringBuffer* command, int& selFrom, int& selTo) override;
    BOOL ExecuteCommandLineOwned(HWND parent, std::wstring& command, int& selFrom, int& selTo);
    virtual BOOL WINAPI QuickRename(const wchar_t* fsName, int mode, HWND parent, CFileData& file, BOOL isDir,
                                    CSalamanderStringBuffer* newName, BOOL& cancel) override;
    BOOL QuickRenameOwned(const wchar_t* fsName, int mode, HWND parent, CFileData& file,
                          BOOL isDir, std::wstring& newName, BOOL& cancel);
    virtual void WINAPI AcceptChangeOnPathNotification(const wchar_t* fsName, const wchar_t* path, BOOL includingSubdirs) override;
    virtual BOOL WINAPI CreateDir(const wchar_t* fsName, int mode, HWND parent, CSalamanderStringBuffer* newName, BOOL& cancel) override;
    BOOL CreateDirOwned(const wchar_t* fsName, int mode, HWND parent,
                        std::wstring& newName, BOOL& cancel);
    virtual void WINAPI ViewFile(const wchar_t* fsName, HWND parent,
                                 CSalamanderForViewFileOnFSAbstract* salamander,
                                 CFileData& file) override;
    virtual BOOL WINAPI Delete(const wchar_t* fsName, int mode, HWND parent, int panel,
                               int selectedFiles, int selectedDirs, BOOL& cancelOrError) override;
    virtual BOOL WINAPI CopyOrMoveFromFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                                         int panel, int selectedFiles, int selectedDirs,
                                         CSalamanderStringBuffer* targetPath, BOOL& operationMask,
                                         BOOL& cancelOrHandlePath, HWND dropTarget) override;
    virtual BOOL WINAPI CopyOrMoveFromDiskToFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                                               const wchar_t* sourcePath, SalEnumSelection2 next,
                                               void* nextParam, int sourceFiles, int sourceDirs,
                                               CSalamanderStringBuffer* targetPath, BOOL* invalidPathOrCancel) override;
    virtual BOOL WINAPI ChangeAttributes(const wchar_t* fsName, HWND parent, int panel,
                                         int selectedFiles, int selectedDirs) override;
    virtual void WINAPI ShowProperties(const wchar_t* fsName, HWND parent, int panel,
                                       int selectedFiles, int selectedDirs) override;
    virtual void WINAPI ContextMenu(const wchar_t* fsName, HWND parent, int menuX, int menuY, int type,
                                    int panel, int selectedFiles, int selectedDirs) override;
    virtual BOOL WINAPI OpenFindDialog(const wchar_t* fsName, int panel) override { return FALSE; }
    virtual void WINAPI OpenActiveFolder(const wchar_t* fsName, HWND parent) override {}
    virtual void WINAPI GetAllowedDropEffects(int mode, const wchar_t* tgtFSPath, DWORD* allowedEffects) override {}
    virtual BOOL WINAPI HandleMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult) { return FALSE; }
    virtual BOOL WINAPI GetNoItemsInPanelText(CSalamanderStringBuffer* textBuf) override { return FALSE; }
    virtual void WINAPI ShowSecurityInfo(HWND parent) {}

private:
    BOOL WINAPI CopyOrMoveFromFSOwned(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                                      int panel, int selectedFiles, int selectedDirs,
                                      std::wstring& targetPath, const std::wstring& suppliedMask,
                                      BOOL& operationMask, BOOL& cancelOrHandlePath,
                                      HWND dropTarget);
    BOOL WINAPI CopyOrMoveFromDiskToFSOwned(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                                            const wchar_t* sourcePath, SalEnumSelection2 next,
                                            void* nextParam, int sourceFiles, int sourceDirs,
                                            std::wstring& targetPath, BOOL* invalidPathOrCancel);

public:
    static void EmptyCache();
};

// plugin interface provided to Salamander
extern CPluginInterface PluginInterface;

// opens the configuration dialog; if it already exists, shows a message and returns
void OnConfiguration(HWND hParent);

// opens the About window
void OnAbout(HWND hParent);

/////////////////////////////////////////////////////////////////////////////
// helper functions for RAPI

// CFileInfo is stored in TDirectArray, whose elements are relocated with memmove. Keep the record
// trivially relocatable, but give the semantic relative path dynamic UTF-16 storage through an
// owned pointer. CFileInfoArray releases those pointers before the raw array storage is destroyed.
struct CFileInfo
{
    wchar_t* cFileName;
    DWORD dwFileAttributes;
    DWORD size;
    int block;
};

class CFileInfoArray : public TDirectArray<CFileInfo>
{
public:
    CFileInfoArray(int base, int delta)
        : TDirectArray<CFileInfo>(base, delta)
    {
    }
    ~CFileInfoArray() override;

    BOOL AddOwned(const wchar_t* fileName, DWORD attributes, DWORD size, int block);
};

class CRAPI
{
public:
    static BOOL Init();
    static BOOL ReInit();
    static void UnInit();

    //RAPI
    static BOOL FindAllFilesWide(const wchar_t* path, DWORD flags, LPDWORD foundCount,
                                 RapiNS::LPCE_FIND_DATA* findData, BOOL tryReinit = FALSE);
    static HANDLE FindFirstFileWide(const wchar_t* lpFileName, RapiNS::LPCE_FIND_DATA lpFindFileData, BOOL tryReinit = FALSE);
    static BOOL FindNextFile(HANDLE hFindFile, RapiNS::LPCE_FIND_DATA lpFindFileData);
    static BOOL FindClose(HANDLE hFindFile);

    static DWORD GetFileAttributesWide(const wchar_t* lpFileName, BOOL tryReinit = FALSE);
    static BOOL GetFileTime(HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime, LPFILETIME lpLastWriteTime);
    static BOOL SetFileTime(HANDLE hFile, FILETIME* lpCreationTime, FILETIME* lpLastAccessTime, FILETIME* lpLastWriteTime);
    static DWORD GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh);
    static BOOL GetStoreInformation(RapiNS::LPSTORE_INFORMATION lpsi);

    static HANDLE CreateFileWide(const wchar_t* lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                 LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                 DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);

    static BOOL CloseHandle(HANDLE hObject);

    static BOOL ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead,
                         LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
    static BOOL WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
                          LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);

    static BOOL CreateDirectoryWide(const wchar_t* lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
    static BOOL DeleteFileWide(const wchar_t* lpFileName);
    static BOOL RemoveDirectoryWide(const wchar_t* lpPathName);
    static BOOL SetFileAttributesWide(const wchar_t* lpFileName, DWORD dwFileAttributes);
    static BOOL MoveFileWide(const wchar_t* lpExistingFileName, const wchar_t* lpNewFileName);

    static BOOL CreateProcessWide(const wchar_t* lpApplicationName, const wchar_t* lpCommandLine);

    static BOOL SHGetShortcutTargetWide(const wchar_t* shortcut, std::wstring& target);

    static HRESULT FreeBuffer(LPVOID Buffer);

    static DWORD GetLastError(void);
    static HRESULT RapiGetError(void);

    // Helpers
    static BOOL FindAllFilesInTreeWide(const wchar_t* rootPath, const wchar_t* fileName,
                                       CFileInfoArray& array, int block, BOOL dirFirst);

    // Wide. Each straddles the phone and the PC; which parameter names which
    // machine is documented at the definitions in rapi.cpp, and it is not symmetric:
    //   CopyFileToPC  existing = device, new = PC
    //   CopyFileToCE  existing = PC,     new = device
    //   CopyFile      both = device
    // 'errorFileName' receives whichever of the two arguments failed, so it aliases a caller
    // string and must not outlive it.
    static DWORD CopyFileToPC(const wchar_t* lpExistingFileName, const wchar_t* lpNewFileName, BOOL bFailIfExists, CProgressDlg* dlg, INT64 totalCopied, INT64 totalSize, const wchar_t** errorFileName);
    static DWORD CopyFileToCE(const wchar_t* lpExistingFileName, const wchar_t* lpNewFileName, BOOL bFailIfExists, CProgressDlg* dlg, INT64 totalCopied, INT64 totalSize, const wchar_t** errorFileName);
    // Named CopyFileWide, not CopyFile: CopyFile is a Win32 macro and under UNICODE this
    // declaration silently became CRAPI::CopyFileW. Same trap as DeleteFile/RemoveDirectory
    // on the delete path, and it cost a build there too.
    static DWORD CopyFileWide(const wchar_t* lpExistingFileName, const wchar_t* lpNewFileName, BOOL bFailIfExists, CProgressDlg* dlg, INT64 totalCopied, INT64 totalSize, const wchar_t** errorFileName);

    static DWORD SetFileTimeWide(const wchar_t* fileName, const SYSTEMTIME* creationTime,
                                 const SYSTEMTIME* lastAccessTime, const SYSTEMTIME* lastWriteTime);

    // Wide. 'errBuf'/'errBufSize' were already being written as wchar_t by the
    // LangStr widening while the parameter still said char* - every caller passes NULL/0 so
    // nothing overran, but the type and the body disagreed. errBufSize counts CHARACTERS.
    static BOOL CheckAndCreateDirectory(const wchar_t* dir, HWND parent, BOOL quiet);

    // Wide. The output is a display string - size, date, time - and the date
    // and time come from GetDateFormat/GetTimeFormat under LOCALE_USER_DEFAULT, so the old
    // "ascii-by-construction" note on its CP_ACP conversion was wrong: under a Japanese or
    // Greek user locale those are not ASCII at all.
    static std::wstring GetFileDataWide(const wchar_t* name);

    static BOOL CheckConnection();

    //Implementation
private:
    static DWORD WaitAndDispatch(DWORD nCount, HANDLE* phWait, DWORD dwTimeout, BOOL bOnlySendMessage);
    static HRESULT InitRapi(HANDLE hExit, DWORD dwTimeout);

    static BOOL initialized;
};
