// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "menu.h"
#include "ui/IPrompter.h"
#include "common/unicode/helpers.h"
#include "common/IRegistry.h"
#include "common/IFileSystem.h"
#include "common/IShell.h"
#include "common/SalPathWide.h"
#include "common/Win32TextCodec.h"
#include "common/fsutil.h"
#include "drivelst.h"
#include "cfgdlg.h"
#include "dialogs.h"
#include "plugins.h"
#include "fileswnd.h"
#include "mainwnd.h"
#include "shellib.h"
#include "toolbar.h"
#include "shiconov.h"
#include "common/widepath.h"
#include "drivelst_network_drive_entry.h"

CNBWNetAC3Thread NBWNetAC3Thread;

void GetNetworkDrives(DWORD& netDrives, std::wstring* netRemotePaths)
{
    BYTE buffer[10000];
    GetNetworkDrivesBody(netDrives, netRemotePaths, buffer, sizeof(buffer));
}

void GetNetworkDrivesBody(DWORD& netDrives, std::wstring* netRemotePaths, BYTE* buffer, DWORD bufferSize)
{
    CALL_STACK_MESSAGE1("GetNetworkDrives(,)");
    netDrives = 0; // bit array of network drives

    HANDLE hEnumNet;
    DWORD err = WNetOpenEnumW(RESOURCE_REMEMBERED, RESOURCETYPE_DISK,
                              RESOURCEUSAGE_CONNECTABLE, NULL, &hEnumNet);
    if (err == ERROR_SUCCESS)
    {
        NETRESOURCEW* netSources = reinterpret_cast<NETRESOURCEW*>(buffer);
        while (1)
        {
            DWORD e = 0xFFFFFFFF; // as many as possible
            DWORD bufSize = bufferSize;
            err = WNetEnumResourceW(hEnumNet, &e, netSources, &bufSize);
            if (err == ERROR_SUCCESS && e > 0)
            {
                for (DWORD i = 0; i < e; i++)
                {
                    int driveIndex = -1;
                    std::wstring remotePath;
                    if (ExtractNetworkDriveEntryW(netSources[i].lpLocalName,
                                                  netSources[i].lpRemoteName,
                                                  driveIndex, remotePath))
                    {
                        netDrives |= (1 << driveIndex);
                        if (netRemotePaths != NULL)
                            netRemotePaths[driveIndex] = std::move(remotePath);
                    }
                }
            }
            else
                break;
        }
        WNetCloseEnum(hEnumNet);
    }
    else
    {
        if (err != ERROR_NO_NETWORK)
            gPrompter->ShowError(LoadStrW(IDS_NETWORKERROR), GetErrorTextOwned(err).c_str());
    }
}

BOOL GetUserNameW(const wchar_t* drive, const wchar_t* remoteName,
                  std::wstring& userName, std::wstring& providerName)
{
    CALL_STACK_MESSAGE3("GetUserNameW(%ls, %ls, ,)", drive, remoteName);
    if (drive == NULL || remoteName == NULL)
        return FALSE;

    userName.clear();
    providerName.clear();
    IRegistry* registry = gRegistry;
    if (registry == NULL)
        registry = GetWin32Registry();
    if (registry == NULL)
        return FALSE; // well, nothing ...

    HKEY network;
    if (!registry->OpenKeyRead(HKEY_CURRENT_USER, SAL_REG_KEY_NETWORK_W, network).success)
        return FALSE; // well, nothing ...

    BOOL ret = FALSE;
    HKEY driveKey;

    const std::wstring keyName(1, drive[0]);
    if (registry->OpenKeyRead(network, keyName.c_str(), driveKey).success)
    {
        std::wstring remotePath;
        RegistryResult res = registry->GetString(driveKey, SAL_REG_VALUE_REMOTE_PATH_W, remotePath);
        if (res.success && IsTheSamePath(remotePath.c_str(), remoteName))
        {
            ret = TRUE; // we will announce success, even if the user name is not defined (the current user should be used)

            std::wstring savedUserName;
            res = registry->GetString(driveKey, SAL_REG_VALUE_USER_NAME_W, savedUserName);
            if (res.success)
                userName = std::move(savedUserName);
            if (!userName.empty()) // do we have a user?
                TRACE_I("Found saved user name for network drive.");

            std::wstring savedProviderName;
            res = registry->GetString(driveKey, SAL_REG_VALUE_PROVIDER_NAME_W, savedProviderName);
            if (res.success)
                providerName = std::move(savedProviderName);
        }
        registry->CloseKey(driveKey);
    }
    registry->CloseKey(network);

    return ret;
}

struct CNBWNetAC3ThreadFParams
{
    DWORD err;
    NETRESOURCEW netResource;
    LPWSTR lpPassword;
    LPWSTR lpUserName;
    DWORD dwFlags;

    std::wstring userName;
    std::wstring password;
    std::wstring localName;
    std::wstring remoteName;
    std::wstring comment;
    std::wstring provider;

    DWORD errProviderCode;
    std::wstring errBuf;
    std::wstring errProviderName;
};

CNBWNetAC3ThreadFParams NBWNetAC3ThreadFParams;

static void ClearNetworkPassword(std::wstring& password)
{
    if (!password.empty())
        SecureZeroMemory(password.data(), password.size() * sizeof(wchar_t));
    password.clear();
}

static bool GetLastNetworkErrorW(DWORD& providerCode, std::wstring& errorText,
                                 std::wstring& providerName)
{
    std::vector<wchar_t> errorBuffer(256, L'\0');
    std::vector<wchar_t> providerBuffer(256, L'\0');
    for (;;)
    {
        const DWORD result = WNetGetLastErrorW(&providerCode,
                                               errorBuffer.data(), (DWORD)errorBuffer.size(),
                                               providerBuffer.data(), (DWORD)providerBuffer.size());
        if (result == NO_ERROR)
        {
            errorText.assign(errorBuffer.data());
            providerName.assign(providerBuffer.data());
            return true;
        }
        if (result != ERROR_MORE_DATA)
        {
            providerCode = 0;
            errorText.clear();
            providerName.clear();
            return false;
        }
        errorBuffer.resize(errorBuffer.size() * 2, L'\0');
        providerBuffer.resize(providerBuffer.size() * 2, L'\0');
    }
}

DWORD WINAPI NBWNetAC3ThreadF(void* param)
{
    CALL_STACK_MESSAGE_NONE
    CNBWNetAC3ThreadFParams* data = &NBWNetAC3ThreadFParams;
    data->err = WNetAddConnection3W(NULL /* we're not using CONNECT_INTERACTIVE, we're in another thread */,
                                    &data->netResource, data->lpPassword, data->lpUserName, data->dwFlags);
    ClearNetworkPassword(data->password);
    data->lpPassword = NULL;
    if (data->err == ERROR_EXTENDED_ERROR &&
        !GetLastNetworkErrorW(data->errProviderCode, data->errBuf, data->errProviderName))
        data->errProviderCode = 0;
    return 0;
}

BOOL NonBlockingWNetAddConnection3W(DWORD& err, LPNETRESOURCEW lpNetResource,
                                    const wchar_t* lpPassword, const wchar_t* lpUserName, DWORD dwFlags,
                                    DWORD* errProviderCode, std::wstring* errBuf,
                                    std::wstring* errProviderName)
{
    CALL_STACK_MESSAGE3("NonBlockingWNetAddConnection3W(0x%X, , , , 0x%X, , ,)", err, dwFlags);

    // Prevent re-entrance
    static CCriticalSection cs;
    CEnterCriticalSection enterCS(cs);

    err = 0; // for sure
    if (errProviderCode != NULL)
        *errProviderCode = 0;
    if (errBuf != NULL)
        errBuf->clear();
    if (errProviderName != NULL)
        errProviderName->clear();

    // first all we will wait for the previous "calculation" to finish
    GetAsyncKeyState(VK_ESCAPE); // init GetAsyncKeyState - see help
    if (NBWNetAC3Thread.ShutdownArrived)
        return FALSE; // soft has already ended, no further action
    if (NBWNetAC3Thread.Thread != NULL)
    {
        while (1)
        {
            DWORD res = WaitForSingleObject(NBWNetAC3Thread.Thread, 200);
            if (res == WAIT_FAILED)
                return FALSE; // invalid thread handle (closed from outside when ending the soft)
            if (res != WAIT_TIMEOUT)
                break;
            if (UserWantsToCancelSafeWaitWindow())
                return FALSE;
        }
        if (NBWNetAC3Thread.ShutdownArrived)
            return FALSE; // soft has already ended, no further action
        NBWNetAC3Thread.Close();
    }

    // then we will set the parameters and try to start a new thread
    if (lpUserName != NULL)
    {
        NBWNetAC3ThreadFParams.userName = lpUserName;
        NBWNetAC3ThreadFParams.lpUserName = NBWNetAC3ThreadFParams.userName.data();
    }
    else
    {
        NBWNetAC3ThreadFParams.userName.clear();
        NBWNetAC3ThreadFParams.lpUserName = NULL;
    }
    if (lpPassword != NULL)
    {
        NBWNetAC3ThreadFParams.password = lpPassword;
        NBWNetAC3ThreadFParams.lpPassword = NBWNetAC3ThreadFParams.password.data();
    }
    else
    {
        ClearNetworkPassword(NBWNetAC3ThreadFParams.password);
        NBWNetAC3ThreadFParams.lpPassword = NULL;
    }
    NBWNetAC3ThreadFParams.netResource = *lpNetResource;
    if (lpNetResource->lpLocalName != NULL)
    {
        NBWNetAC3ThreadFParams.localName = lpNetResource->lpLocalName;
        NBWNetAC3ThreadFParams.netResource.lpLocalName =
            const_cast<wchar_t*>(NBWNetAC3ThreadFParams.localName.c_str());
    }
    else
        NBWNetAC3ThreadFParams.localName.clear();
    if (lpNetResource->lpRemoteName != NULL)
    {
        NBWNetAC3ThreadFParams.remoteName = lpNetResource->lpRemoteName;
        NBWNetAC3ThreadFParams.netResource.lpRemoteName =
            const_cast<wchar_t*>(NBWNetAC3ThreadFParams.remoteName.c_str());
    }
    else
        NBWNetAC3ThreadFParams.remoteName.clear();
    if (lpNetResource->lpComment != NULL)
    {
        NBWNetAC3ThreadFParams.comment = lpNetResource->lpComment;
        NBWNetAC3ThreadFParams.netResource.lpComment =
            const_cast<wchar_t*>(NBWNetAC3ThreadFParams.comment.c_str());
    }
    else
        NBWNetAC3ThreadFParams.comment.clear();
    if (lpNetResource->lpProvider != NULL)
    {
        NBWNetAC3ThreadFParams.provider = lpNetResource->lpProvider;
        NBWNetAC3ThreadFParams.netResource.lpProvider =
            const_cast<wchar_t*>(NBWNetAC3ThreadFParams.provider.c_str());
    }
    else
        NBWNetAC3ThreadFParams.provider.clear();
    NBWNetAC3ThreadFParams.err = 0;
    NBWNetAC3ThreadFParams.dwFlags = dwFlags;
    NBWNetAC3ThreadFParams.errProviderCode = 0;
    NBWNetAC3ThreadFParams.errBuf.clear();
    NBWNetAC3ThreadFParams.errProviderName.clear();

    DWORD ThreadID;
    NBWNetAC3Thread.Set(HANDLES(CreateThread(NULL, 0, NBWNetAC3ThreadF, NULL, 0, &ThreadID)));
    if (NBWNetAC3Thread.Thread == NULL)
    {
        ClearNetworkPassword(NBWNetAC3ThreadFParams.password);
        NBWNetAC3ThreadFParams.lpPassword = NULL;
        TRACE_E("Unable to start add-net-connection-3 thread.");
        return FALSE; // error (simulation of ESC)
    }

    // we will wait for the "calculation" to finish so that we can return the result
    while (1)
    {
        DWORD res = WaitForSingleObject(NBWNetAC3Thread.Thread, 200);
        if (res == WAIT_FAILED)
            return FALSE; // invalid thread handle (closed from outside when ending the soft)
        if (res != WAIT_TIMEOUT)
            break;
        if (UserWantsToCancelSafeWaitWindow())
            return FALSE;
    }
    if (NBWNetAC3Thread.ShutdownArrived)
        return FALSE; // soft has already ended, no further action
    NBWNetAC3Thread.Close();

    if (errProviderCode != NULL)
        *errProviderCode = NBWNetAC3ThreadFParams.errProviderCode;
    if (errBuf != NULL)
        *errBuf = NBWNetAC3ThreadFParams.errBuf;
    if (errProviderName != NULL)
        *errProviderName = NBWNetAC3ThreadFParams.errProviderName;
    err = NBWNetAC3ThreadFParams.err;
    return TRUE;
}

BOOL IsLogonFailureErr(DWORD err)
{
    return err == ERROR_ACCESS_DENIED || // defense against e.g. "no files found"
           err == ERROR_LOGON_FAILURE ||
           err == ERROR_ACCOUNT_RESTRICTION ||
           err == ERROR_INVALID_LOGON_HOURS ||
           err == ERROR_INVALID_WORKSTATION ||
           err == ERROR_PASSWORD_EXPIRED ||
           err == ERROR_PASSWORD_MUST_CHANGE ||
           err == ERROR_ACCOUNT_DISABLED ||
           err == ERROR_LOGON_NOT_GRANTED ||
           err == ERROR_TRUST_FAILURE ||
           err == ERROR_NOLOGON_INTERDOMAIN_TRUST_ACCOUNT ||
           err == ERROR_NOLOGON_WORKSTATION_TRUST_ACCOUNT ||
           err == ERROR_NOLOGON_SERVER_TRUST_ACCOUNT ||
           err == ERROR_ILL_FORMED_PASSWORD ||
           err == ERROR_INVALID_PASSWORD ||
           err == ERROR_INVALID_PASSWORDNAME ||
           err == ERROR_BAD_USERNAME ||
           err == ERROR_NO_SUCH_USER ||
           err == ERROR_DOWNGRADE_DETECTED ||
           err == ERROR_EXTENDED_ERROR; // who knows what extended error this is, but "password expired" belongs here, so that it's a logon failure (at least user says Cancel)
}

BOOL IsBadUserNameOrPasswdErr(DWORD err)
{
    return err == ERROR_LOGON_FAILURE ||
           err == ERROR_INVALID_PASSWORD ||
           err == ERROR_BAD_USERNAME ||
           err == ERROR_NO_SUCH_USER;
}

BOOL CharIsAllowedInServerName(wchar_t c)
{
    switch (c)
    {
    case '`':
    case '~':
    case '!':
    case '@':
    case '#':
    case '$':
    case '%':
    case '^':
    case '&':
    case '*':
    case '(':
    case ')':
    case '=':
    case '+':
        //    case '_':          //  only warning appears for this character
    case '[':
    case ']':
    case '{':
    case '}':
    case '\\':
    case '|':
    case ';':
    case ':':
    case '.':
    case '\'':
    case '"':
    case ',':
    case '<':
    case '>':
    case '/':
    case '?':
    case ' ':
        return FALSE;
    default:
        return TRUE;
    }
}

BOOL IsAdminShareExtraLogonFailureErrW(DWORD err, const wchar_t* root)
{
    if (err == ERROR_INVALID_NAME && root[0] == L'\\' && root[1] == L'\\')
    {
        const wchar_t* server = root + 2;
        root++;
        while (*++root != 0 && *root != L'\\' && CharIsAllowedInServerName(*root))
            ;             // skip server name + simple test (incomplete, e.g. forbids dots in IPv4 and IPv6), whether it is really an invalid name (in that case we will not ask for username+password)
        if (*root == L'.') // we will take care of IPv4 adresses (let's ignore IPv6)
        {
            const wchar_t* r = root;
            while (*++r != 0 && *r != L'\\')
                ;
            if (r - server < 50)
            {
                const std::wstring ip(server, r);
                std::string ipA;
                if (Win32EncodeAcpExact(ip.c_str(), ipA) &&
                    inet_addr(ipA.c_str()) != INADDR_NONE)
                    root = r; // this is an IP string (aa.bb.cc.dd)
            }
        }
        if (*root == L'\\')
        {
            while (*++root != 0 && *root != L'\\')
                ;
            if (*(root - 1) == L'$')
                return TRUE; // admin share (\\server\share$ or \\server\share$\...)
        }
        else
        {
            if (*root != 0)
                TRACE_I("IsAdminShareExtraLogonFailureErrW: invalid character in server name.");
        }
    }
    return FALSE;
}

typedef struct _CREDUI_INFOW
{
    DWORD cbSize;
    HWND hwndParent;
    PCWSTR pszMessageText;
    PCWSTR pszCaptionText;
    HBITMAP hbmBanner;
} CREDUI_INFOW, *PCREDUI_INFOW;

#ifndef __SECHANDLE_DEFINED__
typedef struct _SecHandle
{
    ULONG_PTR dwLower;
    ULONG_PTR dwUpper;
} SecHandle, *PSecHandle;

#define __SECHANDLE_DEFINED__
#endif // __SECHANDLE_DEFINED__

typedef PSecHandle PCtxtHandle;

typedef WINADVAPI DWORD(WINAPI* FT_CredUIPromptForCredentialsW)(
    PCREDUI_INFOW pUiInfo,
    PCWSTR pszTargetName,
    PCtxtHandle pContext,
    DWORD dwAuthError,
    PWSTR pszUserName,
    ULONG ulUserNameBufferSize,
    PWSTR pszPassword,
    ULONG ulPasswordBufferSize,
    BOOL* save,
    DWORD dwFlags);

typedef WINADVAPI DWORD(WINAPI* FT_CredUIConfirmCredentialsW)(
    PCWSTR pszTargetName,
    BOOL bConfirm);

#define CREDUI_FLAGS_DO_NOT_PERSIST 0x00002      // Do not show "Save" checkbox, and do not persist credentials
#define CREDUI_FLAGS_EXPECT_CONFIRMATION 0x20000 // do not persist unless caller later confirms credential via CredUIConfirmCredential() api
#define CREDUI_FLAGS_GENERIC_CREDENTIALS 0x40000 // Credential is a generic credential

BOOL RestoreNetworkConnectionW(HWND parent, const wchar_t* name, const wchar_t* remoteName, DWORD* retErr,
                               LPNETRESOURCEW lpNetResource)
{
    CALL_STACK_MESSAGE3("RestoreNetworkConnectionW(, %ls, %ls, ,)", name, remoteName);

    BOOL ret = TRUE;
    std::wstring serverName;
    NETRESOURCEW nsBuf = {0};
    NETRESOURCEW* ns = NULL;
    std::wstring userNameStorage;
    const wchar_t* userName = NULL;
    std::wstring providerName;

    if (lpNetResource != NULL)
    {
        if (!Windows7AndLater &&
            lpNetResource->lpRemoteName != NULL && lpNetResource->lpRemoteName[0] == L'\\' &&
            lpNetResource->lpRemoteName[1] == L'\\' && lpNetResource->lpRemoteName[2] != L'\\' &&
            lpNetResource->lpRemoteName[2] != 0)
        {
            wchar_t* end = wcschr(lpNetResource->lpRemoteName + 2, L'\\');
            if (end == NULL || *(end + 1) == 0)
            {
                if (end == NULL)
                    serverName = lpNetResource->lpRemoteName + 2;
                else
                    serverName.assign(lpNetResource->lpRemoteName + 2,
                                      end - (lpNetResource->lpRemoteName + 2));
            }
        }
        if (serverName.empty())
        {
            DWORD err = WNetAddConnection2W(lpNetResource, NULL, NULL, CONNECT_INTERACTIVE);
            if (retErr != NULL)
                *retErr = err;
            return err == NO_ERROR;
        }
        ns = lpNetResource;
        remoteName = ns->lpRemoteName;
    }
    else
    {
        nsBuf.dwType = RESOURCETYPE_DISK;
        nsBuf.lpLocalName = const_cast<wchar_t*>(name);
        nsBuf.lpRemoteName = const_cast<wchar_t*>(remoteName);
        ns = &nsBuf;

        if (remoteName != NULL && remoteName[0] == L'\\' && remoteName[1] == L'\\' && remoteName[2] != L'\\')
        {
            const wchar_t* end = wcschr(remoteName + 2, L'\\');
            if (end != NULL && *(end + 1) != L'\\' && *(end + 1) != 0)
            {
                const wchar_t* last = wcschr(end + 2, L'\\');
                if (last == NULL || *(last + 1) == 0)
                    serverName.assign(remoteName + 2, end - (remoteName + 2));
            }
        }

        std::wstring savedUserName;
        if (name != NULL && GetUserNameW(name, remoteName, savedUserName, providerName))
        {
            if (!savedUserName.empty())
            {
                userNameStorage = savedUserName;
                userName = userNameStorage.c_str();
            }
            if (!providerName.empty())
                nsBuf.lpProvider = const_cast<wchar_t*>(providerName.c_str());
        }
    }

    if (remoteName == NULL)
    {
        if (retErr != NULL)
            *retErr = ERROR_INVALID_PARAMETER;
        return FALSE;
    }

    CEnterPasswdDialog dlg(parent, remoteName, userName);
    DWORD err = ERROR_SUCCESS;
    const wchar_t* passwd = NULL;

    HMODULE credUIDLL = !Windows7AndLater ? HANDLES(LoadLibraryW(L"CREDUI.DLL")) : NULL;
    FT_CredUIPromptForCredentialsW credUIPromptForCredentialsW = NULL;
    FT_CredUIConfirmCredentialsW credUIConfirmCredentialsW = NULL;
    if (credUIDLL != NULL)
    {
        credUIPromptForCredentialsW = (FT_CredUIPromptForCredentialsW)GetProcAddress(credUIDLL, "CredUIPromptForCredentialsW");
        credUIConfirmCredentialsW = (FT_CredUIConfirmCredentialsW)GetProcAddress(credUIDLL, "CredUIConfirmCredentialsW");
    }
    if (!Windows7AndLater &&
        (credUIPromptForCredentialsW == NULL || credUIConfirmCredentialsW == NULL))
    {
        TRACE_E("RestoreNetworkConnectionW(): unable to use CredUIPromptForCredentialsW or CredUIConfirmCredentialsW");
    }

    BOOL connectInteractive = FALSE;
    BOOL confirmCred = FALSE;
    CREDUI_INFOW uiInfo = {0};
    uiInfo.cbSize = sizeof(uiInfo);
    uiInfo.hwndParent = parent;
    std::wstring caption;
    std::wstring message;
    auto clearPassword = [&dlg]() {
        if (dlg.Passwd.capacity() != 0)
        {
            dlg.Passwd.resize(dlg.Passwd.capacity());
            SecureZeroMemory(dlg.Passwd.data(), dlg.Passwd.size() * sizeof(wchar_t));
            dlg.Passwd.clear();
        }
    };
    auto promptForCredentials = [&](BOOL* save, DWORD flags) {
        std::wstring userBuffer(USERNAME_MAXLEN, L'\0');
        std::wstring passwordBuffer(PASSWORD_MAXLEN, L'\0');
        lstrcpynW(userBuffer.data(), dlg.User.c_str(), USERNAME_MAXLEN);
        lstrcpynW(passwordBuffer.data(), dlg.Passwd.c_str(), PASSWORD_MAXLEN);
        clearPassword();
        const DWORD result = credUIPromptForCredentialsW(
            &uiInfo, serverName.c_str(), NULL, 0, userBuffer.data(), USERNAME_MAXLEN,
            passwordBuffer.data(), PASSWORD_MAXLEN, save, flags);
        if (result == NO_ERROR)
        {
            dlg.User = userBuffer.c_str();
            dlg.Passwd = passwordBuffer.c_str();
        }
        SecureZeroMemory(passwordBuffer.data(), passwordBuffer.size() * sizeof(wchar_t));
        return result;
    };

    if (name == NULL)
    {
        if (!Windows7AndLater && credUIPromptForCredentialsW != NULL &&
            credUIConfirmCredentialsW != NULL && !serverName.empty())
        {
            BOOL save = FALSE;
            err = promptForCredentials(&save, CREDUI_FLAGS_EXPECT_CONFIRMATION);
            if (err == ERROR_CANCELLED)
                ret = FALSE;
            else
            {
                if (lpNetResource == NULL && MainWindow != NULL)
                    UpdateWindow(MainWindow->HWindow);
                if (err == NO_ERROR)
                {
                    confirmCred = TRUE;
                    userNameStorage = dlg.User;
                    userName = userNameStorage.c_str();
                    passwd = dlg.Passwd.c_str();
                }
                else
                    connectInteractive = TRUE;
            }
            if (!confirmCred)
                clearPassword();
        }
        else
            connectInteractive = TRUE;
    }
    else if (!Windows7AndLater)
    {
        caption = FormatStrW(LoadStrW(IDS_RECONNET_TITLE), remoteName);
        message = FormatStrW(LoadStrW(IDS_RECONNET_TEXT), remoteName);
        uiInfo.pszMessageText = message.c_str();
        uiInfo.pszCaptionText = caption.c_str();
    }

    while (ret)
    {
        err = ERROR_SUCCESS;
        DWORD errProviderCode = 0;
        std::wstring errBuf;
        std::wstring errProviderName;

        if (connectInteractive)
        {
            err = WNetAddConnection3W(parent, ns, passwd, userName,
                                      CONNECT_INTERACTIVE | (name != NULL ? CONNECT_UPDATE_PROFILE : 0));
            clearPassword();
            passwd = NULL;

            if (err == ERROR_CANCELLED)
            {
                ret = FALSE;
                break;
            }
            if (err == ERROR_EXTENDED_ERROR &&
                !GetLastNetworkErrorW(errProviderCode, errBuf, errProviderName))
                errProviderCode = 0;
        }
        else
        {
            BOOL brk = FALSE;
            HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
            if (lpNetResource != NULL)
            {
                err = WNetAddConnection2W(ns, passwd, userName, 0);
                if (err == ERROR_EXTENDED_ERROR &&
                    !GetLastNetworkErrorW(errProviderCode, errBuf, errProviderName))
                    errProviderCode = 0;
            }
            else
            {
                CreateSafeWaitWindow(LoadStrW(IDS_TRYINGRECONNECTESC), NULL, 3000, TRUE, NULL);
                brk = !NonBlockingWNetAddConnection3W(err, ns, passwd, userName,
                                                       name != NULL ? CONNECT_UPDATE_PROFILE : 0,
                                                       &errProviderCode, &errBuf, &errProviderName);
                DestroySafeWaitWindow();
            }
            SetCursor(oldCur);

            clearPassword();
            passwd = NULL;

            if (confirmCred)
            {
                credUIConfirmCredentialsW(serverName.c_str(), err == ERROR_SUCCESS);
                confirmCred = FALSE;
            }

            if (brk)
            {
                ret = FALSE;
                err = ERROR_CANCELLED;
                break;
            }
        }

        if (err == ERROR_SESSION_CREDENTIAL_CONFLICT)
        {
            if (lpNetResource == NULL)
                gPrompter->ShowError(LoadStrW(IDS_NETWORKERROR), LoadStrW(IDS_CREDENTIALCONFLICT));
            ret = FALSE;
            break;
        }

        if (err == ERROR_ALREADY_ASSIGNED)
        {
            if (MainWindow != NULL && MainWindow->HWindow != NULL)
                PostMessage(MainWindow->HWindow, WM_USER_DRIVES_CHANGE, 0, 0);
            break;
        }

        if (IsLogonFailureErr(err))
        {
            if (err == ERROR_EXTENDED_ERROR && !errBuf.empty())
            {
                std::wstring msg = FormatStrW(L"%s%s(%u) %s", errProviderName.c_str(),
                                              (!errProviderName.empty() ? L": " : L""), errProviderCode, errBuf.c_str());
                gPrompter->ShowError(LoadStrW(IDS_NETWORKERROR), msg.c_str());
            }
            else if (name == NULL || !IsBadUserNameOrPasswdErr(err))
            {
                gPrompter->ShowError(LoadStrW(IDS_NETWORKERROR), GetErrorTextOwned(err).c_str());
            }

            BOOL newConnectInteractive = FALSE;
            if (!Windows7AndLater && credUIPromptForCredentialsW != NULL &&
                credUIConfirmCredentialsW != NULL && !serverName.empty())
            {
                BOOL save = FALSE;
                err = promptForCredentials(
                    &save, name != NULL ? CREDUI_FLAGS_DO_NOT_PERSIST | CREDUI_FLAGS_GENERIC_CREDENTIALS
                                        : CREDUI_FLAGS_EXPECT_CONFIRMATION);
                if (err == ERROR_CANCELLED)
                {
                    ret = FALSE;
                    break;
                }
                if (lpNetResource == NULL && MainWindow != NULL)
                    UpdateWindow(MainWindow->HWindow);
                if (err == NO_ERROR)
                {
                    if (name == NULL)
                        confirmCred = TRUE;
                    userNameStorage = dlg.User;
                    userName = name != NULL && dlg.User.empty() ? NULL : userNameStorage.c_str();
                    passwd = name != NULL && dlg.User.empty() && dlg.Passwd.empty() ? NULL : dlg.Passwd.c_str();
                    continue;
                }
                newConnectInteractive = TRUE;
                clearPassword();
                passwd = NULL;
            }
            else
                newConnectInteractive = TRUE;

            if (newConnectInteractive)
            {
                if (!connectInteractive)
                {
                    connectInteractive = TRUE;
                    continue;
                }
                ret = FALSE;
                break;
            }
        }

        if (err != ERROR_SUCCESS && err != ERROR_DEVICE_ALREADY_REMEMBERED)
        {
            if (lpNetResource == NULL)
                gPrompter->ShowError(LoadStrW(IDS_NETWORKERROR), GetErrorTextOwned(err).c_str());
            ret = FALSE;
            break;
        }

        if (MainWindow != NULL && MainWindow->HWindow != NULL)
            PostMessage(MainWindow->HWindow, WM_USER_DRIVES_CHANGE, 0, 0);
        break;
    }

    clearPassword();
    if (credUIDLL != NULL)
        HANDLES(FreeLibrary(credUIDLL));
    if (retErr != NULL)
        *retErr = err;
    return ret;
}

BOOL CheckAndRestoreNetworkConnection(HWND parent, const char drive, BOOL& pathInvalid)
{
    CALL_STACK_MESSAGE2("CheckAndRestoreNetworkConnection(, %u,)", drive);
    pathInvalid = FALSE;

    DWORD netDrives; // bit array of network drives
    std::wstring netRemotePaths['z' - 'a' + 1];
    GetNetworkDrives(netDrives, netRemotePaths);

    if (netDrives & (1 << (LowerCase[drive] - 'a'))) // disk existed, try to restore
    {
        if (GetLogicalDrives() & (1 << (LowerCase[drive] - 'a'))) // theoretically there is nothing to do, it is accessible ... but on Vista after hibernation, the mapped disk can return the same error over and over again (e.g. "(31) device attached to system is not functioning"), we will discuss it only by accessing the UNC path, probably some MS error, but in Explorer it works, who knows what they are doing there
        {
            const std::wstring nameW = {(wchar_t)(unsigned char)drive, L':', L'\\'};
            DWORD err = NO_ERROR;
            const std::wstring& netPath = netRemotePaths[LowerCase[drive] - 'a'];
            if ((netPath.size() > 2 && netPath[0] == L'\\' && netPath[1] == L'\\' &&
                 netPath.find(L'\\', 2) != std::wstring::npos) && // at least a primitive test of a valid UNC path
                (err = SalCheckPathW(FALSE, nameW.c_str(), ERROR_SUCCESS, TRUE, parent)) != ERROR_SUCCESS && err != ERROR_USER_TERMINATED && // mapped disk is not accessible
                (err = SalCheckPathW(FALSE, netPath.c_str(), ERROR_SUCCESS, TRUE, parent)) == ERROR_SUCCESS && // UNC is accessible
                (err = SalCheckPathW(FALSE, nameW.c_str(), ERROR_SUCCESS, TRUE, parent)) == ERROR_SUCCESS)                           // now the mapped disk is accessible again
            {
                return TRUE;
            }
            pathInvalid = err == ERROR_USER_TERMINATED;
            return FALSE;
        }
        else
        {
            const std::wstring nameW = {(wchar_t)(unsigned char)drive, L':'}; // matches 'name' below exactly - no trailing backslash
            const std::wstring& netPath = netRemotePaths[LowerCase[drive] - 'a'];
            pathInvalid = !RestoreNetworkConnectionW(parent, nameW.c_str(), netPath.c_str());
            return !pathInvalid;
        }
    }
    return FALSE;
}

BOOL CheckAndConnectUNCNetworkPathW(HWND parent, const wchar_t* UNCPath, BOOL& pathInvalid,
                                    BOOL donotReconnect)
{
    CALL_STACK_MESSAGE3("CheckAndConnectUNCNetworkPathW(, %ls, , %d)", UNCPath, donotReconnect);
    pathInvalid = FALSE;
    if (!IsUNCPathW(UNCPath) || UNCPath[2] == L'?')
        return FALSE; // no basic format UNC path

    std::wstring root = GetRootPath(UNCPath);
    if (root.empty())
        return FALSE;
    root.resize(root.length() - 1); // trim the trailing backslash at the end of the root path

    DWORD err = SalCheckPathW(FALSE, root.c_str(), ERROR_SUCCESS, TRUE, parent);
    if (err == ERROR_SUCCESS)
    { // UNC root path is accessible, we will not do anything
    }
    else // UNC root of the path is not listable
    {
        if (err == ERROR_USER_TERMINATED)
        {
            pathInvalid = TRUE;
            return FALSE;
        }
        BOOL trySharepoint = err == ERROR_BAD_NET_NAME; // try to call shell when error 67 occurs, so that it can make the path accessible
        if (!donotReconnect &&
            (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)) // on XP, this error is returned if the user does not have access to the server or the share does not exist on the server, to distinguish these two errors, we call WNetAddConnection3
        {
            NETRESOURCEW ns = {0};
            ns.dwType = RESOURCETYPE_DISK;
            ns.lpLocalName = NULL;
            ns.lpRemoteName = const_cast<wchar_t*>(root.c_str());
            ns.lpProvider = NULL;
            HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
            CreateSafeWaitWindow(LoadStrW(IDS_TRYINGRECONNECTESC), NULL, 3000, TRUE, NULL);
            BOOL connected = NonBlockingWNetAddConnection3W(err, &ns, NULL, NULL, 0, NULL, NULL, NULL);
            DestroySafeWaitWindow();
            SetCursor(oldCur);
            if (!connected)
                err = ERROR_PATH_NOT_FOUND; // ESC
        }

        if (IsLogonFailureErr(err) || // a defense against e.g. "no files found"
            IsAdminShareExtraLogonFailureErrW(err, root.c_str()))
        {
            if (donotReconnect)
                pathInvalid = TRUE; // we report the error directly
            else
            {
                // Petr: I commented out, Explorer or TC do not show any error before displaying the dialog for entering user + password; besides
                //       we now show a new error after trying to establish a connection (so that the user finds out that he has an expired password, etc.)
                //          SalMessageBoxW(parent, GetErrorTextOwned(err).c_str(), LoadStrW(IDS_NETWORKERROR),
                //                        MB_OK | MB_ICONEXCLAMATION);

                pathInvalid = !RestoreNetworkConnectionW(parent, NULL, root.c_str());
                return !pathInvalid;
            }
        }
        else
        {
            if (trySharepoint) // we will try to call shell when error 67 occurs, so that it can make the path accessible
            {
                SHFILEINFOW fi;
                if (SHGetFileInfoW(UNCPath, 0, &fi, sizeof(fi), SHGFI_ATTRIBUTES))
                    return TRUE;
            }
        }
    }
    return FALSE;
}

// ****************************************************************************
// this code is taken from Knowledge Base and serves to obtain information about media type
// of floppy drives (3.5", 5.25", 8")

/*
  GetDriveFormFactor returns the drive form factor.

  It returns 350 if the drive is a 3.5" floppy drive.
  It returns 525 if the drive is a 5.25" floppy drive.
  It returns 800 if the drive is a 8" floppy drive.
  It returns   1 if the drive supports removable media other than 3.5", 5.25", and 8" floppies.
  It returns   0 on error.

  iDrive is 1 for drive A:, 2 for drive B:, etc.
*/

DWORD GetDriveFormFactor(int iDrive)
{
    CALL_STACK_MESSAGE2("GetDriveFormFactor(%d)", iDrive);
    DWORD dwRc = 0;

    /*
     On Windows NT, use the technique described in the Knowledge
     Base article Q115828 and in the "FLOPPY" SDK sample.
  */
    DWORD mediaType = 0;
    if (gFileSystem->QueryDriveMediaType((wchar_t)(L'@' + iDrive), &mediaType).success)
    {
        switch ((MEDIA_TYPE)mediaType)
        {
            case F5_160_512:    // 5.25 160K  floppy
            case F5_180_512:    // 5.25 180K  floppy
            case F5_320_512:    // 5.25 320K  floppy
            case F5_320_1024:   // 5.25 320K  floppy
            case F5_360_512:    // 5.25 360K  floppy
            case F5_640_512:    // 5.25 640K  floppy
            case F5_720_512:    // 5.25 720K  floppy
            case F5_1Pt2_512:   // 5.25 1.2MB floppy
            case F5_1Pt23_1024: // 5.25 1.23MB floppy
                dwRc = 525;
                break;

            case F3_640_512:    // 3.5 640K   floppy
            case F3_720_512:    // 3.5 720K   floppy
            case F3_1Pt2_512:   // 3.5 1.2MB floppy
            case F3_1Pt23_1024: // 3.5 1.23MB floppy
            case F3_1Pt44_512:  // 3.5 1.44MB floppy
            case F3_2Pt88_512:  // 3.5 2.88MB floppy
            case F3_20Pt8_512:  // 3.5 20.8MB floppy
            case F3_32M_512:    // 3.5 32MB   floppy
            case F3_120M_512:   // 3.5 120MB  floppy
            case F3_128Mb_512:  // 3.5 120MB  magneto-optical (MO) media
            case F3_230Mb_512:  // 3.5 230MB  magneto-optical (MO) media
            case F3_200Mb_512:  // 3.5 200MB  floppy (HiFD)
            case F3_240M_512:   // 3.5 240MB  floppy (HiFD)
                dwRc = 350;
                break;

            case F8_256_128: // 8 256K floppy
                dwRc = 800;
                break;

            case RemovableMedia: // removable media other than a floppy disk
                dwRc = 1;
                break;
        }
    }
    return dwRc;
}

// ****************************************************************************

void DisplayMenuAux(IContextMenu2* contextMenu, CMINVOKECOMMANDINFO* ici)
{
    CALL_STACK_MESSAGE_NONE

    // temporary lower the thread priority so that some confused shell extension doesn't eat our CPU
    HANDLE hThread = GetCurrentThread(); // pseudo-handle, no need to release
    int oldThreadPriority = GetThreadPriority(hThread);
    SetThreadPriority(hThread, THREAD_PRIORITY_NORMAL);

    __try
    {
        contextMenu->InvokeCommand(ici);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 1))
    {
        ICExceptionHasOccured++;
    }

    SetThreadPriority(hThread, oldThreadPriority);
}

void DisplayMenuAux2(IContextMenu2* contextMenu, HMENU h)
{
    CALL_STACK_MESSAGE_NONE
    // temporary lower the thread priority so that some confused shell extension doesn't eat our CPU
    HANDLE hThread = GetCurrentThread(); // pseudo-handle, no need to release
    int oldThreadPriority = GetThreadPriority(hThread);
    SetThreadPriority(hThread, THREAD_PRIORITY_NORMAL);

    __try
    {
        UINT flags = CMF_NORMAL | CMF_EXPLORE;
        // we will take care of pressing the shift key - extended context menu, under W2K there is Run as ...
#define CMF_EXTENDEDVERBS 0x00000100 // rarely used verbs
        BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (shiftPressed)
            flags |= CMF_EXTENDEDVERBS;

        contextMenu->QueryContextMenu(h, 0, 0, -1, flags);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 2))
    {
        QCMExceptionHasOccured++;
    }

    SetThreadPriority(hThread, oldThreadPriority);
}

//*****************************************************************************
//
// CDrivesList
//

CDrivesList::CDrivesList(CFilesWindow* filesWindow, const wchar_t* currentPath,
                         CDriveTypeEnum* driveType, DWORD_PTR* driveTypeParam,
                         int* postCmd, void** postCmdParam, BOOL* fromContextMenu)
{
    CALL_STACK_MESSAGE_NONE
    Drives = new TDirectArray<CDriveData>(30, 30);
    MenuPopup = NULL;
    FilesWindow = filesWindow;
    DriveType = driveType;
    DriveTypeParam = driveTypeParam;
    CurrentPath = currentPath != NULL ? currentPath : L"";
    PostCmd = postCmd;
    PostCmdParam = postCmdParam;
    FromContextMenu = fromContextMenu;
    CachedDrivesMask = 0;
    CachedCloudStoragesMask = 0;

    // by default it is not a post-cmd, that is why we zero it
    *PostCmd = 0;
    *PostCmdParam = NULL;
    *FromContextMenu = FALSE;
}

CDriveTypeEnum
CDrivesList::OwnGetDriveType(const wchar_t* rootPath)
{
    CALL_STACK_MESSAGE_NONE
    UINT dt = GetDriveTypeW(rootPath);
    CDriveTypeEnum ret = drvtUnknow;
    switch (dt)
    {
    case DRIVE_REMOVABLE:
        ret = drvtRemovable;
        break;
    case DRIVE_NO_ROOT_DIR: // subst of which the directory was deleted (can also be remote, but we don't care about that)
    case DRIVE_FIXED:
        ret = drvtFixed;
        break;
    case DRIVE_REMOTE:
        ret = drvtRemote;
        break;
    case DRIVE_CDROM:
        ret = drvtCDROM;
        break;
    case DRIVE_RAMDISK:
        ret = drvtRAMDisk;
        break;
    }
    return ret;
}

void GetDisplayNameFromSystem(const wchar_t* root, std::wstring& volumeName)
{
    CALL_STACK_MESSAGE2("GetDisplayNameFromSystem(%ls)", root);

    SHFILEINFOW fi;
    if (SHGetFileInfoW(root, 0, &fi, sizeof(fi), SHGFI_DISPLAYNAME))
    {
        volumeName = fi.szDisplayName;
        const size_t openParen = volumeName.rfind(L'(');
        if (openParen != std::wstring::npos)
        {
            size_t end = openParen;
            while (end > 0 && volumeName[end - 1] == L' ')
                --end;
            volumeName.resize(end);
        }
    }
}

static bool GetVolumeLabelW(const wchar_t* root, std::wstring& volumeName, DWORD* flags = NULL)
{
    DWORD capacity = 256;
    for (;;)
    {
        volumeName.assign(capacity, L'\0');
        DWORD ignored = 0;
        DWORD localFlags = 0;
        if (GetVolumeInformationW(root, volumeName.data(), capacity, NULL, &ignored,
                                  &localFlags, NULL, 0))
        {
            volumeName.resize(wcslen(volumeName.c_str()));
            if (flags != NULL)
                *flags = localFlags;
            return true;
        }
        const DWORD error = GetLastError();
        if (error != ERROR_MORE_DATA && error != ERROR_INSUFFICIENT_BUFFER &&
            error != ERROR_FILENAME_EXCED_RANGE)
        {
            volumeName.clear();
            return false;
        }
        if (capacity > (std::numeric_limits<DWORD>::max)() / 2)
        {
            volumeName.clear();
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return false;
        }
        capacity *= 2;
    }
}

static void DuplicateAmpersands(std::wstring& text)
{
    std::wstring escaped;
    escaped.reserve(text.size());
    for (const wchar_t ch : text)
    {
        escaped.push_back(ch);
        if (ch == L'&')
            escaped.push_back(ch);
    }
    text.swap(escaped);
}

static bool GetNetworkConnectionPathW(const wchar_t* device, std::wstring& remotePath)
{
    DWORD capacity = 256;
    for (;;)
    {
        remotePath.assign(capacity, L'\0');
        DWORD length = capacity;
        const DWORD result = WNetGetConnectionW(device, remotePath.data(), &length);
        if (result == NO_ERROR)
        {
            remotePath.resize(wcslen(remotePath.c_str()));
            return true;
        }
        if (result != ERROR_MORE_DATA)
        {
            remotePath.clear();
            return false;
        }
        capacity = length > capacity ? length : capacity * 2;
    }
}

unsigned ReadCDVolNameThreadFBody(void* param) // directory accessibility test
{
    CALL_STACK_MESSAGE1("ReadCDVolNameThreadFBody()");
    UINT_PTR uid = (UINT_PTR)param;
    std::wstring root;
    std::wstring volumeName;

    HANDLES(EnterCriticalSection(&ReadCDVolNameCS));
    BOOL run = FALSE;
    if (uid == ReadCDVolNameReqUID) // someone is still waiting for an answer
    {
        root = ReadCDVolNameBuffer;
        run = TRUE;
    }
    HANDLES(LeaveCriticalSection(&ReadCDVolNameCS));

    if (run)
    {
        if (!GetVolumeLabelW(root.c_str(), volumeName))
            volumeName.clear();
        if (volumeName.empty())
            GetDisplayNameFromSystem(root.c_str(), volumeName);

        HANDLES(EnterCriticalSection(&ReadCDVolNameCS));
        if (uid == ReadCDVolNameReqUID) // someone is still waiting for an answer
            ReadCDVolNameBuffer = volumeName;
        HANDLES(LeaveCriticalSection(&ReadCDVolNameCS));
    }
    return 0;
}

unsigned ReadCDVolNameThreadFEH(void* param)
{
    CALL_STACK_MESSAGE_NONE
#ifndef CALLSTK_DISABLE
    __try
    {
#endif // CALLSTK_DISABLE
        return ReadCDVolNameThreadFBody(param);
#ifndef CALLSTK_DISABLE
    }
    __except (CCallStack::HandleException(GetExceptionInformation()))
    {
        TRACE_I("Thread ReadCDVolName: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // harder exit (this one still calls something)
        return 1;
    }
#endif // CALLSTK_DISABLE
}

DWORD WINAPI ReadCDVolNameThreadF(void* param)
{
    CALL_STACK_MESSAGE_NONE
#ifndef CALLSTK_DISABLE
    CCallStack stack;
#endif // CALLSTK_DISABLE
    return ReadCDVolNameThreadFEH(param);
}

void SortPluginFSTimes(CPluginFSInterfaceEncapsulation** list, int left, int right)
{
    int i = left, j = right;
    CPluginFSInterfaceEncapsulation* pivot = list[(i + j) / 2];

    do
    {
        while (list[i]->GetPluginFSCreateTime() < pivot->GetPluginFSCreateTime() && i < right)
            i++;
        while (pivot->GetPluginFSCreateTime() < list[j]->GetPluginFSCreateTime() && j > left)
            j--;

        if (i <= j)
        {
            CPluginFSInterfaceEncapsulation* swap = list[i];
            list[i] = list[j];
            list[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    if (left < j)
        SortPluginFSTimes(list, left, j);
    if (i < right)
        SortPluginFSTimes(list, i, right);
}

wchar_t* CreateIndexedDrvText(const wchar_t* driveText, int index)
{
    const size_t capacity = wcslen(driveText) + 15; // reserve 14 characters (" [-1234567890]")
    wchar_t* newText = (wchar_t*)malloc(capacity * sizeof(wchar_t));
    if (newText != NULL)
    {
        const wchar_t* s = driveText;
        while (*s != 0 && *s != L'\t')
            s++;
        if (*s == L'\t')
            s++;
        while (*s != 0 && *s != L'\t')
            s++;
        wmemcpy(newText, driveText, s - driveText);
        // The count is what is LEFT of the allocation from this offset, not the 15
        // extra characters the index needs: the tail 's' is appended here too, and
        // passing 15 truncated (or emptied) every drive whose remaining columns ran
        // past ten characters. Pre-unicode used an unbounded sprintf into the same
        // allocation, so the size is right and only the count was wrong.
        swprintf(newText + (s - driveText), capacity - (size_t)(s - driveText),
                 L" [%d]%s", index, s);
    }
    return newText;
}

int GetIndexForDrvText(CPluginFSInterfaceEncapsulation** fsList, int count,
                       CPluginFSInterfaceAbstract* fsIface, int currentIndex)
{
    CPluginFSInterfaceEncapsulation* fs = NULL;
    int z;
    for (z = 0; z < count; z++) // found encapsulation FS (if there is a lack of memory, the indexes in fsList and Drives (from the firstFSIndex offset) may not match)
    {
        if (fsList[z]->GetInterface() == fsIface)
        {
            fs = fsList[z];
            break;
        }
    }
    if (fs != NULL) // selection of the index for the item (either from FS or we assign sequentially)
    {
        if (fs->GetChngDrvDuplicateItemIndex() < currentIndex)
            fs->SetChngDrvDuplicateItemIndex(currentIndex);
        else
            currentIndex = fs->GetChngDrvDuplicateItemIndex();
    }
    return currentIndex;
}

// based on 'hSrcIcon' creates a black and white version of the icon
// WARNING, SLOW, use with caution
HICON ConvertIcon16x16ToGray(HICON hSrcIcon)
{
    CIconList il;
    if (il.Create(16, 16, 1))
    {
        il.ReplaceIcon(0, hSrcIcon);
        CIconList ilGray;
        if (ilGray.CreateAsCopy(&il, TRUE))
        {
            return ilGray.GetIcon(0, TRUE);
        }
    }
    return NULL;
}

const char Base64Table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// performs in-place decode from base64; returns success;
// except of the state when 'input_length' is zero, returns zero-terminated string
BOOL base64_decode(char* data, int input_length, int* output_length, const char* errPrefix)
{
    char decoding_table[256];
    memset(decoding_table, 0xFF, sizeof(decoding_table));
    for (int i = 0; i < 64; i++)
        decoding_table[(unsigned char)Base64Table[i]] = i;

    if (input_length % 4 != 0)
    {
        TRACE_E(errPrefix << "base64_decode(): invalid length of base64 encoded sequence, unable to decode!");
        return FALSE;
    }

    *output_length = input_length / 4 * 3;
    if (input_length > 0 && data[input_length - 1] == '=')
        (*output_length)--;
    if (input_length > 0 && data[input_length - 2] == '=')
        (*output_length)--;

    for (int i = 0, j = 0; i < input_length;)
    {
        DWORD sextet_a = data[i] == '=' ? 0 & i++ : decoding_table[(unsigned char)data[i++]];
        DWORD sextet_b = data[i] == '=' ? 0 & i++ : decoding_table[(unsigned char)data[i++]];
        DWORD sextet_c = data[i] == '=' ? 0 & i++ : decoding_table[(unsigned char)data[i++]];
        DWORD sextet_d = data[i] == '=' ? 0 & i++ : decoding_table[(unsigned char)data[i++]];

        if (sextet_a == 0xFF || sextet_b == 0xFF || sextet_c == 0xFF || sextet_d == 0xFF)
        {
            TRACE_E(errPrefix << "base64_decode(): invalid base64 encoded sequence, unable to decode!");
            return FALSE;
        }
        DWORD triple = (sextet_a << 3 * 6) + (sextet_b << 2 * 6) + (sextet_c << 1 * 6) + (sextet_d << 0 * 6);

        if (j < *output_length)
            data[j++] = (triple >> 2 * 8) & 0xFF;
        if (j < *output_length)
            data[j++] = (triple >> 1 * 8) & 0xFF;
        if (j < *output_length)
            data[j++] = (triple >> 0 * 8) & 0xFF;
    }
    if (*output_length > 0)
        data[*output_length] = 0;
    return TRUE;
}

// a path to the local Dropbox directory
std::wstring DropboxPath;

void InitDropboxPath()
{
    static BOOL alreadyCalled = FALSE;
    if (!alreadyCalled) // it makes sense to find the path only once, then we just ignore it
    {
        DropboxPath.clear();
        std::wstring sDbPath;
        BOOL cfgAlreadyFound = FALSE;
        IShell* shell = gShell != NULL ? gShell : GetWin32Shell();
        if (shell != NULL && shell->GetKnownFolderPath(FOLDERID_RoamingAppData, sDbPath).success)
        {
            SalPathAppendW(sDbPath, L"Dropbox\\host.db");
            if (gFileSystem->FileExists(sDbPath.c_str()))
                cfgAlreadyFound = TRUE;
        }
        else
            TRACE_E("Cannot get value of CSIDL_APPDATA!");
        if (cfgAlreadyFound || (shell != NULL &&
                                shell->GetKnownFolderPath(FOLDERID_LocalAppData, sDbPath).success))
        {
            if (!cfgAlreadyFound)
                SalPathAppendW(sDbPath, L"Dropbox\\host.db");
            if (cfgAlreadyFound || gFileSystem->FileExists(sDbPath.c_str()))
            {
                HANDLE hFile = gFileSystem->CreateFile(sDbPath.c_str(), GENERIC_READ,
                                                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                                       OPEN_EXISTING,
                                                       FILE_FLAG_SEQUENTIAL_SCAN,
                                                       NULL);
                const DWORD openError = GetLastError();
                HANDLES_ADD_EX(__otQuiet, hFile != INVALID_HANDLE_VALUE, __htFile,
                               __hoCreateFile, hFile, openError, TRUE);
                if (hFile != INVALID_HANDLE_VALUE)
                {
                    uint64_t size = 0;
                    if (gFileSystem->GetHandleFileSize(hFile, &size).success && size < 100000) // 100KB is enough for a stupid config file
                    {
                        char* buf = (char*)malloc((size_t)size);
                        DWORD read = 0;
                        if (buf != NULL &&
                            gFileSystem->ReadFromHandle(hFile, buf, (DWORD)size, &read).success &&
                            read == size)
                        {
                            char* secRow = buf;
                            char* end = buf + read;
                            while (secRow < end && *secRow != '\r' && *secRow != '\n')
                                secRow++;
                            if (secRow < end && *secRow == '\r')
                                secRow++;
                            if (secRow < end && *secRow == '\n')
                                secRow++;
                            char* secRowEnd = secRow;
                            while (secRowEnd < end && *secRowEnd != '\r' && *secRowEnd != '\n')
                                secRowEnd++;
                            if (secRow < secRowEnd) // I have a text 2. rows, where the base64 encoded searched path is
                            {
                                int pathLen;
                                if (base64_decode(secRow, (int)(secRowEnd - secRow), &pathLen, "Dropbox path: "))
                                {
                                    std::wstring widePath;
                                    if (Win32DecodeText(CP_UTF8, secRow, static_cast<size_t>(pathLen), widePath))
                                    {
                                        DropboxPath = std::move(widePath);
                                        TRACE_IW(L"Dropbox path: " << DropboxPath);
                                    }
                                    else
                                        TRACE_E("Dropbox path is not valid UTF-8.");
                                }
                            }
                        }
                        else
                            TRACE_EW(L"Unable to read Dropbox's configuration file: " << sDbPath);
                        free(buf);
                    }
                    else
                        TRACE_EW(L"Dropbox's configuration file is too large: " << sDbPath);
                    HANDLES_REMOVE(hFile, __htFile, "IFileSystem::CloseHandle");
                    gFileSystem->CloseFileHandle(hFile);
                }
                else
                    TRACE_EW(L"Cannot open Dropbox's configuration file: " << sDbPath);
            }
            else
                TRACE_IW(L"Cannot find Dropbox's configuration file: " << sDbPath);
        }
        else
            TRACE_E("Cannot get value of CSIDL_LOCAL_APPDATA!");
    }
    alreadyCalled = TRUE;
}

#define my_DEFINE_KNOWN_FOLDER(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    EXTERN_C const GUID DECLSPEC_SELECTANY name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}

// {A52BBA46-E9E1-435f-B3D9-28DAA648C0F6}  // OneDriver folder from the system, introduced only since Windows 8.1
my_DEFINE_KNOWN_FOLDER(my_FOLDERID_SkyDrive, 0xa52bba46, 0xe9e1, 0x435f, 0xb3, 0xd9, 0x28, 0xda, 0xa6, 0x48, 0xc0, 0xf6);

// the path to the local OneDrive folder - Personal (only for personal accounts, for business accounts we have OneDriveBusinessStorages)
std::wstring OneDrivePath;

// the paths to local OneDrive folders - Business (only for business accounts, for personal accounts we have OneDrivePath)
COneDriveBusinessStorages OneDriveBusinessStorages;

void COneDriveBusinessStorages::SortIn(COneDriveBusinessStorage* s)
{
    if (s != NULL)
    {
        for (int i = 0; i < Count; i++)
        {
            int cmp = _wcsicmp(s->DisplayName.c_str(), At(i)->DisplayName.c_str());
            if (cmp <= 0)
            {
                Insert(i, s);
                return;
            }
        }
        Add(s);
    }
}

BOOL COneDriveBusinessStorages::Find(const wchar_t* displayName, const std::wstring** userFolder)
{
    if (displayName != NULL)
    {
        for (int i = 0; i < Count; i++)
        {
            if (_wcsicmp(displayName, At(i)->DisplayName.c_str()) == 0)
            {
                if (userFolder != NULL)
                    *userFolder = &At(i)->UserFolder;
                return TRUE;
            }
        }
    }
    if (userFolder != NULL)
        *userFolder = NULL;
    return FALSE;
}

void InitOneDrivePath()
{
    OneDrivePath.clear(); // we find out the path to OneDrive over and over again, because after the commend "Unlink OneDrive" (from OneDrive) we should stop showing it

    BOOL done = FALSE;
    if (WindowsVistaAndLater) // SHGetKnownFolderPath has existed since Vista
    {
        IShell* shell = gShell != NULL ? gShell : GetWin32Shell();
        std::wstring path;
        if (shell != NULL && shell->GetKnownFolderPath(my_FOLDERID_SkyDrive, path).success)
        {
            if (!path.empty()) // FOLDERID_SkyDrive was introduced in Windows 8.1 = we should not need to hunt it in the registry
            {
                OneDrivePath = std::move(path);
                done = TRUE;
            }
        }
    }

    IRegistry* registry = gRegistry;
    if (registry == NULL)
        registry = GetWin32Registry();

    HKEY hKey;
    if (registry != NULL)
    {
        for (int i = 0; !done && i < 2; i++) // theoretically only needed for Windows 8 and lower (from 8.1 we have FOLDERID_SkyDrive)
        {
            const wchar_t* oneDriveKey = Windows8_1AndLater && !Windows10AndLater
                                             ? (i == 0 ? SAL_REG_KEY_WIN81_ONEDRIVE_W :
                                                    SAL_REG_KEY_WIN81_SKYDRIVE_W)
                                             : (i == 0 ? SAL_REG_KEY_ONEDRIVE_W : SAL_REG_KEY_SKYDRIVE_W);
            if (registry->OpenKeyRead(HKEY_CURRENT_USER, oneDriveKey, hKey).success)
            {
                std::wstring path;
                RegistryResult result = registry->GetString(hKey, SAL_REG_VALUE_USER_FOLDER_W, path);
                if (result.success && !path.empty())
                {
                    OneDrivePath = std::move(path);
                    done = TRUE;
                }
                registry->CloseKey(hKey);
            }
        }
    }

    // we will load OneDrive Business storages from the registry
    OneDriveBusinessStorages.DestroyMembers();
    if (registry != NULL &&
        registry->OpenKeyRead(HKEY_CURRENT_USER, SAL_REG_KEY_ONEDRIVE_ACCOUNTS_W, hKey).success)
    {
        std::vector<std::wstring> accountKeys;
        if (registry->EnumSubKeys(hKey, accountKeys).success)
        {
            for (size_t i = 0; i < accountKeys.size(); i++)
            {
                const std::wstring& keyName = accountKeys[i];
                if (_wcsicmp(keyName.c_str(), L"Personal") != 0) // we don't want to read personal account here (see OneDrivePath)
                {                                                 // we read all accounts except Personal (for now only "Business???", where ??? is a number)
                    HKEY hAccount;
                    if (registry->OpenKeyRead(hKey, keyName.c_str(), hAccount).success)
                    {
                        std::wstring displayName;
                        RegistryResult result = registry->GetString(
                            hAccount, SAL_REG_VALUE_DISPLAY_NAME_W, displayName);
                        if (result.success && !displayName.empty())
                        {
                            std::wstring pathW;
                            result = registry->GetString(hAccount, SAL_REG_VALUE_USER_FOLDER_W, pathW);
                            if (result.success && !pathW.empty())
                            { // we will collect everything that has DisplayName and UserFolder, no matter what it is, we will offer it to the user under "OneDrive"
                                OneDriveBusinessStorages.SortIn(
                                    new COneDriveBusinessStorage(displayName.c_str(), pathW.c_str()));
                            }
                        }
                        registry->CloseKey(hAccount);
                    }
                }
            }
        }
        registry->CloseKey(hKey);
    }
}

int GetOneDriveStorages()
{
    return (!OneDrivePath.empty() ? 1 : 0) + OneDriveBusinessStorages.Count;
}

void CDrivesList::AddToDrives(CDriveData& drv, int textResId, wchar_t hotkey, CDriveTypeEnum driveType,
                              BOOL getGrayIcons, HICON icon, BOOL destroyIcon, const wchar_t* itemText,
                              const wchar_t* oneDriveDisplayName)
{
    const wchar_t* s = itemText != NULL ? itemText : LoadStrW(textResId);
    wchar_t* txt = (wchar_t*)malloc((1 + (hotkey == 0 ? 0 : 1) + wcslen(s) + 1) * sizeof(wchar_t));
    wcscpy(txt, hotkey == 0 ? L"\t" : L" \t");
    if (hotkey != 0)
        *txt = hotkey;
    wcscat(txt, s);
    drv.DriveType = driveType;
    drv.DriveText = txt;
    drv.OneDriveDisplayName = oneDriveDisplayName != NULL ? DupStr(oneDriveDisplayName) : NULL;
    drv.Param = 0;
    drv.Accessible = TRUE;
    drv.DestroyIcon = destroyIcon;
    drv.HIcon = icon;
    if (getGrayIcons && !destroyIcon)
        TRACE_C("CDrivesList::AddToDrives(): unsupported combination of parameters"); // getGrayIcons is TRUE only for drive-bar and there is only one icon (button)
    if (getGrayIcons)
        drv.HGrayIcon = ConvertIcon16x16ToGray(drv.HIcon);
    else
        drv.HGrayIcon = NULL;
    drv.Shared = FALSE;
    Drives->Add(drv);
}

BOOL CDrivesList::BuildData(BOOL noTimeout, TDirectArray<CDriveData>* copyDrives,
                            DWORD copyCachedDrivesMask, BOOL getGrayIcons, BOOL forDriveBar)
{
    CALL_STACK_MESSAGE4("CDrivesList::BuildData(%d, , , %d, %d)", noTimeout, getGrayIcons, forDriveBar);

    int neighborhoodIndex = -1;
    int currentDiskIndex = -1;
    int currentFSIndex = -1;

    int i = 1;
    char root[10] = " :\\";

    // the lowest bit matches 'A', the second bit 'B', ...
    // available drives
    DWORD mask = GetLogicalDrives();

    CDriveData drv;
    drv.Param = 0;
    drv.DestroyIcon = TRUE; // these icons are allocated
    drv.PluginFS = NULL;    // just for sure
    drv.DLLName = NULL;     // just for sure
    drv.OneDriveDisplayName = NULL;
    drv.HGrayIcon = NULL;

    CDriveData drvSeparator;
    ZeroMemory(&drvSeparator, sizeof(drvSeparator));
    drvSeparator.DriveType = drvtSeparator;

    if (copyDrives != NULL) // optimization: data is just copied (e.g. it's the second Drives Bar, so we copy the data from the first Drives Bar, for which we got it from the system a few ms ago)
    {
        CachedDrivesMask = copyCachedDrivesMask;

        // I will add drives a..z
        int lastDiskDrv = -1;
        int x;
        for (x = 0; x < copyDrives->Count; x++)
        {
            CDriveData* driveData = &copyDrives->At(x);
            if (driveData->DriveType > drvtRAMDisk)
                break;
            if (driveData->DriveType != drvtSeparator)
                lastDiskDrv = x;
        }
        for (x = 0; x <= lastDiskDrv; x++)
        {
            CDriveData* driveData = &copyDrives->At(x);
            if (driveData->DriveType == drvtSeparator)
                Drives->Add(drvSeparator);
            else
            {
                drv.DriveType = driveData->DriveType;
                drv.DriveText = DupStr(driveData->DriveText);
                drv.Param = driveData->Param;
                drv.Accessible = driveData->Accessible;
                drv.Shared = driveData->Shared;
                root[0] = (char)drv.DriveText[0];
                if (root[0] >= 'A' && root[0] <= 'Z')
                    i = 1 << (root[0] - 'A');
                else
                {
                    i = -1;
                    TRACE_E("CDrivesList::BuildData(): unexpected value of drv.DriveText");
                }
                int driveType = (mask & i) ? GetDriveTypeA(root) : DRIVE_REMOTE;
                const std::wstring rootW = {(wchar_t)root[0], L':', L'\\'};
                drv.HIcon = GetDriveIconW(rootW.c_str(), driveType, drv.Accessible);
                drv.HGrayIcon = NULL;

                int index = Drives->Add(drv);
                if (LowerCase[(char)Drives->At(index).DriveText[0]] == LowerCase[(char)*DriveTypeParam])
                    currentDiskIndex = index;
            }
        }
    }
    else
    {
        // remembered and not refreshed network drives will not be returned from GetLogicalDrives()
        DWORD netDrives; // bit array of network disks
        // Wide: this table feeds real display text (volumeName below) - the narrow
        // GetNetworkDrives/WNetEnumResourceA would have Windows itself best-fit-substitute a
        // remote share name outside CP_ACP before this code ever saw it, permanently losing the
        // real character before any re-widening could recover it.
        std::wstring netRemotePaths['z' - 'a' + 1];
        GetNetworkDrives(netDrives, netRemotePaths);

        CachedDrivesMask = mask | netDrives; // a cache for a simple test of whether a disk has been added / disappeared

        // driver which should not be displayed to users
        DWORD noDrivesPolicy = SystemPolicies.GetNoDrives();
        noDrivesPolicy |= DRIVES_MASK & (~Configuration.VisibleDrives);

        char drive = 'A';

        CQuadWord freeSpace; // how much space we have on the disk

        Shares.PrepareSearchW(L""); // now we will search for drives roots

        BOOL separateNextDrive = FALSE; // before we insert drive, should we insert separator?

        // I will add drives a..z
        while (i != 0)
        {
            if (!(noDrivesPolicy & i) && ((mask & i) || (netDrives & i))) // disk is accessible
            {
                root[0] = drive;
                // rootW mirrors 'root' for the wide-only APIs below; 'root' itself stays
                // narrow per the scoping note above - a drive root is
                // always ASCII, so this mirror is exact, not a lossy conversion.
                const std::wstring rootW = {(wchar_t)root[0], L':', L'\\'};
                int driveType;
                if (mask & i)
                {
                    drv.DriveType = OwnGetDriveType(rootW.c_str());
                    driveType = GetDriveTypeA(root);
                }
                else
                {
                    drv.DriveType = drvtRemote;
                    driveType = DRIVE_REMOTE;
                }
                drv.Shared = FALSE;
                drv.Accessible = (mask & i) != 0;
                std::wstring volumeName;
                freeSpace = CQuadWord(-1, -1);
                switch (drv.DriveType)
                {
                case drvtRemovable: // diskettes, we will find out if it is 3.5 ", 5.25", 8" or unknown
                {
                    volumeName.clear();
                    int drvIndex = drive - 'A' + 1;
                    if (drvIndex >= 1 && drvIndex <= 26) // we will do "range-check" for sure
                    {
                        DWORD medium = GetDriveFormFactor(drvIndex);
                        switch (medium)
                        {
                        case 350:
                            volumeName = LoadStrW(IDS_FLOPPY350);
                            break;
                        case 525:
                            volumeName = LoadStrW(IDS_FLOPPY525);
                            break;
                        case 800:
                            volumeName = LoadStrW(IDS_FLOPPY800);
                            break;
                        default:
                        {
                            GetDisplayNameFromSystem(rootW.c_str(), volumeName);
                            if (volumeName.empty())
                                volumeName = LoadStrW(IDS_REMOVABLE_DISK);
                            else
                                DuplicateAmpersands(volumeName);
                            break;
                        }
                        }
                    }
                    break;
                }

                case drvtFixed:
                case drvtRAMDisk:
                {
                    DWORD flags;
                    if (GetVolumeLabelW(rootW.c_str(), volumeName, &flags))
                    {
                        CQuadWord t;                                // total disk space
                        freeSpace = MyGetDiskFreeSpaceW(rootW.c_str(), &t); // free disk space
                        // double '&' so that it is not displayed as an underline
                        DuplicateAmpersands(volumeName);
                        if (volumeName.empty())
                            volumeName = LoadStrW(IDS_LOCAL_DISK);
                    }
                    else
                    {
                        volumeName.clear();
                        freeSpace = CQuadWord(-1, -1);
                    }
                    break;
                }

                case drvtRemote:
                {
                    const std::wstring device = {(wchar_t)drive, L':'};
                    if (netDrives & i)
                    {
                        // The remembered remote path comes straight from WNetEnumResourceW;
                        // no ANSI mirror or fixed path capacity exists in between.
                        volumeName = netRemotePaths[drive - 'A'];
                    }
                    else if (!drv.Accessible ||
                             !GetNetworkConnectionPathW(device.c_str(), volumeName))
                    {
                        if (!GetSubstInformationW(drive - 'A', volumeName))
                            volumeName.clear();
                    }
                    // double '&' so that it is not displayed as an underline
                    DuplicateAmpersands(volumeName);
                    break;
                }

                case drvtCDROM:
                {
                    HANDLES(EnterCriticalSection(&ReadCDVolNameCS));
                    UINT_PTR uid = ++ReadCDVolNameReqUID;
                    ReadCDVolNameBuffer = rootW;
                    HANDLES(LeaveCriticalSection(&ReadCDVolNameCS));

                    // create a thread in which we will find out the volume_name of the CD drive
                    DWORD threadID;
                    HANDLE thread = HANDLES(CreateThread(NULL, 0, ReadCDVolNameThreadF,
                                                         (void*)uid, 0, &threadID));
                    if (thread != NULL && WaitForSingleObject(thread, noTimeout ? INFINITE : 500) == WAIT_OBJECT_0)

                    { // give it 500ms to find out the volume-name
                        HANDLES(EnterCriticalSection(&ReadCDVolNameCS));
                        volumeName = ReadCDVolNameBuffer;
                        HANDLES(LeaveCriticalSection(&ReadCDVolNameCS));
                    }
                    else
                        volumeName.clear();
                    if (thread != NULL)
                        AddAuxThread(thread, TRUE); // if the thread is not finished, we will kill it before closing the software
                    if (volumeName.empty())
                        volumeName = LoadStrW(IDS_COMPACT_DISK);
                    else
                    {
                        // double '&' so that it is not displayed as an underline
                        DuplicateAmpersands(volumeName);
                    }
                    break;
                }

                default:
                    volumeName.clear();
                }
                if (freeSpace != CQuadWord(-1, -1))
                {
                    if (volumeName.empty())
                        volumeName.push_back(L'\t');
                    volumeName.push_back(L'\t');
                    volumeName += PrintDiskSize(freeSpace, 0);
                }
                if (!volumeName.empty())
                { // 'c: ' + volume + 0
                    drv.DriveText = (wchar_t*)malloc((2 + volumeName.size() + 1) * sizeof(wchar_t));
                    if (drv.DriveText == NULL)
                    {
                        TRACE_E(LOW_MEMORY);
                        return FALSE;
                    }
                    wcscpy(drv.DriveText, L" \t");
                    drv.DriveText[0] = (wchar_t)drive;
                    wcscat(drv.DriveText, volumeName.c_str());
                }
                else
                {
                    drv.DriveText = (wchar_t*)malloc(2 * sizeof(wchar_t));
                    if (drv.DriveText == NULL)
                    {
                        TRACE_E(LOW_MEMORY);
                        return FALSE;
                    }
                    wcscpy(drv.DriveText, L" ");
                    drv.DriveText[0] = (wchar_t)drive;
                }
                drv.HIcon = GetDriveIconW(rootW.c_str(), driveType, drv.Accessible);
                drv.HGrayIcon = NULL;

                if (drv.DriveType != drvtRemote)
                {
                    // rootW (built above, next to root[0] = drive) mirrors 'root' exactly.
                    drv.Shared = Shares.SearchW(rootW.c_str());
                }

                // we separate drives that the user wanted to separate
                if (separateNextDrive && Drives->Count > 0 && !IsLastItemSeparator())
                    Drives->Add(drvSeparator);
                // if there's no drive A or B in the system and the user has set the separator after A or B, we've shown it after C
                // that's why we have to drop the flag in any case, not just in the previous condition
                separateNextDrive = FALSE;

                int index = Drives->Add(drv);
                if (LowerCase[(char)Drives->At(index).DriveText[0]] == LowerCase[(char)*DriveTypeParam])
                    currentDiskIndex = index;
            }
            drive++;
            separateNextDrive |= ((i & Configuration.SeparatedDrives) != 0);
            i <<= 1;
        }
    }

    // we will add disconnected and active FS
    drv.Accessible = FALSE;
    drv.Shared = FALSE;
    drv.DLLName = NULL;
    CDetachedFSList* list = MainWindow->DetachedFSList;
    CPluginFSInterfaceEncapsulation** fsList = (CPluginFSInterfaceEncapsulation**)malloc(sizeof(CPluginFSInterfaceEncapsulation*) * (list->Count + 2));
    if (fsList != NULL)
    {
        CPluginFSInterfaceEncapsulation** fsListItem = fsList;
        CPluginFSInterfaceEncapsulation* activePanelFS = NULL;
        if (FilesWindow->Is(ptPluginFS))
        {
            activePanelFS = FilesWindow->GetPluginFS();
            *fsListItem++ = activePanelFS;
        }
        CFilesWindow* otherPanel = MainWindow->LeftPanel == FilesWindow ? MainWindow->RightPanel : MainWindow->LeftPanel;
        CPluginFSInterfaceEncapsulation* nonactivePanelFS = NULL;
        if (otherPanel->Is(ptPluginFS))
        {
            nonactivePanelFS = otherPanel->GetPluginFS();
            *fsListItem++ = nonactivePanelFS;
        }
        for (i = 0; i < list->Count; i++)
            *fsListItem++ = list->At(i);
        int count = (int)(fsListItem - fsList);
        if (count > 1)
            SortPluginFSTimes(fsList, 0, count - 1);

        if (count > 0)
        {
            int firstFSIndex = Drives->Count;
            for (i = 0; i < count; i++)
            {
                CPluginFSInterfaceEncapsulation* fs = fsList[i];
                wchar_t* txt = NULL;
                HICON icon = NULL;
                BOOL destroyIcon = FALSE;
                if (fs->GetChangeDriveOrDisconnectItem(fs->GetPluginFSName(), txt, icon, destroyIcon))
                {
                    drv.DriveText = DupStr(txt);
                    free(txt);
                    drv.HIcon = icon;
                    drv.HGrayIcon = NULL;
                    drv.DestroyIcon = destroyIcon;
                    drv.PluginFS = fs->GetInterface();
                    drv.DriveType = (fs == nonactivePanelFS ? drvtPluginFSInOtherPanel : drvtPluginFS);
                    int index = Drives->Add(drv);
                    if (fs == activePanelFS)
                        currentFSIndex = index; // active FS
                }
            }
            // we will check the uniqueness of the text of the items, possibly duplicate items will be indexed
            for (i = firstFSIndex; i < Drives->Count; i++)
            {
                BOOL freeDriveText = FALSE;
                wchar_t* driveText = Drives->At(i).DriveText;
                int currentIndex = 1;
                int x;
                for (x = i + 1; x < Drives->Count; x++)
                {
                    wchar_t* testedDrvText = Drives->At(x).DriveText;
                    if (StrICmpW(driveText, testedDrvText) == 0) // a match -> we have to index the item
                    {
                        if (!freeDriveText) // first match found, we have to index the first occurrence of a duplicate item as well
                        {
                            currentIndex = GetIndexForDrvText(fsList, count, Drives->At(i).PluginFS, currentIndex);
                            Drives->At(i).DriveText = CreateIndexedDrvText(driveText, currentIndex++);
                            if (Drives->At(i).DriveText == NULL)
                                Drives->At(i).DriveText = driveText;
                            else
                                freeDriveText = TRUE;
                        }
                        currentIndex = GetIndexForDrvText(fsList, count, Drives->At(x).PluginFS, currentIndex);
                        Drives->At(x).DriveText = CreateIndexedDrvText(testedDrvText, currentIndex++);
                        if (Drives->At(x).DriveText == NULL)
                            Drives->At(x).DriveText = testedDrvText;
                        else
                            free(testedDrvText);
                    }
                }
                if (freeDriveText)
                    free(driveText);
            }
        }
        free(fsList);
    }
    drv.PluginFS = NULL; // just for sure
    drv.DLLName = NULL;  // just for sure
    int iconSize = GetIconSizeForSystemDPI(ICONSIZE_16);

    // I will add the separator if it is not the first item and if there is no separator yet
    if (Drives->Count > 0 && !IsLastItemSeparator())
        Drives->Add(drvSeparator);

    // adding Documents
    if (Configuration.ChangeDriveShowMyDoc)
    {
        AddToDrives(drv, IDS_MYDOCUMENTS, ';', drvtMyDocuments, getGrayIcons,
                    SalLoadIcon(ImageResDLL, 112, iconSize));
    }

    // adding Cloud Storages (Google Drive, etc.), if I find any...
    CachedCloudStoragesMask = 0;
    if (Configuration.ChangeDriveCloudStorage)
    {
        CSQLite3DynLoadBase* sqlite3_Dyn_InOut = NULL; // I somewhat expected that sqlite3 will be needed for Dropbox, too, which eventually did not happen (so this is here just in case)
        ShellIconOverlays.InitGoogleDrivePath(&sqlite3_Dyn_InOut, TRUE);
        if (ShellIconOverlays.HasGoogleDrivePath())
        {
            CachedCloudStoragesMask |= 0x01 /* Google Drive */;
            AddToDrives(drv, IDS_GOOGLEDRIVE, 0, drvtGoogleDrive, getGrayIcons,
                        SalLoadIcon(HInstance, IDI_GOOGLEDRIVE, iconSize));
        }

        InitDropboxPath();
        if (!DropboxPath.empty())
        {
            CachedCloudStoragesMask |= 0x02 /* Dropbox */;
            AddToDrives(drv, IDS_DROPBOX, 0, drvtDropbox, getGrayIcons,
                        SalLoadIcon(HInstance, IDI_DROPBOX, iconSize));
        }

        InitOneDrivePath();
        int c = GetOneDriveStorages();
        if (c == 1 && !OneDrivePath.empty())
            CachedCloudStoragesMask |= 0x04 /* only one OneDrive storage - Personal */;
        if (c == 1 && OneDrivePath.empty())
            CachedCloudStoragesMask |= 0x08 /* only one OneDrive storage - Business */;
        if (c > 1)
            CachedCloudStoragesMask |= 0x10 /* more OneDrive storages - drop down menu on drive-bar */;
        HICON oneDriveIco = c == 0 ? NULL : SalLoadIcon(HInstance, IDI_ONEDRIVE, iconSize);
        BOOL destroyOneDriveIco = oneDriveIco != NULL;
        if (forDriveBar && c > 1) // data for drive-bar && more storages = let the user choose from the menu (drop down)
        {
            const std::wstring itemText = LoadStrW(IDS_ONEDRIVE);
            AddToDrives(drv, 0, 0, drvtOneDriveMenu, getGrayIcons, oneDriveIco, destroyOneDriveIco, itemText.c_str());
            destroyOneDriveIco = FALSE;
        }
        else // data for change drive menu || drive-bar && the only storage (we give a simple button on the drive-bar)
        {
            if (!OneDrivePath.empty()) // personal
            {
                std::wstring itemText;
                if (c == 1)
                    itemText = LoadStrW(IDS_ONEDRIVE); // the only personal storage = we write only: OneDrive
                else
                    itemText = FormatStrW(L"%s - %s", LoadStrW(IDS_ONEDRIVE), LoadStrW(IDS_ONEDRIVEPERSONAL));
                AddToDrives(drv, 0, 0, drvtOneDrive, getGrayIcons, oneDriveIco, destroyOneDriveIco, itemText.c_str());
                destroyOneDriveIco = FALSE;
            }
            for (int i = 0; i < OneDriveBusinessStorages.Count; i++) // business
            {
                const std::wstring itemText = FormatStrW(L"%s - %s", LoadStrW(IDS_ONEDRIVE), OneDriveBusinessStorages[i]->DisplayName.c_str());
                AddToDrives(drv, 0, 0, drvtOneDriveBus, getGrayIcons, oneDriveIco, destroyOneDriveIco,
                            itemText.c_str(), OneDriveBusinessStorages[i]->DisplayName.c_str());
                destroyOneDriveIco = FALSE;
            }
        }
        if (destroyOneDriveIco)
            TRACE_C("CDrivesList::BuildData(): OneDrive icon unused, should never happen");

        if (sqlite3_Dyn_InOut != NULL)
            delete sqlite3_Dyn_InOut; // release sqlite.dll which is no longer needed
    }

    // adding Network Neighborhood
    CPluginData* nethoodPlugin = NULL;
    BOOL existsNethoodPlugin = Plugins.GetFirstNethoodPluginFSName(NULL, &nethoodPlugin);
    if (!SystemPolicies.GetNoNetHood() && Configuration.ChangeDriveShowNet &&
        !existsNethoodPlugin)
    {
        AddToDrives(drv, IDS_NETWORKDRIVE, ',', drvtNeighborhood, getGrayIcons,
                    SalLoadIcon(ImageResDLL, 152, iconSize));
        neighborhoodIndex = Drives->Count - 1;
    }

    // adding FS commands from all plug-ins
    if (!Plugins.AddItemsToChangeDrvMenu(this, currentFSIndex,
                                         FilesWindow->GetPluginFS()->GetPluginInterfaceForFS()->GetInterface(),
                                         getGrayIcons))
    {
        return FALSE;
    }

    // finding the index of the Nethood plug-in and setting it to neighborhoodIndex (we represent the Network item with everything)
    if (existsNethoodPlugin && nethoodPlugin != NULL && neighborhoodIndex == -1)
    {
        int i2;
        for (i2 = 0; i2 < Drives->Count; i2++)
        {
            CDriveData* item = &Drives->At(i2);
            if (item->DriveType == drvtPluginCmd && nethoodPlugin->DLLName.c_str() == item->DLLName)
            {
                neighborhoodIndex = i2;
                break;
            }
        }
    }

    // determining the active item
    FocusIndex = -1;
    if (FilesWindow->Is(ptPluginFS))
    {
        if (currentFSIndex != -1)
            FocusIndex = currentFSIndex;
    }
    else
    {
        if (!CurrentPath.empty()) // only if we have a path (it's not ptPluginFS)
        {
            if (currentDiskIndex != -1)
                FocusIndex = currentDiskIndex;
            if (FocusIndex == -1 && neighborhoodIndex != -1)
                FocusIndex = neighborhoodIndex;
        }
    }

    // adding Another Panel
    if (Configuration.ChangeDriveShowAnother)
    {
        // a variant with a string 'Another Panel Path' ('As Another Panel'?)
        const wchar_t* s = LoadStrW(IDS_ANOTHERPANEL);
        {
            drv.DriveType = drvtOtherPanel;
            drv.DriveText = (wchar_t*)malloc((2 + wcslen(s) + 1) * sizeof(wchar_t));
            if (drv.DriveText == NULL)
            {
                TRACE_E(LOW_MEMORY);
                return FALSE;
            }
            wcscpy(drv.DriveText, L".\t");
            wcscat(drv.DriveText, s);
            drv.Accessible = TRUE;

            CFilesWindow* panel = MainWindow->GetNonActivePanel();
            if (panel->Is(ptDisk))
            {
                // From the panel's wide path. GetDriveType inspects the ROOT, and for
                // a UNC path that root is "\\server\share" - a name the active code page may not be
                // able to spell, in which case the narrow mirror asked about a path that does not
                // exist and the drive came back DRIVE_NO_ROOT_DIR.
                UINT type = MyGetDriveTypeW(panel->GetPathW());
                const std::wstring root2 = GetRootPath(panel->GetPathW());
                drv.HIcon = GetDriveIconW(root2.c_str(), type, TRUE);
                drv.HGrayIcon = NULL;
                drv.DestroyIcon = TRUE; // these icons are allocated
            }
            else
            {
                if (panel->Is(ptZIPArchive))
                {
                    drv.HIcon = SalLoadIcon(ImageResDLL, 174, iconSize);
                    drv.HGrayIcon = NULL;
                    drv.DestroyIcon = TRUE; // this icon is allocated
                }
                else
                {
                    if (panel->Is(ptPluginFS))
                    {
                        BOOL destroyIcon;
                        HICON icon = panel->GetPluginFS()->GetFSIcon(destroyIcon);
                        if (icon != NULL) // defined by a plugin
                        {
                            drv.HIcon = icon;
                            drv.HGrayIcon = NULL;
                            drv.DestroyIcon = destroyIcon; // we are driven by the plugin
                        }
                        else // standard
                        {
                            drv.HIcon = SalLoadIcon(HInstance, IDI_PLUGINFS, iconSize);
                            drv.HGrayIcon = NULL;
                            drv.DestroyIcon = TRUE; // this icon is allocated
                        }
                    }
                }
            }
            drv.Shared = FALSE;
            Drives->Add(drv);
        }
    }

    // loading hot paths
    drv.DestroyIcon = FALSE;
    drv.HIcon = HFavoritIcon;
    drv.HGrayIcon = NULL;
    BOOL addSeparator = (Drives->Count > 0 && !IsLastItemSeparator()); // wed do not want two separators in a row
    for (i = 0; i < HOT_PATHS_COUNT; i++)
    {
        if (MainWindow->HotPaths.GetVisible(i))
        {
            const std::wstring srcName = MainWindow->HotPaths.GetNameW(i);
            if (!srcName.empty() && MainWindow->HotPaths.GetPathLen(i) > 0)
            {
                if (addSeparator)
                {
                    // adding separator
                    Drives->Add(drvSeparator);
                    addSeparator = FALSE;
                }
                std::wstring text;
                if (i < 10)
                    text = std::to_wstring(i == 9 ? 0 : i + 1) + L"\t" + srcName;
                else
                    text = L"\t" + srcName;
                // double '&' so that it is not displayed as an underline
                DuplicateAmpersands(text);

                drv.DriveType = drvtHotPath;
                drv.Param = i;
                drv.DriveText = DupStr(text.c_str());
                drv.Accessible = TRUE;
                drv.Shared = FALSE;
                Drives->Add(drv);
                drv.DestroyIcon = FALSE; // I won't clean up other icons anymore
            }
        }
    }

    // we don't want a separator at the end
    if (Drives->Count > 0 && IsLastItemSeparator())
        Drives->Delete(Drives->Count - 1);

    if (drv.DestroyIcon) // no hot path, we have to remove the icon here
    {
        HANDLES(DestroyIcon(drv.HIcon));
    }
    return TRUE;
}

void CDrivesList::DestroyDrives(TDirectArray<CDriveData>* drives)
{
    CALL_STACK_MESSAGE1("CDrivesList::DestroyDrives()");
    int i;
    for (i = 0; i < drives->Count; i++)
    {
        if (drives->At(i).DriveType != drvtSeparator)
        {
            free(drives->At(i).DriveText);
            drives->At(i).DriveText = NULL;
            if (drives->At(i).DriveType == drvtOneDriveBus)
            {
                free(drives->At(i).OneDriveDisplayName);
                drives->At(i).OneDriveDisplayName = NULL;
            }
            if (drives->At(i).DestroyIcon && drives->At(i).HIcon != NULL)
            {
                HANDLES(DestroyIcon(drives->At(i).HIcon)); // via GetDriveIcon
                drives->At(i).HIcon = NULL;
            }
            if (drives->At(i).DestroyIcon && drives->At(i).HGrayIcon != NULL)
            {
                HANDLES(DestroyIcon(drives->At(i).HGrayIcon));
                drives->At(i).HGrayIcon = NULL;
            }
        }
    }
    drives->DetachMembers();
}

void CDrivesList::DestroyData()
{
    DestroyDrives(Drives);
}

BOOL CDrivesList::LoadMenuFromData()
{
    CALL_STACK_MESSAGE1("CDrivesList::LoadMenuFromData()");
    MENU_ITEM_INFO mii;
    int i;
    for (i = 0; i < Drives->Count; i++)
    {
        CDriveData* item = &Drives->At(i);
        mii.ID = i + 1;
        if (item->DriveType == drvtSeparator)
        {
            mii.Mask = MENU_MASK_TYPE;
            mii.Type = MENU_TYPE_SEPARATOR;
        }
        else
        {
            mii.Mask = MENU_MASK_TYPE | MENU_MASK_ID | MENU_MASK_ICON | MENU_MASK_OVERLAY |
                       MENU_MASK_STATE | MENU_MASK_STRING;
            mii.Type = MENU_TYPE_STRING;
            mii.HIcon = item->HIcon;
            mii.HOverlay = NULL;
            if (item->Shared)
                mii.HOverlay = HSharedOverlays[ICONSIZE_16];
            mii.String = item->DriveText;
            mii.State = 0;
            if (i == FocusIndex) // if FocusIndex==-1, nothing is marked
                mii.State = MENU_STATE_CHECKED;
            if (item->DriveType == drvtPluginFSInOtherPanel)
                mii.State |= MFS_DISABLED | MFS_GRAYED;
        }
        if (!MenuPopup->InsertItem(i, TRUE, &mii))
            return FALSE;
    }
    return TRUE;
}

BOOL CDrivesList::ExecuteItem(int index, HWND hwnd, const RECT* exclude, BOOL* fromDropDown)
{
    if (fromDropDown != NULL)
        *fromDropDown = FALSE;
    if (index < 0 || index >= Drives->Count)
    {
        TRACE_E("index=" << index);
        return FALSE;
    }
    BOOL ret = TRUE;
    CDriveData* item = &Drives->At(index);
    // transfer outside
    CDriveTypeEnum dt = item->DriveType;
    *DriveType = dt;
    switch (dt)
    {
    case drvtOneDriveBus:
    {
        if (item->OneDriveDisplayName != NULL)
            *DriveTypeParam = (DWORD_PTR)DupStr(item->OneDriveDisplayName);
        else
        {
            TRACE_C("CDrivesList::ExecuteItem(): OneDrive identity is missing");
            ret = FALSE;
        }
        break;
    }

    case drvtOneDriveMenu: // opening the drop down menu for OneDrive storage selection
    {
        if (fromDropDown != NULL)
            *fromDropDown = TRUE;
        InitOneDrivePath();
        int c = GetOneDriveStorages();
        if (c <= 1)
        { // OneDrive is not in drop down anymore, we will refresh both Drive bars, so that the icons disappear or update
            if (MainWindow != NULL && MainWindow->HWindow != NULL)
                PostMessage(MainWindow->HWindow, WM_USER_DRIVES_CHANGE, 0, 0);
            ret = FALSE;
        }
        else
        {
            CMenuPopup menu;
            MENU_ITEM_INFO mii;
            mii.Mask = MENU_MASK_TYPE | MENU_MASK_STRING | MENU_MASK_ID;
            mii.Type = MENU_TYPE_STRING;

            if (!OneDrivePath.empty()) // personal
            {
                const std::wstring itemText = FormatStrW(L"%s - %s", LoadStrW(IDS_ONEDRIVE), LoadStrW(IDS_ONEDRIVEPERSONAL));
                mii.String = const_cast<wchar_t*>(itemText.c_str());
                mii.ID = 1;
                menu.InsertItem(-1, TRUE, &mii);
            }
            for (int i = 0; i < OneDriveBusinessStorages.Count; i++) // business
            {
                const std::wstring itemText = FormatStrW(L"%s - %s", LoadStrW(IDS_ONEDRIVE), OneDriveBusinessStorages[i]->DisplayName.c_str());
                mii.String = const_cast<wchar_t*>(itemText.c_str());
                mii.ID = i + 2;
                menu.InsertItem(-1, TRUE, &mii);
            }

            int cmd = menu.Track(MENU_TRACK_RETURNCMD, exclude->left, exclude->bottom, hwnd, exclude);
            if (cmd > 0)
            {
                if (cmd == 1)
                    *DriveType = drvtOneDrive;
                else
                {
                    const int storageIndex = cmd - 2;
                    if (storageIndex >= 0 && storageIndex < OneDriveBusinessStorages.Count)
                    {
                        *DriveType = drvtOneDriveBus;
                        *DriveTypeParam = (DWORD_PTR)DupStr(OneDriveBusinessStorages[storageIndex]->DisplayName.c_str());
                    }
                    else
                        ret = FALSE;
                }
            }
            else
                ret = FALSE;
        }
        break;
    }

    case drvtHotPath:
    {
        *DriveTypeParam = item->Param;
        break;
    }

    case drvtRemovable:
    case drvtFixed:
    case drvtRemote:
    case drvtCDROM:
    case drvtRAMDisk:
    {
        *DriveTypeParam = (DWORD_PTR)item->DriveText[0];

        // try to revive
        if (!item->Accessible)
        {
            const std::wstring name = {item->DriveText[0], L':'};
            if (!RestoreNetworkConnectionW(FilesWindow->HWindow, name.c_str(), item->DriveText + 2))
                ret = FALSE;
        }

        break;
    }

    case drvtPluginFS:             // a plug-in item: opened FS (active/disconnected)
    case drvtPluginFSInOtherPanel: // this should never come, we will stop it later (no action)
    {
        *DriveTypeParam = (DWORD_PTR)item->PluginFS;
        break;
    }

    case drvtPluginCmd: // a plug-in item: FS command
    {
        *DriveTypeParam = (DWORD_PTR)item->DLLName;
        break;
    }
    }
    return ret;
}

BOOL CDrivesList::Track()
{
    CALL_STACK_MESSAGE1("CDrivesList::Track()");
    RECT r;
    GetWindowRect(FilesWindow->HWindow, &r);
    int dirHeight = MainWindow->GetDirectoryLineHeight();

    RECT buttonRect;
    buttonRect = r;
    buttonRect.bottom = buttonRect.top + dirHeight;
    buttonRect.right = buttonRect.left + dirHeight;

    MenuPopup = new CMenuPopup;
    if (MenuPopup == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }
    MenuPopup->SetStyle(MENU_POPUP_THREECOLUMNS);
    if (!BuildData(FALSE))
    {
        delete MenuPopup;
        MenuPopup = NULL;
        return FALSE;
    }

    // synchronize drive bars, if needed
    MainWindow->RebuildDriveBarsIfNeeded(TRUE, GetCachedDrivesMask(), // to speed up, we will use our cached masks
                                         TRUE, GetCachedCloudStoragesMask());

    if (!LoadMenuFromData())
    {
        DestroyData();
        delete MenuPopup;
        MenuPopup = NULL;
        return FALSE;
    }

    if (FocusIndex != -1)
        MenuPopup->SetSelectedItemIndex(FocusIndex);
    FilesWindow->OpenedDrivesList = this; // so that FilesWindow deliver us notifications
    DWORD cmd = MenuPopup->Track(MENU_TRACK_RETURNCMD | MENU_TRACK_SELECT | MENU_TRACK_VERTICAL,
                                 buttonRect.left, buttonRect.bottom,
                                 FilesWindow->HWindow, &buttonRect);
    FilesWindow->OpenedDrivesList = NULL;

    // let the returning variable be set
    if (cmd != 0)
        if (!ExecuteItem(cmd - 1, NULL, NULL, NULL))
            cmd = 0;

    DestroyData();
    delete MenuPopup;
    MenuPopup = NULL;

    // synchronize drive bars once more (it could have changed during the menu)
    MainWindow->RebuildDriveBarsIfNeeded(TRUE, GetCachedDrivesMask(), // to speed up, we will use our cached masks
                                         TRUE, GetCachedCloudStoragesMask());

    return cmd != 0;
}

BOOL IncludeDriveInDriveBar(CDriveTypeEnum dt)
{
    switch (dt)
    {
    case drvtUnknow:
    case drvtRemovable:
    case drvtFixed:
    case drvtRemote:
    case drvtCDROM:
    case drvtRAMDisk:
    case drvtMyDocuments:
    case drvtGoogleDrive:
    case drvtDropbox:
    case drvtOneDrive:
    case drvtOneDriveBus:
    case drvtOneDriveMenu:
    case drvtNeighborhood:
    case drvtPluginCmd:
        return TRUE;
    }
    return FALSE; // we don't want fs, ...
}

BOOL CDrivesList::FillDriveBar(CDriveBar* driveBar, BOOL bar2)
{
    driveBar->DestroyImageLists();
    int iconSize = GetIconSizeForSystemDPI(ICONSIZE_16);
    driveBar->HDrivesIcons = ImageList_Create(iconSize, iconSize, GetImageListColorFlags() | ILC_MASK, 0, 1);
    driveBar->HDrivesIconsGray = ImageList_Create(iconSize, iconSize, GetImageListColorFlags() | ILC_MASK, 0, 1);

    BOOL insertSeparator = FALSE;
    int imageIndex = 0;
    int i;
    for (i = 0; i < Drives->Count; i++)
    {
        CDriveData* item = &Drives->At(i);
        if (!IncludeDriveInDriveBar(item->DriveType))
        {
            if (item->DriveType == drvtSeparator)
                insertSeparator = TRUE;
            continue;
        }

        if (insertSeparator)
        {
            TLBI_ITEM_INFO2 tii;
            tii.Mask = TLBI_MASK_STYLE;
            tii.Style = TLBI_STYLE_SEPARATOR;
            driveBar->InsertItem2(0xFFFFFFFF, TRUE, &tii);
            insertSeparator = FALSE;
        }

        std::wstring text;
        TLBI_ITEM_INFO2 tii;
        tii.Mask = TLBI_MASK_STYLE | TLBI_MASK_IMAGEINDEX | TLBI_MASK_OVERLAY | TLBI_MASK_ID;
        tii.Style = item->DriveType == drvtOneDriveMenu ? TLBI_STYLE_WHOLEDROPDOWN | TLBI_STYLE_DROPDOWN : TLBI_STYLE_NOPREFIX;
        if (item->DriveType != drvtMyDocuments && item->DriveType != drvtNeighborhood && item->DriveType != drvtPluginCmd &&
            item->DriveType != drvtGoogleDrive && item->DriveType != drvtDropbox && item->DriveType != drvtOneDrive &&
            item->DriveType != drvtOneDriveBus && item->DriveType != drvtOneDriveMenu)
        {
            tii.Mask |= TLBI_MASK_TEXT;
            tii.Style |= TLBI_STYLE_SHOWTEXT;
            text.assign(1, item->DriveText[0]);
            tii.Text = text.data();
        }
        ImageList_AddIcon(driveBar->HDrivesIcons, item->HIcon);
        ImageList_AddIcon(driveBar->HDrivesIconsGray, item->HGrayIcon == NULL ? item->HIcon : item->HGrayIcon);
        tii.ImageIndex = imageIndex++;
        tii.HOverlay = NULL;
        if (item->Shared)
            tii.HOverlay = HSharedOverlays[ICONSIZE_16];
        tii.ID = (bar2 ? CM_DRIVEBAR2_MIN : CM_DRIVEBAR_MIN) + i;
        driveBar->InsertItem2(0xFFFFFFFF, TRUE, &tii);
    }

    driveBar->SetImageList(driveBar->HDrivesIconsGray);
    driveBar->SetHotImageList(driveBar->HDrivesIcons);

    return TRUE;
}

BOOL CDrivesList::GetDriveBarToolTip(int index, wchar_t* text)
{
    if (index < 0 || index >= Drives->Count)
    {
        TRACE_E("index=" << index);
        return FALSE;
    }
    BOOL ret = TRUE;

    text[0] = 0;

    std::wstring volumeName;
    CQuadWord freeSpace;
    char root[10] = " :\\";
    // rootW mirrors 'root' for the wide-only APIs below, same idiom as BuildData()
    // above - a drive root is always ASCII, so this mirror is exact.
    std::wstring rootW;

    CDriveData* item = &Drives->At(index);
    switch (item->DriveType)
    {
    case drvtRemovable: // diskettes, we will find out if it is 3.5", 5.25", 8" or unknown
    {
        root[0] = (char)item->DriveText[0];
        rootW = {(wchar_t)root[0], L':', L'\\'};
        volumeName.clear();
        int drv = item->DriveText[0] - 'A' + 1;
        if (drv >= 1 && drv <= 26) // we will do "range-check" for sure
        {
            DWORD medium = GetDriveFormFactor(drv);
            switch (medium)
            {
            case 350:
                volumeName = LoadStrW(IDS_FLOPPY350);
                break;
            case 525:
                volumeName = LoadStrW(IDS_FLOPPY525);
                break;
            case 800:
                volumeName = LoadStrW(IDS_FLOPPY800);
                break;
            default:
            {
                GetDisplayNameFromSystem(rootW.c_str(), volumeName);
                if (volumeName.empty())
                    volumeName = LoadStrW(IDS_REMOVABLE_DISK);

                break;
            }
            }
        }
        lstrcpynW(text, volumeName.c_str(), TOOLTIP_TEXT_MAX);
        break;
    }

    case drvtFixed:
    case drvtRAMDisk:
    {
        root[0] = (char)item->DriveText[0];
        rootW = {(wchar_t)root[0], L':', L'\\'};
        DWORD flags;
        if (GetVolumeLabelW(rootW.c_str(), volumeName, &flags))
        {
            CQuadWord t;                                // total disk space
            freeSpace = MyGetDiskFreeSpaceW(rootW.c_str(), &t); // free disk space
            const std::wstring freeSpaceText = PrintDiskSize(freeSpace, 0);
            if (volumeName.empty())
                volumeName = LoadStrW(IDS_LOCAL_DISK);
            const std::wstring tooltip = FormatStrW(L"%s (%s)", volumeName.c_str(), freeSpaceText.c_str());
            lstrcpynW(text, tooltip.c_str(), TOOLTIP_TEXT_MAX);
        }
        break;
    }

    case drvtCDROM:
    {
        root[0] = (char)item->DriveText[0];
        rootW = {(wchar_t)root[0], L':', L'\\'};
        HANDLES(EnterCriticalSection(&ReadCDVolNameCS));
        UINT_PTR uid = ++ReadCDVolNameReqUID;
        ReadCDVolNameBuffer = rootW;
        HANDLES(LeaveCriticalSection(&ReadCDVolNameCS));

        // create thread, in which we will find out the volume_name of the CD drive
        DWORD threadID;
        HANDLE thread = HANDLES(CreateThread(NULL, 0, ReadCDVolNameThreadF,
                                             (void*)uid, 0, &threadID));
        if (thread != NULL && WaitForSingleObject(thread, 500) == WAIT_OBJECT_0)
        { // give it 500ms to find out the volume-name
            HANDLES(EnterCriticalSection(&ReadCDVolNameCS));
            volumeName = ReadCDVolNameBuffer;
            HANDLES(LeaveCriticalSection(&ReadCDVolNameCS));
        }
        else
            volumeName.clear();
        if (thread != NULL)
            AddAuxThread(thread, TRUE); // if the thread is still running, we will kill it before closing the program
        if (volumeName.empty())
            volumeName = LoadStrW(IDS_COMPACT_DISK);

        lstrcpynW(text, volumeName.c_str(), TOOLTIP_TEXT_MAX);
        break;
    }

    case drvtRemote:
    {
        if (wcslen(item->DriveText) > 2)
        {
            lstrcpynW(text, item->DriveText + 2, TOOLTIP_TEXT_MAX);
            RemoveAmpersands(text);
        }
        break;
    }

    case drvtMyDocuments:
        lstrcpyW(text, LoadStrW(IDS_MYDOCUMENTS));
        break;
    case drvtGoogleDrive:
        lstrcpyW(text, LoadStrW(IDS_GOOGLEDRIVE));
        break;
    case drvtDropbox:
        lstrcpyW(text, LoadStrW(IDS_DROPBOX));
        break;
    case drvtOneDrive:
    case drvtOneDriveMenu:
        lstrcpyW(text, LoadStrW(IDS_ONEDRIVE));
        break;
    case drvtNeighborhood:
        lstrcpyW(text, LoadStrW(IDS_NETWORKDRIVE));
        break;

    case drvtOneDriveBus:
    {
        const wchar_t* s = item->DriveText;
        if (*s != L'\t' && *s != 0)
            s++; // hot key
        if (*s == L'\t')
            s++;
        lstrcpynW(text, s, TOOLTIP_TEXT_MAX);
        break;
    }

    case drvtPluginCmd:
    {
        // trim the first column in the item name
        const wchar_t* p = item->DriveText;
        while (*p != 0 && *p != L'\t')
            p++;
        if (*p == L'\t')
        {
            const wchar_t* e = p + 1; // trim the potential third column in the item name (can be after the second TAB)
            while (*e != 0 && *e != L'\t')
                e++;
            std::wstring segment(p + 1, e);
            lstrcpynW(text, segment.c_str(), (int)segment.size() + 1);
        }
        break;
    }
    }
    return TRUE;
}

BOOL CDrivesList::OnContextMenu(BOOL posByMouse, int itemIndex, int panel, const wchar_t** pluginFSDLLName)
{
    CALL_STACK_MESSAGE4("CDrivesList::DisplayMenu(%d, %d, %d)", posByMouse, itemIndex, panel);

    if (pluginFSDLLName != NULL)
        *pluginFSDLLName = NULL;

    // for the MenuPopup variable, access is only allowed if posByMouse == FALSE

    int selectedIndex;
    if (itemIndex == -1 || !posByMouse)
    {
        if (MenuPopup == NULL)
        {
            TRACE_E("CDrivesList::OnContextMenu(): Incorrect call.");
            return FALSE;
        }
        selectedIndex = MenuPopup->GetSelectedItemIndex();
        if (selectedIndex == -1)
        {
            TRACE_E("selectedIndex == -1");
            return FALSE;
        }
    }
    else
        selectedIndex = itemIndex;

    // finding a selected item
    RECT selectedIndexRect = {0};
    if (MenuPopup != NULL)
        MenuPopup->GetItemRect(selectedIndex, &selectedIndexRect);
    std::wstring path;
    CDriveTypeEnum dt = Drives->At(selectedIndex).DriveType;
    switch (dt)
    {
    case drvtUnknow:
    case drvtRemovable:
    case drvtFixed:
    case drvtRemote:
    case drvtCDROM:
    case drvtRAMDisk:
    {
        path = L" :\\";
        path[0] = Drives->At(selectedIndex).DriveText[0];
        break;
    }

    case drvtHotPath:
    {
        if (!MainWindow->GetExpandedHotPath(MainWindow->HWindow, Drives->At(selectedIndex).Param, path))
            return FALSE;
        if (path.size() >= 3 && LowerCase[path[0]] >= 'a' && LowerCase[path[0]] <= 'z' && path[1] == ':' && (path[2] == '\\' || path[2] == '/') ||
            path.size() >= 2 && (path[0] == '\\' || path[0] == '/') && (path[1] == '\\' || path[1] == '/'))
        { // absolute path on disk or network (UNC)
            SlashesToBackslashesAndRemoveDups(path);
            int type;
            BOOL isDir;
            wchar_t* secondPart;
            if (!SalParsePathW(MainWindow->HWindow, path, type, isDir, secondPart, LoadStrW(IDS_ERRORTITLE),
                               NULL, FALSE, NULL, NULL, NULL) ||
                type != PATH_TYPE_WINDOWS || // not a windows path
                !isDir || *secondPart != 0)  // the path to a file (not a directory) or a part of the path does not exist
            {
                return FALSE; // we can do the context menu only for windows paths (not for archives)
            }
        }
        else
            return FALSE; // we can't do the context menu for other types of paths (relative, FS plugin)
        break;
    }

    case drvtPluginFS:
    case drvtPluginCmd:
    {
        CPluginFSInterfaceAbstract* pluginFS = NULL;
        const wchar_t* pluginFSName = NULL;
        int pluginFSNameIndex = -1;
        BOOL isDetachedFS = FALSE;
        BOOL refreshMenu;
        BOOL closeMenu;
        int postCmd;
        void* postCmdParam;

        CPluginData* pluginData = NULL;

        if (dt == drvtPluginFS)
        {
            CPluginFSInterfaceAbstract* fs = Drives->At(selectedIndex).PluginFS;
            // we need to verify, that 'fs' is still a valid interface
            if (FilesWindow->Is(ptPluginFS) && FilesWindow->GetPluginFS()->GetInterface() == fs)
            { // active FS selection - we will do refresh
                pluginData = Plugins.GetPluginData(FilesWindow->GetPluginFS()->GetPluginInterfaceForFS()->GetInterface());
                pluginFS = fs;
                pluginFSName = FilesWindow->GetPluginFS()->GetPluginFSName();
                pluginFSNameIndex = FilesWindow->GetPluginFS()->GetPluginFSNameIndex();
            }
            else
            {
                CDetachedFSList* list = MainWindow->DetachedFSList;
                int i;
                for (i = 0; i < list->Count; i++)
                {
                    if (list->At(i)->GetInterface() == fs)
                    { // disconnected FS selection
                        pluginData = Plugins.GetPluginData(list->At(i)->GetPluginInterfaceForFS()->GetInterface());
                        pluginFS = fs;
                        pluginFSName = list->At(i)->GetPluginFSName();
                        pluginFSNameIndex = list->At(i)->GetPluginFSNameIndex();
                        isDetachedFS = TRUE;
                        break;
                    }
                }
            }
        }
        else // drvtPluginCmd
        {
            pluginData = Plugins.GetPluginData(Drives->At(selectedIndex).DLLName);
            if (pluginFSDLLName != NULL)
                *pluginFSDLLName = Drives->At(selectedIndex).DLLName;
        }

        if (pluginData != NULL)
        {
            POINT p;
            if (posByMouse)
            {
                DWORD pos = GetMessagePos();
                p.x = GET_X_LPARAM(pos);
                p.y = GET_Y_LPARAM(pos);
            }
            else
            {
                p.x = selectedIndexRect.left;
                p.y = selectedIndexRect.bottom;
            }

            // 'pluginFS' may cease to exist while the callback is running, so keep the
            // selected file-system name in independent dynamic storage.
            const std::wstring pluginFSNameBuf = pluginFSName != NULL ? pluginFSName : L"";
            if (pluginData->ChangeDriveMenuItemContextMenu(MainWindow->HWindow, panel, p.x, p.y, pluginFS,
                                                           pluginFSName != NULL ? pluginFSNameBuf.c_str() : NULL,
                                                           pluginFSName != NULL ? pluginFSNameIndex : -1,
                                                           isDetachedFS, refreshMenu,
                                                           closeMenu, postCmd, postCmdParam))
            {
                if (closeMenu)
                {
                    BOOL failed = FALSE;
                    if (MenuPopup != NULL) // item list of change drive menu refresh can occur during the opened context menu, so the focus can change to another item than the one for which we opened the menu and thus the post-command would be sent to another place than we need
                    {
                        selectedIndex = MenuPopup->GetSelectedItemIndex();
                        if (!(selectedIndex >= 0 && selectedIndex < Drives->Count &&
                              (dt == drvtPluginCmd && Drives->At(selectedIndex).DLLName == pluginData->DLLName.c_str() ||
                               dt == drvtPluginFS && Drives->At(selectedIndex).PluginFS == pluginFS)))
                        {
                            int i;
                            for (i = 0; i < Drives->Count; i++)
                            {
                                if (dt == drvtPluginCmd && Drives->At(i).DLLName == pluginData->DLLName.c_str() ||
                                    dt == drvtPluginFS && Drives->At(i).PluginFS == pluginFS)
                                {
                                    MenuPopup->SetSelectedItemIndex(i);
                                    break;
                                }
                            }
                            if (i == Drives->Count)
                                failed = TRUE;
                        }
                    }
                    if (!failed)
                    {
                        if (postCmd != 0) // closing Change Drive menu + executing postCmd
                        {
                            *PostCmd = postCmd;
                            *PostCmdParam = postCmdParam;
                            *FromContextMenu = TRUE;
                            return TRUE;
                        }
                        else // just closing Change Drive menu
                        {
                            *PostCmd = 0; // not needed (set from constructor), just for clarity
                            *FromContextMenu = TRUE;
                            return TRUE;
                        }
                    }
                }
                if (refreshMenu) // plug-in asks for menu refresh
                {
                    PostMessage(MainWindow->HWindow, WM_USER_DRIVES_CHANGE, 0, 0);
                }
            }
        }
        return FALSE;
    }

    case drvtPluginFSInOtherPanel: // no menu will be opened (menu item disabled)
    {
        return FALSE;
    }

    case drvtNeighborhood:
    case drvtOtherPanel:
    case drvtMyDocuments:
    case drvtGoogleDrive:
    case drvtDropbox:
    case drvtOneDrive:
    case drvtOneDriveBus:
    case drvtOneDriveMenu:
    {
        // for Documents, Network, As Other Panel a Cloud Storages we can't do context menu)
        return FALSE;
    }

    default:
    {
        TRACE_E("Unhandled DriveType = " << dt);
        return FALSE;
    }
    }

    // commented out so that we can disconnect a network drive that is not accessible at the moment (longer waiting is tolerated)
    //  if (Drives->At(selectedIndex).DriveType == drvtRemote &&
    //      Drives->At(selectedIndex).Accessible &&   // we will allow to disconnect unaccessible network drives, we verify the others
    //      MainWindow->GetActivePanel()->CheckPath(TRUE, path) != ERROR_SUCCESS) return FALSE;

    //  MainWindow->ReleaseMenuNew();  // windows weren't built for more context menus

    BOOL selectedIndexAccessible = Drives->At(selectedIndex).Accessible;

    if (MainWindow->ContextMenuChngDrv != NULL)
    {
        TRACE_E("ContextMenuChngDrv already exist! Releasing...");
        MainWindow->ContextMenuChngDrv->Release();
        MainWindow->ContextMenuChngDrv = NULL;
    }
    MainWindow->ContextMenuChngDrv = CreateIContextMenu2W(MainWindow->HWindow, path.c_str());
    HMENU h = CreatePopupMenu();
    if (MainWindow->ContextMenuChngDrv != NULL && h != NULL)
    {
        DisplayMenuAux2(MainWindow->ContextMenuChngDrv, h);
        RemoveUselessSeparatorsFromMenu(h);

        POINT pt;
        if (posByMouse)
        {
            DWORD pos = GetMessagePos();
            pt.x = GET_X_LPARAM(pos);
            pt.y = GET_Y_LPARAM(pos);
        }
        else
        {
            pt.x = selectedIndexRect.left;
            pt.y = selectedIndexRect.bottom;
        }
        CMenuPopup contextPopup;
        contextPopup.SetTemplateMenu(h);
        DWORD cmd = contextPopup.Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON,
                                       pt.x, pt.y, MainWindow->HWindow, NULL);

        // WARNING: during the opened context menu, the object can be refreshed (e.g. user inserts
        // a USB stick or disconnects a network drive), we have to count on it when accessing the
        // data of this object (e.g. Drives can contain different data)

        if (cmd != 0)
        {
            BOOL releaseLeft = FALSE;  // disconnect left panel from disk?
            BOOL releaseRight = FALSE; // disconnect right panel from disk?

            std::wstring cmdName;
            AuxGetCommandString(MainWindow->ContextMenuChngDrv, cmd, GCS_VERBW, NULL, cmdName);
            if (_wcsicmp(cmdName.c_str(), L"properties") != 0 && // not mandatory for properties
                _wcsicmp(cmdName.c_str(), L"find") != 0 &&       // not mandatory for find
                _wcsicmp(cmdName.c_str(), L"open") != 0 &&       // not mandatory for open
                _wcsicmp(cmdName.c_str(), L"explore") != 0 &&    // not mandatory for explore
                _wcsicmp(cmdName.c_str(), L"link") != 0)         // not mandatory for create-short-cut
            {
                CFilesWindow* win;
                int i;
                for (i = 0; i < 2; i++)
                {
                    win = i == 0 ? MainWindow->LeftPanel : MainWindow->RightPanel;
                    if (HasTheSameRootPath(win->GetPathW(), path.c_str())) // identical disk (both normal and UNC)
                    {
                        if (i == 0)
                            releaseLeft = TRUE;
                        else
                            releaseRight = TRUE;
                    }
                }
            }

            SetCurrentDirectoryToSystem();
            DWORD disks = GetLogicalDrives();

            CShellExecuteWnd shellExecuteWnd;
            CMINVOKECOMMANDINFOEX ici;
            ZeroMemory(&ici, sizeof(CMINVOKECOMMANDINFOEX));
            ici.cbSize = sizeof(CMINVOKECOMMANDINFOEX);
            // CMIC_MASK_UNICODE + lpDirectoryW, same as the shell-verb-invoke sites
            // elsewhere in this codebase - without it only the narrow
            // lpDirectory reaches the handler, and for a non-ANSI path that is a working
            // directory that does not exist.
            ici.fMask = CMIC_MASK_PTINVOKE | CMIC_MASK_UNICODE;
            ici.hwnd = shellExecuteWnd.Create(MainWindow->HWindow, L"SEW: CDrivesList::OnContextMenu cmd=%d cmdName=%s", cmd, cmdName.c_str());
            // lpVerb (inherited from CMINVOKECOMMANDINFO) is always LPCSTR regardless of the
            // Ex/wide fields alongside it - a genuine, permanent Windows Shell API contract.
            ici.lpVerb = MAKEINTRESOURCEA(cmd);
            std::string pathA;
            ici.lpDirectory = Win32EncodeAcpExact(path, pathA) ? pathA.c_str() : NULL;
            ici.lpDirectoryW = path.c_str();
            ici.nShow = SW_SHOWNORMAL;
            if (MenuPopup != NULL)
            {
                ici.ptInvoke.x = selectedIndexRect.left;
                ici.ptInvoke.y = selectedIndexRect.bottom;
            }
            else
            {
                ici.ptInvoke.x = pt.x;
                ici.ptInvoke.y = pt.y;
            }

            BOOL changeToFixedDrv = cmd == 35; // "format" is not modal, we need to change to fixed drive
            if (releaseLeft)
            {
                if (changeToFixedDrv)
                {
                    MainWindow->LeftPanel->ChangeToFixedDrive(MainWindow->LeftPanel->HWindow);
                    // WARNING: we have to invalidate the window, so that the cached bitmap of Alt+F1/2 menu is broken
                    // otherwise, the old part of the panel was displayed in this situation:
                    // there is disk S: in the left panel; Alt+F1, right click on S, Format
                    InvalidateRect(MainWindow->LeftPanel->HWindow, NULL, TRUE);
                }
                else
                    MainWindow->LeftPanel->HandsOff(TRUE);
            }
            if (releaseRight)
            {
                if (changeToFixedDrv)
                {
                    MainWindow->RightPanel->ChangeToFixedDrive(MainWindow->RightPanel->HWindow);
                    InvalidateRect(MainWindow->RightPanel->HWindow, NULL, TRUE);
                }
                else
                    MainWindow->RightPanel->HandsOff(TRUE);
            }

            DisplayMenuAux(MainWindow->ContextMenuChngDrv, (CMINVOKECOMMANDINFO*)&ici);

            // it's possible to change clipboard from context menu, we will check it ...
            IdleRefreshStates = TRUE;  // we will force the check of the state variables at the next Idle
            IdleCheckClipboard = TRUE; // we will the clipboard to be checked as well

            UpdateWindow(MainWindow->HWindow);
            if (releaseLeft && !changeToFixedDrv)
                MainWindow->LeftPanel->HandsOff(FALSE);
            if (releaseRight && !changeToFixedDrv)
                MainWindow->RightPanel->HandsOff(FALSE);

            if (!selectedIndexAccessible) // unmap the remembered inactive network connection?
                PostMessage(MainWindow->HWindow, WM_USER_DRIVES_CHANGE, 0, 0);

            /*
// the notification will be delivered through WM_DEVICECHANGE
      if (GetLogicalDrives() < disks) // unmapping?
      {
        PostMessage(MainWindow->HWindow, WM_USER_DRIVES_CHANGE, 0, 0);
        TRACE_I("sending 1");
      }
*/
        }
    }
    if (MainWindow->ContextMenuChngDrv != NULL)
    {
        MainWindow->ContextMenuChngDrv->Release();
        MainWindow->ContextMenuChngDrv = NULL;
    }
    if (h != NULL)
        DestroyMenu(h);

    return FALSE;
}

BOOL CDrivesList::RebuildMenu()
{
    CALL_STACK_MESSAGE1("CDrivesList::RebuildMenu()");
    if (MenuPopup == NULL)
    {
        TRACE_E("MenuPopup == NULL");
        return FALSE;
    }
    // asking menu for modification
    if (MenuPopup->BeginModifyMode())
    {
        // remove old data
        DestroyData();
        // get new ones
        BuildData(TRUE); // timeout can't be used, reading CD labels takes long time (1,5s) - system notifies the driver earlier than it's loaded
        // remove items from menu
        MenuPopup->RemoveAllItems();
        // get new items
        LoadMenuFromData();
        // let menu to be redrawn
        MenuPopup->EndModifyMode();
    }
    return TRUE;
}

BOOL CDrivesList::FindPanelPathIndex(CFilesWindow* panel, DWORD* index)
{
    if (panel->Is(ptPluginFS))
    {
        if (!panel->GetPluginFS()->IsServiceSupported(FS_SERVICE_GETCHANGEDRIVEORDISCONNECTITEM))
        { // searching for a plug-in FS without its own item in Change Drive menu (these items are not in Drive bars)
            int i;
            for (i = 0; i < Drives->Count; i++)
            {
                CDriveData* item = &Drives->At(i);
                if (item->DriveType == drvtPluginCmd && panel->GetPluginFS()->GetDLLName() == item->DLLName)
                {
                    *index = i;
                    return TRUE;
                }
            }
        }
    }
    else
    {
        const wchar_t* path = panel->GetPathW();
        if (path[0] == '\\' && path[1] == '\\')
        {
            if (path[2] == '.' && path[3] == '\\' && path[4] != 0 && path[5] == ':')
                return FALSE; // "\\.\C:\" type path
            CPluginData* nethoodPlugin = NULL;
            Plugins.GetFirstNethoodPluginFSName(NULL, &nethoodPlugin);
            int i;
            for (i = 0; i < Drives->Count; i++)
            {
                CDriveData* item = &Drives->At(i);
                if (nethoodPlugin != NULL)
                {
                    if (item->DriveType == drvtPluginCmd && nethoodPlugin->DLLName.c_str() == item->DLLName)
                    {
                        *index = i;
                        return TRUE;
                    }
                }
                else
                {
                    if (item->DriveType == drvtNeighborhood)
                    {
                        *index = i;
                        return TRUE;
                    }
                }
            }
        }
        else
        {
            int i;
            for (i = 0; i < Drives->Count; i++)
            {
                CDriveData* item = &Drives->At(i);

                switch (item->DriveType)
                {
                case drvtUnknow:
                case drvtRemovable:
                case drvtFixed:
                case drvtRemote:
                case drvtCDROM:
                case drvtRAMDisk:
                {
                    if (LowerCase[path[0]] == LowerCase[(char)item->DriveText[0]])
                    {
                        *index = i;
                        return TRUE;
                    }
                    break;
                }
                }
            }
        }
    }
    return FALSE;
}

BOOL CDrivesList::IsLastItemSeparator()
{
    if (Drives->Count < 1)
        return FALSE;
    CDriveData* item = &Drives->At(Drives->Count - 1);
    return item->DriveType == drvtSeparator;
}

DWORD
CDrivesList::GetCachedDrivesMask()
{
    return CachedDrivesMask;
}

DWORD
CDrivesList::GetCachedCloudStoragesMask()
{
    return CachedCloudStoragesMask;
}
