// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <algorithm>
#include "regedt_number_parse.h"

TDirectArray<DWORD_PTR> DialogStack(4, 4);
CCS DialogStackCS;

BOOL MinBeepWhenDone;

std::vector<std::wstring> CopyOrMoveHistory;

HFONT EnvFont = NULL; // environment font (edit, toolbar, header, status)
int EnvFontHeight;    // font height

//BOOL HaveForMicrosoftSanfSerif; // the system has the Microsoft Sans Serif font installed

BOOL CreateEnvFont()
{
    CALL_STACK_MESSAGE1("CreateEnvFont()");
    NONCLIENTMETRICS ncm;
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0);
    LOGFONT* lf = &ncm.lfMenuFont;

    if (EnvFont != NULL)
        DeleteObject(EnvFont);
    EnvFont = CreateFontIndirect(lf);
    if (EnvFont == NULL)
    {
        TRACE_E("Failed to create the font.");
        return FALSE;
    }

    // determine the height
    HDC dc = GetDC(NULL);
    HFONT oldFont = (HFONT)SelectObject(dc, EnvFont);
    TEXTMETRIC tm;
    GetTextMetrics(dc, &tm);
    EnvFontHeight = tm.tmHeight + tm.tmExternalLeading;
    SelectObject(dc, oldFont);
    ReleaseDC(NULL, dc);
    return TRUE;
}

int CALLBACK
EnumFontsProc(CONST LOGFONT* lplf, CONST TEXTMETRIC* lptm, DWORD dwType, LPARAM lpData)
{
    CALL_STACK_MESSAGE3("EnumFontsProc(, , 0x%X, 0x%IX)", dwType, lpData);
    *(LPBOOL)lpData = TRUE;
    return 0;
}

/*
BOOL
CheckForMicrosoftSanfSerif()
{
  CALL_STACK_MESSAGE1("CheckForMicrosoftSanfSerif()");
  BOOL ret = FALSE;
  HDC hdc = GetDC(NULL);
  EnumFonts(hdc, "Microsoft Sans Serif", EnumFontsProc, (LPARAM) &ret);
  ReleaseDC(NULL, hdc);
  return ret;
}
*/

void WINAPI HTMLHelpCallback(HWND hWindow, UINT helpID)
{
    SG->OpenHtmlHelp(hWindow, HHCDisplayContext, helpID, FALSE);
}

BOOL InitDialogs()
{
    CALL_STACK_MESSAGE1("InitDialogs()");
    if (!InitializeWinLib(L"RegEdt", DLLInstance))
        return FALSE;
    SetupWinLibHelp(HTMLHelpCallback);

    if (!CreateEnvFont())
    {
        ReleaseWinLib(DLLInstance);
        return FALSE;
    }

    CopyOrMoveHistory.clear();
    PatternHistory.clear();
    LookInHistory.clear();

    MinBeepWhenDone = TRUE;
    SG->GetConfigParameter(SALCFG_MINBEEPWHENDONE, &MinBeepWhenDone, sizeof(BOOL), NULL);

    //  HaveForMicrosoftSanfSerif = CheckForMicrosoftSanfSerif();
    //  TRACE_I("HaveForMicrosoftSanfSerif = " << HaveForMicrosoftSanfSerif);

    return TRUE;
}

void ReleaseDialogs()
{
    CALL_STACK_MESSAGE1("ReleaseDialogs()");
    ReleaseWinLib(DLLInstance);

    CopyOrMoveHistory.clear();
    PatternHistory.clear();
    LookInHistory.clear();

    if (EnvFont)
        DeleteObject(EnvFont);
}

void DialogStackPush(HWND hWindow)
{
    CALL_STACK_MESSAGE1("DialogStackPush()");
    DialogStackCS.Enter();
    DialogStack.Add((DWORD_PTR)hWindow);
    DialogStack.Add(GetCurrentThreadId());
    DialogStackCS.Leave();
}

void DialogStackPop()
{
    CALL_STACK_MESSAGE1("DialogStackPop()");
    DialogStackCS.Enter();
    int i = DialogStack.Count - 1;
    while (i > 0)
    {
        if (DialogStack[i] == GetCurrentThreadId())
        {
            DialogStack.Delete(i--);
            DialogStack.Delete(i);
            break;
        }
        i -= 2;
    }
    DialogStackCS.Leave();
}

HWND DialogStackPeek()
{
    CALL_STACK_MESSAGE1("DialogStackPeek()");
    DialogStackCS.Enter();
    HWND ret = (HWND)-1;
    int i = DialogStack.Count - 1;
    while (i > 0)
    {
        if (DialogStack[i] == GetCurrentThreadId())
        {
            ret = (HWND)DialogStack[--i];
            break;
        }
        i -= 2;
    }
    DialogStackCS.Leave();
    return ret;
}

void HistoryComboBox(CTransferInfoEx& ti, int id, std::wstring& text,
                     std::vector<std::wstring>& history)
{
    HWND combo;
    if (!ti.GetControl(combo, id))
        return;

    if (ti.Type == ttDataFromWindow)
    {
        text = SPLGetWindowTextOwned(combo);
        const auto duplicate = std::find_if(history.begin(), history.end(), [&text](const std::wstring& item) {
            return CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                                  item.c_str(), -1, text.c_str(), -1) == CSTR_EQUAL;
        });
        if (duplicate != history.end())
            history.erase(duplicate);
        history.insert(history.begin(), text);
        if (history.size() > MAX_HISTORY_ENTRIES)
            history.resize(MAX_HISTORY_ENTRIES);
    }

    SendMessage(combo, CB_RESETCONTENT, 0, 0);
    for (const std::wstring& item : history)
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
    if (ti.Type == ttDataFromWindow)
        SendMessage(combo, CB_SETCURSEL, 0, 0);
    else
    {
        SendMessageW(combo, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(text.c_str()));
        SendMessage(combo, CB_SETEDITSEL, 0, -1);
    }
}

// ****************************************************************************
//
// CTransferInfoEx
//
//

void CTransferInfoEx::EditLineW(int ctrlID, LPWSTR buffer, DWORD bufferSize, BOOL select)
{
    CALL_STACK_MESSAGE4("CTransferInfoEx::EditLineW(%d, , 0x%X, %d)", ctrlID,
                        bufferSize, select);
    HWND HWindow;
    if (GetControl(HWindow, ctrlID))
    {
        switch (Type)
        {
        case ttDataToWindow:
        {
            SendMessageW(HWindow, EM_LIMITTEXT, bufferSize - 1, 0);
            SendMessageW(HWindow, WM_SETTEXT, 0, (LPARAM)buffer);
            if (select)
                SendMessage(HWindow, EM_SETSEL, 0, -1);
            break;
        }

        case ttDataFromWindow:
        {
            SendMessageW(HWindow, WM_GETTEXT, bufferSize, (LPARAM)buffer);
            break;
        }
        }
    }
}

// ****************************************************************************
//
// CDialogEx
//
//

BOOL CDialogEx::ValidateData()
{
    CALL_STACK_MESSAGE1("CDialogEx::ValidateData()");
    CTransferInfoEx ti(HWindow, ttDataFromWindow);
    Validate(ti);
    if (!ti.IsGood())
    {
        HWND ctrl;
        if (ti.GetControl(ctrl, ti.FailCtrlID, TRUE))
        {
            HWND wnd = GetFocus();
            while (wnd != NULL && wnd != ctrl)
                wnd = GetParent(wnd);
            if (wnd == NULL) // focus only if the control is not an ancestor of GetFocus
            {                // such as the edit line in a combo box
                SendMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)ctrl, TRUE);
            }
        }
        return FALSE;
    }
    else
        return TRUE;
}

BOOL CDialogEx::TransferData(CTransferType type)
{
    CALL_STACK_MESSAGE1("CDialogEx::TransferData()");
    CTransferInfoEx ti(HWindow, type);
    Transfer(ti);
    if (!ti.IsGood())
    {
        HWND ctrl;
        if (ti.GetControl(ctrl, ti.FailCtrlID, TRUE))
        {
            HWND wnd = GetFocus();
            while (wnd != NULL && wnd != ctrl)
                wnd = GetParent(wnd);
            if (wnd == NULL) // focus only if the control is not an ancestor of GetFocus
            {                // such as the edit line in a combo box
                SendMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)ctrl, TRUE);
            }
        }
        return FALSE;
    }
    else
        return TRUE;
}

INT_PTR
CDialogEx::Execute()
{
    CALL_STACK_MESSAGE1("CDialogEx::Execute()");
    Modal = TRUE;

    //  if (HaveForMicrosoftSanfSerif)
    //  {
    return DialogBoxParamW(Modul, MAKEINTRESOURCEW(ResID), Parent,
                           (DLGPROC)CDialog::CDialogProc, (LPARAM)this);
    //  }

    // if we don't have Microsoft Sans Serif we must replace it in the template
    // with MS Shell Dlg
    /*
  DWORD size;
  LPDLGTEMPLATE templ = LoadDlgTemplate(ResID, size);
  if (!templ) return -1;

  int ret = DialogBoxIndirectParamW(
    Modul, ReplaceDlgTemplateFont(templ, size, L"MS Shell Dlg"),  // dialog box template
    Parent, (DLGPROC)CDialog::CDialogProc, (LPARAM)this);

  free(templ);

  return ret;
*/
}

HWND CDialogEx::Create()
{
    CALL_STACK_MESSAGE1("CDialogEx::Create()");
    Modal = FALSE;

    //  if (HaveForMicrosoftSanfSerif)
    //  {
    return CreateDialogParamW(Modul, MAKEINTRESOURCEW(ResID), Parent,
                              (DLGPROC)CDialog::CDialogProc, (LPARAM)this);
    //  }

    // if we don't have Microsoft Sans Serif we must replace it in the template
    // with MS Shell Dlg
    /*
  DWORD size;
  LPDLGTEMPLATE templ = LoadDlgTemplate(ResID, size);
  if (!templ) return NULL;

  HWND ret = CreateDialogIndirectParamW(
    Modul, ReplaceDlgTemplateFont(templ, size, L"MS Shell Dlg"),  // dialog box template
    Parent, (DLGPROC)CDialog::CDialogProc, (LPARAM)this);

  free(templ);

  return ret;
*/
}

INT_PTR
CDialogEx::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SLOW_CALL_STACK_MESSAGE4("CDialogEx::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam,
                             lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        DialogStackPush(HWindow);
        SG->MultiMonCenterWindow(HWindow, CenterToHWnd, FALSE);
        break;
    }

    case WM_DESTROY:
    {
        DialogStackPop();
        break;
    }
    }
    return CDialog::DialogProc(uMsg, wParam, lParam);
}

void CDialogEx::NotifDlgJustCreated()
{
    SalGUI->ArrangeHorizontalLines(HWindow);
}

// ****************************************************************************
//
// CNewKeyDialog
//
//

void CNewKeyDialog::Validate(CTransferInfoEx& ti)
{
    CALL_STACK_MESSAGE1("CNewKeyDialog::Validate()");
    const std::wstring name = SPLGetDlgItemTextOwned(HWindow, IDE_NAME);
    if (name.empty())
    {
        SG->SalMessageBox(HWindow, LoadStrW(IDS_EMPTY).c_str(), LoadStrW(IDS_ERROR).c_str(), MB_ICONERROR);
        ti.ErrorOn(IDE_NAME);
    }
}

void CNewKeyDialog::Transfer(CTransferInfoEx& ti)
{
    CALL_STACK_MESSAGE1("CNewKeyDialog::Transfer()");
    if (ti.Type == ttDataToWindow)
    {
        if (Title)
            SendMessageW(HWindow, WM_SETTEXT, 0, (LPARAM)Title);
        if (Text)
            SendMessageW(GetDlgItem(HWindow, IDS_TEXT), WM_SETTEXT, 0, (LPARAM)Text);
    }
    if (ti.Type == ttDataToWindow)
        SetDlgItemTextW(HWindow, IDE_NAME, KeyName.c_str());
    else
        KeyName = SPLGetDlgItemTextOwned(HWindow, IDE_NAME);
    if (Direct)
        ti.CheckBox(IDC_DIRECT, *Direct);
}

// ****************************************************************************
//
// CNewValDialog
//
//

void CNewValDialog::Validate(CTransferInfoEx& ti)
{
    CALL_STACK_MESSAGE1("CNewValDialog::Validate()");
    CNewKeyDialog::Validate(ti);
}

void CNewValDialog::Transfer(CTransferInfoEx& ti)
{
    CALL_STACK_MESSAGE1("CNewValDialog::Transfer()");
    HWND combo = GetDlgItem(HWindow, IDC_TYPE);
    if (ti.Type == ttDataToWindow)
    {
        int sel = 0;
        CRegTypeText* tt = RegTypeTexts;
        while (tt->Text != NULL)
        {
            if (DlgType == vdtNewValDialog && tt->CanCreate ||
                DlgType == vdtEditValDialog && tt->CanEdit)
            {
                int ret = (int)SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)tt->Text);
                if (ret != CB_ERR)
                {
                    SendMessage(combo, CB_SETITEMDATA, ret, (LPARAM)tt->Type);
                    if (tt->Type == *Type)
                        sel = ret;
                }
            }
            tt++;
        }
        SendMessage(combo, CB_SETCURSEL, sel, 0);
    }
    else
    {
        int ret = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
        if (ret == CB_ERR)
        {
            ti.ErrorOn(IDC_TYPE);
            return;
        }
        *Type = (DWORD)SendMessage(combo, CB_GETITEMDATA, ret, 0); // x64 - Type is DWORD
    }
    CNewKeyDialog::Transfer(ti);
}

// ****************************************************************************
//
// CEditValDialog
//
//

void CEditValDialog::Validate(CTransferInfoEx& ti)
{
    CALL_STACK_MESSAGE1("CEditValDialog::Validate()");
    CNewValDialog::Validate(ti);
    if (!ti.IsGood())
        return;

    HWND combo = GetDlgItem(HWindow, IDC_TYPE);
    int ret = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
    if (ret == CB_ERR)
    {
        ti.ErrorOn(IDC_TYPE);
        return;
    }
    *Type = (DWORD)SendMessage(combo, CB_GETITEMDATA, ret, 0); // x64 - Type is DWORD

    if (*Type == REG_DWORD || *Type == REG_DWORD_BIG_ENDIAN || *Type == REG_QWORD)
    {
        HWND dataHWnd = GetDlgItem(HWindow, IDE_DATA);
        const std::wstring buffer = SPLGetWindowTextOwned(dataHWnd);
        QWORD d;
        if (Hex)
        {
            if (buffer.empty() || buffer.size() > 16 ||
                !ParseRegedtUnsignedHex(buffer, d))
            {
                Error(IDS_INVALIDHEXVALUE);
                ti.ErrorOn(IDE_DATA);
                return;
            }
        }
        else
        {
            if (buffer.empty() || !ParseRegedtUnsignedDecimal(buffer, d))
            {
                Error(IDS_INVALIDDECVALUE);
                ti.ErrorOn(IDE_DATA);
                return;
            }
        }
        if (*Type != REG_QWORD && HIDWORD(d) != 0)
        {
            Error(IDS_NOTDWORD);
            ti.ErrorOn(IDE_DATA);
        }
    }
}

void CEditValDialog::Transfer(CTransferInfoEx& ti)
{
    CALL_STACK_MESSAGE1("CEditValDialog::Transfer()");
    CNewValDialog::Transfer(ti);
    if (!ti.IsGood())
        return;

    HWND dataHWnd = GetDlgItem(HWindow, IDE_DATA);
    if (ti.Type == ttDataToWindow)
    {
        //SendMessageW(GetDlgItem(HWindow, IDE_NAME), WM_SETTEXT, 0, (LPARAM) KeyName);
        SendMessage(GetDlgItem(HWindow, IDR_HEX), BM_SETCHECK, BST_CHECKED, 0);
        RECT r;
        GetWindowRect(dataHWnd, &r);
        EditWidth = r.right - r.left;
        EditHeight = r.bottom - r.top;

        if (*Type != REG_DWORD && *Type != REG_DWORD_BIG_ENDIAN && *Type != REG_QWORD)
        {
            ShowWindow(GetDlgItem(HWindow, IDR_HEX), SW_HIDE);
            ShowWindow(GetDlgItem(HWindow, IDR_DEC), SW_HIDE);
            ShowWindow(GetDlgItem(HWindow, IDS_BASE), SW_HIDE);

            SendMessageW(dataHWnd, EM_LIMITTEXT, 0, 0);
            SendMessageW(dataHWnd, WM_SETTEXT, 0, (LPARAM)Data);
        }
        else
        {
            SendMessageW(dataHWnd, EM_LIMITTEXT, *Type == REG_QWORD ? 16 : 8, 0);
            SetWindowPos(dataHWnd, NULL, 0, 0, (int)(EditWidth * 0.46), EditHeight, SWP_NOZORDER | SWP_NOMOVE);

            QWORD d = *Type == REG_QWORD ? *(LPQWORD)Data : *(LPDWORD)Data;
            if (*Type == REG_DWORD_BIG_ENDIAN)
                d = d >> 24 | (d & 0x00FF0000) >> 8 | (d & 0x0000FF00) << 8 | (d & 0x000000FF) << 24;
            const std::wstring buffer = SPLFormatStringOwned(L"%I64x", d);
            SetWindowTextW(dataHWnd, buffer.c_str());
        }

        SetFocus(GetDlgItem(HWindow, IDE_DATA));
    }
    else
    {
        if (*Type == REG_DWORD || *Type == REG_DWORD_BIG_ENDIAN || *Type == REG_QWORD)
        {
            const std::wstring buffer = SPLGetWindowTextOwned(dataHWnd);
            QWORD d;
            if (Hex)
                ParseRegedtUnsignedHex(buffer, d);
            else
                ParseRegedtUnsignedDecimal(buffer, d);
            if (*Type == REG_DWORD_BIG_ENDIAN) // reverse the endianness
            {
                d = d >> 24 | (d & 0x00FF0000) >> 8 | (d & 0x0000FF00) << 8 | (d & 0x000000FF) << 24;
            }
            DWORD s = *Type == REG_QWORD ? 8 : 4;
            if (s > Allocated)
            {
                void* ptr = realloc(Data, s);
                if (!ptr)
                {
                    Error(IDS_LOWMEM);
                    ti.ErrorOn(IDE_DATA);
                    return;
                }
                Data = (LPBYTE)ptr;
                Allocated = s;
            }
            Size = s;
            if (*Type == REG_QWORD)
                *(LPQWORD)Data = d;
            else
                *(LPDWORD)Data = (DWORD)d;
        }
        else
        {
            DWORD l = (DWORD)SendMessageW(dataHWnd, WM_GETTEXTLENGTH, 0, 0) * 2 + 2;
            if (l > Allocated)
            {
                void* ptr = realloc(Data, l);
                if (!ptr)
                {
                    Error(IDS_LOWMEM);
                    ti.ErrorOn(IDE_DATA);
                    return;
                }
                Data = (LPBYTE)ptr;
                Allocated = l;
            }
            Size = l;
            SendMessageW(dataHWnd, WM_GETTEXT, Size / 2, (LPARAM)Data);
        }
    }
}

INT_PTR
CEditValDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CEditValDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg,
                        wParam, lParam);
    switch (uMsg)
    {
        /* j.r. if we set the tab order correctly, this is not needed and the edit will be selected too
    case WM_INITDIALOG:
    {
      CNewValDialog::DialogProc(uMsg, wParam, lParam);
      SetFocus(GetDlgItem(HWindow, IDE_DATA));
      return FALSE;
    }
    */

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDC_TYPE:
        {
            HWND combo = (HWND)lParam;
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                int ret = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
                if (ret == CB_ERR)
                    break;
                *Type = (DWORD)SendMessage(combo, CB_GETITEMDATA, ret, 0); // x64 - Type is DWORD

                DWORD show = *Type == REG_DWORD || *Type == REG_DWORD_BIG_ENDIAN || *Type == REG_QWORD ? SW_SHOW : SW_HIDE;
                ShowWindow(GetDlgItem(HWindow, IDR_HEX), show);
                ShowWindow(GetDlgItem(HWindow, IDR_DEC), show);
                ShowWindow(GetDlgItem(HWindow, IDS_BASE), show);
                int max;
                int width;
                if (show == SW_HIDE)
                {
                    max = 0;
                    width = EditWidth;
                }
                else
                {

                    max = Hex ? 8 : 10;
                    if (*Type == REG_QWORD)
                        max *= 2;
                    width = (int)(EditWidth * 0.46);
                }
                SetWindowPos(GetDlgItem(HWindow, IDE_DATA), NULL, 0, 0, width, EditHeight, SWP_NOZORDER | SWP_NOMOVE);
                SendMessageW(GetDlgItem(HWindow, IDE_DATA), EM_LIMITTEXT, max, 0);
            }
            break;
        }

        case IDR_DEC:
        case IDR_HEX:
        {
            BOOL hex = SendMessage(GetDlgItem(HWindow, IDR_HEX), BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (hex != Hex)
            {
                HWND combo = (HWND)GetDlgItem(HWindow, IDC_TYPE);
                int ret = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
                if (ret == CB_ERR)
                    break;
                *Type = (DWORD)SendMessage(combo, CB_GETITEMDATA, ret, 0); // x64 - Type is DWORD

                HWND dataHWnd = GetDlgItem(HWindow, IDE_DATA);
                std::wstring buffer = SPLGetWindowTextOwned(dataHWnd);
                QWORD d;
                BOOL success = TRUE;
                if (hex)
                {
                    if (buffer.empty() || !ParseRegedtUnsignedDecimal(buffer, d))
                    {
                        if (!buffer.empty())
                            Error(IDS_INVALIDDECVALUE);
                        success = FALSE;
                    }
                    else
                        buffer = SPLFormatStringOwned(L"%I64x", d);
                }
                else
                {
                    if (buffer.empty() || buffer.size() > 16 ||
                        !ParseRegedtUnsignedHex(buffer, d))
                    {
                        if (!buffer.empty())
                            Error(IDS_INVALIDHEXVALUE);
                        success = FALSE;
                    }
                    else
                        buffer = SPLFormatStringOwned(L"%I64u", d);
                }
                if (success)
                {
                    Hex = hex;
                    int max = Hex ? 8 : 10;
                    if (*Type == REG_QWORD)
                        max *= 2;
                    if (success)
                        SetWindowTextW(dataHWnd, buffer.c_str());
                    SendMessageW(dataHWnd, EM_LIMITTEXT, max, 0);
                }
                else
                {
                    SendMessage(GetDlgItem(HWindow, IDR_HEX), BM_SETCHECK, Hex ? BST_CHECKED : BST_UNCHECKED, 0);
                    SendMessage(GetDlgItem(HWindow, IDR_DEC), BM_SETCHECK, Hex ? BST_UNCHECKED : BST_CHECKED, 0);
                }
            }
            break;
        }
        }
        break;
    }
    }
    return CNewValDialog::DialogProc(uMsg, wParam, lParam);
}

// ****************************************************************************
//
// CRawEditValDialog
//
//

BOOL CRawEditValDialog::ExportToTempFile()
{
    CALL_STACK_MESSAGE1("CRawEditValDialog::ExportToTempFile()");
    // already exported
    if (!TempFile.empty())
        return TRUE;

    // create a temp file name
    if (!SPLSalGetTempFileNameOwned(SG, NULL, L"SAL", TempDir, FALSE, NULL))
        return Error(IDS_CREATETEMP);

    TempFile = TempDir;
    SPLSalPathAddBackslashOwned(TempFile);
    TempFile.push_back(L'_');
    const size_t nameOffset = TempFile.size();
    TempFile.append(KeyName);
    ReplaceUnsafeCharacters(TempFile.data() + nameOffset);

    // create/open the temp file
    HANDLE file = CreateFileW(TempFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        SG->RemoveTemporaryDir(TempDir.c_str());
        TempDir.clear();
        TempFile.clear();
        return Error(IDS_CREATETEMP);
    }

    DWORD written;
    BOOL b = WriteFile(file, Data, Size, &written, NULL) || written != Size;

    CloseHandle(file);

    if (!b)
    {
        SG->RemoveTemporaryDir(TempDir.c_str());
        TempDir.clear();
        TempFile.clear();
        Error(IDS_WRITETEMP);
    }

    return b;
}

BOOL CRawEditValDialog::ImportFromTempFile()
{
    CALL_STACK_MESSAGE1("CRawEditValDialog::ImportFromTempFile()");
    // create/open the temp file
    HANDLE file = CreateFileW(TempFile.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return Error(IDS_OPENTEMP);

    CQuadWord size;
    DWORD err;
    if (!SG->SalGetFileSize(file, size, err))
    {
        CloseHandle(file);
        return ErrorL(err, GetParent(), IDS_SIZEOFTEMP);
    }

    if (size.HiDWord > 0)
    {
        CloseHandle(file);
        return Error(IDS_LONGDATA);
    }

    LPBYTE newData = (LPBYTE)malloc(size.LoDWord);
    if (!newData)
    {
        CloseHandle(file);
        return Error(IDS_LOWMEM);
    }

    DWORD read;
    BOOL b = ReadFile(file, newData, size.LoDWord, &read, NULL) || read != size.LoDWord;

    CloseHandle(file);

    if (b)
    {
        if (Data)
            free(Data);
        Data = newData;
        Size = Allocated = size.LoDWord;
    }
    else
        Error(IDS_READTEMP);

    return b;
}

void CRawEditValDialog::Transfer(CTransferInfoEx& ti)
{
    CALL_STACK_MESSAGE1("CRawEditValDialog::Transfer()");
    CNewValDialog::Transfer(ti);
    if (!ti.IsGood())
        return;

    // load the new data from the temp file
    if (Edit && !ImportFromTempFile())
    {
        ti.ErrorOn(IDB_EDIT);
        return;
    }
}

INT_PTR
CRawEditValDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CRawEditValDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg,
                        wParam, lParam);
    switch (uMsg)
    {
        /* j.r. if we set the tab order correctly, this is not needed and the edit will be selected too
    case WM_INITDIALOG:
    {
      CNewValDialog::DialogProc(uMsg, wParam, lParam);
      SendMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(HWindow, IDB_EDIT), TRUE);
      return FALSE;
    }
    */

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDB_EDIT:
        {
            if (ExportToTempFile())
            {
                if (ExecuteEditor(TempFile.c_str()))
                {
                    Edit = TRUE;
                    ShowWindow(GetDlgItem(HWindow, IDS_MESSAGE), SW_SHOW);
                }
            }
            break;
        }
        }
        break;
    }

    case WM_DESTROY:
    {
        if (!TempDir.empty())
            SG->RemoveTemporaryDir(TempDir.c_str());
        break;
    }
    }
    return CNewValDialog::DialogProc(uMsg, wParam, lParam);
}

// ****************************************************************************
//
// CCopyOrMoveDialog
//
//

void CCopyOrMoveDialog::Transfer(CTransferInfoEx& ti)
{
    CALL_STACK_MESSAGE1("CCopyOrMoveDialog::Transfer()");
    if (ti.Type == ttDataToWindow)
    {
        if (Title)
            SendMessageW(HWindow, WM_SETTEXT, 0, (LPARAM)Title);
        if (Text)
            SendMessageW(GetDlgItem(HWindow, IDS_TEXT), WM_SETTEXT, 0, (LPARAM)Text);
    }
    HistoryComboBox(ti, IDE_NAME, KeyName, CopyOrMoveHistory);
    if (Direct)
        ti.CheckBox(IDC_DIRECT, *Direct);
}

// ****************************************************************************
//
// CConfigDialog
//
//

void CConfigDialog::Validate(CTransferInfoEx& ti)
{
    CALL_STACK_MESSAGE1("CConfigDialog::Validate()");
    int e1, e2;
    const std::wstring command = SPLGetDlgItemTextOwned(HWindow, IDE_COMMAND);
    if (!SG->ValidateVarString(HWindow, command.c_str(), e1, e2, ExpCommandVariables))
    {
        ti.ErrorOn(IDE_COMMAND);
        SendMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(HWindow, IDE_COMMAND), TRUE);
        SendDlgItemMessage(HWindow, IDE_COMMAND, EM_SETSEL, e1, e2);
        return;
    }

    const std::wstring arguments = SPLGetDlgItemTextOwned(HWindow, IDE_ARGUMENTS);
    if (!SG->ValidateVarString(HWindow, arguments.c_str(), e1, e2, ExpArgumentsVariables))
    {
        ti.ErrorOn(IDE_ARGUMENTS);
        SendMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(HWindow, IDE_ARGUMENTS), TRUE);
        SendDlgItemMessage(HWindow, IDE_ARGUMENTS, EM_SETSEL, e1, e2);
        return;
    }

    const std::wstring initDir = SPLGetDlgItemTextOwned(HWindow, IDE_INITDIR);
    if (!SG->ValidateVarString(HWindow, initDir.c_str(), e1, e2, ExpInitDirVariables))
    {
        ti.ErrorOn(IDE_INITDIR);
        SendMessage(HWindow, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(HWindow, IDE_INITDIR), TRUE);
        SendDlgItemMessage(HWindow, IDE_INITDIR, EM_SETSEL, e1, e2);
        return;
    }
}

void CConfigDialog::Transfer(CTransferInfoEx& ti)
{
    CALL_STACK_MESSAGE1("CConfigDialog::Transfer()");
    if (ti.Type == ttDataToWindow)
    {
        SetDlgItemTextW(HWindow, IDE_COMMAND, Command.c_str());
        SetDlgItemTextW(HWindow, IDE_ARGUMENTS, Arguments.c_str());
        SetDlgItemTextW(HWindow, IDE_INITDIR, InitDir.c_str());
    }
    else
    {
        Command = SPLGetDlgItemTextOwned(HWindow, IDE_COMMAND);
        Arguments = SPLGetDlgItemTextOwned(HWindow, IDE_ARGUMENTS);
        InitDir = SPLGetDlgItemTextOwned(HWindow, IDE_INITDIR);
    }
}

INT_PTR
CConfigDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CConfigDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg,
                        wParam, lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        SalGUI->ChangeToArrowButton(HWindow, IDB_BROWSE);
        SalGUI->ChangeToArrowButton(HWindow, IDB_ARGHELP);
        SalGUI->ChangeToArrowButton(HWindow, IDB_INITDIRHELP);
        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDB_BROWSE:
        {
            RECT r;
            GetWindowRect((HWND)lParam, &r);

            // create the menu
            MENU_TEMPLATE_ITEM templ[] =
                {
                    {MNTT_PB, 0, 0, 0, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_BROWSE, MNTS_ALL, 1, -1, 0, NULL},
                    {MNTT_SP, 0, MNTS_ALL, 0, -1, 0, NULL},

                    {MNTT_IT, IDS_EXP_WINDIR, MNTS_ALL, 2, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_SYSDIR, MNTS_ALL, 3, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_SALDIR, MNTS_ALL, 4, -1, 0, NULL},

                    {MNTT_SP, 0, MNTS_ALL, 0, -1, 0, NULL},

                    {MNTT_IT, IDS_EXP_ENVVAR, MNTS_ALL, 30, -1, 0, NULL},

                    {MNTT_PE, 0, 0, 0, -1, 0, NULL}};

            CGUIMenuPopupAbstract* menu = SalGUI->CreateMenuPopup();
            if (!menu)
                return TRUE;
            if (!menu->LoadFromTemplate(HLanguage, templ, NULL, NULL, NULL))
            {
                SalGUI->DestroyMenuPopup(menu);
                return TRUE;
            }

            DWORD cmd = menu->Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON | MENU_TRACK_NONOTIFY,
                                    r.right, r.top, HWindow, NULL);

            if (cmd > 0)
            {
                if (cmd == 1)
                {
                    std::wstring path = SPLGetDlgItemTextOwned(HWindow, IDE_COMMAND);
                    if (ShowOpenFileDialog(HWindow, NULL, LangStr(IDS_EXEFILES).c_str(), path))
                        SetDlgItemTextW(HWindow, IDE_COMMAND, path.c_str());
                }
                else if (cmd == 30)
                {
                    SendDlgItemMessageW(HWindow, IDE_COMMAND, EM_REPLACESEL, TRUE, (LPARAM)L"$[]");
                }
                else
                {
                    // double-check just to be sure
                    if (cmd < 30)
                    {
                        std::wstring var = L"$(" + std::wstring(ExpCommandVariables[cmd - 2].Name) + L")";
                        SendDlgItemMessageW(HWindow, IDE_COMMAND, EM_REPLACESEL, TRUE, (LPARAM)var.c_str());
                    }
                }
            }

            SalGUI->DestroyMenuPopup(menu);
            return TRUE;
        }

        case IDB_ARGHELP:
        {
            RECT r;
            GetWindowRect((HWND)lParam, &r);

            // create the menu
            MENU_TEMPLATE_ITEM templ[] =
                {
                    {MNTT_PB, 0, 0, 0, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_FULLNAME, MNTS_ALL, 1, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_DRIVE, MNTS_ALL, 2, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_PATH, MNTS_ALL, 3, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_NAME, MNTS_ALL, 4, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_NAMEPART, MNTS_ALL, 5, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_EXTPART, MNTS_ALL, 6, -1, 0, NULL},

                    {MNTT_SP, 0, MNTS_ALL, 0, -1, 0, NULL},

                    {MNTT_IT, IDS_EXP_FULLPATH, MNTS_ALL, 7, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_WINDIR, MNTS_ALL, 8, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_SYSDIR, MNTS_ALL, 9, -1, 0, NULL},

                    {MNTT_SP, 0, MNTS_ALL, 0, -1, 0, NULL},

                    {MNTT_IT, IDS_EXP_DOSFULLNAME, MNTS_ALL, 10, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_DOSDRIVE, MNTS_ALL, 11, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_DOSPATH, MNTS_ALL, 12, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_DOSNAME, MNTS_ALL, 13, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_DOSNAMEPART, MNTS_ALL, 14, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_DOSEXTPART, MNTS_ALL, 15, -1, 0, NULL},

                    {MNTT_SP, 0, MNTS_ALL, 0, -1, 0, NULL},

                    {MNTT_IT, IDS_EXP_DOSFULLPATH, MNTS_ALL, 16, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_DOSWINDIR, MNTS_ALL, 17, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_DOSSYSDIR, MNTS_ALL, 18, -1, 0, NULL},

                    {MNTT_SP, 0, MNTS_ALL, 0, -1, 0, NULL},

                    {MNTT_IT, IDS_EXP_ENVVAR, MNTS_ALL, 30, -1, 0, NULL},

                    {MNTT_PE, 0, 0, 0, -1, 0, NULL}};

            CGUIMenuPopupAbstract* menu = SalGUI->CreateMenuPopup();
            if (!menu)
                return TRUE;
            if (!menu->LoadFromTemplate(HLanguage, templ, NULL, NULL, NULL))
            {
                SalGUI->DestroyMenuPopup(menu);
                return TRUE;
            }

            DWORD cmd = menu->Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON | MENU_TRACK_NONOTIFY,
                                    r.right, r.top, HWindow, NULL);

            if (cmd > 0)
            {
                if (cmd == 30)
                {
                    SendDlgItemMessageW(HWindow, IDE_ARGUMENTS, EM_REPLACESEL, TRUE, (LPARAM)L"$[]");
                }
                else
                {
                    // double-check just to be sure
                    if (cmd < 19)
                    {
                        std::wstring var = L"$(" + std::wstring(ExpArgumentsVariables[cmd - 1].Name) + L")";
                        SendDlgItemMessageW(HWindow, IDE_ARGUMENTS, EM_REPLACESEL, TRUE, (LPARAM)var.c_str());
                    }
                }
            }

            SalGUI->DestroyMenuPopup(menu);
            return TRUE;
        }

        case IDB_INITDIRHELP:
        {
            RECT r;
            GetWindowRect((HWND)lParam, &r);

            // create the menu
            MENU_TEMPLATE_ITEM templ[] =
                {
                    {MNTT_PB, 0, 0, 0, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_DRIVE, MNTS_ALL, 1, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_PATH, MNTS_ALL, 2, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_FULLPATH, MNTS_ALL, 3, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_WINDIR, MNTS_ALL, 4, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_SYSDIR, MNTS_ALL, 5, -1, 0, NULL},
                    {MNTT_SP, 0, MNTS_ALL, 0, -1, 0, NULL},
                    {MNTT_IT, IDS_EXP_ENVVAR, MNTS_ALL, 30, -1, 0, NULL},
                    {MNTT_PE, 0, 0, 0, -1, 0, NULL}};

            CGUIMenuPopupAbstract* menu = SalGUI->CreateMenuPopup();
            if (!menu)
                return TRUE;
            if (!menu->LoadFromTemplate(HLanguage, templ, NULL, NULL, NULL))
            {
                SalGUI->DestroyMenuPopup(menu);
                return TRUE;
            }

            DWORD cmd = menu->Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON | MENU_TRACK_NONOTIFY,
                                    r.right, r.top, HWindow, NULL);

            if (cmd > 0)
            {
                if (cmd == 30)
                {
                    SendDlgItemMessageW(HWindow, IDE_INITDIR, EM_REPLACESEL, TRUE, (LPARAM)L"$[]");
                }
                else
                {
                    // double-check just to be sure
                    if (cmd < 6)
                    {
                        std::wstring var = L"$(" + std::wstring(ExpInitDirVariables[cmd - 1].Name) + L")";
                        SendDlgItemMessageW(HWindow, IDE_INITDIR, EM_REPLACESEL, TRUE, (LPARAM)var.c_str());
                    }
                }
            }

            SalGUI->DestroyMenuPopup(menu);
            return TRUE;
        }
        }
        break;
    }
    }
    return CDialogEx::DialogProc(uMsg, wParam, lParam);
}

// ****************************************************************************
//
// CExportDialog
//
//

void CExportDialog::Validate(CTransferInfoEx& ti)
{
    CALL_STACK_MESSAGE1("CExportDialog::Validate()");
    const std::wstring name = SPLGetDlgItemTextOwned(HWindow, IDE_NAME);
    if (name.empty())
    {
        SG->SalMessageBox(HWindow, LoadStrW(IDS_EMPTY).c_str(), LoadStrW(IDS_ERROR).c_str(), MB_ICONERROR);
        ti.ErrorOn(IDE_NAME);
    }

    const std::wstring file = SPLGetDlgItemTextOwned(HWindow, IDE_FILE);
    if (file.empty())
    {
        SG->SalMessageBox(HWindow, LoadStrW(IDS_EMPTY).c_str(), LoadStrW(IDS_ERROR).c_str(), MB_ICONERROR);
        ti.ErrorOn(IDE_FILE);
    }
}

void CExportDialog::Transfer(CTransferInfoEx& ti)
{
    CALL_STACK_MESSAGE1("CExportDialog::Transfer()");
    if (ti.Type == ttDataToWindow)
        SetDlgItemTextW(HWindow, IDE_NAME, Path.c_str());
    else
        Path = SPLGetDlgItemTextOwned(HWindow, IDE_NAME);
    if (ti.Type == ttDataToWindow)
        SetDlgItemTextW(HWindow, IDE_FILE, File->c_str());
    else
        *File = SPLGetDlgItemTextOwned(HWindow, IDE_FILE);
}

INT_PTR
CExportDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CExportDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg,
                        wParam, lParam);
    switch (uMsg)
    {
    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDB_BROWSE:
        {
            std::wstring path = SPLGetDlgItemTextOwned(HWindow, IDE_FILE);
            if (!path.empty() && path.back() == L'\\')
                path.pop_back();
            if (ShowOpenFileDialog(HWindow, NULL, LangStr(IDS_REGFILES).c_str(), path, TRUE))
            {
                // scan back to the first '.' or '\\', exactly as pre-unicode's
                // SalPathAddExtension did; find_last_of returns npos when the
                // character is absent, so comparing the two positions directly
                // gets both the no-extension and the no-separator case wrong
                SPLSalPathAddExtensionOwned(SG, path, L".reg");
                SetDlgItemTextW(HWindow, IDE_FILE, path.c_str());
            }
            return TRUE;
        }
        }
    }
    }
    return CDialogEx::DialogProc(uMsg, wParam, lParam);
}
