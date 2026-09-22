// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "geticon.h"
#include "shiconov.h"
#include "common/widepath.h"
#include "common/unicode/helpers.h" // WideToAnsi for the narrow-only handler-name log/heuristic bridge
#include "common/IRegistry.h"
#include "common/IFileSystem.h"
#include "common/IPathService.h"
#include "common/IShell.h"
#include "common/SalPathWide.h"
#include "common/Win32TextCodec.h"
#include "plugins\shared\sqlite\sqlite3.h"
#include "shiconov_limits.h"
#include "shiconov_diag.h"
#include "shiconov_icons.h"

// The overlay cap and the CFileData::IconOverlayIndex "no overlay" sentinel must stay in
// lockstep: the maximum valid index is (cap - 1), which must never equal ICONOVERLAYINDEX_NOTUSED.
static_assert(MAX_SHELL_ICON_OVERLAYS == ICONOVERLAYINDEX_NOTUSED,
              "overlay cap must equal the CFileData::IconOverlayIndex sentinel (spl_com.h)");

CShellIconOverlays ShellIconOverlays;                                  // array of all available icon-overlays
TIndirectArray<CShellIconOverlayItem2> ListOfShellIconOverlays(15, 5); // list of all icon overlay handlers

//
// *****************************************************************************

BOOL GetSQLitePath(std::wstring& path)
{
    if (gPathService == NULL || !gPathService->GetModuleFileName(NULL, path).success)
        return FALSE;
    const size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos)
        return FALSE;
    path.resize(slash + 1);
    path += L"utils\\sqlite.dll";
    return TRUE;
}

// IShellIconOverlayIdentifier exposes only a caller-owned output buffer. Keep that
// capacity negotiation at this COM boundary and return an exact UTF-16 owner.
HRESULT GetOverlayInfoDynamic(IShellIconOverlayIdentifier* identifier, std::wstring& iconFile,
                              int* iconIndex, DWORD* flags)
{
    iconFile.clear();
    size_t capacity = 512;
    for (;;)
    {
        std::vector<wchar_t> buffer(capacity);
        const HRESULT hr = identifier->GetOverlayInfo(buffer.data(), static_cast<int>(capacity), iconIndex, flags);
        size_t length = 0;
        while (length < capacity && buffer[length] != 0)
            ++length;
        if (hr == S_OK && length < capacity)
        {
            iconFile.assign(buffer.data(), length);
            return hr;
        }
        if (capacity > static_cast<size_t>(INT_MAX) / 2)
            return hr == S_OK ? HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) : hr;
        if (hr != S_OK && hr != HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) &&
            hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA))
            return hr;
        capacity *= 2;
    }
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
    std::wstring sqlitePath;
    if (GetSQLitePath(sqlitePath))
    {
        SQLite3DLL = HANDLES(LoadLibraryW(sqlitePath.c_str()));
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

BOOL GetGoogleDrivePath(std::wstring& gdPath, CSQLite3DynLoadBase** sqlite3_Dyn_InOut, BOOL* pathIsFromConfig)
{
    BOOL ret = FALSE;
    *pathIsFromConfig = FALSE;

    // 'mbPath' used to be one buffer doing TWO width jobs - the UTF-8 byte
    // string for sqlite3 down in the query branch, and a genuine wide path in the fallback
    // branch at the bottom. Split, because no single width can be right for both.
    std::wstring widePath;
    std::wstring fallbackPath;
    std::wstring sDbPath;
    IShell* shell = gShell != NULL ? gShell : GetWin32Shell();
    if (shell != NULL && shell->GetKnownFolderPath(FOLDERID_LocalAppData, sDbPath).success)
    {
        BOOL pathOK = FALSE;
        const std::wstring localAppData = sDbPath;
        SalPathAppendW(sDbPath, L"Google\\Drive\\user_default\\sync_config.db");
        if (gFileSystem->FileExists(sDbPath.c_str()))
            pathOK = TRUE;
        if (!pathOK)
        {
            sDbPath = localAppData;
            SalPathAppendW(sDbPath, L"Google\\Drive\\sync_config.db");
            if (gFileSystem->FileExists(sDbPath.c_str()))
                pathOK = TRUE;
        }
        if (pathOK)
        {
            // load only if sqlite.dll is not already loaded
            CSQLite3DynLoad* sqlite3_Dyn = sqlite3_Dyn_InOut == NULL || *sqlite3_Dyn_InOut == NULL ? new CSQLite3DynLoad() : (CSQLite3DynLoad*)*sqlite3_Dyn_InOut;
            if (sqlite3_Dyn->OK)
            {
                sqlite3* pDb = NULL;
                sqlite3_stmt* pStmt;
                // REVERTED to char. sqlite3_prepare_v2 takes 'const char* zSql'
                // and this text is UTF-8 by that library's contract - the variable is even named
                // utf8Select. An earlier sweep widened it, which is a hard C2440 against its own
                // narrow initialiser. Byte domain, and it stays that way.
                char utf8Select[] = "SELECT data_value FROM data WHERE entry_key = 'local_sync_root_path';"; // UTF8 string (if any extra character were to be inserted, it would need to be converted from ANSI->UTF8)

                // sqlite3_open_v2's filename is UTF-8 BYTES, so this is a
                // std::string and not a path buffer. sDbPath is ALREADY UTF-16, so ONE conversion
                // does it - the old code read that wide buffer as ANSI first and converted back
                // out. Sized from the source: UTF-8 needs at most 3 bytes per UTF-16 code unit.
                std::string utf8DbPath;
                if (Win32EncodeText(CP_UTF8, sDbPath, utf8DbPath))
                {
                    int iSts = sqlite3_Dyn->open_v2(utf8DbPath.c_str(), &pDb, SQLITE_OPEN_READONLY, NULL);
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
                                // utf8Path is UTF-8 BYTES from sqlite3, so the
                                // cast is (const char*) - it used to say (const wchar_t*).
                                // The former narrow round trip defaulted to CP_ACP purely to
                                // strip a "\\?\" prefix in narrow, corrupting any non-ASCII path.
                                // widePath already holds the correct UTF-16, so strip it there.
                                if (utf8Path != NULL && utf8PathLen > 0 &&
                                    Win32DecodeText(CP_UTF8, reinterpret_cast<const char*>(utf8Path),
                                                    static_cast<size_t>(utf8PathLen), widePath) &&
                                    widePath.find(L'\0') == std::wstring::npos)
                                {
                                    if (widePath.length() >= 8 && _wcsnicmp(widePath.c_str(), L"\\\\?\\UNC\\", 8) == 0)
                                        widePath.replace(0, 8, L"\\\\");
                                    else
                                    {
                                        if (widePath.length() >= 4 && wcsncmp(widePath.c_str(), L"\\\\?\\", 4) == 0)
                                            widePath.erase(0, 4);
                                    }
                                    gdPath = widePath;
                                    TRACE_IW(L"Google Drive path: " << gdPath);
                                    ret = TRUE;
                                    *pathIsFromConfig = TRUE;
                                }
                                else
                                    TRACE_EW(L"SQLite: cannot decode the UTF-8 path value from " << sDbPath);
                            }
                            else
                                TRACE_EW(L"SQLite: cannot step " << sDbPath);
                            sqlite3_Dyn->finalize(pStmt);
                        }
                        else
                            TRACE_IW(L"SQLite: cannot prepare " << sDbPath); // this is hit when GD is installed but "not signed in"
                    }
                    else
                        TRACE_EW(L"SQLite: cannot open " << sDbPath);
                    if (pDb != NULL)
                        sqlite3_Dyn->close(pDb);
                }
            }
            if (sqlite3_Dyn_InOut != NULL)
                *sqlite3_Dyn_InOut = sqlite3_Dyn; // return loaded sqlite.dll for further use (could have been loaded before calling this function)
            else
                delete sqlite3_Dyn; // release sqlite.dll, nobody is waiting for it
        }
        else
            TRACE_IW(L"Cannot find Google Drive's configuration file: " << sDbPath);
    }
    else
        TRACE_E("Cannot get value of CSIDL_LOCAL_APPDATA!");

    if (!ret)
    {
        const GUID& fallbackFolder = WindowsVistaAndLater ? FOLDERID_Profile : FOLDERID_Documents;
        if (shell != NULL && shell->GetKnownFolderPath(fallbackFolder, fallbackPath).success)
        {
            SalPathAppendW(fallbackPath, L"Google Drive");
            TRACE_IW(L"Using default Google Drive path instead: " << fallbackPath);
            gdPath = std::move(fallbackPath);
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

void InitShellIconOverlaysAuxAux(CLSID* clsid, const wchar_t* name, ShellOverlayDiagRecord* diag)
{
    IShellIconOverlayIdentifier* iconOverlayIdentifier;
    // Assign the HRESULT rather than comparing inline: it is the only evidence we get when
    // a handler refuses to activate, and it used to be discarded here.
    HRESULT coCreateHr = CoCreateInstance(*clsid, NULL,
                                          CLSCTX_INPROC_SERVER, IID_IShellIconOverlayIdentifier,
                                          (LPVOID*)&iconOverlayIdentifier);
    if (coCreateHr == S_OK &&
        iconOverlayIdentifier != NULL) // probably unnecessary test, just to be safe
    {
        std::wstring iconFile;
        int iconIndex;
        DWORD flags;
        // NOTE: strict == S_OK, so a handler returning S_FALSE is dropped exactly like one
        // that failed. Recording the value is what will let us tell those apart (issue #90).
        HRESULT overlayInfoHr = GetOverlayInfoDynamic(iconOverlayIdentifier, iconFile, &iconIndex, &flags);
        if (diag != NULL)
        {
            diag->Hr = overlayInfoHr;
            diag->IconFile = iconFile;
        }
        if (overlayInfoHr == S_OK)
        {
            if (diag != NULL)
            {
                diag->InfoFlags = flags;
                diag->IconIndex = iconIndex;
            }
            if (flags & ISIOI_ICONFILE)
            {
                int priority; // priority: first we will query overlay handlers with the lowest priority number
                if (iconOverlayIdentifier->GetPriority(&priority) != S_OK)
                {
                    priority = 100; // lowest priority
                    TRACE_EW(L"InitShellIconOverlays(): GetPriority method returns error for: " << name);
                }
                if (diag != NULL)
                    diag->Priority = priority;

                if ((flags & ISIOI_ICONINDEX) == 0)
                    iconIndex = 0;

                // wide: iconFile is already the genuine wide path from
                // GetOverlayInfo - use it directly instead of narrowing it first.
                // Load this overlay's icon at all three sizes (issue #90 - see
                // shiconov_icons.h for why this must not be a single packed call).
                HICON iconOverlay[ICONSIZE_COUNT] = {0};
                LoadShellOverlayIconsW(iconFile.c_str(), iconIndex, IconSizes, ICONSIZE_COUNT,
                                       iconOverlay);

                int x;
                for (x = 0; x < ICONSIZE_COUNT; x++)
                    if (iconOverlay[x] != NULL)
                        HANDLES_ADD(__htIcon, __hoLoadImage, iconOverlay[x]);

                if (diag != NULL)
                {
                    diag->IconsOk = (unsigned char)((iconOverlay[ICONSIZE_16] != NULL ? 1 : 0) |
                                                    (iconOverlay[ICONSIZE_32] != NULL ? 2 : 0) |
                                                    (iconOverlay[ICONSIZE_48] != NULL ? 4 : 0));
                }

                // insert handler into ShellIconOverlays
                if (iconOverlay[ICONSIZE_16] != NULL && iconOverlay[ICONSIZE_32] != NULL && iconOverlay[ICONSIZE_48] != NULL)
                {
                    BOOL isGoogleDrive = FALSE;
                    const wchar_t* nameSkipWS = name;
                    while (*nameSkipWS != 0 && *nameSkipWS == ' ')
                        nameSkipWS++;
                    if (_wcsicmp(name, L"GDriveBlacklistedOverlay") == 0 ||
                        _wcsicmp(name, L"GDriveSharedEditOverlay") == 0 ||
                        _wcsicmp(name, L"GDriveSharedOverlay") == 0 ||
                        _wcsicmp(name, L"GDriveSharedViewOverlay") == 0 ||
                        _wcsicmp(name, L"GDriveSyncedOverlay") == 0 ||
                        _wcsicmp(name, L"GDriveSyncingOverlay") == 0 ||
                        _wcsicmp(nameSkipWS, L"GoogleDriveBlacklisted") == 0 ||
                        _wcsicmp(nameSkipWS, L"GoogleDriveSynced") == 0 ||
                        _wcsicmp(nameSkipWS, L"GoogleDriveSyncing") == 0)
                    {
                        isGoogleDrive = TRUE;
                        // Google Drive handlers are called only for subdirectories of the path where Google Drive resides
                        ShellIconOverlays.InitGoogleDrivePath(NULL, FALSE /* icon overlays not yet loaded */);
                    }
#ifdef _DEBUG
                    if (!isGoogleDrive && (StrIStr(name, L"GDrive") != NULL || StrIStr(name, L"GoogleDrive") != NULL))
                        TRACE_EW(L"It seems Google Drive again changed names of Icon Overlays in registry. New name: " << name);
#endif // _DEBUG

                    CShellIconOverlayItem* item = new CShellIconOverlayItem;
                    if (item != NULL)
                    {
                        if (ShellIconOverlays.Add(item /*, priority*/))
                        {
                            item->Priority = priority;
                            item->Identifier = iconOverlayIdentifier;
                            item->IconOverlayIdCLSID = *clsid;
                            item->IconOverlayName = name;
                            item->GoogleDriveOverlay = isGoogleDrive;
                            iconOverlayIdentifier = NULL;
                            for (x = 0; x < ICONSIZE_COUNT; x++)
                            {
                                item->IconOverlay[x] = iconOverlay[x];
                                iconOverlay[x] = NULL;
                            }
                            if (diag != NULL)
                            {
                                diag->Outcome = OverlayOutcome::Loaded;
                                diag->LoadedIndex = ShellIconOverlays.GetCount() - 1;
                                ShellOverlayDiag.Header.Loaded++;
                            }
                        }
                        else
                        {
                            // Either the cap was reached or the array could not grow; Add()
                            // does not distinguish, and the second case logs nothing at all.
                            if (diag != NULL)
                                diag->Outcome = ShellIconOverlayCapReached(ShellIconOverlays.GetCount())
                                                    ? OverlayOutcome::CapReached
                                                    : OverlayOutcome::ArrayGrowFailed;
                            delete item;
                        }
                    }
                    else if (diag != NULL)
                        diag->Outcome = OverlayOutcome::ItemAllocFailed;
                }
                else
                {
                    if (diag != NULL)
                        diag->Outcome = OverlayOutcome::IconExtractFailed;
                    TRACE_EW(L"InitShellIconOverlays(): unable to get icons of all sizes for: " << name);
                }

                for (x = 0; x < ICONSIZE_COUNT; x++)
                    if (iconOverlay[x] != NULL)
                        HANDLES(DestroyIcon(iconOverlay[x]));
            }
            else
            {
                if (diag != NULL)
                    diag->Outcome = OverlayOutcome::NoIconFileFlag;
                TRACE_IW(L"InitShellIconOverlays(): unable to get icon overlay location for: " << name);
            }
        }
        else
        {
            if (diag != NULL)
                diag->Outcome = OverlayOutcome::GetOverlayInfoFailed;
            TRACE_IW(L"InitShellIconOverlays(): GetOverlayInfo method returns error for: " << name); // Tortoise does this when more than 12 handlers are registered
        }
        if (iconOverlayIdentifier != NULL)
            iconOverlayIdentifier->Release();
    }
    else
    {
        if (diag != NULL)
        {
            diag->Outcome = OverlayOutcome::CoCreateFailed;
            diag->Hr = coCreateHr;
        }
        TRACE_IW(L"InitShellIconOverlays(): unable to create object for: " << name); // e.g., "Offline Files" reports this on clean XP
    }
}

void InitShellIconOverlaysAux(CLSID* clsid, const wchar_t* name, ShellOverlayDiagRecord* diag)
{
    __try
    {
        InitShellIconOverlaysAuxAux(clsid, name, diag);
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

    // Snapshot the configuration this run is actually using. Which registry root Sally
    // resolved, and whether the overlay values were present in it, is the question behind
    // the imported-configuration theory for issue #90: a config inherited from an older
    // Altap Salamander install can carry a disabled-handler list, or force overlays off
    // outright when the value pair is missing from a version-41-or-later config.
    ShellOverlayDiag.Reset();
    ShellOverlayDiag.Header.AnsiCodePage = GetACP();
    ShellOverlayDiag.Header.IconSizes[0] = IconSizes[ICONSIZE_16];
    ShellOverlayDiag.Header.IconSizes[1] = IconSizes[ICONSIZE_32];
    ShellOverlayDiag.Header.IconSizes[2] = IconSizes[ICONSIZE_48];
    ShellOverlayDiag.Header.SystemDpi = (UINT)SystemDPI;
    ShellOverlayDiag.Header.EnableCustomIconOverlays = Configuration.EnableCustomIconOverlays != FALSE;
    SetOverlayDiagConfigRoot(ShellOverlayDiag.Header, SALAMANDER_ROOT_REG);
    if (Configuration.DisabledCustomIconOverlays != NULL)
        ShellOverlayDiag.Header.DisabledList = Configuration.DisabledCustomIconOverlays;

    HKEY clsIDKey = NULL;
    LONG errRet;
    RegistryResult registryResult = gRegistry->OpenKeyRead(
        HKEY_CLASSES_ROOT, SAL_REG_KEY_CLASSES_ROOT_CLSID_W, clsIDKey);
    if (!registryResult.success)
    {
        errRet = registryResult.errorCode;
        TRACE_IW(L"InitShellIconOverlays(): error opening HKEY_CLASSES_ROOT\\CLSID key: " << GetErrorTextOwned(errRet).c_str());
    }

    HKEY key = NULL;
    registryResult = gRegistry->OpenKeyRead(
        HKEY_LOCAL_MACHINE, SAL_REG_KEY_SHELL_ICON_OVERLAY_IDENTIFIERS_W, key);
    if (registryResult.success)
    {
        TIndirectArray<wchar_t> keyNames(15, 5);
        std::vector<std::wstring> registeredNames;
        registryResult = gRegistry->EnumSubKeys(key, registeredNames);
        if (registryResult.success)
        {
            for (const std::wstring& name : registeredNames)
            {
                int s = 0; // insert new name, there are about 15 of them, so we don't need any quick-sort
                for (; s < keyNames.Count && _wcsicmp(name.c_str(), keyNames[s]) >= 0; s++)
                    ;
                keyNames.Insert(s, DupStr(name.c_str()));
            }
        }
        else
            TRACE_EW(L"InitShellIconOverlays(): error enumerating ShellIconOverlayIdentifiers key: " << GetErrorTextOwned(registryResult.errorCode).c_str());
        // go through sorted list of icon-overlay-handlers (Explorer defines handler priority alphabetically)
        // handlers past MAX_SHELL_ICON_OVERLAYS are rejected in CShellIconOverlays::Add(); Explorer itself shows only ~11-15
        for (int s = 0; s < keyNames.Count; s++)
        { // open icon-overlay-handler key
            // Register this handler in the diagnostic ledger before anything can go wrong,
            // so that a key which fails at any of the branches below still appears in the
            // bug report instead of vanishing without trace (see shiconov_diag.h).
            wchar_t diagName[128];
            diagName[0] = 0;
            lstrcpynW(diagName, keyNames[s], 128);
            diagName[127] = 0;
            ShellOverlayDiagRecord* diag = ShellOverlayDiag.Add(diagName, L"");
            ShellOverlayDiag.Header.Registered++;

            HKEY handler = NULL;
            registryResult = gRegistry->OpenKeyRead(key, keyNames[s], handler);
            if (registryResult.success)
            {
                wchar_t txtClsId[100];
                // lpcbData counts BYTES; this buffer is wchar_t, so a bare 100
                // would declare half of it and truncate any CLSID string past 49 characters.
                DWORD size = (DWORD)sizeof(txtClsId);
                RegValueType type = RegValueType::None;
                registryResult = gRegistry->ReadValue(handler, NULL, type, txtClsId, size);
                if (registryResult.success)
                {
                    if (type == RegValueType::String)
                    {
                        txtClsId[99] = 0; // just to be safe
                        // txtClsId is already wchar_t, so this CP_ACP round trip
                        // converted data that was never narrow. CLSIDFromString takes LPCOLESTR
                        // and consumes it directly, which retires the whole scratch buffer.
                        CLSID clsid;
                        if (CLSIDFromString(txtClsId, &clsid) == NOERROR)
                        {
                            wchar_t descr[1000];
                            descr[0] = 0;
                            if (clsIDKey != NULL)
                            {
                                HKEY classKey = NULL;
                                // txtClsId is wide, so the unsuffixed form resolved
                                // to RegOpenKeyExA. Needle-for-needle - the win32 count is unchanged.
                                registryResult = gRegistry->OpenKeyRead(clsIDKey, txtClsId, classKey);
                                if (registryResult.success)
                                {
                                    DWORD descrSize = (DWORD)sizeof(descr); // BYTES, not characters
                                    RegValueType descrType = RegValueType::None;
                                    registryResult = gRegistry->ReadValue(classKey, NULL, descrType, descr, descrSize);
                                    if (registryResult.success)
                                    {
                                        if (descrType != RegValueType::String)
                                        {
                                            TRACE_EW(L"InitShellIconOverlays(): default value from CLSID\\" << txtClsId << L" key in not REG_SZ!");
                                            descr[0] = 0;
                                        }
                                    }
                                    else
                                    {
                                        errRet = registryResult.errorCode;
                                        if (errRet != ERROR_FILE_NOT_FOUND) // reports this error when handler has no description (which is apparently not an error, because on Vista it applies to e.g., Offline Files)
                                        {
                                            TRACE_EW(L"InitShellIconOverlays(): error reading default value from CLSID\\" << txtClsId << L" key: " << GetErrorTextOwned(errRet).c_str());
                                        }
                                        descr[0] = 0;
                                    }
                                    gRegistry->CloseKey(classKey);
                                }
                                else
                                {
                                    errRet = registryResult.errorCode;
                                    // Petr: after Google Drive update on 30.8.2015, the key for GDriveSharedOverlay was missing
                                    //       under CLSID in registry, I found no difference in overlay display compared to Explorer,
                                    //       so I bypassed this annoying message by removing the
                                    //       GDriveSharedOverlay key from ShellIconOverlayIdentifiers list
                                    TRACE_EW(L"InitShellIconOverlays(): error opening CLSID\\" << txtClsId << L" key: " << GetErrorTextOwned(errRet).c_str());
                                }
                            }

                            CShellIconOverlayItem2* item2 = new CShellIconOverlayItem2;
                            if (item2 != NULL)
                            {
                                ListOfShellIconOverlays.Add(item2);
                                if (ListOfShellIconOverlays.IsGood())
                                {
                                    item2->IconOverlayName = keyNames[s];
                                    item2->IconOverlayDescr = descr;
                                }
                                else
                                {
                                    ListOfShellIconOverlays.ResetState();
                                    delete item2;
                                }
                            }

                            if (diag != NULL)
                                diag->Clsid = txtClsId;

                            // This gate is the only drop in the whole subsystem that logs
                            // nothing in any build, and it is permanent: a handler lands in
                            // DisabledCustomIconOverlays when the user ever answered "disable
                            // this handler" to an overlay crash, and that answer is written
                            // straight to HKCU (callstk.cpp) and survives reinstalls.
                            if (!IsDisabledCustomIconOverlays(keyNames[s]))
                            {
                                InitShellIconOverlaysAux(&clsid, keyNames[s], diag);
                            }
                            else if (diag != NULL)
                            {
                                diag->Outcome = Configuration.EnableCustomIconOverlays
                                                    ? OverlayOutcome::SkippedUserDisabled
                                                    : OverlayOutcome::SkippedGloballyDisabled;
                            }
                        }
                        else
                        {
                            if (diag != NULL)
                                diag->Outcome = OverlayOutcome::InvalidClsid;
                            TRACE_EW(L"InitShellIconOverlays(): invalid CLSID: " << txtClsId);
                        }
                    }
                    else
                    {
                        if (diag != NULL)
                            diag->Outcome = OverlayOutcome::RegValueNotSz;
                        TRACE_EW(L"InitShellIconOverlays(): default value from ShellIconOverlayIdentifiers\\" << keyNames[s] << L" key in not REG_SZ!");
                    }
                }
                else
                {
                    errRet = registryResult.errorCode;
                    if (diag != NULL)
                    {
                        diag->Outcome = OverlayOutcome::RegValueMissing;
                        diag->Hr = HRESULT_FROM_WIN32(errRet);
                    }
                    TRACE_EW(L"InitShellIconOverlays(): error reading default value from ShellIconOverlayIdentifiers\\" << keyNames[s] << L" key: " << GetErrorTextOwned(errRet).c_str());
                }

                gRegistry->CloseKey(handler);
            }
            else
            {
                errRet = registryResult.errorCode;
                if (diag != NULL)
                {
                    diag->Outcome = OverlayOutcome::RegKeyOpenFailed;
                    diag->Hr = HRESULT_FROM_WIN32(errRet);
                }
                TRACE_EW(L"InitShellIconOverlays(): error opening ShellIconOverlayIdentifiers\\" << keyNames[s] << L" key: " << GetErrorTextOwned(errRet).c_str());
            }
        }
        gRegistry->CloseKey(key);
    }
    else
    {
        errRet = registryResult.errorCode;
        TRACE_IW(L"InitShellIconOverlays(): error opening ShellIconOverlayIdentifiers key: " << GetErrorTextOwned(errRet).c_str());
    }

    if (clsIDKey != NULL)
        gRegistry->CloseKey(clsIDKey);
}

void ReleaseShellIconOverlays()
{
    CALL_STACK_MESSAGE1("ReleaseShellIconOverlays()");

    ShellIconOverlays.Release();
    ListOfShellIconOverlays.DestroyMembers();
}

//
// *****************************************************************************

BOOL IsNameInListOfDisabledCustomIconOverlays(const wchar_t* name)
{
    if (Configuration.DisabledCustomIconOverlays != NULL)
    {
        const wchar_t* s = Configuration.DisabledCustomIconOverlays;
        std::wstring entry;
        while (*s != 0)
        {
            if (*s == ';')
            {
                if (*(s + 1) == ';')
                {
                    entry.push_back(L';');
                    s += 2;
                }
                else
                {
                    if (_wcsicmp(name, entry.c_str()) == 0)
                        return TRUE; // is disabled

                    // go to next name
                    s++;
                    entry.clear();
                }
            }
            else
                entry.push_back(*s++);
        }
        if (_wcsicmp(name, entry.c_str()) == 0)
            return TRUE; // is disabled
    }
    return FALSE; // is not in list
}

BOOL IsDisabledCustomIconOverlays(const wchar_t* name)
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

BOOL AddToListOfDisabledCustomIconOverlays(const wchar_t* name)
{
    if (*name == 0)
    {
        TRACE_E("AddToListOfDisabledCustomIconOverlays(): empty name is unexpected here!");
        return TRUE; // nothing to do
    }
    std::wstring escapedName;
    const wchar_t* s = name;
    while (*s != 0)
    {
        if (*s == ';')
        {
            escapedName += L";;";
            s++;
        }
        else
            escapedName.push_back(*s++);
    }
    wchar_t* m = Configuration.DisabledCustomIconOverlays;
    int mLen = (m != NULL ? (int)wcslen(m) : 0);
    m = (wchar_t*)realloc(m, (mLen + 1 + escapedName.length() + 1) * sizeof(wchar_t));
    if (m != NULL)
    {
        if (mLen > 0)
            wcscpy(m + mLen++, L";");
        wcscpy(m + mLen, escapedName.c_str());
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
        __except (CCallStack::HandleException(GetExceptionInformation(), -1, IconOverlayName.c_str()))
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

    if (ShellIconOverlayCapReached(Overlays.Count))
    {
        TRACE_I("CShellIconOverlays::Add(): unexpected situation: more than MAX_SHELL_ICON_OVERLAYS icon-overlay-handlers!");
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

void CreateIconReadersIconOverlayIdsAuxAux(CLSID* clsid, const wchar_t* name, IShellIconOverlayIdentifier** ids, int i)
{
    IShellIconOverlayIdentifier* iconOverlayIdentifier;
    HRESULT coCreateHr = CoCreateInstance(*clsid, NULL, CLSCTX_INPROC_SERVER, IID_IShellIconOverlayIdentifier,
                                          (LPVOID*)&iconOverlayIdentifier);
    if (coCreateHr == S_OK &&
        iconOverlayIdentifier != NULL) // probably unnecessary test, just to be safe
    {
        // just for form's sake, call the usual methods (as if we were Explorer and wanted to show those overlays)
        std::wstring iconFile;
        int iconIndex;
        DWORD flags;
        GetOverlayInfoDynamic(iconOverlayIdentifier, iconFile, &iconIndex, &flags);
        int priority;
        iconOverlayIdentifier->GetPriority(&priority);

        ids[i] = iconOverlayIdentifier;
    }
    else
    {
        // ids[i] stays NULL, so this handler silently matches nothing for the whole life of
        // this panel's icon-reader thread - invisible in the trace, and invisible in the
        // Configuration page, which still shows the handler as loaded and enabled.
        wchar_t diagName[128];
        diagName[0] = 0;
        lstrcpynW(diagName, name, 128);
        diagName[127] = 0;
        ShellOverlayDiagRecord* diag = ShellOverlayDiag.Find(diagName);
        if (diag != NULL)
        {
            // Two panel threads race here; the report is best-effort so an interlocked
            // counter is enough and no lock is warranted.
            InterlockedIncrement(&diag->ReaderFailures);
            diag->ReaderLastHr = coCreateHr;
        }
        TRACE_IW(L"CreateIconReadersIconOverlayIdsAuxAux(): unable to create object for icon-overlay handler: " << name << L"!");
    }
}

void CreateIconReadersIconOverlayIdsAux(CLSID* clsid, const wchar_t* name, IShellIconOverlayIdentifier** ids, int i)
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
                                                   Overlays[i]->IconOverlayName.c_str(), ids, i);
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
                __except (CCallStack::HandleException(GetExceptionInformation(), -1, Overlays[i]->IconOverlayName.c_str()))
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
                               int i, const wchar_t* path, const wchar_t* name, DWORD shAttrs)
{
    HRESULT res;
    if (iconReadersIconOverlayIds[i] != NULL &&
        (res = iconReadersIconOverlayIds[i]->IsMemberOf(path, shAttrs)) == S_OK)
    {
        return TRUE; // found
    }
    else
    {
        if (res != S_FALSE && res != 0x80070002) // 0x80070002 is "file not found", returned by "Offline Files" for everything that is not offline-available
            TRACE_IW(L"CShellIconOverlays::GetIconOverlayIndex(): overlay " << name << L": IsMemberOf() returns error: 0x" << std::hex << res << std::dec);
    }
    return FALSE;
}

BOOL GetIconOverlayIndexAux(IShellIconOverlayIdentifier** iconReadersIconOverlayIds,
                            int i, const wchar_t* path, const wchar_t* name, DWORD shAttrs)
{
    __try
    {
        return GetIconOverlayIndexAuxAux(iconReadersIconOverlayIds, i, path, name, shAttrs);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), -1, name))
    {
        TRACE_I("GetIconOverlayIndexAux: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // harder exit (this one still calls something)
    }
    return FALSE; // just for the compiler
}

DWORD_PTR SHGetFileInfoAux(LPCWSTR pszPath, DWORD dwFileAttributes, SHFILEINFOW* psfi,
                           UINT cbFileInfo, UINT uFlags)
{
    __try
    {
        return SHGetFileInfoW(pszPath, dwFileAttributes, psfi, cbFileInfo, uFlags);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 23))
    {
        FGIExceptionHasOccured++;
        return 0;
    }
}

DWORD
CShellIconOverlays::GetIconOverlayIndex(const wchar_t* path, const wchar_t* name,
                                        DWORD fileAttrs, int minPriority,
                                        IShellIconOverlayIdentifier** iconReadersIconOverlayIds,
                                        BOOL isGoogleDrivePath)
{
    CALL_STACK_MESSAGE_NONE // call-stack would only slow things down here

    if (path == NULL || name == NULL)
        return ICONOVERLAYINDEX_NOTUSED;

    std::wstring fullPath(path);
    if (!fullPath.empty() && fullPath.back() != L'\\')
        fullPath.push_back(L'\\');
    fullPath += name;

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
        if (GetIconOverlayIndexAux(iconReadersIconOverlayIds, i, fullPath.c_str(), overlay->IconOverlayName.c_str(), fileAttrs))
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
    std::wstring iconFile;
    int iconIndex;
    DWORD flags;
    if (GetOverlayInfoDynamic(item->Identifier, iconFile, &iconIndex, &flags) == S_OK)
    {
        if (flags & ISIOI_ICONFILE)
        {
            if ((flags & ISIOI_ICONINDEX) == 0)
                iconIndex = 0;

            // wide: iconFile is already the genuine wide path from
            // GetOverlayInfo - use it directly instead of narrowing it first.
            // Same loader as at startup. Getting this wrong here would re-drop every
            // overlay the moment the display colour depth changed (issue #90).
            HICON iconOverlay[ICONSIZE_COUNT] = {0};
            LoadShellOverlayIconsW(iconFile.c_str(), iconIndex, IconSizes, ICONSIZE_COUNT,
                                   iconOverlay);

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
    __except (CCallStack::HandleException(GetExceptionInformation(), -1, item->IconOverlayName.c_str()))
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
        std::wstring gdPath;
        BOOL pathIsFromConfig;
        if (GetGoogleDrivePath(gdPath, sqlite3_Dyn_InOut, &pathIsFromConfig))
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
    if (GoogleDrivePathIsFromCfg && !GoogleDrivePath.empty())
    {
        if (!GoogleDrivePathExists && gFileSystem->DirectoryExists(GoogleDrivePath.c_str()))
            GoogleDrivePathExists = TRUE;
        return GoogleDrivePathExists;
    }
    return FALSE;
}
