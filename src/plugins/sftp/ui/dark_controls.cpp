#include "precomp.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <vsstyle.h>

#include "dark_controls.h"
#include "plugindarkmode.h"

// A window subclass (SetWindowSubclass) is a hook in front of a control's own window procedure,
// roughly like overriding OnPaint in a derived WinForms control.

namespace
{
const UINT_PTR SUBCLASS_ID = 0x53465450; // any value unique for this subclass

// Fixed buffers instead of std::wstring: nothing here may throw while a paint is open (see DarkSubclassProc).
UINT ReadHeaderText(HWND header, int index, wchar_t* text, int textSize)
{
    text[0] = 0;
    HDITEMW item = {};
    item.mask = HDI_TEXT | HDI_FORMAT;
    item.pszText = text;
    item.cchTextMax = textSize;
    return SendMessageW(header, HDM_GETITEMW, index, reinterpret_cast<LPARAM>(&item)) ? item.fmt : 0;
}

// A pen selected into a DC for the length of a function (like a using block in C#). Windows cannot delete a pen that
// is still selected into a DC (the call fails and the handle leaks), so the old pen is selected back first.
class CSelectedPen
{
public:
    CSelectedPen(HDC dc, COLORREF color) : dc_(dc), pen_(CreatePen(PS_SOLID, 1, color))
    {
        oldPen_ = SelectObject(dc_, pen_);
    }
    ~CSelectedPen()
    {
        SelectObject(dc_, oldPen_);
        DeleteObject(pen_);
    }
    CSelectedPen(const CSelectedPen&) = delete;
    CSelectedPen& operator=(const CSelectedPen&) = delete;

private:
    HDC dc_;
    HPEN pen_;
    HGDIOBJ oldPen_;
};

// Pixel values are written for 96 DPI (100%) and scaled with the DPI of the DC. GetDeviceCaps(LOGPIXELSX) is used
// instead of GetDpiForWindow because the plugins target Windows 7 (WINVER 0x0601), where that API does not exist;
// it returns the system DPI, not the per-monitor one, which is right because Sally's manifest declares system DPI
// awareness only.
int Scale(HDC dc, int px)
{
    return MulDiv(px, GetDeviceCaps(dc, LOGPIXELSX), 96);
}

// The item under the mouse is kept as a window property (index + 1, so that "no property" means "none").
// A property needs no allocation, so nothing can throw or leak here.
const wchar_t* const HOT_PROP = L"SftpDarkHeaderHot";

int GetHotItem(HWND header)
{
    return static_cast<int>(reinterpret_cast<INT_PTR>(GetPropW(header, HOT_PROP))) - 1;
}

void SetHotItem(HWND header, int item)
{
    if (item == GetHotItem(header))
        return;
    SetPropW(header, HOT_PROP, reinterpret_cast<HANDLE>(static_cast<INT_PTR>(item + 1)));
    InvalidateRect(header, nullptr, FALSE);
}

// Mouse moved over the header: ask Windows which item is under the cursor and make it the hot one.
void TrackHotItem(HWND header, LPARAM lParam)
{
    TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, header, 0}; // asks for WM_MOUSELEAVE when the mouse leaves
    TrackMouseEvent(&track);
    HDHITTESTINFO hit = {};
    hit.pt.x = static_cast<short>(LOWORD(lParam));
    hit.pt.y = static_cast<short>(HIWORD(lParam));
    SendMessageW(header, HDM_HITTEST, 0, reinterpret_cast<LPARAM>(&hit));
    SetHotItem(header, (hit.flags & HHT_ONHEADER) != 0 ? hit.iItem : -1);
}

// Small filled triangle (up = ascending sort). Uses the stock pen and brush of the DC, so nothing has to be created.
void DrawSortArrow(HDC dc, int right, int centerY, bool up, COLORREF color)
{
    const int h = Scale(dc, 4);
    const int w = 2 * h;
    const int left = right - w;
    const int base = up ? centerY + h / 2 : centerY - h / 2;
    const int tip = up ? centerY - h / 2 : centerY + h / 2;
    const POINT points[3] = {{left, base}, {right, base}, {left + w / 2, tip}};

    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(DC_PEN));
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCPenColor(dc, color);
    SetDCBrushColor(dc, color);
    Polygon(dc, points, 3);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
}

void PaintHeader(HWND header, HDC dc, const PluginDarkModeColors& colors)
{
    RECT client;
    GetClientRect(header, &client);
    PluginDarkMode_FillRect(dc, &client, colors.DialogBackground);

    SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(header, WM_GETFONT, 0, 0)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, colors.DialogText);
    CSelectedPen pen(dc, colors.Border);

    const int textInset = Scale(dc, 6);
    const int dividerInset = Scale(dc, 2);
    const int arrowWidth = 2 * Scale(dc, 4);
    const int hot = GetHotItem(header);
    const int count = static_cast<int>(SendMessageW(header, HDM_GETITEMCOUNT, 0, 0));
    for (int i = 0; i < count; i++)
    {
        RECT rc;
        if (!SendMessageW(header, HDM_GETITEMRECT, i, reinterpret_cast<LPARAM>(&rc)))
            continue;
        if (i == hot)
            PluginDarkMode_FillRect(dc, &rc, colors.InputBackground); // slightly lighter than the background

        wchar_t text[256];
        const UINT format = ReadHeaderText(header, i, text, 256);
        UINT flags = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
        flags |= (format & HDF_JUSTIFYMASK) == HDF_RIGHT    ? DT_RIGHT
                 : (format & HDF_JUSTIFYMASK) == HDF_CENTER ? DT_CENTER
                                                            : DT_LEFT;
        RECT textRect = rc;
        InflateRect(&textRect, -textInset, 0);
        const bool sorted = (format & (HDF_SORTUP | HDF_SORTDOWN)) != 0;
        if (sorted)
        {
            // the arrow sits at the right end of the item; the text gets the rest
            DrawSortArrow(dc, textRect.right, (rc.top + rc.bottom) / 2, (format & HDF_SORTUP) != 0, colors.DialogText);
            textRect.right -= arrowWidth + textInset / 2;
        }
        DrawTextW(dc, text, -1, &textRect, flags);

        // divider on the right edge and line under the item
        MoveToEx(dc, rc.right - 1, rc.top + dividerInset, nullptr);
        LineTo(dc, rc.right - 1, rc.bottom - dividerInset);
        MoveToEx(dc, rc.left, rc.bottom - 1, nullptr);
        LineTo(dc, rc.right, rc.bottom - 1);
    }
}

void PaintTabs(HWND tabs, HDC dc, const PluginDarkModeColors& colors)
{
    RECT client;
    GetClientRect(tabs, &client);
    PluginDarkMode_FillRect(dc, &client, colors.DialogBackground);

    SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(tabs, WM_GETFONT, 0, 0)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, colors.DialogText);
    CSelectedPen pen(dc, colors.Border);
    SelectObject(dc, GetStockObject(NULL_BRUSH));

    const int selected = static_cast<int>(SendMessageW(tabs, TCM_GETCURSEL, 0, 0));
    const int count = static_cast<int>(SendMessageW(tabs, TCM_GETITEMCOUNT, 0, 0));

    // The frame around the page area: it starts under the row of tabs (the bottom of the first tab) and runs to the
    // edges of the control. The selected tab opens the top line below it (see the loop).
    RECT firstTab = {};
    if (count > 0)
        SendMessageW(tabs, TCM_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&firstTab));
    const int frameTop = count > 0 ? firstTab.bottom : 0;
    Rectangle(dc, client.left, frameTop, client.right, client.bottom);

    for (int i = 0; i < count; i++)
    {
        RECT rc;
        if (!SendMessageW(tabs, TCM_GETITEMRECT, i, reinterpret_cast<LPARAM>(&rc)))
            continue;
        // like the light theme: the selected tab is raised (taller and wider than the others), the others sit lower
        if (i == selected)
        {
            const int grow = Scale(dc, 2);
            rc.left -= grow;
            rc.right += grow;
            rc.top -= grow;
        }
        else
        {
            rc.top += Scale(dc, 1);
        }
        wchar_t text[256] = {};
        TCITEMW item = {};
        item.mask = TCIF_TEXT;
        item.pszText = text;
        item.cchTextMax = 256;
        SendMessageW(tabs, TCM_GETITEMW, i, reinterpret_cast<LPARAM>(&item));

        if (i == selected)
        {
            // the selected tab stands out slightly and is open at the bottom, joined with the page area
            RECT fill = {rc.left, rc.top, rc.right, rc.bottom + 1};
            PluginDarkMode_FillRect(dc, &fill, colors.InputBackground);
            MoveToEx(dc, rc.left, rc.bottom, nullptr);
            LineTo(dc, rc.left, rc.top);
            LineTo(dc, rc.right - 1, rc.top);
            LineTo(dc, rc.right - 1, rc.bottom);
        }
        else
        {
            Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
        }
        DrawTextW(dc, text, -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    }
}

// One paint: WM_PAINT gets its DC from BeginPaint (and must call EndPaint), WM_PRINTCLIENT gets a DC from the caller.
// The destructor always ends the paint and gives the DC back as it was received, also when drawing throws.
class CPaintScope
{
public:
    CPaintScope(HWND hwnd, UINT msg, WPARAM wParam) : hwnd_(hwnd), ownsPaint_(msg == WM_PAINT)
    {
        dc_ = ownsPaint_ ? BeginPaint(hwnd, &ps_) : reinterpret_cast<HDC>(wParam);
        saved_ = dc_ != nullptr ? SaveDC(dc_) : 0;
    }
    ~CPaintScope()
    {
        if (saved_ != 0)
            RestoreDC(dc_, saved_);
        if (ownsPaint_)
            EndPaint(hwnd_, &ps_);
    }
    CPaintScope(const CPaintScope&) = delete;
    CPaintScope& operator=(const CPaintScope&) = delete;
    HDC Dc() const { return dc_; }

private:
    HWND hwnd_;
    bool ownsPaint_;
    PAINTSTRUCT ps_ = {};
    HDC dc_ = nullptr;
    int saved_ = 0;
};

enum class Kind
{
    Header,
    Tabs
};

LRESULT CALLBACK DarkSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data)
{
    try
    {
        const bool isHeader = static_cast<Kind>(data) == Kind::Header;
        if (msg == WM_NCDESTROY)
        {
            RemoveWindowSubclass(hwnd, DarkSubclassProc, id);
            if (isHeader)
                RemovePropW(hwnd, HOT_PROP);
        }
        else if (msg == WM_ERASEBKGND)
            return 1; // WM_PAINT fills everything
        else if (isHeader && msg == WM_MOUSEMOVE)
            TrackHotItem(hwnd, lParam); // then the header handles the message as usual
        else if (isHeader && msg == WM_MOUSELEAVE)
            SetHotItem(hwnd, -1);
        else if (msg == WM_PAINT || msg == WM_PRINTCLIENT)
        {
            PluginDarkModeColors colors;
            if (PluginDarkMode_GetColors(&colors))
            {
                CPaintScope scope(hwnd, msg, wParam);
                if (scope.Dc() != nullptr)
                {
                    if (static_cast<Kind>(data) == Kind::Header)
                        PaintHeader(hwnd, scope.Dc(), colors);
                    else
                        PaintTabs(hwnd, scope.Dc(), colors);
                }
                return 0;
            }
        }
    }
    catch (...)
    {
        TRACE_E("DarkSubclassProc: unexpected exception.");
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Group box: only the frame and the caption are drawn. The inside is left alone because the controls that sit in the
// box are separate windows and the dialog already painted the dark background.
void PaintGroupBox(HWND box, HDC dc, const PluginDarkModeColors& colors)
{
    RECT client;
    GetClientRect(box, &client);
    SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(box, WM_GETFONT, 0, 0)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, colors.DialogText);

    wchar_t text[256];
    if (GetWindowTextW(box, text, 256) <= 0)
        text[0] = 0;
    const LONG style = GetWindowLongW(box, GWL_STYLE);

    // caption size and position (like the light group box: a few pixels from the left edge)
    RECT textRect = {};
    DrawTextW(dc, text, -1, &textRect, DT_CALCRECT | DT_SINGLELINE);
    const int textWidth = textRect.right - textRect.left;
    const int textHeight = textRect.bottom - textRect.top;
    const int margin = Scale(dc, 9);
    int left = client.left + margin;
    if ((style & BS_CENTER) == BS_CENTER)
        left = (client.left + client.right - textWidth) / 2;
    else if ((style & BS_CENTER) == BS_RIGHT)
        left = client.right - margin - textWidth;
    OffsetRect(&textRect, left - textRect.left, client.top);

    // the frame starts at the vertical middle of the caption
    {
        CSelectedPen pen(dc, colors.Border);
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, client.left, client.top + textHeight / 2, client.right, client.bottom);
    }

    // erase the part of the top line behind the caption, then draw the caption
    if (text[0] != 0)
    {
        RECT gap = textRect;
        InflateRect(&gap, Scale(dc, 3), 0);
        PluginDarkMode_FillRect(dc, &gap, colors.DialogBackground);
        DrawTextW(dc, text, -1, &textRect, DT_SINGLELINE | DT_LEFT);
    }
}

LRESULT CALLBACK GroupBoxSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR)
{
    try
    {
        if (msg == WM_NCDESTROY)
            RemoveWindowSubclass(hwnd, GroupBoxSubclassProc, id);
        else if (msg == WM_ERASEBKGND)
            return 1; // the dialog background is already dark; erasing would also wipe the controls' surroundings
        else if (msg == WM_PAINT || msg == WM_PRINTCLIENT)
        {
            PluginDarkModeColors colors;
            if (PluginDarkMode_GetColors(&colors))
            {
                CPaintScope scope(hwnd, msg, wParam);
                if (scope.Dc() != nullptr)
                    PaintGroupBox(hwnd, scope.Dc(), colors);
                return 0;
            }
        }
    }
    catch (...)
    {
        TRACE_E("GroupBoxSubclassProc: unexpected exception.");
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

bool IsRadioStyle(LONG type)
{
    return type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
}

bool IsCheckStyle(LONG type)
{
    return type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_3STATE || type == BS_AUTO3STATE;
}

// Check box and radio button: Windows draws their text with the theme colour (black), whatever the DC says, so the
// glyph comes from the theme and the text is drawn here (the Sally core does the same). Simplified: no hover state.
void PaintCheckRadio(HWND button, HDC dc, const PluginDarkModeColors& colors)
{
    RECT client;
    GetClientRect(button, &client);
    PluginDarkMode_FillRect(dc, &client, colors.DialogBackground);

    const bool radio = IsRadioStyle(GetWindowLongW(button, GWL_STYLE) & BS_TYPEMASK);
    const bool enabled = IsWindowEnabled(button) != FALSE;
    const LRESULT check = SendMessageW(button, BM_GETCHECK, 0, 0);
    const bool pressed = (SendMessageW(button, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;
    const int offset = !enabled ? 3 : (pressed ? 2 : 0); // NORMAL, HOT, PRESSED, DISABLED states follow each other
    int part = radio ? BP_RADIOBUTTON : BP_CHECKBOX;
    int state;
    if (radio)
        state = (check == BST_CHECKED ? RBS_CHECKEDNORMAL : RBS_UNCHECKEDNORMAL) + offset;
    else if (check == BST_INDETERMINATE)
        state = CBS_MIXEDNORMAL + offset;
    else
        state = (check == BST_CHECKED ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL) + offset;

    HFONT font = reinterpret_cast<HFONT>(SendMessageW(button, WM_GETFONT, 0, 0));
    if (font != nullptr)
        SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, enabled ? colors.DialogText : colors.DisabledText);

    // fallback when the theme gives no size: the classic 13 px glyph, scaled (GetSystemMetricsForDpi needs Windows 10)
    SIZE glyph = {Scale(dc, 13), Scale(dc, 13)};
    HTHEME theme = OpenThemeData(button, L"Button");
    if (theme != nullptr)
    {
        SIZE themed = {};
        if (SUCCEEDED(GetThemePartSize(theme, dc, part, state, nullptr, TS_TRUE, &themed)) && themed.cx > 0 && themed.cy > 0)
            glyph = themed;
    }
    RECT glyphRect = {client.left + Scale(dc, 2), client.top + (client.bottom - client.top - glyph.cy) / 2, 0, 0};
    glyphRect.right = glyphRect.left + glyph.cx;
    glyphRect.bottom = glyphRect.top + glyph.cy;
    if (theme != nullptr)
    {
        DrawThemeBackground(theme, dc, part, state, &glyphRect, nullptr);
        CloseThemeData(theme);
    }

    wchar_t text[256];
    if (GetWindowTextW(button, text, 256) > 0)
    {
        RECT textRect = client;
        textRect.left = glyphRect.right + Scale(dc, 4);
        DrawTextW(dc, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (GetFocus() == button)
        {
            RECT size = {};
            DrawTextW(dc, text, -1, &size, DT_CALCRECT | DT_SINGLELINE);
            RECT focus = {textRect.left - 1, (client.top + client.bottom - size.bottom) / 2 - 1,
                          min(textRect.right, textRect.left + size.right + 1), 0};
            focus.bottom = focus.top + size.bottom + 2;
            DrawFocusRect(dc, &focus);
        }
    }
}

LRESULT CALLBACK CheckRadioSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR)
{
    try
    {
        if (msg == WM_NCDESTROY)
            RemoveWindowSubclass(hwnd, CheckRadioSubclassProc, id);
        else if (msg == WM_ERASEBKGND)
            return 1; // WM_PAINT fills everything
        else if (msg == WM_PAINT || msg == WM_PRINTCLIENT)
        {
            PluginDarkModeColors colors;
            if (PluginDarkMode_GetColors(&colors))
            {
                CPaintScope scope(hwnd, msg, wParam);
                if (scope.Dc() != nullptr)
                    PaintCheckRadio(hwnd, scope.Dc(), colors);
                return 0;
            }
        }
        else if (msg == BM_SETCHECK || msg == BM_SETSTATE || msg == WM_ENABLE || msg == WM_SETFOCUS ||
                 msg == WM_KILLFOCUS || msg == WM_SETTEXT)
        {
            const LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
            InvalidateRect(hwnd, nullptr, FALSE); // the state changed: repaint with our painter
            return result;
        }
    }
    catch (...)
    {
        TRACE_E("CheckRadioSubclassProc: unexpected exception.");
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

BOOL CALLBACK SubclassButton(HWND child, LPARAM)
{
    wchar_t className[16];
    if (GetClassNameW(child, className, 16) <= 0 || _wcsicmp(className, L"Button") != 0)
        return TRUE;
    const LONG type = GetWindowLongW(child, GWL_STYLE) & BS_TYPEMASK;
    if (type == BS_GROUPBOX)
        SetWindowSubclass(child, GroupBoxSubclassProc, SUBCLASS_ID, 0);
    else if (IsRadioStyle(type) || IsCheckStyle(type))
        SetWindowSubclass(child, CheckRadioSubclassProc, SUBCLASS_ID, 0);
    else
        return TRUE;
    InvalidateRect(child, nullptr, TRUE);
    return TRUE;
}
} // namespace

void ApplyDarkButtons(HWND dialog)
{
    if (!PluginDarkMode_ShouldUseDark() || dialog == nullptr)
        return;
    EnumChildWindows(dialog, SubclassButton, 0);
}

void ApplyDarkListHeader(HWND listView)
{
    if (!PluginDarkMode_ShouldUseDark() || listView == nullptr)
        return;
    HWND header = ListView_GetHeader(listView);
    if (header != nullptr)
        SetWindowSubclass(header, DarkSubclassProc, SUBCLASS_ID, static_cast<DWORD_PTR>(Kind::Header));
}

void ApplyDarkTabControl(HWND tabControl)
{
    if (!PluginDarkMode_ShouldUseDark() || tabControl == nullptr)
        return;
    SetWindowSubclass(tabControl, DarkSubclassProc, SUBCLASS_ID, static_cast<DWORD_PTR>(Kind::Tabs));
    InvalidateRect(tabControl, nullptr, TRUE);
}
