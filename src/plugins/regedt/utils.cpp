// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

WCHAR*
DupStr(const WCHAR* str)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("DupStr()");
    WCHAR* ret;
    int len = (int)(wcslen(str) + 1) * 2;
    ret = (WCHAR*)SG->Alloc(len);
    if (ret)
        memcpy(ret, str, len);
    return ret;
}

BOOL RegOperationError(int lastError, int error, int title, int keyRoot,
                       const wchar_t* keyName,
                       LPBOOL skip, LPBOOL skipAllErrors)
{
    CALL_STACK_MESSAGE5("RegOperationError(%d, %d, %d, %d, , , )", lastError,
                        error, title, keyRoot);
    if (skipAllErrors && *skipAllErrors)
    {
        if (skip)
            *skip = TRUE;
        return FALSE;
    }

    std::wstring errorText = LoadStrW(error).c_str();
    errorText += SPLGetErrorTextOwned(SG, lastError);

    std::wstring fullName = PredefinedHKeys[keyRoot].KeyName;
    fullName += L'\\';
    fullName += keyName;

    int res = skip ? SG->DialogError(GetParent(), BUTTONS_RETRYSKIPCANCEL, fullName.c_str(), errorText.c_str(), LoadStrW(title).c_str()) : SG->DialogError(GetParent(), BUTTONS_RETRYCANCEL, fullName.c_str(), errorText.c_str(), LoadStrW(title).c_str());
    switch (res)
    {
    case DIALOG_RETRY:
        return TRUE;

    case DIALOG_SKIPALL:
        *skipAllErrors = TRUE;
    case DIALOG_SKIP:
        *skip = TRUE;
        return FALSE;

    default:
        if (skip)
            *skip = FALSE;
        return FALSE; // DIALOG_CANCEL
    }
}

// ****************************************************************************

void LoadHistory(HKEY regKey, const wchar_t* keyPattern, std::vector<std::wstring>& history,
                 CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE_NONE
    history.clear();
    int i;
    for (i = 0; i < MAX_HISTORY_ENTRIES; i++)
    {
        const std::wstring valueName = SPLFormatStringOwned(keyPattern, i);
        DWORD bytes = 0;
        if (!registry->GetSize(regKey, valueName.c_str(), REG_BINARY, bytes) ||
            bytes < sizeof(wchar_t) || bytes % sizeof(wchar_t) != 0)
            break;
        std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
        if (!registry->GetValue(regKey, valueName.c_str(), REG_BINARY,
                                buffer.data(), bytes))
            break;
        const size_t capacity = bytes / sizeof(wchar_t);
        const size_t length = wcsnlen(buffer.data(), capacity);
        if (length == capacity)
            break;
        history.emplace_back(buffer.data(), length);
    }
}

void SaveHistory(HKEY regKey, const wchar_t* keyPattern, const std::vector<std::wstring>& history, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("SaveHistory(, %ls, , )", keyPattern);
    int i;
    for (i = 0; i < static_cast<int>(history.size()) && i < MAX_HISTORY_ENTRIES; i++)
    {
        const std::wstring valueName = SPLFormatStringOwned(keyPattern, i);
        const size_t length = history[i].size();
        if (length >= MAXDWORD / sizeof(wchar_t))
            break;
        registry->SetValue(regKey, valueName.c_str(), REG_BINARY, history[i].c_str(),
                           static_cast<DWORD>((length + 1) * sizeof(wchar_t)));
    }
}

// ****************************************************************************

BOOL TestForCancel()
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("TestForCancel()");  // Petr: prilis pomaly call-stack
    static DWORD nextTest;
    if ((int)(GetTickCount() - nextTest) < 0)
        return FALSE;

    BOOL cancel = FALSE;
    if ((GetAsyncKeyState(VK_ESCAPE) & 0x8001) && GetForegroundWindow() == SG->GetMainWindowHWND())
    {
        MSG msg; // discard the buffered ESC
        while (PeekMessageW(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
            ;
        cancel = TRUE;
    }
    else
    {
        cancel = SG->GetSafeWaitWindowClosePressed();
    }

    cancel = cancel && SG->SalMessageBox(SG->GetMsgBoxParent(),
                                         LoadStrW(IDS_CANCEL).c_str(), LoadStrW(IDS_QUESTION).c_str(),
                                         MB_YESNO | MB_ICONQUESTION | MSGBOXEX_ESCAPEENABLED) == IDYES;
    UpdateWindow(SG->GetMainWindowHWND());
    nextTest = GetTickCount() + 150;
    SG->WaitForESCRelease();
    return cancel;
}

BOOL ParseFullPath(WCHAR* path, WCHAR*& keyName, int& keyRoot)
{
    CALL_STACK_MESSAGE2("ParseFullPath(, , %d)", keyRoot);
    if (path[0] == L'\0')
        return FALSE;
    if (path[0] == L'\\' && path[1] == L'\0')
    {
        keyName = path + 1;
        keyRoot = -1;
        return TRUE;
    }
    keyName = wcschr(path + 1, L'\\');
    if (keyName == NULL)
        keyName = path + wcslen(path);
    int len = (int)(keyName - (path + 1));
    if (*keyName == L'\\')
        keyName++;
    if (len)
    {
        int i = 0;
        while (PredefinedHKeys[i].HKey != NULL)
        {
            if ((int)wcslen(PredefinedHKeys[i].KeyName) == len &&
                CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                               PredefinedHKeys[i].KeyName, len,
                               path + 1, len) == CSTR_EQUAL &&
                ((path + 1)[len] == L'\0' || (path + 1)[len] == L'\\'))
            {
                keyRoot = i;
                return TRUE;
            }
            i++;
        }
    }
    return FALSE;
}

inline BOOL IsXdigit(WCHAR c)
{
    CALL_STACK_MESSAGE_NONE
    return c >= L'0' && c <= L'9' || towlower(c) >= L'a' && towlower(c) <= L'f';
}

BOOL ValidateHexString(LPWSTR text)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("ValidateHexString()");
    int len = 0;
    LPWSTR s = text; //, st = text;
    BYTE value = 0;
    BOOL openedQuotesASCII = FALSE;
    BOOL openedQuotesUnicode = FALSE;
    while (1)
    {
        if (*s == L'"' && !openedQuotesASCII)
        {
            s++;
            openedQuotesUnicode = !openedQuotesUnicode;
            continue;
        }
        if (*s == L'\'' && !openedQuotesUnicode)
        {
            s++;
            openedQuotesASCII = !openedQuotesASCII;
            continue;
        }
        if (openedQuotesUnicode)
        {
            if (*s == 0)
                break;
            s++;
            len += 2;
        }
        else
        {
            if (openedQuotesASCII)
            {
                if (*s == 0)
                    break;
                else
                {
                    s++;
                    len += 1;
                }
            }
            else
            {
                if (IsXdigit(*s))
                {
                    s++;
                    if (IsXdigit(*s)) // second digit (required)
                    {
                        s++;
                    }
                    else
                        return FALSE;
                }
                else
                {
                    if (*s == L'\0')
                        break; // end of string
                    else
                    {
                        if (*s != L' ')
                            return FALSE;
                        s++; // skip the space
                    }
                }
            }
        }
    }
    return TRUE;
}

// ****************************************************************************
//
// CBuffer
//

BOOL CBuffer::Reserve(int size)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("CBuffer::Reserve(%d)", size);
    if (size > Allocated)
    {
        int s = max(Allocated * 2, size);
        Release();
        Buffer = malloc(s);
        if (Buffer)
            Allocated = s;
        else
            return FALSE;
    }
    return TRUE;
}

// ****************************************************************************

char* Replace(char* string, char s, char d)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE3("Replace(, %d, %d)", s, d);
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
                        std::wstring& selectedPath, BOOL save)
{
    CALL_STACK_MESSAGE4("ShowOpenFileDialog(, %ls, %ls, , %d)", title, filter, save);
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;

    std::vector<wchar_t> packedFilter(filter, filter + wcslen(filter));
    packedFilter.push_back(L'\0');
    packedFilter.push_back(L'\0');
    for (wchar_t& character : packedFilter)
        if (character == L'\t')
            character = L'\0';

    std::wstring fileName;
    std::wstring initialDirectory;
    const DWORD attr = SG->SalGetFileAttributes(selectedPath.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        initialDirectory = selectedPath;
        ofn.lpstrInitialDir = initialDirectory.c_str();
    }
    else
        fileName = selectedPath;

    ofn.lpstrFilter = packedFilter.data();
    ofn.lpstrTitle = title;

    BOOL ret = FALSE;
    if (save)
        ret = SPLSafeGetSaveFileNameOwned(SG, &ofn, fileName);
    else
    {
        ofn.Flags |= OFN_FILEMUSTEXIST;
        std::vector<std::wstring> fileNames;
        fileNames.push_back(fileName);
        ret = SPLSafeGetOpenFileNamesOwned(SG, &ofn, fileNames);
        if (ret && fileNames.size() == 1)
            fileName = std::move(fileNames[0]);
        else if (ret)
            ret = FALSE;
    }
    if (ret)
        selectedPath = std::move(fileName);
    return ret;
}

BOOL RemoveFSNameFromPath(LPWSTR path)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("RemoveFSNameFromPath()");
    LPWSTR iterator = path;
    while (*iterator != L'\0' && *iterator != L'\\')
    {
        if (*iterator == L':')
        {
            if (iterator - path)
            {
                int len = (int)(iterator - path);
                if (len == static_cast<int>(AssignedFSName.size()) &&
                    SG->StrNICmp(path, AssignedFSName.c_str(), len) == 0)
                {
                    memmove(path, iterator + 1, (wcslen(iterator + 1) + 1) * 2);
                    return TRUE;
                }
                else
                    return FALSE; // unknown FS
            }
            else
                return FALSE; // unknown FS
        }
        iterator++;
    }
    return TRUE;
}

BOOL RemoveFSNameFromPath(std::wstring& path)
{
    if (path.empty())
        return RemoveFSNameFromPath(const_cast<LPWSTR>(L""));
    if (!RemoveFSNameFromPath(path.data()))
        return FALSE;
    // Re-sync the owner with the shortened C string the raw form just produced.
    path.resize(wcslen(path.c_str()));
    return TRUE;
}

wchar_t* ReplaceUnsafeCharacters(wchar_t* string)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("ReplaceUnsafeCharacters()");
    wchar_t* iterator = string;
    while (*iterator)
    {
        if (*iterator > 0 && *iterator <= 31 ||
            wcschr(L"*?<>:\"/\\|", *iterator))
            *iterator = L'_';
        iterator++;
    }
    return string;
}

/*
LPDLGTEMPLATE
LoadDlgTemplate(int id, DWORD &size)
{
  CALL_STACK_MESSAGE3("LoadDlgTemplate(%d, 0x%X)", id, size);
  LPDLGTEMPLATE ret = NULL;
  HRSRC hRsrc = FindResource(HLanguage, MAKEINTRESOURCE(id),  MAKEINTRESOURCE(RT_DIALOG));
  if (hRsrc)
  {
    void * data = LoadResource(HLanguage, hRsrc);
    if (data)
    {
      size = SizeofResource(HLanguage, hRsrc);
      if (size)
      {
        ret = (LPDLGTEMPLATE) malloc(size);
        if (ret)
        {
          memcpy(ret, data, size);
        }
        else TRACE_E("Low memory");
      }
      else TRACE_E("Unable to get size of resource");
    }
    else TRACE_E("Unable to load resource");
  }
  else TRACE_E("Unable to find resource");
  return ret;
}
*/

/*
LPDLGTEMPLATE
ReplaceDlgTemplateFont(LPDLGTEMPLATE dlgTemplate, DWORD &size, LPCWSTR newFont)
{
  CALL_STACK_MESSAGE2("ReplaceDlgTemplateFont(, 0x%X, )", size);
  if (!(dlgTemplate->style & DS_SETFONT ))
  {
    TRACE_E("ReplaceDlgTemplateFont: Dialog template lacks DS_SETFONT.");
    return dlgTemplate;
  }

  LPWSTR ptr = LPWSTR((char *)dlgTemplate + sizeof(DLGTEMPLATE));

  // skip the menu array
  if (*ptr == 0xFFFF) ptr += 2; // followed by the ordinal value of a menu resource
  else
  {
    // this is a NULL-terminated string
    ptr += wcslen(ptr) + 1;
  }

  // skip the class array
  if (*ptr == 0xFFFF) ptr += 2; // followed by the ordinal value of a predefined system window class
  else
  {
    // this is a NULL-terminated string
    ptr += wcslen(ptr) + 1;
  }

  // skip the window title
  ptr += wcslen(ptr) + 1;

  // skip the point size
  ptr++;

  // adjust the template size and move the data following the font name
  int len1 = wcslen(ptr) + 1;
  int len2 = wcslen(newFont) + 1;
  DWORD dest = (DWORD(ptr + len2) - DWORD(dlgTemplate) + 3)/4*4;
  DWORD sour = (DWORD(ptr + len1) - DWORD(dlgTemplate) + 3)/4*4;
  if (sour > dest)
  {
    memmove((char*)dlgTemplate + dest, (char*)dlgTemplate + sour, size - sour);
    size -= sour - dest;
  }
  else
  {
    if (sour < dest)
    {
      DWORD offset = DWORD(ptr) - DWORD(dlgTemplate);
      void * p = realloc(dlgTemplate, size + (dest - sour));
      if (!p)
      {
        TRACE_E("Low memory");
        return dlgTemplate;
      }
      dlgTemplate = (LPDLGTEMPLATE) p;
      ptr = LPWSTR(DWORD(dlgTemplate) + offset);

      memmove((char*)dlgTemplate + dest, (char*)dlgTemplate + sour, size - sour);
      size += dest - sour;
    }
  }

  // write the new font name
  wcscpy(ptr, newFont);
  
  return dlgTemplate;
}
*/
