// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "spl_fs.h"

// The expression engine owns UTF-8 strings. The registry owns UTF-16 strings.
BOOL SetValueSZ(CSalamanderRegistryAbstract* registry, HKEY regKey, const wchar_t* name, const char* narrowValue)
{
    std::wstring wide = RenamerTextToWide(narrowValue);
    return SPLRegistrySetString(registry, regKey, name, wide);
}

BOOL GetValueSZ(CSalamanderRegistryAbstract* registry, HKEY regKey, const wchar_t* name, char* narrowBuf, int narrowBufSize)
{
    std::string value;
    if (!GetValueSZ(registry, regKey, name, value) || static_cast<int>(value.size()) >= narrowBufSize)
        return FALSE;
    memcpy(narrowBuf, value.c_str(), value.size() + 1);
    return TRUE;
}

BOOL GetValueSZ(CSalamanderRegistryAbstract* registry, HKEY regKey, const wchar_t* name, std::string& value)
{
    std::wstring wide;
    if (!SPLRegistryGetStringOwned(registry, regKey, name, wide))
        return FALSE;
    value = WideToRenamerText(wide.c_str());
    return TRUE;
}

void LoadHistory(HKEY regKey, const char* keyPattern, char** history,
                 CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE2("LoadHistory(, %s, , )", keyPattern);
    char buf[32];
    int i;
    for (i = 0; i < MAX_HISTORY_ENTRIES; i++)
    {
        SalPrintf(buf, 32, keyPattern, i);
        const std::wstring valueNameW = RenamerTextToWide(buf);
        std::string value;
        if (!GetValueSZ(registry, regKey, valueNameW.c_str(), value))
            break;
        char* ptr = _strdup(value.c_str());
        if (!ptr)
            break;
        history[i] = ptr;
    }
}

void SaveHistory(HKEY regKey, const char* keyPattern, char** history, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE2("SaveHistory(, %s, , )", keyPattern);
    char buf[32];
    int i;
    for (i = 0; i < MAX_HISTORY_ENTRIES; i++)
    {
        if (history[i] == NULL)
            break;
        SalPrintf(buf, 32, keyPattern, i);
        const std::wstring valueNameW = RenamerTextToWide(buf);
        SetValueSZ(registry, regKey, valueNameW.c_str(), history[i]);
    }
}

BOOL FileError(HWND parent, const wchar_t* fileName, int error,
               BOOL retry, BOOL* skip, BOOL* skipAll, int title)
{
    CALL_STACK_MESSAGE_NONE
    int err = GetLastError();
    CALL_STACK_MESSAGE1("FileError()");

    std::wstring buffer = LangStr(error).c_str();
    if (err != NO_ERROR)
        buffer += SPLGetErrorTextOwned(SG, err);

    if (skipAll && *skipAll)
    {
        if (skip)
            *skip = TRUE;
        return FALSE;
    }

    int ret;
    if (retry)
    {
        if (skip)
            ret = SG->DialogError(parent, BUTTONS_RETRYSKIPCANCEL, fileName, buffer.c_str(), LangStr(title).c_str());
        else
            ret = SG->DialogError(parent, BUTTONS_RETRYCANCEL, fileName, buffer.c_str(), LangStr(title).c_str());
    }
    else
    {
        if (skip)
            ret = SG->DialogError(parent, BUTTONS_SKIPCANCEL, fileName, buffer.c_str(), LangStr(title).c_str());
        else
            ret = SG->DialogError(parent, BUTTONS_OK, fileName, buffer.c_str(), LangStr(title).c_str());
    }

    switch (ret)
    {
    case DIALOG_RETRY:
        return TRUE;

    case DIALOG_SKIPALL:
        if (skipAll)
            *skipAll = TRUE;

    case DIALOG_SKIP:
        if (skip)
            *skip = TRUE;
        return FALSE;

    //case DIALOG_OK:
    //case DIALOG_CANCEL:
    default:
        if (skip)
            *skip = FALSE;
        return FALSE;
    }
}

std::wstring GetFileData(const wchar_t* file)
{
    CALL_STACK_MESSAGE2("GetFileData(%ls)", file);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(file, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return SPLGetErrorTextOwned(SG, GetLastError());

    SYSTEMTIME st;
    FILETIME ft;
    FileTimeToLocalFileTime(&fd.ftLastWriteTime, &ft);
    FileTimeToSystemTime(&ft, &st);

    const auto formatTime = [&st]()
    {
        const int needed = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, NULL, 0);
        if (needed > 0)
        {
            std::vector<wchar_t> value(static_cast<size_t>(needed));
            if (GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, value.data(), needed) > 0)
                return std::wstring(value.data());
        }
        return std::to_wstring(st.wHour) + L":" +
               (st.wMinute < 10 ? L"0" : L"") + std::to_wstring(st.wMinute) + L":" +
               (st.wSecond < 10 ? L"0" : L"") + std::to_wstring(st.wSecond);
    };
    const auto formatDate = [&st]()
    {
        const int needed = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, NULL, 0);
        if (needed > 0)
        {
            std::vector<wchar_t> value(static_cast<size_t>(needed));
            if (GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, value.data(), needed) > 0)
                return std::wstring(value.data());
        }
        return std::to_wstring(st.wDay) + L"." + std::to_wstring(st.wMonth) + L"." +
               std::to_wstring(st.wYear);
    };

    const std::wstring number = fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY
                                    ? LangStr(IDS_DIRTEXT)
                                    : SPLNumberToStrOwned(SG, CQuadWord(fd.nFileSizeLow, fd.nFileSizeHigh));
    const std::wstring result = number + L", " + formatDate() + L", " + formatTime();
    FindClose(h);
    return result;
}

BOOL FileOverwrite(HWND parent, const wchar_t* fileName1, const wchar_t* fileData1,
                   const wchar_t* fileName2, const wchar_t* fileData2, DWORD attr,
                   int shquestion, int shtitle, BOOL* skip, DWORD* silent)
{
    CALL_STACK_MESSAGE8("FileOverwrite(, %ls, %ls, %ls, %ls, 0x%X, %d, %d, , )",
                        fileName1, fileData1, fileName2, fileData2, attr,
                        shquestion, shtitle);
    std::wstring ownedData1;
    std::wstring ownedData2;
    if (!fileData1)
    {
        ownedData1 = GetFileData(fileName1);
        fileData1 = ownedData1.c_str();
    }
    if (!fileData2)
    {
        ownedData2 = GetFileData(fileName2);
        fileData2 = ownedData2.c_str();
    }

    if (!silent || !(*silent & SILENT_OVERWRITE_FILE_EXIST))
    {
        if (silent && (*silent & SILENT_SKIP_FILE_EXIST))
        {
            if (skip)
                *skip = TRUE;
            return FALSE;
        }

        int ret;
        if (skip)
            ret = SG->DialogOverwrite(parent, BUTTONS_YESALLSKIPCANCEL, fileName1, fileData1, fileName2, fileData2);
        else
            ret = SG->DialogOverwrite(parent, BUTTONS_YESNOCANCEL, fileName1, fileData1, fileName2, fileData2);

        switch (ret)
        {
        case DIALOG_ALL:
            if (silent)
                *silent |= SILENT_OVERWRITE_FILE_EXIST;

        case DIALOG_YES:
            break; // we still need to verify hidden/system overwrite

        case DIALOG_SKIPALL:
            if (silent)
                *silent |= SILENT_SKIP_FILE_EXIST;

        case DIALOG_SKIP:
            if (skip)
                *skip = TRUE;
            return FALSE;

            //case DIALOG_OK:
            //case DIALOG_CANCEL:
        default:
            if (skip)
                *skip = FALSE;
            return FALSE;
        }
    }

    // also check that we are not overwriting a hidden/system directory
    if (attr == -1)
        attr = SG->SalGetFileAttributes(fileName1);
    if ((attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) == 0)
        return TRUE;

    if (silent && (*silent & SILENT_OVERWRITE_FILE_SYSHID))
    {
        return TRUE;
    }

    if (silent && (*silent & SILENT_SKIP_FILE_SYSHID))
    {
        if (skip)
            *skip = TRUE;
        return FALSE;
    }

    int ret;
    if (skip)
        ret = SG->DialogQuestion(parent, BUTTONS_YESALLSKIPCANCEL, fileName1, LangStr(shquestion).c_str(), LangStr(shtitle).c_str());
    else
        ret = SG->DialogQuestion(parent, BUTTONS_YESNOCANCEL, fileName1, LangStr(shquestion).c_str(), LangStr(shtitle).c_str());

    switch (ret)
    {
    case DIALOG_ALL:
        if (silent)
            *silent |= SILENT_OVERWRITE_FILE_SYSHID;

    case DIALOG_YES:
        return TRUE;

    case DIALOG_SKIPALL:
        if (silent)
            *silent |= SILENT_SKIP_FILE_SYSHID;

    case DIALOG_SKIP:
        if (skip)
            *skip = TRUE;
        return FALSE;

        //case DIALOG_OK:
        //case DIALOG_CANCEL:
    default:
        if (skip)
            *skip = FALSE;
        return FALSE;
    }
}

// ****************************************************************************
//
// CBuffer
//

BOOL CBuffer::Reserve(size_t size)
{
    CALL_STACK_MESSAGE2("CBuffer::Reserve(%Iu)", size);
    if (size > Allocated)
    {
        size_t s = max(Allocated * 2, size);
        if (Persistent)
        {
            void* ptr = realloc(Buffer, s);
            if (!ptr)
                return FALSE;
            Buffer = ptr;
        }
        else
        {
            Release();
            Buffer = malloc(s);
            if (!Buffer)
                return FALSE;
        }
        Allocated = s;
    }
    return TRUE;
}

// ****************************************************************************

CGUIMenuPopupAbstract*
CreateVarStrHelpMenu(CVarStrHelpMenuItem* helpMenu, int& id)
{
    CALL_STACK_MESSAGE2("CreateVarStrHelpMenu(, %d)", id);
    CGUIMenuPopupAbstract* ret = SalGUI->CreateMenuPopup();
    if (!ret)
        return NULL;

    MENU_ITEM_INFO mii;
    int i;
    for (i = 0; helpMenu[i].MenuItemStringID; i++)
    {
        // Declared out here so it outlives the InsertItem below.
        //
        // LangStr returns BY VALUE, where the LoadStr it replaced returned a
        // pointer into a static cyclic buffer that stayed valid across the call.
        // Assigning .c_str() of the temporary directly to mii.String left it
        // dangling at the end of that statement, and InsertItem then read freed
        // memory - so every variable-help menu item was labelled with whatever
        // was left in that allocation.
        std::wstring itemText;
        if (helpMenu[i].MenuItemStringID == -1)
        {
            mii.Mask = MENU_MASK_TYPE;
            mii.Type = MNTT_SP;
        }
        else
        {
            mii.Mask = MENU_MASK_TYPE | MENU_MASK_ID | MENU_MASK_SUBMENU |
                       MENU_MASK_STRING | MENU_MASK_CUSTOMDATA;
            mii.Type = MNTT_IT;
            mii.ID = id++;
            mii.SubMenu = helpMenu[i].SubMenu ? CreateVarStrHelpMenu(helpMenu[i].SubMenu, id) : NULL;
            itemText = LangStr(helpMenu[i].MenuItemStringID);
            mii.String = const_cast<LPWSTR>(itemText.c_str());
            mii.CustomData = (ULONG_PTR)&helpMenu[i];
        }
        ret->InsertItem(i, TRUE, &mii);
    }

    return ret;
}

CVarStrHelpMenuItem*
FindItem(CGUIMenuPopupAbstract* menu, DWORD id)
{
    CALL_STACK_MESSAGE2("FindItem(, 0x%X)", id);
    MENU_ITEM_INFO mii;
    int count = menu->GetItemCount();
    int i;
    for (i = 0; i < count; i++)
    {
        mii.Mask = MENU_MASK_CUSTOMDATA | MENU_MASK_ID | MENU_MASK_SUBMENU;
        if (menu->GetItemInfo(i, TRUE, &mii))
        {
            if (mii.ID == id)
                return (CVarStrHelpMenuItem*)mii.CustomData;
            if (mii.SubMenu)
            {
                CVarStrHelpMenuItem* ret = FindItem(mii.SubMenu, id);
                if (ret)
                    return ret;
            }
        }
    }
    return NULL;
}

BOOL SelectVarStrVariable(HWND parent, int x, int y,
                          CVarStrHelpMenuItem* helpMenu, char* buffer, BOOL varStr)
{
    CALL_STACK_MESSAGE4("SelectVarStrVariable(, %d, %d, , , %d)", x, y, varStr);
    int id = 1;
    CGUIMenuPopupAbstract* menu = CreateVarStrHelpMenu(helpMenu, id);

    if (!menu)
        return FALSE;

    BOOL ret = FALSE;
    DWORD cmd = menu->Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON | MENU_TRACK_NONOTIFY,
                            x, y, parent, NULL);
    if (cmd > 0)
    {
        CVarStrHelpMenuItem* item = FindItem(menu, cmd);
        const char *strStart = "", *strEnd = "";
        if (varStr)
        {
            strStart = "$(";
            strEnd = ")";
        }
        if (item)
        {
            char param[1024];
            if (item->FParameterGetValue)
            {
                if (item->FParameterGetValue(parent, param))
                {
                    if (item->VariableName)
                    {
                        if (strlen(param))
                            SalPrintf(buffer, 1024, "%s%s:%s%s", strStart, item->VariableName, param, strEnd);
                        else
                            SalPrintf(buffer, 1024, "%s%s%s", strStart, item->VariableName, strEnd);
                    }
                    else
                        SalPrintf(buffer, 1024, "%s%s%s", strStart, param, strEnd);
                    ret = TRUE;
                }
            }
            else
            {
                SalPrintf(buffer, 1024, "%s%s%s", strStart, item->VariableName, strEnd);
                ret = TRUE;
            }
        }
    }

    SalGUI->DestroyMenuPopup(menu);

    return ret;
}

// ****************************************************************************

const char*
StrQChr(const char* start, const char* end, char q, char c)
{
    CALL_STACK_MESSAGE_NONE
    BOOL quote = FALSE;
    while (start != end)
    {
        if (*start == q)
            quote = !quote;
        if (*start == c && !quote)
            break;
        start++;
    }
    return start;
}

BOOL IsValidInt(const char* begin, const char* end, BOOL isSigned)
{
    CALL_STACK_MESSAGE_NONE
    if (isSigned && *begin == '-' && begin < end)
        begin++;
    if (begin >= end)
        return FALSE;
    while (begin < end)
    {
        if (!IsDigit(*begin))
            return FALSE;
        begin++;
    }
    return TRUE;
}

BOOL IsValidFloat(const char* begin, const char* end)
{
    CALL_STACK_MESSAGE_NONE
    if (*begin == '-' && begin < end)
        begin++;
    if (begin >= end ||
        end - begin == 1 && *begin == '.')
        return FALSE;

    // cela cast
    while (begin < end)
    {
        if (!IsDigit(*begin))
            break;
        begin++;
    }
    if (begin < end)
    {
        // decimal point
        if (*begin != '.')
            return FALSE;

        // fractional part
        while (begin < end)
        {
            if (!IsDigit(*begin))
                FALSE;
            begin++;
        }
    }
    return TRUE;
}

int GetRegExpErrorID(CRegExpErrors err)
{
    CALL_STACK_MESSAGE_NONE
    switch (err)
    {
    case reeNoError:
        return IDS_REENOERROR;
    case reeLowMemory:
        return IDS_REELOWMEMORY;
    case reeEmpty:
        return IDS_REEEMPTY;
    case reeTooBig:
        return IDS_REETOOBIG;
    case reeTooManyParenthesises:
        return IDS_REETOOMANYPARENTHESISES;
    case reeUnmatchedParenthesis:
        return IDS_REEUNMATCHEDPARENTHESIS;
    case reeOperandCouldBeEmpty:
        return IDS_REEOPERANDCOULDBEEMPTY;
    case reeNested:
        return IDS_REENESTED;
    case reeUnmatchedBracket:
        return IDS_REEUNMATCHEDBRACKET;
    case reeFollowsNothing:
        return IDS_REEFOLLOWSNOTHING;
    case reeTrailingBackslash:
        return IDS_REETRAILINGBACKSLASH;
    case reeInternalDisaster:
        return IDS_REEINTERNALDISASTER;
    case reeExpectingExtendedPattern1:
        return IDS_REEEXPECTINGEXTENDEDPATTERN1;
    case reeExpectingExtendedPattern2:
        return IDS_REEEXPECTINGEXTENDEDPATTERN2;
    case reeInvalidPosixClass:
        return IDS_REEINVALIDPOSIXCLASS;
    case reeExpectingXDigit:
        return IDS_REEEXPECTINGXDIGIT;
    case reeStackOverflow:
        return IDS_REESTACKOVERFLOW;
    case reeNoPattern:
        return IDS_REENOPATTERN;
    case reeErrorStartingThread:
        return IDS_REEERRORSTARTINGTHREAD;
    case reeBadFixedWidthLookBehind:
        return IDS_REEBADFIXEDWIDTHLOOKBEHIND;
    default:
        return IDS_ERROR;
    }
}

wchar_t* StripRoot(wchar_t* path, size_t rootLen)
{
    CALL_STACK_MESSAGE_NONE
    const size_t length = wcslen(path);
    wchar_t* ret = path + (std::min)(rootLen, length);
    if (*ret == L'\\' && rootLen)
        ret++;
    return ret;
}

BOOL IsValidFileNameComponent(const wchar_t* start, const wchar_t* end)
{
    CALL_STACK_MESSAGE_NONE
    // ignore dots at the end of the name
    while (end > start && end[-1] == L'.')
        end--;
    const std::wstring component(start, end);
    return SG->SalIsValidFileNameComponent(component.c_str());
}

BOOL IsValidRelativePath(const wchar_t* name, int len)
{
    CALL_STACK_MESSAGE_NONE
    const wchar_t* nameEnd = name + len;
    const wchar_t* start = name;
    const wchar_t* end;
    do
    {
        end = GetNextPathComponent(start);
        if (!IsValidFileNameComponent(start, end))
            return FALSE;
        start = end + 1;
    } while (start <= nameEnd);
    return TRUE;
}

BOOL IsValidFullPath(const wchar_t* name, int len)
{
    CALL_STACK_MESSAGE_NONE
    int i = 0;

    // validujem drive/unc root
    if (name[0] == L'\\' && name[1] == L'\\') // UNC
    {
        i += 2;
        // server name
        if (name[i] == L'\\')
            return FALSE;
        while (name[i] != 0 && name[i] != L'\\')
            i++;
        if (name[i] != L'\\')
            return FALSE;
        i++;
        // share name
        if (name[i] == L'\\')
            return FALSE;
        while (name[i] != 0 && name[i] != L'\\')
            i++;
        if (name[i] != L'\\')
            return FALSE;
        i++;
    }
    else
    {
        if (!iswalpha(name[0]) || name[1] != L':' || name[2] != L'\\')
            return FALSE;
        i = 3;
    }

    return IsValidRelativePath(name + i, len - i);
}

BOOL ValidateFileName(const wchar_t* name, int len, CRenameSpec spec,
                      BOOL* skip, BOOL* skipAll)
{
    CALL_STACK_MESSAGE_NONE
    int err = 0;
    switch (spec)
    {
    case rsFileName:
        if (!IsValidFileNameComponent(name, name + len))
            err = IDS_NOTVALID_FILENAME;
        break;
    case rsRelativePath:
        if (!IsValidRelativePath(name, len))
            err = IDS_NOTVALID_RELATIVEPATH;
        break;
    case rsFullPath:
        if (!IsValidFullPath(name, len))
            err = IDS_NOTVALID_FULLPATH;
        break;
    }
    return err == 0 ||
           skip != NULL && FileError(GetParent(), name, err, FALSE, skip, skipAll, IDS_ERROR);
}

int CutTrailingDots(wchar_t* name, int len, CRenameSpec spec)
{
    CALL_STACK_MESSAGE_NONE
    wchar_t* nameEnd = name + len;
    // skip the root
    if (spec == rsFullPath)
    {
        if (name[0] == L'\\' && name[1] == L'\\') // UNC
        {
            name += 2;
            while (*name != 0 && *name != L'\\')
                name++;
            if (*name != 0)
                name++; // '\\'
            while (*name != 0 && *name != L'\\')
                name++;
            name++;
        }
        else
            name += 3;
    }
    wchar_t *start = nameEnd, *end;
    while (start > name)
    {
        end = start;
        while (start > name && start[-1] == L'.')
            start--;
        memmove(start, end, (nameEnd - end + 1) * sizeof(wchar_t));
        len -= (int)(end - start);
        nameEnd -= end - start;
        while (start > name && *--start != L'\\')
            ;
    }
    return len;
}

char* Replace(char* string, char s, char d)
{
    CALL_STACK_MESSAGE3("Replace(, %u, %u)", s, d);
    char* iterator = string;
    while (*iterator)
    {
        if (*iterator == s)
            *iterator = d;
        iterator++;
    }
    return string;
}
BOOL ShowOpenFileDialog(HWND parent, const wchar_t* title, const wchar_t* filter,
                        std::wstring& fileName)
{
    CALL_STACK_MESSAGE3("ShowOpenFileDialog(, %ls, %ls)", title, filter);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST;

    std::vector<wchar_t> packedFilter(filter, filter + wcslen(filter) + 1);
    packedFilter.push_back(L'\0');
    std::replace(packedFilter.begin(), packedFilter.end(), L'\t', L'\0');
    ofn.lpstrFilter = packedFilter.data();
    ofn.lpstrTitle = title;

    std::vector<std::wstring> files(1, fileName);
    if (!SPLSafeGetOpenFileNamesOwned(SG, &ofn, files) || files.size() != 1)
        return FALSE;
    fileName = std::move(files[0]);
    return TRUE;
}
