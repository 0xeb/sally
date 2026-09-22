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

#define DEMOMENU_WIDEN_IMPL(value) L##value
#define DEMOMENU_WIDEN(value) DEMOMENU_WIDEN_IMPL(value)

// Plugin interface object whose methods are called by Salamander
CPluginInterface PluginInterface;
// Additional interfaces exposed by CPluginInterface
CPluginInterfaceForMenuExt InterfaceForMenuExt;

// Global data
const wchar_t* PluginNameEN = L"DemoMenu";    // Non-translated plugin name, used before loading the language module + for debugging
const wchar_t* PluginNameShort = L"DEMOMENU"; // Plugin name (short, without spaces)

HINSTANCE DLLInstance = NULL; // Handle to SPL - language-independent resources
HINSTANCE HLanguage = NULL;   // Handle to SLG - language-dependent resources

// Salamander general interface - available from Salamander launch until the plugin shuts down
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;

// Variable required by "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

// Variable required by "spl_com.h"
int SalamanderVersion = 0;

// Interface providing customized Windows controls used in Salamander
//CSalamanderGUIAbstract *SalamanderGUI = NULL;

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

void OnAbout(HWND hParent)
{
    const std::wstring text = SPLFormatStringOwned(
        L"%s %s\n\n%s\n\n%s", LoadStr(IDS_PLUGINNAME).c_str(),
        DEMOMENU_WIDEN(VERSINFO_VERSION), DEMOMENU_WIDEN(VERSINFO_COPYRIGHT),
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
    // Set SalamanderDebug for "dbg.h"
    SalamanderDebug = salamander->GetSalamanderDebug();
    // Set SalamanderVersion for "spl_com.h"
    SalamanderVersion = salamander->GetVersion();
    HANDLES_CAN_USE_TRACE();
    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

    // Verify Salamander is at the minimum supported version before continuing
    if (SalamanderVersion < LAST_VERSION_OF_SALAMANDER)
    { // Reject older versions
        MessageBoxW(salamander->GetParentWindow(),
                   DEMOMENU_WIDEN(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                   PluginNameEN, MB_OK | MB_ICONERROR);
        return NULL;
    }

    // Load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), PluginNameEN);
    if (HLanguage == NULL)
        return NULL;

    // Acquire Salamander's general interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    // Acquire the interface providing customized Windows controls used in Salamander
    //  SalamanderGUI = salamander->GetSalamanderGUI();

    // Register the name of the help file
    SalamanderGeneral->SetHelpFileName(L"demomenu.chm");

    // Provide the basic plugin metadata
    salamander->SetBasicPluginData(LoadStr(IDS_PLUGINNAME).c_str(), 0,
                                   DEMOMENU_WIDEN(VERSINFO_VERSION_NO_PLATFORM),
                                   DEMOMENU_WIDEN(VERSINFO_COPYRIGHT),
                                   LoadStr(IDS_PLUGIN_DESCRIPTION).c_str(), PluginNameShort,
                                   NULL, NULL);

    // Register the plugin home page URL
    salamander->SetPluginHomePageURL(LoadStr(IDS_PLUGIN_HOME).c_str());

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

void WINAPI
CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    // Register the basic menu item:
    salamander->AddMenuItem(-1, LoadStr(IDS_TESTCMD).c_str(), SALHOTKEY('M', HOTKEYF_CONTROL | HOTKEYF_SHIFT),
                            MENUCMD_TESTCMD, FALSE, MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);

    /*
  CGUIIconListAbstract *iconList = SalamanderGUI->CreateIconList();
  iconList->Create(16, 16, 1);
  HICON hIcon = (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_PLUGINICON), IMAGE_ICON, 16, 16, SalamanderGeneral->GetIconLRFlags());
  iconList->ReplaceIcon(0, hIcon);
  DestroyIcon(hIcon);
  salamander->SetIconListForGUI(iconList); // Salamander takes care of destroying the icon list

  salamander->SetPluginIcon(0);
  salamander->SetPluginMenuAndToolbarIcon(0);
*/
}

CPluginInterfaceForMenuExtAbstract* WINAPI
CPluginInterface::GetInterfaceForMenuExt()
{
    return &InterfaceForMenuExt;
}
