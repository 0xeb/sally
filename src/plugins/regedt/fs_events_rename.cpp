// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "regedt_registry_enum.h"

BOOL ExportStringsForViewerInASCII = TRUE;

namespace
{
class RegistryKeyOwner
{
public:
    explicit RegistryKeyOwner(HKEY key = nullptr) noexcept : Key(key) {}
    ~RegistryKeyOwner() { Close(); }

    void Close() noexcept
    {
        if (Key != nullptr)
        {
            RegCloseKey(Key);
            Key = nullptr;
        }
    }

private:
    HKEY Key;
};

class SafeWaitWindowOwner
{
public:
    ~SafeWaitWindowOwner()
    {
        if (Active)
            SG->DestroySafeWaitWindow();
    }

    void Activate() noexcept { Active = true; }

private:
    bool Active = false;
};
} // namespace

// ****************************************************************************
//
// CPluginFSInterface - second part
//
//

BOOL CPluginFSInterface::TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason)
{
    CALL_STACK_MESSAGE4("CPluginFSInterface::TryCloseOrDetach(%d, %d, %d)",
                        forceClose, canDetach, detach);
    detach = FALSE;
    return TRUE;
}

void CPluginFSInterface::Event(int event, DWORD param)
{
    CALL_STACK_MESSAGE2("CPluginFSInterface::Event(, 0x%X)", param);
}

HICON
CPluginFSInterface::GetFSIcon(BOOL& destroyIcon)
{
    CALL_STACK_MESSAGE2("CPluginFSInterface::GetFSIcon(%d)", destroyIcon);
    return (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(IDI_REGEDT), IMAGE_ICON,
                            16, 16, SG->GetIconLRFlags());
}

BOOL CPluginFSInterface::GetNextDirectoryLineHotPath(const wchar_t* text, int pathLen, int& offset)
{
    CALL_STACK_MESSAGE4("CPluginFSInterface::GetNextDirectoryLineHotPath(%ls, %d, "
                        "%d)",
                        text, pathLen, offset);
    if (text[offset] == L'\\')
        offset++;
    const wchar_t* ret = wmemchr(text + offset, L'\\', pathLen - offset);
    if (ret)
    {
        if (offset == 0)
            offset = (int)(ret - text + 1);
        else
            offset = (int)(ret - text);
        return TRUE;
    }
    else
        return FALSE;
}

BOOL TestKey(int root, const wchar_t* key)
{
    CALL_STACK_MESSAGE2("TestKey(%d, )", root);
    while (1)
    {
        HKEY hKey;
        int res = RegOpenKeyExW(PredefinedHKeys[root].HKey, key, NULL, KEY_READ, &hKey);
        if (res != ERROR_SUCCESS)
        {
            if (res == ERROR_FILE_NOT_FOUND)
            {
                return TRUE;
            }
            if (!RegOperationError(res, IDS_OPEN, IDS_RENAMEKEY, root, key, NULL, NULL))
                return FALSE;
        }
        else
        {
            RegCloseKey(hKey);
            SG->SalMessageBox(GetParent(), LoadStrW(IDS_CANNOTRENAME).c_str(), LoadStrW(IDS_RENAMEKEY).c_str(), MB_OK);
            return FALSE;
        }
    }
    return TRUE;
}

BOOL CPluginFSInterface::QuickRename(const wchar_t* fsName, int mode, HWND parent,
                                     CFileData& file, BOOL isDir,
                                     CSalamanderStringBuffer* newName, BOOL& cancel)
{
    try
    {
        std::wstring value;
        if (newName == NULL || !sally::plugin_abi::ReadStringBuffer(*newName, value))
            return FALSE;
        const BOOL result = QuickRenameOwned(fsName, mode, parent, file, isDir, value, cancel);
        return sally::plugin_abi::WriteStringBuffer(*newName, value) ? result : FALSE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL CPluginFSInterface::QuickRenameOwned(const wchar_t* fsName, int mode, HWND parent,
                                          CFileData& file, BOOL isDir,
                                          std::wstring& newName, BOOL& cancel)
{
    CALL_STACK_MESSAGE5("CPluginFSInterface::QuickRename(%ls, %d, , , %d, , %d)",
                        fsName, mode, isDir, cancel);
    PARENT(parent);

    if (mode == 2)
    {
        cancel = FALSE;
        return FALSE;
    }
    cancel = TRUE;

    if (CurrentKeyRoot == -1)
        return FALSE; // HKEY_XXX keys cannot be renamed :-)

    CPluginData* pluginData = (CPluginData*)file.PluginData;

    // skip the default value if it is not set
    if (pluginData->Name == NULL && pluginData->Type == REG_NONE)
    {
        Error(IDS_CANNOTRENAMEDEFVAL);
        cancel = FALSE;
        return TRUE;
    }

    std::wstring keyName = pluginData->Name ? pluginData->Name : L"";
    const std::wstring text = SPLFormatStringOwned(LoadStrW(isDir ? IDS_QUICKRENAMEKEY : IDS_QUICKRENAMEVAL).c_str(), keyName.c_str());
    CNewKeyDialog dlg(parent, keyName, NULL, text.c_str(), LoadStrW(IDS_QUICKRENAME).c_str(), IDD_RENAME);
    if (dlg.Execute() != IDOK)
        return FALSE;

    if (CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                       pluginData->Name, -1,
                       keyName.c_str(), -1) == CSTR_EQUAL)
    {
        SG->SalMessageBox(GetParent(), LoadStrW(isDir ? IDS_SAMECASEKEY : IDS_SAMECASEVAL).c_str(),
                          LoadStrW(isDir ? IDS_RENAMEKEY : IDS_RENAMEVAL).c_str(), MB_OK);
        return FALSE;
    }

    BOOL skip, success;
    BOOL skipAllErrors = FALSE; // skip all errors
    BOOL skipAllOverwrites = FALSE;
    BOOL skipAllClassNames = FALSE;
    BOOL overwriteAll = FALSE;
    SafeWaitWindowOwner waitWindow;
    if (isDir)
    {
        // ensure the path does not contain disallowed characters
        if (keyName.find(L'\\') != std::wstring::npos)
        {
            SG->SalMessageBox(GetParent(), LoadStrW(IDS_SYNTAX).c_str(), LoadStrW(IDS_RENAMEKEY).c_str(), MB_OK);
            return FALSE;
        }

        std::wstring sourceKey = CurrentKeyName;
        SPLSalPathAppendOwned(sourceKey, pluginData->Name);
        std::wstring targetKey = CurrentKeyName;
        SPLSalPathAppendOwned(targetKey, keyName.c_str());

        // ensure that the target name does not exist
        if (!TestKey(CurrentKeyRoot, targetKey.c_str()))
            return FALSE;

        GetAsyncKeyState(VK_ESCAPE); // initialize GetAsyncKeyState - see the help
        SG->CreateSafeWaitWindow(LoadStrW(IDS_MOVEPROGRESS).c_str(), LoadStrW(IDS_PLUGINNAME).c_str(), 500, TRUE, SG->GetMainWindowHWND());
        waitWindow.Activate();

        // stack for enumerated subkey names
        // (subkeys must be enumerated all at once
        // before they can be deleted during a move)
        std::vector<std::wstring> stack;
        success = CopyOrMoveKey(CurrentKeyRoot, sourceKey, CurrentKeyRoot, targetKey, TRUE,
                                skip, skipAllErrors,
                                skipAllOverwrites, overwriteAll,
                                skipAllClassNames, stack);
    }
    else
    {
        SG->CreateSafeWaitWindow(LoadStrW(IDS_MOVEPROGRESS).c_str(), LoadStrW(IDS_PLUGINNAME).c_str(), 500, TRUE, SG->GetMainWindowHWND());
        waitWindow.Activate();

        success = CopyOrMoveValue(CurrentKeyRoot, CurrentKeyName.c_str(), pluginData->Name,
                                  CurrentKeyRoot, CurrentKeyName.c_str(), keyName.c_str(), TRUE,
                                  NULL, NULL, NULL, NULL);
    }

    if (success)
    {
        newName = keyName;
        cancel = FALSE;
        return TRUE;
    }
    else
        return FALSE;
}

void CPluginFSInterface::AcceptChangeOnPathNotification(const wchar_t* fsName, const wchar_t* path, BOOL includingSubdirs)
{
    CALL_STACK_MESSAGE4("CPluginFSInterface::AcceptChangeOnPathNotification(%ls, %ls, %d)",
                        fsName, path, includingSubdirs);
    if (CurrentKeyRoot != -1)
    {
        // compare paths or at least their prefixes (only paths on our FS have a chance;
        // disk paths and paths on other FS types in 'path' are filtered out automatically
        // because they can never match 'fsName'+':' at the beginning of 'path2' below)
        std::wstring path1 = path;
        std::wstring path2 = std::wstring(fsName) + L":\\" +
                             PredefinedHKeys[CurrentKeyRoot].KeyName + L"\\" + CurrentKeyName;
        while (!path1.empty() && path1.back() == L'\\')
            path1.pop_back();
        while (!path2.empty() && path2.back() == L'\\')
            path2.pop_back();
        const int len1 = static_cast<int>(path1.size());
        BOOL refresh = path1.size() <= path2.size() &&
                       SG->StrNICmp(path1.c_str(), path2.c_str(), len1) == 0 &&
                       (path2.size() == path1.size() ||
                        includingSubdirs && path2[path1.size()] == L'\\');
        if (refresh)
        {
            SG->PostRefreshPanelFS(this, FocusFirstNewItem); // refresh if this FS is displayed in a panel
        }
    }
}

BOOL CPluginFSInterface::CreateDir(const wchar_t* fsName, int mode, HWND parent,
                                   CSalamanderStringBuffer* newName, BOOL& cancel)
{
    std::wstring value;
    if (newName == NULL || !sally::plugin_abi::ReadStringBuffer(*newName, value))
        return FALSE;
    const BOOL result = CreateDirOwned(fsName, mode, parent, value, cancel);
    return sally::plugin_abi::WriteStringBuffer(*newName, value) ? result : FALSE;
}

BOOL CPluginFSInterface::CreateDirOwned(const wchar_t* fsName, int mode, HWND parent,
                                        std::wstring& newName, BOOL& cancel)
{
    CALL_STACK_MESSAGE3("CPluginFSInterface::CreateDir(%d, , , %d)", mode, cancel);
    PARENT(parent);

    if (mode == 2)
    {
        cancel = FALSE;
        return FALSE;
    }
    cancel = TRUE;

    if (CurrentKeyRoot == -1)
        return FALSE; //Error(IDS_NEWKEYINROOT);

    std::wstring enteredKeyName;
    while (1)
    {
        std::wstring keyName;
        BOOL direct = FALSE;
        CNewKeyDialog dlg(parent, enteredKeyName, &direct);
        if (dlg.Execute() != IDOK)
            return FALSE;

        keyName = enteredKeyName;

        // extract the user portion from the FS path
        if (!direct)
        {
            if (!RemoveFSNameFromPath(keyName))
            {
                Error(IDS_NOTREGEDTPATH);
                continue;
            }
            keyName.resize(wcslen(keyName.c_str()));
            if (keyName.empty())
            {
                Error(IDS_BADPATH);
                continue;
            }
        }

        BOOL success;
        std::wstring resolvedFullName(keyName);
        ResolveFullFSPath(resolvedFullName, success);
        if (!success)
            continue;
        WCHAR* key;
        int root;
        if (!ParseFullPath(resolvedFullName.data(), key, root))
        {
            Error(IDS_BADPATH);
            continue;
        }

        HKEY hKey;
        DWORD disp;
        int err = RegCreateKeyExW(PredefinedHKeys[root].HKey, key, 0, NULL, 0, KEY_READ, NULL, &hKey, &disp);
        if (err != ERROR_SUCCESS)
        {
            ErrorL(err, IDS_NEWKEY);
            continue;
        }

        if (disp == REG_OPENED_EXISTING_KEY)
        {
            SG->SalMessageBox(parent, LoadStrW(IDS_KEYEXISTS).c_str(), LoadStrW(IDS_PLUGINNAME).c_str(), MB_ICONINFORMATION);
            RegCloseKey(hKey);
            continue;
        }

        newName = keyName;

        RegCloseKey(hKey);

        break;
    }

    cancel = FALSE;
    return TRUE;
}

BOOL ExportValueData(int root, LPCWSTR key, LPCWSTR value, LPCWSTR tmpFileName, CQuadWord* newFileSize)
{
    CALL_STACK_MESSAGE3("ExportValueData(%d, , , %ls, )", root, tmpFileName);
    HKEY hKey = nullptr;
    int ret = RegOpenKeyExW(PredefinedHKeys[root].HKey, key, 0, KEY_QUERY_VALUE, &hKey);
    if (ret != ERROR_SUCCESS)
        return ErrorL(ret, IDS_QUERYVAL);
    RegistryKeyOwner keyOwner(hKey);

    DWORD type = 0;
    std::vector<BYTE> registryData;
    ret = RegedtQueryValueOwned(hKey, value, 0, type, registryData);
    if (ret != ERROR_SUCCESS)
    {
        return ErrorL(ret, IDS_QUERYVAL);
    }

    // The viewer preference is an explicit ACP byte projection. If the
    // registry text is malformed or not exactly representable, retain its
    // original UTF-16 bytes instead of silently substituting characters.
    std::vector<BYTE> viewerData;
    if (!RegedtPrepareViewerBytes(type, registryData,
                                  ExportStringsForViewerInASCII != FALSE,
                                  CP_ACP, viewerData))
        return Error(IDS_LOWMEM);

    // create or open the temporary file
    HANDLE file = CreateFileW(tmpFileName, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return Error(IDS_CREATETEMP);

    DWORD written = 0;
    const DWORD size = static_cast<DWORD>(viewerData.size());
    const BOOL writeOK = WriteFile(file,
                                   viewerData.empty() ? nullptr : viewerData.data(),
                                   size, &written, NULL) &&
                         written == size;

    DWORD err;
    SG->SalGetFileSize(file, *newFileSize, err); // ignore errors
    CloseHandle(file);

    return writeOK || Error(IDS_WRITETEMP);
}

void CPluginFSInterface::ViewFile(const wchar_t* fsName, HWND parent,
                                  CSalamanderForViewFileOnFSAbstract* salamander,
                                  CFileData& file)
{
    CALL_STACK_MESSAGE2("CPluginFSInterface::ViewFile(%ls, , , )", fsName);
    PARENT(parent);

    CPluginData* pluginData = (CPluginData*)file.PluginData;

    // skip the default value if it is not set
    // and do nothing at the registry root either
    if (pluginData->Name == NULL && pluginData->Type == REG_NONE || CurrentKeyRoot == -1)
        return;

    // build a unique file name for the disk cache (standard Salamander path format)
    std::wstring uniqueFileName = std::wstring(fsName) + L":\\" +
                                  PredefinedHKeys[CurrentKeyRoot].KeyName + L"\\" + CurrentKeyName;
    SPLSalPathAppendOwned(uniqueFileName, file.Name);

    // disk names are case-insensitive, the disk cache is case-sensitive; converting
    // to lowercase makes the cache behave case-insensitively as well
    SPLToLowerCaseOwned(SG, uniqueFileName);

    // create a file name that Windows can handle
    std::wstring fileName = L"_" + std::wstring(file.Name);
    ReplaceUnsafeCharacters(fileName.data());

    // obtain the copy name in the disk cache
    BOOL fileExists;
    const wchar_t* tmpFileName = salamander->AllocFileNameInCache(parent, uniqueFileName.c_str(),
                                                                  fileName.c_str(),
                                                                  NULL, fileExists);
    if (tmpFileName == NULL)
        return; // fatal error

    // prepare or refresh the key data copy in the disk cache
    BOOL newFileOK = FALSE;
    CQuadWord newFileSize(0, 0);
    if (ExportValueData(CurrentKeyRoot, CurrentKeyName.data(),
                        pluginData->Name ? pluginData->Name : L"",
                        tmpFileName, &newFileSize)) // the copy succeeded
    {
        newFileOK = TRUE; // if getting the file size fails, newFileSize stays zero (not critical)
    }

    // open the viewer
    HANDLE fileLock;
    BOOL fileLockOwner;
    if (!newFileOK || // open the viewer only if the copy is valid
        !salamander->OpenViewer(parent, tmpFileName, &fileLock, &fileLockOwner))
    { // on error reset the "lock"
        fileLock = NULL;
        fileLockOwner = FALSE;
    }

    // call FreeFileNameInCache to pair with AllocFileNameInCache (linking the viewer and disk cache)
    salamander->FreeFileNameInCache(uniqueFileName.c_str(), fileExists, newFileOK,
                                    newFileSize, fileLock, fileLockOwner, TRUE);
}

BOOL CPluginFSInterface::DeleteKey(const std::wstring& keyName, BOOL& skip,
                                   BOOL& skipAllErrors,
                                   std::vector<std::wstring>& stack)
{
    CALL_STACK_MESSAGE3("CPluginFSInterface::DeleteKey(, %d, %d, )", skip,
                        skipAllErrors);
    // check whether the user requested cancellation
    if (TestForCancel())
        return skip = FALSE;

    skip = FALSE;
    HKEY key = nullptr;
    while (1)
    {
        int ret = RegOpenKeyExW(PredefinedHKeys[CurrentKeyRoot].HKey,
                                keyName.c_str(), 0, KEY_ENUMERATE_SUB_KEYS, &key);
        if (ret != ERROR_SUCCESS)
        {
            if (!RegOperationError(ret, IDS_OPEN, IDS_DELKEY, CurrentKeyRoot,
                                   keyName.c_str(), &skip, &skipAllErrors))
                return FALSE;
        }
        else
            break;
    }
    RegistryKeyOwner keyOwner(key);

    DWORD i = 0;
    BOOL success = TRUE;
    const size_t top = stack.size();
    while (success)
    {
        std::wstring name;
        FILETIME ignoredTime{};
        const LONG ret = RegedtEnumerateSubKeyOwned(key, i, 64, name, ignoredTime);
        if (ret == ERROR_NO_MORE_ITEMS)
            break;
        if (ret != ERROR_SUCCESS)
        {
            success = RegOperationError(ret, IDS_ACCESS2, IDS_DELKEY,
                                        CurrentKeyRoot, keyName.c_str(),
                                        &skip, &skipAllErrors);
            continue;
        }

        stack.push_back(std::move(name));
        ++i;
    }

    keyOwner.Close();

    for (size_t j = stack.size(); success && j-- > top;)
    {
        std::wstring subKey = keyName;
        SPLSalPathAppendOwned(subKey, stack[j].c_str());

        BOOL childSkip = FALSE;
        if (!DeleteKey(subKey, childSkip, skipAllErrors, stack))
        {
            if (childSkip)
                skip = TRUE;
            else
                success = FALSE;
        }
        stack.erase(stack.begin() + j);
    }

    stack.resize(top);
    if (!success || skip)
        return FALSE;

    while (success)
    {
        int ret = RegDeleteKeyW(PredefinedHKeys[CurrentKeyRoot].HKey,
                                keyName.c_str());
        if (ret != ERROR_SUCCESS)
        {
            success = RegOperationError(ret, IDS_DELETE, IDS_DELKEY,
                                        CurrentKeyRoot, keyName.c_str(),
                                        &skip, &skipAllErrors);
        }
        else
            break; // delete succeeded
    }

    return success;
}

BOOL CPluginFSInterface::Delete(const wchar_t* fsName, int mode, HWND parent, int panel,
                                int selectedFiles, int selectedDirs, BOOL& cancelOrError)
{
    try
    {
        return DeleteCore(fsName, mode, parent, panel, selectedFiles,
                          selectedDirs, cancelOrError);
    }
    catch (...)
    {
        cancelOrError = TRUE;
        return FALSE;
    }
}

BOOL CPluginFSInterface::DeleteCore(const wchar_t* fsName, int mode, HWND parent,
                                    int panel, int selectedFiles, int selectedDirs,
                                    BOOL& cancelOrError)
{
    CALL_STACK_MESSAGE7("CPluginFSInterface::Delete(%ls, %d, , %d, %d, %d, %d)", fsName, mode,
                        panel, selectedFiles, selectedDirs, cancelOrError);
    PARENT(parent);

    if (CurrentKeyRoot == -1)
    {
        cancelOrError = FALSE;
        //Error(IDS_DELKEYINROOT);
        return TRUE;
    }

    cancelOrError = FALSE;
    if (mode == 1)
        return FALSE; // request the standard prompt (if SALCFG_CNFRMFILEDIRDEL is TRUE)

    BOOL ConfirmOnNotEmptyDirDelete;
    if (!SG->GetConfigParameter(SALCFG_CNFRMNEDIRDEL, &ConfirmOnNotEmptyDirDelete, sizeof(BOOL), NULL))
        ConfirmOnNotEmptyDirDelete = TRUE;

    const CFileData* f = NULL; // pointer to the panel file/directory being processed
    BOOL isDir = FALSE;        // TRUE if 'f' is a directory
    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int index = 0;
    BOOL success = TRUE;        // FALSE if an error occurs or the user cancels
    BOOL skipAllErrors = FALSE; // skip all errors

    GetAsyncKeyState(VK_ESCAPE); // initialize GetAsyncKeyState - see the help
    SG->CreateSafeWaitWindow(LoadStrW(IDS_DELETEPROGRESS).c_str(), LoadStrW(IDS_PLUGINNAME).c_str(), 500, TRUE, SG->GetMainWindowHWND());
    SafeWaitWindowOwner waitWindow;
    waitWindow.Activate();

    while (1)
    {
        // fetch data about the item being processed
        if (focused)
            f = SG->GetPanelFocusedItem(panel, &isDir);
        else
            f = SG->GetPanelSelectedItem(panel, &index, &isDir);

        // delete the file or directory
        if (f != NULL)
        {
            BOOL skip = FALSE;
            CPluginData* pd = (CPluginData*)f->PluginData;

            if (isDir)
            {
                std::wstring keyName = CurrentKeyName;
                SPLSalPathAppendOwned(keyName, pd->Name);

                BOOL empty = FALSE;
                while (success)
                {
                    HKEY key = nullptr;
                    int ret = RegOpenKeyExW(PredefinedHKeys[CurrentKeyRoot].HKey,
                                            keyName.c_str(), 0, KEY_QUERY_VALUE, &key);
                    if (ret == ERROR_SUCCESS)
                    {
                        DWORD subKeys;
                        DWORD values;
                        ret = RegQueryInfoKeyW(key, NULL, NULL, NULL, &subKeys, NULL, NULL,
                                               &values, NULL, NULL, NULL, NULL);
                        empty = subKeys == 0 && values == 0;
                        RegCloseKey(key);
                    }
                    if (ret != ERROR_SUCCESS)
                    {
                        if (!RegOperationError(ret, IDS_ACCESS2, IDS_DELKEY,
                                               CurrentKeyRoot, keyName.c_str(),
                                               &skip, &skipAllErrors))
                        {
                            if (!skip)
                                success = FALSE;
                            break;
                        }
                    }
                    else
                        break;
                }

                if (success && !skip && ConfirmOnNotEmptyDirDelete && !empty)
                {
                    const std::wstring question = SPLFormatStringOwned(
                        LoadStrW(IDS_CONFIRNNONEMPTYDELETE).c_str(), pd->Name);
                    switch (SG->SalMessageBox(parent, question.c_str(),
                                               LoadStrW(IDS_QUESTION).c_str(),
                                               MB_ICONQUESTION | MB_YESNOCANCEL))
                    {
                    case IDYES:
                        break;
                    case IDNO:
                        skip = TRUE;
                        break;
                    case IDCANCEL:
                    default:
                        success = FALSE;
                        break;
                    }
                }

                if (success && !skip)
                {
                    std::vector<std::wstring> stack;
                    if (!DeleteKey(keyName, skip, skipAllErrors, stack) && !skip)
                        success = FALSE;
                }
            }
            else
            {
                // skip deleting the default value if it is not set
                if (pd->Name != NULL || pd->Type != REG_NONE)
                {
                    while (1)
                    {
                        HKEY key;
                        int ret = RegOpenKeyExW(PredefinedHKeys[CurrentKeyRoot].HKey,
                                                CurrentKeyName.c_str(), 0,
                                                KEY_SET_VALUE, &key);
                        if (ret == ERROR_SUCCESS)
                        {
                            ret = RegDeleteValueW(key, pd->Name);
                            RegCloseKey(key);
                        }
                        if (ret != ERROR_SUCCESS)
                        {
                            if (!skipAllErrors)
                            {
                                std::wstring fullName = std::wstring(fsName) + L":" +
                                                        GetCurrentPathOwned();
                                SPLSalPathAppendOwned(fullName, f->Name);
                                int res = SG->DialogError(parent, BUTTONS_RETRYSKIPCANCEL, fullName.c_str(),
                                                          SPLGetErrorTextOwned(SG, ret).c_str(), LoadStrW(IDS_DELVAL).c_str());
                                switch (res)
                                {
                                case DIALOG_RETRY:
                                    break;

                                case DIALOG_SKIPALL:
                                    skipAllErrors = TRUE;
                                case DIALOG_SKIP:
                                    skip = TRUE;
                                    break;

                                default:
                                    success = FALSE;
                                    break; // DIALOG_CANCEL
                                }
                            }
                            else
                                skip = TRUE;
                        }
                        else
                            break; // delete succeeded

                        if (!success || skip)
                            break;
                    }
                }
            }
        }

        // decide whether it makes sense to continue (if no error and another marked item exists)
        if (!success || focused || f == NULL)
            break;
    }

    return success;
}

void CPluginFSInterface::ContextMenu(const wchar_t* fsName, HWND parent, int menuX, int menuY, int type,
                                     int panel, int selectedFiles, int selectedDirs)
{
    CALL_STACK_MESSAGE7("CPluginFSInterface::ContextMenu(%ls, , %d, %d, , %d, %d, %d)",
                        fsName, menuX, menuY, panel, selectedFiles, selectedDirs);
    PARENT(parent);

    // create the menu
    CGUIMenuPopupAbstract* menu = SalGUI->CreateMenuPopup();
    if (!menu)
        return;

    BOOL focusIsDir;
    const CFileData* fd = SG->GetPanelFocusedItem(panel, &focusIsDir);
    CPluginData* pd = (CPluginData*)fd->PluginData;

    BOOL targetIsReg = SG->GetPanelPluginFS(PANEL_TARGET) != NULL;

    int i = 0;
    std::wstring name;
    if (type == fscmItemsInPanel)
    {
        if (!pd)
            return;
        BOOL rawEdit = !focusIsDir &&
                       pd->Type != REG_DWORD_BIG_ENDIAN &&
                       pd->Type != REG_DWORD &&
                       pd->Type != REG_QWORD &&
                       pd->Type != REG_SZ &&
                       pd->Type != REG_EXPAND_SZ &&
                       pd->Type != REG_NONE;

        // open
        MENU_ITEM_INFO mi;
        mi.Mask = MENU_MASK_TYPE | MENU_MASK_STATE | MENU_MASK_ID | MENU_MASK_STRING;
        mi.Type = MENU_TYPE_STRING;
        mi.State = rawEdit ? MENU_STATE_GRAYED : MENU_STATE_DEFAULT;
        mi.ID = SALCMD_OPEN + 1;
        SPLGetSalamanderCommandOwned(SG, SALCMD_OPEN, name, NULL, NULL);
        if (!focusIsDir)
        {
            // append Salamander's shortcut to the command name
            const std::wstring text = LoadStrW(IDS_EDIT);
            const size_t tab = name.rfind(L'\t');
            if (!rawEdit && tab != std::wstring::npos)
                name = text + name.substr(tab);
            else
                name = text;
        }
        mi.String = name.data();
        menu->InsertItem(i++, TRUE, &mi);

        /* used by the export_mnu.py script that generates salmenu.mnu for Translator
   keep synchronized with the InsertItem() calls below...
MENU_TEMPLATE_ITEM ItemsInPanelMenu[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_RAWEDIT
  {MNTT_IT, IDS_EXPORT
  {MNTT_IT, IDS_SEARCH
  {MNTT_IT, IDS_COPYFULLNAME
  {MNTT_IT, IDS_COPYNAME
  {MNTT_PE, 0
};
*/

        // raw edit
        mi.State = focusIsDir ? MENU_STATE_GRAYED : 0;
        if (rawEdit)
        {
            mi.ID = SALCMD_OPEN + 1;
            mi.State |= MENU_STATE_DEFAULT;
            SPLGetSalamanderCommandOwned(SG, SALCMD_OPEN, name, NULL, NULL);

            // append Salamander's shortcut to the command name
            const std::wstring text = LoadStrW(IDS_RAWEDIT);
            const size_t tab = name.rfind(L'\t');
            if (rawEdit && tab != std::wstring::npos)
                name = text + name.substr(tab);
            else
                name = text;
            mi.String = name.data();
        }
        else
        {
            mi.ID = rawEdit ? SALCMD_OPEN + 1 : CMD_RAWEDIT;
            name = LoadStrW(IDS_RAWEDIT);
            mi.String = name.data();
        }
        menu->InsertItem(i++, TRUE, &mi);

        // rename
        mi.State = CurrentKeyRoot == -1 ? MENU_STATE_GRAYED : 0;
        mi.ID = SALCMD_QUICKRENAME + 1;
        SPLGetSalamanderCommandOwned(SG, SALCMD_QUICKRENAME, name, NULL, NULL);
        mi.String = name.data();
        menu->InsertItem(i++, TRUE, &mi);

        // view
        mi.State = focusIsDir ? MENU_STATE_GRAYED : 0;
        mi.ID = SALCMD_VIEW + 1;
        SPLGetSalamanderCommandOwned(SG, SALCMD_VIEW, name, NULL, NULL);
        mi.String = name.data();
        menu->InsertItem(i++, TRUE, &mi);

        // copy
        mi.State = CurrentKeyRoot == -1 /*|| !targetIsReg*/ ? MENU_STATE_GRAYED : 0;
        mi.ID = SALCMD_COPY + 1;
        SPLGetSalamanderCommandOwned(SG, SALCMD_COPY, name, NULL, NULL);
        mi.String = name.data();
        menu->InsertItem(i++, TRUE, &mi);

        // delete
        mi.State = CurrentKeyRoot == -1 ? MENU_STATE_GRAYED : 0;
        mi.ID = SALCMD_DELETE + 1;
        SPLGetSalamanderCommandOwned(SG, SALCMD_DELETE, name, NULL, NULL);
        mi.String = name.data();
        menu->InsertItem(i++, TRUE, &mi);

        // separator
        mi.Mask = MENU_MASK_TYPE;
        mi.Type = MENU_TYPE_SEPARATOR;
        mi.String = NULL;
        menu->InsertItem(i++, TRUE, &mi);

        // export
        mi.Mask = MENU_MASK_TYPE | MENU_MASK_STATE | MENU_MASK_ID | MENU_MASK_STRING;
        mi.Type = MENU_TYPE_STRING;
        mi.State = !focusIsDir ? MENU_STATE_GRAYED : 0;
        mi.ID = CMD_EXPORT;
        name = LoadStrW(IDS_EXPORT);
        mi.String = name.data();
        menu->InsertItem(i++, TRUE, &mi);

        // search
        mi.Mask = MENU_MASK_TYPE | MENU_MASK_STATE | MENU_MASK_ID | MENU_MASK_STRING;
        mi.Type = MENU_TYPE_STRING;
        mi.State = !focusIsDir ? MENU_STATE_GRAYED : 0;
        mi.ID = CMD_SEARCH;
        name = LoadStrW(IDS_SEARCH);
        mi.String = name.data();
        menu->InsertItem(i++, TRUE, &mi);

        // separator
        mi.Mask = MENU_MASK_TYPE;
        mi.Type = MENU_TYPE_SEPARATOR;
        mi.String = NULL;
        menu->InsertItem(i++, TRUE, &mi);

        // copy full name
        mi.Mask = MENU_MASK_TYPE | MENU_MASK_STATE | MENU_MASK_ID | MENU_MASK_STRING;
        mi.Type = MENU_TYPE_STRING;
        mi.State = 0;
        mi.ID = CMD_COPYFULLNAME;
        name = LoadStrW(IDS_COPYFULLNAME);
        mi.String = name.data();
        menu->InsertItem(i++, TRUE, &mi);

        // copy name
        mi.Mask = MENU_MASK_TYPE | MENU_MASK_STATE | MENU_MASK_ID | MENU_MASK_STRING;
        mi.Type = MENU_TYPE_STRING;
        mi.State = 0;
        mi.ID = CMD_COPYNAME;
        name = LoadStrW(IDS_COPYNAME);
        mi.String = name.data();
        menu->InsertItem(i++, TRUE, &mi);
    }
    if (type == fscmPathInPanel || type == fscmPanel)
    {
        /* used by the export_mnu.py script that generates salmenu.mnu for Translator
   keep synchronized with the InsertItem() calls below...
MENU_TEMPLATE_ITEM PanelMenu[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_MENUNEWKEY
  {MNTT_IT, IDS_MENUNEWVAL
  {MNTT_IT, IDS_EXPORT
  {MNTT_IT, IDS_SEARCH
  {MNTT_IT, IDS_COPYPATH
  {MNTT_PE, 0
};
*/

        // new key
        MENU_ITEM_INFO mi;
        mi.Mask = MENU_MASK_TYPE | MENU_MASK_STATE | MENU_MASK_ID | MENU_MASK_STRING;
        mi.Type = MENU_TYPE_STRING;
        mi.State = CurrentKeyRoot == -1 ? MENU_STATE_GRAYED : 0;
        mi.ID = SALCMD_CREATEDIRECTORY + 1;
        // append Salamander's shortcut to the command name
        SPLGetSalamanderCommandOwned(SG, SALCMD_CREATEDIRECTORY, name, NULL, NULL);
        const std::wstring text = LoadStrW(IDS_MENUNEWKEY);
        const size_t tab = name.rfind(L'\t');
        if (tab != std::wstring::npos)
            name = text + name.substr(tab);
        else
            name = text;
        mi.String = name.data();
        menu->InsertItem(i++, TRUE, &mi);

        // new value
        mi.State = CurrentKeyRoot == -1 ? MENU_STATE_GRAYED : 0;
        mi.ID = CMD_NEWVALUE;
        name = LoadStrW(IDS_MENUNEWVAL);
        mi.String = name.data();
        menu->InsertItem(i++, TRUE, &mi);

        // separator
        mi.Mask = MENU_MASK_TYPE;
        mi.Type = MENU_TYPE_SEPARATOR;
        mi.String = NULL;
        menu->InsertItem(i++, TRUE, &mi);

        // export
        mi.Mask = MENU_MASK_TYPE | MENU_MASK_STATE | MENU_MASK_ID | MENU_MASK_STRING;
        mi.Type = MENU_TYPE_STRING;
        mi.State = 0;
        mi.ID = CMD_EXPORT;
        name = LoadStrW(IDS_EXPORT);
        mi.String = name.data();
        menu->InsertItem(i++, TRUE, &mi);

        // search
        mi.State = 0;
        mi.ID = CMD_SEARCH;
        name = LoadStrW(IDS_SEARCH);
        mi.String = name.data();
        menu->InsertItem(i++, TRUE, &mi);

        // separator
        mi.Mask = MENU_MASK_TYPE;
        mi.Type = MENU_TYPE_SEPARATOR;
        mi.String = NULL;
        menu->InsertItem(i++, TRUE, &mi);

        // copy full path
        mi.Mask = MENU_MASK_TYPE | MENU_MASK_STATE | MENU_MASK_ID | MENU_MASK_STRING;
        mi.Type = MENU_TYPE_STRING;
        mi.State = 0;
        mi.ID = CMD_COPYPATH;
        name = LoadStrW(IDS_COPYPATH);
        mi.String = name.data();
        menu->InsertItem(i++, TRUE, &mi);
    }

    DWORD cmd = menu->Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON | MENU_TRACK_NONOTIFY,
                            menuX, menuY, parent, NULL);
    TRACE_I("cmd = " << cmd);

    switch (cmd)
    {
    case CMD_RAWEDIT:
        EditValue(CurrentKeyRoot, CurrentKeyName.data(), pd->Name, TRUE);
        break;
    case CMD_NEWVALUE:
        EditNewFile();
        break;
    case CMD_EXPORT:
    {
        std::wstring path = AssignedFSName + L":" + GetCurrentPathOwned();
        if (type == fscmItemsInPanel && focusIsDir)
            SPLSalPathAppendOwned(path, pd->Name);
        ExportKey(path.data());
        break;
    }

    case CMD_SEARCH:
    {
        std::wstring path = AssignedFSName + L":" + GetCurrentPathOwned();
        if (type == fscmItemsInPanel)
            SPLSalPathAppendOwned(path, pd->Name);
        CFindDialogThread* t = new CFindDialogThread(path.c_str());
        if (t)
        {
            if (!t->Create(ThreadQueue))
                delete t;
        }
        else
            Error(IDS_LOWMEM);
        break;
    }

    case CMD_COPYNAME:
    {
        SG->CopyTextToClipboard(fd->Name, -1, FALSE, NULL);
        break;
    }

    case CMD_COPYFULLNAME:
    {
        std::wstring path = GetCurrentPathOwned();
        SPLSalPathAppendOwned(path, fd->Name);
        SG->CopyTextToClipboard(!path.empty() && path.front() == L'\\' ? path.c_str() + 1 : path.c_str(),
                                -1, FALSE, NULL);
        break;
    }

    case CMD_COPYPATH:
    {
        const std::wstring path = GetCurrentPathOwned();
        SG->CopyTextToClipboard(!path.empty() && path.front() == L'\\' ? path.c_str() + 1 : path.c_str(),
                                -1, FALSE, NULL);
        break;
    }

    default:
        if (cmd > 0 && cmd <= 500)
            SG->PostSalamanderCommand(cmd - 1);
    }

    SalGUI->DestroyMenuPopup(menu);
}

BOOL CPluginFSInterface::EditNewFile()
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::EditNewFile()");
    if (CurrentKeyRoot == -1)
        return TRUE; //Error(IDS_NEWKEYINROOT);

    std::wstring enteredPathName;
    while (1)
    {
        std::wstring relativePathName;
        DWORD type = REG_SZ;
        BOOL direct = FALSE;
        CNewValDialog dlg(GetParent(), enteredPathName, &type, &direct);
        if (dlg.Execute() != IDOK)
            return FALSE;

        relativePathName = enteredPathName;

        // extract the user portion from the FS path
        if (!direct)
        {
            if (!RemoveFSNameFromPath(relativePathName))
            {
                Error(IDS_NOTREGEDTPATH);
                continue;
            }
            relativePathName.resize(wcslen(relativePathName.c_str()));
            if (relativePathName.empty())
            {
                Error(IDS_BADPATH);
                continue;
            }
        }

        BOOL success;
        std::wstring resolvedFullName(relativePathName);
        ResolveFullFSPath(resolvedFullName, success);
        if (!success)
            continue;
        WCHAR* key;
        int root;
        if (!ParseFullPath(resolvedFullName.data(), key, root))
        {
            Error(IDS_BADPATH);
            continue;
        }
        std::wstring keyName = key;
        std::wstring valName;
        const size_t slash = keyName.find_last_of(L'\\');
        if (slash == std::wstring::npos)
        {
            valName.swap(keyName);
        }
        else
        {
            valName = keyName.substr(slash + 1);
            keyName.resize(slash == 0 ? 1 : slash);
        }
        if (valName.empty())
        {
            Error(IDS_BADPATH);
            continue;
        }

        HKEY hKey;
        int ret = RegOpenKeyExW(PredefinedHKeys[root].HKey, keyName.c_str(),
                                0, KEY_QUERY_VALUE | KEY_SET_VALUE, &hKey);
        if (ret != ERROR_SUCCESS)
        {
            ErrorL(ret, IDS_NEWVAL);
            continue;
        }

        // check whether it exists
        DWORD existType;
        ret = RegQueryValueExW(hKey, valName.c_str(), 0, &existType, NULL, 0);
        if (ret == ERROR_SUCCESS)
        {
            const std::wstring message = SPLFormatStringOwned(LoadStrW(IDS_REPLACEVAL).c_str(), resolvedFullName.c_str());

            if (SG->SalMessageBox(GetParent(), message.c_str(), LoadStrW(IDS_QUESTION).c_str(), MB_ICONQUESTION | MB_YESNOCANCEL) != IDYES)
            {
                RegCloseKey(hKey);
                continue;
            }
        }

        switch (type)
        {
        case REG_SZ:
        case REG_EXPAND_SZ:
            ret = RegSetValueExW(hKey, valName.c_str(), 0, type, (CONST BYTE*)L"", 2);
            break;
        case REG_MULTI_SZ:
            ret = RegSetValueExW(hKey, valName.c_str(), 0, type, (CONST BYTE*)L"\0", 4);
            break;

        case REG_DWORD_BIG_ENDIAN:
        case REG_DWORD:
        {
            DWORD d = 0;
            ret = RegSetValueExW(hKey, valName.c_str(), 0, type, (CONST BYTE*)&d, 4);
            break;
        }

        case REG_QWORD:
        {
            QWORD d = 0;
            ret = RegSetValueExW(hKey, valName.c_str(), 0, type, (CONST BYTE*)&d, 8);
            break;
        }

        default:
            ret = RegSetValueExW(hKey, valName.c_str(), 0, type, (CONST BYTE*)L"", 0);
            break;
        }

        RegCloseKey(hKey);

        if (ret != ERROR_SUCCESS)
            return ErrorL(ret, IDS_NEWVAL);

        // ensure the new item receives focus
        FocusFirstNewItem = TRUE;

        break;
    }

    return TRUE;
}
