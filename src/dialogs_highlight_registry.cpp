// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "common/ColumnWidthInput.h"

#include "ui/IPrompter.h"
#include "common/unicode/helpers.h"
#include "common/BuiltinLanguages.h"
#include "common/DialogFont.h"
#include "common/configuration/Win32ConfigurationDraft.h"
#include "common/configuration/ConfigurationLayout.h"
#include "common/update/UpdateSchedule.h"
#include "mainwnd.h"
#include "usermenu.h"
#include "edtlbwnd.h"
#include "cfgdlg.h"
#include "dialogs.h"
#include "sal_colors.h"
#include "plugins.h"
#include "zip.h"
#include "pack.h"
#include "fileswnd.h"
#include "shellib.h"
#include "editwnd.h"
#include "codetbl.h"
#include "execute.h"
#include "viewer.h"
#include "find.h"
#include "gui.h"
#include "darkmode.h"

// Keep the message explicit at this local helper boundary.
#define ListView_SetItemTextW(hwndLV, i, iSubItem_, pszText_) \
    {                                                         \
        LV_ITEMW _ms_lvi;                                     \
        _ms_lvi.iSubItem = iSubItem_;                         \
        _ms_lvi.pszText = pszText_;                           \
        SNDMSG((hwndLV), LVM_SETITEMTEXTW, (WPARAM)(i), (LPARAM)(LV_ITEM*)&_ms_lvi); \
    }

static void IndexColorSearchMetadata(sally::configuration::SearchIndex& index, int page);

//****************************************************************************
//
// CHighlightMasksItem
//

CHighlightMasksItem::CHighlightMasksItem()
{
    CALL_STACK_MESSAGE1("CHighlightMasksItem()");
    Masks = NULL;
    Attr = 0;
    ValidAttr = 0;
    NormalFg = RGBF(0, 0, 0, SCF_DEFAULT);
    NormalBk = RGBF(0, 0, 0, SCF_DEFAULT);
    FocusedFg = RGBF(0, 0, 0, SCF_DEFAULT);
    FocusedBk = RGBF(0, 0, 0, SCF_DEFAULT);
    SelectedFg = RGBF(0, 0, 0, SCF_DEFAULT);
    SelectedBk = RGBF(0, 0, 0, SCF_DEFAULT);
    FocSelFg = RGBF(0, 0, 0, SCF_DEFAULT);
    FocSelBk = RGBF(0, 0, 0, SCF_DEFAULT);
    HighlightFg = RGBF(0, 0, 0, SCF_DEFAULT);
    HighlightBk = RGBF(0, 0, 0, SCF_DEFAULT);
    Set(L"");
}

CHighlightMasksItem::CHighlightMasksItem(CHighlightMasksItem& item)
{
    CALL_STACK_MESSAGE1("CHighlightMasksItem(&)");
    Masks = NULL;
    Attr = item.Attr;
    ValidAttr = item.ValidAttr;
    NormalFg = item.NormalFg;
    NormalBk = item.NormalBk;
    FocusedFg = item.FocusedFg;
    FocusedBk = item.FocusedBk;
    SelectedFg = item.SelectedFg;
    SelectedBk = item.SelectedBk;
    FocSelFg = item.FocSelFg;
    FocSelBk = item.FocSelBk;
    HighlightFg = item.HighlightFg;
    HighlightBk = item.HighlightBk;
    Set(item.Masks->GetMasksString());
}

CHighlightMasksItem::~CHighlightMasksItem()
{
    if (Masks != NULL)
        delete Masks;
}

BOOL CHighlightMasksItem::Set(const wchar_t* masks)
{
    if (Masks == NULL)
        Masks = new CMaskGroup;
    if (Masks == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }
    Masks->SetMasksString(masks);
    return TRUE;
}

BOOL CHighlightMasksItem::IsGood()
{
    return Masks != NULL;
}

BOOL CHighlightMasks::Load(CHighlightMasks& source)
{
    CALL_STACK_MESSAGE1("CHighlightMasks::Load()");
    CHighlightMasksItem* item;
    DestroyMembers();
    int i;
    for (i = 0; i < source.Count; i++)
    {
        item = new CHighlightMasksItem(*source[i]);
        if (!item->IsGood())
        {
            delete item;
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
        Add(item);
        if (!IsGood())
        {
            delete item;
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
    }
    return TRUE;
}

//****************************************************************************
//
// ValidatePathIsNotEmpty
//
// returns FALSE when the path is empty, otherwise TRUE (the path contains 'something')

BOOL ValidatePathIsNotEmpty(HWND hParent, const wchar_t* path)
{
    const wchar_t* iterator = path;
    BOOL empty = TRUE;
    while (*iterator != 0)
    {
        if (*iterator != ' ')
        {
            empty = FALSE;
            break;
        }
        else
            iterator++;
    }

    // an empty string is not allowed
    if (empty)
    {
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_THEPATHISINVALID));
        return FALSE;
    }
    /* originally this function was called ValidateCommandFile and checked file existence, but we abandoned that idea (maybe because of network issues?)
  if (testFileExist)
  {
    // try to access the file
    if (!FileExists(myPath))
    {
      // the file was not found - it might not exist or the path may be unreachable;
      // let the user force it
      wchar_t buff[1000];
      sprintf(buff, LoadStr(IDS_THECOMMANDISINVALID), myPath, itemName);
      int ret = SalMessageBoxW(hParent, buff, LoadStrW(IDS_ERRORTITLE),
                              MB_YESNO | MB_ICONQUESTION);
      if (ret == IDYES)
        return FALSE;    // the user wants to modify the item
    }
  }
*/
    return TRUE;
}

//
// ****************************************************************************
// CLoadSaveToRegistryMutex
//

CLoadSaveToRegistryMutex::CLoadSaveToRegistryMutex()
{
    Mutex = NULL;
    DebugCheck = 0;
}

CLoadSaveToRegistryMutex::~CLoadSaveToRegistryMutex()
{
    if (DebugCheck != 0)
        TRACE_E("CLoadSaveToRegistryMutex(): fatal error: incorrect use of mutex for Load/Save to Registry: " << DebugCheck);
    if (Mutex != NULL)
        HANDLES(CloseHandle(Mutex));
    Mutex = NULL;
}

extern const wchar_t* LOADSAVE_REGISTRY_MUTEX_NAME;

void CLoadSaveToRegistryMutex::Init()
{
    // 2.52b1: adding support for FastUserSwitching/Terminal Services
    // Up to this version the mutex was created in the local namespace under the name SalamanderLoadSaveToRegistryMutex.
    // Now we want to provide interoperability across all sessions, so we place it in the Global namespace.
    // We also append the SID (from W2K on) so that mutexes of Salamander instances running under different users
    // do not collide -- they work with their own Registry trees, no synchronization needed there.
    // We could add the Salamander version into the mutex name as well (every version has its own Registry tree).
    // But new versions can remove obsolete configurations, so we won't do it.
    LPWSTR sid = NULL;
    if (!GetStringSid(&sid))
        sid = NULL;

    wchar_t buff[1000];
    if (sid == NULL)
    {
        // failed to obtain SID -- continue in fallback mode
        swprintf_s(buff, _countof(buff), L"%s", LOADSAVE_REGISTRY_MUTEX_NAME);
    }
    else
    {
        swprintf_s(buff, _countof(buff), L"Global\\%s_%s", LOADSAVE_REGISTRY_MUTEX_NAME, sid);
        LocalFree(sid);
    }

    PSID psidEveryone;
    PACL paclNewDacl;
    SECURITY_ATTRIBUTES sa;
    SECURITY_DESCRIPTOR sd;
    SECURITY_ATTRIBUTES* saPtr = CreateAccessableSecurityAttributes(&sa, &sd, SYNCHRONIZE /*| MUTEX_MODIFY_STATE*/, &psidEveryone, &paclNewDacl);

    Mutex = HANDLES_Q(CreateMutexW(saPtr, FALSE, buff));
    if (Mutex == NULL)
    {
        Mutex = HANDLES_Q(OpenMutexW(SYNCHRONIZE, FALSE, buff));
        if (Mutex == NULL)
        {
            DWORD err = GetLastError();
            TRACE_IW(L"CLoadSaveToRegistryMutex::Init(): Unable to create/open mutex for Load/Save to Registry synchronization! Error: " << GetErrorTextOwned(err).c_str());
        }
    }

    if (psidEveryone != NULL)
        FreeSid(psidEveryone);
    if (paclNewDacl != NULL)
        LocalFree(paclNewDacl);

    DebugCheck = 0;
}

void CLoadSaveToRegistryMutex::Enter()
{
    if (!TryEnter())
    {
        ++DebugCheck; // legacy callers still pair Leave() after a failed acquisition
        TRACE_E("CLoadSaveToRegistryMutex::Enter(): mutex acquisition failed!");
    }
}

BOOL CLoadSaveToRegistryMutex::TryEnter()
{
    if (Mutex == NULL)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    const DWORD result = WaitForSingleObject(Mutex, INFINITE);
    if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
        return FALSE;
    ++DebugCheck;
    return TRUE;
}

void CLoadSaveToRegistryMutex::Leave()
{
    DebugCheck--;
    if (Mutex != NULL)
    {
        if (!ReleaseMutex(Mutex))
            TRACE_E("CLoadSaveToRegistryMutex::Enter(): ReleaseMutex() failed!");
    }
    else
        TRACE_E("CLoadSaveToRegistryMutex::Leave(): the Mutex==NULL! Not initialized?");
}

//
// ****************************************************************************
// CConfiguration
//

const wchar_t* DefTopToolBar = L"11,14,15,70,-1,40,56,-1,30,31,32,-1,41,42,18,27,55,33,-1,3,34,35,20,19,-1,43,46,49";
const wchar_t* DefMiddleToolBar = L"2,3,17,21,22,23,72,26,24,25,27,55,28,29,30,31,32,33";
const wchar_t* DefLeftToolBar = L"36";
const wchar_t* DefRightToolBar = L"51";

CConfiguration::CConfiguration()
{
    ConfigVersion = 0;
    IncludeDirs = FALSE;
    AutoSave = TRUE;
    CloseShell = FALSE;
    ShowGrepErrors = FALSE; // other search tools (FAR/WinCmd/PowerDesk/Windows Find) do not show errors
    FindFullRowSelect = FALSE;
    FindFileTypeMode = 0; // fftmAll

    MinBeepWhenDone = TRUE;
    UseRecycleBin = 1;
    FileNameFormat = 4; // as on the disk
    SizeFormat = SIZE_FORMAT_BYTES;
    RecycleMasks.SetMasksString(L"*.txt;*.doc");
    LastFocusedPage = 0;
    ConfigurationHeight = 0; // the dialog logic will not allow a smaller dialog than the largest page it contains
    ViewersAndEditorsExpanded = 0;
    PackersAndUnpackersExpanded = 0;
    ClearReadOnly = TRUE;
    PrimaryContextMenu = TRUE;
    NotHiddenSystemFiles = FALSE;
    AlwaysOnTop = FALSE;
    CommandShellTargetKind = 0;
    CommandShellProfileGuid.clear();
    CommandShellProfileName.clear();
    //  FastDirectoryMove = TRUE;
    SortUsesLocale = TRUE;
    SortDetectNumbers = TRUE;
    SortNewerOnTop = FALSE; // by default sort like Explorer on XP, newer items at the bottom
    SortDirsByName = FALSE; // so people do not report it as a bug like they did to Ghisler
    SortDirsByExt = FALSE;  // directories have no extensions, an option kept for companies/users relying on the old directory ordering
    SaveHistory = TRUE;
    SaveWorkDirs = FALSE; // by default save space in the registry, the list is large
    EnableCmdLineHistory = TRUE;
    SaveCmdLineHistory = TRUE;
    //  LantasticCheck = FALSE;
    NetwareFastDirMove = FALSE; // choose the slower but 100% working mode; power users can switch it
    UseAsyncCopyAlg = TRUE;
    ReloadEnvVariables = TRUE;
    SuggestPathNames = TRUE;
    UpdateCheckMode = 2; // weekly (Sally::Update::kDefaultUpdateCheckMode)
    QuickRenameSelectAll = FALSE;
    EditNewSelectAll = TRUE;
    ShiftForHotPaths = TRUE;
    OnlyOneInstance = FALSE;
    ForceOnlyOneInstance = FALSE;
    StatusArea = FALSE;
    SingleClick = FALSE;
    TopToolBarVisible = TRUE;
    PluginsBarVisible = FALSE;
    MiddleToolBarVisible = FALSE;
    BottomToolBarVisible = TRUE;
    UserMenuToolBarVisible = FALSE;
    HotPathsBarVisible = FALSE;
    DriveBarVisible = TRUE;
    DriveBar2Visible = FALSE;
    IconSpacingVert = 43;
    IconSpacingHorz = 43;
    TileSpacingVert = 8;
    ThumbnailSpacingHorz = 19; // 29 on Windows XP
    ThumbnailSize = THUMBNAIL_SIZE_DEFAULT;

    // options for Compare Directories
    CompareByTime = TRUE;
    CompareBySize = TRUE;
    CompareByContent = FALSE;
    CompareByAttr = FALSE;
    CompareSubdirs = FALSE;
    CompareSubdirsAttr = FALSE;
    CompareOnePanelDirs = FALSE;
    CompareMoreOptions = FALSE;
    CompareIgnoreFiles = FALSE;
    CompareIgnoreDirs = FALSE;
    int errPos;
    CompareIgnoreFilesMasks.SetMasksString(L"");
    CompareIgnoreFilesMasks.PrepareMasks(errPos);
    CompareIgnoreDirsMasks.SetMasksString(L"");
    CompareIgnoreDirsMasks.PrepareMasks(errPos);

    // Confirmation
    CnfrmFileDirDel = TRUE;
    CnfrmNEDirDel = FALSE;
    CnfrmFileOver = TRUE;
    CnfrmDirOver = FALSE;
    CnfrmSHFileDel = TRUE;
    CnfrmSHDirDel = FALSE;
    CnfrmSHFileOver = TRUE;
    CnfrmNTFSPress = TRUE;
    CnfrmNTFSCrypt = TRUE;
    CnfrmDragDrop = FALSE;
    CnfrmCloseArchive = TRUE;
    CnfrmCreateDir = TRUE;
    CnfrmCloseFind = TRUE;
    CnfrmStopFind = TRUE;
    CnfrmCreatePath = TRUE;
    CnfrmAlwaysOnTop = TRUE;
    CnfrmOnSalClose = FALSE;
    CnfrmSendEmail = TRUE;
    CnfrmAddToArchive = TRUE;
    CnfrmChangeDirTC = TRUE;
    CnfrmShowNamesToCompare = TRUE;
    CnfrmDSTShiftsIgnored = TRUE;
    CnfrmDSTShiftsOccured = TRUE;
    CnfrmCopyMoveOptionsNS = TRUE;
    //  PanelTooltip = TRUE;
    KeepPluginsSorted = TRUE;

    LastUsedSpeedLimit = 1024 * 1024; // default 1 MB/s

    QuickSearchEnterAlt = FALSE;

    // for displaying items in the panel
    FullRowSelect = FALSE;
    FullRowHighlight = TRUE;
    UseIconTincture = TRUE;
    ShowPanelCaption = TRUE;
    ShowPanelZoom = TRUE;
    InfoLineContent = L"$(FileName): $(FileSize), $(FileDate), $(FileTime), $(FileAttributes), $(FileDOSName)";

    HotPathAutoConfig = TRUE;

    DrvSpecFloppyMon = TRUE;
    DrvSpecFloppySimple = TRUE;
    DrvSpecRemovableMon = TRUE;
    DrvSpecRemovableSimple = FALSE; // 2.5b11: since floppies have their own category, we can afford to read icons
    DrvSpecFixedMon = TRUE;
    DrvSpecFixedSimple = FALSE;
    DrvSpecRemoteMon = TRUE;
    DrvSpecRemoteSimple = FALSE;
    DrvSpecRemoteDoNotRefreshOnAct = FALSE;
    DrvSpecCDROMMon = TRUE;
    DrvSpecCDROMSimple = FALSE;

    IfPathIsInaccessibleGoToIsMyDocs = TRUE;
    IfPathIsInaccessibleGoTo.clear();

    SpaceSelCalcSpace = TRUE;

    UseTimeResolution = TRUE;
    TimeResolution = 2;
    IgnoreDSTShifts = FALSE;

    UseDragDropMinTime = TRUE;
    DragDropMinTime = 500;

    TopToolBar = DefTopToolBar;
    MiddleToolBar = DefMiddleToolBar;
    LeftToolBar = DefLeftToolBar;
    RightToolBar = DefRightToolBar;

    SkillLevel = SKILL_LEVEL_ADVANCED; // try to present as many options as possible

    int i;
    for (i = 0; i < SELECT_HISTORY_SIZE; i++)
        SelectHistory[i] = NULL;
    for (i = 0; i < COPY_HISTORY_SIZE; i++)
        CopyHistory[i] = NULL;
    for (i = 0; i < EDIT_HISTORY_SIZE; i++)
        EditHistory[i] = NULL;
    for (i = 0; i < CHANGEDIR_HISTORY_SIZE; i++)
        ChangeDirHistory[i] = NULL;
    for (i = 0; i < FILELIST_HISTORY_SIZE; i++)
        FileListHistory[i] = NULL;
    for (i = 0; i < CREATEDIR_HISTORY_SIZE; i++)
        CreateDirHistory[i] = NULL;
    for (i = 0; i < QUICKRENAME_HISTORY_SIZE; i++)
        QuickRenameHistory[i] = NULL;
    for (i = 0; i < EDITNEW_HISTORY_SIZE; i++)
        EditNewHistory[i] = NULL;
    for (i = 0; i < CONVERT_HISTORY_SIZE; i++)
        ConvertHistory[i] = NULL;
    for (i = 0; i < FILTER_HISTORY_SIZE; i++)
        FilterHistory[i] = NULL;

    FileListHistory[0] = DupStr(L"$(FileName)$(CRLF)"); // default for MakeFileList

    FileListName.clear();
    FileListAppend = FALSE;
    FileListDestination = 0;
    // Internal Viewer:
    CopyFindText = TRUE;
    EOL_CRLF = TRUE;
    EOL_CR = TRUE;
    EOL_LF = TRUE;
    EOL_NULL = TRUE;
    DefViewMode = 0; // Auto-Select
    TabSize = 8;
    SavePosition = FALSE;
    TextModeMasks.SetMasksString(L"*.txt;*.602;*.xml");
    TextModeMasks.PrepareMasks(errPos);
    HexModeMasks.SetMasksString(L"");
    HexModeMasks.PrepareMasks(errPos);
    WindowPlacement.length = 0;
    WrapText = TRUE;
    CodePageAutoSelect = TRUE;
    DefaultConvert.clear();
    AutoCopySelection = FALSE;
    GoToOffsetIsHex = TRUE;

    // Change drive
    ChangeDriveShowMyDoc = TRUE;
    ChangeDriveShowAnother = TRUE;
    ChangeDriveShowNet = TRUE;
    ChangeDriveCloudStorage = TRUE;

    MenuIndex = 0;
    MenuBreak = TRUE;
    MenuWidth = 1; // dummy

    TopToolbarIndex = 1;
    TopToolbarBreak = TRUE;
    TopToolbarWidth = 1; // dummy

    PluginsBarIndex = 2;
    PluginsBarBreak = TRUE;
    PluginsBarWidth = 1; // dummy

    UserMenuToolbarIndex = 3;
    UserMenuToolbarBreak = TRUE;
    UserMenuToolbarWidth = 1; // dummy
    UserMenuToolbarLabels = 1;

    HotPathsBarIndex = 4;
    HotPathsBarBreak = TRUE;
    HotPathsBarWidth = 1; // dummy

    DriveBarIndex = 5;
    DriveBarBreak = TRUE;
    DriveBarWidth = 1; // dummy

    GripsVisible = TRUE;

    // Packers / Unpackers
    UseAnotherPanelForPack = FALSE;   // like WinZip folks - let's annoy them
    UseAnotherPanelForUnpack = FALSE; // or rather strip them bare :-)
    UseSubdirNameByArchiveForUnpack = FALSE;
    UseSimpleIconsInArchives = FALSE;

    UseEditNewFileDefault = FALSE;
    EditNewFileDefault.clear();

    // Tip of the Day
    //  ShowTipOfTheDay = TRUE;
    //  LastTipOfTheDay = 0;

    LastPluginVer = 0;
    LastPluginVerOP = 0;

    ConfigWasImported = FALSE;

    // Find dialog
    SearchFileContent = FALSE;
    FindDialogWindowPlacement.length = 0; // not valid yet
    // column width of the Find dialog
    FindColNameWidth = -1; // let it be set according to the window size

    // Language
    LoadedSLGName.clear();
    SLGName.clear();

    // the ConversionTable variable is not loaded from the configuration
    ConversionTable = L"*";

    TitleBarShowPath = TRUE;
    TitleBarMode = TITLE_BAR_MODE_DIRECTORY; // like Explorer
    UseTitleBarPrefix = FALSE;
    TitleBarPrefix = L"ADMIN";
    UseTitleBarPrefixForced = FALSE;
    TitleBarPrefixForced.clear();

    MainWindowIconIndex = 0; // default icon
    MainWindowIconIndexForced = -1;

    ClickQuickRename = TRUE;

    VisibleDrives = DRIVES_MASK; // by default show all drives
    SeparatedDrives = 0;         // insert no separators by default, we do not know where

    ShowSplashScreen = TRUE;

    EnableCustomIconOverlays = TRUE;
    DisabledCustomIconOverlays = NULL;

#ifndef _WIN64
    AddX86OnlyPlugins = FALSE; // FIXME_X64_WINSCP
#endif                         // _WIN64
}

CConfiguration::~CConfiguration()
{
    ClearHistory();
    ClearListOfDisabledCustomIconOverlays();
}

void CConfiguration::ClearHistory()
{
    int i;
    for (i = 0; i < SELECT_HISTORY_SIZE; i++)
    {
        if (SelectHistory[i] != NULL)
        {
            free(SelectHistory[i]);
            SelectHistory[i] = NULL;
        }
    }
    for (i = 0; i < COPY_HISTORY_SIZE; i++)
    {
        if (CopyHistory[i] != NULL)
        {
            free(CopyHistory[i]);
            CopyHistory[i] = NULL;
        }
    }
    for (i = 0; i < EDIT_HISTORY_SIZE; i++)
    {
        if (EditHistory[i] != NULL)
        {
            free(EditHistory[i]);
            EditHistory[i] = NULL;
        }
    }
    for (i = 0; i < CHANGEDIR_HISTORY_SIZE; i++)
    {
        if (ChangeDirHistory[i] != NULL)
        {
            free(ChangeDirHistory[i]);
            ChangeDirHistory[i] = NULL;
        }
    }
    for (i = 0; i < FILELIST_HISTORY_SIZE; i++)
    {
        if (FileListHistory[i] != NULL)
        {
            free(FileListHistory[i]);
            FileListHistory[i] = NULL;
        }
    }
    for (i = 0; i < CREATEDIR_HISTORY_SIZE; i++)
    {
        if (CreateDirHistory[i] != NULL)
        {
            free(CreateDirHistory[i]);
            CreateDirHistory[i] = NULL;
        }
    }

    for (i = 0; i < QUICKRENAME_HISTORY_SIZE; i++)
    {
        if (QuickRenameHistory[i] != NULL)
        {
            free(QuickRenameHistory[i]);
            QuickRenameHistory[i] = NULL;
        }
    }

    for (i = 0; i < EDITNEW_HISTORY_SIZE; i++)
    {
        if (EditNewHistory[i] != NULL)
        {
            free(EditNewHistory[i]);
            EditNewHistory[i] = NULL;
        }
    }

    for (i = 0; i < CONVERT_HISTORY_SIZE; i++)
    {
        if (ConvertHistory[i] != NULL)
        {
            free(ConvertHistory[i]);
            ConvertHistory[i] = NULL;
        }
    }
    for (i = 0; i < FILTER_HISTORY_SIZE; i++)
    {
        if (FilterHistory[i] != NULL)
        {
            free(FilterHistory[i]);
            FilterHistory[i] = NULL;
        }
    }
}

BOOL CConfiguration::PrepareRecycleMasks(int& errorPos)
{
    return RecycleMasks.PrepareMasks(errorPos);
}

BOOL CConfiguration::AgreeRecycleMasks(const wchar_t* fileName, const wchar_t* fileExt)
{
    return RecycleMasks.AgreeMasks(fileName, fileExt);
}

int CConfiguration::GetMainWindowIconIndex()
{
    int index = MainWindowIconIndexForced != -1 ? MainWindowIconIndexForced : MainWindowIconIndex;
    if (index >= 0 && index < MAINWINDOWICONS_COUNT)
        return index;
    else
        return 0; // default
}

//
// ****************************************************************************
// CConfigurationDlg
//

namespace
{
    sally::configuration::DialogFont ReadConfigurationDialogFont(HWND parent, LOGFONTW& dialogFont)
    {
        sally::configuration::DialogFont result;
        dialogFont = UseCustomPanelFont ? LogFont : Sally::Appearance::SystemDialogFont(0);
        HDC fontDC = GetDC(parent);
        const int dpi = fontDC != NULL ? GetDeviceCaps(fontDC, LOGPIXELSY) : 96;
        if (fontDC != NULL)
            ReleaseDC(parent, fontDC);
        const long long height = dialogFont.lfHeight < 0 ? -static_cast<long long>(dialogFont.lfHeight) : dialogFont.lfHeight;
        const long long points = (height * 72 + dpi / 2) / (dpi > 0 ? dpi : 96);
        result.Points = static_cast<WORD>((std::min)(65535LL, (std::max)(1LL, points)));
        result.Weight = static_cast<WORD>(dialogFont.lfWeight);
        result.Italic = dialogFont.lfItalic;
        result.Charset = dialogFont.lfCharSet;
        result.Face.assign(dialogFont.lfFaceName, wcsnlen_s(dialogFont.lfFaceName, LF_FACESIZE));
        dialogFont.lfHeight = -MulDiv(result.Points, dpi > 0 ? dpi : 96, 72);
        dialogFont.lfWidth = 0;
        return result;
    }
} // namespace

CConfigurationDlg::CConfigurationDlg(HWND parent, CUserMenuItems* userMenuItems,
                                     int mode, int param)
    : CTreePropDialog(parent, HLanguage, LoadStrW(IDS_CONFIGURATION),
                      mode == 0 ? Configuration.LastFocusedPage : mode == 1 ? 14
                                                              : mode == 2   ? 13
                                                              : mode == 3   ? 21
                                                              : mode == 4   ? 12
                                                              : mode == 5   ? 1
                                                                            : 11 /* mode == 6 */,
                      PSH_NOAPPLYNOW | PSH_HASHELP,
                      &Configuration.LastFocusedPage,
                      &Configuration.ConfigurationHeight),
      PageView(mode == 4 ? param : -1), //-1 = active panel
      PageUserMenu(userMenuItems),
      PageHotPath(mode == 1 ? TRUE : FALSE, param),
      PageDrives(mode == 6 ? TRUE : FALSE),
      Page14(TRUE)
{
    HOldPluginMsgBoxParent = NULL;
    ApplyCaption = LoadStrOwned(IDS_CFG_APPLY);
    LOGFONTW nativeFont;
    ConfigurationFont = ReadConfigurationDialogFont(parent, nativeFont);
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // when the page order changes, update the constructor
    // mode == 0 ? Configuration.LastFocusedPage : 4
    // in 1.6b2 this fooled me
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    /*00*/ Add(&PageGeneral);       // General
    /*01*/ Add(&PagePanels);        // Panels
    /*02*/ Add(&PageHistory);       // History
    /*03*/ Add(&PageSystem);        // System
    /*04*/ Add(&PageRegional);      // Regional
    /*05*/ Add(&PageMainWindow);    // MainWindow
    /*06*/ Add(&PageAppear);        // Appearance
    /*07*/ Add(&PageColors);        // Colors
    /*08*/ Add(&PageKeyboard);      // Keyboard
    /*09*/ Add(&PageConfirmations); // Confirmations
    /*10*/ Add(&PageChangeDrive);   // Change Drive Menu
    /*11*/ Add(&PageDrives);        // Drives
    /*12*/ Add(&PageView);          // Views
    /*13*/ Add(&PageUserMenu);      // User Menu
    /*14*/ Add(&PageHotPath);       // Hot Paths
    /*15*/ Add(&PageSecurity);      // Security
    /*16*/ Add(&PageIconOvrls);     // Icon Overlays
    /*17*/ Add(&PageViewEdit, NULL, &Configuration.ViewersAndEditorsExpanded);
    /*18*/ Add(&Page13, &PageViewEdit);
    /*19*/ Add(&Page14, &PageViewEdit);
    /*20*/ Add(&Page15, &PageViewEdit);
    /*21*/ Add(&PageViewer, &PageViewEdit);
    /*22*/ Add(&PagePP, NULL, &Configuration.PackersAndUnpackersExpanded);
    /*23*/ Add(&PageP4, &PagePP);
    /*24*/ Add(&PageP3, &PagePP);
    /*25*/ Add(&PageP1, &PagePP);
    /*26*/ Add(&PageP2, &PagePP);
    /*27*/ //  Add(&PageShellExtensions);
           // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
           // when the page order changes, update the constructor
           // mode == 0 ? Configuration.LastFocusedPage : 4
           // in 1.6b2 this fooled me
           // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    for (int i = 0; i < Count; i++)
        At(i)->SetDarkModeGroupBoxThemeEnabled(TRUE);
    AppliedDrafts.resize(Count);
}

sally::configuration::DraftState CConfigurationDlg::CaptureDraft(int page)
{
    sally::configuration::DraftState state;
    auto* selected = At(page);
    if (selected->HWindow == NULL || selected == &PageSecurity)
        return state; // Master-password operations are separately confirmed, immediate actions.
    const bool modelPage = selected == &PageView || selected == &PageUserMenu || selected == &PageHotPath ||
                           selected == &PageColors || selected == &Page13 || selected == &Page14 || selected == &Page15 ||
                           selected == &PageP1 || selected == &PageP2 || selected == &PageP3 || selected == &PageP4;
    sally::configuration::CaptureDraftControls(selected->HWindow, state, modelPage);
    if (selected == &PageAppear)
    {
        state.Add(PageAppear.LocalUseCustomPanelFont);
        sally::configuration::CaptureDraftFont(PageAppear.LocalPanelLogFont, state);
    }
    else if (selected == &PageViewer)
    {
        state.Add(PageViewer.LocalUseCustomViewerFont);
        sally::configuration::CaptureDraftFont(PageViewer.LocalViewerLogFont, state);
        for (auto color : PageViewer.TmpColors)
            state.Add(color);
    }
    else if (selected == &PageView)
    {
        for (const auto& item : PageView.Config.Items)
            state.Append(item.Mode, item.Name, item.Flags, item.LeftSmartMode, item.RightSmartMode, PageView.Config.SaveColumns(item.Columns));
        sally::configuration::CaptureInvalidColumnWidthControls(selected->HWindow, state, IDC_VIEW_LEFT_WIDTH, IDC_VIEW_RIGHT_WIDTH);
    }
    else if (selected == &PageUserMenu)
    {
        for (int row = 0; row < PageUserMenu.UserMenuItems->Count; ++row)
        {
            const auto& item = *PageUserMenu.UserMenuItems->At(row);
            state.Append(item.ItemName, item.UMCommand, item.Arguments, item.InitDir, item.Icon,
                         item.ThroughShell, item.CloseShell, item.UseWindow, item.ShowInToolbar, item.Type);
        }
    }
    else if (selected == &PageHotPath)
    {
        state.Add(IsDlgButtonChecked(selected->HWindow, IDC_HOTPATH_AUTOCONFIG));
        for (int row = 0; row < HOT_PATHS_COUNT; ++row)
            state.Append(PageHotPath.Config->GetNameW(row), PageHotPath.Config->GetPathW(row), PageHotPath.Config->GetVisible(row));
    }
    else if (selected == &PageColors)
    {
        state.Add(SendDlgItemMessageW(selected->HWindow, IDC_C_SCHEME, CB_GETCURSEL, 0, 0));
        for (auto color : PageColors.TmpColors)
            state.Add(color);
        for (int row = 0; row < PageColors.HighlightMasks.Count; ++row)
        {
            const auto& item = *PageColors.HighlightMasks.At(row);
            state.Append(item.Masks->GetMasksString(), item.Attr, item.ValidAttr, item.NormalFg, item.NormalBk,
                         item.FocusedFg, item.FocusedBk, item.SelectedFg, item.SelectedBk, item.FocSelFg, item.FocSelBk, item.HighlightFg, item.HighlightBk);
        }
    }
    else if (selected == &Page13 || selected == &Page14)
    {
        auto& viewers = selected == &Page13 ? Page13.ViewerMasks : Page14.ViewerMasks;
        for (int row = 0; row < viewers.Count; ++row)
        {
            const auto& item = *viewers.At(row);
            state.Append(item.Masks->GetMasksString(), item.Command, item.Arguments, item.InitDir, item.ViewerType, item.OldType);
        }
    }
    else if (selected == &Page15)
    {
        for (int row = 0; row < Page15.EditorMasks.Count; ++row)
        {
            const auto& item = *Page15.EditorMasks.At(row);
            state.Append(item.Masks->GetMasksString(), item.Command, item.Arguments, item.InitDir);
        }
    }
    else if (selected == &PageP1)
    {
        auto& config = *PageP1.Config;
        for (int row = 0; row < config.GetPackersCount(); ++row)
            state.Append(config.GetPackerTitle(row), config.GetPackerExt(row), config.GetPackerType(row), config.GetPackerOldType(row),
                         config.GetPackerCmdExecCopy(row), config.GetPackerCmdArgsCopy(row), config.GetPackerCmdExecMove(row), config.GetPackerCmdArgsMove(row),
                         config.GetPackerSupLongNames(row), config.GetPackerSupMove(row), config.GetPackerNeedANSIListFile(row));
    }
    else if (selected == &PageP2)
    {
        auto& config = *PageP2.Config;
        for (int row = 0; row < config.GetUnpackersCount(); ++row)
            state.Append(config.GetUnpackerTitle(row), config.GetUnpackerExt(row), config.GetUnpackerType(row), config.GetUnpackerOldType(row),
                         config.GetUnpackerCmdExecExtract(row), config.GetUnpackerCmdArgsExtract(row), config.GetUnpackerSupLongNames(row), config.GetUnpackerNeedANSIListFile(row));
    }
    else if (selected == &PageP3)
    {
        auto& config = *PageP3.Config;
        for (int row = 0; row < config.GetArchiversCount(); ++row)
            state.Append(config.GetArchiverUID(row), config.GetPackerExeFile(row), config.GetUnpackerExeFile(row), config.ArchiverExesAreSame(row));
    }
    else if (selected == &PageP4)
    {
        auto& config = *PageP4.Config;
        for (int row = 0; row < config.GetFormatsCount(); ++row)
            state.Append(config.GetExt(row), config.GetUsePacker(row), config.GetPackerIndex(row), config.GetUnpackerIndex(row), config.GetOldType(row));
    }
    return state;
}

void CConfigurationDlg::PageCreated(int page)
{
    AppliedDrafts[page] = CaptureDraft(page);
    UpdateApplyState();
}

void CConfigurationDlg::ResetAppliedDrafts()
{
    for (int page = 0; page < Count; ++page)
        if (At(page)->HWindow != NULL)
            AppliedDrafts[page] = CaptureDraft(page);
    UpdateApplyState();
}

void CConfigurationDlg::UpdateApplyState()
{
    bool changed = false;
    for (int page = 0; page < Count && !changed; ++page)
        if (At(page)->HWindow != NULL)
            changed = CaptureDraft(page) != AppliedDrafts[page];
    SetApplyButtonEnabled(changed);
}

bool CConfigurationDlg::PrepareDialogTemplate(const void* source, std::size_t size, std::vector<BYTE>& output)
{
    return sally::configuration::SetDialogTemplateFont(source, size, ConfigurationFont, output);
}

bool CConfigurationDlg::AfterTransfer(bool save)
{
    if (ApplyHandler)
        ApplyHandler();
    LOGFONTW nativeFont;
    const auto selected = ReadConfigurationDialogFont(Dialog.HWindow, nativeFont);
    if (selected.Points != ConfigurationFont.Points || selected.Face != ConfigurationFont.Face ||
        selected.Weight != ConfigurationFont.Weight || selected.Italic != ConfigurationFont.Italic ||
        selected.Charset != ConfigurationFont.Charset)
    {
        if (!ReflowFont(nativeFont))
        {
            gPrompter->ShowError(Dialog.HWindow, LoadStrW(IDS_ERRORTITLE), GetErrorTextOwned(ERROR_NOT_ENOUGH_MEMORY).c_str());
            return false;
        }
        ConfigurationFont = selected;
    }
    ResetAppliedDrafts();
    if (!save)
        return true;
    LONG error = ERROR_SUCCESS;
    if (MainWindow->SaveConfig(Dialog.HWindow, &error))
        return true;
    const std::wstring message = LoadStrOwned(IDS_CFG_SAVE_FAILED) + L"\n\n" + GetErrorTextOwned(error);
    gPrompter->ShowError(Dialog.HWindow, LoadStrW(IDS_ERRORSAVECONFIG), message.c_str());
    return false;
}

namespace
{
    enum
    {
        ConfigurationSearchOpenId = 12,
        ConfigurationSearchEditId,
        ConfigurationSearchListId = 15,
        ConfigurationSearchClearId,
        ConfigurationSearchCloseId,
    };

    LRESULT CALLBACK ConfigurationSearchEditProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                                  UINT_PTR subclass, DWORD_PTR)
    {
        if (message == WM_COMMAND && LOWORD(wParam) == ConfigurationSearchClearId)
            return SendMessageW(GetParent(window), message, wParam, lParam);
        if (message == WM_NCDESTROY)
            RemoveWindowSubclass(window, ConfigurationSearchEditProc, subclass);
        return DefSubclassProc(window, message, wParam, lParam);
    }
}

int CConfigurationDlg::NavigationHeaderHeight(HWND window)
{
    RECT units = {0, 0, 0, 18};
    Sally::Appearance::MapCurrentDialogRect(window, units);
    return units.bottom;
}

void CConfigurationDlg::LayoutNavigation(const RECT& area)
{
    if (SearchEdit == NULL)
        return;
    RECT units = {0, 0, 4, 14};
    Sally::Appearance::MapCurrentDialogRect(Dialog.HWindow, units);
    const int width = (std::max)(0L, area.right - area.left);
    const int header = sally::configuration::FitNavigationHeader(NavigationHeaderHeight(Dialog.HWindow), area.bottom - area.top);
    const int rowHeight = (std::min<LONG>)(units.bottom, header);
    const int backWidth = (std::min)(rowHeight, width);
    const int backGap = (std::min<int>)(units.right, width - backWidth);
    const int fieldWidth = width - backWidth - backGap;
    const bool hasText = GetWindowTextLengthW(SearchEdit) != 0;
    const int clearWidth = hasText ? (std::min)(rowHeight, fieldWidth) : 0;
    const int fieldLeft = area.left + backWidth + backGap;
    const int listTop = area.top + header;
    SetWindowPos(SearchOpen, NULL, area.left, area.top, width, rowHeight, SWP_NOZORDER);
    SetWindowPos(SearchClose, NULL, area.left, area.top, backWidth, rowHeight, SWP_NOZORDER);
    // Reserve the edit's trailing area for its clear action, keeping one field row.
    SetWindowPos(SearchEdit, NULL, fieldLeft, area.top, fieldWidth, rowHeight, SWP_NOZORDER);
    SendMessageW(SearchEdit, EM_SETMARGINS, EC_RIGHTMARGIN, MAKELPARAM(0, clearWidth));
    RECT field = {};
    GetClientRect(SearchEdit, &field);
    SetWindowPos(SearchClear, HWND_TOP, (std::max)(0L, field.right - clearWidth), 0,
                 clearWidth, field.bottom, 0);
    ShowWindow(SearchOpen, SearchExpanded ? SW_HIDE : SW_SHOW);
    ShowWindow(SearchEdit, SearchExpanded ? SW_SHOW : SW_HIDE);
    ShowWindow(SearchClose, SearchExpanded ? SW_SHOW : SW_HIDE);
    ShowWindow(SearchClear, SearchExpanded && hasText ? SW_SHOW : SW_HIDE);
    SetWindowPos(SearchList, NULL, area.left, listTop, width, (std::max)(0L, area.bottom - listTop), SWP_NOZORDER);

    HDC dc = GetDC(SearchList);
    if (dc != NULL)
    {
        const HGDIOBJ font = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(SearchList, WM_GETFONT, 0, 0)));
        int extent = 0;
        for (const auto& result : SearchResults)
        {
            SIZE size = {};
            if (GetTextExtentPoint32W(dc, result.Label.c_str(), static_cast<int>(result.Label.size()), &size))
                extent = (std::max<int>)(extent, size.cx + 2 * GetSystemMetrics(SM_CXEDGE));
        }
        SelectObject(dc, font);
        ReleaseDC(SearchList, dc);
        SendMessageW(SearchList, LB_SETHORIZONTALEXTENT, extent, 0);
    }
}

void CConfigurationDlg::UpdateSearch()
{
    const std::wstring query = GetWindowTextStringW(SearchEdit);
    EnableWindow(SearchClear, !query.empty());
    HasSearch = SearchExpanded && query.find_first_not_of(L" \t\r\n") != std::wstring::npos;
    SearchResults = ConfigurationSearch.FindSubstring(query);
    SendMessageW(SearchList, WM_SETREDRAW, FALSE, 0);
    SendMessageW(SearchList, LB_RESETCONTENT, 0, 0);
    for (const auto& result : SearchResults)
        SendMessageW(SearchList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(result.Label.c_str()));
    if (HasSearch && SearchResults.empty())
        SendMessageW(SearchList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(LoadStrW(IDS_CFG_SEARCH_EMPTY)));
    SendMessageW(SearchList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(SearchList, NULL, TRUE);
    ShowNavigationTree(!HasSearch);
    ShowWindow(SearchList, HasSearch ? SW_SHOW : SW_HIDE);
    RefreshLayout();
}

void CConfigurationDlg::ClearSearch()
{
    SetWindowTextW(SearchEdit, L"");
    SetFocus(SearchEdit);
}

void CConfigurationDlg::OpenSearch()
{
    SearchExpanded = true;
    UpdateSearch();
    SetFocus(SearchEdit);
    SendMessageW(SearchEdit, EM_SETSEL, 0, -1);
}

void CConfigurationDlg::CloseSearch()
{
    SearchExpanded = false;
    SetWindowTextW(SearchEdit, L"");
    UpdateSearch();
    // The holder's navigation tree retains its selected page throughout search.
    SetFocus(GetDlgItem(Dialog.HWindow, 1));
}

void CConfigurationDlg::DrawSearchAction(const DRAWITEMSTRUCT& item)
{
    const int saved = SaveDC(item.hDC);
    if (saved == 0)
        return;
    DarkModeColors palette;
    const bool dark = DarkMode_GetColors(&palette) != FALSE;
    const COLORREF background = dark ? palette.DialogBackground : GetSysColor(COLOR_BTNFACE);
    const COLORREF foreground = dark ? palette.DialogText : GetSysColor(COLOR_BTNTEXT);
    const COLORREF subdued = dark ? palette.DisabledText : GetSysColor(COLOR_GRAYTEXT);
    SetDCBrushColor(item.hDC, background);
    FillRect(item.hDC, &item.rcItem, reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SelectObject(item.hDC, reinterpret_cast<HFONT>(SendMessageW(item.hwndItem, WM_GETFONT, 0, 0)));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, foreground);
    SetDCPenColor(item.hDC, foreground);
    SelectObject(item.hDC, GetStockObject(DC_PEN));
    SelectObject(item.hDC, GetStockObject(NULL_BRUSH));
    const int height = item.rcItem.bottom - item.rcItem.top;
    const int size = (std::max)(4, height * 3 / 5);
    const int left = item.rcItem.left + (height - size) / 2;
    const int top = item.rcItem.top + (height - size) / 2;
    if (item.CtlID == ConfigurationSearchCloseId)
    {
        const int middle = item.rcItem.top + height / 2;
        MoveToEx(item.hDC, left + size / 2, top, NULL);
        LineTo(item.hDC, left, middle);
        LineTo(item.hDC, left + size / 2, top + size);
        MoveToEx(item.hDC, left, middle, NULL);
        LineTo(item.hDC, left + size, middle);
    }
    else
    {
        Ellipse(item.hDC, left, top, left + size * 2 / 3, top + size * 2 / 3);
        MoveToEx(item.hDC, left + size / 2, top + size / 2, NULL);
        LineTo(item.hDC, left + size, top + size);
        RECT text = item.rcItem;
        text.left += height;
        text.right -= (std::max)(2, height / 8);
        SIZE shortcut = {};
        GetTextExtentPoint32W(item.hDC, L"Ctrl+F", 6, &shortcut);
        RECT label = text;
        label.right = (std::max)(label.left, text.right - shortcut.cx - height / 3);
        DrawTextW(item.hDC, SearchOpenCaption.c_str(), static_cast<int>(SearchOpenCaption.size()),
                  &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SetTextColor(item.hDC, subdued);
        DrawTextW(item.hDC, L"Ctrl+F", 6, &text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT))
    {
        RECT focus = item.rcItem;
        InflateRect(&focus, -1, -1);
        DrawFocusRect(item.hDC, &focus);
    }
    RestoreDC(item.hDC, saved);
}

bool CConfigurationDlg::HandleDialogMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_DRAWITEM && lParam != 0)
    {
        const auto& item = *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (item.CtlID == ConfigurationSearchOpenId || item.CtlID == ConfigurationSearchCloseId)
        {
            DrawSearchAction(item);
            return true;
        }
    }
    if (message == WM_TIMER && wParam == 0x4346)
    {
        UpdateApplyState();
        return true;
    }
    if (message != WM_COMMAND)
        return false;
    if (LOWORD(wParam) == ConfigurationSearchOpenId && HIWORD(wParam) == BN_CLICKED)
    {
        OpenSearch();
        return true;
    }
    if (LOWORD(wParam) == ConfigurationSearchCloseId && HIWORD(wParam) == BN_CLICKED)
    {
        CloseSearch();
        return true;
    }
    if (LOWORD(wParam) == ConfigurationSearchClearId && HIWORD(wParam) == BN_CLICKED)
    {
        ClearSearch();
        return true;
    }
    if (LOWORD(wParam) == ConfigurationSearchEditId && HIWORD(wParam) == EN_CHANGE)
    {
        UpdateSearch();
        return true;
    }
    if (LOWORD(wParam) == ConfigurationSearchListId && HIWORD(wParam) == LBN_SELCHANGE)
    {
        const int selection = static_cast<int>(SendMessageW(SearchList, LB_GETCURSEL, 0, 0));
        if (selection >= 0 && static_cast<std::size_t>(selection) < SearchResults.size())
        {
            // Browsing previews the setting without moving keyboard navigation
            // out of the results. Enter explicitly focuses the setting below.
            if (NavigateToPage(SearchResults[selection].Page, SearchResults[selection].Control))
                SetFocus(SearchList);
        }
        return true;
    }
    return false;
}

bool CConfigurationDlg::PreTranslateMessage(const MSG& message)
{
    if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE &&
        SearchEdit != NULL && SearchExpanded)
    {
        if (GetWindowTextLengthW(SearchEdit) != 0)
            ClearSearch();
        else
            CloseSearch();
        return true;
    }
    if (message.message == WM_KEYDOWN && message.wParam == L'F' &&
        (GetKeyState(VK_CONTROL) & 0x8000) != 0 && SearchEdit != NULL)
    {
        OpenSearch();
        return true;
    }
    if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN && HasSearch &&
        (GetFocus() == SearchEdit || GetFocus() == SearchList))
    {
        int selection = static_cast<int>(SendMessageW(SearchList, LB_GETCURSEL, 0, 0));
        if (selection == LB_ERR && !SearchResults.empty()) selection = 0;
        if (selection >= 0 && static_cast<std::size_t>(selection) < SearchResults.size())
            NavigateToPage(SearchResults[selection].Page, SearchResults[selection].Control);
        return true;
    }
    return false;
}

void CConfigurationDlg::IndexPageResource(int page, const void* source, std::size_t size)
{
    sally::configuration::DialogTemplate resource;
    if (!sally::configuration::ReadDialogTemplate(source, size, resource))
        return;
    ConfigurationSearch.AddPage(page, resource);
    const auto choices = [&](int control, std::initializer_list<int> labels)
    {
        for (int label : labels)
            ConfigurationSearch.AddOptionValue(page, control, LoadStrOwned(label));
    };
    const auto pluginChoices = [&](int control, auto getIndex)
    {
        for (int count = 0;; ++count)
        {
            const int index = (Plugins.*getIndex)(count);
            if (index < 0) break;
            CPluginData* plugin = Plugins.Get(index);
            if (plugin != NULL) ConfigurationSearch.AddOptionValue(page, control, plugin->GetDisplayName());
        }
    };
    const auto archiverChoices = [&](int control)
    {
        for (int index = 0; index < ArchiverConfig.GetArchiversCount(); ++index)
            ConfigurationSearch.AddOptionValue(page, control, ArchiverConfig.GetArchiverTitle(index));
    };
    if (At(page) == &PageAppear)
    {
        choices(IDC_THEME_MODE, {IDS_THEME_MODE_LIGHT, IDS_THEME_MODE_DARK, IDS_THEME_MODE_SYSTEM});
        for (const auto& icon : MainWindowIcons)
            ConfigurationSearch.AddOptionValue(page, IDC_TITLEBAR_ICON_INDEX, LoadStrOwned(icon.TextResID));
        choices(IDB_PANELFONT, {IDS_FONTDESCRIPTION_DEF, IDS_FONTDESCRIPTION_CST});
    }
    else if (At(page) == &PageGeneral)
    {
        for (int label : {IDS_UPDATECHECK_NEVER, IDS_UPDATECHECK_DAY, IDS_UPDATECHECK_WEEK,
                          IDS_UPDATECHECK_MONTH, IDS_UPDATECHECK_3MONTHS, IDS_UPDATECHECK_6MONTHS, IDS_UPDATECHECK_STARTUP})
            ConfigurationSearch.AddOptionValue(page, IDC_UPDATECHECK, LoadStrOwned(label));
    }
    else if (At(page) == &PageMainWindow)
        choices(IDC_TITLEBAR_MODE, {IDS_TITLEBAR_DIRECTORY, IDS_TITLEBAR_COMPOSITE, IDS_TITLEBAR_FULLPATH});
    else if (At(page) == &PagePanels)
    {
        choices(IDC_NAMEMANGLE, {IDS_NAMEMANGLE_ONTHEDISK, IDS_NAMEMANGLE_EXPLORER, IDS_NAMEMANGLE_VC,
                                IDS_NAMEMANGLE_PARTMIXEDCASE, IDS_NAMEMANGLE_LOWERCASE, IDS_NAMEMANGLE_UPPERCASE});
        choices(IDC_SIZEFORMAT, {IDS_SIZEMODE_BYTES, IDS_SIZEMODE_KB, IDS_SIZEMODE_MIXED});
    }
    else if (At(page) == &PageView)
        choices(IDC_VIEW_LIST, {IDS_BRIEF_VIEW_NAME, IDS_DETAILED_VIEW_NAME, IDS_ICONS_VIEW_NAME,
                                IDS_THUMBNAILS_VIEW_NAME, IDS_TILES_VIEW_NAME});
    else if (At(page) == &Page13 || At(page) == &Page14)
    {
        choices(IDC_VIEW_TYPE, {IDS_VIEWER_EXTERNAL, IDS_VIEWER_INTERNAL});
        pluginChoices(IDC_VIEW_TYPE, &CPlugins::GetViewerIndex);
    }
    else if (At(page) == &PageP1)
    {
        choices(IDC_P1_TYPE, {IDS_PUT_EXTERNAL});
        pluginChoices(IDC_P1_TYPE, &CPlugins::GetCustomPackerIndex);
    }
    else if (At(page) == &PageP2)
    {
        choices(IDC_P2_TYPE, {IDS_PUT_EXTERNAL});
        pluginChoices(IDC_P2_TYPE, &CPlugins::GetCustomUnpackerIndex);
    }
    else if (At(page) == &PageP3)
        archiverChoices(IDC_P3_LIST);
    else if (At(page) == &PageP4)
    {
        choices(IDC_P4_EDIT, {IDS_PUT_NONE});
        archiverChoices(IDC_P4_VIEW);
        archiverChoices(IDC_P4_EDIT);
        pluginChoices(IDC_P4_VIEW, &CPlugins::GetPanelViewIndex);
        pluginChoices(IDC_P4_EDIT, &CPlugins::GetPanelEditIndex);
    }
    else if (At(page) == &PageColors)
        IndexColorSearchMetadata(ConfigurationSearch, page);
    else if (At(page) == &PageSecurity)
        ConfigurationSearch.AddKeywords(page, IDC_SEC_ENABLE_MASTERPWD, LoadStrOwned(IDS_CFGSEC_MASTERPWD_INFO));
}

void CConfigurationDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // ColorsChanged() calls a plug-in method (when colors change PLUGINEVENT_COLORSCHANGED
        // is called) -> we must set the parent for their message boxes
        HOldPluginMsgBoxParent = PluginMsgBoxParent;
        PluginMsgBoxParent = Dialog.HWindow;
        const auto create = [&](DWORD extended, const wchar_t* className, DWORD style, int id, const wchar_t* text)
        {
            HWND control = CreateWindowExW(extended, className, text, WS_CHILD | WS_VISIBLE | style,
                                            0, 0, 0, 0, Dialog.HWindow,
                                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), HInstance, NULL);
            SendMessageW(control, WM_SETFONT, SendMessageW(Dialog.HWindow, WM_GETFONT, 0, 0), FALSE);
            return control;
        };
        SearchOpenCaption = LoadStrOwned(IDS_CFG_SEARCH_OPEN);
        SearchCloseCaption = LoadStrOwned(IDS_CFG_SEARCH_CLOSE);
        const std::wstring openLabel = SearchOpenCaption + L" (Ctrl+F)";
        SearchOpen = create(0, L"BUTTON", WS_TABSTOP | BS_OWNERDRAW, ConfigurationSearchOpenId, openLabel.c_str());
        SearchClose = create(0, L"BUTTON", WS_TABSTOP | BS_OWNERDRAW, ConfigurationSearchCloseId, SearchCloseCaption.c_str());
        SearchEdit = create(WS_EX_CLIENTEDGE, L"EDIT", WS_TABSTOP | ES_AUTOHSCROLL | WS_CLIPCHILDREN, ConfigurationSearchEditId, L"");
        SendMessageW(SearchEdit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(SearchOpenCaption.c_str()));
        SearchClear = create(0, L"BUTTON", WS_TABSTOP | BS_PUSHBUTTON | BS_FLAT, ConfigurationSearchClearId, L"\u00D7");
        CButton* clearButton = new CButton(Dialog.HWindow, ConfigurationSearchClearId, 0);
        if (clearButton != NULL)
            clearButton->SetToolTipText(LoadStrW(IDS_CFG_SEARCH_CLEAR));
        SetParent(SearchClear, SearchEdit);
        SetWindowSubclass(SearchEdit, ConfigurationSearchEditProc, 1, 0);
        EnableWindow(SearchClear, FALSE);
        SearchToolTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_ALWAYSTIP,
                                        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                        Dialog.HWindow, NULL, HInstance, NULL);
        if (SearchToolTip != NULL)
        {
            const auto tip = [&](HWND control, const std::wstring& text)
            {
                TOOLINFOW info = {sizeof(info)};
                info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                info.hwnd = Dialog.HWindow;
                info.uId = reinterpret_cast<UINT_PTR>(control);
                info.lpszText = const_cast<wchar_t*>(text.c_str());
                SendMessageW(SearchToolTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
            };
            tip(SearchOpen, SearchOpenCaption);
            tip(SearchClose, SearchCloseCaption);
        }
        SearchList = create(WS_EX_CLIENTEDGE, L"LISTBOX", WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_HSCROLL,
                            ConfigurationSearchListId, L"");
        ShowWindow(SearchList, SW_HIDE);
        DarkMode_ApplyListTreeThemeRecursive(Dialog.HWindow);
        RefreshLayout();
        UpdateApplyState();
        SetTimer(Dialog.HWindow, 0x4346, 150, NULL);
        MultiMonCenterWindow(Dialog.HWindow, Parent, TRUE);
        break;
    }

    case WM_DESTROY:
    {
        KillTimer(Dialog.HWindow, 0x4346);
        if (SearchToolTip != NULL)
        {
            DestroyWindow(SearchToolTip);
            SearchToolTip = NULL;
        }
        if (GetKeyState(VK_ESCAPE) & 0x8000) // prevent listing interruption in the panel after each ESC
            WaitForESCReleaseBeforeTestingESC = TRUE;

        PluginMsgBoxParent = HOldPluginMsgBoxParent;
        break;
    }

    case WM_SYSCOLORCHANGE:
    {
        int i;
        for (i = 0; i < Count; i++)
        {
            HWND w = At(i)->HWindow;
            if (w != NULL)
                PostMessage(w, WM_SYSCOLORCHANGE, 0, 0);
        }
        break;
    }

    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    {
        for (int i = 0; i < Count; i++)
        {
            HWND w = At(i)->HWindow;
            if (w != NULL)
                SendMessage(w, uMsg, wParam, lParam);
        }
        break;
    }
    }
}

//
// ****************************************************************************
// CCfgPageGeneral
//

CCfgPageGeneral::CCfgPageGeneral()
    : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGPAGE_GENERAL, IDD_CFGPAGE_GENERAL, PSP_USETITLE, NULL)
{
}

void CCfgPageGeneral::Validate(CTransferInfo& ti)
{
    BOOL useTimeRes;
    ti.CheckBox(IDC_TIMERESOLUTION, useTimeRes);
    if (useTimeRes)
    {
        int timerRes;
        ti.EditLine(IDE_TIMERESOLUTION, timerRes);
        if (timerRes < 0 || timerRes > 3600)
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_BADTIMERESOLUTION));
            ti.ErrorOn(IDE_TIMERESOLUTION);
        }
    }
}

void CCfgPageGeneral::Transfer(CTransferInfo& ti)
{
    ti.CheckBox(IDC_AUTOSAVE, Configuration.AutoSave);
    ti.CheckBox(IDC_CLOSESHELL, Configuration.CloseShell);
    ti.CheckBox(IDC_CLEARREADONLY, Configuration.ClearReadOnly);
    //  ti.CheckBox(IDC_FASTDIRMOVE, Configuration.FastDirectoryMove);
    ti.CheckBox(IDC_TIMERESOLUTION, Configuration.UseTimeResolution);
    ti.CheckBox(IDC_IGNOREDSTSHIFTS, Configuration.IgnoreDSTShifts);
    ti.EditLine(IDE_TIMERESOLUTION, Configuration.TimeResolution);
    //  ti.CheckBox(IDC_LANTASTICCHECK, Configuration.LantasticCheck);
    ti.CheckBox(IDC_ONLYONEINSTANCE, Configuration.OnlyOneInstance);
    ti.CheckBox(IDC_MINBEEPWHENDONE, Configuration.MinBeepWhenDone);
    ti.CheckBox(IDC_QUICKRENAME_SELALL, Configuration.QuickRenameSelectAll);
    ti.CheckBox(IDC_EDITNEW_SELALL, Configuration.EditNewSelectAll);
    ti.CheckBox(IDC_NETWAREFASTDIRMOVE, Configuration.NetwareFastDirMove);
    int dummy = 0;
    ti.CheckBox(IDC_ASYNCCOPYALG, Windows7AndLater ? Configuration.UseAsyncCopyAlg : dummy);
    int oldReloadEnvVariables = Configuration.ReloadEnvVariables;
    ti.CheckBox(IDC_RELOADENVVARS, Configuration.ReloadEnvVariables);
    ti.CheckBox(IDC_SUGGESTPATHNAMES, Configuration.SuggestPathNames);
    // How often utils\salupdate.exe looks for a newer release (update_check.cpp); in the order
    // of Sally::Update::UpdateCheckMode.
    if (ti.Type == ttDataToWindow)
    {
        const int names[] = {IDS_UPDATECHECK_NEVER, IDS_UPDATECHECK_DAY, IDS_UPDATECHECK_WEEK,
                             IDS_UPDATECHECK_MONTH, IDS_UPDATECHECK_3MONTHS, IDS_UPDATECHECK_6MONTHS,
                             IDS_UPDATECHECK_STARTUP};
        SendDlgItemMessage(HWindow, IDC_UPDATECHECK, CB_RESETCONTENT, 0, 0);
        for (int name : names)
            SendDlgItemMessageW(HWindow, IDC_UPDATECHECK, CB_ADDSTRING, 0, (LPARAM)LoadStrOwned(name).c_str());
        SendDlgItemMessage(HWindow, IDC_UPDATECHECK, CB_SETCURSEL, Configuration.UpdateCheckMode, 0);
    }
    else
    {
        const int mode = (int)SendDlgItemMessage(HWindow, IDC_UPDATECHECK, CB_GETCURSEL, 0, 0);
        if (mode >= 0 && Sally::Update::IsKnownUpdateCheckMode(static_cast<DWORD>(mode)))
            Configuration.UpdateCheckMode = mode;
    }
    if (ti.Type == ttDataFromWindow && Configuration.ReloadEnvVariables && oldReloadEnvVariables != Configuration.ReloadEnvVariables)
    {
        InitEnvironmentVariablesDifferences();
    }

    if (ti.Type == ttDataToWindow)
        EnableControls();
}

void CCfgPageGeneral::EnableControls()
{
    BOOL useTimeRes = IsDlgButtonChecked(HWindow, IDC_TIMERESOLUTION);
    EnableWindow(GetDlgItem(HWindow, IDE_TIMERESOLUTION), useTimeRes);
    EnableWindow(GetDlgItem(HWindow, IDC_ASYNCCOPYALG), Windows7AndLater);
}

INT_PTR
CCfgPageGeneral::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_COMMAND:
    {
        if (HIWORD(wParam) == BN_CLICKED)
            EnableControls();
        break;
    }
    }
    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CCfgPageRegional
//

CCfgPageRegional::CCfgPageRegional()
    : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGPAGE_REGIONAL, IDD_CFGPAGE_REGIONAL, PSP_USETITLE, NULL)
{
    SLGName = Configuration.SLGName;
    DirName = Configuration.ConversionTable;
}

void CCfgPageRegional::LoadControls()
{
    CLanguage language;
    if (language.Init(sally::languages::LangIdFromPersistedName(SLGName.c_str())))
        SetDlgItemTextW(HWindow, IDE_LANGUAGE, language.DisplayName.c_str());
}

void CCfgPageRegional::Transfer(CTransferInfo& ti)
{
    if (ti.Type == ttDataToWindow)
    {
        LoadControls();
    }
    else
    {
        if (StrICmpW(Configuration.SLGName.c_str(), SLGName.c_str()) != 0)
        {
            gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_LANGUAGE_CHANGE));
            Configuration.SLGName = SLGName;
        }
        // if the table has changed and the old one is already loaded we need to restart Salamander
        if (StrICmpW(Configuration.ConversionTable.c_str(), DirName.c_str()) != 0)
        {
            Configuration.ConversionTable = DirName;
            if (CodeTables.IsLoaded())
            {
                gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_CONVERSION_CHANGE));
            }
        }
    }
}

INT_PTR
CCfgPageRegional::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDB_LANGUAGE:
        {
            CLanguageSelectorDialog dlg(HWindow, SLGName);
            dlg.Initialize();
            if (dlg.Execute() == IDOK)
            {
                LoadControls();
                PostMessage(GetParent(), WM_NEXTDLGCTL, (WPARAM)GetDlgItem(GetParent(), 5 /* _TPD_IDC_OK */), TRUE);
            }
            return 0;
        }

        case IDB_CONVERSION:
        {
            CConversionTablesDialog dlg(HWindow, DirName);
            dlg.Execute();
            return 0;
        }
        }
        break;
    }
    }
    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CCfgPageView
//

// used only to suppress quick search in the list view
class CMyListView : public CWindow
{
public:
    CMyListView(HWND hDlg, int ctrlID) : CWindow(hDlg, ctrlID) {}

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        if (uMsg == WM_CHAR)
        {
            PostMessage(GetParent(HWindow), WM_USER_CHAR, wParam, lParam);
            return 0;
        }

        if (uMsg == WM_LBUTTONDBLCLK)
        {
            int index = ListView_GetNextItem(HWindow, -1, LVNI_SELECTED);
            ListView_EditLabel(HWindow, index);
            return 0;
        }

        return CWindow::WindowProc(uMsg, wParam, lParam);
    }
};

CCfgPageView::CCfgPageView(int index)
    : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGPAGE_VIEWS, IDD_CFGPAGE_VIEWS, PSP_USETITLE, NULL)
{
    for (int i = 0; i < VIEW_TEMPLATES_COUNT; ++i)
        OriginalTemplateIndex[i] = i;
    Dirty = FALSE;
    Header = NULL;
    HListView = NULL;
    Header2 = NULL;
    HListView2 = NULL;
    DisableNotification = FALSE;
    LabelEdit = FALSE;
    if (index == -1)
        index = MainWindow->GetActivePanel()->GetViewTemplateIndex();
    SelectIndex = index;
}

BOOL CCfgPageView::IsDirty()
{
    return Dirty;
}

void CCfgPageView::Transfer(CTransferInfo& ti)
{
    if (ti.Type == ttDataToWindow)
    {
        Config.Load(MainWindow->ViewTemplates);
        for (int i = 0; i < VIEW_TEMPLATES_COUNT; ++i)
            OriginalTemplateIndex[i] = i;

        DisableNotification = TRUE;
        wchar_t buff[20];
        int i;
        for (i = 0; i < VIEW_TEMPLATES_COUNT; i++)
        {
            LVITEMW lvi;
            lvi.mask = LVIF_TEXT | LVIF_STATE;
            lvi.iItem = i;
            lvi.iSubItem = 0;
            lvi.state = 0;
            lvi.pszText = const_cast<wchar_t*>(Config.Items[i].Name.c_str());
            SendMessageW(HListView, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

            switch (Config.Items[i].Mode)
            {
            case VIEW_MODE_TREE:
                lstrcpyW(buff, LoadStrW(IDS_TREE_VIEW_NAME));
                break;
            case VIEW_MODE_BRIEF:
                lstrcpyW(buff, LoadStrW(IDS_BRIEF_VIEW_NAME));
                break;
            case VIEW_MODE_DETAILED:
                lstrcpyW(buff, LoadStrW(IDS_DETAILED_VIEW_NAME));
                break;
            case VIEW_MODE_ICONS:
                lstrcpyW(buff, LoadStrW(IDS_ICONS_VIEW_NAME));
                break;
            case VIEW_MODE_THUMBNAILS:
                lstrcpyW(buff, LoadStrW(IDS_THUMBNAILS_VIEW_NAME));
                break;
            case VIEW_MODE_TILES:
                lstrcpyW(buff, LoadStrW(IDS_TILES_VIEW_NAME));
                break;
            }
            ListView_SetItemTextW(HListView, i, 1, buff);

            swprintf(buff, 20, L"Alt+%d", i < VIEW_TEMPLATES_COUNT - 1 ? i + 1 : 0);
            ListView_SetItemTextW(HListView, i, 2, buff);
        }
        // set column widths
        ListView_SetColumnWidth(HListView, 0, LVSCW_AUTOSIZE_USEHEADER);
        int w = ListView_GetColumnWidth(HListView, 0);
        w += 30;
        ListView_SetColumnWidth(HListView, 0, w);
        ListView_SetColumnWidth(HListView, 1, LVSCW_AUTOSIZE_USEHEADER);
        ListView_SetColumnWidth(HListView, 2, LVSCW_AUTOSIZE_USEHEADER);

        DWORD state = LVIS_SELECTED | LVIS_FOCUSED;
        ListView_SetItemState(HListView, SelectIndex, state, state);
        DisableNotification = FALSE;
        LoadControls();
        EnableHeader();
    }
    else
    {
        MainWindow->ViewTemplates.Load(Config);
        const auto preserveChoice = [&](CFilesWindow* panel)
        {
            if (panel == NULL)
                return;
            const int oldIndex = panel->GetViewTemplateIndex();
            for (int i = 0; i < VIEW_TEMPLATES_COUNT; ++i)
            {
                if (OriginalTemplateIndex[i] == oldIndex)
                {
                    panel->ViewTemplate = &MainWindow->ViewTemplates.Items[i];
                    break;
                }
            }
        };
        preserveChoice(MainWindow->LeftPanel);
        preserveChoice(MainWindow->RightPanel);
        // Apply establishes the baseline for the next draft on this open page.
        for (int i = 0; i < VIEW_TEMPLATES_COUNT; ++i)
            OriginalTemplateIndex[i] = i;
    }
}

void CCfgPageView::Validate(CTransferInfo& ti)
{
    for (int id : {IDC_VIEW_LEFT_WIDTH, IDC_VIEW_RIGHT_WIDTH})
    {
        HWND control = GetDlgItem(HWindow, id);
        if (control == NULL || !IsWindowEnabled(control))
            continue;
        unsigned width;
        if (!sally::configuration::TryParseColumnWidth(GetWindowTextStringW(control), width))
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_INVALIDNUMBER));
            ti.ErrorOn(id);
            return;
        }
    }
}

const int CFGP2ItemsCount = 8 /*9*/;
const int CFGP2Flags[CFGP2ItemsCount] = {0, VIEW_SHOW_EXTENSION, VIEW_SHOW_DOSNAME, VIEW_SHOW_SIZE, VIEW_SHOW_TYPE, VIEW_SHOW_DATE, VIEW_SHOW_TIME, VIEW_SHOW_ATTRIBUTES /*, VIEW_SHOW_DESCRIPTION*/};
const int CFGP2ResID[CFGP2ItemsCount] = {IDS_COLUMN_CFG_NAME, IDS_COLUMN_CFG_EXT, IDS_COLUMN_CFG_DOSNAME, IDS_COLUMN_CFG_SIZE, IDS_COLUMN_CFG_TYPE, IDS_COLUMN_CFG_DATE, IDS_COLUMN_CFG_TIME, IDS_COLUMN_CFG_ATTR /*,  IDS_COLUMN_CFG_DESC*/};

void CCfgPageView::LoadControls()
{
    DisableNotification = TRUE;
    int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);

    BOOL checked[CFGP2ItemsCount];

    BOOL empty = TRUE;
    if (index >= 2 && index != 3 && index != 4 && index != 5) // tree a brief ma disabled checkboxy
    {
        checked[0] = TRUE;
        int i;
        for (i = 1; i < CFGP2ItemsCount; i++)
            checked[i] = (Config.Items[index].Flags & CFGP2Flags[i]) ? TRUE : FALSE;
        empty = FALSE;
    }
    DWORD count = ListView_GetItemCount(HListView2);
    if (empty && count > 0)
        ListView_DeleteAllItems(HListView2);
    if (!empty && count == 0)
    {
        int i;
        for (i = 0; i < CFGP2ItemsCount; i++)
        {
            LVITEMW lvi;
            lvi.mask = LVIF_TEXT | LVIF_STATE;
            lvi.iItem = i;
            lvi.iSubItem = 0;
            lvi.state = 0;
            lvi.pszText = LoadStrW(CFGP2ResID[i]);
            SendMessageW(HListView2, LVM_INSERTITEMW, 0, (LPARAM)&lvi);
        }
        ListView_SetItemState(HListView2, 0, LVIS_FOCUSED | LVIS_SELECTED, LVIS_FOCUSED | LVIS_SELECTED);
        ListView_SetColumnWidth(HListView2, 0, LVSCW_AUTOSIZE_USEHEADER);
    }
    int index2 = -1;
    if (!empty)
    {
        int i;
        for (i = 0; i < CFGP2ItemsCount; i++)
        {
            UINT state = INDEXTOSTATEIMAGEMASK((checked[i] ? 2 : 1));
            ListView_SetItemState(HListView2, i, state, LVIS_STATEIMAGEMASK);
        }
        CTransferInfo ti(HWindow, ttDataToWindow);
        int tmp = Config.Items[index].LeftSmartMode;
        ti.CheckBox(IDC_VIEW_LEFT_SMARTMODE, tmp);
        tmp = Config.Items[index].RightSmartMode;
        ti.CheckBox(IDC_VIEW_RIGHT_SMARTMODE, tmp);
        index2 = ListView_GetNextItem(HListView2, -1, LVNI_SELECTED);
        if (index2 != -1)
        {
            tmp = Config.Items[index].Columns[index2].LeftFixedWidth;
            ti.CheckBox(IDC_VIEW_LEFT_FIXED, tmp);
            tmp = Config.Items[index].Columns[index2].RightFixedWidth;
            ti.CheckBox(IDC_VIEW_RIGHT_FIXED, tmp);
            tmp = Config.Items[index].Columns[index2].LeftWidth;
            ti.EditLine(IDC_VIEW_LEFT_WIDTH, tmp);
            tmp = Config.Items[index].Columns[index2].RightWidth;
            ti.EditLine(IDC_VIEW_RIGHT_WIDTH, tmp);
        }
    }

    EnableControls();
    DisableNotification = FALSE;
}

void CCfgPageView::StoreControls()
{
    int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
    if (index >= 2)
    {
        DWORD flags = 0;
        int i;
        for (i = 1; i < CFGP2ItemsCount; i++)
        {
            DWORD state = ListView_GetItemState(HListView2, i, LVIS_STATEIMAGEMASK);
            if (state == INDEXTOSTATEIMAGEMASK(2))
                flags |= CFGP2Flags[i];
        }
        Config.Items[index].Flags = flags;
        Dirty = TRUE;
    }
}

void CCfgPageView::EnableControls()
{
    int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
    BOOL enable = TRUE;
    if (index == -1 || index < 2 || index == 3 || index == 4 || index == 5 || Config.Items[index].Name.empty())
        enable = FALSE;

    int index2 = ListView_GetNextItem(HListView2, -1, LVNI_SELECTED);
    BOOL supportFixedWidth = (index2 != -1 && enable);

    EnableWindow(HListView2, enable);
    CTransferInfo ti(HWindow, ttDataToWindow);
    int tmp = 0;
    if (!supportFixedWidth)
    {
        ti.CheckBox(IDC_VIEW_LEFT_FIXED, tmp);
        ti.CheckBox(IDC_VIEW_RIGHT_FIXED, tmp);
        wchar_t buff[] = L"";
        ti.EditLineW(IDC_VIEW_LEFT_WIDTH, buff, 1);
        ti.EditLineW(IDC_VIEW_RIGHT_WIDTH, buff, 1);
    }
    if (!enable)
    {
        ti.CheckBox(IDC_VIEW_LEFT_SMARTMODE, tmp);
        ti.CheckBox(IDC_VIEW_RIGHT_SMARTMODE, tmp);
    }
    BOOL enableLeftEdit = supportFixedWidth && IsDlgButtonChecked(HWindow, IDC_VIEW_LEFT_FIXED) == BST_CHECKED;
    BOOL enableRightEdit = supportFixedWidth && IsDlgButtonChecked(HWindow, IDC_VIEW_RIGHT_FIXED) == BST_CHECKED;

    EnableWindow(GetDlgItem(HWindow, IDC_VIEW_TEXT4), enable);
    EnableWindow(GetDlgItem(HWindow, IDC_VIEW_TEXT6), enable);
    EnableWindow(GetDlgItem(HWindow, IDC_VIEW_LEFT_FIXED), supportFixedWidth);
    EnableWindow(GetDlgItem(HWindow, IDC_VIEW_RIGHT_FIXED), supportFixedWidth);
    EnableWindow(GetDlgItem(HWindow, IDC_VIEW_LEFT_WIDTH), enableLeftEdit);
    EnableWindow(GetDlgItem(HWindow, IDC_VIEW_RIGHT_WIDTH), enableRightEdit);
    EnableWindow(GetDlgItem(HWindow, IDC_VIEW_TEXT2), supportFixedWidth);
    EnableWindow(GetDlgItem(HWindow, IDC_VIEW_TEXT5), supportFixedWidth);
    EnableWindow(GetDlgItem(HWindow, IDC_VIEW_TEXT7), enable);
    EnableWindow(GetDlgItem(HWindow, IDC_VIEW_LEFT_SMARTMODE), enable);
    EnableWindow(GetDlgItem(HWindow, IDC_VIEW_RIGHT_SMARTMODE), enable);
}

DWORD
CCfgPageView::GetEnabledFunctions()
{
    DWORD mask = 0;
    if (!LabelEdit)
    {
        int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
        if (index > 6)
        {
            mask |= TLBHDRMASK_MODIFY;
            if (!Config.Items[index].Name.empty())
            {
                mask |= TLBHDRMASK_DELETE;
                if (index > 7)
                    mask |= TLBHDRMASK_UP;
                if (index < VIEW_TEMPLATES_COUNT - 1)
                    mask |= TLBHDRMASK_DOWN;
            }
        }
    }
    return mask;
}

void CCfgPageView::EnableHeader()
{
    Header->EnableToolbar(GetEnabledFunctions());
}

void CCfgPageView::OnModify()
{
    if ((GetEnabledFunctions() & TLBHDRMASK_MODIFY) == 0)
        return;
    Dirty = TRUE;
    int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
    if (index != -1)
        PostMessage(HListView, LVM_EDITLABEL, index, 0);
}

void CCfgPageView::OnDelete()
{
    if ((GetEnabledFunctions() & TLBHDRMASK_DELETE) == 0)
        return;
    Dirty = TRUE;
    int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
    if (index != -1)
    {
        Config.Items[index].Name.clear();
        wchar_t buff[] = L"";
        ListView_SetItemTextW(HListView, index, 0, buff);
        LoadControls();
    }
    EnableHeader();
}

void CCfgPageView::OnMove(BOOL up)
{
    CALL_STACK_MESSAGE2("CCfgPageView::OnMove(%d)", up);
    DWORD mask = GetEnabledFunctions();
    if (up && (mask & TLBHDRMASK_UP) == 0 ||
        !up && (mask & TLBHDRMASK_DOWN) == 0)
        return;
    Dirty = TRUE;
    DisableNotification = TRUE;
    int index1 = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
    int index2 = index1;
    if (index1 == -1)
        return;
    if (up && index1 > 0)
        index2 = index1 - 1;
    if (!up && index1 < VIEW_TEMPLATES_COUNT - 1)
        index2 = index1 + 1;
    if (index2 != index1)
    {
        ListView_SetItemTextW(HListView, index1, 0, const_cast<wchar_t*>(Config.Items[index2].Name.c_str()));
        ListView_SetItemTextW(HListView, index2, 0, const_cast<wchar_t*>(Config.Items[index1].Name.c_str()));
        DWORD state1 = ListView_GetItemState(HListView, index1, LVIS_STATEIMAGEMASK);
        DWORD state2 = ListView_GetItemState(HListView, index2, LVIS_STATEIMAGEMASK);
        ListView_SetItemState(HListView, index1, state2, LVIS_STATEIMAGEMASK);
        state1 |= LVIS_FOCUSED | LVIS_SELECTED;
        ListView_SetItemState(HListView, index2, state1, LVIS_STATEIMAGEMASK | LVIS_FOCUSED | LVIS_SELECTED);
        if (Config.SwapItems(index1, index2))
            std::swap(OriginalTemplateIndex[index1], OriginalTemplateIndex[index2]);
        LoadControls();
    }
    DisableNotification = FALSE;
    EnableHeader();
}

INT_PTR
CCfgPageView::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CCfgPageView::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        CMyListView* listView = new CMyListView(HWindow, IDC_VIEW_LIST);
        HListView = listView->HWindow;
        HListView2 = GetDlgItem(HWindow, IDC_VIEW_LIST2);
        Header = new CToolbarHeader(HWindow, IDC_VIEWLIST_HEADER, HListView,
                                    TLBHDRMASK_MODIFY | TLBHDRMASK_DELETE |
                                        TLBHDRMASK_UP | TLBHDRMASK_DOWN);
        if (Header == NULL)
            TRACE_E(LOW_MEMORY);

        Header2 = new CToolbarHeader(HWindow, IDC_VIEWLIST_HEADER2, HListView2, 0);
        if (Header2 == NULL)
            TRACE_E(LOW_MEMORY);

        DWORD exFlags = LVS_EX_FULLROWSELECT /*| LVS_EX_CHECKBOXES*/;
        DWORD origFlags = ListView_GetExtendedListViewStyle(HListView);
        ListView_SetExtendedListViewStyle(HListView, origFlags | exFlags); // 4.71

        exFlags = LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES;
        origFlags = ListView_GetExtendedListViewStyle(HListView2);
        ListView_SetExtendedListViewStyle(HListView2, origFlags | exFlags); // 4.71

        // determine the list view size
        RECT r;
        GetClientRect(HListView, &r);

        // fill the list view with Name, Mode and HotKey columns
        LVCOLUMNW lvc;
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        lvc.pszText = LoadStrW(IDS_HOTPATH_NAME);
        lvc.cx = 1; // dummy
        lvc.fmt = LVCFMT_LEFT;
        lvc.iSubItem = 0;
        SendMessageW(HListView, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);

        lvc.mask |= LVCF_SUBITEM;
        lvc.pszText = LoadStrW(IDS_VIEW_MODE);
        lvc.iSubItem = 1;
        SendMessageW(HListView, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);
        ListView_SetColumnWidth(HListView, 1, LVSCW_AUTOSIZE_USEHEADER);

        lvc.mask |= LVCF_SUBITEM;
        lvc.pszText = LoadStrW(IDS_HOTPATH_HOTKEY);
        lvc.iSubItem = 2;
        SendMessageW(HListView, LVM_INSERTCOLUMNW, 2, (LPARAM)&lvc);
        ListView_SetColumnWidth(HListView, 2, LVSCW_AUTOSIZE_USEHEADER);

        // insert the Name column into the list view with columns
        GetClientRect(HListView2, &r);
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        lvc.pszText = LoadStrW(IDS_COLUMN_NAME);
        lvc.fmt = LVCFMT_LEFT;
        lvc.iSubItem = 0;
        SendMessageW(HListView2, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);

        // dialog elements should stretch with the dialog size, set split controls
        ElasticVerticalLayout(2, IDC_VIEW_LIST, IDC_VIEW_LIST2);

        break;
    }

    case WM_SYSCOLORCHANGE:
    {
        ListView_SetBkColor(HListView, GetSysColor(COLOR_WINDOW));
        ListView_SetBkColor(HListView2, GetSysColor(COLOR_WINDOW));
        break;
    }

    case WM_USER_CHAR:
    {
        int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
        if (wParam != ' ' && !LabelEdit)
        {
            ListView_EditLabel(HListView, index);
            HWND hEdit = ListView_GetEditControl(HListView);
            SendMessage(hEdit, WM_CHAR, wParam, 0);
        }
        return 0;
    }

    case WM_NOTIFY:
    {
        if (DisableNotification)
            break;

        if (wParam == IDC_VIEW_LIST2)
        {
            LPNMHDR nmh = (LPNMHDR)lParam;
            switch (nmh->code)
            {
            case NM_DBLCLK:
            {
                LVHITTESTINFO ht;
                DWORD pos = GetMessagePos();
                ht.pt.x = GET_X_LPARAM(pos);
                ht.pt.y = GET_Y_LPARAM(pos);
                ScreenToClient(HListView2, &ht.pt);
                ListView_HitTest(HListView2, &ht);
                int index = ListView_GetNextItem(HListView2, -1, LVNI_SELECTED);
                if (index != -1 && ht.iItem == index && ht.flags != LVHT_ONITEMICON)
                {
                    UINT state = ListView_GetItemState(HListView2, index, LVIS_STATEIMAGEMASK);
                    if (state == INDEXTOSTATEIMAGEMASK(2))
                        state = INDEXTOSTATEIMAGEMASK(1);
                    else
                        state = INDEXTOSTATEIMAGEMASK(2);
                    ListView_SetItemState(HListView2, index, state, LVIS_STATEIMAGEMASK);
                }
                break;
            }

            case LVN_ITEMCHANGING:
            {
                LPNMLISTVIEW nmhi = (LPNMLISTVIEW)nmh;
                // disagree :-) beep when attempting to hide the Name column
                if (nmhi->iItem == 0 && (nmhi->uOldState & 0xF000) != (nmhi->uNewState & 0xF000))
                {
                    MessageBeep(MB_ICONASTERISK);
                    SetWindowLongPtr(HWindow, DWLP_MSGRESULT, TRUE);
                }
                else
                    SetWindowLongPtr(HWindow, DWLP_MSGRESULT, FALSE);

                return TRUE;
            }

            case LVN_ITEMCHANGED:
            {
                LPNMLISTVIEW nmhi = (LPNMLISTVIEW)nmh;
                if ((nmhi->uOldState & 0xF000) != (nmhi->uNewState & 0xF000))
                {
                    StoreControls(); // save data when the checkbox is clicked
                    EnableControls();
                }
                else if (!(nmhi->uOldState & LVIS_SELECTED) && nmhi->uNewState & LVIS_SELECTED)
                    LoadControls(); // on column change, load new data to the list and edit line
                break;
            }
            }
            break;
        }

        if (wParam == IDC_VIEW_LIST)
        {
            LPNMHDR nmh = (LPNMHDR)lParam;
            switch (nmh->code)
            {
            case LVN_KEYDOWN:
            {
                LPNMLVKEYDOWN nmhk = (LPNMLVKEYDOWN)nmh;
                int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
                if (nmhk->wVKey == VK_F2)
                {
                    OnModify();
                }
                if (nmhk->wVKey == VK_DELETE)
                {
                    OnDelete();
                }
                if ((GetKeyState(VK_MENU) & 0x8000) &&
                    (nmhk->wVKey == VK_UP || nmhk->wVKey == VK_DOWN))
                {
                    OnMove(nmhk->wVKey == VK_UP);
                }
                if (!LabelEdit && nmhk->wVKey == VK_INSERT)
                    ListView_EditLabel(HListView, index);
                break;
            }

            case LVN_ITEMCHANGED:
            {
                LPNMLISTVIEW nmhi = (LPNMLISTVIEW)nmh;
                if (!(nmhi->uOldState & LVIS_SELECTED) && nmhi->uNewState & LVIS_SELECTED)
                {
                    LoadControls();
                }

                //            if (nmhi->uOldState & 0x2000 && nmhi->uNewState & 0x1000 ||
                //                nmhi->uOldState & 0x1000 && nmhi->uNewState  & 0x2000)
                //            {
                //              BOOL checked = nmhi->uNewState & 0x2000;
                //              Config->SetVisible(nmhi->iItem, checked);
                //            }

                EnableHeader();
                break;
            }

            case LVN_BEGINLABELEDIT:
            {
                if (GetEnabledFunctions() & TLBHDRMASK_MODIFY)
                {
                    LabelEdit = TRUE;
                    EnableHeader();
                }
                else
                {
                    SetWindowLongPtr(HWindow, DWLP_MSGRESULT, TRUE);
                    return TRUE;
                }
                break;
            }

            case LVN_ENDLABELEDIT:
            {
                NMLVDISPINFOW* nmhd = (NMLVDISPINFOW*)nmh;
                LabelEdit = FALSE;
                EnableHeader();
                if (nmhd->item.pszText != NULL)
                {
                    std::wstring name = nmhd->item.pszText;
                    Config.CleanName(name);
                    int index = nmhd->item.iItem;
                    if (Config.Items[index].Name.empty())
                        Config.Items[index].Flags = 0;
                    Config.Items[index].Name = name;
                    LoadControls();
                    ListView_SetItemTextW(HListView, index, 0, const_cast<wchar_t*>(name.c_str()));
                    Dirty = TRUE;
                    break;
                }
                break;
            }
            }
        }
        break;
    }

    case WM_COMMAND:
    {
        if (!DisableNotification && HIWORD(wParam) == BN_CLICKED)
        {
            if (LOWORD(wParam) == IDC_VIEW_LEFT_FIXED || LOWORD(wParam) == IDC_VIEW_RIGHT_FIXED ||
                LOWORD(wParam) == IDC_VIEW_LEFT_SMARTMODE || LOWORD(wParam) == IDC_VIEW_RIGHT_SMARTMODE)
            {
                int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
                int index2 = ListView_GetNextItem(HListView2, -1, LVNI_SELECTED);
                if (index >= 2 && index2 != -1)
                {
                    BOOL checked = IsDlgButtonChecked(HWindow, LOWORD(wParam)) == BST_CHECKED;
                    switch (LOWORD(wParam))
                    {
                    case IDC_VIEW_LEFT_FIXED:
                        Config.Items[index].Columns[index2].LeftFixedWidth = checked ? 1 : 0;
                        break;
                    case IDC_VIEW_RIGHT_FIXED:
                        Config.Items[index].Columns[index2].RightFixedWidth = checked ? 1 : 0;
                        break;
                    case IDC_VIEW_LEFT_SMARTMODE:
                        Config.Items[index].LeftSmartMode = checked;
                        break;
                    case IDC_VIEW_RIGHT_SMARTMODE:
                        Config.Items[index].RightSmartMode = checked;
                        break;
                    }
                    Dirty = TRUE;
                }
                EnableControls();
            }
            break;
        }

        if (!DisableNotification && HIWORD(wParam) == EN_CHANGE)
        {
            if (LOWORD(wParam) == IDC_VIEW_LEFT_WIDTH || LOWORD(wParam) == IDC_VIEW_RIGHT_WIDTH)
            {
                int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
                int index2 = ListView_GetNextItem(HListView2, -1, LVNI_SELECTED);
                if (index >= 2 && index2 != -1)
                {
                    const int id = LOWORD(wParam);
                    unsigned width;
                    if (sally::configuration::TryParseColumnWidth(GetWindowTextStringW(GetDlgItem(HWindow, id)), width))
                    {
                        if (id == IDC_VIEW_LEFT_WIDTH)
                            Config.Items[index].Columns[index2].LeftWidth = width;
                        else
                            Config.Items[index].Columns[index2].RightWidth = width;
                        Dirty = TRUE;
                    }
                }
            }
            break;
        }

        if (LOWORD(wParam) == IDC_VIEWLIST_HEADER)
        {
            if (GetFocus() != HListView)
                SetFocus(HListView);
            switch (HIWORD(wParam))
            {
            case TLBHDR_MODIFY:
                OnModify();
                break;
            case TLBHDR_DELETE:
                OnDelete();
                break;
            case TLBHDR_UP:
                OnMove(TRUE);
                break;
            case TLBHDR_DOWN:
                OnMove(FALSE);
                break;
            }
        }
        break;
    }
    }

    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CCfgPageViewer
//

CCfgPageViewer::CCfgPageViewer()
    : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGPAGE_VIEWER, IDD_CFGPAGE_VIEWER, PSP_USETITLE, NULL)
{
    HFont = NULL;
    LocalUseCustomViewerFont = UseCustomViewerFont;
    memcpy(&LocalViewerLogFont, &ViewerLogFont, sizeof(LocalViewerLogFont));
    NormalText = NULL;
    SelectedText = NULL;
}

CCfgPageViewer::~CCfgPageViewer()
{
    if (HFont != NULL)
        HANDLES(DeleteObject(HFont));
}

void CCfgPageViewer::Validate(CTransferInfo& ti)
{
    int dummy;
    ti.EditLine(IDC_TABSIZE, dummy);
    if (dummy <= 0 || dummy > 30)
    {
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_BADTABSIZE));
        ti.ErrorOn(IDC_TABSIZE);
    }

    if (ti.IsGood())
    {
        const std::wstring candidate = GetWindowTextStringW(GetDlgItem(HWindow, IDC_VIEW_INTEXT));
        CMaskGroup candidateMasks(candidate.c_str(), Configuration.TextModeMasks.GetExtendedMode());
        int errorPos;
        if (!candidateMasks.PrepareMasks(errorPos))
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_INCORRECTSYNTAX));
            SetFocus(GetDlgItem(HWindow, IDC_VIEW_INTEXT));
            SendMessage(GetDlgItem(HWindow, IDC_VIEW_INTEXT), EM_SETSEL, errorPos, errorPos + 1);
            ti.ErrorOn(IDC_VIEW_INTEXT);
        }
    }

    if (ti.IsGood())
    {
        const std::wstring candidate = GetWindowTextStringW(GetDlgItem(HWindow, IDC_VIEW_INHEX));
        CMaskGroup candidateMasks(candidate.c_str(), Configuration.HexModeMasks.GetExtendedMode());
        int errorPos;
        if (!candidateMasks.PrepareMasks(errorPos))
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_INCORRECTSYNTAX));
            SetFocus(GetDlgItem(HWindow, IDC_VIEW_INHEX));
            SendMessage(GetDlgItem(HWindow, IDC_VIEW_INHEX), EM_SETSEL, errorPos, errorPos + 1);
            ti.ErrorOn(IDC_VIEW_INHEX);
        }
    }
}

void CCfgPageViewer::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CCfgPageViewer::Transfer()");
    if (ti.Type == ttDataToWindow)
        LoadControls();
    ti.CheckBox(IDC_COPYFINDTEXT, Configuration.CopyFindText);
    ti.CheckBox(IDC_CRLFEOL, Configuration.EOL_CRLF);
    ti.CheckBox(IDC_CREOL, Configuration.EOL_CR);
    ti.CheckBox(IDC_LFEOL, Configuration.EOL_LF);
    ti.CheckBox(IDC_NULLEOL, Configuration.EOL_NULL);
    ti.EditLine(IDC_TABSIZE, Configuration.TabSize);
    const int oldSavePosition = Configuration.SavePosition;
    ti.RadioButton(IDC_SAVEONCLOSE, 1, Configuration.SavePosition);
    ti.RadioButton(IDC_SETBYMAINWINDOW, 0, Configuration.SavePosition);
    if (ti.Type == ttDataFromWindow && Configuration.SavePosition && !oldSavePosition)
        Configuration.WindowPlacement.length = 0;

    if (ti.Type == ttDataToWindow)
    {
        SetDlgItemTextW(HWindow, IDC_VIEW_INHEX, Configuration.HexModeMasks.GetMasksString());
        SetDlgItemTextW(HWindow, IDC_VIEW_INTEXT, Configuration.TextModeMasks.GetMasksString());
    }
    else
    {
        const std::wstring hexMasks = GetWindowTextStringW(GetDlgItem(HWindow, IDC_VIEW_INHEX));
        const std::wstring textMasks = GetWindowTextStringW(GetDlgItem(HWindow, IDC_VIEW_INTEXT));
        Configuration.HexModeMasks.SetMasksString(hexMasks.c_str());
        Configuration.TextModeMasks.SetMasksString(textMasks.c_str());
    }
    int errPos;
    Configuration.TextModeMasks.PrepareMasks(errPos);
    Configuration.HexModeMasks.PrepareMasks(errPos);

    if (ti.Type == ttDataToWindow)
    {
        int i;
        for (i = 0; i < NUMBER_OF_VIEWERCOLORS; i++)
            TmpColors[i] = ViewerColors[i];
        NormalText->SetColor(GetCOLORREF(TmpColors[VIEWER_FG_NORMAL]), GetCOLORREF(TmpColors[VIEWER_BK_NORMAL]));
        SelectedText->SetColor(GetCOLORREF(TmpColors[VIEWER_FG_SELECTED]), GetCOLORREF(TmpColors[VIEWER_BK_SELECTED]));
    }
    else
    {
        UseCustomViewerFont = LocalUseCustomViewerFont;
        if (memcmp(&ViewerLogFont, &LocalViewerLogFont, sizeof(LocalViewerLogFont)) != 0)
        { // when the font changes we must measure the new one
            HANDLES(EnterCriticalSection(&ViewerFontMeasureCS));
            memcpy(&ViewerLogFont, &LocalViewerLogFont, sizeof(LocalViewerLogFont));
            ViewerFontMeasured = FALSE;
            HANDLES(LeaveCriticalSection(&ViewerFontMeasureCS));
        }
        BOOL colorChanged = FALSE;
        int i;
        for (i = 0; i < NUMBER_OF_VIEWERCOLORS; i++)
        {
            if (ViewerColors[i] != TmpColors[i])
            {
                ViewerColors[i] = TmpColors[i];
                colorChanged = TRUE;
            }
        }
        UpdateViewerColors(ViewerColors);

        // broadcast this change to the plug-ins as well
        if (colorChanged)
            Plugins.Event(PLUGINEVENT_COLORSCHANGED, 0);

        // after closing the dialog BroadcastConfigChanged will be called
    }
}

void CCfgPageViewer::LoadControls()
{
    CALL_STACK_MESSAGE1("CCfgPageViewer::LoadControls()");

    LOGFONT logFont;
    if (LocalUseCustomViewerFont)
        logFont = LocalViewerLogFont;
    else
        GetDefaultViewerLogFont(&logFont);

    HWND hEdit = GetDlgItem(HWindow, IDE_VIEWERFONT);
    int origHeight = logFont.lfHeight;
    logFont.lfHeight = GetWindowFontHeight(hEdit); // use the edit line font size for font presentation
    if (HFont != NULL)
        HANDLES(DeleteObject(HFont));
    HFont = HANDLES(CreateFontIndirect(&logFont));
    HDC hDC = HANDLES(GetDC(HWindow));
    SendMessage(hEdit, WM_SETFONT, (WPARAM)HFont, MAKELPARAM(TRUE, 0));
    wchar_t buf[LF_FACESIZE + 200];
    swprintf_s(buf, _countof(buf), LoadStrW(IDS_FONTDESCRIPTION),
               MulDiv(-origHeight, 72, GetDeviceCaps(hDC, LOGPIXELSY)),
               LocalViewerLogFont.lfFaceName,
               LoadStrW(LocalUseCustomViewerFont ? IDS_FONTDESCRIPTION_CST : IDS_FONTDESCRIPTION_DEF));
    SetWindowTextW(hEdit, buf);

    HANDLES(ReleaseDC(HWindow, hDC));
}

INT_PTR
CCfgPageViewer::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CCfgPageViewer::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        new CButton(HWindow, IDB_VIEWERFONT, BTF_RIGHTARROW);

        NormalText = new CColorArrowButton(HWindow, IDC_IV_NORMAL_TEXT, TRUE);
        SelectedText = new CColorArrowButton(HWindow, IDC_IV_SELECTED_TEXT, TRUE);

        CHyperLink* hl = new CHyperLink(HWindow, IDC_FILEMASK_HINT, STF_DOTUNDERLINE);
        if (hl != NULL)
            hl->SetActionShowHint(LoadStrW(IDS_MASKS_HINT));

        break;
    }

    case WM_COMMAND:
    {
        if (HIWORD(wParam) == BN_CLICKED)
        {
            switch (LOWORD(wParam))
            {
            case IDB_VIEWERFONT:
            {
                /* used by the export_mnu.py script that generates salmenu.mnu for the Translator
    keep synchronized with the InsertMenu() call below...
MENU_TEMPLATE_ITEM CfgPageViewerMenu[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_USEDEFAULTFONT
  {MNTT_IT, IDS_USECUSTOMFONT
  {MNTT_PE, 0
};
*/
                HMENU hMenu = CreatePopupMenu();
                BOOL cstFont = LocalUseCustomViewerFont;
                InsertMenuW(hMenu, 0xFFFFFFFF, cstFont ? 0 : MF_CHECKED | MF_BYCOMMAND | MF_STRING, 1, LoadStrW(IDS_USEDEFAULTFONT));
                InsertMenuW(hMenu, 0xFFFFFFFF, cstFont ? MF_CHECKED : 0 | MF_BYCOMMAND | MF_STRING, 2, LoadStrW(IDS_USECUSTOMFONT));

                TPMPARAMS tpmPar;
                tpmPar.cbSize = sizeof(tpmPar);
                GetWindowRect((HWND)lParam, &tpmPar.rcExclude);
                DWORD cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON, tpmPar.rcExclude.right, tpmPar.rcExclude.top,
                                             HWindow, &tpmPar);
                if (cmd == 1)
                {
                    // default font
                    LocalUseCustomViewerFont = FALSE;
                    LoadControls();
                }
                if (cmd == 2)
                {
                    // custom font
                    CHOOSEFONT cf;
                    LOGFONT logFont = LocalViewerLogFont;
                    memset(&cf, 0, sizeof(CHOOSEFONT));
                    cf.lStructSize = sizeof(CHOOSEFONT);
                    cf.hwndOwner = HWindow;
                    cf.hDC = NULL;
                    cf.lpLogFont = &logFont;
                    cf.iPointSize = 10;
                    cf.Flags = CF_NOVERTFONTS | CF_FIXEDPITCHONLY | CF_SCREENFONTS |
                               CF_INITTOLOGFONTSTRUCT;
                    if (ChooseFont(&cf) != 0)
                    {
                        LocalViewerLogFont = logFont;
                        LocalUseCustomViewerFont = TRUE;
                        LoadControls();
                    }
                }
                return 0;
            }

            case IDC_IV_NORMAL_TEXT:
            case IDC_IV_SELECTED_TEXT:
            {
                CColorArrowButton* button = (CColorArrowButton*)WindowsManager.GetWindowPtr((HWND)lParam);
                if (button != NULL)
                {
                    /* used by the export_mnu.py script that generates salmenu.mnu for the Translator
    keep synchronized with the InsertMenu() call below...
MENU_TEMPLATE_ITEM CfgPageViewerMenu[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_SETCOLOR_CUSTOM_FG
  {MNTT_IT, IDS_SETCOLOR_SYSTEM_FG
  {MNTT_IT, IDS_SETCOLOR_CUSTOM_BK
  {MNTT_IT, IDS_SETCOLOR_SYSTEM_BK
  {MNTT_PE, 0
};
*/
                    HMENU hMenu = CreatePopupMenu();
                    BOOL normal = button == NormalText;
                    BOOL checkedDefaultFg = GetFValue(TmpColors[normal ? VIEWER_FG_NORMAL : VIEWER_FG_SELECTED]) & SCF_DEFAULT;
                    BOOL checkedDefaultBk = GetFValue(TmpColors[normal ? VIEWER_BK_NORMAL : VIEWER_BK_SELECTED]) & SCF_DEFAULT;
                    InsertMenuW(hMenu, 0xFFFFFFFF, checkedDefaultFg ? 0 : MF_CHECKED | MF_BYCOMMAND | MF_STRING, 1, LoadStrW(IDS_SETCOLOR_CUSTOM_FG));
                    InsertMenuW(hMenu, 0xFFFFFFFF, checkedDefaultFg ? MF_CHECKED : 0 | MF_BYCOMMAND | MF_STRING, 2, LoadStrW(IDS_SETCOLOR_SYSTEM_FG));
                    InsertMenuW(hMenu, 0xFFFFFFFF, MF_BYCOMMAND | MF_SEPARATOR, 0, NULL);
                    InsertMenuW(hMenu, 0xFFFFFFFF, checkedDefaultBk ? 0 : MF_CHECKED | MF_BYCOMMAND | MF_STRING, 3, LoadStrW(IDS_SETCOLOR_CUSTOM_BK));
                    InsertMenuW(hMenu, 0xFFFFFFFF, checkedDefaultBk ? MF_CHECKED : 0 | MF_BYCOMMAND | MF_STRING, 4, LoadStrW(IDS_SETCOLOR_SYSTEM_BK));

                    //            int i;
                    //            for (i = 0; i < 4; i++)
                    //              SetMenuItemBitmaps(hMenu, i + 1, MF_BYCOMMAND, NULL, HMenuCheckDot);

                    TPMPARAMS tpmPar;
                    tpmPar.cbSize = sizeof(tpmPar);
                    GetWindowRect(button->HWindow, &tpmPar.rcExclude);
                    DWORD cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON, tpmPar.rcExclude.right, tpmPar.rcExclude.top,
                                                 HWindow, &tpmPar);
                    if (cmd != 0)
                    {
                        int colorIndex;
                        if (normal)
                            colorIndex = (cmd == 1 || cmd == 2) ? VIEWER_FG_NORMAL : VIEWER_BK_NORMAL;
                        else
                            colorIndex = (cmd == 1 || cmd == 2) ? VIEWER_FG_SELECTED : VIEWER_BK_SELECTED;
                        if (cmd == 1 || cmd == 3)
                        {
                            CHOOSECOLOR cc;
                            cc.lStructSize = sizeof(cc);
                            cc.hwndOwner = HWindow;
                            cc.lpCustColors = (LPDWORD)CustomColors;
                            if (cmd == 1)
                                cc.rgbResult = button->GetTextColor();
                            else
                                cc.rgbResult = button->GetBkgndColor();
                            cc.Flags = CC_RGBINIT | CC_FULLOPEN;
                            if (ChooseColor(&cc) == TRUE)
                            {
                                if (cmd == 1)
                                    button->SetTextColor(cc.rgbResult);
                                else
                                    button->SetBkgndColor(cc.rgbResult);

                                BYTE flags = GetFValue(TmpColors[colorIndex]);
                                flags &= ~SCF_DEFAULT;
                                TmpColors[colorIndex] = cc.rgbResult & 0x00ffffff | (((DWORD)flags) << 24);
                            }
                        }
                        else
                        {
                            BYTE flags = GetFValue(TmpColors[colorIndex]);
                            flags |= SCF_DEFAULT;
                            TmpColors[colorIndex] = TmpColors[colorIndex] & 0x00ffffff | (((DWORD)flags) << 24);

                            UpdateViewerColors(TmpColors);
                            button->SetColor(GetCOLORREF(TmpColors[normal ? VIEWER_FG_NORMAL : VIEWER_FG_SELECTED]),
                                             GetCOLORREF(TmpColors[normal ? VIEWER_BK_NORMAL : VIEWER_BK_SELECTED]));
                        }
                    }
                    DestroyMenu(hMenu);
                }
                return 0;
            }
            }
        }
        break;
    }
    }
    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}
//
// ****************************************************************************
// CCfgPageUserMenu
//
/*
void 
CSmallIconWindow::SetIcon(HICON hIcon)
{
  HIcon = hIcon;
  InvalidateRect(HWindow, NULL, TRUE);
  UpdateWindow(HWindow);
}

LRESULT 
CSmallIconWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
    case WM_PAINT:
    {
      PAINTSTRUCT ps;
      HDC hdc = HANDLES(BeginPaint(HWindow, &ps));

      RECT r;
      GetClientRect(HWindow, &r);

      FillRect(hdc, &r, HDialogBrush);

      if (IsWindowEnabled(HWindow))
        DrawIconEx(hdc, (r.right - 16) / 2, (r.bottom - 16) / 2,
                   HIcon, 0, 0, 0, NULL, DI_NORMAL);
      HANDLES(EndPaint(HWindow, &ps));
      return 0;
    }
  }
  return CWindow::WindowProc(uMsg, wParam, lParam);
}
*/

CCfgPageUserMenu::CCfgPageUserMenu(CUserMenuItems* userMenuItems)
    : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGPAGE_USERMENU, IDD_CFGPAGE_USERMENU, PSP_USETITLE, NULL)
{
    SourceUserMenuItems = userMenuItems;
    UserMenuItems = new CUserMenuItems(10, 5);
    UserMenuItems->LoadUMI(*SourceUserMenuItems, FALSE);
    EditLB = NULL;
    //  SmallIcon = NULL;
    DisableNotification = FALSE;
}

CCfgPageUserMenu::~CCfgPageUserMenu()
{
    if (UserMenuItems != NULL)
        delete UserMenuItems;
}

void CCfgPageUserMenu::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CCfgPageUserMenu::Validate()");
    int i;
    for (i = 0; i < UserMenuItems->Count; i++)
    {
        int errorPos1, errorPos2;
        CUserMenuItem* item = UserMenuItems->At(i);
        if (item->Type == umitItem)
        {
            if (!ValidateCommandFile(HWindow, item->UMCommand.c_str(), errorPos1, errorPos2))
            {
                EditLB->SetCurSel(i);
                ti.ErrorOn(IDE_COMMAND);
                PostMessage(GetDlgItem(HWindow, IDE_COMMAND), EM_SETSEL,
                            errorPos1, errorPos2);
                return;
            }
            if (!ValidateUserMenuArguments(HWindow, item->Arguments.c_str(), errorPos1, errorPos2, NULL))
            {
                EditLB->SetCurSel(i);
                ti.ErrorOn(IDE_ARGUMENTS);
                PostMessage(GetDlgItem(HWindow, IDE_ARGUMENTS), EM_SETSEL,
                            errorPos1, errorPos2);
                return;
            }
            if (!ValidateInitDir(HWindow, item->InitDir.c_str(), errorPos1, errorPos2))
            {
                EditLB->SetCurSel(i);
                ti.ErrorOn(IDE_INITDIR);
                PostMessage(GetDlgItem(HWindow, IDE_INITDIR), EM_SETSEL,
                            errorPos1, errorPos2);
                return;
            }
        }
    }
}

void CCfgPageUserMenu::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CCfgPageUserMenu::Transfer()");
    if (ti.Type == ttDataToWindow)
    {
        int i;
        for (i = 0; i < UserMenuItems->Count; i++)
            EditLB->AddItem((INT_PTR)UserMenuItems->At(i));
        EditLB->SetCurSel(0);
        LoadControls();
        EnableButtons();
    }
    else
    {
        SourceUserMenuItems->LoadUMI(*UserMenuItems, UserMenuIconBkgndReader.IsReadingIcons() ||
                                                         UserMenuIconBkgndReader.HasSysColorsChanged()); // after a system color change copying icons is not enough, they must be reloaded (the dialog does not handle icon color changes, icons might appear "broken")
    }
}

// pulls a child from the dialog and enables or disables it based on the 'enable' parameter;
// if 'enable' is FALSE and 'clear' is FALSE, it can also clear the edit line or the checkbox

void EnableDlgWindow(HWND hDialog, int resID, BOOL enable, BOOL clear = FALSE)
{
    HWND hChild = GetDlgItem(hDialog, resID);
    if (hChild == NULL)
    {
        TRACE_E("EnableDlgWindow: cannot find child window resID=" << resID);
        return;
    }
    EnableWindow(hChild, enable);
    if (!enable && clear)
    {
        wchar_t className[31];
        className[0] = 0;
        GetClassNameW(hChild, className, 30);
        if (StrICmpW(className, L"edit") == 0)
            SetWindowTextW(hChild, L"");
        else if (StrICmpW(className, L"button") == 0)
            SendMessage(hChild, BM_SETCHECK, BST_UNCHECKED, 0);
    }
}

void CCfgPageUserMenu::EnableButtons()
{
    BOOL oldDisableNotification = DisableNotification;
    DisableNotification = TRUE;

    BOOL validItem = TRUE;
    INT_PTR itemID;
    EditLB->GetCurSelItemID(itemID);
    if (itemID != -1)
    {
        CUserMenuItem* item = (CUserMenuItem*)itemID;
        if (item->Type == umitSubmenuEnd)
            validItem = FALSE;
    }
    if (itemID == -1)
        validItem = FALSE;

    BOOL separator = IsDlgButtonChecked(HWindow, IDC_UM_SEPARATOR);
    BOOL submenu = IsDlgButtonChecked(HWindow, IDC_UM_SUBMENU);

    EnableDlgWindow(HWindow, IDC_UM_SUBMENU, validItem & !separator, TRUE);
    EnableDlgWindow(HWindow, IDC_UM_SEPARATOR, validItem & !submenu, TRUE);

    EnableWindow(GetDlgItem(HWindow, IDB_UM_CHANGEICON), validItem & !separator & !submenu);
    /*
  EnableWindow(SmallIcon->HWindow, validItem & !separator & !submenu);
  InvalidateRect(SmallIcon->HWindow, NULL, TRUE);
  UpdateWindow(SmallIcon->HWindow);
*/
    EnableDlgWindow(HWindow, IDE_COMMAND, validItem & !separator & !submenu, TRUE);
    EnableDlgWindow(HWindow, IDE_ARGUMENTS, validItem & !separator & !submenu, TRUE);
    EnableDlgWindow(HWindow, IDE_INITDIR, validItem & !separator & !submenu, TRUE);

    EnableWindow(GetDlgItem(HWindow, IDB_BROWSECOMMAND), validItem & !separator & !submenu);
    EnableWindow(GetDlgItem(HWindow, IDB_BROWSEARGUMENTS), validItem & !separator & !submenu);
    EnableWindow(GetDlgItem(HWindow, IDB_BROWSEINITDIR), validItem & !separator & !submenu);

    EnableDlgWindow(HWindow, IDC_THROUGHSHELL, validItem & !separator & !submenu, TRUE);
    BOOL throughShell = IsDlgButtonChecked(HWindow, IDC_THROUGHSHELL);
    EnableDlgWindow(HWindow, IDC_OUTPUTWND,
                    validItem & throughShell & !separator & !submenu, TRUE);
    BOOL openShell = IsDlgButtonChecked(HWindow, IDC_OUTPUTWND);
    EnableDlgWindow(HWindow, IDC_CLOSESHELL,
                    validItem & throughShell & openShell & !separator & !submenu, TRUE);

    EnableWindow(GetDlgItem(HWindow, IDC_UM_TOOLBAR), validItem);
    DisableNotification = oldDisableNotification;
}

void CCfgPageUserMenu::LoadControls()
{
    CALL_STACK_MESSAGE1("CCfgPageUserMenu::LoadControls()");
    INT_PTR itemID;
    EditLB->GetCurSelItemID(itemID);
    BOOL empty = FALSE;
    if (itemID == -1)
        empty = TRUE;

    CUserMenuItem* item = NULL;
    if (!empty)
        item = (CUserMenuItem*)itemID;
    DisableNotification = TRUE;

    CheckDlgButton(HWindow, IDC_UM_SUBMENU, (!empty && (item->Type == umitSubmenuBegin)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(HWindow, IDC_UM_SEPARATOR, (!empty && (item->Type == umitSeparator)) ? BST_CHECKED : BST_UNCHECKED);

    SendMessageW(GetDlgItem(HWindow, IDE_ARGUMENTS), EM_LIMITTEXT, USRMNUARGS_MAXLEN - 1, 0);
    SendMessageW(GetDlgItem(HWindow, IDE_COMMAND), WM_SETTEXT, 0,
                 (LPARAM)(empty ? L"" : item->UMCommand.c_str()));
    SendMessageW(GetDlgItem(HWindow, IDE_ARGUMENTS), WM_SETTEXT, 0,
                 (LPARAM)(empty ? L"" : item->Arguments.c_str()));
    SendMessageW(GetDlgItem(HWindow, IDE_ARGUMENTS), EM_SETSEL, 0, -1); // so that Browse overwrites the content
    SendMessageW(GetDlgItem(HWindow, IDE_INITDIR), WM_SETTEXT, 0,
                 (LPARAM)(empty ? L"" : item->InitDir.c_str()));
    SendMessageW(GetDlgItem(HWindow, IDE_INITDIR), EM_SETSEL, 0, -1); // so that Browse overwrites the content

    //  SmallIcon->SetIcon(!empty ? item->HIcon : NULL);

    CheckDlgButton(HWindow, IDC_OUTPUTWND, (!empty && (item->UseWindow)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(HWindow, IDC_THROUGHSHELL, (!empty && (item->ThroughShell)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(HWindow, IDC_CLOSESHELL, (!empty && (item->CloseShell)) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(HWindow, IDC_UM_TOOLBAR, (!empty && (item->ShowInToolbar)) ? BST_CHECKED : BST_UNCHECKED);
    DisableNotification = FALSE;
}

void CCfgPageUserMenu::StoreControls()
{
    CALL_STACK_MESSAGE1("CCfgPageUserMenu::StoreControls()");
    int index;
    EditLB->GetCurSel(index);
    if (!DisableNotification && index >= 0 && index < EditLB->GetCount())
    {
        CUserMenuItem* item = UserMenuItems->At(index);

        wchar_t arguments[USRMNUARGS_MAXLEN];
        const std::wstring command = GetWindowTextStringW(GetDlgItem(HWindow, IDE_COMMAND));
        SendMessageW(GetDlgItem(HWindow, IDE_ARGUMENTS), WM_GETTEXT,
                     USRMNUARGS_MAXLEN, (LPARAM)arguments);
        const std::wstring initdir = GetWindowTextStringW(GetDlgItem(HWindow, IDE_INITDIR));

        item->Set(item->ItemName.c_str(), command.c_str(), arguments, initdir.c_str(), item->Icon.c_str());

        BOOL submenu = (IsDlgButtonChecked(HWindow, IDC_UM_SUBMENU) == BST_CHECKED);
        BOOL separator = (IsDlgButtonChecked(HWindow, IDC_UM_SEPARATOR) == BST_CHECKED);

        CUserMenuItemType type;
        if (submenu)
            type = umitSubmenuBegin;
        else if (separator)
            type = umitSeparator;
        else
            type = umitItem;
        if (item->Type != type)
        {
            item->SetType(type);
            item->GetIconHandle(NULL, FALSE);
            EditLB->RedrawFocusedItem();
        }

        item->UseWindow = (IsDlgButtonChecked(HWindow, IDC_OUTPUTWND) == BST_CHECKED);
        item->ThroughShell = (IsDlgButtonChecked(HWindow, IDC_THROUGHSHELL) == BST_CHECKED);
        item->CloseShell = (IsDlgButtonChecked(HWindow, IDC_CLOSESHELL) == BST_CHECKED);
        item->ShowInToolbar = (IsDlgButtonChecked(HWindow, IDC_UM_TOOLBAR) == BST_CHECKED);
    }
}

void CCfgPageUserMenu::DeleteSubmenuEnd(int index)
{
    int endIndex = UserMenuItems->GetSubmenuEndIndex(index);
    if (endIndex != -1)
    {
        // a closing item exists, delete it
        EditLB->DeleteItem(endIndex);
        UserMenuItems->Delete(endIndex);
    }
}

void CCfgPageUserMenu::RefreshGroupIconInUMItems()
{
    for (int i = 0; i < UserMenuItems->Count; i++)
    {
        CUserMenuItem* item = UserMenuItems->At(i);
        if (item->Type == umitSubmenuBegin)
            item->UMIcon = HGroupIcon;
    }
}

INT_PTR
CCfgPageUserMenu::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CCfgPageUserMenu::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        RefreshGroupIconInUMItems();                                                          // if colors changed before first visiting the User Menu page and we did not receive WM_SYSCOLORCHANGE, handle it here
        EditLB = new CEditListBox(HWindow, IDL_MENUITEMS, ELB_ENABLECOMMANDS | ELB_SHOWICON); // we need the enabler and icons
        if (EditLB == NULL)
            TRACE_E(LOW_MEMORY);
        ChangeToArrowButton(HWindow, IDB_BROWSECOMMAND);
        ChangeToArrowButton(HWindow, IDB_BROWSEARGUMENTS);
        ChangeToArrowButton(HWindow, IDB_BROWSEINITDIR);
        EditLB->MakeHeader(IDS_USHEADER);
        //      SmallIcon = new CSmallIconWindow(HWindow, IDC_UM_ICON);
        //      if (SmallIcon == NULL) TRACE_E(LOW_MEMORY);

        // dialog elements should stretch with the dialog size, set split controls
        ElasticVerticalLayout(1, IDL_MENUITEMS);

        break;
    }

    case WM_COMMAND:
    {
        if (DisableNotification)
            return 0;

        if (HIWORD(wParam) == BN_CLICKED)
        {
            if (LOWORD(wParam) == IDC_UM_SUBMENU)
            {
                int index;
                EditLB->GetCurSel(index);
                if (index >= 0 && index < EditLB->GetCount())
                {
                    BOOL submenu = IsDlgButtonChecked(HWindow, IDC_UM_SUBMENU) == BST_CHECKED;
                    if (submenu)
                    {
                        // user checked the Submenu checkbox; add a closing item
                        static wchar_t emptyBuffer[] = L"";
                        CUserMenuItem* item = new CUserMenuItem(LoadStrW(IDS_ENDUSERSUBMENU), emptyBuffer, emptyBuffer, emptyBuffer, emptyBuffer,
                                                                FALSE, FALSE, FALSE, FALSE, umitSubmenuEnd, NULL);
                        if (item == NULL)
                            return 0;
                        UserMenuItems->Insert(index + 1, item);
                        EditLB->InsertItem((INT_PTR)item, index + 1);

                        // the icon will change
                        //              EditLB->RedrawFocusedItem();
                    }
                    else
                    {
                        // user cleared the Submenu checkbox; remove the closing item
                        DeleteSubmenuEnd(index);
                    }
                }
            }

            EnableButtons();
            StoreControls();
        }

        if (HIWORD(wParam) == EN_CHANGE)
        {
            StoreControls();
            EnableButtons();
        }

        if (LOWORD(wParam) == IDL_MENUITEMS && HIWORD(wParam) == LBN_SELCHANGE)
        {
            EditLB->OnSelChanged();
            LoadControls();
            EnableButtons();
        }

        switch (LOWORD(wParam))
        {
        case IDB_BROWSECOMMAND:
        {
            const CExecuteItem* item;
            item = TrackExecuteMenu(HWindow, IDB_BROWSECOMMAND, IDE_COMMAND, FALSE,
                                    CommandExecutes, IDS_EXEFILTER);
            if (item != NULL)
            {
                // update icons
                int index;
                EditLB->GetCurSel(index);
                if (index >= 0 && index < EditLB->GetCount())
                {
                    CUserMenuItem* item2 = UserMenuItems->At(index);
                    item2->GetIconHandle(NULL, FALSE);
                    EditLB->RedrawFocusedItem();
                }
            }
            return 0;
        }

        case IDB_BROWSEARGUMENTS:
        {
            TrackExecuteMenu(HWindow, IDB_BROWSEARGUMENTS, IDE_ARGUMENTS, FALSE,
                             UserMenuArgsExecutes);
            return 0;
        }

        case IDB_BROWSEINITDIR:
        {
            TrackExecuteMenu(HWindow, IDB_BROWSEINITDIR, IDE_INITDIR, FALSE,
                             InitDirExecutes);
            return 0;
        }

        case IDB_UM_CHANGEICON:
        {
            std::wstring fileName;
            int resID = 0;

            int index;
            EditLB->GetCurSel(index);
            BOOL error = FALSE;
            if (index >= 0 && index < EditLB->GetCount())
            {
                CUserMenuItem* item = UserMenuItems->At(index);
                if (!item->Icon.empty())
                {
                    // Icon is in the format "file name,resID"
                    // perform decomposition
                    const wchar_t* iconStr = item->Icon.c_str();
                    const wchar_t* iterator = iconStr + item->Icon.length() - 1;
                    while (iterator > iconStr && *iterator != ',')
                        iterator--;
                    if (iterator > iconStr && *iterator == ',')
                    {
                        fileName.assign(iconStr, iterator - iconStr);
                        iterator++;
                        resID = _wtoi(iterator);
                    }
                }
                if (!error && fileName.empty() && !item->UMCommand.empty())
                {
                    if (!ExpandCommand(MainWindow->HWindow, item->UMCommand.c_str(), fileName, FALSE))
                        error = TRUE;
                    else
                    {
                        while (fileName.length() > 2 && fileName.front() == L'"' && fileName.back() == L'"')
                            fileName = fileName.substr(1, fileName.length() - 2);
                    }
                }

                if (!error)
                {
                    CChangeIconDialog dlg(HWindow, fileName, &resID);
                    if (dlg.Execute() == IDOK)
                    {
                        fileName += L"," + std::to_wstring(resID);
                        item->Set(item->ItemName.c_str(), item->UMCommand.c_str(), item->Arguments.c_str(), item->InitDir.c_str(), fileName.c_str());
                        item->GetIconHandle(NULL, FALSE);
                        EditLB->RedrawFocusedItem();
                    }
                }
            }
            return 0;
        }
        }
        break;
    }

    case WM_SYSCOLORCHANGE:
    {
        RefreshGroupIconInUMItems();
        return 0;
    }

    case WM_NOTIFY:
    {
        /*
      LRESULT result;
      if (EditLB->OnWMNotify(lParam, result))
      {
        SetWindowLongPtr(HWindow, DWLP_MSGRESULT, result);
        return 0;
      }
*/
        NMHDR* nmhdr = (NMHDR*)lParam;
        switch (nmhdr->idFrom)
        {
        case IDL_MENUITEMS:
        {
            switch (nmhdr->code)
            {
            case EDTLBN_GETDISPINFO:
            {
                EDTLB_DISPINFO* dispInfo = (EDTLB_DISPINFO*)lParam;
                if (dispInfo->ToDo == edtlbGetData)
                {
                    CUserMenuItem* item = (CUserMenuItem*)dispInfo->ItemID;
                    *dispInfo->Text = item->ItemName;
                    dispInfo->HIcon = item->UMIcon;
                    SetWindowLongPtr(HWindow, DWLP_MSGRESULT, FALSE);
                    return TRUE;
                }
                else
                {
                    CUserMenuItem* item;
                    if (dispInfo->ItemID == -1)
                    {
                        item = new CUserMenuItem();
                        if (item == NULL)
                        {
                            TRACE_E(LOW_MEMORY);
                            SetWindowLongPtr(HWindow, DWLP_MSGRESULT, TRUE);
                            return TRUE;
                        }
                        UserMenuItems->Add(item);
                        item->Set(dispInfo->Text->c_str(), item->UMCommand.c_str(), item->Arguments.c_str(), item->InitDir.c_str(), item->Icon.c_str());
                        EditLB->SetItemData((INT_PTR)item);
                    }
                    else
                    {
                        item = (CUserMenuItem*)dispInfo->ItemID;
                        item->Set(dispInfo->Text->c_str(), item->UMCommand.c_str(), item->Arguments.c_str(), item->InitDir.c_str(), item->Icon.c_str());
                    }

                    LoadControls();
                    EnableButtons();
                    SetWindowLongPtr(HWindow, DWLP_MSGRESULT, TRUE);
                    return TRUE;
                }
                break;
            }

            case EDTLBN_ENABLECOMMANDS:
            {
                EDTLB_DISPINFO* dispInfo = (EDTLB_DISPINFO*)lParam;
                int index;
                EditLB->GetCurSel(index);
                dispInfo->Enable = TLBHDRMASK_NEW;
                if (index < 0 || index >= UserMenuItems->Count)
                {
                    dispInfo->Enable |= TLBHDRMASK_MODIFY;
                    return TRUE;
                }

                if (UserMenuItems->At(index)->Type != umitSubmenuEnd)
                    dispInfo->Enable |= TLBHDRMASK_DELETE | TLBHDRMASK_MODIFY;

                if (UserMenuItems->At(index)->Type == umitSubmenuBegin ||
                    UserMenuItems->At(index)->Type == umitSubmenuEnd)
                {
                    if (UserMenuItems->At(index)->Type == umitSubmenuBegin)
                    {
                        // umitSubmenuBegin - the start can always move up, down until its matching end hits the list end
                        dispInfo->Enable |= TLBHDRMASK_UP;
                        int endIndex = UserMenuItems->GetSubmenuEndIndex(index);
                        if (endIndex != -1 && endIndex + 1 < UserMenuItems->Count)
                        {
                            dispInfo->Enable |= TLBHDRMASK_DOWN;
                        }
                    }
                    else
                    {
                        // umitSubmenuEnd - do not allow the end to move over a different end or start
                        if (index > 0)
                        {
                            if (UserMenuItems->At(index - 1)->Type != umitSubmenuBegin &&
                                UserMenuItems->At(index - 1)->Type != umitSubmenuEnd)
                                dispInfo->Enable |= TLBHDRMASK_UP;
                        }
                        if (index >= 0 && index < UserMenuItems->Count - 1)
                        {
                            if (UserMenuItems->At(index + 1)->Type != umitSubmenuBegin &&
                                UserMenuItems->At(index + 1)->Type != umitSubmenuEnd)
                                dispInfo->Enable |= TLBHDRMASK_DOWN;
                        }
                    }
                }
                else
                {
                    // !umitSubmenuBegin && !umitSubmenuEnd
                    dispInfo->Enable |= TLBHDRMASK_UP | TLBHDRMASK_DOWN;
                }
                return TRUE;
            }
                /*
            case EDTLBN_MOVEITEM:
            {
              EDTLB_DISPINFO *dispInfo = (EDTLB_DISPINFO *)lParam;
              int index;
              EditLB->GetCurSel(index);

              CUserMenuItem *item = UserMenuItems->At(index);

              if (item->Type == umitSubmenuBegin)
              {
                int endIndex = UserMenuItems->GetSubmenuEndIndex(index);
                if (endIndex != -1)
                {
                  // found SubmenuEnd
                  BYTE buf[sizeof(CUserMenuItem)];
                  if (dispInfo->Up)
                  {
                    // move the whole block up
                    if (index > 0)
                    {
                      memcpy(buf, UserMenuItems->At(index - 1), sizeof(CUserMenuItem));
                      int i;
                      for (i = index; i <= endIndex; i++)
                        memcpy(UserMenuItems->At(i - 1), UserMenuItems->At(i), sizeof(CUserMenuItem));
                      memcpy(UserMenuItems->At(endIndex), buf, sizeof(CUserMenuItem));
                      EditLB->SetCurSel(index - 1);
                    }
                  }
                  else
                  {
                    // move the whole block down
                    if (endIndex > 0 && endIndex < EditLB->GetCount() - 1)
                    {
                      memcpy(buf, UserMenuItems->At(endIndex + 1), sizeof(CUserMenuItem));
                      int i;
                      for (i = endIndex; i >= index; i--)
                        memcpy(UserMenuItems->At(i + 1), UserMenuItems->At(i), sizeof(CUserMenuItem));
                      memcpy(UserMenuItems->At(index), buf, sizeof(CUserMenuItem));
                      EditLB->SetCurSel(index + 1);
                    }
                  }
                  InvalidateRect(EditLB->HWindow, NULL, FALSE);
                }

                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, TRUE);  // disallow the change
                return TRUE;
              }
              else
              {
                int srcIndex = index;
                int dstIndex = index + (dispInfo->Up ? -1 : 1);

                BYTE buf[sizeof(CUserMenuItem)];
                memcpy(buf, UserMenuItems->At(srcIndex), sizeof(CUserMenuItem));
                memcpy(UserMenuItems->At(srcIndex), UserMenuItems->At(dstIndex), sizeof(CUserMenuItem));
                memcpy(UserMenuItems->At(dstIndex), buf, sizeof(CUserMenuItem));

                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, FALSE);  // allow the change
                return TRUE;
              }
            }
*/
            case EDTLBN_MOVEITEM2:
            {
                EDTLB_DISPINFO* dispInfo = (EDTLB_DISPINFO*)lParam;
                int index = -1;
                EditLB->GetCurSel(index);
                const int moved = UserMenuItems->MoveItem(index, dispInfo->NewIndex < index ? -1 : 1);
                if (moved != index)
                {
                    EditLB->SetCurSel(moved);
                    InvalidateRect(EditLB->HWindow, NULL, FALSE);
                }
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, TRUE); // the model moved the complete item
                return TRUE;
            }

            case EDTLBN_DELETEITEM:
            {
                int index;
                EditLB->GetCurSel(index);

                // when the user deletes a popup we must remove the closing item
                if (UserMenuItems->At(index)->Type == umitSubmenuBegin)
                    DeleteSubmenuEnd(index);

                UserMenuItems->Delete(index);
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, FALSE); // allow deletion
                return TRUE;
            }
            }
            break;
        }
        }
        break;
    }

    case WM_DRAWITEM:
    {
        int idCtrl = (int)wParam;
        if (idCtrl == IDL_MENUITEMS)
        {
            EditLB->OnDrawItem(lParam);
            return TRUE;
        }
        break;
    }
    }
    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CCfgPageHotPath
//

CCfgPageHotPath::CCfgPageHotPath(BOOL editMode, int editIndex)
    : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGPAGE_HOTPATH, IDD_CFGPAGE_HOTPATH, PSP_USETITLE, NULL)
{
    Dirty = FALSE;
    Header = NULL;
    HListView = NULL;
    DisableNotification = FALSE;
    EditMode = editMode;
    EditIndex = editIndex;
    if (editIndex < 0 || editIndex >= HOT_PATHS_COUNT)
    {
        EditMode = FALSE;
        EditIndex = 0;
    }
    LabelEdit = FALSE;
    Config = new CHotPathItems();
    if (Config == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return;
    }
}

CCfgPageHotPath::~CCfgPageHotPath()
{
    if (Config != NULL)
        delete Config;
}

void CCfgPageHotPath::Validate(CTransferInfo& ti)
{
    if (Dirty)
    {
        // IDC_WAIT is already MAKEINTRESOURCE(32514) (this build has no UNICODE
        // #define, so that resolves through MAKEINTRESOURCEA) - re-wrapping an
        // already-encoded resource-ID pointer in MAKEINTRESOURCEW truncates that POINTER
        // through (WORD) again (C4302). Numerically harmless today only because 32514 fits in
        // 16 bits either way; a plain pointer-type reinterpretation carries the same encoded
        // value without truncating anything.
        HCURSOR hOldCur = SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
        int i;
        for (i = 0; i < HOT_PATHS_COUNT; i++)
        {
            int errorPos1, errorPos2;
            if (Config->GetNameLen(i) > 0)
            {
                if (!ValidateHotPath(HWindow, Config->GetPathW(i).c_str(), errorPos1, errorPos2))
                {
                    ListView_SetItemState(HListView, i, LVIS_SELECTED, LVIS_SELECTED);
                    ListView_EnsureVisible(HListView, i, FALSE);
                    ti.ErrorOn(IDC_HOTPATH_PATH);
                    PostMessage(GetDlgItem(HWindow, IDC_HOTPATH_PATH), EM_SETSEL,
                                errorPos1, errorPos2);
                    return;
                }
            }
        }
        SetCursor(hOldCur);
    }
}

extern std::wstring HotPathSetBufferName;
extern std::wstring HotPathSetBufferPath;

void CCfgPageHotPath::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CCfgPageHotPath::Transfer()");
    ti.CheckBox(IDC_HOTPATH_AUTOCONFIG, Configuration.HotPathAutoConfig);
    if (ti.Type == ttDataToWindow)
    {
        Config->Load(MainWindow->HotPaths);

        if (EditMode)
            Config->SetW(EditIndex, HotPathSetBufferName.c_str(), HotPathSetBufferPath.c_str());

        int index = 0;
        DisableNotification = TRUE;
        wchar_t buff[20];
        int i;
        for (i = 0; i < HOT_PATHS_COUNT; i++)
        {
            LVITEMW lvi;
            lvi.mask = LVIF_TEXT | LVIF_STATE;
            lvi.iItem = i;
            lvi.iSubItem = 0;
            lvi.state = 0;
            lvi.pszText = const_cast<wchar_t*>(Config->GetNameW(i).c_str());
            SendMessageW(HListView, LVM_INSERTITEMW, 0, (LPARAM)&lvi);

            UINT state = INDEXTOSTATEIMAGEMASK((Config->GetVisible(i) ? 2 : 1));
            ListView_SetItemState(HListView, i, state, LVIS_STATEIMAGEMASK);

            if (i < 10)
            {
                swprintf(buff, 20, L"%ls+%d", LoadStrW(IDS_CTRL), i < 9 ? i + 1 : 0);
                ListView_SetItemTextW(HListView, i, 1, buff);
            }
        }

        RECT r;
        GetClientRect(HListView, &r);
        ListView_SetColumnWidth(HListView, 0, r.right - r.left);
        ListView_SetColumnWidth(HListView, 1, LVSCW_AUTOSIZE_USEHEADER);
        int w = ListView_GetColumnWidth(HListView, 1);
        ListView_SetColumnWidth(HListView, 0, r.right - r.left - w);

        DWORD state = LVIS_SELECTED | LVIS_FOCUSED;
        if (EditMode)
            index = EditIndex;
        ListView_SetItemState(HListView, index, state, state);
        DisableNotification = FALSE;
        LoadControls();
        EnableHeader();
    }
    else
    {
        MainWindow->HotPaths.Load(*Config);
    }
    Dirty = FALSE;
}

void CCfgPageHotPath::LoadControls()
{
    int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
    // Wide: Config->GetPathW/SetPathW are the real storage; the
    // narrow GetPath/SetPath pair is documented lossy-by-construction. The edit control
    // itself is a plain USER32 EDIT (wide-native regardless of the dialog's own class),
    // so SetDlgItemTextW/GetDlgItemTextW already work here - only the narrow round trip
    // through this control was mangling a non-ASCII hot path, both on display and (via
    // StoreControls firing on every EN_CHANGE) on every keystroke while typing one.
    std::wstring path;
    BOOL visible = FALSE;
    if (index != -1)
    {
        path = Config->GetPathW(index);
        visible = Config->GetVisible(index);
    }

    DisableNotification = TRUE;
    SendDlgItemMessage(HWindow, IDC_HOTPATH_PATH, EM_LIMITTEXT, 0x7FFFFFFE, 0);
    SetDlgItemTextW(HWindow, IDC_HOTPATH_PATH, path.c_str());

    DisableNotification = FALSE;
    EnableControls();
}

void CCfgPageHotPath::StoreControls()
{
    int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
    if (index != -1)
    {
        HWND edit = GetDlgItem(HWindow, IDC_HOTPATH_PATH);
        const int length = GetWindowTextLengthW(edit);
        std::vector<wchar_t> buffer((size_t)length + 1);
        GetWindowTextW(edit, buffer.data(), length + 1);
        Config->SetPathW(index, buffer.data());
    }
}

void CCfgPageHotPath::EnableControls()
{
    int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
    BOOL enable = TRUE;
    if (index == -1 || Config->GetNameLen(index) == 0)
        enable = FALSE;
    EnableWindow(GetDlgItem(HWindow, IDC_HOTPATH_PATH), enable);
    EnableWindow(GetDlgItem(HWindow, IDC_HOTPATH_BROWSE), enable);
}

void CCfgPageHotPath::EnableHeader()
{
    DWORD mask = 0;
    if (!LabelEdit)
    {
        mask |= TLBHDRMASK_MODIFY | TLBHDRMASK_DELETE;
        int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
        if (index > 0)
            mask |= TLBHDRMASK_UP;
        if (index < HOT_PATHS_COUNT - 1)
            mask |= TLBHDRMASK_DOWN;
    }
    Header->EnableToolbar(mask);
}

void CCfgPageHotPath::OnModify()
{
    int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
    if (index != -1)
        PostMessage(HListView, LVM_EDITLABEL, index, 0);
}

void CCfgPageHotPath::OnDelete()
{
    int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
    if (index != -1)
    {
        Config->SetW(index, L"", L"");
        wchar_t buffEmpty[] = L"";
        ListView_SetItemTextW(HListView, index, 0, buffEmpty);
        LoadControls();
    }
    EnableHeader();
}

void CCfgPageHotPath::OnMove(BOOL up)
{
    CALL_STACK_MESSAGE2("CCfgPageHotPath::OnMove(%d)", up);
    DisableNotification = TRUE;
    int index1 = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
    int index2 = index1;
    if (index1 == -1)
        return;
    if (up && index1 > 0)
        index2 = index1 - 1;
    if (!up && index1 < HOT_PATHS_COUNT - 1)
        index2 = index1 + 1;
    if (index2 != index1)
    {
        std::wstring name1 = Config->GetNameW(index1);
        std::wstring name2 = Config->GetNameW(index2);
        ListView_SetItemTextW(HListView, index1, 0, name2.data());
        ListView_SetItemTextW(HListView, index2, 0, name1.data());
        DWORD state1 = ListView_GetItemState(HListView, index1, LVIS_STATEIMAGEMASK);
        DWORD state2 = ListView_GetItemState(HListView, index2, LVIS_STATEIMAGEMASK);
        ListView_SetItemState(HListView, index1, state2, LVIS_STATEIMAGEMASK);
        state1 |= LVIS_FOCUSED | LVIS_SELECTED;
        ListView_SetItemState(HListView, index2, state1, LVIS_STATEIMAGEMASK | LVIS_FOCUSED | LVIS_SELECTED);
        ListView_EnsureVisible(HListView, index2, FALSE);
        Config->SwapItems(index1, index2);
        LoadControls();
    }
    DisableNotification = FALSE;
    EnableHeader();
}

INT_PTR
CCfgPageHotPath::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CCfgPageHotPath::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_PAINT:
    {
        // Horrible mess - I need a message that arrives
        // after WM_INITDIALOG so we can set the focus
        // hopefully this will survive until the next Salamander version :-)
        if (EditMode)
        {
            SetFocus(HListView);
            SendMessage(HListView, LVM_EDITLABEL, EditIndex, 0);
            EditMode = FALSE;
        }
        break;
    }

    case WM_INITDIALOG:
    {
        CMyListView* listView = new CMyListView(HWindow, IDC_HOTPATH_LIST);
        HListView = listView->HWindow;
        //      HListView = GetDlgItem(HWindow, IDC_HOTPATH_LIST);
        Header = new CToolbarHeader(HWindow, IDC_HOTPATH_HEADER, HListView,
                                    TLBHDRMASK_MODIFY | TLBHDRMASK_DELETE |
                                        TLBHDRMASK_UP | TLBHDRMASK_DOWN);
        if (Header == NULL)
            TRACE_E(LOW_MEMORY);

        DWORD exFlags = LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES;
        //      ListView_SetExtendedListViewStyleEx(HListView, exFlags, exFlags);  // 4.71

        DWORD origFlags = ListView_GetExtendedListViewStyle(HListView);
        ListView_SetExtendedListViewStyle(HListView, origFlags | exFlags);

        // determine the list view size
        RECT r;
        GetClientRect(HListView, &r);
        int nameWidth = r.right - (int)(r.right / 7);

        // fill the list view with the Name and HotKey columns
        LVCOLUMNW lvc;
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        lvc.pszText = LoadStrW(IDS_HOTPATH_NAME);
        lvc.cx = 1;
        lvc.fmt = LVCFMT_LEFT;
        SendMessageW(HListView, LVM_INSERTCOLUMNW, 0, (LPARAM)&lvc);

        lvc.mask |= LVCF_SUBITEM;
        lvc.pszText = LoadStrW(IDS_HOTPATH_HOTKEY);
        lvc.cx = 1;
        lvc.iSubItem = 1;
        SendMessageW(HListView, LVM_INSERTCOLUMNW, 1, (LPARAM)&lvc);

        ChangeToArrowButton(HWindow, IDC_HOTPATH_BROWSE);

        // dialog elements should stretch with the dialog size, set split controls
        ElasticVerticalLayout(1, IDC_HOTPATH_LIST);

        break;
    }

    case WM_USER_CHAR:
    {
        int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
        if (wParam != ' ' && !LabelEdit)
        {
            ListView_EditLabel(HListView, index);
            HWND hEdit = ListView_GetEditControl(HListView);
            SendMessage(hEdit, WM_CHAR, wParam, 0);
        }
        return 0;
    }

    case WM_NOTIFY:
    {
        if (DisableNotification)
            break;

        if (wParam == IDC_HOTPATH_LIST)
        {
            LPNMHDR nmh = (LPNMHDR)lParam;
            switch (nmh->code)
            {
            case LVN_KEYDOWN:
            {
                LPNMLVKEYDOWN nmhk = (LPNMLVKEYDOWN)nmh;
                int index = ListView_GetNextItem(HListView, -1, LVNI_SELECTED);
                if (nmhk->wVKey == VK_F2)
                {
                    OnModify();
                }
                if (nmhk->wVKey == VK_DELETE)
                {
                    OnDelete();
                }
                if ((GetKeyState(VK_MENU) & 0x8000) &&
                    (nmhk->wVKey == VK_UP || nmhk->wVKey == VK_DOWN))
                {
                    OnMove(nmhk->wVKey == VK_UP);
                }
                if (!LabelEdit && nmhk->wVKey == VK_INSERT)
                    ListView_EditLabel(HListView, index);
                break;
            }

            case LVN_ITEMCHANGED:
            {
                LPNMLISTVIEW nmhi = (LPNMLISTVIEW)nmh;
                if (!(nmhi->uOldState & LVIS_SELECTED) && nmhi->uNewState & LVIS_SELECTED)
                {
                    LoadControls();
                }
                if (nmhi->uOldState & 0x2000 && nmhi->uNewState & 0x1000 ||
                    nmhi->uOldState & 0x1000 && nmhi->uNewState & 0x2000)
                {
                    BOOL checked = nmhi->uNewState & 0x2000;
                    Config->SetVisible(nmhi->iItem, checked);
                }
                EnableHeader();
                break;
            }

            case LVN_BEGINLABELEDIT:
            {
                LabelEdit = TRUE;
                EnableHeader();
                break;
            }

            case LVN_ENDLABELEDIT:
            {
                NMLVDISPINFOW* nmhd = (NMLVDISPINFOW*)nmh;
                LabelEdit = FALSE;
                EnableHeader();
                if (nmhd->item.pszText != NULL)
                {
                    std::wstring name = nmhd->item.pszText;
                    Config->CleanName(name);
                    int index = nmhd->item.iItem;
                    const std::wstring path = !name.empty() ? Config->GetPathW(index) : std::wstring();
                    Config->SetW(index, name.c_str(), path.c_str());
                    LoadControls();
                    ListView_SetItemTextW(HListView, index, 0, const_cast<wchar_t*>(name.c_str()));
                    Dirty = TRUE;
                    break;
                }
                break;
            }
            }
        }
        break;
    }

    case WM_COMMAND:
    {
        if (HIWORD(wParam) == EN_CHANGE && LOWORD(wParam) == IDC_HOTPATH_PATH)
        {
            if (!DisableNotification)
            {
                StoreControls();
                Dirty = TRUE;
            }
            return TRUE;
        }

        if (LOWORD(wParam) == IDC_HOTPATH_BROWSE)
        {
            TrackExecuteMenu(HWindow, IDC_HOTPATH_BROWSE, IDC_HOTPATH_PATH, FALSE,
                             HotPathItems);
            return TRUE;
        }

        if (LOWORD(wParam) == IDC_HOTPATH_HEADER)
        {
            if (GetFocus() != HListView)
                SetFocus(HListView);
            switch (HIWORD(wParam))
            {
            case TLBHDR_MODIFY:
                OnModify();
                break;
            case TLBHDR_DELETE:
                OnDelete();
                break;
            case TLBHDR_UP:
                OnMove(TRUE);
                break;
            case TLBHDR_DOWN:
                OnMove(FALSE);
                break;
            }
        }
        break;
    }
    }
    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CCfgPageSystem
//

CCfgPageSystem::CCfgPageSystem()
    : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGPAGE_SYSTEM, IDD_CFGPAGE_SYSTEM, PSP_USETITLE, NULL)
{
}

void CCfgPageSystem::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CCfgPageSystem::Validate()");
    int useRecycle;
    ti.RadioButton(IDR_RECYCLE1, 0, useRecycle);
    ti.RadioButton(IDR_RECYCLE2, 1, useRecycle);
    ti.RadioButton(IDR_RECYCLE3, 2, useRecycle);
    if (useRecycle == 2)
    {
        const std::wstring candidate = GetWindowTextStringW(GetDlgItem(HWindow, IDE_RECYCLEMASKS));
        CMaskGroup candidateMasks(candidate.c_str(), Configuration.RecycleMasks.GetExtendedMode());
        int errorPos;
        if (!candidateMasks.PrepareMasks(errorPos))
        {
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_INCORRECTSYNTAX));
            SetFocus(GetDlgItem(HWindow, IDE_RECYCLEMASKS));
            SendMessage(GetDlgItem(HWindow, IDE_RECYCLEMASKS), EM_SETSEL, errorPos, errorPos + 1);
            ti.ErrorOn(IDE_RECYCLEMASKS);
        }
    }
}

void CCfgPageSystem::Transfer(CTransferInfo& ti)
{
    ti.RadioButton(IDR_RECYCLE1, 0, Configuration.UseRecycleBin);
    ti.RadioButton(IDR_RECYCLE2, 1, Configuration.UseRecycleBin);
    ti.RadioButton(IDR_RECYCLE3, 2, Configuration.UseRecycleBin);
    if (ti.Type == ttDataToWindow)
        SetDlgItemTextW(HWindow, IDE_RECYCLEMASKS, Configuration.RecycleMasks.GetMasksString());
    else
    {
        const std::wstring masks = GetWindowTextStringW(GetDlgItem(HWindow, IDE_RECYCLEMASKS));
        Configuration.RecycleMasks.SetMasksString(masks.c_str());
    }

    if (ti.Type == ttDataToWindow)
        EnableControls();
}

void CCfgPageSystem::EnableControls()
{
}

INT_PTR
CCfgPageSystem::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        CHyperLink* hl = new CHyperLink(HWindow, IDC_FILEMASK_HINT, STF_DOTUNDERLINE);
        if (hl != NULL)
            hl->SetActionShowHint(LoadStrW(IDS_MASKS_HINT));
        break;
    }

    case WM_COMMAND:
    {
        if (HIWORD(wParam) == BN_CLICKED)
            EnableControls();
        break;
    }
    }
    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CCfgPageColors
//

#define CFG7F_SINGLECOLOR 0x01 // only one color is valid (foreground)
#define CFG7F_DEFFG 0x02       // foreground can take the default value
#define CFG7F_DEFBK 0x04       // background can take the default value

struct CConfigurationPage7SubData
{
    int Label;    // resID of the string with the first color name [static before the button]
    BYTE ColorFg; // index of the foreground color (text) to affect
    BYTE ColorBk; // index of the background color to affect
    BYTE Flags;   // options for the item
};

struct CConfigurationPage7Data
{
    int ItemLabel; // resID of the string with the item name [combobox]
    CConfigurationPage7SubData Items[CFG_COLORS_BUTTONS];
};

#define PAGE7DATA_COUNT 7

CConfigurationPage7Data Page7Data[PAGE7DATA_COUNT] =
    {
        // panel item colors
        {
            IDS_COLORITEM_PANELITEM,
            {{IDS_COLORLABEL_NORMAL, ITEM_FG_NORMAL, ITEM_BK_NORMAL, CFG7F_DEFFG | CFG7F_DEFBK},
             {IDS_COLORLABEL_FOCUSED, ITEM_FG_FOCUSED, ITEM_BK_FOCUSED, CFG7F_DEFFG},
             {IDS_COLORLABEL_SELECTED, ITEM_FG_SELECTED, ITEM_BK_SELECTED, CFG7F_DEFBK},
             {IDS_COLORLABEL_FOCUSEDSELECTED, ITEM_FG_FOCSEL, ITEM_BK_FOCSEL, 0},
             {IDS_COLORLABEL_HIGHLIGHTED, ITEM_FG_HIGHLIGHT, ITEM_BK_HIGHLIGHT, CFG7F_DEFFG | CFG7F_DEFBK}},
        },
        // pen colors for the frame around an item
        {
            IDS_COLORITEM_FOCUSEDFRAME,
            {{IDS_COLORLABEL_ACTIVENORMAL, FOCUS_ACTIVE_NORMAL, 0, CFG7F_SINGLECOLOR | CFG7F_DEFFG},
             {IDS_COLORLABEL_ACTIVESELECTED, FOCUS_ACTIVE_SELECTED, 0, CFG7F_SINGLECOLOR | CFG7F_DEFFG},
             {IDS_COLORLABEL_INACTIVENORMAL, FOCUS_FG_INACTIVE_NORMAL, FOCUS_BK_INACTIVE_NORMAL, CFG7F_DEFBK},
             {IDS_COLORLABEL_INACTIVESELECTED, FOCUS_FG_INACTIVE_SELECTED, FOCUS_BK_INACTIVE_SELECTED, CFG7F_DEFBK},
             {0, 0, 0, 0}}},
        // pen colors for the frame around thumbnails
        {
            IDS_COLORITEM_THUMBNAILFRAME,
            {{IDS_COLORLABEL_NORMAL, THUMBNAIL_FRAME_NORMAL, 0, CFG7F_SINGLECOLOR | CFG7F_DEFFG},
             {IDS_COLORLABEL_FOCUSED, THUMBNAIL_FRAME_FOCUSED, 0, CFG7F_SINGLECOLOR | CFG7F_DEFFG},
             {IDS_COLORLABEL_SELECTED, THUMBNAIL_FRAME_SELECTED, 0, CFG7F_SINGLECOLOR},
             {IDS_COLORLABEL_FOCUSEDSELECTED, THUMBNAIL_FRAME_FOCSEL, 0, CFG7F_SINGLECOLOR},
             {0, 0, 0, 0}}},
        // colors for icon blending
        {
            IDS_COLORITEM_BLENDEDICONS,
            {{IDS_COLORLABEL_SELECTED, ICON_BLEND_SELECTED, 0, CFG7F_SINGLECOLOR | CFG7F_DEFFG},
             {IDS_COLORLABEL_FOCUSED, ICON_BLEND_FOCUSED, 0, CFG7F_SINGLECOLOR},
             {IDS_COLORLABEL_FOCUSEDSELECTED, ICON_BLEND_FOCSEL, 0, CFG7F_SINGLECOLOR},
             {0, 0, 0, 0},
             {0, 0, 0, 0}}},
        // progress bar colors
        {
            IDS_COLORITEM_PROGRESS,
            {{IDS_COLORLABEL_LEFTPART, PROGRESS_FG_SELECTED, PROGRESS_BK_SELECTED, CFG7F_DEFFG | CFG7F_DEFBK},
             {IDS_COLORLABEL_RIGHTPART, PROGRESS_FG_NORMAL, PROGRESS_BK_NORMAL, CFG7F_DEFFG | CFG7F_DEFBK},
             {0, 0, 0, 0},
             {0, 0, 0, 0},
             {0, 0, 0, 0}}},
        // panel caption colors
        {
            IDS_COLORITEM_CAPTION,
            {{IDS_COLORLABEL_ACTIVE, ACTIVE_CAPTION_FG, ACTIVE_CAPTION_BK, CFG7F_DEFFG | CFG7F_DEFBK},
             {IDS_COLORLABEL_INACTIVE, INACTIVE_CAPTION_FG, INACTIVE_CAPTION_BK, CFG7F_DEFFG | CFG7F_DEFBK},
             {0, 0, 0, 0},
             {0, 0, 0, 0},
             {0, 0, 0, 0}}},
        // hot item colors
        {
            IDS_COLORITEM_HOT,
            {{IDS_COLORLABEL_HOTPANEL, HOT_PANEL, 0, CFG7F_SINGLECOLOR | CFG7F_DEFFG},
             {IDS_COLORLABEL_HOTACTIVE, HOT_ACTIVE, 0, CFG7F_SINGLECOLOR | CFG7F_DEFFG},
             {IDS_COLORLABEL_HOTINACTIVE, HOT_INACTIVE, 0, CFG7F_SINGLECOLOR | CFG7F_DEFFG},
             {0, 0, 0, 0},
             {0, 0, 0, 0}}},
};

int CConfigurationPage7Items[CFG_COLORS_BUTTONS] = {IDC_C_ITEM1_L, IDC_C_ITEM2_L, IDC_C_ITEM3_L, IDC_C_ITEM4_L, IDC_C_ITEM5_L};
int CConfigurationPage7Masks[CFG_COLORS_BUTTONS] = {IDC_C_MASK1_L, IDC_C_MASK2_L, IDC_C_MASK3_L, IDC_C_MASK4_L, IDC_C_MASK5_L};
int CConfigurationPage7ItemsBut[CFG_COLORS_BUTTONS] = {IDC_C_ITEM1_C, IDC_C_ITEM2_C, IDC_C_ITEM3_C, IDC_C_ITEM4_C, IDC_C_ITEM5_C};
int CConfigurationPage7MasksBut[CFG_COLORS_BUTTONS] = {IDC_C_MASK1_C, IDC_C_MASK2_C, IDC_C_MASK3_C, IDC_C_MASK4_C, IDC_C_MASK5_C};

static void IndexColorSearchMetadata(sally::configuration::SearchIndex& index, int page)
{
    for (int label : {IDS_COLORSCHEME_SALAMANDER, IDS_COLORSCHEME_EXPLORER, IDS_COLORSCHEME_NORTON,
                      IDS_COLORSCHEME_NAVIGATOR, IDS_COLORSCHEME_CUSTOM})
        index.AddOptionValue(page, IDC_C_SCHEME, LoadStrOwned(label));
    for (const auto& item : Page7Data)
    {
        index.AddOptionValue(page, IDC_C_ITEM, LoadStrOwned(item.ItemLabel));
        for (const auto& color : item.Items)
            if (color.Label != 0)
                index.AddKeywords(page, IDC_C_ITEM, LoadStrOwned(color.Label));
    }
}

CCfgPageColors::CCfgPageColors()
    : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGPAGE_COLORS, IDD_CFGPAGE_COLORS, PSP_USETITLE, NULL), HighlightMasks(10, 5)
{
    HScheme = NULL;
    HItem = NULL;
    int i;
    for (i = 0; i < CFG_COLORS_BUTTONS; i++)
    {
        Items[i] = NULL;
        Masks[i] = NULL;
    }

    EditLB = NULL;
    DisableNotification = FALSE;
    SourceHighlightMasks = MainWindow->HighlightMasks;
    HighlightMasks.Load(*SourceHighlightMasks);

    Dirty = FALSE;
}

void CCfgPageColors::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CCfgPageColors::Transfer()");
    if (ti.Type == ttDataToWindow)
    {
        int i;
        for (i = 0; i < NUMBER_OF_COLORS; i++)
            TmpColors[i] = UserColors[i];

        int schemes[5] = {IDS_COLORSCHEME_SALAMANDER, IDS_COLORSCHEME_EXPLORER, IDS_COLORSCHEME_NORTON, IDS_COLORSCHEME_NAVIGATOR, IDS_COLORSCHEME_CUSTOM};
        for (i = 0; i < 5; i++)
            SendMessageW(HScheme, CB_ADDSTRING, 0, (LPARAM)LoadStrW(schemes[i]));

        for (i = 0; i < PAGE7DATA_COUNT; i++)
            SendMessageW(HItem, CB_ADDSTRING, 0, (LPARAM)LoadStrW(Page7Data[i].ItemLabel));

        int labels[CFG_COLORS_BUTTONS] = {IDS_COLORLABEL_NORMAL, IDS_COLORLABEL_FOCUSED, IDS_COLORLABEL_SELECTED, IDS_COLORLABEL_FOCUSEDSELECTED, IDS_COLORLABEL_HIGHLIGHTED};
        for (i = 0; i < CFG_COLORS_BUTTONS; i++)
            SetDlgItemTextW(HWindow, CConfigurationPage7Masks[i], LoadStrW(labels[i]));

        int index = 4; // custom
        if (CurrentColorScheme == SalamanderColors)
            index = 0;
        else if (CurrentColorScheme == ExplorerColors)
            index = 1;
        else if (CurrentColorScheme == NortonColors)
            index = 2;
        else if (CurrentColorScheme == NavigatorColors)
            index = 3;
        SendMessageW(HScheme, CB_SETCURSEL, index, 0);
        SendMessageW(HItem, CB_SETCURSEL, 0, 0);

        // populate the highlight items list
        for (i = 0; i < HighlightMasks.Count; i++)
            EditLB->AddItem((INT_PTR)HighlightMasks[i]);

        DisableNotification = TRUE;
        EditLB->SetCurSel(0);
        DisableNotification = FALSE;
        LoadColors();
        LoadMasks();
        EnableControls();
    }
    else
    {
        int index = (int)SendMessageW(HScheme, CB_GETCURSEL, 0, 0);
        if (index == 0)
            CurrentColorScheme = SalamanderColors;
        else if (index == 1)
            CurrentColorScheme = ExplorerColors;
        else if (index == 2)
            CurrentColorScheme = NortonColors;
        else if (index == 3)
            CurrentColorScheme = NavigatorColors;
        else
        {
            CurrentColorScheme = UserColors;
            int i;
            for (i = 0; i < NUMBER_OF_COLORS; i++)
                UserColors[i] = TmpColors[i];
        }

        ColorsChanged(TRUE, TRUE, FALSE); // save time, change only color-dependent items, do not reload icons

        SourceHighlightMasks->Load(HighlightMasks);
        int errPos;
        int i;
        for (i = 0; i < SourceHighlightMasks->Count; i++)
            SourceHighlightMasks->At(i)->Masks->PrepareMasks(errPos);
    }
}

void CCfgPageColors::LoadColors()
{
    CALL_STACK_MESSAGE1("CCfgPageColors::LoadColors()");

    COLORREF* tmpColors;
    int index = (int)SendMessageW(HScheme, CB_GETCURSEL, 0, 0);
    if (index == 0)
        tmpColors = SalamanderColors;
    else if (index == 1)
        tmpColors = ExplorerColors;
    else if (index == 2)
        tmpColors = NortonColors;
    else if (index == 3)
        tmpColors = NavigatorColors;
    else
        tmpColors = TmpColors;

    // let default values be retrieved
    UpdateDefaultColors(tmpColors, &HighlightMasks, TRUE, TRUE, tmpColors == TmpColors);

    index = (int)SendMessageW(HItem, CB_GETCURSEL, 0, 0);
    CConfigurationPage7Data* data = &Page7Data[index];

    int i;
    for (i = 0; i < CFG_COLORS_BUTTONS; i++)
    {
        CConfigurationPage7SubData* subData = &data->Items[i];
        const wchar_t* label;
        if (subData->Label != 0)
            label = LoadStrW(subData->Label);
        else
            label = L"";
        SetDlgItemTextW(HWindow, CConfigurationPage7Items[i], label);
        if (subData->Label != 0)
        {
            // Mirror the panel's render-time dark resolution so the base-color swatches match
            // what the panel actually shows in dark mode (these indices are stored flag-0 and
            // are no longer dark-mutated in UpdateDefaultColors). Read-only. #81 follow-up.
            BOOL swatchDark = DarkMode_ShouldUseDark();
            COLORREF sfg = ResolvePanelColor(subData->ColorFg, tmpColors[subData->ColorFg], swatchDark, tmpColors == TmpColors);
            COLORREF sbk = ResolvePanelColor(subData->ColorBk, tmpColors[subData->ColorBk], swatchDark, tmpColors == TmpColors);
            if (subData->Flags & CFG7F_SINGLECOLOR)
                Items[i]->SetColor(sfg, sfg);
            else
                Items[i]->SetColor(sfg, sbk);
        }
        ShowWindow(Items[i]->HWindow, subData->Label != 0 ? SW_SHOW : SW_HIDE);
    }

    INT_PTR itemID;
    EditLB->GetCurSelItemID(itemID);
    if (itemID != -1)
    {
        CHighlightMasksItem* item = (CHighlightMasksItem*)itemID;

        // In dark mode, mirror the render-time adjustments from SetFontAndColors
        // so the preview swatches match what the panel actually shows.
        // Read-only — does not touch stored mask data.
        BOOL isDark = DarkMode_ShouldUseDark();
        struct
        {
            SALCOLOR fg;
            SALCOLOR bk;
            int panelFg;
            int panelBk;
        } maskSwatches[] = {
            {item->NormalFg, item->NormalBk, ITEM_FG_NORMAL, ITEM_BK_NORMAL},
            {item->FocusedFg, item->FocusedBk, ITEM_FG_FOCUSED, ITEM_BK_FOCUSED},
            {item->SelectedFg, item->SelectedBk, ITEM_FG_SELECTED, ITEM_BK_SELECTED},
            {item->FocSelFg, item->FocSelBk, ITEM_FG_FOCSEL, ITEM_BK_FOCSEL},
            {item->HighlightFg, item->HighlightBk, ITEM_FG_HIGHLIGHT, ITEM_BK_HIGHLIGHT},
        };
        for (int m = 0; m < 5; m++)
        {
            COLORREF fg = GetCOLORREF(maskSwatches[m].fg);
            COLORREF bk = GetCOLORREF(maskSwatches[m].bk);
            if (GetFValue(maskSwatches[m].fg) & SCF_DEFAULT)
                fg = ResolvePanelColor(maskSwatches[m].panelFg, tmpColors[maskSwatches[m].panelFg], isDark, tmpColors == TmpColors);
            if (GetFValue(maskSwatches[m].bk) & SCF_DEFAULT)
                bk = ResolvePanelColor(maskSwatches[m].panelBk, tmpColors[maskSwatches[m].panelBk], isDark, tmpColors == TmpColors);
            Masks[m]->SetColor(fg, bk);
        }
    }
}

#define PAGE7_CTRLCOUNT 7
DWORD Page7Attributes[PAGE7_CTRLCOUNT] = {FILE_ATTRIBUTE_ARCHIVE, FILE_ATTRIBUTE_READONLY, FILE_ATTRIBUTE_HIDDEN, FILE_ATTRIBUTE_SYSTEM, FILE_ATTRIBUTE_COMPRESSED, FILE_ATTRIBUTE_ENCRYPTED, FILE_ATTRIBUTE_DIRECTORY};
DWORD Page7Controls[PAGE7_CTRLCOUNT] = {IDC_C_ARCHIVE, IDC_C_READONLY, IDC_C_HIDDEN, IDC_C_SYSTEM, IDC_C_COMPRESSED, IDC_C_ENCRYPTED, IDC_C_DIRECTORY};

void CCfgPageColors::LoadMasks()
{
    CALL_STACK_MESSAGE1("CCfgPageColors::LoadMask()");
    INT_PTR itemID;
    EditLB->GetCurSelItemID(itemID);
    BOOL empty = FALSE;
    if (itemID == -1)
        empty = TRUE;

    CHighlightMasksItem* item = NULL;
    if (!empty)
        item = (CHighlightMasksItem*)itemID;

    DisableNotification = TRUE;
    DWORD state = BST_UNCHECKED;
    int i;
    for (i = 0; i < PAGE7_CTRLCOUNT; i++)
    {
        if (!empty)
            state = item->ValidAttr & Page7Attributes[i] ? (item->Attr & Page7Attributes[i] ? BST_CHECKED : BST_UNCHECKED) : BST_INDETERMINATE;
        CheckDlgButton(HWindow, Page7Controls[i], state);
    }
    DisableNotification = FALSE;
}

void CCfgPageColors::StoreMasks()
{
    CALL_STACK_MESSAGE1("CCfgPageColors::StoreMask()");
    INT_PTR itemID;
    EditLB->GetCurSelItemID(itemID);
    if (!DisableNotification && itemID != -1)
    {
        CHighlightMasksItem* item = (CHighlightMasksItem*)itemID;
        item->ValidAttr = 0;
        item->Attr = 0;
        int i;
        for (i = 0; i < PAGE7_CTRLCOUNT; i++)
        {
            DWORD state = IsDlgButtonChecked(HWindow, Page7Controls[i]);
            if (state == BST_CHECKED || state == BST_UNCHECKED)
            {
                item->ValidAttr |= Page7Attributes[i];
                if (state == BST_CHECKED)
                    item->Attr |= Page7Attributes[i];
            }
        }
    }
}

void CCfgPageColors::EnableControls()
{
    CALL_STACK_MESSAGE1("CCfgPageColors::EnableControls()");
    BOOL validItem = TRUE;
    INT_PTR itemID;
    EditLB->GetCurSelItemID(itemID);
    if (itemID == -1)
        validItem = FALSE;

    EnableWindow(GetDlgItem(HWindow, IDC_C_ARCHIVE), validItem);
    EnableWindow(GetDlgItem(HWindow, IDC_C_READONLY), validItem);
    EnableWindow(GetDlgItem(HWindow, IDC_C_HIDDEN), validItem);
    EnableWindow(GetDlgItem(HWindow, IDC_C_SYSTEM), validItem);
    EnableWindow(GetDlgItem(HWindow, IDC_C_COMPRESSED), validItem);
    EnableWindow(GetDlgItem(HWindow, IDC_C_ENCRYPTED), validItem);
    EnableWindow(GetDlgItem(HWindow, IDC_C_DIRECTORY), validItem);

    if (!validItem)
        Masks[0]->SetColor(RGB(255, 255, 255), RGB(255, 255, 255));
    EnableWindow(GetDlgItem(HWindow, IDC_C_MASK1_C), validItem);
    if (!validItem)
        Masks[1]->SetColor(RGB(255, 255, 255), RGB(255, 255, 255));
    EnableWindow(GetDlgItem(HWindow, IDC_C_MASK2_C), validItem);
    if (!validItem)
        Masks[2]->SetColor(RGB(255, 255, 255), RGB(255, 255, 255));
    EnableWindow(GetDlgItem(HWindow, IDC_C_MASK3_C), validItem);
    if (!validItem)
        Masks[3]->SetColor(RGB(255, 255, 255), RGB(255, 255, 255));
    EnableWindow(GetDlgItem(HWindow, IDC_C_MASK4_C), validItem);
    if (!validItem)
        Masks[4]->SetColor(RGB(255, 255, 255), RGB(255, 255, 255));
    EnableWindow(GetDlgItem(HWindow, IDC_C_MASK5_C), validItem);
}

void CCfgPageColors::Validate(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CCfgPageColors::Validate()");
    if (Dirty)
    {
        int i;
        for (i = 0; i < HighlightMasks.Count; i++)
        {
            CMaskGroup masks(HighlightMasks[i]->Masks->GetMasksString());
            int errorPos1;
            if (!masks.PrepareMasks(errorPos1))
            {
                EditLB->SetCurSel(i);
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_INCORRECTSYNTAX));
                ti.ErrorOn(IDC_C_LIST);
                PostMessage(HWindow, WM_USER_EDIT, errorPos1, errorPos1 + 1);
                return;
            }
        }
    }
}

INT_PTR
CCfgPageColors::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CCfgPageColors::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);

    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        HScheme = GetDlgItem(HWindow, IDC_C_SCHEME);
        HItem = GetDlgItem(HWindow, IDC_C_ITEM);

        int i;
        for (i = 0; i < 5; i++)
        {
            Items[i] = new CColorArrowButton(HWindow, CConfigurationPage7ItemsBut[i], TRUE);
            Masks[i] = new CColorArrowButton(HWindow, CConfigurationPage7MasksBut[i], TRUE);
        }

        EditLB = new CEditListBox(HWindow, IDC_C_LIST);
        if (EditLB == NULL)
            TRACE_E(LOW_MEMORY);
        EditLB->MakeHeader(IDC_C_LIST_HEADER);
        EditLB->EnableDrag(::GetParent(HWindow));

        // dialog elements should stretch with the dialog size, set split controls
        ElasticVerticalLayout(1, IDC_C_LIST);

        break;
    }

    case WM_USER_EDIT:
    {
        SetFocus(GetDlgItem(HWindow, IDC_C_LIST));
        EditLB->OnBeginEdit((int)wParam, (int)lParam);
        return 0;
    }

    case WM_COMMAND:
    {
        if (HIWORD(wParam) == BN_CLICKED)
        {
            StoreMasks();
        }

        if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_C_SCHEME || LOWORD(wParam) == IDC_C_ITEM)
        {
            LoadColors();
            break;
        }
        if (HIWORD(wParam) == LBN_SELCHANGE && LOWORD(wParam) == IDC_C_LIST)
        {
            EditLB->OnSelChanged();
            LoadMasks();
            LoadColors();
            EnableControls();
            break;
        }

        WORD id = LOWORD(wParam);
        if (id == IDC_C_ITEM1_C || id == IDC_C_ITEM2_C || id == IDC_C_ITEM3_C || id == IDC_C_ITEM4_C || id == IDC_C_ITEM5_C ||
            id == IDC_C_MASK1_C || id == IDC_C_MASK2_C || id == IDC_C_MASK3_C || id == IDC_C_MASK4_C || id == IDC_C_MASK5_C)
        {
            CColorArrowButton* button = (CColorArrowButton*)WindowsManager.GetWindowPtr((HWND)lParam);
            HMENU hMenu = CreatePopupMenu();

            BOOL item = (id == IDC_C_ITEM1_C || id == IDC_C_ITEM2_C || id == IDC_C_ITEM3_C || id == IDC_C_ITEM4_C || id == IDC_C_ITEM5_C);

            CHighlightMasksItem* highlightItem = NULL;
            if (!item)
            {
                INT_PTR itemID;
                EditLB->GetCurSelItemID(itemID);
                if (itemID == -1)
                {
                    TRACE_E("This should never happen!");
                    break;
                }
                highlightItem = (CHighlightMasksItem*)itemID;
            }
            BOOL singleColor = FALSE;
            BOOL enabledFg = FALSE;
            BOOL enabledBk = FALSE;
            BOOL checkedDefaultFg = FALSE;
            BOOL checkedDefaultBk = FALSE;
            COLORREF* tmpColors = NULL;
            CConfigurationPage7Data* data = NULL;
            CConfigurationPage7SubData* subData = NULL;
            SALCOLOR* fgColor = NULL;
            SALCOLOR* bkColor = NULL;
            if (item)
            {
                data = &Page7Data[SendMessageW(HItem, CB_GETCURSEL, 0, 0)];
                int index;

                index = (int)SendMessageW(HScheme, CB_GETCURSEL, 0, 0);
                if (index == 0)
                    tmpColors = SalamanderColors;
                else if (index == 1)
                    tmpColors = ExplorerColors;
                else if (index == 2)
                    tmpColors = NortonColors;
                else if (index == 3)
                    tmpColors = NavigatorColors;
                else
                    tmpColors = TmpColors;

                if (id == IDC_C_ITEM1_C)
                    index = 0;
                else if (id == IDC_C_ITEM2_C)
                    index = 1;
                else if (id == IDC_C_ITEM3_C)
                    index = 2;
                else if (id == IDC_C_ITEM4_C)
                    index = 3;
                else if (id == IDC_C_ITEM5_C)
                    index = 4;

                subData = &data->Items[index];

                if (GetFValue(tmpColors[subData->ColorFg]) & SCF_DEFAULT)
                    checkedDefaultFg = TRUE;
                if (subData->Flags & CFG7F_SINGLECOLOR)
                    singleColor = TRUE;
                else if (GetFValue(tmpColors[subData->ColorBk]) & SCF_DEFAULT)
                    checkedDefaultBk = TRUE;

                if (subData->Flags & CFG7F_DEFFG)
                    enabledFg = TRUE;
                if (subData->Flags & CFG7F_DEFBK)
                    enabledBk = TRUE;
            }
            else
            {
                enabledFg = TRUE;
                enabledBk = TRUE;

                switch (id)
                {
                case IDC_C_MASK1_C:
                {
                    fgColor = &highlightItem->NormalFg;
                    bkColor = &highlightItem->NormalBk;
                    break;
                }

                case IDC_C_MASK2_C:
                {
                    fgColor = &highlightItem->FocusedFg;
                    bkColor = &highlightItem->FocusedBk;
                    break;
                }

                case IDC_C_MASK3_C:
                {
                    fgColor = &highlightItem->SelectedFg;
                    bkColor = &highlightItem->SelectedBk;
                    break;
                }

                case IDC_C_MASK4_C:
                {
                    fgColor = &highlightItem->FocSelFg;
                    bkColor = &highlightItem->FocSelBk;
                    break;
                }

                case IDC_C_MASK5_C:
                {
                    fgColor = &highlightItem->HighlightFg;
                    bkColor = &highlightItem->HighlightBk;
                    break;
                }
                }

                if (GetFValue(*fgColor) & SCF_DEFAULT)
                    checkedDefaultFg = TRUE;
                if (GetFValue(*bkColor) & SCF_DEFAULT)
                    checkedDefaultBk = TRUE;
            }
            int maxCmd;
            if (singleColor)
            {
                /* used by the export_mnu.py script that generates salmenu.mnu for the Translator,
    keep synchronized with the InsertMenu() call below...
MENU_TEMPLATE_ITEM CfgPageColorsMenu1[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_SETCOLOR_CUSTOM
  {MNTT_IT, IDS_SETCOLOR_SYSTEM
  {MNTT_PE, 0
};
*/
                InsertMenuW(hMenu, 0xFFFFFFFF, checkedDefaultFg ? 0 : MF_CHECKED | MF_BYCOMMAND | MF_STRING, 1, LoadStrW(IDS_SETCOLOR_CUSTOM));
                InsertMenuW(hMenu, 0xFFFFFFFF, checkedDefaultFg ? MF_CHECKED : 0 | MF_BYCOMMAND | MF_STRING | enabledFg ? 0
                                                                                                                       : MF_GRAYED,
                           2, LoadStrW(IDS_SETCOLOR_SYSTEM));
                maxCmd = 2;
            }
            else
            {
                /* used by the export_mnu.py script that generates salmenu.mnu for the Translator,
    keep synchronized with the InsertMenu() call below...
MENU_TEMPLATE_ITEM CfgPageColorsMenu2[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_SETCOLOR_CUSTOM_FG
  {MNTT_IT, IDS_SETCOLOR_SYSTEM_FG
  {MNTT_IT, IDS_SETCOLOR_CUSTOM_BK
  {MNTT_IT, IDS_SETCOLOR_SYSTEM_BK
  {MNTT_PE, 0
};
MENU_TEMPLATE_ITEM CfgPageColorsMenu3[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_SETCOLOR_CUSTOM_FG
  {MNTT_IT, IDS_SETCOLOR_DEFAULT_FG
  {MNTT_IT, IDS_SETCOLOR_CUSTOM_BK
  {MNTT_IT, IDS_SETCOLOR_DEFAULT_BK
  {MNTT_PE, 0
};
*/
                InsertMenuW(hMenu, 0xFFFFFFFF, checkedDefaultFg ? 0 : MF_CHECKED | MF_BYCOMMAND | MF_STRING, 1, LoadStrW(IDS_SETCOLOR_CUSTOM_FG));
                int textResID = (item || id == IDC_C_MASK5_C) ? IDS_SETCOLOR_SYSTEM_FG : IDS_SETCOLOR_DEFAULT_FG;
                InsertMenuW(hMenu, 0xFFFFFFFF, checkedDefaultFg ? MF_CHECKED : 0 | MF_BYCOMMAND | MF_STRING | enabledFg ? 0
                                                                                                                        : MF_GRAYED,
                           2, LoadStrW(textResID));
                InsertMenuW(hMenu, 0xFFFFFFFF, MF_BYCOMMAND | MF_SEPARATOR, 0, NULL);
                InsertMenuW(hMenu, 0xFFFFFFFF, checkedDefaultBk ? 0 : MF_CHECKED | MF_BYCOMMAND | MF_STRING, 3, LoadStrW(IDS_SETCOLOR_CUSTOM_BK));
                textResID = (item || id == IDC_C_MASK5_C) ? IDS_SETCOLOR_SYSTEM_BK : IDS_SETCOLOR_DEFAULT_BK;
                InsertMenuW(hMenu, 0xFFFFFFFF, checkedDefaultBk ? MF_CHECKED : 0 | MF_BYCOMMAND | MF_STRING | enabledBk ? 0
                                                                                                                        : MF_GRAYED,
                           4, LoadStrW(textResID));
                maxCmd = 4;
            }

            //        int i;
            //        for (i = 0; i < maxCmd; i++)
            //          SetMenuItemBitmaps(hMenu, i + 1, MF_BYCOMMAND, NULL, HMenuCheckDot);

            TPMPARAMS tpmPar;
            tpmPar.cbSize = sizeof(tpmPar);
            GetWindowRect(button->HWindow, &tpmPar.rcExclude);
            DWORD cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON, tpmPar.rcExclude.right, tpmPar.rcExclude.top,
                                         HWindow, &tpmPar);
            if (cmd != 0)
            {
                const auto useCustomPalette = [&]()
                {
                    const int scheme = (int)SendMessageW(HScheme, CB_GETCURSEL, 0, 0);
                    if (!item || scheme < 0 || scheme >= 4)
                        return;
                    for (int i = 0; i < NUMBER_OF_COLORS; i++)
                        TmpColors[i] = tmpColors[i];
                    SendMessageW(HScheme, CB_SETCURSEL, 4, 0);
                };

                if (cmd == 1 || cmd == 3)
                {
                    CHOOSECOLOR cc;
                    cc.lStructSize = sizeof(cc);
                    cc.hwndOwner = HWindow;
                    cc.lpCustColors = (LPDWORD)CustomColors;

                    if (cmd == 1)
                        cc.rgbResult = button->GetTextColor();
                    else
                        cc.rgbResult = button->GetBkgndColor();
                    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
                    if (ChooseColor(&cc))
                    {
                        if (item)
                        {
                            int colorIndex = (cmd == 1) ? subData->ColorFg : subData->ColorBk;
                            BYTE flags = GetFValue(tmpColors[colorIndex]);
                            flags &= ~SCF_DEFAULT;
                            const SALCOLOR chosen = cc.rgbResult & 0x00ffffff | (((DWORD)flags) << 24);
                            if (chosen != tmpColors[colorIndex])
                            {
                                useCustomPalette();
                                TmpColors[colorIndex] = chosen;
                            }
                        }
                        else
                        {
                            // masks
                            SALCOLOR* color = (cmd == 1) ? fgColor : bkColor;
                            BYTE flags = GetFValue(*color);
                            flags &= ~SCF_DEFAULT;
                            *color = cc.rgbResult & 0x00ffffff | (((DWORD)flags) << 24);
                        }
                    }
                }
                else
                {
                    // cmd == 2 || cmd == 4
                    if (item)
                    {
                        int colorIndex = (cmd == 2) ? subData->ColorFg : subData->ColorBk;
                        BYTE flags = GetFValue(tmpColors[colorIndex]);
                        flags |= SCF_DEFAULT;
                        const SALCOLOR chosen = tmpColors[colorIndex] & 0x00ffffff | (((DWORD)flags) << 24);
                        if (chosen != tmpColors[colorIndex])
                        {
                            useCustomPalette();
                            TmpColors[colorIndex] = chosen;
                        }
                    }
                    else
                    {
                        SALCOLOR* color = (cmd == 2) ? fgColor : bkColor;
                        BYTE flags = GetFValue(*color);
                        flags |= SCF_DEFAULT;
                        *color = *color & 0x00ffffff | (((DWORD)flags) << 24);
                    }
                }
                // load the colors into the buttons
                LoadColors();
            }
            DestroyMenu(hMenu);
            return 0;
        }
        break;
    }
    case WM_NOTIFY:
    {
        NMHDR* nmhdr = (NMHDR*)lParam;
        switch (nmhdr->idFrom)
        {
        case IDC_C_LIST:
        {
            switch (nmhdr->code)
            {
            case EDTLBN_GETDISPINFO:
            {
                EDTLB_DISPINFO* dispInfo = (EDTLB_DISPINFO*)lParam;
                if (dispInfo->ToDo == edtlbGetData)
                {
                    *dispInfo->Text = ((CHighlightMasksItem*)dispInfo->ItemID)->Masks->GetMasksString();
                    SetWindowLongPtr(HWindow, DWLP_MSGRESULT, FALSE);
                    return TRUE;
                }
                else
                {
                    Dirty = TRUE;
                    CHighlightMasksItem* item;
                    if (dispInfo->ItemID == -1)
                    {
                        item = new CHighlightMasksItem();
                        if (item == NULL)
                        {
                            TRACE_E(LOW_MEMORY);
                            SetWindowLongPtr(HWindow, DWLP_MSGRESULT, TRUE);
                            return TRUE;
                        }
                        HighlightMasks.Add(item);
                        item->Set(dispInfo->Text->c_str());
                        EditLB->SetItemData((INT_PTR)item);
                        PostMessage(HWindow, WM_COMMAND, MAKELPARAM(IDC_C_LIST, LBN_SELCHANGE), (LPARAM)EditLB->HWindow);
                    }
                    else
                    {
                        item = (CHighlightMasksItem*)dispInfo->ItemID;
                        item->Set(dispInfo->Text->c_str());
                    }

                    LoadMasks();
                    LoadColors();
                    EnableControls();
                    SetWindowLongPtr(HWindow, DWLP_MSGRESULT, TRUE);
                    return TRUE;
                }
                break;
            }
                /*
            case EDTLBN_MOVEITEM:
            {
              EDTLB_DISPINFO *dispInfo = (EDTLB_DISPINFO *)lParam;
              int index;
              EditLB->GetCurSel(index);
              int srcIndex = index;
              int dstIndex = index + (dispInfo->Up ? -1 : 1);

              wchar_t buf[sizeof(CHighlightMasksItem)];
              memcpy(buf, HighlightMasks[srcIndex], sizeof(CHighlightMasksItem));
              memcpy(HighlightMasks[srcIndex], HighlightMasks[dstIndex], sizeof(CHighlightMasksItem));
              memcpy(HighlightMasks[dstIndex], buf, sizeof(CHighlightMasksItem));

              SetWindowLongPtr(HWindow, DWLP_MSGRESULT, FALSE);  // allow swapping
              return TRUE;
            }
*/
            case EDTLBN_MOVEITEM2:
            {
                EDTLB_DISPINFO* dispInfo = (EDTLB_DISPINFO*)lParam;
                int index;
                EditLB->GetCurSel(index);
                int srcIndex = index;
                int dstIndex = dispInfo->NewIndex;

                wchar_t buf[sizeof(CHighlightMasksItem)];
                memcpy(buf, HighlightMasks[srcIndex], sizeof(CHighlightMasksItem));
                if (srcIndex < dstIndex)
                {
                    int i;
                    for (i = srcIndex; i < dstIndex; i++)
                        memcpy(HighlightMasks[i], HighlightMasks[i + 1], sizeof(CHighlightMasksItem));
                }
                else
                {
                    int i;
                    for (i = srcIndex; i > dstIndex; i--)
                        memcpy(HighlightMasks[i], HighlightMasks[i - 1], sizeof(CHighlightMasksItem));
                }
                memcpy(HighlightMasks[dstIndex], buf, sizeof(CHighlightMasksItem));

                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, FALSE); // allow change
                return TRUE;
            }

            case EDTLBN_DELETEITEM:
            {
                int index;
                EditLB->GetCurSel(index);
                HighlightMasks.Delete(index);
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, FALSE); // allow deletion
                return TRUE;
            }
            }
            break;
        }
        }
        break;
    }

    case WM_DRAWITEM:
    {
        int idCtrl = (int)wParam;
        if (idCtrl == IDC_C_LIST)
        {
            EditLB->OnDrawItem(lParam);
            return TRUE;
        }
        break;
    }
    }
    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CCfgPageHistory
//

CCfgPageHistory::CCfgPageHistory()
    : CCommonPropSheetPage(NULL, HLanguage, IDD_CFGPAGE_HISTORY, IDD_CFGPAGE_HISTORY, PSP_USETITLE, NULL)
{
}

void CCfgPageHistory::Transfer(CTransferInfo& ti)
{
    ti.CheckBox(IDC_HISTORY_SAVEHISTORY, Configuration.SaveHistory);
    ti.CheckBox(IDC_HISTORY_WORKDIRS, Configuration.SaveWorkDirs);
    ti.CheckBox(IDC_HISTORY_ENABLECMDLINE, Configuration.EnableCmdLineHistory);
    ti.CheckBox(IDC_HISTORY_SAVECMDLINE, Configuration.SaveCmdLineHistory);

    if (ti.Type == ttDataToWindow)
        EnableControls();
    else
        MainWindow->EditWindow->FillHistory();
}

void CCfgPageHistory::EnableControls()
{
    BOOL saveHistory = IsDlgButtonChecked(HWindow, IDC_HISTORY_SAVEHISTORY) == BST_CHECKED;
    if (!saveHistory)
    {
        CheckDlgButton(HWindow, IDC_HISTORY_WORKDIRS, BST_UNCHECKED);
    }
    EnableWindow(GetDlgItem(HWindow, IDC_HISTORY_WORKDIRS), saveHistory);
    BOOL enableCmdLineHistory = IsDlgButtonChecked(HWindow, IDC_HISTORY_ENABLECMDLINE) == BST_CHECKED;
    if (!saveHistory || !enableCmdLineHistory)
        CheckDlgButton(HWindow, IDC_HISTORY_SAVECMDLINE, BST_UNCHECKED);
    EnableWindow(GetDlgItem(HWindow, IDC_HISTORY_SAVECMDLINE), saveHistory && enableCmdLineHistory);
}

void CCfgPageHistory::OnClearHistory()
{
    // set panel paths to fixed (otherwise leaving the path could
    // immediately populate Alt+F12, etc.)
    if (MainWindow->LeftPanel != NULL)
        MainWindow->LeftPanel->ChangeToRescuePathOrFixedDrive(HWindow);
    if (MainWindow->RightPanel != NULL)
        MainWindow->RightPanel->ChangeToRescuePathOrFixedDrive(HWindow);

    // main window history (FileHistory, DirHistory)
    MainWindow->ClearHistory();

    // history from the configuration (SelectHistory, CopyHistory, EditHistory, ChangeDirHistory, FileListHistory)
    Configuration.ClearHistory();
    MainWindow->EditWindow->FillHistory();

    // Find dialog history including combobox of open windows
    ClearFindHistory(FALSE);

    // internal viewer history including combobox of open Find windows
    ClearViewerHistory(FALSE);

    // storage of selected names
    GlobalSelection.Clear();

    // history of both panels (PathHistory, FilterHistory, Selection)
    if (MainWindow->LeftPanel != NULL)
        MainWindow->LeftPanel->ClearHistory();
    if (MainWindow->RightPanel != NULL)
        MainWindow->RightPanel->ClearHistory();

    // clear history in all plugins (they remain loaded so that a subsequent
    // Save can remove data from the Registry)
    Plugins.ClearHistory(HWindow);

    // also clear the memory of recently used directories on individual drives
    InitDefaultDir();
}

INT_PTR
CCfgPageHistory::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDC_HISTORY_CLEARHISTORY:
        {
            if (gPrompter->AskYesNo(LoadStrW(IDS_QUESTION), LoadStrW(IDS_CLEARHISTORY_CNFRM)).type == PromptResult::kYes)
            {
                OnClearHistory();
                gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_HISTORY_WAS_CLEARED));
            }
            return 0;
        }

        case IDC_HISTORY_SAVEHISTORY:
        case IDC_HISTORY_ENABLECMDLINE:
        {
            EnableControls();
            break;
        }
        }
        break;
    }
    }
    return CCommonPropSheetPage::DialogProc(uMsg, wParam, lParam);
}
