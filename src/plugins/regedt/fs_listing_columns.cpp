// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <array>

// global variables that store pointers to Salamander's global variables
// used by both the archive and FS plug-ins - the variables are shared
const CFileData** TransferFileData = NULL;
int* TransferIsDir = NULL;
wchar_t* TransferBuffer = NULL;
int* TransferLen = NULL;
DWORD* TransferRowData = NULL;
CPluginDataInterfaceAbstract** TransferPluginDataIface = NULL;
DWORD* TransferActCustomData = NULL;

int TypeDateColFW = 0; // LO/HI-WORD: left/right panel: Type/Date column: FixedWidth
int TypeDateColW = 0;  // LO/HI-WORD: left/right panel: Type/Date column: Width
int DataTimeColFW = 0; // LO/HI-WORD: left/right panel: Data/Time column: FixedWidth
int DataTimeColW = 0;  // LO/HI-WORD: left/right panel: Data/Time column: Width
int SizeColFW = 0;     // LO/HI-WORD: left/right panel: Size column: FixedWidth
int SizeColW = 0;      // LO/HI-WORD: left/right panel: Size column: Width

// ****************************************************************************
//
// CPluginDataInterface
//
//

void WINAPI GetTypeText()
{
    CALL_STACK_MESSAGE_NONE
    if (*TransferIsDir > 0)
    {
        // it's a key, print the date
        static SYSTEMTIME st;
        static int len;
        if ((*TransferFileData)->LastWrite.dwHighDateTime != 0 &&
            (*TransferFileData)->LastWrite.dwLowDateTime != 0)
        {
            FileTimeToSystemTime(&(*TransferFileData)->LastWrite, &st);
            len = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, TransferBuffer, 50) - 1;
            if (len < 0)
                len = SalPrintfW(TransferBuffer, 50, L"%u.%u.%u", st.wDay, st.wMonth, st.wYear);
            *TransferLen = len;
        }
        else
        {
            *TransferLen = 0;
        }
    }
    else
    {
        // otherwise it's a value, print its type
        switch (((CPluginData*)(*TransferFileData)->PluginData)->Type)
        {
        case REG_BINARY:
            wmemcpy(TransferBuffer, Str_REG_BINARY, Len_REG_BINARY);
            *TransferLen = Len_REG_BINARY;
            break;
        case REG_DWORD:
            wmemcpy(TransferBuffer, Str_REG_DWORD, Len_REG_DWORD);
            *TransferLen = Len_REG_DWORD;
            break;
        case REG_DWORD_BIG_ENDIAN:
            wmemcpy(TransferBuffer, Str_REG_DWORD_BIG_ENDIAN, Len_REG_DWORD_BIG_ENDIAN);
            *TransferLen = Len_REG_DWORD_BIG_ENDIAN;
            break;
        case REG_EXPAND_SZ:
            wmemcpy(TransferBuffer, Str_REG_EXPAND_SZ, Len_REG_EXPAND_SZ);
            *TransferLen = Len_REG_EXPAND_SZ;
            break;
        case REG_LINK:
            wmemcpy(TransferBuffer, Str_REG_LINK, Len_REG_LINK);
            *TransferLen = Len_REG_LINK;
            break;
        case REG_MULTI_SZ:
            wmemcpy(TransferBuffer, Str_REG_MULTI_SZ, Len_REG_MULTI_SZ);
            *TransferLen = Len_REG_MULTI_SZ;
            break;
        case REG_NONE:
            wmemcpy(TransferBuffer, Str_REG_NONE, Len_REG_NONE);
            *TransferLen = Len_REG_NONE;
            break;
        case REG_QWORD:
            wmemcpy(TransferBuffer, Str_REG_QWORD, Len_REG_QWORD);
            *TransferLen = Len_REG_QWORD;
            break;
        case REG_RESOURCE_LIST:
            wmemcpy(TransferBuffer, Str_REG_RESOURCE_LIST, Len_REG_RESOURCE_LIST);
            *TransferLen = Len_REG_RESOURCE_LIST;
            break;
        case REG_SZ:
            wmemcpy(TransferBuffer, Str_REG_SZ, Len_REG_SZ);
            *TransferLen = Len_REG_SZ;
            break;

        case REG_FULL_RESOURCE_DESCRIPTOR:
            wmemcpy(TransferBuffer, Str_REG_FULL_RESOURCE_DESCRIPTOR, Len_REG_FULL_RESOURCE_DESCRIPTOR);
            *TransferLen = Len_REG_FULL_RESOURCE_DESCRIPTOR;
            break;

        case REG_RESOURCE_REQUIREMENTS_LIST:
            wmemcpy(TransferBuffer, Str_REG_RESOURCE_REQUIREMENTS_LIST, Len_REG_RESOURCE_REQUIREMENTS_LIST);
            *TransferLen = Len_REG_RESOURCE_REQUIREMENTS_LIST;
            break;

        default:
            TRACE_E("unknown value type");
            TransferBuffer[0] = L'?';
            *TransferLen = 1;
        }
    }
}

void WINAPI GetDataText()
{
    CALL_STACK_MESSAGE_NONE
    if (*TransferIsDir > 0)
    {
        // it's a key, print the time
        static SYSTEMTIME st;
        static int len;
        if ((*TransferFileData)->LastWrite.dwHighDateTime != 0 &&
            (*TransferFileData)->LastWrite.dwLowDateTime != 0)
        {
            FileTimeToSystemTime(&(*TransferFileData)->LastWrite, &st);
            len = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, TransferBuffer, 50) - 1;
            if (len < 0)
                len = SalPrintfW(TransferBuffer, 50, L"%u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
            *TransferLen = len;
        }
        else
        {
            *TransferLen = 0;
        }
    }
    else
    {
        // it's a value, print it
        static CPluginData* pluginData;
        pluginData = ((CPluginData*)(*TransferFileData)->PluginData);

        if ((*TransferFileData)->Size > CQuadWord(0, 0))
        {
            switch (pluginData->Type)
            {
            case REG_MULTI_SZ:
            case REG_EXPAND_SZ:
            case REG_SZ:
                if (pluginData->Data && pluginData->DataSize)
                {
                    wmemcpy(TransferBuffer, (const WCHAR*)pluginData->Data, pluginData->DataSize);
                    *TransferLen = pluginData->DataSize - 1;
                }
                else
                    *TransferLen = 0;
                break;

            case REG_DWORD_BIG_ENDIAN:
            case REG_DWORD:
                if ((*TransferFileData)->Size == CQuadWord(4, 0))
                    *TransferLen = SalPrintfW(TransferBuffer, TRANSFER_BUFFER_MAX, L"0x%08x (%u)", (DWORD)pluginData->Data, (DWORD)pluginData->Data); // FIXME_X64 - verify the cast to (DWORD)
                else
                    *TransferLen = 0;
                break;

            case REG_QWORD:
                if ((*TransferFileData)->Size == CQuadWord(8, 0))
                {
                    QWORD q = *(LPQWORD)pluginData->Data;
                    *TransferLen = SalPrintfW(TransferBuffer, TRANSFER_BUFFER_MAX, L"0x%016I64x (%I64u)", q, q);
                }
                else
                    *TransferLen = 0;
                break;

            default:
                if (pluginData->Data)
                    wmemcpy(TransferBuffer, (const WCHAR*)pluginData->Data, pluginData->DataSize);
                *TransferLen = pluginData->DataSize;
            }
        }
        else
        {
            *TransferLen = NULL;
        }
    }
}

void WINAPI GetSizeText()
{
    CALL_STACK_MESSAGE_NONE
    if (*TransferIsDir > 0)
    {
        // it's a key, print "KEY"
        *TransferLen = static_cast<int>((std::min<size_t>)(
            KeyText.size(), TRANSFER_BUFFER_MAX));
        wmemcpy(TransferBuffer, KeyText.data(), static_cast<size_t>(*TransferLen));
    }
    else
    {
        // it's a value, print its size
        const std::wstring number = SPLNumberToStrOwned(SG, (*TransferFileData)->Size);
        *TransferLen = static_cast<int>((std::min<size_t>)(number.size(), TRANSFER_BUFFER_MAX));
        wmemcpy(TransferBuffer, number.data(), static_cast<size_t>(*TransferLen));
    }
}

int WINAPI
PluginSimpleIconCallback()
{
    CALL_STACK_MESSAGE_NONE
    if (*TransferIsDir > 0)
    {
        return 0;
    }
    else
    {
        switch (((CPluginData*&)(*TransferFileData)->PluginData)->Type)
        {
        case REG_EXPAND_SZ:
        case REG_MULTI_SZ:
        case REG_SZ:
            return 1;

        case REG_BINARY:
        case REG_DWORD:
        case REG_DWORD_BIG_ENDIAN:
        case REG_QWORD:
        case REG_LINK:
        case REG_RESOURCE_LIST:
        case REG_FULL_RESOURCE_DESCRIPTOR:
        case REG_RESOURCE_REQUIREMENTS_LIST:
            return 2;

        case REG_NONE:
        default:
            return 3;
        }
    }
}

HIMAGELIST
CPluginDataInterface::GetSimplePluginIcons(int iconSize)
{
    CALL_STACK_MESSAGE1("CPluginDataInterface::GetSimplePluginIcons()");
    return ImageList;
}

void CPluginDataInterface::SetupView(BOOL leftPanel, CSalamanderViewAbstract* view, const wchar_t* archivePath,
                                     const CFileData* upperDir)
{
    CALL_STACK_MESSAGE2("CPluginDataInterface::SetupView(, , %ls,)", archivePath);
    view->GetTransferVariables(TransferFileData, TransferIsDir, TransferBuffer, TransferLen, TransferRowData,
                               TransferPluginDataIface, TransferActCustomData);

    view->SetPluginSimpleIconCallback(PluginSimpleIconCallback);

    if (view->GetViewMode() == VIEW_MODE_DETAILED) // adjust the columns
    {
        view->SetViewMode(VIEW_MODE_DETAILED, VALID_DATA_NONE); // remove all columns

        int i = 1;
        CColumn column;

        // add the Type/Date column
        lstrcpynW(column.Name, LoadStrW(IDS_TYPE).c_str(), _countof(column.Name));
        lstrcpynW(column.Description, LoadStrW(IDS_TYPE).c_str(), _countof(column.Description));
        column.GetText = GetTypeText;
        column.SupportSorting = 0;
        column.LeftAlignment = 1;
        column.ID = COLUMN_ID_CUSTOM;
        column.Width = leftPanel ? LOWORD(TypeDateColW) : HIWORD(TypeDateColW);
        column.FixedWidth = leftPanel ? LOWORD(TypeDateColFW) : HIWORD(TypeDateColFW);
        column.CustomData = 1;
        view->InsertColumn(i++, &column);

        // add the Data/Time column
        lstrcpynW(column.Name, LoadStrW(IDS_DATA).c_str(), _countof(column.Name));
        lstrcpynW(column.Description, LoadStrW(IDS_DATA).c_str(), _countof(column.Description));
        column.GetText = GetDataText;
        column.SupportSorting = 0;
        column.LeftAlignment = 1;
        column.ID = COLUMN_ID_CUSTOM;
        column.Width = leftPanel ? LOWORD(DataTimeColW) : HIWORD(DataTimeColW);
        column.FixedWidth = leftPanel ? LOWORD(DataTimeColFW) : HIWORD(DataTimeColFW);
        column.CustomData = 2;
        view->InsertColumn(i++, &column);

        // add the Size column
        lstrcpynW(column.Name, LoadStrW(IDS_SIZE).c_str(), _countof(column.Name));
        lstrcpynW(column.Description, LoadStrW(IDS_SIZE).c_str(), _countof(column.Description));
        column.GetText = GetSizeText;
        column.SupportSorting = 1;
        column.LeftAlignment = 0;
        column.ID = COLUMN_ID_CUSTOM;
        column.Width = leftPanel ? LOWORD(SizeColW) : HIWORD(SizeColW);
        column.FixedWidth = leftPanel ? LOWORD(SizeColFW) : HIWORD(SizeColFW);
        column.CustomData = 3;
        view->InsertColumn(i++, &column);
    }
}

void CPluginDataInterface::ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth)
{
    if (leftPanel)
    {
        switch (column->CustomData)
        {
        case 1:
            TypeDateColFW = MAKELONG(newFixedWidth, HIWORD(TypeDateColFW));
            break;
        case 2:
            DataTimeColFW = MAKELONG(newFixedWidth, HIWORD(DataTimeColFW));
            break;
        case 3:
            SizeColFW = MAKELONG(newFixedWidth, HIWORD(SizeColFW));
            break;
        }
    }
    else
    {
        switch (column->CustomData)
        {
        case 1:
            TypeDateColFW = MAKELONG(LOWORD(TypeDateColFW), newFixedWidth);
            break;
        case 2:
            DataTimeColFW = MAKELONG(LOWORD(DataTimeColFW), newFixedWidth);
            break;
        case 3:
            SizeColFW = MAKELONG(LOWORD(SizeColFW), newFixedWidth);
            break;
        }
    }
    if (newFixedWidth)
        ColumnWidthWasChanged(leftPanel, column, column->Width);
}

void CPluginDataInterface::ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column, int newWidth)
{
    if (leftPanel)
    {
        switch (column->CustomData)
        {
        case 1:
            TypeDateColW = MAKELONG(newWidth, HIWORD(TypeDateColW));
            break;
        case 2:
            DataTimeColW = MAKELONG(newWidth, HIWORD(DataTimeColW));
            break;
        case 3:
            SizeColW = MAKELONG(newWidth, HIWORD(SizeColW));
            break;
        }
    }
    else
    {
        switch (column->CustomData)
        {
        case 1:
            TypeDateColW = MAKELONG(LOWORD(TypeDateColW), newWidth);
            break;
        case 2:
            DataTimeColW = MAKELONG(LOWORD(DataTimeColW), newWidth);
            break;
        case 3:
            SizeColW = MAKELONG(LOWORD(SizeColW), newWidth);
            break;
        }
    }
}

const wchar_t* WINAPI FSInfoLineName(HWND parent, void* param)
{
    CALL_STACK_MESSAGE1("FSInfoLineName(, )");
    return (const WCHAR*)param;
}

const wchar_t* WINAPI FSInfoLineSize(HWND parent, void* param)
{
    CALL_STACK_MESSAGE1("FSInfoLineSize(, )");
    GetSizeText();
    TransferBuffer[*TransferLen] = L'\0';
    return TransferBuffer;
}

const wchar_t* WINAPI FSInfoLineDate(HWND parent, void* param)
{
    CALL_STACK_MESSAGE1("FSInfoLineDate(, )");
    GetTypeText();
    TransferBuffer[*TransferLen] = L'\0';
    return TransferBuffer;
}

const wchar_t* WINAPI FSInfoLineTime(HWND parent, void* param)
{
    CALL_STACK_MESSAGE1("FSInfoLineTime(, )");
    GetDataText();
    TransferBuffer[*TransferLen] = L'\0';
    return TransferBuffer;
}

CSalamanderVarStrEntry FSInfoLine[] =
    {
        {L"Name", FSInfoLineName},
        {L"Size", FSInfoLineSize},
        {L"Date", FSInfoLineDate},
        {L"Time", FSInfoLineTime},
        {NULL, NULL}};

std::wstring ExpandPluralSelection(int files, int dirs)
{
    CALL_STACK_MESSAGE3("ExpandPluralSelection(%d, %d)", files, dirs);

    if (files > 0 && dirs > 0)
    {
        CQuadWord parametersArray[] = {CQuadWord(files, 0), CQuadWord(dirs, 0)};
        const std::wstring format = SPLExpandPluralStringOwned(
            SG, LoadStrW(IDS_SELECTED3).c_str(), 2, parametersArray);
        return SPLFormatStringOwned(format.c_str(), files, dirs);
    }
    CQuadWord param = CQuadWord(files + dirs, 0);
    const std::wstring format = SPLExpandPluralStringOwned(
        SG, LoadStrW(files ? IDS_SELECTED1 : IDS_SELECTED2).c_str(), 1, &param);
    return SPLFormatStringOwned(format.c_str(), files + dirs);
}

BOOL CPluginDataInterface::GetInfoLineContent(
    int panel, const CFileData* file, BOOL isDir, int selectedFiles,
    int selectedDirs, BOOL displaySize, const CQuadWord& selectedSize,
    CSalamanderStringBuffer* buffer, CSalamanderTextRangeBuffer* hotTexts)
{
    CALL_STACK_MESSAGE7("CPluginDataInterface::GetInfoLineContent(%d, , %d, %d, "
                        "%d, %d, %g, , )",
                        panel, isDir, selectedFiles,
                        selectedDirs, displaySize, selectedSize.GetDouble());
    if (buffer == NULL || hotTexts == NULL)
        return FALSE;
    if (file != NULL)
    {
        // ExpandVarString's callback transfer area is an exact live-SDK
        // contract, call-scoped here rather than retained as text ownership.
        std::array<wchar_t, TRANSFER_BUFFER_MAX + 1> liveVarTransferBuffer{};
        const CFileData** oldTransferFileData = TransferFileData;
        int* oldTransferIsDir = TransferIsDir;
        wchar_t* oldTransferBuffer = TransferBuffer;
        int* oldTransferLen = TransferLen;
        //DWORD            *TransferRowData = NULL;
        //CPluginDataInterfaceAbstract **TransferPluginDataIface = NULL;
        //DWORD            *TransferActCustomData = NULL;
        TransferFileData = &file;
        TransferIsDir = &isDir;
        TransferBuffer = liveVarTransferBuffer.data();
        int localTransferLen;
        TransferLen = &localTransferLen;

        BOOL published = SG->ExpandVarString(
            SG->GetMsgBoxParent(), L"$(Name): $(Size), $(Date), $(Time)",
            buffer, FSInfoLine, file->Name, FALSE, hotTexts);
        if (!published)
        {
            const std::vector<CSalamanderTextRange> noRanges;
            published = sally::plugin_abi::WriteTextAndRanges(
                            *buffer, *hotTexts, L"Error!", noRanges)
                            ? TRUE
                            : FALSE;
        }

        TransferFileData = oldTransferFileData;
        TransferIsDir = oldTransferIsDir;
        TransferBuffer = oldTransferBuffer;
        TransferLen = oldTransferLen;

        return published;
    }
    else
    {
        if (selectedFiles == 0 && selectedDirs == 0) // information line for an empty panel
            return FALSE;                            // let Salamander print the text

        std::wstring text;
        if (displaySize)
        {
            // for simplicity we do not use "plural" strings (see SalamanderGeneral->ExpandPluralString())
            const std::wstring format = SPLExpandPluralStringOwned(
                SG, LoadStrW(IDS_SELECTEDSIZE).c_str(), 1, &selectedSize);
            text = SPLFormatStringOwned(format.c_str(), selectedSize.Value);

            /*    // example of using the standard string
      SalamanderGeneral->ExpandPluralBytesFilesDirs(buffer, 1000, selectedSize, selectedFiles,
                                                    selectedDirs, TRUE);
*/
        }
        text += ExpandPluralSelection(selectedFiles, selectedDirs);
        std::vector<CSalamanderTextRange> ranges;
        if (!SPLLookForSubTextsOwned(SG, text, ranges))
            return FALSE;
        return sally::plugin_abi::WriteTextAndRanges(
                   *buffer, *hotTexts, text, ranges)
                   ? TRUE
                   : FALSE;
    }
}
