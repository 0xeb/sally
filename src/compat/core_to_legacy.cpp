// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// core_to_legacy — live wide wrappers over frozen v107 plugin interfaces.

// Never include precomp.h here. Standard-library headers must be visible at
// global scope before sdk107.h enters its frozen namespace wrapper.

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "compat/core_to_legacy.h"

#include "compat/legacy_convert.h"

namespace sdk107
{

    CPluginDataInterfaceEncapsulation::CPluginDataInterfaceEncapsulation(
        CPluginDataInterfaceAbstract& legacy)
        : Legacy(legacy)
    {
    }

    BOOL CPluginDataInterfaceEncapsulation::CallReleaseForFiles()
    {
        return Legacy.CallReleaseForFiles();
    }

    BOOL CPluginDataInterfaceEncapsulation::CallReleaseForDirs()
    {
        return Legacy.CallReleaseForDirs();
    }

    void CPluginDataInterfaceEncapsulation::ReleasePluginData(
        CFileData& file, BOOL isDir)
    {
        Legacy.ReleasePluginData(file, isDir);
    }

    void CPluginDataInterfaceEncapsulation::GetFileDataForUpDir(
        const char* archivePath, CFileData& upDir)
    {
        Legacy.GetFileDataForUpDir(archivePath, upDir);
    }

    BOOL CPluginDataInterfaceEncapsulation::GetFileDataForNewDir(
        const char* dirName, CFileData& dir)
    {
        return Legacy.GetFileDataForNewDir(dirName, dir);
    }

    HIMAGELIST CPluginDataInterfaceEncapsulation::GetSimplePluginIcons(
        int iconSize)
    {
        return Legacy.GetSimplePluginIcons(iconSize);
    }

    BOOL CPluginDataInterfaceEncapsulation::HasSimplePluginIcon(
        CFileData& file, BOOL isDir)
    {
        return Legacy.HasSimplePluginIcon(file, isDir);
    }

    HICON CPluginDataInterfaceEncapsulation::GetPluginIcon(
        const CFileData* file, int iconSize, BOOL& destroyIcon)
    {
        return Legacy.GetPluginIcon(file, iconSize, destroyIcon);
    }

    int CPluginDataInterfaceEncapsulation::CompareFilesFromFS(
        const CFileData* file1, const CFileData* file2)
    {
        return Legacy.CompareFilesFromFS(file1, file2);
    }

    void CPluginDataInterfaceEncapsulation::SetupView(
        BOOL leftPanel, CSalamanderViewAbstract* view,
        const char* archivePath, const CFileData* upperDir)
    {
        Legacy.SetupView(leftPanel, view, archivePath, upperDir);
    }

    void CPluginDataInterfaceEncapsulation::ColumnFixedWidthShouldChange(
        BOOL leftPanel, const CColumn* column, int newFixedWidth)
    {
        Legacy.ColumnFixedWidthShouldChange(leftPanel, column, newFixedWidth);
    }

    void CPluginDataInterfaceEncapsulation::ColumnWidthWasChanged(
        BOOL leftPanel, const CColumn* column, int newWidth)
    {
        Legacy.ColumnWidthWasChanged(leftPanel, column, newWidth);
    }

    BOOL CPluginDataInterfaceEncapsulation::GetInfoLineContent(
        int panel, const CFileData* file, BOOL isDir, int selectedFiles,
        int selectedDirs, BOOL displaySize, const CQuadWord& selectedSize,
        char* buffer, DWORD* hotTexts, int& hotTextsCount)
    {
        return Legacy.GetInfoLineContent(
            panel, file, isDir, selectedFiles, selectedDirs, displaySize,
            selectedSize, buffer, hotTexts, hotTextsCount);
    }

    BOOL CPluginDataInterfaceEncapsulation::CanBeCopiedToClipboard()
    {
        return Legacy.CanBeCopiedToClipboard();
    }

    BOOL CPluginDataInterfaceEncapsulation::GetByteSize(
        const CFileData* file, BOOL isDir, CQuadWord* size)
    {
        return Legacy.GetByteSize(file, isDir, size);
    }

    BOOL CPluginDataInterfaceEncapsulation::GetLastWriteDate(
        const CFileData* file, BOOL isDir, SYSTEMTIME* date)
    {
        return Legacy.GetLastWriteDate(file, isDir, date);
    }

    BOOL CPluginDataInterfaceEncapsulation::GetLastWriteTime(
        const CFileData* file, BOOL isDir, SYSTEMTIME* time)
    {
        return Legacy.GetLastWriteTime(file, isDir, time);
    }

    CPluginInterfaceEncapsulation::CPluginInterfaceEncapsulation(
        CPluginInterfaceAbstract& legacy)
        : Legacy(legacy)
    {
    }

    void CPluginInterfaceEncapsulation::About(HWND parent)
    {
        Legacy.About(parent);
    }

    BOOL CPluginInterfaceEncapsulation::Release(HWND parent, BOOL force)
    {
        return Legacy.Release(parent, force);
    }

    void CPluginInterfaceEncapsulation::LoadConfiguration(
        HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
    {
        Legacy.LoadConfiguration(parent, regKey, registry);
    }

    void CPluginInterfaceEncapsulation::SaveConfiguration(
        HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
    {
        Legacy.SaveConfiguration(parent, regKey, registry);
    }

    void CPluginInterfaceEncapsulation::Configuration(HWND parent)
    {
        Legacy.Configuration(parent);
    }

    void CPluginInterfaceEncapsulation::Connect(
        HWND parent, CSalamanderConnectAbstract* salamander)
    {
        Legacy.Connect(parent, salamander);
    }

    void CPluginInterfaceEncapsulation::ReleasePluginDataInterface(
        CPluginDataInterfaceAbstract* pluginData)
    {
        Legacy.ReleasePluginDataInterface(pluginData);
    }

    CPluginInterfaceForArchiverAbstract*
    CPluginInterfaceEncapsulation::GetInterfaceForArchiver()
    {
        return Legacy.GetInterfaceForArchiver();
    }

    CPluginInterfaceForViewerAbstract*
    CPluginInterfaceEncapsulation::GetInterfaceForViewer()
    {
        return Legacy.GetInterfaceForViewer();
    }

    CPluginInterfaceForMenuExtAbstract*
    CPluginInterfaceEncapsulation::GetInterfaceForMenuExt()
    {
        return Legacy.GetInterfaceForMenuExt();
    }

    CPluginInterfaceForFSAbstract*
    CPluginInterfaceEncapsulation::GetInterfaceForFS()
    {
        return Legacy.GetInterfaceForFS();
    }

    CPluginInterfaceForThumbLoaderAbstract*
    CPluginInterfaceEncapsulation::GetInterfaceForThumbLoader()
    {
        return Legacy.GetInterfaceForThumbLoader();
    }

    void CPluginInterfaceEncapsulation::Event(int event, DWORD param)
    {
        Legacy.Event(event, param);
    }

    void CPluginInterfaceEncapsulation::ClearHistory(HWND parent)
    {
        Legacy.ClearHistory(parent);
    }

    void CPluginInterfaceEncapsulation::AcceptChangeOnPathNotification(
        const char* path, BOOL includingSubdirs)
    {
        Legacy.AcceptChangeOnPathNotification(path, includingSubdirs);
    }

    void CPluginInterfaceEncapsulation::PasswordManagerEvent(
        HWND parent, int event)
    {
        Legacy.PasswordManagerEvent(parent, event);
    }

    CPluginInterfaceForArchiverEncapsulation::
        CPluginInterfaceForArchiverEncapsulation(
            CPluginInterfaceForArchiverAbstract& legacy)
        : Legacy(legacy)
    {
    }

    BOOL CPluginInterfaceForArchiverEncapsulation::ListArchive(
        CSalamanderForOperationsAbstract* salamander, const char* fileName,
        CSalamanderDirectoryAbstract* directory,
        CPluginDataInterfaceAbstract*& pluginData)
    {
        return Legacy.ListArchive(salamander, fileName, directory, pluginData);
    }

    BOOL CPluginInterfaceForArchiverEncapsulation::UnpackArchive(
        CSalamanderForOperationsAbstract* salamander, const char* fileName,
        CPluginDataInterfaceAbstract* pluginData, const char* targetDir,
        const char* archiveRoot, SalEnumSelection next, void* nextParam)
    {
        return Legacy.UnpackArchive(salamander, fileName, pluginData, targetDir,
                                    archiveRoot, next, nextParam);
    }

    BOOL CPluginInterfaceForArchiverEncapsulation::UnpackOneFile(
        CSalamanderForOperationsAbstract* salamander, const char* fileName,
        CPluginDataInterfaceAbstract* pluginData, const char* nameInArchive,
        const CFileData* fileData, const char* targetDir,
        const char* newFileName, BOOL* renamingNotSupported)
    {
        return Legacy.UnpackOneFile(salamander, fileName, pluginData,
                                    nameInArchive, fileData, targetDir,
                                    newFileName, renamingNotSupported);
    }

    BOOL CPluginInterfaceForArchiverEncapsulation::PackToArchive(
        CSalamanderForOperationsAbstract* salamander, const char* fileName,
        const char* archiveRoot, BOOL move, const char* sourcePath,
        SalEnumSelection2 next, void* nextParam)
    {
        return Legacy.PackToArchive(salamander, fileName, archiveRoot, move,
                                    sourcePath, next, nextParam);
    }

    BOOL CPluginInterfaceForArchiverEncapsulation::DeleteFromArchive(
        CSalamanderForOperationsAbstract* salamander, const char* fileName,
        CPluginDataInterfaceAbstract* pluginData, const char* archiveRoot,
        SalEnumSelection next, void* nextParam)
    {
        return Legacy.DeleteFromArchive(salamander, fileName, pluginData,
                                        archiveRoot, next, nextParam);
    }

    BOOL CPluginInterfaceForArchiverEncapsulation::UnpackWholeArchive(
        CSalamanderForOperationsAbstract* salamander, const char* fileName,
        const char* mask, const char* targetDir, BOOL delArchiveWhenDone,
        CDynamicString* archiveVolumes)
    {
        return Legacy.UnpackWholeArchive(salamander, fileName, mask, targetDir,
                                         delArchiveWhenDone, archiveVolumes);
    }

    BOOL CPluginInterfaceForArchiverEncapsulation::CanCloseArchive(
        CSalamanderForOperationsAbstract* salamander, const char* fileName,
        BOOL force, int panel)
    {
        return Legacy.CanCloseArchive(salamander, fileName, force, panel);
    }

    BOOL CPluginInterfaceForArchiverEncapsulation::GetCacheInfo(
        char* tempPath, BOOL* ownDelete, BOOL* cacheCopies)
    {
        return Legacy.GetCacheInfo(tempPath, ownDelete, cacheCopies);
    }

    void CPluginInterfaceForArchiverEncapsulation::DeleteTmpCopy(
        const char* fileName, BOOL firstFile)
    {
        Legacy.DeleteTmpCopy(fileName, firstFile);
    }

    BOOL CPluginInterfaceForArchiverEncapsulation::PrematureDeleteTmpCopy(
        HWND parent, int copiesCount)
    {
        return Legacy.PrematureDeleteTmpCopy(parent, copiesCount);
    }

    CPluginInterfaceForViewerEncapsulation::
        CPluginInterfaceForViewerEncapsulation(
            CPluginInterfaceForViewerAbstract& legacy)
        : Legacy(legacy)
    {
    }

    BOOL CPluginInterfaceForViewerEncapsulation::ViewFile(
        const char* name, int left, int top, int width, int height,
        UINT showCmd, BOOL alwaysOnTop, BOOL returnLock, HANDLE* lock,
        BOOL* lockOwner, CSalamanderPluginViewerData* viewerData,
        int enumFilesSourceUID, int enumFilesCurrentIndex)
    {
        return Legacy.ViewFile(name, left, top, width, height, showCmd,
                               alwaysOnTop, returnLock, lock, lockOwner,
                               viewerData, enumFilesSourceUID,
                               enumFilesCurrentIndex);
    }

    BOOL CPluginInterfaceForViewerEncapsulation::CanViewFile(
        const char* name)
    {
        return Legacy.CanViewFile(name);
    }

    CPluginInterfaceForMenuExtEncapsulation::
        CPluginInterfaceForMenuExtEncapsulation(
            CPluginInterfaceForMenuExtAbstract& legacy)
        : Legacy(legacy)
    {
    }

    DWORD CPluginInterfaceForMenuExtEncapsulation::GetMenuItemState(
        int id, DWORD eventMask)
    {
        return Legacy.GetMenuItemState(id, eventMask);
    }

    BOOL CPluginInterfaceForMenuExtEncapsulation::ExecuteMenuItem(
        CSalamanderForOperationsAbstract* salamander, HWND parent, int id,
        DWORD eventMask)
    {
        return Legacy.ExecuteMenuItem(salamander, parent, id, eventMask);
    }

    BOOL CPluginInterfaceForMenuExtEncapsulation::HelpForMenuItem(
        HWND parent, int id)
    {
        return Legacy.HelpForMenuItem(parent, id);
    }

    void CPluginInterfaceForMenuExtEncapsulation::BuildMenu(
        HWND parent, CSalamanderBuildMenuAbstract* salamander)
    {
        Legacy.BuildMenu(parent, salamander);
    }

    CPluginInterfaceForThumbLoaderEncapsulation::
        CPluginInterfaceForThumbLoaderEncapsulation(
            CPluginInterfaceForThumbLoaderAbstract& legacy)
        : Legacy(legacy)
    {
    }

    BOOL CPluginInterfaceForThumbLoaderEncapsulation::LoadThumbnail(
        const char* filename, int thumbWidth, int thumbHeight,
        CSalamanderThumbnailMakerAbstract* thumbMaker, BOOL fastThumbnail)
    {
        return Legacy.LoadThumbnail(filename, thumbWidth, thumbHeight,
                                    thumbMaker, fastThumbnail);
    }

    CPluginInterfaceForFSEncapsulation::CPluginInterfaceForFSEncapsulation(
        CPluginInterfaceForFSAbstract& legacy)
        : Legacy(legacy)
    {
    }

    CPluginFSInterfaceAbstract* CPluginInterfaceForFSEncapsulation::OpenFS(
        const char* fsName, int fsNameIndex)
    {
        return Legacy.OpenFS(fsName, fsNameIndex);
    }

    void CPluginInterfaceForFSEncapsulation::CloseFS(
        CPluginFSInterfaceAbstract* fs)
    {
        Legacy.CloseFS(fs);
    }

    void CPluginInterfaceForFSEncapsulation::ExecuteChangeDriveMenuItem(
        int panel)
    {
        Legacy.ExecuteChangeDriveMenuItem(panel);
    }

    BOOL CPluginInterfaceForFSEncapsulation::ChangeDriveMenuItemContextMenu(
        HWND parent, int panel, int x, int y,
        CPluginFSInterfaceAbstract* pluginFS, const char* pluginFSName,
        int pluginFSNameIndex, BOOL isDetachedFS, BOOL& refreshMenu,
        BOOL& closeMenu, int& postCmd, void*& postCmdParam)
    {
        return Legacy.ChangeDriveMenuItemContextMenu(
            parent, panel, x, y, pluginFS, pluginFSName, pluginFSNameIndex,
            isDetachedFS, refreshMenu, closeMenu, postCmd, postCmdParam);
    }

    void CPluginInterfaceForFSEncapsulation::ExecuteChangeDrivePostCommand(
        int panel, int postCmd, void* postCmdParam)
    {
        Legacy.ExecuteChangeDrivePostCommand(panel, postCmd, postCmdParam);
    }

    void CPluginInterfaceForFSEncapsulation::ExecuteOnFS(
        int panel, CPluginFSInterfaceAbstract* pluginFS,
        const char* pluginFSName, int pluginFSNameIndex, CFileData& file,
        int isDir)
    {
        Legacy.ExecuteOnFS(panel, pluginFS, pluginFSName, pluginFSNameIndex,
                           file, isDir);
    }

    BOOL CPluginInterfaceForFSEncapsulation::DisconnectFS(
        HWND parent, BOOL isInPanel, int panel,
        CPluginFSInterfaceAbstract* pluginFS, const char* pluginFSName,
        int pluginFSNameIndex)
    {
        return Legacy.DisconnectFS(parent, isInPanel, panel, pluginFS,
                                   pluginFSName, pluginFSNameIndex);
    }

    void CPluginInterfaceForFSEncapsulation::ConvertPathToInternal(
        const char* fsName, int fsNameIndex, char* fsUserPart)
    {
        Legacy.ConvertPathToInternal(fsName, fsNameIndex, fsUserPart);
    }

    void CPluginInterfaceForFSEncapsulation::ConvertPathToExternal(
        const char* fsName, int fsNameIndex, char* fsUserPart)
    {
        Legacy.ConvertPathToExternal(fsName, fsNameIndex, fsUserPart);
    }

    void CPluginInterfaceForFSEncapsulation::EnsureShareExistsOnServer(
        int panel, const char* server, const char* share)
    {
        Legacy.EnsureShareExistsOnServer(panel, server, share);
    }

    CPluginFSInterfaceEncapsulation::CPluginFSInterfaceEncapsulation(
        CPluginFSInterfaceAbstract& legacy)
        : Legacy(legacy)
    {
    }

    BOOL CPluginFSInterfaceEncapsulation::GetCurrentPath(char* userPart)
    {
        return Legacy.GetCurrentPath(userPart);
    }

    BOOL CPluginFSInterfaceEncapsulation::GetFullName(
        CFileData& file, int isDir, char* buf, int bufSize)
    {
        return Legacy.GetFullName(file, isDir, buf, bufSize);
    }

    BOOL CPluginFSInterfaceEncapsulation::GetFullFSPath(
        HWND parent, const char* fsName, char* path, int pathSize,
        BOOL& success)
    {
        return Legacy.GetFullFSPath(parent, fsName, path, pathSize, success);
    }

    BOOL CPluginFSInterfaceEncapsulation::GetRootPath(char* userPart)
    {
        return Legacy.GetRootPath(userPart);
    }

    BOOL CPluginFSInterfaceEncapsulation::IsCurrentPath(
        int currentFSNameIndex, int fsNameIndex, const char* userPart)
    {
        return Legacy.IsCurrentPath(currentFSNameIndex, fsNameIndex, userPart);
    }

    BOOL CPluginFSInterfaceEncapsulation::IsOurPath(
        int currentFSNameIndex, int fsNameIndex, const char* userPart)
    {
        return Legacy.IsOurPath(currentFSNameIndex, fsNameIndex, userPart);
    }

    BOOL CPluginFSInterfaceEncapsulation::ChangePath(
        int currentFSNameIndex, char* fsName, int fsNameIndex,
        const char* userPart, char* cutFileName, BOOL* pathWasCut,
        BOOL forceRefresh, int mode)
    {
        return Legacy.ChangePath(currentFSNameIndex, fsName, fsNameIndex,
                                 userPart, cutFileName, pathWasCut,
                                 forceRefresh, mode);
    }

    BOOL CPluginFSInterfaceEncapsulation::ListCurrentPath(
        CSalamanderDirectoryAbstract* dir,
        CPluginDataInterfaceAbstract*& pluginData, int& iconsType,
        BOOL forceRefresh)
    {
        return Legacy.ListCurrentPath(dir, pluginData, iconsType,
                                      forceRefresh);
    }

    BOOL CPluginFSInterfaceEncapsulation::TryCloseOrDetach(
        BOOL forceClose, BOOL canDetach, BOOL& detach, int reason)
    {
        return Legacy.TryCloseOrDetach(forceClose, canDetach, detach, reason);
    }

    void CPluginFSInterfaceEncapsulation::Event(int event, DWORD param)
    {
        Legacy.Event(event, param);
    }

    void CPluginFSInterfaceEncapsulation::ReleaseObject(HWND parent)
    {
        Legacy.ReleaseObject(parent);
    }

    DWORD CPluginFSInterfaceEncapsulation::GetSupportedServices()
    {
        return Legacy.GetSupportedServices();
    }

    BOOL CPluginFSInterfaceEncapsulation::GetChangeDriveOrDisconnectItem(
        const char* fsName, char*& title, HICON& icon, BOOL& destroyIcon)
    {
        return Legacy.GetChangeDriveOrDisconnectItem(fsName, title, icon,
                                                     destroyIcon);
    }

    HICON CPluginFSInterfaceEncapsulation::GetFSIcon(BOOL& destroyIcon)
    {
        return Legacy.GetFSIcon(destroyIcon);
    }

    void CPluginFSInterfaceEncapsulation::GetDropEffect(
        const char* srcFSPath, const char* tgtFSPath, DWORD allowedEffects,
        DWORD keyState, DWORD* dropEffect)
    {
        Legacy.GetDropEffect(srcFSPath, tgtFSPath, allowedEffects, keyState,
                             dropEffect);
    }

    void CPluginFSInterfaceEncapsulation::GetFSFreeSpace(CQuadWord* retValue)
    {
        Legacy.GetFSFreeSpace(retValue);
    }

    BOOL CPluginFSInterfaceEncapsulation::GetNextDirectoryLineHotPath(
        const char* text, int pathLen, int& offset)
    {
        return Legacy.GetNextDirectoryLineHotPath(text, pathLen, offset);
    }

    void CPluginFSInterfaceEncapsulation::CompleteDirectoryLineHotPath(
        char* path, int pathBufSize)
    {
        Legacy.CompleteDirectoryLineHotPath(path, pathBufSize);
    }

    BOOL CPluginFSInterfaceEncapsulation::GetPathForMainWindowTitle(
        const char* fsName, int mode, char* buf, int bufSize)
    {
        return Legacy.GetPathForMainWindowTitle(fsName, mode, buf, bufSize);
    }

    void CPluginFSInterfaceEncapsulation::ShowInfoDialog(
        const char* fsName, HWND parent)
    {
        Legacy.ShowInfoDialog(fsName, parent);
    }

    BOOL CPluginFSInterfaceEncapsulation::ExecuteCommandLine(
        HWND parent, char* command, int& selFrom, int& selTo)
    {
        return Legacy.ExecuteCommandLine(parent, command, selFrom, selTo);
    }

    BOOL CPluginFSInterfaceEncapsulation::QuickRename(
        const char* fsName, int mode, HWND parent, CFileData& file, BOOL isDir,
        char* newName, BOOL& cancel)
    {
        return Legacy.QuickRename(fsName, mode, parent, file, isDir, newName,
                                  cancel);
    }

    void CPluginFSInterfaceEncapsulation::AcceptChangeOnPathNotification(
        const char* fsName, const char* path, BOOL includingSubdirs)
    {
        Legacy.AcceptChangeOnPathNotification(fsName, path, includingSubdirs);
    }

    BOOL CPluginFSInterfaceEncapsulation::CreateDir(
        const char* fsName, int mode, HWND parent, char* newName,
        BOOL& cancel)
    {
        return Legacy.CreateDir(fsName, mode, parent, newName, cancel);
    }

    void CPluginFSInterfaceEncapsulation::ViewFile(
        const char* fsName, HWND parent,
        CSalamanderForViewFileOnFSAbstract* salamander, CFileData& file)
    {
        Legacy.ViewFile(fsName, parent, salamander, file);
    }

    BOOL CPluginFSInterfaceEncapsulation::Delete(
        const char* fsName, int mode, HWND parent, int panel,
        int selectedFiles, int selectedDirs, BOOL& cancelOrError)
    {
        return Legacy.Delete(fsName, mode, parent, panel, selectedFiles,
                             selectedDirs, cancelOrError);
    }

    BOOL CPluginFSInterfaceEncapsulation::CopyOrMoveFromFS(
        BOOL copy, int mode, const char* fsName, HWND parent, int panel,
        int selectedFiles, int selectedDirs, char* targetPath,
        BOOL& operationMask, BOOL& cancelOrHandlePath, HWND dropTarget)
    {
        return Legacy.CopyOrMoveFromFS(copy, mode, fsName, parent, panel,
                                       selectedFiles, selectedDirs, targetPath,
                                       operationMask, cancelOrHandlePath,
                                       dropTarget);
    }

    BOOL CPluginFSInterfaceEncapsulation::CopyOrMoveFromDiskToFS(
        BOOL copy, int mode, const char* fsName, HWND parent,
        const char* sourcePath, SalEnumSelection2 next, void* nextParam,
        int sourceFiles, int sourceDirs, char* targetPath,
        BOOL* invalidPathOrCancel)
    {
        return Legacy.CopyOrMoveFromDiskToFS(
            copy, mode, fsName, parent, sourcePath, next, nextParam,
            sourceFiles, sourceDirs, targetPath, invalidPathOrCancel);
    }

    BOOL CPluginFSInterfaceEncapsulation::ChangeAttributes(
        const char* fsName, HWND parent, int panel, int selectedFiles,
        int selectedDirs)
    {
        return Legacy.ChangeAttributes(fsName, parent, panel, selectedFiles,
                                       selectedDirs);
    }

    void CPluginFSInterfaceEncapsulation::ShowProperties(
        const char* fsName, HWND parent, int panel, int selectedFiles,
        int selectedDirs)
    {
        Legacy.ShowProperties(fsName, parent, panel, selectedFiles,
                              selectedDirs);
    }

    void CPluginFSInterfaceEncapsulation::ContextMenu(
        const char* fsName, HWND parent, int menuX, int menuY, int type,
        int panel, int selectedFiles, int selectedDirs)
    {
        Legacy.ContextMenu(fsName, parent, menuX, menuY, type, panel,
                           selectedFiles, selectedDirs);
    }

    BOOL CPluginFSInterfaceEncapsulation::HandleMenuMsg(
        UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult)
    {
        return Legacy.HandleMenuMsg(uMsg, wParam, lParam, plResult);
    }

    BOOL CPluginFSInterfaceEncapsulation::OpenFindDialog(
        const char* fsName, int panel)
    {
        return Legacy.OpenFindDialog(fsName, panel);
    }

    void CPluginFSInterfaceEncapsulation::OpenActiveFolder(
        const char* fsName, HWND parent)
    {
        Legacy.OpenActiveFolder(fsName, parent);
    }

    void CPluginFSInterfaceEncapsulation::GetAllowedDropEffects(
        int mode, const char* tgtFSPath, DWORD* allowedEffects)
    {
        Legacy.GetAllowedDropEffects(mode, tgtFSPath, allowedEffects);
    }

    BOOL CPluginFSInterfaceEncapsulation::GetNoItemsInPanelText(
        char* textBuf, int textBufSize)
    {
        return Legacy.GetNoItemsInPanelText(textBuf, textBufSize);
    }

    void CPluginFSInterfaceEncapsulation::ShowSecurityInfo(HWND parent)
    {
        Legacy.ShowSecurityInfo(parent);
    }

    BOOL CPluginFSInterfaceEncapsulation::GetCurrentPathW(
        wchar_t* userPart, int userPartSize)
    {
        return Legacy.GetCurrentPathW(userPart, userPartSize);
    }

    BOOL CPluginFSInterfaceEncapsulation::GetFullNameW(
        CFileData& file, int isDir, wchar_t* buf, int bufSize)
    {
        return Legacy.GetFullNameW(file, isDir, buf, bufSize);
    }

    BOOL CPluginFSInterfaceEncapsulation::GetFullFSPathW(
        HWND parent, const wchar_t* fsName, wchar_t* path, int pathSize,
        BOOL& success)
    {
        return Legacy.GetFullFSPathW(parent, fsName, path, pathSize, success);
    }

    BOOL CPluginFSInterfaceEncapsulation::GetRootPathW(
        wchar_t* userPart, int userPartSize)
    {
        return Legacy.GetRootPathW(userPart, userPartSize);
    }

    BOOL CPluginFSInterfaceEncapsulation::IsCurrentPathW(
        int currentFSNameIndex, int fsNameIndex, const wchar_t* userPart)
    {
        return Legacy.IsCurrentPathW(currentFSNameIndex, fsNameIndex,
                                     userPart);
    }

    BOOL CPluginFSInterfaceEncapsulation::IsOurPathW(
        int currentFSNameIndex, int fsNameIndex, const wchar_t* userPart)
    {
        return Legacy.IsOurPathW(currentFSNameIndex, fsNameIndex, userPart);
    }

    BOOL CPluginFSInterfaceEncapsulation::ChangePathW(
        int currentFSNameIndex, wchar_t* fsName, int fsNameIndex,
        const wchar_t* userPart, wchar_t* cutFileName, int cutFileNameSize,
        BOOL* pathWasCut, BOOL forceRefresh, int mode)
    {
        return Legacy.ChangePathW(currentFSNameIndex, fsName, fsNameIndex,
                                  userPart, cutFileName, cutFileNameSize,
                                  pathWasCut, forceRefresh, mode);
    }

} // namespace sdk107

namespace sally::compat
{
    namespace
    {

        template <typename T, typename... Args>
        bool EmplaceBoundaryObject(std::optional<T>& object,
                                   Args&&... args) noexcept
        {
            try
            {
                object.emplace(std::forward<Args>(args)...);
                return true;
            }
            catch (const std::bad_alloc&)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return false;
            }
            catch (const std::length_error&)
            {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return false;
            }
            catch (...)
            {
                SetLastError(ERROR_INVALID_DATA);
                return false;
            }
        }

        bool AssignWideSpan(std::wstring& destination, const wchar_t* source,
                            std::size_t length) noexcept
        {
            try
            {
                std::wstring staged(source, length);
                destination.swap(staged);
                return true;
            }
            catch (const std::bad_alloc&)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return false;
            }
            catch (const std::length_error&)
            {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return false;
            }
        }

        template <typename Call>
        bool QueryFrozenWideOutput(std::wstring& output,
                                   Call&& call) noexcept
        {
            try
            {
                std::vector<wchar_t> buffer(MAX_PATH, L'\0');
                for (;;)
                {
                    SetLastError(ERROR_SUCCESS);
                    if (call(buffer.data(), static_cast<int>(buffer.size())))
                    {
                        const std::size_t length =
                            ::wcsnlen(buffer.data(), buffer.size());
                        if (length == buffer.size())
                        {
                            SetLastError(ERROR_INSUFFICIENT_BUFFER);
                            return false;
                        }
                        return AssignWideSpan(output, buffer.data(), length);
                    }
                    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
                        buffer.size() > static_cast<std::size_t>(INT_MAX) / 2)
                        return false;
                    buffer.assign(buffer.size() * 2, L'\0');
                }
            }
            catch (const std::bad_alloc&)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return false;
            }
            catch (const std::length_error&)
            {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return false;
            }
        }

        sdk107::CQuadWord ToLegacy(const ::CQuadWord& value)
        {
            sdk107::CQuadWord result;
            result.LoDWord = value.LoDWord;
            result.HiDWord = value.HiDWord;
            return result;
        }

        void ToLive(const sdk107::CQuadWord& value, ::CQuadWord& result)
        {
            result.LoDWord = value.LoDWord;
            result.HiDWord = value.HiDWord;
        }

        void SynchronizeRowState(const sdk107::CFileData& source,
                                 ::CFileData& destination)
        {
            ToLive(source.Size, destination.Size);
            destination.Attr = source.Attr;
            destination.LastWrite = source.LastWrite;
            destination.PluginData = source.PluginData;
            destination.Hidden = source.Hidden;
            destination.IsLink = source.IsLink;
            destination.IsOffline = source.IsOffline;
            destination.IconOverlayIndex = source.IconOverlayIndex;
            destination.Association = source.Association;
            destination.Selected = source.Selected;
            destination.Shared = source.Shared;
            destination.Archive = source.Archive;
            destination.SizeValid = source.SizeValid;
            destination.Dirty = source.Dirty;
            destination.CutToClip = source.CutToClip;
            destination.IconOverlayDone = source.IconOverlayDone;
        }

        class CLegacyRowMirror
        {
        public:
            explicit CLegacyRowMirror(const ::CFileData& source)
                : Valid(FileDataToLegacy(source, Row))
            {
            }

            ~CLegacyRowMirror()
            {
                FreeLegacyFileData(Row);
            }

            void Commit(::CFileData& destination) const
            {
                SynchronizeRowState(Row, destination);
            }

            sdk107::CFileData Row = {};
            bool Valid = false;
        };

        bool NarrowOptionalText(const wchar_t* value, std::string& storage,
                                const char*& result)
        {
            if (value == nullptr)
            {
                result = nullptr;
                return true;
            }
            if (*value == L'\0')
            {
                storage.clear();
                result = storage.c_str();
                return true;
            }
            NarrowResult narrowed = NarrowExact(value);
            if (!narrowed.ok)
            {
                SetLastError(Win32ErrorForNarrowRefusal(narrowed.refusal));
                return false;
            }
            storage.swap(narrowed.value);
            result = storage.c_str();
            return true;
        }

        bool WidenOptionalText(const char* value, std::wstring& storage,
                               const wchar_t*& result)
        {
            if (value == nullptr)
            {
                storage.clear();
                result = nullptr;
                return true;
            }
            if (!WidenPluginText(value, storage))
            {
                result = nullptr;
                return false;
            }
            result = storage.c_str();
            return true;
        }

        template <std::size_t Size>
        bool NarrowBuffer(const wchar_t* source,
                          std::array<char, Size>& destination)
        {
            if (source == nullptr)
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return false;
            }
            destination.fill('\0');
            if (*source == L'\0')
                return true;
            const NarrowResult narrowed = NarrowExact(source);
            if (!narrowed.ok)
            {
                SetLastError(Win32ErrorForNarrowRefusal(narrowed.refusal));
                return false;
            }
            if (narrowed.value.size() >= Size)
            {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return false;
            }
            std::memcpy(destination.data(), narrowed.value.c_str(),
                        narrowed.value.size() + 1);
            return true;
        }

        template <std::size_t Size>
        bool WidenBuffer(const std::array<char, Size>& source,
                         std::wstring& destination)
        {
            if (std::memchr(source.data(), '\0', source.size()) == nullptr)
            {
                destination.clear();
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return false;
            }
            return WidenPluginText(source.data(), destination);
        }

        template <std::size_t Size>
        bool NarrowMultiString(const std::wstring& source,
                               std::array<char, Size>& destination)
        {
            destination.fill('\0');
            std::size_t sourceAt = 0;
            std::size_t destinationAt = 0;
            while (sourceAt <= source.size())
            {
                const std::size_t segmentEnd = source.find(L'\0', sourceAt);
                const std::size_t segmentLength =
                    (segmentEnd == std::wstring::npos ? source.size() : segmentEnd) - sourceAt;
                if (segmentLength == 0)
                {
                    if (destinationAt >= Size)
                    {
                        SetLastError(ERROR_INSUFFICIENT_BUFFER);
                        return false;
                    }
                    destination[destinationAt] = '\0';
                    return true;
                }
                const NarrowResult narrowed = NarrowExact(
                    std::wstring_view(source).substr(sourceAt, segmentLength));
                if (!narrowed.ok)
                {
                    SetLastError(Win32ErrorForNarrowRefusal(narrowed.refusal));
                    return false;
                }
                if (destinationAt + narrowed.value.size() + 1 >= Size)
                {
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                    return false;
                }
                std::memcpy(destination.data() + destinationAt,
                            narrowed.value.c_str(), narrowed.value.size() + 1);
                if (segmentEnd == std::wstring::npos)
                {
                    if (destinationAt + narrowed.value.size() + 1 >= Size)
                    {
                        SetLastError(ERROR_INSUFFICIENT_BUFFER);
                        return false;
                    }
                    destination[destinationAt + narrowed.value.size() + 1] = '\0';
                    return true;
                }
                sourceAt = segmentEnd + 1;
                destinationAt += narrowed.value.size() + 1;
            }
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
        }

        bool WideOffsetToAnsi(const wchar_t* text, int wideOffset,
                              int& ansiOffset)
        {
            if (text == nullptr || wideOffset < 0 ||
                static_cast<std::size_t>(wideOffset) > std::wcslen(text))
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return false;
            }
            if (wideOffset == 0)
            {
                ansiOffset = 0;
                return true;
            }
            const NarrowResult narrowed = NarrowExact(
                std::wstring_view(text, static_cast<std::size_t>(wideOffset)));
            if (!narrowed.ok)
            {
                SetLastError(Win32ErrorForNarrowRefusal(narrowed.refusal));
                return false;
            }
            if (narrowed.value.size() > static_cast<std::size_t>(INT_MAX))
            {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return false;
            }
            ansiOffset = static_cast<int>(narrowed.value.size());
            return true;
        }

        bool AnsiOffsetToWide(const char* text, int ansiOffset,
                              int& wideOffset)
        {
            if (text == nullptr || ansiOffset < 0 ||
                static_cast<std::size_t>(ansiOffset) > std::strlen(text))
            {
                SetLastError(ERROR_INVALID_PARAMETER);
                return false;
            }
            if (ansiOffset == 0)
            {
                wideOffset = 0;
                return true;
            }
            std::wstring prefix;
            if (!WidenPluginSpan(text, ansiOffset, prefix))
                return false;
            wideOffset = static_cast<int>(prefix.size());
            return true;
        }

        class CLegacyViewFileOnFS final
            : public sdk107::CSalamanderForViewFileOnFSAbstract
        {
        public:
            explicit CLegacyViewFileOnFS(
                ::CSalamanderForViewFileOnFSAbstract& wide)
                : Wide(wide)
            {
            }

            const char* WINAPI AllocFileNameInCache(
                HWND parent, const char* uniqueFileName,
                const char* nameInCache, const char* rootTmpPath,
                BOOL& fileExists) override
            {
                std::wstring unique;
                std::wstring name;
                std::wstring root;
                const wchar_t* uniqueValue = nullptr;
                const wchar_t* nameValue = nullptr;
                const wchar_t* rootValue = nullptr;
                if (!WidenOptionalText(uniqueFileName, unique, uniqueValue) ||
                    !WidenOptionalText(nameInCache, name, nameValue) ||
                    !WidenOptionalText(rootTmpPath, root, rootValue))
                    return nullptr;
                const wchar_t* result = Wide.AllocFileNameInCache(
                    parent, uniqueValue, nameValue, rootValue, fileExists);
                if (result == nullptr)
                    return nullptr;
                const NarrowResult narrowed = NarrowExact(result);
                if (!narrowed.ok)
                {
                    const DWORD conversionError =
                        Win32ErrorForNarrowRefusal(narrowed.refusal);
                    ::CQuadWord empty(0, 0);
                    Wide.FreeFileNameInCache(uniqueValue, fileExists, FALSE,
                                             empty, nullptr, FALSE, TRUE);
                    SetLastError(conversionError);
                    return nullptr;
                }
                try
                {
                    std::string key =
                        uniqueFileName != nullptr ? uniqueFileName : "";
                    auto published = CacheNames.insert_or_assign(
                        std::move(key), std::move(narrowed.value));
                    return published.first->second.c_str();
                }
                catch (const std::bad_alloc&)
                {
                    ::CQuadWord empty(0, 0);
                    Wide.FreeFileNameInCache(uniqueValue, fileExists, FALSE,
                                             empty, nullptr, FALSE, TRUE);
                    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                    return nullptr;
                }
                catch (const std::length_error&)
                {
                    ::CQuadWord empty(0, 0);
                    Wide.FreeFileNameInCache(uniqueValue, fileExists, FALSE,
                                             empty, nullptr, FALSE, TRUE);
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                    return nullptr;
                }
            }

            BOOL WINAPI OpenViewer(HWND parent, const char* fileName,
                                   HANDLE* fileLock,
                                   BOOL* fileLockOwner) override
            {
                std::wstring name;
                const wchar_t* value = nullptr;
                if (!WidenOptionalText(fileName, name, value))
                    return FALSE;
                return Wide.OpenViewer(parent, value, fileLock, fileLockOwner);
            }

            void WINAPI FreeFileNameInCache(
                const char* uniqueFileName, BOOL fileExists, BOOL newFileOK,
                const sdk107::CQuadWord& newFileSize, HANDLE fileLock,
                BOOL fileLockOwner, BOOL removeAsSoonAsPossible) override
            {
                std::wstring unique;
                const wchar_t* uniqueValue = nullptr;
                if (!WidenOptionalText(uniqueFileName, unique, uniqueValue))
                    return;
                ::CQuadWord size;
                ToLive(newFileSize, size);
                Wide.FreeFileNameInCache(
                    uniqueValue, fileExists, newFileOK, size, fileLock,
                    fileLockOwner, removeAsSoonAsPossible);
                try
                {
                    CacheNames.erase(uniqueFileName != nullptr
                                         ? uniqueFileName
                                         : "");
                }
                catch (const std::bad_alloc&)
                {
                    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                }
                catch (const std::length_error&)
                {
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                }
            }

        private:
            ::CSalamanderForViewFileOnFSAbstract& Wide;
            std::unordered_map<std::string, std::string> CacheNames;
        };

        int CompareWideFallback(const ::CFileData* file1,
                                const ::CFileData* file2)
        {
            if (file1 == file2)
                return 0;
            if (file1 == nullptr)
                return -1;
            if (file2 == nullptr)
                return 1;
            const wchar_t* name1 = file1->Name != nullptr ? file1->Name : L"";
            const wchar_t* name2 = file2->Name != nullptr ? file2->Name : L"";
            const int names = std::wcscmp(name1, name2);
            if (names != 0)
                return names;
            return std::less<const ::CFileData*>()(file1, file2) ? -1 : 1;
        }

        int Utf8UnitLength(unsigned char first)
        {
            if (first <= 0x7f)
                return 1;
            if (first >= 0xc2 && first <= 0xdf)
                return 2;
            if (first >= 0xe0 && first <= 0xef)
                return 3;
            if (first >= 0xf0 && first <= 0xf4)
                return 4;
            return 0;
        }

        int MergeEnumerationError(int error, bool skipped)
        {
            return skipped && error == SALENUM_SUCCESS ? SALENUM_ERROR : error;
        }

        class CLegacyEnumeration
        {
        public:
            CLegacyEnumeration(::SalEnumSelection next, void* nextParam)
                : NextWide(next), NextParam(nextParam)
            {
            }

            const char* Next(HWND parent, int enumFiles, BOOL* isDir,
                             sdk107::CQuadWord* size,
                             const sdk107::CFileData** fileData,
                             int* errorOccurred)
            {
                if (enumFiles == -1)
                {
                    Name.clear();
                    Row.reset();
                    Skipped = false;
                    NextWide(parent, enumFiles, nullptr, nullptr, nullptr,
                             NextParam, nullptr);
                    if (isDir != nullptr)
                        *isDir = FALSE;
                    if (size != nullptr)
                        *size = sdk107::CQuadWord(0, 0);
                    if (fileData != nullptr)
                        *fileData = nullptr;
                    if (errorOccurred != nullptr)
                        *errorOccurred = SALENUM_SUCCESS;
                    return nullptr;
                }

                for (;;)
                {
                    BOOL wideIsDir = FALSE;
                    ::CQuadWord wideSize(0, 0);
                    const ::CFileData* wideFileData = nullptr;
                    int wideError = SALENUM_SUCCESS;
                    const wchar_t* wideName = NextWide(
                        parent, enumFiles,
                        isDir != nullptr ? &wideIsDir : nullptr,
                        size != nullptr ? &wideSize : nullptr,
                        fileData != nullptr ? &wideFileData : nullptr,
                        NextParam, &wideError);
                    if (wideName == nullptr)
                    {
                        Row.reset();
                        if (fileData != nullptr)
                            *fileData = nullptr;
                        if (errorOccurred != nullptr)
                            *errorOccurred =
                                MergeEnumerationError(wideError, Skipped);
                        return nullptr;
                    }

                    NarrowResult narrowed = NarrowExact(wideName);
                    if (!narrowed.ok)
                    {
                        Skipped = true;
                        if (wideError == SALENUM_CANCEL)
                        {
                            if (fileData != nullptr)
                                *fileData = nullptr;
                            if (errorOccurred != nullptr)
                                *errorOccurred = SALENUM_CANCEL;
                            return nullptr;
                        }
                        continue;
                    }

                    if (fileData != nullptr && wideFileData != nullptr)
                    {
                        Row.emplace(*wideFileData);
                        if (!Row->Valid)
                        {
                            Row.reset();
                            Skipped = true;
                            continue;
                        }
                    }
                    else
                        Row.reset();

                    Name.swap(narrowed.value);
                    if (isDir != nullptr)
                        *isDir = wideIsDir;
                    if (size != nullptr)
                        *size = ToLegacy(wideSize);
                    if (fileData != nullptr)
                        *fileData = Row ? &Row->Row : nullptr;
                    if (errorOccurred != nullptr)
                        *errorOccurred =
                            MergeEnumerationError(wideError, Skipped);
                    return Name.c_str();
                }
            }

        private:
            ::SalEnumSelection NextWide;
            void* NextParam;
            std::string Name;
            std::optional<CLegacyRowMirror> Row;
            bool Skipped = false;
        };

        const char* WINAPI EnumerateLegacySelection(
            HWND parent, int enumFiles, BOOL* isDir,
            sdk107::CQuadWord* size, const sdk107::CFileData** fileData,
            void* parameter, int* errorOccurred)
        {
            return static_cast<CLegacyEnumeration*>(parameter)->Next(
                parent, enumFiles, isDir, size, fileData, errorOccurred);
        }

        class CLegacyDynamicString final : public sdk107::CDynamicString
        {
        public:
            BOOL WINAPI Add(const char* value, int length) override
            {
                if (value == nullptr)
                {
                    SetLastError(ERROR_INVALID_PARAMETER);
                    return FALSE;
                }
                std::size_t byteLength = 0;
                if (length == -1)
                    byteLength = std::strlen(value);
                else if (length == -2)
                    byteLength = std::strlen(value) + 1;
                else if (length >= 0)
                    byteLength = static_cast<std::size_t>(length);
                else
                {
                    SetLastError(ERROR_INVALID_PARAMETER);
                    return FALSE;
                }
                if (byteLength > static_cast<std::size_t>(INT_MAX))
                {
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                    return FALSE;
                }
                try
                {
                    Bytes.insert(Bytes.end(), value, value + byteLength);
                }
                catch (const std::bad_alloc&)
                {
                    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                    return FALSE;
                }
                catch (const std::length_error&)
                {
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                    return FALSE;
                }
                return TRUE;
            }

            bool Commit(::CDynamicString& destination) const
            {
                if (Bytes.empty())
                    return true;
                std::wstring staged;
                try
                {
                    staged.reserve(Bytes.size());
                    std::size_t offset = 0;
                    while (offset < Bytes.size())
                    {
                        const auto terminator = std::find(
                            Bytes.begin() + offset, Bytes.end(), '\0');
                        const std::size_t end = static_cast<std::size_t>(
                            terminator - Bytes.begin());
                        const std::size_t length = end - offset;
                        if (length != 0)
                        {
                            if (length > static_cast<std::size_t>(INT_MAX))
                            {
                                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                                return false;
                            }
                            std::wstring segment;
                            if (!WidenPluginSpan(
                                    Bytes.data() + offset,
                                    static_cast<int>(length), segment))
                                return false;
                            staged.append(segment);
                        }
                        if (terminator == Bytes.end())
                            break;
                        staged.push_back(L'\0');
                        offset = end + 1;
                    }
                }
                catch (const std::bad_alloc&)
                {
                    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                    return false;
                }
                catch (const std::length_error&)
                {
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                    return false;
                }
                if (staged.size() > static_cast<std::size_t>(INT_MAX))
                {
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                    return false;
                }
                return destination.Add(staged.data(),
                                       static_cast<int>(staged.size())) != FALSE;
            }

        private:
            std::vector<char> Bytes;
        };

    } // namespace

    namespace detail
    {

        bool RemapPluginInfoHotTexts(UINT codePage, const char* text,
                                     std::size_t textLength, DWORD* hotTexts,
                                     int count)
        {
            if (text == nullptr || hotTexts == nullptr || textLength > 1000 ||
                count < 0 || count > 100)
                return false;
            std::array<int, 1001> wideOffsets;
            wideOffsets.fill(-1);
            wideOffsets[0] = 0;

            std::size_t offset = 0;
            while (offset < textLength)
            {
                const unsigned char first =
                    static_cast<unsigned char>(text[offset]);
                int unitLength = 1;
                if (codePage == CP_UTF8)
                {
                    unitLength = Utf8UnitLength(first);
                    if (unitLength == 0)
                        return false;
                    for (int i = 1; i < unitLength; ++i)
                    {
                        if (offset + i >= textLength ||
                            (static_cast<unsigned char>(text[offset + i]) &
                             0xc0) != 0x80)
                            return false;
                    }
                }
                else if (IsDBCSLeadByteEx(codePage, first))
                {
                    unitLength = 2;
                }

                offset += unitLength;
                if (offset > textLength)
                    return false;
                std::wstring prefix;
                if (!WidenPluginSpanForCodePage(
                        text, static_cast<int>(offset), codePage, prefix))
                    return false;
                wideOffsets[offset] = static_cast<int>(prefix.size());
            }

            for (int i = 0; i < count; ++i)
            {
                const std::size_t start = LOWORD(hotTexts[i]);
                const std::size_t end = start + HIWORD(hotTexts[i]);
                if (end > textLength || wideOffsets[start] < 0 ||
                    wideOffsets[end] < 0)
                    return false;
                const int wideLength = wideOffsets[end] - wideOffsets[start];
                if (wideOffsets[start] > 0xffff || wideLength < 0 ||
                    wideLength > 0xffff)
                    return false;
                hotTexts[i] = MAKELONG(static_cast<WORD>(wideOffsets[start]),
                                       static_cast<WORD>(wideLength));
            }
            return true;
        }

    } // namespace detail

    CLegacyPluginDataInterface::CLegacyPluginDataInterface(
        sdk107::CPluginDataInterfaceAbstract& legacy)
        : FrozenInterface(legacy), Legacy(legacy), ViewTransfer(this, &legacy)
    {
    }

    sdk107::CPluginDataInterfaceAbstract*
    CLegacyPluginDataInterface::LegacyInterface() const
    {
        return &FrozenInterface;
    }

    BOOL WINAPI CLegacyPluginDataInterface::CallReleaseForFiles()
    {
        return Legacy.CallReleaseForFiles();
    }

    BOOL WINAPI CLegacyPluginDataInterface::CallReleaseForDirs()
    {
        return Legacy.CallReleaseForDirs();
    }

    void WINAPI CLegacyPluginDataInterface::ReleasePluginData(
        ::CFileData& file, BOOL isDir)
    {
        CLegacyRowMirror row(file);
        if (!row.Valid)
            return;
        Legacy.ReleasePluginData(row.Row, isDir);
        row.Commit(file);
    }

    void WINAPI CLegacyPluginDataInterface::GetFileDataForUpDir(
        const wchar_t* archivePath, ::CFileData& upDir)
    {
        std::string pathStorage;
        const char* path = nullptr;
        CLegacyRowMirror row(upDir);
        if (!row.Valid ||
            !NarrowOptionalText(archivePath, pathStorage, path))
            return;
        Legacy.GetFileDataForUpDir(path, row.Row);
        row.Commit(upDir);
    }

    BOOL WINAPI CLegacyPluginDataInterface::GetFileDataForNewDir(
        const wchar_t* dirName, ::CFileData& dir)
    {
        std::string pathStorage;
        const char* path = nullptr;
        CLegacyRowMirror row(dir);
        if (!row.Valid || !NarrowOptionalText(dirName, pathStorage, path))
            return FALSE;
        const BOOL result = Legacy.GetFileDataForNewDir(path, row.Row);
        if (result)
            row.Commit(dir);
        return result;
    }

    HIMAGELIST WINAPI CLegacyPluginDataInterface::GetSimplePluginIcons(
        int iconSize)
    {
        return Legacy.GetSimplePluginIcons(iconSize);
    }

    BOOL WINAPI CLegacyPluginDataInterface::HasSimplePluginIcon(
        ::CFileData& file, BOOL isDir)
    {
        CLegacyRowMirror row(file);
        if (!row.Valid)
            return FALSE;
        const BOOL result = Legacy.HasSimplePluginIcon(row.Row, isDir);
        row.Commit(file);
        return result;
    }

    HICON WINAPI CLegacyPluginDataInterface::GetPluginIcon(
        const ::CFileData* file, int iconSize, BOOL& destroyIcon)
    {
        if (file == nullptr)
            return nullptr;
        CLegacyRowMirror row(*file);
        if (!row.Valid)
            return nullptr;
        return Legacy.GetPluginIcon(&row.Row, iconSize, destroyIcon);
    }

    int WINAPI CLegacyPluginDataInterface::CompareFilesFromFS(
        const ::CFileData* file1, const ::CFileData* file2)
    {
        if (file1 == nullptr || file2 == nullptr)
            return CompareWideFallback(file1, file2);
        CLegacyRowMirror row1(*file1);
        CLegacyRowMirror row2(*file2);
        if (!row1.Valid || !row2.Valid)
            return CompareWideFallback(file1, file2);
        return Legacy.CompareFilesFromFS(&row1.Row, &row2.Row);
    }

    void WINAPI CLegacyPluginDataInterface::SetupView(
        BOOL leftPanel, ::CSalamanderViewAbstract* view,
        const wchar_t* archivePath, const ::CFileData* upperDir)
    {
        if (view == nullptr)
            return;
        std::string pathStorage;
        const char* path = nullptr;
        if (!NarrowOptionalText(archivePath, pathStorage, path))
            return;

        std::optional<CLegacyRowMirror> row;
        const sdk107::CFileData* legacyUpperDir = nullptr;
        if (upperDir != nullptr)
        {
            row.emplace(*upperDir);
            if (!row->Valid)
                return;
            legacyUpperDir = &row->Row;
        }

        CLegacySalamanderView legacyView(*view, ViewTransfer);
        Legacy.SetupView(leftPanel, &legacyView, path, legacyUpperDir);
    }

    void WINAPI CLegacyPluginDataInterface::ColumnFixedWidthShouldChange(
        BOOL leftPanel, const ::CColumn* column, int newFixedWidth)
    {
        const sdk107::CColumn* legacyColumn =
            ViewTransfer.MirrorColumnForPlugin(column);
        if (column != nullptr && legacyColumn == nullptr)
            return;
        Legacy.ColumnFixedWidthShouldChange(leftPanel, legacyColumn,
                                            newFixedWidth);
    }

    void WINAPI CLegacyPluginDataInterface::ColumnWidthWasChanged(
        BOOL leftPanel, const ::CColumn* column, int newWidth)
    {
        const sdk107::CColumn* legacyColumn =
            ViewTransfer.MirrorColumnForPlugin(column);
        if (column != nullptr && legacyColumn == nullptr)
            return;
        Legacy.ColumnWidthWasChanged(leftPanel, legacyColumn, newWidth);
    }

    BOOL WINAPI CLegacyPluginDataInterface::GetInfoLineContent(
        int panel, const ::CFileData* file, BOOL isDir, int selectedFiles,
        int selectedDirs, BOOL displaySize, const ::CQuadWord& selectedSize,
        CSalamanderStringBuffer* buffer,
        CSalamanderTextRangeBuffer* hotTexts)
    {
        if (buffer == nullptr || hotTexts == nullptr ||
            !sally::plugin_abi::IsValidStringBuffer(*buffer) ||
            !sally::plugin_abi::IsValidTextRangeBuffer(*hotTexts))
            return FALSE;
        std::optional<CLegacyRowMirror> row;
        const sdk107::CFileData* legacyFile = nullptr;
        if (file != nullptr)
        {
            row.emplace(*file);
            if (!row->Valid)
                return FALSE;
            legacyFile = &row->Row;
        }

        std::array<char, 1000> legacyBuffer = {};
        std::array<DWORD, 100> legacyHotTexts = {};
        int legacyHotTextCount = 100;
        const sdk107::CQuadWord legacySize = ToLegacy(selectedSize);
        if (!Legacy.GetInfoLineContent(
                panel, legacyFile, isDir, selectedFiles, selectedDirs,
                displaySize, legacySize, legacyBuffer.data(),
                legacyHotTexts.data(), legacyHotTextCount))
            return FALSE;
        if (legacyHotTextCount < 0 || legacyHotTextCount > 100)
            return FALSE;

        std::size_t legacyLength = 0;
        while (legacyLength < legacyBuffer.size() &&
               legacyBuffer[legacyLength] != '\0')
            ++legacyLength;
        if (legacyLength == legacyBuffer.size() ||
            !detail::RemapPluginInfoHotTexts(
                GetACP(), legacyBuffer.data(), legacyLength,
                legacyHotTexts.data(), legacyHotTextCount))
            return FALSE;

        std::wstring wideBuffer;
        if (!WidenBuffer(legacyBuffer, wideBuffer))
            return FALSE;
        std::array<CSalamanderTextRange, 100> publishedRanges = {};
        for (int index = 0; index < legacyHotTextCount; ++index)
            publishedRanges[static_cast<std::size_t>(index)] =
                {LOWORD(legacyHotTexts[index]), HIWORD(legacyHotTexts[index])};
        return sally::plugin_abi::WriteTextAndRanges(
                   *buffer, *hotTexts, wideBuffer, publishedRanges.data(),
                   static_cast<std::size_t>(legacyHotTextCount))
                   ? TRUE
                   : FALSE;
    }

    BOOL WINAPI CLegacyPluginDataInterface::CanBeCopiedToClipboard()
    {
        return Legacy.CanBeCopiedToClipboard();
    }

    BOOL WINAPI CLegacyPluginDataInterface::GetByteSize(
        const ::CFileData* file, BOOL isDir, ::CQuadWord* size)
    {
        if (file == nullptr || size == nullptr)
            return FALSE;
        CLegacyRowMirror row(*file);
        if (!row.Valid)
            return FALSE;
        sdk107::CQuadWord legacySize = ToLegacy(*size);
        const BOOL result = Legacy.GetByteSize(&row.Row, isDir, &legacySize);
        if (result)
            ToLive(legacySize, *size);
        return result;
    }

    BOOL WINAPI CLegacyPluginDataInterface::GetLastWriteDate(
        const ::CFileData* file, BOOL isDir, SYSTEMTIME* date)
    {
        if (file == nullptr || date == nullptr)
            return FALSE;
        CLegacyRowMirror row(*file);
        return row.Valid
                   ? Legacy.GetLastWriteDate(&row.Row, isDir, date)
                   : FALSE;
    }

    BOOL WINAPI CLegacyPluginDataInterface::GetLastWriteTime(
        const ::CFileData* file, BOOL isDir, SYSTEMTIME* time)
    {
        if (file == nullptr || time == nullptr)
            return FALSE;
        CLegacyRowMirror row(*file);
        return row.Valid
                   ? Legacy.GetLastWriteTime(&row.Row, isDir, time)
                   : FALSE;
    }

    ::CPluginDataInterfaceAbstract* CLegacyPluginDataOwner::Resolve(
        sdk107::CPluginDataInterfaceAbstract* legacy)
    {
        if (legacy == nullptr)
            return nullptr;
        auto found = Wrappers.find(legacy);
        if (found != Wrappers.end())
            return found->second.get();
        try
        {
            auto wrapper =
                std::make_unique<CLegacyPluginDataInterface>(*legacy);
            ::CPluginDataInterfaceAbstract* result = wrapper.get();
            const auto inserted = Wrappers.emplace(legacy, std::move(wrapper));
            if (!inserted.second)
                return inserted.first->second.get();
            try
            {
                const auto reverse = LegacyByWrapper.emplace(result, legacy);
                if (!reverse.second)
                {
                    Wrappers.erase(inserted.first);
                    SetLastError(ERROR_INVALID_DATA);
                    return nullptr;
                }
            }
            catch (...)
            {
                Wrappers.erase(inserted.first);
                throw;
            }
            return result;
        }
        catch (const std::bad_alloc&)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return nullptr;
        }
        catch (const std::length_error&)
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return nullptr;
        }
        catch (...)
        {
            SetLastError(ERROR_INVALID_DATA);
            return nullptr;
        }
    }

    sdk107::CPluginDataInterfaceAbstract* CLegacyPluginDataOwner::FindLegacy(
        const ::CPluginDataInterfaceAbstract* wide) const
    {
        const auto found = LegacyByWrapper.find(wide);
        return found != LegacyByWrapper.end() ? found->second : nullptr;
    }

    sdk107::CPluginDataInterfaceAbstract* CLegacyPluginDataOwner::Retire(
        ::CPluginDataInterfaceAbstract* wide)
    {
        const auto found = LegacyByWrapper.find(wide);
        if (found == LegacyByWrapper.end())
            return nullptr;
        sdk107::CPluginDataInterfaceAbstract* legacy = found->second;
        LegacyByWrapper.erase(found);
        Wrappers.erase(legacy);
        return legacy;
    }

    CLegacyPluginInterfaceForArchiver::CLegacyPluginInterfaceForArchiver(
        sdk107::CPluginInterfaceForArchiverAbstract& legacy,
        CLegacyPluginDataOwner& pluginDataOwner, int builtForVersion)
        : FrozenInterface(legacy), Legacy(legacy),
          PluginDataOwner(pluginDataOwner), BuiltForVersion(builtForVersion)
    {
    }

    sdk107::CPluginDataInterfaceAbstract*
    CLegacyPluginInterfaceForArchiver::ResolvePluginData(
        const ::CPluginDataInterfaceAbstract* pluginData) const
    {
        return pluginData != nullptr
                   ? PluginDataOwner.FindLegacy(pluginData)
                   : nullptr;
    }

    BOOL WINAPI CLegacyPluginInterfaceForArchiver::ListArchive(
        ::CSalamanderForOperationsAbstract* salamander,
        const wchar_t* fileName, ::CSalamanderDirectoryAbstract* directory,
        ::CPluginDataInterfaceAbstract*& pluginData)
    {
        std::string fileNameStorage;
        const char* legacyFileName = nullptr;
        if (!NarrowOptionalText(fileName, fileNameStorage, legacyFileName))
            return FALSE;

        std::optional<CLegacySalamanderForOperations> operations;
        if (salamander != nullptr &&
            !EmplaceBoundaryObject(operations, *salamander))
            return FALSE;
        std::optional<CLegacySalamanderDirectory> legacyDirectory;
        if (directory != nullptr &&
            !EmplaceBoundaryObject(legacyDirectory, *directory,
                                   PluginDataOwner, BuiltForVersion))
            return FALSE;

        sdk107::CPluginDataInterfaceAbstract* legacyPluginData = nullptr;
        if (!Legacy.ListArchive(
                operations ? &*operations : nullptr, legacyFileName,
                legacyDirectory ? &*legacyDirectory : nullptr,
                legacyPluginData))
            return FALSE;

        ::CPluginDataInterfaceAbstract* resolved =
            PluginDataOwner.Resolve(legacyPluginData);
        if (legacyPluginData != nullptr && resolved == nullptr)
            return FALSE;
        pluginData = resolved;
        return TRUE;
    }

    BOOL WINAPI CLegacyPluginInterfaceForArchiver::UnpackArchive(
        ::CSalamanderForOperationsAbstract* salamander,
        const wchar_t* fileName, ::CPluginDataInterfaceAbstract* pluginData,
        const wchar_t* targetDir, const wchar_t* archiveRoot,
        ::SalEnumSelection next, void* nextParam)
    {
        sdk107::CPluginDataInterfaceAbstract* legacyPluginData =
            ResolvePluginData(pluginData);
        if (pluginData != nullptr && legacyPluginData == nullptr)
            return FALSE;

        std::string fileNameStorage;
        std::string targetDirStorage;
        std::string archiveRootStorage;
        const char* legacyFileName = nullptr;
        const char* legacyTargetDir = nullptr;
        const char* legacyArchiveRoot = nullptr;
        if (!NarrowOptionalText(fileName, fileNameStorage, legacyFileName) ||
            !NarrowOptionalText(targetDir, targetDirStorage, legacyTargetDir) ||
            !NarrowOptionalText(archiveRoot, archiveRootStorage,
                                legacyArchiveRoot))
            return FALSE;

        std::optional<CLegacySalamanderForOperations> operations;
        if (salamander != nullptr &&
            !EmplaceBoundaryObject(operations, *salamander))
            return FALSE;
        std::optional<CLegacyEnumeration> enumeration;
        if (next != nullptr &&
            !EmplaceBoundaryObject(enumeration, next, nextParam))
            return FALSE;
        return Legacy.UnpackArchive(
            operations ? &*operations : nullptr, legacyFileName,
            legacyPluginData,
            legacyTargetDir, legacyArchiveRoot,
            enumeration ? &EnumerateLegacySelection : nullptr,
            enumeration ? &*enumeration : nextParam);
    }

    BOOL WINAPI CLegacyPluginInterfaceForArchiver::UnpackOneFile(
        ::CSalamanderForOperationsAbstract* salamander,
        const wchar_t* fileName, ::CPluginDataInterfaceAbstract* pluginData,
        const wchar_t* nameInArchive, const ::CFileData* fileData,
        const wchar_t* targetDir, const wchar_t* newFileName,
        BOOL* renamingNotSupported)
    {
        sdk107::CPluginDataInterfaceAbstract* legacyPluginData =
            ResolvePluginData(pluginData);
        if (pluginData != nullptr && legacyPluginData == nullptr)
            return FALSE;

        std::string fileNameStorage;
        std::string nameInArchiveStorage;
        std::string targetDirStorage;
        std::string newFileNameStorage;
        const char* legacyFileName = nullptr;
        const char* legacyNameInArchive = nullptr;
        const char* legacyTargetDir = nullptr;
        const char* legacyNewFileName = nullptr;
        if (!NarrowOptionalText(fileName, fileNameStorage, legacyFileName) ||
            !NarrowOptionalText(nameInArchive, nameInArchiveStorage,
                                legacyNameInArchive) ||
            !NarrowOptionalText(targetDir, targetDirStorage, legacyTargetDir) ||
            !NarrowOptionalText(newFileName, newFileNameStorage,
                                legacyNewFileName))
            return FALSE;

        std::optional<CLegacyRowMirror> row;
        if (fileData != nullptr)
        {
            row.emplace(*fileData);
            if (!row->Valid)
                return FALSE;
        }
        std::optional<CLegacySalamanderForOperations> operations;
        if (salamander != nullptr &&
            !EmplaceBoundaryObject(operations, *salamander))
            return FALSE;
        return Legacy.UnpackOneFile(
            operations ? &*operations : nullptr, legacyFileName,
            legacyPluginData, legacyNameInArchive,
            row ? &row->Row : nullptr,
            legacyTargetDir, legacyNewFileName, renamingNotSupported);
    }

    BOOL WINAPI CLegacyPluginInterfaceForArchiver::PackToArchive(
        ::CSalamanderForOperationsAbstract* salamander,
        const wchar_t* fileName, const wchar_t* archiveRoot, BOOL move,
        const wchar_t* sourcePath, ::SalEnumSelection2 next, void* nextParam)
    {
        std::string fileNameStorage;
        std::string archiveRootStorage;
        std::string sourcePathStorage;
        const char* legacyFileName = nullptr;
        const char* legacyArchiveRoot = nullptr;
        const char* legacySourcePath = nullptr;
        if (!NarrowOptionalText(fileName, fileNameStorage, legacyFileName) ||
            !NarrowOptionalText(archiveRoot, archiveRootStorage,
                                legacyArchiveRoot) ||
            !NarrowOptionalText(sourcePath, sourcePathStorage,
                                legacySourcePath))
            return FALSE;

        std::optional<CLegacySalamanderForOperations> operations;
        if (salamander != nullptr &&
            !EmplaceBoundaryObject(operations, *salamander))
            return FALSE;
        std::optional<CLegacySelection2Bridge> enumeration;
        if (next != nullptr &&
            !EmplaceBoundaryObject(enumeration, next, nextParam))
            return FALSE;
        return Legacy.PackToArchive(
            operations ? &*operations : nullptr, legacyFileName,
            legacyArchiveRoot, move,
            legacySourcePath,
            enumeration ? &CLegacySelection2Bridge::Invoke : nullptr,
            enumeration ? &*enumeration : nextParam);
    }

    BOOL WINAPI CLegacyPluginInterfaceForArchiver::DeleteFromArchive(
        ::CSalamanderForOperationsAbstract* salamander,
        const wchar_t* fileName, ::CPluginDataInterfaceAbstract* pluginData,
        const wchar_t* archiveRoot, ::SalEnumSelection next, void* nextParam)
    {
        sdk107::CPluginDataInterfaceAbstract* legacyPluginData =
            ResolvePluginData(pluginData);
        if (pluginData != nullptr && legacyPluginData == nullptr)
            return FALSE;

        std::string fileNameStorage;
        std::string archiveRootStorage;
        const char* legacyFileName = nullptr;
        const char* legacyArchiveRoot = nullptr;
        if (!NarrowOptionalText(fileName, fileNameStorage, legacyFileName) ||
            !NarrowOptionalText(archiveRoot, archiveRootStorage,
                                legacyArchiveRoot))
            return FALSE;

        std::optional<CLegacySalamanderForOperations> operations;
        if (salamander != nullptr &&
            !EmplaceBoundaryObject(operations, *salamander))
            return FALSE;
        std::optional<CLegacyEnumeration> enumeration;
        if (next != nullptr &&
            !EmplaceBoundaryObject(enumeration, next, nextParam))
            return FALSE;
        return Legacy.DeleteFromArchive(
            operations ? &*operations : nullptr, legacyFileName,
            legacyPluginData,
            legacyArchiveRoot,
            enumeration ? &EnumerateLegacySelection : nullptr,
            enumeration ? &*enumeration : nextParam);
    }

    BOOL WINAPI CLegacyPluginInterfaceForArchiver::UnpackWholeArchive(
        ::CSalamanderForOperationsAbstract* salamander,
        const wchar_t* fileName, const wchar_t* mask,
        const wchar_t* targetDir, BOOL delArchiveWhenDone,
        ::CDynamicString* archiveVolumes)
    {
        std::string fileNameStorage;
        std::string maskStorage;
        std::string targetDirStorage;
        const char* legacyFileName = nullptr;
        const char* legacyMask = nullptr;
        const char* legacyTargetDir = nullptr;
        if (!NarrowOptionalText(fileName, fileNameStorage, legacyFileName) ||
            !NarrowOptionalText(mask, maskStorage, legacyMask) ||
            !NarrowOptionalText(targetDir, targetDirStorage, legacyTargetDir))
            return FALSE;

        std::optional<CLegacySalamanderForOperations> operations;
        if (salamander != nullptr &&
            !EmplaceBoundaryObject(operations, *salamander))
            return FALSE;
        CLegacyDynamicString volumes;
        const BOOL result = Legacy.UnpackWholeArchive(
            operations ? &*operations : nullptr, legacyFileName,
            legacyMask, legacyTargetDir,
            delArchiveWhenDone,
            archiveVolumes != nullptr ? &volumes : nullptr);
        if (!result)
            return FALSE;
        return archiveVolumes == nullptr || volumes.Commit(*archiveVolumes);
    }

    BOOL WINAPI CLegacyPluginInterfaceForArchiver::CanCloseArchive(
        ::CSalamanderForOperationsAbstract* salamander,
        const wchar_t* fileName, BOOL force, int panel)
    {
        std::string fileNameStorage;
        const char* legacyFileName = nullptr;
        if (!NarrowOptionalText(fileName, fileNameStorage, legacyFileName))
            return FALSE;
        std::optional<CLegacySalamanderForOperations> operations;
        if (salamander != nullptr &&
            !EmplaceBoundaryObject(operations, *salamander))
            return FALSE;
        return Legacy.CanCloseArchive(
            operations ? &*operations : nullptr, legacyFileName, force,
            panel);
    }

    BOOL WINAPI CLegacyPluginInterfaceForArchiver::GetCacheInfo(
        CSalamanderStringBuffer* tempPath, BOOL* ownDelete, BOOL* cacheCopies)
    {
        if (tempPath == nullptr)
            return FALSE;
        std::array<char, MAX_PATH> legacyPath = {};
        BOOL legacyOwnDelete = ownDelete != nullptr ? *ownDelete : FALSE;
        BOOL legacyCacheCopies = cacheCopies != nullptr ? *cacheCopies : FALSE;
        if (!Legacy.GetCacheInfo(legacyPath.data(), &legacyOwnDelete,
                                 &legacyCacheCopies))
            return FALSE;
        std::wstring widePath;
        if (!WidenBuffer(legacyPath, widePath))
            return FALSE;
        if (!sally::plugin_abi::WriteStringBuffer(*tempPath, widePath))
            return FALSE;
        if (ownDelete != nullptr)
            *ownDelete = legacyOwnDelete;
        if (cacheCopies != nullptr)
            *cacheCopies = legacyCacheCopies;
        return TRUE;
    }

    void WINAPI CLegacyPluginInterfaceForArchiver::DeleteTmpCopy(
        const wchar_t* fileName, BOOL firstFile)
    {
        std::string fileNameStorage;
        const char* legacyFileName = nullptr;
        if (NarrowOptionalText(fileName, fileNameStorage, legacyFileName))
            Legacy.DeleteTmpCopy(legacyFileName, firstFile);
    }

    BOOL WINAPI CLegacyPluginInterfaceForArchiver::PrematureDeleteTmpCopy(
        HWND parent, int copiesCount)
    {
        return Legacy.PrematureDeleteTmpCopy(parent, copiesCount);
    }

    static_assert(sizeof(::CSalamanderPluginViewerData) ==
                  sizeof(sdk107::CSalamanderPluginViewerData));
    static_assert(alignof(::CSalamanderPluginViewerData) ==
                  alignof(sdk107::CSalamanderPluginViewerData));
    static_assert(offsetof(::CSalamanderPluginViewerData, Size) ==
                  offsetof(sdk107::CSalamanderPluginViewerData, Size));
    static_assert(offsetof(::CSalamanderPluginViewerData, FileName) ==
                  offsetof(sdk107::CSalamanderPluginViewerData, FileName));
    static_assert(sizeof(::CSalamanderPluginInternalViewerData) ==
                  sizeof(sdk107::CSalamanderPluginInternalViewerData));
    static_assert(alignof(::CSalamanderPluginInternalViewerData) ==
                  alignof(sdk107::CSalamanderPluginInternalViewerData));
    static_assert(offsetof(::CSalamanderPluginInternalViewerData, Mode) ==
                  offsetof(sdk107::CSalamanderPluginInternalViewerData, Mode));
    static_assert(offsetof(::CSalamanderPluginInternalViewerData, Caption) ==
                  offsetof(sdk107::CSalamanderPluginInternalViewerData, Caption));
    static_assert(offsetof(::CSalamanderPluginInternalViewerData, WholeCaption) ==
                  offsetof(sdk107::CSalamanderPluginInternalViewerData, WholeCaption));

    CLegacyPluginInterfaceForViewer::CLegacyPluginInterfaceForViewer(
        sdk107::CPluginInterfaceForViewerAbstract& legacy)
        : Legacy(legacy)
    {
    }

    BOOL WINAPI CLegacyPluginInterfaceForViewer::ViewFile(
        const wchar_t* name, int left, int top, int width, int height,
        UINT showCmd, BOOL alwaysOnTop, BOOL returnLock, HANDLE* lock,
        BOOL* lockOwner, ::CSalamanderPluginViewerData* viewerData,
        int enumFilesSourceUID, int enumFilesCurrentIndex)
    {
        std::string nameStorage;
        const char* legacyName = nullptr;
        if (!NarrowOptionalText(name, nameStorage, legacyName))
            return FALSE;

        std::vector<unsigned char> legacyViewerDataStorage;
        sdk107::CSalamanderPluginViewerData* legacyViewerData = nullptr;
        std::string fileNameStorage;
        std::string captionStorage;
        if (viewerData != nullptr)
        {
            if (viewerData->Size <
                static_cast<int>(sizeof(::CSalamanderPluginViewerData)))
                return FALSE;

            const char* legacyFileName = nullptr;
            if (!NarrowOptionalText(viewerData->FileName, fileNameStorage,
                                    legacyFileName))
                return FALSE;

            const char* legacyCaption = nullptr;
            if (viewerData->Size == static_cast<int>(
                                        sizeof(::CSalamanderPluginInternalViewerData)))
            {
                const auto* internal = static_cast<
                    const ::CSalamanderPluginInternalViewerData*>(viewerData);
                if (!NarrowOptionalText(internal->Caption, captionStorage,
                                        legacyCaption))
                    return FALSE;
            }

            // The record is explicitly extensible. Preserve every caller-owned
            // tail byte, then replace only fields whose live and frozen string
            // encodings differ. The copy and converted strings live through the
            // synchronous legacy call.
            try
            {
                legacyViewerDataStorage.resize(
                    static_cast<std::size_t>(viewerData->Size));
            }
            catch (...)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }
            std::memcpy(legacyViewerDataStorage.data(), viewerData,
                        legacyViewerDataStorage.size());
            legacyViewerData = reinterpret_cast<
                sdk107::CSalamanderPluginViewerData*>(
                legacyViewerDataStorage.data());
            legacyViewerData->FileName = legacyFileName;
            if (viewerData->Size == static_cast<int>(
                                        sizeof(::CSalamanderPluginInternalViewerData)))
            {
                auto* internal = reinterpret_cast<
                    sdk107::CSalamanderPluginInternalViewerData*>(
                    legacyViewerData);
                internal->Caption = legacyCaption;
            }
        }
        return Legacy.ViewFile(
            legacyName, left, top, width, height, showCmd, alwaysOnTop,
            returnLock, lock, lockOwner, legacyViewerData,
            enumFilesSourceUID, enumFilesCurrentIndex);
    }

    BOOL WINAPI CLegacyPluginInterfaceForViewer::CanViewFile(
        const wchar_t* name)
    {
        std::string nameStorage;
        const char* legacyName = nullptr;
        return NarrowOptionalText(name, nameStorage, legacyName)
                   ? Legacy.CanViewFile(legacyName)
                   : FALSE;
    }

    CLegacyPluginInterfaceForMenuExt::CLegacyPluginInterfaceForMenuExt(
        sdk107::CPluginInterfaceForMenuExtAbstract& legacy,
        CLegacyGUIIconListTransfer& iconListTransfer)
        : Legacy(legacy), IconListTransfer(iconListTransfer)
    {
    }

    DWORD WINAPI CLegacyPluginInterfaceForMenuExt::GetMenuItemState(
        int id, DWORD eventMask)
    {
        return Legacy.GetMenuItemState(id, eventMask);
    }

    BOOL WINAPI CLegacyPluginInterfaceForMenuExt::ExecuteMenuItem(
        ::CSalamanderForOperationsAbstract* salamander, HWND parent, int id,
        DWORD eventMask)
    {
        std::optional<CLegacySalamanderForOperations> legacySalamander;
        if (salamander != nullptr &&
            !EmplaceBoundaryObject(legacySalamander, *salamander))
            return FALSE;
        return Legacy.ExecuteMenuItem(
            legacySalamander ? &*legacySalamander : nullptr, parent, id,
            eventMask);
    }

    BOOL WINAPI CLegacyPluginInterfaceForMenuExt::HelpForMenuItem(
        HWND parent, int id)
    {
        return Legacy.HelpForMenuItem(parent, id);
    }

    void WINAPI CLegacyPluginInterfaceForMenuExt::BuildMenu(
        HWND parent, ::CSalamanderBuildMenuAbstract* salamander)
    {
        std::optional<CLegacySalamanderBuildMenu> legacySalamander;
        if (salamander != nullptr &&
            !EmplaceBoundaryObject(legacySalamander, *salamander,
                                   IconListTransfer))
            return;
        Legacy.BuildMenu(
            parent, legacySalamander ? &*legacySalamander : nullptr);
    }

    CLegacySalamanderThumbnailMaker::CLegacySalamanderThumbnailMaker(
        ::CSalamanderThumbnailMakerAbstract& wideMaker)
        : WideMaker(wideMaker)
    {
    }

    BOOL WINAPI CLegacySalamanderThumbnailMaker::SetParameters(
        int picWidth, int picHeight, DWORD flags)
    {
        return WideMaker.SetParameters(picWidth, picHeight, flags);
    }

    BOOL WINAPI CLegacySalamanderThumbnailMaker::ProcessBuffer(
        void* buffer, int rowsCount)
    {
        return WideMaker.ProcessBuffer(buffer, rowsCount);
    }

    void* WINAPI CLegacySalamanderThumbnailMaker::GetBuffer(int rowsCount)
    {
        return WideMaker.GetBuffer(rowsCount);
    }

    void WINAPI CLegacySalamanderThumbnailMaker::SetError()
    {
        WideMaker.SetError();
    }

    BOOL WINAPI CLegacySalamanderThumbnailMaker::GetCancelProcessing()
    {
        return WideMaker.GetCancelProcessing();
    }

    CLegacyPluginInterfaceForThumbLoader::
        CLegacyPluginInterfaceForThumbLoader(
            sdk107::CPluginInterfaceForThumbLoaderAbstract& legacy)
        : Legacy(legacy)
    {
    }

    BOOL WINAPI CLegacyPluginInterfaceForThumbLoader::LoadThumbnail(
        const wchar_t* filename, int thumbWidth, int thumbHeight,
        ::CSalamanderThumbnailMakerAbstract* thumbMaker, BOOL fastThumbnail)
    {
        std::string filenameStorage;
        const char* legacyFilename = nullptr;
        if (!NarrowOptionalText(filename, filenameStorage, legacyFilename))
            return FALSE;

        if (thumbMaker == nullptr)
            return Legacy.LoadThumbnail(legacyFilename, thumbWidth, thumbHeight,
                                        nullptr, fastThumbnail);

        CLegacySalamanderThumbnailMaker legacyMaker(*thumbMaker);
        return Legacy.LoadThumbnail(legacyFilename, thumbWidth, thumbHeight,
                                    &legacyMaker, fastThumbnail);
    }

    CLegacyPluginFSInterface::CLegacyPluginFSInterface(
        sdk107::CPluginFSInterfaceAbstract& legacy,
        CLegacyPluginDataOwner& pluginDataOwner, int builtForVersion)
        : FrozenInterface(legacy), Legacy(legacy),
          PluginDataOwner(pluginDataOwner), BuiltForVersion(builtForVersion)
    {
    }

    sdk107::CPluginFSInterfaceAbstract*
    CLegacyPluginFSInterface::LegacyInterface() const
    {
        return &FrozenInterface;
    }

    BOOL WINAPI CLegacyPluginFSInterface::GetCurrentPath(
        ::CSalamanderStringBuffer* userPart)
    {
        if (userPart == nullptr)
            return FALSE;
        std::wstring staged;
        if (BuiltForVersion >= SALLY_PLUGIN_WIDE_FS_VERSION)
        {
            if (!QueryFrozenWideOutput(
                    staged, [&](wchar_t* result, int size) {
                        return Legacy.GetCurrentPathW(result, size);
                    }))
                return FALSE;
        }
        else
        {
            std::array<char, MAX_PATH> result = {};
            if (!Legacy.GetCurrentPath(result.data()) ||
                !WidenBuffer(result, staged))
                return FALSE;
        }
        return sally::plugin_abi::WriteStringBuffer(*userPart, staged);
    }

    BOOL WINAPI CLegacyPluginFSInterface::GetFullName(
        ::CFileData& file, int isDir,
        ::CSalamanderStringBuffer* fullName)
    {
        if (fullName == nullptr)
            return FALSE;
        CLegacyRowMirror row(file);
        if (!row.Valid)
            return FALSE;
        std::wstring staged;
        if (BuiltForVersion >= SALLY_PLUGIN_WIDE_FS_VERSION)
        {
            if (!QueryFrozenWideOutput(
                    staged, [&](wchar_t* result, int size) {
                        return Legacy.GetFullNameW(row.Row, isDir, result,
                                                   size);
                    }))
                return FALSE;
        }
        else
        {
            std::array<char, MAX_PATH> result = {};
            if (!Legacy.GetFullName(row.Row, isDir, result.data(), MAX_PATH) ||
                !WidenBuffer(result, staged))
                return FALSE;
        }
        if (!sally::plugin_abi::WriteStringBuffer(*fullName, staged))
            return FALSE;
        row.Commit(file);
        return TRUE;
    }

    BOOL WINAPI CLegacyPluginFSInterface::GetFullFSPath(
        HWND parent, const wchar_t* fsName,
        ::CSalamanderStringBuffer* pathBuffer,
        BOOL& success)
    {
        std::wstring path;
        if (pathBuffer == nullptr ||
            !sally::plugin_abi::ReadStringBuffer(*pathBuffer, path))
        {
            success = FALSE;
            return FALSE;
        }
        if (BuiltForVersion >= SALLY_PLUGIN_WIDE_FS_VERSION)
        {
            if (path.size() >= static_cast<std::size_t>(INT_MAX))
            {
                success = FALSE;
                return FALSE;
            }
            try
            {
                std::vector<wchar_t> result(
                    (std::max)(static_cast<std::size_t>(MAX_PATH),
                               path.size() + 1),
                    L'\0');
                for (;;)
                {
                    std::fill(result.begin(), result.end(), L'\0');
                    std::wmemcpy(result.data(), path.c_str(), path.size() + 1);
                    SetLastError(ERROR_SUCCESS);
                    const BOOL returned = Legacy.GetFullFSPathW(
                        parent, fsName, result.data(),
                        static_cast<int>(result.size()), success);
                    if (returned)
                    {
                        if (success)
                        {
                            const std::size_t length =
                                ::wcsnlen(result.data(), result.size());
                            std::wstring staged;
                            if (length == result.size() ||
                                !AssignWideSpan(staged, result.data(), length) ||
                                !sally::plugin_abi::WriteStringBuffer(
                                    *pathBuffer, staged))
                            {
                                success = FALSE;
                                return FALSE;
                            }
                        }
                        return TRUE;
                    }
                    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
                        result.size() >
                            static_cast<std::size_t>(INT_MAX) / 2)
                        return FALSE;
                    result.assign(result.size() * 2, L'\0');
                }
            }
            catch (const std::bad_alloc&)
            {
                success = FALSE;
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }
            catch (const std::length_error&)
            {
                success = FALSE;
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return FALSE;
            }
        }
        std::string fsNameStorage;
        const char* legacyFSName = nullptr;
        std::array<char, MAX_PATH> legacyPath = {};
        if (!NarrowOptionalText(fsName, fsNameStorage, legacyFSName) ||
            !NarrowBuffer(path.c_str(), legacyPath))
        {
            success = FALSE;
            return FALSE;
        }
        const BOOL returned = Legacy.GetFullFSPath(
            parent, legacyFSName, legacyPath.data(), MAX_PATH, success);
        if (returned && success)
        {
            std::wstring converted;
            if (!WidenBuffer(legacyPath, converted) ||
                !sally::plugin_abi::WriteStringBuffer(*pathBuffer, converted))
            {
                success = FALSE;
                return FALSE;
            }
        }
        return returned;
    }

    BOOL WINAPI CLegacyPluginFSInterface::GetRootPath(
        ::CSalamanderStringBuffer* userPart)
    {
        if (userPart == nullptr)
            return FALSE;
        std::wstring staged;
        if (BuiltForVersion >= SALLY_PLUGIN_WIDE_FS_VERSION)
        {
            if (!QueryFrozenWideOutput(
                    staged, [&](wchar_t* result, int size) {
                        return Legacy.GetRootPathW(result, size);
                    }))
                return FALSE;
        }
        else
        {
            std::array<char, MAX_PATH> result = {};
            if (!Legacy.GetRootPath(result.data()) ||
                !WidenBuffer(result, staged))
                return FALSE;
        }
        return sally::plugin_abi::WriteStringBuffer(*userPart, staged);
    }

    BOOL WINAPI CLegacyPluginFSInterface::IsCurrentPath(
        int currentFSNameIndex, int fsNameIndex, const wchar_t* userPart)
    {
        if (BuiltForVersion >= SALLY_PLUGIN_WIDE_FS_VERSION)
            return Legacy.IsCurrentPathW(currentFSNameIndex, fsNameIndex,
                                         userPart);
        std::string storage;
        const char* legacyUserPart = nullptr;
        return NarrowOptionalText(userPart, storage, legacyUserPart)
                   ? Legacy.IsCurrentPath(currentFSNameIndex, fsNameIndex,
                                          legacyUserPart)
                   : FALSE;
    }

    BOOL WINAPI CLegacyPluginFSInterface::IsOurPath(
        int currentFSNameIndex, int fsNameIndex, const wchar_t* userPart)
    {
        if (BuiltForVersion >= SALLY_PLUGIN_WIDE_FS_VERSION)
            return Legacy.IsOurPathW(currentFSNameIndex, fsNameIndex,
                                     userPart);
        std::string storage;
        const char* legacyUserPart = nullptr;
        return NarrowOptionalText(userPart, storage, legacyUserPart)
                   ? Legacy.IsOurPath(currentFSNameIndex, fsNameIndex,
                                      legacyUserPart)
                   : FALSE;
    }

    BOOL WINAPI CLegacyPluginFSInterface::ChangePath(
        int currentFSNameIndex, ::CSalamanderStringBuffer* fsNameBuffer,
        int fsNameIndex, const wchar_t* userPart,
        ::CSalamanderStringBuffer* cutFileName,
        BOOL* pathWasCut, BOOL forceRefresh, int mode)
    {
        std::wstring fsName;
        if (fsNameBuffer == nullptr ||
            !sally::plugin_abi::ReadStringBuffer(*fsNameBuffer, fsName))
            return FALSE;

        std::wstring fsNameResult;
        std::wstring cutResult;
        BOOL returned = FALSE;
        if (BuiltForVersion >= SALLY_PLUGIN_WIDE_FS_VERSION)
        {
            if (fsName.size() >= MAX_PATH)
                return FALSE;
            std::array<wchar_t, MAX_PATH> legacyFSName = {};
            std::array<wchar_t, MAX_PATH> legacyCut = {};
            std::wcscpy(legacyFSName.data(), fsName.c_str());
            returned = Legacy.ChangePathW(
                currentFSNameIndex, legacyFSName.data(), fsNameIndex, userPart,
                cutFileName != nullptr ? legacyCut.data() : nullptr,
                MAX_PATH, pathWasCut, forceRefresh, mode);
            if (!returned)
                return FALSE;
            if (std::wmemchr(legacyFSName.data(), L'\0',
                            legacyFSName.size()) ==
                    nullptr ||
                (cutFileName != nullptr &&
                 std::wmemchr(legacyCut.data(), L'\0', legacyCut.size()) ==
                     nullptr))
                return FALSE;
            const std::size_t fsNameLength =
                ::wcsnlen(legacyFSName.data(), legacyFSName.size());
            const std::size_t cutLength =
                ::wcsnlen(legacyCut.data(), legacyCut.size());
            if (!AssignWideSpan(fsNameResult, legacyFSName.data(),
                                fsNameLength) ||
                (cutFileName != nullptr &&
                 !AssignWideSpan(cutResult, legacyCut.data(), cutLength)))
                return FALSE;
        }
        else
        {
            std::array<char, MAX_PATH> legacyFSName = {};
            std::array<char, MAX_PATH> legacyCut = {};
            std::string userStorage;
            const char* legacyUserPart = nullptr;
            if (!NarrowBuffer(fsName.c_str(), legacyFSName) ||
                !NarrowOptionalText(userPart, userStorage, legacyUserPart))
                return FALSE;
            returned = Legacy.ChangePath(
                currentFSNameIndex, legacyFSName.data(), fsNameIndex,
                legacyUserPart,
                cutFileName != nullptr ? legacyCut.data() : nullptr,
                pathWasCut, forceRefresh, mode);
            if (!returned || !WidenBuffer(legacyFSName, fsNameResult) ||
                (cutFileName != nullptr &&
                 !WidenBuffer(legacyCut, cutResult)))
                return FALSE;
        }

        if (!sally::plugin_abi::ReserveStringBuffer(
                *fsNameBuffer, static_cast<DWORD>(fsNameResult.size() + 1)) ||
            (cutFileName != nullptr &&
             !sally::plugin_abi::ReserveStringBuffer(
                 *cutFileName, static_cast<DWORD>(cutResult.size() + 1))))
            return FALSE;
        return sally::plugin_abi::WriteStringBuffer(*fsNameBuffer,
                                                     fsNameResult) &&
               (cutFileName == nullptr ||
                sally::plugin_abi::WriteStringBuffer(*cutFileName,
                                                     cutResult));
    }

    BOOL WINAPI CLegacyPluginFSInterface::ListCurrentPath(
        ::CSalamanderDirectoryAbstract* dir,
        ::CPluginDataInterfaceAbstract*& pluginData, int& iconsType,
        BOOL forceRefresh)
    {
        std::optional<CLegacySalamanderDirectory> legacyDirectory;
        if (dir != nullptr &&
            !EmplaceBoundaryObject(legacyDirectory, *dir, PluginDataOwner,
                                   BuiltForVersion))
            return FALSE;
        sdk107::CPluginDataInterfaceAbstract* legacyPluginData = nullptr;
        int returnedIconsType = iconsType;
        if (!Legacy.ListCurrentPath(
                                    legacyDirectory ? &*legacyDirectory : nullptr,
                                    legacyPluginData,
                                    returnedIconsType, forceRefresh))
            return FALSE;
        ::CPluginDataInterfaceAbstract* resolved =
            PluginDataOwner.Resolve(legacyPluginData);
        if (legacyPluginData != nullptr && resolved == nullptr)
            return FALSE;
        pluginData = resolved;
        iconsType = returnedIconsType;
        return TRUE;
    }

    BOOL WINAPI CLegacyPluginFSInterface::TryCloseOrDetach(
        BOOL forceClose, BOOL canDetach, BOOL& detach, int reason)
    {
        return Legacy.TryCloseOrDetach(forceClose, canDetach, detach, reason);
    }

    void WINAPI CLegacyPluginFSInterface::Event(int event, DWORD param)
    {
        Legacy.Event(event, param);
    }

    void WINAPI CLegacyPluginFSInterface::ReleaseObject(HWND parent)
    {
        Legacy.ReleaseObject(parent);
    }

    DWORD WINAPI CLegacyPluginFSInterface::GetSupportedServices()
    {
        return Legacy.GetSupportedServices();
    }

    BOOL WINAPI CLegacyPluginFSInterface::GetChangeDriveOrDisconnectItem(
        const wchar_t* fsName, wchar_t*& title, HICON& icon,
        BOOL& destroyIcon)
    {
        std::string fsNameStorage;
        const char* legacyFSName = nullptr;
        if (!NarrowOptionalText(fsName, fsNameStorage, legacyFSName))
            return FALSE;
        char* legacyTitle = nullptr;
        HICON returnedIcon = icon;
        BOOL returnedDestroyIcon = destroyIcon;
        if (!Legacy.GetChangeDriveOrDisconnectItem(
                legacyFSName, legacyTitle, returnedIcon,
                returnedDestroyIcon))
            return FALSE;
        if (legacyTitle == nullptr)
            return FALSE;
        std::wstring widened;
        const bool titleValid = WidenPluginText(legacyTitle, widened);
        std::free(legacyTitle);
        if (!titleValid)
            return FALSE;
        const std::size_t bytes = (widened.size() + 1) * sizeof(wchar_t);
        auto* returnedTitle = static_cast<wchar_t*>(std::malloc(bytes));
        if (returnedTitle == nullptr)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        std::memcpy(returnedTitle, widened.c_str(), bytes);
        title = returnedTitle;
        icon = returnedIcon;
        destroyIcon = returnedDestroyIcon;
        return TRUE;
    }

    HICON WINAPI CLegacyPluginFSInterface::GetFSIcon(BOOL& destroyIcon)
    {
        return Legacy.GetFSIcon(destroyIcon);
    }

    void WINAPI CLegacyPluginFSInterface::GetDropEffect(
        const wchar_t* srcFSPath, const wchar_t* tgtFSPath,
        DWORD allowedEffects, DWORD keyState, DWORD* dropEffect)
    {
        std::string sourceStorage;
        std::string targetStorage;
        const char* source = nullptr;
        const char* target = nullptr;
        if (NarrowOptionalText(srcFSPath, sourceStorage, source) &&
            NarrowOptionalText(tgtFSPath, targetStorage, target))
            Legacy.GetDropEffect(source, target, allowedEffects, keyState,
                                 dropEffect);
    }

    void WINAPI CLegacyPluginFSInterface::GetFSFreeSpace(
        ::CQuadWord* retValue)
    {
        if (retValue == nullptr)
            return;
        sdk107::CQuadWord legacyValue;
        Legacy.GetFSFreeSpace(&legacyValue);
        ToLive(legacyValue, *retValue);
    }

    BOOL WINAPI CLegacyPluginFSInterface::GetNextDirectoryLineHotPath(
        const wchar_t* text, int pathLen, int& offset)
    {
        std::string textStorage;
        const char* legacyText = nullptr;
        int legacyPathLen = 0;
        int legacyOffset = 0;
        if (!NarrowOptionalText(text, textStorage, legacyText) ||
            !WideOffsetToAnsi(text, pathLen, legacyPathLen) ||
            !WideOffsetToAnsi(text, offset, legacyOffset))
            return FALSE;
        if (!Legacy.GetNextDirectoryLineHotPath(
                legacyText, legacyPathLen, legacyOffset))
            return FALSE;
        int wideOffset = 0;
        if (!AnsiOffsetToWide(legacyText, legacyOffset, wideOffset))
            return FALSE;
        offset = wideOffset;
        return TRUE;
    }

    BOOL WINAPI CLegacyPluginFSInterface::CompleteDirectoryLineHotPath(
        CSalamanderStringBuffer* path)
    {
        if (path == nullptr)
            return FALSE;
        std::wstring pathText;
        if (!sally::plugin_abi::ReadStringBuffer(*path, pathText))
            return FALSE;
        std::array<char, MAX_PATH> legacyPath = {};
        if (!NarrowBuffer(pathText.c_str(), legacyPath))
            return FALSE;
        Legacy.CompleteDirectoryLineHotPath(legacyPath.data(),
                                            static_cast<int>(legacyPath.size()));
        std::wstring completed;
        if (!WidenBuffer(legacyPath, completed))
            return FALSE;
        return sally::plugin_abi::WriteStringBuffer(*path, completed)
                   ? TRUE
                   : FALSE;
    }

    BOOL WINAPI CLegacyPluginFSInterface::GetPathForMainWindowTitle(
        const wchar_t* fsName, int mode, CSalamanderStringBuffer* buf)
    {
        if (buf == nullptr || !sally::plugin_abi::IsValidStringBuffer(*buf))
            return FALSE;
        std::string fsNameStorage;
        const char* legacyFSName = nullptr;
        if (!NarrowOptionalText(fsName, fsNameStorage, legacyFSName))
            return FALSE;
        std::array<char, MAX_PATH> legacyBuf = {};
        if (!Legacy.GetPathForMainWindowTitle(
                legacyFSName, mode, legacyBuf.data(),
                static_cast<int>(legacyBuf.size())))
            return FALSE;
        std::wstring title;
        if (!WidenBuffer(legacyBuf, title))
            return FALSE;
        return sally::plugin_abi::WriteStringBuffer(*buf, title)
                   ? TRUE
                   : FALSE;
    }

    void WINAPI CLegacyPluginFSInterface::ShowInfoDialog(
        const wchar_t* fsName, HWND parent)
    {
        std::string storage;
        const char* legacyFSName = nullptr;
        if (NarrowOptionalText(fsName, storage, legacyFSName))
            Legacy.ShowInfoDialog(legacyFSName, parent);
    }

    BOOL WINAPI CLegacyPluginFSInterface::ExecuteCommandLine(
        HWND parent, CSalamanderStringBuffer* command, int& selFrom, int& selTo)
    {
        std::wstring liveCommand;
        if (command == nullptr ||
            !sally::plugin_abi::ReadStringBuffer(*command, liveCommand))
            return FALSE;
        std::array<char, SALCMDLINE_MAXLEN + 1> legacyCommand = {};
        const NarrowResult narrowed =
            !liveCommand.empty() ? NarrowExact(liveCommand.c_str()) : NarrowResult{true, ""};
        int legacyFrom = 0;
        int legacyTo = 0;
        if (!narrowed.ok || narrowed.value.size() > SALCMDLINE_MAXLEN ||
            !WideOffsetToAnsi(liveCommand.c_str(), selFrom, legacyFrom) ||
            !WideOffsetToAnsi(liveCommand.c_str(), selTo, legacyTo))
            return FALSE;
        std::memcpy(legacyCommand.data(), narrowed.value.c_str(),
                    narrowed.value.size() + 1);
        if (!Legacy.ExecuteCommandLine(parent, legacyCommand.data(),
                                       legacyFrom, legacyTo))
            return FALSE;
        std::wstring widened;
        if (!WidenBuffer(legacyCommand, widened))
            return FALSE;
        if (!AnsiOffsetToWide(legacyCommand.data(), legacyFrom, selFrom) ||
            !AnsiOffsetToWide(legacyCommand.data(), legacyTo, selTo))
            return FALSE;
        return sally::plugin_abi::WriteStringBuffer(*command, widened) ? TRUE : FALSE;
    }

    BOOL WINAPI CLegacyPluginFSInterface::QuickRename(
        const wchar_t* fsName, int mode, HWND parent, ::CFileData& file,
        BOOL isDir, CSalamanderStringBuffer* newName, BOOL& cancel)
    {
        std::wstring liveName;
        if (newName == nullptr ||
            !sally::plugin_abi::ReadStringBuffer(*newName, liveName))
            return FALSE;
        std::string fsNameStorage;
        const char* legacyFSName = nullptr;
        std::array<char, MAX_PATH> legacyName = {};
        CLegacyRowMirror row(file);
        if (!row.Valid ||
            !NarrowOptionalText(fsName, fsNameStorage, legacyFSName) ||
            !NarrowBuffer(liveName.c_str(), legacyName))
            return FALSE;
        const BOOL returned = Legacy.QuickRename(
            legacyFSName, mode, parent, row.Row, isDir, legacyName.data(),
            cancel);
        std::wstring wideName;
        if (!WidenBuffer(legacyName, wideName))
            return FALSE;
        if (!sally::plugin_abi::WriteStringBuffer(*newName, wideName))
            return FALSE;
        row.Commit(file);
        return returned;
    }

    void WINAPI CLegacyPluginFSInterface::AcceptChangeOnPathNotification(
        const wchar_t* fsName, const wchar_t* path, BOOL includingSubdirs)
    {
        std::string fsNameStorage;
        std::string pathStorage;
        const char* legacyFSName = nullptr;
        const char* legacyPath = nullptr;
        if (NarrowOptionalText(fsName, fsNameStorage, legacyFSName) &&
            NarrowOptionalText(path, pathStorage, legacyPath))
            Legacy.AcceptChangeOnPathNotification(
                legacyFSName, legacyPath, includingSubdirs);
    }

    BOOL WINAPI CLegacyPluginFSInterface::CreateDir(
        const wchar_t* fsName, int mode, HWND parent,
        CSalamanderStringBuffer* newName,
        BOOL& cancel)
    {
        std::wstring liveName;
        if (newName == nullptr ||
            !sally::plugin_abi::ReadStringBuffer(*newName, liveName))
            return FALSE;
        std::string fsNameStorage;
        const char* legacyFSName = nullptr;
        std::array<char, 2 * MAX_PATH> legacyName = {};
        if (!NarrowOptionalText(fsName, fsNameStorage, legacyFSName) ||
            !NarrowBuffer(liveName.c_str(), legacyName))
            return FALSE;
        const BOOL returned = Legacy.CreateDir(
            legacyFSName, mode, parent, legacyName.data(), cancel);
        std::wstring wideName;
        if (!WidenBuffer(legacyName, wideName))
            return FALSE;
        if (!sally::plugin_abi::WriteStringBuffer(*newName, wideName))
            return FALSE;
        return returned;
    }

    void WINAPI CLegacyPluginFSInterface::ViewFile(
        const wchar_t* fsName, HWND parent,
        ::CSalamanderForViewFileOnFSAbstract* salamander, ::CFileData& file)
    {
        std::string fsNameStorage;
        const char* legacyFSName = nullptr;
        CLegacyRowMirror row(file);
        if (!row.Valid ||
            !NarrowOptionalText(fsName, fsNameStorage, legacyFSName))
            return;
        std::optional<CLegacyViewFileOnFS> legacySalamander;
        if (salamander != nullptr &&
            !EmplaceBoundaryObject(legacySalamander, *salamander))
            return;
        Legacy.ViewFile(
            legacyFSName, parent,
            legacySalamander ? &*legacySalamander : nullptr, row.Row);
        row.Commit(file);
    }

    BOOL WINAPI CLegacyPluginFSInterface::Delete(
        const wchar_t* fsName, int mode, HWND parent, int panel,
        int selectedFiles, int selectedDirs, BOOL& cancelOrError)
    {
        std::string storage;
        const char* legacyFSName = nullptr;
        return NarrowOptionalText(fsName, storage, legacyFSName)
                   ? Legacy.Delete(legacyFSName, mode, parent, panel,
                                   selectedFiles, selectedDirs, cancelOrError)
                   : FALSE;
    }

    BOOL WINAPI CLegacyPluginFSInterface::CopyOrMoveFromFS(
        BOOL copy, int mode, const wchar_t* fsName, HWND parent, int panel,
        int selectedFiles, int selectedDirs, CSalamanderStringBuffer* targetPath,
        BOOL& operationMask, BOOL& cancelOrHandlePath, HWND dropTarget)
    {
        std::wstring liveTarget;
        if (targetPath == nullptr ||
            !sally::plugin_abi::ReadStringBuffer(*targetPath, liveTarget))
            return FALSE;
        std::string fsNameStorage;
        const char* legacyFSName = nullptr;
        std::array<char, 2 * MAX_PATH> legacyTarget = {};
        if (!NarrowOptionalText(fsName, fsNameStorage, legacyFSName) ||
            !NarrowMultiString(liveTarget, legacyTarget))
            return FALSE;
        const BOOL returned = Legacy.CopyOrMoveFromFS(
            copy, mode, legacyFSName, parent, panel, selectedFiles,
            selectedDirs, legacyTarget.data(), operationMask,
            cancelOrHandlePath, dropTarget);
        std::wstring returnedTarget;
        if (!WidenBuffer(legacyTarget, returnedTarget))
            return FALSE;
        if (!sally::plugin_abi::WriteStringBuffer(*targetPath, returnedTarget))
            return FALSE;
        return returned;
    }

    BOOL WINAPI CLegacyPluginFSInterface::CopyOrMoveFromDiskToFS(
        BOOL copy, int mode, const wchar_t* fsName, HWND parent,
        const wchar_t* sourcePath, ::SalEnumSelection2 next, void* nextParam,
        int sourceFiles, int sourceDirs, CSalamanderStringBuffer* targetPath,
        BOOL* invalidPathOrCancel)
    {
        std::wstring liveTarget;
        if (targetPath == nullptr ||
            !sally::plugin_abi::ReadStringBuffer(*targetPath, liveTarget))
            return FALSE;
        std::string fsNameStorage;
        std::string sourceStorage;
        const char* legacyFSName = nullptr;
        const char* legacySource = nullptr;
        std::array<char, 2 * MAX_PATH> legacyTarget = {};
        if (!NarrowOptionalText(fsName, fsNameStorage, legacyFSName) ||
            !NarrowOptionalText(sourcePath, sourceStorage, legacySource) ||
            !NarrowMultiString(liveTarget, legacyTarget))
            return FALSE;
        std::optional<CLegacySelection2Bridge> enumeration;
        if (next != nullptr &&
            !EmplaceBoundaryObject(enumeration, next, nextParam))
            return FALSE;
        const BOOL returned = Legacy.CopyOrMoveFromDiskToFS(
            copy, mode, legacyFSName, parent, legacySource,
            enumeration ? &CLegacySelection2Bridge::Invoke : nullptr,
            enumeration ? &*enumeration : nextParam,
            sourceFiles, sourceDirs, legacyTarget.data(),
            invalidPathOrCancel);
        std::wstring returnedTarget;
        if (!WidenBuffer(legacyTarget, returnedTarget))
            return FALSE;
        if (!sally::plugin_abi::WriteStringBuffer(*targetPath, returnedTarget))
            return FALSE;
        return returned;
    }

    BOOL WINAPI CLegacyPluginFSInterface::ChangeAttributes(
        const wchar_t* fsName, HWND parent, int panel, int selectedFiles,
        int selectedDirs)
    {
        std::string storage;
        const char* legacyFSName = nullptr;
        return NarrowOptionalText(fsName, storage, legacyFSName)
                   ? Legacy.ChangeAttributes(legacyFSName, parent, panel,
                                             selectedFiles, selectedDirs)
                   : FALSE;
    }

    void WINAPI CLegacyPluginFSInterface::ShowProperties(
        const wchar_t* fsName, HWND parent, int panel, int selectedFiles,
        int selectedDirs)
    {
        std::string storage;
        const char* legacyFSName = nullptr;
        if (NarrowOptionalText(fsName, storage, legacyFSName))
            Legacy.ShowProperties(legacyFSName, parent, panel, selectedFiles,
                                  selectedDirs);
    }

    void WINAPI CLegacyPluginFSInterface::ContextMenu(
        const wchar_t* fsName, HWND parent, int menuX, int menuY, int type,
        int panel, int selectedFiles, int selectedDirs)
    {
        std::string storage;
        const char* legacyFSName = nullptr;
        if (NarrowOptionalText(fsName, storage, legacyFSName))
            Legacy.ContextMenu(legacyFSName, parent, menuX, menuY, type,
                               panel, selectedFiles, selectedDirs);
    }

    BOOL WINAPI CLegacyPluginFSInterface::HandleMenuMsg(
        UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult)
    {
        return Legacy.HandleMenuMsg(uMsg, wParam, lParam, plResult);
    }

    BOOL WINAPI CLegacyPluginFSInterface::OpenFindDialog(
        const wchar_t* fsName, int panel)
    {
        std::string storage;
        const char* legacyFSName = nullptr;
        return NarrowOptionalText(fsName, storage, legacyFSName)
                   ? Legacy.OpenFindDialog(legacyFSName, panel)
                   : FALSE;
    }

    void WINAPI CLegacyPluginFSInterface::OpenActiveFolder(
        const wchar_t* fsName, HWND parent)
    {
        std::string storage;
        const char* legacyFSName = nullptr;
        if (NarrowOptionalText(fsName, storage, legacyFSName))
            Legacy.OpenActiveFolder(legacyFSName, parent);
    }

    void WINAPI CLegacyPluginFSInterface::GetAllowedDropEffects(
        int mode, const wchar_t* tgtFSPath, DWORD* allowedEffects)
    {
        std::string storage;
        const char* legacyPath = nullptr;
        if (NarrowOptionalText(tgtFSPath, storage, legacyPath))
            Legacy.GetAllowedDropEffects(mode, legacyPath, allowedEffects);
    }

    BOOL WINAPI CLegacyPluginFSInterface::GetNoItemsInPanelText(
        CSalamanderStringBuffer* textBuf)
    {
        if (textBuf == nullptr ||
            !sally::plugin_abi::IsValidStringBuffer(*textBuf))
            return FALSE;
        std::array<char, 300> legacyText = {};
        if (!Legacy.GetNoItemsInPanelText(
                legacyText.data(), static_cast<int>(legacyText.size())))
            return FALSE;
        std::wstring text;
        if (!WidenBuffer(legacyText, text))
            return FALSE;
        return sally::plugin_abi::WriteStringBuffer(*textBuf, text)
                   ? TRUE
                   : FALSE;
    }

    void WINAPI CLegacyPluginFSInterface::ShowSecurityInfo(HWND parent)
    {
        Legacy.ShowSecurityInfo(parent);
    }

    CLegacyPluginFSOwner::CLegacyPluginFSOwner(
        CLegacyPluginDataOwner& pluginDataOwner, int builtForVersion)
        : PluginDataOwner(pluginDataOwner), BuiltForVersion(builtForVersion)
    {
    }

    CLegacyPluginFSInterface* CLegacyPluginFSOwner::Resolve(
        sdk107::CPluginFSInterfaceAbstract* legacy)
    {
        if (legacy == nullptr)
            return nullptr;
        auto found = Wrappers.find(legacy);
        if (found != Wrappers.end())
            return found->second.get();
        try
        {
            auto wrapper = std::make_unique<CLegacyPluginFSInterface>(
                *legacy, PluginDataOwner, BuiltForVersion);
            CLegacyPluginFSInterface* result = wrapper.get();
            const auto inserted = Wrappers.emplace(legacy, std::move(wrapper));
            if (!inserted.second)
                return inserted.first->second.get();
            try
            {
                const auto reverse = LegacyByWrapper.emplace(result, legacy);
                if (!reverse.second)
                {
                    Wrappers.erase(inserted.first);
                    SetLastError(ERROR_INVALID_DATA);
                    return nullptr;
                }
            }
            catch (...)
            {
                Wrappers.erase(inserted.first);
                throw;
            }
            return result;
        }
        catch (const std::bad_alloc&)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return nullptr;
        }
        catch (const std::length_error&)
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return nullptr;
        }
        catch (...)
        {
            SetLastError(ERROR_INVALID_DATA);
            return nullptr;
        }
    }

    sdk107::CPluginFSInterfaceAbstract* CLegacyPluginFSOwner::FindLegacy(
        const ::CPluginFSInterfaceAbstract* wide) const
    {
        const auto found = LegacyByWrapper.find(wide);
        return found != LegacyByWrapper.end() ? found->second : nullptr;
    }

    sdk107::CPluginFSInterfaceAbstract* CLegacyPluginFSOwner::Retire(
        ::CPluginFSInterfaceAbstract* wide)
    {
        const auto found = LegacyByWrapper.find(wide);
        if (found == LegacyByWrapper.end())
            return nullptr;
        sdk107::CPluginFSInterfaceAbstract* legacy = found->second;
        LegacyByWrapper.erase(found);
        Wrappers.erase(legacy);
        return legacy;
    }

    CLegacyPluginInterfaceForFS::CLegacyPluginInterfaceForFS(
        sdk107::CPluginInterfaceForFSAbstract& legacy,
        CLegacyPluginFSOwner& fsOwner)
        : Legacy(legacy), FSOwner(fsOwner)
    {
    }

    ::CPluginFSInterfaceAbstract* WINAPI
    CLegacyPluginInterfaceForFS::OpenFS(const wchar_t* fsName,
                                        int fsNameIndex)
    {
        std::string storage;
        const char* legacyFSName = nullptr;
        if (!NarrowOptionalText(fsName, storage, legacyFSName))
            return nullptr;
        return FSOwner.Resolve(Legacy.OpenFS(legacyFSName, fsNameIndex));
    }

    void WINAPI CLegacyPluginInterfaceForFS::CloseFS(
        ::CPluginFSInterfaceAbstract* fs)
    {
        sdk107::CPluginFSInterfaceAbstract* legacy = FSOwner.Retire(fs);
        if (legacy != nullptr)
            Legacy.CloseFS(legacy);
    }

    void WINAPI CLegacyPluginInterfaceForFS::ExecuteChangeDriveMenuItem(
        int panel)
    {
        Legacy.ExecuteChangeDriveMenuItem(panel);
    }

    BOOL WINAPI
    CLegacyPluginInterfaceForFS::ChangeDriveMenuItemContextMenu(
        HWND parent, int panel, int x, int y,
        ::CPluginFSInterfaceAbstract* pluginFS,
        const wchar_t* pluginFSName, int pluginFSNameIndex,
        BOOL isDetachedFS, BOOL& refreshMenu, BOOL& closeMenu, int& postCmd,
        void*& postCmdParam)
    {
        sdk107::CPluginFSInterfaceAbstract* legacyFS =
            pluginFS != nullptr ? FSOwner.FindLegacy(pluginFS) : nullptr;
        if (pluginFS != nullptr && legacyFS == nullptr)
            return FALSE;
        std::string storage;
        const char* legacyFSName = nullptr;
        if (!NarrowOptionalText(pluginFSName, storage, legacyFSName))
            return FALSE;
        return Legacy.ChangeDriveMenuItemContextMenu(
            parent, panel, x, y, legacyFS, legacyFSName, pluginFSNameIndex,
            isDetachedFS, refreshMenu, closeMenu, postCmd, postCmdParam);
    }

    void WINAPI CLegacyPluginInterfaceForFS::ExecuteChangeDrivePostCommand(
        int panel, int postCmd, void* postCmdParam)
    {
        Legacy.ExecuteChangeDrivePostCommand(panel, postCmd, postCmdParam);
    }

    void WINAPI CLegacyPluginInterfaceForFS::ExecuteOnFS(
        int panel, ::CPluginFSInterfaceAbstract* pluginFS,
        const wchar_t* pluginFSName, int pluginFSNameIndex,
        ::CFileData& file, int isDir)
    {
        sdk107::CPluginFSInterfaceAbstract* legacyFS =
            FSOwner.FindLegacy(pluginFS);
        if (legacyFS == nullptr)
            return;
        std::string storage;
        const char* legacyFSName = nullptr;
        CLegacyRowMirror row(file);
        if (!row.Valid ||
            !NarrowOptionalText(pluginFSName, storage, legacyFSName))
            return;
        Legacy.ExecuteOnFS(panel, legacyFS, legacyFSName, pluginFSNameIndex,
                           row.Row, isDir);
        row.Commit(file);
    }

    BOOL WINAPI CLegacyPluginInterfaceForFS::DisconnectFS(
        HWND parent, BOOL isInPanel, int panel,
        ::CPluginFSInterfaceAbstract* pluginFS,
        const wchar_t* pluginFSName, int pluginFSNameIndex)
    {
        sdk107::CPluginFSInterfaceAbstract* legacyFS =
            FSOwner.FindLegacy(pluginFS);
        if (legacyFS == nullptr)
            return FALSE;
        std::string storage;
        const char* legacyFSName = nullptr;
        return NarrowOptionalText(pluginFSName, storage, legacyFSName)
                   ? Legacy.DisconnectFS(parent, isInPanel, panel, legacyFS,
                                         legacyFSName, pluginFSNameIndex)
                   : FALSE;
    }

    BOOL WINAPI CLegacyPluginInterfaceForFS::ConvertPathToInternal(
        const wchar_t* fsName, int fsNameIndex,
        CSalamanderStringBuffer* fsUserPart)
    {
        if (fsUserPart == nullptr)
            return FALSE;
        std::wstring livePath;
        if (!sally::plugin_abi::ReadStringBuffer(*fsUserPart, livePath))
            return FALSE;
        std::string storage;
        const char* legacyFSName = nullptr;
        std::array<char, MAX_PATH> legacyPath = {};
        if (!NarrowOptionalText(fsName, storage, legacyFSName) ||
            !NarrowBuffer(livePath.c_str(), legacyPath))
            return FALSE;
        Legacy.ConvertPathToInternal(legacyFSName, fsNameIndex,
                                     legacyPath.data());
        std::wstring converted;
        if (!WidenBuffer(legacyPath, converted))
            return FALSE;
        return sally::plugin_abi::WriteStringBuffer(*fsUserPart, converted)
                   ? TRUE
                   : FALSE;
    }

    BOOL WINAPI CLegacyPluginInterfaceForFS::ConvertPathToExternal(
        const wchar_t* fsName, int fsNameIndex,
        CSalamanderStringBuffer* fsUserPart)
    {
        if (fsUserPart == nullptr)
            return FALSE;
        std::wstring livePath;
        if (!sally::plugin_abi::ReadStringBuffer(*fsUserPart, livePath))
            return FALSE;
        std::string storage;
        const char* legacyFSName = nullptr;
        std::array<char, MAX_PATH> legacyPath = {};
        if (!NarrowOptionalText(fsName, storage, legacyFSName) ||
            !NarrowBuffer(livePath.c_str(), legacyPath))
            return FALSE;
        Legacy.ConvertPathToExternal(legacyFSName, fsNameIndex,
                                     legacyPath.data());
        std::wstring converted;
        if (!WidenBuffer(legacyPath, converted))
            return FALSE;
        return sally::plugin_abi::WriteStringBuffer(*fsUserPart, converted)
                   ? TRUE
                   : FALSE;
    }

    void WINAPI CLegacyPluginInterfaceForFS::EnsureShareExistsOnServer(
        int panel, const wchar_t* server, const wchar_t* share)
    {
        std::string serverStorage;
        std::string shareStorage;
        const char* legacyServer = nullptr;
        const char* legacyShare = nullptr;
        if (NarrowOptionalText(server, serverStorage, legacyServer) &&
            NarrowOptionalText(share, shareStorage, legacyShare))
            Legacy.EnsureShareExistsOnServer(panel, legacyServer,
                                             legacyShare);
    }

    CLegacyPluginInterface::CLegacyPluginInterface(
        sdk107::CPluginInterfaceAbstract& legacy,
        CLegacyPluginDataOwner& pluginDataOwner,
        CLegacyPluginChildResolver& childResolver,
        CLegacyGUIIconListTransfer& iconListTransfer)
        : Legacy(legacy), PluginDataOwner(pluginDataOwner),
          ChildResolver(childResolver), IconListTransfer(iconListTransfer)
    {
    }

    void WINAPI CLegacyPluginInterface::About(HWND parent)
    {
        Legacy.About(parent);
    }

    BOOL WINAPI CLegacyPluginInterface::Release(HWND parent, BOOL force)
    {
        return Legacy.Release(parent, force);
    }

    void WINAPI CLegacyPluginInterface::LoadConfiguration(
        HWND parent, HKEY regKey, ::CSalamanderRegistryAbstract* registry)
    {
        std::optional<CLegacySalamanderRegistry> legacyRegistry;
        if (registry != nullptr &&
            !EmplaceBoundaryObject(legacyRegistry, *registry))
            return;
        Legacy.LoadConfiguration(
            parent, regKey, legacyRegistry ? &*legacyRegistry : nullptr);
    }

    void WINAPI CLegacyPluginInterface::SaveConfiguration(
        HWND parent, HKEY regKey, ::CSalamanderRegistryAbstract* registry)
    {
        std::optional<CLegacySalamanderRegistry> legacyRegistry;
        if (registry != nullptr &&
            !EmplaceBoundaryObject(legacyRegistry, *registry))
            return;
        Legacy.SaveConfiguration(
            parent, regKey, legacyRegistry ? &*legacyRegistry : nullptr);
    }

    void WINAPI CLegacyPluginInterface::Configuration(HWND parent)
    {
        Legacy.Configuration(parent);
    }

    void WINAPI CLegacyPluginInterface::Connect(
        HWND parent, ::CSalamanderConnectAbstract* salamander)
    {
        std::optional<CLegacySalamanderConnect> legacyConnect;
        if (salamander != nullptr &&
            !EmplaceBoundaryObject(legacyConnect, *salamander,
                                   IconListTransfer))
            return;
        Legacy.Connect(parent, legacyConnect ? &*legacyConnect : nullptr);
    }

    void WINAPI CLegacyPluginInterface::ReleasePluginDataInterface(
        ::CPluginDataInterfaceAbstract* pluginData)
    {
        if (pluginData == nullptr)
        {
            Legacy.ReleasePluginDataInterface(nullptr);
            return;
        }
        sdk107::CPluginDataInterfaceAbstract* legacyPluginData =
            PluginDataOwner.Retire(pluginData);
        if (legacyPluginData != nullptr)
            Legacy.ReleasePluginDataInterface(legacyPluginData);
    }

    ::CPluginInterfaceForArchiverAbstract* WINAPI
    CLegacyPluginInterface::GetInterfaceForArchiver()
    {
        sdk107::CPluginInterfaceForArchiverAbstract* legacy =
            Legacy.GetInterfaceForArchiver();
        return legacy != nullptr ? ChildResolver.ResolveArchiver(legacy)
                                 : nullptr;
    }

    ::CPluginInterfaceForViewerAbstract* WINAPI
    CLegacyPluginInterface::GetInterfaceForViewer()
    {
        sdk107::CPluginInterfaceForViewerAbstract* legacy =
            Legacy.GetInterfaceForViewer();
        return legacy != nullptr ? ChildResolver.ResolveViewer(legacy)
                                 : nullptr;
    }

    ::CPluginInterfaceForMenuExtAbstract* WINAPI
    CLegacyPluginInterface::GetInterfaceForMenuExt()
    {
        sdk107::CPluginInterfaceForMenuExtAbstract* legacy =
            Legacy.GetInterfaceForMenuExt();
        return legacy != nullptr ? ChildResolver.ResolveMenuExt(legacy)
                                 : nullptr;
    }

    ::CPluginInterfaceForFSAbstract* WINAPI
    CLegacyPluginInterface::GetInterfaceForFS()
    {
        sdk107::CPluginInterfaceForFSAbstract* legacy =
            Legacy.GetInterfaceForFS();
        return legacy != nullptr ? ChildResolver.ResolveFS(legacy) : nullptr;
    }

    ::CPluginInterfaceForThumbLoaderAbstract* WINAPI
    CLegacyPluginInterface::GetInterfaceForThumbLoader()
    {
        sdk107::CPluginInterfaceForThumbLoaderAbstract* legacy =
            Legacy.GetInterfaceForThumbLoader();
        return legacy != nullptr ? ChildResolver.ResolveThumbLoader(legacy)
                                 : nullptr;
    }

    void WINAPI CLegacyPluginInterface::Event(int event, DWORD param)
    {
        Legacy.Event(event, param);
    }

    void WINAPI CLegacyPluginInterface::ClearHistory(HWND parent)
    {
        Legacy.ClearHistory(parent);
    }

    void WINAPI CLegacyPluginInterface::AcceptChangeOnPathNotification(
        const wchar_t* path, BOOL includingSubdirs)
    {
        std::string pathStorage;
        const char* legacyPath = nullptr;
        if (NarrowOptionalText(path, pathStorage, legacyPath))
            Legacy.AcceptChangeOnPathNotification(legacyPath,
                                                  includingSubdirs);
    }

    void WINAPI CLegacyPluginInterface::PasswordManagerEvent(
        HWND parent, int event)
    {
        Legacy.PasswordManagerEvent(parent, event);
    }

} // namespace sally::compat
