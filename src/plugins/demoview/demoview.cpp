// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Copyright (c) 2023 Open Salamander Authors
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#include "precomp.h"

#define DEMOVIEW_WIDEN_IMPL(value) L##value
#define DEMOVIEW_WIDEN(value) DEMOVIEW_WIDEN_IMPL(value)

// plugin interface object, its methods are called by Salamander
CPluginInterface PluginInterface;
// other parts of the CPluginInterface interface
CPluginInterfaceForViewer InterfaceForViewer;
CPluginInterfaceForMenuExt InterfaceForMenuExt;
CPluginInterfaceForThumbLoader InterfaceForThumbLoader;

// global data
const wchar_t* PluginNameEN = L"DemoView"; // untranslated plugin name, used before loading the language module and for debugging
const wchar_t* PluginNameShort = L"DEMOVIEW"; // plugin name (short, without spaces)

BOOL CfgSavePosition = FALSE;             // whether to store the window position / align with the main window
WINDOWPLACEMENT CfgWindowPlacement = {0}; // invalid if CfgSavePosition != TRUE

DWORD LastCfgPage = 0; // start page (sheet) in configuration dialog

const wchar_t* CONFIG_SAVEPOS = L"SavePosition";
const wchar_t* CONFIG_WNDPLACEMENT = L"WindowPlacement";

// ConfigVersion: 0 - no configuration was loaded from the Registry (plugin installation),
//                1 - first configuration version

int ConfigVersion = 0;           // configuration version loaded from the registry (see description above)
#define CURRENT_CONFIG_VERSION 1 // current configuration version (stored in the registry when unloading the plugin)
const wchar_t* CONFIG_VERSION = L"Version";

HINSTANCE DLLInstance = NULL; // handle to the SPL - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to the SLG - language-dependent resources

// general Salamander interface - valid from startup until the plugin is unloaded
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;

// variable definition for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

// variable definition for "spl_com.h"
int SalamanderVersion = 0;

// interface providing customized Windows controls used in Salamander
CSalamanderGUIAbstract* SalamanderGUI = NULL;

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
            MessageBoxW(NULL, L"InitCommonControlsEx failed!", L"Error", MB_OK | MB_ICONERROR);
            return FALSE; // DLL won't start
        }
    }

    return TRUE; // DLL can be loaded
}

// ****************************************************************************

// Wide. SalamanderGeneral->LoadStr has returned WCHAR* since the v108
// ABI break; this went through LoadStrNarrow and was widened again at every call site.
std::wstring LoadStr(int resID)
{
    return SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID);
}

void OnConfiguration(HWND hParent)
{
    static BOOL InConfiguration = FALSE;
    if (InConfiguration)
    {
        SalamanderGeneral->SalMessageBox(hParent, LoadStr(IDS_CFG_ALREADY_OPENED).c_str(), LoadStr(IDS_PLUGINNAME).c_str(),
                                         MB_ICONINFORMATION | MB_OK);
        return;
    }
    InConfiguration = TRUE;
    if (CConfigDialog(hParent).Execute() == IDOK)
    {
        ViewerWindowQueue.BroadcastMessage(WM_USER_VIEWERCFGCHNG, 0, 0);
    }
    InConfiguration = FALSE;
}

void OnAbout(HWND hParent)
{
    const std::wstring text = SPLFormatStringOwned(
        L"%s %s\n\n%s\n\n%s", LoadStr(IDS_PLUGINNAME).c_str(),
        DEMOVIEW_WIDEN(VERSINFO_VERSION), DEMOVIEW_WIDEN(VERSINFO_COPYRIGHT),
        LoadStr(IDS_PLUGIN_DESCRIPTION).c_str());
    SalamanderGeneral->SalMessageBox(hParent, text.c_str(), LoadStr(IDS_ABOUT).c_str(), MB_OK | MB_ICONINFORMATION);
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
        MessageBoxW(salamander->GetParentWindow(),
                   DEMOVIEW_WIDEN(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                   PluginNameEN, MB_OK | MB_ICONERROR);
        return NULL;
    }

    // load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), PluginNameEN);
    if (HLanguage == NULL)
        return NULL;

    // obtain the general Salamander interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    // obtain the interface providing customized Windows controls used in Salamander
    SalamanderGUI = salamander->GetSalamanderGUI();

    // set the help file name
    SalamanderGeneral->SetHelpFileName(L"demoview.chm");

    if (!InitViewer())
        return NULL; // error

    // set the basic plugin information
    salamander->SetBasicPluginData(LoadStr(IDS_PLUGINNAME).c_str(),
                                   FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION | FUNCTION_VIEWER,
                                   DEMOVIEW_WIDEN(VERSINFO_VERSION_NO_PLATFORM),
                                   DEMOVIEW_WIDEN(VERSINFO_COPYRIGHT), LoadStr(IDS_PLUGIN_DESCRIPTION).c_str(),
                                   PluginNameShort, NULL, NULL);

    // set the plugin home-page URL
    salamander->SetPluginHomePageURL(LoadStr(IDS_PLUGIN_HOME).c_str());

    // test SetPluginBugReportInfo
    SalamanderGeneral->SetPluginBugReportInfo(LoadStr(IDS_PLUGIN_BUGREP).c_str(), LoadStr(IDS_PLUGIN_EMAIL).c_str());

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
    BOOL ret = ViewerWindowQueue.Empty();
    if (!ret && (force || SalamanderGeneral->SalMessageBox(parent, LoadStr(IDS_VIEWER_OPENWNDS).c_str(),
                                                           LoadStr(IDS_PLUGINNAME).c_str(),
                                                           MB_YESNO | MB_ICONQUESTION) == IDYES))
    {
        ret = ViewerWindowQueue.CloseAllWindows(force) || force;
    }
    if (ret)
    {
        if (!ThreadQueue.KillAll(force) && !force)
            ret = FALSE;
        else
            ReleaseViewer();
    }
    return ret;
}

void WINAPI
CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::LoadConfiguration(, ,)");

    if (regKey != NULL) // load from the registry
    {
        if (!registry->GetValue(regKey, CONFIG_VERSION, REG_DWORD, &ConfigVersion, sizeof(DWORD)))
            ConfigVersion = CURRENT_CONFIG_VERSION; // probably some rascal... ;-)

        registry->GetValue(regKey, CONFIG_SAVEPOS, REG_DWORD, &CfgSavePosition, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_WNDPLACEMENT, REG_BINARY, &CfgWindowPlacement, sizeof(WINDOWPLACEMENT));
    }
}

void WINAPI
CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, ,)");

    DWORD v = CURRENT_CONFIG_VERSION;
    registry->SetValue(regKey, CONFIG_VERSION, REG_DWORD, &v, sizeof(DWORD));

    registry->SetValue(regKey, CONFIG_SAVEPOS, REG_DWORD, &CfgSavePosition, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_WNDPLACEMENT, REG_BINARY, &CfgWindowPlacement, sizeof(WINDOWPLACEMENT));
}

void WINAPI
CPluginInterface::Configuration(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Configuration()");
    OnConfiguration(parent);
}

void WINAPI
CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    // basic part:
    salamander->AddViewer(L"*.dmv", FALSE);

    salamander->AddMenuItem(-1, L"&View Bitmap from Clipboard", SALHOTKEY('T', HOTKEYF_CONTROL | HOTKEYF_SHIFT),
                            MENUCMD_VIEWBMPFROMCLIP, FALSE, MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);

    HBITMAP hBmp = (HBITMAP)HANDLES(LoadImage(DLLInstance, MAKEINTRESOURCE(IDB_PLUGINICO),
                                              IMAGE_BITMAP, 16, 16, LR_DEFAULTCOLOR));
    salamander->SetBitmapWithIcons(hBmp);
    HANDLES(DeleteObject(hBmp));
    salamander->SetPluginIcon(0);
    salamander->SetPluginMenuAndToolbarIcon(0);

    salamander->SetThumbnailLoader(L"*.bmv"); // provide thumbnails for .bmv files
}

void WINAPI
CPluginInterface::ClearHistory(HWND parent)
{
    ViewerWindowQueue.BroadcastMessage(WM_USER_CLEARHISTORY, 0, 0);
}

void CPluginInterface::Event(int event, DWORD param)
{
    switch (event)
    {
    case PLUGINEVENT_SETTINGCHANGE:
        ViewerWindowQueue.BroadcastMessage(WM_USER_SETTINGCHANGE, 0, 0);
        break;
    }
}

CPluginInterfaceForViewerAbstract* WINAPI
CPluginInterface::GetInterfaceForViewer()
{
    return &InterfaceForViewer;
}

CPluginInterfaceForMenuExtAbstract* WINAPI
CPluginInterface::GetInterfaceForMenuExt()
{
    return &InterfaceForMenuExt;
}

CPluginInterfaceForThumbLoaderAbstract* WINAPI
CPluginInterface::GetInterfaceForThumbLoader()
{
    return &InterfaceForThumbLoader;
}
