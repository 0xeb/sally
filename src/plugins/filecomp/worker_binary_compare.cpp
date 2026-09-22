// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

static std::wstring FormatBinaryChangeReport(size_t index, QWORD length,
                                             QWORD offset, int digits)
{
    wchar_t countText[32];
    wchar_t offsetText[32];
    const std::wstring fmt = SPLExpandPluralStringOwned(
        SG, SPLLoadStrOwned(SG, HLanguage, IDS_BINREPORT1).c_str(), 1,
        &CQuadWord().SetUI64(length));
    _ui64tow_s(length, countText, _countof(countText), 10);
    _snwprintf_s(offsetText, _countof(offsetText), _TRUNCATE, L"%0*I64X", digits, offset);
    return SPLFormatStringOwned(fmt.c_str(), int(index), countText, offsetText);
}

void CFilecompWorker::CompareBinaryFiles()
{
    CCachedFile cf[2];
    int i;

    for (i = 0; i < 2; i++)
    {
        switch (cf[i].SetFile(Files[i].File))
        {
        case 1:
            CException::Raise(IDS_ACCESFILE, GetLastError(), Files[i].Name.c_str());
            break;
        case 2:
            CException(LangStr(IDS_LOWMEM).c_str());
            break;
        }
    }

    // This finds the first difference. The FC window shows empty panes in the meantime. Do we really need this?
    // Can we skip it?
    QWORD changeOffs = 0;
    int ret = CompareBinaryFilesAux(cf, changeOffs);
    if (ret < 0)
    {
        if (ret + 2 < 0)
            throw CAbortByUserException(); // cancel
        CException::Raise(IDS_ACCESFILE, GetLastError(), Files[ret + 2].Name.c_str());
    }

    CBinaryCompareResults results(Options);
    for (i = 0; i < 2; i++)
    {
        results.Files[i].Name = Files[i].Name.c_str();
        results.Files[i].Size = Files[i].Size;
    }
    results.FirstChange = changeOffs;

    if (ret == 0)
    {
        results.FirstChange = (QWORD)-1;
        SendMessage(MainWindow, WM_USER_WORKERNOTIFIES, WN_BINARY_FILES_DIFFER, (LPARAM)&results);
        throw CFilesDontDifferException();
    }

    if (SendMessage(MainWindow, WM_USER_WORKERNOTIFIES, WN_BINARY_FILES_DIFFER, (LPARAM)&results))
    {
        CBinaryChanges changes;

        TRACE_I("FindingDifferences");
        ret = FindDifferencesBody(cf, changeOffs, changes);
        if (ret == 0)
        {
            HWND comboHWnd = (HWND)SendMessage(MainWindow, WM_USER_WORKERNOTIFIES, WN_SETCHANGES, (LPARAM)&changes);
            if (comboHWnd)
            {
                TRACE_I("Initializing Combo");
                SendMessage(comboHWnd, WM_SETREDRAW, FALSE, 0);
                SendMessage(comboHWnd, CB_RESETCONTENT, 0, 0);
                int digits = ComputeAddressCharWidth(Files[0].Size, Files[1].Size);
                size_t j;
                for (j = 0; j < changes.size(); j++)
                {
                    const std::wstring report = FormatBinaryChangeReport(
                        j + 1, changes[j].Length, changes[j].Offset, digits);
                    LRESULT ret2 = SendMessageW(comboHWnd, CB_ADDSTRING, 0,
                                                (LPARAM)report.c_str());
                    if (ret2 == CB_ERR || ret2 == CB_ERRSPACE)
                    {
                        TRACE_E("CB_ADDSTRING has failed, j = " << j);
                        break;
                    }
                    if (CancelFlag)
                        throw CAbortByUserException(); // cancel
                }
                SendMessage(comboHWnd, WM_SETREDRAW, TRUE, 0);
                InvalidateRect(comboHWnd, NULL, TRUE);
                UpdateWindow(comboHWnd);

                const wchar_t* path0Name = SG->SalPathFindFileName(Files[0].Name.c_str());
                const wchar_t* path1Name = SG->SalPathFindFileName(Files[1].Name.c_str());
                std::wstring caption;
                if (changes.size() < MaxBinChanges)
                {
                    CQuadWord qSize((DWORD)changes.size(), 0);
                    const std::wstring fmt = SPLExpandPluralStringOwned(
                        SG, SPLLoadStrOwned(SG, HLanguage, IDS_MAINWNDHEADER).c_str(), 1, &qSize);
                    caption = SPLFormatStringOwned(fmt.c_str(), path0Name, L"",
                                                   path1Name, L"", int(changes.size()));
                }
                else
                {
                    caption = SPLFormatStringOwned(
                        SPLLoadStrOwned(SG, HLanguage, IDS_MAINWNDHEADERTOOMANY).c_str(),
                        path0Name, path1Name);
                }
                // Same as CMainWindow's own captions: the target is a Unicode
                // window, so the CP_ACP projection was pure loss.
                SetWindowTextW(MainWindow, caption.c_str());
                PostMessage(MainWindow, WM_USER_WORKERNOTIFIES, WN_CBINIT_FINISHED, 0);
            }
        }
        else
        {
            switch (ret)
            {
            case 1:
            case 2:
                CException::Raise(IDS_ACCESFILE, GetLastError(), Files[ret - 1].Name.c_str());
            case 3:
                throw CException(LangStr(IDS_LOWMEM).c_str());
            case 4:
                throw CAbortByUserException(); // cancel
            }
        }
    }
}

// Finds the first difference. Do we really need this function?
int CFilecompWorker::CompareBinaryFilesAux(CCachedFile (&cf)[2], QWORD& changeOffs)
{
    CALL_STACK_MESSAGE_NONE
    unsigned int blokSize = cf[0].GetMinViewSize();
    DWORD size, tickCount = GetTickCount() - 1000;
    QWORD remain = __min(Files[0].Size, Files[1].Size);
    QWORD offset = 0, totalSize;
    LPBYTE ptr0, ptr1, start, end;
    int ret = 0, pct = 0;

    totalSize = remain;

    while (remain)
    {
        size = (DWORD)__min(blokSize, remain);

        int newPct = (int)((100 * (totalSize - remain)) / totalSize);

        if (pct != newPct)
        {
            if ((GetTickCount() - tickCount) > 500)
            {
                tickCount = GetTickCount();
                pct = newPct;
                SendMessage(MainWindow, WM_USER_WORKERNOTIFIES, WN_SET_PROGRESS, MAKELPARAM(pct, 0));
            }
        }

        ptr0 = cf[0].ReadBuffer(offset, size, CancelFlag);
        if (!ptr0)
            return CancelFlag ? -3 : -2;

        ptr1 = cf[1].ReadBuffer(offset, size, CancelFlag);
        if (!ptr1)
            return CancelFlag ? -3 : -1;

        start = ptr0;
        end = ptr0 + size;
        if (size > sizeof(int) - 1)
        {
            end -= sizeof(int) - 1;
            while (ptr0 < end && *(int*)ptr0 == *(int*)ptr1)
                ptr0 += sizeof(int), ptr1 += sizeof(int);
            end += sizeof(int) - 1;
        }

        while (ptr0 < end && *ptr0 == *ptr1)
            ptr0++, ptr1++;

        if (ptr0 < end)
        {
            ret = 1;
            break;
        }

        remain -= size;
        offset += size;

        if (CancelFlag)
            return -3;
    }

    if (ret)
        changeOffs = ptr0 - start + offset;
    else
    {
        if (Files[0].Size != Files[1].Size)
        {
            changeOffs = __min(Files[0].Size, Files[1].Size);
            ret = 1;
        }
    }

    if (!ret)
    {
        // The files are equal -> set progress to 100%
        SendMessage(MainWindow, WM_USER_WORKERNOTIFIES, WN_SET_PROGRESS, MAKELPARAM(100, 0));
    }
    return ret;
}

int CFilecompWorker::FindDifferencesBody(CCachedFile (&cf)[2], QWORD changeOffs, CBinaryChanges& changes)
{
    CALL_STACK_MESSAGE1("CFilecompWorker::FindDifferencesBody(, , )");

    unsigned int blokSize = cf[0].GetMinViewSize();
    QWORD remain, totalSize;
    QWORD offset = changeOffs;
    LPBYTE ptr0, ptr1, start, end;
    QWORD offsetSave;
    QWORD lenghtSave;
    int pct = 0;
    DWORD lastChangesSize = -1;
    DWORD tickCount = GetTickCount() - 1000;

    totalSize = __min(Files[0].Size, Files[1].Size);

    for (;;)
    {
        // determine the length of the difference
        // offset points to the start of the difference

        offsetSave = offset;
        remain = totalSize - offset;
        while (remain)
        {
            int newPct = (int)((100 * (totalSize - remain)) / totalSize);

            if (pct != newPct || lastChangesSize != changes.size())
            {
                if ((GetTickCount() - tickCount) > 500)
                {
                    tickCount = GetTickCount();
                    pct = newPct;
                    lastChangesSize = (DWORD)changes.size();
                    SendMessage(MainWindow, WM_USER_WORKERNOTIFIES, WN_SET_PROGRESS, MAKELPARAM(pct, lastChangesSize));
                }
            }

            DWORD size = (DWORD)__min(blokSize, remain);

            ptr0 = cf[0].ReadBuffer(offset, size, CancelFlag);
            if (!ptr0)
                return CancelFlag ? 4 : 1;

            ptr1 = cf[1].ReadBuffer(offset, size, CancelFlag);
            if (!ptr1)
                return CancelFlag ? 4 : 2;

            start = ptr0;
            end = ptr0 + size;

            while (ptr0 < end && *ptr0 != *ptr1)
                ptr0++, ptr1++;

            if (ptr0 < end)
                break;

            remain -= size;
            offset += size;

            if (CancelFlag)
                return 4;
        }

        if (remain)
        {
            lenghtSave = (ptr0 - start + offset) - offsetSave;
        }
        else
        {
            if (Files[0].Size != Files[1].Size)
            {
                lenghtSave = __max(Files[0].Size, Files[1].Size) - offsetSave;
            }
            else
            {
                lenghtSave = Files[0].Size - offsetSave;
            }
        }

        if (lenghtSave)
        {
            changes.push_back(CBinaryChange(offsetSave, lenghtSave));
            if (changes.size() == MaxBinChanges)
                break; // stop searching; we already have plenty
            HWND hComboWnd = (HWND)SendMessage(MainWindow, WM_USER_WORKERNOTIFIES, WN_GET_CHANGESCOMBO, NULL);
            if (hComboWnd)
            {
                int digits = ComputeAddressCharWidth(Files[0].Size, Files[1].Size);
                const std::wstring report = FormatBinaryChangeReport(
                    changes.size(), lenghtSave, offsetSave, digits);
                if (changes.size() == 1)
                    SendMessage(hComboWnd, CB_RESETCONTENT, 0, 0); // Hack
                LRESULT ret = SendMessageW(hComboWnd, CB_ADDSTRING, 0,
                                           (LPARAM)report.c_str());
                if (changes.size() == 1)
                    SendMessage(hComboWnd, CB_SETCURSEL, 0, 0); // Hack
                if (ret == CB_ERR || ret == CB_ERRSPACE)
                {
                    TRACE_E("CB_ADDSTRING has failed");
                    break;
                }
            }
            CBinaryChange* change = &changes.back();
            SendMessage(MainWindow, WM_USER_WORKERNOTIFIES, WN_ADD_CHANGE, (LPARAM)change);
        }
        else
            break;

        // look for the start of the next difference
        offset = changes.back().Offset + changes.back().Length; // starts after the last difference
        if (offset >= totalSize)
            break; // already at the end of the file
        remain = totalSize - offset;

        while (remain)
        {
            int newPct = (int)((100 * (totalSize - remain)) / totalSize);

            if (pct != newPct || lastChangesSize != changes.size())
            {
                if ((GetTickCount() - tickCount) > 500)
                {
                    tickCount = GetTickCount();
                    pct = newPct;
                    lastChangesSize = (DWORD)changes.size();
                    SendMessage(MainWindow, WM_USER_WORKERNOTIFIES, WN_SET_PROGRESS, MAKELPARAM(pct, lastChangesSize));
                }
            }

            // compare data in chunks of the requested size
            DWORD size = (DWORD)__min(blokSize, remain);

            ptr0 = cf[0].ReadBuffer(offset, size, CancelFlag);
            if (!ptr0)
                return CancelFlag ? 4 : 1;

            ptr1 = cf[1].ReadBuffer(offset, size, CancelFlag);
            if (!ptr1)
                return CancelFlag ? 4 : 2;

            start = ptr0;
            end = ptr0 + size;
            if (size > sizeof(int) - 1)
            {
                end -= sizeof(int) - 1;
                while (ptr0 < end && *(int*)ptr0 == *(int*)ptr1)
                    ptr0 += sizeof(int), ptr1 += sizeof(int);
                end += sizeof(int) - 1;
            }

            while (ptr0 < end && *ptr0 == *ptr1)
                ptr0++, ptr1++;

            if (ptr0 < end)
                break;

            remain -= size;
            offset += size;

            if (CancelFlag)
                return 4;
        }

        if (remain)
        {
            offset = ptr0 - start + offset; // offset now points to the start of the next difference
        }
    }

    return 0; // succes
}
