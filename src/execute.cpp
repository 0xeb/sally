// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "plugins.h"
#include "usermenu.h"
#include "execute.h"
#include "cfgdlg.h"
#include "shellib.h"
#include "ui/IPrompter.h"
#include "common/unicode/helpers.h"
#include "common/unicode/WideVariableExpansion.h"
#include "common/widepath.h"
#include "common/IEnvironment.h"
#include "common/IPathService.h"
#include "common/fsutil.h"

//******************************************************************************
//
// CComboboxEdit
//

CComboboxEdit::CComboboxEdit()
    : CWindow(ooAllocated)
{
    SelStart = 0;
    SelEnd = -1;
}


LRESULT
CComboboxEdit::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CComboboxEdit::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_KILLFOCUS:
    {
        SendMessage(HWindow, EM_GETSEL, (WPARAM)&SelStart, (LPARAM)&SelEnd);
        break;
    }

    case EM_REPLACESEL:
    {
        LRESULT res = CWindow::WindowProc(uMsg, wParam, lParam);
        SendMessage(HWindow, EM_GETSEL, (WPARAM)&SelStart, (LPARAM)&SelEnd);
        return res;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}

void CComboboxEdit::GetSel(DWORD* start, DWORD* end)
{
    if (GetFocus() == HWindow)
        SendMessage(HWindow, EM_GETSEL, (WPARAM)start, (LPARAM)end);
    else
    {
        *start = SelStart;
        *end = SelEnd;
    }
}

void CComboboxEdit::ReplaceText(const wchar_t* text)
{
    // we must refresh the selection because the dumb combobox forgot it
    SendMessageW(HWindow, EM_SETSEL, SelStart, SelEnd);
    SendMessageW(HWindow, EM_REPLACESEL, TRUE, (LPARAM)text);
}

//******************************************************************************
//
// Keywords
//

const wchar_t* EXECUTE_DRIVE = L"Drive";
const wchar_t* EXECUTE_PATH = L"Path";
const wchar_t* EXECUTE_DOSPATH = L"DOSPath";
const wchar_t* EXECUTE_NAME = L"Name";
const wchar_t* EXECUTE_DOSNAME = L"DOSName";
const wchar_t* EXECUTE_FULLNAME = L"FullName";
const wchar_t* EXECUTE_DOSFULLNAME = L"DOSFullName";
const wchar_t* EXECUTE_FULLPATH = L"FullPath";
const wchar_t* EXECUTE_WINDIR = L"WinDir";
const wchar_t* EXECUTE_SYSDIR = L"SysDir";
const wchar_t* EXECUTE_SALDIR = L"SalDir";
const wchar_t* EXECUTE_DOSFULLPATH = L"DOSFullPath";
const wchar_t* EXECUTE_DOSWINDIR = L"DOSWinDir";
const wchar_t* EXECUTE_DOSSYSDIR = L"DOSSysDir";
const wchar_t* EXECUTE_NAMEPART = L"NamePart";
const wchar_t* EXECUTE_EXTPART = L"ExtPart";
const wchar_t* EXECUTE_DOSNAMEPART = L"DOSNamePart";
const wchar_t* EXECUTE_DOSEXTPART = L"DOSExtPart";
const wchar_t* EXECUTE_FULLPATHINACTIVE = L"FullPathInactive";
const wchar_t* EXECUTE_FULLPATHLEFT = L"FullPathLeft";
const wchar_t* EXECUTE_FULLPATHRIGHT = L"FullPathRight";
const wchar_t* EXECUTE_COMPAREDFILELEFT = L"FileToCompareLeft";
const wchar_t* EXECUTE_COMPAREDFILERIGHT = L"FileToCompareRight";
const wchar_t* EXECUTE_COMPAREDDIRLEFT = L"DirToCompareLeft";
const wchar_t* EXECUTE_COMPAREDDIRRIGHT = L"DirToCompareRight";
const wchar_t* EXECUTE_COMPAREDFILEACT = L"FileToCompareActive";
const wchar_t* EXECUTE_COMPAREDFILEINACT = L"FileToCompareInactive";
const wchar_t* EXECUTE_COMPAREDDIRACT = L"DirToCompareActive";
const wchar_t* EXECUTE_COMPAREDDIRINACT = L"DirToCompareInactive";
const wchar_t* EXECUTE_COMPAREDLEFT = L"FileOrDirToCompareLeft";
const wchar_t* EXECUTE_COMPAREDRIGHT = L"FileOrDirToCompareRight";
const wchar_t* EXECUTE_COMPAREDACT = L"FileOrDirToCompareActive";
const wchar_t* EXECUTE_COMPAREDINACT = L"FileOrDirToCompareInactive";
const wchar_t* EXECUTE_LISTOFSELNAMES = L"ListOfSelectedNames";
const wchar_t* EXECUTE_LISTOFSELFULLNAMES = L"ListOfSelectedFullNames";

const wchar_t* EXECUTE_ENV = L"$[]";

// dummy strings
// !!! strings must not have the same value because the release build
// would redirect pointers to a single instance during optimizations
const wchar_t* EXECUTE_SEPARATOR = L"Separator";
const wchar_t* EXECUTE_BROWSE = L"Browse";       // directory browsing using the Open dialog
const wchar_t* EXECUTE_BROWSEDIR = L"BrowseDir"; // directory browsing using GetTargetDirectory
const wchar_t* EXECUTE_HELP = L"Help";
const wchar_t* EXECUTE_TERMINATOR = L"Terminator";
const wchar_t* EXECUTE_SUBMENUSTART = L"SubMenuStart";
const wchar_t* EXECUTE_SUBMENUEND = L"SubMenuEnd";

struct CExecuteWideExpData
{
    const wchar_t* Name;
    const wchar_t* DosName;
    BOOL* FileNameUsed;

    CUserMenuAdvancedData* UserMenuAdvancedData; // applies only to User Menu, otherwise NULL here
};

void ExecuteMarkFileNameUsed(CExecuteWideExpData* data)
{
    if (data->FileNameUsed != NULL)
        *data->FileNameUsed = TRUE;
}

std::wstring ExecutePathValueW(const wchar_t* name, BOOL trailingBackslash,
                               const char* caller)
{
    if (name == NULL)
    {
        TRACE_E("Unexpected NULL value in " << caller << ".");
        return L"\\";
    }

    std::wstring root = GetRootPath(name);
    std::size_t rootLength = root.length();
    if (rootLength > 0 && root[rootLength - 1] == L'\\')
        --rootLength;
    const std::wstring fullName(name);
    if (rootLength > fullName.length())
    {
        TRACE_E("Unexpected root length in " << caller << ".");
        return L"\\";
    }

    std::wstring path = fullName.substr(rootLength);
    const std::size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos)
    {
        TRACE_E("Unexpected value in " << caller << ".");
        return L"\\";
    }
    if (trailingBackslash)
        path.resize(slash + 1);
    else if (slash == 0)
        path.resize(1); // root must retain its backslash
    else
        path.resize(slash);
    return path;
}

std::wstring ExecuteNameValueW(const wchar_t* name, const char* caller)
{
    if (name == NULL)
    {
        TRACE_E("Unexpected NULL value in " << caller << ".");
        return std::wstring();
    }
    const wchar_t* slash = wcsrchr(name, L'\\');
    if (slash == NULL)
    {
        TRACE_E("Unexpected value in " << caller << ".");
        return std::wstring();
    }
    return slash + 1;
}

std::wstring ExecuteFullNameValueW(const wchar_t* name)
{
    if (name == NULL)
        return std::wstring();
    const wchar_t* slash = wcsrchr(name, L'\\');
    return slash != NULL && slash[1] == 0 ? std::wstring() : std::wstring(name);
}

std::wstring ExecuteNamePartValueW(const wchar_t* name, const char* caller)
{
    std::wstring value = ExecuteNameValueW(name, caller);
    const std::size_t dot = value.find_last_of(L'.');
    if (dot != std::wstring::npos)
        value.resize(dot); // ".cvspass" is considered an extension in Windows
    return value;
}

std::wstring ExecuteExtPartValueW(const wchar_t* name, const char* caller)
{
    const std::wstring value = ExecuteNameValueW(name, caller);
    const std::size_t dot = value.find_last_of(L'.');
    return dot != std::wstring::npos ? value.substr(dot + 1) : std::wstring();
}

std::wstring ExecuteFullPathValueW(const wchar_t* name, BOOL trailingBackslash,
                                   const char* caller)
{
    if (name == NULL)
    {
        TRACE_E("Unexpected NULL value in " << caller << ".");
        return L"C:\\";
    }
    std::wstring value(name);
    const std::size_t slash = value.find_last_of(L'\\');
    if (slash == std::wstring::npos)
    {
        TRACE_E("Unexpected value in " << caller << ".");
        return L"C:\\";
    }
    if (trailingBackslash || (slash == 2 && value[1] == L':'))
        value.resize(slash + 1);
    else
        value.resize(slash);
    return value;
}

std::wstring ExecuteDirectoryValueW(BOOL windowsDirectory, BOOL trailingBackslash)
{
    std::wstring value;
    if (gEnvironment == NULL)
    {
        TRACE_E("Unable to retrieve system directory for Execute expansion.");
        return std::wstring();
    }
    const EnvResult result = windowsDirectory ? gEnvironment->GetWindowsDirectory(value)
                                              : gEnvironment->GetSystemDirectory(value);
    if (!result.success)
    {
        TRACE_E("Unable to retrieve system directory for Execute expansion.");
        return std::wstring();
    }
    if (trailingBackslash && !value.empty() && value.back() != L'\\')
        value.push_back(L'\\');
    if (!trailingBackslash && !value.empty() && value.back() == L'\\')
        value.pop_back();
    return value;
}

std::wstring ExecuteShortDirectoryValueW(BOOL windowsDirectory)
{
    const std::wstring path = ExecuteDirectoryValueW(windowsDirectory, FALSE);
    if (path.empty())
        return path;
    std::wstring shortPath = GetShortPathW(path.c_str());
    if (!shortPath.empty() && shortPath.back() != L'\\')
        shortPath.push_back(L'\\');
    return shortPath;
}

std::wstring ExecuteSalDirValueW(BOOL trailingBackslash)
{
    std::wstring value;
    if (gPathService == NULL || !gPathService->GetModuleFileName(HInstance, value).success)
    {
        TRACE_E("Unable to retrieve Sally module path for Execute expansion.");
        return L"C:\\";
    }
    const std::size_t slash = value.find_last_of(L'\\');
    if (slash == std::wstring::npos)
    {
        TRACE_E("Unexpected value in ExecuteSalDirValueW().");
        return L"C:\\";
    }
    if (trailingBackslash || (slash == 2 && value[1] == L':'))
        value.resize(slash + 1);
    else
        value.resize(slash);
    return value;
}

std::wstring ExecuteExpDriveW(void* param)
{
    CExecuteWideExpData* data = (CExecuteWideExpData*)param;
    ExecuteMarkFileNameUsed(data);
    std::wstring root = GetRootPath(data->Name);
    if (!root.empty() && root.back() == L'\\')
        root.pop_back();
    return root;
}

std::wstring ExecuteExpPathW(void* param)
{
    CExecuteWideExpData* data = (CExecuteWideExpData*)param;
    ExecuteMarkFileNameUsed(data);
    return ExecutePathValueW(data->Name, TRUE, "ExecuteExpPathW");
}

std::wstring ExecuteExpDOSPathW(void* param)
{
    CExecuteWideExpData* data = (CExecuteWideExpData*)param;
    ExecuteMarkFileNameUsed(data);
    return ExecutePathValueW(data->DosName, TRUE, "ExecuteExpDOSPathW");
}

std::wstring ExecuteExpNameW(void* param)
{
    CExecuteWideExpData* data = (CExecuteWideExpData*)param;
    ExecuteMarkFileNameUsed(data);
    return ExecuteNameValueW(data->Name, "ExecuteExpNameW");
}

std::wstring ExecuteExpDOSNameW(void* param)
{
    CExecuteWideExpData* data = (CExecuteWideExpData*)param;
    ExecuteMarkFileNameUsed(data);
    return ExecuteNameValueW(data->DosName, "ExecuteExpDOSNameW");
}

std::wstring ExecuteExpPath2W(void* param)
{
    CExecuteWideExpData* data = (CExecuteWideExpData*)param;
    ExecuteMarkFileNameUsed(data);
    return ExecutePathValueW(data->Name, FALSE, "ExecuteExpPath2W");
}

std::wstring ExecuteExpFullNameW(void* param)
{
    CExecuteWideExpData* data = (CExecuteWideExpData*)param;
    ExecuteMarkFileNameUsed(data);
    return ExecuteFullNameValueW(data->Name);
}

std::wstring ExecuteExpDOSFullNameW(void* param)
{
    CExecuteWideExpData* data = (CExecuteWideExpData*)param;
    ExecuteMarkFileNameUsed(data);
    return ExecuteFullNameValueW(data->DosName);
}

std::wstring ExecuteExpNamePartW(void* param)
{
    CExecuteWideExpData* data = (CExecuteWideExpData*)param;
    ExecuteMarkFileNameUsed(data);
    return ExecuteNamePartValueW(data->Name, "ExecuteExpNamePartW");
}

std::wstring ExecuteExpExtPartW(void* param)
{
    CExecuteWideExpData* data = (CExecuteWideExpData*)param;
    ExecuteMarkFileNameUsed(data);
    return ExecuteExtPartValueW(data->Name, "ExecuteExpExtPartW");
}

std::wstring ExecuteExpDOSNamePartW(void* param)
{
    CExecuteWideExpData* data = (CExecuteWideExpData*)param;
    ExecuteMarkFileNameUsed(data);
    return ExecuteNamePartValueW(data->DosName, "ExecuteExpDOSNamePartW");
}

std::wstring ExecuteExpDOSExtPartW(void* param)
{
    CExecuteWideExpData* data = (CExecuteWideExpData*)param;
    ExecuteMarkFileNameUsed(data);
    return ExecuteExtPartValueW(data->DosName, "ExecuteExpDOSExtPartW");
}

std::wstring ExecuteExpFullPathW(void* param)
{
    CExecuteWideExpData* data = (CExecuteWideExpData*)param;
    ExecuteMarkFileNameUsed(data);
    return ExecuteFullPathValueW(data->Name, TRUE, "ExecuteExpFullPathW");
}

std::wstring ExecuteExpWinDirW(void*) { return ExecuteDirectoryValueW(TRUE, TRUE); }
std::wstring ExecuteExpSysDirW(void*) { return ExecuteDirectoryValueW(FALSE, TRUE); }
std::wstring ExecuteExpSalDirW(void*) { return ExecuteSalDirValueW(TRUE); }

std::wstring ExecuteExpDOSFullPathW(void* param)
{
    CExecuteWideExpData* data = (CExecuteWideExpData*)param;
    ExecuteMarkFileNameUsed(data);
    return ExecuteFullPathValueW(data->DosName, TRUE, "ExecuteExpDOSFullPathW");
}

std::wstring ExecuteExpDOSWinDirW(void*) { return ExecuteShortDirectoryValueW(TRUE); }
std::wstring ExecuteExpDOSSysDirW(void*) { return ExecuteShortDirectoryValueW(FALSE); }

std::wstring ExecuteExpFullPath2W(void* param)
{
    CExecuteWideExpData* data = (CExecuteWideExpData*)param;
    ExecuteMarkFileNameUsed(data);
    return ExecuteFullPathValueW(data->Name, FALSE, "ExecuteExpFullPath2W");
}

std::wstring ExecuteExpWinDir2W(void*) { return ExecuteDirectoryValueW(TRUE, FALSE); }
std::wstring ExecuteExpSysDir2W(void*) { return ExecuteDirectoryValueW(FALSE, FALSE); }
std::wstring ExecuteExpSalDir2W(void*) { return ExecuteSalDirValueW(FALSE); }

// Information Line Content
const wchar_t* FILEDATA_FILENAME = L"FileName";
const wchar_t* FILEDATA_FILESIZE = L"FileSize";
const wchar_t* FILEDATA_FILEDATE = L"FileDate";
const wchar_t* FILEDATA_FILETIME = L"FileTime";
const wchar_t* FILEDATA_FILEATTR = L"FileAttributes";
const wchar_t* FILEDATA_FILEDOSNAME = L"FileDOSName";

// for Make File List
const wchar_t* FILEDATA_FILENAMEPART = L"FileNamePart";
const wchar_t* FILEDATA_FILEEXTENSION = L"FileExtension";

// text file delimiter
const wchar_t* FILEDATA_LF = L"LF";
const wchar_t* FILEDATA_CR = L"CR";
const wchar_t* FILEDATA_CRLF = L"CRLF";
const wchar_t* FILEDATA_TAB = L"TAB";

// string displayed when no valid data exist for the requested variable
const wchar_t* STR_FILE_DATA_NONE = L"-";

// strings for regular expressions
const wchar_t* REGEXP_ANYCHAR = L".";
const wchar_t* REGEXP_SETOFCHAR = L"[]";
const wchar_t* REGEXP_NOTSETOFCHAR = L"[^]";
const wchar_t* REGEXP_RANGEOFCHAR = L"[-]";
const wchar_t* REGEXP_BEGINOFLINE = L"^";
const wchar_t* REGEXP_ENDOFLINE = L"$";
const wchar_t* REGEXP_OR = L"|";
const wchar_t* REGEXP_0ORMORE = L"*";
const wchar_t* REGEXP_1ORMORE = L"+";
const wchar_t* REGEXP_0OR1 = L"?";

const wchar_t* REGEXP_PARENTHESIS_L_CHAR = L"\\(";
const wchar_t* REGEXP_PARENTHESIS_R_CHAR = L"\\)";
const wchar_t* REGEXP_DOT_CHAR = L"\\.";
const wchar_t* REGEXP_PLUS_CHAR = L"\\+";
const wchar_t* REGEXP_ASTERISK_CHAR = L"\\*";

const wchar_t* REGEXP_ALPHANUMERIC_CHAR = L"[a-zA-Z0-9]";
const wchar_t* REGEXP_ALPHABETIC_CHAR = L"[a-zA-Z]";
const wchar_t* REGEXP_DECIMAL_DIGIT = L"[0-9]";

const wchar_t* REGEXP_DECIMAL_NUMBER = L"([0-9]+)";
const wchar_t* REGEXP_HEXADECIMAL_NUMBER = L"([0-9a-fA-F]+)";
const wchar_t* REGEXP_REAL_NUMBER = L"(([0-9]+\\.[0-9]*)|([0-9]*\\.[0-9]+)|([0-9]+))";

const wchar_t* REGEXP_QUOTED_STRING = L"((\"[^\"]*\")|('[^']*'))";
const wchar_t* REGEXP_ALPHABETIC_STRING = L"([a-zA-Z]+)";
const wchar_t* REGEXP_IDENTIFIER = L"([a-zA-Z_$][a-zA-Z0-9_$]*)";

//******************************************************************************
//
// Predefined arrays
//
//

// Arguments - User Menu

/* used by the export_mnu.py script that generates salmenu.mnu for the Translator
   keep synchronized with the array below...
MENU_TEMPLATE_ITEM UserMenuArgsExecutes[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_EXECUTE_FULLNAME
  {MNTT_IT, IDS_EXECUTE_DRIVE
  {MNTT_IT, IDS_EXECUTE_PATH
  {MNTT_IT, IDS_EXECUTE_NAME
  {MNTT_IT, IDS_EXECUTE_NAMEPART
  {MNTT_IT, IDS_EXECUTE_EXTPART
  {MNTT_IT, IDS_EXECUTE_FULLPATH
  {MNTT_IT, IDS_EXECUTE_WINDIR
  {MNTT_IT, IDS_EXECUTE_SYSDIR
  {MNTT_IT, IDS_EXECUTE_DOSFULLNAME
  {MNTT_IT, IDS_EXECUTE_DOSPATH
  {MNTT_IT, IDS_EXECUTE_DOSNAME
  {MNTT_IT, IDS_EXECUTE_DOSNAMEPART
  {MNTT_IT, IDS_EXECUTE_DOSEXTPART
  {MNTT_IT, IDS_EXECUTE_DOSFULLPATH
  {MNTT_IT, IDS_EXECUTE_DOSWINDIR
  {MNTT_IT, IDS_EXECUTE_DOSSYSDIR
  {MNTT_IT, IDS_EXECUTE_ENV
  {MNTT_PB, IDS_EXECUTE_ADVANCEDMENU
  {MNTT_IT, IDS_EXECUTE_FULLPATHINACTIVE
  {MNTT_IT, IDS_EXECUTE_FULLPATHLEFT
  {MNTT_IT, IDS_EXECUTE_FULLPATHRIGHT
  {MNTT_IT, IDS_EXECUTE_COMPAREDFILELEFT
  {MNTT_IT, IDS_EXECUTE_COMPAREDFILERIGHT
  {MNTT_IT, IDS_EXECUTE_COMPAREDDIRLEFT
  {MNTT_IT, IDS_EXECUTE_COMPAREDDIRRIGHT
  {MNTT_IT, IDS_EXECUTE_COMPAREDLEFT
  {MNTT_IT, IDS_EXECUTE_COMPAREDRIGHT
  {MNTT_IT, IDS_EXECUTE_COMPAREDFILEACT
  {MNTT_IT, IDS_EXECUTE_COMPAREDFILEINACT
  {MNTT_IT, IDS_EXECUTE_COMPAREDDIRACT
  {MNTT_IT, IDS_EXECUTE_COMPAREDDIRINACT
  {MNTT_IT, IDS_EXECUTE_COMPAREDACT
  {MNTT_IT, IDS_EXECUTE_COMPAREDINACT
  {MNTT_IT, IDS_EXECUTE_LISTOFSELNAMES
  {MNTT_IT, IDS_EXECUTE_LISTOFSELFULLNAMES
  {MNTT_PE, 0
  {MNTT_PE, 0
};
*/

CExecuteItem UserMenuArgsExecutes[] =
    {
        {EXECUTE_FULLNAME, IDS_EXECUTE_FULLNAME, EIF_VARIABLE},
        {EXECUTE_DRIVE, IDS_EXECUTE_DRIVE, EIF_VARIABLE},
        {EXECUTE_PATH, IDS_EXECUTE_PATH, EIF_VARIABLE},
        {EXECUTE_NAME, IDS_EXECUTE_NAME, EIF_VARIABLE},
        {EXECUTE_NAMEPART, IDS_EXECUTE_NAMEPART, EIF_VARIABLE},
        {EXECUTE_EXTPART, IDS_EXECUTE_EXTPART, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_FULLPATH, IDS_EXECUTE_FULLPATH, EIF_VARIABLE},
        {EXECUTE_WINDIR, IDS_EXECUTE_WINDIR, EIF_VARIABLE},
        {EXECUTE_SYSDIR, IDS_EXECUTE_SYSDIR, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_DOSFULLNAME, IDS_EXECUTE_DOSFULLNAME, EIF_VARIABLE},
        {EXECUTE_DOSPATH, IDS_EXECUTE_DOSPATH, EIF_VARIABLE},
        {EXECUTE_DOSNAME, IDS_EXECUTE_DOSNAME, EIF_VARIABLE},
        {EXECUTE_DOSNAMEPART, IDS_EXECUTE_DOSNAMEPART, EIF_VARIABLE},
        {EXECUTE_DOSEXTPART, IDS_EXECUTE_DOSEXTPART, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_DOSFULLPATH, IDS_EXECUTE_DOSFULLPATH, EIF_VARIABLE},
        {EXECUTE_DOSWINDIR, IDS_EXECUTE_DOSWINDIR, EIF_VARIABLE},
        {EXECUTE_DOSSYSDIR, IDS_EXECUTE_DOSSYSDIR, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_ENV, IDS_EXECUTE_ENV, EIF_CURSOR_1},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_SUBMENUSTART, IDS_EXECUTE_ADVANCEDMENU, 0},
        {EXECUTE_FULLPATHINACTIVE, IDS_EXECUTE_FULLPATHINACTIVE, EIF_VARIABLE},
        {EXECUTE_FULLPATHLEFT, IDS_EXECUTE_FULLPATHLEFT, EIF_VARIABLE},
        {EXECUTE_FULLPATHRIGHT, IDS_EXECUTE_FULLPATHRIGHT, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_COMPAREDFILELEFT, IDS_EXECUTE_COMPAREDFILELEFT, EIF_VARIABLE},
        {EXECUTE_COMPAREDFILERIGHT, IDS_EXECUTE_COMPAREDFILERIGHT, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_COMPAREDDIRLEFT, IDS_EXECUTE_COMPAREDDIRLEFT, EIF_VARIABLE},
        {EXECUTE_COMPAREDDIRRIGHT, IDS_EXECUTE_COMPAREDDIRRIGHT, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_COMPAREDLEFT, IDS_EXECUTE_COMPAREDLEFT, EIF_VARIABLE},
        {EXECUTE_COMPAREDRIGHT, IDS_EXECUTE_COMPAREDRIGHT, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_COMPAREDFILEACT, IDS_EXECUTE_COMPAREDFILEACT, EIF_VARIABLE},
        {EXECUTE_COMPAREDFILEINACT, IDS_EXECUTE_COMPAREDFILEINACT, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_COMPAREDDIRACT, IDS_EXECUTE_COMPAREDDIRACT, EIF_VARIABLE},
        {EXECUTE_COMPAREDDIRINACT, IDS_EXECUTE_COMPAREDDIRINACT, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_COMPAREDACT, IDS_EXECUTE_COMPAREDACT, EIF_VARIABLE},
        {EXECUTE_COMPAREDINACT, IDS_EXECUTE_COMPAREDINACT, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_LISTOFSELNAMES, IDS_EXECUTE_LISTOFSELNAMES, EIF_VARIABLE},
        {EXECUTE_LISTOFSELFULLNAMES, IDS_EXECUTE_LISTOFSELFULLNAMES, EIF_VARIABLE},
        {EXECUTE_SUBMENUEND, 0, 0},
        {EXECUTE_TERMINATOR, 0, 0},
};

// HotPath

/* used by the export_mnu.py script that generates salmenu.mnu for the Translator
   keep synchronized with the array below...
MENU_TEMPLATE_ITEM HotPathItems[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_EXECUTE_BROWSE
  {MNTT_IT, IDS_EXECUTE_WINDIR
  {MNTT_IT, IDS_EXECUTE_SYSDIR
  {MNTT_IT, IDS_EXECUTE_SALDIR
  {MNTT_IT, IDS_EXECUTE_ENV
  {MNTT_PE, 0
};
*/

CExecuteItem HotPathItems[] =
    {
        {EXECUTE_BROWSEDIR, IDS_EXECUTE_BROWSE, EIF_REPLACE_ALL},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_WINDIR, IDS_EXECUTE_WINDIR, EIF_VARIABLE},
        {EXECUTE_SYSDIR, IDS_EXECUTE_SYSDIR, EIF_VARIABLE},
        {EXECUTE_SALDIR, IDS_EXECUTE_SALDIR, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_ENV, IDS_EXECUTE_ENV, EIF_CURSOR_1},
        {EXECUTE_TERMINATOR, 0, 0},
};

// Command - external View/Edit

/* used by the export_mnu.py script that generates salmenu.mnu for the Translator
   keep synchronized with the array below...
MENU_TEMPLATE_ITEM CommandExecutes[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_EXECUTE_BROWSE
  {MNTT_IT, IDS_EXECUTE_WINDIR
  {MNTT_IT, IDS_EXECUTE_SYSDIR
  {MNTT_IT, IDS_EXECUTE_SALDIR
  {MNTT_IT, IDS_EXECUTE_ENV
  {MNTT_PE, 0
};
*/

CExecuteItem CommandExecutes[] =
    {
        {EXECUTE_BROWSE, IDS_EXECUTE_BROWSE, EIF_REPLACE_ALL},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_WINDIR, IDS_EXECUTE_WINDIR, EIF_VARIABLE},
        {EXECUTE_SYSDIR, IDS_EXECUTE_SYSDIR, EIF_VARIABLE},
        {EXECUTE_SALDIR, IDS_EXECUTE_SALDIR, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_ENV, IDS_EXECUTE_ENV, EIF_CURSOR_1},
        {EXECUTE_TERMINATOR, 0, 0},
};

// Arguments - External View/Edit

/* used by the export_mnu.py script that generates salmenu.mnu for the Translator
   keep synchronized with the array below...
MENU_TEMPLATE_ITEM ArgumentsExecutes[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_EXECUTE_FULLNAME
  {MNTT_IT, IDS_EXECUTE_DRIVE
  {MNTT_IT, IDS_EXECUTE_PATH
  {MNTT_IT, IDS_EXECUTE_NAME
  {MNTT_IT, IDS_EXECUTE_NAMEPART
  {MNTT_IT, IDS_EXECUTE_EXTPART
  {MNTT_IT, IDS_EXECUTE_FULLPATH
  {MNTT_IT, IDS_EXECUTE_WINDIR
  {MNTT_IT, IDS_EXECUTE_SYSDIR
  {MNTT_IT, IDS_EXECUTE_DOSFULLNAME
  {MNTT_IT, IDS_EXECUTE_DOSPATH
  {MNTT_IT, IDS_EXECUTE_DOSNAME
  {MNTT_IT, IDS_EXECUTE_DOSNAMEPART
  {MNTT_IT, IDS_EXECUTE_DOSEXTPART
  {MNTT_IT, IDS_EXECUTE_DOSFULLPATH
  {MNTT_IT, IDS_EXECUTE_DOSWINDIR
  {MNTT_IT, IDS_EXECUTE_DOSSYSDIR
  {MNTT_IT, IDS_EXECUTE_ENV
  {MNTT_PE, 0
};
*/

CExecuteItem ArgumentsExecutes[] =
    {
        {EXECUTE_FULLNAME, IDS_EXECUTE_FULLNAME, EIF_VARIABLE},
        {EXECUTE_DRIVE, IDS_EXECUTE_DRIVE, EIF_VARIABLE},
        {EXECUTE_PATH, IDS_EXECUTE_PATH, EIF_VARIABLE},
        {EXECUTE_NAME, IDS_EXECUTE_NAME, EIF_VARIABLE},
        {EXECUTE_NAMEPART, IDS_EXECUTE_NAMEPART, EIF_VARIABLE},
        {EXECUTE_EXTPART, IDS_EXECUTE_EXTPART, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_FULLPATH, IDS_EXECUTE_FULLPATH, EIF_VARIABLE},
        {EXECUTE_WINDIR, IDS_EXECUTE_WINDIR, EIF_VARIABLE},
        {EXECUTE_SYSDIR, IDS_EXECUTE_SYSDIR, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_DOSFULLNAME, IDS_EXECUTE_DOSFULLNAME, EIF_VARIABLE},
        {EXECUTE_DOSPATH, IDS_EXECUTE_DOSPATH, EIF_VARIABLE},
        {EXECUTE_DOSNAME, IDS_EXECUTE_DOSNAME, EIF_VARIABLE},
        {EXECUTE_DOSNAMEPART, IDS_EXECUTE_DOSNAMEPART, EIF_VARIABLE},
        {EXECUTE_DOSEXTPART, IDS_EXECUTE_DOSEXTPART, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_DOSFULLPATH, IDS_EXECUTE_DOSFULLPATH, EIF_VARIABLE},
        {EXECUTE_DOSWINDIR, IDS_EXECUTE_DOSWINDIR, EIF_VARIABLE},
        {EXECUTE_DOSSYSDIR, IDS_EXECUTE_DOSSYSDIR, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_ENV, IDS_EXECUTE_ENV, EIF_CURSOR_1},
        {EXECUTE_TERMINATOR, 0, 0},
};

// Initial directory

/* used by the export_mnu.py script that generates salmenu.mnu for the Translator
   keep synchronized with the array below...
MENU_TEMPLATE_ITEM InitDirExecutes[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_EXECUTE_FULLPATH
  {MNTT_IT, IDS_EXECUTE_WINDIR
  {MNTT_IT, IDS_EXECUTE_SYSDIR
  {MNTT_IT, IDS_EXECUTE_SALDIR
  {MNTT_IT, IDS_EXECUTE_DRIVE
  {MNTT_IT, IDS_EXECUTE_PATH
  {MNTT_IT, IDS_EXECUTE_ENV
  {MNTT_PE, 0
};
*/

CExecuteItem InitDirExecutes[] =
    {
        {EXECUTE_FULLPATH, IDS_EXECUTE_FULLPATH, EIF_VARIABLE},
        {EXECUTE_WINDIR, IDS_EXECUTE_WINDIR, EIF_VARIABLE},
        {EXECUTE_SYSDIR, IDS_EXECUTE_SYSDIR, EIF_VARIABLE},
        {EXECUTE_SALDIR, IDS_EXECUTE_SALDIR, EIF_VARIABLE},
        {EXECUTE_DRIVE, IDS_EXECUTE_DRIVE, EIF_VARIABLE},
        {EXECUTE_PATH, IDS_EXECUTE_PATH, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_ENV, IDS_EXECUTE_ENV, EIF_CURSOR_1},
        {EXECUTE_TERMINATOR, 0, 0},
};

// Information Line Content

/* used by the export_mnu.py script that generates salmenu.mnu for the Translator
   keep synchronized with the array below...
MENU_TEMPLATE_ITEM InfoLineContentItems[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_FILEDATA_FILENAME
  {MNTT_IT, IDS_FILEDATA_FILESIZE
  {MNTT_IT, IDS_FILEDATA_FILEDATE
  {MNTT_IT, IDS_FILEDATA_FILETIME
  {MNTT_IT, IDS_FILEDATA_FILEATTR
  {MNTT_IT, IDS_FILEDATA_FILEDOSNAME
  {MNTT_PE, 0
};
*/

CExecuteItem InfoLineContentItems[] =
    {
        {FILEDATA_FILENAME, IDS_FILEDATA_FILENAME, EIF_VARIABLE},
        {FILEDATA_FILESIZE, IDS_FILEDATA_FILESIZE, EIF_VARIABLE},
        {FILEDATA_FILEDATE, IDS_FILEDATA_FILEDATE, EIF_VARIABLE},
        {FILEDATA_FILETIME, IDS_FILEDATA_FILETIME, EIF_VARIABLE},
        {FILEDATA_FILEATTR, IDS_FILEDATA_FILEATTR, EIF_VARIABLE},
        {FILEDATA_FILEDOSNAME, IDS_FILEDATA_FILEDOSNAME, EIF_VARIABLE},
        {EXECUTE_TERMINATOR, 0, 0},
};

// Make File List Items

/* used by the export_mnu.py script that generates salmenu.mnu for the Translator
   keep synchronized with the array below...
MENU_TEMPLATE_ITEM MakeFileListItems[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_FILEDATA_FILENAME
  {MNTT_IT, IDS_FILEDATA_FILENAMEPART
  {MNTT_IT, IDS_FILEDATA_FILEEXTENSION
  {MNTT_IT, IDS_FILEDATA_FILESIZE
  {MNTT_IT, IDS_FILEDATA_FILEDATE
  {MNTT_IT, IDS_FILEDATA_FILETIME
  {MNTT_IT, IDS_FILEDATA_FILEATTR
  {MNTT_IT, IDS_FILEDATA_FILEDOSNAME
  {MNTT_IT, IDS_EXECUTE_DRIVE
  {MNTT_IT, IDS_EXECUTE_PATH
  {MNTT_IT, IDS_EXECUTE_DOSPATH
  {MNTT_IT, IDS_FILEDATA_LF
  {MNTT_IT, IDS_FILEDATA_CR
  {MNTT_IT, IDS_FILEDATA_CRLF
  {MNTT_IT, IDS_FILEDATA_TAB
  {MNTT_IT, IDS_EXECUTE_ENV
  {MNTT_PE, 0
};
*/

CExecuteItem MakeFileListItems[] =
    {
        {FILEDATA_FILENAME, IDS_FILEDATA_FILENAME, EIF_VARIABLE},
        {FILEDATA_FILENAMEPART, IDS_FILEDATA_FILENAMEPART, EIF_VARIABLE},
        {FILEDATA_FILEEXTENSION, IDS_FILEDATA_FILEEXTENSION, EIF_VARIABLE},
        {FILEDATA_FILESIZE, IDS_FILEDATA_FILESIZE, EIF_VARIABLE},
        {FILEDATA_FILEDATE, IDS_FILEDATA_FILEDATE, EIF_VARIABLE},
        {FILEDATA_FILETIME, IDS_FILEDATA_FILETIME, EIF_VARIABLE},
        {FILEDATA_FILEATTR, IDS_FILEDATA_FILEATTR, EIF_VARIABLE},
        {FILEDATA_FILEDOSNAME, IDS_FILEDATA_FILEDOSNAME, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_DRIVE, IDS_EXECUTE_DRIVE, EIF_VARIABLE},
        {EXECUTE_PATH, IDS_EXECUTE_PATH, EIF_VARIABLE},
        {EXECUTE_DOSPATH, IDS_EXECUTE_DOSPATH, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {FILEDATA_LF, IDS_FILEDATA_LF, EIF_VARIABLE},
        {FILEDATA_CR, IDS_FILEDATA_CR, EIF_VARIABLE},
        {FILEDATA_CRLF, IDS_FILEDATA_CRLF, EIF_VARIABLE},
        {FILEDATA_TAB, IDS_FILEDATA_TAB, EIF_VARIABLE},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_ENV, IDS_EXECUTE_ENV, EIF_CURSOR_1},
        {EXECUTE_TERMINATOR, 0, 0},
};

// Regular Expression Items

/* used by the export_mnu.py script that generates salmenu.mnu for the Translator
   keep synchronized with the array below...
MENU_TEMPLATE_ITEM RegularExpressionItems[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_REGEXP_ANYCHAR
  {MNTT_IT, IDS_REGEXP_SETOFCHAR
  {MNTT_IT, IDS_REGEXP_NOTSETOFCHAR
  {MNTT_IT, IDS_REGEXP_RANGEOFCHAR
  {MNTT_IT, IDS_REGEXP_BEGINOFLINE
  {MNTT_IT, IDS_REGEXP_ENDOFLINE
  {MNTT_IT, IDS_REGEXP_OR
  {MNTT_IT, IDS_REGEXP_0ORMORE
  {MNTT_IT, IDS_REGEXP_1ORMORE
  {MNTT_IT, IDS_REGEXP_0OR1
  {MNTT_IT, IDS_REGEXP_PARENTHESIS_L_CHAR
  {MNTT_IT, IDS_REGEXP_PARENTHESIS_R_CHAR
  {MNTT_IT, IDS_REGEXP_DOT_CHAR
  {MNTT_IT, IDS_REGEXP_PLUS_CHAR
  {MNTT_IT, IDS_REGEXP_ASTERISK_CHAR
  {MNTT_IT, IDS_REGEXP_ALPHANUMERIC_CHAR
  {MNTT_IT, IDS_REGEXP_ALPHABETIC_CHAR
  {MNTT_IT, IDS_REGEXP_DECIMAL_DIGIT
  {MNTT_IT, IDS_REGEXP_DECIMAL_NUMBER
  {MNTT_IT, IDS_REGEXP_HEXADECIMAL_NUMBER
  {MNTT_IT, IDS_REGEXP_REAL_NUMBER
  {MNTT_IT, IDS_REGEXP_ALPHABETIC_STRING
  {MNTT_IT, IDS_REGEXP_IDENTIFIER
  {MNTT_IT, IDS_REGEXP_QUOTED_STRING
  {MNTT_IT, IDS_REGEXP_HELP
  {MNTT_PE, 0
};
*/

CExecuteItem RegularExpressionItems[] =
    {
        {REGEXP_ANYCHAR, IDS_REGEXP_ANYCHAR, 0},
        {REGEXP_SETOFCHAR, IDS_REGEXP_SETOFCHAR, EIF_CURSOR_1},
        {REGEXP_NOTSETOFCHAR, IDS_REGEXP_NOTSETOFCHAR, EIF_CURSOR_1},
        {REGEXP_RANGEOFCHAR, IDS_REGEXP_RANGEOFCHAR, EIF_CURSOR_2},
        {REGEXP_BEGINOFLINE, IDS_REGEXP_BEGINOFLINE, 0},
        {REGEXP_ENDOFLINE, IDS_REGEXP_ENDOFLINE, 0},
        {REGEXP_OR, IDS_REGEXP_OR, 0},
        {REGEXP_0ORMORE, IDS_REGEXP_0ORMORE, 0},
        {REGEXP_1ORMORE, IDS_REGEXP_1ORMORE, 0},
        {REGEXP_0OR1, IDS_REGEXP_0OR1, 0},
        {EXECUTE_SEPARATOR, 0, 0},
        {REGEXP_PARENTHESIS_L_CHAR, IDS_REGEXP_PARENTHESIS_L_CHAR, 0},
        {REGEXP_PARENTHESIS_R_CHAR, IDS_REGEXP_PARENTHESIS_R_CHAR, 0},
        {REGEXP_DOT_CHAR, IDS_REGEXP_DOT_CHAR, 0},
        {REGEXP_PLUS_CHAR, IDS_REGEXP_PLUS_CHAR, 0},
        {REGEXP_ASTERISK_CHAR, IDS_REGEXP_ASTERISK_CHAR, 0},
        {EXECUTE_SEPARATOR, 0, 0},
        {REGEXP_ALPHANUMERIC_CHAR, IDS_REGEXP_ALPHANUMERIC_CHAR, 0},
        {REGEXP_ALPHABETIC_CHAR, IDS_REGEXP_ALPHABETIC_CHAR, 0},
        {REGEXP_DECIMAL_DIGIT, IDS_REGEXP_DECIMAL_DIGIT, 0},
        {EXECUTE_SEPARATOR, 0, 0},
        {REGEXP_DECIMAL_NUMBER, IDS_REGEXP_DECIMAL_NUMBER, 0},
        {REGEXP_HEXADECIMAL_NUMBER, IDS_REGEXP_HEXADECIMAL_NUMBER, 0},
        {REGEXP_REAL_NUMBER, IDS_REGEXP_REAL_NUMBER, 0},
        {EXECUTE_SEPARATOR, 0, 0},
        {REGEXP_ALPHABETIC_STRING, IDS_REGEXP_ALPHABETIC_STRING, 0},
        {REGEXP_IDENTIFIER, IDS_REGEXP_IDENTIFIER, 0},
        {REGEXP_QUOTED_STRING, IDS_REGEXP_QUOTED_STRING, 0},
        {EXECUTE_SEPARATOR, 0, 0},
        {EXECUTE_HELP, IDS_REGEXP_HELP, EIF_DONT_FOCUS},
        {EXECUTE_TERMINATOR, 0, 0},
};

//******************************************************************************
//
// Custom functions
//

// Information Line Content

struct CFileDataExpData
{
    CPluginDataInterfaceEncapsulation* PluginData;
    const CFileData* FileData;
    BOOL IsDir;          // this is a file, not a directory
    DWORD ValidFileData; // mask of valid data in CFileData
    // The only active file-data expansion route is wide. This keeps
    // a Make File List path in the same representation that reaches its writer.
    std::wstring PathW;
};

std::wstring FileDataEffectiveNameW(const CFileData* file)
{
    return file != NULL && file->Name != NULL ? std::wstring(file->Name) : std::wstring();
}

size_t FileDataExtensionOffsetW(const CFileData* file, const std::wstring& nameW, BOOL isDir)
{
    if (file == NULL || file->Name == NULL || file->Ext == NULL || isDir || nameW.empty())
        return std::wstring::npos;

    const size_t extOffset = (size_t)(file->Ext - file->Name);
    return extOffset <= nameW.length() ? extOffset : sally::unicode::ExtensionOffsetAfterDotW(nameW, FALSE);
}

BOOL GetFileDataExpansionSizeW(CFileDataExpData* data, CQuadWord& size, BOOL& pluginSize)
{
    pluginSize = FALSE;
    if (data->ValidFileData & VALID_DATA_SIZE)
    {
        size = data->FileData->Size;
        return TRUE;
    }
    if ((data->ValidFileData & VALID_DATA_PL_SIZE) &&
        data->PluginData->NotEmpty() &&
        data->PluginData->GetByteSize(data->FileData, data->IsDir, &size))
    {
        pluginSize = TRUE;
        return TRUE;
    }
    return FALSE;
}

std::wstring FileDataDirectoryColumnW()
{
    return LoadStrW(IDS_DIRCOLUMN);
}

std::wstring FormatFileDateW(const SYSTEMTIME& st)
{
    wchar_t buffer[50];
    if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, buffer, _countof(buffer)) == 0)
        swprintf_s(buffer, _countof(buffer), L"%u.%u.%u", st.wDay, st.wMonth, st.wYear);
    return buffer;
}

std::wstring FormatFileTimeW(const SYSTEMTIME& st)
{
    wchar_t buffer[50];
    if (GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, buffer, _countof(buffer)) == 0)
        swprintf_s(buffer, _countof(buffer), L"%u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

BOOL IsEmptyUpDirectoryDateW(const CFileDataExpData* data, const SYSTEMTIME& st)
{
    return st.wYear == 1602 && st.wMonth == 1 && st.wDay == 1 && st.wHour == 0 &&
           st.wMinute == 0 && st.wSecond == 0 && st.wMilliseconds == 0 &&
           data->FileData->Name != NULL && wcscmp(data->FileData->Name, L"..") == 0;
}

std::wstring FileDataExpFileNameW(void* param)
{
    CFileDataExpData* data = (CFileDataExpData*)param;
    std::wstring nameW = FileDataEffectiveNameW(data->FileData);
    return AlterFileNameW(nameW.c_str(), Configuration.FileNameFormat, 0, data->IsDir);
}

std::wstring FileDataExpFileNamePartW(void* param)
{
    CFileDataExpData* data = (CFileDataExpData*)param;
    std::wstring nameW = FileDataEffectiveNameW(data->FileData);
    std::wstring formatted = AlterFileNameW(nameW.c_str(), Configuration.FileNameFormat, 0, data->IsDir);
    size_t extOffset = FileDataExtensionOffsetW(data->FileData, nameW, data->IsDir);
    return sally::unicode::FileNamePartFromExtensionOffsetW(formatted, extOffset);
}

std::wstring FileDataExpFileExtensionW(void* param)
{
    CFileDataExpData* data = (CFileDataExpData*)param;
    std::wstring nameW = FileDataEffectiveNameW(data->FileData);
    std::wstring formatted = AlterFileNameW(nameW.c_str(), Configuration.FileNameFormat, 0, data->IsDir);
    size_t extOffset = FileDataExtensionOffsetW(data->FileData, nameW, data->IsDir);
    return sally::unicode::FileExtensionFromExtensionOffsetW(formatted, extOffset);
}

std::wstring FileDataExpFileSizeW(void* param)
{
    CFileDataExpData* data = (CFileDataExpData*)param;
    CQuadWord size;
    BOOL pluginSize = FALSE;
    BOOL sizeValid = GetFileDataExpansionSizeW(data, size, pluginSize);
    if (!sizeValid && !data->IsDir)
        return STR_FILE_DATA_NONE;
    if (!data->IsDir || ((data->ValidFileData & VALID_DATA_SIZE) && data->FileData->SizeValid) || pluginSize)
    {
        return NumberToStr(size);
    }
    return FileDataDirectoryColumnW();
}

std::wstring FileDataExpFileSizeNoSpacesW(void* param)
{
    CFileDataExpData* data = (CFileDataExpData*)param;
    CQuadWord size;
    BOOL pluginSize = FALSE;
    BOOL sizeValid = GetFileDataExpansionSizeW(data, size, pluginSize);
    if (!sizeValid && !data->IsDir)
        return STR_FILE_DATA_NONE;
    if (!data->IsDir || ((data->ValidFileData & VALID_DATA_SIZE) && data->FileData->SizeValid) || pluginSize)
        return std::to_wstring(size.Value);
    return FileDataDirectoryColumnW();
}

std::wstring FileDataExpFileDateW(void* param)
{
    CFileDataExpData* data = (CFileDataExpData*)param;
    SYSTEMTIME st;
    FILETIME ft;
    if ((data->ValidFileData & VALID_DATA_DATE) == 0 &&
        ((data->ValidFileData & VALID_DATA_PL_DATE) == 0 ||
         !data->PluginData->NotEmpty() ||
         !data->PluginData->GetLastWriteDate(data->FileData, data->IsDir, &st)))
    {
        return STR_FILE_DATA_NONE;
    }
    if ((data->ValidFileData & VALID_DATA_DATE) == 0)
    {
        st.wHour = 0;
        st.wMinute = 0;
        st.wSecond = 0;
        st.wMilliseconds = 0;
    }
    if ((data->ValidFileData & VALID_DATA_DATE) == 0 ||
        FileTimeToLocalFileTime(&data->FileData->LastWrite, &ft) &&
            FileTimeToSystemTime(&ft, &st))
    {
        return FormatFileDateW(st);
    }
    return LoadStrW(IDS_INVALID_DATEORTIME);
}

std::wstring FileDataExpFileDateOnlyForDiskW(void* param)
{
    CFileDataExpData* data = (CFileDataExpData*)param;
    SYSTEMTIME st;
    FILETIME ft;
    if (!FileTimeToLocalFileTime(&data->FileData->LastWrite, &ft) ||
        !FileTimeToSystemTime(&ft, &st))
    {
        return LoadStrW(IDS_INVALID_DATEORTIME);
    }
    if (IsEmptyUpDirectoryDateW(data, st))
        return STR_FILE_DATA_NONE;
    return FormatFileDateW(st);
}

std::wstring FileDataExpFileTimeW(void* param)
{
    CFileDataExpData* data = (CFileDataExpData*)param;
    SYSTEMTIME st;
    FILETIME ft;
    if ((data->ValidFileData & VALID_DATA_TIME) == 0 &&
        ((data->ValidFileData & VALID_DATA_PL_TIME) == 0 ||
         !data->PluginData->NotEmpty() ||
         !data->PluginData->GetLastWriteTime(data->FileData, data->IsDir, &st)))
    {
        return STR_FILE_DATA_NONE;
    }
    if ((data->ValidFileData & VALID_DATA_TIME) == 0)
    {
        st.wYear = 2000;
        st.wMonth = 12;
        st.wDay = 24;
        st.wDayOfWeek = 0;
    }
    if ((data->ValidFileData & VALID_DATA_TIME) == 0 ||
        FileTimeToLocalFileTime(&data->FileData->LastWrite, &ft) &&
            FileTimeToSystemTime(&ft, &st))
    {
        return FormatFileTimeW(st);
    }
    return LoadStrW(IDS_INVALID_DATEORTIME);
}

std::wstring FileDataExpFileTimeOnlyForDiskW(void* param)
{
    CFileDataExpData* data = (CFileDataExpData*)param;
    SYSTEMTIME st;
    FILETIME ft;
    if (!FileTimeToLocalFileTime(&data->FileData->LastWrite, &ft) ||
        !FileTimeToSystemTime(&ft, &st))
    {
        return LoadStrW(IDS_INVALID_DATEORTIME);
    }
    if (IsEmptyUpDirectoryDateW(data, st))
        return STR_FILE_DATA_NONE;
    return FormatFileTimeW(st);
}

std::wstring FileDataExpFileAttrW(void* param)
{
    CFileDataExpData* data = (CFileDataExpData*)param;
    if ((data->ValidFileData & VALID_DATA_ATTRIBUTES) == 0)
        return STR_FILE_DATA_NONE;

    std::wstring attributes;
    if (data->FileData->Attr & FILE_ATTRIBUTE_READONLY)
        attributes += L'R';
    if (data->FileData->Attr & FILE_ATTRIBUTE_HIDDEN)
        attributes += L'H';
    if (data->FileData->Attr & FILE_ATTRIBUTE_SYSTEM)
        attributes += L'S';
    if (data->FileData->Attr & FILE_ATTRIBUTE_ARCHIVE)
        attributes += L'A';
    if (data->FileData->Attr & FILE_ATTRIBUTE_TEMPORARY)
        attributes += L'T';
    if (data->FileData->Attr & FILE_ATTRIBUTE_COMPRESSED)
        attributes += L'C';
    if (data->FileData->Attr & FILE_ATTRIBUTE_ENCRYPTED)
        attributes += L'E';
    if (data->FileData->Attr & FILE_ATTRIBUTE_OFFLINE)
        attributes += L'O';
    return attributes.empty() ? L"-" : attributes;
}

std::wstring FileDataExpFileDOSNameW(void* param)
{
    CFileDataExpData* data = (CFileDataExpData*)param;
    if ((data->ValidFileData & VALID_DATA_DOSNAME) == 0)
        return STR_FILE_DATA_NONE;
    return data->FileData->DosName != NULL ? data->FileData->DosName : data->FileData->Name;
}

std::wstring MFLFileDataPathW(const std::wstring& fullPath)
{
    std::wstring root = GetRootPath(fullPath.c_str());
    size_t rootLength = root.length();
    if (rootLength > 0 && root[rootLength - 1] == L'\\')
        rootLength--;
    std::wstring result = fullPath.length() >= rootLength ? fullPath.substr(rootLength) : std::wstring();
    if (result.empty() || result.back() != L'\\')
        result += L'\\';
    return result;
}

std::wstring MFLFileDataExpDriveW(void* param)
{
    CFileDataExpData* data = (CFileDataExpData*)param;
    std::wstring root = GetRootPath(data->PathW.c_str());
    if (!root.empty() && root.back() == L'\\')
        root.pop_back();
    return root;
}

std::wstring MFLFileDataExpPathW(void* param)
{
    CFileDataExpData* data = (CFileDataExpData*)param;
    return MFLFileDataPathW(data->PathW);
}

std::wstring MFLFileDataExpDOSPathW(void* param)
{
    CFileDataExpData* data = (CFileDataExpData*)param;
    std::wstring shortPath = GetShortPathW(data->PathW.c_str());
    if (shortPath.empty())
    {
        TRACE_E("Unexpected situation in MFLFileDataExpDOSPathW().");
        return MFLFileDataExpPathW(param);
    }
    return MFLFileDataPathW(shortPath);
}

std::wstring FileDataExpLFW(void* param)
{
    return L"\n";
}

std::wstring FileDataExpCRW(void* param)
{
    return L"\r";
}

std::wstring FileDataExpCRLFW(void* param)
{
    return L"\r\n";
}

std::wstring FileDataExpTABW(void* param)
{
    return L"\t";
}

const char* WINAPI ExecuteValDummy(HWND msgParent, void* param)
{
    return "";
}

const char* WINAPI ExecuteValOneByOne(HWND msgParent, void* param)
{
    CUserMenuValidationData* data = (CUserMenuValidationData*)param;
    data->MustHandleItemsOneByOne = TRUE;
    return "";
}

const char* WINAPI ExecuteValFullPathInact(HWND msgParent, void* param)
{
    CUserMenuValidationData* data = (CUserMenuValidationData*)param;
    data->UsesFullPathInactive = TRUE;
    return "";
}

const char* WINAPI ExecuteValFullPathLeft(HWND msgParent, void* param)
{
    CUserMenuValidationData* data = (CUserMenuValidationData*)param;
    data->UsesFullPathLeft = TRUE;
    return "";
}

const char* WINAPI ExecuteValFullPathRight(HWND msgParent, void* param)
{
    CUserMenuValidationData* data = (CUserMenuValidationData*)param;
    data->UsesFullPathRight = TRUE;
    return "";
}

const char* WINAPI ExecuteValCompFileLeft(HWND msgParent, void* param)
{
    CUserMenuValidationData* data = (CUserMenuValidationData*)param;
    data->MustHandleItemsAsGroup = TRUE;
    if (data->UsedCompareType == 0)
        data->UsedCompareType = 1 /* file-left-right */;
    else
    {
        if (data->UsedCompareType != 1)
            data->UsedCompareType = 5 /* collision of multiple types */;
    }
    data->UsedCompareLeftOrActive = TRUE;
    return "";
}

const char* WINAPI ExecuteValCompFileRight(HWND msgParent, void* param)
{
    CUserMenuValidationData* data = (CUserMenuValidationData*)param;
    data->MustHandleItemsAsGroup = TRUE;
    if (data->UsedCompareType == 0)
        data->UsedCompareType = 1 /* file-left-right */;
    else
    {
        if (data->UsedCompareType != 1)
            data->UsedCompareType = 5 /* collision of multiple types */;
    }
    data->UsedCompareRightOrInactive = TRUE;
    return "";
}

const char* WINAPI ExecuteValCompDirLeft(HWND msgParent, void* param)
{
    CUserMenuValidationData* data = (CUserMenuValidationData*)param;
    data->MustHandleItemsAsGroup = TRUE;
    if (data->UsedCompareType == 0)
        data->UsedCompareType = 3 /* dir-left-right */;
    else
    {
        if (data->UsedCompareType != 3)
            data->UsedCompareType = 5 /* collision of multiple types */;
    }
    data->UsedCompareLeftOrActive = TRUE;
    return "";
}

const char* WINAPI ExecuteValCompDirRight(HWND msgParent, void* param)
{
    CUserMenuValidationData* data = (CUserMenuValidationData*)param;
    data->MustHandleItemsAsGroup = TRUE;
    if (data->UsedCompareType == 0)
        data->UsedCompareType = 3 /* dir-left-right */;
    else
    {
        if (data->UsedCompareType != 3)
            data->UsedCompareType = 5 /* collision of multiple types */;
    }
    data->UsedCompareRightOrInactive = TRUE;
    return "";
}

const char* WINAPI ExecuteValCompLeft(HWND msgParent, void* param)
{
    CUserMenuValidationData* data = (CUserMenuValidationData*)param;
    data->MustHandleItemsAsGroup = TRUE;
    if (data->UsedCompareType == 0)
        data->UsedCompareType = 6 /* file-or-dir-left-right */;
    else
    {
        if (data->UsedCompareType != 6)
            data->UsedCompareType = 5 /* collision of multiple types */;
    }
    data->UsedCompareLeftOrActive = TRUE;
    return "";
}

const char* WINAPI ExecuteValCompRight(HWND msgParent, void* param)
{
    CUserMenuValidationData* data = (CUserMenuValidationData*)param;
    data->MustHandleItemsAsGroup = TRUE;
    if (data->UsedCompareType == 0)
        data->UsedCompareType = 6 /* file-or-dir-left-right */;
    else
    {
        if (data->UsedCompareType != 6)
            data->UsedCompareType = 5 /* collision of multiple types */;
    }
    data->UsedCompareRightOrInactive = TRUE;
    return "";
}

const char* WINAPI ExecuteValCompFileActive(HWND msgParent, void* param)
{
    CUserMenuValidationData* data = (CUserMenuValidationData*)param;
    data->MustHandleItemsAsGroup = TRUE;
    if (data->UsedCompareType == 0)
        data->UsedCompareType = 2 /* file-active-inactive */;
    else
    {
        if (data->UsedCompareType != 2)
            data->UsedCompareType = 5 /* collision of multiple types */;
    }
    data->UsedCompareLeftOrActive = TRUE;
    return "";
}

const char* WINAPI ExecuteValCompFileInact(HWND msgParent, void* param)
{
    CUserMenuValidationData* data = (CUserMenuValidationData*)param;
    data->MustHandleItemsAsGroup = TRUE;
    if (data->UsedCompareType == 0)
        data->UsedCompareType = 2 /* file-active-inactive */;
    else
    {
        if (data->UsedCompareType != 2)
            data->UsedCompareType = 5 /* collision of multiple types */;
    }
    data->UsedCompareRightOrInactive = TRUE;
    return "";
}

const char* WINAPI ExecuteValCompDirActive(HWND msgParent, void* param)
{
    CUserMenuValidationData* data = (CUserMenuValidationData*)param;
    data->MustHandleItemsAsGroup = TRUE;
    if (data->UsedCompareType == 0)
        data->UsedCompareType = 4 /* dir-active-inactive */;
    else
    {
        if (data->UsedCompareType != 4)
            data->UsedCompareType = 5 /* collision of multiple types */;
    }
    data->UsedCompareLeftOrActive = TRUE;
    return "";
}

const char* WINAPI ExecuteValCompDirInact(HWND msgParent, void* param)
{
    CUserMenuValidationData* data = (CUserMenuValidationData*)param;
    data->MustHandleItemsAsGroup = TRUE;
    if (data->UsedCompareType == 0)
        data->UsedCompareType = 4 /* dir-active-inactive */;
    else
    {
        if (data->UsedCompareType != 4)
            data->UsedCompareType = 5 /* collision of multiple types */;
    }
    data->UsedCompareRightOrInactive = TRUE;
    return "";
}

const char* WINAPI ExecuteValCompActive(HWND msgParent, void* param)
{
    CUserMenuValidationData* data = (CUserMenuValidationData*)param;
    data->MustHandleItemsAsGroup = TRUE;
    if (data->UsedCompareType == 0)
        data->UsedCompareType = 7 /* file-or-dir-active-inactive */;
    else
    {
        if (data->UsedCompareType != 7)
            data->UsedCompareType = 5 /* collision of multiple types */;
    }
    data->UsedCompareLeftOrActive = TRUE;
    return "";
}

const char* WINAPI ExecuteValCompInact(HWND msgParent, void* param)
{
    CUserMenuValidationData* data = (CUserMenuValidationData*)param;
    data->MustHandleItemsAsGroup = TRUE;
    if (data->UsedCompareType == 0)
        data->UsedCompareType = 7 /* file-or-dir-active-inactive */;
    else
    {
        if (data->UsedCompareType != 7)
            data->UsedCompareType = 5 /* collision of multiple types */;
    }
    data->UsedCompareRightOrInactive = TRUE;
    return "";
}

const char* WINAPI ExecuteValListOfSelNames(HWND msgParent, void* param)
{
    CUserMenuValidationData* data = (CUserMenuValidationData*)param;
    data->MustHandleItemsAsGroup = TRUE;
    data->UsesListOfSelNames = TRUE;
    return "";
}

const char* WINAPI ExecuteValListOfSelFullNames(HWND msgParent, void* param)
{
    CUserMenuValidationData* data = (CUserMenuValidationData*)param;
    data->MustHandleItemsAsGroup = TRUE;
    data->UsesListOfSelFullNames = TRUE;
    return "";
}

std::wstring ExecuteValDummyW(void* param) { ExecuteValDummy(NULL, param); return std::wstring(); }
std::wstring ExecuteValOneByOneW(void* param) { ExecuteValOneByOne(NULL, param); return std::wstring(); }
std::wstring ExecuteValFullPathInactW(void* param) { ExecuteValFullPathInact(NULL, param); return std::wstring(); }
std::wstring ExecuteValFullPathLeftW(void* param) { ExecuteValFullPathLeft(NULL, param); return std::wstring(); }
std::wstring ExecuteValFullPathRightW(void* param) { ExecuteValFullPathRight(NULL, param); return std::wstring(); }
std::wstring ExecuteValCompFileLeftW(void* param) { ExecuteValCompFileLeft(NULL, param); return std::wstring(); }
std::wstring ExecuteValCompFileRightW(void* param) { ExecuteValCompFileRight(NULL, param); return std::wstring(); }
std::wstring ExecuteValCompDirLeftW(void* param) { ExecuteValCompDirLeft(NULL, param); return std::wstring(); }
std::wstring ExecuteValCompDirRightW(void* param) { ExecuteValCompDirRight(NULL, param); return std::wstring(); }
std::wstring ExecuteValCompLeftW(void* param) { ExecuteValCompLeft(NULL, param); return std::wstring(); }
std::wstring ExecuteValCompRightW(void* param) { ExecuteValCompRight(NULL, param); return std::wstring(); }
std::wstring ExecuteValCompFileActiveW(void* param) { ExecuteValCompFileActive(NULL, param); return std::wstring(); }
std::wstring ExecuteValCompFileInactW(void* param) { ExecuteValCompFileInact(NULL, param); return std::wstring(); }
std::wstring ExecuteValCompDirActiveW(void* param) { ExecuteValCompDirActive(NULL, param); return std::wstring(); }
std::wstring ExecuteValCompDirInactW(void* param) { ExecuteValCompDirInact(NULL, param); return std::wstring(); }
std::wstring ExecuteValCompActiveW(void* param) { ExecuteValCompActive(NULL, param); return std::wstring(); }
std::wstring ExecuteValCompInactW(void* param) { ExecuteValCompInact(NULL, param); return std::wstring(); }
std::wstring ExecuteValListOfSelNamesW(void* param) { ExecuteValListOfSelNames(NULL, param); return std::wstring(); }
std::wstring ExecuteValListOfSelFullNamesW(void* param) { ExecuteValListOfSelFullNames(NULL, param); return std::wstring(); }

std::wstring ExecuteExpFullPathInactW(void* param)
{
    const std::wstring* path = ((CExecuteWideExpData*)param)->UserMenuAdvancedData->FullPathInactive;
    return path != nullptr ? *path : std::wstring();
}

std::wstring ExecuteExpFullPathLeftW(void* param)
{
    return ((CExecuteWideExpData*)param)->UserMenuAdvancedData->FullPathLeft;
}

std::wstring ExecuteExpFullPathRightW(void* param)
{
    return ((CExecuteWideExpData*)param)->UserMenuAdvancedData->FullPathRight;
}

std::wstring ExecuteExpCompareName1W(void* param)
{
    return ((CExecuteWideExpData*)param)->UserMenuAdvancedData->CompareName1;
}

std::wstring ExecuteExpCompareName2W(void* param)
{
    return ((CExecuteWideExpData*)param)->UserMenuAdvancedData->CompareName2;
}

std::wstring ExecuteExpListOfSelNamesW(void* param)
{
    return ((CExecuteWideExpData*)param)->UserMenuAdvancedData->ListOfSelNames;
}

std::wstring ExecuteExpListOfSelFullNamesW(void* param)
{
    return ((CExecuteWideExpData*)param)->UserMenuAdvancedData->ListOfSelFullNames;
}

// Arrays

sally::unicode::WideVarEntry UserMenuArgsExpArrayW[] =
    {
        {EXECUTE_DRIVE, ExecuteExpDriveW},
        {EXECUTE_PATH, ExecuteExpPathW},
        {EXECUTE_NAME, ExecuteExpNameW},
        {EXECUTE_DOSPATH, ExecuteExpDOSPathW},
        {EXECUTE_DOSNAME, ExecuteExpDOSNameW},
        {EXECUTE_FULLNAME, ExecuteExpFullNameW},
        {EXECUTE_DOSFULLNAME, ExecuteExpDOSFullNameW},
        {EXECUTE_FULLPATH, ExecuteExpFullPathW},
        {EXECUTE_WINDIR, ExecuteExpWinDirW},
        {EXECUTE_SYSDIR, ExecuteExpSysDirW},
        {EXECUTE_DOSFULLPATH, ExecuteExpDOSFullPathW},
        {EXECUTE_DOSWINDIR, ExecuteExpDOSWinDirW},
        {EXECUTE_DOSSYSDIR, ExecuteExpDOSSysDirW},
        {EXECUTE_NAMEPART, ExecuteExpNamePartW},
        {EXECUTE_EXTPART, ExecuteExpExtPartW},
        {EXECUTE_DOSNAMEPART, ExecuteExpDOSNamePartW},
        {EXECUTE_DOSEXTPART, ExecuteExpDOSExtPartW},
        {EXECUTE_FULLPATHINACTIVE, ExecuteExpFullPathInactW},
        {EXECUTE_FULLPATHLEFT, ExecuteExpFullPathLeftW},
        {EXECUTE_FULLPATHRIGHT, ExecuteExpFullPathRightW},
        {EXECUTE_COMPAREDFILELEFT, ExecuteExpCompareName1W},
        {EXECUTE_COMPAREDFILERIGHT, ExecuteExpCompareName2W},
        {EXECUTE_COMPAREDDIRLEFT, ExecuteExpCompareName1W},
        {EXECUTE_COMPAREDDIRRIGHT, ExecuteExpCompareName2W},
        {EXECUTE_COMPAREDLEFT, ExecuteExpCompareName1W},
        {EXECUTE_COMPAREDRIGHT, ExecuteExpCompareName2W},
        {EXECUTE_COMPAREDFILEACT, ExecuteExpCompareName1W},
        {EXECUTE_COMPAREDFILEINACT, ExecuteExpCompareName2W},
        {EXECUTE_COMPAREDDIRACT, ExecuteExpCompareName1W},
        {EXECUTE_COMPAREDDIRINACT, ExecuteExpCompareName2W},
        {EXECUTE_COMPAREDACT, ExecuteExpCompareName1W},
        {EXECUTE_COMPAREDINACT, ExecuteExpCompareName2W},
        {EXECUTE_LISTOFSELNAMES, ExecuteExpListOfSelNamesW},
        {EXECUTE_LISTOFSELFULLNAMES, ExecuteExpListOfSelFullNamesW},
        {NULL, NULL}};

// Only detects variable use so incompatible user-menu combinations can be rejected.
sally::unicode::WideVarEntry UserMenuArgsValidationArrayW[] =
    {
        {EXECUTE_DRIVE, ExecuteValDummyW},
        {EXECUTE_PATH, ExecuteValDummyW},
        {EXECUTE_DOSPATH, ExecuteValDummyW},
        {EXECUTE_FULLPATH, ExecuteValDummyW},
        {EXECUTE_WINDIR, ExecuteValDummyW},
        {EXECUTE_SYSDIR, ExecuteValDummyW},
        {EXECUTE_DOSFULLPATH, ExecuteValDummyW},
        {EXECUTE_DOSWINDIR, ExecuteValDummyW},
        {EXECUTE_DOSSYSDIR, ExecuteValDummyW},
        {EXECUTE_NAME, ExecuteValOneByOneW},
        {EXECUTE_DOSNAME, ExecuteValOneByOneW},
        {EXECUTE_FULLNAME, ExecuteValOneByOneW},
        {EXECUTE_DOSFULLNAME, ExecuteValOneByOneW},
        {EXECUTE_NAMEPART, ExecuteValOneByOneW},
        {EXECUTE_EXTPART, ExecuteValOneByOneW},
        {EXECUTE_DOSNAMEPART, ExecuteValOneByOneW},
        {EXECUTE_DOSEXTPART, ExecuteValOneByOneW},
        {EXECUTE_FULLPATHINACTIVE, ExecuteValFullPathInactW},
        {EXECUTE_FULLPATHLEFT, ExecuteValFullPathLeftW},
        {EXECUTE_FULLPATHRIGHT, ExecuteValFullPathRightW},
        {EXECUTE_COMPAREDFILELEFT, ExecuteValCompFileLeftW},
        {EXECUTE_COMPAREDFILERIGHT, ExecuteValCompFileRightW},
        {EXECUTE_COMPAREDDIRLEFT, ExecuteValCompDirLeftW},
        {EXECUTE_COMPAREDDIRRIGHT, ExecuteValCompDirRightW},
        {EXECUTE_COMPAREDLEFT, ExecuteValCompLeftW},
        {EXECUTE_COMPAREDRIGHT, ExecuteValCompRightW},
        {EXECUTE_COMPAREDFILEACT, ExecuteValCompFileActiveW},
        {EXECUTE_COMPAREDFILEINACT, ExecuteValCompFileInactW},
        {EXECUTE_COMPAREDDIRACT, ExecuteValCompDirActiveW},
        {EXECUTE_COMPAREDDIRINACT, ExecuteValCompDirInactW},
        {EXECUTE_COMPAREDACT, ExecuteValCompActiveW},
        {EXECUTE_COMPAREDINACT, ExecuteValCompInactW},
        {EXECUTE_LISTOFSELNAMES, ExecuteValListOfSelNamesW},
        {EXECUTE_LISTOFSELFULLNAMES, ExecuteValListOfSelFullNamesW},
        {NULL, NULL}};

sally::unicode::WideVarEntry CommandExpArrayW[] =
    {
        {EXECUTE_WINDIR, ExecuteExpWinDirW},
        {EXECUTE_SYSDIR, ExecuteExpSysDirW},
        {EXECUTE_SALDIR, ExecuteExpSalDirW},
        {NULL, NULL}};

sally::unicode::WideVarEntry HotPathExpArrayW[] =
    {
        {EXECUTE_WINDIR, ExecuteExpWinDirW},
        {EXECUTE_SYSDIR, ExecuteExpSysDirW},
        {EXECUTE_SALDIR, ExecuteExpSalDirW},
        {NULL, NULL}};

sally::unicode::WideVarEntry ArgumentsExpArrayW[] =
    {
        {EXECUTE_DRIVE, ExecuteExpDriveW},
        {EXECUTE_PATH, ExecuteExpPathW},
        {EXECUTE_NAME, ExecuteExpNameW},
        {EXECUTE_DOSPATH, ExecuteExpDOSPathW},
        {EXECUTE_DOSNAME, ExecuteExpDOSNameW},
        {EXECUTE_FULLNAME, ExecuteExpFullNameW},
        {EXECUTE_DOSFULLNAME, ExecuteExpDOSFullNameW},
        {EXECUTE_FULLPATH, ExecuteExpFullPathW},
        {EXECUTE_WINDIR, ExecuteExpWinDirW},
        {EXECUTE_SYSDIR, ExecuteExpSysDirW},
        {EXECUTE_DOSFULLPATH, ExecuteExpDOSFullPathW},
        {EXECUTE_DOSWINDIR, ExecuteExpDOSWinDirW},
        {EXECUTE_DOSSYSDIR, ExecuteExpDOSSysDirW},
        {EXECUTE_NAMEPART, ExecuteExpNamePartW},
        {EXECUTE_EXTPART, ExecuteExpExtPartW},
        {EXECUTE_DOSNAMEPART, ExecuteExpDOSNamePartW},
        {EXECUTE_DOSEXTPART, ExecuteExpDOSExtPartW},
        {NULL, NULL}};

sally::unicode::WideVarEntry InitDirExpArrayW[] =
    {
        {EXECUTE_DRIVE, ExecuteExpDriveW},
        {EXECUTE_PATH, ExecuteExpPath2W},
        {EXECUTE_FULLPATH, ExecuteExpFullPath2W},
        {EXECUTE_WINDIR, ExecuteExpWinDir2W},
        {EXECUTE_SYSDIR, ExecuteExpSysDir2W},
        {EXECUTE_SALDIR, ExecuteExpSalDir2W},
        {NULL, NULL}};

// Information-line expansion has a single native-wide callback table.
sally::unicode::WideVarEntry InfoLineExpArrayW[] =
    {
        {FILEDATA_FILENAME, FileDataExpFileNameW},
        {FILEDATA_FILESIZE, FileDataExpFileSizeW},
        {FILEDATA_FILEDATE, NULL /* see below */},
        {FILEDATA_FILETIME, NULL /* see below */},
        {FILEDATA_FILEATTR, FileDataExpFileAttrW},
        {FILEDATA_FILEDOSNAME, FileDataExpFileDOSNameW},
        {NULL, NULL}};

sally::unicode::WideVarEntry* GetInfoLineExpArrayW(BOOL isDisk)
{
    if (isDisk)
    {
        InfoLineExpArrayW[2].Execute = FileDataExpFileDateOnlyForDiskW;
        InfoLineExpArrayW[3].Execute = FileDataExpFileTimeOnlyForDiskW;
    }
    else
    {
        InfoLineExpArrayW[2].Execute = FileDataExpFileDateW;
        InfoLineExpArrayW[3].Execute = FileDataExpFileTimeW;
    }
    return InfoLineExpArrayW;
}

// Make File List expansion has a single native-wide callback table.
sally::unicode::WideVarEntry MakeFileListExpArrayW[] =
    {
        {FILEDATA_FILENAME, FileDataExpFileNameW},
        {FILEDATA_FILENAMEPART, FileDataExpFileNamePartW},
        {FILEDATA_FILEEXTENSION, FileDataExpFileExtensionW},
        {FILEDATA_FILESIZE, FileDataExpFileSizeNoSpacesW},
        {FILEDATA_FILEDATE, FileDataExpFileDateW},
        {FILEDATA_FILETIME, FileDataExpFileTimeW},
        {FILEDATA_FILEATTR, FileDataExpFileAttrW},
        {FILEDATA_FILEDOSNAME, FileDataExpFileDOSNameW},
        {FILEDATA_LF, FileDataExpLFW},
        {FILEDATA_CR, FileDataExpCRW},
        {FILEDATA_CRLF, FileDataExpCRLFW},
        {FILEDATA_TAB, FileDataExpTABW},
        {EXECUTE_DRIVE, MFLFileDataExpDriveW},
        {EXECUTE_PATH, MFLFileDataExpPathW},
        {EXECUTE_DOSPATH, MFLFileDataExpDOSPathW},
        {NULL, NULL}};

BOOL BrowseDirCommand(HWND hParent, int editlineResID, int filterResID)
{
    CALL_STACK_MESSAGE2("BrowseDirCommand(, %d)", editlineResID);
    // wide: GetTargetDirectory's browse dialog best-fit-narrows the chosen path -
    // the same shape as the sibling dialog fixes. This function is reachable through exactly one
    // caller/edit control (TrackExecuteMenu's EXECUTE_BROWSEDIR item, only present in
    // HotPathItems, wired to IDC_HOTPATH_PATH with combobox=FALSE - a plain edit control), so
    // the "verify every caller's edit control" concern raised earlier turned out to be
    // a single, already-plain-edit site, not a real multi-dialog risk. GetDlgItemTextW/
    // SetDlgItemTextW are safe regardless (standard child controls are always
    // wide-registered by USER32).
    const std::wstring initDirW = GetWindowTextStringW(GetDlgItem(hParent, editlineResID));

    CALL_STACK_MESSAGE1("BrowseDirCommand::GetOpenFileName");
    std::wstring pathW;
    if (GetTargetDirectoryW(hParent, hParent, LoadStrW(IDS_BROWSEDIRECTORY_TITLE), LoadStrW(IDS_BROWSEDIRECTORY_TEXT), pathW, FALSE, initDirW.c_str()))
    {
        SetDlgItemTextW(hParent, editlineResID, pathW.c_str());
        return TRUE;
    }
    return FALSE;
}

BOOL ValidateUserMenuArguments(HWND msgParent, const wchar_t* varText, int& errorPos1, int& errorPos2,
                               CUserMenuValidationData* userMenuValidationData)
{
    CALL_STACK_MESSAGE2("ValidateUserMenuArguments(, %ls, ,)", wcslen(varText) > 300 ? L"(too long)" : varText);
    CUserMenuValidationData dummyUserMenuValidationData;
    if (userMenuValidationData == NULL)
        userMenuValidationData = &dummyUserMenuValidationData; // the caller does not care about validation data
    memset(userMenuValidationData, 0, sizeof(CUserMenuValidationData));
    if (!ValidateWideVarStringW(msgParent, varText, errorPos1, errorPos2, UserMenuArgsExpArrayW))
        return FALSE; // if this is a syntax error, report it here (including the edit position)
    errorPos1 = errorPos2 = 0;
    std::wstring ignoredExpansion;
    ExpandWideVarStringW(msgParent, varText, ignoredExpansion,
                         UserMenuArgsValidationArrayW, userMenuValidationData, TRUE);

    if (userMenuValidationData->MustHandleItemsAsGroup &&
        userMenuValidationData->MustHandleItemsOneByOne)
    { // incompatible work with selection
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_INCOMPATIBLEARGS));
        return FALSE;
    }
    if (userMenuValidationData->UsedCompareType == 5)
    { // collision of multiple Compare parameter types
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_COMPAREARGSCOLISION));
        return FALSE;
    }
    if (userMenuValidationData->UsedCompareType != 0 &&
        (!userMenuValidationData->UsedCompareLeftOrActive ||
         !userMenuValidationData->UsedCompareRightOrInactive))
    { // both Compare parameters are not used together -> nonsense
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_COMPARENEEDSBOTHARGS));
        return FALSE;
    }
    return TRUE;
}

BOOL ExpandUserMenuArguments(HWND msgParent, const wchar_t* name, const wchar_t* dosName, const wchar_t* varText,
                             std::wstring& output, BOOL* fileNameUsed,
                             CUserMenuAdvancedData* userMenuAdvancedData,
                             BOOL ignoreEnvVarNotFoundOrTooLong)
{
    CALL_STACK_MESSAGE4("ExpandUserMenuArguments(, %ls, %ls, %ls, ,)", name, dosName,
                        wcslen(varText) > 300 ? L"(too long)" : varText);
    CExecuteWideExpData data;
    data.Name = name;
    data.DosName = dosName;
    data.FileNameUsed = fileNameUsed;
    data.UserMenuAdvancedData = userMenuAdvancedData;
    return ExpandWideVarStringW(msgParent, varText, output, UserMenuArgsExpArrayW, &data,
                                ignoreEnvVarNotFoundOrTooLong);
}

BOOL ValidateCommandFile(HWND msgParent, const wchar_t* varText, int& errorPos1, int& errorPos2)
{
    CALL_STACK_MESSAGE2("ValidateCommandFile(, %ls, ,)", varText);

    if (!ValidatePathIsNotEmpty(msgParent, varText))
    {
        // edit line is empty or with only spaces -- select all
        errorPos1 = 0;
        errorPos2 = -1;
        return FALSE;
    }

    return ValidateWideVarStringW(msgParent, varText, errorPos1, errorPos2, CommandExpArrayW);
}

BOOL ValidateHotPath(HWND msgParent, const wchar_t* varText, int& errorPos1, int& errorPos2)
{
    CALL_STACK_MESSAGE2("ValidateHotPath(, %ls, ,)", varText);

    if (!ValidatePathIsNotEmpty(msgParent, varText))
    {
        // edit line is empty or with only spaces -- select all
        errorPos1 = 0;
        errorPos2 = -1;
        return FALSE;
    }

    return ValidateWideVarStringW(msgParent, varText, errorPos1, errorPos2, HotPathExpArrayW);
}

BOOL ValidateArguments(HWND msgParent, const wchar_t* varText, int& errorPos1, int& errorPos2)
{
    CALL_STACK_MESSAGE2("ValidateArguments(, %ls, ,)", varText);
    return ValidateWideVarStringW(msgParent, varText, errorPos1, errorPos2, ArgumentsExpArrayW);
}

BOOL ExpandArguments(HWND msgParent, const wchar_t* name, const wchar_t* dosName, const wchar_t* varText,
                     std::wstring& output, BOOL* fileNameUsed)
{
    CALL_STACK_MESSAGE4("ExpandArguments(, %ls, %ls, %ls, ,)", name, dosName, varText);
    CExecuteWideExpData data;
    data.Name = name;
    data.DosName = dosName;
    data.FileNameUsed = fileNameUsed;
    data.UserMenuAdvancedData = NULL;
    return ExpandWideVarStringW(msgParent, varText, output, ArgumentsExpArrayW, &data, FALSE);
}

BOOL ValidateInitDir(HWND msgParent, const wchar_t* varText, int& errorPos1, int& errorPos2)
{
    CALL_STACK_MESSAGE2("ValidateInitDir(, %ls, ,)", varText);
    if (*varText == 0)
    {
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), LoadStrW(IDS_EXP_EMPTYSTR));
        errorPos1 = errorPos2 = 0;
        return FALSE;
    }
    return ValidateWideVarStringW(msgParent, varText, errorPos1, errorPos2, InitDirExpArrayW);
}

BOOL ValidateInfoLineItems(HWND msgParent, const wchar_t* varText, int& errorPos1, int& errorPos2)
{
    CALL_STACK_MESSAGE2("ValidateInfoLineItems(, %ls, ,)", varText);
    return ValidateWideVarStringW(msgParent, varText, errorPos1, errorPos2,
                                  GetInfoLineExpArrayW(TRUE /* for validation there is no difference between TRUE and FALSE */));
}

BOOL ExpandInfoLineItemsW(HWND msgParent, const wchar_t* varText, CPluginDataInterfaceEncapsulation* pluginData,
                          CFileData* fData, BOOL isDir, std::wstring& buffer,
                          std::vector<sally::unicode::WideTextRange>& varPlacements,
                          DWORD validFileData, BOOL isDisk)
{
    CALL_STACK_MESSAGE1("ExpandInfoLineItemsW()");
    CFileDataExpData data;
    data.PluginData = pluginData;
    data.FileData = fData;
    data.IsDir = isDir;
    data.ValidFileData = validFileData;
    return sally::unicode::ExpandWideVarString(varText, GetInfoLineExpArrayW(isDisk), &data,
                                               &buffer, &varPlacements);
}

BOOL ValidateMakeFileList(HWND msgParent, const wchar_t* varText, int& errorPos1, int& errorPos2)
{
    CALL_STACK_MESSAGE2("ValidateMakeFileList(, %ls, ,)", varText);
    return ValidateWideVarStringW(msgParent, varText, errorPos1, errorPos2, MakeFileListExpArrayW);
}

// The generated listing stays wide from the panel path through
// variable expansion; there is no ANSI mirror or callback table in this route.
BOOL ExpandMakeFileListW(HWND msgParent, const wchar_t* varText, CPluginDataInterfaceEncapsulation* pluginData,
                         CFileData* fData, BOOL isDir, std::wstring* buffer, BOOL detectMaxVarSizes,
                         int* maxVarSizes, int maxVarSizesCount, DWORD validFileData, const wchar_t* pathW,
                         BOOL ignoreEnvVarNotFoundOrTooLong)
{
    CALL_STACK_MESSAGE1("ExpandMakeFileListW()");
    CFileDataExpData data;
    data.PluginData = pluginData;
    data.FileData = fData;
    data.IsDir = isDir;
    data.ValidFileData = validFileData;
    data.PathW = pathW != NULL ? pathW : L"";
    return sally::unicode::ExpandWideVarString(varText, MakeFileListExpArrayW, &data,
                                               buffer, NULL, detectMaxVarSizes,
                                               maxVarSizes, maxVarSizesCount);
}

BOOL RemoveDoubleBackslahesFromPath(wchar_t* text)
{
    if (text == NULL)
    {
        TRACE_E("Unexpected situation in RemoveDoubleBackslahesFromPath().");
        return FALSE;
    }
    int len = (int)wcslen(text);
    if (len < 3)
        return TRUE;
    wchar_t* s = text + 2; // UNC paths start with "\\"
    wchar_t* d = s;
    while (*s != 0)
    {
        if (*s == '\\' && *(s + 1) == '\\')
            s++;
        *d = *s;
        s++;
        d++;
    }
    *d = 0;
    return TRUE;
}

static void RemoveDoubleBackslashesFromPath(std::wstring& text)
{
    if (text.length() < 3)
        return;
    size_t read = 2; // preserve the leading pair of a UNC path
    size_t write = 2;
    while (read < text.length())
    {
        if (text[read] == L'\\' && read + 1 < text.length() && text[read + 1] == L'\\')
            ++read;
        text[write++] = text[read++];
    }
    text.resize(write);
}

BOOL ExpandInitDir(HWND msgParent, const wchar_t* name, const wchar_t* dosName, const wchar_t* varText,
                   std::wstring& output, BOOL ignoreEnvVarNotFoundOrTooLong)
{
    CALL_STACK_MESSAGE4("ExpandInitDir(, %ls, %ls, %ls, ,)", name, dosName, varText);
    CExecuteWideExpData data;
    data.Name = name;
    data.DosName = dosName;
    data.FileNameUsed = NULL;
    data.UserMenuAdvancedData = NULL;
    if (!ExpandWideVarStringW(msgParent, varText, output, InitDirExpArrayW, &data,
                              ignoreEnvVarNotFoundOrTooLong))
        return FALSE;
    RemoveDoubleBackslashesFromPath(output);
    return TRUE;
}

BOOL ExpandCommand(HWND msgParent, const wchar_t* varText, wchar_t* buffer, int bufferLen,
                   BOOL ignoreEnvVarNotFoundOrTooLong)
{
    CALL_STACK_MESSAGE2("ExpandCommand(, %ls, , ,)", varText);
    CExecuteWideExpData data;
    data.Name = NULL;
    data.DosName = NULL;
    data.FileNameUsed = NULL;
    data.UserMenuAdvancedData = NULL;
    if (ExpandWideVarStringW(msgParent, varText, buffer, bufferLen, CommandExpArrayW, &data,
                             ignoreEnvVarNotFoundOrTooLong))
    {
        // the EXECUTE_WINDIR, EXECUTE_SYSDIR and EXECUTE_SALDIR variables end with a backslash
        // the user adds another one, so the path contains two of them
        RemoveDoubleBackslahesFromPath(buffer); // collapse double backslashes into one
        return TRUE;
    }
    else
        return FALSE;
}

BOOL ExpandCommand(HWND msgParent, const wchar_t* varText, std::wstring& output,
                   BOOL ignoreEnvVarNotFoundOrTooLong)
{
    CALL_STACK_MESSAGE2("ExpandCommand(, %ls, ,)", varText);
    CExecuteWideExpData data;
    data.Name = NULL;
    data.DosName = NULL;
    data.FileNameUsed = NULL;
    data.UserMenuAdvancedData = NULL;
    if (!ExpandWideVarStringW(msgParent, varText, output, CommandExpArrayW, &data,
                              ignoreEnvVarNotFoundOrTooLong))
        return FALSE;
    RemoveDoubleBackslashesFromPath(output);
    return TRUE;
}

BOOL ExpandHotPath(HWND msgParent, const wchar_t* varText, wchar_t* buffer, int bufferLen,
                   BOOL ignoreEnvVarNotFoundOrTooLong)
{
    std::wstring expanded;
    if (!ExpandHotPath(msgParent, varText, expanded, ignoreEnvVarNotFoundOrTooLong) ||
        buffer == NULL || bufferLen <= 0 || expanded.size() >= static_cast<size_t>(bufferLen))
        return FALSE;
    wcscpy_s(buffer, bufferLen, expanded.c_str());
    return TRUE;
}

BOOL ExpandHotPath(HWND msgParent, const wchar_t* varText, std::wstring& output,
                   BOOL ignoreEnvVarNotFoundOrTooLong)
{
    CALL_STACK_MESSAGE2("ExpandHotPath(, %ls, , ,)", varText);
    CExecuteWideExpData data;
    data.Name = NULL;
    data.DosName = NULL;
    data.FileNameUsed = NULL;
    data.UserMenuAdvancedData = NULL;
    if (ExpandWideVarStringW(msgParent, varText, output, HotPathExpArrayW, &data,
                              ignoreEnvVarNotFoundOrTooLong))
    {
        // the EXECUTE_WINDIR, EXECUTE_SYSDIR and EXECUTE_SALDIR variables end with a backslash
        // the user adds another one, so the path contains two of them
        RemoveDoubleBackslashesFromPath(output);
        return TRUE;
    }
    else
        return FALSE;
}

const CExecuteItem*
TrackExecuteMenu(HWND hParent, int buttonResID, int editlineResID,
                 BOOL combobox, CExecuteItem* executeItems, int filterResID)
{
    CALL_STACK_MESSAGE4("TrackExecuteMenu(, %d, %d, %d)", buttonResID, editlineResID, filterResID);
    HWND hButton = GetDlgItem(hParent, buttonResID);
    if (hButton == NULL)
        TRACE_E("Child window was not found: buttonResID=" << buttonResID);
    HWND hEdit = NULL;
    CComboboxEdit* comboEdit = NULL;
    if (combobox)
    {
        hEdit = GetWindow(GetDlgItem(hParent, editlineResID), GW_CHILD);
        if (hEdit != NULL)
        {
            comboEdit = (CComboboxEdit*)WindowsManager.GetWindowPtr(hEdit);
            if (comboEdit == NULL)
            {
                TRACE_E("CComboboxEdit was not found: editlineResID=" << editlineResID);
                return NULL;
            }
        }
    }
    else
        hEdit = GetDlgItem(hParent, editlineResID);
    if (hEdit == NULL)
    {
        TRACE_E("Child window was not found: editlineResID=" << editlineResID);
        return NULL;
    }

    RECT r;
    GetWindowRect(hButton, &r);
    POINT p;
    p.x = r.right;
    p.y = r.top;

    HMENU hMenu = CreatePopupMenu();
    CExecuteItem* item = executeItems;
    int i = 0;
    while (item[i].Keyword != EXECUTE_TERMINATOR)
    {
        if (item[i].Keyword == EXECUTE_SUBMENUSTART)
        {
            int subMenuIndex = i++;
            HMENU hSubMenu = CreatePopupMenu();
            while (item[i].Keyword != EXECUTE_SUBMENUEND && item[i].Keyword != EXECUTE_TERMINATOR)
            {
                if (item[i].Keyword == EXECUTE_SEPARATOR)
                    InsertMenuW(hSubMenu, 0xFFFFFFFF, MF_BYPOSITION | MF_SEPARATOR, 1, NULL);
                else
                    InsertMenuW(hSubMenu, 0xFFFFFFFF, MF_BYPOSITION | MF_STRING, (UINT_PTR)i + 1,
                                LoadStrW(item[i].NameResID));
                i++;
            }
            InsertMenuW(hMenu, 0xFFFFFFFF, MF_BYPOSITION | MF_POPUP, (UINT_PTR)hSubMenu,
                        LoadStrW(item[subMenuIndex].NameResID));
        }
        else
        {
            if (item[i].Keyword == EXECUTE_SEPARATOR)
                InsertMenuW(hMenu, 0xFFFFFFFF, MF_BYPOSITION | MF_SEPARATOR, 1, NULL);
            else
                InsertMenuW(hMenu, 0xFFFFFFFF, MF_BYPOSITION | MF_STRING, (UINT_PTR)i + 1,
                            LoadStrW(item[i].NameResID));
        }
        i++;
    }

    TPMPARAMS tpmPar;
    tpmPar.cbSize = sizeof(tpmPar);
    tpmPar.rcExclude = r;
    DWORD cmd = TrackPopupMenuEx(hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                 p.x, p.y, hParent, &tpmPar);
    DestroyMenu(hMenu);
    item = NULL;
    if (cmd != 0)
    {
        item = &executeItems[cmd - 1];
        wchar_t buff[255];
        if (item->Keyword == EXECUTE_BROWSE)
        {
            BrowseCommand(hParent, editlineResID, filterResID);
            return item;
        }
        if (item->Keyword == EXECUTE_BROWSEDIR)
        {
            BrowseDirCommand(hParent, editlineResID, filterResID); // JRFIXME
            return item;
        }
        if (item->Keyword == EXECUTE_HELP)
            return item;

        if (item->Flags & EIF_VARIABLE)
            swprintf_s(buff, L"$(%ls)", item->Keyword);
        else
            swprintf_s(buff, L"%ls", item->Keyword);

        if (item->Flags & EIF_REPLACE_ALL)
        {
            SendMessageW(hEdit, WM_SETTEXT, 0, (LPARAM)buff);
            SendMessageW(hEdit, EM_SETSEL, lstrlenW(buff), lstrlenW(buff));
        }
        else
        {
            if (comboEdit != NULL)
                comboEdit->ReplaceText(buff);
            else
                SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)buff);
        }
        if ((item->Flags & EIF_DONT_FOCUS) == 0)
        {
            // in the case of a combo box we need to set the focus first
            DWORD start;
            DWORD end;
            if (comboEdit != NULL)
            {
                SendMessageW(hEdit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
                SetFocus(hEdit);
            }
            if (item->Flags & EIF_CURSOR_1 || item->Flags & EIF_CURSOR_2)
            {
                int delta = 1;
                if (item->Flags & EIF_CURSOR_2)
                    delta = 2;
                if (delta > lstrlenW(buff))
                {
                    TRACE_E("delta > strlen(buff)");
                    delta = (int)wcslen(buff);
                }
                if (comboEdit == NULL)
                    SendMessageW(hEdit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
                SendMessageW(hEdit, EM_SETSEL, end - delta, end - delta);
            }
            else if (comboEdit != NULL)
                SendMessageW(hEdit, EM_SETSEL, end, end);
            if (comboEdit == NULL)
                SetFocus(hEdit);
            // the default would remain with us -- give it back to the dialog
            SendMessageW(hButton, BM_SETSTYLE, BS_PUSHBUTTON, TRUE);
            HWND hDialog = hParent;
            DWORD dlgStyle;
            do
            {
                dlgStyle = (DWORD)GetWindowLongPtr(hDialog, GWL_STYLE);
                if (dlgStyle & DS_CONTROL)
                {
                    HWND hPar = GetParent(hDialog);
                    if (hPar == NULL)
                        break;
                    hDialog = hPar;
                }
            } while (dlgStyle & DS_CONTROL);
            DWORD defID = (DWORD)SendMessageW(hDialog, DM_GETDEFID, 0, 0);
            if (HIWORD(defID) == DC_HASDEFID)
                SendMessageW(GetDlgItem(hDialog, LOWORD(defID)), BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
        }
    }
    return item;
}

// Wide file picker: the edit control filled by WM_GETTEXT/WM_SETTEXT is a
// standard USER32 EDIT control (Unicode-native by class registration regardless of the
// parent dialog's own ANSI/Unicode nature - this same dialog already sends it wide text
// via SetDlgItemTextW elsewhere), so SendMessageW round-trips correctly. GetOpenFileNameW
// avoids narrowing the picked path (and the browsed-from starting path) through CP_ACP.
BOOL BrowseCommand(HWND hParent, int editlineResID, int filterResID)
{
    CALL_STACK_MESSAGE2("BrowseCommand(, %d)", editlineResID);
    HWND hEdit = GetDlgItem(hParent, editlineResID);
    std::wstring file = GetWindowTextStringW(hEdit);
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(OPENFILENAMEW));
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = hParent;
    wchar_t* s = LoadStrW(filterResID);
    ofn.lpstrFilter = s;
    while (*s != 0) // create a double-null terminated list
    {
        if (*s == L'|')
            *s = 0;
        s++;
    }
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    CALL_STACK_MESSAGE1("BrowseCommand::GetOpenFileName");
    if (SafeGetOpenFileNameOwnedW(&ofn, file))
    {
        if (SalGetFullNameW(file))
        {
            CALL_STACK_MESSAGE1("BrowseCommand::SendMessage");
            SendMessageW(hEdit, WM_SETTEXT, 0, (LPARAM)file.c_str());
            return TRUE;
        }
    }
    else
    {
        DWORD error = CommDlgExtendedError();
        if (error == FNERR_INVALIDFILENAME)
        {
            std::wstring msg = FormatStrW(LoadStrW(IDS_COMDLG_INVALIDFILENAME), file.c_str());
            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
        }
    }
    return FALSE;
}
