// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "filter_process.h"
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

static sally::renamer::FilterResult RunFilterWithProgress(HWND parent, const std::wstring& title,
                                                          const std::wstring& commandLine, const wchar_t* directory, HANDLE input, HANDLE output, HANDLE errors)
{
    CProgressDialog dialog(parent, ooStatic);
    if (!dialog.Create())
        return {sally::renamer::FilterState::Failed, GetLastError()};
    struct WindowOwner
    {
        HWND Parent, Progress;
        BOOL WasEnabled;
        ~WindowOwner()
        {
            EnableWindow(Parent, WasEnabled);
            DestroyWindow(Progress);
        }
    } owner{parent, dialog.HWindow, IsWindowEnabled(parent)};
    EnableWindow(parent, FALSE);
    dialog.SetText(title.c_str());
    dialog.Update(0, TRUE);
    class ProgressAdapter final : public sally::renamer::IFilterProgress
    {
        CProgressDialog& Dialog;

    public:
        explicit ProgressAdapter(CProgressDialog& dialog) : Dialog(dialog) {}
        bool KeepRunning() override { return !Dialog.Update(0); }
    } progress(dialog);
    return sally::renamer::RunFilterProcess(commandLine, directory, input, output, errors, progress);
}

BOOL CRenamerDialog::ExecuteCommand(const char* command)
{
    CALL_STACK_MESSAGE2("CRenamerDialog::ExecuteCommand(%s)", command);
    // create the command line
    std::wstring shell;
    if (!GetEnvironmentValueOwned(L"SHELL", shell))
    {
        if (!GetEnvironmentValueOwned(L"COMSPEC", shell))
            return ErrorL(ERROR_ENVVAR_NOT_FOUND, IDS_PROCESS);
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
    if (cmdLine.size() >= sally::renamer::WindowsCommandLineLimit)
        return Error(IDS_LONGDATA);

    std::wstring tempDir;
    std::wstring inName, outName, errName;
    HANDLE inFile = INVALID_HANDLE_VALUE;
    DWORD exitCode = 0;
    HANDLE outFile = INVALID_HANDLE_VALUE;
    HANDLE errFile = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES saAttr;
    TBuffer<char> buffer;
    TBuffer<wchar_t> editBuffer;
    std::string inputText;
    CQuadWord size;
    BOOL ret = TRUE;

    // so the handles can be inherited
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.lpSecurityDescriptor = NULL;
    saAttr.bInheritHandle = TRUE;

    // create names for the tmp files
    if (!SPLSalGetTempFileNameOwned(SG, NULL, L"SAL", tempDir, FALSE, NULL))
        return Error(IDS_CREATETEMP);
    inName = tempDir;
    SPLSalPathAppendOwned(inName, L"stdin");
    outName = tempDir;
    errName = tempDir;
    SPLSalPathAppendOwned(outName, L"stdout");
    SPLSalPathAppendOwned(errName, L"stderr");

    // A finite spool file cannot block on a child that refuses to read stdin.
    inFile = CreateFileW(inName.c_str(), GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ, &saAttr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (inFile == INVALID_HANDLE_VALUE)
    {
        ret = Error(IDS_CREATETEMP);
        goto LERROR;
    }

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
        ret = Error(IDS_LOWMEM);
        goto LERROR;
    }
    const int editChars = GetWindowTextW(ManualEdit->HWindow, editBuffer.Get(), (int)editBuffer.GetSize());
    inputText = WideToRenamerText(editBuffer.Get(), editChars);

    {
        size_t position = 0;
        while (position < inputText.size())
        {
            const DWORD count = static_cast<DWORD>((std::min)(inputText.size() - position, size_t{1024 * 1024}));
            DWORD written = 0;
            if (!WriteFile(inFile, inputText.data() + position, count, &written, NULL) || written != count)
            {
                ret = Error(IDS_WRITETEMP);
                goto LERROR;
            }
            position += written;
        }
        CloseHandle(inFile);
        inFile = CreateFileW(inName.c_str(), GENERIC_READ, FILE_SHARE_READ,
                             &saAttr, OPEN_EXISTING, FILE_ATTRIBUTE_TEMPORARY, NULL);
        if (inFile == INVALID_HANDLE_VALUE)
        {
            ret = Error(IDS_READTEMP);
            goto LERROR;
        }
        const auto execution = RunFilterWithProgress(HWindow, commandW, cmdLine,
                                                     Root.empty() ? NULL : Root.c_str(), inFile, outFile, errFile);
        if (execution.State != sally::renamer::FilterState::Completed)
        {
            ret = execution.State == sally::renamer::FilterState::Failed ? ErrorL(execution.Error, IDS_PROCESS) : FALSE;
            goto LERROR;
        }
        exitCode = execution.ExitCode;
    }

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
    if (inFile != INVALID_HANDLE_VALUE)
        CloseHandle(inFile);
    if (outFile != INVALID_HANDLE_VALUE)
        CloseHandle(outFile);
    if (errFile != INVALID_HANDLE_VALUE)
        CloseHandle(errFile);
    if (!tempDir.empty())
        SG->RemoveTemporaryDir(tempDir.c_str());

    return ret;
}
