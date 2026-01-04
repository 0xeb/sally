// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

// ****************************************************************************

class CPropertyDialog;
class CTreePropHolderDlg;

struct CElasticLayoutCtrl
{
    HWND HCtrl; // handle of control that we want to move
    POINT Pos;  // position of control relative to bottom edge of bounding box
};

// helper class used for control layout in dialog based on its size
class CElasticLayout
{
public:
    CElasticLayout(HWND hWindow);
    void AddResizeCtrl(int resID);
    // performs arrangement of controls
    void LayoutCtrls();

protected:
    static BOOL CALLBACK FindMoveControls(HWND hChild, LPARAM lParam);

    void FindMoveCtrls();

protected:
    // handle of dialog whose layout we ensure
    HWND HWindow;
    // dividing line from which we move controls (lies on bottom edge of ResizeCntrls)
    // client coordinates in points
    int SplitY;
    // controls that we stretch with size (typically listview)
    TDirectArray<CElasticLayoutCtrl> ResizeCtrls;
    // temporary array filled from FindMoveCtrls; ideally it would be a local variable, but
    // for convenient calling of callback FindMoveControls (where we need to pass it)
    // we place it as a class attribute
    TDirectArray<CElasticLayoutCtrl> MoveCtrls;
};

class CPropSheetPage : protected CDialog
{
public:
    using CDialog::HWindow;         // HWindow remains also accessible
    using CDialog::SetObjectOrigin; // access to allowed methods of parent
    using CDialog::Transfer;

    CPropSheetPage(const TCHAR* title, HINSTANCE modul, int resID,
                   DWORD flags /* = PSP_USETITLE*/, HICON icon,
                   CObjectOrigin origin = ooStatic);
    CPropSheetPage(const TCHAR* title, HINSTANCE modul, int resID, UINT helpID,
                   DWORD flags /* = PSP_USETITLE*/, HICON icon,
                   CObjectOrigin origin = ooStatic);
    ~CPropSheetPage();

    void Init(const TCHAR* title, HINSTANCE modul, int resID,
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
    BOOL ElasticVerticalLayout(int count, ...);

    TCHAR* Title;
    DWORD Flags;
    HICON Icon;

    CPropertyDialog* ParentDialog; // owner of this page
    // for TreeDialog
    CPropSheetPage* ParentPage;
    HTREEITEM HTreeItem;
    BOOL* Expanded;

    // if different from NULL, we change the layout of controls when dialog size changes
    CElasticLayout* ElasticLayout;

    friend class CPropertyDialog;
    friend class CTreePropDialog;
    friend class CTreePropHolderDlg;
};

// ****************************************************************************

class CPropertyDialog : public TIndirectArray<CPropSheetPage>
{
public:
    CPropertyDialog(HWND parent, HINSTANCE modul, const TCHAR* caption,
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
    HWND Parent; // parameters for dialog creation
    HWND HWindow;
    HINSTANCE Modul;
    HICON Icon;
    const TCHAR* Caption;
    int StartPage;
    DWORD Flags;
    PFNPROPSHEETCALLBACK Callback;

    DWORD* LastPage; // last selected page (can be NULL if not interested)

    friend class CPropSheetPage;
};

// ****************************************************************************

class CTreePropDialog;

// gray shaded bar above property sheet in tree variant of PropertyDialog,
// where the name of current page is displayed
class CTPHCaptionWindow : protected CWindow
{
protected:
    TCHAR* Text;
    int Allocated;

public:
    CTPHCaptionWindow(HWND hDlg, int ctrlID);
    ~CTPHCaptionWindow();

    void SetText(const TCHAR* text);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

// on grip control we want only top-down cursor
class CTPHGripWindow : public CWindow
{
public:
    CTPHGripWindow(HWND hDlg, int ctrlID) : CWindow(hDlg, ctrlID) {};

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

// dialog that holds treeview, shaded title and current property sheet
class CTreePropHolderDlg : public CDialog
{
protected:
    CTreePropDialog* TPD;
    HWND HTreeView;
    CTPHCaptionWindow* CaptionWindow;
    CTPHGripWindow* GripWindow;
    RECT ChildDialogRect;
    int CurrentPageIndex;
    CPropSheetPage* ChildDialog;
    int ExitButton; // ID of button that closed the dialog

    // dimensions in points
    SIZE MinWindowSize;  // minimal dimensions of dialog (determined by largest child dlg)
    DWORD* WindowHeight; // current height of dialog
    int TreeWidth;       // width of treeview, calculated based on content
    int CaptionHeight;   // height of title
    SIZE ButtonSize;     // dimensions of buttons on bottom edge of dialog
    int ButtonMargin;    // spacing between buttons
    SIZE GripSize;       // dimensions of resize grip in right bottom corner of dialog
    SIZE MarginSize;     // horizontal and vertical margin

public:
    CTreePropHolderDlg(HWND hParent, DWORD* windowHeight);

    int ExecuteIndirect(LPCDLGTEMPLATE hDialogTemplate);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnCtrlTab(BOOL shift);
    void LayoutControls();
    int BuildAndMeasureTree();
    void EnableButtons();
    BOOL SelectPage(int pageIndex);

    friend class CTreePropDialog;
};

// data holder for pages in tree version of PropertyDialog
class CTreePropDialog : public CPropertyDialog
{
protected:
    CTreePropHolderDlg Dialog;

public:
    CTreePropDialog(HWND hParent, HINSTANCE hInstance, TCHAR* caption,
                    int startPage, DWORD flags, DWORD* lastPage,
                    DWORD* windowHeight)
        : CPropertyDialog(hParent, hInstance, caption, startPage, flags, NULL, lastPage),
          Dialog(hParent, windowHeight)
    {
        Dialog.TPD = this;
    }

    virtual int Execute(const TCHAR* buttonOK,
                        const TCHAR* buttonCancel,
                        const TCHAR* buttonHelp);
    virtual int GetCurSel();
    int Add(CPropSheetPage* page, CPropSheetPage* parent = NULL, BOOL* expanded = NULL);

protected:
    WORD* lpdwAlign(WORD* lpIn);
    int AddItemEx(LPWORD& lpw, const TCHAR* className, WORD id, int x, int y, int cx, int cy,
                  UINT style, UINT exStyle, const TCHAR* text);

    //    DLGTEMPLATE *DoLockDlgRes(int page);
    friend class CTreePropHolderDlg;

    // only for forwarding messages from CTreePropHolderDlg
    virtual void DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) {};
};
