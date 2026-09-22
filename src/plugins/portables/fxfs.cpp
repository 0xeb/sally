// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
	Salamander Plugin Development Framework
	
	Copyright (c) 2015 Milan Kase <manison@manison.cz>
	Copyright (c) 2015 Open Salamander Authors
	
	fxfs.cpp
	Contains classes for developing Salamander file system plugins.
*/

#include "precomp.h"
#include "fxfs.h"
#include "fx_lang.rh"
#include "unicode/helpers.h" // WideToAnsi

namespace Fx
{

    const CFxPath::XCHAR CFxStandardPathTraits::RootPath[] = {CFxStandardPathTraits::PathSeparator, 0};

    void WINAPI FxPathAltSepToSepAndRemoveDups(
        CFxPath::XCHAR* path,
        CFxPath::XCHAR sep,
        CFxPath::XCHAR altSep)
    {
        CFxPath::XCHAR* s = path;
        while (*s != L'\0')
        {
            if (*s == altSep)
            {
                *s = sep;
            }

            if (*s == sep && s > path && *(s - 1) == sep)
            {
                memmove(s, s + 1, (wcslen(s + 1) + 1) * sizeof(wchar_t));
                --s;
            }

            ++s;
        }
    }

    ////////////////////////////////////////////////////////////////////////////////
    // CFxPluginInterfaceForFS

    CFxPluginInterfaceForFS::CFxPluginInterfaceForFS(CFxPluginInterface& owner)
        : m_owner(owner)
    {
    }

    CFxPluginInterfaceForFS::~CFxPluginInterfaceForFS()
    {
    }

    void WINAPI CFxPluginInterfaceForFS::ExecuteChangeDriveMenuItem(int panel)
    {
        CALL_STACK_MESSAGE2("CFxPluginInterfaceForFS::ExecuteChangeDriveMenuItem(%d)", panel);

        BOOL changeRes;
        int failReason;

        changeRes = SalamanderGeneral->ChangePanelPathToPluginFS(
            panel,
            GetAssignedFSName(),
            L"",
            &failReason);
    }

    BOOL WINAPI CFxPluginInterfaceForFS::ChangeDriveMenuItemContextMenu(
        HWND parent,
        int panel,
        int x,
        int y,
        CPluginFSInterfaceAbstract* pluginFS,
        const wchar_t* pluginFSName,
        int pluginFSNameIndex,
        BOOL isDetachedFS,
        BOOL& refreshMenu,
        BOOL& closeMenu,
        int& postCmd,
        void*& postCmdParam)
    {
        return FALSE;
    }

    void WINAPI CFxPluginInterfaceForFS::ExecuteChangeDrivePostCommand(int panel, int postCmd, void* postCmdParam)
    {
    }

    void WINAPI CFxPluginInterfaceForFS::ExecuteOnFS(
        int panel,
        CPluginFSInterfaceAbstract* pluginFS,
        const wchar_t* pluginFSName,
        int pluginFSNameIndex,
        CFileData& file,
        int isDir)
    {
        // Redirect call to the file system.
        auto* fxPluginFS = static_cast<CFxPluginFSInterface*>(pluginFS);
        fxPluginFS->ExecuteOnFS(file, isDir);
    }

    BOOL WINAPI CFxPluginInterfaceForFS::DisconnectFS(
        HWND parent,
        BOOL isInPanel,
        int panel,
        CPluginFSInterfaceAbstract* pluginFS,
        const wchar_t* pluginFSName,
        int pluginFSNameIndex)
    {
        BOOL ret = FALSE;

        CALL_STACK_MESSAGE5("CFxPluginInterfaceForFS::DisconnectFS(, %d, %d, , %ls, %d)",
                            isInPanel, panel, pluginFSName, pluginFSNameIndex);

        auto* fxPluginFS = static_cast<CFxPluginFSInterface*>(pluginFS);
        fxPluginFS->m_calledFromDisconnectDialog = true;

        if (isInPanel)
        {
            SalamanderGeneral->DisconnectFSFromPanel(parent, panel);
            ret = SalamanderGeneral->GetPanelPluginFS(panel) != pluginFS;
        }
        else
        {
            ret = SalamanderGeneral->CloseDetachedFS(parent, pluginFS);
        }

        if (!ret)
        {
            fxPluginFS->m_calledFromDisconnectDialog = false;
        }

        return ret;
    }

    BOOL WINAPI CFxPluginInterfaceForFS::ConvertPathToInternal(
        const wchar_t* fsName,
        int fsNameIndex,
        CSalamanderStringBuffer* fsUserPart)
    {
        return fsUserPart != NULL &&
               sally::plugin_abi::IsValidStringBuffer(*fsUserPart);
    }

    BOOL WINAPI CFxPluginInterfaceForFS::ConvertPathToExternal(
        const wchar_t* fsName,
        int fsNameIndex,
        CSalamanderStringBuffer* fsUserPart)
    {
        return fsUserPart != NULL &&
               sally::plugin_abi::IsValidStringBuffer(*fsUserPart);
    }

    void WINAPI CFxPluginInterfaceForFS::EnsureShareExistsOnServer(int panel, const wchar_t* server, const wchar_t* share)
    {
    }

    ////////////////////////////////////////////////////////////////////////////////
    // CFxPluginFSInterface

    CFxPluginFSInterface::CFxPluginFSInterface(CFxPluginInterfaceForFS& owner)
        : m_owner(owner),
          m_calledFromDisconnectDialog(false),
          m_currentPath(nullptr),
          m_currentPathEnumerator(nullptr),
          m_lastParentItemTime(FILETIME_Nul)
    {
    }

    CFxPluginFSInterface::~CFxPluginFSInterface()
    {
        delete m_currentPath;

        if (m_currentPathEnumerator != nullptr)
        {
            m_currentPathEnumerator->Release();
        }
    }

    BOOL WINAPI CFxPluginFSInterface::IsCurrentPath(int currentFSNameIndex, int fsNameIndex, const wchar_t* userPart)
    {
        BOOL result = (currentFSNameIndex == fsNameIndex) &&
                      SalamanderGeneral->IsTheSamePath(
                          m_currentPath != nullptr ? m_currentPath->GetString() : L"", userPart);
        return result;
    }

    BOOL WINAPI CFxPluginFSInterface::IsOurPath(int currentFSNameIndex, int fsNameIndex, const wchar_t* userPart)
    {
        UNREFERENCED_PARAMETER(currentFSNameIndex);
        UNREFERENCED_PARAMETER(fsNameIndex);
        UNREFERENCED_PARAMETER(userPart);

        // It's always our path.
        return TRUE;
    }

    BOOL WINAPI CFxPluginFSInterface::GetRootPath(CSalamanderStringBuffer* userPart)
    {
        // Ineffective default implementation, if possible override in your descendant.
        CFxPath* root = CreatePath(L"");
        root->SetRoot();
        const BOOL copied = userPart != nullptr &&
                            sally::plugin_abi::WriteStringBuffer(
                                *userPart, std::wstring(root->GetString()));
        delete root;
        return copied;
    }

    BOOL WINAPI CFxPluginFSInterface::GetCurrentPath(CSalamanderStringBuffer* userPart)
    {
        return userPart != nullptr &&
               sally::plugin_abi::WriteStringBuffer(
                   *userPart, std::wstring(m_currentPath != nullptr ?
                                               m_currentPath->GetString() :
                                               L""));
    }

    BOOL WINAPI CFxPluginFSInterface::ChangePath(
        int currentFSNameIndex,
        CSalamanderStringBuffer* fsName,
        int fsNameIndex,
        const wchar_t* userPart,
        CSalamanderStringBuffer* cutFileName,
        BOOL* pathWasCut,
        BOOL forceRefresh,
        int mode)
    {
        // Changes the current path in this FS to the path specified through 'fsName' and 'userPart'
        // (either exactly or to the closest accessible subpath of 'userPart'—see the value of
        // 'mode'). If the path is shortened because it points to a file (it is enough to suspect
        // that it might be a file—the file existence is verified after listing the path and a
        // message is shown to the user if the file is missing) and 'cutFileName' is not NULL (only
        // possible in 'mode' 3), the buffer 'cutFileName' receives the
        // name of that file (without the path). Otherwise, the buffer 'cutFileName' receives an
        // empty string. 'currentFSNameIndex' is the index of the current FS name; 'fsName' contains,
        // on input, the FS name from the path that belongs to
        // this plugin (it does not have to match the current FS name of this object as long as
        // IsOurPath() returns TRUE for it) and, on output, the current FS name of this object (it
        // must belong to this plugin). 'fsNameIndex' is the index of the FS name 'fsName' inside the
        // plugin (to make it easier to detect which FS name is used). If 'pathWasCut' is not NULL,
        // TRUE is returned in it when the path was shortened. Salamander uses 'cutFileName' and
        // 'pathWasCut' with the Change Directory command (Shift+F7) when a file name is entered—it
        // selects that file. If 'forceRefresh' is TRUE, this is a hard refresh (Ctrl+R) and the
        // plugin should change the path without using cached information (it is necessary to verify
        // that the new path exists). 'mode' controls how the path is changed:
        //   1 (refresh path) - shortens the path if needed; does not report path non-existence (cut
        //                      silently), but reports files instead of paths, path inaccessibility,
        //                      and other errors
        //   2 (ChangePanelPathToPluginFS call, back/forward in history, etc.) - shortens the path if
        //                      needed; reports all path errors (file instead of path, non-existence,
        //                      inaccessibility, and others)
        //   3 (change-dir command) - shortens the path only when it points to a file or the path
        //                      cannot be listed (ListCurrentPath returns FALSE for it); does not
        //                      report a file instead of a path (cut silently and return the file
        //                      name), but reports all other path errors (non-existence,
        //                      inaccessibility, and others)
        // If 'mode' is 1 or 2, the function returns FALSE only when no path is accessible on this
        // FS (e.g. when the connection is down). If 'mode' is 3, it returns FALSE when the desired
        // path or file is not accessible (the path is shortened only if it points to a file). If
        // opening the FS is time-consuming (e.g. connecting to an FTP server) and 'mode' is 3, the
        // behavior can be adjusted like for archives—shorten the path if necessary and return FALSE
        // only when no path is accessible on the FS; error reporting remains unchanged.

        HRESULT hr;
        std::wstring fsNameValue;
        if (fsName == nullptr ||
            !sally::plugin_abi::ReadStringBuffer(*fsName, fsNameValue))
            return FALSE;
        (void)fsNameValue;

        if (pathWasCut != nullptr)
        {
            *pathWasCut = FALSE;
        }

        if (cutFileName != nullptr &&
            !sally::plugin_abi::WriteStringBuffer(*cutFileName, std::wstring()))
            return FALSE;

        CFxPath* newPath = CreatePath(userPart);
        hr = newPath->Canonicalize();
        if (FAILED(hr))
        {
            ShowChangePathError(newPath->GetString(), hr);
        }
        else
        {
            // Path is syntactically correct, now check if the path
            // physically exists on the file system.
            CFxItemEnumerator* enumerator = nullptr;
            int cutIndex = -1;
            hr = GetEnumeratorForPath(enumerator, *newPath, cutIndex, !!forceRefresh);
            HRESULT hrShowError = hr;
            CFxPath* showErrorPath = nullptr;

            if (hr == FX_E_PATH_IS_FILE && enumerator != nullptr && cutIndex >= newPath->GetLength() && mode == 3)
            {
                // The last path component is a file and we are in Shift+F7
                // mode. Trim the file specification.
                std::wstring cutComponent;
                if (newPath->CutLastComponent(cutComponent))
                {
                    if (cutFileName != nullptr &&
                        !sally::plugin_abi::WriteStringBuffer(*cutFileName, cutComponent))
                    {
                        delete newPath;
                        return FALSE;
                    }
                    *pathWasCut = TRUE;
                    hr = hrShowError = S_OK;
                }
            }
            else if (FAILED(hr) && (mode == 1 || mode == 2) && enumerator != nullptr)
            {
                // Use accessible path.
                showErrorPath = CreatePath(*newPath);
                newPath->Truncate(cutIndex);
                if (newPath->CutLastComponent())
                {
                    // Some path is accessible, change current path to the accessible one.
                    hr = S_OK;

                    if (hrShowError == FX_E_PATH_NOT_FOUND && mode == 1)
                    {
                        // During refresh (mode == 1) do not display
                        // non-existent paths, just change to the accessible
                        // path silently.
                        hrShowError = S_OK;
                    }
                }
            }

            if (FAILED(hrShowError))
            {
                if (showErrorPath == nullptr)
                {
                    showErrorPath = CreatePath(newPath->Left(cutIndex));
                }
                ShowChangePathError(showErrorPath->GetString(), hrShowError);
            }

            delete showErrorPath;

            if (SUCCEEDED(hr))
            {
                SetCurrentPathEnumerator(newPath, enumerator);
                newPath = nullptr; // Prevent double delete.
            }
            else
            {
                if (enumerator != nullptr)
                {
                    enumerator->Release();
                }
            }
        }

        delete newPath;
        return SUCCEEDED(hr);
    }

    BOOL WINAPI CFxPluginFSInterface::ListCurrentPath(
        CSalamanderDirectoryAbstract* dir,
        CPluginDataInterfaceAbstract*& pluginData,
        int& iconsType,
        BOOL forceRefresh)
    {
        iconsType = pitSimple;

        CFxItemEnumerator* enumerator;
        HRESULT hr;
        hr = GetEnumeratorForCurrentPath(enumerator, !!forceRefresh);
        if (FAILED(hr))
        {
            TRACE_E("GetEnumerator failed with HRESULT 0x" << std::hex << hr);
            return FALSE;
        }

        auto fxPluginData = this->CreatePluginData(enumerator);
        pluginData = fxPluginData;
        bool assignItemForFiles = false;
        bool assignItemForDirs = false;
        if (pluginData != nullptr)
        {
            assignItemForFiles = !!pluginData->CallReleaseForFiles();
            assignItemForDirs = !!pluginData->CallReleaseForDirs();
            iconsType = fxPluginData->GetIconsType();
        }

        DWORD validData = enumerator->GetValidData();
        dir->SetValidData(validData);

        IFxItemConverter* converter = GetConverter();
        _ASSERTE(converter != nullptr);

        bool upDirAdded = false;

        while ((hr = enumerator->MoveNext()) == S_OK)
        {
            CFxItem* item = enumerator->GetCurrent();
            _ASSERTE(item != nullptr);
            CFileData fileData;
            bool fileDataIsDir;
            if (converter->ConvertItemToFileData(item, validData, fileData, fileDataIsDir))
            {
                bool addedOk;

                if (fileDataIsDir)
                {
                    if (assignItemForDirs)
                    {
                        // Tranfer ownership of the FSItem to the PluginData.
                        fileData.PluginData = reinterpret_cast<DWORD_PTR>(item);
                        item->AddRef();
                    }
                    addedOk = !!dir->AddDir(nullptr, fileData, nullptr);
                    if (addedOk && !upDirAdded)
                    {
                        upDirAdded = fileData.NameLen == 2 &&
                                     fileData.Name[0] == '.' &&
                                     fileData.Name[1] == '.';
                    }
                }
                else
                {
                    if (assignItemForFiles)
                    {
                        // Tranfer ownership of the FSItem to the PluginData.
                        fileData.PluginData = reinterpret_cast<DWORD_PTR>(item);
                        item->AddRef();
                    }
                    addedOk = !!dir->AddFile(nullptr, fileData, nullptr);
                }

                item->Release();

                _ASSERTE(addedOk);
                if (!addedOk)
                {
                    converter->FreeFileData(fileData);
                }
            }
        }

        enumerator->Release();

        if (SUCCEEDED(hr) && !upDirAdded && IsUpDirNeededForCurrentPath())
        {
            CFileData upDirFileData = {
                0,
            };
            if (GetFileDataForUpDir(upDirFileData))
            {
                _ASSERTE(upDirFileData.NameLen == 2 &&
                         upDirFileData.Name[0] == '.' &&
                         upDirFileData.Name[1] == '.' &&
                         upDirFileData.Name[2] == '\0');
                dir->AddDir(nullptr, upDirFileData, nullptr);
            }
        }

        m_lastParentItemTime = FILETIME_Nul;

        return SUCCEEDED(hr);
    }

    CFxPluginDataInterface* WINAPI CFxPluginFSInterface::CreatePluginData(CFxItemEnumerator* enumerator)
    {
        UNREFERENCED_PARAMETER(enumerator);
        return new CFxPluginFSDataInterface(*this);
    }

    IFxItemConverter* WINAPI CFxPluginFSInterface::GetConverter() const
    {
        return CFxStandardItemConverter::GetInstance();
    }

    HICON WINAPI CFxPluginFSInterface::GetFSIcon(BOOL& destroyIcon)
    {
        destroyIcon = FALSE;
        return m_owner.GetOwner().GetPluginIcon();
    }

    BOOL WINAPI CFxPluginFSInterface::GetFullName(
        CFileData& file, int isDir, CSalamanderStringBuffer* fullName)
    {
        return FALSE;
    }

    BOOL WINAPI CFxPluginFSInterface::GetFullFSPath(
        HWND parent,
        const wchar_t* fsName,
        CSalamanderStringBuffer* path,
        BOOL& success)
    {
        success = FALSE;
        return FALSE;
    }

    BOOL WINAPI CFxPluginFSInterface::TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason)
    {
        return TRUE;
    }

    void WINAPI CFxPluginFSInterface::Event(int event, DWORD param)
    {
    }

    void WINAPI CFxPluginFSInterface::ReleaseObject(HWND parent)
    {
    }

    BOOL WINAPI CFxPluginFSInterface::GetChangeDriveOrDisconnectItem(
        const wchar_t* fsName,
        wchar_t*& title,
        HICON& icon,
        BOOL& destroyIcon)
    {
        // If you support FS_SERVICE_GETCHANGEDRIVEORDISCONNECTITEM service,
        // then you must override this method in your descendant class and
        // provide your own implementation.
        _ASSERTE(0);
        return FALSE;
    }

    void WINAPI CFxPluginFSInterface::GetDropEffect(
        const wchar_t* srcFSPath,
        const wchar_t* tgtFSPath,
        DWORD allowedEffects,
        DWORD keyState,
        DWORD* dropEffect)
    {
    }

    void WINAPI CFxPluginFSInterface::GetFSFreeSpace(CQuadWord* retValue)
    {
        // If you support FS_SERVICE_GETFREESPACE service, then you must override this
        // method in your descendant class and provide your own implementation.
        _ASSERTE(0);
    }

    BOOL WINAPI CFxPluginFSInterface::GetNextDirectoryLineHotPath(const wchar_t* text, int pathLen, int& offset)
    {
        // If you support FS_SERVICE_GETNEXTDIRLINEHOTPATH service, then you must override this
        // method in your descendant class and provide your own implementation.
        _ASSERTE(0);
        return FALSE;
    }

    BOOL WINAPI CFxPluginFSInterface::CompleteDirectoryLineHotPath(CSalamanderStringBuffer* path)
    {
        // If you support FS_SERVICE_GETNEXTDIRLINEHOTPATH service, then you must override this
        // method in your descendant class and provide your own implementation.
        _ASSERTE(0);
        return FALSE;
    }

    BOOL WINAPI CFxPluginFSInterface::GetPathForMainWindowTitle(const wchar_t* fsName, int mode, CSalamanderStringBuffer* buf)
    {
        // If you support FS_SERVICE_GETPATHFORMAINWNDTITLE service, then you
        // must override this method in your descendant class and provide your
        // own implementation.
        _ASSERTE(0);
        return FALSE;
    }

    void WINAPI CFxPluginFSInterface::ShowInfoDialog(const wchar_t* fsName, HWND parent)
    {
        // If you support FS_SERVICE_SHOWINFO service, then you must override this
        // method in your descendant class and provide your own implementation.
        _ASSERTE(0);
    }

    BOOL WINAPI CFxPluginFSInterface::ExecuteCommandLine(HWND parent, CSalamanderStringBuffer* command, int& selFrom, int& selTo)
    {
        // If you support FS_SERVICE_COMMANDLINE service, then you must override this
        // method in your descendant class and provide your own implementation.
        _ASSERTE(0);
        return FALSE;
    }

    BOOL WINAPI CFxPluginFSInterface::QuickRename(
        const wchar_t* fsName,
        int mode,
        HWND parent,
        CFileData& file,
        BOOL isDir,
        CSalamanderStringBuffer* newName,
        BOOL& cancel)
    {
        // If you support FS_SERVICE_QUICKRENAME service, then you must override this
        // method in your descendant class and provide your own implementation.
        _ASSERTE(0);
        return FALSE;
    }

    void WINAPI CFxPluginFSInterface::AcceptChangeOnPathNotification(
        const wchar_t* fsName,
        const wchar_t* path,
        BOOL includingSubdirs)
    {
        // If you support FS_SERVICE_ACCEPTSCHANGENOTIF service, then you must
        // override this method in your descendant class and provide your own
        // implementation.
        _ASSERTE(0);
    }

    BOOL WINAPI CFxPluginFSInterface::CreateDir(
        const wchar_t* fsName,
        int mode,
        HWND parent,
        CSalamanderStringBuffer* newName,
        BOOL& cancel)
    {
        // If you support FS_SERVICE_CREATEDIR service, then you must override this
        // method in your descendant class and provide your own implementation.
        _ASSERTE(0);
        return FALSE;
    }

    void WINAPI CFxPluginFSInterface::ViewFile(
        const wchar_t* fsName,
        HWND parent,
        CSalamanderForViewFileOnFSAbstract* salamander,
        CFileData& file)
    {
        // If you support FS_SERVICE_VIEWFILE service, then you must override this
        // method in your descendant class and provide your own implementation.
        _ASSERTE(0);
    }

    BOOL WINAPI CFxPluginFSInterface::Delete(
        const wchar_t* fsName,
        int mode,
        HWND parent,
        int panel,
        int selectedFiles,
        int selectedDirs,
        BOOL& cancelOrError)
    {
        // If you support FS_SERVICE_DELETE service, then you must override this
        // method in your descendant class and provide your own implementation.
        _ASSERTE(0);
        return FALSE;
    }

    BOOL WINAPI CFxPluginFSInterface::CopyOrMoveFromFS(
        BOOL copy,
        int mode,
        const wchar_t* fsName,
        HWND parent,
        int panel,
        int selectedFiles,
        int selectedDirs,
        CSalamanderStringBuffer* targetPath,
        BOOL& operationMask,
        BOOL& cancelOrHandlePath,
        HWND dropTarget)
    {
        // If you support FS_SERVICE_COPYFROMFS/MOVEFROMFS service, then you
        // must override this method in your descendant class and provide your
        // own implementation.
        _ASSERTE(0);
        return FALSE;
    }

    BOOL WINAPI CFxPluginFSInterface::CopyOrMoveFromDiskToFS(
        BOOL copy,
        int mode,
        const wchar_t* fsName,
        HWND parent,
        const wchar_t* sourcePath,
        SalEnumSelection2 next,
        void* nextParam,
        int sourceFiles,
        int sourceDirs,
        CSalamanderStringBuffer* targetPath,
        BOOL* invalidPathOrCancel)
    {
        // If you support FS_SERVICE_COPYFROMDISKTOFS/MOVEFROMDISKTOFS service,
        // then you must override this method in your descendant class and
        // provide your own implementation.
        _ASSERTE(0);
        return FALSE;
    }

    BOOL WINAPI CFxPluginFSInterface::ChangeAttributes(
        const wchar_t* fsName,
        HWND parent,
        int panel,
        int selectedFiles,
        int selectedDirs)
    {
        // If you support FS_SERVICE_CHANGEATTRS service, then you must override this
        // method in your descendant class and provide your own implementation.
        _ASSERTE(0);
        return FALSE;
    }

    void WINAPI CFxPluginFSInterface::ShowProperties(
        const wchar_t* fsName,
        HWND parent,
        int panel,
        int selectedFiles,
        int selectedDirs)
    {
        // If you support FS_SERVICE_SHOWPROPERTIES service, then you must override this
        // method in your descendant class and provide your own implementation.
        _ASSERTE(0);
    }

    void WINAPI CFxPluginFSInterface::ContextMenu(
        const wchar_t* fsName,
        HWND parent,
        int menuX,
        int menuY,
        int type,
        int panel,
        int selectedFiles,
        int selectedDirs)
    {
        // If you support FS_SERVICE_CONTEXTMENU service, then you must override this
        // method in your descendant class and provide your own implementation.
        _ASSERTE(0);
    }

    BOOL WINAPI CFxPluginFSInterface::HandleMenuMsg(
        UINT uMsg,
        WPARAM wParam,
        LPARAM lParam,
        LRESULT* plResult)
    {
        return FALSE;
    }

    BOOL WINAPI CFxPluginFSInterface::OpenFindDialog(const wchar_t* fsName, int panel)
    {
        // If you support FS_SERVICE_OPENFINDDLG service, then you must override this
        // method in your descendant class and provide your own implementation.
        _ASSERTE(0);
        return FALSE;
    }

    void WINAPI CFxPluginFSInterface::OpenActiveFolder(const wchar_t* fsName, HWND parent)
    {
        // If you support FS_SERVICE_OPENACTIVEFOLDER service, then you must override this
        // method in your descendant class and provide your own implementation.
        _ASSERTE(0);
    }

    void WINAPI CFxPluginFSInterface::GetAllowedDropEffects(int mode, const wchar_t* tgtFSPath, DWORD* allowedEffects)
    {
    }

    BOOL WINAPI CFxPluginFSInterface::GetNoItemsInPanelText(CSalamanderStringBuffer* textBuf)
    {
        return FALSE;
    }

    void WINAPI CFxPluginFSInterface::ShowSecurityInfo(HWND parent)
    {
        // If you support FS_SERVICE_SHOWSECURITYINFO service, then you must override this
        // method in your descendant class and provide your own implementation.
        _ASSERTE(0);
    }

    void WINAPI CFxPluginFSInterface::ShowChangePathError(PCWSTR path, HRESULT hr)
    {
        CFxString s;
        FxGetErrorDescription(hr, s);
        ShowChangePathError(path, s);
    }

    void WINAPI CFxPluginFSInterface::ShowChangePathError(PCWSTR path, PCWSTR error)
    {
        CFxString title, message;
        title.LoadString(IDS_FX_CHANGEPATHERRORTITLE);
        CFxString fullPath;
        const std::wstring fsName =
            SPLGetPluginFSNameOwned(SalamanderGeneral, 0);
        fullPath = fsName.c_str();
        fullPath.AppendChar(L':');
        fullPath.Append(path);
        message.Format(IDS_FX_CHANGEPATHERRORFMT, fullPath, error);
        SalamanderGeneral->ShowMessageBox(message, title, MSGBOX_ERROR);
    }

    void WINAPI CFxPluginFSInterface::SetCurrentPath(CFxPath* path)
    {
        _ASSERTE(path != nullptr);

        if (m_currentPath != nullptr)
        {
            delete m_currentPath;
        }

        m_currentPath = path;
    }

    void WINAPI CFxPluginFSInterface::SetCurrentPathEnumerator(CFxItemEnumerator* enumerator)
    {
        _ASSERTE(enumerator != nullptr);
        _ASSERTE(m_currentPath != nullptr);

        if (m_currentPathEnumerator != nullptr)
        {
            m_currentPathEnumerator->Release();
        }

        m_currentPathEnumerator = enumerator;
    }

    HRESULT WINAPI CFxPluginFSInterface::GetEnumeratorForPath(
        _Out_ CFxItemEnumerator*& enumerator,
        _Inout_ CFxPath& path,
        _Out_ int& pathCutIndex,
        bool forceRefresh)
    {
        HRESULT hr;
        CFxItemEnumerator* parentEnumerator = nullptr;
        CFxItemEnumerator* childEnumerator;
        int level = 0;
        CFxPathComponentToken pathComponentToken;

        enumerator = nullptr;

        for (;;)
        {
            childEnumerator = nullptr;
            m_lastParentItemTime = FILETIME_Nul;

            bool gotPathComponent = path.GetNextPathComponent(pathComponentToken);

            CFxItem* parentItem = nullptr;
            if (parentEnumerator != nullptr)
            {
                parentItem = parentEnumerator->GetCurrent();
                _ASSERTE(parentItem != nullptr);
            }

            hr = GetChildEnumerator(childEnumerator, parentItem, level, forceRefresh);
            _ASSERTE(SUCCEEDED(hr) || level > 0);

            if (parentEnumerator != nullptr &&
                (parentEnumerator->GetValidData() & (VALID_DATA_DATE | VALID_DATA_TIME | VALID_DATA_PL_DATE | VALID_DATA_PL_TIME)))
            {
                // Parent enumerator can provide date/time info. Remember
                // last write time of the parent item so we have data
                // for the up-dir item.
                FILETIME parentItemTime;
                if (SUCCEEDED(parentItem->GetLastWriteTime(parentItemTime)))
                {
                    m_lastParentItemTime = parentItemTime;
                }
            }

            if (parentItem != nullptr)
            {
                parentItem->Release();
            }

            bool exitLoop = !gotPathComponent || FAILED(hr);

            if (parentEnumerator != nullptr)
            {
                if (childEnumerator == nullptr && exitLoop)
                {
                    // We are exitting, return the last enumerator we have.
                    childEnumerator = parentEnumerator;
                }
                else
                {
                    parentEnumerator->Release();
                }
            }

            if (exitLoop)
            {
                break;
            }

            pathCutIndex = pathComponentToken.GetTokenEnd();

            if (SUCCEEDED(hr))
            {
                bool componentFound = false;

                while (!componentFound && (hr = childEnumerator->MoveNext()) == S_OK)
                {
                    CFxItem* item = childEnumerator->GetCurrent();
                    _ASSERTE(item != nullptr);
                    CFxString name;
                    item->GetName(name);
                    _ASSERTE(!name.IsEmpty());
                    componentFound = pathComponentToken.ComponentEquals(name);
                    if (componentFound)
                    {
                        // Update the path components because
                        // it may differ in letter cases.
                        pathComponentToken.ReplaceComponent(name);

                        // Path components need to be directories.
                        DWORD attrs = item->GetAttributes();
                        if (!(attrs & FILE_ATTRIBUTE_DIRECTORY))
                        {
                            hr = FX_E_PATH_IS_FILE;
                        }
                    }

                    item->Release();
                }

                if (hr == S_FALSE)
                {
                    hr = FX_E_PATH_NOT_FOUND;
                    break;
                }
                else if (FAILED(hr))
                {
                    break;
                }

                parentEnumerator = childEnumerator;
            }
            else
            {
                break;
            }

            ++level;
        }

        _ASSERTE(childEnumerator != nullptr || FAILED(hr));
        enumerator = childEnumerator;

        return hr;
    }

    HRESULT WINAPI CFxPluginFSInterface::GetEnumeratorForCurrentPath(_Out_ CFxItemEnumerator*& enumerator, bool forceRefresh)
    {
        UNREFERENCED_PARAMETER(forceRefresh);

        _ASSERTE(m_currentPathEnumerator != nullptr);

        enumerator = m_currentPathEnumerator;
        enumerator->AddRef();
        enumerator->Reset();

        return S_OK;
    }

    bool WINAPI CFxPluginFSInterface::IsUpDirNeededForCurrentPath()
    {
        // We only want up dirs for non-root paths.
        CFxPath* rootPath = CreatePath(L"");
        rootPath->SetRoot();
        const bool isRoot = m_currentPath != nullptr &&
                            m_currentPath->Equals(rootPath->GetString());
        delete rootPath;
        return !isRoot;
    }

    void WINAPI CFxPluginFSInterface::ExecuteOnFS(CFileData& file, int isDir)
    {
        if (isDir == 2)
        {
            ExecuteUpDir();
        }
        else
        {
            auto item = reinterpret_cast<CFxItem*>(file.PluginData);
            _ASSERTE(item != nullptr);
            ExecuteItem(*item);
        }
    }

    void WINAPI CFxPluginFSInterface::ExecuteUpDir()
    {
        _ASSERTE(m_currentPath != nullptr);
        CFxPath* newPath = CreatePath(m_currentPath->GetString());
        std::wstring cutComponent;
        newPath->CutLastComponent(cutComponent);
        ChangeDirectory(newPath->GetString(), cutComponent.c_str());
        delete newPath;
    }

    void WINAPI CFxPluginFSInterface::ExecuteItem(CFxItem& item)
    {
        DWORD attr = item.GetAttributes();
        if (attr & FILE_ATTRIBUTE_DIRECTORY)
        {
            _ASSERTE(m_currentPath != nullptr);
            CFxPath* newPath = CreatePath(m_currentPath->GetString());
            newPath->IncludeTrailingSeparator();
            CFxString name;
            item.GetName(name);
            newPath->Append(name);
            ChangeDirectory(newPath->GetString());
            delete newPath;
        }
        else
        {
            // TODO:
            ////SalamanderGeneral->SetUserWorkedOnPanelPath(panel);
            item.Execute();
        }
    }

    void WINAPI CFxPluginFSInterface::ChangeDirectory(PCWSTR newPath, PCWSTR focusedName)
    {
        int panel;
        bool gotFSOk = !!SalamanderGeneral->GetPanelWithPluginFS(this, panel);
        _ASSERTE(gotFSOk);

        const std::wstring fsName =
            SPLGetPluginFSNameOwned(SalamanderGeneral, 0);

        SalamanderGeneral->ChangePanelPathToPluginFS(
            panel,
            fsName.c_str(),
            newPath,
            nullptr,
            -1,
            focusedName);
    }

    bool WINAPI CFxPluginFSInterface::GetFileDataForUpDir(_Out_ CFileData& upDirFileData)
    {
        upDirFileData.Name = SalamanderGeneral->DupStr(L"..");
        upDirFileData.NameLen = 2;
        upDirFileData.Ext = upDirFileData.Name + upDirFileData.NameLen;
        upDirFileData.Attr = FILE_ATTRIBUTE_DIRECTORY;

        if (m_lastParentItemTime.dwLowDateTime != 0U && m_lastParentItemTime.dwHighDateTime != 0U)
        {
            // We have time information from the last ChangePath.
            upDirFileData.LastWrite = m_lastParentItemTime;
        }
        else
        {
            // Just use the current time.
            ::GetSystemTimeAsFileTime(&upDirFileData.LastWrite);
        }

        return true;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // CFxPluginFSDataInterface

    CFxPluginFSDataInterface::CFxPluginFSDataInterface(CFxPluginFSInterface& owner)
        : m_owner(owner)
    {
    }

    CFxPluginFSDataInterface::~CFxPluginFSDataInterface()
    {
    }

    void WINAPI CFxPluginFSDataInterface::SetupView(
        BOOL leftPanel,
        CSalamanderViewAbstract* view,
        const wchar_t* archivePath,
        const CFileData* upperDir)
    {
        __super::SetupView(leftPanel, view, archivePath, upperDir);

        view->SetPluginSimpleIconCallback(&CFxPluginFSDataInterface::GetSimpleIconIndex);
    }

    int WINAPI CFxPluginFSDataInterface::GetSimpleIconIndex()
    {
        auto item = reinterpret_cast<CFxItem*>((*CFxPluginDataInterface::s_transferFileData)->PluginData);
        int iconIndex = item->GetSimpleIconIndex();
        return iconIndex;
    }

}; // namespace Fx
