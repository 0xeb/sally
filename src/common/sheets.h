// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <vector>

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
    void Scale(int numeratorX, int denominatorX, int numeratorY, int denominatorY);

protected:
    static BOOL CALLBACK FindMoveControls(HWND hChild, LPARAM lParam);

    void FindMoveCtrls();

protected:
    friend class CTreePropHolderDlg;
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

    HWND CreateFromTemplate(const DLGTEMPLATE* dialogTemplate) { return CDialog::CreateIndirect(dialogTemplate); }

    CPropSheetPage(const wchar_t* title, HINSTANCE modul, int resID,
                   DWORD flags /* = PSP_USETITLE*/, HICON icon,
                   CObjectOrigin origin = ooStatic);
    CPropSheetPage(const wchar_t* title, HINSTANCE modul, int resID, UINT helpID,
                   DWORD flags /* = PSP_USETITLE*/, HICON icon,
                   CObjectOrigin origin = ooStatic);
    ~CPropSheetPage();

    void Init(const wchar_t* title, HINSTANCE modul, int resID,
              HICON icon, DWORD flags, CObjectOrigin origin);

    virtual BOOL ValidateData();
    virtual BOOL TransferData(CTransferType type);

    HPROPSHEETPAGE CreatePropSheetPage();
    virtual BOOL Is(int type) { return type == otPropSheetPage || CDialog::Is(type); }
    virtual int GetObjectType() { return otPropSheetPage; }
    virtual BOOL IsAllocated() { return ObjectOrigin == ooAllocated; }
    void SetDarkModeGroupBoxThemeEnabled(BOOL enabled) { DarkModeGroupBoxThemeEnabled = enabled; }

    static INT_PTR CALLBACK CPropSheetPageProc(HWND hwndDlg, UINT uMsg,
                                               WPARAM wParam, LPARAM lParam);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL ElasticVerticalLayout(int count, ...);

    wchar_t* Title;
    DWORD Flags;
    HICON Icon;

    CPropertyDialog* ParentDialog; // owner of this page
    // for TreeDialog
    CPropSheetPage* ParentPage;
    HTREEITEM HTreeItem;
    BOOL* Expanded;

    // if different from NULL, we change the layout of controls when dialog size changes
    CElasticLayout* ElasticLayout;
    BOOL DarkModeGroupBoxThemeEnabled;
    bool ResponsiveLayout = false;

    friend class CPropertyDialog;
    friend class CTreePropDialog;
    friend class CTreePropHolderDlg;
};

// ****************************************************************************

class CPropertyDialog : public TIndirectArray<CPropSheetPage>
{
public:
    CPropertyDialog(HWND parent, HINSTANCE modul, const wchar_t* caption,
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
    const wchar_t* Caption;
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
    WCHAR* Text;
    int Allocated;

public:
    CTPHCaptionWindow(HWND hDlg, int ctrlID);
    ~CTPHCaptionWindow();

    // wide: this control is entirely self-painted (WM_PAINT draws Text directly
    // via DrawTextW), so its own window's ANSI/Unicode class is irrelevant to what it can paint.
    void SetText(const WCHAR* text);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

// Legacy tree dialogs resize vertically; Configuration opts into both axes.
class CTPHGripWindow : public CWindow
{
public:
    CTPHGripWindow(HWND hDlg, int ctrlID) : CWindow(hDlg, ctrlID) {};

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

class CTPHPageViewport : public CWindow
{
    CTreePropHolderDlg* Holder;
    int WheelRemainder = 0;
public:
    explicit CTPHPageViewport(CTreePropHolderDlg* holder);
protected:
    virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
};

// dialog that holds treeview, shaded title and current property sheet
class CTreePropHolderDlg : public CDialog
{
protected:
    CTreePropDialog* TPD;
    HWND HTreeView;
    CTPHCaptionWindow* CaptionWindow;
    CTPHGripWindow* GripWindow;
    HWND PageViewport = NULL;
    SIZE NaturalPageSize = {};
    SIZE MaximumPageSize = {};
    std::vector<SIZE> PageSizes;
    std::vector<HFONT> ReflowFonts;
    struct PageControl
    {
        HWND Window;
        RECT Bounds;
        bool Stretch, Right, Grow, Below;
    };
    std::vector<std::vector<PageControl>> PageControls;
    void CapturePageControls(int page);
    void RestorePageControls();
    void SetMinimumSize(int borderX, int borderY, const RECT& workArea);
    HWND LastPageFocus = NULL;
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
    ~CTreePropHolderDlg();

    int ExecuteIndirect(LPCDLGTEMPLATE hDialogTemplate);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnCtrlTab(BOOL shift);
    void LayoutControls();
    int BuildAndMeasureTree();
    void EnableButtons();
    BOOL SelectPage(int pageIndex);
    void LayoutPageViewport();
    void ScrollPage(int bar, int command, UINT lines = 1);
    void PositionPage();
    void EnsurePageFocusVisible();
    bool ReflowFont(const LOGFONTW& font);

    friend class CTreePropDialog;
    friend class CTPHPageViewport;
};

// data holder for pages in tree version of PropertyDialog
class CTreePropDialog : public CPropertyDialog
{
protected:
    CTreePropHolderDlg Dialog;
    // CTreePropDialog's own Execute() never reaches CPropertyDialog::Execute()'s
    // psh.pszCaption = Caption (that 0-arg overload isn't reachable via the 3-arg Execute this
    // class exposes - the base Caption field is unused dead weight for this subclass), so the
    // window title can be stored wide here instead of round-tripping through the base's narrow
    // Caption.
    const WCHAR* CaptionW;

public:
    CTreePropDialog(HWND hParent, HINSTANCE hInstance, const WCHAR* caption,
                    int startPage, DWORD flags, DWORD* lastPage,
                    DWORD* windowHeight)
        : CPropertyDialog(hParent, hInstance, NULL, startPage, flags, NULL, lastPage),
          Dialog(hParent, windowHeight)
    {
        CaptionW = caption;
        Dialog.TPD = this;
    }

    // buttonOK/Cancel/Help are wide - the DLGTEMPLATE control-text fields they
    // feed (via AddItemEx/WinLibCopyText) are always UTF-16 regardless of ANSI/Unicode entry
    // point, so a narrow (LoadStr-sourced) caption would be a lossy CP_ACP round trip for no
    // reason.
    virtual int Execute(const WCHAR* buttonOK,
                        const WCHAR* buttonCancel,
                        const WCHAR* buttonHelp);
    virtual int GetCurSel();
    int Add(CPropSheetPage* page, CPropSheetPage* parent = NULL, BOOL* expanded = NULL);

    // Optional customization before USER32 measures any dialog or page. Other
    // tree property dialogs keep their original resource font and behavior.
    virtual bool PrepareDialogTemplate(const void* source, std::size_t size, std::vector<BYTE>& output) { return false; }
    virtual void IndexPageResource(int page, const void* source, std::size_t size) {}
    virtual void PageCreated(int page) {}
    void SetApplyButtonEnabled(bool enabled);
    virtual int NavigationHeaderHeight(HWND window) { return 0; }
    virtual void LayoutNavigation(const RECT& area) {}
    virtual bool HandleDialogMessage(UINT message, WPARAM wParam, LPARAM lParam) { return false; }
    virtual bool PreTranslateMessage(const MSG& message) { return false; }
    virtual const WCHAR* ApplyButtonCaption() const { return NULL; }
    virtual bool AfterTransfer(bool save) { return true; }
    virtual bool ScrollablePages() const { return false; }
    virtual bool ResizablePages() const { return false; }
    // Readability affects the scrollable canvas, never the dialog's saved/default/minimum size.
    virtual int MinimumPageContentWidth(HWND window, int naturalWidth) { return naturalWidth; }
    virtual bool StretchFullRowCheckboxes(HWND window) const { return false; }
    virtual bool LayoutPage(HWND window, SIZE available, SIZE& content, bool apply = true) { return false; }
    void RefreshLayout() { Dialog.LayoutControls(); }
    bool ReflowFont(const LOGFONTW& font) { return Dialog.ReflowFont(font); }
    bool NavigateToPage(int page, int control = 0);
    HWND GetDialogWindow() const { return Dialog.HWindow; }
    void ShowNavigationTree(bool visible) { ShowWindow(Dialog.HTreeView, visible ? SW_SHOW : SW_HIDE); }

protected:
    WORD* lpdwAlign(WORD* lpIn);
    int AddItemEx(LPWORD& lpw, const wchar_t* className, WORD id, int x, int y, int cx, int cy,
                  UINT style, UINT exStyle, const WCHAR* text);

    //    DLGTEMPLATE *DoLockDlgRes(int page);
    friend class CTreePropHolderDlg;

    // only for forwarding messages from CTreePropHolderDlg
    virtual void DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) {};
};
