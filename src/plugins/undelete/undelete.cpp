// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// ****************************************************************************
//
//  UNDELETE
//
//
//      +=============+   +=========+  +---------+  +----+
//      | UNDELETE    |   | RESTORE |  | MISCSTR |  | OS |
//      +=============+   +=========+  +---------+  +----+
//             |
//      +======*======+                    +-------------+
//      | FS1         |-------------------*| DIALOGS     |
//      +=============+                    +------*------+
//             |                                  |
//      +======*=========================================+
//      | FS2                                            |
//      +================================================+
//                      |                         |
//      +---------------*-------------+    +------*------+
//      | SNAPSHOT                    |*---| STREAM      |
//      +-----------------------------+    +-------------+
//          |          |          |               |
//      +---*---+  +---*---+  +---*---+           |
//      | NTFS  |  | FAT   |  | EXFAT |           |
//      +-------+  +-------+  +-------+           |
//          |          |          |               |
//      +---*----------*----------*---------------*------+
//      | VOLUME                                         |
//      +------------------------------------------------+
//
//  Legenda: A -* B ... A uses B
//           ====== ... Salamander dependent
//           ------ ... independent
//

#include "precomp.h"

#include "undelete.rh"
#include "undelete.rh2"
#include "lang\lang.rh"

#include "library\undelete.h"
#include "miscstr.h"
#include "os.h"
#include "volume.h"
#include "snapshot.h"
#include "dialogs.h"
#include "undelete.h"
#include "restore.h"

#define UNDELETE_WIDEN_IMPL(value) L##value
#define UNDELETE_WIDEN(value) UNDELETE_WIDEN_IMPL(value)

HINSTANCE DLLInstance = NULL; // handle for SPL - language independent resources
HINSTANCE HLanguage = NULL;   // handle for SLG - language dependent resources

CPluginInterface PluginInterface;
//CPluginInterfaceForMenuExt InterfaceForMenuExt;
CPluginInterfaceForFS InterfaceForFS;
CPluginInterfaceForMenuExt InterfaceForMenuExt;

CSalamanderGeneralAbstract* SalamanderGeneral = NULL;
CSalamanderDebugAbstract* SalamanderDebug = NULL;
CSalamanderSafeFileAbstract* SalamanderSafeFile = NULL;
CSalamanderGUIAbstract* SalamanderGUI = NULL;

int SalamanderVersion = 0;

CLUSTER_MAP_I cluster_map;

// ****************************************************************************
//
//  CTopIndexMem
//

void CTopIndexMem::Push(const wchar_t* path, int topIndex)
{
    CALL_STACK_MESSAGE3("CTopIndexMem::Push(%ls, %d)", path, topIndex);

    // detect if path continues after Path (path==Path+"\\name")
    const wchar_t* s = path + wcslen(path);
    if (s > path && *(s - 1) == L'\\')
        s--;
    BOOL ok;
    if (s == path)
        ok = FALSE;
    else
    {
        if (s > path && *s == L'\\')
            s--;
        while (s > path && *s != L'\\')
            s--;

        int l = (int)Path.size();
        if (l > 0 && Path[l - 1] == L'\\')
            l--;
        ok = s - path == l && SalamanderGeneral->StrNICmp(path, Path.c_str(), l) == 0;
    }

    if (ok) // it continues -> store next top-index
    {
        if (TopIndexesCount == TOP_INDEX_MEM_SIZE) // we need to release first top-index
        {
            int i;
            for (i = 0; i < TOP_INDEX_MEM_SIZE - 1; i++)
                TopIndexes[i] = TopIndexes[i + 1];
            TopIndexesCount--;
        }
        Path.assign(path);
        TopIndexes[TopIndexesCount++] = topIndex;
    }
    else // it doesn't continue -> first top-index v raw
    {
        Path.assign(path);
        TopIndexesCount = 1;
        TopIndexes[0] = topIndex;
    }
}

BOOL CTopIndexMem::FindAndPop(const wchar_t* path, int& topIndex)
{
    CALL_STACK_MESSAGE3("CTopIndexMem::FindAndPop(%ls, %d)", path, topIndex);

    // detect if path match to Path (path==Path)
    int l1 = (int)wcslen(path);
    if (l1 > 0 && path[l1 - 1] == L'\\')
        l1--;
    int l2 = (int)Path.size();
    if (l2 > 0 && Path[l2 - 1] == L'\\')
        l2--;
    if (l1 == l2 && SalamanderGeneral->StrNICmp(path, Path.c_str(), l1) == 0)
    {
        if (TopIndexesCount > 0)
        {
            size_t end = Path.size();
            if (end > 0 && Path[end - 1] == L'\\')
                --end;
            const size_t separator = end == 0 ? std::wstring::npos : Path.rfind(L'\\', end - 1);
            Path.erase(separator == std::wstring::npos ? 0 : separator);
            topIndex = TopIndexes[--TopIndexesCount];
            return TRUE;
        }
        else // we don't have this item anymore (it wasn't stored or was released due to low memory)
        {
            Clear();
            return FALSE;
        }
    }
    else // another path -> release memory, it is long jump
    {
        Clear();
        return FALSE;
    }
}

// ****************************************************************************
//
//   InitIconOverlays
//

void InitIconOverlays()
{
    // 48x48 only from XP onward (will soon be obsolete; we'll run on XP+ only, then drop this)
    // in fact large icons have been supported for a long time, they can be enabled
    // Desktop/Properties/???/Large Icons; beware, the system image list will not exist then
    // for 32x32 icons; additionally we should pull the actual icon sizes from the system
    // for now we ignore it and enable 48x48 only from XP where they are commonly available
    int iconSizes[3] = {16, 32, 48};
    if (!SalIsWindowsVersionOrGreater(5, 1, 0)) // not WindowsXPAndLater: this is not XP or later
        iconSizes[2] = 32;

    HICON iconOverlays[3];
    for (int i = 0; i < 3; i++)
    {
        iconOverlays[i] = (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_DELETED),
                                           IMAGE_ICON, iconSizes[i], iconSizes[i],
                                           SalamanderGeneral->GetIconLRFlags());
    }

    // NOTE: if loading icons fails, SetPluginIconOverlays() returns failure but releases the valid icons from iconOverlays[]
    SalamanderGeneral->SetPluginIconOverlays(1, iconOverlays);
}

// ****************************************************************************
//
//   DllMain and SalamanderPluginEntry
//

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DLLInstance = hinstDLL;

        INITCOMMONCONTROLSEX initCtrls;
        initCtrls.dwSize = sizeof(INITCOMMONCONTROLSEX);
        initCtrls.dwICC = ICC_USEREX_CLASSES;
        if (!InitCommonControlsEx(&initCtrls))
        {
            // wide: English-only diagnostic, no LoadStr involved (language module
            // isn't even loaded yet at DllMain time).
            MessageBoxW(NULL, L"InitCommonControlsEx failed!", L"Error", MB_OK | MB_ICONERROR);
            return FALSE; // DLL won't start
        }
    }

    return TRUE; // DLL can be loaded
}

void WINAPI HTMLHelpCallback(HWND hWindow, UINT helpID)
{
    SalamanderGeneral->OpenHtmlHelp(hWindow, HHCDisplayContext, helpID, FALSE);
}

CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander)
{
    // set SalamanderDebug for "dbg.h"
    SalamanderDebug = salamander->GetSalamanderDebug();
    SalamanderVersion = salamander->GetVersion();
    HANDLES_CAN_USE_TRACE();
    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

    // works with current and newer Salamander version - check it out
    if (SalamanderVersion < LAST_VERSION_OF_SALAMANDER)
    { // deny old versions
        // wide: same call-site-local widen shape as checksum.cpp/unlha.cpp (205,206).
        MessageBoxW(salamander->GetParentWindow(), UNDELETE_WIDEN(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"Undelete" /* DO NOT TRANSLATE! */, MB_OK | MB_ICONERROR);
        return NULL;
    }

    // load language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"Undelete" /* neprekladat! */);
    if (HLanguage == NULL)
        return NULL;

    // get Salamander interfaces
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalamanderSafeFile = salamander->GetSalamanderSafeFile();
    SalamanderGUI = salamander->GetSalamanderGUI();

    // set help file name
    SalamanderGeneral->SetHelpFileName(L"undelete.chm");

    // init
    if (!OS<wchar_t>::OS_InitLibraryData())
        return NULL;
    if (!InitFS())
    {
        OS<wchar_t>::OS_ReleaseLibraryData();
        return NULL;
    }
    InitializeWinLib(L"Undelete" /* DO NOT TRANSLATE! */, DLLInstance);
    SetupWinLibHelp(HTMLHelpCallback);

    InitIconOverlays();

    // set basic information about plugin
    salamander->SetBasicPluginData(String<wchar_t>::LangStr(IDS_UNDELETE).c_str(),
                                   FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION |
                                       FUNCTION_FILESYSTEM,
                                   UNDELETE_WIDEN(VERSINFO_VERSION_NO_PLATFORM),
                                   UNDELETE_WIDEN(VERSINFO_COPYRIGHT),
                                   String<wchar_t>::LangStr(IDS_DESCRIPTION).c_str(),
                                   L"UNDELETE" /* DO NOT TRANSLATE! */, NULL, L"del");

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    // get our FS-name (it could be different than "del", Salamander could change it)
    AssignedFSName = SPLGetPluginFSNameOwned(SalamanderGeneral, 0);

    return &PluginInterface;
}

int WINAPI SalamanderPluginGetReqVer()
{
    return LAST_VERSION_OF_SALAMANDER;
}

// ****************************************************************************
//
//   CPluginInterface
//

void WINAPI CPluginInterface::About(HWND parent)
{
    const std::wstring message = SPLFormatStringOwned(
        L"%s %s\n\n%s\n\n%s",
        String<wchar_t>::LangStr(IDS_UNDELETE).c_str(),
        UNDELETE_WIDEN(VERSINFO_VERSION), UNDELETE_WIDEN(VERSINFO_COPYRIGHT),
        String<wchar_t>::LangStr(IDS_DESCRIPTION).c_str());
    SalamanderGeneral->SalMessageBox(parent, message.c_str(), String<wchar_t>::LangStr(IDS_ABOUTTITLE).c_str(), MB_OK | MB_ICONINFORMATION);
}

BOOL WINAPI CPluginInterface::Release(HWND parent, BOOL force)
{
    CALL_STACK_MESSAGE2("CPluginInterface::Release(, %d)", force);
    ReleaseFS();
    OS<wchar_t>::OS_ReleaseLibraryData();
    ReleaseWinLib(DLLInstance);
    /*if (ret && InterfaceForFS.GetActiveFSCount() != 0)
  {
    TRACE_E("Some FS interfaces were not closed (count=" << InterfaceForFS.GetActiveFSCount() << ")");
  }*/
    // fixme
    return TRUE;
}

void WINAPI CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");
    /*
  HBITMAP hBmp = (HBITMAP) HANDLES(LoadImage(DLLInstance, MAKEINTRESOURCE(IDB_FS),
                                   IMAGE_BITMAP, 16, 16, LR_DEFAULTCOLOR));
  salamander->SetBitmapWithIcons(hBmp);
  HANDLES(DeleteObject(hBmp));
  */
    // switch to icons with alpha channel support
    CGUIIconListAbstract* iconList = SalamanderGUI->CreateIconList();
    iconList->Create(16, 16, 1);
    //  HICON hIcon = (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_FS), IMAGE_ICON, 16, 16, SalamanderGeneral->GetIconLRFlags());
    HICON hIcon = OS<wchar_t>::OS_GetEmptyRecycleBinIcon(FALSE);
    iconList->ReplaceIcon(0, hIcon);
    DestroyIcon(hIcon);
    salamander->SetIconListForGUI(iconList); // will be destroyed by Salamander

    salamander->SetChangeDriveMenuItem(String<wchar_t>::LangStr(IDS_UNDELETEINCHDRVMENU).c_str(), 0);
    salamander->SetPluginIcon(0);
    salamander->SetPluginMenuAndToolbarIcon(0);

    /* used by the export_mnu.py script, which generates salmenu.mnu for Translator
   keep synchronized with the salamander->AddMenuItem() calls below...
MENU_TEMPLATE_ITEM PluginMenu[] = 
{
	{MNTT_PB, 0
	{MNTT_IT, IDS_UNDELETECMD
	{MNTT_IT, IDS_RESTORECMD
	{MNTT_PE, 0
};
*/

    // for better discoverability put plugin also to Plugins menu
    salamander->AddMenuItem(-1, String<wchar_t>::LangStr(IDS_UNDELETECMD).c_str(), SALHOTKEY('U', HOTKEYF_CONTROL | HOTKEYF_SHIFT),
                            CMD_UNDELETE, FALSE, MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);

    salamander->AddMenuItem(-1, String<wchar_t>::LangStr(IDS_RESTORECMD).c_str(), 0,
                            CMD_RESTORE_ENCRYPTED, FALSE, MENU_EVENT_FILE_FOCUSED | MENU_EVENT_DIR_FOCUSED | MENU_EVENT_FILES_SELECTED | MENU_EVENT_DIRS_SELECTED, MENU_EVENT_DISK | MENU_EVENT_TARGET_DISK, MENU_SKILLLEVEL_ALL);
}

void WINAPI CPluginInterface::ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData)
{
    delete ((CPluginFSDataInterface*)pluginData);
}

CPluginInterfaceForFSAbstract* WINAPI CPluginInterface::GetInterfaceForFS()
{
    return &InterfaceForFS;
}

CPluginInterfaceForMenuExtAbstract* WINAPI CPluginInterface::GetInterfaceForMenuExt()
{
    return &InterfaceForMenuExt;
}

void WINAPI CPluginInterface::Event(int event, DWORD param)
{
    if (event == PLUGINEVENT_COLORSCHANGED)
    {
        InitIconOverlays();

        // DFSImageList != NULL required, entry-point would fail otherwise
        // COLORREF bkColor = SalamanderGeneral->GetCurrentColor(SALCOL_ITEM_BK_NORMAL);
        // if (ImageList_GetBkColor(DFSImageList) != bkColor)
        //   ImageList_SetBkColor(DFSImageList, bkColor);
    }
}

// ****************************************************************************
//
// CPluginInterfaceForMenuExt
//

BOOL CPluginInterfaceForMenuExt::ExecuteMenuItem(CSalamanderForOperationsAbstract* SalOp,
                                                 HWND parent, int id, DWORD eventMask)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForMenuExt::ExecuteMenuItem( , , %ld, %X)", id, eventMask);

    switch (id)
    {
    case CMD_UNDELETE:
    {
        InterfaceForFS.ExecuteChangeDriveMenuItem(PANEL_SOURCE);
        return FALSE;
    }

    case CMD_RESTORE_ENCRYPTED:
    {
        CRestoreDialog dlg(parent);
        if (dlg.Execute() == IDCANCEL)
            return FALSE;
        return RestoreEncryptedFiles(dlg.TargetPath.c_str(), parent);
        // SalamanderGeneral->SalMessageBox(parent, "Not implemented yet.", "Restore", MB_OK | MB_ICONINFORMATION);
        // return FALSE;
    }

    default:
    {
        TRACE_E("Invalid menu item ID");
    }
    }
    return FALSE;
}

BOOL CPluginInterfaceForMenuExt::HelpForMenuItem(HWND parent, int id)
{
    int helpID = 0;
    switch (id)
    {
    case 1:
        helpID = IDH_UNDELETE;
        break;
    case 2:
        helpID = IDH_RESTOREENCRFILES;
        break;
    }
    if (helpID != 0)
        SalamanderGeneral->OpenHtmlHelp(parent, HHCDisplayContext, helpID, FALSE);
    return helpID != 0;
}

// ****************************************************************************
//
// Config
//

BOOL ConfigAlwaysReuseScanInfo;
BOOL ConfigScanVacantClusters;
BOOL ConfigShowExistingFiles;
BOOL ConfigShowZeroFiles;
BOOL ConfigShowEmptyDirs;
BOOL ConfigShowMetafiles;
BOOL ConfigEstimateDamage;
std::wstring ConfigTempPath;
BOOL ConfigDontShowEncryptedWarning;
BOOL ConfigDontShowSamePartitionWarning;
int ConditionFixedWidth = 0; // column Condition (FS): LO/HI-WORD: left/right panel: FixedWidth
int ConditionWidth = 0;      // column Condition (FS): LO/HI-WORD: left/right panel: Width

// wide: registry key NAMES only (never recovered-file data) - matches
// CSalamanderRegistryAbstract::GetValue/SetValue's wide-only 'name' parameter.
static const wchar_t* KEY_ALWAYSREUSE = L"Always Reuse Scan Info";
static const wchar_t* KEY_SCANVACENT = L"Scan Vacant Clusters";
static const wchar_t* KEY_SHOWEXISTING = L"Show Existing Files";
static const wchar_t* KEY_SHOWZEROFILES = L"Show Zero Files";
static const wchar_t* KEY_SHOWEMPTYDIRS = L"Show Empty Dirs";
static const wchar_t* KEY_SHOWMETAFILES = L"Show Metafiles";
static const wchar_t* KEY_ESTIMATEDAMAGE = L"Estimate Damage";
static const wchar_t* KEY_TEMPPATH = L"Alternate Temp Path";
static const wchar_t* KEY_DONTSHOWENCRYPTED = L"Dont Show Encrypted Warning";
static const wchar_t* KEY_DONTSHOWSAMEPARTITION = L"Dont Show Same Partition Warning";
static const wchar_t* KEY_CONDITIONFIXEDWIDTH = L"Condition Fixed Width";
static const wchar_t* KEY_CONDITIONWIDTH = L"Condition Width";

void CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::LoadConfiguration(, ,)");

    // default values
    ConfigAlwaysReuseScanInfo = FALSE;
    ConfigScanVacantClusters = TRUE;
    ConfigShowExistingFiles = FALSE;
    ConfigShowZeroFiles = TRUE;
    ConfigShowEmptyDirs = TRUE;
    ConfigShowMetafiles = FALSE;
    ConfigEstimateDamage = TRUE;
    ConfigTempPath.clear();
    ConfigDontShowEncryptedWarning = FALSE;
    ConfigDontShowSamePartitionWarning = FALSE;
    ConditionFixedWidth = 0;
    ConditionWidth = 0;

    if (regKey != NULL) // load from the Registry
    {
        registry->GetValue(regKey, KEY_ALWAYSREUSE, REG_DWORD, &ConfigAlwaysReuseScanInfo, sizeof(DWORD));
        registry->GetValue(regKey, KEY_SCANVACENT, REG_DWORD, &ConfigScanVacantClusters, sizeof(DWORD));
        registry->GetValue(regKey, KEY_SHOWEXISTING, REG_DWORD, &ConfigShowExistingFiles, sizeof(DWORD));
        registry->GetValue(regKey, KEY_SHOWZEROFILES, REG_DWORD, &ConfigShowZeroFiles, sizeof(DWORD));
        registry->GetValue(regKey, KEY_SHOWEMPTYDIRS, REG_DWORD, &ConfigShowEmptyDirs, sizeof(DWORD));
        registry->GetValue(regKey, KEY_SHOWMETAFILES, REG_DWORD, &ConfigShowMetafiles, sizeof(DWORD));
        registry->GetValue(regKey, KEY_ESTIMATEDAMAGE, REG_DWORD, &ConfigEstimateDamage, sizeof(DWORD));
        SPLRegistryGetStringOwned(registry, regKey, KEY_TEMPPATH, ConfigTempPath);
        registry->GetValue(regKey, KEY_DONTSHOWENCRYPTED, REG_DWORD, &ConfigDontShowEncryptedWarning, sizeof(DWORD));
        registry->GetValue(regKey, KEY_DONTSHOWSAMEPARTITION, REG_DWORD, &ConfigDontShowSamePartitionWarning, sizeof(DWORD));
        registry->GetValue(regKey, KEY_CONDITIONFIXEDWIDTH, REG_DWORD, &ConditionFixedWidth, sizeof(DWORD));
        registry->GetValue(regKey, KEY_CONDITIONWIDTH, REG_DWORD, &ConditionWidth, sizeof(DWORD));
    }
}

void CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, ,)");

    registry->SetValue(regKey, KEY_ALWAYSREUSE, REG_DWORD, &ConfigAlwaysReuseScanInfo, sizeof(DWORD));
    registry->SetValue(regKey, KEY_SCANVACENT, REG_DWORD, &ConfigScanVacantClusters, sizeof(DWORD));
    registry->SetValue(regKey, KEY_SHOWEXISTING, REG_DWORD, &ConfigShowExistingFiles, sizeof(DWORD));
    registry->SetValue(regKey, KEY_SHOWZEROFILES, REG_DWORD, &ConfigShowZeroFiles, sizeof(DWORD));
    registry->SetValue(regKey, KEY_SHOWEMPTYDIRS, REG_DWORD, &ConfigShowEmptyDirs, sizeof(DWORD));
    registry->SetValue(regKey, KEY_SHOWMETAFILES, REG_DWORD, &ConfigShowMetafiles, sizeof(DWORD));
    registry->SetValue(regKey, KEY_ESTIMATEDAMAGE, REG_DWORD, &ConfigEstimateDamage, sizeof(DWORD));
    SPLRegistrySetString(registry, regKey, KEY_TEMPPATH, ConfigTempPath);
    registry->SetValue(regKey, KEY_DONTSHOWENCRYPTED, REG_DWORD, &ConfigDontShowEncryptedWarning, sizeof(DWORD));
    registry->SetValue(regKey, KEY_DONTSHOWSAMEPARTITION, REG_DWORD, &ConfigDontShowSamePartitionWarning, sizeof(DWORD));
    registry->SetValue(regKey, KEY_CONDITIONFIXEDWIDTH, REG_DWORD, &ConditionFixedWidth, sizeof(DWORD));
    registry->SetValue(regKey, KEY_CONDITIONWIDTH, REG_DWORD, &ConditionWidth, sizeof(DWORD));
}

void CPluginInterface::Configuration(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Configuration()");
    CConfigDialog dlg(parent);
    dlg.Execute();
}
