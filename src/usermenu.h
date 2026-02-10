// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

#define USRMNUARGS_MAXLEN 32772    // buffer size (+1 versus max string length) (=32776 (Vista/Win7 via .bat) - 5 ("C:\\a ") + 1)
#define USRMNUCMDLINE_MAXLEN 32777 // buffer size (+1 versus max string length)

//****************************************************************************
//
// CUserMenuIconBkgndReader
//

struct CUserMenuIconData
{
    CPathBuffer FileName;     // file name from which we read icon at IconIndex (via ExtractIconEx())
    DWORD IconIndex;          // see comment for FileName
    CPathBuffer UMCommand;    // file name from which we read icon (via GetFileOrPathIconAux())

    HICON LoadedIcon; // NULL = icon not loaded, otherwise handle of loaded icon

    CUserMenuIconData(const char* fileName, DWORD iconIndex, const char* umCommand);
    ~CUserMenuIconData();

    void Clear();
};

class CUserMenuIconDataArr : public TIndirectArray<CUserMenuIconData>
{
protected:
    DWORD IRThreadID; // unique ID of thread for loading these icons

public:
    CUserMenuIconDataArr() : TIndirectArray<CUserMenuIconData>(50, 50) { IRThreadID = 0; }

    void SetIRThreadID(DWORD id) { IRThreadID = id; }
    DWORD GetIRThreadID() { return IRThreadID; }

    HICON GiveIconForUMI(const char* fileName, DWORD iconIndex, const char* umCommand);
};

class CUserMenuIconBkgndReader
{
protected:
    BOOL SysColorsChanged; // helper variable to detect system color changes since config dialog opened

    CRITICAL_SECTION CS;       // critical section for object data access
    DWORD IconReaderThreadUID; // generator of unique thread IDs for icon reading
    BOOL CurIRThreadIDIsValid; // TRUE = thread is running and CurIRThreadID is valid
    DWORD CurIRThreadID;       // unique thread ID (see IconReaderThreadUID) reading icons for current user-menu version
    BOOL AlreadyStopped;       // TRUE = no more icon reading, main window closed/closing

    int UserMenuIconsInUse;                            // > 0: user menu icons are in an open menu, cannot update immediately; max 2 (Salamander config + Find: user menu)
    CUserMenuIconDataArr* UserMenuIIU_BkgndReaderData; // stash of new icons when UserMenuIconsInUse > 0
    DWORD UserMenuIIU_ThreadID;                        // stash of thread ID (data freshness) when UserMenuIconsInUse > 0

public:
    CUserMenuIconBkgndReader();
    ~CUserMenuIconBkgndReader();

    // main window is closing = no longer accept any user menu icon data
    void EndProcessing();

    // WARNING: 'bkgndReaderData' is deallocated inside
    void StartBkgndReadingIcons(CUserMenuIconDataArr* bkgndReaderData);

    BOOL IsCurrentIRThreadID(DWORD threadID);

    BOOL IsReadingIcons();

    // WARNING: after calling this function, this object is responsible for freeing 'bkgndReaderData'
    void ReadingFinished(DWORD threadID, CUserMenuIconDataArr* bkgndReaderData);

    // enter/leave section where user menu icons are used and thus cannot be updated
    // during this section (mainly opening the user menu)
    void BeginUserMenuIconsInUse();
    void EndUserMenuIconsInUse();

    // if icons were loaded for an already outdated user menu, returns FALSE, otherwise:
    // if icons are currently in an open menu (see UserMenuIconsInUse), returns FALSE;
    // if icons are not in an open menu, returns TRUE and WARNING: does not leave CS,
    // so access from other threads is blocked (mainly access to user menu from Find thread);
    // to leave CS after icon update, use LeaveCSAfterUMIconsUpdate()
    BOOL EnterCSIfCanUpdateUMIcons(CUserMenuIconDataArr** bkgndReaderData, DWORD threadID);
    void LeaveCSAfterUMIconsUpdate();

    void ResetSysColorsChanged() { SysColorsChanged = FALSE; }
    void SetSysColorsChanged() { SysColorsChanged = TRUE; }
    BOOL HasSysColorsChanged() { return SysColorsChanged; }
};

extern CUserMenuIconBkgndReader UserMenuIconBkgndReader;

//****************************************************************************
//
// CUserMenuItem
//

enum CUserMenuItemType
{
    umitItem,         // regular item
    umitSubmenuBegin, // marks popup start
    umitSubmenuEnd,   // marks popup end
    umitSeparator     // marks popup end
};

struct CUserMenuItem
{
    std::string ItemName,
        UMCommand,
        Arguments,
        InitDir,
        Icon;

    int ThroughShell,
        CloseShell,
        UseWindow,
        ShowInToolbar;

    CUserMenuItemType Type;

    HICON UMIcon;

    CUserMenuItem(const char* name, const char* umCommand, const char* arguments, const char* initDir, const char* icon,
                  int throughShell, int closeShell, int useWindow, int showInToolbar,
                  CUserMenuItemType type, CUserMenuIconDataArr* bkgndReaderData);

    CUserMenuItem();

    CUserMenuItem(CUserMenuItem& item, CUserMenuIconDataArr* bkgndReaderData);

    ~CUserMenuItem();

    // attempts to obtain icon handle in this order
    // a) Icon variable
    // b) SHGetFileInfo
    // c) take system default
    // background icon loading: if 'bkgndReaderData' is NULL, read immediately, otherwise icons
    // are read in the background - if 'getIconsFromReader' is FALSE, we collect into 'bkgndReaderData'
    // what to read; if TRUE, icons are already loaded and we just take handles of loaded
    // icons from 'bkgndReaderData'
    BOOL GetIconHandle(CUserMenuIconDataArr* bkgndReaderData, BOOL getIconsFromReader);

    // searches ItemName for & and returns HotKey and TRUE when found
    BOOL GetHotKey(char* key);

    BOOL Set(const char* name, const char* umCommand, const char* arguments, const char* initDir, const char* icon);
    void SetType(CUserMenuItemType type);
    BOOL IsGood() { return TRUE; }
};

//****************************************************************************
//
// CUserMenuItems
//

class CUserMenuItems : public TIndirectArray<CUserMenuItem>
{
public:
    CUserMenuItems(DWORD base, DWORD delta, CDeleteType dt = dtDelete)
        : TIndirectArray<CUserMenuItem>(base, delta, dt) {}

    // copies list from 'source'
    BOOL LoadUMI(CUserMenuItems& source, BOOL readNewIconsOnBkgnd);

    // finds the last (closing) submenu item addressed by variable 'index'
    // if not found, returns -1
    int GetSubmenuEndIndex(int index);
};
