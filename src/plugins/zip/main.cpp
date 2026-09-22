// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "reg_sz_narrow_bridge.h"
#include <tchar.h>
#include <crtdbg.h>
#include <ostream>
#include <commctrl.h>
#include <stdio.h>
#include <vector>

#include "versinfo.rh2"

#include "spl_com.h"
#include "spl_base.h"
#include "spl_gen.h"
#include "spl_arc.h"
#include "spl_menu.h"
#include "spl_vers.h"
#include "dbg.h"

#include "array2.h"

#include "selfextr/comdefs.h"
#include "config.h"
#include "typecons.h"
#include "chicon.h"
#include "common.h"
#include "list.h"
#include "extract.h"
#include "add_del.h"
#include "zipdll.h"
#include "dialogs.h"
#include "main.h"
#include "common/unicode/helpers.h"
#include "zip.rh"
#include "zip.rh2"
#include "lang\lang.rh"

// plugin interface object whose methods Salamander calls
CPluginInterface PluginInterface;
// additional parts of the CPluginInterface
CPluginInterfaceForArchiver InterfaceForArchiver;
CPluginInterfaceForMenuExt InterfaceForMenuExt;

// general Salamander interface available from plugin startup to shutdown
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;

// interface for convenient file operations
CSalamanderSafeFileAbstract* SalamanderSafeFile = NULL;

// Interface for AES encryption
CSalamanderCryptAbstract* SalamanderCrypt = NULL;

// Interface for BZIP2 de/compression
CSalamanderBZIP2Abstract* SalamanderBZIP2;

// variable definition for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

// interface providing customized Windows controls used in Salamander
CSalamanderGUIAbstract* SalamanderGUI = NULL;

// configuration key definitions
const wchar_t* CONFIG_LEVEL = L"Level";
const wchar_t* CONFIG_ENCRYPTMETHOD = L"Encryption Method";
const wchar_t* CONFIG_NOEMPTYDIRS = L"No Empty Dirs";
const wchar_t* CONFIG_BACKUPZIP = L"Backup ZIP";
const wchar_t* CONFIG_SHOWEXOPT = L"Show Extended Options";
const wchar_t* CONFIG_TIMETONEWESTFILE = L"Time To Newest File";
const wchar_t* CONFIG_VOLSIZECACHE = L"Volume Size %d";
const wchar_t* CONFIG_VOLSIZECACHE1 = L"Volume Size 1";
const wchar_t* CONFIG_VOLSIZECACHE2 = L"Volume Size 2";
const wchar_t* CONFIG_VOLSIZECACHE3 = L"Volume Size 3";
const wchar_t* CONFIG_VOLSIZECACHE4 = L"Volume Size 4";
const wchar_t* CONFIG_VOLSIZECACHE5 = L"Volume Size 5";
const wchar_t* CONFIG_VOLSIZEUNITS = L"Volume Size Units %d";
const wchar_t* CONFIG_VOLSIZEUNITS1 = L"Volume Size Units 1";
const wchar_t* CONFIG_VOLSIZEUNITS2 = L"Volume Size Units 2";
const wchar_t* CONFIG_VOLSIZEUNITS3 = L"Volume Size Units 3";
const wchar_t* CONFIG_VOLSIZEUNITS4 = L"Volume Size Units 4";
const wchar_t* CONFIG_VOLSIZEUNITS5 = L"Volume Size Units 5";
const wchar_t* CONFIG_LASTUSEDAUTO = L"Last Used Auto";
const wchar_t* CONFIG_AUTOEXPANDMV = L"Auto Expand MV";
const wchar_t* CONFIG_VERSION = L"Version";
const wchar_t* CONFIG_DEFSFX = L"Default Sfx File";
const wchar_t* CONFIG_SFXLAST = L"Last Used Sfx Settings";
const wchar_t* CONFIG_SFXLASTSIZE = L"Last Used Sfx Settings Size";
const wchar_t* CONFIG_SFXFAV_KEY = L"Favorities";
const wchar_t* CONFIG_SFXFAVCOUNT = L"Number Of Favorities";
const wchar_t* CONFIG_SFXFAVNAME = L"Favorite %d Name";
const wchar_t* CONFIG_SFXFAVSIZE = L"Favorite %d Size";
const wchar_t* CONFIG_SFXFAVDATA = L"Favorite %d Data";
const wchar_t* CONFIG_SFXLASTEXPORTPATH = L"Last Export Path";
const wchar_t* CONFIG_SALVER = L"Salamander Version";
const wchar_t* CONFIG_CHLANG = L"Change Language Reaction";
const wchar_t* CONFIG_WINZIPNAMES = L"Winzip Names";

const wchar_t* CONFIG_LIST_INFO_PACKED_SIZE = L"List Info Packed Size";
const wchar_t* CONFIG_COL_PACKEDSIZE_FIXEDWIDTH = L"Column PackedSize FixedWidth";
const wchar_t* CONFIG_COL_PACKEDSIZE_WIDTH = L"Column PackedSize Width";

// menu command ID definitions
#define MID_CREATESFX 1
#define MID_REPAIR 2
#define MID_TEST 3
#define MID_COMMENT 4

//
// ****************************************************************************
// SalamanderPluginGetReqVer
//

int WINAPI SalamanderPluginGetReqVer()
{
    CALL_STACK_MESSAGE_NONE
    return LAST_VERSION_OF_SALAMANDER;
}

//
// ****************************************************************************
// SalamanderPluginEntry
//

CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander)
{
    CALL_STACK_MESSAGE_NONE
    // set up SalamanderDebug for "dbg.h"
    SalamanderDebug = salamander->GetSalamanderDebug();

    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

#define ZIP_WIDEN2(x) L##x
#define ZIP_WIDEN(x) ZIP_WIDEN2(x)

    // ensure the plugin runs on the current Salamander version or newer
    if (salamander->GetVersion() < LAST_VERSION_OF_SALAMANDER)
    { // reject older versions
        // wide: same call-site-local widen shape as checksum/unlha/undelete
        // (205-207).
        MessageBoxW(salamander->GetParentWindow(),
                    ZIP_WIDEN(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"ZIP" /* neprekladat! */, MB_OK | MB_ICONERROR);
        return NULL;
    }

    // load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"ZIP" /* neprekladat! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain the general Salamander interfaces
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalamanderSafeFile = salamander->GetSalamanderSafeFile();
    SalamanderCrypt = SalamanderGeneral->GetSalamanderCrypt();
    SalamanderBZIP2 = SalamanderGeneral->GetSalamanderBZIP2();

    // obtain the interface providing customized Windows controls used in Salamander
    SalamanderGUI = salamander->GetSalamanderGUI();

    SalamanderGeneral->SetHelpFileName(L"zip.chm");

    /*
    // beta valid until the end of February 2001
  SYSTEMTIME st;
  GetLocalTime(&st);
  if (st.wYear == 2001 && st.wMonth > 2 || st.wYear > 2001)
  {
    SalamanderGeneral->ShowMessageBox(LangStr(IDS_EXPIRE).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
    return NULL;
  }
  */

    // provide the basic plugin information
    salamander->SetBasicPluginData(LoadStrW(IDS_PLUGINNAME).c_str(),
                                   FUNCTION_PANELARCHIVERVIEW | FUNCTION_PANELARCHIVEREDIT |
                                       FUNCTION_CUSTOMARCHIVERPACK | FUNCTION_CUSTOMARCHIVERUNPACK |
                                       FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION,
                                   ZIP_WIDEN(VERSINFO_VERSION_NO_PLATFORM),
                                   ZIP_WIDEN(VERSINFO_COPYRIGHT),
                                   LoadStrW(IDS_PLUGIN_DESCRIPTION).c_str(),
                                   L"ZIP" /* neprekladat! */, L"zip;pk3;pk4;jar");

    // register the plugin home page URL
    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

#undef ZIP_WIDEN
#undef ZIP_WIDEN2

    return &PluginInterface;
}

//
// ****************************************************************************
// CPluginInterface
//

void CPluginInterface::About(HWND parent)
{
    const std::wstring version = ZipTextToWide(VERSINFO_VERSION);
    const std::wstring copyright = ZipTextToWide(VERSINFO_COPYRIGHT);
    wchar_t buf[1000];
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%ls %ls\n\n%ls\n\n%ls",
                 LangStr(IDS_PLUGINNAME).c_str(), version.c_str(), copyright.c_str(),
                 LangStr(IDS_PLUGIN_DESCRIPTION).c_str());
    SalamanderGeneral->SalMessageBox(parent, buf, LoadStrW(IDS_ABOUTPLUGINTITLE).c_str(), MB_OK | MB_ICONINFORMATION);
}

BOOL CPluginInterface::Release(HWND parent, BOOL force)
{
    CALL_STACK_MESSAGE2("CPluginInterface::Release(, %d)", force);
    if (SfxLanguages)
        delete SfxLanguages;
    if (DefLanguage)
        delete DefLanguage;
    return TRUE;
}

// Registry-corruption bug family (see reg_sz_narrow_bridge.h):
// VolSizeCache[] and CFavoriteSfx::Name retain their serialized byte layout.
// Config.DefSfxFile and LastExportPath are semantic names/paths and remain UTF-16.
// Locale numeric strings remain locale bytes. The shared
// registry facade's REG_SZ path (SetValueW/GetValueW) is wide-only, so these
// fields cannot round-trip through it without a bridge at the Load()/Save()
// boundary - same pattern as plugins/ftp/ftp.cpp's SetValueSZ/GetValueSZ.
// This is a distinct field family from Options.SfxSettings.SfxFile (the path
// baked into the packed CSfxSettings blob written into the SFX EXE stub via
// REG_BINARY/PackSfxSettings) - that one remains the SFX file-format boundary.
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

void ValidateDefSfxFile()
{
    // ensure the file exists; otherwise use the first available one as default
    std::wstring directory;
    if (!SPLGetModuleFileNameOwned(DLLInstance, directory))
    {
        Config.DefSfxFile.clear();
        return;
    }
    SPLCutDirectoryOwned(SalamanderGeneral, directory);
    SPLSalPathAppendOwned(directory, L"sfx");
    std::wstring path = directory;
    SPLSalPathAppendOwned(path, Config.DefSfxFile.c_str());
    DWORD attr = SalamanderGeneral->SalGetFileAttributes(path.c_str());
    if (attr == 0xFFFFFFFF || attr & FILE_ATTRIBUTE_DIRECTORY)
    {
        WIN32_FIND_DATAW fd;
        std::wstring pattern = directory;
        SPLSalPathAppendOwned(pattern, L"*.sfx");
        HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
        while (find != INVALID_HANDLE_VALUE && fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (!FindNextFileW(find, &fd))
            {
                FindClose(find);
                find = INVALID_HANDLE_VALUE;
            }
        }
        if (find == INVALID_HANDLE_VALUE)
            Config.DefSfxFile.clear();
        else
        {
            Config.DefSfxFile = fd.cFileName;
            FindClose(find);
        }
    }
}

void CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::LoadConfiguration(, ,)");
    DWORD v;
    v = Config.CurSalamanderVersion;
    Config = DefConfig;
    Config.CurSalamanderVersion = v;
    if (regKey != NULL) // load z registry
    {
        registry->GetValue(regKey, CONFIG_LEVEL, REG_DWORD, &Config.Level, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_ENCRYPTMETHOD, REG_DWORD, &Config.EncryptMethod, sizeof(DWORD));
        if (registry->GetValue(regKey, CONFIG_NOEMPTYDIRS, REG_DWORD, &v, sizeof(DWORD)))
        {
            Config.NoEmptyDirs = (v != 0);
        }
        if (registry->GetValue(regKey, CONFIG_BACKUPZIP, REG_DWORD, &v, sizeof(DWORD)))
        {
            Config.BackupZip = (v != 0);
        }
        if (registry->GetValue(regKey, CONFIG_SHOWEXOPT, REG_DWORD, &v, sizeof(DWORD)))
        {
            Config.ShowExOptions = (v != 0);
        }
        if (registry->GetValue(regKey, CONFIG_TIMETONEWESTFILE, REG_DWORD, &v, sizeof(DWORD)))
        {
            Config.TimeToNewestFile = (v != 0);
        }

        wchar_t key1[64];
        wchar_t key2[64];
        char size[MAX_VOL_STR];
        DWORD units;
        int i;
        for (i = 0; i < 5; i++)
        {
            swprintf_s(key1, _countof(key1), CONFIG_VOLSIZECACHE, i + 1);
            swprintf_s(key2, _countof(key2), CONFIG_VOLSIZEUNITS, i + 1);
            if (GetValueSZ(registry, regKey, key1, size, MAX_VOL_STR) &&
                registry->GetValue(regKey, key2, REG_DWORD, &units, sizeof(DWORD)))
            {
                lstrcpyA(Config.VolSizeCache[i], size);
                Config.VolSizeUnits[i] = units;
            }
            else
                break;
        }
        if (registry->GetValue(regKey, CONFIG_LASTUSEDAUTO, REG_DWORD, &v, sizeof(DWORD)))
        {
            Config.LastUsedAuto = (v != 0);
        }
        if (registry->GetValue(regKey, CONFIG_AUTOEXPANDMV, REG_DWORD, &v, sizeof(DWORD)))
        {
            Config.AutoExpandMV = (v != 0);
        }
        if (!registry->GetValue(regKey, CONFIG_VERSION, REG_DWORD, &Config.Version, sizeof(DWORD)))
        {
            Config.Version = 1; // beta 3 did not store this value in the configuration
        }
        SPLRegistryGetStringOwned(registry, regKey, CONFIG_DEFSFX, Config.DefSfxFile);
        ValidateDefSfxFile();

        char* buffer = NULL;
        DWORD allocated = 0;
        DWORD siz;

        // load last used sfx settings
        *LastUsedSfxSet.Name = 0;
        if (registry->GetValue(regKey, CONFIG_SFXLASTSIZE, REG_DWORD, &siz, sizeof(DWORD)))
        {
            if (siz > allocated)
            {
                allocated = max(siz, allocated * 2);
                buffer = (char*)realloc(buffer, allocated);
            }
            if (registry->GetValue(regKey, CONFIG_SFXLAST, REG_BINARY, buffer, siz))
            {
                if (ExpandSfxSettings(&LastUsedSfxSet.Settings, buffer, siz) == siz)
                {
                    lstrcpyA(LastUsedSfxSet.Name, "Last Used");
                }
            }
        }

        // load favorite options
        HKEY favKey;
        if (registry->OpenKey(regKey, CONFIG_SFXFAV_KEY, favKey))
        {
            int count;
            if (registry->GetValue(favKey, CONFIG_SFXFAVCOUNT, REG_DWORD, &count, sizeof(DWORD)))
            {
                wchar_t key[64];
                CFavoriteSfx temp;
                Favorities.Destroy();
                for (i = 0; i < count; i++)
                {
                    swprintf_s(key, _countof(key), CONFIG_SFXFAVSIZE, i + 1);
                    if (registry->GetValue(favKey, key, REG_DWORD, &siz, sizeof(DWORD)))
                    {
                        if (siz > allocated)
                        {
                            allocated = max(siz, allocated * 2);
                            buffer = (char*)realloc(buffer, allocated);
                        }
                        swprintf_s(key, _countof(key), CONFIG_SFXFAVDATA, i + 1);
                        if (registry->GetValue(favKey, key, REG_BINARY, buffer, siz))
                        {
                            if (ExpandSfxSettings(&temp.Settings, buffer, siz) == siz)
                            {
                                swprintf_s(key, _countof(key), CONFIG_SFXFAVNAME, i + 1);
                                if (GetValueSZ(registry, favKey, key, temp.Name, MAX_FAVNAME))
                                {
                                    CFavoriteSfx* newSetting = new CFavoriteSfx;
                                    if (newSetting)
                                        *newSetting = temp;
                                    if (!newSetting || !Favorities.Add(newSetting))
                                    {
                                        if (newSetting)
                                            delete newSetting;
                                        SalamanderGeneral->SalMessageBox(parent, LoadStrW(IDS_ERRLOADCONFIG).c_str(), LoadStrW(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            registry->CloseKey(favKey);
        }

        if (buffer)
            free(buffer);

        SPLRegistryGetStringOwned(registry, regKey, CONFIG_SFXLASTEXPORTPATH, Config.LastExportPath);

        if (!registry->GetValue(regKey, CONFIG_SALVER, REG_DWORD, &v, sizeof(DWORD)))
            v = 0;
        if (!registry->GetValue(regKey, CONFIG_CHLANG, REG_DWORD, &Config.ChangeLangReaction, sizeof(DWORD)))
            Config.ChangeLangReaction = CLR_ASK;
        if (!registry->GetValue(regKey, CONFIG_WINZIPNAMES, REG_DWORD, &Config.WinZipNames, sizeof(DWORD)))
            Config.WinZipNames = TRUE;

        registry->GetValue(regKey, CONFIG_LIST_INFO_PACKED_SIZE, REG_DWORD, &Config.ListInfoPackedSize, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_COL_PACKEDSIZE_FIXEDWIDTH, REG_DWORD, &Config.ColumnPackedSizeFixedWidth, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_COL_PACKEDSIZE_WIDTH, REG_DWORD, &Config.ColumnPackedSizeWidth, sizeof(DWORD));
    }
    else
    {
        ValidateDefSfxFile();
    }
}

void CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, ,)");

    registry->SetValue(regKey, CONFIG_LEVEL, REG_DWORD, &Config.Level, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_ENCRYPTMETHOD, REG_DWORD, &Config.EncryptMethod, sizeof(DWORD));
    DWORD v = Config.NoEmptyDirs;
    registry->SetValue(regKey, CONFIG_NOEMPTYDIRS, REG_DWORD, &v, sizeof(DWORD));
    v = Config.BackupZip;
    registry->SetValue(regKey, CONFIG_BACKUPZIP, REG_DWORD, &v, sizeof(DWORD));
    v = Config.ShowExOptions;
    registry->SetValue(regKey, CONFIG_SHOWEXOPT, REG_DWORD, &v, sizeof(DWORD));
    v = Config.TimeToNewestFile;
    registry->SetValue(regKey, CONFIG_TIMETONEWESTFILE, REG_DWORD, &v, sizeof(DWORD));
    /*
  registry->SetValue(regKey, CONFIG_VOLSIZECACHE1, REG_BINARY, Config.VolSizeCache[0], MAX_VOL_STR);
  registry->SetValue(regKey, CONFIG_VOLSIZECACHE2, REG_BINARY, Config.VolSizeCache[1], MAX_VOL_STR);
  registry->SetValue(regKey, CONFIG_VOLSIZECACHE3, REG_BINARY, Config.VolSizeCache[2], MAX_VOL_STR);
  registry->SetValue(regKey, CONFIG_VOLSIZECACHE4, REG_BINARY, Config.VolSizeCache[3], MAX_VOL_STR);
  registry->SetValue(regKey, CONFIG_VOLSIZECACHE5, REG_BINARY, Config.VolSizeCache[4], MAX_VOL_STR);
  */
    SetValueSZ(registry, regKey, CONFIG_VOLSIZECACHE1, Config.VolSizeCache[0]);
    SetValueSZ(registry, regKey, CONFIG_VOLSIZECACHE2, Config.VolSizeCache[1]);
    SetValueSZ(registry, regKey, CONFIG_VOLSIZECACHE3, Config.VolSizeCache[2]);
    SetValueSZ(registry, regKey, CONFIG_VOLSIZECACHE4, Config.VolSizeCache[3]);
    SetValueSZ(registry, regKey, CONFIG_VOLSIZECACHE5, Config.VolSizeCache[4]);
    registry->SetValue(regKey, CONFIG_VOLSIZEUNITS1, REG_DWORD, &Config.VolSizeUnits[0], sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_VOLSIZEUNITS2, REG_DWORD, &Config.VolSizeUnits[1], sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_VOLSIZEUNITS3, REG_DWORD, &Config.VolSizeUnits[2], sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_VOLSIZEUNITS4, REG_DWORD, &Config.VolSizeUnits[3], sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_VOLSIZEUNITS5, REG_DWORD, &Config.VolSizeUnits[4], sizeof(DWORD));
    v = Config.LastUsedAuto;
    registry->SetValue(regKey, CONFIG_LASTUSEDAUTO, REG_DWORD, &v, sizeof(DWORD));
    v = Config.AutoExpandMV;
    registry->SetValue(regKey, CONFIG_AUTOEXPANDMV, REG_DWORD, &v, sizeof(DWORD));
    v = CURRENT_CONFIG_VERSION;
    registry->SetValue(regKey, CONFIG_VERSION, REG_DWORD, &v, sizeof(DWORD));
    SPLRegistrySetString(registry, regKey, CONFIG_DEFSFX, Config.DefSfxFile);

    char* buffer = NULL;
    DWORD allocated = 0;
    DWORD siz;

    //save last used sfx settings
    if (*LastUsedSfxSet.Name)
    {
        siz = PackSfxSettings(&LastUsedSfxSet.Settings, buffer, allocated);
        if (siz == -1)
            TRACE_E("chyba v PackSfxSettings.");
        else
        {
            if (registry->SetValue(regKey, CONFIG_SFXLAST, REG_BINARY, buffer, siz))
                registry->SetValue(regKey, CONFIG_SFXLASTSIZE, REG_DWORD, &siz, sizeof(DWORD));
        }
    }

    //save favorite sfx settings
    HKEY favKey;
    if (registry->CreateKey(regKey, CONFIG_SFXFAV_KEY, favKey))
    {
        registry->ClearKey(favKey);
        wchar_t key[64];
        int i;
        for (i = 0; i < Favorities.Count; i++)
        {
            CFavoriteSfx* fav = Favorities[i];

            swprintf_s(key, _countof(key), CONFIG_SFXFAVNAME, i + 1);
            if (!SetValueSZ(registry, favKey, key, fav->Name))
                break;
            siz = PackSfxSettings(&fav->Settings, buffer, allocated);
            if (siz == -1)
            {
                TRACE_E("chyba v PackSfxSettings.");
                break;
            }
            swprintf_s(key, _countof(key), CONFIG_SFXFAVDATA, i + 1);
            if (!registry->SetValue(favKey, key, REG_BINARY, buffer, siz))
                break;
            swprintf_s(key, _countof(key), CONFIG_SFXFAVSIZE, i + 1);
            if (!registry->SetValue(favKey, key, REG_DWORD, &siz, sizeof(DWORD)))
                break;
        }
        registry->SetValue(favKey, CONFIG_SFXFAVCOUNT, REG_DWORD, &i, sizeof(DWORD));
        registry->CloseKey(favKey);
    }

    if (buffer)
        free(buffer);

    SPLRegistrySetString(registry, regKey, CONFIG_SFXLASTEXPORTPATH,
                         Config.LastExportPath);
    registry->SetValue(regKey, CONFIG_CHLANG, REG_DWORD, &Config.ChangeLangReaction, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_SALVER, REG_DWORD, &Config.CurSalamanderVersion, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_WINZIPNAMES, REG_DWORD, &Config.WinZipNames, sizeof(DWORD));

    registry->SetValue(regKey, CONFIG_LIST_INFO_PACKED_SIZE, REG_DWORD, &Config.ListInfoPackedSize, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_COL_PACKEDSIZE_FIXEDWIDTH, REG_DWORD, &Config.ColumnPackedSizeFixedWidth, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_COL_PACKEDSIZE_WIDTH, REG_DWORD, &Config.ColumnPackedSizeWidth, sizeof(DWORD));
}

void CPluginInterface::Configuration(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Configuration()");

    CConfigDialog dlg(parent, &Config);

    if (dlg.Proceed() == IDOK)
    {
        if (SalamanderGeneral->GetPanelPluginData(PANEL_LEFT) != NULL)
            SalamanderGeneral->PostRefreshPanelPath(PANEL_LEFT);
        if (SalamanderGeneral->GetPanelPluginData(PANEL_RIGHT) != NULL)
            SalamanderGeneral->PostRefreshPanelPath(PANEL_RIGHT);
    }
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    salamander->AddCustomPacker(L"ZIP (Plugin)", L"zip", FALSE);
    salamander->AddCustomUnpacker(L"ZIP (Plugin)", L"*.zip;*.pk3;*.jar",
                                  Config.Version < 2); // before SS 1.6 beta 4 there was no *.jar support -> restore it
    salamander->AddPanelArchiver(L"zip;pk3;jar", TRUE, FALSE);

    /* used by the export_mnu.py script that generates salmenu.mnu for Translator
   keep it in sync with the salamander->AddMenuItem() calls below...
MENU_TEMPLATE_ITEM PluginMenu[] = 
{
	{MNTT_PB, 0
	{MNTT_IT, IDS_MENUCOMMENT
	{MNTT_IT, IDS_MENUCREATESFX
//	{MNTT_IT, IDS_MENUREPAIR
	{MNTT_IT, IDS_MENUTEST
	{MNTT_PE, 0
};
*/

    salamander->AddMenuItem(-1, LangStr(IDS_MENUCOMMENT).c_str(), 0, MID_COMMENT, TRUE, 0, 0,
                            MENU_SKILLLEVEL_INTERMEDIATE | MENU_SKILLLEVEL_ADVANCED);
    salamander->AddMenuItem(-1, LangStr(IDS_MENUCREATESFX).c_str(), 0, MID_CREATESFX, TRUE, 0, 0,
                            MENU_SKILLLEVEL_ALL);
    //salamander->AddMenuItem(LangStr(IDS_MENUREPAIR), 0, MID_REPAIR, TRUE, 0, 0);
    salamander->AddMenuItem(-1, LangStr(IDS_MENUTEST).c_str(), 0, MID_TEST, TRUE, 0, 0, MENU_SKILLLEVEL_ALL);

    if (Config.Version < 2) // before SS 1.6 beta 4
    {
        salamander->AddPanelArchiver(L"jar", TRUE, TRUE); // no JAR entry was present -> add it
    }

    // assign the plugin icon
    HBITMAP hBmp = (HBITMAP)LoadImage(DLLInstance, MAKEINTRESOURCE(IDB_ZIP),
                                      IMAGE_BITMAP, 16, 16, LR_DEFAULTCOLOR);
    salamander->SetBitmapWithIcons(hBmp);
    DeleteObject(hBmp);
    salamander->SetPluginIcon(0);
    salamander->SetPluginMenuAndToolbarIcon(0);
}

void CPluginInterface::ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData)
{
    delete ((CPluginDataInterface*)pluginData);
}

CPluginInterfaceForArchiverAbstract*
CPluginInterface::GetInterfaceForArchiver()
{
    CALL_STACK_MESSAGE_NONE
    return &InterfaceForArchiver;
}

CPluginInterfaceForMenuExtAbstract*
CPluginInterface::GetInterfaceForMenuExt()
{
    CALL_STACK_MESSAGE_NONE
    return &InterfaceForMenuExt;
}

//
// ****************************************************************************
// CPluginInterfaceForMenuExt
//

DWORD
CPluginInterfaceForMenuExt::GetMenuItemState(int id, DWORD eventMask)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForMenuExt::GetMenuItemState(%d, 0x%X)",
                        id, eventMask);
    // must reside on disk or inside our plugin
    if ((eventMask & (MENU_EVENT_DISK | MENU_EVENT_THIS_PLUGIN_ARCH)) == 0)
        return 0;

    // when already inside our archive, everything stays enabled
    if (eventMask & MENU_EVENT_THIS_PLUGIN_ARCH)
        return MENU_ITEM_STATE_ENABLED;

    // use the selected files or the file under focus
    const CFileData* file = NULL;
    BOOL isDir;
    if ((eventMask & MENU_EVENT_FILES_SELECTED) == 0)
    {
        if ((eventMask & MENU_EVENT_FILE_FOCUSED) == 0)
            return 0;
        file = SalamanderGeneral->GetPanelFocusedItem(PANEL_SOURCE, &isDir); // nothing is selected -> enumeration fails
    }

    BOOL ret = TRUE;
    int count = 0;

    int index = 0;
    if (file == NULL)
        file = SalamanderGeneral->GetPanelSelectedItem(PANEL_SOURCE, &index, &isDir);
    while (file != NULL && ret)
    {
        if (SalamanderGeneral->IsArchiveHandledByThisPlugin(file->Name))
            count++;
        else
            ret = FALSE;
        file = SalamanderGeneral->GetPanelSelectedItem(PANEL_SOURCE, &index, &isDir);
    }

    return (ret && count > 0) ? MENU_ITEM_STATE_ENABLED : 0; // all selected files are handled by us
}

BOOL CPluginInterfaceForMenuExt::ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander, HWND parent,
                                                 int id, DWORD eventMask)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForMenuExt::ExecuteMenuItem(, , %d, 0x%X)", id,
                        eventMask);

    std::wstring zipFile;
    size_t archiveOffset = std::wstring::npos;
    BOOL selFiles = FALSE;
    int index = 0;
    BOOL ok = TRUE;

    if (!SPLGetPanelPathOwned(SalamanderGeneral, PANEL_SOURCE, zipFile, NULL, &archiveOffset))
        return FALSE;

    const BOOL panelShowsArchive = archiveOffset != std::wstring::npos;
    std::wstring diskDirectory;
    if (!panelShowsArchive)
    {
        diskDirectory = zipFile;
        selFiles = eventMask & MENU_EVENT_FILES_SELECTED;
    }

    BOOL changesReported = FALSE; // helper flag — TRUE once a path change was already reported
    do
    {
        if (panelShowsArchive)
        {
            zipFile.resize(archiveOffset);
        }
        else
        {
            const CFileData* fileData;
            if (selFiles)
                fileData = SalamanderGeneral->GetPanelSelectedItem(PANEL_SOURCE, &index, NULL);
            else
                fileData = SalamanderGeneral->GetPanelFocusedItem(PANEL_SOURCE, NULL);
            if (!fileData)
                break; // end of enumeration, or an error (for GetFocusedItem)
            zipFile = diskDirectory;
            SPLSalPathAppendOwned(zipFile, fileData->Name);
            DWORD attr = SalamanderGeneral->SalGetFileAttributes(zipFile.c_str());
            if (attr != 0xFFFFFFFF && attr & FILE_ATTRIBUTE_DIRECTORY)
                continue;
        }

        if (!ok && SalamanderGeneral->ShowMessageBox(LangStr(IDS_CONTINUE).c_str(), LangStr(IDS_PLUGINNAME).c_str(),
                                                     MSGBOX_QUESTION) != IDYES)
            return FALSE;
        ok = TRUE;

        switch (id)
        {
        case MID_COMMENT:
        {
            SalamanderGeneral->SetUserWorkedOnPanelPath(PANEL_SOURCE); // treat this command as work on the path (shows up in Alt+F12)

            CZipPack pack(zipFile.c_str(), "", salamander);

            if (pack.ErrorID || pack.CommentArchive())
            {
                if (pack.ErrorID != IDS_NODISPLAY)
                    SalamanderGeneral->ShowMessageBox(LangStr(pack.ErrorID).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                ok = FALSE;
            }
            if (pack.UserBreak)
                ok = FALSE;
            break;
        }

        case MID_CREATESFX:
        {
            SalamanderGeneral->SetUserWorkedOnPanelPath(PANEL_SOURCE); // treat this command as work on the path (shows up in Alt+F12)

            CZipPack pack(zipFile.c_str(), "", salamander);

            if (pack.ErrorID || pack.CreateSFX())
            {
                if (pack.ErrorID != IDS_NODISPLAY)
                    SalamanderGeneral->ShowMessageBox(LangStr(pack.ErrorID).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                ok = FALSE;
            }
            if (pack.UserBreak)
                ok = FALSE;
            break;
        }

            //case MID_REPAIR:break;

        case MID_TEST:
        {
            SalamanderGeneral->SetUserWorkedOnPanelPath(PANEL_SOURCE); // treat this command as work on the path (shows up in Alt+F12)

            CZipUnpack unpack(zipFile.c_str(), "", salamander, NULL);

            unpack.Test = true;
            unpack.AllFilesOK = TRUE;
            if (unpack.ErrorID || unpack.UnpackWholeArchive("*.*", L""))
            {
                if (unpack.ErrorID != IDS_NODISPLAY)
                    SalamanderGeneral->ShowMessageBox(LangStr(unpack.ErrorID).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
                ok = FALSE;
            }
            if (unpack.UserBreak)
                ok = FALSE;
            if (ok)
            {
                const std::wstring text = SPLFormatStringOwned(
                    unpack.AllFilesOK ? LangStr(IDS_TESTOK).c_str() : LangStr(IDS_TESTKO).c_str(),
                    zipFile.c_str());
                SalamanderGeneral->ShowMessageBox(text.c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
            }
            break;
        }
        }

        if (id == MID_COMMENT || id == MID_CREATESFX || id == MID_REPAIR) // when the operation may have modified the path
        {
            if (!changesReported) // path changed and has not been reported yet -> report it
            {
                changesReported = TRUE;
                // notify the path containing the modified PAK files (notification happens after leaving
                // the plugin code — once this method returns)
                std::wstring zipFileDir = zipFile;
                SPLCutDirectoryOwned(SalamanderGeneral, zipFileDir); // must succeed because the file exists
                SalamanderGeneral->PostChangeOnPathNotification(zipFileDir.c_str(), FALSE);
            }
        }
    } while (selFiles); // keep looping until GetSelectedItem returns NULL

    return TRUE;
}

BOOL CPluginInterfaceForMenuExt::HelpForMenuItem(HWND parent, int id)
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForMenuExt::HelpForMenuItem(, %d)", id);
    int helpID = 0;
    switch (id)
    {
    case MID_COMMENT:
        helpID = IDH_COMMENT;
        break;
    case MID_CREATESFX:
        helpID = IDH_CREATESFX;
        break;
    case MID_TEST:
        helpID = IDH_TEST;
        break;
    }
    if (helpID != 0)
        SalamanderGeneral->OpenHtmlHelp(parent, HHCDisplayContext, helpID, FALSE);
    return helpID != 0;
}

//
// ****************************************************************************
// CPluginDataInterface
//

void CPluginDataInterface::ReleasePluginData(CFileData& file, BOOL isDir)
{
    // file.PluginData is NULL for folders not having extra items in the archive - see GetFileDataForUpDir & GetFileDataForNewDir
    delete (CZIPFileData*)file.PluginData; // However, delete NULL is perfectly OK
}

// Callback called by Salamander to obtain custom column text - see spl_com.h / FColumnGetText
// Global variables - pointers to global variables used by Salamander
static const CFileData** TransferFileData = NULL;
static int* TransferIsDir = NULL;
static wchar_t* TransferBuffer = NULL;
static int* TransferLen = NULL;
static DWORD* TransferRowData = NULL;
static CPluginDataInterfaceAbstract** TransferPluginDataIface = NULL;
static DWORD* TransferActCustomData = NULL;

static void WINAPI GetPackedSizeText()
{
    if (*TransferIsDir)
    {
        *TransferLen = 0;
    }
    else
    {
        CZIPFileData* zipFileData = (CZIPFileData*)(*TransferFileData)->PluginData;
        if (zipFileData->PackedSize != 0)
        {
            const std::wstring number = SPLNumberToStrOwned(
                SalamanderGeneral, CQuadWord().SetUI64(zipFileData->PackedSize));
            *TransferLen = static_cast<int>((std::min<size_t>)(number.size(), TRANSFER_BUFFER_MAX));
            wmemcpy(TransferBuffer, number.data(), static_cast<size_t>(*TransferLen));
        }
        else
        {
            *TransferLen = 0;
        }
    }
}

void WINAPI
CPluginDataInterface::SetupView(BOOL leftPanel, CSalamanderViewAbstract* view, const wchar_t* archivePath,
                                const CFileData* upperDir)
{
    view->GetTransferVariables(TransferFileData, TransferIsDir, TransferBuffer, TransferLen, TransferRowData,
                               TransferPluginDataIface, TransferActCustomData);

    // Special columns added only in Detailed mode
    if (view->GetViewMode() == VIEW_MODE_DETAILED)
    {
        CColumn column;
        if (Config.ListInfoPackedSize)
        {
            // We add Packed Size just after the Size column; or at the end in case of failure
            int sizeIndex = view->GetColumnsCount();
            int i;
            for (i = 0; i < sizeIndex; i++)
                if (view->GetColumn(i)->ID == COLUMN_ID_SIZE)
                {
                    sizeIndex = i + 1;
                    break;
                }

            lstrcpyW(column.Name, LoadStrW(IDS_LISTINFO_PAKEDSIZE).c_str());
            lstrcpyW(column.Description, LoadStrW(IDS_LISTINFO_PAKEDSIZE_DESC).c_str());
            column.GetText = GetPackedSizeText;
            column.SupportSorting = 0;
            column.LeftAlignment = 0;
            column.ID = COLUMN_ID_CUSTOM;
            column.CustomData = 0;
            column.Width = leftPanel ? LOWORD(Config.ColumnPackedSizeWidth) : HIWORD(Config.ColumnPackedSizeWidth);
            column.FixedWidth = leftPanel ? LOWORD(Config.ColumnPackedSizeFixedWidth) : HIWORD(Config.ColumnPackedSizeFixedWidth);
            view->InsertColumn(sizeIndex, &column);
        }
    }
}

void CPluginDataInterface::ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth)
{
    if (column->CustomData == 0)
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
    if (column->CustomData == 0)
    {
        if (leftPanel)
            Config.ColumnPackedSizeWidth = MAKELONG(newWidth, HIWORD(Config.ColumnPackedSizeWidth));
        else
            Config.ColumnPackedSizeWidth = MAKELONG(LOWORD(Config.ColumnPackedSizeWidth), newWidth);
    }
}

//
// ****************************************************************************
// CPluginInterfaceForArchiver
//

static BOOL EncodeWideToZipText(const wchar_t* source, std::string& target)
{
    return source != NULL && TryWideToZipText(source, target) ? TRUE : FALSE;
}

BOOL CPluginInterfaceForArchiver::ListArchive(CSalamanderForOperationsAbstract* salamander,
                                              const wchar_t* fileName,
                                              CSalamanderDirectoryAbstract* dir,
                                              CPluginDataInterfaceAbstract*& pluginData)
try
{
    CALL_STACK_MESSAGE2("CPluginInterfaceForArchiver::ListArchive(, %ls, ,)", fileName);

    CZipList list(fileName, salamander);

    BOOL haveFiles;
    if (list.ErrorID || list.ListArchive(dir, haveFiles))
    {
        if (list.ErrorID != IDS_NODISPLAY)
        {
            SalamanderGeneral->ShowMessageBox(LangStr(list.ErrorID).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
        }
        if (!haveFiles)
            return FALSE;
    }
    pluginData = new CPluginDataInterface();
    return TRUE;
}
catch (...)
{
    return FALSE;
}

BOOL CPluginInterfaceForArchiver::UnpackArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                CPluginDataInterfaceAbstract* pluginData, const wchar_t* targetDir,
                                                const wchar_t* archiveRoot, SalEnumSelection next, void* nextParam)
try
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackArchive(, %ls, , %ls, %ls, ,)", fileName, targetDir, archiveRoot);

    std::string archiveRootA;
    if (archiveRoot != NULL && !EncodeWideToZipText(archiveRoot, archiveRootA))
    {
        return FALSE;
    }

    CRT_MEM_CHECKPOINT

    {
        const char* arcRoot = archiveRoot != NULL ? archiveRootA.c_str() : "";
        if (*arcRoot == '\\')
            arcRoot++;
        CZipUnpack unpack(fileName, arcRoot, salamander, NULL);
        if (unpack.ErrorID || unpack.UnpackArchive(targetDir, next, nextParam))
        {
            if (unpack.ErrorID != IDS_NODISPLAY)
                SalamanderGeneral->ShowMessageBox(LangStr(unpack.ErrorID).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            if (unpack.Fatal)
                return TRUE;
            else
                return FALSE;
        }
        if (unpack.UserBreak)
            return FALSE;
    }

    CRT_MEM_DUMP_ALL_OBJECTS_SINCE

    return TRUE;
}
catch (...)
{
    return FALSE;
}

BOOL CPluginInterfaceForArchiver::UnpackOneFile(CSalamanderForOperationsAbstract* salamander,
                                                const wchar_t* fileName, CPluginDataInterfaceAbstract* pluginData,
                                                const wchar_t* nameInArchive, const CFileData* fileData,
                                                const wchar_t* targetDir, const wchar_t* newFileName,
                                                BOOL* renamingNotSupported)
try
{
    CALL_STACK_MESSAGE4("CPluginInterfaceForArchiver::UnpackOneFile(, %ls, , %ls, , %ls, ,)", fileName,
                        nameInArchive, targetDir);

    std::string nameInArchiveA;
    if (!EncodeWideToZipText(nameInArchive, nameInArchiveA))
    {
        return FALSE;
    }

    CRT_MEM_CHECKPOINT

    {
        /*    if (newFileName != NULL)
    {
      *renamingNotSupported = TRUE;
      return FALSE;
    }*/

        CZipUnpack unpack(fileName, "", salamander, NULL);
        if (unpack.ErrorID || unpack.UnpackOneFile(nameInArchiveA.c_str(), fileData, targetDir, newFileName))
        {
            if (unpack.ErrorID != IDS_NODISPLAY)
                SalamanderGeneral->ShowMessageBox(LangStr(unpack.ErrorID).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            return FALSE;
        }
        if (unpack.UserBreak)
            return FALSE;
    }
    /*_CrtMemCheckpoint(&st2);
  _CrtMemDifference(&stdiff, &st1, &st2);
  _CrtMemDumpStatistics(&st1);
  _CrtMemDumpStatistics(&st2);
  _CrtMemDumpStatistics(&stdiff);*/
    CRT_MEM_DUMP_ALL_OBJECTS_SINCE

    return TRUE;
}
catch (...)
{
    return FALSE;
}

BOOL CPluginInterfaceForArchiver::PackToArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                const wchar_t* archiveRoot, BOOL move, const wchar_t* sourcePath,
                                                SalEnumSelection2 next, void* nextParam)
try
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForArchiver::PackToArchive(, %ls, %ls, %d, %ls, ,)", fileName,
                        archiveRoot, move, sourcePath);

    std::string archiveRootA;
    if (archiveRoot != NULL && !EncodeWideToZipText(archiveRoot, archiveRootA))
    {
        return FALSE;
    }

    CRT_MEM_CHECKPOINT

    {
        const char* arcRoot = archiveRoot != NULL ? archiveRootA.c_str() : "";
        if (*arcRoot == '\\')
            arcRoot++;
        CZipPack pack(fileName, arcRoot, salamander);

        if (pack.ErrorID || pack.PackToArchive(move, sourcePath, next, nextParam))
        {
            if (pack.ErrorID != IDS_NODISPLAY)
                SalamanderGeneral->ShowMessageBox(LangStr(pack.ErrorID).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            if (!pack.RecoverOK)
                SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERRRECOVER).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            if (pack.Fatal)
                return TRUE;
            else
                return FALSE;
        }
        if (pack.UserBreak)
            return FALSE;
    }

    CRT_MEM_DUMP_ALL_OBJECTS_SINCE

    return TRUE;
}
catch (...)
{
    return FALSE;
}

BOOL CPluginInterfaceForArchiver::DeleteFromArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                    CPluginDataInterfaceAbstract* pluginData, const wchar_t* archiveRoot,
                                                    SalEnumSelection next, void* nextParam)
try
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForArchiver::DeleteFromArchive(, %ls, , %ls, ,)", fileName, archiveRoot);

    std::string archiveRootA;
    if (archiveRoot != NULL && !EncodeWideToZipText(archiveRoot, archiveRootA))
    {
        return FALSE;
    }

    CRT_MEM_CHECKPOINT

    {
        const char* arcRoot = archiveRoot != NULL ? archiveRootA.c_str() : "";
        if (*arcRoot == '\\')
            arcRoot++;
        CZipPack pack(fileName, arcRoot, salamander);

        if (pack.ErrorID || pack.DeleteFromArchive(next, nextParam))
        {
            if (pack.ErrorID != IDS_NODISPLAY)
                SalamanderGeneral->ShowMessageBox(LangStr(pack.ErrorID).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            if (!pack.RecoverOK)
                SalamanderGeneral->ShowMessageBox(LangStr(IDS_ERRRECOVER).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);

            if (pack.Fatal)
                return TRUE;
            else
                return FALSE;
        }
        if (pack.UserBreak)
            return FALSE;
    }

    CRT_MEM_DUMP_ALL_OBJECTS_SINCE

    return TRUE;
}
catch (...)
{
    return FALSE;
}

BOOL CPluginInterfaceForArchiver::UnpackWholeArchive(CSalamanderForOperationsAbstract* salamander, const wchar_t* fileName,
                                                     const wchar_t* mask, const wchar_t* targetDir, BOOL delArchiveWhenDone,
                                                     CDynamicString* archiveVolumes)
try
{
    CALL_STACK_MESSAGE5("CPluginInterfaceForArchiver::UnpackWholeArchive(, %ls, %ls, %ls, %d,)",
                        fileName, mask, targetDir, delArchiveWhenDone);

    std::string maskA;
    if (!EncodeWideToZipText(mask, maskA))
    {
        return FALSE;
    }

    CRT_MEM_CHECKPOINT

    {
        std::vector<std::wstring> arcVolumes;
        CZipUnpack unpack(fileName, "", salamander, delArchiveWhenDone ? &arcVolumes : NULL);
        if (unpack.ErrorID || unpack.UnpackWholeArchive(maskA.c_str(), targetDir))
        {
            if (unpack.ErrorID != IDS_NODISPLAY)
                SalamanderGeneral->ShowMessageBox(LangStr(unpack.ErrorID).c_str(), LangStr(IDS_PLUGINNAME).c_str(), MSGBOX_ERROR);
            /*    // Petr: I really do not know what this was for... I have to disable it now, otherwise a fatal error with 'delArchiveWhenDone'==TRUE would delete the archive, which is unacceptable
      if (unpack.Fatal)
        return TRUE;
      else
*/
            return FALSE;
        }
        if (unpack.UserBreak)
            return FALSE;
        if (delArchiveWhenDone) // move unique volumes from 'arcVolumes' to 'archiveVolumes' (arcVolumes may contain duplicates)
        {
            std::sort(arcVolumes.begin(), arcVolumes.end());
            std::wstring lastVolume;
            for (const std::wstring& volume : arcVolumes)
            {
                if (lastVolume.empty() || lastVolume != volume)
                {
                    lastVolume = volume;
                    archiveVolumes->Add(volume.c_str(), -2);
                }
            }
        }
    }

    CRT_MEM_DUMP_ALL_OBJECTS_SINCE

    return TRUE;
}
catch (...)
{
    return FALSE;
}
