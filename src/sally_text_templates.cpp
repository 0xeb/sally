// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "plugins.h"
#include "fileswnd.h"
#include "mainwnd.h"
#include "salinflt.h"
#include "ui/IPrompter.h"
#include "common/IClipboard.h"
#include "common/IPathService.h"
#include "common/text/PluralExpander.h"
#include "common/text/CaseFolding.h"
#include "common/Win32TextCodec.h"
#include "common/ExternalToolRunner.h"
#include "common/unicode/helpers.h"
#include "common/unicode/WideTextRange.h"

//****************************************************************************
//
// CTruncatedString
//
// documented in Hck
//

CTruncatedString::CTruncatedString()
{
    SubStrIndex = -1;
    SubStrLen = 0;
    HasTruncated = FALSE;
}

CTruncatedString::~CTruncatedString()
{
};

BOOL CTruncatedString::CopyFrom(const CTruncatedString* src)
{
    TextW = src->TextW;
    SubStrIndex = src->SubStrIndex;
    SubStrLen = src->SubStrLen;
    TruncatedTextW = src->TruncatedTextW;
    HasTruncated = src->HasTruncated;
    return TRUE;
}

BOOL CTruncatedString::SetW(const wchar_t* str, const wchar_t* subStr)
{
    TruncatedTextW.clear();
    HasTruncated = FALSE;

    if (str == NULL)
        str = L"";

    int subStrIndex = -1;
    int subStrLen = 0;
    const wchar_t* insertPos = NULL;
    if (subStr != NULL)
    {
        const wchar_t* p = str;
        int doubles = 0;
        while (*p != 0)
        {
            if (*p == L'%')
            {
                if (*(p + 1) == L'%')
                {
                    p++;
                    doubles++;
                }
                else
                {
                    if (*(p + 1) == L's')
                    {
                        insertPos = p;
                        subStrIndex = (int)(p - str - doubles);
                        break;
                    }
                    else
                    {
                        TRACE_E("CTruncatedString::SetW: unknown format specifier");
                        break;
                    }
                }
            }
            p++;
        }
        if (subStrIndex == -1)
            TRACE_E("CTruncatedString::SetW: %s was not found");
        else
            subStrLen = (int)wcslen(subStr);
    }

    if (insertPos != NULL)
    {
        TextW.assign(str, insertPos - str);
        TextW += subStr;
        TextW += insertPos + 2;
        SubStrIndex = subStrIndex;
        SubStrLen = subStrLen;
    }
    else
    {
        TextW = str;
        SubStrIndex = -1;
        SubStrLen = 0;
    }

    return TRUE;
}

const wchar_t*
CTruncatedString::Get()
{
    if (SubStrIndex == -1 || !HasTruncated)
    {
        if (TextW.empty())
        {
            TRACE_E("Text == NULL");
            return L"";
        }
        else
            return TextW.c_str();
    }
    else
    {
        return TruncatedTextW.c_str();
    }
}

// GetW() and Get() are the same function now - GetW's only difference was
// returning L"" when UseWideText was FALSE, a state the only setter never produced. Kept as a
// forwarder rather than renamed, because both names have callers and neither is wrong.
const wchar_t*
CTruncatedString::GetW()
{
    return Get();
}

BOOL CTruncatedString::TruncateText(HWND hWindow, BOOL forMessageBox)
{
    // if there is nothing to truncate, exit
    if (SubStrIndex == -1)
        return TRUE;

    BOOL ret = TRUE;

    HDC hDC = HANDLES(GetDC(hWindow));
    HFONT hFont = (HFONT)SendMessage(hWindow, WM_GETFONT, 0, 0);
    HFONT hOldFont = (HFONT)SelectObject(hDC, hFont);

    // Was `if (UseWideText)`. There is one representation now, so the scope is
    // kept only to bound the measuring locals.
    {
        int fitChars;
        int alpDx[8000];
        int textLen = (int)TextW.length();
        HasTruncated = TRUE;

        if (forMessageBox)
        {
            int maxWidth = 400;
            SIZE sz;
            GetTextExtentExPointW(hDC, TextW.c_str() + SubStrIndex, SubStrLen, maxWidth, &fitChars, alpDx, &sz);
            if (fitChars < SubStrLen)
            {
                TruncatedTextW.assign(TextW.c_str(), SubStrIndex + fitChars);
                TruncatedTextW += L"...";
                TruncatedTextW += TextW.c_str() + SubStrIndex + SubStrLen;
            }
            else
            {
                TruncatedTextW = TextW;
            }
        }
        else
        {
            RECT r;
            GetClientRect(hWindow, &r);
            int maxWidth = r.right;

            SIZE sz;
            if (textLen > 8000)
            {
                TRACE_E("Text was truncated (to 7999 characters)");
                TextW.resize(7999);
                textLen = 7999;
            }
            GetTextExtentExPointW(hDC, TextW.c_str(), textLen, 0, NULL, alpDx, &sz);
            if (sz.cx > maxWidth)
            {
                int width = sz.cx;

                GetTextExtentPoint32W(hDC, L"...", 3, &sz);
                int ellipsisWidth = sz.cx;

                int keepEnd = SubStrIndex + SubStrLen;
                maxWidth -= ellipsisWidth;
                while (width > maxWidth && keepEnd > SubStrIndex)
                {
                    int charIndex = keepEnd - 1;
                    int prevWidth = charIndex > 0 ? alpDx[charIndex - 1] : 0;
                    width -= alpDx[charIndex] - prevWidth;
                    keepEnd--;
                }

                TruncatedTextW.assign(TextW.c_str(), keepEnd);
                TruncatedTextW += L"...";
                TruncatedTextW += TextW.c_str() + SubStrIndex + SubStrLen;
            }
            else
            {
                TruncatedTextW = TextW;
            }
        }

    }

    // The narrow arm that used to follow here is deleted. It measured with
    // GetTextExtentExPointA and spliced with byte memcpy/strcpy against the `Text` mirror -
    // reachable only when UseWideText was FALSE, which the only setter (SetW) never left it.
    // 73 lines of code that had not run since the mirror was introduced.
    SelectObject(hDC, hOldFont);
    HANDLES(ReleaseDC(hWindow, hDC));
    return ret;
}

//****************************************************************************
//
// StrToUInt64

//****************************************************************************

//****************************************************************************
//
// ExpandPluralFilesDirs
//
// Writes a string to lpOut depending on the values of the variables 'files' and 'dirs':
// files > 0 && dirs == 0  ->  XXX (selected) files
// files == 0 && dirs > 0  ->  YYY (selected) directories
// files > 0 && dirs > 0   ->  XXX (selected) files and YYY directories
//
// where XXX and YYY correspond to the values of the files and dirs variables.
// The selectedForm variable controls inserting the word selected.
//
// forDlgCaption is TRUE/FALSE if the text is/is not meant for a dialog caption
// (initial capital letters are required in English).
//
// Returns the number of copied characters without the terminator.
//

int ExpandPluralFilesDirsW(wchar_t* lpOut, int nOutMax, int files, int dirs, int mode, BOOL forDlgCaption)
{
    static int form[2][3][3] =
        {
            {{IDS_PLURAL_X_FILES, IDS_PLURAL_X_DIRS, IDS_PLURAL_X_FILES_Y_DIRS},
             {IDS_PLURAL_X_SEL_FILES, IDS_PLURAL_X_SEL_DIRS, IDS_PLURAL_X_SEL_FILES_Y_SEL_DIRS},
             {IDS_PLURAL_X_HID_FILES, IDS_PLURAL_X_HID_DIRS, IDS_PLURAL_X_HID_FILES_Y_HID_DIRS}},

            {{IDS_DLG_PLURAL_X_FILES, IDS_DLG_PLURAL_X_DIRS, IDS_DLG_PLURAL_X_FILES_Y_DIRS},
             {IDS_DLG_PLURAL_X_SEL_FILES, IDS_DLG_PLURAL_X_SEL_DIRS, IDS_DLG_PLURAL_X_SEL_FILES_Y_SEL_DIRS},
             {IDS_DLG_PLURAL_X_HID_FILES, IDS_DLG_PLURAL_X_HID_DIRS, IDS_DLG_PLURAL_X_HID_FILES_Y_HID_DIRS}},
        };
    int indDlgCaption = forDlgCaption ? 1 : 0;
    wchar_t expanded[200];
    if (nOutMax > 200)
        nOutMax = 200;
    nOutMax -= 20; // make room for the numbers of files and dirs

    int ret;

    if (files > 0 && dirs == 0)
    {
        CQuadWord qwFiles(files, 0);
        ExpandPluralStringW(expanded, nOutMax, LoadStrW(form[indDlgCaption][mode][0]), 1, &qwFiles);
        ret = swprintf(lpOut, (size_t)nOutMax + 20, expanded, files);
    }
    else
    {
        if (files == 0 && dirs > 0)
        {
            CQuadWord qwDirs(dirs, 0);
            ExpandPluralStringW(expanded, nOutMax, LoadStrW(form[indDlgCaption][mode][1]), 1, &qwDirs);
            ret = swprintf(lpOut, (size_t)nOutMax + 20, expanded, dirs);
        }
        else
        {
            CQuadWord qwPars[2] = {CQuadWord(files, 0), CQuadWord(dirs, 0)};
            ExpandPluralStringW(expanded, nOutMax, LoadStrW(form[indDlgCaption][mode][2]), 2, qwPars);
            ret = swprintf(lpOut, (size_t)nOutMax + 20, expanded, files, dirs);
        }
    }
    return ret;
}

std::wstring ExpandPluralFilesDirsTextW(int files, int dirs, int mode, BOOL forDlgCaption)
{
    static int form[2][3][3] =
        {
            {{IDS_PLURAL_X_FILES, IDS_PLURAL_X_DIRS, IDS_PLURAL_X_FILES_Y_DIRS},
             {IDS_PLURAL_X_SEL_FILES, IDS_PLURAL_X_SEL_DIRS, IDS_PLURAL_X_SEL_FILES_Y_SEL_DIRS},
             {IDS_PLURAL_X_HID_FILES, IDS_PLURAL_X_HID_DIRS, IDS_PLURAL_X_HID_FILES_Y_HID_DIRS}},

            {{IDS_DLG_PLURAL_X_FILES, IDS_DLG_PLURAL_X_DIRS, IDS_DLG_PLURAL_X_FILES_Y_DIRS},
             {IDS_DLG_PLURAL_X_SEL_FILES, IDS_DLG_PLURAL_X_SEL_DIRS, IDS_DLG_PLURAL_X_SEL_FILES_Y_SEL_DIRS},
             {IDS_DLG_PLURAL_X_HID_FILES, IDS_DLG_PLURAL_X_HID_DIRS, IDS_DLG_PLURAL_X_HID_FILES_Y_HID_DIRS}},
        };
    const int captionIndex = forDlgCaption ? 1 : 0;

    if (files > 0 && dirs == 0)
    {
        const CQuadWord parameters[] = {CQuadWord(files, 0)};
        const std::wstring expanded = ExpandPluralStringOwnedW(LoadStrW(form[captionIndex][mode][0]), 1, parameters);
        return FormatStrW(expanded.c_str(), files);
    }
    if (files == 0 && dirs > 0)
    {
        const CQuadWord parameters[] = {CQuadWord(dirs, 0)};
        const std::wstring expanded = ExpandPluralStringOwnedW(LoadStrW(form[captionIndex][mode][1]), 1, parameters);
        return FormatStrW(expanded.c_str(), dirs);
    }

    const CQuadWord parameters[] = {CQuadWord(files, 0), CQuadWord(dirs, 0)};
    const std::wstring expanded = ExpandPluralStringOwnedW(LoadStrW(form[captionIndex][mode][2]), 2, parameters);
    return FormatStrW(expanded.c_str(), files, dirs);
}

int ExpandPluralBytesFilesDirsW(wchar_t* lpOut, int nOutMax, const CQuadWord& selectedBytes, int files, int dirs, BOOL useSubTexts)
{
    wchar_t expanded[200];
    const std::wstring number = NumberToStr(selectedBytes);
    if (nOutMax > 200)
        nOutMax = 200;
    nOutMax -= 30; // make room for the numbers of files and dirs

    int ret;

    if (files > 0 && dirs == 0)
    {
        CQuadWord qwPars[2] = {selectedBytes, CQuadWord(files, 0)};
        ExpandPluralStringW(expanded, nOutMax,
                           LoadStrW(useSubTexts ? IDS_PLURAL_X_BYTES_Y_SEL_FILES2 : IDS_PLURAL_X_BYTES_Y_SEL_FILES),
                           2, qwPars);
        ret = swprintf(lpOut, (size_t)nOutMax + 30, expanded, number.c_str(), files);
    }
    else
    {
        if (files == 0 && dirs > 0)
        {
            CQuadWord qwPars[2] = {selectedBytes, CQuadWord(dirs, 0)};
            ExpandPluralStringW(expanded, nOutMax,
                               LoadStrW(useSubTexts ? IDS_PLURAL_X_BYTES_Y_SEL_DIRS2 : IDS_PLURAL_X_BYTES_Y_SEL_DIRS),
                               2, qwPars);
            ret = swprintf(lpOut, (size_t)nOutMax + 30, expanded, number.c_str(), dirs);
        }
        else
        {
            CQuadWord qwPars[3] = {selectedBytes, CQuadWord(files, 0), CQuadWord(dirs, 0)};
            ExpandPluralStringW(expanded, nOutMax,
                               LoadStrW(useSubTexts ? IDS_PLURAL_X_BYTES_Y_SEL_FILES_Z_SEL_DIRS2 : IDS_PLURAL_X_BYTES_Y_SEL_FILES_Z_SEL_DIRS),
                               3, qwPars);
            ret = swprintf(lpOut, (size_t)nOutMax + 30, expanded, number.c_str(), files, dirs);
        }
    }
    return ret;
}

std::wstring ExpandPluralBytesFilesDirsTextW(const CQuadWord& selectedBytes, int files, int dirs,
                                             BOOL useSubTexts)
{
    const std::wstring number = NumberToStr(selectedBytes);

    if (files > 0 && dirs == 0)
    {
        const CQuadWord parameters[] = {selectedBytes, CQuadWord(files, 0)};
        const std::wstring expanded = ExpandPluralStringOwnedW(
            LoadStrW(useSubTexts ? IDS_PLURAL_X_BYTES_Y_SEL_FILES2 : IDS_PLURAL_X_BYTES_Y_SEL_FILES),
            2, parameters);
        return FormatStrW(expanded.c_str(), number.c_str(), files);
    }
    if (files == 0 && dirs > 0)
    {
        const CQuadWord parameters[] = {selectedBytes, CQuadWord(dirs, 0)};
        const std::wstring expanded = ExpandPluralStringOwnedW(
            LoadStrW(useSubTexts ? IDS_PLURAL_X_BYTES_Y_SEL_DIRS2 : IDS_PLURAL_X_BYTES_Y_SEL_DIRS),
            2, parameters);
        return FormatStrW(expanded.c_str(), number.c_str(), dirs);
    }

    const CQuadWord parameters[] = {selectedBytes, CQuadWord(files, 0), CQuadWord(dirs, 0)};
    const std::wstring expanded = ExpandPluralStringOwnedW(
        LoadStrW(useSubTexts ? IDS_PLURAL_X_BYTES_Y_SEL_FILES_Z_SEL_DIRS2 : IDS_PLURAL_X_BYTES_Y_SEL_FILES_Z_SEL_DIRS),
        3, parameters);
    return FormatStrW(expanded.c_str(), number.c_str(), files, dirs);
}

BOOL LookForSubTexts(std::wstring& text,
                     std::vector<sally::unicode::WideTextRange>& varPlacements)
{
    try
    {
        std::wstring parsed;
        parsed.reserve(text.size());
        std::vector<sally::unicode::WideTextRange> parsedPlacements;
        std::size_t variableStart = std::wstring::npos;

        for (std::size_t source = 0; source < text.size(); ++source)
        {
            wchar_t character = text[source];
            if (character == L'\\' && source + 1 < text.size() &&
                (text[source + 1] == L'<' || text[source + 1] == L'>' ||
                 text[source + 1] == L'\\'))
            {
                character = text[++source];
            }
            else if (character == L'<')
            {
                if (variableStart != std::wstring::npos)
                {
                    TRACE_E("LookForSubTexts: nested opening marker");
                    return FALSE;
                }
                variableStart = parsed.size();
                continue;
            }
            else if (character == L'>')
            {
                if (variableStart == std::wstring::npos)
                {
                    TRACE_E("LookForSubTexts: unmatched closing marker");
                    return FALSE;
                }
                parsedPlacements.push_back(
                    {variableStart, parsed.size() - variableStart});
                variableStart = std::wstring::npos;
                continue;
            }
            parsed.push_back(character);
        }

        if (variableStart != std::wstring::npos)
        {
            TRACE_E("LookForSubTexts: unmatched opening marker");
            return FALSE;
        }
        text.swap(parsed);
        varPlacements.swap(parsedPlacements);
        return TRUE;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    catch (const std::length_error&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
}

//****************************************************************************
//
// CViewTemplates
//

// Registry VALUE NAMES are form-agnostic: the wide API addresses the same values.
const wchar_t* SALAMANDER_VIEWTEMPLATE_NAME = L"Name";
const wchar_t* SALAMANDER_VIEWTEMPLATE_FLAGS = L"Flags";
const wchar_t* SALAMANDER_VIEWTEMPLATE_COLUMNS = L"Columns";
const wchar_t* SALAMANDER_VIEWTEMPLATE_LEFTSMARTMODE = L"Left Smart Mode";
const wchar_t* SALAMANDER_VIEWTEMPLATE_RIGHTSMARTMODE = L"Right Smart Mode";

CViewTemplates::CViewTemplates()
{
    // default values
    Set(0, VIEW_MODE_TREE, LoadStrW(IDS_TREE_VIEW), 0, TRUE, TRUE);
    Set(1, VIEW_MODE_BRIEF, LoadStrW(IDS_BRIEF_VIEW), 0, TRUE, TRUE);
    Set(2, VIEW_MODE_DETAILED, LoadStrW(IDS_DETAILED_VIEW), VIEW_SHOW_SIZE | VIEW_SHOW_DATE | VIEW_SHOW_TIME | VIEW_SHOW_ATTRIBUTES, TRUE, TRUE);
    Set(3, VIEW_MODE_ICONS, LoadStrW(IDS_ICONS_VIEW), 0, TRUE, TRUE);
    Set(4, VIEW_MODE_THUMBNAILS, LoadStrW(IDS_THUMBNAILS_VIEW), 0, TRUE, TRUE);
    Set(5, VIEW_MODE_TILES, LoadStrW(IDS_TILES_VIEW), 0, TRUE, TRUE);
    Set(6, VIEW_MODE_DETAILED, LoadStrW(IDS_TYPES_VIEW), VIEW_SHOW_SIZE | VIEW_SHOW_TYPE | VIEW_SHOW_DATE | VIEW_SHOW_TIME | VIEW_SHOW_ATTRIBUTES, TRUE, TRUE);
    //  Set(4, VIEW_MODE_DETAILED, LoadStr(IDS_DESCRIPTIONS_VIEW), VIEW_SHOW_SIZE | VIEW_SHOW_DESCRIPTION, TRUE, TRUE);
    int i;
    for (i = 7; i < VIEW_TEMPLATES_COUNT; i++)
        Set(i, VIEW_MODE_DETAILED, L"", 0, TRUE, TRUE);
    for (i = 0; i < VIEW_TEMPLATES_COUNT; i++)
        ZeroMemory(Items[i].Columns, sizeof(Items[i].Columns));
}

void CViewTemplates::Set(DWORD index, const wchar_t* name, DWORD flags, BOOL leftSmartMode, BOOL rightSmartMode)
{
    Items[index].Name = name != NULL ? name : L"";
    Items[index].Flags = flags;
    Items[index].LeftSmartMode = leftSmartMode;
    Items[index].RightSmartMode = rightSmartMode;
}

void CViewTemplates::Set(DWORD index, DWORD viewMode, const wchar_t* name, DWORD flags, BOOL leftSmartMode, BOOL rightSmartMode)
{
    Items[index].Mode = viewMode;
    Set(index, name, flags, leftSmartMode, rightSmartMode);
}

BOOL CViewTemplates::SwapItems(int index1, int index2)
{
    if (index1 < 2 || index2 < 2)
    {
        TRACE_E("It is not possible to move first nor second item.");
        return FALSE;
    }

    if (index1 >= VIEW_TEMPLATES_COUNT || index2 >= VIEW_TEMPLATES_COUNT)
    {
        TRACE_E("Index is out of range");
        return FALSE;
    }

    CViewTemplate tmp = Items[index1];
    Items[index1] = Items[index2];
    Items[index2] = tmp;
    return TRUE;
}

BOOL CViewTemplates::CleanName(std::wstring& name)
{
    const size_t first = name.find_first_not_of(L' ');
    if (first == std::wstring::npos)
    {
        name.clear();
        return FALSE;
    }
    const size_t last = name.find_last_not_of(L' ');
    name = name.substr(first, last - first + 1);
    return TRUE;
}

std::wstring CViewTemplates::SaveColumns(const CColumnConfig* columns)
{
    std::wstring value;
    int i;
    for (i = 0; i < STANDARD_COLUMNS_COUNT; i++)
    {
        const CColumnConfig* column = &columns[i];
        if (i > 0)
            value.push_back(L',');
        const DWORD data = column->LeftWidth | column->LeftFixedWidth << 16;
        value += FormatStrW(L"%lx", data);
    }
    value.push_back(L',');
    for (i = 0; i < STANDARD_COLUMNS_COUNT; i++)
    {
        const CColumnConfig* column = &columns[i];
        if (i > 0)
            value.push_back(L',');
        const DWORD data = column->RightWidth | column->RightFixedWidth << 16;
        value += FormatStrW(L"%lx", data);
    }
    return value;
}

void CViewTemplates::LoadColumns(CColumnConfig* columns, const std::wstring& value)
{
    size_t start = 0;
    int token = 0;
    while (start <= value.size() && token < 2 * STANDARD_COLUMNS_COUNT)
    {
        const size_t separator = value.find(L',', start);
        const std::wstring field = value.substr(start, separator - start);
        wchar_t* end = NULL;
        const unsigned long parsed = wcstoul(field.c_str(), &end, 16);
        if (end != field.c_str() && *end == 0)
        {
            const DWORD data = (DWORD)parsed;
            CColumnConfig& column = columns[token % STANDARD_COLUMNS_COUNT];
            unsigned width = data & 0x0000ffff;
            if (width > 2000)
                width = 2000;
            const unsigned fixedWidth = (data & 0x00010000) >> 16;
            if (token < STANDARD_COLUMNS_COUNT)
            {
                column.LeftWidth = width;
                column.LeftFixedWidth = fixedWidth;
                column.RightWidth = width;
                column.RightFixedWidth = fixedWidth;
            }
            else
            {
                column.RightWidth = width;
                column.RightFixedWidth = fixedWidth;
            }
        }
        ++token;
        if (separator == std::wstring::npos)
            break;
        start = separator + 1;
    }
}

BOOL CViewTemplates::Save(HKEY hKey)
{
    int i;
    for (i = 0; i < VIEW_TEMPLATES_COUNT; i++)
    {
        const std::wstring keyName = std::to_wstring(i < VIEW_TEMPLATES_COUNT - 1 ? i + 1 : 0);
        HKEY actKey;
        if (CreateKeyW(hKey, keyName.c_str(), actKey))
        {
            SetValueW(actKey, SALAMANDER_VIEWTEMPLATE_NAME, REG_SZ, Items[i].Name.c_str(), -1);
            SetValueW(actKey, SALAMANDER_VIEWTEMPLATE_FLAGS, REG_DWORD, &Items[i].Flags, sizeof(DWORD));
            // SaveColumns hoisted out of the argument list: it returns a
            // wchar_t COUNT, which used to be passed as 'dataSize'. SetValueW ignores
            // dataSize for REG_SZ (it takes the NUL-terminated wide string), so spelling
            // that -1 says what actually happens instead of implying a byte size.
            const std::wstring columns = SaveColumns(Items[i].Columns);
            SetValueW(actKey, SALAMANDER_VIEWTEMPLATE_COLUMNS, REG_SZ, columns.c_str(), -1);
            SetValueW(actKey, SALAMANDER_VIEWTEMPLATE_LEFTSMARTMODE, REG_DWORD, &Items[i].LeftSmartMode, sizeof(DWORD));
            SetValueW(actKey, SALAMANDER_VIEWTEMPLATE_RIGHTSMARTMODE, REG_DWORD, &Items[i].RightSmartMode, sizeof(DWORD));
            CloseKey(actKey);
        }
    }
    return TRUE;
}

BOOL CViewTemplates::Load(HKEY hKey)
{
    int i;
    for (i = 0; i < VIEW_TEMPLATES_COUNT; i++)
    {
        const std::wstring keyName = std::to_wstring(i < VIEW_TEMPLATES_COUNT - 1 ? i + 1 : 0);
        if (i == 6 && Configuration.ConfigVersion < 23)
            continue; // for the IDS_TYPES_VIEW view we want default columns
        HKEY actKey;
        if (OpenKeyW(hKey, keyName.c_str(), actKey))
        {
            std::wstring name;
            std::wstring columns;
            DWORD flags;
            flags = 0;
            DWORD leftSM = TRUE;
            DWORD rightSM = TRUE;
            GetValueW(actKey, SALAMANDER_VIEWTEMPLATE_LEFTSMARTMODE, REG_DWORD, &leftSM, sizeof(DWORD));
            GetValueW(actKey, SALAMANDER_VIEWTEMPLATE_RIGHTSMARTMODE, REG_DWORD, &rightSM, sizeof(DWORD));
            if (GetStringValueW(actKey, SALAMANDER_VIEWTEMPLATE_NAME, name) &&
                GetValueW(actKey, SALAMANDER_VIEWTEMPLATE_FLAGS, REG_DWORD, &flags, sizeof(DWORD)) &&
                GetStringValueW(actKey, SALAMANDER_VIEWTEMPLATE_COLUMNS, columns))
            {
                LoadColumns(Items[i].Columns, columns);
                CleanName(name);

                // overwrite file names the user could not change anyway
                int resID = -1;
                switch (i)
                {
                case 0:
                    resID = IDS_TREE_VIEW;
                    break;
                case 1:
                    resID = IDS_BRIEF_VIEW;
                    break;
                case 2:
                    resID = IDS_DETAILED_VIEW;
                    break;
                case 3:
                    resID = IDS_ICONS_VIEW;
                    break;
                case 4:
                    resID = IDS_THUMBNAILS_VIEW;
                    break;
                case 5:
                    resID = IDS_TILES_VIEW;
                    break;
                case 6:
                    resID = IDS_TYPES_VIEW;
                    break;
                }
                if (resID != -1)
                    name = LoadStrW(resID);

                Set(i, name.c_str(), flags, leftSM, rightSM);
            }
            CloseKey(actKey);
        }
    }
    return TRUE;
}

// ****************************************************************************

DWORD AddAcpTextToClipboard(const char* bytes, int byteCount)
{
    std::wstring text;
    const Win32TextConversionResult conversion =
        Win32DecodeText(CP_ACP, bytes, static_cast<size_t>(byteCount), text);
    DWORD err = conversion.Succeeded() ? ERROR_SUCCESS : conversion.Win32Error;
    if (err == ERROR_SUCCESS)
    {
        HGLOBAL unicode = NOHANDLES(GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE,
                                                sizeof(WCHAR) * (text.size() + 1)));
        if (unicode != NULL)
        {
            WCHAR* unicodeStr = (WCHAR*)HANDLES(GlobalLock(unicode));
            if (unicodeStr != NULL)
            {
                memcpy(unicodeStr, text.c_str(), sizeof(WCHAR) * (text.size() + 1));
                HANDLES(GlobalUnlock(unicode));
                if (err == ERROR_SUCCESS && SetClipboardData(CF_UNICODETEXT, unicode) == NULL)
                    err = GetLastError();
            }
            else
                err = GetLastError();
            if (err != ERROR_SUCCESS)
                NOHANDLES(GlobalFree(unicode));
        }
        else
            err = GetLastError();
    }
    if (err != ERROR_SUCCESS)
        TRACE_EW(L"SetClipboardData failed for Unicode version of text. Error: " << GetErrorTextOwned(err).c_str());
    return err;
}

BOOL CopyHTextToClipboardW(HGLOBAL hGlobalText, int textLen)
{
    if (hGlobalText == NULL)
    {
        TRACE_E("hGlobalText == NULL");
        return FALSE;
    }

    DWORD err = ERROR_SUCCESS;

    if (OpenClipboard(NULL))
    {
        if (EmptyClipboard())
        {
            // CF_UNICODETEXT is authoritative. Windows synthesizes CF_TEXT for legacy consumers.
            if (SetClipboardData(CF_UNICODETEXT, hGlobalText) == NULL)
                err = GetLastError();
        }
        else
            err = GetLastError();
        CloseClipboard();

        IdleRefreshStates = TRUE;  // on the next idle force a check of the status variables
        IdleCheckClipboard = TRUE; // also let it check the clipboard
    }
    else
    {
        err = GetLastError();
        TRACE_E("OpenClipboard() has failed!");
    }

    return err == ERROR_SUCCESS;
}

// ****************************************************************************

BOOL CopyTextToClipboardW(const wchar_t* text, int textLen, BOOL showEcho, HWND hEchoParent)
{
    if (text == NULL)
    {
        TRACE_E("text == NULL");
        return FALSE;
    }

    // Create null-terminated string of specified length
    std::wstring textStr(text, textLen == -1 ? wcslen(text) : textLen);

    auto result = gClipboard->SetText(textStr.c_str());

    if (result.success)
    {
        IdleRefreshStates = TRUE;  // force state variable check on next Idle
        IdleCheckClipboard = TRUE; // also enable clipboard checking
    }

    if (showEcho)
    {
        if (!result.success)
            gPrompter->ShowError(LoadStrW(IDS_COPYTOCLIPBOARD), GetErrorTextOwned(result.errorCode).c_str());
        else
            gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_TEXTCOPIED));
    }
    return result.success ? TRUE : FALSE;
}

// ****************************************************************************

BOOL CopyAcpTextToClipboard(const char* bytes, int byteCount, BOOL showEcho, HWND hEchoParent)
{
    if (bytes == NULL)
    {
        TRACE_E("text == NULL");
        return FALSE;
    }
    // Decode the explicitly ACP-encoded byte span and delegate to the native-wide owner.
    const size_t length = byteCount == -1 ? strlen(bytes) : static_cast<size_t>(byteCount);
    std::wstring wideText;
    const Win32TextConversionResult conversion = Win32DecodeText(CP_ACP, bytes, length, wideText);
    if (!conversion.Succeeded())
        return FALSE;
    return CopyTextToClipboardW(wideText.c_str(), -1, showEcho, hEchoParent);
}

// ****************************************************************************

BOOL CopyAcpHTextToClipboard(HGLOBAL hGlobalBytes, int byteCount, BOOL showEcho, HWND hEchoParent)
{
    if (hGlobalBytes == NULL)
    {
        TRACE_E("hGlobalText == NULL");
        return FALSE;
    }

    DWORD err = ERROR_SUCCESS;

    if (OpenClipboard(NULL))
    {
        if (EmptyClipboard())
        {
            char* bytes = (char*)HANDLES(GlobalLock(hGlobalBytes));
            if (bytes != NULL)
            {
                if (byteCount == -1)
                    byteCount = lstrlenA(bytes);
                err = AddAcpTextToClipboard(bytes, byteCount);
                HANDLES(GlobalUnlock(hGlobalBytes));
            }
            else
                err = GetLastError();

            if (SetClipboardData(CF_TEXT, hGlobalBytes) == NULL) // then publish the original ACP bytes
                err = GetLastError();
        }
        else
            err = GetLastError();
        CloseClipboard();

        IdleRefreshStates = TRUE;  // on the next idle force a check of the status variables
        IdleCheckClipboard = TRUE; // also let it check the clipboard
    }
    else
    {
        err = GetLastError();
        TRACE_E("OpenClipboard() has failed!");
    }

    if (showEcho)
    {
        if (err != ERROR_SUCCESS)
            gPrompter->ShowError(LoadStrW(IDS_COPYTOCLIPBOARD), GetErrorTextOwned(err).c_str());
        else
            gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE), LoadStrW(IDS_TEXTCOPIED));
    }
    return err == ERROR_SUCCESS;
}

//****************************************************************************
//
// Internal functions for retrieving column content
//

// initialized in the drawing routine before calling the callback
const CFileData* TransferFileData;
int TransferIsDir;
wchar_t TransferBuffer[TRANSFER_BUFFER_MAX];
int TransferLen;
DWORD TransferRowData;
CPluginDataInterfaceAbstract* TransferPluginDataIface;
DWORD TransferActCustomData;

int TransferAssocIndex;

void WINAPI InternalGetDosName()
{
    if (TransferFileData->DosName != NULL)
    {
        TransferLen = lstrlenW(TransferFileData->DosName);
        // wmemcpy: TransferLen came from lstrlenW so it counts CHARACTERS
        // (sally.h:321 says so), and both buffers are wchar_t. CopyMemory counts bytes, so
        // this copied half the DOS name into the panel column.
        wmemcpy(TransferBuffer, TransferFileData->DosName, TransferLen);
    }
    else
        TransferLen = 0;
}

void WINAPI InternalGetSize()
{
    if (TransferIsDir && !TransferFileData->SizeValid) // only directories without a known size
    {
        wmemcpy(TransferBuffer, DirColumnStrW.c_str(), DirColumnStrWLen);
        TransferLen = DirColumnStrWLen;
    }
    else
    {
        switch (Configuration.SizeFormat)
        {
        case SIZE_FORMAT_BYTES:
        {
            const std::wstring value = NumberToStr(TransferFileData->Size);
            const size_t copyLength = value.size() < static_cast<size_t>(TRANSFER_BUFFER_MAX)
                                          ? value.size()
                                          : static_cast<size_t>(TRANSFER_BUFFER_MAX);
            TransferLen = static_cast<int>(copyLength);
            wmemcpy(TransferBuffer, value.data(), static_cast<size_t>(TransferLen));
            break;
        }

        case SIZE_FORMAT_KB: // WARNING: the same code is elsewhere, search for this constant
        {
            const std::wstring value = PrintDiskSize(TransferFileData->Size, 3);
            TransferLen = static_cast<int>((std::min<size_t>)(value.size(), TRANSFER_BUFFER_MAX));
            wmemcpy(TransferBuffer, value.data(), static_cast<size_t>(TransferLen));
            break;
        }

        case SIZE_FORMAT_MIXED:
        {
            const std::wstring value = PrintDiskSize(TransferFileData->Size, 0);
            TransferLen = static_cast<int>((std::min<size_t>)(value.size(), TRANSFER_BUFFER_MAX));
            wmemcpy(TransferBuffer, value.data(), static_cast<size_t>(TransferLen));
            break;
        }
        }
    }
}

void WINAPI InternalGetType()
{
    if (TransferIsDir) // we will have to handle directories differently
    {
        TransferLen = TransferIsDir == 1 ? FolderTypeNameLen : UpDirTypeNameLen;
        // TransferLen counts CHARACTERS and TransferBuffer is wchar_t, so this
        // must be wmemcpy - memcpy with a character count copied half the text, silently.
        wmemcpy(TransferBuffer, TransferIsDir == 1 ? FolderTypeName : UpDirTypeName.c_str(), TransferLen);
    }
    else
    {
        if (TransferAssocIndex == -2) // the extension lookup has not run yet
        {
            if (TransferFileData->Ext[0] != 0)
            {
                const std::wstring foldedExtension = sally::text::Fold(TransferFileData->Ext);
                if (!Associations.GetIndex(foldedExtension.c_str(), TransferAssocIndex))
                    TransferAssocIndex = -1; // not found
            }
            else
                TransferAssocIndex = -1; // without an extension -> cannot be in Associations
        }

        if (TransferAssocIndex == -1)
            GetCommonFileTypeStr(TransferBuffer, &TransferLen, TransferFileData->Ext);
        else
        {
            const wchar_t* type = Associations[TransferAssocIndex].Type;
            if (type != NULL) // valid file type
            {
                TransferLen = (int)wcslen(type);
                wmemcpy(TransferBuffer, type, TransferLen);
            }
            else
                GetCommonFileTypeStr(TransferBuffer, &TransferLen, TransferFileData->Ext);
        }
    }
}

// we can afford this optimization because we are not called from multiple threads simultaneously
static SYSTEMTIME InternalColumnST;
static FILETIME InternalColumnFT;

// The four formatters below all write TransferBuffer, which sally.h:320
// declares 'wchar_t[TRANSFER_BUFFER_MAX]' with TransferLen counting CHARACTERS. Every one of
// their twelve producers was narrow: GetDateFormatA/GetTimeFormatA on the primary paths (a
// visible error), and sprintf() on the invalid-date and format-failure fallbacks - which
// wrote NARROW BYTES into the wide buffer and returned a BYTE count, SILENTLY. The fallback
// LoadStr() was also being used AS THE FORMAT STRING; it is now passed as data via L"%s".
void WINAPI InternalGetDate()
{
    if ((TransferRowData & 0x00000001) == 0)
    {
        if (!FileTimeToLocalFileTime(&TransferFileData->LastWrite, &InternalColumnFT) ||
            !FileTimeToSystemTime(&InternalColumnFT, &InternalColumnST))
        {
            TransferLen = swprintf_s(TransferBuffer, TRANSFER_BUFFER_MAX, L"%s", LoadStrW(IDS_INVALID_DATEORTIME));
            return;
        }
        TransferRowData |= 0x00000001;
    }
    TransferLen = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &InternalColumnST, NULL, TransferBuffer, TRANSFER_BUFFER_MAX) - 1;
    if (TransferLen < 0)
        TransferLen = swprintf_s(TransferBuffer, TRANSFER_BUFFER_MAX, L"%u.%u.%u", InternalColumnST.wDay, InternalColumnST.wMonth, InternalColumnST.wYear);
}

void WINAPI InternalGetDateOnlyForDisk()
{
    if ((TransferRowData & 0x00000001) == 0)
    {
        if (!FileTimeToLocalFileTime(&TransferFileData->LastWrite, &InternalColumnFT) ||
            !FileTimeToSystemTime(&InternalColumnFT, &InternalColumnST))
        {
            TransferLen = swprintf_s(TransferBuffer, TRANSFER_BUFFER_MAX, L"%s", LoadStrW(IDS_INVALID_DATEORTIME));
            return;
        }
        TransferRowData |= 0x00000001;
    }
    if (TransferIsDir == 2 /* UP-DIR */ &&
        InternalColumnST.wYear == 1602 && InternalColumnST.wMonth == 1 && InternalColumnST.wDay == 1 &&
        InternalColumnST.wHour == 0 && InternalColumnST.wMinute == 0 && InternalColumnST.wSecond == 0 &&
        InternalColumnST.wMilliseconds == 0)
    {
        TransferLen = 0;
        return;
    }
    TransferLen = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &InternalColumnST, NULL, TransferBuffer, TRANSFER_BUFFER_MAX) - 1;
    if (TransferLen < 0)
        TransferLen = swprintf_s(TransferBuffer, TRANSFER_BUFFER_MAX, L"%u.%u.%u", InternalColumnST.wDay, InternalColumnST.wMonth, InternalColumnST.wYear);
}

void WINAPI InternalGetTime()
{
    if ((TransferRowData & 0x00000001) == 0)
    {
        if (!FileTimeToLocalFileTime(&TransferFileData->LastWrite, &InternalColumnFT) ||
            !FileTimeToSystemTime(&InternalColumnFT, &InternalColumnST))
        {
            TransferLen = swprintf_s(TransferBuffer, TRANSFER_BUFFER_MAX, L"%s", LoadStrW(IDS_INVALID_DATEORTIME));
            return;
        }
        TransferRowData |= 0x00000001;
    }
    TransferLen = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &InternalColumnST, NULL, TransferBuffer, TRANSFER_BUFFER_MAX) - 1;
    if (TransferLen < 0)
        TransferLen = swprintf_s(TransferBuffer, TRANSFER_BUFFER_MAX, L"%u:%02u:%02u", InternalColumnST.wHour, InternalColumnST.wMinute, InternalColumnST.wSecond);
}

void WINAPI InternalGetTimeOnlyForDisk()
{
    if ((TransferRowData & 0x00000001) == 0)
    {
        if (!FileTimeToLocalFileTime(&TransferFileData->LastWrite, &InternalColumnFT) ||
            !FileTimeToSystemTime(&InternalColumnFT, &InternalColumnST))
        {
            TransferLen = swprintf_s(TransferBuffer, TRANSFER_BUFFER_MAX, L"%s", LoadStrW(IDS_INVALID_DATEORTIME));
            return;
        }
        TransferRowData |= 0x00000001;
    }
    if (TransferIsDir == 2 /* UP-DIR */ &&
        InternalColumnST.wYear == 1602 && InternalColumnST.wMonth == 1 && InternalColumnST.wDay == 1 &&
        InternalColumnST.wHour == 0 && InternalColumnST.wMinute == 0 && InternalColumnST.wSecond == 0 &&
        InternalColumnST.wMilliseconds == 0)
    {
        TransferLen = 0;
        return;
    }
    TransferLen = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &InternalColumnST, NULL, TransferBuffer, TRANSFER_BUFFER_MAX) - 1;
    if (TransferLen < 0)
        TransferLen = swprintf_s(TransferBuffer, TRANSFER_BUFFER_MAX, L"%u:%02u:%02u", InternalColumnST.wHour, InternalColumnST.wMinute, InternalColumnST.wSecond);
}

void WINAPI InternalGetAttr()
{
    TransferLen = 0;
    // WARNING: if we want to display more attributes, we must rework GetAttrsStringW() and the DISPLAYED_ATTRIBUTES mask!!!
    if (TransferFileData->Attr & FILE_ATTRIBUTE_READONLY)
        TransferBuffer[TransferLen++] = 'R';
    if (TransferFileData->Attr & FILE_ATTRIBUTE_HIDDEN)
        TransferBuffer[TransferLen++] = 'H';
    if (TransferFileData->Attr & FILE_ATTRIBUTE_SYSTEM)
        TransferBuffer[TransferLen++] = 'S';
    if (TransferFileData->Attr & FILE_ATTRIBUTE_ARCHIVE)
        TransferBuffer[TransferLen++] = 'A';
    if (TransferFileData->Attr & FILE_ATTRIBUTE_TEMPORARY)
        TransferBuffer[TransferLen++] = 'T';
    if (TransferFileData->Attr & FILE_ATTRIBUTE_COMPRESSED)
        TransferBuffer[TransferLen++] = 'C';
    if (TransferFileData->Attr & FILE_ATTRIBUTE_ENCRYPTED)
        TransferBuffer[TransferLen++] = 'E';
    if (TransferFileData->Attr & FILE_ATTRIBUTE_OFFLINE)
        TransferBuffer[TransferLen++] = 'O';
}

void WINAPI InternalGetDescr()
{
    TransferLen = 0;
}

//****************************************************************************
//
// Internal function to obtain the index of simple icons for file systems with their own icons (pitFromPlugin)
//

int WINAPI InternalGetPluginIconIndex()
{
    return 0;
}

//*****************************************************************************
//
// CSalamanderView
//

CSalamanderView::CSalamanderView(CFilesWindow* panel)
{
    Panel = panel;
    Panel->GetPluginIconIndex = InternalGetPluginIconIndex;
}

DWORD
CSalamanderView::GetViewMode()
{
    return Panel->ViewTemplate->Mode;
}

void CSalamanderView::SetViewMode(DWORD viewMode, DWORD validFileData)
{
    if (Panel->ViewTemplate->Mode != viewMode)
    {
        int templateIndex;
        switch (viewMode)
        {
        case VIEW_MODE_TREE:
            templateIndex = 0;
            break;
        case VIEW_MODE_BRIEF:
            templateIndex = 1;
            break;
        case VIEW_MODE_DETAILED:
            templateIndex = 2;
            break;
        case VIEW_MODE_ICONS:
            templateIndex = 3;
            break;
        case VIEW_MODE_THUMBNAILS:
            templateIndex = 4;
            break;
        case VIEW_MODE_TILES:
            templateIndex = 5;
            break;
        default:
            TRACE_E("Unknown viewMode=" << viewMode);
            templateIndex = 2;
            break;
        }
        Panel->SelectViewTemplate(templateIndex, FALSE, TRUE, validFileData);
    }
    else
        Panel->SelectViewTemplate(Panel->GetViewTemplateIndex(), FALSE, TRUE, validFileData);
}

void CSalamanderView::SetPluginSimpleIconCallback(FGetPluginIconIndex callback)
{
    Panel->GetPluginIconIndex = callback;
}

int CSalamanderView::GetColumnsCount()
{
    return Panel->Columns.Count;
}

const CColumn*
CSalamanderView::GetColumn(int index)
{
    return (index >= 0 && index < Panel->Columns.Count) ? &Panel->Columns[index] : NULL;
}

BOOL CSalamanderView::InsertColumn(int index, const CColumn* column)
{
    int low = 1;
    if (Panel->Columns.Count > 1 && Panel->Columns[1].ID == COLUMN_ID_EXTENSION)
        low++; // must not let it wedge between Name and Ext
    if (index < low || index > Panel->Columns.Count)
    {
        TRACE_E("CSalamanderView::InsertColumn(): index=" << index << " is incorrect.");
        return FALSE;
    }
    if (column->ID != COLUMN_ID_CUSTOM)
    {
        TRACE_E("CSalamanderView::InsertColumn(): column->ID != COLUMN_ID_CUSTOM.");
        return FALSE;
    }
    Panel->Columns.Insert(index, *column);
    if (!Panel->Columns.IsGood())
    {
        TRACE_E("CSalamanderView::InsertColumn(): Columns.Insert() failed");
        Panel->Columns.ResetState();
        return FALSE;
    }
    return TRUE;
}

BOOL CSalamanderView::InsertStandardColumn(int index, DWORD id)
{
    int low = 1;
    if (Panel->Columns.Count > 1 && Panel->Columns[1].ID == COLUMN_ID_EXTENSION)
        low++; // must not let it wedge between Name and Ext
    if (index < low || index > Panel->Columns.Count ||
        index != 1 && id == COLUMN_ID_EXTENSION)
    {
        TRACE_E("CSalamanderView::InsertStandardColumn(): index=" << index << " is incorrect.");
        return FALSE;
    }
    if (id == COLUMN_ID_NAME || id == COLUMN_ID_CUSTOM)
    {
        TRACE_E("CSalamanderView::InsertStandardColumn(): column->ID == COLUMN_ID_CUSTOM or COLUMN_ID_NAME.");
        return FALSE;
    }

    CColumDataItem* item = NULL;
    int i;
    for (i = 0; i < STANDARD_COLUMNS_COUNT; i++)
    {
        if (GetStdColumn(i, FALSE)->ID == id) // found the requested standard column
        {
            item = GetStdColumn(i, FALSE);
            break;
        }
    }
    if (item != NULL)
    {
        CColumn column;
        column.CustomData = 0;
        lstrcpyW(column.Name, LoadStrW(item->NameResID));
        lstrcpyW(column.Description, LoadStrW(item->DescResID));
        column.GetText = item->GetText;
        column.SupportSorting = item->SupportSorting;
        column.LeftAlignment = item->LeftAlignment;
        column.ID = item->ID;
        CColumnConfig* colCfg = Panel->ViewTemplate->Columns;
        BOOL leftPanel = Panel == MainWindow->LeftPanel;
        column.Width = leftPanel ? colCfg[i].LeftWidth : colCfg[i].RightWidth;
        column.FixedWidth = leftPanel ? colCfg[i].LeftFixedWidth : colCfg[i].RightFixedWidth;
        column.MinWidth = 0; // dummy—will be overwritten when sizing HeaderLine

        Panel->Columns.Insert(index, column);
        if (!Panel->Columns.IsGood())
        {
            TRACE_E("CSalamanderView::InsertStandardColumn(): Columns.Insert() failed");
            Panel->Columns.ResetState();
            return FALSE;
        }
        return TRUE;
    }
    else
    {
        TRACE_E("CSalamanderView::InsertStandardColumn(): id=" << id << " is unknown.");
        return FALSE;
    }
}

static void WriteColumnTextPairToAbiRecord(wchar_t* output,
                                           std::size_t outputCapacity,
                                           const wchar_t* first,
                                           const wchar_t* second)
{
    std::size_t firstLength = std::wcslen(first);
    std::size_t secondLength = std::wcslen(second);
    if (firstLength + 1 + secondLength + 1 > outputCapacity)
    {
        const std::size_t payloadCapacity = outputCapacity - 2;
        const std::size_t half = outputCapacity / 2 - 1;
        if (secondLength <= half)
            firstLength = payloadCapacity - secondLength;
        else if (firstLength <= half)
            secondLength = payloadCapacity - firstLength;
        else
            firstLength = secondLength = half;
    }
    std::wmemcpy(output, first, firstLength);
    output[firstLength] = L'\0';
    std::wmemcpy(output + firstLength + 1, second, secondLength);
    output[firstLength + 1 + secondLength] = L'\0';
}

BOOL CSalamanderView::SetColumnName(int index, const wchar_t* name,
                                    const wchar_t* description,
                                    const wchar_t* extensionName,
                                    const wchar_t* extensionDescription)
{
    if (index < 0 || index >= Panel->Columns.Count)
    {
        TRACE_E("CSalamanderView::SetColumnName(): index=" << index << " is out of columns array range.");
        return FALSE;
    }
    if (name == NULL || *name == 0 || description == NULL || *description == 0)
    {
        TRACE_E("CSalamanderView::SetColumnName(): name or description is NULL or empty string.");
        return FALSE;
    }
    if (index == 0 && !Panel->IsExtensionInSeparateColumn() && (Panel->ValidFileData & VALID_DATA_EXTENSION))
    {
        if (extensionName == NULL || *extensionName == 0)
        {
            TRACE_E("CSalamanderView::SetColumnName(): extension name is NULL or empty.");
            return FALSE;
        }
        if (extensionDescription == NULL || *extensionDescription == 0)
        {
            TRACE_E("CSalamanderView::SetColumnName(): extension description is NULL or empty.");
            return FALSE;
        }
        WriteColumnTextPairToAbiRecord(Panel->Columns[index].Name,
                                       COLUMN_NAME_MAX, name, extensionName);
        WriteColumnTextPairToAbiRecord(Panel->Columns[index].Description,
                                       COLUMN_DESCRIPTION_MAX, description,
                                       extensionDescription);
    }
    else
    {
        lstrcpynW(Panel->Columns[index].Name, name, COLUMN_NAME_MAX);
        lstrcpynW(Panel->Columns[index].Description, description, COLUMN_DESCRIPTION_MAX);
    }
    return TRUE;
}

BOOL CSalamanderView::DeleteColumn(int index)
{
    if (index < 0 || index >= Panel->Columns.Count)
    {
        TRACE_E("CSalamanderView::DeleteColumn(): index=" << index << " is out of columns array range.");
        return FALSE;
    }
    if (index == 0)
    {
        TRACE_E("CSalamanderView::DeleteColumn(): index=" << index << " Name column can't be deleted.");
        return FALSE;
    }
    Panel->Columns.Delete(index);
    if (!Panel->Columns.IsGood())
        Panel->Columns.ResetState(); // cannot fail; the array just was not shrunk
    return TRUE;
}

BOOL CSalamanderView::IsNameColumnExtensionMerged()
{
    return !Panel->IsExtensionInSeparateColumn() &&
           (Panel->ValidFileData & VALID_DATA_EXTENSION) != 0;
}

//*****************************************************************************
//
// CFileHistoryItem, CFileHistory
//
// Holds a list of files on which the user invoked View or Edit.
//

CFileHistoryItem::CFileHistoryItem(CFileHistoryItemTypeEnum type, DWORD handlerID, const wchar_t* fileName)
{
    Type = type;
    HandlerID = handlerID;
    FileName = fileName;
    HIcon = NULL;
    if (FileName.empty())
        return;

    HIcon = GetFileOrPathIconAuxW(FileName.c_str(), FALSE, FALSE);
}

CFileHistoryItem::~CFileHistoryItem()
{
    if (HIcon != NULL)
        HANDLES(DestroyIcon(HIcon));
}

BOOL CFileHistoryItem::Equal(CFileHistoryItemTypeEnum type, DWORD handlerID, const wchar_t* fileName)
{
    // wcscmp, case-SENSITIVE, matching the lstrcmp it replaces. The wide confirmation that used
    // to be ANDed on was already case-sensitive for exactly the same reason.
    return Type == type && HandlerID == handlerID && wcscmp(FileName.c_str(), fileName) == 0;
}

BOOL CFileHistoryItem::Execute()
{
    CALL_STACK_MESSAGE1("CFileHistoryItem::Execute()");
    CFilesWindow* panel = MainWindow->GetActivePanel();
    // FileName IS the wide name now, so there is nothing to re-resolve.
    const wchar_t* fileNameW = FileName.empty() ? NULL : FileName.c_str();
    switch (Type)
    {
    case fhitView:
        panel->ViewFile(fileNameW, FALSE, HandlerID, -1, -1);
        break;
    case fhitEdit:
        panel->EditFile(fileNameW, HandlerID);
        break;
    case fhitOpen:
    {
        const size_t separator = FileName.find_last_of(L'\\');
        if (separator != std::wstring::npos)
        {
            HCURSOR hOldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
            MainWindow->SetDefaultDirectories(); // so the starting process inherits the correct current directories
            const std::wstring directory = FileName.substr(0, separator);
            ExecuteAssociationW(panel->GetListBoxHWND(), directory.c_str(), FileName.c_str() + separator + 1);
            SetCursor(hOldCur);
        }
        break;
    }

    default:
        TRACE_E("Unknown Type=" << Type);
    }

    return TRUE;
}

BOOL IsFileURLPath(const wchar_t* path)
{
    if (path == NULL)
        return FALSE;
    // skip whitespaces at the beginning of the string
    const wchar_t* s = path;
    while (*s != 0 && *s <= ' ')
        s++;
    // find the FS name
    const wchar_t* name = s;
    while (*s != 0 && *s != ':' && s - name < 4)
        s++;
    return *s == ':' && s - name == 4 && StrNICmpW(name, L"file", 4) == 0;
}

BOOL IsPluginFSPath(wchar_t* path, std::wstring* fsName, wchar_t** userPart)
{
    const wchar_t* constUserPart = NULL;
    const BOOL result = IsPluginFSPath(
        static_cast<const wchar_t*>(path), fsName,
        userPart != NULL ? &constUserPart : NULL);
    if (result && userPart != NULL)
        *userPart = const_cast<wchar_t*>(constUserPart);
    return result;
}

BOOL IsPluginFSPath(const wchar_t* path, std::wstring* fsName, const wchar_t** userPart)
{
    CALL_STACK_MESSAGE_NONE

    if (path == NULL)
        return FALSE;
    const wchar_t* start = path;
    // skip whitespaces at the beginning of the string
    while (*start >= 1 && *start <= ' ')
        start++;
    // find the FS name
    const wchar_t* name = start;
    while (*name != L'\0')
    {
        const wchar_t folded = sally::unicode::FoldCharW(*name);
        if (!((folded >= L'a' && folded <= L'z') ||
              (*name >= L'0' && *name <= L'9') || *name == L'_' ||
              *name == L'-' || *name == L'+'))
            break;
        ++name;
    }
    // test whether the FS name meets all conditions (a ':' follows and length >= 2 characters)
    if (*name == L':' && name - start >= 2)
    {
        // copy the FS name
        if (fsName != NULL)
        {
            fsName->assign(start, static_cast<size_t>(name - start));
        }
        // pointer into 'path' to the first character of the plugin-defined path (after the first ':')
        if (userPart != NULL)
            *userPart = name + 1;
        return TRUE;
    }
    else
        return FALSE;
}
