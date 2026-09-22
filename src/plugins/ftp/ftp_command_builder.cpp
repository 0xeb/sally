// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ftp_command_builder.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace
{
BOOL PrepareFTPCommandV(std::string& command, std::string* logCommand,
                        CFtpCmdCode ftpCmd, int* cmdLen, va_list args) noexcept
{
    const char* format = nullptr;
    std::string specializedFormat;
    switch (ftpCmd)
    {
    case ftpcmdQuit: format = "QUIT"; break;
    case ftpcmdSystem: format = "SYST"; break;
    case ftpcmdAbort: format = "ABOR"; break;
    case ftpcmdPrintWorkingPath: format = "PWD"; break;
    case ftpcmdNoOperation: format = "NOOP"; break;
    case ftpcmdChangeWorkingPath: format = "CWD %s"; break;
    case ftpcmdSetTransferMode:
        format = va_arg(args, BOOL) ? "TYPE A" : "TYPE I";
        break;
    case ftpcmdPassive: format = "PASV"; break;
    case ftpcmdSetPort:
    {
        const DWORD ip = va_arg(args, DWORD);
        const unsigned short port = static_cast<unsigned short>(va_arg(args, int));
        if (!FTPFormatString(specializedFormat, "PORT %u,%u,%u,%u,%d,%d",
                             (ip & 0xff), ((ip >> 8) & 0xff), ((ip >> 16) & 0xff),
                             ((ip >> 24) & 0xff), ((port >> 8) & 0xff), (port & 0xff)))
        {
            if (cmdLen != nullptr)
                *cmdLen = 0;
            return FALSE;
        }
        format = specializedFormat.c_str();
        break;
    }
    case ftpcmdDeleteFile: format = "DELE %s"; break;
    case ftpcmdDeleteDir: format = "RMD %s"; break;
    case ftpcmdChangeAttrs: format = "SITE CHMOD %03o %s"; break;
    case ftpcmdChangeAttrsQuoted: format = "SITE CHMOD %03o \"%s\""; break;
    case ftpcmdRestartTransfer: format = "REST %s"; break;
    case ftpcmdRetrieveFile: format = "RETR %s"; break;
    case ftpcmdStoreFile: format = "STOR %s"; break;
    case ftpcmdAppendFile: format = "APPE %s"; break;
    case ftpcmdCreateDir: format = "MKD %s"; break;
    case ftpcmdRenameFrom: format = "RNFR %s"; break;
    case ftpcmdRenameTo: format = "RNTO %s"; break;
    case ftpcmdGetSize: format = "SIZE %s"; break;
    default:
        if (cmdLen != nullptr)
            *cmdLen = 0;
        return FALSE;
    }

    va_list measureArgs;
    va_copy(measureArgs, args);
    const int payloadLength = _vscprintf(format, measureArgs);
    va_end(measureArgs);
    if (payloadLength < 0 || payloadLength > (std::numeric_limits<int>::max)() - 2)
    {
        if (cmdLen != nullptr)
            *cmdLen = 0;
        return FALSE;
    }

    try
    {
        std::string staged(static_cast<size_t>(payloadLength) + 1, '\0');
        va_list writeArgs;
        va_copy(writeArgs, args);
        const int written = _vsnprintf_s(staged.data(), staged.size(), _TRUNCATE,
                                         format, writeArgs);
        va_end(writeArgs);
        if (written != payloadLength)
        {
            if (cmdLen != nullptr)
                *cmdLen = 0;
            return FALSE;
        }
        staged.resize(static_cast<size_t>(payloadLength));
        staged.append("\r\n");

        std::string stagedLog;
        if (logCommand != nullptr)
            stagedLog = staged;
        command.swap(staged);
        if (logCommand != nullptr)
            logCommand->swap(stagedLog);
        if (cmdLen != nullptr)
            *cmdLen = payloadLength + 2;
        return TRUE;
    }
    catch (const std::bad_alloc&)
    {
    }
    catch (const std::length_error&)
    {
    }
    if (cmdLen != nullptr)
        *cmdLen = 0;
    return FALSE;
}

BOOL FTPFormatStringV(std::string& output, const char* format, va_list args) noexcept
{
    if (format == nullptr)
        return FALSE;
    va_list measureArgs;
    va_copy(measureArgs, args);
    const int length = _vscprintf(format, measureArgs);
    va_end(measureArgs);
    if (length < 0)
        return FALSE;
    try
    {
        std::string staged(static_cast<size_t>(length) + 1, '\0');
        va_list writeArgs;
        va_copy(writeArgs, args);
        const int written = _vsnprintf_s(staged.data(), staged.size(), _TRUNCATE,
                                         format, writeArgs);
        va_end(writeArgs);
        if (written != length)
            return FALSE;
        staged.resize(static_cast<size_t>(length));
        output.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}
}

BOOL PrepareFTPCommand(std::string& command, std::string* logCommand,
                       CFtpCmdCode ftpCmd, int* cmdLen, ...) noexcept
{
    va_list args;
    va_start(args, cmdLen);
    const BOOL result = PrepareFTPCommandV(command, logCommand, ftpCmd, cmdLen, args);
    va_end(args);
    return result;
}

BOOL PrepareFTPCommand(char* command, int commandSize, char* logCommand,
                       int logCommandSize, CFtpCmdCode ftpCmd, int* cmdLen, ...) noexcept
{
    va_list args;
    va_start(args, cmdLen);
    std::string stagedCommand;
    std::string stagedLog;
    const BOOL built = PrepareFTPCommandV(stagedCommand,
                                          logCommand != nullptr ? &stagedLog : nullptr,
                                          ftpCmd, cmdLen, args);
    va_end(args);

    if (!built || command == nullptr || commandSize <= 0 ||
        stagedCommand.size() >= static_cast<size_t>(commandSize) ||
        logCommand != nullptr &&
            (logCommandSize <= 0 || stagedLog.size() >= static_cast<size_t>(logCommandSize)))
    {
        if (command != nullptr && commandSize > 0)
            command[0] = '\0';
        if (logCommand != nullptr && logCommandSize > 0)
            logCommand[0] = '\0';
        if (cmdLen != nullptr)
            *cmdLen = 0;
        return FALSE;
    }

    memcpy(command, stagedCommand.c_str(), stagedCommand.size() + 1);
    if (logCommand != nullptr)
        memcpy(logCommand, stagedLog.c_str(), stagedLog.size() + 1);
    return TRUE;
}

BOOL FTPFormatString(std::string& output, const char* format, ...) noexcept
{
    va_list args;
    va_start(args, format);
    const BOOL result = FTPFormatStringV(output, format, args);
    va_end(args);
    return result;
}

BOOL FTPFormatDecimalIndex(std::wstring& output, int value) noexcept
{
    try
    {
        std::wstring staged = std::to_wstring(value);
        output.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL FTPFormatUNIXRights(std::string& output, DWORD attrs) noexcept
{
    std::string staged;
    if (!FTPFormatString(staged, "%03o (", attrs))
        return FALSE;
    try
    {
        staged.push_back((attrs & 0400) ? 'r' : '-');
        staged.push_back((attrs & 0200) ? 'w' : '-');
        staged.push_back((attrs & 0100) ? 'x' : '-');
        staged.push_back((attrs & 0040) ? 'r' : '-');
        staged.push_back((attrs & 0020) ? 'w' : '-');
        staged.push_back((attrs & 0010) ? 'x' : '-');
        staged.push_back((attrs & 0004) ? 'r' : '-');
        staged.push_back((attrs & 0002) ? 'w' : '-');
        staged.push_back((attrs & 0001) ? 'x' : '-');
        staged.push_back(')');
        output.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL FTPBuildUserCommandBytes(std::string_view command,
                              std::string_view commandForLog,
                              std::string& wireCommand,
                              std::string& logCommand) noexcept
{
    try
    {
        std::string stagedWire(command);
        std::string stagedLogCommand(commandForLog);
        stagedWire.append("\r\n");
        stagedLogCommand.append("\r\n");
        wireCommand.swap(stagedWire);
        logCommand.swap(stagedLogCommand);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}

BOOL FTPEscapeQuotedCommandArgument(std::string& output, const char* argument) noexcept
{
    if (argument == nullptr)
        return FALSE;
    try
    {
        std::string staged;
        staged.reserve(strlen(argument));
        for (const char* current = argument; *current != '\0'; ++current)
        {
            if (*current == '"')
                staged.push_back('\\');
            staged.push_back(*current);
        }
        output.swap(staged);
        return TRUE;
    }
    catch (...)
    {
        return FALSE;
    }
}
