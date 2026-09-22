// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "regedt_registry_enum.h"
#include "../../registry_names.h"

// ****************************************************************************
//
// CPluginFSInterface - third part
//
//

BOOL ConfirmOnFileOverwrite, ConfirmOnSystemHiddenFileOverwrite,
    ConfirmOnOverwrite, ConfirmOnCreateTargetPath;

namespace
{
class RegistryKeyOwner
{
public:
    explicit RegistryKeyOwner(HKEY key = nullptr) noexcept : Key(key) {}
    ~RegistryKeyOwner() { Close(); }

    RegistryKeyOwner(const RegistryKeyOwner&) = delete;
    RegistryKeyOwner& operator=(const RegistryKeyOwner&) = delete;

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
    SafeWaitWindowOwner() noexcept = default;
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

BOOL SafeOpenKey(int root, const wchar_t* key, DWORD sam, HKEY& hKey,
                 int errorTitle, LPBOOL skip, LPBOOL skipAll)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE4("SafeOpenKey(%d, , 0x%X, , %d, , )", root, sam, errorTitle); // Petr: call-stack too slow
    while (1)
    {
        int res = RegOpenKeyExW(PredefinedHKeys[root].HKey, key, 0, sam, &hKey);
        if (res != ERROR_SUCCESS)
        {
            if (!RegOperationError(res, IDS_OPEN, errorTitle, root, key, skip, skipAll))
                return FALSE;
        }
        else
            break;
    }
    return TRUE;
}

BOOL SafeCreateKey(int root, const wchar_t* key, const wchar_t* className, DWORD sam, HKEY& hKey,
                   int errorTitle, BOOL& skip, BOOL& skipAll)
{
    CALL_STACK_MESSAGE6("SafeCreateKey(%d, , , 0x%X, , %d, %d, %d)", root, sam,
                        errorTitle, skip, skipAll);
    while (1)
    {
        int res = RegCreateKeyExW(PredefinedHKeys[root].HKey, key, 0,
                                  const_cast<wchar_t*>(className), 0, sam,
                                  NULL, &hKey, NULL);
        if (res != ERROR_SUCCESS)
        {
            if (!RegOperationError(res, IDS_CREATE, errorTitle, root, key, &skip, &skipAll))
                return FALSE;
        }
        else
            break;
    }
    return TRUE;
}

BOOL SafeQueryInfoKey(HKEY hKey, int root, const wchar_t* key, std::wstring* className,
                      LPDWORD maxData, FILETIME* time,
                      int errorTitle, BOOL& skip, BOOL& skipAll)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE5("SafeQueryInfoKey(, %d, , , , , %d, %d, %d)", root,
    //                      errorTitle, skip, skipAll);  // Petr: call-stack too slow
    while (1)
    {
        const int res = RegedtQueryKeyInfoOwned(hKey, className, maxData, time);
        if (res != ERROR_SUCCESS)
        {
            if (!RegOperationError(res, IDS_ACCESS2, errorTitle, root, key, &skip, &skipAll))
                return FALSE;
        }
        else
        {
#ifdef _DEBUG
            if (className && !className->empty())
            {
                TRACE_IW(L"registry key " << key << L" has the class name set to: " << className->c_str());
            }
#endif // _DEBUG
            break;
        }
    }
    return TRUE;
}

BOOL TestValue(HKEY hKey, int root, const wchar_t* key, const wchar_t* name,
               int sourceRoot, const wchar_t* sourceKey, const wchar_t* sourceName,
               int errorTitle, LPBOOL skip, LPBOOL skipAllErrors,
               LPBOOL overwriteAll, LPBOOL skipAllOvewrites)
{
    //  CALL_STACK_MESSAGE4("TestValue(, %d, , , %d, , , %d, , , , )", root,
    //                      sourceRoot, errorTitle);  // Petr: call-stack too slow, simplified for performance + ignoring that it is slow (TRACE_E that generates it annoys me)
    SLOW_CALL_STACK_MESSAGE1("TestValue()");
    while (1)
    {
        DWORD type, size;
        int res = RegQueryValueExW(hKey, name, NULL, &type, NULL, &size);
        if (res != ERROR_SUCCESS)
        {
            if (res == ERROR_FILE_NOT_FOUND)
            {
                return TRUE;
            }
            if (!RegOperationError(res, IDS_ACCESS2, errorTitle, root, key, skip, skipAllErrors))
                return FALSE;
        }
        else
        {
            if (overwriteAll && *overwriteAll)
                return TRUE;

            if (skipAllOvewrites && *skipAllOvewrites)
            {
                *skip = TRUE;
                return FALSE;
            }

            // prompt to overwrite
            const std::wstring valueName = name && *name ? name : LoadStrW(IDS_DEFAULTVALUE);
            const std::wstring sourceValueName = sourceName && *sourceName ? sourceName : LoadStrW(IDS_DEFAULTVALUE);
            const std::wstring fullFSPath = std::wstring(PredefinedHKeys[root].KeyName) + L"\\" + key + L"\\" + valueName;
            const std::wstring sourceFullFSPath = std::wstring(PredefinedHKeys[sourceRoot].KeyName) + L"\\" + sourceKey + L"\\" + sourceValueName;

            res = skip ? SG->DialogOverwrite(GetParent(), BUTTONS_YESALLSKIPCANCEL, fullFSPath.c_str(), L"", sourceFullFSPath.c_str(), L"") : SG->DialogOverwrite(GetParent(), BUTTONS_YESNOCANCEL, fullFSPath.c_str(), L"", sourceFullFSPath.c_str(), L"");
            switch (res)
            {
            case DIALOG_ALL:
                *overwriteAll = TRUE;
            case DIALOG_YES:
                return TRUE;
            case DIALOG_SKIPALL:
                *skipAllOvewrites = TRUE;
            case DIALOG_SKIP:
                *skip = TRUE;
                return FALSE;
            default:
                if (skip)
                    *skip = FALSE;
                return FALSE;
            }
        }
    }
    return TRUE;
}

BOOL SafeSetValue(HKEY hKey, int root, const wchar_t* key,
                  const wchar_t* name, DWORD type, const void* data, DWORD size,
                  int errorTitle, LPBOOL skip, LPBOOL skipAll)
{
    //  CALL_STACK_MESSAGE5("SafeSetValue(, %d, , , 0x%X, , 0x%X, %d, , )", root,
    //                      type, size, errorTitle); // Petr: call-stack too slow, simplified for performance + ignoring that it is slow (TRACE_E that generates it annoys me)
    SLOW_CALL_STACK_MESSAGE1("SafeSetValue()");
    while (1)
    {
        int res = RegSetValueExW(hKey, name, 0, type,
                                 static_cast<const BYTE*>(data), size);
        if (res != ERROR_SUCCESS)
        {
            if (!RegOperationError(res, IDS_SETVAL2, errorTitle, root, key, skip, skipAll))
                return FALSE;
        }
        else
            break;
    }
    return TRUE;
}

BOOL SafeDeleteKey(int root, const wchar_t* key, int errorTitle, BOOL& skip, BOOL& skipAll)
{
    CALL_STACK_MESSAGE5("SafeDeleteKey(%d, , %d, %d, %d)", root, errorTitle,
                        skip, skipAll);
    while (1)
    {
        int res = RegDeleteKeyW(PredefinedHKeys[root].HKey, key);
        if (res != ERROR_SUCCESS)
        {
            if (!RegOperationError(res, IDS_REMOVESOURCE, errorTitle, root, key, &skip, &skipAll))
                return FALSE;
        }
        else
            break;
    }
    return TRUE;
}

static BOOL CopyOrMoveKeyCore(int sourceRoot, std::wstring& source,
                              int targetRoot, std::wstring& target,
                              BOOL move, BOOL& skip, BOOL& skipAllErrors,
                              BOOL& skipAllOverwrites, BOOL& overwriteAll,
                              BOOL& skipAllClassNames,
                              std::vector<std::wstring>& stack)
{
    //  CALL_STACK_MESSAGE10("CopyOrMoveKey(%d, , %d, , %d, %d, %d, %d, %d, %d, %d, , )",
    //                       sourceRoot, targetRoot, move, skip, skipAllErrors,
    //                       skipAllLongNames, skipAllOverwrites, overwriteAll,
    //                       skipAllClassNames); // Petr: call-stack too slow, simplified for performance
    CALL_STACK_MESSAGE1("CopyOrMoveKey()");
    // check for user cancellation
    if (TestForCancel())
        return skip = FALSE;

    HKEY sourceHKey = nullptr;
    int errorTitle = move ? IDS_MOVEKEY : IDS_COPYKEY;

    // open the source key
    if (!SafeOpenKey(sourceRoot, source.data(), KEY_READ, sourceHKey,
                     errorTitle, &skip, &skipAllErrors))
        return FALSE;
    RegistryKeyOwner sourceOwner(sourceHKey);

    // load the class name and maximum data size
    DWORD maxData;
    std::wstring sourceClassName;
    if (!SafeQueryInfoKey(sourceHKey, sourceRoot, source.data(), &sourceClassName,
                          &maxData, NULL, errorTitle, skip, skipAllErrors))
    {
        return FALSE;
    }

    // create the target key
    HKEY targetHKey = nullptr;
    if (!SafeCreateKey(targetRoot, target.data(), sourceClassName.data(), KEY_WRITE | KEY_READ, targetHKey,
                       errorTitle, skip, skipAllErrors))
        return FALSE;
    RegistryKeyOwner targetOwner(targetHKey);

    // verify that the new key has the same class name
    if (!skipAllClassNames)
    {
        std::wstring targetClassName;
        if (!SafeQueryInfoKey(targetHKey, targetRoot, target.data(), &targetClassName, NULL, NULL, errorTitle, skip, skipAllErrors))
        {
            return FALSE;
        }

        if (CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                            sourceClassName.c_str(), -1,
                            targetClassName.c_str(), -1) != CSTR_EQUAL)
        {
            const std::wstring fileName = std::wstring(PredefinedHKeys[sourceRoot].KeyName) + L"\\" + source;
            switch (SG->DialogQuestion(GetParent(), BUTTONS_YESALLCANCEL, fileName.c_str(), LoadStrW(IDS_COPYCLASSNAME).c_str(), LoadStrW(IDS_WARNING).c_str()))
            {
            case DIALOG_ALL:
                skipAllClassNames = TRUE;
            case DIALOG_YES:
                break;

            default:
                skip = FALSE;
                return FALSE;
            }
        }
    }

    // Copy value bytes through the named registry adapter. Names and payloads grow independently.
    DWORD index = 0;
    std::wstring name;
    std::vector<BYTE> data;
    DWORD type, size;
    BOOL noMore = TRUE;
    BOOL someFileSkipped = FALSE;
    BOOL success;

    while (1)
    {
        const LONG enumResult = RegedtEnumerateValueOwned(sourceHKey, index, 64, maxData,
                                                           name, type, data, size);
        if (enumResult == ERROR_NO_MORE_ITEMS)
        {
            noMore = TRUE;
            break;
        }
        if (enumResult != ERROR_SUCCESS)
        {
            const BOOL retry = RegOperationError(enumResult, IDS_ACCESS2, errorTitle,
                                                  sourceRoot, source.data(), &skip, &skipAllErrors);
            if (retry)
                continue;
            ++index;
            noMore = FALSE;
            if (skip)
            {
                someFileSkipped = TRUE;
                skip = FALSE;
                continue;
            }
            break;
        }
        ++index;
        noMore = FALSE;
        success = TestValue(targetHKey, targetRoot, target.data(), name.data(),
                            sourceRoot, source.data(), name.data(),
                            errorTitle, &skip, &skipAllErrors,
                            &overwriteAll, &skipAllOverwrites) &&
                  SafeSetValue(targetHKey, targetRoot, target.data(), name.data(),
                               type, data.data(), size, errorTitle, &skip, &skipAllErrors);

        if (!success)
        {
            if (skip)
            {
                someFileSkipped = TRUE;
                skip = FALSE;
            }
            else
                break;
        }

        // check for user cancellation
        if (TestForCancel())
            break;
    }

    if (!noMore)
    {
        return skip = FALSE;
    }

    // copy the subkeys recursively as well

    // first enumerate all keys onto the stack
    const size_t sourceLen = source.size();
    const size_t targetLen = target.size();
    index = 0;
    const size_t top = stack.size();
    while (1)
    {
        FILETIME ignoredTime{};
        const LONG enumResult = RegedtEnumerateSubKeyOwned(sourceHKey, index, 64, name, ignoredTime);
        if (enumResult == ERROR_SUCCESS)
        {
            ++index;
            noMore = FALSE;
            stack.push_back(name);
        }
        else if (enumResult == ERROR_NO_MORE_ITEMS)
        {
            noMore = TRUE;
            break;
        }
        else
        {
            const BOOL retry = RegOperationError(enumResult, IDS_ACCESS2, errorTitle,
                                                  sourceRoot, source.data(), &skip, &skipAllErrors);
            if (retry)
                continue;
            ++index;
            noMore = FALSE;
            if (skip)
            {
                someFileSkipped = TRUE;
                skip = FALSE;
            }
            else
                break;
        }

        // check for user cancellation
        if (TestForCancel())
            break;
    }

    if (!noMore)
    {
        return skip = FALSE;
    }

    // copy the keys stored on the stack
    for (size_t i = stack.size(); i-- > top;)
    {
        source.resize(sourceLen);
        target.resize(targetLen);
        if (!source.empty())
            source.push_back(L'\\');
        if (!target.empty())
            target.push_back(L'\\');
        source.append(stack[i]);
        target.append(stack[i]);

        if (!CopyOrMoveKeyCore(sourceRoot, source, targetRoot, target, move,
                               skip, skipAllErrors,
                               skipAllOverwrites, overwriteAll,
                               skipAllClassNames, stack))
        {
            if (skip)
                someFileSkipped = TRUE;
            else
            {
                return FALSE;
            }
        }
        stack.erase(stack.begin() + i);
    }

    source.resize(sourceLen);
    target.resize(targetLen);

    if (someFileSkipped)
    {
        skip = TRUE;
        return FALSE;
    }

    if (move)
    {
        sourceOwner.Close();
        targetOwner.Close();
        return SafeDeleteKey(sourceRoot, source.data(), errorTitle, skip, skipAllErrors);
    }

    return TRUE;
}

BOOL CopyOrMoveKey(int sourceRoot, std::wstring& source, int targetRoot,
                   std::wstring& target, BOOL move, BOOL& skip,
                   BOOL& skipAllErrors, BOOL& skipAllOverwrites,
                   BOOL& overwriteAll, BOOL& skipAllClassNames,
                   std::vector<std::wstring>& stack)
{
    const size_t sourceSize = source.size();
    const size_t targetSize = target.size();
    const size_t stackSize = stack.size();
    try
    {
        const BOOL result = CopyOrMoveKeyCore(sourceRoot, source, targetRoot,
                                              target, move, skip,
                                              skipAllErrors, skipAllOverwrites,
                                              overwriteAll, skipAllClassNames,
                                              stack);
        source.resize(sourceSize);
        target.resize(targetSize);
        stack.resize(stackSize);
        return result;
    }
    catch (...)
    {
        source.resize(sourceSize);
        target.resize(targetSize);
        stack.resize(stackSize);
        skip = FALSE;
        return Error(IDS_LOWMEM);
    }
}

BOOL SafeDeleteValue(HKEY hKey, int root, const wchar_t* key, const wchar_t* value,
                     int errorTitle, LPBOOL skip, LPBOOL skipAll)
{
    CALL_STACK_MESSAGE3("SafeDeleteValue(, %d, , , %d, , )", root, errorTitle);
    while (1)
    {
        int res = RegDeleteValueW(hKey, value);
        if (res != ERROR_SUCCESS)
        {
            if (!RegOperationError(res, IDS_REMOVESOURCE2, errorTitle, root, key, skip, skipAll))
                return FALSE;
        }
        else
            break;
    }
    return TRUE;
}

static BOOL CopyOrMoveValueCore(int sourceRoot, const wchar_t* sourcePath,
                                const wchar_t* sourceName, int targetRoot,
                                const wchar_t* targetPath,
                                const wchar_t* targetName, BOOL move,
                                LPBOOL skip, LPBOOL skipAllErrors,
                                LPBOOL skipAllOverwrites, LPBOOL overwriteAll)
{
    CALL_STACK_MESSAGE4("CopyOrMoveValue(%d, , , %d, , , %d, , , , )",
                        sourceRoot, targetRoot, move);
    HKEY sourceHKey = nullptr;
    int errorTitle = move ? IDS_MOVEVALUE : IDS_COPYVALUE;

    // open the source key
    if (!SafeOpenKey(sourceRoot, sourcePath, KEY_READ | (move ? KEY_WRITE : 0), sourceHKey,
                     errorTitle, skip, skipAllErrors))
        return FALSE;
    RegistryKeyOwner sourceOwner(sourceHKey);

    // open the target key
    HKEY targetHKey = nullptr;
    if (!SafeOpenKey(targetRoot, targetPath, KEY_READ | KEY_WRITE, targetHKey,
                     errorTitle, skip, skipAllErrors))
        return FALSE;
    RegistryKeyOwner targetOwner(targetHKey);

    // test whether overwriting is possible
    if (!TestValue(targetHKey, targetRoot, targetPath, targetName,
                   sourceRoot, sourcePath, sourceName,
                   errorTitle, skip, skipAllErrors, overwriteAll, skipAllOverwrites))
    {
        return FALSE;
    }

    DWORD type = 0;
    std::vector<BYTE> data;
    while (1)
    {
        const LONG result = RegedtQueryValueOwned(sourceHKey, sourceName, 0, type, data);
        if (result == ERROR_SUCCESS)
            break;
        if (!RegOperationError(result, IDS_ACCESS2, errorTitle, sourceRoot,
                               sourcePath, skip, skipAllErrors))
            return FALSE;
    }

    // set the value
    if (!SafeSetValue(targetHKey, targetRoot, targetPath, targetName, type,
                      data.empty() ? nullptr : data.data(),
                      static_cast<DWORD>(data.size()),
                      errorTitle, skip, skipAllErrors))
        return FALSE;

    // for a move delete the value in the source key
    BOOL ret = move ? SafeDeleteValue(sourceHKey, sourceRoot, sourcePath, sourceName,
                                      errorTitle, skip, skipAllErrors)
                    : TRUE;

    return ret;
}

BOOL CopyOrMoveValue(int sourceRoot, const wchar_t* sourcePath,
                     const wchar_t* sourceName, int targetRoot,
                     const wchar_t* targetPath, const wchar_t* targetName,
                     BOOL move, LPBOOL skip, LPBOOL skipAllErrors,
                     LPBOOL skipAllOverwrites, LPBOOL overwriteAll)
{
    try
    {
        return CopyOrMoveValueCore(sourceRoot, sourcePath, sourceName,
                                   targetRoot, targetPath, targetName, move,
                                   skip, skipAllErrors, skipAllOverwrites,
                                   overwriteAll);
    }
    catch (...)
    {
        if (skip)
            *skip = FALSE;
        return Error(IDS_LOWMEM);
    }
}

BOOL CreateTargetPath(int root, const wchar_t* key, int errorTitle)
{
    CALL_STACK_MESSAGE3("CreateTargetPath(%d, , %d)", root, errorTitle);
    const std::wstring fullPath = key ? key : L"";
    size_t end = 0;
    while (end < fullPath.size())
    {
        end = fullPath.find(L'\\', end + 1);
        const std::wstring targetPath = fullPath.substr(0, end);

        int err;
        HKEY hKey;
        if ((err = RegCreateKeyExW(PredefinedHKeys[root].HKey, targetPath.c_str(), 0, NULL, 0, KEY_READ, NULL, &hKey, NULL)) != ERROR_SUCCESS)
        {
            return !ErrorL(err, IDS_CREATE, PredefinedHKeys[root].KeyName, targetPath.c_str());
        }
        else
            RegCloseKey(hKey);
        if (end == std::wstring::npos)
            break;
    }
    return TRUE;
}

std::wstring ExpandPluralFilesDirs(int files, int dirs, int panel,
                                   BOOL focused, BOOL copy)
{
    CALL_STACK_MESSAGE6("ExpandPluralFilesDirs(%d, %d, %d, %d, %d)",
                        files, dirs, panel, focused, copy);
    const std::wstring copyOrMove = LoadStrW(copy ? IDS_COPY : IDS_MOVE);

    if (files > 0 && dirs > 0)
    {
        CQuadWord parametersArray[] = {CQuadWord(files, 0), CQuadWord(dirs, 0)};
        const std::wstring plural = SPLExpandPluralStringOwned(
            SG, LoadStrW(IDS_COPYORMOVE5).c_str(), 2, parametersArray);
        return SPLFormatStringOwned(plural.c_str(), copyOrMove.c_str(), files, dirs);
    }
    if (files == 1 || dirs == 1)
    {
        int index = 0;
        const CFileData* f;
        BOOL isDir;
        if (focused)
            f = SG->GetPanelFocusedItem(panel, &isDir);
        else
            f = SG->GetPanelSelectedItem(panel, &index, &isDir);
        CPluginData* pd = (CPluginData*)f->PluginData;

        return SPLFormatStringOwned(
            LoadStrW(files == 1 ? IDS_COPYORMOVE1 : IDS_COPYORMOVE2).c_str(),
            copyOrMove.c_str(),
            pd->Name == NULL ? LoadStrW(IDS_DEFAULTVALUE).c_str() : pd->Name);
    }
    CQuadWord param = CQuadWord(files + dirs, 0);
    const std::wstring plural = SPLExpandPluralStringOwned(
        SG, LoadStrW(files ? IDS_COPYORMOVE3 : IDS_COPYORMOVE4).c_str(), 1, &param);
    return SPLFormatStringOwned(plural.c_str(), copyOrMove.c_str(), files + dirs);
}

BOOL CPluginFSInterface::CopyOrMoveFromFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                                          int panel, int selectedFiles, int selectedDirs,
                                          CSalamanderStringBuffer* targetPath, BOOL& operationMask,
                                          BOOL& cancelOrHandlePath, HWND dropTarget)
{
    try
    {
        return CopyOrMoveFromFSCore(copy, mode, fsName, parent, panel,
                                    selectedFiles, selectedDirs, targetPath,
                                    operationMask, cancelOrHandlePath, dropTarget);
    }
    catch (...)
    {
        cancelOrHandlePath = TRUE;
        return FALSE;
    }
}

BOOL CPluginFSInterface::CopyOrMoveFromFSCore(BOOL copy, int mode,
                                              const wchar_t* fsName, HWND parent,
                                              int panel, int selectedFiles,
                                              int selectedDirs,
                                              CSalamanderStringBuffer* targetPath,
                                              BOOL& operationMask,
                                              BOOL& cancelOrHandlePath,
                                              HWND dropTarget)
{
    std::wstring targetPathValue;
    if (targetPath == NULL ||
        !sally::plugin_abi::ReadStringBuffer(*targetPath, targetPathValue))
        return FALSE;
    const size_t targetSeparator = targetPathValue.find(L'\0');
    if (targetSeparator != std::wstring::npos)
        targetPathValue.resize(targetSeparator);
    CALL_STACK_MESSAGE9("CPluginFSInterface::CopyOrMoveFromFS(%d, %d, %ls, , %d, %d, "
                        "%d, , %d, %d, )",
                        copy, mode, fsName, panel, selectedFiles,
                        selectedDirs, operationMask, cancelOrHandlePath);
    PARENT(parent);

    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
    int errorTitle = copy ? IDS_COPYKEY : IDS_MOVEKEY;
    int title = copy ? IDS_COPY : IDS_MOVERENAME;

    cancelOrHandlePath = TRUE;

    std::wstring targetPathW;
    // target path specified via drag&drop
    if (mode == 5)
    {
        targetPathW = targetPathValue;
    }

    // just to be sure
    if (mode != 1 && mode != 5)
        return TRUE;

    if (CurrentKeyRoot == -1)
        return TRUE; //HKEY_XXX cannot be copied

    std::wstring enteredTargetPathW;
    int targetPanel = (panel == PANEL_LEFT ? PANEL_RIGHT : PANEL_LEFT);

    // check whether the second panel shows the registry
    CPluginFSInterface* targetFS = (CPluginFSInterface*)SG->GetPanelPluginFS(targetPanel);
    if (mode != 5 && targetFS && targetFS->CurrentKeyRoot != -1)
    {
        if (!targetFS->CurrentKeyName.empty())
            enteredTargetPathW = std::wstring(fsName) + L":\\" +
                                 PredefinedHKeys[targetFS->CurrentKeyRoot].KeyName + L"\\" +
                                 targetFS->CurrentKeyName + L"\\";
        else
            enteredTargetPathW = std::wstring(fsName) + L":\\" +
                                 PredefinedHKeys[targetFS->CurrentKeyRoot].KeyName + L"\\";

        SG->SetUserWorkedOnPanelPath(PANEL_TARGET); // default action = work with the path in the target panel
    }
    else
        enteredTargetPathW.clear();

    BOOL firstRound = TRUE;
    while (1)
    {
        if (!firstRound && mode == 5)
            return TRUE; // error on the drag&drop path, abort
        firstRound = FALSE;

        // ask the user for the path
        BOOL direct = FALSE;
        const std::wstring text = ExpandPluralFilesDirs(
            selectedFiles, selectedDirs, panel, focused, copy);
        CCopyOrMoveDialog dlg(parent, enteredTargetPathW, &direct,
                              text.c_str(), LoadStrW(title).c_str());
        if (mode != 5 && dlg.Execute() != IDOK)
            return TRUE;

        if (mode != 5)
            targetPathW = enteredTargetPathW;

        // separate the user part from the FS path
        if (!direct)
        {
            // The overload re-syncs size() itself; this was the one call site
            // that remembered to do it by hand, and the other four did not.
            if (!RemoveFSNameFromPath(targetPathW))
            {
                Error(IDS_NOTREGEDTPATH);
                continue;
            }
            if (targetPathW.empty())
            {
                Error(IDS_BADPATH);
                continue;
            }
        }

        // convert the relative path to an absolute one
        BOOL success; // FALSE in case of an error or user cancellation
        std::wstring resolvedTargetPath(targetPathW);
        ResolveFullFSPath(resolvedTargetPath, success);
        if (!success)
            continue;
        targetPathW = std::move(resolvedTargetPath);

        WCHAR* key;
        int root;
        if (!ParseFullPath(targetPathW.data(), key, root))
        {
            Error(IDS_BADPATH);
            continue;
        }

        if (root == -1)
        {
            Error(IDS_COPYTOTROOT);
            continue;
        }

        std::wstring targetKey = key;
        std::wstring targetName; // target item name when exactly one item is selected
        BOOL useTargetName = FALSE;
        int err;
        HKEY hKey;

        // determine what type of operation it is
        if (!targetKey.empty())
        {
            if (targetKey.back() != L'\\')
            {
                if ((err = RegOpenKeyExW(PredefinedHKeys[root].HKey, targetKey.c_str(), 0, KEY_READ, &hKey)) != ERROR_SUCCESS)
                {
                    // if a path without a trailing slash does not exist, treat the last part of the path as
                    // the name of the target file (wildcards unsupported), makes sense only when just
                    // one file
                    if (err != ERROR_FILE_NOT_FOUND)
                    {
                        ErrorL(err, IDS_ACCESS, PredefinedHKeys[root].KeyName, targetKey.c_str());
                        continue;
                    }
                    if (selectedFiles + selectedDirs > 1)
                    {
                        Error(IDS_SAMETARGET);
                        continue;
                    }
                    const size_t slash = targetKey.find_last_of(L'\\');
                    if (slash == std::wstring::npos)
                    {
                        targetName.swap(targetKey);
                    }
                    else
                    {
                        targetName = targetKey.substr(slash + 1);
                        targetKey.resize(slash == 0 ? 1 : slash);
                    }
                    if (targetName.empty())
                    {
                        Error(IDS_BADPATH);
                        continue;
                    }
                    useTargetName = TRUE;
                }
                else
                    RegCloseKey(hKey);
            }
            else
                targetKey.pop_back();
        }

        // verify whether the target path exists
        if ((err = RegOpenKeyExW(PredefinedHKeys[root].HKey, targetKey.c_str(), 0, KEY_READ, &hKey)) != ERROR_SUCCESS)
        {
            if (err != ERROR_FILE_NOT_FOUND)
            {
                ErrorL(err, IDS_ACCESS, PredefinedHKeys[root].KeyName, targetKey.c_str());
                continue;
            }

            // path does not exist, ask whether to create it
            const std::wstring message = SPLFormatStringOwned(LoadStrW(IDS_CREATETARGET).c_str(), PredefinedHKeys[root].KeyName, targetKey.c_str());
            if (SG->SalMessageBox(GetParent(), message.c_str(), LoadStrW(title).c_str(), MB_YESNO) != IDYES)
                continue;

            // create it
            if (!CreateTargetPath(root, targetKey.c_str(), errorTitle))
                continue;
        }
        else
            RegCloseKey(hKey);

        // retrieve the "Confirm on" values from the configuration
        SG->GetConfigParameter(SALCFG_CNFRMFILEOVER, &ConfirmOnFileOverwrite, 4, NULL);
        SG->GetConfigParameter(SALCFG_CNFRMSHFILEOVER, &ConfirmOnSystemHiddenFileOverwrite, 4, NULL);
        SG->GetConfigParameter(SALCFG_CNFRMCREATEPATH, &ConfirmOnCreateTargetPath, 4, NULL);
        //ConfirmOnOverwrite = ConfirmOnFileOverwrite || ConfirmOnSystemHiddenFileOverwrite;
        ConfirmOnOverwrite = TRUE;

        const CFileData* f = NULL; // pointer to the file/directory in the panel to process
        BOOL isDir = FALSE;        // TRUE when 'f' is a directory
        int index = 0;
        BOOL skipAllErrors = FALSE; // skip all errors
        BOOL skipAllOverwrites = FALSE;
        BOOL skipAllClassNames = FALSE;
        BOOL overwriteAll = !ConfirmOnOverwrite;
        std::wstring nextFocus;
        BOOL sourceEqualsTarget = FALSE;

        // ensure we are not copying the file onto itself
        if (root == CurrentKeyRoot &&
            CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                            targetKey.c_str(), -1,
                           CurrentKeyName.c_str(), -1) == CSTR_EQUAL)
        {
            // fetch data about the first processed file
            int index2 = 0;
            if (focused)
                f = SG->GetPanelFocusedItem(panel, &isDir);
            else
                f = SG->GetPanelSelectedItem(panel, &index2, &isDir);
            CPluginData* pd = (CPluginData*)f->PluginData;

            if (!useTargetName ||
                pd->Name && CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                                           pd->Name, -1,
                                            targetName.c_str(), -1) == CSTR_EQUAL)
            {
                Error(copy ? IDS_CANTCOPYTOITSELF : IDS_CANTMOVETOITSELF);
                continue;
            }

            // this is a rename
            if (!copy)
                nextFocus = targetName;
            sourceEqualsTarget = TRUE;
        }

        int sourceRoot = CurrentKeyRoot;
        const std::wstring sourceKey = CurrentKeyName;
        // stack for enumerated subkey names
        // (subkeys must all be enumerated at once
        // and then they can be deleted (during a move)
        std::vector<std::wstring> stack;

        GetAsyncKeyState(VK_ESCAPE); // init GetAsyncKeyState - see help
        SG->CreateSafeWaitWindow(LoadStrW(copy ? IDS_COPYPROGRESS : IDS_MOVEPROGRESS).c_str(),
                                 LoadStrW(IDS_PLUGINNAME).c_str(), 500, TRUE, SG->GetMainWindowHWND());
        SafeWaitWindowOwner waitWindow;
        waitWindow.Activate();

        while (1)
        {
            // fetch data about the processed file
            if (focused)
                f = SG->GetPanelFocusedItem(panel, &isDir);
            else
                f = SG->GetPanelSelectedItem(panel, &index, &isDir);

            // perform copy/move on the file/directory
            if (f != NULL)
            {
                CPluginData* pd = (CPluginData*)f->PluginData;
                BOOL skip = FALSE;
                if (isDir)
                {
                    if (success && !skip)
                    {
                        std::wstring sourceItem = sourceKey;
                        SPLSalPathAppendOwned(sourceItem, pd->Name);
                        std::wstring targetItem = targetKey;
                        SPLSalPathAppendOwned(targetItem, useTargetName ? targetName.c_str() : pd->Name);

                        // also verify that sourceKey is not a prefix of targetKey
                        const size_t sourceLen2 = sourceItem.size();
                        if (root == sourceRoot &&
                            sourceLen2 <= targetItem.size() &&
                            CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                                            sourceItem.c_str(), static_cast<int>(sourceLen2),
                                            targetItem.c_str(), static_cast<int>(sourceLen2)) == CSTR_EQUAL &&
                            (sourceLen2 == targetItem.size() || targetItem[sourceLen2] == L'\\'))
                        {
                            const std::wstring message = SPLFormatStringOwned(
                                LoadStrW(copy ? IDS_CANTCOPYTOITSELF2 : IDS_CANTMOVETOITSELF2).c_str(), pd->Name);

                            if (SG->SalMessageBox(GetParent(), message.c_str(), LoadStrW(errorTitle).c_str(), MB_OKCANCEL) == IDOK)
                                skip = TRUE;
                            else
                                success = FALSE;
                        }

                        if (!skip && success)
                        {
                            success = CopyOrMoveKey(sourceRoot, sourceItem, root, targetItem, !copy,
                                                    skip, skipAllErrors,
                                                    skipAllOverwrites, overwriteAll,
                                                    skipAllClassNames, stack) ||
                                      skip;
                        }
                    }
                }
                else
                {
                    // do not process the default value if it is not set
                    if (pd->Name != NULL || pd->Type != REG_NONE)
                    {
                        success = CopyOrMoveValue(sourceRoot, sourceKey.c_str(), pd->Name,
                                                  root, targetKey.c_str(), useTargetName ? targetName.c_str() : pd->Name, !copy,
                                                  &skip, &skipAllErrors, &skipAllOverwrites, &overwriteAll) ||
                                  skip;
                    }
                }
            }

            // determine whether it makes sense to continue (if not cancelled and another selected item exists)
            if (!success || focused || f == NULL)
                break;
        }

        if (success)
        {
            if (!sally::plugin_abi::WriteStringBuffer(*targetPath, nextFocus))
                return FALSE;
            cancelOrHandlePath = FALSE;
        }
        else
            cancelOrHandlePath = TRUE; // error/cancel

        break;
    }

    return TRUE;
}

BOOL CPluginFSInterface::OpenFindDialog(const wchar_t* fsName, int panel)
{
    CALL_STACK_MESSAGE3("CPluginFSInterface::OpenFindDialog(%ls, %d)", fsName, panel);

    PARENT(SG->GetMainWindowHWND());
    const std::wstring path = std::wstring(fsName) + L":" + GetCurrentPathOwned();
    CFindDialogThread* t = new CFindDialogThread(path.c_str());
    if (t)
    {
        if (!t->Create(ThreadQueue))
            delete t;
    }
    else
        Error(IDS_LOWMEM);
    return TRUE;
}

void CPluginFSInterface::OpenActiveFolder(const wchar_t* fsName, HWND parent)
{
    // regedit has no parameter to set which Registry path should be displayed
    // but we can set HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Applets\Regedit\LastKey
    // in the form (e.g.) "Computer\HKEY_CURRENT_USER\AppEvents\Schemes\Apps"
    // NOTE: the word "Computer" is localized as "Počítač" on Czech Win7, fortunately regedit.exe
    // does not require the word and the path may start directly at the HKEY_* root

    // store the current panel path into the LastKey value for RegEdit
    HKEY hKey;
    DWORD disp;
    RegCreateKeyExW(HKEY_CURRENT_USER, SAL_REG_KEY_REGEDIT_APPLET_W, 0, NULL,
                    REG_OPTION_NON_VOLATILE, KEY_CREATE_SUB_KEY | KEY_WRITE, NULL, &hKey, &disp);
    const std::wstring path = GetCurrentPathOwned();
    const wchar_t* path2 = path.c_str();
    if (*path2 == L'\\')
        path2++;
    RegSetValueExW(hKey, SAL_REG_VALUE_LAST_KEY_W, 0, REG_SZ, (const BYTE*)path2,
                   ((DWORD)wcslen(path2) + 1) * sizeof(WCHAR));
    RegCloseKey(hKey);

    // launch regedit with optional elevation (Vista/7)
    std::wstring regEditPath;
    SPLGetWindowsDirectoryOwned(regEditPath);
    SPLSalPathAppendOwned(regEditPath, L"regedit.exe");

    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = SG->GetMainWindowHWND();
    sei.lpFile = regEditPath.c_str();
    sei.lpParameters = L"";
    sei.lpDirectory = L"";
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei))
    {
        DWORD err = GetLastError();
        if (err != ERROR_CANCELLED)
            Error(IDS_PROCESS2, L"regedit.exe"); // %s in a wide format takes a wide arg
    }
}
