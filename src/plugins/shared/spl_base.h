// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

//****************************************************************************
//
// Copyright (c) 2023 Open Salamander Authors
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#pragma once

#ifdef _MSC_VER
#pragma pack(push, enter_include_spl_base) // so that structures are independent of the configured alignment
#pragma pack(4)
#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression
#endif                    // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

// in debug version we will test whether source and destination memory overlap (for memcpy they must not overlap)
#if defined(_DEBUG) && defined(TRACE_ENABLE)
#define memcpy _sal_safe_memcpy
#ifdef __cplusplus
extern "C"
{
#endif
    void* _sal_safe_memcpy(void* dest, const void* src, size_t count);
#ifdef __cplusplus
}
#endif
#endif // defined(_DEBUG) && defined(TRACE_ENABLE)

// the following functions do not crash when working with invalid memory (nor when working with NULL):
// lstrcpy, lstrcpyn, lstrlen and lstrcat (these are defined with suffix A or W, therefore
// we do not redefine them directly), for easier debugging we need them to crash,
// because otherwise the error is discovered later in a place where it may not be clear what
// caused it
#define lstrcpyA _sal_lstrcpyA
#define lstrcpyW _sal_lstrcpyW
#define lstrcpynA _sal_lstrcpynA
#define lstrcpynW _sal_lstrcpynW
#define lstrlenA _sal_lstrlenA
#define lstrlenW _sal_lstrlenW
#define lstrcatA _sal_lstrcatA
#define lstrcatW _sal_lstrcatW
#ifdef __cplusplus
extern "C"
{
#endif
    LPSTR _sal_lstrcpyA(LPSTR lpString1, LPCSTR lpString2);
    LPWSTR _sal_lstrcpyW(LPWSTR lpString1, LPCWSTR lpString2);
    LPSTR _sal_lstrcpynA(LPSTR lpString1, LPCSTR lpString2, int iMaxLength);
    LPWSTR _sal_lstrcpynW(LPWSTR lpString1, LPCWSTR lpString2, int iMaxLength);
    int _sal_lstrlenA(LPCSTR lpString);
    int _sal_lstrlenW(LPCWSTR lpString);
    LPSTR _sal_lstrcatA(LPSTR lpString1, LPCSTR lpString2);
    LPWSTR _sal_lstrcatW(LPWSTR lpString1, LPCWSTR lpString2);
#ifdef __cplusplus
}
#endif

// original SDK that was part of VC6 had the value defined as 0x00000040 (year 1998, when the attribute was not yet used, it came with W2K)
#if (FILE_ATTRIBUTE_ENCRYPTED != 0x00004000)
#pragma message(__FILE__ " ERROR: FILE_ATTRIBUTE_ENCRYPTED != 0x00004000. You have to install latest version of Microsoft SDK. This value has changed!")
#endif

class CSalamanderGeneralAbstract;
class CPluginDataInterfaceAbstract;
class CPluginInterfaceForArchiverAbstract;
class CPluginInterfaceForViewerAbstract;
class CPluginInterfaceForMenuExtAbstract;
class CPluginInterfaceForFSAbstract;
class CPluginInterfaceForThumbLoaderAbstract;
class CSalamanderGUIAbstract;
class CSalamanderSafeFileAbstract;
class CGUIIconListAbstract;

//
// ****************************************************************************
// CSalamanderDebugAbstract
//
// set of methods from Salamander used for finding bugs in both debug and release versions

// macro CALLSTK_MEASURETIMES - enables measurement of time spent preparing call-stack reports (measures ratio against
//                              total function execution time)
//                              WARNING: must also be enabled for each plugin separately
// macro CALLSTK_DISABLEMEASURETIMES - suppresses measurement of time spent preparing call-stack reports in DEBUG/SDK/PB version

#if (defined(_DEBUG) || defined(CALLSTK_MEASURETIMES)) && !defined(CALLSTK_DISABLEMEASURETIMES)
struct CCallStackMsgContext
{
    DWORD PushesCounterStart;                      // start state of counter of Pushes called in this thread
    LARGE_INTEGER PushPerfTimeCounterStart;        // start state of counter of time spent in Push methods called in this thread
    LARGE_INTEGER IgnoredPushPerfTimeCounterStart; // start state of counter of time spent in non-measured (ignored) Push methods called in this thread
    LARGE_INTEGER StartTime;                       // "time" of Push for this call-stack macro
    DWORD_PTR PushCallerAddress;                   // address of CALL_STACK_MESSAGE macro (address of Push)
};
#else  // (defined(_DEBUG) || defined(CALLSTK_MEASURETIMES)) && !defined(CALLSTK_DISABLEMEASURETIMES)
struct CCallStackMsgContext;
#endif // (defined(_DEBUG) || defined(CALLSTK_MEASURETIMES)) && !defined(CALLSTK_DISABLEMEASURETIMES)

class CSalamanderDebugAbstract
{
public:
    // outputs 'file'+'line'+'str' TRACE_I to TRACE SERVER - only in DEBUG/SDK/PB version of Salamander
    virtual void WINAPI TraceI(const char* file, int line, const char* str) = 0;
    virtual void WINAPI TraceIW(const WCHAR* file, int line, const WCHAR* str) = 0;

    // outputs 'file'+'line'+'str' TRACE_E to TRACE SERVER - only in DEBUG/SDK/PB version of Salamander
    virtual void WINAPI TraceE(const char* file, int line, const char* str) = 0;
    virtual void WINAPI TraceEW(const WCHAR* file, int line, const WCHAR* str) = 0;

    // registers a new thread with TRACE (assigns Unique ID), 'thread'+'tid' are returned by
    // _beginthreadex and CreateThread, optional (UID is then -1)
    virtual void WINAPI TraceAttachThread(HANDLE thread, unsigned tid) = 0;

    // sets the name of the active thread for TRACE, optional (thread is marked as "unknown")
    // WARNING: requires thread registration with TRACE (see TraceAttachThread), otherwise does nothing
    virtual void WINAPI TraceSetThreadName(const char* name) = 0;
    virtual void WINAPI TraceSetThreadNameW(const WCHAR* name) = 0;

    // introduces things needed for CALL-STACK methods into the thread (see Push and Pop below),
    // in all called plugin methods it is possible to use CALL_STACK methods directly,
    // this method is used only for new plugin threads,
    // runs function 'threadBody' with parameter 'param', returns result of function 'threadBody'
    virtual unsigned WINAPI CallWithCallStack(unsigned(WINAPI* threadBody)(void*), void* param) = 0;

    // stores a message on CALL-STACK ('format'+'args' see vsprintf), on application crash
    // the CALL-STACK contents are displayed in the Bug Report window reporting the crash
    virtual void WINAPI Push(const char* format, va_list args, CCallStackMsgContext* callStackMsgContext,
                             BOOL doNotMeasureTimes) = 0;

    // removes the last message from CALL-STACK, call must be paired with Push
    virtual void WINAPI Pop(CCallStackMsgContext* callStackMsgContext) = 0;

    // sets the name of the active thread for VC debugger
    virtual void WINAPI SetThreadNameInVC(const char* name) = 0;

    // calls TraceSetThreadName and SetThreadNameInVC for 'name' (description see these two methods)
    virtual void WINAPI SetThreadNameInVCAndTrace(const char* name) = 0;

    // If we are not already connected to Trace Server, tries to establish connection (server
    // must be running). SDK version of Salamander only (including Preview Builds): if server
    // autostart is enabled and server is not running (e.g. user terminated it), tries to
    // start it before connecting.
    virtual void WINAPI TraceConnectToServer() = 0;

    // called for modules that may report memory leaks, if memory leaks are detected,
    // a load "as image" (without module init) of all such registered modules occurs (during
    // memory leak check these modules are already unloaded), and only then memory leaks are
    // displayed = .cpp module names are visible instead of "#File Error#"
    // can be called from any thread
    virtual void WINAPI AddModuleWithPossibleMemoryLeaks(const char* fileName) = 0;
};

//
// ****************************************************************************
// CSalamanderRegistryAbstract
//
// set of Salamander methods for working with system registry,
// used in CPluginInterfaceAbstract::LoadConfiguration
// and CPluginInterfaceAbstract::SaveConfiguration

class CSalamanderRegistryAbstract
{
public:
    // clears key 'key' of all subkeys and values, returns success
    virtual BOOL WINAPI ClearKey(HKEY key) = 0;

    // creates or opens existing subkey 'name' of key 'key', returns 'createdKey' and success;
    // obtained key ('createdKey') must be closed by calling CloseKey
    virtual BOOL WINAPI CreateKey(HKEY key, const char* name, HKEY& createdKey) = 0;

    // opens existing subkey 'name' of key 'key', returns 'openedKey' and success
    // obtained key ('openedKey') must be closed by calling CloseKey
    virtual BOOL WINAPI OpenKey(HKEY key, const char* name, HKEY& openedKey) = 0;

    // closes key opened via OpenKey or CreateKey
    virtual void WINAPI CloseKey(HKEY key) = 0;

    // deletes subkey 'name' of key 'key', returns success
    virtual BOOL WINAPI DeleteKey(HKEY key, const char* name) = 0;

    // loads value 'name'+'type'+'buffer'+'bufferSize' from key 'key', returns success
    virtual BOOL WINAPI GetValue(HKEY key, const char* name, DWORD type, void* buffer, DWORD bufferSize) = 0;

    // saves value 'name'+'type'+'data'+'dataSize' to key 'key', for strings it is possible
    // to specify 'dataSize' == -1 -> string length calculation using strlen function,
    // returns success
    virtual BOOL WINAPI SetValue(HKEY key, const char* name, DWORD type, const void* data, DWORD dataSize) = 0;

    // deletes value 'name' of key 'key', returns success
    virtual BOOL WINAPI DeleteValue(HKEY key, const char* name) = 0;

    // retrieves into 'bufferSize' the required size for value 'name'+'type' from key 'key', returns success
    virtual BOOL WINAPI GetSize(HKEY key, const char* name, DWORD type, DWORD& bufferSize) = 0;
};

//
// ****************************************************************************
// CSalamanderConnectAbstract
//
// set of Salamander methods for connecting plugin to Salamander
// (custom pack/unpack + panel archiver view/edit + file viewer + menu-items)

// constants for CSalamanderConnectAbstract::AddMenuItem
#define MENU_EVENT_TRUE 0x0001                    // always occurs
#define MENU_EVENT_DISK 0x0002                    // source is windows directory ("c:\path" or UNC)
#define MENU_EVENT_THIS_PLUGIN_ARCH 0x0004        // source is archive of this plugin
#define MENU_EVENT_THIS_PLUGIN_FS 0x0008          // source is file-system of this plugin
#define MENU_EVENT_FILE_FOCUSED 0x0010            // focus is on a file
#define MENU_EVENT_DIR_FOCUSED 0x0020             // focus is on a directory
#define MENU_EVENT_UPDIR_FOCUSED 0x0040           // focus is on ".."
#define MENU_EVENT_FILES_SELECTED 0x0080          // files are selected
#define MENU_EVENT_DIRS_SELECTED 0x0100           // directories are selected
#define MENU_EVENT_TARGET_DISK 0x0200             // target is windows directory ("c:\path" or UNC)
#define MENU_EVENT_TARGET_THIS_PLUGIN_ARCH 0x0400 // target is archive of this plugin
#define MENU_EVENT_TARGET_THIS_PLUGIN_FS 0x0800   // target is file-system of this plugin
#define MENU_EVENT_SUBDIR 0x1000                  // directory is not root (contains "..")
// focus is on a file for which this plugin provides "panel archiver view" or "panel archiver edit"
#define MENU_EVENT_ARCHIVE_FOCUSED 0x2000
// only 0x4000 is still available (both masks are combined into DWORD and masked with 0x7FFF beforehand)

// determines for which user the item is intended
#define MENU_SKILLLEVEL_BEGINNER 0x0001     // intended for most important menu items, for beginners
#define MENU_SKILLLEVEL_INTERMEDIATE 0x0002 // also set for less frequently used commands; for intermediate users
#define MENU_SKILLLEVEL_ADVANCED 0x0004     // set for all commands (professionals should have everything in menu)
#define MENU_SKILLLEVEL_ALL 0x0007          // helper constant combining all previous ones

// macro for preparing 'HotKey' for AddMenuItem()
// LOWORD - hot key (virtual key + modifiers) (LOBYTE - virtual key, HIBYTE - modifiers)
// mods: combination of HOTKEYF_CONTROL, HOTKEYF_SHIFT, HOTKEYF_ALT
// examples: SALHOTKEY('A', HOTKEYF_CONTROL | HOTKEYF_SHIFT), SALHOTKEY(VK_F1, HOTKEYF_CONTROL | HOTKEYF_ALT | HOTKEYF_EXT)
//#define SALHOTKEY(vk,mods,cst) ((DWORD)(((BYTE)(vk)|((WORD)((BYTE)(mods))<<8))|(((DWORD)(BYTE)(cst))<<16)))
#define SALHOTKEY(vk, mods) ((DWORD)(((BYTE)(vk) | ((WORD)((BYTE)(mods)) << 8))))

// macro for preparing 'hotKey' for AddMenuItem()
// tells Salamander that the menu item will contain a hot key (separated by '\t' character)
// Salamander will not complain via TRACE_E in this case and will display the hot key in Plugins menu
// WARNING: this is not a hot key that Salamander would deliver to the plugin, it is really just a label
// if user assigns a custom hot key to this command in Plugin Manager, the hint will be suppressed
#define SALHOTKEY_HINT ((DWORD)0x00020000)

class CSalamanderConnectAbstract
{
public:
    // adds plugin to list for "custom archiver pack",
    // 'title' is the name of custom packer for the user, 'defaultExtension' is the default extension
    // for new archives, if not upgrading "custom pack" (or adding the entire plugin) and
    // 'update' is FALSE, the call is ignored; if 'update' is TRUE, settings are overwritten with
    // new values 'title' and 'defaultExtension' - prevention against repeated 'update'==TRUE
    // (constant overwriting of settings) is necessary
    virtual void WINAPI AddCustomPacker(const char* title, const char* defaultExtension, BOOL update) = 0;

    // adds plugin to list for "custom archiver unpack",
    // 'title' is the name of custom unpacker for the user, 'masks' are archive file masks (used
    // to find what to unpack the archive with, separator is ';' (escape sequence for ';' is
    // ";;") and classic wildcards '*' and '?' plus '#' for '0'..'9' are used), if not upgrading
    // "custom unpack" (or adding the entire plugin) and 'update' is FALSE the call is ignored;
    // if 'update' is TRUE, settings are overwritten with new values 'title' and 'masks' - prevention
    // against repeated 'update'==TRUE (constant overwriting of settings) is necessary
    virtual void WINAPI AddCustomUnpacker(const char* title, const char* masks, BOOL update) = 0;

    // adds plugin to list for "panel archiver view/edit",
    // 'extensions' are archive extensions to be processed by this plugin
    // (separator is ';' (here ';' has no escape sequence) and wildcard '#' for
    // '0'..'9' is used), if 'edit' is TRUE, this plugin handles "panel archiver view/edit", otherwise only
    // "panel archiver view", if not upgrading "panel archiver view/edit" (or adding the
    // entire plugin) and 'updateExts' is FALSE the call is ignored; if 'updateExts' is TRUE,
    // it adds new archive extensions (ensures presence of all extensions from 'extensions') - prevention
    // against repeated 'updateExts'==TRUE (constant revival of extensions from 'extensions') is necessary
    virtual void WINAPI AddPanelArchiver(const char* extensions, BOOL edit, BOOL updateExts) = 0;

    // removes extension from list for "panel archiver view/edit" (only from items related to
    // this plugin), 'extension' is the archive extension (single; wildcard '#' for '0'..'9' is used),
    // prevention against repeated calls (constant deletion of 'extension') is necessary
    virtual void WINAPI ForceRemovePanelArchiver(const char* extension) = 0;

    // adds plugin to list for "file viewer",
    // 'masks' are viewer extensions to be processed by this plugin
    // (separator is ';' (escape sequence for ';' is ";;") and wildcards '*' and '?' are used,
    // avoid using spaces if possible + character '|' is forbidden (inverse masks are not allowed)),
    // if not upgrading "file viewer" (or adding the entire plugin) and 'force' is FALSE,
    // the call is ignored; if 'force' is TRUE, 'masks' are always added (if not already on the
    // list) - prevention against repeated 'force'==TRUE (constant adding of 'masks') is necessary
    virtual void WINAPI AddViewer(const char* masks, BOOL force) = 0;

    // removes mask from list for "file viewer" (only from items related to this plugin),
    // 'mask' is the viewer extension (single; wildcards '*' and '?' are used), prevention
    // against repeated calls (constant deletion of 'mask') is necessary
    virtual void WINAPI ForceRemoveViewer(const char* mask) = 0;

    // adds items to menu Plugins/"plugin name" in Salamander, 'iconIndex' is the index
    // of item icon (-1=no icon; bitmap with icons specification see
    // CSalamanderConnectAbstract::SetBitmapWithIcons; ignored for separator), 'name' is
    // item name (max. MAX_PATH - 1 characters) or NULL if separator (parameters
    // 'state_or'+'state_and' have no meaning in this case); 'hotKey' is the hot key
    // of the item obtained using SALHOTKEY macro; 'name' can contain hot key hint,
    // separated by '\t' character, in variable 'hotKey' the constant SALHOTKEY_HINT must
    // be assigned in this case, see comment for SALHOTKEY_HINT; 'id' is unique identification
    // number of the item within the plugin (for separator it has meaning only if 'callGetState' is TRUE),
    // if 'callGetState' is TRUE, method CPluginInterfaceForMenuExtAbstract::GetMenuItemState
    // is called to determine item state (for separator only MENU_ITEM_STATE_HIDDEN state has
    // meaning, others are ignored), otherwise 'state_or'+'state_and' are used to calculate
    // item state (enabled/disabled) - when calculating item state, first a mask ('eventMask')
    // is assembled by logically summing all events that occurred (events see MENU_EVENT_XXX),
    // item will be "enabled" if the following expression is TRUE:
    //   ('eventMask' & 'state_or') != 0 && ('eventMask' & 'state_and') == 'state_and',
    // parameter 'skillLevel' determines for which user levels the item (or separator)
    // will be displayed; value contains one or more (ORed) MENU_SKILLLEVEL_XXX constants;
    // menu items are updated on each plugin load (possible change of items according to configuration)
    // WARNING: for "dynamic menu extension" use CSalamanderBuildMenuAbstract::AddMenuItem
    virtual void WINAPI AddMenuItem(int iconIndex, const char* name, DWORD hotKey, int id, BOOL callGetState,
                                    DWORD state_or, DWORD state_and, DWORD skillLevel) = 0;

    // adds submenu to menu Plugins/"plugin name" in Salamander, 'iconIndex'
    // is the index of submenu icon (-1=no icon; bitmap with icons specification
    // see CSalamanderConnectAbstract::SetBitmapWithIcons), 'name' is the name of
    // submenu (max. MAX_PATH - 1 characters), 'id' is unique identification number of menu
    // item within the plugin (for submenu it has meaning only if 'callGetState' is TRUE),
    // if 'callGetState' is TRUE, method CPluginInterfaceForMenuExtAbstract::GetMenuItemState
    // is called to determine submenu state (only MENU_ITEM_STATE_ENABLED and MENU_ITEM_STATE_HIDDEN
    // states have meaning, others are ignored), otherwise 'state_or'+'state_and' are used
    // to calculate item state (enabled/disabled) - state calculation see
    // CSalamanderConnectAbstract::AddMenuItem(), parameter 'skillLevel' determines for which
    // user levels the submenu will be displayed, value contains one or more (ORed)
    // MENU_SKILLLEVEL_XXX constants, submenu is terminated by calling
    // CSalamanderConnectAbstract::AddSubmenuEnd();
    // menu items are updated on each plugin load (possible change of items according to configuration)
    // WARNING: for "dynamic menu extension" use CSalamanderBuildMenuAbstract::AddSubmenuStart
    virtual void WINAPI AddSubmenuStart(int iconIndex, const char* name, int id, BOOL callGetState,
                                        DWORD state_or, DWORD state_and, DWORD skillLevel) = 0;

    // terminates submenu in menu Plugins/"plugin name" in Salamander, next items will be
    // added to higher (parent) menu level;
    // menu items are updated on each plugin load (possible change of items according to configuration)
    // WARNING: for "dynamic menu extension" use CSalamanderBuildMenuAbstract::AddSubmenuEnd
    virtual void WINAPI AddSubmenuEnd() = 0;

    // sets item for FS in Change Drive menu and in Drive bars; 'title' is its text,
    // 'iconIndex' is the index of its icon (-1=no icon; bitmap with icons specification see
    // CSalamanderConnectAbstract::SetBitmapWithIcons), 'title' can contain up to three columns
    // separated by '\t' (see Alt+F1/F2 menu); item visibility can be set
    // from Plugins Manager or directly from plugin using method
    // CSalamanderGeneralAbstract::SetChangeDriveMenuItemVisibility
    virtual void WINAPI SetChangeDriveMenuItem(const char* title, int iconIndex) = 0;

    // informs Salamander that plugin can load thumbnails from files matching
    // group mask 'masks' (separator is ';' (escape sequence for ';' is ";;") and
    // wildcards '*' and '?' are used); to load thumbnail
    // CPluginInterfaceForThumbLoaderAbstract::LoadThumbnail is called
    virtual void WINAPI SetThumbnailLoader(const char* masks) = 0;

    // sets bitmap with plugin icons; Salamander copies bitmap contents to internal
    // structures, plugin is responsible for bitmap destruction (from Salamander side
    // bitmap is used only during this function); icon count is derived from
    // bitmap width, icons are always 16x16 pixels; transparent part of icons is magenta
    // color (RGB(255,0,255)), bitmap color depth can be 4 or 8 bits (16 or 256
    // colors), ideally have both color variants prepared and choose from them according
    // to result of method CSalamanderGeneralAbstract::CanUse256ColorsBitmap()
    // WARNING: this method is obsolete, does not support alpha transparency, use
    //          SetIconListForGUI() instead
    virtual void WINAPI SetBitmapWithIcons(HBITMAP bitmap) = 0;

    // sets index of plugin icon used for plugin in Plugins/Plugins Manager window,
    // in Help/About Plugin menu and possibly also for plugin submenu in Plugins menu (details
    // see CSalamanderConnectAbstract::SetPluginMenuAndToolbarIcon()); if plugin does not call
    // this method, standard Salamander icon for plugin is used; 'iconIndex'
    // is the index of icon being set (bitmap with icons specification see
    // CSalamanderConnectAbstract::SetBitmapWithIcons)
    virtual void WINAPI SetPluginIcon(int iconIndex) = 0;

    // sets index of icon for plugin submenu, used for plugin submenu
    // in Plugins menu and possibly also in top toolbar for drop-down button serving
    // to display plugin submenu; if plugin does not call this method,
    // plugin icon is used for plugin submenu in Plugins menu (setting see
    // CSalamanderConnectAbstract::SetPluginIcon) and button for plugin will not appear
    // in top toolbar; 'iconIndex' is the index of icon being set (-1=plugin icon should
    // be used, see CSalamanderConnectAbstract::SetPluginIcon(); bitmap with icons
    // specification see CSalamanderConnectAbstract::SetBitmapWithIcons);
    virtual void WINAPI SetPluginMenuAndToolbarIcon(int iconIndex) = 0;

    // sets bitmap with plugin icons; bitmap must be allocated using
    // CSalamanderGUIAbstract::CreateIconList() call and then created and filled using
    // CGUIIconListAbstract interface methods; icon dimensions must be 16x16 pixels;
    // Salamander takes over the bitmap object into its management, plugin must not
    // destroy it after calling this function; bitmap is saved to Salamander configuration
    // so icons can be used on next launch without loading the plugin, therefore only
    // insert necessary icons into it
    virtual void WINAPI SetIconListForGUI(CGUIIconListAbstract* iconList) = 0;
};

//
// ****************************************************************************
// CDynamicString
//
// dynamic string: reallocates as needed

class CDynamicString
{
public:
    // returns TRUE if string 'str' of length 'len' was successfully added; if 'len' is -1,
    // 'len' is determined as "strlen(str)" (adding without null terminator); if 'len' is -2,
    // 'len' is determined as "strlen(str)+1" (adding including null terminator)
    virtual BOOL WINAPI Add(const char* str, int len = -1) = 0;
};

//
// ****************************************************************************
// CPluginInterfaceAbstract
//
// set of plugin methods that Salamander needs for working with the plugin
//
// For better clarity, parts are separated for:
// archivers - see CPluginInterfaceForArchiverAbstract,
// viewers - see CPluginInterfaceForViewerAbstract,
// menu extension - see CPluginInterfaceForMenuExtAbstract,
// file-systems - see CPluginInterfaceForFSAbstract,
// thumbnail loaders - see CPluginInterfaceForThumbLoaderAbstract.
// Parts are connected to CPluginInterfaceAbstract via CPluginInterfaceAbstract::GetInterfaceForXXX

// flags indicating which functions the plugin provides (which methods of
// CPluginInterfaceAbstract descendant are actually implemented in the plugin):
#define FUNCTION_PANELARCHIVERVIEW 0x0001     // methods for "panel archiver view"
#define FUNCTION_PANELARCHIVEREDIT 0x0002     // methods for "panel archiver edit"
#define FUNCTION_CUSTOMARCHIVERPACK 0x0004    // methods for "custom archiver pack"
#define FUNCTION_CUSTOMARCHIVERUNPACK 0x0008  // methods for "custom archiver unpack"
#define FUNCTION_CONFIGURATION 0x0010         // Configuration method
#define FUNCTION_LOADSAVECONFIGURATION 0x0020 // methods for "load/save configuration"
#define FUNCTION_VIEWER 0x0040                // methods for "file viewer"
#define FUNCTION_FILESYSTEM 0x0080            // methods for "file system"
#define FUNCTION_DYNAMICMENUEXT 0x0100        // methods for "dynamic menu extension"

// codes of various events (and meaning of 'param' parameter), received by CPluginInterfaceAbstract::Event() method:
// color change occurred (due to system color change / WM_SYSCOLORCHANGE or due to configuration change); plugin can
// retrieve new versions of Salamander colors via CSalamanderGeneralAbstract::GetCurrentColor;
// if plugin has file-system with icons of type pitFromPlugin, it should recolor the background of image-list
// with simple icons to SALCOL_ITEM_BK_NORMAL color; 'param' is ignored here
#define PLUGINEVENT_COLORSCHANGED 0

// Salamander configuration change occurred; plugin can retrieve new versions of Salamander
// configuration parameters via CSalamanderGeneralAbstract::GetConfigParameter;
// 'param' is ignored here
#define PLUGINEVENT_CONFIGURATIONCHANGED 1

// left and right panels were swapped (Swap Panels - Ctrl+U)
// 'param' is ignored here
#define PLUGINEVENT_PANELSSWAPPED 2

// active panel change occurred (switching between panels)
// 'param' is PANEL_LEFT or PANEL_RIGHT - indicates the activated panel
#define PLUGINEVENT_PANELACTIVATED 3

// Salamander received WM_SETTINGCHANGE and based on it regenerated fonts for toolbars.
// Then it sends this event to all plugins so they have the opportunity to call SetFont()
// method on their toolbars;
// 'param' is ignored here
#define PLUGINEVENT_SETTINGCHANGE 4

// event codes in Password Manager, received by CPluginInterfaceAbstract::PasswordManagerEvent() method:
#define PME_MASTERPASSWORDCREATED 1 // user created master password (passwords need to be encrypted)
#define PME_MASTERPASSWORDCHANGED 2 // user changed master password (passwords need to be decrypted and then re-encrypted)
#define PME_MASTERPASSWORDREMOVED 3 // user removed master password (passwords need to be decrypted)

class CPluginInterfaceAbstract
{
#ifdef INSIDE_SALAMANDER
private: // protection against incorrect direct method calls (see CPluginInterfaceEncapsulation)
    friend class CPluginInterfaceEncapsulation;
#else  // INSIDE_SALAMANDER
public:
#endif // INSIDE_SALAMANDER

    // called as reaction to About button in Plugins window or command from Help/About Plugins menu
    virtual void WINAPI About(HWND parent) = 0;

    // called before plugin unload (naturally only if SalamanderPluginEntry returned
    // this object and not NULL), returns TRUE if unload can proceed,
    // 'parent' is parent of messageboxes, 'force' is TRUE if return value is not considered,
    // if it returns TRUE, this object and all others obtained from it will no longer be used
    // and plugin unload will occur; if critical shutdown is in progress (see
    // CSalamanderGeneralAbstract::IsCriticalShutdown), there is no point asking user anything
    // (do not open any windows anymore)
    // WARNING!!! All plugin threads must be terminated (if Release returns TRUE, FreeLibrary
    // is called on plugin .SPL => plugin code is unmapped from memory => threads then have
    // nothing to execute => usually neither bug-report nor Windows exception info appears)
    virtual BOOL WINAPI Release(HWND parent, BOOL force) = 0;

    // function for loading default configuration and for "load/save configuration" (load from plugin's
    // private key in registry), 'parent' is parent of messageboxes, if 'regKey' == NULL, it is
    // default configuration, 'registry' is object for working with registry, this method is always called
    // after SalamanderPluginEntry and before other calls (load from private key is called if
    // this function is provided by plugin and key exists in registry, otherwise only default
    // configuration load is called)
    virtual void WINAPI LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry) = 0;

    // function for "load/save configuration", called to save plugin configuration to its private
    // key in registry, 'parent' is parent of messageboxes, 'registry' is object for working with registry,
    // when Salamander saves configuration, it also calls this method (if provided by plugin); Salamander
    // also offers saving plugin configuration on its unload (e.g. manually from Plugins Manager),
    // in this case save is performed only if Salamander key exists in registry
    virtual void WINAPI SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry) = 0;

    // called as reaction to Configure button in Plugins window
    virtual void WINAPI Configuration(HWND parent) = 0;

    // called to connect plugin to Salamander, called after LoadConfiguration,
    // 'parent' is parent of messageboxes, 'salamander' is set of methods for connecting plugin

    /*  RULES FOR IMPLEMENTING THE CONNECT METHOD
        (plugins must have stored configuration version - see DEMOPLUGin,
         variable ConfigVersion and constant CURRENT_CONFIG_VERSION; below is
         an illustrative EXAMPLE of adding extension "dmp2" to DEMOPLUGin):

      -with each change, the configuration version number needs to be increased - CURRENT_CONFIG_VERSION
       (in the first version of Connect method CURRENT_CONFIG_VERSION=1)
      -in the basic part (before conditions "if (ConfigVersion < YYY)"):
        -code for plugin installation is written (very first plugin load):
         see CSalamanderConnectAbstract methods
        -during upgrades, extension lists for installation need to be updated for "custom archiver
         unpack" (AddCustomUnpacker), "panel archiver view/edit" (AddPanelArchiver),
         "file viewer" (AddViewer), menu items (AddMenuItem), etc.
        -for AddPanelArchiver and AddViewer calls, leave 'updateExts' and 'force' FALSE
         (otherwise we would force on user not only new, but also old extensions that they may have
         manually deleted)
        -for AddCustomPacker/AddCustomUnpacker calls, put condition "ConfigVersion < XXX"
         in 'update' parameter, where XXX is the number of the last version where
         extensions for custom packers/unpackers changed (both calls need to be evaluated separately;
         here for simplicity we force all extensions on user, if they deleted or added some,
         bad luck, they'll have to do it manually again)
        -AddMenuItem, SetChangeDriveMenuItem and SetThumbnailLoader work the same on each plugin
         load (installation/upgrades don't differ - always starting from scratch)
      -only during upgrades: in the upgrade part (after basic part):
        -add condition "if (ConfigVersion < XXX)", where XXX is the new value of
         CURRENT_CONFIG_VERSION constant + add comment for this version;
         in the body of this condition call:
          -if extensions for "panel archiver" were added, call
           "AddPanelArchiver(PPP, EEE, TRUE)", where PPP are only new extensions separated
           by semicolon and EEE is TRUE/FALSE ("panel view+edit"/"only panel view")
          -if extensions for "viewer" were added, call "AddViewer(PPP, TRUE)",
           where PPP are only new extensions separated by semicolon
          -if some old extensions for "viewer" should be deleted, call
           "ForceRemoveViewer(PPP)" for each such extension PPP
          -if some old extensions for "panel archiver" should be deleted, call
           "ForceRemovePanelArchiver(PPP)" for each such extension PPP

      VERIFICATION: after these modifications I recommend testing if it works correctly,
                just compile the plugin and try to load it into Salamander, automatic
                upgrade from previous version should occur (without need to
                remove and add the plugin):
                -see Options/Configuration menu:
                  -Viewers are on Viewers page: find added extensions,
                   verify that removed extensions no longer exist
                  -Panel Archivers are on Archives Associations in Panels page:
                   find added extensions
                  -Custom Unpackers are on Unpackers in Unpack Dialog Box page:
                   find your plugin and verify if the mask list is OK
                -check new appearance of plugin submenu (in Plugins menu)
                -check new appearance of Change Drive menu (Alt+F1/F2)
                -check in Plugins Manager (in Plugins menu) thumbnailer masks:
                 focus your plugin, then check "Thumbnails" editbox
              +finally you can also try to remove and add the plugin, if
               plugin "installation" works: verification see all previous points

      NOTE: when adding extensions for "panel archiver", it is also necessary to add
            to the extension list in 'extensions' parameter of SetBasicPluginData method

      EXAMPLE OF ADDING EXTENSION "dmp2" FOR VIEWER AND ARCHIVER:
        (lines starting with "-" were removed, lines starting with "+" added,
         symbol "=====" at line start marks interruption of continuous code section)
        Summary of changes:
          -configuration version increased from 2 to 3:
            -comment for version 3 added
            -CURRENT_CONFIG_VERSION increased to 3
          -extension "dmp2" added to 'extensions' parameter of SetBasicPluginData
           (because we're adding extension "dmp2" for "panel archiver")
          -mask "*.dmp2" added to AddCustomUnpacker + version increased from 1 to 3
           in condition (because we're adding extension "dmp2" for "custom unpacker")
          -extension "dmp2" added to AddPanelArchiver (because we're adding extension
           "dmp2" for "panel archiver")
          -mask "*.dmp2" added to AddViewer (because we're adding extension "dmp2"
           for "viewer")
          -condition for upgrade to version 3 added + comment for this upgrade,
           body of condition:
            -AddPanelArchiver call for extension "dmp2" with 'updateExts' TRUE
             (because we're adding extension "dmp2" for "panel archiver")
            -AddViewer call for mask "*.dmp2" with 'force' TRUE (because
             we're adding extension "dmp2" for "viewer")
=====
  // ConfigVersion: 0 - no configuration was loaded from Registry (plugin installation),
  //                1 - first configuration version
  //                2 - second configuration version (some values added to configuration)
+ //                3 - third configuration version (extension "dmp2" added)

  int ConfigVersion = 0;
- #define CURRENT_CONFIG_VERSION 2
+ #define CURRENT_CONFIG_VERSION 3
  const char *CONFIG_VERSION = "Version";
=====
  // set basic plugin information
  salamander->SetBasicPluginData("Salamander Demo Plugin",
                                 FUNCTION_PANELARCHIVERVIEW | FUNCTION_PANELARCHIVEREDIT |
                                 FUNCTION_CUSTOMARCHIVERPACK | FUNCTION_CUSTOMARCHIVERUNPACK |
                                 FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION |
                                 FUNCTION_VIEWER | FUNCTION_FILESYSTEM,
                                 "2.0",
                                 "Copyright © 1999-2023 Open Salamander Authors",
                                 "This plugin should help you to make your own plugins.",
-                                "DEMOPLUG", "dmp", "dfs");
+                                "DEMOPLUG", "dmp;dmp2", "dfs");
=====
  void WINAPI
  CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract *salamander)
  {
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    // basic part:
    salamander->AddCustomPacker("DEMOPLUG (Plugin)", "dmp", FALSE);
-   salamander->AddCustomUnpacker("DEMOPLUG (Plugin)", "*.dmp", ConfigVersion < 1);
+   salamander->AddCustomUnpacker("DEMOPLUG (Plugin)", "*.dmp;*.dmp2", ConfigVersion < 3);
-   salamander->AddPanelArchiver("dmp", TRUE, FALSE);
+   salamander->AddPanelArchiver("dmp;dmp2", TRUE, FALSE);
-   salamander->AddViewer("*.dmp", FALSE);
+   salamander->AddViewer("*.dmp;*.dmp2", FALSE);
===== (I omitted adding menu items, setting icons and thumbnailer masks)
    // part for upgrades:
+   if (ConfigVersion < 3)   // version 3: extension "dmp2" added
+   {
+     salamander->AddPanelArchiver("dmp2", TRUE, TRUE);
+     salamander->AddViewer("*.dmp2", TRUE);
+   }
  }
=====
    */
    virtual void WINAPI Connect(HWND parent, CSalamanderConnectAbstract* salamander) = 0;

    // releases interface 'pluginData' that Salamander obtained from plugin using call to
    // CPluginInterfaceForArchiverAbstract::ListArchive or
    // CPluginFSInterfaceAbstract::ListCurrentPath; before this call,
    // file and directory data (CFileData::PluginData) are released using
    // CPluginDataInterfaceAbstract methods
    virtual void WINAPI ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData) = 0;

    // returns archiver interface; plugin must return this interface if it has
    // at least one of the following functions (see SetBasicPluginData): FUNCTION_PANELARCHIVERVIEW,
    // FUNCTION_PANELARCHIVEREDIT, FUNCTION_CUSTOMARCHIVERPACK and/or FUNCTION_CUSTOMARCHIVERUNPACK;
    // if plugin does not contain archiver, returns NULL
    virtual CPluginInterfaceForArchiverAbstract* WINAPI GetInterfaceForArchiver() = 0;

    // returns viewer interface; plugin must return this interface if it has function
    // (see SetBasicPluginData) FUNCTION_VIEWER; if plugin does not contain viewer, returns NULL
    virtual CPluginInterfaceForViewerAbstract* WINAPI GetInterfaceForViewer() = 0;

    // returns menu extension interface; plugin must return this interface if it adds
    // items to menu (see CSalamanderConnectAbstract::AddMenuItem) or if it has
    // function (see SetBasicPluginData) FUNCTION_DYNAMICMENUEXT; otherwise returns NULL
    virtual CPluginInterfaceForMenuExtAbstract* WINAPI GetInterfaceForMenuExt() = 0;

    // returns file-system interface; plugin must return this interface if it has function
    // (see SetBasicPluginData) FUNCTION_FILESYSTEM; if plugin does not contain file-system, returns NULL
    virtual CPluginInterfaceForFSAbstract* WINAPI GetInterfaceForFS() = 0;

    // returns thumbnail loader interface; plugin must return this interface if it informed
    // Salamander that it can load thumbnails (see CSalamanderConnectAbstract::SetThumbnailLoader);
    // if plugin cannot load thumbnails, returns NULL
    virtual CPluginInterfaceForThumbLoaderAbstract* WINAPI GetInterfaceForThumbLoader() = 0;

    // receives various events, see event codes PLUGINEVENT_XXX; called only if plugin is
    // loaded; 'param' is event parameter
    // WARNING: can be called anytime after plugin entry-point (SalamanderPluginEntry) completes
    virtual void WINAPI Event(int event, DWORD param) = 0;

    // user wants all histories to be deleted (launched Clear History from configuration
    // on History page); history here means everything that is automatically created from
    // user-entered values (e.g. list of texts executed in command line, list of current paths
    // on individual drives, etc.); this does not include user-created lists - e.g. hot-paths,
    // user-menu, etc.; 'parent' is parent of potential messageboxes; after saving configuration,
    // history must not remain in registry; if plugin has open windows containing histories
    // (comboboxes), it must clear histories there as well
    virtual void WINAPI ClearHistory(HWND parent) = 0;

    // receives information about change on path 'path' (if 'includingSubdirs' is TRUE,
    // also includes change in subdirectories of 'path'); this method can be used e.g.
    // for invalidating/cleaning file/directory cache; NOTE: for plugin file-systems (FS)
    // there is method CPluginFSInterfaceAbstract::AcceptChangeOnPathNotification()
    virtual void WINAPI AcceptChangeOnPathNotification(const char* path, BOOL includingSubdirs) = 0;

    // this method is called only for plugins that use Password Manager (see
    // CSalamanderGeneralAbstract::SetPluginUsesPasswordManager()):
    // informs plugin about changes in Password Manager; 'parent' is parent of potential
    // messageboxes/dialogs; 'event' contains the event, see PME_XXX
    virtual void WINAPI PasswordManagerEvent(HWND parent, int event) = 0;
};

//
// ****************************************************************************
// CSalamanderPluginEntryAbstract
//
// set of methods from Salamander used in SalamanderPluginEntry

// flags informing about reason for plugin load (see method CSalamanderPluginEntryAbstract::GetLoadInformation)
#define LOADINFO_INSTALL 0x0001          // first plugin load (installation into Salamander)
#define LOADINFO_NEWSALAMANDERVER 0x0002 // new Salamander version (installation of all plugins from \
                                         // plugins subdirectory), loads all plugins (possible \
                                         // upgrade of all)
#define LOADINFO_NEWPLUGINSVER 0x0004    // change in plugins.ver file (plugin installation/upgrade), \
                                         // for simplicity loads all plugins (possible upgrade \
                                         // of all)
#define LOADINFO_LOADONSTART 0x0008      // load occurred because "load on start" flag was found

class CSalamanderPluginEntryAbstract
{
public:
    // returns Salamander version, see spl_vers.h, constants LAST_VERSION_OF_SALAMANDER and REQUIRE_LAST_VERSION_OF_SALAMANDER
    virtual int WINAPI GetVersion() = 0;

    // returns "parent" window of Salamander (parent for messageboxes)
    virtual HWND WINAPI GetParentWindow() = 0;

    // returns pointer to interface for Salamander debugging functions,
    // interface is valid for the entire lifetime of the plugin (not just within
    // "SalamanderPluginEntry" function) and is just a reference, so it is not released
    virtual CSalamanderDebugAbstract* WINAPI GetSalamanderDebug() = 0;

    // setting basic plugin data (data that Salamander remembers about the plugin along with
    // DLL file name), must be called, otherwise plugin cannot be connected;
    // 'pluginName' is plugin name; 'functions' contains ORed all functions that plugin
    // supports (see FUNCTION_XXX constants); 'version'+'copyright'+'description' are data for
    // user displayed in Plugins window; 'regKeyName' is proposed name of private key
    // for storing configuration in registry (ignored without FUNCTION_LOADSAVECONFIGURATION);
    // 'extensions' are basic extensions (e.g. just "ARJ"; "A01" etc. not included) of processed
    // archives separated by ';' (here ';' has no escape sequence) - Salamander uses these extensions
    // only when looking for replacement for removed panel archivers (occurs when plugin is removed;
    // solves problem "what will now handle extension XXX when original associated archiver
    // was removed as part of plugin PPP?") (ignored without FUNCTION_PANELARCHIVERVIEW and without
    // FUNCTION_PANELARCHIVEREDIT); 'fsName' is proposed name (obtaining assigned name is done using
    // CSalamanderGeneralAbstract::GetPluginFSName) of file system (ignored without FUNCTION_FILESYSTEM,
    // allowed characters are 'a-zA-Z0-9_+-', min. length 2 characters), if plugin needs
    // more file system names, it can use method CSalamanderPluginEntryAbstract::AddFSName;
    // returns TRUE on successful data acceptance
    virtual BOOL WINAPI SetBasicPluginData(const char* pluginName, DWORD functions,
                                           const char* version, const char* copyright,
                                           const char* description, const char* regKeyName = NULL,
                                           const char* extensions = NULL, const char* fsName = NULL) = 0;

    // returns pointer to interface for generally usable Salamander functions,
    // interface is valid for the entire lifetime of the plugin (not just within
    // "SalamanderPluginEntry" function) and is just a reference, so it is not released
    virtual CSalamanderGeneralAbstract* WINAPI GetSalamanderGeneral() = 0;

    // returns information related to plugin load; information is returned in DWORD value
    // as logical sum of LOADINFO_XXX flags (to test flag presence use
    // condition: (GetLoadInformation() & LOADINFO_XXX) != 0)
    virtual DWORD WINAPI GetLoadInformation() = 0;

    // loads module with language-dependent resources (SLG file); always tries to load module
    // of the same language in which Salamander is currently running, if such module is not found
    // (or version doesn't match), lets user select alternative module (if more than one
    // alternative exists + if user's selection from previous plugin load is not already stored);
    // if no module is found, returns NULL -> plugin should terminate;
    // 'parent' is parent of error messageboxes and dialog for selecting alternative
    // language module; 'pluginName' is plugin name (so user knows which plugin
    // is involved in error message or alternative language module selection)
    // WARNING: this method can only be called once; obtained language module handle
    //          is released automatically on plugin unload
    virtual HINSTANCE WINAPI LoadLanguageModule(HWND parent, const char* pluginName) = 0;

    // returns ID of current language selected for Salamander environment (e.g. english.slg =
    // English (US) = 0x409, czech.slg = Czech = 0x405)
    virtual WORD WINAPI GetCurrentSalamanderLanguageID() = 0;

    // returns pointer to interface providing modified Windows controls used
    // in Salamander, interface is valid for the entire lifetime of the plugin (not just
    // within "SalamanderPluginEntry" function) and is just a reference, so it is not released
    virtual CSalamanderGUIAbstract* WINAPI GetSalamanderGUI() = 0;

    // returns pointer to interface for convenient file operations,
    // interface is valid for the entire lifetime of the plugin (not just within
    // "SalamanderPluginEntry" function) and is just a reference, so it is not released
    virtual CSalamanderSafeFileAbstract* WINAPI GetSalamanderSafeFile() = 0;

    // sets URL to be displayed in Plugins Manager window as plugin home-page;
    // Salamander maintains the value until next plugin load (URL is displayed also for
    // unloaded plugins); on each plugin load the URL must be set again, otherwise
    // no URL is displayed (protection against holding invalid home-page URL)
    virtual void WINAPI SetPluginHomePageURL(const char* url) = 0;

    // adds another file system name; without FUNCTION_FILESYSTEM in 'functions' parameter
    // when calling SetBasicPluginData method, this method always returns only error;
    // 'fsName' is proposed name (obtaining assigned name is done using
    // CSalamanderGeneralAbstract::GetPluginFSName) of file system (allowed characters are
    // 'a-zA-Z0-9_+-', min. length 2 characters); in 'newFSNameIndex' (must not be NULL)
    // the index of newly added file system name is returned; returns TRUE on success;
    // returns FALSE on fatal error - in this case 'newFSNameIndex' is ignored
    // restriction: must not be called before SetBasicPluginData method
    virtual BOOL WINAPI AddFSName(const char* fsName, int* newFSNameIndex) = 0;
};

//
// ****************************************************************************
// FSalamanderPluginEntry
//
// Open Salamander 1.6 or Later Plugin Entry Point Function Type,
// plugin exports this function as "SalamanderPluginEntry" and Salamander calls it
// to connect the plugin at plugin load time
// returns plugin interface on successful connection, otherwise NULL,
// plugin interface is released by calling its Release method before plugin unload

typedef CPluginInterfaceAbstract*(WINAPI* FSalamanderPluginEntry)(CSalamanderPluginEntryAbstract* salamander);

//
// ****************************************************************************
// FSalamanderPluginGetReqVer
//
// Open Salamander 2.5 Beta 2 or Later Plugin Get Required Version of Salamander Function Type,
// plugin exports this function as "SalamanderPluginGetReqVer" and Salamander calls it
// as the first plugin function (before "SalamanderPluginGetSDKVer" and "SalamanderPluginEntry")
// at plugin load time;
// returns Salamander version for which the plugin is built (oldest version into which the plugin can be loaded)

typedef int(WINAPI* FSalamanderPluginGetReqVer)();

//
// ****************************************************************************
// FSalamanderPluginGetSDKVer
//
// Open Salamander 2.52 beta 2 (PB 22) or Later Plugin Get SDK Version Function Type,
// plugin optionally exports this function as "SalamanderPluginGetSDKVer" and Salamander
// tries to call it as the second plugin function (before "SalamanderPluginEntry")
// at plugin load time;
// returns SDK version used to build the plugin (informs Salamander which methods
// the plugin provides); exporting "SalamanderPluginGetSDKVer" makes sense only if
// "SalamanderPluginGetReqVer" returns number smaller than LAST_VERSION_OF_SALAMANDER; it is appropriate
// to return LAST_VERSION_OF_SALAMANDER directly

typedef int(WINAPI* FSalamanderPluginGetSDKVer)();

// ****************************************************************************
// SalIsWindowsVersionOrGreater
//
// Based on SDK 8.1 VersionHelpers.h
// Indicates if the current OS version matches, or is greater than, the provided
// version information. This function is useful in confirming a version of Windows
// Server that doesn't share a version number with a client release.
// http://msdn.microsoft.com/en-us/library/windows/desktop/dn424964%28v=vs.85%29.aspx
//

#ifdef __BORLANDC__
inline void* SecureZeroMemory(void* ptr, int cnt)
{
    char* vptr = (char*)ptr;
    while (cnt)
    {
        *vptr++ = 0;
        cnt--;
    }
    return ptr;
}
#endif // __BORLANDC__

inline BOOL SalIsWindowsVersionOrGreater(WORD wMajorVersion, WORD wMinorVersion, WORD wServicePackMajor)
{
    OSVERSIONINFOEXW osvi;
    DWORDLONG const dwlConditionMask = VerSetConditionMask(VerSetConditionMask(VerSetConditionMask(0,
                                                                                                   VER_MAJORVERSION, VER_GREATER_EQUAL),
                                                                               VER_MINORVERSION, VER_GREATER_EQUAL),
                                                           VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);

    SecureZeroMemory(&osvi, sizeof(osvi)); // replacement for memset (does not require RTL)
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    osvi.dwMajorVersion = wMajorVersion;
    osvi.dwMinorVersion = wMinorVersion;
    osvi.wServicePackMajor = wServicePackMajor;
    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR, dwlConditionMask) != FALSE;
}

// Find Windows version using bisection method and VerifyVersionInfo.
// Author:   M1xA, www.m1xa.com
// Licence:  MIT
// Version:  1.0
// https://bitbucket.org/AnyCPU/findversion/src/ebdec778fdbcdee67ac9a4d520239e134e047d8d/include/findversion.h?at=default
// Tested on: Windows 2000 .. Windows 8.1.
//
// WARNING: This function is ***SLOW_HACK***, use SalIsWindowsVersionOrGreater() instead (if you can).

#define M1xA_FV_EQUAL 0
#define M1xA_FV_LESS -1
#define M1xA_FV_GREAT 1
#define M1xA_FV_MIN_VALUE 0
#define M1xA_FV_MINOR_VERSION_MAX_VALUE 16
inline int M1xA_testValue(OSVERSIONINFOEX* value, DWORD verPart, DWORDLONG eq, DWORDLONG gt)
{
    if (VerifyVersionInfo(value, verPart, eq) == FALSE)
    {
        if (VerifyVersionInfo(value, verPart, gt) == TRUE)
            return M1xA_FV_GREAT;
        return M1xA_FV_LESS;
    }
    else
        return M1xA_FV_EQUAL;
}

#define M1xA_findPartTemplate(T) \
    inline BOOL M1xA_findPart##T(T* part, DWORD partType, OSVERSIONINFOEX* ret, T a, T b) \
    { \
        int funx = M1xA_FV_EQUAL; \
\
        DWORDLONG const eq = VerSetConditionMask(0, partType, VER_EQUAL); \
        DWORDLONG const gt = VerSetConditionMask(0, partType, VER_GREATER); \
\
        T* p = part; \
\
        *p = (T)((a + b) / 2); \
\
        while ((funx = M1xA_testValue(ret, partType, eq, gt)) != M1xA_FV_EQUAL) \
        { \
            switch (funx) \
            { \
            case M1xA_FV_GREAT: \
                a = *p; \
                break; \
            case M1xA_FV_LESS: \
                b = *p; \
                break; \
            } \
\
            *p = (T)((a + b) / 2); \
\
            if (*p == a) \
            { \
                if (M1xA_testValue(ret, partType, eq, gt) == M1xA_FV_EQUAL) \
                    return TRUE; \
\
                *p = b; \
\
                if (M1xA_testValue(ret, partType, eq, gt) == M1xA_FV_EQUAL) \
                    return TRUE; \
\
                a = 0; \
                b = 0; \
                *p = 0; \
            } \
\
            if (a == b) \
            { \
                *p = 0; \
                return FALSE; \
            } \
        } \
\
        return TRUE; \
    }
M1xA_findPartTemplate(DWORD)
    M1xA_findPartTemplate(WORD)
        M1xA_findPartTemplate(BYTE)

            inline BOOL SalGetVersionEx(OSVERSIONINFOEX* osVer, BOOL versionOnly)
{
    BOOL ret = TRUE;
    ZeroMemory(osVer, sizeof(OSVERSIONINFOEX));
    osVer->dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    if (!versionOnly)
    {
        ret &= M1xA_findPartDWORD(&osVer->dwPlatformId, VER_PLATFORMID, osVer, M1xA_FV_MIN_VALUE, MAXDWORD);
    }
    ret &= M1xA_findPartDWORD(&osVer->dwMajorVersion, VER_MAJORVERSION, osVer, M1xA_FV_MIN_VALUE, MAXDWORD);
    ret &= M1xA_findPartDWORD(&osVer->dwMinorVersion, VER_MINORVERSION, osVer, M1xA_FV_MIN_VALUE, M1xA_FV_MINOR_VERSION_MAX_VALUE);
    if (!versionOnly)
    {
        ret &= M1xA_findPartDWORD(&osVer->dwBuildNumber, VER_BUILDNUMBER, osVer, M1xA_FV_MIN_VALUE, MAXDWORD);
        ret &= M1xA_findPartWORD(&osVer->wServicePackMajor, VER_SERVICEPACKMAJOR, osVer, M1xA_FV_MIN_VALUE, MAXWORD);
        ret &= M1xA_findPartWORD(&osVer->wServicePackMinor, VER_SERVICEPACKMINOR, osVer, M1xA_FV_MIN_VALUE, MAXWORD);
        ret &= M1xA_findPartWORD(&osVer->wSuiteMask, VER_SUITENAME, osVer, M1xA_FV_MIN_VALUE, MAXWORD);
        ret &= M1xA_findPartBYTE(&osVer->wProductType, VER_PRODUCT_TYPE, osVer, M1xA_FV_MIN_VALUE, MAXBYTE);
    }
    return ret;
}

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_base)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__
