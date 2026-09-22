// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <vector>

#include "mmviewer.rh"
#include "mmviewer.rh2"
#include "lang\lang.rh"
#include "output.h"
#include "renderer.h"
//#include "dialogs.h"
#include "mmviewer.h"
#include "parser.h"
#include "unicode/helpers.h"

#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))

//****************************************************************************
//
// CRendererWindow
//

CRendererWindow::CRendererWindow(int enumFilesSourceUID, int enumFilesCurrentIndex)
    : CWindow(ooStatic)
{
    FileName.clear();
    Viewer = NULL;
    Creating = TRUE;
    EnumFilesSourceUID = enumFilesSourceUID;
    EnumFilesCurrentIndex = enumFilesCurrentIndex;
}

CRendererWindow::~CRendererWindow()
{
}

void CRendererWindow::OnFileOpen()
{
    std::wstring fileName;
    if (ShowOpenFileDialog(HWindow, NULL, LangStr(IDS_VIEWERFILTER).c_str(), fileName, NULL, FALSE))
        OpenFile(fileName.c_str());
}

BOOL CRendererWindow::OpenFile(const wchar_t* name)
{
    // if an error occurs during OpenFile, the background will be repainted
    InvalidateRect(HWindow, NULL, TRUE);

    Output.DestroyItems();

    CParserInterface* parser;
    CParserResultEnum result = CreateAppropriateParser(name, &parser);
    if (result == preOK)
    {
        const wchar_t* oFName = name;
        const wchar_t* tmp = wcsrchr(name, L'\\');
        if (tmp)
            oFName = tmp + 1;

        Output.AddHeader(oFName, TRUE);
        Output.AddSeparator();

        parser->GetFileInfo(&Output);
        parser->CloseFile();
        delete (parser);
        Output.PrepareForRender(HWindow);

        FileName = name;
    }
    else
    {
        const std::wstring errorText = SPLFormatStringOwned(
            SPLLoadStrOwned(SalGeneral, HLanguage, IDS_ERROR_OPENING).c_str(), name);
        SalGeneral->SalMessageBox(HWindow, errorText.c_str(), SPLLoadStrOwned(SalGeneral, HLanguage, IDS_PLUGIN_NAME).c_str(), MB_ICONEXCLAMATION);

        FileName.clear();
    }

    SetViewerTitle();

    HDC hDC;
    if ((hDC = GetDC(HWindow)) != NULL)
    {
        int headerW = ComputeExtents(hDC, sLeft, FALSE, TRUE);
        ComputeExtents(hDC, sRight, TRUE);

        // do not make it wider; it's hard to read
        if (sRight.cx > 800)
            sRight.cx = 800;

        if (sLeft.cx + sRight.cx < headerW)
            sRight.cx = headerW - sLeft.cx;

        width = sLeft.cx + sRight.cx;
        height = sRight.cy;

        TRACE_I("MMV: sRight.cx: " << sRight.cx << ", sLeft.cx: " << sLeft.cx);
        ReleaseDC(HWindow, hDC);
    }

    Paint(hDC, TRUE, 0);
    Paint(hDC, TRUE, 0xFFFFFFFF);
    SetupScrollBars();
    InvalidateRect(HWindow, NULL, TRUE);
    PostMessage(HWindow, WM_SIZE, 0, 0);

    Viewer->UpdateEnablers();

    return (result == preOK);
}

void CRendererWindow::SetupScrollBars()
{
    RECT r;
    GetClientRect(HWindow, &r);

    SCROLLINFO si;
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_PAGE | SIF_RANGE;
    si.nMin = 0;
    si.nMax = width;
    si.nPage = r.right;
    SetScrollInfo(HWindow, SB_HORZ, &si, TRUE);

    si.nMax = height;
    si.nPage = r.bottom;
    SetScrollInfo(HWindow, SB_VERT, &si, TRUE);
}

void CRendererWindow::SetViewerTitle()
{
    const std::wstring title = FileName.empty()
                                   ? LangStr(IDS_PLUGIN_NAME).c_str()
                                   : FileName + L" - " + LangStr(IDS_PLUGIN_NAME).c_str();
    SetWindowTextW(GetParent(HWindow), title.c_str());
}

LRESULT CRendererWindow::OnCommand(WPARAM wParam, LPARAM lParam)
{
    switch (LOWORD(wParam))
    {
    case CM_COPY:
    {
        TDirectArray<wchar_t> text(4096, 4096);

        int i;
        for (i = 0; i < Output.GetCount(); i++)
        {
            const COutputItem* item = Output.GetItem(i);

            if (item->Name)
            {
                // The label is already wide - no conversion on this side.
                text.Add(item->Name, (int)wcslen(item->Name));
                if (item->Value)
                {
                    text.Add('\t');
                    text.Add(item->DisplayValue, (int)wcslen(item->DisplayValue));
                }
            }

            text.Add(L"\r\n", 2);
        }
        text.Add('\0');
        SalGeneral->CopyTextToClipboard(&text[0], -1, FALSE, NULL);
    }
    break;

    case CM_FILE_FIRST:
    case CM_FILE_PREV:
    case CM_FILE_NEXT:
    case CM_FILE_LAST:
    {
        BOOL ok = FALSE;
        BOOL srcBusy = FALSE;
        BOOL noMoreFiles = FALSE;
        std::wstring fileName;
        int enumFilesCurrentIndex = EnumFilesCurrentIndex;
        if (LOWORD(wParam) == CM_FILE_PREV || LOWORD(wParam) == CM_FILE_LAST)
        {
            if (LOWORD(wParam) == CM_FILE_LAST)
                enumFilesCurrentIndex = -1;
            ok = SPLGetAdjacentFileNameForViewerOwned(SalGeneral, TRUE, EnumFilesSourceUID,
                                                   &enumFilesCurrentIndex, FileName.c_str(),
                                                   FALSE, TRUE,
                                                   fileName, &noMoreFiles, &srcBusy);
        }
        else
        {
            if (LOWORD(wParam) == CM_FILE_FIRST)
                enumFilesCurrentIndex = -1;
            ok = SPLGetAdjacentFileNameForViewerOwned(SalGeneral, FALSE, EnumFilesSourceUID,
                                                   &enumFilesCurrentIndex, FileName.c_str(),
                                                   FALSE, TRUE,
                                                   fileName, &noMoreFiles, &srcBusy);
        }

        if (ok) // we have a new name
        {
            if (lstrcmpiW(fileName.c_str(), FileName.c_str()) != 0)
            {
                if (Viewer->Lock != NULL)
                {
                    SetEvent(Viewer->Lock);
                    Viewer->Lock = NULL; // now it's just up to the disk cache
                }
                if (!OpenFile(fileName.c_str()))
                {
                    FileName.clear();
                }

                // set the index even if it fails so the user can move to the next/previous image
                EnumFilesCurrentIndex = enumFilesCurrentIndex;
            }
        }
        else
            int fixme = 0;
        return 0;
    }

    case CM_FILES_EXPORT_HTML:
    {
        std::wstring fname = FileName;
        const std::wstring extOwner = LangStr(IDS_HTMLEXT);
        const wchar_t* ext = extOwner.c_str();
        const size_t dot = fname.find_last_of(L'.'); // ".cvspass" is an extension in Windows
        if (dot != std::wstring::npos)
            fname.resize(dot);
        fname += ext;

        if (ShowOpenFileDialog(HWindow, NULL, LangStr(IDS_HTMLEXPFILTER).c_str(), fname, ext, TRUE))
        {
            int r;
            if ((r = ExportToHTML(fname.c_str(), Output)) > 0)
            {
                if (SalGeneral->SalMessageBox(HWindow, SPLLoadStrOwned(SalGeneral, HLanguage, IDS_EXPORTOPEN).c_str(), SPLLoadStrOwned(SalGeneral, HLanguage, IDS_PLUGIN_NAME).c_str(), MB_YESNO | MB_ICONQUESTION) == DIALOG_YES)
                    ExecuteFile(fname.c_str());
                //SalGeneral->ExecuteAssociation(HWindow, NULL, fname);
            }
            else
            {
                switch (r)
                {
                case -1:
                case -2:
                default: // for now treat all errors as write errors
                    SalGeneral->SalMessageBox(HWindow, SPLLoadStrOwned(SalGeneral, HLanguage, IDS_MMV_WRITE_ERROR).c_str(), SPLLoadStrOwned(SalGeneral, HLanguage, IDS_PLUGIN_NAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
                    break;
                }
            }
        }
    }
        return 0;

    case CM_FILES_EXPORT_XML:
    {
        std::wstring fname = FileName;
        const size_t dot = fname.find_last_of(L'.'); // ".cvspass" is an extension in Windows
        if (dot != std::wstring::npos)
            fname.resize(dot);
        fname += L".xml";

        //TODO
    }
        return 0;

    case CM_FILES_OPEN:
    {
        OnFileOpen();
        return 0;
    }

    case CM_ABOUT:
    {
        MMViewerAbout(HWindow);
        return 0;
    }

    case CM_EXIT:
    {
        DestroyWindow(GetParent(HWindow));
        return 0;
    }

    case CM_FULLSCREEN:
    {
        WPARAM wParam2;
        if (IsZoomed(GetParent(HWindow)))
            wParam2 = SC_RESTORE;
        else
            wParam2 = SC_MAXIMIZE;
        SendMessageW(GetParent(HWindow), WM_SYSCOMMAND, wParam2, 0);
        return 0;
    }
    }
    return CWindow::WindowProc(WM_COMMAND, wParam, lParam);
} /* CRendererWindow::OnCommand */

LRESULT
CRendererWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    {
        switch (wParam)
        {
        case VK_TAB:
        {
            if (Output.GetCount() > 1)
            {
                HWND h = NULL;

                if (KEY_DOWN(VK_SHIFT))
                {
                    // find the first edit control from the back
                    int i;
                    for (i = Output.GetCount(); i--;)
                        if (Output.GetItem(i)->hwnd)
                        {
                            h = Output.GetItem(i)->hwnd;
                            break;
                        }
                }
                else
                {
                    // find the first edit control from the front
                    int i;
                    for (i = 0; i < Output.GetCount(); i++)
                        if (Output.GetItem(i)->hwnd)
                        {
                            h = Output.GetItem(i)->hwnd;
                            break;
                        }
                }

                SetFocus(h);
                SendMessageW(h, EM_SETSEL, 0, -1);
                SendMessageW(h, WM_ENSUREVISIBLE, 0, 0);
                TRACE_I("CRendererWindow::WindowProc: VK_TAB: " << h);
                return 0;
            }
        }
        break;
        }

        WORD wScrollNotify = -1;

        switch (wParam)
        {
        case VK_ESCAPE:
            PostMessage(GetParent(HWindow), WM_CLOSE, wParam, lParam);
            break;

        case VK_UP:
            wScrollNotify = SB_LINEUP;
            break;

        case VK_PRIOR:
            wScrollNotify = SB_PAGEUP;
            break;

        case VK_NEXT:
            wScrollNotify = SB_PAGEDOWN;
            break;

        case VK_DOWN:
            wScrollNotify = SB_LINEDOWN;
            break;

        case VK_HOME:
            wScrollNotify = SB_TOP;
            break;

        case VK_END:
            wScrollNotify = SB_BOTTOM;
            break;
        }

        if (wScrollNotify != -1)
            SendMessageW(HWindow, WM_VSCROLL, MAKELONG(wScrollNotify, 0), 0L);

        wScrollNotify = -1;

        switch (wParam)
        {
        case VK_LEFT:
            wScrollNotify = (KEY_DOWN(VK_CONTROL)) ? SB_PAGELEFT : SB_LINELEFT;
            break;

        case VK_RIGHT:
            wScrollNotify = (KEY_DOWN(VK_CONTROL)) ? SB_PAGERIGHT : SB_LINERIGHT;
            break;

        case VK_HOME:
        case VK_END:
            wScrollNotify = SB_LEFT;
            break;
        }

        if (wScrollNotify != -1)
            SendMessageW(HWindow, WM_HSCROLL, MAKELONG(wScrollNotify, 0), 0L);

        break;
    }
        /*
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:*/
    case WM_MOUSEACTIVATE:
        SetFocus(HWindow);
        break;

    case WM_COMMAND:
    {
        LRESULT res = OnCommand(wParam, lParam);
        Viewer->UpdateEnablers();
        return res;
    }

    case WM_ERASEBKGND:
    {
        if (Output.GetCount())
            return 1;
        // Let Windows erase the bground for us if we have nothing to show...
        break;
    }

    case WM_HSCROLL:
    {
        SCROLLINFO si;

        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;

        GetScrollInfo(HWindow, SB_HORZ, &si);

        switch (LOWORD(wParam))
        {
        case SB_LEFT:
            si.nPos = 0;
            break;

        case SB_LINELEFT:
            si.nPos -= (FontHeight + 1);
            break;

        case SB_LINERIGHT:
            si.nPos += (FontHeight + 1);
            break;

        case SB_PAGELEFT:
            si.nPos -= si.nPage;
            break;

        case SB_PAGERIGHT:
            si.nPos += si.nPage;
            break;

        case SB_THUMBTRACK:
            si.nPos = si.nTrackPos;
            break;

        default:
            break;
        }

        si.fMask = SIF_POS;
        SetScrollInfo(HWindow, SB_HORZ, &si, TRUE);
        GetScrollInfo(HWindow, SB_HORZ, &si);

        InvalidateRect(HWindow, NULL, TRUE);
        Paint(NULL, TRUE); // shift the edit boxes
    }
        return 0;

    case WM_MOUSEWHEEL:
    case WM_VSCROLL:
    {
        SCROLLINFO si;

        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(HWindow, SB_VERT, &si);

        if (uMsg == WM_MOUSEWHEEL)
        {
            int zDelta = ((short)HIWORD(wParam));

            wParam = (zDelta < 0) ? SB_LINEDOWN : SB_LINEUP;
        }

        switch (LOWORD(wParam))
        {
        case SB_TOP:
            si.nPos = si.nMin;
            break;

        case SB_BOTTOM:
            si.nPos = si.nMax;
            break;

        case SB_LINEUP:
            si.nPos -= (FontHeight + 1);
            break;

        case SB_LINEDOWN:
            si.nPos += (FontHeight + 1);
            break;

        case SB_PAGEUP:
            si.nPos -= si.nPage;
            break;

        case SB_PAGEDOWN:
            si.nPos += si.nPage;
            break;

        case SB_THUMBTRACK:
            si.nPos = si.nTrackPos;
            break;

        default:
            break;
        }

        si.fMask = SIF_POS;
        SetScrollInfo(HWindow, SB_VERT, &si, TRUE);
        GetScrollInfo(HWindow, SB_VERT, &si);

        InvalidateRect(HWindow, NULL, TRUE);
        Paint(NULL, TRUE); // shift the edit boxes
    }
        return 0;

    case WM_SIZE:
        SetupScrollBars();
        Paint(NULL, TRUE); // shift the edit boxes
        InvalidateRect(HWindow, NULL, TRUE);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hDC = BeginPaint(HWindow, &ps);
        if (hDC != NULL)
            Paint(hDC, FALSE);
        EndPaint(HWindow, &ps);
        return 0;
    }

    case WM_ENSUREVISIBLE:
    {
#define OFSX(ofs) \
    { \
        xPos += ofs; \
        r.left = rr.left; \
        r.right = rr.right; \
        OffsetRect(&r, -xPos, 0); \
    }
#define OFSY(ofs) \
    { \
        yPos += ofs; \
        r.top = rr.top; \
        r.bottom = rr.bottom; \
        OffsetRect(&r, 0, -yPos); \
    }

        RECT r, rr, fr;

        fr = *(RECT*)lParam;
        GetClientRect(HWindow, &r);
        rr = r;

        SCROLLINFO si;
        si.cbSize = sizeof(si);
        si.fMask = SIF_POS;
        GetScrollInfo(HWindow, SB_VERT, &si);
        int yPos = si.nPos;
        GetScrollInfo(HWindow, SB_HORZ, &si);
        int xPos = si.nPos;

        OffsetRect(&r, -xPos, -yPos);
        OffsetRect(&fr, -xPos, -yPos);

        TRACE_I("CRendererWindow::WindowProc:WM_ENSUREVISIBLE (" << fr.left << ", " << fr.top << ", " << fr.right << ", " << fr.bottom << ") ->(" << r.left << ", " << r.top << ", " << r.right << ", " << r.bottom << ")");
        /*
      if (fr.right > r.right)
        OFSX(fr.right - r.right)*/

        if (fr.left < r.left)
            OFSX(fr.left - r.left)
        /*else
        if (fr.left > r.right)
          OFSX(fr.left - r.left)*/

        if (fr.top < r.top)
            OFSY(fr.top - r.top)
        else if (fr.bottom > r.bottom)
            OFSY(fr.bottom - r.bottom);

        si.fMask = SIF_POS;
        si.nPos = yPos;
        SetScrollInfo(HWindow, SB_VERT, &si, TRUE);
        si.nPos = xPos;
        SetScrollInfo(HWindow, SB_HORZ, &si, TRUE);

        InvalidateRect(HWindow, NULL, TRUE);
        Paint(NULL, TRUE); // shift the edit boxes
    }
        return 0;
    }

    return CWindow::WindowProc(uMsg, wParam, lParam);
}
