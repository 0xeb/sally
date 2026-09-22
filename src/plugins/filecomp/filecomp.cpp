// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "reg_sz_narrow_bridge.h"
#include "common/LegacyLogFontImport.h" // ImportLegacyLogFont

#pragma comment(lib, "uxtheme.lib")

// Registry-corruption bug family (see 24-intree-plugins-wide.md):
// CSalamanderRegistry::GetValue/SetValue's REG_SZ path is wide-only - reads
// memcpy the stored UTF-16LE bytes straight into the caller's buffer, and
// writes derive their length from wcslen() over 'data' regardless of the
// dataSize argument. ASCII8InputEncTableName is genuinely narrow (char*) by design,
// so it must bridge here rather than
// round-trip through GetValue/SetValue directly. Same wiring as
// plugins/ftp/ftp.cpp's SetValueSZ/GetValueSZ.
static BOOL SetValueSZ(CSalamanderRegistryAbstract* registry, HKEY regKey, const wchar_t* name, const char* narrowValue)
{
    std::wstring wide;
    if (!EncodeRegSzFromNarrowOwned(narrowValue, wide))
        return FALSE;
    return SPLRegistrySetString(registry, regKey, name, wide);
}

static BOOL GetValueSZ(CSalamanderRegistryAbstract* registry, HKEY regKey, const wchar_t* name, char* narrowBuf, int narrowBufSize)
{
    std::wstring wideBuf;
    if (!SPLRegistryGetStringOwned(registry, regKey, name, wideBuf))
        return FALSE;
    return DecodeRegSzToNarrow(wideBuf.c_str(), narrowBuf, narrowBufSize);
}

// enables dumping blocks that remain allocated on the heap after the plugin finishes
// #define DUMP_MEM

// plugin interface object whose methods are invoked by Salamander
CPluginInterface PluginInterface;
// portion of CPluginInterface dedicated to menus
CPluginInterfaceForMenu InterfaceForMenu;

CWindowQueue MainWindowQueue("FileComp Windows"); // list of all plugin windows
CThreadQueue ThreadQueue("FileComp Windows, Workers, and Remote Control");
CMappedFontFactory MappedFontFactory;
HINSTANCE hNormalizDll = NULL;
TNormalizeString PNormalizeString = NULL;
BOOL AlwaysOnTop = FALSE;

const wchar_t* CONFIG_VERSION = L"Version";
const wchar_t* CONFIG_CONFIGURATION = L"Configuration";
const wchar_t* CONFIG_COLORS = L"Colors";
const wchar_t* CONFIG_CUSTOMCOLORS = L"Custom Colors";
const wchar_t* CONFIG_DEFOPTIONS = L"Default Diff Options";
const wchar_t* CONFIG_FORCETEXT = L"Force Text";
const wchar_t* CONFIG_FORCEBINARY = L"Force Binary";
const wchar_t* CONFIG_IGNORESPACECHANGE = L"Ignore Space Change";
const wchar_t* CONFIG_IGNOREALLSPACE = L"Ignore All Space";
const wchar_t* CONFIG_IGNORELINEBREAKSCHG = L"Ignore Line Breaks Changes";
const wchar_t* CONFIG_IGNORECASE = L"Ignore Case";
const wchar_t* CONFIG_EOLCONVERSION0 = L"EOL Conversion 0";
const wchar_t* CONFIG_EOLCONVERSION1 = L"EOL Conversion 1";
const wchar_t* CONFIG_REBARBANDSLAYOUT = L"Rebar Bands Layout";
const wchar_t* CONFIG_HISTORY = L"History %d";
const wchar_t* CONFIG_LASTCFGPAGE = L"Last Configuration Page";
const wchar_t* CONFIG_LOADONSTART = L"Load On Start";
const wchar_t* CONFIG_VIEW_HORIZONTAL = L"Horizontal View";
const wchar_t* CONFIG_AUTO_COPY = L"Auto-Copy Selection";
const wchar_t* CONFIG_NORMALIZATION_FORM = L"Normalization Form";
const wchar_t* CONFIG_ENCODING0 = L"Encoding 0";
const wchar_t* CONFIG_ENCODING1 = L"Encoding 1";
const wchar_t* CONFIG_ENDIANS0 = L"Endians 0";
const wchar_t* CONFIG_ENDIANS1 = L"Endians 1";
const wchar_t* CONFIG_INPUTENC0 = L"InputEnc 0";
const wchar_t* CONFIG_INPUTENC1 = L"InputEnc 1";
const wchar_t* CONFIG_INPUTENCTABLE0 = L"InputEnc Table 0";
const wchar_t* CONFIG_INPUTENCTABLE1 = L"InputEnc Table 1";

BOOL LoadOnStart;

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

#ifdef DUMP_MEM
    _CrtMemCheckpoint(&___CrtMemState);
#endif //DUMP_MEM

    if (!InitLCUtils(salamander, "File Comparator" /* do not translate! */))
        return NULL;

    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

    SG->SetHelpFileName(L"filecomp.chm");

    if (!InitDialogs())
    {
        ReleaseLCUtils();
        return NULL;
    }

    InitXUnicode();
    MappedFontFactory.Init();

    hNormalizDll = LoadLibraryW(L"normaliz.dll");
    if (hNormalizDll)
    {
        PNormalizeString = (TNormalizeString)GetProcAddress(hNormalizDll, "NormalizeString"); // Min: Vista
    }

    // set basic metadata about the plugin
    salamander->SetBasicPluginData(SPLLoadStrOwned(SG, HLanguage, IDS_PLUGINNAME).c_str(),
                                   FUNCTION_LOADSAVECONFIGURATION | FUNCTION_CONFIGURATION,
                                   _CRT_WIDE(VERSINFO_VERSION_NO_PLATFORM),
                                   _CRT_WIDE(VERSINFO_COPYRIGHT),
                                   SPLLoadStrOwned(SG, HLanguage, IDS_PLUGIN_DESCRIPTION).c_str(),
                                   L"File Comparator" /* do not translate! */);

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    // must be after salamander->SetBasicPluginData because worker threads use the plugin
    // version at startup and salamander->SetBasicPluginData updates that value (it used to
    // crash occasionally when the version string was reallocated and the freed buffer was
    // still referenced)
    CRemoteComparator::CreateRemoteComparator();

    return &PluginInterface;
}

// ****************************************************************************
//
// CPluginInterface
//

void CPluginInterface::About(HWND parent)
{
    const std::wstring text = SPLFormatStringOwned(
        L"%ls %ls\n\n%ls\n\n%ls",
        SPLLoadStrOwned(SG, HLanguage, IDS_PLUGINNAME).c_str(),
        _CRT_WIDE(VERSINFO_VERSION), _CRT_WIDE(VERSINFO_COPYRIGHT),
        SPLLoadStrOwned(SG, HLanguage, IDS_PLUGIN_DESCRIPTION).c_str());
    SG->SalMessageBox(parent, text.c_str(),
                      SPLLoadStrOwned(SG, HLanguage, IDS_ABOUT).c_str(),
                      MB_OK | MB_ICONINFORMATION);
}

void WINAPI
LoadOrSaveConfiguration(BOOL load, HKEY regKey, CSalamanderRegistryAbstract* registry, void* param)
{
    CALL_STACK_MESSAGE2("LoadOrSaveConfiguration(%d, , , )", load);
    if (!load)
        PluginInterface.SaveConfiguration((HWND)param, regKey, registry);
}

BOOL CPluginInterface::Release(HWND parent, BOOL force)
{
    CALL_STACK_MESSAGE2("CPluginInterface::Release(, %d)", force);

    BOOL ret = CRemoteComparator::Terminate(force) || force;
    if (ret)
    {
        ret = MainWindowQueue.Empty();
        if (!ret)
        {
            ret = MainWindowQueue.CloseAllWindows(force) || force;
        }
        if (ret)
        {
            if (!ThreadQueue.KillAll(force) && !force)
                ret = FALSE;
            else
            {
                //SG->CallLoadOrSaveConfiguration(FALSE, LoadOrSaveConfiguration, parent);

                ReleaseDialogs();
                ReleaseLCUtils();
                MappedFontFactory.Free();
                if (hNormalizDll)
                    FreeLibrary(hNormalizDll);

#ifdef DUMP_MEM
                _CrtMemDumpAllObjectsSince(&___CrtMemState);
#endif //DUMP_MEM
            }
        }
    }
    _CrtCheckMemory();
    return ret;
}

void CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::LoadConfiguration(, , )");

    // initialize default values

    // configuration
    ::Configuration.ConfirmSelection = TRUE;
    ::Configuration.Context = 2;
    ::Configuration.TabSize = 8;
    ::Configuration.UseViewerFont = TRUE;
    ::Configuration.WhiteSpace = (char)0xB7;
    LoadOnStart = FALSE;

    // switches
    ::Configuration.ViewMode = fvmStandard;
    ::Configuration.ShowWhiteSpace = FALSE;
    ::Configuration.DetailedDifferences = TRUE;
    ::Configuration.HorizontalView = FALSE;
    SG->GetConfigParameter(SALCFG_AUTOCOPYSELTOCLIPBOARD, &::Configuration.AutoCopy,
                           sizeof(::Configuration.AutoCopy), NULL);

    // colors
    memcpy(Colors, DefaultColors, sizeof(SALCOLOR) * NUMBER_OF_COLORS);
    BandsParams[0].Width = -1;
    memset(CustomColors, 0, 16 * sizeof(COLORREF));

    // default compare options
    DefCompareOptions = DefaultCompareOptions;

    // history
    CBHistory.clear();

    // last configuration page that was opened
    LastCfgPage = 0;

    if (regKey != NULL) // load from the registry
    {
        DWORD configVersion = 0;
        registry->GetValue(regKey, CONFIG_VERSION, REG_DWORD, &configVersion, sizeof(DWORD));
        if ((configVersion == CURRENT_CONFIG_VERSION) || (configVersion == CURRENT_CONFIG_VERSION_NARROWLOGFONT) || (configVersion == CURRENT_CONFIG_VERSION_NORECOMPAREBUTTON) || (configVersion == CURRENT_CONFIG_VERSION_PRESEPARATEOPTIONS))
        {
            if (configVersion <= CURRENT_CONFIG_VERSION_NARROWLOGFONT)
            { // stored by a narrow release: a different, shorter layout - convert it
                CRegBLOBConfigurationNarrow blob;
                memset(&blob, 0, sizeof(blob));
                if (registry->GetValue(regKey, CONFIG_CONFIGURATION, REG_BINARY, &blob, sizeof(blob)))
                {
                    ::Configuration.ConfirmSelection = blob.ConfirmSelection;
                    ::Configuration.Context = blob.Context;
                    ::Configuration.TabSize = blob.TabSize;
                    // on failure the default font stays; the switches below are still good
                    ImportLegacyLogFont(blob.FileViewLogFont, ::Configuration.FileViewLogFont);
                    ::Configuration.UseViewerFont = blob.UseViewerFont;
                    ::Configuration.WhiteSpace = blob.WhiteSpace;
                    ::Configuration.ViewMode = blob.ViewMode;
                    ::Configuration.ShowWhiteSpace = blob.ShowWhiteSpace;
                    ::Configuration.DetailedDifferences = blob.DetailedDifferences;
                }
            }
            else
            {
                CRegBLOBConfiguration blob;
                memset(&blob, 0, sizeof(blob));
                // load configuration from the registry
                if (registry->GetValue(regKey, CONFIG_CONFIGURATION, REG_BINARY, &blob, sizeof(blob)))
                { // Configuration as stored in binary form in registry
                    ::Configuration.ConfirmSelection = blob.ConfirmSelection;
                    ::Configuration.Context = blob.Context;
                    ::Configuration.TabSize = blob.TabSize;
                    ::Configuration.FileViewLogFont = blob.FileViewLogFont;
                    ::Configuration.UseViewerFont = blob.UseViewerFont;
                    ::Configuration.WhiteSpace = blob.WhiteSpace;
                    ::Configuration.ViewMode = blob.ViewMode;
                    ::Configuration.ShowWhiteSpace = blob.ShowWhiteSpace;
                    ::Configuration.DetailedDifferences = blob.DetailedDifferences;
                }
            }

            // load colors
            registry->GetValue(regKey, CONFIG_COLORS, REG_BINARY, Colors, sizeof(SALCOLOR) * NUMBER_OF_COLORS);
            registry->GetValue(regKey, CONFIG_CUSTOMCOLORS, REG_BINARY, CustomColors, 16 * sizeof(COLORREF));
            // default compare options
            if (configVersion == CURRENT_CONFIG_VERSION_PRESEPARATEOPTIONS)
            {
                // import old config
                struct COldOptions
                {
                    int ForceText;
                    int ForceBinary;
                    int IgnoreSpaceChange;
                    int IgnoreAllSpace;
                    int Unused1;
                    int Unused2;
                    int Unused3;
                    int IgnoreCase;
                    char* Unused4[4];
                    char* Unused5[3];
                    unsigned int EolConversion[2];
                } old;
                if (registry->GetValue(regKey, CONFIG_DEFOPTIONS, REG_BINARY, &old, sizeof(COldOptions)))
                {
                    DefCompareOptions.ForceText = old.ForceText;
                    DefCompareOptions.ForceBinary = old.ForceBinary;
                    DefCompareOptions.IgnoreSpaceChange = old.IgnoreSpaceChange;
                    DefCompareOptions.IgnoreAllSpace = old.IgnoreAllSpace;
                    DefCompareOptions.IgnoreCase = old.IgnoreCase;
                    DefCompareOptions.EolConversion[0] = old.EolConversion[0] >> 1;
                    DefCompareOptions.EolConversion[1] = old.EolConversion[1] >> 1;
                }
            }
            else
            {
                _ASSERT(sizeof(int) == 4);
                registry->GetValue(regKey, CONFIG_FORCETEXT, REG_DWORD, &DefCompareOptions.ForceText, 4);
                registry->GetValue(regKey, CONFIG_FORCEBINARY, REG_DWORD, &DefCompareOptions.ForceBinary, 4);
                registry->GetValue(regKey, CONFIG_IGNORESPACECHANGE, REG_DWORD, &DefCompareOptions.IgnoreSpaceChange, 4);
                registry->GetValue(regKey, CONFIG_IGNOREALLSPACE, REG_DWORD, &DefCompareOptions.IgnoreAllSpace, 4);
                registry->GetValue(regKey, CONFIG_IGNORELINEBREAKSCHG, REG_DWORD, &DefCompareOptions.IgnoreLineBreakChanges, 4);
                registry->GetValue(regKey, CONFIG_IGNORECASE, REG_DWORD, &DefCompareOptions.IgnoreCase, 4);
                registry->GetValue(regKey, CONFIG_EOLCONVERSION0, REG_DWORD, &DefCompareOptions.EolConversion[0], 4);
                registry->GetValue(regKey, CONFIG_EOLCONVERSION1, REG_DWORD, &DefCompareOptions.EolConversion[1], 4);
                registry->GetValue(regKey, CONFIG_ENCODING0, REG_DWORD, &DefCompareOptions.Encoding[0], 4);
                registry->GetValue(regKey, CONFIG_ENCODING1, REG_DWORD, &DefCompareOptions.Encoding[1], 4);
                registry->GetValue(regKey, CONFIG_ENDIANS0, REG_DWORD, &DefCompareOptions.Endians[0], 4);
                registry->GetValue(regKey, CONFIG_ENDIANS1, REG_DWORD, &DefCompareOptions.Endians[1], 4);
                registry->GetValue(regKey, CONFIG_INPUTENC0, REG_DWORD, &DefCompareOptions.PerformASCII8InputEnc[0], 4);
                registry->GetValue(regKey, CONFIG_INPUTENC1, REG_DWORD, &DefCompareOptions.PerformASCII8InputEnc[1], 4);
                GetValueSZ(registry, regKey, CONFIG_INPUTENCTABLE0, DefCompareOptions.ASCII8InputEncTableName[0], 101);
                GetValueSZ(registry, regKey, CONFIG_INPUTENCTABLE1, DefCompareOptions.ASCII8InputEncTableName[1], 101);
                DWORD dw;
                if (registry->GetValue(regKey, CONFIG_NORMALIZATION_FORM, REG_DWORD, &dw, sizeof(DWORD)))
                    DefCompareOptions.NormalizationForm = dw ? TRUE : FALSE;
            }
            // history of recently used files
            wchar_t buf[32];
            for (int historyIndex = 0; historyIndex < MAX_HISTORY_ENTRIES;
                 ++historyIndex)
            {
                _snwprintf_s(buf, _countof(buf), _TRUNCATE, CONFIG_HISTORY,
                             historyIndex);
                std::wstring historyPath;
                if (!SPLRegistryGetStringOwned(registry, regKey, buf, historyPath))
                    break;
                CBHistory.push_back(std::move(historyPath));
            }
            if (configVersion > CURRENT_CONFIG_VERSION_NORECOMPAREBUTTON)
            {
                // rebar layout: Do not read from config versions prior to 8 because Recompare btn was added in 8
                // and thus the toolbar could be partially covered by Differences
                registry->GetValue(regKey, CONFIG_REBARBANDSLAYOUT, REG_BINARY, BandsParams, sizeof(CBandParams) * 2);
            }
            // last visited page in the configuration dialog
            registry->GetValue(regKey, CONFIG_LASTCFGPAGE, REG_DWORD, &LastCfgPage, sizeof(DWORD));
            // load on start flag
            DWORD dw;
            if (registry->GetValue(regKey, CONFIG_LOADONSTART, REG_DWORD, &dw, sizeof(DWORD)))
                LoadOnStart = dw != 0;
            if (registry->GetValue(regKey, CONFIG_VIEW_HORIZONTAL, REG_DWORD, &dw, sizeof(DWORD)))
                ::Configuration.HorizontalView = dw ? TRUE : FALSE;
            if (registry->GetValue(regKey, CONFIG_AUTO_COPY, REG_DWORD, &dw, sizeof(DWORD)))
                ::Configuration.AutoCopy = dw ? TRUE : FALSE;
        }
    }

    SG->GetConfigParameter(SALCFG_VIEWERFONT, &::Configuration.InternalViewerFont, sizeof(LOGFONT), NULL);
    if (::Configuration.UseViewerFont)
    {
        // Used the font of Internal Viewer;
        ::Configuration.FileViewLogFont = ::Configuration.InternalViewerFont;
    }

    UpdateDefaultColors(Colors, Palette);
    // Do not allow normalization if Normaliz.dll not present
    if (!PNormalizeString)
        DefCompareOptions.NormalizationForm = FALSE;
}

void CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, , )");

    // version information
    DWORD dw = CURRENT_CONFIG_VERSION;
    registry->SetValue(regKey, CONFIG_VERSION, REG_DWORD, &dw, sizeof(DWORD));

    // configuration block
    CRegBLOBConfiguration blob;
    memset(&blob, 0, sizeof(blob)); // the struct has padding, and it all goes to the registry
    // Configuration is stored in binary form in registry
    blob.ConfirmSelection = ::Configuration.ConfirmSelection;
    blob.Context = ::Configuration.Context;
    blob.TabSize = ::Configuration.TabSize;
    blob.FileViewLogFont = ::Configuration.FileViewLogFont;
    blob.UseViewerFont = ::Configuration.UseViewerFont;
    blob.WhiteSpace = ::Configuration.WhiteSpace;
    blob.ViewMode = ::Configuration.ViewMode;
    blob.ShowWhiteSpace = ::Configuration.ShowWhiteSpace;
    blob.DetailedDifferences = ::Configuration.DetailedDifferences;
    registry->SetValue(regKey, CONFIG_CONFIGURATION, REG_BINARY, &blob, sizeof(blob));

    // colors
    registry->SetValue(regKey, CONFIG_COLORS, REG_BINARY, Colors, sizeof(SALCOLOR) * NUMBER_OF_COLORS);
    registry->SetValue(regKey, CONFIG_CUSTOMCOLORS, REG_BINARY, CustomColors, 16 * sizeof(COLORREF));
    // default compare options
    registry->SetValue(regKey, CONFIG_FORCETEXT, REG_DWORD, &DefCompareOptions.ForceText, 4);
    registry->SetValue(regKey, CONFIG_FORCEBINARY, REG_DWORD, &DefCompareOptions.ForceBinary, 4);
    registry->SetValue(regKey, CONFIG_IGNORESPACECHANGE, REG_DWORD, &DefCompareOptions.IgnoreSpaceChange, 4);
    registry->SetValue(regKey, CONFIG_IGNOREALLSPACE, REG_DWORD, &DefCompareOptions.IgnoreAllSpace, 4);
    registry->SetValue(regKey, CONFIG_IGNORELINEBREAKSCHG, REG_DWORD, &DefCompareOptions.IgnoreLineBreakChanges, 4);
    registry->SetValue(regKey, CONFIG_IGNORECASE, REG_DWORD, &DefCompareOptions.IgnoreCase, 4);
    registry->SetValue(regKey, CONFIG_EOLCONVERSION0, REG_DWORD, &DefCompareOptions.EolConversion[0], 4);
    registry->SetValue(regKey, CONFIG_EOLCONVERSION1, REG_DWORD, &DefCompareOptions.EolConversion[1], 4);
    registry->SetValue(regKey, CONFIG_ENCODING0, REG_DWORD, &DefCompareOptions.Encoding[0], 4);
    registry->SetValue(regKey, CONFIG_ENCODING1, REG_DWORD, &DefCompareOptions.Encoding[1], 4);
    registry->SetValue(regKey, CONFIG_ENDIANS0, REG_DWORD, &DefCompareOptions.Endians[0], 4);
    registry->SetValue(regKey, CONFIG_ENDIANS1, REG_DWORD, &DefCompareOptions.Endians[1], 4);
    registry->SetValue(regKey, CONFIG_INPUTENC0, REG_DWORD, &DefCompareOptions.PerformASCII8InputEnc[0], 4);
    registry->SetValue(regKey, CONFIG_INPUTENC1, REG_DWORD, &DefCompareOptions.PerformASCII8InputEnc[1], 4);
    SetValueSZ(registry, regKey, CONFIG_INPUTENCTABLE0, DefCompareOptions.ASCII8InputEncTableName[0]);
    SetValueSZ(registry, regKey, CONFIG_INPUTENCTABLE1, DefCompareOptions.ASCII8InputEncTableName[1]);
    dw = DefCompareOptions.NormalizationForm;
    registry->SetValue(regKey, CONFIG_NORMALIZATION_FORM, REG_DWORD, &dw, sizeof(DWORD));
    // history of recently used files
    BOOL b;
    if (SG->GetConfigParameter(SALCFG_SAVEHISTORY, &b, sizeof(BOOL), NULL) && b)
    {
        wchar_t buf[32];
        int i;
        for (i = 0; i < static_cast<int>(CBHistory.size()); i++)
        {
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, CONFIG_HISTORY, i);
            SPLRegistrySetString(registry, regKey, buf, CBHistory[i]);
        }
    }
    else
    {
        // trim the history list
        wchar_t buf[32];
        int i;
        for (i = 0; i < MAX_HISTORY_ENTRIES; i++)
        {
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, CONFIG_HISTORY, i);
            registry->DeleteValue(regKey, buf);
        }
    }
    // rebar layout
    registry->SetValue(regKey, CONFIG_REBARBANDSLAYOUT, REG_BINARY, BandsParams, sizeof(CBandParams) * 2);
    // last configuration page that was opened
    registry->SetValue(regKey, CONFIG_LASTCFGPAGE, REG_DWORD, &LastCfgPage, sizeof(DWORD));
    dw = LoadOnStart;
    registry->SetValue(regKey, CONFIG_LOADONSTART, REG_DWORD, &dw, sizeof(DWORD));
    SG->SetFlagLoadOnSalamanderStart(LoadOnStart);
    dw = ::Configuration.HorizontalView;
    registry->SetValue(regKey, CONFIG_VIEW_HORIZONTAL, REG_DWORD, &dw, sizeof(DWORD));
    dw = ::Configuration.AutoCopy;
    registry->SetValue(regKey, CONFIG_AUTO_COPY, REG_DWORD, &dw, sizeof(DWORD));
}

void CPluginInterface::Configuration(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Configuration()");

    DWORD flag;
    CConfigurationDialog dlg(parent, &::Configuration, Colors, &DefCompareOptions, &flag);
    dlg.Execute();
    if (flag)
        MainWindowQueue.BroadcastMessage(WM_USER_CFGCHNG, flag, 0);
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    /* used by the export_mnu.py script that generates salmenu.mnu for the Translator
   keep synchronized with the salamander->AddMenuItem() calls below...
MENU_TEMPLATE_ITEM PluginMenu[] =
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_COMPAREFILES
  {MNTT_PE, 0
};
*/
    salamander->AddMenuItem(-1, SPLLoadStrOwned(SG, HLanguage, IDS_COMPAREFILES).c_str(), SALHOTKEY('C', HOTKEYF_CONTROL | HOTKEYF_SHIFT), MID_COMPAREFILES, FALSE,
                            MENU_EVENT_DISK, 0, MENU_SKILLLEVEL_ALL);
    // assign the plugin icon
    HBITMAP hBmp = (HBITMAP)LoadImage(DLLInstance, MAKEINTRESOURCE(IDB_FILECOMP),
                                      IMAGE_BITMAP, 16, 16, LR_DEFAULTCOLOR);
    salamander->SetBitmapWithIcons(hBmp);
    DeleteObject(hBmp);
    salamander->SetPluginIcon(0);
    salamander->SetPluginMenuAndToolbarIcon(0);
}

CPluginInterfaceForMenuExtAbstract*
CPluginInterface::GetInterfaceForMenuExt()
{
    CALL_STACK_MESSAGE_NONE
    return &InterfaceForMenu;
}

void CPluginInterface::Event(int event, DWORD param) // FIXME_X64 - is a 32-bit DWORD sufficient here?
{
    CALL_STACK_MESSAGE2("CPluginInterface::Event(, 0x%X)", param);
    switch (event)
    {
    case PLUGINEVENT_COLORSCHANGED:
    {
        // the text color may have changed
        MainWindowQueue.BroadcastMessage(WM_USER_CFGCHNG, CC_COLORS, 0);
        break;
    }

    case PLUGINEVENT_CONFIGURATIONCHANGED:
    {
        // Cache the value for use in Config dialog
        SG->GetConfigParameter(SALCFG_VIEWERFONT, &::Configuration.InternalViewerFont, sizeof(LOGFONT), NULL);
        // the viewer font may have changed
        if (::Configuration.UseViewerFont)
        {
            ::Configuration.FileViewLogFont = ::Configuration.InternalViewerFont;
            MainWindowQueue.BroadcastMessage(WM_USER_CFGCHNG, CC_FONT, 0);
        }
        break;
    }
    }
}

void CPluginInterface::ClearHistory(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginInterface::ClearHistory()");
    MainWindowQueue.BroadcastMessage(WM_USER_CLEARHISTORY, 0, 0);
    CBHistory.clear();
}

// ****************************************************************************
//
// CPluginInterfaceForMenu
//

BOOL CPluginInterfaceForMenu::ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander, HWND parent,
                                              int id, DWORD eventMask)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForMenu::ExecuteMenuItem(, , %d, 0x%X)",
                        id, eventMask);
    switch (id)
    {
    case MID_COMPAREFILES:
    {
        std::wstring file1, file2;
        std::wstring panelPath;
        const CFileData *fd1, *fd2 = NULL;
        int index = 0;
        BOOL isDir;
        BOOL secondFromSource = FALSE;
        int tgtPathType;
        SG->GetPanelPath(PANEL_TARGET, NULL, &tgtPathType, NULL);
        BOOL tgtPanelIsDisk = (tgtPathType == PATH_TYPE_WINDOWS);

        fd1 = SG->GetPanelSelectedItem(PANEL_SOURCE, &index, &isDir);

        if (fd1 && isDir)
            goto SELECTION_FINISHED; // ignore directories

        if (fd1)
        {
            // we have the first selected file; try to find a second one in the source panel
            fd2 = SG->GetPanelSelectedItem(PANEL_SOURCE, &index, &isDir);

            if (fd2 && isDir)
                goto SELECTION_FINISHED; // ignore directories

            if (!fd2)
            {
                // one item is selected and we take the other either from focus
                // or from the selection in the target panel
                index = 0;
                fd2 = SG->GetPanelSelectedItem(PANEL_TARGET, &index, &isDir);
                if (!tgtPanelIsDisk || !fd2 || SG->GetPanelSelectedItem(PANEL_TARGET, &index, &isDir))
                {
                    // the target panel contains zero or more than one selected file
                    // so fall back to the focused item in the source panel
                    fd2 = SG->GetPanelFocusedItem(PANEL_SOURCE, &isDir);
                }
                else
                    fd2 = NULL;
            }
            else
            {
                // we have two selected files; ensure a third one is not selected
                // three selected files are ignored
                if (SG->GetPanelSelectedItem(PANEL_SOURCE, &index, &isDir))
                    goto SELECTION_FINISHED;
            }
        }
        else
        {
            // no file was selected; use the focused item instead
            fd1 = SG->GetPanelFocusedItem(PANEL_SOURCE, &isDir);

            if (fd1 && isDir)
                goto SELECTION_FINISHED; // ignore directories
        }

        if (fd1 == NULL)
            goto SELECTION_FINISHED; // empty panel

        // store the name of the first file
        if (!SPLGetPanelPathOwned(SG, PANEL_SOURCE, panelPath))
            return NULL;
        file1 = panelPath;
        SPLSalPathAppendOwned(file1, fd1->Name);

        if (fd2 &&
            !isDir && fd2 != fd1) // in case we take the file from the focus
        {
            // store the name of the second file
            file2 = panelPath;
            SPLSalPathAppendOwned(file2, fd2->Name);
            secondFromSource = TRUE;
        }
        else
        {
            if (tgtPanelIsDisk)
            {
                // we still need to pick the second file from the target panel
                index = 0;
                fd2 = SG->GetPanelSelectedItem(PANEL_TARGET, &index, &isDir);

                if (!fd2 || isDir || SG->GetPanelSelectedItem(PANEL_TARGET, &index, &isDir))
                {
                    // find the file with the same name as the first one
                    index = 0;
                    while ((fd2 = SG->GetPanelItem(PANEL_TARGET, &index, &isDir)) != 0)
                    {
                        // For Unicode filenames, compare using wide names
                        if (!isDir)
                        {
                            if (SG->StrICmp(fd1->Name, fd2->Name) == 0)
                                break;
                        }
                    }
                }

                if (fd2)
                {
                    // store the name of the second file
                    if (!SPLGetPanelPathOwned(SG, PANEL_TARGET, file2))
                        return NULL;
                    SPLSalPathAppendOwned(file2, fd2->Name);
                }
            }
        }

    SELECTION_FINISHED:

        BOOL doNotSwapNames = secondFromSource || SG->GetSourcePanel() == PANEL_LEFT;
        SG->GetConfigParameter(SALCFG_ALWAYSONTOP, &AlwaysOnTop, sizeof(AlwaysOnTop), NULL);
        const std::wstring& path1 = doNotSwapNames ? file1 : file2;
        const std::wstring& path2 = doNotSwapNames ? file2 : file1;
        CFilecompThread* d = new CFilecompThread(
            path1.empty() ? NULL : path1.c_str(),
            path2.empty() ? NULL : path2.c_str(), FALSE, "");
        if (!d)
            return Error((HWND)-1, IDS_LOWMEM);
        if (!d->Create(ThreadQueue))
            delete d;
        SG->SetUserWorkedOnPanelPath(PANEL_SOURCE); // treat this command as working with the path (appears in Alt+F12)
        if (!secondFromSource && !file2.empty())
            SG->SetUserWorkedOnPanelPath(PANEL_TARGET); // also record the target panel

        return FALSE;
    }
    }
    return FALSE;
}

BOOL WINAPI
CPluginInterfaceForMenu::HelpForMenuItem(HWND parent, int id)
{
    int helpID = 0;
    switch (id)
    {
    case MID_COMPAREFILES:
        helpID = IDH_COMPAREFILES;
        break;
    }
    if (helpID != 0)
        SG->OpenHtmlHelp(parent, HHCDisplayContext, helpID, FALSE);
    return helpID != 0;
}

// ****************************************************************************
//
// CFilecompThread
//

unsigned
CFilecompThread::Body()
{
    CALL_STACK_MESSAGE1("CFilecompThread::CThreadBody()");
    BOOL succes = FALSE;
    BOOL dialogBox = TRUE;
    HWND wnd;
    CCompareOptions options = DefCompareOptions;

    if (Path1.empty() || Path2.empty() || !DontConfirmSelection && Configuration.ConfirmSelection)
    {
        CCompareFilesDialog* dlg = new CCompareFilesDialog(
            0, Path1, Path2, succes, &options);
        if (!dlg)
        {
            Error(HWND(NULL), IDS_LOWMEM);
            goto LBODYFINAL;
        }
        wnd = dlg->Create();
        if (!wnd)
        {
            TRACE_E("Failed to create CompareFilesDialog");
            goto LBODYFINAL;
        }
        SetForegroundWindow(wnd);
    }
    else
    {
        AddToHistory(Path2.c_str());
        AddToHistory(Path1.c_str());
        goto LLAUNCHFC;
    }

    while (1)
    {
        if (!MainWindowQueue.Add(new CWindowQueueItem(wnd)))
        {
            TRACE_E("Low memory");
            DestroyWindow(wnd);
            break;
        }

        MSG msg;
        while (IsWindow(wnd) && GetMessageW(&msg, NULL, 0, 0))
        {
            if (!dialogBox && !TranslateAccelerator(wnd, HAccels, &msg) ||
                dialogBox && !IsDialogMessage(wnd, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        if (!dialogBox || !succes)
            break; // leave the message loop

    LLAUNCHFC:

        WINDOWPLACEMENT wp;
        wp.length = sizeof(wp);
        GetWindowPlacement(SG->GetMainWindowHWND(), &wp);
        // GetWindowPlacement respects the taskbar, so if the taskbar is at the top or left
        // the coordinates are offset by its size. Apply a correction.
        RECT monitorRect;
        RECT workRect;
        SG->MultiMonGetClipRectByRect(&wp.rcNormalPosition, &workRect, &monitorRect);
        OffsetRect(&wp.rcNormalPosition, workRect.left - monitorRect.left,
                   workRect.top - monitorRect.top);

        // if the main window is minimized, keep the File Comparator restored instead
        CMainWindow* win = new CMainWindow(
            Path1.c_str(), Path2.c_str(), &options,
            wp.showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOW);
        if (!win)
        {
            Error(HWND(NULL), IDS_LOWMEM);
            break;
        }
        wnd = win->CreateEx(AlwaysOnTop ? WS_EX_TOPMOST : 0,
                            MAINWINDOW_CLASSNAME,
                            LangStr(IDS_PLUGINNAME).c_str(),
                            WS_OVERLAPPEDWINDOW | WS_VISIBLE | (wp.showCmd == SW_SHOWMAXIMIZED ? WS_MAXIMIZE : 0),
                            wp.rcNormalPosition.left,
                            wp.rcNormalPosition.top,
                            wp.rcNormalPosition.right - wp.rcNormalPosition.left,
                            wp.rcNormalPosition.bottom - wp.rcNormalPosition.top,
                            NULL, // parent
                            LoadMenu(HLanguage, MAKEINTRESOURCE(IDM_MENU)),
                            DLLInstance,
                            win);
        if (!wnd)
        {
            TRACE_E("Failed to create MainWindow, GetLastError() = " << GetLastError());
            break;
        }
        dialogBox = FALSE;
    }
LBODYFINAL:
    if (!ReleaseEvent.empty())
    {
        // allow filecomp.exe to continue
        HANDLE event = OpenEventA(EVENT_MODIFY_STATE, FALSE, ReleaseEvent.c_str());
        SetEvent(event);
        CloseHandle(event);
    }

    return 0;
}
