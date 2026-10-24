// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "SallyUrls.h"
#include "supporter/Win32SupporterUi.h"
#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "cfgdlg.h"
#include "dialogs.h"
#include "darkmode.h"
#include "gui.h"
#include "md5.h"

#include <uxtheme.h>
#include <vssym32.h>
#include <ppl.h>

#include "svg.h"
#include "iconlist.h"
#include "common/artwork/SplashPixels.h"
#include "common/artwork/IdentityFonts.h"

#include "versinfo.rh2"

void GetDlgItemRectAndDestroy(HWND hWindow, int resID, RECT* r)
{
    HWND hItem = GetDlgItem(hWindow, resID);
    if (hItem == NULL)
    {
        r->left = r->top = r->right = r->bottom = 0;
        return;
    }
    GetWindowRect(hItem, r);
    MapWindowPoints(NULL, hWindow, (POINT*)r, 2);
    DestroyWindow(hItem);
}

//*****************************************************************************
//
// Splash Screen
//

CSplashScreen::CSplashScreen()
    : CDialog(HInstance, IDD_SPLASH, NULL, ooStatic)
{
    Bitmap = NULL;
    OriginalBitmap = NULL;
    HNormalFont = NULL;
    HBoldFont = NULL;
    Width = 0;
    Height = 0;

    HTaglineFont = NULL;
}

CSplashScreen::~CSplashScreen()
{
    // by this time the bitmap should be destroyed, but do it again just in case
    DestroyBitmap();
    if (HNormalFont != NULL)
        HANDLES(DeleteObject(HNormalFont));
    if (HBoldFont != NULL)
        HANDLES(DeleteObject(HBoldFont));
    if (HTaglineFont != NULL)
        HANDLES(DeleteObject(HTaglineFont));
}

namespace
{
    HFONT SplashFont(int pixels, const wchar_t* face, bool bold)
    {
        HFONT font = Sally::Artwork::CreateIdentityFont(pixels, face, bold);
        if (font) HANDLES_ADD(__htFont, __hoCreateFontIndirect, font);
        return font;
    }

    bool PaintSplashMascot(HDC dc, int width, int height, const Sally::Artwork::Rect* placement = nullptr)
    {
        HRSRC resource = FindResourceW(HInstance, MAKEINTRESOURCEW(IDB_GECKO_SPLASH), RT_RCDATA);
        if (!resource)
            return false;
        const DWORD size = SizeofResource(HInstance, resource);
        const void* data = LockResource(LoadResource(HInstance, resource));
        if (!data || !size)
            return false;
        std::vector<unsigned char> png;
        try
        {
            if (!Sally::Artwork::EmbeddedPng(std::string_view(static_cast<const char*>(data), size), png))
                return false;
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        HBITMAP original = LoadRawPNGBitmap(png.data(), static_cast<DWORD>(png.size()), SALPNG_GETALPHA | SALPNG_PREMULTIPLE);
        if (!original)
            return false;
        DIBSECTION dib = {};
        std::vector<unsigned char> overlay;
        try
        {
            // LoadRawPNGBitmap creates top-down BGRA storage; GetObject may report
            // the height normalized to a positive value for that same DIB section.
            if (GetObject(original, sizeof(dib), &dib) == sizeof(dib) && dib.dsBm.bmBits && dib.dsBm.bmBitsPixel == 32)
                overlay = Sally::Artwork::MascotOverlay(static_cast<const unsigned char*>(dib.dsBm.bmBits),
                                                        dib.dsBm.bmWidth, dib.dsBm.bmHeight, width, height, placement);
        }
        catch (const std::bad_alloc&)
        {
        }
        HANDLES(DeleteObject(original));
        if (overlay.empty())
            return false;
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = NULL;
        HBITMAP bitmap = HANDLES(CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, NULL, 0));
        if (!bitmap)
            return false;
        memcpy(bits, overlay.data(), overlay.size());
        HDC source = HANDLES(CreateCompatibleDC(dc));
        if (!source)
        {
            HANDLES(DeleteObject(bitmap));
            return false;
        }
        HGDIOBJ previous = SelectObject(source, bitmap);
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        const BOOL painted = ::AlphaBlend(dc, 0, 0, width, height, source, 0, 0, width, height, blend);
        SelectObject(source, previous);
        HANDLES(DeleteDC(source));
        HANDLES(DeleteObject(bitmap));
        return painted != FALSE;
    }
    void IdentityText(HDC dc, const wchar_t* text, RECT bounds, HFONT font, COLORREF color, UINT flags = DT_NOPREFIX | DT_WORDBREAK)
    {
        const int saved = SaveDC(dc);
        IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        SelectObject(dc, font);
        DrawTextW(dc, text, -1, &bounds, flags);
        RestoreDC(dc, saved);
    }

    class IdentityWordmark : public CSVGSprite
    {
    public:
        bool LoadColor(int width, COLORREF color)
        {
            // Keep the classic SVG bytes, but crop its padded viewport to the same
            // original glyph bounds used by the approved HTML vector mask.
            if (!Load(IDB_LOGO_TEXT, static_cast<int>(std::lround(width * 196.3 / 86.9713)), -1, SVGSTATE_ORIGINAL)) return false;
            DIBSECTION dib = {};
            if (GetObject(HBitmaps[0], sizeof(dib), &dib) != sizeof(dib) || !dib.dsBm.bmBits) return false;
            const double scale = Width / 196.3;
            const int left = static_cast<int>(std::lround(8.9297 * scale)), top = static_cast<int>(std::lround(10.1957 * scale));
            const int croppedWidth = static_cast<int>(std::lround(86.9713 * scale)), croppedHeight = static_cast<int>(std::lround(35.875 * scale));
            if (left < 0 || top < 0 || left + croppedWidth > Width || top + croppedHeight > Height) return false;
            HBITMAP cropped = NULL;
            void* memory = NULL;
            CreateDIB(croppedWidth, croppedHeight, &cropped, &memory);
            if (!cropped || !memory) return false;
            auto pixels = static_cast<unsigned char*>(memory);
            const auto source = static_cast<const unsigned char*>(dib.dsBm.bmBits);
            for (int y = 0; y < croppedHeight; ++y)
                memcpy(pixels + size_t(y) * croppedWidth * 4, source + (size_t(y + top) * Width + left) * 4, size_t(croppedWidth) * 4);
            for (int i = 0; i < croppedWidth * croppedHeight; ++i)
            {
                const unsigned a = pixels[i * 4 + 3];
                pixels[i * 4] = static_cast<unsigned char>((GetBValue(color) * a + 127) / 255);
                pixels[i * 4 + 1] = static_cast<unsigned char>((GetGValue(color) * a + 127) / 255);
                pixels[i * 4 + 2] = static_cast<unsigned char>((GetRValue(color) * a + 127) / 255);
            }
            HANDLES(DeleteObject(HBitmaps[0]));
            HBitmaps[0] = cropped;
            Width = croppedWidth; Height = croppedHeight;
            return true;
        }
    };

    void PaintIdentityCanvas(HDC dc, int width, int height, bool about, const Sally::Artwork::IdentityStyle& style, int footerHeight = 0)
    {
        const auto colors = Sally::Artwork::Colors(style);
        const double scale = width / Sally::Artwork::CanvasWidth(about);
        auto px = [scale](double n) { return static_cast<int>(std::lround(n * scale)); };
        RECT canvas = {0, 0, width, height};
        SetBkColor(dc, colors.Paper);
        ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &canvas, L"", 0, NULL);
        const int bandTop = about ? height - (footerHeight ? footerHeight : px(68.2)) : height - px(89.6);
        RECT solid = {0, bandTop, px(440), height};
        SetBkColor(dc, colors.Band);
        ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &solid, L"", 0, NULL);
        TRIVERTEX vertices[2] = {{solid.right, bandTop, COLOR16(GetRValue(colors.Band) << 8), COLOR16(GetGValue(colors.Band) << 8), COLOR16(GetBValue(colors.Band) << 8), 0},
                                {width, height, COLOR16(GetRValue(colors.BandLight) << 8), COLOR16(GetGValue(colors.BandLight) << 8), COLOR16(GetBValue(colors.BandLight) << 8), 0}};
        GRADIENT_RECT mesh = {0, 1};
        GradientFill(dc, vertices, 2, &mesh, 1, GRADIENT_FILL_RECT_H);
        const auto placement = Sally::Artwork::ArtPlacement(width, about);
        const Sally::Artwork::Rect art = {placement.Left, placement.Top, width, placement.Top + placement.Side};
        if (!PaintSplashMascot(dc, width, height, &art))
        {
            CSVGSprite fallback;
            if (fallback.Load(IDB_LOGO_HAND, placement.Side, -1, SVGSTATE_ORIGINAL))
                fallback.AlphaBlend(dc, placement.Left, px(16), -1, -1, SVGSTATE_ORIGINAL);
        }
        RECT word = {px(33.7), about ? px(16) : px(77.8), px(442), about ? px(106) : px(167.8)};
        IdentityWordmark vector;
        if (style.Lettering == 0 && vector.LoadColor(px(205.4), colors.Brand))
            vector.AlphaBlend(dc, word.left, word.top, -1, -1, SVGSTATE_ORIGINAL);
        else
        {
            HFONT font = SplashFont(px(84.48), Sally::Artwork::LetterFace(style), style.Lettering != 0 && style.Lettering != 5);
            IdentityText(dc, L"Sally", word, font, colors.Brand, DT_NOPREFIX | DT_SINGLELINE);
            HANDLES(DeleteObject(font));
        }
        RECT tagline = {px(33.7), about ? px(108) : px(190.3), px(about ? 475 : 426), about ? px(138.5) : px(230)};
        HFONT font = SplashFont(px(about ? 21.56 : 27.28), Sally::Artwork::TextFace(style), false);
        const auto caption = LoadStrOwned(IDS_SPLASH_TAGLINE);
        HFONT fitted = Sally::Artwork::FitIdentityLine(dc, font, caption.c_str(), tagline.right - tagline.left);
        if (fitted) HANDLES_ADD(__htFont, __hoCreateFontIndirect, fitted);
        IdentityText(dc, caption.c_str(), tagline, fitted ? fitted : font, colors.Muted, DT_NOPREFIX | DT_SINGLELINE | DT_VCENTER);
        if (fitted) HANDLES(DeleteObject(fitted));
        HANDLES(DeleteObject(font));
        HFONT fineprint = SplashFont(px(about ? 12 : 12.4), Sally::Artwork::TextFace(style), false);
        RECT credits = {px(33.7), height - px(about ? 47.5 : 44), px(412), height - px(about ? 14 : 8)};
        IdentityText(dc, Sally::Artwork::IdentityCopyright, credits, fineprint, colors.Credit);
        HANDLES(DeleteObject(fineprint));
    }
} // namespace

BOOL CSplashScreen::PaintText(const wchar_t* text, const RECT& bounds, HFONT font, COLORREF clr, bool wrap, bool ellipsis)
{
    if (!Bitmap || !font || !text)
        return FALSE;
    HDC dc = Bitmap->HMemDC;
    RECT rectangle = bounds;
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, clr);
    HGDIOBJ previous = SelectObject(dc, font);
    HFONT fitted = NULL;
    const UINT flags = DT_NOPREFIX | (wrap ? DT_WORDBREAK | DT_EDITCONTROL : DT_SINGLELINE) | (ellipsis ? DT_END_ELLIPSIS : 0);
    LOGFONTW description = {};
    GetObjectW(font, sizeof(description), &description);
    // Measure actual translated text/notices. Never ellipsize away copyright.
    for (int pixels = -description.lfHeight; !ellipsis && pixels > 1; --pixels)
    {
        RECT measured = {0, 0, bounds.right - bounds.left, 0};
        DrawTextW(dc, text, -1, &measured, flags | DT_CALCRECT);
        if ((wrap ? measured.bottom <= bounds.bottom - bounds.top : measured.right <= bounds.right - bounds.left))
            break;
        SelectObject(dc, previous);
        if (fitted)
            HANDLES(DeleteObject(fitted));
        description.lfHeight = -(pixels - 1);
        fitted = HANDLES(CreateFontIndirectW(&description));
        SelectObject(dc, fitted ? fitted : font);
        if (!fitted)
            break;
    }
    DrawTextW(dc, text, -1, &rectangle, flags);
    SelectObject(dc, previous);
    if (fitted)
        HANDLES(DeleteObject(fitted));
    RestoreDC(dc, saved);
    return TRUE;
}

BOOL CSplashScreen::PrepareBitmap()
{
    DestroyBitmap();
    Bitmap = new CBitmap();
    OriginalBitmap = new CBitmap();
    HDC screen = HANDLES(GetDC(NULL));
    const bool created = Bitmap && OriginalBitmap && Bitmap->CreateBmp(screen, Width, Height) &&
                         OriginalBitmap->CreateBmp(screen, Width, Height);
    HANDLES(ReleaseDC(NULL, screen));
    if (!created)
    {
        DestroyBitmap();
        return FALSE;
    }
    HDC dc = Bitmap->HMemDC;
    RECT canvas = {0, 0, Width, Height};
    SetBkColor(dc, RGB(247, 248, 247));
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &canvas, L"", 0, NULL);
    PaintIdentityCanvas(dc, Width, Height, false, Identity);
    PaintText(SALAMANDER_TEXT_VERSIONW(), VersionR, HNormalFont, Sally::Artwork::Colors(Identity).Muted);
    BitBlt(OriginalBitmap->HMemDC, 0, 0, Width, Height, dc, 0, 0, SRCCOPY);
    return TRUE;
}

void CSplashScreen::SetText(const wchar_t* text)
{
    if (Bitmap != NULL && OriginalBitmap != NULL)
    {
        // restore the background
        BitBlt(Bitmap->HMemDC, StatusR.left, StatusR.top, StatusR.right - StatusR.left, StatusR.bottom - StatusR.top, OriginalBitmap->HMemDC, StatusR.left, StatusR.top, SRCCOPY);
        PaintText(text, StatusR, HNormalFont, StatusColor, false, true);

        // if visible, update the display with the change
        if (HWindow != NULL)
        {
            HDC hDC = HANDLES(GetDC(HWindow));
            BitBlt(hDC, StatusR.left, StatusR.top, StatusR.right - StatusR.left, StatusR.bottom - StatusR.top, Bitmap->HMemDC, StatusR.left, StatusR.top, SRCCOPY);
            HANDLES(ReleaseDC(HWindow, hDC));
        }
    }
}

void CSplashScreen::DestroyBitmap()
{
    if (Bitmap != NULL)
    {
        delete Bitmap;
        Bitmap = NULL;
    }
    if (OriginalBitmap != NULL)
    {
        delete OriginalBitmap;
        OriginalBitmap = NULL;
    }
}

INT_PTR
CSplashScreen::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CSplashScreen::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // Size the approved composition in DPI-scaled pixels, within the work area.
        const auto getDpi = reinterpret_cast<UINT(WINAPI*)(HWND)>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
        const UINT dpi = getDpi ? getDpi(HWindow) : static_cast<UINT>(GetSystemDPI());
        MONITORINFO monitor = {sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(HWindow, MONITOR_DEFAULTTONEAREST), &monitor);
        Identity = Configuration.Identity;
        Sally::Artwork::Normalize(Identity);
        StatusColor = Sally::Artwork::Colors(Identity).Credit;
        const int wantedWidth = MulDiv(Sally::Artwork::SplashLogicalWidth, dpi ? dpi : 96, 96);
        const int wantedHeight = MulDiv(Sally::Artwork::SplashLogicalHeight, dpi ? dpi : 96, 96);
        const double fit = min(1.0, min((monitor.rcWork.right - monitor.rcWork.left - 24.0) / wantedWidth,
                                        (monitor.rcWork.bottom - monitor.rcWork.top - 24.0) / wantedHeight));
        Width = max(1, static_cast<int>(wantedWidth * fit));
        Height = max(1, static_cast<int>(wantedHeight * fit));
        RECT window = {0, 0, Width, Height};
        AdjustWindowRectEx(&window, GetWindowLong(HWindow, GWL_STYLE), FALSE, GetWindowLong(HWindow, GWL_EXSTYLE));
        SetWindowPos(HWindow, NULL, 0, 0, window.right - window.left, window.bottom - window.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        RECT unused;
        GetDlgItemRectAndDestroy(HWindow, IDC_SPLASH_OPENSAL, &unused);
        GetDlgItemRectAndDestroy(HWindow, IDC_SPLASH_VERSION, &unused);
        GetDlgItemRectAndDestroy(HWindow, IDC_SPLASH_COPYRIGHT, &unused);
        GetDlgItemRectAndDestroy(HWindow, IDC_SPLASH_STATUS, &unused);
        auto px = [&](int n) { return MulDiv(n, Width, 880); };
        VersionR = {px(34), px(259), px(426), px(292)};
        StatusR = {px(34), Height - px(80), px(426), Height - px(50)};
        if (HNormalFont)
            HANDLES(DeleteObject(HNormalFont));
        if (HBoldFont)
            HANDLES(DeleteObject(HBoldFont));
        if (HTaglineFont)
            HANDLES(DeleteObject(HTaglineFont));
        HNormalFont = SplashFont(px(18), Sally::Artwork::TextFace(Identity), false);
        HBoldFont = NULL;
        HTaglineFont = NULL;
        PrepareBitmap();
        break;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HANDLES(BeginPaint(HWindow, &ps));
        if (Bitmap)
            BitBlt(ps.hdc, 0, 0, Width, Height, Bitmap->HMemDC, 0, 0, SRCCOPY);
        HANDLES(EndPaint(HWindow, &ps));
        return FALSE;
    }

    case WM_ERASEBKGND:
    {
        return TRUE;
    }
    }

    return CDialog::DialogProc(uMsg, wParam, lParam);
}

CSplashScreen SplashScreen;

BOOL SplashScreenOpen()
{
    if (SplashScreen.HWindow != NULL)
    {
        TRACE_E("SplashScreenOpen(): splash screen already exists!");
        return FALSE;
    }

    SplashScreen.Create();

    if (SplashScreen.HWindow != NULL)
    {
        MultiMonCenterWindow(SplashScreen.HWindow, NULL, FALSE);
        ShowWindow(SplashScreen.HWindow, SW_SHOWNOACTIVATE);
        UpdateWindow(SplashScreen.HWindow);
        return TRUE;
    }
    else
    {
        SplashScreen.DestroyBitmap();
        return FALSE;
    }
}

void SplashScreenCloseIfExist()
{
    if (SplashScreen.HWindow != NULL)
    {
        DestroyWindow(SplashScreen.HWindow);
        SplashScreen.DestroyBitmap();
    }
}

BOOL ExistSplashScreen()
{
    return (SplashScreen.HWindow != NULL);
}

void IfExistSetSplashScreenText(const wchar_t* text)
{
    if (SplashScreen.HWindow != NULL)
        SplashScreen.SetText(text);
}

HWND GetSplashScreenHandle()
{
    return SplashScreen.HWindow;
}

//
// ****************************************************************************
// CAboutDialog
//

class CIdentityHyperLink : public CHyperLink
{
public:
    CIdentityHyperLink(HWND parent, int id) : CHyperLink(parent, id, STF_UNDERLINE) {}
    void SetIdentityFont(HFONT font)
    {
        HFONT replacement = Sally::Artwork::CreateIdentityLinkFont(font);
        if (!replacement) return;
        HANDLES_ADD(__htFont, __hoCreateFontIndirect, replacement);
        if (HFont && DestroyFont) HANDLES(DeleteObject(HFont));
        HFont = replacement; DestroyFont = TRUE;
        CHyperLink::WindowProc(WM_SETFONT, reinterpret_cast<WPARAM>(replacement), FALSE);
        PrepareForPaint();
        InvalidateRect(HWindow, NULL, TRUE);
    }
};

CAboutDialog::CAboutDialog(HWND parent, const Sally::Artwork::IdentityStyle* preview)
    : CCommonDialog(HLanguage, IDD_ABOUT, parent)
{
    Identity = preview ? *preview : Configuration.Identity;
    Sally::Artwork::Normalize(Identity);
    HGradientBkBrush = HANDLES(CreateSolidBrush(Sally::Artwork::Colors(Identity).Band));
    BackgroundBitmap = NULL;
    IdentityFont = DetailFont = NULL;
    IdentityScale = 1;
    IdentityLinks[0] = IdentityLinks[1] = NULL;
}

CAboutDialog::~CAboutDialog()
{
    if (BackgroundBitmap != NULL)
        delete BackgroundBitmap;
    HANDLES(DeleteObject(HGradientBkBrush));
    if (IdentityFont) HANDLES(DeleteObject(IdentityFont));
    if (DetailFont) HANDLES(DeleteObject(DetailFont));
}

void CAboutDialog::LayoutIdentity()
{
    const auto getDpi = reinterpret_cast<UINT(WINAPI*)(HWND)>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    const unsigned dpi = getDpi ? getDpi(HWindow) : static_cast<unsigned>(GetSystemDPI());
    MONITORINFO monitor = {sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(HWindow, MONITOR_DEFAULTTONEAREST), &monitor);
    const int availableWidth = monitor.rcWork.right - monitor.rcWork.left - 32;
    const int availableHeight = monitor.rcWork.bottom - monitor.rcWork.top - 32;
    // Trim the canvas width while preserving the approved body/control font sizes.
    IdentityScale = (std::min)((dpi / 96.0) * (720.0 / 880.0), availableWidth / Sally::Artwork::AboutCanvasWidth);
    HDC dc = HANDLES(GetDC(HWindow));
    auto text = [&](int id) {
        HWND control = GetDlgItem(HWindow, id);
        std::wstring value(static_cast<size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
        value.resize(GetWindowTextW(control, value.data(), static_cast<int>(value.size())));
        return value;
    };
    int width = 0, height = 0, licenseHeight = 0, supportTop = 0, supportHeight = 0, footerHeight = 0;
    int buttonWidths[3] = {};
    const int buttons[] = {IDOK, 62045, 62044};
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        auto px = [&](double n) { return (std::max)(1, static_cast<int>(std::lround(n * IdentityScale))); };
        width = px(Sally::Artwork::AboutCanvasWidth);
        HFONT font = SplashFont(px(15), Sally::Artwork::TextFace(Identity), false);
        HFONT detail = SplashFont(px(13.2), Sally::Artwork::TextFace(Identity), false);
        for (int id : {IDS_ABOUT_SALAMANDER, IDC_ABOUT_WEBSITE, IDC_ABOUT_WWW, 62044, 62045, IDOK})
            SendDlgItemMessageW(HWindow, id, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        for (int id : {IDC_STATIC_5, 62043})
            SendDlgItemMessageW(HWindow, id, WM_SETFONT, reinterpret_cast<WPARAM>(detail), FALSE);
        for (auto link : IdentityLinks)
            if (link) link->SetIdentityFont(font);
        if (IdentityFont) HANDLES(DeleteObject(IdentityFont));
        if (DetailFont) HANDLES(DeleteObject(DetailFont));
        IdentityFont = font; DetailFont = detail;
        auto measure = [&](int id) {
            RECT r = {0, 0, px(431), 0};
            const auto value = text(id);
            const auto old = SelectObject(dc, detail);
            DrawTextW(dc, value.c_str(), -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(dc, old);
            return r.bottom;
        };
        licenseHeight = measure(IDC_STATIC_5);
        supportHeight = measure(62043);
        supportTop = (std::max)(px(272), px(229) + licenseHeight + px(26));
        int totalButtons = px(44);
        const auto previous = SelectObject(dc, font);
        for (int i = 0; i < 3; ++i)
        {
            const auto value = text(buttons[i]);
            SIZE size = {};
            GetTextExtentPoint32W(dc, value.c_str(), static_cast<int>(value.size()), &size);
            buttonWidths[i] = (std::max)(px(84), static_cast<int>(size.cx) + px(28));
            totalButtons += buttonWidths[i] + px(10);
        }
        SelectObject(dc, previous);
        footerHeight = px(totalButtons > width - px(388) ? 112 : 68.2);
        height = (std::max)(px(398), supportTop + supportHeight + px(22) + footerHeight);
        if (height <= availableHeight) break;
        IdentityScale *= availableHeight / double(height);
    }
    auto px = [&](double n) { return static_cast<int>(std::lround(n * IdentityScale)); };
    RECT outer = {0, 0, width, height};
    AdjustWindowRectEx(&outer, GetWindowLongW(HWindow, GWL_STYLE), FALSE, GetWindowLongW(HWindow, GWL_EXSTYLE));
    SetWindowPos(HWindow, NULL, 0, 0, outer.right - outer.left, outer.bottom - outer.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    auto place = [&](int id, int x, int y, int w, int h) {
        SetWindowPos(GetDlgItem(HWindow, id), NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    };
    for (int id : {IDC_ABOUT_OPENSAL, IDC_ABOUT_LOGO, IDC_ABOUT_BOTTOM, IDC_STATIC_1, IDC_STATIC_3, IDC_STATIC_4, IDC_STATIC_6})
        ShowWindow(GetDlgItem(HWindow, id), SW_HIDE);
    const auto previousFont = SelectObject(dc, IdentityFont);
    const auto versionText = text(IDS_ABOUT_SALAMANDER);
    SIZE versionSize = {};
    GetTextExtentPoint32W(dc, versionText.c_str(), static_cast<int>(versionText.size()), &versionSize);
    SelectObject(dc, previousFont);
    const int pillWidth = (std::min)(px(431), static_cast<int>(versionSize.cx) + px(18));
    place(IDS_ABOUT_SALAMANDER, px(43), px(153), pillWidth - px(18), px(24));
    place(IDC_ABOUT_WEBSITE, px(34), px(194), px(100), px(25));
    place(IDC_ABOUT_WWW, px(152), px(194), px(135), px(25));
    place(IDC_STATIC_5, px(34), px(229), px(431), licenseHeight);
    place(62043, px(34), supportTop, px(431), supportHeight);
    int right = width - px(22);
    for (int i = 0; i < 3; ++i)
    {
        place(buttons[i], right - buttonWidths[i], height - footerHeight + px(16), buttonWidths[i], px(36));
        right -= buttonWidths[i] + px(10);
    }
    if (BackgroundBitmap) delete BackgroundBitmap;
    BackgroundBitmap = new CBitmap();
    if (BackgroundBitmap->CreateBmp(dc, width, height))
    {
        PaintIdentityCanvas(BackgroundBitmap->HMemDC, width, height, true, Identity, footerHeight);
        const auto colors = Sally::Artwork::Colors(Identity);
        HDC background = BackgroundBitmap->HMemDC;
        HPEN pen = HANDLES(CreatePen(PS_SOLID, (std::max)(1, px(1)), colors.Line));
        HBRUSH brush = HANDLES(CreateSolidBrush(colors.Wash));
        const auto oldPen = SelectObject(background, pen), oldBrush = SelectObject(background, brush);
        RoundRect(background, px(34), px(149), px(34) + pillWidth, px(180), px(6), px(6));
        SelectObject(background, oldPen); SelectObject(background, oldBrush);
        HANDLES(DeleteObject(pen)); HANDLES(DeleteObject(brush));
        RECT rule = {px(34), supportTop - px(14), px(465), supportTop - px(14) + (std::max)(1, px(1))};
        SetBkColor(background, colors.Line);
        ExtTextOutW(background, 0, 0, ETO_OPAQUE, &rule, L"", 0, NULL);
    }
    else { delete BackgroundBitmap; BackgroundBitmap = NULL; }
    HANDLES(ReleaseDC(HWindow, dc));
    InvalidateRect(HWindow, NULL, TRUE);
}

HDWP OffsetControl(HWND hWindow, HDWP hdwp, int id, int yOffset)
{
    HWND hCtrl = GetDlgItem(hWindow, id);
    RECT r;
    GetWindowRect(hCtrl, &r);
    ScreenToClient(hWindow, (LPPOINT)&r);

    hdwp = HANDLES(DeferWindowPos(hdwp, hCtrl, NULL, r.left, r.top + yOffset, 0, 0, SWP_NOSIZE | SWP_NOZORDER));
    return hdwp;
}

CBitmap*
AboutAndEvalDlgCreateBkgnd(HWND hWindow)
{
    RECT opensalR;
    RECT logoR;
    RECT gradR;
    GetDlgItemRectAndDestroy(hWindow, IDC_ABOUT_OPENSAL, &opensalR);
    GetDlgItemRectAndDestroy(hWindow, IDC_ABOUT_LOGO, &logoR);
    GetDlgItemRectAndDestroy(hWindow, IDC_ABOUT_BOTTOM, &gradR);

    RECT r;
    GetClientRect(hWindow, &r);

    CBitmap* bitmap = new CBitmap();
    HDC hDC = HANDLES(GetDC(NULL));
    if (!bitmap->CreateBmp(hDC, r.right - r.left, r.bottom - r.top))
    {
        delete bitmap;
        bitmap = NULL;
    }
    HANDLES(ReleaseDC(NULL, hDC));
    if (bitmap == NULL)
        return NULL;

    hDC = bitmap->HMemDC;

    // paint the background white
    SetBkColor(hDC, RGB(255, 255, 255));
    ExtTextOutW(hDC, 0, 0, ETO_OPAQUE, &r, L"", 0, NULL);

    CSVGSprite svgText;
    CSVGSprite svgGrad;
    CSVGSprite svgHand;
    concurrency::parallel_invoke(
        [&]
        { svgText.Load(IDB_LOGO_TEXT, opensalR.right - opensalR.left, opensalR.bottom - opensalR.top, SVGSTATE_ORIGINAL); },
        [&]
        { svgGrad.Load(IDB_ABOUT_GRAD, r.right - r.left, -1, SVGSTATE_ORIGINAL); },
        [&]
        { svgHand.Load(IDB_LOGO_HAND, logoR.right - logoR.left, logoR.bottom - logoR.top, SVGSTATE_ORIGINAL); });

    SIZE textSize, gradSize, handSize;
    svgText.GetSize(&textSize);
    svgGrad.GetSize(&gradSize);
    svgHand.GetSize(&handSize);

    svgText.AlphaBlend(hDC, opensalR.left, opensalR.top, textSize.cx, textSize.cy, SVGSTATE_ORIGINAL);
    svgGrad.AlphaBlend(hDC, 0, gradR.top, gradSize.cx, r.bottom - r.top - gradR.top, SVGSTATE_ORIGINAL);
    svgHand.AlphaBlend(hDC, r.right - r.left - handSize.cx, 0, handSize.cx, handSize.cy, SVGSTATE_ORIGINAL);

    return bitmap;
}

void AboutAndEvalDlgPaintBkgnd(HWND hWindow, HDC hDC, CBitmap* bitmap)
{
    BitBlt(hDC, 0, 0, bitmap->GetWidth(), bitmap->GetHeight(), bitmap->HMemDC, 0, 0, SRCCOPY);
}

INT_PTR
CAboutDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CAboutDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);

    auto refreshSupporter = [&]()
    {
        const auto state = Sally::Supporter::ReadSupporterState(SALAMANDER_ROOT_REG, *gRegistry);
        const auto certificate = Sally::Supporter::VerifyCertificate(state.Token, Sally::Supporter::NowUtc(), Sally::Supporter::BuiltinPublicKeys());
        SetDlgItemTextW(HWindow, 62043, Sally::Supporter::SupporterSummary(certificate, LanguageID).c_str());
        SetDlgItemTextW(HWindow, 62044, Sally::Supporter::SupporterText(IDS_SUP_SUPPORT, LanguageID).c_str());
        SetDlgItemTextW(HWindow, 62045, LoadStrW(IDS_MENU_HELP_ABOUTPLUGINS));
        ShowWindow(GetDlgItem(HWindow, 62044), SW_SHOWNA);
        LayoutIdentity();
    };

    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        CHyperLink* hl;

        DarkMode_SetLightSurface(HWindow, TRUE);

        SetDlgItemTextW(HWindow, IDS_ABOUT_SALAMANDER, SALAMANDER_TEXT_VERSIONW());
        HWND close = GetDlgItem(HWindow, IDOK);
        SetWindowLongPtrW(close, GWL_STYLE, (GetWindowLongPtrW(close, GWL_STYLE) & ~BS_TYPEMASK) | BS_OWNERDRAW);
        SendMessageW(HWindow, DM_SETDEFID, IDOK, 0);
        //      new CStaticText(HWindow, IDS_ABOUT_FIRM, STF_BOLD);

        // if the environment is in Czech or in Slovak, show the Czech website automatically
        BOOL english = LanguageID != 0x405 /* Czech */ && LanguageID != 0x41B /* Slovak */;

        IdentityLinks[0] = new CIdentityHyperLink(HWindow, IDC_ABOUT_WEBSITE);
        hl = IdentityLinks[0];
        if (hl != NULL)
        {
            const wchar_t* url = Sally::Urls::Website;
            std::wstring caption = LoadStrOwned(IDS_IDENTITY_WEBSITE);
            SetDlgItemTextW(HWindow, IDC_ABOUT_WEBSITE, caption.c_str());
            hl->SetActionOpen(url);
        }

        IdentityLinks[1] = new CIdentityHyperLink(HWindow, IDC_ABOUT_WWW);
        hl = IdentityLinks[1];
        if (hl != NULL)
        {
            const wchar_t* url = Sally::Urls::Repository;
            SetDlgItemTextW(HWindow, IDC_ABOUT_WWW, L"GitHub ↗");
            hl->SetActionOpen(url);
        }

        refreshSupporter();
        break;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == 62044)
        {
            Sally::Supporter::ShowSupportHub(HWindow, LanguageID, SALAMANDER_ROOT_REG, *gRegistry);
            refreshSupporter();
            return TRUE;
        }
        if (LOWORD(wParam) == 62045)
        {
            HMENU menu = CreatePopupMenu();
            if (!menu) return TRUE;
            for (int i = 0; i < Plugins.GetCount(); ++i)
            {
                auto name = Plugins.Get(i)->Name;
                for (size_t at = 0; (at = name.find(L'&', at)) != std::wstring::npos; at += 2) name.insert(at, 1, L'&');
                AppendMenuW(menu, MF_STRING, i + 1, name.c_str());
            }
            RECT button{}; GetWindowRect(GetDlgItem(HWindow, 62045), &button);
            const UINT item = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                button.left, button.bottom, 0, HWindow, nullptr);
            DestroyMenu(menu);
            if (item && item <= static_cast<UINT>(Plugins.GetCount())) Plugins.OnPluginAbout(HWindow, item - 1);
            return TRUE;
        }
        break;

    case WM_DRAWITEM:
    {
        const auto item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item && item->CtlID == IDOK && item->CtlType == ODT_BUTTON)
        {
            const auto colors = Sally::Artwork::Colors(Identity);
            HBRUSH brush = HANDLES(CreateSolidBrush(colors.Brand));
            FillRect(item->hDC, &item->rcItem, brush);
            HANDLES(DeleteObject(brush));
            RECT bounds = item->rcItem;
            std::wstring caption(static_cast<size_t>(GetWindowTextLengthW(item->hwndItem)) + 1, L'\0');
            GetWindowTextW(item->hwndItem, caption.data(), static_cast<int>(caption.size()));
            IdentityText(item->hDC, caption.c_str(), bounds, IdentityFont, RGB(255, 255, 255), DT_NOPREFIX | DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (item->itemState & ODS_FOCUS)
            {
                InflateRect(&bounds, -3, -3);
                DrawFocusRect(item->hDC, &bounds);
            }
            return TRUE;
        }
        break;
    }

    case WM_DESTROY:
    {
        DarkMode_SetLightSurface(HWindow, FALSE);
        break;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdcStatic = (HDC)wParam;
        HWND hwndStatic = (HWND)lParam;
        int resID = GetWindowLong(hwndStatic, GWL_ID);
        const auto colors = Sally::Artwork::Colors(Identity);
        COLORREF textClr = colors.Muted;
        switch (resID)
        {
        case IDS_ABOUT_SALAMANDER:
            textClr = colors.Brand;
            break;
        case IDC_ABOUT_WEBSITE:
        case IDC_ABOUT_WWW:
            textClr = colors.Link;
            break;
        case IDC_STATIC_6:
        case IDC_STATIC_7:
        case IDC_STATIC_8:
            textClr = RGB(128, 128, 128);
            break;
        }
        SetTextColor(hdcStatic, textClr);
        SetBkMode(hdcStatic, TRANSPARENT);
        SetBkColor(hdcStatic, colors.Paper);
        return (BOOL)(UINT_PTR)GetStockObject(NULL_BRUSH);
    }

    case WM_CTLCOLORBTN:
    {
        if (IsAppThemed())
        {
            // without this workaround there's a gray frame around the OK button
            if (WindowsVistaAndLater)
                return (BOOL)(UINT_PTR)HGradientBkBrush;
            else
                return (BOOL)(UINT_PTR)GetStockObject(NULL_BRUSH); // under XP this still worked fine
        }
        break;
    }

    case WM_ERASEBKGND:
    {
        HDC hDC = (HDC)wParam;
        if (BackgroundBitmap) AboutAndEvalDlgPaintBkgnd(HWindow, hDC, BackgroundBitmap);
        return TRUE;
    }

    case WM_NCHITTEST:
    {
        POINT point = {static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
        ScreenToClient(HWindow, &point);
        HWND child = ChildWindowFromPointEx(HWindow, point, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
        if (child != HWindow && child != nullptr && (GetWindowLongPtrW(child, GWL_STYLE) & WS_TABSTOP))
        { SetWindowLongPtr(HWindow, DWLP_MSGRESULT, HTCLIENT); return HTCLIENT; }
        SetWindowLongPtr(HWindow, DWLP_MSGRESULT, HTCAPTION);
        return HTCAPTION;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}
