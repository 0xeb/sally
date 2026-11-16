// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <libssh2.h>
#include <openssl/crypto.h>

#include "SallyUrls.h"
#include "../core/ssh_init.h"
#include "../ops/op_trace.h"
#include "fs_data.h"
#include "fs_interface.h"
#include "operations_registry.h"
#include "site_config.h"
#include "../ui/config_dialog.h"
#include "../ui/session_log_window.h"

#define SFTP_WIDEN_IMPL(value) L##value
#define SFTP_WIDEN(value) SFTP_WIDEN_IMPL(value)

// the plugin interface; the object is returned from SalamanderPluginEntry
class CPluginInterface : public CPluginInterfaceAbstract
{
public:
    virtual void WINAPI About(HWND parent);

    virtual BOOL WINAPI Release(HWND parent, BOOL force);

    virtual void WINAPI LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry);
    virtual void WINAPI SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry);
    virtual void WINAPI Configuration(HWND parent);

    virtual void WINAPI Connect(HWND parent, CSalamanderConnectAbstract* salamander);

    virtual void WINAPI ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData);

    virtual CPluginInterfaceForArchiverAbstract* WINAPI GetInterfaceForArchiver() { return nullptr; }
    virtual CPluginInterfaceForViewerAbstract* WINAPI GetInterfaceForViewer() { return nullptr; }
    virtual CPluginInterfaceForMenuExtAbstract* WINAPI GetInterfaceForMenuExt();
    virtual CPluginInterfaceForFSAbstract* WINAPI GetInterfaceForFS();
    virtual CPluginInterfaceForThumbLoaderAbstract* WINAPI GetInterfaceForThumbLoader() { return nullptr; }

    virtual void WINAPI Event(int event, DWORD param) {}
    virtual void WINAPI ClearHistory(HWND parent) {}
    virtual void WINAPI AcceptChangeOnPathNotification(const wchar_t* path, BOOL includingSubdirs);

    virtual void WINAPI PasswordManagerEvent(HWND parent, int event);
};

CPluginInterface PluginInterface;
CPluginInterfaceForFS InterfaceForFS;
CPluginInterfaceForMenuExt InterfaceForMenuExt;

HINSTANCE DLLInstance = nullptr; // handle to this DLL
HINSTANCE HLanguage = nullptr;   // module with the language-dependent resources (this DLL)

CSalamanderGeneralAbstract* SalamanderGeneral = nullptr;

// variable definition for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = nullptr;

// variable definition for "spl_com.h"
int SalamanderVersion = 0;

CSalamanderGUIAbstract* SalamanderGUI = nullptr;

std::wstring AssignedFSName;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
        DLLInstance = hinstDLL;

    return TRUE; // DLL can be loaded
}

#ifdef TRACE_ENABLE
// The text of the operations layer (see ops/op_trace.h) goes to Sally's trace server. Called on any thread.
static void ForwardOpTrace(const char* text)
{
    TRACE_I("SFTP ops: " << text);
}
#endif

// F1 and Help buttons of the plugin's dialogs (any dialog thread): Sally opens the page of the sftp manual for the id
static void WINAPI HelpCallback(HWND window, UINT helpID)
{
    SalamanderGeneral->OpenHtmlHelp(window, HHCDisplayContext, helpID, FALSE);
}

std::wstring LangStr(int resID)
{
    return SPLLoadStrOwned(SalamanderGeneral, HLanguage, resID);
}

//
// ****************************************************************************
// SalamanderPluginGetReqVer and SalamanderPluginGetSDKVer
//

int WINAPI SalamanderPluginGetReqVer()
{
    return LAST_VERSION_OF_SALAMANDER;
}

int WINAPI SalamanderPluginGetSDKVer()
{
    return LAST_VERSION_OF_SALAMANDER; // return current SDK version
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

    // this plugin is made for the current version of Salamander and higher - perform a check
    if (SalamanderVersion < LAST_VERSION_OF_SALAMANDER)
    { // reject older versions
        MessageBoxW(salamander->GetParentWindow(), SFTP_WIDEN(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"SFTP" /* do not translate */, MB_OK | MB_ICONERROR);
        return nullptr;
    }

    // the plugin's resources (every language) are in its own DLL
    HLanguage = DLLInstance;

    // obtain the general Salamander interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();

    // obtain the interface providing customized Windows controls used in Salamander
    SalamanderGUI = salamander->GetSalamanderGUI();

    bool winLibInitialized = false;
    try
    {
        // provide basic information about the plugin
        if (!salamander->SetBasicPluginData(LangStr(IDS_PLUGINNAME).c_str(),
                                            FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION | FUNCTION_FILESYSTEM,
                                            SFTP_WIDEN(VERSINFO_VERSION_NO_PLATFORM), SFTP_WIDEN(VERSINFO_COPYRIGHT),
                                            LangStr(IDS_PLUGIN_DESCRIPTION).c_str(), L"SFTP", nullptr, L"sftp"))
            return nullptr;

        salamander->SetPluginHomePageURL(Sally::Urls::Website);

        // names the manual ("sftp"); Sally opens its pages on the website
        SalamanderGeneral->SetHelpFileName(L"sftp.chm");

        // remembered passwords go through Sally's password manager: ask for PasswordManagerEvent calls
        SalamanderGeneral->SetPluginUsesPasswordManager();

        // obtain our FS name (it might not stay "sftp" because Salamander can adjust it)
        AssignedFSName = SPLGetPluginFSNameOwned(SalamanderGeneral, 0);
        if (AssignedFSName.empty())
            return nullptr;

        // WinLib (dark mode, dialogs) is released in Release, or here when a later step fails
        if (!InitializeWinLib(L"SFTP" /* do not translate */, DLLInstance))
            return nullptr;
        winLibInitialized = true;
        SetupWinLibHelp(HelpCallback);
        const std::wstring invalidNumber = LangStr(IDS_INVALIDNUMBER);
        const std::wstring pluginTitle = LangStr(IDS_PLUGINNAME);
        SetWinLibStrings(invalidNumber.c_str(), pluginTitle.c_str());

#ifdef TRACE_ENABLE // TRACE_I does nothing otherwise
        SetOpTraceSink(ForwardOpTrace);
#endif

        // last step, so that nothing more has to be undone when it fails
        CSshError sshInitError;
        if (!SshGlobalInit(&sshInitError))
        {
            ReleaseWinLib(DLLInstance);
            winLibInitialized = false;
            SalamanderGeneral->SalMessageBox(salamander->GetParentWindow(),
                                             SPLFormatStringOwned(LangStr(IDS_LIBSSH2_INIT_FAILED).c_str(), sshInitError.libssh2Code).c_str(),
                                             LangStr(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONERROR);
            return nullptr;
        }
    }
    catch (...)
    {
        TRACE_E("SalamanderPluginEntry: out of memory.");
        if (winLibInitialized)
            ReleaseWinLib(DLLInstance);
        return nullptr;
    }

    return &PluginInterface;
}

//
// ****************************************************************************
// CPluginInterface
//

void WINAPI
CPluginInterface::About(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginInterface::About()");
    try
    {
        const std::wstring text = SPLFormatStringOwned(
            LangStr(IDS_ABOUT_FORMAT).c_str(), LangStr(IDS_PLUGINNAME).c_str(),
            SFTP_WIDEN(VERSINFO_VERSION), SFTP_WIDEN(VERSINFO_COPYRIGHT),
            LangStr(IDS_PLUGIN_DESCRIPTION).c_str(), libssh2_version(0), OpenSSL_version(OPENSSL_VERSION));
        SalamanderGeneral->SalMessageBox(parent, text.c_str(), LangStr(IDS_ABOUT).c_str(), MB_OK | MB_ICONINFORMATION);
    }
    catch (...)
    {
        TRACE_E("About: out of memory.");
    }
}

void WINAPI
CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::LoadConfiguration(, ,)");
    // catches everything itself; a read-only (newer version) configuration is reported when the user tries to
    // change the site list, not with a message box during Sally's startup
    SiteConfig.Load(regKey, registry);
}

void WINAPI
CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, ,)");
    SiteConfig.Save(regKey, registry); // catches everything itself
}

// Plugins / SFTP / Configuration (also the Configure button of the plugin manager). Main thread.
void ShowPluginConfiguration(HWND parent)
{
    try
    {
        CGlobalConfig config = GlobalConfigStore().Get();
        if (!ShowConfigDialog(parent, config))
            return;
        GlobalConfigStore().Set(config);
        SiteConfig.SaveNow(); // the values must survive a crash of Sally, like the sites
    }
    catch (...)
    {
        TRACE_E("Configuration: unexpected exception.");
    }
}

void WINAPI
CPluginInterface::Configuration(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Configuration()");
    ShowPluginConfiguration(parent);
}

// The state at the event: for CHANGED and REMOVED the old key is still usable, for CREATED/CHANGED the new master
// password is already set (the same order and assumptions as the FTP plugin).
void WINAPI
CPluginInterface::PasswordManagerEvent(HWND parent, int event)
{
    CALL_STACK_MESSAGE2("CPluginInterface::PasswordManagerEvent(, %d)", event);
    try
    {
        bool allConverted = true;
        if (event == PME_MASTERPASSWORDCREATED || event == PME_MASTERPASSWORDCHANGED || event == PME_MASTERPASSWORDREMOVED)
            allConverted &= SiteConfig.ReencryptAll(false); // AES (old key) -> scrambled
        if (event == PME_MASTERPASSWORDCREATED || event == PME_MASTERPASSWORDCHANGED)
            SiteConfig.ReencryptAll(true); // scrambled -> AES with the new key
        if (!allConverted)
            SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_CANNOT_DECRYPT_SOMEPASSWORDS).c_str(),
                                             LangStr(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
    }
    catch (...)
    {
        TRACE_E("PasswordManagerEvent: out of memory.");
    }
}

BOOL WINAPI
CPluginInterface::Release(HWND parent, BOOL force)
{
    CALL_STACK_MESSAGE2("CPluginInterface::Release(, %d)", force);
    // Running transfers: ask first (nothing is changed when the user says No), then cancel them and join their threads.
    try
    {
        if (!OperationsRegistry.ConfirmRelease(parent, force))
            return FALSE;
        OperationsRegistry.Shutdown();
    }
    catch (...)
    {
        TRACE_E("Release: unexpected exception while stopping the operations.");
    }
    // Every session thread must be gone before libssh2 is shut down and the DLL can be unloaded.
    try
    {
        InterfaceForFS.CloseAllSessions();
    }
    catch (...)
    {
        TRACE_E("Release: unexpected exception while closing the sessions.");
    }
    try
    {
        SessionLogWindow().Close(); // joins the window thread
    }
    catch (...)
    {
        TRACE_E("Release: unexpected exception while closing the session log window.");
    }
    SetOpTraceSink(nullptr); // every worker thread is gone
    // the F3 copies in Sally's disk cache are of no use without the plugin (the SDK asks for this call)
    SalamanderGeneral->RemoveFilesFromCache((AssignedFSName + L":").c_str());
    SshGlobalExit();
    ReleaseWinLib(DLLInstance);
    return TRUE;
}

// Something changed on 'path' anywhere in Sally (also by this plugin's own operations: they post
// PostChangeOnPathNotification for the folders they change). The F3 copies in the disk cache of every sftp: file are
// dropped. That is more than the changed folder needs, but simple and always correct: a viewed file is downloaded again
// on the next F3. (The cache name also holds size and time, so this is a second safety net, not the only one.)
// Called on the main thread. The call is made for every path of every plugin, so other file systems are ignored.
void WINAPI
CPluginInterface::AcceptChangeOnPathNotification(const wchar_t* path, BOOL includingSubdirs)
{
    CALL_STACK_MESSAGE2("CPluginInterface::AcceptChangeOnPathNotification(, %d)", includingSubdirs);
    try
    {
        const size_t nameLength = AssignedFSName.size();
        if (path == nullptr || wcslen(path) <= nameLength || path[nameLength] != L':' ||
            SalamanderGeneral->StrNICmp(path, AssignedFSName.c_str(), static_cast<int>(nameLength)) != 0)
            return;
        SalamanderGeneral->RemoveFilesFromCache((AssignedFSName + L":").c_str());
    }
    catch (...)
    {
        TRACE_E("AcceptChangeOnPathNotification: unexpected exception.");
    }
}

// A listing is gone from the panel: release its data interface (created in ListCurrentPath).
void WINAPI
CPluginInterface::ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData)
{
    CALL_STACK_MESSAGE1("CPluginInterface::ReleasePluginDataInterface()");
    delete static_cast<CPluginDataInterface*>(pluginData);
}

void WINAPI
CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");
    try
    {
        // The plugin icon: one 16x16 icon with alpha in an icon list that Salamander keeps (it shows it in the Plugins
        // Manager, the Plugins menu, the top toolbar, the Change Drive menu and the Drive bars without loading us).
        // -1 = no icon, when anything below fails.
        int iconIndex = -1;
        CGUIIconListAbstract* iconList = SalamanderGUI != nullptr ? SalamanderGUI->CreateIconList() : nullptr;
        if (iconList != nullptr)
        {
            HICON icon = static_cast<HICON>(LoadImageW(DLLInstance, MAKEINTRESOURCEW(IDI_SFTP_FS), IMAGE_ICON, 16, 16,
                                                       LR_DEFAULTCOLOR));
            if (icon != nullptr && iconList->Create(16, 16, 1) && iconList->ReplaceIcon(0, icon))
            {
                salamander->SetIconListForGUI(iconList); // Salamander owns the list from here on
                iconIndex = 0;
            }
            else
            {
                SalamanderGUI->DestroyIconList(iconList);
            }
            if (icon != nullptr)
                DestroyIcon(icon); // the list holds its own copy
        }
        if (iconIndex >= 0)
        {
            salamander->SetPluginIcon(iconIndex);
            salamander->SetPluginMenuAndToolbarIcon(iconIndex);
        }

        // item in the Change Drive menu and in the Drive bars
        salamander->SetChangeDriveMenuItem(LangStr(IDS_CHNGDRVITEM).c_str(), iconIndex);

        // Plugins / SFTP menu. Keep in step with CPluginInterfaceForMenuExt.
        salamander->AddMenuItem(-1, LangStr(IDS_MENU_CONNECT).c_str(), 0, SftpCmdConnect, FALSE, MENU_EVENT_TRUE,
                                MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
        salamander->AddMenuItem(-1, LangStr(IDS_MENU_DISCONNECT).c_str(), 0, SftpCmdDisconnect, FALSE,
                                MENU_EVENT_THIS_PLUGIN_FS, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
        salamander->AddMenuItem(-1, LangStr(IDS_MENU_OPERATIONS).c_str(), 0, SftpCmdOperations, FALSE,
                                MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
        salamander->AddMenuItem(-1, LangStr(IDS_MENU_SESSION_LOG).c_str(), 0, SftpCmdSessionLog, FALSE,
                                MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
        salamander->AddMenuItem(-1, LangStr(IDS_MENU_CONFIGURATION).c_str(), 0, SftpCmdConfiguration, FALSE,
                                MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
    }
    catch (...)
    {
        TRACE_E("Connect: out of memory.");
    }
}

CPluginInterfaceForMenuExtAbstract* WINAPI
CPluginInterface::GetInterfaceForMenuExt()
{
    return &InterfaceForMenuExt;
}

CPluginInterfaceForFSAbstract* WINAPI
CPluginInterface::GetInterfaceForFS()
{
    return &InterfaceForFS;
}
