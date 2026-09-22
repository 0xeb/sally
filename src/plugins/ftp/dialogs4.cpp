// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

static BOOL FormatServerTypeDateTime(const SYSTEMTIME& value, BOOL date, std::wstring& output) noexcept
{
    try
    {
        const int needed = date ? GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &value, NULL, NULL, 0)
                                : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &value, NULL, NULL, 0);
        if (needed > 0)
        {
            std::wstring staged(needed, L'\0');
            const int written = date ? GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &value, NULL, staged.data(), needed)
                                     : GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &value, NULL, staged.data(), needed);
            if (written == needed)
            {
                staged.resize(needed - 1);
                output.swap(staged);
                return TRUE;
            }
        }

        if (date)
            output = std::to_wstring(value.wDay) + L"." + std::to_wstring(value.wMonth) + L"." + std::to_wstring(value.wYear);
        else
            output = std::to_wstring(value.wHour) + L":" + (value.wMinute < 10 ? L"0" : L"") + std::to_wstring(value.wMinute) +
                     L":" + (value.wSecond < 10 ? L"0" : L"") + std::to_wstring(value.wSecond);
        return TRUE;
    }
    catch (...)
    {
        output.clear();
        return FALSE;
    }
}

//
// ****************************************************************************
// CEditSrvTypeColumnDlg
//

CEditSrvTypeColumnDlg::CEditSrvTypeColumnDlg(HWND parent,
                                             TIndirectArray<CSrvTypeColumn>* columnsData,
                                             int* editedColumn, BOOL edit)
    : CCenteredDialog(HLanguage, IDD_EDITSRVTYCOLUMN, IDD_EDITSRVTYCOLUMN, parent)
{
    ColumnsData = columnsData;
    EditedColumn = editedColumn;
    Edit = edit;
    LastUsedIndexForName = -1;
    LastUsedIndexForDescr = -1;
    FirstSelNotifyAfterTransfer = FALSE;
}

BOOL IsValidIdentifier(const char* s, int* errResID)
{
    if (s == NULL || *s == 0 || _stricmp(s, "is_dir") == 0 || _stricmp(s, "is_hidden") == 0 ||
        _stricmp(s, "is_link") == 0)
    {
        if (errResID != NULL)
            *errResID = (s == NULL || *s == 0) ? IDS_STC_ERR_IDEMPTY : IDS_STC_ERR_IDRESERVED;
        return FALSE;
    }
    if (*s >= 'a' && *s <= 'z' || *s >= 'A' && *s <= 'Z' || *s == '_')
    {
        s++;
        while (*s != 0 && (*s >= 'a' && *s <= 'z' || *s >= 'A' && *s <= 'Z' ||
                           *s >= '0' && *s <= '9' || *s == '_'))
            s++;
    }
    if (*s != 0)
    {
        if (errResID != NULL)
            *errResID = IDS_STC_ERR_IDINVALID;
        return FALSE;
    }
    return TRUE;
}

void CEditSrvTypeColumnDlg::Validate(CTransferInfo& ti)
{
    // check syntax, avoid reserved IDs (is_dir+is_hidden+is_link), ensure uniqueness and non-empty ID
    std::string id;
    BOOL ok = TRUE;
    ti.EditLine(IDE_COL_ID, id);
    int errResID = 0;
    if (!ti.IsGood())
    {
        ti.ErrorOn(IDE_COL_ID);
        return;
    }
    if (IsValidIdentifier(id.c_str(), &errResID))
    {
        int j;
        for (j = 0; j < ColumnsData->Count; j++)
        {
            if ((!Edit || *EditedColumn != j) &&
                _stricmp(id.c_str(), HandleNULLStr(ColumnsData->At(j)->ID)) == 0)
            {
                ok = FALSE;
                errResID = IDS_STC_ERR_IDNOTUNIQUE;
                break;
            }
        }
    }
    else
        ok = FALSE;
    if (!ok)
    {
        SalamanderGeneral->SalMessageBox(HWindow, SPLLoadStrOwned(SalamanderGeneral, HLanguage, errResID).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                         MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(IDE_COL_ID);
        return;
    }

    // check syntax of "empty value"
    CSrvTypeColumnTypes type = stctNone;
    HWND combo = GetDlgItem(HWindow, IDC_COL_TYPE);
    int i = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
    if (i != CB_ERR)
    {
        // x64 - ITEMDATA does not hold a pointer, casting to (int) is safe
        int t = (int)SendMessage(combo, CB_GETITEMDATA, i, 0); // obtain the selected column type
        if (t > stctNone && t < stctLastItem)
            type = (CSrvTypeColumnTypes)t;
    }
    std::string emptyVal;
    ti.EditLine(IDE_COL_EMPTY, emptyVal);
    if (!ti.IsGood() || !GetColumnEmptyValue(emptyVal.empty() ? NULL : emptyVal.c_str(), type, NULL, NULL, NULL, FALSE))
    {
        SalamanderGeneral->SalMessageBox(HWindow, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_STC_ERR_INVALEMPTY).c_str(),
                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(IDE_COL_EMPTY);
        return;
    }

    // check that the column name and description are not empty
    BOOL errOnName = LastUsedIndexForName == -1 && GetWindowTextLength(GetDlgItem(HWindow, IDC_COL_NAME)) == 0;
    BOOL errOnDescr = LastUsedIndexForDescr == -1 && GetWindowTextLength(GetDlgItem(HWindow, IDC_COL_DESCR)) == 0;
    if (errOnName || errOnDescr)
    {
        SalamanderGeneral->SalMessageBox(HWindow, SPLLoadStrOwned(SalamanderGeneral, HLanguage, errOnName ? IDS_STC_ERR_EMPTYNAME : IDS_STC_ERR_EMPTYDESCR).c_str(),
                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(), MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(errOnName ? IDC_COL_NAME : IDC_COL_DESCR);
        return;
    }
}

void CEditSrvTypeColumnDlg::Transfer(CTransferInfo& ti)
{
    std::string id;
    std::string emptyVal;
    if (ti.Type == ttDataToWindow)
    {
        if (Edit)
        {
            id = HandleNULLStr(ColumnsData->At(*EditedColumn)->ID);
            emptyVal = HandleNULLStr(ColumnsData->At(*EditedColumn)->EmptyValue);
        }
        ti.EditLine(IDE_COL_ID, id);
        ti.EditLine(IDE_COL_EMPTY, emptyVal);
    }
    else
    {
        ti.EditLine(IDE_COL_ID, id);
        ti.EditLine(IDE_COL_EMPTY, emptyVal);
    }

    std::string bufName;
    std::string bufDescr;
    HWND comboName, comboDescr;
    if (ti.GetControl(comboName, IDC_COL_NAME) && ti.GetControl(comboDescr, IDC_COL_DESCR))
    {
        if (ti.Type == ttDataToWindow)
        {
            // append standard strings first
            SendMessage(comboName, CB_RESETCONTENT, 0, 0);
            SendMessage(comboDescr, CB_RESETCONTENT, 0, 0);
            int i;
            for (i = 0; i < STC_STD_NAMES_COUNT; i++)
            {
                std::wstring nameText;
                std::wstring descrText;
                LoadStdColumnStrName(i, nameText);
                LoadStdColumnStrDescr(i, descrText);
                SendMessageW(comboName, CB_ADDSTRING, 0, (LPARAM)nameText.c_str());
                SendMessageW(comboDescr, CB_ADDSTRING, 0, (LPARAM)descrText.c_str());
            }
            // if editing, insert the current texts into the edit line
            if (Edit)
            {
                CSrvTypeColumn* col = ColumnsData->At(*EditedColumn);
                std::wstring nameText;
                std::wstring descrText;
                if (col->NameID != -1)
                {
                    LoadStdColumnStrName(col->NameID, nameText);
                    LastUsedIndexForName = col->NameID;
                }
                else
                    FtpDecodeLocalText(HandleNULLStr(col->NameStr), nameText);
                if (col->DescrID != -1)
                {
                    LoadStdColumnStrDescr(col->DescrID, descrText);
                    LastUsedIndexForDescr = col->DescrID;
                }
                else
                    FtpDecodeLocalText(HandleNULLStr(col->DescrStr), descrText);
                SetWindowTextW(comboName, nameText.c_str());
                SetWindowTextW(comboDescr, descrText.c_str());
            }
        }
        else
        {
            // if the user has custom text, pull it out, otherwise use indexes from the last selection
            // in the combo (finding the index by looking up the string in the list is impossible, duplicate strings may occur)
            if (LastUsedIndexForName == -1)
            {
                if (!ReadWindowLocalText(comboName, bufName))
                    ti.ErrorOn(IDC_COL_NAME);
            }
            if (LastUsedIndexForDescr == -1)
            {
                if (!ReadWindowLocalText(comboDescr, bufDescr))
                    ti.ErrorOn(IDC_COL_DESCR);
            }
        }
    }

    HWND combo;
    CSrvTypeColumnTypes colType = stctNone;
    if (ti.GetControl(combo, IDC_COL_TYPE))
    {
        if (ti.Type == ttDataToWindow)
        {
            std::wstring typeName;
            SendMessage(combo, CB_RESETCONTENT, 0, 0);
            int count = 0;
            int focus = 0;
            int i;
            for (i = stctName; i < stctLastItem; i++)
            {
                BOOL add = TRUE;
                if (i == stctExt && Edit && *EditedColumn != 1)
                    add = FALSE; // the extension can only be the second column
                if (add && i < stctFirstGeneral)
                {
                    int j;
                    for (j = 0; j < ColumnsData->Count; j++)
                    {
                        if ((!Edit || *EditedColumn != j) && // when editing we naturally include our current type
                            i == (int)ColumnsData->At(j)->Type)
                        {
                            add = FALSE;
                            break;
                        }
                    }
                }
                if (add && GetColumnTypeName((CSrvTypeColumnTypes)i, typeName))
                {
                    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)typeName.c_str()); // add the string and
                    SendMessage(combo, CB_SETITEMDATA, count++, i);   // associate which column type it is
                    if (Edit && (int)(ColumnsData->At(*EditedColumn)->Type) == i)
                    {
                        focus = count - 1; // when editing we focus our type
                        if (i == stctName)
                            break; // the type of the first column cannot be changed
                    }
                }
            }
            SendMessage(combo, CB_SETCURSEL, focus, 0);
            PostMessage(HWindow, WM_COMMAND, MAKELONG(IDC_COL_TYPE, CBN_SELCHANGE), 0);
            FirstSelNotifyAfterTransfer = TRUE;
        }
        else
        {
            int i = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
            if (i != CB_ERR)
            {
                // x64 - ITEMDATA does not hold a pointer, casting to (int) is safe
                int type = (int)SendMessage(combo, CB_GETITEMDATA, i, 0); // obtain the selected column type
                if (type > stctNone && type < stctLastItem)
                    colType = (CSrvTypeColumnTypes)type;
                else
                    TRACE_E("Unexpected situation in CEditSrvTypeColumnDlg::Transfer(): unknown type of column!");
            }
        }
    }

    BOOL leftAlignment = TRUE;
    if (ti.GetControl(combo, IDC_COL_ALIGNMENT))
    {
        if (ti.Type == ttDataToWindow)
        {
            SendMessage(combo, CB_RESETCONTENT, 0, 0);
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_SRVTYPECOL_ALIGNLEFT).c_str());  // add the string "left"
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)LangStr(IDS_SRVTYPECOL_ALIGNRIGHT).c_str()); // add the string "right"
            SendMessage(combo, CB_SETCURSEL, ColumnsData->At(*EditedColumn)->Type >= stctFirstGeneral ? (ColumnsData->At(*EditedColumn)->LeftAlignment ? 0 : 1) : -1, 0);
        }
        else
        {
            int i = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
            if (i != CB_ERR)
                leftAlignment = i == 0;
        }
    }

    // process the acquired data (id, emptyVal, LastUsedIndexForName, bufName, LastUsedIndexForDescr,
    // bufDescr, colType, leftAlignment)
    if (!ti.IsGood())
        return;
    if (ti.Type == ttDataFromWindow)
    {
        // for types Name, Ext, and Type empty values make no sense, clear them
        if (colType == stctName || colType == stctExt || colType == stctType)
            emptyVal.clear();

        if (Edit) // editing a column
        {
            CSrvTypeColumn* col = ColumnsData->At(*EditedColumn);
            UpdateStr(col->ID, id.c_str()); // in case of an error we keep the original ID
            if (LastUsedIndexForName == -1)
            {
                BOOL err = FALSE;
                UpdateStr(col->NameStr, bufName.c_str(), &err);
                if (!err)
                    col->NameID = -1; // in case of an error we leave the original column name
            }
            else
            {
                if (col->NameStr != NULL)
                {
                    free(col->NameStr);
                    col->NameStr = NULL;
                }
                col->NameID = LastUsedIndexForName;
            }
            if (LastUsedIndexForDescr == -1)
            {
                BOOL err = FALSE;
                UpdateStr(col->DescrStr, bufDescr.c_str(), &err);
                if (!err)
                    col->DescrID = -1; // in case of an error we leave the original column description
            }
            else
            {
                if (col->DescrStr != NULL)
                {
                    free(col->DescrStr);
                    col->DescrStr = NULL;
                }
                col->DescrID = LastUsedIndexForDescr;
            }
            // editing stctExt must result in Visible = TRUE
            if (colType == stctExt)
                col->Visible = TRUE;
            col->Type = colType;
            col->LeftAlignment = leftAlignment;
            BOOL err = FALSE;
            UpdateStr(col->EmptyValue, emptyVal.empty() ? NULL : emptyVal.c_str(), &err);
            if (err && col->EmptyValue != NULL)
            {
                free(col->EmptyValue);
                col->EmptyValue = NULL;
            }
        }
        else // new column
        {
            CSrvTypeColumn* col = new CSrvTypeColumn;
            if (col != NULL && col->IsGood())
            {
                col->Set(TRUE, id.c_str(), LastUsedIndexForName, bufName.empty() ? NULL : bufName.c_str(),
                         LastUsedIndexForDescr, bufDescr.empty() ? NULL : bufDescr.c_str(), colType,
                         emptyVal.empty() ? NULL : emptyVal.c_str(), leftAlignment, 0, 0);
                // adding type stctExt must insert it after "Name" (that is, at index 1 in the array)
                if (colType == stctExt)
                {
                    ColumnsData->Insert(1, col);
                    *EditedColumn = 1; // focus the inserted column
                }
                else
                {
                    ColumnsData->Add(col);
                    *EditedColumn = ColumnsData->Count - 1; // focus the added column
                }
                if (ColumnsData->IsGood())
                    col = NULL;
                else
                    ColumnsData->ResetState();
            }
            if (col != NULL)
                delete col;
        }
    }
}

INT_PTR
CEditSrvTypeColumnDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CEditSrvTypeColumnDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        if (!Edit)
            SetWindowTextW(HWindow, LangStr(IDS_SRVTYPECOL_NEWTITLE).c_str());
        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDC_COL_NAME:
        case IDC_COL_DESCR:
        {
            if (HIWORD(wParam) == CBN_SELCHANGE) // save the last selected index from the combo box
            {
                HWND combo = GetDlgItem(HWindow, LOWORD(wParam));
                int i = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
                if (i != CB_ERR)
                {
                    if (LOWORD(wParam) == IDC_COL_NAME)
                        LastUsedIndexForName = i;
                    else
                        LastUsedIndexForDescr = i;
                }
            }
            if (HIWORD(wParam) == CBN_EDITCHANGE) // invalidate the last selected index from the combo box
            {
                if (LOWORD(wParam) == IDC_COL_NAME)
                    LastUsedIndexForName = -1;
                else
                    LastUsedIndexForDescr = -1;
            }
            break;
        }

        case IDC_COL_TYPE:
        {
            if (HIWORD(wParam) == CBN_SELCHANGE) // set the help text for the "Empty Value" format and enable and set the Alignment combo box
            {
                HWND combo = GetDlgItem(HWindow, IDC_COL_TYPE);
                int i = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
                if (i != CB_ERR)
                {
                    // x64 - ITEMDATA does not hold a pointer, casting to (int) is safe
                    int type = (int)SendMessage(combo, CB_GETITEMDATA, i, 0); // obtain the selected column type
                    if (type > stctNone && type < stctLastItem)
                    {
                        BOOL leftAlignment = TRUE;
                        int resID = IDS_SRVTYPECOL_FORMSTR;
                        switch (type)
                        {
                        case stctSize:
                        case stctGeneralNumber:
                            resID = IDS_SRVTYPECOL_FORMNUM;
                            leftAlignment = FALSE;
                            break;

                        case stctDate:
                        case stctGeneralDate:
                            resID = IDS_SRVTYPECOL_FORMDATE;
                            leftAlignment = FALSE;
                            break;

                        case stctTime:
                        case stctGeneralTime:
                            resID = IDS_SRVTYPECOL_FORMTIME;
                            leftAlignment = FALSE;
                            break;
                        }
                        SetDlgItemTextW(HWindow, IDT_COL_FORMHELP, LangStr(resID).c_str());

                        // enable the Empty Value edit line
                        BOOL enable = (type != stctName && type != stctExt && type != stctType);
                        HWND focus = GetFocus();
                        if (!enable && focus == GetDlgItem(HWindow, IDE_COL_EMPTY))
                            SendMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(HWindow, IDC_COL_TYPE), TRUE);
                        EnableWindow(GetDlgItem(HWindow, IDE_COL_EMPTY), enable);

                        // enable and configure the Alignment combo box
                        enable = type >= stctFirstGeneral;
                        if (!FirstSelNotifyAfterTransfer)
                        {
                            SendMessage(GetDlgItem(HWindow, IDC_COL_ALIGNMENT), CB_SETCURSEL,
                                        enable ? (leftAlignment ? 0 : 1) : -1, 0);
                        }
                        focus = GetFocus();
                        if (!enable && focus == GetDlgItem(HWindow, IDC_COL_ALIGNMENT))
                            SendMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(HWindow, IDC_COL_TYPE), TRUE);
                        EnableWindow(GetDlgItem(HWindow, IDC_COL_ALIGNMENT), enable);
                    }
                }
                FirstSelNotifyAfterTransfer = FALSE;
            }
            break;
        }
        }
        break;
    }
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CSrvTypeTestParserDlg
//

CSrvTypeTestParserDlg::CSrvTypeTestParserDlg(HWND parent, CFTPParser* parser,
                                             TIndirectArray<CSrvTypeColumn>* columns,
                                             char** rawListing, BOOL* rawListIncomplete)
    : CCenteredDialog(HLanguage, IDD_SRVTYPETESTPARSER, IDD_SRVTYPETESTPARSER, parent), Offsets(500, 500)
{
    HListView = NULL;
    Parser = parser;
    Columns = columns;
    RawListing = rawListing;
    RawListIncomplete = rawListIncomplete;
    AllocatedSizeOfRawListing = *RawListing == NULL ? 0 : (int)strlen(*RawListing) + 1;
    LastSelectedOffset = -1;

    SymbolsImageList = ImageList_Create(16, 16, ILC_MASK | SalamanderGeneral->GetImageListColorFlags(), 3, 0);
    if (SymbolsImageList != NULL)
    {
        ImageList_SetImageCount(SymbolsImageList, 3); // initialization

        HINSTANCE iconsDLL;
        if (WindowsVistaAndLater)
            iconsDLL = HANDLES(LoadLibraryExA("imageres.dll", NULL, LOAD_LIBRARY_AS_DATAFILE));
        else
        {
            const char* Shell32DLLName = "shell32.dll";
            iconsDLL = HANDLES(LoadLibraryExA(Shell32DLLName, NULL, LOAD_LIBRARY_AS_DATAFILE));
        }

        BOOL err = FALSE;
        if (iconsDLL != NULL)
        {
            HICON shortcutOverlay = (HICON)HANDLES(LoadImage(iconsDLL, MAKEINTRESOURCE(WindowsVistaAndLater ? 163 : 30),
                                                             IMAGE_ICON, 16, 16, SalamanderGeneral->GetIconLRFlags()));
            if (shortcutOverlay != NULL)
            {
                ImageList_ReplaceIcon(SymbolsImageList, 2, shortcutOverlay);
                ImageList_SetOverlayImage(SymbolsImageList, 2, 1);
                HANDLES(DestroyIcon(shortcutOverlay));
                int i;
                for (i = 0; i < 2; i++)
                {
                    int resID = (i == 0 ? 4 /* directory */ : 1 /* non-assoc. file */);
                    int vistaResID = (i == 0 ? 4 /* directory */ : 2 /* non-assoc. file */);
                    HICON hIcon = (HICON)HANDLES(LoadImage(iconsDLL, MAKEINTRESOURCE(WindowsVistaAndLater ? vistaResID : resID),
                                                           IMAGE_ICON, 16, 16, SalamanderGeneral->GetIconLRFlags()));
                    if (hIcon != NULL)
                    {
                        ImageList_ReplaceIcon(SymbolsImageList, i, hIcon);
                        HANDLES(DestroyIcon(hIcon));
                    }
                    else
                        err = TRUE;
                }
            }
            else
                err = TRUE;
            HANDLES(FreeLibrary(iconsDLL));
        }
        else
            err = TRUE;
        if (err)
        {
            ImageList_Destroy(SymbolsImageList);
            SymbolsImageList = NULL;
        }
    }

    MinDlgHeight = 0;
    MinDlgWidth = 0;
    ListingHeight = 0;
    ListingSpacing = 0;
    ButtonsY = 0;
    ParseBorderX = 0;
    ReadBorderX = 0;
    CloseBorderX = 0;
    HelpBorderX = 0;
    CloseBorderY = 0;
    ResultsSpacingX = 0;
    ResultsSpacingY = 0;
    SizeBoxWidth = 0;
    SizeBoxHeight = 0;

    SizeBox = NULL;
}

CSrvTypeTestParserDlg::~CSrvTypeTestParserDlg()
{
    if (SymbolsImageList != NULL)
        ImageList_Destroy(SymbolsImageList);
}

bool CSrvTypeTestParserDlg::PublishRawListing(const std::string& listing, size_t headroom)
{
    if (headroom > static_cast<size_t>(INT_MAX) - 1 ||
        listing.size() > static_cast<size_t>(INT_MAX) - headroom - 1)
        return false;
    const size_t required = listing.size() + 1;
    const size_t requested = required + headroom;
    if (static_cast<size_t>(AllocatedSizeOfRawListing) < required)
    {
        char* resized = static_cast<char*>(realloc(*RawListing, requested));
        if (resized == NULL)
            return false;
        *RawListing = resized;
        AllocatedSizeOfRawListing = static_cast<int>(requested);
    }
    memcpy(*RawListing, listing.c_str(), required);
    return true;
}

void CSrvTypeTestParserDlg::Transfer(CTransferInfo& ti)
{
    ti.CheckBox(IDC_PARSER_LISTINCOMPL, *RawListIncomplete);
    if (ti.Type == ttDataToWindow)
    {
        SendDlgItemMessage(HWindow, IDE_PARSER_RAWLIST, EM_LIMITTEXT, 0, 0);
        if (*RawListing != NULL)
            SetWindowLocalText(GetDlgItem(HWindow, IDE_PARSER_RAWLIST), *RawListing);
    }
    else
    {
        HWND edit = GetDlgItem(HWindow, IDE_PARSER_RAWLIST);
        std::string listing;
        if (ReadWindowLocalText(edit, listing))
        {
            if (!PublishRawListing(listing))
            {
                TRACE_E(LOW_MEMORY);
                ti.ErrorOn(IDE_PARSER_RAWLIST);
            }
        }
        else
            ti.ErrorOn(IDE_PARSER_RAWLIST);

        // release the image list from the list view, we want to free the image list ourselves
        ListView_SetImageList(HListView, NULL, LVSIL_SMALL);
        if (SymbolsImageList != NULL)
        {
            ImageList_Destroy(SymbolsImageList);
            SymbolsImageList = NULL; // simulate the state when the image list could not be created
        }
        ListView_DeleteAllItems(HListView); // clear the list view while the dialog is visible - overall better behavior
    }
}

void CSrvTypeTestParserDlg::InitColumns()
{
    LVCOLUMNW lvc;
    lvc.mask = LVCF_FMT | LVCF_TEXT | LVCF_SUBITEM;
    lvc.fmt = LVCFMT_LEFT;
    int i;
    for (i = 0; i < Columns->Count; i++) // create columns
    {
        CSrvTypeColumn* col = Columns->At(i);
        std::wstring bufNameW;
        if (col->NameID != -1)
            LoadStdColumnStrName(col->NameID, bufNameW);
        else
            FtpDecodeLocalText(HandleNULLStr(col->NameStr), bufNameW);
        lvc.pszText = const_cast<LPWSTR>(bufNameW.c_str());
        lvc.iSubItem = i;
        ListView_InsertColumn(HListView, i, &lvc);
        //    ListView_SetColumnWidth(HListView, i, LVSCW_AUTOSIZE_USEHEADER); // widths will be set later in SetColumnWidths()
    }
    if (SymbolsImageList != NULL)
        ListView_SetImageList(HListView, SymbolsImageList, LVSIL_SMALL);
}

void CSrvTypeTestParserDlg::SetColumnWidths()
{
    if (Columns->Count == 0)
        return;

    int i;
    for (i = 0; i < Columns->Count; i++)
        ListView_SetColumnWidth(HListView, i, LVSCW_AUTOSIZE_USEHEADER);
}

void CSrvTypeTestParserDlg::ParseListingToListView()
{
    HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

    // LockWindowUpdate(HListView);  // do not use - the entire Windows flickers
    //  SendMessage(HListView, WM_SETREDRAW, FALSE, 0);
    // when calling SetColumnWidths(), using only WM_SETREDRAW leads to
    // clearing the list view and thus unnecessary flickering. Therefore we hide the window during filling.
    SetWindowPos(HListView, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_HIDEWINDOW | SWP_NOREDRAW | SWP_NOSENDCHANGING | SWP_NOZORDER);

    int selIndex = 0;
    int topIndex = 0;
    if (ListView_GetItemCount(HListView) > 0)
    {
        topIndex = ListView_GetTopIndex(HListView);
        selIndex = ListView_GetNextItem(HListView, -1, LVIS_FOCUSED);
    }

    ListView_DeleteAllItems(HListView);
    Offsets.DestroyMembers();
    LastSelectedOffset = -1;

    const std::wstring strOnlyInPanel = LangStr(IDS_SRVTYPE_ONLYINPANEL).c_str();
    const std::wstring strDIR = LangStr(IDS_SRVTYPE_SIZEISDIR).c_str();

    CFileData file;
    const CFtpTextCodec textCodec = FtpLocalTextCodec();
    CFTPListingPluginDataInterface dataIface(Columns, FALSE, 0 /* not used here */,
                                              FALSE /* not used here */, textCodec);
    std::wstring formattedNumber;
    CQuadWord qwVal(0, 0);
    __int64 int64Val = 0;
    SYSTEMTIME stDateVal;
    GetLocalTime(&stDateVal);         // initialize to some valid values
    SYSTEMTIME stTimeVal = stDateVal; // initialize to some valid values
    SYSTEMTIME st;                    // helper
    FILETIME ft;                      // helper
    const char* listingStart = HandleNULLStr(*RawListing);
    const char* listing = listingStart;
    const char* listingEnd = listing + strlen(listing);
    if (*RawListIncomplete) // shorten the listing so that it contains only complete lines
    {
        const char* s = listingEnd;
        while (s > listingStart && *(s - 1) != '\r' && *(s - 1) != '\n')
            s--;
        listingEnd = s;
    }
    BOOL lowMem = FALSE;
    DWORD* emptyCol = new DWORD[Columns->Count]; // helper preallocated array for GetNextItemFromListing
    if (dataIface.IsGood() && emptyCol != NULL)
    {
        const char* itemStart = NULL;
        BOOL isDir = FALSE;
        int i = 0;
        Parser->BeforeParsing(listingStart, listingEnd, stDateVal.wYear, stDateVal.wMonth,
                              stDateVal.wDay, *RawListIncomplete); // initialize the parser
        while (Parser->GetNextItemFromListing(&file, &isDir, &dataIface, Columns, &listing,
                                              listingEnd, &itemStart, &lowMem, emptyCol, textCodec))
        {
            Offsets.Add((DWORD)(itemStart - listingStart));
            Offsets.Add((DWORD)(listing - listingStart));

            // the first column is always Name
            LVITEMW lvi;
            lvi.mask = LVIF_STATE | LVIF_TEXT | (SymbolsImageList != NULL ? LVIF_IMAGE : 0);
            lvi.iImage = (isDir ? 0 : 1);
            lvi.iItem = i;
            lvi.iSubItem = 0;
            lvi.state = (file.Hidden ? LVIS_CUT : 0) | (file.IsLink ? INDEXTOOVERLAYMASK(1) : 0);
            lvi.pszText = file.Name;
            ListView_InsertItem(HListView, &lvi);

            // insert data for other columns
            int j;
            for (j = 1; j < Columns->Count; j++)
            {
                std::wstring value;
                CSrvTypeColumn* col = Columns->At(j);
                switch (col->Type)
                {
                // case stctName:   // Name can only be in the first column
                case stctGeneralText:
                    FtpDecodeLocalText(HandleNULLStr(dataIface.GetStringFromColumn(file, j)), value);
                    break;

                case stctExt:
                case stctType:
                    value = strOnlyInPanel;
                    break;

                case stctSize:
                {
                    if (!isDir)
                    {
                        formattedNumber = SPLNumberToStrOwned(SalamanderGeneral, file.Size);
                        value = formattedNumber;
                    }
                    else
                        value = strDIR;
                    break;
                }

                case stctGeneralNumber:
                {
                    int64Val = dataIface.GetNumberFromColumn(file, j);
                    if (int64Val >= 0)
                    {
                        formattedNumber = SPLNumberToStrOwned(SalamanderGeneral, qwVal.SetUI64((unsigned __int64)int64Val));
                        value = formattedNumber;
                    }
                    else
                    {
                        if (int64Val != INT64_EMPTYNUMBER) // should not display ""
                        {
                            formattedNumber = SPLNumberToStrOwned(SalamanderGeneral, qwVal.SetUI64((unsigned __int64)(-int64Val)));
                            formattedNumber.insert(0, 1, L'-');
                            value = formattedNumber;
                        }
                    }
                    break;
                }

                case stctDate:
                {
                    FileTimeToLocalFileTime(&file.LastWrite, &ft);
                    FileTimeToSystemTime(&ft, &st);
                    FormatServerTypeDateTime(st, TRUE, value);
                    break;
                }

                case stctGeneralDate:
                {
                    dataIface.GetDateFromColumn(file, j, &stDateVal);
                    if (stDateVal.wDay != 0) // should not display ""
                    {
                        FormatServerTypeDateTime(stDateVal, TRUE, value);
                    }
                    break;
                }

                case stctTime:
                {
                    FileTimeToLocalFileTime(&file.LastWrite, &ft);
                    FileTimeToSystemTime(&ft, &st);
                    FormatServerTypeDateTime(st, FALSE, value);
                    break;
                }

                case stctGeneralTime:
                {
                    dataIface.GetTimeFromColumn(file, j, &stTimeVal);
                    if (stTimeVal.wHour != 24) // should not display ""
                    {
                        FormatServerTypeDateTime(stTimeVal, FALSE, value);
                    }
                    break;
                }
                }
                ListView_SetItemText(HListView, i, j, const_cast<LPWSTR>(value.c_str()));
            }
            i++;
            // release data of the file or directory
            dataIface.ReleasePluginData(file, isDir);
            free(file.Name);
        }
    }
    else
    {
        if (emptyCol == NULL)
            TRACE_E(LOW_MEMORY);
        lowMem = TRUE;
    }
    if (emptyCol != NULL)
        delete[] emptyCol;

    int count = ListView_GetItemCount(HListView);
    if (count > 0)
    {
        if (topIndex >= count)
            topIndex = count - 1;
        if (topIndex < 0)
            topIndex = 0;
        if (selIndex >= count)
            selIndex = count - 1;
        if (selIndex < 0)
            selIndex = 0;
        // replacement for SetTopIndex in the list view
        ListView_EnsureVisible(HListView, count - 1, FALSE);
        ListView_EnsureVisible(HListView, topIndex, FALSE);
        // normal focus
        DWORD state = LVIS_SELECTED | LVIS_FOCUSED;
        ListView_SetItemState(HListView, selIndex, state, state);
        ListView_EnsureVisible(HListView, selIndex, FALSE);
    }
    SetColumnWidths();

    //  LockWindowUpdate(NULL);   // do not use - Windows flickers during longer parsing
    //  SendMessage(HListView, WM_SETREDRAW, TRUE, 0);
    SetWindowPos(HListView, NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_SHOWWINDOW /*| SWP_NOREDRAW */ | SWP_NOSENDCHANGING | SWP_NOZORDER);

    SetCursor(oldCur);

    // if the entire listing was not parsed successfully, report an error
    if (!lowMem && listing < listingEnd) // we do not report lack of memory (fatal error)
    {
        SalamanderGeneral->SalMessageBox(HWindow, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_SRVTYPE_PARSEERROR).c_str(),
                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                         MB_OK | MB_ICONEXCLAMATION);
        // highlight the error location in the parsing rules text
        DWORD errorPos = (DWORD)(listing - listingStart);
        SendDlgItemMessage(HWindow, IDE_PARSER_RAWLIST, EM_SETSEL, (WPARAM)errorPos,
                           (LPARAM)errorPos);
        SendDlgItemMessage(HWindow, IDE_PARSER_RAWLIST, EM_SCROLLCARET, 0, 0); // scroll caret into view
        SendMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(HWindow, IDE_PARSER_RAWLIST), TRUE);
    }
}

void CSrvTypeTestParserDlg::LoadTextFromFile()
{
    static std::wstring initDir;
    if (initDir.empty())
        GetMyDocumentsPathW(initDir);
    std::vector<std::wstring> selectedFiles;
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = HWindow;
    std::wstring filter = LangStr(IDS_RAWLISTINGFILTER).c_str();
    for (wchar_t& ch : filter)
        if (ch == L'|')
            ch = 0;
    filter.push_back(0);
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrInitialDir = initDir.empty() ? NULL : initDir.c_str();
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (SPLSafeGetOpenFileNamesOwned(SalamanderGeneral, &ofn, selectedFiles) && selectedFiles.size() == 1)
    {
        const std::wstring& fileName = selectedFiles[0];
        HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
        FtpRememberSelectedDirectory(fileName, initDir);

        HANDLE file = HANDLES_Q(CreateFileW(fileName.c_str(), GENERIC_READ,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                           OPEN_EXISTING,
                                           FILE_FLAG_SEQUENTIAL_SCAN,
                                           NULL));
        CQuadWord size;
        DWORD err = NO_ERROR;
        if (file != INVALID_HANDLE_VALUE &&
            SalamanderGeneral->SalGetFileSize(file, size, err))
        {
            int len;
            if (size > CQuadWord(1024 * 1024, 0))
            {
                SalamanderGeneral->SalMessageBox(HWindow, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_SRVTYPE_READRAWLISTLIMIT).c_str(),
                                                 SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPPLUGINTITLE).c_str(),
                                                 MB_OK | MB_ICONWARNING);
                len = 1024 * 1024;
            }
            else
                len = (DWORD)size.Value;

            try
            {
                std::string fileBytes(static_cast<size_t>(len), '\0');
                DWORD read;
                if (ReadFile(file, fileBytes.data(), len, &read, NULL) && read == (DWORD)len)
                {
                    if (PublishRawListing(fileBytes))
                        SetWindowLocalText(GetDlgItem(HWindow, IDE_PARSER_RAWLIST), *RawListing);
                    else
                        err = ERROR_NOT_ENOUGH_MEMORY;
                }
                else
                    err = GetLastError();
            }
            catch (const std::bad_alloc&)
            {
                err = ERROR_NOT_ENOUGH_MEMORY;
            }

            HANDLES(CloseHandle(file));
            SetCursor(oldCur);
            if (err != NO_ERROR) // print the error
            {
                try
                {
                    const std::wstring errorText = SPLFormatStringOwned(LangStr(IDS_SRVTYPE_READRAWLISTERR).c_str(), SPLGetErrorTextOwned(SalamanderGeneral, err).c_str());
                    SalamanderGeneral->SalMessageBox(HWindow, errorText.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                                     MB_OK | MB_ICONEXCLAMATION);
                }
                catch (const std::bad_alloc&)
                {
                    SalamanderGeneral->SalMessageBox(HWindow, L"Not enough memory.", L"FTP", MB_OK | MB_ICONEXCLAMATION);
                }
            }
        }
        else
        {
            if (err == NO_ERROR)
                err = GetLastError();
            if (file != INVALID_HANDLE_VALUE)
                HANDLES(CloseHandle(file));
            SetCursor(oldCur);
            try
            {
                const std::wstring errorText = SPLFormatStringOwned(LangStr(IDS_SRVTYPE_READRAWLISTERR).c_str(), SPLGetErrorTextOwned(SalamanderGeneral, err).c_str());
                SalamanderGeneral->SalMessageBox(HWindow, errorText.c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                                 MB_OK | MB_ICONEXCLAMATION);
            }
            catch (const std::bad_alloc&)
            {
                SalamanderGeneral->SalMessageBox(HWindow, L"Not enough memory.", L"FTP", MB_OK | MB_ICONEXCLAMATION);
            }
        }
    }
}

void CSrvTypeTestParserDlg::OnWMSize(int width, int height, BOOL notInitDlg, WPARAM wParam)
{
    HWND listing = GetDlgItem(HWindow, IDE_PARSER_RAWLIST);
    HWND buttonParse = GetDlgItem(HWindow, IDB_PARSER_PARSELIST);
    HWND buttonRead = GetDlgItem(HWindow, IDB_PARSER_LOADLIST);
    HWND buttonClose = GetDlgItem(HWindow, IDOK);
    HWND buttonHelp = GetDlgItem(HWindow, IDHELP);
    HWND results = GetDlgItem(HWindow, IDL_PARSER_COLUMNS);

    HDWP hdwp = HANDLES(BeginDeferWindowPos(8));
    if (hdwp != NULL)
    {
        hdwp = HANDLES(DeferWindowPos(hdwp, listing, NULL, 0, 0, width - ListingSpacing, ListingHeight,
                                      SWP_NOZORDER | SWP_NOMOVE));

        hdwp = HANDLES(DeferWindowPos(hdwp, buttonParse, NULL, width - ParseBorderX, ButtonsY, 0, 0,
                                      SWP_NOZORDER | SWP_NOSIZE));

        hdwp = HANDLES(DeferWindowPos(hdwp, buttonRead, NULL, width - ReadBorderX, ButtonsY, 0, 0,
                                      SWP_NOZORDER | SWP_NOSIZE));

        hdwp = HANDLES(DeferWindowPos(hdwp, results, NULL, 0, 0, width - ResultsSpacingX, height - ResultsSpacingY,
                                      SWP_NOZORDER | SWP_NOMOVE));

        hdwp = HANDLES(DeferWindowPos(hdwp, buttonClose, NULL, width - CloseBorderX, height - CloseBorderY, 0, 0,
                                      SWP_NOZORDER | SWP_NOSIZE));

        hdwp = HANDLES(DeferWindowPos(hdwp, buttonHelp, NULL, width - HelpBorderX, height - CloseBorderY, 0, 0,
                                      SWP_NOZORDER | SWP_NOSIZE));

        if (SizeBox != NULL)
        {
            hdwp = HANDLES(DeferWindowPos(hdwp, SizeBox, NULL, width - SizeBoxWidth,
                                          height - SizeBoxHeight, SizeBoxWidth, SizeBoxHeight, SWP_NOZORDER));
            if (notInitDlg)
            {
                // supposedly show/hide cannot be combined with resizing and moving
                hdwp = HANDLES(DeferWindowPos(hdwp, SizeBox, NULL, 0, 0, 0, 0,
                                              SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | (wParam == SIZE_RESTORED ? SWP_SHOWWINDOW : SWP_HIDEWINDOW)));
            }
        }

        HANDLES(EndDeferWindowPos(hdwp));
    }
}

INT_PTR
CSrvTypeTestParserDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CSrvTypeTestParserDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        // disable select-all on focus and set a fixed font for the listing edit
        CSimpleDlgControlWindow* wnd = new CSimpleDlgControlWindow(HWindow, IDE_PARSER_RAWLIST, FALSE);
        if (wnd != NULL && wnd->HWindow == NULL)
            delete wnd; // failed to attach - it does not deallocate itself
        if (FixedFont != NULL)
            SendDlgItemMessage(HWindow, IDE_PARSER_RAWLIST, WM_SETFONT, (WPARAM)FixedFont, TRUE);

        RECT r1, r2, r3, r4, r5, r6;
        GetWindowRect(HWindow, &r1);
        GetClientRect(GetDlgItem(HWindow, IDE_PARSER_RAWLIST), &r2);
        GetClientRect(GetDlgItem(HWindow, IDL_PARSER_COLUMNS), &r3);
        MinDlgHeight = r1.bottom - r1.top - r3.bottom + 50; // allow the edit to shrink to 50 points of client area "results of parsing"
        GetWindowRect(GetDlgItem(HWindow, IDC_PARSER_LISTINCOMPL), &r4);
        GetWindowRect(GetDlgItem(HWindow, IDB_PARSER_PARSELIST), &r5);
        GetWindowRect(GetDlgItem(HWindow, IDB_PARSER_LOADLIST), &r6);
        MinDlgWidth = r1.right - r1.left - r2.right + r4.right - r4.left + r6.right - r5.left; // allow the edit control's client area to fit at least the checkbox and two buttons including the gap between them

        RECT r7, r8, r9, r10;
        GetWindowRect(GetDlgItem(HWindow, IDE_PARSER_RAWLIST), &r7);
        ListingHeight = r7.bottom - r7.top;
        GetClientRect(HWindow, &r8);
        ListingSpacing = r8.right - (r7.right - r7.left);
        POINT p;
        p.x = r5.left;
        p.y = r5.top;
        ScreenToClient(HWindow, &p);
        ParseBorderX = r8.right - p.x;
        ButtonsY = p.y;
        p.x = r6.left;
        p.y = r6.top;
        ScreenToClient(HWindow, &p);
        ReadBorderX = r8.right - p.x;
        GetWindowRect(GetDlgItem(HWindow, IDOK), &r9);
        p.x = r9.left;
        p.y = r9.top;
        ScreenToClient(HWindow, &p);
        CloseBorderX = r8.right - p.x;
        CloseBorderY = r8.bottom - p.y;
        GetWindowRect(GetDlgItem(HWindow, IDHELP), &r9);
        p.x = r9.left;
        p.y = r9.top;
        ScreenToClient(HWindow, &p);
        HelpBorderX = r8.right - p.x;
        GetWindowRect(GetDlgItem(HWindow, IDL_PARSER_COLUMNS), &r10);
        ResultsSpacingX = r8.right - (r10.right - r10.left);
        ResultsSpacingY = r8.bottom - (r10.bottom - r10.top);

        // insert a resize grip into the bottom right corner
        SizeBox = CreateWindowExW(0,
                                 L"scrollbar",
                                 L"",
                                 WS_CHILDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_VISIBLE |
                                     WS_GROUP | SBS_SIZEBOX | SBS_SIZEGRIP | SBS_SIZEBOXBOTTOMRIGHTALIGN,
                                 0, 0, r8.right, r8.bottom,
                                 HWindow,
                                 NULL,
                                 DLLInstance,
                                 NULL);
        RECT r11;
        GetClientRect(SizeBox, &r11);
        SizeBoxWidth = r11.right;
        SizeBoxHeight = r11.bottom;

        // set the desired initial dimensions of the dialog
        if (Config.TestParserDlgWidth > 0 && Config.TestParserDlgHeight > 0)
            MoveWindow(HWindow, r1.left, r1.top, Config.TestParserDlgWidth, Config.TestParserDlgHeight, TRUE);

        // perform the dialog layout
        RECT clientRect;
        GetClientRect(HWindow, &clientRect);
        OnWMSize(clientRect.right, clientRect.bottom, FALSE, 0);

        // transfer data into the window
        INT_PTR ret = CCenteredDialog::DialogProc(uMsg, wParam, lParam);

        // listview setup
        HListView = GetDlgItem(HWindow, IDL_PARSER_COLUMNS);
        DWORD exFlags = LVS_EX_FULLROWSELECT;
        DWORD origFlags = ListView_GetExtendedListViewStyle(HListView);
        ListView_SetExtendedListViewStyle(HListView, origFlags | exFlags); // 4.71

        // insert columns
        InitColumns();

        // set column widths
        SetColumnWidths();

        // immediately after opening the dialog start parsing the listing
        PostMessage(HWindow, WM_COMMAND, IDB_PARSER_PARSELIST, 0);

        if (Config.TestParserDlgWidth == -2) // show maximized
        {
            ShowWindow(HWindow, SW_MAXIMIZE);
            UpdateWindow(HWindow);
        }

        return ret;
    }

    case WM_SIZE:
    {
        RECT clientRect;
        GetClientRect(HWindow, &clientRect);
        OnWMSize(clientRect.right, clientRect.bottom, TRUE, wParam);
        break;
    }

    case WM_GETMINMAXINFO:
    {
        LPMINMAXINFO lpmmi = (LPMINMAXINFO)lParam;
        lpmmi->ptMinTrackSize.x = MinDlgWidth;
        lpmmi->ptMinTrackSize.y = MinDlgHeight;
        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDCANCEL:
            wParam = IDOK;
            break; // in this dialog Cancel = OK (data always need to be transferred)

        case IDE_PARSER_RAWLIST:
        {
            if (HIWORD(wParam) == EN_CHANGE)
                Offsets.DestroyMembers(); // text change -> offsets are no longer valid
            break;
        }

        case IDB_PARSER_PARSELIST:
        {
            *RawListIncomplete = IsDlgButtonChecked(HWindow, IDC_PARSER_LISTINCOMPL) == BST_CHECKED;
            HWND edit = GetDlgItem(HWindow, IDE_PARSER_RAWLIST);
            std::string listing;
            if (ReadWindowLocalText(edit, listing))
            {
                if (PublishRawListing(listing, 49))
                {
                    // start parsing, results go directly into the list view
                    ParseListingToListView();
                }
                else
                    TRACE_E(LOW_MEMORY);
            }
            return TRUE; // do not continue further
        }

        case IDB_PARSER_LOADLIST:
        {
            LoadTextFromFile();
            return TRUE; // do not continue further
        }
        }
        break;
    }

    case WM_NOTIFY:
    {
        if (wParam == IDL_PARSER_COLUMNS)
        {
            LPNMHDR nmh = (LPNMHDR)lParam;
            if (nmh->code == LVN_DELETEALLITEMS)
            {
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, TRUE); // suppress sending LVN_DELETEITEM for every item
                return TRUE;
            }
            if (nmh->code == LVN_ITEMCHANGED)
            {
                LPNMLISTVIEW nmhi = (LPNMLISTVIEW)nmh;
                if ((nmhi->uOldState & LVIS_SELECTED) != (nmhi->uNewState & LVIS_SELECTED))
                {
                    int i = 2 * nmhi->iItem;
                    if (LastSelectedOffset != i && i >= 0 && i + 1 < Offsets.Count)
                    {
                        LastSelectedOffset = i;
                        SendDlgItemMessage(HWindow, IDE_PARSER_RAWLIST, EM_SETSEL, Offsets[i + 1], Offsets[i]);
                        SendDlgItemMessage(HWindow, IDE_PARSER_RAWLIST, EM_SCROLLCARET, 0, 0);
                    }
                }
            }
        }
        break;
    }

    case WM_SYSCOLORCHANGE:
    {
        ListView_SetBkColor(HListView, GetSysColor(COLOR_WINDOW));
        break;
    }

    case WM_DESTROY:
    {
        if (IsZoomed(HWindow))
            Config.TestParserDlgWidth = -2;
        else
        {
            if (!IsIconic(HWindow)) // "always false"
            {
                RECT r;
                GetWindowRect(HWindow, &r);
                Config.TestParserDlgWidth = r.right - r.left;
                Config.TestParserDlgHeight = r.bottom - r.top;
            }
        }
        break;
    }
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CCopyMoveDlg
//

CCopyMoveDlg::CCopyMoveDlg(HWND parent, std::wstring& path, const wchar_t* title,
                           const wchar_t* subject, wchar_t* history[], int historyCount, int helpID)
    : CCenteredDialog(HLanguage, IDD_COPYMOVEDLG, helpID, parent),
      Path(path)
{
    Title = title;
    Subject = subject;
    History = history;
    HistoryCount = historyCount;
}

void CCopyMoveDlg::Transfer(CTransferInfo& ti)
{
    CALL_STACK_MESSAGE1("CCopyMoveDlg::Transfer()");
    if (History != NULL)
    {
        HWND hWnd;
        if (ti.GetControl(hWnd, IDC_TGTPATH))
        {
            if (ti.Type == ttDataToWindow)
            {
                SalamanderGeneral->LoadComboFromStdHistoryValues(hWnd, History, HistoryCount);
                SendMessageW(hWnd, WM_SETTEXT, 0, (LPARAM)Path.c_str());
            }
            else
            {
                const int length = GetWindowTextLengthW(hWnd);
                std::vector<wchar_t> value(static_cast<size_t>(length) + 1, L'\0');
                GetWindowTextW(hWnd, value.data(), length + 1);
                Path.assign(value.data());
                SalamanderGeneral->AddValueToStdHistoryValues(History, HistoryCount,
                                                               Path.c_str(), FALSE);
            }
        }
    }
    ti.CheckBox(IDC_ADDTOQUEUE, Config.DownloadAddToQueue);
}

INT_PTR
CCopyMoveDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CCopyMoveDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        SalamanderGeneral->InstallWordBreakProc(GetDlgItem(HWindow, IDC_TGTPATH)); // install WordBreakProc into the combo box
        SetWindowTextW(HWindow, Title);
        SetDlgItemTextW(HWindow, IDT_TGTPATHSUBJECT, Subject);
        break;
    }
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CConfirmDeleteDlg
//

CConfirmDeleteDlg::CConfirmDeleteDlg(HWND parent, const wchar_t* subject, HICON icon)
    : CCenteredDialog(HLanguage, IDD_CONFIRMDELETEDLG, parent)
{
    Subject = subject;
    Icon = icon;
}

void CConfirmDeleteDlg::Transfer(CTransferInfo& ti)
{
    ti.CheckBox(IDC_DELADDTOQUEUE, Config.DeleteAddToQueue);
}

INT_PTR
CConfirmDeleteDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CConfirmDeleteDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        SetDlgItemTextW(HWindow, IDT_DELSUBJECT, Subject);
        SendDlgItemMessage(HWindow, IDC_DELICON, STM_SETICON,
                           (WPARAM)(Icon == NULL ? HANDLES(LoadIcon(NULL, IDI_QUESTION)) : Icon), 0);
        break;
    }
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CChangeAttrsDlg
//

CChangeAttrsDlg::CChangeAttrsDlg(HWND parent, const wchar_t* subject, DWORD attr, DWORD attrDiff,
                                 BOOL selDirs)
    : CCenteredDialog(HLanguage, IDD_CHANGEATTRSDLG, IDD_CHANGEATTRSDLG, parent)
{
    Subject = subject;
    Attr = attr;
    AttrDiff = attrDiff;
    SelFiles = TRUE;
    SelDirs = selDirs;
    IncludeSubdirs = FALSE;
    AttrAndMask = 0777;
    AttrOrMask = 0;
    EnableNotification = TRUE;
}

void CChangeAttrsDlg::RefreshNumValue()
{
    UINT userRead = IsDlgButtonChecked(HWindow, IDC_READOWNER);
    UINT groupRead = IsDlgButtonChecked(HWindow, IDC_READGROUP);
    UINT othersRead = IsDlgButtonChecked(HWindow, IDC_READOTHERS);
    UINT userWrite = IsDlgButtonChecked(HWindow, IDC_WRITEOWNER);
    UINT groupWrite = IsDlgButtonChecked(HWindow, IDC_WRITEGROUP);
    UINT othersWrite = IsDlgButtonChecked(HWindow, IDC_WRITEOTHERS);
    UINT userExec = IsDlgButtonChecked(HWindow, IDC_EXECUTEOWNER);
    UINT groupExec = IsDlgButtonChecked(HWindow, IDC_EXECUTEGROUP);
    UINT othersExec = IsDlgButtonChecked(HWindow, IDC_EXECUTEOTHERS);

    std::wstring text(3, L'-');
    if (userRead == 2 || userWrite == 2 || userExec == 2)
        text[0] = L'-';
    else
        text[0] = L'0' + (userRead << 2) + (userWrite << 1) + userExec;
    if (groupRead == 2 || groupWrite == 2 || groupExec == 2)
        text[1] = L'-';
    else
        text[1] = L'0' + (groupRead << 2) + (groupWrite << 1) + groupExec;
    if (othersRead == 2 || othersWrite == 2 || othersExec == 2)
        text[2] = L'-';
    else
        text[2] = L'0' + (othersRead << 2) + (othersWrite << 1) + othersExec;
    EnableNotification = FALSE;
    HWND edit = GetDlgItem(HWindow, IDE_NUMATTRVALUE);
    DWORD start = 0;
    DWORD end = 0;
    SendMessage(edit, EM_GETSEL, (WPARAM)(&start), (LPARAM)(&end));
    SetWindowTextW(edit, text.c_str());
    SendMessage(edit, EM_SETSEL, start, end);
    EnableNotification = TRUE;
}

void CChangeAttrsDlg::Validate(CTransferInfo& ti)
{
    if (IsDlgButtonChecked(HWindow, IDC_READOWNER) == 2 &&
        IsDlgButtonChecked(HWindow, IDC_READGROUP) == 2 &&
        IsDlgButtonChecked(HWindow, IDC_READOTHERS) == 2 &&
        IsDlgButtonChecked(HWindow, IDC_WRITEOWNER) == 2 &&
        IsDlgButtonChecked(HWindow, IDC_WRITEGROUP) == 2 &&
        IsDlgButtonChecked(HWindow, IDC_WRITEOTHERS) == 2 &&
        IsDlgButtonChecked(HWindow, IDC_EXECUTEOWNER) == 2 &&
        IsDlgButtonChecked(HWindow, IDC_EXECUTEGROUP) == 2 &&
        IsDlgButtonChecked(HWindow, IDC_EXECUTEOTHERS) == 2)
    {
        SalamanderGeneral->SalMessageBox(HWindow, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_CHATTRNOTHINGTODO2).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                         MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(IDC_READOWNER);
        return;
    }

    BOOL files, dirs;
    ti.CheckBox(IDC_CHATTRSETFILES, files);
    ti.CheckBox(IDC_CHATTRSETDIRS, dirs);
    if (!files && !dirs)
    {
        SalamanderGeneral->SalMessageBox(HWindow, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_CHATTRNOTHINGTODO).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                         MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(IDC_CHATTRSETFILES);
        return;
    }
    const std::wstring text = SPLGetDlgItemTextOwned(HWindow, IDE_NUMATTRVALUE);
    if (text.size() != 3)
    {
        SalamanderGeneral->SalMessageBox(HWindow, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_CHATTRNUMVAL3DIGITS).c_str(), SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_FTPERRORTITLE).c_str(),
                                         MB_OK | MB_ICONEXCLAMATION);
        ti.ErrorOn(IDE_NUMATTRVALUE);
        return;
    }
}

void CChangeAttrsDlg::Transfer(CTransferInfo& ti)
{
    int userRead = (AttrDiff & 0400) ? 2 : ((Attr & 0400) != 0);
    int groupRead = (AttrDiff & 0040) ? 2 : ((Attr & 0040) != 0);
    int othersRead = (AttrDiff & 0004) ? 2 : ((Attr & 0004) != 0);
    int userWrite = (AttrDiff & 0200) ? 2 : ((Attr & 0200) != 0);
    int groupWrite = (AttrDiff & 0020) ? 2 : ((Attr & 0020) != 0);
    int othersWrite = (AttrDiff & 0002) ? 2 : ((Attr & 0002) != 0);
    int userExec = (AttrDiff & 0100) ? 2 : ((Attr & 0100) != 0);
    int groupExec = (AttrDiff & 0010) ? 2 : ((Attr & 0010) != 0);
    int othersExec = (AttrDiff & 0001) ? 2 : ((Attr & 0001) != 0);
    ti.CheckBox(IDC_READOWNER, userRead);
    ti.CheckBox(IDC_WRITEOWNER, userWrite);
    ti.CheckBox(IDC_EXECUTEOWNER, userExec);
    ti.CheckBox(IDC_READGROUP, groupRead);
    ti.CheckBox(IDC_WRITEGROUP, groupWrite);
    ti.CheckBox(IDC_EXECUTEGROUP, groupExec);
    ti.CheckBox(IDC_READOTHERS, othersRead);
    ti.CheckBox(IDC_WRITEOTHERS, othersWrite);
    ti.CheckBox(IDC_EXECUTEOTHERS, othersExec);

    if (ti.Type == ttDataFromWindow)
    {
        AttrAndMask = 0777;
        AttrOrMask = 0;
        if (userRead == 0)
            AttrAndMask &= ~0400;
        if (groupRead == 0)
            AttrAndMask &= ~0040;
        if (othersRead == 0)
            AttrAndMask &= ~0004;
        if (userWrite == 0)
            AttrAndMask &= ~0200;
        if (groupWrite == 0)
            AttrAndMask &= ~0020;
        if (othersWrite == 0)
            AttrAndMask &= ~0002;
        if (userExec == 0)
            AttrAndMask &= ~0100;
        if (groupExec == 0)
            AttrAndMask &= ~0010;
        if (othersExec == 0)
            AttrAndMask &= ~0001;

        if (userRead == 1)
            AttrOrMask |= 0400;
        if (groupRead == 1)
            AttrOrMask |= 0040;
        if (othersRead == 1)
            AttrOrMask |= 0004;
        if (userWrite == 1)
            AttrOrMask |= 0200;
        if (groupWrite == 1)
            AttrOrMask |= 0020;
        if (othersWrite == 1)
            AttrOrMask |= 0002;
        if (userExec == 1)
            AttrOrMask |= 0100;
        if (groupExec == 1)
            AttrOrMask |= 0010;
        if (othersExec == 1)
            AttrOrMask |= 0001;
    }
    else
        RefreshNumValue();

    ti.CheckBox(IDC_INCLUDESUBDIRS, IncludeSubdirs);
    ti.CheckBox(IDC_CHATTRSETFILES, SelFiles);
    ti.CheckBox(IDC_CHATTRSETDIRS, SelDirs);
    ti.CheckBox(IDC_CHATTRADDTOQUEUE, Config.ChAttrAddToQueue);
}

void UpdateCheckBox(HWND hWindow, int checkID, int value)
{
    if (IsDlgButtonChecked(hWindow, checkID) != (UINT)value)
        CheckDlgButton(hWindow, checkID, value);
}

INT_PTR
CChangeAttrsDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CChangeAttrsDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        SetDlgItemTextW(HWindow, IDT_CHATTRSUBJECT, Subject);
        EnableWindow(GetDlgItem(HWindow, IDC_INCLUDESUBDIRS), SelDirs);
        EnableWindow(GetDlgItem(HWindow, IDC_CHATTRSETFILES), SelDirs);
        EnableWindow(GetDlgItem(HWindow, IDC_CHATTRSETDIRS), SelDirs);
        SendDlgItemMessage(HWindow, IDE_NUMATTRVALUE, EM_LIMITTEXT, 3, 0);
        break;
    }

    case WM_COMMAND:
    {
        if (EnableNotification)
        {
            if (HIWORD(wParam) == BN_CLICKED &&
                (LOWORD(wParam) == IDC_READOWNER ||
                 LOWORD(wParam) == IDC_WRITEOWNER ||
                 LOWORD(wParam) == IDC_EXECUTEOWNER ||
                 LOWORD(wParam) == IDC_READGROUP ||
                 LOWORD(wParam) == IDC_WRITEGROUP ||
                 LOWORD(wParam) == IDC_EXECUTEGROUP ||
                 LOWORD(wParam) == IDC_READOTHERS ||
                 LOWORD(wParam) == IDC_WRITEOTHERS ||
                 LOWORD(wParam) == IDC_EXECUTEOTHERS))
            {
                RefreshNumValue();
            }
            else
            {
                if (HIWORD(wParam) == EN_CHANGE)
                {
                    const std::wstring text = SPLGetDlgItemTextOwned(HWindow, IDE_NUMATTRVALUE);
                    if (text.size() == 3)
                    {
                        DWORD attr = 0;
                        DWORD attrDiff = 0;
                        if (text[0] >= L'0' && text[0] <= L'7')
                            attr |= (text[0] - L'0') << 6;
                        else
                            attrDiff |= 0700;
                        if (text[1] >= L'0' && text[1] <= L'7')
                            attr |= (text[1] - L'0') << 3;
                        else
                            attrDiff |= 0070;
                        if (text[2] >= L'0' && text[2] <= L'7')
                            attr |= text[2] - L'0';
                        else
                            attrDiff |= 0007;

                        int userRead = (attrDiff & 0400) ? 2 : ((attr & 0400) != 0);
                        int groupRead = (attrDiff & 0040) ? 2 : ((attr & 0040) != 0);
                        int othersRead = (attrDiff & 0004) ? 2 : ((attr & 0004) != 0);
                        int userWrite = (attrDiff & 0200) ? 2 : ((attr & 0200) != 0);
                        int groupWrite = (attrDiff & 0020) ? 2 : ((attr & 0020) != 0);
                        int othersWrite = (attrDiff & 0002) ? 2 : ((attr & 0002) != 0);
                        int userExec = (attrDiff & 0100) ? 2 : ((attr & 0100) != 0);
                        int groupExec = (attrDiff & 0010) ? 2 : ((attr & 0010) != 0);
                        int othersExec = (attrDiff & 0001) ? 2 : ((attr & 0001) != 0);
                        UpdateCheckBox(HWindow, IDC_READOWNER, userRead);
                        UpdateCheckBox(HWindow, IDC_WRITEOWNER, userWrite);
                        UpdateCheckBox(HWindow, IDC_EXECUTEOWNER, userExec);
                        UpdateCheckBox(HWindow, IDC_READGROUP, groupRead);
                        UpdateCheckBox(HWindow, IDC_WRITEGROUP, groupWrite);
                        UpdateCheckBox(HWindow, IDC_EXECUTEGROUP, groupExec);
                        UpdateCheckBox(HWindow, IDC_READOTHERS, othersRead);
                        UpdateCheckBox(HWindow, IDC_WRITEOTHERS, othersWrite);
                        UpdateCheckBox(HWindow, IDC_EXECUTEOTHERS, othersExec);

                        RefreshNumValue();
                    }
                }
            }
        }
        break;
    }
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}

//
// ****************************************************************************
// CViewErrAsciiTrForBinFileDlg
//

CViewErrAsciiTrForBinFileDlg::CViewErrAsciiTrForBinFileDlg(HWND parent)
    : CCenteredDialog(HLanguage, IDD_VIEWERRASCIITRFORBINFILE, parent)
{
}

INT_PTR
CViewErrAsciiTrForBinFileDlg::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CViewErrAsciiTrForBinFileDlg::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    if (uMsg == WM_INITDIALOG)
    {
        SendDlgItemMessage(HWindow, IDI_INFOICON, STM_SETICON,
                           (WPARAM)HANDLES(LoadIcon(NULL, IDI_QUESTION)), 0);
    }
    if (uMsg == WM_COMMAND && LOWORD(wParam) == IDIGNORE)
    {
        if (Modal)
            EndDialog(HWindow, LOWORD(wParam));
        else
            DestroyWindow(HWindow);
        return TRUE;
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}
