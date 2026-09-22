// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include "ui/IPrompter.h"
#include "ui/UnicodeHistoryUtils.h"
#include "menu_item_text.h"
#include "window_placement_policy.h"
#include "common/DiagnosticTextEncoding.h"
#include "common/unicode/helpers.h"
#include "common/IEnvironment.h"
#include "common/IRegistry.h"
#include "common/fsutil.h"
#include <shlwapi.h>
#undef PathIsPrefix // otherwise, collision with CSalamanderGeneral::PathIsPrefix

#include "toolbar.h"
#include "stswnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "editwnd.h"
#include "mainwnd.h"
#include "cfgdlg.h"
#include "usermenu.h"
#include "viewer.h"
#include "zip.h"
#include "pack.h"
#include "find.h"
#include "dialogs.h"
#include "logo.h"
#include "common/widepath.h"
#include "tasklist.h"
#include "pwdmngr.h"
#include "darkmode.h"

//
// ConfigVersion - version number of the loaded configuration
//
// 0 = no configuration found - default values are used
// 1 = version 1.52 and older
// 2 = 1.6b1
// 3 = 1.6b1.x
// 4 = 1.6b3.x
// 5 = 1.6b3.x          needed for proper conversion of packer configuration between our versions
// 6 = 1.6b4.x
// 7 = 1.6b5.x          needed for proper conversion of supported plug-in functions (see CPlugins::Load)
// 8 = 1.6b5.x          better overlook ;-)
// 9 = 1.6b5.x          because of switching from exe name to variable for custom packers
// 10 = 1.6b6           due to renaming "XXX (Internal)" to "XXX (Plug-in)" in Pack and Unpack dialogs
//                      and to set the ANSI version of the "list of files" for ACE32 and PKZIP25 (un)packers
// 11 = 1.6b7           added CheckVer plug-in - ensure automatic installation
// 12 = 2.0             auto-close salopen.exe + added PEViewer plug-in - ensure automatic installation
// 13 = 2.5b1           added missing conversion of custom packer configuration - reflect change for LHA
// 14 = 2.5b1           new Advanced Options in Find dialog. Switched to CFilterCriteria. Converted inverse filter mask.
// 15 = 2.5b2           newer version so that plug-ins load (upgrade registry records)
// 16 = 2.5b2           added coloring of Encrypted files and folders (added when loading configuration + included in default configuration)
// 17 = 2.5b2           added *.xml mask to internal viewer settings - "force text mode"
// 18 = 2.5b3           only to transfer plug-in configuration from version 2.5b2
// 19 = 2.5b4           only to transfer plug-in configuration from version 2.5b3
// 20 = 2.5b5           only to transfer plug-in configuration from version 2.5b4
// 21 = 2.5b6           only to transfer plug-in configuration from version 2.5b5(a)
// 22 = 2.5b6           panel filters - unified into one history
// 23 = 2.5b6           new panel view (Tiles)
// 24 = 2.5b7           only to transfer plug-in configuration from version 2.5b6
// 25 = 2.5b7           plugins: show in plugin bar -> transfer variable into CPluginData
// 26 = 2.5b8           only to transfer plug-in configuration from version 2.5b7
// 27 = 2.5b9           only to transfer plug-in configuration from version 2.5b8
// 28 = 2.5b9           new color scheme from old DOS Navigator -> convert 'scheme'
// 29 = 2.5b10          only to transfer plug-in configuration from version 2.5b9
// 30 = 2.5b11          only to transfer plug-in configuration from version 2.5b10
// 31 = 2.5b11          introduced Floppy section in Drives configuration - need to force reading icons for Removable
// 32 = 2.5b11          Find: "Local Settings\\Temporary Internet Files" is searched by default
// 33 = 2.5b12          only to transfer plug-in configuration from version 2.5b11
// 34 = 2.5b12          modification of external packer/unpacker PKZIP25 (external Win32 version)
// 35 = 2.5RC1          only to transfer plug-in configuration from version 2.5b12 (internal only, we shipped RC1 instead)
// 36 = 2.5RC2          only to transfer plug-in configuration from version 2.5RC1
// 37 = 2.5RC3          only to transfer plug-in configuration from version 2.5RC2
// 38 = 2.5RC3          renamed Servant Salamander to Altap Salamander
// 39 = 2.5             only to transfer plug-in configuration from version 2.5RC3
// 40 = 2.51            only to transfer plug-in configuration from version 2.5
// 41 = 2.51            configuration version containing a list of disabled icon overlay handlers (see CONFIG_DISABLEDCUSTICOVRLS_REG)
// 42 = 2.52b1          only to transfer plug-in configuration from version 2.51
// 43 = 2.52b2          only to transfer plug-in configuration from version 2.52 beta 1
// 44 = 2.52b2          changed viewer, editor and archiver extensions to lowercase (uppercase extensions are obsolete in Windows)
// 45 = 2.52b2          introduced password manager, forced FTP client load so it registers to use the Password Manager, see SetPluginUsesPasswordManager
// 46 = 2.52 (DB30)     only to transfer plug-in configuration from version 2.52 beta 2
// 47 = 2.52 (IB31)     support for Sal/Env variables like $(SalDir) or $[USERPROFILE] in hot paths; need to escape old hot paths
// 48 = 2.52            only to transfer plug-in configuration from version 2.52 (DB30)
// 49 = 2.53b1 (DB33)   only to transfer plug-in configuration from version 2.52
// 50 = 2.53b1 (DB36)   only to transfer plug-in configuration from version 2.53b1 (DB33)
// 51 = 2.53b1 (PB38)   only to transfer plug-in configuration from version 2.53b1 (DB36)
// 52 = 2.53b1 (DB39)   only to transfer plug-in configuration from version 2.53b1 (PB38)
// 53 = 2.53b1 (DB41)   only to transfer plug-in configuration from version 2.53b1 (DB39)
// 54 = 2.53b1 (PB44)   only to transfer plug-in configuration from version 2.53b1 (DB41)
// 55 = 2.53b1 (DB46)   only to transfer plug-in configuration from version 2.53b1 (PB44)
// 56 = 2.53b1          only to transfer plug-in configuration from version 2.53b1 (DB46)
// 57 = 2.53 (DB52)     only to transfer plug-in configuration from version 2.53b1
// 58 = 2.53b2 (IB55)   only to transfer plug-in configuration from version 2.53 (DB52)
// 59 = 2.53b2          only to transfer plug-in configuration from version 2.53b2 (IB55)
// 60 = 2.53 (DB60)     only to transfer plug-in configuration from version 2.53b2
// 61 = 2.53            only to transfer plug-in configuration from version 2.53 (DB60)
// 62 = 2.54b1 (DB66)   only to transfer plug-in configuration from version 2.53
// 63 = 2.54            only to transfer plug-in configuration from version 2.54b1 (DB66)
// 64 = 2.55b1 (DB72)   only to transfer plug-in configuration from version 2.54
// 65 = 2.55b1 (DB72)   external archivers: identify by UID instead of Title (translated according to language version, so cannot be used for identification) - switching language caused external archiver paths to be lost
// 66 = 3.00b1 (PB75)   only to transfer plug-in configuration from version 2.55b1 (DB72)
// 67 = 3.00b1 (DB76)   only to transfer plug-in configuration from version 3.00b1 (PB75)
// 68 = 3.00b1 (PB79)   only to transfer plug-in configuration from version 3.00b1 (DB76)
// 69 = 3.00b1 (DB80)   only to transfer plug-in configuration from version 3.00b1 (PB79)
// 70 = 3.00b1 (DB83)   only to transfer plug-in configuration from version 3.00b1 (DB80)
// 71 = 3.00b1 (PB87)   only to transfer plug-in configuration from version 3.00b1 (DB83)
// 72 = 3.00b1 (DB88)   only to transfer plug-in configuration from version 3.00b1 (PB87)
// 73 = 3.00b1          only to transfer plug-in configuration from version 3.00b1 (DB88)
// 74 = 3.00b2 (DB94)   only to transfer plug-in configuration from version 3.00b1
// 75 = 3.00b2          only to transfer plug-in configuration from version 3.00b2 (DB94)
// 76 = 3.00b3 (DB100)  only to transfer plug-in configuration from version 3.00b2
// 77 = 3.00b3 (PB103)  only to transfer plug-in configuration from version 3.00b3 (DB100)
// 78 = 3.00b3 (DB105)  only to transfer plug-in configuration from version 3.00b3 (PB103)
// 79 = 3.00b3          only to transfer plug-in configuration from version 3.00b3 (DB105)
// 80 = 3.00b4 (DB111)  only to transfer plug-in configuration from version 3.00b3
// 81 = 3.00b4 (DB111)  RAR 5.0 needs a new switch on the command line because of file list encoding
// 82 = 3.00b4          only to transfer plug-in configuration from version 3.00b4 (DB111)
// 83 = 3.00b5 (DB117)  only to transfer plug-in configuration from version 3.00b4
// 84 = 3.0             only to transfer plug-in configuration from version 3.00b5 (DB117)
// 85 = 3.10b1 (DB123)  only to transfer plug-in configuration from version 3.0
// 86 = 3.01            only to transfer plug-in configuration from version 3.10b1 (DB123)
// 87 = 3.10b1 (DB129)  only to transfer plug-in configuration from version 3.01
// 88 = 3.02            only to transfer plug-in configuration from version 3.10b1 (DB129)
// 89 = 3.10b1 (DB135)  only to transfer plug-in configuration from version 3.02
// 90 = 3.03            only to transfer plug-in configuration from version 3.10b1 (DB135)
// 91 = 3.10b1 (DB141)  only to transfer plug-in configuration from version 3.03
// 92 = 3.04            only to transfer plug-in configuration from version 3.10b1 (DB141)
// 93 = 3.10b1 (DB147)  only to transfer plug-in configuration from version 3.04
// 94 = 3.05            only to transfer plug-in configuration from version 3.10b1 (DB147)
// 95 = 3.10b1 (DB153)  only to transfer plug-in configuration from version 3.05
// 96 = 3.06            only to transfer plug-in configuration from version 3.10b1 (DB153)
// 97 = 3.10b1 (DB159)  only to transfer plug-in configuration from version 3.06
// 98 = 3.10b1 (DB162)  only to transfer plug-in configuration from version 3.10b1 (DB159)
// 99 = 3.07            only to transfer plug-in configuration from version 3.10b1 (DB162)
// 100 = 4.00b1 (DB168) only to transfer plug-in configuration from version 3.07
// 101 = 3.08           only to transfer plug-in configuration from version 4.00b1 (DB168) - by mistake 3.08 and DB171 share the same version number 101, sorry, I will be more careful next time
// 101 = 4.00b1 (DB171) only to transfer plug-in configuration from version 4.00b1 (DB168); this is the last VC2008 build, later versions are VC2019
// 102 = 4.00b1 (DB177) only to transfer plug-in configuration from version 4.00b1 (DB171)
// 103 = 4.00           only to transfer plug-in configuration from version 4.00b1 (DB177)
// 104 = 5.00           only to transfer plug-in configuration from version 4.00, first Open Salamander release
// 105 = 5.04           force plugin auto-install after config import from 4.00
//
// When increasing configuration version, add one to THIS_CONFIG_VERSION
//
// When upgrading to a new program version, THIS_CONFIG_VERSION must be incremented by 1
// so that new plug-ins are auto-installed and the plugins.ver counter resets.
//

const DWORD THIS_CONFIG_VERSION = 106; // 106 = plugins renamed from .spl to .dll

// Configuration roots for individual Open Salamander versions.
// The root of the current (youngest) configuration is at index 0.
// Then follow other roots towards older versions of the program.
// The last index contains NULL and serves as a terminator when working with the array.
// When creating a new configuration version (should be stored separately in the registry from the previous one)
// simply insert the path at index 0.

// !!! Keep the corresponding lines in SalamanderConfigurationVersions up to date
const wchar_t* SalamanderConfigurationRoots[SALCFG_ROOTS_COUNT + 1] =
    {
        SAL_REG_CONFIGURATION_ROOTS
};
const wchar_t* SalamanderConfigurationVersions[SALCFG_ROOTS_COUNT] =
    {
        SAL_REG_CONFIGURATION_VERSIONS
};

const wchar_t* SALAMANDER_ROOT_REG = NULL; // will be set in salamander_entry_lifecycle.cpp

// Publishes the configuration root Sally actually resolved into this process's environment,
// so in-process plugins can read the SAME key the core reads.
//
// Plugins get their dark-mode setting from plugins/shared/plugindarkmode.cpp, which used to
// hardcode "Software\Sally\1.0" and the Open Salamander 5.0 root. A Debug build runs on
// "Software\Sally\1.0 Debug", so the core read "Theme mode" from one key and every plugin
// dialog read it from another: switch the core to light and the FTP and Welcome dialogs stayed
// black, across restarts, because the two keys genuinely disagreed. Any non-default root
// (Debug, Preview, an imported config) has the same split.
//
// An environment variable rather than a new plugin API on purpose: plugins live in this
// process, it needs no ABI change and no lockstep rebuild, and a plugin running without Sally
// simply does not see it and falls back to its old behaviour.
void PublishConfigRootToEnvironment()
{
    // NULL is meaningful (UPGRADE abort: "do not write configuration"), so clear it rather
    // than leaving a stale value that would outlive the reason it was set.
    //
    // [merge:main->unicode] W, not A: SALAMANDER_ROOT_REG is const wchar_t* on this branch, so
    // main's SetEnvironmentVariableA would not compile. The A-form macro deliberately survives
    // in registry_names.h because plugins/shared/plugindarkmode.cpp still READS it via
    // GetEnvironmentVariableA - see the comment there for why that round-trip is exact.
    SetEnvironmentVariableW(SAL_ENV_CONFIG_ROOT_W, SALAMANDER_ROOT_REG);
}

const wchar_t* SALAMANDER_SAVE_IN_PROGRESS = L"Save In Progress"; // value exists only during configuration save (detects interrupted saves -> corrupted configuration)
BOOL IsSetSALAMANDER_SAVE_IN_PROGRESS = FALSE;                // TRUE = the registry contains SALAMANDER_SAVE_IN_PROGRESS (detect interrupted configuration saving)

const wchar_t* FINDDIALOG_WINDOW_REG = L"Find Dialog Window";
const wchar_t* SALAMANDER_WINDOW_REG = L"Window";
const wchar_t* WINDOW_LEFT_REG = L"Left";
const wchar_t* WINDOW_RIGHT_REG = L"Right";
const wchar_t* WINDOW_TOP_REG = L"Top";
const wchar_t* WINDOW_BOTTOM_REG = L"Bottom";
const wchar_t* WINDOW_SPLIT_REG = L"Split Position";
const wchar_t* WINDOW_BEFOREZOOMSPLIT_REG = L"Before Zoom Split Position";
const wchar_t* WINDOW_SHOW_REG = L"Show";
const wchar_t* FINDDIALOG_NAMEWIDTH_REG = L"Name Width";

const wchar_t* SALAMANDER_LEFTP_REG = L"Left Panel";
const wchar_t* SALAMANDER_RIGHTP_REG = L"Right Panel";

// #97: collect monitor work areas and clamp a normal-window rect to a sane, visible size
// before it is applied - so a stale/off-screen saved rect (e.g. after resume-from-sleep
// monitor reattach at high DPI) cannot collapse the window to a sliver.
static BOOL CALLBACK CollectMonitorWorkAreaProc(HMONITOR hMon, HDC, LPRECT, LPARAM lParam)
{
    std::vector<RECT>* mons = reinterpret_cast<std::vector<RECT>*>(lParam);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfo(hMon, &mi))
        mons->push_back(mi.rcWork);
    return TRUE;
}

RECT SanitizeMainWindowNormalRect(RECT rect)
{
    std::vector<RECT> mons;
    EnumDisplayMonitors(NULL, NULL, CollectMonitorWorkAreaProc, reinterpret_cast<LPARAM>(&mons));
    return SanitizeWindowRect(rect, mons.data(), (int)mons.size(), 400, 300);
}

// Wide twins for every value name used in a REG_DWORD call.
// Emitted beside the ANSI constants rather than replacing them, so any other
// use of the narrow name still compiles - that is the check that this pass
// reached only where it was meant to. The ANSI set is retired later.
const wchar_t* CONFIG_ALWAYSONTOP_REG_W = L"Always On Top";
const wchar_t* CONFIG_ASYNCCOPYALG_REG_W = L"Async Copy Alg On Network";
const wchar_t* CONFIG_BOTTOMTOOLBARVISIBLE_REG_W = L"Show Bottom ToolBar";
const wchar_t* CONFIG_CHD_SHOWANOTHER_W = L"Change Drive Show Another";
const wchar_t* CONFIG_CHD_SHOWCLOUDSTOR_W = L"Change Drive Show Cloud Storages";
const wchar_t* CONFIG_CHD_SHOWMYDOC_W = L"Change Drive Show My Documents";
const wchar_t* CONFIG_CHD_SHOWNET_W = L"Change Drive Network";
const wchar_t* CONFIG_CLEARREADONLY_REG_W = L"Clear Readonly Attribute";
const wchar_t* CONFIG_CLICKQUICKRENAME_REG_W = L"Click to Quick Rename";
const wchar_t* CONFIG_CLOSESHELL_REG_W = L"Close Shell Window";
const wchar_t* CONFIG_CMDLFOCUS_REG_W = L"Command Line Focused";
const wchar_t* CONFIG_CMDLINE_REG_W = L"Command Line";
const wchar_t* CONFIG_COMMANDSHELL_KIND_REG_W = L"Command Shell Kind";
const wchar_t* CONFIG_COMPAREBYATTR_REG_W = L"Compare By Attr";
const wchar_t* CONFIG_COMPAREBYCONTENT_REG_W = L"Compare By Content";
const wchar_t* CONFIG_COMPAREBYSIZE_REG_W = L"Compare By Size";
const wchar_t* CONFIG_COMPAREBYSUBDIRSATTR_REG_W = L"Compare By Subdirs Attr";
const wchar_t* CONFIG_COMPAREBYSUBDIRS_REG_W = L"Compare By Subdirs";
const wchar_t* CONFIG_COMPAREBYTIME_REG_W = L"Compare By Time";
const wchar_t* CONFIG_COMPAREIGNOREDIRS_REG_W = L"Compare Ignore Dirs";
const wchar_t* CONFIG_COMPAREIGNOREFILES_REG_W = L"Compare Ignore Files";
const wchar_t* CONFIG_COMPAREMOREOPTIONS_REG_W = L"Compare More Options";
const wchar_t* CONFIG_COMPAREONEPANELDIRS_REG_W = L"Compare One Panel Dirs";
const wchar_t* CONFIG_CONFIGURATION_HEIGHT_W = L"Configuration Height";
const wchar_t* CONFIG_COPYFINDTEXT_REG_W = L"Copy Find Text";
const wchar_t* CONFIG_DRAGDROPMINTIME_W = L"DragDrop Min Time";
const wchar_t* CONFIG_DRIVEBAR2VISIBLE_REG_W = L"Show Drive Bar2";
const wchar_t* CONFIG_DRIVEBARBREAK_REG_W = L"Drive Bar Break";
const wchar_t* CONFIG_DRIVEBARINDEX_REG_W = L"Drive Bar Index";
const wchar_t* CONFIG_DRIVEBARVISIBLE_REG_W = L"Show Drive Bar";
const wchar_t* CONFIG_DRIVEBARWIDTH_REG_W = L"Drive Bar Width";
const wchar_t* CONFIG_EDITNEWFILE_USEDEFAULT_REG_W = L"Edit New File Use Default";
const wchar_t* CONFIG_EDITNEW_SELALL_REG_W = L"Edit New File Select All";
const wchar_t* CONFIG_ENABLECMDLINEHISTORY_REG_W = L"Enable CmdLine History";
const wchar_t* CONFIG_ENABLECUSTICOVRLS_REG_W = L"Enable Custom Icon Overlays";
const wchar_t* CONFIG_EXPLORERLOOK_REG_W = L"Explorer Look";
const wchar_t* CONFIG_FILELISTAPPEND_REG_W = L"Make File List Append";
const wchar_t* CONFIG_FILELISTDESTINATION_REG_W = L"Make File List Destination";
const wchar_t* CONFIG_FILENAMEFORMAT_REG_W = L"File Name Format";
const wchar_t* CONFIG_FINDFILETYPEMODE_REG_W = L"Find Files Type Filter";
const wchar_t* CONFIG_FINDFULLROW_REG_W = L"Show Full Row In Find Files";
const wchar_t* CONFIG_FULLROWHIGHLIGHT_REG_W = L"Full Row Highlight";
const wchar_t* CONFIG_FULLROWSELECT_REG_W = L"Full Row Select";
const wchar_t* CONFIG_GRIPSVISIBLE_REG_W = L"Grips Visible";
const wchar_t* CONFIG_HOTPATHSBARVISIBLE_REG_W = L"Hot Paths Bar";
const wchar_t* CONFIG_HOTPATHSBREAK_REG_W = L"Hot Paths Break";
const wchar_t* CONFIG_HOTPATHSINDEX_REG_W = L"Hot Paths Index";
const wchar_t* CONFIG_HOTPATHSWIDTH_REG_W = L"Hot Paths Width";
const wchar_t* CONFIG_HOTPATH_AUTOCONFIG_W = L"Auto Configurate Hot Paths";
const wchar_t* CONFIG_IFPATHISINACCESSIBLEGOTOISMYDOCS_REG_W = L"If Path Is Inaccessible Go To My Docs";
const wchar_t* CONFIG_IGNOREDSTSHIFTS_W = L"Ignore DST Shifts";
const wchar_t* CONFIG_KEEPPLUGINSSORTED_REG_W = L"Keep Plugins Sorted";
const wchar_t* CONFIG_LANGUAGECHANGED_REG_W = L"Language Changed";
const wchar_t* CONFIG_LASTFOCUSEDPAGE_W = L"Last Focused Page";
const wchar_t* CONFIG_LASTPLUGINVER_W = L"Plugins.ver Version (x64)";
const wchar_t* CONFIG_LASTPLUGINVER_OP_W = L"Plugins.ver Version (x86)";
const wchar_t* CONFIG_LASTUSEDSPEEDLIM_REG_W = L"Speed Limit";
const wchar_t* CONFIG_MAINWINDOWICONINDEX_REG_W = L"Main window icon index";
const wchar_t* CONFIG_MENUBREAK_REG_W = L"Menu Break";
const wchar_t* CONFIG_MENUINDEX_REG_W = L"Menu Index";
const wchar_t* CONFIG_MENUWIDTH_REG_W = L"Menu Width";
const wchar_t* CONFIG_MIDDLETOOLBARVISIBLE_REG_W = L"Show Middle ToolBar";
const wchar_t* CONFIG_MINBEEPWHENDONE_REG_W = L"Use Speeker Beep";
const wchar_t* CONFIG_NETWAREFASTDIRMOVE_REG_W = L"Netware Fast Dir Move";
const wchar_t* CONFIG_NOTHIDDENSYSTEM_REG_W = L"Hide Hidden and System Files and Directories";
const wchar_t* CONFIG_ONLYONEINSTANCE_REG_W = L"Only One Instance";
const wchar_t* CONFIG_PACKEPAND_W = L"Packers And Unpackers Expanded";
const wchar_t* CONFIG_PLGTOOLBARVISIBLE_REG_W = L"Show Plugins Bar";
const wchar_t* CONFIG_PLUGINSBARBREAK_REG_W = L"PluginsBar Break";
const wchar_t* CONFIG_PLUGINSBARINDEX_REG_W = L"PluginsBar Index";
const wchar_t* CONFIG_PLUGINSBARWIDTH_REG_W = L"PluginsBar Width";
const wchar_t* CONFIG_PRIMARYCONTEXTMENU_REG_W = L"Primary Context Menu";
const wchar_t* CONFIG_QUICKRENAME_SELALL_REG_W = L"Quick Rename Select All";
const wchar_t* CONFIG_QUICKSEARCHENTER_REG_W = L"Quick Search Enter Alt";
const wchar_t* CONFIG_RECYCLEBIN_REG_W = L"Use Recycle Bin";
const wchar_t* CONFIG_RELOAD_ENV_VARS_REG_W = L"Reload Environment Variables";
const wchar_t* CONFIG_RIGHT_FOCUS_REG_W = L"Right Panel Focused";
const wchar_t* CONFIG_SAVECMDLINEHISTORY_REG_W = L"Save CmdLine History";
const wchar_t* CONFIG_SAVEHISTORY_REG_W = L"Save History";
const wchar_t* CONFIG_SAVEONEXIT_REG_W = L"Save Configuration On Exit";
const wchar_t* CONFIG_SAVEWORKDIRS_REG_W = L"Save Working Dirs";
const wchar_t* CONFIG_SEARCHFILECONTENT_W = L"Search File Content";
const wchar_t* CONFIG_SELECTION_REG_W = L"Select/Deselect Directories";
const wchar_t* CONFIG_SEPARATEDDRIVES_REG_W = L"Separated Drives";
const wchar_t* CONFIG_SHIFTFORHOTPATHS_REG_W = L"Use Shift For GoTo HotPath";
const wchar_t* CONFIG_SHOWGREPERRORS_REG_W = L"Show Errors In Find Files";
const wchar_t* CONFIG_SHOWPANELCAPTION_REG_W = L"Show Panel Caption";
const wchar_t* CONFIG_SHOWPANELZOOM_REG_W = L"Show Panel Zoom";
const wchar_t* CONFIG_SHOWSLGINCOMPLETE_REG_W = L"Show Translation Is Incomplete";
const wchar_t* CONFIG_SHOWSPLASHSCREEN_REG_W = L"Show Splash Screen";
const wchar_t* CONFIG_SINGLECLICK_REG_W = L"Single Click";
const wchar_t* CONFIG_SIZEFORMAT_REG_W = L"Size Format";
const wchar_t* CONFIG_SKILLLEVEL_REG_W = L"Skill Level";
const wchar_t* CONFIG_SORTDETECTNUMBERS_REG_W = L"Sort Detects Numbers";
const wchar_t* CONFIG_SORTDIRSBYEXT_REG_W = L"Sort Dirs By Ext";
const wchar_t* CONFIG_SORTDIRSBYNAME_REG_W = L"Sort Dirs By Name";
const wchar_t* CONFIG_SORTNEWERONTOP_REG_W = L"Sort Newer On Top";
const wchar_t* CONFIG_SORTUSESLOCALE_REG_W = L"Sort Uses Locale";
const wchar_t* CONFIG_STATUSAREA_REG_W = L"Status Area";
const wchar_t* CONFIG_THEME_MODE_REG_W = L"Theme mode";
const wchar_t* CONFIG_THUMBNAILSIZE_REG_W = L"Thumbnail Size";
const wchar_t* CONFIG_TIMERESOLUTION_W = L"Time Resolution";
const wchar_t* CONFIG_TITLEBARMODE_REG_W = L"Title bar mode";
const wchar_t* CONFIG_TITLEBARPREFIX_REG_W = L"Title bar prefix";
const wchar_t* CONFIG_TITLEBARSHOWPATH_REG_W = L"Title bar show path";
const wchar_t* CONFIG_TOOLBARBREAK_REG_W = L"ToolBar Break";
const wchar_t* CONFIG_TOOLBARINDEX_REG_W = L"ToolBar Index";
const wchar_t* CONFIG_TOOLBARWIDTH_REG_W = L"ToolBar Width";
const wchar_t* CONFIG_TOPTOOLBARVISIBLE_REG_W = L"Show Top ToolBar";
const wchar_t* CONFIG_USEALTLANGFORPLUGINS_REG_W = L"Use Alternate Language for Plugins";
const wchar_t* CONFIG_USECUSTOMPANELFONT_REG_W = L"Use Custom Panel Font";
const wchar_t* CONFIG_USEDRAGDROPMINTIME_W = L"Use DragDrop Min Time";
const wchar_t* CONFIG_USEICONTINCTURE_REG_W = L"Use Icon Tincture";
const wchar_t* CONFIG_USERMENUBREAK_REG_W = L"User Menu Break";
const wchar_t* CONFIG_USERMENUINDEX_REG_W = L"User Menu Index";
const wchar_t* CONFIG_USERMENULABELS_REG_W = L"User Menu Labels";
const wchar_t* CONFIG_USERMENUTOOLBARVISIBLE_REG_W = L"Show User Menu ToolBar";
const wchar_t* CONFIG_USERMENUWIDTH_REG_W = L"User Menu Width";
const wchar_t* CONFIG_USETIMERESOLUTION_W = L"Use Time Resolution";
const wchar_t* CONFIG_VIEWANDEDITEXPAND_W = L"Viewers And Editors Expanded";
const wchar_t* CONFIG_VISIBLEDRIVES_REG_W = L"Visible Drives";
const wchar_t* FINDDIALOG_NAMEWIDTH_REG_W = L"Name Width";
const wchar_t* PANEL_FILTER_INVERSE_W = L"Inverse Filter";
const wchar_t* SALAMANDER_CLRSCHEME_REG_W = L"Color Scheme";
const wchar_t* SALAMANDER_SIMPLEICONSINARCHIVES_W = L"Simple Icons In Archives";
const wchar_t* SALAMANDER_VERSIONREG_REG_W = SAL_REG_SUBKEY_CONFIGURATION_W;
const wchar_t* VIEWER_AUTOCOPYSELECTION_REG_W = L"Auto-Copy Selection";
const wchar_t* VIEWER_CONFIGCRLF_REG_W = L"EOL CRLF";
const wchar_t* VIEWER_CONFIGCR_REG_W = L"EOL CR";
const wchar_t* VIEWER_CONFIGDEFMODE_REG_W = L"Default Mode";
const wchar_t* VIEWER_CONFIGLF_REG_W = L"EOL LF";
const wchar_t* VIEWER_CONFIGNULL_REG_W = L"EOL NULL";
const wchar_t* VIEWER_CONFIGSAVEWINPOS_REG_W = L"Save Window Position";
const wchar_t* VIEWER_CONFIGTABSIZE_REG_W = L"Tabelator Size";
const wchar_t* VIEWER_CONFIGUSECUSTOMFONT_REG_W = L"Viewer Use Custom Font";
const wchar_t* VIEWER_CONFIGWNDBOTTOM_REG_W = L"Bottom";
const wchar_t* VIEWER_CONFIGWNDLEFT_REG_W = L"Left";
const wchar_t* VIEWER_CONFIGWNDRIGHT_REG_W = L"Right";
const wchar_t* VIEWER_CONFIGWNDSHOW_REG_W = L"Show";
const wchar_t* VIEWER_CONFIGWNDTOP_REG_W = L"Top";
const wchar_t* VIEWER_CPAUTOSELECT_REG_W = L"Auto-Select";
const wchar_t* VIEWER_FINDCASESENSITIVE_REG_W = L"Case Sensitive";
const wchar_t* VIEWER_FINDFORWARD_REG_W = L"Forward Direction";
const wchar_t* VIEWER_FINDHEXMODE_REG_W = L"HEX-mode";
const wchar_t* VIEWER_FINDREGEXP_REG_W = L"Regular Expression";
const wchar_t* VIEWER_FINDWHOLEWORDS_REG_W = L"Whole Words";
const wchar_t* VIEWER_GOTOOFFSETISHEX_REG_W = L"Go to Offset Is Hex";
const wchar_t* VIEWER_WRAPTEXT_REG_W = L"Wrap Text";
const wchar_t* WINDOW_BOTTOM_REG_W = L"Bottom";
const wchar_t* WINDOW_LEFT_REG_W = L"Left";
const wchar_t* WINDOW_RIGHT_REG_W = L"Right";
const wchar_t* WINDOW_SHOW_REG_W = L"Show";
const wchar_t* WINDOW_TOP_REG_W = L"Top";

// Wide twins for the panel region. The names are ASCII, so the twin
// is not about the NAME's content - it is about reaching the wide facades without
// an AnsiToWideReg at every call, which is what lets a later pass delete the ANSI
// facades entirely. Converted region by region; the ANSI constants stay until the
// last region moves.
const wchar_t* SALAMANDER_LEFTP_REG_W = L"Left Panel";
const wchar_t* SALAMANDER_RIGHTP_REG_W = L"Right Panel";
const wchar_t* PANEL_PATH_REG_W = L"Path";
const wchar_t* PANEL_VIEW_REG_W = L"View Type";
const wchar_t* PANEL_SORT_REG_W = L"Sort Type";
const wchar_t* PANEL_REVERSE_REG_W = L"Reverse Sort";
const wchar_t* PANEL_DIRLINE_REG_W = L"Directory Line";
const wchar_t* PANEL_STATUS_REG_W = L"Status Line";
const wchar_t* PANEL_HEADER_REG_W = L"Header Line";
const wchar_t* PANEL_FILTER_ENABLE_W = L"Enable Filter";
const wchar_t* PANEL_FILTER_W = L"Filter";
const wchar_t* PANEL_PATH_REG = L"Path";
const wchar_t* PANEL_VIEW_REG = L"View Type";
const wchar_t* PANEL_SORT_REG = L"Sort Type";
const wchar_t* PANEL_REVERSE_REG = L"Reverse Sort";
const wchar_t* PANEL_DIRLINE_REG = L"Directory Line";
const wchar_t* PANEL_STATUS_REG = L"Status Line";
const wchar_t* PANEL_HEADER_REG = L"Header Line";
const wchar_t* PANEL_FILTER_ENABLE = L"Enable Filter";
const wchar_t* PANEL_FILTER_INVERSE = L"Inverse Filter";
const wchar_t* PANEL_FILTERHISTORY_REG = L"Filter History";
const wchar_t* PANEL_FILTER = L"Filter";

const wchar_t* SALAMANDER_DEFDIRS_REG = L"Default Directories";

const wchar_t* SALAMANDER_CONFIG_REG = SAL_REG_SUBKEY_CONFIGURATION_W;
const wchar_t* SALAMANDER_CONFIG_REG_W = SAL_REG_SUBKEY_CONFIGURATION_W;
const wchar_t* CONFIG_SKILLLEVEL_REG = L"Skill Level";
const wchar_t* CONFIG_FILENAMEFORMAT_REG = L"File Name Format";
const wchar_t* CONFIG_SIZEFORMAT_REG = L"Size Format";
const wchar_t* CONFIG_SELECTION_REG = L"Select/Deselect Directories";
const wchar_t* CONFIG_LONGNAMES_REG = L"Use Long File Names";
const wchar_t* CONFIG_RECYCLEBIN_REG = L"Use Recycle Bin";
const wchar_t* CONFIG_RECYCLEMASKS_REG = L"Use Recycle Bin For";
const wchar_t* CONFIG_SAVEONEXIT_REG = L"Save Configuration On Exit";
const wchar_t* CONFIG_SHOWGREPERRORS_REG = L"Show Errors In Find Files";
const wchar_t* CONFIG_FINDFULLROW_REG = L"Show Full Row In Find Files";
const wchar_t* CONFIG_FINDFILETYPEMODE_REG = L"Find Files Type Filter";
const wchar_t* CONFIG_MINBEEPWHENDONE_REG = L"Use Speeker Beep";
const wchar_t* CONFIG_INTRN_VIEWER_REG = L"Internal Viewer";
const wchar_t* CONFIG_VIEWER_REG = L"External Viewer";
const wchar_t* CONFIG_EDITOR_REG = L"External Editor";
const wchar_t* CONFIG_CMDLINE_REG = L"Command Line";
const wchar_t* CONFIG_CMDLFOCUS_REG = L"Command Line Focused";
const wchar_t* CONFIG_CLOSESHELL_REG = L"Close Shell Window";
const wchar_t* CONFIG_USECUSTOMPANELFONT_REG = L"Use Custom Panel Font";
const wchar_t* CONFIG_PANELFONT_REG = L"Panel Font";
// Registry names are native UTF-16 even where the corresponding legacy value
// payload is imported as ACP bytes. The ...W names select the canonical UTF-16
// history values; the unsuffixed names are read-only legacy import locations.
const wchar_t* CONFIG_NAMEDHISTORY_REG = L"Named History";
const wchar_t* CONFIG_LOOKINHISTORY_REG = L"Look In History";
const wchar_t* CONFIG_GREPHISTORY_REG = L"Grep History";
const wchar_t* CONFIG_VIEWERHISTORY_REG = L"Viewer History";
const wchar_t* CONFIG_NAMEDHISTORYW_REG = L"Named History W";
const wchar_t* CONFIG_LOOKINHISTORYW_REG = L"Look In History W";
const wchar_t* CONFIG_GREPHISTORYW_REG = L"Grep History W";
const wchar_t* CONFIG_VIEWERHISTORYW_REG = L"Viewer History W";
const wchar_t* CONFIG_COMMANDHISTORY_REG = L"Command History";
const wchar_t* CONFIG_SELECTHISTORY_REG = L"Select History";
const wchar_t* CONFIG_COPYHISTORY_REG = L"Copy History";
const wchar_t* CONFIG_COPYHISTORYW_REG = L"Copy History W";
const wchar_t* CONFIG_CHANGEDIRHISTORY_REG = L"ChangeDir History";
const wchar_t* CONFIG_FILELISTHISTORY_REG = L"File List History";
const wchar_t* CONFIG_CREATEDIRHISTORY_REG = L"Create Directory History";
const wchar_t* CONFIG_QUICKRENAMEHISTORY_REG = L"Quick Rename History";
const wchar_t* CONFIG_EDITNEWHISTORY_REG = L"Edit New History";
const wchar_t* CONFIG_CREATEDIRHISTORYW_REG = L"Create Directory History W";
const wchar_t* CONFIG_QUICKRENAMEHISTORYW_REG = L"Quick Rename History W";
const wchar_t* CONFIG_EDITNEWHISTORYW_REG = L"Edit New History W";
const wchar_t* CONFIG_CONVERTHISTORY_REG = L"Convert History";
const wchar_t* CONFIG_FILTERHISTORY_REG = L"Filter History";

// Delete an unsuffixed ACP history key once its UTF-16 successor has been written.
//
// The import path reads these keys whenever the wide history is EMPTY, which is
// exactly the state "Clear history" and "Save history = off" produce - so a
// surviving narrow key is not inert legacy data, it is a copy of the entries the
// user asked Sally to forget, waiting to be read back on the next launch.
static void RetireLegacyHistoryKey(HKEY parent, const wchar_t* name)
{
    HKEY legacy;
    if (!OpenKey(parent, name, legacy)) // nothing to retire: already migrated, or never existed
        return;
    CloseKey(legacy);
    DeleteKey(parent, name);
}
const wchar_t* CONFIG_SELECTHISTORYW_REG = L"Select History W";
const wchar_t* CONFIG_COMMANDHISTORYW_REG = L"Command History W";
const wchar_t* CONFIG_CHANGEDIRHISTORYW_REG = L"ChangeDir History W";
const wchar_t* CONFIG_FILELISTHISTORYW_REG = L"File List History W";
const wchar_t* CONFIG_CONVERTHISTORYW_REG = L"Convert History W";
const wchar_t* CONFIG_FILTERHISTORYW_REG = L"Filter History W";
const wchar_t* CONFIG_WORKDIRSHISTORY_REG = L"Working Directories";
const wchar_t* CONFIG_FILELISTNAME_REG = L"Make File List Name";
const wchar_t* CONFIG_FILELISTAPPEND_REG = L"Make File List Append";
const wchar_t* CONFIG_FILELISTDESTINATION_REG = L"Make File List Destination";
const wchar_t* CONFIG_COPYFINDTEXT_REG = L"Copy Find Text";
const wchar_t* CONFIG_CLEARREADONLY_REG = L"Clear Readonly Attribute";
const wchar_t* CONFIG_PRIMARYCONTEXTMENU_REG = L"Primary Context Menu";
const wchar_t* CONFIG_NOTHIDDENSYSTEM_REG = L"Hide Hidden and System Files and Directories";
const wchar_t* CONFIG_RIGHT_FOCUS_REG = L"Right Panel Focused";
const wchar_t* CONFIG_SHOWCHDBUTTON_REG = L"Show Change Drive Button";
const wchar_t* CONFIG_ALWAYSONTOP_REG = L"Always On Top";
const wchar_t* CONFIG_COMMANDSHELL_KIND_REG = L"Command Shell Kind";
const wchar_t* CONFIG_COMMANDSHELL_PROFILE_GUID_REG = L"Command Shell Profile GUID";
const wchar_t* CONFIG_COMMANDSHELL_PROFILE_NAME_REG = L"Command Shell Profile Name";
// wide twins - these values are read and written through gRegistry,
// which is wide, so the name no longer detours through AnsiToWideReg.
const wchar_t* CONFIG_COMMANDSHELL_PROFILE_GUID_REG_W = L"Command Shell Profile GUID";
const wchar_t* CONFIG_COMMANDSHELL_PROFILE_NAME_REG_W = L"Command Shell Profile Name";
//const char *CONFIG_FASTDIRMOVE_REG = "Fast Directory Move";
const wchar_t* CONFIG_SORTUSESLOCALE_REG = L"Sort Uses Locale";
const wchar_t* CONFIG_SORTDETECTNUMBERS_REG = L"Sort Detects Numbers";
const wchar_t* CONFIG_SORTNEWERONTOP_REG = L"Sort Newer On Top";
const wchar_t* CONFIG_SORTDIRSBYNAME_REG = L"Sort Dirs By Name";
const wchar_t* CONFIG_SORTDIRSBYEXT_REG = L"Sort Dirs By Ext";
const wchar_t* CONFIG_SAVEHISTORY_REG = L"Save History";
const wchar_t* CONFIG_SAVEWORKDIRS_REG = L"Save Working Dirs";
const wchar_t* CONFIG_ENABLECMDLINEHISTORY_REG = L"Enable CmdLine History";
const wchar_t* CONFIG_SAVECMDLINEHISTORY_REG = L"Save CmdLine History";
//const char *CONFIG_LANTASTICCHECK_REG = "Lantastic Check";
const wchar_t* CONFIG_NETWAREFASTDIRMOVE_REG = L"Netware Fast Dir Move";
const wchar_t* CONFIG_ASYNCCOPYALG_REG = L"Async Copy Alg On Network";
const wchar_t* CONFIG_RELOAD_ENV_VARS_REG = L"Reload Environment Variables";
const wchar_t* CONFIG_QUICKRENAME_SELALL_REG = L"Quick Rename Select All";
const wchar_t* CONFIG_EDITNEW_SELALL_REG = L"Edit New File Select All";
const wchar_t* CONFIG_SHIFTFORHOTPATHS_REG = L"Use Shift For GoTo HotPath";
const wchar_t* CONFIG_ONLYONEINSTANCE_REG = L"Only One Instance";
const wchar_t* CONFIG_STATUSAREA_REG = L"Status Area";
const wchar_t* CONFIG_SINGLECLICK_REG = L"Single Click";
//const char *CONFIG_SHOWTIPOFTHEDAY_REG = "Show tip of the Day";
//const char *CONFIG_LASTTIPOFTHEDAY_REG = "Last tip of the Day";
const wchar_t* CONFIG_TOPTOOLBAR_REG = L"Top ToolBar";
const wchar_t* CONFIG_MIDDLETOOLBAR_REG = L"Middle ToolBar";
const wchar_t* CONFIG_LEFTTOOLBAR_REG = L"Left ToolBar";
const wchar_t* CONFIG_RIGHTTOOLBAR_REG = L"Right ToolBar";
const wchar_t* CONFIG_TOPTOOLBARVISIBLE_REG = L"Show Top ToolBar";
const wchar_t* CONFIG_PLGTOOLBARVISIBLE_REG = L"Show Plugins Bar";
const wchar_t* CONFIG_MIDDLETOOLBARVISIBLE_REG = L"Show Middle ToolBar";
const wchar_t* CONFIG_USERMENUTOOLBARVISIBLE_REG = L"Show User Menu ToolBar";
const wchar_t* CONFIG_HOTPATHSBARVISIBLE_REG = L"Hot Paths Bar";
const wchar_t* CONFIG_DRIVEBARVISIBLE_REG = L"Show Drive Bar";
const wchar_t* CONFIG_DRIVEBAR2VISIBLE_REG = L"Show Drive Bar2";
const wchar_t* CONFIG_BOTTOMTOOLBARVISIBLE_REG = L"Show Bottom ToolBar";

const wchar_t* CONFIG_EXPLORERLOOK_REG = L"Explorer Look";
const wchar_t* CONFIG_FULLROWSELECT_REG = L"Full Row Select";
const wchar_t* CONFIG_FULLROWHIGHLIGHT_REG = L"Full Row Highlight";
const wchar_t* CONFIG_USEICONTINCTURE_REG = L"Use Icon Tincture";
const wchar_t* CONFIG_SHOWPANELCAPTION_REG = L"Show Panel Caption";
const wchar_t* CONFIG_SHOWPANELZOOM_REG = L"Show Panel Zoom";
const wchar_t* CONFIG_INFOLINECONTENT_REG = L"Information Line Content";
const wchar_t* CONFIG_IFPATHISINACCESSIBLEGOTOISMYDOCS_REG = L"If Path Is Inaccessible Go To My Docs";
const wchar_t* CONFIG_IFPATHISINACCESSIBLEGOTO_REG = L"If Path Is Inaccessible Go To";
const wchar_t* CONFIG_HOTPATH_AUTOCONFIG = L"Auto Configurate Hot Paths";
const wchar_t* CONFIG_LASTUSEDSPEEDLIM_REG = L"Speed Limit";
const wchar_t* CONFIG_QUICKSEARCHENTER_REG = L"Quick Search Enter Alt";
const wchar_t* CONFIG_CHD_SHOWMYDOC = L"Change Drive Show My Documents";
const wchar_t* CONFIG_CHD_SHOWANOTHER = L"Change Drive Show Another";
const wchar_t* CONFIG_CHD_SHOWCLOUDSTOR = L"Change Drive Show Cloud Storages";
const wchar_t* CONFIG_CHD_SHOWNET = L"Change Drive Network";
const wchar_t* CONFIG_CURRRENTTIPINDEX = L"Current Tip Index";
const wchar_t* CONFIG_SEARCHFILECONTENT = L"Search File Content";
const wchar_t* CONFIG_FINDOPTIONS_REG = L"Find Options";
const wchar_t* CONFIG_FINDIGNORE_REG = L"Find Ignore";
#ifdef _WIN64
const wchar_t* CONFIG_LASTPLUGINVER = L"Plugins.ver Version (x64)";
const wchar_t* CONFIG_LASTPLUGINVER_OP = L"Plugins.ver Version (x86)";
#else  // _WIN64
const wchar_t* CONFIG_LASTPLUGINVER = L"Plugins.ver Version (x86)";
const wchar_t* CONFIG_LASTPLUGINVER_OP = L"Plugins.ver Version (x64)";
#endif // _WIN64
const wchar_t* CONFIG_LANGUAGE_REG = L"Language";
const wchar_t* CONFIG_SHOWSPLASHSCREEN_REG = L"Show Splash Screen";
const wchar_t* CONFIG_CONVERSIONTABLE_REG = L"Conversion Table";
const wchar_t* CONFIG_TITLEBARSHOWPATH_REG = L"Title bar show path";
const wchar_t* CONFIG_TITLEBARMODE_REG = L"Title bar mode";
const wchar_t* CONFIG_THEME_MODE_REG = L"Theme mode";
const wchar_t* CONFIG_TITLEBARPREFIX_REG = L"Title bar prefix";
const wchar_t* CONFIG_TITLEBARPREFIXTEXT_REG = L"Title bar prefix text";
const wchar_t* CONFIG_MAINWINDOWICONINDEX_REG = L"Main window icon index";
const wchar_t* CONFIG_CLICKQUICKRENAME_REG = L"Click to Quick Rename";
const wchar_t* CONFIG_VISIBLEDRIVES_REG = L"Visible Drives";
const wchar_t* CONFIG_SEPARATEDDRIVES_REG = L"Separated Drives";

const wchar_t* CONFIG_COMPAREBYTIME_REG = L"Compare By Time";
const wchar_t* CONFIG_COMPAREBYSIZE_REG = L"Compare By Size";
const wchar_t* CONFIG_COMPAREBYCONTENT_REG = L"Compare By Content";
const wchar_t* CONFIG_COMPAREBYATTR_REG = L"Compare By Attr";
const wchar_t* CONFIG_COMPAREBYSUBDIRS_REG = L"Compare By Subdirs";
const wchar_t* CONFIG_COMPAREBYSUBDIRSATTR_REG = L"Compare By Subdirs Attr";
const wchar_t* CONFIG_COMPAREONEPANELDIRS_REG = L"Compare One Panel Dirs";
const wchar_t* CONFIG_COMPAREMOREOPTIONS_REG = L"Compare More Options";
const wchar_t* CONFIG_COMPAREIGNOREFILES_REG = L"Compare Ignore Files";
const wchar_t* CONFIG_COMPAREIGNOREDIRS_REG = L"Compare Ignore Dirs";
const wchar_t* CONFIG_CONFIGTIGNOREFILESMASKS_REG = L"Compare Ignore Files Masks";
const wchar_t* CONFIG_CONFIGTIGNOREDIRSMASKS_REG = L"Compare Ignore Dirs Masks";
const wchar_t* CONFIG_THUMBNAILSIZE_REG = L"Thumbnail Size";
const wchar_t* CONFIG_ALTLANGFORPLUGINS_REG = L"Alternate Language for Plugins";
const wchar_t* CONFIG_USEALTLANGFORPLUGINS_REG = L"Use Alternate Language for Plugins";
const wchar_t* CONFIG_LANGUAGECHANGED_REG = L"Language Changed";
const wchar_t* CONFIG_ENABLECUSTICOVRLS_REG = L"Enable Custom Icon Overlays";
const wchar_t* CONFIG_DISABLEDCUSTICOVRLS_REG = L"Disabled Custom Icon Overlays";
const wchar_t* CONFIG_COPYMOVEOPTIONS_REG = L"Copy Move Options";
const wchar_t* CONFIG_KEEPPLUGINSSORTED_REG = L"Keep Plugins Sorted";
const wchar_t* CONFIG_SHOWSLGINCOMPLETE_REG = L"Show Translation Is Incomplete";

const wchar_t* CONFIG_EDITNEWFILE_USEDEFAULT_REG = L"Edit New File Use Default";
const wchar_t* CONFIG_EDITNEWFILE_DEFAULT_REG = L"Edit New File Default";

//const char *CONFIG_SPACESELCALCSPACE = "Space Selecting";
const wchar_t* CONFIG_USETIMERESOLUTION = L"Use Time Resolution";
const wchar_t* CONFIG_TIMERESOLUTION = L"Time Resolution";
const wchar_t* CONFIG_IGNOREDSTSHIFTS = L"Ignore DST Shifts";

const wchar_t* CONFIG_USEDRAGDROPMINTIME = L"Use DragDrop Min Time";
const wchar_t* CONFIG_DRAGDROPMINTIME = L"DragDrop Min Time";

// configuration dialog pages
const wchar_t* CONFIG_LASTFOCUSEDPAGE = L"Last Focused Page";
const wchar_t* CONFIG_VIEWANDEDITEXPAND = L"Viewers And Editors Expanded";
const wchar_t* CONFIG_PACKEPAND = L"Packers And Unpackers Expanded";
const wchar_t* CONFIG_CONFIGURATION_HEIGHT = L"Configuration Height";

const wchar_t* CONFIG_MENUINDEX_REG = L"Menu Index";
const wchar_t* CONFIG_MENUBREAK_REG = L"Menu Break";
const wchar_t* CONFIG_MENUWIDTH_REG = L"Menu Width";
const wchar_t* CONFIG_TOOLBARINDEX_REG = L"ToolBar Index";
const wchar_t* CONFIG_TOOLBARBREAK_REG = L"ToolBar Break";
const wchar_t* CONFIG_TOOLBARWIDTH_REG = L"ToolBar Width";
const wchar_t* CONFIG_PLUGINSBARINDEX_REG = L"PluginsBar Index";
const wchar_t* CONFIG_PLUGINSBARBREAK_REG = L"PluginsBar Break";
const wchar_t* CONFIG_PLUGINSBARWIDTH_REG = L"PluginsBar Width";
const wchar_t* CONFIG_USERMENUINDEX_REG = L"User Menu Index";
const wchar_t* CONFIG_USERMENUBREAK_REG = L"User Menu Break";
const wchar_t* CONFIG_USERMENUWIDTH_REG = L"User Menu Width";
const wchar_t* CONFIG_USERMENULABELS_REG = L"User Menu Labels";
const wchar_t* CONFIG_HOTPATHSINDEX_REG = L"Hot Paths Index";
const wchar_t* CONFIG_HOTPATHSBREAK_REG = L"Hot Paths Break";
const wchar_t* CONFIG_HOTPATHSWIDTH_REG = L"Hot Paths Width";
const wchar_t* CONFIG_DRIVEBARINDEX_REG = L"Drive Bar Index";
const wchar_t* CONFIG_DRIVEBARBREAK_REG = L"Drive Bar Break";
const wchar_t* CONFIG_DRIVEBARWIDTH_REG = L"Drive Bar Width";
const wchar_t* CONFIG_GRIPSVISIBLE_REG = L"Grips Visible";

const wchar_t* SALAMANDER_CONFIRMATION_REG = L"Confirmation";
const wchar_t* CONFIG_CNFRM_FILEDIRDEL = L"Files or Dirs Del";
const wchar_t* CONFIG_CNFRM_NEDIRDEL = L"Non-empty Dir Del";
const wchar_t* CONFIG_CNFRM_FILEOVER = L"File Overwrite";
const wchar_t* CONFIG_CNFRM_DIROVER = L"Directory Overwrite";
const wchar_t* CONFIG_CNFRM_SHFILEDEL = L"SH File Del";
const wchar_t* CONFIG_CNFRM_SHDIRDEL = L"SH Dir Del";
const wchar_t* CONFIG_CNFRM_SHFILEOVER = L"SH File Overwrite";
const wchar_t* CONFIG_CNFRM_NTFSPRESS = L"NTFS Compress and Uncompress";
const wchar_t* CONFIG_CNFRM_NTFSCRYPT = L"NTFS Encrypt and Decrypt";
const wchar_t* CONFIG_CNFRM_DAD = L"Drag and Drop";
const wchar_t* CONFIG_CNFRM_CLOSEARCHIVE = L"Close Archive";
const wchar_t* CONFIG_CNFRM_CLOSEFIND = L"Close Find";
const wchar_t* CONFIG_CNFRM_STOPFIND = L"Stop Find";
const wchar_t* CONFIG_CNFRM_CREATETARGETPATH = L"Create Target Path";
const wchar_t* CONFIG_CNFRM_ALWAYSONTOP = L"Always on Top";
const wchar_t* CONFIG_CNFRM_ONSALCLOSE = L"Close Salamander";
const wchar_t* CONFIG_CNFRM_SENDEMAIL = L"Send Email";
const wchar_t* CONFIG_CNFRM_ADDTOARCHIVE = L"Add To Archive";
const wchar_t* CONFIG_CNFRM_CREATEDIR = L"Create Dir";
const wchar_t* CONFIG_CNFRM_CHANGEDIRTC = L"Change Dir TC";
const wchar_t* CONFIG_CNFRM_SHOWNAMETOCOMP = L"Show Names To Compare";
const wchar_t* CONFIG_CNFRM_DSTSHIFTSIGNORED = L"DST Shifts Ignored";
const wchar_t* CONFIG_CNFRM_DSTSHIFTSOCCURED = L"DST Shifts Occured";
const wchar_t* CONFIG_CNFRM_COPYMOVEOPTIONSNS = L"Copy Move Options Not Supported";

const wchar_t* SALAMANDER_DRVSPEC_REG = L"Drive Special Settings";
const wchar_t* CONFIG_DRVSPEC_FLOPPY_MON = L"Floppy Automatic Refresh";
const wchar_t* CONFIG_DRVSPEC_FLOPPY_SIMPLE = L"Floppy Simple Icons";
const wchar_t* CONFIG_DRVSPEC_REMOVABLE_MON = L"Removable Automatic Refresh";
const wchar_t* CONFIG_DRVSPEC_REMOVABLE_SIMPLE = L"Removable Simple Icons";
const wchar_t* CONFIG_DRVSPEC_FIXED_MON = L"Fixed Automatic Refresh";
const wchar_t* CONFIG_DRVSPEC_FIXED_SIMPLE = L"Fixed Simple Icons";
const wchar_t* CONFIG_DRVSPEC_REMOTE_MON = L"Remote Automatic Refresh";
const wchar_t* CONFIG_DRVSPEC_REMOTE_SIMPLE = L"Remote Simple Icons";
const wchar_t* CONFIG_DRVSPEC_REMOTE_ACT = L"Remote Do Not Refresh on Activation";
const wchar_t* CONFIG_DRVSPEC_CDROM_MON = L"CDROM Automatic Refresh";
const wchar_t* CONFIG_DRVSPEC_CDROM_SIMPLE = L"CDROM Simple Icons";

const wchar_t* SALAMANDER_HOTPATHS_REG = L"Hot Paths";

const wchar_t* SALAMANDER_VIEWTEMPLATES_REG = L"View Templates";

const wchar_t* SALAMANDER_VIEWER_REG = L"Viewer";
const wchar_t* VIEWER_FINDFORWARD_REG = L"Forward Direction";
const wchar_t* VIEWER_FINDWHOLEWORDS_REG = L"Whole Words";
const wchar_t* VIEWER_FINDCASESENSITIVE_REG = L"Case Sensitive";
const wchar_t* VIEWER_FINDTEXT_REG = L"Find Text";
const wchar_t* VIEWER_FINDHEXMODE_REG = L"HEX-mode";
const wchar_t* VIEWER_FINDREGEXP_REG = L"Regular Expression";
const wchar_t* VIEWER_CONFIGCRLF_REG = L"EOL CRLF";
const wchar_t* VIEWER_CONFIGCR_REG = L"EOL CR";
const wchar_t* VIEWER_CONFIGLF_REG = L"EOL LF";
const wchar_t* VIEWER_CONFIGNULL_REG = L"EOL NULL";
const wchar_t* VIEWER_CONFIGTABSIZE_REG = L"Tabelator Size";
const wchar_t* VIEWER_CONFIGDEFMODE_REG = L"Default Mode";
const wchar_t* VIEWER_CONFIGTEXTMASK_REG = L"Text Masks";
const wchar_t* VIEWER_CONFIGHEXMASK_REG = L"Hex Masks";
const wchar_t* VIEWER_CONFIGUSECUSTOMFONT_REG = L"Viewer Use Custom Font";
const wchar_t* VIEWER_CONFIGFONT_REG = L"Viewer Font";
const wchar_t* VIEWER_WRAPTEXT_REG = L"Wrap Text";
const wchar_t* VIEWER_CPAUTOSELECT_REG = L"Auto-Select";
const wchar_t* VIEWER_DEFAULTCONVERT_REG = L"Default Convert";
const wchar_t* VIEWER_AUTOCOPYSELECTION_REG = L"Auto-Copy Selection";
const wchar_t* VIEWER_GOTOOFFSETISHEX_REG = L"Go to Offset Is Hex";

const wchar_t* VIEWER_CONFIGSAVEWINPOS_REG = L"Save Window Position";
const wchar_t* VIEWER_CONFIGWNDLEFT_REG = L"Left";
const wchar_t* VIEWER_CONFIGWNDRIGHT_REG = L"Right";
const wchar_t* VIEWER_CONFIGWNDTOP_REG = L"Top";
const wchar_t* VIEWER_CONFIGWNDBOTTOM_REG = L"Bottom";
const wchar_t* VIEWER_CONFIGWNDSHOW_REG = L"Show";

const wchar_t* SALAMANDER_USERMENU_REG = L"User Menu";
const wchar_t* USERMENU_ITEMNAME_REG = L"Item Name";
const wchar_t* USERMENU_COMMAND_REG = L"Command";
const wchar_t* USERMENU_ARGUMENTS_REG = L"Arguments";
const wchar_t* USERMENU_INITDIR_REG = L"Initial Directory";
const wchar_t* USERMENU_SHELL_REG = L"Execute Through Shell";
const wchar_t* USERMENU_USEWINDOW_REG = L"Open Shell Window";
const wchar_t* USERMENU_CLOSE_REG = L"Close Shell Window";
const wchar_t* USERMENU_SEPARATOR_REG = L"Separator";
const wchar_t* USERMENU_SHOWINTOOLBAR_REG = L"Show In Toolbar";
const wchar_t* USERMENU_TYPE_REG = L"Type";
const wchar_t* USERMENU_ICON_REG = L"Icon";

const wchar_t* SALAMANDER_VIEWERS_REG = L"Viewers";
const wchar_t* SALAMANDER_ALTVIEWERS_REG = L"Alternative Viewers";
const wchar_t* VIEWERS_MASKS_REG = L"Masks";
const wchar_t* VIEWERS_COMMAND_REG = USERMENU_COMMAND_REG;
const wchar_t* VIEWERS_ARGUMENTS_REG = USERMENU_ARGUMENTS_REG;
const wchar_t* VIEWERS_INITDIR_REG = USERMENU_INITDIR_REG;
const wchar_t* VIEWERS_TYPE_REG = L"Type";

const wchar_t* SALAMANDER_IZIP_REG = L"Internal ZIP Packer";

const wchar_t* SALAMANDER_EDITORS_REG = L"Editors";
const wchar_t* EDITORS_MASKS_REG = VIEWERS_MASKS_REG;
const wchar_t* EDITORS_COMMAND_REG = USERMENU_COMMAND_REG;
const wchar_t* EDITORS_ARGUMENTS_REG = USERMENU_ARGUMENTS_REG;
const wchar_t* EDITORS_INITDIR_REG = USERMENU_INITDIR_REG;

const wchar_t* SALAMANDER_VERSION_REG = SAL_REG_VALUE_VERSION_W;
const wchar_t* SALAMANDER_VERSION_REG_W = SAL_REG_VALUE_VERSION_W;
const wchar_t* SALAMANDER_VERSIONREG_REG = SAL_REG_SUBKEY_CONFIGURATION_W;

const wchar_t* SALAMANDER_CUSTOMCOLORS_REG = L"Custom Colors";

// colors
const wchar_t* SALAMANDER_COLORS_REG = L"Colors";
const wchar_t* SALAMANDER_CLR_FOCUS_ACTIVE_NORMAL_REG = L"Focus Active Normal";
const wchar_t* SALAMANDER_CLR_FOCUS_ACTIVE_SELECTED_REG = L"Focus Active Selected";
const wchar_t* SALAMANDER_CLR_FOCUS_INACTIVE_NORMAL_REG = L"Focus Inactive Normal";
const wchar_t* SALAMANDER_CLR_FOCUS_INACTIVE_SELECTED_REG = L"Focus Inactive Selected";
const wchar_t* SALAMANDER_CLR_FOCUS_BK_INACTIVE_NORMAL_REG = L"Focus Bk Inactive Normal";
const wchar_t* SALAMANDER_CLR_FOCUS_BK_INACTIVE_SELECTED_REG = L"Focus Bk Inactive Selected";

const wchar_t* SALAMANDER_CLR_ITEM_FG_NORMAL_REG = L"Item Fg Normal";
const wchar_t* SALAMANDER_CLR_ITEM_FG_SELECTED_REG = L"Item Fg Selected";
const wchar_t* SALAMANDER_CLR_ITEM_FG_FOCUSED_REG = L"Item Fg Focused";
const wchar_t* SALAMANDER_CLR_ITEM_FG_FOCSEL_REG = L"Item Fg Focused and Selected";
const wchar_t* SALAMANDER_CLR_ITEM_FG_HIGHLIGHT_REG = L"Item Fg Highlight";

const wchar_t* SALAMANDER_CLR_ITEM_BK_NORMAL_REG = L"Item Bk Normal";
const wchar_t* SALAMANDER_CLR_ITEM_BK_SELECTED_REG = L"Item Bk Selected";
const wchar_t* SALAMANDER_CLR_ITEM_BK_FOCUSED_REG = L"Item Bk Focused";
const wchar_t* SALAMANDER_CLR_ITEM_BK_FOCSEL_REG = L"Item Bk Focused and Selected";
const wchar_t* SALAMANDER_CLR_ITEM_BK_HIGHLIGHT_REG = L"Item Bk Highlight";

const wchar_t* SALAMANDER_CLR_ICON_BLEND_SELECTED_REG = L"Icon Blend Selected";
const wchar_t* SALAMANDER_CLR_ICON_BLEND_FOCUSED_REG = L"Icon Blend Focused";
const wchar_t* SALAMANDER_CLR_ICON_BLEND_FOCSEL_REG = L"Icon Blend Focused and Selected";

const wchar_t* SALAMANDER_CLR_PROGRESS_FG_NORMAL_REG = L"Progress Fg Normal";
const wchar_t* SALAMANDER_CLR_PROGRESS_FG_SELECTED_REG = L"Progress Fg Selected";
const wchar_t* SALAMANDER_CLR_PROGRESS_BK_NORMAL_REG = L"Progress Bk Normal";
const wchar_t* SALAMANDER_CLR_PROGRESS_BK_SELECTED_REG = L"Progress Bk Selected";

const wchar_t* SALAMANDER_CLR_VIEWER_FG_NORMAL_REG = L"Viewer Fg Normal";
const wchar_t* SALAMANDER_CLR_VIEWER_BK_NORMAL_REG = L"Viewer Bk Normal";
const wchar_t* SALAMANDER_CLR_VIEWER_FG_SELECTED_REG = L"Viewer Fg Selected";
const wchar_t* SALAMANDER_CLR_VIEWER_BK_SELECTED_REG = L"Viewer Bk Selected";

const wchar_t* SALAMANDER_CLR_HOT_PANEL_REG = L"Hot Panel";
const wchar_t* SALAMANDER_CLR_HOT_ACTIVE_REG = L"Hot Active";
const wchar_t* SALAMANDER_CLR_HOT_INACTIVE_REG = L"Hot Inactive";

const wchar_t* SALAMANDER_CLR_ACTIVE_CAPTION_FG_REG = L"Active Caption Fg";
const wchar_t* SALAMANDER_CLR_ACTIVE_CAPTION_BK_REG = L"Active Caption Bk";
const wchar_t* SALAMANDER_CLR_INACTIVE_CAPTION_FG_REG = L"Inactive Caption Fg";
const wchar_t* SALAMANDER_CLR_INACTIVE_CAPTION_BK_REG = L"Inactive Caption Bk";

const wchar_t* SALAMANDER_CLR_THUMBNAIL_FRAME_NORMAL_REG = L"Thumbnail Frame Normal";
const wchar_t* SALAMANDER_CLR_THUMBNAIL_FRAME_SELECTED_REG = L"Thumbnail Frame Selected";
const wchar_t* SALAMANDER_CLR_THUMBNAIL_FRAME_FOCUSED_REG = L"Thumbnail Frame Focused";
const wchar_t* SALAMANDER_CLR_THUMBNAIL_FRAME_FOCSEL_REG = L"Thumbnail Frame Focused and Selected";

const wchar_t* SALAMANDER_HLT = L"Panel Items Hilighting";
const wchar_t* SALAMANDER_HLT_ITEM_MASKS = L"Masks";
const wchar_t* SALAMANDER_HLT_ITEM_ATTR = L"Attributes";
const wchar_t* SALAMANDER_HLT_ITEM_VALIDATTR = L"Valid Attributes";
const wchar_t* SALAMANDER_HLT_ITEM_FG_NORMAL_REG = L"Item Fg Normal";
const wchar_t* SALAMANDER_HLT_ITEM_FG_SELECTED_REG = L"Item Fg Selected";
const wchar_t* SALAMANDER_HLT_ITEM_FG_FOCUSED_REG = L"Item Fg Focused";
const wchar_t* SALAMANDER_HLT_ITEM_FG_FOCSEL_REG = L"Item Fg Focused and Selected";
const wchar_t* SALAMANDER_HLT_ITEM_FG_HIGHLIGHT_REG = L"Item Fg Highlight";
const wchar_t* SALAMANDER_HLT_ITEM_BK_NORMAL_REG = L"Item Bk Normal";
const wchar_t* SALAMANDER_HLT_ITEM_BK_SELECTED_REG = L"Item Bk Selected";
const wchar_t* SALAMANDER_HLT_ITEM_BK_FOCUSED_REG = L"Item Bk Focused";
const wchar_t* SALAMANDER_HLT_ITEM_BK_FOCSEL_REG = L"Item Bk Focused and Selected";
const wchar_t* SALAMANDER_HLT_ITEM_BK_HIGHLIGHT_REG = L"Item Bk Highlight";

const wchar_t* SALAMANDER_CLRSCHEME_REG = L"Color Scheme";

// Plugins
const wchar_t* SALAMANDER_PLUGINS = L"Plugins";
const wchar_t* SALAMANDER_PLUGINS_NAME = L"Name";
const wchar_t* SALAMANDER_PLUGINS_DLLNAME = L"DLL";
const wchar_t* SALAMANDER_PLUGINS_VERSION = L"Version";
const wchar_t* SALAMANDER_PLUGINS_COPYRIGHT = L"Copyright";
const wchar_t* SALAMANDER_PLUGINS_EXTENSIONS = L"Extensions";
const wchar_t* SALAMANDER_PLUGINS_DESCRIPTION = L"Description";
const wchar_t* SALAMANDER_PLUGINS_LASTSLGNAME = L"LastSLGName";
const wchar_t* SALAMANDER_PLUGINS_HOMEPAGE = L"HomePage";
//const char *SALAMANDER_PLUGINS_PLGICONS = "PluginIcons";
const wchar_t* SALAMANDER_PLUGINS_PLGICONLIST = L"PluginIconList";
const wchar_t* SALAMANDER_PLUGINS_PLGICONINDEX = L"PluginIconIndex";
const wchar_t* SALAMANDER_PLUGINS_PLGSUBMENUICONINDEX = L"SubmenuIconIndex";
const wchar_t* SALAMANDER_PLUGINS_SUBMENUINPLUGINSBAR = L"SubmenuInPluginsBar";
const wchar_t* SALAMANDER_PLUGINS_THUMBMASKS = L"ThumbnailMasks";
const wchar_t* SALAMANDER_PLUGINS_REGKEYNAME = L"Configuration Key";
const wchar_t* SALAMANDER_PLUGINS_FSNAME = L"FS Name";
const wchar_t* SALAMANDER_PLUGINS_FUNCTIONS = L"Functions";
const wchar_t* SALAMANDER_PLUGINS_LOADONSTART = L"Load On Start";
const wchar_t* SALAMANDER_PLUGINS_LEGACYCOMPATAPPROVED = L"Legacy Compat Approved";
const wchar_t* SALAMANDER_PLUGINS_MENU = L"Menu";
const wchar_t* SALAMANDER_PLUGINS_MENUITEMNAME = L"Name";
const wchar_t* SALAMANDER_PLUGINS_MENUITEMSTATE = L"State";
const wchar_t* SALAMANDER_PLUGINS_MENUITEMID = L"ID";
const wchar_t* SALAMANDER_PLUGINS_MENUITEMSKILLLEVEL = L"Skill";
const wchar_t* SALAMANDER_PLUGINS_MENUITEMICONINDEX = L"Icon";
const wchar_t* SALAMANDER_PLUGINS_MENUITEMTYPE = L"Type";
const wchar_t* SALAMANDER_PLUGINS_MENUITEMHOTKEY = L"HotKey";
const wchar_t* SALAMANDER_PLUGINS_FSCMDNAME = L"FS Cmd Name";
const wchar_t* SALAMANDER_PLUGINS_FSCMDICON = L"FS Cmd Icon";
const wchar_t* SALAMANDER_PLUGINS_FSCMDVISIBLE = L"FS Cmd Visible";
const wchar_t* SALAMANDER_PLUGINS_ISNETHOOD = L"Is Nethood";
const wchar_t* SALAMANDER_PLUGINS_USESPASSWDMAN = L"Uses Password Manager";

// Plugins: the following eight strings are only for converting configuration from version 6 and older
const wchar_t* SALAMANDER_PLUGINS_PANELVIEW = L"Panel List";
const wchar_t* SALAMANDER_PLUGINS_PANELEDIT = L"Panel Pack";
const wchar_t* SALAMANDER_PLUGINS_CUSTPACK = L"Custom Pack";
const wchar_t* SALAMANDER_PLUGINS_CUSTUNPACK = L"Custom Unpack";
const wchar_t* SALAMANDER_PLUGINS_CONFIG = L"Configuration";
const wchar_t* SALAMANDER_PLUGINS_LOADSAVE = L"Persistent";
const wchar_t* SALAMANDER_PLUGINS_VIEWER = L"File Viewer";
const wchar_t* SALAMANDER_PLUGINS_FS = L"File System";

// Plugins Configuration
const wchar_t* SALAMANDER_PLUGINSCONFIG = L"Plugins Configuration";

// Plugins Order
const wchar_t* SALAMANDER_PLUGINSORDER = L"Plugins Order";
const wchar_t* SALAMANDER_PLUGINSORDER_SHOW = L"ShowInBar";

// Packers & Unpackers
const wchar_t* SALAMANDER_PACKANDUNPACK = L"Packers & Unpackers";
const wchar_t* SALAMANDER_CUSTOMPACKERS = L"Custom Packers";
const wchar_t* SALAMANDER_CUSTOMUNPACKERS = L"Custom Unpackers";
const wchar_t* SALAMANDER_PREDPACKERS = L"Predefined Packers";
const wchar_t* SALAMANDER_ARCHIVEASSOC = L"Archive Association";
// for SALAMANDER_CUSTOMPACKERS and SALAMANDER_CUSTOMUNPACKERS
const wchar_t* SALAMANDER_ANOTHERPANEL = L"Use Another Panel";
const wchar_t* SALAMANDER_PREFFERED = L"Preffered";
const wchar_t* SALAMANDER_NAMEBYARCHIVE = L"Use Subdir Name By Archive";
const wchar_t* SALAMANDER_SIMPLEICONSINARCHIVES = L"Simple Icons In Archives";

const wchar_t* SALAMANDER_PWDMNGR_REG = L"Password Manager";

static IRegistry* GetMainWindowRegistry()
{
    IRegistry* registry = gRegistry;
    if (registry == NULL)
        registry = GetWin32Registry();
    return registry;
}

static BOOL ClearRegistryKeyTree(IRegistry* registry, HKEY key)
{
    if (registry == NULL || key == NULL)
        return FALSE;

    std::vector<std::wstring> subKeys;
    if (!registry->EnumSubKeys(key, subKeys).success)
        return FALSE;

    for (const std::wstring& subKeyName : subKeys)
    {
        HKEY subKey = NULL;
        if (!registry->OpenKeyReadWrite(key, subKeyName.c_str(), subKey).success)
            return FALSE;

        BOOL subKeyCleared = ClearRegistryKeyTree(registry, subKey);
        registry->CloseKey(subKey);
        if (!subKeyCleared)
            return FALSE;

        if (!registry->DeleteKey(key, subKeyName.c_str()).success)
            return FALSE;
    }

    std::vector<std::wstring> valueNames;
    if (!registry->EnumValues(key, valueNames).success)
        return FALSE;

    for (const std::wstring& valueName : valueNames)
    {
        if (!registry->DeleteValue(key, valueName.c_str()).success)
            return FALSE;
    }
    return TRUE;
}

//****************************************************************************
//
// GetUpgradeInfo
//
// Tries to find "AutoImportConfig" in the configuration key of this version of Salamander.
// If it is not found or if the key stored in AutoImportConfig does not exist
// (points to the key of this version, which makes no sense)
// or if it contains a corrupted (incomplete save) or empty configuration, it returns
// FALSE in 'autoImportConfig'. Otherwise it returns TRUE in 'autoImportConfig' and
// in 'autoImportConfigFromKey' returns the path of the key from which to import the configuration.
// Handles the case when AutoImportConfig points to a key that itself contains AutoImportConfig
// for another key. We simply follow the "target" key and leave intermediate keys untouched-
// if the import succeeds, the target key will be removed anyway. Returns FALSE only if the application should exit.
//
// If the configuration key of this version contains, besides AutoImportConfig, also the "Configuration"
// key (expected to be a saved configuration),
// we ask the user whether to:
//   - Use the current configuration and ignore the old one (we do not delete it so the user does not lose data,
//     and it does not require that much space anyway). In this case delete AutoImportConfig immediately.
//     This is done silently, if AutoImportConfig points to this version of Salamander`s key
//     (DEFAULT OFFER because it does not cause data loss and users may dismiss the message box without reading).
//   - Delete the current configuration and import the old version. In this case remove everything except AutoImportConfig.
//   - Exit the application - simply return FALSE.

BOOL GetUpgradeInfo(BOOL* autoImportConfig, std::wstring& autoImportConfigFromKey)
{
    HKEY rootKey = NULL;
    DWORD saveInProgress; // dummy
    BOOL doNotExit = TRUE;
    IRegistry* registry = GetMainWindowRegistry();
    autoImportConfigFromKey.clear();
    LoadSaveToRegistryMutex.Enter();
    int rounds = 0; // prevent infinite loops
    *autoImportConfig = FALSE;
    if (registry != NULL &&
        registry->OpenKeyRead(HKEY_CURRENT_USER, SalamanderConfigurationRoots[0], rootKey).success)
    {
        HKEY oldCfgKey;
        std::wstring oldKeyName;

        // AutoImportConfig's value NAME stays narrow-literal (bridged inside GetValue),
        // its data is already the natively-wide REG_SZ payload - oldKeyName being wide is correct here
        if (registry->GetString(rootKey, SAL_REG_VALUE_AUTO_IMPORT_CONFIG_W, oldKeyName).success)
        { // we found "AutoImportConfig"
        OPEN_AUTO_IMPORT_CONFIG_KEY:
            autoImportConfigFromKey = SalamanderConfigurationRoots[0];
            if (CutDirectoryW(autoImportConfigFromKey))
            {
                SalPathAppendW(autoImportConfigFromKey, oldKeyName.c_str());
                if (IsTheSamePath(autoImportConfigFromKey.c_str(), SalamanderConfigurationRoots[0]) ||
                    !registry->OpenKeyRead(HKEY_CURRENT_USER, autoImportConfigFromKey.c_str(), oldCfgKey).success)
                    goto AUTO_IMPORT_CONFIG_NOT_FOUND;

                // if the current "target" key also contains AutoImportConfig, follow it...
                if (registry->GetString(oldCfgKey, SAL_REG_VALUE_AUTO_IMPORT_CONFIG_W, oldKeyName).success && ++rounds <= 50)
                {
                    registry->CloseKey(oldCfgKey);
                    goto OPEN_AUTO_IMPORT_CONFIG_KEY;
                }
                HKEY cfgKey;
                if (rounds <= 50 &&
                    !registry->GetDWord(oldCfgKey, SALAMANDER_SAVE_IN_PROGRESS, saveInProgress).success &&
                    registry->OpenKeyRead(oldCfgKey, SALAMANDER_CONFIG_REG_W, cfgKey).success)
                {
                    registry->CloseKey(cfgKey);
                    *autoImportConfig = TRUE; // configuration is valid and not empty
                }
                registry->CloseKey(oldCfgKey);
            }
        AUTO_IMPORT_CONFIG_NOT_FOUND:;
        }
        if (*autoImportConfig) // check whether this version's key also contains configuration (besides "AutoImportConfig")
        {
            HKEY cfgKey;
            std::wstring currentConfigKey = SalamanderConfigurationRoots[0];
            SalPathAppendW(currentConfigKey, SALAMANDER_CONFIG_REG);
            if (registry->OpenKeyRead(HKEY_CURRENT_USER, currentConfigKey.c_str(), cfgKey).success)
            {
                registry->CloseKey(cfgKey);
                BOOL clearCfg = FALSE;
                if (!registry->GetDWord(rootKey, SALAMANDER_SAVE_IN_PROGRESS, saveInProgress).success)
                { // this key contains a valid configuration; ask the user what to do
                    registry->CloseKey(rootKey);
                    rootKey = NULL;
                    LoadSaveToRegistryMutex.Leave();

                    MSGBOXEX_PARAMS params;
                    memset(&params, 0, sizeof(params));
                    params.HParent = NULL;
                    params.Flags = MB_ABORTRETRYIGNORE | MB_ICONQUESTION | MB_SETFOREGROUND;
                    params.Caption = SALAMANDER_TEXT_VERSIONW();
                    std::wstring oldKeyPath = autoImportConfigFromKey;
                    std::wstring keyName;
                    if (!CutDirectoryW(oldKeyPath, &keyName))
                        keyName = oldKeyPath; // theoretically cannot happen
                    const std::wstring message = FormatStrW(L"You have upgraded from %ls (old version) to %ls (new version). The configuration of the old "
                                  L"version should be imported to the new version now, but there is already existing "
                                  L"configuration for the new version. You can use this existing configuration (the configuration of "
                                  L"the old version remains in registry, so you can import it later). Or you can overwrite "
                                  L"this existing configuration (it would be lost) with the configuration of the old version. "
                                  L"Or you can exit Open Salamander and solve this problem later.",
                             keyName.c_str(), SALAMANDER_TEXT_VERSIONW());
                    params.Text = message.c_str();
                    const std::wstring aliasBtnNames = FormatStrW(L"%d\t%ls\t%d\t%ls\t%d\t%ls",
                             DIALOG_ABORT, L"&Use Existing Configuration",
                             DIALOG_RETRY, L"&Overwrite Existing Configuration",
                             DIALOG_IGNORE, L"&Exit");
                    params.AliasBtnNames = aliasBtnNames.c_str();
                    int res = SalMessageBoxEx(&params);
                    switch (res)
                    {
                    case DIALOG_ABORT:
                        *autoImportConfig = FALSE;
                        break;
                    case DIALOG_RETRY:
                        clearCfg = TRUE;
                        break;

                    // case DIALOG_IGNORE:
                    default:
                        doNotExit = FALSE;
                        break;
                    }

                    LoadSaveToRegistryMutex.Enter();
                }
                else
                    clearCfg = TRUE; // configuration is corrupted, delete it
                if (clearCfg &&
                    registry->OpenKeyReadWrite(HKEY_CURRENT_USER, SalamanderConfigurationRoots[0], cfgKey).success)
                { // delete the configuration and leave only "AutoImportConfig" (recreate it)
                    ClearRegistryKeyTree(registry, cfgKey);
                    std::wstring oldKeyPath = autoImportConfigFromKey;
                    std::wstring keyName;
                    if (!CutDirectoryW(oldKeyPath, &keyName))
                        keyName = oldKeyPath; // theoretically cannot happen
                    registry->SetString(cfgKey, SAL_REG_VALUE_AUTO_IMPORT_CONFIG_W, keyName.c_str());
                    registry->CloseKey(cfgKey);
                }
            }
        }
        if (rootKey != NULL)
            registry->CloseKey(rootKey);
    }
    if (!*autoImportConfig && // this version's key lacks "AutoImportConfig" or does not point to a valid old configuration
        registry != NULL &&
        registry->OpenKeyReadWrite(HKEY_CURRENT_USER, SalamanderConfigurationRoots[0], rootKey).success)
    { // remove "AutoImportConfig" from this version's key (if it exists it makes no sense here)
        registry->DeleteValue(rootKey, SAL_REG_VALUE_AUTO_IMPORT_CONFIG_W);
        registry->CloseKey(rootKey);
    }
    LoadSaveToRegistryMutex.Leave();
    return doNotExit;
}

//****************************************************************************
//
// FindLanguageFromPrevVerOfSal
//
// Retrieves the language (the .slg module used) from an older version of Salamander.
// The oldest version from which we obtain this information is 2.53 beta 2 (the first version shipped with multiple languages: CZ+DE+EN).
// If a configuration for the current version exists or such a language is not found, returns FALSE.
// Otherwise returns the language in 'slgName'.

BOOL FindLanguageFromPrevVerOfSal(std::wstring& slgName)
{
    HKEY hCfgKey = NULL;
    HKEY hRootKey = NULL;
    int rootIndex = 0;
    const wchar_t* root;
    DWORD saveInProgress; // dummy
    IRegistry* registry = GetMainWindowRegistry();

    slgName.clear();
    LoadSaveToRegistryMutex.Enter();
    if (registry == NULL)
    {
        LoadSaveToRegistryMutex.Leave();
        return FALSE;
    }
    do
    {
        // check if the key exists and if a configuration is stored under it
        root = SalamanderConfigurationRoots[rootIndex];
        BOOL rootFound = registry->OpenKeyRead(HKEY_CURRENT_USER, root, hRootKey).success;
        BOOL cfgFound = rootFound &&
                        registry->OpenKeyRead(hRootKey, SALAMANDER_CONFIG_REG_W, hCfgKey).success;
        if (cfgFound && GetValueW(hRootKey, SALAMANDER_SAVE_IN_PROGRESS, REG_DWORD, &saveInProgress, sizeof(DWORD)))
        { // the configuration is corrupted
            cfgFound = FALSE;
            registry->CloseKey(hCfgKey);
        }
        DWORD configVersion = 1; // this is configuration from 1.52 or older
        if (cfgFound)
        {
            HKEY actKey;
            if (registry->OpenKeyRead(hRootKey, SALAMANDER_VERSION_REG_W, actKey).success)
            {
                configVersion = 2; // configuration from 1.6b1
                GetValueW(actKey, SALAMANDER_VERSIONREG_REG_W, REG_DWORD, &configVersion, sizeof(DWORD));
                registry->CloseKey(actKey);
            }
        }
        if (rootFound)
            registry->CloseKey(hRootKey);
        if (cfgFound)
        {
            BOOL found = FALSE;
            if (rootIndex != 0 &&                      // only for one of the older keys
                configVersion >= 59 /* 2.53 beta 2 */) // before 2.53 beta 2 there was only English, so reading makes no sense; offer system default language or manual selection of the language
            {
                GetStringValueW(hCfgKey, CONFIG_LANGUAGE_REG, slgName);
                found = !slgName.empty();
            }
            registry->CloseKey(hCfgKey);
            LoadSaveToRegistryMutex.Leave();
            return found;
        }
        rootIndex++;
    } while (rootIndex < SALCFG_ROOTS_COUNT);

    LoadSaveToRegistryMutex.Leave();
    return FALSE;
}

// obtains a number from a string (unsigned decimal format); returns TRUE, if a number was found
// ignores white spaces before and after the number
BOOL GetNumFromStr(const wchar_t* s, DWORD* retNum)
{
    DWORD n = 0;
    while (*s != 0 && *s <= ' ')
        s++;
    BOOL mayBeOK = *s >= '0' && *s <= '9';
    while (*s >= '0' && *s <= '9')
        n = 10 * n + (*s++ - '0');
    while (*s != 0 && *s <= ' ')
        s++;
    *retNum = n;
    return mayBeOK && *s == 0;
}

void CheckShutdownParams()
{
    // HKEY_CURRENT_USER\Control Panel\Desktop\WaitToKillAppTimeout=20000,REG_SZ ... warn if less than 20000
    // HKEY_CURRENT_USER\Control Panel\Desktop\AutoEndTasks=0,REG_SZ ... warn if not 0
    // W2K and XP have it; I could not find it on Vista but supposedly it is there (info from the internet)

    BOOL showWarning = FALSE;
    IRegistry* registry = GetMainWindowRegistry();
    HKEY key;
    if (registry != NULL &&
        registry->OpenKeyRead(HKEY_CURRENT_USER, SAL_REG_KEY_CONTROL_PANEL_DESKTOP_W, key).success)
    {
        std::wstring num;
        DWORD value;
        if (registry->GetString(key, SAL_REG_VALUE_WAIT_TO_KILL_APP_TIMEOUT_W, num).success &&
            GetNumFromStr(num.c_str(), &value) && value < 20000)
        {
            TRACE_E("CheckShutdownParams(): WaitToKillAppTimeout is '" << sally::diagnostic::EncodeAcpLossy(num) << "' (" << value << ")");
            showWarning = TRUE;
        }
        if (registry->GetString(key, SAL_REG_VALUE_AUTO_END_TASKS_W, num).success &&
            GetNumFromStr(num.c_str(), &value) && value != 0)
        {
            TRACE_E("CheckShutdownParams(): AutoEndTasks is '" << sally::diagnostic::EncodeAcpLossy(num) << "' (" << value << ")");
            showWarning = TRUE;
        }
        registry->CloseKey(key);
    }

    if (showWarning)
        gPrompter->ShowError(SALAMANDER_TEXT_VERSIONW(), LoadStrW(IDS_CHANGEDSHUTDOWNPARS));
}

BOOL MyRegRenameKey(HKEY key, const wchar_t* name, const wchar_t* newName)
{
    BOOL ret = FALSE;
    IRegistry* registry = GetMainWindowRegistry();
    if (registry == NULL)
    {
        TRACE_E("MyRegRenameKey(): registry service unavailable");
        return FALSE;
    }
    // There is also NtRenameKey but I could not get it working (requires UNICODE_STRING
    // and probably the key opened via NtOpenKey with the key passed via OBJECT_ATTRIBUTES initialized through
    // InitializeObjectAttributes). It's overly complicated and not frequently used code,
    // so we'll do it the slow but simple way... copy the key to a new one and then delete the original
    HKEY newKey;
    if (!registry->OpenKeyRead(key, newName, newKey).success) // verify if the target key does not already exist
    {
        if (registry->CreateKey(key, newName, newKey).success) // create the target key
        {
            // I also tried RegCopyTree (didn't work without KEY_ALL_ACCESS) and the speed was the same as SHCopyKey
            if (SHCopyKeyW(key, name, newKey, 0) == ERROR_SUCCESS) // copy into the target key
                ret = TRUE;
            registry->CloseKey(newKey);
            if (ret &&
                !registry->DeleteKeyRecursive(key, name).success)
            {
                TRACE_E("MyRegRenameKey(): unable to delete source key after copy: " << sally::diagnostic::EncodeAcpLossy(name));
            }
        }
        else
            TRACE_E("MyRegRenameKey(): unable to create target key: " << sally::diagnostic::EncodeAcpLossy(newName));
    }
    else
    {
        registry->CloseKey(newKey);
        TRACE_E("MyRegRenameKey(): target key already exists: " << sally::diagnostic::EncodeAcpLossy(newName));
    }
    return ret;
}

//****************************************************************************
//
// FindLatestConfiguration
//
// Tries to find a configuration that matches our program version.
// If it succeeds, 'loadConfiguration' variable is set and the function returns TRUE.
// If a configuration for this version does not exist yet, the function scans
// older configurations from the 'SalamanderConfigurationRoots' array (from newest to oldest).
// When it finds one of the configurations, a dialog is shown offering to convert it into the current configuration
// and delete it from the registry. After the last dialog, it returns TRUE and fills
// 'deleteConfigurations' and 'loadConfiguration' according to the user's choice.
// If the user chooses to exit the application, the function returns FALSE.
//

BOOL FindLatestConfiguration(BOOL* deleteConfigurations, const wchar_t*& loadConfiguration)
{
    HKEY hRootKey;
    loadConfiguration = NULL; // we don't want to load any configuration - default values will be used
    int rootIndex = 0;
    const wchar_t* root;
    DWORD saveInProgress; // dummy
    HKEY hCfgKey;
    IRegistry* registry = GetMainWindowRegistry();

    CImportConfigDialog dlg;
    ZeroMemory(dlg.ConfigurationExist, sizeof(dlg.ConfigurationExist)); // none of the configurations found yet
    dlg.DeleteConfigurations = deleteConfigurations;
    dlg.IndexOfConfigurationToLoad = -1;

    BOOL offerImportDlg = FALSE; // if an old configuration or keys exist, offer import

    LoadSaveToRegistryMutex.Enter();

    const std::wstring backup = FormatStrW(L"%ls.backup.63A7CD13", SalamanderConfigurationRoots[0]); // "63A7CD13" prevents the key name from matching the user name
    HKEY backupKey;
    BOOL backupFound = registry != NULL &&
                       registry->OpenKeyRead(HKEY_CURRENT_USER, backup.c_str(), backupKey).success;
    if (backupFound)
    {
        DWORD copyIsOK;
        if (registry->GetDWord(backupKey, SAL_REG_VALUE_COPY_IS_OK_W, copyIsOK).success)
            copyIsOK = 1; // backup is valid
        else
            copyIsOK = 0; // backup is corrupted
        registry->CloseKey(backupKey);
        if (!copyIsOK) // delete the corrupted backup and pretend it never existed (it probably wasn't fully created)
        {
            TRACE_I("Configuration backup is incomplete, removing... " << sally::diagnostic::EncodeAcpLossy(backup));
            if (!registry->DeleteKeyRecursive(HKEY_CURRENT_USER, backup.c_str()).success)
                TRACE_E("FindLatestConfiguration(): unable to delete corrupted backup: " << sally::diagnostic::EncodeAcpLossy(backup));
            backupFound = FALSE;
        }
        else
            TRACE_I("Configuration backup is OK: " << sally::diagnostic::EncodeAcpLossy(backup));
    }

    do
    {
        root = SalamanderConfigurationRoots[rootIndex];
        // check whether the key exists
        BOOL rootFound = registry != NULL &&
                         registry->OpenKeyRead(HKEY_CURRENT_USER, root, hRootKey).success;
        if (rootFound &&
            registry->GetDWord(hRootKey, SALAMANDER_SAVE_IN_PROGRESS, saveInProgress).success)
        { // this configuration is corrupted
            TRACE_E("Configuration is corrupted!");
            rootFound = FALSE;
            registry->CloseKey(hRootKey);
            if (rootIndex == 0 && backupFound) // use the backup, if available and don't bother the user
            {
                const std::wstring corrupted = FormatStrW(L"%ls.corrupted.63A7CD13", root); // "63A7CD13" prevents the key name from matching the user name
                registry->DeleteKeyRecursive(HKEY_CURRENT_USER, corrupted.c_str()); // if we already have a corrupted configuration, remove it-one is enough
                if (MyRegRenameKey(HKEY_CURRENT_USER, root, corrupted.c_str()) &&
                    MyRegRenameKey(HKEY_CURRENT_USER, backup.c_str(), root))
                {
                    backupFound = FALSE;
                    if (registry->CreateKey(HKEY_CURRENT_USER, root, hRootKey).success)
                    {
                        registry->DeleteValue(hRootKey, SAL_REG_VALUE_COPY_IS_OK_W);
                        registry->CloseKey(hRootKey);
                    }
                    TRACE_I("Corrupted configuration was moved to: " << sally::diagnostic::EncodeAcpLossy(corrupted));
                    TRACE_I("Using configuration backup instead ...");
                    continue; // in the second pass load configuration from the backup created during "critical shutdown"
                }
                else
                    TRACE_E("Unable to move corrupted configuration or configuration backup.");
            }

            if (rootIndex == 0) // for the active version inform the user about the corrupted configuration and let them back up the key, then try to delete it (older versions - simply ignore the corrupted configuration)
            {
                const std::wstring message = FormatStrW(LoadStrW(IDS_CORRUPTEDCONFIGFOUND), root);
                LoadSaveToRegistryMutex.Leave();

                MSGBOXEX_PARAMS params;
                memset(&params, 0, sizeof(params));
                params.HParent = NULL;
                params.Flags = MB_OKCANCEL | MB_ICONERROR | MB_DEFBUTTON2;
                params.Caption = SALAMANDER_TEXT_VERSIONW();
                params.Text = message.c_str();
                /* used by the export_mnu.py script that generates salmenu.mnu for the Translator
   we let the message box buttons handle hotkey collisions by simulating it as a menu
MENU_TEMPLATE_ITEM MsgBoxButtons[] =
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_CORRUPTEDCONFIGREMOVEBTN
  {MNTT_IT, IDS_SELLANGEXITBUTTON
  {MNTT_PE, 0
};
*/
                const std::wstring aliasBtnNames = FormatStrW(L"%d\t%ls\t%d\t%ls", DIALOG_OK, LoadStrW(IDS_CORRUPTEDCONFIGREMOVEBTN),
                                                             DIALOG_CANCEL, LoadStrW(IDS_SELLANGEXITBUTTON));
                params.AliasBtnNames = aliasBtnNames.c_str();
                if (SalMessageBoxEx(&params) == IDCANCEL)
                {
                    CheckShutdownParams(); // optionally show this warning; if they rename the key in the registry they might never see it
                    return FALSE;          // Exit
                }

                CheckShutdownParams();
                LoadSaveToRegistryMutex.Enter();
                if (registry != NULL &&
                    registry->OpenKeyReadWrite(HKEY_CURRENT_USER, root, hRootKey).success)
                { // delete the corrupted configuration (if it's still there - user might have renamed it for backup)
                    TRACE_I("Deleting corrupted configuration on user demand: " << sally::diagnostic::EncodeAcpLossy(root));
                    ClearRegistryKeyTree(registry, hRootKey);
                    registry->CloseKey(hRootKey);
                    registry->DeleteKeyRecursive(HKEY_CURRENT_USER, root);
                }
            }
        }
        BOOL cfgFound = rootFound &&
                        registry->OpenKeyRead(hRootKey, SALAMANDER_CONFIG_REG, hCfgKey).success;
        if (rootFound)
            registry->CloseKey(hRootKey);

        if (rootIndex == 0 && backupFound) // backup not needed, remove it
        {
            TRACE_I("Removing unnecessary configuration backup: " << sally::diagnostic::EncodeAcpLossy(backup));
            if (!registry->DeleteKeyRecursive(HKEY_CURRENT_USER, backup.c_str()).success)
                TRACE_E("FindLatestConfiguration(): unable to remove unnecessary backup: " << sally::diagnostic::EncodeAcpLossy(backup));
            backupFound = FALSE;
        }

        if (cfgFound) // a key is considered a configuration key only if the "Configuration" subkey exists (mere existence of the key isn't enough because it may contain only "AutoImportConfig")
        {
            registry->CloseKey(hCfgKey);
            if (rootIndex == 0)
            {
                // this is the key for the active program version => confirm loading it and return
                loadConfiguration = root;
                LoadSaveToRegistryMutex.Leave();
                return TRUE;
            }
            // this is one of the older keys

            // offer this configuration for import and deletion
            dlg.ConfigurationExist[rootIndex] = TRUE;
            offerImportDlg = TRUE;
        }
        rootIndex++;
    } while (rootIndex < SALCFG_ROOTS_COUNT);

    LoadSaveToRegistryMutex.Leave();

    if (offerImportDlg)
    {
        HWND hSplash = GetSplashScreenHandle(); // if a splash screen exists, temporarily hide it
        if (hSplash != NULL)
            ShowWindow(hSplash, SW_HIDE);

        int dlgRet = (int)dlg.Execute();

        if (hSplash != NULL)
        {
            ShowWindow(hSplash, SW_SHOW);
            UpdateWindow(hSplash);
        }

        if (dlgRet == IDCANCEL)
        {
            return FALSE; // user wants to quit Salamander
        }
        if (dlg.IndexOfConfigurationToLoad != -1)
            loadConfiguration = SalamanderConfigurationRoots[dlg.IndexOfConfigurationToLoad];
    }
    return TRUE;
}

// deletes keys according to the array returned by FindLatestConfiguration

void CMainWindow::DeleteOldConfigurations(BOOL* deleteConfigurations, BOOL autoImportConfig,
                                          const wchar_t* autoImportConfigFromKey,
                                          BOOL doNotDeleteImportedCfg)
{
    // anything to delete?
    BOOL dirty = FALSE;
    if (autoImportConfig)
        dirty = TRUE;
    else
    {
        int rootIndex;
        for (rootIndex = 0; rootIndex < SALCFG_ROOTS_COUNT; rootIndex++)
        {
            if (deleteConfigurations[rootIndex])
            {
                dirty = TRUE;
                break;
            }
        }
    }
    if (dirty)
    {
        // remove old configurations
        HCURSOR hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
        CWaitWindow analysing(HWindow, IDS_DELETINGCONFIGURATION, FALSE, ooStatic);
        analysing.Create();
        EnableWindow(HWindow, FALSE);
        LoadSaveToRegistryMutex.Enter();
        IRegistry* registry = GetMainWindowRegistry();
        int rootIndex;
        for (rootIndex = 0; rootIndex < SALCFG_ROOTS_COUNT; rootIndex++)
        {
            if (deleteConfigurations[rootIndex])
            {
                HKEY hKey;
                const wchar_t* key = SalamanderConfigurationRoots[rootIndex];
                if (registry != NULL &&
                    registry->CreateKey(HKEY_CURRENT_USER, key, hKey).success)
                {
                    ClearRegistryKeyTree(registry, hKey);
                    registry->CloseKey(hKey);
                    registry->DeleteKeyRecursive(HKEY_CURRENT_USER, key);
                }
            }
        }
        if (autoImportConfig) // clean old configuration (already stored in the new key) and remove "AutoImportConfig" from the new key
        {
            BOOL ok = FALSE;
            HKEY cfgKey;
            if (registry != NULL &&
                registry->OpenKeyReadWrite(HKEY_CURRENT_USER, SalamanderConfigurationRoots[0], cfgKey).success)
            { // remove "AutoImportConfig" value from the new key
                if (registry->DeleteValue(cfgKey, SAL_REG_VALUE_AUTO_IMPORT_CONFIG_W).success)
                    ok = TRUE;
                registry->CloseKey(cfgKey);
            }
            if (!ok) // if this happens it's probably fine because we likely didn't
                     // write Salamander's configuration either (it goes to the
                     // same key) and the whole upgrade will need to be run again
            {
                TRACE_E("CMainWindow::DeleteOldConfigurations(): unable to delete AutoImportConfig value from HKCU\\" << sally::diagnostic::EncodeAcpLossy(SalamanderConfigurationRoots[0]));
            }
            else // clean the old configuration (already saved to the new key)
            {
                if (!doNotDeleteImportedCfg)
                {
                    if (registry != NULL &&
                        registry->OpenKeyReadWrite(HKEY_CURRENT_USER, autoImportConfigFromKey, cfgKey).success)
                    {
                        ClearRegistryKeyTree(registry, cfgKey);
                        registry->CloseKey(cfgKey);
                        registry->DeleteKeyRecursive(HKEY_CURRENT_USER, autoImportConfigFromKey);
                    }
                }
            }
        }
        LoadSaveToRegistryMutex.Leave();
        EnableWindow(HWindow, TRUE);
        DestroyWindow(analysing.HWindow);
        SetCursor(hOldCursor);
    }
}

//
// ****************************************************************************
// CMainWindow
//

void CMainWindow::SavePanelConfig(CFilesWindow* panel, HKEY hSalamander, const wchar_t* reg)
{
    // FIRST REGION ON THE WIDE FACADES. Every registry call here goes
    // through the *W entry points, so nothing in this function narrows a key or
    // value name. The pattern established here is what the remaining regions of
    // this file follow.
    HKEY actKey;
    if (CreateKeyW(hSalamander, reg, actKey))
    {
        DWORD value;
        value = panel->HeaderLineVisible;
        SetValueW(actKey, PANEL_HEADER_REG_W, REG_DWORD, &value, sizeof(DWORD));
        // Wide-only persistence: write the panel path as REG_SZ Unicode so Unicode-only
        // disk roots (e.g. C:\Temp\zz中文) survive across restart. Older Sally builds
        // reading this config will not find a usable PANEL_PATH and fall back to the
        // rescue path on first launch (then resave). User-approved clean break.
        //
        // The AnsiToWideReg that used to wrap the value name here is GONE: the name
        // is a wide constant now, so there is no conversion left to get wrong.
        if (gRegistry != NULL)
        {
            gRegistry->SetString(actKey, PANEL_PATH_REG_W, panel->GetPathW());
        }
        else
        {
            // gRegistry should always be set in the running app; this is a defensive
            // fallback so a misconfigured test harness still records something.
            SetValueW(actKey, PANEL_PATH_REG_W, REG_SZ, panel->GetPathW(), -1);
        }
        value = panel->GetViewTemplateIndex();
        SetValueW(actKey, PANEL_VIEW_REG_W, REG_DWORD, &value, sizeof(DWORD));
        value = panel->SortType;
        SetValueW(actKey, PANEL_SORT_REG_W, REG_DWORD, &value, sizeof(DWORD));
        value = panel->ReverseSort;
        SetValueW(actKey, PANEL_REVERSE_REG_W, REG_DWORD, &value, sizeof(DWORD));
        value = (panel->DirectoryLine->HWindow != NULL);
        SetValueW(actKey, PANEL_DIRLINE_REG_W, REG_DWORD, &value, sizeof(DWORD));
        value = (panel->StatusLine->HWindow != NULL);
        SetValueW(actKey, PANEL_STATUS_REG_W, REG_DWORD, &value, sizeof(DWORD));
        SetValueW(actKey, PANEL_FILTER_ENABLE_W, REG_DWORD, &panel->FilterEnabled,
                  sizeof(DWORD));
        // CMaskGroup::GetMasksString() has returned the wide storage directly since
        // - no ANSI rendering left to bridge.
        SetValueW(actKey, PANEL_FILTER_W, REG_SZ, panel->Filter.GetMasksString(), -1);

        CloseKey(actKey);
    }
}

void CMainWindow::SaveConfig(HWND parent)
{
    CALL_STACK_MESSAGE1("CMainWindow::SaveConfig()");

    if (parent == NULL)
        parent = HWindow;

    if (SALAMANDER_ROOT_REG == NULL)
    {
        TRACE_E("SALAMANDER_ROOT_REG == NULL"); // not necessarily an error: during UPGRADE we may exit Salamander without saving the configuration (if not all plug-ins are installed and the user chooses Exit)
        return;
    }

    HCURSOR hOldCursor = NULL;
    if (GlobalSaveWaitWindow == NULL)
        hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    CWaitWindow analysing(parent, IDS_SAVINGCONFIGURATION, FALSE, ooStatic, TRUE);
    int savingProgress = 0;
    HWND oldPluginMsgBoxParent = PluginMsgBoxParent;
    if (GlobalSaveWaitWindow == NULL)
    {
        //TRACE_I("analysing.SetProgressPos() savingProgress="<<savingProgress);
        analysing.SetProgressMax(7 /* MUST BE SYNCHRONIZED with CMainWindow::WindowProc::WM_USER_CLOSE_MAINWND !!! */); // one less so they can enjoy looking at 100%
        analysing.Create();
        EnableWindow(parent, FALSE);

        // SaveConfiguration plug-ins will be invoked as well -> set the parent for their message boxes
        PluginMsgBoxParent = analysing.HWindow;
    }

    LoadSaveToRegistryMutex.Enter();

    HKEY salamander;
    if (CreateKey(HKEY_CURRENT_USER, SALAMANDER_ROOT_REG, salamander))
    {
        HKEY actKey;

        BOOL cfgIsOK = TRUE;
        BOOL deleteSALAMANDER_SAVE_IN_PROGRESS = !IsSetSALAMANDER_SAVE_IN_PROGRESS;
        if (deleteSALAMANDER_SAVE_IN_PROGRESS)
        {
            IRegistry* registry = GetMainWindowRegistry();
            DWORD saveInProgress = 1;
            if (registry != NULL &&
                registry->GetDWord(salamander, SALAMANDER_SAVE_IN_PROGRESS, saveInProgress).success)
            {
                cfgIsOK = FALSE; // the configuration is corrupted; saving won't fix it (it wasn't stored completely)
                TRACE_E("CMainWindow::SaveConfig(): unable to save configuration, configuration key in registry is corrupted");
            }
            else
            {
                saveInProgress = 1;
                SetValue(salamander, SALAMANDER_SAVE_IN_PROGRESS, REG_DWORD, &saveInProgress, sizeof(DWORD));
                IsSetSALAMANDER_SAVE_IN_PROGRESS = TRUE;
            }
        }

        if (cfgIsOK)
        {
            //--- version
            if (CreateKey(salamander, SALAMANDER_VERSION_REG, actKey))
            {
                DWORD newConfigVersion = THIS_CONFIG_VERSION;
                SetValueW(actKey, SALAMANDER_VERSIONREG_REG_W, REG_DWORD,
                         &newConfigVersion, sizeof(DWORD));
                CloseKey(actKey);
            }

            //---  window

            if (CreateKey(salamander, SALAMANDER_WINDOW_REG, actKey))
            {
                WINDOWPLACEMENT place;
                place.length = sizeof(WINDOWPLACEMENT);
                GetWindowPlacement(HWindow, &place);
                SetValueW(actKey, WINDOW_LEFT_REG_W, REG_DWORD,
                         &(place.rcNormalPosition.left), sizeof(DWORD));
                SetValueW(actKey, WINDOW_RIGHT_REG_W, REG_DWORD,
                         &(place.rcNormalPosition.right), sizeof(DWORD));
                SetValueW(actKey, WINDOW_TOP_REG_W, REG_DWORD,
                         &(place.rcNormalPosition.top), sizeof(DWORD));
                SetValueW(actKey, WINDOW_BOTTOM_REG_W, REG_DWORD,
                         &(place.rcNormalPosition.bottom), sizeof(DWORD));
                SetValueW(actKey, WINDOW_SHOW_REG_W, REG_DWORD,
                         &(place.showCmd), sizeof(DWORD));
                // WIDE, not narrow. WINDOW_SPLIT_REG is a const wchar_t*, so
                // this resolved to the WIDE-name SetValue overload, which forwards dataSize==-1
                // to SetValueAux's wcslen()-based auto-length - correct only for wide data. The
                // buffer here used to be char[20], so wcslen() scanned narrow bytes ("50.0\0",
                // a single trailing zero BYTE, not the two consecutive zero bytes wcslen looks
                // for) straight past the end into uninitialized stack fill until it happened to
                // hit a wide NUL, then wrote that garbage length. Observed live as a 515-char
                // REG_SZ of "50.0" followed by 0xCC debug-fill, surfacing on the next startup as
                // "Error Loading Configuration (234) More data is available" - twice, once per
                // value. Note the payload also has to be genuinely wide, not merely correctly
                // measured: RegSetValueExW tags REG_SZ as UTF-16, so narrow bytes stored here
                // would round-trip only because the matching read was equally narrow, and would
                // render as mojibake in regedit and to any correct future reader.
                wchar_t buf[20];
                swprintf_s(buf, _countof(buf), L"%.1lf", SplitPosition * 100);
                SetValueW(actKey, WINDOW_SPLIT_REG, REG_SZ, buf, -1);
                swprintf_s(buf, _countof(buf), L"%.1lf", BeforeZoomSplitPosition * 100);
                SetValueW(actKey, WINDOW_BEFOREZOOMSPLIT_REG, REG_SZ, buf, -1);

                CloseKey(actKey);
            }

            if (Configuration.FindDialogWindowPlacement.length != 0)
            {
                if (CreateKey(salamander, FINDDIALOG_WINDOW_REG, actKey))
                {
                    SetValueW(actKey, WINDOW_LEFT_REG_W, REG_DWORD,
                             &(Configuration.FindDialogWindowPlacement.rcNormalPosition.left), sizeof(DWORD));
                    SetValueW(actKey, WINDOW_RIGHT_REG_W, REG_DWORD,
                             &(Configuration.FindDialogWindowPlacement.rcNormalPosition.right), sizeof(DWORD));
                    SetValueW(actKey, WINDOW_TOP_REG_W, REG_DWORD,
                             &(Configuration.FindDialogWindowPlacement.rcNormalPosition.top), sizeof(DWORD));
                    SetValueW(actKey, WINDOW_BOTTOM_REG_W, REG_DWORD,
                             &(Configuration.FindDialogWindowPlacement.rcNormalPosition.bottom), sizeof(DWORD));
                    SetValueW(actKey, WINDOW_SHOW_REG_W, REG_DWORD,
                             &(Configuration.FindDialogWindowPlacement.showCmd), sizeof(DWORD));

                    SetValueW(actKey, FINDDIALOG_NAMEWIDTH_REG_W, REG_DWORD,
                             &(Configuration.FindColNameWidth), sizeof(DWORD));
                    CloseKey(actKey);
                }
            }

            //---  left and right panel

            SavePanelConfig(LeftPanel, salamander, SALAMANDER_LEFTP_REG_W);
            SavePanelConfig(RightPanel, salamander, SALAMANDER_RIGHTP_REG_W);

            //---  default directories

            if (CreateKey(salamander, SALAMANDER_DEFDIRS_REG, actKey))
            {
                wchar_t name[2];
                name[1] = 0;
                wchar_t d;
                for (d = 'A'; d <= 'Z'; d++)
                {
                    name[0] = d;
                    const wchar_t* path = DefaultDir[d - 'A'].c_str();
                    if (path[1] == L':' && path[2] == L'\\' && path[3] != 0) // not "C:\"
                        // SetValueW, not SetValue: the narrow one takes const void* and turns
                        // dataSize -1 into strlen(), which on a wide path measures ONE character.
                        SetValueW(actKey, name, REG_SZ, path, -1);
                    else
                        DeleteValueW(actKey, name);
                }
                CloseKey(actKey);
            }

            //---  password manager

            if (CreateKey(salamander, SALAMANDER_PWDMNGR_REG, actKey))
            {
                PasswordManager.Save(actKey);
                CloseKey(actKey);
            }

            //---  hot paths

            if (CreateKey(salamander, SALAMANDER_HOTPATHS_REG, actKey))
            {
                HotPaths.Save(actKey);
                CloseKey(actKey);
            }

            //--- view templates

            if (CreateKey(salamander, SALAMANDER_VIEWTEMPLATES_REG, actKey))
            {
                ViewTemplates.Save(actKey);
                CloseKey(actKey);
            }

            //---  Plugins
            HKEY configKey;
            HKEY orderKey;
            if (CreateKey(salamander, SALAMANDER_PLUGINS, actKey) &&
                CreateKey(salamander, SALAMANDER_PLUGINSCONFIG, configKey) &&
                CreateKey(salamander, SALAMANDER_PLUGINSORDER, orderKey))
            {
                Plugins.Save(parent, actKey, configKey, orderKey);
                CloseKey(orderKey);
                CloseKey(actKey);
                CloseKey(configKey);
            }

            if (GlobalSaveWaitWindow == NULL)
                analysing.SetProgressPos(++savingProgress); // 1
            else
                GlobalSaveWaitWindow->SetProgressPos(++GlobalSaveWaitWindowProgress); // 1
            //TRACE_I("analysing.SetProgressPos() savingProgress="<<savingProgress);

            //---  Packers & Unpackers
            if (CreateKey(salamander, SALAMANDER_PACKANDUNPACK, actKey))
            {
                SetValueW(actKey, SALAMANDER_SIMPLEICONSINARCHIVES_W, REG_DWORD,
                         &(Configuration.UseSimpleIconsInArchives), sizeof(DWORD));

                //---  Custom Packers
                HKEY actSubKey;
                if (CreateKey(actKey, SALAMANDER_CUSTOMPACKERS, actSubKey))
                {
                    ClearKey(actSubKey);
                    HKEY itemKey;
                    wchar_t buf[30];
                    int i;
                    for (i = 0; i < PackerConfig.GetPackersCount(); i++)
                    {
                        _itow_s(i + 1, buf, _countof(buf), 10);
                        if (CreateKey(actSubKey, buf, itemKey))
                        {
                            PackerConfig.Save(i, itemKey);
                            CloseKey(itemKey);
                        }
                        else
                            break;
                    }
                    SetValue(actSubKey, SALAMANDER_ANOTHERPANEL, REG_DWORD,
                             &(Configuration.UseAnotherPanelForPack), sizeof(DWORD));
                    int pp = PackerConfig.GetPreferedPacker();
                    SetValue(actSubKey, SALAMANDER_PREFFERED, REG_DWORD, &pp, sizeof(DWORD));
                    CloseKey(actSubKey);
                }

                if (GlobalSaveWaitWindow == NULL)
                    analysing.SetProgressPos(++savingProgress); // 2
                else
                    GlobalSaveWaitWindow->SetProgressPos(++GlobalSaveWaitWindowProgress); // 2
                //TRACE_I("analysing.SetProgressPos() savingProgress="<<savingProgress);

                //---  Custom Unpackers
                if (CreateKey(actKey, SALAMANDER_CUSTOMUNPACKERS, actSubKey))
                {
                    ClearKey(actSubKey);
                    HKEY itemKey;
                    wchar_t buf[30];
                    int i;
                    for (i = 0; i < UnpackerConfig.GetUnpackersCount(); i++)
                    {
                        _itow_s(i + 1, buf, _countof(buf), 10);
                        if (CreateKey(actSubKey, buf, itemKey))
                        {
                            UnpackerConfig.Save(i, itemKey);
                            CloseKey(itemKey);
                        }
                        else
                            break;
                    }
                    SetValue(actSubKey, SALAMANDER_ANOTHERPANEL, REG_DWORD,
                             &(Configuration.UseAnotherPanelForUnpack), sizeof(DWORD));
                    SetValue(actSubKey, SALAMANDER_NAMEBYARCHIVE, REG_DWORD,
                             &(Configuration.UseSubdirNameByArchiveForUnpack), sizeof(DWORD));
                    int pp = UnpackerConfig.GetPreferedUnpacker();
                    SetValue(actSubKey, SALAMANDER_PREFFERED, REG_DWORD, &pp, sizeof(DWORD));
                    CloseKey(actSubKey);
                }

                if (GlobalSaveWaitWindow == NULL)
                    analysing.SetProgressPos(++savingProgress); // 3
                else
                    GlobalSaveWaitWindow->SetProgressPos(++GlobalSaveWaitWindowProgress); // 3
                //TRACE_I("analysing.SetProgressPos() savingProgress="<<savingProgress);

                //---  Predefined Packers
                if (CreateKey(actKey, SALAMANDER_PREDPACKERS, actSubKey))
                {
                    ClearKey(actSubKey);
                    HKEY itemKey;
                    wchar_t buf[30];
                    int i;
                    for (i = 0; i < ArchiverConfig.GetArchiversCount(); i++)
                    {
                        _itow_s(i + 1, buf, _countof(buf), 10);
                        if (CreateKey(actSubKey, buf, itemKey))
                        {
                            ArchiverConfig.Save(i, itemKey);
                            CloseKey(itemKey);
                        }
                        else
                            break;
                    }
                    CloseKey(actSubKey);
                }

                //---  Archive Association
                if (CreateKey(actKey, SALAMANDER_ARCHIVEASSOC, actSubKey))
                {
                    ClearKey(actSubKey);
                    HKEY itemKey;
                    wchar_t buf[30];
                    int i;
                    for (i = 0; i < PackerFormatConfig.GetFormatsCount(); i++)
                    {
                        _itow_s(i + 1, buf, _countof(buf), 10);
                        if (CreateKey(actSubKey, buf, itemKey))
                        {
                            PackerFormatConfig.Save(i, itemKey);
                            CloseKey(itemKey);
                        }
                        else
                            break;
                    }
                    CloseKey(actSubKey);
                }

                CloseKey(actKey);
            }

            //---  configuration

            if (CreateKey(salamander, SALAMANDER_CONFIG_REG, actKey))
            {
                //---  top rebar begin
                SetValueW(actKey, CONFIG_MENUINDEX_REG_W, REG_DWORD,
                         &Configuration.MenuIndex, sizeof(DWORD));
                SetValueW(actKey, CONFIG_MENUBREAK_REG_W, REG_DWORD,
                         &Configuration.MenuBreak, sizeof(DWORD));
                SetValueW(actKey, CONFIG_MENUWIDTH_REG_W, REG_DWORD,
                         &Configuration.MenuWidth, sizeof(DWORD));
                SetValueW(actKey, CONFIG_TOOLBARINDEX_REG_W, REG_DWORD,
                         &Configuration.TopToolbarIndex, sizeof(DWORD));
                SetValueW(actKey, CONFIG_TOOLBARBREAK_REG_W, REG_DWORD,
                         &Configuration.TopToolbarBreak, sizeof(DWORD));
                SetValueW(actKey, CONFIG_TOOLBARWIDTH_REG_W, REG_DWORD,
                         &Configuration.TopToolbarWidth, sizeof(DWORD));
                SetValueW(actKey, CONFIG_PLUGINSBARINDEX_REG_W, REG_DWORD,
                         &Configuration.PluginsBarIndex, sizeof(DWORD));
                SetValueW(actKey, CONFIG_PLUGINSBARBREAK_REG_W, REG_DWORD,
                         &Configuration.PluginsBarBreak, sizeof(DWORD));
                SetValueW(actKey, CONFIG_PLUGINSBARWIDTH_REG_W, REG_DWORD,
                         &Configuration.PluginsBarWidth, sizeof(DWORD));
                SetValueW(actKey, CONFIG_USERMENUINDEX_REG_W, REG_DWORD,
                         &Configuration.UserMenuToolbarIndex, sizeof(DWORD));
                SetValueW(actKey, CONFIG_USERMENUBREAK_REG_W, REG_DWORD,
                         &Configuration.UserMenuToolbarBreak, sizeof(DWORD));
                SetValueW(actKey, CONFIG_USERMENUWIDTH_REG_W, REG_DWORD,
                         &Configuration.UserMenuToolbarWidth, sizeof(DWORD));
                SetValueW(actKey, CONFIG_USERMENULABELS_REG_W, REG_DWORD,
                         &Configuration.UserMenuToolbarLabels, sizeof(DWORD));
                SetValueW(actKey, CONFIG_HOTPATHSINDEX_REG_W, REG_DWORD,
                         &Configuration.HotPathsBarIndex, sizeof(DWORD));
                SetValueW(actKey, CONFIG_HOTPATHSBREAK_REG_W, REG_DWORD,
                         &Configuration.HotPathsBarBreak, sizeof(DWORD));
                SetValueW(actKey, CONFIG_HOTPATHSWIDTH_REG_W, REG_DWORD,
                         &Configuration.HotPathsBarWidth, sizeof(DWORD));
                SetValueW(actKey, CONFIG_DRIVEBARINDEX_REG_W, REG_DWORD,
                         &Configuration.DriveBarIndex, sizeof(DWORD));
                SetValueW(actKey, CONFIG_DRIVEBARBREAK_REG_W, REG_DWORD,
                         &Configuration.DriveBarBreak, sizeof(DWORD));
                SetValueW(actKey, CONFIG_DRIVEBARWIDTH_REG_W, REG_DWORD,
                         &Configuration.DriveBarWidth, sizeof(DWORD));
                SetValueW(actKey, CONFIG_GRIPSVISIBLE_REG_W, REG_DWORD,
                         &Configuration.GripsVisible, sizeof(DWORD));

                //---  top rebar end
                SetValueW(actKey, CONFIG_FILENAMEFORMAT_REG_W, REG_DWORD,
                         &Configuration.FileNameFormat, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SIZEFORMAT_REG_W, REG_DWORD,
                         &Configuration.SizeFormat, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SELECTION_REG_W, REG_DWORD,
                         &Configuration.IncludeDirs, sizeof(DWORD));
                SetValueW(actKey, CONFIG_COPYFINDTEXT_REG_W, REG_DWORD,
                         &Configuration.CopyFindText, sizeof(DWORD));
                SetValueW(actKey, CONFIG_CLEARREADONLY_REG_W, REG_DWORD,
                         &Configuration.ClearReadOnly, sizeof(DWORD));
                SetValueW(actKey, CONFIG_PRIMARYCONTEXTMENU_REG_W, REG_DWORD,
                         &Configuration.PrimaryContextMenu, sizeof(DWORD));
                SetValueW(actKey, CONFIG_NOTHIDDENSYSTEM_REG_W, REG_DWORD,
                         &Configuration.NotHiddenSystemFiles, sizeof(DWORD));
                SetValueW(actKey, CONFIG_RECYCLEBIN_REG_W, REG_DWORD,
                         &Configuration.UseRecycleBin, sizeof(DWORD));
                SetValue(actKey, CONFIG_RECYCLEMASKS_REG, REG_SZ,
                         Configuration.RecycleMasks.GetMasksString(), -1);
                SetValueW(actKey, CONFIG_SAVEONEXIT_REG_W, REG_DWORD,
                         &Configuration.AutoSave, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SHOWGREPERRORS_REG_W, REG_DWORD,
                         &Configuration.ShowGrepErrors, sizeof(DWORD));
                SetValueW(actKey, CONFIG_FINDFULLROW_REG_W, REG_DWORD,
                         &Configuration.FindFullRowSelect, sizeof(DWORD));
                SetValueW(actKey, CONFIG_FINDFILETYPEMODE_REG_W, REG_DWORD,
                         &Configuration.FindFileTypeMode, sizeof(DWORD));
                SetValueW(actKey, CONFIG_MINBEEPWHENDONE_REG_W, REG_DWORD,
                         &Configuration.MinBeepWhenDone, sizeof(DWORD));
                SetValueW(actKey, CONFIG_CLOSESHELL_REG_W, REG_DWORD,
                         &Configuration.CloseShell, sizeof(DWORD));
                DWORD rightPanelFocused = (GetActivePanel() == RightPanel);
                SetValueW(actKey, CONFIG_RIGHT_FOCUS_REG_W, REG_DWORD,
                         &rightPanelFocused, sizeof(DWORD));
                SetValueW(actKey, CONFIG_ALWAYSONTOP_REG_W, REG_DWORD,
                         &Configuration.AlwaysOnTop, sizeof(DWORD));
                SetValueW(actKey, CONFIG_COMMANDSHELL_KIND_REG_W, REG_DWORD,
                         &Configuration.CommandShellTargetKind, sizeof(DWORD));
                if (gRegistry != NULL)
                {
                    gRegistry->SetString(actKey, CONFIG_COMMANDSHELL_PROFILE_GUID_REG_W,
                                         Configuration.CommandShellProfileGuid.c_str());
                    gRegistry->SetString(actKey, CONFIG_COMMANDSHELL_PROFILE_NAME_REG_W,
                                         Configuration.CommandShellProfileName.c_str());
                }
                //      SetValue(actKey, CONFIG_FASTDIRMOVE_REG, REG_DWORD,
                //               &Configuration.FastDirectoryMove, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SORTUSESLOCALE_REG_W, REG_DWORD,
                         &Configuration.SortUsesLocale, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SORTDETECTNUMBERS_REG_W, REG_DWORD,
                         &Configuration.SortDetectNumbers, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SORTNEWERONTOP_REG_W, REG_DWORD,
                         &Configuration.SortNewerOnTop, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SORTDIRSBYNAME_REG_W, REG_DWORD,
                         &Configuration.SortDirsByName, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SORTDIRSBYEXT_REG_W, REG_DWORD,
                         &Configuration.SortDirsByExt, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SAVEHISTORY_REG_W, REG_DWORD,
                         &Configuration.SaveHistory, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SAVEWORKDIRS_REG_W, REG_DWORD,
                         &Configuration.SaveWorkDirs, sizeof(DWORD));
                SetValueW(actKey, CONFIG_ENABLECMDLINEHISTORY_REG_W, REG_DWORD,
                         &Configuration.EnableCmdLineHistory, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SAVECMDLINEHISTORY_REG_W, REG_DWORD,
                         &Configuration.SaveCmdLineHistory, sizeof(DWORD));
                //      SetValue(actKey, CONFIG_LANTASTICCHECK_REG, REG_DWORD,
                //               &Configuration.LantasticCheck, sizeof(DWORD));
                SetValueW(actKey, CONFIG_ONLYONEINSTANCE_REG_W, REG_DWORD,
                         &Configuration.OnlyOneInstance, sizeof(DWORD));
                SetValueW(actKey, CONFIG_STATUSAREA_REG_W, REG_DWORD,
                         &Configuration.StatusArea, sizeof(DWORD));
                SetValueW(actKey, CONFIG_FULLROWSELECT_REG_W, REG_DWORD,
                         &Configuration.FullRowSelect, sizeof(DWORD));
                SetValueW(actKey, CONFIG_FULLROWHIGHLIGHT_REG_W, REG_DWORD,
                         &Configuration.FullRowHighlight, sizeof(DWORD));
                SetValueW(actKey, CONFIG_USEICONTINCTURE_REG_W, REG_DWORD,
                         &Configuration.UseIconTincture, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SHOWPANELCAPTION_REG_W, REG_DWORD,
                         &Configuration.ShowPanelCaption, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SHOWPANELZOOM_REG_W, REG_DWORD,
                         &Configuration.ShowPanelZoom, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SINGLECLICK_REG_W, REG_DWORD,
                         &Configuration.SingleClick, sizeof(DWORD));
                //      SetValue(actKey, CONFIG_SHOWTIPOFTHEDAY_REG, REG_DWORD,
                //               &Configuration.ShowTipOfTheDay, sizeof(DWORD));
                //      SetValue(actKey, CONFIG_LASTTIPOFTHEDAY_REG, REG_DWORD,
                //               &Configuration.LastTipOfTheDay, sizeof(DWORD));
                SetValueW(actKey, CONFIG_INFOLINECONTENT_REG, REG_SZ,
                          Configuration.InfoLineContent.c_str(), -1);
                SetValueW(actKey, CONFIG_IFPATHISINACCESSIBLEGOTOISMYDOCS_REG_W, REG_DWORD,
                         &Configuration.IfPathIsInaccessibleGoToIsMyDocs, sizeof(DWORD));
                SetValueW(actKey, CONFIG_IFPATHISINACCESSIBLEGOTO_REG, REG_SZ,
                          Configuration.IfPathIsInaccessibleGoTo.c_str(), -1);
                SetValueW(actKey, CONFIG_HOTPATH_AUTOCONFIG_W, REG_DWORD,
                         &Configuration.HotPathAutoConfig, sizeof(DWORD));
                SetValueW(actKey, CONFIG_LASTUSEDSPEEDLIM_REG_W, REG_DWORD,
                         &Configuration.LastUsedSpeedLimit, sizeof(DWORD));
                SetValueW(actKey, CONFIG_QUICKSEARCHENTER_REG_W, REG_DWORD,
                         &Configuration.QuickSearchEnterAlt, sizeof(DWORD));
                SetValueW(actKey, CONFIG_CHD_SHOWMYDOC_W, REG_DWORD,
                         &Configuration.ChangeDriveShowMyDoc, sizeof(DWORD));
                SetValueW(actKey, CONFIG_CHD_SHOWCLOUDSTOR_W, REG_DWORD,
                         &Configuration.ChangeDriveCloudStorage, sizeof(DWORD));
                SetValueW(actKey, CONFIG_CHD_SHOWANOTHER_W, REG_DWORD,
                         &Configuration.ChangeDriveShowAnother, sizeof(DWORD));
                SetValueW(actKey, CONFIG_CHD_SHOWNET_W, REG_DWORD,
                         &Configuration.ChangeDriveShowNet, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SEARCHFILECONTENT_W, REG_DWORD,
                         &Configuration.SearchFileContent, sizeof(DWORD));
                SetValueW(actKey, CONFIG_LASTPLUGINVER_W, REG_DWORD,
                         &Configuration.LastPluginVer, sizeof(DWORD));
                SetValueW(actKey, CONFIG_LASTPLUGINVER_OP_W, REG_DWORD,
                         &Configuration.LastPluginVerOP, sizeof(DWORD));
                SetValueW(actKey, CONFIG_NETWAREFASTDIRMOVE_REG_W, REG_DWORD,
                         &Configuration.NetwareFastDirMove, sizeof(DWORD));
                if (Windows7AndLater)
                    SetValueW(actKey, CONFIG_ASYNCCOPYALG_REG_W, REG_DWORD,
                             &Configuration.UseAsyncCopyAlg, sizeof(DWORD));
                SetValueW(actKey, CONFIG_RELOAD_ENV_VARS_REG_W, REG_DWORD,
                         &Configuration.ReloadEnvVariables, sizeof(DWORD));
                SetValueW(actKey, CONFIG_QUICKRENAME_SELALL_REG_W, REG_DWORD,
                         &Configuration.QuickRenameSelectAll, sizeof(DWORD));
                SetValueW(actKey, CONFIG_EDITNEW_SELALL_REG_W, REG_DWORD,
                         &Configuration.EditNewSelectAll, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SHIFTFORHOTPATHS_REG_W, REG_DWORD,
                         &Configuration.ShiftForHotPaths, sizeof(DWORD));
                SetValue(actKey, CONFIG_LANGUAGE_REG, REG_SZ,
                         Configuration.SLGName.c_str(), -1);
                SetValueW(actKey, CONFIG_USEALTLANGFORPLUGINS_REG_W, REG_DWORD,
                         &Configuration.UseAsAltSLGInOtherPlugins, sizeof(DWORD));
                SetValue(actKey, CONFIG_ALTLANGFORPLUGINS_REG, REG_SZ,
                         Configuration.AltPluginSLGName.c_str(), -1);
                DWORD langChanged = (StrICmpW(Configuration.SLGName.c_str(), Configuration.LoadedSLGName.c_str()) != 0); // TRUE if user changed Salamander language
                SetValueW(actKey, CONFIG_LANGUAGECHANGED_REG_W, REG_DWORD, &langChanged, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SHOWSPLASHSCREEN_REG_W, REG_DWORD,
                         &Configuration.ShowSplashScreen, sizeof(DWORD));
                SetValue(actKey, CONFIG_CONVERSIONTABLE_REG, REG_SZ,
                         Configuration.ConversionTable.c_str(), -1);
                SetValueW(actKey, CONFIG_SKILLLEVEL_REG_W, REG_DWORD,
                         &Configuration.SkillLevel, sizeof(DWORD));
                SetValueW(actKey, CONFIG_TITLEBARSHOWPATH_REG_W, REG_DWORD,
                         &Configuration.TitleBarShowPath, sizeof(DWORD));
                SetValueW(actKey, CONFIG_TITLEBARMODE_REG_W, REG_DWORD,
                         &Configuration.TitleBarMode, sizeof(DWORD));
                SetValueW(actKey, CONFIG_THEME_MODE_REG_W, REG_DWORD,
                         &Configuration.ThemeMode, sizeof(DWORD));
                SetValueW(actKey, CONFIG_TITLEBARPREFIX_REG_W, REG_DWORD,
                         &Configuration.UseTitleBarPrefix, sizeof(DWORD));
                SetValueW(actKey, CONFIG_TITLEBARPREFIXTEXT_REG, REG_SZ,
                          Configuration.TitleBarPrefix.c_str(), -1);
                SetValueW(actKey, CONFIG_MAINWINDOWICONINDEX_REG_W, REG_DWORD,
                         &Configuration.MainWindowIconIndex, sizeof(DWORD));
                SetValueW(actKey, CONFIG_CLICKQUICKRENAME_REG_W, REG_DWORD,
                         &Configuration.ClickQuickRename, sizeof(DWORD));
                SetValueW(actKey, CONFIG_VISIBLEDRIVES_REG_W, REG_DWORD,
                         &Configuration.VisibleDrives, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SEPARATEDDRIVES_REG_W, REG_DWORD,
                         &Configuration.SeparatedDrives, sizeof(DWORD));
                SetValueW(actKey, CONFIG_COMPAREBYTIME_REG_W, REG_DWORD,
                         &Configuration.CompareByTime, sizeof(DWORD));
                SetValueW(actKey, CONFIG_COMPAREBYSIZE_REG_W, REG_DWORD,
                         &Configuration.CompareBySize, sizeof(DWORD));
                SetValueW(actKey, CONFIG_COMPAREBYCONTENT_REG_W, REG_DWORD,
                         &Configuration.CompareByContent, sizeof(DWORD));
                SetValueW(actKey, CONFIG_COMPAREBYATTR_REG_W, REG_DWORD,
                         &Configuration.CompareByAttr, sizeof(DWORD));
                SetValueW(actKey, CONFIG_COMPAREBYSUBDIRS_REG_W, REG_DWORD,
                         &Configuration.CompareSubdirs, sizeof(DWORD));
                SetValueW(actKey, CONFIG_COMPAREBYSUBDIRSATTR_REG_W, REG_DWORD,
                         &Configuration.CompareSubdirsAttr, sizeof(DWORD));
                SetValueW(actKey, CONFIG_COMPAREONEPANELDIRS_REG_W, REG_DWORD,
                         &Configuration.CompareOnePanelDirs, sizeof(DWORD));
                SetValueW(actKey, CONFIG_COMPAREMOREOPTIONS_REG_W, REG_DWORD,
                         &Configuration.CompareMoreOptions, sizeof(DWORD));
                SetValueW(actKey, CONFIG_COMPAREIGNOREFILES_REG_W, REG_DWORD,
                         &Configuration.CompareIgnoreFiles, sizeof(DWORD));
                SetValueW(actKey, CONFIG_COMPAREIGNOREDIRS_REG_W, REG_DWORD,
                         &Configuration.CompareIgnoreDirs, sizeof(DWORD));
                SetValue(actKey, CONFIG_CONFIGTIGNOREFILESMASKS_REG, REG_SZ,
                         Configuration.CompareIgnoreFilesMasks.GetMasksString(), -1);
                SetValue(actKey, CONFIG_CONFIGTIGNOREDIRSMASKS_REG, REG_SZ,
                         Configuration.CompareIgnoreDirsMasks.GetMasksString(), -1);

                SetValueW(actKey, CONFIG_THUMBNAILSIZE_REG_W, REG_DWORD,
                         &Configuration.ThumbnailSize, sizeof(DWORD));
                SetValueW(actKey, CONFIG_KEEPPLUGINSSORTED_REG_W, REG_DWORD,
                         &Configuration.KeepPluginsSorted, sizeof(DWORD));
                SetValueW(actKey, CONFIG_SHOWSLGINCOMPLETE_REG_W, REG_DWORD,
                         &Configuration.ShowSLGIncomplete, sizeof(DWORD));

                // WARNING: when an icon overlay handler crashes, these values are written directly into the registry
                //         (prevents Salamander from becoming "unstartable"), see InformAboutIconOvrlsHanCrash()
                SetValueW(actKey, CONFIG_ENABLECUSTICOVRLS_REG_W, REG_DWORD,
                         &Configuration.EnableCustomIconOverlays, sizeof(DWORD));
                SetValueW(actKey, CONFIG_DISABLEDCUSTICOVRLS_REG, REG_SZ,
                          Configuration.DisabledCustomIconOverlays != NULL ? Configuration.DisabledCustomIconOverlays : L"", -1);

                SetValueW(actKey, CONFIG_EDITNEWFILE_USEDEFAULT_REG_W, REG_DWORD,
                         &Configuration.UseEditNewFileDefault, sizeof(DWORD));
                SetValue(actKey, CONFIG_EDITNEWFILE_DEFAULT_REG, REG_SZ,
                         Configuration.EditNewFileDefault.c_str(), -1);

#ifndef _WIN64 // FIXME_X64_WINSCP
                SetValue(actKey, L"Add x86-Only Plugins", REG_DWORD,
                         &Configuration.AddX86OnlyPlugins, sizeof(DWORD));
#endif // _WIN64

                HKEY actSubKey;
                if (CreateKey(actKey, SALAMANDER_CONFIRMATION_REG, actSubKey))
                {
                    SetValue(actSubKey, CONFIG_CNFRM_FILEDIRDEL, REG_DWORD,
                             &Configuration.CnfrmFileDirDel, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_NEDIRDEL, REG_DWORD,
                             &Configuration.CnfrmNEDirDel, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_FILEOVER, REG_DWORD,
                             &Configuration.CnfrmFileOver, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_DIROVER, REG_DWORD,
                             &Configuration.CnfrmDirOver, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_SHFILEDEL, REG_DWORD,
                             &Configuration.CnfrmSHFileDel, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_SHDIRDEL, REG_DWORD,
                             &Configuration.CnfrmSHDirDel, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_SHFILEOVER, REG_DWORD,
                             &Configuration.CnfrmSHFileOver, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_NTFSPRESS, REG_DWORD,
                             &Configuration.CnfrmNTFSPress, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_NTFSCRYPT, REG_DWORD,
                             &Configuration.CnfrmNTFSCrypt, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_DAD, REG_DWORD,
                             &Configuration.CnfrmDragDrop, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_CLOSEARCHIVE, REG_DWORD,
                             &Configuration.CnfrmCloseArchive, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_CLOSEFIND, REG_DWORD,
                             &Configuration.CnfrmCloseFind, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_STOPFIND, REG_DWORD,
                             &Configuration.CnfrmStopFind, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_CREATETARGETPATH, REG_DWORD,
                             &Configuration.CnfrmCreatePath, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_ALWAYSONTOP, REG_DWORD,
                             &Configuration.CnfrmAlwaysOnTop, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_ONSALCLOSE, REG_DWORD,
                             &Configuration.CnfrmOnSalClose, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_SENDEMAIL, REG_DWORD,
                             &Configuration.CnfrmSendEmail, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_ADDTOARCHIVE, REG_DWORD,
                             &Configuration.CnfrmAddToArchive, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_CREATEDIR, REG_DWORD,
                             &Configuration.CnfrmCreateDir, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_CHANGEDIRTC, REG_DWORD,
                             &Configuration.CnfrmChangeDirTC, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_SHOWNAMETOCOMP, REG_DWORD,
                             &Configuration.CnfrmShowNamesToCompare, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_DSTSHIFTSIGNORED, REG_DWORD,
                             &Configuration.CnfrmDSTShiftsIgnored, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_DSTSHIFTSOCCURED, REG_DWORD,
                             &Configuration.CnfrmDSTShiftsOccured, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_CNFRM_COPYMOVEOPTIONSNS, REG_DWORD,
                             &Configuration.CnfrmCopyMoveOptionsNS, sizeof(DWORD));

                    CloseKey(actSubKey);
                }

                if (CreateKey(actKey, SALAMANDER_DRVSPEC_REG, actSubKey))
                {
                    SetValue(actSubKey, CONFIG_DRVSPEC_FLOPPY_MON, REG_DWORD,
                             &Configuration.DrvSpecFloppyMon, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_DRVSPEC_FLOPPY_SIMPLE, REG_DWORD,
                             &Configuration.DrvSpecFloppySimple, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_DRVSPEC_REMOVABLE_MON, REG_DWORD,
                             &Configuration.DrvSpecRemovableMon, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_DRVSPEC_REMOVABLE_SIMPLE, REG_DWORD,
                             &Configuration.DrvSpecRemovableSimple, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_DRVSPEC_FIXED_MON, REG_DWORD,
                             &Configuration.DrvSpecFixedMon, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_DRVSPEC_FIXED_SIMPLE, REG_DWORD,
                             &Configuration.DrvSpecFixedSimple, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_DRVSPEC_REMOTE_MON, REG_DWORD,
                             &Configuration.DrvSpecRemoteMon, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_DRVSPEC_REMOTE_SIMPLE, REG_DWORD,
                             &Configuration.DrvSpecRemoteSimple, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_DRVSPEC_REMOTE_ACT, REG_DWORD,
                             &Configuration.DrvSpecRemoteDoNotRefreshOnAct, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_DRVSPEC_CDROM_MON, REG_DWORD,
                             &Configuration.DrvSpecCDROMMon, sizeof(DWORD));
                    SetValue(actSubKey, CONFIG_DRVSPEC_CDROM_SIMPLE, REG_DWORD,
                             &Configuration.DrvSpecCDROMSimple, sizeof(DWORD));
                    CloseKey(actSubKey);
                }

                SetValue(actKey, CONFIG_TOPTOOLBAR_REG, REG_SZ, Configuration.TopToolBar.c_str(), -1);
                SetValue(actKey, CONFIG_MIDDLETOOLBAR_REG, REG_SZ, Configuration.MiddleToolBar.c_str(), -1);

                SetValue(actKey, CONFIG_LEFTTOOLBAR_REG, REG_SZ, Configuration.LeftToolBar.c_str(), -1);
                SetValue(actKey, CONFIG_RIGHTTOOLBAR_REG, REG_SZ, Configuration.RightToolBar.c_str(), -1);

                SetValueW(actKey, CONFIG_TOPTOOLBARVISIBLE_REG_W, REG_DWORD,
                         &Configuration.TopToolBarVisible, sizeof(DWORD));
                SetValueW(actKey, CONFIG_PLGTOOLBARVISIBLE_REG_W, REG_DWORD,
                         &Configuration.PluginsBarVisible, sizeof(DWORD));
                SetValueW(actKey, CONFIG_MIDDLETOOLBARVISIBLE_REG_W, REG_DWORD,
                         &Configuration.MiddleToolBarVisible, sizeof(DWORD));

                SetValueW(actKey, CONFIG_USERMENUTOOLBARVISIBLE_REG_W, REG_DWORD,
                         &Configuration.UserMenuToolBarVisible, sizeof(DWORD));
                SetValueW(actKey, CONFIG_HOTPATHSBARVISIBLE_REG_W, REG_DWORD,
                         &Configuration.HotPathsBarVisible, sizeof(DWORD));

                SetValueW(actKey, CONFIG_DRIVEBARVISIBLE_REG_W, REG_DWORD,
                         &Configuration.DriveBarVisible, sizeof(DWORD));
                SetValueW(actKey, CONFIG_DRIVEBAR2VISIBLE_REG_W, REG_DWORD,
                         &Configuration.DriveBar2Visible, sizeof(DWORD));

                SetValueW(actKey, CONFIG_BOTTOMTOOLBARVISIBLE_REG_W, REG_DWORD,
                         &Configuration.BottomToolBarVisible, sizeof(DWORD));

                //      SetValue(actKey, CONFIG_SPACESELCALCSPACE, REG_DWORD,
                //               &Configuration.SpaceSelCalcSpace, sizeof(DWORD));
                SetValueW(actKey, CONFIG_USETIMERESOLUTION_W, REG_DWORD,
                         &Configuration.UseTimeResolution, sizeof(DWORD));
                SetValueW(actKey, CONFIG_TIMERESOLUTION_W, REG_DWORD,
                         &Configuration.TimeResolution, sizeof(DWORD));
                SetValueW(actKey, CONFIG_IGNOREDSTSHIFTS_W, REG_DWORD,
                         &Configuration.IgnoreDSTShifts, sizeof(DWORD));
                SetValueW(actKey, CONFIG_USEDRAGDROPMINTIME_W, REG_DWORD,
                         &Configuration.UseDragDropMinTime, sizeof(DWORD));
                SetValueW(actKey, CONFIG_DRAGDROPMINTIME_W, REG_DWORD,
                         &Configuration.DragDropMinTime, sizeof(DWORD));

                SetValueW(actKey, CONFIG_LASTFOCUSEDPAGE_W, REG_DWORD,
                         &Configuration.LastFocusedPage, sizeof(DWORD));
                SetValueW(actKey, CONFIG_CONFIGURATION_HEIGHT_W, REG_DWORD,
                         &Configuration.ConfigurationHeight, sizeof(DWORD));
                SetValueW(actKey, CONFIG_VIEWANDEDITEXPAND_W, REG_DWORD,
                         &Configuration.ViewersAndEditorsExpanded, sizeof(DWORD));
                SetValueW(actKey, CONFIG_PACKEPAND_W, REG_DWORD,
                         &Configuration.PackersAndUnpackersExpanded, sizeof(DWORD));

                SetValueW(actKey, CONFIG_CMDLINE_REG_W, REG_DWORD, &EditPermanentVisible, sizeof(DWORD));
                SetValueW(actKey, CONFIG_CMDLFOCUS_REG_W, REG_DWORD, &EditMode, sizeof(DWORD));

                SetValueW(actKey, CONFIG_USECUSTOMPANELFONT_REG_W, REG_DWORD, &UseCustomPanelFont, sizeof(DWORD));
                SaveLogFont(actKey, CONFIG_PANELFONT_REG, &LogFont);

                if (GlobalSaveWaitWindow == NULL)
                    analysing.SetProgressPos(++savingProgress); // 4
                else
                    GlobalSaveWaitWindow->SetProgressPos(++GlobalSaveWaitWindowProgress); // 4
                //TRACE_I("analysing.SetProgressPos() savingProgress="<<savingProgress);

                SaveHistory(actKey, CONFIG_NAMEDHISTORYW_REG, FindNamedHistory,
                             FIND_NAMED_HISTORY_SIZE, !Configuration.SaveHistory);
                SaveHistory(actKey, CONFIG_LOOKINHISTORYW_REG, FindLookInHistory,
                             FIND_LOOKIN_HISTORY_SIZE, !Configuration.SaveHistory);
                SaveHistory(actKey, CONFIG_GREPHISTORYW_REG, FindGrepHistory,
                             FIND_GREP_HISTORY_SIZE, !Configuration.SaveHistory);
                SaveHistory(actKey, CONFIG_SELECTHISTORYW_REG, Configuration.SelectHistory,
                             SELECT_HISTORY_SIZE, !Configuration.SaveHistory);
                // Standard histories have one UTF-16 owner. Keep writing the wide keys;
                // the old narrow keys below are read only when importing old settings.
                SaveHistory(actKey, CONFIG_COPYHISTORYW_REG, Configuration.CopyHistory,
                             COPY_HISTORY_SIZE, !Configuration.SaveHistory);
                SaveHistory(actKey, CONFIG_CHANGEDIRHISTORYW_REG, Configuration.ChangeDirHistory,
                             CHANGEDIR_HISTORY_SIZE, !Configuration.SaveHistory);

                if (GlobalSaveWaitWindow == NULL)
                    analysing.SetProgressPos(++savingProgress); // 5
                else
                    GlobalSaveWaitWindow->SetProgressPos(++GlobalSaveWaitWindowProgress); // 5
                //TRACE_I("analysing.SetProgressPos() savingProgress="<<savingProgress);

                SaveHistory(actKey, CONFIG_VIEWERHISTORYW_REG, ViewerHistory,
                             VIEWER_HISTORY_SIZE, !Configuration.SaveHistory);
                SaveHistory(actKey, CONFIG_COMMANDHISTORYW_REG, Configuration.EditHistory,
                             EDIT_HISTORY_SIZE, !(Configuration.SaveHistory && Configuration.EnableCmdLineHistory && Configuration.SaveCmdLineHistory));
                // FileListHistory: same shape, sole consumer is
                // ExpandMakeFileListW's wide param (files_window_copy_move.cpp).
                SaveHistory(actKey, CONFIG_FILELISTHISTORYW_REG, Configuration.FileListHistory,
                             FILELIST_HISTORY_SIZE, !Configuration.SaveHistory);
                SaveHistory(actKey, CONFIG_CREATEDIRHISTORYW_REG, Configuration.CreateDirHistory,
                             CREATEDIR_HISTORY_SIZE, !Configuration.SaveHistory);
                SaveHistory(actKey, CONFIG_QUICKRENAMEHISTORYW_REG, Configuration.QuickRenameHistory,
                             QUICKRENAME_HISTORY_SIZE, !Configuration.SaveHistory);
                SaveHistory(actKey, CONFIG_EDITNEWHISTORYW_REG, Configuration.EditNewHistory,
                             EDITNEW_HISTORY_SIZE, !Configuration.SaveHistory);
                SaveHistory(actKey, CONFIG_CONVERTHISTORYW_REG, Configuration.ConvertHistory,
                             CONVERT_HISTORY_SIZE, !Configuration.SaveHistory);
                SaveHistory(actKey, CONFIG_FILTERHISTORYW_REG, Configuration.FilterHistory,
                             FILTER_HISTORY_SIZE, !Configuration.SaveHistory);

                // Retire the unsuffixed ACP keys now that the wide ones have been written.
                //
                // They are not merely stale. The loader falls back to them when the WIDE
                // history reads back EMPTY (IsWideHistoryEmpty), not when the wide key is
                // absent - so leaving them in place meant "Clear history", and switching
                // "Save history" off, cleared the wide key and then had the old entries
                // RESURRECTED from the narrow key on the next launch. Pre-unicode had only
                // the one key per history and cleared it, so this is a privacy promise the
                // widening quietly broke. Clearing here also completes the ACP->UTF-16
                // migration: after one save the import path has nothing left to import.
                RetireLegacyHistoryKey(actKey, CONFIG_NAMEDHISTORY_REG);
                RetireLegacyHistoryKey(actKey, CONFIG_LOOKINHISTORY_REG);
                RetireLegacyHistoryKey(actKey, CONFIG_GREPHISTORY_REG);
                RetireLegacyHistoryKey(actKey, CONFIG_SELECTHISTORY_REG);
                RetireLegacyHistoryKey(actKey, CONFIG_COPYHISTORY_REG);
                RetireLegacyHistoryKey(actKey, CONFIG_CHANGEDIRHISTORY_REG);
                RetireLegacyHistoryKey(actKey, CONFIG_VIEWERHISTORY_REG);
                RetireLegacyHistoryKey(actKey, CONFIG_COMMANDHISTORY_REG);
                RetireLegacyHistoryKey(actKey, CONFIG_FILELISTHISTORY_REG);
                RetireLegacyHistoryKey(actKey, CONFIG_CREATEDIRHISTORY_REG);
                RetireLegacyHistoryKey(actKey, CONFIG_QUICKRENAMEHISTORY_REG);
                RetireLegacyHistoryKey(actKey, CONFIG_EDITNEWHISTORY_REG);
                RetireLegacyHistoryKey(actKey, CONFIG_CONVERTHISTORY_REG);
                RetireLegacyHistoryKey(actKey, CONFIG_FILTERHISTORY_REG);

                if (DirHistory != NULL)
                    DirHistory->SaveToRegistry(actKey, CONFIG_WORKDIRSHISTORY_REG, !Configuration.SaveWorkDirs);

                if (GlobalSaveWaitWindow == NULL)
                    analysing.SetProgressPos(++savingProgress); // 6
                else
                    GlobalSaveWaitWindow->SetProgressPos(++GlobalSaveWaitWindowProgress); // 6
                //TRACE_I("analysing.SetProgressPos() savingProgress="<<savingProgress);

                if (CreateKey(actKey, CONFIG_COPYMOVEOPTIONS_REG, actSubKey))
                {
                    CopyMoveOptions.Save(actSubKey);
                    CloseKey(actSubKey);
                }

                if (CreateKey(actKey, CONFIG_FINDOPTIONS_REG, actSubKey))
                {
                    FindOptions.Save(actSubKey);
                    CloseKey(actSubKey);
                }

                if (CreateKey(actKey, CONFIG_FINDIGNORE_REG, actSubKey))
                {
                    FindIgnore.Save(actSubKey);
                    CloseKey(actSubKey);
                }

                SetValue(actKey, CONFIG_FILELISTNAME_REG, REG_SZ, Configuration.FileListName.c_str(), -1);
                SetValueW(actKey, CONFIG_FILELISTAPPEND_REG_W, REG_DWORD, &Configuration.FileListAppend, sizeof(DWORD));
                SetValueW(actKey, CONFIG_FILELISTDESTINATION_REG_W, REG_DWORD, &Configuration.FileListDestination, sizeof(DWORD));

                CloseKey(actKey);
            }

            //---  viewer

            if (CreateKey(salamander, SALAMANDER_VIEWER_REG, actKey))
            {
                SetValueW(actKey, VIEWER_FINDFORWARD_REG_W, REG_DWORD,
                         &GlobalFindDialog.Forward, sizeof(DWORD));
                SetValueW(actKey, VIEWER_FINDWHOLEWORDS_REG_W, REG_DWORD,
                         &GlobalFindDialog.WholeWords, sizeof(DWORD));
                SetValueW(actKey, VIEWER_FINDCASESENSITIVE_REG_W, REG_DWORD,
                         &GlobalFindDialog.CaseSensitive, sizeof(DWORD));
                SetValueW(actKey, VIEWER_FINDREGEXP_REG_W, REG_DWORD,
                         &GlobalFindDialog.Regular, sizeof(DWORD));
                SetValue(actKey, VIEWER_FINDTEXT_REG, REG_SZ, GlobalFindDialog.Text.c_str(), -1);
                SetValueW(actKey, VIEWER_FINDHEXMODE_REG_W, REG_DWORD,
                         &GlobalFindDialog.HexMode, sizeof(DWORD));

                SetValueW(actKey, VIEWER_CONFIGCRLF_REG_W, REG_DWORD,
                         &Configuration.EOL_CRLF, sizeof(DWORD));
                SetValueW(actKey, VIEWER_CONFIGCR_REG_W, REG_DWORD,
                         &Configuration.EOL_CR, sizeof(DWORD));
                SetValueW(actKey, VIEWER_CONFIGLF_REG_W, REG_DWORD,
                         &Configuration.EOL_LF, sizeof(DWORD));
                SetValueW(actKey, VIEWER_CONFIGNULL_REG_W, REG_DWORD,
                         &Configuration.EOL_NULL, sizeof(DWORD));
                SetValueW(actKey, VIEWER_CONFIGTABSIZE_REG_W, REG_DWORD,
                         &Configuration.TabSize, sizeof(DWORD));
                SetValueW(actKey, VIEWER_CONFIGDEFMODE_REG_W, REG_DWORD,
                         &Configuration.DefViewMode, sizeof(DWORD));
                SetValue(actKey, VIEWER_CONFIGTEXTMASK_REG, REG_SZ,
                         Configuration.TextModeMasks.GetMasksString(), -1);
                SetValue(actKey, VIEWER_CONFIGHEXMASK_REG, REG_SZ,
                         Configuration.HexModeMasks.GetMasksString(), -1);
                SetValueW(actKey, VIEWER_CONFIGUSECUSTOMFONT_REG_W, REG_DWORD,
                         &UseCustomViewerFont, sizeof(DWORD));
                SaveLogFont(actKey, VIEWER_CONFIGFONT_REG, &ViewerLogFont);
                SetValueW(actKey, VIEWER_WRAPTEXT_REG_W, REG_DWORD,
                         &Configuration.WrapText, sizeof(DWORD));
                SetValueW(actKey, VIEWER_CPAUTOSELECT_REG_W, REG_DWORD,
                         &Configuration.CodePageAutoSelect, sizeof(DWORD));
                SetValue(actKey, VIEWER_DEFAULTCONVERT_REG, REG_SZ, Configuration.DefaultConvert.c_str(), -1);
                SetValueW(actKey, VIEWER_AUTOCOPYSELECTION_REG_W, REG_DWORD,
                         &Configuration.AutoCopySelection, sizeof(DWORD));
                SetValueW(actKey, VIEWER_GOTOOFFSETISHEX_REG_W, REG_DWORD,
                         &Configuration.GoToOffsetIsHex, sizeof(DWORD));

                SetValueW(actKey, VIEWER_CONFIGSAVEWINPOS_REG_W, REG_DWORD,
                         &Configuration.SavePosition, sizeof(DWORD));
                if (Configuration.WindowPlacement.length != 0)
                {
                    SetValueW(actKey, VIEWER_CONFIGWNDLEFT_REG_W, REG_DWORD,
                             &Configuration.WindowPlacement.rcNormalPosition.left, sizeof(DWORD));
                    SetValueW(actKey, VIEWER_CONFIGWNDRIGHT_REG_W, REG_DWORD,
                             &Configuration.WindowPlacement.rcNormalPosition.right, sizeof(DWORD));
                    SetValueW(actKey, VIEWER_CONFIGWNDTOP_REG_W, REG_DWORD,
                             &Configuration.WindowPlacement.rcNormalPosition.top, sizeof(DWORD));
                    SetValueW(actKey, VIEWER_CONFIGWNDBOTTOM_REG_W, REG_DWORD,
                             &Configuration.WindowPlacement.rcNormalPosition.bottom, sizeof(DWORD));
                    SetValueW(actKey, VIEWER_CONFIGWNDSHOW_REG_W, REG_DWORD,
                             &Configuration.WindowPlacement.showCmd, sizeof(DWORD));
                }

                CloseKey(actKey);
            }

            //---  user menu

            if (CreateKey(salamander, SALAMANDER_USERMENU_REG, actKey))
            {
                ClearKey(actKey);

                HKEY subKey;
                wchar_t buf[30];
                int i;
                for (i = 0; i < UserMenuItems->Count; i++)
                {
                    _itow_s(i + 1, buf, _countof(buf), 10);
                    if (CreateKey(actKey, buf, subKey))
                    {
                        SetValueW(subKey, USERMENU_ITEMNAME_REG, REG_SZ, UserMenuItems->At(i)->ItemName.c_str(), -1);
                        SetValueW(subKey, USERMENU_COMMAND_REG, REG_SZ, UserMenuItems->At(i)->UMCommand.c_str(), -1);
                        SetValueW(subKey, USERMENU_ARGUMENTS_REG, REG_SZ, UserMenuItems->At(i)->Arguments.c_str(), -1);
                        SetValueW(subKey, USERMENU_INITDIR_REG, REG_SZ, UserMenuItems->At(i)->InitDir.c_str(), -1);
                        SetValue(subKey, USERMENU_SHELL_REG, REG_DWORD,
                                 &UserMenuItems->At(i)->ThroughShell, sizeof(DWORD));
                        SetValue(subKey, USERMENU_CLOSE_REG, REG_DWORD,
                                 &UserMenuItems->At(i)->CloseShell, sizeof(DWORD));
                        SetValue(subKey, USERMENU_USEWINDOW_REG, REG_DWORD,
                                 &UserMenuItems->At(i)->UseWindow, sizeof(DWORD));

                        SetValueW(subKey, USERMENU_ICON_REG, REG_SZ, UserMenuItems->At(i)->Icon.c_str(), -1);
                        SetValue(subKey, USERMENU_TYPE_REG, REG_DWORD,
                                 &UserMenuItems->At(i)->Type, sizeof(DWORD));
                        SetValue(subKey, USERMENU_SHOWINTOOLBAR_REG, REG_DWORD,
                                 &UserMenuItems->At(i)->ShowInToolbar, sizeof(DWORD));

                        CloseKey(subKey);
                    }
                    else
                        break;
                }
                CloseKey(actKey);
            }

            //---  internal ZIP packer

            if (Configuration.ConfigVersion < 6 && // only for old configurations, otherwise we neither create nor clear the key
                CreateKey(salamander, SALAMANDER_IZIP_REG, actKey))
            {
                ClearKey(actKey);

                CloseKey(actKey);

                DeleteKey(salamander, SALAMANDER_IZIP_REG);
            }

            //---  viewers

            SaveViewers(salamander, SALAMANDER_VIEWERS_REG, ViewerMasks);
            SaveViewers(salamander, SALAMANDER_ALTVIEWERS_REG, AltViewerMasks);

            //---  editors

            SaveEditors(salamander, SALAMANDER_EDITORS_REG, EditorMasks);

            if (GlobalSaveWaitWindow == NULL)
                analysing.SetProgressPos(++savingProgress); // 7
            else
                GlobalSaveWaitWindow->SetProgressPos(++GlobalSaveWaitWindowProgress); // 7
            //TRACE_I("analysing.SetProgressPos() savingProgress="<<savingProgress);

            //---  colors
            if (CreateKey(salamander, SALAMANDER_CUSTOMCOLORS_REG, actKey))
            {
                wchar_t buff[10];
                int i;
                for (i = 0; i < NUMBER_OF_CUSTOMCOLORS; i++)
                {
                    _itow_s(i + 1, buff, _countof(buff), 10);
                    SaveRGB(actKey, buff, CustomColors[i]);
                }

                CloseKey(actKey);
            }

            if (CreateKey(salamander, SALAMANDER_COLORS_REG, actKey))
            {
                DWORD scheme = 4; // custom
                if (CurrentColors == SalamanderColors)
                    scheme = 0;
                else if (CurrentColors == ExplorerColors)
                    scheme = 1;
                else if (CurrentColors == NortonColors)
                    scheme = 2;
                else if (CurrentColors == NavigatorColors)
                    scheme = 3;
                SetValueW(actKey, SALAMANDER_CLRSCHEME_REG_W, REG_DWORD, &scheme, sizeof(DWORD));

                SaveRGBF(actKey, SALAMANDER_CLR_FOCUS_ACTIVE_NORMAL_REG, UserColors[FOCUS_ACTIVE_NORMAL]);
                SaveRGBF(actKey, SALAMANDER_CLR_FOCUS_ACTIVE_SELECTED_REG, UserColors[FOCUS_ACTIVE_SELECTED]);
                SaveRGBF(actKey, SALAMANDER_CLR_FOCUS_INACTIVE_NORMAL_REG, UserColors[FOCUS_FG_INACTIVE_NORMAL]);
                SaveRGBF(actKey, SALAMANDER_CLR_FOCUS_INACTIVE_SELECTED_REG, UserColors[FOCUS_FG_INACTIVE_SELECTED]);
                SaveRGBF(actKey, SALAMANDER_CLR_FOCUS_BK_INACTIVE_NORMAL_REG, UserColors[FOCUS_BK_INACTIVE_NORMAL]);
                SaveRGBF(actKey, SALAMANDER_CLR_FOCUS_BK_INACTIVE_SELECTED_REG, UserColors[FOCUS_BK_INACTIVE_SELECTED]);

                SaveRGBF(actKey, SALAMANDER_CLR_ITEM_FG_NORMAL_REG, UserColors[ITEM_FG_NORMAL]);
                SaveRGBF(actKey, SALAMANDER_CLR_ITEM_FG_SELECTED_REG, UserColors[ITEM_FG_SELECTED]);
                SaveRGBF(actKey, SALAMANDER_CLR_ITEM_FG_FOCUSED_REG, UserColors[ITEM_FG_FOCUSED]);
                SaveRGBF(actKey, SALAMANDER_CLR_ITEM_FG_FOCSEL_REG, UserColors[ITEM_FG_FOCSEL]);
                SaveRGBF(actKey, SALAMANDER_CLR_ITEM_FG_HIGHLIGHT_REG, UserColors[ITEM_FG_HIGHLIGHT]);

                SaveRGBF(actKey, SALAMANDER_CLR_ITEM_BK_NORMAL_REG, UserColors[ITEM_BK_NORMAL]);
                SaveRGBF(actKey, SALAMANDER_CLR_ITEM_BK_SELECTED_REG, UserColors[ITEM_BK_SELECTED]);
                SaveRGBF(actKey, SALAMANDER_CLR_ITEM_BK_FOCUSED_REG, UserColors[ITEM_BK_FOCUSED]);
                SaveRGBF(actKey, SALAMANDER_CLR_ITEM_BK_FOCSEL_REG, UserColors[ITEM_BK_FOCSEL]);
                SaveRGBF(actKey, SALAMANDER_CLR_ITEM_BK_HIGHLIGHT_REG, UserColors[ITEM_BK_HIGHLIGHT]);

                SaveRGBF(actKey, SALAMANDER_CLR_ICON_BLEND_SELECTED_REG, UserColors[ICON_BLEND_SELECTED]);
                SaveRGBF(actKey, SALAMANDER_CLR_ICON_BLEND_FOCUSED_REG, UserColors[ICON_BLEND_FOCUSED]);
                SaveRGBF(actKey, SALAMANDER_CLR_ICON_BLEND_FOCSEL_REG, UserColors[ICON_BLEND_FOCSEL]);

                SaveRGBF(actKey, SALAMANDER_CLR_PROGRESS_FG_NORMAL_REG, UserColors[PROGRESS_FG_NORMAL]);
                SaveRGBF(actKey, SALAMANDER_CLR_PROGRESS_FG_SELECTED_REG, UserColors[PROGRESS_FG_SELECTED]);
                SaveRGBF(actKey, SALAMANDER_CLR_PROGRESS_BK_NORMAL_REG, UserColors[PROGRESS_BK_NORMAL]);
                SaveRGBF(actKey, SALAMANDER_CLR_PROGRESS_BK_SELECTED_REG, UserColors[PROGRESS_BK_SELECTED]);

                SaveRGBF(actKey, SALAMANDER_CLR_HOT_PANEL_REG, UserColors[HOT_PANEL]);
                SaveRGBF(actKey, SALAMANDER_CLR_HOT_ACTIVE_REG, UserColors[HOT_ACTIVE]);
                SaveRGBF(actKey, SALAMANDER_CLR_HOT_INACTIVE_REG, UserColors[HOT_INACTIVE]);

                SaveRGBF(actKey, SALAMANDER_CLR_ACTIVE_CAPTION_FG_REG, UserColors[ACTIVE_CAPTION_FG]);
                SaveRGBF(actKey, SALAMANDER_CLR_ACTIVE_CAPTION_BK_REG, UserColors[ACTIVE_CAPTION_BK]);
                SaveRGBF(actKey, SALAMANDER_CLR_INACTIVE_CAPTION_FG_REG, UserColors[INACTIVE_CAPTION_FG]);
                SaveRGBF(actKey, SALAMANDER_CLR_INACTIVE_CAPTION_BK_REG, UserColors[INACTIVE_CAPTION_BK]);

                SaveRGBF(actKey, SALAMANDER_CLR_THUMBNAIL_FRAME_NORMAL_REG, UserColors[THUMBNAIL_FRAME_NORMAL]);
                SaveRGBF(actKey, SALAMANDER_CLR_THUMBNAIL_FRAME_SELECTED_REG, UserColors[THUMBNAIL_FRAME_SELECTED]);
                SaveRGBF(actKey, SALAMANDER_CLR_THUMBNAIL_FRAME_FOCUSED_REG, UserColors[THUMBNAIL_FRAME_FOCUSED]);
                SaveRGBF(actKey, SALAMANDER_CLR_THUMBNAIL_FRAME_FOCSEL_REG, UserColors[THUMBNAIL_FRAME_FOCSEL]);

                SaveRGBF(actKey, SALAMANDER_CLR_VIEWER_FG_NORMAL_REG, ViewerColors[VIEWER_FG_NORMAL]);
                SaveRGBF(actKey, SALAMANDER_CLR_VIEWER_BK_NORMAL_REG, ViewerColors[VIEWER_BK_NORMAL]);
                SaveRGBF(actKey, SALAMANDER_CLR_VIEWER_FG_SELECTED_REG, ViewerColors[VIEWER_FG_SELECTED]);
                SaveRGBF(actKey, SALAMANDER_CLR_VIEWER_BK_SELECTED_REG, ViewerColors[VIEWER_BK_SELECTED]);

                // save colors for file highlighting
                HKEY hHltKey;
                if (CreateKey(actKey, SALAMANDER_HLT, hHltKey))
                {
                    ClearKey(hHltKey);
                    HKEY hSubKey;
                    wchar_t buf[30];
                    int i;
                    for (i = 0; i < HighlightMasks->Count; i++)
                    {
                        _itow_s(i + 1, buf, _countof(buf), 10);
                        if (CreateKey(hHltKey, buf, hSubKey))
                        {
                            CHighlightMasksItem* item = HighlightMasks->At(i);
                            SetValue(hSubKey, SALAMANDER_HLT_ITEM_MASKS, REG_SZ, item->Masks->GetMasksString(), -1);
                            SetValue(hSubKey, SALAMANDER_HLT_ITEM_ATTR, REG_DWORD, &item->Attr, sizeof(DWORD));
                            SetValue(hSubKey, SALAMANDER_HLT_ITEM_VALIDATTR, REG_DWORD, &item->ValidAttr, sizeof(DWORD));

                            SaveRGBF(hSubKey, SALAMANDER_HLT_ITEM_FG_NORMAL_REG, item->NormalFg);
                            SaveRGBF(hSubKey, SALAMANDER_HLT_ITEM_FG_SELECTED_REG, item->SelectedFg);
                            SaveRGBF(hSubKey, SALAMANDER_HLT_ITEM_FG_FOCUSED_REG, item->FocusedFg);
                            SaveRGBF(hSubKey, SALAMANDER_HLT_ITEM_FG_FOCSEL_REG, item->FocSelFg);
                            SaveRGBF(hSubKey, SALAMANDER_HLT_ITEM_FG_HIGHLIGHT_REG, item->HighlightFg);

                            SaveRGBF(hSubKey, SALAMANDER_HLT_ITEM_BK_NORMAL_REG, item->NormalBk);
                            SaveRGBF(hSubKey, SALAMANDER_HLT_ITEM_BK_SELECTED_REG, item->SelectedBk);
                            SaveRGBF(hSubKey, SALAMANDER_HLT_ITEM_BK_FOCUSED_REG, item->FocusedBk);
                            SaveRGBF(hSubKey, SALAMANDER_HLT_ITEM_BK_FOCSEL_REG, item->FocSelBk);
                            SaveRGBF(hSubKey, SALAMANDER_HLT_ITEM_BK_HIGHLIGHT_REG, item->HighlightBk);
                            CloseKey(hSubKey);
                        }
                    }
                    CloseKey(hHltKey);
                }
                CloseKey(actKey);
            }

            if (GlobalSaveWaitWindow == NULL)
                analysing.SetProgressPos(++savingProgress); // 8
            else
                GlobalSaveWaitWindow->SetProgressPos(++GlobalSaveWaitWindowProgress); // 8
            //TRACE_I("analysing.SetProgressPos() savingProgress="<<savingProgress);

            if (deleteSALAMANDER_SAVE_IN_PROGRESS)
            {
                DeleteValue(salamander, SALAMANDER_SAVE_IN_PROGRESS);
                IsSetSALAMANDER_SAVE_IN_PROGRESS = FALSE;
            }
        }
        CloseKey(salamander);
    }

    LoadSaveToRegistryMutex.Leave();

    if (GlobalSaveWaitWindow == NULL)
    {
        EnableWindow(parent, TRUE);
        PluginMsgBoxParent = oldPluginMsgBoxParent;
        DestroyWindow(analysing.HWindow);
        SetCursor(hOldCursor);
    }
}

void CMainWindow::LoadPanelConfig(std::wstring& panelPath, CFilesWindow* panel, HKEY hSalamander, const wchar_t* reg)
{
    // On the wide facades, symmetric with SavePanelConfig — the pair
    // reads and writes through the same entry points, so a value written wide
    // cannot be looked up narrow.
    HKEY actKey;
    if (OpenKeyW(hSalamander, reg, actKey))
    {
        DWORD value;
        BOOL panelPathLoaded = FALSE;
        // Read the panel path as REG_SZ Unicode (matches the wide-only save in
        // SavePanelConfig). Older Sally configs that wrote ANSI PANEL_PATH miss
        // on the wide read and fall through to the rescue path on first launch.
        if (gRegistry != NULL)
        {
            RegistryResult result = gRegistry->GetString(actKey, PANEL_PATH_REG_W, panelPath);
            panelPathLoaded = result.success;
        }

        if (panelPathLoaded)
        {
            if (GetValueW(actKey, PANEL_HEADER_REG_W, REG_DWORD, &value, sizeof(DWORD)))
                panel->HeaderLineVisible = value;
            if (GetValueW(actKey, PANEL_VIEW_REG_W, REG_DWORD, &value, sizeof(DWORD)))
            {
                if (Configuration.ConfigVersion < 13 && !value) // conversion: the Detailed view was stored as FALSE
                    value = 2;
                panel->SelectViewTemplate(value, FALSE, FALSE, VALID_DATA_ALL, FALSE, TRUE);
            }
            if (GetValueW(actKey, PANEL_REVERSE_REG_W, REG_DWORD, &value, sizeof(DWORD)))
                panel->ReverseSort = value;
            if (GetValueW(actKey, PANEL_SORT_REG_W, REG_DWORD, &value, sizeof(DWORD)))
            {
                if (value > stAttr)
                    value = stName;
                panel->SortType = (CSortType)value;
            }
            if (GetValueW(actKey, PANEL_DIRLINE_REG_W, REG_DWORD, &value, sizeof(DWORD)))
                if ((BOOL)value != (panel->DirectoryLine->HWindow != NULL))
                    panel->ToggleDirectoryLine();
            if (GetValueW(actKey, PANEL_STATUS_REG_W, REG_DWORD, &value, sizeof(DWORD)))
                if ((BOOL)value != (panel->StatusLine->HWindow != NULL))
                    panel->ToggleStatusLine();
            GetValueW(actKey, PANEL_FILTER_ENABLE_W, REG_DWORD, &panel->FilterEnabled,
                     sizeof(DWORD));

            std::wstring filter;
            if (!GetStringValueW(actKey, PANEL_FILTER, filter))
            {
                filter.clear();
                if (Configuration.ConfigVersion < 22)
                {
                    wchar_t* filterHistory[1] = {};
                    LoadLegacyHistory(actKey, PANEL_FILTERHISTORY_REG, filterHistory, 1);
                    if (filterHistory[0] != NULL) // load the initial filter state as well
                    {
                        DWORD filterInverse = FALSE;
                        if (panel->FilterEnabled && Configuration.ConfigVersion < 14) // conversion: the inverse filter checkbox was removed
                            GetValueW(actKey, PANEL_FILTER_INVERSE_W, REG_DWORD, &filterInverse, sizeof(DWORD));
                        if (filterInverse)
                            filter = L"|";
                        else
                            filter.clear();
                        filter += filterHistory[0];
                        free(filterHistory[0]);
                    }
                }
                else
                    panel->FilterEnabled = FALSE;
            }
            if (!filter.empty())
                panel->Filter.SetMasksString(filter.c_str());

            panel->UpdateFilterSymbol();
            int errPos;
            if (!panel->Filter.PrepareMasks(errPos))
            {
                panel->Filter.SetMasksString(L"*.*");
                panel->Filter.PrepareMasks(errPos);
            }
        }

        CloseKey(actKey);
    }
}

void LoadIconOvrlsInfo(const wchar_t* root)
{
    HKEY hSalamander;
    if (OpenKey(HKEY_CURRENT_USER, root, hSalamander))
    {
        HKEY actKey;
        DWORD configVersion = 1; // this configuration is from version 1.52 or older
        if (OpenKey(hSalamander, SALAMANDER_VERSION_REG, actKey))
        {
            configVersion = 2; // this configuration is from version 1.6b1
            GetValueW(actKey, SALAMANDER_VERSIONREG_REG_W, REG_DWORD,
                     &configVersion, sizeof(DWORD));
            CloseKey(actKey);
        }
        if (OpenKey(hSalamander, SALAMANDER_CONFIG_REG, actKey))
        {
            ClearListOfDisabledCustomIconOverlays();
            DWORD disabledCustomIconOverlaysBufSize;
            if (GetValueW(actKey, CONFIG_ENABLECUSTICOVRLS_REG_W, REG_DWORD,
                         &Configuration.EnableCustomIconOverlays, sizeof(DWORD)) &&
                GetSizeW(actKey, CONFIG_DISABLEDCUSTICOVRLS_REG, REG_SZ, disabledCustomIconOverlaysBufSize))
            {
                if (disabledCustomIconOverlaysBufSize > 1) // <= 1 means an empty string, NULL is enough in that case
                {
                    Configuration.DisabledCustomIconOverlays = (wchar_t*)malloc(disabledCustomIconOverlaysBufSize);
                    if (Configuration.DisabledCustomIconOverlays == NULL)
                    {
                        TRACE_E(LOW_MEMORY);
                        Configuration.EnableCustomIconOverlays = FALSE; // for safety reasons (icon overlay handlers crash often)
                    }
                    else
                    {
                        if (!GetValueW(actKey, CONFIG_DISABLEDCUSTICOVRLS_REG, REG_SZ,
                                       Configuration.DisabledCustomIconOverlays, disabledCustomIconOverlaysBufSize))
                        {
                            free(Configuration.DisabledCustomIconOverlays);
                            Configuration.DisabledCustomIconOverlays = NULL;
                            Configuration.EnableCustomIconOverlays = FALSE; // for safety reasons (icon overlay handlers crash often)
                        }
                    }
                }
            }
            else
            {
                if (configVersion >= 41) // if this value is missing in newer configurations, disable overlays (older versions didn't have these variables, so it's not an error-leave overlays enabled)
                    Configuration.EnableCustomIconOverlays = FALSE;
            }

            CloseKey(actKey);
        }
        CloseKey(hSalamander);
    }
}

// #95: The main window becomes VISIBLE inside LoadConfig(): SetWindowPlacement() below
// applies the persisted showCmd. That happened ~100 lines BEFORE the theme was finalized,
// so the first painted frame was the default LIGHT theme and flipped to dark a few hundred
// milliseconds later (measured: ~250ms of a fully light-themed window). Finalize the theme -
// including the class erase brush - before the window can appear.
static void ApplyStartupThemeToMainWindow(HWND hWindow)
{
    DarkMode_SetThemeMode(Configuration.ThemeMode);
    ColorsChanged(TRUE, FALSE, TRUE); // rebuild color-dependent resources for the initial theme
    // the class brush is what WM_ERASEBKGND paints with before the children draw
    SetClassLongPtr(hWindow, GCLP_HBRBACKGROUND,
                    (LONG_PTR)(DarkMode_ShouldUseDark() ? DarkMode_GetMainFrameBrush()
                                                        : (HBRUSH)(COLOR_WINDOW + 1)));
    DarkMode_ApplyTitleBar(hWindow);
    DarkMode_ApplyToThreadTopLevelWindows(GetCurrentThreadId());
}

BOOL CMainWindow::LoadConfig(
    BOOL importingOldConfig,
    const sally::cmdline::CommandLineRequest* cmdLineParams)
{
    CALL_STACK_MESSAGE2("CMainWindow::LoadConfig(%d)", importingOldConfig);
    if (SALAMANDER_ROOT_REG == NULL)
        return FALSE;

    BOOL themeFinalized = FALSE; // #95: theme applied before the window can become visible

    LoadSaveToRegistryMutex.Enter();

    HKEY salamander;
    if (OpenKey(HKEY_CURRENT_USER, SALAMANDER_ROOT_REG, salamander))
    {
        HKEY actKey;
        BOOL ret = TRUE;

        IfExistSetSplashScreenText(LoadStrW(IDS_STARTUP_CONFIG));

        Configuration.ConfigVersion = 1; // this configuration is from version 1.52 or older
                                         //--- version
        if (OpenKey(salamander, SALAMANDER_VERSION_REG, actKey))
        {
            Configuration.ConfigVersion = 2; // this configuration is from version 1.6b1
            GetValueW(actKey, SALAMANDER_VERSIONREG_REG_W, REG_DWORD,
                     &Configuration.ConfigVersion, sizeof(DWORD));
            CloseKey(actKey);
        }

        //---  viewers

        EnterViewerMasksCS();
        LoadViewers(salamander, SALAMANDER_VIEWERS_REG, ViewerMasks);
        LeaveViewerMasksCS();
        LoadViewers(salamander, SALAMANDER_ALTVIEWERS_REG, AltViewerMasks);

        //---  editors

        LoadEditors(salamander, SALAMANDER_EDITORS_REG, EditorMasks);

        //---  colors
        if (OpenKey(salamander, SALAMANDER_CUSTOMCOLORS_REG, actKey))
        {
            wchar_t buff[10];
            int i;
            for (i = 0; i < NUMBER_OF_CUSTOMCOLORS; i++)
            {
                _itow_s(i + 1, buff, _countof(buff), 10);
                LoadRGB(actKey, buff, CustomColors[i]);
            }

            CloseKey(actKey);
        }

        if (OpenKey(salamander, SALAMANDER_COLORS_REG, actKey))
        {
            DWORD scheme;
            CurrentColors = UserColors;
            if (GetValueW(actKey, SALAMANDER_CLRSCHEME_REG_W, REG_DWORD, &scheme, sizeof(DWORD)))
            {
                // we added a new scheme (DOS Navigator) at position 3
                if (Configuration.ConfigVersion < 28 && scheme == 3)
                    scheme = 4;

                if (scheme == 0)
                    CurrentColors = SalamanderColors;
                else if (scheme == 1)
                    CurrentColors = ExplorerColors;
                else if (scheme == 2)
                    CurrentColors = NortonColors;
                else if (scheme == 3)
                    CurrentColors = NavigatorColors;
            }

            LoadRGBF(actKey, SALAMANDER_CLR_ITEM_FG_NORMAL_REG, UserColors[ITEM_FG_NORMAL]);
            LoadRGBF(actKey, SALAMANDER_CLR_ITEM_FG_SELECTED_REG, UserColors[ITEM_FG_SELECTED]);
            LoadRGBF(actKey, SALAMANDER_CLR_ITEM_FG_FOCUSED_REG, UserColors[ITEM_FG_FOCUSED]);
            LoadRGBF(actKey, SALAMANDER_CLR_ITEM_FG_FOCSEL_REG, UserColors[ITEM_FG_FOCSEL]);
            LoadRGBF(actKey, SALAMANDER_CLR_ITEM_FG_HIGHLIGHT_REG, UserColors[ITEM_FG_HIGHLIGHT]);

            LoadRGBF(actKey, SALAMANDER_CLR_ITEM_BK_NORMAL_REG, UserColors[ITEM_BK_NORMAL]);
            LoadRGBF(actKey, SALAMANDER_CLR_ITEM_BK_SELECTED_REG, UserColors[ITEM_BK_SELECTED]);
            LoadRGBF(actKey, SALAMANDER_CLR_ITEM_BK_FOCUSED_REG, UserColors[ITEM_BK_FOCUSED]);
            LoadRGBF(actKey, SALAMANDER_CLR_ITEM_BK_FOCSEL_REG, UserColors[ITEM_BK_FOCSEL]);
            LoadRGBF(actKey, SALAMANDER_CLR_ITEM_BK_HIGHLIGHT_REG, UserColors[ITEM_BK_HIGHLIGHT]);

            LoadRGBF(actKey, SALAMANDER_CLR_FOCUS_ACTIVE_NORMAL_REG, UserColors[FOCUS_ACTIVE_NORMAL]);
            LoadRGBF(actKey, SALAMANDER_CLR_FOCUS_ACTIVE_SELECTED_REG, UserColors[FOCUS_ACTIVE_SELECTED]);
            LoadRGBF(actKey, SALAMANDER_CLR_FOCUS_INACTIVE_NORMAL_REG, UserColors[FOCUS_FG_INACTIVE_NORMAL]);
            LoadRGBF(actKey, SALAMANDER_CLR_FOCUS_INACTIVE_SELECTED_REG, UserColors[FOCUS_FG_INACTIVE_SELECTED]);
            if (!LoadRGBF(actKey, SALAMANDER_CLR_FOCUS_BK_INACTIVE_NORMAL_REG, UserColors[FOCUS_BK_INACTIVE_NORMAL]))
                UserColors[FOCUS_BK_INACTIVE_NORMAL] = UserColors[ITEM_BK_NORMAL]; // conversion of older configurations
            if (!LoadRGBF(actKey, SALAMANDER_CLR_FOCUS_BK_INACTIVE_SELECTED_REG, UserColors[FOCUS_BK_INACTIVE_SELECTED]))
                UserColors[FOCUS_BK_INACTIVE_SELECTED] = UserColors[ITEM_BK_NORMAL]; // conversion of older configurations

            LoadRGBF(actKey, SALAMANDER_CLR_ICON_BLEND_SELECTED_REG, UserColors[ICON_BLEND_SELECTED]);
            LoadRGBF(actKey, SALAMANDER_CLR_ICON_BLEND_FOCUSED_REG, UserColors[ICON_BLEND_FOCUSED]);
            LoadRGBF(actKey, SALAMANDER_CLR_ICON_BLEND_FOCSEL_REG, UserColors[ICON_BLEND_FOCSEL]);

            LoadRGBF(actKey, SALAMANDER_CLR_PROGRESS_FG_NORMAL_REG, UserColors[PROGRESS_FG_NORMAL]);
            LoadRGBF(actKey, SALAMANDER_CLR_PROGRESS_FG_SELECTED_REG, UserColors[PROGRESS_FG_SELECTED]);
            LoadRGBF(actKey, SALAMANDER_CLR_PROGRESS_BK_NORMAL_REG, UserColors[PROGRESS_BK_NORMAL]);
            LoadRGBF(actKey, SALAMANDER_CLR_PROGRESS_BK_SELECTED_REG, UserColors[PROGRESS_BK_SELECTED]);

            LoadRGBF(actKey, SALAMANDER_CLR_HOT_PANEL_REG, UserColors[HOT_PANEL]);
            LoadRGBF(actKey, SALAMANDER_CLR_HOT_ACTIVE_REG, UserColors[HOT_ACTIVE]);
            LoadRGBF(actKey, SALAMANDER_CLR_HOT_INACTIVE_REG, UserColors[HOT_INACTIVE]);

            LoadRGBF(actKey, SALAMANDER_CLR_ACTIVE_CAPTION_FG_REG, UserColors[ACTIVE_CAPTION_FG]);
            LoadRGBF(actKey, SALAMANDER_CLR_ACTIVE_CAPTION_BK_REG, UserColors[ACTIVE_CAPTION_BK]);
            LoadRGBF(actKey, SALAMANDER_CLR_INACTIVE_CAPTION_FG_REG, UserColors[INACTIVE_CAPTION_FG]);
            LoadRGBF(actKey, SALAMANDER_CLR_INACTIVE_CAPTION_BK_REG, UserColors[INACTIVE_CAPTION_BK]);

            LoadRGBF(actKey, SALAMANDER_CLR_THUMBNAIL_FRAME_NORMAL_REG, UserColors[THUMBNAIL_FRAME_NORMAL]);
            LoadRGBF(actKey, SALAMANDER_CLR_THUMBNAIL_FRAME_SELECTED_REG, UserColors[THUMBNAIL_FRAME_SELECTED]);
            LoadRGBF(actKey, SALAMANDER_CLR_THUMBNAIL_FRAME_FOCUSED_REG, UserColors[THUMBNAIL_FRAME_FOCUSED]);
            LoadRGBF(actKey, SALAMANDER_CLR_THUMBNAIL_FRAME_FOCSEL_REG, UserColors[THUMBNAIL_FRAME_FOCSEL]);

            LoadRGBF(actKey, SALAMANDER_CLR_VIEWER_FG_NORMAL_REG, ViewerColors[VIEWER_FG_NORMAL]);
            LoadRGBF(actKey, SALAMANDER_CLR_VIEWER_BK_NORMAL_REG, ViewerColors[VIEWER_BK_NORMAL]);
            LoadRGBF(actKey, SALAMANDER_CLR_VIEWER_FG_SELECTED_REG, ViewerColors[VIEWER_FG_SELECTED]);
            LoadRGBF(actKey, SALAMANDER_CLR_VIEWER_BK_SELECTED_REG, ViewerColors[VIEWER_BK_SELECTED]);

            // load colors for file highlighting
            HKEY hHltKey;
            if (OpenKey(actKey, SALAMANDER_HLT, hHltKey))
            {
                HKEY hSubKey;
                wchar_t buf[30];
                wcscpy_s(buf, L"1");
                int i = 1;
                HighlightMasks->DestroyMembers();
                while (OpenKey(hHltKey, buf, hSubKey))
                {
                    std::wstring masks;
                    if (GetStringValueW(hSubKey, SALAMANDER_HLT_ITEM_MASKS, masks))
                    {
                        CHighlightMasksItem* item = new CHighlightMasksItem();
                        if (item == NULL || !item->Set(masks.c_str()))
                        {
                            TRACE_E(LOW_MEMORY);
                            if (item != NULL)
                                delete item;
                            continue;
                        }
                        int errPos;
                        item->Masks->PrepareMasks(errPos);

                        GetValue(hSubKey, SALAMANDER_HLT_ITEM_ATTR, REG_DWORD, &item->Attr, sizeof(DWORD));
                        GetValue(hSubKey, SALAMANDER_HLT_ITEM_VALIDATTR, REG_DWORD, &item->ValidAttr, sizeof(DWORD));

                        LoadRGBF(hSubKey, SALAMANDER_HLT_ITEM_FG_NORMAL_REG, item->NormalFg);
                        LoadRGBF(hSubKey, SALAMANDER_HLT_ITEM_FG_SELECTED_REG, item->SelectedFg);
                        LoadRGBF(hSubKey, SALAMANDER_HLT_ITEM_FG_FOCUSED_REG, item->FocusedFg);
                        LoadRGBF(hSubKey, SALAMANDER_HLT_ITEM_FG_FOCSEL_REG, item->FocSelFg);
                        LoadRGBF(hSubKey, SALAMANDER_HLT_ITEM_FG_HIGHLIGHT_REG, item->HighlightFg);

                        LoadRGBF(hSubKey, SALAMANDER_HLT_ITEM_BK_NORMAL_REG, item->NormalBk);
                        LoadRGBF(hSubKey, SALAMANDER_HLT_ITEM_BK_SELECTED_REG, item->SelectedBk);
                        LoadRGBF(hSubKey, SALAMANDER_HLT_ITEM_BK_FOCUSED_REG, item->FocusedBk);
                        LoadRGBF(hSubKey, SALAMANDER_HLT_ITEM_BK_FOCSEL_REG, item->FocSelBk);
                        LoadRGBF(hSubKey, SALAMANDER_HLT_ITEM_BK_HIGHLIGHT_REG, item->HighlightBk);
                        HighlightMasks->Add(item);
                        if (!HighlightMasks->IsGood())
                        {
                            HighlightMasks->ResetState();
                            delete item;
                        }
                        _itow_s(++i, buf, _countof(buf), 10);
                        CloseKey(hSubKey);
                    }
                }
                if (Configuration.ConfigVersion < 16) // add highlighting for encrypted files/directories
                {
                    CHighlightMasksItem* hItem = new CHighlightMasksItem();
                    if (hItem != NULL)
                    {
                        HighlightMasks->Add(hItem);
                        hItem->Set(L"*.*");
                        int errPos;
                        hItem->Masks->PrepareMasks(errPos);
                        hItem->NormalFg = RGBF(19, 143, 13, 0); // color taken from Windows XP
                        hItem->FocusedFg = RGBF(19, 143, 13, 0);
                        hItem->ValidAttr = FILE_ATTRIBUTE_ENCRYPTED;
                        hItem->Attr = FILE_ATTRIBUTE_ENCRYPTED;
                    }
                }
                CloseKey(hHltKey);
            }

            ColorsChanged(FALSE, TRUE, TRUE); // save time by updating only color-dependent items

            CloseKey(actKey);
        }

        //---  window

        WINDOWPLACEMENT place;
        BOOL useWinPlacement = FALSE;
        if (OpenKey(salamander, SALAMANDER_WINDOW_REG, actKey))
        {
            place.length = sizeof(WINDOWPLACEMENT);
            GetWindowPlacement(HWindow, &place);
            if (GetValueW(actKey, WINDOW_LEFT_REG_W, REG_DWORD,
                         &(place.rcNormalPosition.left), sizeof(DWORD)) &&
                GetValueW(actKey, WINDOW_RIGHT_REG_W, REG_DWORD,
                         &(place.rcNormalPosition.right), sizeof(DWORD)) &&
                GetValueW(actKey, WINDOW_TOP_REG_W, REG_DWORD,
                         &(place.rcNormalPosition.top), sizeof(DWORD)) &&
                GetValueW(actKey, WINDOW_BOTTOM_REG_W, REG_DWORD,
                         &(place.rcNormalPosition.bottom), sizeof(DWORD)) &&
                GetValueW(actKey, WINDOW_SHOW_REG_W, REG_DWORD,
                         &(place.showCmd), sizeof(DWORD)))
            {
                // WIDE, matching the write side (see the comment there): these
                // are REG_SZ values read back through RegQueryValueExW, so the payload is UTF-16.
                // The buffer-size argument is BYTES (it goes straight to RegQueryValueExW's
                // lpcbData), hence sizeof(buf) rather than the old literal 20 that matched a
                // char[20]. swscanf_s's return value is checked so a value left over from the
                // old narrow format - or one corrupted by the length bug this fixes - cleanly
                // falls back to the existing default instead of yielding an arbitrary number.
                // Zero-initialized and deliberately sized one wchar_t short: RegQueryValueExW does
                // not guarantee a terminator when the stored data lacks one, so reserving the last
                // slot keeps swscanf_s from running off the end of a malformed value.
                wchar_t buf[20] = {0};
                if (GetValueW(actKey, WINDOW_SPLIT_REG, REG_SZ, buf, sizeof(buf) - sizeof(wchar_t)))
                {
                    if (swscanf_s(buf, L"%lf", &SplitPosition) == 1)
                    {
                        SplitPosition /= 100;
                        if (SplitPosition < 0)
                            SplitPosition = 0;
                        if (SplitPosition > 1)
                            SplitPosition = 1;
                    }
                }
                buf[0] = L'\0';
                if (GetValueW(actKey, WINDOW_BEFOREZOOMSPLIT_REG, REG_SZ, buf, sizeof(buf) - sizeof(wchar_t)))
                {
                    if (swscanf_s(buf, L"%lf", &BeforeZoomSplitPosition) == 1)
                    {
                        BeforeZoomSplitPosition /= 100;
                        if (BeforeZoomSplitPosition < 0)
                            BeforeZoomSplitPosition = 0;
                        if (BeforeZoomSplitPosition > 1)
                            BeforeZoomSplitPosition = 1;
                    }
                }
                useWinPlacement = TRUE;
            }
            else
                ret = FALSE;

            CloseKey(actKey);
        }
        else
            ret = FALSE;

        if (OpenKey(salamander, FINDDIALOG_WINDOW_REG, actKey))
        {
            Configuration.FindDialogWindowPlacement.length = sizeof(WINDOWPLACEMENT);

            GetValueW(actKey, WINDOW_LEFT_REG_W, REG_DWORD,
                     &(Configuration.FindDialogWindowPlacement.rcNormalPosition.left), sizeof(DWORD));
            GetValueW(actKey, WINDOW_RIGHT_REG_W, REG_DWORD,
                     &(Configuration.FindDialogWindowPlacement.rcNormalPosition.right), sizeof(DWORD));
            GetValueW(actKey, WINDOW_TOP_REG_W, REG_DWORD,
                     &(Configuration.FindDialogWindowPlacement.rcNormalPosition.top), sizeof(DWORD));
            GetValueW(actKey, WINDOW_BOTTOM_REG_W, REG_DWORD,
                     &(Configuration.FindDialogWindowPlacement.rcNormalPosition.bottom), sizeof(DWORD));
            GetValueW(actKey, WINDOW_SHOW_REG_W, REG_DWORD,
                     &(Configuration.FindDialogWindowPlacement.showCmd), sizeof(DWORD));

            GetValueW(actKey, FINDDIALOG_NAMEWIDTH_REG_W, REG_DWORD,
                     &(Configuration.FindColNameWidth), sizeof(DWORD));
            CloseKey(actKey);
        }

        //---  default directories

        if (OpenKey(salamander, SALAMANDER_DEFDIRS_REG, actKey))
        {
            IRegistry* registry = GetMainWindowRegistry();
            std::vector<std::wstring> valueNames;
            RegistryResult enumResult = registry != NULL ? registry->EnumValues(actKey, valueNames)
                                                         : RegistryResult::Error(ERROR_INVALID_FUNCTION);
            if (enumResult.success)
            {
                wchar_t dir[4] = L" :\\"; // reset DefaultDir
                wchar_t d;
                for (d = L'A'; d <= L'Z'; d++)
                {
                    dir[0] = d;
                    DefaultDir[d - L'A'] = dir;
                }

                for (size_t i = 0; i < valueNames.size(); i++)
                {
                    // The value name is a single drive letter. It used to be rendered down to
                    // ANSI with WideCharToMultiByte into a buffer the sweep had widened to
                    // wchar_t - that API counts and writes BYTES, so the widened buffer was a
                    // byte buffer wearing the wrong type. Read the letter from the wide name.
                    const std::wstring& valueName = valueNames[i];
                    if (valueName.length() != 1)
                    {
                        gPrompter->ShowError(LoadStrW(IDS_ERRORLOADCONFIG), LoadStrW(IDS_UNEXPECTEDVALUE));
                        continue;
                    }

                    // IRegistry::GetString is the wide primitive; GetStringA is only an ANSI
                    // wrapper around it that adds a WideCharToMultiByte on the way out.
                    std::wstring pathValue;
                    RegistryResult valueResult = registry->GetString(actKey, valueName.c_str(), pathValue);
                    if (valueResult.success)
                    {
                        const wchar_t* path = pathValue.c_str();
                        wchar_t d2 = (wchar_t)towlower(valueName[0]);
                        if (d2 >= L'a' && d2 <= L'z')
                        {
                            size_t dataLen = pathValue.length() + 1;
                            if (dataLen > 2 && (wchar_t)towlower(path[0]) == d2 &&
                                path[1] == L':' && path[2] == L'\\')
                                DefaultDir[d2 - L'a'] = path;
                            else
                                gPrompter->ShowError(LoadStrW(IDS_ERRORLOADCONFIG), LoadStrW(IDS_UNEXPECTEDVALUE));
                        }
                        else
                            gPrompter->ShowError(LoadStrW(IDS_ERRORLOADCONFIG), LoadStrW(IDS_UNEXPECTEDVALUE));
                    }
                    else if (valueResult.errorCode == ERROR_INVALID_DATATYPE)
                        gPrompter->ShowError(LoadStrW(IDS_ERRORLOADCONFIG), LoadStrW(IDS_UNEXPECTEDVALUETYPE));
                    else
                        gPrompter->ShowError(LoadStrW(IDS_ERRORLOADCONFIG), GetErrorTextOwned(valueResult.errorCode).c_str());
                }
            }
            else if (enumResult.errorCode != ERROR_FILE_NOT_FOUND)
                gPrompter->ShowError(LoadStrW(IDS_ERRORLOADCONFIG), GetErrorTextOwned(enumResult.errorCode).c_str());
            CloseKey(actKey);
        }

        //---  password manager

        if (OpenKey(salamander, SALAMANDER_PWDMNGR_REG, actKey))
        {
            PasswordManager.Load(actKey);
            CloseKey(actKey);
        }

        //---  hot paths

        if (OpenKey(salamander, SALAMANDER_HOTPATHS_REG, actKey))
        {
            if (Configuration.ConfigVersion == 1) // HotPaths need conversion
                HotPaths.Load1_52(actKey);
            else
                HotPaths.Load(actKey);

            CloseKey(actKey);
        }

        //--- view templates

        if (OpenKey(salamander, SALAMANDER_VIEWTEMPLATES_REG, actKey))
        {
            ViewTemplates.Load(actKey);
            CloseKey(actKey);
        }

        //---  Plugins Order
        if (OpenKey(salamander, SALAMANDER_PLUGINSORDER, actKey))
        {
            Plugins.LoadOrder(HWindow, actKey);
            CloseKey(actKey);
        }

        //---  Plugins
        if (OpenKey(salamander, SALAMANDER_PLUGINS, actKey)) // otherwise default values
        {
            Plugins.Load(HWindow, actKey);
            CloseKey(actKey);
        }
        else
        {
            if (Configuration.ConfigVersion >= 6)
                Plugins.Clear(); // does not even want default archivers ...
        }

        //---  Packers & Unpackers
        if (OpenKey(salamander, SALAMANDER_PACKANDUNPACK, actKey))
        {
            GetValueW(actKey, SALAMANDER_SIMPLEICONSINARCHIVES_W, REG_DWORD,
                     &(Configuration.UseSimpleIconsInArchives), sizeof(DWORD));
            //---  Custom Packers
            HKEY actSubKey;
            if (OpenKey(actKey, SALAMANDER_CUSTOMPACKERS, actSubKey))
            {
                PackerConfig.DeleteAllPackers();
                HKEY itemKey;
                wchar_t buf[30];
                int i = 1;
                wcscpy_s(buf, L"1");
                while (OpenKey(actSubKey, buf, itemKey))
                {
                    PackerConfig.Load(itemKey);
                    CloseKey(itemKey);
                    _itow_s(++i, buf, _countof(buf), 10);
                }
                GetValue(actSubKey, SALAMANDER_ANOTHERPANEL, REG_DWORD,
                         &(Configuration.UseAnotherPanelForPack), sizeof(DWORD));
                int pp;
                if (GetValue(actSubKey, SALAMANDER_PREFFERED, REG_DWORD, &pp, sizeof(DWORD)))
                {
                    PackerConfig.SetPreferedPacker(pp);
                }
                CloseKey(actSubKey);
                // add new items introduced since the previous version :-)
                PackerConfig.AddDefault(Configuration.ConfigVersion);
            }
            //---  Custom Unpackers
            if (OpenKey(actKey, SALAMANDER_CUSTOMUNPACKERS, actSubKey))
            {
                UnpackerConfig.DeleteAllUnpackers();
                HKEY itemKey;
                wchar_t buf[30];
                int i = 1;
                wcscpy_s(buf, L"1");
                while (OpenKey(actSubKey, buf, itemKey))
                {
                    UnpackerConfig.Load(itemKey);
                    CloseKey(itemKey);
                    _itow_s(++i, buf, _countof(buf), 10);
                }
                GetValue(actSubKey, SALAMANDER_ANOTHERPANEL, REG_DWORD,
                         &(Configuration.UseAnotherPanelForUnpack), sizeof(DWORD));
                GetValue(actSubKey, SALAMANDER_NAMEBYARCHIVE, REG_DWORD,
                         &(Configuration.UseSubdirNameByArchiveForUnpack), sizeof(DWORD));
                int pp;
                if (GetValue(actSubKey, SALAMANDER_PREFFERED, REG_DWORD, &pp, sizeof(DWORD)))
                {
                    UnpackerConfig.SetPreferedUnpacker(pp);
                }
                CloseKey(actSubKey);
                // add new items introduced since the previous version
                UnpackerConfig.AddDefault(Configuration.ConfigVersion);
            }
            //---  Predefined Packers
            if (OpenKey(actKey, SALAMANDER_PREDPACKERS, actSubKey))
            {
                // j.r.
                // External Archivers Locations: default values are no longer deleted during configuration load;
                // they are only updated. If the registry contains an incomplete or unknown entry,
                // it is ignored. Only when the Title matches one of the default values are its paths used.
                // ArchiverConfig.DeleteAllArchivers();
                HKEY itemKey;
                wchar_t buf[30];
                int i = 1;
                wcscpy_s(buf, L"1");
                while (OpenKey(actSubKey, buf, itemKey))
                {
                    ArchiverConfig.Load(itemKey);
                    CloseKey(itemKey);
                    _itow_s(++i, buf, _countof(buf), 10);
                }
                CloseKey(actSubKey);
                // add new items introduced since the previous version
                // ArchiverConfig.AddDefault(Configuration.ConfigVersion); // j.r. no longer needed
            }
            //---  Archive Association
            if (OpenKey(actKey, SALAMANDER_ARCHIVEASSOC, actSubKey))
            {
                PackerFormatConfig.DeleteAllFormats();
                HKEY itemKey;
                wchar_t buf[30];
                int i = 1;
                wcscpy_s(buf, L"1");
                while (OpenKey(actSubKey, buf, itemKey))
                {
                    PackerFormatConfig.Load(itemKey);
                    CloseKey(itemKey);
                    _itow_s(++i, buf, _countof(buf), 10);
                }
                CloseKey(actSubKey);
                // add new items introduced since the previous version
                PackerFormatConfig.AddDefault(Configuration.ConfigVersion);
                PackerFormatConfig.BuildArray();
            }
            CloseKey(actKey);
        }

        Plugins.CheckData(); // adjust loaded data

        //---  user menu

        IfExistSetSplashScreenText(LoadStrW(IDS_STARTUP_USERMENU));

        if (OpenKey(salamander, SALAMANDER_USERMENU_REG, actKey))
        {
            HKEY subKey;
            wchar_t buf[30];
            wcscpy_s(buf, L"1");
            std::wstring name;
            std::wstring command;
            std::wstring arguments;
            std::wstring initDir;
            int throughShell, closeShell, useWindow;
            int showInToolbar, separator;
            CUserMenuItemType type;
            std::wstring icon;
            int i = 1;
            UserMenuItems->DestroyMembers();

            CUserMenuIconDataArr* bkgndReaderData = new CUserMenuIconDataArr();

            while (OpenKey(actKey, buf, subKey))
            {
                if (gRegistry->GetString(subKey, USERMENU_ITEMNAME_REG, name).success &&
                    gRegistry->GetString(subKey, USERMENU_COMMAND_REG, command).success &&
                    GetValue(subKey, USERMENU_SHELL_REG, REG_DWORD,
                             &throughShell, sizeof(DWORD)) &&
                    GetValue(subKey, USERMENU_CLOSE_REG, REG_DWORD,
                             &closeShell, sizeof(DWORD)))
                {
                    if (Configuration.ConfigVersion == 1 ||
                        !gRegistry->GetString(subKey, USERMENU_ARGUMENTS_REG, arguments).success)
                    {
                        // convert from user-menu version 1.52 to the current version
                        size_t variable = std::wstring::npos;
                        for (size_t pos = 0; pos < command.length(); ++pos)
                        {
                            if (command[pos] != L'%')
                                continue;
                            if (pos + 1 < command.length() && command[pos + 1] == L'%')
                                ++pos;
                            else
                            {
                                variable = pos;
                                break;
                            }
                        }
                        arguments.clear();
                        if (variable == std::wstring::npos)
                        {
                            // no parameters
                        }
                        else
                        {
                            const size_t separatorPos = command.rfind(L' ', variable);
                            if (separatorPos != std::wstring::npos)
                            {
                                const std::wstring legacyArguments = command.substr(separatorPos + 1);
                                command.resize(separatorPos);
                                for (size_t pos = 0; pos < legacyArguments.length(); ++pos)
                                {
                                    if (legacyArguments[pos] == L'%' && pos + 1 < legacyArguments.length())
                                    {
                                        const wchar_t* add = L"";
                                        switch (towlower(legacyArguments[++pos]))
                                        {
                                        case '%':
                                            add = L"%";
                                            break;
                                        case 'd':
                                            add = L"$(Drive)";
                                            break;
                                        case 'p':
                                            add = L"$(Path)";
                                            break;
                                        case 'h':
                                            add = L"$(DOSPath)";
                                            break;
                                        case 'f':
                                            add = L"$(Name)";
                                            break;
                                        case 's':
                                            add = L"$(DOSName)";
                                            break;
                                        }
                                        arguments.append(add);
                                    }
                                    else
                                        arguments.push_back(legacyArguments[pos]);
                                }
                            }
                        }
                    }
                    if (Configuration.ConfigVersion == 1 ||
                        !gRegistry->GetString(subKey, USERMENU_INITDIR_REG, initDir).success)
                    {
                        initDir = L"$(Drive)$(Path)";
                    }
                    if (Configuration.ConfigVersion == 1 ||
                        !GetValue(subKey, USERMENU_USEWINDOW_REG, REG_DWORD, &useWindow, sizeof(DWORD)))
                    {
                        useWindow = TRUE;
                    }

                    if (Configuration.ConfigVersion == 1 ||
                        !gRegistry->GetString(subKey, USERMENU_ICON_REG, icon).success)
                    {
                        icon.clear();
                    }

                    if (Configuration.ConfigVersion == 1 ||
                        !GetValue(subKey, USERMENU_SEPARATOR_REG, REG_DWORD, &separator, sizeof(DWORD)))
                    {
                        separator = FALSE;
                    }

                    if (!GetValue(subKey, USERMENU_TYPE_REG, REG_DWORD, &type, sizeof(DWORD)))
                    {
                        type = separator ? umitSeparator : umitItem;
                    }

                    if (Configuration.ConfigVersion == 1 ||
                        !GetValue(subKey, USERMENU_SHOWINTOOLBAR_REG, REG_DWORD, &showInToolbar, sizeof(DWORD)))
                    {
                        showInToolbar = TRUE;
                    }

                    CUserMenuItem* item = new CUserMenuItem(name.c_str(), command.c_str(), arguments.c_str(), initDir.c_str(), icon.c_str(),
                                                            throughShell, closeShell, useWindow,
                                                            showInToolbar, type, bkgndReaderData);
                    if (item != NULL && item->IsGood())
                    {
                        UserMenuItems->Add(item);
                        if (!UserMenuItems->IsGood())
                        {
                            delete item;
                            UserMenuItems->ResetState();
                            break;
                        }
                    }
                    else
                    {
                        if (item != NULL)
                            delete item;
                        TRACE_E(LOW_MEMORY);
                        break;
                    }
                }
                else
                    break;
                _itow_s(++i, buf, _countof(buf), 10);
                CloseKey(subKey);
            }

            UserMenuIconBkgndReader.StartBkgndReadingIcons(bkgndReaderData); // CAUTION: frees 'bkgndReaderData'

            CloseKey(actKey);
        }

        IfExistSetSplashScreenText(LoadStrW(IDS_STARTUP_CONFIG));

        //---  configuration

        DWORD cmdLine = 0, cmdLineFocus = 0;
        DWORD rightPanelFocused = FALSE;
        Configuration.ThemeMode = THEME_MODE_LIGHT;
        if (OpenKey(salamander, SALAMANDER_CONFIG_REG, actKey))
        {
            if (importingOldConfig)
            {
                GetValueW(actKey, CONFIG_ONLYONEINSTANCE_REG_W, REG_DWORD,
                         &Configuration.OnlyOneInstance, sizeof(DWORD));
            }
            //---  top rebar begin
            GetValueW(actKey, CONFIG_MENUINDEX_REG_W, REG_DWORD,
                     &Configuration.MenuIndex, sizeof(DWORD));
            GetValueW(actKey, CONFIG_MENUBREAK_REG_W, REG_DWORD,
                     &Configuration.MenuBreak, sizeof(DWORD));
            GetValueW(actKey, CONFIG_MENUWIDTH_REG_W, REG_DWORD,
                     &Configuration.MenuWidth, sizeof(DWORD));
            GetValueW(actKey, CONFIG_TOOLBARINDEX_REG_W, REG_DWORD,
                     &Configuration.TopToolbarIndex, sizeof(DWORD));
            GetValueW(actKey, CONFIG_TOOLBARBREAK_REG_W, REG_DWORD,
                     &Configuration.TopToolbarBreak, sizeof(DWORD));
            GetValueW(actKey, CONFIG_TOOLBARWIDTH_REG_W, REG_DWORD,
                     &Configuration.TopToolbarWidth, sizeof(DWORD));
            GetValueW(actKey, CONFIG_PLUGINSBARINDEX_REG_W, REG_DWORD,
                     &Configuration.PluginsBarIndex, sizeof(DWORD));
            GetValueW(actKey, CONFIG_PLUGINSBARBREAK_REG_W, REG_DWORD,
                     &Configuration.PluginsBarBreak, sizeof(DWORD));
            GetValueW(actKey, CONFIG_PLUGINSBARWIDTH_REG_W, REG_DWORD,
                     &Configuration.PluginsBarWidth, sizeof(DWORD));
            GetValueW(actKey, CONFIG_USERMENUINDEX_REG_W, REG_DWORD,
                     &Configuration.UserMenuToolbarIndex, sizeof(DWORD));
            GetValueW(actKey, CONFIG_USERMENUBREAK_REG_W, REG_DWORD,
                     &Configuration.UserMenuToolbarBreak, sizeof(DWORD));
            GetValueW(actKey, CONFIG_USERMENUWIDTH_REG_W, REG_DWORD,
                     &Configuration.UserMenuToolbarWidth, sizeof(DWORD));
            GetValueW(actKey, CONFIG_USERMENULABELS_REG_W, REG_DWORD,
                     &Configuration.UserMenuToolbarLabels, sizeof(DWORD));
            GetValueW(actKey, CONFIG_HOTPATHSINDEX_REG_W, REG_DWORD,
                     &Configuration.HotPathsBarIndex, sizeof(DWORD));
            GetValueW(actKey, CONFIG_HOTPATHSBREAK_REG_W, REG_DWORD,
                     &Configuration.HotPathsBarBreak, sizeof(DWORD));
            GetValueW(actKey, CONFIG_HOTPATHSWIDTH_REG_W, REG_DWORD,
                     &Configuration.HotPathsBarWidth, sizeof(DWORD));
            GetValueW(actKey, CONFIG_DRIVEBARINDEX_REG_W, REG_DWORD,
                     &Configuration.DriveBarIndex, sizeof(DWORD));
            GetValueW(actKey, CONFIG_DRIVEBARBREAK_REG_W, REG_DWORD,
                     &Configuration.DriveBarBreak, sizeof(DWORD));
            GetValueW(actKey, CONFIG_DRIVEBARWIDTH_REG_W, REG_DWORD,
                     &Configuration.DriveBarWidth, sizeof(DWORD));
            GetValueW(actKey, CONFIG_GRIPSVISIBLE_REG_W, REG_DWORD,
                     &Configuration.GripsVisible, sizeof(DWORD));
            //---  top rebar end
            GetValueW(actKey, CONFIG_FILENAMEFORMAT_REG_W, REG_DWORD,
                     &Configuration.FileNameFormat, sizeof(DWORD));
            GetValueW(actKey, CONFIG_SIZEFORMAT_REG_W, REG_DWORD,
                     &Configuration.SizeFormat, sizeof(DWORD));
            // automatic conversion from "mixed-case" to "partially-mixed-case"
            if (Configuration.FileNameFormat == 1)
                Configuration.FileNameFormat = 7;

            GetValueW(actKey, CONFIG_SELECTION_REG_W, REG_DWORD,
                     &Configuration.IncludeDirs, sizeof(DWORD));
            GetValueW(actKey, CONFIG_COPYFINDTEXT_REG_W, REG_DWORD,
                     &Configuration.CopyFindText, sizeof(DWORD));
            GetValueW(actKey, CONFIG_CLEARREADONLY_REG_W, REG_DWORD,
                     &Configuration.ClearReadOnly, sizeof(DWORD));
            GetValueW(actKey, CONFIG_PRIMARYCONTEXTMENU_REG_W, REG_DWORD,
                     &Configuration.PrimaryContextMenu, sizeof(DWORD));
            GetValueW(actKey, CONFIG_NOTHIDDENSYSTEM_REG_W, REG_DWORD,
                     &Configuration.NotHiddenSystemFiles, sizeof(DWORD));
            GetValueW(actKey, CONFIG_RECYCLEBIN_REG_W, REG_DWORD,
                     &Configuration.UseRecycleBin, sizeof(DWORD));
            // Adopt only a value that was really there. Pre-unicode read straight into the
            // live mask buffer (GetWritableMasksString), and GetValue leaves that buffer
            // untouched when the value is missing - so an absent key left the built-in mask
            // standing. Reading into a fresh std::wstring and assigning it unconditionally
            // replaced that default with an empty mask group, which matches nothing.
            std::wstring recycleMasks;
            if (GetStringValueW(actKey, CONFIG_RECYCLEMASKS_REG, recycleMasks))
                Configuration.RecycleMasks.SetMasksString(recycleMasks.c_str());
            GetValueW(actKey, CONFIG_SAVEONEXIT_REG_W, REG_DWORD,
                     &Configuration.AutoSave, sizeof(DWORD));
            GetValueW(actKey, CONFIG_SHOWGREPERRORS_REG_W, REG_DWORD,
                     &Configuration.ShowGrepErrors, sizeof(DWORD));
            GetValueW(actKey, CONFIG_FINDFULLROW_REG_W, REG_DWORD,
                     &Configuration.FindFullRowSelect, sizeof(DWORD));
            GetValueW(actKey, CONFIG_FINDFILETYPEMODE_REG_W, REG_DWORD,
                     &Configuration.FindFileTypeMode, sizeof(DWORD));
            if (Configuration.FindFileTypeMode < 0 || Configuration.FindFileTypeMode > 2)
                Configuration.FindFileTypeMode = 0;
            if (Configuration.ConfigVersion <= 6)
                Configuration.ShowGrepErrors = FALSE; // force FALSE so we don't annoy users unnecessarily (others do it this way too)
            GetValueW(actKey, CONFIG_MINBEEPWHENDONE_REG_W, REG_DWORD,
                     &Configuration.MinBeepWhenDone, sizeof(DWORD));
            GetValueW(actKey, CONFIG_CLOSESHELL_REG_W, REG_DWORD,
                     &Configuration.CloseShell, sizeof(DWORD));
            GetValueW(actKey, CONFIG_RIGHT_FOCUS_REG_W, REG_DWORD,
                     &rightPanelFocused, sizeof(DWORD));
            GetValueW(actKey, CONFIG_ALWAYSONTOP_REG_W, REG_DWORD,
                     &Configuration.AlwaysOnTop, sizeof(DWORD));
            GetValueW(actKey, CONFIG_COMMANDSHELL_KIND_REG_W, REG_DWORD,
                     &Configuration.CommandShellTargetKind, sizeof(DWORD));
            if (Configuration.CommandShellTargetKind < 0 || Configuration.CommandShellTargetKind > 2)
                Configuration.CommandShellTargetKind = 0;
            if (gRegistry != NULL)
            {
                std::wstring profileValue;
                if (gRegistry->GetString(actKey, CONFIG_COMMANDSHELL_PROFILE_GUID_REG_W, profileValue).success)
                    Configuration.CommandShellProfileGuid = profileValue;
                if (gRegistry->GetString(actKey, CONFIG_COMMANDSHELL_PROFILE_NAME_REG_W, profileValue).success)
                    Configuration.CommandShellProfileName = profileValue;
            }
            //      GetValue(actKey, CONFIG_FASTDIRMOVE_REG, REG_DWORD,
            //               &Configuration.FastDirectoryMove, sizeof(DWORD));
            GetValueW(actKey, CONFIG_SORTUSESLOCALE_REG_W, REG_DWORD,
                     &Configuration.SortUsesLocale, sizeof(DWORD));
            GetValueW(actKey, CONFIG_SORTDETECTNUMBERS_REG_W, REG_DWORD,
                     &Configuration.SortDetectNumbers, sizeof(DWORD));
            GetValueW(actKey, CONFIG_SORTNEWERONTOP_REG_W, REG_DWORD,
                     &Configuration.SortNewerOnTop, sizeof(DWORD));
            GetValueW(actKey, CONFIG_SORTDIRSBYNAME_REG_W, REG_DWORD,
                     &Configuration.SortDirsByName, sizeof(DWORD));
            GetValueW(actKey, CONFIG_SORTDIRSBYEXT_REG_W, REG_DWORD,
                     &Configuration.SortDirsByExt, sizeof(DWORD));
            GetValueW(actKey, CONFIG_SAVEHISTORY_REG_W, REG_DWORD,
                     &Configuration.SaveHistory, sizeof(DWORD));
            GetValueW(actKey, CONFIG_SAVEWORKDIRS_REG_W, REG_DWORD,
                     &Configuration.SaveWorkDirs, sizeof(DWORD));
            GetValueW(actKey, CONFIG_ENABLECMDLINEHISTORY_REG_W, REG_DWORD,
                     &Configuration.EnableCmdLineHistory, sizeof(DWORD));
            GetValueW(actKey, CONFIG_SAVECMDLINEHISTORY_REG_W, REG_DWORD,
                     &Configuration.SaveCmdLineHistory, sizeof(DWORD));
            //      GetValue(actKey, CONFIG_LANTASTICCHECK_REG, REG_DWORD,
            //               &Configuration.LantasticCheck, sizeof(DWORD));
            GetValueW(actKey, CONFIG_STATUSAREA_REG_W, REG_DWORD,
                     &Configuration.StatusArea, sizeof(DWORD));
            if (!GetValueW(actKey, CONFIG_FULLROWSELECT_REG_W, REG_DWORD,
                          &Configuration.FullRowSelect, sizeof(DWORD)))
            {
                // we don't want conversion - force TRUE
                //        if (GetValueW(actKey, CONFIG_EXPLORERLOOK_REG_W, REG_DWORD,
                //                     &Configuration.FullRowSelect, sizeof(DWORD)))
                //        {
                DeleteValue(actKey, CONFIG_EXPLORERLOOK_REG);
                //          Configuration.FullRowSelect = !Configuration.FullRowSelect;
                //        }
            }
            GetValueW(actKey, CONFIG_FULLROWHIGHLIGHT_REG_W, REG_DWORD,
                     &Configuration.FullRowHighlight, sizeof(DWORD));
            GetValueW(actKey, CONFIG_USEICONTINCTURE_REG_W, REG_DWORD,
                     &Configuration.UseIconTincture, sizeof(DWORD));
            GetValueW(actKey, CONFIG_SHOWPANELCAPTION_REG_W, REG_DWORD,
                     &Configuration.ShowPanelCaption, sizeof(DWORD));
            GetValueW(actKey, CONFIG_SHOWPANELZOOM_REG_W, REG_DWORD,
                     &Configuration.ShowPanelZoom, sizeof(DWORD));
            GetValueW(actKey, CONFIG_SINGLECLICK_REG_W, REG_DWORD,
                     &Configuration.SingleClick, sizeof(DWORD));
            //      GetValue(actKey, CONFIG_SHOWTIPOFTHEDAY_REG, REG_DWORD,
            //               &Configuration.ShowTipOfTheDay, sizeof(DWORD));
            //      GetValue(actKey, CONFIG_LASTTIPOFTHEDAY_REG, REG_DWORD,
            //               &Configuration.LastTipOfTheDay, sizeof(DWORD));
            GetStringValueW(actKey, CONFIG_INFOLINECONTENT_REG, Configuration.InfoLineContent);
            GetStringValueW(actKey, CONFIG_IFPATHISINACCESSIBLEGOTO_REG,
                            Configuration.IfPathIsInaccessibleGoTo);
            if (!GetValueW(actKey, CONFIG_IFPATHISINACCESSIBLEGOTOISMYDOCS_REG_W, REG_DWORD,
                          &Configuration.IfPathIsInaccessibleGoToIsMyDocs, sizeof(DWORD)))
            {
                std::wstring path;
                GetIfPathIsInaccessibleGoToW(path, TRUE);
                if (IsTheSamePath(path.c_str(), Configuration.IfPathIsInaccessibleGoTo.c_str())) // user wants to go to My Documents
                {
                    Configuration.IfPathIsInaccessibleGoToIsMyDocs = TRUE;
                    Configuration.IfPathIsInaccessibleGoTo.clear();
                }
                else
                    Configuration.IfPathIsInaccessibleGoToIsMyDocs = FALSE;
            }
            GetValueW(actKey, CONFIG_HOTPATH_AUTOCONFIG_W, REG_DWORD,
                     &Configuration.HotPathAutoConfig, sizeof(DWORD));
            GetValueW(actKey, CONFIG_LASTUSEDSPEEDLIM_REG_W, REG_DWORD,
                     &Configuration.LastUsedSpeedLimit, sizeof(DWORD));
            GetValueW(actKey, CONFIG_QUICKSEARCHENTER_REG_W, REG_DWORD,
                     &Configuration.QuickSearchEnterAlt, sizeof(DWORD));
            GetValueW(actKey, CONFIG_CHD_SHOWMYDOC_W, REG_DWORD,
                     &Configuration.ChangeDriveShowMyDoc, sizeof(DWORD));
            GetValueW(actKey, CONFIG_CHD_SHOWCLOUDSTOR_W, REG_DWORD,
                     &Configuration.ChangeDriveCloudStorage, sizeof(DWORD));
            GetValueW(actKey, CONFIG_CHD_SHOWANOTHER_W, REG_DWORD,
                     &Configuration.ChangeDriveShowAnother, sizeof(DWORD));
            GetValueW(actKey, CONFIG_CHD_SHOWNET_W, REG_DWORD,
                     &Configuration.ChangeDriveShowNet, sizeof(DWORD));
            GetValueW(actKey, CONFIG_SEARCHFILECONTENT_W, REG_DWORD,
                     &Configuration.SearchFileContent, sizeof(DWORD));
            GetValueW(actKey, CONFIG_LASTPLUGINVER_W, REG_DWORD,
                     &Configuration.LastPluginVer, sizeof(DWORD));
            GetValueW(actKey, CONFIG_LASTPLUGINVER_OP_W, REG_DWORD,
                     &Configuration.LastPluginVerOP, sizeof(DWORD));
            GetValueW(actKey, CONFIG_QUICKRENAME_SELALL_REG_W, REG_DWORD,
                     &Configuration.QuickRenameSelectAll, sizeof(DWORD));
            GetValueW(actKey, CONFIG_EDITNEW_SELALL_REG_W, REG_DWORD,
                     &Configuration.EditNewSelectAll, sizeof(DWORD));
            // "Use salopen.exe" is no longer read: salopen.exe was retired along with the
            // Configuration.UseSalOpen setting. The value is left in the registry rather than
            // deleted, so downgrading to an older build finds its own setting intact.
            GetValueW(actKey, CONFIG_NETWAREFASTDIRMOVE_REG_W, REG_DWORD,
                     &Configuration.NetwareFastDirMove, sizeof(DWORD));
            if (Windows7AndLater)
                GetValueW(actKey, CONFIG_ASYNCCOPYALG_REG_W, REG_DWORD,
                         &Configuration.UseAsyncCopyAlg, sizeof(DWORD));
            GetValueW(actKey, CONFIG_RELOAD_ENV_VARS_REG_W, REG_DWORD,
                     &Configuration.ReloadEnvVariables, sizeof(DWORD));
            GetValueW(actKey, CONFIG_SHIFTFORHOTPATHS_REG_W, REG_DWORD,
                     &Configuration.ShiftForHotPaths, sizeof(DWORD));
            //      GetValue(actKey, CONFIG_LANGUAGE_REG, REG_SZ,
            //               Configuration.SLGName, MAX_PATH);
            //      GetValueW(actKey, CONFIG_USEALTLANGFORPLUGINS_REG_W, REG_DWORD,
            //               &Configuration.UseAsAltSLGInOtherPlugins, sizeof(DWORD));
            //      GetValue(actKey, CONFIG_ALTLANGFORPLUGINS_REG, REG_SZ,
            //               Configuration.AltPluginSLGName, MAX_PATH);
            GetStringValueW(actKey, CONFIG_CONVERSIONTABLE_REG,
                            Configuration.ConversionTable);
            GetValueW(actKey, CONFIG_SKILLLEVEL_REG_W, REG_DWORD,
                     &Configuration.SkillLevel, sizeof(DWORD));
            GetValueW(actKey, CONFIG_TITLEBARSHOWPATH_REG_W, REG_DWORD,
                     &Configuration.TitleBarShowPath, sizeof(DWORD));
            GetValueW(actKey, CONFIG_TITLEBARMODE_REG_W, REG_DWORD,
                     &Configuration.TitleBarMode, sizeof(DWORD));
            GetValueW(actKey, CONFIG_THEME_MODE_REG_W, REG_DWORD,
                     &Configuration.ThemeMode, sizeof(DWORD));
            if (Configuration.ThemeMode < THEME_MODE_LIGHT || Configuration.ThemeMode > THEME_MODE_SYSTEM)
                Configuration.ThemeMode = THEME_MODE_LIGHT;
            GetValueW(actKey, CONFIG_TITLEBARPREFIX_REG_W, REG_DWORD,
                     &Configuration.UseTitleBarPrefix, sizeof(DWORD));
            GetStringValueW(actKey, CONFIG_TITLEBARPREFIXTEXT_REG, Configuration.TitleBarPrefix);
            GetValueW(actKey, CONFIG_MAINWINDOWICONINDEX_REG_W, REG_DWORD,
                     &Configuration.MainWindowIconIndex, sizeof(DWORD));
            if (Configuration.MainWindowIconIndex < 0 || Configuration.MainWindowIconIndex > MAINWINDOWICONS_COUNT)
                Configuration.MainWindowIconIndex = 0;
            GetValueW(actKey, CONFIG_CLICKQUICKRENAME_REG_W, REG_DWORD,
                     &Configuration.ClickQuickRename, sizeof(DWORD));
            GetValueW(actKey, CONFIG_VISIBLEDRIVES_REG_W, REG_DWORD,
                     &Configuration.VisibleDrives, sizeof(DWORD));
            GetValueW(actKey, CONFIG_SEPARATEDDRIVES_REG_W, REG_DWORD,
                     &Configuration.SeparatedDrives, sizeof(DWORD));
            GetValueW(actKey, CONFIG_COMPAREBYTIME_REG_W, REG_DWORD,
                     &Configuration.CompareByTime, sizeof(DWORD));
            if (!GetValueW(actKey, CONFIG_COMPAREBYSIZE_REG_W, REG_DWORD,
                          &Configuration.CompareBySize, sizeof(DWORD)))
            { // conversion from older configuration - BySize used to be part of ByTime, so copy that setting
                Configuration.CompareBySize = Configuration.CompareByTime;
            }
            GetValueW(actKey, CONFIG_COMPAREBYCONTENT_REG_W, REG_DWORD,
                     &Configuration.CompareByContent, sizeof(DWORD));
            GetValueW(actKey, CONFIG_COMPAREBYATTR_REG_W, REG_DWORD,
                     &Configuration.CompareByAttr, sizeof(DWORD));
            GetValueW(actKey, CONFIG_COMPAREBYSUBDIRS_REG_W, REG_DWORD,
                     &Configuration.CompareSubdirs, sizeof(DWORD));
            GetValueW(actKey, CONFIG_COMPAREBYSUBDIRSATTR_REG_W, REG_DWORD,
                     &Configuration.CompareSubdirsAttr, sizeof(DWORD));

            GetValueW(actKey, CONFIG_COMPAREONEPANELDIRS_REG_W, REG_DWORD,
                     &Configuration.CompareOnePanelDirs, sizeof(DWORD));
            GetValueW(actKey, CONFIG_COMPAREMOREOPTIONS_REG_W, REG_DWORD,
                     &Configuration.CompareMoreOptions, sizeof(DWORD));
            GetValueW(actKey, CONFIG_COMPAREIGNOREFILES_REG_W, REG_DWORD,
                     &Configuration.CompareIgnoreFiles, sizeof(DWORD));
            GetValueW(actKey, CONFIG_COMPAREIGNOREDIRS_REG_W, REG_DWORD,
                     &Configuration.CompareIgnoreDirs, sizeof(DWORD));
            // Same as the recycle masks above: a missing value must leave the default alone.
            std::wstring compareIgnoreFiles;
            if (GetStringValueW(actKey, CONFIG_CONFIGTIGNOREFILESMASKS_REG, compareIgnoreFiles))
                Configuration.CompareIgnoreFilesMasks.SetMasksString(compareIgnoreFiles.c_str());
            std::wstring compareIgnoreDirs;
            if (GetStringValueW(actKey, CONFIG_CONFIGTIGNOREDIRSMASKS_REG, compareIgnoreDirs))
                Configuration.CompareIgnoreDirsMasks.SetMasksString(compareIgnoreDirs.c_str());
            int errPos;
            Configuration.CompareIgnoreFilesMasks.PrepareMasks(errPos);
            Configuration.CompareIgnoreDirsMasks.PrepareMasks(errPos);

            GetValueW(actKey, CONFIG_THUMBNAILSIZE_REG_W, REG_DWORD,
                     &Configuration.ThumbnailSize, sizeof(DWORD));
            LeftPanel->SetThumbnailSize(Configuration.ThumbnailSize);
            RightPanel->SetThumbnailSize(Configuration.ThumbnailSize);

            GetValueW(actKey, CONFIG_KEEPPLUGINSSORTED_REG_W, REG_DWORD,
                     &Configuration.KeepPluginsSorted, sizeof(DWORD));

            Configuration.ShowSLGIncomplete = TRUE;
            if (Configuration.ConfigVersion == THIS_CONFIG_VERSION)
            {
                GetValueW(actKey, CONFIG_SHOWSLGINCOMPLETE_REG_W, REG_DWORD,
                         &Configuration.ShowSLGIncomplete, sizeof(DWORD));
            }

            GetValueW(actKey, CONFIG_EDITNEWFILE_USEDEFAULT_REG_W, REG_DWORD,
                     &Configuration.UseEditNewFileDefault, sizeof(DWORD));
            GetStringValueW(actKey, CONFIG_EDITNEWFILE_DEFAULT_REG,
                            Configuration.EditNewFileDefault);

#ifndef _WIN64 // FIXME_X64_WINSCP
            if (!GetValue(actKey, L"Add x86-Only Plugins", REG_DWORD,
                          &Configuration.AddX86OnlyPlugins, sizeof(DWORD)))
            {
                Configuration.AddX86OnlyPlugins = TRUE;
            }
#endif // _WIN64

            HKEY actSubKey;
            if (OpenKey(actKey, SALAMANDER_CONFIRMATION_REG, actSubKey))
            {
                GetValue(actSubKey, CONFIG_CNFRM_FILEDIRDEL, REG_DWORD,
                         &Configuration.CnfrmFileDirDel, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_NEDIRDEL, REG_DWORD,
                         &Configuration.CnfrmNEDirDel, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_FILEOVER, REG_DWORD,
                         &Configuration.CnfrmFileOver, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_DIROVER, REG_DWORD,
                         &Configuration.CnfrmDirOver, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_SHFILEDEL, REG_DWORD,
                         &Configuration.CnfrmSHFileDel, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_SHDIRDEL, REG_DWORD,
                         &Configuration.CnfrmSHDirDel, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_SHFILEOVER, REG_DWORD,
                         &Configuration.CnfrmSHFileOver, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_NTFSPRESS, REG_DWORD,
                         &Configuration.CnfrmNTFSPress, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_NTFSCRYPT, REG_DWORD,
                         &Configuration.CnfrmNTFSCrypt, sizeof(DWORD));
                if (Configuration.ConfigVersion != 1)
                    GetValue(actSubKey, CONFIG_CNFRM_DAD, REG_DWORD,
                             &Configuration.CnfrmDragDrop, sizeof(DWORD));
                else // for old configs we read it one level up
                    GetValue(actKey, L"Confirm Drop Operations", REG_DWORD,
                             &Configuration.CnfrmDragDrop, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_CLOSEARCHIVE, REG_DWORD,
                         &Configuration.CnfrmCloseArchive, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_CLOSEFIND, REG_DWORD,
                         &Configuration.CnfrmCloseFind, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_STOPFIND, REG_DWORD,
                         &Configuration.CnfrmStopFind, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_CREATETARGETPATH, REG_DWORD,
                         &Configuration.CnfrmCreatePath, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_ALWAYSONTOP, REG_DWORD,
                         &Configuration.CnfrmAlwaysOnTop, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_ONSALCLOSE, REG_DWORD,
                         &Configuration.CnfrmOnSalClose, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_SENDEMAIL, REG_DWORD,
                         &Configuration.CnfrmSendEmail, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_ADDTOARCHIVE, REG_DWORD,
                         &Configuration.CnfrmAddToArchive, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_CREATEDIR, REG_DWORD,
                         &Configuration.CnfrmCreateDir, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_CHANGEDIRTC, REG_DWORD,
                         &Configuration.CnfrmChangeDirTC, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_SHOWNAMETOCOMP, REG_DWORD,
                         &Configuration.CnfrmShowNamesToCompare, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_DSTSHIFTSIGNORED, REG_DWORD,
                         &Configuration.CnfrmDSTShiftsIgnored, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_DSTSHIFTSOCCURED, REG_DWORD,
                         &Configuration.CnfrmDSTShiftsOccured, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_CNFRM_COPYMOVEOPTIONSNS, REG_DWORD,
                         &Configuration.CnfrmCopyMoveOptionsNS, sizeof(DWORD));

                CloseKey(actSubKey);
            }

            if (OpenKey(actKey, SALAMANDER_DRVSPEC_REG, actSubKey))
            {
                GetValue(actSubKey, CONFIG_DRVSPEC_FLOPPY_MON, REG_DWORD,
                         &Configuration.DrvSpecFloppyMon, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_DRVSPEC_FLOPPY_SIMPLE, REG_DWORD,
                         &Configuration.DrvSpecFloppySimple, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_DRVSPEC_REMOVABLE_MON, REG_DWORD,
                         &Configuration.DrvSpecRemovableMon, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_DRVSPEC_REMOVABLE_SIMPLE, REG_DWORD,
                         &Configuration.DrvSpecRemovableSimple, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_DRVSPEC_FIXED_MON, REG_DWORD,
                         &Configuration.DrvSpecFixedMon, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_DRVSPEC_FIXED_SIMPLE, REG_DWORD,
                         &Configuration.DrvSpecFixedSimple, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_DRVSPEC_REMOTE_MON, REG_DWORD,
                         &Configuration.DrvSpecRemoteMon, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_DRVSPEC_REMOTE_SIMPLE, REG_DWORD,
                         &Configuration.DrvSpecRemoteSimple, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_DRVSPEC_REMOTE_ACT, REG_DWORD,
                         &Configuration.DrvSpecRemoteDoNotRefreshOnAct, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_DRVSPEC_CDROM_MON, REG_DWORD,
                         &Configuration.DrvSpecCDROMMon, sizeof(DWORD));
                GetValue(actSubKey, CONFIG_DRVSPEC_CDROM_SIMPLE, REG_DWORD,
                         &Configuration.DrvSpecCDROMSimple, sizeof(DWORD));

                // for old versions we force icon reading on removable drives because we introduced the floppy category
                if (Configuration.ConfigVersion < 31)
                    Configuration.DrvSpecRemovableSimple = FALSE;

                CloseKey(actSubKey);
            }

            if (Configuration.ConfigVersion >= 8) // force the new toolbar for old versions
                GetStringValueW(actKey, CONFIG_TOPTOOLBAR_REG, Configuration.TopToolBar);
            GetStringValueW(actKey, CONFIG_MIDDLETOOLBAR_REG, Configuration.MiddleToolBar);
            GetStringValueW(actKey, CONFIG_LEFTTOOLBAR_REG, Configuration.LeftToolBar);
            GetStringValueW(actKey, CONFIG_RIGHTTOOLBAR_REG, Configuration.RightToolBar);
            // there used to be only one change drive button - now we introduce two buttons
            // and merge all bitmaps into one

            if (Configuration.ConfigVersion <= 3 && !Configuration.RightToolBar.empty())
            {
                std::wstring tmp = Configuration.RightToolBar;
                Configuration.RightToolBar.clear();

                BOOL first = TRUE;
                wchar_t* context = NULL;
                wchar_t* p = wcstok_s(tmp.data(), L",", &context);
                while (p != NULL)
                {
                    int i = _wtoi(p);

                    // replace the old tbbeChangeDrive with tbbeChangeDriveR
                    //#define TBBE_CHANGE_DRIVE_R     51
                    if (i == 36)
                        i = 51;

                    if (!first)
                        Configuration.RightToolBar.push_back(L',');
                    Configuration.RightToolBar += std::to_wstring(i);
                    first = FALSE;
                    p = wcstok_s(NULL, L",", &context);
                }
            }

            if (TopToolBar != NULL)
                TopToolBar->Load(Configuration.TopToolBar.c_str());
            if (MiddleToolBar != NULL)
                MiddleToolBar->Load(Configuration.MiddleToolBar.c_str());
            if (LeftPanel->DirectoryLine->ToolBar != NULL)
                LeftPanel->DirectoryLine->ToolBar->Load(Configuration.LeftToolBar.c_str());
            if (RightPanel->DirectoryLine->ToolBar != NULL)
                RightPanel->DirectoryLine->ToolBar->Load(Configuration.RightToolBar.c_str());

            GetValueW(actKey, CONFIG_TOPTOOLBARVISIBLE_REG_W, REG_DWORD,
                     &Configuration.TopToolBarVisible, sizeof(DWORD));
            GetValueW(actKey, CONFIG_PLGTOOLBARVISIBLE_REG_W, REG_DWORD,
                     &Configuration.PluginsBarVisible, sizeof(DWORD));
            GetValueW(actKey, CONFIG_MIDDLETOOLBARVISIBLE_REG_W, REG_DWORD,
                     &Configuration.MiddleToolBarVisible, sizeof(DWORD));

            GetValueW(actKey, CONFIG_USERMENUTOOLBARVISIBLE_REG_W, REG_DWORD,
                     &Configuration.UserMenuToolBarVisible, sizeof(DWORD));
            GetValueW(actKey, CONFIG_HOTPATHSBARVISIBLE_REG_W, REG_DWORD,
                     &Configuration.HotPathsBarVisible, sizeof(DWORD));

            // if this is an old version of configuration and the user menu contains items,
            // show the UserMenuBar
            if (Configuration.ConfigVersion <= 3 && UserMenuItems->Count > 0)
                Configuration.UserMenuToolBarVisible = TRUE;

            GetValueW(actKey, CONFIG_DRIVEBARVISIBLE_REG_W, REG_DWORD,
                     &Configuration.DriveBarVisible, sizeof(DWORD));
            GetValueW(actKey, CONFIG_DRIVEBAR2VISIBLE_REG_W, REG_DWORD,
                     &Configuration.DriveBar2Visible, sizeof(DWORD));

            if (ret) // if we return FALSE, everything will be inserted later
            {
                // bands must be inserted in the correct order according to their index
                BOOL menuInserted = FALSE; // the menu is important, insert it at all costs
                // disable saving positions while adding bands, otherwise their order would be overwritten
                int idx;
                for (idx = 0; idx < 10; idx++) // we can safely try more indices than there are bands
                {
                    if (idx == Configuration.MenuIndex)
                    {
                        InsertMenuBand();
                        menuInserted = TRUE;
                    }
                    if (idx == Configuration.TopToolbarIndex && Configuration.TopToolBarVisible)
                        ToggleTopToolBar(FALSE);
                    if (idx == Configuration.PluginsBarIndex && Configuration.PluginsBarVisible)
                        TogglePluginsBar(FALSE);
                    if (idx == Configuration.UserMenuToolbarIndex && Configuration.UserMenuToolBarVisible)
                        ToggleUserMenuToolBar(FALSE);
                    if (idx == Configuration.HotPathsBarIndex && Configuration.HotPathsBarVisible)
                        ToggleHotPathsBar(FALSE);
                    if (idx == Configuration.DriveBarIndex && Configuration.DriveBarVisible)
                        ToggleDriveBar(Configuration.DriveBar2Visible, FALSE);
                }
                if (!menuInserted)
                {
                    TRACE_E("Inserting MenuBar. Configuration seems to be corrupted.");
                    Configuration.MenuIndex = 0;
                    InsertMenuBand();
                }
                if (Configuration.MiddleToolBarVisible)
                    ToggleMiddleToolBar();
                CreateAndInsertWorkerBand(); // insert the worker band at the end
            }

            GetValueW(actKey, CONFIG_BOTTOMTOOLBARVISIBLE_REG_W, REG_DWORD,
                     &Configuration.BottomToolBarVisible, sizeof(DWORD));
            if (Configuration.BottomToolBarVisible)
                ToggleBottomToolBar();

            //      GetValue(actKey, CONFIG_SPACESELCALCSPACE, REG_DWORD,
            //               &Configuration.SpaceSelCalcSpace, sizeof(DWORD));
            GetValueW(actKey, CONFIG_USETIMERESOLUTION_W, REG_DWORD,
                     &Configuration.UseTimeResolution, sizeof(DWORD));
            GetValueW(actKey, CONFIG_TIMERESOLUTION_W, REG_DWORD,
                     &Configuration.TimeResolution, sizeof(DWORD));
            GetValueW(actKey, CONFIG_IGNOREDSTSHIFTS_W, REG_DWORD,
                     &Configuration.IgnoreDSTShifts, sizeof(DWORD));
            GetValueW(actKey, CONFIG_USEDRAGDROPMINTIME_W, REG_DWORD,
                     &Configuration.UseDragDropMinTime, sizeof(DWORD));
            GetValueW(actKey, CONFIG_DRAGDROPMINTIME_W, REG_DWORD,
                     &Configuration.DragDropMinTime, sizeof(DWORD));

            GetValueW(actKey, CONFIG_LASTFOCUSEDPAGE_W, REG_DWORD,
                     &Configuration.LastFocusedPage, sizeof(DWORD));
            GetValueW(actKey, CONFIG_CONFIGURATION_HEIGHT_W, REG_DWORD,
                     &Configuration.ConfigurationHeight, sizeof(DWORD));
            GetValueW(actKey, CONFIG_VIEWANDEDITEXPAND_W, REG_DWORD,
                     &Configuration.ViewersAndEditorsExpanded, sizeof(DWORD));
            GetValueW(actKey, CONFIG_PACKEPAND_W, REG_DWORD,
                     &Configuration.PackersAndUnpackersExpanded, sizeof(DWORD));

            GetValueW(actKey, CONFIG_CMDLINE_REG_W, REG_DWORD, &cmdLine, sizeof(DWORD));
            GetValueW(actKey, CONFIG_CMDLFOCUS_REG_W, REG_DWORD, &cmdLineFocus, sizeof(DWORD));

            GetValueW(actKey, CONFIG_USECUSTOMPANELFONT_REG_W, REG_DWORD, &UseCustomPanelFont, sizeof(DWORD));
            if (LoadLogFont(actKey, CONFIG_PANELFONT_REG, &LogFont) && UseCustomPanelFont)
            {
                // if the user uses a custom font, propagate it now
                SetFont();
            }

            LoadHistory(actKey, CONFIG_NAMEDHISTORYW_REG, FindNamedHistory, FIND_NAMED_HISTORY_SIZE);
            if (IsWideHistoryEmpty(FindNamedHistory, FIND_NAMED_HISTORY_SIZE))
                LoadLegacyHistory(actKey, CONFIG_NAMEDHISTORY_REG, FindNamedHistory, FIND_NAMED_HISTORY_SIZE);

            LoadHistory(actKey, CONFIG_LOOKINHISTORYW_REG, FindLookInHistory, FIND_LOOKIN_HISTORY_SIZE);
            if (IsWideHistoryEmpty(FindLookInHistory, FIND_LOOKIN_HISTORY_SIZE))
                LoadLegacyHistory(actKey, CONFIG_LOOKINHISTORY_REG, FindLookInHistory, FIND_LOOKIN_HISTORY_SIZE);

            LoadHistory(actKey, CONFIG_GREPHISTORYW_REG, FindGrepHistory, FIND_GREP_HISTORY_SIZE);
            if (IsWideHistoryEmpty(FindGrepHistory, FIND_GREP_HISTORY_SIZE))
                LoadLegacyHistory(actKey, CONFIG_GREPHISTORY_REG, FindGrepHistory, FIND_GREP_HISTORY_SIZE);

            LoadHistory(actKey, CONFIG_SELECTHISTORYW_REG, Configuration.SelectHistory, SELECT_HISTORY_SIZE);
            if (IsWideHistoryEmpty(Configuration.SelectHistory, SELECT_HISTORY_SIZE))
                LoadLegacyHistory(actKey, CONFIG_SELECTHISTORY_REG, Configuration.SelectHistory, SELECT_HISTORY_SIZE);
            //      Guys (Honza Patera, Tomas Jelinek) didn't like this because when they
            //      launch a new instance, they don't remember the previous mask. They hit (Un)Select
            //      and the last mask is still there. FAR, VC, NC start with *.* when launched,
            //      we will behave the same way.
            //      if (Configuration.SelectHistory[0] != NULL)  // load the initial state of +/- selection as well
            //        strcpy(SelectionMask, Configuration.SelectHistory[0]);
            // Unsuffixed registry values are read-only legacy ACP imports. Decode each directly
            // into its final wide owner only when no current UTF-16 history exists.
            LoadHistory(actKey, CONFIG_COPYHISTORYW_REG, Configuration.CopyHistory, COPY_HISTORY_SIZE);
            if (IsWideHistoryEmpty(Configuration.CopyHistory, COPY_HISTORY_SIZE))
                LoadLegacyHistory(actKey, CONFIG_COPYHISTORY_REG, Configuration.CopyHistory, COPY_HISTORY_SIZE);

            LoadHistory(actKey, CONFIG_CHANGEDIRHISTORYW_REG, Configuration.ChangeDirHistory, CHANGEDIR_HISTORY_SIZE);
            if (IsWideHistoryEmpty(Configuration.ChangeDirHistory, CHANGEDIR_HISTORY_SIZE))
                LoadLegacyHistory(actKey, CONFIG_CHANGEDIRHISTORY_REG, Configuration.ChangeDirHistory, CHANGEDIR_HISTORY_SIZE);

            LoadHistory(actKey, CONFIG_VIEWERHISTORYW_REG, ViewerHistory, VIEWER_HISTORY_SIZE);
            if (IsWideHistoryEmpty(ViewerHistory, VIEWER_HISTORY_SIZE))
                LoadLegacyHistory(actKey, CONFIG_VIEWERHISTORY_REG, ViewerHistory, VIEWER_HISTORY_SIZE);

            LoadHistory(actKey, CONFIG_COMMANDHISTORYW_REG, Configuration.EditHistory, EDIT_HISTORY_SIZE);
            if (IsWideHistoryEmpty(Configuration.EditHistory, EDIT_HISTORY_SIZE))
                LoadLegacyHistory(actKey, CONFIG_COMMANDHISTORY_REG, Configuration.EditHistory, EDIT_HISTORY_SIZE);

            LoadHistory(actKey, CONFIG_FILELISTHISTORYW_REG, Configuration.FileListHistory, FILELIST_HISTORY_SIZE);
            if (IsWideHistoryEmpty(Configuration.FileListHistory, FILELIST_HISTORY_SIZE))
                LoadLegacyHistory(actKey, CONFIG_FILELISTHISTORY_REG, Configuration.FileListHistory, FILELIST_HISTORY_SIZE);
            // dialogs_highlight_registry.cpp's ClearHistory default runs before this load; re-apply
            // it only if the registry genuinely had nothing at all.
            if (IsWideHistoryEmpty(Configuration.FileListHistory, FILELIST_HISTORY_SIZE))
                Configuration.FileListHistory[0] = DupStr(L"$(FileName)$(CRLF)");

            LoadHistory(actKey, CONFIG_CREATEDIRHISTORYW_REG, Configuration.CreateDirHistory, CREATEDIR_HISTORY_SIZE);
            if (IsWideHistoryEmpty(Configuration.CreateDirHistory, CREATEDIR_HISTORY_SIZE))
                LoadLegacyHistory(actKey, CONFIG_CREATEDIRHISTORY_REG, Configuration.CreateDirHistory, CREATEDIR_HISTORY_SIZE);

            LoadHistory(actKey, CONFIG_QUICKRENAMEHISTORYW_REG, Configuration.QuickRenameHistory, QUICKRENAME_HISTORY_SIZE);
            if (IsWideHistoryEmpty(Configuration.QuickRenameHistory, QUICKRENAME_HISTORY_SIZE))
                LoadLegacyHistory(actKey, CONFIG_QUICKRENAMEHISTORY_REG, Configuration.QuickRenameHistory, QUICKRENAME_HISTORY_SIZE);

            LoadHistory(actKey, CONFIG_EDITNEWHISTORYW_REG, Configuration.EditNewHistory, EDITNEW_HISTORY_SIZE);
            if (IsWideHistoryEmpty(Configuration.EditNewHistory, EDITNEW_HISTORY_SIZE))
                LoadLegacyHistory(actKey, CONFIG_EDITNEWHISTORY_REG, Configuration.EditNewHistory, EDITNEW_HISTORY_SIZE);

            LoadHistory(actKey, CONFIG_CONVERTHISTORYW_REG, Configuration.ConvertHistory, CONVERT_HISTORY_SIZE);
            if (IsWideHistoryEmpty(Configuration.ConvertHistory, CONVERT_HISTORY_SIZE))
                LoadLegacyHistory(actKey, CONFIG_CONVERTHISTORY_REG, Configuration.ConvertHistory, CONVERT_HISTORY_SIZE);

            LoadHistory(actKey, CONFIG_FILTERHISTORYW_REG, Configuration.FilterHistory, FILTER_HISTORY_SIZE);
            if (IsWideHistoryEmpty(Configuration.FilterHistory, FILTER_HISTORY_SIZE))
                LoadLegacyHistory(actKey, CONFIG_FILTERHISTORY_REG, Configuration.FilterHistory, FILTER_HISTORY_SIZE);
            if (DirHistory != NULL)
            {
                DirHistory->LoadFromRegistry(actKey, CONFIG_WORKDIRSHISTORY_REG);
                if (LeftPanel != NULL)
                    LeftPanel->DirectoryLine->SetHistory(DirHistory->HasPaths());
                if (RightPanel != NULL)
                    RightPanel->DirectoryLine->SetHistory(DirHistory->HasPaths());
            }

            if (OpenKey(actKey, CONFIG_COPYMOVEOPTIONS_REG, actSubKey))
            {
                CopyMoveOptions.Load(actSubKey);
                CloseKey(actSubKey);
            }

            if (OpenKey(actKey, CONFIG_FINDOPTIONS_REG, actSubKey))
            {
                FindOptions.Load(actSubKey, Configuration.ConfigVersion);
                CloseKey(actSubKey);
            }

            if (OpenKey(actKey, CONFIG_FINDIGNORE_REG, actSubKey))
            {
                FindIgnore.Load(actSubKey, Configuration.ConfigVersion);
                CloseKey(actSubKey);
            }

            GetStringValueW(actKey, CONFIG_FILELISTNAME_REG, Configuration.FileListName);
            GetValueW(actKey, CONFIG_FILELISTAPPEND_REG_W, REG_DWORD, &Configuration.FileListAppend, sizeof(DWORD));
            GetValueW(actKey, CONFIG_FILELISTDESTINATION_REG_W, REG_DWORD, &Configuration.FileListDestination, sizeof(DWORD));

            CloseKey(actKey);
        }

        //---  viewer

        if (OpenKey(salamander, SALAMANDER_VIEWER_REG, actKey))
        {
            GetValueW(actKey, VIEWER_FINDFORWARD_REG_W, REG_DWORD,
                     &GlobalFindDialog.Forward, sizeof(DWORD));
            GetValueW(actKey, VIEWER_FINDWHOLEWORDS_REG_W, REG_DWORD,
                     &GlobalFindDialog.WholeWords, sizeof(DWORD));
            GetValueW(actKey, VIEWER_FINDCASESENSITIVE_REG_W, REG_DWORD,
                     &GlobalFindDialog.CaseSensitive, sizeof(DWORD));
            GetValueW(actKey, VIEWER_FINDREGEXP_REG_W, REG_DWORD,
                     &GlobalFindDialog.Regular, sizeof(DWORD));
            GetStringValueW(actKey, VIEWER_FINDTEXT_REG, GlobalFindDialog.Text);
            GetValueW(actKey, VIEWER_FINDHEXMODE_REG_W, REG_DWORD,
                     &GlobalFindDialog.HexMode, sizeof(DWORD));

            GetValueW(actKey, VIEWER_CONFIGCRLF_REG_W, REG_DWORD,
                     &Configuration.EOL_CRLF, sizeof(DWORD));
            GetValueW(actKey, VIEWER_CONFIGCR_REG_W, REG_DWORD,
                     &Configuration.EOL_CR, sizeof(DWORD));
            GetValueW(actKey, VIEWER_CONFIGLF_REG_W, REG_DWORD,
                     &Configuration.EOL_LF, sizeof(DWORD));
            GetValueW(actKey, VIEWER_CONFIGNULL_REG_W, REG_DWORD,
                     &Configuration.EOL_NULL, sizeof(DWORD));
            GetValueW(actKey, VIEWER_CONFIGTABSIZE_REG_W, REG_DWORD,
                     &Configuration.TabSize, sizeof(DWORD));
            GetValueW(actKey, VIEWER_CONFIGDEFMODE_REG_W, REG_DWORD,
                     &Configuration.DefViewMode, sizeof(DWORD));
            // Same as the recycle masks above. The version-17 upgrade then tests the
            // EFFECTIVE mask group rather than the temporary, because pre-unicode compared
            // the live buffer - which, for a fresh profile with no stored value, holds the
            // very default the upgrade is meant to extend.
            std::wstring textMasks;
            if (GetStringValueW(actKey, VIEWER_CONFIGTEXTMASK_REG, textMasks))
                Configuration.TextModeMasks.SetMasksString(textMasks.c_str());
            if (Configuration.ConfigVersion < 17 &&
                wcscmp(Configuration.TextModeMasks.GetMasksString(), L"*.txt;*.602") == 0)
                Configuration.TextModeMasks.SetMasksString(L"*.txt;*.602;*.xml");
            int errPos;
            Configuration.TextModeMasks.PrepareMasks(errPos);
            std::wstring hexMasks;
            if (GetStringValueW(actKey, VIEWER_CONFIGHEXMASK_REG, hexMasks))
                Configuration.HexModeMasks.SetMasksString(hexMasks.c_str());
            Configuration.HexModeMasks.PrepareMasks(errPos);

            GetValueW(actKey, VIEWER_CONFIGUSECUSTOMFONT_REG_W, REG_DWORD,
                     &UseCustomViewerFont, sizeof(DWORD));
            LoadLogFont(actKey, VIEWER_CONFIGFONT_REG, &ViewerLogFont); // no viewer can be open yet, so no need to call SetViewerFont()
            GetValueW(actKey, VIEWER_WRAPTEXT_REG_W, REG_DWORD,
                     &Configuration.WrapText, sizeof(DWORD));
            GetValueW(actKey, VIEWER_CPAUTOSELECT_REG_W, REG_DWORD,
                     &Configuration.CodePageAutoSelect, sizeof(DWORD));
            GetStringValueW(actKey, VIEWER_DEFAULTCONVERT_REG, Configuration.DefaultConvert);
            GetValueW(actKey, VIEWER_AUTOCOPYSELECTION_REG_W, REG_DWORD,
                     &Configuration.AutoCopySelection, sizeof(DWORD));
            GetValueW(actKey, VIEWER_GOTOOFFSETISHEX_REG_W, REG_DWORD,
                     &Configuration.GoToOffsetIsHex, sizeof(DWORD));

            GetValueW(actKey, VIEWER_CONFIGSAVEWINPOS_REG_W, REG_DWORD,
                     &Configuration.SavePosition, sizeof(DWORD));
            BOOL plcmntExist = TRUE;
            plcmntExist &= GetValueW(actKey, VIEWER_CONFIGWNDLEFT_REG_W, REG_DWORD,
                                    &Configuration.WindowPlacement.rcNormalPosition.left, sizeof(DWORD));
            plcmntExist &= GetValueW(actKey, VIEWER_CONFIGWNDRIGHT_REG_W, REG_DWORD,
                                    &Configuration.WindowPlacement.rcNormalPosition.right, sizeof(DWORD));
            plcmntExist &= GetValueW(actKey, VIEWER_CONFIGWNDTOP_REG_W, REG_DWORD,
                                    &Configuration.WindowPlacement.rcNormalPosition.top, sizeof(DWORD));
            plcmntExist &= GetValueW(actKey, VIEWER_CONFIGWNDBOTTOM_REG_W, REG_DWORD,
                                    &Configuration.WindowPlacement.rcNormalPosition.bottom, sizeof(DWORD));
            plcmntExist &= GetValueW(actKey, VIEWER_CONFIGWNDSHOW_REG_W, REG_DWORD,
                                    &Configuration.WindowPlacement.showCmd, sizeof(DWORD));
            if (plcmntExist)
                Configuration.WindowPlacement.length = sizeof(Configuration.WindowPlacement);

            CloseKey(actKey);
        }

        //---  left and right panel

        std::wstring leftPanelPath;
        if (gEnvironment == NULL || !gEnvironment->GetSystemDirectory(leftPanelPath).success || leftPanelPath.empty())
            leftPanelPath = L"C:\\";
        std::wstring rightPanelPath = leftPanelPath;
        const wchar_t systemDrive = (wchar_t)towlower(leftPanelPath[0]);
        std::wstring sysDefDir;
        if (systemDrive >= L'a' && systemDrive <= L'z')
            sysDefDir = DefaultDir[systemDrive - L'a'];
        LoadPanelConfig(leftPanelPath, LeftPanel, salamander, SALAMANDER_LEFTP_REG_W);
        LoadPanelConfig(rightPanelPath, RightPanel, salamander, SALAMANDER_RIGHTP_REG_W);

        CloseKey(salamander);
        salamander = NULL;

        LoadSaveToRegistryMutex.Leave();

        //---  END OF LOADING CONFIGURATION

        if (cmdLine && !SystemPolicies.GetNoRun())
            PostMessage(HWindow, WM_COMMAND, CM_TOGGLEEDITLINE, TRUE);

        MSG msg; // process all pending messages
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // set the active panel according to command line parameters
        if (ret && cmdLineParams != NULL)
        {
            if (cmdLineParams->activatePanel == 1 && rightPanelFocused ||
                cmdLineParams->activatePanel == 2 && !rightPanelFocused)
            {
                rightPanelFocused = !rightPanelFocused;
            }
        }

        FocusPanel(rightPanelFocused ? RightPanel : LeftPanel);
        (rightPanelFocused ? RightPanel : LeftPanel)->SetCaretIndex(0, FALSE);
        if (cmdLineFocus)
            SendMessage(HWindow, WM_COMMAND, CM_EDITLINE, 0);

        // this caused trouble:
        // when a panel pointed to an unavailable UNC path,
        // it would wait here for several seconds
        //    LeftPanel->UpdateDriveIcon(TRUE);
        //    RightPanel->UpdateDriveIcon(TRUE);
        //    RefreshMenuAndTB(TRUE);

        HMENU h = GetSystemMenu(HWindow, FALSE);
        if (h != NULL)
        {
            CheckMenuItem(h, CM_ALWAYSONTOP, MF_BYCOMMAND | (Configuration.AlwaysOnTop ? MF_CHECKED : MF_UNCHECKED));

            MENUITEMINFOW mii;
            ZeroMemory(&mii, sizeof(mii));
            mii.cbSize = sizeof(mii);
            mii.fMask = MIIM_STRING;

            std::wstring text;
            if (ReadMenuItemTextW(h, SC_MINIMIZE, text, FALSE))
            {
                text += FormatStrW(L"\t%ls+%ls", LoadStrW(IDS_SHIFT), LoadStrW(IDS_ESCAPE));
                mii.dwTypeData = text.data();
                SetMenuItemInfoW(h, SC_MINIMIZE, FALSE, &mii);
            }

            if (ReadMenuItemTextW(h, SC_MAXIMIZE, text, FALSE))
            {
                text += FormatStrW(L"\t%ls+%ls+%ls", LoadStrW(IDS_CTRL), LoadStrW(IDS_SHIFT), LoadStrW(IDS_F11));
                mii.dwTypeData = text.data();
                SetMenuItemInfoW(h, SC_MAXIMIZE, FALSE, &mii);
            }
        }

        SplashScreenCloseIfExist();
        if (Configuration.StatusArea)
            AddTrayIcon();

        SetWindowIcon();
        SetWindowTitle();

        SetWindowPos(HWindow,
                     Configuration.AlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

        // show the window in full
        if (useWinPlacement)
        {
            // from MSDN:
            // ShowCmd: 0 = SW_SHOWNORMAL
            //          3 = SW_SHOWMAXIMIZED
            //          7 = SW_SHOWMINNOACTIVE

            // we don't want the application minimized on startup unless the user
            // defined it in a shortcut
            if (!Configuration.StatusArea)
            {
                switch (CmdShow)
                {
                case SW_SHOWNORMAL:
                {
                    // if the configuration specifies a minimized window, open it restored
                    if (place.showCmd == SW_MINIMIZE)
                        place.showCmd = SW_RESTORE;
                    if (place.showCmd == SW_SHOWMINIMIZED)
                        place.showCmd = SW_SHOWNORMAL;
                    break;
                }

                // settings in the shortcut take priority over the configuration
                case SW_SHOWMINNOACTIVE:
                case SW_SHOWMAXIMIZED:
                {
                    place.showCmd = CmdShow;
                    break;
                }
                }
            }
            else
            {
                switch (CmdShow)
                {
                case SW_SHOWNORMAL:
                {
                    // if the configuration specifies a minimized window, open it restored
                    if (place.showCmd == SW_MINIMIZE)
                        place.showCmd = SW_RESTORE;
                    if (place.showCmd == SW_SHOWMINIMIZED)
                        place.showCmd = SW_SHOWNORMAL;
                    break;
                }

                // settings in the shortcut take priority over the configuration
                case SW_SHOWMINNOACTIVE:
                {
                    place.showCmd = SW_HIDE;
                    PostMessage(HWindow, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                    break;
                }

                // settings in the shortcut take priority over the configuration
                case SW_SHOWMAXIMIZED:
                {
                    place.showCmd = CmdShow;
                    PostMessage(HWindow, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
                    break;
                }
                }
            }
            // #97: clamp the restored normal rect to a visible, sane size on a real monitor.
            place.rcNormalPosition = SanitizeMainWindowNormalRect(place.rcNormalPosition);
            // #95: SetWindowPlacement() makes the window visible - finalize the theme first so the
            // very first painted frame is already dark.
            ApplyStartupThemeToMainWindow(HWindow);
            themeFinalized = TRUE;
            SetWindowPlacement(HWindow, &place);
        }
        LeftPanel->SetupListBoxScrollBars();
        RightPanel->SetupListBoxScrollBars();

        UpdateWindow(HWindow);

        // set panel paths according to command line parameters (all path types, including archives and FS)
        BOOL leftPanelPathSet = FALSE;
        BOOL rightPanelPathSet = FALSE;
        if (ret && cmdLineParams != NULL)
        {
            if (cmdLineParams->leftPath.empty() && cmdLineParams->rightPath.empty() && !cmdLineParams->activePath.empty())
            {
                if (GetActivePanel()->ChangeDirLite(cmdLineParams->activePath.c_str())) // no point in combining this with left/right panel settings
                {
                    if (rightPanelFocused)
                        rightPanelPathSet = TRUE;
                    else
                    {
                        leftPanelPathSet = TRUE;
                        LeftPanel->RefreshVisibleItemsArray(); // see "RefreshVisibleItemsArray" comment below
                    }
                }
            }
            else
            {
                if (!cmdLineParams->leftPath.empty())
                {
                    if (LeftPanel->ChangeDirLite(cmdLineParams->leftPath.c_str()))
                    {
                        leftPanelPathSet = TRUE;
                        LeftPanel->RefreshVisibleItemsArray(); // see "RefreshVisibleItemsArray" comment below
                    }
                }
                if (!cmdLineParams->rightPath.empty())
                {
                    if (RightPanel->ChangeDirLite(cmdLineParams->rightPath.c_str()))
                        rightPanelPathSet = TRUE;
                }
            }
        }

        // save the array of visible items; normally this is done in idle time, but if it
        // should be ready so that icon reading for user menu entries has priority over icons
        // outside the visible part of the panel, we must handle it manually (icon loading
        // is already running, but sooner is better than later, this minimal delay should not hurt)
        if (rightPanelPathSet)
            RightPanel->RefreshVisibleItemsArray();

        // leftPanelPath and rightPanelPath are only disk paths; we don't store archives or FS paths.
        // Apply via the wide variants so Unicode-only roots survive validation and reopening.
        DWORD err, lastErr;
        BOOL pathInvalid, cut;
        BOOL tryNet = TRUE;
        if (!leftPanelPathSet)
        {
            if (SalCheckAndRestorePathWithCutW(LeftPanel->HWindow, leftPanelPath, tryNet,
                                               err, lastErr, pathInvalid, cut, TRUE))
            {
                LeftPanel->ChangePathToDisk(LeftPanel->HWindow, leftPanelPath.c_str());
            }
            else
                LeftPanel->ChangeToRescuePathOrFixedDrive(LeftPanel->HWindow);
            LeftPanel->RefreshVisibleItemsArray(); // see comment "RefreshVisibleItemsArray" above
        }
        UpdateWindow(LeftPanel->HWindow); // ensures dir/info line is drawn immediately after the panel content

        tryNet = TRUE;
        if (!rightPanelPathSet)
        {
            if (SalCheckAndRestorePathWithCutW(RightPanel->HWindow, rightPanelPath, tryNet,
                                               err, lastErr, pathInvalid, cut, TRUE))
            {
                RightPanel->ChangePathToDisk(RightPanel->HWindow, rightPanelPath.c_str());
            }
            else
                RightPanel->ChangeToRescuePathOrFixedDrive(RightPanel->HWindow);
            RightPanel->RefreshVisibleItemsArray(); // see comment "RefreshVisibleItemsArray" above
        }
        UpdateWindow(RightPanel->HWindow); // ensures dir/info line is drawn immediately after the panel content

        // restore default-dir on the system drive (damaged - system root was in both panels)
        if (!sysDefDir.empty() && systemDrive >= L'a' && systemDrive <= L'z')
            DefaultDir[systemDrive - L'a'] = sysDefDir;
        // restore DefaultDir
        MainWindow->UpdateDefaultDir(TRUE);

        // #95: normally already done above, before SetWindowPlacement() could show the
        // window. This covers the path where no persisted window placement existed.
        if (!themeFinalized)
            ApplyStartupThemeToMainWindow(HWindow);
        if (EditWindow != NULL && EditWindow->HWindow != NULL)
        {
            EditWindowSetDirectory();
            RedrawWindow(EditWindow->HWindow, NULL, NULL,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
        InvalidateRect(HWindow, NULL, TRUE);

        return ret;
    }

    LoadSaveToRegistryMutex.Leave();

    return FALSE;
}
