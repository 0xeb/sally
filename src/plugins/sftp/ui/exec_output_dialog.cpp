#include "precomp.h"

#include "dark_controls.h"
#include "exec_output_dialog.h"

CExecOutputDialog::CExecOutputDialog(HWND parent, std::wstring editText, std::wstring statusText)
    : CCenteredDialog(HLanguage, IDD_EXEC_OUTPUT, parent), text_(std::move(editText)), status_(std::move(statusText))
{
}

void CExecOutputDialog::Run()
{
    try
    {
        Execute();
    }
    catch (...)
    {
        TRACE_E("CExecOutputDialog: unexpected exception.");
    }
}

CExecOutputDialog::~CExecOutputDialog()
{
    if (font_ != nullptr)
        DeleteObject(font_); // also when Execute failed before WM_DESTROY
}

// Same size and weight as the dialog font, but a fixed-width face so columns of command output line up.
// Consolas if the system has it (CreateFont never fails, it silently substitutes), else Courier New.
void CExecOutputDialog::SetFixedFont(HWND edit)
{
    LOGFONTW lf = {};
    if (GetObjectW(reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0)), sizeof(lf), &lf) == 0)
        return; // keep the dialog font
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    const wchar_t* faces[] = {L"Consolas", L"Courier New"};
    HDC dc = GetDC(edit);
    for (const wchar_t* face : faces)
    {
        wcscpy_s(lf.lfFaceName, face);
        HFONT font = CreateFontIndirectW(&lf);
        if (font == nullptr)
            continue;
        // the face of the font that Windows really selected (a missing face is replaced by another one)
        HGDIOBJ old = SelectObject(dc, font);
        wchar_t actual[LF_FACESIZE] = {};
        GetTextFaceW(dc, LF_FACESIZE, actual);
        SelectObject(dc, old);
        if (_wcsicmp(actual, face) == 0 || face == faces[1])
        {
            font_ = font;
            break;
        }
        DeleteObject(font);
    }
    ReleaseDC(edit, dc);
    if (font_ != nullptr)
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
}

void CExecOutputDialog::InitControls()
{
    ApplyDarkButtons(HWindow);
    HWND edit = GetDlgItem(HWindow, IDE_EXEC_OUTPUT);
    // an edit control holds 32 KB by default; the output can be 1 MB (0 = the largest the system allows)
    SendMessageW(edit, EM_LIMITTEXT, 0, 0);
    SetFixedFont(edit);
    SetWindowTextW(edit, text_.c_str());
    SetDlgItemTextW(HWindow, IDC_EXEC_STATUS, status_.c_str());
    SetFocus(GetDlgItem(HWindow, IDCANCEL)); // not the edit: it would select all the text
}

void CExecOutputDialog::CopyToClipboard()
{
    SalamanderGeneral->CopyTextToClipboard(text_.c_str(), static_cast<int>(text_.size()), TRUE, HWindow);
}

INT_PTR CExecOutputDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    try
    {
        if (uMsg == WM_COMMAND && LOWORD(wParam) == IDB_EXEC_COPY && HIWORD(wParam) == BN_CLICKED)
        {
            CopyToClipboard();
            return TRUE;
        }
        const INT_PTR result = CCenteredDialog::DialogProc(uMsg, wParam, lParam);
        if (uMsg == WM_INITDIALOG)
        {
            InitControls();
            return FALSE; // the focus was set by hand
        }
        return result;
    }
    catch (...)
    {
        TRACE_E("CExecOutputDialog: unexpected exception in a window message.");
    }
    return FALSE;
}
