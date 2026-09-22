// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

// enable dumping blocks left allocated on the heap when the plug-in ends
// #define DUMP_MEM

// ****************************************************************************

HINSTANCE DLLInstance = NULL; // handle to SPL - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to SLG - language-dependent resources
BOOL WindowsVistaAndLater;

// plug-in interface object whose methods Salamander calls
CPluginInterface PluginInterface;
CPluginInterfaceForMenuExt InterfaceForMenuExt;
CPluginInterfaceForFS InterfaceForFS;

// general Salamander interface - valid from plug-in startup until shutdown
CSalamanderGeneralAbstract* SG = NULL;

// define the variable for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

// define the variable for "spl_com.h"
int SalamanderVersion = 0;

CSalamanderGUIAbstract* SalGUI = NULL;

const wchar_t* CONFIG_RECENTPATH = L"Recent Path";
const wchar_t* CONFIG_COPYORMOVEHISTORY = L"Copy Or Move History %d";
const wchar_t* CONFIG_PATTERNHISTORY = L"Pattern History %d";
const wchar_t* CONFIG_LOOKINHISTORY = L"Look In History %d";
const wchar_t* CONFIG_WIDTH = L"Window Width";
const wchar_t* CONFIG_HEIGHT = L"Window Height";
const wchar_t* CONFIG_MAXIMIZED = L"Maximized";
const wchar_t* CONFIG_COMMAND = L"Command";
const wchar_t* CONFIG_ARGUMENTS = L"Arguments";
const wchar_t* CONFIG_INITDIR = L"InitDir";
const wchar_t* CONFIG_EXPORTDIR = L"Last Export Directory";
const wchar_t* CONFIG_TYPEDATECOLFW = L"TypeDateColFW";
const wchar_t* CONFIG_TYPEDATECOLW = L"TypeDateColW";
const wchar_t* CONFIG_DATATIMECOLFW = L"DataTimeColFW";
const wchar_t* CONFIG_DATATIMECOLW = L"DataTimeColW";
const wchar_t* CONFIG_SIZECOLFW = L"SizeColFW";
const wchar_t* CONFIG_SIZECOLW = L"SizeColW";

// FS name assigned by Salamander after loading the plug-in
std::wstring AssignedFSName;

CThreadQueue ThreadQueue("RegEdt Find Dialogs, Workers, and Changes Monitor");
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

std::wstring LoadStrW(int resID)
{
    return SPLLoadStrOwned(SG, HLanguage, resID); // LoadStrW folded into the wide primary
}

namespace
{
std::wstring GetRegedtSystemErrorText(int error)
{
    if (SG != nullptr)
        return SPLGetErrorTextOwned(SG, error);

    wchar_t* systemText = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&systemText), 0, nullptr);
    if (length == 0 || systemText == nullptr)
        return std::wstring();
    try
    {
        std::wstring result(systemText, length);
        LocalFree(systemText);
        return result;
    }
    catch (...)
    {
        LocalFree(systemText);
        throw;
    }
}
} // namespace

BOOL ErrorHelper(HWND parent, const wchar_t* message, int lastError, va_list arglist)
{
    CALL_STACK_MESSAGE3("ErrorHelper(, %ls, %d, )", message, lastError);
    std::wstring text;
    try
    {
        text = SPLFormatStringOwnedV(message, arglist);
        if (lastError != ERROR_SUCCESS)
        {
            if (!text.empty() && text.back() != L' ')
                text.push_back(L' ');
            text += GetRegedtSystemErrorText(lastError);
        }
    }
    catch (...)
    {
        text = L"Error";
    }
    if (SG)
    {
        if ((DWORD_PTR)parent == -1)
            parent = SG->GetMsgBoxParent();
        try
        {
            SG->SalMessageBox(parent, text.c_str(), LangStr(IDS_REGEDTERR).c_str(), MB_ICONERROR);
        }
        catch (...)
        {
            SG->SalMessageBox(parent, text.c_str(), L"Registry Editor", MB_ICONERROR);
        }
    }
    else
    {
        if ((DWORD_PTR)parent == -1)
            parent = 0;
        MessageBoxW(parent, text.c_str(), L"RegEdit - Error", MB_OK | MB_ICONERROR);
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

BOOL ErrorL(int lastError, HWND parent, int error, ...)
{
    CALL_STACK_MESSAGE3("ErrorL(%d, , %d, )", lastError, error);
    va_list arglist;
    va_start(arglist, error);
    BOOL ret = ErrorHelper(parent, LangStr(error).c_str(), lastError, arglist);
    va_end(arglist);
    return ret;
}

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
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE3("SalPrintf(, 0x%X, %s, )", count, format);
    va_list arglist;
    va_start(arglist, format);
    int ret = _vsnprintf_s(buffer, count, _TRUNCATE, format, arglist);
    va_end(arglist);
    return ret;
}

int SalPrintfW(LPWSTR buffer, unsigned count, LPCWSTR format, ...)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("SalPrintfW(, 0x%X, , )", count);
    va_list arglist;
    va_start(arglist, format);
    int ret = _vsnwprintf_s(buffer, count, _TRUNCATE, format, arglist);
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

#define REGEDT_WIDEN2(x) L##x
#define REGEDT_WIDEN(x) REGEDT_WIDEN2(x)

    // this plug-in targets the current Salamander version and newer - verify that
    if (SalamanderVersion < LAST_VERSION_OF_SALAMANDER)
    { // cannot call Error here because it uses SG->SalMessageBox (SG not initialized + incompatible interface)
        // wide: REQUIRE_LAST_VERSION_OF_SALAMANDER is a shared narrow SDK macro
        // (spl_vers.h) used by ~35 plugins - widen only at this call site via the same
        // two-macro token-paste idiom already used for __WFILE__ in common/trace.h, rather
        // than touching the shared macro itself.
        MessageBoxW(salamander->GetParentWindow(),
                    REGEDT_WIDEN(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"Registry Editor" /* neprekladat! */, MB_OK | MB_ICONERROR);
        return NULL;
    }

    // load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"Registry Editor" /* neprekladat! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain Salamander's general interface
    SG = salamander->GetSalamanderGeneral();
    SalGUI = salamander->GetSalamanderGUI();

    // set the help file name
    SG->SetHelpFileName(L"regedt.chm");

    // detect which OS we run on
    WindowsVistaAndLater = SalIsWindowsVersionOrGreater(6, 0, 0);

    if (!InitDialogs())
        return NULL;

    if (!InitFS())
    {
        ReleaseDialogs();
        return NULL;
    }

    // set the basic plug-in information
    salamander->SetBasicPluginData(LoadStrW(IDS_PLUGINNAME).c_str(),
                                   FUNCTION_FILESYSTEM | FUNCTION_LOADSAVECONFIGURATION | FUNCTION_CONFIGURATION,
                                   REGEDT_WIDEN(VERSINFO_VERSION_NO_PLATFORM),
                                   REGEDT_WIDEN(VERSINFO_COPYRIGHT),
                                   LoadStrW(IDS_DESCRIPTION).c_str(),
                                   L"RegEdit",
                                   NULL,
                                   L"reg");

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

#undef REGEDT_WIDEN
#undef REGEDT_WIDEN2

    // obtain our FS name (it may differ from "reg"; Salamander can adjust it)
    AssignedFSName = SPLGetPluginFSNameOwned(SG, 0);

    return &PluginInterface;
}

// ****************************************************************************
//
// CPluginInterface
//

void CPluginInterface::About(HWND parent)
{
    try
    {
        const std::wstring message = SPLFormatStringOwned(
            L"%s " _CRT_WIDE(VERSINFO_VERSION) L"\n\n" _CRT_WIDE(VERSINFO_COPYRIGHT) L"\n\n%s",
            LangStr(IDS_PLUGINNAME).c_str(), LangStr(IDS_DESCRIPTION).c_str());
        SG->SalMessageBox(parent, message.c_str(), LoadStrW(IDS_ABOUT).c_str(),
                          MB_OK | MB_ICONINFORMATION);
    }
    catch (...)
    {
        SG->SalMessageBox(parent, L"Registry Editor", L"About",
                          MB_OK | MB_ICONINFORMATION);
    }
}

BOOL CPluginInterface::Release(HWND parent, BOOL force)
{
    CALL_STACK_MESSAGE2("CPluginInterface::Release(, %d)", force);

    BOOL ret = WindowQueue.Empty();
    if (!ret)
    {
        ret = WindowQueue.CloseAllWindows(force) || force;
    }
    if (ret)
    {
        ChangeMonitor.Stop();

        if (!ThreadQueue.KillAll(force) && !force)
            ret = FALSE; // Petr: it's OK that the change monitor stopped; our FS should be gone
        else             // the FS should no longer exist (plug-in unload = removed from panel)
        {
            ReleaseDialogs();
            ReleaseFS();

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
    // default values
    DialogWidth = DialogHeight = -1;
    RecentFullPath = L"\\";
    Command.clear();
    Arguments = L"\"$(Name)\"";
    InitDir = L"$(FullPath)";
    LastExportPath.clear();
    if (regKey)
    {
        DWORD recentPathBytes = 0;
        if (registry->GetSize(regKey, CONFIG_RECENTPATH, REG_BINARY,
                              recentPathBytes) &&
            recentPathBytes >= sizeof(wchar_t) &&
            recentPathBytes % sizeof(wchar_t) == 0)
        {
            std::vector<wchar_t> recentPathStorage(
                recentPathBytes / sizeof(wchar_t) + 1, L'\0');
            if (registry->GetValue(regKey, CONFIG_RECENTPATH, REG_BINARY,
                                   recentPathStorage.data(), recentPathBytes))
            {
                RecentFullPath.assign(
                    recentPathStorage.data(),
                    wcsnlen(recentPathStorage.data(), recentPathStorage.size()));
            }
        }

        // history of opened files
        LoadHistory(regKey, CONFIG_COPYORMOVEHISTORY, CopyOrMoveHistory, registry);
        // history of searched patterns
        LoadHistory(regKey, CONFIG_PATTERNHISTORY, PatternHistory, registry);
        // history of searched paths
        LoadHistory(regKey, CONFIG_LOOKINHISTORY, LookInHistory, registry);

        // window position
        if (!registry->GetValue(regKey, CONFIG_WIDTH, REG_DWORD, &DialogWidth, sizeof(int)) ||
            !registry->GetValue(regKey, CONFIG_HEIGHT, REG_DWORD, &DialogHeight, sizeof(int)))
        {
            DialogWidth = DialogHeight = -1;
        }
        registry->GetValue(regKey, CONFIG_MAXIMIZED, REG_DWORD, &Maximized, sizeof(BOOL));
        const auto loadString = [&](const wchar_t* name, std::wstring& value)
        {
            std::wstring loaded;
            if (SPLRegistryGetStringOwned(registry, regKey, name, loaded))
                value = std::move(loaded);
        };
        loadString(CONFIG_COMMAND, Command);
        loadString(CONFIG_ARGUMENTS, Arguments);
        loadString(CONFIG_INITDIR, InitDir);
        loadString(CONFIG_EXPORTDIR, LastExportPath);

        registry->GetValue(regKey, CONFIG_TYPEDATECOLFW, REG_DWORD, &TypeDateColFW, sizeof(int));
        registry->GetValue(regKey, CONFIG_TYPEDATECOLW, REG_DWORD, &TypeDateColW, sizeof(int));
        registry->GetValue(regKey, CONFIG_DATATIMECOLFW, REG_DWORD, &DataTimeColFW, sizeof(int));
        registry->GetValue(regKey, CONFIG_DATATIMECOLW, REG_DWORD, &DataTimeColW, sizeof(int));
        registry->GetValue(regKey, CONFIG_SIZECOLFW, REG_DWORD, &SizeColFW, sizeof(int));
        registry->GetValue(regKey, CONFIG_SIZECOLW, REG_DWORD, &SizeColW, sizeof(int));
    }
}

void CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, , )");
    if (RecentFullPath.size() < (std::numeric_limits<DWORD>::max)() /
                                    sizeof(wchar_t))
    {
        registry->SetValue(
            regKey, CONFIG_RECENTPATH, REG_BINARY, RecentFullPath.c_str(),
            static_cast<DWORD>((RecentFullPath.size() + 1) * sizeof(wchar_t)));
    }

    BOOL b;
    if (SG->GetConfigParameter(SALCFG_SAVEHISTORY, &b, sizeof(BOOL), NULL) && b)
    {
        SaveHistory(regKey, CONFIG_COPYORMOVEHISTORY, CopyOrMoveHistory, registry);
        SaveHistory(regKey, CONFIG_PATTERNHISTORY, PatternHistory, registry);
        SaveHistory(regKey, CONFIG_LOOKINHISTORY, LookInHistory, registry);
    }
    else
    {
        // trim the histories
        int i;
        for (i = 0; i < MAX_HISTORY_ENTRIES; i++)
        {
            const std::wstring copyName = SPLFormatStringOwned(CONFIG_COPYORMOVEHISTORY, i);
            const std::wstring patternName = SPLFormatStringOwned(CONFIG_PATTERNHISTORY, i);
            const std::wstring lookInName = SPLFormatStringOwned(CONFIG_LOOKINHISTORY, i);
            registry->DeleteValue(regKey, copyName.c_str());
            registry->DeleteValue(regKey, patternName.c_str());
            registry->DeleteValue(regKey, lookInName.c_str());
        }
    }
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
    }

    registry->SetValue(regKey, CONFIG_WIDTH, REG_DWORD, &DialogWidth, sizeof(int));
    registry->SetValue(regKey, CONFIG_HEIGHT, REG_DWORD, &DialogHeight, sizeof(int));
    registry->SetValue(regKey, CONFIG_MAXIMIZED, REG_DWORD, &Maximized, sizeof(BOOL));
    SPLRegistrySetString(registry, regKey, CONFIG_COMMAND, Command);
    SPLRegistrySetString(registry, regKey, CONFIG_ARGUMENTS, Arguments);
    SPLRegistrySetString(registry, regKey, CONFIG_INITDIR, InitDir);
    SPLRegistrySetString(registry, regKey, CONFIG_EXPORTDIR, LastExportPath);

    registry->SetValue(regKey, CONFIG_TYPEDATECOLFW, REG_DWORD, &TypeDateColFW, sizeof(int));
    registry->SetValue(regKey, CONFIG_TYPEDATECOLW, REG_DWORD, &TypeDateColW, sizeof(int));
    registry->SetValue(regKey, CONFIG_DATATIMECOLFW, REG_DWORD, &DataTimeColFW, sizeof(int));
    registry->SetValue(regKey, CONFIG_DATATIMECOLW, REG_DWORD, &DataTimeColW, sizeof(int));
    registry->SetValue(regKey, CONFIG_SIZECOLFW, REG_DWORD, &SizeColFW, sizeof(int));
    registry->SetValue(regKey, CONFIG_SIZECOLW, REG_DWORD, &SizeColW, sizeof(int));
}

void CPluginInterface::Configuration(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Configuration()");
    CConfigDialog(parent).Execute();
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    /* used by the export_mnu.py script that generates salmenu.mnu for Translator
   keep synchronized with the salamander->AddMenuItem() calls below...
MENU_TEMPLATE_ITEM PluginMenu[] = 
{
	{MNTT_PB, 0
	{MNTT_IT, IDS_FIND
	{MNTT_IT, IDS_MENUNEWKEY
	{MNTT_IT, IDS_MENUNEWVAL
	{MNTT_IT, IDS_EXPORT
	{MNTT_PE, 0
};
*/
    salamander->AddMenuItem(-1, LoadStrW(IDS_FIND).c_str(), 0, MID_FIND, FALSE, MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, NULL, 0, 0, FALSE, 0, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStrW(IDS_MENUNEWKEY).c_str(), 0, MID_NEWKEY, FALSE,
                            MENU_EVENT_THIS_PLUGIN_FS | MENU_EVENT_SUBDIR,
                            MENU_EVENT_THIS_PLUGIN_FS | MENU_EVENT_SUBDIR,
                            MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStrW(IDS_MENUNEWVAL).c_str(), 0, MID_NEWVAL, FALSE,
                            MENU_EVENT_THIS_PLUGIN_FS | MENU_EVENT_SUBDIR,
                            MENU_EVENT_THIS_PLUGIN_FS | MENU_EVENT_SUBDIR,
                            MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, NULL, 0, 0, FALSE, 0, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStrW(IDS_EXPORT).c_str(), 0, MID_EXPORT, FALSE, MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);

    HBITMAP hBmp = (HBITMAP)LoadImage(DLLInstance, MAKEINTRESOURCE(IDB_REGEDT),
                                      IMAGE_BITMAP, 16, 16, LR_DEFAULTCOLOR);
    salamander->SetBitmapWithIcons(hBmp);
    DeleteObject(hBmp);
    salamander->SetChangeDriveMenuItem(LoadStrW(IDS_DRIVEMENUTEXT).c_str(), 0);
    salamander->SetPluginIcon(0);
    salamander->SetPluginMenuAndToolbarIcon(0);
}

void CPluginInterface::ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData)
{
    CALL_STACK_MESSAGE1("CPluginInterface::ReleasePluginDataInterface()");
    delete ((CPluginDataInterface*)pluginData);
}

CPluginInterfaceForMenuExtAbstract*
CPluginInterface::GetInterfaceForMenuExt()
{
    CALL_STACK_MESSAGE1("CPluginInterface::GetInterfaceForMenuExt()");
    return &InterfaceForMenuExt;
}

CPluginInterfaceForFSAbstract*
CPluginInterface::GetInterfaceForFS()
{
    CALL_STACK_MESSAGE1("CPluginInterface::GetInterfaceForFS()");
    return &InterfaceForFS;
}

void CPluginInterface::Event(int event, DWORD param)
{
    CALL_STACK_MESSAGE2("CPluginInterface::Event(, 0x%X)", param);
    if (event == PLUGINEVENT_COLORSCHANGED)
    {
        // ImageList must not be NULL, otherwise the entry point would fail
        COLORREF bkColor = SG->GetCurrentColor(SALCOL_ITEM_BK_NORMAL);
        if (ImageList_GetBkColor(ImageList) != bkColor)
            ImageList_SetBkColor(ImageList, bkColor);
    }
}

void CPluginInterface::ClearHistory(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginInterface::ClearHistory()");
    CopyOrMoveHistory.clear();
    PatternHistory.clear();
    LookInHistory.clear();
}
