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
#pragma pack(push, enter_include_spl_gen) // to make structures independent of the configured alignment
#pragma pack(4)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

struct CFileData;
class CPluginDataInterfaceAbstract;

//
// ****************************************************************************
// CSalamanderGeneralAbstract
//
// general purpose methods of Salamander (for all types of plugins)

// message-box types
#define MSGBOX_INFO 0
#define MSGBOX_ERROR 1
#define MSGBOX_EX_ERROR 2
#define MSGBOX_QUESTION 3
#define MSGBOX_EX_QUESTION 4
#define MSGBOX_WARNING 5
#define MSGBOX_EX_WARNING 6

// constants for CSalamanderGeneralAbstract::SalMessageBoxEx
#define MSGBOXEX_OK 0x00000000                // MB_OK
#define MSGBOXEX_OKCANCEL 0x00000001          // MB_OKCANCEL
#define MSGBOXEX_ABORTRETRYIGNORE 0x00000002  // MB_ABORTRETRYIGNORE
#define MSGBOXEX_YESNOCANCEL 0x00000003       // MB_YESNOCANCEL
#define MSGBOXEX_YESNO 0x00000004             // MB_YESNO
#define MSGBOXEX_RETRYCANCEL 0x00000005       // MB_RETRYCANCEL
#define MSGBOXEX_CANCELTRYCONTINUE 0x00000006 // MB_CANCELTRYCONTINUE
#define MSGBOXEX_CONTINUEABORT 0x00000007     // MB_CONTINUEABORT
#define MSGBOXEX_YESNOOKCANCEL 0x00000008

#define MSGBOXEX_ICONHAND 0x00000010        // MB_ICONHAND / MB_ICONSTOP / MB_ICONERROR
#define MSGBOXEX_ICONQUESTION 0x00000020    // MB_ICONQUESTION
#define MSGBOXEX_ICONEXCLAMATION 0x00000030 // MB_ICONEXCLAMATION / MB_ICONWARNING
#define MSGBOXEX_ICONINFORMATION 0x00000040 // MB_ICONASTERISK / MB_ICONINFORMATION

#define MSGBOXEX_DEFBUTTON1 0x00000000 // MB_DEFBUTTON1
#define MSGBOXEX_DEFBUTTON2 0x00000100 // MB_DEFBUTTON2
#define MSGBOXEX_DEFBUTTON3 0x00000200 // MB_DEFBUTTON3
#define MSGBOXEX_DEFBUTTON4 0x00000300 // MB_DEFBUTTON4

#define MSGBOXEX_HELP 0x00004000 // MB_HELP (bit mask)

#define MSGBOXEX_SETFOREGROUND 0x00010000 // MB_SETFOREGROUND (bit mask)

// altap specific
#define MSGBOXEX_SILENT 0x10000000 // messagebox will not play any sound when opened (bit mask)
// in case of MB_YESNO messagebox enables Escape (generates IDNO); in MB_ABORTRETRYIGNORE messagebox
// enables Escape (generates IDCANCEL) (bit mask)
#define MSGBOXEX_ESCAPEENABLED 0x20000000
#define MSGBOXEX_HINT 0x40000000 // if CheckBoxText is used, the \t separator will be searched for in it and displayed as a hint
// Vista: default button will have "requires elevation" state (elevated icon will be displayed)
#define MSGBOXEX_SHIELDONDEFBTN 0x80000000

#define MSGBOXEX_TYPEMASK 0x0000000F // MB_TYPEMASK
#define MSGBOXEX_ICONMASK 0x000000F0 // MB_ICONMASK
#define MSGBOXEX_DEFMASK 0x00000F00  // MB_DEFMASK
#define MSGBOXEX_MODEMASK 0x00003000 // MB_MODEMASK
#define MSGBOXEX_MISCMASK 0x0000C000 // MB_MISCMASK
#define MSGBOXEX_EXMASK 0xF0000000

// message box return values
#define DIALOG_FAIL 0x00000000 // dialog failed to open
// individual buttons
#define DIALOG_OK 0x00000001       // IDOK
#define DIALOG_CANCEL 0x00000002   // IDCANCEL
#define DIALOG_ABORT 0x00000003    // IDABORT
#define DIALOG_RETRY 0x00000004    // IDRETRY
#define DIALOG_IGNORE 0x00000005   // IDIGNORE
#define DIALOG_YES 0x00000006      // IDYES
#define DIALOG_NO 0x00000007       // IDNO
#define DIALOG_TRYAGAIN 0x0000000a // IDTRYAGAIN
#define DIALOG_CONTINUE 0x0000000b // IDCONTINUE
// altap specific
#define DIALOG_SKIP 0x10000000
#define DIALOG_SKIPALL 0x20000000
#define DIALOG_ALL 0x30000000

typedef void(CALLBACK* MSGBOXEX_CALLBACK)(LPHELPINFO helpInfo);

struct MSGBOXEX_PARAMS
{
    HWND HParent;
    const char* Text;
    const char* Caption;
    DWORD Flags;
    HICON HIcon;
    DWORD ContextHelpId;
    MSGBOXEX_CALLBACK HelpCallback;
    const char* CheckBoxText;
    BOOL* CheckBoxValue;
    const char* AliasBtnNames;
    const char* URL;
    const char* URLText;
};

/*
HParent
  Handle to the owner window. Message box is centered to this window.
  If this parameter is NULL, the message box has no owner window.

Text
  Pointer to a null-terminated string that contains the message to be displayed.

Caption
  Pointer to a null-terminated string that contains the message box title.
  If this member is NULL, the default title "Error" is used.

Flags
  Specifies the contents and behavior of the message box.
  This parameter can be a combination of flags from the following groups of flags.

   To indicate the buttons displayed in the message box, specify one of the following values.
    MSGBOXEX_OK                   (MB_OK)
      The message box contains one push button: OK. This is the default.
      Message box can be closed using Escape and return value will be DIALOG_OK (IDOK).
    MSGBOXEX_OKCANCEL             (MB_OKCANCEL)
      The message box contains two push buttons: OK and Cancel.
    MSGBOXEX_ABORTRETRYIGNORE     (MB_ABORTRETRYIGNORE)
      The message box contains three push buttons: Abort, Retry, and Ignore.
      Message box can be closed using Escape when MSGBOXEX_ESCAPEENABLED flag is specified.
      In that case return value will be DIALOG_CANCEL (IDCANCEL).
    MSGBOXEX_YESNOCANCEL          (MB_YESNOCANCEL)
      The message box contains three push buttons: Yes, No, and Cancel.
    MSGBOXEX_YESNO                (MB_YESNO)
      The message box contains two push buttons: Yes and No.
      Message box can be closed using Escape when MSGBOXEX_ESCAPEENABLED flag is specified.
      In that case return value will be DIALOG_NO (IDNO).
    MSGBOXEX_RETRYCANCEL          (MB_RETRYCANCEL)
      The message box contains two push buttons: Retry and Cancel.
    MSGBOXEX_CANCELTRYCONTINUE    (MB_CANCELTRYCONTINUE)
      The message box contains three push buttons: Cancel, Try Again, Continue.

   To display an icon in the message box, specify one of the following values.
    MSGBOXEX_ICONHAND             (MB_ICONHAND / MB_ICONSTOP / MB_ICONERROR)
      A stop-sign icon appears in the message box.
    MSGBOXEX_ICONQUESTION         (MB_ICONQUESTION)
      A question-mark icon appears in the message box.
    MSGBOXEX_ICONEXCLAMATION      (MB_ICONEXCLAMATION / MB_ICONWARNING)
      An exclamation-point icon appears in the message box.
    MSGBOXEX_ICONINFORMATION      (MB_ICONASTERISK / MB_ICONINFORMATION)
      An icon consisting of a lowercase letter i in a circle appears in the message box.

   To indicate the default button, specify one of the following values.
    MSGBOXEX_DEFBUTTON1           (MB_DEFBUTTON1)
      The first button is the default button.
      MSGBOXEX_DEFBUTTON1 is the default unless MSGBOXEX_DEFBUTTON2, MSGBOXEX_DEFBUTTON3,
      or MSGBOXEX_DEFBUTTON4 is specified.
    MSGBOXEX_DEFBUTTON2           (MB_DEFBUTTON2)
      The second button is the default button.
    MSGBOXEX_DEFBUTTON3           (MB_DEFBUTTON3)
      The third button is the default button.
    MSGBOXEX_DEFBUTTON4           (MB_DEFBUTTON4)
      The fourth button is the default button.

   To specify other options, use one or more of the following values.
    MSGBOXEX_HELP                 (MB_HELP)
      Adds a Help button to the message box.
      When the user clicks the Help button or presses F1, the system sends a WM_HELP message to the owner
      or calls HelpCallback (see HelpCallback for details).
    MSGBOXEX_SETFOREGROUND        (MB_SETFOREGROUND)
      The message box becomes the foreground window. Internally, the system calls the SetForegroundWindow
      function for the message box.
    MSGBOXEX_SILENT
      No sound will be played when message box is displayed.
    MSGBOXEX_ESCAPEENABLED
      When MSGBOXEX_YESNO is specified, user can close message box using Escape key and DIALOG_NO (IDNO)
      will be returned. When MSGBOXEX_ABORTRETRYIGNORE is specified, user can close message box using
      Escape key and DIALOG_CANCEL (IDCANCEL) will be returned. Otherwise this option is ignored.

HIcon
  Handle to the icon to be drawn in the message box. Icon will not be destroyed when messagebox is closed.
  If this parameter is NULL, MSGBOXEX_ICONxxx style will be used.

ContextHelpId
  Identifies a help context. If a help event occurs, this value is specified in
  the HELPINFO structure that the message box sends to the owner window or callback function.

HelpCallback
  Pointer to the callback function that processes help events for the message box.
  The callback function has the following form:
    VOID CALLBACK MSGBOXEX_CALLBACK(LPHELPINFO helpInfo)
  If this member is NULL, the message box sends WM_HELP messages to the owner window
  when help events occur.

CheckBoxText
  Pointer to a null-terminated string that contains the checkbox text.
  If the MSGBOXEX_HINT flag is specified in the Flags, this text must contain HINT.
  Hint is separated from string by the TAB character. Hint is divided by the second TAB character
  on two parts. The first part is label, that will be displayed behind the check box.
  The second part is the text displayed when user clicks the hint label.

  Example: "This is text for checkbox\tHint Label\tThis text will be displayed when user click the Hint Label."
  If this member is NULL, checkbox will not be displayed.

CheckBoxValue
  Pointer to a BOOL variable contains the checkbox initial and return state (TRUE: checked, FALSE: unchecked).
  This parameter is ignored if CheckBoxText parameter is NULL. Otherwise this parameter must be set.

AliasBtnNames
  Pointer to a buffer containing pairs of id and alias strings. The last string in the
  buffer must be terminated by NULL character.

  The first string in each pair is a decimal number that specifies button ID.
  Number must be one of the DIALOG_xxx values. The second string specifies alias text
  for this button.

  First and second string in each pair are separated by TAB character.
  Pairs are separated by TAB character too.

  If this member is NULL, normal names of buttons will displayed.

  Example: sprintf(buffer, "%d\t%s\t%d\t%s", DIALOG_OK, "&Start", DIALOG_CANCEL, "E&xit");
           buffer: "1\t&Start\t2\tE&xit"

URL
  Pointer to a null-terminated string that contains the URL displayed below text.
  If this member is NULL, the URL is not displayed.

URLText
  Pointer to a null-terminated string that contains the URL text displayed below text.
  If this member is NULL, the URL is displayed instead.

*/

// panel identification
#define PANEL_SOURCE 1 // source panel (active panel)
#define PANEL_TARGET 2 // target panel (inactive panel)
#define PANEL_LEFT 3   // left panel
#define PANEL_RIGHT 4  // right panel

// path types
#define PATH_TYPE_WINDOWS 1 // Windows path ("c:\path" or UNC path)
#define PATH_TYPE_ARCHIVE 2 // path into archive (archive is located on Windows path)
#define PATH_TYPE_FS 3      // path to plugin file-system

// From the following group of flags, only one can be selected.
// They define the set of displayed buttons in various error messages.
#define BUTTONS_OK 0x00000000               // OK
#define BUTTONS_RETRYCANCEL 0x00000001      // Retry / Cancel
#define BUTTONS_SKIPCANCEL 0x00000002       // Skip / Skip all / Cancel
#define BUTTONS_RETRYSKIPCANCEL 0x00000003  // Retry / Skip / Skip all / Cancel
#define BUTTONS_YESALLSKIPCANCEL 0x00000004 // Yes / All / Skip / Skip all / Cancel
#define BUTTONS_YESNOCANCEL 0x00000005      // Yes / No / Cancel
#define BUTTONS_YESALLCANCEL 0x00000006     // Yes / All / Cancel
#define BUTTONS_MASK 0x000000FF             // internal mask, do not use
// detection whether combination has SKIP or YES button is left here in inline form, so that
// when adding new combinations it is visible and we do not forget to update it
inline BOOL ButtonsContainsSkip(DWORD btn)
{
    return (btn & BUTTONS_MASK) == BUTTONS_SKIPCANCEL ||
           (btn & BUTTONS_MASK) == BUTTONS_RETRYSKIPCANCEL ||
           (btn & BUTTONS_MASK) == BUTTONS_YESALLSKIPCANCEL;
}
inline BOOL ButtonsContainsYes(DWORD btn)
{
    return (btn & BUTTONS_MASK) == BUTTONS_YESALLSKIPCANCEL ||
           (btn & BUTTONS_MASK) == BUTTONS_YESNOCANCEL ||
           (btn & BUTTONS_MASK) == BUTTONS_YESALLCANCEL;
}

// error constants for CSalamanderGeneralAbstract::SalGetFullName
#define GFN_SERVERNAMEMISSING 1   // server name is missing in UNC path
#define GFN_SHARENAMEMISSING 2    // share name is missing in UNC path
#define GFN_TOOLONGPATH 3         // operation would result in too long path
#define GFN_INVALIDDRIVE 4        // in normal path (c:\) there is no letter A-Z (nor a-z)
#define GFN_INCOMLETEFILENAME 5   // relative path without specified 'curDir' -> unsolvable
#define GFN_EMPTYNAMENOTALLOWED 6 // empty string 'name'
#define GFN_PATHISINVALID 7       // cannot eliminate "..", e.g. "c:\.."

// error code for the state when user interrupts CSalamanderGeneralAbstract::SalCheckPath with ESC key
#define ERROR_USER_TERMINATED -100

#define PATH_MAX_PATH 248 // limit for max. path length (full directory name), note: the limit already includes null-terminator (max. string length is 247 characters)

// error constants for CSalamanderGeneralAbstract::SalParsePath:
// input was empty path and .curPath. was NULL (empty path is replaced with current path,
// but it is not known here)
#define SPP_EMPTYPATHNOTALLOWED 1
// Windows path (normal + UNC) does not exist, is not accessible, or user interrupted test
// for path accessibility (includes attempt to restore network connection)
#define SPP_WINDOWSPATHERROR 2
// Windows path starts with file name which is not an archive (otherwise it would be a path into archive)
#define SPP_NOTARCHIVEFILE 3
// FS path - plugin FS name (fs-name - before ':' in path) is not known (no plugin
// has this name registered)
#define SPP_NOTPLUGINFS 4
// it is a relative path, but current path is not known or it is FS (root cannot be determined there
// and we do not know the fs-user-part path structure at all, so conversion to absolute path cannot be performed)
// if current path is FS (.curPathIsDiskOrArchive. is FALSE), no error will be reported in this case
// to user (further processing on the FS side which called SalParsePath method is expected)
#define SPP_INCOMLETEPATH 5

// constants of Salamander's internal colors
#define SALCOL_FOCUS_ACTIVE_NORMAL 0 // pen colors for frame around item
#define SALCOL_FOCUS_ACTIVE_SELECTED 1
#define SALCOL_FOCUS_FG_INACTIVE_NORMAL 2
#define SALCOL_FOCUS_FG_INACTIVE_SELECTED 3
#define SALCOL_FOCUS_BK_INACTIVE_NORMAL 4
#define SALCOL_FOCUS_BK_INACTIVE_SELECTED 5
#define SALCOL_ITEM_FG_NORMAL 6 // text colors of items in panel
#define SALCOL_ITEM_FG_SELECTED 7
#define SALCOL_ITEM_FG_FOCUSED 8
#define SALCOL_ITEM_FG_FOCSEL 9
#define SALCOL_ITEM_FG_HIGHLIGHT 10
#define SALCOL_ITEM_BK_NORMAL 11 // background colors of items in panel
#define SALCOL_ITEM_BK_SELECTED 12
#define SALCOL_ITEM_BK_FOCUSED 13
#define SALCOL_ITEM_BK_FOCSEL 14
#define SALCOL_ITEM_BK_HIGHLIGHT 15
#define SALCOL_ICON_BLEND_SELECTED 16 // colors for icon blending
#define SALCOL_ICON_BLEND_FOCUSED 17
#define SALCOL_ICON_BLEND_FOCSEL 18
#define SALCOL_PROGRESS_FG_NORMAL 19 // progress bar colors
#define SALCOL_PROGRESS_FG_SELECTED 20
#define SALCOL_PROGRESS_BK_NORMAL 21
#define SALCOL_PROGRESS_BK_SELECTED 22
#define SALCOL_HOT_PANEL 23           // color of hot item in panel
#define SALCOL_HOT_ACTIVE 24          //                   in active window caption
#define SALCOL_HOT_INACTIVE 25        //                   in inactive caption, statusbar,...
#define SALCOL_ACTIVE_CAPTION_FG 26   // text color in active panel title
#define SALCOL_ACTIVE_CAPTION_BK 27   // background color in active panel title
#define SALCOL_INACTIVE_CAPTION_FG 28 // text color in inactive panel title
#define SALCOL_INACTIVE_CAPTION_BK 29 // background color in inactive panel title
#define SALCOL_VIEWER_FG_NORMAL 30    // text color in internal text/hex viewer
#define SALCOL_VIEWER_BK_NORMAL 31    // background color in internal text/hex viewer
#define SALCOL_VIEWER_FG_SELECTED 32  // selected text color in internal text/hex viewer
#define SALCOL_VIEWER_BK_SELECTED 33  // selected background color in internal text/hex viewer
#define SALCOL_THUMBNAIL_NORMAL 34    // pen colors for frame around thumbnail
#define SALCOL_THUMBNAIL_SELECTED 35
#define SALCOL_THUMBNAIL_FOCUSED 36
#define SALCOL_THUMBNAIL_FOCSEL 37

// constants for reasons why CSalamanderGeneralAbstract::ChangePanelPathToXXX methods returned failure:
#define CHPPFR_SUCCESS 0 // new path is in panel, success (return value is TRUE)
// new path (or archive name) cannot be converted from relative to absolute or
// new path (or archive name) is not accessible or
// path to FS cannot be opened (no plugin, refuses its load, refuses to open FS, fatal ChangePath error)
#define CHPPFR_INVALIDPATH 1
#define CHPPFR_INVALIDARCHIVE 2  // file is not an archive or cannot be listed as archive
#define CHPPFR_CANNOTCLOSEPATH 4 // current path cannot be closed
// shortened new path is in panel,
// clarification for FS: in panel there is either shortened new path or original path or shortened
// original path - original path is tried to be returned to panel only if new path was being opened
// in current FS (IsOurPath method returned TRUE for it) and if new path is not accessible
// (nor any of its subpaths)
#define CHPPFR_SHORTERPATH 5
// shortened new path is in panel; reason for shortening was that requested path was file name
// - path to file is in panel and file will be focused
#define CHPPFR_FILENAMEFOCUSED 6

// types for CSalamanderGeneralAbstract::ValidateVarString() and CSalamanderGeneralAbstract::ExpandVarString()
typedef const char*(WINAPI* FSalamanderVarStrGetValue)(HWND msgParent, void* param);
struct CSalamanderVarStrEntry
{
    const char* Name;                  // variable name in string (e.g. in string "$(name)" it is "name")
    FSalamanderVarStrGetValue Execute; // function that returns text representing the variable
};

class CSalamanderRegistryAbstract;

// callback type used for load/save configuration via
// CSalamanderGeneral::CallLoadOrSaveConfiguration; 'regKey' is NULL when loading
// default configuration (save is not called when 'regKey' == NULL); 'registry' is object for
// working with registry; 'param' is user parameter of function (see
// CSalamanderGeneral::CallLoadOrSaveConfiguration)
typedef void(WINAPI* FSalLoadOrSaveConfiguration)(BOOL load, HKEY regKey,
                                                  CSalamanderRegistryAbstract* registry, void* param);

// base structure for CSalamanderGeneralAbstract::ViewFileInPluginViewer (each plugin
// viewer can have this structure extended with its parameters - structure is passed to
// CPluginInterfaceForViewerAbstract::ViewFile - parameters can be e.g. window title,
// viewer mode, offset from file beginning, selection position, etc.); WARNING!!! about packing
// of structures (required is 4 bytes - see "#pragma pack(4)")
struct CSalamanderPluginViewerData
{
    // how many bytes from structure beginning are valid (for distinguishing structure versions)
    int Size;
    // file name to be opened in viewer (do not use in method
    // CPluginInterfaceForViewerAbstract::ViewFile - file name is given by parameter 'name')
    const char* FileName;
};

// extension of CSalamanderPluginViewerData structure for internal text/hex viewer
struct CSalamanderPluginInternalViewerData : public CSalamanderPluginViewerData
{
    int Mode;            // 0 - text mode, 1 - hex mode
    const char* Caption; // NULL -> window caption contains FileName, otherwise Caption
    BOOL WholeCaption;   // has meaning if Caption != NULL. TRUE -> in title
                         // viewer only the Caption string will be displayed; FALSE -> after
                         // Caption the standard " - Viewer" will be appended.
};

// constants for Salamander configuration parameter types (see CSalamanderGeneralAbstract::GetConfigParameter)
#define SALCFGTYPE_NOTFOUND 0 // parameter not found
#define SALCFGTYPE_BOOL 1     // TRUE/FALSE
#define SALCFGTYPE_INT 2      // 32-bit integer
#define SALCFGTYPE_STRING 3   // null-terminated multibyte string
#define SALCFGTYPE_LOGFONT 4  // Win32 LOGFONT structure

// constants for Salamander configuration parameters (see CSalamanderGeneralAbstract::GetConfigParameter);
// in comment the parameter type is specified (BOOL, INT, STRING), after STRING the required
// buffer size for string is in parentheses
//
// general parameters
#define SALCFG_SELOPINCLUDEDIRS 1        // BOOL, select/deselect operations (num *, num +, num -) work also with directories
#define SALCFG_SAVEONEXIT 2              // BOOL, save configuration on Salamander exit
#define SALCFG_MINBEEPWHENDONE 3         // BOOL, should it beep (play sound) after end of work in inactive window?
#define SALCFG_HIDEHIDDENORSYSTEMFILES 4 // BOOL, should it hide system and/or hidden files?
#define SALCFG_ALWAYSONTOP 6             // BOOL, main window is Always On Top?
#define SALCFG_SORTUSESLOCALE 7          // BOOL, should it use regional settings when sorting?
#define SALCFG_SINGLECLICK 8             // BOOL, single click mode (single click to open file, etc.)
#define SALCFG_TOPTOOLBARVISIBLE 9       // BOOL, is top toolbar visible?
#define SALCFG_BOTTOMTOOLBARVISIBLE 10   // BOOL, is bottom toolbar visible?
#define SALCFG_USERMENUTOOLBARVISIBLE 11 // BOOL, is user-menu toolbar visible?
#define SALCFG_INFOLINECONTENT 12        // STRING (200), content of Information Line (string with parameters)
#define SALCFG_FILENAMEFORMAT 13         // INT, how to alter file name before displaying (parameter 'format' to CSalamanderGeneralAbstract::AlterFileName)
#define SALCFG_SAVEHISTORY 14            // BOOL, may history related data be stored to configuration?
#define SALCFG_ENABLECMDLINEHISTORY 15   // BOOL, is command line history enabled?
#define SALCFG_SAVECMDLINEHISTORY 16     // BOOL, may command line history be stored to configuration?
#define SALCFG_MIDDLETOOLBARVISIBLE 17   // BOOL, is middle toolbar visible?
#define SALCFG_SORTDETECTNUMBERS 18      // BOOL, should it use numerical sort for numbers contained in strings when sorting?
#define SALCFG_SORTBYEXTDIRSASFILES 19   // BOOL, should it treat dirs as files when sorting by extension? BTW, if TRUE, directories extensions are also displayed in separated Ext column. (directories have no extensions, only files have extensions, but many people have requested sort by extension and displaying extension in separated Ext column even for directories)
#define SALCFG_SIZEFORMAT 20             // INT, units for custom size columns, 0 - Bytes, 1 - KB, 2 - short (mixed B, KB, MB, GB, ...)
#define SALCFG_SELECTWHOLENAME 21        // BOOL, should be whole name selected (including extension) when entering new filename? (for dialog boxes F2:QuickRename, Alt+F5:Pack, etc)
// recycle bin parameters
#define SALCFG_USERECYCLEBIN 50   // INT, 0 - do not use, 1 - use for all, 2 - use for files matching at \
                                  //      least one of masks (see SALCFG_RECYCLEBINMASKS)
#define SALCFG_RECYCLEBINMASKS 51 // STRING (MAX_PATH), masks for SALCFG_USERECYCLEBIN==2
// time resolution of file compare (used in command Compare Directories)
#define SALCFG_COMPDIRSUSETIMERES 60 // BOOL, should it use time resolution? (FALSE==exact match)
#define SALCFG_COMPDIRTIMERES 61     // INT, time resolution for file compare (from 0 to 3600 second)
// confirmations
#define SALCFG_CNFRMFILEDIRDEL 70 // BOOL, files or directories delete
#define SALCFG_CNFRMNEDIRDEL 71   // BOOL, non-empty directory delete
#define SALCFG_CNFRMFILEOVER 72   // BOOL, file overwrite
#define SALCFG_CNFRMSHFILEDEL 73  // BOOL, system or hidden file delete
#define SALCFG_CNFRMSHDIRDEL 74   // BOOL, system or hidden directory delete
#define SALCFG_CNFRMSHFILEOVER 75 // BOOL, system or hidden file overwrite
#define SALCFG_CNFRMCREATEPATH 76 // BOOL, show "do you want to create target path?" in Copy/Move operations
#define SALCFG_CNFRMDIROVER 77    // BOOL, directory overwrite (copy/move selected directory: ask user if directory already exists on target path - standard behaviour is to join contents of both directories)
// drive specific settings
#define SALCFG_DRVSPECFLOPPYMON 88         // BOOL, floppy disks - use automatic refresh (changes monitoring)
#define SALCFG_DRVSPECFLOPPYSIM 89         // BOOL, floppy disks - use simple icons
#define SALCFG_DRVSPECREMOVABLEMON 90      // BOOL, removable disks - use automatic refresh (changes monitoring)
#define SALCFG_DRVSPECREMOVABLESIM 91      // BOOL, removable disks - use simple icons
#define SALCFG_DRVSPECFIXEDMON 92          // BOOL, fixed disks - use automatic refresh (changes monitoring)
#define SALCFG_DRVSPECFIXEDSIMPLE 93       // BOOL, fixed disks - use simple icons
#define SALCFG_DRVSPECREMOTEMON 94         // BOOL, remote (network) disks - use automatic refresh (changes monitoring)
#define SALCFG_DRVSPECREMOTESIMPLE 95      // BOOL, remote (network) disks - use simple icons
#define SALCFG_DRVSPECREMOTEDONOTREF 96    // BOOL, remote (network) disks - do not refresh on activation of Salamander
#define SALCFG_DRVSPECCDROMMON 97          // BOOL, CDROM disks - use automatic refresh (changes monitoring)
#define SALCFG_DRVSPECCDROMSIMPLE 98       // BOOL, CDROM disks - use simple icons
#define SALCFG_IFPATHISINACCESSIBLEGOTO 99 // STRING (MAX_PATH), path where to go if path in panel is inaccessible
// internal text/hex viewer
#define SALCFG_VIEWEREOLCRLF 120          // BOOL, accept CR-LF ("\r\n") line ends?
#define SALCFG_VIEWEREOLCR 121            // BOOL, accept CR ("\r") line ends?
#define SALCFG_VIEWEREOLLF 122            // BOOL, accept LF ("\n") line ends?
#define SALCFG_VIEWEREOLNULL 123          // BOOL, accept NULL ("\0") line ends?
#define SALCFG_VIEWERTABSIZE 124          // INT, size of tab ("\t") character in spaces
#define SALCFG_VIEWERSAVEPOSITION 125     // BOOL, TRUE = save position of viewer window, FALSE = always use position of main window
#define SALCFG_VIEWERFONT 126             // LOGFONT, viewer font
#define SALCFG_VIEWERWRAPTEXT 127         // BOOL, wrap text (divide long text line to more lines)
#define SALCFG_AUTOCOPYSELTOCLIPBOARD 128 // BOOL, TRUE = when user selects some text, this text is instantly copied to the cliboard
// archivers
#define SALCFG_ARCOTHERPANELFORPACK 140    // BOOL, should it pack to other panel path?
#define SALCFG_ARCOTHERPANELFORUNPACK 141  // BOOL, should it unpack to other panel path?
#define SALCFG_ARCSUBDIRBYARCFORUNPACK 142 // BOOL, should it unpack to subdirectory named by archive?
#define SALCFG_ARCUSESIMPLEICONS 143       // BOOL, should it use simple icons in archives?

// callback type used in method CSalamanderGeneral::SalSplitGeneralPath
typedef BOOL(WINAPI* SGP_IsTheSamePathF)(const char* path1, const char* path2);

// callback type used in method CSalamanderGeneralAbstract::CallPluginOperationFromDisk
// 'sourcePath' is source path on disk (other paths are relative to it);
// selected files/directories are specified by enumeration function 'next' with parameter
// 'nextParam'; 'param' is parameter passed to CallPluginOperationFromDisk as 'param'
typedef void(WINAPI* SalPluginOperationFromDisk)(const char* sourcePath, SalEnumSelection2 next,
                                                 void* nextParam, void* param);

// flags for text search algorithms (CSalamanderBMSearchData and CSalamanderREGEXPSearchData);
// flags can be logically combined
#define SASF_CASESENSITIVE 0x01 // case sensitivity is important (if not set, search is case insensitive)
#define SASF_FORWARD 0x02       // search forward direction (if not set, search is backward)

// icons for GetSalamanderIcon
#define SALICON_EXECUTABLE 1    // exe/bat/pif/com
#define SALICON_DIRECTORY 2     // dir
#define SALICON_NONASSOCIATED 3 // non-associated file
#define SALICON_ASSOCIATED 4    // associated file
#define SALICON_UPDIR 5         // up-dir ".."
#define SALICON_ARCHIVE 6       // archive

// icon sizes for GetSalamanderIcon
#define SALICONSIZE_16 1 // 16x16
#define SALICONSIZE_32 2 // 32x32
#define SALICONSIZE_48 3 // 48x48

// interface of Boyer-Moore algorithm object for text searching
// WARNING: each allocated object can only be used within a single thread
// (does not have to be the main thread, does not have to be the same thread for all objects)
class CSalamanderBMSearchData
{
public:
    // set pattern; 'pattern' is null-terminated pattern text; 'flags' are algorithm flags
    // (see SASF_XXX constants)
    virtual void WINAPI Set(const char* pattern, WORD flags) = 0;

    // set pattern; 'pattern' is binary pattern of length 'length' (buffer 'pattern' must
    // have length at least ('length' + 1) characters - for compatibility with text patterns);
    // 'flags' are algorithm flags (see SASF_XXX constants)
    virtual void WINAPI Set(const char* pattern, const int length, WORD flags) = 0;

    // set algorithm flags; 'flags' are algorithm flags (see SASF_XXX constants)
    virtual void WINAPI SetFlags(WORD flags) = 0;

    // returns pattern length (usable after successful call to Set method)
    virtual int WINAPI GetLength() const = 0;

    // returns pattern (usable after successful call to Set method)
    virtual const char* WINAPI GetPattern() const = 0;

    // returns TRUE if searching can begin (pattern and flags were successfully set,
    // failure only occurs with empty pattern)
    virtual BOOL WINAPI IsGood() const = 0;

    // search for pattern in text 'text' of length 'length' from offset 'start' forward;
    // returns offset of found pattern or -1 if pattern was not found;
    // WARNING: algorithm must have SASF_FORWARD flag set
    virtual int WINAPI SearchForward(const char* text, int length, int start) = 0;

    // search for pattern in text 'text' of length 'length' backward (starts searching at end of text);
    // returns offset of found pattern or -1 if pattern was not found;
    // WARNING: algorithm must NOT have SASF_FORWARD flag set
    virtual int WINAPI SearchBackward(const char* text, int length) = 0;
};

// interface of regular expression search algorithm object for text searching
// WARNING: each allocated object can only be used within a single thread
// (does not have to be the main thread, does not have to be the same thread for all objects)
class CSalamanderREGEXPSearchData
{
public:
    // set regular expression; 'pattern' is null-terminated regular expression text; 'flags'
    // are algorithm flags (see SASF_XXX constants); on error returns FALSE and error description
    // can be obtained by calling GetLastErrorText method
    virtual BOOL WINAPI Set(const char* pattern, WORD flags) = 0;

    // set algorithm flags; 'flags' are algorithm flags (see SASF_XXX constants);
    // on error returns FALSE and error description can be obtained by calling GetLastErrorText method
    virtual BOOL WINAPI SetFlags(WORD flags) = 0;

    // returns error text from last Set or SetFlags call (can be NULL)
    virtual const char* WINAPI GetLastErrorText() const = 0;

    // returns regular expression text (usable after successful call to Set method)
    virtual const char* WINAPI GetPattern() const = 0;

    // set text line (line is from 'start' to 'end', 'end' points past the last character of line),
    // in which to search; always returns TRUE
    virtual BOOL WINAPI SetLine(const char* start, const char* end) = 0;

    // search for substring matching regular expression in line set by SetLine method;
    // searches from offset 'start' forward; returns offset of found substring and its length
    // (in 'foundLen') or -1 if substring was not found;
    // WARNING: algorithm must have SASF_FORWARD flag set
    virtual int WINAPI SearchForward(int start, int& foundLen) = 0;

    // search for substring matching regular expression in line set by SetLine method;
    // searches backward (starts searching at end of text of length 'length' from beginning of line);
    // returns offset of found substring and its length (in 'foundLen') or -1 if substring
    // was not found;
    // WARNING: algorithm must NOT have SASF_FORWARD flag set
    virtual int WINAPI SearchBackward(int length, int& foundLen) = 0;
};

// command types used in CSalamanderGeneralAbstract::EnumSalamanderCommands method
#define sctyUnknown 0
#define sctyForFocusedFile 1                 // only for focused file (e.g. View)
#define sctyForFocusedFileOrDirectory 2      // for focused file or directory (e.g. Open)
#define sctyForSelectedFilesAndDirectories 3 // for selected/focused files and directories (e.g. Copy)
#define sctyForCurrentPath 4                 // for current path in panel (e.g. Create Directory)
#define sctyForConnectedDrivesAndFS 5        // for connected drives and FS (e.g. Disconnect)

// Salamander commands used in CSalamanderGeneralAbstract::EnumSalamanderCommands
// and CSalamanderGeneralAbstract::PostSalamanderCommand methods
// (WARNING: command numbers are reserved only in interval <0, 499>)
#define SALCMD_VIEW 0     // view (F3 key in panel)
#define SALCMD_ALTVIEW 1  // alternate view (Alt+F3 key in panel)
#define SALCMD_VIEWWITH 2 // view with (Ctrl+Shift+F3 key in panel)
#define SALCMD_EDIT 3     // edit (F4 key in panel)
#define SALCMD_EDITWITH 4 // edit with (Ctrl+Shift+F4 key in panel)

#define SALCMD_OPEN 20        // open (Enter key in panel)
#define SALCMD_QUICKRENAME 21 // quick rename (F2 key in panel)

#define SALCMD_COPY 40          // copy (F5 key in panel)
#define SALCMD_MOVE 41          // move/rename (F6 key in panel)
#define SALCMD_EMAIL 42         // email (Ctrl+E key in panel)
#define SALCMD_DELETE 43        // delete (Delete key in panel)
#define SALCMD_PROPERTIES 44    // show properties (Alt+Enter key in panel)
#define SALCMD_CHANGECASE 45    // change case (Ctrl+F7 key in panel)
#define SALCMD_CHANGEATTRS 46   // change attributes (Ctrl+F2 key in panel)
#define SALCMD_OCCUPIEDSPACE 47 // calculate occupied space (Alt+F10 key in panel)

#define SALCMD_EDITNEWFILE 70     // edit new file (Shift+F4 key in panel)
#define SALCMD_REFRESH 71         // refresh (Ctrl+R key in panel)
#define SALCMD_CREATEDIRECTORY 72 // create directory (F7 key in panel)
#define SALCMD_DRIVEINFO 73       // drive info (Ctrl+F1 key in panel)
#define SALCMD_CALCDIRSIZES 74    // calculate directory sizes (Ctrl+Shift+F10 key in panel)

#define SALCMD_DISCONNECT 90 // disconnect (network drive or plugin-fs) (F12 key in panel)

#define MAX_GROUPMASK 1001 // max. number of characters (including null terminator) in group mask

// shared history identifiers (last used values in comboboxes) for
// CSalamanderGeneral::GetStdHistoryValues()
#define SALHIST_QUICKRENAME 1 // names in Quick Rename dialog (F2)
#define SALHIST_COPYMOVETGT 2 // target paths in Copy/Move dialog (F5/F6)
#define SALHIST_CREATEDIR 3   // directory names in Create Directory dialog (F7)
#define SALHIST_CHANGEDIR 4   // paths in Change Directory dialog (Shift+F7)
#define SALHIST_EDITNEW 5     // names in Edit New dialog (Shift+F4)
#define SALHIST_CONVERT 6     // names in Convert dialog (Ctrl+K)

// interface of object for working with a group of file masks
// WARNING: object methods are not synchronized, so they can only be used
//          within a single thread (does not have to be the main thread) or
//          the plugin must ensure synchronization (no "write" can be performed during
//          execution of another method; "write"=SetMasksString+PrepareMasks;
//          "read" can be performed from multiple threads simultaneously; "read"=GetMasksString+
//          AgreeMasks)
//
// Object lifecycle:
//   1) Allocate using CSalamanderGeneralAbstract::AllocSalamanderMaskGroup method
//   2) Pass the mask group in SetMasksString method
//   3) Call PrepareMasks to build internal data; on failure
//      display error location and after fixing the mask return to step (3)
//   4) Call AgreeMasks as needed to check if name matches the mask group
//   5) After optional call to SetMasksString continue from step (3)
//   6) Destroy object using CSalamanderGeneralAbstract::FreeSalamanderMaskGroup method
//
// Mask:
//   '?' - any character
//   '*' - any string (including empty)
//   '#' - any digit (only if 'extendedMode'==TRUE)
//
//   Examples:
//     *     - all names
//     *.*   - all names
//     *.exe - names with extension "exe"
//     *.t?? - names with extension starting with 't' and containing two more arbitrary characters
//     *.r## - names with extension starting with 'r' and containing two more arbitrary digits
//
class CSalamanderMaskGroup
{
public:
    // set masks string (masks are separated by ';' (escape sequence for ';' is ";;"));
    // 'masks' is the masks string (max. length including null terminator is MAX_GROUPMASK)
    // if 'extendedMode' is TRUE, character '#' matches any digit ('0'-'9')
    // character '|' can be used as separator; following masks (again separated by ';')
    // will be evaluated inversely, meaning if they match a name,
    // AgreeMasks will return FALSE; character '|' can be at the beginning of string
    //
    //   Examples:
    //     *.txt;*.cpp - all names with extension txt or cpp
    //     *.h*|*.html - all names with extension starting with 'h', but not names with extension "html"
    //     |*.txt      - all names with extension other than "txt"
    virtual void WINAPI SetMasksString(const char* masks, BOOL extendedMode) = 0;

    // returns masks string; 'buffer' is buffer of at least MAX_GROUPMASK length
    virtual void WINAPI GetMasksString(char* buffer) = 0;

    // returns 'extendedMode' set in SetMasksString method
    virtual BOOL WINAPI GetExtendedMode() = 0;

    // working with file masks: ('?' any char, '*' any string - including empty, if
    //  'extendedMode' in SetMasksString method was TRUE, '#' any digit - '0'..'9'):
    // 1) convert masks to simpler format; 'errorPos' returns error position in masks string;
    //    returns TRUE if no error occurred (returns FALSE -> 'errorPos' is set)
    virtual BOOL WINAPI PrepareMasks(int& errorPos) = 0;
    // 2) use converted masks to test if any of them matches file 'filename';
    //    'fileExt' points either to end of 'fileName' or to extension (if exists), 'fileExt'
    //    can be NULL (extension is found using standard rules); returns TRUE if file
    //    matches at least one of the masks
    virtual BOOL WINAPI AgreeMasks(const char* fileName, const char* fileExt) = 0;
};

// interface of object for MD5 calculation
//
// Object lifecycle:
//
//   1) Allocate using CSalamanderGeneralAbstract::AllocSalamanderMD5 method
//   2) Call Update() method repeatedly for data for which we want to calculate MD5
//   3) Call Finalize() method
//   4) Retrieve calculated MD5 using GetDigest() method
//   5) If we want to reuse the object, call Init() method
//      (called automatically in step (1)) and go to step (2)
//   6) Destroy object using CSalamanderGeneralAbstract::FreeSalamanderMD5 method
//
class CSalamanderMD5
{
public:
    // object initialization, automatically called in constructor
    // method is published for multiple use of allocated object
    virtual void WINAPI Init() = 0;

    // updates internal state of object based on data block specified by 'input' variable,
    // 'input_length' specifies buffer size in bytes
    virtual void WINAPI Update(const void* input, DWORD input_length) = 0;

    // prepares MD5 for retrieval using GetDigest method
    // after calling Finalize method, only GetDigest() and Init() can be called
    virtual void WINAPI Finalize() = 0;

    // retrieves MD5, 'dest' must point to a buffer of 16 bytes size
    // method can only be called after calling Finalize() method
    virtual void WINAPI GetDigest(void* dest) = 0;
};

#define SALPNG_GETALPHA 0x00000002    // when creating DIB, alpha channel is also set (otherwise it will be 0)
#define SALPNG_PREMULTIPLE 0x00000004 // meaningful if SALPNG_GETALPHA is set; premultiplies RGB components so that AlphaBlend() can be called on the bitmap with BLENDFUNCTION::AlphaFormat==AC_SRC_ALPHA

class CSalamanderPNGAbstract
{
public:
    // creates bitmap based on PNG resource; 'hInstance' and 'lpBitmapName' specify the resource,
    // 'flags' contains 0 or bits from SALPNG_xxx family
    // on success returns bitmap handle, otherwise NULL
    // plugin is responsible for destroying bitmap by calling DeleteObject()
    // can be called from any thread
    virtual HBITMAP WINAPI LoadPNGBitmap(HINSTANCE hInstance, LPCTSTR lpBitmapName, DWORD flags, COLORREF unused) = 0;

    // creates bitmap based on PNG provided in memory; 'rawPNG' is pointer to memory containing PNG
    // (e.g. loaded from file) and 'rawPNGSize' specifies size of memory occupied by PNG in bytes,
    // 'flags' contains 0 or bits from SALPNG_xxx family
    // on success returns bitmap handle, otherwise NULL
    // plugin is responsible for destroying bitmap by calling DeleteObject()
    // can be called from any thread
    virtual HBITMAP WINAPI LoadRawPNGBitmap(const void* rawPNG, DWORD rawPNGSize, DWORD flags, COLORREF unused) = 0;

    // note 1: loaded PNG should be compressed using PNGSlim, see https://forum.altap.cz/viewtopic.php?f=15&t=3278
    // note 2: example of direct DIB data access see Demoplugin, AlphaBlend function
    // note 3: supported are non-interlaced PNG types: Greyscale, Greyscale with alpha, Truecolour, Truecolour with alpha, Indexed-colour
    //         condition is 8 bits per channel
};

// all methods can only be called from the main thread
class CSalamanderPasswordManagerAbstract
{
public:
    // returns TRUE if user has set master password in Salamander configuration, otherwise returns FALSE
    // (unrelated to whether MP was entered in this session)
    virtual BOOL WINAPI IsUsingMasterPassword() = 0;

    // returns TRUE if user has entered correct master password in this Salamander session, otherwise returns FALSE
    virtual BOOL WINAPI IsMasterPasswordSet() = 0;

    // displays window with parent 'hParent' prompting for master password entry
    // returns TRUE if correct MP was entered, otherwise returns FALSE
    // asks even if master password was already entered in this session, see IsMasterPasswordSet()
    // if user is not using master password, returns FALSE, see IsUsingMasterPassword()
    virtual BOOL WINAPI AskForMasterPassword(HWND hParent) = 0;

    // reads 'plainPassword' terminated with null and based on 'encrypt' variable either encrypts it (if TRUE) using AES or
    // only scrambles it (if FALSE); stores allocated result in 'encryptedPassword' and returns its size in variable
    // 'encryptedPasswordSize'; returns TRUE on success, otherwise FALSE
    // if 'encrypt'==TRUE, caller must ensure master password is entered before calling this function, see AskForMasterPassword()
    // note: returned 'encryptedPassword' is allocated on Salamander heap; if plugin does not use salrtl, buffer must be freed
    // using SalamanderGeneral->Free(), otherwise free() is sufficient;
    virtual BOOL WINAPI EncryptPassword(const char* plainPassword, BYTE** encryptedPassword, int* encryptedPasswordSize, BOOL encrypt) = 0;

    // reads 'encryptedPassword' of size 'encryptedPasswordSize' and converts it to plain password, which is returned
    // in allocated buffer 'plainPassword'; returns TRUE on success, otherwise FALSE
    // note: returned 'plainPassword' is allocated on Salamander heap; if plugin does not use salrtl, buffer must be freed
    // using SalamanderGeneral->Free(), otherwise free() is sufficient;
    virtual BOOL WINAPI DecryptPassword(const BYTE* encryptedPassword, int encryptedPasswordSize, char** plainPassword) = 0;

    // returns TRUE if 'encyptedPassword' of length 'encyptedPasswordSize' is encrypted using AES; otherwise returns FALSE
    virtual BOOL WINAPI IsPasswordEncrypted(const BYTE* encyptedPassword, int encyptedPasswordSize) = 0;
};

// modes for CSalamanderGeneralAbstract::ExpandPluralFilesDirs method
#define epfdmNormal 0   // XXX files and YYY directories
#define epfdmSelected 1 // XXX selected files and YYY selected directories
#define epfdmHidden 2   // XXX hidden files and YYY hidden directories

// commands for HTML help: see CSalamanderGeneralAbstract::OpenHtmlHelp method
enum CHtmlHelpCommand
{
    HHCDisplayTOC,     // see HH_DISPLAY_TOC: dwData = 0 (no topic) or: pointer to a topic within a compiled help file
    HHCDisplayIndex,   // see HH_DISPLAY_INDEX: dwData = 0 (no keyword) or: keyword to select in the index (.hhk) file
    HHCDisplaySearch,  // see HH_DISPLAY_SEARCH: dwData = 0 (empty search) or: pointer to an HH_FTS_QUERY structure
    HHCDisplayContext, // see HH_HELP_CONTEXT: dwData = numeric ID of the topic to display
};

// serves as parameter for OpenHtmlHelpForSalamander when command==HHCDisplayContext
#define HTMLHELP_SALID_PWDMANAGER 1 // displays help for Password Manager

class CPluginFSInterfaceAbstract;

class CSalamanderZLIBAbstract;

class CSalamanderBZIP2Abstract;

class CSalamanderCryptAbstract;

class CSalamanderGeneralAbstract
{
public:
    // displays message-box with specified text and title, parent of message-box is HWND
    // returned by GetMsgBoxParent() method (see below); uses SalMessageBox (see below)
    // type = MSGBOX_INFO        - information (ok)
    // type = MSGBOX_ERROR       - error message (ok)
    // type = MSGBOX_EX_ERROR    - error message (ok/cancel) - returns IDOK, IDCANCEL
    // type = MSGBOX_QUESTION    - question (yes/no) - returns IDYES, IDNO
    // type = MSGBOX_EX_QUESTION - question (yes/no/cancel) - returns IDYES, IDNO, IDCANCEL
    // type = MSGBOX_WARNING     - warning (ok)
    // type = MSGBOX_EX_WARNING  - warning (yes/no/cancel) - returns IDYES, IDNO, IDCANCEL
    // returns 0 on error
    // limitation: main thread
    virtual int WINAPI ShowMessageBox(const char* text, const char* title, int type) = 0;

    // SalMessageBox and SalMessageBoxEx create, display and after selecting one of the buttons
    // close message box. Message box can contain user-defined title, message,
    // buttons, icon, checkbox with some text.
    //
    // If 'hParent' is not the current foreground window (msgbox in inactive application),
    // FlashWindow(mainwnd, TRUE) is called before displaying msgbox and after closing msgbox
    // FlashWindow(mainwnd, FALSE) is called, mainwnd is the window in parent chain of 'hParent'
    // that has no parent (typically the main Salamander window).
    //
    // SalMessageBox fills MSGBOXEX_PARAMS structure (hParent->HParent, lpText->Text,
    // lpCaption->Caption and uType->Flags; other structure members are zeroed) and calls
    // SalMessageBoxEx, so we will only describe SalMessageBoxEx below.
    //
    // SalMessageBoxEx tries to behave as closely as possible to Windows API functions
    // MessageBox and MessageBoxIndirect. Differences are:
    //   - message box is centered to hParent (if it's a child window, non-child parent is found)
    //   - for MB_YESNO/MB_ABORTRETRYIGNORE message boxes, closing the window with
    //     Escape key or clicking the X button in title can be enabled (flag
    //     MSGBOXEX_ESCAPEENABLED); return value will then be IDNO/IDCANCEL
    //   - beep can be suppressed (flag MSGBOXEX_SILENT)
    //
    // Comment on uType see comment on MSGBOXEX_PARAMS::Flags
    //
    // Return Values
    //    DIALOG_FAIL       (0)            The function fails.
    //    DIALOG_OK         (IDOK)         'OK' button was selected.
    //    DIALOG_CANCEL     (IDCANCEL)     'Cancel' button was selected.
    //    DIALOG_ABORT      (IDABORT)      'Abort' button was selected.
    //    DIALOG_RETRY      (IDRETRY)      'Retry' button was selected.
    //    DIALOG_IGNORE     (IDIGNORE)     'Ignore' button was selected.
    //    DIALOG_YES        (IDYES)        'Yes' button was selected.
    //    DIALOG_NO         (IDNO)         'No' button was selected.
    //    DIALOG_TRYAGAIN   (IDTRYAGAIN)   'Try Again' button was selected.
    //    DIALOG_CONTINUE   (IDCONTINUE)   'Continue' button was selected.
    //    DIALOG_SKIP                      'Skip' button was selected.
    //    DIALOG_SKIPALL                   'Skip All' button was selected.
    //    DIALOG_ALL                       'All' button was selected.
    //
    // SalMessageBox and SalMessageBoxEx can be called from any thread
    virtual int WINAPI SalMessageBox(HWND hParent, LPCTSTR lpText, LPCTSTR lpCaption, UINT uType) = 0;
    virtual int WINAPI SalMessageBoxEx(const MSGBOXEX_PARAMS* params) = 0;

    // returns HWND of suitable parent for opened message-boxes (or other modal windows),
    // this is the main window, progress-dialog, Plugins/Plugins dialog or other modal window
    // opened to the main window
    // limitation: main thread, returned HWND is always from main thread
    virtual HWND WINAPI GetMsgBoxParent() = 0;

    // returns handle of Salamander main window
    // can be called from any thread
    virtual HWND WINAPI GetMainWindowHWND() = 0;

    // restores focus in panel or in command line (depending on what was last activated); this
    // call is needed if plugin disables/enables Salamander main window (this creates situations
    // where disabled main window is activated - focus cannot be set in disabled window -
    // after enabling main window, focus must be restored using this method)
    virtual void WINAPI RestoreFocusInSourcePanel() = 0;

    // commonly used dialogs, dialog parent 'parent', return values DIALOG_XXX;
    // if 'parent' is not the current foreground window (dialog in inactive application),
    // FlashWindow(mainwnd, TRUE) is called before displaying dialog and after closing dialog
    // FlashWindow(mainwnd, FALSE) is called, mainwnd is the window in parent chain of 'parent'
    // that has no parent (typically the main Salamander window)
    // ERROR: filename+error+title (if 'title' == NULL, standard title "Error" is used)
    //
    // Variable 'flags' determines displayed buttons, for DialogError one of these values can be used:
    // BUTTONS_OK               // OK                                    (old DialogError3)
    // BUTTONS_RETRYCANCEL      // Retry / Cancel                        (old DialogError4)
    // BUTTONS_SKIPCANCEL       // Skip / Skip all / Cancel              (old DialogError2)
    // BUTTONS_RETRYSKIPCANCEL  // Retry / Skip / Skip all / Cancel      (old DialogError)
    //
    // all can be called from any thread
    virtual int WINAPI DialogError(HWND parent, DWORD flags, const char* fileName, const char* error, const char* title) = 0;

    // CONFIRM FILE OVERWRITE: filename1+filedata1+filename2+filedata2
    // Variable 'flags' determines displayed buttons, for DialogOverwrite one of these values can be used:
    // BUTTONS_YESALLSKIPCANCEL // Yes / All / Skip / Skip all / Cancel  (old DialogOverwrite)
    // BUTTONS_YESNOCANCEL      // Yes / No / Cancel                     (old DialogOverwrite2)
    virtual int WINAPI DialogOverwrite(HWND parent, DWORD flags, const char* fileName1, const char* fileData1,
                                       const char* fileName2, const char* fileData2) = 0;

    // QUESTION: filename+question+title (if 'title' == NULL, standard title "Question" is used)
    // Variable 'flags' determines displayed buttons, for DialogQuestion one of these values can be used:
    // BUTTONS_YESALLSKIPCANCEL // Yes / All / Skip / Skip all / Cancel  (old DialogQuestion)
    // BUTTONS_YESNOCANCEL      // Yes / No / Cancel                     (old DialogQuestion2)
    // BUTTONS_YESALLCANCEL     // Yes / All / Cancel                    (old DialogQuestion3)
    virtual int WINAPI DialogQuestion(HWND parent, DWORD flags, const char* fileName,
                                      const char* question, const char* title) = 0;

    // if path 'dir' does not exist, allows creating it (asks user; creates multiple
    // directories at the end of path if needed); returns TRUE if path exists or is successfully created;
    // if path does not exist and 'quiet' is TRUE, does not ask user if they want to create
    // path 'dir'; if 'errBuf' is NULL, shows errors in windows; if 'errBuf' is not NULL,
    // puts error descriptions in buffer 'errBuf' of size 'errBufSize' (no error windows are
    // opened); all opened windows have 'parent' as parent, if 'parent' is NULL,
    // Salamander main window is used; if 'firstCreatedDir' is not NULL, it's a buffer
    // of size MAX_PATH for storing full name of first created directory on path
    // 'dir' (returns empty string if path 'dir' already exists); if 'manualCrDir' is TRUE,
    // does not allow creating directory with space at beginning of name (Windows doesn't mind,
    // but it's potentially dangerous, e.g. Explorer also doesn't allow it)
    // can be called from any thread
    virtual BOOL WINAPI CheckAndCreateDirectory(const char* dir, HWND parent = NULL, BOOL quiet = TRUE,
                                                char* errBuf = NULL, int errBufSize = 0,
                                                char* firstCreatedDir = NULL, BOOL manualCrDir = FALSE) = 0;

    // checks free space on path and if not >= totalSize asks if user wants to continue;
    // question window has parent 'parent', returns TRUE if there is enough space or if user answered
    // "continue"; if 'parent' is not the current foreground window (dialog in inactive application),
    // FlashWindow(mainwnd, TRUE) is called before displaying dialog and after closing dialog
    // FlashWindow(mainwnd, FALSE) is called, mainwnd is the window in parent chain of 'parent'
    // that has no parent (typically the main Salamander window)
    // 'messageTitle' will be displayed in the title of the messagebox with question and should be
    // the name of the plugin that called the method
    // can be called from any thread
    virtual BOOL WINAPI TestFreeSpace(HWND parent, const char* path, const CQuadWord& totalSize,
                                      const char* messageTitle) = 0;

    // returns in 'retValue' (must not be NULL) free space on given path (currently the most correct
    // value obtainable from Windows, on NT/W2K/XP/Vista can work with reparse points
    // and substs (Salamander 2.5 works only with junction-points)); 'path' is the path where
    // we check free space (does not have to be root); if 'total' is not NULL, total disk size
    // is returned in it, on error returns CQuadWord(-1, -1)
    // can be called from any thread
    virtual void WINAPI GetDiskFreeSpace(CQuadWord* retValue, const char* path, CQuadWord* total) = 0;

    // custom clone of Windows GetDiskFreeSpace: can get correct values for paths containing
    // substs and reparse points under Windows 2000/XP/Vista/7 (Salamander 2.5 works only
    // with junction-points); 'path' is the path where we check free space; other parameters
    // correspond to standard Win32 API function GetDiskFreeSpace
    //
    // WARNING: do not use return values 'lpNumberOfFreeClusters' and 'lpTotalNumberOfClusters', because
    //          on larger disks they contain nonsense (DWORD may not be enough for total cluster count),
    //          use previous GetDiskFreeSpace method instead, which returns 64-bit numbers
    //
    // can be called from any thread
    virtual BOOL WINAPI SalGetDiskFreeSpace(const char* path, LPDWORD lpSectorsPerCluster,
                                            LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters,
                                            LPDWORD lpTotalNumberOfClusters) = 0;

    // custom clone of Windows GetVolumeInformation: can get correct values also for
    // paths containing substs and reparse points under Windows 2000/XP/Vista (Salamander 2.5
    // works only with junction-points); 'path' is the path for which we get information;
    // in 'rootOrCurReparsePoint' (if not NULL, must be at least MAX_PATH
    // characters large buffer) root directory or current (last) local reparse
    // point on path 'path' is returned (Salamander 2.5 returns path for which values were
    // successfully obtained or at least root directory); other parameters correspond to standard Win32 API
    // function GetVolumeInformation
    // can be called from any thread
    virtual BOOL WINAPI SalGetVolumeInformation(const char* path, char* rootOrCurReparsePoint, LPTSTR lpVolumeNameBuffer,
                                                DWORD nVolumeNameSize, LPDWORD lpVolumeSerialNumber,
                                                LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags,
                                                LPTSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize) = 0;

    // custom clone of Windows GetDriveType: can get correct values also for paths
    // containing substs and reparse points under Windows 2000/XP/Vista (Salamander 2.5
    // works only with junction-points); 'path' is the path whose type we check
    // can be called from any thread
    virtual UINT WINAPI SalGetDriveType(const char* path) = 0;

    // because Windows GetTempFileName doesn't work, we wrote our own clone:
    // creates file/directory (according to 'file') on path 'path' (NULL -> Windows TEMP dir),
    // with prefix 'prefix', returns name of created file in 'tmpName' (min. size MAX_PATH),
    // returns success (on failure returns Windows error code in 'err' (if not NULL))
    // can be called from any thread
    virtual BOOL WINAPI SalGetTempFileName(const char* path, const char* prefix, char* tmpName, BOOL file, DWORD* err) = 0;

    // removes directory including its contents (SHFileOperation is terribly slow)
    // can be called from any thread
    virtual void WINAPI RemoveTemporaryDir(const char* dir) = 0;

    // because Windows version of MoveFile cannot handle renaming file with read-only attribute on Novell,
    // we wrote our own (if error occurs during MoveFile, tries to remove read-only, perform operation,
    // and then set it again); returns success (on failure returns Windows error code in 'err' (if not NULL))
    // can be called from any thread
    virtual BOOL WINAPI SalMoveFile(const char* srcName, const char* destName, DWORD* err) = 0;

    // variant of Windows version GetFileSize (has simpler error handling); 'file' is open
    // file for calling GetFileSize(); in 'size' returns obtained file size; returns success,
    // on FALSE (error) 'err' contains Windows error code and 'size' is zero;
    // NOTE: there is variant SalGetFileSize2(), which works with full file name
    // can be called from any thread
    virtual BOOL WINAPI SalGetFileSize(HANDLE file, CQuadWord& size, DWORD& err) = 0;

    // opens file/directory 'name' on path 'path'; follows Windows associations, opens
    // via Open item in context menu (can also use salopen.exe, depends on configuration);
    // before starting sets current directories on local drives according to panels;
    // 'parent' is parent of any windows (e.g. when opening non-associated file)
    // limitation: main thread (otherwise salopen.exe wouldn't work - uses one shared memory)
    virtual void WINAPI ExecuteAssociation(HWND parent, const char* path, const char* name) = 0;

    // opens browse dialog where user selects path; 'parent' is parent of browse dialog;
    // 'hCenterWindow' - window to which dialog will be centered; 'title' is browse dialog title;
    // 'comment' is comment in browse dialog; 'path' is buffer for resulting path (min. MAX_PATH
    // characters); if 'onlyNet' is TRUE, only network paths can be browsed (otherwise no limit); if
    // 'initDir' is not NULL, contains path where browse dialog should open; returns TRUE if
    // 'path' contains new selected path
    // WARNING: if called outside main thread, COM must be initialized first (maybe better entire
    //          OLE - see CoInitialize or OLEInitialize)
    // can be called from any thread
    virtual BOOL WINAPI GetTargetDirectory(HWND parent, HWND hCenterWindow, const char* title,
                                           const char* comment, char* path, BOOL onlyNet,
                                           const char* initDir) = 0;

    // working with file masks: ('?' any char, '*' any string - including empty)
    // all can be called from any thread
    // 1) convert mask to simpler format (src -> mask buffer - min. size of
    //    buffer 'mask' is (strlen(src) + 1))
    virtual void WINAPI PrepareMask(char* mask, const char* src) = 0;
    // 2) use converted mask to test if file filename matches it,
    //    hasExtension = TRUE if file has extension
    //    returns TRUE if file matches mask
    virtual BOOL WINAPI AgreeMask(const char* filename, const char* mask, BOOL hasExtension) = 0;
    // 3) unmodified mask (do not call PrepareMask for it) can be used to create name from
    //    given name and mask ("a.txt" + "*.cpp" -> "a.cpp" etc.),
    //    buffer should be at least strlen(name)+strlen(mask) (2*MAX_PATH is suitable)
    //    returns created name (pointer 'buffer')
    virtual char* WINAPI MaskName(char* buffer, int bufSize, const char* name, const char* mask) = 0;

    // working with extended file masks: ('?' any char, '*' any string - including empty,
    // '#' any digit - '0'..'9')
    // all can be called from any thread
    // 1) convert mask to simpler format (src -> mask buffer - min. length strlen(src) + 1)
    virtual void WINAPI PrepareExtMask(char* mask, const char* src) = 0;
    // 2) use converted mask to test if file filename matches it,
    //    hasExtension = TRUE if file has extension
    //    returns TRUE if file matches mask
    virtual BOOL WINAPI AgreeExtMask(const char* filename, const char* mask, BOOL hasExtension) = 0;

    // allocates new object for working with file mask group
    // can be called from any thread
    virtual CSalamanderMaskGroup* WINAPI AllocSalamanderMaskGroup() = 0;

    // frees object for working with file mask group (obtained via AllocSalamanderMaskGroup method)
    // can be called from any thread
    virtual void WINAPI FreeSalamanderMaskGroup(CSalamanderMaskGroup* maskGroup) = 0;

    // memory allocation on Salamander heap (unnecessary when using salrtl9.dll - standard malloc is sufficient);
    // on insufficient memory, message is shown to user with buttons Retry (another allocation attempt),
    // Abort (after another prompt terminates application) and Ignore (passing allocation error to application - after
    // warning user that application may crash, Alloc returns NULL;
    // checking for NULL makes sense only for large memory blocks, e.g. more than 500 MB, where allocation
    // may fail due to address space fragmentation by loaded DLL libraries);
    // NOTE: Realloc() was added later, it's below in this module
    // can be called from any thread
    virtual void* WINAPI Alloc(int size) = 0;
    // memory deallocation from Salamander heap (unnecessary when using salrtl9.dll - standard free is sufficient)
    // can be called from any thread
    virtual void WINAPI Free(void* ptr) = 0;

    // string duplication - memory allocation (on Salamander heap - heap accessible via salrtl9.dll)
    // + string copy; if 'str'==NULL returns NULL;
    // can be called from any thread
    virtual char* WINAPI DupStr(const char* str) = 0;

    // returns mapping table for lowercase and uppercase letters (array of 256 characters - lowercase/uppercase letter at
    // index of queried letter); if 'lowerCase' is not NULL, lowercase table is returned in it;
    // if 'upperCase' is not NULL, uppercase table is returned in it
    // can be called from any thread
    virtual void WINAPI GetLowerAndUpperCase(unsigned char** lowerCase, unsigned char** upperCase) = 0;

    // converts string 'str' to lowercase/uppercase; unlike ANSI C tolower/toupper works
    // directly with string and supports not only characters 'A' to 'Z' (lowercase conversion uses
    // array initialized by Win32 API function CharLower)
    virtual void WINAPI ToLowerCase(char* str) = 0;
    virtual void WINAPI ToUpperCase(char* str) = 0;

    //*****************************************************************************
    //
    // StrCmpEx
    //
    // Function compares two substrings.
    // If the two substrings are of different lengths, they are compared up to the
    // length of the shortest one. If they are equal to that point, then the return
    // value will indicate that the longer string is greater.
    //
    // Parameters
    //   s1, s2: strings to compare
    //   l1    : compared length of s1 (must be less or equal to strlen(s1))
    //   l2    : compared length of s2 (must be less or equal to strlen(s1))
    //
    // Return Values
    //   -1 if s1 < s2 (if substring pointed to by s1 is less than the substring pointed to by s2)
    //    0 if s1 = s2 (if the substrings are equal)
    //   +1 if s1 > s2 (if substring pointed to by s1 is greater than the substring pointed to by s2)
    //
    // Method can be called from any thread.
    virtual int WINAPI StrCmpEx(const char* s1, int l1, const char* s2, int l2) = 0;

    //*****************************************************************************
    //
    // StrICpy
    //
    // Function copies characters from source to destination. Upper case letters are mapped to
    // lower case using LowerCase array (filled using CharLower Win32 API call).
    //
    // Parameters
    //   dest: pointer to the destination string
    //   src: pointer to the null-terminated source string
    //
    // Return Values
    //   The StrICpy returns the number of bytes stored in buffer, not counting
    //   the terminating null character.
    //
    // Method can be called from any thread.
    virtual int WINAPI StrICpy(char* dest, const char* src) = 0;

    //*****************************************************************************
    //
    // StrICmp
    //
    // Function compares two strings. The comparison is not case sensitive and ignores
    // regional settings. For the purposes of the comparison, all characters are converted
    // to lower case using LowerCase array (filled using CharLower Win32 API call).
    //
    // Parameters
    //   s1, s2: null-terminated strings to compare
    //
    // Return Values
    //   -1 if s1 < s2 (if string pointed to by s1 is less than the string pointed to by s2)
    //    0 if s1 = s2 (if the strings are equal)
    //   +1 if s1 > s2 (if string pointed to by s1 is greater than the string pointed to by s2)
    //
    // Method can be called from any thread.
    virtual int WINAPI StrICmp(const char* s1, const char* s2) = 0;

    //*****************************************************************************
    //
    // StrICmpEx
    //
    // Function compares two substrings. The comparison is not case sensitive and ignores
    // regional settings. For the purposes of the comparison, all characters are converted
    // to lower case using LowerCase array (filled using CharLower Win32 API call).
    // If the two substrings are of different lengths, they are compared up to the
    // length of the shortest one. If they are equal to that point, then the return
    // value will indicate that the longer string is greater.
    //
    // Parameters
    //   s1, s2: strings to compare
    //   l1    : compared length of s1 (must be less or equal to strlen(s1))
    //   l2    : compared length of s2 (must be less or equal to strlen(s2))
    //
    // Return Values
    //   -1 if s1 < s2 (if substring pointed to by s1 is less than the substring pointed to by s2)
    //    0 if s1 = s2 (if the substrings are equal)
    //   +1 if s1 > s2 (if substring pointed to by s1 is greater than the substring pointed to by s2)
    //
    // Method can be called from any thread.
    virtual int WINAPI StrICmpEx(const char* s1, int l1, const char* s2, int l2) = 0;

    //*****************************************************************************
    //
    // StrNICmp
    //
    // Function compares two strings. The comparison is not case sensitive and ignores
    // regional settings. For the purposes of the comparison, all characters are converted
    // to lower case using LowerCase array (filled using CharLower Win32 API call).
    // The comparison stops after: (1) a difference between the strings is found,
    // (2) the end of the string is reached, or (3) n characters have been compared.
    //
    // Parameters
    //   s1, s2: strings to compare
    //   n:      maximum length to compare
    //
    // Return Values
    //   -1 if s1 < s2 (if substring pointed to by s1 is less than the substring pointed to by s2)
    //    0 if s1 = s2 (if the substrings are equal)
    //   +1 if s1 > s2 (if substring pointed to by s1 is greater than the substring pointed to by s2)
    //
    // Method can be called from any thread.
    virtual int WINAPI StrNICmp(const char* s1, const char* s2, int n) = 0;

    //*****************************************************************************
    //
    // MemICmp
    //
    // Compares n bytes of the two blocks of memory stored at buf1 and buf2.
    // Characters are converted to lowercase before comparing (not permanently;
    // using LowerCase array which was filled using CharLower Win32 API call),
    // so case is ignored in comparation.
    //
    // Parameters
    //   buf1, buf2: memory buffers to compare
    //   n:          maximum length to compare
    //
    // Return Values
    //   -1 if buf1 < buf2 (if buffer pointed to by buf1 is less than the buffer pointed to by buf2)
    //    0 if buf1 = buf2 (if the buffers are equal)
    //   +1 if buf1 > buf2 (if buffer pointed to by buf1 is greater than the buffer pointed to by buf2)
    //
    // Method can be called from any thread.
    virtual int WINAPI MemICmp(const void* buf1, const void* buf2, int n) = 0;

    // compares two strings 's1' and 's2' case-insensitively (ignore-case),
    // if SALCFG_SORTUSESLOCALE is TRUE, uses sorting according to Windows regional settings,
    // otherwise compares same as CSalamanderGeneral::StrICmp, if SALCFG_SORTDETECTNUMBERS
    // is TRUE, uses numerical sorting for numbers contained in strings
    // returns <0 ('s1' < 's2'), ==0 ('s1' == 's2'), >0 ('s1' > 's2')
    virtual int WINAPI RegSetStrICmp(const char* s1, const char* s2) = 0;

    // compares two strings 's1' and 's2' (of lengths 'l1' and 'l2') case-insensitively
    // (ignore-case), if SALCFG_SORTUSESLOCALE is TRUE, uses sorting according to
    // Windows regional settings, otherwise compares same as CSalamanderGeneral::StrICmp,
    // if SALCFG_SORTDETECTNUMBERS is TRUE, uses numerical sorting for numbers contained
    // in strings; in 'numericalyEqual' (if not NULL) returns TRUE if strings are
    // numerically equal (e.g. "a01" and "a1"), is automatically TRUE if strings are equal
    // returns <0 ('s1' < 's2'), ==0 ('s1' == 's2'), >0 ('s1' > 's2')
    virtual int WINAPI RegSetStrICmpEx(const char* s1, int l1, const char* s2, int l2,
                                       BOOL* numericalyEqual) = 0;

    // compares (case-sensitive) two strings 's1' and 's2', if SALCFG_SORTUSESLOCALE is TRUE,
    // uses sorting according to Windows regional settings, otherwise compares same as
    // standard C library function strcmp, if SALCFG_SORTDETECTNUMBERS is TRUE, uses
    // numerical sorting for numbers contained in strings
    // returns <0 ('s1' < 's2'), ==0 ('s1' == 's2'), >0 ('s1' > 's2')
    virtual int WINAPI RegSetStrCmp(const char* s1, const char* s2) = 0;

    // compares (case-sensitive) two strings 's1' and 's2' (of lengths 'l1' and 'l2'), if
    // SALCFG_SORTUSESLOCALE is TRUE, uses sorting according to Windows regional settings,
    // otherwise compares same as standard C library function strcmp, if
    // SALCFG_SORTDETECTNUMBERS is TRUE, uses numerical sorting for numbers contained in strings;
    // in 'numericalyEqual' (if not NULL) returns TRUE if strings are numerically equal
    // (e.g. "a01" and "a1"), is automatically TRUE if strings are equal
    // returns <0 ('s1' < 's2'), ==0 ('s1' == 's2'), >0 ('s1' > 's2')
    virtual int WINAPI RegSetStrCmpEx(const char* s1, int l1, const char* s2, int l2,
                                      BOOL* numericalyEqual) = 0;

    // returns path in panel; 'panel' is one of PANEL_XXX; 'buffer' is buffer for path (can
    // be NULL); 'bufferSize' is size of buffer 'buffer' (if 'buffer' is NULL, must be
    // zero); 'type' if not NULL points to variable where path type is stored
    // (see PATH_TYPE_XXX); if it's an archive and 'archiveOrFS' is not NULL and 'buffer' is not NULL,
    // 'archiveOrFS' is returned set to 'buffer' at position after archive file;
    // if it's a file-system and 'archiveOrFS' is not NULL and 'buffer' is not NULL,
    // 'archiveOrFS' is returned set to 'buffer' at ':' after file-system name (after ':' is user-part
    // of file-system path); if 'convertFSPathToExternal' is TRUE and panel path is on FS,
    // plugin whose path it is (by fs-name) is found and its
    // CPluginInterfaceForFSAbstract::ConvertPathToExternal() is called; returns success (if
    // 'bufferSize'!=0, it's also considered failure if path doesn't fit in buffer
    // 'buffer')
    // limitation: main thread
    virtual BOOL WINAPI GetPanelPath(int panel, char* buffer, int bufferSize, int* type,
                                     char** archiveOrFS, BOOL convertFSPathToExternal = FALSE) = 0;

    // returns last visited Windows path in panel, useful for returns from FS (more pleasant than
    // going directly to fixed-drive); 'panel' is one of PANEL_XXX; 'buffer' is buffer for path;
    // 'bufferSize' is size of buffer 'buffer'; returns success
    // limitation: main thread
    virtual BOOL WINAPI GetLastWindowsPanelPath(int panel, char* buffer, int bufferSize) = 0;

    // returns FS name assigned "for lifetime" to plugin by Salamander (according to proposal from SetBasicPluginData);
    // 'buf' is buffer of at least MAX_PATH characters; 'fsNameIndex' is fs-name index (index is
    // zero for fs-name specified in CSalamanderPluginEntryAbstract::SetBasicPluginData, for others
    // fs-name index is returned by CSalamanderPluginEntryAbstract::AddFSName)
    // limitation: main thread (otherwise plugin configuration may change during call),
    // in entry-point can be called only after SetBasicPluginData, may not be known earlier
    virtual void WINAPI GetPluginFSName(char* buf, int fsNameIndex) = 0;

    // returns interface of plugin file-system (FS) opened in panel 'panel' (one of PANEL_XXX);
    // if no FS is opened in panel or it's FS of another plugin (doesn't belong to calling plugin),
    // method returns NULL (cannot work with object of another plugin, its structure is unknown)
    // limitation: main thread
    virtual CPluginFSInterfaceAbstract* WINAPI GetPanelPluginFS(int panel) = 0;

    // returns plugin data interface of panel listing (can be NULL), 'panel' is one of PANEL_XXX;
    // if plugin data interface exists but doesn't belong to this (calling) plugin,
    // method returns NULL (cannot work with object of another plugin, its structure is unknown)
    // limitation: main thread
    virtual CPluginDataInterfaceAbstract* WINAPI GetPanelPluginData(int panel) = 0;

    // returns focused item in panel (file/directory/updir("..")), 'panel' is one of PANEL_XXX,
    // returns NULL (no item in panel) or data of focused item; if 'isDir' is not NULL,
    // returns FALSE in it if it's a file (otherwise it's a directory or updir)
    // WARNING: returned item data are read-only
    // limitation: main thread
    virtual const CFileData* WINAPI GetPanelFocusedItem(int panel, BOOL* isDir) = 0;

    // returns panel items sequentially (first directories, then files), 'panel' is one of PANEL_XXX,
    // 'index' is input/output variable, points to int which is 0 on first call,
    // function stores value for next call on return (usage: zero at start, then
    // don't change), returns NULL (no more items) or data of next (or first) item;
    // if 'isDir' is not NULL, returns FALSE in it if it's a file (otherwise it's a directory or updir)
    // WARNING: returned item data are read-only
    // limitation: main thread
    virtual const CFileData* WINAPI GetPanelItem(int panel, int* index, BOOL* isDir) = 0;

    // returns selected panel items sequentially (first directories, then files), 'panel' is one of
    // PANEL_XXX, 'index' is input/output variable, points to int which is 0 on first call,
    // function stores value for next call on return (usage: zero at start, then
    // don't change), returns NULL (no more items) or data of next (or first) item;
    // if 'isDir' is not NULL, returns FALSE in it if it's a file (otherwise it's a directory or updir)
    // WARNING: returned item data are read-only
    // limitation: main thread
    virtual const CFileData* WINAPI GetPanelSelectedItem(int panel, int* index, BOOL* isDir) = 0;

    // finds how many files and directories are selected in panel; 'panel' is one of PANEL_XXX;
    // if 'selectedFiles' is not NULL, returns number of selected files in it; if 'selectedDirs' is
    // not NULL, returns number of selected directories in it; returns TRUE if at least one
    // file or directory is selected or if focus is on file or directory (if there is something
    // to work with - focus is not on up-dir)
    // limitation: main thread (otherwise panel contents may change)
    virtual BOOL WINAPI GetPanelSelection(int panel, int* selectedFiles, int* selectedDirs) = 0;

    // returns top-index of listbox in panel; 'panel' is one of PANEL_XXX
    // limitation: main thread (otherwise panel contents may change)
    virtual int WINAPI GetPanelTopIndex(int panel) = 0;

    // informs Salamander main window that viewer window is being deactivated, if main window
    // will be activated immediately and panels have non-automatically refreshed
    // drives, they won't be refreshed (viewers don't change disk contents), optional (may cause
    // unnecessary refresh)
    // can be called from any thread
    virtual void WINAPI SkipOneActivateRefresh() = 0;

    // selects/deselects panel item, 'file' is pointer to changed item obtained by previous
    // "get-item" call (methods GetPanelFocusedItem, GetPanelItem and GetPanelSelectedItem);
    // plugin must not be left since "get-item" call and call must occur in main
    // thread (to prevent panel refresh - pointer invalidation); 'panel' must be same
    // as 'panel' parameter of corresponding "get-item" call; if 'select' is TRUE selection occurs,
    // otherwise deselection occurs; after last call RepaintChangedItems('panel') must be used for
    // panel repaint
    // limitation: main thread
    virtual void WINAPI SelectPanelItem(int panel, const CFileData* file, BOOL select) = 0;

    // repaints panel items that were changed (selection); 'panel' is
    // one of PANEL_XXX
    // limitation: main thread
    virtual void WINAPI RepaintChangedItems(int panel) = 0;

    // selects/deselects all items in panel, 'panel' is one of PANEL_XXX; if 'select' is
    // TRUE selection occurs, otherwise deselection occurs; if 'repaint' is TRUE all changed
    // items in panel are repainted, otherwise no repaint occurs (can call RepaintChangedItems later)
    // limitation: main thread
    virtual void WINAPI SelectAllPanelItems(int panel, BOOL select, BOOL repaint) = 0;

    // sets focus in panel, 'file' is pointer to focused item obtained by previous
    // "get-item" call (methods GetPanelFocusedItem, GetPanelItem and GetPanelSelectedItem);
    // plugin must not be left since "get-item" call and call must occur in main
    // thread (to prevent panel refresh - pointer invalidation); 'panel' must be same
    // as 'panel' parameter of corresponding "get-item" call; if 'partVis' is TRUE and item will
    // be only partially visible, panel won't scroll on focus, if FALSE panel scrolls
    // so that entire item is visible
    // limitation: main thread
    virtual void WINAPI SetPanelFocusedItem(int panel, const CFileData* file, BOOL partVis) = 0;

    // finds if filter is used in panel and if so, gets mask string of
    // this filter; 'panel' indicates panel we're interested in (one of PANEL_XXX);
    // 'masks' is buffer for filter masks of at least 'masksBufSize' bytes (recommended
    // size is MAX_GROUPMASK); returns TRUE if filter is used and buffer 'masks' is
    // large enough; returns FALSE if filter is not used or mask string didn't fit
    // in 'masks'
    // limitation: main thread
    virtual BOOL WINAPI GetFilterFromPanel(int panel, char* masks, int masksBufSize) = 0;

    // returns position of source panel (is it left or right?), returns PANEL_LEFT or PANEL_RIGHT
    // limitation: main thread
    virtual int WINAPI GetSourcePanel() = 0;

    // finds in which panel 'pluginFS' is opened; if not in either panel,
    // returns FALSE; if returns TRUE, panel number is in 'panel' (PANEL_LEFT or PANEL_RIGHT)
    // limitation: main thread (otherwise panel contents may change)
    virtual BOOL WINAPI GetPanelWithPluginFS(CPluginFSInterfaceAbstract* pluginFS, int& panel) = 0;

    // activates other panel (like TAB key); panels marked via PANEL_SOURCE and PANEL_TARGET
    // are naturally swapped
    // limitation: main thread
    virtual void WINAPI ChangePanel() = 0;

    // converts number to "more readable" string (space every three digits), returns string in
    // 'buffer' (min. size 50 bytes), returns 'buffer'
    // can be called from any thread
    virtual char* WINAPI NumberToStr(char* buffer, const CQuadWord& number) = 0;

    // prints disk size to 'buf' (min. buffer size is 100 bytes),
    // mode==0 "1.23 MB", mode==1 "1 230 000 bytes, 1.23 MB", mode==2 "1 230 000 bytes",
    // mode==3 "12 KB" (always in whole kilobytes), mode==4 (like mode==0, but always
    // at least 3 significant digits, e.g. "2.00 MB")
    // returns 'buf'
    // can be called from any thread
    virtual char* WINAPI PrintDiskSize(char* buf, const CQuadWord& size, int mode) = 0;

    // converts number of seconds to string ("5 sec", "1 hr 34 min", etc.); 'buf' is
    // buffer for result text, must be at least 100 characters; 'secs' is number of seconds;
    // returns 'buf'
    // can be called from any thread
    virtual char* WINAPI PrintTimeLeft(char* buf, const CQuadWord& secs) = 0;

    // compares root of normal (c:\path) and UNC (\\server\share\path) paths, returns TRUE if root is same
    // can be called from any thread
    virtual BOOL WINAPI HasTheSameRootPath(const char* path1, const char* path2) = 0;

    // Returns number of characters in common path. On normal path root must be terminated with backslash,
    // otherwise function returns 0. ("C:\"+"C:"->0, "C:\A\B"+"C:\"->3, "C:\A\B\"+"C:\A"->4,
    // "C:\AA\BB"+"C:\AA\CC"->5)
    // Works for normal and UNC paths.
    virtual int WINAPI CommonPrefixLength(const char* path1, const char* path2) = 0;

    // Returns TRUE if path 'prefix' is base of path 'path'. Otherwise returns FALSE.
    // "C:\aa","C:\Aa\BB"->TRUE
    // "C:\aa","C:\aaa"->FALSE
    // "C:\aa\","C:\Aa"->TRUE
    // "\\server\share","\\server\share\aaa"->TRUE
    // Works for normal and UNC paths.
    virtual BOOL WINAPI PathIsPrefix(const char* prefix, const char* path) = 0;

    // compares two normal (c:\path) and UNC (\\server\share\path) paths, ignores case,
    // also ignores one backslash at beginning and end of paths, returns TRUE if paths are same
    // can be called from any thread
    virtual BOOL WINAPI IsTheSamePath(const char* path1, const char* path2) = 0;

    // gets root path from normal (c:\path) or UNC (\\server\share\path) path 'path', in 'root' returns
    // path in format 'c:\' or '\\server\share\' (min. size of 'root' buffer is MAX_PATH),
    // returns number of characters in root path (without null-terminator); for UNC root path longer than MAX_PATH
    // truncation to MAX_PATH-2 characters occurs with backslash added (it's not 100% a root path anyway)
    // can be called from any thread
    virtual int WINAPI GetRootPath(char* root, const char* path) = 0;

    // shortens normal (c:\path) or UNC (\\server\share\path) path by last directory
    // (cuts at last backslash - backslash remains at end of trimmed path
    // only for 'c:\'), 'path' is in/out buffer (min. size strlen(path)+2 bytes),
    // in 'cutDir' (if not NULL) pointer is returned (into buffer 'path' after 1st null-terminator)
    // to last directory (cut part), this method replaces PathRemoveFileSpec,
    // returns TRUE if shortening occurred (was not root path)
    // can be called from any thread
    virtual BOOL WINAPI CutDirectory(char* path, char** cutDir = NULL) = 0;

    // works with normal (c:\path) and UNC (\\server\share\path) paths,
    // joins 'path' and 'name' into 'path', ensures joining with backslash, 'path' is buffer of at least
    // 'pathSize' characters, returns TRUE if 'name' fit after 'path'; if 'path' or 'name' is
    // empty, joining (initial/terminating) backslash won't be added (e.g. "c:\" + "" -> "c:")
    // can be called from any thread
    virtual BOOL WINAPI SalPathAppend(char* path, const char* name, int pathSize) = 0;

    // works with normal (c:\path) and UNC (\\server\share\path) paths,
    // if 'path' doesn't end with backslash yet, adds it to end of 'path'; 'path' is buffer
    // of at least 'pathSize' characters; returns TRUE if backslash fit after 'path'; if 'path'
    // is empty, backslash is not added
    // can be called from any thread
    virtual BOOL WINAPI SalPathAddBackslash(char* path, int pathSize) = 0;

    // works with normal (c:\path) and UNC (\\server\share\path) paths,
    // if 'path' ends with backslash, removes it
    // can be called from any thread
    virtual void WINAPI SalPathRemoveBackslash(char* path) = 0;

    // works with normal (c:\path) and UNC (\\server\share\path) paths,
    // makes name from full name ("c:\path\file" -> "file")
    // can be called from any thread
    virtual void WINAPI SalPathStripPath(char* path) = 0;

    // works with normal (c:\path) and UNC (\\server\share\path) paths,
    // if name has extension, removes it
    // can be called from any thread
    virtual void WINAPI SalPathRemoveExtension(char* path) = 0;

    // works with normal (c:\path) and UNC (\\server\share\path) paths,
    // if name 'path' doesn't have extension yet, adds extension 'extension' (e.g. ".txt"),
    // 'path' is buffer of at least 'pathSize' characters, returns FALSE if buffer 'path' isn't enough
    // for resulting path
    // can be called from any thread
    virtual BOOL WINAPI SalPathAddExtension(char* path, const char* extension, int pathSize) = 0;

    // works with normal (c:\path) and UNC (\\server\share\path) paths,
    // changes/adds extension 'extension' (e.g. ".txt") in name 'path', 'path' is buffer
    // of at least 'pathSize' characters, returns FALSE if buffer 'path' isn't enough for resulting path
    // can be called from any thread
    virtual BOOL WINAPI SalPathRenameExtension(char* path, const char* extension, int pathSize) = 0;

    // works with normal (c:\path) and UNC (\\server\share\path) paths,
    // returns pointer into 'path' to file/directory name (ignores backslash at end of 'path'),
    // if name contains no other backslashes except at end of string, returns 'path'
    // can be called from any thread
    virtual const char* WINAPI SalPathFindFileName(const char* path) = 0;

    // adjusts relative or absolute normal (c:\path) or UNC (\\server\share\path) path
    // to absolute without '.', '..' and trailing backslash (except for "c:\" type); if 'curDir' is NULL,
    // relative paths like "\path" and "path" return error (indeterminate), otherwise 'curDir' is valid
    // adjusted current path (UNC and normal); current paths of other drives (except
    // 'curDir' + only normal, not UNC) are in Salamander's DefaultDir array (before use
    // it's good to call SalUpdateDefaultDir method); 'name' - in/out path buffer of at least 'nameBufSize'
    // characters; if 'nextFocus' is not NULL and given relative path doesn't contain backslash,
    // strcpy(nextFocus, name) is performed; returns TRUE - name 'name' is ready for use, otherwise if
    // 'errTextID' is not NULL it contains error (GFN_XXX constants - text can be obtained via GetGFNErrorText)
    // WARNING: before use it's good to call SalUpdateDefaultDir method
    // limitation: main thread (otherwise DefaultDir changes may occur in main thread)
    virtual BOOL WINAPI SalGetFullName(char* name, int* errTextID = NULL, const char* curDir = NULL,
                                       char* nextFocus = NULL, int nameBufSize = MAX_PATH) = 0;

    // refreshes Salamander's DefaultDir array according to panel paths, if 'activePrefered' is TRUE,
    // path in active panel will have priority (written later to DefaultDir), otherwise
    // path in inactive panel has priority
    // limitation: main thread (otherwise DefaultDir changes may occur in main thread)
    virtual void WINAPI SalUpdateDefaultDir(BOOL activePrefered) = 0;

    // returns text representation of GFN_XXX error constant; returns 'buf' (so GetGFNErrorText can be passed
    // directly as function parameter)
    // can be called from any thread
    virtual char* WINAPI GetGFNErrorText(int GFN, char* buf, int bufSize) = 0;

    // returns text representation of system error (ERROR_XXX) in buffer 'buf' of size 'bufSize';
    // returns 'buf' (so GetErrorText can be passed directly as function parameter); 'buf' can be NULL or
    // 'bufSize' 0, in that case returns text in internal buffer (text may change due to change of
    // internal buffer caused by subsequent GetErrorText calls from other plugins or Salamander;
    // buffer is dimensioned for at least 10 texts, only then overwrite may occur; if you need text
    // for later use, we recommend copying it to local buffer of size MAX_PATH + 20)
    // can be called from any thread
    virtual char* WINAPI GetErrorText(int err, char* buf = NULL, int bufSize = 0) = 0;

    // returns internal Salamander color, 'color' is color constant (see SALCOL_XXX)
    // can be called from any thread
    virtual COLORREF WINAPI GetCurrentColor(int color) = 0;

    // ensures activation of Salamander main window + focus of file/directory 'name' on path
    // 'path' in panel 'panel'; changes path in panel if needed; 'panel' is one
    // of PANEL_XXX; 'path' is any path (Windows (disk), FS or archive);
    // 'name' can be empty string if nothing should be focused;
    // limitation: main thread + outside CPluginFSInterfaceAbstract and CPluginDataInterfaceAbstract methods
    // (e.g. FS opened in panel may close - method's 'this' could cease to exist)
    virtual void WINAPI FocusNameInPanel(int panel, const char* path, const char* name) = 0;

    // changes path in panel - input can be absolute or relative UNC (\\server\share\path)
    // or normal (c:\path) path, both Windows (disk), archive or FS path
    // (absolute/relative is resolved by plugin); if input is path to file,
    // this file is focused; if suggestedTopIndex is not -1, top-index in panel
    // is set; if suggestedFocusName is not NULL, item of same name is found (ignore-case) and focused;
    // if 'failReason' is not NULL, it's set to one of CHPPFR_XXX constants
    // (informs about method result); if 'convertFSPathToInternal' is TRUE and path is
    // FS path, plugin whose path it is (by fs-name) is found and its
    // CPluginInterfaceForFSAbstract::ConvertPathToInternal() is called; returns TRUE if requested
    // path was successfully listed;
    // NOTE: when FS path is specified, attempt to open path is made in this order: in FS
    // in panel, in disconnected FS, or in new FS (for panel FS and disconnected FS it's checked
    // if plugin-fs-name matches and if FS IsOurPath method returns TRUE for given path);
    // limitation: main thread + outside CPluginFSInterfaceAbstract and CPluginDataInterfaceAbstract methods
    // (e.g. FS opened in panel may close - method's 'this' could cease to exist)
    virtual BOOL WINAPI ChangePanelPath(int panel, const char* path, int* failReason = NULL,
                                        int suggestedTopIndex = -1,
                                        const char* suggestedFocusName = NULL,
                                        BOOL convertFSPathToInternal = TRUE) = 0;

    // changes path in panel to relative or absolute UNC (\\server\share\path) or normal (c:\path)
    // path, if new path is not accessible, tries to succeed with shortened paths; if it's path change
    // within one disk (including archive on this disk) and accessible path cannot be found on disk,
    // changes path to root of first local fixed drive (high chance of success, panel won't stay empty);
    // when translating relative to absolute path, path in panel 'panel' is preferred
    // (only if it's disk path (including archive), otherwise not used); 'panel' is
    // one of PANEL_XXX; 'path' is new path; if 'suggestedTopIndex' is not -1, it will be set as
    // top-index in panel (only for new path, not set on shortened (changed) path); if
    // 'suggestedFocusName' is not NULL, item of same name is found (ignore-case) and focused
    // (only for new path, not done on shortened (changed) path); if 'failReason' is not NULL,
    // it's set to one of CHPPFR_XXX constants (informs about method result); returns TRUE if
    // requested path was successfully listed (not shortened/changed)
    // limitation: main thread + outside CPluginFSInterfaceAbstract and CPluginDataInterfaceAbstract methods
    // (e.g. FS opened in panel may close - method's 'this' could cease to exist)
    virtual BOOL WINAPI ChangePanelPathToDisk(int panel, const char* path, int* failReason = NULL,
                                              int suggestedTopIndex = -1,
                                              const char* suggestedFocusName = NULL) = 0;

    // changes path in panel to archive, 'archive' is relative or absolute UNC
    // (\\server\share\path\file) or normal (c:\path\file) archive name, 'archivePath' is path
    // inside archive, if new path in archive is not accessible, tries to succeed with shortened paths;
    // when translating relative to absolute path, path in panel 'panel' is preferred
    // (only if it's disk path (including archive), otherwise not used); 'panel' is one of PANEL_XXX;
    // if 'suggestedTopIndex' is not -1, it will be set as top-index in panel (only for new
    // path, not set on shortened (changed) path); if 'suggestedFocusName' is not NULL,
    // item of same name is found (ignore-case) and focused (only for new path,
    // not done on shortened (changed) path); if 'forceUpdate' is TRUE and path change is made
    // inside archive 'archive' (archive is already open in panel), archive file change test is performed
    // (size & time check) and if changed, archive is closed (risk of updating edited
    // files) and re-listed or if file ceased to exist, path is changed to disk
    // (archive closed; if disk path is not accessible, changes path to root of first local
    // fixed drive); if 'forceUpdate' is FALSE, path changes inside archive 'archive' are made without
    // archive file check; if 'failReason' is not NULL, it's set to one of CHPPFR_XXX constants
    // (informs about method result); returns TRUE if requested
    // path was successfully listed (not shortened/changed)
    // limitation: main thread + outside CPluginFSInterfaceAbstract and CPluginDataInterfaceAbstract methods
    // (e.g. FS opened in panel may close - method's 'this' could cease to exist)
    virtual BOOL WINAPI ChangePanelPathToArchive(int panel, const char* archive, const char* archivePath,
                                                 int* failReason = NULL, int suggestedTopIndex = -1,
                                                 const char* suggestedFocusName = NULL,
                                                 BOOL forceUpdate = FALSE) = 0;

    // changes path in panel to plugin FS, 'fsName' is FS name (see GetPluginFSName; doesn't have to be
    // from this plugin), 'fsUserPart' is path within FS; if new path in FS is not
    // accessible, tries to succeed with shortened paths (repeated ChangePath and ListCurrentPath calls,
    // see CPluginFSInterfaceAbstract); if it's path change within current FS (see
    // CPluginFSInterfaceAbstract::IsOurPath) and accessible path cannot be found from new path,
    // tries to find accessible path from original (current) path, and if that fails too,
    // changes path to root of first local fixed drive (high chance of success, panel won't stay empty);
    // 'panel' is one of PANEL_XXX; if 'suggestedTopIndex' is not -1, it will be set as top-index
    // in panel (only for new path, not set on shortened (changed) path); if
    // 'suggestedFocusName' is not NULL, item of same name is found (ignore-case) and focused
    // (only for new path, not done on shortened (changed) path); if 'forceUpdate' is TRUE,
    // the case of path change to current path in panel is not optimized (path is listed normally)
    // (either new path matches current path directly or it was shortened to it by first ChangePath);
    // if 'convertPathToInternal' is TRUE, plugin is found by 'fsName' and its method
    // CPluginInterfaceForFSAbstract::ConvertPathToInternal() is called for 'fsUserPart';
    // if 'failReason' is not NULL, it is set to one of CHPPFR_XXX constants (informs
    // about method result); returns TRUE if requested path was successfully listed
    // (not shortened/not changed)
    // NOTE: if you need the FS path to be tried to open also in detached FS, use method
    // ChangePanelPath (ChangePanelPathToPluginFS ignores detached FS - works only with FS opened
    // in panel or opens new FS);
    // limitation: main thread + outside methods CPluginFSInterfaceAbstract and CPluginDataInterfaceAbstract
    // (there's risk e.g. of closing FS opened in panel - 'this' could cease to exist for the method)
    virtual BOOL WINAPI ChangePanelPathToPluginFS(int panel, const char* fsName, const char* fsUserPart,
                                                  int* failReason = NULL, int suggestedTopIndex = -1,
                                                  const char* suggestedFocusName = NULL,
                                                  BOOL forceUpdate = FALSE,
                                                  BOOL convertPathToInternal = FALSE) = 0;

    // changes path in panel to detached plugin FS (see FSE_DETACHED/FSE_ATTACHED),
    // 'detachedFS' is detached plugin FS; if current path in detached FS is not accessible,
    // tries to succeed with shortened paths (repeated ChangePath and ListCurrentPath calls, see
    // CPluginFSInterfaceAbstract); 'panel' is one of PANEL_XXX; if 'suggestedTopIndex' is not -1,
    // it will be set as top-index in panel (only for new path, not set on shortened (changed) path);
    // if 'suggestedFocusName' is not NULL, item of same name is found (ignore-case) and focused
    // (only for new path, not done on shortened (changed) path);
    // if 'failReason' is not NULL, it is set to one of CHPPFR_XXX constants (informs about method
    // result); returns TRUE if requested path was successfully listed (not shortened/not changed)
    // limitation: main thread + outside methods CPluginFSInterfaceAbstract and CPluginDataInterfaceAbstract
    // (there's risk e.g. of closing FS opened in panel - 'this' could cease to exist for the method)
    virtual BOOL WINAPI ChangePanelPathToDetachedFS(int panel, CPluginFSInterfaceAbstract* detachedFS,
                                                    int* failReason = NULL, int suggestedTopIndex = -1,
                                                    const char* suggestedFocusName = NULL) = 0;

    // changes path in panel to root of first local fixed drive, this is almost certain change
    // of current path in panel; 'panel' is one of PANEL_XXX; if 'failReason' is not NULL,
    // it is set to one of CHPPFR_XXX constants (informs about method result); returns
    // TRUE if root of first local fixed drive was successfully listed
    // limitation: main thread + outside methods CPluginFSInterfaceAbstract and CPluginDataInterfaceAbstract
    // (there's risk e.g. of closing FS opened in panel - 'this' could cease to exist for the method)
    virtual BOOL WINAPI ChangePanelPathToFixedDrive(int panel, int* failReason = NULL) = 0;

    // refreshes path in panel (reloads listing and transfers selection, icons, focus, etc.
    // to new panel content); disk and FS paths are always reloaded, archive paths
    // are reloaded only if archive file changed (size & time check) or if
    // 'forceRefresh' is TRUE; thumbnails on disk paths are reloaded only when file size changes
    // or date/time of last write changes or if 'forceRefresh' is TRUE; 'panel' is one of PANEL_XXX;
    // if 'focusFirstNewItem' is TRUE and only one item was added to panel, this new item is focused
    // (used e.g. for focusing newly created file/directory)
    // limitation: main thread and also outside methods CPluginFSInterfaceAbstract and
    // CPluginDataInterfaceAbstract (there's risk e.g. of closing FS opened in panel - 'this'
    // could cease to exist for the method)
    virtual void WINAPI RefreshPanelPath(int panel, BOOL forceRefresh = FALSE,
                                         BOOL focusFirstNewItem = FALSE) = 0;

    // posts message to panel that path should be refreshed (reloads listing and
    // transfers selection, icons, focus, etc. to new panel content); refresh is performed when
    // main Salamander window is activated (when suspend-mode ends); disk
    // and FS paths are always reloaded, archive paths are reloaded only if archive file
    // changed (size & time check); 'panel' is one of PANEL_XXX; if
    // 'focusFirstNewItem' is TRUE and only one item was added to panel, this new item is focused
    // (used e.g. for focusing newly created file/directory)
    // can be called from any thread (if main thread is not executing code inside plugin,
    // refresh happens as soon as possible, otherwise refresh waits at least until main
    // thread leaves the plugin)
    virtual void WINAPI PostRefreshPanelPath(int panel, BOOL focusFirstNewItem = FALSE) = 0;

    // posts message to panel with active FS 'modifiedFS' that path should be
    // refreshed (reloads listing and transfers selection, icons, focus, etc. to
    // new panel content); refresh is performed when main Salamander window is activated
    // (when suspend-mode ends); FS path is always reloaded; if 'modifiedFS' is not in any
    // panel, nothing happens; if 'focusFirstNewItem' is TRUE and only one item was added to panel,
    // this new item is focused (used e.g. for focusing newly created file/directory);
    // NOTE: there's also PostRefreshPanelFS2, which returns TRUE if refresh was performed,
    // FALSE if 'modifiedFS' was not found in any panel;
    // can be called from any thread (if main thread is not executing code inside plugin,
    // refresh happens as soon as possible, otherwise refresh waits at least until main
    // thread leaves the plugin)
    virtual void WINAPI PostRefreshPanelFS(CPluginFSInterfaceAbstract* modifiedFS,
                                           BOOL focusFirstNewItem = FALSE) = 0;

    // closes detached plugin FS (if it allows, see CPluginFSInterfaceAbstract::TryCloseOrDetach),
    // 'detachedFS' is detached plugin FS; returns TRUE on success (FALSE means detached
    // plugin FS was not closed); 'parent' is parent for any message boxes (currently can be opened only by
    // CPluginFSInterfaceAbstract::ReleaseObject)
    // Note: plugin FS opened in panel is closed e.g. using ChangePanelPathToRescuePathOrFixedDrive
    // limitation: main thread + outside methods CPluginFSInterfaceAbstract (we're trying to close
    // detached FS - 'this' could cease to exist for the method)
    virtual BOOL WINAPI CloseDetachedFS(HWND parent, CPluginFSInterfaceAbstract* detachedFS) = 0;

    // duplicates '&' - useful for paths displayed in menu ('&&' is displayed as '&');
    // 'buffer' is input/output string, 'bufferSize' is size of 'buffer' in bytes;
    // returns TRUE if duplication didn't cause loss of characters from end of string (buffer was
    // large enough)
    // can be called from any thread
    virtual BOOL WINAPI DuplicateAmpersands(char* buffer, int bufferSize) = 0;

    // removes '&' from text; if it finds pair "&&", replaces it with single '&' character
    // can be called from any thread
    virtual void WINAPI RemoveAmpersands(char* text) = 0;

    // ValidateVarString and ExpandVarString:
    // methods for validating and expanding strings with variables in form "$(var_name)", "$(var_name:num)"
    // (num is variable width, it's numeric value from 1 to 9999), "$(var_name:max)" ("max" is
    // symbol indicating that variable width is governed by value in 'maxVarWidths' array, details
    // at ExpandVarString) and "$[env_var]" (expands environment variable value); used when
    // user can enter string format (like in info-line) example of string with variables:
    // "$(files) files and $(dirs) directories" - variables 'files' and 'dirs';
    // source code for use in info-line (without variables in form "$(varname:max)") is in DEMOPLUG
    //
    // validates syntax of 'varText' (string with variables), returns FALSE if it finds error,
    // error position is placed in 'errorPos1' (offset of error start) and 'errorPos2' (offset of error end);
    // 'variables' is array of CSalamanderVarStrEntry structures, terminated by structure with
    // Name==NULL; 'msgParent' is parent of message-box with errors, if NULL, errors are not displayed
    virtual BOOL WINAPI ValidateVarString(HWND msgParent, const char* varText, int& errorPos1, int& errorPos2,
                                          const CSalamanderVarStrEntry* variables) = 0;
    //
    // fills 'buffer' with result of expanding 'varText' (string with variables), returns FALSE if
    // 'buffer' is too small (assumes string with variables was validated via ValidateVarString, otherwise
    // also returns FALSE on syntax error) or user clicked Cancel on environment-variable error
    // (not found or too large); 'bufferLen' is size of buffer 'buffer';
    // 'variables' is array of CSalamanderVarStrEntry structures, terminated by structure
    // with Name==NULL; 'param' is pointer passed to CSalamanderVarStrEntry::Execute
    // when expanding found variable; 'msgParent' is parent of message-box with errors, if NULL,
    // errors are not displayed; if 'ignoreEnvVarNotFoundOrTooLong' is TRUE, environment-variable
    // errors are ignored (not found or too large), if FALSE, message box with error is displayed;
    // if 'varPlacements' is not NULL, it points to array of DWORDs with '*varPlacementsCount' items,
    // which will be filled with DWORDs composed of variable position in output buffer (lower WORD)
    // and variable character count (upper WORD); if 'varPlacementsCount' is not NULL, it returns
    // number of filled items in 'varPlacements' array (essentially number of variables in input
    // string);
    // if this method is used only to expand string for single 'param' value,
    // 'detectMaxVarWidths' should be set to FALSE, 'maxVarWidths' to NULL and 'maxVarWidthsCount'
    // to 0; however if this method is used to expand string repeatedly for certain
    // set of 'param' values (e.g. in Make File List it's line expansion for all
    // selected files and directories), it makes sense to use variables in form "$(varname:max)",
    // for these variables width is determined as maximum width of expanded variable across entire
    // set of values; measurement of maximum expanded variable width is performed in first cycle
    // (for all set values) of ExpandVarString calls, in first cycle parameter
    // 'detectMaxVarWidths' has value TRUE and 'maxVarWidths' array with 'maxVarWidthsCount' items
    // is pre-zeroed (serves for storing maxima between individual ExpandVarString calls);
    // actual expansion then happens in second cycle (for all set values) of
    // ExpandVarString calls, in second cycle parameter 'detectMaxVarWidths' has value FALSE and
    // 'maxVarWidths' array with 'maxVarWidthsCount' items contains pre-calculated maximum widths
    // (from first cycle)
    virtual BOOL WINAPI ExpandVarString(HWND msgParent, const char* varText, char* buffer, int bufferLen,
                                        const CSalamanderVarStrEntry* variables, void* param,
                                        BOOL ignoreEnvVarNotFoundOrTooLong = FALSE,
                                        DWORD* varPlacements = NULL, int* varPlacementsCount = NULL,
                                        BOOL detectMaxVarWidths = FALSE, int* maxVarWidths = NULL,
                                        int maxVarWidthsCount = 0) = 0;

    // sets load-on-salamander-start flag (load plugin at Salamander startup?) for plugin;
    // 'start' is new flag value; returns old flag value; if SetFlagLoadOnSalamanderStart
    // was never called, flag is set to FALSE (plugin is not loaded at startup, but
    // only when needed)
    // limitation: main thread (otherwise plugin configuration may change during call)
    virtual BOOL WINAPI SetFlagLoadOnSalamanderStart(BOOL start) = 0;

    // sets flag for calling plugin to unload at earliest opportunity
    // (when all posted menu commands are executed (see PostMenuExtCommand), there are no
    // messages in main thread's message-queue and Salamander is not "busy");
    // WARNING: if called from non-main thread, unload request (runs in main thread) may happen
    // even before PostUnloadThisPlugin finishes (more info about unload - see
    // CPluginInterfaceAbstract::Release)
    // can be called from any thread (but only after plugin entry-point finishes, while
    // entry-point is running, method can only be called from main thread)
    virtual void WINAPI PostUnloadThisPlugin() = 0;

    // returns Salamander modules one by one (executable and .spl files of installed
    // plugins, all including versions); 'index' is input/output variable, points to int
    // which is 0 on first call, function stores value for next call on return
    // (usage: zero at start, then don't change); 'module' is buffer for module name
    // (min. size MAX_PATH chars); 'version' is buffer for module version (min. size
    // MAX_PATH chars); returns FALSE if 'module' + 'version' is not filled and there are no
    // more modules, returns TRUE if 'module' + 'version' contains next module
    // limitation: main thread (otherwise plugin configuration may change during
    // call - add/remove)
    virtual BOOL WINAPI EnumInstalledModules(int* index, char* module, char* version) = 0;

    // calls 'loadOrSaveFunc' for load or save of configuration; if 'load' is TRUE, it's load
    // of configuration, if plugin supports "load/save configuration" and plugin's private
    // registry key exists at call time, 'loadOrSaveFunc' is called for this key, otherwise
    // default configuration load is called ('regKey' parameter of 'loadOrSaveFunc' is NULL);
    // if 'load' is FALSE, it's save of configuration, 'loadOrSaveFunc' is called only if
    // plugin supports "load/save configuration" and Salamander's key exists at call time;
    // 'param' is user parameter and is passed to 'loadOrSaveFunc'
    // limitation: main thread, in entry-point can be called only after SetBasicPluginData,
    // earlier it may not be known if "load/save configuration" support exists and private
    // registry key name
    virtual void WINAPI CallLoadOrSaveConfiguration(BOOL load, FSalLoadOrSaveConfiguration loadOrSaveFunc,
                                                    void* param) = 0;

    // saves 'text' of length 'textLen' (-1 means "use strlen") to clipboard as both multibyte
    // and Unicode (otherwise e.g. Notepad can't handle Czech), on success can (if 'echo' is TRUE)
    // display message "Text was successfully copied to clipboard." (messagebox parent will be
    // 'echoParent'); returns TRUE on success
    // can be called from any thread
    virtual BOOL WINAPI CopyTextToClipboard(const char* text, int textLen, BOOL showEcho, HWND echoParent) = 0;

    // saves unicode 'text' of length 'textLen' (-1 means "use wcslen") to clipboard as both
    // unicode and multibyte (otherwise e.g. MSVC6.0 can't handle Czech), on success can (if
    // 'echo' is TRUE) display message "Text was successfully copied to clipboard." (messagebox parent
    // will be 'echoParent'); returns TRUE on success
    // can be called from any thread
    virtual BOOL WINAPI CopyTextToClipboardW(const wchar_t* text, int textLen, BOOL showEcho, HWND echoParent) = 0;

    // executes menu command with identification number 'id' in main thread (calling
    // CPluginInterfaceForMenuExtAbstract::ExecuteMenuItem(salamander, main-window-hwnd, 'id', 0),
    // 'salamander' is NULL if 'waitForSalIdle' is FALSE, otherwise contains pointer to valid
    // set of usable Salamander methods for performing operations; return value
    // is ignored); if 'waitForSalIdle' is FALSE, message posted to main window is used to start
    // (this message is delivered by any running message-loop in main thread - including modal
    // dialogs/messageboxes, including those opened by plugin), so there's risk of multiple entry
    // into plugin; if 'waitForSalIdle' is TRUE, 'id' is limited to interval <0, 999999> and
    // command is executed when there are no messages in main thread's message-queue and Salamander
    // is not "busy" (no modal dialog is open and no message is being processed);
    // WARNING: if called from non-main thread, menu command execution (runs in main thread)
    // may happen even before PostMenuExtCommand finishes
    // can be called from any thread and if 'waitForSalIdle' is FALSE, must wait until after
    // CPluginInterfaceAbstract::GetInterfaceForMenuExt call (called after entry-point from main thread)
    virtual void WINAPI PostMenuExtCommand(int id, BOOL waitForSalIdle) = 0;

    // determines if there's high chance (cannot be determined for certain) that Salamander
    // won't be "busy" in next few moments (no modal dialog open and no message being processed)
    // - in this case returns TRUE (otherwise FALSE); if 'lastIdleTime' is not NULL,
    // GetTickCount() from moment of last transition from "idle" to "busy" state is returned in it;
    // can be used e.g. as prediction for delivery of command posted via
    // CSalamanderGeneralAbstract::PostMenuExtCommand with 'waitForSalIdle'==TRUE parameter;
    // can be called from any thread
    virtual BOOL WINAPI SalamanderIsNotBusy(DWORD* lastIdleTime) = 0;

    // sets message to be displayed by Bug Report dialog if crash occurs inside plugin
    // (inside plugin = at least one call-stack-message saved from plugin) and allows redefining
    // standard bug-report email address (support@altap.cz); 'message' is new message
    // (NULL means "no message"); 'email' is new email address (NULL means "use
    // standard"; max email length is 100 chars); this method can be called repeatedly, original
    // message and email are overwritten; Salamander doesn't remember message or email for next
    // run, so this method must be called again on each plugin load (preferably in entry-point)
    // limitation: main thread (otherwise plugin configuration may change during call)
    virtual void WINAPI SetPluginBugReportInfo(const char* message, const char* email) = 0;

    // determines if plugin is installed (but doesn't determine if it can be loaded - if user
    // e.g. deleted it only from disk); 'pluginSPL' identifies plugin - it's required
    // ending part of full path to plugin's .SPL file (e.g. "ieviewer\\ieviewer.spl" identifies
    // IEViewer shipped with Salamander); returns TRUE if plugin is installed
    // limitation: main thread (otherwise plugin configuration may change during call)
    virtual BOOL WINAPI IsPluginInstalled(const char* pluginSPL) = 0;

    // opens file in viewer implemented in plugin or internal text/hex viewer;
    // if 'pluginSPL' is NULL, internal text/hex viewer should be used, otherwise identifies plugin
    // viewer - it's required ending part of full path to plugin's .SPL file (e.g.
    // "ieviewer\\ieviewer.spl" identifies IEViewer shipped with Salamander); 'pluginData'
    // is data structure containing viewed file name and optionally contains extended
    // viewer parameters (see CSalamanderPluginViewerData description); if 'useCache' is FALSE,
    // 'rootTmpPath' and 'fileNameInCache' are ignored and file is just opened in viewer;
    // if 'useCache' is TRUE, file is first moved to disk cache under file name
    // 'fileNameInCache' (name without path), then opened in viewer and after closing viewer
    // removed from disk cache, if file 'pluginData->FileName' is on same disk as
    // disk cache, move is instant, otherwise copying between volumes occurs, which may
    // take longer, but no progress is shown (if 'rootTmpPath' is NULL, disk cache is in
    // Windows TEMP directory, otherwise path to disk cache is in 'rootTmpPath'; SalMoveFile
    // is used for move to disk cache), ideal is using SalGetTempFileName
    // with 'path' parameter equal to 'rootTmpPath'; returns TRUE on successful file opening in
    // viewer; returns FALSE if error occurs while opening (specific reason is stored in
    // 'error' - 0 - success, 1 - cannot load plugin, 2 - ViewFile from plugin returned
    // error, 3 - cannot move file to disk cache), if 'useCache' is TRUE,
    // file is removed from disk (as after closing viewer)
    // limitation: main thread (otherwise plugin configuration may change during call),
    // also cannot be called from entry-point (plugin load is not reentrant)
    virtual BOOL WINAPI ViewFileInPluginViewer(const char* pluginSPL,
                                               CSalamanderPluginViewerData* pluginData,
                                               BOOL useCache, const char* rootTmpPath,
                                               const char* fileNameInCache, int& error) = 0;

    // as soon as possible informs Salamander, then all loaded plugins and then all open
    // FS (in panels and detached) about change on path 'path' (disk or FS path); important for
    // paths where changes cannot be monitored automatically (see FindFirstChangeNotification) or
    // user disabled this monitoring (auto-refresh), for FS the plugin handles change monitoring
    // itself; notification about changes happens as soon as possible (if main thread is not executing
    // code inside plugin, refresh happens after message delivery to main window and possibly after
    // re-enabling refresh (after closing dialog, etc.), otherwise refresh waits at least until
    // main thread leaves plugin); 'includingSubdirs' is TRUE if change may
    // also affect subdirectories of 'path';
    // WARNING: if called from non-main thread, notification about changes (runs in main thread)
    // may happen even before PostChangeOnPathNotification finishes
    // can be called from any thread
    virtual void WINAPI PostChangeOnPathNotification(const char* path, BOOL includingSubdirs) = 0;

    // tries to access Windows path 'path' (normal or UNC), runs in worker thread, so
    // allows interrupting test with ESC key (after certain time shows window with ESC message)
    // 'echo' TRUE means error message display is allowed (if path is not accessible);
    // 'err' different from ERROR_SUCCESS combined with 'echo' TRUE only displays error (path
    // is not accessed); 'parent' is messagebox parent; returns ERROR_SUCCESS if
    // path is OK, otherwise returns standard Windows error code or ERROR_USER_TERMINATED
    // if user used ESC key to interrupt test
    // limitation: main thread (repeated calls not possible and main thread uses this method)
    virtual DWORD WINAPI SalCheckPath(BOOL echo, const char* path, DWORD err, HWND parent) = 0;

    // tries if Windows path 'path' is accessible, optionally restores network connections (if it's
    // normal path, tries to revive remembered network connection, if it's UNC path, allows login
    // with new username and password); returns TRUE if path is accessible; 'parent' is parent
    // of messageboxes and dialogs; 'tryNet' is TRUE if it makes sense to try restoring network connections
    // (with FALSE degrades to SalCheckPath; here only for optimization possibility)
    // limitation: main thread (repeated calls not possible and main thread uses this method)
    virtual BOOL WINAPI SalCheckAndRestorePath(HWND parent, const char* path, BOOL tryNet) = 0;

    // more complex variant of SalCheckAndRestorePath method; tries if Windows path 'path' is
    // accessible, optionally shortens it; if 'tryNet' is TRUE, also tries to restore network connection
    // and sets 'tryNet' to FALSE (if it's normal path, tries to revive remembered network connection, if
    // it's UNC path, allows login with new username and password); if 'donotReconnect' is
    // TRUE, only error is detected, connection restore is not performed; returns 'err' (Windows error code
    // of current path), 'lastErr' (error code leading to path shortening), 'pathInvalid' (TRUE if
    // network connection restore was attempted without success), 'cut' (TRUE if resulting path is shortened);
    // 'parent' is messagebox parent; returns TRUE if resulting path 'path' is accessible
    // limitation: main thread (repeated calls not possible and main thread uses this method)
    virtual BOOL WINAPI SalCheckAndRestorePathWithCut(HWND parent, char* path, BOOL& tryNet, DWORD& err,
                                                      DWORD& lastErr, BOOL& pathInvalid, BOOL& cut,
                                                      BOOL donotReconnect) = 0;

    // recognizes path type (FS/Windows/archive) and handles splitting into
    // its parts (for FS it's fs-name and fs-user-part, for archive it's path-to-archive and
    // path-in-archive, for Windows paths it's existing part and rest of path), for FS paths
    // nothing is checked, for Windows (normal + UNC) paths it checks how far path exists
    // (optionally restores network connection), for archive it checks archive file existence
    // (archive distinguished by extension);
    // 'path' is full or relative path (buffer min. 'pathBufSize' chars; for relative paths
    // current path 'curPath' (if not NULL) is considered as base for full path evaluation;
    // 'curPathIsDiskOrArchive' is TRUE if 'curPath' is Windows or archive path;
    // if current path is archive, 'curArchivePath' contains archive name, otherwise is NULL),
    // resulting full path is stored in 'path' (must be min. 'pathBufSize' chars); returns TRUE on
    // successful recognition, then 'type' is path type (see PATH_TYPE_XXX) and 'secondPart' is set:
    // - in 'path' to position after existing path (after '\\' or at end of string; if file exists
    //   in path, points after path to this file) (Windows path type), WARNING: length of returned
    //   path part is not handled (whole path may be longer than MAX_PATH)
    // - after archive file (archive path type), WARNING: path length in archive is not handled (may be
    //   longer than MAX_PATH)
    // - after ':' after file-system name - user-part of file-system path (FS path type), WARNING:
    //   user-part path length is not handled (may be longer than MAX_PATH);
    // if returns TRUE, 'isDir' is also set to:
    // - TRUE if existing path part is directory, FALSE == file (Windows path type)
    // - FALSE for archive and FS path types;
    // if returns FALSE, error was displayed to user (with one exception - see SPP_INCOMLETEPATH description),
    // that occurred during recognition (if 'error' is not NULL, one of SPP_XXX constants is returned in it);
    // 'errorTitle' is error messagebox title; if 'nextFocus' != NULL and Windows/archive
    // path doesn't contain '\\' or ends only with '\\', path is copied to 'nextFocus' (see SalGetFullName);
    // WARNING: uses SalGetFullName, so it's good to first call method
    //          CSalamanderGeneralAbstract::SalUpdateDefaultDir
    // limitation: main thread (repeated calls not possible and main thread uses this method)
    virtual BOOL WINAPI SalParsePath(HWND parent, char* path, int& type, BOOL& isDir, char*& secondPart,
                                     const char* errorTitle, char* nextFocus, BOOL curPathIsDiskOrArchive,
                                     const char* curPath, const char* curArchivePath, int* error,
                                     int pathBufSize) = 0;

    // extracts existing part and operation mask from Windows target path; allows creating
    // non-existing part; on success returns TRUE and existing Windows target path (in 'path')
    // and found operation mask (in 'mask' - points into 'path' buffer, but path and mask are separated
    // by null; if path has no mask, automatically creates mask "*.*"); 'parent' - parent of any
    // messageboxes; 'title' + 'errorTitle' are messagebox titles for info + error; 'selCount' is
    // count of selected files and directories; 'path' is input target path to process, on output
    // (at least 2 * MAX_PATH chars) existing target path; 'secondPart' points into 'path' to position
    // after existing path (after '\\' or at end of string; if file exists in path, points after path
    // to this file); 'pathIsDir' is TRUE/FALSE if existing path part is directory/file;
    // 'backslashAtEnd' is TRUE if there was backslash at end of 'path' before "parse" (e.g.
    // SalParsePath removes such backslash); 'dirName' + 'curDiskPath' are not NULL if at most
    // one file/directory is selected (its name without path is in 'dirName'; if nothing is selected,
    // focus is used) and current path is Windows (path is in 'curDiskPath'); 'mask' is on output
    // pointer to operation mask in 'path' buffer; if path has error, method returns FALSE,
    // problem was already reported to user
    // can be called from any thread
    virtual BOOL WINAPI SalSplitWindowsPath(HWND parent, const char* title, const char* errorTitle,
                                            int selCount, char* path, char* secondPart, BOOL pathIsDir,
                                            BOOL backslashAtEnd, const char* dirName,
                                            const char* curDiskPath, char*& mask) = 0;

    // extracts existing part and operation mask from target path; recognizes non-existing part; on
    // success returns TRUE, relative path to create (in 'newDirs'), existing target path (in 'path';
    // existing only assuming creation of relative path 'newDirs') and found operation mask
    // (in 'mask' - points into 'path' buffer, but path and mask are separated by null; if path has no
    // mask, automatically creates mask "*.*"); 'parent' - parent of any messageboxes;
    // 'title' + 'errorTitle' are messagebox titles for info + error; 'selCount' is count of selected
    // files and directories; 'path' is input target path to process, on output (at least 2 * MAX_PATH
    // chars) existing target path (always ends with backslash); 'afterRoot' points into 'path' after path root
    // (after '\\' or at end of string); 'secondPart' points into 'path' to position after existing path (after
    // '\\' or at end of string; if file exists in path, points after path to this file);
    // 'pathIsDir' is TRUE/FALSE if existing path part is directory/file; 'backslashAtEnd' is
    // TRUE if there was backslash at end of 'path' before "parse" (e.g. SalParsePath removes such
    // backslash); 'dirName' + 'curPath' are not NULL if at most one file/directory is selected
    // (its name without path is in 'dirName'; its path is in 'curPath'; if nothing is selected,
    // focus is used); 'mask' is on output pointer to operation mask in 'path' buffer; if 'newDirs' is not NULL,
    // it's buffer (at least MAX_PATH size) for relative path (relative to existing path
    // in 'path'), which needs to be created (user agreed to creation, same query as for
    // disk to disk copy was used; empty string = create nothing); if 'newDirs' is NULL and
    // some relative path needs to be created, only error is displayed; 'isTheSamePathF' is function for
    // comparing two paths (needed only if 'curPath' is not NULL), if NULL then IsTheSamePath is used;
    // if path has error, method returns FALSE, problem was already reported to user
    // can be called from any thread
    virtual BOOL WINAPI SalSplitGeneralPath(HWND parent, const char* title, const char* errorTitle,
                                            int selCount, char* path, char* afterRoot, char* secondPart,
                                            BOOL pathIsDir, BOOL backslashAtEnd, const char* dirName,
                                            const char* curPath, char*& mask, char* newDirs,
                                            SGP_IsTheSamePathF isTheSamePathF) = 0;

    // removes ".." (skips ".." together with one subdirectory to the left) and "." (skips just ".")
    // from path; condition is backslash as subdirectory separator; 'afterRoot' points after root
    // of processed path (path changes happen only after 'afterRoot'); returns TRUE if modifications
    // succeeded, FALSE if ".." cannot be removed (root is already on left)
    // can be called from any thread
    virtual BOOL WINAPI SalRemovePointsFromPath(char* afterRoot) = 0;

    // returns parameter from Salamander configuration; 'paramID' identifies which parameter
    // (see SALCFG_XXX constants); 'buffer' points to buffer where parameter
    // data will be copied, buffer size is 'bufferSize'; if 'type' is not NULL,
    // one of SALCFGTYPE_XXX constants or SALCFGTYPE_NOTFOUND is returned in it (if parameter with
    // 'paramID' was not found); returns TRUE if 'paramID' is valid and configuration
    // parameter value fits in buffer 'buffer'
    // note: changes in Salamander configuration are reported via event
    //       PLUGINEVENT_CONFIGURATIONCHANGED (see CPluginInterfaceAbstract::Event method)
    // limitation: main thread, configuration changes happen only in main thread (doesn't contain
    //             other synchronization)
    virtual BOOL WINAPI GetConfigParameter(int paramID, void* buffer, int bufferSize, int* type) = 0;

    // changes letter case in file name (name is without path); 'tgtName' is buffer for result
    // (size is min. for storing string 'srcName'); 'srcName' is file name (written to,
    // but restored before method returns); 'format' is result format (1 - capitalize first letters
    // of words, 2 - all lowercase, 3 - all uppercase, 4 - no changes, 5 - if
    // DOS name (8.3) -> capitalize first letters of words, 6 - file lowercase, directory uppercase,
    // 7 - capitalize first letters in name and lowercase in extension);
    // 'changedParts' determines which parts of name to change (0 - changes name and extension, 1 - changes only
    // name (possible only with format == 1, 2, 3, 4), 2 - changes only extension (possible only with
    // format == 1, 2, 3, 4)); 'isDir' is TRUE if it's directory name
    // can be called from any thread
    virtual void WINAPI AlterFileName(char* tgtName, char* srcName, int format, int changedParts,
                                      BOOL isDir) = 0;

    // shows/hides message in window in its own thread (doesn't pump message-queue); shows
    // only one message at a time, repeated calls report error to TRACE (not fatal);
    // NOTE: used in SalCheckPath and other routines, so there may be collision between
    //       requests to open windows (not fatal, just won't be shown)
    // all can be called from any thread (but window must be handled only
    // from one thread - cannot show from one thread and hide from another)
    //
    // opens window with text 'message' with delay 'delay' (in ms), only if 'hForegroundWnd' is NULL
    // or identifies foreground window
    // 'message' can be multiline; individual lines are separated by '\n' character
    // 'caption' can be NULL: then "Open Salamander" is used
    // 'showCloseButton' specifies whether window will contain Close button; equivalent to Escape key
    virtual void WINAPI CreateSafeWaitWindow(const char* message, const char* caption,
                                             int delay, BOOL showCloseButton, HWND hForegroundWnd) = 0;
    // closes window
    virtual void WINAPI DestroySafeWaitWindow() = 0;
    // hides/shows window (if open); call as reaction to WM_ACTIVATE from hForegroundWnd window:
    //    case WM_ACTIVATE:
    //    {
    //      ShowSafeWaitWindow(LOWORD(wParam) != WA_INACTIVE);
    //      break;
    //    }
    // If thread (from which window was created) is busy, messages are not
    // distributed, so WM_ACTIVATE is not delivered when clicking
    // on another application. Messages are delivered when messagebox is shown,
    // which is exactly what we need: temporarily hide and later (after closing
    // messagebox and activating hForegroundWnd window) show again.
    virtual void WINAPI ShowSafeWaitWindow(BOOL show) = 0;
    // after calling CreateSafeWaitWindow or ShowSafeWaitWindow returns FALSE until
    // user clicked mouse on Close button (if shown); then returns TRUE
    virtual BOOL WINAPI GetSafeWaitWindowClosePressed() = 0;
    // used for subsequent text change in window
    // WARNING: window is not re-layouted and if text stretches more,
    // it will be clipped; use for example for countdown: 60s, 55s, 50s, ...
    virtual void WINAPI SetSafeWaitWindowText(const char* message) = 0;

    // finds existing file copy in disk-cache and locks it (prevents its deletion); 'uniqueFileName'
    // is unique name of original file (disk-cache is searched by this name; full file name in
    // Salamander form should suffice - "fs-name:fs-user-part"; WARNING: name is
    // compared "case-sensitive", if plugin requires "case-insensitive", all names must be
    // converted e.g. to lowercase - see CSalamanderGeneralAbstract::ToLowerCase); 'tmpName'
    // returns pointer (valid until file copy deletion in disk-cache) to full name of file copy,
    // which is located in temporary directory; 'fileLock' is file copy lock, it's system event
    // in nonsignaled state, which after processing file copy transitions to signaled state (must use
    // UnlockFileInCache method; plugin signals that copy in disk-cache can be deleted);
    // if copy was not found returns FALSE and 'tmpName' NULL (otherwise returns TRUE)
    // can be called from any thread
    virtual BOOL WINAPI GetFileFromCache(const char* uniqueFileName, const char*& tmpName, HANDLE fileLock) = 0;

    // unlocks file copy lock in disk-cache (sets 'fileLock' to signaled state, requests
    // disk-cache to perform lock check, and then sets 'fileLock' back to nonsignaled state);
    // if it was last lock, copy may be deleted, when deletion happens depends
    // on disk-cache size on disk; lock can be used for multiple file copies (lock
    // must be "manual reset" type, otherwise after unlocking first copy lock is set to
    // nonsignaled state and unlocking ends), in this case unlocking happens for all copies
    // can be called from any thread
    virtual void WINAPI UnlockFileInCache(HANDLE fileLock) = 0;

    // inserts (moves) file copy to disk-cache (inserted copy is not locked, so it can be deleted anytime);
    // 'uniqueFileName' is unique name of original file (disk-cache is searched by this
    // name; full file name in Salamander form should suffice - "fs-name:fs-user-part"; WARNING:
    // name is compared "case-sensitive", if plugin requires "case-insensitive", all names must be
    // converted e.g. to lowercase - see CSalamanderGeneralAbstract::ToLowerCase); 'nameInCache' is name
    // of file copy, which will be located in temporary directory (last part of original file name is
    // expected here, so it later reminds user of original file); 'newFileName' is full name of stored
    // file copy, which will be moved to disk-cache under name 'nameInCache', must be located on same disk
    // as disk cache (if 'rootTmpPath' is NULL, disk cache is in Windows TEMP directory, otherwise
    // path to disk-cache is in 'rootTmpPath'; for renaming to disk cache via Win32 API function
    // MoveFile); 'newFileName' is ideally obtained by calling SalGetTempFileName with 'path' parameter equal
    // to 'rootTmpPath'); 'newFileSize' contains size of stored file copy; returns TRUE on success
    // (file was moved to disk-cache - disappeared from original location on disk), returns FALSE on
    // internal error or if file is already in disk-cache (if 'alreadyExists' is not NULL,
    // TRUE is returned in it if file is already in disk-cache)
    // NOTE: if plugin uses disk-cache, it should at least on plugin unload call
    //       CSalamanderGeneralAbstract::RemoveFilesFromCache("fs-name:"), otherwise its
    //       file copies will unnecessarily clutter disk-cache
    // can be called from any thread
    virtual BOOL WINAPI MoveFileToCache(const char* uniqueFileName, const char* nameInCache,
                                        const char* rootTmpPath, const char* newFileName,
                                        const CQuadWord& newFileSize, BOOL* alreadyExists) = 0;

    // removes file copy from disk-cache whose unique name is 'uniqueFileName' (WARNING: name
    // is compared "case-sensitive", if plugin requires "case-insensitive", all names must be
    // converted e.g. to lowercase - see CSalamanderGeneralAbstract::ToLowerCase); if file copy
    // is still being used, it will be removed when possible (when viewers are closed), anyway
    // disk-cache won't provide it to anyone as valid file copy (it's marked as out-of-date)
    // can be called from any thread
    virtual void WINAPI RemoveOneFileFromCache(const char* uniqueFileName) = 0;

    // removes all file copies from disk-cache whose unique names start with 'fileNamesRoot'
    // (used when closing file-system, when it's no longer desirable to cache downloaded file
    // copies; WARNING: names are compared "case-sensitive", if plugin requires "case-insensitive",
    // all names must be converted e.g. to lowercase - see CSalamanderGeneralAbstract::ToLowerCase);
    // if file copies are still being used, they will be removed when possible (when unlocked
    // e.g. after closing viewers), anyway disk-cache won't provide them to anyone as valid file
    // copies (they're marked as out-of-date)
    // can be called from any thread
    virtual void WINAPI RemoveFilesFromCache(const char* fileNamesRoot) = 0;

    // returns conversion tables one by one (loaded from convert\XXX\convert.cfg file
    // in Salamander installation - XXX is currently used conversion tables directory);
    // 'parent' is messagebox parent (if NULL, parent is main window);
    // 'index' is input/output variable, points to int which is 0 on first call,
    // function stores value for next call on return (usage: zero at start, then don't change);
    // returns FALSE if there are no more tables; if returns TRUE, 'name' (if not NULL) contains
    // reference to conversion name (may contain '&' - underlined character in menu) or NULL
    // if it's separator and 'table' (if not NULL) reference to 256-byte conversion table or NULL
    // if it's separator; references 'name' and 'table' are valid for entire Salamander runtime (no need
    // to copy content)
    // WARNING: use 'table' pointer this way (cast to "unsigned" required):
    //          *s = table[(unsigned char)*s]
    // can be called from any thread
    virtual BOOL WINAPI EnumConversionTables(HWND parent, int* index, const char** name, const char** table) = 0;

    // returns conversion table 'table' (buffer min. 256 chars) for conversion 'conversion' (conversion
    // name see convert\XXX\convert.cfg file in Salamander installation, e.g. "ISO-8859-2 - CP1250";
    // characters <= ' ' and '-' and '&' in name don't matter when searching; search is case-insensitive);
    // 'parent' is messagebox parent (if NULL, parent is main window); returns TRUE
    // if conversion was found (otherwise 'table' content is not valid);
    // WARNING: use this way (cast to "unsigned" required): *s = table[(unsigned char)*s]
    // can be called from any thread
    virtual BOOL WINAPI GetConversionTable(HWND parent, char* table, const char* conversion) = 0;

    // returns name of code page used in Windows in this region (sources from convert\XXX\convert.cfg
    // in Salamander installation); it's normally displayable encoding, so it's used when
    // text created in different code page needs to be displayed (specified here as
    // "target" encoding when searching for conversion table, see GetConversionTable method);
    // 'parent' is messagebox parent (if NULL, parent is main window); 'codePage' is buffer
    // (min. 101 chars) for code page name (if this name is not defined in convert\XXX\convert.cfg file,
    // empty string is returned in buffer)
    // can be called from any thread
    virtual void WINAPI GetWindowsCodePage(HWND parent, char* codePage) = 0;

    // determines from buffer 'pattern' of length 'patternLen' (e.g. first 10000 chars) if it's
    // text (there's code page in which it contains only allowed characters - displayable
    // and control) and if it's text, also determines its code page (most probable);
    // 'parent' is messagebox parent (if NULL, parent is main window); if 'forceText' is
    // TRUE, check for disallowed characters is not performed (used if 'pattern' contains
    // text); if 'isText' is not NULL, TRUE is returned in it if it's text; if 'codePage' is not
    // NULL, it's buffer (min. 101 chars) for code page name (most probable)
    // can be called from any thread
    virtual void WINAPI RecognizeFileType(HWND parent, const char* pattern, int patternLen, BOOL forceText,
                                          BOOL* isText, char* codePage) = 0;

    // determines from buffer 'text' of length 'textLen' if it's ANSI text (contains (in ANSI
    // character set) only allowed characters - displayable and control); decides without context
    // (doesn't depend on character count or their order - tested text can be split
    // into arbitrary parts and tested sequentially); returns TRUE if it's ANSI text (otherwise
    // 'text' buffer content is binary)
    // can be called from any thread
    virtual BOOL WINAPI IsANSIText(const char* text, int textLen) = 0;

    // calls function 'callback' with parameters 'param' and function for getting selected
    // files/directories (see SalPluginOperationFromDisk type definition) from panel 'panel'
    // (Windows path must be opened in panel); 'panel' is one of PANEL_XXX
    // limitation: main thread
    virtual void WINAPI CallPluginOperationFromDisk(int panel, SalPluginOperationFromDisk callback,
                                                    void* param) = 0;

    // returns standard charset that user has set (part of regional
    // settings); fonts must be constructed with this charset, otherwise texts may not be
    // readable (if text is in standard code page, see Win32 API function
    // GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE, ...))
    // can be called from any thread
    virtual BYTE WINAPI GetUserDefaultCharset() = 0;

    // allocates new Boyer-Moore search algorithm object
    // can be called from any thread
    virtual CSalamanderBMSearchData* WINAPI AllocSalamanderBMSearchData() = 0;

    // frees Boyer-Moore search algorithm object (obtained by AllocSalamanderBMSearchData method)
    // can be called from any thread
    virtual void WINAPI FreeSalamanderBMSearchData(CSalamanderBMSearchData* data) = 0;

    // allocates new regular expression search algorithm object
    // can be called from any thread
    virtual CSalamanderREGEXPSearchData* WINAPI AllocSalamanderREGEXPSearchData() = 0;

    // frees regular expression search algorithm object (obtained by
    // AllocSalamanderREGEXPSearchData method)
    // can be called from any thread
    virtual void WINAPI FreeSalamanderREGEXPSearchData(CSalamanderREGEXPSearchData* data) = 0;

    // returns Salamander commands one by one (proceeds in order of SALCMD_XXX constant definitions);
    // 'index' is input/output variable, points to int which is 0 on first call,
    // function stores value for next call on return (usage: zero at start, then don't change);
    // returns FALSE if there are no more commands; if returns TRUE, 'salCmd' (if not NULL) contains
    // Salamander command number (see SALCMD_XXX constants; numbers have reserved interval 0 to 499,
    // so if Salamander commands should be in menu together with other commands, it's no problem to
    // create mutually non-overlapping sets of command values e.g. by shifting all values by chosen
    // number - example see DEMOPLUGin - CPluginFSInterface::ContextMenu), 'nameBuf' (buffer of size
    // 'nameBufSize' bytes) contains command name (name is prepared for menu use - has doubled ampersands,
    // underlined characters marked with ampersands and after '\t' has keyboard shortcut descriptions),
    // 'enabled' (if not NULL) contains command state (TRUE/FALSE if enabled/disabled), 'type'
    // (if not NULL) contains command type (see sctyXXX constants description)
    // can be called from any thread
    virtual BOOL WINAPI EnumSalamanderCommands(int* index, int* salCmd, char* nameBuf, int nameBufSize,
                                               BOOL* enabled, int* type) = 0;

    // returns Salamander command with number 'salCmd' (see SALCMD_XXX constants);
    // returns FALSE if such command doesn't exist; if returns TRUE, 'nameBuf' (buffer of size
    // 'nameBufSize' bytes) contains command name (name is prepared for menu use - has doubled ampersands,
    // underlined characters marked with ampersands and after '\t' has keyboard shortcut descriptions),
    // 'enabled' (if not NULL) contains command state (TRUE/FALSE if enabled/disabled), 'type'
    // (if not NULL) contains command type (see sctyXXX constants description)
    // can be called from any thread
    virtual BOOL WINAPI GetSalamanderCommand(int salCmd, char* nameBuf, int nameBufSize, BOOL* enabled,
                                             int* type) = 0;

    // sets flag for calling plugin to execute Salamander command with number 'salCmd' at earliest
    // opportunity (when there are no messages in main thread's message-queue and Salamander is not
    // "busy" (no modal dialog is open and no message is being processed));
    // WARNING: if called from non-main thread, Salamander command execution (runs in main thread)
    // may happen even before PostSalamanderCommand finishes
    // can be called from any thread
    virtual void WINAPI PostSalamanderCommand(int salCmd) = 0;

    // sets "user worked with current path" flag in panel 'panel' (this flag is used
    // when populating List Of Working Directories (Alt+F12));
    // 'panel' is one of PANEL_XXX
    // limitation: main thread
    virtual void WINAPI SetUserWorkedOnPanelPath(int panel) = 0;

    // in panel 'panel' (one of PANEL_XXX constants) saves selected names
    // to special array, from which user can restore selection using Edit/Restore Selection command
    // used for commands that cancel current selection, so user can
    // return to it and perform another operation
    // limitation: main thread
    virtual void WINAPI StoreSelectionOnPanelPath(int panel) = 0;

    //
    // UpdateCrc32
    //   Updates CRC-32 (32-bit Cyclic Redundancy Check) with specified array of bytes.
    //
    // Parameters
    //   'buffer'
    //      [in] Pointer to the starting address of the block of memory to update 'crcVal' with.
    //
    //   'count'
    //      [in] Size, in bytes, of the block of memory to update 'crcVal' with.
    //
    //   'crcVal'
    //      [in] Initial crc value. Set this value to zero to calculate CRC-32 of the 'buffer'.
    //
    // Return Values
    //   Returns updated CRC-32 value.
    //
    // Remarks
    //   Method can be called from any thread.
    //
    virtual DWORD WINAPI UpdateCrc32(const void* buffer, DWORD count, DWORD crcVal) = 0;

    // allocates new object for MD5 calculation
    // can be called from any thread
    virtual CSalamanderMD5* WINAPI AllocSalamanderMD5() = 0;

    // frees MD5 calculation object (obtained by AllocSalamanderMD5 method)
    // can be called from any thread
    virtual void WINAPI FreeSalamanderMD5(CSalamanderMD5* md5) = 0;

    // Finds pairs '<' '>' in text, removes them from buffer and adds references to
    // their content into 'varPlacements'. 'varPlacements' is array of DWORDs with '*varPlacementsCount'
    // items, DWORDs are composed of reference position in output buffer (lower WORD)
    // and reference character count (upper WORD). Strings "\<", "\>", "\\" are understood
    // as escape sequences and will be replaced with '<', '>' and '\\' characters.
    // Returns TRUE on success, otherwise FALSE; always sets 'varPlacementsCount' to
    // number of processed variables.
    // can be called from any thread
    virtual BOOL WINAPI LookForSubTexts(char* text, DWORD* varPlacements, int* varPlacementsCount) = 0;

    // waits (maximum 0.2 seconds) for ESC key release; used if plugin contains
    // actions that are interrupted by ESC key (ESC key monitoring via
    // GetAsyncKeyState(VK_ESCAPE)) - prevents following action monitoring ESC key from being
    // immediately interrupted after pressing ESC in dialog/messagebox
    // can be called from any thread
    virtual void WINAPI WaitForESCRelease() = 0;

    //
    // GetMouseWheelScrollLines
    //   An OS independent method to retrieve the number of wheel scroll lines.
    //
    // Return Values
    //   Number of scroll lines where WHEEL_PAGESCROLL (0xffffffff) indicates to scroll a page at a time.
    //
    // Remarks
    //   Method can be called from any thread.
    //
    virtual DWORD WINAPI GetMouseWheelScrollLines() = 0;

    //
    // GetTopVisibleParent
    //   Retrieves the visible root window by walking the chain of parent windows
    //   returned by GetParent.
    //
    // Parameters
    //   'hParent'
    //      [in] Handle to the window whose parent window handle is to be retrieved.
    //
    // Return Values
    //   The return value is the handle to the top Popup or Overlapped visible parent window.
    //
    // Remarks
    //   Method can be called from any thread.
    //
    virtual HWND WINAPI GetTopVisibleParent(HWND hParent) = 0;

    //
    // MultiMonGetDefaultWindowPos
    //   Retrieves the default position of the upper-left corner for a newly created window
    //   on the display monitor that has the largest area of intersection with the bounding
    //   rectangle of a specified window.
    //
    // Parameters
    //   'hByWnd'
    //      [in] Handle to the window of interest.
    //
    //   'p'
    //      [out] Pointer to a POINT structure that receives the virtual-screen coordinates
    //      of the upper-left corner for the window that would be created with CreateWindow
    //      with CW_USEDEFAULT in the 'x' parameter. Note that if the monitor is not the
    //      primary display monitor, some of the point's coordinates may be negative values.
    //
    // Return Values
    //   If the default window position lies on the primary monitor or some error occured,
    //   the return value is FALSE and you should use CreateWindow with CW_USEDEFAULT in
    //   the 'x' parameter.
    //
    //   Otherwise the return value is TRUE and coordinates from 'p' structure should be used
    //   in the CreateWindow 'x' and 'y' parameters.
    //
    // Remarks
    //   Method can be called from any thread.
    //
    virtual BOOL WINAPI MultiMonGetDefaultWindowPos(HWND hByWnd, POINT* p) = 0;

    //
    // MultiMonGetClipRectByRect
    //   Retrieves the bounding rectangle of the display monitor that has the largest
    //   area of intersection with a specified rectangle.
    //
    // Parameters
    //   'rect'
    //      [in] Pointer to a RECT structure that specifies the rectangle of interest
    //      in virtual-screen coordinates.
    //
    //   'workClipRect'
    //      [out] A RECT structure that specifies the work area rectangle of the
    //      display monitor, expressed in virtual-screen coordinates. Note that
    //      if the monitor is not the primary display monitor, some of the rectangle's
    //      coordinates may be negative values.
    //
    //   'monitorClipRect'
    //      [out] A RECT structure that specifies the display monitor rectangle,
    //      expressed in virtual-screen coordinates. Note that if the monitor is
    //      not the primary display monitor, some of the rectangle's coordinates
    //      may be negative values. This parameter can be NULL.
    //
    // Remarks
    //   Method can be called from any thread.
    //
    virtual void WINAPI MultiMonGetClipRectByRect(const RECT* rect, RECT* workClipRect, RECT* monitorClipRect) = 0;

    //
    // MultiMonGetClipRectByWindow
    //   Retrieves the bounding rectangle of the display monitor that has the largest
    //   area of intersection with the bounding rectangle of a specified window.
    //
    // Parameters
    //   'hByWnd'
    //      [in] Handle to the window of the interest. If this parameter is NULL,
    //      or window is not visible or is iconic, monitor with the currently active window
    //      from the same application will be used. Otherwise primary monitor will be used.
    //
    //   'workClipRect'
    //      [out] A RECT structure that specifies the work area rectangle of the
    //      display monitor, expressed in virtual-screen coordinates. Note that
    //      if the monitor is not the primary display monitor, some of the rectangle's
    //      coordinates may be negative values.
    //
    //   'monitorClipRect'
    //      [out] A RECT structure that specifies the display monitor rectangle,
    //      expressed in virtual-screen coordinates. Note that if the monitor is
    //      not the primary display monitor, some of the rectangle's coordinates
    //      may be negative values. This parameter can be NULL.
    //
    // Remarks
    //   Method can be called from any thread.
    //
    virtual void WINAPI MultiMonGetClipRectByWindow(HWND hByWnd, RECT* workClipRect, RECT* monitorClipRect) = 0;

    //
    // MultiMonCenterWindow
    //   Centers the window against a specified window or monitor.
    //
    // Parameters
    //   'hWindow'
    //      [in] Handle to the window whose parent window handle is to be retrieved.
    //
    //   'hByWnd'
    //      [in] Handle to the window against which to center. If this parameter is NULL,
    //      or window is not visible or is iconic, the method will center 'hWindow' against
    //      the working area of the monitor. Monitor with the currently active window
    //      from the same application will be used. Otherwise primary monitor will be used.
    //
    //   'findTopWindow'
    //      [in] If this parameter is TRUE, non child visible window will be used by walking
    //      the chain of parent windows of 'hByWnd' as the window against which to center.
    //
    //      If this parameter is FALSE, 'hByWnd' will be to the window against which to center.
    //
    // Remarks
    //   If centered window gets over working area of the monitor, the method positions
    //   the window to be whole visible.
    //
    //   Method can be called from any thread.
    //
    virtual void WINAPI MultiMonCenterWindow(HWND hWindow, HWND hByWnd, BOOL findTopWindow) = 0;

    //
    // MultiMonEnsureRectVisible
    //   Ensures that specified rectangle is either entirely or partially visible,
    //   adjusting the coordinates if necessary. All monitors are considered.
    //
    // Parameters
    //   'rect'
    //      [in/out] Pointer to the RECT structure that contain the coordinated to be
    //      adjusted. The rectangle is presumed to be in virtual-screen coordinates.
    //
    //   'partialOK'
    //      [in] Value specifying whether the rectangle must be entirely visible.
    //      If this parameter is TRUE, no moving occurs if the item is at least
    //      partially visible.
    //
    // Return Values
    //   If the rectangle is adjusted, the return value is TRUE.
    //
    //   If the rectangle is not adjusted, the return value is FALSE.
    //
    // Remarks
    //   Method can be called from any thread.
    //
    virtual BOOL WINAPI MultiMonEnsureRectVisible(RECT* rect, BOOL partialOK) = 0;

    //
    // InstallWordBreakProc
    //   Installs special word break procedure to the specified window. This procedure
    //   is inteded for easier cursor movevement in the single line edit controls.
    //   Delimiters '\\', '/', ' ', ';', ',', and '.' are used as cursor stops when user
    //   navigates using Ctrl+Left or Ctrl+Right keys.
    //   You can use Ctrl+Backspace to delete one word.
    //
    // Parameters
    //   'hWindow'
    //      [in] Handle to the window or control where word break proc is to be isntalled.
    //      Window may be either edit or combo box with edit control.
    //
    // Return Values
    //   The return value is TRUE if the word break proc is installed. It is FALSE if the
    //   window is neither edit nor combo box with edit control, some error occured, or
    //   this special word break proc is not supported on your OS.
    //
    // Remarks
    //   You needn't uninstall word break procedure before window is destroyed.
    //
    //   Method can be called from any thread.
    //
    virtual BOOL WINAPI InstallWordBreakProc(HWND hWindow) = 0;

    // Salamander 3 or newer: returns TRUE if this Altap Salamander instance was
    // first to start (at instance startup time, other running instances
    // of version 3 or newer are searched for);
    //
    // Notes on different SID / Session / Integrity Level (doesn't apply to Salamander 2.5 and 2.51):
    // function returns TRUE even if Salamander instance is already running
    // under different SID; session and integrity level don't matter, so if Salamander
    // instance is already running on different session, or with different integrity level, but
    // with same SID, newly started instance returns FALSE
    //
    // can be called from any thread
    virtual BOOL WINAPI IsFirstInstance3OrLater() = 0;

    // support for parameter dependent strings (dealing with singles/plurals);
    // 'format' is format string for resulting string - its description follows;
    // resulting string is copied to 'buffer' buffer which size is 'bufferSize' bytes;
    // 'parametersArray' is array of parameters; 'parametersCount' is count of
    // these parameters; returns length of the resulting string
    //
    // format string description:
    //   - each format string starts with signature "{!}"
    //   - format string can contain following escape sequences (it allows to use special
    //     character without its special meaning): "\\" = "\", "\{" = "{", "\}" = "}",
    //     "\:" = ":", and "\|" = "|" (do not forget to double backslashes when writing C++
    //     strings, this applies only to format strings placed directly in C++ source code)
    //   - text which is not placed in curly brackets goes directly to resulting string
    //     (only escape sequences are handled)
    //   - parameter dependent text is placed in curly brackets
    //   - each parameter dependent text uses one parameter from 'parametersArray'
    //     (it is 64-bit unsigned int)
    //   - parameter dependent text contains more variants of resulting text, which variant
    //     is used depends on parameter value, more precisely to which defined interval the
    //     value belongs
    //   - variants of resulting text and interval bounds are separated by "|" character
    //   - first interval is from 0 to first interval bound
    //   - last interval is from last interval bound plus one to infinity (2^64-1)
    //   - parameter dependent text "{}" is used to skip one parameter from 'parametersArray'
    //     (nothing goes to resulting string)
    //   - you can also specify index of parameter to use for parameter dependent text,
    //     just place its index (from one to number of parameters) to the beginning of
    //     parameter dependent text and follow it by ':' character
    //   - if you don't specify index of parameter to use, it is assigned automatically
    //     (starting from one to number of parameters)
    //   - if you specify index of parameter to use, the next index which is assigned
    //     automatically is not affected,
    //     e.g. in "{!}%d file{2:s|0||1|s} and %d director{y|1|ies}" the first parameter
    //     dependent text uses parameter with index 2 and second uses parameter with index 1
    //   - you can use any number of parameter dependent texts with specified index
    //     of parameter to use
    //
    // examples of format strings:
    //   - "{!}director{y|1|ies}": for parameter values from 0 to 1 resulting string will be
    //     "directory" and for parameter values from 2 to infinity (2^64-1) resulting string
    //     will be "directories"
    //   - "{!}%d soubor{u|0||1|y|4|u} a %d adresar{u|0||1|e|4|u}": it needs two parameters
    //     because there are two dependent texts in curly brackets, resulting string for
    //     choosen pairs of parameters (I believe it is not needed to show all possible variants):
    //       0, 0: "%d souboru a %d adresaru"
    //       1, 12: "%d soubor a %d adresaru"
    //       3, 4: "%d soubory a %d adresare"
    //       13, 1: "%d souboru a %d adresar"
    //
    // method can be called from any thread
    virtual int WINAPI ExpandPluralString(char* buffer, int bufferSize, const char* format,
                                          int parametersCount, const CQuadWord* parametersArray) = 0;

    // in current Salamander language version prepares string "XXX (selected/hidden)
    // files and YYY (selected/hidden) directories"; if XXX ('files' parameter value)
    // or YYY ('dirs' parameter value) is zero, respective string part is omitted (both
    // parameters being zero is not considered); use of "selected" and "hidden" depends
    // on 'mode' - see epfdmXXX constants description; resulting text
    // is returned in buffer 'buffer' of size 'bufferSize' bytes; returns length of resulting
    // text; 'forDlgCaption' is TRUE/FALSE if text is/isn't intended for dialog caption
    // (capitalized first letters needed in English)
    // can be called from any thread
    virtual int WINAPI ExpandPluralFilesDirs(char* buffer, int bufferSize, int files, int dirs,
                                             int mode, BOOL forDlgCaption) = 0;

    // in current Salamander language version prepares string "BBB bytes in XXX selected
    // files and YYY selected directories"; BBB is 'selectedBytes' parameter value;
    // if XXX ('files' parameter value) or YYY ('dirs' parameter value) is zero,
    // respective string part is omitted (both parameters being zero is not considered);
    // if 'useSubTexts' is TRUE, BBB is enclosed in '<' and '>', so BBB can be
    // further processed on info-line (see CSalamanderGeneralAbstract::LookForSubTexts method and
    // CPluginDataInterfaceAbstract::GetInfoLineContent); resulting text is returned in buffer
    // 'buffer' of size 'bufferSize' bytes; returns length of resulting text
    // can be called from any thread
    virtual int WINAPI ExpandPluralBytesFilesDirs(char* buffer, int bufferSize,
                                                  const CQuadWord& selectedBytes, int files, int dirs,
                                                  BOOL useSubTexts) = 0;

    // returns string describing what is being worked with (e.g. "file "test.txt"" or "directory "test""
    // or "3 files and 1 directory"); 'sourceDescr' is buffer for result with size
    // at least 'sourceDescrSize'; 'panel' describes source panel of operation (one of PANEL_XXX or -1
    // if operation has no source panel (e.g. CPluginFSInterfaceAbstract::CopyOrMoveFromDiskToFS));
    // 'selectedFiles'+'selectedDirs' - if operation has source panel, this is count of selected
    // files and directories in source panel, if both values are zero, file/directory under cursor
    // (focus) is used; 'selectedFiles'+'selectedDirs' - if operation has no source panel, this is
    // count of files/directories the operation works with;
    // 'fileOrDirName'+'isDir' - used only if operation has no source panel and if
    // 'selectedFiles + selectedDirs == 1', contains file/directory name and whether it's file
    // or directory ('isDir' is FALSE or TRUE); 'forDlgCaption' is TRUE/FALSE if text is/isn't
    // intended for dialog caption (capitalized first letters needed in English)
    // limitation: main thread (may work with panel)
    virtual void WINAPI GetCommonFSOperSourceDescr(char* sourceDescr, int sourceDescrSize,
                                                   int panel, int selectedFiles, int selectedDirs,
                                                   const char* fileOrDirName, BOOL isDir,
                                                   BOOL forDlgCaption) = 0;

    // copies string 'srcStr' after string 'dstStr' (after its terminating null);
    // 'dstStr' is buffer of size 'dstBufSize' (must be at least 2);
    // if both strings don't fit in buffer, they are shortened (always so that
    // as many characters from both strings fit as possible)
    // can be called from any thread
    virtual void WINAPI AddStrToStr(char* dstStr, int dstBufSize, const char* srcStr) = 0;

    // determines if string 'fileNameComponent' can be used as name component
    // on Windows filesystem (handles strings longer than MAX_PATH-4 (4 = "C:\"
    // + null-terminator), empty string, strings of '.' chars, strings of white-spaces,
    // characters "*?\\/<>|\":" and simple names like "prn" and "prn  .txt")
    // can be called from any thread
    virtual BOOL WINAPI SalIsValidFileNameComponent(const char* fileNameComponent) = 0;

    // transforms string 'fileNameComponent' so it can be used as name component
    // on Windows filesystem (handles strings longer than MAX_PATH-4 (4 = "C:\"
    // + null-terminator), handles empty string, strings of '.' chars, strings of
    // white-spaces, replaces "*?\\/<>|\":" chars with '_' + simple names like "prn"
    // and "prn  .txt" get '_' appended to end of name); 'fileNameComponent' must be
    // expandable by at least one character (however at most MAX_PATH bytes from
    // 'fileNameComponent' are used)
    // can be called from any thread
    virtual void WINAPI SalMakeValidFileNameComponent(char* fileNameComponent) = 0;

    // returns TRUE if enumeration source is panel, in 'panel' then returns PANEL_LEFT or
    // PANEL_RIGHT; if enumeration source was not found or it's Find window, returns FALSE;
    // 'srcUID' is unique source identifier (passed as parameter when opening
    // viewer or can be obtained by calling GetPanelEnumFilesParams)
    // can be called from any thread
    virtual BOOL WINAPI IsFileEnumSourcePanel(int srcUID, int* panel) = 0;

    // returns next file name for viewer from source (left/right panel or Find windows);
    // 'srcUID' is unique source identifier (passed as parameter when opening
    // viewer or can be obtained by calling GetPanelEnumFilesParams); 'lastFileIndex'
    // (must not be NULL) is IN/OUT parameter, plugin should change it only if it wants to return
    // first file name, in this case set 'lastFileIndex' to -1; initial
    // 'lastFileIndex' value is passed as parameter when opening viewer and
    // when calling GetPanelEnumFilesParams; 'lastFileName' is full name of current file
    // (empty string if not known, e.g. if 'lastFileIndex' is -1); if
    // 'preferSelected' is TRUE and at least one name is selected, selected names are returned;
    // if 'onlyAssociatedExtensions' is TRUE, returns only files with extension associated with
    // this plugin's viewer (F3 on this file would try to open this plugin's viewer +
    // ignores potential shadowing by another plugin's viewer); 'fileName' is buffer
    // for obtained name (size at least MAX_PATH); returns TRUE if name is successfully
    // obtained; returns FALSE on error: no more file names in source (if 'noMoreFiles'
    // is not NULL, TRUE is returned in it), source is busy (not processing messages;
    // if 'srcBusy' is not NULL, TRUE is returned in it), otherwise source ceased to exist (path
    // change in panel, etc.);
    // can be called from any thread; WARNING: use from main thread doesn't make sense
    // (Salamander is busy during plugin method call, so always returns FALSE + TRUE
    // in 'srcBusy')
    virtual BOOL WINAPI GetNextFileNameForViewer(int srcUID, int* lastFileIndex, const char* lastFileName,
                                                 BOOL preferSelected, BOOL onlyAssociatedExtensions,
                                                 char* fileName, BOOL* noMoreFiles, BOOL* srcBusy) = 0;

    // returns previous file name for viewer from source (left/right panel or Find windows);
    // 'srcUID' is unique source identifier (passed as parameter when opening
    // viewer or can be obtained by calling GetPanelEnumFilesParams); 'lastFileIndex' (must
    // not be NULL) is IN/OUT parameter, plugin should change it only if it wants to return
    // last file name, in this case set 'lastFileIndex' to -1; initial
    // 'lastFileIndex' value is passed as parameter when opening viewer and when calling
    // GetPanelEnumFilesParams; 'lastFileName' is full name of current file (empty
    // string if not known, e.g. if 'lastFileIndex' is -1); if 'preferSelected'
    // is TRUE and at least one name is selected, selected names are returned; if
    // 'onlyAssociatedExtensions' is TRUE, returns only files with extension associated with
    // this plugin's viewer (F3 on this file would try to open this plugin's viewer +
    // ignores potential shadowing by another plugin's viewer); 'fileName' is buffer
    // for obtained name (size at least MAX_PATH); returns TRUE if name is successfully
    // obtained; returns FALSE on error: no previous file name in source (if 'noMoreFiles'
    // is not NULL, TRUE is returned in it), source is busy (not processing messages;
    // if 'srcBusy' is not NULL, TRUE is returned in it), otherwise source ceased to exist (path
    // change in panel, etc.)
    // can be called from any thread; WARNING: use from main thread doesn't make sense
    // (Salamander is busy during plugin method call, so always returns FALSE + TRUE
    // in 'srcBusy')
    virtual BOOL WINAPI GetPreviousFileNameForViewer(int srcUID, int* lastFileIndex, const char* lastFileName,
                                                     BOOL preferSelected, BOOL onlyAssociatedExtensions,
                                                     char* fileName, BOOL* noMoreFiles, BOOL* srcBusy) = 0;

    // determines if current file from viewer is selected in source (left/right
    // panel or Find windows); 'srcUID' is unique source identifier (passed as parameter
    // when opening viewer or can be obtained by calling GetPanelEnumFilesParams); 'lastFileIndex'
    // is parameter plugin should not change, initial 'lastFileIndex' value is passed
    // as parameter when opening viewer and when calling GetPanelEnumFilesParams;
    // 'lastFileName' is full name of current file; returns TRUE if it was possible to determine
    // if current file is selected, result is in 'isFileSelected' (must not be NULL);
    // returns FALSE on error: source ceased to exist (path change in panel, etc.) or file
    // 'lastFileName' is no longer in source (for these two errors, if 'srcBusy' is not NULL,
    // FALSE is returned in it), source is busy (not processing messages; for this error,
    // if 'srcBusy' is not NULL, TRUE is returned in it)
    // can be called from any thread; WARNING: use from main thread doesn't make sense
    // (Salamander is busy during plugin method call, so always returns FALSE + TRUE
    // in 'srcBusy')
    virtual BOOL WINAPI IsFileNameForViewerSelected(int srcUID, int lastFileIndex,
                                                    const char* lastFileName,
                                                    BOOL* isFileSelected, BOOL* srcBusy) = 0;

    // sets selection on current file from viewer in source (left/right
    // panel or Find windows); 'srcUID' is unique source identifier (passed as parameter
    // when opening viewer or can be obtained by calling GetPanelEnumFilesParams);
    // 'lastFileIndex' is parameter plugin should not change, initial
    // 'lastFileIndex' value is passed as parameter when opening viewer and when calling
    // GetPanelEnumFilesParams; 'lastFileName' is full name of current file; 'select'
    // is TRUE/FALSE if current file should be selected/deselected; returns TRUE on success;
    // returns FALSE on error: source ceased to exist (path change in panel, etc.) or
    // file 'lastFileName' is no longer in source (for these two errors, if 'srcBusy'
    // is not NULL, FALSE is returned in it), source is busy (not processing messages; for this
    // error, if 'srcBusy' is not NULL, TRUE is returned in it)
    // can be called from any thread; WARNING: use from main thread doesn't make sense
    // (Salamander is busy during plugin method call, so always returns FALSE + TRUE
    // in 'srcBusy')
    virtual BOOL WINAPI SetSelectionOnFileNameForViewer(int srcUID, int lastFileIndex,
                                                        const char* lastFileName, BOOL select,
                                                        BOOL* srcBusy) = 0;

    // returns reference to shared history (recently used values) of chosen combobox;
    // it's array of allocated strings; array has fixed number of strings, which is returned
    // in 'historyItemsCount' (must not be NULL); reference to array is returned in 'historyArr'
    // (must not be NULL); 'historyID' (one of SALHIST_XXX) determines which shared history reference
    // should be returned
    // limitation: main thread (shared histories cannot be used from other thread, access
    // to them is not synchronized)
    virtual BOOL WINAPI GetStdHistoryValues(int historyID, char*** historyArr, int* historyItemsCount) = 0;

    // adds allocated copy of new 'value' to shared history ('historyArr'+'historyItemsCount');
    // if 'caseSensitiveValue' is TRUE, value (string) is searched in history array
    // using case-sensitive comparison (FALSE = case-insensitive comparison),
    // found value is only moved to first position in history array
    // limitation: main thread (shared histories cannot be used from other thread, access
    // to them is not synchronized)
    // NOTE: if used for non-shared histories, can be called from any thread
    virtual void WINAPI AddValueToStdHistoryValues(char** historyArr, int historyItemsCount,
                                                   const char* value, BOOL caseSensitiveValue) = 0;

    // adds texts from shared history ('historyArr'+'historyItemsCount') to combobox ('combo');
    // resets combobox content before adding (see CB_RESETCONTENT)
    // limitation: main thread (shared histories cannot be used from other thread, access
    // to them is not synchronized)
    // NOTE: if used for non-shared histories, can be called from any thread
    virtual void WINAPI LoadComboFromStdHistoryValues(HWND combo, char** historyArr, int historyItemsCount) = 0;

    // determines color depth of current display and if more than 8-bit (256 colors), returns TRUE
    // can be called from any thread
    virtual BOOL WINAPI CanUse256ColorsBitmap() = 0;

    // checks if enabled-root-parent of window 'parent' is foreground window, if not,
    // FlashWindow(root-parent of window 'parent', TRUE) is called and root-parent of window 'parent'
    // is returned, otherwise NULL is returned
    // USAGE:
    //    HWND mainWnd = GetWndToFlash(parent);
    //    CDlg(parent).Execute();
    //    if (mainWnd != NULL) FlashWindow(mainWnd, FALSE);  // on W2K+ probably not needed anymore: flashing must be removed manually
    // can be called from any thread
    virtual HWND WINAPI GetWndToFlash(HWND parent) = 0;

    // reactivates drop-target (after drop during drag&drop) after opening our progress
    // window (which activates on open, deactivating drop-target); if 'dropTarget' is not
    // NULL and not a panel in this Salamander, activates 'progressWnd' and then
    // activates farthest enabled ancestor of 'dropTarget' (this combination removes activated
    // state without active application, which otherwise sometimes occurs)
    // can be called from any thread
    virtual void WINAPI ActivateDropTarget(HWND dropTarget, HWND progressWnd) = 0;

    // schedules opening of Pack dialog with selected packer from this plugin (see
    // CSalamanderConnectAbstract::AddCustomPacker), if packer from this plugin
    // doesn't exist (e.g. because user deleted it), error message is displayed to user;
    // dialog opens when there are no messages in main thread's message-queue
    // and Salamander is not "busy" (no modal dialog is open and no message is being
    // processed); repeated calls to this method before Pack dialog opens
    // only change 'delFilesAfterPacking' parameter;
    // 'delFilesAfterPacking' affects "Delete files after packing" checkbox
    // in Pack dialog: 0=default, 1=checked, 2=unchecked
    // limitation: main thread
    virtual void WINAPI PostOpenPackDlgForThisPlugin(int delFilesAfterPacking) = 0;

    // schedules opening of Unpack dialog with selected unpacker from this plugin (see
    // CSalamanderConnectAbstract::AddCustomUnpacker), if unpacker from this plugin
    // doesn't exist (e.g. because user deleted it), error message is displayed to user;
    // dialog opens when there are no messages in main thread's message-queue
    // and Salamander is not "busy" (no modal dialog is open and no message is being
    // processed); repeated calls to this method before Unpack dialog opens
    // only change 'unpackMask' parameter;
    // 'unpackMask' affects "Unpack files" mask: NULL=default, otherwise mask text
    // limitation: main thread
    virtual void WINAPI PostOpenUnpackDlgForThisPlugin(const char* unpackMask) = 0;

    // creates file with name 'fileName' via classic Win32 API call
    // CreateFile (lpSecurityAttributes==NULL, dwCreationDisposition==CREATE_NEW,
    // hTemplateFile==NULL); this method handles 'fileName' collision with DOS name
    // of already existing file/directory (only if it's not also collision with long
    // file/directory name) - ensures DOS name change so file with
    // name 'fileName' can be created (method: temporarily renames conflicting
    // file/directory to different name and after creating 'fileName' renames it back);
    // returns file handle or INVALID_HANDLE_VALUE on error (returns Windows
    // error code in 'err' (if not NULL))
    // can be called from any thread
    virtual HANDLE WINAPI SalCreateFileEx(const char* fileName, DWORD desiredAccess, DWORD shareMode,
                                          DWORD flagsAndAttributes, DWORD* err) = 0;

    // creates directory with name 'name' via classic Win32 API call
    // CreateDirectory(lpSecurityAttributes==NULL); this method handles 'name'
    // collision with DOS name of already existing file/directory (only if it's not
    // also collision with long file/directory name) - ensures DOS name change
    // so directory with name 'name' can be created (method: temporarily renames
    // conflicting file/directory to different name and after creating 'name'
    // renames it back); also handles names ending with spaces (can create them, unlike
    // CreateDirectory, which silently trims spaces and thus creates different
    // directory); returns TRUE on success, FALSE on error (returns Windows
    // error code in 'err' (if not NULL))
    // can be called from any thread
    virtual BOOL WINAPI SalCreateDirectoryEx(const char* name, DWORD* err) = 0;

    // allows disconnecting/connecting change monitoring (only for Windows paths and archive paths)
    // on paths browsed in one of panels; purpose: if your code (disk formatting,
    // disk shredding, etc.) is hindered by panel having "ChangeNotification" handle open for path,
    // you can temporarily disconnect it with this method (refresh for path in panel
    // is triggered after connecting); 'panel' is one of PANEL_XXX; 'stopMonitoring'
    // is TRUE/FALSE (disconnect/connect)
    // limitation: main thread
    virtual void WINAPI PanelStopMonitoring(int panel, BOOL stopMonitoring) = 0;

    // allocates a new CSalamanderDirectory object for working with files/directories in an archive or
    // file-system; if 'isForFS' is TRUE, the object is preset for use with a file-system,
    // otherwise the object is preset for use with an archive (default object flags differ
    // for archive and file-system, see method CSalamanderDirectoryAbstract::SetFlags)
    // can be called from any thread
    virtual CSalamanderDirectoryAbstract* WINAPI AllocSalamanderDirectory(BOOL isForFS) = 0;

    // frees a CSalamanderDirectory object (obtained via AllocSalamanderDirectory method,
    // WARNING: must not be called for any other CSalamanderDirectoryAbstract pointer)
    // can be called from any thread
    virtual void WINAPI FreeSalamanderDirectory(CSalamanderDirectoryAbstract* salDir) = 0;

    // adds a new timer for a plugin FS object; when the timer times out, the method
    // CPluginFSInterfaceAbstract::Event() of plugin FS object 'timerOwner' is called with parameters
    // FSE_TIMER and 'timerParam'; 'timeout' is the timer timeout from its addition (in milliseconds,
    // must be >= 0); the timer is canceled at the moment of its timeout (before calling
    // CPluginFSInterfaceAbstract::Event()) or when the plugin FS object is closed;
    // returns TRUE if the timer was successfully added
    // limitation: main thread
    virtual BOOL WINAPI AddPluginFSTimer(int timeout, CPluginFSInterfaceAbstract* timerOwner,
                                         DWORD timerParam) = 0;

    // cancels either all timers of plugin FS object 'timerOwner' (if 'allTimers' is TRUE)
    // or only all timers with parameter equal to 'timerParam' (if 'allTimers' is FALSE);
    // returns the number of canceled timers
    // limitation: main thread
    virtual int WINAPI KillPluginFSTimer(CPluginFSInterfaceAbstract* timerOwner, BOOL allTimers,
                                         DWORD timerParam) = 0;

    // queries the visibility of the FS item in Change Drive menu and in Drive bars; returns TRUE
    // if the item is visible, otherwise returns FALSE
    // limitation: main thread (otherwise changes in plugin configuration may occur during the call)
    virtual BOOL WINAPI GetChangeDriveMenuItemVisibility() = 0;

    // sets the visibility of the FS item in Change Drive menu and in Drive bars; use
    // only during plugin installation (otherwise user-chosen visibility may be overwritten);
    // 'visible' is TRUE if the item should be visible
    // limitation: main thread (otherwise changes in plugin configuration may occur during the call)
    virtual void WINAPI SetChangeDriveMenuItemVisibility(BOOL visible) = 0;

    // Sets a breakpoint on the x-th COM/OLE allocation. Used to find COM/OLE leaks.
    // Does nothing in the release version of Salamander. Debug version of Salamander
    // displays the list of COM/OLE leaks to the debugger Debug window and to Trace Server upon exit.
    // In square brackets is the allocation order, which we pass as 'alloc' to the
    // OleSpySetBreak call. Can be called from any thread.
    virtual void WINAPI OleSpySetBreak(int alloc) = 0;

    // Returns copies of icons that Salamander uses in panels. 'icon' specifies the icon and is
    // one of the SALICON_xxx values. 'iconSize' specifies what size the returned icon should have
    // and is one of the SALICONSIZE_xxx values.
    // On success, returns the handle of the created icon. The plugin must ensure icon destruction
    // by calling the API DestroyIcon. On failure, returns NULL.
    // limitation: main thread
    virtual HICON WINAPI GetSalamanderIcon(int icon, int iconSize) = 0;

    // GetFileIcon
    //   Function retrieves handle to large or small icon from the specified object,
    //   such as a file, a folder, a directory, or a drive root.
    //
    // Parameters
    //   'path'
    //      [in] Pointer to a null-terminated string that contains the path and file
    //      name. If the 'pathIsPIDL' parameter is TRUE, this parameter must be the
    //      address of an ITEMIDLIST (PIDL) structure that contains the list of item
    //      identifiers that uniquely identify the file within the Shell's namespace.
    //      The PIDL must be a fully qualified PIDL. Relative PIDLs are not allowed.
    //
    //   'pathIsPIDL'
    //      [in] Indicate that 'path' is the address of an ITEMIDLIST structure rather
    //      than a path name.
    //
    //   'hIcon'
    //      [out] Pointer to icon handle that receive handle to the icon extracted
    //      from the object.
    //
    //   'iconSize'
    //      [in] Required size of icon. SALICONSIZE_xxx
    //
    //   'fallbackToDefIcon'
    //      [in] Value specifying whether the default (simple) icon should be used if
    //      the icon of the specified object is not available. If this parameter is
    //      TRUE, function tries to return the default (simple) icon in this situation.
    //      Otherwise, it returns no icon (return value is FALSE).
    //
    //   'defIconIsDir'
    //      [in] Specifies whether the default (simple) icon for 'path' is icon of
    //      directory. This parameter is ignored unless 'fallbackToDefIcon' is TRUE.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    // Remarks
    //   You are responsible for freeing returned icons with DestroyIcon when you
    //   no longer need them.
    //
    //   You must initialize COM with CoInitialize or OLEInitialize prior to
    //   calling GetFileIcon.
    //
    //   Method can be called from any thread.
    //
    virtual BOOL WINAPI GetFileIcon(const char* path, BOOL pathIsPIDL,
                                    HICON* hIcon, int iconSize, BOOL fallbackToDefIcon,
                                    BOOL defIconIsDir) = 0;

    // FileExists
    //   Function checks the existence of a file. It returns TRUE if the specified
    //   file exists. If the file does not exist, it returns 0. FileExists only checks
    //   the existence of files, directories are ignored.
    // can be called from any thread
    virtual BOOL WINAPI FileExists(const char* fileName) = 0;

    // changes the path in the panel to the last known disk path, if not accessible,
    // changes to the user-chosen "rescue" path (see
    // SALCFG_IFPATHISINACCESSIBLEGOTO) and if that also fails, to the root of the first local
    // fixed drive (Salamander 2.5 and 2.51 only changes to the root of the first local fixed drive);
    // used for closing a file-system in the panel (disconnect); 'parent' is the parent of any
    // message boxes; 'panel' is one of PANEL_XXX
    // limitation: main thread + outside CPluginFSInterfaceAbstract and CPluginDataInterfaceAbstract methods
    // (risk e.g. closing FS opened in panel - 'this' could cease to exist for the method)
    virtual void WINAPI DisconnectFSFromPanel(HWND parent, int panel) = 0;

    // returns TRUE if the file name 'name' is associated in Archives Associations in Panels
    // to the calling plugin
    // 'name' must be only the file name, not with full or relative path
    // limitation: main thread
    virtual BOOL WINAPI IsArchiveHandledByThisPlugin(const char* name) = 0;

    // serves as LR_xxx parameter for the API function LoadImage()
    // if the user doesn't have hi-color icons enabled in desktop configuration,
    // returns LR_VGACOLOR to avoid incorrect loading of a more colorful version of the icon
    // otherwise returns 0 (LR_DEFAULTCOLOR); the function result can be OR-ed with other LR_xxx flags
    // can be called from any thread
    virtual DWORD WINAPI GetIconLRFlags() = 0;

    // determines based on file extension whether it's a link ("lnk", "pif" or "url"); 'fileExtension'
    // is the file extension (pointer after the dot), must not be NULL; returns 1 if it's a link, otherwise
    // returns 0; NOTE: used for filling CFileData::IsLink
    // can be called from any thread
    virtual int WINAPI IsFileLink(const char* fileExtension) = 0;

    // returns ILC_COLOR??? based on Windows version - tuned for use of imagelists in listviews
    // typical usage: ImageList_Create(16, 16, ILC_MASK | GetImageListColorFlags(), ???, ???)
    // can be called from any thread
    virtual DWORD WINAPI GetImageListColorFlags() = 0;

    // "safe" version of GetOpenFileName()/GetSaveFileName() handles the situation when the provided path
    // in OPENFILENAME::lpstrFile is not valid (for example z:\); in this case the std. API version
    // of the function doesn't open the dialog and silently returns FALSE and CommDlgExtendedError() returns FNERR_INVALIDFILENAME.
    // The following two functions in this case call the API again, but with a "safely"
    // existing path (Documents, or Desktop).
    virtual BOOL WINAPI SafeGetOpenFileName(LPOPENFILENAME lpofn) = 0;
    virtual BOOL WINAPI SafeGetSaveFileName(LPOPENFILENAME lpofn) = 0;

    // plugin must provide Salamander with the name of its .chm file before using OpenHtmlHelp()
    // without path (e.g. "demoplug.chm")
    // can be called from any thread, but concurrent calls with OpenHtmlHelp() must be avoided
    virtual void WINAPI SetHelpFileName(const char* chmName) = 0;

    // opens the plugin's HTML help, selects the help language (directory with .chm files) as follows:
    // -directory obtained from current Salamander .slg file (see SLGHelpDir in shared\versinfo.rc)
    // -HELP\ENGLISH\*.chm
    // -first found subdirectory in the HELP subdirectory
    // plugin must call SetHelpFileName() before using OpenHtmlHelp(); 'parent' is the parent
    // of the error message box; 'command' is the HTML help command, see HHCDisplayXXX; 'dwData' is the parameter
    // of the HTML help command, see HHCDisplayXXX
    // can be called from any thread
    // note: for displaying Salamander's help see OpenHtmlHelpForSalamander
    virtual BOOL WINAPI OpenHtmlHelp(HWND parent, CHtmlHelpCommand command, DWORD_PTR dwData,
                                     BOOL quiet) = 0;

    // returns TRUE if paths 'path1' and 'path2' are on the same volume; in 'resIsOnlyEstimation'
    // (if not NULL) returns TRUE if the result is not certain (certain only in case of path match or
    // if "volume name" (volume GUID) can be obtained for both paths, which is only possible for
    // local paths under W2K or newer NT family)
    // can be called from any thread
    virtual BOOL WINAPI PathsAreOnTheSameVolume(const char* path1, const char* path2,
                                                BOOL* resIsOnlyEstimation) = 0;

    // reallocates memory on Salamander's heap (unnecessary when using salrtl9.dll - standard realloc suffices);
    // on insufficient memory displays a message to the user with Retry and Cancel buttons (after another prompt
    // terminates the application)
    // can be called from any thread
    virtual void* WINAPI Realloc(void* ptr, int size) = 0;

    // returns in 'enumFilesSourceUID' (must not be NULL) a unique source identifier for panel
    // 'panel' (one of PANEL_XXX), used in viewers when enumerating files
    // from the panel (see parameter 'srcUID' e.g. in method GetNextFileNameForViewer), this
    // identifier changes e.g. when the path in the panel changes; if 'enumFilesCurrentIndex' is not
    // NULL, returns the index of the focused file (if there's no focused file, returns -1);
    // limitation: main thread (otherwise panel content may change)
    virtual void WINAPI GetPanelEnumFilesParams(int panel, int* enumFilesSourceUID,
                                                int* enumFilesCurrentIndex) = 0;

    // posts a message to the panel with active FS 'modifiedFS' that a path refresh should be
    // performed (reloads listing and transfers selection, icons, focus, etc. to
    // the new panel content); refresh is performed when the Salamander main window is activated
    // (after suspend-mode ends); FS path is always reloaded; if 'modifiedFS' is not in any
    // panel, nothing is performed; if 'focusFirstNewItem' is TRUE and only a single
    // item was added to the panel, that new item is focused (used e.g. for focusing a newly created
    // file/directory); returns TRUE if refresh was performed, FALSE if 'modifiedFS' was not
    // found in either panel
    // can be called from any thread (if the main thread is not running code inside a plugin,
    // refresh happens as soon as possible, otherwise refresh waits at least until the main
    // thread leaves the plugin)
    virtual BOOL WINAPI PostRefreshPanelFS2(CPluginFSInterfaceAbstract* modifiedFS,
                                            BOOL focusFirstNewItem = FALSE) = 0;

    // loads text with ID 'resID' from module 'module' resources; returns text in internal buffer (risk of
    // text change due to internal buffer change caused by subsequent LoadStr calls from other
    // plugins or Salamander; buffer is 10000 characters large, overwrite risk only after it's
    // filled (used cyclically); if you need to use the text later, we recommend
    // copying it to a local buffer); if 'module' is NULL or 'resID' is not in the module,
    // returns text "ERROR LOADING STRING" (and debug/SDK version outputs TRACE_E)
    // can be called from any thread
    virtual char* WINAPI LoadStr(HINSTANCE module, int resID) = 0;

    // loads text with ID 'resID' from module 'module' resources; returns text in internal buffer (risk of
    // text change due to internal buffer change caused by subsequent LoadStrW calls from other
    // plugins or Salamander; buffer is 10000 characters large, overwrite risk only after it's
    // filled (used cyclically); if you need to use the text later, we recommend
    // copying it to a local buffer); if 'module' is NULL or 'resID' is not in the module,
    // returns text L"ERROR LOADING WIDE STRING" (and debug/SDK version outputs TRACE_E)
    // can be called from any thread
    virtual WCHAR* WINAPI LoadStrW(HINSTANCE module, int resID) = 0;

    // changes the path in the panel to the user-chosen "rescue" path (see
    // SALCFG_IFPATHISINACCESSIBLEGOTO) and if that also fails, to the root of the first local fixed
    // drive, this is an almost certain change of the current path in the panel; 'panel' is one of PANEL_XXX;
    // if 'failReason' is not NULL, it is set to one of the CHPPFR_XXX constants (informs about method result);
    // returns TRUE if the path change succeeded (to "rescue" or fixed drive)
    // limitation: main thread + outside CPluginFSInterfaceAbstract and CPluginDataInterfaceAbstract methods
    // (risk e.g. closing FS opened in panel - 'this' could cease to exist for the method)
    virtual BOOL WINAPI ChangePanelPathToRescuePathOrFixedDrive(int panel, int* failReason = NULL) = 0;

    // registers the plugin as a replacement for the Network item in Change Drive menu and in Drive bars,
    // plugin must add a file-system to Salamander on which incomplete
    // UNC paths ("\\" and "\\server") from the Change Directory command are then opened and to which you go
    // via the up-dir symbol ("..") from the root of UNC paths;
    // limitation: call only from the plugin's entry-point and only after SetBasicPluginData
    virtual void WINAPI SetPluginIsNethood() = 0;

    // opens system context menu for selected items or focused item on network path
    // ('forItems' is TRUE) or for network path ('forItems' is FALSE), also executes
    // the selected command from the menu; menu is obtained by traversing the CSIDL_NETWORK folder; 'parent' is the suggested parent
    // of the context menu; 'panel' identifies the panel (PANEL_LEFT or PANEL_RIGHT), for which
    // the context menu should be opened (focused/selected files/directories are obtained from this panel
    // to work with); 'menuX' + 'menuY' are suggested coordinates of the top-left corner
    // of the context menu; 'netPath' is network path, only "\\" and "\\server" are allowed; if
    // 'newlyMappedDrive' is not NULL, it returns the letter ('A' to 'Z') of the newly mapped drive (via
    // Map Network Drive command from context menu), if it returns zero, no new mapping occurred
    // limitation: main thread
    virtual void WINAPI OpenNetworkContextMenu(HWND parent, int panel, BOOL forItems, int menuX,
                                               int menuY, const char* netPath,
                                               char* newlyMappedDrive) = 0;

    // duplicates '\\' - useful for texts that we send to LookForSubTexts, which '\\\\'
    // reduces back to '\\'; 'buffer' is input/output string, 'bufferSize' is size
    // of 'buffer' in bytes; returns TRUE if duplication did not cause loss of characters from end of string
    // (buffer was large enough)
    // can be called from any thread
    virtual BOOL WINAPI DuplicateBackslashes(char* buffer, int bufferSize) = 0;

    // shows in panel 'panel' a throbber (animation informing the user about activity related
    // to the panel, e.g. "loading data from network") with delay 'delay' (in ms); 'panel' is one
    // of PANEL_XXX; if 'tooltip' is not NULL, it's the text shown when hovering mouse over
    // the throbber (if NULL, no text is shown); if a throbber is already displayed in the panel
    // or waiting to be displayed, its identification number and tooltip are changed (if displayed,
    // 'delay' is ignored, if waiting to be displayed, new delay is set according to 'delay');
    // returns the throbber identification number (never -1, so -1 can be used as
    // empty value + all returned numbers are unique, more precisely they start repeating
    // after unrealistic 2^32 throbber displays);
    // NOTE: a suitable place to display throbber for FS is receiving the FSE_PATHCHANGED event,
    // at that point FS is in the panel (whether throbber should or shouldn't be displayed can be determined beforehand
    // in ChangePath or ListCurrentPath)
    // limitation: main thread
    virtual int WINAPI StartThrobber(int panel, const char* tooltip, int delay) = 0;

    // hides the throbber with identification number 'id'; returns TRUE if the throbber
    // is hidden; returns FALSE if this throbber has already been hidden or another
    // throbber was displayed over it;
    // NOTE: throbber is automatically hidden just before path change in panel or
    // before refresh (for FS this means right after successful ListCurrentPath call, for archives
    // it's after opening and listing the archive, for disks it's after verifying path accessibility)
    // limitation: main thread
    virtual BOOL WINAPI StopThrobber(int id) = 0;

    // shows in panel 'panel' a security icon (locked or unlocked padlock, e.g. for FTPS informs
    // the user that the connection to server is secured via SSL and server identity is
    // verified (locked padlock) or not verified (unlocked padlock)); 'panel' is one of PANEL_XXX;
    // if 'showIcon' is TRUE, the icon is shown, otherwise hidden; 'isLocked' determines whether it's
    // a locked (TRUE) or unlocked (FALSE) padlock; if 'tooltip' is not NULL, it's the text shown
    // when hovering mouse over the icon (if NULL, no text is shown); if an action should be performed
    // on security icon click (e.g. for FTPS a dialog with server certificate is displayed),
    // it must be added to the CPluginFSInterfaceAbstract::ShowSecurityInfo method of the file-system
    // displayed in the panel;
    // NOTE: a suitable place to display security icon for FS is receiving the
    // FSE_PATHCHANGED event, at that point FS is in the panel (whether icon should or shouldn't be displayed can be determined
    // beforehand in ChangePath or ListCurrentPath)
    // NOTE: security icon is automatically hidden just before path change in panel or
    // before refresh (for FS this means right after successful ListCurrentPath call, for archives
    // it's after opening and listing the archive, for disks it's after verifying path accessibility)
    // limitation: main thread
    virtual void WINAPI ShowSecurityIcon(int panel, BOOL showIcon, BOOL isLocked,
                                         const char* tooltip) = 0;

    // removes current path in panel from directory history displayed in panel (Alt+Left/Right)
    // and from working paths list (Alt+F12); used to make transitional paths invisible,
    // e.g. "net:\Entire Network\Microsoft Windows Network\WORKGROUP\server\share" automatically
    // transitions to "\\server\share" and it's undesirable to make this transition when navigating history
    // limitation: main thread
    virtual void WINAPI RemoveCurrentPathFromHistory(int panel) = 0;

    // returns TRUE if current user is a member of the Administrators group, otherwise returns FALSE
    // can be called from any thread
    virtual BOOL WINAPI IsUserAdmin() = 0;

    // returns TRUE if Salamander is running on remote desktop (RemoteDesktop), otherwise returns FALSE
    // can be called from any thread
    virtual BOOL WINAPI IsRemoteSession() = 0;

    // equivalent to calling WNetAddConnection2(lpNetResource, NULL, NULL, CONNECT_INTERACTIVE);
    // advantage is more detailed display of error states (e.g. password expired,
    // wrong password or name, password needs to be changed, etc.)
    // can be called from any thread
    virtual DWORD WINAPI SalWNetAddConnection2Interactive(LPNETRESOURCE lpNetResource) = 0;

    //
    // GetMouseWheelScrollChars
    //   An OS independent method to retrieve the number of wheel scroll chars.
    //
    // Return Values
    //   Number of scroll characters where WHEEL_PAGESCROLL (0xffffffff) indicates to scroll a page at a time.
    //
    // Remarks
    //   Method can be called from any thread.
    virtual DWORD WINAPI GetMouseWheelScrollChars() = 0;

    //
    // GetSalamanderZLIB
    //   Provides simplified interface to the ZLIB library provided by Salamander,
    //   for details see spl_zlib.h.
    //
    // Remarks
    //   Method can be called from any thread.
    virtual CSalamanderZLIBAbstract* WINAPI GetSalamanderZLIB() = 0;

    //
    // GetSalamanderPNG
    //   Provides interface to the PNG library provided by Salamander.
    //
    // Remarks
    //   Method can be called from any thread.
    virtual CSalamanderPNGAbstract* WINAPI GetSalamanderPNG() = 0;

    //
    // GetSalamanderCrypt
    //   Provides interface to encryption libraries provided by Salamander,
    //   for details see spl_crypt.h.
    //
    // Remarks
    //   Method can be called from any thread.
    virtual CSalamanderCryptAbstract* WINAPI GetSalamanderCrypt() = 0;

    // informs Salamander that the plugin uses Password Manager and therefore Salamander should
    // report to the plugin the setting/change/removal of master password (see
    // CPluginInterfaceAbstract::PasswordManagerEvent)
    // limitation: call only from the plugin's entry-point and only after SetBasicPluginData
    virtual void WINAPI SetPluginUsesPasswordManager() = 0;

    //
    // GetSalamanderPasswordManager
    //   Provides interface to Password Manager provided by Salamander.
    //
    // Remarks
    //   Method can be called from main thread only.
    virtual CSalamanderPasswordManagerAbstract* WINAPI GetSalamanderPasswordManager() = 0;

    // opens HTML help for Salamander itself (instead of plugin help, which opens via OpenHtmlHelp()),
    // selects the help language (directory with .chm files) as follows:
    // -directory obtained from current Salamander .slg file (see SLGHelpDir in shared\versinfo.rc)
    // -HELP\ENGLISH\*.chm
    // -first found subdirectory in the HELP subdirectory
    // 'parent' is the parent of the error message box; 'command' is the HTML help command, see HHCDisplayXXX;
    // 'dwData' is the parameter of the HTML help command, see HHCDisplayXXX; if command==HHCDisplayContext,
    // the 'dwData' value must be from the HTMLHELP_SALID_XXX family of constants
    // can be called from any thread
    virtual BOOL WINAPI OpenHtmlHelpForSalamander(HWND parent, CHtmlHelpCommand command, DWORD_PTR dwData, BOOL quiet) = 0;

    //
    // GetSalamanderBZIP2
    //   Provides simplified interface to the BZIP2 library provided by Salamander,
    //   for details see spl_bzip2.h.
    //
    // Remarks
    //   Method can be called from any thread.
    virtual CSalamanderBZIP2Abstract* WINAPI GetSalamanderBZIP2() = 0;

    //
    // GetFocusedItemMenuPos
    //   Returns point (in screen coordinates) where the context menu for focused item in the
    //   active panel should be displayed. The upper left corner of the panel is returned when
    //   focused item is not visible
    //
    // Remarks
    //   Method can be called from main thread only.
    virtual void WINAPI GetFocusedItemMenuPos(POINT* pos) = 0;

    //
    // LockMainWindow
    //   Locks main window to pretend it is disabled. Main windows is still able to receive focus
    //   in the locked state. Set 'lock' to TRUE to lock main window and to FALSE to revert it back
    //   to normal state. 'hToolWnd' is reserverd parameter, set it to NULL. 'lockReason' is (optional,
    //   can be NULL) describes the reason for main window locked state. It will be displayed during
    //   attempt to close locked main window; content of string is copied to internal structure
    //   so buffer can be deallocated after return from LockMainWindow().
    //
    // Remarks
    //   Method can be called from main thread only.
    virtual void WINAPI LockMainWindow(BOOL lock, HWND hToolWnd, const char* lockReason) = 0;

    // only for "dynamic menu extension" plugins (see FUNCTION_DYNAMICMENUEXT):
    // sets a flag for the calling plugin that the menu should be rebuilt at the nearest opportunity
    // (as soon as there are no messages in the main thread message-queue and Salamander is not
    // "busy" (no modal dialog is open and no message is being processed))
    // by calling the CPluginInterfaceForMenuExtAbstract::BuildMenu method;
    // WARNING: if called from a thread other than main, BuildMenu may be called
    // (runs in main thread) even before PostPluginMenuChanged finishes
    // can be called from any thread
    virtual void WINAPI PostPluginMenuChanged() = 0;

    //
    // GetMenuItemHotKey
    //   Search through plugin's menu items added with AddMenuItem() for item with 'id'.
    //   When such item is found, its 'hotKey' and 'hotKeyText' (up to 'hotKeyTextSize' characters)
    //   is set. Both 'hotKey' and 'hotKeyText' could be NULL.
    //   Returns TRUE when item with 'id' is found, otherwise returns FALSE.
    //
    //   Remarks
    //   Method can be called from main thread only.
    virtual BOOL WINAPI GetMenuItemHotKey(int id, WORD* hotKey, char* hotKeyText, int hotKeyTextSize) = 0;

    // our variants of RegQueryValue and RegQueryValueEx functions, unlike API variants
    // ensure adding null-terminator for types REG_SZ, REG_MULTI_SZ and REG_EXPAND_SZ
    // WARNING: when determining required buffer size, returns one or two (two
    //        only for REG_MULTI_SZ) characters more in case the string needs to be
    //        terminated with null(s)
    // can be called from any thread
    virtual LONG WINAPI SalRegQueryValue(HKEY hKey, LPCSTR lpSubKey, LPSTR lpData, PLONG lpcbData) = 0;
    virtual LONG WINAPI SalRegQueryValueEx(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved,
                                           LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData) = 0;

    // because the Windows version of GetFileAttributes cannot work with names ending with space,
    // we wrote our own (for these names it adds backslash at the end, which makes
    // GetFileAttributes work correctly, but only for directories, for files with space at
    // the end we have no solution, but at least it's not detected from another file - Windows version
    // trims spaces and thus works with a different file/directory)
    // can be called from any thread
    virtual DWORD WINAPI SalGetFileAttributes(const char* fileName) = 0;

    // there's no Win32 API for SSD detection yet, so detection is done heuristically
    // based on querying support for TRIM, StorageDeviceSeekPenaltyProperty, etc.
    // function returns TRUE if disk at path 'path' appears to be SSD; FALSE otherwise
    // result is not 100%, people report algorithm not working e.g. on SSD PCIe cards:
    // http://stackoverflow.com/questions/23363115/detecting-ssd-in-windows/33359142#33359142
    // can determine correct data even for paths containing substs and reparse points under Windows
    // 2000/XP/Vista (Salamander 2.5 works only with junction-points); 'path' is the path for which
    // we're determining information; if path goes through a network path, silently returns FALSE
    // can be called from any thread
    virtual BOOL WINAPI IsPathOnSSD(const char* path) = 0;

    // returns TRUE if it's a UNC path (detects both formats: \\server\share and \\?\UNC\server\share)
    // can be called from any thread
    virtual BOOL WINAPI IsUNCPath(const char* path) = 0;

    // replaces substs in path 'resPath' with their target paths (conversion to path without SUBST drive-letters);
    // 'resPath' must point to a buffer of at least 'MAX_PATH' characters
    // returns TRUE on success, FALSE on error
    // can be called from any thread
    virtual BOOL WINAPI ResolveSubsts(char* resPath) = 0;

    // call only for paths 'path' whose root (after removing subst) is DRIVE_FIXED (elsewhere there's no point looking for
    // reparse points); we're looking for a path without reparse points, leading to the same volume as 'path'; for a path
    // containing a symlink leading to a network path (UNC or mapped) we return only the root of this network path
    // (even Vista cannot work with reparse points on network paths, so it's probably not worth bothering);
    // if such path doesn't exist because the current (last) local reparse point is a volume mount
    // point (or unknown type of reparse point), we return the path to this volume mount point (or reparse
    // point of unknown type); if the path contains more than 50 reparse points (probably an infinite loop),
    // we return the original path;
    //
    // 'resPath' is a buffer for result of size MAX_PATH; 'path' is the original path; in 'cutResPathIsPossible'
    // (must not be NULL) we return FALSE if the resulting path in 'resPath' contains a reparse point at the end (volume
    // mount point or unknown type of reparse point) and thus we must not shorten it (we would likely get
    // to a different volume); if 'rootOrCurReparsePointSet' is non-NULL and contains FALSE and there is
    // at least one local reparse point on the original path (we ignore reparse points on network part of path), we return
    // TRUE in this variable + in 'rootOrCurReparsePoint' (if not NULL) we return full path to the current (last
    // local) reparse point (note, not where it leads); target path of current reparse point (only if it's
    // a junction or symlink) is returned in 'junctionOrSymlinkTgt' (if not NULL) + type is returned in 'linkType':
    // 2 (JUNCTION POINT), 3 (SYMBOLIC LINK); in 'netPath' (if not NULL) we return network path to which
    // the current (last) local symlink in path leads - in this situation root of network path is returned in 'resPath'
    // can be called from any thread
    virtual void WINAPI ResolveLocalPathWithReparsePoints(char* resPath, const char* path,
                                                          BOOL* cutResPathIsPossible,
                                                          BOOL* rootOrCurReparsePointSet,
                                                          char* rootOrCurReparsePoint,
                                                          char* junctionOrSymlinkTgt, int* linkType,
                                                          char* netPath) = 0;

    // Performs resolve of substs and reparse points for path 'path', then for the mount-point of the path
    // (if missing then for path root) tries to obtain GUID path. On failure returns FALSE. On
    // success, returns TRUE and sets 'mountPoint' and 'guidPath' (if different from NULL, they must
    // point to buffers of at least MAX_PATH size; strings will be terminated with backslash).
    // can be called from any thread
    virtual BOOL WINAPI GetResolvedPathMountPointAndGUID(const char* path, char* mountPoint, char* guidPath) = 0;

    // replaces in string the last '.' character with decimal separator obtained from system LOCALE_SDECIMAL
    // string length may grow, because separator can have up to 4 characters according to MSDN
    // returns TRUE if buffer was large enough and operation completed, otherwise returns FALSE
    // can be called from any thread
    virtual BOOL WINAPI PointToLocalDecimalSeparator(char* buffer, int bufferSize) = 0;

    // sets the icon-overlays array for this plugin; after setting, the plugin can return
    // icon-overlay index in listings (see CFileData::IconOverlayIndex), which should be displayed over the icon
    // of the listing item, this way up to 15 icon-overlays can be used (indexes 0 to 14, because
    // index 15=ICONOVERLAYINDEX_NOTUSED meaning: don't display icon-overlay); 'iconOverlaysCount'
    // is the number of icon-overlays for the plugin; array 'iconOverlays' contains for each icon-overlay
    // sequentially all icon sizes: SALICONSIZE_16, SALICONSIZE_32 and SALICONSIZE_48 - so
    // array 'iconOverlays' has 3 * 'iconOverlaysCount' icons; freeing icons in array 'iconOverlays'
    // is handled by Salamander (calls DestroyIcon()), the array itself is the caller's responsibility, if
    // there are any NULLs in the array (e.g. icon load failed), the function fails, but frees valid icons from array;
    // when system colors change, the plugin should reload icon-overlays and set them again
    // with this function, ideal is reaction to PLUGINEVENT_COLORSCHANGED in function
    // CPluginInterfaceAbstract::Event()
    // WARNING: before Windows XP (in W2K) icon size SALICONSIZE_48 is only 32 pixels!
    // limitation: main thread
    virtual void WINAPI SetPluginIconOverlays(int iconOverlaysCount, HICON* iconOverlays) = 0;

    // description see SalGetFileSize(), first difference is that file is specified by full path;
    // second is that 'err' can be NULL if we don't need the error code;
    virtual BOOL WINAPI SalGetFileSize2(const char* fileName, CQuadWord& size, DWORD* err) = 0;

    // determines the size of file that symlink 'fileName' points to; returns size in 'size';
    // 'ignoreAll' is in + out, if TRUE all errors are ignored (before action must be
    // set to FALSE, otherwise the error window won't show at all, then don't change);
    // on error displays standard window with Retry / Ignore / Ignore All / Cancel prompt
    // with parent 'parent'; if size is successfully determined, returns TRUE; on error and pressing
    // Ignore / Ignore All button in error window, returns FALSE and returns FALSE in 'cancel';
    // if 'ignoreAll' is TRUE, window isn't shown, no button press is awaited, behaves as if
    // user pressed Ignore; on error and pressing Cancel in error window returns FALSE and
    // returns TRUE in 'cancel';
    // can be called from any thread
    virtual BOOL WINAPI GetLinkTgtFileSize(HWND parent, const char* fileName, CQuadWord* size,
                                           BOOL* cancel, BOOL* ignoreAll) = 0;

    // deletes link to directory (junction point, symbolic link, mount point); on success
    // returns TRUE; on error returns FALSE and if 'err' is not NULL, returns error code in 'err'
    // can be called from any thread
    virtual BOOL WINAPI DeleteDirLink(const char* name, DWORD* err) = 0;

    // if file/directory 'name' has read-only attribute, we try to turn it off
    // (reason: e.g. so it can be deleted via DeleteFile); if we already have attributes of 'name'
    // loaded, pass them in 'attr', if 'attr' is -1, attributes of 'name' are read from disk;
    // returns TRUE if attempt to change attribute is made (success is not checked);
    // NOTE: only turns off read-only attribute, so in case of multiple hardlinks there's no
    // unnecessarily large attribute change on remaining hardlinks of the file (all hardlinks
    // share attributes)
    // can be called from any thread
    virtual BOOL WINAPI ClearReadOnlyAttr(const char* name, DWORD attr = -1) = 0;

    // determines if critical shutdown (or log off) is currently in progress, if yes, returns TRUE;
    // during this shutdown we only have 5s to save configuration of the entire program
    // including plugins, so time-consuming operations must be skipped, after 5s
    // the system forcefully terminates our process, see WM_ENDSESSION, flag ENDSESSION_CRITICAL,
    // this is Vista+
    virtual BOOL WINAPI IsCriticalShutdown() = 0;

    // iterates through all windows in thread 'tid' (0 = current) (EnumThreadWindows) and posts WM_CLOSE to all enabled
    // and visible dialogs (class name "#32770") owned by window 'parent';
    // used during critical shutdown to unblock window/dialog over which modal dialogs are open,
    // if multiple layers are possible, must be called repeatedly
    virtual void WINAPI CloseAllOwnedEnabledDialogs(HWND parent, DWORD tid = 0) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_gen)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__
