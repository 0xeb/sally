// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "plugin_window_text.h"
#include "regedt_find_pattern.h"
#include "regedt_registry_enum.h"

std::vector<std::wstring> PatternHistory;
std::vector<std::wstring> LookInHistory;

#ifdef _DEBUG
// statistics of value type frequency
DWORD TypeStat[12];
#endif

//*********************************************************************************
//
// CStatusBar
//

CStatusBar::CStatusBar()
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CStatusBar::CStatusBar()");
    BaseLen = 0;
    Dirty = 0;
    Width = 0;
    TextWidth = 0;
    Height = 0;
    HBitmap = NULL;
}

CStatusBar::~CStatusBar()
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CStatusBar::~CStatusBar()");
    if (HBitmap != NULL)
        DeleteObject(HBitmap);
}

void CStatusBar::AllocateBitmap()
{
    CALL_STACK_MESSAGE1("CStatusBar::AllocateBitmap()");
    HDC screenDC = GetDC(HWindow);
    if (screenDC != NULL)
    {
        if (HBitmap != NULL)
            DeleteObject(HBitmap);
        HBitmap = (HBITMAP)CreateCompatibleBitmap(screenDC, Width, Height);
        // pre-draw the frame and sizing grip into the bitmap
        HDC dc = CreateCompatibleDC(screenDC);
        if (dc)
        {
            HBITMAP oldBmp = (HBITMAP)SelectObject(dc, HBitmap);
            RECT r;
            SetRect(&r, 0, 0, Width, Height);

            DrawEdge(dc, &r, BDR_SUNKENOUTER, BF_RECT);

            r.top++;
            r.bottom--;
            r.left = TextWidth;
            r.right--;

            DrawFrameControl(dc, &r, DFC_SCROLL, DFCS_SCROLLSIZEGRIP);
            HPEN pen = CreatePen(PS_SOLID, 0, GetSysColor(COLOR_BTNFACE));
            HPEN oldPen = (HPEN)SelectObject(dc, pen);
            MoveToEx(dc, r.left + 2, r.bottom, NULL);
            LineTo(dc, r.right, r.bottom);
            LineTo(dc, r.right, r.bottom - GetSystemMetrics(SM_CYVSCROLL) + 3);
            SelectObject(dc, oldPen);
            DeleteObject(pen);

            SelectObject(dc, oldBmp);
            DeleteDC(dc);
        }
        ReleaseDC(HWindow, screenDC);
    }
}

void CStatusBar::SetBase(LPCWSTR text, BOOL updateInIdle)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("CStatusBar::SetBase(, %d)", updateInIdle);
    std::wstring staged;
    try
    {
        staged = text ? text : L"";
    }
    catch (...)
    {
        return;
    }
    Section.Enter();
    Text.swap(staged);
    BaseLen = Text.size();
    // do not wait for a repaint
    if (!Dirty)
    {
        if (updateInIdle)
            UpdateInIdle = TRUE;
        else
        {
            Dirty = TRUE;
            RECT r;
            SetRect(&r, 1, 1, TextWidth, Height - 1);
            InvalidateRect(HWindow, &r, FALSE);
            PostMessage(HWindow, WM_PAINT, 0, 0);
        }
    }
    Section.Leave();
}

void CStatusBar::Set(LPCWSTR text, BOOL updateInIdle)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("CStatusBar::Set(, %d)", updateInIdle);
    Section.Enter();
    try
    {
        Text.resize(BaseLen);
        if (text)
            Text.append(text);
    }
    catch (...)
    {
        Section.Leave();
        return;
    }
    // do not wait for a repaint
    if (!Dirty)
    {
        if (updateInIdle)
            UpdateInIdle = TRUE;
        else
        {
            Dirty = TRUE;
            RECT r;
            SetRect(&r, 1, 1, TextWidth, Height - 1);
            InvalidateRect(HWindow, &r, FALSE);
            PostMessage(HWindow, WM_PAINT, 0, 0);
        }
    }
    Section.Leave();
}

void CStatusBar::OnEnterIdle()
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CStatusBar::OnEnterIdle()");
    Section.Enter();
    // should we perform the update while idle?
    if (UpdateInIdle)
    {
        UpdateInIdle = FALSE;
        // do not wait for a repaint
        if (!Dirty)
        {
            Dirty = TRUE;
            RECT r;
            SetRect(&r, 1, 1, TextWidth, Height - 1);
            InvalidateRect(HWindow, &r, FALSE);
            PostMessage(HWindow, WM_PAINT, 0, 0);
        }
    }
    Section.Leave();
}

LRESULT
CStatusBar::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CStatusBar::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam,
                        lParam);
    switch (uMsg)
    {
    case WM_SIZE:
    {
        Width = LOWORD(lParam);
        TextWidth = Width - GetSystemMetrics(SM_CXHSCROLL) + 1;
        Height = HIWORD(lParam);
        AllocateBitmap();
        return 0;
    }

    case WM_NCHITTEST:
    {
        RECT r;
        POINT pt;
        pt.x = TextWidth;
        pt.y = 0; //Height - GetSystemMetrics(SM_CYVSCROLL);
        ClientToScreen(HWindow, &pt);
        ;
        r.left = pt.x;
        r.top = pt.y;
        pt.x = Width;
        pt.y = Height;
        ClientToScreen(HWindow, &pt);
        ;
        r.right = pt.x;
        r.bottom = pt.y;

        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);
        if (PtInRect(&r, pt))
            return HTBOTTOMRIGHT;
        break;
    }

    case WM_ERASEBKGND:
    {
        return TRUE;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(HWindow, &ps);
        if (HBitmap)
        {
            HDC dc = CreateCompatibleDC(ps.hdc);
            if (dc)
            {
                HBITMAP oldBmp = (HBITMAP)SelectObject(dc, HBitmap);

                HFONT oldFont = (HFONT)SelectObject(dc, EnvFont);
                SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
                SetBkColor(dc, GetSysColor(COLOR_BTNFACE));

                Section.Enter();
                RECT r;
                SetRect(&r, 1, 1, TextWidth, Height - 1);
                ExtTextOutW(dc, 2, (r.top + r.bottom - EnvFontHeight) / 2, ETO_OPAQUE | ETO_CLIPPED, &r, Text.c_str(), static_cast<UINT>(Text.size()), NULL);
                Dirty = FALSE;
                Section.Leave();

                SelectObject(dc, oldFont);

                BitBlt(ps.hdc, 0, 0, Width, Height, dc, 0, 0, SRCCOPY);
                SelectObject(dc, oldBmp);
                DeleteDC(dc);
            }
        }

        EndPaint(HWindow, &ps);

        return 0;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}

//*********************************************************************************
//
// CFoundFilesData
//

LPCWSTR Bin2ASCIIW = L"0123456789abcdef";

void PrintHexValueW(unsigned char* data, int size, LPWSTR buffer, int bufSize)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE3("PrintHexValueW(, %d, , %d)", size, bufSize);
    int i;
    for (i = 0; i < min(size, (bufSize) / 3); i++)
    {
        buffer[i * 3] = Bin2ASCIIW[data[i] >> 4];
        buffer[i * 3 + 1] = Bin2ASCIIW[data[i] & 0x0F];
        if (i + 1 < min(size, (bufSize) / 3))
            buffer[i * 3 + 2] = L' ';
    }
    if (i != size)
    {
        // if it does not fit, append an ellipsis
        wcscpy(buffer + bufSize - 4, L"...");
    }
    else
        buffer[i * 3 - 1] = L'\0';
}

CFoundFilesData::CFoundFilesData(LPWSTR name, int root, LPWSTR key, DWORD type,
                                 DWORD size, unsigned char* data,
                                 FILETIME time, BOOL isDir)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE5("CFoundFilesData::CFoundFilesData(, %d, , 0x%X, 0x%X, , , "
    //                      "%d)", root, type, size, isDir);
    Name = *name == L'\0' ? LoadStrW(IDS_DEFAULTVALUE) : name;
    Path = PredefinedHKeys[root].KeyName;
    if (key && *key)
    {
        if (!Path.empty() && Path.back() != L'\\')
            Path.push_back(L'\\');
        Path.append(key);
    }
    Type = type;
    Size = size;
    Data = NULL;
    Allocated = 0;
    Time = time;
    IsDir = isDir;
    Default = *name == L'\0' ? 1 : 0;

    if (!IsDir && data)
    {
        // adjust the data for display
        switch (type)
        {
        case REG_MULTI_SZ:
        {
            // replace separator NULL characters with spaces
            WCHAR* ptr = (WCHAR*)data;
            while (ptr < (WCHAR*)data + min(size / 2, MAX_DATASIZE))
            {
                if (*ptr == L'\0')
                {
                    if (ptr + 2 >= (WCHAR*)data + size / 2)
                    {
                        size -= 2; // do not count the last '\0'; there are two of them
                        break;
                    }
                    else
                    {
                        *ptr = L' ';
                    }
                }
                ptr++;
            }
            // keep going
        }
        case REG_EXPAND_SZ:
        case REG_SZ:
        {
            DWORD truncated = min(size, MAX_DATASIZE * 2);
            if (size)
            {
                Allocated = 1;
                Data = (DWORD_PTR)malloc(truncated);
                memcpy((void*)Data, data, truncated);
                if (truncated < size)
                    wcscpy((LPWSTR)Data + truncated / 2 - 4, L"...");
            }
            break;
        }

        case REG_QWORD:
            if (size > 0)
            {
                Allocated = 1;
                Data = (DWORD_PTR)malloc(8);
                memcpy((void*)Data, data, 8);
            }
            break;

        case REG_DWORD:
            Data = *(LPDWORD)data;
            break;

        case REG_DWORD_BIG_ENDIAN:
            Data = (DWORD)data[3] | (data[2] << 8) | (data[1] << 16) | (data[0] << 24);
            break;

        default:
            if (size > 0)
            {
                Allocated = 1;
                int hexsize = (unsigned char)min(size * 3 - 1, MAX_DATASIZE) + 1;
                Data = (DWORD_PTR)malloc(hexsize * 2);
                PrintHexValueW(data, size, (LPWSTR)Data, hexsize);
            }
        }
    }
}

LPCWSTR
CFoundFilesData::GetText(int i, std::wstring& buffer)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("CFoundFilesData::GetText(%d, )", i);
    static wchar_t emptyBuffer[] = L"";
    LPCWSTR ret = emptyBuffer; //"?";
    switch (i)
    {
    case CI_NAME:
        ret = Name.c_str();
        break;
    case CI_PATH:
        ret = Path.c_str();
        break;

    case CI_SIZE:
    {
        if (IsDir)
        {
            buffer = KeyText;
            ret = buffer.c_str();
        }
        else
        {
            buffer = SPLNumberToStrOwned(SG, CQuadWord().Set(Size, 0));
            ret = buffer.c_str();
        }
        break;
    }

    case CI_DATE:
    {
        SYSTEMTIME st;
        FileTimeToSystemTime(&Time, &st);
        buffer = SPLFormatStringOwned(L"%u.%u.%u", st.wDay, st.wMonth, st.wYear);
        ret = buffer.c_str();
        break;
    }

    case CI_TIME:
    {
        SYSTEMTIME st;
        FileTimeToSystemTime(&Time, &st);
        buffer = SPLFormatStringOwned(L"%u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
        ret = buffer.c_str();
        break;
    }

    case CI_TYPE:
    {
        if (!IsDir)
        {
            // if it is a value, output its type
            switch (Type)
            {
            case REG_BINARY:
                buffer = Str_REG_BINARY;
                break;
            case REG_DWORD:
                buffer = Str_REG_DWORD;
                break;
            case REG_DWORD_BIG_ENDIAN:
                buffer = Str_REG_DWORD_BIG_ENDIAN;
                break;
            case REG_QWORD:
                buffer = Str_REG_QWORD;
                break;
            case REG_EXPAND_SZ:
                buffer = Str_REG_EXPAND_SZ;
                break;
            case REG_LINK:
                buffer = Str_REG_LINK;
                break;
            case REG_MULTI_SZ:
                buffer = Str_REG_MULTI_SZ;
                break;
            case REG_NONE:
                buffer = Str_REG_NONE;
                break;
            case REG_RESOURCE_LIST:
                buffer = Str_REG_RESOURCE_LIST;
                break;
            case REG_SZ:
                buffer = Str_REG_SZ;
                break;
            case REG_FULL_RESOURCE_DESCRIPTOR:
                buffer = Str_REG_FULL_RESOURCE_DESCRIPTOR;
                break;
            case REG_RESOURCE_REQUIREMENTS_LIST:
                buffer = Str_REG_RESOURCE_REQUIREMENTS_LIST;
                break;
            default:
                TRACE_E("unknown value type");
                buffer = L"?";
            }
            ret = buffer.c_str();
        }
        break;
    }

    case CI_DATA:
    {
        if (!IsDir)
        {
            if (Size > 0)
            {
                switch (Type)
                {
                case REG_MULTI_SZ:
                case REG_EXPAND_SZ:
                case REG_SZ:
                    if (Data)
                        ret = (LPWSTR)Data;
                    break;

                case REG_DWORD_BIG_ENDIAN:
                case REG_DWORD:
                    if (Size == 4)
                    {
                        buffer = SPLFormatStringOwned(L"0x%08x (%u)", Data, Data);
                        ret = buffer.c_str();
                    }
                    break;

                case REG_QWORD:
                    if (Size == 8)
                    {
                        buffer = SPLFormatStringOwned(L"0x%016I64x (%I64u)", *(LPQWORD)Data, *(LPQWORD)Data);
                        ret = buffer.c_str();
                    }
                    break;

                default:
                    if (Data)
                        ret = (LPWSTR)Data;
                }
            }
        }
        break;
    }
    }

    return ret;
}

//*********************************************************************************
//
// CFoundFilesListView
//

CFoundFilesListView::CFoundFilesListView(CFindDialog* searchDialog)
    : Data(100, 100)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CFoundFilesListView::CFoundFilesListView()");
    SearchDialog = searchDialog;
}

CFoundFilesListView::~CFoundFilesListView()
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CFoundFilesListView::~CFoundFilesListView()");
}

void CFoundFilesListView::StoreItemsState()
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CFoundFilesListView::StoreItemsState()");
    int count = GetCount();
    int i;
    for (i = 0; i < count; i++)
        Data[i]->State = ListView_GetItemState(HWindow, i, LVIS_FOCUSED | LVIS_SELECTED);
}

void CFoundFilesListView::RestoreItemsState()
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CFoundFilesListView::RestoreItemsState()");
    int count = GetCount();
    int i;
    for (i = 0; i < count; i++)
        ListView_SetItemState(HWindow, i, Data[i]->State, LVIS_FOCUSED | LVIS_SELECTED);
}

int CFoundFilesListView::CompareFunc(CFoundFilesData* f1, CFoundFilesData* f2, int sortBy)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("CFoundFilesListView::CompareFunc(, , %d)", sortBy);
    int res;
    int next = 0, sortByIndex;
    static int sortKeys[] = {CI_NAME, CI_PATH, CI_TYPE, CI_TIME, CI_SIZE};
    switch (sortBy)
    {
    case CI_NAME:
        next = 0;
        break;
    case CI_PATH:
        next = 1;
        break;
    case CI_TYPE:
        next = 2;
        break;
    case CI_DATE:
        next = 3;
        break;
    case CI_TIME:
        next = 3;
        break;
    case CI_SIZE:
        next = 4;
        break;
    }
    sortByIndex = next;
    do
    {
        switch (sortKeys[next])
        {
        case CI_NAME:
        {
            if (f1->IsDir == f2->IsDir)
            {
                res = CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, f1->Name.c_str(), -1, f2->Name.c_str(), -1) - 2;
                if (res == 0)
                    res = CompareStringW(LOCALE_USER_DEFAULT, 0, f1->Name.c_str(), -1, f2->Name.c_str(), -1) - 2;
            }
            else
                res = f1->IsDir ? -1 : 1;
            break;
        }

        case CI_PATH:
        {
            res = CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, f1->Path.c_str(), -1, f2->Path.c_str(), -1) - 2;
            if (res == 0)
                res = CompareStringW(LOCALE_USER_DEFAULT, 0, f1->Path.c_str(), -1, f2->Path.c_str(), -1) - 2;
            break;
        }

        case CI_TYPE:
        {
            if (f1->IsDir == f2->IsDir)
            {
                if (f1->IsDir)
                    res = 0;
                else
                {
                    if (f1->Type < f2->Type)
                        res = -1;
                    else
                    {
                        if (f1->Type == f2->Type)
                            res = 0;
                        else
                            res = 1;
                    }
                }
            }
            else
                res = f1->IsDir ? -1 : 1;
            break;
        }

        case CI_SIZE:
        {
            if (f1->IsDir == f2->IsDir)
            {
                if (f1->IsDir)
                    res = 0;
                else
                {
                    if (f1->Size < f2->Size)
                        res = -1;
                    else
                    {
                        if (f1->Size == f2->Size)
                            res = 0;
                        else
                            res = 1;
                    }
                }
            }
            else
                res = f1->IsDir ? -1 : 1;

            break;
        }

        case CI_TIME:
        {
            if (f1->IsDir == f2->IsDir)
                res = CompareFileTime(&f1->Time, &f2->Time);
            else
                res = f1->IsDir ? -1 : 1;
            break;
        }
        }
        if (next == sortByIndex)
        {
            if (sortBy != 0)
                next = 0;
            else
                next = 1;
        }
        else if (next + 1 != sortByIndex)
            next++;
        else
            next += 2;
    } while (res == 0 && next <= 4);

    return res;
}

void CFoundFilesListView::QuickSort(int left, int right, int sortBy)
{
    CALL_STACK_MESSAGE_NONE

LABEL_QuickSort:

    //  CALL_STACK_MESSAGE4("CFoundFilesListView::QuickSort(%d, %d, %d)", left,
    //                      right, sortBy);  // Petr: call stack too slow
    int i = left, j = right;
    CFoundFilesData* pivot = Data[(i + j) / 2];

    do
    {
        while (CompareFunc(Data[i], pivot, sortBy) < 0 && i < right)
            i++;
        while (CompareFunc(pivot, Data[j], sortBy) < 0 && j > left)
            j--;

        if (i <= j)
        {
            CFoundFilesData* swap = Data[i];
            Data[i] = Data[j];
            Data[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // the following "nice" code was replaced with one that saves a lot of stack (maximum log(N) recursion depth)
    //  if (left < j) QuickSort(left, j, sortBy);
    //  if (i < right) QuickSort(i, right, sortBy);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // both "halves" must be sorted, so recurse into the smaller one and handle the other via "goto"
            {
                QuickSort(left, j, sortBy);
                left = i;
                goto LABEL_QuickSort;
            }
            else
            {
                QuickSort(i, right, sortBy);
                right = j;
                goto LABEL_QuickSort;
            }
        }
        else
        {
            right = j;
            goto LABEL_QuickSort;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_QuickSort;
        }
    }
}

void CFoundFilesListView::SortItems(int sortBy)
{
    CALL_STACK_MESSAGE2("CFoundFilesListView::SortItems(%d)", sortBy);
    HCURSOR hCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    DataCriticalSection.Enter();

    // if we have any items in the data that are not in the list view, transfer them
    SearchDialog->UpdateListViewItems();

    if (Data.Count > 0)
    {
        // save the state of the selected and focused items
        StoreItemsState();

        // sort the array according to the requested criterion
        QuickSort(0, Data.Count - 1, sortBy);

        RestoreItemsState();
        int focusIndex = ListView_GetNextItem(HWindow, -1, LVNI_FOCUSED);
        if (focusIndex != -1)
            ListView_EnsureVisible(HWindow, focusIndex, FALSE);
        ListView_RedrawItems(HWindow, 0, Data.Count - 1);
        UpdateWindow(HWindow);
    }

    DataCriticalSection.Leave();
    SetCursor(hCursor);
}

CFoundFilesData*
CFoundFilesListView::At(int index)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("CFoundFilesListView::At(%d)", index);  // Petr: call stack too slow
    CFoundFilesData* ptr;
    DataCriticalSection.Enter();
    ptr = Data[index];
    DataCriticalSection.Leave();
    return ptr;
}

void CFoundFilesListView::DestroyMembers()
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CFoundFilesListView::DestroyMembers()");
    //  DataCriticalSection.Enter();
    Data.DestroyMembers();
    //  DataCriticalSection.Leave();
}

int CFoundFilesListView::GetCount()
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CFoundFilesListView::GetCount()");
    int count;
    DataCriticalSection.Enter();
    count = Data.Count;
    DataCriticalSection.Leave();
    return count;
}

int CFoundFilesListView::Add(CFoundFilesData* item)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CFoundFilesListView::Add()"); // Petr: call stack too slow
    int index;
    DataCriticalSection.Enter();
    index = Data.Add(item);
    DataCriticalSection.Leave();
    return index;
}

BOOL CFoundFilesListView::IsGood()
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CFoundFilesListView::IsGood()");
    BOOL isGood;
    DataCriticalSection.Enter();
    isGood = Data.IsGood();
    DataCriticalSection.Leave();
    return isGood;
}

BOOL CFoundFilesListView::InitColumns()
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CFoundFilesListView::InitColumns()");

    LV_COLUMN lvc;
    int header[] = {IDS_NAME_COLUMN, IDS_TYPE_COLUMN, IDS_DATA_COLUMN, IDS_PATH_COLUMN,
                    IDS_SIZE_COLUMN, IDS_DATE_COLUMN, IDS_TIME_COLUMN};

    lvc.mask = LVCF_FMT | LVCF_TEXT | LVCF_SUBITEM;
    lvc.fmt = LVCFMT_LEFT;
    int i;
    for (i = 0; i < 7; i++) // create the columns
    {
        // LangStr is wide, so the fork and its conversion are both gone.
        lvc.pszText = const_cast<LPWSTR>(LangStr(header[i]).c_str());
        lvc.iSubItem = i;
        if (ListView_InsertColumn(HWindow, i, &lvc) == -1)
            return FALSE;
    }

    RECT r;
    GetClientRect(HWindow, &r);
    DWORD cx = r.right - r.left + (1 ? -1 : 1);
    ListView_SetColumnWidth(HWindow, CI_TIME, ListView_GetStringWidth(HWindow, "00:00:00") + 20);
    ListView_SetColumnWidth(HWindow, CI_DATE, ListView_GetStringWidth(HWindow, "00.00.0000") + 20);
    ListView_SetColumnWidth(HWindow, CI_SIZE, ListView_GetStringWidth(HWindow, "000000") + 20);
    ListView_SetColumnWidth(HWindow, CI_PATH, ListView_GetStringWidth(HWindow, "X") * 16 + 20);
    ListView_SetColumnWidth(HWindow, CI_TYPE, ListView_GetStringWidth(HWindow, "EXPAND_SZ") + 20);
    ListView_SetColumnWidth(HWindow, CI_NAME, 20 + ListView_GetStringWidth(HWindow, "X") * 8 + 20);
    cx -= ListView_GetColumnWidth(HWindow, CI_NAME) + ListView_GetColumnWidth(HWindow, CI_TYPE) +
          ListView_GetColumnWidth(HWindow, CI_PATH) // + ListView_GetColumnWidth(HWindow, CI_SIZE) +
          //ListView_GetColumnWidth(HWindow, CI_DATE) + ListView_GetColumnWidth(HWindow, CI_TIME)
          //+ ListView_GetColumnWidth(HWindow, CI_ATTRIBUTES)
          + GetSystemMetrics(SM_CXHSCROLL) - 1;
    ListView_SetColumnWidth(HWindow, CI_DATA, cx);
    ListView_SetImageList(HWindow, ImageList, LVSIL_SMALL);

    return TRUE;
}

void CFoundFilesListView::GetContextMenuPos(POINT* p)
{
    CALL_STACK_MESSAGE1("CFoundFilesListView::GetContextMenuPos()");
    if (ListView_GetItemCount(HWindow) == 0)
    {
        p->x = 0;
        p->y = 0;
        ClientToScreen(HWindow, p);
        return;
    }
    int focIndex = ListView_GetNextItem(HWindow, -1, LVNI_FOCUSED);
    if (focIndex != -1)
    {
        if ((ListView_GetItemState(HWindow, focIndex, LVNI_SELECTED) & LVNI_SELECTED) == 0)
            focIndex = ListView_GetNextItem(HWindow, -1, LVNI_SELECTED);
    }
    RECT cr;
    GetClientRect(HWindow, &cr);
    RECT r;
    ListView_GetItemRect(HWindow, 0, &r, LVIR_LABEL);
    p->x = r.left;
    if (p->x < 0)
        p->x = 0;
    if (focIndex != -1)
        ListView_GetItemRect(HWindow, focIndex, &r, LVIR_BOUNDS);
    if (focIndex == -1 || r.bottom < 0 || r.bottom > cr.bottom)
        r.bottom = 0;
    p->y = r.bottom;
    ClientToScreen(HWindow, p);
}

LRESULT
CFoundFilesListView::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SLOW_CALL_STACK_MESSAGE4("CFoundFilesListView::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_GETDLGCODE:
    {
        if (lParam != NULL)
        {
            // if it is Enter, we want to process it
            MSG* msg = (LPMSG)lParam;
            if (msg->message == WM_KEYDOWN && msg->wParam == VK_RETURN)
                return DLGC_WANTMESSAGE;
        }
        return DLGC_WANTCHARS | DLGC_WANTARROWS;
    }

    case WM_MOUSEACTIVATE:
    {
        // if Find is inactive and the user wants to drag and drop
        // one of the items, Find must not pop up
        return MA_NOACTIVATE;
    }

    case WM_SETFOCUS:
    {
        SendMessage(GetParent(HWindow), WM_USER_BUTTONS, 0, 1);
        break;
    }

    case WM_KILLFOCUS:
    {
        HWND next = (HWND)wParam;
        BOOL nextIsButton;
        if (next != NULL)
        {
            std::wstring className;
            WORD wl = LOWORD(GetWindowLong(next, GWL_STYLE)); // jen BS_...
            nextIsButton = (ReadWindowClassOwnedW(next, className) &&
                            SG->StrICmp(className.c_str(), L"BUTTON") == 0 &&
                            (wl == BS_PUSHBUTTON || wl == BS_DEFPUSHBUTTON));
        }
        else
            nextIsButton = FALSE;
        SendMessage(GetParent(HWindow), WM_USER_BUTTONS, nextIsButton ? wParam : 0, 0);
        break;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}

/*
//******************************************************************************
//
// CComboboxEdit
//

CComboboxEdit::CComboboxEdit()
 : CWindow(ooAllocated)
{
    SelStart = 0;
    SelEnd = -1;
}

LRESULT
CComboboxEdit::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  CALL_STACK_MESSAGE4("CComboboxEdit::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
  switch (uMsg)
  {
    case WM_KILLFOCUS:
    {
      SendMessage(HWindow, EM_GETSEL, (WPARAM)&SelStart, (LPARAM)&SelEnd);
      break;
    }

    case EM_REPLACESEL:
    {
      LRESULT res = CWindow::WindowProc(uMsg, wParam, lParam);
      SendMessage(HWindow, EM_GETSEL, (WPARAM)&SelStart, (LPARAM)&SelEnd);
      return res;
    }
  }
  return CWindow::WindowProc(uMsg, wParam, lParam);
}

void
CComboboxEdit::GetSel(DWORD *start, DWORD *end)
{
  if (GetFocus() == HWindow)
    SendMessage(HWindow, EM_GETSEL, (WPARAM)start, (LPARAM)end);
  else
  {
    *start = SelStart;
    *end = SelEnd;
  }
}

void
CComboboxEdit::ReplaceText(const char *text)
{
  // we have to revive the selection because the dumb combobox forgot it
  SendMessage(HWindow, EM_SETSEL, SelStart, SelEnd);
  SendMessage(HWindow, EM_REPLACESEL, TRUE, (LPARAM)text);
}

  */

//*********************************************************************************
//
// CFindThread
//

BOOL CFindThread::TestTime(FILETIME& ft)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CFindThread::TestTime()");  // Petr: call stack too slow

    if (!UseMinTime && !UseMaxTime)
        return TRUE;

    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    if (UseMinTime)
    {
        if (st.wYear < MinTime.wYear ||
            st.wYear == MinTime.wYear &&
                (st.wMonth < MinTime.wMonth ||
                 st.wMonth == MinTime.wMonth &&
                     (st.wDay < MinTime.wDay ||
                      st.wDay == MinTime.wDay &&
                          (st.wHour < MinTime.wHour ||
                           st.wHour == MinTime.wHour &&
                               (st.wMinute < MinTime.wMinute ||
                                st.wMinute == MinTime.wMinute &&
                                    (st.wSecond < MinTime.wSecond))))))
        {
            return FALSE;
        }
    }
    if (UseMaxTime)
    {
        if (st.wYear > MaxTime.wYear ||
            st.wYear == MaxTime.wYear &&
                (st.wMonth > MaxTime.wMonth ||
                 st.wMonth == MaxTime.wMonth &&
                     (st.wDay > MaxTime.wDay ||
                      st.wDay == MaxTime.wDay &&
                          (st.wHour > MaxTime.wHour ||
                           st.wHour == MaxTime.wHour &&
                               (st.wMinute > MaxTime.wMinute ||
                                st.wMinute == MaxTime.wMinute &&
                                    (st.wSecond > MaxTime.wSecond))))))
        {
            return FALSE;
        }
    }

    return TRUE;
}

BOOL CFindThread::Test(char* text, int len, BOOL name, DWORD type)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE4("CFindThread::Test(, %d, %d, 0x%X)", len, name, type);
    // an empty string is contained in every string
    if (PatternALen == 0 && PatternWLen == 0)
        return TRUE;

    if (RegExp)
    {
        // translate the string to ASCII
        if (name || type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ)
        {
            if (!name)
            {
                if (type == REG_SZ || type == REG_EXPAND_SZ)
                {
                    len = max(0, len - 2); // trim the terminating NULL
                }
                else
                {
                    len = max(0, len - 4); // trim the terminating NULL
                }
            }

            len /= 2;

            if (len > 0)
            {
                std::wstring_view source(reinterpret_cast<wchar_t*>(text), static_cast<size_t>(len));
                if (!EncodeRegedtSearchSubject(source, AsciiBuffer))
                {
                    return FALSE;
                }
                text = AsciiBuffer.data();
                len = static_cast<int>(AsciiBuffer.size());
            }
        }

        // parse individual lines and test the regexp
        char* start;
        char* end = text;
        do
        {
            start = end;

            while (end < text + len && *end != '\n' && *end != '\r' && *end != '\0')
                end++;

            // test the line
            SalRegExp->SetLine(start, end);
            int pos = 0;
            do
            {
                int matchLen;
                if ((pos = SalRegExp->SearchForward(pos, matchLen)) != -1)
                {
                    if (!WholeWords ||
                        matchLen > 0 && (pos == 0 || isspace(text[pos - 1])) &&
                            (pos + matchLen == end - start || isspace(text[pos + matchLen])))
                        return TRUE;
                    pos++; // try a little further; maybe it will work
                }
                else
                    break;
            } while (pos < end - start);

            // for CRLF we must skip two characters
            if (end + 1 < text + len && end[0] == '\r' && end[1] == '\n')
                end++;

            // move to the start of the next line
            end++;
        } while (end < text + len);
    }
    else
    {
        if (name)
        {
            // we search the name only against the Unicode pattern; in a hex
            // search the Unicode pattern is the same as ASCII
            int pos = 0;
            do
            {
                if ((pos = BMForPatternW->SearchForward(text, len, pos)) != -1)
                {
                    if (!WholeWords ||
                        (pos < 2 || iswspace(*(LPWSTR)(text + pos - 2))) &&
                            (pos + PatternWLen > len - 2 || iswspace(*(LPWSTR)(text + pos + PatternWLen))))
                        return TRUE;
                    pos += 2; // try a little further; maybe it will work
                }
                else
                    break;
            } while (pos <= len - PatternWLen);
        }
        else
        {
            switch (type)
            {
            case REG_MULTI_SZ:
            case REG_EXPAND_SZ:
            case REG_SZ:
            {
                // we search SZ only against the Unicode pattern; in a hex
                // search the Unicode pattern is the same as ASCII
                int pos = 0;
                do
                {
                    if ((pos = BMForPatternW->SearchForward(text, len, pos)) != -1)
                    {
                        if (!WholeWords ||
                            (pos < 2 || iswspace(*(LPWSTR)(text + pos - 2))) &&
                                (pos + PatternWLen > len - 2 || iswspace(*(LPWSTR)(text + pos + PatternWLen))))
                            return TRUE;
                        pos += 2; // try a little further; maybe it will work
                    }
                    else
                        break;
                } while (pos <= len - PatternWLen);
                break;
            }

            case REG_DWORD_BIG_ENDIAN:
                // compare numbers with their numeric value
                if (UseNumber && len >= static_cast<int>(sizeof(DWORD)))
                {
                    const auto* bytes = reinterpret_cast<const unsigned char*>(text);
                    return (static_cast<DWORD>(bytes[3]) |
                            static_cast<DWORD>(bytes[2]) << 8 |
                            static_cast<DWORD>(bytes[1]) << 16 |
                            static_cast<DWORD>(bytes[0]) << 24) == Number;
                }
                break;

            case REG_DWORD:
                // compare numbers with their numeric value
                if (UseNumber && len >= static_cast<int>(sizeof(DWORD)))
                {
                    DWORD value = 0;
                    memcpy(&value, text, sizeof(value));
                    return value == Number;
                }
                break;

            case REG_QWORD:
                // compare numbers with their numeric value
                if (UseNumber && len >= static_cast<int>(sizeof(QWORD)))
                {
                    QWORD value = 0;
                    memcpy(&value, text, sizeof(value));
                    return value == Number;
                }
                break;

            default:
            {
                // in binary values we try to match both the Unicode and ASCII pattern
                int pos = 0;
                do
                {
                    if ((pos = BMForPatternW->SearchForward(text, len, pos)) != -1)
                    {
                        if (!WholeWords ||
                            (pos < 2 || iswspace(*(LPWSTR)(text + pos - 2))) &&
                                (pos + PatternWLen > len - 2 || iswspace(*(LPWSTR)(text + pos + PatternWLen))))
                            return TRUE;
                        pos += 2; // try a little further; maybe it will work
                    }
                    else
                        break;
                } while (pos <= len - PatternWLen);

                if (BMForPatternA && BMForPatternA != BMForPatternW)
                {
                    // also try the ASCII pattern
                    pos = 0;
                    do
                    {
                        if ((pos = BMForPatternA->SearchForward(text, len, pos)) != -1)
                        {
                            if (!WholeWords ||
                                (pos == 0 || isspace(text[pos - 1])) &&
                                    (pos + PatternALen == len || isspace(text[pos + PatternALen])))
                                return TRUE;
                            pos++; // try a little further; maybe it will work
                        }
                        else
                            break;
                    } while (pos <= len - PatternALen);
                }
                break;
            }
            }
        }
    }
    return FALSE;
}

BOOL CFindThread::ScanKeyAux(int root, std::wstring& key, BOOL& skip, BOOL& skipAllErrors,
                             std::vector<std::wstring>& stack)
{
    HKEY hKey = NULL;
    try
    {
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE4("CFindThread::ScanKeyAux(%d, , %d, %d, , )", root, skip,
    //                           skipAllErrors);
    // status text
    if ((int)(GetTickCount() - NextStatusUpdate) > 0)
    {
        FindDialog->StatusBar->Set(key.c_str());
        NextStatusUpdate = GetTickCount() + 1;
    }

    // check for cancellation by the user
    if (WaitForSingleObject(CancelEvent, 0) == WAIT_OBJECT_0)
        return skip = FALSE;

    // open the key being searched
    if (!SafeOpenKey(root, key.data(), KEY_READ, hKey, IDS_SEARCHERROR, &skip, &skipAllErrors))
        return FALSE;

    // test the values
    FILETIME time, localFileTime;
    DWORD maxData = 0;

    // load the maximum data size and time of the key
    if (!SafeQueryInfoKey(hKey, root, key.data(), NULL, &maxData, &time, IDS_SEARCHERROR, skip, skipAllErrors))
    {
        RegCloseKey(hKey);
        return FALSE;
    }
    FileTimeToLocalFileTime(&time, &localFileTime);

    DWORD index = 0;
    std::wstring name;
    std::vector<BYTE> data;
    DWORD type, size;
    BOOL noMore = TRUE;

    if ((LookAtValues || LookAtData) && TestTime(localFileTime))
    {
        while (true)
        {
            const LONG result = RegedtEnumerateValueOwned(hKey, index, 64, maxData, name, type, data, size, LookAtData != FALSE);
            if (result == ERROR_NO_MORE_ITEMS)
            {
                noMore = TRUE;
                skip = FALSE;
                break;
            }
            if (result != ERROR_SUCCESS)
            {
                const BOOL retry = RegOperationError(result, IDS_ACCESS2, IDS_SEARCHERROR, root, key.data(), &skip, &skipAllErrors);
                if (!retry)
                {
                    ++index;
                    noMore = FALSE;
                }
                if (skip)
                {
                    skip = FALSE;
                    continue;
                }
                if (!retry)
                    break;
                continue;
            }
            ++index;
            noMore = FALSE;
            if (!skip)
            {
                if ((LookAtValues && Test(reinterpret_cast<char*>(name.data()), static_cast<int>(name.size() * sizeof(wchar_t)), TRUE, 0) ||
                     LookAtData && Test(reinterpret_cast<char*>(data.data()), static_cast<int>(size), FALSE, type)))
                {
#ifdef _DEBUG
                    // statistics of value type frequency
                    if (type >= 0 && type < 12) // Petr: I have a value on my machine with type==0x397e7d0 (HKEY_CURRENT_USER\Software\Microsoft\Internet Explorer\Explorer Bars\{32683183-48a0-441b-a342-7c2a440a9478}\BarSize)
                        TypeStat[type]++;
#endif

                    // add the item to the list view

                    CFoundFilesData* fd =
                        new CFoundFilesData(name.data(),
                                            root, key.data(), type, size,
                                            LookAtData && !data.empty() ? data.data() : nullptr,
                                            localFileTime, FALSE);
                    if (!fd || FindDialog->List->Add(fd) == ULONG_MAX)
                    {
                        if (fd)
                            delete fd;
                        skip = FALSE;
                        RegCloseKey(hKey);
                        return Error(IDS_LOWMEM);
                    }
                    // every 100 added items ask the list view to repaint
                    if (FindDialog->FoundVisibleCount + 100 < FindDialog->List->GetCount())
                        PostMessage(FindDialog->HWindow, WM_USER_ADDFILE, 0, 0);
                }
            }
            // check for cancellation by the user
            if (WaitForSingleObject(CancelEvent, 0) == WAIT_OBJECT_0)
            {
                RegCloseKey(hKey);
                return skip = FALSE;
            }
        }
    }

    if (!noMore)
    {
        RegCloseKey(hKey);
        return FALSE;
    }

    // recursively search the subkeys as well

    // first enumerate all keys onto the stack
    const size_t len = key.size();
    index = 0;
    const size_t top = stack.size();
    while (true)
    {
        const LONG result = RegedtEnumerateSubKeyOwned(hKey, index, 64, name, time);
        if (result == ERROR_NO_MORE_ITEMS)
        {
            noMore = TRUE;
            skip = FALSE;
            break;
        }
        if (result != ERROR_SUCCESS)
        {
            const BOOL retry = RegOperationError(result, IDS_ACCESS2, IDS_SEARCHERROR, root, key.data(), &skip, &skipAllErrors);
            if (!retry)
            {
                ++index;
                noMore = FALSE;
            }
            if (skip)
            {
                skip = FALSE;
                continue;
            }
            if (!retry)
                break;
            continue;
        }
        ++index;
        noMore = FALSE;
        if (!skip)
        {
            FileTimeToLocalFileTime(&time, &localFileTime);
            if (LookAtKeys && TestTime(localFileTime) && Test(reinterpret_cast<char*>(name.data()), static_cast<int>(name.size() * sizeof(wchar_t)), TRUE, 0))
            {
                // add the key to the result
                CFoundFilesData* fd =
                    new CFoundFilesData(name.data(), root, key.data(), 0, 0, NULL, localFileTime, TRUE);
                if (!fd || FindDialog->List->Add(fd) == ULONG_MAX)
                {
                    if (fd)
                        delete fd;
                    skip = FALSE;
                    RegCloseKey(hKey);
                    return Error(IDS_LOWMEM);
                }
                // every 100 added items ask the list view to repaint
                if (FindDialog->FoundVisibleCount + 100 < FindDialog->List->GetCount())
                    PostMessage(FindDialog->HWindow, WM_USER_ADDFILE, 0, 0);
            }

            if (IncludeSubkeys)
                stack.push_back(name);
        }
        else
            skip = FALSE;

        // check for cancellation by the user
        if (WaitForSingleObject(CancelEvent, 0) == WAIT_OBJECT_0)
        {
            RegCloseKey(hKey);
            return skip = FALSE;
        }
    }

    if (!noMore)
    {
        RegCloseKey(hKey);
        return FALSE;
    }

    // search the keys stored on the stack
    for (size_t i = stack.size(); i-- > top;)
    {
        key.resize(len);
        if (!key.empty())
            key.push_back(L'\\');
        key.append(stack[i]);

        if (!ScanKeyAux(root, key, skip, skipAllErrors, stack))
        {
            if (!skip)
            {
                RegCloseKey(hKey);
                return FALSE;
            }
        }
        stack.erase(stack.begin() + i);
    }
    key.resize(len);

    RegCloseKey(hKey);

    return TRUE;
    }
    catch (...)
    {
        if (hKey)
            RegCloseKey(hKey);
        skip = FALSE;
        return Error(IDS_LOWMEM);
    }
}

BOOL CFindThread::ScanKey(int root, std::wstring& key, BOOL& skip, BOOL& skipAllErrors,
                          std::vector<std::wstring>& stack)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE4("CFindThread::ScanKey(%d, , %d, %d, , )", root, skip,
    //                      skipAllErrors);
    const std::wstring base = SPLFormatStringOwned(LoadStrW(IDS_SEARCHING).c_str(), PredefinedHKeys[root].KeyName);
    FindDialog->StatusBar->SetBase(base.c_str());
    return ScanKeyAux(root, key, skip, skipAllErrors, stack);
}

BOOL CFindThread::ScanRegistry(BOOL& skipAllErrors)
{
    CALL_STACK_MESSAGE2("CFindThread::ScanRegistry(%d)", skipAllErrors);
    BOOL skip = FALSE;
    std::wstring key;
    std::vector<std::wstring> stack;
    int i = 0;
    while (PredefinedHKeys[i].HKey != NULL)
    {
        if (RegQueryInfoKeyW(PredefinedHKeys[i].HKey, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
        {
            key.clear();
            if (!ScanKey(i, key, skip, skipAllErrors, stack) && !skip)
                return FALSE;
        }
        i++;
    }
    return TRUE;
}

BOOL CFindThread::Init()
{
    CALL_STACK_MESSAGE1("CFindThread::Init()");
    if (PatternALen || PatternWLen)
    {
        // initialize the search objects
        WORD flags = SASF_FORWARD | (CaseSensitive ? SASF_CASESENSITIVE : 0);
        if (RegExp)
        {
            SalRegExp = SG->AllocSalamanderREGEXPSearchData();
            if (!SalRegExp)
                return Error(IDS_LOWMEM);

            if (!SalRegExp->Set(PatternA.c_str(), flags))
            {
                const char* err = SalRegExp->GetLastErrorText();
                if (err)
                {
                    std::wstring errorText;
                    if (DecodeRegedtRegexError(err, errorText))
                        Error(GetParent(), L"%s", errorText.c_str());
                    else
                        Error(IDS_REGEXPERR, L"Invalid encoded error text");
                }
                return FALSE;
            }
        }
        else
        {
            if (PatternALen != 0)
            {
                BMForPatternA = SG->AllocSalamanderBMSearchData();
                if (!BMForPatternA)
                    return Error(IDS_LOWMEM);

                BMForPatternA->Set(PatternA.data(), PatternALen, flags);
                if (!BMForPatternA->IsGood())
                    return Error(IDS_LOWMEM);
            }

            const bool patternsEqual = PatternALen == PatternWLen && PatternALen != 0 &&
                                       memcmp(PatternA.data(), PatternW.data(), static_cast<size_t>(PatternALen)) == 0;
            if (patternsEqual)
                BMForPatternW = BMForPatternA;
            else if (PatternWLen != 0)
            {
                BMForPatternW = SG->AllocSalamanderBMSearchData();
                if (!BMForPatternW)
                    return Error(IDS_LOWMEM);

                BMForPatternW->Set(PatternW.data(), PatternWLen, flags);
                if (!BMForPatternW->IsGood())
                    return Error(IDS_LOWMEM);
            }
        }
    }
    NextStatusUpdate = GetTickCount();
    return TRUE;
}

unsigned
CFindThread::Body()
{
    try
    {
        return BodyCore();
    }
    catch (...)
    {
        Error(IDS_LOWMEM);
        PostMessage(FindDialog->HWindow, WM_USER_SEARCH_FINISHED, 0, 0);
        return FALSE;
    }
}

unsigned
CFindThread::BodyCore()
{
    CALL_STACK_MESSAGE1("CFindThread::Body()");
    Sleep(50); // until Petr removes the bug in auxtools

    PARENT(FindDialog->HWindow);
    TRACE_I("Starting search for files");

#ifdef _DEBUG
    // statistics of value type frequency
    int i;
    for (i = 0; i < 12; i++)
        TypeStat[i] = 0;
#endif

    if (Init())
    {
        std::vector<std::wstring> stack;
        BOOL skipAllErrors = FALSE;
        int root;
        LPWSTR key;
        int j;
        for (j = 0; j < static_cast<int>(LookIn.size()); j++)
        {
            std::wstring location = LookIn[j];
            if (!RemoveFSNameFromPath(location))
            {
                Error(IDS_NOTREGEDTPATH);
                continue;
            }
            if (!ParseFullPath(location.data(), key, root))
            {
                Error(IDS_BADPATH);
                continue;
            }

            if (root == -1)
            {
                // search the entire registry
                if (!ScanRegistry(skipAllErrors))
                    break;
            }
            else
            {
                // search the selected key
                BOOL skip = FALSE;
                std::wstring keyBuffer = key;
                if (!ScanKey(root, keyBuffer, skip, skipAllErrors, stack) && !skip)
                    break;
            }
        }
    }

#ifdef _DEBUG
    // statistics of value type frequency
    for (i = 0; i < 12; i++)
        TRACE_I("Type " << i << " frequency = " << TypeStat[i]);
#endif

    TRACE_I("Ending search for files");

    PostMessage(FindDialog->HWindow, WM_USER_SEARCH_FINISHED, 0, 0);
    return TRUE;
}
