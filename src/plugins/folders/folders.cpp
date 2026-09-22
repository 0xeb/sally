// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "dialogs.h"
#include "folders.h"
#include "iltools.h"

#include "folders.rh"
#include "folders.rh2"
#include "lang\lang.rh"

#define FOLDERS_WIDEN_IMPL(value) L##value
#define FOLDERS_WIDEN(value) FOLDERS_WIDEN_IMPL(value)

// ****************************************************************************

HINSTANCE DLLInstance = NULL; // handle to the SPL - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to the SLG - language-dependent resources

// plugin interface object whose methods are called from Salamander
CPluginInterface PluginInterface;

// file system interface
CPluginInterfaceForFS InterfaceForFS;

// general Salamander interface - valid from plugin start until it is unloaded
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;

// interface providing customized Windows controls used in Salamander
CSalamanderGUIAbstract* SalamanderGUI = NULL;

// variable definition for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

// variable definition for "spl_com.h"
int SalamanderVersion = 0;

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

// Wide. SalamanderGeneral->LoadStr has returned WCHAR* since the v108
// ABI break; this went through LoadStrNarrow and was widened again at every call site.
std::wstring LangStr(int resID)
{
    return SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID);
}

int WINAPI SalamanderPluginGetReqVer()
{
    return LAST_VERSION_OF_SALAMANDER;
}

CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander)
{
    // set SalamanderDebug for "dbg.h"
    SalamanderDebug = salamander->GetSalamanderDebug();
    // set SalamanderVersion for "spl_com.h"
    SalamanderVersion = salamander->GetVersion();

    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

    // this plugin is built for the current Salamander version and newer - verify it
    if (SalamanderVersion < LAST_VERSION_OF_SALAMANDER)
    { // reject older versions
        // wide: same call-site-local widen shape used throughout this backlog
        // (205-230).
        MessageBoxW(salamander->GetParentWindow(),
                    FOLDERS_WIDEN(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"Folders" /* neprekladat! */, MB_OK | MB_ICONERROR);
        return NULL;
    }

    // load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"Folders" /* neprekladat! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain the general Salamander interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();

    // obtain the interface providing customized Windows controls used in Salamander
    SalamanderGUI = salamander->GetSalamanderGUI();

    if (!InitializeWinLib(L"Folders" /* neprekladat! */, DLLInstance))
        return NULL;
    SetWinLibStrings(L"Invalid number!", LangStr(IDS_PLUGINNAME).c_str());

    // set the basic plugin information
    salamander->SetBasicPluginData(LangStr(IDS_PLUGINNAME).c_str(),
                                   FUNCTION_FILESYSTEM,
                                   FOLDERS_WIDEN(VERSINFO_VERSION_NO_PLATFORM),
                                   FOLDERS_WIDEN(VERSINFO_COPYRIGHT),
                                   LangStr(IDS_PLUGIN_DESCRIPTION).c_str(),
                                   L"FOLDERS" /* neprekladat! */, NULL, L"fld");

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    // obtain our FS name (it may not be "fld", Salamander can adjust it)
    AssignedFSName = SPLGetPluginFSNameOwned(SalamanderGeneral, 0);

    if (!InitFS())
        return NULL; // error

    return &PluginInterface;
}

// ****************************************************************************
//
// CPluginInterface
//

void CPluginInterface::About(HWND parent)
{
    const std::wstring text = SPLFormatStringOwned(
        L"%ls %ls\n\n%ls\n\n%ls", LangStr(IDS_PLUGINNAME).c_str(),
        FOLDERS_WIDEN(VERSINFO_VERSION), FOLDERS_WIDEN(VERSINFO_COPYRIGHT),
        LangStr(IDS_PLUGIN_DESCRIPTION).c_str());
    SalamanderGeneral->SalMessageBox(parent, text.c_str(), LangStr(IDS_ABOUT).c_str(), MB_OK | MB_ICONINFORMATION);
}

BOOL CPluginInterface::Release(HWND parent, BOOL force)
{
    CALL_STACK_MESSAGE2("CPluginInterface::Release(, %d)", force);

    ReleaseWinLib(DLLInstance);

    ReleaseFS();

    return TRUE;
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    HBITMAP hBmp = (HBITMAP)LoadImage(DLLInstance, MAKEINTRESOURCE(IDB_FOLDERS),
                                      IMAGE_BITMAP, 16, 16, LR_DEFAULTCOLOR);
    salamander->SetBitmapWithIcons(hBmp);
    DeleteObject(hBmp);
    salamander->SetChangeDriveMenuItem(LangStr(IDS_DRIVEMENUTEXT).c_str(), 0);
    salamander->SetPluginIcon(0);
    salamander->SetPluginMenuAndToolbarIcon(0);
}

void CPluginInterface::ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData)
{
    delete ((CPluginDataInterface*)pluginData);
}

CPluginInterfaceForFSAbstract* WINAPI
CPluginInterface::GetInterfaceForFS()
{
    return &InterfaceForFS;
}
