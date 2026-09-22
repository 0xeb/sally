// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define CURRENT_CONFIG_VERSION 1

// menu ID definitions
#define MID_FIND 1
#define MID_NEWKEY 2
#define MID_NEWVAL 3
#define MID_EXPORT 4
#define MID_FOCUS 5

#ifndef REG_QWORD
// see winnt.h
#define REG_QWORD (11) // 64-bit number
#endif

// see winreg.h
#ifndef HKEY_PERFORMANCE_TEXT
#define HKEY_PERFORMANCE_TEXT ((HKEY)0x80000050)
#endif

#ifndef HKEY_PERFORMANCE_NLSTEXT
#define HKEY_PERFORMANCE_NLSTEXT ((HKEY)0x80000060)
#endif

#define MAX_VAL_DATA 32767

// plugin interface object whose methods Salamander invokes
class CPluginInterface;
extern CPluginInterface PluginInterface;
class CPluginInterfaceForMenuExt;
extern CPluginInterfaceForMenuExt InterfaceForMenuExt;

// general Salamander interface - valid from plugin startup until shutdown
extern CSalamanderGeneralAbstract* SG;

extern CSalamanderGUIAbstract* SalGUI;

// FS name assigned by Salamander after loading the plugin
extern std::wstring AssignedFSName;

class CWindowQueueEx : public CWindowQueue
{
public:
    CWindowQueueEx() : CWindowQueue("RegEdt Find Dialogs") {}

    HWND GetLastWnd()
    {
        CS.Enter();
        HWND w = NULL;
        CWindowQueueItem* next = Head;
        if (next != NULL) // find the last window
        {
            while (next->Next)
                next = next->Next;
            w = next->HWindow;
        }
        CS.Leave();
        return w;
    }
};

extern CThreadQueue ThreadQueue;
extern CWindowQueueEx WindowQueue;
extern BOOL AlwaysOnTop;

// ****************************************************************************

struct CCS
{
    CRITICAL_SECTION cs;

    CCS() { InitializeCriticalSection(&cs); }
    ~CCS() { DeleteCriticalSection(&cs); }

    void Enter() { EnterCriticalSection(&cs); }
    void Leave() { LeaveCriticalSection(&cs); }
};

// ****************************************************************************

//char * ErrorStr(char * buf, int error, ...);
BOOL ErrorHelper(HWND parent, const wchar_t* message, int lastError, va_list arglist);
BOOL Error(HWND parent, int error, ...);
BOOL Error(HWND parent, const wchar_t* error, ...);
BOOL Error(int error, ...);
BOOL ErrorL(int lastError, HWND parent, int error, ...);
BOOL ErrorL(int lastError, int error, ...);
int SalPrintf(char* buffer, unsigned count, const char* format, ...);
int SalPrintfW(LPWSTR buffer, unsigned count, LPCWSTR format, ...);

// ****************************************************************************
//
// Plug-in interface
//

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

    virtual CPluginInterfaceForArchiverAbstract* WINAPI GetInterfaceForArchiver() { return NULL; };
    virtual CPluginInterfaceForViewerAbstract* WINAPI GetInterfaceForViewer() { return NULL; }
    virtual CPluginInterfaceForMenuExtAbstract* WINAPI GetInterfaceForMenuExt();
    virtual CPluginInterfaceForFSAbstract* WINAPI GetInterfaceForFS();
    virtual CPluginInterfaceForThumbLoaderAbstract* WINAPI GetInterfaceForThumbLoader() { return NULL; }

    virtual void WINAPI Event(int event, DWORD param);
    virtual void WINAPI ClearHistory(HWND parent);
    virtual void WINAPI AcceptChangeOnPathNotification(const wchar_t* path, BOOL includingSubdirs) {}

    virtual void WINAPI PasswordManagerEvent(HWND parent, int event) {}
};

// ****************************************************************************

class CPluginInterfaceForMenuExt : public CPluginInterfaceForMenuExtAbstract
{
    // used to focus items from a non-main thread
    std::wstring Path;
    std::wstring Name;

public:
    BOOL PostFocusCommand(const wchar_t* path, const wchar_t* name);

    virtual DWORD WINAPI GetMenuItemState(int id, DWORD eventMask);
    virtual BOOL WINAPI ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander, HWND parent,
                                        int id, DWORD eventMask);
    virtual BOOL WINAPI HelpForMenuItem(HWND parent, int id);
    virtual void WINAPI BuildMenu(HWND parent, CSalamanderBuildMenuAbstract* salamander) {}
};

// ****************************************************************************

extern HIMAGELIST ImageList;

extern const wchar_t* Str_REG_BINARY;
extern const wchar_t* Str_REG_DWORD;
extern const wchar_t* Str_REG_DWORD_BIG_ENDIAN;
extern const wchar_t* Str_REG_EXPAND_SZ;
extern const wchar_t* Str_REG_LINK;
extern const wchar_t* Str_REG_MULTI_SZ;
extern const wchar_t* Str_REG_NONE;
extern const wchar_t* Str_REG_QWORD;
extern const wchar_t* Str_REG_RESOURCE_LIST;
extern const wchar_t* Str_REG_SZ;
extern const wchar_t* Str_REG_FULL_RESOURCE_DESCRIPTOR;
extern const wchar_t* Str_REG_RESOURCE_REQUIREMENTS_LIST;

extern int Len_REG_BINARY;
extern int Len_REG_DWORD;
extern int Len_REG_DWORD_BIG_ENDIAN;
extern int Len_REG_EXPAND_SZ;
extern int Len_REG_LINK;
extern int Len_REG_MULTI_SZ;
extern int Len_REG_NONE;
extern int Len_REG_QWORD;
extern int Len_REG_RESOURCE_LIST;
extern int Len_REG_SZ;
extern int Len_REG_FULL_RESOURCE_DESCRIPTOR;
extern int Len_REG_RESOURCE_REQUIREMENTS_LIST;

struct CRegTypeText
{
    const wchar_t* Text;
    DWORD Type;
    unsigned CanCreate : 1;
    unsigned CanEdit : 1;
};

extern CRegTypeText RegTypeTexts[];

extern std::wstring KeyText;

struct CPredefinedHKey
{
    HKEY HKey;
    const WCHAR* KeyName;
};

extern CPredefinedHKey PredefinedHKeys[];

extern std::wstring RecentFullPath;

BOOL InitFS();
void ReleaseFS();

class CPluginInterfaceForFS : public CPluginInterfaceForFSAbstract
{
public:
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

//****************************************************************************
//
// CTopIndexMem
//
// listbox top-index cache in the panel - used by CPluginFSInterface to keep
// ExecuteOnFS behavior correct (preserve the top index when entering and
// leaving a subdirectory)

#define TOP_INDEX_MEM_SIZE 50 // number of remembered top indexes (levels), at least 1

class CTopIndexMem
{
protected:
    // path for the last remembered top index
    std::wstring Path;
    int TopIndexes[TOP_INDEX_MEM_SIZE]; // cached top indexes
    int TopIndexesCount;                // number of cached top indexes

public:
    CTopIndexMem() { Clear(); }
    void Clear()
    {
        Path.clear();
        TopIndexesCount = 0;
    } // clear the cache
    void Push(LPCWSTR path, int topIndex);        // store the top index for the given path
    BOOL FindAndPop(LPCWSTR path, int& topIndex); // find the top index for the given path, FALSE -> not found
};

// ****************************************************************************

void EditValue(int root, LPWSTR key, LPWSTR name, BOOL rawEdit);

BOOL SafeOpenKey(int root, const wchar_t* key, DWORD sam, HKEY& hKey,
                 int errorTitle, LPBOOL skip, LPBOOL skipAll);

BOOL SafeQueryInfoKey(HKEY hKey, int root, const wchar_t* key, std::wstring* className,
                      LPDWORD maxData, FILETIME* time,
                      int errorTitle, BOOL& skip, BOOL& skipAll);

BOOL CopyOrMoveKey(int sourceRoot, std::wstring& source, int targetRoot, std::wstring& target,
                   BOOL move, BOOL& skip,
                   BOOL& skipAllErrors,
                   BOOL& skipAllOverwrites, BOOL& overwriteAll,
                   BOOL& skipAllClassNames, std::vector<std::wstring>& stack);

BOOL CopyOrMoveValue(int sourceRoot, const wchar_t* sourcePath, const wchar_t* sourceName,
                     int targetRoot, const wchar_t* targetPath, const wchar_t* targetName,
                     BOOL move, LPBOOL skip,
                     LPBOOL skipAllErrors, LPBOOL skipAllOverwrites,
                     LPBOOL overwriteAll);

class CChangeMonitor;

class CPluginFSInterface : public CPluginFSInterfaceAbstract
{
public:
    int CurrentKeyRoot;
    std::wstring CurrentKeyName;
    CTopIndexMem TopIndexMem; // top-index cache for ExecuteOnFS()
    BOOL FocusFirstNewItem;

protected:
    BOOL PathError;
    std::wstring NewPath;
    BOOL NewPathValid;
    BOOL FirstChangePath; // ignore errors on the first ChangePath call

    BOOL ListCurrentPathCore(CSalamanderDirectoryAbstract* dir,
                             CPluginDataInterfaceAbstract*& pluginData,
                             int& iconsType, BOOL forceRefresh);

public:
    CPluginFSInterface();
    ~CPluginFSInterface();

    BOOL IsGood() { return TRUE; }

    BOOL SetNewPath(const wchar_t* newPath);

    virtual BOOL WINAPI GetCurrentPath(CSalamanderStringBuffer* userPart) override;
    std::wstring GetCurrentPathOwned() const;

    virtual BOOL WINAPI GetFullName(CFileData& file, int isDir,
                                    CSalamanderStringBuffer* fullName) override;
    BOOL ResolveFullFSPath(std::wstring& path, BOOL& success);
    virtual BOOL WINAPI GetFullFSPath(HWND parent, const wchar_t* fsName,
                                      CSalamanderStringBuffer* path, BOOL& success) override;

    virtual BOOL WINAPI GetRootPath(CSalamanderStringBuffer* userPart) override;
    virtual BOOL WINAPI IsCurrentPath(int currentFSNameIndex, int fsNameIndex,
                                      const wchar_t* userPart) override;
    virtual BOOL WINAPI IsOurPath(int currentFSNameIndex, int fsNameIndex,
                                  const wchar_t* userPart) override;
    virtual BOOL WINAPI ChangePath(int currentFSNameIndex, CSalamanderStringBuffer* fsName,
                                   int fsNameIndex, const wchar_t* userPart,
                                   CSalamanderStringBuffer* cutFileName, BOOL* pathWasCut,
                                   BOOL forceRefresh, int mode) override;

    virtual BOOL WINAPI ListCurrentPath(CSalamanderDirectoryAbstract* dir,
                                        CPluginDataInterfaceAbstract*& pluginData,
                                        int& iconsType, BOOL forceRefresh);

    virtual BOOL WINAPI TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason);

    virtual void WINAPI Event(int event, DWORD param);

    virtual void WINAPI ReleaseObject(HWND parent) {}

    virtual DWORD WINAPI GetSupportedServices()
    {
        return FS_SERVICE_GETFSICON | FS_SERVICE_CREATEDIR | FS_SERVICE_DELETE |
               FS_SERVICE_CONTEXTMENU | FS_SERVICE_GETNEXTDIRLINEHOTPATH |
               FS_SERVICE_ACCEPTSCHANGENOTIF | FS_SERVICE_QUICKRENAME |
               FS_SERVICE_COPYFROMFS | FS_SERVICE_MOVEFROMFS | FS_SERVICE_VIEWFILE |
               FS_SERVICE_GETPATHFORMAINWNDTITLE | FS_SERVICE_OPENFINDDLG |
               FS_SERVICE_OPENACTIVEFOLDER;
    }

    virtual BOOL WINAPI GetChangeDriveOrDisconnectItem(const wchar_t* fsName, wchar_t*& title, HICON& icon, BOOL& destroyIcon) { return FALSE; }

    virtual HICON WINAPI GetFSIcon(BOOL& destroyIcon);

    virtual void WINAPI GetDropEffect(const wchar_t* srcFSPath, const wchar_t* tgtFSPath,
                                      DWORD allowedEffects, DWORD keyState,
                                      DWORD* dropEffect) {}

    virtual void WINAPI GetFSFreeSpace(CQuadWord* retValue) { *retValue = CQuadWord(-1, -1); }

    virtual BOOL WINAPI GetNextDirectoryLineHotPath(const wchar_t* text, int pathLen, int& offset);
    virtual BOOL WINAPI CompleteDirectoryLineHotPath(CSalamanderStringBuffer* path) { return TRUE; }
    virtual BOOL WINAPI GetPathForMainWindowTitle(const wchar_t* fsName, int mode, CSalamanderStringBuffer* buf) { return FALSE; }

    virtual void WINAPI ShowInfoDialog(const wchar_t* fsName, HWND parent) { ; }

    virtual BOOL WINAPI ExecuteCommandLine(HWND parent, CSalamanderStringBuffer* command, int& selFrom, int& selTo) { return FALSE; }

    virtual BOOL WINAPI QuickRename(const wchar_t* fsName, int mode, HWND parent, CFileData& file, BOOL isDir,
                                    CSalamanderStringBuffer* newName, BOOL& cancel);
    BOOL QuickRenameOwned(const wchar_t* fsName, int mode, HWND parent, CFileData& file,
                          BOOL isDir, std::wstring& newName, BOOL& cancel);

    virtual void WINAPI AcceptChangeOnPathNotification(const wchar_t* fsName, const wchar_t* path, BOOL includingSubdirs);

    virtual BOOL WINAPI CreateDir(const wchar_t* fsName, int mode, HWND parent, CSalamanderStringBuffer* newName, BOOL& cancel);
    BOOL CreateDirOwned(const wchar_t* fsName, int mode, HWND parent,
                        std::wstring& newName, BOOL& cancel);

    virtual void WINAPI ViewFile(const wchar_t* fsName, HWND parent,
                                 CSalamanderForViewFileOnFSAbstract* salamander,
                                 CFileData& file);

    BOOL DeleteKey(const std::wstring& keyName, BOOL& skip, BOOL& skipAllErrors,
                   std::vector<std::wstring>& stack);
    BOOL DeleteCore(const wchar_t* fsName, int mode, HWND parent, int panel,
                    int selectedFiles, int selectedDirs, BOOL& cancelOrError);
    BOOL CopyOrMoveFromFSCore(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                              int panel, int selectedFiles, int selectedDirs,
                              CSalamanderStringBuffer* targetPath, BOOL& operationMask,
                              BOOL& cancelOrHandlePath, HWND dropTarget);

    virtual BOOL WINAPI Delete(const wchar_t* fsName, int mode, HWND parent, int panel,
                               int selectedFiles, int selectedDirs, BOOL& cancelOrError);

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
                                       int selectedFiles, int selectedDirs) { ; }

    virtual void WINAPI ContextMenu(const wchar_t* fsName, HWND parent, int menuX, int menuY, int type,
                                    int panel, int selectedFiles, int selectedDirs);

    virtual BOOL WINAPI OpenFindDialog(const wchar_t* fsName, int panel);

    virtual void WINAPI OpenActiveFolder(const wchar_t* fsName, HWND parent);

    virtual void WINAPI GetAllowedDropEffects(int mode, const wchar_t* tgtFSPath, DWORD* allowedEffects) {}

    virtual BOOL WINAPI HandleMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult) { return FALSE; }

    virtual BOOL WINAPI GetNoItemsInPanelText(CSalamanderStringBuffer* textBuf) { return FALSE; }

    virtual void WINAPI ShowSecurityInfo(HWND parent) {}

    BOOL EditNewFile();
};

// ****************************************************************************

#define MAX_DATASIZE 200

struct CPluginData
{
    WCHAR* Name; // name in Unicode
    DWORD Type;
    DWORD_PTR Data;
    unsigned Allocated : 1;
    unsigned char DataSize;

    CPluginData(WCHAR* name, DWORD type, DWORD_PTR data = NULL, unsigned allocated = 0,
                unsigned char dataSize = 4)
    {
        Name = name;
        Type = type;
        Data = data;
        Allocated = allocated > 0;
        DataSize = dataSize;
    }

    ~CPluginData()
    {
        if (Allocated && Data)
            free((void*)Data);
        if (Name)
            free(Name);
    }
};

class CPluginDataInterface : public CPluginDataInterfaceAbstract
{
    virtual BOOL WINAPI CallReleaseForFiles() { return TRUE; }
    virtual BOOL WINAPI CallReleaseForDirs() { return TRUE; }

    virtual void WINAPI ReleasePluginData(CFileData& file, BOOL isDir)
    {
        delete ((CPluginData*)file.PluginData);
    }

    virtual void WINAPI GetFileDataForUpDir(const wchar_t* archivePath, CFileData& upDir) { ; }
    virtual BOOL WINAPI GetFileDataForNewDir(const wchar_t* dirName, CFileData& dir) { return FALSE; }

    virtual HIMAGELIST WINAPI GetSimplePluginIcons(int iconSize);

    virtual BOOL WINAPI HasSimplePluginIcon(CFileData& file, BOOL isDir) { return TRUE; }

    virtual HICON WINAPI GetPluginIcon(const CFileData* file, int iconSize, BOOL& destroyIcon) { return NULL; }

    virtual int WINAPI CompareFilesFromFS(const CFileData* file1, const CFileData* file2)
    {
        return wcscmp(file1->Name, file2->Name);
    }

    virtual void WINAPI SetupView(BOOL leftPanel, CSalamanderViewAbstract* view, const wchar_t* archivePath,
                                  const CFileData* upperDir);
    virtual void WINAPI ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth);
    virtual void WINAPI ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column, int newWidth);

    virtual BOOL WINAPI GetInfoLineContent(int panel, const CFileData* file, BOOL isDir, int selectedFiles,
                                           int selectedDirs, BOOL displaySize, const CQuadWord& selectedSize,
                                            CSalamanderStringBuffer* buffer, CSalamanderTextRangeBuffer* hotTexts);

    virtual BOOL WINAPI CanBeCopiedToClipboard() { return TRUE; }

    virtual BOOL WINAPI GetByteSize(const CFileData* file, BOOL isDir, CQuadWord* size) { return FALSE; }
    virtual BOOL WINAPI GetLastWriteDate(const CFileData* file, BOOL isDir, SYSTEMTIME* date) { return FALSE; }
    virtual BOOL WINAPI GetLastWriteTime(const CFileData* file, BOOL isDir, SYSTEMTIME* time) { return FALSE; }
};

// ****************************************************************************

extern HINSTANCE DLLInstance; // handle to SPL - language-independent resources
extern HINSTANCE HLanguage;   // handle to SLG - language-dependent resources
extern BOOL WindowsVistaAndLater;

std::wstring LangStr(int resID);
std::wstring LoadStrW(int resID);
