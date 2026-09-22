// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "../../registry_names.h"

// enables dumping of blocks that remain allocated on the heap after the plugin finishes
// #define DUMP_MEM

// ****************************************************************************

HINSTANCE DLLInstance = NULL; // handle to the SPL - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to the SLG - language-dependent resources

// plugin interface object; its methods are called by Salamander
CPluginInterface PluginInterface;
CPluginInterfaceForMenuExt InterfaceForMenuExt;

// Salamander general interface - valid from startup until the plugin terminates
CSalamanderGeneralAbstract* SG = NULL;

// variable definition for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

// variable definition for "spl_com.h"
int SalamanderVersion = 0;

CSalamanderGUIAbstract* SalGUI = NULL;

const wchar_t* CONFIG_WIDTH = L"Window Width";
const wchar_t* CONFIG_HEIGHT = L"Window Height";
const wchar_t* CONFIG_MAXIMIZED = L"Maximized";
const wchar_t* CONFIG_CUSTOMFONT = L"Custom Font";
const wchar_t* CONFIG_MANUALFONT = L"Manual Mode Font";
const char* CONFIG_MASKHISTORY = "Mask History %d";
const char* CONFIG_NEWNAMEHISTORY = "New Name History %d";
const char* CONFIG_SEARCHHISTORY = "Search History %d";
const char* CONFIG_REPLACEHISTORY = "Replace History %d";
const char* CONFIG_COMMANDHISTORY = "Command History %d";
const wchar_t* CONFIG_COMMAND = L"Command";
const wchar_t* CONFIG_ARGUMENTS = L"Arguments";
const wchar_t* CONFIG_INITDIR = L"InitDir";
const wchar_t* CONFIG_LASTUSED = L"Last Used Options";
const wchar_t* CONFIG_LASTMASK = L"Last Last Mask";
const wchar_t* CONFIG_LASTSUBDIRS = L"Last Subdirs";
const wchar_t* CONFIG_LASTREMOVESOURCE = L"Last Remove Source Path";
const wchar_t* CONFIG_CONFIRMESCCLOSE = L"Confirm ESC Close";

CThreadQueue ThreadQueue("Renamer Dialogs");
CWindowQueueEx WindowQueue;
BOOL AlwaysOnTop = FALSE;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    CALL_STACK_MESSAGE_NONE
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

// Wide - SG->LoadStr has returned WCHAR* since the v108 ABI break.
std::wstring LangStr(int resID)
{
    return SPLLoadStrOwned(SG, HLanguage, resID);
}

BOOL ErrorHelper(HWND parent, const wchar_t* message, int lastError, va_list arglist)
{
    CALL_STACK_MESSAGE3("ErrorHelper(, %ls, %d, )", message, lastError);
    wchar_t buf[1024]; //temp variable
    *buf = 0;
    vswprintf_s(buf, _countof(buf), message, arglist);
    if (lastError != ERROR_SUCCESS)
    {
        int l = lstrlenW(buf);
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, lastError,
                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf + l, 1024 - l, NULL);
    }
    if (SG)
    {
        if (parent == (HWND)-1)
            parent = SG->GetMsgBoxParent();
        SG->SalMessageBox(parent, buf, LangStr(IDS_RENAMERERR).c_str(), MB_ICONERROR);
    }
    else
    {
        if (parent == (HWND)-1)
            parent = 0;
        MessageBoxW(parent, buf, L"Renamer - Error", MB_OK | MB_ICONERROR);
    }
    return FALSE;
}

BOOL Error(HWND parent, int error, ...)
{
    CALL_STACK_MESSAGE_NONE
    int lastError = GetLastError();
    CALL_STACK_MESSAGE2("Error(, %d, )", error);
    va_list arglist;
    va_start(arglist, error);
    BOOL ret = ErrorHelper(parent, LangStr(error).c_str(), lastError, arglist);
    va_end(arglist);
    return ret;
}

BOOL Error(HWND parent, const wchar_t* error, ...)
{
    CALL_STACK_MESSAGE_NONE
    int lastError = GetLastError();
    CALL_STACK_MESSAGE2("Error(, %ls, )", error);
    va_list arglist;
    va_start(arglist, error);
    BOOL ret = ErrorHelper(parent, error, lastError, arglist);
    va_end(arglist);
    return ret;
}

BOOL Error(int error, ...)
{
    CALL_STACK_MESSAGE_NONE
    int lastError = GetLastError();
    CALL_STACK_MESSAGE2("Error(%d, )", error);
    va_list arglist;
    va_start(arglist, error);
    BOOL ret = ErrorHelper(DialogStackPeek(), LangStr(error).c_str(), lastError, arglist);
    va_end(arglist);
    return ret;
}

// BOOL
// ErrorL(int lastError, HWND parent, int error, ...)
// {
//   CALL_STACK_MESSAGE3("ErrorL(%d, , %d, )", lastError, error);
//   va_list arglist;
//   va_start(arglist, error);
//   BOOL ret = ErrorHelper(parent, LangStr(error), lastError, arglist);
//   va_end(arglist);
//   return ret;
// }

BOOL ErrorL(int lastError, int error, ...)
{
    CALL_STACK_MESSAGE3("ErrorL(%d, %d, )", lastError, error);
    va_list arglist;
    va_start(arglist, error);
    BOOL ret = ErrorHelper(DialogStackPeek(), LangStr(error).c_str(), lastError, arglist);
    va_end(arglist);
    return ret;
}

int SalPrintf(char* buffer, unsigned count, const char* format, ...)
{
    CALL_STACK_MESSAGE3("SalPrintf(, 0x%X, %s, )", count, format);
    va_list arglist;
    va_start(arglist, format);
    int ret = _vsnprintf_s(buffer, count, _TRUNCATE, format, arglist);
    va_end(arglist);
    return ret;
}

#ifdef DUMP_MEM
_CrtMemState ___CrtMemState;
#endif //DUMP_MEM

int WINAPI SalamanderPluginGetReqVer()
{
    CALL_STACK_MESSAGE_NONE
    return LAST_VERSION_OF_SALAMANDER;
}

CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander)
{
    CALL_STACK_MESSAGE_NONE
    // set SalamanderDebug for "dbg.h"
    SalamanderDebug = salamander->GetSalamanderDebug();
    // set SalamanderVersion for "spl_com.h"
    SalamanderVersion = salamander->GetVersion();

    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

#ifdef DUMP_MEM
    _CrtMemCheckpoint(&___CrtMemState);
#endif //DUMP_MEM

    // this plugin is built for the current Salamander version and newer - perform a check
    if (SalamanderVersion < LAST_VERSION_OF_SALAMANDER)
    { // cannot call Error here because it uses SG->SalMessageBox (SG is not initialized yet and the interface is incompatible)
        // wide: REQUIRE_LAST_VERSION_OF_SALAMANDER is a shared narrow SDK macro
        // (spl_vers.h) used by ~35 plugins - widen only at this call site via the same
        // two-macro token-paste idiom already used for __WFILE__ in common/trace.h, rather
        // than touching the shared macro itself.
#define RENAMER_WIDEN2(x) L##x
#define RENAMER_WIDEN(x) RENAMER_WIDEN2(x)
        MessageBoxW(salamander->GetParentWindow(),
                    RENAMER_WIDEN(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"Renamer" /* do not translate! */, MB_OK | MB_ICONERROR);
#undef RENAMER_WIDEN
#undef RENAMER_WIDEN2
        return NULL;
    }

    // load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"Renamer" /* do not translate! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain the Salamander general interface
    SG = salamander->GetSalamanderGeneral();
    SalGUI = salamander->GetSalamanderGUI();

    // set the help file name
    SG->SetHelpFileName(L"renamer.chm");

    if (!InitDialogs())
        return NULL;

    // set the basic plugin information
    salamander->SetBasicPluginData(LangStr(IDS_PLUGINNAME).c_str(),
                                   FUNCTION_LOADSAVECONFIGURATION | FUNCTION_CONFIGURATION,
                                   RenamerTextToWide(VERSINFO_VERSION_NO_PLATFORM).c_str(),
                                   RenamerTextToWide(VERSINFO_COPYRIGHT).c_str(),
                                   LangStr(IDS_DESCRIPTION).c_str(),
                                   L"Renamer" /* do not translate! */);

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    return &PluginInterface;
}

// ****************************************************************************
//
// CPluginInterface
//

void CPluginInterface::About(HWND parent)
{
    const std::wstring version = RenamerTextToWide(VERSINFO_VERSION);
    const std::wstring copyright = RenamerTextToWide(VERSINFO_COPYRIGHT);
    wchar_t buf[1000];
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%ls %ls\n\n%ls\n\n%ls",
                 LangStr(IDS_PLUGINNAME).c_str(), version.c_str(), copyright.c_str(),
                 LangStr(IDS_DESCRIPTION).c_str());
    SG->SalMessageBox(parent, buf, LangStr(IDS_ABOUT).c_str(), MB_OK | MB_ICONINFORMATION);
}

BOOL CPluginInterface::Release(HWND parent, BOOL force)
{
    CALL_STACK_MESSAGE2("CPluginInterface::Release(, %d)", force);

    BOOL ret = WindowQueue.Empty();
    if (!ret)
    {
        if (force)
            UpdateWindow(SG->GetMainWindowHWND());
        if (force)
            SG->CreateSafeWaitWindow(LangStr(IDS_WAITINGFORWINDOWS).c_str(), NULL, 0, FALSE, NULL);
        ret = WindowQueue.CloseAllWindows(force, 1000, INFINITE) || force;
        if (force)
            SG->DestroySafeWaitWindow();
    }
    if (ret)
    {
        if (!ThreadQueue.KillAll(force) && !force)
            ret = FALSE;
        else
        {
            ReleaseDialogs();

#ifdef DUMP_MEM
            _CrtMemDumpAllObjectsSince(&___CrtMemState);
#endif //DUMP_MEM
        }
    }
    return ret;
}

void CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::LoadConfiguration(, , )");
    // set default values
    DialogWidth = DialogHeight = -1;
    LastOptions.Reset(FALSE);
    strcpy(LastMask, "*.*");
    LastSubdirs = FALSE;
    LastRemoveSourcePath = FALSE;
    UseCustomFont = FALSE;
    ConfirmESCClose = TRUE;
    Command = L"notepad";
    Arguments = L"\"$(Name)\"";
    InitDir = L"$(FullPath)";
    if (regKey)
    {
        // history
        LoadHistory(regKey, CONFIG_MASKHISTORY, MaskHistory, registry);
        LoadHistory(regKey, CONFIG_NEWNAMEHISTORY, NewNameHistory, registry);
        LoadHistory(regKey, CONFIG_SEARCHHISTORY, SearchHistory, registry);
        LoadHistory(regKey, CONFIG_REPLACEHISTORY, ReplaceHistory, registry);
        LoadHistory(regKey, CONFIG_COMMANDHISTORY, CommandHistory, registry);

        // load from the registry
        registry->GetValue(regKey, CONFIG_CUSTOMFONT, REG_DWORD, &UseCustomFont, sizeof(int));
        UseCustomFont = UseCustomFont &&
                        registry->GetValue(regKey, CONFIG_MANUALFONT, REG_BINARY, &ManualModeLogFont, sizeof(LOGFONT));

        // window position
        if (!registry->GetValue(regKey, CONFIG_WIDTH, REG_DWORD, &DialogWidth, sizeof(int)) ||
            !registry->GetValue(regKey, CONFIG_HEIGHT, REG_DWORD, &DialogHeight, sizeof(int)))
        {
            DialogWidth = DialogHeight = -1;
        }
        registry->GetValue(regKey, CONFIG_MAXIMIZED, REG_DWORD, &Maximized, sizeof(BOOL));

        // last options settings
        HKEY subKey;
        if (registry->OpenKey(regKey, CONFIG_LASTUSED, subKey))
        {
            LastOptions.Load(subKey, registry);
            // LastMask stays narrow (feeds the still-narrow Mask dialog
            // control), but the shared registry facade's REG_SZ path is wide-only -
            // bridge here, same pattern as regedt's Command/Arguments/InitDir fix.
            GetValueSZ(registry, subKey, CONFIG_LASTMASK, LastMask, MAX_GROUPMASK);
            registry->GetValue(subKey, CONFIG_LASTSUBDIRS, REG_DWORD, &LastSubdirs, sizeof(BOOL));
            registry->GetValue(subKey, CONFIG_LASTREMOVESOURCE, REG_DWORD,
                               &LastRemoveSourcePath, sizeof(BOOL));
            registry->CloseKey(subKey);
        }

        // Counter dialog settings
        if (registry->OpenKey(regKey, SAL_REG_SUBKEY_COUNTER_W, subKey))
        {
            registry->GetValue(subKey, SAL_REG_VALUE_START_W, REG_DWORD, &LastCounterStart, sizeof(int));
            registry->GetValue(subKey, SAL_REG_VALUE_STEP_W, REG_BINARY, &LastCounterStep, sizeof(double));
            registry->GetValue(subKey, SAL_REG_VALUE_BASE_W, REG_DWORD, &LastCounterBase, sizeof(int));
            registry->GetValue(subKey, SAL_REG_VALUE_MIN_WIDTH_W, REG_DWORD, &LastCounterMinWidth, sizeof(int));
            registry->GetValue(subKey, SAL_REG_VALUE_FILL_W, REG_DWORD, &LastCounterFill, sizeof(int));
            registry->GetValue(subKey, SAL_REG_VALUE_LEFT_W, REG_DWORD, &LastCounterLeft, sizeof(BOOL));
            registry->CloseKey(subKey);
        }

        SPLRegistryGetStringOwned(registry, regKey, CONFIG_COMMAND, Command);
        SPLRegistryGetStringOwned(registry, regKey, CONFIG_ARGUMENTS, Arguments);
        SPLRegistryGetStringOwned(registry, regKey, CONFIG_INITDIR, InitDir);

        registry->GetValue(regKey, CONFIG_CONFIRMESCCLOSE, REG_DWORD, &ConfirmESCClose, sizeof(BOOL));
    }
}

void CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, , )");

    BOOL b;

    if (SG->GetConfigParameter(SALCFG_SAVEHISTORY, &b, sizeof(BOOL), NULL) && b)
    {
        SaveHistory(regKey, CONFIG_MASKHISTORY, MaskHistory, registry);
        SaveHistory(regKey, CONFIG_NEWNAMEHISTORY, NewNameHistory, registry);
        SaveHistory(regKey, CONFIG_SEARCHHISTORY, SearchHistory, registry);
        SaveHistory(regKey, CONFIG_REPLACEHISTORY, ReplaceHistory, registry);
        SaveHistory(regKey, CONFIG_COMMANDHISTORY, CommandHistory, registry);
    }
    else
    {
        // trim the histories
        char buf[32];
        int i;
        for (i = 0; i < MAX_HISTORY_ENTRIES; i++)
        {
            SalPrintf(buf, 32, CONFIG_MASKHISTORY, i);
            registry->DeleteValue(regKey, RenamerTextToWide(buf).c_str());
            SalPrintf(buf, 32, CONFIG_NEWNAMEHISTORY, i);
            registry->DeleteValue(regKey, RenamerTextToWide(buf).c_str());
            SalPrintf(buf, 32, CONFIG_SEARCHHISTORY, i);
            registry->DeleteValue(regKey, RenamerTextToWide(buf).c_str());
            SalPrintf(buf, 32, CONFIG_REPLACEHISTORY, i);
            registry->DeleteValue(regKey, RenamerTextToWide(buf).c_str());
            SalPrintf(buf, 32, CONFIG_COMMANDHISTORY, i);
            registry->DeleteValue(regKey, RenamerTextToWide(buf).c_str());
        }
    }

    registry->SetValue(regKey, CONFIG_CUSTOMFONT, REG_DWORD, &UseCustomFont, sizeof(int));
    if (UseCustomFont)
        registry->SetValue(regKey, CONFIG_MANUALFONT, REG_BINARY, &ManualModeLogFont, sizeof(LOGFONT));

    // window position
    HWND lastWnd = WindowQueue.GetLastWnd();
    if (lastWnd != NULL)
    {
        WINDOWPLACEMENT wndpl;
        wndpl.length = sizeof(WINDOWPLACEMENT);
        if (GetWindowPlacement(lastWnd, &wndpl))
        {
            DialogWidth = wndpl.rcNormalPosition.right - wndpl.rcNormalPosition.left;
            DialogHeight = wndpl.rcNormalPosition.bottom - wndpl.rcNormalPosition.top;
            Maximized = wndpl.showCmd == SW_SHOWMAXIMIZED;
        }
        // CRenamerDialog * dlg = (CRenamerDialog*) WindowsManager.GetWindowPtr(lastWnd);
        // if (dlg && dlg->Is(otDialog))
        // {
        //   LastOptions = dlg->RenamerOptions;
        //   LastSubdirs = dlg->Subdirs;
        //   LastRemoveSourcePath = dlg->RemoveSourcePath;
        // }
    }
    registry->SetValue(regKey, CONFIG_WIDTH, REG_DWORD, &DialogWidth, sizeof(int));
    registry->SetValue(regKey, CONFIG_HEIGHT, REG_DWORD, &DialogHeight, sizeof(int));
    registry->SetValue(regKey, CONFIG_MAXIMIZED, REG_DWORD, &Maximized, sizeof(BOOL));
    // last options settings
    HKEY subKey;
    if (registry->CreateKey(regKey, CONFIG_LASTUSED, subKey))
    {
        LastOptions.Save(subKey, registry);
        SetValueSZ(registry, subKey, CONFIG_LASTMASK, LastMask);
        registry->SetValue(subKey, CONFIG_LASTSUBDIRS, REG_DWORD, &LastSubdirs, sizeof(BOOL));
        registry->SetValue(subKey, CONFIG_LASTREMOVESOURCE, REG_DWORD,
                           &LastRemoveSourcePath, sizeof(BOOL));
        registry->CloseKey(subKey);
    }
    // Counter dialog settings
    if (registry->CreateKey(regKey, SAL_REG_SUBKEY_COUNTER_W, subKey))
    {
        registry->SetValue(subKey, SAL_REG_VALUE_START_W, REG_DWORD, &LastCounterStart, sizeof(int));
        registry->SetValue(subKey, SAL_REG_VALUE_STEP_W, REG_BINARY, &LastCounterStep, sizeof(double));
        registry->SetValue(subKey, SAL_REG_VALUE_BASE_W, REG_DWORD, &LastCounterBase, sizeof(int));
        registry->SetValue(subKey, SAL_REG_VALUE_MIN_WIDTH_W, REG_DWORD, &LastCounterMinWidth, sizeof(int));
        registry->SetValue(subKey, SAL_REG_VALUE_FILL_W, REG_DWORD, &LastCounterFill, sizeof(int));
        registry->SetValue(subKey, SAL_REG_VALUE_LEFT_W, REG_DWORD, &LastCounterLeft, sizeof(BOOL));
        registry->CloseKey(subKey);
    }
    SPLRegistrySetString(registry, regKey, CONFIG_COMMAND, Command);
    SPLRegistrySetString(registry, regKey, CONFIG_ARGUMENTS, Arguments);
    SPLRegistrySetString(registry, regKey, CONFIG_INITDIR, InitDir);
    registry->SetValue(regKey, CONFIG_CONFIRMESCCLOSE, REG_DWORD, &ConfirmESCClose, sizeof(BOOL));
}

void OnConfiguration(HWND hParent)
{
    CALL_STACK_MESSAGE1("OnConfiguration");

    static BOOL InConfiguration = FALSE;
    if (InConfiguration)
    {
        SG->SalMessageBox(hParent, LangStr(IDS_CFG_CONFLICT).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MB_ICONINFORMATION | MB_OK);
        return;
    }
    InConfiguration = TRUE;

    if (CConfigDialog(hParent).Execute() == IDOK)
    {
        WindowQueue.BroadcastMessage(WM_USER_CFGCHNG, 0, 0);
    }
    InConfiguration = FALSE;
}

void CPluginInterface::Configuration(HWND parent)
{
    OnConfiguration(parent);
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    /* used by the export_mnu.py script, which generates salmenu.mnu for the Translator
   keep synchronized with the salamander->AddMenuItem() calls below...
MENU_TEMPLATE_ITEM PluginMenu[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_PLUGMENU_RENAME
  {MNTT_PE, 0
};
*/
    salamander->AddMenuItem(-1, LangStr(IDS_PLUGMENU_RENAME).c_str(), SALHOTKEY('R', HOTKEYF_CONTROL | HOTKEYF_SHIFT),
                            MID_RENAME, FALSE, MENU_EVENT_FILE_FOCUSED | MENU_EVENT_DIR_FOCUSED | MENU_EVENT_FILES_SELECTED | MENU_EVENT_DIRS_SELECTED,
                            MENU_EVENT_DISK, MENU_SKILLLEVEL_ALL);
    // salamander->AddMenuItem(-1, LangStr(IDS_PLUGMENU_UNDO), 0, MID_UNDO, FALSE,
    //     MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);

    // set the plugin icon
    HBITMAP hBmp = (HBITMAP)LoadImage(DLLInstance, MAKEINTRESOURCE(IDB_RENAMER),
                                      IMAGE_BITMAP, 16, 16, SG->GetIconLRFlags());
    salamander->SetBitmapWithIcons(hBmp);
    DeleteObject(hBmp);
    salamander->SetPluginIcon(0);
    salamander->SetPluginMenuAndToolbarIcon(0);
}

CPluginInterfaceForMenuExtAbstract*
CPluginInterface::GetInterfaceForMenuExt()
{
    CALL_STACK_MESSAGE1("CPluginInterface::GetInterfaceForMenuExt()");
    return &InterfaceForMenuExt;
}

void CPluginInterface::Event(int event, DWORD param)
{
    CALL_STACK_MESSAGE2("CPluginInterface::Event(, 0x%X)", param);
    switch (event)
    {
    case PLUGINEVENT_COLORSCHANGED:
    {
        // HSymbolsImageList must not be NULL, otherwise the entry point would return an error
        COLORREF bkColor = GetSysColor(COLOR_WINDOW);
        if (ImageList_GetBkColor(HSymbolsImageList) != bkColor)
            ImageList_SetBkColor(HSymbolsImageList, bkColor);
        break;
    }

    case PLUGINEVENT_CONFIGURATIONCHANGED:
    {
        // load confirmations from the configuration
        Silent = 0;
        BOOL b;
        if (SG->GetConfigParameter(SALCFG_CNFRMFILEOVER, &b, sizeof(b), NULL) && !b)
            Silent |= SILENT_OVERWRITE_FILE_EXIST;
        if (SG->GetConfigParameter(SALCFG_CNFRMSHFILEOVER, &b, sizeof(b), NULL) && !b)
            Silent |= SILENT_OVERWRITE_FILE_SYSHID;

        SG->GetConfigParameter(SALCFG_MINBEEPWHENDONE, &MinBeepWhenDone, sizeof(BOOL), NULL);
        break;
    }

    case PLUGINEVENT_SETTINGCHANGE:
    {
        WindowQueue.BroadcastMessage(WM_USER_SETTINGCHANGE, 0, 0);
        break;
    }
    }
}

void CPluginInterface::ClearHistory(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginInterface::ClearHistory()");

    int i;
    for (i = 0; i < MAX_HISTORY_ENTRIES; i++)
    {
        if (MaskHistory[i])
        {
            free(MaskHistory[i]);
            MaskHistory[i] = NULL;
        }
        if (NewNameHistory[i])
        {
            free(NewNameHistory[i]);
            NewNameHistory[i] = NULL;
        }
        if (SearchHistory[i])
        {
            free(SearchHistory[i]);
            SearchHistory[i] = NULL;
        }
        if (ReplaceHistory[i])
        {
            free(ReplaceHistory[i]);
            ReplaceHistory[i] = NULL;
        }
        if (CommandHistory[i])
        {
            free(CommandHistory[i]);
            CommandHistory[i] = NULL;
        }
    }
}
