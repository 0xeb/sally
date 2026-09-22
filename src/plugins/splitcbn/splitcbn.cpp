// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <tchar.h>
#include "splitcbn.rh"
#include "splitcbn.rh2"
#include "lang\lang.rh"
#include "splitcbn.h"
#include "split.h"
#include "combine.h"
#include "dialogs.h"
#include "splitcbn_text.h"

// ****************************************************************************

HINSTANCE DLLInstance = NULL; // handle to SPL - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to SLG - language-dependent resources

// plugin interface instance invoked directly by Salamander
CPluginInterface PluginInterface;
// portion of CPluginInterface that drives the extensions menu
CPluginInterfaceForMenuExt InterfaceForMenuExt;
// general Salamander interface, valid from startup until the plugin shuts down
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;
// interface offering convenient file-handling helpers
CSalamanderSafeFileAbstract* SalamanderSafeFile = NULL;
// interface providing Salamander-specific custom Windows controls
CSalamanderGUIAbstract* SalamanderGUI = NULL;
// SalamanderDebug instance shared with "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

BOOL configIncludeFileExt;
BOOL configCreateBatchFile;
BOOL configSplitToOther;
BOOL configCombineToOther;
BOOL configSplitToSubdir;

static const wchar_t* KEY_INCLUDEFILEEXT = L"Include Original Extension";
static const wchar_t* KEY_CREATEBATCHFILE = L"Create Batch File";
static const wchar_t* KEY_SPLITTOOTHER = L"Split To Other Panel";
static const wchar_t* KEY_COMBINETOOTHER = L"Combine To Other Panel";
static const wchar_t* KEY_SPLITTOSUBDIR = L"Split To Subdirectory";

// ****************************************************************************

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DLLInstance = hinstDLL;
        InitCommonControls();
    }
    return TRUE; // DLL can be loaded
}

// Wide. SalamanderGeneral->LoadStr has returned WCHAR* since the v108
// ABI break; this went through LoadStrNarrow and was widened again at every call site.
std::wstring LangStr(int resID)
{
    return SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID);
}

//****************************************************************************

int WINAPI SalamanderPluginGetReqVer()
{
    return LAST_VERSION_OF_SALAMANDER;
}

// ****************************************************************************

CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander)
{
    // set SalamanderDebug for "dbg.h"
    SalamanderDebug = salamander->GetSalamanderDebug();

    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

    // this plugin is built for the current version of Salamander and newer - perform a check
    if (salamander->GetVersion() < LAST_VERSION_OF_SALAMANDER)
    { // reject older versions
        // wide: same call-site-local widen shape as checksum/unlha/undelete/zip
        // (205-209).
#define SPLITCBN_WIDEN2(x) L##x
#define SPLITCBN_WIDEN(x) SPLITCBN_WIDEN2(x)
        MessageBoxW(salamander->GetParentWindow(),
                    SPLITCBN_WIDEN(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"Split & Combine" /* do not translate! */, MB_OK | MB_ICONERROR);
#undef SPLITCBN_WIDEN
#undef SPLITCBN_WIDEN2
        return NULL;
    }

    // ask Salamander to load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"Split & Combine" /* do not translate! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain the general Salamander interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalamanderSafeFile = salamander->GetSalamanderSafeFile();
    // obtain the interface providing customized Windows controls used in Salamander
    SalamanderGUI = salamander->GetSalamanderGUI();

    // set the help file name
    SalamanderGeneral->SetHelpFileName(L"splitcbn.chm");

    // set the basic information about the plugin
    salamander->SetBasicPluginData(LangStr(IDS_PLUGINNAME).c_str(),
                                   FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION,
                                   _CRT_WIDE(VERSINFO_VERSION_NO_PLATFORM),
                                   _CRT_WIDE(VERSINFO_COPYRIGHT),
                                   LangStr(IDS_PLUGIN_DESCRIPTION).c_str(),
                                   L"SplitCombine");

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    return &PluginInterface;
}

// ****************************************************************************
//
//  CPluginInterface
//

void CPluginInterface::About(HWND parent)
{
    try
    {
        const std::wstring message = SPLFormatStringOwned(
            L"%s " _CRT_WIDE(VERSINFO_VERSION) L"\n\n" _CRT_WIDE(VERSINFO_COPYRIGHT) L"\n\n%s",
            LangStr(IDS_PLUGINNAME).c_str(), LangStr(IDS_PLUGIN_DESCRIPTION).c_str());
        SalamanderGeneral->SalMessageBox(parent, message.c_str(),
                                          LangStr(IDS_ABOUTTITLE).c_str(),
                                          MB_OK | MB_ICONINFORMATION);
    }
    catch (...)
    {
        SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_PLUGINNAME).c_str(),
                                          LangStr(IDS_ABOUTTITLE).c_str(),
                                          MB_OK | MB_ICONINFORMATION);
    }
}

void CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::LoadConfiguration(, ,)");
    configIncludeFileExt = TRUE;
    configCreateBatchFile = TRUE;
    SalamanderGeneral->GetConfigParameter(SALCFG_ARCOTHERPANELFORUNPACK, &configSplitToOther, sizeof(BOOL), NULL);
    SalamanderGeneral->GetConfigParameter(SALCFG_ARCOTHERPANELFORPACK, &configCombineToOther, sizeof(BOOL), NULL);
    SalamanderGeneral->GetConfigParameter(SALCFG_ARCSUBDIRBYARCFORUNPACK, &configSplitToSubdir, sizeof(BOOL), NULL);
    if (regKey != NULL)
    {
        registry->GetValue(regKey, KEY_INCLUDEFILEEXT, REG_DWORD, &configIncludeFileExt, sizeof(DWORD));
        registry->GetValue(regKey, KEY_CREATEBATCHFILE, REG_DWORD, &configCreateBatchFile, sizeof(DWORD));
        registry->GetValue(regKey, KEY_SPLITTOOTHER, REG_DWORD, &configSplitToOther, sizeof(DWORD));
        registry->GetValue(regKey, KEY_COMBINETOOTHER, REG_DWORD, &configCombineToOther, sizeof(DWORD));
        registry->GetValue(regKey, KEY_SPLITTOSUBDIR, REG_DWORD, &configSplitToSubdir, sizeof(DWORD));
    }
    //if (!configSplitToOther) configSplitToSubdir = FALSE;
}

void CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, ,)");
    registry->SetValue(regKey, KEY_INCLUDEFILEEXT, REG_DWORD, &configIncludeFileExt, sizeof(DWORD));
    registry->SetValue(regKey, KEY_CREATEBATCHFILE, REG_DWORD, &configCreateBatchFile, sizeof(DWORD));
    registry->SetValue(regKey, KEY_SPLITTOOTHER, REG_DWORD, &configSplitToOther, sizeof(DWORD));
    registry->SetValue(regKey, KEY_COMBINETOOTHER, REG_DWORD, &configCombineToOther, sizeof(DWORD));
    registry->SetValue(regKey, KEY_SPLITTOSUBDIR, REG_DWORD, &configSplitToSubdir, sizeof(DWORD));
}

void CPluginInterface::Configuration(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Configuration( )");
    ConfigDialog(parent);
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    /* used by the script export_mnu.py, which generates salmenu.mnu for Translator
   keep synchronized with the salamander->AddMenuItem() calls below...
MENU_TEMPLATE_ITEM PluginMenu[] = 
{
	{MNTT_PB, 0
	{MNTT_IT, IDS_MENU1
	{MNTT_IT, IDS_MENU2
	{MNTT_PE, 0
};
*/

    salamander->AddMenuItem(-1, LangStr(IDS_MENU1).c_str(), 0, 1, FALSE, MENU_EVENT_TRUE,
                            MENU_EVENT_FILE_FOCUSED | MENU_EVENT_DISK, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LangStr(IDS_MENU2).c_str(), 0, 2, FALSE, MENU_EVENT_FILES_SELECTED | MENU_EVENT_FILE_FOCUSED, MENU_EVENT_DISK, MENU_SKILLLEVEL_ALL);

    // set the plugin icon
    HBITMAP hBmp = (HBITMAP)LoadImage(DLLInstance, MAKEINTRESOURCE(IDB_SPLIT),
                                      IMAGE_BITMAP, 16, 16, LR_DEFAULTCOLOR);
    salamander->SetBitmapWithIcons(hBmp);
    DeleteObject(hBmp);
    salamander->SetPluginIcon(0);
    salamander->SetPluginMenuAndToolbarIcon(0);
}

CPluginInterfaceForMenuExtAbstract*
CPluginInterface::GetInterfaceForMenuExt()
{
    return &InterfaceForMenuExt;
}

// ****************************************************************************
//
//  CPluginInterfaceForMenuExt
//

BOOL CPluginInterfaceForMenuExt::ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander,
                                                 HWND parent, int id, DWORD eventMask)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForMenuExt::ExecuteMenuItem( , , %ld, %X)", id, eventMask);

    SalamanderGeneral->SetUserWorkedOnPanelPath(PANEL_SOURCE); // treat all commands as working with the path (shown in Alt+F12)

    switch (id)
    {
    case 1:
    {
        return SplitCommand(parent, salamander);
    }

    case 2:
    {
        return CombineCommand(eventMask, parent, salamander);
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
        helpID = IDH_SPLIT;
        break;
    case 2:
        helpID = IDH_COMBINE;
        break;
    }
    if (helpID != 0)
        SalamanderGeneral->OpenHtmlHelp(parent, HHCDisplayContext, helpID, FALSE);
    return helpID != 0;
}

// ****************************************************************************
//
//  Helper functions
//

void CenterWindow(HWND hWnd)
{
    CALL_STACK_MESSAGE1("CenterWindow()");
    HWND hParent = GetParent(hWnd);
    if (hParent != NULL)
        SalamanderGeneral->MultiMonCenterWindow(hWnd, hParent, TRUE);
}

std::wstring GetInfo(CQuadWord& size)
{
    CALL_STACK_MESSAGE2("GetInfo(%I64u)", size.Value);
    SYSTEMTIME st;
    GetLocalTime(&st);

    std::wstring date;
    std::wstring time;
    if (!FormatSplitLocalDateTime(st, date, time))
    {
        date = SPLFormatStringOwned(L"%u.%u.%u", st.wDay, st.wMonth, st.wYear);
        time = SPLFormatStringOwned(L"%u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    }
    const std::wstring number = SPLNumberToStrOwned(SalamanderGeneral, size);
    return SPLFormatStringOwned(L"%s, %s, %s", number.c_str(), date.c_str(), time.c_str());
}

void StripExtension(wchar_t* fileName)
{
    CALL_STACK_MESSAGE2("StripExtension(%ls)", fileName);
    wchar_t* dot = wcsrchr(fileName, L'.');
    if (dot != NULL)
        *dot = 0; // ".cvspass" is treated as an extension in Windows
}

void StripExtension(std::wstring& fileName)
{
    const size_t dot = fileName.find_last_of(L'.');
    if (dot != std::wstring::npos)
        fileName.resize(dot); // ".cvspass" is treated as an extension in Windows
}

BOOL Error(int title, int error, ...)
{
    int lastErr = GetLastError();
    CALL_STACK_MESSAGE3("Error(%d, %d, ...)", title, error);
    va_list arglist;
    va_start(arglist, error);
    std::wstring message;
    try
    {
        message = SPLFormatStringOwnedV(LangStr(error).c_str(), arglist);
    }
    catch (...)
    {
        message = L"Error";
        lastErr = ERROR_SUCCESS;
    }
    va_end(arglist);
    try
    {
        if (lastErr != ERROR_SUCCESS)
        {
            message += L" ";
            message += SPLGetErrorTextOwned(SalamanderGeneral, lastErr);
        }
    }
    catch (...)
    {
    }
    SalamanderGeneral->ShowMessageBox(message.c_str(), LangStr(title).c_str(), MSGBOX_ERROR);

    return FALSE;
}

BOOL Error2(HWND hParent, int title, int error, ...)
{
    int lastErr = GetLastError();
    CALL_STACK_MESSAGE3("Error2( , %d, %d, ...)", title, error);
    va_list arglist;
    va_start(arglist, error);
    std::wstring message;
    try
    {
        message = SPLFormatStringOwnedV(LangStr(error).c_str(), arglist);
    }
    catch (...)
    {
        message = L"Error";
        lastErr = ERROR_SUCCESS;
    }
    va_end(arglist);
    try
    {
        if (lastErr != ERROR_SUCCESS)
        {
            message += L" ";
            message += SPLGetErrorTextOwned(SalamanderGeneral, lastErr);
        }
    }
    catch (...)
    {
    }
    SalamanderGeneral->SalMessageBox(hParent, message.c_str(), LangStr(title).c_str(),
                                      MSGBOXEX_OK | MSGBOXEX_ICONEXCLAMATION);

    return FALSE;
}

BOOL GetTargetDir(std::wstring& targetDir, const wchar_t* subdirName, BOOL bSplit)
{
    // This function returns the target directory for split or combine, respecting the configuration
    // configSplitToOther/configCombineToOther. If the target path would lead into an archive
    // or to a file system plugin, regardless of the configuration the source panel path is offered,
    // which is always guaranteed to be PATH_TYPE_WINDOWS (thanks to the menu enablers).

    int type;
    if (!SPLGetPanelPathOwned(
            SalamanderGeneral,
            (bSplit ? configSplitToOther : configCombineToOther) ? PANEL_TARGET : PANEL_SOURCE,
            targetDir, &type))
        return FALSE;

    if (type != PATH_TYPE_WINDOWS)
        if (!SPLGetPanelPathOwned(SalamanderGeneral, PANEL_SOURCE, targetDir))
            return FALSE;

    if (bSplit && configSplitToSubdir && subdirName != NULL)
    {
        // StripExtension is the bare-FILENAME helper and scans the whole string, so a dot in a
        // parent directory ("C:\Builds\v1.2\bigfile") would cut the path there. SalPathRemoveExtension
        // stops at the backslash, which is what pre-unicode used here.
        SPLSalPathAppendOwned(targetDir, subdirName);
        SPLSalPathRemoveExtensionOwned(SalamanderGeneral, targetDir);
    }
    return TRUE;
}

BOOL MakePathAbsolute(std::wstring& path, BOOL pathIsDir,
                      const std::wstring& absRoot, BOOL activePreferred,
                      int errorTitle)
{
    int type;
    size_t secondPartOffset;
    BOOL isDir;

    SalamanderGeneral->SalUpdateDefaultDir(!configCombineToOther);
    if (!SPLSalParsePathOwned(SalamanderGeneral,
                              SalamanderGeneral->GetMsgBoxParent(), path,
                              type, isDir, secondPartOffset,
                              LangStr(IDS_PATHERROR).c_str(), TRUE,
                              absRoot.c_str()))
        return FALSE;

    if (type != PATH_TYPE_WINDOWS) // only Windows paths are supported
        return Error(errorTitle, IDS_WINPATH);

    if (isDir)
    {
        const wchar_t* s = path.c_str() + secondPartOffset;
        if (!pathIsDir)
            while (*s != 0 && *s != L'\\')
                s++;
        if (*s != 0) // contains subdirectories, ask whether to create them
            if (SalamanderGeneral->SalMessageBox(SalamanderGeneral->GetMsgBoxParent(),
                                                 LangStr(IDS_TARGETPATHEXIST).c_str(), LangStr(errorTitle).c_str(), MB_YESNO | MB_ICONQUESTION) == IDNO)
                return FALSE;
    }

    return TRUE;
}
