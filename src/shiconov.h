// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

#include "common/widepath.h"

void InitShellIconOverlays();
void ReleaseShellIconOverlays();

struct CSQLite3DynLoadBase
{
    BOOL OK; // TRUE if SQLite3 is loaded successfully and ready to use
    HINSTANCE SQLite3DLL;

    CSQLite3DynLoadBase()
    {
        OK = FALSE;
        SQLite3DLL = NULL;
    }
    ~CSQLite3DynLoadBase()
    {
        if (SQLite3DLL != NULL)
            HANDLES(FreeLibrary(SQLite3DLL));
    }
};

struct CShellIconOverlayItem
{
    CPathBuffer IconOverlayName;             // key name under HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Explorer\ShellIconOverlayIdentifiers
    IShellIconOverlayIdentifier* Identifier; // IShellIconOverlayIdentifier object, WARNING: usable only in the main thread
    CLSID IconOverlayIdCLSID;                // CLSID of the corresponding IShellIconOverlayIdentifier object
    int Priority;                            // priority of this icon overlay (0-100, highest priority is zero)
    HICON IconOverlay[ICONSIZE_COUNT];       // icon overlay in all sizes
    BOOL GoogleDriveOverlay;                 // TRUE = Google Drive handler (they crash, we handle extra synchronization)

    void Cleanup();

    CShellIconOverlayItem();
    ~CShellIconOverlayItem();
};

class CShellIconOverlays
{
protected:
    TIndirectArray<CShellIconOverlayItem> Overlays; // priority-sorted list of icon overlays
    CRITICAL_SECTION GD_CS;                         // for GoogleDrive we must mutually exclude IsMemberOf calls from both icon readers (otherwise it crashes, corrupts heap)
    BOOL GetGDAlreadyCalled;                        // TRUE = Google Drive folder location already checked
    CPathBuffer GoogleDrivePath;                    // Google Drive folder (we do not call their handler elsewhere, it is very slow and crashes without extra synchronization)
    BOOL GoogleDrivePathIsFromCfg;                  // Google Drive folder read from config (FALSE = may be default only + Google Drive may not be installed)
    BOOL GoogleDrivePathExists;                     // does the Google Drive folder exist on disk?

public:
    CShellIconOverlays() : Overlays(1, 5)
    {
        HANDLES(InitializeCriticalSection(&GD_CS));
        GoogleDrivePath[0] = 0;
        GetGDAlreadyCalled = FALSE;
        GoogleDrivePathIsFromCfg = FALSE;
        GoogleDrivePathExists = FALSE;
    }
    ~CShellIconOverlays() { HANDLES(DeleteCriticalSection(&GD_CS)); }

    // adds 'item' to the array (Drive was incorrectly ordered by 'priority')
    BOOL Add(CShellIconOverlayItem* item /*, int priority*/);

    // releases all icon overlays
    void Release() { Overlays.Destroy(); }

    // allocates array of IShellIconOverlayIdentifier objects for the calling thread (we use COM in
    // STA threading model, so the object must be created and used in a single thread)
    IShellIconOverlayIdentifier** CreateIconReadersIconOverlayIds();

    // releases array of IShellIconOverlayIdentifier objects
    void ReleaseIconReadersIconOverlayIds(IShellIconOverlayIdentifier** iconReadersIconOverlayIds);

    // returns icon overlay index for file/dir "wPath+name"
    DWORD GetIconOverlayIndex(WCHAR* wPath, WCHAR* wName, char* aPath, char* aName, char* name,
                              DWORD fileAttrs, int minPriority,
                              IShellIconOverlayIdentifier** iconReadersIconOverlayIds,
                              BOOL isGoogleDrivePath);

    HICON GetIconOverlay(int iconOverlayIndex, CIconSizeEnum iconSize)
    {
        return Overlays[iconOverlayIndex]->IconOverlay[iconSize];
    }

    // called when display color depth changes, all overlay icons must be reloaded
    // WARNING: can only be called from the main thread
    void ColorsChanged();

    // if we have not done it yet, find where Google Drive lives; 'sqlite3_Dyn_InOut'
    // serves as a cache for sqlite.dll (if already loaded, use it; if loaded in
    // this function, return it)
    void InitGoogleDrivePath(CSQLite3DynLoadBase** sqlite3_Dyn_InOut, BOOL debugTestOverlays);

    BOOL HasGoogleDrivePath();

    BOOL GetPathForGoogleDrive(char* path, int pathLen)
    {
        strcpy_s(path, pathLen, GoogleDrivePath);
        return GoogleDrivePath[0] != 0;
    }

    void SetGoogleDrivePath(const char* path, BOOL pathIsFromConfig)
    {
        strcpy_s(GoogleDrivePath, GoogleDrivePath.Size(), path);
        GoogleDrivePathIsFromCfg = pathIsFromConfig;
        GoogleDrivePathExists = FALSE;
    }

    BOOL IsGoogleDrivePath(const char* path) { return GoogleDrivePath[0] != 0 && SalPathIsPrefix(GoogleDrivePath, path); }
};

struct CShellIconOverlayItem2 // only list of icon overlay handlers (for configuration dialog, Icon Overlays page)
{
    CPathBuffer IconOverlayName;  // key name under HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Explorer\ShellIconOverlayIdentifiers
    CPathBuffer IconOverlayDescr; // description of COM object icon overlay handler
};

extern CShellIconOverlays ShellIconOverlays;                           // array of all available icon overlays
extern TIndirectArray<CShellIconOverlayItem2> ListOfShellIconOverlays; // list of all icon overlay handlers
