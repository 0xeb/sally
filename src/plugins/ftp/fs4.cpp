// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

//
// ****************************************************************************
// CFTPListingPluginDataInterface
//

void WINAPI GetTextFromGeneralTextColumn()
{
    char* s = *(char**)(((char*)((*TransferFileData)->PluginData)) + (*TransferActCustomData));
    if (s != NULL)
    {
        // The parser owns negotiated server bytes; the listing interface carries
        // the matching codec to this presentation callback.
        std::wstring text;
        CFTPListingPluginDataInterface* data =
            static_cast<CFTPListingPluginDataInterface*>(*TransferPluginDataIface);
        if (data != NULL &&
            data->DecodeTextForPresentation(std::string_view(s, strlen(s)), text))
        {
            *TransferLen = static_cast<int>((std::min)(
                text.size(), static_cast<size_t>(TRANSFER_BUFFER_MAX)));
            wmemcpy(TransferBuffer, text.c_str(), *TransferLen);
        }
        else
            *TransferLen = 0;
    }
    else
        *TransferLen = 0;
}

SYSTEMTIME GlobalGeneralDateTimeStruct = {2002, 1, 0, 1, 0, 0, 0, 0}; // helper global variable, columns are populated only in the main thread, no sync problems

void WINAPI GetTextFromGeneralDateColumn()
{
    CFTPDate* date = (CFTPDate*)(((char*)((*TransferFileData)->PluginData)) + (*TransferActCustomData));
    if (date->Day != 0) // should not display ""
    {
        GlobalGeneralDateTimeStruct.wDay = date->Day;
        GlobalGeneralDateTimeStruct.wMonth = date->Month;
        GlobalGeneralDateTimeStruct.wYear = date->Year;
        int len;
        if ((len = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &GlobalGeneralDateTimeStruct,
                                  NULL, TransferBuffer, TRANSFER_BUFFER_MAX)) == 0)
        {
            len = 1 + swprintf(TransferBuffer, TRANSFER_BUFFER_MAX, L"%u.%u.%u", GlobalGeneralDateTimeStruct.wDay,
                               GlobalGeneralDateTimeStruct.wMonth, GlobalGeneralDateTimeStruct.wYear);
        }
        *TransferLen = len - 1;
    }
    else
        *TransferLen = 0;
}

void WINAPI GetTextFromGeneralTimeColumn()
{
    CFTPTime* time = (CFTPTime*)(((char*)((*TransferFileData)->PluginData)) + (*TransferActCustomData));
    if (time->Hour != 24) // should not display ""
    {
        GlobalGeneralDateTimeStruct.wHour = time->Hour;
        GlobalGeneralDateTimeStruct.wMinute = time->Minute;
        GlobalGeneralDateTimeStruct.wSecond = time->Second;
        GlobalGeneralDateTimeStruct.wMilliseconds = time->Millisecond;
        int len;
        if ((len = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &GlobalGeneralDateTimeStruct,
                                  NULL, TransferBuffer, TRANSFER_BUFFER_MAX)) == 0)
        {
            len = 1 + swprintf(TransferBuffer, TRANSFER_BUFFER_MAX, L"%u:%02u:%02u", GlobalGeneralDateTimeStruct.wHour,
                               GlobalGeneralDateTimeStruct.wMinute, GlobalGeneralDateTimeStruct.wSecond);
        }
        *TransferLen = len - 1;
    }
    else
        *TransferLen = 0;
}

void WINAPI GetTextFromGeneralNumberColumn()
{
    __int64 int64Val = *(__int64*)(((char*)((*TransferFileData)->PluginData)) + (*TransferActCustomData));
    if (int64Val >= 0)
    {
        const std::wstring number = SPLNumberToStrOwned(
            SalamanderGeneral, CQuadWord().SetUI64((unsigned __int64)int64Val));
        *TransferLen = static_cast<int>((std::min<size_t>)(number.size(), TRANSFER_BUFFER_MAX));
        wmemcpy(TransferBuffer, number.data(), static_cast<size_t>(*TransferLen));
    }
    else
    {
        if (int64Val != INT64_EMPTYNUMBER) // should not display ""
        {
            std::wstring number(1, L'-');
            number += SPLNumberToStrOwned(
                SalamanderGeneral, CQuadWord().SetUI64((unsigned __int64)(-int64Val)));
            *TransferLen = static_cast<int>((std::min<size_t>)(number.size(), TRANSFER_BUFFER_MAX));
            wmemcpy(TransferBuffer, number.data(), static_cast<size_t>(*TransferLen));
        }
        else
            *TransferLen = 0;
    }
}

void CFTPListingPluginDataInterface::SetupView(BOOL leftPanel, CSalamanderViewAbstract* view,
                                               const wchar_t* archivePath, const CFileData* upperDir)
{
    view->GetTransferVariables(TransferFileData, TransferIsDir, TransferBuffer, TransferLen,
                               TransferRowData, TransferPluginDataIface, TransferActCustomData);

    // adjust columns only in detailed mode
    if (view->GetViewMode() == VIEW_MODE_DETAILED)
    {
        BOOL sepExt = FALSE; // TRUE = standalone "Extension" column, FALSE = extension is part of the "Name" column
        if (view->GetColumnsCount() > 1 && view->GetColumn(1)->ID == COLUMN_ID_EXTENSION)
            sepExt = TRUE;

        view->SetViewMode(VIEW_MODE_DETAILED, VALID_DATA_NONE); // drop the other columns, keep only the Name column

        int colCount = 1;
        int i;
        for (i = 0; i < Columns->Count; i++)
        {
            CSrvTypeColumn* col = Columns->At(i);

            if (col->Visible) // show the column only when it is visible
            {
                std::wstring colNameStorage;
                if (col->NameID != -1)
                    LoadStdColumnStrName(col->NameID, colNameStorage);
                else
                    FtpDecodeLocalText(HandleNULLStr(col->NameStr), colNameStorage);

                std::wstring colDescrStorage;
                if (col->DescrID != -1)
                    LoadStdColumnStrDescr(col->DescrID, colDescrStorage);
                else
                    FtpDecodeLocalText(HandleNULLStr(col->DescrStr), colDescrStorage);

                // Server-type records remain explicitly encoded bytes; the panel header is UTF-16.
                const wchar_t* colName = colNameStorage.c_str();
                const wchar_t* colDescr = colDescrStorage.c_str();

                switch (col->Type)
                {
                case stctName: // the Name column is already inserted (cannot be removed), just tweak the name+description
                {
                    view->SetColumnName(i, colName, colDescr,
                                        L"a", L"a"); // dummy title and description of the "Ext" column (always needed because the "Name" column is currently the only one in the panel)

                    CColumn* c = (CColumn*)(view->GetColumn(0));

                    break;
                }

                case stctExt:
                {
                    if (sepExt) // user prefers a standalone column for the extension
                    {
                        view->InsertStandardColumn(colCount, COLUMN_ID_EXTENSION);
                        view->SetColumnName(colCount, colName, colDescr);
                        colCount++;
                    }
                    else // user prefers the name and extension in one column
                    {
                        const CColumn* nameColumn = view->GetColumn(0);
                        view->SetColumnName(0, nameColumn->Name,
                                            nameColumn->Description,
                                            colName, colDescr);
                    }
                    break;
                }

                case stctSize:
                {
                    view->InsertStandardColumn(colCount, COLUMN_ID_SIZE);
                    view->SetColumnName(colCount, colName, colDescr);
                    colCount++;
                    break;
                }

                case stctDate:
                {
                    view->InsertStandardColumn(colCount, COLUMN_ID_DATE);
                    view->SetColumnName(colCount, colName, colDescr);
                    colCount++;
                    break;
                }

                case stctTime:
                {
                    view->InsertStandardColumn(colCount, COLUMN_ID_TIME);
                    view->SetColumnName(colCount, colName, colDescr);
                    colCount++;
                    break;
                }

                case stctType:
                {
                    view->InsertStandardColumn(colCount, COLUMN_ID_TYPE);
                    view->SetColumnName(colCount, colName, colDescr);
                    colCount++;
                    break;
                }

                case stctGeneralText:
                case stctGeneralDate:
                case stctGeneralTime:
                case stctGeneralNumber:
                {
                    CColumn column;
                    lstrcpynW(column.Name, colName, COLUMN_NAME_MAX);
                    lstrcpynW(column.Description, colDescr, COLUMN_DESCRIPTION_MAX);
                    switch (col->Type)
                    {
                    case stctGeneralText:
                        column.GetText = GetTextFromGeneralTextColumn;
                        break;
                    case stctGeneralDate:
                        column.GetText = GetTextFromGeneralDateColumn;
                        break;
                    case stctGeneralTime:
                        column.GetText = GetTextFromGeneralTimeColumn;
                        break;
                    case stctGeneralNumber:
                        column.GetText = GetTextFromGeneralNumberColumn;
                        break;
                    default:
                        column.GetText = NULL;
                        TRACE_E("Fatal error in CFTPListingPluginDataInterface::SetupView(): unknown column type!");
                        break;
                    }
                    column.CustomData = DataOffsets[i];
#ifdef _DEBUG
                    if (column.CustomData == -1)
                        TRACE_E("Fatal error in CFTPListingPluginDataInterface::SetupView(): invalid column data offset!");
#endif
                    column.SupportSorting = 0;
                    column.LeftAlignment = col->LeftAlignment;
                    column.ID = COLUMN_ID_CUSTOM;
                    column.Width = leftPanel ? LOWORD(col->ColWidths->Width) : HIWORD(col->ColWidths->Width);
                    column.FixedWidth = leftPanel ? LOWORD(col->ColWidths->FixedWidth) : HIWORD(col->ColWidths->FixedWidth);
                    view->InsertColumn(colCount++, &column);
                    break;
                }
                }
            }
        }
    }
}

void CFTPListingPluginDataInterface::ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth)
{
    int i;
    for (i = 0; i < Columns->Count; i++)
    {
        if (DataOffsets[i] == column->CustomData)
        {
            CSrvTypeColumn* col = Columns->At(i);
            if (leftPanel)
                col->ColWidths->FixedWidth = MAKELONG(newFixedWidth, HIWORD(col->ColWidths->FixedWidth));
            else
                col->ColWidths->FixedWidth = MAKELONG(LOWORD(col->ColWidths->FixedWidth), newFixedWidth);
            if (newFixedWidth)
                ColumnWidthWasChanged(leftPanel, column, column->Width);
            return;
        }
    }
    TRACE_E("CFTPListingPluginDataInterface::ColumnFixedWidthShouldChange(): unexpected situation: column not found!");
}

void CFTPListingPluginDataInterface::ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column, int newWidth)
{
    int i;
    for (i = 0; i < Columns->Count; i++)
    {
        if (DataOffsets[i] == column->CustomData)
        {
            CSrvTypeColumn* col = Columns->At(i);
            if (leftPanel)
                col->ColWidths->Width = MAKELONG(newWidth, HIWORD(col->ColWidths->Width));
            else
                col->ColWidths->Width = MAKELONG(LOWORD(col->ColWidths->Width), newWidth);
            return;
        }
    }
    TRACE_E("CFTPListingPluginDataInterface::ColumnWidthWasChanged(): unexpected situation: column not found!");
}

static std::wstring FormatDateOwned(const SYSTEMTIME& value)
{
    const int required = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE,
                                        &value, NULL, NULL, 0);
    if (required > 0)
    {
        std::vector<wchar_t> buffer(static_cast<size_t>(required), L'\0');
        const int written = GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE,
                                           &value, NULL, buffer.data(), required);
        if (written > 0)
            return std::wstring(buffer.data(), static_cast<size_t>(written - 1));
    }
    return SPLFormatStringOwned(L"%u.%u.%u", value.wDay, value.wMonth,
                                value.wYear);
}

static std::wstring FormatTimeOwned(const SYSTEMTIME& value)
{
    const int required = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &value,
                                        NULL, NULL, 0);
    if (required > 0)
    {
        std::vector<wchar_t> buffer(static_cast<size_t>(required), L'\0');
        const int written = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &value,
                                           NULL, buffer.data(), required);
        if (written > 0)
            return std::wstring(buffer.data(), static_cast<size_t>(written - 1));
    }
    return SPLFormatStringOwned(L"%u:%02u:%02u", value.wHour, value.wMinute,
                                value.wSecond);
}

BOOL CFTPListingPluginDataInterface::GetInfoLineContent(int panel, const CFileData* file, BOOL isDir,
                                                        int selectedFiles, int selectedDirs,
                                                        BOOL displaySize, const CQuadWord& selectedSize,
                                                        CSalamanderStringBuffer* buffer,
                                                        CSalamanderTextRangeBuffer* hotTexts)
{
    if (buffer == NULL || hotTexts == NULL)
        return FALSE;

    if (file == NULL)
    {
        if (selectedFiles == 0 && selectedDirs == 0)                                  // Information Line for an empty panel
            return FALSE;                                                             // let Salamander print the text
        if (BytesColumnOffset == -1 && BlocksColumnOffset == -1 || selectedDirs != 0) // no size in bytes nor blocks or directories are selected too
            return FALSE;                                                             // let Salamander print the counts of selected files and folders
        // when only files are selected (block size is unknown for directories)
        // sum up the number of blocks
        DWORD numOffset = (BytesColumnOffset != -1 ? BytesColumnOffset : BlocksColumnOffset);
        __int64 size = 0;
        int index = 0;
        const CFileData* file2 = NULL;
        while ((file2 = SalamanderGeneral->GetPanelSelectedItem(panel, &index, NULL)) != NULL)
        {
            __int64 s = *(__int64*)(((char*)(file2->PluginData)) + numOffset);
            if (s != INT64_EMPTYNUMBER)
                size += s;
            else
                return FALSE; // if the column contains an empty value we cannot compute the total size, so stop here and let Salamander print the counts of selected files and folders
        }

        CQuadWord params[2];
        params[0].SetUI64(size);
        if (BytesColumnOffset == -1) // size in blocks
        {
            params[1].Set(selectedFiles, 0);
            const std::wstring num1Text = SPLNumberToStrOwned(SalamanderGeneral, params[0]);
            const std::wstring num2Text = SPLNumberToStrOwned(SalamanderGeneral, params[1]);
            const std::wstring format = SPLExpandPluralStringOwned(
                SalamanderGeneral,
                SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLSTR_BLOCKSINSELFILES).c_str(),
                2, params);
            std::wstring text = SPLFormatStringOwned(format.c_str(), num1Text.c_str(), num2Text.c_str());
            std::vector<CSalamanderTextRange> ranges;
            if (!SPLLookForSubTextsOwned(SalamanderGeneral, text, ranges))
                return FALSE;
            return sally::plugin_abi::WriteTextAndRanges(
                       *buffer, *hotTexts, text, ranges)
                       ? TRUE
                       : FALSE;
        }
        std::wstring text = SPLExpandPluralBytesFilesDirsOwned(
            SalamanderGeneral, params[0], selectedFiles, 0, TRUE);
        std::vector<CSalamanderTextRange> ranges;
        if (!SPLLookForSubTextsOwned(SalamanderGeneral, text, ranges))
            return FALSE;
        return sally::plugin_abi::WriteTextAndRanges(
                   *buffer, *hotTexts, text, ranges)
                   ? TRUE
                   : FALSE;
    }
    else
    {
        std::wstring text;
        std::vector<CSalamanderTextRange> ranges;
        FILETIME ft;
        SYSTEMTIME st;
        BOOL stEmpty = TRUE;
        BOOL separate = FALSE;
        const auto appendSeparator = [&]() {
            if (separate)
                text += L", ";
            else
                separate = TRUE;
        };
        const auto appendHotText = [&](const std::wstring& value) -> bool {
            if (value.empty())
                return true;
            if (text.size() > (std::numeric_limits<DWORD>::max)() ||
                value.size() > (std::numeric_limits<DWORD>::max)())
                return false;
            ranges.push_back({static_cast<DWORD>(text.size()),
                              static_cast<DWORD>(value.size())});
            text += value;
            return true;
        };
        for (int i = 0; i < Columns->Count; i++)
        {
            switch (Columns->At(i)->Type)
            {
            case stctName:
            {
                int fileNameFormat;
                SalamanderGeneral->GetConfigParameter(SALCFG_FILENAMEFORMAT, &fileNameFormat,
                                                      sizeof(fileNameFormat), NULL);
                std::wstring formattedFileName;
                SPLAlterFileNameOwned(SalamanderGeneral, file->Name,
                                      fileNameFormat, 0, isDir,
                                      formattedFileName);
                if (!appendHotText(formattedFileName.c_str()))
                    return FALSE;
                text += L": ";
                break;
            }

                // case stctExt: break;  // it is part of "Name"

            case stctSize:
            {
                appendSeparator();
                std::wstring value;
                if (isDir)
                    value = SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_SRVTYPE_SIZEISDIR).c_str();
                else
                {
                    value = SPLNumberToStrOwned(SalamanderGeneral, file->Size);
                }
                if (!appendHotText(value))
                    return FALSE;
                break;
            }

            case stctDate:
            case stctTime:
            {
                if (stEmpty)
                {
                    FileTimeToLocalFileTime(&file->LastWrite, &ft);
                    FileTimeToSystemTime(&ft, &st);
                    stEmpty = FALSE;
                }
                appendSeparator();
                // NOTE: inherited behaviour - the Time column has always rendered the date here.
                if (!appendHotText(FormatDateOwned(st)))
                    return FALSE;
                break;
            }

                // case stctType: break; // we do not include it in the info line

            case stctGeneralText:
            {
                appendSeparator();
                char* txt = *(char**)(((char*)(file->PluginData)) + DataOffsets[i]);
                if (txt != NULL)
                {
                    std::wstring text;
                    if (!DecodeTextForPresentation(std::string_view(txt, strlen(txt)), text) ||
                        !appendHotText(text))
                        return FALSE;
                }
                break;
            }

            case stctGeneralDate:
            {
                appendSeparator();
                CFTPDate* date = (CFTPDate*)(((char*)(file->PluginData)) + DataOffsets[i]);
                if (date->Day != 0) // should not display ""
                {
                    GlobalGeneralDateTimeStruct.wDay = date->Day;
                    GlobalGeneralDateTimeStruct.wMonth = date->Month;
                    GlobalGeneralDateTimeStruct.wYear = date->Year;
                    if (!appendHotText(FormatDateOwned(GlobalGeneralDateTimeStruct)))
                        return FALSE;
                }
                break;
            }

            case stctGeneralTime:
            {
                appendSeparator();
                CFTPTime* time = (CFTPTime*)(((char*)(file->PluginData)) + DataOffsets[i]);
                if (time->Hour != 24) // should not display ""
                {
                    GlobalGeneralDateTimeStruct.wHour = time->Hour;
                    GlobalGeneralDateTimeStruct.wMinute = time->Minute;
                    GlobalGeneralDateTimeStruct.wSecond = time->Second;
                    GlobalGeneralDateTimeStruct.wMilliseconds = time->Millisecond;
                    if (!appendHotText(FormatTimeOwned(GlobalGeneralDateTimeStruct)))
                        return FALSE;
                }
                break;
            }

            case stctGeneralNumber:
            {
                appendSeparator();
                __int64 int64Val = *(__int64*)(((char*)(file->PluginData)) + DataOffsets[i]);
                std::wstring value;
                if (int64Val >= 0)
                {
                    value = SPLNumberToStrOwned(
                        SalamanderGeneral, CQuadWord().SetUI64((unsigned __int64)int64Val));
                }
                else if (int64Val != INT64_EMPTYNUMBER) // should not display ""
                {
                    const std::wstring magnitude = SPLNumberToStrOwned(
                        SalamanderGeneral, CQuadWord().SetUI64(
                                              static_cast<unsigned __int64>(-(int64Val + 1)) + 1));
                    value = L'-';
                    value += magnitude;
                }
                if (!appendHotText(value))
                    return FALSE;
                break;
            }
            }
        }
        return sally::plugin_abi::WriteTextAndRanges(
                   *buffer, *hotTexts, text, ranges)
                   ? TRUE
                   : FALSE;
    }
}

//
// ****************************************************************************
// CPluginFSInterface
//

CFTPQueueItem* CreateItemForDeleteOperation(const CFileData* f, BOOL isDir, int rightsCol,
                                            CFTPListingPluginDataInterface* dataIface,
                                            CFTPQueueItemType* type, BOOL* ok, BOOL isTopLevelDir,
                                            int hiddenFileDel, int hiddenDirDel,
                                            CFTPQueueItemState* state, DWORD* problemID,
                                            int* skippedItems, int* uiNeededItems)
{
    CFTPQueueItem* item = NULL;
    *type = fqitNone;
    BOOL isFile = TRUE;
    if (rightsCol != -1 && IsUNIXLink(dataIface->GetStringFromColumn(*f, rightsCol)))
    { // link
        *type = fqitDeleteLink;
        item = new CFTPQueueItemDel;
        if (item != NULL && !((CFTPQueueItemDel*)item)->SetItemDel(f->Hidden))
            *ok = FALSE;
    }
    else
    {
        if (isDir) // directory
        {
            isFile = FALSE;
            *type = fqitDeleteExploreDir;
            item = new CFTPQueueItemDelExplore;
            if (item != NULL && !((CFTPQueueItemDelExplore*)item)->SetItemDelExplore(isTopLevelDir, f->Hidden))
                *ok = FALSE;
        }
        else // file
        {
            *type = fqitDeleteFile;
            item = new CFTPQueueItemDel;
            if (item != NULL && !((CFTPQueueItemDel*)item)->SetItemDel(f->Hidden))
                *ok = FALSE;
        }
    }
    if (f->Hidden)
    {
        if (isFile)
        {
            switch (hiddenFileDel)
            {
            case HIDDENFILEDEL_USERPROMPT:
                *state = sqisUserInputNeeded;
                *problemID = ITEMPR_FILEISHIDDEN;
                (*uiNeededItems)++;
                break;
            case HIDDENFILEDEL_DELETEIT:
                break;
            case HIDDENFILEDEL_SKIP:
                *state = sqisSkipped;
                *problemID = ITEMPR_FILEISHIDDEN;
                (*skippedItems)++;
                break;
            }
        }
        else
        {
            switch (hiddenDirDel)
            {
            case HIDDENDIRDEL_USERPROMPT:
                *state = sqisUserInputNeeded;
                *problemID = ITEMPR_DIRISHIDDEN;
                (*uiNeededItems)++;
                break;
            case HIDDENDIRDEL_DELETEIT:
                break;
            case HIDDENDIRDEL_SKIP:
                *state = sqisSkipped;
                *problemID = ITEMPR_DIRISHIDDEN;
                (*skippedItems)++;
                break;
            }
        }
    }
    return item;
}

BOOL CPluginFSInterface::Delete(const wchar_t* fsName, int mode, HWND parent, int panel,
                                int selectedFiles, int selectedDirs, BOOL& cancelOrError) try
{
    CALL_STACK_MESSAGE5("CPluginFSInterface::Delete(, %d, , %d, %d, %d, )",
                        mode, panel, selectedFiles, selectedDirs);

    if (ControlConnection == NULL)
    {
        TRACE_E("Unexpected situation in CPluginFSInterface::Delete(): ControlConnection == NULL!");
        cancelOrError = TRUE; // cancel the operation
        return TRUE;
    }

    // check whether the panel shows only a simple listing -> in that case we cannot do anything yet
    CFTPListingPluginDataInterface* dataIface = (CFTPListingPluginDataInterface*)SalamanderGeneral->GetPanelPluginData(panel);
    if (dataIface != NULL && (void*)dataIface == (void*)&SimpleListPluginDataInterface)
    {
        SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_NEEDPARSEDLISTING).c_str(),
                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(), MB_OK | MB_ICONINFORMATION);
        cancelOrError = TRUE; // cancel the operation
        return TRUE;
    }

    // prepare the text describing what we are working with ("file "test.txt"", etc.)
    std::wstring subjectSrcW;
    SPLGetCommonFSOperSourceDescrOwned(SalamanderGeneral, panel, selectedFiles,
                                       selectedDirs, NULL, FALSE, FALSE,
                                       subjectSrcW);
    std::wstring dlgSubjectSrcW;
    SPLGetCommonFSOperSourceDescrOwned(SalamanderGeneral, panel, selectedFiles,
                                       selectedDirs, NULL, FALSE, TRUE,
                                       dlgSubjectSrcW);
    cancelOrError = FALSE;
    if (mode == 1)
    {
        BOOL CnfrmFileDirDel;
        SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMFILEDIRDEL, &CnfrmFileDirDel, 4, NULL);

        if (CnfrmFileDirDel)
        {
            // build the delete prompt
            const std::wstring subject = SPLFormatStringOwned(
                SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_DELETEFROMFTP).c_str(),
                subjectSrcW.c_str());

            // open a message box asking about the delete
            HINSTANCE Shell32DLL;
            Shell32DLL = HANDLES(LoadLibraryExW(L"shell32.dll", NULL, LOAD_LIBRARY_AS_DATAFILE));
            HICON hIcon = NULL;
            if (Shell32DLL != NULL)
            {
                hIcon = (HICON)HANDLES(LoadImage(Shell32DLL, MAKEINTRESOURCE(WindowsVistaAndLater ? 16777 : 161), // delete icon
                                                 IMAGE_ICON, 32, 32, SalamanderGeneral->GetIconLRFlags()));
                HANDLES(FreeLibrary(Shell32DLL));
            }
            INT_PTR res = CConfirmDeleteDlg(parent, subject.c_str(), hIcon).Execute();
            UpdateWindow(SalamanderGeneral->GetMainWindowHWND()); // so the user does not have to watch the rest of the dialog for the whole operation
            if (hIcon != NULL)
                HANDLES(DestroyIcon(hIcon));

            if (res != IDOK)
            {
                cancelOrError = TRUE; // cancel the operation
                return TRUE;
            }
        }
    }

    // ignore the "Confirm on" values from the configuration (they are overridden by the plugin settings for handling Delete situations)
    // BOOL ConfirmOnNotEmptyDirDelete, ConfirmOnSystemHiddenFileDelete, ConfirmOnSystemHiddenDirDelete;
    // SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMNEDIRDEL, &ConfirmOnNotEmptyDirDelete, 4, NULL);
    // SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMSHFILEDEL, &ConfirmOnSystemHiddenFileDelete, 4, NULL);
    // SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMSHDIRDEL, &ConfirmOnSystemHiddenDirDelete, 4, NULL);

    cancelOrError = TRUE; // pre-set the cancel/error state of the operation
    // create the operation object
    CFTPOperation* oper = new CFTPOperation;
    if (oper != NULL)
    {
        oper->SetEncryptControlConnection(ControlConnection->GetEncryptControlConnection());
        oper->SetEncryptDataConnection(ControlConnection->GetEncryptDataConnection());
        CCertificate* cert = ControlConnection->GetCertificate();
        oper->SetCertificate(cert);
        if (cert)
            cert->Release();
        oper->SetCompressData(ControlConnection->GetCompressData());
        if (ControlConnection->InitOperation(oper)) // initialize the server connection according to the "control connection"
        {
            if (!oper->SetBasicData(dlgSubjectSrcW.c_str(), (AutodetectSrvType ? NULL : LastServerType.c_str())))
            {
                delete oper;
                return TRUE;
            }
            std::wstring pathText;
            if (!BuildFullPathText(fsName, Path.c_str(), pathText))
            {
                delete oper;
                return TRUE;
            }
            CFTPServerPathType pathType = ControlConnection->GetFTPServerPathType(Path.c_str());
            if (!oper->SetOperationDelete(Path.c_str(), pathText.c_str(), FTPGetPathDelimiter(pathType), TRUE, selectedDirs > 0,
                                          Config.OperationsNonemptyDirDel, Config.OperationsHiddenFileDel,
                                          Config.OperationsHiddenDirDel))
            {
                delete oper;
                return TRUE;
            }
            int operUID;
            if (FTPOperationsList.AddOperation(oper, &operUID))
            {
                BOOL ok = TRUE;

                // build the queue of operation items
                CFTPQueue* queue = new CFTPQueue(ControlConnection->GetTextCodec());
                if (queue != NULL)
                {
                    if (dataIface != NULL && (void*)dataIface == (void*)&SimpleListPluginDataInterface)
                        dataIface = NULL; // we care only about a data interface of type CFTPListingPluginDataInterface
                    int rightsCol = -1;   // column index with permissions (used to detect links)
                    if (dataIface != NULL)
                        rightsCol = dataIface->FindRightsColumn();
                    const CFileData* f = NULL; // pointer to the file/directory/link in the panel to process
                    BOOL isDir = FALSE;        // TRUE if 'f' is a directory
                    BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
                    int skippedItems = 0;  // number of skipped items inserted into the queue
                    int uiNeededItems = 0; // number of user-input-needed items inserted into the queue
                    int index = 0;
                    while (1)
                    {
                        // fetch the data about the processed file
                        if (focused)
                            f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
                        else
                            f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);

                        // process the file/directory/link
                        if (f != NULL)
                        {
                            CFTPQueueItemType type;
                            CFTPQueueItemState state = sqisWaiting;
                            DWORD problemID = ITEMPR_OK;
                            CFTPQueueItem* item = CreateItemForDeleteOperation(f, isDir, rightsCol, dataIface, &type, &ok, TRUE,
                                                                               Config.OperationsHiddenFileDel,
                                                                               Config.OperationsHiddenDirDel,
                                                                               &state, &problemID, &skippedItems, &uiNeededItems);
                            if (item != NULL)
                            {
                                std::string itemNameBytes;
                                if (ok && (dataIface == NULL ||
                                           !dataIface->GetWireName(*f, ControlConnection->GetTextCodec(), itemNameBytes)))
                                    ok = FALSE;
                                if (ok)
                                    item->SetItem(-1, type, state, problemID, Path.c_str(), itemNameBytes.c_str());
                                if (!ok || !queue->AddItem(item)) // add the operation to the queue
                                {
                                    ok = FALSE;
                                    delete item;
                                }
                            }
                            else
                            {
                                TRACE_E(LOW_MEMORY);
                                ok = FALSE;
                            }
                        }
                        // determine whether it makes sense to continue (if there is no error and another selected item still exists)
                        if (!ok || focused || f == NULL)
                            break;
                    }
                    if (ok)
                        oper->SetChildItems(queue->GetCount(), skippedItems, 0, uiNeededItems);
                    else
                    {
                        delete queue;
                        queue = NULL;
                    }
                }
                else
                {
                    TRACE_E(LOW_MEMORY);
                    ok = FALSE;
                }

                if (ok) // the queue with the operation items is filled
                {
                    oper->SetQueue(queue); // assign the queue of its items to the operation
                    queue = NULL;
                    if (Config.DeleteAddToQueue)
                        cancelOrError = FALSE; // run the operation later -> for now the operation succeeds
                    else                       // run the operation within the active "control connection"
                    {
                        // open the operation progress window and start the operation
                        if (RunOperation(SalamanderGeneral->GetMsgBoxParent(), operUID, oper, NULL))
                            cancelOrError = FALSE; // operation succeeded
                        else
                            ok = FALSE;
                    }
                }
                if (!ok)
                    FTPOperationsList.DeleteOperation(operUID, TRUE);
                oper = NULL; // the operation is already added to the array, do not release it via 'delete' (see below)
            }
        }
        if (oper != NULL)
            delete oper;
    }
    else
        TRACE_E(LOW_MEMORY);
    return TRUE;
}
catch (...)
{
    cancelOrError = TRUE;
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return TRUE;
}

CFTPQueueItem* CreateItemForCopyOrMoveOperation(const CFileData* f, BOOL isDir, int rightsCol,
                                                CFTPListingPluginDataInterface* dataIface,
                                                CFTPQueueItemType* type, int transferMode,
                                                CFTPOperation* oper, BOOL copy, const wchar_t* localTargetPath,
                                                const wchar_t* localTargetName, CQuadWord* size,
                                                BOOL* sizeInBytes, CQuadWord* totalSize)
{
    CFTPQueueItem* item = NULL;
    *type = fqitNone;

    const wchar_t *name, *ext;      // helper variables for auto-detect transfer mode
    BOOL asciiTransferMode = FALSE; // helper variable for auto-detect transfer mode
    std::wstring basicNameStorage;
    BOOL isLink = rightsCol != -1 && IsUNIXLink(dataIface->GetStringFromColumn(*f, rightsCol));
    if (isLink || !isDir) // when 'asciiTransferMode' is used, calculate it
    {
        if (transferMode == trmAutodetect)
        {
            if (dataIface == NULL || !dataIface->GetBasicName(*f, &name, &ext, basicNameStorage))
            {
                name = f->Name;
                ext = f->Ext;
            }
            asciiTransferMode = oper->IsASCIIFile(name, ext);
        }
        else
            asciiTransferMode = transferMode == trmASCII;
    }
    if (isLink)
    { // link
        *type = copy ? fqitCopyResolveLink : fqitMoveResolveLink;

        BOOL dateAndTimeValid = FALSE;
        CFTPDate date;
        CFTPTime time;
        if (dataIface != NULL)
            dataIface->GetLastWriteDateAndTime(*f, &dateAndTimeValid, &date, &time);
        if (!dateAndTimeValid)
        {
            memset(&date, 0, sizeof(date));
            memset(&time, 0, sizeof(time));
        }

        item = new CFTPQueueItemCopyOrMove;
        if (item != NULL)
        {
            ((CFTPQueueItemCopyOrMove*)item)->SetItemCopyOrMove(localTargetPath, localTargetName, CQuadWord(-1, -1) /* unknown size */, asciiTransferMode, TRUE, TGTFILESTATE_UNKNOWN, dateAndTimeValid, date, time);
        }
    }
    else
    {
        if (isDir) // directory
        {
            *type = copy ? fqitCopyExploreDir : fqitMoveExploreDir;
            item = new CFTPQueueItemCopyMoveExplore;
            if (item != NULL)
            {
                ((CFTPQueueItemCopyMoveExplore*)item)->SetItemCopyMoveExplore(localTargetPath, localTargetName, TGTDIRSTATE_UNKNOWN);
            }
        }
        else // file
        {
            *type = copy ? fqitCopyFileOrFileLink : fqitMoveFileOrFileLink;
            item = new CFTPQueueItemCopyOrMove;
            if (dataIface != NULL && dataIface->GetSize(*f, *size, *sizeInBytes))
                *totalSize += *size;
            else
                size->Set(-1, -1); // unknown file size

            BOOL dateAndTimeValid = FALSE;
            CFTPDate date;
            CFTPTime time;
            if (dataIface != NULL)
                dataIface->GetLastWriteDateAndTime(*f, &dateAndTimeValid, &date, &time);
            if (!dateAndTimeValid)
            {
                memset(&date, 0, sizeof(date));
                memset(&time, 0, sizeof(time));
            }

            if (item != NULL)
            {
                ((CFTPQueueItemCopyOrMove*)item)->SetItemCopyOrMove(localTargetPath, localTargetName, *size, asciiTransferMode, *sizeInBytes, TGTFILESTATE_UNKNOWN, dateAndTimeValid, date, time);
            }
        }
    }
    return item;
}

BOOL CPluginFSInterface::CopyOrMoveFromFS(BOOL copy, int mode, const wchar_t* fsName, HWND parent,
                                          int panel, int selectedFiles, int selectedDirs,
                                          CSalamanderStringBuffer* targetPath, BOOL& operationMask,
                                          BOOL& cancelOrHandlePath, HWND dropTarget) try
{
    std::wstring payload;
    if (targetPath == NULL ||
        !sally::plugin_abi::ReadStringBuffer(*targetPath, payload))
        return FALSE;
    const size_t separator = payload.find(L'\0');
    std::wstring path(payload.data(), separator);
    std::wstring mask;
    if (separator != std::wstring::npos)
        mask.assign(payload.data() + separator + 1, payload.size() - separator - 1);
    const BOOL result = CopyOrMoveFromFSOwned(
        copy, mode, fsName, parent, panel, selectedFiles, selectedDirs,
        path, mask, operationMask, cancelOrHandlePath, dropTarget);
    return sally::plugin_abi::WriteStringBuffer(*targetPath, path) ? result : FALSE;
}
catch (...)
{
    cancelOrHandlePath = TRUE;
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
}

BOOL CPluginFSInterface::CopyOrMoveFromFSOwned(
    BOOL copy, int mode, const wchar_t* fsName, HWND parent, int panel,
    int selectedFiles, int selectedDirs, std::wstring& targetPath,
    const std::wstring& suppliedMask, BOOL& operationMask,
    BOOL& cancelOrHandlePath, HWND dropTarget)
{
    CALL_STACK_MESSAGE6("CPluginFSInterface::CopyOrMoveFromFS(%d, %d, , , %d, %d, %d, , , ,)",
                        copy, mode, panel, selectedFiles, selectedDirs);

    if (ControlConnection == NULL)
    {
        TRACE_E("Unexpected situation in CPluginFSInterface::CopyOrMoveFromFS(): ControlConnection == NULL!");
        cancelOrHandlePath = TRUE;
        return TRUE; // cancel the operation
    }

    // check whether the panel shows only a simple listing -> in that case we cannot do anything yet
    CFTPListingPluginDataInterface* dataIface = (CFTPListingPluginDataInterface*)SalamanderGeneral->GetPanelPluginData(panel);
    if (dataIface != NULL && (void*)dataIface == (void*)&SimpleListPluginDataInterface)
    {
        SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_NEEDPARSEDLISTING).c_str(),
                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(), MB_OK | MB_ICONINFORMATION);
        cancelOrHandlePath = TRUE; // cancel the operation
        return TRUE;
    }

    // compose the edit line title with the copy/move destination
    std::wstring subjectSrcW;
    SPLGetCommonFSOperSourceDescrOwned(SalamanderGeneral, panel, selectedFiles,
                                       selectedDirs, NULL, FALSE, FALSE,
                                       subjectSrcW);
    std::wstring dlgSubjectSrcW;
    SPLGetCommonFSOperSourceDescrOwned(SalamanderGeneral, panel, selectedFiles,
                                       selectedDirs, NULL, FALSE, TRUE,
                                       dlgSubjectSrcW);
    std::wstring subject = SPLFormatStringOwned(
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, copy ? IDS_COPYFROMFTP : IDS_MOVEFROMFTP).c_str(),
        subjectSrcW.c_str());

    if (mode == 1 && !targetPath.empty()) // only when opening the dialog for the first time and the target path is selected
    {
        SPLSalPathAppendOwned(targetPath, L"*.*");
        SalamanderGeneral->SetUserWorkedOnPanelPath(PANEL_TARGET); // default action = work with the path in the target panel
    }
    if (mode != 3 && mode != 5)
    {
        wchar_t** history;
        int historyCount;
        SalamanderGeneral->GetStdHistoryValues(SALHIST_COPYMOVETGT, &history, &historyCount);
        CCopyMoveDlg dlg(parent, targetPath, LangStr(copy ? IDS_COPYTITLE : IDS_MOVETITLE).c_str(),
                         subject.c_str(), history, historyCount, copy ? IDD_COPYMOVEDLG : IDH_MOVEDLG);
        INT_PTR res = dlg.Execute();
        UpdateWindow(SalamanderGeneral->GetMainWindowHWND()); // so the user does not have to watch the rest of the dialog for the whole operation
        if (res == IDOK)
        {
            cancelOrHandlePath = TRUE;
            operationMask = TRUE;
            return FALSE; // let the path be parsed in the standard way
        }
        else
        {
            cancelOrHandlePath = TRUE;
            return TRUE; // cancel the operation
        }
    }
    else
    {
        const wchar_t* opMask = NULL; // operation mask
        if (mode == 5)             // the operation target was provided via drag&drop
        {
            // if it is a disk path, just set the operation mask and continue (same as with 'mode'==3);
            // if it is a path to an archive or another FS, report a "not supported" error

            BOOL ok = FALSE;
            opMask = L"*.*";
            int type;
            size_t secondPartOffset = std::wstring::npos;
            BOOL isDir;
            if (targetPath.size() >= 2 &&
                (targetPath[1] == L':' ||
                 targetPath[0] == L'\\' && targetPath[1] == L'\\'))
            {                                                   // add a trailing backslash so it is a path in every case ('mode'==5 always provides a path)
                SPLSalPathAddBackslashOwned(targetPath);
            }
            subject = SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str();
            std::wstring parsedTarget(targetPath);
            if (SPLSalParsePathOwned(SalamanderGeneral, parent, parsedTarget, type,
                                     isDir, secondPartOffset, subject.c_str(), FALSE,
                                     NULL, NULL))
            {
                targetPath = parsedTarget;
                const wchar_t* secondPart = targetPath.c_str() + secondPartOffset;
                switch (type)
                {
                case PATH_TYPE_WINDOWS:
                {
                    if (*secondPart != 0) // this should probably never happen
                    {
                        SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_DRAGDROP_TGTNOTEXIST).c_str(),
                                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
                    }
                    else
                        ok = TRUE;
                    break;
                }

                default: // archive or FS, just report "not supported"
                {
                    SalamanderGeneral->SalMessageBox(parent, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_DRAGDROP_TGTARCORFS).c_str(),
                                                     SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
                    break;
                }
                }
            }
            if (!ok)
            {
                cancelOrHandlePath = TRUE;
                return TRUE;
            }
        }

        // the path is parsed, start the operation

        // ignore the "Confirm on" values from the configuration (they are overridden by the handling settings for "file already exists" - see FILEALREADYEXISTS_XXX)
        // BOOL ConfirmOnFileOverwrite, ConfirmOnDirOverwrite, ConfirmOnSystemHiddenFileOverwrite;
        // SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMFILEOVER, &ConfirmOnFileOverwrite, 4, NULL);
        // SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMDIROVER, &ConfirmOnDirOverwrite, 4, NULL);
        // SalamanderGeneral->GetConfigParameter(SALCFG_CNFRMSHFILEOVER, &ConfirmOnSystemHiddenFileOverwrite, 4, NULL);

        // find the operation mask (the target path is in 'targetPath')
        if (opMask == NULL)
        {
            opMask = suppliedMask.c_str();
        }

        const std::wstring asciiFileMasksW = SPLGetMasksStringOwned(Config.ASCIIFileMasks);
        BOOL success = FALSE; // pre-set the cancel/error state of the operation
        // create the operation object
        CFTPOperation* oper = new CFTPOperation;
        if (oper != NULL)
        {
            oper->SetEncryptControlConnection(ControlConnection->GetEncryptControlConnection());
            oper->SetEncryptDataConnection(ControlConnection->GetEncryptDataConnection());
            CCertificate* cert = ControlConnection->GetCertificate();
            oper->SetCertificate(cert);
            if (cert)
                cert->Release();
            oper->SetCompressData(ControlConnection->GetCompressData());
            if (ControlConnection->InitOperation(oper)) // initialize the server connection according to the "control connection"
            {
                if (!oper->SetBasicData(dlgSubjectSrcW.c_str(), (AutodetectSrvType ? NULL : LastServerType.c_str())))
                {
                    delete oper;
                    return TRUE;
                }
                std::wstring sourcePathText;
                if (!BuildFullPathText(fsName, Path.c_str(), sourcePathText))
                {
                    delete oper;
                    return TRUE;
                }
                CFTPServerPathType pathType = ControlConnection->GetFTPServerPathType(Path.c_str());
                BOOL is_AS_400_QSYS_LIB_Path = pathType == ftpsptAS400 &&
                                               FTPIsPrefixOfServerPath(ftpsptAS400, "/QSYS.LIB", Path.c_str());
                if (oper->SetOperationCopyMoveDownload(copy, Path.c_str(), sourcePathText.c_str(), FTPGetPathDelimiter(pathType),
                                                       !copy, copy ? FALSE : (selectedDirs > 0),
                                                       targetPath.c_str(), '\\', TRUE, selectedDirs > 0, asciiFileMasksW.c_str(),
                                                       TransferMode == trmAutodetect, TransferMode == trmASCII,
                                                       Config.OperationsCannotCreateFile,
                                                       Config.OperationsCannotCreateDir,
                                                       Config.OperationsFileAlreadyExists,
                                                       Config.OperationsDirAlreadyExists,
                                                       Config.OperationsRetryOnCreatedFile,
                                                       Config.OperationsRetryOnResumedFile,
                                                       Config.OperationsAsciiTrModeButBinFile))
                {
                    int operUID;
                    if (FTPOperationsList.AddOperation(oper, &operUID))
                    {
                        BOOL ok = TRUE;

                        // build the queue of operation items
                        CFTPQueue* queue = new CFTPQueue(ControlConnection->GetTextCodec());
                        if (queue != NULL)
                        {
                            if (dataIface != NULL && (void*)dataIface == (void*)&SimpleListPluginDataInterface)
                                dataIface = NULL; // we care only about a data interface of type CFTPListingPluginDataInterface
                            int rightsCol = -1;   // column index with permissions (used to detect links)
                            if (dataIface != NULL)
                                rightsCol = dataIface->FindRightsColumn();
                            CQuadWord totalSize(0, 0); // total size (in bytes or blocks)
                            CQuadWord size(-1, -1);    // variable for the current file size
                            BOOL sizeInBytes = TRUE;   // TRUE/FALSE = sizes in bytes/blocks (a single listing cannot mix them - see CFTPListingPluginDataInterface::GetSize())
                            const CFileData* f = NULL; // pointer to the file/directory/link in the panel to process
                            BOOL isDir = FALSE;        // TRUE if 'f' is a directory
                            BOOL focused = (selectedFiles == 0 && selectedDirs == 0);
                            BOOL donotUseOpMask = wcscmp(opMask, L"*.*") == 0 || wcscmp(opMask, L"*") == 0;
                            int index = 0;
                            while (1)
                            {
                                // fetch the data about the processed file
                                if (focused)
                                    f = SalamanderGeneral->GetPanelFocusedItem(panel, &isDir);
                                else
                                    f = SalamanderGeneral->GetPanelSelectedItem(panel, &index, &isDir);

                                // process the file/directory/link
                                if (f != NULL)
                                {
                                    // create the target name according to the operation mask
                                    std::wstring targetNameW;
                                    if (!is_AS_400_QSYS_LIB_Path)
                                    {
                                        if (donotUseOpMask)
                                            targetNameW = f->Name; // masks trim '.' from name ends, which is not always OK (e.g. directories "a.b" and "a.b." would merge) - probably rare, so for now we solve it only provisionally like this
                                        else
                                            targetNameW = SPLMaskNameOwned(SalamanderGeneral, f->Name, opMask);
                                    }
                                    else
                                    {
                                        const std::wstring mbrNameW = FTPAS400CutFileNamePartW(f->Name);
                                        if (donotUseOpMask)
                                            targetNameW = mbrNameW; // masks trim '.' from name ends, which is not always OK (e.g. directories "a.b" and "a.b." would merge) - probably rare, so for now we solve it only provisionally like this
                                        else
                                            targetNameW = SPLMaskNameOwned(SalamanderGeneral, mbrNameW.c_str(), opMask);
                                    }

                                    std::string itemNameBytes;
                                    if (dataIface == NULL ||
                                        !dataIface->GetWireName(*f, ControlConnection->GetTextCodec(), itemNameBytes))
                                    {
                                        ok = FALSE;
                                        break;
                                    }
                                    CFTPQueueItemType type;
                                    CFTPQueueItem* item = CreateItemForCopyOrMoveOperation(f, isDir, rightsCol,
                                                                                           dataIface, &type,
                                                                                           TransferMode, oper,
                                                                                           copy, targetPath.c_str(),
                                                                                           targetNameW.c_str(), &size,
                                                                                           &sizeInBytes, &totalSize);
                                    if (item != NULL)
                                    {
                                        if (ok)
                                            item->SetItem(-1, type, sqisWaiting, ITEMPR_OK, Path.c_str(), itemNameBytes.c_str());
                                        if (!ok || !queue->AddItem(item)) // add the operation to the queue
                                        {
                                            ok = FALSE;
                                            delete item;
                                        }
                                    }
                                    else
                                    {
                                        TRACE_E(LOW_MEMORY);
                                        ok = FALSE;
                                    }
                                }
                                // determine whether it makes sense to continue (if there is no error and another selected item still exists)
                                if (!ok || focused || f == NULL)
                                    break;
                            }
                            if (ok)
                            {
                                oper->SetChildItems(queue->GetCount(), 0, 0, 0);
                                oper->AddToTotalSize(totalSize, sizeInBytes);
                            }
                            else
                            {
                                delete queue;
                                queue = NULL;
                            }
                        }
                        else
                        {
                            TRACE_E(LOW_MEMORY);
                            ok = FALSE;
                        }

                        if (ok) // the queue with the operation items is filled
                        {
                            oper->SetQueue(queue); // assign the queue of its items to the operation
                            queue = NULL;
                            if (Config.DownloadAddToQueue)
                                success = TRUE; // run the operation later -> for now the operation succeeds
                            else                // run the operation within the active "control connection"
                            {
                                // open the operation progress window and start the operation
                                if (RunOperation(SalamanderGeneral->GetMsgBoxParent(), operUID, oper, dropTarget))
                                    success = TRUE; // operation succeeded
                                else
                                    ok = FALSE;
                            }
                        }
                        if (!ok)
                            FTPOperationsList.DeleteOperation(operUID, TRUE);
                        oper = NULL; // the operation is already added to the array, do not release it via 'delete' (see below)
                    }
                }
            }
            if (oper != NULL)
                delete oper;
        }
        else
            TRACE_E(LOW_MEMORY);

        if (success)
        {
            cancelOrHandlePath = FALSE; // operation succeeded
            targetPath.clear();         // no name should receive focus (that would have to be a copy within the FTP server, which we do not support yet)
        }
        else
            cancelOrHandlePath = TRUE; // cancel the operation
        return TRUE;
    }
}

CFTPQueueItem* CreateItemForChangeAttrsOperation(const CFileData* f, BOOL isDir, int rightsCol,
                                                 CFTPListingPluginDataInterface* dataIface,
                                                 CFTPQueueItemType* type, BOOL* ok,
                                                 CFTPQueueItemState* state, DWORD* problemID,
                                                 int* skippedItems, int* uiNeededItems,
                                                 BOOL* skip, BOOL selFiles,
                                                 BOOL selDirs, BOOL includeSubdirs,
                                                 DWORD attrAndMask, DWORD attrOrMask,
                                                 int operationsUnknownAttrs)
{
    CFTPQueueItem* item = NULL;
    *type = fqitNone;
    *state = sqisWaiting;
    *problemID = ITEMPR_OK;
    *skip = FALSE; // TRUE if the file/directory/link should not be processed at all (unrelated to skippedItems)
    char* rights = NULL;
    if (rightsCol != -1 && IsUNIXLink((rights = dataIface->GetStringFromColumn(*f, rightsCol))))
    {                       // link
        if (includeSubdirs) // try whether it is a link to a directory (otherwise there is nothing to do)
        {
            *type = fqitChAttrsResolveLink;
            item = new CFTPQueueItem;
        }
        else
            *skip = TRUE; // attributes of a link cannot be changed, so there is nothing to do
    }
    else
    {
        // calculate new permissions for the file/directory
        DWORD actAttr;
        DWORD attrDiff = 0;
        BOOL attrErr = FALSE;
        if (rightsCol != -1 && GetAttrsFromUNIXRights(&actAttr, &attrDiff, rights))
        {
            DWORD changeMask = (~attrAndMask | attrOrMask) & 0777;
            if ((!includeSubdirs || !isDir) &&                                                   // cannot optimize "explore dir" this way
                (attrDiff & changeMask) == 0 &&                                                  // no unknown attribute should be changed
                (actAttr & changeMask) == (((actAttr & attrAndMask) | attrOrMask) & changeMask)) // no known attribute should be changed
            {                                                                                    // nothing to do (no attribute change)
                *skip = TRUE;
            }
            else
            {
                if (((attrDiff & attrAndMask) & attrOrMask) != (attrDiff & attrAndMask))
                {                        // problem: an unknown attribute should be preserved, which we cannot do
                    actAttr |= attrDiff; // set at least 'x' when we cannot handle 's' or 't' or whatever it currently is (see UNIX permissions)
                    attrErr = TRUE;
                }
                actAttr = (actAttr & attrAndMask) | attrOrMask;
            }
        }
        else // unknown permissions
        {
            actAttr = attrOrMask; // assume no permissions (actAttr==0)
            if (((~attrAndMask | attrOrMask) & 0777) != 0777)
            { // problem: permissions are unknown and some attribute should be preserved (we do not know its value -> cannot keep it)
                attrErr = TRUE;
            }
        }

        if (!*skip)
        {
            if (isDir) // directory
            {
                if (includeSubdirs) // apply the attribute change to the directory contents as well
                {                   // any permissions error will be reported by the inserted fqitChAttrsDir item
                    *type = fqitChAttrsExploreDir;
                    item = new CFTPQueueItemChAttrExplore;
                    if (item != NULL)
                        ((CFTPQueueItemChAttrExplore*)item)->SetItemChAttrExplore(rights);
                }
                else
                {
                    if (selDirs)
                    { // without subdirectories the task reduces to setting attributes of the selected directory
                        if (attrErr)
                        {
                            switch (operationsUnknownAttrs)
                            {
                            case UNKNOWNATTRS_IGNORE:
                                attrErr = FALSE;
                                break;

                            case UNKNOWNATTRS_SKIP:
                            {
                                *state = sqisSkipped;
                                *problemID = ITEMPR_UNKNOWNATTRS;
                                (*skippedItems)++;
                                break;
                            }

                            default: // UNKNOWNATTRS_USERPROMPT
                            {
                                *state = sqisUserInputNeeded;
                                *problemID = ITEMPR_UNKNOWNATTRS;
                                (*uiNeededItems)++;
                                break;
                            }
                            }
                        }
                        if (!attrErr)
                            rights = NULL; // if everything is OK there is no reason to remember the original permissions
                        *type = fqitChAttrsDir;
                        item = new CFTPQueueItemChAttrDir;
                        if (item != NULL)
                        {
                            if (!((CFTPQueueItemChAttrDir*)item)->SetItemDir(0, 0, 0, 0))
                                *ok = FALSE;
                            else
                                ((CFTPQueueItemChAttrDir*)item)->SetItemChAttrDir((WORD)actAttr, rights, attrErr);
                        }
                    }
                    else
                        *skip = TRUE; // we are not supposed to touch directories
                }
            }
            else // file
            {
                if (selFiles)
                {
                    if (attrErr)
                    {
                        switch (operationsUnknownAttrs)
                        {
                        case UNKNOWNATTRS_IGNORE:
                            attrErr = FALSE;
                            break;

                        case UNKNOWNATTRS_SKIP:
                        {
                            *state = sqisSkipped;
                            *problemID = ITEMPR_UNKNOWNATTRS;
                            (*skippedItems)++;
                            break;
                        }

                        default: // UNKNOWNATTRS_USERPROMPT
                        {
                            *state = sqisUserInputNeeded;
                            *problemID = ITEMPR_UNKNOWNATTRS;
                            (*uiNeededItems)++;
                            break;
                        }
                        }
                    }
                    if (!attrErr)
                        rights = NULL; // if everything is OK there is no reason to remember the original permissions
                    *type = fqitChAttrsFile;
                    item = new CFTPQueueItemChAttr;
                    if (item != NULL)
                        ((CFTPQueueItemChAttr*)item)->SetItemChAttr((WORD)actAttr, rights, attrErr);
                }
                else
                    *skip = TRUE; // we are not supposed to touch files
            }
        }
    }
    return item;
}
