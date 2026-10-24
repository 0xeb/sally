// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#ifdef SALLY_WORKER_CORE_STANDALONE
#include "common/WorkerCoreStandalone.h"
#else
#include "precomp.h"
#endif

#include "IShell.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <stdlib.h>
#include <string.h>
#include <VersionHelpers.h>
#include "ShellUndoPolicy.h"
#include "ShellFileOperationResult.h"

namespace
{
class ScopedComInitialization
{
public:
    ScopedComInitialization()
        : Result(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))
        , MustUninitialize(SUCCEEDED(Result))
    {
    }

    ~ScopedComInitialization()
    {
        if (MustUninitialize)
            CoUninitialize();
    }

    bool IsUsable() const { return SUCCEEDED(Result) || Result == RPC_E_CHANGED_MODE; }
    HRESULT GetResult() const { return Result; }

private:
    HRESULT Result;
    bool MustUninitialize;
};

typedef HRESULT(WINAPI * CreateShellItemFromPathFn)(PCWSTR, IBindCtx*, REFIID, void**);
typedef HRESULT(WINAPI * CreateKnownFolderItemFn)(REFKNOWNFOLDERID, DWORD, PCWSTR,
                                                  REFIID, void**);

HRESULT CreateShellItemFromPath(const wchar_t* path, IShellItem** item)
{
    if (item == NULL)
        return E_POINTER;
    *item = NULL;
    if (path == NULL || path[0] == L'\0')
        return E_INVALIDARG;

    HMODULE shell32 = GetModuleHandleW(L"shell32.dll");
    CreateShellItemFromPathFn createItem = shell32 != NULL
                                               ? reinterpret_cast<CreateShellItemFromPathFn>(
                                                     GetProcAddress(shell32, "SHCreateItemFromParsingName"))
                                               : NULL;
    if (createItem == NULL)
        return HRESULT_FROM_WIN32(ERROR_CALL_NOT_IMPLEMENTED);
    return createItem(path, NULL, IID_IShellItem, reinterpret_cast<void**>(item));
}

HRESULT CreateNetworkFolderItem(IShellItem** item)
{
    if (item == NULL)
        return E_POINTER;
    *item = NULL;

    HMODULE shell32 = GetModuleHandleW(L"shell32.dll");
    CreateKnownFolderItemFn createItem = shell32 != NULL
                                             ? reinterpret_cast<CreateKnownFolderItemFn>(
                                                   GetProcAddress(shell32, "SHCreateItemInKnownFolder"))
                                             : NULL;
    if (createItem == NULL)
        return HRESULT_FROM_WIN32(ERROR_CALL_NOT_IMPLEMENTED);
    return createItem(FOLDERID_NetworkFolder, KF_FLAG_DEFAULT, NULL, IID_IShellItem,
                      reinterpret_cast<void**>(item));
}

ShellResult ResultFromHResult(HRESULT result)
{
    return SUCCEEDED(result) ? ShellResult::Ok()
                             : ShellResult::Error(static_cast<DWORD>(result));
}

struct ShellFolderIds
{
    PIDLIST_ABSOLUTE Folder = nullptr;
    PIDLIST_RELATIVE Child = nullptr;
    ShellFolderIds() = default;
    ShellFolderIds(const ShellFolderIds&) = delete;
    ShellFolderIds& operator=(const ShellFolderIds&) = delete;
    ~ShellFolderIds()
    {
        CoTaskMemFree(Child);
        CoTaskMemFree(Folder);
    }
};

ShellResult ExecuteFolderId(HWND owner, PIDLIST_ABSOLUTE folder)
{
    SHELLEXECUTEINFOW request = {};
    request.cbSize = sizeof(request);
    request.fMask = SEE_MASK_IDLIST | SEE_MASK_FLAG_NO_UI;
    request.hwnd = owner;
    request.lpVerb = L"open";
    request.lpIDList = folder;
    request.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&request))
        return ShellResult::Ok();
    const DWORD error = GetLastError();
    return ShellResult::Error(error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE);
}
} // namespace

class Win32Shell : public IShell
{
    IExplorerBrowser* UndoBrowser = nullptr;
    HWND UndoHost = nullptr;

    HRESULT PrepareUndoView(HWND owner, IShellView** view)
    {
        *view = nullptr;
        if (UndoBrowser == nullptr)
        {
            HRESULT hr = CoCreateInstance(CLSID_ExplorerBrowser, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(&UndoBrowser));
            if (FAILED(hr))
                return hr;
            UndoHost = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1,
                                       owner, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (UndoHost == nullptr)
            {
                hr = HRESULT_FROM_WIN32(GetLastError());
                ReleaseShellUndoState();
                return hr;
            }
            RECT rect = {0, 0, 1, 1};
            FOLDERSETTINGS settings = {FVM_DETAILS, FWF_NOCLIENTEDGE};
            hr = UndoBrowser->SetOptions(EBO_NOTRAVELLOG | EBO_NOPERSISTVIEWSTATE);
            if (SUCCEEDED(hr))
                hr = UndoBrowser->Initialize(UndoHost, &rect, &settings);
            PIDLIST_ABSOLUTE desktop = nullptr;
            if (SUCCEEDED(hr))
                hr = SHGetSpecialFolderLocation(owner, CSIDL_DESKTOPDIRECTORY, &desktop);
            if (SUCCEEDED(hr))
                hr = UndoBrowser->BrowseToIDList(desktop, SBSP_ABSOLUTE);
            CoTaskMemFree(desktop);
            if (FAILED(hr))
            {
                ReleaseShellUndoState();
                return hr;
            }
        }
        return UndoBrowser->GetCurrentView(IID_PPV_ARGS(view));
    }

    HRESULT QueryUndo(HWND owner, IContextMenu** context, HMENU& menu, UINT& command,
                      ShellUndoState& state)
    {
        state = {};
        *context = nullptr;
        menu = nullptr;
        command = 0;
        IShellView* view = nullptr;
        HRESULT hr = PrepareUndoView(owner, &view);
        if (FAILED(hr))
            return hr;
        hr = view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(context));
        view->Release();
        if (FAILED(hr))
            return hr;
        menu = CreatePopupMenu();
        if (menu == nullptr)
            return HRESULT_FROM_WIN32(GetLastError());
        constexpr UINT first = 1;
        hr = (*context)->QueryContextMenu(menu, 0, first, 0x7fff, CMF_NORMAL | CMF_EXPLORE | CMF_DISABLEDVERBS);
        if (FAILED(hr))
            return hr;
        for (int i = 0; i < GetMenuItemCount(menu); ++i)
        {
            MENUITEMINFOW item = {};
            item.cbSize = sizeof(item);
            item.fMask = MIIM_ID | MIIM_STATE | MIIM_FTYPE | MIIM_SUBMENU;
            if (!GetMenuItemInfoW(menu, i, TRUE, &item) || item.hSubMenu != nullptr ||
                (item.fType & MFT_SEPARATOR) != 0 || item.wID < first || item.wID > 0x7fff)
                continue;
            std::vector<wchar_t> verb(128);
            for (;;)
            {
                hr = (*context)->GetCommandString(item.wID - first, GCS_VERBW, nullptr,
                                                  reinterpret_cast<LPSTR>(verb.data()), static_cast<UINT>(verb.size()));
                if (FAILED(hr) || wcslen(verb.data()) < verb.size() - 1)
                    break;
                verb.assign(verb.size() * 2, L'\0');
            }
            if (FAILED(hr) || _wcsicmp(verb.data(), L"undo") != 0)
                continue;
            command = item.wID - first;
            state.available = (item.fState & (MFS_DISABLED | MFS_GRAYED)) == 0;
            item.fMask = MIIM_STRING;
            item.dwTypeData = nullptr;
            item.cch = 0;
            if (GetMenuItemInfoW(menu, i, TRUE, &item) && item.cch != 0)
            {
                std::vector<wchar_t> caption(static_cast<size_t>(item.cch) + 1);
                item.dwTypeData = caption.data();
                item.cch = static_cast<UINT>(caption.size());
                if (GetMenuItemInfoW(menu, i, TRUE, &item))
                    state.caption.assign(caption.data(), item.cch);
            }
            return S_OK;
        }
        return S_OK; // An empty or unavailable history is not an adapter error.
    }

public:
    ShellResult GetShellUndoState(HWND owner, ShellUndoState& state) override
    {
        IContextMenu* context = nullptr;
        HMENU menu = nullptr;
        UINT command = 0;
        const HRESULT hr = QueryUndo(owner, &context, menu, command, state);
        if (menu != nullptr) DestroyMenu(menu);
        if (context != nullptr) context->Release();
        return ResultFromHResult(hr);
    }

    ShellResult UndoLastShellOperation(HWND owner) override
    {
        IContextMenu* context = nullptr;
        HMENU menu = nullptr;
        UINT command = 0;
        ShellUndoState state;
        HRESULT hr = QueryUndo(owner, &context, menu, command, state);
        if (SUCCEEDED(hr) && state.available)
        {
            CMINVOKECOMMANDINFOEX info = {};
            info.cbSize = sizeof(info);
            info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_NOASYNC;
            info.hwnd = owner;
            info.lpVerb = MAKEINTRESOURCEA(command);
            info.lpVerbW = MAKEINTRESOURCEW(command);
            info.nShow = SW_SHOWNORMAL;
            hr = context->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info));
        }
        if (menu != nullptr) DestroyMenu(menu);
        if (context != nullptr) context->Release();
        return ResultFromHResult(hr);
    }

    void ReleaseShellUndoState() override
    {
        if (UndoBrowser != nullptr)
        {
            UndoBrowser->Destroy();
            UndoBrowser->Release();
            UndoBrowser = nullptr;
        }
        if (UndoHost != nullptr)
        {
            DestroyWindow(UndoHost);
            UndoHost = nullptr;
        }
    }

    ShellExecResult Execute(const ShellExecInfo& info) override
    {
        SHELLEXECUTEINFOW sei;
        memset(&sei, 0, sizeof(sei));
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_FLAG_DDEWAIT | SEE_MASK_NOCLOSEPROCESS;
        sei.hwnd = info.hwnd;
        sei.lpVerb = info.verb;
        sei.lpFile = info.file;
        sei.lpParameters = info.parameters;
        sei.lpDirectory = info.directory;
        sei.nShow = info.showCommand;

        if (ShellExecuteExW(&sei))
        {
            // Close the process handle if one was returned
            if (sei.hProcess)
                CloseHandle(sei.hProcess);
            return ShellExecResult::Ok(sei.hInstApp);
        }

        return ShellExecResult::Error(GetLastError());
    }

    ShellResult FileOperation(ShellFileOp operation,
                              const std::vector<std::wstring>& sourcePaths,
                              const std::wstring& destPath,
                              DWORD flags,
                              HWND hwnd) override
    {
        if (sourcePaths.empty())
            return ShellResult::Error(ERROR_INVALID_PARAMETER);

        ScopedComInitialization com;
        if (!com.IsUsable())
            return ResultFromHResult(com.GetResult());

        IFileOperation* fileOperation = NULL;
        HRESULT hr = CoCreateInstance(CLSID_FileOperation, NULL, CLSCTX_INPROC_SERVER,
                                      IID_IFileOperation,
                                      reinterpret_cast<void**>(&fileOperation));
        if (FAILED(hr) || fileOperation == NULL)
            return ResultFromHResult(FAILED(hr) ? hr : E_NOINTERFACE);

        if ((flags & FOF_ALLOWUNDO) != 0 && IsWindows8OrGreater())
        {
            flags |= FOFX_ADDUNDORECORD;
            if (operation == ShellFileOp::Delete)
                flags |= FOFX_RECYCLEONDELETE;
        }
        hr = fileOperation->SetOperationFlags(flags);
        if (SUCCEEDED(hr) && hwnd != NULL)
            hr = fileOperation->SetOwnerWindow(hwnd);

        IShellItem* destination = NULL;
        if (SUCCEEDED(hr) && operation != ShellFileOp::Delete &&
            operation != ShellFileOp::Rename)
        {
            hr = CreateShellItemFromPath(destPath.c_str(), &destination);
        }

        // One source that will not resolve must not cancel the sources that did.
        // SHFileOperationW, which this replaced, took the whole double-NUL list into the
        // shell and reported per item; breaking out of the loop here instead meant a single
        // stale or unparsable name silently discarded the entire batch - PerformOperations
        // was never reached, so the shell showed nothing either. Remember the first such
        // failure, queue everything that does resolve, and report it only if the operation
        // itself had nothing to say.
        HRESULT firstItemError = S_OK;
        size_t queued = 0;
        for (size_t i = 0; SUCCEEDED(hr) && i < sourcePaths.size(); ++i)
        {
            IShellItem* source = NULL;
            const HRESULT itemResult = CreateShellItemFromPath(sourcePaths[i].c_str(), &source);
            if (FAILED(itemResult) || source == NULL)
            {
                if (SUCCEEDED(firstItemError))
                    firstItemError = FAILED(itemResult) ? itemResult : E_FAIL;
                continue;
            }

            switch (operation)
            {
            case ShellFileOp::Copy:
                hr = fileOperation->CopyItem(source, destination, NULL, NULL);
                break;
            case ShellFileOp::Move:
                hr = fileOperation->MoveItem(source, destination, NULL, NULL);
                break;
            case ShellFileOp::Delete:
                hr = fileOperation->DeleteItem(source, NULL);
                break;
            case ShellFileOp::Rename:
            {
                const size_t separator = destPath.find_last_of(L"\\/");
                const wchar_t* newName = destPath.c_str() +
                                         (separator == std::wstring::npos ? 0 : separator + 1);
                hr = newName[0] != L'\0' ? fileOperation->RenameItem(source, newName, NULL)
                                         : E_INVALIDARG;
                break;
            }
            default:
                hr = E_INVALIDARG;
                break;
            }
            if (SUCCEEDED(hr))
                queued++;
            source->Release();
        }

        if (destination != NULL)
            destination->Release();
        if (SUCCEEDED(hr) && queued != 0)
            hr = fileOperation->PerformOperations();

        BOOL aborted = FALSE;
        if (SUCCEEDED(hr) && queued != 0)
            hr = fileOperation->GetAnyOperationsAborted(&aborted);
        fileOperation->Release();

        return sally::shell::FileOperationResult(hr, aborted != FALSE, firstItemError);
    }

    ShellResult GetFileInfo(const wchar_t* path,
                            DWORD attributes,
                            SHFILEINFOW& info,
                            UINT flags) override
    {
        memset(&info, 0, sizeof(info));
        DWORD_PTR result = SHGetFileInfoW(path, attributes, &info, sizeof(info), flags);

        if (result == 0 && !(flags & SHGFI_SYSICONINDEX))
            return ShellResult::Error(GetLastError());

        return ShellResult::Ok();
    }

    ShellResult PickFolder(const FolderPickerOptions& options,
                           std::wstring& selectedPath) override
    {
        ScopedComInitialization com;
        if (!com.IsUsable())
            return ResultFromHResult(com.GetResult());

        IFileOpenDialog* dialog = NULL;
        HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                      IID_IFileOpenDialog,
                                      reinterpret_cast<void**>(&dialog));
        if (FAILED(hr) || dialog == NULL)
            return ResultFromHResult(FAILED(hr) ? hr : E_NOINTERFACE);

        FILEOPENDIALOGOPTIONS dialogOptions = 0;
        hr = dialog->GetOptions(&dialogOptions);
        if (SUCCEEDED(hr))
            hr = dialog->SetOptions(dialogOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                                    FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
        if (SUCCEEDED(hr) && options.title != NULL && options.title[0] != L'\0')
            hr = dialog->SetTitle(options.title);

        if (SUCCEEDED(hr) && options.instruction != NULL && options.instruction[0] != L'\0')
        {
            IFileDialogCustomize* customize = NULL;
            if (SUCCEEDED(dialog->QueryInterface(IID_IFileDialogCustomize,
                                                 reinterpret_cast<void**>(&customize))) &&
                customize != NULL)
            {
                (void)customize->AddText(1000, options.instruction);
                customize->Release();
            }
        }

        IShellItem* initialFolder = NULL;
        if (SUCCEEDED(hr) && options.initialDirectory != NULL &&
            options.initialDirectory[0] != L'\0' &&
            (!options.networkOnly ||
             (options.initialDirectory[0] == L'\\' && options.initialDirectory[1] == L'\\')))
        {
            (void)CreateShellItemFromPath(options.initialDirectory, &initialFolder);
        }
        if (SUCCEEDED(hr) && initialFolder == NULL && options.networkOnly)
            (void)CreateNetworkFolderItem(&initialFolder);
        if (SUCCEEDED(hr) && initialFolder != NULL)
        {
            hr = dialog->SetFolder(initialFolder);
            initialFolder->Release();
        }

        if (SUCCEEDED(hr))
            hr = dialog->Show(options.owner);

        IShellItem* selectedItem = NULL;
        if (SUCCEEDED(hr))
            hr = dialog->GetResult(&selectedItem);

        PWSTR allocatedPath = NULL;
        if (SUCCEEDED(hr) && selectedItem != NULL)
            hr = selectedItem->GetDisplayName(SIGDN_FILESYSPATH, &allocatedPath);
        if (selectedItem != NULL)
            selectedItem->Release();
        dialog->Release();

        if (FAILED(hr) || allocatedPath == NULL)
        {
            if (allocatedPath != NULL)
                CoTaskMemFree(allocatedPath);
            return ResultFromHResult(FAILED(hr) ? hr : E_FAIL);
        }

        std::wstring result(allocatedPath);
        CoTaskMemFree(allocatedPath);
        // No post-hoc UNC check: see FolderPickerOptions::networkOnly. Refusing the selection here
        // turned Select into a silent Cancel for any folder the user reached by leaving Network -
        // which this dialog, unlike the rooted browse tree it replaced, lets them do.
        selectedPath.swap(result);
        return ShellResult::Ok();
    }

    ShellResult GetKnownFolderPath(const GUID& folderId, std::wstring& path) override
    {
        typedef HRESULT(WINAPI * GetKnownFolderPathFn)(REFKNOWNFOLDERID, DWORD, HANDLE, PWSTR*);
        HMODULE shell32 = GetModuleHandleW(L"shell32.dll");
        GetKnownFolderPathFn getKnownFolderPath = shell32 != NULL
                                                       ? reinterpret_cast<GetKnownFolderPathFn>(
                                                             GetProcAddress(shell32, "SHGetKnownFolderPath"))
                                                       : NULL;
        if (getKnownFolderPath == NULL)
            return ShellResult::Error(ERROR_CALL_NOT_IMPLEMENTED);

        PWSTR allocatedPath = NULL;
        HRESULT hr = getKnownFolderPath(folderId, 0, NULL, &allocatedPath);
        if (FAILED(hr) || allocatedPath == NULL)
        {
            if (allocatedPath != NULL)
                CoTaskMemFree(allocatedPath);
            return ShellResult::Error(FAILED(hr) ? static_cast<DWORD>(hr) : ERROR_PATH_NOT_FOUND);
        }

        std::wstring resolved(allocatedPath);
        CoTaskMemFree(allocatedPath);
        path.swap(resolved);
        return ShellResult::Ok();
    }

    ShellResult GetFileSystemPathFromIdList(LPCITEMIDLIST itemId, std::wstring& path) override
    {
        if (itemId == NULL)
            return ShellResult::Error(ERROR_INVALID_PARAMETER);

        typedef HRESULT(WINAPI * CreateItemFromIdListFn)(PCIDLIST_ABSOLUTE, REFIID, void**);
        HMODULE shell32 = GetModuleHandleW(L"shell32.dll");
        CreateItemFromIdListFn createItem = shell32 != NULL
                                                    ? reinterpret_cast<CreateItemFromIdListFn>(
                                                          GetProcAddress(shell32, "SHCreateItemFromIDList"))
                                                    : NULL;
        if (createItem == NULL)
            return ShellResult::Error(ERROR_CALL_NOT_IMPLEMENTED);

        IShellItem* shellItem = NULL;
        HRESULT hr = createItem(reinterpret_cast<PCIDLIST_ABSOLUTE>(itemId), IID_IShellItem,
                                reinterpret_cast<void**>(&shellItem));
        if (FAILED(hr) || shellItem == NULL)
            return ShellResult::Error(FAILED(hr) ? static_cast<DWORD>(hr) : ERROR_PATH_NOT_FOUND);

        PWSTR allocatedPath = NULL;
        hr = shellItem->GetDisplayName(SIGDN_FILESYSPATH, &allocatedPath);
        shellItem->Release();
        if (FAILED(hr) || allocatedPath == NULL)
        {
            if (allocatedPath != NULL)
                CoTaskMemFree(allocatedPath);
            return ShellResult::Error(FAILED(hr) ? static_cast<DWORD>(hr) : ERROR_PATH_NOT_FOUND);
        }

        std::wstring resolved(allocatedPath);
        CoTaskMemFree(allocatedPath);
        path.swap(resolved);
        return ShellResult::Ok();
    }

    ShellResult OpenSpecialFolder(int folderId, HWND owner) override
    {
        ScopedComInitialization com;
        if (!com.IsUsable())
            return ResultFromHResult(com.GetResult());
        ShellFolderIds ids;
        const HRESULT result = SHGetSpecialFolderLocation(NULL, folderId, &ids.Folder);
        if (FAILED(result) || ids.Folder == nullptr)
            return ResultFromHResult(FAILED(result) ? result : E_FAIL);
        return ExecuteFolderId(owner, ids.Folder);
    }

    ShellResult OpenFolderAndSelectItem(const wchar_t* directory, const wchar_t* item, HWND owner) override
    {
        if (directory == nullptr || *directory == 0)
            return ShellResult::Error(ERROR_INVALID_PARAMETER);
        ScopedComInitialization com;
        if (!com.IsUsable())
            return ResultFromHResult(com.GetResult());
        ShellFolderIds ids;
        const HRESULT parsed = SHParseDisplayName(directory, NULL, &ids.Folder, 0, NULL);
        if (FAILED(parsed) || ids.Folder == nullptr)
            return ResultFromHResult(FAILED(parsed) ? parsed : E_FAIL);

        if (item != nullptr && *item != 0)
        {
            // Keep this dynamically resolved, matching the historical import floor.
            typedef HRESULT(WINAPI * OpenAndSelectFn)(PCIDLIST_ABSOLUTE, UINT, PCUITEMID_CHILD_ARRAY, DWORD);
            const HMODULE shell32 = GetModuleHandleW(L"shell32.dll");
            const auto select = shell32 != nullptr
                                    ? reinterpret_cast<OpenAndSelectFn>(GetProcAddress(shell32, "SHOpenFolderAndSelectItems"))
                                    : nullptr;
            if (select != nullptr)
            {
                std::wstring mutableItem(item);
                IShellFolder* desktop = nullptr;
                IShellFolder* folder = nullptr;
                HRESULT result = SHGetDesktopFolder(&desktop);
                if (SUCCEEDED(result) && desktop != nullptr)
                    result = desktop->BindToObject(ids.Folder, NULL, IID_IShellFolder, reinterpret_cast<void**>(&folder));
                if (desktop != nullptr)
                    desktop->Release();
                if (SUCCEEDED(result) && folder != nullptr)
                {
                    ULONG eaten = 0;
                    result = folder->ParseDisplayName(NULL, NULL, mutableItem.data(), &eaten, &ids.Child, NULL);
                    folder->Release();
                    if (SUCCEEDED(result) && ids.Child != nullptr)
                    {
                        PCUITEMID_CHILD children[] = {reinterpret_cast<PCUITEMID_CHILD>(ids.Child)};
                        if (SUCCEEDED(select(ids.Folder, 1, children, 0)))
                            return ShellResult::Ok();
                    }
                }
                else if (folder != nullptr)
                    folder->Release();
            }
        }
        return ExecuteFolderId(owner, ids.Folder);
    }

    ShellResult MoveToRecycleBin(const std::vector<std::wstring>& paths,
                                 HWND parentWnd = NULL) override
    {
        if (paths.empty())
            return ShellResult::Ok();
        return FileOperation(ShellFileOp::Delete, paths, std::wstring(),
                             FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI |
                                 FOF_SILENT | FOF_NOCONFIRMMKDIR,
                             parentWnd);
    }
};

// Global instance
static Win32Shell g_win32Shell;
IShell* gShell = &g_win32Shell;

IShell* GetWin32Shell()
{
    return &g_win32Shell;
}
