// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "compat/legacy_host_api.h"
#include "compat/plugin_abi_routing.h"
#include "ui/IPrompter.h"
#include "common/IPathService.h"
#include "common/IRegistry.h"
#include "common/fsutil.h" // IsUNCPathW / IsUNCRootPathW
#include "common/unicode/helpers.h"
#include "common/unicode/PanelPathPolicy.h"
#include "plugins/shared/spl_vers.h"
#include "menu.h"
#include "cfgdlg.h"
#include "plugins.h"
#include "fileswnd.h"
#include "stswnd.h"
#include "mainwnd.h"
#include "zip.h"
#include "pack.h"
#include "cache.h"
#include <uxtheme.h>
#include "dialogs.h"

CPlugins Plugins;

static IRegistry* GetPluginsRegistry()
{
    return gRegistry != nullptr ? gRegistry : GetWin32Registry();
}

static PathResult GetPluginDllPathOwned(const std::wstring& dllName, std::wstring& path)
{
    const bool isUNC = dllName.size() >= 2 && dllName[0] == L'\\' && dllName[1] == L'\\';
    const bool isDrivePath = dllName.size() >= 2 && dllName[1] == L':';
    if (isUNC || isDrivePath)
    {
        path = dllName;
        return PathResult::Ok();
    }

    if (gPathService == NULL)
        return PathResult::Error(ERROR_INVALID_STATE);
    PathResult result = gPathService->GetModuleFileName(HInstance, path);
    if (!result.success)
        return result;
    const size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return PathResult::Error(ERROR_BAD_PATHNAME);
    path.resize(separator + 1);
    path.append(L"plugins\\");
    path.append(dllName);
    return PathResult::Ok();
}

// global "time" (counter) for obtaining the FS creation "time"
DWORD CPluginFSInterfaceEncapsulation::PluginFSTime = 1; // zero is used as the "uninitialized time"

// ****************************************************************************

int AlreadyInPlugin = 0;

void EnterPlugin()
{
    if (AlreadyInPlugin == 0)
    {
#ifdef _DEBUG
        // verification code for the CSalamanderGeneral::GetMsgBoxParent() method
        // if there's an issue, another window for PluginMsgBoxParent must be added,
        // if the following code is modified, it must also be updated in CSalamanderGeneral::GetMsgBoxParent
        HWND wnd = PluginProgressDialog != NULL ? PluginProgressDialog : PluginMsgBoxParent;
        if (!IsWindowEnabled(wnd))
        {
            TRACE_E("CSalamanderGeneral::GetMsgBoxParent() will return incorrect value (0x" << wnd << ")!");
        }
#endif // _DEBUG

        AllowChangeDirectory(FALSE); // we do not want the current directory to change automatically
        BeginStopRefresh(TRUE);      // we do not want panels to refresh while a plug-in is running
        AlreadyInPlugin = 1;
    }
    else
    {
        //    TRACE_I("Warning! Plugin was entered multiple times.");
        AlreadyInPlugin++;
    }
}

void LeavePlugin()
{
    if (AlreadyInPlugin == 1)
    {
        // precaution to avoid interrupting panel listing after each ESC in the plugin (mainly
        // when closing modal dialogs)
        WaitForESCReleaseBeforeTestingESC = TRUE;

        EndStopRefresh(TRUE, TRUE);
        AllowChangeDirectory(TRUE);
        AlreadyInPlugin = 0;

        if (MainWindow != NULL && MainWindow->NeedToResentDispachChangeNotif &&
            StopRefresh == 0) // if we haven't left stop-refresh mode yet, sending the message is pointless
        {
            MainWindow->NeedToResentDispachChangeNotif = FALSE;

            // post a request to broadcast messages about path changes
            HANDLES(EnterCriticalSection(&TimeCounterSection));
            int t1 = MyTimeCounter++;
            HANDLES(LeaveCriticalSection(&TimeCounterSection));
            PostMessage(MainWindow->HWindow, WM_USER_DISPACHCHANGENOTIF, 0, t1);
        }
    }
    else
    {
        if (AlreadyInPlugin == 0)
            TRACE_E("Unmatched call to LeavePlugin()!");
        else
            AlreadyInPlugin--;
    }
}

//
// ****************************************************************************
// CPluginFSInterfaceEncapsulation
//

BOOL CPluginFSInterfaceEncapsulation::IsFSNameFromSamePluginAsThisFS(const wchar_t* fsName, int& fsNameIndex)
{
    CALL_STACK_MESSAGE4("CPluginFSInterfaceEncapsulation::IsFSNameFromSamePluginAsThisFS(%ls,) (%ls v. %ls)",
                        fsName, DLLName, Version);

    return Plugins.AreFSNamesFromSamePlugin(PluginFSName.c_str(), fsName, fsNameIndex);
}

BOOL CPluginFSInterfaceEncapsulation::IsPathFromThisFS(const wchar_t* fsName, const wchar_t* fsUserPart)
{
    CALL_STACK_MESSAGE5("CPluginFSInterfaceEncapsulation::IsPathFromThisFS(%ls, %ls) (%ls v. %ls)",
                        fsName, fsUserPart, DLLName, Version);

    int fsNameIndex;
    if (IsFSNameFromSamePluginAsThisFS(fsName, fsNameIndex)) // is the FS name from the same plugin?
    {
        return IsOurPath(PluginFSNameIndex, fsNameIndex, fsUserPart); // does the user part match?
    }
    return FALSE; // not our path
}

BOOL CPluginFSInterfaceEncapsulation::GetCurrentPathW(std::wstring& userPart)
{
    CALL_STACK_MESSAGE3("CPluginFSInterfaceEncapsulation::GetCurrentPathW() (%ls v. %ls)",
                        DLLName, Version);
    CSalamanderStringBufferOwner owner;
    if (!owner.IsValid())
        return FALSE;
    EnterPlugin();
    const BOOL result = Interface->GetCurrentPath(owner.Buffer());
    LeavePlugin();
    std::wstring staged;
    if (!result || !owner.GetValue(staged))
        return FALSE;
    userPart.swap(staged);
    return TRUE;
}

BOOL CPluginFSInterfaceEncapsulation::GetFullNameW(CFileData& file, int isDir, std::wstring& fullName)
{
    CALL_STACK_MESSAGE4("CPluginFSInterfaceEncapsulation::GetFullNameW(, %d, ,) (%ls v. %ls)",
                        isDir, DLLName, Version);
    CSalamanderStringBufferOwner owner;
    if (!owner.IsValid())
        return FALSE;
    EnterPlugin();
    const BOOL result = Interface->GetFullName(file, isDir, owner.Buffer());
    LeavePlugin();
    std::wstring staged;
    if (!result || !owner.GetValue(staged))
        return FALSE;
    fullName.swap(staged);
    return TRUE;
}

BOOL CPluginFSInterfaceEncapsulation::GetFullFSPathW(HWND parent, const std::wstring& fsName,
                                                     std::wstring& path, BOOL& success)
{
    CALL_STACK_MESSAGE3("CPluginFSInterfaceEncapsulation::GetFullFSPathW() (%ls v. %ls)",
                        DLLName, Version);
    CSalamanderStringBufferOwner owner(path);
    if (!owner.IsValid())
    {
        success = FALSE;
        return FALSE;
    }
    EnterPlugin();
    const BOOL result = Interface->GetFullFSPath(parent, fsName.c_str(),
                                                  owner.Buffer(), success);
    LeavePlugin();
    if (!result || !success)
        return result;
    std::wstring staged;
    if (!owner.GetValue(staged))
    {
        success = FALSE;
        return FALSE;
    }
    path.swap(staged);
    return TRUE;
}

BOOL CPluginFSInterfaceEncapsulation::GetRootPathW(std::wstring& userPart)
{
    CALL_STACK_MESSAGE3("CPluginFSInterfaceEncapsulation::GetRootPathW() (%ls v. %ls)",
                        DLLName, Version);
    CSalamanderStringBufferOwner owner;
    if (!owner.IsValid())
        return FALSE;
    EnterPlugin();
    const BOOL result = Interface->GetRootPath(owner.Buffer());
    LeavePlugin();
    std::wstring staged;
    if (!result || !owner.GetValue(staged))
        return FALSE;
    userPart.swap(staged);
    return TRUE;
}

namespace
{
bool BuildPluginTargetPathValue(const std::wstring& targetPath,
                                const std::wstring* targetMask,
                                std::wstring& value)
{
    const size_t maskLength = targetMask != NULL ? targetMask->size() : 0;
    if (targetPath.size() > SIZE_MAX - maskLength - 2)
        return false;
    try
    {
        value = targetPath;
        value.push_back(L'\0');
        if (targetMask != NULL)
            value.append(*targetMask);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    catch (const std::length_error&)
    {
        return false;
    }
}

bool ReadPluginTargetPathValue(const std::wstring& value, std::wstring& targetPath)
{
    // 'value' comes from CSalamanderStringBufferOwner::GetValue(), which
    // constructs it from exactly Length characters - the plugin-reported
    // logical length, deliberately excluding the terminator it separately
    // verified is present. There is never an embedded NUL to find here: every
    // call, for every well-behaved plugin, made find(L'\0') return npos, so
    // assign(data, npos) threw length_error unconditionally and both
    // CopyOrMoveFromFS and CopyOrMoveFromDiskToFS always treated a normal
    // plugin-provided target path as a cancel.
    try
    {
        targetPath.assign(value.data(), value.size());
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    catch (const std::length_error&)
    {
        return false;
    }
}
} // namespace

BOOL CPluginFSInterfaceEncapsulation::CopyOrMoveFromFS(
    BOOL copy, int mode, const wchar_t* fsName, HWND parent, int panel,
    int selectedFiles, int selectedDirs, std::wstring& targetPath,
    const std::wstring* targetMask, BOOL& operationMask,
    BOOL& cancelOrHandlePath, HWND dropTarget)
{
    CALL_STACK_MESSAGE10("CPluginFSInterfaceEncapsulation::CopyOrMoveFromFS(%d, %d, %ls, , %d, %d, %d, %ls, , ,) (%ls v. %ls)",
                         copy, mode, fsName, panel, selectedFiles, selectedDirs,
                         targetPath.c_str(), DLLName, Version);
    if (!(copy && IsServiceSupported(FS_SERVICE_COPYFROMFS) ||
          !copy && IsServiceSupported(FS_SERVICE_MOVEFROMFS)))
    {
        cancelOrHandlePath = TRUE;
        return TRUE;
    }

    std::wstring pluginValue;
    if (!BuildPluginTargetPathValue(targetPath, targetMask, pluginValue))
    {
        cancelOrHandlePath = TRUE;
        return FALSE;
    }

    CSalamanderStringBufferOwner owner(pluginValue);
    if (!owner.IsValid())
    {
        cancelOrHandlePath = TRUE;
        return FALSE;
    }
    EnterPlugin();
    const BOOL result = Interface->CopyOrMoveFromFS(
        copy, mode, fsName, parent, panel, selectedFiles, selectedDirs,
        owner.Buffer(), operationMask, cancelOrHandlePath, dropTarget);
    LeavePlugin();
    std::wstring returnedValue;
    if (!owner.GetValue(returnedValue) ||
        !ReadPluginTargetPathValue(returnedValue, targetPath))
    {
        cancelOrHandlePath = TRUE;
        return FALSE;
    }
    return result;
}

BOOL CPluginFSInterfaceEncapsulation::CopyOrMoveFromDiskToFS(
    BOOL copy, int mode, const wchar_t* fsName, HWND parent,
    const wchar_t* sourcePath, SalEnumSelection2 next, void* nextParam,
    int sourceFiles, int sourceDirs, std::wstring& targetPath,
    BOOL* invalidPathOrCancel)
{
    CALL_STACK_MESSAGE9("CPluginFSInterfaceEncapsulation::CopyOrMoveFromDiskToFS(%d, %d, %ls, , %ls, , , %d, %d, ,) (%ls v. %ls)",
                        copy, mode, fsName, sourcePath, sourceFiles, sourceDirs,
                        DLLName, Version);
    if (!(copy && IsServiceSupported(FS_SERVICE_COPYFROMDISKTOFS) ||
          !copy && IsServiceSupported(FS_SERVICE_MOVEFROMDISKTOFS)))
    {
        if (mode == 1)
            return FALSE;
        SalMessageBoxW(parent, LoadStrW(IDS_FSCOPYMOVE_TOFS_NOTSUP),
                       LoadStrW(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
        if (invalidPathOrCancel != NULL)
            *invalidPathOrCancel = TRUE;
        return FALSE;
    }

    std::wstring pluginValue;
    if (!BuildPluginTargetPathValue(targetPath, NULL, pluginValue))
    {
        if (invalidPathOrCancel != NULL)
            *invalidPathOrCancel = TRUE;
        return FALSE;
    }
    CSalamanderStringBufferOwner owner(pluginValue);
    if (!owner.IsValid())
    {
        if (invalidPathOrCancel != NULL)
            *invalidPathOrCancel = TRUE;
        return FALSE;
    }

    EnterPlugin();
    const BOOL result = Interface->CopyOrMoveFromDiskToFS(
        copy, mode, fsName, parent, sourcePath, next, nextParam, sourceFiles,
        sourceDirs, owner.Buffer(), invalidPathOrCancel);
    LeavePlugin();
    std::wstring returnedValue;
    if (!owner.GetValue(returnedValue) ||
        !ReadPluginTargetPathValue(returnedValue, targetPath))
    {
        if (invalidPathOrCancel != NULL)
            *invalidPathOrCancel = TRUE;
        return FALSE;
    }
    return result;
}

BOOL CPluginFSInterfaceEncapsulation::CompleteDirectoryLineHotPathW(
    std::wstring& path)
{
    CALL_STACK_MESSAGE4("CPluginFSInterfaceEncapsulation::CompleteDirectoryLineHotPathW(%ls) (%ls v. %ls)",
                        path.c_str(), DLLName, Version);
    if (!IsServiceSupported(FS_SERVICE_GETNEXTDIRLINEHOTPATH))
        return FALSE;

    CSalamanderStringBufferOwner owner(path);
    if (!owner.IsValid())
        return FALSE;
    EnterPlugin();
    const BOOL result = Interface->CompleteDirectoryLineHotPath(owner.Buffer());
    LeavePlugin();
    if (!result)
        return FALSE;
    return owner.GetValue(path) ? TRUE : FALSE;
}

BOOL CPluginFSInterfaceEncapsulation::GetPathForMainWindowTitleW(
    const wchar_t* fsName, int mode, std::wstring& path)
{
    path.clear();
    if (!IsServiceSupported(FS_SERVICE_GETPATHFORMAINWNDTITLE))
        return FALSE;

    CSalamanderStringBufferOwner owner;
    if (!owner.IsValid())
        return FALSE;
    EnterPlugin();
    const BOOL result = Interface->GetPathForMainWindowTitle(
        fsName, mode, owner.Buffer());
    LeavePlugin();
    return result && owner.GetValue(path) ? TRUE : FALSE;
}

BOOL CPluginFSInterfaceEncapsulation::GetNoItemsInPanelText(std::wstring& text)
{
    CSalamanderStringBufferOwner owner;
    if (!owner.IsValid())
        return FALSE;
    EnterPlugin();
    const BOOL result = Interface->GetNoItemsInPanelText(owner.Buffer());
    LeavePlugin();
    return result && owner.GetValue(text) ? TRUE : FALSE;
}

BOOL CPluginFSInterfaceEncapsulation::IsCurrentPathW(int currentFSNameIndex, int fsNameIndex,
                                                     const std::wstring& userPart)
{
    CALL_STACK_MESSAGE5("CPluginFSInterfaceEncapsulation::IsCurrentPathW(%d, %d,) (%ls v. %ls)",
                        currentFSNameIndex, fsNameIndex, DLLName, Version);
    EnterPlugin();
    BOOL r = Interface->IsCurrentPath(currentFSNameIndex, fsNameIndex, userPart.c_str());
    LeavePlugin();
    return r;
}

BOOL CPluginFSInterfaceEncapsulation::IsOurPathW(int currentFSNameIndex, int fsNameIndex,
                                                 const std::wstring& userPart)
{
    CALL_STACK_MESSAGE5("CPluginFSInterfaceEncapsulation::IsOurPathW(%d, %d,) (%ls v. %ls)",
                        currentFSNameIndex, fsNameIndex, DLLName, Version);
    EnterPlugin();
    BOOL r = Interface->IsOurPath(currentFSNameIndex, fsNameIndex, userPart.c_str());
    LeavePlugin();
    return r;
}

BOOL CPluginFSInterfaceEncapsulation::ChangePathW(int currentFSNameIndex, std::wstring& fsName,
                                                  int fsNameIndex, const std::wstring& userPart,
                                                  std::wstring* cutFileName, BOOL* pathWasCut,
                                                  BOOL forceRefresh, int mode)
{
    CALL_STACK_MESSAGE7("CPluginFSInterfaceEncapsulation::ChangePathW(%d, , %d, , , , %d, %d) (%ls v. %ls)",
                        currentFSNameIndex, fsNameIndex, forceRefresh, mode, DLLName, Version);
    CSalamanderStringBufferOwner fsNameOwner(fsName);
    CSalamanderStringBufferOwner cutFileNameOwner;
    if (!fsNameOwner.IsValid() || (cutFileName != NULL && !cutFileNameOwner.IsValid()))
        return FALSE;
    EnterPlugin();
    BOOL r = Interface->ChangePath(currentFSNameIndex, fsNameOwner.Buffer(),
                                   fsNameIndex, userPart.c_str(),
                                   cutFileName != NULL ? cutFileNameOwner.Buffer() : NULL,
                                   pathWasCut, forceRefresh, mode);
    CALL_STACK_MESSAGE1("CPluginFSInterface::GetSupportedServices()");
    SupportedServices = Interface->GetSupportedServices();
    LeavePlugin();
    if (r)
    {
        std::wstring stagedFSName;
        std::wstring stagedCutFileName;
        if (!fsNameOwner.GetValue(stagedFSName) ||
            (cutFileName != NULL && !cutFileNameOwner.GetValue(stagedCutFileName)))
            return FALSE;
        fsName.swap(stagedFSName);
        if (cutFileName != NULL)
            cutFileName->swap(stagedCutFileName);
    }
    return r;
}

BOOL CPluginFSInterfaceEncapsulation::ListCurrentPath(CSalamanderDirectoryAbstract* dir,
                                                      CPluginDataInterfaceAbstract*& pluginData,
                                                      int& iconsType, BOOL forceRefresh)
{
    CALL_STACK_MESSAGE4("CPluginFSInterfaceEncapsulation::ListCurrentPath(, , , %d) (%ls v. %ls)",
                        forceRefresh, DLLName, Version);
    EnterPlugin();
    //TRACE_I("list path: begin");
    BOOL r = Interface->ListCurrentPath(dir, pluginData, iconsType, forceRefresh);
    //TRACE_I("list path: end");
#ifdef _DEBUG
    if (r && pluginData != NULL) // increase OpenedPDCounter
    {
        CPluginData* data = Plugins.GetPluginData(Iface);
        if (data != NULL)
            data->OpenedPDCounter++;
        else
            TRACE_E("Unexpected situation in CPluginFSInterfaceEncapsulation::ListCurrentPath()");
    }
#endif // _DEBUG
    CALL_STACK_MESSAGE1("CPluginFSInterface::GetSupportedServices()");
    SupportedServices = Interface->GetSupportedServices();
    LeavePlugin();
    return r;
}

BOOL CPluginFSInterfaceEncapsulation::GetChangeDriveOrDisconnectItem(const wchar_t* fsName, wchar_t*& title,
                                                                     HICON& icon, BOOL& destroyIcon)
{
    CALL_STACK_MESSAGE4("CPluginFSInterfaceEncapsulation::GetChangeDriveOrDisconnectItem(%ls, , ,) (%ls v. %ls)",
                        fsName, DLLName, Version);
    if (IsServiceSupported(FS_SERVICE_GETCHANGEDRIVEORDISCONNECTITEM))
    {
        EnterPlugin();
        BOOL r = Interface->GetChangeDriveOrDisconnectItem(fsName, title, icon, destroyIcon);
        if (r && icon != NULL && destroyIcon) // add the handle for 'icon' to HANDLES
            HANDLES_ADD(__htIcon, __hoLoadImage, icon);
        LeavePlugin();
        return r;
    }
    else
        return FALSE;
}

HICON
CPluginFSInterfaceEncapsulation::GetFSIcon(BOOL& destroyIcon)
{
    CALL_STACK_MESSAGE3("CPluginFSInterfaceEncapsulation::GetFSIcon() (%ls v. %ls)",
                        DLLName, Version);
    if (IsServiceSupported(FS_SERVICE_GETFSICON))
    {
        EnterPlugin();
        HICON r = Interface->GetFSIcon(destroyIcon);
        if (r != NULL && destroyIcon) // add the handle of the returned icon to HANDLES
            HANDLES_ADD(__htIcon, __hoLoadImage, r);
        LeavePlugin();
        return r;
    }
    else
        return NULL;
}

//
// ****************************************************************************
// CPluginInterfaceEncapsulation
//

void CPluginInterfaceForFSEncapsulation::CloseFS(CPluginFSInterfaceAbstract* fs)
{
    CPluginData* data = Plugins.GetPluginData(Interface);
    if (data == NULL || !data->GetLoaded())
    {
        TRACE_E("Incorrect call to CPluginInterfaceForFSEncapsulation::CloseFS()");
        return;
    }
#ifdef _DEBUG
    if (--data->OpenedFSCounter < 0)
    {
        TRACE_E("OpenedFSCounter is negative number - too much calls to CloseFS()");
    }
#endif // _DEBUG
    CALL_STACK_MESSAGE3("CPluginInterfaceForFSEncapsulation::CloseFS() (%ls v. %ls)",
                        data->DLLName.c_str(), data->Version.c_str());
    EnterPlugin();
    Interface->CloseFS(fs);
    Plugins.KillPluginFSTimer(fs, TRUE, 0); // we must remove timers of the closing FS (they wouldn't deliver and TRACE_E would appear)
    LeavePlugin();

    if (MainWindow != NULL)
    {
        MainWindow->ClearPluginFSFromHistory(fs);
        if (MainWindow->LeftPanel != NULL)
            MainWindow->LeftPanel->ClearPluginFSFromHistory(fs);
        if (MainWindow->RightPanel != NULL)
            MainWindow->RightPanel->ClearPluginFSFromHistory(fs);
    }
}

void CPluginInterfaceForFSEncapsulation::ExecuteChangeDriveMenuItem(int panel)
{
    EnterPlugin();
    Interface->ExecuteChangeDriveMenuItem(panel);
    LeavePlugin();
}

BOOL CPluginInterfaceForFSEncapsulation::ChangeDriveMenuItemContextMenu(HWND parent, int panel, int x, int y,
                                                                        CPluginFSInterfaceAbstract* pluginFS,
                                                                        const wchar_t* pluginFSName, int pluginFSNameIndex,
                                                                        BOOL isDetachedFS, BOOL& refreshMenu,
                                                                        BOOL& closeMenu, int& postCmd, void*& postCmdParam)
{
    EnterPlugin();
    BOOL r = Interface->ChangeDriveMenuItemContextMenu(parent, panel, x, y, pluginFS,
                                                       pluginFSName, pluginFSNameIndex,
                                                       isDetachedFS, refreshMenu,
                                                       closeMenu, postCmd, postCmdParam);
    LeavePlugin();
    return r;
}

void CPluginInterfaceForFSEncapsulation::ExecuteChangeDrivePostCommand(int panel, int postCmd, void* postCmdParam)
{
    CPluginData* data = Plugins.GetPluginData(Interface);
    if (data == NULL || !data->GetLoaded())
    {
        TRACE_E("Incorrect call to CPluginInterfaceForFSEncapsulation::ExecuteChangeDrivePostCommand()");
        return;
    }
    CALL_STACK_MESSAGE5("CPluginInterfaceForFSEncapsulation::ExecuteChangeDrivePostCommand(%d, %d, ) (%ls v. %ls)",
                        panel, postCmd, data->DLLName.c_str(), data->Version.c_str());
    EnterPlugin();
    Interface->ExecuteChangeDrivePostCommand(panel, postCmd, postCmdParam);
    LeavePlugin();
}

void CPluginInterfaceForFSEncapsulation::ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* pluginFS,
                                                     const wchar_t* pluginFSName, int pluginFSNameIndex,
                                                     CFileData& file, int isDir)
{
    CPluginData* data = Plugins.GetPluginData(Interface);
    if (data == NULL || !data->GetLoaded())
    {
        TRACE_E("Incorrect call to CPluginInterfaceForFSEncapsulation::ExecuteOnFS()");
        return;
    }
    CALL_STACK_MESSAGE7("CPluginInterfaceForFSEncapsulation::ExecuteOnFS(%d, , %ls, %d, , %d) (%ls v. %ls)",
                        panel, pluginFSName, pluginFSNameIndex, isDir, data->DLLName.c_str(), data->Version.c_str());
    EnterPlugin();
    Interface->ExecuteOnFS(panel, pluginFS, pluginFSName, pluginFSNameIndex, file, isDir);
    LeavePlugin();
}

BOOL CPluginInterfaceForFSEncapsulation::DisconnectFS(HWND parent, BOOL isInPanel, int panel,
                                                      CPluginFSInterfaceAbstract* pluginFS,
                                                      const wchar_t* pluginFSName, int pluginFSNameIndex)
{
    CPluginData* data = Plugins.GetPluginData(Interface);
    if (data == NULL || !data->GetLoaded())
    {
        TRACE_E("Incorrect call to CPluginInterfaceForFSEncapsulation::DisconnectFS()");
        return FALSE;
    }
    CALL_STACK_MESSAGE7("CPluginInterfaceForFSEncapsulation::DisconnectFS(, %d, %d, , %ls, %d) (%ls v. %ls)",
                        isInPanel, panel, pluginFSName, pluginFSNameIndex, data->DLLName.c_str(), data->Version.c_str());
    EnterPlugin();
    BOOL ret = Interface->DisconnectFS(parent, isInPanel, panel, pluginFS, pluginFSName, pluginFSNameIndex);
    LeavePlugin();
    return ret;
}

BOOL CPluginInterfaceForFSEncapsulation::ConvertPathToInternalW(const wchar_t* fsName, int fsNameIndex,
                                                                std::wstring& fsUserPart)
{
    CPluginData* data = Plugins.GetPluginData(Interface);
    if (data == NULL || !data->GetLoaded())
    {
        TRACE_E("Incorrect call to CPluginInterfaceForFSEncapsulation::ConvertPathToInternalW()");
        return FALSE;
    }

    CSalamanderStringBufferOwner owner(fsUserPart);
    if (!owner.IsValid())
        return FALSE;
    EnterPlugin();
    const BOOL result = Interface->ConvertPathToInternal(
        fsName, fsNameIndex, owner.Buffer());
    LeavePlugin();
    return result && owner.GetValue(fsUserPart) ? TRUE : FALSE;
}

BOOL CPluginInterfaceForFSEncapsulation::ConvertPathToExternalW(const wchar_t* fsName, int fsNameIndex,
                                                                std::wstring& fsUserPart)
{
    CPluginData* data = Plugins.GetPluginData(Interface);
    if (data == NULL || !data->GetLoaded())
    {
        TRACE_E("Incorrect call to CPluginInterfaceForFSEncapsulation::ConvertPathToExternalW()");
        return FALSE;
    }

    CSalamanderStringBufferOwner owner(fsUserPart);
    if (!owner.IsValid())
        return FALSE;
    EnterPlugin();
    const BOOL result = Interface->ConvertPathToExternal(
        fsName, fsNameIndex, owner.Buffer());
    LeavePlugin();
    return result && owner.GetValue(fsUserPart) ? TRUE : FALSE;
}

void CPluginInterfaceEncapsulation::ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData)
{
    CPluginData* data = Plugins.GetPluginData(Interface);
    if (data == NULL || !data->GetLoaded())
    {
        TRACE_E("Incorrect call to CPluginInterfaceEncapsulation::ReleasePluginDataInterface()");
        return;
    }
#ifdef _DEBUG
    if (--data->OpenedPDCounter < 0)
    {
        TRACE_E("OpenedPDCounter is negative number - too much calls to ReleasePluginDataInterface()");
    }
#endif // _DEBUG
    CALL_STACK_MESSAGE3("CPluginInterfaceEncapsulation::ReleasePluginDataInterface() (%ls v. %ls)",
                        data->DLLName.c_str(), data->Version.c_str());
    EnterPlugin();
    Interface->ReleasePluginDataInterface(pluginData);
    LeavePlugin();
}

//
// ****************************************************************************
// CPluginDataInterfaceEncapsulation
//

void CPluginDataInterfaceEncapsulation::ReleaseFilesOrDirs(CFilesArray* filesOrDirs, BOOL areDirs)
{
    SLOW_CALL_STACK_MESSAGE4("CPluginDataInterfaceEncapsulation::ReleaseFilesOrDirs(, %d) (%ls v. %ls)",
                             areDirs, DLLName, Version);
    EnterPlugin();
    int i;
    for (i = 0; i < filesOrDirs->Count; i++)
    {
        Interface->ReleasePluginData(filesOrDirs->At(i), areDirs);
    }
    LeavePlugin();
}

//
// ****************************************************************************
// CSalamanderDebug
//

void CSalamanderDebug::TraceAttachThread(HANDLE thread, unsigned tid)
{
#if defined(MULTITHREADED_TRACE_ENABLE) && defined(TRACE_ENABLE)
    HANDLE handle;
    if (NOHANDLES(DuplicateHandle(GetCurrentProcess(), thread, GetCurrentProcess(), // HANDLES cannot be used -> module
                                  &handle, 0, FALSE, DUPLICATE_SAME_ACCESS)))       // TRACE does not use HANDLES
    {
        HANDLES(EnterCriticalSection(&GetTrace().CriticalSection));
        if (!GetTrace().ThreadCache.Add(handle, tid))
            NOHANDLES(CloseHandle(handle));
        HANDLES(LeaveCriticalSection(&GetTrace().CriticalSection));
    }
#endif // defined(MULTITHREADED_TRACE_ENABLE) && defined(TRACE_ENABLE)
}

void CSalamanderDebug::TraceSetThreadName(const wchar_t* name)
{
    SetTraceThreadNameW(name);
}

void CSalamanderDebug::SetThreadNameInVC(const wchar_t* name)
{
    ::SetThreadNameInVC(name);
}

void CSalamanderDebug::SetThreadNameInVCAndTrace(const wchar_t* name)
{
    SetThreadNameInVC(name);
    SetTraceThreadNameW(name);
}

void CSalamanderDebug::TraceConnectToServer()
{
    ConnectToTraceServer();
}

void CSalamanderDebug::AddModuleWithPossibleMemoryLeaks(const wchar_t* fileName)
{
#ifdef _DEBUG
    ::AddModuleWithPossibleMemoryLeaks(fileName);
#endif // _DEBUG
}

void CSalamanderDebug::TraceI(const wchar_t* file, int line, const wchar_t* str)
{
    TRACE_MIW(file, line, str);
}

void CSalamanderDebug::TraceE(const wchar_t* file, int line, const wchar_t* str)
{
    TRACE_MEW(file, line, str);
}

unsigned CallWithCallStackEHBody(const wchar_t* dllName, const wchar_t* version,
                                 unsigned(WINAPI* threadBody)(void*), void* param)
{
    CALL_STACK_MESSAGE3("Plugin Thread (%ls v. %ls)", dllName, version);
    return threadBody(param);
}

unsigned
CSalamanderDebug::CallWithCallStackEH(unsigned(WINAPI* threadBody)(void*), void* param)
{
#ifndef CALLSTK_DISABLE
    __try
    {
#endif // CALLSTK_DISABLE
        return CallWithCallStackEHBody(DLLName, Version, threadBody, param);
#ifndef CALLSTK_DISABLE
    }
    __except (CCallStack::HandleException(GetExceptionInformation()))
    {
        TRACE_I("Thread address " << threadBody << ": calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // harder exit (this call still performs some operations)
        return 1;
    }
#endif // CALLSTK_DISABLE
}

unsigned
CSalamanderDebug::CallWithCallStack(unsigned(WINAPI* threadBody)(void*), void* param)
{
#ifndef CALLSTK_DISABLE
    CCallStack stack;
#endif // CALLSTK_DISABLE
    return CallWithCallStackEH(threadBody, param);
}

// so that we don't include the entire intrin.h
extern "C"
{
    void* _AddressOfReturnAddress(void);
}

void CSalamanderDebug::Push(const wchar_t* format, va_list args, CCallStackMsgContext* callStackMsgContext,
                            BOOL doNotMeasureTimes)
{
#ifndef CALLSTK_DISABLE
    CCallStack* stack = CCallStack::GetThis();
    if (stack != NULL)
    {
#if (defined(_DEBUG) || defined(CALLSTK_MEASURETIMES)) && !defined(CALLSTK_DISABLEMEASURETIMES)
        LARGE_INTEGER pushTime;
        QueryPerformanceCounter(&pushTime);
#endif                             // (defined(_DEBUG) || defined(CALLSTK_MEASURETIMES)) && !defined(CALLSTK_DISABLEMEASURETIMES)
        stack->Push(format, args); // unhandled threads have TLS set to NULL
#if (defined(_DEBUG) || defined(CALLSTK_MEASURETIMES)) && !defined(CALLSTK_DISABLEMEASURETIMES)
        if (callStackMsgContext != NULL)
        {
            QueryPerformanceCounter(&callStackMsgContext->StartTime);
            callStackMsgContext->PushesCounterStart = stack->PushesCounter;
            if (!doNotMeasureTimes)
                stack->PushPerfTimeCounter.QuadPart += callStackMsgContext->StartTime.QuadPart - pushTime.QuadPart;
            else
                stack->IgnoredPushPerfTimeCounter.QuadPart += callStackMsgContext->StartTime.QuadPart - pushTime.QuadPart;
            callStackMsgContext->PushPerfTimeCounterStart.QuadPart = stack->PushPerfTimeCounter.QuadPart;
            callStackMsgContext->IgnoredPushPerfTimeCounterStart.QuadPart = stack->IgnoredPushPerfTimeCounter.QuadPart;

            __try
            {
                callStackMsgContext->PushCallerAddress = *(DWORD_PTR*)_AddressOfReturnAddress();
                if (!doNotMeasureTimes)
                {
                    stack->CheckCallFrequency(callStackMsgContext->PushCallerAddress,
                                              &pushTime, &callStackMsgContext->StartTime);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                callStackMsgContext->PushCallerAddress = 0;
            }
        }
        else
        {
            TRACE_I("CSalamanderDebug::Push(): callStackMsgContext == NULL! (not DEBUG version or missing macro CALLSTK_MEASURETIMES)");
        }
#endif // (defined(_DEBUG) || defined(CALLSTK_MEASURETIMES)) && !defined(CALLSTK_DISABLEMEASURETIMES)
        stack->PushPluginDLLName(DLLName);
    }
    else
    {
        TRACE_EW(L"Invalid use of CALL_STACK_MESSAGE: call-stack object was not defined in this thread. Format=\"" << format << L"\"");
#if (defined(_DEBUG) || defined(CALLSTK_MEASURETIMES)) && !defined(CALLSTK_DISABLEMEASURETIMES)
        if (callStackMsgContext != NULL)
        {
            callStackMsgContext->PushesCounterStart = 0;
            callStackMsgContext->StartTime.QuadPart = 0;
            callStackMsgContext->PushCallerAddress = 0;
            callStackMsgContext->PushPerfTimeCounterStart.QuadPart = 0;
            callStackMsgContext->IgnoredPushPerfTimeCounterStart.QuadPart = 0;
        }
#endif // (defined(_DEBUG) || defined(CALLSTK_MEASURETIMES)) && !defined(CALLSTK_DISABLEMEASURETIMES)
    }
    va_end(args);
#endif // CALLSTK_DISABLE
}

void CSalamanderDebug::Pop(CCallStackMsgContext* callStackMsgContext)
{
#ifndef CALLSTK_DISABLE
    CCallStack* stack = CCallStack::GetThis();
    if (stack != NULL)
    {
#if (defined(_DEBUG) || defined(CALLSTK_MEASURETIMES)) && !defined(CALLSTK_DISABLEMEASURETIMES)
        BOOL printCallStackTop = FALSE;
        if (callStackMsgContext != NULL)
        {
            __int64 internalPushPerfTime = stack->PushPerfTimeCounter.QuadPart - callStackMsgContext->PushPerfTimeCounterStart.QuadPart;
            if (internalPushPerfTime > 0) // only if there were nested Push calls
            {
                LARGE_INTEGER endTime;
                QueryPerformanceCounter(&endTime);
                __int64 realPerfTime = (endTime.QuadPart - callStackMsgContext->StartTime.QuadPart) -
                                       (stack->IgnoredPushPerfTimeCounter.QuadPart - callStackMsgContext->IgnoredPushPerfTimeCounterStart.QuadPart); // subtract times of ignored (unmeasured) Pushes to avoid artificially improving the ratio
                if (realPerfTime / internalPushPerfTime < CALLSTK_MINRATIO && realPerfTime > 0 &&
                    ((realPerfTime * 1000) / CCallStack::SavedPerfFreq.QuadPart) > CALLSTK_MINWARNTIME)
                {
                    DWORD aproxMinCallsCount = (DWORD)(((CCallStack::SpeedBenchmark * (internalPushPerfTime + stack->IgnoredPushPerfTimeCounter.QuadPart - callStackMsgContext->IgnoredPushPerfTimeCounterStart.QuadPart) * 1000) /
                                                        (CALLSTK_BENCHMARKTIME * CCallStack::SavedPerfFreq.QuadPart)) /
                                                       3); // assume 3x slower than the measured call-stack macro
                    if (CCallStack::SpeedBenchmark != 0 && aproxMinCallsCount < stack->PushesCounter - callStackMsgContext->PushesCounterStart)
                    { // suppress cases of increased time when rescheduling a process/thread (can measure 50ms for 280 call-stack calls)
                        TRACE_E("Call Stack Messages Slowdown Detected: time ratio callstack/total: " << (DWORD)(100 * internalPushPerfTime / realPerfTime) << "%, total time: " << (DWORD)((realPerfTime * 1000) / CCallStack::SavedPerfFreq.QuadPart) << "ms, push time: " << (DWORD)(internalPushPerfTime * 1000 / CCallStack::SavedPerfFreq.QuadPart) << "ms, ignored pushes: " << (DWORD)((stack->IgnoredPushPerfTimeCounter.QuadPart - callStackMsgContext->IgnoredPushPerfTimeCounterStart.QuadPart) * 1000 / CCallStack::SavedPerfFreq.QuadPart) << "ms, call address: 0x" << std::hex << callStackMsgContext->PushCallerAddress << std::dec << " (see next line in trace-server for text), count: " << (stack->PushesCounter - callStackMsgContext->PushesCounterStart));
                        printCallStackTop = TRUE;
                    }
                }
            }
        }
        else
        {
            TRACE_I("CSalamanderDebug::Pop(): callStackMsgContext == NULL! (not DEBUG version or missing macro CALLSTK_MEASURETIMES)");
        }
        stack->Pop(printCallStackTop); // unhandled threads have TLS set to NULL
#else                                  // (defined(_DEBUG) || defined(CALLSTK_MEASURETIMES)) && !defined(CALLSTK_DISABLEMEASURETIMES)
        stack->Pop(); // unhandled threads have TLS set to NULL
#endif                                 // (defined(_DEBUG) || defined(CALLSTK_MEASURETIMES)) && !defined(CALLSTK_DISABLEMEASURETIMES)
        stack->PopPluginDLLName();
    }
    else
    {
        TRACE_E("Invalid use of CALL_STACK_MESSAGE: call-stack object was not defined in this thread.");
    }
#endif // CALLSTK_DISABLE
}

//
// ****************************************************************************
// CSalamanderConnect
//

void CSalamanderConnect::AddCustomPacker(const wchar_t* title, const wchar_t* defaultExtension, BOOL update)
{
    CALL_STACK_MESSAGE4("CSalamanderConnect::AddCustomPacker(%ls, %ls, %d)", title, defaultExtension, update);
    if (CustomPack)
    {
        int i = PackerConfig.AddPacker(TRUE);
        if (i != -1)
            PackerConfig.SetPacker(i, -Index - 1, title, defaultExtension, FALSE);
    }
    else
    {
        if (update)
        {
            int i;
            for (i = 0; i < PackerConfig.GetPackersCount(); i++)
            {
                if (PackerConfig.GetPackerType(i) == -Index - 1) // found, restore the data
                {
                    PackerConfig.SetPacker(i, -Index - 1, title, defaultExtension, FALSE);
                    return;
                }
            }
        }
    }
}

void CSalamanderConnect::AddCustomUnpacker(const wchar_t* title, const wchar_t* masks, BOOL update)
{
    CALL_STACK_MESSAGE4("CSalamanderConnect::AddCustomUnpacker(%ls, %ls, %d)", title, masks, update);
    if (CustomUnpack)
    {
        int i = UnpackerConfig.AddUnpacker(TRUE);
        if (i != -1)
            UnpackerConfig.SetUnpacker(i, -Index - 1, title, masks, FALSE);
    }
    else
    {
        if (update)
        {
            int i;
            for (i = 0; i < UnpackerConfig.GetUnpackersCount(); i++)
            {
                if (UnpackerConfig.GetUnpackerType(i) == -Index - 1) // found, restore the data
                {
                    UnpackerConfig.SetUnpacker(i, -Index - 1, title, masks, FALSE);
                    return;
                }
            }
        }
    }
}

void CSalamanderConnect::AddViewer(const wchar_t* masks, BOOL force)
{
    CALL_STACK_MESSAGE3("CSalamanderConnect::AddViewer(%ls, %d)", masks, force);
    if (wcschr(masks, L'|') != NULL)
    {
        TRACE_E("CSalamanderConnect::AddViewer(): you can not use character '|', sorry"); // '|' acts as negation in group masks; merging masks in GetViewersAssoc can't handle it
        return;
    }
    if (Viewer || force)
    {
        wchar_t ext[300];        // copy of masks (replace ';' with '\0')
        wchar_t ext2[300];       // used to split found masks (replace ';' with '\0'); also stores the "force" result
        if (!Viewer && force) // this is an update, not an installation, so check whether it is already on the list
        {
            int len = (int)wcslen(masks);
            if (len > 299)
                len = 299;
            wmemcpy(ext, masks, len);
            ext[len] = 0;
            TDirectArray<wchar_t*> extArray(10, 5); // array of extensions from masks
            wchar_t* s = ext + len;
            while (s > ext)
            {
                while (--s >= ext)
                {
                    if (*s == L';')
                    {
                        wchar_t* p = s;
                        while (--p >= ext && *p == L';')
                            ;
                        if (((s - p) & 1) == 1)
                            break;
                        s = p + 1;
                    }
                }
                if (s >= ext)
                    *s = 0;
                wchar_t* ss = s + 1 + wcslen(s + 1);
                while (--ss >= s + 1 && *ss <= L' ')
                    ;
                *(ss + 1) = 0; // trim the spaces at the end of the mask
                ss = s + 1;
                while (*ss != 0 && *ss <= L' ')
                    ss++;     // skip spaces at the beginning of the mask
                if (*ss != 0) // if the mask is not empty, add it to the array
                {
                    extArray.Insert(0, ss); // one of the extensions series, O(n^2) to preserve the order
                    if (!extArray.IsGood())
                        extArray.ResetState();
                }
            }

            // trim the list of extensions (masks) by removing those already registered...
            int i;
            for (i = 0; i < MainWindow->ViewerMasks->Count; i++)
            {
                if (MainWindow->ViewerMasks->At(i)->ViewerType == -Index - 1) // correct plug-in
                {
                    const wchar_t* m = MainWindow->ViewerMasks->At(i)->Masks->GetMasksString();
                    len = (int)wcslen(m);
                    if (len > 299)
                        len = 299;
                    wmemcpy(ext2, m, len);
                    ext2[len] = 0;
                    s = ext2 + len;
                    while (s > ext2)
                    {
                        while (--s >= ext2)
                        {
                            if (*s == L';')
                            {
                                wchar_t* p = s;
                                while (--p >= ext2 && *p == L';')
                                    ;
                                if (((s - p) & 1) == 1)
                                    break;
                                s = p + 1;
                            }
                        }
                        if (s >= ext2)
                            *s = 0;
                        wchar_t* ss = s + 1 + wcslen(s + 1);
                        while (--ss >= s + 1 && *ss <= L' ')
                            ;
                        *(ss + 1) = 0; // trim spaces at the end of the mask
                        ss = s + 1;
                        while (*ss != 0 && *ss <= L' ')
                            ss++; // skip spaces at the beginning of the mask
                        int k;
                        for (k = 0; k < extArray.Count; k++)
                        {
                            if (StrICmpW(ss, extArray[k]) == 0) // we already have this mask, don't add it
                            {
                                extArray.Delete(k);
                                if (!extArray.IsGood())
                                    extArray.ResetState();
                                k--;
                            }
                        }
                    }
                }
            }
            if (extArray.Count == 0)
                return; // empty mask -> nothing to do
            // rejoin the mask 'masks' (the useful remainder)
            s = ext2;
            int k;
            for (k = 0; k < extArray.Count; k++)
            {
                if (extArray[k][0] == L';' && s != ext2)
                    *s++ = L' '; // space is necessary (otherwise the previous ';' wouldn't act as a separator but will merge with this ';')
                wcscpy(s, extArray[k]);
                if (k + 1 < extArray.Count)
                    wcscat(s, L";");
                s += wcslen(s);
            }
            masks = ext2;
        }

        if (Viewer && !force || // plug-in installation
            !Viewer && force)   // plug-in update, but not during its installation
        {
            CViewerMasksItem* item = new CViewerMasksItem(masks, L"", L"", L"", -Index - 1, FALSE);
            if (item != NULL && item->IsGood())
            {
                MainWindow->EnterViewerMasksCS();
                MainWindow->ViewerMasks->Insert(0, item);
                if (MainWindow->ViewerMasks->IsGood())
                    item = NULL;
                else
                    MainWindow->ViewerMasks->ResetState();
                MainWindow->LeaveViewerMasksCS();
            }
            if (item != NULL)
                delete item;
        }
    }
}

// Rewritten wide, replacing the CP_ACP-limited 256-entry LowerCase[]
// table (common/str.h) with towlower - that table indexed by a wide code unit is
// pattern #14 (a narrow lookup table indexed by a wide character), and would have
// read out of bounds for anything above U+00FF. Sole definition, sole caller
// (ForceRemoveViewer below), both local to this file - safe to widen outright.
int StrICmpIgnoreSpacesOnStartAndEnd(const wchar_t* s1, const wchar_t* s2)
{
    while (*s1 != 0 && *s1 <= L' ')
        s1++;
    while (*s2 != 0 && *s2 <= L' ')
        s2++;
    while (*s1 != 0 && towlower(*s1) == towlower(*s2))
    {
        s1++;
        s2++;
    }
    while (*s1 != 0 && *s1 <= L' ')
        s1++;
    while (*s2 != 0 && *s2 <= L' ')
        s2++;
    if (*s1 == 0 && *s2 == 0)
        return 0;
    if ((unsigned)towlower(*s1) < (unsigned)towlower(*s2))
        return -1;
    else
        return 1;
}

void CSalamanderConnect::ForceRemoveViewer(const wchar_t* mask)
{
    CALL_STACK_MESSAGE2("CSalamanderConnect::ForceRemoveViewer(%ls)", mask);
    wchar_t ext2[300]; // used to split found masks (replace ';' with '\0')
    int i;
    for (i = 0; i < MainWindow->ViewerMasks->Count; i++)
    {
        if (MainWindow->ViewerMasks->At(i)->ViewerType == -Index - 1) // correct plug-in
        {
            const wchar_t* m = MainWindow->ViewerMasks->At(i)->Masks->GetMasksString();
            int len = (int)wcslen(m);
            if (len > 299)
                len = 299;
            wmemcpy(ext2, m, len);
            ext2[len] = 0;
            wchar_t* s = ext2 + len;
            // find and eliminate the extension 'mask', side effect of removing the ';' (replacing it with 0)
            while (s > ext2)
            {
                while (--s >= ext2)
                {
                    if (*s == L';')
                    {
                        wchar_t* p = s;
                        while (--p >= ext2 && *p == L';')
                            ;
                        if (((s - p) & 1) == 1)
                            break;
                        s = p + 1;
                    }
                }
                if (s >= ext2)
                    *s = 0;
                if (StrICmpIgnoreSpacesOnStartAndEnd(s + 1, mask) == 0) // we are looking for this mask, we will delete it
                {
                    int sLen = (int)wcslen(s + 1);
                    wmemmove(s + 1, s + 1 + sLen + 1, len - ((s + 1) - ext2) - sLen);
                    if (len > sLen + 1)
                        len -= sLen + 1;
                    else
                        ext2[(len = 0)] = 0;
                }
            }
            // restore ';'
            s = ext2;
            while (s - ext2 < len)
            {
                if (*s == 0)
                    *s = L';';
                s++;
            }
            if (ext2[0] != 0) // mask changed
            {
                if (wcscmp(ext2, m) != 0)
                    MainWindow->ViewerMasks->At(i)->Set(ext2, L"", L"", L"");
            }
            else // entry removed (last mask deleted)
            {
                MainWindow->EnterViewerMasksCS();
                MainWindow->ViewerMasks->Delete(i);
                if (!MainWindow->ViewerMasks->IsGood())
                    MainWindow->ViewerMasks->ResetState();
                MainWindow->LeaveViewerMasksCS();
                i--;
            }
        }
    }
}

void CSalamanderConnect::AddPanelArchiver(const wchar_t* extensions, BOOL edit, BOOL updateExts)
{
    CALL_STACK_MESSAGE3("CSalamanderConnect::AddPanelArchiver(%ls, %d)", extensions, edit);

    if (!PanelView && (!edit || !PanelEdit) && !updateExts)
        return; // nothing to do (neither a plug-in upgrade nor extension update)

    wchar_t ext[300]; // copy of extensions (replace ';' with '\0')
    int len = (int)wcslen(extensions);
    if (len > 299)
        len = 299;
    wmemcpy(ext, extensions, len);
    ext[len] = 0;
    TDirectArray<wchar_t*> extArray(10, 5); // array of extensions from "extensions"
    wchar_t* s = ext + len;
    while (s > ext)
    {
        while (--s >= ext && *s != L';')
            ;
        if (s >= ext)
            *s = 0;
        extArray.Insert(0, s + 1); // one of the extensions series, O(n^2) to preserve the order
        if (!extArray.IsGood())
            extArray.ResetState();
    }

    int index = -1; // index of the desired intersection of extensions or a record where the plugin provides at least
                    // "view" when updating extensions (the plugin extends/modifies an existing record)
    wchar_t ext2[300]; // copy of the extension from PackerFormatConfig (replace ';' with '\0')
    int i;
    for (i = 0; i < PackerFormatConfig.GetFormatsCount(); i++)
    {
        BOOL found = FALSE; // TRUE if this plugin provides at least "view" during extension update
        if (updateExts)     // when updating extensions
        {
            if (PackerFormatConfig.GetUnpackerIndex(i) == -Index - 1) // and if the plug-in is configured at least for "view"
            {
                found = TRUE;
            }
            else
                continue; // this is an extension upgrade; we don't look for intersections
        }

        len = (int)wcslen(PackerFormatConfig.GetExt(i));
        if (len > 299)
            len = 299;
        wmemcpy(ext2, PackerFormatConfig.GetExt(i), len);
        ext2[len] = 0;
        s = ext2 + len;
        while (s > ext2)
        {
            while (--s >= ext2 && *s != L';')
                ;
            if (s >= ext2)
                *s = 0;
            int j;
            for (j = 0; j < extArray.Count; j++)
            {
                if (found || StrICmpW(s + 1, extArray[j]) == 0) // upgrade or extension sets have a non-empty intersection
                {
                    index = i;

                    if (!found || StrICmpW(s + 1, extArray[j]) == 0) // only if the extensions match
                    {
                        extArray.Delete(j); // it's already in ext2, so it doesn't need to be in ext
                        if (!extArray.IsGood())
                            extArray.ResetState();
                    }
                    // scan the rest of ext2 and remove matching extensions from ext
                    BOOL firstRound = TRUE;
                    while (s > ext2)
                    {
                        if (!firstRound || !found)
                        {
                            while (--s >= ext2 && *s != L';')
                                ;
                            if (s >= ext2)
                                *s = 0;
                        }
                        for (j = 0; j < extArray.Count; j++)
                        {
                            if (StrICmpW(s + 1, extArray[j]) == 0) // another identical extension
                            {
                                extArray.Delete(j); // it's already in ext2, so it doesn't need to be in ext
                                if (!extArray.IsGood())
                                    extArray.ResetState();
                                break;
                            }
                        }
                        firstRound = FALSE;
                    }
                    break;
                }
            }
        }
        if (index != -1)
            break; // index found
    }

    BOOL newItem = FALSE;
    if (index == -1) // extensions contain brand new types of extensions; we may add a new entry later
    {
        newItem = TRUE;
        ext2[0] = 0;
    }
    else
        wcscpy(ext2, PackerFormatConfig.GetExt(index));

    // in ext2, we prepare the union of the old extensions and the new extensions, or only the new extensions (if applicable)
    len = (int)wcslen(ext2);
    if (len > 0 && ext2[len - 1] == L';')
        len--;
    for (i = 0; i < extArray.Count; i++)
    {
        int len2 = (int)wcslen(extArray[i]);
        if (len + len2 + 1 <= 300) // add the extension to ext2 if it fits
        {
            if (len != 0)
                ext2[len] = L';';
            else
                len--;
            wcscpy(ext2 + len + 1, extArray[i]);
            len += len2 + 1;
        }
    }

    BOOL usePacker;
    int packerIndex;
    int unpackerIndex;
    if (!newItem)
    {
        if (!PackerFormatConfig.GetUsePacker(index))
            usePacker = FALSE;
        else
        {
            usePacker = TRUE;
            packerIndex = PackerFormatConfig.GetPackerIndex(index);
        }
        unpackerIndex = PackerFormatConfig.GetUnpackerIndex(index);
    }
    else
        usePacker = FALSE;

    BOOL change = FALSE; // any changes in the view and/or edit settings?
    if (PanelView && edit && PanelEdit)
    {
        packerIndex = unpackerIndex = -Index - 1; // view & edit
        usePacker = TRUE;
        change = TRUE;
    }
    else
    {
        if (PanelView) // view
        {
            /*    // commented out because otherwise, in the plugin's ::Connect call to AddPanelArchiver,
      // it is enough to set the parameter 'edit'==TRUE and even a plugin that does not provide panel edit
      // would be marked here as "panel edit" for the given extensions, which is of course wrong
      if (newItem && edit)
      {
        packerIndex = -Index - 1;
        usePacker = TRUE;
      }
*/
            unpackerIndex = -Index - 1;
            change = TRUE;
        }
        else
        {
            if (edit && PanelEdit) // edit
            {
                usePacker = TRUE;
                packerIndex = -Index - 1;
                change = TRUE;
            }
            //      if (newItem)     // the user probably does not want it, so we will not add it automatically
            //      {
            //        unpackerIndex = -Index - 1;
            //        change = TRUE;
            //      }
        }
    }
    if (updateExts && newItem) // all plugin entries were removed by the user; add at least the new ones
    {
        CPluginData* p = Plugins.Get(Index);
        if (p != NULL && p->SupportPanelView)
        {
            packerIndex = unpackerIndex = -Index - 1; // view and possibly an edit
            usePacker = p->SupportPanelEdit;
            change = TRUE;
        }
    }
    if (updateExts && !newItem &&          // update the extension list (only if the record exists)
            -Index - 1 == unpackerIndex || // and this plugin provides at least "view" (otherwise updating extensions makes no sense)
        change)                            // only when there is a change (otherwise unwanted auto-adding
    {                                      // of supported extensions would occur)
        if (newItem)                       // a new record must be added
        {
            index = PackerFormatConfig.AddFormat();
            if (index == -1)
                return; // error
        }
        PackerFormatConfig.SetFormat(index, ext2, usePacker, usePacker ? packerIndex : -1, unpackerIndex, FALSE);
        PackerFormatConfig.BuildArray();
    }
}

void CSalamanderConnect::ForceRemovePanelArchiver(const wchar_t* extension)
{
    CALL_STACK_MESSAGE2("CSalamanderConnect::ForceRemovePanelArchiver(%ls)", extension);
    BOOL needBuild = FALSE;

NEXT_ROUND:

    for (int i = 0; i < PackerFormatConfig.GetFormatsCount(); i++)
    {
        if (PackerFormatConfig.GetUnpackerIndex(i) == -Index - 1) // if the plug-in is configured at least for "view"
        {
            wchar_t ext[300];
            lstrcpynW(ext, PackerFormatConfig.GetExt(i), _countof(ext));
            wchar_t* s = ext + wcslen(ext);
            wchar_t* extEnd = NULL;
            while (s > ext)
            {
                while (--s >= ext && *s != L';')
                    ;
                if (extEnd != NULL)
                    *extEnd = 0;
                if (StrICmpW(s + 1, extension) == 0) // the searched extension found
                {
                    if (s < ext)
                    {
                        if (extEnd != NULL)
                            wmemmove(ext, extEnd + 1, wcslen(extEnd + 1) + 1);
                        else
                            ext[0] = 0;
                    }
                    else
                    {
                        if (extEnd != NULL)
                            wmemmove(s + 1, extEnd + 1, wcslen(extEnd + 1) + 1);
                        else
                            *s = 0;
                    }
                    if (ext[0] != 0)
                    {
                        PackerFormatConfig.SetFormat(i, ext,
                                                     PackerFormatConfig.GetUsePacker(i),
                                                     PackerFormatConfig.GetPackerIndex(i),
                                                     PackerFormatConfig.GetUnpackerIndex(i),
                                                     PackerFormatConfig.GetOldType(i));
                    }
                    else
                        PackerFormatConfig.DeleteFormat(i);
                    needBuild = TRUE;
                    goto NEXT_ROUND; // restart the entire search (minor issue = no point in optimizations)
                }
                if (extEnd != NULL)
                    *extEnd = L';';
                extEnd = s;
            }
        }
    }
    if (needBuild)
        PackerFormatConfig.BuildArray();
}

void CSalamanderConnect::AddMenuItem(int iconIndex, const wchar_t* name, DWORD hotKey, int id, BOOL callGetState,
                                     DWORD state_or, DWORD state_and, DWORD skillLevel)
{
    CALL_STACK_MESSAGE9("CSalamanderConnect::AddMenuItem(%d, %S, %u, %d, 0x%X, 0x%X, 0x%X, 0x%X)",
                        iconIndex, name, hotKey, id, callGetState, state_or, state_and, skillLevel);
    if (iconIndex < -1)
        iconIndex = -1;
    CPluginData* p = Plugins.Get(Index);
    if (p != NULL)
    {
        if (!p->SupportDynMenuExt)
        {
            if (p->GetPluginInterfaceForMenuExt()->NotEmpty())
            {
                p->AddMenuItem(iconIndex, name, hotKey, id, callGetState, state_or, state_and,
                               skillLevel, pmitItemOrSeparator);
            }
            else
            {
                TRACE_E("Unable to add new menu item. The plug-in didn't provide interface for "
                        "menu extensions (see GetInterfaceForMenuExt).");
            }
        }
        else
            TRACE_E("CSalamanderConnect::AddMenuItem(): call ignored because you have dynamic menu (see FUNCTION_DYNAMICMENUEXT).");
    }
}

void CSalamanderConnect::AddSubmenuStart(int iconIndex, const wchar_t* name, int id, BOOL callGetState,
                                         DWORD state_or, DWORD state_and, DWORD skillLevel)
{
    CALL_STACK_MESSAGE8("CSalamanderConnect::AddSubmenuStart(%d, %S, %d, %d, 0x%X, 0x%X, 0x%X)",
                        iconIndex, name, id, callGetState, state_or, state_and, skillLevel);
    if (name == NULL)
    {
        TRACE_E("CSalamanderConnect::AddSubmenuStart(): 'name' may not be NULL!");
        return;
    }
    SubmenuLevel++;
    if (iconIndex < -1)
        iconIndex = -1;
    CPluginData* p = Plugins.Get(Index);
    if (p != NULL)
    {
        if (!p->SupportDynMenuExt)
        {
            if (p->GetPluginInterfaceForMenuExt()->NotEmpty())
            {
                p->AddMenuItem(iconIndex, name, 0, id, callGetState, state_or, state_and,
                               skillLevel, pmitStartSubmenu);
            }
            else
            {
                TRACE_E("Unable to add new menu item. The plugin didn't provide interface for "
                        "menu extensions (see GetInterfaceForMenuExt).");
            }
        }
        else
            TRACE_E("CSalamanderConnect::AddSubmenuStart(): call ignored because you have dynamic menu (see FUNCTION_DYNAMICMENUEXT).");
    }
}

void CSalamanderConnect::AddSubmenuEnd()
{
    CALL_STACK_MESSAGE1("CSalamanderConnect::AddSubmenuEnd()");
    if (SubmenuLevel > 0)
    {
        SubmenuLevel--;
        CPluginData* p = Plugins.Get(Index);
        if (p != NULL)
        {
            if (!p->SupportDynMenuExt)
            {
                if (p->GetPluginInterfaceForMenuExt()->NotEmpty())
                {
                    p->AddMenuItem(-1, NULL, 0, 0, FALSE, 0, 0, MENU_SKILLLEVEL_ALL, pmitEndSubmenu);
                }
                else
                {
                    TRACE_E("Unable to add new menu item. The plugin didn't provide interface for "
                            "menu extensions (see GetInterfaceForMenuExt).");
                }
            }
            else
                TRACE_E("CSalamanderConnect::AddSubmenuEnd(): call ignored because you have dynamic menu (see FUNCTION_DYNAMICMENUEXT).");
        }
    }
    else
        TRACE_E("Incorrect call to CSalamanderConnect::AddSubmenuEnd(): no submenu is opened!");
}

void CSalamanderConnect::SetChangeDriveMenuItem(const wchar_t* title, int iconIndex)
{
    CALL_STACK_MESSAGE3("CSalamanderConnect::SetChangeDriveMenuItem(%ls, %d)", title, iconIndex);
    CPluginData* p = Plugins.Get(Index);
    if (p != NULL)
    {
        if (p->SupportFS)
        {
            p->ChDrvMenuFSItemName.clear();
            p->ChDrvMenuFSItemIconIndex = -1;

            if (title == NULL)
            {
                TRACE_E("CSalamanderConnect::SetChangeDriveMenuItem(): 'title' may not be NULL!");
                return;
            }

            p->ChDrvMenuFSItemName = title;
            if (!p->ChDrvMenuFSItemName.empty())
                p->ChDrvMenuFSItemIconIndex = iconIndex;
        }
        else
        {
            TRACE_E("Unable to set Change Drive menu item. The plugin didn't provide interface for "
                    "file-system (see GetInterfaceForFS).");
        }
    }
}

void CSalamanderConnect::SetThumbnailLoader(const wchar_t* masks)
{
    if (masks == NULL || *masks == 0)
    {
        TRACE_E("CSalamanderConnect::SetThumbnailLoader(): unexpected parameter value (NULL or empty string).");
        return;
    }

    CPluginData* p = Plugins.Get(Index);
    if (p != NULL)
    {
        if (p->GetPluginInterfaceForThumbLoader()->NotEmpty())
        {
            p->ThumbnailMasks.SetMasksString(masks);
            int err;
            if (!p->ThumbnailMasks.PrepareMasks(err)) // error
            {
                TRACE_E("Unable to set thumbnail loader masks. Error in group mask (syntactical).");
                p->ThumbnailMasks.SetMasksString(L"");
            }
        }
        else
        {
            TRACE_E("Unable to set thumbnail loader masks. The plugin didn't provide interface for "
                    "thumbnail loader (see GetPluginInterfaceForThumbLoader).");
        }
    }
}

void CSalamanderConnect::SetBitmapWithIcons(HBITMAP bitmap)
{
    if (bitmap == NULL)
    {
        TRACE_E("CSalamanderConnect::SetBitmapWithIcons(): 'bitmap' may not be NULL!");
        return;
    }

    CPluginData* p = Plugins.Get(Index);
    if (p != NULL)
    {
        //  copy the 'bitmap' into our DIB,
        // to which we will have access at the RAW-data level
        BITMAP bmp;
        GetObject(bitmap, sizeof(bmp), &bmp);

        if (p->PluginIcons != NULL)
        {
            delete p->PluginIcons;
            p->PluginIcons = NULL;
        }
        if (p->PluginIconsGray != NULL)
        {
            delete p->PluginIconsGray;
            p->PluginIconsGray = NULL;
        }

        p->PluginIcons = new CIconList();
        if (p->PluginIcons != NULL)
        {
            if (p->PluginIcons->CreateFromBitmap(bitmap, bmp.bmWidth / 16, RGB(255, 0, 255)))
            {
                p->PluginIconsGray = new CIconList();
                if (p->PluginIconsGray != NULL && !p->PluginIconsGray->CreateAsCopy(p->PluginIcons, TRUE))
                {
                    delete p->PluginIconsGray;
                    p->PluginIconsGray = NULL;
                }
            }
            else
            {
                delete p->PluginIcons;
                p->PluginIcons = NULL;
            }
        }
    }
}

void CSalamanderConnect::SetPluginIcon(int iconIndex)
{
    if (iconIndex < 0)
    {
        TRACE_E("CSalamanderConnect::SetPluginIcon(): 'iconIndex' may not be negative number!");
        return;
    }

    CPluginData* p = Plugins.Get(Index);
    if (p != NULL)
        p->PluginIconIndex = iconIndex;
}

void CSalamanderConnect::SetPluginMenuAndToolbarIcon(int iconIndex)
{
    CPluginData* p = Plugins.Get(Index);
    if (p != NULL)
    {
        p->PluginSubmenuIconIndex = iconIndex;
    }
}

void CSalamanderConnect::SetIconListForGUI(CGUIIconListAbstract* iconList)
{
    CPluginData* p = Plugins.Get(Index);
    if (p != NULL)
    {
        if (p->PluginIcons != NULL)
        {
            delete p->PluginIcons;
            p->PluginIcons = NULL;
        }
        if (p->PluginIconsGray != NULL)
        {
            delete p->PluginIconsGray;
            p->PluginIconsGray = NULL;
        }
        p->PluginIcons = (CIconList*)iconList;
        if (p->PluginIcons != NULL)
        {
            p->PluginIconsGray = new CIconList();
            if (p->PluginIconsGray != NULL && !p->PluginIconsGray->CreateAsCopy(p->PluginIcons, TRUE))
            {
                delete p->PluginIconsGray;
                p->PluginIconsGray = NULL;
            }
        }
    }
}

//
// ****************************************************************************
// CSalamanderBuildMenu
//

void CSalamanderBuildMenu::AddMenuItem(int iconIndex, const wchar_t* name, DWORD hotKey, int id, BOOL callGetState,
                                       DWORD state_or, DWORD state_and, DWORD skillLevel)
{
    CALL_STACK_MESSAGE9("CSalamanderBuildMenu::AddMenuItem(%d, %S, %u, %d, 0x%X, 0x%X, 0x%X, 0x%X)",
                        iconIndex, name, hotKey, id, callGetState, state_or, state_and, skillLevel);
    if (iconIndex < -1)
        iconIndex = -1;
    CPluginData* p = Plugins.Get(Index);
    if (p != NULL)
    {
        if (p->GetPluginInterfaceForMenuExt()->NotEmpty())
        {
            p->AddMenuItem(iconIndex, name, hotKey, id, callGetState, state_or, state_and,
                           skillLevel, pmitItemOrSeparator);
        }
        else
        {
            TRACE_E("Unable to add new menu item. The plug-in didn't provide interface for "
                    "menu extensions (see GetInterfaceForMenuExt).");
        }
    }
}

void CSalamanderBuildMenu::AddSubmenuStart(int iconIndex, const wchar_t* name, int id, BOOL callGetState,
                                           DWORD state_or, DWORD state_and, DWORD skillLevel)
{
    CALL_STACK_MESSAGE8("CSalamanderBuildMenu::AddSubmenuStart(%d, %S, %d, %d, 0x%X, 0x%X, 0x%X)",
                        iconIndex, name, id, callGetState, state_or, state_and, skillLevel);
    SubmenuLevel++;
    CPluginData* p = Plugins.Get(Index);
    if (name == NULL)
    {
        TRACE_E("CSalamanderBuildMenu::AddSubmenuStart(): 'name' may not be NULL!");
        return;
    }
    if (iconIndex < -1)
        iconIndex = -1;
    if (p != NULL)
    {
        if (p->GetPluginInterfaceForMenuExt()->NotEmpty())
        {
            p->AddMenuItem(iconIndex, name, 0, id, callGetState, state_or, state_and,
                           skillLevel, pmitStartSubmenu);
        }
        else
        {
            TRACE_E("Unable to add new menu item. The plugin didn't provide interface for "
                    "menu extensions (see GetInterfaceForMenuExt).");
        }
    }
}

void CSalamanderBuildMenu::AddSubmenuEnd()
{
    CALL_STACK_MESSAGE1("CSalamanderBuildMenu::AddSubmenuEnd()");
    if (SubmenuLevel > 0)
    {
        SubmenuLevel--;
        CPluginData* p = Plugins.Get(Index);
        if (p != NULL)
        {
            if (p->GetPluginInterfaceForMenuExt()->NotEmpty())
            {
                p->AddMenuItem(-1, NULL, 0, 0, FALSE, 0, 0, MENU_SKILLLEVEL_ALL, pmitEndSubmenu);
            }
            else
            {
                TRACE_E("Unable to add new menu item. The plugin didn't provide interface for "
                        "menu extensions (see GetInterfaceForMenuExt).");
            }
        }
    }
    else
        TRACE_E("Incorrect call to CSalamanderBuildMenu::AddSubmenuEnd(): no submenu is opened!");
}

void CSalamanderBuildMenu::SetIconListForMenu(CGUIIconListAbstract* iconList)
{
    CPluginData* p = Plugins.Get(Index);
    if (p != NULL)
    {
        p->ReleasePluginDynMenuIcons();
        p->PluginDynMenuIcons = (CIconList*)iconList;
    }
}

//
// ****************************************************************************
// CSalamanderRegistry
//

BOOL CSalamanderRegistry::ClearKey(HKEY key)
{
    CALL_STACK_MESSAGE1("CSalamanderRegistry::ClearKey()");
    return ::ClearKey(key);
}

BOOL CSalamanderRegistry::CreateKey(HKEY key, const wchar_t* name, HKEY& createdKey)
{
    CALL_STACK_MESSAGE1("CSalamanderRegistry::CreateKey()");
    return ::CreateKeyW(key, name, createdKey);
}

BOOL CSalamanderRegistry::OpenKey(HKEY key, const wchar_t* name, HKEY& openedKey)
{
    CALL_STACK_MESSAGE1("CSalamanderRegistry::OpenKey()");
    return ::OpenKeyW(key, name, openedKey);
}

void CSalamanderRegistry::CloseKey(HKEY key)
{
    CALL_STACK_MESSAGE1("CSalamanderRegistry::CloseKey()");
    ::CloseKey(key);
}

BOOL CSalamanderRegistry::DeleteKey(HKEY key, const wchar_t* name)
{
    CALL_STACK_MESSAGE1("CSalamanderRegistry::DeleteKey()");
    return ::DeleteKeyW(key, name);
}

BOOL CSalamanderRegistry::GetValue(HKEY key, const wchar_t* name, DWORD type, void* buffer, DWORD bufferSize)
{
    SLOW_CALL_STACK_MESSAGE1("CSalamanderRegistry::GetValue()");
    return ::GetValueW(key, name, type, buffer, bufferSize);
}

BOOL CSalamanderRegistry::SetValue(HKEY key, const wchar_t* name, DWORD type, const void* data, DWORD dataSize)
{
    SLOW_CALL_STACK_MESSAGE1("CSalamanderRegistry::SetValue()");
    return ::SetValueW(key, name, type, data, dataSize);
}

BOOL CSalamanderRegistry::DeleteValue(HKEY key, const wchar_t* name)
{
    CALL_STACK_MESSAGE1("CSalamanderRegistry::DeleteValue()");
    return ::DeleteValueW(key, name);
}

BOOL CSalamanderRegistry::GetSize(HKEY key, const wchar_t* name, DWORD type, DWORD& bufferSize)
{
    SLOW_CALL_STACK_MESSAGE3("CSalamanderRegistry::GetSize(, %ls, 0x%x, )", name, type);
    return ::GetSizeW(key, name, type, bufferSize);
}

//
// ****************************************************************************
// CSalamanderPluginEntry
//

BOOL CSalamanderPluginEntry::SetBasicPluginData(const wchar_t* pluginName, DWORD functions,
                                                const wchar_t* version, const wchar_t* copyright,
                                                const wchar_t* description, const wchar_t* regKeyName,
                                                const wchar_t* extensions, const wchar_t* fsName)
{
    CALL_STACK_MESSAGE9("CSalamanderPluginEntry::SetBasicPluginData(%ls, 0x%X, %ls, %ls, %ls, %ls, %ls, %ls)",
                        pluginName, functions, version, copyright, description, regKeyName, extensions,
                        fsName);

    if (Valid)
    {
        TRACE_E("CSalamanderPluginEntry::SetBasicPluginData(): this method can be called only once!");
        return FALSE;
    }

    BOOL supportPanelView = (functions & FUNCTION_PANELARCHIVERVIEW) != 0;
    BOOL supportPanelEdit = (functions & FUNCTION_PANELARCHIVEREDIT) != 0;
    BOOL supportCustomPack = (functions & FUNCTION_CUSTOMARCHIVERPACK) != 0;
    BOOL supportCustomUnpack = (functions & FUNCTION_CUSTOMARCHIVERUNPACK) != 0;
    BOOL supportConfiguration = (functions & FUNCTION_CONFIGURATION) != 0;
    BOOL supportLoadSave = (functions & FUNCTION_LOADSAVECONFIGURATION) != 0;
    BOOL supportViewer = (functions & FUNCTION_VIEWER) != 0;
    BOOL supportFS = (functions & FUNCTION_FILESYSTEM) != 0;
    BOOL supportDynMenuExt = (functions & FUNCTION_DYNAMICMENUEXT) != 0;

    if (pluginName == NULL || version == NULL || copyright == NULL || description == NULL ||
        supportLoadSave && (regKeyName == NULL || regKeyName[0] == 0) ||
        (supportPanelView || supportPanelEdit) && extensions == NULL ||
        supportFS && fsName == NULL)
    {
        TRACE_E("CSalamanderPluginEntry::SetBasicPluginData(): Invalid parameter (NULL or empty string)!");
        Error = TRUE;
        return FALSE;
    }

    // this is either loading an installed plugin or adding a new plugin,
    // perform a check and update data (a new plugin should always pass the tests)
    if (Plugin->SupportPanelView && !supportPanelView ||
        Plugin->SupportPanelEdit && !supportPanelEdit ||
        Plugin->SupportCustomPack && !supportCustomPack ||
        Plugin->SupportCustomUnpack && !supportCustomUnpack ||
        Plugin->SupportViewer && !supportViewer ||
        Plugin->SupportFS && !supportFS)
    { // downgrading capabilities is not possible ...
        std::wstring msg = FormatStrW(LoadStrW(IDS_REINSTALLPLUGIN), Plugin->Name.c_str(), Plugin->DLLName.c_str());
        gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
        Error = TRUE;
        return FALSE;
    }

    // update data
    Plugin->SupportPanelView = supportPanelView;
    Plugin->SupportPanelEdit = supportPanelEdit;
    Plugin->SupportCustomPack = supportCustomPack;
    Plugin->SupportCustomUnpack = supportCustomUnpack;
    Plugin->SupportConfiguration = supportConfiguration;
    Plugin->SupportLoadSave = supportLoadSave;
    Plugin->SupportViewer = supportViewer;
    Plugin->SupportFS = supportFS;
    Plugin->SupportDynMenuExt = supportDynMenuExt;

    Plugin->Name = pluginName;
    Plugin->Version = version;
    Plugin->SalamanderDebug.Init(Plugin->DLLName.c_str(), Plugin->Version.c_str());
    Plugin->SalamanderPasswordManager.Init(Plugin->DLLName.c_str());
    Plugin->Copyright = copyright;
    Plugin->Extensions = (supportPanelView || supportPanelEdit) ? extensions : L"";
    Plugin->Description = description;
    if (supportLoadSave)
    {
        if (Plugin->RegKeyName.empty() || Plugin->RegKeyName[0] == 0)
        { // new plugin with load/save - set a new key name in the registry
            Plugin->RegKeyName = Plugins.GetUniqueRegKeyName(regKeyName);
        }
    }
    else // does not support load/save
    {
        Plugin->RegKeyName.clear();
    }
    if (Plugin->SupportFS)
    {
        OldFSNames = std::move(Plugin->FSNames); // take ownership of old names
        Plugin->FSNames.clear();

        Plugin->FSNames.push_back(Plugins.GetUniqueFSName(fsName, NULL, &OldFSNames));
    }
    else // does not support FS
    {
        Plugin->FSNames.clear();
    }

    Valid = TRUE;
    return TRUE; // data successfully acquired
}

HINSTANCE
CSalamanderPluginEntry::LoadLanguageModule(HWND parent, const wchar_t* pluginName)
{
    HINSTANCE lang = NULL;
    std::wstring pluginPath;
    if (!GetPluginDllPathOwned(Plugin->DLLName, pluginPath).success)
        return NULL;
    const size_t separator = pluginPath.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
        return NULL;
    std::wstring langDirectory(pluginPath, 0, separator + 1);
    langDirectory.append(L"lang\\");
    std::wstring slgName = Configuration.LoadedSLGName;
    std::wstring path = langDirectory + slgName;

    // first try to load the SLG of the language Salamander is currently running in
    lang = HANDLES_Q(LoadLibraryW(path.c_str()));
    WORD languageID = 0;
    if (lang == NULL || !IsSLGFileValid(Plugin->GetPluginDLL(), lang, languageID, NULL))
    { // the SLG doesn't exist or isn't the expected one (completely different file or at least another version)
        if (lang != NULL)
            HANDLES(FreeLibrary(lang));
        lang = NULL;
        if (Plugin->LastSLGName.empty() ||                       // no .slg chosen during the previous plugin load
            _wcsicmp(slgName.c_str(), Plugin->LastSLGName.c_str()) == 0) // we already tried this .slg
        {
            if (!Configuration.DoNotDispCantLoadPluginSLG)
            {
                std::wstring msg = FormatStrW(LoadStrW(IDS_CANTLOADPLUGINSLG1), path.c_str());
                bool dontShow = Configuration.DoNotDispCantLoadPluginSLG != FALSE;
                gPrompter->ShowErrorWithCheckbox(pluginName, msg.c_str(),
                                                 LoadStrW(IDS_DONOTSHOWCANTLOADPLUGINSLG), &dontShow);
                Configuration.DoNotDispCantLoadPluginSLG = dontShow ? TRUE : FALSE;
            }
        }
        else // try to load the .slg chosen during the previous plugin load
        {
            slgName = Plugin->LastSLGName;
            path = langDirectory + slgName;
            lang = HANDLES_Q(LoadLibraryW(path.c_str()));
            if (lang == NULL || !IsSLGFileValid(Plugin->GetPluginDLL(), lang, languageID, NULL))
            { // the SLG doesn't exist or isn't the expected one (completely different file or at least another version)
                if (lang != NULL)
                    HANDLES(FreeLibrary(lang));
                lang = NULL;
                if (!Configuration.DoNotDispCantLoadPluginSLG2)
                {
                    std::wstring msg = FormatStrW(LoadStrW(IDS_CANTLOADPLUGINSLG2), path.c_str());
                    bool dontShow = Configuration.DoNotDispCantLoadPluginSLG2 != FALSE;
                    gPrompter->ShowErrorWithCheckbox(pluginName, msg.c_str(),
                                                     LoadStrW(IDS_DONOTSHOWCANTLOADPLUGINSLG), &dontShow);
                    Configuration.DoNotDispCantLoadPluginSLG2 = dontShow ? TRUE : FALSE;
                }
            }
        }
        if (lang == NULL) // find all .slg files on the disk for the plugin and let the user choose (if there's more than one .slg)
        {
            std::wstring selSLGName;
            CLanguageSelectorDialog slgDialog(parent, selSLGName, pluginName);
            const std::wstring searchPath = langDirectory + L"*.slg";
            slgDialog.Initialize(searchPath.c_str(), Plugin->GetPluginDLL());
            if (slgDialog.GetLanguagesCount() == 0)
                gPrompter->ShowError(pluginName, LoadStrW(IDS_PLUGINSLGNOTFOUND));
            else
            {
                if (slgDialog.GetLanguagesCount() == 1)
                    slgDialog.GetSLGName(selSLGName); // if only one language exists, use it
                else
                {
                    if (Configuration.UseAsAltSLGInOtherPlugins &&
                        slgDialog.SLGNameExists(Configuration.AltPluginSLGName.c_str()))
                    {
                        selSLGName = Configuration.AltPluginSLGName;
                    }
                    else
                    {
                        if (Configuration.UseAsAltSLGInOtherPlugins) // fallback language is defined but unavailable for this plugin, let the user choose another one
                            Configuration.UseAsAltSLGInOtherPlugins = FALSE;
                        slgDialog.Execute();
                    }
                }
                slgName = selSLGName;
                path = langDirectory + slgName;
                lang = HANDLES_Q(LoadLibraryW(path.c_str()));
                if (lang == NULL || !IsSLGFileValid(Plugin->GetPluginDLL(), lang, languageID, NULL))
                { // shouldn't theoretically happen (dialog verifies the validity of the .SLG module)
                    if (lang != NULL)
                        HANDLES(FreeLibrary(lang));
                    lang = NULL;
                    TRACE_E("CSalamanderPluginEntry::LoadLanguageModule(): unexpected situation: SLG module is invalid!");
                }
            }
        }
    }
    Plugin->LastSLGName.clear();
    if (lang != NULL)
    {
        if (_wcsicmp(slgName.c_str(), Configuration.LoadedSLGName.c_str()) != 0)
            Plugin->LastSLGName = slgName;
        if (Plugin->SalamanderGeneral.LanguageModule == NULL)
            Plugin->SalamanderGeneral.LanguageModule = lang;
        else
        {
            HANDLES(FreeLibrary(lang));
            lang = NULL;
            TRACE_E("CSalamanderPluginEntry::LoadLanguageModule(): you can call this method only once!");
        }
    }
    return lang;
}

void CSalamanderPluginEntry::SetPluginHomePageURL(const wchar_t* url)
{
    CALL_STACK_MESSAGE2("CSalamanderPluginEntry::SetPluginHomePageURL(%ls)", url);
    Plugin->PluginHomePageURL = url ? url : L"";
}

BOOL CSalamanderPluginEntry::AddFSName(const wchar_t* fsName, int* newFSNameIndex)
{
    CALL_STACK_MESSAGE2("CSalamanderPluginEntry::AddFSName(%ls,)", fsName);
    if (fsName == NULL || newFSNameIndex == NULL)
    {
        TRACE_E("CSalamanderPluginEntry::AddFSName(): invalid parameter (NULL)!");
        return FALSE;
    }

    if (!Valid)
    {
        TRACE_E("CSalamanderPluginEntry::AddFSName(): called before SetBasicPluginData()!");
        return FALSE;
    }

    if (!Plugin->SupportFS)
    {
        TRACE_E("CSalamanderPluginEntry::AddFSName(): unexpected call: FUNCTION_FILESYSTEM is not supported by this plugin!");
        return FALSE;
    }

    if (Plugin->FSNames.empty())
    {
        TRACE_E("CSalamanderPluginEntry::AddFSName(): unable to add fs-name, first fs-name is missing!");
        return FALSE;
    }

    Plugin->FSNames.push_back(Plugins.GetUniqueFSName(fsName, NULL, &OldFSNames));
    *newFSNameIndex = (int)Plugin->FSNames.size() - 1;
    return TRUE;
}

//
// ****************************************************************************
// CPluginMenuItem
//

CPluginMenuItem::CPluginMenuItem(int iconIndex, const wchar_t* name, DWORD hotKey, DWORD stateMask,
                                 int id, DWORD skillLevel, CPluginMenuItemType type)
{
    Type = type;
    IconIndex = iconIndex;
    if (name != NULL)
        Name = name; // on error we will become a separator ;-)
    // else Name stays empty (= separator)
#ifdef _DEBUG
    if (name != NULL && (hotKey & HOTKEY_HINT) == 0)
    {
        // since version 2.5 beta 7 hot keys are supported
        // the hot key must not be part of the text
        auto tabPos = Name.find(L'\t');
        if (tabPos != std::wstring::npos)
        {
            if (Configuration.ConfigVersion >= 25) // warn only on newer configurations
                TRACE_EW(L"Plugin menu item contains hot key (" << name << L"). Use the AddMenuItem/'hotKey' parameter instead.");
            Name.resize(tabPos);
        }
    }
#endif // _DEBUG
    StateMask = stateMask;
    if ((skillLevel & ~MENU_SKILLLEVEL_ALL) != 0 ||
        (skillLevel & MENU_SKILLLEVEL_ALL) == 0)
    {
        TRACE_E("CPluginMenuItem::CPluginMenuItem wrong skillLevel=" << skillLevel);
        // make a correction
        skillLevel = MENU_SKILLLEVEL_ALL;
    }
    SkillLevel = skillLevel;
    ID = id;
    SUID = -1;
    HotKey = hotKey; // no hot key; the user may set one
}

//
// ****************************************************************************
// CPluginData
//

CPluginData::CPluginData(const wchar_t* name, const wchar_t* dllName, BOOL supportPanelView,
                         BOOL supportPanelEdit, BOOL supportCustomPack, BOOL supportCustomUnpack,
                         BOOL supportConfiguration, BOOL supportLoadSave, BOOL supportViewer,
                         BOOL supportFS, BOOL supportDynMenuExt, const wchar_t* version, const wchar_t* copyright,
                         const wchar_t* description, const wchar_t* regKeyName, const wchar_t* extensions,
                         const std::vector<std::wstring>* fsNames, BOOL loadOnStart, const wchar_t* lastSLGName,
                         const wchar_t* pluginHomePageURL)
    : MenuItems(10, 5), Commands(1, 5), PluginIfaceForFS(NULL, 0),
      PluginIfaceForMenuExt(NULL, 0)
{
    CALL_STACK_MESSAGE20("CPluginData::CPluginData(%ls, %ls, %d, %d, %d, %d, %d, %d, %d, %d, %d, %ls, %ls, %ls, %ls, %ls, , %d, %ls, %ls)",
                         name, dllName, supportPanelView, supportPanelEdit, supportCustomPack,
                         supportCustomUnpack, supportConfiguration, supportLoadSave, supportViewer,
                         supportFS, supportDynMenuExt, version, copyright, description, regKeyName,
                         extensions, loadOnStart, lastSLGName, pluginHomePageURL);
    ArcCacheHaveInfo = FALSE;
    ArcCacheOwnDelete = FALSE;
    ArcCacheCacheCopies = TRUE;
    PluginIcons = NULL;
    PluginIconsGray = NULL;
    PluginDynMenuIcons = NULL;
    PluginIconIndex = -1;
    PluginSubmenuIconIndex = -1;
    ShowSubmenuInPluginsBar = TRUE;
    ThumbnailMasksDisabled = FALSE;
    DynMenuWasAlreadyBuild = FALSE;
    // BugReportMessage, BugReportEMail default-construct to empty
    SubMenu = NULL;
    DLL = NULL;
    BuiltForVersion = 0;
    Name = name ? name : L"";
    DLLName = dllName ? dllName : L"";
    Version = version ? version : L"";
    Copyright = copyright ? copyright : L"";
    Extensions = extensions ? extensions : L"";
    Description = description ? description : L"";
    RegKeyName = regKeyName ? regKeyName : L"";
    SupportFS = supportFS;
    if (SupportFS && fsNames != NULL)
        FSNames = *fsNames; // vector copy; throws std::bad_alloc on OOM
    LastSLGName = (lastSLGName != NULL && lastSLGName[0] != 0) ? lastSLGName : L"";
    PluginHomePageURL = pluginHomePageURL != NULL ? pluginHomePageURL : L"";
    SalamanderDebug.Init(DLLName.c_str(), Version.c_str());
    SalamanderPasswordManager.Init(DLLName.c_str());
    SupportPanelView = supportPanelView;
    SupportPanelEdit = supportPanelEdit;
    SupportCustomPack = supportCustomPack;
    SupportCustomUnpack = supportCustomUnpack;
    SupportConfiguration = supportConfiguration;
    SupportLoadSave = supportLoadSave;
    SupportViewer = supportViewer;
    SupportDynMenuExt = supportDynMenuExt;
    LoadOnStart = loadOnStart;
    LegacyCompatApproved = FALSE;
    // ChDrvMenuFSItemName default-constructs to empty
    ChDrvMenuFSItemVisible = TRUE;
    ChDrvMenuFSItemIconIndex = -1;
    ShouldUnload = FALSE;
    ShouldRebuildMenu = FALSE;
#ifdef _DEBUG
    OpenedFSCounter = 0;
    OpenedPDCounter = 0;
#endif // _DEBUG
    OpenPackDlg = FALSE;
    PackDlgDelFilesAfterPacking = 0;
    OpenUnpackDlg = FALSE;
    // UnpackDlgUnpackMask default-constructs to empty
    PluginIsNethood = FALSE;
    PluginUsesPasswordManager = FALSE;
    IconOverlaysCount = 0;
    IconOverlays = NULL;
}

CPluginData::~CPluginData()
{
    CALL_STACK_MESSAGE1("CPluginData::~CPluginData()");
#ifdef _DEBUG
    if (OpenedFSCounter != 0)
    {
        TRACE_E("OpenedFSCounter is " << OpenedFSCounter << " - mismatch in calls to CloseFS().");
    }
    if (OpenedPDCounter != 0)
    {
        TRACE_E("OpenedPDCounter is " << OpenedPDCounter << " - mismatch in calls to ReleasePluginDataInterface().");
    }
#endif // _DEBUG
    if (PluginIface.NotEmpty())
        PluginIface.Release(NULL, TRUE);
    LegacyHost.reset();
    if (DLL != NULL)
    {
        TRACE_E("CPluginData::~CPluginData(): unexpected situation (2)!");
        HANDLES(FreeLibrary(DLL));
    }
    // Name, DLLName, Version, Copyright, Extensions, Description, RegKeyName,
    // ChDrvMenuFSItemName, LastSLGName, PluginHomePageURL are std::wstring (auto-destruct)
    // FSNames is std::vector<std::wstring> (auto-destruct)
    // BugReportMessage, BugReportEMail, UnpackDlgUnpackMask, and ArcCacheTmpPath are
    // std::wstring (auto-destruct).
    if (PluginIcons != NULL)
        delete PluginIcons;
    if (PluginIconsGray != NULL)
        delete PluginIconsGray;
    if (PluginDynMenuIcons != NULL)
    {
        TRACE_E("CPluginData::~CPluginData(): PluginDynMenuIcons is not NULL, please contact Petr Solin");
        delete PluginDynMenuIcons;
    }
    if (IconOverlaysCount != 0 || IconOverlays != NULL)
        TRACE_E("CPluginData::~CPluginData(): IconOverlaysCount is not 0 or IconOverlays is not NULL, please contact Petr Solin");
}

BOOL CPluginData::InitDLL(HWND parent, BOOL quiet, BOOL waitCursor, BOOL showUnsupOnX64, BOOL releaseDynMenuIcons)
{
    CALL_STACK_MESSAGE8("CPluginData::InitDLL(0x%p, %d, %d, %d, %d) (%ls v. %ls)",
                        parent, quiet, waitCursor, showUnsupOnX64,
                        releaseDynMenuIcons, DLLName.c_str(), Version.c_str());

    if (DLL == NULL)
    {
        BOOL refreshUNCRootPaths = FALSE;

        // obtain the full DLL name
        std::wstring pluginPath;
        const PathResult pluginPathResult = GetPluginDllPathOwned(DLLName, pluginPath);
        if (!pluginPathResult.success)
        {
            if (!quiet)
            {
                std::wstring msg = FormatStrW(LoadStrW(IDS_UNABLETOLOADPLUGIN), Name.c_str(),
                                              DLLName.c_str(), GetErrorTextOwned(pluginPathResult.errorCode).c_str());
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
            }
            return FALSE;
        }
        const wchar_t* s = pluginPath.c_str();

        // load the DLL
        HCURSOR oldCur;
        if (waitCursor)
            oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
        DLL = HANDLES(LoadLibraryExW(s, NULL, LOAD_WITH_ALTERED_SEARCH_PATH));
        if (waitCursor)
            SetCursor(oldCur);
        if (DLL == NULL) // error
        {
            DWORD err = GetLastError();
            if (!quiet)
            {
                std::wstring msg = FormatStrW(LoadStrW(IDS_UNABLETOLOADPLUGIN), Name.c_str(), s, GetErrorTextOwned(err).c_str());
                gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
            }
        }
        else // connect to the DLL
        {
            FARPROC entryProc = GetProcAddress(DLL, "SalamanderPluginEntry"); // plug-in entry point
            if (entryProc != NULL)
            {
#ifdef _DEBUG
                AddModuleWithPossibleMemoryLeaks(s);
#endif // _DEBUG

                // variables for detecting features that "upgrade" (FALSE->TRUE)
                BOOL supportPanelView = SupportPanelView;
                BOOL supportPanelEdit = SupportPanelEdit;
                BOOL supportCustomPack = SupportCustomPack;
                BOOL supportCustomUnpack = SupportCustomUnpack;
                BOOL supportViewer = SupportViewer;

                CSalamanderPluginEntry salamander(parent, (CPluginData*)this);
                // build the flag returned via CSalamanderPluginEntry::GetLoadInformation()
                salamander.AddLoadInfo(Plugins.LoadInfoBase); // base (none/auto-install/new-plugins.ver)
                                                              //        if (LoadOnStart) salamander.AddLoadInfo(LOADINFO_LOADONSTART);  // "load on start" flag

                // remove commands inherited from the previous load (the array should be empty anyway)
                Commands.DestroyMembers();

                // drop icon overlays (just in case; it should already be empty)
                ReleaseIconOverlays();

                // clear the disk-cache settings for the archiver; we must obtain them again
                ArcCacheHaveInfo = FALSE;
                ArcCacheTmpPath.clear();
                ArcCacheOwnDelete = FALSE;
                ArcCacheCacheCopies = TRUE;

                FSalamanderPluginGetReqVer getReqVer = (FSalamanderPluginGetReqVer)GetProcAddress(DLL, "SalamanderPluginGetReqVer"); // plugin function
                BuiltForVersion = -1;                                                                                                // -1 = plugin built for a version older than 2.5 beta 2 (does not export "SalamanderPluginGetReqVer")
                if (getReqVer != NULL && (BuiltForVersion = getReqVer()) >= PLUGIN_REQVER)
                {
                    FSalamanderPluginGetSDKVer getSDKVer = (FSalamanderPluginGetSDKVer)GetProcAddress(DLL, "SalamanderPluginGetSDKVer"); // plugin function
                    if (getSDKVer != NULL)                                                                                               // if the plugin exports this function it likely wants to raise BuiltForVersion (it pretends to be old for compatibility with older Salamander versions but wants to use new services with newer versions)
                    {
                        int verSDK = getSDKVer();
                        if (BuiltForVersion <= verSDK)
                            BuiltForVersion = verSDK;
                        else
                            TRACE_E("CPluginData::InitDLL(): nonsense: SalamanderPluginGetSDKVer() returns older version than SalamanderPluginGetReqVer()");
                    }
                }
                BOOL suppressOldVerError = FALSE;
                sally::compat::RoutingDecision abiRoute =
                    sally::compat::DecideAbiRoute(
                        BuiltForVersion, LAST_VERSION_OF_SALAMANDER,
                        LegacyCompatApproved != FALSE);
                if (abiRoute.route == sally::compat::AbiRoute::NeedsUserApproval &&
                    !quiet)
                {
                    std::wstring msg;
                    if (Name.empty() || Name[0] == 0)
                        msg = FormatStrW(LoadStrW(IDS_OLDPLUGINVERSION_CONFIRM2), s);
                    else
                        msg = FormatStrW(LoadStrW(IDS_OLDPLUGINVERSION_CONFIRM), Name.c_str(), s);
                    if (gPrompter->AskYesNo(LoadStrW(IDS_QUESTION), msg.c_str()).type == PromptResult::kYes)
                    {
                        LegacyCompatApproved = TRUE; // persist in configuration so this path is not prompted again
                        abiRoute = sally::compat::DecideAbiRoute(
                            BuiltForVersion, LAST_VERSION_OF_SALAMANDER, true);
                    }
                    else
                        suppressOldVerError = TRUE; // user already refused in this attempt
                }
                BOOL oldVer = !abiRoute.loadable();
                if (abiRoute.route == sally::compat::AbiRoute::Refused)
                {
                    TRACE_E("CPluginData::InitDLL(): plugin ABI version " << BuiltForVersion << " is refused by the ABI route.");
                }
                if (!oldVer)
                {
                    if (!PluginHomePageURL.empty())
                    { // delete the plugin URL just before calling the entry point so it remains available if loading of the plugin fails due to an old plugin version (the user can use the URL to obtain a new version of the plugin)
                        PluginHomePageURL.clear();
                    }

                    BOOL oldPluginIsNethood = PluginIsNethood;
                    BOOL oldPluginUsesPasswordManager = PluginUsesPasswordManager;
                    PluginIsNethood = FALSE;
                    PluginUsesPasswordManager = FALSE;

                    EnterPlugin(); // for the plugin entry point
                    Plugins.EnterDataCS();
                    PluginIface.Init((CPluginInterfaceAbstract*)-1, BuiltForVersion); // so SetFlagLoadOnSalamanderStart can be used from the entry point
                    Plugins.LeaveDataCS();
                    SalamanderGeneral.Init((CPluginInterfaceAbstract*)-1); // so SetFlagLoadOnSalamanderStart can be used from the entry point

                    // !!! CALLING THE PLUGIN ENTRY POINT !!!
                    CPluginInterfaceAbstract* resIface = NULL;
                    if (abiRoute.needsAdapter())
                    {
                        LegacyHost = sally::compat::CreateLegacyPluginHost(
                            salamander, SalamanderDebug, SalamanderGeneral,
                            SalSafeFile, SalamanderGUI, BuiltForVersion);
                        if (LegacyHost)
                            resIface = sally::compat::InvokeLegacyPluginEntry(
                                *LegacyHost, entryProc);
                    }
                    else
                    {
                        FSalamanderPluginEntry entry =
                            reinterpret_cast<FSalamanderPluginEntry>(entryProc);
                        resIface = entry(&salamander);
                    }

                    Plugins.EnterDataCS();
                    PluginIface.Init(resIface, BuiltForVersion);
                    Plugins.LeaveDataCS();
                    LeavePlugin();
                    SalamanderGeneral.Init(PluginIface.GetInterface());

                    BOOL pluginNethoodChanged = FALSE;
                    if (!PluginIface.NotEmpty())
                    {
                        PluginIsNethood = oldPluginIsNethood; // when the entry point fails, we restore the original value
                        PluginUsesPasswordManager = oldPluginUsesPasswordManager;
                    }
                    else
                    {
                        pluginNethoodChanged = PluginIsNethood != oldPluginIsNethood;
                        if (pluginNethoodChanged)
                            refreshUNCRootPaths = TRUE;
                    }
                }
                else // probably unnecessary, just to be safe
                {
                    Plugins.EnterDataCS();
                    PluginIface.Init(NULL, 0);
                    Plugins.LeaveDataCS();
                    SalamanderGeneral.Init(NULL);
                }

                if (!oldVer && PluginIface.NotEmpty() && salamander.DataValid())
                { // pull interfaces of other plugin parts
                    PluginIfaceForArchiver.Init(PluginIface.GetInterfaceForArchiver());
                    PluginIfaceForViewer.Init(PluginIface.GetInterfaceForViewer());
                    PluginIfaceForMenuExt.Init(PluginIface.GetInterfaceForMenuExt(), BuiltForVersion);
                    PluginIfaceForFS.Init(PluginIface.GetInterfaceForFS(), BuiltForVersion);
                    PluginIfaceForThumbLoader.Init(PluginIface.GetInterfaceForThumbLoader(), DLLName.c_str(), Version.c_str());
                }
                else // clear the other parts of the plugin interface as well
                {
                    if (salamander.ShowError() && oldVer && !suppressOldVerError) // old version and it has not been reported yet...
                    {
                        if (!quiet)
                        {
                            // The quarantined 104 vintage says so by name. That
                            // message went out with ShouldRejectBrokenWideFSPlugin
                            // while the routing kept refusing the vintage, so a
                            // broken transitional FS ABI was reported as a plain
                            // "plugin too old" — sending the user to look for an
                            // update that may already be installed. The problem is
                            // the ABI, not the age.
                            const bool quarantined =
                                abiRoute.reason == sally::compat::RefusalReason::Quarantined;
                            const int withName = quarantined ? IDS_BROKENFSPLUGINVERSION
                                                             : IDS_OLDPLUGINVERSION;
                            const int withoutName = quarantined ? IDS_BROKENFSPLUGINVERSION2
                                                                : IDS_OLDPLUGINVERSION2;
                            std::wstring msg;
                            if (Name.empty() || Name[0] == 0)
                                msg = FormatStrW(LoadStrW(withoutName), s);
                            else
                                msg = FormatStrW(LoadStrW(withName), Name.c_str(), s);
                            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
                        }
                    }

                    PluginIfaceForArchiver.Init(NULL);
                    PluginIfaceForViewer.Init(NULL);
                    PluginIfaceForMenuExt.Init(NULL, 0);
                    PluginIfaceForFS.Init(NULL, 0);
                    PluginIfaceForThumbLoader.Init(NULL, NULL, NULL);
                }

                BOOL archiverOK = (!SupportPanelView && !SupportPanelEdit && !SupportCustomPack &&
                                       !SupportCustomUnpack ||
                                   PluginIfaceForArchiver.NotEmpty());
                BOOL viewerOK = !SupportViewer || PluginIfaceForViewer.NotEmpty();
                // menuExtOK cannot be determined - if the menu extension interface is unavailable, menu items will not be added
                BOOL FSOK = !SupportFS || PluginIfaceForFS.NotEmpty();

                if (!oldVer && PluginIface.NotEmpty() && salamander.DataValid() &&
                    archiverOK && viewerOK && FSOK)
                {
                    supportPanelView = (!supportPanelView && SupportPanelView);
                    supportPanelEdit = (!supportPanelEdit && SupportPanelEdit);
                    supportCustomPack = (!supportCustomPack && SupportCustomPack);
                    supportCustomUnpack = (!supportCustomUnpack && SupportCustomUnpack);
                    supportViewer = (!supportViewer && SupportViewer);

                    BOOL loaded = FALSE;
                    if (SupportLoadSave) // if load/save from registry is supported, perform it
                    {
                        LoadSaveToRegistryMutex.Enter();
                        HKEY hSal;
                        if (SALAMANDER_ROOT_REG != NULL &&
                            OpenKeyW(HKEY_CURRENT_USER, SALAMANDER_ROOT_REG, hSal))
                        {
                            HKEY actKey;
                            if (OpenKeyW(hSal, SALAMANDER_PLUGINSCONFIG, actKey))
                            {
                                HKEY regKey;
                                if (OpenKeyW(actKey, RegKeyName.c_str(), regKey))
                                {
                                    CSalamanderRegistry registry;
                                    {
                                        CALL_STACK_MESSAGE3("1.PluginIface.LoadConfiguration(, ,) (%ls v. %ls)", DLLName.c_str(), Version.c_str());
                                        PluginIface.LoadConfiguration(parent, regKey, &registry);
                                    }
                                    loaded = TRUE;
                                    CloseKey(regKey);
                                }
                                CloseKey(actKey);
                            }
                            CloseKey(hSal);
                        }
                        LoadSaveToRegistryMutex.Leave();
                    }
                    // if the registry load did not occur, load default values
                    if (!loaded)
                    {
                        CSalamanderRegistry registry;
                        {
                            CALL_STACK_MESSAGE3("2.PluginIface.LoadConfiguration(, ,) (%ls v. %ls)", DLLName.c_str(), Version.c_str());
                            PluginIface.LoadConfiguration(parent, NULL, &registry);
                        }
                    }

                    ThumbnailMasks.SetMasksString(L""); // remove masks for the thumbnail loader; only the new ones apply
                    ThumbnailMasksDisabled = FALSE;
                    if (PluginIcons != NULL)
                    {
                        delete PluginIcons; // discard the bitmap with icons; only the new one applies
                        PluginIcons = NULL;
                    }
                    if (PluginIconsGray != NULL)
                    {
                        delete PluginIconsGray; // discard the bitmap with icons; only the new one applies
                        PluginIconsGray = NULL;
                    }
                    if (PluginDynMenuIcons != NULL)
                        TRACE_E("CPluginData::InitDLL(): PluginDynMenuIcons is not NULL, please contact Petr Solin");
                    ReleasePluginDynMenuIcons(); // if it happens to exist, remove it; it is unnecessary
                    PluginIconIndex = -1;        // clear the icon index; only the new one applies
                    PluginSubmenuIconIndex = -1; // clear the icon index; only the new one applies
                    //j.r. we will not overwrite this value; it was set in the constructor or by user changes
                    //     and set when reading the plugin configuration
                    //ShowSubmenuInPluginsBar = FALSE;  // remove the toolbar button; it will appear again only if the plugin requests it...

                    // instead of destroying it, we back up the old array
                    TIndirectArray<CPluginMenuItem> oldMenuItems(max(1, MenuItems.Count), 1); // copy of the menu for hot key synchronization
                    if (!SupportDynMenuExt)                                                   // dynamic menus are not created in Connect, so we leave it for later
                    {
                        oldMenuItems.Add(MenuItems.GetData(), MenuItems.Count); // if the copy fails, IsGood() will return FALSE
                        if (oldMenuItems.IsGood())
                            MenuItems.DetachMembers(); // destroy them only after synchronization
                        else
                            MenuItems.DestroyMembers(); // discard all menu items; only the new ones apply
                    }

                    // remove the FS command from the change-drive menu; only the new one applies
                    if (!ChDrvMenuFSItemName.empty())
                        ChDrvMenuFSItemName.clear();
                    ChDrvMenuFSItemIconIndex = -1;

                    CSalamanderConnect salConnect(Plugins.GetIndexJustForConnect(this), supportCustomPack, supportCustomUnpack,
                                                  supportPanelView, supportPanelEdit, supportViewer);
                    {
                        CALL_STACK_MESSAGE3("PluginIface.Connect(,) (%ls v. %ls)", DLLName.c_str(), Version.c_str());
                        PluginIface.Connect(parent, &salConnect); // call the plugin's Connect
                        if (!SupportDynMenuExt)
                            HotKeysMerge(&oldMenuItems); // synchronize hot keys
                        HotKeysEnsureIntegrity();        // prevent conflicts with Salamander or another plugin
                    }
                    if (oldMenuItems.IsGood())
                        oldMenuItems.DestroyMembers(); // we can now discard the old array

                    if (SupportDynMenuExt)
                    {
                        BuildMenu(parent, TRUE);
                        if (releaseDynMenuIcons)
                            ReleasePluginDynMenuIcons(); // this object is unnecessary (for the next menu display everything is loaded again)
                    }
                }
                else
                {
                    if (PluginIface.NotEmpty())
                    {
                        if (!archiverOK)
                        {
                            TRACE_E("The plugin didn't provide interface for archiver (see GetInterfaceForArchiver).");
                        }
                        if (!viewerOK)
                        {
                            TRACE_E("The plugin didn't provide interface for viewer (see GetInterfaceForViewer).");
                        }
                        if (!FSOK)
                        {
                            TRACE_E("The plugin didn't provide interface for file-system (see GetInterfaceForFS).");
                        }
                        {
                            CALL_STACK_MESSAGE3("PluginIface.Release(,) (%ls v. %ls)", DLLName.c_str(), Version.c_str());
                            PluginIface.Release(parent, TRUE);
                        }
                        Plugins.EnterDataCS();
                        PluginIface.Init(NULL, 0);
                        Plugins.LeaveDataCS();
                        PluginIfaceForArchiver.Init(NULL);
                        PluginIfaceForViewer.Init(NULL);
                        PluginIfaceForMenuExt.Init(NULL, 0);
                        PluginIfaceForFS.Init(NULL, 0);
                        PluginIfaceForThumbLoader.Init(NULL, NULL, NULL);
                        SalamanderGeneral.Init(NULL);
                    }
                    SalamanderGeneral.Clear();
                    LegacyHost.reset();
                    HANDLES(FreeLibrary(DLL));
                    DLL = NULL;
                    BuiltForVersion = 0;

                    // clear icon overlays (if the plugin even managed to set them)
                    ReleaseIconOverlays();

                    if (!oldVer && salamander.ShowError())
                    { // the plugin is the correct version and did not call SetBasicPluginData successfully or unsuccessfully
                        if (!quiet)
                        {
                            std::wstring msg;
                            if (Name.empty() || Name[0] == 0)
                                msg = FormatStrW(LoadStrW(IDS_PLUGININVALID2), s);
                            else
                                msg = FormatStrW(LoadStrW(IDS_PLUGININVALID), Name.c_str(), s);
                            gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
                        }
                    }
                }
            }
            else // the plugin has no Salamander Plugin Entry Point ...
            {
                HANDLES(FreeLibrary(DLL));
                DLL = NULL;

                if (!quiet)
                {
                    std::wstring msg = FormatStrW(LoadStrW(IDS_UNABLETOFINDPLUGINENTRY), s);
                    gPrompter->ShowError(LoadStrW(IDS_ERRORTITLE), msg.c_str());
                }
            }
        }
        // inform the main window about the plugin load
        if (DLL != NULL)
            MainWindow->OnPluginsStateChanged();

        if (refreshUNCRootPaths && MainWindow != NULL &&
            MainWindow->LeftPanel != NULL && MainWindow->RightPanel != NULL)
        { // the return value of CPlugins::GetFirstNethoodPluginFSName() changed - it affects UNC root paths (whether an up-dir exists or not); refresh needed
            if (MainWindow->LeftPanel->Is(ptDisk) && IsUNCRootPathW(MainWindow->LeftPanel->GetPathW()))
            {
                HANDLES(EnterCriticalSection(&TimeCounterSection));
                int t1 = MyTimeCounter++;
                HANDLES(LeaveCriticalSection(&TimeCounterSection));
                PostMessage(MainWindow->LeftPanel->HWindow, WM_USER_REFRESH_DIR, 0, t1);
            }
            if (MainWindow->RightPanel->Is(ptDisk) && IsUNCRootPathW(MainWindow->RightPanel->GetPathW()))
            {
                HANDLES(EnterCriticalSection(&TimeCounterSection));
                int t1 = MyTimeCounter++;
                HANDLES(LeaveCriticalSection(&TimeCounterSection));
                PostMessage(MainWindow->RightPanel->HWindow, WM_USER_REFRESH_DIR, 0, t1);
            }
            if ((MainWindow->LeftPanel->Is(ptDisk) || MainWindow->LeftPanel->Is(ptZIPArchive)) &&
                IsUNCPathW(MainWindow->LeftPanel->GetPathW()) &&
                MainWindow->LeftPanel->DirectoryLine != NULL)
            {
                MainWindow->LeftPanel->DirectoryLine->BuildHotTrackItems();
            }
            if ((MainWindow->RightPanel->Is(ptDisk) || MainWindow->RightPanel->Is(ptZIPArchive)) &&
                IsUNCPathW(MainWindow->RightPanel->GetPathW()) &&
                MainWindow->RightPanel->DirectoryLine != NULL)
            {
                MainWindow->RightPanel->DirectoryLine->BuildHotTrackItems();
            }
            PostMessage(MainWindow->HWindow, WM_USER_DRIVES_CHANGE, 0, 0);
        }
    }
    return DLL != NULL;
}

std::wstring CPluginData::GetDisplayName() const
{
    const wchar_t* add = LoadStrW(IDS_PLUGINSUFFIX);
    return Name + add;
}

void CPluginData::AddMenuItem(int iconIndex, const wchar_t* name, DWORD hotKey, int id, BOOL callGetState,
                              DWORD state_or, DWORD state_and, DWORD skillLevel,
                              CPluginMenuItemType type)
{
    CALL_STACK_MESSAGE12("CPluginData::AddMenuItem(%d, %S, %u, %d, %d, 0x%X, 0x%X, 0x%X, %d) (%ls v. %ls)",
                         iconIndex, name, hotKey, id, callGetState, state_or, state_and,
                         skillLevel, (int)type, DLLName.c_str(), Version.c_str());
    DWORD state = 0;
    if (callGetState)
        state = -1;
    else
    {
        if (name != NULL)                                               // not a separator
            state = ((state_or & 0x7FFF) << 16) | (state_and & 0x7FFF); // ensure it never produces -1 (0xFFFFFFFF)
        else
            id = 0; // separators don't have an 'id' unless 'callGetState' is TRUE
    }
    if (type == pmitEndSubmenu)
    {
        name = NULL;
        state = 0;
        id = 0;
        skillLevel = MENU_SKILLLEVEL_ALL;
    }
    if (name == NULL)
        iconIndex = -1;
    //  if (type == pmitStartSubmenu && state != -1) id = 0;  // Petr: I don't know why this was here; anyway Shift+F1 for a disabled submenu is the only way I know (e.g. FTP Client/Transfer Mode)
    CPluginMenuItem* item = new CPluginMenuItem(iconIndex, name, hotKey, state, id, skillLevel, type);
    if (item != NULL)
    {
        MenuItems.Add(item);
        if (MenuItems.IsGood())
            item = NULL;
        else
            MenuItems.ResetState();
    }
    else
        TRACE_E(LOW_MEMORY);
    if (item != NULL)
        delete item;
}

BOOL CPluginData::GetMenuItemHotKey(int id, WORD* hotKey, std::wstring* hotKeyText)
{
    int i;
    for (i = 0; i < MenuItems.Count; i++)
    {
        CPluginMenuItem* item = MenuItems[i];
        if (item->ID == id)
        {
            if (hotKey != NULL)
                *hotKey = HOTKEY_GET(item->HotKey);
            if (hotKeyText != NULL)
                *hotKeyText = GetHotKeyText(HOTKEY_GET(item->HotKey));
            return TRUE;
        }
    }
    return FALSE;
}

void CPluginData::ClearSUID()
{
    int i;
    for (i = 0; i < MenuItems.Count; i++)
        MenuItems[i]->SUID = -1;
}

BOOL CPluginData::Remove(HWND parent, int index, BOOL canDelPluginRegKey)
{
    CALL_STACK_MESSAGE6("CPluginData::Remove(0x%p, %d, %d) (%ls v. %ls)",
                        parent, index, canDelPluginRegKey, DLLName.c_str(), Version.c_str());
    BOOL unloaded = !GetLoaded();
    if (!unloaded)
    {
        if (MainWindow == NULL || MainWindow->CanUnloadPlugin(parent, PluginIface.GetInterface()))
        {
            // the plugin may unload: let it delete any remaining temp files from the disk cache
            CPluginInterfaceAbstract* unloadedPlugin = PluginIface.GetInterface();
            DeleteManager.PluginMayBeUnloaded(parent, this);

            // the plugin is no longer used by Salamander; it can be unloaded
            if (PluginIface.Release(parent, FALSE))
                unloaded = TRUE; // will be unloaded and can be removed
            else
            {
                std::wstring msg = FormatStrW(LoadStrW(IDS_PLUGINFORCEUNLOAD), Name.c_str());
                if (gPrompter->AskYesNo(LoadStrW(IDS_QUESTION), msg.c_str()).type == PromptResult::kYes)
                {
                    PluginIface.Release(parent, TRUE);
                    unloaded = TRUE; // will be unloaded and can be removed
                }
            }
            if (unloaded)
            {
                // unload SPL+SLG and clean up the interfaces
                SalamanderGeneral.Clear();
                LegacyHost.reset();
                if (DLL != NULL)
                    HANDLES(FreeLibrary(DLL));
                DLL = NULL;
                BuiltForVersion = 0;
                Plugins.EnterDataCS();
                PluginIface.Init(NULL, 0);
                Plugins.LeaveDataCS();
                PluginIfaceForArchiver.Init(NULL);
                PluginIfaceForViewer.Init(NULL);
                PluginIfaceForMenuExt.Init(NULL, 0);
                PluginIfaceForFS.Init(NULL, 0);
                PluginIfaceForThumbLoader.Init(NULL, NULL, NULL);
                SalamanderGeneral.Init(NULL);

                // remove its icon overlays when unloading the plugin
                ReleaseIconOverlays();

                // disconnect the unloaded plugin from the delete manager and the disk cache
                DeleteManager.PluginWasUnloaded(this, unloadedPlugin);
            }
        }
    }

    if (unloaded)
    {
        // adjust "file viewer" - delete the records related to this plug-in + measures due to the shift of the Plugins array
        CViewerMasks* viewerMasks;
        MainWindow->EnterViewerMasksCS();
        int k;
        for (k = 0; k < 2; k++)
        {
            if (k == 0)
                viewerMasks = MainWindow->ViewerMasks;
            else
                viewerMasks = MainWindow->AltViewerMasks;
            int i;
            for (i = 0; i < viewerMasks->Count; i++)
            {
                int type = viewerMasks->At(i)->ViewerType;
                if (type < 0) // not external or internal -> plug-in viewer
                {
                    type = -type - 1;
                    if (type == index)
                        viewerMasks->Delete(i--); // this plugin -> delete record
                    else
                    {
                        if (type > index) // the Plugins array shifts -> decrease 'type' by one
                        {
                            type--;
                            viewerMasks->At(i)->ViewerType = -type - 1;
                        }
                    }
                }
            }
        }
        MainWindow->LeaveViewerMasksCS();

        // adjust "custom pack" - delete the records related to this plug-in + measures due to the shift of the Plugins array
        int i;
        for (i = 0; i < PackerConfig.GetPackersCount(); i++)
        {
            int type = PackerConfig.GetPackerType(i);
            if (type != CUSTOMPACKER_EXTERNAL) // not external
            {
                type = -type - 1;
                if (type == index)
                    PackerConfig.DeletePacker(i--); // this plugin -> delete record
                else
                {
                    if (type > index) // the Plugins array shifts -> decrease 'type' by one
                    {
                        type--;
                        PackerConfig.SetPackerType(i, -type - 1);
                    }
                }
            }
        }

        // adjust "custom unpack" - delete the records related to this plug-in + measures due to the shift of the Plugins array
        for (i = 0; i < UnpackerConfig.GetUnpackersCount(); i++)
        {
            int type = UnpackerConfig.GetUnpackerType(i);
            if (type != CUSTOMUNPACKER_EXTERNAL) // not external
            {
                type = -type - 1;
                if (type == index)
                    UnpackerConfig.DeleteUnpacker(i--); // this plugin -> delete record
                else
                {
                    if (type > index) // the Plugins array shifts -> decrease 'type' by one
                    {
                        type--;
                        UnpackerConfig.SetUnpackerType(i, -type - 1);
                    }
                }
            }
        }

        // adjust "panel view/edit" - modify or remove records related to this plugin
        // and handle shifting of the Plugins array
        for (i = 0; i < PackerFormatConfig.GetFormatsCount(); i++)
        {
            BOOL usePack = PackerFormatConfig.GetUsePacker(i);
            int pack;
            if (usePack)
                pack = PackerFormatConfig.GetPackerIndex(i);
            int unpack = PackerFormatConfig.GetUnpackerIndex(i);
            BOOL removePack = FALSE;
            BOOL removeUnpack = FALSE;
            if (unpack < 0) // not an external "view"
            {
                unpack = -unpack - 1;
                if (unpack == index)
                    removeUnpack = TRUE; // this plugin -> delete record
                else
                {
                    if (unpack > index) // Plugins array shifts -> decrease 'unpack' by one
                    {
                        unpack--;
                        PackerFormatConfig.SetUnpackerIndex(i, -unpack - 1);
                    }
                }
            }
            if (usePack && pack < 0) // not an external "edit"
            {
                pack = -pack - 1;
                if (pack == index)
                    removePack = TRUE; // this plugin -> delete record
                else
                {
                    if (pack > index) // the Plugins array shifts -> decrease 'pack' by one
                    {
                        pack--;
                        PackerFormatConfig.SetPackerIndex(i, -pack - 1);
                    }
                }
            }

            if (removePack || removeUnpack) // a replacement for "view" and/or "edit" is needed
            {
                // we will search for an archiver that supports "view" and/or "edit" for some of the extensions
                int newView, newEdit;
                BOOL viewFound, editFound;
                Plugins.FindViewEdit(PackerFormatConfig.GetExt(i), index, viewFound, newView, editFound, newEdit);
                if (newView < 0 && -newView - 1 > index)
                    newView++; // the Plugins array shifts -> adjustment needed
                if (newEdit < 0 && -newEdit - 1 > index)
                    newEdit++; // the Plugins array shifts -> adjustment needed

                if (removeUnpack) // need to replace "view"
                {
                    if (viewFound)
                        PackerFormatConfig.SetUnpackerIndex(i, newView); // use new "view"
                    else
                        PackerFormatConfig.DeleteFormat(i--); // cannot work without "view"
                }
                if (removePack &&                 // need to replace "edit" and
                    (!removeUnpack || viewFound)) // record was not deleted
                {
                    if (editFound)
                        PackerFormatConfig.SetPackerIndex(i, newEdit); // use new "edit"
                    else
                        PackerFormatConfig.SetUsePacker(i, FALSE); // no "edit"
                }
            }
        }
        PackerFormatConfig.BuildArray();

        if (SupportLoadSave && canDelPluginRegKey) // if the plugin supports load/save configuration + we can delete its registry key (not an import of the configuration from an older version of Salamander)
        {                                          // try to open the private registry key; if successful, delete it, it's no longer needed
            BOOL shouldDelete = FALSE;
            LoadSaveToRegistryMutex.Enter();
            HKEY salamander;
            if (SALAMANDER_ROOT_REG != NULL &&
                OpenKeyW(HKEY_CURRENT_USER, SALAMANDER_ROOT_REG, salamander))
            {
                HKEY actKey;
                if (OpenKeyW(salamander, SALAMANDER_PLUGINSCONFIG, actKey))
                {
                    HKEY regKey;
                    if (OpenKeyW(actKey, RegKeyName.c_str(), regKey))
                    {
                        shouldDelete = TRUE;
                        CloseKey(regKey);
                    }
                    CloseKey(actKey);
                }
                CloseKey(salamander);
            }
            if (shouldDelete)
            {
                if (SALAMANDER_ROOT_REG != NULL &&
                    CreateKeyW(HKEY_CURRENT_USER, SALAMANDER_ROOT_REG, salamander)) // ensure write permissions
                {
                    HKEY actKey;
                    if (CreateKeyW(salamander, SALAMANDER_PLUGINSCONFIG, actKey))
                    {
                        HKEY regKey;
                        if (CreateKeyW(actKey, RegKeyName.c_str(), regKey))
                        {
                            ClearKey(regKey);
                            CloseKey(regKey);
                        }
                        DeleteKeyW(actKey, RegKeyName.c_str());
                        CloseKey(actKey);
                    }
                    CloseKey(salamander);
                }
            }
            LoadSaveToRegistryMutex.Leave();
        }
        return TRUE;
    }
    ThumbnailMasksDisabled = FALSE; // removal was interrupted
    return FALSE;
}

void CPluginData::Save(HWND parent, HKEY regKeyConfig)
{
    CALL_STACK_MESSAGE5("CPluginData::Save(0x%p, 0x%p) (%ls v. %ls)", parent, regKeyConfig, DLLName.c_str(), Version.c_str());
    if (SupportLoadSave && InitDLL(parent))
    {
        HKEY regKey;
        if (CreateKeyW(regKeyConfig, RegKeyName.c_str(), regKey))
        {
            CSalamanderRegistry registry;
            PluginIface.SaveConfiguration(parent, regKey, &registry);
            CloseKey(regKey);
        }
    }
}

void CPluginData::Configuration(HWND parent)
{
    CALL_STACK_MESSAGE4("CPluginData::Configuration(0x%p) (%ls v. %ls)", parent, DLLName.c_str(), Version.c_str());
    if (InitDLL(parent))
    {
        PluginIface.Configuration(parent);
    }
}

void CPluginData::Event(int event, DWORD param)
{
    CALL_STACK_MESSAGE4("CPluginData::Event(%d,) (%ls v. %ls)", event, DLLName.c_str(), Version.c_str());
    if (GetLoaded() && PluginIface.NotEmpty()) // call only if the plugin is loaded (just a "notification")
    {
        PluginIface.Event(event, param);
    }
}

void CPluginData::ClearHistory(HWND parent)
{
    CALL_STACK_MESSAGE3("CPluginData::ClearHistory() (%ls v. %ls)", DLLName.c_str(), Version.c_str());
    if (InitDLL(parent))
    {
        PluginIface.ClearHistory(parent);
    }
}

void CPluginData::AcceptChangeOnPathNotification(const wchar_t* path, BOOL includingSubdirs)
{
    CALL_STACK_MESSAGE3("CPluginData::AcceptChangeOnPathNotification() (%ls v. %ls)", DLLName.c_str(), Version.c_str());
    if (GetLoaded() && PluginIface.NotEmpty()) // call only if the plugin is loaded (just a "notification")
    {
        PluginIface.AcceptChangeOnPathNotification(path, includingSubdirs);
    }
}

void CPluginData::PasswordManagerEvent(HWND parent, int event)
{
    CALL_STACK_MESSAGE4("CPluginData::PasswordManagerEvent(, %d) (%ls v. %ls)", event, DLLName.c_str(), Version.c_str());
    if (GetLoaded() && PluginUsesPasswordManager) // in case the plugin stopped using the Password Manager (did not call SetPluginUsesPasswordManager())
        PluginIface.PasswordManagerEvent(parent, event);
}

void CPluginData::About(HWND parent)
{
    CALL_STACK_MESSAGE4("CPluginData::About(0x%p) (%ls v. %ls)", parent, DLLName.c_str(), Version.c_str());
    if (InitDLL(parent))
    {
        PluginIface.About(parent);
    }
}

void CPluginData::CallLoadOrSaveConfiguration(BOOL load,
                                              FSalLoadOrSaveConfiguration loadOrSaveFunc,
                                              void* param)
{ // called from the plugin (no need to print DLLName + Version)
    CALL_STACK_MESSAGE2("CPluginData::CallLoadOrSaveConfiguration(%d, ,)", load);
    if (load) // load
    {
        BOOL loaded = FALSE;
        if (SupportLoadSave) // if load/save from registry is supported
        {
            LoadSaveToRegistryMutex.Enter();
            HKEY salamander;
            if (SALAMANDER_ROOT_REG != NULL &&
                OpenKeyW(HKEY_CURRENT_USER, SALAMANDER_ROOT_REG, salamander))
            {
                HKEY actKey;
                if (OpenKeyW(salamander, SALAMANDER_PLUGINSCONFIG, actKey))
                {
                    HKEY regKey;
                    if (OpenKeyW(actKey, RegKeyName.c_str(), regKey)) // try to open the plugin's private key
                    {
                        CSalamanderRegistry registry;
                        {
                            CALL_STACK_MESSAGE1("1.CPluginData::CallLoadOrSaveConfiguration::loadOrSaveFunc()");
                            loadOrSaveFunc(load, regKey, &registry, param);
                        }
                        loaded = TRUE;
                        CloseKey(regKey);
                    }
                    CloseKey(actKey);
                }
                CloseKey(salamander);
            }
            LoadSaveToRegistryMutex.Leave();
        }

        // otherwise load the default configuration
        if (!loaded)
        {
            CSalamanderRegistry registry;
            {
                CALL_STACK_MESSAGE1("2.CPluginData::CallLoadOrSaveConfiguration::loadOrSaveFunc()");
                loadOrSaveFunc(load, NULL, &registry, param);
            }
        }
    }
    else // save
    {
        if (SupportLoadSave) // otherwise there is nowhere to save
        {
            LoadSaveToRegistryMutex.Enter();
            HKEY salamander;
            IRegistry* registry = GetPluginsRegistry();
            if (SALAMANDER_ROOT_REG != NULL &&
                registry->OpenKeyRead(HKEY_CURRENT_USER, SALAMANDER_ROOT_REG, salamander).success) // check whether the Salamander key exists at all (otherwise nothing is saved)
            {
                registry->CloseKey(salamander);
                if (CreateKeyW(HKEY_CURRENT_USER, SALAMANDER_ROOT_REG, salamander))
                {
                    BOOL cfgIsOK = TRUE;
                    BOOL deleteSALAMANDER_SAVE_IN_PROGRESS = !IsSetSALAMANDER_SAVE_IN_PROGRESS;
                    if (deleteSALAMANDER_SAVE_IN_PROGRESS)
                    {
                        DWORD saveInProgress = 1;
                        if (registry->GetDWord(salamander, SALAMANDER_SAVE_IN_PROGRESS, saveInProgress).success)
                        {
                            cfgIsOK = FALSE; // corrupted configuration; saving won't fix it (not all data is stored)
                            TRACE_EW(L"CPluginData::CallLoadOrSaveConfiguration(): unable to save configuration, configuration key in registry is corrupted, plugin: " << Name);
                        }
                        else
                        {
                            saveInProgress = 1;
                            SetValueW(salamander, SALAMANDER_SAVE_IN_PROGRESS, REG_DWORD, &saveInProgress, sizeof(DWORD));
                            IsSetSALAMANDER_SAVE_IN_PROGRESS = TRUE;
                        }
                    }
                    if (cfgIsOK)
                    {
                        HKEY actKey;
                        if (CreateKeyW(salamander, SALAMANDER_PLUGINSCONFIG, actKey))
                        {
                            HKEY regKey;
                            if (CreateKeyW(actKey, RegKeyName.c_str(), regKey))
                            {
                                CSalamanderRegistry registry;
                                {
                                    CALL_STACK_MESSAGE1("3.CPluginData::CallLoadOrSaveConfiguration::loadOrSaveFunc()");
                                    loadOrSaveFunc(load, regKey, &registry, param);
                                }
                                CloseKey(regKey);
                            }
                            CloseKey(actKey);
                        }
                        if (deleteSALAMANDER_SAVE_IN_PROGRESS)
                        {
                            DeleteValueW(salamander, SALAMANDER_SAVE_IN_PROGRESS);
                            IsSetSALAMANDER_SAVE_IN_PROGRESS = FALSE;
                        }
                    }
                    CloseKey(salamander);
                }
            }
            LoadSaveToRegistryMutex.Leave();
        }
    }
}

BOOL CPluginData::Unload(HWND parent, BOOL ask)
{
    CALL_STACK_MESSAGE5("CPluginData::Unload(0x%p, %d) (%ls v. %ls)", parent, ask, DLLName.c_str(), Version.c_str());
    BOOL ret = FALSE;
    if (DLL != NULL)
    {
        if (MainWindow == NULL || MainWindow->CanUnloadPlugin(parent, PluginIface.GetInterface()))
        { // the plugin is no longer used by Salamander; it can be unloaded
            BOOL skipUnload = FALSE;
            if (SupportLoadSave && ::Configuration.AutoSave)
            { // ask if the user wants to save configuration when "save on exit" is on
                std::wstring msg = FormatStrW(LoadStrW(IDS_PLUGINSAVECONFIG), Name.c_str());
                if (!ask || gPrompter->AskYesNo(LoadStrW(IDS_QUESTION), msg.c_str()).type == PromptResult::kYes)
                {
                    LoadSaveToRegistryMutex.Enter();
                    BOOL salKeyDoesNotExist = FALSE;
                    HKEY salamander;
                    IRegistry* registry = GetPluginsRegistry();
                    if (SALAMANDER_ROOT_REG != NULL &&
                        registry->OpenKeyRead(HKEY_CURRENT_USER, SALAMANDER_ROOT_REG, salamander).success) // check whether the Salamander key exists at all (otherwise nothing is saved)
                    {
                        registry->CloseKey(salamander);
                        if (CreateKeyW(HKEY_CURRENT_USER, SALAMANDER_ROOT_REG, salamander))
                        {
                            BOOL cfgIsOK = TRUE;
                            BOOL deleteSALAMANDER_SAVE_IN_PROGRESS = !IsSetSALAMANDER_SAVE_IN_PROGRESS;
                            if (deleteSALAMANDER_SAVE_IN_PROGRESS)
                            {
                                DWORD saveInProgress = 1;
                                if (registry->GetDWord(salamander, SALAMANDER_SAVE_IN_PROGRESS, saveInProgress).success)
                                {
                                    cfgIsOK = FALSE; // corrupted configuration; saving won't fix it (not all data is stored)
                                    salKeyDoesNotExist = TRUE;
                                    TRACE_EW(L"CPluginData::Unload(): unable to save configuration, configuration key in registry is corrupted, plugin: " << Name);
                                }
                                else
                                {
                                    saveInProgress = 1;
                                    SetValueW(salamander, SALAMANDER_SAVE_IN_PROGRESS, REG_DWORD, &saveInProgress, sizeof(DWORD));
                                    IsSetSALAMANDER_SAVE_IN_PROGRESS = TRUE;
                                }
                            }
                            if (cfgIsOK)
                            {
                                HKEY actKey;
                                if (CreateKeyW(salamander, SALAMANDER_PLUGINSCONFIG, actKey))
                                {
                                    Save(parent, actKey);
                                    CloseKey(actKey);
                                }
                                if (deleteSALAMANDER_SAVE_IN_PROGRESS)
                                {
                                    DeleteValueW(salamander, SALAMANDER_SAVE_IN_PROGRESS);
                                    IsSetSALAMANDER_SAVE_IN_PROGRESS = FALSE;
                                }
                            }
                            CloseKey(salamander);
                        }
                        else
                            salKeyDoesNotExist = TRUE;
                    }
                    else
                        salKeyDoesNotExist = TRUE;
                    LoadSaveToRegistryMutex.Leave();

                    if (ask && salKeyDoesNotExist)
                    {
                        std::wstring failMsg = FormatStrW(LoadStrW(IDS_PLUGINSAVEFAILED), Name.c_str());
                        skipUnload = gPrompter->AskYesNo(LoadStrW(IDS_QUESTION), failMsg.c_str()).type == PromptResult::kNo;
                    }
                }
                if (GlobalSaveWaitWindow != NULL)
                    GlobalSaveWaitWindow->SetProgressPos(++GlobalSaveWaitWindowProgress);
            }

            if (!skipUnload && PluginIface.NotEmpty())
            {
                // the plugin may unload: let it delete any remaining temp files from the disk cache
                CPluginInterfaceAbstract* unloadedPlugin = PluginIface.GetInterface();
                DeleteManager.PluginMayBeUnloaded(parent, this);

                if (PluginIface.Release(parent, CriticalShutdown) || CriticalShutdown)
                    ret = TRUE;
                else
                {
                    std::wstring forceMsg = FormatStrW(LoadStrW(IDS_PLUGINFORCEUNLOAD), Name.c_str());
                    if (gPrompter->AskYesNo(LoadStrW(IDS_QUESTION), forceMsg.c_str()).type == PromptResult::kYes)
                    {
                        PluginIface.Release(parent, TRUE);
                        ret = TRUE;
                    }
                }

                if (ret)
                {
                    // unload SPL+SLG and clean up the interfaces
                    SalamanderGeneral.Clear();
                    LegacyHost.reset();
                    if (DLL != NULL)
                        HANDLES(FreeLibrary(DLL));
                    DLL = NULL;
                    BuiltForVersion = 0;
                    Plugins.EnterDataCS();
                    PluginIface.Init(NULL, 0);
                    Plugins.LeaveDataCS();
                    PluginIfaceForArchiver.Init(NULL);
                    PluginIfaceForViewer.Init(NULL);
                    PluginIfaceForMenuExt.Init(NULL, 0);
                    PluginIfaceForFS.Init(NULL, 0);
                    PluginIfaceForThumbLoader.Init(NULL, NULL, NULL);
                    SalamanderGeneral.Init(NULL);

                    // when unloading the plugin, remove its icon overlays
                    ReleaseIconOverlays();

                    // disconnect the unloaded plugin from the delete manager and the disk cache
                    DeleteManager.PluginWasUnloaded(this, unloadedPlugin);
                }
            }
        }
        ThumbnailMasksDisabled = FALSE; // unloading completed (we can safely allow the plugin to load again)
    }
    return ret;
}

BOOL CPluginData::GetMenuItemStateType(int pluginIndex, int menuItemIndex, MENU_ITEM_INFO* mii)
{
    CPluginMenuItem* item = MenuItems[menuItemIndex];

    DWORD mask = GetMaskForMenuItems(pluginIndex);

    if (item->StateMask == -1) // should the item’s state be queried directly from the plug-in?
    {
        DWORD state = 0;
        if (PluginIfaceForMenuExt.NotEmpty())
            state = PluginIfaceForMenuExt.GetMenuItemState(item->ID, mask);
        else
            TRACE_E("PluginIfaceForMenuExt is not initialized!");

        //    if (state & MENU_ITEM_STATE_HIDDEN) hidden = TRUE;
        if (state & MENU_ITEM_STATE_ENABLED)
            mii->State = 0;
        else
            mii->State = MENU_STATE_GRAYED;
        if (state & MENU_ITEM_STATE_CHECKED)
        {
            mii->State |= MENU_STATE_CHECKED;
            if (state & MENU_ITEM_STATE_RADIO)
                mii->Type |= MENU_TYPE_RADIOCHECK;
        }
    }
    else // the item state is computed from the AND and OR masks
    {
        if ((mask & HIWORD(item->StateMask)) != 0 &&                     // OR mask
            (mask & LOWORD(item->StateMask)) == LOWORD(item->StateMask)) // AND mask
        {
            mii->State = 0;
        }
        else
        {
            mii->State = MENU_STATE_GRAYED;
        }
    }
    return (mii->State != MENU_STATE_GRAYED);
}

void CPluginData::AddMenuItemsToSubmenuAux(CMenuPopup* menu, int& i, int count, DWORD mask)
{
    CALL_STACK_MESSAGE4("CPluginData::AddMenuItemsToSubmenuAux(, %d, %d, 0x%X)", i, count, mask);
    for (; i < MenuItems.Count; i++)
    {
        CPluginMenuItem* item = MenuItems[i];
        if (item->Type == pmitEndSubmenu)
            return; // return from the submenu
        BOOL skipSubMenu = FALSE;
        if (item->SkillLevel & CfgSkillLevelToMenu(::Configuration.SkillLevel)) // apply the skill-level menu reduction
        {
            BOOL hidden = FALSE;
            MENU_ITEM_INFO mi;
            std::wstring menuText;
            if (item->Name.empty()) // separator or failed allocation of start-submenu name
            {
                if (item->Type == pmitStartSubmenu)
                    skipSubMenu = TRUE; // failed to allocate start-submenu name: insert separator and skip the rest of the submenu
                mi.Mask = MENU_MASK_TYPE | MENU_MASK_SKILLLEVEL;
                mi.Type = MENU_TYPE_SEPARATOR;
                mi.SkillLevel = 0;
                if (item->SkillLevel & MENU_SKILLLEVEL_BEGINNER)
                    mi.SkillLevel |= MENU_LEVEL_BEGINNER;
                if (item->SkillLevel & MENU_SKILLLEVEL_INTERMEDIATE)
                    mi.SkillLevel |= MENU_LEVEL_INTERMEDIATE;
                if (item->SkillLevel & MENU_SKILLLEVEL_ADVANCED)
                    mi.SkillLevel |= MENU_LEVEL_ADVANCED;
                if (item->StateMask == -1) // should the visibility of the separator be queried directly from the plug-in?
                {
                    DWORD state = PluginIfaceForMenuExt.GetMenuItemState(item->ID, mask);

                    if (state & MENU_ITEM_STATE_HIDDEN)
                        hidden = TRUE;
                }
            }
            else // a regular menu or submenu item
            {
                mi.Mask = MENU_MASK_TYPE | MENU_MASK_STATE | MENU_MASK_ID |
                          MENU_MASK_STRING | MENU_MASK_SKILLLEVEL | MENU_MASK_IMAGEINDEX |
                          (item->Type == pmitStartSubmenu ? MENU_MASK_SUBMENU : 0);
                mi.Type = MENU_TYPE_STRING;
                // MENU_ITEM_INFO::String (plugins/shared/spl_gui.h) is wide in the
                // live SDK; the frozen sdk107 snapshot alone still has it narrow. item->Name is
                // already wide, so no WideToAnsi bridge is needed here.
                menuText = item->Name;
                mi.String = menuText.data();

                if (HOTKEY_GET(item->HotKey) != 0)
                {
                    // if we have a hint in the text, remove it
                    if ((item->HotKey & HOTKEY_HINT) != 0)
                    {
                        const std::size_t hint = menuText.find(L'\t');
                        if (hint != std::wstring::npos)
                            menuText.resize(hint);
                    }

                    menuText += L'\t';
                    menuText += GetHotKeyText(LOWORD(item->HotKey));
                    mi.String = menuText.data();
                }
                mi.ImageIndex = item->IconIndex;
                mi.SkillLevel = 0;
                if (item->SkillLevel & MENU_SKILLLEVEL_BEGINNER)
                    mi.SkillLevel |= MENU_LEVEL_BEGINNER;
                if (item->SkillLevel & MENU_SKILLLEVEL_INTERMEDIATE)
                    mi.SkillLevel |= MENU_LEVEL_INTERMEDIATE;
                if (item->SkillLevel & MENU_SKILLLEVEL_ADVANCED)
                    mi.SkillLevel |= MENU_LEVEL_ADVANCED;
                if (item->StateMask == -1) // should the item’s state be queried directly from the plug-in?
                {
                    DWORD state = PluginIfaceForMenuExt.GetMenuItemState(item->ID, mask);

                    if (state & MENU_ITEM_STATE_HIDDEN)
                        hidden = TRUE;
                    if (state & MENU_ITEM_STATE_ENABLED)
                        mi.State = 0;
                    else
                        mi.State = MENU_STATE_GRAYED;
                    if (state & MENU_ITEM_STATE_CHECKED)
                    {
                        mi.State |= MENU_STATE_CHECKED;
                        if (state & MENU_ITEM_STATE_RADIO)
                            mi.Type |= MENU_TYPE_RADIOCHECK;
                    }
                }
                else // the item state is computed from the AND and OR masks
                {
                    if ((mask & HIWORD(item->StateMask)) != 0 &&                     // OR mask
                        (mask & LOWORD(item->StateMask)) == LOWORD(item->StateMask)) // AND mask
                    {
                        mi.State = 0;
                    }
                    else
                    {
                        mi.State = MENU_STATE_GRAYED;
                    }
                }

                mi.ID = Plugins.LastSUID;      // another unique number within Salamander (SUID)
                item->SUID = Plugins.LastSUID; // remember which SUID was assigned
                if (Plugins.LastSUID < CM_PLUGINCMD_MAX)
                    Plugins.LastSUID++; // generate another SUID
                else
                    TRACE_E("Too much commands in plugins.");

                if (item->Type == pmitStartSubmenu && !hidden) // let the submenu be populated
                {
                    mi.SubMenu = new CMenuPopup();
                    if ((mi.State & MENU_STATE_GRAYED) == 0 && mi.SubMenu != NULL)
                    {
                        i++;
                        AddMenuItemsToSubmenuAux((CMenuPopup*)mi.SubMenu, i, 0, mask);
                        if (i >= MenuItems.Count)
                            TRACE_E("CPluginData::AddMenuItemsToSubmenuAux(): missing symbol of end of submenu - see CSalamanderConnectAbstract::AddSubmenuEnd()");
                    }
                    else
                    {
                        if (mi.SubMenu == NULL)
                        {
                            mi.Mask &= ~MENU_MASK_SUBMENU; // we turn the submenu into a normal item; nothing else can be done (it will run command number 0, hopefully that’s not a problem…)
                            TRACE_E(LOW_MEMORY);
                        }
                        skipSubMenu = TRUE; // submenu could not be allocated or is disabled - skip it
                    }
                }
            }
            if (!hidden)
            {
                if (!menu->InsertItem(count++, TRUE, &mi) && (mi.Mask & MENU_MASK_SUBMENU) && mi.SubMenu != NULL)
                    delete mi.SubMenu;
            }
            else
                skipSubMenu = TRUE; // item hidden due to the hidden state
        }
        else
            skipSubMenu = TRUE; // item hidden due to skill level

        if (skipSubMenu && item->Type == pmitStartSubmenu)
        { // if it is a submenu, skip nested items and submenus (they won't be touched at all)
            int level = 1;
            for (i++; i < MenuItems.Count; i++)
            {
                CPluginMenuItemType type = MenuItems[i]->Type;
                if (type == pmitStartSubmenu)
                    level++;
                else
                {
                    if (type == pmitEndSubmenu && --level == 0)
                        break; // end of submenu found
                }
            }
        }
    }
}

DWORD
CPluginData::GetMaskForMenuItems(int index)
{
    DWORD mask = Plugins.StateCache.ActualStateMask;
    if (index == Plugins.StateCache.ActiveUnpackerIndex || index == Plugins.StateCache.ActivePackerIndex)
    {
        mask |= MENU_EVENT_THIS_PLUGIN_ARCH;
    }
    if (index == Plugins.StateCache.NonactiveUnpackerIndex || index == Plugins.StateCache.NonactivePackerIndex)
    {
        mask |= MENU_EVENT_TARGET_THIS_PLUGIN_ARCH;
    }
    if (index == Plugins.StateCache.ActiveFSIndex)
        mask |= MENU_EVENT_THIS_PLUGIN_FS;
    if (index == Plugins.StateCache.NonactiveFSIndex)
        mask |= MENU_EVENT_TARGET_THIS_PLUGIN_FS;
    if (index == Plugins.StateCache.FileUnpackerIndex || index == Plugins.StateCache.FilePackerIndex)
    {
        mask |= MENU_EVENT_ARCHIVE_FOCUSED;
    }
    return mask;
}

void CPluginData::InitMenuItems(HWND parent, int index, CMenuPopup* menu)
{
    CALL_STACK_MESSAGE4("CPluginData::InitMenuItems(, %d, ) (%ls v. %ls)", index, DLLName.c_str(), Version.c_str());
    int count = menu->GetItemCount();
    if (count == 0) // submenu needs to be initialized
    {

    CHECK_MENU_AGAIN:

        BOOL ok = TRUE;
        if (SupportDynMenuExt) // dynamic menu: rebuild it, if it changes to a static one when the plugin loads, we will check it below
        {
            if (GetLoaded())
                BuildMenu(parent, FALSE); // already loaded -> rebuild the menu manually
            else
                InitDLL(parent, FALSE, TRUE, TRUE, FALSE); // not loaded -> the menu will rebuild itself during the plugin load
            if (!GetLoaded() || SupportDynMenuExt && !PluginIfaceForMenuExt.NotEmpty())
                ok = FALSE;
            else
            {
                if (SupportDynMenuExt && MenuItems.Count == 0)
                    TRACE_I("Plugin has dynamic menu which is empty (unexpected situation). We will not open submenu.");
            }
        }
        // static menu: determine whether there is a reason to load the plugin (item state obtained via
        // GetMenuItemState) and also whether the plugin returns PluginIfaceForMenuExt in that case (required)
        if (ok && !SupportDynMenuExt)
        {
            DWORD mask = GetMaskForMenuItems(index);
            int i;
            for (i = 0; i < MenuItems.Count; i++)
            {
                CPluginMenuItem* item = MenuItems[i];
                BOOL skipSubMenu = FALSE;
                if (item->SkillLevel & CfgSkillLevelToMenu(::Configuration.SkillLevel)) // apply the skill-level menu reduction
                {
                    if (item->StateMask == -1)
                    {
                        if (GetLoaded()) // if already loaded, check whether we have the menu extension interface
                        {
                            if (!PluginIfaceForMenuExt.NotEmpty())
                                TRACE_E("Plugin has menu with items whose state is determined by calling CPluginInterfaceForMenuExtAbstract::GetMenuItemState so it must have menu extension interface (see CPluginInterfaceAbstract::GetInterfaceForMenuExt).");
                        }
                        else // the DLL will load -> menu items will be updated, full test must run again
                        {
                            if (InitDLL(parent))
                                goto CHECK_MENU_AGAIN;
                        }
                        ok = GetLoaded() && PluginIfaceForMenuExt.NotEmpty();
                        break;
                    }
                    else
                    {
                        if ((mask & HIWORD(item->StateMask)) == 0 ||                     // OR mask
                            (mask & LOWORD(item->StateMask)) != LOWORD(item->StateMask)) // AND mask
                        {                                                                // disabled item
                            skipSubMenu = TRUE;
                        }
                    }
                }
                else
                    skipSubMenu = TRUE; // item hidden due to skill level

                if (skipSubMenu && item->Type == pmitStartSubmenu)
                { // if it is a submenu, skip nested items and submenus (they won't be touched at all)
                    int level = 1;
                    for (i++; i < MenuItems.Count; i++)
                    {
                        CPluginMenuItemType type = MenuItems[i]->Type;
                        if (type == pmitStartSubmenu)
                            level++;
                        else
                        {
                            if (type == pmitEndSubmenu && --level == 0)
                                break; // end of submenu found
                        }
                    }
                }
            }
        }

        if (ok)
        {
            int i = 0;
            DWORD mask = GetMaskForMenuItems(index); // if the plugin was loaded, something may have changed
            AddMenuItemsToSubmenuAux(menu, i, count, mask);
            if (i < MenuItems.Count)
                TRACE_E("CPluginData::InitMenuItems(): superfluous symbol of end of submenu - see CSalamanderConnectAbstract::AddSubmenuEnd()");
        }
    }
}

BOOL CPluginData::ExecuteMenuItem(CFilesWindow* panel, HWND parent, int index, int suid, BOOL& unselect)
{
    CALL_STACK_MESSAGE5("CPluginData::ExecuteMenuItem(, , %d, %d, ) (%ls v. %ls)", index, suid, DLLName.c_str(), Version.c_str());
    unselect = FALSE;
    int id;
    int i;
    for (i = 0; i < MenuItems.Count; i++)
    {
        if (MenuItems[i]->SUID == suid) // comparing the menu item's SUID with the executed command
        {
            id = MenuItems[i]->ID;
            if (InitDLL(parent) && PluginIfaceForMenuExt.NotEmpty())
            {
                DWORD mask = GetMaskForMenuItems(index);
                CSalamanderForOperations sm(panel);
                unselect = PluginIfaceForMenuExt.ExecuteMenuItem(&sm, parent, id, mask);
            }
            Plugins.SetLastPlgCmd(DLLName.c_str(), id); // save last command
            return TRUE;
        }
    }
    return FALSE;
}

BOOL CPluginData::ExecuteMenuItem2(CFilesWindow* panel, HWND parent, int index, int id, BOOL& unselect)
{
    CALL_STACK_MESSAGE5("CPluginData::ExecuteMenuItem2(, , %d, %d, ) (%ls v. %ls)", index, id, DLLName.c_str(), Version.c_str());
    unselect = FALSE;
    int i;
    for (i = 0; i < MenuItems.Count; i++)
    {
        if (MenuItems[i]->ID == id) // comparing the menu item's ID with the executed command
        {
            if (PluginIfaceForMenuExt.NotEmpty())
            {
                DWORD mask = GetMaskForMenuItems(index);
                CSalamanderForOperations sm(panel);
                unselect = PluginIfaceForMenuExt.ExecuteMenuItem(&sm, parent, id, mask);
            }
            else
                TRACE_E("PluginIfaceForMenuExt is not initialized!");
            Plugins.SetLastPlgCmd(DLLName.c_str(), id); // save last command
            return TRUE;
        }
    }
    return FALSE;
}

BOOL CPluginData::HelpForMenuItem(HWND parent, int index, int suid, BOOL& helpDisplayed)
{
    CALL_STACK_MESSAGE5("CPluginData::HelpForMenuItem(, %d, %d, ) (%ls v. %ls)", index, suid, DLLName.c_str(), Version.c_str());
    helpDisplayed = FALSE;
    int id;
    int i;
    for (i = 0; i < MenuItems.Count; i++)
    {
        if (MenuItems[i]->SUID == suid) // comparing the menu item's SUID with the executed command
        {
            id = MenuItems[i]->ID;
            if (InitDLL(parent) && PluginIfaceForMenuExt.NotEmpty())
                helpDisplayed = PluginIfaceForMenuExt.HelpForMenuItem(parent, id);
            return TRUE;
        }
    }
    return FALSE;
}

BOOL CPluginData::BuildMenu(HWND parent, BOOL force)
{
    CALL_STACK_MESSAGE3("CPluginData::BuildMenu() (%ls v. %ls)", DLLName.c_str(), Version.c_str());
    if (GetLoaded() && SupportDynMenuExt && (!DynMenuWasAlreadyBuild || force))
    {
        DynMenuWasAlreadyBuild = TRUE; // prevent needless rebuilds of the menu, especially when building the menu for Last Command and again when opening a plugin submenu containing command in the Last Command
        if (PluginDynMenuIcons != NULL)
            TRACE_E("CPluginData::BuildMenu(): PluginDynMenuIcons is not NULL, please contact Petr Solin");
        ReleasePluginDynMenuIcons(); // drop it if it exists; it's unnecessary
        if (PluginIfaceForMenuExt.NotEmpty())
        {
            // instead of destroying it, we back up the old array
            TIndirectArray<CPluginMenuItem> oldMenuItems(max(1, MenuItems.Count), 1); // copy of the menu for hot key synchronization
            oldMenuItems.Add(MenuItems.GetData(), MenuItems.Count);                   // if the copy fails, IsGood() will return FALSE
            if (oldMenuItems.IsGood())
                MenuItems.DetachMembers(); // destroy them only after synchronization
            else
                MenuItems.DestroyMembers(); // remove all menu items; only the new ones apply

            CSalamanderBuildMenu salBuildMenu(Plugins.GetIndexJustForConnect(this));
            {
                CALL_STACK_MESSAGE3("PluginIfaceForMenuExt.BuildMenu(,) (%ls v. %ls)", DLLName.c_str(), Version.c_str());
                PluginIfaceForMenuExt.BuildMenu(parent, &salBuildMenu); // call the plugin's BuildMenu
                HotKeysMerge(&oldMenuItems);                            // synchronize hot keys
                HotKeysEnsureIntegrity();                               // prevent conflicts with Salamander or another plugin
            }
            if (oldMenuItems.IsGood())
                oldMenuItems.DestroyMembers(); // we can now discard the old array
        }
        else
            TRACE_E("Plugin has dynamic menu so it must have menu extension interface (see CPluginInterfaceAbstract::GetInterfaceForMenuExt).");
    }
    return GetLoaded() && (!SupportDynMenuExt || PluginIfaceForMenuExt.NotEmpty());
}

BOOL CPluginData::ListArchive(CFilesWindow* panel, const wchar_t* archiveFileName, CSalamanderDirectory& dir,
                              CPluginDataInterfaceAbstract*& pluginData)
{
    CALL_STACK_MESSAGE4("CPluginData::ListArchive(, %ls, ,) (%ls v. %ls)", archiveFileName, DLLName.c_str(), Version.c_str());
    BOOL ret = FALSE;
    if (InitDLL(MainWindow->HWindow))
    {
        CSalamanderForOperations sc(panel);
        ret = PluginIfaceForArchiver.ListArchive(&sc, archiveFileName, &dir, pluginData);
#ifdef _DEBUG
        if (ret && pluginData != NULL)
            OpenedPDCounter++; // increment OpenedPDCounter
#endif
    }
    return ret;
}

BOOL CPluginData::UnpackArchive(CFilesWindow* panel, const wchar_t* archiveFileName,
                                CPluginDataInterfaceAbstract* pluginData,
                                const wchar_t* targetDir, const wchar_t* archiveRoot,
                                SalEnumSelection nextName, void* param)
{
    CALL_STACK_MESSAGE6("CPluginData::UnpackArchive(, %ls, , %ls, %ls, ,) (%ls v. %ls)", archiveFileName,
                        targetDir, archiveRoot, DLLName.c_str(), Version.c_str());
    BOOL ret = FALSE;
    if (InitDLL(MainWindow->HWindow))
    {
        CSalamanderForOperations sc(panel);
        ret = PluginIfaceForArchiver.UnpackArchive(&sc, archiveFileName, pluginData, targetDir,
                                                   archiveRoot, nextName, param);
    }
    return ret;
}

BOOL CPluginData::UnpackOneFile(CFilesWindow* panel, const wchar_t* archiveFileName,
                                CPluginDataInterfaceAbstract* pluginData, const wchar_t* nameInArchive,
                                const CFileData* fileData, const wchar_t* targetDir,
                                const wchar_t* newFileName, BOOL* renamingNotSupported)
{
    CALL_STACK_MESSAGE7("CPluginData::UnpackOneFile(, %ls, , %ls, , %ls, %ls, ) (%ls v. %ls)", archiveFileName,
                        nameInArchive, targetDir, newFileName, DLLName.c_str(), Version.c_str());
    BOOL ret = FALSE;
    if (InitDLL(MainWindow->HWindow))
    {
        CSalamanderForOperations sc(panel);
        CreateSafeWaitWindow(LoadStrW(IDS_UNPACKINGFILEFROMARC), NULL, 2000, FALSE, MainWindow->HWindow);
        ret = PluginIfaceForArchiver.UnpackOneFile(&sc, archiveFileName, pluginData, nameInArchive,
                                                   fileData, targetDir, newFileName, renamingNotSupported);
        DestroySafeWaitWindow();
    }
    return ret;
}

BOOL CPluginData::PackToArchive(CFilesWindow* panel, const wchar_t* archiveFileName,
                                const wchar_t* archiveRoot, BOOL move, const wchar_t* sourceDir,
                                SalEnumSelection2 nextName, void* param)
{
    CALL_STACK_MESSAGE7("CPluginData::PackToArchive(, %ls, %ls, %d, %ls, ,) (%ls v. %ls)", archiveFileName,
                        archiveRoot, move, sourceDir, DLLName.c_str(), Version.c_str());
    BOOL ret = FALSE;
    if (InitDLL(MainWindow->HWindow))
    {
        CSalamanderForOperations sc(panel);
        ret = PluginIfaceForArchiver.PackToArchive(&sc, archiveFileName, archiveRoot, move, sourceDir, nextName, param);
    }
    return ret;
}

BOOL CPluginData::DeleteFromArchive(CFilesWindow* panel, const wchar_t* archiveFileName,
                                    CPluginDataInterfaceAbstract* pluginData, const wchar_t* archiveRoot,
                                    SalEnumSelection nextName, void* param)
{
    CALL_STACK_MESSAGE5("CPluginData::DeleteFromArchive(, %ls, , %ls, ,) (%ls v. %ls)",
                        archiveFileName, archiveRoot, DLLName.c_str(), Version.c_str());
    BOOL ret = FALSE;
    if (InitDLL(MainWindow->HWindow))
    {
        CSalamanderForOperations sc(panel);
        ret = PluginIfaceForArchiver.DeleteFromArchive(&sc, archiveFileName, pluginData, archiveRoot, nextName, param);
    }
    return ret;
}

BOOL CPluginData::UnpackWholeArchive(CFilesWindow* panel, const wchar_t* archiveFileName, const wchar_t* mask,
                                     const wchar_t* targetDir, BOOL delArchiveWhenDone, CDynamicString* archiveVolumes)
{
    CALL_STACK_MESSAGE7("CPluginData::UnpackWholeArchive(, %ls, %ls, %ls, %d,) (%ls v. %ls)", archiveFileName,
                        mask, targetDir, delArchiveWhenDone, DLLName.c_str(), Version.c_str());
    BOOL ret = FALSE;
    if (InitDLL(MainWindow->HWindow))
    {
        CSalamanderForOperations sc(panel);
        ret = PluginIfaceForArchiver.UnpackWholeArchive(&sc, archiveFileName, mask, targetDir,
                                                        delArchiveWhenDone, archiveVolumes);
    }
    return ret;
}

BOOL CPluginData::CanCloseArchive(CFilesWindow* panel, const wchar_t* archiveFileName, BOOL force)
{
    CALL_STACK_MESSAGE5("CPluginData::CanCloseArchive(, %ls, %d) (%ls v. %ls)", archiveFileName,
                        force, DLLName.c_str(), Version.c_str());
    BOOL ret = TRUE;
    if (InitDLL(MainWindow->HWindow))
    {
        CSalamanderForOperations sc(panel);
        ret = PluginIfaceForArchiver.CanCloseArchive(&sc, archiveFileName, force,
                                                     (panel == MainWindow->LeftPanel) ? PANEL_LEFT : PANEL_RIGHT);
        if (force)
            ret = TRUE;
    }
    return ret;
}

BOOL CPluginData::CanViewFile(const wchar_t* name)
{
    CALL_STACK_MESSAGE4("CPluginData::CanViewFile(%ls) (%ls v. %ls)", name, DLLName.c_str(), Version.c_str());
    BOOL ret = FALSE;
    if (InitDLL(MainWindow->HWindow)
        /*&& PluginIfaceForViewer.NotEmpty()*/) // unnecessary, because downgrade is not possible and InitDLL checks the interfaces
    {
        ret = PluginIfaceForViewer.CanViewFile(name);
    }
    return ret;
}

BOOL CPluginData::ViewFile(const wchar_t* name, int left, int top, int width, int height,
                           UINT showCmd, BOOL alwaysOnTop, BOOL returnLock,
                           HANDLE* lock, BOOL* lockOwner, int enumFilesSourceUID,
                           int enumFilesCurrentIndex)
{
    CALL_STACK_MESSAGE13("CPluginData::ViewFile(%ls, %d, %d, %d, %d, %u, %d, %d, , , %d, %d) (%ls v. %ls)",
                         name, left, top, width, height, showCmd, alwaysOnTop, returnLock,
                         enumFilesSourceUID, enumFilesCurrentIndex, DLLName.c_str(), Version.c_str());
    BOOL ret = FALSE;
    if (InitDLL(MainWindow->HWindow)
        /*&& PluginIfaceForViewer.NotEmpty()*/) // unnecessary, because downgrade is impossible and InitDLL checks the interfaces
    {
        ret = PluginIfaceForViewer.ViewFile(name, left, top, width, height, showCmd, alwaysOnTop,
                                            returnLock, lock, lockOwner, NULL, enumFilesSourceUID,
                                            enumFilesCurrentIndex);
        if (ret && returnLock && *lock != NULL && *lockOwner)
        { // add the 'lock' handle to HANDLES (disk cache will want to close it - it will search for it)
            HANDLES_ADD(__htEvent, __hoCreateEvent, *lock);
        }
    }
    return ret;
}

CPluginFSInterfaceAbstract*
CPluginData::OpenFS(const wchar_t* fsName, int fsNameIndex)
{
    CALL_STACK_MESSAGE5("CPluginData::OpenFS(%ls, %d) (%ls v. %ls)", fsName, fsNameIndex, DLLName.c_str(), Version.c_str());
    CPluginFSInterfaceAbstract* ret = NULL;
    if (InitDLL(MainWindow->HWindow)
        /*&& PluginIfaceForFS.NotEmpty()*/) // unnecessary, because downgrade is impossible and InitDLL checks the interfaces
    {
        ret = PluginIfaceForFS.OpenFS(fsName, fsNameIndex);
#ifdef _DEBUG
        if (ret != NULL)
            OpenedFSCounter++; // increase OpenedFSCounter
#endif
    }
    return ret;
}

void CPluginData::ExecuteChangeDriveMenuItem(int panel)
{
    CALL_STACK_MESSAGE4("CPluginData::ExecuteChangeDriveMenuItem(%d) (%ls v. %ls)", panel, DLLName.c_str(), Version.c_str());
    if (InitDLL(MainWindow->HWindow) &&
        !ChDrvMenuFSItemName.empty()         // in case the plugin removed the item during this load
        /*&& PluginIfaceForFS.NotEmpty()*/) // unnecessary, because downgrade is impossible and InitDLL checks the interfaces
    {
        PluginIfaceForFS.ExecuteChangeDriveMenuItem(panel);
    }
}

BOOL CPluginData::ChangeDriveMenuItemContextMenu(HWND parent, int panel, int x, int y,
                                                 CPluginFSInterfaceAbstract* pluginFS,
                                                 const wchar_t* pluginFSName, int pluginFSNameIndex,
                                                 BOOL isDetachedFS, BOOL& refreshMenu,
                                                 BOOL& closeMenu, int& postCmd, void*& postCmdParam)
{
    CALL_STACK_MESSAGE9("CPluginData::ChangeDriveMenuItemContextMenu(, %d, %d, %d, , %ls, %d, %d, , , ,) (%ls v. %ls)",
                        panel, x, y, pluginFSName, pluginFSNameIndex, isDetachedFS, DLLName.c_str(), Version.c_str());
    if (InitDLL(parent) &&
        (pluginFS != NULL || !ChDrvMenuFSItemName.empty()) // in case the plugin removed the item during this load
        /*&& PluginIfaceForFS.NotEmpty()*/)               // unnecessary, because downgrade is impossible and InitDLL checks the interfaces
    {
        return PluginIfaceForFS.ChangeDriveMenuItemContextMenu(parent, panel, x, y, pluginFS,
                                                               pluginFSName, pluginFSNameIndex,
                                                               isDetachedFS, refreshMenu,
                                                               closeMenu, postCmd, postCmdParam);
    }
    return FALSE; // error, so return "value parameters should be ignored"
}

void CPluginData::EnsureShareExistsOnServer(HWND parent, int panel, const wchar_t* server, const wchar_t* share)
{
    CALL_STACK_MESSAGE6("CPluginData::EnsureShareExistsOnServer(, %d, %ls, %ls) (%ls v. %ls)",
                        panel, server, share, DLLName.c_str(), Version.c_str());
    if (InitDLL(parent, TRUE) &&     // we don't want to report possible load errors; EnsureShareExistsOnServer provides only supplementary info (if it isn't called, almost nothing happens)
        PluginIsNethood &&           // in case the plug-in stops replacing Network (i.e., it does not call SetPluginIsNethood()) right during this load
        PluginIfaceForFS.NotEmpty()) // PluginIsNethood is independent of PluginIfaceForFS, so we check it separately
    {
        PluginIfaceForFS.EnsureShareExistsOnServer(panel, server, share);
    }
}

void CPluginData::GetCacheInfo(std::wstring& arcCacheTmpPath, BOOL* arcCacheOwnDelete, BOOL* arcCacheCacheCopies)
{
    CALL_STACK_MESSAGE3("CPluginData::GetCacheInfo(, ,) (%ls v. %ls)", DLLName.c_str(), Version.c_str());
    if (InitDLL(MainWindow->HWindow) &&
        PluginIfaceForArchiver.NotEmpty()) // this part of the condition is most likely "always true"
    {
        if (ArcCacheHaveInfo) // the settings are already cached; we do not need to bother the plugin anymore
        {
            arcCacheTmpPath = ArcCacheTmpPath;
            *arcCacheOwnDelete = ArcCacheOwnDelete;
            *arcCacheCacheCopies = ArcCacheCacheCopies;
        }
        else
        {
            if (!PluginIfaceForArchiver.GetCacheInfo(arcCacheTmpPath, arcCacheOwnDelete, arcCacheCacheCopies))
            {                            // default values should be used
                ArcCacheHaveInfo = TRUE; // default values are set by InitDLL()()
                arcCacheTmpPath.clear();
                *arcCacheOwnDelete = FALSE;
                *arcCacheCacheCopies = TRUE;
            }
            else
            {
                ArcCacheOwnDelete = *arcCacheOwnDelete; // is set in every case; reason: method IsArchiverAndHaveOwnDelete()()
                ArcCacheHaveInfo = TRUE;
                ArcCacheTmpPath = arcCacheTmpPath;
                ArcCacheCacheCopies = *arcCacheCacheCopies;
            }
        }
    }
}

void CPluginData::DeleteTmpCopy(const wchar_t* fileName, BOOL firstFile)
{
    CALL_STACK_MESSAGE5("CPluginData::DeleteTmpCopy(%ls, %d) (%ls v. %ls)",
                        fileName, firstFile, DLLName.c_str(), Version.c_str());
    if (PluginIfaceForArchiver.NotEmpty())
        PluginIfaceForArchiver.DeleteTmpCopy(fileName, firstFile);
    else
        TRACE_E("Unexpected situation in CPluginData::DeleteTmpCopy(): plugin has not interface for archiver or is not loaded!");
}

BOOL CPluginData::PrematureDeleteTmpCopy(HWND parent, int copiesCount)
{
    CALL_STACK_MESSAGE4("CPluginData::PrematureDeleteTmpCopy(, %d) (%ls v. %ls)",
                        copiesCount, DLLName.c_str(), Version.c_str());
    if (PluginIfaceForArchiver.NotEmpty())
    {
        return PluginIfaceForArchiver.PrematureDeleteTmpCopy(parent, copiesCount);
    }
    else
    {
        TRACE_E("Unexpected situation in CPluginData::PrematureDeleteTmpCopy(): plugin has not interface for archiver or is not loaded!");
        return FALSE;
    }
}

HIMAGELIST
CPluginData::CreateImageList(BOOL gray)
{
    CIconList* srcList = NULL;
    BOOL deleteSrcList = FALSE;
    if (SupportDynMenuExt)
    {
        if (gray && PluginDynMenuIcons != NULL)
        {
            deleteSrcList = TRUE;
            srcList = new CIconList();
            if (srcList != NULL && !srcList->CreateAsCopy(PluginDynMenuIcons, TRUE))
            {
                delete srcList;
                srcList = NULL;
            }
        }
        else
            srcList = PluginDynMenuIcons;
    }
    else
        srcList = gray ? PluginIconsGray : PluginIcons;
    if (srcList == NULL)
        return NULL; // the plugin has no bitmap assigned

    HIMAGELIST ret = srcList->GetImageList();
    if (deleteSrcList)
        delete srcList;
    return ret;
}

void CPluginData::HotKeysMerge(TIndirectArray<CPluginMenuItem>* oldMenuItems)
{
    CALL_STACK_MESSAGE1("CPluginData::HotKeysMergeAndTestIntegrity()");
    if (!oldMenuItems->IsGood())
        return;

    // if the old menu had some "dirty" hot keys, transfer them to the new one by ID
    int i;
    for (i = 0; i < oldMenuItems->Count; i++)
    {
        CPluginMenuItem* oldItem = oldMenuItems->At(i);
        if (oldItem->HotKey & HOTKEY_DIRTY)
        {
            // found a dirty item, try to locate it in the new IDs
            int j;
            for (j = 0; j < MenuItems.Count; j++)
            {
                CPluginMenuItem* item = MenuItems[j];
                if (item->ID == oldItem->ID)
                {
                    // transfer the hot key
                    item->HotKey = oldItem->HotKey;
                    break;
                }
            }
        }
    }
}

void CPluginData::HotKeysEnsureIntegrity()
{
    CALL_STACK_MESSAGE1("CPluginData::HotKeysEnsureIntegrity()");

    int i;
    for (i = 0; i < MenuItems.Count; i++)
    {
        CPluginMenuItem* item = MenuItems[i];
        WORD hotKey = HOTKEY_GET(item->HotKey);
        if (hotKey == 0)
            continue;
        BOOL dirty = HOTKEY_GETDIRTY(item->HotKey);
        if (IsSalHotKey(hotKey))
        {
            // the hot key must not belong to Salamander
            item->HotKey = 0;
            TRACE_EW(L"CPluginData::HotKeysEnsureIntegrity() hot key is already assigned to Salamander; item:" << item->Name);
        }
        else
        {
            // the hot key must not belong to another plugin
            int pluginIndex;
            int menuItemIndex;
            if (Plugins.FindHotKey(hotKey, TRUE, this, &pluginIndex, &menuItemIndex))
            {
                if (dirty) // if we have a predefined hot key, remove it from the competitor instead
                    Plugins.Get(pluginIndex)->MenuItems[menuItemIndex]->HotKey = 0;
                else
                    item->HotKey = 0;
            }
        }
        // within one menu the hot key must not be repeated
        if (item->HotKey != 0)
        {
            int j;
            for (j = 0; j < MenuItems.Count; j++)
            {
                if (j == i)
                    continue;
                CPluginMenuItem* item2 = MenuItems[j];
                if (HOTKEY_GET(item2->HotKey) == hotKey)
                {
                    if (dirty)
                        item2->HotKey = 0;
                    else
                        item->HotKey = 0;
                }
            }
        }
    }
}

void CPluginData::ReleasePluginDynMenuIcons()
{
    if (PluginDynMenuIcons != NULL)
    {
        delete PluginDynMenuIcons;
        PluginDynMenuIcons = NULL;
    }
}

void CPluginData::ReleaseIconOverlays()
{
    if (IconOverlaysCount > 0)
    {
        if (IconOverlays != NULL)
            for (int i = 0; i < IconOverlaysCount; i++)
            {
                if (IconOverlays[i * 3 + 0] != NULL)
                    HANDLES(DestroyIcon(IconOverlays[i * 3 + 0])); // 16x16
                if (IconOverlays[i * 3 + 1] != NULL)
                    HANDLES(DestroyIcon(IconOverlays[i * 3 + 1])); // 32x32
                if (IconOverlays[i * 3 + 2] != NULL)
                    HANDLES(DestroyIcon(IconOverlays[i * 3 + 2])); // 48x48
            }
        else
            TRACE_E("CPluginData::ReleaseIconOverlays(): unexpected situation: IconOverlaysCount is greater then 0 and IconOverlays is NULL.");
        IconOverlaysCount = 0;
    }
    if (IconOverlays != NULL)
    {
        free(IconOverlays);
        IconOverlays = NULL;
    }
}
