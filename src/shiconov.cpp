// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "geticon.h"
#include "shiconov.h"
#include "common/widepath.h"
#include "plugins\shared\sqlite\sqlite3.h"

CShellIconOverlays ShellIconOverlays;                                  // array of all available icon-overlays
TIndirectArray<CShellIconOverlayItem2> ListOfShellIconOverlays(15, 5); // list of all icon overlay handlers

//
// *****************************************************************************

BOOL GetSQLitePath(char* path, int pathSize)
{
    if (!GetModuleFileName(NULL, path, pathSize))
        return FALSE;
    char* ptr = strrchr(path, '\\');
    if (ptr == NULL)
        return FALSE;
    *ptr = 0;
    return SalPathAppend(path, "utils\\sqlite.dll", pathSize);
}

typedef int (*FT_sqlite3_open_v2)(const char* filename, sqlite3** ppDb, int flags, const char* zVfs);
typedef int (*FT_sqlite3_prepare_v2)(sqlite3* db, const char* zSql, int nByte, sqlite3_stmt** ppStmt, const char** pzTail);
typedef int (*FT_sqlite3_step)(sqlite3_stmt*);
typedef const unsigned char* (*FT_sqlite3_column_text)(sqlite3_stmt*, int iCol);
typedef int (*FT_sqlite3_column_bytes)(sqlite3_stmt*, int iCol);
typedef int (*FT_sqlite3_finalize)(sqlite3_stmt* pStmt);
typedef int (*FT_sqlite3_close)(sqlite3*);

struct CSQLite3DynLoad : public CSQLite3DynLoadBase
{
    FT_sqlite3_open_v2 open_v2;
    FT_sqlite3_prepare_v2 prepare_v2;
    FT_sqlite3_step step;
    FT_sqlite3_column_text column_text;
    FT_sqlite3_column_bytes column_bytes;
    FT_sqlite3_finalize finalize;
    FT_sqlite3_close close;

    CSQLite3DynLoad();
};

CSQLite3DynLoad::CSQLite3DynLoad()
{
    CPathBuffer sqlitePath; // Heap-allocated for long path support
    if (GetSQLitePath(sqlitePath, sqlitePath.Size()))
    {
        SQLite3DLL = HANDLES(LoadLibrary(sqlitePath));
        if (SQLite3DLL != NULL)
        {
            open_v2 = (FT_sqlite3_open_v2)GetProcAddress(SQLite3DLL, "sqlite3_open_v2");
            prepare_v2 = (FT_sqlite3_prepare_v2)GetProcAddress(SQLite3DLL, "sqlite3_prepare_v2");
            step = (FT_sqlite3_step)GetProcAddress(SQLite3DLL, "sqlite3_step");
            column_text = (FT_sqlite3_column_text)GetProcAddress(SQLite3DLL, "sqlite3_column_text");
            column_bytes = (FT_sqlite3_column_bytes)GetProcAddress(SQLite3DLL, "sqlite3_column_bytes");
            finalize = (FT_sqlite3_finalize)GetProcAddress(SQLite3DLL, "sqlite3_finalize");
            close = (FT_sqlite3_close)GetProcAddress(SQLite3DLL, "sqlite3_close");

            OK = open_v2 != NULL && prepare_v2 != NULL && step != NULL && column_text != NULL &&
                 column_bytes != NULL && finalize != NULL && close != NULL;
            if (!OK)
                TRACE_E("Cannot get sqlite.dll exports!");
        }
        else
            TRACE_E("Cannot load sqlite.dll!");
    }
    else
        TRACE_E("Cannot find path with sqlite.dll!");
}

BOOL GetGoogleDrivePath(char* gdPath, int gdPathMax, CSQLite3DynLoadBase** sqlite3_Dyn_InOut, BOOL* pathIsFromConfig)
{
    BOOL ret = FALSE;
    *pathIsFromConfig = FALSE;

    CWidePathBuffer widePath;
    CPathBuffer mbPath; // Heap-allocated for long path support
    CPathBuffer sDbPath; // Heap-allocated for long path support
    if (SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, 0 /* SHGFP_TYPE_CURRENT */, sDbPath) == S_OK)
    {
        BOOL pathOK = FALSE;
        char* sDbPathEnd = sDbPath + strlen(sDbPath);
        if (SalPathAppend(sDbPath, "Google\\Drive\\user_default\\sync_config.db", sDbPath.Size()) &&
            FileExists(sDbPath))
            pathOK = TRUE;
        if (!pathOK)
        {
            *sDbPathEnd = 0;
            if (SalPathAppend(sDbPath, "Google\\Drive\\sync_config.db", sDbPath.Size()) &&
                FileExists(sDbPath))
                pathOK = TRUE;
        }
        if (pathOK)
        {
            // load only if sqlite.dll is not already loaded
            CSQLite3DynLoad* sqlite3_Dyn = sqlite3_Dyn_InOut == NULL || *sqlite3_Dyn_InOut == NULL ? new CSQLite3DynLoad() : (CSQLite3DynLoad*)*sqlite3_Dyn_InOut;
            if (sqlite3_Dyn->OK)
            {
                sqlite3* pDb;
                sqlite3_stmt* pStmt;
                char utf8Select[] = "SELECT data_value FROM data WHERE entry_key = 'local_sync_root_path';"; // UTF8 string (if any extra character were to be inserted, it would need to be converted from ANSI->UTF8)

                // sqlite3_open_v2 requires UTF8 path, so we will convert it from ANSI to UTF8
                if (ConvertA2U(sDbPath, -1, widePath, widePath.Size()) &&
                    ConvertU2A(widePath, -1, mbPath, mbPath.Size(), FALSE, CP_UTF8))
                {
                    int iSts = sqlite3_Dyn->open_v2(mbPath, &pDb, SQLITE_OPEN_READONLY, NULL);
                    if (!iSts)
                    {
                        iSts = sqlite3_Dyn->prepare_v2(pDb, utf8Select, -1, &pStmt, NULL);
                        if (!iSts)
                        {
                            iSts = sqlite3_Dyn->step(pStmt);
                            if (iSts == SQLITE_ROW)
                            {
                                const unsigned char* utf8Path = sqlite3_Dyn->column_text(pStmt, 0);
                                int utf8PathLen = sqlite3_Dyn->column_bytes(pStmt, 0);
                                if (utf8Path != NULL && utf8PathLen > 0 &&
                                    ConvertA2U((const char*)utf8Path, utf8PathLen, widePath, widePath.Size(), CP_UTF8) &&
                                    ConvertU2A(widePath, -1, mbPath, mbPath.Size()) &&
                                    (int)strlen(mbPath) < gdPathMax)
                                {
                                    if (strnicmp(mbPath, "\\\\?\\UNC\\", 8) == 0)
                                        memmove(mbPath + 1, mbPath + 7, strlen(mbPath + 7) + 1);
                                    else
                                    {
                                        if (strncmp(mbPath, "\\\\?\\", 4) == 0)
                                            memmove(mbPath, mbPath + 4, strlen(mbPath + 4) + 1);
                                    }
                                    strcpy_s(gdPath, gdPathMax, mbPath);
                                    TRACE_I("Google Drive path: " << gdPath);
                                    ret = TRUE;
                                    *pathIsFromConfig = TRUE;
                                }
                                else
                                    TRACE_E("SQLite: cannot get value (or value too big or not convertible to ANSI string) from " << sDbPath);
                            }
                            else
                                TRACE_E("SQLite: cannot step " << sDbPath);
                            sqlite3_Dyn->finalize(pStmt);
                        }
                        else
                            TRACE_I("SQLite: cannot prepare " << sDbPath); // this is hit when GD is installed but "not signed in"
                    }
                    else
                        TRACE_E("SQLite: cannot open " << sDbPath);
                    sqlite3_Dyn->close(pDb);
                }
            }
            if (sqlite3_Dyn_InOut != NULL)
                *sqlite3_Dyn_InOut = sqlite3_Dyn; // return loaded sqlite.dll for further use (could have been loaded before calling this function)
            else
                delete sqlite3_Dyn; // release sqlite.dll, nobody is waiting for it
        }
        else
            TRACE_I("Cannot find Google Drive's configuration file: " << sDbPath);
    }
    else
        TRACE_E("Cannot get value of CSIDL_LOCAL_APPDATA!");

    if (!ret)
    {
        if (SHGetFolderPath(NULL, WindowsVistaAndLater ? CSIDL_PROFILE : CSIDL_MYDOCUMENTS,
                            NULL, 0 /* SHGFP_TYPE_CURRENT */, mbPath) == S_OK &&
            SalPathAppend(mbPath, "Google Drive", mbPath.Size()) &&
            (int)strlen(mbPath) < gdPathMax)
        {
            TRACE_I("Using default Google Drive path instead: " << mbPath);
            strcpy_s(gdPath, gdPathMax, mbPath);
            ret = TRUE;
        }
    }
    return ret;
}

//
// *****************************************************************************

/*
// returns the module (DLL) containing the specified function address
// (gets DLL module handle for specified function address)
// if we need this for currently executing code, this is also a solution (MS specific):
// EXTERN_C IMAGE_DOS_HEADER __ImageBase;
// #define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)
// see http://blogs.msdn.com/b/oldnewthing/archive/2004/10/25/247180.aspx
HMODULE GetModuleByAddress(void *address)
{
  MEMORY_BASIC_INFORMATION mbi;
  memset(&mbi, 0, sizeof(mbi));
  if (VirtualQuery(address, &mbi, sizeof(mbi)))
    return (HMODULE)(mbi.AllocationBase);
  return NULL;
}
*/

void InitShellIconOverlaysAuxAux(CLSID* clsid, const char* name)
{
    IShellIconOverlayIdentifier* iconOverlayIdentifier;
    if (CoCreateInstance(*clsid, NULL,
                         CLSCTX_INPROC_SERVER, IID_IShellIconOverlayIdentifier,
                         (LPVOID*)&iconOverlayIdentifier) == S_OK &&
        iconOverlayIdentifier != NULL) // probably unnecessary test, just to be safe
    {
        CWidePathBuffer iconFile;
        int iconIndex;
        DWORD flags;
        if (iconOverlayIdentifier->GetOverlayInfo(iconFile, iconFile.Size(), &iconIndex, &flags) == S_OK)
        {
            if (flags & ISIOI_ICONFILE)
            {
                int priority; // priority: first we will query overlay handlers with the lowest priority number
                if (iconOverlayIdentifier->GetPriority(&priority) != S_OK)
                {
                    priority = 100; // lowest priority
                    TRACE_E("InitShellIconOverlays(): GetPriority method returns error for: " << name);
                }

                if ((flags & ISIOI_ICONINDEX) == 0)
                    iconIndex = 0;

                CPathBuffer iconFileMB; // Heap-allocated for long path support
                WideCharToMultiByte(CP_ACP, 0, (wchar_t*)iconFile, -1, iconFileMB, iconFileMB.Size(), NULL, NULL);
                iconFileMB[iconFileMB.Size() - 1] = 0;

                // load icons of all sizes for this icon-overlay
                HICON hIcons[2] = {0};
                ExtractIcons(iconFileMB, iconIndex,
                             MAKELONG(IconSizes[ICONSIZE_32], IconSizes[ICONSIZE_16]),
                             MAKELONG(IconSizes[ICONSIZE_32], IconSizes[ICONSIZE_16]),
                             hIcons, NULL, 2, IconLRFlags);

                HICON iconOverlay[ICONSIZE_COUNT] = {0};
                iconOverlay[ICONSIZE_32] = hIcons[0];
                iconOverlay[ICONSIZE_16] = hIcons[1];

                ExtractIcons(iconFileMB, iconIndex,
                             IconSizes[ICONSIZE_48],
                             IconSizes[ICONSIZE_48],
                             hIcons, NULL, 1, IconLRFlags);
                iconOverlay[ICONSIZE_48] = hIcons[0];

                int x;
                for (x = 0; x < ICONSIZE_COUNT; x++)
                    if (iconOverlay[x] != NULL)
                        HANDLES_ADD(__htIcon, __hoLoadImage, iconOverlay[x]);

                // insert handler into ShellIconOverlays
                if (iconOverlay[ICONSIZE_16] != NULL && iconOverlay[ICONSIZE_32] != NULL && iconOverlay[ICONSIZE_48] != NULL)
                {
                    BOOL isGoogleDrive = FALSE;
                    const char* nameSkipWS = name;
                    while (*nameSkipWS != 0 && *nameSkipWS == ' ')
                        nameSkipWS++;
                    if (stricmp(name, "GDriveBlacklistedOverlay") == 0 ||
                        stricmp(name, "GDriveSharedEditOverlay") == 0 ||
                        stricmp(name, "GDriveSharedOverlay") == 0 ||
                        stricmp(name, "GDriveSharedViewOverlay") == 0 ||
                        stricmp(name, "GDriveSyncedOverlay") == 0 ||
                        stricmp(name, "GDriveSyncingOverlay") == 0 ||
                        stricmp(nameSkipWS, "GoogleDriveBlacklisted") == 0 ||
                        stricmp(nameSkipWS, "GoogleDriveSynced") == 0 ||
                        stricmp(nameSkipWS, "GoogleDriveSyncing") == 0)
                    {
                        isGoogleDrive = TRUE;
                        // Google Drive handlers are called only for subdirectories of the path where Google Drive resides
                        ShellIconOverlays.InitGoogleDrivePath(NULL, FALSE /* icon overlays not yet loaded */);
                    }
#ifdef _DEBUG
                    if (!isGoogleDrive && (StrIStr(name, "GDrive") != NULL || StrIStr(name, "GoogleDrive") != NULL))
                        TRACE_E("It seems Google Drive again changed names of Icon Overlays in registry. New name: " << name);
#endif // _DEBUG

                    CShellIconOverlayItem* item = new CShellIconOverlayItem;
                    if (item != NULL)
                    {
                        if (ShellIconOverlays.Add(item /*, priority*/))
                        {
                            item->Priority = priority;
                            item->Identifier = iconOverlayIdentifier;
                            item->IconOverlayIdCLSID = *clsid;
                            lstrcpyn(item->IconOverlayName, name, item->IconOverlayName.Size());
                            item->GoogleDriveOverlay = isGoogleDrive;
                            iconOverlayIdentifier = NULL;
                            for (x = 0; x < ICONSIZE_COUNT; x++)
                            {
                                item->IconOverlay[x] = iconOverlay[x];
                                iconOverlay[x] = NULL;
                            }
                        }
                        else
                            delete item;
                    }
                }
                else
                    TRACE_E("InitShellIconOverlays(): unable to get icons of all sizes for: " << name);

                for (x = 0; x < ICONSIZE_COUNT; x++)
                    if (iconOverlay[x] != NULL)
                        HANDLES(DestroyIcon(iconOverlay[x]));
            }
            else
                TRACE_I("InitShellIconOverlays(): unable to get icon overlay location for: " << name);
        }
        else
            TRACE_I("InitShellIconOverlays(): GetOverlayInfo method returns error for: " << name); // Tortoise does this when more than 12 handlers are registered
        if (iconOverlayIdentifier != NULL)
            iconOverlayIdentifier->Release();
    }
    else
        TRACE_I("InitShellIconOverlays(): unable to create object for: " << name); // e.g., "Offline Files" reports this on clean XP
}

void InitShellIconOverlaysAux(CLSID* clsid, const char* name)
{
    __try
    {
        InitShellIconOverlaysAuxAux(clsid, name);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), -1, name))
    {
        TRACE_I("InitShellIconOverlaysAux: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // harder exit (this one still calls something)
    }
}

void InitShellIconOverlays()
{
    CALL_STACK_MESSAGE1("InitShellIconOverlays()");

    HKEY clsIDKey;
    LONG errRet;
    if ((errRet = HANDLES_Q(RegOpenKeyEx(HKEY_CLASSES_ROOT, "CLSID",
                                         0, KEY_QUERY_VALUE, &clsIDKey))) != ERROR_SUCCESS)
    {
        TRACE_I("InitShellIconOverlays(): error opening HKEY_CLASSES_ROOT\\CLSID key: " << GetErrorText(errRet));
        clsIDKey = NULL;
    }

    HKEY key;
    if ((errRet = HANDLES_Q(RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                                         "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellIconOverlayIdentifiers",
                                         0, KEY_ENUMERATE_SUB_KEYS, &key))) == ERROR_SUCCESS)
    {
        TIndirectArray<char> keyNames(15, 5);
        CPathBuffer name; // Heap-allocated for long path support
        DWORD i = 0;
        while (1)
        { // enumerate all icon-overlay-handlers sequentially
            FILETIME dummy;
            DWORD nameLen = name.Size() - 1; // RegEnumKeyEx expects size excluding null terminator
            if ((errRet = RegEnumKeyEx(key, i, name, &nameLen, NULL, NULL, NULL, &dummy)) == ERROR_SUCCESS)
            {
                int s = 0; // insert new name, there are about 15 of them, so we don't need any quick-sort
                for (; s < keyNames.Count && stricmp(name, keyNames[s]) >= 0; s++)
                    ;
                keyNames.Insert(s, DupStr(name));
            }
            else
            {
                if (errRet != ERROR_NO_MORE_ITEMS)
                    TRACE_E("InitShellIconOverlays(): error enumerating ShellIconOverlayIdentifiers key: " << GetErrorText(errRet));
                break;
            }
            i++;
        }
        // go through sorted list of icon-overlay-handlers (Explorer defines handler priority alphabetically)
        // we take only the first 15, Explorer even only the first 11
        for (int s = 0; s < keyNames.Count; s++)
        { // open icon-overlay-handler key
            HKEY handler;
            if ((errRet = HANDLES_Q(RegOpenKeyEx(key, keyNames[s], 0, KEY_QUERY_VALUE, &handler))) == ERROR_SUCCESS)
            {
                char txtClsId[100];
                DWORD size = 100;
                DWORD type;
                if ((errRet = SalRegQueryValueEx(handler, NULL, NULL, &type, (BYTE*)txtClsId, &size)) == ERROR_SUCCESS)
                {
                    if (type == REG_SZ)
                    {
                        txtClsId[99] = 0; // just to be safe
                        OLECHAR oleTxtClsId[100];
                        MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, txtClsId, -1, oleTxtClsId, 100);
                        oleTxtClsId[99] = 0; // just to be safe

                        CLSID clsid;
                        if (CLSIDFromString(oleTxtClsId, &clsid) == NOERROR)
                        {
                            char descr[1000];
                            descr[0] = 0;
                            if (clsIDKey != NULL)
                            {
                                HKEY classKey;
                                if ((errRet = HANDLES_Q(RegOpenKeyEx(clsIDKey, txtClsId, 0, KEY_QUERY_VALUE, &classKey))) == ERROR_SUCCESS)
                                {
                                    DWORD descrSize = 1000;
                                    DWORD descrType;
                                    if ((errRet = SalRegQueryValueEx(classKey, NULL, NULL, &descrType, (BYTE*)descr, &descrSize)) == ERROR_SUCCESS)
                                    {
                                        if (descrType != REG_SZ)
                                        {
                                            TRACE_E("InitShellIconOverlays(): default value from CLSID\\" << txtClsId << " key in not REG_SZ!");
                                            descr[0] = 0;
                                        }
                                    }
                                    else
                                    {
                                        if (errRet != ERROR_FILE_NOT_FOUND) // reports this error when handler has no description (which is apparently not an error, because on Vista it applies to e.g., Offline Files)
                                        {
                                            TRACE_E("InitShellIconOverlays(): error reading default value from CLSID\\" << txtClsId << " key: " << GetErrorText(errRet));
                                        }
                                        descr[0] = 0;
                                    }
                                    HANDLES(RegCloseKey(classKey));
                                }
                                else
                                {
                                    // Petr: after Google Drive update on 30.8.2015, the key for GDriveSharedOverlay was missing
                                    //       under CLSID in registry, I found no difference in overlay display compared to Explorer,
                                    //       so I bypassed this annoying message by removing the
                                    //       GDriveSharedOverlay key from ShellIconOverlayIdentifiers list
                                    TRACE_E("InitShellIconOverlays(): error opening CLSID\\" << txtClsId << " key: " << GetErrorText(errRet));
                                }
                            }

                            CShellIconOverlayItem2* item2 = new CShellIconOverlayItem2;
                            if (item2 != NULL)
                            {
                                ListOfShellIconOverlays.Add(item2);
                                if (ListOfShellIconOverlays.IsGood())
                                {
                                    lstrcpyn(item2->IconOverlayName, keyNames[s], item2->IconOverlayName.Size());
                                    lstrcpyn(item2->IconOverlayDescr, descr, item2->IconOverlayDescr.Size());
                                }
                                else
                                {
                                    ListOfShellIconOverlays.ResetState();
                                    delete item2;
                                }
                            }

                            if (!IsDisabledCustomIconOverlays(keyNames[s]))
                                InitShellIconOverlaysAux(&clsid, keyNames[s]);
                        }
                        else
                            TRACE_E("InitShellIconOverlays(): invalid CLSID: " << txtClsId);
                    }
                    else
                        TRACE_E("InitShellIconOverlays(): default value from ShellIconOverlayIdentifiers\\" << keyNames[s] << " key in not REG_SZ!");
                }
                else
                    TRACE_E("InitShellIconOverlays(): error reading default value from ShellIconOverlayIdentifiers\\" << keyNames[s] << " key: " << GetErrorText(errRet));

                HANDLES(RegCloseKey(handler));
            }
            else
                TRACE_E("InitShellIconOverlays(): error opening ShellIconOverlayIdentifiers\\" << keyNames[s] << " key: " << GetErrorText(errRet));
        }
        HANDLES(RegCloseKey(key));
    }
    else
        TRACE_I("InitShellIconOverlays(): error opening ShellIconOverlayIdentifiers key: " << GetErrorText(errRet));

    if (clsIDKey != NULL)
        HANDLES(RegCloseKey(clsIDKey));
}

void ReleaseShellIconOverlays()
{
    CALL_STACK_MESSAGE1("ReleaseShellIconOverlays()");

    ShellIconOverlays.Release();
    ListOfShellIconOverlays.DestroyMembers();
}

//
// *****************************************************************************

BOOL IsNameInListOfDisabledCustomIconOverlays(const char* name)
{
    if (Configuration.DisabledCustomIconOverlays != NULL)
    {
        static char buf[SAL_MAX_LONG_PATH]; // called also from Bug Report - we don't want to burden the stack
        const char* s = Configuration.DisabledCustomIconOverlays;
        char* d = buf;
        char* end = buf + SAL_MAX_LONG_PATH;
        while (*s != 0)
        {
            if (*s == ';')
            {
                if (*(s + 1) == ';')
                {
                    *d++ = ';';
                    s += 2;
                }
                else
                {
                    *d = 0;
                    if (stricmp(name, buf) == 0)
                        return TRUE; // is disabled

                    // go to next name
                    s++;
                    d = buf;
                }
            }
            else
                *d++ = *s++;
            if (d >= end)
            {
                d = end - 1; // for longer names (should not happen), we will overwrite the last character (null-terminator)
                TRACE_E("IsNameInListOfDisabledCustomIconOverlays(): unexpected situation: too long name in list of disabled icon overlay handlers!");
            }
        }
        *d = 0;
        if (stricmp(name, buf) == 0)
            return TRUE; // is disabled
    }
    return FALSE; // is not in list
}

BOOL IsDisabledCustomIconOverlays(const char* name)
{
    if (!Configuration.EnableCustomIconOverlays ||
        IsNameInListOfDisabledCustomIconOverlays(name))
    {
        return TRUE; // disabled
    }
    return FALSE; // enabled
}

void ClearListOfDisabledCustomIconOverlays()
{
    if (Configuration.DisabledCustomIconOverlays != NULL)
    {
        free(Configuration.DisabledCustomIconOverlays);
        Configuration.DisabledCustomIconOverlays = NULL;
    }
}

BOOL AddToListOfDisabledCustomIconOverlays(const char* name)
{
    if (*name == 0)
    {
        TRACE_E("AddToListOfDisabledCustomIconOverlays(): empty name is unexpected here!");
        return TRUE; // nothing to do
    }
    static char n[2 * MAX_PATH]; // called also from Bug Report - we don't want to burden the stack
    char* d = n;
    const char* s = name;
    while (*s != 0)
    {
        if (*s == ';')
        {
            *d++ = ';';
            *d++ = ';';
            s++;
        }
        else
            *d++ = *s++;
    }
    *d = 0;
    char* m = Configuration.DisabledCustomIconOverlays;
    int mLen = (m != NULL ? (int)strlen(m) : 0);
    m = (char*)realloc(m, mLen + 1 + strlen(n) + 1);
    if (m != NULL)
    {
        if (mLen > 0)
            strcpy(m + mLen++, ";");
        strcpy(m + mLen, n);
        Configuration.DisabledCustomIconOverlays = m;
        return TRUE;
    }
    else
    {
        Configuration.EnableCustomIconOverlays = FALSE;
        TRACE_E("AddToListOfDisabledCustomIconOverlays(): low memory: disabling custom icon overlay handlers!");
        return FALSE;
    }
}

//
// *****************************************************************************
// CShellIconOverlayItem
//

CShellIconOverlayItem::CShellIconOverlayItem()
{
    Identifier = NULL;
    memset(&IconOverlayIdCLSID, 0, sizeof(IconOverlayIdCLSID));
    Priority = 0;
    int i;
    for (i = 0; i < ICONSIZE_COUNT; i++)
        IconOverlay[i] = NULL;
    GoogleDriveOverlay = FALSE;
}

void CShellIconOverlayItem::Cleanup()
{
    if (Identifier != NULL)
    {
        __try
        {
            Identifier->Release();
        }
        __except (CCallStack::HandleException(GetExceptionInformation(), -1, IconOverlayName))
        {
            TRACE_I("CShellIconOverlayItem::~CShellIconOverlayItem(): calling ExitProcess(1).");
            //      ExitProcess(1);
            TerminateProcess(GetCurrentProcess(), 1); // harder exit (this one still calls something)
        }
    }
    int i;
    for (i = 0; i < ICONSIZE_COUNT; i++)
        if (IconOverlay[i] != NULL)
            HANDLES(DestroyIcon(IconOverlay[i]));
}

CShellIconOverlayItem::~CShellIconOverlayItem()
{
    // VC2015 did not like __try / __except block in the destructor, the linker complained in the x64 version:
    // error LNK2001: unresolved external symbol __C_specific_handler_noexcept
    // moving the code into a separate function resolved the problem
    Cleanup();
}

//
// *****************************************************************************
// CShellIconOverlays
//

BOOL CShellIconOverlays::Add(CShellIconOverlayItem* item /*, int priority*/)
{
    CALL_STACK_MESSAGE1("CShellIconOverlays::Add()");

    if (Overlays.Count == 15)
    {
        TRACE_I("CShellIconOverlays::Add(): unexpected situation: more than 15 icon-overlay-handlers!");
        return FALSE;
    }
    // sorting by priority is nonsense, MS says it uses it only if other prioritization methods
    // fail, in reality it goes alphabetically in the overlay handlers list; priority is probably used only for
    // this: overlays for link, share and slow files (offline) have priority 10, so for such files
    // we only take overlays with higher priority (lower number than 10)
    /*
  int i;
  for (i = 0; i < Overlays.Count; i++)
    if (Overlays[i]->Priority > priority) break;
  Overlays.Insert(i, item);
*/
    Overlays.Add(item);
    BOOL ok = Overlays.IsGood();
    if (!ok)
        Overlays.ResetState();
    return ok;
}

void CreateIconReadersIconOverlayIdsAuxAux(CLSID* clsid, const char* name, IShellIconOverlayIdentifier** ids, int i)
{
    IShellIconOverlayIdentifier* iconOverlayIdentifier;
    if (CoCreateInstance(*clsid, NULL, CLSCTX_INPROC_SERVER, IID_IShellIconOverlayIdentifier,
                         (LPVOID*)&iconOverlayIdentifier) == S_OK &&
        iconOverlayIdentifier != NULL) // probably unnecessary test, just to be safe
    {
        // just for form's sake, call the usual methods (as if we were Explorer and wanted to show those overlays)
        CWidePathBuffer iconFile;
        int iconIndex;
        DWORD flags;
        iconOverlayIdentifier->GetOverlayInfo(iconFile, iconFile.Size(), &iconIndex, &flags);
        int priority;
        iconOverlayIdentifier->GetPriority(&priority);

        ids[i] = iconOverlayIdentifier;
    }
    else
        TRACE_I("CreateIconReadersIconOverlayIdsAuxAux(): unable to create object for icon-overlay handler: " << name << "!");
}

void CreateIconReadersIconOverlayIdsAux(CLSID* clsid, const char* name, IShellIconOverlayIdentifier** ids, int i)
{
    __try
    {
        CreateIconReadersIconOverlayIdsAuxAux(clsid, name, ids, i);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), -1, name))
    {
        TRACE_I("CreateIconReadersIconOverlayIdsAux: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // harder exit (this one still calls something)
    }
}

IShellIconOverlayIdentifier**
CShellIconOverlays::CreateIconReadersIconOverlayIds()
{
    CALL_STACK_MESSAGE1("CShellIconOverlays::CreateIconReadersIconOverlayIds()");

    IShellIconOverlayIdentifier** ids = NULL;
    if (Overlays.Count > 0)
    {
        ids = (IShellIconOverlayIdentifier**)malloc(Overlays.Count * sizeof(IShellIconOverlayIdentifier*));
        if (ids != NULL)
        {
            memset(ids, 0, Overlays.Count * sizeof(IShellIconOverlayIdentifier*));
            int i;
            for (i = 0; i < Overlays.Count; i++)
            {
                CreateIconReadersIconOverlayIdsAux(&Overlays[i]->IconOverlayIdCLSID,
                                                   Overlays[i]->IconOverlayName, ids, i);
            }
        }
        else
            TRACE_E(LOW_MEMORY);
    }
    else
        TRACE_I("CShellIconOverlays::CreateIconReadersIconOverlayIds(): there is no icon-overlay handler!");
    return ids;
}

void CShellIconOverlays::ReleaseIconReadersIconOverlayIds(IShellIconOverlayIdentifier** iconReadersIconOverlayIds)
{
    if (iconReadersIconOverlayIds != NULL)
    {
        int i;
        for (i = 0; i < Overlays.Count; i++)
        {
            if (iconReadersIconOverlayIds[i] != NULL)
            {
                __try
                {
                    iconReadersIconOverlayIds[i]->Release();
                    iconReadersIconOverlayIds[i] = NULL;
                }
                __except (CCallStack::HandleException(GetExceptionInformation(), -1, Overlays[i]->IconOverlayName))
                {
                    TRACE_I("CShellIconOverlays::ReleaseIconReadersIconOverlayIds: calling ExitProcess(1).");
                    //          ExitProcess(1);
                    TerminateProcess(GetCurrentProcess(), 1); // harder exit (this one still calls something)
                }
            }
        }
        free(iconReadersIconOverlayIds);
    }
}

BOOL GetIconOverlayIndexAuxAux(IShellIconOverlayIdentifier** iconReadersIconOverlayIds,
                               int i, WCHAR* wPath, const char* name, DWORD shAttrs)
{
    HRESULT res;
    if (iconReadersIconOverlayIds[i] != NULL &&
        (res = iconReadersIconOverlayIds[i]->IsMemberOf(wPath, shAttrs)) == S_OK)
    {
        return TRUE; // found
    }
    else
    {
        if (res != S_FALSE && res != 0x80070002) // 0x80070002 is "file not found", returned by "Offline Files" for everything that is not offline-available
            TRACE_I("CShellIconOverlays::GetIconOverlayIndex(): overlay " << name << ": IsMemberOf() returns error: 0x" << std::hex << res << std::dec);
    }
    return FALSE;
}

BOOL GetIconOverlayIndexAux(IShellIconOverlayIdentifier** iconReadersIconOverlayIds,
                            int i, WCHAR* wPath, const char* name, DWORD shAttrs)
{
    __try
    {
        return GetIconOverlayIndexAuxAux(iconReadersIconOverlayIds, i, wPath, name, shAttrs);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), -1, name))
    {
        TRACE_I("GetIconOverlayIndexAux: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // harder exit (this one still calls something)
    }
    return FALSE; // just for the compiler
}

DWORD_PTR SHGetFileInfoAux(LPCTSTR pszPath, DWORD dwFileAttributes, SHFILEINFO* psfi,
                           UINT cbFileInfo, UINT uFlags)
{
    __try
    {
        return SHGetFileInfo(pszPath, dwFileAttributes, psfi, cbFileInfo, uFlags);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 23))
    {
        FGIExceptionHasOccured++;
        return 0;
    }
}

DWORD
CShellIconOverlays::GetIconOverlayIndex(WCHAR* wPath, WCHAR* wName, char* aPath, char* aName,
                                        char* name, DWORD fileAttrs, int minPriority,
                                        IShellIconOverlayIdentifier** iconReadersIconOverlayIds,
                                        BOOL isGoogleDrivePath)
{
    CALL_STACK_MESSAGE_NONE // call-stack would only slow things down here

        if ((wName - wPath) + strlen(name) >= MAX_PATH)
    {
        TRACE_I("CShellIconOverlays::GetIconOverlayIndex(): too long file name: " << name);
        return ICONOVERLAYINDEX_NOTUSED;
    }
    MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, name, -1, wName, MAX_PATH - (int)(wName - wPath));
    wPath[MAX_PATH - 1] = 0; // just to be safe
    strcpy(aName, name);

    //  SHFILEINFO fi;
    //  if (SHGetFileInfoAux(aPath, 0, &fi, sizeof(fi), SHGFI_ATTRIBUTES))
    //  {
    // GoogleDrive crashes with concurrent calls to IsMemberOf() from both icon-readers, one thread allocates,
    // the other deallocates and heap corruption occurs (hard to say why, their bug), the critical section
    // slows it down quite a bit (2x) when reading in both panels simultaneously, so we try to use it only when GD
    // is active (in its directory)
    BOOL isGD_CS_entered = FALSE;
    for (int i = 0; i < Overlays.Count; i++)
    {
        CShellIconOverlayItem* overlay = Overlays[i];
        if (overlay->Priority > minPriority)
            continue; // usage: overlays for link, share and slow files (offline) have priority 10, so we only take overlays with higher priority (lower number than 10)
                      //      if (GetIconOverlayIndexAux(iconReadersIconOverlayIds, i, wPath, Overlays[i]->IconOverlayName, fi.dwAttributes))
        if (overlay->GoogleDriveOverlay)
        {
            if (!isGoogleDrivePath)
                continue; // Google Drive handlers are called only for their directory and its subdirectories (they are slow and crash without added synchronization)
            if (!isGD_CS_entered)
            {
                HANDLES(EnterCriticalSection(&GD_CS));
                isGD_CS_entered = TRUE;
            }
        }
        if (GetIconOverlayIndexAux(iconReadersIconOverlayIds, i, wPath, overlay->IconOverlayName, fileAttrs))
        {
            if (isGD_CS_entered)
                HANDLES(LeaveCriticalSection(&GD_CS));
            return i; // found
        }
    }
    if (isGD_CS_entered)
        HANDLES(LeaveCriticalSection(&GD_CS));
    //  }
    //  else TRACE_I("CShellIconOverlays::GetIconOverlayIndex(): unable to get shell-attributes of: " << wPath);
    return ICONOVERLAYINDEX_NOTUSED; // not found
}

void ColorsChangedAuxAux(CShellIconOverlayItem* item)
{
    CWidePathBuffer iconFile;
    int iconIndex;
    DWORD flags;
    if (item->Identifier->GetOverlayInfo(iconFile, iconFile.Size(), &iconIndex, &flags) == S_OK)
    {
        if (flags & ISIOI_ICONFILE)
        {
            if ((flags & ISIOI_ICONINDEX) == 0)
                iconIndex = 0;

            CPathBuffer iconFileMB; // Heap-allocated for long path support
            WideCharToMultiByte(CP_ACP, 0, (wchar_t*)iconFile, -1, iconFileMB, iconFileMB.Size(), NULL, NULL);
            iconFileMB[iconFileMB.Size() - 1] = 0;

            // load icons of all sizes for this icon-overlay
            HICON hIcons[2] = {0};
            ExtractIcons(iconFileMB, iconIndex,
                         MAKELONG(IconSizes[ICONSIZE_32], IconSizes[ICONSIZE_16]),
                         MAKELONG(IconSizes[ICONSIZE_32], IconSizes[ICONSIZE_16]),
                         hIcons, NULL, 2, IconLRFlags);

            HICON iconOverlay[ICONSIZE_COUNT] = {0};
            iconOverlay[ICONSIZE_32] = hIcons[0];
            iconOverlay[ICONSIZE_16] = hIcons[1];

            ExtractIcons(iconFileMB, iconIndex,
                         IconSizes[ICONSIZE_48],
                         IconSizes[ICONSIZE_48],
                         hIcons, NULL, 1, IconLRFlags);
            iconOverlay[ICONSIZE_48] = hIcons[0];

            int x;
            for (x = 0; x < ICONSIZE_COUNT; x++)
                if (iconOverlay[x] != NULL)
                    HANDLES_ADD(__htIcon, __hoLoadImage, iconOverlay[x]);

            // insert new icons into 'item'
            if (iconOverlay[ICONSIZE_16] != NULL && iconOverlay[ICONSIZE_32] != NULL && iconOverlay[ICONSIZE_48] != NULL)
            {
                for (x = 0; x < ICONSIZE_COUNT; x++)
                {
                    HANDLES(DestroyIcon(item->IconOverlay[x]));
                    item->IconOverlay[x] = iconOverlay[x];
                }
            }
            else
            {
                TRACE_E("CShellIconOverlays::ColorsChanged(): unable to get icons of all sizes!");
                for (x = 0; x < ICONSIZE_COUNT; x++)
                    if (iconOverlay[x] != NULL)
                        HANDLES(DestroyIcon(iconOverlay[x]));
            }
        }
        else
            TRACE_E("CShellIconOverlays::ColorsChanged(): unable to get icon overlay location!");
    }
    else
        TRACE_E("CShellIconOverlays::ColorsChanged(): GetOverlayInfo method returns error!");
}

void ColorsChangedAux(CShellIconOverlayItem* item)
{
    __try
    {
        ColorsChangedAuxAux(item);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), -1, item->IconOverlayName))
    {
        TRACE_I("ColorsChangedAux: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // harder exit (this one still calls something)
    }
}

void CShellIconOverlays::ColorsChanged()
{
    CALL_STACK_MESSAGE1("CShellIconOverlays::ColorsChanged()");

    for (int j = 0; j < Overlays.Count; j++)
        ColorsChangedAux(Overlays[j]);
}

void CShellIconOverlays::InitGoogleDrivePath(CSQLite3DynLoadBase** sqlite3_Dyn_InOut, BOOL debugTestOverlays)
{
    CALL_STACK_MESSAGE1("CShellIconOverlays::InitGoogleDrivePath(,)");

    if (!GetGDAlreadyCalled)
    {
        CPathBuffer gdPath; // Heap-allocated for long path support
        BOOL pathIsFromConfig;
        if (GetGoogleDrivePath(gdPath, gdPath.Size(), sqlite3_Dyn_InOut, &pathIsFromConfig))
            SetGoogleDrivePath(gdPath, pathIsFromConfig);
        GetGDAlreadyCalled = TRUE;
    }

#ifdef _DEBUG
    static BOOL firstCall = TRUE; // one test is enough
    if (firstCall && debugTestOverlays && HasGoogleDrivePath())
    { // test only if we will advertise Google Drive on toolbar and in change drive menu
        firstCall = FALSE;
        BOOL found = FALSE;
        for (int j = 0; j < Overlays.Count; j++)
        {
            if (Overlays[j]->GoogleDriveOverlay)
            {
                found = TRUE;
                break;
            }
        }
        if (!found)
        {
            // Google Drive installs in a version matching Windows (x86 / x64). So Salamander x86
            // on x64 Windows (and vice versa) won't find GD icon-handlers and that's not an error.
#ifdef _WIN64
            if (Windows64Bit)
#else  // _WIN64
            if (!Windows64Bit)
#endif // _WIN64
            {
                TRACE_E("Google Drive found but its icon overlay handlers were not found (not identified as GD)!");
            }
        }
    }
#endif // _DEBUG
}

BOOL CShellIconOverlays::HasGoogleDrivePath()
{
    CALL_STACK_MESSAGE_NONE;
    if (GoogleDrivePathIsFromCfg && GoogleDrivePath[0] != 0)
    {
        if (!GoogleDrivePathExists && DirExists(GoogleDrivePath))
            GoogleDrivePathExists = TRUE;
        return GoogleDrivePathExists;
    }
    return FALSE;
}
