// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <list>
#include "reg_sz_narrow_bridge.h"
#include "ftp_persisted_text_codec.h"

// plugin interface object, its methods are called from Salamander
CPluginInterface PluginInterface;
// other parts of the CPluginInterface interface
CPluginInterfaceForFS InterfaceForFS;
CPluginInterfaceForMenuExt InterfaceForMenuExt;

// ConfigVersion: 0 - default (without load),
//                1 - work version before listing parsing support (from this version on there are sensible lists of server-types and ftp-servers)
//                2 - work version before the first MVS adjustments according to tester Michael Knigge (server-type list adjustment)
//                3 - work version before the second MVS adjustments according to tester Michael Knigge (server-type list adjustment)
//                4 - work version before the third MVS adjustments according to tester Michael Knigge (server-type list adjustment)
//                5 - work version before the first VMS adjustments (server-type list adjustment)
//                6 - work version before the fourth MVS adjustments according to tester Michael Knigge (server-type list adjustment)
//                7 - work version before modifying values in the configuration on the Operations page
//                8 - work version before Servant Salamander 2.1 beta 1
//                9 - Servant Salamander 2.5 beta 6 (+adjustments to server-type autodetect conditions)
//                10 - added parser for Unix systems with two spaces before the file/directory name in the listing (Filezilla + AIX)
//                11 - fixed parser for Unix systems with two spaces before the file/directory name in the listing (Filezilla + AIX)
//                12 - priority change: the parser for Unix systems with two spaces after the year before names comes before the parser with one space after the year before names (the variant without spaces at the beginning of names is more likely) + enriched MVS1 and MVS2 parsers
//                13 - fixed column descriptions in the MVS PO parser (all four): used "Number of Records" instead of "Size"
//                14 - fixed the MVS PO 4 parser: the value can be missing both in column "AC" and in column "Alias" - now reading them directly by offsets + trimming trailing spaces, there is no other way + fixed the MVS1 and MVS2 parsers (volume OK + followed by "Error determining attributes")
//                     + fixed the column names in the MVS PO parser (all four): used "Records" instead of "Size" - the size is not in bytes, nonsensical progress was shown during download
//                15 - added parser for OS/2 FTP server
//                16 - added parser for VxWorks FTP server
//                17 - fixed the MVS parser (another type of error message in the listing + another combination of skipped data in the listing) + fixed the VxWorks parser
//                18 - added alignment to parser columns - we usually align dates+times+numbers to the right
//                19 - adjusted the z/VM parser: Date is of type GeneralDate so that item ".." shows an empty string instead of "1.1.1602"
//                20 - added the "is_link" identifier (TRUE = the panel icon has a link overlay)
//                21 - added the UNIX4 parser (German dates - months longer than 3 letters), added functions "month_txt"
//                22 - modified values in the configuration on the Operations 2 (Upload) page
//                23 - adjusted parser autodetection conditions that skip the first or last rows of the listing (if Microsoft IIS or Netprezenz returned a listing with one or two rows, the VMS parser simply ignored it and an empty listing was used)
//                24 - adjusted skipping listing headers in parsers, now an invalid row is not skipped when a header is missing
//                25 - adjusted the Microsoft IIS parser: directories containing spaces instead of names are ignored (I do not understand why they are even shown when they have no name, but they are)
//                26 - added UNIX5 (IBM AIX - German version) parser (the month and day columns are swapped)
//                27 - adjusted the VMS1-4 parsers, novelty on cs.felk.cvut.cz: an empty directory returns a listing containing "Total of 0 files, 0/0 blocks" (the listing previously returned the error "no files found")
//                28 - added parser for Tandem
//                29 - default change: "Resume or Overwrite" instead of "Overwrite" for Config.UploadRetryOnCreatedFile; Config.DisableLoggingOfWorkers changed to FALSE (I am tired of writing to everyone to enable logging, the logging overhead is minimal)
//                30 - added parser for IBM AS/400
//                31 - adjusted parser for IBM AS/400
//                32 - renamed parser for IBM AS/400 to "IBM iSeries/i5, AS/400"
//                33 - adjusted parser for IBM AS/400: names must not contain spaces (otherwise it nonsensically parses, for example, unparsable Unix listings)
//                34 - adjusted all five UNIX parsers: user+group can contain more spaces (in that case they are ignored because we do not know how to separate user from group)
//                35 - added UNIX parser for MOXA FTP server (missing times + dates); change in all UNIX parsers: reading the <rights> column is no longer done by fixed 10 characters but by a word (reason: ACL on Unix introduced '+' after those ten characters, e.g. "drwxrwxr-x+")
//                36 - change in the CZ+EN parser for IBM AS/400: ignore the first row with an empty file name
//                37 - added parser for Xbox 360

int ConfigVersion = 0;
#define CURRENT_CONFIG_VERSION 37
#define RELOAD_PARSERS_BEFORE_CONFIG_VERSION 37 // parsers are not read from configurations before this version; defaults are used (primitive parser update for users)

// names of values in the configuration (in the registry)
const wchar_t* CONFIG_VERSION = L"Version";
const wchar_t* CONFIG_LASTCFGPAGE = L"Last Config Page";

const wchar_t* CONFIG_SHOWWELCOMEMESSAGE = L"Show Welcome Message";
const wchar_t* CONFIG_PRIORITYTOPANELCON = L"Priority to Panel Connections";
const wchar_t* CONFIG_ENABLETOTALSPEEDLIM = L"Enable Total Speed Limit";
const wchar_t* CONFIG_TOTALSPEEDLIMIT = L"Total Speed Limit";
const wchar_t* CONFIG_ANONYMOUSPASSWD = L"Anonymous Password";
const wchar_t* CONFIG_OPERDLGPOSITION = L"OperDlg Position";
const wchar_t* CONFIG_OPERDLGSPLITPOS = L"OperDlg Split Pos.";
const wchar_t* CONFIG_OPERDLGCLOSEIFSUCCESS = L"Close OperDlg If Successfully Finished";
const wchar_t* CONFIG_OPERDLGCLOSEWHENFINISHES = L"Close OperDlg When Oper Finishes";
const wchar_t* CONFIG_OPENSOLVEERRIFIDLE = L"Open SolveErrDlg If Idle";
const wchar_t* CONFIG_SIMPLELSTCOLFIXEDWIDTH = L"Simple Listing Fixed Column Width";
const wchar_t* CONFIG_SIMPLELSTCOLWIDTH = L"Simple Listing Column Width";

const wchar_t* CONFIG_PASSIVEMODE = L"Passive Mode";
const wchar_t* CONFIG_KEEPALIVE = L"Keep Alive";
const wchar_t* CONFIG_USEMAXCON = L"Max. Connections";
const wchar_t* CONFIG_SPEEDLIM = L"Speed Limit";
const wchar_t* CONFIG_TRANSFERMODE = L"Transfer Mode";
const wchar_t* CONFIG_USELISTINGSCACHE = L"Use Listings Cache";
const wchar_t* CONFIG_ASCIIMASKS = L"ASCII File Masks";
const wchar_t* CONFIG_COMPRESSDATA = L"Compress Data";

const wchar_t* CONFIG_SRVREPTIMEOUT = L"Server Replies Timeout";
const wchar_t* CONFIG_NODATATRTIMEOUT = L"No Data Transfer Timeout";
const wchar_t* CONFIG_DELBETWCONRETR = L"Delay Connect Retries";
const wchar_t* CONFIG_CONATTEMPTS = L"Connect Attempts";
const wchar_t* CONFIG_RESUMEOVERLAP = L"Resume Overlap";
const wchar_t* CONFIG_RESUMEMINFILESIZE = L"Resume Min File Size";
const wchar_t* CONFIG_KASENDEVERY = L"Keep Alive - Every";
const wchar_t* CONFIG_KASTOPAFTER = L"Keep Alive - Stop After";
const wchar_t* CONFIG_KACOMMAND = L"Keep Alive - Command";
const wchar_t* CONFIG_CACHEMAXSIZE = L"Mem Cache Max Size";

const wchar_t* CONFIG_LASTBOOKMARK = L"Last Bookmark";

const wchar_t* CONFIG_DOWNLOADADDTOQUEUE = L"Download Add To Queue";
const wchar_t* CONFIG_DELETEADDTOQUEUE = L"Delete Add To Queue";
const wchar_t* CONFIG_CHATTRADDTOQUEUE = L"ChngAttr Add To Queue";

const wchar_t* CONFIG_OPERCANNOTCREATEFILE = L"If Cannot Create File";
const wchar_t* CONFIG_OPERCANNOTCREATEDIR = L"If Cannot Create Dir";
const wchar_t* CONFIG_OPERFILEALREADYEXISTS = L"If File Already Exists";
const wchar_t* CONFIG_OPERDIRALREADYEXISTS = L"If Dir Already Exists";
const wchar_t* CONFIG_OPERRETRYONCREATFILE = L"If Retry On Created";
const wchar_t* CONFIG_OPERRETRYONRESUMFILE = L"If Retry On Resumed";
const wchar_t* CONFIG_OPERASCIITRMODEFORBIN = L"If Ascii Mode For Binary File";
const wchar_t* CONFIG_OPERUNKNOWNATTRS = L"If Unknown Attrs";
const wchar_t* CONFIG_OPERNONEMPTYDIRDEL = L"If Directory Is Not Empty";
const wchar_t* CONFIG_OPERHIDDENFILEDEL = L"If File Is Hidden";
const wchar_t* CONFIG_OPERHIDDENDIRDEL = L"If Dir Is Hidden";

const wchar_t* CONFIG_UPLOADCANNOTCREATEFILE = L"Upload - If Cannot Create File";
const wchar_t* CONFIG_UPLOADCANNOTCREATEDIR = L"Upload - If Cannot Create Dir";
const wchar_t* CONFIG_UPLOADFILEALREADYEXISTS = L"Upload - If File Already Exists";
const wchar_t* CONFIG_UPLOADDIRALREADYEXISTS = L"Upload - If Dir Already Exists";
const wchar_t* CONFIG_UPLOADRETRYONCREATFILE = L"Upload - If Retry On Created";
const wchar_t* CONFIG_UPLOADRETRYONRESUMFILE = L"Upload - If Retry On Resumed";
const wchar_t* CONFIG_UPLOADASCIITRMODEFORBIN = L"Upload - If Ascii Mode For Binary File";

const wchar_t* CONFIG_SERVERTYPES = L"Server Types";
const wchar_t* CONFIG_STNAME = L"Name";
const wchar_t* CONFIG_STADCOND = L"Autodetect Condition";
const wchar_t* CONFIG_STCOLUMNS = L"Columns";
const wchar_t* CONFIG_STRULESFORPARS = L"Rules For Parsing";

const wchar_t* CONFIG_FTPSERVERLIST = L"Bookmarks";
const wchar_t* CONFIG_FTPSRVNAME = L"Name";
const wchar_t* CONFIG_FTPSRVADDRESS = L"Address";
const wchar_t* CONFIG_FTPSRVPATH = L"Initial Path";
const wchar_t* CONFIG_FTPSRVANONYM = L"Anonymous";
const wchar_t* CONFIG_FTPSRVUSER = L"User";
const wchar_t* CONFIG_FTPSRVPASSWD_OLD = L"Password"; // import only, scrambled by FTP plugin
const wchar_t* CONFIG_FTPSRVPASSWD_SCRAMBLED = L"PasswordS";
const wchar_t* CONFIG_FTPSRVPASSWD_ENCRYPTED = L"PasswordE";
const wchar_t* CONFIG_FTPSRVSAVEPASSWD = L"Save Password";
const wchar_t* CONFIG_FTPSRVPROXYSRVUID = L"Proxy Server UID";
const wchar_t* CONFIG_FTPSRVTGTPATH = L"Target Path";
const wchar_t* CONFIG_FTPSRVTYPE = L"Server Type";
const wchar_t* CONFIG_FTPSRVTRANSFMODE = L"Transfer Mode";
const wchar_t* CONFIG_FTPSRVPORT = L"Port";
const wchar_t* CONFIG_FTPSRVPASV = L"Passive Mode";
const wchar_t* CONFIG_FTPSRVKALIVE = L"Keep Alive";
const wchar_t* CONFIG_FTPSRVKASENDEVERY = L"Keep Alive - Every";
const wchar_t* CONFIG_FTPSRVKASTOPAFTER = L"Keep Alive - Stop After";
const wchar_t* CONFIG_FTPSRVKACOMMAND = L"Keep Alive - Command";
const wchar_t* CONFIG_FTPSRVUSEMAXCON = L"Max. Connections";
const wchar_t* CONFIG_FTPSRVSPDLIM = L"Speed Limit";
const wchar_t* CONFIG_FTPSRVUSELISTINGSCACHE = L"Use Listings Cache";
const wchar_t* CONFIG_FTPSRVINITFTPCMDS = L"Initial FTP Commands";
const wchar_t* CONFIG_FTPSRVLISTCMD = L"List Command";
const wchar_t* CONFIG_FTPSRVENCRYPTCONTROLCONNECTION = L"Encrypt Control Connection";
const wchar_t* CONFIG_FTPSRVENCRYPTDATACONNECTION = L"Encrypt Data Connection";
const wchar_t* CONFIG_FTPSRVCOMPRESSDATA = L"Compress Data";

const wchar_t* CONFIG_FTPPROXYLIST = L"Proxy Servers";
const wchar_t* CONFIG_FTPPRXUID = L"Unique ID";
const wchar_t* CONFIG_FTPPRXNAME = L"Name";
const wchar_t* CONFIG_FTPPRXTYPE = L"Type";
const wchar_t* CONFIG_FTPPRXHOST = L"Host";
const wchar_t* CONFIG_FTPPRXPORT = L"Port";
const wchar_t* CONFIG_FTPPRXUSER = L"User";
const wchar_t* CONFIG_FTPPRXPASSWD_OLD = L"Password"; // import only, scrambled by FTP plugin
const wchar_t* CONFIG_FTPPRXPASSWD_SCRAMBLED = L"PasswordS";
const wchar_t* CONFIG_FTPPRXPASSWD_ENCRYPTED = L"PasswordE";
const wchar_t* CONFIG_FTPPRXSCRIPT = L"Script";

const wchar_t* CONFIG_DEFAULTFTPPRXUID = L"Default Proxy UID";

const wchar_t* CONFIG_ALWAYSNOTCLOSECON = L"Always Detach";
const wchar_t* CONFIG_ALWAYSDISCONNECT = L"Always Disconnect";
const wchar_t* CONFIG_ALWAYSRECONNECT = L"Always Reconnect";
const wchar_t* CONFIG_ALWAYSOVEWRITE = L"Always Overwrite";
const wchar_t* CONFIG_CONVERTHEXESCSEQ = L"Convert Hex-esc-sequences";
const wchar_t* CONFIG_WARNWHENCONLOST = L"Connection Lost Message";
const wchar_t* CONFIG_HINTLISTHIDDENFILES = L"List Hidden Files Hint";

const wchar_t* CONFIG_ENABLELOGGING = L"Enable Logging";
const wchar_t* CONFIG_LOGMAXSIZE = L"Log Max. Size";
const wchar_t* CONFIG_MAXCLOSEDCONLOGS = L"Max. Closed Connections Logs";
const wchar_t* CONFIG_LOGSDLGPOSITION = L"Logs Position";
const wchar_t* CONFIG_ALWAYSSHOWLOGFORACTPAN = L"Always Show Panel Log";
const wchar_t* CONFIG_DISABLELOGWORKERS = L"Disable Log for Operations";

const wchar_t* CONFIG_COMMANDHISTORY = L"Command History";
const wchar_t* CONFIG_SENDSECRETCOMMAND = L"Send Secret Command";

const wchar_t* CONFIG_HOSTADDRESSHISTORY = L"Host Address History";
const wchar_t* CONFIG_INITPATHHISTORY = L"Init Path History";

// frequently used error message
const char* LOW_MEMORY = "Low memory";

const char* LIST_CMD_TEXT = "LIST";      // text of the FTP command "LIST"
const char* NLST_CMD_TEXT = "NLST";      // text of the FTP command "NLST"
const char* LIST_a_CMD_TEXT = "LIST -a"; // text of the FTP command "LIST -a"

int SortByExtDirsAsFiles = FALSE; // current value of the Salamander configuration variable SALCFG_SORTBYEXTDIRSASFILES
int InactiveBeepWhenDone = TRUE;  // current value of the Salamander configuration variable SALCFG_MINBEEPWHENDONE

HINSTANCE DLLInstance = NULL; // handle to SPL - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to SLG - language-dependent resources

// general interface of Salamander - valid from startup until the plug-in is closed
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;

// ZLIB compression/decompression interface;
CSalamanderZLIBAbstract* SalZLIB = NULL;

// variable definition for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

// variable definition for "spl_com.h"
int SalamanderVersion = 0;

// interface providing customized Windows controls used in Salamander
CSalamanderGUIAbstract* SalamanderGUI = NULL;

// pointers to the mapping tables for lower/upper case
unsigned char* LowerCase = NULL;
unsigned char* UpperCase = NULL;

CConfiguration Config; // global configuration of the FTP client

// array with all open FS (use only from the main thread - unsynchronized)
TIndirectArray<CPluginFSInterface> FTPConnections(5, 10, dtNoDelete);

BOOL WindowsVistaAndLater = FALSE; // Windows Vista or later from the NT family

// global variables for the FTPCMD_CHANGETGTPANELPATH command
int TargetPanelPathPanel = PANEL_LEFT;
std::wstring TargetPanelPath;

std::wstring UserDefinedSuffix; // preloaded suffix for user-defined server types

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DLLInstance = hinstDLL;

        INITCOMMONCONTROLSEX initCtrls;
        initCtrls.dwSize = sizeof(INITCOMMONCONTROLSEX);
        initCtrls.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES;
        if (!InitCommonControlsEx(&initCtrls))
        {
            MessageBoxW(NULL, L"InitCommonControlsEx failed!", L"Error", MB_OK | MB_ICONERROR);
            return FALSE; // DLL won't start
        }

        WindowsVistaAndLater = SalIsWindowsVersionOrGreater(6, 0, 0);
    }

    return TRUE; // DLL can be loaded
}

//
// ****************************************************************************
// LoadStr
//

// LangStr is the real one: it is what the SDK already returns, with no conversion
// at all. Prefer it everywhere a string reaches USER32, the SDK, or a dialog.
std::wstring LangStr(int resID)
{
    return SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID);
}

// Temporary adapter for narrow formatting and protocol buffers that have not yet moved to the
// explicit FTP codec. CLogs itself is UTF-16 and direct localized log messages use LangStr.
//
// The returned pointer must stay valid after the call, so each projection is retained - but the
// retention is BOUNDED, because that is the whole contract this replaced. SalamanderGeneral->LoadStr
// promised a 10000-character buffer "used cyclically" (spl_gen.h), so a pointer stayed good until
// that much later text had been loaded and the storage cost was flat forever. A list that only ever
// grows keeps the stability half of that promise and silently drops the bounded half: LoadStr is
// reached once per queue row per repaint (LVN_GETDISPINFO -> GetListViewDataForW ->
// GetProblemDescr, every arm of which is FTPFormatString(msg, LoadStr(...))), so scrolling a large
// queue on a long transfer grew the plugin heap without limit for the life of the dialog thread.
//
// std::list is what makes the trim safe: popping the front never relocates an element that is still
// retained, so every pointer still inside the budget stays valid.
char* LoadStr(int resID)
{
    static char failed[] = "ERROR LOADING STRING";
    if (SalamanderGeneral == NULL)
        return failed;

    std::wstring wide;
    std::string encoded;
    if (!SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID, wide) ||
        !FtpEncodeLocalText(wide.c_str(), encoded))
        return failed;

    static thread_local std::list<std::string> values;
    static thread_local size_t retainedBytes = 0;
    retainedBytes += encoded.size() + 1;
    values.emplace_back(std::move(encoded));

    // The same budget the frozen buffer had, so a caller keeps the same guarantee it could already
    // rely on. The floor keeps one expression that passes several LoadStr results as arguments safe
    // even when the individual strings are enormous - the one case the fixed buffer could not
    // survive and this can.
    const size_t retentionBudget = 10000;
    const size_t minRetained = 16;
    while (retainedBytes > retentionBudget && values.size() > minRetained)
    {
        retainedBytes -= values.front().size() + 1;
        values.pop_front();
    }
    return values.back().data();
}

//
// ****************************************************************************
// SalamanderPluginGetReqVer
//

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
    // set SalamanderDebug for "dbg.h"
    SalamanderDebug = salamander->GetSalamanderDebug();
    // set SalamanderVersion for "spl_com.h"
    SalamanderVersion = salamander->GetVersion();
    HANDLES_CAN_USE_TRACE();
    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

    // this plug-in is made for the current version of Salamander and higher - perform a check
    if (SalamanderVersion < LAST_VERSION_OF_SALAMANDER)
    { // reject older versions
        // wide: same call-site-local widen shape as checksum/unlha/undelete/zip/
        // splitcbn/pak (205-211).
#define FTP_WIDEN2(x) L##x
#define FTP_WIDEN(x) FTP_WIDEN2(x)
        MessageBoxW(salamander->GetParentWindow(), FTP_WIDEN(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"FTP Client" /* neprekladat! */, MB_OK | MB_ICONERROR);
#undef FTP_WIDEN
#undef FTP_WIDEN2
        return NULL;
    }

    // let the language module (.slg) load
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"FTP Client" /* neprekladat! */);
    if (HLanguage == NULL)
        return NULL;

    // get the general interface of Salamander
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalZLIB = SalamanderGeneral->GetSalamanderZLIB();

    // set the help file name
    SalamanderGeneral->SetHelpFileName(L"ftp.chm");

    SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &SortByExtDirsAsFiles,
                                          sizeof(SortByExtDirsAsFiles), NULL);
    SalamanderGeneral->GetConfigParameter(SALCFG_MINBEEPWHENDONE, &InactiveBeepWhenDone,
                                          sizeof(InactiveBeepWhenDone), NULL);
    SalamanderGeneral->GetLowerAndUpperCase(&LowerCase, &UpperCase);
    try
    {
        UserDefinedSuffix = LangStr(IDS_SRVTYPEUSERDEF);
    }
    catch (...)
    {
        return NULL;
    }
    if (!Config.InitWithSalamanderGeneral())
        return NULL; // error

    // obtain the interface providing customized Windows controls used in Salamander
    SalamanderGUI = salamander->GetSalamanderGUI();

    if (!InitSockets(salamander->GetParentWindow()))
    {
        Config.ReleaseDataFromSalamanderGeneral();
        return NULL; // error
    }

    if (!InitFS())
    {
        ReleaseSockets();
        Config.ReleaseDataFromSalamanderGeneral();
        return NULL; // error
    }

    // set the basic information about the plug-in
    std::wstring versionText;
    std::wstring copyrightText;
    if (!FtpDecodeLocalText(VERSINFO_VERSION_NO_PLATFORM, versionText) ||
        !FtpDecodeLocalText(VERSINFO_COPYRIGHT, copyrightText))
    {
        ReleaseSockets();
        Config.ReleaseDataFromSalamanderGeneral();
        return NULL;
    }
    salamander->SetBasicPluginData(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(),
                                   FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION |
                                       FUNCTION_FILESYSTEM,
                                   versionText.c_str(), copyrightText.c_str(),
                                   SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINDESCR).c_str(),
                                   L"FTP", NULL, L"ftp");

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    // we want to receive messages about creation/change/removal of the master password
    SalamanderGeneral->SetPluginUsesPasswordManager();

    // Keep the host-assigned semantic FS names natively wide.
    AssignedFSName = SPLGetPluginFSNameOwned(SalamanderGeneral, 0);
    if (AssignedFSName.empty())
        return FALSE;

    // also add a name for FTPS (FTP over SSL)
    if (salamander->AddFSName(L"ftps", &AssignedFSNameIndexFTPS))
    {
        AssignedFSNameFTPS = SPLGetPluginFSNameOwned(
            SalamanderGeneral, AssignedFSNameIndexFTPS);
        if (AssignedFSNameFTPS.empty())
            return FALSE;
    }
    else
    {
        AssignedFSNameFTPS = AssignedFSName; // probably "dead code"
    }

    return &PluginInterface;
}

//
// ****************************************************************************
// CPluginInterface
//

void CPluginInterface::About(HWND parent)
{
    try
    {
        std::wstring version;
        std::wstring copyright;
        if (!FtpDecodeLocalText(VERSINFO_VERSION, version) ||
            !FtpDecodeLocalText(VERSINFO_COPYRIGHT, copyright))
        {
            SalamanderGeneral->SalMessageBox(
                parent, LangStr(IDS_PLUGINDESCR).c_str(),
                LangStr(IDS_ABOUTPLUGINTITLE).c_str(), MB_OK | MB_ICONINFORMATION);
            return;
        }
        const std::wstring text = SPLFormatStringOwned(
            L"%ls %ls\n\n%ls\n\n%ls", LangStr(IDS_FTPPLUGINTITLE).c_str(),
            version.c_str(), copyright.c_str(), LangStr(IDS_PLUGINDESCR).c_str());
        SalamanderGeneral->SalMessageBox(
            parent, text.c_str(), LangStr(IDS_ABOUTPLUGINTITLE).c_str(),
            MB_OK | MB_ICONINFORMATION);
    }
    catch (...)
    {
        SalamanderGeneral->SalMessageBox(
            parent, LangStr(IDS_PLUGINDESCR).c_str(),
            LangStr(IDS_ABOUTPLUGINTITLE).c_str(), MB_OK | MB_ICONINFORMATION);
    }
}

BOOL CPluginInterface::Release(HWND parent, BOOL force)
{
    CALL_STACK_MESSAGE2("CPluginInterface::Release(, %d)", force);

    BOOL ret = FALSE;
    if (force ||
        FTPOperationsList.IsEmpty() ||
        SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_CANCELEXISTINGOPER).c_str(),
                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                         MB_YESNO | MB_ICONQUESTION | MSGBOXEX_ESCAPEENABLED) == IDYES)
    { // any cancellation of all operations is performed in ReleaseFS()
        ret = TRUE;
    }

    if (ret)
    {
        ReleaseFS();

        if (InterfaceForFS.GetActiveFSCount() != 0)
        {
            TRACE_E("Some FS interfaces were not closed (count=" << InterfaceForFS.GetActiveFSCount() << ")");
        }

        // remove all copies of files from FTP in the disk cache (they exist because they survive connection close and are unnecessary)
        SalamanderGeneral->RemoveFilesFromCache(
            (AssignedFSName + L':').c_str());
        SalamanderGeneral->RemoveFilesFromCache(
            (AssignedFSNameFTPS + L':').c_str());

        ReleaseSockets();
        FreeSSL();
        Config.ReleaseDataFromSalamanderGeneral();
    }
    return ret;
}

// Several of this plugin's REG_SZ fields (this history mechanism;
// CFTPServer's bookmark identity fields; CConfiguration's speed-limit/
// anonymous-password/mask/dialog-position text) stay narrow in memory by
// design - they are byte-owned bookmark/protocol/UI text, not something this
// tick widens - but the shared registry facade's REG_SZ path (SetValueW/
// GetValueW) is wide-only: on write it derives the byte count from wcslen()
// over 'data' (now bounded/refused rather than OOB-reading past a narrow
// buffer's real end, see reg_sz_safe_length.h), and on read it blits the
// stored UTF-16LE bytes into 'buffer' with zero conversion. A narrow char*
// therefore does not round-trip through this facade AT ALL: writes are
// refused outright (a narrow C-string almost never contains an aligned wide
// NUL within the scan bound) and reads of an existing wide value corrupt a
// narrow destination. That is a live bug, not a hypothetical one - as of the
// facade's OOB-read fix, every field using these helpers previously silently
// failed to save.
//
// Bridge here, at each Load()/Save() boundary, converting to/from wide right
// at the registry call. This does not widen any in-memory field or touch any
// consumer (dialogs, connection code) - it only makes the already-narrow
// representation actually reach the registry and come back again.
static BOOL SetValueSZ(CSalamanderRegistryAbstract* registry, HKEY regKey, const wchar_t* name, const char* narrowValue)
{
    std::wstring wide;
    if (!EncodeRegSzFromNarrowOwned(narrowValue, wide))
        return FALSE;
    return SPLRegistrySetString(registry, regKey, name, wide);
}

static BOOL GetValueSZ(CSalamanderRegistryAbstract* registry, HKEY regKey, const wchar_t* name, std::string& value)
{
    std::wstring wideValue;
    std::string staged;
    if (!SPLRegistryGetStringOwned(registry, regKey, name, wideValue) ||
        !DecodeRegSzToNarrowOwned(wideValue.c_str(), staged))
        return FALSE;
    value.swap(staged);
    return TRUE;
}

static BOOL GetValueStringW(CSalamanderRegistryAbstract* registry, HKEY regKey, const wchar_t* name, std::wstring& value)
{
    return SPLRegistryGetStringOwned(registry, regKey, name, value);
}

static BOOL SetValueStringW(CSalamanderRegistryAbstract* registry, HKEY regKey, const wchar_t* name, const std::wstring& value)
{
    return SPLRegistrySetString(registry, regKey, name, value);
}

static BOOL LoadWideHistory(CSalamanderRegistryAbstract* registry, HKEY hKey,
                            const wchar_t* name, std::wstring history[], int maxCount)
{
    HKEY historyKey;
    if (!registry->OpenKey(hKey, name, historyKey))
        return TRUE;
    for (int i = 0; i < maxCount; i++)
        history[i].clear();
    for (int i = 0; i < maxCount; i++)
    {
        std::wstring valueName;
        if (!FTPFormatDecimalIndex(valueName, i + 1))
        {
            registry->CloseKey(historyKey);
            return FALSE;
        }
        if (!SPLRegistryGetStringOwned(registry, historyKey, valueName.c_str(), history[i]))
            break;
    }
    registry->CloseKey(historyKey);
    return TRUE;
}

static BOOL SaveWideHistory(CSalamanderRegistryAbstract* registry, HKEY hKey,
                            const wchar_t* name, const std::wstring history[], int maxCount)
{
    HKEY historyKey;
    if (!registry->CreateKey(hKey, name, historyKey))
        return TRUE;
    registry->ClearKey(historyKey);
    BOOL saveHistory = FALSE;
    if (SalamanderGeneral->GetConfigParameter(SALCFG_SAVEHISTORY, &saveHistory,
                                               sizeof(saveHistory), NULL) &&
        saveHistory)
    {
        for (int i = 0; i < maxCount && !history[i].empty(); i++)
        {
            std::wstring valueName;
            if (!FTPFormatDecimalIndex(valueName, i + 1))
            {
                registry->CloseKey(historyKey);
                return FALSE;
            }
            SPLRegistrySetString(registry, historyKey, valueName.c_str(), history[i]);
        }
    }
    registry->CloseKey(historyKey);
    return TRUE;
}

void CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::LoadConfiguration(, ,)");

    if (regKey != NULL) // load from the registry
    {
        registry->GetValue(regKey, CONFIG_VERSION, REG_DWORD, &ConfigVersion, sizeof(DWORD));

        registry->GetValue(regKey, CONFIG_LASTCFGPAGE, REG_DWORD, &Config.LastCfgPage, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_SHOWWELCOMEMESSAGE, REG_DWORD, &Config.ShowWelcomeMessage, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_PRIORITYTOPANELCON, REG_DWORD, &Config.PriorityToPanelConnections, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_ENABLETOTALSPEEDLIM, REG_DWORD, &Config.EnableTotalSpeedLimit, sizeof(DWORD));
        std::string numberText;
        if (GetValueSZ(registry, regKey, CONFIG_TOTALSPEEDLIMIT, numberText))
        {
            Config.TotalSpeedLimit = atof(numberText.c_str());
        }
        std::wstring anonymousPassword;
        if (GetValueStringW(registry, regKey, CONFIG_ANONYMOUSPASSWD, anonymousPassword))
        {
            Config.SetAnonymousPasswd(anonymousPassword.c_str());
            FTPSecureWipe(anonymousPassword);
        }

        registry->GetValue(regKey, CONFIG_PASSIVEMODE, REG_DWORD, &Config.PassiveMode, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_KEEPALIVE, REG_DWORD, &Config.KeepAlive, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_COMPRESSDATA, REG_DWORD, &Config.CompressData, sizeof(DWORD));

        DWORD dw;
        if (registry->GetValue(regKey, CONFIG_USEMAXCON, REG_DWORD, &dw, sizeof(DWORD)) &&
            dw != -1)
        {
            Config.MaxConcurrentConnections = dw;
            Config.UseMaxConcurrentConnections = TRUE;
        }
        else
            Config.UseMaxConcurrentConnections = FALSE;
        if (GetValueSZ(registry, regKey, CONFIG_SPEEDLIM, numberText) &&
            atof(numberText.c_str()) != -1)
        {
            Config.ServerSpeedLimit = atof(numberText.c_str());
            Config.UseServerSpeedLimit = TRUE;
        }
        else
            Config.UseServerSpeedLimit = FALSE;
        registry->GetValue(regKey, CONFIG_USELISTINGSCACHE, REG_DWORD, &Config.UseListingsCache, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_TRANSFERMODE, REG_DWORD, &Config.TransferMode, sizeof(DWORD));
        // Read the masks as the UTF-16 REG_SZ already holds - see the write side. An older build
        // wrote the same value through an ACP round trip, so anything it managed to store reads
        // back identically here.
        std::wstring masksText;
        if (GetValueStringW(registry, regKey, CONFIG_ASCIIMASKS, masksText))
            Config.ASCIIFileMasks->SetMasksString(masksText.c_str(), FALSE);

        if (registry->GetValue(regKey, CONFIG_SRVREPTIMEOUT, REG_DWORD, &dw, sizeof(DWORD)))
            Config.SetServerRepliesTimeout(dw);
        if (registry->GetValue(regKey, CONFIG_NODATATRTIMEOUT, REG_DWORD, &dw, sizeof(DWORD)))
            Config.SetNoDataTransferTimeout(dw);
        if (registry->GetValue(regKey, CONFIG_DELBETWCONRETR, REG_DWORD, &dw, sizeof(DWORD)))
            Config.SetDelayBetweenConRetries(dw);
        if (registry->GetValue(regKey, CONFIG_CONATTEMPTS, REG_DWORD, &dw, sizeof(DWORD)))
            Config.SetConnectRetries(dw);
        if (registry->GetValue(regKey, CONFIG_RESUMEOVERLAP, REG_DWORD, &dw, sizeof(DWORD)))
        {
            if (dw > 1024 * 1024 * 1024)
                dw = 1024 * 1024 * 1024;
            Config.SetResumeOverlap(dw);
        }
        if (registry->GetValue(regKey, CONFIG_RESUMEMINFILESIZE, REG_DWORD, &dw, sizeof(DWORD)))
            Config.SetResumeMinFileSize(dw);
        registry->GetValue(regKey, CONFIG_KASENDEVERY, REG_DWORD, &Config.KeepAliveSendEvery, sizeof(DWORD));
        if (Config.KeepAliveSendEvery < 0 || Config.KeepAliveSendEvery > 10000)
            Config.KeepAliveSendEvery = 60;
        registry->GetValue(regKey, CONFIG_KASTOPAFTER, REG_DWORD, &Config.KeepAliveStopAfter, sizeof(DWORD));
        if (Config.KeepAliveStopAfter < 0 || Config.KeepAliveStopAfter > 10000)
            Config.KeepAliveStopAfter = 30;
        registry->GetValue(regKey, CONFIG_KACOMMAND, REG_DWORD, &Config.KeepAliveCommand, sizeof(DWORD));
        DWORD cacheMaxSize;
        if (registry->GetValue(regKey, CONFIG_CACHEMAXSIZE, REG_DWORD, &cacheMaxSize, sizeof(DWORD)))
        {
            Config.CacheMaxSize = CQuadWord(cacheMaxSize, 0); // storing it as a DWORD is sufficient for now
            if (Config.CacheMaxSize < CQuadWord(100 * 1024, 0))
                Config.CacheMaxSize = CQuadWord(100 * 1024, 0);
        }
        registry->GetValue(regKey, CONFIG_DOWNLOADADDTOQUEUE, REG_DWORD, &Config.DownloadAddToQueue, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_DELETEADDTOQUEUE, REG_DWORD, &Config.DeleteAddToQueue, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_CHATTRADDTOQUEUE, REG_DWORD, &Config.ChAttrAddToQueue, sizeof(DWORD));

        if (ConfigVersion > 7) // older versions use default values
        {
            registry->GetValue(regKey, CONFIG_OPERCANNOTCREATEFILE, REG_DWORD, &Config.OperationsCannotCreateFile, sizeof(DWORD));
            registry->GetValue(regKey, CONFIG_OPERCANNOTCREATEDIR, REG_DWORD, &Config.OperationsCannotCreateDir, sizeof(DWORD));
            registry->GetValue(regKey, CONFIG_OPERFILEALREADYEXISTS, REG_DWORD, &Config.OperationsFileAlreadyExists, sizeof(DWORD));
            registry->GetValue(regKey, CONFIG_OPERDIRALREADYEXISTS, REG_DWORD, &Config.OperationsDirAlreadyExists, sizeof(DWORD));
            registry->GetValue(regKey, CONFIG_OPERRETRYONCREATFILE, REG_DWORD, &Config.OperationsRetryOnCreatedFile, sizeof(DWORD));
            registry->GetValue(regKey, CONFIG_OPERRETRYONRESUMFILE, REG_DWORD, &Config.OperationsRetryOnResumedFile, sizeof(DWORD));
            registry->GetValue(regKey, CONFIG_OPERASCIITRMODEFORBIN, REG_DWORD, &Config.OperationsAsciiTrModeButBinFile, sizeof(DWORD));
            registry->GetValue(regKey, CONFIG_OPERUNKNOWNATTRS, REG_DWORD, &Config.OperationsUnknownAttrs, sizeof(DWORD));
            registry->GetValue(regKey, CONFIG_OPERNONEMPTYDIRDEL, REG_DWORD, &Config.OperationsNonemptyDirDel, sizeof(DWORD));
            registry->GetValue(regKey, CONFIG_OPERHIDDENFILEDEL, REG_DWORD, &Config.OperationsHiddenFileDel, sizeof(DWORD));
            registry->GetValue(regKey, CONFIG_OPERHIDDENDIRDEL, REG_DWORD, &Config.OperationsHiddenDirDel, sizeof(DWORD));
        }

        if (ConfigVersion >= 22) // older versions use default values
        {
            registry->GetValue(regKey, CONFIG_UPLOADCANNOTCREATEFILE, REG_DWORD, &Config.UploadCannotCreateFile, sizeof(DWORD));
            registry->GetValue(regKey, CONFIG_UPLOADCANNOTCREATEDIR, REG_DWORD, &Config.UploadCannotCreateDir, sizeof(DWORD));
            registry->GetValue(regKey, CONFIG_UPLOADFILEALREADYEXISTS, REG_DWORD, &Config.UploadFileAlreadyExists, sizeof(DWORD));
            registry->GetValue(regKey, CONFIG_UPLOADDIRALREADYEXISTS, REG_DWORD, &Config.UploadDirAlreadyExists, sizeof(DWORD));
            registry->GetValue(regKey, CONFIG_UPLOADRETRYONCREATFILE, REG_DWORD, &Config.UploadRetryOnCreatedFile, sizeof(DWORD));

            // adjustment of a poorly chosen default - based on user feedback I changed "Overwrite" to "Resume or Overwrite"
            // (they were annoyed that after an hour of upload and a broken connection the file was overwritten instead of resumed, the risk of
            // Resume is hopefully small enough, so I changed it to "Resume or Overwrite")
            if (ConfigVersion < 29 && Config.UploadRetryOnCreatedFile == RETRYONCREATFILE_OVERWRITE)
                Config.UploadRetryOnCreatedFile = RETRYONCREATFILE_RES_OVRWR;

            registry->GetValue(regKey, CONFIG_UPLOADRETRYONRESUMFILE, REG_DWORD, &Config.UploadRetryOnResumedFile, sizeof(DWORD));
            registry->GetValue(regKey, CONFIG_UPLOADASCIITRMODEFORBIN, REG_DWORD, &Config.UploadAsciiTrModeButBinFile, sizeof(DWORD));
        }

        registry->GetValue(regKey, CONFIG_LASTBOOKMARK, REG_DWORD, &Config.LastBookmark, sizeof(DWORD));

        if (ConfigVersion != 1) // when moving from version 1 to 2 the server-types and ftp-servers lists in the registry are ignored
        {
            if (ConfigVersion < 2 || ConfigVersion >= RELOAD_PARSERS_BEFORE_CONFIG_VERSION)
            { // when upgrading to the latest version the server-types list is adjusted (ignore the old one)
                Config.LockServerTypeList()->Load(parent, regKey, registry);
                Config.UnlockServerTypeList();
            }
            Config.FTPProxyServerList.Load(parent, regKey, registry);
            Config.FTPServerList.Load(parent, regKey, registry);
        }

        registry->GetValue(regKey, CONFIG_DEFAULTFTPPRXUID, REG_DWORD, &Config.DefaultProxySrvUID, sizeof(DWORD));
        if (Config.DefaultProxySrvUID != -1 &&
            !Config.FTPProxyServerList.IsValidUID(Config.DefaultProxySrvUID))
        {
            Config.DefaultProxySrvUID = -1; // "not used"
        }

        registry->GetValue(regKey, CONFIG_ALWAYSNOTCLOSECON, REG_DWORD, &Config.AlwaysNotCloseCon, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_ALWAYSDISCONNECT, REG_DWORD, &Config.AlwaysDisconnect, sizeof(DWORD));

        registry->GetValue(regKey, CONFIG_ENABLELOGGING, REG_DWORD, &Config.EnableLogging, sizeof(DWORD));
        if (registry->GetValue(regKey, CONFIG_LOGMAXSIZE, REG_DWORD, &dw, sizeof(DWORD)))
        {
            if (dw != -1)
            {
                Config.LogMaxSize = dw;
                Config.UseLogMaxSize = TRUE;
            }
            else
                Config.UseLogMaxSize = FALSE;
        }
        if (registry->GetValue(regKey, CONFIG_MAXCLOSEDCONLOGS, REG_DWORD, &dw, sizeof(DWORD)))
        {
            if (dw != -1)
            {
                Config.MaxClosedConLogs = dw;
                Config.UseMaxClosedConLogs = TRUE;
            }
            else
                Config.UseMaxClosedConLogs = FALSE;
        }
        registry->GetValue(regKey, CONFIG_ALWAYSSHOWLOGFORACTPAN, REG_DWORD, &Config.AlwaysShowLogForActPan, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_DISABLELOGWORKERS, REG_DWORD, &Config.DisableLoggingOfWorkers, sizeof(DWORD));

        // adjustment of a poorly chosen default - logging will be enabled in workers by default (I am tired of constantly pointing people to how to enable logging + the logging overhead is minimal)
        if (ConfigVersion < 29 && Config.DisableLoggingOfWorkers)
            Config.DisableLoggingOfWorkers = FALSE;

        std::string placementText;
        if (GetValueSZ(registry, regKey, CONFIG_LOGSDLGPOSITION, placementText) &&
            !placementText.empty())
        {
            RECT rect{};
            UINT showCmd = 0;
            if (sscanf(placementText.c_str(), "%d, %d, %d, %d, %u",
                       &rect.left, &rect.top, &rect.right, &rect.bottom, &showCmd) == 5)
            {
                Config.LogsDlgPlacement.length = sizeof(WINDOWPLACEMENT);
                Config.LogsDlgPlacement.showCmd = showCmd;
                // ensure the window does not exceed the working area of the monitor where its major part lies
                RECT clipRect;
                SalamanderGeneral->MultiMonGetClipRectByRect(&rect, &clipRect, NULL);
                IntersectRect(&Config.LogsDlgPlacement.rcNormalPosition, &rect, &clipRect);
            }
        }

        if (GetValueSZ(registry, regKey, CONFIG_OPERDLGPOSITION, placementText) &&
            !placementText.empty())
        {
            RECT rect{};
            UINT showCmd = 0;
            if (sscanf(placementText.c_str(), "%d, %d, %u",
                       &rect.right, &rect.bottom, &showCmd) == 3)
            {
                Config.OperDlgPlacement.length = sizeof(WINDOWPLACEMENT);
                Config.OperDlgPlacement.showCmd = showCmd;
                // ensure the window does not exceed the working area of the monitor where its major part lies
                RECT clipRect;
                SalamanderGeneral->MultiMonGetClipRectByRect(&rect, &clipRect, NULL);
                IntersectRect(&Config.OperDlgPlacement.rcNormalPosition, &rect, &clipRect);
            }
        }

        if (registry->GetValue(regKey, CONFIG_OPERDLGSPLITPOS, REG_DWORD, &dw, sizeof(DWORD)))
        {
            Config.OperDlgSplitPos = dw / 100000.0;
            if (Config.OperDlgSplitPos < 0 || Config.OperDlgSplitPos > 1)
                Config.OperDlgSplitPos = 0.5;
        }

        LoadWideHistory(registry, regKey, CONFIG_COMMANDHISTORY, Config.CommandHistory, COMMAND_HISTORY_SIZE);
        registry->GetValue(regKey, CONFIG_SENDSECRETCOMMAND, REG_DWORD, &Config.SendSecretCommand, sizeof(DWORD));

        LoadWideHistory(registry, regKey, CONFIG_HOSTADDRESSHISTORY, Config.HostAddressHistory, HOSTADDRESS_HISTORY_SIZE);
        LoadWideHistory(registry, regKey, CONFIG_INITPATHHISTORY, Config.InitPathHistory, INITIALPATH_HISTORY_SIZE);

        registry->GetValue(regKey, CONFIG_ALWAYSRECONNECT, REG_DWORD, &Config.AlwaysReconnect, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_WARNWHENCONLOST, REG_DWORD, &Config.WarnWhenConLost, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_HINTLISTHIDDENFILES, REG_DWORD, &Config.HintListHiddenFiles, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_ALWAYSOVEWRITE, REG_DWORD, &Config.AlwaysOverwrite, sizeof(DWORD));

        registry->GetValue(regKey, CONFIG_CONVERTHEXESCSEQ, REG_DWORD, &Config.ConvertHexEscSeq, sizeof(DWORD));

        registry->GetValue(regKey, CONFIG_OPERDLGCLOSEIFSUCCESS, REG_DWORD,
                           &Config.CloseOperationDlgIfSuccessfullyFinished, sizeof(DWORD));

        // commented out because remembering this checkbox feels odd to me - I now see the meaning of the checkbox
        // in having the option to let the dialog close after the operation completes (which does not depend on the previous operation)
        //    registry->GetValue(regKey, CONFIG_OPERDLGCLOSEWHENFINISHES, REG_DWORD,
        //                       &Config.CloseOperationDlgWhenOperFinishes, sizeof(DWORD));

        registry->GetValue(regKey, CONFIG_OPENSOLVEERRIFIDLE, REG_DWORD,
                           &Config.OpenSolveErrIfIdle, sizeof(DWORD));

        registry->GetValue(regKey, CONFIG_SIMPLELSTCOLFIXEDWIDTH, REG_DWORD,
                           &CSimpleListPluginDataInterface::ListingColumnFixedWidth, sizeof(DWORD));
        registry->GetValue(regKey, CONFIG_SIMPLELSTCOLWIDTH, REG_DWORD,
                           &CSimpleListPluginDataInterface::ListingColumnWidth, sizeof(DWORD));
    }
}

void CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, ,)");

    DWORD v = CURRENT_CONFIG_VERSION;
    registry->SetValue(regKey, CONFIG_VERSION, REG_DWORD, &v, sizeof(DWORD));

    registry->SetValue(regKey, CONFIG_LASTCFGPAGE, REG_DWORD, &Config.LastCfgPage, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_SHOWWELCOMEMESSAGE, REG_DWORD, &Config.ShowWelcomeMessage, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_PRIORITYTOPANELCON, REG_DWORD, &Config.PriorityToPanelConnections, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_ENABLETOTALSPEEDLIM, REG_DWORD, &Config.EnableTotalSpeedLimit, sizeof(DWORD));
    std::string totalSpeedText;
    if (FTPFormatString(totalSpeedText, "%g", Config.TotalSpeedLimit))
        SetValueSZ(registry, regKey, CONFIG_TOTALSPEEDLIMIT, totalSpeedText.c_str());
    std::wstring anonymousPassword;
    if (Config.GetAnonymousPasswd(anonymousPassword))
    {
        SetValueStringW(registry, regKey, CONFIG_ANONYMOUSPASSWD, anonymousPassword);
        FTPSecureWipe(anonymousPassword);
    }

    registry->SetValue(regKey, CONFIG_PASSIVEMODE, REG_DWORD, &Config.PassiveMode, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_KEEPALIVE, REG_DWORD, &Config.KeepAlive, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_COMPRESSDATA, REG_DWORD, &Config.CompressData, sizeof(DWORD));

    DWORD dw = Config.UseMaxConcurrentConnections ? Config.MaxConcurrentConnections : -1;
    registry->SetValue(regKey, CONFIG_USEMAXCON, REG_DWORD, &dw, sizeof(DWORD));
    std::string serverSpeedText;
    if (FTPFormatString(serverSpeedText, "%g",
                        Config.UseServerSpeedLimit ? Config.ServerSpeedLimit : -1.0))
        SetValueSZ(registry, regKey, CONFIG_SPEEDLIM, serverSpeedText.c_str());
    registry->SetValue(regKey, CONFIG_USELISTINGSCACHE, REG_DWORD, &Config.UseListingsCache, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_TRANSFERMODE, REG_DWORD, &Config.TransferMode, sizeof(DWORD));
    // ASCII file masks are matched against LOCAL file names, so they are semantic text, not protocol
    // bytes - and both endpoints are already wide (SPLGetMasksStringOwned returns a std::wstring,
    // SetMasksString takes a const wchar_t*). Only the registry bridge narrowed them, and the
    // failure arm CLEARED the accumulated string rather than leaving the stored value alone: one
    // mask containing a character with no exact ACP mapping - reachable now that the masks dialog is
    // Unicode - wrote "" and destroyed the user's whole list, defaults included, with no error.
    //
    // REG_SZ is UTF-16 in the registry either way, so writing the wide string directly produces
    // byte-identical values for everything that was previously storable and correct values for the
    // rest. The on-disk format does not change; only the lossy hop in the middle is gone.
    SetValueStringW(registry, regKey, CONFIG_ASCIIMASKS, SPLGetMasksStringOwned(Config.ASCIIFileMasks));

    dw = Config.GetServerRepliesTimeout();
    registry->SetValue(regKey, CONFIG_SRVREPTIMEOUT, REG_DWORD, &dw, sizeof(DWORD));
    dw = Config.GetNoDataTransferTimeout();
    registry->SetValue(regKey, CONFIG_NODATATRTIMEOUT, REG_DWORD, &dw, sizeof(DWORD));
    dw = Config.GetDelayBetweenConRetries();
    registry->SetValue(regKey, CONFIG_DELBETWCONRETR, REG_DWORD, &dw, sizeof(DWORD));
    dw = Config.GetConnectRetries();
    registry->SetValue(regKey, CONFIG_CONATTEMPTS, REG_DWORD, &dw, sizeof(DWORD));
    dw = Config.GetResumeOverlap();
    registry->SetValue(regKey, CONFIG_RESUMEOVERLAP, REG_DWORD, &dw, sizeof(DWORD));
    dw = Config.GetResumeMinFileSize();
    registry->SetValue(regKey, CONFIG_RESUMEMINFILESIZE, REG_DWORD, &dw, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_KASENDEVERY, REG_DWORD, &Config.KeepAliveSendEvery, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_KASTOPAFTER, REG_DWORD, &Config.KeepAliveStopAfter, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_KACOMMAND, REG_DWORD, &Config.KeepAliveCommand, sizeof(DWORD));
    DWORD cacheMaxSize = (DWORD)Config.CacheMaxSize.Value; // storing it as a DWORD is sufficient for now
    registry->SetValue(regKey, CONFIG_CACHEMAXSIZE, REG_DWORD, &cacheMaxSize, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_DOWNLOADADDTOQUEUE, REG_DWORD, &Config.DownloadAddToQueue, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_DELETEADDTOQUEUE, REG_DWORD, &Config.DeleteAddToQueue, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_CHATTRADDTOQUEUE, REG_DWORD, &Config.ChAttrAddToQueue, sizeof(DWORD));

    registry->SetValue(regKey, CONFIG_OPERCANNOTCREATEFILE, REG_DWORD, &Config.OperationsCannotCreateFile, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_OPERCANNOTCREATEDIR, REG_DWORD, &Config.OperationsCannotCreateDir, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_OPERFILEALREADYEXISTS, REG_DWORD, &Config.OperationsFileAlreadyExists, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_OPERDIRALREADYEXISTS, REG_DWORD, &Config.OperationsDirAlreadyExists, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_OPERRETRYONCREATFILE, REG_DWORD, &Config.OperationsRetryOnCreatedFile, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_OPERRETRYONRESUMFILE, REG_DWORD, &Config.OperationsRetryOnResumedFile, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_OPERASCIITRMODEFORBIN, REG_DWORD, &Config.OperationsAsciiTrModeButBinFile, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_OPERUNKNOWNATTRS, REG_DWORD, &Config.OperationsUnknownAttrs, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_OPERNONEMPTYDIRDEL, REG_DWORD, &Config.OperationsNonemptyDirDel, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_OPERHIDDENFILEDEL, REG_DWORD, &Config.OperationsHiddenFileDel, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_OPERHIDDENDIRDEL, REG_DWORD, &Config.OperationsHiddenDirDel, sizeof(DWORD));

    registry->SetValue(regKey, CONFIG_UPLOADCANNOTCREATEFILE, REG_DWORD, &Config.UploadCannotCreateFile, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_UPLOADCANNOTCREATEDIR, REG_DWORD, &Config.UploadCannotCreateDir, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_UPLOADFILEALREADYEXISTS, REG_DWORD, &Config.UploadFileAlreadyExists, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_UPLOADDIRALREADYEXISTS, REG_DWORD, &Config.UploadDirAlreadyExists, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_UPLOADRETRYONCREATFILE, REG_DWORD, &Config.UploadRetryOnCreatedFile, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_UPLOADRETRYONRESUMFILE, REG_DWORD, &Config.UploadRetryOnResumedFile, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_UPLOADASCIITRMODEFORBIN, REG_DWORD, &Config.UploadAsciiTrModeButBinFile, sizeof(DWORD));

    registry->SetValue(regKey, CONFIG_LASTBOOKMARK, REG_DWORD, &Config.LastBookmark, sizeof(DWORD));

    Config.LockServerTypeList()->Save(parent, regKey, registry);
    Config.UnlockServerTypeList();

    CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
    if (passwordManager->IsUsingMasterPassword())
    {
        // determine whether any of the passwords is stored in an unencrypted form
        BOOL containsUnsecurePassword = Config.FTPServerList.ContainsUnsecuredPassword() || Config.FTPProxyServerList.ContainsUnsecuredPassword();
        if (containsUnsecurePassword)
        {
            if (!passwordManager->IsMasterPasswordSet())
            {
                // we do not know the master password, ask the user
                SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PASSWORD_UNSECURED).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
                passwordManager->AskForMasterPassword(parent);
            }
            if (passwordManager->IsMasterPasswordSet()) // if we already know the master password, we can encrypt
            {
                Config.FTPServerList.EncryptPasswords(parent, TRUE);
                Config.FTPProxyServerList.EncryptPasswords(parent, TRUE);
            }
        }
    }
    Config.FTPProxyServerList.Save(parent, regKey, registry);
    Config.FTPServerList.Save(parent, regKey, registry);

    registry->SetValue(regKey, CONFIG_DEFAULTFTPPRXUID, REG_DWORD, &Config.DefaultProxySrvUID, sizeof(DWORD));

    registry->SetValue(regKey, CONFIG_ALWAYSNOTCLOSECON, REG_DWORD, &Config.AlwaysNotCloseCon, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_ALWAYSDISCONNECT, REG_DWORD, &Config.AlwaysDisconnect, sizeof(DWORD));

    registry->SetValue(regKey, CONFIG_ENABLELOGGING, REG_DWORD, &Config.EnableLogging, sizeof(DWORD));
    dw = Config.UseLogMaxSize ? Config.LogMaxSize : -1;
    registry->SetValue(regKey, CONFIG_LOGMAXSIZE, REG_DWORD, &dw, sizeof(DWORD));
    dw = Config.UseMaxClosedConLogs ? Config.MaxClosedConLogs : -1;
    registry->SetValue(regKey, CONFIG_MAXCLOSEDCONLOGS, REG_DWORD, &dw, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_ALWAYSSHOWLOGFORACTPAN, REG_DWORD, &Config.AlwaysShowLogForActPan, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_DISABLELOGWORKERS, REG_DWORD, &Config.DisableLoggingOfWorkers, sizeof(DWORD));

    Logs.SaveLogsDlgPos(); // also store the position of the currently opened window
    std::string logsPlacementText;
    BOOL logsPlacementReady = TRUE;
    if (Config.LogsDlgPlacement.length != 0)
    {
        logsPlacementReady = FTPFormatString(
            logsPlacementText, "%d, %d, %d, %d, %u",
            Config.LogsDlgPlacement.rcNormalPosition.left,
            Config.LogsDlgPlacement.rcNormalPosition.top,
            Config.LogsDlgPlacement.rcNormalPosition.right,
            Config.LogsDlgPlacement.rcNormalPosition.bottom,
            Config.LogsDlgPlacement.showCmd);
    }
    if (logsPlacementReady)
        SetValueSZ(registry, regKey, CONFIG_LOGSDLGPOSITION, logsPlacementText.c_str());

    std::string operationPlacementText;
    BOOL operationPlacementReady = TRUE;
    if (Config.OperDlgPlacement.length != 0)
    {
        operationPlacementReady = FTPFormatString(
            operationPlacementText, "%d, %d, %u",
            Config.OperDlgPlacement.rcNormalPosition.right -
                Config.OperDlgPlacement.rcNormalPosition.left,
            Config.OperDlgPlacement.rcNormalPosition.bottom -
                Config.OperDlgPlacement.rcNormalPosition.top,
            Config.OperDlgPlacement.showCmd);
    }
    if (operationPlacementReady)
        SetValueSZ(registry, regKey, CONFIG_OPERDLGPOSITION, operationPlacementText.c_str());

    dw = (DWORD)(Config.OperDlgSplitPos * 100000);
    registry->SetValue(regKey, CONFIG_OPERDLGSPLITPOS, REG_DWORD, &dw, sizeof(DWORD));

    SaveWideHistory(registry, regKey, CONFIG_COMMANDHISTORY, Config.CommandHistory, COMMAND_HISTORY_SIZE);
    registry->SetValue(regKey, CONFIG_SENDSECRETCOMMAND, REG_DWORD, &Config.SendSecretCommand, sizeof(DWORD));

    SaveWideHistory(registry, regKey, CONFIG_HOSTADDRESSHISTORY, Config.HostAddressHistory, HOSTADDRESS_HISTORY_SIZE);
    SaveWideHistory(registry, regKey, CONFIG_INITPATHHISTORY, Config.InitPathHistory, INITIALPATH_HISTORY_SIZE);

    registry->SetValue(regKey, CONFIG_ALWAYSRECONNECT, REG_DWORD, &Config.AlwaysReconnect, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_WARNWHENCONLOST, REG_DWORD, &Config.WarnWhenConLost, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_HINTLISTHIDDENFILES, REG_DWORD, &Config.HintListHiddenFiles, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_ALWAYSOVEWRITE, REG_DWORD, &Config.AlwaysOverwrite, sizeof(DWORD));

    registry->SetValue(regKey, CONFIG_CONVERTHEXESCSEQ, REG_DWORD, &Config.ConvertHexEscSeq, sizeof(DWORD));

    registry->SetValue(regKey, CONFIG_OPERDLGCLOSEIFSUCCESS, REG_DWORD,
                       &Config.CloseOperationDlgIfSuccessfullyFinished, sizeof(DWORD));

    // commented out because remembering this checkbox feels odd to me - I now see the meaning of the checkbox
    // in having the option to let the dialog close after the operation completes (which does not depend on the previous operation)
    //  registry->SetValue(regKey, CONFIG_OPERDLGCLOSEWHENFINISHES, REG_DWORD,
    //                     &Config.CloseOperationDlgWhenOperFinishes, sizeof(DWORD));

    registry->SetValue(regKey, CONFIG_OPENSOLVEERRIFIDLE, REG_DWORD,
                       &Config.OpenSolveErrIfIdle, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_SIMPLELSTCOLFIXEDWIDTH, REG_DWORD,
                       &CSimpleListPluginDataInterface::ListingColumnFixedWidth, sizeof(DWORD));
    registry->SetValue(regKey, CONFIG_SIMPLELSTCOLWIDTH, REG_DWORD,
                       &CSimpleListPluginDataInterface::ListingColumnWidth, sizeof(DWORD));
}

void CPluginInterface::Configuration(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Configuration()");
    CConfigDlg(parent).Execute();
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    HBITMAP hBmp = (HBITMAP)HANDLES(LoadImage(DLLInstance, MAKEINTRESOURCE(IDC_FTPICONBMP),
                                              IMAGE_BITMAP, 16, 16, LR_DEFAULTCOLOR));
    salamander->SetBitmapWithIcons(hBmp);
    HANDLES(DeleteObject(hBmp));
    salamander->SetChangeDriveMenuItem(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPCHNGDRVITEM).c_str(), 0);
    salamander->SetPluginIcon(0);
    salamander->SetPluginMenuAndToolbarIcon(0);

    /* used by the export_mnu.py script that generates salmenu.mnu for Translator
   keep synchronized with the salamander->AddMenuItem() calls below...
MENU_TEMPLATE_ITEM PluginMenu[] = 
{
	{MNTT_PB, 0
	{MNTT_IT, IDS_CONNECTFTPSERVER
	{MNTT_IT, IDS_ORGBOOKMARKSCMD
	{MNTT_IT, IDS_SHOWLOGS
	{MNTT_IT, IDS_DISCONNECT
	{MNTT_IT, IDS_SHOWCERT
	{MNTT_PB, IDS_MENUTRANSFERMODE
	{MNTT_IT, IDS_MENUTRMODEAUTO
	{MNTT_IT, IDS_MENUTRMODEASCII
	{MNTT_IT, IDS_MENUTRMODEBINARY
	{MNTT_PE, 0
	{MNTT_IT, IDS_REFRESHPATH
	{MNTT_IT, IDS_ADDBOOKMARK
	{MNTT_IT, IDS_SENDFTPCOMMAND
	{MNTT_IT, IDS_SHOWRAWLISTING
	{MNTT_IT, IDS_LISTHIDDENFILES
	{MNTT_PE, 0
};
*/

    salamander->AddMenuItem(-1, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_CONNECTFTPSERVER).c_str(), SALHOTKEY('F', HOTKEYF_CONTROL | HOTKEYF_SHIFT),
                            FTPCMD_CONNECTFTPSERVER, FALSE, MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ORGBOOKMARKSCMD).c_str(), 0, FTPCMD_ORGANIZEBOOKMARKS, FALSE,
                            MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_SHOWLOGS).c_str(), 0, FTPCMD_SHOWLOGS, FALSE,
                            MENU_EVENT_TRUE, MENU_EVENT_TRUE,
                            MENU_SKILLLEVEL_INTERMEDIATE | MENU_SKILLLEVEL_ADVANCED);
    salamander->AddMenuItem(-1, NULL, 0, 0, FALSE, 0, 0, MENU_SKILLLEVEL_ALL); // separator
    salamander->AddMenuItem(-1, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_DISCONNECT).c_str(), SALHOTKEY_HINT,
                            FTPCMD_DISCONNECT_F12, FALSE, MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_SHOWCERT).c_str(), 0, FTPCMD_SHOWCERT, TRUE,
                            0, 0, MENU_SKILLLEVEL_ALL);
    // start of the Transfer Mode submenu
    salamander->AddSubmenuStart(-1, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_MENUTRANSFERMODE).c_str(), FTPCMD_TRMODESUBMENU, FALSE,
                                MENU_EVENT_THIS_PLUGIN_FS, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_MENUTRMODEAUTO).c_str(), 0, FTPCMD_TRMODEAUTO, TRUE,
                            0, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_MENUTRMODEASCII).c_str(), 0, FTPCMD_TRMODEASCII, TRUE,
                            0, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_MENUTRMODEBINARY).c_str(), 0, FTPCMD_TRMODEBINARY, TRUE,
                            0, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddSubmenuEnd();
    // end of the Transfer Mode submenu
    salamander->AddMenuItem(-1, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_REFRESHPATH).c_str(), 0, FTPCMD_REFRESHPATH, FALSE,
                            MENU_EVENT_THIS_PLUGIN_FS, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, NULL, 0, 0, FALSE, 0, 0, MENU_SKILLLEVEL_ALL); // separator
    salamander->AddMenuItem(-1, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ADDBOOKMARK).c_str(), 0, FTPCMD_ADDBOOKMARK, FALSE,
                            MENU_EVENT_THIS_PLUGIN_FS, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_SENDFTPCOMMAND).c_str(), 0, FTPCMD_SENDFTPCOMMAND, FALSE,
                            MENU_EVENT_THIS_PLUGIN_FS, MENU_EVENT_TRUE,
                            MENU_SKILLLEVEL_INTERMEDIATE | MENU_SKILLLEVEL_ADVANCED);
    salamander->AddMenuItem(-1, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_SHOWRAWLISTING).c_str(), 0, FTPCMD_SHOWRAWLISTING, FALSE,
                            MENU_EVENT_THIS_PLUGIN_FS, MENU_EVENT_TRUE,
                            MENU_SKILLLEVEL_ADVANCED);
    salamander->AddMenuItem(-1, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_LISTHIDDENFILES).c_str(), 0, FTPCMD_LISTHIDDENFILES, TRUE,
                            0, 0, MENU_SKILLLEVEL_ALL);
}

CPluginInterfaceForFSAbstract*
CPluginInterface::GetInterfaceForFS()
{
    return &InterfaceForFS;
}

void RefreshValuesOfPanelCtrlCon()
{
    HANDLES(EnterCriticalSection(&PanelCtrlConSect));
    CPluginFSInterface* leftFS = (CPluginFSInterface*)(SalamanderGeneral->GetPanelPluginFS(PANEL_LEFT));
    if (leftFS != NULL)
        LeftPanelCtrlCon = leftFS->GetControlConnection();
    else
        LeftPanelCtrlCon = NULL;
    CPluginFSInterface* rightFS = (CPluginFSInterface*)(SalamanderGeneral->GetPanelPluginFS(PANEL_RIGHT));
    if (rightFS != NULL)
        RightPanelCtrlCon = rightFS->GetControlConnection();
    else
        RightPanelCtrlCon = NULL;
    HANDLES(LeaveCriticalSection(&PanelCtrlConSect));
}

void CPluginInterface::Event(int event, DWORD param)
{
    if (event == PLUGINEVENT_CONFIGURATIONCHANGED)
    {
        SalamanderGeneral->GetConfigParameter(SALCFG_SORTBYEXTDIRSASFILES, &SortByExtDirsAsFiles,
                                              sizeof(SortByExtDirsAsFiles), NULL);
        SalamanderGeneral->GetConfigParameter(SALCFG_MINBEEPWHENDONE, &InactiveBeepWhenDone,
                                              sizeof(InactiveBeepWhenDone), NULL);
    }
    if (event == PLUGINEVENT_PANELSSWAPPED)
    {
        RefreshValuesOfPanelCtrlCon();
        Logs.RefreshListOfLogsInLogsDlg();
    }
    if (event == PLUGINEVENT_PANELACTIVATED)
    {
        CPluginFSInterface* fs = (CPluginFSInterface*)SalamanderGeneral->GetPanelPluginFS(PANEL_SOURCE);
        if (fs != NULL)
            fs->ActivateWelcomeMsg(); // activate the welcome message window (cannot be done from the keyboard so the user can close it at all)

        if (Config.AlwaysShowLogForActPan)
        {
            CPluginFSInterface* fs2 = (CPluginFSInterface*)(SalamanderGeneral->GetPanelPluginFS(param));
            if (fs2 != NULL)
                Logs.ActivateLog(fs2->GetLogUID());
        }
    }
}

void CPluginInterface::ClearHistory(HWND parent)
{
    int i;
    for (i = 0; i < COMMAND_HISTORY_SIZE; i++)
        FTPSecureWipe(Config.CommandHistory[i]);
    for (i = 0; i < HOSTADDRESS_HISTORY_SIZE; i++)
        Config.HostAddressHistory[i].clear();
    for (i = 0; i < INITIALPATH_HISTORY_SIZE; i++)
        Config.InitPathHistory[i].clear();
}

void CPluginInterface::AcceptChangeOnPathNotification(const wchar_t* path, BOOL includingSubdirs)
{
    // WARNING: in 'includingSubdirs' for FTP paths the value is ORed with 0x02 if it is a "soft refresh"

    const size_t pathLength = wcslen(path);
    BOOL isFTP = pathLength > AssignedFSName.size() &&
                 SalamanderGeneral->StrNICmp(path, AssignedFSName.c_str(), (int)AssignedFSName.size()) == 0 &&
                 path[AssignedFSName.size()] == L':';
    BOOL isFTPS = pathLength > AssignedFSNameFTPS.size() &&
                  SalamanderGeneral->StrNICmp(path, AssignedFSNameFTPS.c_str(), (int)AssignedFSNameFTPS.size()) == 0 &&
                  path[AssignedFSNameFTPS.size()] == L':';

    if (isFTP || isFTPS)
    {
        const size_t assignedFSNameLength = isFTP ? AssignedFSName.size() : AssignedFSNameFTPS.size();
        ListingCache.AcceptChangeOnPathNotification(path + assignedFSNameLength + 1,
                                                    (includingSubdirs & 0x01)); // drop listings for both FTP and FTPS paths

        SalamanderGeneral->RemoveFilesFromCache(path);

        std::wstring alternatePath = isFTPS ? AssignedFSName : AssignedFSNameFTPS;
        alternatePath.append(path + assignedFSNameLength);
        SalamanderGeneral->RemoveFilesFromCache(alternatePath.c_str());
    }
}

void CPluginInterface::ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData)
{
    if (pluginData != &SimpleListPluginDataInterface) // it is global, no need to release it
    {
        delete ((CFTPListingPluginDataInterface*)pluginData);
    }
}

CPluginInterfaceForMenuExtAbstract*
CPluginInterface::GetInterfaceForMenuExt()
{
    return &InterfaceForMenuExt;
}

void CPluginInterface::PasswordManagerEvent(HWND parent, int event)
{
    BOOL allPasswordsDecrypted = TRUE; // whether all passwords were successfully decrypted

    if (event == PME_MASTERPASSWORDCREATED || event == PME_MASTERPASSWORDCHANGED || event == PME_MASTERPASSWORDREMOVED)
    {
        // if the master password is being created, changed, or removed, try to convert encrypted passwords to scrambled form
        allPasswordsDecrypted &= Config.FTPServerList.EncryptPasswords(parent, FALSE);
        allPasswordsDecrypted &= Config.FTPProxyServerList.EncryptPasswords(parent, FALSE);
    }

    if (event == PME_MASTERPASSWORDCREATED || event == PME_MASTERPASSWORDCHANGED)
    {
        // if the master password is being created or changed, we must use it to encrypt the passwords
        Config.FTPServerList.EncryptPasswords(parent, TRUE);
        Config.FTPProxyServerList.EncryptPasswords(parent, TRUE);
    }

    if (!allPasswordsDecrypted)
    {
        // if at least one password could not be decrypted, inform the user about it
        SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_CANNOT_DECRYPT_SOMEPASSWORDS).c_str(),
                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
    }
}

//
// ****************************************************************************
// CFTPServer
//

void CFTPServer::Init()
{
    ItemName.clear();
    Address.clear();
    InitialPath.clear();
    AnonymousConnection = TRUE;
    UserName.clear();
    EncryptedPassword = NULL;
    EncryptedPasswordSize = 0;
    SavePassword = FALSE;
    ProxyServerUID = -2;
    TargetPanelPath.clear();
    ServerType.clear();
    TransferMode = 0;
    Port = IPPORT_FTP;
    UsePassiveMode = 2;
    KeepConnectionAlive = 2;
    KeepAliveSendEvery = Config.KeepAliveSendEvery;
    KeepAliveStopAfter = Config.KeepAliveStopAfter;
    KeepAliveCommand = Config.KeepAliveCommand;
    UseMaxConcurrentConnections = 2;
    MaxConcurrentConnections = 1;
    UseServerSpeedLimit = 2;
    ServerSpeedLimit = 2;
    UseListingsCache = 2;
    InitFTPCommands.clear();
    ListCommand.clear();
    EncryptControlConnection = EncryptDataConnection = 0;
    CompressData = -1;
    // WARNING: default values here must match the default values used in the Save() method
}

void CFTPServer::Release()
{
    if (EncryptedPassword != NULL)
    {
        memset(EncryptedPassword, 0, EncryptedPasswordSize); // clean memory containing the password
        SalamanderGeneral->Free(EncryptedPassword);
    }
    Init();
}

// Some parser/configuration records remain explicitly encoded byte structures.
// Duplicate those byte fields directly instead of routing them through the wide SDK.
static char* DupStrA(const char* s)
{
    if (s == NULL)
        return NULL;
    size_t len = strlen(s) + 1;
    char* p = (char*)SalamanderGeneral->Alloc((int)len);
    if (p != NULL)
        memcpy(p, s, len);
    return p;
}

CFTPServer*
CFTPServer::MakeCopy() noexcept
{
    CFTPServer* n = NULL;
    try
    {
        n = new CFTPServer;
        if (n != NULL && !n->Set(*this))
        {
            delete n;
            n = NULL;
        }
    }
    catch (...)
    {
        delete n;
        n = NULL;
        TRACE_E(LOW_MEMORY);
    }
    return n;
}

void UpdateStr(char*& str, const char* newStr, BOOL* err, BOOL clearMem)
{
    if (str == NULL) // there is no previous version of the string
    {
        str = DupStrA(newStr);
        if (newStr != NULL && str == NULL && err != NULL)
            *err = TRUE;
    }
    else // an older version of the string exists
    {
        if (newStr != NULL) // the new string is not NULL
        {
            if (strcmp(str, newStr) != 0) // the strings differ
            {
                if (strlen(str) < strlen(newStr)) // the new string is longer (not enough space, reallocation needed)
                {
                    char* old = str;
                    str = DupStrA(newStr);
                    if (str != NULL)
                    {
                        if (clearMem)
                            memset(old, 0, strlen(old)); // clean memory before deallocation because of passwords
                        SalamanderGeneral->Free(old);
                    }
                    else
                    {
                        str = old; // allocation failed, keep the older version of the string
                        if (err != NULL)
                            *err = TRUE;
                    }
                }
                else
                {
                    if (clearMem)
                        memset(str, 0, strlen(str)); // clean memory before shortening because of passwords
                    strcpy(str, newStr);
                }
            }
        }
        else
        {
            if (clearMem)
                memset(str, 0, strlen(str)); // clean memory before deallocation because of passwords
            free(str);
            str = NULL;
        }
    }
}

void FTPSecureWipe(std::wstring& password) noexcept
{
    if (!password.empty())
        SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
    password.clear();
}

void FTPSecureWipe(std::string& password) noexcept
{
    if (!password.empty())
        SecureZeroMemory(password.data(), password.size());
    password.clear();
}

BOOL FTPEncryptPasswordW(CSalamanderPasswordManagerAbstract* passwordManager,
                         const wchar_t* plainPassword, BYTE** encryptedPassword,
                         int* encryptedPasswordSize, BOOL encrypt) noexcept
{
    if (passwordManager == NULL || plainPassword == NULL ||
        encryptedPassword == NULL || encryptedPasswordSize == NULL)
        return FALSE;
    BYTE* stagedPassword = NULL;
    int stagedPasswordSize = 0;
    try
    {
        if (!passwordManager->EncryptPassword(plainPassword, &stagedPassword,
                                              &stagedPasswordSize, encrypt))
        {
            if (stagedPassword != NULL)
            {
                if (stagedPasswordSize > 0)
                    SecureZeroMemory(stagedPassword, stagedPasswordSize);
                SalamanderGeneral->Free(stagedPassword);
            }
            return FALSE;
        }
        *encryptedPassword = stagedPassword;
        *encryptedPasswordSize = stagedPasswordSize;
        return TRUE;
    }
    catch (...)
    {
        if (stagedPassword != NULL)
        {
            if (stagedPasswordSize > 0)
                SecureZeroMemory(stagedPassword, stagedPasswordSize);
            SalamanderGeneral->Free(stagedPassword);
        }
        return FALSE;
    }
}

BOOL FTPDecryptPasswordW(CSalamanderPasswordManagerAbstract* passwordManager,
                         const BYTE* encryptedPassword, int encryptedPasswordSize,
                         std::wstring* plainPassword) noexcept
{
    if (passwordManager == NULL)
        return FALSE;
    CSalamanderStringBufferOwner owner;
    auto wipeOwner = [&owner]() noexcept
    {
        CSalamanderStringBuffer* buffer = owner.Buffer();
        if (buffer != NULL && buffer->Data != NULL)
            SecureZeroMemory(buffer->Data, buffer->Capacity * sizeof(wchar_t));
    };
    try
    {
        if (plainPassword == NULL)
            return passwordManager->DecryptPassword(encryptedPassword, encryptedPasswordSize, NULL);

        if (!owner.IsValid() ||
            !passwordManager->DecryptPassword(encryptedPassword, encryptedPasswordSize,
                                              owner.Buffer()))
        {
            wipeOwner();
            return FALSE;
        }

        std::wstring staged;
        const BOOL result = owner.GetValue(staged);
        wipeOwner();
        if (!result)
        {
            FTPSecureWipe(staged);
            return FALSE;
        }
        plainPassword->swap(staged);
        FTPSecureWipe(staged);
        return TRUE;
    }
    catch (...)
    {
        wipeOwner();
        return FALSE;
    }
}

BOOL CFTPServer::Set(const wchar_t* itemName,
                     const wchar_t* address,
                     const wchar_t* initialPath,
                     int anonymousConnection,
                     const wchar_t* userName,
                     const BYTE* encryptedPassword,
                     int encryptedPasswordSize,
                     int savePassword,
                     int proxyServerUID,
                     const wchar_t* targetPanelPath,
                     const char* serverType,
                     int transferMode,
                     int port,
                     int usePassiveMode,
                     int keepConnectionAlive,
                     int useMaxConcurrentConnections,
                     int maxConcurrentConnections,
                     int useServerSpeedLimit,
                     double serverSpeedLimit,
                     int useListingsCache,
                     const char* initFTPCommands,
                     const char* listCommand,
                     int keepAliveSendEvery,
                     int keepAliveStopAfter,
                     int keepAliveCommand,
                     int encryptControlConnection,
                     int encryptDataConnection,
                     int compressData) noexcept
{
    if (encryptedPassword != NULL && encryptedPasswordSize <= 0)
        return FALSE;
    std::wstring stagedItemName;
    std::wstring stagedAddress;
    std::wstring stagedUserName;
    std::wstring stagedTargetPanelPath;
    std::wstring stagedInitialPath;
    std::string stagedServerType;
    std::string stagedInitFTPCommands;
    std::string stagedListCommand;
    if (!FtpStoreWideText(itemName != NULL ? itemName : L"", stagedItemName) ||
        !FtpStoreWideText(address != NULL ? address : L"", stagedAddress) ||
        !FtpStoreWideText(!anonymousConnection && userName != NULL ? userName : L"", stagedUserName) ||
        !FtpStoreWideText(targetPanelPath != NULL ? targetPanelPath : L"", stagedTargetPanelPath) ||
        !FtpStoreWideText(initialPath != NULL ? initialPath : L"", stagedInitialPath) ||
        !FtpStoreLocalTextBytes(serverType != NULL ? serverType : "", stagedServerType) ||
        !FtpStoreProtocolBytes(initFTPCommands != NULL ? initFTPCommands : "", stagedInitFTPCommands) ||
        !FtpStoreProtocolBytes(listCommand != NULL ? listCommand : "", stagedListCommand))
        return FALSE;

    BYTE* stagedEncryptedPassword = NULL;
    int stagedEncryptedPasswordSize = 0;
    if (!anonymousConnection && encryptedPassword != NULL && encryptedPasswordSize > 0)
    {
        stagedEncryptedPassword = DupEncryptedPassword(encryptedPassword, encryptedPasswordSize);
        if (stagedEncryptedPassword == NULL)
            return FALSE;
        stagedEncryptedPasswordSize = encryptedPasswordSize;
    }

    BYTE* oldEncryptedPassword = EncryptedPassword;
    int oldEncryptedPasswordSize = EncryptedPasswordSize;
    ItemName.swap(stagedItemName);
    Address.swap(stagedAddress);
    InitialPath.swap(stagedInitialPath);
    AnonymousConnection = anonymousConnection;
    UserName.swap(stagedUserName);
    EncryptedPassword = stagedEncryptedPassword;
    EncryptedPasswordSize = stagedEncryptedPasswordSize;
    SavePassword = anonymousConnection ? FALSE : savePassword;
    ProxyServerUID = proxyServerUID;
    TargetPanelPath.swap(stagedTargetPanelPath);
    ServerType.swap(stagedServerType);
    TransferMode = transferMode;
    Port = port;
    UsePassiveMode = usePassiveMode;
    KeepConnectionAlive = keepConnectionAlive;
    KeepAliveSendEvery = keepAliveSendEvery;
    KeepAliveStopAfter = keepAliveStopAfter;
    KeepAliveCommand = keepAliveCommand;
    UseMaxConcurrentConnections = useMaxConcurrentConnections;
    MaxConcurrentConnections = maxConcurrentConnections;
    UseServerSpeedLimit = useServerSpeedLimit;
    ServerSpeedLimit = serverSpeedLimit;
    InitFTPCommands.swap(stagedInitFTPCommands);
    UseListingsCache = useListingsCache;
    ListCommand.swap(stagedListCommand);
    EncryptControlConnection = encryptControlConnection;
    EncryptDataConnection = encryptDataConnection;
    CompressData = compressData;

    if (oldEncryptedPassword != NULL)
    {
        SecureZeroMemory(oldEncryptedPassword, oldEncryptedPasswordSize);
        SalamanderGeneral->Free(oldEncryptedPassword);
    }
    return TRUE;
}

const char* GetStrOrNULL(const char* s)
{
    return (s != NULL && s[0] != 0) ? s : NULL;
}

unsigned char ScrambleTable[256] =
    {
        0, 223, 235, 233, 240, 185, 88, 102, 22, 130, 27, 53, 79, 125, 66, 201,
        90, 71, 51, 60, 134, 104, 172, 244, 139, 84, 91, 12, 123, 155, 237, 151,
        192, 6, 87, 32, 211, 38, 149, 75, 164, 145, 52, 200, 224, 226, 156, 50,
        136, 190, 232, 63, 129, 209, 181, 120, 28, 99, 168, 94, 198, 40, 238, 112,
        55, 217, 124, 62, 227, 30, 36, 242, 208, 138, 174, 231, 26, 54, 214, 148,
        37, 157, 19, 137, 187, 111, 228, 39, 110, 17, 197, 229, 118, 246, 153, 80,
        21, 128, 69, 117, 234, 35, 58, 67, 92, 7, 132, 189, 5, 103, 10, 15,
        252, 195, 70, 147, 241, 202, 107, 49, 20, 251, 133, 76, 204, 73, 203, 135,
        184, 78, 194, 183, 1, 121, 109, 11, 143, 144, 171, 161, 48, 205, 245, 46,
        31, 72, 169, 131, 239, 160, 25, 207, 218, 146, 43, 140, 127, 255, 81, 98,
        42, 115, 173, 142, 114, 13, 2, 219, 57, 56, 24, 126, 3, 230, 47, 215,
        9, 44, 159, 33, 249, 18, 93, 95, 29, 113, 220, 89, 97, 182, 248, 64,
        68, 34, 4, 82, 74, 196, 213, 165, 179, 250, 108, 254, 59, 14, 236, 175,
        85, 199, 83, 106, 77, 178, 167, 225, 45, 247, 163, 158, 8, 221, 61, 191,
        119, 16, 253, 105, 186, 23, 170, 100, 216, 65, 162, 122, 150, 176, 154, 193,
        206, 222, 188, 152, 210, 243, 96, 41, 86, 180, 101, 177, 166, 141, 212, 116};

BOOL InitUnscrambleTable = TRUE;
unsigned char UnscrambleTable[256];

static BOOL UnscramblePassword(std::string& password) noexcept
{
    if (InitUnscrambleTable)
    {
        int i;
        for (i = 0; i < 256; i++)
        {
            UnscrambleTable[ScrambleTable[i]] = i;
        }
        InitUnscrambleTable = FALSE;
    }

    std::string staged;
    if (!FtpStoreProtocolBytes(password.c_str(), staged))
        return FALSE;
    char* s = staged.data();
    int last = 31;
    while (*s != 0)
    {
        int x = (int)UnscrambleTable[(unsigned char)*s] - 1 - (last % 255);
        if (x <= 0)
            x += 255;
        *s = (char)x;
        last = (last + x) % 255 + 1;
        s++;
    }

    s = staged.data();
    while (*s != 0 && (*s < '0' || *s > '9'))
        s++; // find the length of the password
    BOOL ok = FALSE;
    if (strlen(s) >= 3)
    {
        int len = (s[0] - '0') + 10 * (s[1] - '0') + 100 * (s[2] - '0');
        int total = (((len + 3) / 17) * 17 + 17);
        int passwordLen = (int)staged.size();
        if (len >= 0 && total == passwordLen && total - (s - staged.data()) - 3 == len)
        {
            staged.erase(0, passwordLen - len);
            ok = TRUE;
        }
    }
    if (!ok)
    {
        FTPSecureWipe(staged);
        TRACE_E("Unable to unscramble legacy password");
        return FALSE;
    }
    FTPSecureWipe(password);
    password.swap(staged);
    FTPSecureWipe(staged);
    return TRUE;
}

void LoadPassword(HKEY regKey, CSalamanderRegistryAbstract* registry, const wchar_t* oldPwdName, const wchar_t* scrambledPwdName, const wchar_t* encryptedPwdName, BYTE** encryptedPassword, int* encryptedPasswordSize)
{
    *encryptedPassword = NULL;
    *encryptedPasswordSize = 0;
    CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
    BOOL passwordFound = FALSE; // do we have a password?
    // in the first step try to fetch the AES-encrypted or scrambled version of the password
    DWORD gotType;
    DWORD bufferSize;
    const wchar_t* keyName = encryptedPwdName;
    LONG res = SalamanderGeneral->SalRegQueryValueEx(regKey, keyName, 0, &gotType, NULL, &bufferSize);
    if (res != ERROR_SUCCESS || gotType != REG_BINARY || bufferSize == 0)
    {
        keyName = scrambledPwdName;
        res = SalamanderGeneral->SalRegQueryValueEx(regKey, keyName, 0, &gotType, NULL, &bufferSize);
    }
    if (res == ERROR_SUCCESS && gotType == REG_BINARY && bufferSize != 0 && bufferSize <= INT_MAX)
    {
        BYTE* passwordReg = (BYTE*)SalamanderGeneral->Alloc(bufferSize);
        if (passwordReg != NULL && registry->GetValue(regKey, keyName, REG_BINARY, passwordReg, bufferSize))
        {
            *encryptedPassword = passwordReg;
            *encryptedPasswordSize = static_cast<int>(bufferSize);
            passwordFound = TRUE;
        }
        else if (passwordReg != NULL)
            SalamanderGeneral->Free(passwordReg);
    }

    // this may be the original FTP-scrambled version of the password
    if (!passwordFound)
    {
        std::wstring passwordRegW;
        std::string passwordReg;
        if (SPLRegistryGetStringOwned(registry, regKey, oldPwdName, passwordRegW) &&
            DecodeRegSzToNarrowOwned(passwordRegW.c_str(), passwordReg))
        {
            // obtain the plain password using the original FTP-scrambled method
            std::string password;
            const BOOL decoded = FtpDecodePersistedText(passwordReg.c_str(), password) &&
                                 UnscramblePassword(password);

            if (decoded && !password.empty())
            {
                // at this moment it is possible that the master password usage is enabled and MP is entered, so we could
                // keep the password encrypted in that case, but we will not complicate matters and postpone possible AES
                // encryption until saving the plug-in configuration; for now we keep the password only scrambled
                std::wstring passwordW;
                if (FtpDecodeLocalText(password.c_str(), passwordW))
                {
                    FTPEncryptPasswordW(passwordManager, passwordW.c_str(), encryptedPassword,
                                        encryptedPasswordSize, FALSE);
                    FTPSecureWipe(passwordW);
                }
            }
            FTPSecureWipe(password);
        }
        FTPSecureWipe(passwordRegW);
        FTPSecureWipe(passwordReg);
    }
}

BOOL CFTPServer::Load(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    std::wstring itemName = ItemName;
    std::wstring address = Address;
    std::wstring initialPath = InitialPath;
    int anonymousConnection;
    std::wstring userName = UserName;
    BYTE* encryptedPassword;
    int encryptedPasswordSize;
    int savePassword;
    int proxyServerUID;
    std::wstring targetPanelPath = TargetPanelPath;
    std::string serverType = ServerType;
    int transferMode;
    int port;
    int usePassiveMode;
    int keepConnectionAlive;
    int keepAliveSendEvery;
    int keepAliveStopAfter;
    int keepAliveCommand;
    int useMaxConcurrentConnections;
    int maxConcurrentConnections;
    int useServerSpeedLimit;
    double serverSpeedLimit;
    int useListingsCache;
    std::string initFTPCommands = InitFTPCommands;
    std::string listCommand = ListCommand;
    int encryptControlConnection, encryptDataConnection;
    int compressData;

    // take over default values (the object is clean, just initialized)
    anonymousConnection = AnonymousConnection;
    encryptedPassword = NULL;
    encryptedPasswordSize = 0;
    savePassword = SavePassword;
    proxyServerUID = ProxyServerUID;
    transferMode = TransferMode;
    port = Port;
    usePassiveMode = UsePassiveMode;
    keepConnectionAlive = KeepConnectionAlive;
    keepAliveSendEvery = KeepAliveSendEvery;
    keepAliveStopAfter = KeepAliveStopAfter;
    keepAliveCommand = KeepAliveCommand;
    useMaxConcurrentConnections = UseMaxConcurrentConnections;
    maxConcurrentConnections = MaxConcurrentConnections;
    useServerSpeedLimit = UseServerSpeedLimit;
    serverSpeedLimit = ServerSpeedLimit;
    useListingsCache = UseListingsCache;
    encryptControlConnection = EncryptControlConnection;
    encryptDataConnection = EncryptDataConnection;
    compressData = CompressData;

    if (!GetValueStringW(registry, regKey, CONFIG_FTPSRVNAME, itemName))
        return FALSE; // the name is mandatory
    GetValueStringW(registry, regKey, CONFIG_FTPSRVADDRESS, address);
    GetValueStringW(registry, regKey, CONFIG_FTPSRVPATH, initialPath);
    registry->GetValue(regKey, CONFIG_FTPSRVANONYM, REG_DWORD, &anonymousConnection, sizeof(DWORD));
    GetValueStringW(registry, regKey, CONFIG_FTPSRVUSER, userName);

    LoadPassword(regKey, registry, CONFIG_FTPSRVPASSWD_OLD, CONFIG_FTPSRVPASSWD_SCRAMBLED, CONFIG_FTPSRVPASSWD_ENCRYPTED, &encryptedPassword, &encryptedPasswordSize);

    registry->GetValue(regKey, CONFIG_FTPSRVSAVEPASSWD, REG_DWORD, &savePassword, sizeof(DWORD));
    if (!savePassword && encryptedPassword != NULL && encryptedPasswordSize != 0)
        savePassword = TRUE;
    registry->GetValue(regKey, CONFIG_FTPSRVPROXYSRVUID, REG_DWORD, &proxyServerUID, sizeof(DWORD));
    if (proxyServerUID != -1 && proxyServerUID != -2 &&
        !Config.FTPProxyServerList.IsValidUID(proxyServerUID))
    {
        proxyServerUID = -2; // "default"
    }
    GetValueStringW(registry, regKey, CONFIG_FTPSRVTGTPATH, targetPanelPath);
    GetValueSZ(registry, regKey, CONFIG_FTPSRVTYPE, serverType);
    registry->GetValue(regKey, CONFIG_FTPSRVTRANSFMODE, REG_DWORD, &transferMode, sizeof(DWORD));
    registry->GetValue(regKey, CONFIG_FTPSRVPORT, REG_DWORD, &port, sizeof(DWORD));
    registry->GetValue(regKey, CONFIG_FTPSRVPASV, REG_DWORD, &usePassiveMode, sizeof(DWORD));
    registry->GetValue(regKey, CONFIG_FTPSRVKALIVE, REG_DWORD, &keepConnectionAlive, sizeof(DWORD));
    registry->GetValue(regKey, CONFIG_FTPSRVKASENDEVERY, REG_DWORD, &keepAliveSendEvery, sizeof(DWORD));
    registry->GetValue(regKey, CONFIG_FTPSRVKASTOPAFTER, REG_DWORD, &keepAliveStopAfter, sizeof(DWORD));
    registry->GetValue(regKey, CONFIG_FTPSRVKACOMMAND, REG_DWORD, &keepAliveCommand, sizeof(DWORD));
    if (registry->GetValue(regKey, CONFIG_FTPSRVUSEMAXCON, REG_DWORD, &maxConcurrentConnections, sizeof(DWORD)))
    {
        useMaxConcurrentConnections = (maxConcurrentConnections == -1 ? 0 : 1);
        if (maxConcurrentConnections == -1)
            maxConcurrentConnections = MaxConcurrentConnections; // do not leave -1 there -> use the default value
    }
    std::string speedLimitText;
    if (GetValueSZ(registry, regKey, CONFIG_FTPSRVSPDLIM, speedLimitText))
    {
        serverSpeedLimit = atof(speedLimitText.c_str());
        useServerSpeedLimit = (serverSpeedLimit == -1 ? 0 : 1);
        if (serverSpeedLimit == -1)
            serverSpeedLimit = ServerSpeedLimit; // do not leave -1 there -> use the default value
    }
    registry->GetValue(regKey, CONFIG_FTPSRVUSELISTINGSCACHE, REG_DWORD, &useListingsCache, sizeof(DWORD));
    GetValueSZ(registry, regKey, CONFIG_FTPSRVINITFTPCMDS, initFTPCommands);
    GetValueSZ(registry, regKey, CONFIG_FTPSRVLISTCMD, listCommand);
    if (listCommand == LIST_CMD_TEXT)
        listCommand.clear();
    registry->GetValue(regKey, CONFIG_FTPSRVENCRYPTCONTROLCONNECTION, REG_DWORD, &encryptControlConnection, sizeof(DWORD));
    registry->GetValue(regKey, CONFIG_FTPSRVENCRYPTDATACONNECTION, REG_DWORD, &encryptDataConnection, sizeof(DWORD));
    registry->GetValue(regKey, CONFIG_FTPSRVCOMPRESSDATA, REG_DWORD, &compressData, sizeof(DWORD));

    BOOL ret = Set(itemName.c_str(),
                   address.c_str(),
                   initialPath.empty() ? NULL : initialPath.c_str(),
                   anonymousConnection,
                   userName.c_str(),
                   encryptedPassword, encryptedPasswordSize,
                   savePassword,
                   proxyServerUID,
                   targetPanelPath.c_str(),
                   GetStrOrNULL(serverType.c_str()),
                   transferMode,
                   port,
                   usePassiveMode,
                   keepConnectionAlive,
                   useMaxConcurrentConnections,
                   maxConcurrentConnections,
                   useServerSpeedLimit,
                   serverSpeedLimit,
                   useListingsCache,
                   GetStrOrNULL(initFTPCommands.c_str()),
                   GetStrOrNULL(listCommand.c_str()),
                   keepAliveSendEvery,
                   keepAliveStopAfter,
                   keepAliveCommand,
                   encryptControlConnection,
                   encryptDataConnection,
                   compressData);
    if (encryptedPassword != NULL)
    {
        memset(encryptedPassword, 0, encryptedPasswordSize);
        SalamanderGeneral->Free(encryptedPassword);
    }
    return ret;
}

BOOL IsNotEmptyStr(const char* s)
{
    return s != NULL && *s != 0;
}

BYTE* DupEncryptedPassword(const BYTE* password, int size) noexcept
{
    if (password == NULL || size <= 0)
        return NULL;

    BYTE* buf = (BYTE*)SalamanderGeneral->Alloc(size);
    if (buf != NULL)
        memcpy(buf, password, size);
    return buf;
}

BOOL UpdateEncryptedPassword(BYTE** password, int* passwordSize, const BYTE* newPassword, int newPasswordSize) noexcept
{
    if (password == NULL || passwordSize == NULL ||
        (newPassword != NULL && newPasswordSize <= 0))
        return FALSE;
    if (newPassword == *password)
        return TRUE;

    BYTE* stagedPassword = DupEncryptedPassword(newPassword, newPasswordSize);
    if (newPassword != NULL && newPasswordSize > 0 && stagedPassword == NULL)
        return FALSE;

    BYTE* oldPassword = *password;
    int oldPasswordSize = *passwordSize;
    *password = stagedPassword;
    *passwordSize = stagedPassword != NULL ? newPasswordSize : 0;
    if (oldPassword != NULL)
    {
        SecureZeroMemory(oldPassword, oldPasswordSize);
        SalamanderGeneral->Free(oldPassword);
    }
    return TRUE;
}

void CFTPServer::Save(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    SetValueStringW(registry, regKey, CONFIG_FTPSRVNAME, ItemName);
    if (!Address.empty())
        SetValueStringW(registry, regKey, CONFIG_FTPSRVADDRESS, Address);
    if (!InitialPath.empty())
        SetValueStringW(registry, regKey, CONFIG_FTPSRVPATH, InitialPath);
    registry->SetValue(regKey, CONFIG_FTPSRVANONYM, REG_DWORD, &AnonymousConnection, sizeof(DWORD));
    if (!AnonymousConnection)
    {
        if (!UserName.empty())
            SetValueStringW(registry, regKey, CONFIG_FTPSRVUSER, UserName);

        if (SavePassword)
        {
            if (EncryptedPassword != NULL && EncryptedPasswordSize > 0)
            {
                CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
                BOOL encrypted = passwordManager->IsPasswordEncrypted(EncryptedPassword, EncryptedPasswordSize);
                registry->SetValue(regKey, encrypted ? CONFIG_FTPSRVPASSWD_ENCRYPTED : CONFIG_FTPSRVPASSWD_SCRAMBLED, REG_BINARY, EncryptedPassword, EncryptedPasswordSize);
            }
            registry->SetValue(regKey, CONFIG_FTPSRVSAVEPASSWD, REG_DWORD, &SavePassword, sizeof(DWORD));
        }
    }

    if (ProxyServerUID != -2)
        registry->SetValue(regKey, CONFIG_FTPSRVPROXYSRVUID, REG_DWORD, &ProxyServerUID, sizeof(DWORD));
    if (!TargetPanelPath.empty())
        SetValueStringW(registry, regKey, CONFIG_FTPSRVTGTPATH, TargetPanelPath);
    if (!ServerType.empty())
        SetValueSZ(registry, regKey, CONFIG_FTPSRVTYPE, ServerType.c_str());
    if (TransferMode != 0)
        registry->SetValue(regKey, CONFIG_FTPSRVTRANSFMODE, REG_DWORD, &TransferMode, sizeof(DWORD));
    if (Port != IPPORT_FTP)
        registry->SetValue(regKey, CONFIG_FTPSRVPORT, REG_DWORD, &Port, sizeof(DWORD));
    if (UsePassiveMode != 2)
        registry->SetValue(regKey, CONFIG_FTPSRVPASV, REG_DWORD, &UsePassiveMode, sizeof(DWORD));
    if (KeepConnectionAlive != 2)
        registry->SetValue(regKey, CONFIG_FTPSRVKALIVE, REG_DWORD, &KeepConnectionAlive, sizeof(DWORD));
    if (KeepConnectionAlive == 1) // store only when explicitly configured
    {
        registry->SetValue(regKey, CONFIG_FTPSRVKASENDEVERY, REG_DWORD, &KeepAliveSendEvery, sizeof(DWORD));
        registry->SetValue(regKey, CONFIG_FTPSRVKASTOPAFTER, REG_DWORD, &KeepAliveStopAfter, sizeof(DWORD));
        registry->SetValue(regKey, CONFIG_FTPSRVKACOMMAND, REG_DWORD, &KeepAliveCommand, sizeof(DWORD));
    }
    if (UseMaxConcurrentConnections != 2)
    {
        DWORD dw;
        if (UseMaxConcurrentConnections == 1)
            dw = MaxConcurrentConnections;
        else
            dw = -1;
        registry->SetValue(regKey, CONFIG_FTPSRVUSEMAXCON, REG_DWORD, &dw, sizeof(DWORD));
    }
    if (UseServerSpeedLimit != 2)
    {
        std::string speedLimitText;
        if (FTPFormatString(speedLimitText, "%g",
                            UseServerSpeedLimit == 1 ? ServerSpeedLimit : -1.0))
            SetValueSZ(registry, regKey, CONFIG_FTPSRVSPDLIM, speedLimitText.c_str());
    }
    if (UseListingsCache != 2)
        registry->SetValue(regKey, CONFIG_FTPSRVUSELISTINGSCACHE, REG_DWORD, &UseListingsCache, sizeof(DWORD));
    if (!InitFTPCommands.empty())
        SetValueSZ(registry, regKey, CONFIG_FTPSRVINITFTPCMDS, InitFTPCommands.c_str());
    if (!ListCommand.empty())
        SetValueSZ(registry, regKey, CONFIG_FTPSRVLISTCMD, ListCommand.c_str());

    if (EncryptControlConnection != 0)
        registry->SetValue(regKey, CONFIG_FTPSRVENCRYPTCONTROLCONNECTION, REG_DWORD, &EncryptControlConnection, sizeof(DWORD));
    if (EncryptDataConnection != 0)
        registry->SetValue(regKey, CONFIG_FTPSRVENCRYPTDATACONNECTION, REG_DWORD, &EncryptDataConnection, sizeof(DWORD));
    if (CompressData != -1)
        registry->SetValue(regKey, CONFIG_FTPSRVCOMPRESSDATA, REG_DWORD, &CompressData, sizeof(DWORD));
}

BOOL CFTPServer::EnsurePasswordCanBeDecrypted(HWND hParent)
{
    CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
    if (!AnonymousConnection && EncryptedPassword != NULL &&
        passwordManager->IsPasswordEncrypted(EncryptedPassword, EncryptedPasswordSize))
    {
        // verify whether a master password is needed to decrypt the password
        if (passwordManager->IsUsingMasterPassword() && !passwordManager->IsMasterPasswordSet())
        {
            if (!passwordManager->AskForMasterPassword(hParent))
                return FALSE; // the user did not enter the correct master password
        }
        // verify that this is the correct master password for this password
        if (!FTPDecryptPasswordW(passwordManager, EncryptedPassword,
                                 EncryptedPasswordSize, NULL))
        {
            int ret = SalamanderGeneral->SalMessageBox(hParent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_CANNOT_DECRYPT_PASSWORD_DELETE).c_str(),
                                                       SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(), MB_YESNO | MSGBOXEX_ESCAPEENABLED | MB_DEFBUTTON2 | MB_ICONEXCLAMATION);
            if (ret == IDNO)
                return FALSE; // failed to decrypt the password

            // the user wanted to delete the password
            UpdateEncryptedPassword(&EncryptedPassword, &EncryptedPasswordSize, NULL, 0);
            // clear the save password checkbox
            SavePassword = FALSE;
        }
    }
    return TRUE;
}
