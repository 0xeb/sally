// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <vector>

static void RemoveTemporaryFileDirectory(const std::wstring& fileName)
{
    std::wstring directory = fileName;
    if (SPLCutDirectoryOwned(SG, directory))
        SG->RemoveTemporaryDir(directory.c_str());
}

static BOOL GetEnvironmentValueOwned(const wchar_t* name, std::wstring& value)
{
    DWORD needed = GetEnvironmentVariableW(name, NULL, 0);
    if (needed == 0)
        return FALSE;
    std::vector<wchar_t> buffer(needed, L'\0');
    const DWORD length = GetEnvironmentVariableW(name, buffer.data(), needed);
    if (length == 0 || length >= needed)
        return FALSE;
    value.assign(buffer.data(), length);
    return TRUE;
}

BOOL CRenamerDialog::ExportToTempFile()
{
    CALL_STACK_MESSAGE1("CRenamerDialog::ExportToTempFile()");

    // create the name of the tmp file
    if (!SPLSalGetTempFileNameOwned(SG, NULL, L"SAL", TempFile, FALSE, NULL))
        return Error(IDS_CREATETEMP);
    SPLSalPathAppendOwned(TempFile, L"list.txt");

    // create/open the tmp file
    HANDLE file = CreateFileW(TempFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        RemoveTemporaryFileDirectory(TempFile);
        return Error(IDS_CREATETEMP);
    }

    // nacteme text z controlu
    TBuffer<wchar_t> buffer;
    if (!buffer.Reserve(GetWindowTextLengthW(ManualEdit->HWindow) + 1))
    {
        RemoveTemporaryFileDirectory(TempFile);
        return Error(IDS_LOWMEM);
    }
    const int chars = GetWindowTextW(ManualEdit->HWindow, buffer.Get(), (int)buffer.GetSize());
    const std::string text = WideToRenamerText(buffer.Get(), chars);

    DWORD written;
    BOOL b = WriteFile(file, text.data(), (DWORD)text.size(), &written, NULL) && written == text.size();

    CloseHandle(file);

    if (!b)
    {
        RemoveTemporaryFileDirectory(TempFile);
        Error(IDS_WRITETEMP);
    }

    return b;
}

BOOL CRenamerDialog::ImportFromTempFile()
{
    CALL_STACK_MESSAGE1("CRawEditValDialog::ImportFromTempFile()");
    // open the tmp file
    HANDLE file = CreateFileW(TempFile.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return Error(IDS_OPENTEMP);

    CQuadWord size;
    DWORD err;
    if (!SG->SalGetFileSize(file, size, err))
    {
        CloseHandle(file);
        return ErrorL(err, IDS_SIZEOFTEMP);
    }

    if (size.HiDWord > 0)
    {
        CloseHandle(file);
        return Error(IDS_LONGDATA);
    }

    TBuffer<char> buffer;
    if (!buffer.Reserve(size.LoDWord + 1))
    {
        CloseHandle(file);
        return Error(IDS_LOWMEM);
    }

    DWORD read;
    BOOL b = ReadFile(file, buffer.Get(), size.LoDWord, &read, NULL) && read == size.LoDWord;

    CloseHandle(file);

    if (b)
    {
        buffer.Get()[size.LoDWord] = 0;
        const std::wstring text = RenamerTextToWide(buffer.Get(), (int)size.LoDWord);
        SendMessageW(ManualEdit->HWindow, EM_SETSEL, 0, -1);
        SendMessageW(ManualEdit->HWindow, EM_REPLACESEL, FALSE, LPARAM(text.c_str()));
    }
    else
        Error(IDS_READTEMP);

    return b;
}

static std::wstring EscapeQuotes(const wchar_t* string)
{
    std::wstring escaped;
    while (*string)
    {
        if (*string == L'"')
            escaped.push_back(L'\\');
        escaped.push_back(*string++);
    }
    return escaped;
}

BOOL CRenamerDialog::ExecuteCommand(const char* command)
{
    CALL_STACK_MESSAGE2("CRenamerDialog::ExecuteCommand(%s)", command);
    // create the command line
    std::wstring shell;
    if (!GetEnvironmentValueOwned(L"SHELL", shell))
    {
        if (!GetEnvironmentValueOwned(L"COMSPEC", shell))
            return FALSE;
    }

    const wchar_t* shellName = wcsrchr(shell.c_str(), L'\\');
    shellName = shellName ? shellName + 1 : shell.c_str();
    std::wstring shellNameLower(shellName);
    CharLowerBuffW(shellNameLower.data(), (DWORD)shellNameLower.size());
    BOOL sh = !shellNameLower.empty() && shellNameLower.find(L"sh") != std::wstring::npos;

    const std::wstring commandW = RenamerTextToWide(command);
    std::wstring cmdLine;
    if (sh)
    {
        cmdLine = L"\"" + shell + L"\" -c \"" + EscapeQuotes(commandW.c_str()) + L"\"";
    }
    else
        cmdLine = L"\"" + shell + L"\" /C " + commandW;
    constexpr size_t WindowsCommandLineLimit = 32767;
    if (cmdLine.size() >= WindowsCommandLineLimit)
        return Error(IDS_LONGDATA);

    std::wstring tempDir;
    std::wstring outName, errName;
    HANDLE inPipeWr = INVALID_HANDLE_VALUE;
    HANDLE inPipeWrDup = INVALID_HANDLE_VALUE;
    HANDLE inPipeRd = INVALID_HANDLE_VALUE;
    HANDLE outFile = INVALID_HANDLE_VALUE;
    HANDLE errFile = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES saAttr;
    TBuffer<char> buffer;
    TBuffer<wchar_t> editBuffer;
    std::string inputText;
    std::vector<wchar_t> mutableCmdLine;
    CQuadWord size;
    BOOL ret = TRUE;

    // so the handles can be inherited
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.lpSecurityDescriptor = NULL;
    saAttr.bInheritHandle = TRUE;

    // create names for the tmp files
    if (!SPLSalGetTempFileNameOwned(SG, NULL, L"SAL", tempDir, FALSE, NULL))
        return Error(IDS_CREATETEMP);
    outName = tempDir;
    errName = tempDir;
    SPLSalPathAppendOwned(outName, L"stdout");
    SPLSalPathAppendOwned(errName, L"stderr");

    // create the pipe for input
    if (!CreatePipe(&inPipeRd, &inPipeWr, &saAttr, 0))
    {
        inPipeWr = INVALID_HANDLE_VALUE;
        inPipeRd = INVALID_HANDLE_VALUE;
        ret = Error(IDS_CREATEPIPE);
        goto LERROR;
    }
    if (!DuplicateHandle(GetCurrentProcess(), inPipeWr,
                         GetCurrentProcess(), &inPipeWrDup, 0,
                         FALSE,
                         DUPLICATE_SAME_ACCESS))
        return FALSE;
    CloseHandle(inPipeWr);
    inPipeWr = INVALID_HANDLE_VALUE;

    // create/open tmp files for output
    outFile = CreateFileW(outName.c_str(), GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, &saAttr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_TEMPORARY, NULL);
    errFile = CreateFileW(errName.c_str(), GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, &saAttr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (outFile == INVALID_HANDLE_VALUE ||
        errFile == INVALID_HANDLE_VALUE)
    {
        ret = Error(IDS_CREATETEMP);
        goto LERROR;
    }

    // nacteme text z controlu
    if (!editBuffer.Reserve(GetWindowTextLengthW(ManualEdit->HWindow) + 1))
    {
        RemoveTemporaryFileDirectory(TempFile);
        ret = Error(IDS_LOWMEM);
        goto LERROR;
    }
    const int editChars = GetWindowTextW(ManualEdit->HWindow, editBuffer.Get(), (int)editBuffer.GetSize());
    inputText = WideToRenamerText(editBuffer.Get(), editChars);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    si.lpTitle = &cmdLine[0];
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_SHOWMINIMIZED;
    si.hStdInput = inPipeRd;
    si.hStdOutput = outFile;
    si.hStdError = errFile;

    mutableCmdLine.assign(cmdLine.begin(), cmdLine.end());
    mutableCmdLine.push_back(L'\0');
    if (!CreateProcessW(NULL, mutableCmdLine.data(), NULL, NULL, TRUE,
                       CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS,
                       NULL, Root.empty() ? NULL : Root.c_str(), &si, &pi))
    {
        ret = Error(IDS_PROCESS);
        goto LERROR;
    }

    CloseHandle(inPipeRd);
    inPipeRd = INVALID_HANDLE_VALUE;

    DWORD written;
    if (!WriteFile(inPipeWrDup, inputText.data(), (DWORD)inputText.size(), &written, NULL) || written != inputText.size())
    {
        if (GetLastError() != ERROR_BROKEN_PIPE)
            ret = Error(IDS_WRITEPIPE);
    }

    CloseHandle(inPipeWrDup);
    inPipeWrDup = INVALID_HANDLE_VALUE;

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // verify that no error occurred
    size.HiDWord = 0;
    size.LoDWord = SetFilePointer(errFile, 0, LPLONG(&size.HiDWord), FILE_END);
    if (exitCode || size.LoDWord)
    {
        if (!buffer.Reserve(size.LoDWord + 1))
        {
            ret = Error(IDS_LOWMEM);
            goto LERROR;
        }

        LONG l;
        l = 0;
        SetFilePointer(errFile, 0, &l, FILE_BEGIN);

        DWORD read;
        if (ReadFile(errFile, buffer.Get(), size.LoDWord, &read, NULL) && read == size.LoDWord)
        {
            buffer.Get()[size.LoDWord] = 0;
            const std::string commandText = WideToRenamerText(cmdLine.c_str());
            ret = CCommandErrorDialog(HWindow, commandText.c_str(), exitCode, buffer.Get()).Execute() == IDOK;
            if (!ret)
                goto LERROR;
        }
        else
        {
            ret = Error(IDS_READTEMP);
            goto LERROR;
        }
    }

    if (!ret)
        goto LERROR;

    // read the text from the file
    size.HiDWord = 0;
    size.LoDWord = SetFilePointer(outFile, 0, LPLONG(&size.HiDWord), FILE_END);

    if (size.HiDWord > 0)
    {
        ret = Error(IDS_LONGDATA);
        goto LERROR;
    }

    if (!buffer.Reserve(size.LoDWord + 1))
    {
        ret = Error(IDS_LOWMEM);
        goto LERROR;
    }

    LONG l;
    l = 0;
    SetFilePointer(outFile, 0, &l, FILE_BEGIN);

    DWORD read;
    if (ReadFile(outFile, buffer.Get(), size.LoDWord, &read, NULL) && read == size.LoDWord)
    {
        buffer.Get()[size.LoDWord] = 0;
        const std::wstring text = RenamerTextToWide(buffer.Get(), (int)size.LoDWord);
        SendMessageW(ManualEdit->HWindow, EM_SETSEL, 0, -1);
        SendMessageW(ManualEdit->HWindow, EM_REPLACESEL, TRUE, LPARAM(text.c_str()));
    }
    else
    {
        ret = Error(IDS_READTEMP);
        goto LERROR;
    }

LERROR:
    if (inPipeWr != INVALID_HANDLE_VALUE)
        CloseHandle(inPipeWr);
    if (inPipeRd != INVALID_HANDLE_VALUE)
        CloseHandle(inPipeRd);
    if (inPipeWrDup != INVALID_HANDLE_VALUE)
        CloseHandle(inPipeWrDup);
    if (outFile != INVALID_HANDLE_VALUE)
        CloseHandle(outFile);
    if (errFile != INVALID_HANDLE_VALUE)
        CloseHandle(errFile);
    if (!tempDir.empty())
        SG->RemoveTemporaryDir(tempDir.c_str());

    return ret;
}
