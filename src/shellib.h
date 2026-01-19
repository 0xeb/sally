// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

// library initialization
BOOL InitializeShellib();

// library release
void ReleaseShellib();

// safe call to IContextMenu2::GetCommandString() where MS sometimes crashes
HRESULT AuxGetCommandString(IContextMenu2* menu, UINT_PTR idCmd, UINT uType, UINT* pReserved, LPSTR pszName, UINT cchMax);

// callback that returns names of selected files for creating the next interface
typedef const char* (*CEnumFileNamesFunction)(int index, void* param);

// creates a data object for drag&drop operations on selected files and directories from rootDir
IDataObject* CreateIDataObject(HWND hOwnerWindow, const char* rootDir, int files,
                               CEnumFileNamesFunction nextFile, void* param);

// creates a context menu interface for selected files and directories from rootDir
IContextMenu2* CreateIContextMenu2(HWND hOwnerWindow, const char* rootDir, int files,
                                   CEnumFileNamesFunction nextFile, void* param);

// creates a context menu interface for the specified directory
IContextMenu2* CreateIContextMenu2(HWND hOwnerWindow, const char* dir);

// does the specified directory or file have a drop target?
BOOL HasDropTarget(const char* dir);

// creates a drop target for drag&drop operations into the specified directory or file
IDropTarget* CreateIDropTarget(HWND hOwnerWindow, const char* dir);

// opens the special folder window
void OpenSpecFolder(HWND hOwnerWindow, int specFolder);

// opens the 'dir' folder window and focuses 'item'
void OpenFolderAndFocusItem(HWND hOwnerWindow, const char* dir, const char* item);

// opens the browse dialog and selects a path (can be limited to a network path)
// hCenterWindow - window to which the dialog will be centered
BOOL GetTargetDirectory(HWND parent, HWND hCenterWindow, const char* title, const char* comment,
                        char* path, BOOL onlyNet = FALSE, const char* initDir = NULL);

// detects whether it is a NetHood path (directory with target.lnk),
// optionally resolves target.lnk and returns the path in 'path'; 'path' is an in/out path
// (min. MAX_PATH znaku)
void ResolveNetHoodPath(char* path);

class CMenuNew;

// returns the New menu - handle of popup-menu and IContextMenu through which commands run
void GetNewOrBackgroundMenu(HWND hOwnerWindow, const char* dir, CMenuNew* menu,
                            int minCmd, int maxCmd, BOOL backgoundMenu);

struct CDragDropOperData
{
    char SrcPath[MAX_PATH];     // source path common to all files/dirs from Names ("" == failed conversion from Unicode)
    TIndirectArray<char> Names; // sorted allocated names of files/dirs (CF_HDROP does not distinguish file vs dir) ("" == failed conversion from Unicode)

    CDragDropOperData() : Names(200, 200) { SrcPath[0] = 0; }
};

// determines whether 'pDataObject' contains disk files and dirs from a single path,
// optionally stores their names in 'namesList' (if not NULL)
BOOL IsSimpleSelection(IDataObject* pDataObject, CDragDropOperData* namesList);

// obtains the name for 'pidl' via GetDisplayNameOf(flags) (shortens the ID-list by one ID, gets
// the folder for the shortened ID-list from desktop, then calls GetDisplayNameOf for the last
// ID with the specified 'flags'); on success returns TRUE + name in 'name' (buffer size 'nameSize');
// does not deallocate 'pidl'; 'alloc' is the iface obtained via CoGetMalloc
BOOL GetSHObjectName(ITEMIDLIST* pidl, DWORD flags, char* name, int nameSize, IMalloc* alloc);

// TRUE = drag&drop effect was calculated in plugin FS, so there is no need to force Copy
// in CImpIDropSource::GiveFeedback
extern BOOL DragFromPluginFSEffectIsFromPlugin;

//*****************************************************************************
//
// CImpIDropSource
//
// Basic version of the object, behaves normally (default cursors, etc.).
//
// Exception: when dragging from plugin FS (with possible Copy and Move effects) into Explorer
// to a disk with a TEMP directory, Move is offered by default instead of Copy (which makes no
// sense; users expect Copy), so we force this case by showing a different cursor than dwEffect
// in GiveFeedback and then taking the final effect from the last cursor shape instead of the
// result of DoDragDrop.

class CImpIDropSource : public IDropSource
{
private:
    long RefCount;
    DWORD MouseButton; // -1 = uninitialized value, otherwise MK_LBUTTON or MK_RBUTTON

public:
    // last effect returned by GiveFeedback - we track it because
    // DoDragDrop does not return dwEffect == DROPEFFECT_MOVE; for MOVE it returns dwEffect == 0,
    // for reasons see "Handling Shell Data Transfer Scenarios" section "Handling Optimized Move Operations":
    // http://msdn.microsoft.com/en-us/library/windows/desktop/bb776904%28v=vs.85%29.aspx
    // (short: optimized Move is used, which means no copy to target followed by deletion of the
    //          original; so the source does not accidentally delete the original (it may not yet
    //          be moved), it gets DROPEFFECT_NONE or DROPEFFECT_COPY as the result)
    DWORD LastEffect;

    BOOL DragFromPluginFSWithCopyAndMove; // dragging from plugin FS with possible Copy and Move, details above

public:
    CImpIDropSource(BOOL dragFromPluginFSWithCopyAndMove)
    {
        RefCount = 1;
        MouseButton = -1;
        LastEffect = -1;
        DragFromPluginFSWithCopyAndMove = dragFromPluginFSWithCopyAndMove;
    }

    virtual ~CImpIDropSource()
    {
        if (RefCount != 0)
            TRACE_E("Preliminary destruction of this object.");
    }

    STDMETHOD(QueryInterface)
    (REFIID, void FAR * FAR*);
    STDMETHOD_(ULONG, AddRef)
    (void) { return ++RefCount; }
    STDMETHOD_(ULONG, Release)
    (void)
    {
        if (--RefCount == 0)
        {
            delete this;
            return 0; // must not touch the object, it no longer exists
        }
        return RefCount;
    }

    STDMETHOD(GiveFeedback)
    (DWORD dwEffect)
    {
        if (DragFromPluginFSWithCopyAndMove && !DragFromPluginFSEffectIsFromPlugin)
        {
            BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if ((!shiftPressed || controlPressed) && (dwEffect & DROPEFFECT_MOVE))
            { // Copy should be done, but Move is offered -> force this case, show Copy cursor and set LastEffect to Copy
                LastEffect = DROPEFFECT_COPY;
                SetCursor(LoadCursor(HInstance, MAKEINTRESOURCE(IDC_DRAGCOPYEFFECT)));
                return S_OK;
            }
        }
        DragFromPluginFSEffectIsFromPlugin = FALSE;
        LastEffect = dwEffect;
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

    STDMETHOD(QueryContinueDrag)
    (BOOL fEscapePressed, DWORD grfKeyState)
    {
        DWORD mb = grfKeyState & (MK_LBUTTON | MK_RBUTTON);
        if (mb == 0)
            return DRAGDROP_S_DROP;
        if (MouseButton == -1)
            MouseButton = mb;
        if (fEscapePressed || MouseButton != mb)
            return DRAGDROP_S_CANCEL;
        return S_OK;
    }
};

//*****************************************************************************
//
// CImpDropTarget
//
// calls defined callbacks to obtain drop target (directory),
// drop notification or ESC,
// leaves the rest of the operations to the system IDropTarget object from IShellFolder

// record used in data for copy and move callback
struct CCopyMoveRecord
{
    char* FileName;     // ANSI filename (may have lossy conversion for Unicode names)
    char* MapName;
    wchar_t* FileNameW; // Wide filename (preserved for Unicode support, NULL if not needed)

    CCopyMoveRecord(const char* fileName, const char* mapName);
    CCopyMoveRecord(const wchar_t* fileName, const char* mapName);
    CCopyMoveRecord(const char* fileName, const wchar_t* mapName);
    CCopyMoveRecord(const wchar_t* fileName, const wchar_t* mapName);
    ~CCopyMoveRecord();

    char* AllocChars(const char* name);
    char* AllocChars(const wchar_t* name);
    wchar_t* AllocWideChars(const wchar_t* name);
    bool HasWideFileName() const { return FileNameW != NULL; }
};

// data for copy and move callback
class CCopyMoveData : public TIndirectArray<CCopyMoveRecord>
{
public:
    BOOL MakeCopyOfName; // TRUE if it should try "Copy of..." when target already exists

public:
    CCopyMoveData(int base, int delta) : TIndirectArray<CCopyMoveRecord>(base, delta)
    {
        MakeCopyOfName = FALSE;
    }
};

// callback for copy and move operations, handles destruction of 'data'
typedef BOOL (*CDoCopyMove)(BOOL copy, char* targetDir, CCopyMoveData* data,
                            void* param);

// callback for drag&drop operations; 'copy' is TRUE/FALSE (copy/move), 'toArchive' is TRUE/FALSE
// (to archive/FS), 'archiveOrFSName' (may be NULL if the info should be obtained from the panel)
// is the archive file name or FS-name, 'archivePathOrUserPart' is a path in the archive or FS
// user-part path, 'data' contains description of source files/dirs, the function handles destruction
// of the 'data' object, 'param' is the parameter passed to CImpDropTarget constructor
typedef void (*CDoDragDropOper)(BOOL copy, BOOL toArchive, const char* archiveOrFSName,
                                const char* archivePathOrUserPart, CDragDropOperData* data,
                                void* param);

// callback that returns target directory for point 'pt'
typedef const char* (*CGetCurDir)(POINTL& pt, void* param, DWORD* pdwEffect, BOOL rButton,
                                  BOOL& isTgtFile, DWORD keyState, int& tgtType, int srcType);

// callback notifying end of drop operation, drop == FALSE on ESC
typedef void (*CDropEnd)(BOOL drop, BOOL shortcuts, void* param, BOOL ownRutine,
                         BOOL isFakeDataObject, int tgtType);

// callback for query before completing the operation (drop)
typedef BOOL (*CConfirmDrop)(DWORD& effect, DWORD& defEffect, DWORD& grfKeyState);

// callback notifying mouse enter/leave of target
typedef void (*CEnterLeaveDrop)(BOOL enter, void* param);

// callback that allows use of our routines for copy/move
typedef BOOL (*CUseOwnRutine)(IDataObject* pDataObject);

// callback for determining default drop effect when dragging FS to FS
typedef void (*CGetFSToFSDropEffect)(const char* srcFSPath, const char* tgtFSPath,
                                     DWORD allowedEffects, DWORD keyState,
                                     DWORD* dropEffect, void* param);

enum CIDTTgtType
{
    idtttWindows,          // files/dirs from Windows path to Windows path
    idtttArchive,          // files/dirs from Windows path to archive
    idtttPluginFS,         // files/dirs from Windows path to FS
    idtttArchiveOnWinPath, // archiv na windowsove ceste (drop=pack to archive)
    idtttFullPluginFSPath, // FS to FS
};

class CImpDropTarget : public IDropTarget
{
private:
    long RefCount;
    HWND OwnerWindow;
    IDataObject* OldDataObject;
    BOOL OldDataObjectIsFake;
    int OldDataObjectIsSimple;                 // -1 (unknown value), TRUE/FALSE = is/is not simple (all names on one path)
    int OldDataObjectSrcType;                  // 0 (unknown type), 1/2 = archive/FS
    char OldDataObjectSrcFSPath[2 * MAX_PATH]; // only for FS type: source FS path

    CDoCopyMove DoCopyMove;
    void* DoCopyMoveParam;

    CDoDragDropOper DoDragDropOper;
    void* DoDragDropOperParam;

    CGetCurDir GetCurDir;
    void* GetCurDirParam;

    CDropEnd DropEnd;
    void* DropEndParam;

    CConfirmDrop ConfirmDrop;
    BOOL* ConfirmDropEnable;

    int TgtType; // values see CIDTTgtType; idtttWindows also for archives and FS without ability to drop current dataobject
    IDropTarget* CurDirDropTarget;
    char CurDir[2 * MAX_PATH];

    CEnterLeaveDrop EnterLeaveDrop;
    void* EnterLeaveDropParam;

    BOOL RButton; // action by right mouse button?

    CUseOwnRutine UseOwnRutine;

    DWORD LastEffect; // last effect found in DragEnter or DragOver (-1 => invalid)

    CGetFSToFSDropEffect GetFSToFSDropEffect;
    void* GetFSToFSDropEffectParam;

public:
    CImpDropTarget(HWND ownerWindow, CDoCopyMove doCopyMove, void* doCopyMoveParam,
                   CGetCurDir getCurDir, void* getCurDirParam, CDropEnd dropEnd,
                   void* dropEndParam, CConfirmDrop confirmDrop, BOOL* confirmDropEnable,
                   CEnterLeaveDrop enterLeaveDrop, void* enterLeaveDropParam,
                   CUseOwnRutine useOwnRutine, CDoDragDropOper doDragDropOper,
                   void* doDragDropOperParam, CGetFSToFSDropEffect getFSToFSDropEffect,
                   void* getFSToFSDropEffectParam)
    {
        RefCount = 1;
        OwnerWindow = ownerWindow;
        DoCopyMove = doCopyMove;
        DoCopyMoveParam = doCopyMoveParam;
        DoDragDropOper = doDragDropOper;
        DoDragDropOperParam = doDragDropOperParam;
        GetCurDir = getCurDir;
        GetCurDirParam = getCurDirParam;
        TgtType = idtttWindows;
        CurDirDropTarget = NULL;
        CurDir[0] = 0;
        DropEnd = dropEnd;
        DropEndParam = dropEndParam;
        OldDataObject = NULL;
        OldDataObjectIsFake = FALSE;
        OldDataObjectIsSimple = -1; // unknown value
        OldDataObjectSrcType = 0;   // unknown type
        OldDataObjectSrcFSPath[0] = 0;
        ConfirmDrop = confirmDrop;
        ConfirmDropEnable = confirmDropEnable;
        RButton = FALSE;
        EnterLeaveDrop = enterLeaveDrop;
        EnterLeaveDropParam = enterLeaveDropParam;
        UseOwnRutine = useOwnRutine;
        LastEffect = -1;
        GetFSToFSDropEffect = getFSToFSDropEffect;
        GetFSToFSDropEffectParam = getFSToFSDropEffectParam;
    }
    virtual ~CImpDropTarget()
    {
        if (RefCount != 0)
            TRACE_E("Preliminary destruction of this object.");
        if (CurDirDropTarget != NULL)
            CurDirDropTarget->Release();
    }

    void SetDirectory(const char* path, DWORD grfKeyState, POINTL pt,
                      DWORD* effect, IDataObject* dataObject, BOOL tgtIsFile, int tgtType);
    BOOL TryCopyOrMove(BOOL copy, IDataObject* pDataObject, UINT CF_FileMapA,
                       UINT CF_FileMapW, BOOL cfFileMapA, BOOL cfFileMapW);
    BOOL ProcessClipboardData(BOOL copy, const DROPFILES* data, const char* mapA,
                              const wchar_t* mapW);

    STDMETHOD(QueryInterface)
    (REFIID, void FAR * FAR*);
    STDMETHOD_(ULONG, AddRef)
    (void) { return ++RefCount; }
    STDMETHOD_(ULONG, Release)
    (void)
    {
        if (--RefCount == 0)
        {
            delete this;
            return 0; // nesmime sahnout do objektu, uz neexistuje
        }
        return RefCount;
    }

    STDMETHOD(DragEnter)
    (IDataObject* pDataObject, DWORD grfKeyState,
     POINTL pt, DWORD* pdwEffect);
    STDMETHOD(DragOver)
    (DWORD grfKeyState, POINTL pt, DWORD* pdwEffect);
    STDMETHOD(DragLeave)
    ();
    STDMETHOD(Drop)
    (IDataObject* pDataObject, DWORD grfKeyState, POINTL pt,
     DWORD* pdwEffect);
};

struct IShellFolder;
struct IContextMenu;
struct IContextMenu2;

class CMenuNew
{
protected:
    IContextMenu2* Menu2; // menu-interface 2
    HMENU Menu;           // submenu New

public:
    CMenuNew() { Init(); }
    ~CMenuNew() { Release(); }

    void Init()
    {
        Menu2 = NULL;
        Menu = NULL;
    }

    void Set(IContextMenu2* menu2, HMENU menu)
    {
        if (menu == NULL)
            return; // is-not-set
        Menu2 = menu2;
        Menu = menu;
    }

    BOOL MenuIsAssigned() { return Menu != NULL; }

    HMENU GetMenu() { return Menu; }
    IContextMenu2* GetMenu2() { return Menu2; }

    void Release();
    void ReleaseBody();
};

//
//*****************************************************************************
// CTextDataObject
//

class CTextDataObject : public IDataObject
{
private:
    long RefCount;
    HGLOBAL Data;

public:
    CTextDataObject(HGLOBAL data)
    {
        RefCount = 1;
        Data = data;
    }
    virtual ~CTextDataObject()
    {
        if (RefCount != 0)
            TRACE_E("Preliminary destruction of this object.");
        NOHANDLES(GlobalFree(Data));
    }

    STDMETHOD(QueryInterface)
    (REFIID, void FAR * FAR*);
    STDMETHOD_(ULONG, AddRef)
    (void) { return ++RefCount; }
    STDMETHOD_(ULONG, Release)
    (void)
    {
        if (--RefCount == 0)
        {
            delete this;
            return 0; // nesmime sahnout do objektu, uz neexistuje
        }
        return RefCount;
    }

    STDMETHOD(GetData)
    (FORMATETC* formatEtc, STGMEDIUM* medium);

    STDMETHOD(GetDataHere)
    (FORMATETC* pFormatetc, STGMEDIUM* pmedium)
    {
        return E_NOTIMPL;
    }

    STDMETHOD(QueryGetData)
    (FORMATETC* formatEtc)
    {
        if (formatEtc == NULL)
            return E_INVALIDARG;
        if ((formatEtc->cfFormat == CF_TEXT || formatEtc->cfFormat == CF_UNICODETEXT) &&
            (formatEtc->tymed & TYMED_HGLOBAL))
        {
            return S_OK;
        }
        return (formatEtc->tymed & TYMED_HGLOBAL) ? DV_E_FORMATETC : DV_E_TYMED;
    }

    STDMETHOD(GetCanonicalFormatEtc)
    (FORMATETC* pFormatetcIn, FORMATETC* pFormatetcOut)
    {
        return E_NOTIMPL;
    }

    STDMETHOD(SetData)
    (FORMATETC* pFormatetc, STGMEDIUM* pmedium, BOOL fRelease)
    {
        return E_NOTIMPL;
    }

    STDMETHOD(EnumFormatEtc)
    (DWORD dwDirection, IEnumFORMATETC** ppenumFormatetc)
    {
        return E_NOTIMPL;
    }

    STDMETHOD(DAdvise)
    (FORMATETC* pFormatetc, DWORD advf, IAdviseSink* pAdvSink,
     DWORD* pdwConnection)
    {
        return E_NOTIMPL;
    }

    STDMETHOD(DUnadvise)
    (DWORD dwConnection)
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    STDMETHOD(EnumDAdvise)
    (IEnumSTATDATA** ppenumAdvise)
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }
};

// uvolni CopyMoveData
void DestroyCopyMoveData(CCopyMoveData* data);
