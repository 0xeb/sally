// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

//****************************************************************************
//
// CProgressBar
//
// Class is always allocated (CObjectOrigin origin = ooAllocated)

class CProgressBar : public CWindow
{
public:
    // hDlg is parent window (dialog or window)
    // ctrlID is child window ID
    CProgressBar(HWND hDlg, int ctrlID);
    ~CProgressBar();

    // SetProgress can be called from any thread, internally sends WM_USER_SETPROGRESS
    // progress bar thread must run
    void SetProgress(DWORD progress, const char* text = NULL);
    void SetProgress2(const CQuadWord& progressCurrent, const CQuadWord& progressTotal,
                      const char* text = NULL);

    void SetSelfMoveTime(DWORD time);
    void SetSelfMoveSpeed(DWORD moveTime);
    void Stop();

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void Paint(HDC hDC);

    void MoveBar();

protected:
    int Width, Height;
    DWORD Progress;
    CBitmap* Bitmap;     // bitmap for memDC -> paint cache
    int BarX;            // X coordinate of rectangle for unknown progress (for Progress==-1)
    BOOL MoveBarRight;   // is rectangle moving right?
    DWORD SelfMoveTime;  // 0: after calling SetProgress(-1) the rectangle moves only one increment (0 is default value)
                         // more than 0: time in [ms], how long we will continue to move after calling SetProgress(-1)
    DWORD SelfMoveTicks; // stored value of GetTickCount() during the last call to SetSelfMoveTime()
    DWORD SelfMoveSpeed; // speed of rectangle movement: value is in [ms] and indicates the time between rectangle movements
                         // minimum is 10ms, default value is 50ms -- so 20 movements per second
                         // be careful with low values, animation can noticeably load the processor
    BOOL TimerIsRunning; // is timer running?
    char* Text;          // if different from NULL, will be displayed instead of number
    HFONT HFont;         // font for progress bar
};

//****************************************************************************
//
// CStaticText
//
// Class is always allocated (CObjectOrigin origin = ooAllocated)

class CStaticText : public CWindow
{
public:
    // hDlg is parent window (dialog or window)
    // ctrlID is child window ID
    // flags is a combination of values from the STF_* family (shared\spl_gui.h)
    CStaticText(HWND hDlg, int ctrlID, DWORD flags);
    ~CStaticText();

    // sets Text, returns TRUE on success and FALSE on memory shortage
    BOOL SetText(const char* text);

    // warning, returned Text may be NULL
    const char* GetText() { return Text; }

    // sets Text (if it starts or ends with a space, puts it in double quotes),
    // returns TRUE on success and FALSE on memory shortage
    BOOL SetTextToDblQuotesIfNeeded(const char* text);

    // on some filesystems there can be a different path separator
    // must be different from '\0';
    void SetPathSeparator(char separator);

    // assigns text that will be displayed as tooltip
    BOOL SetToolTipText(const char* text);

    // assigns window and id that will receive WM_USER_TTGETTEXT when tooltip is displayed
    void SetToolTip(HWND hNotifyWindow, DWORD id);

    // if set to TRUE, tooltip can be invoked by clicking on text or
    // pressing Up/Down/Space keys, if control has focus
    // tooltip will then be displayed just below the text and remain displayed
    // implicitly set to FALSE
    void EnableHintToolTip(BOOL enable);

    //    void UpdateControl();

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void PrepareForPaint();

    BOOL TextHitTest(POINT* screenCursorPos);
    int GetTextXOffset(); // based on Alignment, Width and TextWidth variables returns X text offset
    void DrawFocus(HDC hDC);

    BOOL ToolTipAssigned();

    BOOL ShowHint();

    DWORD Flags;         // flags for control behavior
    char* Text;          // allocated text
    int TextLen;         // string length
    char* Text2;         // allocated text containing ellipsis; used only with STF_END_ELLIPSIS or STF_PATH_ELLIPSIS
    int Text2Len;        // Text2 length
    int* AlpDX;          // array of substring lengths; used only with STF_END_ELLIPSIS or STF_PATH_ELLIPSIS
    int TextWidth;       // text width in points
    int TextHeight;      // text height in points
    int Allocated;       // size of allocated buffer 'Text' and 'AlpDX'
    int Width, Height;   // static dimensions
    CBitmap* Bitmap;     // cache for drawing; used only with STF_CACHED_PAINT
    HFONT HFont;         // font handle used for text drawing
    BOOL DestroyFont;    // if HFont is allocated, is TRUE, otherwise is FALSE
    BOOL ClipDraw;       // need to clip drawing, otherwise we would go out of bounds
    BOOL Text2Draw;      // we will draw from buffer containing ellipsis
    int Alignment;       // 0=left, 1=center, 2=right
    char PathSeparator;  // path separator; implicitly '\\'
    BOOL MouseIsTracked; // we installed mouse leave tracking
    // tooltip support
    char* ToolTipText; // string that will be displayed as our tooltip
    HWND HToolTipNW;   // notification window
    DWORD ToolTipID;   // and ID under which tool tip should ask for text
    BOOL HintMode;     // should we display tooltip as Hint?
    WORD UIState;      // display of accelerators
};

//****************************************************************************
//
// CHyperLink
//

class CHyperLink : public CStaticText
{
public:
    // hDlg is parent window (dialog or window)
    // ctrlID is child window ID
    // flags is a combination of values from the STF_* family (shared\spl_gui.h)
    CHyperLink(HWND hDlg, int ctrlID, DWORD flags = STF_UNDERLINE | STF_HYPERLINK_COLOR);

    void SetActionOpen(const char* file);
    void SetActionPostCommand(WORD command);
    BOOL SetActionShowHint(const char* text);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    void OnContextMenu(int x, int y);
    BOOL ExecuteIt();

protected:
    char File[MAX_PATH]; // if different from 0, passed to ShellExecute
    WORD Command;        // if different from 0, posted on action
    HWND HDialog;        // parent dialog
};

//****************************************************************************
//
// CColorRectangle
//
// renders the entire area of the object with Color
// combine with WS_EX_CLIENTEDGE
//

class CColorRectangle : public CWindow
{
protected:
    COLORREF Color;

public:
    CColorRectangle(HWND hDlg, int ctrlID, CObjectOrigin origin = ooAllocated);

    void SetColor(COLORREF color);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    virtual void PaintFace(HDC hdc);
};

//****************************************************************************
//
// CColorGraph
//

class CColorGraph : public CWindow
{
protected:
    HBRUSH Color1Light;
    HBRUSH Color1Dark;
    HBRUSH Color2Light;
    HBRUSH Color2Dark;

    RECT ClientRect;
    double UsedProc;

public:
    CColorGraph(HWND hDlg, int ctrlID, CObjectOrigin origin = ooAllocated);
    ~CColorGraph();

    void SetColor(COLORREF color1Light, COLORREF color1Dark,
                  COLORREF color2Light, COLORREF color2Dark);

    void SetUsed(double used); // used = <0, 1>

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    virtual void PaintFace(HDC hdc);
};

//****************************************************************************
//
// CBitmapButton
//

class CButton : public CWindow
{
protected:
    DWORD Flags;
    BOOL DropDownPressed;
    BOOL Checked;
    BOOL ButtonPressed;
    BOOL Pressed;
    BOOL DefPushButton;
    BOOL Captured;
    BOOL Space;
    RECT ClientRect;
    // tooltip support
    BOOL MouseIsTracked;  // we installed mouse leave tracking
    char* ToolTipText;    // string that will be displayed as our tooltip
    HWND HToolTipNW;      // notification window
    DWORD ToolTipID;      // and ID under which tool tip should ask for text
    DWORD DropDownUpTime; // time in [ms], when drop down was released, to protect against new pressing
    // XP Theme support
    BOOL Hot;
    WORD UIState; // display of accelerators

public:
    CButton(HWND hDlg, int ctrlID, DWORD flags, CObjectOrigin origin = ooAllocated);
    ~CButton();

    // assigns text that will be displayed as tooltip
    BOOL SetToolTipText(const char* text);

    // assigns window and id that will receive WM_USER_TTGETTEXT when tooltip is displayed
    void SetToolTip(HWND hNotifyWindow, DWORD id);

    DWORD GetFlags();
    void SetFlags(DWORD flags, BOOL updateWindow);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    virtual void PaintFace(HDC hdc, const RECT* rect, BOOL enabled);

    int HitTest(LPARAM lParam); // returns 0: nowhere; 1: button; 2: drop down
    void PaintFrame(HDC hDC, const RECT* r, BOOL down);
    void PaintDrop(HDC hDC, const RECT* r, BOOL enabled);
    int GetDropPartWidth();

    void RePaint();
    void NotifyParent(WORD notify);

    BOOL ToolTipAssigned();
};

//****************************************************************************
//
// CColorArrowButton
//
// background with text, followed by an arrow - used for menu expansion
//

class CColorArrowButton : public CButton
{
protected:
    COLORREF TextColor;
    COLORREF BkgndColor;
    BOOL ShowArrow;

public:
    CColorArrowButton(HWND hDlg, int ctrlID, BOOL showArrow, CObjectOrigin origin = ooAllocated);

    void SetColor(COLORREF textColor, COLORREF bkgndColor);
    //    void     SetColor(COLORREF color);

    void SetTextColor(COLORREF textColor);
    void SetBkgndColor(COLORREF bkgndColor);

    COLORREF GetTextColor() { return TextColor; }
    COLORREF GetBkgndColor() { return BkgndColor; }

protected:
    virtual void PaintFace(HDC hdc, const RECT* rect, BOOL enabled);
};

//****************************************************************************
//
// CToolbarHeader
//

//#define TOOLBARHDR_USE_SVG

class CToolBar;

class CToolbarHeader : public CWindow
{
protected:
    CToolBar* ToolBar;
#ifdef TOOLBARHDR_USE_SVG
    HIMAGELIST HEnabledImageList;
    HIMAGELIST HDisabledImageList;
#else
    HIMAGELIST HHotImageList;
    HIMAGELIST HGrayImageList;
#endif
    DWORD ButtonMask;   // used buttons
    HWND HNotifyWindow; // where I send commands
    WORD UIState;       // display of accelerators

public:
    CToolbarHeader(HWND hDlg, int ctrlID, HWND hAlignWindow, DWORD buttonMask);

    void EnableToolbar(DWORD enableMask);
    void CheckToolbar(DWORD checkMask);
    void SetNotifyWindow(HWND hWnd) { HNotifyWindow = hWnd; }

protected:
#ifdef TOOLBARHDR_USE_SVG
    void CreateImageLists(HIMAGELIST* enabled, HIMAGELIST* disabled);
#endif

    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnPaint(HDC hDC, BOOL hideAccel, BOOL prefixOnly);
};

//****************************************************************************
//
// CAnimate
//

/*
class CAnimate: public CWindow
{
  protected:
    HBITMAP          HBitmap;             // bitmap from which we take individual animation frames
    int              FramesCount;         // number of frames in bitmap
    int              FirstLoopFrame;      // if we are looping, from the end we transition to this frame
    SIZE             FrameSize;           // frame size in points
    CRITICAL_SECTION GDICriticalSection;  // critical section for GDI resource access
    CRITICAL_SECTION DataCriticalSection; // critical section for data access
    HANDLE           HThread;
    HANDLE           HRunEvent;           // if set, animation thread is running
    HANDLE           HTerminateEvent;     // if set, thread terminates
    COLORREF         BkColor;

    // control variables, come into play when HRunEvent is set
    BOOL             SleepThread;         // thread should sleep, HRunEvent will be reset

    int              CurrentFrame;        // zero-based index of currently displayed frame
    int              NestedCount;
    BOOL             MouseIsTracked;      // we installed mouse leave tracking

  public:
    // 'hBitmap'          is the bitmap from which we draw frames during animation;
    //                    frames must be below each other and must have constant height
    // 'framesCount'      gives the total number of frames in the bitmap
    // 'firstLoopFrame'   zero-based index of the frame where we return during looping
    //                    animation after reaching the end
    CAnimate(HBITMAP hBitmap, int framesCount, int firstLoopFrame, COLORREF bkColor, CObjectOrigin origin = ooAllocated);
    BOOL IsGood();                // did the constructor complete successfully?

    void Start();                 // if we are not animating, we start
    void Stop();                  // stops animation and displays the first frame
    void GetFrameSize(SIZE *sz);  // returns the size in points needed to display a frame

  protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void Paint(HDC hdc = NULL);   // display current frame; if hdc is NULL, get window DC
    void FirstFrame();            // set Frame to first frame
    void NextFrame();             // set Frame to next frame; skip first sequence

    // thread body
    static unsigned ThreadF(void *param);
    static unsigned AuxThreadEH(void *param);
    static DWORD WINAPI AuxThreadF(void *param);

    // ThreadF will be friend, to be able to access our data
    friend static unsigned ThreadF(void *param);
};
*/

//
//  ****************************************************************************
// ChangeToArrowButton
//

BOOL ChangeToArrowButton(HWND hParent, int ctrlID);

//
//  ****************************************************************************
// ChangeToIconButton
//

BOOL ChangeToIconButton(HWND hParent, int ctrlID, int iconID);

//
//  ****************************************************************************
// VerticalAlignChildToChild
//
// used to align "browse" button after editline / combobox (in resource workshop there is a problem to hit the button after combobox)
// modifies size and position of child window 'alignID' so that it fits at the same height (and is the same high) as child 'toID'
void VerticalAlignChildToChild(HWND hParent, int alignID, int toID);

//
//  ****************************************************************************
// CondenseStaticTexts
//
// condenses static texts so that they will follow closely - the distance between them will be
// the width of dialog font space; 'staticsArr' is an array of static IDs ending with zero
void CondenseStaticTexts(HWND hWindow, int* staticsArr);

//
//  ****************************************************************************
// ArrangeHorizontalLines
//
// finds horizontal lines and pushes them from the right to the text they follow
// also finds checkboxes and radioboxes that form labels for groupboxes and shortens
// them according to their text and current font in the dialog (eliminates unnecessary
// spaces created due to different screen DPI)
void ArrangeHorizontalLines(HWND hWindow);

//
//  ****************************************************************************
// GetWindowFontHeight
//
// for hWindow gets the current font and returns its height
int GetWindowFontHeight(HWND hWindow);

//
//  ****************************************************************************
// CreateCheckboxImagelist
//
// creates an imagelist containing two states of checkbox (unchecked and checked)
// and returns its handle; 'itemSize' is the width and height of one item in points
HIMAGELIST CreateCheckboxImagelist(int itemSize);

//
//  ****************************************************************************
// SalLoadIcon
//
// loads the icon specified by 'hInst' and 'iconName', returns its handle or NULL in case of
// error; 'iconSize' specifies the desired size of the icon; function is High DPI ready
// and returns its handle; 'itemSize' is the width and height of one item in points
//
// Note: old API LoadIcon() cannot handle larger icons, so we introduce this
// function, which reads icons using the new LoadIconWithScaleDown()
HICON SalLoadIcon(HINSTANCE hInst, LPCTSTR iconName, CIconSizeEnum iconSize);
