// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
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
} // namespace

class Win32Shell : public IShell
{
public:
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

        if (FAILED(hr))
            return ResultFromHResult(hr);
        if (aborted)
            return ShellResult::Error(ERROR_CANCELLED);
        // Nothing was queued, or something was dropped on the way in: the shell never saw
        // those items, so it cannot have reported them. Say so rather than returning Ok.
        if (FAILED(firstItemError))
            return ResultFromHResult(firstItemError);
        return ShellResult::Ok();
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
