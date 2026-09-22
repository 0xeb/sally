// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

// macro __DEBUG_WINLIB enables several tests for tricky WinLib errors

// constants for WinLib strings (internal use only in WinLib)
enum CWLS
{
    WLS_INVALID_NUMBER,
    WLS_ERROR,

    WLS_COUNT
};

// set custom text to WinLib
void SetWinLibStrings(const wchar_t* invalidNumber, // "not a number" (for number transfer buffer)
                      const wchar_t* error);        // title "error" (for number transfer buffer)

extern HINSTANCE HInstance;
extern const wchar_t* CWINDOW_CLASSNAME;  // name of universal window class
extern const wchar_t* CWINDOW_CLASSNAME2; // name of universal window class - does not have CS_VREDRAW | CS_HREDRAW


class CWinLibHelp;

// must be called before using WinLib
BOOL InitializeWinLib();
// must be called after using WinLib
void ReleaseWinLib();
// must be called before using help
BOOL SetupWinLibHelp(CWinLibHelp* winLibHelp);

class CWinLibHelp
{
public:
    virtual void OnHelp(HWND /*hWindow*/, UINT /*helpID*/, HELPINFO* /*helpInfo*/,
                        BOOL /*ctrlPressed*/, BOOL /*shiftPressed*/) {}
    virtual void OnContextMenu(HWND /*hWindow*/, WORD /*xPos*/, WORD /*yPos*/) {}
};

// ****************************************************************************

enum CObjectOrigin // used during window and dialog destruction
{
    ooAllocated, // will be deallocated on WM_DESTROY
    ooStatic,    // HWindow will be set to NULL on WM_DESTROY
    ooStandard   // for modal dlg = ooStatic, for non-modal dlg = ooAllocated
};

// ****************************************************************************

enum CObjectType // for recognizing object type
{
    otBase,
    otWindow,
    otDialog,
    otPropSheetPage,
    otLastWinLibObject
};

// ****************************************************************************

class CWindowsObject // base class of all MS-Windows objects
{
public:
    HWND HWindow;
    UINT HelpID; // -1 = empty value (do not use help)

    // The unicodeWnd opt-in is GONE. Every window is a Unicode window: the
    // define is unconditional (CMakeLists.txt), so a per-window flag could only ever have
    // held TRUE. It survived the flip only so that the remaining work could land
    // separately - the flag kept ~166 call sites compiling while the define moved. Both
    // halves are done now, so the parameter, the member and the call sites go together.
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

    virtual ~CWindowsObject() {} // so that destructor is called for descendants

    virtual BOOL Is(int) { return FALSE; } // object identification
    virtual int GetObjectType() { return otBase; }

    virtual BOOL IsAllocated() { return ObjectOrigin == ooAllocated; }
    void SetObjectOrigin(CObjectOrigin origin) { ObjectOrigin = origin; }

    void SetHelpID(UINT helpID)
    {
        if (helpID == -1)
            TRACE_ET(L"CWindowsObject::SetHelpID(): helpID==-1, -1 is 'empty value', you should use another helpID! If you want to set HelpID to -1, use ClearHelpID().");
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
    CWindow(CObjectOrigin origin = ooAllocated) : CWindowsObject(origin)
    {
        DefWndProc = GetDefWindowProc();
    }

    CWindow(HWND hDlg, int ctrlID, CObjectOrigin origin = ooAllocated) : CWindowsObject(origin)
    {
        DefWndProc = GetDefWindowProc();
        AttachToControl(hDlg, ctrlID);
    }

    CWindow(HWND hDlg, int ctrlID, UINT helpID, CObjectOrigin origin = ooAllocated)
        : CWindowsObject(helpID, origin)
    {
        DefWndProc = GetDefWindowProc();
        AttachToControl(hDlg, ctrlID);
    }

    virtual BOOL Is(int type) { return type == otWindow; }
    virtual int GetObjectType() { return otWindow; }

    static BOOL RegisterUniversalClass();
    static BOOL RegisterUniversalClass(UINT style,
                                       int cbClsExtra,
                                       int cbWndExtra,
                                       HICON hIcon,
                                       HCURSOR hCursor,
                                       HBRUSH hbrBackground,
                                       LPCWSTR lpszMenuName,
                                       LPCWSTR lpszClassName,
                                       HICON hIconSm);

    HWND Create(LPCWSTR lpszClassName,  // address of registered class name
                LPCWSTR lpszWindowName, // address of window name
                DWORD dwStyle,          // window style
                int x,                  // horizontal position of window
                int y,                  // vertical position of window
                int nWidth,             // window width
                int nHeight,            // window height
                HWND hwndParent,        // handle of parent or owner window
                HMENU hmenu,            // handle of menu or child-window identifier
                HINSTANCE hinst,        // handle of application instance
                LPVOID lpvParam);       // pointer to created window object

    HWND CreateEx(DWORD dwExStyle,        // extended window style
                  LPCWSTR lpszClassName,  // address of registered class name
                  LPCWSTR lpszWindowName, // address of window name
                  DWORD dwStyle,          // window style
                  int x,                  // horizontal position of window
                  int y,                  // vertical position of window
                  int nWidth,             // window width
                  int nHeight,            // window height
                  HWND hwndParent,        // handle of parent or owner window
                  HMENU hmenu,            // handle of menu or child-window identifier
                  HINSTANCE hinst,        // handle of application instance
                  LPVOID lpvParam);       // pointer to created window object

    void AttachToWindow(HWND hWnd);
    void AttachToControl(HWND dlg, int ctrlID);
    void DetachWindow();

    static LRESULT CALLBACK CWindowProc(HWND hwnd, UINT uMsg,
                                        WPARAM wParam, LPARAM lParam);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);


    WNDPROC GetDefWindowProc() { return DefWindowProcW; }

    WNDPROC DefWndProc;
};

// ****************************************************************************

enum CTransferType
{
    ttDataToWindow,  // data goes to window
    ttDataFromWindow // data comes from window
};

// ****************************************************************************

class CTransferInfo
{
public:
    int FailCtrlID; // INT_MAX - all ok, otherwise ID of control with error
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

    void EditLine(int ctrlID, wchar_t* buffer, DWORD bufferSizeInChars, BOOL select = TRUE);
    void EditLine(int ctrlID, double& value, wchar_t* format, BOOL select = TRUE); // format e.g. L"%.2lf"
    void EditLine(int ctrlID, int& value, BOOL select = TRUE);
    void EditLine(int ctrlID, __int64& value, BOOL select = TRUE, BOOL unsignedNum = FALSE /* signed number */,
                  BOOL hexMode = FALSE /* decimal mode */, BOOL ignoreOverflow = FALSE, BOOL quiet = FALSE);
    void RadioButton(int ctrlID, int ctrlValue, int& value);
    void CheckBox(int ctrlID, int& value); // 0-unchecked, 1-checked, 2-grayed
    void TrackBar(int ctrlID, int& value);

    // Unconditional in both builds - EditLineW is a distinct name from EditLine,
    // so there is no A/W signature collision to guard against here (unlike the doubled-ctor
    // case). Under _UNICODE its body is functionally redundant with EditLine(wchar_t*, ...)
    // (both resolve to SendMessageW), which is fine: keeping it means the ~30 existing
    // .EditLineW(...) call sites across dialogs still compile under the canary without each
    // needing its own #ifdef, matching the winlib constructor-unification precedent (accept
    // in both builds now, so the redundancy can be retired later).
    void EditLineW(int ctrlID, WCHAR* buffer, DWORD bufferSizeInChars, BOOL select = TRUE);
    void EditLineW(int ctrlID, std::wstring& value, BOOL select = TRUE);

protected:
    HWND HDialog; // dialog handle for which transfer is performed
};

// ****************************************************************************

class CDialog : public CWindowsObject
{
public:
    using CWindowsObject::HWindow;         // for CPropSheetPage compilability
    using CWindowsObject::SetObjectOrigin; // for CPropSheetPage compilability

    // The unicodeWnd opt-in that used to sit here is gone; see CWindowsObject.
    // Its history, worth one line: the default was FALSE, dialogs opted in one at a
    // time, P0.4 flipped the default to TRUE, and P4.2's unconditional define made the flag
    // unobservable - Execute()/Create() reach DialogBoxParamW/CreateDialogParamW either way.
    // Every dialog is a Unicode window and a wide caption is never re-narrowed by USER32.
    CDialog(HINSTANCE modul, int resID, HWND parent, CObjectOrigin origin = ooStandard)
        : CWindowsObject(origin)
    {
        Modal = 0;
        Modul = modul;
        ResID = resID;
        Parent = parent;
    }

    CDialog(HINSTANCE modul, int resID, UINT helpID, HWND parent, CObjectOrigin origin = ooStandard)
        : CWindowsObject(helpID, origin)
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
    HWND GetParent() { return Parent; }
    INT_PTR Execute(); // modal dialog
    HWND Create();     // non-modal dialog

    static INT_PTR CALLBACK CDialogProc(HWND hwndDlg, UINT uMsg,
                                        WPARAM wParam, LPARAM lParam);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    virtual void NotifDlgJustCreated() {}

    BOOL Modal; // due to dialog destruction method
    HINSTANCE Modul;
    int ResID;
    HWND Parent;
};

// ****************************************************************************
// ****************************************************************************

struct CWindowData
{
    // if window objects (Wnd) are placed on stack (typically modal dialogs, e.g., SalMessageBox())
    // and thread termination occurs, stack becomes invalid and window objects are no longer accessible,
    // we solve this by accessing objects (Wnd) only while valid window handles (HWnd) exist
    HWND HWnd;
    CWindowsObject* Wnd;
};

#define WNDMGR_CACHE_SIZE 256 // (2kB cache) must be in sync with GetCacheIndex

inline int GetCacheIndex(HWND hWnd)
{
    return ((int)(INT_PTR)hWnd) & 0xff;
}

struct CWinLibCS
{
    CRITICAL_SECTION cs;

    CWinLibCS() { HANDLES(InitializeCriticalSection(&cs)); }
    ~CWinLibCS() { HANDLES(DeleteCriticalSection(&cs)); }

    void Enter() { HANDLES(EnterCriticalSection(&cs)); }
    void Leave() { HANDLES(LeaveCriticalSection(&cs)); }
};

class CWindowsManager : protected TDirectArray<CWindowData>
{
public:
#ifdef __DEBUG_WINLIB
    int search, cache, maxWndCount;
#endif

    CWinLibCS CS; // is public to locally prevent changes in Windows Manager

public:
    CWindowsManager();

    BOOL AddWindow(HWND hWnd, CWindowsObject* wnd);
    void DetachWindow(HWND hWnd);
    CWindowsObject* GetWindowPtr(HWND hWnd);
    int GetCount();

private:
    HWND LastHWnd[WNDMGR_CACHE_SIZE]; // last request - cache
    CWindowsObject* LastWnd[WNDMGR_CACHE_SIZE];

    inline BOOL GetIndex(HWND hWnd, int& index);

    friend void ReleaseWinLib();
};

// ****************************************************************************

BOOL CWindowsManager::GetIndex(HWND hWnd, int& index)
{
    CS.Enter();
    if (Count == 0)
    {
        index = 0;
        CS.Leave();
        return FALSE;
    }

    int l = 0, r = Count - 1, m;
    while (1)
    {
        m = (l + r) / 2;
        HWND hw = At(m).HWnd;
        if (hw == hWnd) // found
        {
            index = m;
            CS.Leave();
            return TRUE;
        }
        else if (hw > hWnd)
        {
            if (l == r || l > m - 1) // not found
            {
                index = m; // should be at this position
                CS.Leave();
                return FALSE;
            }
            r = m - 1;
        }
        else
        {
            if (l == r) // not found
            {
                index = m + 1; // should be right after this position
                CS.Leave();
                return FALSE;
            }
            l = m + 1;
        }
    }
}

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
    const wchar_t* QueueName; // queue name (debug purposes only)
    CWindowQueueItem* Head;
    int Count;
    CWinLibCS CS; // access from multiple threads -> synchronization required

public:
    CWindowQueue(const wchar_t* queueName /* e.g. "Find Dialogs" */)
    {
        QueueName = queueName;
        Head = NULL;
        Count = 0;
    }
    ~CWindowQueue();

    BOOL Add(CWindowQueueItem* item); // adds item to queue, returns success
    void Remove(HWND hWindow);        // removes item from queue
    BOOL Empty();                     // returns TRUE if queue is empty
    int GetWindowCount();             // returns count of windows in queue

    // sends message to all windows (PostMessage - windows can be in different threads)
    void BroadcastMessage(DWORD uMsg, WPARAM wParam, LPARAM lParam);
};

// ****************************************************************************

extern CWindowsManager WindowsManager;
