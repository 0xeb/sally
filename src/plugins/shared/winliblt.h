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

// "light" version of WinLib

#pragma once

// macros to suppress unnecessary parts of WinLibLT (easier compilation):
// ENABLE_PROPERTYDIALOG - if defined, property sheet dialog can be used (CPropertyDialog)

// set custom texts for WinLib
void SetWinLibStrings(const char* invalidNumber, // "not a number" (for number transfer buffers)
                      const char* error);        // title "error" (for number transfer buffers)

// must be called before using WinLib; 'pluginName' is the plugin name (e.g. "DEMOPLUG"),
// used to distinguish universal window class names (must differ between plugins,
// otherwise class name collisions occur and WinLib will not work - only the first started
// plugin will work); 'dllInstance' is the plugin module (used when registering universal WinLib classes)
BOOL InitializeWinLib(const char* pluginName, HINSTANCE dllInstance);
// must be called after using WinLib; 'dllInstance' is the plugin module (used when unregistering
// the universal WinLib classes)
void ReleaseWinLib(HINSTANCE dllInstance);

// callback type for connecting to HTML help
typedef void(WINAPI* FWinLibLTHelpCallback)(HWND hWindow, UINT helpID);

// set callback for connecting to HTML help
void SetupWinLibHelp(FWinLibLTHelpCallback helpCallback);

// constants for WinLib strings (internal use in WinLib only)
enum CWLS
{
    WLS_INVALID_NUMBER,
    WLS_ERROR,

    WLS_COUNT
};

extern char CWINDOW_CLASSNAME[100];  // universal window class name
extern char CWINDOW_CLASSNAME2[100]; // universal window class name - does not have CS_VREDRAW | CS_HREDRAW

// ****************************************************************************

enum CObjectOrigin // used when destroying windows and dialogs
{
    ooAllocated, // deallocated on WM_DESTROY
    ooStatic,    // HWindow set to NULL on WM_DESTROY
    ooStandard   // for modal dlg = ooStatic, for modeless dlg = ooAllocated
};

// ****************************************************************************

enum CObjectType // for identifying object type
{
    otBase,
    otWindow,
    otDialog,
#ifdef ENABLE_PROPERTYDIALOG
    otPropSheetPage,
#endif // ENABLE_PROPERTYDIALOG
    otLastWinLibObject
};

// ****************************************************************************

class CWindowsObject // base of all MS-Windows objects
{
public:
    HWND HWindow;
    UINT HelpID; // -1 = empty value (do not use help)

    CWindowsObject(CObjectOrigin origin)
    {
        HWindow = NULL;
        ObjectOrigin = origin;
        HelpID = -1;
    }
    CWindowsObject(UINT helpID, CObjectOrigin origin)
    {
        HWindow = NULL;
        ObjectOrigin = origin;
        SetHelpID(helpID);
    }

    virtual ~CWindowsObject() {} // so derived destructors are called

    virtual BOOL Is(int) { return FALSE; } // object identification
    virtual int GetObjectType() { return otBase; }

    virtual BOOL IsAllocated() { return ObjectOrigin == ooAllocated; }
    void SetObjectOrigin(CObjectOrigin origin) { ObjectOrigin = origin; }

    void SetHelpID(UINT helpID)
    {
        if (helpID == -1)
            TRACE_E("CWindowsObject::SetHelpID(): helpID==-1, -1 is 'empty value', you should use another helpID! If you want to set HelpID to -1, use ClearHelpID().");
        HelpID = helpID;
    }
    void ClearHelpID() { HelpID = -1; }

protected:
    CObjectOrigin ObjectOrigin;
};

// ****************************************************************************

class CWindow : public CWindowsObject
{
public:
    CWindow(CObjectOrigin origin = ooAllocated) : CWindowsObject(origin) { DefWndProc = DefWindowProc; }
    CWindow(HWND hDlg, int ctrlID, CObjectOrigin origin = ooAllocated)
        : CWindowsObject(origin)
    {
        DefWndProc = DefWindowProc;
        AttachToControl(hDlg, ctrlID);
    }
    CWindow(HWND hDlg, int ctrlID, UINT helpID, CObjectOrigin origin = ooAllocated)
        : CWindowsObject(helpID, origin)
    {
        DefWndProc = DefWindowProc;
        AttachToControl(hDlg, ctrlID);
    }

    virtual BOOL Is(int type) { return type == otWindow; }
    virtual int GetObjectType() { return otWindow; }

    // registers universal WinLib classes, called automatically (unregistration also automatic)
    static BOOL RegisterUniversalClass(HINSTANCE dllInstance);

    // register a custom universal class; WARNING: on plugin unload you must unregister the class,
    // otherwise reloading the plugin will fail on registration (conflict with the old class)
    static BOOL RegisterUniversalClass(UINT style,
                                       int cbClsExtra,
                                       int cbWndExtra,
                                       HINSTANCE dllInstance,
                                       HICON hIcon,
                                       HCURSOR hCursor,
                                       HBRUSH hbrBackground,
                                       LPCTSTR lpszMenuName,
                                       LPCTSTR lpszClassName,
                                       HICON hIconSm);

    HWND Create(LPCTSTR lpszClassName,  // address of registered class name
                LPCTSTR lpszWindowName, // address of window name
                DWORD dwStyle,          // window style
                int x,                  // horizontal position of window
                int y,                  // vertical position of window
                int nWidth,             // window width
                int nHeight,            // window height
                HWND hwndParent,        // handle of parent or owner window
                HMENU hmenu,            // handle of menu or child-window identifier
                HINSTANCE hinst,        // handle of application instance
                LPVOID lpvParam);       // pointer to the created window object

    HWND CreateEx(DWORD dwExStyle,        // extended window style
                  LPCTSTR lpszClassName,  // address of registered class name
                  LPCTSTR lpszWindowName, // address of window name
                  DWORD dwStyle,          // window style
                  int x,                  // horizontal position of window
                  int y,                  // vertical position of window
                  int nWidth,             // window width
                  int nHeight,            // window height
                  HWND hwndParent,        // handle of parent or owner window
                  HMENU hmenu,            // handle of menu or child-window identifier
                  HINSTANCE hinst,        // handle of application instance
                  LPVOID lpvParam);       // pointer to the created window object

    void AttachToWindow(HWND hWnd);
    void AttachToControl(HWND dlg, int ctrlID);
    void DetachWindow();

    static LRESULT CALLBACK CWindowProc(HWND hwnd, UINT uMsg,
                                        WPARAM wParam, LPARAM lParam);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    WNDPROC DefWndProc;
};

// ****************************************************************************

enum CTransferType
{
    ttDataToWindow,  // data goes to the window
    ttDataFromWindow // data goes from the window
};

// ****************************************************************************

class CTransferInfo
{
public:
    int FailCtrlID; // INT_MAX - all ok, otherwise ID of the control with an error
    CTransferType Type;

    CTransferInfo(HWND hDialog, CTransferType type)
    {
        HDialog = hDialog;
        FailCtrlID = INT_MAX;
        Type = type;
    }

    BOOL IsGood() { return FailCtrlID == INT_MAX; }
    void ErrorOn(int ctrlID) { FailCtrlID = ctrlID; }
    BOOL GetControl(HWND& ctrlHWnd, int ctrlID, BOOL ignoreIsGood = FALSE);
    void EnsureControlIsFocused(int ctrlID);

    void EditLine(int ctrlID, char* buffer, DWORD bufferSize, BOOL select = TRUE);
    void RadioButton(int ctrlID, int ctrlValue, int& value);
    void CheckBox(int ctrlID, int& value); // 0-unchecked, 1-checked, 2-grayed

    // validates double value (if not a number, it fails); decimal separator can be '.' or ',';
    // 'format' is used in sprintf when converting the number to a string (e.g. "%.2f" or "%g")
    void EditLine(int ctrlID, double& value, char* format, BOOL select = TRUE);

    // validates int value (if not a number, it fails)
    void EditLine(int ctrlID, int& value, BOOL select = TRUE);

protected:
    HWND HDialog; // handle of the dialog for which transfer is performed
};

// ****************************************************************************

class CDialog : public CWindowsObject
{
public:
#ifdef ENABLE_PROPERTYDIALOG
    CWindowsObject::HWindow;         // to make CPropSheetPage compilable
    CWindowsObject::SetObjectOrigin; // to make CPropSheetPage compilable
#endif                               // ENABLE_PROPERTYDIALOG

    CDialog(HINSTANCE modul, int resID, HWND parent,
            CObjectOrigin origin = ooStandard) : CWindowsObject(origin)
    {
        Modal = 0;
        Modul = modul;
        ResID = resID;
        Parent = parent;
    }
    CDialog(HINSTANCE modul, int resID, UINT helpID, HWND parent,
            CObjectOrigin origin = ooStandard) : CWindowsObject(helpID, origin)
    {
        Modal = 0;
        Modul = modul;
        ResID = resID;
        Parent = parent;
    }

    virtual BOOL ValidateData();
    virtual void Validate(CTransferInfo& /*ti*/) {}
    virtual BOOL TransferData(CTransferType type);
    virtual void Transfer(CTransferInfo& /*ti*/) {}

    virtual BOOL Is(int type) { return type == otDialog; }
    virtual int GetObjectType() { return otDialog; }

    virtual BOOL IsAllocated() { return ObjectOrigin == ooAllocated ||
                                        (!Modal && ObjectOrigin == ooStandard); }

    void SetParent(HWND parent) { Parent = parent; }
    INT_PTR Execute(); // modal dialog
    HWND Create();     // modeless dialog

    static INT_PTR CALLBACK CDialogProc(HWND hwndDlg, UINT uMsg,
                                        WPARAM wParam, LPARAM lParam);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    virtual void NotifDlgJustCreated() {}

    BOOL Modal; // for dialog destruction handling
    HINSTANCE Modul;
    int ResID;
    HWND Parent;
};

// ****************************************************************************

#ifdef ENABLE_PROPERTYDIALOG

class CPropertyDialog;

class CPropSheetPage : protected CDialog
{
public:
    CDialog::HWindow; // HWindow remains accessible

    CDialog::SetObjectOrigin; // expose allowed base methods
    CDialog::Transfer;

    // tested with dialog resource style:
    // DS_CONTROL | DS_3DLOOK | WS_CHILD | WS_CAPTION;
    // if we want to use the title directly from the resource, set 'title'==NULL and
    // 'flags'==0
    CPropSheetPage(char* title, HINSTANCE modul, int resID,
                   DWORD flags /* = PSP_USETITLE*/, HICON icon,
                   CObjectOrigin origin = ooStatic);
    CPropSheetPage(char* title, HINSTANCE modul, int resID, int helpID,
                   DWORD flags /* = PSP_USETITLE*/, HICON icon,
                   CObjectOrigin origin = ooStatic);
    ~CPropSheetPage();

    void Init(char* title, HINSTANCE modul, int resID,
              HICON icon, DWORD flags, CObjectOrigin origin);

    virtual BOOL ValidateData();
    virtual BOOL TransferData(CTransferType type);

    HPROPSHEETPAGE CreatePropSheetPage();
    virtual BOOL Is(int type) { return type == otPropSheetPage || CDialog::Is(type); }
    virtual int GetObjectType() { return otPropSheetPage; }
    virtual BOOL IsAllocated() { return ObjectOrigin == ooAllocated; }

    static INT_PTR CALLBACK CPropSheetPageProc(HWND hwndDlg, UINT uMsg,
                                               WPARAM wParam, LPARAM lParam);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    char* Title;
    DWORD Flags;
    HICON Icon;

    CPropertyDialog* ParentDialog; // owner of this page

    friend class CPropertyDialog;
};

// ****************************************************************************

class CPropertyDialog : public TIndirectArray<CPropSheetPage>
{
public:
    // it is ideal to add objects of individual pages to this object
    // and then add them as "static" (default option) via Add;
    // 'startPage' and 'lastPage' can be the same variable (value in/out);
    // 'flags' see help for 'PROPSHEETHEADER', mainly the constants
    // PSH_NOAPPLYNOW, PSH_USECALLBACK and PSH_HASHELP (otherwise 'flags'==0 is enough)
    CPropertyDialog(HWND parent, HINSTANCE modul, char* caption,
                    int startPage, DWORD flags, HICON icon = NULL,
                    DWORD* lastPage = NULL, PFNPROPSHEETCALLBACK callback = NULL)
        : TIndirectArray<CPropSheetPage>(10, 5, dtNoDelete)
    {
        Parent = parent;
        HWindow = NULL;
        Modul = modul;
        Icon = icon;
        Caption = caption;
        StartPage = startPage;
        Flags = flags;
        LastPage = lastPage;
        Callback = callback;
    }

    virtual INT_PTR Execute();

    virtual int GetCurSel();

protected:
    HWND Parent; // parameters for creating the dialog
    HWND HWindow;
    HINSTANCE Modul;
    HICON Icon;
    char* Caption;
    int StartPage;
    DWORD Flags;
    PFNPROPSHEETCALLBACK Callback;

    DWORD* LastPage; // last selected page (can be NULL if not needed)

    friend class CPropSheetPage;
};

#endif // ENABLE_PROPERTYDIALOG

// ****************************************************************************

class CWindowsManager
{
public:
    int WindowsCount; // number of windows handled by WinLib (current state)

public:
    CWindowsManager() { WindowsCount = 0; }

    BOOL AddWindow(HWND hWnd, CWindowsObject* wnd);
    void DetachWindow(HWND hWnd);
    CWindowsObject* GetWindowPtr(HWND hWnd);
};

// ****************************************************************************

struct CWindowQueueItem
{
    HWND HWindow;
    CWindowQueueItem* Next;

    CWindowQueueItem(HWND hWindow)
    {
        HWindow = hWindow;
        Next = NULL;
    }
};

class CWindowQueue
{
protected:
    const char* QueueName; // queue name (debug only)
    CWindowQueueItem* Head;

    struct CCS // access from multiple threads -> synchronization required
    {
        CRITICAL_SECTION cs;

        CCS() { InitializeCriticalSection(&cs); }
        ~CCS() { DeleteCriticalSection(&cs); }

        void Enter() { EnterCriticalSection(&cs); }
        void Leave() { LeaveCriticalSection(&cs); }
    } CS;

public:
    CWindowQueue(const char* queueName /* e.g. "DemoPlug Viewers" */)
    {
        QueueName = queueName;
        Head = NULL;
    }
    ~CWindowQueue();

    BOOL Add(CWindowQueueItem* item); // adds an item to the queue, returns success
    void Remove(HWND hWindow);        // removes an item from the queue
    BOOL Empty();                     // returns TRUE if the queue is empty

    // sends (PostMessage - windows may be in different threads) a message to all windows
    void BroadcastMessage(DWORD uMsg, WPARAM wParam, LPARAM lParam);

    // broadcasts WM_CLOSE, then waits for an empty queue (max time per 'force' is either 'forceWaitTime'
    // or 'waitTime'); returns TRUE if the queue is empty (all windows closed)
    // or if 'force' is TRUE; INFINITE means unlimited wait
    // Note: when 'force' is TRUE it always returns TRUE, there is no point waiting, so forceWaitTime = 0
    BOOL CloseAllWindows(BOOL force, int waitTime = 1000, int forceWaitTime = 0);
};

// ****************************************************************************

extern CWindowsManager WindowsManager;
