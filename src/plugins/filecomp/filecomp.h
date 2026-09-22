// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// menu ID definitions
#define MID_COMPAREFILES 1

#define CURRENT_CONFIG_VERSION_PRESEPARATEOPTIONS 6
#define CURRENT_CONFIG_VERSION_NORECOMPAREBUTTON 7
// Version 8 and below store CRegBLOBConfigurationNarrow, whose LOGFONTA makes the blob 32 bytes
// shorter and puts every switch after the font at a different offset. Version 9 stores
// CRegBLOBConfiguration.
#define CURRENT_CONFIG_VERSION_NARROWLOGFONT 8
#define CURRENT_CONFIG_VERSION 9

// plugin interface object whose methods are invoked by Salamander
class CPluginInterface;
extern CPluginInterface PluginInterface;
extern BOOL AlwaysOnTop;

extern BOOL LoadOnStart;

// ****************************************************************************
//
// Plugin interface
//

class CPluginInterface : public CPluginInterfaceAbstract
{
public:
    virtual void WINAPI About(HWND parent);

    virtual BOOL WINAPI Release(HWND parent, BOOL force);

    virtual void WINAPI LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry);
    virtual void WINAPI SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry);
    virtual void WINAPI Configuration(HWND parent);

    virtual void WINAPI Connect(HWND parent, CSalamanderConnectAbstract* salamander);

    virtual void WINAPI ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData) { return; }

    virtual CPluginInterfaceForArchiverAbstract* WINAPI GetInterfaceForArchiver() { return NULL; };
    virtual CPluginInterfaceForViewerAbstract* WINAPI GetInterfaceForViewer() { return NULL; }
    virtual CPluginInterfaceForMenuExtAbstract* WINAPI GetInterfaceForMenuExt();
    virtual CPluginInterfaceForFSAbstract* WINAPI GetInterfaceForFS() { return NULL; }
    virtual CPluginInterfaceForThumbLoaderAbstract* WINAPI GetInterfaceForThumbLoader() { return NULL; }
    virtual void WINAPI Event(int event, DWORD param);
    virtual void WINAPI ClearHistory(HWND parent);
    virtual void WINAPI AcceptChangeOnPathNotification(const wchar_t* path, BOOL includingSubdirs) {}
    virtual void WINAPI PasswordManagerEvent(HWND parent, int event) {}
};

class CPluginInterfaceForMenu : public CPluginInterfaceForMenuExtAbstract
{
public:
    // returns the state of the menu item with the identifier 'id'; the return value is a
    // combination of flags (see MENU_ITEM_STATE_XXX); 'eventMask' corresponds to
    // CSalamanderConnectAbstract::AddMenuItem
    virtual DWORD WINAPI GetMenuItemState(int id, DWORD eventMask) { return 0; }

    // executes the menu command identified by 'id'; see
    // CSalamanderConnectAbstract::AddMenuItem for the meaning of 'eventMask'; 'salamander'
    // exposes helper methods for performing operations; 'parent' is the owner for message
    // boxes; returns TRUE if the panel selection should be cleared (Cancel was not used but
    // Skip might have been), otherwise FALSE (leave the selection as is);
    // NOTE: If the command modifies any path (disk or FS), it should call
    //       CSalamanderGeneralAbstract::PostChangeOnPathNotification to notify panels
    //       without automatic refresh and any open FS windows (both active and detached)
    virtual BOOL WINAPI ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander, HWND parent,
                                        int id, DWORD eventMask);
    virtual BOOL WINAPI HelpForMenuItem(HWND parent, int id);
    virtual void WINAPI BuildMenu(HWND parent, CSalamanderBuildMenuAbstract* salamander) {}
};

// ****************************************************************************
//
// CFileCompThread
//

class CFilecompThread : public CThread
{
public:
    std::wstring Path1;
    std::wstring Path2;
    BOOL DontConfirmSelection;
    std::string ReleaseEvent;

    CFilecompThread(const wchar_t* file1, const wchar_t* file2,
                    BOOL dontConfirmSelection, const char* releaseEvent)
        : CThread(L"Filecomp Thread")
    {
        Path1 = file1 ? file1 : L"";
        Path2 = file2 ? file2 : L"";
        DontConfirmSelection = dontConfirmSelection;
        ReleaseEvent = releaseEvent ? releaseEvent : "";
    }

    virtual unsigned Body();
};

extern CWindowQueue MainWindowQueue; // list of all FileComp windows
extern CThreadQueue ThreadQueue;     // list of all FileComp windows, workers, and the remote control
