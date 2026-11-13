// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// The command line under the panels (ExecuteCommandLine): what the user types there while an sftp: panel is active runs
// on the server through SSH "exec", in the panel's current directory. Sally has no terminal for it, so the output
// (and the exit code) is shown in a window when the command has ended.
//
// The command line is not a shell session: every command is a separate "cd '<dir>' && { <command> }" in a fresh shell.
// The braces matter: without them "cd d && rm a; rm b" would run "rm b" even when the cd failed. So "cd x" typed here
// changes nothing (change the directory in the panel). The panel path is never changed from here
// (the SDK warns that the FS could close).

#include "precomp.h"

#include "fs_object.h"
#include "fs_messages.h"

#include "../ui/exec_output_dialog.h"
#include "exec_text.h"
#include "shell_quote.h"
#include "text_util.h"
#include "wait_for_request.h"

namespace
{
// Yes/No, default No. The command runs on the server, not here: make the user see that.
bool ConfirmRun(HWND parent, const CSite& site, const std::wstring& typed)
{
    const std::wstring text = SPLFormatStringOwned(LangStr(IDS_EXEC_CONFIRM).c_str(), HostTextOf(site).c_str(),
                                                   TruncateForDisplay(typed, 300).c_str());
    return SalamanderGeneral->SalMessageBox(parent, text.c_str(), LangStr(IDS_PLUGINNAME).c_str(),
                                            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}

std::wstring Trim(const std::wstring& text)
{
    const wchar_t* blanks = L" \t\r\n";
    const size_t first = text.find_first_not_of(blanks);
    if (first == std::wstring::npos)
        return std::wstring();
    return text.substr(first, text.find_last_not_of(blanks) - first + 1);
}

// "Exit code 3" or "Terminated by signal TERM", plus "Output cut at 1 MB" when the output was longer than the limit.
std::wstring StatusText(const CPanelRequest& request, const CNameCodec& codec)
{
    std::wstring status;
    if (!request.ExitSignal().empty())
        status = SPLFormatStringOwned(LangStr(IDS_EXEC_STATUS_SIGNAL).c_str(), codec.Decode(request.ExitSignal()).c_str());
    else
        status = SPLFormatStringOwned(LangStr(IDS_EXEC_STATUS_EXIT).c_str(), request.ExitStatus());
    if (request.OutputTruncated())
        status = SPLFormatStringOwned(LangStr(IDS_EXEC_STATUS_CUT).c_str(), status.c_str());
    return status;
}
} // namespace

BOOL WINAPI
CPluginFSInterface::ExecuteCommandLine(HWND parent, CSalamanderStringBuffer* command, int& selFrom, int& selTo)
{
    CALL_STACK_MESSAGE1("CPluginFSInterface::ExecuteCommandLine(, , ,)");
    CBusyGuard busy(this); // declared first: destroyed last, and may delete 'this' (see ChangePath)
    selFrom = 0;
    selTo = 0;
    try
    {
        return DoExecuteCommandLine(parent, command, selFrom, selTo);
    }
    catch (...) // no exception may leave an SDK entry point
    {
        TRACE_E("ExecuteCommandLine: unexpected exception.");
        return FALSE;
    }
}

// FALSE: nothing was run, or it was cancelled or failed (Sally keeps the text and does not put it in the history).
// TRUE: the command ran (whatever its exit code); the command line is cleared.
BOOL CPluginFSInterface::DoExecuteCommandLine(HWND parent, CSalamanderStringBuffer* command, int& selFrom, int& selTo)
{
    if (closeRequested_ || !connected_ || IsWaiting() || command == nullptr)
        return FALSE;
    std::wstring typed;
    if (!sally::plugin_abi::ReadStringBuffer(*command, typed))
        return FALSE;
    typed = Trim(typed);
    if (typed.empty())
        return FALSE; // an empty line is not a command

    bool lossy = false;
    const std::string raw = codec_.Encode(typed, &lossy); // a lost character would silently change the command
    if (lossy)
    {
        ShowMessage(parent, LangStr(IDS_ERR_EXEC_ENCODING));
        return FALSE;
    }

    if (!execConfirmed_) // asked once per connection (a new FS object = a new connection)
    {
        if (!ConfirmRun(parent, site_, typed))
            return FALSE; // the command stays in the line
        execConfirmed_ = true;
    }

    std::shared_ptr<CPanelRequest> request = session_->Exec(BuildRemoteCommand(curRaw_, raw));
    const std::wstring waitText = LangStr(IDS_WAIT_EXEC);
    const WaitResult waited = WaitForRequest(parent, *session_, request, waitText);
    if (closeRequested_)
        return FALSE;
    if (waited != WaitResult::Done || request->Cancelled() || !request->IsDone())
    {
        NotifyFolderChanged(true); // the command may have changed files before it was stopped
        return FALSE;
    }
    if (!request->Ok())
    {
        if (request->Error().kind == SshErrorKind::Unsupported)
            ShowMessage(parent, LangStr(IDS_ERR_EXEC_REFUSED));
        else
            ShowMessage(parent, SPLFormatStringOwned(LangStr(IDS_ERR_EXEC).c_str(),
                                                     DescribeError(request->Error()).c_str()));
        return FALSE;
    }

    sally::plugin_abi::WriteStringBuffer(*command, std::wstring()); // the command is done: clear the line
    selFrom = 0;
    selTo = 0;
    NotifyFolderChanged(true); // the command may have changed files
    ShowCommandResult(parent, *request);
    return TRUE;
}

// The window with the output. A command that succeeded without printing anything shows nothing.
void CPluginFSInterface::ShowCommandResult(HWND parent, const CPanelRequest& request) const
{
    const bool failed = request.ExitStatus() != 0 || !request.ExitSignal().empty();
    if (!failed && request.Output().empty())
        return;
    // Invalid bytes become U+FFFD in Decode; OutputToEditText makes the text safe for the edit control.
    const std::wstring text = OutputToEditText(codec_.Decode(request.Output()));
    CExecOutputDialog dialog(parent, text, StatusText(request, codec_));
    dialog.Run();
}
