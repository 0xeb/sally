// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "dbg.h"

#include "..\7zip\7za\c\7zVersion.h"

#include "7zip.h"
#include "dialogs.h"
#include "7zip.rh"
#include "7zip.rh2"
#include "lang\lang.rh"

#define INITGUID

#include "7zclient.h"

#define ZIP7_WIDEN_IMPL(value) L##value
#define ZIP7_WIDEN(value) ZIP7_WIDEN_IMPL(value)

// ****************************************************************************

HINSTANCE DLLInstance = NULL; // handle to the SPL module - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to the SLG module - language-dependent resources

// plugin interface object; its methods are called from Salamander
CPluginInterface PluginInterface;
// CPluginInterface section for the archiver
CPluginInterfaceForArchiver InterfaceForArchiver;

// CPluginInterface section for the viewer
//CPluginInterfaceForViewer InterfaceForViewer;

// interface for the menu
CPluginInterfaceForMenuExt InterfaceForMenuExt;

// Salamander general interface - valid from startup until the plugin is unloaded
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;

// interface for convenient work with files
CSalamanderSafeFileAbstract* SalamanderSafeFile = NULL;

// define the variable for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

// define the variable for "spl_com.h"
int SalamanderVersion = 0;

// interface providing customized Windows controls used in Salamander
CSalamanderGUIAbstract* SalamanderGUI = NULL;

int ConfigVersion = 0;

// CURRENT_CONFIG_VERSION history
// 1: ?
// 2: ?
// 3: Igor changed the default values for LZMA compression (dictionary size, etc.). There are more changes,
//    so Honza Patera and I agreed that when importing old configurations we will
//    ignore compression settings and use the new defaults instead.
#define CURRENT_CONFIG_VERSION 3
const wchar_t* CONFIG_VERSION = L"Version";

CConfig Config;

int SortByExtDirsAsFiles = FALSE; // current value of the Salamander configuration variable SALCFG_SORTBYEXTDIRSASFILES

// global variables used to store pointers to Salamander's global variables
// shared for both the archiver and FS
const CFileData** TransferFileData = NULL;
int* TransferIsDir = NULL;
wchar_t* TransferBuffer = NULL;
int* TransferLen = NULL;
DWORD* TransferRowData = NULL;
CPluginDataInterfaceAbstract** TransferPluginDataIface = NULL;
DWORD* TransferActCustomData = NULL;

// names of the entries in the registry
const wchar_t* CONFIG_SHOW_EXTENDED_OPTIONS = L"Show Extended Options";
const wchar_t* CONFIG_EXTENDED_LIST_INFO = L"Extended List Info";
const wchar_t* CONFIG_LIST_INFO_PACKED_SIZE = L"List Info Packed Size";
const wchar_t* CONFIG_LIST_INFO_METHOD = L"List Info Method";

const wchar_t* CONFIG_COL_PACKEDSIZE_FIXEDWIDTH = L"Column PackedSize FixedWidth";
const wchar_t* CONFIG_COL_PACKEDSIZE_WIDTH = L"Column PackedSize Width";
const wchar_t* CONFIG_COL_METHOD_FIXEDWIDTH = L"Column Method FixedWidth";
const wchar_t* CONFIG_COL_METHOD_WIDTH = L"Column Method Width";

const wchar_t* CONFIG_COMPRESS_LEVEL = L"Compression Level";
const wchar_t* CONFIG_COMPRESS_METHOD = L"Compression Method";
const wchar_t* CONFIG_DICT_SIZE = L"Dictionary Size";
const wchar_t* CONFIG_WORD_SIZE = L"Word Size";
const wchar_t* CONFIG_SOLID_ARCHIVE = L"Solid Archive";

// menu id
#define IDM_TESTARCHIVE 1

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DLLInstance = hinstDLL;
        break;
    }

    case DLL_PROCESS_DETACH:
    {
        break;
    }
    }
    return TRUE; // DLL can be loaded
}

// Wide - SalamanderGeneral->LoadStr has returned WCHAR* since the v108 ABI break.
std::wstring LangStr(int resID)
{
    return SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID);
}

int WINAPI SalamanderPluginGetReqVer()
{
    return LAST_VERSION_OF_SALAMANDER;
}

void WINAPI HTMLHelpCallback(HWND hWindow, UINT helpID)
{
    SalamanderGeneral->OpenHtmlHelp(hWindow, HHCDisplayContext, helpID, FALSE);
}

CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander)
{
    // set SalamanderDebug for "dbg.h"
    SalamanderDebug = salamander->GetSalamanderDebug();
    // set SalamanderVersion for "spl_com.h"
    SalamanderVersion = salamander->GetVersion();

    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

    // this plugin is built for the current version of Salamander and newer - verify that
    if (SalamanderVersion < LAST_VERSION_OF_SALAMANDER)
    { // reject older versions
        // wide: same call-site-local widen shape as checksum/unlha/undelete/zip/
        // splitcbn/pak/ftp/diskmap/demoplug (205-217).
        MessageBoxW(salamander->GetParentWindow(),
                    ZIP7_WIDEN(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"7-Zip" /* neprekladat! */, MB_OK | MB_ICONERROR);
        return NULL;
    }

    // load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"7-Zip" /* neprekladat! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain Salamander's general interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalamanderSafeFile = salamander->GetSalamanderSafeFile();
    // obtain the interface that provides customized Windows controls used in Salamander
    SalamanderGUI = salamander->GetSalamanderGUI();

    // set the help file name
    SalamanderGeneral->SetHelpFileName(L"7zip.chm");

    SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &SortByExtDirsAsFiles,
                                          sizeof(SortByExtDirsAsFiles), NULL);

    if (!InterfaceForArchiver.Init())
        return NULL;

    if (!InitializeWinLib(L"7zip", DLLInstance))
        return NULL;
    SetWinLibStrings(L"Invalid number!", LangStr(IDS_PLUGINNAME).c_str());
    SetupWinLibHelp(HTMLHelpCallback);

    // set the basic plugin information
    salamander->SetBasicPluginData(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(),
                                   FUNCTION_PANELARCHIVERVIEW | FUNCTION_CUSTOMARCHIVERUNPACK |
                                       FUNCTION_PANELARCHIVEREDIT | FUNCTION_CUSTOMARCHIVERPACK |
                                       FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION,
                                   ZIP7_WIDEN(VERSINFO_VERSION_NO_PLATFORM), ZIP7_WIDEN(VERSINFO_COPYRIGHT),
                                   SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGIN_DESCRIPTION).c_str(), L"7zip", L"7z");

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    return &PluginInterface;
}

BOOL Warning(int resID, BOOL quiet, ...)
{
    if (!quiet)
    {
        va_list arglist;
        va_start(arglist, quiet);
        const std::wstring text = SPLFormatStringOwnedV(
            SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID).c_str(), arglist);
        va_end(arglist);

        SalamanderGeneral->ShowMessageBox(text.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_WARNING);
    }
    return FALSE;
}

BOOL Error(int resID, BOOL quiet, ...)
{
    if (!quiet)
    {
        va_list arglist;
        va_start(arglist, quiet);
        const std::wstring text = SPLFormatStringOwnedV(
            SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID).c_str(), arglist);
        va_end(arglist);

        SalamanderGeneral->ShowMessageBox(text.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
    }
    return FALSE;
}

BOOL Error(HWND hParent, int resID, BOOL quiet, ...)
{
    if (!quiet)
    {
        va_list arglist;
        va_start(arglist, quiet);
        const std::wstring text = SPLFormatStringOwnedV(
            SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID).c_str(), arglist);
        va_end(arglist);

        SalamanderGeneral->SalMessageBox(hParent, text.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MB_OK);
    }
    return FALSE;
}

BOOL ErrorWidePath(int resID, BOOL quiet, const wchar_t* path)
{
    if (!quiet)
    {
        const std::wstring text = SPLFormatStringOwned(
            SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID).c_str(), path);
        SalamanderGeneral->ShowMessageBox(text.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
    }
    return FALSE;
}

/*
BOOL
SysError(char *msg, DWORD err, BOOL quiet)
{
  if (!quiet)
    if (err != ERROR_FILE_NOT_FOUND && err != ERROR_NO_MORE_FILES)  // this is an error
    {
      char buf[1024];
      sprintf(buf, "%s\n\n%s", msg, SalamanderGeneral->GetErrorText(err));
      SalamanderGeneral->ShowMessageBox(ToWideArg(buf).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
    }

  return FALSE;
}
*/

BOOL SysError(int resID, DWORD err, BOOL quiet, ...)
{
    if (!quiet)
    {
        va_list arglist;
        va_start(arglist, quiet);
        const std::wstring message = SPLFormatStringOwnedV(
            SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID).c_str(), arglist);
        va_end(arglist);

        const std::wstring errorText = SPLGetErrorTextOwned(SalamanderGeneral, err);
        const std::wstring text = SPLFormatStringOwned(
            L"%ls\n\n%ls", message.c_str(), errorText.c_str());
        SalamanderGeneral->ShowMessageBox(text.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
    }

    return FALSE;
}

BOOL SysErrorWidePath(int resID, DWORD err, BOOL quiet, const wchar_t* path)
{
    if (!quiet)
    {
        const std::wstring message = SPLFormatStringOwned(
            SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID).c_str(), path);
        const std::wstring errorText = SPLGetErrorTextOwned(SalamanderGeneral, err);
        const std::wstring text = SPLFormatStringOwned(
            L"%ls\n\n%ls", message.c_str(), errorText.c_str());
        SalamanderGeneral->ShowMessageBox(text.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
    }
    return FALSE;
}
/*
BOOL SysError(int title, int error, ...)
{
  int lastErr = GetLastError();
  CALL_STACK_MESSAGE3("SysError(%d, %d, ...)", title, error);
  char buf[1024];
  *buf = 0;
  va_list arglist;
  va_start(arglist, error);
  vsprintf(buf, LangStr(error).c_str(), arglist);
  va_end(arglist);
  if (lastErr != ERROR_SUCCESS)
  {
    strcat(buf, " ");
    int l = strlen(buf);
    FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, lastErr,
                  MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf + l, 1024 - l, NULL);
  }
  SalamanderGeneral->ShowMessageBox(ToWideArg(buf).c_str(), LangStr(title).c_str(), MSGBOX_ERROR);
  return FALSE;
}
*/

static std::wstring FormatLocalTime(const SYSTEMTIME& value)
{
    const int required = GetTimeFormatW(
        LOCALE_USER_DEFAULT, 0, &value, NULL, NULL, 0);
    if (required > 0)
    {
        std::vector<wchar_t> buffer(static_cast<size_t>(required), L'\0');
        if (GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &value, NULL, buffer.data(),
                           required) > 0)
            return std::wstring(buffer.data());
    }
    return SPLFormatStringOwned(
        L"%u:%02u:%02u", value.wHour, value.wMinute, value.wSecond);
}

static std::wstring FormatLocalDate(const SYSTEMTIME& value)
{
    const int required = GetDateFormatW(
        LOCALE_USER_DEFAULT, DATE_SHORTDATE, &value, NULL, NULL, 0);
    if (required > 0)
    {
        std::vector<wchar_t> buffer(static_cast<size_t>(required), L'\0');
        if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &value, NULL,
                           buffer.data(), required) > 0)
            return std::wstring(buffer.data());
    }
    return SPLFormatStringOwned(
        L"%u.%u.%u", value.wDay, value.wMonth, value.wYear);
}

std::wstring GetInfoW(const FILETIME* lastWrite, UINT64 size)
{
    CALL_STACK_MESSAGE2("GetInfoW(, , 0x%I64X)", size);
    SYSTEMTIME st;
    FILETIME ft;
    FileTimeToLocalFileTime(lastWrite, &ft);
    FileTimeToSystemTime(&ft, &st);

    CQuadWord qwsize;
    qwsize.Value = size;

    const std::wstring time = FormatLocalTime(st);
    const std::wstring date = FormatLocalDate(st);
    const std::wstring number = SPLNumberToStrOwned(SalamanderGeneral, qwsize);
    return SPLFormatStringOwned(
        L"%ls, %ls, %ls", number.c_str(), date.c_str(), time.c_str());
}

BOOL SafeDeleteFile(const wchar_t* fileName, BOOL& silent)
{
    int mbRet;

    do
    {
        mbRet = DIALOG_OK;
        if (!DeleteFileW(fileName) && !silent)
        {
            DWORD err = ::GetLastError();
            const std::wstring errorText = SPLGetErrorTextOwned(SalamanderGeneral, err);
            mbRet = SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(), BUTTONS_RETRYSKIPCANCEL,
                                                   fileName, errorText.c_str(), LangStr(IDS_DELETE_ERROR).c_str());
        }
    } while (mbRet == DIALOG_RETRY);

    BOOL ret = FALSE;
    switch (mbRet)
    {
    case DIALOG_OK:
        ret = TRUE;
        break;
    case DIALOG_CANCEL:
        ret = FALSE;
        break;
    case DIALOG_SKIP:
        ret = TRUE;
        break;
    case DIALOG_SKIPALL:
        ret = TRUE;
        silent = TRUE;
        break;
    }

    return ret;
}

// ****************************************************************************
//
// CPluginInterface
//

void CPluginInterface::About(HWND parent)
{
    const std::wstring text = SPLFormatStringOwned(
        L"%ls %ls\n7za.dll %ls\n\n%ls\n%ls\n\n%ls",
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(),
        ZIP7_WIDEN(VERSINFO_VERSION), ZIP7_WIDEN(MY_VERSION),
        ZIP7_WIDEN(VERSINFO_COPYRIGHT), ZIP7_WIDEN(MY_COPYRIGHT_CR),
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGIN_DESCRIPTION).c_str());
    SalamanderGeneral->SalMessageBox(parent, text.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ABOUT).c_str(), MB_OK | MB_ICONINFORMATION);
}

BOOL CPluginInterface::Release(HWND parent, BOOL force)
{
    CALL_STACK_MESSAGE2("CPluginInterface::Release(, %d)", force);

    ReleaseWinLib(DLLInstance);

    return TRUE;
}

void CPluginInterface::SetDefaultConfiguration()
{
    Config.ExtendedListInfo = FALSE;
    Config.ListInfoMethod = FALSE;
    Config.ListInfoPackedSize = FALSE;

    Config.ColumnPackedSizeFixedWidth = 0;
    Config.ColumnPackedSizeWidth = 0;
    Config.ColumnMethodFixedWidth = 0;
    Config.ColumnMethodWidth = 0;

    Config.ShowExtendedOptions = TRUE;

    Config.CompressParams.CompressLevel = COMPRESS_LEVEL_NORMAL;
    Config.CompressParams.Method = CCompressParams::LZMA;
    Config.CompressParams.DictSize = 16 * 1024; // Dictionary Size in KB
    Config.CompressParams.WordSize = 32;
    Config.CompressParams.SolidArchive = TRUE;
}

void CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::LoadConfiguration(, ,)");

    if (regKey != NULL) // load from registry
    {
        if (!registry->GetValue(regKey, CONFIG_VERSION, REG_DWORD, &ConfigVersion, sizeof(DWORD)))
            ConfigVersion = 0; // default configuration
    }
    else
    {
        ConfigVersion = 0; // default configuration
    }

    // set config defaults
    SetDefaultConfiguration();

    if (regKey != NULL) // load from registry
    {
        registry->GetValue(regKey, CONFIG_SHOW_EXTENDED_OPTIONS, REG_DWORD, &Config.ShowExtendedOptions, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_EXTENDED_LIST_INFO, REG_DWORD, &Config.ExtendedListInfo, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_LIST_INFO_PACKED_SIZE, REG_DWORD, &Config.ListInfoPackedSize, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_LIST_INFO_METHOD, REG_DWORD, &Config.ListInfoMethod, sizeof(DWORD));

        registry->GetValue(regKey, CONFIG_COL_PACKEDSIZE_FIXEDWIDTH, REG_DWORD, &Config.ColumnPackedSizeFixedWidth, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_COL_PACKEDSIZE_WIDTH, REG_DWORD, &Config.ColumnPackedSizeWidth, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_COL_METHOD_FIXEDWIDTH, REG_DWORD, &Config.ColumnMethodFixedWidth, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_COL_METHOD_WIDTH, REG_DWORD, &Config.ColumnMethodWidth, sizeof(DWORD));

        if (ConfigVersion >= 1)
        {
            // compress params
            registry->GetValue(regKey, CONFIG_SOLID_ARCHIVE, REG_DWORD, &Config.CompressParams.SolidArchive, sizeof(DWORD));
            if (registry->GetValue(regKey, CONFIG_COMPRESS_METHOD, REG_DWORD, &Config.CompressParams.Method, sizeof(DWORD)) &&
                (Config.CompressParams.Method != CCompressParams::LZMA || ConfigVersion >= 3)) // for configuration version 3 we reset the defaults for LZMA because they differ
            {
                registry->GetValue(regKey, CONFIG_COMPRESS_LEVEL, REG_DWORD, &Config.CompressParams.CompressLevel, sizeof(DWORD));
                registry->GetValue(regKey, CONFIG_DICT_SIZE, REG_DWORD, &Config.CompressParams.DictSize, sizeof(DWORD));
                registry->GetValue(regKey, CONFIG_WORD_SIZE, REG_DWORD, &Config.CompressParams.WordSize, sizeof(DWORD));
            }
        }
    }
}

void CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, ,)");

    DWORD v = CURRENT_CONFIG_VERSION;
    registry->SetValue(regKey, CONFIG_VERSION, REG_DWORD, &v, sizeof(DWORD));

    registry->SetValue(regKey, CONFIG_SHOW_EXTENDED_OPTIONS, REG_DWORD, &Config.ShowExtendedOptions, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_EXTENDED_LIST_INFO, REG_DWORD, &Config.ExtendedListInfo, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_LIST_INFO_PACKED_SIZE, REG_DWORD, &Config.ListInfoPackedSize, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_LIST_INFO_METHOD, REG_DWORD, &Config.ListInfoMethod, sizeof(DWORD));

    registry->SetValue(regKey, CONFIG_COL_PACKEDSIZE_FIXEDWIDTH, REG_DWORD, &Config.ColumnPackedSizeFixedWidth, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_COL_PACKEDSIZE_WIDTH, REG_DWORD, &Config.ColumnPackedSizeWidth, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_COL_METHOD_FIXEDWIDTH, REG_DWORD, &Config.ColumnMethodFixedWidth, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_COL_METHOD_WIDTH, REG_DWORD, &Config.ColumnMethodWidth, sizeof(DWORD));

    // config version == 2
    // compress params
    registry->SetValue(regKey, CONFIG_COMPRESS_LEVEL, REG_DWORD, &Config.CompressParams.CompressLevel, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_COMPRESS_METHOD, REG_DWORD, &Config.CompressParams.Method, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_DICT_SIZE, REG_DWORD, &Config.CompressParams.DictSize, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_WORD_SIZE, REG_DWORD, &Config.CompressParams.WordSize, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_SOLID_ARCHIVE, REG_DWORD, &Config.CompressParams.SolidArchive, sizeof(DWORD));
}

void CPluginInterface::Configuration(HWND parent)
{
    CConfigurationDialog dlg(parent);

    dlg.Cfg = Config;
    if (dlg.Execute() == IDOK)
    {
        Config = dlg.Cfg;
        if (SalamanderGeneral->GetPanelPluginData(PANEL_LEFT) != NULL)
            SalamanderGeneral->PostRefreshPanelPath(PANEL_LEFT);
        if (SalamanderGeneral->GetPanelPluginData(PANEL_RIGHT) != NULL)
            SalamanderGeneral->PostRefreshPanelPath(PANEL_RIGHT);
    }
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    // when adding more extensions we must raise CURRENT_CONFIG_VERSION

    // BASIC SECTION
    // AddViewer and AddPanelArchiver will fall under the UPGRADE SECTION
    //  salamander->AddViewer("*.7z", FALSE); // default (plugin install), otherwise Salamander ignores it

    salamander->AddPanelArchiver(L"7z", TRUE, FALSE);

    salamander->AddCustomPacker(L"7-Zip (Plugin)", L"7z", ConfigVersion < 1);
    salamander->AddCustomUnpacker(L"7-Zip (Plugin)", L"*.7z", ConfigVersion < 1);

    /* used by the export_mnu.py script, which generates salmenu.mnu for the Translator
   keep it synchronized with the calls to salamander->AddMenuItem() below...
MENU_TEMPLATE_ITEM PluginMenu[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_TESTARCHIVE
  {MNTT_PE, 0
};
*/

    // add a diagnostic menu item that lets users easily send the first 1 MB of an ISO image
    // add a menu item for testing archive integrity
    salamander->AddMenuItem(-1, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_TESTARCHIVE).c_str(), 0, IDM_TESTARCHIVE, FALSE, MENU_EVENT_ARCHIVE_FOCUSED,
                            MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);

    // set the plugin icon
    HBITMAP hBmp = (HBITMAP)LoadImage(DLLInstance, MAKEINTRESOURCE(IDB_7ZIP),
                                      IMAGE_BITMAP, 16, 16, LR_DEFAULTCOLOR);
    salamander->SetBitmapWithIcons(hBmp);
    DeleteObject(hBmp);
    salamander->SetPluginIcon(0);
    salamander->SetPluginMenuAndToolbarIcon(0);

    /*
  // UPGRADE SECTION
  if (ConfigVersion < 2) // add nrg, pdi, cdi, cif, ncd
  {
    salamander->AddViewer("*.nrg;*.pdi;*.cdi;*.cif;*.ncd", TRUE);
    salamander->AddPanelArchiver("nrg;pdi;cdi;cif;ncd", FALSE, TRUE);
  }

  if (ConfigVersion < 3) // add c2d
  {
    salamander->AddViewer("*.c2d", TRUE);
    salamander->AddPanelArchiver("c2d", FALSE, TRUE);
  }
*/
}

void CPluginInterface::Event(int event, DWORD param)
{
    if (event == PLUGINEVENT_CONFIGURATIONCHANGED)
    {
        SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &SortByExtDirsAsFiles,
                                              sizeof(SortByExtDirsAsFiles), NULL);
    }
}

void CPluginInterface::ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData)
{
    delete ((CPluginDataInterface*)pluginData);
}

CPluginInterfaceForArchiverAbstract*
CPluginInterface::GetInterfaceForArchiver()
{
    return &InterfaceForArchiver;
}

/*
CPluginInterfaceForViewerAbstract *
CPluginInterface::GetInterfaceForViewer()
{
  return &InterfaceForViewer;
}
*/

CPluginInterfaceForMenuExtAbstract*
CPluginInterface::GetInterfaceForMenuExt()
{
    return &InterfaceForMenuExt;
}

// ****************************************************************************
//
// CPluginInterfaceForArchiver
//

CPluginInterfaceForArchiver::CPluginInterfaceForArchiver()
{
}

BOOL CPluginInterfaceForArchiver::Init()
{
    CALL_STACK_MESSAGE1("CPluginInterfaceForArchiver::Init()");

    return TRUE;
}

BOOL CPluginInterfaceForArchiver::ListArchive(CSalamanderForOperationsAbstract* salamander,
                                              const wchar_t* fileName,
                                              CSalamanderDirectoryAbstract* dir,
                                              CPluginDataInterfaceAbstract*& pluginDataPar)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ListArchive(, %ls, ,)", fileName);

    C7zClient* client = new C7zClient();
    if (client == NULL)
        return Error(IDS_INSUFFICIENT_MEMORY);

    CPluginDataInterface* pluginData;
    pluginDataPar = pluginData = new CPluginDataInterface(client);
    if (pluginData == NULL)
    {
        delete client;
        return Error(IDS_INSUFFICIENT_MEMORY);
    }

    // open the archive
    // pass the complete listing to the Salamander core
    if (!client->ListArchive(fileName, dir, pluginData, pluginData->Password))
    {
        delete pluginData;
        return FALSE;
    }

    return TRUE;
}

void FreeString(void* strig)
{
    free(strig);
}

BOOL CPluginInterfaceForArchiver::UnpackArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                CPluginDataInterfaceAbstract* pluginDataPar, const wchar_t* targetDir,
                                                const wchar_t* archiveRoot, SalEnumSelection next, void* nextParam)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackArchive(, %ls, , %ls, %ls, ,)", fileName, targetDir, archiveRoot);

    if (pluginDataPar == NULL)
    {
        TRACE_E("Internal error");
        return FALSE;
    }

    CPluginDataInterface* pluginData = (CPluginDataInterface*)pluginDataPar;
    C7zClient* client = pluginData->Get7zClient();
    if (client == NULL)
    {
        TRACE_E("Internal error");
        return FALSE;
    }

    BOOL ret = FALSE;
    // compute 'totalSize' for the progress dialog
    BOOL isDir;
    CQuadWord size;
    CQuadWord totalSize(0, 0);
    int itemCount = 0; // number of processed items
    const wchar_t* name;
    const CFileData* fileData;
    int errorOccured;

    // first compute how much we will process and how large it will be
    while ((name = next(SalamanderGeneral->GetMsgBoxParent(), 1, &isDir, &size, &fileData, nextParam, &errorOccured)) != NULL)
    {
        if (fileData->PluginData != 0)
            itemCount++;

        totalSize += size;
    }
    // check whether an error occurred and the user did not request cancellation
    if (errorOccured == SALENUM_CANCEL)
        return FALSE;

    BOOL delTempDir = TRUE;
    if (itemCount > 0 &&
        SalamanderGeneral->TestFreeSpace(SalamanderGeneral->GetMsgBoxParent(),
                                         targetDir, totalSize, LangStr(IDS_UNPACKING_ARCHIVE).c_str()))
    {
        salamander->OpenProgressDialog(LangStr(IDS_UNPACKING_ARCHIVE).c_str(), FALSE, NULL, FALSE);
        salamander->ProgressDialogAddText(LangStr(IDS_READING_ARCHIVEITEMS).c_str(), FALSE);

        // store the processed items in an array
        TIndirectArray<CArchiveItemInfo> itemList(itemCount, 10, dtDelete);
        next(NULL, -1, NULL, NULL, NULL, nextParam, NULL);
        while ((name = next(NULL /* we do not log errors the second time */, 1, &isDir, &size, &fileData, nextParam, NULL)) != NULL)
        {
            if (fileData->PluginData != 0)
            {
                CArchiveItemInfo* aii = new CArchiveItemInfo(name, fileData, isDir == TRUE);
                if (aii == NULL)
                    return Error(IDS_INSUFFICIENT_MEMORY);
                itemList.Add(aii);
            }
        }

        salamander->ProgressDialogAddText(LangStr(IDS_UNPACKING).c_str(), FALSE);
        ret = client->Decompress(salamander, fileName, targetDir, &itemList, pluginData->Password) != OPER_CANCEL;
        salamander->CloseProgressDialog();
    }

    return ret;
}

BOOL CPluginInterfaceForArchiver::UnpackOneFile(CSalamanderForOperationsAbstract* salamander,
                                                const wchar_t* fileName, CPluginDataInterfaceAbstract* pluginDataPar,
                                                const wchar_t* nameInArchive, const CFileData* fileData,
                                                const wchar_t* targetDir, const wchar_t* newFileName,
                                                BOOL* renamingNotSupported)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackOneFile(, %ls, , %ls, , %ls, ,)", fileName,
                        nameInArchive, targetDir);

    if (newFileName != NULL)
    {
        *renamingNotSupported = TRUE;
        return FALSE;
    }

    if (pluginDataPar == NULL)
    {
        TRACE_E("Internal error");
        return FALSE;
    }

    CPluginDataInterface* pluginData = (CPluginDataInterface*)pluginDataPar;
    C7zClient* client = pluginData->Get7zClient();
    if (client == NULL)
    {
        TRACE_E("Internal error");
        return FALSE;
    }

    BOOL ret = FALSE;
    TIndirectArray<CArchiveItemInfo> archiveItems(1, 10, dtDelete);
    if (fileData->PluginData != 0)
    {
        CArchiveItemInfo* aii = new CArchiveItemInfo(fileData->Name, fileData, FALSE);
        if (aii == NULL)
            return Error(IDS_INSUFFICIENT_MEMORY);
        archiveItems.Add(aii);

        if (SalamanderGeneral->TestFreeSpace(SalamanderGeneral->GetMsgBoxParent(),
                                             targetDir, CQuadWord(fileData->Size), LangStr(IDS_UNPACKING_ARCHIVE).c_str()))
        {
            salamander->OpenProgressDialog(LangStr(IDS_UNPACKING_ARCHIVE).c_str(), FALSE, NULL, FALSE);
            ret = client->Decompress(salamander, fileName, targetDir, &archiveItems, pluginData->Password, TRUE) != OPER_CANCEL;
            //      ret = client->Decompress(salamander, fileName, targetDir, &archiveItems) == OPER_OK;
            salamander->CloseProgressDialog();
        }
    }

    return ret;
}

// NOTE: it is assumed itemCount is inited on entry
void CalcSize(CSalamanderDirectoryAbstract const* dir, const wchar_t* mask, CQuadWord& size, int& itemCount)
{
    int count = dir->GetFilesCount();
    int i;
    for (i = 0; i < count; i++)
    {
        CFileData const* file = dir->GetFile(i);
        //    TRACE_I("CalcSize(): file: " << path << (path[0] != 0 ? "\\" : "") << file->Name);

        if (SalamanderGeneral->AgreeMask(file->Name, mask, file->Ext[0] != 0))
        {
            size += file->Size + CQuadWord(1, 0);
            if (file->PluginData != 0)
                itemCount++;
        }
    }

    count = dir->GetDirsCount();
    int j;
    for (j = 0; j < count; j++)
    {
        CFileData const* file = dir->GetDir(j);
        if (file->PluginData != 0)
            itemCount++;
        CSalamanderDirectoryAbstract const* subDir = dir->GetSalDir(j);
        CalcSize(subDir, mask, size, itemCount);
    }
}

int GatherItems(CSalamanderDirectoryAbstract const* dir, const wchar_t* mask, TIndirectArray<CArchiveItemInfo>* archiveItems, std::wstring& archivePath)
{
    const size_t archivePathLen = archivePath.size();
    // files
    int count = dir->GetFilesCount();
    int i;
    for (i = 0; i < count; i++)
    {
        CFileData const* fileData = dir->GetFile(i);
        if (SalamanderGeneral->AgreeMask(fileData->Name, mask, fileData->Ext[0] != 0) &&
            fileData->PluginData != 0)
        {
            SPLSalPathAppendOwned(archivePath, fileData->Name); // reuse archivePath for the file name in the archive
            CArchiveItemInfo* aii = new CArchiveItemInfo(archivePath.c_str(), fileData, FALSE);
            archivePath.resize(archivePathLen);
            if (aii == NULL)
            {
                Error(IDS_INSUFFICIENT_MEMORY);
                return OPER_CANCEL;
            }
            archiveItems->Add(aii);
        }
    } // for

    count = dir->GetDirsCount();
    int j;
    for (j = 0; j < count; j++)
    {
        CFileData const* fileData = dir->GetDir(j);
        CSalamanderDirectoryAbstract const* subDir = dir->GetSalDir(j);
        SPLSalPathAppendOwned(archivePath, fileData->Name);
        // include directories in processing (but only those that have PluginData defined)
        if (SalamanderGeneral->AgreeMask(fileData->Name, mask,
                                         wcschr(fileData->Name, L'.') != NULL) && // for a directory the extension may be missing, so we look for the dot via strchr
            fileData->PluginData != 0)
        {
            CArchiveItemInfo* aii = new CArchiveItemInfo(archivePath.c_str(), fileData, TRUE);
            if (aii == NULL)
            {
                Error(IDS_INSUFFICIENT_MEMORY);
                return OPER_CANCEL;
            }
            archiveItems->Add(aii);
        }

        if (GatherItems(subDir, mask, archiveItems, archivePath) == OPER_CANCEL)
            return OPER_CANCEL;
        archivePath.resize(archivePathLen);
    }

    return OPER_OK;
}

BOOL CPluginInterfaceForArchiver::UnpackWholeArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                     const wchar_t* mask, const wchar_t* targetDir, BOOL delArchiveWhenDone,
                                                     CDynamicString* archiveVolumes)
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForArchiver::UnpackWholeArchive(, %ls, %ls, %ls, %d,)",
                        fileName, mask, targetDir, delArchiveWhenDone);

    if (delArchiveWhenDone)
        archiveVolumes->Add(fileName, -2); // FIXME: once the 7-zip plugin learns multi-volume archives (.7z.001, .7z.002, etc.), we must add all archive volumes here (so the entire archive is deleted)
    CSalamanderDirectoryAbstract* dir = SalamanderGeneral->AllocSalamanderDirectory(FALSE);
    if (dir == NULL)
        return Error(IDS_INSUFFICIENT_MEMORY);

    BOOL ret = FALSE;

    C7zClient* client = new C7zClient();
    if (client)
    {
        CPluginDataInterface* pluginData = new CPluginDataInterface(client);
        if (pluginData)
        {
            // open the archive
            if (client->ListArchive(fileName, dir, pluginData, pluginData->Password))
            {
                CQuadWord totalSize(0, 0);
                int itemCount = 0;
                CalcSize(dir, mask, totalSize, itemCount);

                //
                BOOL delTempDir = TRUE;
                if (SalamanderGeneral->TestFreeSpace(SalamanderGeneral->GetMsgBoxParent(),
                                                     targetDir, totalSize, LangStr(IDS_UNPACKING_ARCHIVE).c_str()))
                {
                    salamander->OpenProgressDialog(LangStr(IDS_UNPACKING_ARCHIVE).c_str(), FALSE, NULL, FALSE);
                    salamander->ProgressDialogAddText(LangStr(IDS_READING_ARCHIVEITEMS).c_str(), FALSE);

                    const std::wstring modmask =
                        SPLPrepareMaskOwned(SalamanderGeneral, mask);

                    std::wstring archivePath;

                    TIndirectArray<CArchiveItemInfo> archiveItems(itemCount, 10, dtDelete);
                    if (GatherItems(dir, modmask.c_str(), &archiveItems, archivePath) != OPER_CANCEL)
                    {
                        C7zClient* client2 = ((CPluginDataInterface*)pluginData)->Get7zClient();

                        salamander->ProgressDialogAddText(LangStr(IDS_UNPACKING).c_str(), FALSE);
                        ret = client2->Decompress(salamander, fileName, targetDir, &archiveItems, pluginData->Password) != OPER_CANCEL;
                    }
                    salamander->CloseProgressDialog();
                }

                dir->Clear(pluginData);
                if (pluginData != NULL)
                    PluginInterface.ReleasePluginDataInterface(pluginData);

                int panel = -1;
                CanCloseArchive(salamander, fileName, TRUE, panel);
            }
            else
            {
                delete (CPluginDataInterface*)pluginData;
            }
        }
        else
        {
            ret = Error(IDS_INSUFFICIENT_MEMORY);
            delete client;
        }
    }
    else
    {
        ret = Error(IDS_INSUFFICIENT_MEMORY);
    }

    SalamanderGeneral->FreeSalamanderDirectory(dir);

    return ret;
}

BOOL CPluginInterfaceForArchiver::CanCloseArchive(CSalamanderForOperationsAbstract* salamander,
                                                  const wchar_t* fileName,
                                                  BOOL force, int panel)
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::CanCloseArchive(, %ls, %d, %d)", fileName, force, panel);

    return TRUE;
}

BOOL CPluginInterfaceForArchiver::PackToArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                const wchar_t* archiveRoot, BOOL move, const wchar_t* sourcePath,
                                                SalEnumSelection2 next, void* nextParam)
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForArchiver::PackToArchive(, %ls, %ls, %d, %ls, ,)", fileName,
                        archiveRoot, move, sourcePath);

    // test whether the archive exists (we need to distinguish between update and create new archive)
    BOOL isNewArchive = FALSE;
    HANDLE hArchive = ::CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hArchive == INVALID_HANDLE_VALUE)
    {
        isNewArchive = TRUE; // the file does not exist; this is a new archive

        // check whether the target path is writable
        hArchive = INVALID_HANDLE_VALUE;
        hArchive = ::CreateFileW(fileName, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hArchive == INVALID_HANDLE_VALUE)
            return SysError(IDS_CANT_CREATE_ARCHIVE, ::GetLastError());
        else
        {
            ::CloseHandle(hArchive);
            DeleteFileW(fileName);
        }
    }
    else
    {
        ::CloseHandle(hArchive); // the file exists; close it, we will update it

        // check for the read-only attribute
        DWORD attrs = SalamanderGeneral->SalGetFileAttributes(fileName);
        if (attrs != -1)
        {
            if (attrs & FILE_ATTRIBUTE_READONLY)
                return Error(IDS_READONLY_ARCHIVE);
        }
        // at this point the file has no READ-ONLY attribute, or retrieving the attributes failed and the next test should catch it

        // check whether the file is writable
        hArchive = INVALID_HANDLE_VALUE;
        hArchive = ::CreateFileW(fileName, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hArchive == INVALID_HANDLE_VALUE)
            return SysErrorWidePath(IDS_CANT_UPDATE_ARCHIVE, ::GetLastError(), FALSE, fileName);
        else
            ::CloseHandle(hArchive); // the file exists; close it, we will update it
    }

    CCompressParams compressParams;
    bool passwordDefined = false;
    char password[PASSWORD_LEN];
    if (Config.ShowExtendedOptions)
    {
        // show the extended options dialog box
        CExtOptionsDialog dlg(SalamanderGeneral->GetMsgBoxParent());
        if (isNewArchive)
            dlg.SetTitle(LangStr(IDS_CREATE_NEW_ARCHIVE).c_str());
        else
            dlg.SetTitle(LangStr(IDS_ADD_FILES_TO_ARCHIVE).c_str());
        dlg.SetArchiveName(fileName);
        dlg.CompressParams = Config.CompressParams;
        int res = (int)dlg.Execute();
        if (res == IDCANCEL)
            return FALSE;

        if (res == IDOK)
        {
            passwordDefined = dlg.IsPasswordDefined() == TRUE;
            strcpy(password, dlg.GetPassword());

            Config.ShowExtendedOptions = dlg.GetNotAgain() == FALSE;

            // take the config from the extended dialog
            compressParams = dlg.CompressParams;
        }
    }
    else
    {
        compressParams = Config.CompressParams;
    }

    // open progress dialog
    if (isNewArchive)
        salamander->OpenProgressDialog(LangStr(IDS_PACKING_ARCHIVE).c_str(), FALSE, NULL, FALSE);
    else
        salamander->OpenProgressDialog(LangStr(IDS_UPDATING_ARCHIVE).c_str(), FALSE, NULL, FALSE);
    salamander->ProgressDialogAddText(LangStr(IDS_READING_DIRTREE).c_str(), FALSE);

    BOOL isDir;
    const wchar_t* name;
    const wchar_t* dosName; // dummy
    CQuadWord size;
    DWORD attr;
    FILETIME lastWrite;
    int errorOccured;

    // count how many items we will compress
    // (we could use a growing array right away, but with a large number it would fragment memory, and iterating the list twice
    // will not kill us)
    int itemCount = 0;
    while ((name = next(SalamanderGeneral->GetMsgBoxParent(), 3, &dosName, &isDir, &size,
                        &attr, &lastWrite, nextParam, &errorOccured)) != NULL)
    {
        itemCount++;
    }
    // check whether an error occurred and the user did not request cancellation
    if (errorOccured == SALENUM_CANCEL)
    {
        salamander->CloseProgressDialog();
        return FALSE;
    }

    // prepare the list of items to compress
    TIndirectArray<CFileItem> fileList(itemCount, 20, dtDelete);
    next(NULL, -1, NULL, NULL, NULL, NULL, NULL, nextParam, NULL);
    while ((name = next(NULL /* we do not log errors the second time */, 3, &dosName, &isDir, &size,
                        &attr, &lastWrite, nextParam, &errorOccured)) != NULL)
    {
        if (errorOccured == SALENUM_ERROR)
            TRACE_I("Not all files and directories from disk will be packed.");

        // building the list of files that should be packed
        CFileItem* fi = new CFileItem(sourcePath, archiveRoot, name, attr, size.Value, lastWrite, isDir == TRUE);
        if (fi == NULL)
        {
            salamander->CloseProgressDialog();
            Error(IDS_INSUFFICIENT_MEMORY);
            return FALSE;
        }
        if (move)
            fi->CanDelete = TRUE;
        fileList.Add(fi);
    }
    if (errorOccured != SALENUM_SUCCESS)
        TRACE_I("Not all files and directories from disk will be packed.");

    // create the archive
    C7zClient client;
    if (isNewArchive)
        salamander->ProgressDialogAddText(LangStr(IDS_PACKING).c_str(), FALSE);
    else
        salamander->ProgressDialogAddText(LangStr(IDS_UPDATING).c_str(), FALSE);
    BOOL ret = client.Update(salamander, fileName, sourcePath, isNewArchive, &fileList, &compressParams, passwordDefined,
                             GetUnicodeString(password)) == OPER_OK;

    // delete files afterwards if we are moving them into the archive
    if (move && ret)
    { // first lock the archive file so we cannot delete it ourselves (bug: https://forum.altap.cz/viewtopic.php?f=3&t=3859)
        while (1)
        {
            hArchive = ::CreateFileW(fileName, GENERIC_READ /* I tried 0, but the system then allowed deleting the file */,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

            if (hArchive != INVALID_HANDLE_VALUE)
                break;
            DWORD err = ::GetLastError();
            const std::wstring errorText = SPLGetErrorTextOwned(SalamanderGeneral, err);
            if (SalamanderGeneral->DialogError(SalamanderGeneral->GetMsgBoxParent(),
                                               BUTTONS_RETRYCANCEL, fileName,
                                               errorText.c_str(),
                                               LangStr(IDS_ARCLOCK_ERROR).c_str()) != DIALOG_RETRY)
            {
                break;
            }
        }
        if (hArchive != INVALID_HANDLE_VALUE)
        {
            BOOL fileSilent = FALSE;
            BOOL dirSilent = FALSE;

            salamander->ProgressDialogAddText(LangStr(IDS_REMOVING_FILES).c_str(), FALSE);
            salamander->ProgressSetTotalSize(CQuadWord(itemCount, 0), CQuadWord(-1, -1));
            salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), FALSE);

            // delete the files first (those that have CanDelete)
            int i;
            for (i = 0; i < fileList.Count; i++)
            {
                CFileItem* fi = fileList[i];
                if (fi->CanDelete && !fi->IsDir)
                {
                    // delete the file
                    const wchar_t* name2 = fi->FullPath;

                    // drop the read-only attribute if needed
                    SalamanderGeneral->ClearReadOnlyAttr(name2);

                    if (!SafeDeleteFile(name2, fileSilent))
                    {
                        // failed to delete the file and the user pressed cancel
                        ret = FALSE;
                        break;
                    }

                    if (!salamander->ProgressAddSize(1, TRUE))
                    {
                        salamander->ProgressDialogAddText(LangStr(IDS_CANCELING_OPERATION).c_str(), FALSE);
                        salamander->ProgressEnableCancel(FALSE);
                        // the user canceled during deletion
                        ret = FALSE;
                        break;
                    }
                }
            }

            // files deleted (no cancel); continue with deletion, now empty directories are next
            if (ret)
            {
                // prepare the buffer for names
                std::wstring sourceDirectory = sourcePath;

                // delete directories; if something remains inside they will not be removed and that's fine :)
                // because we iterate from leaves to the root, we can delete them this way
                next(NULL, -1, NULL, NULL, NULL, NULL, NULL, nextParam, NULL);
                while ((name = next(NULL /* we do not log errors the second time */, 3, &dosName, &isDir, &size,
                                    &attr, &lastWrite, nextParam, NULL)) != NULL)
                {
                    if (isDir)
                    {
                        std::wstring sourceName = sourceDirectory;
                        SPLSalPathAppendOwned(sourceName, name);
                        // drop the read-only attribute if needed
                        SalamanderGeneral->ClearReadOnlyAttr(sourceName.c_str(), attr);
                        RemoveDirectoryW(sourceName.c_str());
                        if (!salamander->ProgressAddSize(1, TRUE))
                        {
                            salamander->ProgressDialogAddText(LangStr(IDS_CANCELING_OPERATION).c_str(), FALSE);
                            salamander->ProgressEnableCancel(FALSE);
                            // the user canceled during deletion
                            ret = FALSE;
                            break;
                        }
                    }
                }
            }
            ::CloseHandle(hArchive);
        }
    }

    salamander->CloseProgressDialog();

    return ret;
}

BOOL CPluginInterfaceForArchiver::DeleteFromArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                    CPluginDataInterfaceAbstract* pluginDataPar, const wchar_t* archiveRoot,
                                                    SalEnumSelection next, void* nextParam)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForArchiver::DeleteFromArchive(, %ls, , %ls, ,)",
                        fileName, archiveRoot);

    //  char password[PASSWORD_LEN];
    bool passwordDefined = false;

    // open progress dialog
    salamander->OpenProgressDialog(LangStr(IDS_UPDATING_ARCHIVE).c_str(), FALSE, NULL, FALSE);
    salamander->ProgressDialogAddText(LangStr(IDS_READING_ARCHIVEITEMS).c_str(), FALSE);

    BOOL isDir;
    CQuadWord size;
    CQuadWord totalSize(0, 0);
    const wchar_t* name;
    const CFileData* fileData;
    int errorOccured;
    int itemCount = 0;
    while ((name = next(SalamanderGeneral->GetMsgBoxParent(), 1, &isDir, &size, &fileData, nextParam, &errorOccured)) != NULL)
    {
        if (fileData->PluginData != 0)
            itemCount++;
    }
    // check whether an error occurred and the user did not request cancellation
    BOOL ret = TRUE;
    if (errorOccured == SALENUM_CANCEL)
        ret = FALSE;
    if (ret && itemCount > 0)
    {
        TIndirectArray<CArchiveItemInfo> archiveItems(itemCount, 20, dtDelete);
        next(NULL, -1, NULL, NULL, NULL, nextParam, NULL);
        while ((name = next(NULL /* we do not log errors the second time */, 1, &isDir, &size, &fileData, nextParam, NULL)) != NULL)
        {
            if (fileData->PluginData != 0)
            {
                CArchiveItemInfo* aii = new CArchiveItemInfo(name, fileData, isDir == TRUE);
                if (aii == NULL)
                    return Error(IDS_INSUFFICIENT_MEMORY);
                archiveItems.Add(aii);
            }
        }

        salamander->ProgressDialogAddText(LangStr(IDS_REMOVING).c_str(), FALSE);
        CPluginDataInterface* pluginData = (CPluginDataInterface*)pluginDataPar;
        C7zClient* client = pluginData->Get7zClient();
        ret = client->Delete(salamander, fileName, &archiveItems, passwordDefined, pluginData->Password) == OPER_OK;
    }
    salamander->CloseProgressDialog();
    return ret;
}

//
// ****************************************************************************
// Menu Handlers
//

static BOOL TestArchive(CSalamanderForOperationsAbstract* salamander, HWND hParent)
{
    // get the path to the focused 7z archive
    const CFileData* cfd = SalamanderGeneral->GetPanelFocusedItem(PANEL_SOURCE, NULL);
    if (cfd == NULL)
        return FALSE;
    C7zClient client;

    salamander->OpenProgressDialog(LangStr(IDS_TESTING_ARCHIVE).c_str(), FALSE, NULL, FALSE);
    const std::wstring progressText = SPLFormatStringOwned(LangStr(IDS_TESTING_ARCHIVE_NAME).c_str(), cfd->Name);
    salamander->ProgressDialogAddText(progressText.c_str(), FALSE);

    std::wstring fileName;
    if (!SPLGetPanelPathOwned(SalamanderGeneral, PANEL_SOURCE, fileName))
    {
        salamander->CloseProgressDialog();
        return FALSE;
    }
    SPLSalPathAppendOwned(fileName, cfd->Name);
    int ret = client.TestArchive(salamander, fileName.c_str());
    salamander->CloseProgressDialog();

    if (ret == OPER_OK)
    {
        const std::wstring text = SPLFormatStringOwned(LangStr(IDS_TESTARCHIVEOK).c_str(), fileName.c_str());
        SalamanderGeneral->SalMessageBox(hParent, text.c_str(), LangStr(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONINFORMATION);
        ret = TRUE;
    }
    else if (ret == OPER_CONTINUE)
    {
        const std::wstring text = SPLFormatStringOwned(LangStr(IDS_TESTARCHIVECORRUPTED).c_str(), fileName.c_str());
        SalamanderGeneral->SalMessageBox(hParent, text.c_str(), LangStr(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONINFORMATION);
        ret = FALSE;
    }

    return ret;
}

//
// ****************************************************************************
// CPluginInterfaceForMenuExt
//
BOOL CPluginInterfaceForMenuExt::ExecuteMenuItem(CSalamanderForOperationsAbstract* SalOp,
                                                 HWND parent, int id, DWORD eventMask)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForMenuExt::ExecuteMenuItem( , , %ld, %X)", id, eventMask);

    switch (id)
    {
    case IDM_TESTARCHIVE:
    {
        return TestArchive(SalOp, parent);
    }

    default:
    {
        TRACE_E("Wrong menu ID");
    }
    }
    return FALSE;
}

BOOL CPluginInterfaceForMenuExt::HelpForMenuItem(HWND parent, int id)
{
    int helpID = 0;
    switch (id)
    {
    case IDM_TESTARCHIVE:
        helpID = IDH_TESTARCHIVE;
        break;
    }
    if (helpID != 0)
        SalamanderGeneral->OpenHtmlHelp(parent, HHCDisplayContext, helpID, FALSE);
    return helpID != 0;
}

// ****************************************************************************
//
// CPluginDataInterface
//

// callback invoked by Salamander to obtain text
// see spl_com.h / FColumnGetText for a description
void WINAPI GetPackedSizeText()
{
    if (*TransferIsDir)
    {
        *TransferLen = 0;
    }
    else
    {
        C7zClient::CItemData* itemData = (C7zClient::CItemData*)(*TransferFileData)->PluginData;
        if (itemData->PackedSize != 0)
        {
            const std::wstring number = SPLNumberToStrOwned(
                SalamanderGeneral, CQuadWord().SetUI64(itemData->PackedSize));
            *TransferLen = static_cast<int>((std::min<size_t>)(number.size(), TRANSFER_BUFFER_MAX));
            wmemcpy(TransferBuffer, number.data(), static_cast<size_t>(*TransferLen));
        }
        else
        {
            *TransferLen = 0;
        }
    }
}

void WINAPI GetMethodText()
{
    if (*TransferIsDir)
    {
        *TransferLen = 0;
    }
    else
    {
        C7zClient::CItemData* itemData = (C7zClient::CItemData*)(*TransferFileData)->PluginData;
        unsigned len = itemData->Method.Len();
        if (len > TRANSFER_BUFFER_MAX)
            len = TRANSFER_BUFFER_MAX;
        wmemcpy(TransferBuffer, (const wchar_t*)itemData->Method, len);
        *TransferLen = (int)len;
    }
}

CPluginDataInterface::CPluginDataInterface(C7zClient* client)
{
    Client = client;
}

CPluginDataInterface::~CPluginDataInterface()
{
    Password = L"empty";
    delete Client;
    Client = NULL;
}

void CPluginDataInterface::ReleasePluginData(CFileData& file, BOOL isDir)
{
    delete (C7zClient::CItemData*)file.PluginData;
}

void WINAPI
CPluginDataInterface::SetupView(BOOL leftPanel, CSalamanderViewAbstract* view, const wchar_t* archivePath,
                                const CFileData* upperDir)
{
    view->GetTransferVariables(TransferFileData, TransferIsDir, TransferBuffer, TransferLen, TransferRowData,
                               TransferPluginDataIface, TransferActCustomData);

    // adjust columns only in detailed mode
    if (view->GetViewMode() == VIEW_MODE_DETAILED)
    {
        // try to find the standard Size column and insert ourselves after it; if it is not found,
        // append at the end
        int sizeIndex = view->GetColumnsCount();
        int i;
        for (i = 0; i < sizeIndex; i++)
            if (view->GetColumn(i)->ID == COLUMN_ID_SIZE)
            {
                sizeIndex = i + 1;
                break;
            }

        CColumn column;
        if (Config.ListInfoPackedSize)
        {
            // column for displaying the compressed size
            lstrcpynW(column.Name, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LISTINFO_PAKEDSIZE).c_str(), _countof(column.Name));
            lstrcpynW(column.Description, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LISTINFO_PAKEDSIZE_DESC).c_str(), _countof(column.Description));
            column.GetText = GetPackedSizeText;
            column.SupportSorting = 0;
            column.LeftAlignment = 0;
            column.ID = COLUMN_ID_CUSTOM;
            column.CustomData = 0;
            column.Width = leftPanel ? LOWORD(Config.ColumnPackedSizeWidth) : HIWORD(Config.ColumnPackedSizeWidth);
            column.FixedWidth = leftPanel ? LOWORD(Config.ColumnPackedSizeFixedWidth) : HIWORD(Config.ColumnPackedSizeFixedWidth);
            view->InsertColumn(sizeIndex, &column);
        }

        if (Config.ListInfoMethod)
        {
            // column for displaying the method
            sizeIndex = view->GetColumnsCount();
            lstrcpynW(column.Name, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LISTINFO_METHOD).c_str(), _countof(column.Name));
            lstrcpynW(column.Description, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LISTINFO_METHOD_DESC).c_str(), _countof(column.Description));
            column.GetText = GetMethodText;
            column.SupportSorting = 0;
            column.LeftAlignment = 1;
            column.ID = COLUMN_ID_CUSTOM;
            column.CustomData = 1;
            column.Width = leftPanel ? LOWORD(Config.ColumnMethodWidth) : HIWORD(Config.ColumnMethodWidth);
            column.FixedWidth = leftPanel ? LOWORD(Config.ColumnMethodFixedWidth) : HIWORD(Config.ColumnMethodFixedWidth);
            view->InsertColumn(sizeIndex, &column);
        }
    }
}

void CPluginDataInterface::ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth)
{
    if (column->CustomData == 1)
    {
        if (leftPanel)
            Config.ColumnMethodFixedWidth = MAKELONG(newFixedWidth, HIWORD(Config.ColumnMethodFixedWidth));
        else
            Config.ColumnMethodFixedWidth = MAKELONG(LOWORD(Config.ColumnMethodFixedWidth), newFixedWidth);
    }
    else
    {
        if (leftPanel)
            Config.ColumnPackedSizeFixedWidth = MAKELONG(newFixedWidth, HIWORD(Config.ColumnPackedSizeFixedWidth));
        else
            Config.ColumnPackedSizeFixedWidth = MAKELONG(LOWORD(Config.ColumnPackedSizeFixedWidth), newFixedWidth);
    }
    if (newFixedWidth)
        ColumnWidthWasChanged(leftPanel, column, column->Width);
}

void CPluginDataInterface::ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column, int newWidth)
{
    if (column->CustomData == 1)
    {
        if (leftPanel)
            Config.ColumnMethodWidth = MAKELONG(newWidth, HIWORD(Config.ColumnMethodWidth));
        else
            Config.ColumnMethodWidth = MAKELONG(LOWORD(Config.ColumnMethodWidth), newWidth);
    }
    else
    {
        if (leftPanel)
            Config.ColumnPackedSizeWidth = MAKELONG(newWidth, HIWORD(Config.ColumnPackedSizeWidth));
        else
            Config.ColumnPackedSizeWidth = MAKELONG(LOWORD(Config.ColumnPackedSizeWidth), newWidth);
    }
}
