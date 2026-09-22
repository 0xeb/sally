// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "regedt_registry_enum.h"

namespace
{
void AppendRegistryPath(std::wstring& path, const wchar_t* more)
{
    if (more == NULL || *more == L'\0')
        return;
    if (!path.empty() && path.back() != L'\\' && *more != L'\\')
        path.push_back(L'\\');
    path.append(more);
}

bool CutRegistryDirectory(std::wstring& path, std::wstring* cutDirectory = nullptr)
{
    const size_t separator = path.find_last_of(L'\\');
    if (separator == std::wstring::npos)
    {
        if (path.empty())
            return false;
        if (cutDirectory != nullptr)
            *cutDirectory = path;
        path.clear();
        return true;
    }
    if (cutDirectory != nullptr)
        *cutDirectory = path.substr(separator + 1);
    path.resize(separator == 0 ? 1 : separator);
    return true;
}

bool ParseFullPathOwned(const std::wstring& path, std::wstring& keyName, int& keyRoot)
{
    if (path.empty())
        return false;
    if (path == L"\\")
    {
        keyName.clear();
        keyRoot = -1;
        return true;
    }
    const size_t rootEnd = path.find(L'\\', 1);
    const size_t rootLength = (rootEnd == std::wstring::npos ? path.size() : rootEnd) - 1;
    if (rootLength == 0)
        return false;
    for (int index = 0; PredefinedHKeys[index].HKey != NULL; ++index)
    {
        const wchar_t* candidate = PredefinedHKeys[index].KeyName;
        if (wcslen(candidate) == rootLength &&
            CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, candidate,
                           static_cast<int>(rootLength), path.c_str() + 1,
                           static_cast<int>(rootLength)) == CSTR_EQUAL)
        {
            keyRoot = index;
            keyName = rootEnd == std::wstring::npos ? L"" : path.substr(rootEnd + 1);
            return true;
        }
    }
    return false;
}
} // namespace

// ****************************************************************************
//
// CPluginFSInterface
//
//

CPluginFSInterface::CPluginFSInterface()
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::CPluginFSInterface()");
    FocusFirstNewItem = TRUE;

    PathError = FALSE;
    NewPath.clear();
    NewPathValid = FALSE;

    if (!ParseFullPathOwned(RecentFullPath, CurrentKeyName, CurrentKeyRoot))
        CurrentKeyRoot = -1;

    FirstChangePath = TRUE;
}

CPluginFSInterface::~CPluginFSInterface()
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::~CPluginFSInterface()");
    // stop monitoring for this FS instance
    ChangeMonitor.Cancel(this);
}

BOOL CPluginFSInterface::SetNewPath(const wchar_t* newPath)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::SetNewPath()");
    NewPath.assign(newPath != NULL ? newPath : L"");
    NewPathValid = TRUE;
    return TRUE;
}

BOOL WINAPI CPluginFSInterface::GetCurrentPath(CSalamanderStringBuffer* userPart)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::GetCurrentPath()");
    const std::wstring currentPath = GetCurrentPathOwned();
    return userPart != NULL &&
           sally::plugin_abi::WriteStringBuffer(*userPart, currentPath);
}

std::wstring CPluginFSInterface::GetCurrentPathOwned() const
{
    std::wstring currentPath = L"\\";
    if (CurrentKeyRoot == -1)
        return currentPath;
    currentPath.append(PredefinedHKeys[CurrentKeyRoot].KeyName);
    if (!CurrentKeyName.empty())
    {
        if (currentPath.back() != L'\\' && CurrentKeyName.front() != L'\\')
            currentPath.push_back(L'\\');
        currentPath.append(CurrentKeyName);
    }
    return currentPath;
}

BOOL CPluginFSInterface::GetFullName(CFileData& file, int isDir,
                                     CSalamanderStringBuffer* fullNameBuffer)
{
    CALL_STACK_MESSAGE2("CPluginFSInterface::GetFullName(, %d, )", isDir);
    CPluginData* pluginData = (CPluginData*)file.PluginData;
    std::wstring fullName = GetCurrentPathOwned();

    if (isDir == 2) // up-dir
    {
        if (!CutRegistryDirectory(fullName))
            return FALSE;
    }
    else
    {
        AppendRegistryPath(fullName, pluginData->Name != NULL && *pluginData->Name != 0 ?
                                         pluginData->Name :
                                         LoadStrW(IDS_DEFAULTVALUE).c_str());
    }
    return fullNameBuffer != NULL &&
           sally::plugin_abi::WriteStringBuffer(*fullNameBuffer, fullName);
}

BOOL CPluginFSInterface::ResolveFullFSPath(std::wstring& path, BOOL& success)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::ResolveFullFSPath()");
    success = FALSE;
    std::wstring row;
    if (!path.empty() && path.front() == L'\\')
        row = path;
    else
    {
        row = GetCurrentPathOwned();
        AppendRegistryPath(row, path.c_str());
    }

    std::wstring translated;
    size_t source = 0;
    for (;;)
    {
        size_t escape = row.find(L"\\.", source);
        int type = 0;
        while (escape != std::wstring::npos)
        {
            const size_t afterDot = escape + 2;
            if (afterDot == row.size() || row[afterDot] == L'\\')
            {
                type = 1;
                break;
            }
            const size_t afterDots = escape + 3;
            if (row[afterDot] == L'.' &&
                (afterDots == row.size() || row[afterDots] == L'\\'))
            {
                type = 2;
                break;
            }
            escape = row.find(L"\\.", afterDot);
        }
        if (escape == std::wstring::npos)
        {
            translated.append(row, source, std::wstring::npos);
            break;
        }
        translated.append(row, source, escape - source);
        if (type == 2 && !CutRegistryDirectory(translated))
            return Error(IDS_BADPATH), TRUE;
        source = escape + (type == 1 ? 2 : 3);
    }
    path.swap(translated);
    success = TRUE;
    return TRUE;
}

BOOL CPluginFSInterface::GetFullFSPath(HWND parent, const wchar_t* fsName,
                                       CSalamanderStringBuffer* pathBuffer,
                                       BOOL& success)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::GetFullFSPath()");
    PARENT(parent);
    success = FALSE;

    std::wstring path;
    if (pathBuffer == NULL ||
        !sally::plugin_abi::ReadStringBuffer(*pathBuffer, path))
        return FALSE;

    if (!path.empty() && path.front() == L'?')
    {
        TRACE_E("CPluginFSInterface::GetFullFSPath called with path '?'");
        return TRUE;
    }
    ResolveFullFSPath(path, success);
    if (!success)
        return TRUE;

    path.insert(0, L":");
    path.insert(0, fsName != NULL ? fsName : L"");
    success = sally::plugin_abi::WriteStringBuffer(*pathBuffer, path);
    return TRUE;
}

BOOL CPluginFSInterface::GetRootPath(CSalamanderStringBuffer* userPart)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::GetRootPath()");
    return userPart != NULL &&
           sally::plugin_abi::WriteStringBuffer(*userPart, std::wstring(L"\\"));
}

BOOL CPluginFSInterface::IsCurrentPath(int currentFSNameIndex, int fsNameIndex,
                                       const wchar_t* userPart)
{
    CALL_STACK_MESSAGE3("CPluginFSInterface::IsCurrentPath(%d, %d, )",
                        currentFSNameIndex, fsNameIndex);
    const wchar_t* comparePath = userPart != NULL && userPart[0] == L'?' &&
                                         !NewPath.empty() ?
                                     NewPath.c_str() :
                                     userPart;
    const std::wstring currentPath = GetCurrentPathOwned();
    return comparePath != NULL &&
           CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                          currentPath.c_str(), -1, comparePath, -1) == CSTR_EQUAL;
}

BOOL CPluginFSInterface::IsOurPath(int currentFSNameIndex, int fsNameIndex,
                                   const wchar_t* userPart)
{
    CALL_STACK_MESSAGE3("CPluginFSInterface::IsOurPath(%d, %d, )",
                        currentFSNameIndex, fsNameIndex);
    return TRUE;
}

BOOL ReportPathError(int err, int keyRoot, const std::wstring& path)
{
    CALL_STACK_MESSAGE3("ReportPathError(%d, %d, )", err, keyRoot);
    std::wstring fullPath(PredefinedHKeys[keyRoot].KeyName);
    AppendRegistryPath(fullPath, path.c_str());
    return ErrorL(err, IDS_OPENKEY, fullPath.c_str());
}

BOOL CPluginFSInterface::ChangePath(int currentFSNameIndex,
                                    CSalamanderStringBuffer* fsName, int fsNameIndex,
                                    const wchar_t* userPart,
                                    CSalamanderStringBuffer* cutFileName,
                                    BOOL* pathWasCut, BOOL forceRefresh, int mode)
{
    CALL_STACK_MESSAGE5("CPluginFSInterface::ChangePath(%d, , %d, , , , %d, %d)",
                        currentFSNameIndex, fsNameIndex, forceRefresh, mode);
    std::wstring fsNameValue;
    if (fsName == NULL || !sally::plugin_abi::ReadStringBuffer(*fsName, fsNameValue))
        return FALSE;
    (void)fsNameValue;
    BOOL firstChangePath = FirstChangePath;
    FirstChangePath = FALSE;

    FocusFirstNewItem = FALSE;
    if (pathWasCut != NULL)
        *pathWasCut = FALSE;
    if (cutFileName != NULL &&
        !sally::plugin_abi::WriteStringBuffer(*cutFileName, std::wstring()))
        return FALSE;

    // build the full path
    std::wstring path;
    if (userPart != NULL && userPart[0] == L'?' && NewPathValid)
    {
        NewPathValid = FALSE;
        path = NewPath;
    }
    else
        path.assign(userPart != NULL ? userPart : L"");
    BOOL success;
    ResolveFullFSPath(path, success);
    if (!success)
        return FALSE;

    std::wstring keyName;
    int keyRoot;
    HKEY openHKey;
    if (!ParseFullPathOwned(path, keyName, keyRoot))
        return Error(IDS_BADPATH);

    std::wstring cutFileNameValue;
    BOOL fileNameAlreadyCut = FALSE;
    BOOL errorReported = FALSE;
    LONG err = ERROR_SUCCESS;
    if (PathError) // listing failed, try enumerating the trimmed path
    {
        PathError = FALSE;
        if (!CutRegistryDirectory(keyName, &cutFileNameValue))
        {
            keyRoot = -1; // fall back to the root
            if (pathWasCut != NULL)
                *pathWasCut = TRUE;
            fileNameAlreadyCut = TRUE;
            cutFileNameValue.clear(); // it can no longer be a file name
        }
        else
        {
            fileNameAlreadyCut = TRUE;
            if (pathWasCut != NULL)
                *pathWasCut = TRUE;
        }
    }
    while (1)
    {
        int prevErr = err; // for mode 3 remember the previous error (on the untrimmed path)
        if (keyRoot == -1 ||
            (err = RegOpenKeyExW(PredefinedHKeys[keyRoot].HKey, keyName.c_str(), 0,
                                 KEY_READ, &openHKey)) == ERROR_SUCCESS)
        {
            // success, use the path as the current one
            if (keyRoot != -1)
            {
                if (cutFileName != NULL && !cutFileNameValue.empty())
                {
                    if (RegQueryValueExW(openHKey, cutFileNameValue.c_str(), 0, NULL,
                                         NULL, NULL) != ERROR_SUCCESS)
                    {
                        if (mode == 3)
                        {
                            // report the error and abort
                            RegCloseKey(openHKey);
                            AppendRegistryPath(keyName, cutFileNameValue.c_str());
                            return ReportPathError(prevErr, keyRoot, keyName);
                        }
                        cutFileNameValue.clear();
                    }
                }
                if (keyName.empty())
                    ChangeMonitor.IgnoreNextRootChange(keyRoot);
                RegCloseKey(openHKey);
            }
            else
                cutFileNameValue.clear();
            CurrentKeyName = keyName;
            CurrentKeyRoot = keyRoot;
            if (cutFileName != NULL &&
                !sally::plugin_abi::WriteStringBuffer(*cutFileName, cutFileNameValue))
                return FALSE;

            RecentFullPath = GetCurrentPathOwned();

            ChangeMonitor.Cancel(this);
            return TRUE;
        }
        else // failure, try to shorten the path
        {
            if (mode == 1 && err != ERROR_FILE_NOT_FOUND ||
                mode == 2)
            {
                // report the error and keep shortening
                if (!errorReported)
                {
                    if (!firstChangePath)
                        ReportPathError(err, keyRoot, keyName);
                    errorReported = TRUE;
                }
            }
            else
            {
                // report the error and stop
                if (mode == 3 && (fileNameAlreadyCut || keyName.empty()))
                    return ReportPathError(err, keyRoot, keyName);
            }

            if (!CutRegistryDirectory(keyName, &cutFileNameValue)) // nothing left to shorten
            {
                keyRoot = -1; // fall back to the root
                if (pathWasCut != NULL)
                    *pathWasCut = TRUE;
                fileNameAlreadyCut = TRUE;
                cutFileNameValue.clear(); // it can no longer be a file name
            }
            else
            {
                if (pathWasCut != NULL)
                    *pathWasCut = TRUE;
                if (!fileNameAlreadyCut) // it can be a file name only during the first trimming
                {
                    fileNameAlreadyCut = TRUE;
                }
                else
                {
                    cutFileNameValue.clear(); // it can no longer be a file name
                }
            }
        }
    }

    return FALSE;
}

const wchar_t* Bin2ASCII = L"0123456789abcdef";

void PrintHexValue(unsigned char* data, int size, wchar_t* buffer, int bufSize)
{
    CALL_STACK_MESSAGE3("PrintHexValue(, %d, , %d)", size, bufSize);
    int i;
    for (i = 0; i < min(size, (bufSize + 1) / 3); i++)
    {
        buffer[i * 3] = Bin2ASCII[data[i] >> 4];
        buffer[i * 3 + 1] = Bin2ASCII[data[i] & 0x0F];
        if (i + 1 < min(size, (bufSize + 1) / 3))
            buffer[i * 3 + 2] = L' ';
    }
    if (i != size)
    {
        // doesn't fit, append an ellipsis
        wmemcpy(buffer + bufSize - 3, L"...", 3);
    }
}

namespace
{
class RegistryKeyOwner
{
    HKEY Key;

public:
    explicit RegistryKeyOwner(HKEY key) : Key(key) {}
    ~RegistryKeyOwner()
    {
        if (Key != nullptr)
            RegCloseKey(Key);
    }
};

bool ShowEnumerationError(LONG error)
{
    const std::wstring message = SPLFormatStringOwned(
        LoadStrW(IDS_ENUMKEY).c_str(), SPLGetErrorTextOwned(SG, error).c_str());
    return SG->SalMessageBox(SG->GetMainWindowHWND(), message.c_str(),
                             LoadStrW(IDS_REGEDTERR).c_str(),
                             MB_YESNO | MB_ICONHAND) == IDYES;
}
} // namespace

BOOL CPluginFSInterface::ListCurrentPath(CSalamanderDirectoryAbstract* dir,
                                         CPluginDataInterfaceAbstract*& pluginData,
                                         int& iconsType, BOOL forceRefresh)
{
    pluginData = nullptr;
    try
    {
        return ListCurrentPathCore(dir, pluginData, iconsType, forceRefresh);
    }
    catch (...)
    {
        if (pluginData != nullptr)
        {
            dir->Clear(pluginData);
            delete pluginData;
            pluginData = nullptr;
        }
        return FALSE;
    }
}

BOOL CPluginFSInterface::ListCurrentPathCore(CSalamanderDirectoryAbstract* dir,
                                             CPluginDataInterfaceAbstract*& pluginData,
                                             int& iconsType, BOOL forceRefresh)
{
    CALL_STACK_MESSAGE2("CPluginFSInterface::ListCurrentPath(, , , %d)",
                        forceRefresh);
    CFileData file;
    FILETIME time;

    dir->Clear(NULL);
    dir->SetValidData(VALID_DATA_DATE | VALID_DATA_TIME | VALID_DATA_SIZE);

    pluginData = new CPluginDataInterface();

    if (CurrentKeyRoot == -1)
    {
        int i = 0;
        while (PredefinedHKeys[i].HKey != NULL)
        {
            if (RegQueryInfoKeyW(PredefinedHKeys[i].HKey, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, &time) == ERROR_SUCCESS)
            {
                file.Name = SG->DupStr(PredefinedHKeys[i].KeyName);
                file.NameLen = static_cast<DWORD>(wcslen(file.Name));
                file.Size = CQuadWord(0, 0);
                FileTimeToLocalFileTime(&time, &file.LastWrite);
                file.PluginData = (DWORD_PTR) new CPluginData(DupStr(PredefinedHKeys[i].KeyName), 0);
                dir->AddDir(NULL, file, pluginData);
            }
            i++;
        }

        iconsType = pitFromPlugin;

        return TRUE;
    }

    PathError = TRUE;

    HKEY openHKey;
    LONG err;
    BOOL errorReported = FALSE;
    if ((err = RegOpenKeyExW(PredefinedHKeys[CurrentKeyRoot].HKey,
                             CurrentKeyName.c_str(), 0, KEY_READ,
                             &openHKey)) == ERROR_SUCCESS)
    {
        RegistryKeyOwner openKeyOwner(openHKey);
        DWORD subKeys;
        DWORD maxSubKeyLen;
        DWORD values;
        DWORD maxValueNameLen;
        DWORD maxValueLen;
        if ((err = RegQueryInfoKeyW(openHKey, NULL, NULL, NULL, &subKeys, &maxSubKeyLen, NULL,
                                    &values, &maxValueNameLen, &maxValueLen, NULL, &time)) == ERROR_SUCCESS)
        {
            // add the parent directory entry
            file.Name = SG->DupStr(L"..");
            file.NameLen = static_cast<DWORD>(wcslen(file.Name));
            FileTimeToLocalFileTime(&time, &file.LastWrite);
            file.PluginData = (DWORD_PTR) new CPluginData(DupStr(L".."), 0);
            dir->AddDir(NULL, file, pluginData);

            // enumerate subkeys
            std::wstring name;
            DWORD i;
            for (i = 0; i < subKeys; i++)
            {
                if ((err = RegedtEnumerateSubKeyOwned(openHKey, i, maxSubKeyLen,
                                                      name, time)) == ERROR_SUCCESS)
                {
                    file.Name = SG->DupStr(name.c_str());
                    file.NameLen = static_cast<DWORD>(wcslen(file.Name));
                    file.Size = CQuadWord(0, 0);
                    FileTimeToLocalFileTime(&time, &file.LastWrite);
                    file.PluginData = (DWORD_PTR) new CPluginData(DupStr(name.c_str()), 0);
                    dir->AddDir(NULL, file, pluginData);
                }
                else
                {
                    if (!errorReported)
                    {
                        errorReported = TRUE;
                        if (!ShowEnumerationError(err))
                            break;
                    }
                }
            }

            // enumerate values only if enumerating subkeys succeeded
            if (i >= subKeys)
            {
                time.dwLowDateTime = 0;
                time.dwHighDateTime = 0;
                DWORD type;
                DWORD size;
                BOOL hasDefault = FALSE;
                std::vector<BYTE> data;
                for (i = 0; i < values; i++)
                {
                    if ((err = RegedtEnumerateValueOwned(openHKey, i, maxValueNameLen, maxValueLen,
                                                         name, type, data, size)) == ERROR_SUCCESS)
                    {
                        if (name.empty())
                        {
                            hasDefault = TRUE;
                            file.Name = SG->DupStr(LoadStrW(IDS_DEFAULTVALUE).c_str());
                        }
                        else
                        {
                            file.Name = SG->DupStr(name.c_str());
                        }
                        file.NameLen = static_cast<DWORD>(wcslen(file.Name));
                        file.Size = CQuadWord(size, 0);
                        file.LastWrite = time;
                        CPluginData* pd = new CPluginData(DupStr(name.c_str()), type);
                        file.PluginData = (DWORD_PTR)pd;
                        switch (type)
                        {
                        case REG_MULTI_SZ:
                        {
                            // replace separator NULL characters with spaces
                            WCHAR* ptr = reinterpret_cast<WCHAR*>(data.data());
                            while (ptr < reinterpret_cast<WCHAR*>(data.data()) + min(size / 2, MAX_DATASIZE))
                            {
                                if (*ptr == L'\0')
                                {
                                    if (ptr + 2 == reinterpret_cast<WCHAR*>(data.data()) + size / 2)
                                    {
                                        size -= 2; // ignore the last '\0'; there are two of them
                                        break;
                                    }
                                    else
                                    {
                                        *ptr = L' ';
                                    }
                                }
                                ptr++;
                            }
                            // continue processing
                        }
                        case REG_EXPAND_SZ:
                        case REG_SZ:
                            pd->DataSize = (unsigned char)min(size / 2, MAX_DATASIZE);
                            if (pd->DataSize)
                            {
                                void* preview = malloc(pd->DataSize * sizeof(WCHAR));
                                if (preview != nullptr)
                                {
                                    pd->Allocated = 1;
                                    pd->Data = reinterpret_cast<DWORD_PTR>(preview);
                                    wmemcpy(static_cast<WCHAR*>(preview), reinterpret_cast<WCHAR*>(data.data()), pd->DataSize);
                                    if (pd->DataSize < size / 2)
                                        wcscpy(static_cast<WCHAR*>(preview) + pd->DataSize - 4, L"...");
                                }
                                else
                                    pd->DataSize = 0;
                            }
                            break;

                        case REG_DWORD:
                            if (size >= sizeof(DWORD))
                            {
                                DWORD value;
                                memcpy(&value, data.data(), sizeof(value));
                                pd->Data = value;
                            }
                            break;

                        case REG_DWORD_BIG_ENDIAN:
                            if (size >= sizeof(DWORD))
                                pd->Data = (DWORD)data[3] | (data[2] << 8) | (data[1] << 16) | (data[0] << 24);
                            break;

                        case REG_QWORD:
                            if (size >= sizeof(ULONGLONG))
                            {
                                void* preview = malloc(sizeof(ULONGLONG));
                                if (preview != nullptr)
                                {
                                    pd->Allocated = 1;
                                    pd->Data = reinterpret_cast<DWORD_PTR>(preview);
                                    memcpy(preview, data.data(), sizeof(ULONGLONG));
                                }
                            }
                            break;

                        default:
                            if (size > 0)
                            {
                                const DWORD previewChars = size > (MAX_DATASIZE + 1) / 3
                                                               ? MAX_DATASIZE
                                                               : size * 3 - 1;
                                pd->DataSize = static_cast<unsigned char>(previewChars);
                                void* preview = malloc(pd->DataSize * sizeof(WCHAR));
                                if (preview != nullptr)
                                {
                                    pd->Allocated = 1;
                                    pd->Data = reinterpret_cast<DWORD_PTR>(preview);
                                    PrintHexValue(data.data(), size, static_cast<WCHAR*>(preview), pd->DataSize);
                                }
                                else
                                    pd->DataSize = 0;
                            }
                        }
                        dir->AddFile(NULL, file, pluginData);
                    }
                    else
                    {
                        if (!errorReported)
                        {
                            errorReported = TRUE;
                            if (!ShowEnumerationError(err))
                                break;
                        }
                    }
                }
                if (i >= values)
                {
                    if (!hasDefault)
                    {
                        file.Name = SG->DupStr(LoadStrW(IDS_DEFAULTVALUE).c_str());
                        file.NameLen = static_cast<DWORD>(wcslen(file.Name));
                        file.Size = CQuadWord(0, 0);
                        file.LastWrite = time;
                        file.PluginData = (DWORD_PTR) new CPluginData(NULL, REG_NONE);
                        dir->AddFile(NULL, file, pluginData);
                    }
                    PathError = FALSE; // success
                }
            }
        }
        if (CurrentKeyName.empty())
            ChangeMonitor.IgnoreNextRootChange(CurrentKeyRoot);
    }

    if (PathError)
    {
        dir->Clear(pluginData);
        delete pluginData;
        pluginData = nullptr;
        return !errorReported && ErrorL(err, IDS_OPENKEY2,
                                        PredefinedHKeys[CurrentKeyRoot].KeyName,
                                        CurrentKeyName.c_str());
    }

    iconsType = pitFromPlugin;

    // register the new path with the change monitor
    ChangeMonitor.AddPath(CurrentKeyRoot, CurrentKeyName.data(), this);

    return TRUE;
}
