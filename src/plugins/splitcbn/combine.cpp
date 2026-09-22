// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "splitcbn_text.h"
#include <tchar.h>
#include "splitcbn.h"
#include "splitcbn.rh"
#include "splitcbn.rh2"
#include "lang\lang.rh"
#include "combine.h"
#include "dialogs.h"

// *****************************************************************************
//
//  Combine Files
//

#define BUFSIZE (512 * 1024)

BOOL CombineFiles(TIndirectArray<wchar_t>& files, const wchar_t* targetName,
                  BOOL bOnlyCrc, BOOL bTestCrc, UINT32& Crc,
                  BOOL bTime, FILETIME* origTime, HWND parent,
                  CSalamanderForOperationsAbstract* salamander)
{
    CALL_STACK_MESSAGE4("CombineFiles( , %ls, %ld, %X, , )", targetName, bTestCrc, Crc);

    if (!bOnlyCrc && !files.Count)
    {
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_ZEROFILES).c_str(), LangStr(IDS_COMBINE).c_str(), MSGBOX_ERROR);
        return FALSE;
    }

    int idTitle = bOnlyCrc ? IDS_CRCTITLE : IDS_COMBINE;

    // sum sizes of all partial files (while simultaneously checking their accessibility)
    CQuadWord totalSize = CQuadWord(0, 0);
    std::wstring text;
    int i;
    for (i = 0; i < files.Count; i++)
    {
        SAFE_FILE file;
        if (!SalamanderSafeFile->SafeFileOpen(&file, files[i], GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING,
                                              0, parent, BUTTONS_RETRYCANCEL, NULL, NULL))
        {
            return FALSE;
        }
        CQuadWord size;
        size.LoDWord = GetFileSize(file.HFile, &size.HiDWord);
        totalSize += size;
        SalamanderSafeFile->SafeFileClose(&file);
    }

    // check available free space
    if (!bOnlyCrc)
    {
        std::wstring dir = targetName;
        SPLCutDirectoryOwned(SalamanderGeneral, dir);
        if (!SalamanderGeneral->TestFreeSpace(parent, dir.c_str(), totalSize, LangStr(IDS_COMBINE).c_str()))
            return FALSE;
    }

    // create the output file
    SAFE_FILE outfile;
    if (!bOnlyCrc)
    {
        if (SalamanderSafeFile->SafeFileCreate(targetName, GENERIC_WRITE, FILE_SHARE_READ, FILE_ATTRIBUTE_NORMAL,
                                               FALSE, parent, NULL, NULL, NULL, FALSE, NULL, NULL, 0, NULL, &outfile) == INVALID_HANDLE_VALUE)
        {
            return FALSE;
        }
    }

    // merge the files
    char* pBuffer = new char[BUFSIZE];
    if (pBuffer == NULL)
    {
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_OUTOFMEM).c_str(), LangStr(idTitle).c_str(), MSGBOX_ERROR);
        if (!bOnlyCrc)
            SalamanderSafeFile->SafeFileClose(&outfile);
        return FALSE;
    }

    UINT32 CrcVal = 0;

    // open the progress dialog
    salamander->OpenProgressDialog(LangStr(idTitle).c_str(), TRUE, parent, FALSE);
    salamander->ProgressSetTotalSize(CQuadWord(-1, -1), totalSize);
    salamander->ProgressSetSize(CQuadWord(-1, -1), CQuadWord(0, 0), FALSE);
    CQuadWord totalProgress = CQuadWord(0, 0);

    int ret = TRUE;
    int j;
    for (j = 0; j < files.Count; j++)
    {
        text = SPLFormatStringOwned(L"%s %s...", LangStr(IDS_PROCESSING).c_str(), files[j]);
        salamander->ProgressDialogAddText(text.c_str(), TRUE);

        SAFE_FILE file;
        if (!SalamanderSafeFile->SafeFileOpen(&file, files[j], GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING,
                                              FILE_FLAG_SEQUENTIAL_SCAN, parent, BUTTONS_RETRYCANCEL, NULL, NULL))
        {
            ret = FALSE;
            break;
        }

        DWORD numread, numwr;
        CQuadWord currentProgress = CQuadWord(0, 0), size;
        size.LoDWord = GetFileSize(file.HFile, &size.HiDWord);
        salamander->ProgressSetTotalSize(size, CQuadWord(-1, -1));
        salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE);
        do
        {
            if (!SalamanderSafeFile->SafeFileRead(&file, pBuffer, BUFSIZE, &numread, parent, BUTTONS_RETRYCANCEL, NULL, NULL))
            {
                ret = FALSE;
                break;
            }
            if (!bOnlyCrc && numread)
            {
                if (!SalamanderSafeFile->SafeFileWrite(&outfile, pBuffer, numread, &numwr, parent, BUTTONS_RETRYCANCEL, NULL, NULL))
                {
                    ret = FALSE;
                    break;
                }
            }
            CrcVal = SalamanderGeneral->UpdateCrc32(pBuffer, numread, CrcVal);
            currentProgress += CQuadWord(numread, 0);
            if (!salamander->ProgressSetSize(currentProgress, totalProgress + currentProgress, TRUE))
            {
                ret = FALSE;
                break;
            }
        } while (numread == BUFSIZE);

        totalProgress += currentProgress;
        SalamanderSafeFile->SafeFileClose(&file);
        if (ret == FALSE)
            break;
    }

    salamander->CloseProgressDialog();
    delete[] pBuffer;
    if (!bOnlyCrc)
    {
        if (ret)
        {
            if (bTime)
                SetFileTime(outfile.HFile, NULL, NULL, origTime);
            SalamanderSafeFile->SafeFileClose(&outfile);

            std::wstring targetDirectory = targetName;
            if (SPLCutDirectoryOwned(SalamanderGeneral, targetDirectory))
            {
                SalamanderGeneral->PostChangeOnPathNotification(targetDirectory.c_str(), FALSE);
            }
        }
        else
        {
            SalamanderSafeFile->SafeFileClose(&outfile);
            DeleteFileW(targetName);
        }
    }

    if (!bOnlyCrc)
    {
        if (ret && bTestCrc && Crc != CrcVal)
        {
            SalamanderGeneral->ShowMessageBox(LangStr(IDS_CRCERROR).c_str(), LangStr(idTitle).c_str(), MSGBOX_ERROR);
            ret = FALSE;
        }
    }
    else
        Crc = CrcVal;

    return ret;
}

// *****************************************************************************
//
//  CombineCommand
//

static BOOL AddFile(TIndirectArray<wchar_t>& files, const wchar_t* sourceDir, const wchar_t* name, BOOL bReverse)
{
    CALL_STACK_MESSAGE1("AllocName( , , )");
    std::wstring fullName = sourceDir;
    SPLSalPathAppendOwned(fullName, name);
    wchar_t* str = _wcsdup(fullName.c_str());
    if (str == NULL)
    {
        SalamanderGeneral->ShowMessageBox(LangStr(IDS_OUTOFMEM).c_str(), LangStr(IDS_COMBINE).c_str(), MSGBOX_ERROR);
        return FALSE;
    }
    if (bReverse)
        files.Insert(0, str);
    else
        files.Add(str);
    return TRUE;
}

static BOOL IsInPanel(const wchar_t* fileName)
{
    CALL_STACK_MESSAGE1("IsInPanel()");
    int index = 0;
    const CFileData* pfd;
    BOOL isDir;
    while ((pfd = SalamanderGeneral->GetPanelItem(PANEL_SOURCE, &index, &isDir)) != NULL)
        if (!isDir)
            if (!lstrcmpiW(fileName, pfd->Name))
                return TRUE;
    return FALSE;
}

static BOOL FindValue(const char*& p)
{
    CALL_STACK_MESSAGE1("FindValue()");
    while (*p && *p != '\r' && *p != '\n' && (*p == ' ' || *p == '\t'))
        p++;
    if (*p != '=' && *p != ':')
        return FALSE;
    p++;
    while (*p && *p != '\r' && *p != '\n' && (*p == ' ' || *p == '\t'))
        p++;
    return *p && *p != '\r' && *p != '\n';
}

static BOOL FindCrc(const char* text, const char* searchstring, UINT32& crc)
{
    CALL_STACK_MESSAGE2("FindCrc( , %s, )", searchstring);
    const char* p = strstr(text, searchstring);
    if (p != NULL)
    {
        p += strlen(searchstring);
        if (FindValue(p))
            return sscanf(p, "%x", &crc) == 1;
        else
            return FALSE;
    }
    else
        return FALSE;
}

static BOOL FindName(const char* text, const char* text_locase,
                     const char* searchstring, std::string& name)
{
    CALL_STACK_MESSAGE2("FindName( , %s, )", searchstring);
    const char* p = strstr(text_locase, searchstring);
    if (p != NULL)
    {
        p += strlen(searchstring);
        if (FindValue(p))
        {
            p = text + static_cast<size_t>(p - text_locase);
            if (*p == '\"')
                p++;
            const char* end = p;
            while (*end && *end != '\r' && *end != '\n' && *end != '\"')
                ++end;
            name.assign(p, end);
            return TRUE;
        }
        else
            return FALSE;
    }
    else
        return FALSE;
}

static BOOL FindTime(const char* text, const char* searchstring, FILETIME* ft)
{
    CALL_STACK_MESSAGE2("FindTime( , %s, )", searchstring);
    const char* p = strstr(text, searchstring);
    if (p != NULL)
    {
        p += strlen(searchstring);
        if (FindValue(p))
        {
            SYSTEMTIME st;
            st.wMilliseconds = 0;
            if (sscanf(p, "%hu-%hu-%hu %hu:%hu:%hu", &st.wYear, &st.wMonth, &st.wDay, &st.wHour, &st.wMinute, &st.wSecond) != 6)
                return FALSE;
            return SystemTimeToFileTime(&st, ft);
        }
        else
            return FALSE;
    }
    else
        return FALSE;
}

// 'fileName' is the real on-disk companion .bat/.crc file (wide). Control fields are
// parsed as OEM/ASCII bytes; the semantic output name is decoded once at the named
// protocol boundary before it rejoins the UTF-16 path flow.
static void AnalyzeFile(const wchar_t* fileName, std::wstring& origName,
                        UINT32& origCrc, FILETIME* origTime,
                        BOOL& bNameAcquired, BOOL& bCrcAcquired, BOOL& bTimeAcquired)
{
    CALL_STACK_MESSAGE2("AnalyzeFile(%ls, , , , )", fileName);
    bNameAcquired = bCrcAcquired = FALSE;
    // load the file into a buffer
    HANDLE hFile;
    if ((hFile = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                             FILE_FLAG_SEQUENTIAL_SCAN, NULL)) == INVALID_HANDLE_VALUE)
        return;
    DWORD size = GetFileSize(hFile, NULL), numread;
    // j.r. 21.1.2003: when splitting to 999 parts I received a batch of 30KB
    //  if (size > 10000) { CloseHandle(hFile); return; } // skip such large files
    if (size > 200000)
    {
        CloseHandle(hFile);
        return;
    } // skip such large files
    try
    {
        std::string text(static_cast<size_t>(size), '\0');
        const BOOL ok = ReadFile(hFile, text.data(), size, &numread, NULL);
        CloseHandle(hFile);
        hFile = INVALID_HANDLE_VALUE;
        if (!ok || size != numread)
            return;

        std::string textLocase = text;
        for (char& value : textLocase)
            if (value >= 'A' && value <= 'Z')
                value = static_cast<char>(value - 'A' + 'a');

        // Control keys and numeric fields are ASCII within the OEM file.
        if (!bCrcAcquired)
        {
            bCrcAcquired = FindCrc(textLocase.c_str(), "crc32", origCrc);
            if (!bCrcAcquired)
                bCrcAcquired = FindCrc(textLocase.c_str(), "crc", origCrc);
        }
        if (!bNameAcquired)
        {
            std::string encodedName;
            BOOL found = FindName(text.c_str(), textLocase.c_str(), "filename", encodedName);
            if (!found)
                found = FindName(text.c_str(), textLocase.c_str(), "name", encodedName);
            bNameAcquired = found && DecodeSplitBatchText(encodedName, origName);
        }
        if (!bTimeAcquired)
            bTimeAcquired = FindTime(textLocase.c_str(), "time", origTime);
    }
    catch (...)
    {
        if (hFile != INVALID_HANDLE_VALUE)
            CloseHandle(hFile);
    }
}

BOOL CombineCommand(DWORD eventMask, HWND parent, CSalamanderForOperationsAbstract* salamander)
{
    CALL_STACK_MESSAGE2("CombineCommand(%X, , )", eventMask);

    TIndirectArray<wchar_t> files(100, 100, dtDelete);

    std::wstring sourceDir;
    if (!SPLGetPanelPathOwned(SalamanderGeneral, PANEL_SOURCE, sourceDir))
        return FALSE;

    BOOL bTestCompanionFile = FALSE;
    std::wstring companionFile;
    std::wstring name1, name2;
    const CFileData* pfd;
    BOOL isDir;

    if (eventMask & MENU_EVENT_FILES_SELECTED)
    { // files are selected
        int index = 0;
        BOOL bAllSameNames = TRUE;
        BOOL bFirst = TRUE;

        // load selected items into the array (except directories)
        while ((pfd = SalamanderGeneral->GetPanelSelectedItem(PANEL_SOURCE, &index, &isDir)) != NULL)
        {
            if (!isDir)
            {
                if (bFirst)
                {
                    name1 = pfd->Name;
                    StripExtension(name1);
                    bFirst = FALSE;
                }
                else if (bAllSameNames)
                {
                    name2 = pfd->Name;
                    StripExtension(name2);
                    if (lstrcmpiW(name1.c_str(), name2.c_str()))
                        bAllSameNames = FALSE;
                }
                if (!AddFile(files, sourceDir.c_str(), pfd->Name, FALSE))
                    return FALSE;
            }
        }

        // if all names were identical, there is a chance we will find a companion .BAT or .CRC
        bTestCompanionFile = bAllSameNames;
        if (!bAllSameNames)
            name1 = L"combinedfile";
        companionFile = sourceDir;
        SPLSalPathAppendOwned(companionFile, name1.c_str());
        companionFile.push_back(L'.');
    }
    else // only the focus is on a file - try to extend the selection with "higher" files
    {
        pfd = SalamanderGeneral->GetPanelFocusedItem(PANEL_SOURCE, &isDir);
        if (pfd == NULL || isDir)
        {
            TRACE_E("CombineCommand(): No focus on a file?!?");
            return FALSE;
        }
        BOOL bJustOneFile = FALSE;
        // first analyze the extension
        wchar_t* ext = wcsrchr(pfd->Name, L'.');
        if (ext != NULL) // ".cvspass" is an extension in Windows
        {
            BOOL bZeroPadded, bAddThisFile = TRUE;
            int nextIndex;
            if (!lstrcmpiW(ext, L".tns"))
            { // tns = Turbo Navigator Split - consider it as "000"
                bZeroPadded = FALSE;
                nextIndex = 2;
            }
            else if (!lstrcmpiW(ext, L".bat") || !lstrcmpiW(ext, L".crc"))
            { // Salamander's BAT or WinCommander CRC; this will not work with TN files here
                bZeroPadded = TRUE;
                nextIndex = 1;
                bAddThisFile = FALSE;
            }
            else
            { // is the extension composed of digits?
                BOOL bNumbers = TRUE;
                int numberCount = 0, i = 1;
                while (ext[i])
                    if (ext[i] < L'0' || ext[i] > L'9')
                    {
                        bNumbers = FALSE;
                        break;
                    }
                    else
                    {
                        numberCount++;
                        i++;
                    }
                if (!bNumbers || numberCount > 3)
                    bJustOneFile = TRUE; // strange extension - we will not extend anything
                else
                {
                    bZeroPadded = (ext[1] == L'0');
                    nextIndex = _wtol(ext + 1) + 1;
                }
            }

            name1.assign(pfd->Name, static_cast<size_t>(ext - pfd->Name + 1));
            companionFile = sourceDir;
            SPLSalPathAppendOwned(companionFile, name1.c_str());

            if (!bJustOneFile)
            {
                if (nextIndex > 1)
                {
                    int prevIndex = nextIndex - 2;
                    while (1)
                    {
                        name2 = SPLFormatStringOwned(
                            bZeroPadded ? L"%s%#03ld" : L"%s%ld",
                            name1.c_str(), prevIndex--);
                        if (!IsInPanel(name2.c_str()))
                            break;
                        if (!AddFile(files, sourceDir.c_str(), name2.c_str(), TRUE))
                            return FALSE;
                    }
                }

                name2 = pfd->Name;
                bTestCompanionFile = TRUE;
                do
                {
                    if (bAddThisFile)
                        if (!AddFile(files, sourceDir.c_str(), name2.c_str(), FALSE))
                            return FALSE;
                    name2 = SPLFormatStringOwned(
                        bZeroPadded ? L"%s%#03ld" : L"%s%ld",
                        name1.c_str(), nextIndex++);
                    bAddThisFile = TRUE;
                } while (IsInPanel(name2.c_str()));
            }
        }
        else
        { // missing extension - skip it, we will not extend anything
            bJustOneFile = TRUE;
            companionFile = sourceDir;
            SPLSalPathAppendOwned(companionFile, L"combinedfile");
        }

        if (bJustOneFile)
            if (!AddFile(files, sourceDir.c_str(), pfd->Name, FALSE))
                return FALSE;
    }

    BOOL bName = FALSE, bCrc = FALSE, bTime = FALSE;
    UINT32 origCrc;
    FILETIME origTime;

    if (bTestCompanionFile)
    { // inspect a potential BAT or CRC
        std::wstring parsedName;
        const size_t ext = companionFile.size();
        companionFile.append(L"bat");
        AnalyzeFile(companionFile.c_str(), parsedName, origCrc, &origTime,
                    bName, bCrc, bTime);
        if (!bName || !bCrc)
        {
            companionFile.resize(ext);
            companionFile.append(L"crc");
            AnalyzeFile(companionFile.c_str(), parsedName, origCrc, &origTime,
                        bName, bCrc, bTime);
        }
        companionFile.resize(ext);
        if (bName)
        {
            name2 = std::move(parsedName);
            name1 = sourceDir;
            SPLSalPathAppendOwned(name1, name2.c_str());
        }
    }

    if (!bName)
    { // set a default name
        name1 = companionFile;
        if (!name1.empty())
            name1.pop_back();
        if (name1.find_last_of(L'.') == std::wstring::npos)
            name1.append(L".EXT");
    }

    if (configCombineToOther)
    { // JC - February 2002 - adjustment for combining to the other panel, sorry, a bit of a hack
        // but the previous code did not anticipate it, so I would have had to rewrite it all...
        // This code replaces the path in "name1", which points to the source panel, with the target panel path
        name1 = SalamanderGeneral->SalPathFindFileName(name1.c_str());
        if (!GetTargetDir(name2, NULL, FALSE))
            return FALSE;
        SPLSalPathAppendOwned(name2, name1.c_str());
        name1 = name2;
    }

    if (!CombineDialog(files, name1, bCrc, origCrc, parent, salamander))
        return FALSE;

    if (!GetTargetDir(sourceDir, NULL, FALSE))
        return FALSE;
    if (!MakePathAbsolute(name1, FALSE, sourceDir, !configCombineToOther, IDS_COMBINE))
        return FALSE;

    return CombineFiles(files, name1.c_str(), FALSE, bCrc, origCrc, bTime,
                        &origTime, parent, salamander);
}
