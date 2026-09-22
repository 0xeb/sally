// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#define WMOBILE_WIDEN2(x) L##x
#define WMOBILE_WIDEN(x) WMOBILE_WIDEN2(x)

// plugin interface object; Salamander calls its methods
CPluginInterface PluginInterface;
// additional parts of the CPluginInterface interface
CPluginInterfaceForFS InterfaceForFS;

// ConfigVersion: 0 - no configuration was read from the Registry (plugin installation or a version without configuration - up to and including 2.5 beta 7),
//                1 - first configuration version (since 2.5 beta 8; introduced to automatically disable the Alt+F1/F2 menu item when rapi.dll is not installed)

int ConfigVersion = 0;           // version of the configuration loaded from the registry (see description above)
#define CURRENT_CONFIG_VERSION 1 // current configuration version (stored in the registry when the plugin unloads)
const wchar_t* CONFIG_VERSION = L"Version";

// global data

// pointers to lower/upper case mapping tables
unsigned char* LowerCase = NULL;
unsigned char* UpperCase = NULL;

HINSTANCE DLLInstance = NULL; // handle to SPL - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to SLG - language-dependent resources

// Salamander general interface - valid from startup until the plugin terminates
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;

// variable definition for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

// variable definition for "spl_com.h"
int SalamanderVersion = 0;

// interface providing customized Windows controls used in Salamander
CSalamanderGUIAbstract* SalamanderGUI = NULL;

CFileInfoArray::~CFileInfoArray()
{
    if (SalamanderGeneral != NULL)
    {
        for (int i = 0; i < Count; ++i)
            SalamanderGeneral->Free(At(i).cFileName);
    }
}

BOOL CFileInfoArray::AddOwned(const wchar_t* fileName, DWORD attributes, DWORD fileSize, int block)
{
    if (fileName == NULL || SalamanderGeneral == NULL)
        return FALSE;

    CFileInfo entry = {};
    entry.cFileName = SalamanderGeneral->DupStr(fileName);
    if (entry.cFileName == NULL)
        return FALSE;
    entry.dwFileAttributes = attributes;
    entry.size = fileSize;
    entry.block = block;

    Add(entry);
    if (State != etNone)
    {
        SalamanderGeneral->Free(entry.cFileName);
        return FALSE;
    }
    return TRUE;
}

static std::wstring TitleWMobileStorage;
static std::wstring TitleWMobileErrorStorage;
static std::wstring TitleWMobileQuestionStorage;
const wchar_t* TitleWMobile = L"Windows Mobile Plugin";
const wchar_t* TitleWMobileError = L"Windows Mobile Plugin Error";
const wchar_t* TitleWMobileQuestion = L"Windows Mobile Plugin Question";

// ****************************************************************************

// Wide - SalamanderGeneral->LoadStr has returned WCHAR* since the v108 ABI break.
std::wstring LangStr(int resID)
{
    return SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DLLInstance = hinstDLL;

        INITCOMMONCONTROLSEX initCtrls;
        initCtrls.dwSize = sizeof(INITCOMMONCONTROLSEX);
        initCtrls.dwICC = ICC_BAR_CLASSES;
        if (!InitCommonControlsEx(&initCtrls))
        {
            // wide: English-only diagnostic, no LangStr involved (language
            // module isn't loaded yet at DllMain time) - same shape as undelete.cpp/demoplug.cpp
            // (208, 217).
            MessageBoxW(NULL, L"InitCommonControlsEx failed!", L"Error", MB_OK | MB_ICONERROR);
            return FALSE; // DLL won't start
        }
    }

    return TRUE; // DLL can be loaded
}

void OnAbout(HWND hParent)
{
    const std::wstring text = SPLFormatStringOwned(
        L"%ls " WMOBILE_WIDEN(VERSINFO_VERSION) L"\n\n" WMOBILE_WIDEN(VERSINFO_COPYRIGHT) L"\n\n%ls",
        LangStr(IDS_PLUGINNAME).c_str(), LangStr(IDS_PLUGIN_DESCRIPTION).c_str());
    SalamanderGeneral->SalMessageBox(hParent, text.c_str(), LangStr(IDS_ABOUT).c_str(), MB_OK | MB_ICONINFORMATION);
}

//
// ****************************************************************************
// SalamanderPluginGetReqVer
//

#ifdef __BORLANDC__
extern "C"
{
    int WINAPI SalamanderPluginGetReqVer();
    CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander);
};
#endif // __BORLANDC__

int WINAPI SalamanderPluginGetReqVer()
{
    return LAST_VERSION_OF_SALAMANDER;
}

//
// ****************************************************************************
// SalamanderPluginEntry
//

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
    HANDLES_CAN_USE_TRACE();
    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

    // this plugin is built for the current Salamander version and newer - perform a check
    if (SalamanderVersion < LAST_VERSION_OF_SALAMANDER)
    { // reject older versions
        // wide: same call-site-local widen shape used throughout this backlog
        // (205-228).
        MessageBoxW(salamander->GetParentWindow(),
                    WMOBILE_WIDEN(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"Windows Mobile Plugin" /* do not translate! */, MB_OK | MB_ICONERROR);
        return NULL;
    }

    // load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"Windows Mobile Plugin" /* do not translate! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain Salamander's general interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalamanderGeneral->GetLowerAndUpperCase(&LowerCase, &UpperCase);

    TitleWMobileStorage = LangStr(IDS_WMPLUGINTITLE).c_str();
    TitleWMobileErrorStorage = LangStr(IDS_WMPLUGINTITLE_ERROR).c_str();
    TitleWMobileQuestionStorage = LangStr(IDS_WMPLUGINTITLE_QUESTION).c_str();
    TitleWMobile = TitleWMobileStorage.c_str();
    TitleWMobileError = TitleWMobileErrorStorage.c_str();
    TitleWMobileQuestion = TitleWMobileQuestionStorage.c_str();

    // obtain the interface that provides customized Windows controls used in Salamander
    SalamanderGUI = salamander->GetSalamanderGUI();

    // set the help file name
    SalamanderGeneral->SetHelpFileName(L"wmobile.chm");

    if (!InitializeWinLib(L"WMOBILE" /* do not translate! */, DLLInstance))
        return FALSE;
    SetupWinLibHelp(HTMLHelpCallback);

    if (!InitFS())
        return NULL; // error

    // configure the basic plugin information
    salamander->SetBasicPluginData(LangStr(IDS_PLUGINNAME).c_str(),
                                   FUNCTION_FILESYSTEM | FUNCTION_LOADSAVECONFIGURATION,
                                   WMOBILE_WIDEN(VERSINFO_VERSION_NO_PLATFORM),
                                   WMOBILE_WIDEN(VERSINFO_COPYRIGHT),
                                   LangStr(IDS_PLUGIN_DESCRIPTION).c_str(),
                                   L"WMOBILE" /* do not translate! */, NULL, L"CE");

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    // obtain our FS name (it may not be "cefs"; Salamander can adjust it)
    AssignedFSName = SPLGetPluginFSNameOwned(SalamanderGeneral, 0);

    return &PluginInterface;
}

//
// ****************************************************************************
// CPluginInterface
//

void WINAPI
CPluginInterface::About(HWND parent)
{
    OnAbout(parent);
}

BOOL WINAPI
CPluginInterface::Release(HWND parent, BOOL force)
{
    CALL_STACK_MESSAGE2("CPluginInterface::Release(, %d)", force);
    BOOL ret = TRUE;

    if (ret)
    {
        ReleaseFS();

        ReleaseWinLib(DLLInstance);

        // remove all copies of FS files from the disk cache (theoretically redundant, every FS should delete its own copies)
        std::wstring uniqueFileNameW = AssignedFSName + L":";
        // disk names are case-insensitive while the disk cache is case-sensitive; converting
        // to lowercase makes the disk cache behave case-insensitively as well
        SPLToLowerCaseOwned(SalamanderGeneral, uniqueFileNameW);
        SalamanderGeneral->RemoveFilesFromCache(uniqueFileNameW.c_str());
    }
    if (ret && InterfaceForFS.GetActiveFSCount() != 0)
    {
        TRACE_E("Some FS interfaces were not closed (count=" << InterfaceForFS.GetActiveFSCount() << ")");
    }
    return ret;
}

void WINAPI
CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::LoadConfiguration(, ,)");

    if (regKey != NULL) // load from the registry
    {
        registry->GetValue(regKey, CONFIG_VERSION, REG_DWORD, &ConfigVersion, sizeof(DWORD));
    }
}

void WINAPI
CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, ,)");

    DWORD v = CURRENT_CONFIG_VERSION;
    registry->SetValue(regKey, CONFIG_VERSION, REG_DWORD, &v, sizeof(DWORD));
}

void WINAPI
CPluginInterface::Configuration(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Configuration()");
}

void WINAPI
CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    // switch to icons with alpha-channel support
    CGUIIconListAbstract* iconList = SalamanderGUI->CreateIconList();
    iconList->Create(16, 16, 1);
    HICON hIcon = (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_FS), IMAGE_ICON, 16, 16, SalamanderGeneral->GetIconLRFlags());
    iconList->ReplaceIcon(0, hIcon);
    DestroyIcon(hIcon);
    salamander->SetIconListForGUI(iconList); // Salamander takes care of destroying the icon list
    salamander->SetChangeDriveMenuItem(L"\tMobile Device", 0);
    salamander->SetPluginIcon(0);
    salamander->SetPluginMenuAndToolbarIcon(0);

    if (ConfigVersion < 1) // do this only during plugin installation or upgrade from 2.5 beta 7 or older (to keep the user's settings)
    {
        // if rapi is not installed, hide the icon so it does not get in the way
        HINSTANCE hLib = LoadLibraryW(L"rapi.dll");
        if (hLib != NULL)
            FreeLibrary(hLib);
        else
            SalamanderGeneral->SetChangeDriveMenuItemVisibility(FALSE);
    }
}

void WINAPI
CPluginInterface::ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData)
{
    //JR the Windows Mobile plugin does not use a dedicated data interface
}

void WINAPI
CPluginInterface::ClearHistory(HWND parent)
{
}

CPluginInterfaceForFSAbstract* WINAPI
CPluginInterface::GetInterfaceForFS()
{
    return &InterfaceForFS;
}
