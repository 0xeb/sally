// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"
#include <time.h>
//#ifdef MSVC_RUNTIME_CHECKS
#include <rtcapi.h>
//#endif // MSVC_RUNTIME_CHECKS

#include "allochan.h"
#include "common/fsutil.h" // GetRootPath / IsUNCPathW / IsTheSamePath
#include "menu.h"
#include "cfgdlg.h"
#include "plugins.h"
#include "fileswnd.h"
#include "mainwnd.h"
#include "shellib.h"
#include "worker.h"
#include "snooper.h"
#include "viewer.h"
#include "ui/IPrompter.h"
#include "common/Win32TextCodec.h"
#include "common/DiagnosticTextEncoding.h"
#include "common/unicode/helpers.h"
#include "common/IEnvironment.h"
#include "common/ExternalToolRunner.h"
#include "common/IRegistry.h"
#include "ui/IPrompter.h"
#include "editwnd.h"
#include "find.h"
#include "zip.h"
#include "pack.h"
#include "cache.h"
#include "dialogs.h"
#include "gui.h"
#include "tasklist.h"
#include <uxtheme.h>
#include "olespy.h"
#include "geticon.h"
#include "logo.h"
#include "color.h"
#include "toolbar.h"
#include "darkmode.h"
#include "bottombar_keycaps.h"
#include "salext_cleanup.h"
#include "common/IFileSystem.h"

// Issue #82: shell-extension DLLs from OLD Sally/Salamander installs stay loaded/locked by
// Explorer after an upgrade, keeping their install folders undeletable. The CLSID-family sweep
// lives in salext_cleanup.cpp so it can run headlessly against fake interfaces; this wrapper
// supplies the OS-abstraction interfaces and traces the outcome.
// wide: the caller already has a genuine wide path (BuildModuleRelativePathW) -
// used to narrow-then-rewiden here via MultiByteToWideChar, which could not recover characters
// the CALLER's own narrowing (a since-removed GetModuleFileName/CP_ACP step) had already lost.
static void CleanupStaleShellExtensions(const wchar_t* currentSalextPathW)
{
    IRegistry* registry = gRegistry != NULL ? gRegistry : GetWin32Registry();
    IFileSystem* fs = gFileSystem != NULL ? gFileSystem : GetWin32FileSystem();

    SalextCleanupStats stats = ReclaimStaleSalextRegistrations(currentSalextPathW != NULL ? currentSalextPathW : L"", registry, fs);

    if (stats.stale > 0)
        TRACE_I("CleanupStaleShellExtensions(): " << stats.stale << " stale registration(s), "
                                                  << stats.scheduled << " scheduled for reboot-delete, "
                                                  << stats.keysRemoved << " CLSID key(s) removed");
    if (stats.scheduled < stats.stale)
        TRACE_I("CleanupStaleShellExtensions(): reboot-delete not scheduled for all (administrator rights may be required)");
}

static BOOL FileExistsWLocal(const wchar_t* path)
{
    IFileSystem* fs = gFileSystem != NULL ? gFileSystem : GetWin32FileSystem();
    DWORD attr = fs->GetFileAttributes(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        DWORD err = GetLastError();
        return err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static BOOL GetOurPathInRoamingAPPDATAW(std::wstring& path)
{
    wchar_t buf[MAX_PATH];
    buf[0] = L'\0';
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, buf) != S_OK)
        return FALSE;
    path.assign(buf);
    if (!path.empty() && path.back() != L'\\')
        path.push_back(L'\\');
    path.append(L"Sally");
    return TRUE;
}

static BOOL BuildModuleRelativePathW(HINSTANCE module, const wchar_t* relativePath, std::wstring& path)
{
    DWORD capacity = 256;
    for (;;)
    {
        std::wstring modulePath;
        modulePath.resize(capacity);

        SetLastError(ERROR_SUCCESS);
        DWORD len = GetModuleFileNameW(module, &modulePath[0], capacity);
        if (len == 0)
            return FALSE;

        DWORD err = GetLastError();
        // Some Windows versions report truncation as capacity - 1 plus ERROR_INSUFFICIENT_BUFFER.
        BOOL truncated = len >= capacity || (len == capacity - 1 && err == ERROR_INSUFFICIENT_BUFFER);
        if (!truncated)
        {
            modulePath.resize(len);
            size_t slash = modulePath.find_last_of(L"\\/");
            if (slash == std::wstring::npos)
                return FALSE;
            path.assign(modulePath, 0, slash + 1);
            path.append(relativePath);
            return TRUE;
        }

        if (capacity > MAXDWORD / 2)
            return FALSE;
        capacity *= 2;
    }
}

static IRegistry* GetMainSalamanderRegistry()
{
    return gRegistry != nullptr ? gRegistry : GetWin32Registry();
}

#include "svg.h"

extern "C"
{
#include "shexreg.h"
}
#include "salshlib.h"
#include "shiconov.h"
#include "salmoncl.h"
#include "jumplist.h"
#include "usermenu.h"
#include "execute.h"
#include "drivelst.h"

#ifndef SALLY_E2E_HOST
#pragma comment(linker, "/ENTRY:MyEntryPoint") // we want our own application entry point
#endif

#pragma comment(lib, "uxtheme.lib")

// make the original application entry point accessible
extern "C" int WinMainCRTStartup();

#ifdef X64_STRESS_TEST

#define X64_STRESS_TEST_ALLOC_COUNT 1000

LPVOID X64StressTestPointers[X64_STRESS_TEST_ALLOC_COUNT];

void X64StressTestAlloc()
{
    // at this point the loader has already loaded EXE and RTL, and during RTL initialization
    // the heap was created and allocated at addresses below 4GB; to push further allocations
    // above 4GB, we need to occupy the lower part of virtual memory and then force RTL
    // to expand its heap through allocations
    //
    // occupy space in virtual memory
    UINT64 vaAllocated = 0;
    _int64 allocSize[] = {10000000, 1000000, 100000, 10000, 1000, 100, 10, 1, 0};
    for (int i = 0; allocSize[i] != 0; i++)
        while (VirtualAlloc(0, allocSize[i], MEM_RESERVE, PAGE_NOACCESS) <= (LPVOID)(UINT_PTR)0x00000000ffffffff) // we want an exception on access and don't want MEM_COMMIT to avoid wasting memory
            vaAllocated += allocSize[i];

    // now inflate the RTL heap
    UINT64 rtlAllocated = 0;
    _int64 rtlAllocSize[] = {10000000, 1000000, 100000, 10000, 1000, 100, 10, 1, 0};
    for (int i = 0; rtlAllocSize[i] != 0; i++)
        while (_malloc_dbg(rtlAllocSize[i], _CRT_BLOCK, __FILE__, __LINE__) <= (LPVOID)(UINT_PTR)0x00000000ffffffff)
            rtlAllocated += rtlAllocSize[i];

    // verify success
    void* testNew = new char; // new goes through alloc, but let's verify anyway
    if (testNew <= (LPVOID)(UINT_PTR)0x00000000ffffffff)
        MessageBoxW(NULL, L"new address <= 0x00000000ffffffff!\nPlease open an issue at https://github.com/0xeb/sally/issues with this information.", L"X64_STRESS_TEST", MB_OK | MB_ICONEXCLAMATION);
    delete testNew;
}

#endif //X64_STRESS_TEST
// our own entry point, which we requested from the linker via pragma
int MyEntryPoint()
{
#ifdef X64_STRESS_TEST
    // consume the lower 4GB of memory with allocations, so that further allocations have pointers larger than DWORD
    X64StressTestAlloc();
#endif //X64_STRESS_TEST

    int ret = 1; // error

    // start Salmon, we want it to catch as many of our crashes as possible
    if (SalmonInit())
    {
        // call the original application entry point to start the program
        ret = WinMainCRTStartup();
    }
    else
        // wide: same MessageBoxW/SALAMANDER_TEXT_VERSIONW() pairing already used
        // elsewhere in this file (e.g. below, around the SalmonInit()-adjacent error paths).
        MessageBoxW(NULL, L"Sally Bug Reporter (salmon.exe) initialization has failed. Please reinstall Sally.",
                    SALAMANDER_TEXT_VERSIONW(), MB_OK | MB_ICONSTOP);

    // the debugger no longer reaches here, we get killed in RTL (tested under VC 2008 with our RTL)

    // we're done
    return ret;
}

BOOL SalamanderBusy = TRUE;       // is Salamander busy?
DWORD LastSalamanderIdleTime = 0; // GetTickCount() from the moment when SalamanderBusy last changed to TRUE

int PasteLinkIsRunning = 0; // if greater than zero, the Paste Shortcuts command is currently running in one of the panels

BOOL CannotCloseSalMainWnd = FALSE; // TRUE = the main window must not be closed

DWORD MainThreadID = -1;

int MenuNewExceptionHasOccured = 0;
int FGIExceptionHasOccured = 0;
int ICExceptionHasOccured = 0;
int QCMExceptionHasOccured = 0;
int OCUExceptionHasOccured = 0;
int GTDExceptionHasOccured = 0;
int SHLExceptionHasOccured = 0;
int RelExceptionHasOccured = 0;

std::wstring DecimalSeparator = L".";
std::wstring ThousandsSeparator = L" ";

BOOL WindowsXP64AndLater = FALSE;  // JRYFIXME - remove
BOOL WindowsVistaAndLater = FALSE; // JRYFIXME - remove
BOOL Windows7AndLater = FALSE;     // JRYFIXME - remove
BOOL Windows8AndLater = FALSE;
BOOL Windows8_1AndLater = FALSE;
BOOL Windows10AndLater = FALSE;

BOOL Windows64Bit = FALSE;

BOOL RunningAsAdmin = FALSE;

DWORD CCVerMajor = 0;
DWORD CCVerMinor = 0;

std::wstring ConfigurationNameW; // Authoritative wide path (supports non-ASCII install dirs)
BOOL ConfigurationNameIgnoreIfNotExists = TRUE;

int StopRefresh = 0;

BOOL ExecCmdsOrUnloadMarkedPlugins = FALSE;
BOOL OpenPackOrUnpackDlgForMarkedPlugins = FALSE;

int StopIconRepaint = 0;
BOOL PostAllIconsRepaint = FALSE;

int StopStatusbarRepaint = 0;
BOOL PostStatusbarRepaint = FALSE;

int ChangeDirectoryAllowed = 0;
BOOL ChangeDirectoryRequest = FALSE;

BOOL SkipOneActivateRefresh = FALSE;

std::wstring DirColumnStrW;
int DirColumnStrWLen = 0;
std::wstring ColExtStrW;
int ColExtStrWLen = 0;
int TextEllipsisWidth = 0;
int TextEllipsisWidthEnv = 0;
std::wstring ProgDlgHoursStr;
std::wstring ProgDlgMinutesStr;
std::wstring ProgDlgSecsStr;

wchar_t FolderTypeName[80] = L"";
int FolderTypeNameLen = 0;
std::wstring UpDirTypeName;
int UpDirTypeNameLen = 0;
std::wstring CommonFileTypeName;
int CommonFileTypeNameLen = 0;
std::wstring CommonFileTypeName2;

std::wstring WindowsDirectory;

// to ensure escape from removed drives to fixed drive (after device ejection - USB flash disk, etc.)
BOOL ChangeLeftPanelToFixedWhenIdleInProgress = FALSE; // TRUE = path is currently being changed, setting ChangeLeftPanelToFixedWhenIdle to TRUE is unnecessary
BOOL ChangeLeftPanelToFixedWhenIdle = FALSE;
BOOL ChangeRightPanelToFixedWhenIdleInProgress = FALSE; // TRUE = path is currently being changed, setting ChangeRightPanelToFixedWhenIdle to TRUE is unnecessary
BOOL ChangeRightPanelToFixedWhenIdle = FALSE;
BOOL OpenCfgToChangeIfPathIsInaccessibleGoTo = FALSE; // TRUE = in idle opens configuration to Drives and focuses "If path in panel is inaccessible, go to:"

wchar_t IsSLGIncomplete[ISSLGINCOMPLETE_SIZE]; // if the string is empty, SLG is completely translated; otherwise contains URL to forum section for the given language

UINT TaskbarBtnCreatedMsg = 0;

// ****************************************************************************

C__MainWindowCS MainWindowCS;
BOOL CanDestroyMainWindow = FALSE;
CMainWindow* MainWindow = NULL;
CFilesWindow* DropSourcePanel = NULL;
BOOL OurClipDataObject = FALSE;
const char* SALCF_IDATAOBJECT = "SalIDataObject";
const char* SALCF_FAKE_REALPATH = "SalFakeRealPath";
const char* SALCF_FAKE_SRCTYPE = "SalFakeSrcType";
const char* SALCF_FAKE_SRCFSPATH = "SalFakeSrcFSPath";

const wchar_t* MAINWINDOW_NAME = L"Sally";
const wchar_t* CMAINWINDOW_CLASSNAME = L"SalamanderMainWindowVer25";
const wchar_t* SAVEBITS_CLASSNAME = L"SalamanderSaveBits";
const wchar_t* SAFEWAIT_CLASSNAMEW = L"SalamanderSafeWaitWindow";
const wchar_t* SHELLEXECUTE_CLASSNAMEW = L"SalamanderShellExecute";

CAssociations Associations; // associations loaded from registry
CShares Shares;

std::wstring DefaultDir['Z' - 'A' + 1];

HACCEL AccelTable1 = NULL;
HACCEL AccelTable2 = NULL;

HINSTANCE NtDLL = NULL;             // handle to ntdll.dll
HINSTANCE Shell32DLL = NULL;        // handle to shell32.dll (icons)
HINSTANCE ImageResDLL = NULL;       // handle to imageres.dll (icons - Vista)
HINSTANCE User32DLL = NULL;         // handle to user32.dll (DisableProcessWindowsGhosting)
HINSTANCE HLanguage = NULL;         // handle to language-dependent resources (.SPL file)
std::wstring CurrentHelpDir; // after first use of help, this contains path to help directory (location of all .chm files)
WORD LanguageID = 0;                // language-id of .SPL file

std::wstring OpenReadmeInNotepad; // used only when launched from installer: filename to open in notepad during IDLE (start notepad)

BOOL UseCustomPanelFont = FALSE;
HFONT Font = NULL;
HFONT FontUL = NULL;
LOGFONT LogFont;
int FontCharHeight = 0;

HFONT EnvFont = NULL;
HFONT EnvFontBold = NULL;
HFONT EnvFontUL = NULL;
//LOGFONT EnvLogFont;
int EnvFontCharHeight = 0;
HFONT TooltipFont = NULL;

HBRUSH HNormalBkBrush = NULL;
HBRUSH HFocusedBkBrush = NULL;
HBRUSH HSelectedBkBrush = NULL;
HBRUSH HFocSelBkBrush = NULL;
HBRUSH HDialogBrush = NULL;
HBRUSH HButtonTextBrush = NULL;
HBRUSH HDitherBrush = NULL;
HBRUSH HActiveCaptionBrush = NULL;
HBRUSH HInactiveCaptionBrush = NULL;

HBRUSH HMenuSelectedBkBrush = NULL;
HBRUSH HMenuSelectedTextBrush = NULL;
HBRUSH HMenuHilightBrush = NULL;
HBRUSH HMenuGrayTextBrush = NULL;

HPEN HActiveNormalPen = NULL; // pens for frame around item
HPEN HActiveSelectedPen = NULL;
HPEN HInactiveNormalPen = NULL;
HPEN HInactiveSelectedPen = NULL;

HPEN HThumbnailNormalPen = NULL; // pens for frame around thumbnail
HPEN HThumbnailFucsedPen = NULL;
HPEN HThumbnailSelectedPen = NULL;
HPEN HThumbnailFocSelPen = NULL;

HPEN BtnShadowPen = NULL;
HPEN BtnHilightPen = NULL;
HPEN Btn3DLightPen = NULL;
HPEN BtnFacePen = NULL;
HPEN WndFramePen = NULL;
HPEN WndPen = NULL;
HBITMAP HFilter = NULL;
HBITMAP HHeaderSort = NULL;

HIMAGELIST HFindSymbolsImageList = NULL;
HIMAGELIST HMenuMarkImageList = NULL;
HIMAGELIST HGrayToolBarImageList = NULL;
HIMAGELIST HHotToolBarImageList = NULL;
HIMAGELIST HBottomTBImageList = NULL;
HIMAGELIST HHotBottomTBImageList = NULL;

CBitmap ItemBitmap;

HBITMAP HUpDownBitmap = NULL;
HBITMAP HZoomBitmap = NULL;

//HBITMAP HWorkerBitmap = NULL;

HCURSOR HHelpCursor = NULL;

int SystemDPI = 0; // Global DPI across all monitors. Salamander does not support Per-Monitor DPI, see https://msdn.microsoft.com/library/windows/desktop/dn469266.aspx
int IconSizes[] = {16, 32, 48};
int IconLRFlags = 0;
HICON HSharedOverlays[] = {0};
HICON HShortcutOverlays[] = {0};
HICON HSlowFileOverlays[] = {0};
CIconList* SimpleIconLists[] = {0};
CIconList* ThrobberFrames = NULL;
CIconList* LockFrames = NULL; // for simplicity declared and loaded as throbber

HICON HGroupIcon = NULL;
HICON HFavoritIcon = NULL;
HICON HSlowFileIcon = NULL;

RGBQUAD ColorTable[256] = {0};

DWORD MouseHoverTime = 0;

SYSTEMTIME SalamanderStartSystemTime = {0}; // Salamander start time (GetSystemTime)

BOOL WaitForESCReleaseBeforeTestingESC = FALSE; // should we wait for ESC release before starting path browsing in panel?

int SPACE_WIDTH = 10;

const char* LOW_MEMORY = "Low memory.";

BOOL DragFullWindows = TRUE;

CWindowQueue ViewerWindowQueue(L"Internal Viewers");

CFindSetDialog GlobalFindDialog(NULL /* ignored */, 0 /* ignored */, 0 /* ignored */);

CNames GlobalSelection;

HWND PluginProgressDialog = NULL;
HWND PluginMsgBoxParent = NULL;

BOOL CriticalShutdown = FALSE;

// mutex for synchronizing load/save to Registry (two processes cannot do it at once, it has unpleasant consequences)
CLoadSaveToRegistryMutex LoadSaveToRegistryMutex;

BOOL IsNotAlphaNorNum[256]; // TRUE/FALSE array for characters (TRUE = not a letter or digit)
BOOL IsAlpha[256];          // TRUE/FALSE array for characters (TRUE = letter)

// default user's charset for fonts; under W2K+ DEFAULT_CHARSET would suffice
//
// Under WinXP you can choose Czech as the default in regional settings,
// but not install Czech fonts on the Advanced tab. Then when constructing
// a font with UserCharset encoding, the operating system returns a font with
// a completely different name (face name), mainly to have the required encoding. Therefore it is IMPORTANT
// when specifying font parameters to correctly choose the lfPitchAndFamily variable,
// where you can choose between FF_SWISS and FF_ROMAN fonts (sans-serif/serif).
int UserCharset = DEFAULT_CHARSET;

DWORD AllocationGranularity = 1; // allocation granularity (needed for using memory-mapped files)

#ifdef USE_BETA_EXPIRATION_DATE

// specifies the first day when this beta/PB version will no longer run
// beta/PB version 4.0 beta 1 will run only until February 1, 2020
//                                 YEAR  MONTH DAY
SYSTEMTIME BETA_EXPIRATION_DATE = {2020, 2, 0, 1, 0, 0, 0, 0};
#endif // USE_BETA_EXPIRATION_DATE

//******************************************************************************
//
// Idle processing control (CMainWindow::OnEnterIdle)
//

BOOL IdleRefreshStates = TRUE;  // initially let the variables be set
BOOL IdleForceRefresh = FALSE;  // invalidates Enabler* cache
BOOL IdleCheckClipboard = TRUE; // we'll also check the clipboard

DWORD EnablerUpDir = FALSE;
DWORD EnablerRootDir = FALSE;
DWORD EnablerForward = FALSE;
DWORD EnablerBackward = FALSE;
DWORD EnablerFileOnDisk = FALSE;
DWORD EnablerLeftFileOnDisk = FALSE;
DWORD EnablerRightFileOnDisk = FALSE;
DWORD EnablerFileOnDiskOrArchive = FALSE;
DWORD EnablerFileOrDirLinkOnDisk = FALSE;
DWORD EnablerFiles = FALSE;
DWORD EnablerFilesOnDisk = FALSE;
DWORD EnablerFilesOnDiskCompress = FALSE;
DWORD EnablerFilesOnDiskEncrypt = FALSE;
DWORD EnablerFilesOnDiskOrArchive = FALSE;
DWORD EnablerOccupiedSpace = FALSE;
DWORD EnablerFilesCopy = FALSE;
DWORD EnablerFilesMove = FALSE;
DWORD EnablerFilesDelete = FALSE;
DWORD EnablerFileDir = FALSE;
DWORD EnablerFileDirANDSelected = FALSE;
DWORD EnablerQuickRename = FALSE;
DWORD EnablerOnDisk = FALSE;
DWORD EnablerCalcDirSizes = FALSE;
DWORD EnablerPasteFiles = FALSE;
DWORD EnablerPastePath = FALSE;
DWORD EnablerPasteLinks = FALSE;
DWORD EnablerPasteSimpleFiles = FALSE;
DWORD EnablerPasteDefEffect = FALSE;
DWORD EnablerPasteFilesToArcOrFS = FALSE;
DWORD EnablerPaste = FALSE;
DWORD EnablerPasteLinksOnDisk = FALSE;
DWORD EnablerSelected = FALSE;
DWORD EnablerUnselected = FALSE;
DWORD EnablerHiddenNames = FALSE;
DWORD EnablerSelectionStored = FALSE;
DWORD EnablerGlobalSelStored = FALSE;
DWORD EnablerSelGotoPrev = FALSE;
DWORD EnablerSelGotoNext = FALSE;
DWORD EnablerLeftUpDir = FALSE;
DWORD EnablerRightUpDir = FALSE;
DWORD EnablerLeftRootDir = FALSE;
DWORD EnablerRightRootDir = FALSE;
DWORD EnablerLeftForward = FALSE;
DWORD EnablerRightForward = FALSE;
DWORD EnablerLeftBackward = FALSE;
DWORD EnablerRightBackward = FALSE;
DWORD EnablerFileHistory = FALSE;
DWORD EnablerDirHistory = FALSE;
DWORD EnablerCustomizeLeftView = FALSE;
DWORD EnablerCustomizeRightView = FALSE;
DWORD EnablerDriveInfo = FALSE;
DWORD EnablerCreateDir = FALSE;
DWORD EnablerViewFile = FALSE;
DWORD EnablerChangeAttrs = FALSE;
DWORD EnablerShowProperties = FALSE;
DWORD EnablerItemsContextMenu = FALSE;
DWORD EnablerOpenActiveFolder = FALSE;
DWORD EnablerPermissions = FALSE;

COLORREF* CurrentColors = SalamanderColors;

COLORREF UserColors[NUMBER_OF_COLORS];

SALCOLOR ViewerColors[NUMBER_OF_VIEWERCOLORS] =
    {
        RGBF(0, 0, 0, SCF_DEFAULT),       // VIEWER_FG_NORMAL
        RGBF(255, 255, 255, SCF_DEFAULT), // VIEWER_BK_NORMAL
        RGBF(255, 255, 255, SCF_DEFAULT), // VIEWER_FG_SELECTED
        RGBF(0, 0, 0, SCF_DEFAULT),       // VIEWER_BK_SELECTED
};

COLORREF SalamanderColors[NUMBER_OF_COLORS] =
    {
        // pen colors for frame around item
        RGBF(0, 0, 0, SCF_DEFAULT),       // FOCUS_ACTIVE_NORMAL
        RGBF(0, 0, 0, SCF_DEFAULT),       // FOCUS_ACTIVE_SELECTED
        RGBF(128, 128, 128, 0),           // FOCUS_FG_INACTIVE_NORMAL
        RGBF(128, 128, 128, 0),           // FOCUS_FG_INACTIVE_SELECTED
        RGBF(255, 255, 255, SCF_DEFAULT), // FOCUS_BK_INACTIVE_NORMAL
        RGBF(255, 255, 255, SCF_DEFAULT), // FOCUS_BK_INACTIVE_SELECTED

        // text colors of items in panel
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_NORMAL
        RGBF(255, 0, 0, 0),         // ITEM_FG_SELECTED
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_FOCUSED
        RGBF(255, 0, 0, 0),         // ITEM_FG_FOCSEL
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_HIGHLIGHT

        // background colors of items in panel
        RGBF(255, 255, 255, SCF_DEFAULT), // ITEM_BK_NORMAL
        RGBF(255, 255, 255, SCF_DEFAULT), // ITEM_BK_SELECTED
        RGBF(232, 232, 232, 0),           // ITEM_BK_FOCUSED
        RGBF(232, 232, 232, 0),           // ITEM_BK_FOCSEL
        RGBF(0, 0, 0, SCF_DEFAULT),       // ITEM_BK_HIGHLIGHT

        // colors for icon blend
        RGBF(255, 128, 128, SCF_DEFAULT), // ICON_BLEND_SELECTED
        RGBF(128, 128, 128, 0),           // ICON_BLEND_FOCUSED
        RGBF(255, 0, 0, 0),               // ICON_BLEND_FOCSEL

        // progress bar colors
        RGBF(0, 0, 192, SCF_DEFAULT),     // PROGRESS_FG_NORMAL
        RGBF(255, 255, 255, SCF_DEFAULT), // PROGRESS_FG_SELECTED
        RGBF(255, 255, 255, SCF_DEFAULT), // PROGRESS_BK_NORMAL
        RGBF(0, 0, 192, SCF_DEFAULT),     // PROGRESS_BK_SELECTED

        // hot item colors
        RGBF(0, 0, 255, SCF_DEFAULT),     // HOT_PANEL
        RGBF(128, 128, 128, SCF_DEFAULT), // HOT_ACTIVE
        RGBF(128, 128, 128, SCF_DEFAULT), // HOT_INACTIVE

        // panel caption colors
        RGBF(255, 255, 255, SCF_DEFAULT), // ACTIVE_CAPTION_FG
        RGBF(0, 0, 128, SCF_DEFAULT),     // ACTIVE_CAPTION_BK
        RGBF(255, 255, 255, SCF_DEFAULT), // INACTIVE_CAPTION_FG
        RGBF(128, 128, 128, SCF_DEFAULT), // INACTIVE_CAPTION_BK

        // pen colors for frame around thumbnails
        RGBF(192, 192, 192, 0), // THUMBNAIL_FRAME_NORMAL
        RGBF(0, 0, 0, 0),       // THUMBNAIL_FRAME_FOCUSED
        RGBF(255, 0, 0, 0),     // THUMBNAIL_FRAME_SELECTED
        RGBF(128, 0, 0, 0),     // THUMBNAIL_FRAME_FOCSEL
};

COLORREF ExplorerColors[NUMBER_OF_COLORS] =
    {
        // pen colors for frame around item
        RGBF(0, 0, 0, SCF_DEFAULT),       // FOCUS_ACTIVE_NORMAL
        RGBF(255, 255, 0, 0),             // FOCUS_ACTIVE_SELECTED
        RGBF(128, 128, 128, 0),           // FOCUS_FG_INACTIVE_NORMAL
        RGBF(0, 0, 128, 0),               // FOCUS_FG_INACTIVE_SELECTED
        RGBF(255, 255, 255, SCF_DEFAULT), // FOCUS_BK_INACTIVE_NORMAL
        RGBF(255, 255, 0, 0),             // FOCUS_BK_INACTIVE_SELECTED

        // text colors of items in panel
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_NORMAL
        RGBF(255, 255, 255, 0),     // ITEM_FG_SELECTED
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_FOCUSED
        RGBF(255, 255, 255, 0),     // ITEM_FG_FOCSEL
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_HIGHLIGHT

        // background colors of items in panel
        RGBF(255, 255, 255, SCF_DEFAULT), // ITEM_BK_NORMAL
        RGBF(0, 0, 128, 0),               // ITEM_BK_SELECTED
        RGBF(232, 232, 232, 0),           // ITEM_BK_FOCUSED
        RGBF(0, 0, 128, 0),               // ITEM_BK_FOCSEL
        RGBF(0, 0, 0, SCF_DEFAULT),       // ITEM_BK_HIGHLIGHT

        // colors for icon blend
        RGBF(0, 0, 128, SCF_DEFAULT), // ICON_BLEND_SELECTED
        RGBF(128, 128, 128, 0),       // ICON_BLEND_FOCUSED
        RGBF(0, 0, 128, 0),           // ICON_BLEND_FOCSEL

        // progress bar colors
        RGBF(0, 0, 192, SCF_DEFAULT),     // PROGRESS_FG_NORMAL
        RGBF(255, 255, 255, SCF_DEFAULT), // PROGRESS_FG_SELECTED
        RGBF(255, 255, 255, SCF_DEFAULT), // PROGRESS_BK_NORMAL
        RGBF(0, 0, 192, SCF_DEFAULT),     // PROGRESS_BK_SELECTED

        // hot item colors
        RGBF(0, 0, 255, SCF_DEFAULT),     // HOT_PANEL
        RGBF(128, 128, 128, SCF_DEFAULT), // HOT_ACTIVE
        RGBF(128, 128, 128, SCF_DEFAULT), // HOT_INACTIVE

        // panel caption colors
        RGBF(255, 255, 255, SCF_DEFAULT), // ACTIVE_CAPTION_FG
        RGBF(0, 0, 128, SCF_DEFAULT),     // ACTIVE_CAPTION_BK
        RGBF(255, 255, 255, SCF_DEFAULT), // INACTIVE_CAPTION_FG
        RGBF(128, 128, 128, SCF_DEFAULT), // INACTIVE_CAPTION_BK

        // pen colors for frame around thumbnails
        RGBF(192, 192, 192, 0), // THUMBNAIL_FRAME_NORMAL
        RGBF(0, 0, 128, 0),     // THUMBNAIL_FRAME_FOCUSED
        RGBF(0, 0, 128, 0),     // THUMBNAIL_FRAME_SELECTED
        RGBF(0, 0, 128, 0),     // THUMBNAIL_FRAME_FOCSEL
};

COLORREF NortonColors[NUMBER_OF_COLORS] =
    {
        // pen colors for frame around item
        RGBF(0, 128, 128, 0), // FOCUS_ACTIVE_NORMAL
        RGBF(0, 128, 128, 0), // FOCUS_ACTIVE_SELECTED
        RGBF(0, 128, 128, 0), // FOCUS_FG_INACTIVE_NORMAL
        RGBF(0, 128, 128, 0), // FOCUS_FG_INACTIVE_SELECTED
        RGBF(0, 0, 128, 0),   // FOCUS_BK_INACTIVE_NORMAL
        RGBF(0, 0, 128, 0),   // FOCUS_BK_INACTIVE_SELECTED

        // text colors of items in panel
        RGBF(0, 255, 255, 0),       // ITEM_FG_NORMAL
        RGBF(255, 255, 0, 0),       // ITEM_FG_SELECTED
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_FOCUSED
        RGBF(255, 255, 0, 0),       // ITEM_FG_FOCSEL
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_HIGHLIGHT

        // background colors of items in panel
        RGBF(0, 0, 128, 0),         // ITEM_BK_NORMAL
        RGBF(0, 0, 128, 0),         // ITEM_BK_SELECTED
        RGBF(0, 128, 128, 0),       // ITEM_BK_FOCUSED
        RGBF(0, 128, 128, 0),       // ITEM_BK_FOCSEL
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_BK_HIGHLIGHT

        // colors for icon blend
        RGBF(255, 255, 0, SCF_DEFAULT), // ICON_BLEND_SELECTED
        RGBF(128, 128, 128, 0),         // ICON_BLEND_FOCUSED
        RGBF(255, 255, 0, 0),           // ICON_BLEND_FOCSEL

        // progress bar colors
        RGBF(0, 0, 192, SCF_DEFAULT),     // PROGRESS_FG_NORMAL
        RGBF(255, 255, 255, SCF_DEFAULT), // PROGRESS_FG_SELECTED
        RGBF(255, 255, 255, SCF_DEFAULT), // PROGRESS_BK_NORMAL
        RGBF(0, 0, 192, SCF_DEFAULT),     // PROGRESS_BK_SELECTED

        // hot item colors
        RGBF(0, 0, 255, SCF_DEFAULT),     // HOT_PANEL
        RGBF(128, 128, 128, SCF_DEFAULT), // HOT_ACTIVE
        RGBF(128, 128, 128, SCF_DEFAULT), // HOT_INACTIVE

        // panel caption colors
        RGBF(255, 255, 255, SCF_DEFAULT), // ACTIVE_CAPTION_FG
        RGBF(0, 0, 128, SCF_DEFAULT),     // ACTIVE_CAPTION_BK
        RGBF(255, 255, 255, SCF_DEFAULT), // INACTIVE_CAPTION_FG
        RGBF(128, 128, 128, SCF_DEFAULT), // INACTIVE_CAPTION_BK

        // pen colors for frame around thumbnails
        RGBF(192, 192, 192, 0), // THUMBNAIL_FRAME_NORMAL
        RGBF(0, 128, 128, 0),   // THUMBNAIL_FRAME_FOCUSED
        RGBF(255, 255, 0, 0),   // THUMBNAIL_FRAME_SELECTED
        RGBF(255, 255, 0, 0),   // THUMBNAIL_FRAME_FOCSEL
};

COLORREF NavigatorColors[NUMBER_OF_COLORS] =
    {
        // pen colors for frame around item
        RGBF(0, 128, 128, 0), // FOCUS_ACTIVE_NORMAL
        RGBF(0, 128, 128, 0), // FOCUS_ACTIVE_SELECTED
        RGBF(0, 128, 128, 0), // FOCUS_FG_INACTIVE_NORMAL
        RGBF(0, 128, 128, 0), // FOCUS_FG_INACTIVE_SELECTED
        RGBF(0, 0, 128, 0),   // FOCUS_BK_INACTIVE_NORMAL
        RGBF(0, 0, 128, 0),   // FOCUS_BK_INACTIVE_SELECTED

        // text colors of items in panel
        RGBF(255, 255, 255, 0),     // ITEM_FG_NORMAL
        RGBF(255, 255, 0, 0),       // ITEM_FG_SELECTED
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_FOCUSED
        RGBF(255, 255, 0, 0),       // ITEM_FG_FOCSEL
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_HIGHLIGHT

        // background colors of items in panel
        RGBF(80, 80, 80, 0),        // ITEM_BK_NORMAL
        RGBF(80, 80, 80, 0),        // ITEM_BK_SELECTED
        RGBF(0, 128, 128, 0),       // ITEM_BK_FOCUSED
        RGBF(0, 128, 128, 0),       // ITEM_BK_FOCSEL
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_BK_HIGHLIGHT

        // colors for icon blend
        RGBF(255, 255, 0, SCF_DEFAULT), // ICON_BLEND_SELECTED
        RGBF(128, 128, 128, 0),         // ICON_BLEND_FOCUSED
        RGBF(255, 255, 0, 0),           // ICON_BLEND_FOCSEL

        // progress bar colors
        RGBF(0, 0, 192, SCF_DEFAULT),     // PROGRESS_FG_NORMAL
        RGBF(255, 255, 255, SCF_DEFAULT), // PROGRESS_FG_SELECTED
        RGBF(255, 255, 255, SCF_DEFAULT), // PROGRESS_BK_NORMAL
        RGBF(0, 0, 192, SCF_DEFAULT),     // PROGRESS_BK_SELECTED

        // hot item colors
        RGBF(0, 0, 255, SCF_DEFAULT),     // HOT_PANEL
        RGBF(173, 182, 205, SCF_DEFAULT), // HOT_ACTIVE
        RGBF(212, 212, 212, SCF_DEFAULT), // HOT_INACTIVE

        // panel caption colors
        RGBF(255, 255, 255, SCF_DEFAULT), // ACTIVE_CAPTION_FG
        RGBF(0, 0, 128, SCF_DEFAULT),     // ACTIVE_CAPTION_BK
        RGBF(255, 255, 255, SCF_DEFAULT), // INACTIVE_CAPTION_FG
        RGBF(128, 128, 128, SCF_DEFAULT), // INACTIVE_CAPTION_BK

        // pen colors for frame around thumbnails
        RGBF(192, 192, 192, 0), // THUMBNAIL_FRAME_NORMAL
        RGBF(0, 128, 128, 0),   // THUMBNAIL_FRAME_FOCUSED
        RGBF(255, 255, 0, 0),   // THUMBNAIL_FRAME_SELECTED
        RGBF(255, 255, 0, 0),   // THUMBNAIL_FRAME_FOCSEL
};

COLORREF CustomColors[NUMBER_OF_CUSTOMCOLORS] =
    {
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
};

//*****************************************************************************
//
// CRC32
//

static DWORD Crc32Tab[256];
static BOOL Crc32TabInitialized = FALSE;

void MakeCrc32Table(DWORD* crcTab)
{
    DWORD c;
    DWORD poly = 0xedb88320L; //polynomial exclusive-or pattern

    /*
  // generate crc polonomial, using precomputed poly should be faster
  // terms of polynomial defining this crc (except x^32):
  static const Byte p[] = {0,1,2,4,5,7,8,10,11,12,16,22,23,26};

  // make exclusive-or pattern from polynomial (0xedb88320L)
  poly = 0L;
  for (n = 0; n < sizeof(p)/sizeof(Byte); n++)
    poly |= 1L << (31 - p[n]);
*/
    int n;
    for (n = 0; n < 256; n++)
    {
        c = (UINT32)n;

        int k;
        for (k = 0; k < 8; k++)
            c = c & 1 ? poly ^ (c >> 1) : c >> 1;

        crcTab[n] = c;
    }
}

DWORD UpdateCrc32(const void* buffer, DWORD count, DWORD crcVal)
{
    CALL_STACK_MESSAGE_NONE

    if (buffer == NULL)
        return 0;

    if (!Crc32TabInitialized)
    {
        MakeCrc32Table(Crc32Tab);
        Crc32TabInitialized = TRUE;
    }

    BYTE* p = (BYTE*)buffer;
    DWORD c = crcVal ^ 0xFFFFFFFF;

    if (count)
        do
        {
            c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
        } while (--count);

    // Honza: I measured the following optimizations and they have no significance;
    // the only chance would be rewriting to ASM and reading memory by DWORDs,
    // from which individual bytes could then be extracted;
    // with the current release build settings, the compiler is not able to
    // perform this optimization for us.
    /*
  int remain = count % 8;
  count -= remain;
  while (remain)
  {
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    remain--;
  }
  while (count)
  {
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    count -= 8;
  }
*/
    /*
  int remain = count % 4;
  count -= remain;
  while (remain > 0)
  {
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    remain--;
  }


  DWORD *pdw = (DWORD*)p;
  DWORD dw;
  while (count > 0)
  {
    dw = *pdw++;
    c = Crc32Tab[((int)c ^ ((BYTE)(dw))) & 0xFF] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ ((BYTE)(dw >> 8))) & 0xFF] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ ((BYTE)(dw >> 16))) & 0xFF] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ ((BYTE)(dw >> 24))) & 0xFF] ^ (c >> 8);
    count -= 4;
  }
*/
    return c ^ 0xFFFFFFFF; /* (instead of ~c for 64-bit machines) */
}

BOOL IsRemoteSession(void)
{
    return GetSystemMetrics(SM_REMOTESESSION);
}

// ****************************************************************************

BOOL SalamanderIsNotBusy(DWORD* lastIdleTime)
{
    // SalamanderBusy and LastSalamanderIdleTime are accessed without critical sections, that's OK,
    // because they are DWORDs and therefore cannot be "in progress" when context is switched
    // (there is always either the old or new value, nothing else is possible)
    if (lastIdleTime != NULL)
        *lastIdleTime = LastSalamanderIdleTime;
    if (!SalamanderBusy)
        return TRUE;
    DWORD oldLastIdleTime = LastSalamanderIdleTime;
    if (GetTickCount() - oldLastIdleTime <= 100)                                   // if SalamanderBusy hasn't been set for too long (e.g., open modal dialog)
        Sleep(100);                                                                // wait to see if SalamanderBusy changes
    return !SalamanderBusy || (int)(LastSalamanderIdleTime - oldLastIdleTime) > 0; // not "busy" or at least oscillating
}

BOOL InitPreloadedStrings()
{
    DirColumnStrW = LoadStrW(IDS_DIRCOLUMN);
    DirColumnStrWLen = (int)DirColumnStrW.length();

    ColExtStrW = LoadStrW(IDS_COLUMN_NAME_EXT);
    ColExtStrWLen = (int)ColExtStrW.length();

    UpDirTypeName = LoadStrW(IDS_UPDIRTYPENAME);
    UpDirTypeNameLen = (int)UpDirTypeName.length();

    CommonFileTypeName = LoadStrW(IDS_COMMONFILETYPE);
    CommonFileTypeNameLen = (int)CommonFileTypeName.length();
    CommonFileTypeName2 = LoadStrW(IDS_COMMONFILETYPE2);

    ProgDlgHoursStr = LoadStrW(IDS_PROGDLGHOURS);
    ProgDlgMinutesStr = LoadStrW(IDS_PROGDLGMINUTES);
    ProgDlgSecsStr = LoadStrW(IDS_PROGDLGSECS);

    return TRUE;
}

void ReleasePreloadedStrings()
{
    DirColumnStrW.clear();
    ColExtStrW.clear();

    UpDirTypeName.clear();

    CommonFileTypeName.clear();
    CommonFileTypeName2.clear();

    ProgDlgHoursStr.clear();
    ProgDlgMinutesStr.clear();
    ProgDlgSecsStr.clear();

    DirColumnStrWLen = 0;
    ColExtStrWLen = 0;
    UpDirTypeNameLen = 0;
    CommonFileTypeNameLen = 0;
}

// ****************************************************************************

void InitLocales()
{
    int i;
    for (i = 0; i < 256; i++)
    {
        IsNotAlphaNorNum[i] = !IsCharAlphaNumeric((char)i);
        IsAlpha[i] = IsCharAlpha((char)i);
    }

    const auto loadLocaleText = [](LCTYPE type, const wchar_t* fallback)
    {
        const int required = GetLocaleInfoW(LOCALE_USER_DEFAULT, type, NULL, 0);
        if (required <= 1)
            return std::wstring(fallback);
        std::wstring value(static_cast<size_t>(required), L'\0');
        const int written = GetLocaleInfoW(LOCALE_USER_DEFAULT, type, value.data(), required);
        if (written <= 1)
            return std::wstring(fallback);
        value.resize(static_cast<size_t>(written - 1));
        return value;
    };
    DecimalSeparator = loadLocaleText(LOCALE_SDECIMAL, L".");
    ThousandsSeparator = loadLocaleText(LOCALE_STHOUSAND, L" ");
}

// ****************************************************************************

HICON GetFileOrPathIconAuxW(const wchar_t* path, BOOL large, BOOL isDir)
{
    __try
    {
        SHFILEINFO shi;
        if (!GetFileIcon(path, &shi.hIcon, large ? ICONSIZE_32 : ICONSIZE_16, TRUE, isDir))
            shi.hIcon = NULL;
        //We switched to our own implementation (lower memory requirements, working XOR icons)
        //shi.hIcon = NULL;
        //SHGetFileInfo(path, 0, &shi, sizeof(shi),
        //              SHGFI_ICON | SHGFI_SHELLICONSIZE | (large ? 0 : SHGFI_SMALLICON));
        // add handle to 'shi.hIcon' to HANDLES
        if (shi.hIcon != NULL)
            HANDLES_ADD(__htIcon, __hoLoadImage, shi.hIcon);
        return shi.hIcon;
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 13))
    {
        FGIExceptionHasOccured++;
    }
    return NULL;
}

HICON GetDriveIconW(const wchar_t* root, UINT type, BOOL accessible, BOOL large)
{
    CALL_STACK_MESSAGE5("GetDriveIconW(%ls, %u, %d, %d)", root, type, accessible, large);
    int id;
    switch (type)
    {
    case DRIVE_REMOVABLE: // icons for 3.5", 5.25"
    {
        HICON i = GetFileOrPathIconAuxW(root, large, TRUE);
        if (i != NULL)
            return i;
        id = 28; // 3 1/2" floppy drive
        break;
    }

    case DRIVE_REMOTE:
        id = (accessible ? 33 : 31);
        break;
    case DRIVE_CDROM:
        id = 30;
        break;
    case DRIVE_RAMDISK:
        id = 34;
        break;

    default:
    {
        id = 32;
        if (type == DRIVE_FIXED && root[1] == L':')
        {
            std::wstring winW;
            if (gEnvironment->GetWindowsDirectory(winW).success &&
                winW.length() > 1 && winW[1] == L':' && winW[0] == root[0])
                id = 36;
        }
        break;
    }
    }
    int iconSize = IconSizes[large ? ICONSIZE_32 : ICONSIZE_16];
    return SalLoadIcon(ImageResDLL, id, iconSize);

    // JRYFIXME - investigate whether IconLRFlags can be removed? (W7+)

    /* JRYFIXME - grep all sources for LoadImage / IMAGE_ICON
  return (HICON)HANDLES(LoadImage(ImageResDLL, MAKEINTRESOURCE(id), IMAGE_ICON,
                                  large ? ICON32_CX : ICON16_CX,
                                  large ? ICON32_CX : ICON16_CX,
                                  IconLRFlags));
  */
}

HICON SalLoadIcon(HINSTANCE hDLL, int id, int iconSize)
{
    //return (HICON)HANDLES(LoadImage(hDLL, MAKEINTRESOURCE(id), IMAGE_ICON, iconSize, iconSize, IconLRFlags));
    HICON hIcon;
    LoadIconWithScaleDown(hDLL, MAKEINTRESOURCEW(id), iconSize, iconSize, &hIcon);
    HANDLES_ADD(__htIcon, __hoLoadImage, hIcon);
    return hIcon;
}

// ****************************************************************************

// ****************************************************************************

BOOL HasTheSameRootPath(const wchar_t* path1, const wchar_t* path2)
{
    if (towlower(path1[0]) == towlower(path2[0]) && path1[1] == path2[1])
    {
        if (path1[1] == L':')
            return TRUE; // same root for normal ("c:\path") path
        else
        {
            if (path1[0] == L'\\' && path1[1] == L'\\') // both UNC
            {
                const wchar_t* s1 = path1 + 2;
                const wchar_t* s2 = path2 + 2;
                while (*s1 != 0 && *s1 != L'\\')
                {
                    if (towlower(*s1) == towlower(*s2))
                    {
                        s1++;
                        s2++;
                    }
                    else
                        break; // different machines
                }
                if (*s1 != 0 && *s1++ == *s2++) // skip '\\'
                {
                    while (*s1 != 0 && *s1 != L'\\')
                    {
                        if (towlower(*s1) == towlower(*s2))
                        {
                            s1++;
                            s2++;
                        }
                        else
                            break; // different drives
                    }
                    return (*s1 == 0 && (*s2 == 0 || *s2 == L'\\')) || *s1 == *s2 ||
                           (*s2 == 0 && (*s1 == 0 || *s1 == L'\\'));
                }
            }
        }
    }
    return FALSE;
}

// ****************************************************************************

// This is not a reuse of
// PathsAreOnTheSameVolumeW's algorithm below, which drops the same-root gate
// this function starts from and answers a related but not identical question
// (it isn't a drop-in behavioral twin). Narrowed, two DIFFERENT Unicode
// directories can collapse to the same '?'-bearing path, so the narrow form
// can answer "same volume" for paths it never actually distinguished — the
// same failure mode already fixed for PathsAreOnTheSameVolumeW's callers.
BOOL HasTheSameRootPathAndVolume(const wchar_t* p1, const wchar_t* p2)
{
    BOOL ret = FALSE;
    if (HasTheSameRootPath(p1, p2))
    {
        ret = TRUE;
        std::wstring resPath(p1);
        ResolveSubstsW(resPath);
        std::wstring root = GetRootPath(resPath.c_str());
        if (!IsUNCPathW(root.c_str()) && GetDriveTypeW(root.c_str()) == DRIVE_FIXED) // it makes sense to look for reparse points only on fixed drives
        {
            // if it's not a root path, we'll try traversing through reparse points
            wchar_t p1Volume[100] = L"1";
            wchar_t p2Volume[100] = L"2";
            BOOL cutPathIsPossible = TRUE;
            CLocalPathResolutionW res1;
            ResolveLocalPathWithReparsePointsW(p1, res1);
            std::wstring ourPath = res1.ResPath;
            cutPathIsPossible = res1.CutResPathIsPossible;
            std::wstring p1NetPath = res1.NetPath;

            if (p1NetPath.empty()) // cannot get volume from network path, won't even try
            {
                while (!GetVolumeNameForVolumeMountPointW(ourPath.c_str(), p1Volume, 100))
                {
                    if (!cutPathIsPossible || !CutDirectoryW(ourPath))
                    {
                        wcscpy(p1Volume, L"fail"); // even root didn't succeed, unexpected (unfortunately happens on substed drives under W2K - debugged at Bachaalany - on failure for both paths we return MATCH, because it's more likely)
                        break;
                    }
                    SalPathAddBackslashW(ourPath);
                }
            }

            // if we're under W2K and it's not a root path, we'll try traversing through reparse points
            cutPathIsPossible = TRUE;
            CLocalPathResolutionW res2;
            ResolveLocalPathWithReparsePointsW(p2, res2);
            ourPath = res2.ResPath;
            cutPathIsPossible = res2.CutResPathIsPossible;
            std::wstring p2NetPath = res2.NetPath;

            if (p1NetPath.empty() != p2NetPath.empty() || // if only one of the paths is network or
                !p1NetPath.empty() && !HasTheSameRootPath(p1NetPath.c_str(), p2NetPath.c_str()))
                ret = FALSE; // they don't have the same root, we report different volumes (cannot verify volumes on network paths)

            if (p2NetPath.empty() && ret) // cannot get volume from network path, won't even try + if already decided, also won't try
            {
                while (!GetVolumeNameForVolumeMountPointW(ourPath.c_str(), p2Volume, 100))
                {
                    if (!cutPathIsPossible || !CutDirectoryW(ourPath))
                    {
                        wcscpy(p2Volume, L"fail"); // even root didn't succeed, unexpected (unfortunately happens on substed drives under W2K - debugged at Bachaalany - on failure for both paths we return MATCH, because it's more likely)
                        break;
                    }
                    SalPathAddBackslashW(ourPath);
                }
                if (wcscmp(p1Volume, p2Volume) != 0)
                    ret = FALSE;
            }
        }
    }
    return ret;
}

// ****************************************************************************

// Wide-native, on top of the ported reparse walk.
//
// The volume NAMES this compares are GUIDs and always ASCII — what was lossy is
// the two PATHS on the way in. Narrowed, two different Unicode directories can
// resolve to the same '?'-bearing path, so the function answered "same volume"
// for paths it had never actually distinguished. That answer decides whether a
// move is a rename or a copy-then-delete, so getting it wrong is not cosmetic.
BOOL PathsAreOnTheSameVolumeW(const wchar_t* path1, const wchar_t* path2, BOOL* resIsOnlyEstimation)
{
    std::wstring ourPath(path1);
    ResolveSubstsW(ourPath);
    std::wstring root1 = GetRootPath(ourPath.c_str());
    ourPath.assign(path2);
    ResolveSubstsW(ourPath);
    std::wstring root2 = GetRootPath(ourPath.c_str());

    BOOL ret = TRUE;
    BOOL trySimpleTest = TRUE;
    if (resIsOnlyEstimation != NULL)
        *resIsOnlyEstimation = TRUE;

    if (!IsUNCPathW(path1) && !IsUNCPathW(path2)) // volumes on UNC paths don't make sense to resolve
    {
        wchar_t p1Volume[100] = L"1";
        wchar_t p2Volume[100] = L"2";
        UINT drvType1 = GetDriveTypeW(root1.c_str());
        UINT drvType2 = GetDriveTypeW(root2.c_str());
        if (drvType1 != DRIVE_REMOTE && drvType2 != DRIVE_REMOTE) // except for network there's a chance to get volume name
        {
            std::wstring path1NetPath, path2NetPath;
            BOOL cutPathIsPossible = TRUE;
            if (drvType1 == DRIVE_FIXED) // reparse points only make sense to look for on fixed disks
            {
                CLocalPathResolutionW res;
                ResolveLocalPathWithReparsePointsW(path1, res);
                ourPath = res.ResPath;
                cutPathIsPossible = res.CutResPathIsPossible;
                path1NetPath = res.NetPath;
            }
            else
                ourPath = root1;

            int numOfGetVolNamesFailed = 0;
            if (path1NetPath.empty()) // cannot get volume name from network path, won't even try
            {
                while (!GetVolumeNameForVolumeMountPointW(ourPath.c_str(), p1Volume, 100))
                {
                    if (!cutPathIsPossible || !CutDirectoryW(ourPath))
                    { // even root didn't return success (happens on substed drives); on failure for
                      // both paths with the same roots we return MATCH, because it's more probable
                        numOfGetVolNamesFailed++;
                        break;
                    }
                    SalPathAddBackslashW(ourPath);
                }
            }

            cutPathIsPossible = TRUE;
            if (drvType2 == DRIVE_FIXED)
            {
                CLocalPathResolutionW res;
                ResolveLocalPathWithReparsePointsW(path2, res);
                ourPath = res.ResPath;
                cutPathIsPossible = res.CutResPathIsPossible;
                path2NetPath = res.NetPath;
            }
            else
                ourPath = root2;

            if (path2NetPath.empty())
            {
                if (path1NetPath.empty())
                {
                    while (!GetVolumeNameForVolumeMountPointW(ourPath.c_str(), p2Volume, 100))
                    {
                        if (!cutPathIsPossible || !CutDirectoryW(ourPath))
                        {
                            numOfGetVolNamesFailed++;
                            break;
                        }
                        SalPathAddBackslashW(ourPath);
                    }
                    if (numOfGetVolNamesFailed != 2)
                    {
                        // the only certain case is when a volume name was obtained for BOTH paths
                        if (numOfGetVolNamesFailed == 0 && resIsOnlyEstimation != NULL)
                            *resIsOnlyEstimation = FALSE;
                        if (numOfGetVolNamesFailed == 1 || wcscmp(p1Volume, p2Volume) != 0)
                            ret = FALSE;
                        trySimpleTest = FALSE;
                    }
                }
                else // only one path is network, so they're not the same volumes
                {
                    ret = FALSE;
                    trySimpleTest = FALSE;
                }
            }
            else
            {
                if (!path1NetPath.empty()) // compare roots of network paths
                {
                    root1 = GetRootPath(path1NetPath.c_str());
                    root2 = GetRootPath(path2NetPath.c_str());
                }
                else // only one path is network, so they're not the same volumes
                {
                    ret = FALSE;
                    trySimpleTest = FALSE;
                }
            }
        }
    }

    if (trySimpleTest) // let's just try if root paths match (network paths + everything on NT)
    {
        ret = _wcsicmp(root1.c_str(), root2.c_str()) == 0;

        if (resIsOnlyEstimation != NULL)
        {
            std::wstring p1(path1), p2(path2);
            if (ResolveSubstsW(p1) && ResolveSubstsW(p2))
            {
                if (IsTheSamePath(p1.c_str(), p2.c_str()))
                    *resIsOnlyEstimation = FALSE; // same paths = definitely same volumes
            }
        }
    }
    return ret;
}

// PathsAreOnTheSameVolume (narrow) deleted: superseded entirely by
// PathsAreOnTheSameVolumeW (a genuinely separate, already-existing implementation, not a
// signature twin) - confirmed zero remaining callers of the narrow free function.

// ****************************************************************************

BOOL IsDirError(DWORD err)
{
    return err == ERROR_NETWORK_ACCESS_DENIED ||
           err == ERROR_ACCESS_DENIED ||
           err == ERROR_SECTOR_NOT_FOUND ||
           err == ERROR_SHARING_VIOLATION ||
           err == ERROR_BAD_PATHNAME ||
           err == ERROR_FILE_NOT_FOUND ||
           err == ERROR_PATH_NOT_FOUND ||
           err == ERROR_INVALID_NAME ||   // if there's a diacritic in path on English Windows, this error is reported instead of ERROR_PATH_NOT_FOUND
           err == ERROR_INVALID_FUNCTION; // reported for one guy on WinXP on network drive Y: when Salam was accessing a path that no longer existed (no shortening occurred and the guy was in deep trouble ;-) Shift+F7 on Y:\ solved it)
}

// ****************************************************************************

BOOL CutDirectory(wchar_t* path, wchar_t** cutDir)
{
    CALL_STACK_MESSAGE2("CutDirectory(%ls,)", path);
    int l = (int)wcslen(path);
    wchar_t* lastBackslash = path + l - 1;
    while (--lastBackslash >= path && *lastBackslash != L'\\')
        ;
    wchar_t* nextBackslash = lastBackslash;
    while (--nextBackslash >= path && *nextBackslash != L'\\')
        ;
    if (lastBackslash < path)
    {
        if (cutDir != NULL)
            *cutDir = path + l;
        return FALSE; // "somedir" or "c:\"
    }
    if (nextBackslash < path) // "c:\somedir" or "c:\somedir\"
    {
        if (cutDir != NULL)
        {
            if (*(path + l - 1) == L'\\')
                *(path + --l) = 0; // remove trailing '\\'
            // l and the pointer difference count CHARACTERS; memmove wants BYTES.
            memmove(lastBackslash + 2, lastBackslash + 1,
                    (size_t)(l - (lastBackslash - path)) * sizeof(wchar_t));
            *cutDir = lastBackslash + 2; // "somedir" or "seconddir"
        }
        *(lastBackslash + 1) = 0; // "c:\"
    }
    else // "c:\firstdir\seconddir" or "c:\firstdir\seconddir\"
    {    // UNC: "\\server\share\path"
        if (path[0] == L'\\' && path[1] == L'\\' && nextBackslash <= path + 2)
        { // "\\server\share" - cannot be shortened
            if (cutDir != NULL)
                *cutDir = path + l;
            return FALSE;
        }
        *lastBackslash = 0;
        if (cutDir != NULL) // cut off trailing '\'
        {
            if (*(path + l - 1) == L'\\')
                *(path + l - 1) = 0;
            *cutDir = lastBackslash + 1;
        }
    }
    return TRUE;
}

// CutDirectoryW moved to common/SalPathWide.cpp (shared with private tests).

// ****************************************************************************

// goes through all colors from configuration and if they have default value set,
// sets them to appropriate color values

COLORREF GetHilightColor(COLORREF clr1, COLORREF clr2)
{
    WORD h1, l1, s1;
    ColorRGBToHLS(clr1, &h1, &l1, &s1);
    BYTE gray1 = GetGrayscaleFromRGB(GetRValue(clr1), GetGValue(clr1), GetBValue(clr1));
    BYTE gray2 = GetGrayscaleFromRGB(GetRValue(clr2), GetGValue(clr2), GetBValue(clr2));
    COLORREF res;
    if (gray2 < 170 && gray1 <= 220)
    {
        unsigned wantedGray = (unsigned)gray1 + 20 + (220 - (unsigned)gray1) / 2;
        if (wantedGray < (unsigned)gray2 + 100)
            wantedGray = (unsigned)gray2 + 100;
        if (wantedGray > 255)
            wantedGray = 255;
        BOOL first = TRUE;
        while (first || l1 != 240)
        {
            first = FALSE;
            l1 += 5;
            if (l1 > 240)
                l1 = 240;
            res = ColorHLSToRGB(h1, l1, s1);
            if ((unsigned)GetGrayscaleFromRGB(GetRValue(res), GetGValue(res), GetBValue(res)) >= wantedGray)
                break;
        }
    }
    else
    {
        if ((gray1 >= gray2 ? gray1 - gray2 : gray2 - gray1) > 85 ||
            gray2 < 85 ||
            gray1 < 75)
        {
            if (gray1 > gray2)
            {
                res = RGB((4 * (unsigned)GetRValue(clr1) + 3 * (unsigned)GetRValue(clr2)) / 7,
                          (4 * (unsigned)GetGValue(clr1) + 3 * (unsigned)GetGValue(clr2)) / 7,
                          (4 * (unsigned)GetBValue(clr1) + 3 * (unsigned)GetBValue(clr2)) / 7);
            }
            else
            {
                res = RGB(((unsigned)GetRValue(clr1) + (unsigned)GetRValue(clr2)) / 2,
                          ((unsigned)GetGValue(clr1) + (unsigned)GetGValue(clr2)) / 2,
                          ((unsigned)GetBValue(clr1) + (unsigned)GetBValue(clr2)) / 2);
            }
        }
        else
        {
            res = RGB(0, 0, 0);
        }
    }
    return res;
}

COLORREF GetFullRowHighlight(COLORREF bkHighlightColor) // returns "heuristic" highlight for full row mode
{
    // some heuristics: light background will be "slightly" darkened and dark background "slightly" lightened
    WORD h, l, s;
    ColorRGBToHLS(bkHighlightColor, &h, &l, &s);

    if (l < 121) // [DARK]  0-120 -> lighten Luminance progressively 0..120 -> +40..+20
        l += 20 + 20 * (120 - l) / 120;
    else // [LIGHT] 121-240 -> darken Luminance by constant 20
        l -= 20;

    return ColorHLSToRGB(h, l, s);
}

void UpdateDefaultColors(SALCOLOR* colors, CHighlightMasks* highlightMasks, BOOL processColors, BOOL processMasks)
{
    BOOL darkDefaults = DarkMode_ShouldUseDark();

    if (processColors)
    {
        int bitsPerPixel = GetCurrentBPP();
        COLORREF sysWindow = darkDefaults ? RGB(30, 30, 30) : GetSysColor(COLOR_WINDOW);
        COLORREF sysWindowText = darkDefaults ? RGB(232, 232, 232) : GetSysColor(COLOR_WINDOWTEXT);
        COLORREF sysHighlight = darkDefaults ? RGB(62, 125, 231) : GetSysColor(COLOR_HIGHLIGHT);
        COLORREF sysHighlightText = darkDefaults ? RGB(255, 255, 255) : GetSysColor(COLOR_HIGHLIGHTTEXT);
        COLORREF sysActiveCaption = darkDefaults ? RGB(45, 45, 48) : GetSysColor(COLOR_ACTIVECAPTION);
        COLORREF sysCaptionText = darkDefaults ? RGB(235, 235, 235) : GetSysColor(COLOR_CAPTIONTEXT);
        COLORREF sysInactiveCaption = darkDefaults ? RGB(37, 37, 38) : GetSysColor(COLOR_INACTIVECAPTION);
        COLORREF sysInactiveCaptionText = darkDefaults ? RGB(180, 180, 180) : GetSysColor(COLOR_INACTIVECAPTIONTEXT);

        // pen colors for frame around item - we take from system window text color
        if (GetFValue(colors[FOCUS_ACTIVE_NORMAL]) & SCF_DEFAULT)
            SetRGBPart(&colors[FOCUS_ACTIVE_NORMAL], sysWindowText);
        if (GetFValue(colors[FOCUS_ACTIVE_SELECTED]) & SCF_DEFAULT)
            SetRGBPart(&colors[FOCUS_ACTIVE_SELECTED], sysWindowText);
        if (GetFValue(colors[FOCUS_BK_INACTIVE_NORMAL]) & SCF_DEFAULT)
            SetRGBPart(&colors[FOCUS_BK_INACTIVE_NORMAL], sysWindow);
        if (GetFValue(colors[FOCUS_BK_INACTIVE_SELECTED]) & SCF_DEFAULT)
            SetRGBPart(&colors[FOCUS_BK_INACTIVE_SELECTED], sysWindow);

        // panel item text colors - we take from system window text color
        if (GetFValue(colors[ITEM_FG_NORMAL]) & SCF_DEFAULT)
            SetRGBPart(&colors[ITEM_FG_NORMAL], sysWindowText);
        if (GetFValue(colors[ITEM_FG_FOCUSED]) & SCF_DEFAULT)
            SetRGBPart(&colors[ITEM_FG_FOCUSED], sysWindowText);
        if (GetFValue(colors[ITEM_FG_HIGHLIGHT]) & SCF_DEFAULT) // FULL ROW HIGHLIGHT based on _NORMAL
            SetRGBPart(&colors[ITEM_FG_HIGHLIGHT], GetCOLORREF(colors[ITEM_FG_NORMAL]));

        // panel item background colors - we take from system window background color
        if (GetFValue(colors[ITEM_BK_NORMAL]) & SCF_DEFAULT)
            SetRGBPart(&colors[ITEM_BK_NORMAL], sysWindow);
        if (GetFValue(colors[ITEM_BK_SELECTED]) & SCF_DEFAULT)
            SetRGBPart(&colors[ITEM_BK_SELECTED], sysWindow);
        if (GetFValue(colors[ITEM_BK_HIGHLIGHT]) & SCF_DEFAULT) // HIGHLIGHT copied from NORMAL (to work with custom/norton modes too)
            SetRGBPart(&colors[ITEM_BK_HIGHLIGHT], GetFullRowHighlight(GetCOLORREF(colors[ITEM_BK_NORMAL])));

        // progress bar colors
        if (GetFValue(colors[PROGRESS_FG_NORMAL]) & SCF_DEFAULT)
            SetRGBPart(&colors[PROGRESS_FG_NORMAL], sysWindowText);
        if (GetFValue(colors[PROGRESS_FG_SELECTED]) & SCF_DEFAULT)
            SetRGBPart(&colors[PROGRESS_FG_SELECTED], sysHighlightText);
        if (GetFValue(colors[PROGRESS_BK_NORMAL]) & SCF_DEFAULT)
            SetRGBPart(&colors[PROGRESS_BK_NORMAL], sysWindow);
        if (GetFValue(colors[PROGRESS_BK_SELECTED]) & SCF_DEFAULT)
            SetRGBPart(&colors[PROGRESS_BK_SELECTED], sysHighlight);

        // selected icon blend color
        if (GetFValue(colors[ICON_BLEND_SELECTED]) & SCF_DEFAULT)
        {
            // normally we copy color from focused+selected to selected
            SetRGBPart(&colors[ICON_BLEND_SELECTED], GetCOLORREF(colors[ICON_BLEND_FOCSEL]));
            // if it's red (salamander profile and we can afford it thanks to color depth)
            // we use a lighter shade for selected
            if (bitsPerPixel > 8 && GetCOLORREF(colors[ICON_BLEND_FOCSEL]) == RGB(255, 0, 0))
                SetRGBPart(&colors[ICON_BLEND_SELECTED], RGB(255, 128, 128));
        }

#define COLOR_HOTLIGHT 26 // winuser.h

        // panel titles (active/inactive)

        // active panel title: BACKGROUND
        if (GetFValue(colors[ACTIVE_CAPTION_BK]) & SCF_DEFAULT)
            SetRGBPart(&colors[ACTIVE_CAPTION_BK], sysActiveCaption);
        // active panel title: TEXT
        if (GetFValue(colors[ACTIVE_CAPTION_FG]) & SCF_DEFAULT)
            SetRGBPart(&colors[ACTIVE_CAPTION_FG], sysCaptionText);
        // inactive panel title: BACKGROUND
        if (GetFValue(colors[INACTIVE_CAPTION_BK]) & SCF_DEFAULT)
            SetRGBPart(&colors[INACTIVE_CAPTION_BK], sysInactiveCaption);
        // inactive panel title: TEXT
        if (GetFValue(colors[INACTIVE_CAPTION_FG]) & SCF_DEFAULT)
        {
            // we prefer the same text color as for active title, but sometimes this color is too
            // close to background color, then we try the inactive title text color
            COLORREF clrBk = GetCOLORREF(colors[INACTIVE_CAPTION_BK]);
            COLORREF clrFgAc = sysCaptionText;
            COLORREF clrFgIn = sysInactiveCaptionText;
            BYTE grayBk = GetGrayscaleFromRGB(GetRValue(clrBk), GetGValue(clrBk), GetBValue(clrBk));
            BYTE grayFgAc = GetGrayscaleFromRGB(GetRValue(clrFgAc), GetGValue(clrFgAc), GetBValue(clrFgAc));
            BYTE grayFgIn = GetGrayscaleFromRGB(GetRValue(clrFgIn), GetGValue(clrFgIn), GetBValue(clrFgIn));
            SetRGBPart(&colors[INACTIVE_CAPTION_FG], (abs(grayFgAc - grayBk) >= abs(grayFgIn - grayBk)) ? clrFgAc : clrFgIn);
        }

        // hot item colors
        COLORREF hotColor = darkDefaults ? RGB(120, 170, 255) : GetSysColor(COLOR_HOTLIGHT);
        if (GetFValue(colors[HOT_PANEL]) & SCF_DEFAULT)
            SetRGBPart(&colors[HOT_PANEL], hotColor);

        // highlight for active panel caption
        if (GetFValue(colors[HOT_ACTIVE]) & SCF_DEFAULT)
        {
            COLORREF clr = GetCOLORREF(colors[ACTIVE_CAPTION_FG]);
            if (bitsPerPixel > 4)
                clr = GetHilightColor(clr, GetCOLORREF(colors[ACTIVE_CAPTION_BK]));
            SetRGBPart(&colors[HOT_ACTIVE], clr);
        }
        // highlight for inactive panel caption
        if (GetFValue(colors[HOT_INACTIVE]) & SCF_DEFAULT)
        {
            COLORREF clr = GetCOLORREF(colors[INACTIVE_CAPTION_FG]);
            if (bitsPerPixel > 4)
                clr = GetHilightColor(clr, GetCOLORREF(colors[INACTIVE_CAPTION_BK]));
            SetRGBPart(&colors[HOT_INACTIVE], clr);
        }

        if (darkDefaults)
        {
            // Force a coherent dark palette for panel/client rendering in V2.
            // Keeping these synchronized avoids icon background artifacts when
            // image-list background follows ITEM_BK_NORMAL.
            //
            // NOTE: the "base" selection/focus colors that are stored flag-0 in the color
            // schemes (their RGB is the source of truth, not SCF_DEFAULT-derived) must NOT
            // be overwritten here. Writing dark values in place corrupts their light RGB and
            // leaves selection colors dark after a dark->light switch (#81 follow-up). Those
            // indices — FOCUS_FG_INACTIVE_NORMAL/SELECTED, ITEM_FG_SELECTED/FOCSEL,
            // ITEM_BK_FOCUSED/FOCSEL, ICON_BLEND_FOCUSED/FOCSEL — are resolved to their dark
            // value at render time via ResolveDarkBaseColor() (see sal_colors.cpp), mirroring
            // how the highlight-mask dark contrast is already handled in files_window_paint.cpp.
            SetRGBPart(&colors[FOCUS_ACTIVE_NORMAL], RGB(145, 145, 145));
            SetRGBPart(&colors[FOCUS_ACTIVE_SELECTED], RGB(220, 220, 220));
            SetRGBPart(&colors[FOCUS_BK_INACTIVE_NORMAL], RGB(30, 30, 30));
            SetRGBPart(&colors[FOCUS_BK_INACTIVE_SELECTED], RGB(30, 30, 30));

            SetRGBPart(&colors[ITEM_FG_NORMAL], RGB(232, 232, 232));
            SetRGBPart(&colors[ITEM_FG_FOCUSED], RGB(255, 255, 255));
            SetRGBPart(&colors[ITEM_FG_HIGHLIGHT], RGB(255, 255, 255));

            SetRGBPart(&colors[ITEM_BK_NORMAL], RGB(30, 30, 30));
            SetRGBPart(&colors[ITEM_BK_SELECTED], RGB(30, 30, 30));
            SetRGBPart(&colors[ITEM_BK_HIGHLIGHT], RGB(45, 45, 48));

            SetRGBPart(&colors[ICON_BLEND_SELECTED], RGB(120, 170, 255));

            SetRGBPart(&colors[PROGRESS_FG_NORMAL], RGB(232, 232, 232));
            SetRGBPart(&colors[PROGRESS_FG_SELECTED], RGB(255, 255, 255));
            SetRGBPart(&colors[PROGRESS_BK_NORMAL], RGB(30, 30, 30));
            SetRGBPart(&colors[PROGRESS_BK_SELECTED], RGB(62, 125, 231));

            SetRGBPart(&colors[HOT_PANEL], RGB(120, 170, 255));
            SetRGBPart(&colors[HOT_ACTIVE], RGB(170, 200, 255));
            SetRGBPart(&colors[HOT_INACTIVE], RGB(140, 170, 220));

            SetRGBPart(&colors[ACTIVE_CAPTION_FG], RGB(235, 235, 235));
            SetRGBPart(&colors[ACTIVE_CAPTION_BK], RGB(45, 45, 48));
            SetRGBPart(&colors[INACTIVE_CAPTION_FG], RGB(180, 180, 180));
            SetRGBPart(&colors[INACTIVE_CAPTION_BK], RGB(37, 37, 38));
        }
    }

    if (processMasks)
    {
        // colors dependent on file name+attributes
        int i;
        for (i = 0; i < highlightMasks->Count; i++)
        {
            CHighlightMasksItem* item = highlightMasks->At(i);
            if (GetFValue(item->NormalFg) & SCF_DEFAULT)
                SetRGBPart(&item->NormalFg, GetCOLORREF(colors[ITEM_FG_NORMAL]));
            if (GetFValue(item->NormalBk) & SCF_DEFAULT)
                SetRGBPart(&item->NormalBk, GetCOLORREF(colors[ITEM_BK_NORMAL]));
            if (GetFValue(item->FocusedFg) & SCF_DEFAULT)
                SetRGBPart(&item->FocusedFg, GetCOLORREF(colors[ITEM_FG_FOCUSED]));
            if (GetFValue(item->FocusedBk) & SCF_DEFAULT)
                SetRGBPart(&item->FocusedBk, GetCOLORREF(colors[ITEM_BK_FOCUSED]));
            if (GetFValue(item->SelectedFg) & SCF_DEFAULT)
                SetRGBPart(&item->SelectedFg, GetCOLORREF(colors[ITEM_FG_SELECTED]));
            if (GetFValue(item->SelectedBk) & SCF_DEFAULT)
                SetRGBPart(&item->SelectedBk, GetCOLORREF(colors[ITEM_BK_SELECTED]));
            if (GetFValue(item->FocSelFg) & SCF_DEFAULT)
                SetRGBPart(&item->FocSelFg, GetCOLORREF(colors[ITEM_FG_FOCSEL]));
            if (GetFValue(item->FocSelBk) & SCF_DEFAULT)
                SetRGBPart(&item->FocSelBk, GetCOLORREF(colors[ITEM_BK_FOCSEL]));
            if (GetFValue(item->HighlightFg) & SCF_DEFAULT)
                SetRGBPart(&item->HighlightFg, GetCOLORREF(item->NormalFg));
            if (GetFValue(item->HighlightBk) & SCF_DEFAULT) // FULL ROW HIGHLIGHT based on _NORMAL
                SetRGBPart(&item->HighlightBk, GetFullRowHighlight(GetCOLORREF(item->NormalBk)));
        }
    }
}

//****************************************************************************
//
// Based on display color depth, determines whether to use 256-color
// or 16-color bitmaps.
//

BOOL Use256ColorsBitmap()
{
    int bitsPerPixel = GetCurrentBPP();
    return (bitsPerPixel > 8); // more than 256 colors
}

DWORD GetImageListColorFlags()
{
    // if image list has 16-bit color depth, alpha channel of new icons misbehaves under WinXP 32-bit color display (32-bit depth works)
    // if image list has 32-bit color depth, blend misbehaves when drawing selected item under Win2K 32-bit color display (16-bit depth works)
    return ILC_COLOR32;
}

// ****************************************************************************

int LoadColorTable(int id, RGBQUAD* rgb, int rgbCount)
{
    int count = 0;
    HRSRC hRsrc = FindResource(HInstance, MAKEINTRESOURCE(id), RT_RCDATA);
    if (hRsrc)
    {
        void* data = LoadResource(HInstance, hRsrc);
        if (data)
        {
            DWORD size = SizeofResource(HInstance, hRsrc);
            if (size > 0)
            {
                int max = min(rgbCount, (WORD)size / 3);
                BYTE* ptr = (BYTE*)data;
                int i;
                for (i = 0; i < max; i++)
                {
                    rgb[i].rgbBlue = *ptr++;
                    rgb[i].rgbGreen = *ptr++;
                    rgb[i].rgbRed = *ptr++;
                    rgb[i].rgbReserved = 0;
                    count++;
                }
            }
        }
    }
    return count;
}

BOOL InitializeConstGraphics()
{
    // ensure smooth graphics output
    // 20 GDI API calls should be plenty
    // it's the default value from NT 4.0 WS
    if (GdiGetBatchLimit() < 20)
    {
        TRACE_I("Increasing GdiBatchLimit");
        GdiSetBatchLimit(20);
    }

    if (LoadColorTable(IDC_COLORTABLE, ColorTable, 256) != 256)
    {
        TRACE_E("Loading ColorTable failed");
        return FALSE;
    }

    if (SystemParametersInfo(SPI_GETDRAGFULLWINDOWS, 0, &DragFullWindows, FALSE) == 0)
        DragFullWindows = TRUE;

    // LogFont structure initialization
    NONCLIENTMETRICS ncm;
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0);
    LogFont = ncm.lfStatusFont;
    /*
  LogFont.lfHeight = -10;
  LogFont.lfWidth = 0;
  LogFont.lfEscapement = 0;
  LogFont.lfOrientation = 0;
  LogFont.lfWeight = FW_NORMAL;
  LogFont.lfItalic = 0;
  LogFont.lfUnderline = 0;
  LogFont.lfStrikeOut = 0;
  LogFont.lfCharSet = UserCharset;
  LogFont.lfOutPrecision = OUT_DEFAULT_PRECIS;
  LogFont.lfClipPrecision = CLIP_DEFAULT_PRECIS;
  LogFont.lfQuality = DEFAULT_QUALITY;
  LogFont.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
  strcpy(LogFont.lfFaceName, "MS Shell Dlg 2");
  */

    // these brushes are allocated by the system and automatically change when colors change
    HDialogBrush = GetSysColorBrush(COLOR_BTNFACE);
    HButtonTextBrush = GetSysColorBrush(COLOR_BTNTEXT);
    HMenuSelectedBkBrush = GetSysColorBrush(COLOR_HIGHLIGHT);
    HMenuSelectedTextBrush = GetSysColorBrush(COLOR_HIGHLIGHTTEXT);
    HMenuHilightBrush = GetSysColorBrush(COLOR_3DHILIGHT);
    HMenuGrayTextBrush = GetSysColorBrush(COLOR_3DSHADOW);
    if (HDialogBrush == NULL || HButtonTextBrush == NULL ||
        HMenuSelectedTextBrush == NULL || HMenuHilightBrush == NULL ||
        HMenuGrayTextBrush == NULL)
    {
        TRACE_E("Unable to create brush.");
        return FALSE;
    }
    ItemBitmap.CreateBmp(NULL, 1, 1); // ensure bitmap exists

    // we load the bitmap only once (we don't refresh it when resolution changes)
    // and if user would switch colors from 256 up, with LoadBitmap (i.e. bitmap
    // compatible with display DC) the bitmap would remain in degraded colors;
    // that's why we load it as DIB
    // HWorkerBitmap = HANDLES(LoadBitmap(HInstance, MAKEINTRESOURCE(IDB_WORKER)));
    //HWorkerBitmap = (HBITMAP)HANDLES(LoadImage(HInstance, MAKEINTRESOURCE(IDB_WORKER), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    //if (HWorkerBitmap == NULL)
    //  return FALSE;

    // when font changes, CreatePanelFont and CreateEnvFont are called explicitly, we do first initialization here
    CreatePanelFont();
    CreateEnvFonts();

    if (Font == NULL || FontUL == NULL || EnvFont == NULL || EnvFontUL == NULL || TooltipFont == NULL)
    {
        TRACE_E("Unable to create fonts.");
        return FALSE;
    }

    return TRUE;
}

void ReleaseConstGraphics()
{
    ItemBitmap.Destroy();
    //if (HWorkerBitmap != NULL)
    //{
    //  HANDLES(DeleteObject(HWorkerBitmap));
    //  HWorkerBitmap = NULL;
    //}

    if (Font != NULL)
    {
        HANDLES(DeleteObject(Font));
        Font = NULL;
    }

    if (FontUL != NULL)
    {
        HANDLES(DeleteObject(FontUL));
        FontUL = NULL;
    }

    if (TooltipFont != NULL)
    {
        HANDLES(DeleteObject(TooltipFont));
        TooltipFont = NULL;
    }

    if (EnvFont != NULL)
    {
        HANDLES(DeleteObject(EnvFont));
        EnvFont = NULL;
    }

    if (EnvFontBold != NULL)
    {
        HANDLES(DeleteObject(EnvFontBold));
        EnvFontBold = NULL;
    }

    if (EnvFontUL != NULL)
    {
        HANDLES(DeleteObject(EnvFontUL));
        EnvFontUL = NULL;
    }
}

BOOL AuxAllocateImageLists()
{
    int i;
    for (i = 0; i < ICONSIZE_COUNT; i++)
    {
        SimpleIconLists[i] = new CIconList();
        if (SimpleIconLists[i] == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
    }

    ThrobberFrames = new CIconList();
    if (ThrobberFrames == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }

    LockFrames = new CIconList();
    if (LockFrames == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }

    return TRUE;
}

// users can change shortcut icon via TweakUI (default, custom, none)
// we'll try to honor it
BOOL GetShortcutOverlay()
{
    int i;
    for (i = 0; i < ICONSIZE_COUNT; i++)
    {
        if (HShortcutOverlays[i] != NULL)
        {
            HANDLES(DestroyIcon(HShortcutOverlays[i]));
            HShortcutOverlays[i] = NULL;
        }
    }

    /*  
  //#include <CommonControls.h>

  // reading overlay icons from system image-list, unnecessarily slow, we load them directly from imageres.dll
  // I'm leaving this code here just in case we need to find out where those icons are again
  typedef DECLSPEC_IMPORT HRESULT (WINAPI *F__SHGetImageList)(int iImageList, REFIID riid, void **ppvObj);

  F__SHGetImageList MySHGetImageList = (F__SHGetImageList)GetProcAddress(Shell32DLL, "SHGetImageList"); // Min: XP
  if (MySHGetImageList != NULL)
  {
    int shareIndex = SHGetIconOverlayIndex(NULL, IDO_SHGIOI_SHARE);
    int linkIndex = SHGetIconOverlayIndex(NULL, IDO_SHGIOI_LINK);
    int offlineIndex = SHGetIconOverlayIndex(NULL, IDO_SHGIOI_SLOWFILE);
    shareIndex = SHGetIconOverlayIndex(NULL, IDO_SHGIOI_SHARE);

    IImageList *imageList;
    if (MySHGetImageList(1 /* SHIL_SMALL * /, IID_IImageList, (void **)&imageList) == S_OK &&
        imageList != NULL)
    {
      int i;
      imageList->GetOverlayImage(linkIndex, &i);
      HICON icon;
      if (imageList->GetIcon(i, 0, &icon) != S_OK)
        icon = NULL;
      HShortcutOverlays[ICONSIZE_16] = icon;
      imageList->Release();
    }
    if (MySHGetImageList(0 /* SHIL_LARGE * /, IID_IImageList, (void **)&imageList) == S_OK &&
        imageList != NULL)
    {
      int i;
      imageList->GetOverlayImage(linkIndex, &i);
      HICON icon;
      if (imageList->GetIcon(i, 0, &icon) != S_OK)
        icon = NULL;
      HShortcutOverlays[ICONSIZE_32] = icon;
      imageList->Release();
    }
    if (MySHGetImageList(2 /* SHIL_EXTRALARGE * /, IID_IImageList, (void **)&imageList) == S_OK &&
        imageList != NULL)
    {
      int i;
      imageList->GetOverlayImage(linkIndex, &i);
      HICON icon;
      if (imageList->GetIcon(i, 0, &icon) != S_OK)
        icon = NULL;
      HShortcutOverlays[ICONSIZE_48] = icon;
      imageList->Release();
    }
  }
*/

    HKEY hKey;
    IRegistry* registry = GetMainSalamanderRegistry();
    if (registry->OpenKeyRead(HKEY_LOCAL_MACHINE,
                              SAL_REG_KEY_EXPLORER_SHELL_ICONS_W,
                              hKey).success)
    {
        std::wstring buff;
        registry->GetString(hKey, L"29", buff);
        if (!buff.empty())
        {
            const size_t comma = buff.find_last_of(L','); // icon number is after the last comma
            if (comma != std::wstring::npos)
            {
                int index = _wtoi(buff.c_str() + comma + 1);
                buff.resize(comma);

                HICON hIcons[2] = {0, 0};

                ExtractIconsW(buff.c_str(), index,
                              MAKELONG(32, 16),
                              MAKELONG(32, 16),
                              hIcons, NULL, 2, IconLRFlags);

                HShortcutOverlays[ICONSIZE_32] = hIcons[0];
                HShortcutOverlays[ICONSIZE_16] = hIcons[1];

                ExtractIconsW(buff.c_str(), index,
                              48,
                              48,
                              hIcons, NULL, 1, IconLRFlags);
                HShortcutOverlays[ICONSIZE_48] = hIcons[0];

                for (i = 0; i < ICONSIZE_COUNT; i++)
                    if (HShortcutOverlays[i] != NULL)
                        HANDLES_ADD(__htIcon, __hoLoadImage, HShortcutOverlays[i]);
            }
        }
        registry->CloseKey(hKey);
    }

    for (i = 0; i < ICONSIZE_COUNT; i++)
    {
        if (HShortcutOverlays[i] == NULL)
        {
            // Try imageres.dll first (icon 163 = shortcut arrow)
            if (ImageResDLL != NULL)
            {
                HShortcutOverlays[i] = (HICON)HANDLES(LoadImage(ImageResDLL, MAKEINTRESOURCE(163),
                                                                IMAGE_ICON, IconSizes[i], IconSizes[i], IconLRFlags));
            }
            // Fallback to shell32.dll (icon 29 = shortcut arrow) for Wine compatibility
            if (HShortcutOverlays[i] == NULL && Shell32DLL != NULL)
            {
                HShortcutOverlays[i] = (HICON)HANDLES(LoadImage(Shell32DLL, MAKEINTRESOURCE(29),
                                                                IMAGE_ICON, IconSizes[i], IconSizes[i], IconLRFlags));
            }
        }
    }
    return (HShortcutOverlays[ICONSIZE_16] != NULL &&
            HShortcutOverlays[ICONSIZE_32] != NULL &&
            HShortcutOverlays[ICONSIZE_48] != NULL);
}

int GetCurrentBPP(HDC hDC)
{
    HDC hdc;
    if (hDC == NULL)
        hdc = GetDC(NULL);
    else
        hdc = hDC;
    int bpp = GetDeviceCaps(hdc, PLANES) * GetDeviceCaps(hdc, BITSPIXEL);
    if (hDC == NULL)
        ReleaseDC(NULL, hdc);

    return bpp;
}

int GetSystemDPI()
{
    if (SystemDPI == 0)
    {
        TRACE_E("GetSystemDPI() SystemDPI == 0!");
        return 96;
    }
    else
    {
        return SystemDPI;
    }
}

int GetScaleForSystemDPI()
{
    int dpi = GetSystemDPI();
    int scale;
    if (dpi <= 96)
        scale = 100;
    else if (dpi <= 120)
        scale = 125;
    else if (dpi <= 144)
        scale = 150;
    else if (dpi <= 192)
        scale = 200;
    else if (dpi <= 240)
        scale = 250;
    else if (dpi <= 288)
        scale = 300;
    else if (dpi <= 384)
        scale = 400;
    else
        scale = 500;

    return scale;
}

int GetIconSizeForSystemDPI(CIconSizeEnum iconSize)
{
    if (SystemDPI == 0)
    {
        TRACE_E("GetIconSizeForSystemDPI() SystemDPI == 0!");
        return 16;
    }

    if (iconSize < ICONSIZE_16 || iconSize >= ICONSIZE_COUNT)
    {
        TRACE_E("GetIconSizeForSystemDPI() unknown iconSize!");
        return 16;
    }

    // DPI Name      DPI   Scale factor
    // --------------------------------
    // Smaller        96   1.00 (100%)
    // Medium        120   1.25 (125%)
    // Larger        144   1.50 (150%)
    // Extra Large   192   2.00 (200%)
    // Custom        240   2.50 (250%)
    // Custom        288   3.00 (300%)
    // Custom        384   4.00 (400%)
    // Custom        480   5.00 (500%)

    int scale = GetScaleForSystemDPI();

    int baseIconSize[ICONSIZE_COUNT] = {16, 32, 48}; // must match CIconSizeEnum

    return (baseIconSize[iconSize] * scale) / 100;
}

void GetSystemDPI(HDC hDC)
{
    HDC hTmpDC;
    if (hDC == NULL)
        hTmpDC = GetDC(NULL);
    else
        hTmpDC = hDC;
    SystemDPI = GetDeviceCaps(hTmpDC, LOGPIXELSX);
#ifdef _DEBUG
    if (SystemDPI != GetDeviceCaps(hTmpDC, LOGPIXELSY))
        TRACE_E("Unexpected situation: LOGPIXELSX != LOGPIXELSY.");
#endif
    if (hDC == NULL)
        ReleaseDC(NULL, hTmpDC);
}

// Helper to isolate SEH from functions that use C++ objects such as std::wstring.
// SEH (__try/__except) cannot coexist with objects that require unwinding.
static HICON GetDirectoryIconSEH(const wchar_t* path, CIconSizeEnum sizeIndex)
{
    HICON hIcon = NULL;
    __try
    {
        if (!GetFileIcon(path, &hIcon, sizeIndex, FALSE, FALSE))
            hIcon = NULL;
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 15))
    {
        FGIExceptionHasOccured++;
        hIcon = NULL;
    }
    return hIcon;
}

// #96: the F-key indicators used to be a fixed 17x13 bitmap (bottomtb.bmp) with no DPI
// scaling, so at high DPI the labels grew with the font and the keys stayed tiny. Generate
// them from the environment font instead. Enabled caps are "inverted" (light cap, dark text
// in dark mode; dark cap, light text in light mode) exactly as the reference build draws
// them; the grayed list is dimmer, since CToolBar::DrawItem uses HImageList for grayed items
// and HHotImageList for enabled ones.
// #96: the cap label is drawn in a SMALLER font than the bar label, as the reference build
// does. That keeps the cap compact, so the row height is driven by the label text and the cap
// sits inside it with margin instead of filling the whole row.
static HFONT CreateBottomTBCapFont()
{
    LOGFONT lf;
    if (EnvFont == NULL || GetObject(EnvFont, sizeof(lf), &lf) == 0)
        return NULL;
    lf.lfHeight = lf.lfHeight * 4 / 5; // lfHeight is negative, so this is 80% of the size
    lf.lfWeight = FW_NORMAL;
    return CreateFontIndirect(&lf);
}

static void GetBottomTBKeyCapSize(int* cx, int* cy)
{
    HDC hDC = HANDLES(GetDC(NULL));
    HFONT hCapFont = CreateBottomTBCapFont();
    SIZE size = CalcBottomBarKeyCapSize(hDC, hCapFont != NULL ? hCapFont : EnvFont);
    if (hCapFont != NULL)
        DeleteObject(hCapFont);
    HANDLES(ReleaseDC(NULL, hDC));
    *cx = (int)size.cx;
    *cy = (int)size.cy;
}

static HBITMAP CreateBottomTBKeyCapStrip(int capW, int capH, COLORREF fill, COLORREF textColor,
                                         COLORREF outline)
{
    HDC hScreenDC = HANDLES(GetDC(NULL));
    HDC hDC = HANDLES(CreateCompatibleDC(hScreenDC));
    HBITMAP hBmp = hDC != NULL ? HANDLES(CreateCompatibleBitmap(hScreenDC, capW * 12, capH)) : NULL;
    HANDLES(ReleaseDC(NULL, hScreenDC));
    if (hDC == NULL || hBmp == NULL)
    {
        if (hBmp != NULL)
            HANDLES(DeleteObject(hBmp));
        if (hDC != NULL)
            HANDLES(DeleteDC(hDC));
        return NULL;
    }

    HBITMAP hOldBmp = (HBITMAP)SelectObject(hDC, hBmp);
    HFONT hCapFont = CreateBottomTBCapFont();
    HFONT hOldFont = (HFONT)SelectObject(hDC, hCapFont != NULL ? hCapFont
                                                               : (EnvFont != NULL ? EnvFont : (HFONT)GetStockObject(DEFAULT_GUI_FONT)));

    // magenta is the transparency key used by ImageList_AddMasked below
    RECT all = {0, 0, capW * 12, capH};
    HBRUSH hMagenta = CreateSolidBrush(RGB(255, 0, 255));
    if (hMagenta != NULL)
    {
        FillRect(hDC, &all, hMagenta);
        DeleteObject(hMagenta);
    }

    for (int keyIndex = 0; keyIndex < 12; keyIndex++)
    {
        wchar_t keyText[8];
        swprintf_s(keyText, L"F%d", keyIndex + 1);
        RECT cap = {keyIndex * capW, 0, (keyIndex + 1) * capW - 1, capH}; // 1px gap between caps
        DrawBottomBarKeyCap(hDC, &cap, keyText, fill, textColor, outline);
    }

    SelectObject(hDC, hOldFont);
    if (hCapFont != NULL)
        DeleteObject(hCapFont);
    SelectObject(hDC, hOldBmp);
    HANDLES(DeleteDC(hDC));
    return hBmp;
}

BOOL InitializeGraphics(BOOL colorsOnly)
{
    // 48x48 only from XP
    // actually large icons are supported for a long time, can be enabled via
    // Desktop/Properties/???/Large Icons; note that system image list for 32x32 icons
    // won't exist then; also we should get real icon sizes from system, for now we
    // don't bother and enable 48x48 only from XP, where they're commonly available

    //
    // Get required icon color depth from Registry
    //
    int iconColorsCount = 0;
    HDC hDesktopDC = GetDC(NULL);
    int bpp = GetCurrentBPP(hDesktopDC);
    GetSystemDPI(hDesktopDC);
    ReleaseDC(NULL, hDesktopDC);

    IconSizes[ICONSIZE_16] = GetIconSizeForSystemDPI(ICONSIZE_16);
    IconSizes[ICONSIZE_32] = GetIconSizeForSystemDPI(ICONSIZE_32);
    IconSizes[ICONSIZE_48] = GetIconSizeForSystemDPI(ICONSIZE_48);

    HKEY hKey;
    IRegistry* registry = GetMainSalamanderRegistry();
    if (registry->OpenKeyRead(HKEY_CURRENT_USER, SAL_REG_KEY_WINDOW_METRICS_W, hKey).success)
    {
        // other interesting values: "Shell Icon Size", "Shell Small Icon Size"
        std::wstring iconBpp;
        if (registry->GetString(hKey, SAL_REG_VALUE_SHELL_ICON_BPP_W, iconBpp).success)
        {
            iconColorsCount = _wtoi(iconBpp.c_str());
        }
        else
        {
            if (WindowsVistaAndLater)
            {
                // in Vista this key simply doesn't exist and I don't know yet what replaced it,
                // so we'll pretend icons run in full colors (otherwise we were displaying 16-color ugliness)
                iconColorsCount = 32;
            }
        }

        if (iconColorsCount > bpp)
            iconColorsCount = bpp;
        if (bpp <= 8)
            iconColorsCount = 0;

        registry->CloseKey(hKey);
    }

    TRACE_I("InitializeGraphics() bpp=" << bpp << " iconColorsCount=" << iconColorsCount);
    if (bpp >= 4 && iconColorsCount <= 4)
        IconLRFlags = LR_VGACOLOR;
    else
        IconLRFlags = 0;

    HDC dc = HANDLES(GetDC(NULL));
    CHighlightMasks* masks = MainWindow != NULL ? MainWindow->HighlightMasks : NULL;
    UpdateDefaultColors(CurrentColors, masks, TRUE, masks != NULL);
    if (!colorsOnly)
    {
        // Load shell32.dll first - always available (including under Wine)
        Shell32DLL = HANDLES(LoadLibraryExA("shell32.dll", NULL, LOAD_LIBRARY_AS_DATAFILE));
        if (Shell32DLL == NULL)
        {
            TRACE_E("Unable to load library shell32.dll.");
            return FALSE;
        }

        // Try to load imageres.dll - may fail under Wine (not included)
        ImageResDLL = HANDLES(LoadLibraryExA("imageres.dll", NULL, LOAD_LIBRARY_AS_DATAFILE));
        BOOL hasImageResDLL = (ImageResDLL != NULL);
        if (!hasImageResDLL)
        {
            TRACE_I("imageres.dll not available - using shell32.dll fallback (Wine compatibility)");
        }

        // Use imageres.dll if available, otherwise fall back to shell32.dll
        HINSTANCE iconDLL = hasImageResDLL ? ImageResDLL : Shell32DLL;
        int iconIndex;
        int i;

        // Shared folder overlays: imageres.dll icon 164, or shell32.dll icon 28 as fallback
        iconIndex = hasImageResDLL ? 164 : 28;
        for (i = 0; i < ICONSIZE_COUNT; i++)
        {
            HSharedOverlays[i] = (HICON)HANDLES(LoadImage(iconDLL, MAKEINTRESOURCE(iconIndex),
                                                          IMAGE_ICON, IconSizes[i], IconSizes[i], IconLRFlags));
        }
        GetShortcutOverlay(); // HShortcutOverlayXX

        // Slow file overlays: imageres.dll icon 97, or shell32.dll icon 14 as fallback
        iconIndex = hasImageResDLL ? 97 : 14;
        for (i = 0; i < ICONSIZE_COUNT; i++)
        {
            HSlowFileOverlays[i] = (HICON)HANDLES(LoadImage(iconDLL, MAKEINTRESOURCE(iconIndex),
                                                            IMAGE_ICON, IconSizes[i], IconSizes[i], IconLRFlags));
        }

        HGroupIcon = SalLoadImage(4, 20, IconSizes[ICONSIZE_16], IconSizes[ICONSIZE_16], IconLRFlags);
        HFavoritIcon = (HICON)HANDLES(LoadImage(Shell32DLL, MAKEINTRESOURCE(319), IMAGE_ICON, IconSizes[ICONSIZE_16], IconSizes[ICONSIZE_16], IconLRFlags));
        if (HFavoritIcon == NULL)
        {
            HFavoritIcon = (HICON)HANDLES(LoadImage(Shell32DLL, MAKEINTRESOURCE(43), IMAGE_ICON, IconSizes[ICONSIZE_16], IconSizes[ICONSIZE_16], IconLRFlags));
        }

        // Check critical icons - shortcut overlays and group icon are required
        BOOL iconCheckFailed = FALSE;
        if (HShortcutOverlays[ICONSIZE_16] == NULL ||
            HShortcutOverlays[ICONSIZE_32] == NULL ||
            HShortcutOverlays[ICONSIZE_48] == NULL ||
            HGroupIcon == NULL)
        {
            iconCheckFailed = TRUE;
        }

        // Only require shared/slow overlays if imageres.dll was available
        if (hasImageResDLL)
        {
            if (HSharedOverlays[ICONSIZE_16] == NULL ||
                HSharedOverlays[ICONSIZE_32] == NULL ||
                HSharedOverlays[ICONSIZE_48] == NULL ||
                HSlowFileOverlays[ICONSIZE_16] == NULL ||
                HSlowFileOverlays[ICONSIZE_32] == NULL ||
                HSlowFileOverlays[ICONSIZE_48] == NULL ||
                HFavoritIcon == NULL)
            {
                iconCheckFailed = TRUE;
            }
        }

        if (iconCheckFailed)
        {
            TRACE_E("Unable to read icon overlays for shared directories, shortcuts or slow files, or icon for groups or favorites.");
            return FALSE;
        }

        // compiler reported error: error C2712: Cannot use __try in functions that require object unwinding
        // working around it by moving allocation to a function
        //    SymbolsIconList = new CIconList();
        //    LargeSymbolsIconList = new CIconList();
        if (!AuxAllocateImageLists())
            return FALSE;

        for (i = 0; i < ICONSIZE_COUNT; i++)
        {
            if (!SimpleIconLists[i]->Create(IconSizes[i], IconSizes[i], symbolsCount))
            {
                TRACE_E("Unable to create image lists.");
                return FALSE;
            }
            SimpleIconLists[i]->SetBkColor(GetCOLORREF(CurrentColors[ITEM_BK_NORMAL]));
        }

        if (!ThrobberFrames->CreateFromPNG(HInstance, MAKEINTRESOURCEW(IDB_THROBBER), THROBBER_WIDTH))
        {
            TRACE_E("Unable to create throbber.");
            return FALSE;
        }

        if (!LockFrames->CreateFromPNG(HInstance, MAKEINTRESOURCEW(IDB_LOCK), LOCK_WIDTH))
        {
            TRACE_E("Unable to create lock.");
            return FALSE;
        }

        HFindSymbolsImageList = ImageList_Create(IconSizes[ICONSIZE_16], IconSizes[ICONSIZE_16], ILC_MASK | GetImageListColorFlags(), 2, 0);
        if (HFindSymbolsImageList == NULL)
        {
            TRACE_E("Unable to create image list.");
            return FALSE;
        }
        ImageList_SetImageCount(HFindSymbolsImageList, 2); // initialization
                                                           //    ImageList_SetBkColor(HFindSymbolsImageList, GetSysColor(COLOR_WINDOW)); // for transparent icons to work under XP
        COLORREF toolBarBkColor = DarkMode_ShouldUseDark() ? RGB(45, 45, 48) : GetSysColor(COLOR_BTNFACE);

        int iconSize = IconSizes[ICONSIZE_16];
        HBITMAP hTmpMaskBitmap;
        HBITMAP hTmpGrayBitmap;
        HBITMAP hTmpColorBitmap;
        if (!CreateToolbarBitmaps(HInstance,
                                  IDB_MENU,
                                  RGB(255, 0, 255), toolBarBkColor,
                                  hTmpMaskBitmap, hTmpGrayBitmap, hTmpColorBitmap,
                                  FALSE, NULL, 0))
            return FALSE;
        HMenuMarkImageList = ImageList_Create(iconSize, iconSize, ILC_MASK | ILC_COLORDDB, 2, 1);
        ImageList_Add(HMenuMarkImageList, hTmpColorBitmap, hTmpMaskBitmap);
        HANDLES(DeleteObject(hTmpMaskBitmap));
        HANDLES(DeleteObject(hTmpGrayBitmap));
        HANDLES(DeleteObject(hTmpColorBitmap));

        CSVGIcon* svgIcons;
        int svgIconsCount;
        GetSVGIconsMainToolbar(&svgIcons, &svgIconsCount);
        if (!CreateToolbarBitmaps(HInstance,
                                  Use256ColorsBitmap() ? IDB_TOOLBAR_256 : IDB_TOOLBAR_16,
                                  RGB(255, 0, 255), toolBarBkColor,
                                  hTmpMaskBitmap, hTmpGrayBitmap, hTmpColorBitmap,
                                  TRUE, svgIcons, svgIconsCount))
            return FALSE;
        HHotToolBarImageList = ImageList_Create(iconSize, iconSize, ILC_MASK | ILC_COLORDDB, IDX_TB_COUNT, 1);
        HGrayToolBarImageList = ImageList_Create(iconSize, iconSize, ILC_MASK | ILC_COLORDDB, IDX_TB_COUNT, 1);
        ImageList_Add(HHotToolBarImageList, hTmpColorBitmap, hTmpMaskBitmap);
        ImageList_Add(HGrayToolBarImageList, hTmpGrayBitmap, hTmpMaskBitmap);
        HANDLES(DeleteObject(hTmpMaskBitmap));
        HANDLES(DeleteObject(hTmpGrayBitmap));
        HANDLES(DeleteObject(hTmpColorBitmap));

        if (HHotToolBarImageList == NULL || HGrayToolBarImageList == NULL)
        {
            TRACE_E("Unable to create image list.");
            return FALSE;
        }

        int capCX, capCY; // #96: sized from the environment font, not a fixed 17x13 bitmap
        GetBottomTBKeyCapSize(&capCX, &capCY);
        HBottomTBImageList = ImageList_Create(capCX, capCY, ILC_MASK | ILC_COLORDDB, 12, 0);
        HHotBottomTBImageList = ImageList_Create(capCX, capCY, ILC_MASK | ILC_COLORDDB, 12, 0);
        if (HBottomTBImageList == NULL || HHotBottomTBImageList == NULL)
        {
            TRACE_E("Unable to create image list.");
            return FALSE;
        }

        // extract icons from shell32:
        int indexes[] = {symbolsExecutable, symbolsDirectory, symbolsNonAssociated, symbolsAssociated, -1};
        int resID[] = {3, 4, 1, 2, -1};
        int vistaResID[] = {15, 4, 2, 90, -1};
        HICON hIcon;
        for (i = 0; indexes[i] != -1; i++)
        {
            int sizeIndex;
            for (sizeIndex = 0; sizeIndex < ICONSIZE_COUNT; sizeIndex++)
            {
                hIcon = SalLoadImage(vistaResID[i], resID[i], IconSizes[sizeIndex], IconSizes[sizeIndex], IconLRFlags);
                if (hIcon != NULL)
                {
                    SimpleIconLists[sizeIndex]->ReplaceIcon(indexes[i], hIcon);
                    if (sizeIndex == ICONSIZE_16)
                    {
                        if (indexes[i] == symbolsDirectory)
                            ImageList_ReplaceIcon(HFindSymbolsImageList, 0, hIcon);
                        if (indexes[i] == symbolsNonAssociated)
                            ImageList_ReplaceIcon(HFindSymbolsImageList, 1, hIcon);
                    }
                    HANDLES(DestroyIcon(hIcon));
                }
                else
                    TRACE_E("Cannot retrieve icon from IMAGERES.DLL or SHELL32.DLL resID=" << resID[i]);
            }
        }
        std::wstring systemDirW;
        const BOOL haveSystemDir = gEnvironment->GetSystemDirectory(systemDirW).success;
        // 16x16, 32x32, 48x48
        int sizeIndex;
        for (sizeIndex = ICONSIZE_16; sizeIndex < ICONSIZE_COUNT; sizeIndex++)
        {
            // directory icon
            hIcon = haveSystemDir ? GetDirectoryIconSEH(systemDirW.c_str(), (CIconSizeEnum)sizeIndex) : NULL;
            if (hIcon != NULL) // if we can't get the icon, there's still the 4th one from shell32.dll
            {
                SimpleIconLists[sizeIndex]->ReplaceIcon(symbolsDirectory, hIcon);
                NOHANDLES(DestroyIcon(hIcon));
            }

            // ".." icon
            hIcon = (HICON)HANDLES(LoadImage(HInstance, MAKEINTRESOURCE(IDI_UPPERDIR),
                                             IMAGE_ICON, IconSizes[sizeIndex], IconSizes[sizeIndex],
                                             IconLRFlags));
            SimpleIconLists[sizeIndex]->ReplaceIcon(symbolsUpDir, hIcon);
            HANDLES(DestroyIcon(hIcon));

            // archive icon
            hIcon = LoadArchiveIcon(IconSizes[sizeIndex], IconSizes[sizeIndex], IconLRFlags);
            SimpleIconLists[sizeIndex]->ReplaceIcon(symbolsArchive, hIcon);
            HANDLES(DestroyIcon(hIcon));
        }

        WORD bits[8] = {0x0055, 0x00aa, 0x0055, 0x00aa,
                        0x0055, 0x00aa, 0x0055, 0x00aa};
        HBITMAP hBrushBitmap = HANDLES(CreateBitmap(8, 8, 1, 1, &bits));
        HDitherBrush = HANDLES(CreatePatternBrush(hBrushBitmap));
        HANDLES(DeleteObject(hBrushBitmap));
        if (HDitherBrush == NULL)
            return FALSE;

        HUpDownBitmap = HANDLES(LoadBitmap(HInstance, MAKEINTRESOURCE(IDB_UPDOWN)));
        HZoomBitmap = HANDLES(LoadBitmap(HInstance, MAKEINTRESOURCE(IDB_ZOOM)));
        HFilter = HANDLES(LoadBitmap(HInstance, MAKEINTRESOURCE(IDB_FILTER)));

        if (HUpDownBitmap == NULL ||
            HZoomBitmap == NULL || HFilter == NULL)
        {
            TRACE_E("HUpDownBitmap == NULL || HZoomBitmap == NULL || HFilter == NULL");
            return FALSE;
        }

        SVGArrowRight.Load(IDV_ARROW_RIGHT, -1, -1, SVGSTATE_ENABLED | SVGSTATE_DISABLED);
        SVGArrowRightSmall.Load(IDV_ARROW_RIGHT, -1, (int)((double)iconSize / 2.5), SVGSTATE_ENABLED | SVGSTATE_DISABLED);
        SVGArrowMore.Load(IDV_ARROW_MORE, -1, -1, SVGSTATE_ENABLED | SVGSTATE_DISABLED);
        SVGArrowLess.Load(IDV_ARROW_LESS, -1, -1, SVGSTATE_ENABLED | SVGSTATE_DISABLED);
        SVGArrowDropDown.Load(IDV_ARROW_DOWN, -1, -1, SVGSTATE_ENABLED | SVGSTATE_DISABLED);
    }

    COLORREF toolBarBkColor = DarkMode_ShouldUseDark() ? RGB(45, 45, 48) : GetSysColor(COLOR_BTNFACE);
    ImageList_SetBkColor(HHotToolBarImageList, toolBarBkColor);
    ImageList_SetBkColor(HGrayToolBarImageList, toolBarBkColor);

    if (SystemParametersInfo(SPI_GETMOUSEHOVERTIME, 0, &MouseHoverTime, FALSE) == 0)
    {
        if (SystemParametersInfo(SPI_GETMENUSHOWDELAY, 0, &MouseHoverTime, FALSE) == 0)
            MouseHoverTime = 400;
    }
    //  TRACE_I("MouseHoverTime="<<MouseHoverTime);

    COLORREF normalBkgnd = GetNearestColor(dc, GetCOLORREF(CurrentColors[ITEM_BK_NORMAL]));
    COLORREF selectedBkgnd = GetNearestColor(dc, GetCOLORREF(CurrentColors[ITEM_BK_SELECTED]));
    COLORREF focusedBkgnd = GetNearestColor(dc, GetCOLORREF(CurrentColors[ITEM_BK_FOCUSED]));
    COLORREF focselBkgnd = GetNearestColor(dc, GetCOLORREF(CurrentColors[ITEM_BK_FOCSEL]));
    COLORREF activeCaption = GetNearestColor(dc, GetCOLORREF(CurrentColors[ACTIVE_CAPTION_BK]));
    COLORREF inactiveCaption = GetNearestColor(dc, GetCOLORREF(CurrentColors[INACTIVE_CAPTION_BK]));
    HANDLES(ReleaseDC(NULL, dc));

    HNormalBkBrush = HANDLES(CreateSolidBrush(normalBkgnd));
    HFocusedBkBrush = HANDLES(CreateSolidBrush(focusedBkgnd));
    HSelectedBkBrush = HANDLES(CreateSolidBrush(selectedBkgnd));
    HFocSelBkBrush = HANDLES(CreateSolidBrush(focselBkgnd));
    HActiveCaptionBrush = HANDLES(CreateSolidBrush(activeCaption));
    HInactiveCaptionBrush = HANDLES(CreateSolidBrush(inactiveCaption));

    if (HNormalBkBrush == NULL || HFocusedBkBrush == NULL ||
        HSelectedBkBrush == NULL || HFocSelBkBrush == NULL ||
        HActiveCaptionBrush == NULL || HInactiveCaptionBrush == NULL ||
        HMenuSelectedBkBrush == NULL)
    {
        TRACE_E("Unable to create brush.");
        return FALSE;
    }

    HActiveNormalPen = HANDLES(CreatePen(PS_SOLID, 0, GetCOLORREF(CurrentColors[FOCUS_ACTIVE_NORMAL])));
    HActiveSelectedPen = HANDLES(CreatePen(PS_SOLID, 0, GetCOLORREF(CurrentColors[FOCUS_ACTIVE_SELECTED])));
    HInactiveNormalPen = HANDLES(CreatePen(PS_DOT, 0, GetCOLORREF(CurrentColors[FOCUS_FG_INACTIVE_NORMAL])));
    HInactiveSelectedPen = HANDLES(CreatePen(PS_DOT, 0, GetCOLORREF(CurrentColors[FOCUS_FG_INACTIVE_SELECTED])));

    HThumbnailNormalPen = HANDLES(CreatePen(PS_SOLID, 0, GetCOLORREF(CurrentColors[THUMBNAIL_FRAME_NORMAL])));
    HThumbnailFucsedPen = HANDLES(CreatePen(PS_SOLID, 0, GetCOLORREF(CurrentColors[THUMBNAIL_FRAME_FOCUSED])));
    HThumbnailSelectedPen = HANDLES(CreatePen(PS_SOLID, 0, GetCOLORREF(CurrentColors[THUMBNAIL_FRAME_SELECTED])));
    HThumbnailFocSelPen = HANDLES(CreatePen(PS_SOLID, 0, GetCOLORREF(CurrentColors[THUMBNAIL_FRAME_FOCSEL])));

    BtnShadowPen = HANDLES(CreatePen(PS_SOLID, 0, GetSysColor(COLOR_BTNSHADOW)));
    BtnHilightPen = HANDLES(CreatePen(PS_SOLID, 0, GetSysColor(COLOR_BTNHILIGHT)));
    Btn3DLightPen = HANDLES(CreatePen(PS_SOLID, 0, GetSysColor(COLOR_3DLIGHT)));
    BtnFacePen = HANDLES(CreatePen(PS_SOLID, 0, GetSysColor(COLOR_BTNFACE)));
    WndFramePen = HANDLES(CreatePen(PS_SOLID, 0, GetSysColor(COLOR_WINDOWFRAME)));
    WndPen = HANDLES(CreatePen(PS_SOLID, 0, GetSysColor(COLOR_WINDOW)));
    if (HActiveNormalPen == NULL || HActiveSelectedPen == NULL ||
        HInactiveNormalPen == NULL || HInactiveSelectedPen == NULL ||
        HThumbnailNormalPen == NULL || HThumbnailFucsedPen == NULL ||
        HThumbnailSelectedPen == NULL || HThumbnailFocSelPen == NULL ||
        BtnShadowPen == NULL || BtnHilightPen == NULL || BtnFacePen == NULL ||
        Btn3DLightPen == NULL || WndFramePen == NULL || WndPen == NULL)
    {
        TRACE_E("Unable to create a pen.");
        return FALSE;
    }

    COLORMAP clrMap[3];
    clrMap[0].from = RGB(255, 0, 255);
    clrMap[0].to = toolBarBkColor;
    clrMap[1].from = RGB(255, 255, 255);
    clrMap[1].to = GetSysColor(COLOR_BTNHILIGHT);
    clrMap[2].from = RGB(128, 128, 128);
    clrMap[2].to = GetSysColor(COLOR_BTNSHADOW);
    HHeaderSort = HANDLES(CreateMappedBitmap(HInstance, IDB_HEADER, 0, clrMap, 3));
    if (HHeaderSort == NULL)
    {
        TRACE_E("Unable to load bitmap HHeaderSort.");
        return FALSE;
    }

    // #96: draw the F-key caps at the image list's CURRENT size (queried, so a colours-only
    // rebuild - which does not recreate the lists - still fills them correctly).
    int capW = BOTTOMBAR_CX, capH = BOTTOMBAR_CY;
    ImageList_GetIconSize(HBottomTBImageList, &capW, &capH);
    BOOL capsUseDark = DarkMode_ShouldUseDark();
    // "inverted" relative to the bar, which is what the reference build does and what the
    // reporter asked for: the cap reads as a key, not as more body text.
    // Measured from the reference build the reporter praised: in dark mode the cap is only
    // slightly lighter than the bar - RGB(56,56,56) on RGB(32,32,32) - with a dark outline and
    // LIGHT text. Deliberately not an inverted white block.
    COLORREF capFillEnabled = capsUseDark ? RGB(70, 70, 74) : RGB(214, 214, 214);
    COLORREF capTextEnabled = capsUseDark ? RGB(232, 232, 232) : RGB(30, 30, 30);
    COLORREF capEdgeEnabled = capsUseDark ? RGB(25, 25, 28) : RGB(120, 120, 120);
    COLORREF capFillGrayed = capsUseDark ? RGB(58, 58, 61) : RGB(228, 228, 228);
    COLORREF capTextGrayed = capsUseDark ? RGB(132, 132, 132) : RGB(140, 140, 140);
    COLORREF capEdgeGrayed = capsUseDark ? RGB(30, 30, 33) : RGB(175, 175, 175);
    HBITMAP hBottomTB = CreateBottomTBKeyCapStrip(capW, capH, capFillGrayed, capTextGrayed, capEdgeGrayed);
    HBITMAP hHotBottomTB = CreateBottomTBKeyCapStrip(capW, capH, capFillEnabled, capTextEnabled, capEdgeEnabled);
    if (hBottomTB == NULL || hHotBottomTB == NULL)
    {
        TRACE_E("Unable to build bottom toolbar key caps.");
        if (hBottomTB != NULL)
            HANDLES(DeleteObject(hBottomTB));
        if (hHotBottomTB != NULL)
            HANDLES(DeleteObject(hHotBottomTB));
        return FALSE;
    }
    ImageList_RemoveAll(HBottomTBImageList);
    ImageList_AddMasked(HBottomTBImageList, hBottomTB, RGB(255, 0, 255));
    ImageList_RemoveAll(HHotBottomTBImageList);
    ImageList_AddMasked(HHotBottomTBImageList, hHotBottomTB, RGB(255, 0, 255));
    HANDLES(DeleteObject(hBottomTB));
    HANDLES(DeleteObject(hHotBottomTB));
    ImageList_SetBkColor(HBottomTBImageList, toolBarBkColor);
    ImageList_SetBkColor(HHotBottomTBImageList, toolBarBkColor);
    return TRUE;
}

// ****************************************************************************

void ReleaseGraphics(BOOL colorsOnly)
{
    if (!colorsOnly)
    {
        int i;
        for (i = 0; i < ICONSIZE_COUNT; i++)
        {
            if (HSharedOverlays[i] != NULL)
            {
                HANDLES(DestroyIcon(HSharedOverlays[i]));
                HSharedOverlays[i] = NULL;
            }
            if (HShortcutOverlays[i] != NULL)
            {
                HANDLES(DestroyIcon(HShortcutOverlays[i]));
                HShortcutOverlays[i] = NULL;
            }
            if (HSlowFileOverlays[i] != NULL)
            {
                HANDLES(DestroyIcon(HSlowFileOverlays[i]));
                HSlowFileOverlays[i] = NULL;
            }
        }

        if (HGroupIcon != NULL)
        {
            HANDLES(DestroyIcon(HGroupIcon));
            HGroupIcon = NULL;
        }

        if (HFavoritIcon != NULL)
        {
            HANDLES(DestroyIcon(HFavoritIcon));
            HFavoritIcon = NULL;
        }

        if (HZoomBitmap != NULL)
        {
            HANDLES(DeleteObject(HZoomBitmap));
            HZoomBitmap = NULL;
        }

        if (HFilter != NULL)
        {
            HANDLES(DeleteObject(HFilter));
            HFilter = NULL;
        }

        if (HUpDownBitmap != NULL)
        {
            HANDLES(DeleteObject(HUpDownBitmap));
            HUpDownBitmap = NULL;
        }
    }

    if (HNormalBkBrush != NULL)
    {
        HANDLES(DeleteObject(HNormalBkBrush));
        HNormalBkBrush = NULL;
    }
    if (HFocusedBkBrush != NULL)
    {
        HANDLES(DeleteObject(HFocusedBkBrush));
        HFocusedBkBrush = NULL;
    }
    if (HSelectedBkBrush != NULL)
    {
        HANDLES(DeleteObject(HSelectedBkBrush));
        HSelectedBkBrush = NULL;
    }
    if (HFocSelBkBrush != NULL)
    {
        HANDLES(DeleteObject(HFocSelBkBrush));
        HFocSelBkBrush = NULL;
    }
    if (HActiveCaptionBrush != NULL)
    {
        HANDLES(DeleteObject(HActiveCaptionBrush));
        HActiveCaptionBrush = NULL;
    }
    if (HInactiveCaptionBrush != NULL)
    {
        HANDLES(DeleteObject(HInactiveCaptionBrush));
        HInactiveCaptionBrush = NULL;
    }
    if (HActiveNormalPen != NULL)
    {
        HANDLES(DeleteObject(HActiveNormalPen));
        HActiveNormalPen = NULL;
    }
    if (HActiveSelectedPen != NULL)
    {
        HANDLES(DeleteObject(HActiveSelectedPen));
        HActiveSelectedPen = NULL;
    }
    if (HInactiveNormalPen != NULL)
    {
        HANDLES(DeleteObject(HInactiveNormalPen));
        HInactiveNormalPen = NULL;
    }
    if (HInactiveSelectedPen != NULL)
    {
        HANDLES(DeleteObject(HInactiveSelectedPen));
        HInactiveSelectedPen = NULL;
    }
    if (HThumbnailNormalPen != NULL)
    {
        HANDLES(DeleteObject(HThumbnailNormalPen));
        HThumbnailNormalPen = NULL;
    }
    if (HThumbnailFucsedPen != NULL)
    {
        HANDLES(DeleteObject(HThumbnailFucsedPen));
        HThumbnailFucsedPen = NULL;
    }
    if (HThumbnailSelectedPen != NULL)
    {
        HANDLES(DeleteObject(HThumbnailSelectedPen));
        HThumbnailSelectedPen = NULL;
    }
    if (HThumbnailFocSelPen != NULL)
    {
        HANDLES(DeleteObject(HThumbnailFocSelPen));
        HThumbnailFocSelPen = NULL;
    }
    if (BtnShadowPen != NULL)
    {
        HANDLES(DeleteObject(BtnShadowPen));
        BtnShadowPen = NULL;
    }
    if (BtnHilightPen != NULL)
    {
        HANDLES(DeleteObject(BtnHilightPen));
        BtnHilightPen = NULL;
    }
    if (Btn3DLightPen != NULL)
    {
        HANDLES(DeleteObject(Btn3DLightPen));
        Btn3DLightPen = NULL;
    }
    if (BtnFacePen != NULL)
    {
        HANDLES(DeleteObject(BtnFacePen));
        BtnFacePen = NULL;
    }
    if (WndFramePen != NULL)
    {
        HANDLES(DeleteObject(WndFramePen));
        WndFramePen = NULL;
    }
    if (WndPen != NULL)
    {
        HANDLES(DeleteObject(WndPen));
        WndPen = NULL;
    }
    if (HHeaderSort != NULL)
    {
        HANDLES(DeleteObject(HHeaderSort));
        HHeaderSort = NULL;
    }

    if (!colorsOnly)
    {
        if (HDitherBrush != NULL)
        {
            HANDLES(DeleteObject(HDitherBrush));
            HDitherBrush = NULL;
        }
        if (HHotToolBarImageList != NULL)
        {
            ImageList_Destroy(HHotToolBarImageList);
            HHotToolBarImageList = NULL;
        }
        if (HGrayToolBarImageList != NULL)
        {
            ImageList_Destroy(HGrayToolBarImageList);
            HGrayToolBarImageList = NULL;
        }
        if (HBottomTBImageList != NULL)
        {
            ImageList_Destroy(HBottomTBImageList);
            HBottomTBImageList = NULL;
        }
        if (HHotBottomTBImageList != NULL)
        {
            ImageList_Destroy(HHotBottomTBImageList);
            HHotBottomTBImageList = NULL;
        }
        if (HMenuMarkImageList != NULL)
        {
            ImageList_Destroy(HMenuMarkImageList);
            HMenuMarkImageList = NULL;
        }
        int i;
        for (i = 0; i < ICONSIZE_COUNT; i++)
        {
            if (SimpleIconLists[i] != NULL)
            {
                delete SimpleIconLists[i];
                SimpleIconLists[i] = NULL;
            }
        }
        if (ThrobberFrames != NULL)
        {
            delete ThrobberFrames;
            ThrobberFrames = NULL;
        }
        if (LockFrames != NULL)
        {
            delete LockFrames;
            LockFrames = NULL;
        }
        if (HFindSymbolsImageList != NULL)
        {
            ImageList_Destroy(HFindSymbolsImageList);
            HFindSymbolsImageList = NULL;
        }
        if (Shell32DLL != NULL)
        {
            HANDLES(FreeLibrary(Shell32DLL));
            Shell32DLL = NULL;
        }
        if (ImageResDLL != NULL)
        {
            HANDLES(FreeLibrary(ImageResDLL));
            ImageResDLL = NULL;
        }
    }
}

// ****************************************************************************

// 2026-08-25: NumberToStr2 (an alternate int-returning, thousands-separator-
// inserting variant of NumberToStr) was deleted - confirmed-dead (zero callers anywhere).

BOOL PointToLocalDecimalSeparator(std::wstring& text) noexcept
{
    const std::wstring::size_type point = text.find_last_of(L'.');
    if (point == std::wstring::npos)
        return TRUE;
    try
    {
        text.replace(point, 1, DecimalSeparator);
        return TRUE;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
}

// ****************************************************************************
//
// GetCmdLine - get parameters from command line
//
// buf + size - buffer for parameters
// argv - array of pointers that will be filled with parameters
// argCount - on input is the number of elements in argv, on output contains number of parameters
// cmdLine - command line parameters (without .exe file name - from WinMain)

BOOL GetCmdLine(wchar_t* buf, int size, wchar_t* argv[], int& argCount, const wchar_t* cmdLine)
{
    int space = argCount;
    argCount = 0;
    wchar_t* c = buf;
    wchar_t* end = buf + size;

    const wchar_t* s = cmdLine;
    wchar_t term;
    while (*s != 0)
    {
        if (*s == L'"') // opening '"'
        {
            if (*++s == 0)
                break;
            term = L'"';
        }
        else
            term = L' ';

        if (argCount < space && c < end)
            argv[argCount++] = c;
        else
            return c < end; // error only if buffer is too small

        while (1)
        {
            if (*s == term || *s == 0)
            {
                if (*s == 0 || term != L'"' || *++s != L'"') // unless it's replacement "" -> "
                {
                    if (*s != 0)
                        s++;
                    while (*s != 0 && *s == L' ')
                        s++;
                    if (c < end)
                    {
                        *c++ = 0;
                        break;
                    }
                    else
                        return FALSE;
                }
            }
            if (c < end)
                *c++ = *s++;
            else
                return FALSE;
        }
    }
    return TRUE;
}

// ****************************************************************************
//
// GetComCtlVersion
//

typedef struct _DllVersionInfo
{
    DWORD cbSize;
    DWORD dwMajorVersion; // Major version
    DWORD dwMinorVersion; // Minor version
    DWORD dwBuildNumber;  // Build number
    DWORD dwPlatformID;   // DLLVER_PLATFORM_*
} DLLVERSIONINFO;

typedef HRESULT(CALLBACK* DLLGETVERSIONPROC)(DLLVERSIONINFO*);

HRESULT GetComCtlVersion(LPDWORD pdwMajor, LPDWORD pdwMinor)
{
    HINSTANCE hComCtl;
    //load the DLL
    hComCtl = HANDLES(LoadLibraryW(L"comctl32.dll"));
    if (hComCtl)
    {
        HRESULT hr = S_OK;
        DLLGETVERSIONPROC pDllGetVersion;
        /*
     You must get this function explicitly because earlier versions of the DLL
     don't implement this function. That makes the lack of implementation of the
     function a version marker in itself.
    */
        // GetProcAddress's 2nd argument is always LPCSTR - exported symbol names are ANSI-only,
        // there is no GetProcAddressW - so a plain narrow literal, not TEXT(), is correct
        // regardless of this project's own UNICODE define.
        pDllGetVersion = (DLLGETVERSIONPROC)GetProcAddress(hComCtl, "DllGetVersion"); // has no header
        if (pDllGetVersion)
        {
            DLLVERSIONINFO dvi;
            ZeroMemory(&dvi, sizeof(dvi));
            dvi.cbSize = sizeof(dvi);
            hr = (*pDllGetVersion)(&dvi);
            if (SUCCEEDED(hr))
            {
                *pdwMajor = dvi.dwMajorVersion;
                *pdwMinor = dvi.dwMinorVersion;
            }
            else
            {
                hr = E_FAIL;
            }
        }
        else
        {
            /*
      If GetProcAddress failed, then the DLL is a version previous to the one
      shipped with IE 3.x.
      */
            *pdwMajor = 4;
            *pdwMinor = 0;
        }
        HANDLES(FreeLibrary(hComCtl));
        return hr;
    }
    TRACE_E("LoadLibrary on comctl32.dll failed");
    return E_FAIL;
}

// ****************************************************************************

void InitDefaultDir()
{
    wchar_t dir[4] = L" :\\";
    wchar_t d;
    for (d = L'A'; d <= L'Z'; d++)
    {
        dir[0] = d;
        DefaultDir[d - L'A'] = dir;
    }
}

// ****************************************************************************

BOOL PackErrorHandler(HWND parent, const WORD err, ...)
{
    va_list argList;
    BOOL ret = FALSE;

    parent = parent == NULL ? (MainWindow != NULL ? MainWindow->HWindow : NULL) : parent;

    // The packer callback keeps a byte varargs transport, but it is explicitly UTF-8:
    // resources and semantic values are UTF-16 owners on both sides of this adapter.
    const std::wstring format = LoadStrOwned(err);
    std::string formatUtf8;
    std::wstring message;
    va_start(argList, err);
    if (Win32EncodeText(CP_UTF8, format.c_str(), format.size(), formatUtf8))
    {
        char* formatted = NULL;
        const DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ALLOCATE_BUFFER,
                                            formatUtf8.c_str(), 0, 0,
                                            reinterpret_cast<LPSTR>(&formatted), 0, &argList);
        if (length != 0 && formatted != NULL)
        {
            if (!Win32DecodeText(CP_UTF8, formatted, length, message))
                message = L"Unable to decode packer diagnostic.";
            LocalFree(formatted);
        }
        else
            message = format;
    }
    else
        message = L"Unable to encode packer diagnostic.";
    va_end(argList);

    if (err < IDS_PACKQRY_PREFIX)
        gPrompter->ShowError(LoadStrOwned(IDS_PACKERR_TITLE).c_str(), message.c_str());
    else
        ret = gPrompter->ConfirmError(LoadStrOwned(IDS_PACKERR_TITLE).c_str(), message.c_str()).type == PromptResult::kOk;
    return ret;
}

//
// ****************************************************************************
// ColorsChanged
//

void ColorsChanged(BOOL refresh, BOOL colorsOnly, BOOL reloadUMIcons)
{
    CALL_STACK_MESSAGE2("ColorsChanged(%d)", refresh);
    // WARNING! fonts must be FALSE, to prevent font handle change, which
    // the toolbars using it must be notified about
    ReleaseGraphics(colorsOnly);
    InitializeGraphics(colorsOnly);
    ItemBitmap.ReCreateForScreenDC();
    UpdateViewerColors(ViewerColors);
    if (!colorsOnly)
        ShellIconOverlays.ColorsChanged();

    if (MainWindow != NULL && MainWindow->EditWindow != NULL)
        MainWindow->EditWindow->SetFont();

    Associations.ColorsChanged();

    if (MainWindow != NULL)
    {
        MainWindow->OnColorsChanged(reloadUMIcons);
    }

    // notify find dialogs about color change
    FindDialogQueue.BroadcastMessage(WM_USER_COLORCHANGEFIND, 0, 0);

    // broadcast this news to plugins too
    Plugins.Event(PLUGINEVENT_COLORSCHANGED, 0);

    if (MainWindow != NULL && MainWindow->HTopRebar != NULL)
    {
        DarkModeMainFramePalette palette;
        if (DarkMode_GetMainFramePalette(&palette))
            SendMessage(MainWindow->HTopRebar, RB_SETBKCOLOR, 0, (LPARAM)palette.Fill);
        else
            SendMessage(MainWindow->HTopRebar, RB_SETBKCOLOR, 0, (LPARAM)GetSysColor(COLOR_BTNFACE));
        RedrawWindow(MainWindow->HTopRebar, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    }

    if (refresh && MainWindow != NULL)
    {
        InvalidateRect(MainWindow->HWindow, NULL, TRUE);
    }
    // Internal Viewer and Find: refresh all windows
    BroadcastConfigChanged();
}

#ifdef USE_BETA_EXPIRATION_DATE

int ShowBetaExpDlg()
{
    CBetaExpiredDialog dlg(NULL);
    return (int)dlg.Execute();
}

#endif // USE_BETA_EXPIRATION_DATE

//
// ****************************************************************************

struct VS_VERSIONINFO_HEADER
{
    WORD wLength;
    WORD wValueLength;
    WORD wType;
};

BOOL GetModuleVersion(HINSTANCE hModule, WORD* major, WORD* minor)
{
    HRSRC hRes = FindResource(hModule, MAKEINTRESOURCE(VS_VERSION_INFO), RT_VERSION);
    if (hRes == NULL)
        return FALSE;

    HGLOBAL hVer = LoadResource(hModule, hRes);
    if (hVer == NULL)
        return FALSE;

    DWORD resSize = SizeofResource(hModule, hRes);
    const BYTE* first = (BYTE*)LockResource(hVer);
    if (resSize == 0 || first == 0)
        return FALSE;

    const BYTE* iterator = first + sizeof(VS_VERSIONINFO_HEADER);

    DWORD signature = 0xFEEF04BD;

    while (memcmp(iterator, &signature, 4) != 0)
    {
        iterator++;
        if (iterator + 4 >= first + resSize)
            return FALSE;
    }

    VS_FIXEDFILEINFO* ffi = (VS_FIXEDFILEINFO*)iterator;

    *major = HIWORD(ffi->dwFileVersionMS);
    *minor = LOWORD(ffi->dwFileVersionMS);

    return TRUE;
}

//****************************************************************************
//
// CMessagesKeeper
//

CMessagesKeeper::CMessagesKeeper()
{
    Index = 0;
    Count = 0;
}

void CMessagesKeeper::Add(const MSG* msg)
{
    Messages[Index] = *msg;
    Index = (Index + 1) % MESSAGES_KEEPER_COUNT;
    if (Count < MESSAGES_KEEPER_COUNT)
        Count++;
}

void CMessagesKeeper::Print(char* buffer, int buffMax, int index)
{
    if (buffMax <= 0)
        return;
    if (index >= Count)
    {
        _snprintf_s(buffer, buffMax, _TRUNCATE, "(error)");
    }
    else
    {
        int i;
        if (Count == MESSAGES_KEEPER_COUNT)
            i = (Index + index) % MESSAGES_KEEPER_COUNT;
        else
            i = index;
        const MSG* msg = &Messages[i];
        _snprintf_s(buffer, buffMax, _TRUNCATE, "w=0x%p m=0x%X w=0x%IX l=0x%IX t=%u p=%d,%d",
                    msg->hwnd, msg->message, msg->wParam, msg->lParam, msg->time - SalamanderExceptionTime, msg->pt.x, msg->pt.y);
    }
}

CMessagesKeeper MessagesKeeper;

typedef VOID(WINAPI* FDisableProcessWindowsGhosting)(VOID);

void TurnOFFWindowGhosting() // when "ghosting" is not turned off, safe-wait windows hide after five seconds of "not responding" app state (when app doesn't process messages)
{
    if (User32DLL != NULL)
    {
        FDisableProcessWindowsGhosting disableProcessWindowsGhosting = (FDisableProcessWindowsGhosting)GetProcAddress(User32DLL, "DisableProcessWindowsGhosting"); // Min: XP
        if (disableProcessWindowsGhosting != NULL)
            disableProcessWindowsGhosting();
    }
}

//
// ****************************************************************************

void CleanUID(char* uid)
{
    char* s = uid;
    char* d = uid;
    while (*s != 0)
    {
        while (*s == '{' || *s == '}' || *s == '-')
            s++;
        *d++ = *s++;
    }
}

//
// ****************************************************************************

//#ifdef MSVC_RUNTIME_CHECKS
char RTCErrorDescription[RTC_ERROR_DESCRIPTION_SIZE] = {0};
// custom reporting function copied from MSDN - http://msdn.microsoft.com/en-us/library/cb00sk7k(v=VS.90).aspx
#pragma runtime_checks("", off)
int MyRTCErrorFunc(int errType, const wchar_t* file, int line,
                   const wchar_t* module, const wchar_t* format, ...)
{
    // Prevent re-entrance.
    static long running = 0;
    while (InterlockedExchange(&running, 1))
        Sleep(0);
    // Now, disable all RTC failures.
    int numErrors = _RTC_NumErrors();
    int* errors = (int*)_alloca(numErrors);
    for (int i = 0; i < numErrors; i++)
        errors[i] = _RTC_SetErrorType((_RTC_ErrorNumber)i, _RTC_ERRTYPE_IGNORE);

    // First, get the rtc error number from the var-arg list.
    va_list vl;
    va_start(vl, format);
    _RTC_ErrorNumber rtc_errnum = va_arg(vl, _RTC_ErrorNumber);
    va_end(vl);

    static wchar_t buf[RTC_ERROR_DESCRIPTION_SIZE];
    const char* err = _RTC_GetErrDesc(rtc_errnum);
    _snwprintf_s(buf, _TRUNCATE, L"  Error Number: %d\r\n  Description: %S\r\n  Line: #%d\r\n  File: %s\r\n  Module: %s\r\n",
                 rtc_errnum,
                 err,
                 line,
                 file ? file : L"Unknown",
                 module ? module : L"Unknown");

    sally::diagnostic::CopyAcpLossy(buf, RTCErrorDescription,
                                    RTC_ERROR_DESCRIPTION_SIZE);

    // better to break here with exception, hopefully clearer callstack - if not, we can remove this exception here
    // see description of _CrtDbgReportW behavior - http://msdn.microsoft.com/en-us/library/8hyw4sy7(v=VS.90).aspx
    RaiseException(OPENSAL_EXCEPTION_RTC, 0, 0, NULL); // our own "rtc" exception

    // we won't reach here anymore, process was terminated; continuing only for formality, in case we change something

    // Now, restore the RTC errortypes.
    for (int i = 0; i < numErrors; i++)
        _RTC_SetErrorType((_RTC_ErrorNumber)i, errors[i]);
    running = 0;

    return -1;
}
#pragma runtime_checks("", restore)
//#endif // MSVC_RUNTIME_CHECKS

//
// ****************************************************************************

#ifdef _DEBUG

DWORD LastCrtCheckMemoryTime; // when we last checked memory in IDLE

#endif //_DEBUG

BOOL FindPluginsWithoutImportedCfg(BOOL* doNotDeleteImportedCfg)
{
    wchar_t names[1000];
    int skipped;
    Plugins.RemoveNoLongerExistingPlugins(FALSE, TRUE, names, 1000, 10, &skipped, MainWindow->HWindow);
    if (names[0] != 0)
    {
        *doNotDeleteImportedCfg = TRUE;
        wchar_t skippedNames[200];
        skippedNames[0] = 0;
        if (skipped > 0)
        {
            // was sprintf() into a wide buffer with a narrow format, and the two
            // AnsiToWide() calls below re-encoded buffers that had never been narrow.
            _snwprintf_s(skippedNames, _TRUNCATE, LoadStrW(IDS_NUMOFSKIPPEDPLUGINNAMES), skipped);
        }
        std::wstring msg = FormatStrW(LoadStrW(IDS_NOTALLPLUGINSCFGIMPORTED), names, skippedNames);
        // OK = start without missing plugins, Cancel = exit
        return gPrompter->ConfirmError(SALAMANDER_TEXT_VERSIONW(), msg.c_str()).type == PromptResult::kCancel;
    }
    return FALSE;
}

// Wide version - no MAX_PATH buffer limitations
void StartNotepadW(const wchar_t* file)
{
    std::wstring sysDir;
    if (!gEnvironment->GetSystemDirectory(sysDir).success)
        return;

    // Build command line: notepad.exe "filename"
    std::wstring cmdLine = L"notepad.exe \"";
    cmdLine += file;
    cmdLine += L"\"";

    ExternalToolRequest request;
    request.commandLine = cmdLine;
    request.workingDirectory = sysDir;
    request.inheritHandles = true;
    ExternalToolResult result = gExternalToolRunner != NULL
                                    ? gExternalToolRunner->Launch(request)
                                    : ExternalToolResult::Error(ERROR_INVALID_PARAMETER);
    result.CloseProcess();
}

BOOL RunningInCompatibilityMode()
{
    // If running under XP or later OS, there's a risk that an eager user enabled
    // Compatibility Mode. If so, we'll show a warning.
    // WARNING: Application Verifier sets Windows version higher than it really is,
    // it does this when testing app for "Windows 7 Software Logo".
    WORD kernel32major, kernel32minor;
    if (GetModuleVersion(GetModuleHandleA("kernel32.dll"), &kernel32major, &kernel32minor))
    {
        TRACE_I("kernel32.dll: " << kernel32major << ":" << kernel32minor);
        // we must call GetVersionEx, because it returns values according to the set Compatibility Mode
        // (SalIsWindowsVersionOrGreater ignores the set Compatibility Mode)
        // Explicitly OSVERSIONINFOA (not the wchar_t-generic OSVERSIONINFO) - the code below
        // deliberately resolves and calls "GetVersionExA" by name, so the struct it fills
        // must always be the A layout regardless of this project's own UNICODE define.
        OSVERSIONINFOA os;
        os.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);

        // just avoiding deprecated warning, GetVersionEx should be available always and everywhere
        typedef BOOL(WINAPI * FDynGetVersionExA)(LPOSVERSIONINFOA lpVersionInformation);
        FDynGetVersionExA DynGetVersionExA = (FDynGetVersionExA)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                                                               "GetVersionExA");
        if (DynGetVersionExA == NULL)
        {
            TRACE_E("RunningInCompatibilityMode(): unable to get address of GetVersionEx()");
            return FALSE;
        }

        DynGetVersionExA(&os);
        TRACE_I("GetVersionEx(): " << os.dwMajorVersion << ":" << os.dwMinorVersion);

        // current version of Salamander is manifested for Windows 10
        const DWORD SAL_MANIFESTED_FOR_MAJOR = 10;
        const DWORD SAL_MANIFESTED_FOR_MINOR = 0;

        // GetVersionEx will never return more than os.dwMajorVersion == SAL_MANIFESTED_FOR_MAJOR
        // and os.dwMinorVersion == SAL_MANIFESTED_FOR_MINOR, so if kernel32.dll is higher
        // version, we can't 100% detect Compatibility Mode, need to manifest Salamander for
        // new Windows and update SAL_MANIFESTED_FOR_MAJOR and SAL_MANIFESTED_FOR_MINOR
        // constants, we detect at least Compatibility Mode set to older Windows than
        // Salamander is manifested for
        if (kernel32major > SAL_MANIFESTED_FOR_MAJOR ||
            kernel32major == SAL_MANIFESTED_FOR_MAJOR && kernel32minor > SAL_MANIFESTED_FOR_MINOR)
        {
            kernel32major = SAL_MANIFESTED_FOR_MAJOR;
            kernel32minor = SAL_MANIFESTED_FOR_MINOR;
            TRACE_I("kernel32.dll version was limited by Salamander's manifest to: " << kernel32major << ":" << kernel32minor);
        }
        if (kernel32major > os.dwMajorVersion || kernel32major == os.dwMajorVersion && kernel32minor > os.dwMinorVersion)
            return TRUE;
    }
    return FALSE;
}

static std::wstring ResolveCommandLinePath(const std::wstring& argument,
                                           BOOL hotpathForJumplist)
{
    if (argument.empty())
        return {};

    std::wstring result = argument;
    if (hotpathForJumplist)
    {
        std::wstring expanded;
        if (ExpandHotPath(NULL, argument.c_str(), expanded, FALSE))
            result.swap(expanded);
        else
            TRACE_E("ExpandHotPath failed.");
    }
    else
    {
        // ExpandEnvironmentStringsW reports the required size including NUL.
        // Query first so semantic command-line paths never inherit a ceiling.
        DWORD required = ExpandEnvironmentStringsW(argument.c_str(), NULL, 0);
        if (required != 0)
        {
            std::vector<wchar_t> expanded(required);
            DWORD written = ExpandEnvironmentStringsW(argument.c_str(),
                                                       expanded.data(), required);
            if (written != 0 && written <= required)
                result.assign(expanded.data());
            else
                TRACE_E("ExpandEnvironmentStrings failed.");
        }
        else
            TRACE_E("ExpandEnvironmentStrings failed.");
    }

    std::wstring curDir;
    if (!IsPluginFSPath(result.c_str()) &&
        gEnvironment->GetCurrentDirectory(curDir).success)
    {
        SalGetFullNameW(result, NULL, curDir.c_str(), NULL, NULL);
    }
    return result;
}

// If parameters are valid, returns TRUE. All semantic text remains dynamically
// owned UTF-16 from GetCommandLineW through startup and activation handoff.
BOOL ParseCommandLineParameters(
    LPSTR cmdLine, sally::cmdline::CommandLineRequest* commandLineRequest)
{
    if (commandLineRequest == NULL)
        return FALSE;

    (void)cmdLine; // WinMain's lpCmdLine has already passed through CP_ACP.
    try
    {
        sally::cmdline::CommandLineRequest parsed =
            sally::cmdline::ParseRawCommandLine(GetCommandLineW());
        if (!parsed.ok())
            return FALSE;

        // Build the install-relative config.reg path in Unicode so non-ASCII
        // install directories survive intact through ImportConfigurationW.
        ConfigurationNameW.clear();
        if (!BuildModuleRelativePathW(HInstance, L"config.reg", ConfigurationNameW))
            ConfigurationNameW.clear();
        if (!ConfigurationNameW.empty() &&
            !FileExistsWLocal(ConfigurationNameW.c_str()))
        {
            std::wstring appdata;
            if (GetOurPathInRoamingAPPDATAW(appdata))
            {
                if (!appdata.empty() && appdata.back() != L'\\')
                    appdata.push_back(L'\\');
                appdata.append(L"config.reg");
                if (FileExistsWLocal(appdata.c_str()))
                {
                    ConfigurationNameW = appdata;
                    ConfigurationNameIgnoreIfNotExists = FALSE;
                }
            }
        }

        parsed.leftPath = ResolveCommandLinePath(parsed.leftPath, FALSE);
        parsed.rightPath = ResolveCommandLinePath(parsed.rightPath, FALSE);
        parsed.activePath = ResolveCommandLinePath(
            parsed.activePath, parsed.activePathUsesHotPath ? TRUE : FALSE);

        if (parsed.configFileSpecified)
        {
            const std::wstring& config = parsed.configFile;
            if ((config.size() >= 2 && config[0] == L'\\' &&
                 config[1] == L'\\') ||
                (config.size() >= 2 && config[1] == L':'))
            {
                ConfigurationNameW = config;
            }
            else
            {
                if (!BuildModuleRelativePathW(HInstance, config.c_str(),
                                              ConfigurationNameW))
                {
                    ConfigurationNameW.clear();
                }
                if (!ConfigurationNameW.empty() &&
                    !FileExistsWLocal(ConfigurationNameW.c_str()))
                {
                    std::wstring appdata;
                    if (GetOurPathInRoamingAPPDATAW(appdata))
                    {
                        if (!appdata.empty() && appdata.back() != L'\\')
                            appdata.push_back(L'\\');
                        appdata.append(config);
                        if (FileExistsWLocal(appdata.c_str()))
                            ConfigurationNameW = appdata;
                    }
                }
            }
            ConfigurationNameIgnoreIfNotExists = FALSE;
        }

        if (parsed.setMainWindowIconIndex)
        {
            Configuration.MainWindowIconIndexForced =
                parsed.mainWindowIconIndex;
        }
        if (parsed.titlePrefixSpecified)
        {
            Configuration.UseTitleBarPrefixForced = TRUE;
            if (parsed.setTitlePrefix)
                Configuration.TitleBarPrefixForced = parsed.titlePrefix;
        }
        if (parsed.forceOnlyOneInstance)
            Configuration.ForceOnlyOneInstance = TRUE;

        OpenReadmeInNotepad = parsed.runNotepadPath;
        *commandLineRequest = std::move(parsed);
        return TRUE;
    }
    catch (const std::bad_alloc&)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }
}

int WinMainBody(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR cmdLine, int cmdShow)
{
    int myExitCode = 1;

    //--- don't want any critical errors like "no disk in drive A:"
    SetErrorMode(SetErrorMode(0) | SEM_FAILCRITICALERRORS);

    // seed random number generator
    srand((unsigned)time(NULL) ^ (unsigned)_getpid());

#ifdef _DEBUG
    // #define _CRTDBG_ALLOC_MEM_DF        0x01  /* Turn on debug allocation */ (DEFAULT ON)
    // #define _CRTDBG_DELAY_FREE_MEM_DF   0x02  /* Don't actually free memory */
    // #define _CRTDBG_CHECK_ALWAYS_DF     0x04  /* Check heap every alloc/dealloc */
    // #define _CRTDBG_RESERVED_DF         0x08  /* Reserved - do not use */
    // #define _CRTDBG_CHECK_CRT_DF        0x10  /* Leak check/diff CRT blocks */
    // #define _CRTDBG_LEAK_CHECK_DF       0x20  /* Leak check at program exit */

    // if you suspect overwriting allocated memory, you can uncomment the following two lines
    // Salamander will slow down and heap consistency test will be performed on each free/alloc
    // int crtDbg = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);   // Get the current bits
    // _CrtSetDbgFlag(crtDbg | _CRTDBG_CHECK_ALWAYS_DF);
    // _CrtSetDbgFlag(crtDbg | _CRTDBG_LEAK_CHECK_DF);

    // another interesting debugging function: if a memory leak occurs, a decimal number
    // is displayed in brackets indicating the order of the allocated block, e.g. _CRT_WARN: {104200};
    // _CrtSetBreakAlloc function allows to break on this block
    // _CrtSetBreakAlloc(7700);

    LastCrtCheckMemoryTime = GetTickCount();

    // this case of overwriting end of memory will trigger protection -- a messagebox will appear in IDLE
    // and debug messages will be poured into TraceServer
    //
//  char *p1 = (char*)malloc( 4 );
//  strcpy( p1 , "Oops" );
#endif //_DEBUG

    /*
   // test "Heap Block Corruptions: Full-page heap", see http://support.microsoft.com/kb/286470
   // allocates all blocks (must have at least 16 bytes) so that there's an inaccessible page after them,
   // thus any overwrite of block end leads to exception
   // install Debugging Tools for Windows, in gflags.exe for "sally.exe" select "Enable page heap",
   // it worked for me under W2K and XP (should work under Vista too)
   // OR: use prepared sal-pageheap-register.reg and sal-pageheap-unregister.reg (then you don't need
   // to install Debugging Tools for Windows)
   //
   // December/2011: I tested under VS2008 + page heap + Win7x64 and the following overwrite doesn't trigger exception
   // I found description of allocation in this mode: http://msdn.microsoft.com/en-us/library/ms220938(v=VS.90).aspx

  char *test = (char *)malloc(16);
//  char *test = (char *)HeapAlloc(GetProcessHeap(), 0, 16);
  wchar_t bufff[100];
  sprintf(bufff, "test=%p", test);
  MessageBox(NULL, bufff, "a", MB_OK);
  test[16] = 0;
*/

    char testCharValue = 129;
    int testChar = testCharValue;
    if (testChar != 129) // if testChar is negative, we have a problem: LowerCase[testCharValue] reaches outside the array...
    {
        // wide: English-only compiler-config diagnostic, no LoadStr/resource
        // involved - same shape as heap.cpp's Debug leak popup (iteration 196).
        MessageBoxW(NULL, L"Default type 'char' is not 'unsigned char', but 'signed char'. See '/J' compiler switch in MSVC.",
                    L"Compilation Error", MB_OK | MB_ICONSTOP);
    }

    MainThreadID = GetCurrentThreadId();
    HInstance = hInstance;
    CALL_STACK_MESSAGE4("WinMainBody(0x%p, , %s, %d)", hInstance, cmdLine, cmdShow);

    // Well, I'm not to blame for this... what to do when the competition does it, we must
    // too - inspired by Explorer.
    // And I was wondering why their paint runs so nicely.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    SetTraceProcessNameW(MAINWINDOW_NAME);
    SetThreadNameInVCAndTrace(L"Main");
    SetMessagesTitleW(MAINWINDOW_NAME);

    // Initialize default UI prompter (UTF-16 first) for decoupled prompts.
    gPrompter = GetUIPrompter();
    TRACE_I("Begin");

    // OLE initialization
    if (FAILED(OleInitialize(NULL)))
    {
        TRACE_E("Error in CoInitialize.");
        return 1;
    }

    //  HOldWPHookProc = SetWindowsHookEx(WH_CALLWNDPROC,     // HANDLES doesn't know how!
    //                                    WPMessageHookProc,
    //                                    NULL, GetCurrentThreadId());

    User32DLL = NOHANDLES(LoadLibraryA("user32.dll"));
    if (User32DLL == NULL)
        TRACE_E("Unable to load library user32.dll."); // not a fatal error

    TurnOFFWindowGhosting();

    NtDLL = HANDLES(LoadLibraryA("NTDLL.DLL"));
    if (NtDLL == NULL)
        TRACE_E("Unable to load library ntdll.dll."); // not a fatal error

    // detection of default user charset for fonts
    CHARSETINFO ci;
    memset(&ci, 0, sizeof(ci));
    wchar_t bufANSI[10];
    if (GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE, bufANSI, 10))
    {
        if (TranslateCharsetInfo((DWORD*)(DWORD_PTR)MAKELONG(_wtoi(bufANSI), 0), &ci, TCI_SRCCODEPAGE))
        {
            UserCharset = ci.ciCharset;
        }
    }

    // due to using memory-mapped files, it's necessary to get allocation granularity
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    AllocationGranularity = si.dwAllocationGranularity;

    // Windows Versions supported by Open Salamander
    //
    // Name               wMajorVersion  wMinorVersion
    //------------------------------------------------
    // Windows XP         5              1
    // Windows XP x64     5              2
    // Windows Vista      6              0
    // Windows 7          6              1
    // Windows 8          6              2
    // Windows 8.1        6              3
    // Windows 10         10             0             (note: preview versions of W10 from 2014 returned version 6.4)

    if (!SalIsWindowsVersionOrGreater(6, 1, 0))
    {
        // we probably won't get here, on older systems exports of statically linked libraries will be missing
        // and the user will be served some incomprehensible message at the PE loader level in Windows
        // do not call SalMessageBox
        // wide: same MessageBoxW/SALAMANDER_TEXT_VERSIONW() pairing already used
        // elsewhere in this file (e.g. the language-file error paths above).
        MessageBoxW(NULL, L"You need at least Windows 7 to run this program.",
                    SALAMANDER_TEXT_VERSIONW(), MB_OK | MB_ICONEXCLAMATION);
    EXIT_1:
        if (User32DLL != NULL)
        {
            NOHANDLES(FreeLibrary(User32DLL));
            User32DLL = NULL;
        }
        if (NtDLL != NULL)
        {
            HANDLES(FreeLibrary(NtDLL));
            NtDLL = NULL;
        }
        return myExitCode;
    }

    WindowsVistaAndLater = SalIsWindowsVersionOrGreater(6, 0, 0);
    WindowsXP64AndLater = SalIsWindowsVersionOrGreater(5, 2, 0);
    Windows7AndLater = SalIsWindowsVersionOrGreater(6, 1, 0);
    Windows8AndLater = SalIsWindowsVersionOrGreater(6, 2, 0);
    Windows8_1AndLater = SalIsWindowsVersionOrGreater(6, 3, 0);
    Windows10AndLater = SalIsWindowsVersionOrGreater(10, 0, 0);

    DWORD integrityLevel;
    if (GetProcessIntegrityLevel(&integrityLevel) && integrityLevel >= SECURITY_MANDATORY_HIGH_RID)
        RunningAsAdmin = TRUE;

    // if possible, we'll use GetNativeSystemInfo, otherwise we'll keep the result of GetSystemInfo
    typedef void(WINAPI * PGNSI)(LPSYSTEM_INFO);
    PGNSI pGNSI = (PGNSI)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetNativeSystemInfo"); // Min: XP
    if (pGNSI != NULL)
        pGNSI(&si);
    Windows64Bit = si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64;

    if (!gEnvironment->GetWindowsDirectory(WindowsDirectory).success)
        WindowsDirectory.clear();

    // we're interested in the ITaskbarList3 interface, which MS introduced from Windows 7 - for example progress in taskbar buttons
    if (Windows7AndLater)
    {
        TaskbarBtnCreatedMsg = RegisterWindowMessageA("TaskbarButtonCreated");
        if (TaskbarBtnCreatedMsg == 0)
        {
            DWORD err = GetLastError();
            TRACE_E("RegisterWindowMessage() failed for 'TaskbarButtonCreated'. Error:" << err);
        }
    }

    // we have global variables set, we can initialize this mutex
    if (!TaskList.Init())
        TRACE_E("TaskList.Init() failed!");

    if (!InitializeWinLib())
        goto EXIT_1; // we must initialize WinLib before first showing
                     // of wait dialog (window classes must be registered)
                     // ImportConfiguration can already open this dialog

    LoadSaveToRegistryMutex.Init();

    // try to extract "AutoImportConfig" value from current configuration -> exists in case we're performing UPGRADE
    BOOL autoImportConfig = FALSE;
    std::wstring autoImportConfigFromKey;
    if (!GetUpgradeInfo(&autoImportConfig, autoImportConfigFromKey)) // user wishes to exit the software
    {
        myExitCode = 0;
    EXIT_1a:
        ReleaseWinLib();
        goto EXIT_1;
    }
    const wchar_t* configKey = autoImportConfig ? autoImportConfigFromKey.c_str() : SalamanderConfigurationRoots[0];

    // try to extract the language-determining key from current configuration
    LoadSaveToRegistryMutex.Enter();
    HKEY hSalamander;
    DWORD langChanged = FALSE; // TRUE = we're starting Salamander for the first time with a different language (we'll load all plugins to verify we have this language version for them too, or let user decide which alternative versions to use)
    if (OpenKeyW(HKEY_CURRENT_USER, configKey, hSalamander))
    {
        HKEY actKey;
        DWORD configVersion = 1; // this is config from 1.52 and older
        if (OpenKeyW(hSalamander, SALAMANDER_VERSION_REG, actKey))
        {
            configVersion = 2; // this is config from 1.6b1
            GetValueW(actKey, SALAMANDER_VERSIONREG_REG, REG_DWORD,
                      &configVersion, sizeof(DWORD));
            CloseKey(actKey);
        }
        if (configVersion >= 59 /* 2.53 beta 2 */ && // before 2.53 beta 2 there was only English, so reading doesn't make sense, we'll offer user default system language or manual language selection
            OpenKeyW(hSalamander, SALAMANDER_CONFIG_REG, actKey))
        {
            GetStringValueW(actKey, CONFIG_LANGUAGE_REG, Configuration.SLGName);
            GetValueW(actKey, CONFIG_USEALTLANGFORPLUGINS_REG, REG_DWORD,
                      &Configuration.UseAsAltSLGInOtherPlugins, sizeof(DWORD));
            GetStringValueW(actKey, CONFIG_ALTLANGFORPLUGINS_REG, Configuration.AltPluginSLGName);
            GetValueW(actKey, CONFIG_LANGUAGECHANGED_REG, REG_DWORD, &langChanged, sizeof(DWORD));
            CloseKey(actKey);
        }
        CloseKey(hSalamander);
    }
    LoadSaveToRegistryMutex.Leave();

FIND_NEW_SLG_FILE:

    // if key doesn't exist, we'll show selection dialog
    BOOL newSLGFile = FALSE; // TRUE if .SLG was selected during this Salamander launch
    if (Configuration.SLGName.empty())
    {
        CLanguageSelectorDialog slgDialog(NULL, Configuration.SLGName, NULL);
        slgDialog.Initialize();
        if (slgDialog.GetLanguagesCount() == 0)
        {
            // wide: same MessageBoxW/SALAMANDER_TEXT_VERSIONW() pairing already
            // used a few lines below in this same function (the "not a valid language file" path).
            MessageBoxW(NULL, L"Unable to find any language file (.SLG) in subdirectory LANG.\n"
                              L"Please reinstall Open Salamander.",
                        SALAMANDER_TEXT_VERSIONW(), MB_OK | MB_ICONERROR);
            goto EXIT_1a;
        }
        Configuration.UseAsAltSLGInOtherPlugins = FALSE;
        Configuration.AltPluginSLGName.clear();

        std::wstring prevVerSLGName;
        if (!autoImportConfig &&                            // during UPGRADE this doesn't make sense (language is read a few lines above, this routine would just re-read it)
            FindLanguageFromPrevVerOfSal(prevVerSLGName) && // we'll import language from previous version, it's quite probable user wants to use it again (it's about importing old Salamander configuration)
            slgDialog.SLGNameExists(prevVerSLGName.c_str()))
        {
            Configuration.SLGName = prevVerSLGName;
        }
        else
        {
            int langIndex = slgDialog.GetPreferredLanguageIndex(NULL, TRUE);
            if (langIndex == -1) // this installation doesn't contain language matching current user-locale in Windows
            {

// if this is commented out, we won't send people to download language versions from web (e.g. when there are none)
// JRY: for AS 2.53, which comes with Czech, German and English, for other languages we'll send them to forum section
//      "Translations" https://forum.altap.cz/viewforum.php?f=23 - maybe it will motivate someone to create their translation
#define OFFER_OTHERLANGUAGE_VERSIONS

#ifndef OFFER_OTHERLANGUAGE_VERSIONS
                if (slgDialog.GetLanguagesCount() == 1)
                    slgDialog.GetSLGName(Configuration.SLGName); // if only one language exists, we'll use it
                else
                {
#endif // OFFER_OTHERLANGUAGE_VERSIONS

                    // we'll open language selection dialog, so user can download and install other languages
                    if (slgDialog.Execute() == IDCANCEL)
                        goto EXIT_1a;

#ifndef OFFER_OTHERLANGUAGE_VERSIONS
                }
#endif // OFFER_OTHERLANGUAGE_VERSIONS
            }
            else
            {
                slgDialog.GetSLGName(Configuration.SLGName, langIndex); // if language matching current user-locale in Windows exists, we'll use it
            }
        }
        newSLGFile = TRUE;
        langChanged = TRUE;
    }

    std::wstring pathW;
    BuildModuleRelativePathW(NULL, (L"lang\\" + Configuration.SLGName).c_str(), pathW);
    HLanguage = pathW.empty() ? NULL : HANDLES(LoadLibraryW(pathW.c_str()));
    LanguageID = 0;
    if (HLanguage == NULL || !IsSLGFileValid(HInstance, HLanguage, LanguageID, IsSLGIncomplete))
    {
        if (HLanguage != NULL)
            HANDLES(FreeLibrary(HLanguage));
        if (!newSLGFile) // remembered .SLG file probably stopped existing, we'll try to find another one
        {
            std::wstring errorText = FormatStrW(L"File %s was not found or is not valid language file.\nSally "
                                                L"will try to search for some other language file (.SLG).",
                                                pathW.c_str());
            MessageBoxW(NULL, errorText.c_str(), SALAMANDER_TEXT_VERSIONW(), MB_OK | MB_ICONERROR);
            Configuration.SLGName.clear();
            goto FIND_NEW_SLG_FILE;
        }
        else // shouldn't happen at all - .SLG file was already tested
        {
            std::wstring errorText = FormatStrW(L"File %s was not found or is not valid language file.\n"
                                                L"Please run Sally again and try to choose some other language file.",
                                                pathW.c_str());
            MessageBoxW(NULL, errorText.c_str(), L"Sally", MB_OK | MB_ICONERROR);
            goto EXIT_1a;
        }
    }

    Configuration.LoadedSLGName = Configuration.SLGName;

    // let already running salmon load the selected SLG (it was using some provisional one so far)
    SalmonSetSLG(Configuration.SLGName.c_str());

    // set localized messages into ALLOCHAN module (ensures reporting to user when memory is low + Retry button + if all fails then Cancel to terminate the software)
    // SetAllocHandlerMessage is wchar_t-generic (winlib's own ALLOCHAN string table);
    // LoadStr/SALAMANDER_TEXT_VERSION are permanently narrow, so under _UNICODE this needs their
    // already-established wide siblings instead.
    SetAllocHandlerMessage(LoadStrW(IDS_ALLOCHANDLER_MSG), SALAMANDER_TEXT_VERSIONW(),
                           LoadStrW(IDS_ALLOCHANDLER_WRNIGNORE), LoadStrW(IDS_ALLOCHANDLER_WRNABORT));

    sally::cmdline::CommandLineRequest cmdLineParams;
    if (!ParseCommandLineParameters(cmdLine, &cmdLineParams))
    {
        gPrompter->ShowError(SALAMANDER_TEXT_VERSIONW(), LoadStrW(IDS_INVALIDCMDLINE));

    EXIT_2:
        if (HLanguage != NULL)
            HANDLES(FreeLibrary(HLanguage));
        goto EXIT_1a;
    }

    if (RunningInCompatibilityMode())
    {
        CCommonDialog dlg(HLanguage, IDD_COMPATIBILITY_MODE, NULL);
        if (dlg.Execute() == IDCANCEL)
            goto EXIT_2;
    }

#ifdef USE_BETA_EXPIRATION_DATE
    // beta version is time-limited, see BETA_EXPIRATION_DATE
    // if today is the day specified by this variable or any later, we'll show a window and end
    SYSTEMTIME st;
    GetLocalTime(&st);
    SYSTEMTIME* expire = &BETA_EXPIRATION_DATE;
    if (st.wYear > expire->wYear ||
        (st.wYear == expire->wYear && st.wMonth > expire->wMonth) ||
        (st.wYear == expire->wYear && st.wMonth == expire->wMonth && st.wDay >= expire->wDay))
    {
        if (ShowBetaExpDlg() == IDCANCEL)
            goto EXIT_2;
    }
#endif // USE_BETA_EXPIRATION_DATE

    // open splash screen

    GetSystemDPI(NULL);

    // if configuration doesn't exist or will be subsequently changed during file import, user is out of luck
    // and splash screen will follow default or old value
    LoadSaveToRegistryMutex.Enter();
    if (OpenKeyW(HKEY_CURRENT_USER, configKey, hSalamander))
    {
        HKEY actKey;
        if (OpenKeyW(hSalamander, SALAMANDER_CONFIG_REG, actKey))
        {
            GetValueW(actKey, CONFIG_SHOWSPLASHSCREEN_REG, REG_DWORD,
                      &Configuration.ShowSplashScreen, sizeof(DWORD));
            CloseKey(actKey);
        }
        CloseKey(hSalamander);
    }
    LoadSaveToRegistryMutex.Leave();

    if (Configuration.ShowSplashScreen)
        SplashScreenOpen();

    // configuration import window contains listview with checkboxes, we must initialize COMMON CONTROLS
    INITCOMMONCONTROLSEX initCtrls;
    initCtrls.dwSize = sizeof(INITCOMMONCONTROLSEX);
    initCtrls.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES |
                      ICC_TAB_CLASSES | ICC_COOL_CLASSES |
                      ICC_DATE_CLASSES | ICC_USEREX_CLASSES;
    if (!InitCommonControlsEx(&initCtrls))
    {
        TRACE_E("InitCommonControlsEx failed");
        SplashScreenCloseIfExist();
        goto EXIT_2;
    }

    SetWinLibStrings(LoadStrW(IDS_INVALIDNUMBER), MAINWINDOW_NAME); // j.r. - move to correct place

    // initialization of packers; previously done in constructors; now moved here,
    // when language DLL is already decided
    PackerFormatConfig.InitializeDefaultValues();
    ArchiverConfig.InitializeDefaultValues();
    PackerConfig.InitializeDefaultValues();
    UnpackerConfig.InitializeDefaultValues();

    // if file exists, it will be imported to registry
    BOOL importCfgFromFileWasSkipped = FALSE;
    ImportConfigurationW(NULL, ConfigurationNameW.c_str(), ConfigurationNameIgnoreIfNotExists, autoImportConfig,
                         &importCfgFromFileWasSkipped);

    // handle transition from old config to new

    // Call function that will try to find configuration corresponding to our program version.
    // If it succeeds, 'loadConfiguration' variable will be set and function will return
    // TRUE. If configuration doesn't exist yet, function will sequentially search old
    // configurations from 'SalamanderConfigurationRoots' array (from newest to oldest).
    // If it finds one of the configurations, it will show dialog and offer its conversion to
    // current configuration and deletion from registry. After showing the last dialog it will return
    // TRUE and set 'deleteConfigurations' and 'loadConfiguration' variables according to user's
    // choices. If user chooses to terminate application, function returns FALSE.

    // array determining configuration indices in 'SalamanderConfigurationRoots' array,
    // which should be deleted (0 -> none)
    BOOL deleteConfigurations[SALCFG_ROOTS_COUNT];
    ZeroMemory(deleteConfigurations, sizeof(deleteConfigurations));

    CALL_STACK_MESSAGE1("WinMainBody::FindLatestConfiguration");

    // pointer into 'SalamanderConfigurationRoots' array to configuration that should be
    // loaded (NULL -> none; default values will be used)
    if (autoImportConfig)
        SALAMANDER_ROOT_REG = autoImportConfigFromKey.c_str(); // during UPGRADE searching for configuration doesn't make sense
    else
    {
        if (!FindLatestConfiguration(deleteConfigurations, SALAMANDER_ROOT_REG))
        {
            SplashScreenCloseIfExist();
            goto EXIT_2;
        }
    }

    // Plugins read "Theme mode" from this key too; tell them which one it is.
    PublishConfigRootToEnvironment();

    InitializeShellib(); // OLE needs to be initialized before opening HTML help - CSalamanderEvaluation

    // if new configuration key doesn't exist yet, we'll create it before potential deletion
    // of old keys
    BOOL currentCfgDoesNotExist = autoImportConfig || SALAMANDER_ROOT_REG != SalamanderConfigurationRoots[0];
    BOOL saveNewConfig = currentCfgDoesNotExist;

    // if user doesn't want multiple instances, we'll just activate the previous one
    if (!currentCfgDoesNotExist &&
        CheckOnlyOneInstance(&cmdLineParams))
    {
        SplashScreenCloseIfExist();
        myExitCode = 0;
    EXIT_3:
        ReleaseShellib();
        goto EXIT_2;
    }

    // verify CommonControl version
    if (GetComCtlVersion(&CCVerMajor, &CCVerMinor) != S_OK) // JRYFIXME - move tests around common controls to W7+
    {
        CCVerMajor = 0; // this probably never happens - they don't have comctl32.dll
        CCVerMinor = 0;
    }

    CALL_STACK_MESSAGE1("WinMainBody::StartupDialog");

    //  StartupDialog.Open(HLanguage);

    int i;
    for (i = 0; i < NUMBER_OF_COLORS; i++)
        UserColors[i] = SalamanderColors[i];

    //--- initialization part
    CALL_STACK_MESSAGE1("WinMainBody::inicialization");
    IfExistSetSplashScreenText(LoadStrW(IDS_STARTUP_DATA));

    InitDefaultDir();
    PackSetErrorHandler(PackErrorHandler);
    InitLocales();

    if (!InitPreloadedStrings())
    {
        SplashScreenCloseIfExist();
    EXIT_4:
        ReleasePreloadedStrings();
        goto EXIT_3;
    }
    if (!InitializeCheckThread() || !InitializeFind())
    {
        SplashScreenCloseIfExist();
    EXIT_5:
        ReleaseCheckThreads();
        goto EXIT_4;
    }
    InitializeMenuWheelHook();
    SetupWinLibHelp(&SalamanderHelp);
    if (!InitializeDiskCache())
    {
        SplashScreenCloseIfExist();
    EXIT_6:
        ReleaseFind();
        goto EXIT_5;
    }
    if (!InitializeConstGraphics())
    {
        SplashScreenCloseIfExist();
    EXIT_7:
        ReleaseConstGraphics();
        goto EXIT_6;
    }
    if (!InitializeGraphics(FALSE))
    {
        SplashScreenCloseIfExist();
    EXIT_8:
        ReleaseGraphics(FALSE);
        goto EXIT_7;
    }
    if (!InitializeMenu() || !BuildSalamanderMenus())
    {
        SplashScreenCloseIfExist();
        goto EXIT_8;
    }
    if (!InitializeThread())
    {
        SplashScreenCloseIfExist();
    EXIT_9:
        TerminateThread();
        goto EXIT_8;
    }
    if (!InitializeViewer())
    {
        SplashScreenCloseIfExist();
        ReleaseViewer();
        goto EXIT_9;
    }

    // attach OLE SPY
    // moved below InitializeGraphics, which under WinXP reported leaks (probably some cache again)
    // OleSpyRegister();    // disconnected because after the Windows 2000 update from 02/2005, starting+closing
    //                       // Salamander from MSVC began to hit debug-breakpoint: Invalid Address specified to
    //                       // RtlFreeHeap( 130000, 14bc74 ) - apparently MS started calling RtlFreeHeap directly
    //                       // instead of OLE free and because of the spy info block at the start of the allocated
    //                       // block it broke (malloc returns a pointer shifted past the spy info block)
    //OleSpySetBreak(2754); // break on [nth] allocation from the dump

    // worker initialization (disk operations)
    InitWorker();

    // library initialization for communication with SalShExt/SalamExt/SalExtX86/SalExtX64.DLL (shell copy hook + shell context menu)
    InitSalShLib();

    // library initialization for working with shell icon overlays (Tortoise SVN + CVS)
    LoadIconOvrlsInfo(SALAMANDER_ROOT_REG);
    InitShellIconOverlays();

    // initialization of functions for browsing through next/prev file in panel/Find from viewer
    InitFileNamesEnumForViewers();

    // load list of shared directories
    IfExistSetSplashScreenText(LoadStrW(IDS_STARTUP_SHARES));
    Shares.Refresh();

    CMainWindow::RegisterUniversalClass(CS_DBLCLKS | CS_SAVEBITS,
                                        0,
                                        0,
                                        NULL,
                                        LoadCursor(NULL, IDC_ARROW),
                                        (HBRUSH)(COLOR_3DFACE + 1),
                                        NULL,
                                        SAVEBITS_CLASSNAME,
                                        NULL);
    CMainWindow::RegisterUniversalClass(CS_DBLCLKS | CS_SAVEBITS,
                                         0,
                                         0,
                                         NULL,
                                         LoadCursorW(NULL, MAKEINTRESOURCEW(32512)), // IDC_ARROW is bound to A without UNICODE.
                                         (HBRUSH)(COLOR_3DFACE + 1),
                                         NULL,
                                         SAFEWAIT_CLASSNAMEW,
                                         NULL);
    // CShellExecuteWnd stores its diagnostic title in UTF-16 and is
    // enumerated with GetWindowTextW. Register the class with the matching W window proc;
    // an ANSI class would round-trip that title through CP_ACP before the crash report.
    CMainWindow::RegisterUniversalClass(CS_DBLCLKS,
                                         0,
                                         0,
                                         NULL,
                                         LoadCursor(NULL, IDC_ARROW),
                                         (HBRUSH)(COLOR_3DFACE + 1),
                                         NULL,
                                         SHELLEXECUTE_CLASSNAMEW,
                                         NULL);

    Associations.ReadAssociations(FALSE); // loading associations from Registry

    // shell extensions registration
    // if we find library in "utils" subdirectory, we'll verify its registration and potentially register it
    // wide: GetModuleFileName (ANSI) narrowed Sally's own install path via CP_ACP
    // right at the source - a non-ASCII install path (Unicode user-profile name, non-English
    // portable install) could produce a '?'-collapsed path that FileExists() reports missing
    // (silently skipping registration) or, worse, one that happens to match a DIFFERENT real
    // file. SECRegisterToRegistry (shexreg.c) is narrow-only throughout, and widening its
    // registry read/write format is a bigger, separate job (deferred - the value it writes is
    // also read back narrow elsewhere, so switching the write format alone risks breaking that
    // readback for everyone, not just non-ASCII installs). Resolve the narrow representation
    // through an exact round-trip or 8.3 short-path fallback instead of blind best-fit, matching
    // the ANSI-boundary policy already established at ChangePathToArchive
    // (files_window_actions.cpp) - if neither succeeds, skip registering that DLL rather than
    // risk registering (or reporting missing) the wrong file.
    std::wstring shellExtX86PathW, shellExtX64PathW, shellExtARM64PathW;
    BOOL haveX86PathW = BuildModuleRelativePathW(HInstance, L"utils\\salextx86.dll", shellExtX86PathW);
    BOOL haveX64PathW = BuildModuleRelativePathW(HInstance, L"utils\\salextx64.dll", shellExtX64PathW);
    BOOL haveARM64PathW = BuildModuleRelativePathW(HInstance, L"utils\\salextARM64.dll", shellExtARM64PathW);
    auto resolveShellExtPath = [](const std::wstring& wide, std::wstring& outPath) -> BOOL
    {
        // SECRegisterToRegistry takes const wchar_t* directly - the ANSI
        // round-trip is a VALIDATION that this path is safe for whatever narrow registry
        // format it writes internally, not the value to store. Storing the ansi bytes here
        // (reinterpreted as wide chars) was a real bug: it would hand SECRegisterToRegistry
        // and FileExistsW a corrupted path instead of the verified wide one.
        std::string ansi;
        const std::wstring* verified = &wide;
        std::wstring shortWide;
        if (!Win32EncodeAcpExact(wide, ansi))
        {
            shortWide = GetShortPathW(wide.c_str());
            if (shortWide.empty() || !Win32EncodeAcpExact(shortWide, ansi))
                return FALSE;
            verified = &shortWide;
        }
        if (verified->empty())
            return FALSE;
        outPath = *verified;
        return TRUE;
    };
    std::wstring shellExtX86Path, shellExtX64Path, shellExtARM64Path;
    BOOL haveX86Path = haveX86PathW && resolveShellExtPath(shellExtX86PathW, shellExtX86Path);
    BOOL haveX64Path = haveX64PathW && resolveShellExtPath(shellExtX64PathW, shellExtX64Path);
    BOOL haveARM64Path = haveARM64PathW && resolveShellExtPath(shellExtARM64PathW, shellExtARM64Path);
    if (haveX86Path || haveX64Path || haveARM64Path)
    {
#if defined(_M_ARM64)
        if (haveARM64Path && FileExistsW(shellExtARM64Path.c_str()))
            SalShExtRegistered = SECRegisterToRegistry(shellExtARM64Path.c_str(), FALSE, 0);
        else
            SalShExtRegistered = FALSE;
        const std::wstring& cleanupPathW = shellExtARM64PathW;
#elif defined(_WIN64)
        BOOL x86Present = haveX86Path && FileExistsW(shellExtX86Path.c_str());
        BOOL x86Registered = FALSE;
        if (x86Present)
            x86Registered = SECRegisterToRegistry(shellExtX86Path.c_str(), TRUE, KEY_WOW64_32KEY);
        if (haveX64Path && FileExistsW(shellExtX64Path.c_str()))
        {
            SalShExtRegistered = SECRegisterToRegistry(shellExtX64Path.c_str(), FALSE, 0);
            if (x86Present) // if x86 DLL was present, both must register successfully
                SalShExtRegistered &= x86Registered;
        }
        else
            SalShExtRegistered = FALSE;
        const std::wstring& cleanupPathW = shellExtX64PathW;
#else  // _WIN64
        if (haveX86Path && FileExistsW(shellExtX86Path.c_str()))
            SalShExtRegistered = SECRegisterToRegistry(shellExtX86Path.c_str(), FALSE, 0);
        if (Windows64Bit)
        {
            if (haveX64Path && FileExistsW(shellExtX64Path.c_str()))
                SalShExtRegistered &= SECRegisterToRegistry(shellExtX64Path.c_str(), TRUE, KEY_WOW64_64KEY);
            else
                SalShExtRegistered = FALSE;
        }
        const std::wstring& cleanupPathW = Windows64Bit ? shellExtX64PathW : shellExtX86PathW;
#endif

        // #82: reclaim previous installs' salext DLLs still locked by Explorer so their folders
        // become deletable (schedules delete-on-reboot; best-effort, needs admin).
        CleanupStaleShellExtensions(cleanupPathW.c_str());
    }

    //--- creating main window
    if (CMainWindow::RegisterUniversalClass(CS_DBLCLKS | CS_OWNDC,
                                            0,
                                            0,
                                            NULL, // HIcon
                                            LoadCursor(NULL, IDC_ARROW),
                                            NULL /*(HBRUSH)(COLOR_WINDOW + 1)*/, // HBrush
                                            NULL,
                                            CFILESBOX_CLASSNAME,
                                            NULL) &&
        CMainWindow::RegisterUniversalClass(CS_DBLCLKS,
                                             0,
                                             0,
                                             HANDLES(LoadIcon(HInstance,
                                                              MAKEINTRESOURCE(IDI_SALAMANDER))),
                                             LoadCursor(NULL, IDC_ARROW),
                                             (HBRUSH)(COLOR_WINDOW + 1),
                                             NULL,
                                             CMAINWINDOW_CLASSNAME,
                                             NULL))
    {
        MainWindow = new CMainWindow;
        if (MainWindow != NULL)
        {
            MainWindow->CmdShow = cmdShow;
            if (MainWindow->Create(CMAINWINDOW_CLASSNAME,
                                    L"",
                                    WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                    CW_USEDEFAULT, 0, CW_USEDEFAULT, 0,
                                    NULL,
                                    NULL,
                                    HInstance,
                                    MainWindow))
            {
                SetMessagesParent(MainWindow->HWindow);
                PluginMsgBoxParent = MainWindow->HWindow;

                // extract Group Policy from registry
                IfExistSetSplashScreenText(LoadStrW(IDS_STARTUP_POLICY));
                SystemPolicies.LoadFromRegistry();

                CALL_STACK_MESSAGE1("WinMainBody::load_config");
                BOOL setActivePanelAndPanelPaths = FALSE; // active panel + paths in panels are set in MainWindow->LoadConfig()
                if (!MainWindow->LoadConfig(currentCfgDoesNotExist, !importCfgFromFileWasSkipped ? &cmdLineParams : NULL))
                {
                    setActivePanelAndPanelPaths = TRUE;
                    UpdateDefaultColors(CurrentColors, MainWindow->HighlightMasks, FALSE, TRUE);
                    Plugins.CheckData();
                    MainWindow->InsertMenuBand();
                    if (Configuration.TopToolBarVisible)
                        MainWindow->ToggleTopToolBar();
                    if (Configuration.DriveBarVisible)
                        MainWindow->ToggleDriveBar(Configuration.DriveBar2Visible, FALSE);
                    if (Configuration.PluginsBarVisible)
                        MainWindow->TogglePluginsBar();
                    if (Configuration.MiddleToolBarVisible)
                        MainWindow->ToggleMiddleToolBar();
                    if (Configuration.BottomToolBarVisible)
                        MainWindow->ToggleBottomToolBar();
                    MainWindow->CreateAndInsertWorkerBand(); // finally insert worker
                    MainWindow->LeftPanel->UpdateDriveIcon(TRUE);
                    MainWindow->RightPanel->UpdateDriveIcon(TRUE);
                    MainWindow->LeftPanel->UpdateFilterSymbol();
                    MainWindow->RightPanel->UpdateFilterSymbol();
                    if (!SystemPolicies.GetNoRun())
                        SendMessage(MainWindow->HWindow, WM_COMMAND, CM_TOGGLEEDITLINE, TRUE);
                    MainWindow->SetWindowIcon();
                    MainWindow->SetWindowTitle();
                    SplashScreenCloseIfExist();
                    ShowWindow(MainWindow->HWindow, cmdShow);
                    UpdateWindow(MainWindow->HWindow);
                    MainWindow->RefreshDirs();
                    MainWindow->FocusLeftPanel();
                }

                if (Configuration.ReloadEnvVariables)
                    InitEnvironmentVariablesDifferences();

                if (newSLGFile)
                {
                    Plugins.ClearLastSLGNames(); // so that potentially new alternative language selection can occur for all plugins
                    Configuration.ShowSLGIncomplete = TRUE;
                }

                MainMenu.SetSkillLevel(CfgSkillLevelToMenu(Configuration.SkillLevel));

                if (!MainWindow->IsGood())
                {
                    SetMessagesParent(NULL);
                    DestroyWindow(MainWindow->HWindow);
                    TRACE_E(LOW_MEMORY);
                }
                else
                {
                    if (!importCfgFromFileWasSkipped) // only if software doesn't immediately exit (then it doesn't make sense)
                        MainWindow->ApplyCommandLineParams(&cmdLineParams, setActivePanelAndPanelPaths);

                    if (Windows7AndLater)
                        CreateJumpList();

                    IdleRefreshStates = TRUE;  // at next Idle we'll force check of state variables
                    IdleCheckClipboard = TRUE; // let clipboard be checked too

                    AccelTable1 = HANDLES(LoadAccelerators(HInstance, MAKEINTRESOURCE(IDA_MAINACCELS1)));
                    AccelTable2 = HANDLES(LoadAccelerators(HInstance, MAKEINTRESOURCE(IDA_MAINACCELS2)));

                    MainWindow->CanClose = TRUE; // only now we allow closing main window
                    // so that files don't pop up gradually (as their icons are loading)
                    UpdateWindow(MainWindow->HWindow);

                    BOOL doNotDeleteImportedCfg = FALSE;
                    if (autoImportConfig && // find out if new version doesn't have fewer plugins than old one and thus part of old configuration won't be transferred
                        FindPluginsWithoutImportedCfg(&doNotDeleteImportedCfg))
                    {                               // software exit without saving configuration is needed
                        SALAMANDER_ROOT_REG = NULL; // this should reliably prevent writing to configuration in registry
                        PublishConfigRootToEnvironment();
                        PostMessage(MainWindow->HWindow, WM_USER_FORCECLOSE_MAINWND, 0, 0);
                    }
                    else
                    {
                        if (Configuration.ConfigVersion < THIS_CONFIG_VERSION
#ifndef _WIN64 // FIXME_X64_WINSCP
                            || Configuration.AddX86OnlyPlugins
#endif // _WIN64
                        )
                        {                                            // auto-install plugins from standard plugin subdirectory "plugins"
#ifndef _WIN64                                                       // FIXME_X64_WINSCP
                            Configuration.AddX86OnlyPlugins = FALSE; // once is enough
#endif                                                               // _WIN64
                            Plugins.AutoInstallStdPluginsDir(MainWindow->HWindow);
                            Configuration.LastPluginVer = 0;   // when transitioning to new version, plugins.ver file will be deleted
                            Configuration.LastPluginVerOP = 0; // when transitioning to new version, plugins.ver file will be deleted for second platform too
                            saveNewConfig = TRUE;              // new configuration must be saved (so this doesn't repeat on next launch)
                        }
                        // loading plugins.ver file ((re)installation of plugins), necessary even the first time (in case
                        // of plugin installation before first Salamander launch)
                        if (Plugins.ReadPluginsVer(MainWindow->HWindow, Configuration.ConfigVersion < THIS_CONFIG_VERSION))
                            saveNewConfig = TRUE; // new configuration must be saved (so this doesn't repeat on next launch)
                        // load plugins that have load-on-start flag set
                        Plugins.HandleLoadOnStartFlag(MainWindow->HWindow);
                        // if we're starting for the first time with changed language, we'll load all plugins to show
                        // if they have this language version + potentially let user choose alternative languages
                        if (langChanged)
                            Plugins.LoadAll(MainWindow->HWindow);

                        // FTP and WinSCP plugins now call SalamanderGeneral->SetPluginUsesPasswordManager() to subscribe to password manager events
                        // introduced with configuration version 45 -- let all plugins have chance to subscribe
                        if (Configuration.ConfigVersion < 45) // password manager introduction
                            Plugins.LoadAll(MainWindow->HWindow);

                        // save will now go to newest key
                        SALAMANDER_ROOT_REG = SalamanderConfigurationRoots[0];
                        PublishConfigRootToEnvironment();
                        // we'll save configuration immediately, while it's still clean conversion of old version -- user may
                        // have "Save Cfg on Exit" disabled and if they change something during Salamander operation, they don't want to save it at the end
                        if (saveNewConfig)
                        {
                            MainWindow->SaveConfig();
                        }
                        // browse array and if any root is marked for deletion, delete it + delete old configuration
                        // after UPGRADE and also delete "AutoImportConfig" value in this Salamander version's configuration key
                        MainWindow->DeleteOldConfigurations(deleteConfigurations, autoImportConfig, autoImportConfigFromKey.c_str(),
                                                            doNotDeleteImportedCfg);

                        // only first Salamander instance: let's see if TEMP needs cleaning
                        // of unnecessary disk-cache files (during crash or lock by another application
                        // files in TEMP can remain)
                        // we must test on global (across all sessions) variable, so that two
                        // Salamander instances launched under FastUserSwitching can see each other
                        // Problem reported on forum: https://forum.altap.cz/viewtopic.php?t=2643
                        if (FirstInstance_3_or_later)
                        {
                            DiskCache.ClearTEMPIfNeeded(MainWindow->HWindow, MainWindow->GetActivePanelHWND());
                        }

                        if (importCfgFromFileWasSkipped) // if we skipped config.reg or other .reg file import (parameter -C)
                        {                                // inform user about need for new Salamander start and let them exit the software
                            gPrompter->ShowInfo(SALAMANDER_TEXT_VERSIONW(), LoadStrW(IDS_IMPORTCFGFROMFILESKIPPED));
                            PostMessage(MainWindow->HWindow, WM_USER_FORCECLOSE_MAINWND, 0, 0);
                        }
                        /*
            // if needed, we'll trigger Tip of the Day dialog display
            // 0xffffffff = open quiet - if it doesn't work out, we won't bother the user
            if (Configuration.ShowTipOfTheDay)
              PostMessage(MainWindow->HWindow, WM_COMMAND, CM_HELP_TIP, 0xffffffff);
  */
                    }

                    // from now on closed paths will be remembered
                    MainWindow->CanAddToDirHistory = TRUE;

                    // users want to have start-up path in history even if they didn't dirty it
                    MainWindow->LeftPanel->UserWorkedOnThisPath = TRUE;
                    MainWindow->RightPanel->UserWorkedOnThisPath = TRUE;

                    // let process list know that we're running and have main window (it's possible to activate us with OnlyOneInstance)
                    TaskList.SetProcessState(PROCESS_STATE_RUNNING, MainWindow->HWindow);

                    // ask Salmon to check if there are old bug reports on disk that need to be sent
                    SalmonCheckBugs();

                    if (IsSLGIncomplete[0] != 0 && Configuration.ShowSLGIncomplete)
                        PostMessage(MainWindow->HWindow, WM_USER_SLGINCOMPLETE, 0, 0);

                    //--- application loop
                    CALL_STACK_MESSAGE1("WinMainBody::message_loop");
                    DWORD activateParamsRequestUID = 0;
                    BOOL skipMenuBar;
                    MSG msg;
                    BOOL haveMSG = FALSE; // FALSE if GetMessageW() should be called in loop condition
                    while (haveMSG || GetMessageW(&msg, NULL, 0, 0))
                    {
                        haveMSG = FALSE;
                        if (msg.message != WM_USER_SHOWWINDOW && msg.message != WM_USER_WAKEUP_FROM_IDLE && /*msg.message != WM_USER_SETPATHS &&*/
                            msg.message != WM_QUERYENDSESSION && msg.message != WM_USER_SALSHEXT_PASTE &&
                            msg.message != WM_USER_CLOSE_MAINWND && msg.message != WM_USER_FORCECLOSE_MAINWND)
                        { // except for "connect", "shutdown", "do-paste" and "close-main-wnd" messages, all others begin BUSY mode
                            SalamanderBusy = TRUE;
                            LastSalamanderIdleTime = GetTickCount();
                        }

                        if ((msg.message == WM_SYSKEYDOWN || msg.message == WM_KEYDOWN) &&
                            msg.wParam != VK_MENU && msg.wParam != VK_CONTROL && msg.wParam != VK_SHIFT)
                        {
                            SetCurrentToolTip(NULL, 0); // hide the tooltip
                        }

                        skipMenuBar = FALSE;
                        if (Configuration.QuickSearchEnterAlt && msg.message == WM_SYSCHAR)
                            skipMenuBar = TRUE;

                        // ensure messages are sent to our menu (bypassing the need for a keyboard hook)
                        if (MainWindow == NULL || MainWindow->MenuBar == NULL || !MainWindow->CaptionIsActive ||
                            MainWindow->QuickRenameWindowActive() ||
                            skipMenuBar || GetCapture() != NULL || // if the mouse is captured, we could cause visual issues
                            !MainWindow->MenuBar->IsMenuBarMessage(&msg))
                        {
                            CWindowsObject* wnd = WindowsManager.GetWindowPtr(GetActiveWindow());

                            // Bottom Toolbar - change text based on VK_CTRL, VK_MENU and VK_SHIFT
                            if ((msg.message == WM_SYSKEYDOWN || msg.message == WM_KEYDOWN ||
                                 msg.message == WM_SYSKEYUP || msg.message == WM_KEYUP) &&
                                MainWindow != NULL)
                                MainWindow->UpdateBottomToolBar();

                            if ((wnd == NULL || !wnd->Is(otDialog) ||
                                 !IsDialogMessage(wnd->HWindow, &msg)) &&
                                (MainWindow == NULL || !MainWindow->CaptionIsActive || // added "!MainWindow->CaptionIsActive" so accelerators are not translated in modeless plugin windows (F7 in "FTP Logs" is not great)
                                 MainWindow->QuickRenameWindowActive() ||
                                 !TranslateAccelerator(MainWindow->HWindow, AccelTable1, &msg) &&
                                     (MainWindow->EditMode || !TranslateAccelerator(MainWindow->HWindow, AccelTable2, &msg))))
                            {
                                TranslateMessage(&msg);
                                DispatchMessageW(&msg);
                            }
                        }

                        if (MainWindow != NULL && MainWindow->CanClose)
                        { // if Salamander is started, we can mark it as NOT BUSY
                            SalamanderBusy = FALSE;
                        }

                    TEST_IDLE:
                        if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
                        {
                            if (msg.message == WM_QUIT)
                                break;      // equivalent to GetMessageW() returning FALSE
                            haveMSG = TRUE; // we have a message, process it (without calling GetMessageW())
                        }
                        else // if there is no message in the queue, perform idle processing
                        {
#ifdef _DEBUG
                            // once every three seconds check heap consistency
                            if (_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) & _CRTDBG_ALLOC_MEM_DF)
                            {
                                if (GetTickCount() - LastCrtCheckMemoryTime > 3000) // every three seconds
                                {
                                    if (!_CrtCheckMemory())
                                    {
                                        HWND hParent = NULL;
                                        if (MainWindow != NULL)
                                            hParent = MainWindow->HWindow;
                                        MessageBoxW(hParent, L"_CrtCheckMemory failed. Look to the Trace Server for details.", L"Sally", MB_OK | MB_ICONERROR);
                                    }
                                    LastCrtCheckMemoryTime = GetTickCount();
                                }
                            }
#endif //_DEBUG

                            if (MainWindow != NULL)
                            {
                                CannotCloseSalMainWnd = TRUE; // prevent closing the main Salamander window while running the following routines
                                MainWindow->OnEnterIdle();

                                // wait for ESC release only if a panel listing refresh directly follows
                                // (which does not happen during IDLE)
                                if (WaitForESCReleaseBeforeTestingESC)
                                    WaitForESCReleaseBeforeTestingESC = FALSE;

                                // Check whether another "OnlyOneInstance" Sally asks us to
                                // activate and set panel paths. The task-list adapter owns the
                                // synchronized dynamic handoff and acknowledges only after this
                                // thread has copied a fresh request.
                                if (!SalamanderBusy)
                                {
                                    sally::cmdline::CommandLineRequest paramsCopy;
                                    if (MainWindow != NULL &&
                                        TaskList.TakePendingActivationRequest(
                                            activateParamsRequestUID, paramsCopy))
                                    {
                                        activateParamsRequestUID = paramsCopy.requestUID;
                                        SendMessage(MainWindow->HWindow, WM_USER_SHOWWINDOW, 0, 0);
                                        MainWindow->ApplyCommandLineParams(&paramsCopy);
                                    }
                                }

                                // ensure escape from removed drives to a fixed drive (after ejecting a device - USB flash disk, etc.)
                                if (!SalamanderBusy && ChangeLeftPanelToFixedWhenIdle)
                                {
                                    ChangeLeftPanelToFixedWhenIdle = FALSE;
                                    ChangeLeftPanelToFixedWhenIdleInProgress = TRUE;
                                    if (MainWindow != NULL && MainWindow->LeftPanel != NULL)
                                        MainWindow->LeftPanel->ChangeToRescuePathOrFixedDrive(MainWindow->LeftPanel->HWindow);
                                    ChangeLeftPanelToFixedWhenIdleInProgress = FALSE;
                                }
                                if (!SalamanderBusy && ChangeRightPanelToFixedWhenIdle)
                                {
                                    ChangeRightPanelToFixedWhenIdle = FALSE;
                                    ChangeRightPanelToFixedWhenIdleInProgress = TRUE;
                                    if (MainWindow != NULL && MainWindow->RightPanel != NULL)
                                        MainWindow->RightPanel->ChangeToRescuePathOrFixedDrive(MainWindow->RightPanel->HWindow);
                                    ChangeRightPanelToFixedWhenIdleInProgress = FALSE;
                                }
                                if (!SalamanderBusy && OpenCfgToChangeIfPathIsInaccessibleGoTo)
                                {
                                    OpenCfgToChangeIfPathIsInaccessibleGoTo = FALSE;
                                    if (MainWindow != NULL)
                                        PostMessage(MainWindow->HWindow, WM_USER_CONFIGURATION, 6, 0);
                                }

                                // if a plug-in requested unload or menu rebuild, perform it... (only when not "busy")
                                if (!SalamanderBusy && ExecCmdsOrUnloadMarkedPlugins)
                                {
                                    int cmd;
                                    CPluginData* data;
                                    Plugins.GetCmdAndUnloadMarkedPlugins(MainWindow->HWindow, &cmd, &data);
                                    ExecCmdsOrUnloadMarkedPlugins = (cmd != -1);
                                    if (cmd >= 0 && cmd < 500) // execute a Salamander command at the plug-in's request
                                    {
                                        int wmCmd = GetWMCommandFromSalCmd(cmd);
                                        if (wmCmd != -1)
                                        {
                                            // generate WM_COMMAND and process it immediately
                                            msg.hwnd = MainWindow->HWindow;
                                            msg.message = WM_COMMAND;
                                            msg.wParam = (DWORD)LOWORD(wmCmd); // truncate the high WORD (0 - cmd from menu)
                                            msg.lParam = 0;
                                            msg.time = GetTickCount();
                                            GetCursorPos(&msg.pt);

                                            haveMSG = TRUE; // we have a message, process it (without calling GetMessageW())
                                        }
                                    }
                                    else
                                    {
                                        if (cmd >= 500 && cmd < 1000500) // execute menuExt command at the plug-in's request
                                        {
                                            int id = cmd - 500;
                                            SalamanderBusy = TRUE; // about to execute a menu command - we are "busy" again
                                            LastSalamanderIdleTime = GetTickCount();
                                            if (data != NULL && data->GetLoaded())
                                            {
                                                if (data->GetPluginInterfaceForMenuExt()->NotEmpty())
                                                {
                                                    CALL_STACK_MESSAGE4("CPluginInterfaceForMenuExt::ExecuteMenuItem(, , %d,) (%ls v. %ls)",
                                                                        id, data->DLLName.c_str(), data->Version.c_str());

                                                    // lower thread priority to "normal" (so operations do not overload the machine)
                                                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

                                                    CSalamanderForOperations sm(MainWindow->GetActivePanel());
                                                    data->GetPluginInterfaceForMenuExt()->ExecuteMenuItem(&sm, MainWindow->HWindow, id, 0);

                                                    // raise thread priority again, operation completed
                                                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                                                }
                                                else
                                                {
                                                    TRACE_E("Plugin must have PluginInterfaceForMenuExt when "
                                                            "calling CSalamanderGeneral::PostMenuExtCommand()!");
                                                }
                                            }
                                            else
                                            {
                                                // it does not need to be loaded; it is enough that PostMenuExtCommand is called from the
                                                // Release plug-in, which is invoked by a posted unload
                                                // TRACE_E("Unexpected situation during call of menu command in \"sal-idle\".");
                                            }
                                            if (MainWindow != NULL && MainWindow->CanClose) // end of menu command execution
                                            {                                               // if Salamander is started, we can mark it as NOT BUSY
                                                SalamanderBusy = FALSE;
                                            }
                                            CannotCloseSalMainWnd = FALSE;
                                            goto TEST_IDLE; // try "idle" again (e.g. to process another posted command/unload)
                                        }
                                    }
                                }
                                if (!SalamanderBusy && OpenPackOrUnpackDlgForMarkedPlugins)
                                {
                                    CPluginData* data;
                                    int pluginIndex;
                                    Plugins.OpenPackOrUnpackDlgForMarkedPlugins(&data, &pluginIndex);
                                    OpenPackOrUnpackDlgForMarkedPlugins = (data != NULL);
                                    if (data != NULL) // open Pack/Unpack dialog at the plug-in's request
                                    {
                                        SalamanderBusy = TRUE; // about to execute a menu command - we are "busy" again
                                        LastSalamanderIdleTime = GetTickCount();
                                        if (data->OpenPackDlg)
                                        {
                                            CFilesWindow* activePanel = MainWindow->GetActivePanel();
                                            if (activePanel != NULL && activePanel->Is(ptDisk))
                                            { // open Pack dialog
                                                MainWindow->CancelPanelsUI();
                                                activePanel->UserWorkedOnThisPath = TRUE;
                                                activePanel->StoreSelection(); // store selection for Restore Selection command
                                                activePanel->Pack(MainWindow->GetNonActivePanel(), pluginIndex,
                                                                  data->Name.c_str(), data->PackDlgDelFilesAfterPacking);
                                            }
                                            else
                                                TRACE_E("Unexpected situation: type of active panel is not Disk!");
                                            data->OpenPackDlg = FALSE;
                                            data->PackDlgDelFilesAfterPacking = 0;
                                        }
                                        else
                                        {
                                            if (data->OpenUnpackDlg)
                                            {
                                                CFilesWindow* activePanel = MainWindow->GetActivePanel();
                                                if (activePanel != NULL && activePanel->Is(ptDisk))
                                                { // open Unpack dialog
                                                    MainWindow->CancelPanelsUI();
                                                    activePanel->UserWorkedOnThisPath = TRUE;
                                                    activePanel->StoreSelection(); // store selection for Restore Selection command
                                                    activePanel->Unpack(MainWindow->GetNonActivePanel(), pluginIndex,
                                                                        data->Name.c_str(), data->UnpackDlgUnpackMask.empty() ? NULL : data->UnpackDlgUnpackMask.c_str());
                                                }
                                                else
                                                    TRACE_E("Unexpected situation: type of active panel is not Disk!");
                                                data->OpenUnpackDlg = FALSE;
                                                data->UnpackDlgUnpackMask.clear();
                                            }
                                        }
                                        if (MainWindow != NULL && MainWindow->CanClose) // end of opening Pack/Unpack dialog
                                        {                                               // if Salamander is started, we can mark it as NOT BUSY
                                            SalamanderBusy = FALSE;
                                        }
                                        CannotCloseSalMainWnd = FALSE;
                                        goto TEST_IDLE; // try "idle" again (e.g. to process another posted command/unload/Pack/Unpack)
                                    }
                                }
                                if (!SalamanderBusy && !OpenReadmeInNotepad.empty())
                                { // start notepad with file 'OpenReadmeInNotepad' for installer on Vista+
                                    StartNotepadW(OpenReadmeInNotepad.c_str());
                                    OpenReadmeInNotepad.clear();
                                }
                                CannotCloseSalMainWnd = FALSE;
                            }
                        }
                    }
                }
                PluginMsgBoxParent = NULL;
            }
            else
            {
                TRACE_E(LOW_MEMORY);
            }
        }
    }
    else
        TRACE_E("Unable to register main window class.");

    // in case of error, try to close the dialog
    SplashScreenCloseIfExist();

    // restore thread priority to the original state
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

    //--- give all windows 1 second to close, then let them detach
    int timeOut = 10;
    int winsCount = WindowsManager.GetCount();
    while (timeOut-- && winsCount > 0)
    {
        Sleep(100);
        int c = WindowsManager.GetCount();
        if (winsCount > c) // windows are still decreasing, wait at least one more second
        {
            winsCount = c;
            timeOut = 10;
        }
    }

//--- info
#ifdef __DEBUG_WINLIB
    TRACE_I("WindowsManager: " << WindowsManager.maxWndCount << " windows, " << WindowsManager.search << " searches, " << WindowsManager.cache << " cached searches.");
#endif
    //---
    DestroySafeWaitWindow(TRUE); // "terminate" command for the safe-wait-message thread
    Sleep(1000);                 // give all viewer threads time to finish
    NBWNetAC3Thread.Close(TRUE); // kill the running thread (move to AuxThreads), block further actions
    TerminateAuxThreads();       // terminate the rest forcibly
                                 //---
    TerminateThread();
    ReleaseFileNamesEnumForViewers();
    ReleaseShellIconOverlays();
    ReleaseSalShLib();
    ReleaseWorker();
    ReleaseViewer();
    ReleaseWinLib();
    ReleaseMenuWheelHook();
    ReleaseFind();
    ReleaseCheckThreads();
    ReleasePreloadedStrings();
    ReleaseShellib();
    ReleaseGraphics(FALSE);
    ReleaseConstGraphics();

    HANDLES(FreeLibrary(HLanguage));
    HLanguage = NULL;

    if (NtDLL != NULL)
    {
        HANDLES(FreeLibrary(NtDLL));
        NtDLL = NULL;
    }
    if (User32DLL != NULL)
    {
        NOHANDLES(FreeLibrary(User32DLL));
        User32DLL = NULL;
    }

    //OleSpyStressTest(); // multi-threaded stress test
    // OleSpyRevoke();     // detach OLESPY
    OleUninitialize(); // deinitialize OLE
    // OleSpyDump();       // dump leaks

    // Release plugin data before global destructors run, so the heap leak checker
    // (C__GCHeapInit destructor) doesn't report std::string/std::vector heap allocations
    // from CPluginData objects still alive in the global Plugins array.
    Plugins.ReleaseData();

    // Same reason: TaskList is a compiler-group global, destroyed after the leak checker has
    // already taken its final checkpoint, so its activation-request owner and the debug container
    // proxies of that request's members would be reported as leaks on every debug exit.
    TaskList.ReleasePendingRequest();

    TRACE_I("End");
    return 0;
}

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR cmdLine, int cmdShow)
{
#ifndef CALLSTK_DISABLE
    __try
    {
#endif // CALLSTK_DISABLE

        //#ifdef MSVC_RUNTIME_CHECKS
        _RTC_SetErrorFuncW(&MyRTCErrorFunc);
        //#endif // MSVC_RUNTIME_CHECKS

        int result = WinMainBody(hInstance, hPrevInstance, cmdLine, cmdShow);

        return result;
#ifndef CALLSTK_DISABLE
    }
    __except (CCallStack::HandleException(GetExceptionInformation()))
    {
        TRACE_I("Thread Main: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // harder exit (this one still calls something)
        return 1;
    }
#endif // CALLSTK_DISABLE
}

// ****************************************************************************
// Wide common-prefix and prefix test.
//
// The narrow forms compare with `LowerCase[]`, the 256-entry CP_ACP table, so
// two path components differing only outside the active code page compare EQUAL
// and a longer common prefix is reported than actually exists. On a path the
// code page cannot represent the comparison is meaningless entirely.
//
// Folding is per character (see sally::unicode::FoldCharW) because this is a
// positional walk, not a string comparison - CompareFolded cannot report WHERE
// two paths diverge.

int CommonPrefixLength(const wchar_t* path1, const wchar_t* path2)
{
    const wchar_t* lastBackslash = path1;
    int backslashCount = 0;
    const wchar_t* s1 = path1;
    const wchar_t* s2 = path2;
    while (*s1 != 0 && *s2 != 0 &&
           sally::unicode::FoldCharW(*s1) == sally::unicode::FoldCharW(*s2))
    {
        if (*s1 == L'\\')
        {
            lastBackslash = s1;
            backslashCount++;
        }
        s1++;
        s2++;
    }

    if (s1 - path1 < 3)
        return 0;

    if (*s1 == 0 && *s2 == L'\\' || *s1 == L'\\' && *s2 == 0 ||
        *s1 == 0 && *s2 == 0 && *(s1 - 1) != L'\\')
    {
        lastBackslash = s1; // this terminator won't be in lastBackslash
        backslashCount++;
    }

    if (path1[1] == L':')
    {
        // classic path
        if (path1[2] != L'\\')
            return 0;

        // handle special case: for root path we must return length including the last backslash
        if (lastBackslash - path1 < 3)
            return 3;

        return (int)(lastBackslash - path1);
    }
    else
    {
        // UNC path
        if (path1[0] != L'\\' || path1[1] != L'\\')
            return 0;
        if (backslashCount < 4) // path must have form "\machine\share"
            return 0;

        return (int)(lastBackslash - path1);
    }
}

BOOL SalPathIsPrefix(const wchar_t* prefix, const wchar_t* path)
{
    int commonLen = CommonPrefixLength(prefix, path);
    if (commonLen == 0)
        return FALSE;

    int prefixLen = (int)wcslen(prefix);
    if (prefixLen < 3)
        return FALSE;

    // CommonPrefixLength returned length without the last backslash (unless it was a root path)
    // if our prefix has trailing backslash, we must discard it
    if (prefixLen > 3 && prefix[prefixLen - 1] == L'\\')
        prefixLen--;

    return (commonLen == prefixLen);
}
