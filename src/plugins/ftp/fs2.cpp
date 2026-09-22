// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

namespace
{
class CScopedWideSecretWipe
{
public:
    explicit CScopedWideSecretWipe(std::wstring& value) noexcept
        : Value(value)
    {
    }

    ~CScopedWideSecretWipe()
    {
        FTPSecureWipe(Value);
    }

private:
    std::wstring& Value;
};
} // namespace

int CSimpleListPluginDataInterface::ListingColumnWidth = 0;      // LO/HI-WORD: left/right panel: width of the Raw Listing column
int CSimpleListPluginDataInterface::ListingColumnFixedWidth = 0; // LO/HI-WORD: left/right panel: does the Raw Listing column have a fixed width?

// Global variables where I store pointers to Salamander's global variables
const CFileData** TransferFileData = NULL;
int* TransferIsDir = NULL;
wchar_t* TransferBuffer = NULL;
int* TransferLen = NULL;
DWORD* TransferRowData = NULL;
CPluginDataInterfaceAbstract** TransferPluginDataIface = NULL;
DWORD* TransferActCustomData = NULL;

CSimpleListPluginDataInterface SimpleListPluginDataInterface;

//
// ****************************************************************************
// CPluginFSInterface
//

CPluginFSInterface::CPluginFSInterface()
{
    Host.clear();
    Port = -1;
    User.clear();
    Path.clear();

    ErrorState = fesOK;
    IsDetached = FALSE;

    ControlConnection = NULL;
    RescuePath.clear();
    HomeDir.clear();
    OverwritePathListing = FALSE;
    PathListing.reset();
    memset(&PathListingDate, 0, sizeof(PathListingDate));
    PathListingIsIncomplete = FALSE;
    PathListingIsBroken = FALSE;
    PathListingMayBeOutdated = FALSE;
    PathListingStartTime = 0;

    DirLineHotPathType = ftpsptEmpty;
    DirLineHotPathUserLength = 0;

    ChangePathOnlyGetCurPathTime = 0;

    TotalConnectAttemptNum = 1;

    AutodetectSrvType = TRUE;
    LastServerType.clear();

    InformAboutUnknownSrvType = TRUE;
    NextRefreshCanUseOldListing = FALSE;
    NextRefreshWontClearCache = FALSE;

    TransferMode = Config.TransferMode;

    CalledFromDisconnectDialog = FALSE;

    RefreshPanelOnActivation = FALSE;
}

CPluginFSInterface::~CPluginFSInterface()
{
    if (PathListing.has_value())
        FTPSecureWipe(*PathListing);
    if (ControlConnection != NULL)
        TRACE_E("Unexpected situation in CPluginFSInterface::~CPluginFSInterface(): ControlConnection is not closed!");
}

BOOL CPluginFSInterface::BuildUserPartText(std::wstring& userPart, const char* path,
                                           BOOL ignorePath) const
{
    try
    {
        std::wstring result(L"//");
        if (User != L"anonymous")
        {
            result.append(User);
            result.push_back(L'@');
        }
        result.append(Host);
        if (Port != IPPORT_FTP)
        {
            result.push_back(L':');
            result.append(std::to_wstring(Port));
        }
        if (!ignorePath)
        {
            const char* remotePath = path != NULL ? path : Path.c_str();
            wchar_t separator = L'/';
            if (*remotePath == '/')
                ++remotePath;
            else if (*remotePath == '\\')
            {
                separator = L'\\';
                ++remotePath;
            }
            std::wstring remotePathText;
            if (!DecodeSessionBytes(std::string_view(remotePath, strlen(remotePath)), remotePathText))
                return FALSE;
            result.push_back(separator);
            result.append(remotePathText);
        }
        userPart.swap(result);
        return TRUE;
    }
    catch (...)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
}

BOOL CPluginFSInterface::BuildFullPathText(const wchar_t* fsName, const char* path,
                                           std::wstring& fullPath) const
{
    std::wstring userPart;
    if (!BuildUserPartText(userPart, path))
        return FALSE;
    try
    {
        std::wstring staged(fsName != NULL ? fsName : L"");
        staged.push_back(L':');
        staged.append(userPart);
        fullPath.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
}

BOOL CPluginFSInterface::DecodeSessionBytes(std::string_view bytes,
                                             std::wstring& text) const
{
    if (ControlConnection != NULL)
        return ControlConnection->DecodeText(bytes.data(), bytes.size(), text);
    return FtpDecodeLocalText(bytes, text);
}

BOOL CPluginFSInterface::EncodeSessionText(const wchar_t* text,
                                            std::string& bytes) const
{
    const wchar_t* value = text != NULL ? text : L"";
    if (ControlConnection != NULL)
        return ControlConnection->EncodeText(value, bytes);
    return FtpEncodeLocalText(value, bytes);
}

BOOL CPluginFSInterface::GetEncodedSessionUserLength(int& userLength) const
{
    std::string userBytes;
    if (!EncodeSessionText(User.c_str(), userBytes))
        return FALSE;
    userLength = FTPGetUserLength(userBytes.c_str());
    return TRUE;
}

void CPluginFSInterface::CheckCtrlConClose(HWND parent)
{
    if (ControlConnection != NULL && !ControlConnection->IsConnected())
    {
        int panel;
        BOOL notInPanel = !SalamanderGeneral->GetPanelWithPluginFS(this, panel);
        ControlConnection->CheckCtrlConClose(notInPanel, panel == PANEL_LEFT, parent, notInPanel);
    }
}

CFTPServerPathType
CPluginFSInterface::GetFTPServerPathType(const char* path)
{
    if (ControlConnection != NULL)
        return ControlConnection->GetFTPServerPathType(path);
    return ftpsptEmpty;
}

BOOL CPluginFSInterface::ReconnectIfNeeded(HWND parent, BOOL* reconnected, BOOL setStartTimeIfConnected,
                                           int* totalAttemptNum, const std::string* retryMessage)
{
    CALL_STACK_MESSAGE3("CPluginFSInterface::ReconnectIfNeeded(, , %d, , %s)",
                        setStartTimeIfConnected,
                        retryMessage != NULL ? retryMessage->c_str() : NULL);
    if (ControlConnection != NULL)
    {
        int panel;
        BOOL notInPanel = !SalamanderGeneral->GetPanelWithPluginFS(this, panel);
        return ControlConnection->ReconnectIfNeeded(notInPanel, panel == PANEL_LEFT, parent,
                                                    User, reconnected,
                                                    setStartTimeIfConnected, totalAttemptNum,
                                                    retryMessage, NULL, -1, FALSE);
    }
    return FALSE;
}

BOOL CPluginFSInterface::GetRootPath(CSalamanderStringBuffer* userPart)
{
    std::wstring text;
    return userPart != NULL && BuildUserPartText(text, "/") &&
           sally::plugin_abi::WriteStringBuffer(*userPart, text);
}

BOOL CPluginFSInterface::GetCurrentPath(CSalamanderStringBuffer* userPart)
{
    std::wstring text;
    return userPart != NULL && BuildUserPartText(text) &&
           sally::plugin_abi::WriteStringBuffer(*userPart, text);
}

BOOL CPluginFSInterface::ResolveFullServerPathBytes(HWND parent, std::string& path,
                                                    BOOL& success)
{
    if (ControlConnection == NULL)
        return FALSE; // translation is not possible (the FS has not been connected yet); let Salamander report the error

    std::string logMessage;
    if (!FTPFormatString(logMessage, LoadStr(IDS_LOGMSGCHANGINGPATH), path.c_str()))
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }
    ControlConnection->LogMessage(logMessage.c_str(), -1, TRUE);

    int panel;
    BOOL notInPanel = !SalamanderGeneral->GetPanelWithPluginFS(this, panel);
    std::string replyBuf;
    TotalConnectAttemptNum = 1; // start of a user-requested action -> if reconnection is needed, this is the first attempt
    const std::string* retryMessageToUse = NULL;
    BOOL canRetry = FALSE;
    std::string nextRetryMessage;
    ControlConnection->SetStartTime();
    BOOL retErr = TRUE;
    while (ControlConnection->SendChangeWorkingPath(notInPanel, panel == PANEL_LEFT,
                                                    SalamanderGeneral->GetMsgBoxParent(),
                                                    path.c_str(), User, &success, replyBuf,
                                                    Path.c_str(), &TotalConnectAttemptNum,
                                                    retryMessageToUse, FALSE, NULL))
    {
        BOOL run = FALSE;
        if (!success)
        {
            std::wstring errorText;
            if (!FtpFormatServerReplyMessage(ControlConnection->GetTextCodec(),
                                             LangStr(IDS_CHANGEWORKPATHERROR).c_str(),
                                             path, replyBuf, errorText))
                errorText = LangStr(IDS_OPERDOPPR_LOWMEM);
            SalamanderGeneral->SalMessageBox(parent, errorText.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                             MB_OK | MB_ICONEXCLAMATION);
        }
        else
        {
            if (ControlConnection->GetCurrentWorkingPath(SalamanderGeneral->GetMsgBoxParent(),
                                                         path, TRUE, &canRetry, &nextRetryMessage))
            {
                // successfully obtained a new path on the server
            }
            else
            {
                if (canRetry) // the "retry" option is allowed
                {
                    run = TRUE;
                    retryMessageToUse = &nextRetryMessage;
                }
                else
                    success = FALSE;
            }
        }
        if (!run)
        {
            retErr = FALSE;
            break;
        }
    }
    if (retErr)
        success = FALSE;
    if (success)
    {
        ChangePathOnlyGetCurPathTime = GetTickCount(); // optimization for ChangePath() called right after obtaining the working path
    }
    return TRUE;
}

BOOL CPluginFSInterface::GetFullFSPath(HWND parent, const wchar_t* fsName,
                                       CSalamanderStringBuffer* pathBuffer,
                                       BOOL& success)
{
    success = FALSE;
    std::wstring pathText;
    if (pathBuffer == NULL ||
        !sally::plugin_abi::ReadStringBuffer(*pathBuffer, pathText))
        return FALSE;
    std::string pathBytes;
    if (!EncodeSessionText(pathText.c_str(), pathBytes))
        return FALSE;
    const BOOL handled = ResolveFullServerPathBytes(parent, pathBytes, success);
    if (!handled || !success)
        return handled;

    std::wstring fullPath;
    if (!BuildFullPathText(fsName, pathBytes.c_str(), fullPath))
    {
        success = FALSE;
        return TRUE;
    }
    success = sally::plugin_abi::WriteStringBuffer(*pathBuffer, fullPath);
    return TRUE;
}

BOOL CPluginFSInterface::GetFullName(CFileData& file, int isDir,
                                      CSalamanderStringBuffer* fullName)
{
    std::string remotePath;
    if (!FtpStoreProtocolBytes(Path, remotePath))
        return FALSE;
    CFTPServerPathType type = GetFTPServerPathType(Path.c_str());
    if (isDir == 2) // up-dir
    {
        if (!FTPCutDirectory(type, remotePath, NULL, NULL))
            return FALSE;
    }
    else
    {
        std::string fileNameBytes;
        if (ControlConnection == NULL || !ControlConnection->EncodeText(file.Name, fileNameBytes))
            return FALSE;
        if (!FTPPathAppend(type, remotePath, fileNameBytes.c_str(), isDir))
            return FALSE;
    }
    std::wstring fullNameText;
    return fullName != NULL && BuildUserPartText(fullNameText, remotePath.c_str()) &&
           sally::plugin_abi::WriteStringBuffer(*fullName, fullNameText);
}

BOOL CPluginFSInterface::IsCurrentPath(int currentFSNameIndex, int fsNameIndex,
                                       const wchar_t* userPart)
{
    std::wstring currentUserPart;
    if (currentFSNameIndex == fsNameIndex && userPart != NULL &&
        BuildUserPartText(currentUserPart))
    {
        CFTPServerPathType type = GetFTPServerPathType(Path.c_str());
        return FTPIsTheSamePathW(type, currentUserPart.c_str(), userPart, TRUE,
                                 FTPGetUserLengthW(User.c_str()));
    }
    else
        return FALSE; // does not fit, cannot compare
}

BOOL CPluginFSInterface::IsOurPath(int currentFSNameIndex, int fsNameIndex,
                                   const wchar_t* userPart)
{
    if (Config.UseConnectionDataFromConfig)
    { // the user is opening a new connection from the Connect dialog - we must return FALSE (the user might
        // request a second connection to the same server - a situation where ChangePath would otherwise return TRUE)
        return FALSE;
    }

    if (currentFSNameIndex != fsNameIndex)
        return FALSE; // cannot mix FTP and FTPS

    std::wstring currentUserPart;
    if (userPart != NULL && BuildUserPartText(currentUserPart))
    {
        return FTPHasTheSameRootPathW(currentUserPart.c_str(), userPart,
                                      FTPGetUserLengthW(User.c_str()));
    }
    else
        return FALSE; // does not fit, cannot compare
}

void CPluginFSInterface::ClearHostFromListingCacheIfFirstCon(const wchar_t* host, int port, const wchar_t* user)
{
    int i;
    for (i = 0; i < FTPConnections.Count; i++) // try to find an FS with the given host+port+user combination
    {
        CPluginFSInterface* fs = FTPConnections[i];
        if (fs != this && fs->ContainsHost(host, port, user))
            return; // found one, the cache will not be cleared
    }

    // Clear cached listings for all paths with the given host+port+user combination
    ListingCache.RefreshOnPath(host, port, user, ftpsptEmpty, "", TRUE);

    std::wstring userPart;
    if (BuildUserPartText(userPart, NULL, TRUE))
    {
        std::wstring path = AssignedFSName + L":" + userPart;
        SalamanderGeneral->RemoveFilesFromCache((path + L"/").c_str());
        SalamanderGeneral->RemoveFilesFromCache((path + L"\\").c_str());

        path = AssignedFSNameFTPS + L":" + userPart;
        SalamanderGeneral->RemoveFilesFromCache((path + L"/").c_str());
        SalamanderGeneral->RemoveFilesFromCache((path + L"\\").c_str());
    }
}

BOOL CPluginFSInterface::ChangePath(int currentFSNameIndex,
                                    CSalamanderStringBuffer* fsName, int fsNameIndex,
                                    const wchar_t* userPart,
                                    CSalamanderStringBuffer* cutFileName,
                                    BOOL* pathWasCut, BOOL forceRefresh, int mode)
{
    std::wstring fsNameText;
    if (fsName == NULL || !sally::plugin_abi::ReadStringBuffer(*fsName, fsNameText))
        return FALSE;

    std::string userPartBytes;
    if (ControlConnection != NULL && !EncodeSessionText(userPart, userPartBytes))
        return FALSE;

    std::wstring returnedFSName;
    std::string returnedCutFileNameBytes;
    BOOL returnedPathWasCut = FALSE;
    if (!FtpStoreWideText(fsNameText, returnedFSName))
        return FALSE;

    BOOL result = FALSE;
    try
    {
        result = ChangePathBytes(
            currentFSNameIndex, returnedFSName, fsNameIndex,
            userPart != NULL ? userPart : L"", userPartBytes.c_str(),
            cutFileName != NULL ? &returnedCutFileNameBytes : NULL,
            pathWasCut != NULL ? &returnedPathWasCut : NULL,
            forceRefresh, mode);
    }
    catch (...)
    {
        return FALSE;
    }
    if (!result)
        return FALSE;

    std::wstring returnedCutFileName;
    if (cutFileName != NULL &&
         !DecodeSessionBytes(returnedCutFileNameBytes,
                              returnedCutFileName))
        return FALSE;

    if (returnedFSName.size() >= (std::numeric_limits<DWORD>::max)() ||
        returnedCutFileName.size() >= (std::numeric_limits<DWORD>::max)())
        return FALSE;
    if (!sally::plugin_abi::ReserveStringBuffer(
            *fsName, static_cast<DWORD>(returnedFSName.size() + 1)) ||
        (cutFileName != NULL &&
         !sally::plugin_abi::ReserveStringBuffer(
             *cutFileName, static_cast<DWORD>(returnedCutFileName.size() + 1))))
        return FALSE;
    if (!sally::plugin_abi::WriteStringBuffer(*fsName, returnedFSName) ||
        (cutFileName != NULL &&
         !sally::plugin_abi::WriteStringBuffer(*cutFileName,
                                                returnedCutFileName)))
        return FALSE;
    if (pathWasCut != NULL)
        *pathWasCut = returnedPathWasCut;
    return TRUE;
}

BOOL CPluginFSInterface::ChangePathBytes(int currentFSNameIndex, std::wstring& fsName,
                                         int fsNameIndex, const wchar_t* userPartW,
                                         const char* userPart,
                                         std::string* cutFileName, BOOL* pathWasCut,
                                         BOOL forceRefresh, int mode)
{
    if (forceRefresh && RefreshPanelOnActivation)
        RefreshPanelOnActivation = FALSE;

    if (mode != 3 && (pathWasCut != NULL || cutFileName != NULL))
    {
        TRACE_E("Incorrect value of 'mode' in CPluginFSInterface::ChangePath().");
        mode = 3;
    }
    OverwritePathListing = TRUE;
    if (cutFileName != NULL)
        cutFileName->clear();
    if (pathWasCut != NULL)
        *pathWasCut = FALSE;
    CFTPErrorState lastErrorState = ErrorState;
    ErrorState = fesOK; // ready for the next call again

    if (lastErrorState == fesFatal)
    {
        TargetPanelPath.clear(); // the connection failed, no path change in the target panel
        return FALSE;           // fatal error, stop
    }

    // Treat a hard refresh as distrust of the path; drop from the disk cache all files
    // downloaded for View (including files from subpaths for both FTP and FTPS)
    if (mode == 1 && forceRefresh && !NextRefreshWontClearCache)
    {
        int i;
        for (i = 0; i < 2; i++)
        {
            const std::wstring uniqueFileName =
                (i == 0 ? AssignedFSName : AssignedFSNameFTPS) +
                L":" + (userPartW != NULL ? userPartW : L"");
            SalamanderGeneral->RemoveFilesFromCache(uniqueFileName.c_str());
        }
    }

    std::string newUserPart;
    if (ControlConnection == NULL) // opening the connection (opening the path on the FTP server)
    {
        TargetPanelPath.clear();
        TotalConnectAttemptNum = 1; // opening the connection = first attempt to open it
        InformAboutUnknownSrvType = TRUE;

        BOOL parsedPath = TRUE; // TRUE = path obtained from the user part; need to decide whether to trim '/' or '\\' at the start
        BOOL encodeInitialPathAfterLogin = FALSE;
        std::wstring initialPathText;
        ControlConnection = new CControlConnectionSocket;
        if (ControlConnection == NULL || !ControlConnection->IsGood())
        {
            if (ControlConnection != NULL) // insufficient system resources for allocating the object
            {
                DeleteSocket(ControlConnection);
                ControlConnection = NULL;
            }
            else
                TRACE_E(LOW_MEMORY);
            TargetPanelPath.clear(); // the connection failed, no path change in the target panel
            return FALSE;           // fatal error
        }
        const auto failOpeningConnection = [&]() -> BOOL
        {
            DeleteSocket(ControlConnection);
            ControlConnection = NULL;
            TargetPanelPath.clear();
            return FALSE;
        };

        if ((userPartW == NULL || *userPartW == 0) && Config.UseConnectionDataFromConfig) // data from the Connect dialog
        {
            // Pull server data selected in the Connect dialog from the configuration
            CFTPServer* server;
            if (Config.LastBookmark == 0)
                server = &Config.QuickConnectServer;
            else
            {
                if (Config.LastBookmark - 1 >= 0 && Config.LastBookmark - 1 < Config.FTPServerList.Count)
                {
                    server = Config.FTPServerList[Config.LastBookmark - 1];
                }
                else
                {
                    TRACE_E("Unexpected situation in CPluginFSInterface::ChangePath().");
                    return failOpeningConnection();
                }
            }

            std::wstring stagedHost;
            std::wstring connectionUser;
            std::wstring stagedTargetPanelPath;
            if (!FtpStoreWideText(server->Address, stagedHost) ||
                !FtpStoreWideText(server->AnonymousConnection ? L"anonymous" : server->UserName.c_str(),
                                  connectionUser) ||
                !FtpStoreWideText(server->InitialPath, initialPathText) ||
                !FtpStoreWideText(server->TargetPanelPath, stagedTargetPanelPath))
                return failOpeningConnection();
            encodeInitialPathAfterLogin = TRUE;
            parsedPath = FALSE; // path entered by the user (never trim '/' or '\\' at the beginning)

            BOOL useListingsCache = Config.UseListingsCache;
            if (server->UseListingsCache != 2)
                useListingsCache = server->UseListingsCache;
            BOOL usePassiveMode = Config.PassiveMode;
            if (server->UsePassiveMode != 2)
                usePassiveMode = server->UsePassiveMode;
            BOOL keepConnectionAlive = Config.KeepAlive;
            int keepAliveSendEvery = Config.KeepAliveSendEvery;
            int keepAliveStopAfter = Config.KeepAliveStopAfter;
            int keepAliveCommand = Config.KeepAliveCommand;
            if (server->KeepConnectionAlive != 2)
            {
                keepConnectionAlive = server->KeepConnectionAlive;
                if (server->KeepConnectionAlive == 1) // custom values
                {
                    keepAliveSendEvery = server->KeepAliveSendEvery;
                    keepAliveStopAfter = server->KeepAliveStopAfter;
                    keepAliveCommand = server->KeepAliveCommand;
                }
            }
            int stagedTransferMode = Config.TransferMode;
            if (server->TransferMode != 0)
            {
                switch (server->TransferMode)
                {
                case 1:
                    stagedTransferMode = trmBinary;
                    break;
                case 2:
                    stagedTransferMode = trmASCII;
                    break;
                default:
                    stagedTransferMode = trmAutodetect;
                    break;
                }
            }
            int encControlConn = server->EncryptControlConnection == 1;
            if (!FtpStoreWideText(
                    encControlConn ? AssignedFSNameFTPS : AssignedFSName,
                    fsName))
                return failOpeningConnection();
            int encDataConn = encControlConn && server->EncryptDataConnection == 1;
            int compressData = (server->CompressData >= 0) ? server->CompressData : Config.CompressData;

            std::wstring password;
            CScopedWideSecretWipe passwordWipe(password);
            if (server->AnonymousConnection)
            {
                if (!Config.GetAnonymousPasswd(password))
                    return failOpeningConnection();
            }
            else
            {
                CSalamanderPasswordManagerAbstract* passwordManager = SalamanderGeneral->GetSalamanderPasswordManager();
                if (server->EncryptedPassword != NULL &&
                    !FTPDecryptPasswordW(passwordManager, server->EncryptedPassword,
                                         server->EncryptedPasswordSize, &password))
                    password.clear();
            }

            // Handling decryptability of a possible proxy password is done in the connect dialog
            if (!ControlConnection->SetConnectionParameters(stagedHost.c_str(), server->Port, connectionUser.c_str(),
                                                       password.c_str(),
                                                       useListingsCache,
                                                       server->InitFTPCommands.c_str(),
                                                       usePassiveMode,
                                                       server->ListCommand.c_str(),
                                                       keepConnectionAlive,
                                                       keepAliveSendEvery,
                                                       keepAliveStopAfter,
                                                       keepAliveCommand,
                                                       server->ProxyServerUID,
                                                       encControlConn,
                                                       encDataConn,
                                                       compressData))
                return failOpeningConnection();

            AutodetectSrvType = server->ServerType.empty();
            if (!server->ServerType.empty())
            {
                const char* serverType = server->ServerType.c_str() + (server->ServerType[0] == '*' ? 1 : 0);
                if (!FtpStoreLocalTextBytes(serverType, LastServerType))
                    return failOpeningConnection();
            }
            else
                LastServerType.clear();
            Host.swap(stagedHost);
            Port = server->Port;
            User.swap(connectionUser);
            Path.clear();
            TargetPanelPath.swap(stagedTargetPanelPath);
            TransferMode = stagedTransferMode;
            ClearHostFromListingCacheIfFirstCon(Host.c_str(), Port, User.c_str());
        }
        else // connection based on changing the path in the FTP file system (e.g. Shift+F7 + "ftp://ftp.altap.cz/")
        {
            if (Config.UseConnectionDataFromConfig)
                TRACE_E("Unexpected situation in CPluginFSInterface::ChangePath() - UseConnectionDataFromConfig + nonempty userpart.");

            int encControlAndDataConn = currentFSNameIndex == AssignedFSNameIndexFTPS;

            // Verify that any password for the default proxy can be decrypted (we may call SetConnectionParameters() only if it can)
            if (!Config.FTPProxyServerList.EnsurePasswordCanBeDecrypted(SalamanderGeneral->GetMsgBoxParent(), Config.DefaultProxySrvUID))
            {
                return failOpeningConnection();
            }

            AutodetectSrvType = TRUE; // we are using automatic server type detection
            LastServerType.clear();

            std::wstring newUserPartText;
            if (!FtpStoreWideText(userPartW != NULL ? userPartW : L"", newUserPartText))
                return failOpeningConnection();
            CScopedWideSecretWipe userPartWipe(newUserPartText);
            wchar_t *u, *host, *p, *path, *password;
            wchar_t firstCharOfPath = L'/';
            FTPSplitPathW(newUserPartText.data(), &u, &password, &host, &p, &path,
                          &firstCharOfPath, 0);
            if (password != NULL && *password == 0)
                password = NULL;
            if (host == NULL || *host == 0)
            {
                SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_HOSTNAMEMISSING).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(), MSGBOX_ERROR);
                return failOpeningConnection();
            }
            std::wstring parsedUser;
            if (!FtpStoreWideText(u == NULL || *u == 0 ? L"anonymous" : u, parsedUser))
                return failOpeningConnection();
            int port = IPPORT_FTP;
            if (p != NULL && *p != 0)
            {
                unsigned parsedPort = 0;
                const wchar_t* digit = p;
                while (*digit >= L'0' && *digit <= L'9' && parsedPort <= 65535)
                {
                    parsedPort = parsedPort * 10 + static_cast<unsigned>(*digit - L'0');
                    digit++;
                }
                if (*digit != 0 || parsedPort < 1 || parsedPort > 65535)
                {
                    SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_INVALIDPORT).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(), MSGBOX_ERROR);
                    return failOpeningConnection();
                }
                port = static_cast<int>(parsedPort);
            }

            std::wstring parsedHost;
            if (!FtpStoreWideText(host, parsedHost))
                return failOpeningConnection();
            if (path != NULL)
            {
                try
                {
                    initialPathText.assign(1, firstCharOfPath);
                    initialPathText.append(path);
                }
                catch (...)
                {
                    return failOpeningConnection();
                }
                encodeInitialPathAfterLogin = TRUE;
            }
            else
                Path.clear();

            std::wstring passwordW;
            CScopedWideSecretWipe passwordWipe(passwordW);
            const BOOL havePassword = parsedUser == L"anonymous" && password == NULL ?
                                          Config.GetAnonymousPasswd(passwordW) :
                                          FtpStoreWideText(password != NULL ? password : L"", passwordW);
            if (!havePassword ||
                !ControlConnection->SetConnectionParameters(parsedHost.c_str(), port, parsedUser.c_str(), passwordW.c_str(),
                                                       Config.UseListingsCache, NULL, Config.PassiveMode,
                                                       NULL, Config.KeepAlive, Config.KeepAliveSendEvery,
                                                       Config.KeepAliveStopAfter, Config.KeepAliveCommand,
                                                       -2 /* default proxy server */,
                                                       encControlAndDataConn, encControlAndDataConn, Config.CompressData))
                return failOpeningConnection();
            Host.swap(parsedHost);
            Port = port;
            User.swap(parsedUser);
            ClearHostFromListingCacheIfFirstCon(Host.c_str(), Port, User.c_str());
            TransferMode = Config.TransferMode;
        }

        ControlConnection->SetStartTime();
        if (!ControlConnection->StartControlConnection(SalamanderGeneral->GetMsgBoxParent(),
                                                       User, FALSE, &RescuePath,
                                                       &TotalConnectAttemptNum,
                                                       NULL, TRUE, -1, FALSE))
        {                                            // failed to connect, release the socket object (signals the "never connected" state)
            ControlConnection->ActivateWelcomeMsg(); // if any message box deactivated the welcome-msg window, activate it again
            DeleteSocket(ControlConnection);
            ControlConnection = NULL;
            Logs.RefreshListOfLogsInLogsDlg();
            TargetPanelPath.clear(); // the connection failed, no path change in the target panel
            return FALSE;
        }
        if (encodeInitialPathAfterLogin &&
            !ControlConnection->EncodeText(initialPathText.c_str(), Path))
        {
            SalamanderGeneral->ShowMessageBox(
                SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_INVALIDPATH).c_str(),
                SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                MSGBOX_ERROR);
            DeleteSocket(ControlConnection);
            ControlConnection = NULL;
            TargetPanelPath.clear();
            return FALSE;
        }
        if (!FtpStoreProtocolBytes(RescuePath, HomeDir))
        {
            DeleteSocket(ControlConnection);
            ControlConnection = NULL;
            TargetPanelPath.clear();
            return FALSE;
        }
        std::optional<std::string> pathListing;
        CFTPDate pathListingDate;
        memset(&pathListingDate, 0, sizeof(pathListingDate));
        DWORD pathListingStartTime = 0;
        BOOL ret = ControlConnection->ChangeWorkingPath(TRUE, FALSE, SalamanderGeneral->GetMsgBoxParent(),
                                                        Path, User,
                                                        parsedPath, forceRefresh, mode, FALSE,
                                                        cutFileName, pathWasCut, RescuePath, TRUE,
                                                        pathListing, &pathListingDate,
                                                        &pathListingStartTime, &TotalConnectAttemptNum, TRUE);

        if (pathListing.has_value()) // the listing was in the cache
        {
            OverwritePathListing = FALSE;
            if (PathListing.has_value())
                FTPSecureWipe(*PathListing);
            PathListing.emplace(std::move(*pathListing));
            PathListingDate = pathListingDate;
            PathListingIsIncomplete = FALSE; // only complete listings are stored in the cache
            PathListingIsBroken = FALSE;     // only intact listings are stored in the cache
            PathListingMayBeOutdated = FALSE;
            PathListingStartTime = pathListingStartTime;
        }

        if (ret && !TargetPanelPath.empty())
        {
            TargetPanelPathPanel = SalamanderGeneral->GetSourcePanel();
            TargetPanelPathPanel = (TargetPanelPathPanel == PANEL_RIGHT) ? PANEL_LEFT : PANEL_RIGHT;
            SalamanderGeneral->PostMenuExtCommand(FTPCMD_CHANGETGTPANELPATH, TRUE); // send later in "idle"
        }
        if (!ret)
            TargetPanelPath.clear();           // the connection failed, no path change in the target panel
        ControlConnection->ActivateWelcomeMsg(); // if any message box deactivated the welcome-msg window, activate it again
        return ret;
    }
    else // path change
    {
        ControlConnection->DetachWelcomeMsg(); // the welcome-msg window will no longer be activated (the user likely switched to the panel)

        BOOL skipFirstReconnectIfNeeded = TRUE;
        int backupTotalConnectAttemptNum = TotalConnectAttemptNum;
        if (lastErrorState == fesOK) // on the first call (within a single path change and if it is not a connect)
        {                            // no rescue path - Salamander itself restores the original panel path on failure
            RescuePath.clear();
            TotalConnectAttemptNum = 1;         // starting the path change = potential first attempt to open the connection
            skipFirstReconnectIfNeeded = FALSE; // nothing to resume on the first call -> connection test required
        }

        BOOL ret = TRUE;
        if (!NextRefreshCanUseOldListing || !PathListing.has_value() /* always false */)
        {
            NextRefreshCanUseOldListing = FALSE;

            // retrieve the new path
            if (!FtpStoreProtocolBytes(userPart != NULL ? userPart : "", newUserPart))
                return FALSE;
            char* path;
            char firstCharOfPath;
            int userLength = 0;
            if (!GetEncodedSessionUserLength(userLength))
            {
                FTPSecureWipe(newUserPart);
                return FALSE;
            }
            FTPSplitPath(newUserPart.data(), NULL, NULL, NULL, NULL, &path, &firstCharOfPath, userLength);
            if (path != NULL)
            {
                Path.assign(1, firstCharOfPath);
                Path.append(path);
            }
            else
                Path.clear();
            FTPSecureWipe(newUserPart); // erase the memory that contained the password

            if (IsDetached && !ControlConnection->IsConnected())
            { // reconnecting a detached FS with a closed control connection - inform the user about what was
                // most likely written to the log already (in the "quiet" mode of CheckCtrlConClose())
                ControlConnection->CheckCtrlConClose(TRUE, FALSE /* leftPanel - ignored */,
                                                     SalamanderGeneral->GetMsgBoxParent(), FALSE);
            }

            BOOL showChangeInLog = TRUE;
            if (lastErrorState == fesOK && // on the first call (within a single path change and if it is not a connect)
                GetTickCount() - ChangePathOnlyGetCurPathTime <= 1000)
            { // less than a second has passed since obtaining the working path - optimize ChangeWorkingPath()
                ChangePathOnlyGetCurPathTime = 0;
                Path.clear();                                          // the next ChangeWorkingPath() call only pulls the working path from cache (no further path change)
                showChangeInLog = FALSE;                               // the log message was already shown in GetFullFSPath()
                skipFirstReconnectIfNeeded = TRUE;                     // continue from the previous control connection work
                TotalConnectAttemptNum = backupTotalConnectAttemptNum; // continue with the previous reconnect count
            }
            else
            {
                BOOL old = SalamanderGeneral->GetMainWindowHWND() != GetForegroundWindow(); // if the main window is inactive, we must show wait windows; otherwise it cannot be activated and another application could gain focus after the operation window closes automatically
                ControlConnection->SetStartTime(old);                                       // set the time only when it is the start of the operation (and not a continuation of GetFullFSPath())
            }

            int panel;
            BOOL notInPanel = !SalamanderGeneral->GetPanelWithPluginFS(this, panel);
            std::optional<std::string> pathListing;
            CFTPDate pathListingDate;
            memset(&pathListingDate, 0, sizeof(pathListingDate));
            DWORD pathListingStartTime = 0;
            ret = ControlConnection->ChangeWorkingPath(notInPanel, panel == PANEL_LEFT,
                                                       SalamanderGeneral->GetMsgBoxParent(), Path,
                                                       User, TRUE,
                                                       forceRefresh, mode,
                                                       lastErrorState == fesInaccessiblePath, /* if it cannot be listed, shorten it */
                                                       lastErrorState == fesInaccessiblePath ? NULL : cutFileName,
                                                       pathWasCut, RescuePath, showChangeInLog,
                                                       pathListing, &pathListingDate,
                                                       &pathListingStartTime, &TotalConnectAttemptNum,
                                                       skipFirstReconnectIfNeeded);
            if (pathListing.has_value()) // the listing was in the cache
            {
                OverwritePathListing = FALSE;
                if (PathListing.has_value())
                    FTPSecureWipe(*PathListing);
                PathListing.emplace(std::move(*pathListing));
                PathListingDate = pathListingDate;
                PathListingIsIncomplete = FALSE; // only complete listings are stored in the cache
                PathListingIsBroken = FALSE;     // only intact listings are stored in the cache
                PathListingMayBeOutdated = FALSE;
                PathListingStartTime = pathListingStartTime;
            }
            if (!ret)
                TargetPanelPath.clear(); // the connection failed, no path change in the target panel
        }
        else // when refreshing because of server-type or column configuration changes we stay on the same path with the same listing text (even if it is NULL)
        {
            ControlConnection->SetStartTime(); // set the time for listing
            OverwritePathListing = FALSE;
        }
        return ret;
    }
}

void CPluginFSInterface::AddUpdir(BOOL& err, BOOL& needUpDir, CFTPListingPluginDataInterface* dataIface,
                                  TIndirectArray<CSrvTypeColumn>* columns, CSalamanderDirectoryAbstract* dir)
{
    needUpDir = FALSE;
    CFileData updir;
    memset(&updir, 0, sizeof(updir));
    if (dataIface->AllocPluginData(updir))
    {
        updir.Name = SalamanderGeneral->DupStr(L"..");
        if (updir.Name != NULL)
        {
            updir.NameLen = wcslen(updir.Name);
            updir.Ext = updir.Name + updir.NameLen;

            // fill empty values into empty columns
            FillEmptyValues(err, &updir, TRUE, dataIface, columns, NULL, NULL, 0, 0, 0);

            // add it to 'dir'
            if (!err && !dir->AddDir(NULL, updir, NULL))
                err = TRUE;
            if (err)
            {
                // release the up-dir data
                dataIface->ReleasePluginData(updir, TRUE);
                SalamanderGeneral->Free(updir.Name);
            }
        }
        else
            err = TRUE; // low memory
    }
    else
        err = TRUE; // low memory
}

BOOL CPluginFSInterface::ParseListing(CSalamanderDirectoryAbstract* dir,
                                      CPluginDataInterfaceAbstract** pluginData,
                                      CServerType* serverType, BOOL* lowMem, BOOL isVMS,
                                      const wchar_t* findName, BOOL caseSensitive,
                                      BOOL* fileExists, BOOL* dirExists)
{
    BOOL ret = FALSE;
    *lowMem = FALSE;
    if (pluginData != NULL)
        *pluginData = NULL;
    if (findName != NULL)
    {
        if (fileExists != NULL)
            *fileExists = FALSE;
        if (dirExists != NULL)
            *dirExists = FALSE;
    }

    TIndirectArray<CSrvTypeColumn>* columns = new TIndirectArray<CSrvTypeColumn>(5, 5);
    DWORD validDataMask = VALID_DATA_HIDDEN | VALID_DATA_ISLINK; // Name + NameLen + Hidden + IsLink
    BOOL err = FALSE;
    if (columns != NULL && columns->IsGood())
    {
        // Create a local copy of the column data (the server type is not locked for the whole lifetime of the listing in the panel)
        int i;
        for (i = 0; i < serverType->Columns.Count; i++)
        {
            CSrvTypeColumn* c = serverType->Columns[i]->MakeCopy();
            if (c != NULL)
            {
                switch (c->Type)
                {
                case stctExt:
                    validDataMask |= VALID_DATA_EXTENSION;
                    break;
                case stctSize:
                    validDataMask |= VALID_DATA_SIZE;
                    break;
                case stctDate:
                    validDataMask |= VALID_DATA_DATE;
                    break;
                case stctTime:
                    validDataMask |= VALID_DATA_TIME;
                    break;
                case stctType:
                    validDataMask |= VALID_DATA_TYPE;
                    break;
                }
                columns->Add(c);
                if (!columns->IsGood())
                {
                    err = TRUE;
                    delete c;
                    columns->ResetState();
                    break;
                }
            }
            else
            {
                err = TRUE;
                break;
            }
        }

        if (!err) // we have a copy of the columns
        {
            const CFtpTextCodec textCodec = ControlConnection->GetTextCodec();
            CFTPListingPluginDataInterface* dataIface = new CFTPListingPluginDataInterface(columns, TRUE,
                                                                                           validDataMask, isVMS,
                                                                                           textCodec);
            if (dataIface != NULL)
            {
                validDataMask |= dataIface->GetPLValidDataMask();
                DWORD* emptyCol = new DWORD[columns->Count]; // helper pre-allocated array for GetNextItemFromListing
                if (dataIface->IsGood() && emptyCol != NULL)
                {
                    CFTPParser* parser = serverType->CompiledParser;
                    if (parser == NULL)
                    {
                        parser = CompileParsingRules(HandleNULLStr(serverType->RulesForParsing), columns,
                                                     NULL, NULL, NULL);
                        serverType->CompiledParser = parser; // do not deallocate 'parser'; it is now in 'serverType'
                    }
                    if (parser != NULL)
                    {
                        BOOL needUpDir = FTPIsValidAndNotRootPath(GetFTPServerPathType(Path.c_str()), Path.c_str()); // TRUE while an up-dir ("..") still needs to be inserted

                        CFileData file;
                        const char* listing = PathListing->data();
                        const char* listingEnd = listing + (PathListingIsBroken ? 0 /* if the listing is not OK, use an empty listing instead */ : PathListing->size());
                        BOOL isDir = FALSE;

                        if (dir != NULL)
                        {
                            dir->SetValidData(validDataMask);
                            dir->SetFlags(SALDIRFLAG_CASESENSITIVE | SALDIRFLAG_IGNOREDUPDIRS); // probably unnecessary, but everything is treated as case-sensitive so this should be safe
                        }
                        parser->BeforeParsing(listing, listingEnd, PathListingDate.Year, PathListingDate.Month,
                                              PathListingDate.Day, PathListingIsIncomplete); // initialize the parser
                        while (parser->GetNextItemFromListing(&file, &isDir, dataIface, columns, &listing,
                                                              listingEnd, NULL, &err, emptyCol, textCodec))
                        {
                            BOOL dealloc = TRUE;
                            if (isDir && file.NameLen <= 2 &&
                                file.Name[0] == '.' && (file.Name[1] == 0 || file.Name[1] == '.')) // directories "." and ".."
                            {
                                if (needUpDir && file.Name[1] == '.') // directory ".."
                                {
                                    file.Hidden = FALSE;
                                    needUpDir = FALSE;
                                    if (dir != NULL)
                                    {
                                        if (dir->AddDir(NULL, file, NULL))
                                            dealloc = FALSE;
                                        else
                                            err = TRUE;
                                    }
                                }
                            }
                            else // add a normal file/directory to 'dir'
                            {
                                if (findName != NULL)
                                {
                                    if (caseSensitive && wcscmp(findName, file.Name) == 0 ||
                                        !caseSensitive && SalamanderGeneral->StrICmp(findName, file.Name) == 0)
                                    {
                                        if (isDir)
                                        {
                                            if (dirExists != NULL)
                                                *dirExists = TRUE;
                                        }
                                        else
                                        {
                                            if (fileExists != NULL)
                                                *fileExists = TRUE;
                                        }
                                    }
                                }

                                if (dir != NULL)
                                {
                                    if (isDir)
                                    {
                                        // add a directory to 'dir'
                                        if (dir->AddDir(NULL, file, NULL))
                                            dealloc = FALSE;
                                        else
                                            err = TRUE;
                                    }
                                    else
                                    {
                                        // add a file to 'dir'
                                        if (dir->AddFile(NULL, file, NULL))
                                            dealloc = FALSE;
                                        else
                                            err = TRUE;
                                    }
                                }
                            }
                            if (dealloc)
                            {
                                // release the file or directory data
                                dataIface->ReleasePluginData(file, isDir);
                                SalamanderGeneral->Free(file.Name);
                            }
                            if (err)
                                break;
                        }
                        if (needUpDir && dir != NULL) // still need to insert the up-dir ("..")
                            AddUpdir(err, needUpDir, dataIface, columns, dir);
                        if (!err && listing == listingEnd) // parsing finished successfully
                        {
                            if (pluginData != NULL)
                            {
                                *pluginData = dataIface;
                                dataIface = NULL; // will leave as the return value
                            }
                            ret = TRUE; // success
                        }
                        else
                        {
                            if (dir != NULL)
                                dir->Clear(dataIface); // otherwise release all allocated data
                        }
                    }
                    else
                        err = TRUE; // can only be a lack of memory
                }
                else
                {
                    if (emptyCol == NULL)
                        TRACE_E(LOW_MEMORY);
                    err = TRUE; // low memory
                }
                if (emptyCol != NULL)
                    delete[] emptyCol;
                columns = NULL; // they now live in 'dataIface'
                if (dataIface != NULL)
                    delete dataIface;
            }
            else
            {
                TRACE_E(LOW_MEMORY);
                err = TRUE; // low memory
            }
        }
    }
    else
    {
        if (columns == NULL)
            TRACE_E(LOW_MEMORY);
        err = TRUE; // low memory
    }
    *lowMem = err;
    if (columns != NULL)
        delete columns;
    return ret;
}

BOOL CPluginFSInterface::ListCurrentPath(CSalamanderDirectoryAbstract* dir,
                                         CPluginDataInterfaceAbstract*& pluginData,
                                         int& iconsType, BOOL forceRefresh)
{
    if (ControlConnection == NULL) // "always false"
    {
        TRACE_E("Unexpected situation in CPluginFSInterface::ListCurrentPath().");
        ErrorState = fesFatal;
        return FALSE; // fatal error
    }

    if (OverwritePathListing) // we have an old listing, free it (it must be overwritten by a new one)
    {
        OverwritePathListing = FALSE;
        if (PathListing.has_value())
            FTPSecureWipe(*PathListing);
        PathListing.reset();
        memset(&PathListingDate, 0, sizeof(PathListingDate));
        PathListingIsIncomplete = FALSE;
        PathListingIsBroken = FALSE;
        PathListingMayBeOutdated = FALSE;
        PathListingStartTime = 0;
    }

    std::string logBuf;
    if (FTPFormatString(logBuf, LoadStr(PathListing.has_value() ? IDS_LOGMSGLISTINGCACHEDPATH : IDS_LOGMSGLISTINGPATH), Path.c_str()))
        ControlConnection->LogMessage(logBuf.c_str(), -1, TRUE);

    //  if (PathListing.has_value() && forceRefresh && !NextRefreshCanUseOldListing)  // may happen when the user requests a hard refresh and refuses to reconnect - then the cached listing is used (if available)
    //    TRACE_E("Unexpected situation in CPluginFSInterface::ListCurrentPath() - cached refresh!");
    BOOL fatalError = FALSE;
    BOOL listingIsNotFromCache = !PathListing.has_value();
    if (PathListing.has_value() || // process the cached listing; otherwise list on the server...
        ControlConnection->ListWorkingPath(SalamanderGeneral->GetMsgBoxParent(),
                                           Path.c_str(), User, PathListing,
                                           &PathListingDate,
                                           &PathListingIsIncomplete, &PathListingIsBroken,
                                           &PathListingMayBeOutdated, &PathListingStartTime,
                                           forceRefresh, &TotalConnectAttemptNum, &fatalError,
                                           NextRefreshWontClearCache))
    {
        NextRefreshCanUseOldListing = FALSE;
        NextRefreshWontClearCache = FALSE;
        ControlConnection->ActivateWelcomeMsg(); // if any message box deactivated the welcome-msg window, activate it again
        if (PathListing.has_value())             // we have at least part of the listing, go parse it
        {
            CFTPServerPathType pathType = GetFTPServerPathType(Path.c_str());
            BOOL isVMS = pathType == ftpsptOpenVMS; // find out whether this might be a VMS listing

            BOOL needSimpleListing = TRUE;
            HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

            // so the user does not stare at the remains of the list-wait window during parsing
            UpdateWindow(SalamanderGeneral->GetMsgBoxParent());

            // after 3 seconds show the wait window "parsing listing, please wait..."
            SalamanderGeneral->CreateSafeWaitWindow(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_WAITWNDPARSINGLST).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                                    WAITWND_PARSINGLST, FALSE,
                                                    SalamanderGeneral->GetMsgBoxParent());

            char* welcomeReply = ControlConnection->AllocServerFirstReply();
            char* systReply = ControlConnection->AllocServerSystemReply();

            if (listingIsNotFromCache && !PathListingIsIncomplete && !PathListingIsBroken &&
                welcomeReply != NULL && systReply != NULL)
            { // update the cached listing uploaded earlier (replace the approximate listing with the real one)
                UploadListingCache.AddOrUpdateListing(User.c_str(), Host.c_str(), Port, Path.c_str(), pathType,
                                                      PathListing->data(), static_cast<int>(PathListing->size()),
                                                      PathListingDate, PathListingStartTime,
                                                      TRUE /* just update the listing */, welcomeReply, systReply,
                                                      AutodetectSrvType ? NULL : LastServerType.c_str(),
                                                      ControlConnection->GetTextCodec());
            }

            // Reset the helper variable indicating which server type has already been (unsuccessfully) tested
            CServerTypeList* serverTypeList = Config.LockServerTypeList();
            int serverTypeListCount = serverTypeList->Count;
            int j;
            for (j = 0; j < serverTypeListCount; j++)
                serverTypeList->At(j)->ParserAlreadyTested = FALSE;

            // look for LastServerType
            CServerType* serverType = NULL;
            BOOL err = FALSE;
            if (!LastServerType.empty())
            {
                int i;
                for (i = 0; i < serverTypeListCount; i++)
                {
                    serverType = serverTypeList->At(i);
                    const char* s = serverType->TypeName;
                    if (*s == '*')
                        s++;
                    const CFtpTextCompareStatus comparison = FtpCompareLocalTextNoCase(LastServerType, s);
                    if (comparison == CFtpTextCompareStatus::Failure)
                    {
                        err = TRUE;
                        break;
                    }
                    if (comparison == CFtpTextCompareStatus::Equal)
                    {
                        // serverType is selected, try its parser on the listing
                        serverType->ParserAlreadyTested = TRUE;
                        if (ParseListing(dir, &pluginData, serverType, &err, isVMS, NULL, FALSE, NULL, NULL))
                        {
                            needSimpleListing = FALSE; // successfully parsed the listing
                        }
                        break; // found the desired server type, stop
                    }
                }
                if (i == serverTypeListCount) // LastServerType does not exist -> fall back to autodetection
                {
                    AutodetectSrvType = TRUE;
                    LastServerType.clear();
                }
            }
            else
                AutodetectSrvType = TRUE; // probably redundant, just to be safe...

            // Autodetection - choose the server types whose autodetection condition is satisfied
            if (!err && needSimpleListing && AutodetectSrvType)
            {
                if (welcomeReply == NULL || systReply == NULL)
                    err = TRUE;
                else
                {
                    int welcomeReplyLen = (int)strlen(welcomeReply);
                    int systReplyLen = (int)strlen(systReply);
                    int i;
                    for (i = 0; i < serverTypeListCount; i++)
                    {
                        serverType = serverTypeList->At(i);
                        if (!serverType->ParserAlreadyTested) // only if we have not tried it yet
                        {
                            if (serverType->CompiledAutodetCond == NULL)
                            {
                                serverType->CompiledAutodetCond = CompileAutodetectCond(HandleNULLStr(serverType->AutodetectCond),
                                                                                        NULL, NULL, NULL, NULL);
                                if (serverType->CompiledAutodetCond == NULL) // can only fail due to lack of memory
                                {
                                    err = TRUE;
                                    break;
                                }
                            }
                            if (serverType->CompiledAutodetCond->Evaluate(welcomeReply, welcomeReplyLen,
                                                                          systReply, systReplyLen))
                            {
                                // serverType is selected, try its parser on the listing
                                serverType->ParserAlreadyTested = TRUE;
                                if (ParseListing(dir, &pluginData, serverType, &err, isVMS, NULL, FALSE, NULL, NULL) || err)
                                {
                                    if (!err)
                                    {
                                        const char* s = serverType->TypeName;
                                        if (*s == '*')
                                            s++;
                                        if (!FtpStoreLocalTextBytes(s, LastServerType))
                                            err = TRUE;
                                    }
                                    needSimpleListing = err; // successfully parsed the listing or ran into low memory, stop
                                    break;
                                }
                            }
                        }
                    }
                }

                // Autodetection - pick the remaining server types
                if (!err && needSimpleListing)
                {
                    int i;
                    for (i = 0; i < serverTypeListCount; i++)
                    {
                        serverType = serverTypeList->At(i);
                        if (!serverType->ParserAlreadyTested) // only if we have not tried it yet
                        {
                            // serverType is selected, try its parser on the listing
                            // serverType->ParserAlreadyTested = TRUE;  // unnecessary, not used afterwards
                            if (ParseListing(dir, &pluginData, serverType, &err, isVMS, NULL, FALSE, NULL, NULL) || err)
                            {
                                if (!err)
                                {
                                    const char* s = serverType->TypeName;
                                    if (*s == '*')
                                        s++;
                                    if (!FtpStoreLocalTextBytes(s, LastServerType))
                                        err = TRUE;
                                }
                                needSimpleListing = err; // successfully parsed the listing or ran into low memory, stop
                                break;
                            }
                        }
                    }
                }
            }
            Config.UnlockServerTypeList();

            if (welcomeReply != NULL)
                SalamanderGeneral->Free(welcomeReply);
            if (systReply != NULL)
                SalamanderGeneral->Free(systReply);

            if (!err)
            {
                if (needSimpleListing) // unknown listing; show a message about reporting the listing on the Sally issue tracker
                {                      // and log "Unknown Server Type"
                    if (FtpStoreProtocolBytes(LoadStr(AutodetectSrvType ? IDS_LOGMSGUNKNOWNSRVTYPE : IDS_LOGMSGUNKNOWNSRVTYPE2), logBuf))
                        ControlConnection->LogMessage(logBuf.c_str(), -1, TRUE);
                    if (InformAboutUnknownSrvType)
                    {
                        SalamanderGeneral->ShowMessageBox(SPLLoadStrOwned(SalamanderGeneral, HLanguage, AutodetectSrvType ? IDS_UNKNOWNSRVTYPEINFO : IDS_UNKNOWNSRVTYPEINFO2).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(), MSGBOX_INFO);
                        InformAboutUnknownSrvType = FALSE;
                    }
                }
                else // log which parser handled it
                {
                    if (!LastServerType.empty()) // "always true"
                    {
                        if (FTPFormatString(logBuf, LoadStr(IDS_LOGMSGPARSEDBYSRVTYPE), LastServerType.c_str()))
                            ControlConnection->LogMessage(logBuf.c_str(), -1, TRUE);
                    }
                }
            }

            if (!err && needSimpleListing)
            {
                CFileData file;
                pluginData = &SimpleListPluginDataInterface; // ATTENTION: the change may also affect obtaining the data interface in CPluginFSInterface::ChangeAttributes!
                dir->SetValidData(VALID_DATA_NONE);
                dir->SetFlags(SALDIRFLAG_CASESENSITIVE | SALDIRFLAG_IGNOREDUPDIRS); // probably unnecessary, but everything is treated as case-sensitive so this should be safe
                if (!PathListingIsBroken &&                                         // if the listing is not OK, rather use an empty listing
                    !PathListing->empty())
                {
                    char* beg = PathListing->data();
                    char* end = beg + PathListing->size();
                    char* s = beg;
                    int lines = 0;
                    while (s < end)
                    {
                        while (s < end && *s != '\r' && *s != '\n')
                            s++; // look for the end of the line
                        lines++;
                        if (s < end && *s == '\r')
                            s++;
                        if (s < end && *s == '\n')
                            s++;
                    }
                    int width = 1;
                    for (int value = lines; value >= 10; value /= 10)
                        ++width;

                    int line = 1;
                    s = beg;
                    const CFtpTextCodec rawListingCodec = ControlConnection->GetTextCodec();
                    while (beg < end)
                    {
                        while (s < end && *s != '\r' && *s != '\n')
                            s++; // look for the end of the line

                        std::wstring lineName;
                        try
                        {
                            lineName = SPLFormatStringOwned(L"%0*d", width, line);
                        }
                        catch (...)
                        {
                            err = TRUE;
                            break;
                        }
                        line++;
                        file.NameLen = static_cast<int>(lineName.size());
                        file.Name = (wchar_t*)SalamanderGeneral->Alloc((file.NameLen + 1) * sizeof(wchar_t));
                        if (file.Name == NULL)
                        {
                            err = TRUE;
                            break;
                        }
                        wcscpy_s(file.Name, file.NameLen + 1, lineName.c_str());

                        // Decode the negotiated server-byte row once. The simple-list
                        // data interface then owns semantic UTF-16 for its whole lifetime.
                        std::wstring rowText;
                        if (!FtpDecodeServerTextForPresentation(
                                rawListingCodec,
                                std::string_view(beg, static_cast<size_t>(s - beg)), rowText))
                        {
                            SalamanderGeneral->Free(file.Name);
                            err = TRUE;
                            break;
                        }
                        wchar_t* row = (wchar_t*)malloc((rowText.size() + 1) * sizeof(wchar_t));
                        if (row == NULL)
                        {
                            SalamanderGeneral->Free(file.Name);
                            err = TRUE;
                            break;
                        }
                        memcpy(row, rowText.c_str(), (rowText.size() + 1) * sizeof(wchar_t));
                        file.PluginData = (DWORD_PTR)row;
                        if (!dir->AddFile(NULL, file, NULL))
                        {
                            SalamanderGeneral->Free(file.Name);
                            free(row);
                            err = TRUE;
                            break;
                        }

                        // move to the start of the next line
                        if (s < end && *s == '\r')
                            s++;
                        if (s < end && *s == '\n')
                            s++;
                        beg = s;
                    }
                }

                if (!err && FTPIsValidAndNotRootPath(pathType, Path.c_str()))
                {
                    file.Name = SalamanderGeneral->DupStr(L"..");
                    if (file.Name == NULL)
                        err = TRUE;
                    else
                    {
                        file.NameLen = wcslen(file.Name);
                        file.PluginData = NULL;
                        if (!dir->AddDir(NULL, file, NULL))
                        {
                            SalamanderGeneral->Free(file.Name);
                            err = TRUE;
                        }
                    }
                }
                if (err)
                    dir->Clear(pluginData);
                iconsType = pitSimple;
            }
            else
                iconsType = pitFromRegistry;

            if (err)
                ErrorState = fesFatal;

            // hide the wait window "parsing listing, please wait..."
            SalamanderGeneral->DestroySafeWaitWindow();

            SetCursor(oldCur);
            ControlConnection->ActivateWelcomeMsg(); // if any message box deactivated the welcome-msg window, activate it again
            return !err;
        }
        else
            ErrorState = fesFatal; // low memory, already reported to trace
    }
    else
        ErrorState = fatalError ? fesFatal : fesInaccessiblePath; // the server reports a fatal error or simply that the path cannot be listed
    NextRefreshWontClearCache = FALSE;
    ControlConnection->ActivateWelcomeMsg(); // if any message box deactivated the welcome-msg window, activate it again
    return FALSE;
}
