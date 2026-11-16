#include "precomp.h"

#include "host_key_dialog.h"

CHostKeyDialog::CHostKeyDialog(HWND parent, const std::wstring& host, const std::wstring& keyType,
                               const std::wstring& fingerprint, bool changed)
    : CCenteredDialog(HLanguage, IDD_HOSTKEY, IDH_HOST_KEY, parent), host_(host), keyType_(keyType), fingerprint_(fingerprint),
      changed_(changed)
{
}

CHostKeyDialog::~CHostKeyDialog()
{
    if (boldFont_ != nullptr)
        DeleteObject(boldFont_);
}

HostKeyDecision CHostKeyDialog::Run()
{
    try
    {
        switch (Execute())
        {
        case IDB_HOSTKEY_SAVE:
            return HostKeyDecision::AcceptAndSave;
        case IDB_HOSTKEY_ONCE:
            return HostKeyDecision::AcceptOnce;
        default: // IDCANCEL, or -1 when the dialog could not be created
            return HostKeyDecision::Reject;
        }
    }
    catch (...)
    {
        return HostKeyDecision::Reject;
    }
}

void CHostKeyDialog::Transfer(CTransferInfo& ti)
{
    // read-only edits: the values go to the window only (Cancel and the other buttons never read them back)
    ti.EditLine(IDE_HOSTKEY_HOST, host_, FALSE);
    ti.EditLine(IDE_HOSTKEY_TYPE, keyType_, FALSE);
    ti.EditLine(IDE_HOSTKEY_FINGERPRINT, fingerprint_, FALSE);
}

void CHostKeyDialog::InitControls()
{
    SetWindowTextW(HWindow, LangStr(changed_ ? IDS_HOSTKEY_TITLE_CHANGED : IDS_HOSTKEY_TITLE_NEW).c_str());
    SetDlgItemTextW(HWindow, IDC_HOSTKEY_MESSAGE,
                    LangStr(changed_ ? IDS_HOSTKEY_TEXT_CHANGED : IDS_HOSTKEY_TEXT_NEW).c_str());
    SetDlgItemTextW(HWindow, IDB_HOSTKEY_SAVE,
                    LangStr(changed_ ? IDS_HOSTKEY_SAVE_CHANGED : IDS_HOSTKEY_SAVE_NEW).c_str());
    // Cancel is the default button with the focus for every key (the safe answer: Enter never trusts a server)
    SendMessageW(HWindow, DM_SETDEFID, IDCANCEL, 0);
    SetFocus(GetDlgItem(HWindow, IDCANCEL)); // also: not the first read-only edit
    if (!changed_)
        return;

    // changed key: bold warning
    HWND message = GetDlgItem(HWindow, IDC_HOSTKEY_MESSAGE);
    HFONT font = (HFONT)SendMessageW(message, WM_GETFONT, 0, 0);
    LOGFONTW lf;
    if (font != nullptr && GetObjectW(font, sizeof(lf), &lf) != 0)
    {
        lf.lfWeight = FW_BOLD;
        boldFont_ = CreateFontIndirectW(&lf);
        if (boldFont_ != nullptr)
            SendMessageW(message, WM_SETFONT, (WPARAM)boldFont_, TRUE);
    }
}

INT_PTR CHostKeyDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        CCenteredDialog::DialogProc(uMsg, wParam, lParam); // transfers the values
        InitControls();
        return FALSE; // the focus was set by hand
    }

    case WM_CTLCOLORSTATIC:
    {
        if (changed_ && (HWND)lParam == GetDlgItem(HWindow, IDC_HOSTKEY_MESSAGE))
        {
            HDC dc = (HDC)wParam;
            // the shared dark mode code returns its brush (and colors) in dark mode and nothing in light mode
            INT_PTR brush = CCenteredDialog::DialogProc(uMsg, wParam, lParam);
            if (brush != 0)
            {
                SetTextColor(dc, RGB(255, 110, 110)); // readable on the dark background
                return brush;
            }
            SetTextColor(dc, RGB(192, 0, 0));
            SetBkMode(dc, TRANSPARENT);
            return (INT_PTR)GetSysColorBrush(COLOR_3DFACE);
        }
        break;
    }

    case WM_COMMAND:
    {
        const int id = LOWORD(wParam);
        if (id == IDB_HOSTKEY_SAVE || id == IDB_HOSTKEY_ONCE)
        {
            EndDialog(HWindow, id);
            return TRUE;
        }
        break;
    }
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}
