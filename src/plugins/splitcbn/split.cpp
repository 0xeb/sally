// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "common/ByteFormat.h"
#include "splitcbn.h"
#include "splitcbn.rh"
#include "splitcbn.rh2"
#include "lang\lang.rh"
#include "split.h"
#include "dialogs.h"
#include "splitcbn_text.h"

// *****************************************************************************
//
//  SplitFile
//

// cmd.exe rejects a command line longer than 8191 characters. OEM byte length is a
// conservative upper bound for the decoded command length on DBCS systems.
constexpr size_t CmdExeCommandCharacterLimit = 8191;

#define BUFSIZE1 (128 * 1024) // buffer size for a removable drive (kept small to check ESC presses)
#define BUFSIZE2 (256 * 1024) // size for the others

static BOOL EnsureDiskInsertedEtc(const wchar_t* targetDir, CQuadWord& qwPartSize, CQuadWord* freeSpace,
                                  UINT driveType, CQuadWord& bytesRemaining, CQuadWord* thisPartSize, HWND parent)
{
    CALL_STACK_MESSAGE1("EnsureDiskInserted()");
    *freeSpace = CQuadWord(1, 0);
    if (driveType == DRIVE_REMOVABLE)
    {
        while (1)
        {
            DWORD err;
            while ((err = SalamanderGeneral->SalCheckPath(FALSE, targetDir, ERROR_SUCCESS, parent)) == ERROR_USER_TERMINATED)
            {
                if (SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_CANCEL).c_str(), LangStr(IDS_SPLIT).c_str(),
                                                     MB_YESNO | MB_ICONQUESTION) == IDYES)
                    return FALSE;
            }
            if (err == ERROR_SUCCESS && qwPartSize == SIZE_AUTODETECT)
            {
                SalamanderGeneral->GetDiskFreeSpace(freeSpace, targetDir, NULL);
                if (*freeSpace == CQuadWord(-1, -1))
                {
                    SalamanderGeneral->ShowMessageBox(LangStr(IDS_OUTOFSPACE).c_str(), LangStr(IDS_SPLIT).c_str(), MSGBOX_ERROR);
                    return FALSE;
                }
            }

            if (err != ERROR_SUCCESS || *freeSpace == CQuadWord(0, 0))
            {
                if (SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_INSERTDISK).c_str(),
                                                     LangStr(IDS_SPLIT).c_str(), MB_OKCANCEL | MB_ICONINFORMATION) == IDCANCEL)
                    return FALSE;
            }
            else
                break;
        }
    }

    if (driveType != DRIVE_REMOVABLE || qwPartSize != SIZE_AUTODETECT)
        SalamanderGeneral->GetDiskFreeSpace(freeSpace, targetDir, NULL);

    if (qwPartSize == SIZE_AUTODETECT)
    {
        *thisPartSize = (*freeSpace < bytesRemaining) ? *freeSpace : bytesRemaining;
    }
    else
    {
        *thisPartSize = (qwPartSize < bytesRemaining) ? qwPartSize : bytesRemaining;
        while (*freeSpace < *thisPartSize)
        {
            if (driveType != DRIVE_REMOVABLE)
            {
                SalamanderGeneral->ShowMessageBox(LangStr(IDS_OUTOFSPACE).c_str(), LangStr(IDS_SPLIT).c_str(), MSGBOX_ERROR);
                return FALSE;
            }
            else
            {
                if (SalamanderGeneral->ShowMessageBox(LangStr(IDS_INSERTDISK).c_str(), LangStr(IDS_SPLIT).c_str(),
                                                      MSGBOX_EX_ERROR) == IDCANCEL)
                    return FALSE;
                SalamanderGeneral->GetDiskFreeSpace(freeSpace, targetDir, NULL);
            }
        }
    }
    return TRUE;
}

static BOOL SplitFile(const wchar_t* fileName, const wchar_t* targetDir, CQuadWord& qwPartSize,
                      HWND parent, CSalamanderForOperationsAbstract* salamander)
{
    CALL_STACK_MESSAGE4("SplitFile(%ls, %ls, %I64u, , )", fileName, targetDir, qwPartSize.Value);

    // create the target path
    DWORD silent = 0;
    BOOL bSkip;
    if (SalamanderSafeFile->SafeFileCreate(targetDir, 0, 0, 0, TRUE, parent, NULL, NULL, &silent,
                                           TRUE, &bSkip, NULL, 0, NULL, NULL) == INVALID_HANDLE_VALUE)
        return FALSE;

    // open the file
    SAFE_FILE file;
    if (!SalamanderSafeFile->SafeFileOpen(&file, fileName, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING,
                                          FILE_FLAG_SEQUENTIAL_SCAN, parent, BUTTONS_RETRYCANCEL, NULL, NULL))
    {
        return FALSE;
    }
    // obtain the file size
    CQuadWord bytesRemaining;
    bytesRemaining.LoDWord = GetFileSize(file.HFile, &bytesRemaining.HiDWord);

    // obtain the file timestamp
    FILETIME ft;
    GetFileTime(file.HFile, NULL, NULL, &ft);

    // obtain the base name of the files
    std::wstring name = SalamanderGeneral->SalPathFindFileName(fileName);
    if (!configIncludeFileExt)
        StripExtension(name);

    // determine the type of the target media
    std::wstring text;
    std::wstring rootPathW;
    SPLGetRootPathOwned(SalamanderGeneral, targetDir, rootPathW);
    UINT driveType = GetDriveTypeW(rootPathW.c_str());

    BOOL abort = FALSE;

    // check whether the user selected Autodetect on a fixed drive
    if (driveType != DRIVE_REMOVABLE && qwPartSize == SIZE_AUTODETECT)
    {
        if (SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_FIXEDNOSENSE).c_str(),
                                             LangStr(IDS_SPLIT).c_str(), MB_YESNO | MB_ICONQUESTION) == IDNO)
            abort = TRUE;
    }

    // check for more than 100 parts
    if (!qwPartSize.Value || (bytesRemaining - CQuadWord(1, 0)) / qwPartSize + CQuadWord(1, 0) > CQuadWord(100, 0))
    {
        if (SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_TOOMANYPARTS).c_str(),
                                             LangStr(IDS_SPLIT).c_str(), MB_YESNO | MB_ICONQUESTION) == IDNO)
            abort = TRUE;
    }

    // if we split to a fixed disk, verify that there is free space (ignore the batch file)
    if (!abort && driveType != DRIVE_REMOVABLE &&
        !SalamanderGeneral->TestFreeSpace(parent, targetDir, bytesRemaining, LangStr(IDS_SPLIT).c_str()))
        abort = TRUE;

    if (abort)
    {
        SalamanderSafeFile->SafeFileClose(&file);
        return TRUE;
    }

    // allocate the buffer
    DWORD dwBufSize = (driveType == DRIVE_REMOVABLE) ? BUFSIZE1 : BUFSIZE2;
    char* pBuffer = new char[dwBufSize];
    if (pBuffer == NULL)
    {
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_OUTOFMEM).c_str(), LangStr(IDS_SPLIT).c_str(), MSGBOX_ERROR);
        SalamanderSafeFile->SafeFileClose(&file);
        return FALSE;
    }

    // init CRC
    UINT32 Crc = 0;

    // progress dialog
    salamander->OpenProgressDialog(LangStr(IDS_SPLIT).c_str(), TRUE, NULL, FALSE);
    salamander->ProgressSetTotalSize(CQuadWord(-1, -1), bytesRemaining);
    BOOL delayed = (driveType != DRIVE_REMOVABLE); // splitting to floppies allegedly failed to repaint the dialog fast enough
    salamander->ProgressSetSize(CQuadWord(-1, -1), CQuadWord(0, 0), delayed);
    CQuadWord totalProgress = CQuadWord(0, 0), fileProgress;

    // from now on the progress dialog is the parent
    parent = SalamanderGeneral->GetMsgBoxParent();

    BOOL ret = TRUE;
    int partNum = 1;
    std::wstring name2;
    CQuadWord thisPartSize;
    CQuadWord freeSpace;

    while (bytesRemaining.Value)
    {
        if (!EnsureDiskInsertedEtc(targetDir, qwPartSize, &freeSpace, driveType, bytesRemaining, &thisPartSize, parent))
        {
            ret = FALSE;
            break;
        }

        // create the name of the target file
        name2 = SPLFormatStringOwned(L"%s.%#03ld", name.c_str(), partNum++);
        text = targetDir;
        SPLSalPathAppendOwned(text, name2.c_str());

        // create the file
        const std::wstring info = GetInfo(thisPartSize);
        SAFE_FILE outfile;
        if (SalamanderSafeFile->SafeFileCreate(text.c_str(), GENERIC_WRITE, FILE_SHARE_READ, FILE_ATTRIBUTE_NORMAL,
                                               FALSE, parent, name2.c_str(), info.c_str(), &silent, TRUE, &bSkip, NULL, 0, NULL, &outfile) == INVALID_HANDLE_VALUE &&
            !bSkip)
        {
            ret = FALSE;
            break;
        }

        // finally: copy thisPartSize bytes of the input file to the output file
        salamander->ProgressSetTotalSize(thisPartSize, CQuadWord(-1, -1));
        salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), delayed);
        fileProgress = CQuadWord(0, 0);
        if (!bSkip)
        {
            const std::wstring text2 = SPLFormatStringOwned(
                L"%s %s...", LangStr(IDS_WRITING).c_str(), name2.c_str());
            salamander->ProgressDialogAddText(text2.c_str(), delayed);

            CQuadWord numBytes = thisPartSize;
            while (numBytes.Value)
            {
                DWORD toread = (numBytes > CQuadWord(dwBufSize, 0)) ? dwBufSize : numBytes.LoDWord;
                DWORD numread, numwr;
                if (!SalamanderSafeFile->SafeFileRead(&file, pBuffer, toread, &numread, parent, BUTTONS_RETRYCANCEL, NULL, NULL) ||
                    !SalamanderSafeFile->SafeFileWrite(&outfile, pBuffer, numread, &numwr, parent, BUTTONS_RETRYCANCEL, NULL, NULL))
                {
                    ret = FALSE;
                    break;
                }
                Crc = SalamanderGeneral->UpdateCrc32(pBuffer, numread, Crc);
                CQuadWord qwnr(numread, 0);
                numBytes -= qwnr;
                fileProgress += qwnr;
                if (!salamander->ProgressSetSize(fileProgress, totalProgress + fileProgress, delayed))
                {
                    ret = FALSE;
                    break;
                }
            }
            SalamanderSafeFile->SafeFileClose(&outfile);
            if (ret == FALSE)
            {
                DeleteFileW(text.c_str());
                break;
            }
        }
        else
        {
            CQuadWord distance = thisPartSize; // 'thisPartSize' must not change (use 'distance' instead)
            SalamanderSafeFile->SafeFileSeekMsg(&file, &distance, FILE_CURRENT, parent,
                                                BUTTONS_RETRYCANCEL, NULL, NULL, TRUE);
        }

        bytesRemaining -= thisPartSize;
        totalProgress += thisPartSize;

        // notify about inserting the next disk
        if (bytesRemaining.Value && driveType == DRIVE_REMOVABLE &&
            (qwPartSize == SIZE_AUTODETECT || freeSpace - qwPartSize < qwPartSize))
        {
            if (SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_INSERTNEXT).c_str(),
                                                 LangStr(IDS_SPLIT).c_str(), MB_OKCANCEL | MB_ICONINFORMATION) == IDCANCEL)
            {
                ret = FALSE;
                break;
            }
        }
    }

    delete[] pBuffer;
    SalamanderSafeFile->SafeFileClose(&file);

    // create the batch file

    if (ret != FALSE && configCreateBatchFile)
    {
        int nparts = partNum - 1;
        int linenum = 1, partnum = 1;
        // The .bat is an OEM byte protocol. Refuse a script if cmd.exe cannot name the
        // real UTF-16 files exactly; substituting a character could combine the wrong file.
        std::string origName;
        std::string nameA;
        std::string descriptionFormat;
        std::string generatedBy;
        std::string quitHint;
        std::string origArgument;
        std::string nameArgument;
        std::string escapedOriginalName;
        if (!EncodeSplitBatchText(SalamanderGeneral->SalPathFindFileName(fileName), origName) ||
            !EncodeSplitBatchText(name, nameA) ||
            !EncodeSplitBatchText(LangStr(IDS_BATFILE_DESCR), descriptionFormat) ||
            !EncodeSplitBatchText(LangStr(IDS_BATFILE_GENBYSAL), generatedBy) ||
            !EncodeSplitBatchText(LangStr(IDS_BATFILE_CTRL_C_TO_QUIT), quitHint) ||
            !EscapeSplitBatchArgument(origName, origArgument) ||
            !EscapeSplitBatchArgument(nameA, nameArgument) ||
            !EscapeSplitBatchEchoText(origName, escapedOriginalName))
        {
            SalamanderGeneral->ShowMessageBox(LangStr(IDS_BATTOOLONG).c_str(),
                                               LangStr(IDS_SPLIT).c_str(), MSGBOX_ERROR);
            salamander->CloseProgressDialog();
            SalamanderGeneral->PostChangeOnPathNotification(targetDir, FALSE);
            return FALSE;
        }
        SYSTEMTIME st;
        FileTimeToSystemTime(&ft, &st);
        const std::string description = sally::bytes::Format(
            descriptionFormat.c_str(),
            escapedOriginalName.c_str());
        std::string batfile = sally::bytes::Format(
                "@echo off\r\n"
                "rem %s, https://github.com/0xeb/sally\r\n"
                "rem name=%s\r\n"
                "rem crc32=%X\r\n"
                "rem time=%d-%d-%d %d:%02d:%02d\r\n"
                "echo %s\r\n"
                "echo %s\r\n"
                "pause\r\n",
                generatedBy.c_str(), origName.c_str(), Crc,
                (int)st.wYear, (int)st.wMonth, (int)st.wDay, (int)st.wHour, (int)st.wMinute, (int)st.wSecond,
                description.c_str(), quitHint.c_str());

        while (partnum <= nparts)
        {
            std::string line = "copy /b ";
            if (linenum == 1)
            { // first copy command, start = "xxx.001"+"xxx.002"
                line.push_back('"');
                line.append(nameArgument);
                line.append(sally::bytes::Format(".%#03ld", partnum++));
                if (nparts > 1)
                {
                    line.append("\"+\"");
                    line.append(nameArgument);
                    line.append(sally::bytes::Format(".%#03ld", partnum++));
                }
            }
            else
            { // subsequent copy commands - start = "xxx"+"xxx.yyy"
                line.push_back('"');
                line.append(origArgument);
                line.append("\"+\"");
                line.append(nameArgument);
                line.append(sally::bytes::Format(".%#03ld", partnum++));
            }
            line.push_back('"');

            while ((partnum < nparts) &&
                   (line.size() + origArgument.size() + nameArgument.size() + 10 <=
                    CmdExeCommandCharacterLimit))
            { // remainder of the line until all parts are used or the maximum line length is reached
                line.append("+\"");
                line.append(nameArgument);
                line.append(sally::bytes::Format(".%#03ld", partnum++));
                line.push_back('"');
            }

            // end of the line
            line.append(" \"");
            line.append(origArgument);
            line.append("\"\r\n");
            if (line.size() > CmdExeCommandCharacterLimit + 2) // CRLF is not part of the command.
            {
                SalamanderGeneral->ShowMessageBox(LangStr(IDS_BATTOOLONG).c_str(),
                                                   LangStr(IDS_SPLIT).c_str(), MSGBOX_ERROR);
                salamander->CloseProgressDialog();
                SalamanderGeneral->PostChangeOnPathNotification(targetDir, FALSE);
                return FALSE;
            }
            linenum++;
            batfile.append(line);
        }

        if (ret)
        {
            CQuadWord batSize(static_cast<DWORD>(batfile.size()), 0);
            bytesRemaining = batSize;
            thisPartSize = batSize;

            SalamanderGeneral->GetDiskFreeSpace(&freeSpace, targetDir, NULL);
            if (driveType == DRIVE_REMOVABLE && freeSpace < batSize)
            { // "insert next disk"
                if (SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_INSERTNEXT).c_str(),
                                                     LangStr(IDS_SPLIT).c_str(), MB_OKCANCEL | MB_ICONINFORMATION) == IDCANCEL)
                    ret = FALSE;
            }

            if (ret)
                if (EnsureDiskInsertedEtc(targetDir, batSize, &freeSpace, driveType, bytesRemaining,
                                          &thisPartSize, parent))
                {
                    name2 = name + L".bat";
                    text = targetDir;
                    SPLSalPathAppendOwned(text, name2.c_str());

                    const std::wstring info = GetInfo(batSize);
                    SAFE_FILE bf;
                    if (SalamanderSafeFile->SafeFileCreate(text.c_str(), GENERIC_WRITE, FILE_SHARE_READ, FILE_ATTRIBUTE_NORMAL,
                                                           FALSE, parent, name2.c_str(), info.c_str(), &silent, TRUE, &bSkip, NULL, 0, NULL, &bf) != INVALID_HANDLE_VALUE &&
                        !bSkip)
                    {
                        text = SPLFormatStringOwned(L"%s %s", LangStr(IDS_WRITING).c_str(), name2.c_str());
                        salamander->ProgressDialogAddText(text.c_str(), TRUE);
                        DWORD numw;
                        if (!SalamanderSafeFile->SafeFileWrite(&bf, batfile.data(), batSize.LoDWord,
                                                               &numw, parent, BUTTONS_RETRYCANCEL,
                                                               NULL, NULL))
                            ret = FALSE;
                        SalamanderSafeFile->SafeFileClose(&bf);
                    }
                }
        }

    }

    salamander->CloseProgressDialog();
    SalamanderGeneral->PostChangeOnPathNotification(targetDir, FALSE);
    return ret;
}

// *****************************************************************************
//
//  SplitCommand
//

BOOL SplitCommand(HWND parent, CSalamanderForOperationsAbstract* salamander)
{
    CALL_STACK_MESSAGE1("SplitCommand( , )");
    // obtain information about the file
    std::wstring targetdir;
    const CFileData* pfd;
    BOOL isDir;
    pfd = SalamanderGeneral->GetPanelFocusedItem(PANEL_SOURCE, &isDir);
    if (pfd == NULL || isDir || !GetTargetDir(targetdir, pfd->Name, TRUE))
        return FALSE;

    // determine the file size
    std::wstring path;
    WIN32_FIND_DATAW wfd;
    HANDLE hFind;
    CQuadWord qwFileSize;
    if (!SPLGetPanelPathOwned(SalamanderGeneral, PANEL_SOURCE, path))
        return FALSE;
    SPLSalPathAppendOwned(path, pfd->Name);
    if ((hFind = FindFirstFileW(path.c_str(), &wfd)) != INVALID_HANDLE_VALUE)
    {
        FindClose(hFind);
        qwFileSize.Set(wfd.nFileSizeLow, wfd.nFileSizeHigh);
        if ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
            (wfd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            if (!SalamanderGeneral->SalGetFileSize2(path.c_str(), qwFileSize, NULL))
                qwFileSize.Set(wfd.nFileSizeLow, wfd.nFileSizeHigh);
        }
    }
    else
    {
        return Error(IDS_SPLIT, IDS_OPENERROR);
    }

    // files smaller than 2 bytes cannot be split
    if (qwFileSize.Value < 2)
    {
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_ZEROSIZE).c_str(), LangStr(IDS_SPLIT).c_str(), MSGBOX_WARNING);
        return TRUE;
    }

    // split dialog
    CQuadWord qwPartialSize;
    if (!SplitDialog(pfd->Name, qwFileSize, targetdir, &qwPartialSize, parent))
        return FALSE;

    // validation
    std::wstring panelpath;
    if (!GetTargetDir(panelpath, NULL, TRUE))
        return FALSE;
    if (!MakePathAbsolute(targetdir, TRUE, panelpath, !configSplitToOther, IDS_SPLIT))
        return FALSE;

    // split the file
    return SplitFile(path.c_str(), targetdir.c_str(), qwPartialSize, parent, salamander);
}
