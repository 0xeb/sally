// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "viewer.h"

#include "configuration_dialog.h"
#include "engine/wic_engine.h"
#include "rename_utils.h"
#include "dialogs.h"
#include "plugin_services.h"
#include "exif_text.h"
#include "pictview.rh2"
#include "lang/lang.rh"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <cstdarg>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <commctrl.h>
#include <cderr.h>
#include <commdlg.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <objidl.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <wrl/client.h>

#include "spl_com.h"
#include "spl_base.h"
#include "spl_gen.h"
#include "spl_gui.h"
#include "plugindarkmode.h"

using Microsoft::WRL::ComPtr;

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace pictview
{

namespace
{

constexpr wchar_t ViewerClassName[] = L"SallyPictViewViewerWindow";
constexpr uint64_t MaxInputFileBytes = 1024ull * 1024ull * 1024ull;
constexpr UINT WM_PICTVIEW_LOAD_IMAGE = WM_APP + 0x220;
constexpr UINT WM_PICTVIEW_LOAD_COMPLETE = WM_APP + 0x221;
constexpr UINT WM_PICTVIEW_LOAD_PROGRESS = WM_APP + 0x224; // wParam percent, lParam load sequence
constexpr UINT WM_PICTVIEW_REFRESH_MAIN_MENU = WM_APP + 0x222;
constexpr UINT WM_PICTVIEW_HOST_EVENT = WM_APP + 0x223;
constexpr UINT_PTR AnimationTimerId = 1;
constexpr UINT DefaultAnimationDelayMs = 100;
constexpr UINT MinimumAnimationDelayMs = 20;
constexpr UINT MaximumAnimationDelayMs = 60000;
constexpr wchar_t HistogramWindowClassName[] = L"SallyPictViewHistogramWindow";
constexpr int CopyToLineCount = static_cast<int>(ViewerCopyToHistoryEntryCount);
constexpr UINT_PTR CaptureTimerId = 2;
constexpr UINT_PTR CursorTimerId = 3;   // hides the cursor in full screen
constexpr UINT_PTR EdgeScrollTimerId = 4; // scrolls while a selection is dragged past the edge
constexpr UINT FullScreenCursorDelayMs = 3000;
constexpr UINT EdgeScrollIntervalMs = 50;
constexpr int CaptureHotKeyId = 0x7057;
constexpr int MinimumManualZoomPercent = 1;
constexpr int MaximumManualZoomPercent = 1600; // as the old PictView (and Photoshop)
const wchar_t OpenImagePatterns[] = L"*.bmp;*.dib;*.gif;*.ico;*.jpg;*.jpeg;*.jpe;*.jfif;*.png;*.tif;*.tiff;*.wdp;*.jxr;*.dds";

// Runs the Open or Save common dialog with a name buffer that grows on demand. comdlg32 has no
// path ceiling of its own: when the name does not fit it fails with FNERR_BUFFERTOOSMALL (and the
// Open dialog stores the length it needs in the buffer's first WORD), so the dialog is shown
// again with a larger buffer. 'fileName' is the initial name in, the chosen name out.
bool RunFileNameDialog(OPENFILENAMEW& ofn, bool save, std::wstring& fileName)
{
    constexpr size_t initialCapacity = 1024;
    size_t capacity = (std::max)(fileName.size() + 1, initialCapacity);
    for (;;)
    {
        std::vector<wchar_t> buffer(capacity, L'\0');
        std::copy(fileName.begin(), fileName.end(), buffer.begin());
        ofn.lpstrFile = buffer.data();
        ofn.nMaxFile = static_cast<DWORD>(capacity);
        const BOOL chosen = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
        ofn.lpstrFile = nullptr;
        ofn.nMaxFile = 0;
        if (chosen)
        {
            fileName.assign(buffer.data());
            return true;
        }
        if (CommDlgExtendedError() != FNERR_BUFFERTOOSMALL)
            return false;

        const size_t required = *reinterpret_cast<const WORD*>(buffer.data());
        const size_t next = (std::max)(capacity * 2, required + 1);
        if (next > (std::numeric_limits<DWORD>::max)())
            return false;
        capacity = next;
    }
}
constexpr wchar_t WallpaperRegistryKey[] = L"Control Panel\\Desktop";
constexpr wchar_t WallpaperValueName[] = L"Wallpaper";
constexpr wchar_t WallpaperStyleValueName[] = L"WallpaperStyle";
constexpr wchar_t TileWallpaperValueName[] = L"TileWallpaper";
constexpr wchar_t PrevWallpaperValueName[] = L"PictViewPrevWallpaper";
constexpr wchar_t PrevWallpaperStyleValueName[] = L"PictViewPrevWallpaperStyle";
constexpr wchar_t PrevTileWallpaperValueName[] = L"PictViewPrevTileWallpaper";
constexpr wchar_t WallpaperFileName[] = L"PictView_Wallpaper.bmp";
constexpr int StatusPartMessage = 0;
constexpr int StatusPartCursor = 1;
constexpr int StatusPartSize = 2;
constexpr int StatusPartSample = 3;
constexpr int StatusPartCount = 4;
constexpr int StatusIconMargin = 32;

// Command IDs are PictView's own (pictview.rh2): the menu templates, the toolbar and
// WM_COMMAND all use them, as the old viewer did.
enum ViewerCommand
{
    CmdOpen = CMD_OPEN,
    CmdSaveAs = CMD_SAVEAS,
    CmdReload = CMD_RELOAD,
    CmdRenameFile = CMD_IMG_RENAME,
    CmdCopyFileTo = CMD_IMG_COPYTO,
    CmdDeleteFile = CMD_IMG_DELETE,
    CmdProperties = CMD_IMG_PROP,
    CmdHistogram = CMD_IMG_HISTOGRAM,
    CmdTogglePipette = CMD_TOOLS_PIPETTE,
    CmdPrint = CMD_PRINT,
    CmdScreenCapture = CMD_CAPTURE,
    CmdWallpaperCenter = CMD_WALLPAPER_CENTER,
    CmdWallpaperTile = CMD_WALLPAPER_TILE,
    CmdWallpaperStretch = CMD_WALLPAPER_STRETCH,
    CmdWallpaperRestore = CMD_WALLPAPER_RESTORE,
    CmdWallpaperNone = CMD_WALLPAPER_NONE,
    CmdPrevSourceFile = CMD_FILE_PREV,
    CmdNextSourceFile = CMD_FILE_NEXT,
    CmdPrevSelectedSourceFile = CMD_FILE_PREVSELFILE,
    CmdNextSelectedSourceFile = CMD_FILE_NEXTSELFILE,
    CmdFirstSourceFile = CMD_FILE_FIRST,
    CmdLastSourceFile = CMD_FILE_LAST,
    CmdToggleSourceSelection = CMD_FILE_TOGGLESELECT,
    CmdFocusSourceFile = CMD_IMG_FOCUS,
    CmdFirstFrame = CMD_FIRSTPAGE,
    CmdPrevFrame = CMD_PREVPAGE,
    CmdGoToFrame = CMD_PAGE,
    CmdNextFrame = CMD_NEXTPAGE,
    CmdLastFrame = CMD_LASTPAGE,
    CmdToggleAnimation = CMD_ANIMATION,
    CmdCopy = CMD_COPY,
    CmdPaste = CMD_PASTE,
    CmdSelectAll = CMD_SELECTALL,
    CmdDeselect = CMD_DESELECT,
    CmdZoomTo = CMD_ZOOM_TO,
    CmdZoomOut = CMD_ZOOM_OUT,
    CmdZoomIn = CMD_ZOOM_IN,
    CmdFitWhole = CMD_ZOOMWHOLE,
    CmdFitWidth = CMD_ZOOMWIDTH,
    CmdActualSize = CMD_ZOOMACTUAL,
    CmdFullScreen = CMD_FULLSCREEN,
    CmdToggleToolbar = CMD_TOOLBAR,
    CmdToggleStatusbar = CMD_STATUSBAR,
    CmdRotateLeft = CMD_ROTATE_LEFT,
    CmdRotateRight = CMD_ROTATE_RIGHT,
    CmdRotate180 = CMD_ROTATE180,
    CmdFlipHorizontal = CMD_MIRROR_HOR,
    CmdFlipVertical = CMD_MIRROR_VERT,
    CmdCrop = CMD_CROP,
    CmdHelpContents = CMD_HELP_CONTENTS,
    CmdHelpIndex = CMD_HELP_INDEX,
    CmdHelpSearch = CMD_HELP_SEARCH,
    CmdAbout = CMD_ABOUT,
    CmdClose = CMD_EXIT,
    CmdToolHand = CMD_TOOLS_HAND,
    CmdToolZoom = CMD_TOOLS_ZOOM,
    CmdToolSelect = CMD_TOOLS_SELECT,
    CmdMetadataDetails = CMD_IMG_EXIF,
    CmdZoomMax = CMD_ZOOM_INMAX,
    CmdConfiguration = CMD_CFG,
    CmdResizeWindowToFit = CMD_ZOOMWINDOW,
    CmdRecentFileFirst = CMD_RECENTFILES_FIRST,
    CmdRecentFileLast = CmdRecentFileFirst + static_cast<int>(ViewerRecentHistoryEntryCount) - 1,
    CmdRecentDirectoryFirst = CMD_RECENRDIRS_FIRST,
    CmdRecentDirectoryLast = CmdRecentDirectoryFirst + static_cast<int>(ViewerRecentHistoryEntryCount) - 1,
};
static_assert(CmdRecentFileLast <= CMD_RECENTFILES_LAST && CmdRecentDirectoryLast <= CMD_RECENTDIRS_LAST,
              "the recent-file menus have room for every history entry");

enum class ZoomMode
{
    FitWhole,
    FitWidth,
    ActualSize,
    Manual,
};

enum class ViewerTool
{
    Hand,
    Zoom,
    Select,
    Pipette,
};

enum class ViewerStartupAction
{
    LoadPath,
    PasteClipboard,
    ScreenCapture,
};

ZoomMode ZoomModeFromPreference(ViewerDefaultZoomMode mode)
{
    switch (mode)
    {
    case ViewerDefaultZoomMode::FitWidth:
        return ZoomMode::FitWidth;
    case ViewerDefaultZoomMode::ActualSize:
        return ZoomMode::ActualSize;
    case ViewerDefaultZoomMode::FullScreen:
    case ViewerDefaultZoomMode::FitWhole:
    default:
        return ZoomMode::FitWhole;
    }
}

// Enablers: one DWORD per state, read by the Sally menu and toolbar. The templates hold the
// index; LoadFromTemplate and InsertItem2 resolve it against each window's own array.
enum ViewerEnabler
{
    vweAlwaysEnabled, // index 0 means "no enabler"
    vweFileOpened,
    vweFileOpened2, // opened from a file (not clipboard or capture)
    vwePaste,
    vwePrevPage,
    vweNextPage,
    vweMorePages,
    vweImgInfoAvailable,
    vweImgExifAvailable,
    vweNotLoading,
    vweSelSrcFile,
    vweNextFile,
    vwePrevFile,
    vweNextSelFile,
    vwePrevSelFile,
    vweFirstFile,
    vweSelection,
    vweAnimation,
    vweFocusFile,
    vweCount
};

#define PV_ENABLER(index) reinterpret_cast<DWORD*>(static_cast<uintptr_t>(index))

// Toolbar bitmap indexes (res/tb16.bmp, res/tb256.bmp).
constexpr int IDX_TB_ZOOMNUMBER = -3;
constexpr int IDX_TB_TERMINATOR = -2;
constexpr int IDX_TB_SEPARATOR = -1;
constexpr int IDX_TB_PROPERTIES = 0;
constexpr int IDX_TB_COPY = 1;
constexpr int IDX_TB_PASTE = 2;
constexpr int IDX_TB_FULLSCREEN = 3;
constexpr int IDX_TB_OPEN = 4;
constexpr int IDX_TB_180 = 5;
constexpr int IDX_TB_RIGHT = 6;
constexpr int IDX_TB_LEFT = 7;
constexpr int IDX_TB_FLIPV = 8;
constexpr int IDX_TB_FLIPH = 9;
constexpr int IDX_TB_ZOOMIN = 10;
constexpr int IDX_TB_ZOOMOUT = 11;
constexpr int IDX_TB_FIRST = 12;
constexpr int IDX_TB_LAST = 13;
constexpr int IDX_TB_SAVE = 14;
constexpr int IDX_TB_PREVPAGE = 15;
constexpr int IDX_TB_NEXTPAGE = 16;
constexpr int IDX_TB_HELP = 17;
constexpr int IDX_TB_HAND = 18;
constexpr int IDX_TB_PICK = 19;
constexpr int IDX_TB_SELECT = 20;
constexpr int IDX_TB_ZOOM = 21;
constexpr int IDX_TB_PRINT = 22;
constexpr int IDX_TB_ZOOMACTUAL = 23;
constexpr int IDX_TB_ZOOMWHOLE = 24;
constexpr int IDX_TB_ZOOMWIDTH = 25;
constexpr int IDX_TB_PREV = 26;
constexpr int IDX_TB_NEXT = 27;
constexpr int IDX_TB_SELSRCFILE = 34;
constexpr int IDX_TB_CROP = 35;
constexpr int IDX_TB_PREVSELFILE = 36;
constexpr int IDX_TB_NEXTSELFILE = 37;
constexpr int IDX_TB_COUNT = 38;

constexpr BYTE MenuSkill = MNTS_B | MNTS_I | MNTS_A;

MENU_TEMPLATE_ITEM MainMenuTemplate[] = {
    {MNTT_PB, -1, MenuSkill, 0, -1, 0, nullptr},

    // File
    {MNTT_PB, IDS_MENU_FILE, MenuSkill, CML_FILE, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_FILE_OPEN, MenuSkill, CmdOpen, IDX_TB_OPEN, 0, PV_ENABLER(vweNotLoading)},
    {MNTT_IT, IDS_MENU_FILE_REFRESH, MenuSkill, CmdReload, -1, 0, PV_ENABLER(vweFileOpened2)},
    {MNTT_IT, IDS_MENU_FILE_SAVEAS, MenuSkill, CmdSaveAs, IDX_TB_SAVE, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_FILE_CAPTURE, MenuSkill, CmdScreenCapture, -1, 0, PV_ENABLER(vweNotLoading)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_PB, IDS_MENU_FILE_WALLPAPER, MenuSkill, CML_WALLPAPER, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_WALLPARER_CENTER, MenuSkill, CmdWallpaperCenter, -1, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_IT, IDS_MENU_WALLPARER_TILE, MenuSkill, CmdWallpaperTile, -1, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_IT, IDS_MENU_WALLPARER_STRETCH, MenuSkill, CmdWallpaperStretch, -1, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_WALLPARER_RESTORE, MenuSkill, CmdWallpaperRestore, -1, 0, PV_ENABLER(vweNotLoading)},
    {MNTT_IT, IDS_MENU_WALLPARER_NONE, MenuSkill, CmdWallpaperNone, -1, 0, PV_ENABLER(vweNotLoading)},
    {MNTT_PE},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_FILE_PROP, MenuSkill, CmdProperties, IDX_TB_PROPERTIES, 0, PV_ENABLER(vweImgInfoAvailable)},
    {MNTT_IT, IDS_MENU_FILE_EXIF, MenuSkill, CmdMetadataDetails, -1, 0, PV_ENABLER(vweImgExifAvailable)},
    {MNTT_IT, IDS_MENU_FILE_HISTOGRAM, MenuSkill, CmdHistogram, -1, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_FILE_FOCUS, MenuSkill, CmdFocusSourceFile, -1, 0, PV_ENABLER(vweFocusFile)},
    {MNTT_IT, IDS_MENU_FILE_SELCRCFILE, MenuSkill, CmdToggleSourceSelection, IDX_TB_SELSRCFILE, 0, PV_ENABLER(vweSelSrcFile)},
    {MNTT_IT, IDS_MENU_FILE_RENAME, MenuSkill, CmdRenameFile, -1, 0, PV_ENABLER(vweFileOpened2)},
    {MNTT_IT, IDS_MENU_FILE_COPYTO, MenuSkill, CmdCopyFileTo, -1, 0, PV_ENABLER(vweFileOpened2)},
    {MNTT_IT, IDS_MENU_FILE_DELETE, MenuSkill, CmdDeleteFile, -1, 0, PV_ENABLER(vweFileOpened2)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_FILE_PRINT, MenuSkill, CmdPrint, IDX_TB_PRINT, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_PB, IDS_MENU_FILE_OTHER, MenuSkill, CML_OTHERFILES, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_FILE_PREV, MenuSkill, CmdPrevSourceFile, IDX_TB_PREV, 0, PV_ENABLER(vwePrevFile)},
    {MNTT_IT, IDS_MENU_FILE_NEXT, MenuSkill, CmdNextSourceFile, IDX_TB_NEXT, 0, PV_ENABLER(vweNextFile)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_FILE_PREVSEL, MenuSkill, CmdPrevSelectedSourceFile, IDX_TB_PREVSELFILE, 0, PV_ENABLER(vwePrevSelFile)},
    {MNTT_IT, IDS_MENU_FILE_NEXTSEL, MenuSkill, CmdNextSelectedSourceFile, IDX_TB_NEXTSELFILE, 0, PV_ENABLER(vweNextSelFile)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_FILE_FIRST, MenuSkill, CmdFirstSourceFile, IDX_TB_FIRST, 0, PV_ENABLER(vweFirstFile)},
    {MNTT_IT, IDS_MENU_FILE_LAST, MenuSkill, CmdLastSourceFile, IDX_TB_LAST, 0, PV_ENABLER(vweFirstFile)},
    {MNTT_PE},
    {MNTT_PB, IDS_MENU_FILE_FILES, MenuSkill, CML_RECENTFILES, -1, 0, nullptr},
    {MNTT_PE},
    {MNTT_PB, IDS_MENU_FILE_DIRS, MenuSkill, CML_RECENTDIRS, -1, 0, nullptr},
    {MNTT_PE},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_FILE_EXIT, MenuSkill, CmdClose, -1, 0, nullptr},
    {MNTT_PE},

    // Edit
    {MNTT_PB, IDS_MENU_EDIT, MenuSkill, CML_EDIT, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_EDIT_COPY, MenuSkill, CmdCopy, IDX_TB_COPY, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_IT, IDS_MENU_EDIT_PASTE, MenuSkill, CmdPaste, IDX_TB_PASTE, 0, PV_ENABLER(vwePaste)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_EDIT_SELECTALL, MenuSkill, CmdSelectAll, -1, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_IT, IDS_MENU_EDIT_DESELECT, MenuSkill, CmdDeselect, -1, 0, PV_ENABLER(vweSelection)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_EDIT_CROP, MenuSkill, CmdCrop, IDX_TB_CROP, 0, PV_ENABLER(vweSelection)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_EDIT_LEFT, MenuSkill, CmdRotateLeft, IDX_TB_LEFT, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_IT, IDS_MENU_EDIT_RIGHT, MenuSkill, CmdRotateRight, IDX_TB_RIGHT, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_IT, IDS_MENU_EDIT_180, MenuSkill, CmdRotate180, IDX_TB_180, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_EDIT_FLIPH, MenuSkill, CmdFlipHorizontal, IDX_TB_FLIPH, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_IT, IDS_MENU_EDIT_FLIPV, MenuSkill, CmdFlipVertical, IDX_TB_FLIPV, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_PE},

    // View
    {MNTT_PB, IDS_MENU_VIEW, MenuSkill, CML_VIEW, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_VIEW_ZOOMIN, MenuSkill, CmdZoomIn, IDX_TB_ZOOMIN, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_IT, IDS_MENU_VIEW_ZOOMOUT, MenuSkill, CmdZoomOut, IDX_TB_ZOOMOUT, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_IT, IDS_MENU_VIEW_ZOOM, MenuSkill, CmdZoomTo, -1, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_VIEW_WHOLE, MenuSkill, CmdFitWhole, IDX_TB_ZOOMWHOLE, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_IT, IDS_MENU_VIEW_WIDTH, MenuSkill, CmdFitWidth, IDX_TB_ZOOMWIDTH, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_IT, IDS_MENU_VIEW_ACTUAL, MenuSkill, CmdActualSize, IDX_TB_ZOOMACTUAL, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_IT, IDS_MENU_VIEW_WINDOW, MenuSkill, CmdResizeWindowToFit, -1, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_VIEW_TOOLBAR, MenuSkill, CmdToggleToolbar, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_VIEW_STATUSBAR, MenuSkill, CmdToggleStatusbar, -1, 0, nullptr},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_VIEW_FULLSCREEN, MenuSkill, CmdFullScreen, IDX_TB_FULLSCREEN, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_PB, IDS_MENU_MULTIPAGE, MenuSkill, CML_MULTIPAGE, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_PREVPAGE, MenuSkill, CmdPrevFrame, IDX_TB_PREVPAGE, 0, PV_ENABLER(vwePrevPage)},
    {MNTT_IT, IDS_MENU_NEXTPAGE, MenuSkill, CmdNextFrame, IDX_TB_NEXTPAGE, 0, PV_ENABLER(vweNextPage)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_FIRSTPAGE, MenuSkill, CmdFirstFrame, -1, 0, PV_ENABLER(vwePrevPage)},
    {MNTT_IT, IDS_MENU_LASTPAGE, MenuSkill, CmdLastFrame, -1, 0, PV_ENABLER(vweNextPage)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_PAGE, MenuSkill, CmdGoToFrame, -1, 0, PV_ENABLER(vweMorePages)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_ANIMATION, MenuSkill, CmdToggleAnimation, -1, 0, PV_ENABLER(vweAnimation)},
    {MNTT_PE},
    {MNTT_PE},

    // Tools
    {MNTT_PB, IDS_MENU_TOOLS, MenuSkill, CML_TOOLS, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_TOOLS_HAND, MenuSkill, CmdToolHand, IDX_TB_HAND, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_IT, IDS_MENU_TOOLS_ZOOM, MenuSkill, CmdToolZoom, IDX_TB_ZOOM, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_IT, IDS_MENU_TOOLS_SELECT, MenuSkill, CmdToolSelect, IDX_TB_SELECT, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_IT, IDS_MENU_TOOLS_PICK, MenuSkill, CmdTogglePipette, IDX_TB_PICK, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_TOOLS_OPTIONS, MenuSkill, CmdConfiguration, -1, 0, nullptr},
    {MNTT_PE},

    // Help
    {MNTT_PB, IDS_MENU_HELP, MenuSkill, CML_HELP, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_HELP_CONTENTS, MenuSkill, CmdHelpContents, IDX_TB_HELP, 0, nullptr},
    {MNTT_IT, IDS_MENU_HELP_INDEX, MenuSkill, CmdHelpIndex, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_HELP_SEARCH, MenuSkill, CmdHelpSearch, -1, 0, nullptr},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_HELP_ABOUT, MenuSkill, CmdAbout, -1, 0, nullptr},
    {MNTT_PE},

    {MNTT_PE}, // terminator
};

MENU_TEMPLATE_ITEM ContextMenuTemplate[] = {
    {MNTT_PB, -1, MenuSkill, CML_CONTEXT, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_FILE_PREV, MenuSkill, CmdPrevSourceFile, IDX_TB_PREV, 0, PV_ENABLER(vwePrevFile)},
    {MNTT_IT, IDS_MENU_FILE_NEXT, MenuSkill, CmdNextSourceFile, IDX_TB_NEXT, 0, PV_ENABLER(vweNextFile)},
    {MNTT_IT, IDS_MENU_EDIT_COPY, MenuSkill, CmdCopy, IDX_TB_COPY, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_IT, IDS_MENU_EDIT_PASTE, MenuSkill, CmdPaste, IDX_TB_PASTE, 0, PV_ENABLER(vwePaste)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_FILE_PROP, MenuSkill, CmdProperties, IDX_TB_PROPERTIES, 0, PV_ENABLER(vweImgInfoAvailable)},
    {MNTT_IT, IDS_MENU_FILE_EXIF, MenuSkill, CmdMetadataDetails, -1, 0, PV_ENABLER(vweImgExifAvailable)},
    {MNTT_IT, IDS_MENU_FILE_HISTOGRAM, MenuSkill, CmdHistogram, -1, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_FILE_SELCRCFILE, MenuSkill, CmdToggleSourceSelection, IDX_TB_SELSRCFILE, 0, PV_ENABLER(vweSelSrcFile)},
    {MNTT_IT, IDS_MENU_FILE_PREVSEL, MenuSkill, CmdPrevSelectedSourceFile, IDX_TB_PREVSELFILE, 0, PV_ENABLER(vwePrevSelFile)},
    {MNTT_IT, IDS_MENU_FILE_NEXTSEL, MenuSkill, CmdNextSelectedSourceFile, IDX_TB_NEXTSELFILE, 0, PV_ENABLER(vweNextSelFile)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_FILE_FOCUS, MenuSkill, CmdFocusSourceFile, -1, 0, PV_ENABLER(vweFocusFile)},
    {MNTT_IT, IDS_MENU_FILE_RENAME, MenuSkill, CmdRenameFile, -1, 0, PV_ENABLER(vweFileOpened2)},
    {MNTT_IT, IDS_MENU_FILE_DELETE, MenuSkill, CmdDeleteFile, -1, 0, PV_ENABLER(vweFileOpened2)},
    {MNTT_SP, -1, MenuSkill, 0, -1, 0, nullptr},
    {MNTT_IT, IDS_MENU_VIEW_FULLSCREEN, MenuSkill, CmdFullScreen, IDX_TB_FULLSCREEN, 0, PV_ENABLER(vweFileOpened)},
    {MNTT_PE}, // terminator
};

struct PipetteSample
{
    uint32_t X = 0;
    uint32_t Y = 0;
    uint8_t Red = 0;
    uint8_t Green = 0;
    uint8_t Blue = 0;
    int Index = -1; // the palette index of an indexed image, -1 when unknown
};

struct ToolbarButton
{
    int ImageIndex;
    int ToolTipResID;
    int Command;
    int Enabler;
};

const ToolbarButton ToolbarButtons[] = {
    {IDX_TB_OPEN, IDS_TT_OPEN, CmdOpen, vweNotLoading},
    {IDX_TB_SAVE, IDS_TT_SAVE, CmdSaveAs, vweFileOpened},
    {IDX_TB_PROPERTIES, IDS_TT_PROPERTIES, CmdProperties, vweImgInfoAvailable},
    {IDX_TB_PRINT, IDS_TT_PRINT, CmdPrint, vweFileOpened},
    {IDX_TB_SEPARATOR},
    {IDX_TB_PREV, IDS_TT_PREV, CmdPrevSourceFile, vwePrevFile},
    {IDX_TB_NEXT, IDS_TT_NEXT, CmdNextSourceFile, vweNextFile},
    {IDX_TB_SELSRCFILE, IDS_TT_SELSRCFILE, CmdToggleSourceSelection, vweSelSrcFile},
    {IDX_TB_PREVSELFILE, IDS_TT_PREVSELFILE, CmdPrevSelectedSourceFile, vwePrevSelFile},
    {IDX_TB_NEXTSELFILE, IDS_TT_NEXTSELFILE, CmdNextSelectedSourceFile, vweNextSelFile},
    {IDX_TB_SEPARATOR},
    {IDX_TB_COPY, IDS_TT_COPY, CmdCopy, vweFileOpened},
    {IDX_TB_PASTE, IDS_TT_PASTE, CmdPaste, vwePaste},
    {IDX_TB_SEPARATOR},
    {IDX_TB_HAND, IDS_TT_TOOL_HAND, CmdToolHand, vweFileOpened},
    {IDX_TB_ZOOM, IDS_TT_TOOL_ZOOM, CmdToolZoom, vweFileOpened},
    {IDX_TB_SELECT, IDS_TT_TOOL_SELECT, CmdToolSelect, vweFileOpened},
    {IDX_TB_PICK, IDS_TT_TOOL_PIPETTE, CmdTogglePipette, vweFileOpened},
    {IDX_TB_SEPARATOR},
    {IDX_TB_LEFT, IDS_TT_LEFT, CmdRotateLeft, vweFileOpened},
    {IDX_TB_RIGHT, IDS_TT_RIGHT, CmdRotateRight, vweFileOpened},
    {IDX_TB_CROP, IDS_TT_CROP, CmdCrop, vweSelection},
    {IDX_TB_SEPARATOR},
    {IDX_TB_ZOOMOUT, IDS_TT_ZOOM_OUT, CmdZoomOut, vweFileOpened},
    {IDX_TB_ZOOMNUMBER, IDS_TT_ZOOM_TO, CmdZoomTo, vweFileOpened},
    {IDX_TB_ZOOMIN, IDS_TT_ZOOM_IN, CmdZoomIn, vweFileOpened},
    {IDX_TB_SEPARATOR},
    {IDX_TB_ZOOMWHOLE, IDS_TT_ZOOM_WHOLE, CmdFitWhole, vweFileOpened},
    {IDX_TB_ZOOMWIDTH, IDS_TT_ZOOM_WIDTH, CmdFitWidth, vweFileOpened},
    {IDX_TB_ZOOMACTUAL, IDS_TT_ZOOM_ACTUAL, CmdActualSize, vweFileOpened},
    {IDX_TB_FULLSCREEN, IDS_TT_FULL_SCREEN, CmdFullScreen, vweFileOpened},
    {IDX_TB_TERMINATOR},
};

// The zoom button's drop-down presets, in hundredths of a percent.
constexpr int ZoomPresets[] = {600, 1200, 2500, 5000, 7500, 10000, 12500, 15000, 20000, 40000, 60000, 80000, 100000, 160000};

constexpr UINT_PTR RebarBandMenu = 1;
constexpr UINT_PTR RebarBandToolbar = 2;

bool IsViewerToolCommand(int command)
{
    return command == CmdToolHand ||
           command == CmdToolZoom ||
           command == CmdToolSelect ||
           command == CmdTogglePipette;
}

int SanitizeSelectRatio(int value)
{
    if (value <= 0 || value > 999)
        return 1;
    return value;
}

HINSTANCE ModuleInstance = nullptr;
ViewerMessageBoxFunction HostMessageBox = nullptr;
CSalamanderGeneralAbstract* HostGeneral = nullptr;
CSalamanderGUIAbstract* HostGui = nullptr;
ATOM ViewerClassAtom = 0;
ATOM HistogramClassAtom = 0;
std::mutex CopyToHistoryMutex;
std::array<std::wstring, CopyToLineCount> CopyToHistory;
int CopyToLastIndex = 0;
std::mutex RecentHistoryMutex;
std::array<std::wstring, ViewerRecentHistoryEntryCount> RecentFiles;
std::array<std::wstring, ViewerRecentHistoryEntryCount> RecentDirectories;
std::mutex ViewerPreferencesMutex;
ViewerPreferences GlobalViewerPreferences;
std::mutex HistogramPreferencesMutex;
ViewerHistogramPreferences GlobalHistogramPreferences;
std::mutex MetadataDetailsPreferencesMutex;
ViewerMetadataDetailsPreferences GlobalMetadataDetailsPreferences;

HICON LoadViewerBigIcon()
{
    HICON icon = LoadIconW(ModuleInstance, MAKEINTRESOURCEW(IDI_WINDOW_ICON));
    if (icon != nullptr)
        return icon;
    return LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
}

HICON LoadViewerSmallIcon()
{
    HICON icon = static_cast<HICON>(LoadImageW(ModuleInstance,
                                              MAKEINTRESOURCEW(IDI_WINDOW_ICON),
                                              IMAGE_ICON,
                                              16,
                                              16,
                                              LR_DEFAULTCOLOR | LR_SHARED));
    if (icon != nullptr)
        return icon;
    return static_cast<HICON>(LoadImageW(nullptr,
                                        MAKEINTRESOURCEW(32512),
                                        IMAGE_ICON,
                                        16,
                                        16,
                                        LR_DEFAULTCOLOR | LR_SHARED));
}

HCURSOR LoadStockCursor(int resourceId)
{
    return LoadCursorW(nullptr, MAKEINTRESOURCEW(resourceId));
}

HCURSOR LoadViewerCursor(int resourceId, int fallbackResourceId)
{
    HCURSOR cursor = static_cast<HCURSOR>(LoadImageW(ModuleInstance,
                                                     MAKEINTRESOURCEW(resourceId),
                                                     IMAGE_CURSOR,
                                                     0,
                                                     0,
                                                     LR_DEFAULTCOLOR | LR_SHARED));
    if (cursor != nullptr)
        return cursor;
    return LoadStockCursor(fallbackResourceId);
}

HICON LoadViewerStatusIcon(int resourceId)
{
    return static_cast<HICON>(LoadImageW(ModuleInstance,
                                         MAKEINTRESOURCEW(resourceId),
                                         IMAGE_ICON,
                                         16,
                                         16,
                                         LR_DEFAULTCOLOR | LR_SHARED));
}

class ViewerWindowList
{
public:
    ViewerWindowList()
    {
        InitializeCriticalSection(&m_cs);
    }

    ~ViewerWindowList()
    {
        DeleteCriticalSection(&m_cs);
    }

    void Add(HWND hwnd)
    {
        EnterCriticalSection(&m_cs);
        m_windows.push_back(hwnd);
        LeaveCriticalSection(&m_cs);
    }

    void Remove(HWND hwnd)
    {
        EnterCriticalSection(&m_cs);
        m_windows.erase(std::remove(m_windows.begin(), m_windows.end(), hwnd), m_windows.end());
        LeaveCriticalSection(&m_cs);
    }

    std::vector<HWND> Snapshot()
    {
        EnterCriticalSection(&m_cs);
        std::vector<HWND> copy = m_windows;
        LeaveCriticalSection(&m_cs);
        return copy;
    }

    bool Empty()
    {
        EnterCriticalSection(&m_cs);
        const bool empty = m_windows.empty();
        LeaveCriticalSection(&m_cs);
        return empty;
    }

private:
    CRITICAL_SECTION m_cs = {};
    std::vector<HWND> m_windows;
};

ViewerWindowList ViewerWindows;

std::wstring LastWin32ErrorText(const wchar_t* action, DWORD error)
{
    std::wostringstream text;
    text << action << L" (Win32 error " << error << L")";
    return text.str();
}

std::wstring HResultErrorText(const wchar_t* action, HRESULT hr)
{
    std::wostringstream text;
    text << action << L" (HRESULT 0x" << std::hex << static_cast<unsigned long>(hr) << L")";
    return text.str();
}

HRESULT LastWin32Hr()
{
    DWORD error = GetLastError();
    return error == ERROR_SUCCESS ? E_FAIL : HRESULT_FROM_WIN32(error);
}

std::wstring DecorateLongPath(const std::wstring& path)
{
    if (path.rfind(L"\\\\?\\", 0) == 0)
        return path;
    if (path.rfind(L"\\\\", 0) == 0)
        return L"\\\\?\\UNC\\" + path.substr(2);
    if (path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'))
        return L"\\\\?\\" + path;
    return path;
}

HANDLE OpenInputFile(const std::wstring& path)
{
    HANDLE file = CreateFileW(path.c_str(),
                              GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                              nullptr);
    if (file != INVALID_HANDLE_VALUE)
        return file;

    const std::wstring decorated = DecorateLongPath(path);
    if (decorated == path)
        return INVALID_HANDLE_VALUE;

    return CreateFileW(decorated.c_str(),
                       GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       nullptr,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                       nullptr);
}

struct StreamLoadResult
{
    ComPtr<IStream> Stream;
    HRESULT Hr = S_OK;
    std::wstring Message;

    bool Succeeded() const { return Stream != nullptr && SUCCEEDED(Hr); }
};

struct MemoryLoadResult
{
    std::vector<uint8_t> Bytes;
    HRESULT Hr = S_OK;
    std::wstring Message;

    bool Succeeded() const { return SUCCEEDED(Hr); }
};

MemoryLoadResult ReadFileToMemoryBuffer(const std::wstring& path, const CancellationToken& cancellation = CancellationToken())
{
    MemoryLoadResult result;
    if (cancellation.IsCancellationRequested())
    {
        result.Hr = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        result.Message = ViewerText(IDS_PV_IMAGE_LOAD_WAS_CANCELED);
        return result;
    }

    HANDLE file = OpenInputFile(path);
    if (file == INVALID_HANDLE_VALUE)
    {
        result.Hr = LastWin32Hr();
        result.Message = LastWin32ErrorText(ViewerText(IDS_PV_UNABLE_TO_OPEN_IMAGE_FILE), GetLastError());
        return result;
    }

    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart < 0)
    {
        result.Hr = LastWin32Hr();
        result.Message = LastWin32ErrorText(ViewerText(IDS_PV_UNABLE_TO_READ_IMAGE_FILE), GetLastError());
        CloseHandle(file);
        return result;
    }

    if (static_cast<uint64_t>(fileSize.QuadPart) > MaxInputFileBytes)
    {
        result.Hr = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        result.Message = ViewerText(IDS_PV_IMAGE_FILE_EXCEEDS_THE_CURRENT);
        CloseHandle(file);
        return result;
    }

    try
    {
        result.Bytes.resize(static_cast<size_t>(fileSize.QuadPart));
    }
    catch (const std::bad_alloc&)
    {
        result.Hr = E_OUTOFMEMORY;
        result.Message = ViewerText(IDS_PV_UNABLE_TO_ALLOCATE_AN_IN);
        CloseHandle(file);
        return result;
    }

    uint8_t* out = result.Bytes.data();
    uint64_t remaining = static_cast<uint64_t>(fileSize.QuadPart);
    bool readOk = true;
    while (remaining > 0)
    {
        if (cancellation.IsCancellationRequested())
        {
            readOk = false;
            SetLastError(ERROR_CANCELLED);
            break;
        }

        const DWORD chunk = static_cast<DWORD>(std::min<uint64_t>(remaining, 16ull * 1024ull * 1024ull));
        DWORD bytesRead = 0;
        if (!ReadFile(file, out, chunk, &bytesRead, nullptr) || bytesRead == 0)
        {
            readOk = false;
            break;
        }

        out += bytesRead;
        remaining -= bytesRead;
    }

    const DWORD readError = readOk ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);

    if (!readOk)
    {
        result.Hr = readError == ERROR_SUCCESS ? E_FAIL : HRESULT_FROM_WIN32(readError);
        result.Message = readError == ERROR_CANCELLED ? ViewerText(IDS_PV_IMAGE_LOAD_WAS_CANCELED) : LastWin32ErrorText(ViewerText(IDS_PV_UNABLE_TO_READ_IMAGE_FILE_2), readError);
        result.Bytes.clear();
        return result;
    }

    result.Hr = S_OK;
    return result;
}

StreamLoadResult CreateMemoryStreamFromBytes(const std::vector<uint8_t>& bytes)
{
    StreamLoadResult result;
    const SIZE_T allocationSize = static_cast<SIZE_T>(bytes.empty() ? 1 : bytes.size());
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, allocationSize);
    if (memory == nullptr)
    {
        result.Hr = E_OUTOFMEMORY;
        result.Message = ViewerText(IDS_PV_UNABLE_TO_ALLOCATE_AN_IN);
        return result;
    }

    void* locked = GlobalLock(memory);
    if (locked == nullptr)
    {
        result.Hr = LastWin32Hr();
        result.Message = LastWin32ErrorText(ViewerText(IDS_PV_UNABLE_TO_LOCK_IMAGE_STREAM), GetLastError());
        GlobalFree(memory);
        return result;
    }

    if (!bytes.empty())
        memcpy(locked, bytes.data(), bytes.size());
    GlobalUnlock(memory);

    HRESULT hr = CreateStreamOnHGlobal(memory, TRUE, &result.Stream);
    if (FAILED(hr))
    {
        result.Hr = hr;
        result.Message = ViewerText(IDS_PV_UNABLE_TO_CREATE_AN_IMAGE);
        GlobalFree(memory);
        return result;
    }

    LARGE_INTEGER zero = {};
    result.Stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    result.Hr = S_OK;
    return result;
}

StreamLoadResult ReadFileToMemoryStream(const std::wstring& path, const CancellationToken& cancellation = CancellationToken())
{
    MemoryLoadResult memory = ReadFileToMemoryBuffer(path, cancellation);
    if (!memory.Succeeded())
    {
        StreamLoadResult result;
        result.Hr = memory.Hr;
        result.Message = std::move(memory.Message);
        return result;
    }

    return CreateMemoryStreamFromBytes(memory.Bytes);
}

std::wstring FileNameFromPath(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return path;
    return path.substr(slash + 1);
}

std::wstring DirectoryFromPath(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return std::wstring();
    if (slash == 2 && path.size() >= 3 && path[1] == L':')
        return path.substr(0, 3);
    return path.substr(0, slash);
}

template <std::size_t EntryCount>
bool RemoveRecentEntry(std::array<std::wstring, EntryCount>& entries, const std::wstring& value)
{
    for (std::size_t i = 0; i < EntryCount; ++i)
    {
        if (!entries[i].empty() && _wcsicmp(entries[i].c_str(), value.c_str()) == 0)
        {
            for (std::size_t j = i; j + 1 < EntryCount; ++j)
                entries[j] = std::move(entries[j + 1]);
            entries[EntryCount - 1].clear();
            return true;
        }
    }
    return false;
}

// The folder of 'path' changed: Sally refreshes the panels that show it.
void NotifyPathChanged(const std::wstring& path)
{
    const std::wstring directory = DirectoryFromPath(path);
    if (directory.empty())
        return;
    if (Services().PathChanged)
        Services().PathChanged(directory);
    else if (HostGeneral != nullptr)
        HostGeneral->PostChangeOnPathNotification(directory.c_str(), FALSE);
}

template <std::size_t EntryCount>
void AddRecentEntry(std::array<std::wstring, EntryCount>& entries, const std::wstring& value)
{
    if (value.empty())
        return;

    std::size_t from = EntryCount - 1;
    for (std::size_t i = 0; i < EntryCount; ++i)
    {
        if (!entries[i].empty() && _wcsicmp(entries[i].c_str(), value.c_str()) == 0)
        {
            if (i == 0)
                return;
            from = i;
            break;
        }
    }

    for (std::size_t i = from; i > 0; --i)
        entries[i] = std::move(entries[i - 1]);
    entries[0] = value;
}

std::wstring RecentMenuItemText(std::size_t index, const std::wstring& path)
{
    std::wostringstream text;
    text << L"&" << (index < 9 ? index + 1 : 0) << L" " << path;
    return text.str();
}

void AppendRecentHistoryMenu(HMENU menu,
                             const std::array<std::wstring, ViewerRecentHistoryEntryCount>& entries,
                             int firstCommand)
{
    if (entries[0].empty())
    {
        AppendMenuW(menu, MF_GRAYED | MF_STRING, firstCommand, L"(Empty)");
        return;
    }

    for (std::size_t i = 0; i < entries.size() && !entries[i].empty(); ++i)
    {
        const std::wstring text = RecentMenuItemText(i, entries[i]);
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(firstCommand + static_cast<int>(i)), text.c_str());
    }
}

std::wstring JoinPathComponent(const std::wstring& directory, const wchar_t* name)
{
    if (directory.empty())
        return name != nullptr ? std::wstring(name) : std::wstring();

    std::wstring path = directory;
    const wchar_t last = path.back();
    if (last != L'\\' && last != L'/')
        path += L'\\';
    if (name != nullptr)
        path += name;
    return path;
}

std::wstring StemFromFileName(const std::wstring& fileName)
{
    const size_t dot = fileName.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0)
        return fileName;
    return fileName.substr(0, dot);
}

bool PathHasExtension(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    return dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash);
}

std::wstring LowerExtension(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot <= slash))
        return std::wstring();

    std::wstring ext = path.substr(dot);
    for (wchar_t& ch : ext)
        ch = static_cast<wchar_t>(std::towlower(ch));
    return ext;
}

ImageSaveFormat SaveFormatFromFilterIndex(DWORD filterIndex)
{
    switch (filterIndex)
    {
    case 2:
        return ImageSaveFormat::Jpeg;
    case 3:
        return ImageSaveFormat::Tiff;
    case 4:
        return ImageSaveFormat::Bmp;
    case 5:
        return ImageSaveFormat::Gif;
    case 1:
    default:
        return ImageSaveFormat::Png;
    }
}

DWORD SaveFilterIndexForFormat(ImageSaveFormat format)
{
    switch (format)
    {
    case ImageSaveFormat::Jpeg:
        return 2;
    case ImageSaveFormat::Tiff:
        return 3;
    case ImageSaveFormat::Bmp:
        return 4;
    case ImageSaveFormat::Gif:
        return 5;
    case ImageSaveFormat::Png:
    default:
        return 1;
    }
}

DWORD ClampSaveFilterIndex(int filterIndex)
{
    if (filterIndex < 1 || filterIndex > 5)
        return 1;
    return static_cast<DWORD>(filterIndex);
}

ImageSaveFormat SaveFormatFromPathOrFilter(const std::wstring& path, DWORD filterIndex)
{
    const std::wstring ext = LowerExtension(path);
    if (ext == L".jpg" || ext == L".jpeg")
        return ImageSaveFormat::Jpeg;
    if (ext == L".bmp" || ext == L".dib")
        return ImageSaveFormat::Bmp;
    if (ext == L".tif" || ext == L".tiff")
        return ImageSaveFormat::Tiff;
    if (ext == L".gif")
        return ImageSaveFormat::Gif;
    if (ext == L".png")
        return ImageSaveFormat::Png;
    return SaveFormatFromFilterIndex(filterIndex);
}

std::wstring SaveInitialDirectoryForDialog(const std::wstring& currentPath, const ViewerPreferences& preferences)
{
    const std::wstring currentDirectory = DirectoryFromPath(currentPath);
    if (!currentDirectory.empty())
        return currentDirectory;

    if (preferences.RememberSavePath)
        return preferences.SaveInitialDirectory;

    return std::wstring();
}

bool WideEqualsIgnoreCase(const std::wstring& left, const std::wstring& right)
{
    return CompareStringOrdinal(left.c_str(),
                                static_cast<int>(left.size()),
                                right.c_str(),
                                static_cast<int>(right.size()),
                                TRUE) == CSTR_EQUAL;
}

std::wstring ReadEnvironmentString(const wchar_t* name)
{
    if (name == nullptr || name[0] == L'\0')
        return std::wstring();

    DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0)
        return std::wstring();

    std::vector<wchar_t> buffer(needed);
    DWORD copied = GetEnvironmentVariableW(name, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (copied == 0 || copied >= buffer.size())
        return std::wstring();
    return std::wstring(buffer.data(), copied);
}

bool EnsureDirectoryExists(const std::wstring& path)
{
    if (path.empty())
        return false;

    if (CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    return false;
}

std::wstring WallpaperOutputPath()
{
    const std::wstring localAppData = ReadEnvironmentString(L"LOCALAPPDATA");
    if (localAppData.empty())
        return std::wstring();

    const std::wstring sallyDirectory = JoinPathComponent(localAppData, L"Sally");
    if (!EnsureDirectoryExists(sallyDirectory))
        return std::wstring();

    const std::wstring pictviewDirectory = JoinPathComponent(sallyDirectory, L"PictView");
    if (!EnsureDirectoryExists(pictviewDirectory))
        return std::wstring();

    return JoinPathComponent(pictviewDirectory, WallpaperFileName);
}

struct WallpaperState
{
    std::wstring Wallpaper;
    std::wstring WallpaperStyle;
    std::wstring TileWallpaper;
};

std::wstring ReadRegistryStringValue(HKEY key, const wchar_t* name)
{
    if (key == nullptr || name == nullptr)
        return std::wstring();

    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || bytes == 0)
    {
        return std::wstring();
    }

    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 2, L'\0');
    if (RegQueryValueExW(key,
                         name,
                         nullptr,
                         &type,
                         reinterpret_cast<BYTE*>(buffer.data()),
                         &bytes) != ERROR_SUCCESS)
    {
        return std::wstring();
    }

    return std::wstring(buffer.data());
}

bool WriteRegistryStringValue(HKEY key, const wchar_t* name, const std::wstring& value)
{
    if (key == nullptr || name == nullptr)
        return false;

    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    return RegSetValueExW(key,
                          name,
                          0,
                          REG_SZ,
                          reinterpret_cast<const BYTE*>(value.c_str()),
                          bytes) == ERROR_SUCCESS;
}

WallpaperState ReadWallpaperState(HKEY key, bool previous)
{
    WallpaperState state;
    state.Wallpaper = ReadRegistryStringValue(key, previous ? PrevWallpaperValueName : WallpaperValueName);
    state.WallpaperStyle = ReadRegistryStringValue(key, previous ? PrevWallpaperStyleValueName : WallpaperStyleValueName);
    state.TileWallpaper = ReadRegistryStringValue(key, previous ? PrevTileWallpaperValueName : TileWallpaperValueName);
    return state;
}

bool WriteWallpaperState(HKEY key, bool previous, const WallpaperState& state)
{
    const bool wallpaper = WriteRegistryStringValue(key, previous ? PrevWallpaperValueName : WallpaperValueName, state.Wallpaper);
    const bool style = WriteRegistryStringValue(key, previous ? PrevWallpaperStyleValueName : WallpaperStyleValueName, state.WallpaperStyle);
    const bool tile = WriteRegistryStringValue(key, previous ? PrevTileWallpaperValueName : TileWallpaperValueName, state.TileWallpaper);
    return wallpaper && style && tile;
}

bool NotifyWallpaperChanged(const std::wstring& wallpaper)
{
    std::wstring mutableWallpaper = wallpaper;
    return SystemParametersInfoW(SPI_SETDESKWALLPAPER,
                                 0,
                                 mutableWallpaper.empty() ? const_cast<wchar_t*>(L"") : mutableWallpaper.data(),
                                 SPIF_UPDATEINIFILE | SPIF_SENDCHANGE) != FALSE;
}

const wchar_t* SaveExtension(ImageSaveFormat format)
{
    switch (format)
    {
    case ImageSaveFormat::Jpeg:
        return L".jpg";
    case ImageSaveFormat::Bmp:
        return L".bmp";
    case ImageSaveFormat::Tiff:
        return L".tif";
    case ImageSaveFormat::Gif:
        return L".gif";
    case ImageSaveFormat::Png:
    default:
        return L".png";
    }
}

bool SaveFormatPreservesAlpha(ImageSaveFormat format)
{
    return format == ImageSaveFormat::Png || format == ImageSaveFormat::Tiff;
}

ImageJpegSubsampling JpegSubsamplingFromPreference(ViewerJpegSubsampling subsampling)
{
    switch (subsampling)
    {
    case ViewerJpegSubsampling::OneToOneOne:
        return ImageJpegSubsampling::OneToOneOne;
    case ViewerJpegSubsampling::TwoToOneOne:
    default:
        return ImageJpegSubsampling::TwoToOneOne;
    }
}

ImageTiffCompression TiffCompressionFromPreference(ViewerTiffCompression compression)
{
    switch (compression)
    {
    case ViewerTiffCompression::None:
        return ImageTiffCompression::None;
    case ViewerTiffCompression::Lzw:
        return ImageTiffCompression::Lzw;
    case ViewerTiffCompression::Zip:
        return ImageTiffCompression::Zip;
    case ViewerTiffCompression::Default:
    default:
        return ImageTiffCompression::Default;
    }
}

UINT ImageErrorSummaryId(ImageErrorCode code)
{
    switch (code)
    {
    case ImageErrorCode::ComInitializationFailed:
    case ImageErrorCode::WicFactoryFailed:
        return IDS_PV_ERR_WIC_UNAVAILABLE;
    case ImageErrorCode::OpenFailed:
        return IDS_PV_ERR_OPEN;
    case ImageErrorCode::UnsupportedFormat:
        return IDS_UNSUPPORTED_IMAGE_TYPE;
    case ImageErrorCode::MetadataFailed:
    case ImageErrorCode::DecodeFailed:
        return IDS_PV_ERR_DECODE;
    case ImageErrorCode::FrameOutOfRange:
        return IDS_PV_ERR_FRAME;
    case ImageErrorCode::SizeLimitExceeded:
        return IDS_PV_ERR_TOO_LARGE;
    case ImageErrorCode::Canceled:
        return IDS_PV_ERR_CANCELED;
    case ImageErrorCode::OutOfMemory:
        return IDS_PV_ERR_MEMORY;
    case ImageErrorCode::SaveFailed:
        return IDS_PV_ERR_SAVE;
    default:
        return IDS_PV_IMAGE_OPERATION_FAILED;
    }
}

// A translated summary of the error, then the engine's own diagnostic and its HRESULT.
std::wstring ImageErrorText(const ImageError& error)
{
    std::wostringstream text;
    text << ViewerText(ImageErrorSummaryId(error.Code));
    if (error.Code == ImageErrorCode::Canceled)
        return text.str();

    if (!error.Message.empty())
        text << L"\n\n" << error.Message;
    if (FAILED(error.Hr))
        text << L"\nHRESULT: 0x" << std::hex << static_cast<unsigned long>(error.Hr);
    return text.str();
}

int ClampInt64ToInt(long long value)
{
    if (value < (std::numeric_limits<int>::min)())
        return (std::numeric_limits<int>::min)();
    if (value > (std::numeric_limits<int>::max)())
        return (std::numeric_limits<int>::max)();
    return static_cast<int>(value);
}

int RectWidth(const RECT& rect)
{
    return rect.right - rect.left;
}

int RectHeight(const RECT& rect)
{
    return rect.bottom - rect.top;
}

struct ClipboardImageResult
{
    ImageSurface Surface;
    std::wstring ErrorText;
    bool Success = false;
};

struct CaptureImageResult
{
    ImageSurface Surface;
    std::wstring ErrorText;
    bool Success = false;
};

bool ClipboardContainsImage()
{
    return IsClipboardFormatAvailable(CF_DIBV5) ||
           IsClipboardFormatAvailable(CF_DIB) ||
           IsClipboardFormatAvailable(CF_BITMAP);
}

void ForceOpaqueAlpha(ImageSurface& surface)
{
    for (uint32_t y = 0; y < surface.Height; ++y)
    {
        uint8_t* row = surface.Pixels.data() + static_cast<size_t>(y) * surface.Stride;
        for (uint32_t x = 0; x < surface.Width; ++x)
            row[static_cast<size_t>(x) * 4u + 3] = 255;
    }
}

bool AllocateBgraSurface(uint32_t width,
                         uint32_t height,
                         ImageSurface& surface,
                         std::wstring& errorText,
                         const wchar_t* imageKind = ViewerText(IDS_PV_CLIPBOARD_IMAGE))
{
    if (width == 0 || height == 0)
    {
        errorText = std::wstring(imageKind) + L" dimensions are invalid.";
        return false;
    }

    const uint64_t stride = static_cast<uint64_t>(width) * 4ull;
    const uint64_t bytes = stride * height;
    if (stride > (std::numeric_limits<uint32_t>::max)() ||
        bytes > (std::numeric_limits<size_t>::max)())
    {
        errorText = std::wstring(imageKind) + L" is too large.";
        return false;
    }

    ImageSurface next;
    next.Width = width;
    next.Height = height;
    next.Stride = static_cast<uint32_t>(stride);
    next.Format = ImageSurfaceFormat::Bgra32;
    try
    {
        next.Pixels.resize(static_cast<size_t>(bytes));
    }
    catch (const std::bad_alloc&)
    {
        errorText = FormatViewerText(IDS_PV_ALLOCATE_PIXELS_FAILED, imageKind);
        return false;
    }

    surface = std::move(next);
    return true;
}

void DrawCurrentCursorOnCapture(HDC dc, int virtualLeft, int virtualTop)
{
    CURSORINFO cursorInfo = {};
    cursorInfo.cbSize = sizeof(cursorInfo);
    if (!GetCursorInfo(&cursorInfo) ||
        (cursorInfo.flags & CURSOR_SHOWING) == 0 ||
        cursorInfo.hCursor == nullptr)
        return;

    ICONINFO iconInfo = {};
    if (!GetIconInfo(cursorInfo.hCursor, &iconInfo))
        return;

    const int x = cursorInfo.ptScreenPos.x - virtualLeft - static_cast<int>(iconInfo.xHotspot);
    const int y = cursorInfo.ptScreenPos.y - virtualTop - static_cast<int>(iconInfo.yHotspot);
    DrawIconEx(dc, x, y, cursorInfo.hCursor, 0, 0, 0, nullptr, DI_NORMAL);

    if (iconInfo.hbmMask != nullptr)
        DeleteObject(iconInfo.hbmMask);
    if (iconInfo.hbmColor != nullptr)
        DeleteObject(iconInfo.hbmColor);
}

bool CaptureScreenToDc(HDC targetDc,
                       HDC screenDc,
                       int virtualLeft,
                       int virtualTop,
                       int virtualWidth,
                       int virtualHeight,
                       DWORD& lastError)
{
    SetLastError(ERROR_SUCCESS);
    BOOL copied = BitBlt(targetDc,
                         0,
                         0,
                         virtualWidth,
                         virtualHeight,
                         screenDc,
                         virtualLeft,
                         virtualTop,
                         SRCCOPY | CAPTUREBLT);
    if (!copied)
    {
        lastError = GetLastError();
        SetLastError(ERROR_SUCCESS);
        copied = BitBlt(targetDc,
                        0,
                        0,
                        virtualWidth,
                        virtualHeight,
                        screenDc,
                        virtualLeft,
                        virtualTop,
                        SRCCOPY);
        if (!copied)
            lastError = GetLastError();
    }

    return copied != FALSE;
}

bool CaptureVirtualScreenViaCompatibleBitmap(HDC screen,
                                             HDC memoryDc,
                                             int virtualLeft,
                                             int virtualTop,
                                             int virtualWidth,
                                             int virtualHeight,
                                             bool includeCursor,
                                             ImageSurface& surface,
                                             std::wstring& errorText)
{
    HBITMAP bitmap = CreateCompatibleBitmap(screen, virtualWidth, virtualHeight);
    if (bitmap == nullptr)
    {
        errorText = ViewerText(IDS_PV_UNABLE_TO_CREATE_A_COMPATIBLE);
        return false;
    }

    HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
    if (oldBitmap == nullptr || oldBitmap == HGDI_ERROR)
    {
        DeleteObject(bitmap);
        errorText = ViewerText(IDS_PV_UNABLE_TO_SELECT_THE_COMPATIBLE);
        return false;
    }

    DWORD captureError = ERROR_SUCCESS;
    if (!CaptureScreenToDc(memoryDc,
                           screen,
                           virtualLeft,
                           virtualTop,
                           virtualWidth,
                           virtualHeight,
                           captureError))
    {
        SelectObject(memoryDc, oldBitmap);
        DeleteObject(bitmap);
        errorText = LastWin32ErrorText(ViewerText(IDS_PV_UNABLE_TO_CAPTURE_THE_SCREEN), captureError);
        return false;
    }

    if (includeCursor)
        DrawCurrentCursorOnCapture(memoryDc, virtualLeft, virtualTop);
    SelectObject(memoryDc, oldBitmap);

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = virtualWidth;
    bitmapInfo.bmiHeader.biHeight = -virtualHeight;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    const int rows = GetDIBits(screen,
                               bitmap,
                               0,
                               static_cast<UINT>(virtualHeight),
                               surface.Pixels.data(),
                               &bitmapInfo,
                               DIB_RGB_COLORS);
    DeleteObject(bitmap);
    if (rows != virtualHeight)
    {
        errorText = ViewerText(IDS_PV_UNABLE_TO_READ_PIXELS_FROM);
        return false;
    }

    ForceOpaqueAlpha(surface);
    return true;
}

bool CaptureVirtualScreenWithDc(HDC screen,
                                int virtualLeft,
                                int virtualTop,
                                int virtualWidth,
                                int virtualHeight,
                                bool includeCursor,
                                CaptureImageResult& result)
{
    HDC memoryDc = screen != nullptr ? CreateCompatibleDC(screen) : nullptr;
    if (memoryDc == nullptr)
    {
        result.ErrorText = ViewerText(IDS_PV_UNABLE_TO_CREATE_A_DRAWING);
        return false;
    }

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = virtualWidth;
    bitmapInfo.bmiHeader.biHeight = -virtualHeight;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib != nullptr && bits != nullptr)
    {
        HGDIOBJ oldBitmap = SelectObject(memoryDc, dib);
        DWORD captureError = ERROR_SUCCESS;
        if ((oldBitmap != nullptr && oldBitmap != HGDI_ERROR) &&
            CaptureScreenToDc(memoryDc,
                              screen,
                              virtualLeft,
                              virtualTop,
                              virtualWidth,
                              virtualHeight,
                              captureError))
        {
            if (includeCursor)
                DrawCurrentCursorOnCapture(memoryDc, virtualLeft, virtualTop);
            memcpy(result.Surface.Pixels.data(), bits, result.Surface.Pixels.size());
            ForceOpaqueAlpha(result.Surface);

            SelectObject(memoryDc, oldBitmap);
            DeleteObject(dib);
            DeleteDC(memoryDc);

            result.Success = true;
            return true;
        }

        if (oldBitmap != nullptr && oldBitmap != HGDI_ERROR)
            SelectObject(memoryDc, oldBitmap);
        DeleteObject(dib);
    }
    else if (dib != nullptr)
    {
        DeleteObject(dib);
    }

    DeleteDC(memoryDc);

    HDC fallbackDc = screen != nullptr ? CreateCompatibleDC(screen) : nullptr;
    if (fallbackDc == nullptr)
    {
        result.ErrorText = ViewerText(IDS_PV_UNABLE_TO_CREATE_A_FALLBACK);
        return false;
    }

    result.Success = CaptureVirtualScreenViaCompatibleBitmap(screen,
                                                             fallbackDc,
                                                             virtualLeft,
                                                             virtualTop,
                                                             virtualWidth,
                                                             virtualHeight,
                                                             includeCursor,
                                                             result.Surface,
                                                             result.ErrorText);
    DeleteDC(fallbackDc);
    return result.Success;
}

RECT IntersectRects(RECT left, RECT right)
{
    RECT intersection = {};
    IntersectRect(&intersection, &left, &right);
    return intersection;
}

bool IsRectEmptyOrInvalid(const RECT& rect)
{
    return rect.right <= rect.left || rect.bottom <= rect.top;
}

struct CaptureScopeTarget
{
    RECT Bounds = {};
    HRGN ClipRegion = nullptr;
    int ClipOriginX = 0;
    int ClipOriginY = 0;
};

void DestroyCaptureScopeTarget(CaptureScopeTarget& target)
{
    if (target.ClipRegion != nullptr)
    {
        DeleteObject(target.ClipRegion);
        target.ClipRegion = nullptr;
    }
}

void AttachComplexWindowRegion(HWND hwnd, CaptureScopeTarget& target)
{
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    if (region == nullptr)
        return;

    const int regionType = GetWindowRgn(hwnd, region);
    if (regionType == COMPLEXREGION)
    {
        target.ClipRegion = region;
        target.ClipOriginX = target.Bounds.left;
        target.ClipOriginY = target.Bounds.top;
        return;
    }

    DeleteObject(region);
}

bool ApplyCaptureRegionMask(ImageSurface& surface, HRGN region, COLORREF fillColor)
{
    const uint64_t minimumStride = static_cast<uint64_t>(surface.Width) * 4ull;
    const uint64_t minimumSize = surface.Height == 0
                                     ? 0
                                     : (static_cast<uint64_t>(surface.Height) - 1ull) * surface.Stride + minimumStride;
    if (region == nullptr ||
        surface.Format != ImageSurfaceFormat::Bgra32 ||
        surface.Width == 0 ||
        surface.Height == 0 ||
        minimumStride > (std::numeric_limits<uint32_t>::max)() ||
        surface.Stride < minimumStride ||
        minimumSize > surface.Pixels.size())
    {
        return false;
    }

    const uint8_t fillBlue = static_cast<uint8_t>(GetBValue(fillColor));
    const uint8_t fillGreen = static_cast<uint8_t>(GetGValue(fillColor));
    const uint8_t fillRed = static_cast<uint8_t>(GetRValue(fillColor));

    for (uint32_t y = 0; y < surface.Height; ++y)
    {
        uint8_t* row = surface.Pixels.data() + static_cast<size_t>(y) * surface.Stride;
        for (uint32_t x = 0; x < surface.Width; ++x)
        {
            if (PtInRegion(region, static_cast<int>(x), static_cast<int>(y)))
                continue;

            uint8_t* pixel = row + static_cast<size_t>(x) * 4u;
            pixel[0] = fillBlue;
            pixel[1] = fillGreen;
            pixel[2] = fillRed;
            pixel[3] = 255;
        }
    }

    return true;
}

RECT PrimaryDesktopBounds()
{
    const int width = GetSystemMetrics(SM_CXSCREEN);
    const int height = GetSystemMetrics(SM_CYSCREEN);
    if (width <= 0 || height <= 0)
        return {};
    return {0, 0, width, height};
}

RECT VirtualScreenBounds()
{
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0)
        return PrimaryDesktopBounds();
    return {left, top, left + width, top + height};
}

HWND ForegroundApplicationWindow(HWND hwnd)
{
    HWND current = hwnd;
    HWND parent = nullptr;
    while (current != nullptr &&
           (parent = GetParent(current)) != nullptr &&
           IsWindowVisible(parent))
    {
        current = parent;
    }
    return current;
}

bool TryForegroundCaptureTarget(ViewerCaptureScope scope, CaptureScopeTarget& target)
{
    HWND foreground = GetForegroundWindow();
    if (foreground == nullptr || !IsWindow(foreground))
        return false;

    switch (scope)
    {
    case ViewerCaptureScope::Application:
        foreground = ForegroundApplicationWindow(foreground);
        if (foreground == nullptr ||
            GetWindowRect(foreground, &target.Bounds) == FALSE ||
            IsRectEmptyOrInvalid(target.Bounds))
        {
            return false;
        }
        AttachComplexWindowRegion(foreground, target);
        return true;
    case ViewerCaptureScope::Window:
        if (GetWindowRect(foreground, &target.Bounds) == FALSE ||
            IsRectEmptyOrInvalid(target.Bounds))
        {
            return false;
        }
        AttachComplexWindowRegion(foreground, target);
        return true;
    case ViewerCaptureScope::ClientArea:
        if (!GetClientRect(foreground, &target.Bounds))
            return false;
        MapWindowPoints(foreground, nullptr, reinterpret_cast<POINT*>(&target.Bounds), 2);
        return !IsRectEmptyOrInvalid(target.Bounds);
    case ViewerCaptureScope::Desktop:
    case ViewerCaptureScope::VirtualScreen:
    default:
        return false;
    }
}

CaptureScopeTarget CaptureTargetForScope(ViewerCaptureScope scope)
{
    const RECT virtualBounds = VirtualScreenBounds();
    CaptureScopeTarget target;

    switch (scope)
    {
    case ViewerCaptureScope::Desktop:
        target.Bounds = PrimaryDesktopBounds();
        break;
    case ViewerCaptureScope::Application:
    case ViewerCaptureScope::Window:
    case ViewerCaptureScope::ClientArea:
        if (!TryForegroundCaptureTarget(scope, target))
            target.Bounds = virtualBounds;
        break;
    case ViewerCaptureScope::VirtualScreen:
    default:
        target.Bounds = virtualBounds;
        break;
    }

    RECT bounded = IsRectEmptyOrInvalid(virtualBounds) ? target.Bounds : IntersectRects(target.Bounds, virtualBounds);
    if (IsRectEmptyOrInvalid(bounded))
    {
        DestroyCaptureScopeTarget(target);
        bounded = virtualBounds;
    }
    if (IsRectEmptyOrInvalid(bounded))
    {
        DestroyCaptureScopeTarget(target);
        bounded = PrimaryDesktopBounds();
    }

    if (target.ClipRegion != nullptr)
        OffsetRgn(target.ClipRegion, target.ClipOriginX - bounded.left, target.ClipOriginY - bounded.top);

    target.Bounds = bounded;
    return target;
}

void CopyDxgiMappedRowsToSurface(const D3D11_TEXTURE2D_DESC& textureDesc,
                                 const D3D11_MAPPED_SUBRESOURCE& mapped,
                                 DXGI_FORMAT format,
                                 const RECT& outputRect,
                                 const RECT& copyRect,
                                 int virtualLeft,
                                 int virtualTop,
                                 ImageSurface& surface)
{
    const uint32_t copyWidth = static_cast<uint32_t>(copyRect.right - copyRect.left);
    const uint32_t copyHeight = static_cast<uint32_t>(copyRect.bottom - copyRect.top);
    const uint32_t sourceX = static_cast<uint32_t>(copyRect.left - outputRect.left);
    const uint32_t sourceY = static_cast<uint32_t>(copyRect.top - outputRect.top);
    const uint32_t destX = static_cast<uint32_t>(copyRect.left - virtualLeft);
    const uint32_t destY = static_cast<uint32_t>(copyRect.top - virtualTop);
    const uint32_t boundedWidth = std::min<uint32_t>(copyWidth, textureDesc.Width > sourceX ? textureDesc.Width - sourceX : 0);
    const uint32_t boundedHeight = std::min<uint32_t>(copyHeight, textureDesc.Height > sourceY ? textureDesc.Height - sourceY : 0);

    for (uint32_t y = 0; y < boundedHeight; ++y)
    {
        const uint8_t* source = static_cast<const uint8_t*>(mapped.pData) +
                                static_cast<size_t>(sourceY + y) * mapped.RowPitch +
                                static_cast<size_t>(sourceX) * 4u;
        uint8_t* dest = surface.Pixels.data() +
                        static_cast<size_t>(destY + y) * surface.Stride +
                        static_cast<size_t>(destX) * 4u;

        if (format == DXGI_FORMAT_B8G8R8A8_UNORM ||
            format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
            format == DXGI_FORMAT_B8G8R8X8_UNORM ||
            format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB)
        {
            memcpy(dest, source, static_cast<size_t>(boundedWidth) * 4u);
        }
        else
        {
            for (uint32_t x = 0; x < boundedWidth; ++x)
            {
                const uint8_t* srcPixel = source + static_cast<size_t>(x) * 4u;
                uint8_t* dstPixel = dest + static_cast<size_t>(x) * 4u;
                dstPixel[0] = srcPixel[2];
                dstPixel[1] = srcPixel[1];
                dstPixel[2] = srcPixel[0];
                dstPixel[3] = srcPixel[3];
            }
        }
    }
}

bool CaptureDxgiOutput(IDXGIAdapter1* adapter,
                       IDXGIOutput* output,
                       const RECT& virtualRect,
                       int virtualLeft,
                       int virtualTop,
                       ImageSurface& surface,
                       HRESULT& lastHr)
{
    if (adapter == nullptr || output == nullptr)
        return false;

    DXGI_OUTPUT_DESC outputDesc = {};
    HRESULT hr = output->GetDesc(&outputDesc);
    if (FAILED(hr))
    {
        lastHr = hr;
        return false;
    }

    const RECT copyRect = IntersectRects(outputDesc.DesktopCoordinates, virtualRect);
    if (IsRectEmptyOrInvalid(copyRect))
        return false;

    static const D3D_FEATURE_LEVEL FeatureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    hr = D3D11CreateDevice(adapter,
                           D3D_DRIVER_TYPE_UNKNOWN,
                           nullptr,
                           D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                           FeatureLevels,
                           static_cast<UINT>(sizeof(FeatureLevels) / sizeof(FeatureLevels[0])),
                           D3D11_SDK_VERSION,
                           &device,
                           &featureLevel,
                           &context);
    if (FAILED(hr))
    {
        lastHr = hr;
        return false;
    }

    ComPtr<IDXGIOutput1> output1;
    hr = output->QueryInterface(IID_PPV_ARGS(&output1));
    if (FAILED(hr))
    {
        lastHr = hr;
        return false;
    }

    ComPtr<IDXGIOutputDuplication> duplication;
    hr = output1->DuplicateOutput(device.Get(), &duplication);
    if (FAILED(hr))
    {
        lastHr = hr;
        return false;
    }

    DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
    ComPtr<IDXGIResource> frameResource;
    hr = duplication->AcquireNextFrame(1000, &frameInfo, &frameResource);
    if (FAILED(hr))
    {
        lastHr = hr;
        return false;
    }

    bool frameAcquired = true;
    bool copied = false;
    ComPtr<ID3D11Texture2D> frameTexture;
    hr = frameResource->QueryInterface(IID_PPV_ARGS(&frameTexture));
    if (SUCCEEDED(hr))
    {
        D3D11_TEXTURE2D_DESC textureDesc = {};
        frameTexture->GetDesc(&textureDesc);

        if (textureDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
            textureDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
            textureDesc.Format == DXGI_FORMAT_B8G8R8X8_UNORM ||
            textureDesc.Format == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB ||
            textureDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
            textureDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
        {
            D3D11_TEXTURE2D_DESC stagingDesc = textureDesc;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            stagingDesc.MiscFlags = 0;

            ComPtr<ID3D11Texture2D> staging;
            hr = device->CreateTexture2D(&stagingDesc, nullptr, &staging);
            if (SUCCEEDED(hr))
            {
                context->CopyResource(staging.Get(), frameTexture.Get());
                D3D11_MAPPED_SUBRESOURCE mapped = {};
                hr = context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
                if (SUCCEEDED(hr))
                {
                    CopyDxgiMappedRowsToSurface(textureDesc,
                                                mapped,
                                                textureDesc.Format,
                                                outputDesc.DesktopCoordinates,
                                                copyRect,
                                                virtualLeft,
                                                virtualTop,
                                                surface);
                    context->Unmap(staging.Get(), 0);
                    copied = true;
                }
            }
        }
        else
        {
            hr = DXGI_ERROR_UNSUPPORTED;
        }
    }

    if (FAILED(hr))
        lastHr = hr;

    if (frameAcquired)
        duplication->ReleaseFrame();

    return copied;
}

bool CaptureVirtualScreenViaDxgi(int virtualLeft,
                                 int virtualTop,
                                 int virtualWidth,
                                 int virtualHeight,
                                 CaptureImageResult& result)
{
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr))
    {
        result.ErrorText = HResultErrorText(ViewerText(IDS_PV_UNABLE_TO_INITIALIZE_DXGI_SCREEN), hr);
        return false;
    }

    const RECT virtualRect = {virtualLeft, virtualTop, virtualLeft + virtualWidth, virtualTop + virtualHeight};
    HRESULT lastHr = S_OK;
    bool capturedAny = false;

    for (UINT adapterIndex = 0;; ++adapterIndex)
    {
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory->EnumAdapters1(adapterIndex, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(hr))
        {
            lastHr = hr;
            continue;
        }

        for (UINT outputIndex = 0;; ++outputIndex)
        {
            ComPtr<IDXGIOutput> output;
            hr = adapter->EnumOutputs(outputIndex, &output);
            if (hr == DXGI_ERROR_NOT_FOUND)
                break;
            if (FAILED(hr))
            {
                lastHr = hr;
                continue;
            }

            if (CaptureDxgiOutput(adapter.Get(),
                                  output.Get(),
                                  virtualRect,
                                  virtualLeft,
                                  virtualTop,
                                  result.Surface,
                                  lastHr))
            {
                capturedAny = true;
            }
        }
    }

    if (!capturedAny)
    {
        result.ErrorText = FAILED(lastHr)
                               ? HResultErrorText(ViewerText(IDS_PV_UNABLE_TO_CAPTURE_THE_SCREEN_2), lastHr)
                               : ViewerText(IDS_PV_UNABLE_TO_CAPTURE_THE_SCREEN_3);
        return false;
    }

    ForceOpaqueAlpha(result.Surface);
    result.Success = true;
    return true;
}

CaptureImageResult CaptureScreenScopeSurface(ViewerCaptureScope scope, bool includeCursor)
{
    CaptureImageResult result;

    CaptureScopeTarget target = CaptureTargetForScope(scope);
    auto finishCapture = [&]() {
        if (result.Success && target.ClipRegion != nullptr)
            ApplyCaptureRegionMask(result.Surface, target.ClipRegion, GetViewerPreferences().TransparentColor);
        DestroyCaptureScopeTarget(target);
        return result;
    };

    const RECT bounds = target.Bounds;
    int virtualLeft = bounds.left;
    int virtualTop = bounds.top;
    int virtualWidth = bounds.right - bounds.left;
    int virtualHeight = bounds.bottom - bounds.top;
    if (virtualWidth <= 0 || virtualHeight <= 0)
    {
        result.ErrorText = ViewerText(IDS_PV_SCREEN_CAPTURE_BOUNDS_ARE_INVALID);
        return finishCapture();
    }

    if (virtualWidth <= 0 || virtualHeight <= 0 ||
        !AllocateBgraSurface(static_cast<uint32_t>(virtualWidth),
                             static_cast<uint32_t>(virtualHeight),
                             result.Surface,
                             result.ErrorText,
                             ViewerText(IDS_PV_SCREEN_CAPTURE)))
    {
        return finishCapture();
    }

    std::wstring lastError;

    HDC screen = GetDC(nullptr);
    if (screen != nullptr)
    {
        if (CaptureVirtualScreenWithDc(screen, virtualLeft, virtualTop, virtualWidth, virtualHeight, includeCursor, result))
        {
            ReleaseDC(nullptr, screen);
            return finishCapture();
        }
        lastError = result.ErrorText;
        ReleaseDC(nullptr, screen);
    }

    HWND desktop = GetDesktopWindow();
    HDC desktopDc = desktop != nullptr ? GetDC(desktop) : nullptr;
    if (desktopDc != nullptr)
    {
        if (CaptureVirtualScreenWithDc(desktopDc, virtualLeft, virtualTop, virtualWidth, virtualHeight, includeCursor, result))
        {
            ReleaseDC(desktop, desktopDc);
            return finishCapture();
        }
        lastError = result.ErrorText;
        ReleaseDC(desktop, desktopDc);
    }

    HDC displayDc = CreateDCW(L"DISPLAY", nullptr, nullptr, nullptr);
    if (displayDc != nullptr)
    {
        if (CaptureVirtualScreenWithDc(displayDc, virtualLeft, virtualTop, virtualWidth, virtualHeight, includeCursor, result))
        {
            DeleteDC(displayDc);
            return finishCapture();
        }
        lastError = result.ErrorText;
        DeleteDC(displayDc);
    }

    result.ErrorText = lastError.empty() ? ViewerText(IDS_PV_UNABLE_TO_CREATE_A_DRAWING) : lastError;
    const std::wstring gdiError = result.ErrorText;
    if (CaptureVirtualScreenViaDxgi(virtualLeft, virtualTop, virtualWidth, virtualHeight, result))
        return finishCapture();

    if (!gdiError.empty() && !result.ErrorText.empty())
        result.ErrorText = gdiError + L"\n" + result.ErrorText;
    return finishCapture();
}

size_t DibColorTableEntries(const BITMAPINFOHEADER& header)
{
    if (header.biClrUsed != 0)
        return header.biClrUsed;

    if (header.biBitCount <= 8)
        return static_cast<size_t>(1u) << header.biBitCount;

    return 0;
}

bool DibBitsOffset(const BITMAPINFOHEADER& header, size_t dataSize, size_t& offset)
{
    if (header.biSize < sizeof(BITMAPINFOHEADER) || header.biSize > dataSize)
        return false;

    offset = header.biSize;
    if (header.biSize == sizeof(BITMAPINFOHEADER) && header.biCompression == BI_BITFIELDS)
        offset += 3u * sizeof(DWORD);

    offset += DibColorTableEntries(header) * sizeof(RGBQUAD);
    return offset <= dataSize;
}

ClipboardImageResult SurfaceFromDibMemory(HGLOBAL memory)
{
    ClipboardImageResult result;
    if (memory == nullptr)
    {
        result.ErrorText = ViewerText(IDS_PV_CLIPBOARD_IMAGE_DATA_IS_UNAVAILABLE);
        return result;
    }

    const SIZE_T dataSize = GlobalSize(memory);
    if (dataSize < sizeof(BITMAPINFOHEADER))
    {
        result.ErrorText = ViewerText(IDS_PV_CLIPBOARD_IMAGE_DATA_IS_INCOMPLETE);
        return result;
    }

    uint8_t* data = static_cast<uint8_t*>(GlobalLock(memory));
    if (data == nullptr)
    {
        result.ErrorText = LastWin32ErrorText(ViewerText(IDS_PV_UNABLE_TO_LOCK_CLIPBOARD_IMAGE), GetLastError());
        return result;
    }

    const BITMAPINFOHEADER* header = reinterpret_cast<const BITMAPINFOHEADER*>(data);
    if (header->biSize < sizeof(BITMAPINFOHEADER) ||
        header->biPlanes != 1 ||
        header->biWidth <= 0 ||
        header->biHeight == 0 ||
        header->biCompression == BI_JPEG ||
        header->biCompression == BI_PNG)
    {
        GlobalUnlock(memory);
        result.ErrorText = ViewerText(IDS_PV_CLIPBOARD_IMAGE_FORMAT_IS_UNSUPPORTED);
        return result;
    }

    size_t bitsOffset = 0;
    if (!DibBitsOffset(*header, static_cast<size_t>(dataSize), bitsOffset))
    {
        GlobalUnlock(memory);
        result.ErrorText = ViewerText(IDS_PV_CLIPBOARD_IMAGE_DATA_IS_INCOMPLETE);
        return result;
    }

    const uint32_t width = static_cast<uint32_t>(header->biWidth);
    const uint32_t height = static_cast<uint32_t>(header->biHeight < 0 ? -static_cast<long long>(header->biHeight) : header->biHeight);
    if (!AllocateBgraSurface(width, height, result.Surface, result.ErrorText))
    {
        GlobalUnlock(memory);
        return result;
    }

    HDC screen = GetDC(nullptr);
    HDC memoryDc = screen != nullptr ? CreateCompatibleDC(screen) : nullptr;
    if (screen == nullptr || memoryDc == nullptr)
    {
        if (memoryDc != nullptr)
            DeleteDC(memoryDc);
        if (screen != nullptr)
            ReleaseDC(nullptr, screen);
        GlobalUnlock(memory);
        result.ErrorText = ViewerText(IDS_PV_UNABLE_TO_CREATE_A_DRAWING_2);
        return result;
    }

    BITMAPINFO dstInfo = {};
    dstInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    dstInfo.bmiHeader.biWidth = static_cast<LONG>(width);
    dstInfo.bmiHeader.biHeight = -static_cast<LONG>(height);
    dstInfo.bmiHeader.biPlanes = 1;
    dstInfo.bmiHeader.biBitCount = 32;
    dstInfo.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(memoryDc, &dstInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib == nullptr || bits == nullptr)
    {
        if (dib != nullptr)
            DeleteObject(dib);
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screen);
        GlobalUnlock(memory);
        result.ErrorText = ViewerText(IDS_PV_UNABLE_TO_CREATE_CLIPBOARD_IMAGE);
        return result;
    }

    HGDIOBJ oldBitmap = SelectObject(memoryDc, dib);
    SetStretchBltMode(memoryDc, COLORONCOLOR);
    const int copied = StretchDIBits(memoryDc,
                                     0,
                                     0,
                                     static_cast<int>(width),
                                     static_cast<int>(height),
                                     0,
                                     0,
                                     static_cast<int>(width),
                                     static_cast<int>(height),
                                     data + bitsOffset,
                                     reinterpret_cast<const BITMAPINFO*>(data),
                                     DIB_RGB_COLORS,
                                     SRCCOPY);
    if (copied == GDI_ERROR || copied == 0)
    {
        SelectObject(memoryDc, oldBitmap);
        DeleteObject(dib);
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screen);
        GlobalUnlock(memory);
        result.ErrorText = ViewerText(IDS_PV_UNABLE_TO_CONVERT_CLIPBOARD_IMAGE);
        return result;
    }

    memcpy(result.Surface.Pixels.data(), bits, result.Surface.Pixels.size());
    ForceOpaqueAlpha(result.Surface);

    SelectObject(memoryDc, oldBitmap);
    DeleteObject(dib);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screen);
    GlobalUnlock(memory);

    result.Success = true;
    return result;
}

ClipboardImageResult SurfaceFromBitmapHandle(HBITMAP bitmap, const wchar_t* bitmapKind = ViewerText(IDS_PV_CLIPBOARD_BITMAP))
{
    ClipboardImageResult result;
    if (bitmap == nullptr)
    {
        result.ErrorText = std::wstring(bitmapKind) + L" is unavailable.";
        return result;
    }

    BITMAP info = {};
    if (GetObjectW(bitmap, sizeof(info), &info) != sizeof(info) ||
        info.bmWidth <= 0 || info.bmHeight <= 0)
    {
        result.ErrorText = std::wstring(bitmapKind) + L" dimensions are invalid.";
        return result;
    }

    const uint32_t width = static_cast<uint32_t>(info.bmWidth);
    const uint32_t height = static_cast<uint32_t>(info.bmHeight);
    if (!AllocateBgraSurface(width, height, result.Surface, result.ErrorText))
        return result;

    HDC screen = GetDC(nullptr);
    if (screen == nullptr)
    {
        result.ErrorText = FormatViewerText(IDS_PV_CONVERSION_CONTEXT_FAILED, bitmapKind);
        return result;
    }

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(width);
    bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(height);
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    const int rows = GetDIBits(screen,
                               bitmap,
                               0,
                               height,
                               result.Surface.Pixels.data(),
                               &bitmapInfo,
                               DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen);
    if (rows == 0)
    {
        result.ErrorText = FormatViewerText(IDS_PV_READ_PIXELS_FAILED, bitmapKind);
        return result;
    }

    ForceOpaqueAlpha(result.Surface);
    result.Success = true;
    return result;
}

CaptureImageResult CaptureWindowSurface(HWND hwnd)
{
    CaptureImageResult result;
    if (hwnd == nullptr || !IsWindow(hwnd))
    {
        result.ErrorText = ViewerText(IDS_PV_UNABLE_TO_CAPTURE_THE_FALLBACK);
        return result;
    }

    RECT rect = {};
    if (!GetWindowRect(hwnd, &rect) || rect.right <= rect.left || rect.bottom <= rect.top)
    {
        result.ErrorText = ViewerText(IDS_PV_FALLBACK_WINDOW_DIMENSIONS_ARE_INVALID);
        return result;
    }

    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    HDC screen = GetDC(nullptr);
    if (screen == nullptr)
    {
        result.ErrorText = ViewerText(IDS_PV_UNABLE_TO_CREATE_A_DRAWING_3);
        return result;
    }

    HDC memoryDc = CreateCompatibleDC(screen);
    HBITMAP bitmap = memoryDc != nullptr ? CreateCompatibleBitmap(screen, width, height) : nullptr;
    HGDIOBJ oldBitmap = bitmap != nullptr ? SelectObject(memoryDc, bitmap) : nullptr;
    bool printed = false;
    if (oldBitmap != nullptr && oldBitmap != HGDI_ERROR)
    {
        printed = PrintWindow(hwnd, memoryDc, PW_RENDERFULLCONTENT) != FALSE;
        if (!printed)
            printed = PrintWindow(hwnd, memoryDc, 0) != FALSE;
    }

    ClipboardImageResult converted;
    if (printed)
        converted = SurfaceFromBitmapHandle(bitmap, L"fallback window bitmap");

    if (oldBitmap != nullptr && oldBitmap != HGDI_ERROR)
        SelectObject(memoryDc, oldBitmap);
    if (bitmap != nullptr)
        DeleteObject(bitmap);
    if (memoryDc != nullptr)
        DeleteDC(memoryDc);
    ReleaseDC(nullptr, screen);

    if (!printed)
    {
        result.ErrorText = ViewerText(IDS_PV_UNABLE_TO_PRINT_THE_FALLBACK);
        return result;
    }
    if (!converted.Success)
    {
        result.ErrorText = converted.ErrorText;
        return result;
    }

    result.Surface = std::move(converted.Surface);
    result.Success = true;
    return result;
}

void ApplyDefaultFont(HWND hwnd)
{
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    if (font != nullptr)
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

int SanitizeCaptureTimerSeconds(int seconds)
{
    if (seconds < 1 || seconds > 360)
        return 5;
    return seconds;
}

WORD SanitizeCaptureHotKey(WORD hotKey)
{
    return hotKey == 0 ? static_cast<WORD>(VK_F10) : hotKey;
}

UINT RegisterHotKeyModifiersFromControl(WORD hotKey)
{
    const BYTE modifiers = HIBYTE(hotKey);
    UINT result = 0;
    if ((modifiers & HOTKEYF_SHIFT) != 0)
        result |= MOD_SHIFT;
    if ((modifiers & HOTKEYF_CONTROL) != 0)
        result |= MOD_CONTROL;
    if ((modifiers & HOTKEYF_ALT) != 0)
        result |= MOD_ALT;
    return result;
}

UINT RegisterHotKeyVirtualKeyFromControl(WORD hotKey)
{
    return LOBYTE(hotKey);
}

class ScopedComApartment
{
public:
    explicit ScopedComApartment(DWORD flags)
    {
        const HRESULT hr = CoInitializeEx(nullptr, flags);
        m_uninitialize = hr == S_OK || hr == S_FALSE;
    }

    ~ScopedComApartment()
    {
        if (m_uninitialize)
            CoUninitialize();
    }

    ScopedComApartment(const ScopedComApartment&) = delete;
    ScopedComApartment& operator=(const ScopedComApartment&) = delete;

private:
    bool m_uninitialize = false;
};

ViewerHistogramChannel SanitizeHistogramChannel(ViewerHistogramChannel channel)
{
    switch (channel)
    {
    case ViewerHistogramChannel::Luminosity:
    case ViewerHistogramChannel::Red:
    case ViewerHistogramChannel::Green:
    case ViewerHistogramChannel::Blue:
    case ViewerHistogramChannel::Rgb:
        return channel;
    default:
        return ViewerHistogramChannel::Rgb;
    }
}

ViewerHistogramPreferences SanitizeHistogramPreferences(ViewerHistogramPreferences preferences)
{
    // Whole multiples of 256 pixels wide, as the window keeps them.
    preferences.ChartWidth = std::clamp((preferences.ChartWidth + 128) & ~0xFF, 256, 4096);
    preferences.ChartHeight = std::clamp(preferences.ChartHeight, 60, 4000);
    preferences.Channel = SanitizeHistogramChannel(preferences.Channel);
    return preferences;
}

int ClampMetadataDetailsWindowDimension(int value)
{
    if (value < 260)
        return 260;
    if (value > 4000)
        return 4000;
    return value;
}

ViewerMetadataDetailsPreferences SanitizeMetadataDetailsPreferences(ViewerMetadataDetailsPreferences preferences)
{
    preferences.WindowWidth = ClampMetadataDetailsWindowDimension(preferences.WindowWidth);
    preferences.WindowHeight = ClampMetadataDetailsWindowDimension(preferences.WindowHeight);
    return preferences;
}

// ------------------------------------------------------------------------------------------
// Histogram window: the old PictView's, on Sally's tool bar. A drop-down picks the channel shown
// as bars; a toggle draws the other channels as curves over it; a tone band runs underneath.

constexpr int HistogramSeparatorHeight = 3;
constexpr int HistogramToneBandHeight = 16;
constexpr DWORD HistogramCmdChannelButton = 100;
constexpr DWORD HistogramCmdFirstChannel = 101; // + ViewerHistogramChannel
constexpr DWORD HistogramCmdOtherChannels = 106;
constexpr int IDX_TB_OTHERCHANNELS = 28;
constexpr int IDX_TB_FIRSTCHANNEL = 29; // + ViewerHistogramChannel: luminosity, red, green, blue, RGB

struct HistogramWindowState
{
    HistogramLevels Levels = {};
    ViewerHistogramChannel Channel = ViewerHistogramChannel::Rgb;
    bool ShowOtherChannels = true;
    HWND Owner = nullptr;
    CGUIToolBarAbstract* Toolbar = nullptr;
    HIMAGELIST GrayImages = nullptr;
    HIMAGELIST HotImages = nullptr;
    std::wstring ChannelText;
    int FrameWidth = 0; // window width minus the chart width
};

UINT HistogramChannelTextId(ViewerHistogramChannel channel)
{
    return IDS_HIST_LUMINOSITY + static_cast<UINT>(channel); // Luminosity, Red, Green, Blue, RGB
}

int HistogramToolbarHeight(const HistogramWindowState* state)
{
    return state != nullptr && state->Toolbar != nullptr ? state->Toolbar->GetNeededHeight() : 0;
}

// The chart's place in the client area: under the tool bar, inside a one-pixel sunken edge.
RECT HistogramChartRect(HWND hwnd, const HistogramWindowState* state)
{
    RECT client = {};
    GetClientRect(hwnd, &client);
    client.top += HistogramToolbarHeight(state);
    InflateRect(&client, -1, -1);
    if (client.right < client.left)
        client.right = client.left;
    if (client.bottom < client.top)
        client.bottom = client.top;
    return client;
}

void PaintHistogramChart(HDC dc, const RECT& chart, const HistogramWindowState& state)
{
    const int width = chart.right - chart.left;
    const int mag = (std::max)(1, width / 256);
    const int height = (std::max)(0, static_cast<int>(chart.bottom - chart.top) - HistogramSeparatorHeight - HistogramToneBandHeight);
    auto fill = [&](int left, int top, int right, int bottom, COLORREF color) {
        RECT rect = {chart.left + left, chart.top + top, chart.left + right, chart.top + bottom};
        SetBkColor(dc, color);
        ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &rect, nullptr, 0, nullptr);
    };

    fill(0, 0, width, chart.bottom - chart.top, RGB(0, 0, 0));
    const size_t channel = static_cast<size_t>(state.Channel);
    for (int i = 0; i < 256; ++i)
    {
        const int bar = static_cast<int>(height * state.Levels[channel][static_cast<size_t>(i)] + 0.5);
        fill(i * mag, height - bar, i * mag + mag, height, RGB(0xB4, 0xB4, 0xB4));
    }

    if (state.ShowOtherChannels)
    {
        const COLORREF colors[5] = {RGB(255, 255, 0), RGB(255, 0, 0), RGB(0, 255, 0), RGB(0x20, 0x40, 0xFF), RGB(255, 255, 255)};
        for (size_t c = 0; c < 5; ++c)
        {
            if (c == channel)
                continue;
            HPEN pen = CreatePen(PS_SOLID, 0, colors[c]);
            HGDIOBJ previous = SelectObject(dc, pen);
            int y = height - static_cast<int>(height * state.Levels[c][0] + 0.5);
            MoveToEx(dc, chart.left, chart.top + y, nullptr);
            for (int i = 0; i < 256; ++i)
            {
                y = height - static_cast<int>(height * state.Levels[c][static_cast<size_t>(i)] + 0.5);
                LineTo(dc, chart.left + i * mag + mag / 2, chart.top + y);
            }
            LineTo(dc, chart.right, chart.top + y);
            SelectObject(dc, previous);
            DeleteObject(pen);
        }
    }

    // The 3D separator, then the tone band of the shown channel.
    const COLORREF separator[3] = {GetSysColor(COLOR_3DHILIGHT), GetSysColor(COLOR_3DFACE), GetSysColor(COLOR_3DSHADOW)};
    for (int k = 0; k < HistogramSeparatorHeight; ++k)
        fill(0, height + k, width, height + k + 1, separator[k]);
    const int band = height + HistogramSeparatorHeight;
    for (int i = 0; i < 256; ++i)
    {
        const BYTE level = static_cast<BYTE>(i);
        COLORREF color = RGB(level, level, level);
        if (state.Channel == ViewerHistogramChannel::Red)
            color = RGB(level, 0, 0);
        else if (state.Channel == ViewerHistogramChannel::Green)
            color = RGB(0, level, 0);
        else if (state.Channel == ViewerHistogramChannel::Blue)
            color = RGB(0, 0, level);
        fill(i * mag, band, i * mag + mag, chart.bottom - chart.top, color);
    }
}

void PaintHistogramWindow(HWND hwnd, HistogramWindowState* state)
{
    PAINTSTRUCT ps = {};
    HDC dc = BeginPaint(hwnd, &ps);
    if (dc == nullptr)
        return;
    if (state != nullptr)
    {
        RECT edge = HistogramChartRect(hwnd, state);
        InflateRect(&edge, 1, 1);
        const RECT chart = HistogramChartRect(hwnd, state);
        // Composed off-screen, as the viewer is, so channel changes do not flicker.
        const int width = chart.right - chart.left;
        const int height = chart.bottom - chart.top;
        HDC memory = width > 0 && height > 0 ? CreateCompatibleDC(dc) : nullptr;
        HBITMAP bitmap = memory != nullptr ? CreateCompatibleBitmap(dc, width, height) : nullptr;
        if (bitmap != nullptr)
        {
            HGDIOBJ previous = SelectObject(memory, bitmap);
            SetViewportOrgEx(memory, -chart.left, -chart.top, nullptr);
            PaintHistogramChart(memory, chart, *state);
            BitBlt(dc, chart.left, chart.top, width, height, memory, chart.left, chart.top, SRCCOPY);
            SelectObject(memory, previous);
            DeleteObject(bitmap);
        }
        if (memory != nullptr)
            DeleteDC(memory);
        DrawEdge(dc, &edge, BDR_SUNKENOUTER, BF_RECT);
    }
    EndPaint(hwnd, &ps);
}

void UpdateHistogramToolbar(HistogramWindowState* state)
{
    if (state->Toolbar == nullptr)
        return;
    state->ChannelText = ViewerText(HistogramChannelTextId(state->Channel));
    TLBI_ITEM_INFO2 item = {};
    item.Mask = TLBI_MASK_TEXT;
    item.Text = state->ChannelText.data();
    state->Toolbar->SetItemInfo2(HistogramCmdChannelButton, FALSE, &item);
    state->Toolbar->CheckItem(HistogramCmdOtherChannels, FALSE, state->ShowOtherChannels);
}

void LayoutHistogramWindow(HWND hwnd, HistogramWindowState* state)
{
    if (state == nullptr || state->Toolbar == nullptr)
        return;
    RECT client = {};
    GetClientRect(hwnd, &client);
    SetWindowPos(state->Toolbar->GetHWND(), nullptr, 0, 0, (std::max)(0L, client.right), state->Toolbar->GetNeededHeight(),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void StoreHistogramPreferencesFromWindow(HWND hwnd, HistogramWindowState* state)
{
    if (hwnd == nullptr || state == nullptr)
        return;
    ViewerHistogramPreferences preferences = GetHistogramPreferences();
    RECT rect = {};
    if (GetWindowRect(hwnd, &rect))
    {
        preferences.WindowX = rect.left;
        preferences.WindowY = rect.top;
    }
    const RECT chart = HistogramChartRect(hwnd, state);
    preferences.ChartWidth = chart.right - chart.left;
    preferences.ChartHeight = chart.bottom - chart.top;
    preferences.Channel = state->Channel;
    preferences.ShowOtherChannels = state->ShowOtherChannels;
    SetHistogramPreferences(preferences);
}

void HistogramCommand(HWND hwnd, HistogramWindowState* state, DWORD command)
{
    if (command >= HistogramCmdFirstChannel && command < HistogramCmdFirstChannel + 5)
        state->Channel = static_cast<ViewerHistogramChannel>(command - HistogramCmdFirstChannel);
    else if (command == HistogramCmdOtherChannels)
        state->ShowOtherChannels = !state->ShowOtherChannels;
    else
        return;
    UpdateHistogramToolbar(state);
    InvalidateRect(hwnd, nullptr, FALSE);
}

bool CreateHistogramToolbar(HWND hwnd, HistogramWindowState* state)
{
    if (HostGui == nullptr)
        return false;
    state->Toolbar = HostGui->CreateToolBar(hwnd);
    if (state->Toolbar == nullptr)
        return false;
    state->Toolbar->SetStyle(TLB_STYLE_IMAGE | TLB_STYLE_TEXT);
    if (!state->Toolbar->CreateWnd(hwnd))
    {
        HostGui->DestroyToolBar(state->Toolbar);
        state->Toolbar = nullptr;
        return false;
    }
    state->Toolbar->SetImageList(state->GrayImages);
    state->Toolbar->SetHotImageList(state->HotImages);

    state->ChannelText = ViewerText(HistogramChannelTextId(state->Channel));
    TLBI_ITEM_INFO2 item = {};
    item.Mask = TLBI_MASK_ID | TLBI_MASK_TEXT | TLBI_MASK_STYLE;
    item.Style = TLBI_STYLE_DROPDOWN | TLBI_STYLE_WHOLEDROPDOWN | TLBI_STYLE_SHOWTEXT;
    item.Text = state->ChannelText.data();
    item.ID = HistogramCmdChannelButton;
    state->Toolbar->InsertItem2(0xFFFFFFFF, TRUE, &item);

    item = {};
    item.Mask = TLBI_MASK_STYLE;
    item.Style = TLBI_STYLE_SEPARATOR;
    state->Toolbar->InsertItem2(0xFFFFFFFF, TRUE, &item);

    item = {};
    item.Mask = TLBI_MASK_IMAGEINDEX | TLBI_MASK_ID | TLBI_MASK_STYLE;
    item.Style = TLBI_STYLE_CHECK;
    item.ImageIndex = IDX_TB_OTHERCHANNELS;
    item.ID = HistogramCmdOtherChannels;
    state->Toolbar->InsertItem2(0xFFFFFFFF, TRUE, &item);
    UpdateHistogramToolbar(state);
    ShowWindow(state->Toolbar->GetHWND(), SW_SHOW);
    return true;
}

void ShowHistogramChannelMenu(HWND hwnd, HistogramWindowState* state, int index)
{
    RECT rect = {};
    state->Toolbar->GetItemRect(index, rect);
    MENU_TEMPLATE_ITEM menu[] = {
        {MNTT_PB, 0, 0, 0, -1, 0, nullptr},
        {MNTT_IT, IDS_HIST_MENU_LUMINOSITY, MenuSkill, HistogramCmdFirstChannel + 0, IDX_TB_FIRSTCHANNEL + 0, 0, nullptr},
        {MNTT_IT, IDS_HIST_MENU_RGBSUM, MenuSkill, HistogramCmdFirstChannel + 4, IDX_TB_FIRSTCHANNEL + 4, 0, nullptr},
        {MNTT_IT, IDS_HIST_MENU_RED, MenuSkill, HistogramCmdFirstChannel + 1, IDX_TB_FIRSTCHANNEL + 1, 0, nullptr},
        {MNTT_IT, IDS_HIST_MENU_GREEN, MenuSkill, HistogramCmdFirstChannel + 2, IDX_TB_FIRSTCHANNEL + 2, 0, nullptr},
        {MNTT_IT, IDS_HIST_MENU_BLUE, MenuSkill, HistogramCmdFirstChannel + 3, IDX_TB_FIRSTCHANNEL + 3, 0, nullptr},
        {MNTT_PE, 0, 0, 0, -1, 0, nullptr},
    };
    CGUIMenuPopupAbstract* popup = HostGui->CreateMenuPopup();
    if (popup == nullptr)
        return;
    if (popup->LoadFromTemplate(ModuleInstance, menu, nullptr, state->HotImages, nullptr))
    {
        const DWORD command = popup->Track(MENU_TRACK_RETURNCMD, rect.left, rect.bottom, hwnd, &rect);
        if (command != 0)
            PostMessageW(hwnd, WM_COMMAND, command, 0);
    }
    HostGui->DestroyMenuPopup(popup);
}

LRESULT CALLBACK HistogramWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    HistogramWindowState* state = reinterpret_cast<HistogramWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        break;
    }

    case WM_CREATE:
        if (state == nullptr || !CreateHistogramToolbar(hwnd, state))
        {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); // the caller still owns the state
            return -1;
        }
        return 0;

    case WM_SIZING:
        if (state != nullptr)
        {
            // The chart stays a whole multiple of 256 pixels wide.
            RECT* rect = reinterpret_cast<RECT*>(lParam);
            const int width = (std::max)(static_cast<int>((rect->right - rect->left - state->FrameWidth + 128) & ~0xFF), 256) + state->FrameWidth;
            if (wParam == WMSZ_LEFT || wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT)
                rect->left = rect->right - width;
            else
                rect->right = rect->left + width;
            return TRUE;
        }
        break;

    case WM_SIZE:
        LayoutHistogramWindow(hwnd, state);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        PaintHistogramWindow(hwnd, state);
        return 0;

    case WM_COMMAND:
        if (state != nullptr)
        {
            HistogramCommand(hwnd, state, LOWORD(wParam));
            return 0;
        }
        break;

    case WM_KEYDOWN:
        if (state != nullptr)
        {
            DWORD command = 0;
            switch (wParam)
            {
            case 'L':
                command = HistogramCmdFirstChannel + static_cast<DWORD>(ViewerHistogramChannel::Luminosity);
                break;
            case 'A':
                command = HistogramCmdFirstChannel + static_cast<DWORD>(ViewerHistogramChannel::Rgb);
                break;
            case 'R':
                command = HistogramCmdFirstChannel + static_cast<DWORD>(ViewerHistogramChannel::Red);
                break;
            case 'G':
                command = HistogramCmdFirstChannel + static_cast<DWORD>(ViewerHistogramChannel::Green);
                break;
            case 'B':
                command = HistogramCmdFirstChannel + static_cast<DWORD>(ViewerHistogramChannel::Blue);
                break;
            case 'O':
                command = HistogramCmdOtherChannels;
                break;
            case VK_ESCAPE:
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            if (command != 0)
            {
                HistogramCommand(hwnd, state, command);
                return 0;
            }
        }
        break;

    case WM_USER_TBGETTOOLTIP:
        if (state != nullptr)
        {
            TOOLBAR_TOOLTIP* tooltip = reinterpret_cast<TOOLBAR_TOOLTIP*>(lParam);
            if (tooltip == nullptr || tooltip->Buffer == nullptr)
                return FALSE;
            const UINT textId = tooltip->ID == HistogramCmdOtherChannels
                                    ? IDS_HIST_TIP_OTHERCHANELS
                                    : IDS_HIST_TIP_LUMINOSITY + static_cast<UINT>(state->Channel);
            std::wstring text = ViewerText(textId);
            if (text.size() >= TOOLTIP_TEXT_MAX)
                text.resize(TOOLTIP_TEXT_MAX - 1);
            std::copy(text.begin(), text.end(), tooltip->Buffer);
            tooltip->Buffer[text.size()] = L'\0';
            return TRUE;
        }
        break;

    case WM_USER_TBDROPDOWN:
        if (state != nullptr && state->Toolbar != nullptr && state->Toolbar->GetHWND() == reinterpret_cast<HWND>(wParam))
        {
            TLBI_ITEM_INFO2 item = {};
            item.Mask = TLBI_MASK_ID;
            if (state->Toolbar->GetItemInfo2(static_cast<DWORD>(lParam), TRUE, &item) && item.ID == HistogramCmdChannelButton)
                ShowHistogramChannelMenu(hwnd, state, static_cast<int>(lParam));
            return 0;
        }
        break;

    case WM_CLOSE:
        // Modal over its viewer, as in the old PictView.
        if (state != nullptr && state->Owner != nullptr)
        {
            EnableWindow(state->Owner, TRUE);
            SetActiveWindow(state->Owner);
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        StoreHistogramPreferencesFromWindow(hwnd, state);
        if (state != nullptr && state->Toolbar != nullptr && HostGui != nullptr)
        {
            HostGui->DestroyToolBar(state->Toolbar);
            state->Toolbar = nullptr;
        }
        return 0;

    case WM_NCDESTROY:
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool EnsureHistogramWindowClass()
{
    if (HistogramClassAtom != 0)
        return true;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = HistogramWindowProc;
    wc.hInstance = ModuleInstance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = HistogramWindowClassName;

    HistogramClassAtom = RegisterClassExW(&wc);
    return HistogramClassAtom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool ShowHistogramWindow(HWND owner, const ImageHistogram& histogram, HIMAGELIST grayImages, HIMAGELIST hotImages)
{
    if (!EnsureHistogramWindowClass())
        return false;

    const ViewerHistogramPreferences preferences = GetHistogramPreferences();
    std::unique_ptr<HistogramWindowState> state = std::make_unique<HistogramWindowState>();
    state->Levels = ComputeHistogramLevels(histogram);
    state->Channel = preferences.Channel;
    state->ShowOtherChannels = preferences.ShowOtherChannels;
    state->Owner = owner;
    state->GrayImages = grayImages;
    state->HotImages = hotImages;

    HistogramWindowState* raw = state.get();
    HWND hwnd = CreateWindowExW(0, HistogramWindowClassName, ViewerText(IDS_HIST_HISTOGRAM),
                                WS_POPUP | WS_CLIPCHILDREN | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, 0, 0, 100, 100, owner, nullptr,
                                ModuleInstance, raw);
    if (hwnd == nullptr)
        return false;
    state.release();

    // The window is sized around the remembered chart size.
    RECT window = {};
    GetWindowRect(hwnd, &window);
    const RECT chart = HistogramChartRect(hwnd, raw);
    raw->FrameWidth = (window.right - window.left) - (chart.right - chart.left);
    const int frameHeight = (window.bottom - window.top) - (chart.bottom - chart.top);
    int chartWidth = preferences.ChartWidth;
    int chartHeight = preferences.ChartHeight;
    if (preferences.WindowX < 0)
    {
        // First use: the old 256 x 119 pixels, scaled to the screen (in whole 256-pixel steps).
        HDC screen = GetDC(owner);
        const UINT dpi = screen != nullptr ? static_cast<UINT>(GetDeviceCaps(screen, LOGPIXELSY)) : 96;
        if (screen != nullptr)
            ReleaseDC(owner, screen);
        if (dpi > 96)
        {
            chartWidth = 256 * static_cast<int>((dpi + 47) / 96);
            chartHeight = MulDiv(chartHeight, static_cast<int>(dpi), 96);
        }
    }
    const int width = chartWidth + raw->FrameWidth;
    const int height = chartHeight + frameHeight;

    RECT ownerRect = {};
    GetWindowRect(owner, &ownerRect);
    RECT rect = {};
    rect.left = preferences.WindowX >= 0 ? preferences.WindowX : (ownerRect.left + ownerRect.right - width) / 2;
    rect.top = preferences.WindowX >= 0 ? preferences.WindowY : (ownerRect.top + ownerRect.bottom - height) / 2;
    rect.right = rect.left + width;
    rect.bottom = rect.top + height;
    if (HostGeneral != nullptr)
        HostGeneral->MultiMonEnsureRectVisible(&rect, FALSE);
    PluginDarkMode_ApplyTitleBar(hwnd);
    SetWindowPos(hwnd, nullptr, rect.left, rect.top, width, height, SWP_NOZORDER | SWP_SHOWWINDOW);
    EnableWindow(owner, FALSE);
    return true;
}

struct ViewerLoadRequest
{
    HWND Target = nullptr;
    std::wstring Path;
    std::shared_ptr<CancellationSource> Cancellation;
    bool ApplyExifOrientation = true;
    LPARAM Sequence = 0; // tells this load's progress from a cancelled one's
};

struct ViewerLoadResult
{
    std::wstring Path;
    std::wstring DisplayName;
    std::wstring ErrorText;
    std::vector<uint8_t> SourceBytes;
    ImageMetadata Metadata;
    ImageSurface Surface;
    bool Success = false;
    bool Canceled = false;
};

DWORD WINAPI ViewerLoadWorkerProc(void* param)
{
    std::unique_ptr<ViewerLoadRequest> request(static_cast<ViewerLoadRequest*>(param));
    std::unique_ptr<ViewerLoadResult> result = std::make_unique<ViewerLoadResult>();
    result->Path = request->Path;
    result->DisplayName = FileNameFromPath(request->Path);

    CancellationToken cancellation = request->Cancellation != nullptr ? request->Cancellation->Token() : CancellationToken();
    if (cancellation.IsCancellationRequested())
    {
        result->Canceled = true;
    }
    else
    {
        ImageEngine engine;
        if (!engine.IsAvailable())
        {
            result->ErrorText = ImageErrorText(engine.InitializationError());
        }
        else
        {
            MemoryLoadResult memory = ReadFileToMemoryBuffer(request->Path, cancellation);
            if (!memory.Succeeded())
            {
                result->Canceled = memory.Hr == HRESULT_FROM_WIN32(ERROR_CANCELLED);
                if (!result->Canceled)
                    result->ErrorText = memory.Message;
            }
            else
            {
                result->SourceBytes = std::move(memory.Bytes);
                StreamLoadResult stream = CreateMemoryStreamFromBytes(result->SourceBytes);
                if (!stream.Succeeded())
                {
                    result->ErrorText = stream.Message;
                }
                else
                {
                    OpenDocumentResult open = engine.OpenStream(stream.Stream.Get(), cancellation);
                    if (!open.Succeeded())
                    {
                        result->Canceled = open.Error.Code == ImageErrorCode::Canceled;
                        if (!result->Canceled)
                            result->ErrorText = ImageErrorText(open.Error);
                    }
                    else
                    {
                        result->Metadata = open.Document->Metadata();
                        DecodeOptions decodeOptions;
                        decodeOptions.ApplyOrientation = request->ApplyExifOrientation;
                        decodeOptions.KeepPaletteIndices = true; // the pipette shows the index
                        decodeOptions.Progress = [target = request->Target, sequence = request->Sequence](uint32_t percent) {
                            PostMessageW(target, WM_PICTVIEW_LOAD_PROGRESS, percent, sequence);
                        };
                        DecodeFrameResult decoded = open.Document->DecodeFrame(0, decodeOptions, cancellation);
                        if (!decoded.Succeeded())
                        {
                            result->Canceled = decoded.Error.Code == ImageErrorCode::Canceled;
                            if (!result->Canceled)
                                result->ErrorText = ImageErrorText(decoded.Error);
                        }
                        else
                        {
                            result->Surface = std::move(decoded.Surface);
                            result->Success = true;
                        }
                    }
                }
            }
        }
    }

    HWND target = request->Target;
    if (target != nullptr && IsWindow(target) &&
        PostMessageW(target, WM_PICTVIEW_LOAD_COMPLETE, 0, reinterpret_cast<LPARAM>(result.get())))
    {
        result.release();
    }
    return 0;
}

class ViewerWindow
{
public:
    ViewerWindow()
    {
        const ViewerPreferences preferences = GetViewerPreferences();
        m_toolbarVisible = preferences.ToolbarVisible;
        m_statusVisible = preferences.StatusbarVisible;
        m_pipetteInHex = preferences.PipetteInHex;
        m_showFullPathInTitle = preferences.ShowFullPathInTitle;
        m_applyExifOrientation = preferences.AutoRotate;
        m_captureCursor = preferences.CaptureCursor;
        m_captureScope = preferences.CaptureScope;
        m_captureTrigger = preferences.CaptureTrigger;
        m_captureHotKey = SanitizeCaptureHotKey(preferences.CaptureHotKey);
        m_captureTimerSeconds = SanitizeCaptureTimerSeconds(preferences.CaptureTimerSeconds);
        m_zoomMode = ZoomModeFromPreference(preferences.DefaultZoomMode);
        m_initialFullScreenPending = preferences.DefaultZoomMode == ViewerDefaultZoomMode::FullScreen;
        m_initialWindowSizeMode = preferences.WindowSizeMode;
        m_backgroundColor = preferences.BackgroundColor;
        m_fullScreenBackgroundColor = preferences.FullScreenBackgroundColor;
        m_transparentColor = preferences.TransparentColor;
        m_fullScreenTransparentColor = preferences.FullScreenTransparentColor;
    }

    ~ViewerWindow() = default;

    HWND Hwnd() const { return m_hwnd; }

    HANDLE GetLock()
    {
        if (m_lock == nullptr)
            m_lock = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        return m_lock;
    }

    void CloseLockOnFailure()
    {
        if (m_lock != nullptr)
        {
            CloseHandle(m_lock);
            m_lock = nullptr;
        }
    }

    void SetPendingLoadPath(std::wstring path)
    {
        m_pendingLoadPath = std::move(path);
        m_pendingLoadPreserveImage = false;
        m_pendingLoadClearsSourceNavigation = false;
        m_hasPendingSourceFile = false;
        m_displayName = FileNameFromPath(m_pendingLoadPath);
        m_loading = true;
    }

    void SetSourceNavigation(ViewerSourceNavigation sourceNavigation)
    {
        m_sourceNavigation = std::move(sourceNavigation);
    }

    void SetHostActions(ViewerHostActions hostActions)
    {
        m_hostActions = hostActions;
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        ViewerWindow* window = nullptr;
        if (msg == WM_NCCREATE)
        {
            CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            window = static_cast<ViewerWindow*>(create->lpCreateParams);
            if (window == nullptr)
                return FALSE;
            window->m_hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
            ViewerWindows.Add(hwnd);
        }
        else
        {
            window = reinterpret_cast<ViewerWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (window != nullptr)
            return window->WindowProc(msg, wParam, lParam);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

private:
    LRESULT WindowProc(UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
            if (!CreateChildControls())
                return -1;
            UpdateTitle();
            UpdateStatus();
            UpdateCommands();
            return 0;

        case WM_PICTVIEW_LOAD_IMAGE:
            LoadPendingImage();
            return 0;

        case WM_PICTVIEW_LOAD_PROGRESS:
            if (m_loading && lParam == m_loadSequence && wParam != m_loadPercent)
            {
                m_loadPercent = static_cast<UINT>(wParam);
                UpdateStatus();
            }
            return 0;

        case WM_PICTVIEW_LOAD_COMPLETE:
            CompletePendingImageLoad(reinterpret_cast<ViewerLoadResult*>(lParam));
            return 0;

        case WM_HSCROLL:
        case WM_VSCROLL:
            OnScroll(msg == WM_HSCROLL, LOWORD(wParam));
            return 0;

        case WM_SIZE:
            if (m_capturePending && wParam != SIZE_MINIMIZED)
                CancelPendingScreenCapture(); // the user brought the viewer back, as before
            Layout();
            InvalidateRect(m_hwnd, nullptr, TRUE);
            return 0;

        case WM_PAINT:
            Paint();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_SETCURSOR:
            if (HandleSetCursor(lParam))
                return TRUE;
            break;

        case WM_COMMAND:
            HandleCommand(LOWORD(wParam));
            return 0;

        case WM_NOTIFY:
            if (HandleNotify(reinterpret_cast<NMHDR*>(lParam)))
                return 0;
            break;

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE && HostGeneral != nullptr)
                HostGeneral->SkipOneActivateRefresh(); // leaving the viewer does not refresh Sally's panels
            UpdateCommands();
            break;

        case WM_SETFOCUS:
            break;

        case WM_SETTINGCHANGE:
            if (PluginDarkMode_OnSettingChange(lParam))
            {
                PluginDarkMode_ApplyTitleBar(m_hwnd);
                PluginDarkMode_ApplyListTreeThemeRecursive(m_hScroll);
                PluginDarkMode_ApplyListTreeThemeRecursive(m_vScroll);
                InvalidateRect(m_hwnd, nullptr, TRUE);
            }
            break;

        case WM_USER_TBGETTOOLTIP:
            return HandleToolbarTooltip(reinterpret_cast<TOOLBAR_TOOLTIP*>(lParam)) ? TRUE : FALSE;

        case WM_USER_INITMENUPOPUP:
            HandleInitMenuPopup(reinterpret_cast<CGUIMenuPopupAbstract*>(wParam), HIWORD(lParam));
            return 0;

        case WM_USER_TBDROPDOWN:
            HandleToolbarDropDown(reinterpret_cast<HWND>(wParam), static_cast<int>(lParam));
            return 0;

        case WM_CONTEXTMENU:
            ShowContextMenu(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_LBUTTONDOWN:
            HandleLeftButtonDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_MOUSEMOVE:
            if (m_fullScreen)
                RestartFullScreenCursorTimer(lParam);
            if (m_tool == ViewerTool::Pipette && !m_selecting && !m_panning)
            {
                UpdatePipetteSample(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                return 0;
            }

            if (m_selecting)
            {
                ContinueSelection(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                return 0;
            }

            if (m_panning)
            {
                ContinuePan(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                return 0;
            }
            UpdateStatusBarDetails();
            break;

        case WM_LBUTTONUP:
            if (m_zoomSelecting)
            {
                EndZoomSelection(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
                return 0;
            }

            if (m_selecting)
            {
                EndSelection();
                return 0;
            }

            if (m_panning)
            {
                EndPan();
                return 0;
            }
            break;

        case WM_CAPTURECHANGED:
            m_panning = false;
            m_selecting = false;
            m_constrainSelectionRatio = false;
            m_zoomSelecting = false;
            break;

        case WM_KEYDOWN:
            if (HandleKeyDown(wParam))
                return 0;
            break;

        case WM_MOUSEWHEEL:
            if (HandleMouseWheel(wParam))
                return 0;
            break;

        case WM_MOUSEHWHEEL:
            PanByWheel(-GET_WHEEL_DELTA_WPARAM(wParam), true);
            return 0;

        case WM_LBUTTONDBLCLK:
            // Double-click toggles full screen (Hand or Pick Color, no modifier keys).
            if ((m_tool == ViewerTool::Hand || m_tool == ViewerTool::Pipette) && (wParam & (MK_CONTROL | MK_SHIFT)) == 0 &&
                !m_surface.Pixels.empty())
            {
                HandleCommand(CmdFullScreen);
                return 0;
            }
            HandleLeftButtonDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;

        case WM_DROPFILES:
            OpenDroppedFile(reinterpret_cast<HDROP>(wParam));
            return 0;

        case WM_TIMER:
            if (wParam == AnimationTimerId)
            {
                AdvanceAnimationFrame();
                return 0;
            }
            if (wParam == CursorTimerId)
            {
                KillTimer(m_hwnd, CursorTimerId);
                if (m_fullScreen && GetCapture() != m_hwnd)
                {
                    m_cursorHidden = true;
                    SetCursor(nullptr);
                }
                return 0;
            }
            if (wParam == EdgeScrollTimerId)
            {
                EdgeScrollTick();
                return 0;
            }
            if (wParam == CaptureTimerId)
            {
                CompletePendingScreenCapture();
                return 0;
            }
            break;

        case WM_HOTKEY:
            if (wParam == static_cast<WPARAM>(CaptureHotKeyId))
            {
                CompletePendingScreenCapture();
                return 0;
            }
            break;

        case WM_PICTVIEW_REFRESH_MAIN_MENU:
            RefreshMainMenu();
            return 0;

        case WM_PICTVIEW_HOST_EVENT:
            HandleHostEvent(static_cast<ViewerHostEvent>(wParam));
            return 0;

        case WM_CLOSE:
            DestroyWindow(m_hwnd);
            return 0;

        case WM_DESTROY:
            DestroyFrame();
            StopAnimation();
            StopPendingScreenCapture();
            CancelPendingImageLoad();
            SignalLock();
            ViewerWindows.Remove(m_hwnd);
            PostQuitMessage(0);
            return 0;

        case WM_NCDESTROY:
            SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, 0);
            m_hwnd = nullptr;
            break;
        }

        return DefWindowProcW(m_hwnd, msg, wParam, lParam);
    }

    bool HandleNotify(NMHDR* header)
    {
        if (header != nullptr && header->hwndFrom == m_rebar && header->code == RBN_AUTOSIZE)
        {
            Layout();
            InvalidateRect(m_hwnd, nullptr, TRUE);
            return true;
        }
        return false;
    }

    // The frame is Sally's: its menu bar and tool bar sit in a rebar above the image, the menus
    // come from PictView's templates and every item is enabled through m_enablers.
    bool CreateChildControls()
    {
        DragAcceptFiles(m_hwnd, TRUE); // an image dropped on the viewer opens in it
        m_hScroll = CreateWindowExW(0, L"SCROLLBAR", nullptr, WS_CHILD | SBS_HORZ, 0, 0, 0, 0, m_hwnd, nullptr, ModuleInstance, nullptr);
        m_vScroll = CreateWindowExW(0, L"SCROLLBAR", nullptr, WS_CHILD | SBS_VERT, 0, 0, 0, 0, m_hwnd, nullptr, ModuleInstance, nullptr);
        PluginDarkMode_ApplyListTreeThemeRecursive(m_hScroll);
        PluginDarkMode_ApplyListTreeThemeRecursive(m_vScroll);
        if (HostGui == nullptr)
            return false;

        if (!CreateToolbarImageLists())
            return false;

        m_mainMenu = HostGui->CreateMenuPopup();
        if (m_mainMenu == nullptr ||
            !m_mainMenu->LoadFromTemplate(ModuleInstance, MainMenuTemplate, m_enablers, m_grayImages, m_hotImages))
            return false;

        m_menuBar = HostGui->CreateMenuBar(m_mainMenu, m_hwnd);
        if (m_menuBar == nullptr)
            return false;

        RECT client = {};
        GetClientRect(m_hwnd, &client);
        m_rebar = CreateWindowExW(WS_EX_TOOLWINDOW,
                                  REBARCLASSNAMEW,
                                  L"",
                                  WS_VISIBLE | WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
                                      RBS_VARHEIGHT | CCS_NODIVIDER | RBS_BANDBORDERS | CCS_NOPARENTALIGN | RBS_AUTOSIZE,
                                  0,
                                  0,
                                  client.right,
                                  client.bottom,
                                  m_hwnd,
                                  nullptr,
                                  ModuleInstance,
                                  nullptr);
        if (m_rebar == nullptr)
            return false;
        HostGui->DisableWindowVisualStyles(m_rebar);

        if (!m_menuBar->CreateWnd(m_rebar))
            return false;
        InsertRebarBand(m_menuBar->GetHWND(), RebarBandMenu, m_menuBar->GetNeededWidth(), m_menuBar->GetNeededHeight(), 0, false);

        if (m_toolbarVisible)
            CreateToolbar();

        m_status = CreateWindowExW(0,
                                   STATUSCLASSNAMEW,
                                   nullptr,
                                   WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SBARS_SIZEGRIP,
                                   0,
                                   0,
                                   0,
                                   0,
                                   m_hwnd,
                                   nullptr,
                                   ModuleInstance,
                                   nullptr);
        if (m_status == nullptr)
            return false;
        if (!m_statusVisible)
            ShowWindow(m_status, SW_HIDE);

        PluginDarkMode_ApplyTitleBar(m_hwnd);
        Layout();
        return true;
    }

    bool CreateToolbarImageLists()
    {
        const bool use256 = HostGeneral == nullptr || HostGeneral->CanUse256ColorsBitmap();
        HBITMAP color = static_cast<HBITMAP>(LoadImageW(ModuleInstance,
                                                        MAKEINTRESOURCEW(use256 ? IDB_TOOLBAR256 : IDB_TOOLBAR16),
                                                        IMAGE_BITMAP,
                                                        0,
                                                        0,
                                                        LR_DEFAULTCOLOR));
        if (color == nullptr)
            return false;

        HBITMAP gray = nullptr;
        HBITMAP mask = nullptr;
        const BOOL converted = HostGui->CreateGrayscaleAndMaskBitmaps(color, RGB(255, 0, 255), gray, mask);
        m_hotImages = ImageList_Create(16, 16, ILC_MASK | ILC_COLORDDB, IDX_TB_COUNT, 1);
        m_grayImages = ImageList_Create(16, 16, ILC_MASK | ILC_COLORDDB, IDX_TB_COUNT, 1);
        if (converted && m_hotImages != nullptr && m_grayImages != nullptr)
        {
            ImageList_Add(m_hotImages, color, mask);
            ImageList_Add(m_grayImages, gray, mask);
        }
        if (gray != nullptr)
            DeleteObject(gray);
        if (mask != nullptr)
            DeleteObject(mask);
        DeleteObject(color);
        return converted && m_hotImages != nullptr && m_grayImages != nullptr;
    }

    // The menu bar is the first band; the tool bar starts its own row below it (RBBS_BREAK)
    // and takes the full width.
    void InsertRebarBand(HWND child, UINT id, int width, int height, int position, bool newRow)
    {
        REBARBANDINFOW band = {};
        band.cbSize = sizeof(band);
        band.fMask = RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_STYLE | RBBIM_ID | (newRow ? RBBIM_SIZE : 0);
        band.cxMinChild = width;
        band.cyMinChild = height;
        band.cx = newRow ? 10000 : 0;
        band.fStyle = RBBS_NOGRIPPER | (newRow ? RBBS_BREAK : 0);
        band.hwndChild = child;
        band.wID = id;
        SendMessageW(m_rebar, RB_INSERTBANDW, static_cast<WPARAM>(position), reinterpret_cast<LPARAM>(&band));
    }

    void CreateToolbar()
    {
        if (m_toolbar != nullptr || HostGui == nullptr || m_rebar == nullptr)
            return;

        m_toolbar = HostGui->CreateToolBar(m_hwnd);
        if (m_toolbar == nullptr)
            return;
        m_toolbar->SetStyle(TLB_STYLE_IMAGE | TLB_STYLE_TEXT);
        if (!m_toolbar->CreateWnd(m_rebar))
        {
            HostGui->DestroyToolBar(m_toolbar);
            m_toolbar = nullptr;
            return;
        }
        m_toolbar->SetImageList(m_grayImages);
        m_toolbar->SetHotImageList(m_hotImages);

        for (const ToolbarButton& button : ToolbarButtons)
        {
            if (button.ImageIndex == IDX_TB_TERMINATOR)
                break;

            TLBI_ITEM_INFO2 item = {};
            if (button.ImageIndex == IDX_TB_SEPARATOR)
            {
                item.Mask = TLBI_MASK_STYLE;
                item.Style = TLBI_STYLE_SEPARATOR;
            }
            else if (button.ImageIndex == IDX_TB_ZOOMNUMBER)
            {
                item.Mask = TLBI_MASK_ID | TLBI_MASK_TEXT | TLBI_MASK_STYLE | TLBI_MASK_ENABLER;
                item.Style = TLBI_STYLE_DROPDOWN | TLBI_STYLE_WHOLEDROPDOWN | TLBI_STYLE_SHOWTEXT;
                item.Text = m_toolbarZoomText.data();
                item.ID = static_cast<DWORD>(button.Command);
                item.Enabler = m_enablers + button.Enabler;
            }
            else
            {
                item.Mask = TLBI_MASK_IMAGEINDEX | TLBI_MASK_ID | TLBI_MASK_ENABLER | TLBI_MASK_STYLE;
                item.Style = IsViewerToolCommand(button.Command) ? TLBI_STYLE_RADIO
                             : button.Command == CmdToggleSourceSelection ? TLBI_STYLE_CHECK
                                                                          : 0;
                item.ImageIndex = button.ImageIndex;
                item.ID = static_cast<DWORD>(button.Command);
                item.Enabler = button.Enabler == vweAlwaysEnabled ? nullptr : m_enablers + button.Enabler;
            }
            m_toolbar->InsertItem2(0xFFFFFFFF, TRUE, &item);
        }

        InsertRebarBand(m_toolbar->GetHWND(), RebarBandToolbar, m_toolbar->GetNeededWidth(), m_toolbar->GetNeededHeight(), 1, true);
        m_toolbar->UpdateItemsState();
        UpdateToolbarChecks();
    }

    void DestroyToolbar()
    {
        if (m_toolbar == nullptr)
            return;

        const LRESULT index = SendMessageW(m_rebar, RB_IDTOINDEX, RebarBandToolbar, 0);
        if (index >= 0)
            SendMessageW(m_rebar, RB_DELETEBAND, static_cast<WPARAM>(index), 0);
        HostGui->DestroyToolBar(m_toolbar);
        m_toolbar = nullptr;
    }

    void DestroyFrame()
    {
        if (HostGui != nullptr)
        {
            if (m_menuBar != nullptr)
                HostGui->DestroyMenuBar(m_menuBar);
            if (m_mainMenu != nullptr)
                HostGui->DestroyMenuPopup(m_mainMenu);
            if (m_toolbar != nullptr)
                HostGui->DestroyToolBar(m_toolbar);
        }
        m_menuBar = nullptr;
        m_mainMenu = nullptr;
        m_toolbar = nullptr;
        if (m_hotImages != nullptr)
            ImageList_Destroy(m_hotImages);
        if (m_grayImages != nullptr)
            ImageList_Destroy(m_grayImages);
        m_hotImages = nullptr;
        m_grayImages = nullptr;
    }

    bool HandleToolbarTooltip(TOOLBAR_TOOLTIP* tooltip)
    {
        if (tooltip == nullptr || tooltip->Buffer == nullptr)
            return false;

        for (const ToolbarButton& button : ToolbarButtons)
        {
            if (button.ImageIndex == IDX_TB_TERMINATOR)
                break;
            if (button.ImageIndex == IDX_TB_SEPARATOR || static_cast<DWORD>(button.Command) != tooltip->ID)
                continue;

            std::wstring text = ViewerText(static_cast<UINT>(button.ToolTipResID));
            if (text.size() >= TOOLTIP_TEXT_MAX)
                text.resize(TOOLTIP_TEXT_MAX - 1);
            std::copy(text.begin(), text.end(), tooltip->Buffer);
            tooltip->Buffer[text.size()] = L'\0';
            SPLPrepareToolTipTextForAbiBuffer(HostGui, tooltip->Buffer, TOOLTIP_TEXT_MAX, FALSE);
            return true;
        }
        return false;
    }

    void FillRecentHistoryMenu(CGUIMenuPopupAbstract* popup, const std::array<std::wstring, ViewerRecentHistoryEntryCount>& entries,
                               int firstCommand)
    {
        popup->RemoveAllItems();

        MENU_ITEM_INFO item = {};
        item.Mask = MENU_MASK_TYPE | MENU_MASK_ID | MENU_MASK_STRING | MENU_MASK_STATE;
        item.Type = MENU_TYPE_STRING;

        bool any = false;
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            if (entries[i].empty())
                continue;
            std::wstring text = L"&";
            text += std::to_wstring(i < 9 ? i + 1 : 0);
            text += L" ";
            text += entries[i];
            item.String = text.data();
            item.ID = static_cast<DWORD>(firstCommand + static_cast<int>(i));
            item.State = 0;
            popup->InsertItem(0xFFFFFFFF, TRUE, &item);
            any = true;
        }
        if (!any)
        {
            std::wstring empty = ViewerText(IDS_EMPTY);
            item.String = empty.data();
            item.ID = static_cast<DWORD>(firstCommand);
            item.State = MENU_STATE_GRAYED;
            popup->InsertItem(0xFFFFFFFF, TRUE, &item);
        }
    }

    void HandleInitMenuPopup(CGUIMenuPopupAbstract* popup, WORD popupId)
    {
        UpdateCommands();
        if (popup == nullptr)
            return;

        switch (popupId)
        {
        case CML_RECENTFILES:
            FillRecentHistoryMenu(popup, GetRecentHistory().Files, CmdRecentFileFirst);
            break;

        case CML_RECENTDIRS:
            FillRecentHistoryMenu(popup, GetRecentHistory().Directories, CmdRecentDirectoryFirst);
            break;

        case CML_FILE:
            popup->CheckItem(CmdToggleSourceSelection, FALSE, m_sourceFileSelected ? TRUE : FALSE);
            break;

        case CML_VIEW:
            popup->CheckItem(CmdToggleToolbar, FALSE, m_toolbar != nullptr ? TRUE : FALSE);
            popup->CheckItem(CmdToggleStatusbar, FALSE, m_statusVisible ? TRUE : FALSE);
            popup->CheckItem(CmdFullScreen, FALSE, m_fullScreen ? TRUE : FALSE);
            break;

        case CML_TOOLS:
            popup->CheckItem(CmdToolHand, FALSE, m_tool == ViewerTool::Hand ? TRUE : FALSE);
            popup->CheckItem(CmdToolZoom, FALSE, m_tool == ViewerTool::Zoom ? TRUE : FALSE);
            popup->CheckItem(CmdToolSelect, FALSE, m_tool == ViewerTool::Select ? TRUE : FALSE);
            popup->CheckItem(CmdTogglePipette, FALSE, m_tool == ViewerTool::Pipette ? TRUE : FALSE);
            break;

        case CML_CONTEXT:
            popup->CheckItem(CmdFullScreen, FALSE, m_fullScreen ? TRUE : FALSE);
            popup->CheckItem(CmdToggleSourceSelection, FALSE, m_sourceFileSelected ? TRUE : FALSE);
            break;
        }
    }

    void HandleToolbarDropDown(HWND toolbar, int index)
    {
        if (m_toolbar == nullptr || HostGui == nullptr || m_toolbar->GetHWND() != toolbar)
            return;

        TLBI_ITEM_INFO2 info = {};
        info.Mask = TLBI_MASK_ID;
        if (!m_toolbar->GetItemInfo2(static_cast<DWORD>(index), TRUE, &info) || info.ID != static_cast<DWORD>(CmdZoomTo))
            return;

        RECT rect = {};
        m_toolbar->GetItemRect(index, rect);

        CGUIMenuPopupAbstract* popup = HostGui->CreateMenuPopup();
        if (popup == nullptr)
            return;

        MENU_ITEM_INFO item = {};
        item.Mask = MENU_MASK_TYPE | MENU_MASK_ID | MENU_MASK_STRING;
        item.Type = MENU_TYPE_STRING;
        for (std::size_t i = 0; i < std::size(ZoomPresets); ++i)
        {
            std::wstring text = FormatZoomPresetPercent(ZoomPresets[i]);
            item.String = text.data();
            item.ID = static_cast<DWORD>(i + 1);
            popup->InsertItem(0xFFFFFFFF, TRUE, &item);
        }
        const DWORD choice = popup->Track(MENU_TRACK_RETURNCMD, rect.left, rect.bottom, m_hwnd, &rect);
        HostGui->DestroyMenuPopup(popup);
        if (choice >= 1 && choice <= std::size(ZoomPresets))
            SetManualZoomPercent(static_cast<double>(ZoomPresets[choice - 1]) / 100.0);
    }

    static std::wstring FormatZoomPresetPercent(int hundredths)
    {
        std::wstring text = std::to_wstring(hundredths / 100);
        const int fraction = hundredths % 100;
        if (fraction != 0)
        {
            text += L".";
            text += fraction % 10 == 0 ? std::to_wstring(fraction / 10) : (fraction < 10 ? L"0" : L"") + std::to_wstring(fraction);
        }
        text += L"%";
        return text;
    }

    void RefreshMainMenu()
    {
        UpdateCommands();
    }

    void HandleHostEvent(ViewerHostEvent event)
    {
        switch (event)
        {
        case ViewerHostEvent::ColorsChanged:
            if (m_toolbar != nullptr)
                m_toolbar->OnColorsChanged();
            if (m_status != nullptr)
                SendMessageW(m_status, WM_SYSCOLORCHANGE, 0, 0);
            RefreshMainMenu();
            Layout();
            UpdateStatus();
            UpdateCommands();
            InvalidateRect(m_hwnd, nullptr, TRUE);
            break;

        case ViewerHostEvent::SettingsChanged:
            if (m_menuBar != nullptr)
                m_menuBar->SetFont();
            if (m_toolbar != nullptr)
                m_toolbar->SetFont();
            if (m_status != nullptr)
                SendMessageW(m_status, WM_SETTINGCHANGE, 0, 0);
            RefreshMainMenu();
            Layout();
            UpdateStatus();
            UpdateCommands();
            InvalidateRect(m_hwnd, nullptr, TRUE);
            break;

        case ViewerHostEvent::ConfigurationChanged:
        {
            ApplyPreferences(GetViewerPreferences());
            UpdateTitle();
            UpdateStatus();
            RefreshMainMenu();
            UpdateCommands();
            InvalidateRect(m_hwnd, nullptr, TRUE);
            break;
        }
        }
    }

    void Layout()
    {
        RECT client = {};
        GetClientRect(m_hwnd, &client);

        int toolbarHeight = 0;
        if (m_rebar != nullptr && IsWindowVisible(m_rebar))
        {
            RECT rebarRect = {};
            GetWindowRect(m_rebar, &rebarRect);
            toolbarHeight = rebarRect.bottom - rebarRect.top;
            // +4: the rebar leaves its last pixels unpainted when the window widens.
            MoveWindow(m_rebar, 0, 0, client.right - client.left + 4, toolbarHeight, TRUE);
        }

        int statusHeight = 0;
        if (m_status != nullptr && m_statusVisible)
        {
            SendMessageW(m_status, WM_SIZE, 0, 0);
            RECT statusRect = {};
            GetWindowRect(m_status, &statusRect);
            statusHeight = statusRect.bottom - statusRect.top;
        }
        SetupStatusBarParts();

        m_imageArea.left = client.left;
        m_imageArea.top = client.top + toolbarHeight;
        m_imageArea.right = client.right;
        m_imageArea.bottom = client.bottom - statusHeight;
        if (m_imageArea.bottom < m_imageArea.top)
            m_imageArea.bottom = m_imageArea.top;
        LayoutScrollBars();
    }

    // Shows a scrollbar for each axis along which the image is larger than the view; the image
    // gets the rest of the area. Returns whether the image area changed.
    bool LayoutScrollBars()
    {
        const RECT before = m_content;
        const int barWidth = GetSystemMetrics(SM_CXVSCROLL);
        const int barHeight = GetSystemMetrics(SM_CYHSCROLL);
        bool horizontal = false;
        bool vertical = false;
        for (int pass = 0; pass < 3; ++pass) // a bar on one axis can make the other one needed
        {
            m_content = m_imageArea;
            if (vertical)
                m_content.right = std::max(m_content.left, m_content.right - barWidth);
            if (horizontal)
                m_content.bottom = std::max(m_content.top, m_content.bottom - barHeight);
            if (m_surface.Pixels.empty())
                break;
            long long drawWidth = 0;
            long long drawHeight = 0;
            GetDrawSize(drawWidth, drawHeight);
            const bool needHorizontal = drawWidth > RectWidth(m_content);
            const bool needVertical = drawHeight > RectHeight(m_content);
            if (needHorizontal == horizontal && needVertical == vertical)
                break;
            horizontal = horizontal || needHorizontal;
            vertical = vertical || needVertical;
        }
        ClampPanOffsets();
        PlaceScrollBar(m_hScroll, horizontal, m_content.left, m_content.bottom, RectWidth(m_content), barHeight);
        PlaceScrollBar(m_vScroll, vertical, m_content.right, m_content.top, barWidth, RectHeight(m_content));
        SyncScrollBars();
        return !EqualRect(&before, &m_content);
    }

    void PlaceScrollBar(HWND bar, bool visible, int x, int y, int width, int height)
    {
        if (bar == nullptr)
            return;
        if (visible)
            SetWindowPos(bar, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        else if (IsWindowVisible(bar))
            ShowWindow(bar, SW_HIDE);
    }

    void SyncScrollBars()
    {
        long long drawWidth = 0;
        long long drawHeight = 0;
        if (!m_surface.Pixels.empty())
            GetDrawSize(drawWidth, drawHeight);
        auto sync = [](HWND bar, const ViewerScrollAxis& axis) {
            if (bar == nullptr || !axis.Visible)
                return;
            SCROLLINFO info = {sizeof(info)};
            info.fMask = SIF_ALL | SIF_DISABLENOSCROLL;
            info.nMax = axis.Maximum;
            info.nPage = static_cast<UINT>(axis.Page);
            info.nPos = axis.Position;
            SetScrollInfo(bar, SB_CTL, &info, TRUE);
        };
        sync(m_hScroll, ScrollAxisFromPan(drawWidth, RectWidth(m_content), m_panX));
        sync(m_vScroll, ScrollAxisFromPan(drawHeight, RectHeight(m_content), m_panY));
    }

    void OnScroll(bool horizontal, int request)
    {
        HWND bar = horizontal ? m_hScroll : m_vScroll;
        if (bar == nullptr || m_surface.Pixels.empty())
            return;
        long long drawWidth = 0;
        long long drawHeight = 0;
        GetDrawSize(drawWidth, drawHeight);
        const long long drawSize = horizontal ? drawWidth : drawHeight;
        const int viewSize = horizontal ? RectWidth(m_content) : RectHeight(m_content);
        int& pan = horizontal ? m_panX : m_panY;
        SCROLLINFO info = {sizeof(info)};
        info.fMask = SIF_TRACKPOS; // 32-bit thumb position (the message carries only 16 bits)
        GetScrollInfo(bar, SB_CTL, &info);
        const ViewerScrollAxis axis = ScrollAxisFromPan(drawSize, viewSize, pan);
        const int position = ScrollPositionForRequest(request, axis.Position, info.nTrackPos, drawSize, viewSize);
        const int newPan = PanFromScrollPosition(drawSize, viewSize, position);
        if (newPan == pan)
            return;
        pan = newPan;
        ClampPanOffsets();
        SyncScrollBars();
        InvalidateRect(m_hwnd, &m_content, FALSE);
    }

    int MeasureStatusTextWidth(const wchar_t* text) const
    {
        if (m_status == nullptr || text == nullptr)
            return 0;

        HDC dc = GetDC(m_status);
        if (dc == nullptr)
            return 0;

        HFONT font = reinterpret_cast<HFONT>(SendMessageW(m_status, WM_GETFONT, 0, 0));
        HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;
        SIZE size = {};
        GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
        if (oldFont != nullptr)
            SelectObject(dc, oldFont);
        ReleaseDC(m_status, dc);
        return size.cx;
    }

    void SetupStatusBarParts()
    {
        if (m_status == nullptr)
            return;

        RECT rect = {};
        GetClientRect(m_status, &rect);
        const int width = std::max<LONG>(0, rect.right - rect.left);
        const int cursorWidth = StatusIconMargin + std::max(MeasureStatusTextWidth(L"99999, 99999"), 80);
        const int sizeWidth = StatusIconMargin + std::max(MeasureStatusTextWidth(L"99999 x 99999"), 78);
        const int sampleWidth = StatusIconMargin + std::max(MeasureStatusTextWidth(FormatPipetteSample(99999, 99999, 255, 255, 255, false).c_str()), 134) + GetSystemMetrics(SM_CXVSCROLL);

        int parts[StatusPartCount] = {};
        parts[StatusPartMessage] = std::max(0, width - cursorWidth - sizeWidth - sampleWidth);
        parts[StatusPartCursor] = std::min(width, parts[StatusPartMessage] + cursorWidth);
        parts[StatusPartSize] = std::min(width, parts[StatusPartCursor] + sizeWidth);
        parts[StatusPartSample] = -1;
        SendMessageW(m_status, SB_SETPARTS, StatusPartCount, reinterpret_cast<LPARAM>(parts));
    }

    void SetStatusPartText(int part, const std::wstring& text)
    {
        if (m_status != nullptr)
            SendMessageW(m_status, SB_SETTEXTW, static_cast<WPARAM>(part), reinterpret_cast<LPARAM>(text.c_str()));
    }

    void SetStatusPartIcon(int part, int resourceId)
    {
        if (m_status != nullptr)
            SendMessageW(m_status, SB_SETICON, static_cast<WPARAM>(part), reinterpret_cast<LPARAM>(LoadViewerStatusIcon(resourceId)));
    }

    void UpdateStatusBarDetails()
    {
        if (m_status == nullptr)
            return;

        SetStatusPartIcon(StatusPartCursor, IDI_SB_CURSOR);
        SetStatusPartIcon(StatusPartSize, IDI_SB_SIZE);

        std::wstring cursorText;
        std::wstring sizeText;
        std::wstring sampleText;
        const bool hasSelection = HasCropSelection();
        SetStatusPartIcon(StatusPartSample, hasSelection ? IDI_SB_ANCHOR : IDI_SB_PIPETTE);

        if (!m_surface.Pixels.empty())
        {
            if (hasSelection)
            {
                sizeText = FormatViewerText(IDS_SB_WH, static_cast<int>(m_selectionImage.right - m_selectionImage.left),
                                            static_cast<int>(m_selectionImage.bottom - m_selectionImage.top));
                sampleText = FormatViewerText(IDS_SB_XY, static_cast<int>(m_selectionImage.left),
                                              static_cast<int>(m_selectionImage.top));
            }
            else
            {
                sizeText = FormatViewerText(IDS_SB_WH, static_cast<int>(m_surface.Width), static_cast<int>(m_surface.Height));
            }

            POINT cursor = {};
            if (GetCursorPos(&cursor) && ScreenToClient(m_hwnd, &cursor))
            {
                PipetteSample sample;
                if (SampleImagePixel(cursor.x, cursor.y, sample))
                {
                    cursorText = FormatViewerText(IDS_SB_XY, sample.X, sample.Y);
                    if (!hasSelection)
                        sampleText = FormatViewerText(IDS_SB_RGB, static_cast<int>(sample.Red), static_cast<int>(sample.Green),
                                                      static_cast<int>(sample.Blue));
                }
            }
        }

        SetStatusPartText(StatusPartCursor, cursorText);
        SetStatusPartText(StatusPartSize, sizeText);
        SetStatusPartText(StatusPartSample, sampleText);
    }

    bool DecodeCurrentFrame(std::wstring& errorText)
    {
        if (!EnsureDocumentOpen(errorText))
            return false;

        DecodeOptions options;
        options.ApplyOrientation = m_applyExifOrientation;
        options.KeepPaletteIndices = true;
        DecodeFrameResult decoded = m_document->DecodeFrame(m_currentFrame, options);
        if (!decoded.Succeeded())
        {
            errorText = ImageErrorText(decoded.Error);
            return false;
        }

        m_surface = std::move(decoded.Surface);
        m_surfaceModified = false;
        m_quarterTurned = QuarterTurnedByOrientation(CurrentFrameInfo(), m_applyExifOrientation);
        m_panX = 0;
        m_panY = 0;
        ClearSelection();
        ClearPipetteSample();
        return true;
    }

    uint32_t FrameCount() const
    {
        return m_metadata.FrameCount();
    }

    static bool QuarterTurnedByOrientation(const ImageFrameInfo* frame, bool applyOrientation)
    {
        return frame != nullptr && applyOrientation && frame->HasOrientation && frame->Orientation >= 5 && frame->Orientation <= 8;
    }

    const ImageFrameInfo* CurrentFrameInfo() const
    {
        if (m_currentFrame >= m_metadata.Frames.size())
            return nullptr;
        return &m_metadata.Frames[m_currentFrame];
    }

    bool EnsureEngine(std::wstring& errorText)
    {
        if (m_engine != nullptr)
            return true;

        m_engine = std::make_unique<ImageEngine>();
        if (!m_engine->IsAvailable())
        {
            errorText = ImageErrorText(m_engine->InitializationError());
            m_engine.reset();
            return false;
        }
        return true;
    }

    bool EnsureDocumentOpen(std::wstring& errorText)
    {
        if (m_document != nullptr)
            return true;

        if (m_sourceBytes.empty())
        {
            errorText = ViewerText(IDS_PV_IMAGE_SOURCE_DATA_IS_UNAVAILABLE);
            return false;
        }

        if (!EnsureEngine(errorText))
            return false;

        StreamLoadResult stream = CreateMemoryStreamFromBytes(m_sourceBytes);
        if (!stream.Succeeded())
        {
            errorText = stream.Message;
            return false;
        }

        OpenDocumentResult open = m_engine->OpenStream(stream.Stream.Get());
        if (!open.Succeeded())
        {
            errorText = ImageErrorText(open.Error);
            return false;
        }

        m_sourceStream = stream.Stream;
        m_document = std::move(open.Document);
        if (m_metadata.Frames.empty())
            m_metadata = m_document->Metadata();
        return true;
    }

    COLORREF CurrentCanvasBackgroundColor() const
    {
        return m_fullScreen ? m_fullScreenBackgroundColor : m_backgroundColor;
    }

    COLORREF CurrentTransparentColor() const
    {
        return m_fullScreen ? m_fullScreenTransparentColor : m_transparentColor;
    }

    bool SurfaceHasAlpha() const
    {
        if (m_surface.Pixels.empty())
            return false;

        for (uint32_t y = 0; y < m_surface.Height; ++y)
        {
            const uint8_t* row = m_surface.Pixels.data() + static_cast<size_t>(y) * m_surface.Stride;
            for (uint32_t x = 0; x < m_surface.Width; ++x)
            {
                if (row[static_cast<size_t>(x) * 4u + 3] != 255)
                    return true;
            }
        }
        return false;
    }

    const uint8_t* PixelsForPaint(std::vector<uint8_t>& compositedPixels) const
    {
        if (!SurfaceHasAlpha())
            return m_surface.Pixels.data();

        compositedPixels = m_surface.Pixels;
        const COLORREF transparentColor = CurrentTransparentColor();
        const uint32_t matteBlue = GetBValue(transparentColor);
        const uint32_t matteGreen = GetGValue(transparentColor);
        const uint32_t matteRed = GetRValue(transparentColor);
        for (uint32_t y = 0; y < m_surface.Height; ++y)
        {
            uint8_t* row = compositedPixels.data() + static_cast<size_t>(y) * m_surface.Stride;
            for (uint32_t x = 0; x < m_surface.Width; ++x)
            {
                uint8_t* pixel = row + static_cast<size_t>(x) * 4u;
                const uint32_t alpha = pixel[3];
                if (alpha == 255)
                    continue;

                const uint32_t inverse = 255u - alpha;
                pixel[0] = static_cast<uint8_t>((static_cast<uint32_t>(pixel[0]) * alpha + matteBlue * inverse + 127u) / 255u);
                pixel[1] = static_cast<uint8_t>((static_cast<uint32_t>(pixel[1]) * alpha + matteGreen * inverse + 127u) / 255u);
                pixel[2] = static_cast<uint8_t>((static_cast<uint32_t>(pixel[2]) * alpha + matteRed * inverse + 127u) / 255u);
                pixel[3] = 255;
            }
        }
        return compositedPixels.data();
    }

    // The frame is composed off-screen and copied to the window in one step, so the background
    // never shows on its own between the fill and the (slow, HALFTONE) stretch of the image:
    // that showed as a flicker on every repaint, e.g. when Space moves to the next image.
    void Paint()
    {
        if (LayoutScrollBars())
            InvalidateRect(m_hwnd, nullptr, FALSE);
        PAINTSTRUCT ps = {};
        HDC dc = BeginPaint(m_hwnd, &ps);
        if (dc == nullptr)
            return;

        const RECT& area = ps.rcPaint;
        const int width = area.right - area.left;
        const int height = area.bottom - area.top;
        HDC memory = width > 0 && height > 0 ? CreateCompatibleDC(dc) : nullptr;
        HBITMAP bitmap = memory != nullptr ? CreateCompatibleBitmap(dc, width, height) : nullptr;
        if (bitmap == nullptr)
        {
            if (memory != nullptr)
                DeleteDC(memory);
            PaintFrame(dc, area); // no memory for a back buffer: draw directly
            EndPaint(m_hwnd, &ps);
            return;
        }

        HGDIOBJ previous = SelectObject(memory, bitmap);
        SetViewportOrgEx(memory, -area.left, -area.top, nullptr); // keep window coordinates
        PaintFrame(memory, area);
        BitBlt(dc, area.left, area.top, width, height, memory, area.left, area.top, SRCCOPY);
        SelectObject(memory, previous);
        DeleteObject(bitmap);
        DeleteDC(memory);
        EndPaint(m_hwnd, &ps);
    }

    void PaintFrame(HDC dc, const RECT& area)
    {
        HBRUSH background = CreateSolidBrush(CurrentCanvasBackgroundColor());
        FillRect(dc, &area, background);
        DeleteObject(background);

        if (m_surface.Pixels.empty())
        {
            const wchar_t* text = ViewerText(IDS_PV_NO_IMAGE_LOADED);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            DrawTextW(dc, text, -1, &m_content, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return;
        }

        RECT imageRect = CalculateImageRect();
        // The image stays inside its area: the corner between the scrollbars keeps the background.
        const int savedDc = SaveDC(dc);
        IntersectClipRect(dc, m_content.left, m_content.top, m_content.right, m_content.bottom);

        BITMAPINFO bitmapInfo = {};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(m_surface.Width);
        bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(m_surface.Height);
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        std::vector<uint8_t> compositedPixels;
        StretchDIBits(dc,
                      imageRect.left,
                      imageRect.top,
                      imageRect.right - imageRect.left,
                      imageRect.bottom - imageRect.top,
                      0,
                      0,
                      m_surface.Width,
                      m_surface.Height,
                      PixelsForPaint(compositedPixels),
                      &bitmapInfo,
                      DIB_RGB_COLORS,
                      SRCCOPY);

        DrawSelection(dc);
        RestoreDC(dc, savedDc);
    }

    RECT CalculateImageRect() const
    {
        RECT rect = m_content;
        if (m_surface.Width == 0 || m_surface.Height == 0)
            return rect;

        const int viewWidth = std::max<LONG>(1, m_content.right - m_content.left);
        const int viewHeight = std::max<LONG>(1, m_content.bottom - m_content.top);
        const double scale = EffectiveScale(viewWidth, viewHeight);
        const long long drawWidth = std::max<long long>(1, std::llround(static_cast<double>(m_surface.Width) * scale));
        const long long drawHeight = std::max<long long>(1, std::llround(DisplayHeight() * scale));

        const int left = m_content.left + (viewWidth - ClampInt64ToInt(drawWidth)) / 2 + m_panX;
        const int top = m_content.top + (viewHeight - ClampInt64ToInt(drawHeight)) / 2 + m_panY;
        rect.left = left;
        rect.top = top;
        rect.right = left + ClampInt64ToInt(drawWidth);
        rect.bottom = top + ClampInt64ToInt(drawHeight);
        return rect;
    }

    double EffectiveScale(int viewWidth, int viewHeight) const
    {
        if (m_surface.Width == 0 || m_surface.Height == 0)
            return 1.0;

        double scale = 1.0;
        if (m_zoomMode == ZoomMode::FitWhole)
        {
            scale = std::min(static_cast<double>(viewWidth) / static_cast<double>(m_surface.Width),
                             static_cast<double>(viewHeight) / DisplayHeight());
        }
        else if (m_zoomMode == ZoomMode::FitWidth)
        {
            scale = static_cast<double>(viewWidth) / static_cast<double>(m_surface.Width);
        }
        else if (m_zoomMode == ZoomMode::Manual)
        {
            scale = m_manualZoom;
        }

        if (!std::isfinite(scale))
            scale = 1.0;
        return std::max(0.01, std::min(scale, 64.0));
    }

    void GetDrawSize(long long& drawWidth, long long& drawHeight) const
    {
        const int viewWidth = std::max<LONG>(1, m_content.right - m_content.left);
        const int viewHeight = std::max<LONG>(1, m_content.bottom - m_content.top);
        const double scale = EffectiveScale(viewWidth, viewHeight);
        drawWidth = std::max<long long>(1, std::llround(static_cast<double>(m_surface.Width) * scale));
        drawHeight = std::max<long long>(1, std::llround(DisplayHeight() * scale));
    }

    // The image height in screen pixels at 100%: non-square pixels (a fax at 204x98 dpi) are drawn
    // at their real aspect, as the old PictView did.
    double DisplayHeight() const
    {
        const ImageFrameInfo* frame = CurrentFrameInfo();
        const double aspect = frame != nullptr ? PixelAspectRatio(frame->DpiX, frame->DpiY, m_quarterTurned) : 1.0;
        return static_cast<double>(m_surface.Height) * aspect;
    }

    void ClampPanOffsets()
    {
        if (m_surface.Width == 0 || m_surface.Height == 0)
        {
            m_panX = 0;
            m_panY = 0;
            return;
        }

        const int viewWidth = std::max<LONG>(1, m_content.right - m_content.left);
        const int viewHeight = std::max<LONG>(1, m_content.bottom - m_content.top);

        long long drawWidth = 0;
        long long drawHeight = 0;
        GetDrawSize(drawWidth, drawHeight);

        auto clampAxis = [](int value, long long drawSize, int viewSize) {
            if (drawSize <= viewSize)
                return 0;
            const int limit = ClampInt64ToInt((drawSize - viewSize + 1) / 2);
            return std::max(-limit, std::min(value, limit));
        };

        m_panX = clampAxis(m_panX, drawWidth, viewWidth);
        m_panY = clampAxis(m_panY, drawHeight, viewHeight);
    }

    bool CanPan() const
    {
        if (m_surface.Width == 0 || m_surface.Height == 0)
            return false;

        const int viewWidth = std::max<LONG>(1, m_content.right - m_content.left);
        const int viewHeight = std::max<LONG>(1, m_content.bottom - m_content.top);
        long long drawWidth = 0;
        long long drawHeight = 0;
        GetDrawSize(drawWidth, drawHeight);
        return drawWidth > viewWidth || drawHeight > viewHeight;
    }

    bool CanPanHorizontally() const
    {
        if (m_surface.Width == 0 || m_surface.Height == 0)
            return false;

        const int viewWidth = std::max<LONG>(1, m_content.right - m_content.left);
        long long drawWidth = 0;
        long long drawHeight = 0;
        GetDrawSize(drawWidth, drawHeight);
        return drawWidth > viewWidth;
    }

    bool CanPanVertically() const
    {
        if (m_surface.Width == 0 || m_surface.Height == 0)
            return false;

        const int viewHeight = std::max<LONG>(1, m_content.bottom - m_content.top);
        long long drawWidth = 0;
        long long drawHeight = 0;
        GetDrawSize(drawWidth, drawHeight);
        return drawHeight > viewHeight;
    }

    bool PointInContent(int x, int y) const
    {
        return x >= m_content.left && x < m_content.right &&
               y >= m_content.top && y < m_content.bottom;
    }

    bool PointToImagePixel(int x, int y, bool requireInside, uint32_t& imageX, uint32_t& imageY) const
    {
        if (m_surface.Width == 0 || m_surface.Height == 0)
            return false;

        const RECT imageRect = CalculateImageRect();
        const int drawWidth = imageRect.right - imageRect.left;
        const int drawHeight = imageRect.bottom - imageRect.top;
        if (drawWidth <= 0 || drawHeight <= 0)
            return false;

        if (requireInside &&
            (x < imageRect.left || x >= imageRect.right ||
             y < imageRect.top || y >= imageRect.bottom))
        {
            return false;
        }

        const int clampedX = std::max<int>(imageRect.left, std::min<int>(x, imageRect.right - 1));
        const int clampedY = std::max<int>(imageRect.top, std::min<int>(y, imageRect.bottom - 1));
        const long long relX = clampedX - imageRect.left;
        const long long relY = clampedY - imageRect.top;
        imageX = static_cast<uint32_t>(std::min<long long>(m_surface.Width - 1, (relX * m_surface.Width) / drawWidth));
        imageY = static_cast<uint32_t>(std::min<long long>(m_surface.Height - 1, (relY * m_surface.Height) / drawHeight));
        return true;
    }

    bool SampleImagePixel(int x, int y, PipetteSample& sample) const
    {
        uint32_t imageX = 0;
        uint32_t imageY = 0;
        if (!PointToImagePixel(x, y, true, imageX, imageY))
            return false;

        const size_t offset = (static_cast<size_t>(imageY) * m_surface.Width + imageX) * 4;
        if (offset + 2 >= m_surface.Pixels.size())
            return false;

        sample.X = imageX;
        sample.Y = imageY;
        sample.Blue = m_surface.Pixels[offset + 0];
        sample.Green = m_surface.Pixels[offset + 1];
        sample.Red = m_surface.Pixels[offset + 2];
        const size_t pixel = static_cast<size_t>(imageY) * m_surface.Width + imageX;
        sample.Index = pixel < m_surface.PaletteIndices.size() ? m_surface.PaletteIndices[pixel] : -1;
        return true;
    }

    std::wstring PipetteStatusText() const
    {
        if (m_tool != ViewerTool::Pipette)
            return std::wstring();
        if (!m_pipetteHasSample)
            return ViewerText(IDS_PV_PICK_COLOR_MOVE_MOUSE_OVER);

        return FormatPipetteSample(m_pipetteSample.X, m_pipetteSample.Y, m_pipetteSample.Red, m_pipetteSample.Green,
                                   m_pipetteSample.Blue, m_pipetteInHex, m_pipetteSample.Index);
    }

    void ClearPipetteSample()
    {
        m_pipetteHasSample = false;
        m_pipetteSample = PipetteSample();
    }

    void UpdatePipetteSample(int x, int y)
    {
        if (m_tool != ViewerTool::Pipette)
            return;

        PipetteSample sample;
        m_pipetteHasSample = SampleImagePixel(x, y, sample);
        if (m_pipetteHasSample)
            m_pipetteSample = sample;
        else
            m_pipetteSample = PipetteSample();
        UpdateStatus();
        UpdatePipetteTip(x, y);
    }

    // The old PictView showed the picked colour next to the cursor; a tracking tool tip does it.
    void UpdatePipetteTip(int x, int y)
    {
        if (m_tool != ViewerTool::Pipette || !m_pipetteHasSample)
        {
            HidePipetteTip();
            return;
        }
        if (m_pipetteTip == nullptr)
        {
            m_pipetteTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0,
                                           0, m_hwnd, nullptr, ModuleInstance, nullptr);
            if (m_pipetteTip == nullptr)
                return;
            TOOLINFOW tool = {};
            tool.cbSize = sizeof(tool);
            tool.uFlags = TTF_TRACK | TTF_ABSOLUTE;
            tool.hwnd = m_hwnd;
            tool.lpszText = const_cast<wchar_t*>(L"");
            SendMessageW(m_pipetteTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
            PluginDarkMode_ApplyTooltipTheme(m_pipetteTip);
        }
        m_pipetteTipText = PipetteStatusText();
        TOOLINFOW tool = {};
        tool.cbSize = sizeof(tool);
        tool.hwnd = m_hwnd;
        tool.lpszText = m_pipetteTipText.data();
        SendMessageW(m_pipetteTip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
        POINT screen = {x, y};
        ClientToScreen(m_hwnd, &screen);
        SendMessageW(m_pipetteTip, TTM_TRACKPOSITION, 0, MAKELPARAM(screen.x + 16, screen.y + 20));
        SendMessageW(m_pipetteTip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
    }

    void HidePipetteTip()
    {
        if (m_pipetteTip == nullptr)
            return;
        TOOLINFOW tool = {};
        tool.cbSize = sizeof(tool);
        tool.hwnd = m_hwnd;
        SendMessageW(m_pipetteTip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
    }

    void CapturePipetteSampleAtCursor()
    {
        POINT cursor = {};
        if (GetCursorPos(&cursor))
        {
            ScreenToClient(m_hwnd, &cursor);
            PipetteSample sample;
            if (SampleImagePixel(cursor.x, cursor.y, sample))
            {
                m_pipetteSample = sample;
                m_pipetteHasSample = true;
            }
        }
    }

    HCURSOR CursorForCurrentTool(POINT clientPoint) const
    {
        const bool controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
        const bool shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (m_panning)
            return LoadViewerCursor(IDC_HAND2, 32512);

        switch (m_tool)
        {
        case ViewerTool::Hand:
            if (controlPressed && !altPressed && !shiftPressed)
                return LoadViewerCursor(IDC_ZOOMIN, 32512);
            if (!controlPressed && !altPressed && shiftPressed)
                return LoadViewerCursor(IDC_ZOOMOUT, 32512);
            return LoadViewerCursor(IDC_HAND1, 32512);

        case ViewerTool::Zoom:
            if (!controlPressed && !altPressed && shiftPressed && !m_zoomSelecting)
                return LoadViewerCursor(IDC_ZOOMOUT, 32512);
            return LoadViewerCursor(IDC_ZOOMIN, 32512);

        case ViewerTool::Select:
        {
            uint32_t imageX = 0;
            uint32_t imageY = 0;
            if (PointToImagePixel(clientPoint.x, clientPoint.y, true, imageX, imageY))
                return LoadViewerCursor(IDC_SELECT, 32512);
            return LoadStockCursor(32512);
        }

        case ViewerTool::Pipette:
            if (controlPressed && !altPressed && !shiftPressed)
                return LoadViewerCursor(IDC_ZOOMIN, 32512);
            if (!controlPressed && !altPressed && shiftPressed)
                return LoadViewerCursor(IDC_ZOOMOUT, 32512);
            return LoadViewerCursor(IDC_PIPETA, 32512);
        }

        return LoadStockCursor(32512);
    }

    // Full screen: the cursor shows on movement and hides after a few seconds of rest.
    void RestartFullScreenCursorTimer(LPARAM position)
    {
        if (position == m_lastMousePosition) // WM_MOUSEMOVE also arrives without movement
            return;
        m_lastMousePosition = position;
        if (m_cursorHidden)
        {
            m_cursorHidden = false;
            HandleSetCursor(MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        }
        SetTimer(m_hwnd, CursorTimerId, FullScreenCursorDelayMs, nullptr);
    }

    void OpenDroppedFile(HDROP drop)
    {
        const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
        std::wstring path;
        if (length > 0 && length != static_cast<UINT>(-1))
        {
            path.resize(static_cast<size_t>(length) + 1);
            path.resize(DragQueryFileW(drop, 0, path.data(), length + 1));
        }
        DragFinish(drop);
        if (!path.empty() && !m_loading && (GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_DIRECTORY) == 0)
            QueueFileLoad(std::move(path), !m_surface.Pixels.empty(), true);
    }

    bool HandleSetCursor(LPARAM lParam)
    {
        if (LOWORD(lParam) != HTCLIENT)
            return false;
        if (m_cursorHidden)
        {
            SetCursor(nullptr);
            return true;
        }

        if (m_loading)
        {
            SetCursor(LoadStockCursor(32514));
            return true;
        }

        if (m_surface.Pixels.empty())
        {
            SetCursor(LoadStockCursor(32512));
            return true;
        }

        POINT cursor = {};
        if (!GetCursorPos(&cursor) || !ScreenToClient(m_hwnd, &cursor))
        {
            SetCursor(LoadStockCursor(32512));
            return true;
        }

        if (!PointInContent(cursor.x, cursor.y))
        {
            SetCursor(LoadStockCursor(32512));
            return true;
        }

        SetCursor(CursorForCurrentTool(cursor));
        return true;
    }

    void SetActiveTool(ViewerTool tool)
    {
        if (m_loading || m_surface.Pixels.empty())
            return;

        if (GetCapture() == m_hwnd)
            ReleaseCapture();
        m_panning = false;
        m_selecting = false;
        m_constrainSelectionRatio = false;
        m_zoomSelecting = false;

        m_tool = tool;
        HidePipetteTip();
        ClearPipetteSample();
        if (m_tool == ViewerTool::Pipette)
            CapturePipetteSampleAtCursor();
        UpdateStatus();
        UpdateCommands();
        HandleSetCursor(MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    void TogglePipetteTool()
    {
        SetActiveTool(m_tool == ViewerTool::Pipette ? ViewerTool::Hand : ViewerTool::Pipette);
    }

    bool HasCropSelection() const
    {
        return m_hasSelection &&
               m_selectionImage.right > m_selectionImage.left &&
               m_selectionImage.bottom > m_selectionImage.top;
    }

    void ClearSelection()
    {
        m_hasSelection = false;
        m_selecting = false;
        m_constrainSelectionRatio = false;
        m_zoomSelecting = false;
        m_selectionImage = {};
    }

    POINT ConstrainSelectionEndToRatio(POINT current) const
    {
        if (m_surface.Width == 0 || m_surface.Height == 0)
            return current;

        const ViewerPreferences preferences = GetViewerPreferences();
        const long long ratioX = SanitizeSelectRatio(preferences.SelectRatioX);
        const long long ratioY = SanitizeSelectRatio(preferences.SelectRatioY);

        const long long dx = static_cast<long long>(current.x) - m_selectionStart.x;
        const long long dy = static_cast<long long>(current.y) - m_selectionStart.y;
        const long long draggedWidth = std::llabs(dx) + 1;
        const long long draggedHeight = std::llabs(dy) + 1;

        long long constrainedWidth = draggedWidth;
        long long constrainedHeight = draggedHeight;
        if (draggedWidth * ratioY < draggedHeight * ratioX)
            constrainedHeight = (std::max)(1ll, (draggedWidth * ratioY) / ratioX);
        else
            constrainedWidth = (std::max)(1ll, (draggedHeight * ratioX) / ratioY);

        const long long signX = dx < 0 ? -1 : 1;
        const long long signY = dy < 0 ? -1 : 1;
        long long x = static_cast<long long>(m_selectionStart.x) + signX * (constrainedWidth - 1);
        long long y = static_cast<long long>(m_selectionStart.y) + signY * (constrainedHeight - 1);

        x = (std::max)(0ll, (std::min)(x, static_cast<long long>(m_surface.Width - 1)));
        y = (std::max)(0ll, (std::min)(y, static_cast<long long>(m_surface.Height - 1)));
        current.x = static_cast<LONG>(x);
        current.y = static_cast<LONG>(y);
        return current;
    }

    void UpdateSelectionToPoint(int x, int y)
    {
        uint32_t imageX = 0;
        uint32_t imageY = 0;
        if (!PointToImagePixel(x, y, false, imageX, imageY))
            return;

        POINT current = {static_cast<LONG>(imageX), static_cast<LONG>(imageY)};
        if (m_constrainSelectionRatio)
            current = ConstrainSelectionEndToRatio(current);

        const LONG currentX = current.x;
        const LONG currentY = current.y;
        m_selectionImage.left = std::min(m_selectionStart.x, currentX);
        m_selectionImage.top = std::min(m_selectionStart.y, currentY);
        m_selectionImage.right = std::max(m_selectionStart.x, currentX) + 1;
        m_selectionImage.bottom = std::max(m_selectionStart.y, currentY) + 1;
    }

    void BeginSelection(int x, int y, bool constrainRatio)
    {
        SetFocus(m_hwnd);
        uint32_t imageX = 0;
        uint32_t imageY = 0;
        if (!PointToImagePixel(x, y, true, imageX, imageY))
            return;

        m_selecting = true;
        m_constrainSelectionRatio = constrainRatio;
        m_hasSelection = true;
        m_selectionStart.x = static_cast<LONG>(imageX);
        m_selectionStart.y = static_cast<LONG>(imageY);
        UpdateSelectionToPoint(x, y);
        SetCapture(m_hwnd);
        UpdateStatus();
        UpdateCommands();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    void ContinueSelection(int x, int y)
    {
        if (!m_selecting)
            return;

        // Space held: the whole area follows the mouse instead of its second corner.
        uint32_t imageX = 0;
        uint32_t imageY = 0;
        const bool onImage = PointToImagePixel(x, y, true, imageX, imageY);
        if (onImage && GetKeyState(VK_SPACE) < 0 && m_hasLastSelectionPoint)
        {
            const LONG dx = static_cast<LONG>(imageX) - m_lastSelectionPoint.x;
            const LONG dy = static_cast<LONG>(imageY) - m_lastSelectionPoint.y;
            m_selectionStart.x = (std::max)(0L, (std::min)(m_selectionStart.x + dx, static_cast<LONG>(m_surface.Width) - 1));
            m_selectionStart.y = (std::max)(0L, (std::min)(m_selectionStart.y + dy, static_cast<LONG>(m_surface.Height) - 1));
        }
        if (onImage)
        {
            m_lastSelectionPoint = {static_cast<LONG>(imageX), static_cast<LONG>(imageY)};
            m_hasLastSelectionPoint = true;
        }
        if (m_tool == ViewerTool::Select)
            m_constrainSelectionRatio = GetKeyState(VK_SHIFT) < 0; // read on every move, as before
        UpdateEdgeScroll(x, y);

        if (m_zoomSelecting)
        {
            m_zoomSelectionEndClient.x = x;
            m_zoomSelectionEndClient.y = y;
        }
        UpdateSelectionToPoint(x, y);
        UpdateStatus();
        UpdateCommands();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    // While a selection is dragged past the view's edge the image scrolls toward the mouse.
    void UpdateEdgeScroll(int x, int y)
    {
        const bool outside = x < m_content.left || x >= m_content.right || y < m_content.top || y >= m_content.bottom;
        if (outside && !m_edgeScrolling)
        {
            m_edgeScrolling = SetTimer(m_hwnd, EdgeScrollTimerId, EdgeScrollIntervalMs, nullptr) != 0;
        }
        else if (!outside && m_edgeScrolling)
        {
            KillTimer(m_hwnd, EdgeScrollTimerId);
            m_edgeScrolling = false;
        }
    }

    void EdgeScrollTick()
    {
        POINT cursor = {};
        if (!m_selecting || !GetCursorPos(&cursor) || !ScreenToClient(m_hwnd, &cursor))
        {
            UpdateEdgeScroll(m_content.left, m_content.top);
            return;
        }
        const int step = 16;
        int dx = 0;
        int dy = 0;
        if (cursor.x < m_content.left)
            dx = step;
        else if (cursor.x >= m_content.right)
            dx = -step;
        if (cursor.y < m_content.top)
            dy = step;
        else if (cursor.y >= m_content.bottom)
            dy = -step;
        PanByPixels(dx, dy);
        ContinueSelection(cursor.x, cursor.y);
    }

    // Esc during a drag: the selection, zoom marquee or pan stops where it is.
    void CancelDrag()
    {
        if (m_edgeScrolling)
        {
            KillTimer(m_hwnd, EdgeScrollTimerId);
            m_edgeScrolling = false;
        }
        if (m_zoomSelecting)
        {
            m_zoomSelecting = false;
            m_selecting = false;
            ReleaseCapture();
            ClearSelection();
            return;
        }
        if (m_selecting)
        {
            EndSelection();
            return;
        }
        if (m_panning)
            EndPan();
        else
            ReleaseCapture();
    }

    void EndSelection()
    {
        if (m_edgeScrolling)
        {
            KillTimer(m_hwnd, EdgeScrollTimerId);
            m_edgeScrolling = false;
        }
        m_hasLastSelectionPoint = false;
        if (GetCapture() == m_hwnd)
            ReleaseCapture();
        m_selecting = false;
        m_constrainSelectionRatio = false;
        UpdateStatus();
        UpdateCommands();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    RECT SelectionClientRect() const
    {
        RECT result = {};
        if (!HasCropSelection() || m_surface.Width == 0 || m_surface.Height == 0)
            return result;

        const RECT imageRect = CalculateImageRect();
        const long long drawWidth = imageRect.right - imageRect.left;
        const long long drawHeight = imageRect.bottom - imageRect.top;
        if (drawWidth <= 0 || drawHeight <= 0)
            return result;

        auto mapXFloor = [&](LONG value) {
            return imageRect.left + ClampInt64ToInt((static_cast<long long>(value) * drawWidth) / m_surface.Width);
        };
        auto mapYFloor = [&](LONG value) {
            return imageRect.top + ClampInt64ToInt((static_cast<long long>(value) * drawHeight) / m_surface.Height);
        };
        auto mapXCeil = [&](LONG value) {
            return imageRect.left + ClampInt64ToInt((static_cast<long long>(value) * drawWidth + m_surface.Width - 1) / m_surface.Width);
        };
        auto mapYCeil = [&](LONG value) {
            return imageRect.top + ClampInt64ToInt((static_cast<long long>(value) * drawHeight + m_surface.Height - 1) / m_surface.Height);
        };

        result.left = mapXFloor(m_selectionImage.left);
        result.top = mapYFloor(m_selectionImage.top);
        result.right = mapXCeil(m_selectionImage.right);
        result.bottom = mapYCeil(m_selectionImage.bottom);
        return result;
    }

    void DrawSelection(HDC dc) const
    {
        if (!HasCropSelection())
            return;

        RECT selection = SelectionClientRect();
        RECT clipped = {};
        if (!IntersectRect(&clipped, &selection, &m_content))
            return;

        const int oldRop = SetROP2(dc, R2_NOTXORPEN);
        HPEN pen = CreatePen(PS_DOT, 1, RGB(255, 255, 255));
        HGDIOBJ oldPen = SelectObject(dc, pen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        Rectangle(dc, clipped.left, clipped.top, clipped.right, clipped.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        SetROP2(dc, oldRop);
    }

    void BeginPan(int x, int y)
    {
        SetFocus(m_hwnd);
        if (!CanPan() || !PointInContent(x, y))
            return;

        m_panning = true;
        m_panStartPoint.x = x;
        m_panStartPoint.y = y;
        m_panStartX = m_panX;
        m_panStartY = m_panY;
        SetCapture(m_hwnd);
        SetCursor(LoadViewerCursor(IDC_HAND2, 32512));
    }

    void ContinuePan(int x, int y)
    {
        if (!m_panning)
            return;

        m_panX = m_panStartX + (x - m_panStartPoint.x);
        m_panY = m_panStartY + (y - m_panStartPoint.y);
        ClampPanOffsets();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    void EndPan()
    {
        if (GetCapture() == m_hwnd)
            ReleaseCapture();
        m_panning = false;
        HandleSetCursor(MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    }

    void PanByPixels(int dx, int dy)
    {
        if (!CanPan())
            return;

        const int oldPanX = m_panX;
        const int oldPanY = m_panY;
        m_panX += dx;
        m_panY += dy;
        ClampPanOffsets();
        if (m_panX != oldPanX || m_panY != oldPanY)
            InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    void PanToEdge(bool horizontal, bool positiveEdge)
    {
        if (horizontal ? !CanPanHorizontally() : !CanPanVertically())
            return;

        const int oldPanX = m_panX;
        const int oldPanY = m_panY;
        const int viewSize = horizontal ?
                                 std::max<LONG>(1, m_content.right - m_content.left) :
                                 std::max<LONG>(1, m_content.bottom - m_content.top);
        long long drawWidth = 0;
        long long drawHeight = 0;
        GetDrawSize(drawWidth, drawHeight);
        const long long drawSize = horizontal ? drawWidth : drawHeight;
        const int limit = ClampInt64ToInt((drawSize - viewSize + 1) / 2);
        if (horizontal)
            m_panX = positiveEdge ? limit : -limit;
        else
            m_panY = positiveEdge ? limit : -limit;

        ClampPanOffsets();
        if (m_panX != oldPanX || m_panY != oldPanY)
            InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    bool HandlePanKey(WPARAM key, bool shift)
    {
        if (m_surface.Pixels.empty() || m_loading || !CanPan())
            return false;

        const int viewWidth = std::max<LONG>(1, m_content.right - m_content.left);
        const int viewHeight = std::max<LONG>(1, m_content.bottom - m_content.top);
        const int lineX = std::max(16, viewWidth / 12);
        const int lineY = std::max(16, viewHeight / 12);
        const int pageX = std::max(16, (viewWidth * 9) / 10);
        const int pageY = std::max(16, (viewHeight * 9) / 10);

        switch (key)
        {
        case VK_LEFT:
            PanByPixels(lineX, 0);
            return true;
        case VK_RIGHT:
            PanByPixels(-lineX, 0);
            return true;
        case VK_UP:
            PanByPixels(0, lineY);
            return true;
        case VK_DOWN:
            PanByPixels(0, -lineY);
            return true;
        case VK_HOME:
            if (shift)
                PanToEdge(true, true);
            else
                PanByPixels(pageX, 0);
            return true;
        case VK_END:
            if (shift)
                PanToEdge(true, false);
            else
                PanByPixels(-pageX, 0);
            return true;
        case VK_PRIOR:
            if (shift)
                PanToEdge(false, true);
            else
                PanByPixels(0, pageY);
            return true;
        case VK_NEXT:
            if (shift)
                PanToEdge(false, false);
            else
                PanByPixels(0, -pageY);
            return true;
        default:
            return false;
        }
    }

    void PanByWheel(int delta, bool horizontal)
    {
        if (m_surface.Pixels.empty() || m_loading || !CanPan())
            return;

        const int viewWidth = std::max<LONG>(1, m_content.right - m_content.left);
        const int viewHeight = std::max<LONG>(1, m_content.bottom - m_content.top);
        const int lineX = std::max(16, viewWidth / 12);
        const int lineY = std::max(16, viewHeight / 12);
        const int steps = std::max(1, std::abs(delta) / WHEEL_DELTA);
        const int pixels = (horizontal ? lineX : lineY) * steps;
        if (horizontal)
            PanByPixels(delta > 0 ? pixels : -pixels, 0);
        else
            PanByPixels(0, delta > 0 ? pixels : -pixels);
    }

    void NavigatePageUpDownPreference(bool previous)
    {
        if (HasSourceNavigation())
            NavigateSourceFile(previous ? ViewerSourceNavigationMode::Previous : ViewerSourceNavigationMode::Next);
        else
            ChangeFrame(previous ? -1 : 1);
    }

    bool HandleMouseWheel(WPARAM wParam)
    {
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (delta == 0)
            return true;

        const bool control = (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0;
        const bool shift = (GET_KEYSTATE_WPARAM(wParam) & MK_SHIFT) != 0;

        if (control)
        {
            ZoomStep(delta > 0);
            return true;
        }

        if (!shift && !GetViewerPreferences().PageUpDownScrolls)
        {
            NavigatePageUpDownPreference(delta > 0);
            return true;
        }

        PanByWheel(delta, shift);
        return true;
    }

    double ClampManualZoom(double zoom) const
    {
        if (!std::isfinite(zoom))
            zoom = 1.0;
        return std::max(0.01, std::min(zoom, 64.0));
    }

    void SetManualZoomAroundImagePoint(double zoom, double imageX, double imageY, int clientX, int clientY)
    {
        if (m_surface.Width == 0 || m_surface.Height == 0)
            return;

        imageX = std::max(0.0, std::min(imageX, static_cast<double>(m_surface.Width)));
        imageY = std::max(0.0, std::min(imageY, static_cast<double>(m_surface.Height)));

        m_zoomMode = ZoomMode::Manual;
        m_manualZoom = ClampManualZoom(zoom);

        const int viewWidth = std::max<LONG>(1, m_content.right - m_content.left);
        const int viewHeight = std::max<LONG>(1, m_content.bottom - m_content.top);
        long long drawWidth = 0;
        long long drawHeight = 0;
        GetDrawSize(drawWidth, drawHeight);

        const int baseLeft = m_content.left + (viewWidth - ClampInt64ToInt(drawWidth)) / 2;
        const int baseTop = m_content.top + (viewHeight - ClampInt64ToInt(drawHeight)) / 2;
        const int imageOffsetX = ClampInt64ToInt(static_cast<long long>(std::llround((imageX * static_cast<double>(drawWidth)) / static_cast<double>(m_surface.Width))));
        const int imageOffsetY = ClampInt64ToInt(static_cast<long long>(std::llround((imageY * static_cast<double>(drawHeight)) / static_cast<double>(m_surface.Height))));
        m_panX = clientX - baseLeft - imageOffsetX;
        m_panY = clientY - baseTop - imageOffsetY;

        ClampPanOffsets();
        UpdateStatus();
        UpdateCommands();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    void ZoomAtClientPoint(int x, int y, double factor)
    {
        if (m_surface.Pixels.empty() || m_loading)
            return;

        uint32_t imageX = 0;
        uint32_t imageY = 0;
        if (!PointToImagePixel(x, y, true, imageX, imageY))
            return;

        const int viewWidth = std::max<LONG>(1, m_content.right - m_content.left);
        const int viewHeight = std::max<LONG>(1, m_content.bottom - m_content.top);
        const double currentScale = EffectiveScale(viewWidth, viewHeight);
        SetManualZoomAroundImagePoint(currentScale * factor,
                                      static_cast<double>(imageX) + 0.5,
                                      static_cast<double>(imageY) + 0.5,
                                      x,
                                      y);
    }

    void ZoomToSelection()
    {
        if (!HasCropSelection() || m_surface.Pixels.empty() || m_loading)
            return;

        const RECT selection = m_selectionImage;
        ClearSelection();

        const int viewWidth = std::max<LONG>(1, m_content.right - m_content.left);
        const int viewHeight = std::max<LONG>(1, m_content.bottom - m_content.top);
        const LONG selectionWidth = std::max<LONG>(1, selection.right - selection.left);
        const LONG selectionHeight = std::max<LONG>(1, selection.bottom - selection.top);
        const double zoom = std::min(static_cast<double>(viewWidth) / static_cast<double>(selectionWidth),
                                     static_cast<double>(viewHeight) / static_cast<double>(selectionHeight));
        const double centerX = (static_cast<double>(selection.left) + static_cast<double>(selection.right)) / 2.0;
        const double centerY = (static_cast<double>(selection.top) + static_cast<double>(selection.bottom)) / 2.0;
        const int clientX = m_content.left + viewWidth / 2;
        const int clientY = m_content.top + viewHeight / 2;
        SetManualZoomAroundImagePoint(zoom, centerX, centerY, clientX, clientY);
    }

    void BeginZoomSelection(int x, int y)
    {
        m_zoomSelectionStartClient.x = x;
        m_zoomSelectionStartClient.y = y;
        m_zoomSelectionEndClient = m_zoomSelectionStartClient;
        BeginSelection(x, y, false);
        m_zoomSelecting = m_selecting;
    }

    void EndZoomSelection(int x, int y)
    {
        m_zoomSelectionEndClient.x = x;
        m_zoomSelectionEndClient.y = y;
        const bool dragged = std::abs(m_zoomSelectionEndClient.x - m_zoomSelectionStartClient.x) >= 4 ||
                             std::abs(m_zoomSelectionEndClient.y - m_zoomSelectionStartClient.y) >= 4;
        EndSelection();

        if (dragged && HasCropSelection())
        {
            ZoomToSelection();
        }
        else
        {
            ClearSelection();
            ZoomAtClientPoint(x, y, ZoomStepFactor(true));
        }
        m_zoomSelecting = false;
    }

    void HandleLeftButtonDown(WPARAM keys, int x, int y)
    {
        SetFocus(m_hwnd);
        if (m_surface.Pixels.empty() || m_loading)
            return;

        const bool control = (keys & MK_CONTROL) != 0 || GetKeyState(VK_CONTROL) < 0;
        const bool shift = (keys & MK_SHIFT) != 0 || GetKeyState(VK_SHIFT) < 0;

        if (m_tool == ViewerTool::Pipette)
        {
            if (control != shift)
                ZoomAtClientPoint(x, y, ZoomStepFactor(control));
            else
                UpdatePipetteSample(x, y);
            return;
        }

        if (m_tool == ViewerTool::Zoom)
        {
            if (control || shift)
                ZoomAtClientPoint(x, y, ZoomStepFactor(!(shift && !control)));
            else
                BeginZoomSelection(x, y);
            return;
        }

        if (m_tool == ViewerTool::Select)
        {
            BeginSelection(x, y, shift);
            return;
        }

        if (control != shift) // Hand: Ctrl zooms in, Shift zooms out, on the spot
        {
            ZoomAtClientPoint(x, y, ZoomStepFactor(control));
            return;
        }

        BeginPan(x, y);
    }

    void HandleCommand(int command)
    {
        if (command >= CmdRecentFileFirst && command <= CmdRecentFileLast)
        {
            OpenRecentFile(static_cast<std::size_t>(command - CmdRecentFileFirst));
            return;
        }

        if (command >= CmdRecentDirectoryFirst && command <= CmdRecentDirectoryLast)
        {
            OpenRecentDirectory(static_cast<std::size_t>(command - CmdRecentDirectoryFirst));
            return;
        }

        switch (command)
        {
        case CmdOpen:
            OpenAnotherFile();
            break;

        case CmdSaveAs:
            SaveAs();
            break;

        case CmdReload:
            ReloadFromFile();
            break;

        case CmdRenameFile:
            RenameCurrentFile();
            break;

        case CmdCopyFileTo:
            CopyCurrentFileTo();
            break;

        case CmdDeleteFile:
            DeleteCurrentFile((GetKeyState(VK_SHIFT) & 0x8000) == 0);
            break;

        case CmdProperties:
            ShowProperties();
            break;

        case CmdMetadataDetails:
            ShowMetadataDetails();
            break;

        case CmdHistogram:
            ShowHistogram();
            break;

        case CmdToolHand:
            SetActiveTool(ViewerTool::Hand);
            break;

        case CmdToolZoom:
            SetActiveTool(ViewerTool::Zoom);
            break;

        case CmdToolSelect:
            SetActiveTool(ViewerTool::Select);
            break;

        case CmdTogglePipette:
            TogglePipetteTool();
            break;

        case CmdPrint:
            PrintImage();
            break;

        case CmdScreenCapture:
            BeginScreenCaptureWorkflow();
            break;

        case CmdWallpaperCenter:
            SetAsWallpaper(ViewerWallpaperMode::Center);
            break;

        case CmdWallpaperTile:
            SetAsWallpaper(ViewerWallpaperMode::Tile);
            break;

        case CmdWallpaperStretch:
            SetAsWallpaper(ViewerWallpaperMode::Stretch);
            break;

        case CmdWallpaperRestore:
            SetAsWallpaper(ViewerWallpaperMode::Restore);
            break;

        case CmdWallpaperNone:
            SetAsWallpaper(ViewerWallpaperMode::None);
            break;

        case CmdPrevSourceFile:
            NavigateSourceFile(ViewerSourceNavigationMode::Previous);
            break;

        case CmdNextSourceFile:
            NavigateSourceFile(ViewerSourceNavigationMode::Next);
            break;

        case CmdPrevSelectedSourceFile:
            NavigateSourceFile(ViewerSourceNavigationMode::PreviousSelected);
            break;

        case CmdNextSelectedSourceFile:
            NavigateSourceFile(ViewerSourceNavigationMode::NextSelected);
            break;

        case CmdFirstSourceFile:
            NavigateSourceFile(ViewerSourceNavigationMode::First);
            break;

        case CmdLastSourceFile:
            NavigateSourceFile(ViewerSourceNavigationMode::Last);
            break;

        case CmdToggleSourceSelection:
            ToggleSourceSelection();
            break;

        case CmdFocusSourceFile:
            FocusSourceFile();
            break;

        case CmdFirstFrame:
            NavigateToFrame(0);
            break;

        case CmdPrevFrame:
            ChangeFrame(-1);
            break;

        case CmdGoToFrame:
            PromptGoToFrame();
            break;

        case CmdNextFrame:
            ChangeFrame(1);
            break;

        case CmdLastFrame:
            ShowLastFrame();
            break;

        case CmdToggleAnimation:
            ToggleAnimation();
            break;

        case CmdCopy:
            CopyImageToClipboard();
            break;

        case CmdPaste:
            PasteImageFromClipboard();
            break;

        case CmdSelectAll:
            SelectWholeImage();
            break;

        case CmdDeselect:
            DeselectImage();
            break;

        case CmdRotateLeft:
            ApplySurfaceTransform(ImageSurfaceTransform::Rotate90CounterClockwise);
            break;

        case CmdRotateRight:
            ApplySurfaceTransform(ImageSurfaceTransform::Rotate90Clockwise);
            break;

        case CmdRotate180:
            ApplySurfaceTransform(ImageSurfaceTransform::Rotate180);
            break;

        case CmdFlipHorizontal:
            ApplySurfaceTransform(ImageSurfaceTransform::FlipHorizontal);
            break;

        case CmdFlipVertical:
            ApplySurfaceTransform(ImageSurfaceTransform::FlipVertical);
            break;

        case CmdCrop:
            ApplyCropSelection();
            break;

        case CmdHelpContents:
        case CmdHelpIndex:
        case CmdHelpSearch:
            if (HostGeneral != nullptr)
            {
                const CHtmlHelpCommand helpCommand = command == CmdHelpIndex    ? HHCDisplayIndex
                                                     : command == CmdHelpSearch ? HHCDisplaySearch
                                                                                : HHCDisplayTOC;
                HostGeneral->OpenHtmlHelp(m_hwnd, helpCommand, 0, FALSE);
            }
            break;

        case CmdAbout:
            ShowAboutDialog(m_hwnd);
            break;

        case CmdZoomOut:
            ZoomStep(false);
            break;

        case CmdZoomTo:
            PromptZoomTo();
            break;

        case CmdZoomIn:
            ZoomStep(true);
            break;

        case CmdZoomMax:
            SetMaximumZoom();
            break;

        case CmdFitWhole:
            SetZoomMode(ZoomMode::FitWhole);
            break;

        case CmdFitWidth:
            SetZoomMode(ZoomMode::FitWidth);
            break;

        case CmdActualSize:
            SetZoomMode(ZoomMode::ActualSize);
            break;

        case CmdResizeWindowToFit:
            ResizeWindowToFitImage(false, true);
            break;

        case CmdFullScreen:
            ToggleFullScreen();
            break;

        case CmdToggleToolbar:
            ToggleToolbar();
            break;

        case CmdToggleStatusbar:
            ToggleStatusbar();
            break;

        case CmdConfiguration:
            ConfigureViewerPreferences();
            break;

        case CmdClose:
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
            break;
        }
    }

    bool HandleKeyDown(WPARAM key)
    {
        const bool control = GetKeyState(VK_CONTROL) < 0;
        const bool shift = GetKeyState(VK_SHIFT) < 0;
        const bool plain = !control && !shift;

        if (control && (key == 'C' || key == VK_INSERT))
        {
            CopyImageToClipboard();
            return true;
        }

        if ((control && key == 'V') || (shift && key == VK_INSERT))
        {
            PasteImageFromClipboard();
            return true;
        }

        if (control && key == 'S')
        {
            SaveAs();
            return true;
        }

        if (control && key == 'O')
        {
            OpenAnotherFile();
            return true;
        }

        if (control && key == 'P')
        {
            PrintImage();
            return true;
        }

        if (control && key == 'R')
        {
            ReloadFromFile();
            return true;
        }

        if (control && key == 'A')
        {
            SelectWholeImage();
            return true;
        }

        if (control && key == 'D')
        {
            DeselectImage();
            return true;
        }

        if (control && key == 'H')
        {
            ShowHistogram();
            return true;
        }

        if (control && (key == VK_LEFT || key == VK_PRIOR))
        {
            ChangeFrame(-1);
            return true;
        }

        if (control && (key == VK_RIGHT || key == VK_NEXT))
        {
            ChangeFrame(1);
            return true;
        }

        if (control && key == VK_HOME)
        {
            NavigateToFrame(0);
            return true;
        }

        if (control && key == VK_END)
        {
            ShowLastFrame();
            return true;
        }

        if (plain && (key == VK_PRIOR || key == VK_NEXT) && !GetViewerPreferences().PageUpDownScrolls)
        {
            NavigatePageUpDownPreference(key == VK_PRIOR);
            return true;
        }

        if (!control && HandlePanKey(key, shift))
            return true;

        switch (key)
        {
        case VK_F1:
            HandleCommand(CmdHelpContents);
            return true;
        case VK_ESCAPE:
            // As the old viewer: a drag first, then the pipette, then a load, then the window.
            if (GetCapture() == m_hwnd)
                CancelDrag();
            else if (m_tool == ViewerTool::Pipette)
                HandleCommand(CmdToolHand);
            else if (m_loading)
                CancelPendingImageLoad();
            else
                PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
            return true;
        case VK_ADD:
            if (control && !shift)
            {
                HandleCommand(CmdZoomMax);
                return true;
            }
            [[fallthrough]];
        case VK_OEM_PLUS:
            ZoomStep(true);
            return true;
        case VK_SUBTRACT:
        case VK_OEM_MINUS:
            ZoomStep(false);
            return true;
        case VK_SPACE:
            if (HasSourceNavigation())
            {
                if (control && !shift)
                    NavigateSourceFile(ViewerSourceNavigationMode::NextSelected);
                else if (!control && shift)
                    NavigateSourceFile(ViewerSourceNavigationMode::Last);
                else if (!control && !shift)
                    NavigateSourceFile(ViewerSourceNavigationMode::Next);
                else
                    return false;
                return true;
            }
            else if (!control && !shift)
            {
                ChangeFrame(1);
                return true;
            }
            return false;
        case VK_BACK:
            if (HasSourceNavigation())
            {
                if (control && !shift)
                    NavigateSourceFile(ViewerSourceNavigationMode::PreviousSelected);
                else if (!control && shift)
                    NavigateSourceFile(ViewerSourceNavigationMode::First);
                else if (!control && !shift)
                    NavigateSourceFile(ViewerSourceNavigationMode::Previous);
                else
                    return false;
                return true;
            }
            else if (!control && !shift)
            {
                ChangeFrame(-1);
                return true;
            }
            return false;
        case VK_INSERT:
            if (!control && !shift)
                ToggleSourceSelection();
            return true;
        case VK_F2:
            if (!control && !shift)
            {
                RenameCurrentFile();
                return true;
            }
            return false;
        case VK_DELETE:
            if (!control)
            {
                DeleteCurrentFile(!shift);
                return true;
            }
            return false;
        case VK_NEXT:
            ChangeFrame(1);
            return true;
        case VK_PRIOR:
            ChangeFrame(-1);
            return true;
        case VK_HOME:
            NavigateToFrame(0);
            return true;
        case VK_END:
            ShowLastFrame();
            return true;
        case 'R':
            if (!plain)
                return false;
            ApplySurfaceTransform(ImageSurfaceTransform::Rotate90Clockwise);
            return true;
        case 'L':
            if (!plain)
                return false;
            ApplySurfaceTransform(ImageSurfaceTransform::Rotate90CounterClockwise);
            return true;
        case 'T':
            if (!plain)
                return false;
            ApplySurfaceTransform(ImageSurfaceTransform::Rotate180);
            return true;
        case 'H':
            if (!plain)
                return false;
            ApplySurfaceTransform(ImageSurfaceTransform::FlipHorizontal);
            return true;
        case 'V':
            if (!plain)
                return false;
            ApplySurfaceTransform(ImageSurfaceTransform::FlipVertical);
            return true;
        case 'X':
            if (!control && !shift)
            {
                CopyCurrentFileTo();
                return true;
            }
            return false;
        case 'S':
            if (!control && !shift)
            {
                SetActiveTool(ViewerTool::Select);
                return true;
            }
            return false;
        case 'Z':
            if (!control && !shift)
            {
                SetActiveTool(ViewerTool::Zoom);
                return true;
            }
            return false;
        case 'C':
            if (!plain)
                return false;
            ApplyCropSelection();
            return true;
        case 'P':
            if (!control && !shift)
            {
                TogglePipetteTool();
                return true;
            }
            return false;
        case 'A':
            if (!control && !shift)
            {
                SetActiveTool(ViewerTool::Hand);
                return true;
            }
            return false;
        case 'Q':
            if (!control && !shift)
            {
                BeginScreenCaptureWorkflow();
                return true;
            }
            return false;
        case 'F':
            if (!plain)
                return false;
            FocusSourceFile();
            return true;
        case 'G':
            if (!plain)
                return false;
            PromptGoToFrame();
            return true;
        case 'E':
            if (!plain)
                return false;
            ShowMetadataDetails();
            return true;
        case 'I':
            if (!plain)
                return false;
            ShowProperties();
            return true;
        case VK_F5:
            SetZoomMode(ZoomMode::FitWhole);
            return true;
        case VK_F6:
            SetZoomMode(ZoomMode::FitWidth);
            return true;
        case VK_F7:
            SetZoomMode(ZoomMode::ActualSize);
            return true;
        case VK_F8:
            ResizeWindowToFitImage(false, true);
            return true;
        case VK_MULTIPLY:
        case 'M':
            if (!plain)
                return false;
            PromptZoomTo();
            return true;
        case VK_F9:
            ToggleToolbar();
            return true;
        case VK_F3:
            ShowProperties();
            return true;
        case VK_F11:
            ToggleFullScreen();
            return true;
        default:
            return false;
        }
    }

    void ChangeFrame(int delta)
    {
        const uint32_t count = FrameCount();
        if (count == 0)
            return;

        const int next = static_cast<int>(m_currentFrame) + delta;
        if (next < 0 || next >= static_cast<int>(count))
            return;

        NavigateToFrame(static_cast<uint32_t>(next));
    }

    void NavigateToFrame(uint32_t frameIndex)
    {
        if (frameIndex >= FrameCount() || frameIndex == m_currentFrame)
            return;

        StopAnimation();
        ShowFrame(frameIndex);
    }

    void ShowLastFrame()
    {
        const uint32_t count = FrameCount();
        if (count == 0)
            return;

        NavigateToFrame(count - 1);
    }

    void PromptGoToFrame()
    {
        const uint32_t count = FrameCount();
        if (count < 2)
            return;

        uint32_t frameIndex = m_currentFrame;
        bool selected = false;
        if (m_hostActions.ChooseFrameIndex != nullptr)
        {
            bool canceled = false;
            selected = m_hostActions.ChooseFrameIndex(m_hostActions.Context,
                                                      m_hwnd,
                                                      count,
                                                      m_currentFrame,
                                                      frameIndex,
                                                      canceled) &&
                       !canceled;
        }
        else
        {
            selected = PromptForFrameNumber(m_hwnd, count, m_currentFrame, frameIndex);
        }

        if (selected && frameIndex < count)
            NavigateToFrame(frameIndex);
    }

    bool IsGifDocument() const
    {
        return IsEqualGUID(m_metadata.ContainerFormat, GUID_ContainerFormatGif) != FALSE;
    }

    bool CanAnimate() const
    {
        // Frames of any size: they are composed on the logical screen (ComposeDocumentFrame).
        return IsGifDocument() && FrameCount() >= 2 && !m_surface.Pixels.empty() && !m_metadata.Frames.empty() &&
               !m_surfaceModified;
    }

    UINT FrameDelayMilliseconds(uint32_t frameIndex) const
    {
        UINT delay = DefaultAnimationDelayMs;
        if (frameIndex < m_metadata.Frames.size())
        {
            const ImageFrameInfo& frame = m_metadata.Frames[frameIndex];
            if (frame.HasAnimationDelay && frame.AnimationDelayMilliseconds > 0)
                delay = static_cast<UINT>(std::min<uint32_t>(frame.AnimationDelayMilliseconds, MaximumAnimationDelayMs));
        }

        if (delay < MinimumAnimationDelayMs)
            delay = DefaultAnimationDelayMs;
        return delay;
    }

    void StartAnimation()
    {
        if (!CanAnimate())
            return;

        // Playback composes from the first frame, so offsets and disposal build up correctly.
        if (!ShowAnimationFrame(0))
            return;
        m_animationPlaying = true;
        if (!ScheduleAnimationTimer())
        {
            m_animationPlaying = false;
            ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_UNABLE_TO_START_ANIMATED_GIF), MB_OK | MB_ICONERROR);
            return;
        }

        UpdateStatus();
        UpdateCommands();
    }

    void StopAnimation(bool updateUi = true)
    {
        if (m_hwnd != nullptr)
            KillTimer(m_hwnd, AnimationTimerId);

        const bool wasPlaying = m_animationPlaying;
        m_animationPlaying = false;
        if (updateUi && wasPlaying)
        {
            UpdateStatus();
            UpdateCommands();
        }
    }

    void ToggleAnimation()
    {
        if (!CanAnimate())
            return;

        if (m_animationPlaying)
            StopAnimation();
        else
            StartAnimation();
    }

    bool ScheduleAnimationTimer()
    {
        if (m_hwnd == nullptr || !m_animationPlaying)
            return false;

        KillTimer(m_hwnd, AnimationTimerId);
        return SetTimer(m_hwnd, AnimationTimerId, FrameDelayMilliseconds(m_currentFrame), nullptr) != 0;
    }

    void AdvanceAnimationFrame()
    {
        if (!m_animationPlaying)
            return;

        const uint32_t count = FrameCount();
        if (!CanAnimate() || count < 2)
        {
            StopAnimation();
            return;
        }

        const uint32_t next = (m_currentFrame + 1) % count;
        if (!ShowAnimationFrame(next))
        {
            StopAnimation();
            return;
        }

        if (!ScheduleAnimationTimer())
            StopAnimation();
    }

    // The composed animation frame 'frameIndex' (0 restarts the canvas).
    bool ShowAnimationFrame(uint32_t frameIndex)
    {
        std::wstring error;
        if (!EnsureDocumentOpen(error))
            return false;
        const ImageError composed = ComposeDocumentFrame(*m_document, frameIndex, m_animation);
        if (!composed.Succeeded())
            return false;
        m_surface = m_animation.Surface;
        m_currentFrame = frameIndex;
        UpdateTitle();
        UpdateStatus();
        UpdateCommands();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return true;
    }

    bool ShowFrame(uint32_t frameIndex)
    {
        if (frameIndex >= FrameCount())
            return false;

        const uint32_t previous = m_currentFrame;
        m_currentFrame = frameIndex;
        std::wstring error;
        if (!DecodeCurrentFrame(error))
        {
            m_currentFrame = previous;
            ViewerMessageBox(m_hwnd, error.c_str(), MB_OK | MB_ICONERROR);
            return false;
        }

        UpdateTitle();
        UpdateStatus();
        UpdateCommands();
        InvalidateRect(m_hwnd, nullptr, TRUE);
        return true;
    }

    // The factor from the current zoom to the next ladder level.
    double ZoomStepFactor(bool zoomIn) const
    {
        const double current = static_cast<double>(CurrentZoomPercent()) / 100.0;
        const double scale = current > 0 ? current : 1.0;
        return NextZoomLevel(scale, zoomIn) / scale;
    }

    void ZoomStep(bool zoomIn)
    {
        RECT content = m_content;
        const int viewWidth = std::max<LONG>(1, content.right - content.left);
        const int viewHeight = std::max<LONG>(1, content.bottom - content.top);
        const double current = EffectiveScale(viewWidth, viewHeight);
        m_zoomMode = ZoomMode::Manual;
        m_manualZoom = NextZoomLevel(current, zoomIn);
        ClampPanOffsets();
        UpdateStatus();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    void SetMaximumZoom()
    {
        if (m_surface.Pixels.empty() || m_loading)
            return;

        m_zoomMode = ZoomMode::Manual;
        m_manualZoom = ClampManualZoom(static_cast<double>(MaximumManualZoomPercent) / 100.0);
        ClampPanOffsets();
        UpdateStatus();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    void SetManualZoomPercent(double percent)
    {
        if (m_surface.Pixels.empty() || m_loading)
            return;
        percent = (std::max)(static_cast<double>(MinimumManualZoomPercent), (std::min)(percent, static_cast<double>(MaximumManualZoomPercent)));
        m_zoomMode = ZoomMode::Manual;
        m_manualZoom = percent / 100.0;
        ClampPanOffsets();
        UpdateStatus();
        UpdateCommands();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    void PromptZoomTo()
    {
        if (m_surface.Pixels.empty() || m_loading)
            return;

        RECT content = m_content;
        const int viewWidth = std::max<LONG>(1, content.right - content.left);
        const int viewHeight = std::max<LONG>(1, content.bottom - content.top);
        const int currentPercent = static_cast<int>(std::lround(EffectiveScale(viewWidth, viewHeight) * 100.0));

        int selectedPercent = currentPercent;
        bool selected = false;
        bool canceled = false;
        if (m_hostActions.ChooseZoomPercent != nullptr)
        {
            selected = m_hostActions.ChooseZoomPercent(m_hostActions.Context,
                                                       m_hwnd,
                                                       currentPercent,
                                                       selectedPercent,
                                                       canceled) &&
                       !canceled;
        }
        else
        {
            selected = PromptForZoomPercent(m_hwnd, currentPercent, selectedPercent);
        }

        if (!selected ||
            selectedPercent < MinimumManualZoomPercent ||
            selectedPercent > MaximumManualZoomPercent)
            return;

        m_zoomMode = ZoomMode::Manual;
        m_manualZoom = static_cast<double>(selectedPercent) / 100.0;
        ClampPanOffsets();
        UpdateStatus();
        UpdateCommands();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    void SetZoomMode(ZoomMode mode)
    {
        m_zoomMode = mode;
        m_panX = 0;
        m_panY = 0;
        UpdateStatus();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    bool HasSourceNavigation() const
    {
        return m_sourceNavigation.FindFile != nullptr &&
               m_sourceNavigation.SourceUID != -1 &&
               !m_sourceNavigation.CurrentPath.empty();
    }

    bool HasSourceSelection() const
    {
        return m_sourceNavigation.ToggleSelection != nullptr &&
               m_sourceNavigation.SourceUID != -1 &&
               !m_sourceNavigation.CurrentPath.empty();
    }

    // The file Focus shows in Sally's panel: the navigation's, else the opened one.
    const std::wstring& FocusPath() const
    {
        return !m_sourceNavigation.CurrentPath.empty() ? m_sourceNavigation.CurrentPath : m_path;
    }

    bool HasSourceFocus() const
    {
        return m_sourceNavigation.FocusCurrentFile != nullptr && !FocusPath().empty();
    }

    bool CanDeleteCurrentFile() const
    {
        return m_hostActions.DeleteFile != nullptr && !m_path.empty();
    }

    bool CanRenameCurrentFile() const
    {
        return m_hostActions.RenameFile != nullptr && !m_path.empty();
    }

    bool CanCopyCurrentFileTo() const
    {
        return m_hostActions.CopyFile != nullptr && !m_path.empty();
    }

    void ClearSourceNavigation()
    {
        m_sourceNavigation.SourceUID = -1;
        m_sourceNavigation.CurrentIndex = -1;
        m_sourceNavigation.CurrentPath.clear();
        m_hasPendingSourceFile = false;
        m_hasActiveSourceFile = false;
    }

    void SetStatusMessage(const std::wstring& message)
    {
        if (m_status != nullptr)
            SendMessageW(m_status, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(message.c_str()));
    }

    void NavigateSourceFile(ViewerSourceNavigationMode mode)
    {
        if (m_loading || !HasSourceNavigation())
            return;

        ViewerSourceFile sourceFile;
        bool noMoreFiles = false;
        bool sourceBusy = false;
        if (!m_sourceNavigation.FindFile(m_sourceNavigation.Context,
                                         m_sourceNavigation.SourceUID,
                                         m_sourceNavigation.CurrentIndex,
                                         m_sourceNavigation.CurrentPath.c_str(),
                                         mode,
                                         sourceFile,
                                         noMoreFiles,
                                         sourceBusy))
        {
            if (sourceBusy)
                SetStatusMessage(ViewerText(IDS_SAL_IS_BUSY));
            else if (noMoreFiles)
            {
                SetStatusMessage(ViewerText(IDS_NO_MORE_IMAGES));
            }
            else
                SetStatusMessage(ViewerText(IDS_PV_SOURCE_NAVIGATION_IS_NO_LONGER));
            return;
        }

        if (sourceFile.Path.empty())
            return;

        StopAnimation(false);
        m_pendingSourceFile = std::move(sourceFile);
        m_hasPendingSourceFile = true;
        m_pendingLoadClearsSourceNavigation = false;
        m_pendingLoadPath = m_pendingSourceFile.Path;
        m_pendingLoadPreserveImage = !m_surface.Pixels.empty();
        PostMessageW(m_hwnd, WM_PICTVIEW_LOAD_IMAGE, 0, 0);
    }

    void ToggleSourceSelection()
    {
        if (m_loading || !HasSourceSelection())
            return;

        bool selected = false;
        bool sourceBusy = false;
        if (!m_sourceNavigation.ToggleSelection(m_sourceNavigation.Context,
                                                m_sourceNavigation.SourceUID,
                                                m_sourceNavigation.CurrentIndex,
                                                m_sourceNavigation.CurrentPath.c_str(),
                                                selected,
                                                sourceBusy))
        {
            SetStatusMessage(sourceBusy ? ViewerText(IDS_SAL_IS_BUSY) : ViewerText(IDS_PV_SOURCE_SELECTION_IS_NO_LONGER));
            return;
        }

        m_sourceFileSelected = selected;
        UpdateToolbarChecks();
        SetStatusMessage(selected ? ViewerText(IDS_PV_OPENED_FILE_IS_SELECTED_IN) : ViewerText(IDS_PV_OPENED_FILE_IS_UNSELECTED_IN));
    }

    void RenameCurrentFile()
    {
        if (m_loading || !CanRenameCurrentFile())
            return;

        if (m_fullScreen)
            ToggleFullScreen();

        const std::wstring oldName = FileNameFromPath(m_path);
        std::wstring newName = oldName;
        if (m_hostActions.ChooseRenameName != nullptr)
        {
            bool canceled = false;
            if (!m_hostActions.ChooseRenameName(m_hostActions.Context,
                                                m_hwnd,
                                                oldName.c_str(),
                                                newName,
                                                canceled) ||
                canceled ||
                newName.empty())
            {
                return;
            }
        }
        else
        {
            if (!PromptForRenameFileName(m_hwnd, oldName, m_hostActions.SelectWholeNameOnRename, newName))
                return;
        }

        if (newName == oldName)
        {
            SetStatusMessage(ViewerText(IDS_PV_FILE_NAME_UNCHANGED));
            return;
        }

        StopAnimation(false);

        std::wstring renamedPath;
        bool renamed = false;
        DWORD renameError = ERROR_SUCCESS;
        if (!m_hostActions.RenameFile(m_hostActions.Context,
                                      m_hwnd,
                                      m_path.c_str(),
                                      newName.c_str(),
                                      renamedPath,
                                      renamed,
                                      renameError))
        {
            ViewerMessageBox(m_hwnd, LastWin32ErrorText(ViewerText(IDS_PV_UNABLE_TO_RENAME_THE_FILE), renameError).c_str(), MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
            return;
        }

        if (!renamed)
        {
            SetStatusMessage(ViewerText(IDS_PV_RENAME_CANCELED));
            return;
        }

        if (renamedPath.empty())
        {
            SetStatusMessage(ViewerText(IDS_PV_FILE_WAS_RENAMED_BUT_THE));
            return;
        }

        NotifyPathChanged(m_path);
        m_path = std::move(renamedPath);
        m_displayName = FileNameFromPath(m_path);
        // Next/Previous continue from the renamed file (same enumeration source and index).
        if (HasSourceNavigation())
            m_sourceNavigation.CurrentPath = m_path;
        UpdateTitle();
        UpdateStatus();
        UpdateCommands();
        SetStatusMessage(ViewerText(IDS_PV_FILE_RENAMED));
    }

    void CopyCurrentFileTo()
    {
        if (m_loading || !CanCopyCurrentFileTo())
            return;

        if (m_fullScreen)
            ToggleFullScreen();

        std::wstring targetPath;
        if (m_hostActions.ChooseCopyTargetPath != nullptr)
        {
            bool canceled = false;
            if (!m_hostActions.ChooseCopyTargetPath(m_hostActions.Context,
                                                    m_hwnd,
                                                    m_path.c_str(),
                                                    targetPath,
                                                    canceled) ||
                canceled ||
                targetPath.empty())
            {
                return;
            }
        }
        else
        {
            if (!PromptForCopyToTargetPath(m_hwnd, m_path, targetPath))
                return;
        }

        bool copied = false;
        bool canceled = false;
        if (!m_hostActions.CopyFile(m_hostActions.Context,
                                    m_hwnd,
                                    m_path.c_str(),
                                    targetPath.c_str(),
                                    copied,
                                    canceled))
        {
            SetStatusMessage(canceled ? ViewerText(IDS_PV_COPY_CANCELED) : ViewerText(IDS_PV_UNABLE_TO_COPY_THE_FILE));
            return;
        }

        if (copied)
            NotifyPathChanged(targetPath);
        SetStatusMessage(copied ? ViewerText(IDS_PV_FILE_COPIED) : ViewerText(IDS_PV_COPY_WAS_NOT_COMPLETED));
    }

    void DeleteCurrentFile(bool recycle)
    {
        if (m_loading || !CanDeleteCurrentFile())
            return;

        if (m_fullScreen)
            ToggleFullScreen();

        StopAnimation(false);

        bool deleted = false;
        bool canceled = false;
        if (!m_hostActions.DeleteFile(m_hostActions.Context,
                                      m_hwnd,
                                      m_path.c_str(),
                                      recycle,
                                      deleted,
                                      canceled))
        {
            SetStatusMessage(ViewerText(IDS_PV_UNABLE_TO_DELETE_THE_FILE));
            return;
        }

        if (deleted)
        {
            NotifyPathChanged(m_path);
            m_path.clear();
            m_activeLoadPath.clear();
            m_displayName = ViewerText(IDS_DELETED_TITLE);
            UpdateTitle();
            UpdateStatus();
            UpdateCommands();
            SetStatusMessage(recycle ? ViewerText(IDS_PV_FILE_MOVED_TO_THE_RECYCLE) : ViewerText(IDS_PV_FILE_DELETED));
            return;
        }

        SetStatusMessage(canceled ? ViewerText(IDS_PV_DELETE_CANCELED) : ViewerText(IDS_PV_FILE_WAS_NOT_DELETED));
    }

    void FocusSourceFile()
    {
        if (m_loading || !HasSourceFocus())
            return;

        bool sourceBusy = false;
        if (m_fullScreen)
            ToggleFullScreen();

        if (!m_sourceNavigation.FocusCurrentFile(m_sourceNavigation.Context,
                                                 FocusPath().c_str(),
                                                 sourceBusy))
        {
            if (sourceBusy)
                ViewerMessageBox(m_hwnd, ViewerText(IDS_SAL_IS_BUSY), MB_OK | MB_ICONINFORMATION); // as the old viewer did
            else
                SetStatusMessage(ViewerText(IDS_PV_SOURCE_FOCUS_IS_NO_LONGER));
            return;
        }

        SetStatusMessage(ViewerText(IDS_PV_FOCUSING_OPENED_FILE_IN_THE));
    }

    bool SaveCurrentSurfaceForWallpaper(const std::wstring& wallpaperPath)
    {
        if (m_surface.Pixels.empty())
            return false;

        std::wstring engineError;
        if (!EnsureEngine(engineError))
        {
            ViewerMessageBox(m_hwnd, engineError.c_str(), MB_OK | MB_ICONERROR);
            return false;
        }

        SaveOptions options;
        options.Format = ImageSaveFormat::Bmp;
        SaveImageResult save = m_engine->SaveSurfaceToPath(m_surface, wallpaperPath.c_str(), options);
        if (!save.Succeeded())
        {
            ViewerMessageBox(m_hwnd, ImageErrorText(save.Error).c_str(), MB_OK | MB_ICONERROR);
            return false;
        }
        return true;
    }

    bool WallpaperNeedsSurface(ViewerWallpaperMode mode) const
    {
        return mode == ViewerWallpaperMode::Center ||
               mode == ViewerWallpaperMode::Tile ||
               mode == ViewerWallpaperMode::Stretch;
    }

    std::wstring StatusForWallpaperMode(ViewerWallpaperMode mode) const
    {
        switch (mode)
        {
        case ViewerWallpaperMode::Restore:
            return ViewerText(IDS_PV_PREVIOUS_WALLPAPER_RESTORED);
        case ViewerWallpaperMode::None:
            return ViewerText(IDS_PV_WALLPAPER_CLEARED);
        case ViewerWallpaperMode::Center:
        case ViewerWallpaperMode::Tile:
        case ViewerWallpaperMode::Stretch:
        default:
            return ViewerText(IDS_PV_WALLPAPER_UPDATED);
        }
    }

    void SetAsWallpaper(ViewerWallpaperMode mode)
    {
        if (m_loading)
            return;

        if (WallpaperNeedsSurface(mode) && m_surface.Pixels.empty())
        {
            ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_NO_IMAGE_IS_LOADED), MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (m_hostActions.ApplyWallpaper != nullptr)
        {
            std::wstring wallpaperPath;
            if (WallpaperNeedsSurface(mode))
            {
                if (m_hostActions.ChooseWallpaperPath != nullptr)
                {
                    if (!m_hostActions.ChooseWallpaperPath(m_hostActions.Context, m_hwnd, wallpaperPath) ||
                        wallpaperPath.empty())
                    {
                        ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_UNABLE_TO_CREATE_A_WALLPAPER), MB_OK | MB_ICONERROR);
                        return;
                    }
                }
                else
                {
                    wallpaperPath = WallpaperOutputPath();
                    if (wallpaperPath.empty())
                    {
                        ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_UNABLE_TO_CREATE_A_WALLPAPER), MB_OK | MB_ICONERROR);
                        return;
                    }
                }

                if (!SaveCurrentSurfaceForWallpaper(wallpaperPath))
                    return;
            }

            bool applied = false;
            if (!m_hostActions.ApplyWallpaper(m_hostActions.Context,
                                              m_hwnd,
                                              mode,
                                              wallpaperPath.empty() ? nullptr : wallpaperPath.c_str(),
                                              applied) ||
                !applied)
            {
                ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_UNABLE_TO_UPDATE_THE_DESKTOP), MB_OK | MB_ICONERROR);
                return;
            }

            SetStatusMessage(StatusForWallpaperMode(mode));
            return;
        }

        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, WallpaperRegistryKey, 0, KEY_READ | KEY_WRITE, &key) != ERROR_SUCCESS)
        {
            ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_UNABLE_TO_OPEN_THE_DESKTOP), MB_OK | MB_ICONERROR);
            return;
        }

        bool ok = false;
        std::wstring status;
        switch (mode)
        {
        case ViewerWallpaperMode::Center:
        case ViewerWallpaperMode::Tile:
        case ViewerWallpaperMode::Stretch:
        {
            const std::wstring wallpaperPath = WallpaperOutputPath();
            if (wallpaperPath.empty())
            {
                ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_UNABLE_TO_CREATE_A_WALLPAPER), MB_OK | MB_ICONERROR);
                break;
            }

            if (!SaveCurrentSurfaceForWallpaper(wallpaperPath))
                break;

            WallpaperState current = ReadWallpaperState(key, false);
            if (!WideEqualsIgnoreCase(current.Wallpaper, wallpaperPath))
                WriteWallpaperState(key, true, current);

            current.Wallpaper = wallpaperPath;
            current.WallpaperStyle = mode == ViewerWallpaperMode::Stretch ? L"2" : L"0";
            current.TileWallpaper = mode == ViewerWallpaperMode::Tile ? L"1" : L"0";

            ok = WriteWallpaperState(key, false, current) && NotifyWallpaperChanged(wallpaperPath);
            status = ViewerText(IDS_PV_WALLPAPER_UPDATED);
            break;
        }

        case ViewerWallpaperMode::Restore:
        {
            WallpaperState current = ReadWallpaperState(key, false);
            WallpaperState previous = ReadWallpaperState(key, true);
            ok = WriteWallpaperState(key, true, current) &&
                 WriteWallpaperState(key, false, previous) &&
                 NotifyWallpaperChanged(previous.Wallpaper);
            status = ViewerText(IDS_PV_PREVIOUS_WALLPAPER_RESTORED);
            break;
        }

        case ViewerWallpaperMode::None:
        {
            WallpaperState current = ReadWallpaperState(key, false);
            WallpaperState empty;
            ok = WriteWallpaperState(key, true, current) &&
                 WriteWallpaperState(key, false, empty) &&
                 NotifyWallpaperChanged(std::wstring());
            status = ViewerText(IDS_PV_WALLPAPER_CLEARED);
            break;
        }
        }

        RegCloseKey(key);
        if (!ok)
        {
            ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_UNABLE_TO_UPDATE_THE_DESKTOP), MB_OK | MB_ICONERROR);
            return;
        }

        SetStatusMessage(status.c_str());
    }

    void RecordRecentPath(const std::wstring& path)
    {
        if (path.empty())
            return;

        const std::wstring directory = DirectoryFromPath(path);
        {
            std::lock_guard<std::mutex> lock(RecentHistoryMutex);
            AddRecentEntry(RecentFiles, path);
            AddRecentEntry(RecentDirectories, directory);
        }
        RefreshMainMenu();
    }

    void QueueFileLoad(std::wstring selectedPath, bool preserveImage, bool clearSourceNavigation)
    {
        if (selectedPath.empty())
            return;

        StopAnimation(false);
        m_pendingLoadPath = std::move(selectedPath);
        m_pendingLoadPreserveImage = preserveImage;
        m_pendingLoadClearsSourceNavigation = clearSourceNavigation;
        m_hasPendingSourceFile = false;
        PostMessageW(m_hwnd, WM_PICTVIEW_LOAD_IMAGE, 0, 0);
    }

    void OpenAnotherFile(const std::wstring& initialDirectory = std::wstring())
    {
        if (m_loading)
            return;

        std::wstring selectedPath;
        if (m_hostActions.ChooseOpenPath != nullptr)
        {
            bool canceled = false;
            if (!m_hostActions.ChooseOpenPath(m_hostActions.Context,
                                              m_hwnd,
                                              initialDirectory.empty() ? m_path.c_str() : initialDirectory.c_str(),
                                              selectedPath,
                                              canceled) ||
                canceled ||
                selectedPath.empty())
            {
                return;
            }
        }
        else
        {
            const std::wstring initialDir = initialDirectory.empty() ? DirectoryFromPath(m_path) : initialDirectory;
            selectedPath = initialDirectory.empty() ? FileNameFromPath(m_path) : std::wstring();

            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = m_hwnd;
            std::wstring filter = ViewerText(IDS_PV_IMAGE_FILES);
            filter.push_back(L'\0');
            filter += OpenImagePatterns;
            if (const std::wstring extra = GetAdditionalOpenMasks(); !extra.empty())
                filter += L";" + extra;
            filter.push_back(L'\0');
            filter += ViewerText(IDS_PV_ALL_FILES);
            filter.push_back(L'\0');
            filter += L"*.*";
            filter.push_back(L'\0');
            ofn.lpstrFilter = filter.c_str();
            ofn.nFilterIndex = 1;
            ofn.lpstrInitialDir = initialDir.empty() ? nullptr : initialDir.c_str();
            ofn.lpstrTitle = ViewerText(IDS_PV_OPEN_IMAGE);
            ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_LONGNAMES;

            if (!RunFileNameDialog(ofn, false, selectedPath))
            {
                const DWORD dialogError = CommDlgExtendedError();
                if (dialogError != 0)
                {
                    ViewerMessageBox(m_hwnd, FormatViewerText(IDS_PV_FILE_DIALOG_FAILED, dialogError).c_str(),
                                     MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
                }
                return;
            }

            if (selectedPath.empty())
                return;
        }

        QueueFileLoad(std::move(selectedPath), !m_surface.Pixels.empty(), true);
    }

    void OpenRecentFile(std::size_t index)
    {
        if (m_loading || index >= ViewerRecentHistoryEntryCount)
            return;

        const ViewerRecentHistory history = GetRecentHistory();
        if (history.Files[index].empty())
            return;

        QueueFileLoad(history.Files[index], !m_surface.Pixels.empty(), true);
    }

    void OpenRecentDirectory(std::size_t index)
    {
        if (m_loading || index >= ViewerRecentHistoryEntryCount)
            return;

        const ViewerRecentHistory history = GetRecentHistory();
        if (history.Directories[index].empty())
            return;

        const std::wstring directory = history.Directories[index];
        if (GetFileAttributesW(directory.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            RemoveRecentDirectory(directory);
            OpenAnotherFile();
            return;
        }
        OpenAnotherFile(directory);
    }

    void ReloadFromFile()
    {
        if (m_loading || m_path.empty())
            return;

        StopAnimation(false);
        m_pendingLoadPath = m_path;
        m_pendingLoadPreserveImage = true;
        m_pendingLoadClearsSourceNavigation = false;
        m_hasPendingSourceFile = false;
        PostMessageW(m_hwnd, WM_PICTVIEW_LOAD_IMAGE, 0, 0);
    }

    void ShowHistogram()
    {
        if (m_surface.Pixels.empty() || m_loading)
            return;

        ImageHistogramResult histogram = ComputeSurfaceHistogram(m_surface);
        if (!histogram.Succeeded())
        {
            ViewerMessageBox(m_hwnd, ImageErrorText(histogram.Error).c_str(), MB_OK | MB_ICONERROR);
            return;
        }

        if (!ShowHistogramWindow(m_hwnd, histogram.Histogram, m_grayImages, m_hotImages))
            ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_UNABLE_TO_OPEN_THE_HISTOGRAM), MB_OK | MB_ICONERROR);
    }

    void SetUnsavedSurface(std::wstring displayName, std::wstring containerName, ImageSurface&& surface)
    {
        StopAnimation(false);
        CancelPendingImageLoad();
        m_path.clear();
        m_activeLoadPath.clear();
        m_displayName = std::move(displayName);
        m_sourceBytes.clear();
        m_sourceStream.Reset();
        m_document.reset();
        ClearSourceNavigation();
        m_metadata = ImageMetadata();
        m_metadata.ContainerName = std::move(containerName);
        ImageFrameInfo frame;
        frame.Width = surface.Width;
        frame.Height = surface.Height;
        frame.DpiX = 96.0;
        frame.DpiY = 96.0;
        m_metadata.Frames.push_back(frame);
        m_surface = std::move(surface);
        m_surfaceModified = true;
        m_currentFrame = 0;
        m_panX = 0;
        m_panY = 0;
        m_zoomMode = ZoomMode::FitWhole;
        m_manualZoom = 1.0;
        m_animationPlaying = false;
        ClearSelection();
        ClearPipetteSample();
        ApplyInitialFullScreenPreference();
        UpdateTitle();
        UpdateStatus();
        UpdateCommands();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    ViewerCaptureOptions CurrentCaptureOptions() const
    {
        ViewerCaptureOptions options;
        options.Scope = m_captureScope;
        options.Trigger = m_captureTrigger;
        options.HotKey = SanitizeCaptureHotKey(m_captureHotKey);
        options.TimerSeconds = SanitizeCaptureTimerSeconds(m_captureTimerSeconds);
        options.IncludeCursor = m_captureCursor;
        return options;
    }

    void ApplyCaptureOptions(const ViewerCaptureOptions& options)
    {
        m_captureScope = options.Scope;
        m_captureTrigger = options.Trigger;
        m_captureHotKey = SanitizeCaptureHotKey(options.HotKey);
        m_captureTimerSeconds = SanitizeCaptureTimerSeconds(options.TimerSeconds);
        m_captureCursor = options.IncludeCursor;

        ViewerPreferences preferences = GetViewerPreferences();
        preferences.CaptureScope = m_captureScope;
        preferences.CaptureTrigger = m_captureTrigger;
        preferences.CaptureHotKey = m_captureHotKey;
        preferences.CaptureTimerSeconds = m_captureTimerSeconds;
        preferences.CaptureCursor = m_captureCursor;
        SetViewerPreferences(preferences);
    }

    bool PromptForCaptureOptionsForViewer(ViewerCaptureOptions& options)
    {
        return PromptForCaptureOptions(m_hwnd, options);
    }

    void StopPendingScreenCapture()
    {
        if (!m_capturePending)
            return;

        if (m_capturePendingTrigger == ViewerCaptureTrigger::HotKey)
            UnregisterHotKey(m_hwnd, CaptureHotKeyId);
        else
            KillTimer(m_hwnd, CaptureTimerId);
        m_capturePending = false;
    }

    void CancelPendingScreenCapture()
    {
        if (!m_capturePending)
            return;

        StopPendingScreenCapture();
        if (m_surface.Pixels.empty() && m_path.empty())
        {
            DestroyWindow(m_hwnd);
            return;
        }
        ShowWindow(m_hwnd, SW_RESTORE);
        SetForegroundWindow(m_hwnd);
        UpdateTitle();
        SetStatusMessage(ViewerText(IDS_CAPTURECANCELED));
    }

    void CompletePendingScreenCapture()
    {
        if (!m_capturePending)
            return;

        StopPendingScreenCapture();
        PerformScreenCapture();
        ShowWindow(m_hwnd, SW_RESTORE);
        SetForegroundWindow(m_hwnd);
        UpdateWindow(m_hwnd);
    }

    void BeginScreenCaptureWorkflow()
    {
        if (m_loading)
            return;

        if (m_capturePending)
        {
            CancelPendingScreenCapture();
            return;
        }

        ViewerCaptureOptions options = CurrentCaptureOptions();
        if (!PromptForCaptureOptionsForViewer(options))
        {
            if (m_surface.Pixels.empty() && m_path.empty())
            {
                DestroyWindow(m_hwnd); // opened from the Plugins menu only to capture
                return;
            }
            SetStatusMessage(ViewerText(IDS_CAPTURECANCELED));
            return;
        }

        ApplyCaptureOptions(options);

        bool armed = false;
        if (m_captureTrigger == ViewerCaptureTrigger::HotKey)
        {
            const UINT vk = RegisterHotKeyVirtualKeyFromControl(m_captureHotKey);
            const UINT modifiers = RegisterHotKeyModifiersFromControl(m_captureHotKey);
            armed = vk != 0 && RegisterHotKey(m_hwnd, CaptureHotKeyId, modifiers, vk) != 0;
            m_capturePendingTrigger = ViewerCaptureTrigger::HotKey;
        }
        else
        {
            armed = SetTimer(m_hwnd, CaptureTimerId, static_cast<UINT>(m_captureTimerSeconds) * 1000u, nullptr) != 0;
            m_capturePendingTrigger = ViewerCaptureTrigger::Timer;
        }

        if (!armed)
        {
            SetStatusMessage(m_captureTrigger == ViewerCaptureTrigger::HotKey
                                 ? ViewerText(IDS_PV_UNABLE_TO_REGISTER_THE_SCREEN)
                                 : ViewerText(IDS_PV_UNABLE_TO_START_THE_SCREEN));
            return;
        }

        m_capturePending = true;
        m_title.clear(); // UpdateTitle puts the real title back afterwards
        SetWindowTextW(m_hwnd, ViewerText(IDS_PV_CAPTURING_PICTVIEW));
        ShowWindow(m_hwnd, SW_MINIMIZE);
        if (m_captureTrigger == ViewerCaptureTrigger::HotKey)
            SetStatusMessage(ViewerText(IDS_PV_SCREEN_CAPTURE_IS_WAITING_FOR));
        else
            SetStatusMessage(ViewerText(IDS_PV_SCREEN_CAPTURE_TIMER_STARTED));
    }

    void PerformScreenCapture()
    {
        if (m_loading)
            return;

        const bool hideViewerForCapture = m_captureScope == ViewerCaptureScope::Desktop ||
                                          m_captureScope == ViewerCaptureScope::VirtualScreen;
        const bool wasVisible = hideViewerForCapture && IsWindowVisible(m_hwnd) != FALSE;
        if (wasVisible)
        {
            ShowWindow(m_hwnd, SW_HIDE);
            Sleep(0);
        }

        CaptureImageResult captured = CaptureScreenScopeSurface(m_captureScope, m_captureCursor);

        if (wasVisible)
            ShowWindow(m_hwnd, SW_SHOW);

        if (!captured.Success)
        {
            CaptureImageResult fallback = CaptureWindowSurface(m_hwnd);
            if (fallback.Success)
            {
                SetUnsavedSurface(ViewerText(IDS_CAPTURE_TITLE), ViewerText(IDS_CAPTURE_TITLE), std::move(fallback.Surface));
                SetStatusMessage(ViewerText(IDS_PV_SCREEN_CAPTURED_FROM_THE_VIEWER));
                return;
            }

            ViewerMessageBox(m_hwnd, captured.ErrorText.empty() ? ViewerText(IDS_PV_UNABLE_TO_CAPTURE_THE_SCREEN_4) : captured.ErrorText.c_str(), MB_OK | MB_ICONERROR);
            return;
        }

        SetUnsavedSurface(ViewerText(IDS_CAPTURE_TITLE), ViewerText(IDS_CAPTURE_TITLE), std::move(captured.Surface));
        SetStatusMessage(ViewerText(IDS_PV_SCREEN_CAPTURED));
    }

    void PasteImageFromClipboard()
    {
        if (m_loading)
            return;

        if (!ClipboardContainsImage())
        {
            ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_THE_CLIPBOARD_DOES_NOT_CONTAIN), MB_OK | MB_ICONINFORMATION);
            return;
        }

        if (!OpenClipboard(m_hwnd))
        {
            ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_UNABLE_TO_OPEN_THE_CLIPBOARD), MB_OK | MB_ICONERROR);
            return;
        }

        ClipboardImageResult pasted;
        if (IsClipboardFormatAvailable(CF_DIBV5))
        {
            pasted = SurfaceFromDibMemory(static_cast<HGLOBAL>(GetClipboardData(CF_DIBV5)));
        }
        else if (IsClipboardFormatAvailable(CF_DIB))
        {
            pasted = SurfaceFromDibMemory(static_cast<HGLOBAL>(GetClipboardData(CF_DIB)));
        }
        else if (IsClipboardFormatAvailable(CF_BITMAP))
        {
            pasted = SurfaceFromBitmapHandle(static_cast<HBITMAP>(GetClipboardData(CF_BITMAP)));
        }
        CloseClipboard();

        if (!pasted.Success)
        {
            ViewerMessageBox(m_hwnd, pasted.ErrorText.empty() ? ViewerText(IDS_PV_UNABLE_TO_PASTE_THE_CLIPBOARD) : pasted.ErrorText.c_str(), MB_OK | MB_ICONERROR);
            return;
        }

        SetUnsavedSurface(ViewerText(IDS_CLIPBOARD_TITLE), ViewerText(IDS_CLIPBOARD_TITLE), std::move(pasted.Surface));
    }

    void ApplySurfaceTransform(ImageSurfaceTransform transform)
    {
        if (m_surface.Pixels.empty())
            return;

        StopAnimation();
        TransformSurfaceResult transformed = TransformSurface(m_surface, transform);
        if (!transformed.Succeeded())
        {
            ViewerMessageBox(m_hwnd, ImageErrorText(transformed.Error).c_str(), MB_OK | MB_ICONERROR);
            return;
        }

        const bool hadSelection = HasCropSelection();
        const RECT selection = hadSelection ? TransformSelectionRect(m_selectionImage, m_surface.Width, m_surface.Height, transform)
                                            : RECT{};
        m_surface = std::move(transformed.Surface);
        m_surfaceModified = true;
        if (transform == ImageSurfaceTransform::Rotate90Clockwise || transform == ImageSurfaceTransform::Rotate90CounterClockwise)
            m_quarterTurned = !m_quarterTurned;
        m_panX = 0;
        m_panY = 0;
        ClearSelection();
        if (hadSelection) // the selection turns with the image, as before
        {
            m_hasSelection = true;
            m_selectionImage = selection;
        }
        ClearPipetteSample();
        ClampPanOffsets();
        UpdateStatus();
        UpdateCommands();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    void ApplyCropSelection()
    {
        if (m_surface.Pixels.empty())
            return;
        if (!HasCropSelection())
        {
            ViewerMessageBox(m_hwnd, ViewerText(IDS_SELECT_REGION), MB_OK | MB_ICONSTOP);
            return;
        }

        StopAnimation();
        const uint32_t left = static_cast<uint32_t>(m_selectionImage.left);
        const uint32_t top = static_cast<uint32_t>(m_selectionImage.top);
        const uint32_t width = static_cast<uint32_t>(m_selectionImage.right - m_selectionImage.left);
        const uint32_t height = static_cast<uint32_t>(m_selectionImage.bottom - m_selectionImage.top);

        CropSurfaceResult cropped = CropSurface(m_surface, left, top, width, height);
        if (!cropped.Succeeded())
        {
            ViewerMessageBox(m_hwnd, ImageErrorText(cropped.Error).c_str(), MB_OK | MB_ICONERROR);
            return;
        }

        m_surface = std::move(cropped.Surface);
        m_surfaceModified = true;
        m_panX = 0;
        m_panY = 0;
        ClearSelection();
        ClearPipetteSample();
        ClampPanOffsets();
        UpdateStatus();
        UpdateCommands();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    const ImageSurface* SurfaceForOutput(ImageSurface& selectedSurface, std::wstring& errorText) const
    {
        if (!HasCropSelection())
            return &m_surface;

        const uint32_t left = static_cast<uint32_t>(m_selectionImage.left);
        const uint32_t top = static_cast<uint32_t>(m_selectionImage.top);
        const uint32_t width = static_cast<uint32_t>(m_selectionImage.right - m_selectionImage.left);
        const uint32_t height = static_cast<uint32_t>(m_selectionImage.bottom - m_selectionImage.top);

        CropSurfaceResult cropped = CropSurface(m_surface, left, top, width, height);
        if (!cropped.Succeeded())
        {
            errorText = ImageErrorText(cropped.Error);
            return nullptr;
        }

        selectedSurface = std::move(cropped.Surface);
        return &selectedSurface;
    }

    void SelectWholeImage()
    {
        if (m_surface.Pixels.empty() || m_surface.Width == 0 || m_surface.Height == 0)
            return;

        if (m_selecting && GetCapture() == m_hwnd)
            ReleaseCapture();
        m_selecting = false;
        m_hasSelection = true;
        m_selectionStart = {0, 0};
        m_selectionImage.left = 0;
        m_selectionImage.top = 0;
        m_selectionImage.right = static_cast<LONG>(m_surface.Width);
        m_selectionImage.bottom = static_cast<LONG>(m_surface.Height);
        UpdateStatus();
        UpdateCommands();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    void DeselectImage()
    {
        if (!m_hasSelection && !m_selecting)
            return;

        if (m_selecting && GetCapture() == m_hwnd)
            ReleaseCapture();
        ClearSelection();
        UpdateStatus();
        UpdateCommands();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    void ToggleFullScreen()
    {
        if (!m_fullScreen)
        {
            m_savedPlacement.length = sizeof(m_savedPlacement);
            GetWindowPlacement(m_hwnd, &m_savedPlacement);
            m_savedStyle = GetWindowLongPtrW(m_hwnd, GWL_STYLE);
            m_savedExStyle = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);

            MONITORINFO monitor = {};
            monitor.cbSize = sizeof(monitor);
            GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &monitor);

            SetWindowLongPtrW(m_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, m_savedExStyle);
            if (m_rebar != nullptr)
                ShowWindow(m_rebar, SW_HIDE);
            if (m_status != nullptr)
                ShowWindow(m_status, SW_HIDE);
            SetWindowPos(m_hwnd,
                         HWND_TOP,
                         monitor.rcMonitor.left,
                         monitor.rcMonitor.top,
                         monitor.rcMonitor.right - monitor.rcMonitor.left,
                         monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            m_fullScreen = true;
            SetTimer(m_hwnd, CursorTimerId, FullScreenCursorDelayMs, nullptr);
        }
        else
        {
            SetWindowLongPtrW(m_hwnd, GWL_STYLE, m_savedStyle);
            SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, m_savedExStyle);
            if (m_rebar != nullptr)
                ShowWindow(m_rebar, SW_SHOW);
            if (m_status != nullptr && m_statusVisible)
                ShowWindow(m_status, SW_SHOW);
            SetWindowPlacement(m_hwnd, &m_savedPlacement);
            SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            m_fullScreen = false;
            KillTimer(m_hwnd, CursorTimerId);
            if (m_cursorHidden)
            {
                m_cursorHidden = false;
                HandleSetCursor(MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
            }
        }
        Layout();
        ClampPanOffsets();
    }

    void StoreCurrentViewerPreferences() const
    {
        ViewerPreferences preferences = GetViewerPreferences();
        preferences.ToolbarVisible = m_toolbarVisible;
        preferences.StatusbarVisible = m_statusVisible;
        preferences.PipetteInHex = m_pipetteInHex;
        SetViewerPreferences(preferences);
    }

    void ApplyInitialFullScreenPreference()
    {
        if (!m_initialFullScreenPending)
            return;

        m_initialFullScreenPending = false;
        m_initialWindowSizePending = false;
        if (m_surface.Width == 0 ||
            m_surface.Height == 0 ||
            m_fullScreen ||
            IsIconic(m_hwnd))
        {
            return;
        }

        ToggleFullScreen();
    }

    bool ResizeWindowToFitImage(bool largerIfNeeded, bool useCurrentDisplayScale)
    {
        if (m_surface.Width == 0 ||
            m_surface.Height == 0 ||
            m_fullScreen ||
            IsIconic(m_hwnd) ||
            IsZoomed(m_hwnd))
        {
            return false;
        }

        Layout();

        RECT current = {};
        if (!GetWindowRect(m_hwnd, &current))
            return false;

        const int currentWidth = RectWidth(current);
        const int currentHeight = RectHeight(current);
        const int contentWidth = std::max(1, RectWidth(m_content));
        const int contentHeight = std::max(1, RectHeight(m_content));
        const int chromeWidth = std::max(0, currentWidth - contentWidth);
        const int chromeHeight = std::max(0, currentHeight - contentHeight);

        long long imageWidth = static_cast<long long>(m_surface.Width);
        long long imageHeight = static_cast<long long>(m_surface.Height);
        if (useCurrentDisplayScale)
        {
            const double scale = EffectiveScale(contentWidth, contentHeight);
            imageWidth = std::max<long long>(1, std::llround(static_cast<double>(m_surface.Width) * scale));
            imageHeight = std::max<long long>(1, std::llround(DisplayHeight() * scale));
        }
        else
        {
            imageHeight = std::max<long long>(1, std::llround(DisplayHeight()));
        }

        int targetWidth = ClampInt64ToInt(imageWidth + chromeWidth);
        int targetHeight = ClampInt64ToInt(imageHeight + chromeHeight);
        if (largerIfNeeded)
        {
            targetWidth = std::max(currentWidth, targetWidth);
            targetHeight = std::max(currentHeight, targetHeight);
        }

        MONITORINFO monitor = {};
        monitor.cbSize = sizeof(monitor);
        const bool haveMonitor = GetMonitorInfoW(MonitorFromRect(&current, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE;
        if (haveMonitor)
        {
            const int workWidth = std::max(1, RectWidth(monitor.rcWork));
            const int workHeight = std::max(1, RectHeight(monitor.rcWork));
            targetWidth = std::min(targetWidth, workWidth);
            targetHeight = std::min(targetHeight, workHeight);
        }

        targetWidth = std::max(1, targetWidth);
        targetHeight = std::max(1, targetHeight);

        RECT target = {};
        target.left = current.left - (targetWidth - currentWidth) / 2;
        target.top = current.top - (targetHeight - currentHeight) / 2;
        target.right = target.left + targetWidth;
        target.bottom = target.top + targetHeight;

        if (haveMonitor)
        {
            const RECT work = monitor.rcWork;
            if (target.left < work.left)
                OffsetRect(&target, work.left - target.left, 0);
            if (target.right > work.right)
                OffsetRect(&target, work.right - target.right, 0);
            if (target.top < work.top)
                OffsetRect(&target, 0, work.top - target.top);
            if (target.bottom > work.bottom)
                OffsetRect(&target, 0, work.bottom - target.bottom);
        }

        SetWindowPos(m_hwnd,
                     nullptr,
                     target.left,
                     target.top,
                     RectWidth(target),
                     RectHeight(target),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        ClampPanOffsets();
        return true;
    }

    void ApplyInitialWindowSizePreference()
    {
        if (!m_initialWindowSizePending)
            return;

        m_initialWindowSizePending = false;
        if (m_initialWindowSizeMode == ViewerWindowSizeMode::SameAsSally)
            return;

        ResizeWindowToFitImage(m_initialWindowSizeMode == ViewerWindowSizeMode::LargerIfNeeded, false);
    }

    void ToggleToolbar()
    {
        if (m_toolbar == nullptr)
            CreateToolbar();
        else
            DestroyToolbar();
        m_toolbarVisible = m_toolbar != nullptr;
        StoreCurrentViewerPreferences();
        Layout();
        UpdateCommands();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    void ToggleStatusbar()
    {
        if (m_status == nullptr)
            return;

        m_statusVisible = !m_statusVisible;
        ShowWindow(m_status, m_statusVisible ? SW_SHOW : SW_HIDE);
        StoreCurrentViewerPreferences();
        Layout();
        UpdateCommands();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    // Every open viewer, this one included, applies the result (ConfigurationChanged).
    void ConfigureViewerPreferences()
    {
        EditViewerPreferences(m_hwnd, ModuleInstance);
    }

    // What a viewer takes from the configuration and follows while it is open.
    void ApplyPreferences(const ViewerPreferences& preferences)
    {
        m_pipetteInHex = preferences.PipetteInHex;
        m_showFullPathInTitle = preferences.ShowFullPathInTitle;
        m_applyExifOrientation = preferences.AutoRotate; // from the next image on
        m_captureCursor = preferences.CaptureCursor;
        m_captureScope = preferences.CaptureScope;
        m_captureTrigger = preferences.CaptureTrigger;
        m_captureHotKey = SanitizeCaptureHotKey(preferences.CaptureHotKey);
        m_captureTimerSeconds = SanitizeCaptureTimerSeconds(preferences.CaptureTimerSeconds);
        m_backgroundColor = preferences.BackgroundColor;
        m_fullScreenBackgroundColor = preferences.FullScreenBackgroundColor;
        m_transparentColor = preferences.TransparentColor;
        m_fullScreenTransparentColor = preferences.FullScreenTransparentColor;
    }

    // The EXIF of the page shown (each TIFF page has its own).
    const std::vector<ImageExifEntry>& CurrentExif() const
    {
        if (m_currentFrame < m_metadata.Frames.size() && !m_metadata.Frames[m_currentFrame].Exif.empty())
            return m_metadata.Frames[m_currentFrame].Exif;
        return m_metadata.Exif;
    }

    bool CanShowMetadataDetails() const
    {
        return !m_loading && !m_surface.Pixels.empty() && !CurrentExif().empty();
    }

    void ShowMetadataDetails()
    {
        if (CanShowMetadataDetails())
        {
            // Translated titles and decoded values, as the old PictView showed them.
            std::vector<ImageExifEntry> shown = CurrentExif();
            for (ImageExifEntry& entry : shown)
            {
                entry.Name = ExifTagTitle(entry);
                entry.Value = ExifValueText(entry);
            }
            ShowExifDialog(m_hwnd, shown);
        }
    }

    const ImageExifEntry* FindExif(uint16_t tag) const
    {
        for (const ImageExifEntry& entry : CurrentExif())
        {
            if (entry.Tag == tag && entry.Group != L"GPS")
                return &entry;
        }
        return nullptr;
    }

    // The container implies the compression, except TIFF, where the Compression tag says it.
    std::wstring CompressionDescription() const
    {
        const GUID& container = m_metadata.ContainerFormat;
        if (IsEqualGUID(container, GUID_ContainerFormatJpeg))
            return L"JPEG";
        if (IsEqualGUID(container, GUID_ContainerFormatPng))
            return L"Deflate";
        if (IsEqualGUID(container, GUID_ContainerFormatGif))
            return L"LZW";
        if (IsEqualGUID(container, GUID_ContainerFormatTiff))
        {
            const ImageExifEntry* tag = FindExif(0x0103);
            if (tag != nullptr)
            {
                switch (wcstoul(tag->Value.c_str(), nullptr, 10))
                {
                case 1: return L"None";
                case 2: return L"CCITT 1D";
                case 3: return L"CCITT Group 3";
                case 4: return L"CCITT Group 4";
                case 5: return L"LZW";
                case 6:
                case 7: return L"JPEG";
                case 8:
                case 32946: return L"Deflate";
                case 32773: return L"PackBits";
                }
            }
        }
        return std::wstring();
    }

    std::wstring ImageComment() const
    {
        std::wstring comment;
        if (const ImageFrameInfo* frame = CurrentFrameInfo())
        {
            for (const wchar_t c : frame->Comment) // the edit box wants CR LF
            {
                if (c == L'\n' && (comment.empty() || comment.back() != L'\r'))
                    comment += L'\r';
                comment += c;
            }
        }
        for (const uint16_t tag : {uint16_t(0x010E), uint16_t(0x9C9C), uint16_t(0x9286)}) // description, XPComment, UserComment
        {
            const ImageExifEntry* entry = FindExif(tag);
            if (entry == nullptr || entry->Value.empty() || entry->Value.front() == L'(' ||
                comment.find(entry->Value) != std::wstring::npos)
                continue;
            if (!comment.empty())
                comment += L"\r\n";
            comment += entry->Value;
        }
        return comment;
    }

    void ShowProperties()
    {
        const ImageFrameInfo* frame = CurrentFrameInfo();
        if (frame == nullptr || m_surface.Pixels.empty())
            return;

        ImagePropertiesInfo info;
        info.Page = m_currentFrame;
        info.PageCount = (std::max)(1u, FrameCount());
        info.Animation = CanAnimate();
        info.Width = frame->Width;
        info.Height = frame->Height;
        info.PixelFormat = frame->PixelFormat;
        info.BitsPerPixel = frame->BitsPerPixel;
        info.ChannelCount = frame->ChannelCount;
        info.MemoryBytes = static_cast<uint64_t>(m_surface.Pixels.size());
        WIN32_FILE_ATTRIBUTE_DATA attributes = {};
        if (!m_path.empty() && GetFileAttributesExW(m_path.c_str(), GetFileExInfoStandard, &attributes))
            info.FileBytes = (static_cast<uint64_t>(attributes.nFileSizeHigh) << 32) | attributes.nFileSizeLow;
        info.DpiX = frame->DpiX;
        info.DpiY = frame->DpiY;
        info.Format = m_metadata.ContainerName;
        info.Compression = CompressionDescription();
        info.Comment = ImageComment();
        ShowImagePropertiesDialog(m_hwnd, info);
    }

    void NotifySaveSuccess(const std::wstring& outputPath)
    {
        if (!GetViewerPreferences().ShowSaveSuccessMessage)
            return;

        if (m_hostActions.NotifySaveSuccess != nullptr)
        {
            m_hostActions.NotifySaveSuccess(m_hostActions.Context, m_hwnd, outputPath.c_str());
            return;
        }

        bool dontShowAgain = false;
        MessageBoxWithDontShowAgain(IDS_SAVE_AS_SUCCESS, IDS_DONT_SHOW_AGAIN, MSGBOXEX_OK | MSGBOXEX_ICONINFORMATION, dontShowAgain);
        if (dontShowAgain)
        {
            ViewerPreferences preferences = GetViewerPreferences();
            preferences.ShowSaveSuccessMessage = false;
            SetViewerPreferences(preferences);
        }
    }

    // A message box with the old PictView's "don't show this again" check box (Sally's
    // SalMessageBoxEx); a plain one without a host.
    int MessageBoxWithDontShowAgain(UINT textId, UINT checkTextId, DWORD flags, bool& dontShowAgain)
    {
        dontShowAgain = false;
        if (HostGeneral == nullptr)
            return ViewerMessageBox(m_hwnd, ViewerText(textId), (flags & MSGBOXEX_YESNO) != 0 ? MB_YESNO | MB_ICONQUESTION : MB_OK | MB_ICONINFORMATION);
        BOOL checked = FALSE;
        MSGBOXEX_PARAMS params = {};
        params.HParent = m_hwnd;
        params.Text = ViewerText(textId);
        params.Caption = ViewerText(IDS_PLUGINNAME);
        params.Flags = flags | MSGBOXEX_SILENT;
        params.CheckBoxText = ViewerText(checkTextId);
        params.CheckBoxValue = &checked;
        const int result = HostGeneral->SalMessageBoxEx(&params);
        dontShowAgain = checked != FALSE;
        return result;
    }

    bool ConfirmAlphaLoss(const std::wstring& outputPath)
    {
        if (!GetViewerPreferences().ShowAlphaLossWarning)
            return true;

        if (m_hostActions.ConfirmAlphaLoss != nullptr)
        {
            bool proceed = false;
            if (!m_hostActions.ConfirmAlphaLoss(m_hostActions.Context, m_hwnd, outputPath.c_str(), proceed))
                return false;
            return proceed;
        }

        bool dontShowAgain = false;
        const bool proceed = MessageBoxWithDontShowAgain(IDS_SAVE_LOST_ALPHA, IDS_DONT_SHOW_AGAIN_SLA, MSGBOXEX_YESNO | MSGBOXEX_ICONQUESTION,
                                                         dontShowAgain) == IDYES;
        if (proceed && dontShowAgain) // "save without the alpha channel" from now on
        {
            ViewerPreferences preferences = GetViewerPreferences();
            preferences.ShowAlphaLossWarning = false;
            SetViewerPreferences(preferences);
        }
        return proceed;
    }

    // The name Save As offers: the image's own, or "Clipbrd1", "Capture2", ... for a pasted or
    // captured one, as the old PictView did.
    std::wstring SuggestedSaveStem()
    {
        if (m_path.empty())
        {
            static int clipboardCount = 0;
            static int captureCount = 0;
            if (m_displayName == ViewerText(IDS_CLIPBOARD_TITLE))
                return ViewerText(IDS_CLIPBOARD_FNAME) + std::to_wstring(++clipboardCount);
            if (m_displayName == ViewerText(IDS_CAPTURE_TITLE))
                return ViewerText(IDS_CAPTURE_FNAME) + std::to_wstring(++captureCount);
        }
        std::wstring stem = StemFromFileName(m_displayName);
        return stem.empty() ? std::wstring(L"image") : stem;
    }

    // The surface to save with the Save As rotation and flip applied (rotation first).
    const ImageSurface* SurfaceForSave(SaveAsRotation rotation, SaveAsFlip flip, ImageSurface& storage, std::wstring& errorText)
    {
        const ImageSurface* surface = &m_surface;
        auto apply = [&](ImageSurfaceTransform transform) {
            TransformSurfaceResult result = TransformSurface(*surface, transform);
            if (!result.Succeeded())
            {
                errorText = ImageErrorText(result.Error);
                return false;
            }
            storage = std::move(result.Surface);
            surface = &storage;
            return true;
        };
        switch (rotation)
        {
        case SaveAsRotation::Clockwise90:
            if (!apply(ImageSurfaceTransform::Rotate90Clockwise))
                return nullptr;
            break;
        case SaveAsRotation::Rotate180:
            if (!apply(ImageSurfaceTransform::Rotate180))
                return nullptr;
            break;
        case SaveAsRotation::Clockwise270:
            if (!apply(ImageSurfaceTransform::Rotate90CounterClockwise))
                return nullptr;
            break;
        case SaveAsRotation::None:
            break;
        }
        if (flip == SaveAsFlip::Vertical && !apply(ImageSurfaceTransform::FlipVertical))
            return nullptr;
        if (flip == SaveAsFlip::Horizontal && !apply(ImageSurfaceTransform::FlipHorizontal))
            return nullptr;
        return surface;
    }

    void SaveAs()
    {
        if (m_surface.Pixels.empty())
            return;

        std::wstring engineError;
        if (!EnsureEngine(engineError))
        {
            ViewerMessageBox(m_hwnd, engineError.c_str(), MB_OK | MB_ICONERROR);
            return;
        }

        ViewerPreferences savePreferences = GetViewerPreferences();
        DWORD filterIndex = ClampSaveFilterIndex(savePreferences.LastSaveFilterIndex);
        ImageSaveFormat suggestedFormat = SaveFormatFromFilterIndex(filterIndex);

        std::wstring suggested = SuggestedSaveStem();
        suggested += SaveExtension(suggestedFormat);
        SaveAsRotation rotation = SaveAsRotation::None;
        SaveAsFlip flip = SaveAsFlip::None;

        std::wstring outputPath;
        if (m_hostActions.ChooseSavePath != nullptr)
        {
            bool canceled = false;
            if (!m_hostActions.ChooseSavePath(m_hostActions.Context,
                                              m_hwnd,
                                              suggested.c_str(),
                                              outputPath,
                                              canceled) ||
                canceled ||
                outputPath.empty())
            {
                return;
            }
        }
        else
        {
            SaveAsRequest request;
            request.Filter = ViewerText(IDS_PV_SAVEAS_FILTER);
            request.FilterFormats = {ImageSaveFormat::Png, ImageSaveFormat::Jpeg, ImageSaveFormat::Tiff,
                                     ImageSaveFormat::Bmp, ImageSaveFormat::Gif};
            request.FilterIndex = filterIndex;
            request.InitialDirectory = SaveInitialDirectoryForDialog(m_path, savePreferences);
            request.FileName = suggested;
            request.TiffCompression = savePreferences.TiffCompression;
            request.JpegQuality = savePreferences.JpegQuality;
            request.JpegSubsampling = savePreferences.JpegSubsampling;
            if (!PromptForSaveAs(m_hwnd, request) || request.FileName.empty())
                return;

            outputPath = std::move(request.FileName);
            filterIndex = request.FilterIndex;
            rotation = request.Rotation;
            flip = request.Flip;
            // The options persist like the configuration's, as the old Save As did.
            ViewerPreferences options = GetViewerPreferences();
            options.TiffCompression = request.TiffCompression;
            options.JpegQuality = request.JpegQuality;
            options.JpegSubsampling = request.JpegSubsampling;
            SetViewerPreferences(options);
        }

        ImageSaveFormat format = SaveFormatFromPathOrFilter(outputPath, filterIndex);
        if (!PathHasExtension(outputPath))
        {
            outputPath += SaveExtension(format);
            if (GetFileAttributesW(outputPath.c_str()) != INVALID_FILE_ATTRIBUTES)
            {
                bool overwrite = false;
                if (m_hostActions.ConfirmSaveOverwrite != nullptr)
                {
                    if (!m_hostActions.ConfirmSaveOverwrite(m_hostActions.Context,
                                                            m_hwnd,
                                                            outputPath.c_str(),
                                                            overwrite) ||
                        !overwrite)
                    {
                        return;
                    }
                }
                else
                {
                    const std::wstring prompt = FormatViewerText(IDS_SAVE_ERR_EXISTS_OVERWRITE, outputPath.c_str());
                    if (ViewerMessageBox(m_hwnd, prompt.c_str(), MB_YESNO | MB_ICONQUESTION) != IDYES)
                        return;
                }
            }
        }

        if (!SaveFormatPreservesAlpha(format) && SurfaceHasAlpha() && !ConfirmAlphaLoss(outputPath))
            return;

        SaveOptions options;
        options.Format = format;
        if (const ImageFrameInfo* frame = CurrentFrameInfo(); frame != nullptr && frame->DpiX > 0 && frame->DpiY > 0)
        {
            options.DpiX = frame->DpiX;
            options.DpiY = frame->DpiY;
        }
        if (format == ImageSaveFormat::Jpeg)
        {
            const ViewerPreferences preferences = GetViewerPreferences();
            const int quality = (std::max)(1, (std::min)(100, preferences.JpegQuality));
            options.JpegQuality = static_cast<float>(quality) / 100.0f;
            options.JpegSubsampling = JpegSubsamplingFromPreference(preferences.JpegSubsampling);
        }
        else if (format == ImageSaveFormat::Tiff)
        {
            const ViewerPreferences preferences = GetViewerPreferences();
            options.TiffCompression = TiffCompressionFromPreference(preferences.TiffCompression);
        }
        ImageSurface transformed;
        std::wstring transformError;
        const ImageSurface* surface = SurfaceForSave(rotation, flip, transformed, transformError);
        if (surface == nullptr)
        {
            ViewerMessageBox(m_hwnd, transformError.c_str(), MB_OK | MB_ICONERROR);
            return;
        }
        SaveImageResult save = m_engine->SaveSurfaceToPath(*surface, outputPath.c_str(), options);
        if (!save.Succeeded())
        {
            ViewerMessageBox(m_hwnd, ImageErrorText(save.Error).c_str(), MB_OK | MB_ICONERROR);
            return;
        }

        ViewerPreferences updatedPreferences = GetViewerPreferences();
        updatedPreferences.LastSaveFilterIndex = static_cast<int>(SaveFilterIndexForFormat(format));
        if (m_path.empty() && updatedPreferences.RememberSavePath)
            updatedPreferences.SaveInitialDirectory = DirectoryFromPath(outputPath);
        SetViewerPreferences(updatedPreferences);

        NotifySaveSuccess(outputPath);

        if (m_status != nullptr)
        {
            const std::wstring status = FormatViewerText(IDS_PV_SAVED, FileNameFromPath(outputPath).c_str());
            SendMessageW(m_status, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(status.c_str()));
        }
    }

    void PrintImage()
    {
        if (m_surface.Pixels.empty())
            return;

        ImageSurface selectedSurface;
        std::wstring outputError;
        const ImageSurface* outputSurface = SurfaceForOutput(selectedSurface, outputError);
        if (outputSurface == nullptr)
        {
            ViewerMessageBox(m_hwnd, outputError.c_str(), MB_OK | MB_ICONERROR);
            return;
        }

        const std::wstring docName = m_displayName.empty() ? ViewerText(IDS_PV_PICTVIEW_IMAGE) : m_displayName;
        if (m_hostActions.PrintSurface != nullptr)
        {
            bool printed = false;
            if (!m_hostActions.PrintSurface(m_hostActions.Context,
                                            m_hwnd,
                                            docName.c_str(),
                                            outputSurface->Width,
                                            outputSurface->Height,
                                            outputSurface->Stride,
                                            outputSurface->Pixels.data(),
                                            outputSurface->Pixels.size(),
                                            printed) ||
                !printed)
            {
                ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_UNABLE_TO_PRINT_THE_IMAGE), MB_OK | MB_ICONERROR);
            }
            return;
        }

        static PrintSettings sessionSettings; // remembered while Sally runs, as in the old PictView
        PrintRequest request;
        request.Image = &m_surface;
        request.SelectionImage = outputSurface != &m_surface ? outputSurface : nullptr;
        if (const ImageFrameInfo* frame = CurrentFrameInfo())
        {
            request.ImageDpiX = frame->DpiX;
            request.ImageDpiY = frame->DpiY;
        }
        request.Settings = sessionSettings;
        request.Settings.Selection = request.SelectionImage != nullptr;
        if (!PromptForPrint(m_hwnd, request))
            return;
        sessionSettings = request.Settings;

        HDC printer = request.Printer;
        const ImageSurface* printed = request.Settings.Selection && request.SelectionImage != nullptr ? request.SelectionImage : &m_surface;
        const PrinterPage page = DescribePrinterPage(printer);
        double width = 0;
        double height = 0;
        NaturalPrintSize(printed->Width, printed->Height, request.ImageDpiX, request.ImageDpiY, page.DpiX, page.DpiY, width, height);
        const PrintPlacement placement = page.Valid()
                                             ? PlaceOnPage(request.Settings, width, height,
                                                           static_cast<double>(page.PrintableWidth) / page.DpiX * 72.0,
                                                           static_cast<double>(page.PrintableHeight) / page.DpiY * 72.0)
                                             : PrintPlacement();
        if (!placement.Valid)
        {
            DeleteDC(printer);
            ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_UNABLE_TO_READ_PRINTER_PAGE), MB_OK | MB_ICONERROR);
            return;
        }
        const RECT target = PlacementToDevice(placement, page);

        DOCINFOW docInfo = {};
        docInfo.cbSize = sizeof(docInfo);
        docInfo.lpszDocName = docName.c_str();

        SetStatusMessage(ViewerText(IDS_PRINTING));
        bool success = false;
        if (StartDocW(printer, &docInfo) > 0)
        {
            if (StartPage(printer) > 0)
            {
                BITMAPINFO bitmapInfo = {};
                bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(printed->Width);
                bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(printed->Height);
                bitmapInfo.bmiHeader.biPlanes = 1;
                bitmapInfo.bmiHeader.biBitCount = 32;
                bitmapInfo.bmiHeader.biCompression = BI_RGB;

                SetStretchBltMode(printer, HALFTONE);
                SetBrushOrgEx(printer, 0, 0, nullptr);
                const int copied = StretchDIBits(printer,
                                                 target.left,
                                                 target.top,
                                                 target.right - target.left,
                                                 target.bottom - target.top,
                                                 0,
                                                 0,
                                                 printed->Width,
                                                 printed->Height,
                                                 printed->Pixels.data(),
                                                 &bitmapInfo,
                                                 DIB_RGB_COLORS,
                                                 SRCCOPY);
                success = copied != GDI_ERROR; // "Show bounding box" is drawn in the preview only, as before
                success = EndPage(printer) > 0 && success;
            }
            success = EndDoc(printer) > 0 && success;
        }
        DeleteDC(printer);
        SetStatusMessage(std::wstring());
        if (!success)
            ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_UNABLE_TO_PRINT_THE_IMAGE), MB_OK | MB_ICONERROR);
    }

    void CopyImageToClipboard()
    {
        if (m_surface.Pixels.empty())
            return;

        ImageSurface selectedSurface;
        std::wstring outputError;
        const ImageSurface* outputSurface = SurfaceForOutput(selectedSurface, outputError);
        if (outputSurface == nullptr)
        {
            ViewerMessageBox(m_hwnd, outputError.c_str(), MB_OK | MB_ICONERROR);
            return;
        }

        const size_t headerSize = sizeof(BITMAPINFOHEADER);
        const size_t pixelBytes = outputSurface->Pixels.size();
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, headerSize + pixelBytes);
        if (memory == nullptr)
        {
            ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_UNABLE_TO_ALLOCATE_CLIPBOARD_IMAGE), MB_OK | MB_ICONERROR);
            return;
        }

        uint8_t* data = static_cast<uint8_t*>(GlobalLock(memory));
        if (data == nullptr)
        {
            GlobalFree(memory);
            ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_UNABLE_TO_LOCK_CLIPBOARD_IMAGE_2), MB_OK | MB_ICONERROR);
            return;
        }

        BITMAPINFOHEADER* header = reinterpret_cast<BITMAPINFOHEADER*>(data);
        *header = {};
        header->biSize = sizeof(BITMAPINFOHEADER);
        header->biWidth = static_cast<LONG>(outputSurface->Width);
        header->biHeight = static_cast<LONG>(outputSurface->Height);
        header->biPlanes = 1;
        header->biBitCount = 32;
        header->biCompression = BI_RGB;
        header->biSizeImage = static_cast<DWORD>(pixelBytes);

        uint8_t* dstPixels = data + headerSize;
        for (uint32_t y = 0; y < outputSurface->Height; ++y)
        {
            const uint8_t* src = outputSurface->Pixels.data() + static_cast<size_t>(y) * outputSurface->Stride;
            uint8_t* dst = dstPixels + static_cast<size_t>(outputSurface->Height - 1 - y) * outputSurface->Stride;
            memcpy(dst, src, outputSurface->Stride);
        }
        GlobalUnlock(memory);

        if (!OpenClipboard(m_hwnd))
        {
            GlobalFree(memory);
            ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_UNABLE_TO_OPEN_THE_CLIPBOARD), MB_OK | MB_ICONERROR);
            return;
        }

        bool success = EmptyClipboard() && SetClipboardData(CF_DIB, memory) != nullptr;
        CloseClipboard();
        if (!success)
        {
            GlobalFree(memory);
            ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_UNABLE_TO_COPY_IMAGE_DATA), MB_OK | MB_ICONERROR);
        }
    }

    void ShowContextMenu(int x, int y)
    {
        if (HostGui == nullptr)
            return;

        UpdateCommands();
        CGUIMenuPopupAbstract* popup = HostGui->CreateMenuPopup();
        if (popup == nullptr)
            return;
        if (popup->LoadFromTemplate(ModuleInstance, ContextMenuTemplate, m_enablers, m_grayImages, m_hotImages))
        {
            if (x == -1 && y == -1)
            {
                RECT rect = CalculateImageRect();
                POINT point = {rect.left, rect.top};
                ClientToScreen(m_hwnd, &point);
                x = point.x;
                y = point.y;
            }
            popup->CheckItem(CmdFullScreen, FALSE, m_fullScreen ? TRUE : FALSE);
            popup->CheckItem(CmdToggleSourceSelection, FALSE, m_sourceFileSelected ? TRUE : FALSE);
            const DWORD command = popup->Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON, x, y, m_hwnd, nullptr);
            if (command != 0)
                PostMessageW(m_hwnd, WM_COMMAND, MAKEWPARAM(command, 0), 0);
        }
        HostGui->DestroyMenuPopup(popup);
    }

    int CurrentZoomPercent() const
    {
        const int viewWidth = std::max<LONG>(1, m_content.right - m_content.left);
        const int viewHeight = std::max<LONG>(1, m_content.bottom - m_content.top);
        return static_cast<int>(std::lround(EffectiveScale(viewWidth, viewHeight) * 100.0));
    }

    // "name (W x H x colors [n of m] - zoom%) - PictView", as the old PictView titled its windows.
    void UpdateTitle()
    {
        std::wstring title = ViewerText(IDS_PLUGINNAME);
        if (!m_surface.Pixels.empty())
        {
            std::wstring name = m_displayName;
            if (m_showFullPathInTitle && !m_path.empty())
                name = m_path;
            const ImageFrameInfo* frame = CurrentFrameInfo();
            const std::wstring colors = frame != nullptr ? DescribeImageColors(frame->BitsPerPixel, frame->PixelFormat)
                                                         : DescribeImageColors(32, GUID_WICPixelFormat32bppBGRA);
            title = FormatViewerTitle(name, m_surface.Width, m_surface.Height, colors, m_currentFrame, FrameCount(),
                                      CurrentZoomPercent());
        }
        if (title != m_title)
        {
            m_title = title;
            SetWindowTextW(m_hwnd, m_title.c_str());
        }
    }

    // The tool's hint, as the old status bar showed it in its first pane.
    const wchar_t* ToolHint() const
    {
        switch (m_tool)
        {
        case ViewerTool::Hand:
            return ViewerText(IDS_SB_HAND);
        case ViewerTool::Zoom:
            return ViewerText(IDS_SB_ZOOM);
        case ViewerTool::Select:
            return ViewerText(IDS_SB_CAGE);
        default:
            return L"";
        }
    }

    void UpdateStatus()
    {
        std::wstring text;
        int zoomPercent = 0;
        if (m_loading)
        {
            text = m_loadPercent > 0 ? FormatViewerText(IDS_PV_LOADING_PERCENT, m_loadPercent) : std::wstring(ViewerText(IDS_PV_LOADING));
        }
        else if (!m_surface.Pixels.empty())
        {
            zoomPercent = CurrentZoomPercent();
            text = m_tool == ViewerTool::Pipette ? PipetteStatusText() : std::wstring(ToolHint());
        }

        UpdateTitle(); // the title carries the zoom and the page
        UpdateToolbarZoomText(zoomPercent);
        if (m_status != nullptr)
        {
            SendMessageW(m_status, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(text.c_str()));
            UpdateStatusBarDetails();
        }
    }

    void UpdateToolbarZoomText(int zoomPercent)
    {
        std::wstring text = std::to_wstring(zoomPercent > 0 ? zoomPercent : 0) + L" %";
        if (text == m_toolbarZoomText)
            return;

        m_toolbarZoomText = std::move(text);
        if (m_toolbar == nullptr)
            return;
        TLBI_ITEM_INFO2 item = {};
        item.Mask = TLBI_MASK_TEXT;
        item.Text = m_toolbarZoomText.data();
        m_toolbar->SetItemInfo2(static_cast<DWORD>(CmdZoomTo), FALSE, &item);
    }

    void UpdateCommands()
    {
        const bool hasImage = !m_surface.Pixels.empty();
        const bool canUseImage = hasImage && !m_loading;
        const bool canNavigateSource = !m_loading && HasSourceNavigation();

        m_enablers[vweFileOpened] = canUseImage;
        m_enablers[vweFileOpened2] = canUseImage && !m_path.empty();
        m_enablers[vwePaste] = !m_loading && ClipboardContainsImage();
        m_enablers[vwePrevPage] = canUseImage && m_currentFrame > 0;
        m_enablers[vweNextPage] = canUseImage && m_currentFrame + 1 < FrameCount();
        m_enablers[vweMorePages] = canUseImage && FrameCount() > 1;
        m_enablers[vweImgInfoAvailable] = canUseImage;
        m_enablers[vweImgExifAvailable] = CanShowMetadataDetails();
        m_enablers[vweNotLoading] = !m_loading;
        m_enablers[vweSelSrcFile] = !m_loading && HasSourceSelection();
        m_enablers[vweNextFile] = canNavigateSource;
        m_enablers[vwePrevFile] = canNavigateSource;
        m_enablers[vweNextSelFile] = canNavigateSource;
        m_enablers[vwePrevSelFile] = canNavigateSource;
        m_enablers[vweFirstFile] = canNavigateSource;
        m_enablers[vweSelection] = canUseImage && HasCropSelection();
        m_enablers[vweAnimation] = canUseImage && CanAnimate();
        m_enablers[vweFocusFile] = !m_loading && HasSourceFocus();

        if (m_toolbar != nullptr)
        {
            m_toolbar->UpdateItemsState();
            UpdateToolbarChecks();
        }
    }

    void UpdateToolbarChecks()
    {
        if (m_toolbar == nullptr)
            return;
        m_toolbar->CheckItem(CmdToolHand, FALSE, m_tool == ViewerTool::Hand ? TRUE : FALSE);
        m_toolbar->CheckItem(CmdToolZoom, FALSE, m_tool == ViewerTool::Zoom ? TRUE : FALSE);
        m_toolbar->CheckItem(CmdToolSelect, FALSE, m_tool == ViewerTool::Select ? TRUE : FALSE);
        m_toolbar->CheckItem(CmdTogglePipette, FALSE, m_tool == ViewerTool::Pipette ? TRUE : FALSE);
        m_toolbar->CheckItem(CmdToggleSourceSelection, FALSE, m_sourceFileSelected ? TRUE : FALSE);
    }

public:
    BOOL IsMenuBarMessage(const MSG* message)
    {
        return m_menuBar != nullptr && m_menuBar->IsMenuBarMessage(message);
    }

private:
    void LoadPendingImage()
    {
        if (m_pendingLoadPath.empty())
            return;

        std::wstring path;
        path.swap(m_pendingLoadPath);
        const bool preserveDisplayedImage = m_pendingLoadPreserveImage;
        m_activeLoadClearsSourceNavigation = m_pendingLoadClearsSourceNavigation;
        m_hasActiveSourceFile = m_hasPendingSourceFile && m_pendingSourceFile.Path == path;
        if (m_hasActiveSourceFile)
            m_activeSourceFile = m_pendingSourceFile;
        else
            m_activeSourceFile = ViewerSourceFile();
        m_pendingLoadPreserveImage = false;
        m_pendingLoadClearsSourceNavigation = false;
        m_hasPendingSourceFile = false;
        m_pendingSourceFile = ViewerSourceFile();

        StopAnimation(false);
        CancelPendingImageLoad();
        m_loading = true;
        m_loadPreservesImage = preserveDisplayedImage;
        m_activeLoadPath = path;
        if (!preserveDisplayedImage)
        {
            m_path = m_activeLoadPath;
            m_displayName = FileNameFromPath(m_path);
            m_sourceBytes.clear();
            m_metadata = ImageMetadata();
            m_sourceStream.Reset();
            m_document.reset();
            m_surface = ImageSurface();
            m_currentFrame = 0;
            m_surfaceModified = false;
        }
        ClearSelection();
        UpdateTitle();
        UpdateStatus();
        UpdateCommands();
        InvalidateRect(m_hwnd, nullptr, TRUE);

        m_loadCancellation = std::make_shared<CancellationSource>();
        std::unique_ptr<ViewerLoadRequest> request = std::make_unique<ViewerLoadRequest>();
        request->Target = m_hwnd;
        request->Path = m_activeLoadPath;
        request->Cancellation = m_loadCancellation;
        request->ApplyExifOrientation = m_applyExifOrientation;
        request->Sequence = ++m_loadSequence;
        m_loadPercent = 0;

        HANDLE thread = CreateThread(nullptr, 0, ViewerLoadWorkerProc, request.get(), 0, nullptr);
        if (thread == nullptr)
        {
            m_loading = false;
            m_loadCancellation.reset();
            m_activeLoadPath.clear();
            m_activeLoadClearsSourceNavigation = false;
            m_hasActiveSourceFile = false;
            m_activeSourceFile = ViewerSourceFile();
            ViewerMessageBox(m_hwnd, ViewerText(IDS_PV_UNABLE_TO_START_THE_IMAGE), MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
            if (!preserveDisplayedImage)
                DestroyWindow(m_hwnd);
            else
            {
                m_loadPreservesImage = false;
                UpdateStatus();
                UpdateCommands();
            }
            return;
        }

        request.release();
        CloseHandle(thread);
    }

    void CompletePendingImageLoad(ViewerLoadResult* rawResult)
    {
        std::unique_ptr<ViewerLoadResult> result(rawResult);
        if (result == nullptr)
            return;

        if (result->Path != m_activeLoadPath)
            return;

        m_loading = false;
        m_loadCancellation.reset();

        if (result->Canceled)
        {
            if (!m_loadPreservesImage)
                DestroyWindow(m_hwnd);
            else
            {
                m_loadPreservesImage = false;
                m_activeLoadPath.clear();
                m_activeLoadClearsSourceNavigation = false;
                m_hasActiveSourceFile = false;
                m_activeSourceFile = ViewerSourceFile();
                UpdateStatus();
                UpdateCommands();
                InvalidateRect(m_hwnd, nullptr, TRUE);
            }
            return;
        }

        if (!result->Success)
        {
            RemoveRecentFile(result->Path); // an entry that no longer opens leaves the list
            if (!result->ErrorText.empty())
                ViewerMessageBox(m_hwnd, result->ErrorText.c_str(), MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
            if (!m_loadPreservesImage)
                DestroyWindow(m_hwnd);
            else
            {
                m_loadPreservesImage = false;
                m_activeLoadPath.clear();
                m_activeLoadClearsSourceNavigation = false;
                m_hasActiveSourceFile = false;
                m_activeSourceFile = ViewerSourceFile();
                UpdateTitle();
                UpdateStatus();
                UpdateCommands();
                InvalidateRect(m_hwnd, nullptr, TRUE);
            }
            return;
        }

        m_path = std::move(result->Path);
        m_displayName = std::move(result->DisplayName);
        m_activeLoadPath.clear();
        m_sourceBytes = std::move(result->SourceBytes);
        m_metadata = std::move(result->Metadata);
        m_sourceStream.Reset();
        m_document.reset();
        m_surface = std::move(result->Surface);
        m_surfaceModified = false;
        m_quarterTurned = QuarterTurnedByOrientation(CurrentFrameInfo(), m_applyExifOrientation);
        if (m_activeLoadClearsSourceNavigation)
        {
            ClearSourceNavigation();
        }
        else if (m_hasActiveSourceFile)
        {
            m_sourceNavigation.CurrentIndex = m_activeSourceFile.SourceIndex;
            m_sourceNavigation.CurrentPath = std::move(m_activeSourceFile.Path);
        }
        m_activeLoadClearsSourceNavigation = false;
        m_hasActiveSourceFile = false;
        m_activeSourceFile = ViewerSourceFile();
        m_currentFrame = 0;
        m_panX = 0;
        m_panY = 0;
        m_animationPlaying = false;
        m_loadPreservesImage = false;
        ClearSelection();
        ClearPipetteSample();

        std::wstring errorText;
        if (!EnsureEngine(errorText))
        {
            ViewerMessageBox(m_hwnd, errorText.c_str(), MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
            DestroyWindow(m_hwnd);
            return;
        }

        RecordRecentPath(m_path);
        ApplyInitialFullScreenPreference();
        ApplyInitialWindowSizePreference();
        UpdateTitle();
        UpdateStatus();
        UpdateCommands();
        InvalidateRect(m_hwnd, nullptr, TRUE);
        if (CanAnimate())
            StartAnimation();
    }

    void CancelPendingImageLoad()
    {
        if (m_loadCancellation != nullptr)
        {
            m_loadCancellation->Cancel();
            m_loadCancellation.reset();
        }
    }

    void SignalLock()
    {
        if (m_lock != nullptr)
        {
            SetEvent(m_lock);
            m_lock = nullptr;
        }
    }

    HWND m_hwnd = nullptr;
    CGUIMenuPopupAbstract* m_mainMenu = nullptr;
    CGUIMenuBarAbstract* m_menuBar = nullptr;
    CGUIToolBarAbstract* m_toolbar = nullptr;
    HWND m_rebar = nullptr;
    HIMAGELIST m_hotImages = nullptr;
    HIMAGELIST m_grayImages = nullptr;
    DWORD m_enablers[vweCount] = {};
    bool m_sourceFileSelected = false;
    HWND m_status = nullptr;
    HWND m_hScroll = nullptr; // shown while the image is wider than the view
    HWND m_vScroll = nullptr;
    RECT m_imageArea = {};    // between the tool bar and the status bar; m_content less the scrollbars
    std::wstring m_toolbarZoomText = L"0 %";
    bool m_toolbarVisible = true;
    bool m_statusVisible = true;
    HANDLE m_lock = nullptr;
    RECT m_content = {};
    std::wstring m_path;
    std::wstring m_activeLoadPath;
    std::wstring m_displayName;
    std::unique_ptr<ImageEngine> m_engine;
    ComPtr<IStream> m_sourceStream;
    std::unique_ptr<ImageDocument> m_document;
    std::vector<uint8_t> m_sourceBytes;
    ImageMetadata m_metadata;
    ImageSurface m_surface;
    uint32_t m_currentFrame = 0;
    ZoomMode m_zoomMode = ZoomMode::FitWhole;
    double m_manualZoom = 1.0;
    int m_panX = 0;
    int m_panY = 0;
    int m_panStartX = 0;
    int m_panStartY = 0;
    POINT m_panStartPoint = {};
    bool m_panning = false;
    POINT m_selectionStart = {};
    RECT m_selectionImage = {};
    bool m_hasSelection = false;
    bool m_selecting = false;
    bool m_constrainSelectionRatio = false;
    ViewerTool m_tool = ViewerTool::Hand;
    bool m_zoomSelecting = false;
    POINT m_zoomSelectionStartClient = {};
    POINT m_zoomSelectionEndClient = {};
    bool m_pipetteHasSample = false;
    bool m_pipetteInHex = false;
    PipetteSample m_pipetteSample;
    bool m_fullScreen = false;
    bool m_showFullPathInTitle = false;
    bool m_applyExifOrientation = true;
    bool m_captureCursor = false;
    ViewerCaptureScope m_captureScope = ViewerCaptureScope::Desktop;
    ViewerCaptureTrigger m_captureTrigger = ViewerCaptureTrigger::HotKey;
    WORD m_captureHotKey = VK_F10;
    int m_captureTimerSeconds = 5;
    bool m_capturePending = false;
    ViewerCaptureTrigger m_capturePendingTrigger = ViewerCaptureTrigger::HotKey;
    bool m_initialFullScreenPending = false;
    ViewerWindowSizeMode m_initialWindowSizeMode = ViewerWindowSizeMode::SameAsSally;
    bool m_initialWindowSizePending = true;
    COLORREF m_backgroundColor = RGB(128, 128, 128);
    COLORREF m_fullScreenBackgroundColor = RGB(0, 0, 0);
    COLORREF m_transparentColor = RGB(255, 255, 255);
    COLORREF m_fullScreenTransparentColor = RGB(255, 255, 255);
    bool m_loading = false;
    bool m_animationPlaying = false;
    bool m_surfaceModified = false;
    bool m_quarterTurned = false; // the surface is turned a quarter against the file's pixel grid
    ViewerHostActions m_hostActions;
    ViewerSourceNavigation m_sourceNavigation;
    std::wstring m_pendingLoadPath;
    bool m_pendingLoadPreserveImage = false;
    std::wstring m_title;
    AnimationCanvas m_animation;
    bool m_cursorHidden = false;
    HWND m_pipetteTip = nullptr;
    std::wstring m_pipetteTipText;
    LPARAM m_lastMousePosition = -1;
    bool m_edgeScrolling = false;
    POINT m_lastSelectionPoint = {};
    bool m_hasLastSelectionPoint = false;
    bool m_pendingLoadClearsSourceNavigation = false;
    ViewerSourceFile m_pendingSourceFile;
    bool m_hasPendingSourceFile = false;
    bool m_activeLoadClearsSourceNavigation = false;
    LPARAM m_loadSequence = 0; // counts loads; progress of an older one is ignored
    UINT m_loadPercent = 0;    // of the load running, 0 until its first band
    ViewerSourceFile m_activeSourceFile;
    bool m_hasActiveSourceFile = false;
    bool m_loadPreservesImage = false;
    std::shared_ptr<CancellationSource> m_loadCancellation;
    LONG_PTR m_savedStyle = 0;
    LONG_PTR m_savedExStyle = 0;
    WINDOWPLACEMENT m_savedPlacement = {};
};

struct ViewerThreadData
{
    std::wstring Path;
    ViewerStartupAction StartupAction = ViewerStartupAction::LoadPath;
    int Left = CW_USEDEFAULT;
    int Top = CW_USEDEFAULT;
    int Width = CW_USEDEFAULT;
    int Height = CW_USEDEFAULT;
    UINT ShowCmd = SW_SHOWNORMAL;
    BOOL AlwaysOnTop = FALSE;
    BOOL ReturnLock = FALSE;
    HANDLE* Lock = nullptr;
    BOOL* LockOwner = nullptr;
    ViewerHostActions HostActions;
    ViewerSourceNavigation SourceNavigation;
    HANDLE Continue = nullptr;
    BOOL Success = FALSE;
};

DWORD WINAPI ViewerThreadProc(void* param)
{
    ScopedComApartment comApartment(COINIT_APARTMENTTHREADED);

    ViewerThreadData* data = static_cast<ViewerThreadData*>(param);
    std::wstring path = data->Path;
    const int left = data->Left;
    const int top = data->Top;
    const int width = data->Width;
    const int height = data->Height;
    const UINT showCmd = data->ShowCmd;
    const BOOL alwaysOnTop = data->AlwaysOnTop;
    const BOOL returnLock = data->ReturnLock;
    const ViewerStartupAction startupAction = data->StartupAction;
    HANDLE* lock = data->Lock;
    BOOL* lockOwner = data->LockOwner;
    ViewerHostActions hostActions = data->HostActions;
    ViewerSourceNavigation sourceNavigation = std::move(data->SourceNavigation);
    HANDLE continueEvent = data->Continue;

    std::unique_ptr<ViewerWindow> window = std::make_unique<ViewerWindow>();
    HANDLE createdLock = nullptr;
    if (returnLock && lock != nullptr && lockOwner != nullptr)
    {
        createdLock = window->GetLock();
        *lock = createdLock;
        *lockOwner = TRUE;
    }

    HWND hwnd = nullptr;
    if (!returnLock || createdLock != nullptr)
    {
        window->SetHostActions(hostActions);
        window->SetSourceNavigation(std::move(sourceNavigation));
        if (startupAction == ViewerStartupAction::LoadPath)
            window->SetPendingLoadPath(path);
        hwnd = CreateWindowExW(alwaysOnTop ? WS_EX_TOPMOST : 0,
                               ViewerClassName,
                               L"PictView",
                               WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                               left,
                               top,
                               width,
                               height,
                               nullptr,
                               nullptr,
                               ModuleInstance,
                               window.get());
    }

    if (hwnd != nullptr)
    {
        data->Success = TRUE;
    }
    else
    {
        if (returnLock)
            window->CloseLockOnFailure();
    }

    const BOOL success = data->Success;
    SetEvent(continueEvent);

    if (success)
    {
        ShowWindow(window->Hwnd(), showCmd);
        SetForegroundWindow(window->Hwnd());
        UpdateWindow(window->Hwnd());
        switch (startupAction)
        {
        case ViewerStartupAction::LoadPath:
            PostMessageW(window->Hwnd(), WM_PICTVIEW_LOAD_IMAGE, 0, 0);
            break;
        case ViewerStartupAction::PasteClipboard:
            PostMessageW(window->Hwnd(), WM_COMMAND, CmdPaste, 0);
            break;
        case ViewerStartupAction::ScreenCapture:
            PostMessageW(window->Hwnd(), WM_COMMAND, CmdScreenCapture, 0);
            break;
        }

        MSG msg = {};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            if (!window->IsMenuBarMessage(&msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }

    return 0;
}

} // namespace

const wchar_t* ViewerText(UINT id)
{
    static std::mutex cacheMutex;
    static std::unordered_map<UINT, std::wstring> cache;

    std::lock_guard<std::mutex> lock(cacheMutex);
    auto found = cache.find(id);
    if (found == cache.end())
    {
        const wchar_t* text = nullptr;
        const int length = LoadStringW(ModuleInstance, id, reinterpret_cast<LPWSTR>(&text), 0);
        found = cache.emplace(id, length > 0 && text != nullptr ? std::wstring(text, static_cast<size_t>(length))
                                                                 : std::wstring()).first;
    }
    return found->second.c_str();
}

namespace
{

std::wstring FormatTextV(const wchar_t* format, va_list args)
{
    va_list probe;
    va_copy(probe, args);
    const int length = _vscwprintf(format, probe);
    va_end(probe);
    std::wstring text;
    if (length > 0)
    {
        text.resize(static_cast<size_t>(length) + 1);
        vswprintf(text.data(), text.size(), format, args);
        text.resize(static_cast<size_t>(length));
    }
    return text;
}

std::wstring FormatText(const wchar_t* format, ...)
{
    va_list args;
    va_start(args, format);
    std::wstring text = FormatTextV(format, args);
    va_end(args);
    return text;
}

} // namespace

std::wstring ImageErrorMessage(const ImageError& error)
{
    return ImageErrorText(error);
}

std::wstring FormatViewerText(UINT id, ...)
{
    va_list args;
    va_start(args, id);
    std::wstring text = FormatTextV(ViewerText(id), args);
    va_end(args);
    return text;
}

std::wstring FormatPipetteSample(int x, int y, int red, int green, int blue, bool hex, int index)
{
    std::wstring text = FormatViewerText(hex ? IDS_RGBXY_HEX : IDS_RGBXY, x, y, red, green, blue);
    if (index >= 0)
        text += FormatViewerText(IDS_INDEX, index);
    return text;
}

namespace
{
std::mutex AdditionalOpenMasksMutex;
std::wstring AdditionalOpenMasks;
} // namespace

void SetAdditionalOpenMasks(const std::wstring& masks)
{
    std::lock_guard<std::mutex> lock(AdditionalOpenMasksMutex);
    AdditionalOpenMasks = masks;
}

std::wstring GetAdditionalOpenMasks()
{
    std::lock_guard<std::mutex> lock(AdditionalOpenMasksMutex);
    return AdditionalOpenMasks;
}

ViewerScrollAxis ScrollAxisFromPan(long long drawSize, int viewSize, int pan)
{
    ViewerScrollAxis axis;
    if (viewSize <= 0 || drawSize <= viewSize)
        return axis;
    const long long span = drawSize - viewSize;
    const long long limit = (span + 1) / 2; // the pan runs from -limit (right edge) to +limit (left edge)
    axis.Visible = true;
    axis.Maximum = static_cast<int>(std::min<long long>(drawSize - 1, INT_MAX));
    axis.Page = viewSize;
    axis.Position = static_cast<int>(std::max<long long>(0, std::min<long long>(span, limit - pan)));
    return axis;
}

int PanFromScrollPosition(long long drawSize, int viewSize, int position)
{
    if (viewSize <= 0 || drawSize <= viewSize)
        return 0;
    const long long span = drawSize - viewSize;
    const long long limit = (span + 1) / 2;
    const long long clamped = std::max<long long>(0, std::min<long long>(span, position));
    return static_cast<int>(limit - clamped);
}

int ScrollPositionForRequest(int request, int position, int trackPosition, long long drawSize, int viewSize)
{
    if (viewSize <= 0 || drawSize <= viewSize)
        return 0;
    const long long span = drawSize - viewSize;
    const long long line = std::max(8, viewSize / 10);
    long long next = position;
    switch (request)
    {
    case SB_LINEUP:
        next -= line;
        break;
    case SB_LINEDOWN:
        next += line;
        break;
    case SB_PAGEUP:
        next -= viewSize;
        break;
    case SB_PAGEDOWN:
        next += viewSize;
        break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
        next = trackPosition;
        break;
    case SB_TOP:
        next = 0;
        break;
    case SB_BOTTOM:
        next = span;
        break;
    }
    return static_cast<int>(std::max<long long>(0, std::min(span, next)));
}

double PixelAspectRatio(double dpiX, double dpiY, bool quarterTurned)
{
    if (!(dpiX > 0.0) || !(dpiY > 0.0) || !std::isfinite(dpiX) || !std::isfinite(dpiY))
        return 1.0;
    const double ratio = quarterTurned ? dpiY / dpiX : dpiX / dpiY;
    if (std::fabs(ratio - 1.0) < 0.01 || ratio < 0.1 || ratio > 10.0)
        return 1.0;
    return ratio;
}

RECT TransformSelectionRect(const RECT& s, uint32_t width, uint32_t height, ImageSurfaceTransform transform)
{
    const LONG w = static_cast<LONG>(width);
    const LONG h = static_cast<LONG>(height);
    switch (transform)
    {
    case ImageSurfaceTransform::Rotate90Clockwise:
        return {h - s.bottom, s.left, h - s.top, s.right};
    case ImageSurfaceTransform::Rotate90CounterClockwise:
        return {s.top, w - s.right, s.bottom, w - s.left};
    case ImageSurfaceTransform::Rotate180:
        return {w - s.right, h - s.bottom, w - s.left, h - s.top};
    case ImageSurfaceTransform::FlipHorizontal:
        return {w - s.right, s.top, w - s.left, s.bottom};
    case ImageSurfaceTransform::FlipVertical:
    default:
        return {s.left, h - s.bottom, s.right, h - s.top};
    }
}

double NextZoomLevel(double scale, bool zoomIn)
{
    if (!(scale > 0) || !std::isfinite(scale))
        scale = 1.0;
    const double position = 3.0 * std::log2(scale);
    // A value within a thousandth of a step of a level counts as that level.
    const double step = zoomIn ? std::floor(position + 1e-3) + 1.0 : std::ceil(position - 1e-3) - 1.0;
    const double level = std::pow(2.0, step / 3.0);
    return (std::max)(static_cast<double>(MinimumManualZoomPercent) / 100.0,
                      (std::min)(level, static_cast<double>(MaximumManualZoomPercent) / 100.0));
}

std::wstring ExpandPluralText(const wchar_t* format, uint64_t value)
{
    if (format == nullptr)
        return std::wstring();
    if (HostGeneral != nullptr)
    {
        CQuadWord parameter(static_cast<DWORD>(value), static_cast<DWORD>(value >> 32));
        std::wstring expanded = SPLExpandPluralStringOwned(HostGeneral, format, 1, &parameter);
        if (!expanded.empty())
            return expanded;
    }
    // Without a host: each "{form|limit|form|...}" picks the first form whose limit is not
    // exceeded, else the last one.
    std::wstring text;
    const wchar_t* p = format;
    if (wcsncmp(p, L"{!}", 3) == 0)
        p += 3;
    while (*p != L'\0')
    {
        if (*p != L'{')
        {
            text.push_back(*p++);
            continue;
        }
        const wchar_t* end = wcschr(p, L'}');
        if (end == nullptr)
            break;
        std::vector<std::wstring> parts(1);
        for (const wchar_t* q = p + 1; q < end; ++q)
        {
            if (*q == L'|')
                parts.emplace_back();
            else
                parts.back().push_back(*q);
        }
        size_t form = 0;
        while (form + 1 < parts.size() && value > wcstoull(parts[form + 1].c_str(), nullptr, 10))
            form += 2;
        text += parts[(std::min)(form, parts.size() - 1)];
        p = end + 1;
    }
    return text;
}

std::wstring DescribeImageColors(uint32_t bitsPerPixel, const GUID& pixelFormat)
{
    if (IsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppCMYK) || IsEqualGUID(pixelFormat, GUID_WICPixelFormat64bppCMYK) ||
        IsEqualGUID(pixelFormat, GUID_WICPixelFormat40bppCMYKAlpha) || IsEqualGUID(pixelFormat, GUID_WICPixelFormat80bppCMYKAlpha))
    {
        return ViewerText(IDS_NCOLORS_CMYK);
    }
    if (bitsPerPixel == 0)
        return ViewerText(IDS_UNKNOWN);
    uint64_t colors = 16777216; // true colour and deeper, as the old PictView counted
    if (bitsPerPixel <= 8)
        colors = 1ull << bitsPerPixel;
    else if (bitsPerPixel == 15 || IsEqualGUID(pixelFormat, GUID_WICPixelFormat16bppBGR555))
        colors = 1ull << 15; // 15-bit HiColor
    else if (bitsPerPixel == 16)
        colors = 65536;
    const std::wstring format = ExpandPluralText(ViewerText(IDS_NCOLORS), colors);
    return FormatText(format.c_str(), static_cast<int>(colors));
}

std::wstring FormatViewerTitle(const std::wstring& name, uint32_t width, uint32_t height, const std::wstring& colors,
                               uint32_t frame, uint32_t frameCount, int zoomPercent)
{
    if (frameCount > 1)
        return FormatViewerText(IDS_TITLE_MULTI, name.c_str(), static_cast<int>(width), static_cast<int>(height),
                                colors.c_str(), static_cast<int>(frame + 1), static_cast<int>(frameCount), zoomPercent,
                                ViewerText(IDS_PLUGINNAME));
    return FormatViewerText(IDS_TITLE, name.c_str(), static_cast<int>(width), static_cast<int>(height), colors.c_str(),
                            zoomPercent, ViewerText(IDS_PLUGINNAME));
}

void SetViewerMessageBox(ViewerMessageBoxFunction messageBox)
{
    HostMessageBox = messageBox;
}

int ViewerMessageBox(HWND parent, const wchar_t* text, UINT type)
{
    const wchar_t* caption = ViewerText(IDS_PLUGINNAME);
    if (HostMessageBox != nullptr)
        return HostMessageBox(parent, text, caption, type);
    return MessageBoxW(parent, text, caption, type);
}

bool InitializeViewer(HINSTANCE instance, CSalamanderGeneralAbstract* general, CSalamanderGUIAbstract* gui)
{
    ModuleInstance = instance;
    HostGeneral = general;
    HostGui = gui;
    InitializeDialogs(instance, general, gui);

    INITCOMMONCONTROLSEX commonControls = {};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_BAR_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&commonControls);

    if (ViewerClassAtom != 0)
        return true;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = ViewerWindow::WndProc;
    wc.hInstance = ModuleInstance;
    wc.hIcon = LoadViewerBigIcon();
    wc.hIconSm = LoadViewerSmallIcon();
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_APPWORKSPACE + 1);
    wc.lpszClassName = ViewerClassName;

    ViewerClassAtom = RegisterClassExW(&wc);
    return ViewerClassAtom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void ReleaseViewer()
{
    if (Services().ReleaseViewers)
        Services().ReleaseViewers();

    if (HistogramClassAtom != 0 && ModuleInstance != nullptr)
    {
        UnregisterClassW(HistogramWindowClassName, ModuleInstance);
        HistogramClassAtom = 0;
    }







    if (ViewerClassAtom != 0 && ModuleInstance != nullptr)
    {
        UnregisterClassW(ViewerClassName, ModuleInstance);
        ViewerClassAtom = 0;
    }
}

bool AnyViewerWindowOpen()
{
    return !ViewerWindows.Empty();
}

void RemoveRecentFile(const std::wstring& path)
{
    ViewerRecentHistory history = GetRecentHistory();
    if (RemoveRecentEntry(history.Files, path))
        SetRecentHistory(history);
}

void RemoveRecentDirectory(const std::wstring& path)
{
    ViewerRecentHistory history = GetRecentHistory();
    if (RemoveRecentEntry(history.Directories, path))
        SetRecentHistory(history);
}

bool CloseAllViewerWindows(bool force)
{
    if (Services().CloseAllViewers)
    {
        bool result = true;
        if (Services().CloseAllViewers(force, result))
            return result;
    }

    const std::vector<HWND> windows = ViewerWindows.Snapshot();
    for (HWND hwnd : windows)
        PostMessageW(hwnd, WM_CLOSE, 0, 0);

    const DWORD start = GetTickCount();
    const DWORD waitMs = force ? 5000u : 1000u;
    while (!ViewerWindows.Empty())
    {
        if (GetTickCount() - start >= waitMs)
            break;
        Sleep(50);
    }

    return ViewerWindows.Empty();
}


ViewerCopyToHistory GetCopyToHistory()
{
    std::lock_guard<std::mutex> lock(CopyToHistoryMutex);
    ViewerCopyToHistory history;
    history.Targets = CopyToHistory;
    history.LastIndex = std::clamp(CopyToLastIndex, 0, CopyToLineCount - 1);
    return history;
}

void SetCopyToHistory(const ViewerCopyToHistory& history)
{
    std::lock_guard<std::mutex> lock(CopyToHistoryMutex);
    CopyToHistory = history.Targets;
    CopyToLastIndex = std::clamp(history.LastIndex, 0, CopyToLineCount - 1);
}

ViewerRecentHistory GetRecentHistory()
{
    std::lock_guard<std::mutex> lock(RecentHistoryMutex);
    ViewerRecentHistory history;
    history.Files = RecentFiles;
    history.Directories = RecentDirectories;
    return history;
}

void SetRecentHistory(const ViewerRecentHistory& history)
{
    std::lock_guard<std::mutex> lock(RecentHistoryMutex);
    RecentFiles = history.Files;
    RecentDirectories = history.Directories;
}

void ClearRecentHistory()
{
    SetRecentHistory(ViewerRecentHistory{});

    const std::vector<HWND> windows = ViewerWindows.Snapshot();
    for (HWND hwnd : windows)
        PostMessageW(hwnd, WM_PICTVIEW_REFRESH_MAIN_MENU, 0, 0);
}

ViewerPreferences GetViewerPreferences()
{
    std::lock_guard<std::mutex> lock(ViewerPreferencesMutex);
    return GlobalViewerPreferences;
}

void SetViewerPreferences(const ViewerPreferences& preferences)
{
    std::lock_guard<std::mutex> lock(ViewerPreferencesMutex);
    GlobalViewerPreferences = preferences;
}

HistogramLevels ComputeHistogramLevels(const ImageHistogram& histogram)
{
    const std::array<uint64_t, 256>* channels[5] = {&histogram.Luminosity, &histogram.Red, &histogram.Green, &histogram.Blue,
                                                     &histogram.Rgb};
    double average = 0;
    for (const std::array<uint64_t, 256>* buckets : channels)
        for (uint64_t bucket : *buckets)
            average += static_cast<double>(bucket) / (5.0 * 256.0);
    const double peak = (std::max)(10.0, std::floor(average * 4));

    HistogramLevels levels = {};
    for (size_t c = 0; c < 5; ++c)
        for (size_t i = 0; i < 256; ++i)
            levels[c][i] = (std::min)(1.0, static_cast<double>((*channels[c])[i]) / peak);
    return levels;
}

ViewerHistogramPreferences GetHistogramPreferences()
{
    std::lock_guard<std::mutex> lock(HistogramPreferencesMutex);
    return SanitizeHistogramPreferences(GlobalHistogramPreferences);
}

void SetHistogramPreferences(const ViewerHistogramPreferences& preferences)
{
    std::lock_guard<std::mutex> lock(HistogramPreferencesMutex);
    GlobalHistogramPreferences = SanitizeHistogramPreferences(preferences);
}

ViewerMetadataDetailsPreferences GetMetadataDetailsPreferences()
{
    std::lock_guard<std::mutex> lock(MetadataDetailsPreferencesMutex);
    return SanitizeMetadataDetailsPreferences(GlobalMetadataDetailsPreferences);
}

void SetMetadataDetailsPreferences(const ViewerMetadataDetailsPreferences& preferences)
{
    std::lock_guard<std::mutex> lock(MetadataDetailsPreferencesMutex);
    GlobalMetadataDetailsPreferences = SanitizeMetadataDetailsPreferences(preferences);
}

namespace
{
std::atomic<bool> ConfigurationOpen{false};
void (*ThumbnailSettingsChanged)() = nullptr;
} // namespace

void SetThumbnailSettingsChangedHandler(void (*handler)())
{
    ThumbnailSettingsChanged = handler;
}

bool EditViewerPreferences(HWND parent, HINSTANCE instance)
{
    if (ConfigurationOpen.exchange(true))
    {
        ViewerMessageBox(parent, ViewerText(IDS_CFG_CONFLICT), MB_OK | MB_ICONINFORMATION);
        return false;
    }
    const ViewerPreferences original = GetViewerPreferences();
    ViewerPreferences preferences = original;
    const bool accepted = Services().PromptConfiguration ? Services().PromptConfiguration(parent, instance, preferences)
                                                         : PromptViewerPreferences(parent, instance, preferences);
    ConfigurationOpen = false;
    if (!accepted)
        return false;

    SetViewerPreferences(preferences);
    NotifyViewerHostEvent(ViewerHostEvent::ConfigurationChanged);
    if (ThumbnailSettingsChanged != nullptr &&
        (original.IgnoreThumbnails != preferences.IgnoreThumbnails || original.AutoRotate != preferences.AutoRotate ||
         original.MaxThumbImgSize != preferences.MaxThumbImgSize))
    {
        ThumbnailSettingsChanged();
    }
    return true;
}

void NotifyViewerHostEvent(ViewerHostEvent event)
{
    if (Services().HostEvent)
    {
        Services().HostEvent(event);
        return;
    }

    const std::vector<HWND> windows = ViewerWindows.Snapshot();
    for (HWND hwnd : windows)
        PostMessageW(hwnd, WM_PICTVIEW_HOST_EVENT, static_cast<WPARAM>(event), 0);
}

void ShowAboutDialog(HWND parent)
{
    if (Services().AboutDialog)
    {
        Services().AboutDialog(parent);
        return;
    }
    ShowAboutPictViewDialog(parent);
}

PluginServices& Services()
{
    static PluginServices services;
    return services;
}


BOOL OpenViewerWindowWithStartup(ViewerStartupAction startupAction,
                                 const std::wstring& path,
                                 int left,
                                 int top,
                                 int width,
                                 int height,
                                 UINT showCmd,
                                 BOOL alwaysOnTop,
                                 BOOL returnLock,
                                 HANDLE* lock,
                                 BOOL* lockOwner,
                                 const ViewerHostActions& hostActions,
                                 const ViewerSourceNavigation& sourceNavigation)
{
    if (startupAction == ViewerStartupAction::LoadPath && path.empty())
        return FALSE;

    ViewerThreadData data;
    data.Path = path;
    data.StartupAction = startupAction;
    data.Left = left;
    data.Top = top;
    data.Width = width;
    data.Height = height;
    data.ShowCmd = showCmd;
    data.AlwaysOnTop = alwaysOnTop;
    data.ReturnLock = returnLock;
    data.Lock = lock;
    data.LockOwner = lockOwner;
    data.HostActions = hostActions;
    data.SourceNavigation = sourceNavigation;
    data.Continue = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (data.Continue == nullptr)
        return FALSE;

    HANDLE thread = CreateThread(nullptr, 0, ViewerThreadProc, &data, 0, nullptr);
    if (thread != nullptr)
    {
        WaitForSingleObject(data.Continue, INFINITE);
        CloseHandle(thread);
    }
    else
    {
        data.Success = FALSE;
    }

    CloseHandle(data.Continue);
    return data.Success;
}

BOOL OpenViewerWindow(const std::wstring& path,
                      int left,
                      int top,
                      int width,
                      int height,
                      UINT showCmd,
                      BOOL alwaysOnTop,
                      BOOL returnLock,
                      HANDLE* lock,
                      BOOL* lockOwner,
                      const ViewerHostActions& hostActions,
                      const ViewerSourceNavigation& sourceNavigation)
{
    return OpenViewerWindowWithStartup(ViewerStartupAction::LoadPath, path, left, top, width, height,
                                       showCmd, alwaysOnTop, returnLock, lock, lockOwner,
                                       hostActions, sourceNavigation);
}

BOOL OpenClipboardViewerWindow(int left,
                               int top,
                               int width,
                               int height,
                               UINT showCmd,
                               BOOL alwaysOnTop)
{
    ViewerHostActions hostActions;
    ViewerSourceNavigation sourceNavigation;
    return OpenViewerWindowWithStartup(ViewerStartupAction::PasteClipboard, std::wstring(), left, top,
                                       width, height, showCmd, alwaysOnTop, FALSE, nullptr, nullptr,
                                       hostActions, sourceNavigation);
}

BOOL OpenScreenCaptureViewerWindow(int left,
                                   int top,
                                   int width,
                                   int height,
                                   UINT showCmd,
                                   BOOL alwaysOnTop)
{
    ViewerHostActions hostActions;
    ViewerSourceNavigation sourceNavigation;
    return OpenViewerWindowWithStartup(ViewerStartupAction::ScreenCapture, std::wstring(), left, top,
                                       width, height, showCmd, alwaysOnTop, FALSE, nullptr, nullptr,
                                       hostActions, sourceNavigation);
}

} // namespace pictview
