// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <windows.h>

// Command codes for PrepareFTPCommand. FTP commands are protocol bytes; paths
// supplied to these commands have already been encoded by the session codec.
enum CFtpCmdCode
{
    ftpcmdQuit,
    ftpcmdSystem,
    ftpcmdAbort,
    ftpcmdPrintWorkingPath,
    ftpcmdChangeWorkingPath,
    ftpcmdSetTransferMode,
    ftpcmdPassive,
    ftpcmdSetPort,
    ftpcmdNoOperation,
    ftpcmdDeleteFile,
    ftpcmdDeleteDir,
    ftpcmdChangeAttrs,
    ftpcmdChangeAttrsQuoted,
    ftpcmdRestartTransfer,
    ftpcmdRetrieveFile,
    ftpcmdCreateDir,
    ftpcmdRenameFrom,
    ftpcmdRenameTo,
    ftpcmdStoreFile,
    ftpcmdGetSize,
    ftpcmdAppendFile,
};

// Builds the complete wire command, including CRLF. Publication is
// transactional: on failure the supplied strings are unchanged and cmdLen is
// set to zero when present.
BOOL PrepareFTPCommand(std::string& command, std::string* logCommand,
                       CFtpCmdCode ftpCmd, int* cmdLen, ...) noexcept;

// Frozen caller-buffer adapter. It copies only when the complete command and
// log command fit; it never truncates either output.
BOOL PrepareFTPCommand(char* command, int commandSize, char* logCommand,
                       int logCommandSize, CFtpCmdCode ftpCmd, int* cmdLen, ...) noexcept;

// Dynamic formatter for FTP wire/log byte text. Publication is transactional.
BOOL FTPFormatString(std::string& output, const char* format, ...) noexcept;

// Formats a decimal registry/list index without exposing a fixed caller buffer.
// Publication is transactional.
BOOL FTPFormatDecimalIndex(std::wstring& output, int value) noexcept;

// Formats numeric and symbolic UNIX rights without a caller-owned scratch buffer.
BOOL FTPFormatUNIXRights(std::string& output, DWORD attrs) noexcept;

// Builds the raw user-command wire bytes and redacted log command as one
// transaction. Inputs are already encoded in the connection's wire domain;
// localized log presentation remains UTF-16 at the caller.
BOOL FTPBuildUserCommandBytes(std::string_view command,
                              std::string_view commandForLog,
                              std::string& wireCommand,
                              std::string& logCommand) noexcept;

// Escapes quotes in an encoded FTP command argument. Publication is transactional.
BOOL FTPEscapeQuotedCommandArgument(std::string& output, const char* argument) noexcept;
