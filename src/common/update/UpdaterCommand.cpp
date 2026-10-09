// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "update/UpdaterCommand.h"

#include "ExtendedLengthPath.h"
#include "Win32TextCodec.h"

namespace Sally::Update
{

namespace
{

std::string ToUtf8(const std::wstring& text)
{
    std::string utf8;
    Win32EncodeText(CP_UTF8, text, utf8);
    return utf8;
}

std::wstring FromUtf8(const std::string& text)
{
    std::wstring wide;
    Win32DecodeText(CP_UTF8, text, wide);
    return wide;
}

// Values are one line each: backslash and line breaks are escaped.
std::string Escape(const std::wstring& value)
{
    std::string out;
    for (char ch : ToUtf8(value))
    {
        if (ch == '\\')
            out += "\\\\";
        else if (ch == '\n')
            out += "\\n";
        else if (ch == '\r')
            out += "\\r";
        else
            out.push_back(ch);
    }
    return out;
}

std::wstring Unescape(const std::string& value)
{
    std::string out;
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '\\' && i + 1 < value.size())
        {
            const char next = value[++i];
            out.push_back(next == 'n' ? '\n' : next == 'r' ? '\r' : next);
        }
        else
        {
            out.push_back(value[i]);
        }
    }
    return FromUtf8(out);
}

const char* StatusName(UpdateStatus status)
{
    switch (status)
    {
    case UpdateStatus::Prepared:
        return "prepared";
    case UpdateStatus::Updated:
        return "updated";
    case UpdateStatus::Aborted:
        return "aborted";
    default:
        return "failed";
    }
}

bool ParsePids(const std::wstring& text, std::vector<DWORD>& pids)
{
    pids.clear();
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find(L',', start);
        if (end == std::wstring::npos)
            end = text.size();
        const std::wstring part = text.substr(start, end - start);
        if (part.empty() || part.size() > 10 || part.find_first_not_of(L"0123456789") != std::wstring::npos)
            return false;
        const unsigned long long value = std::stoull(part);
        if (value == 0 || value > 0xFFFFFFFFull)
            return false;
        pids.push_back(static_cast<DWORD>(value));
        start = end + 1;
    }
    return !pids.empty();
}

} // namespace

std::wstring QuoteCommandLineArgument(const std::wstring& argument)
{
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos)
        return argument;
    std::wstring quoted = L"\"";
    for (size_t i = 0;; ++i)
    {
        size_t backslashes = 0;
        while (i < argument.size() && argument[i] == L'\\')
        {
            ++i;
            ++backslashes;
        }
        if (i == argument.size())
        {
            quoted.append(backslashes * 2, L'\\');
            break;
        }
        if (argument[i] == L'"')
        {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'"');
        }
        else
        {
            quoted.append(backslashes, L'\\');
            quoted.push_back(argument[i]);
        }
    }
    quoted.push_back(L'"');
    return quoted;
}

bool ParseUpdaterCommand(const std::vector<std::wstring>& args, UpdaterCommand& command, std::wstring& error)
{
    command = UpdaterCommand();
    error.clear();
    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::wstring& name = args[i];
        auto value = [&](std::wstring& target) {
            if (i + 1 >= args.size() || args[i + 1].empty())
            {
                error = name + L" needs a value";
                return false;
            }
            target = args[++i];
            return true;
        };
        if (!name.empty() && name.compare(0, 2, L"--") != 0)
        {
            // A package given by hand (or dropped onto the program).
            if (command.Mode != UpdaterMode::None || !command.Package.empty())
            {
                error = L"only one package";
                return false;
            }
            command.Mode = UpdaterMode::Manual;
            command.Package = name;
        }
        else if (name == L"--prepare" || name == L"--apply")
        {
            if (command.Mode != UpdaterMode::None)
            {
                error = L"only one of --prepare and --apply";
                return false;
            }
            command.Mode = name == L"--prepare" ? UpdaterMode::Prepare : UpdaterMode::Apply;
        }
        else if (name == L"--package")
        {
            if (!value(command.Package))
                return false;
        }
        else if (name == L"--sha256")
        {
            if (!value(command.Sha256))
                return false;
        }
        else if (name == L"--staging")
        {
            if (!value(command.Staging))
                return false;
        }
        else if (name == L"--version")
        {
            if (!value(command.Version))
                return false;
        }
        else if (name == L"--arch")
        {
            if (!value(command.Arch))
                return false;
        }
        else if (name == L"--install")
        {
            if (!value(command.InstallRoot))
                return false;
        }
        else if (name == L"--from")
        {
            if (!value(command.FromVersion))
                return false;
        }
        else if (name == L"--to")
        {
            if (!value(command.ToVersion))
                return false;
        }
        else if (name == L"--result")
        {
            if (!value(command.ResultFile))
                return false;
        }
        else if (name == L"--wait")
        {
            std::wstring pids;
            if (!value(pids))
                return false;
            if (!ParsePids(pids, command.WaitPids))
            {
                error = L"--wait needs process ids separated by commas";
                return false;
            }
        }
        else if (name == L"--no-relaunch")
        {
            command.Relaunch = false;
        }
        else if (name == L"--elevated")
        {
            command.Elevated = true;
        }
        else if (name == L"--notify")
        {
            command.Notify = true;
        }
        else if (name == L"--yes")
        {
            command.Yes = true;
        }
        else
        {
            error = L"unknown argument " + name;
            return false;
        }
    }

    auto require = [&](const std::wstring& field, const wchar_t* name) {
        if (field.empty())
        {
            error = std::wstring(name) + L" is required";
            return false;
        }
        return true;
    };
    switch (command.Mode)
    {
    case UpdaterMode::Prepare:
        return require(command.Package, L"--package") && require(command.Sha256, L"--sha256") &&
               require(command.Staging, L"--staging") && require(command.Version, L"--version") &&
               require(command.Arch, L"--arch") && require(command.ResultFile, L"--result");
    case UpdaterMode::Apply:
        return require(command.Staging, L"--staging") && require(command.InstallRoot, L"--install") &&
               require(command.ResultFile, L"--result");
    default:
        // Nothing but options: a manual run without a package (it recovers an interrupted update).
        if (command.Mode == UpdaterMode::None && command.Package.empty() && command.Staging.empty() &&
            command.ResultFile.empty() && command.WaitPids.empty())
        {
            command.Mode = UpdaterMode::Manual;
            return true;
        }
        if (command.Mode == UpdaterMode::Manual)
            return true;
        error = L"--prepare or --apply is required";
        return false;
    }
}

std::wstring FormatUpdaterCommand(const UpdaterCommand& command)
{
    std::wstring line;
    auto add = [&](const wchar_t* name, const std::wstring& value) {
        if (value.empty())
            return;
        if (!line.empty())
            line += L' ';
        line += name;
        line += L' ';
        line += QuoteCommandLineArgument(value);
    };
    auto flag = [&](const wchar_t* name) {
        if (!line.empty())
            line += L' ';
        line += name;
    };
    if (command.Mode == UpdaterMode::Prepare)
        flag(L"--prepare");
    else if (command.Mode == UpdaterMode::Apply)
        flag(L"--apply");
    add(L"--package", command.Package);
    add(L"--sha256", command.Sha256);
    add(L"--staging", command.Staging);
    add(L"--version", command.Version);
    add(L"--arch", command.Arch);
    add(L"--install", command.InstallRoot);
    std::wstring pids;
    for (DWORD pid : command.WaitPids)
    {
        if (!pids.empty())
            pids += L',';
        pids += std::to_wstring(pid);
    }
    add(L"--wait", pids);
    add(L"--from", command.FromVersion);
    add(L"--to", command.ToVersion);
    add(L"--result", command.ResultFile);
    if (!command.Relaunch)
        flag(L"--no-relaunch");
    if (command.Elevated)
        flag(L"--elevated");
    if (command.Notify)
        flag(L"--notify");
    if (command.Yes)
        flag(L"--yes");
    return line;
}

std::string FormatUpdateResult(const UpdateResult& result)
{
    std::string text = "sally-update-result 1\r\n";
    auto add = [&](const char* key, const std::string& value) {
        text += key;
        text += '=';
        text += value;
        text += "\r\n";
    };
    add("status", StatusName(result.Status));
    add("install", Escape(result.InstallRoot));
    add("from", Escape(result.FromVersion));
    add("to", Escape(result.ToVersion));
    add("path", Escape(result.FailedPath));
    add("code", std::to_string(result.Code));
    add("message", Escape(result.Message));
    add("rolledback", result.RolledBack ? "1" : "0");
    add("rollbackcomplete", result.RollbackComplete ? "1" : "0");
    for (const std::wstring& leftover : result.Leftovers)
        add("leftover", Escape(leftover));
    return text;
}

bool ParseUpdateResult(const std::string& text, UpdateResult& result)
{
    result = UpdateResult();
    size_t start = 0;
    bool header = false;
    bool status = false;
    while (start < text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        if (!header)
        {
            if (line != "sally-update-result 1")
                return false;
            header = true;
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string::npos)
            return false;
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key == "status")
        {
            status = true;
            if (value == "prepared")
                result.Status = UpdateStatus::Prepared;
            else if (value == "updated")
                result.Status = UpdateStatus::Updated;
            else if (value == "aborted")
                result.Status = UpdateStatus::Aborted;
            else if (value == "failed")
                result.Status = UpdateStatus::Failed;
            else
                return false;
        }
        else if (key == "install")
            result.InstallRoot = Unescape(value);
        else if (key == "from")
            result.FromVersion = Unescape(value);
        else if (key == "to")
            result.ToVersion = Unescape(value);
        else if (key == "path")
            result.FailedPath = Unescape(value);
        else if (key == "code")
            result.Code = static_cast<DWORD>(strtoul(value.c_str(), NULL, 10));
        else if (key == "message")
            result.Message = Unescape(value);
        else if (key == "rolledback")
            result.RolledBack = value == "1";
        else if (key == "rollbackcomplete")
            result.RollbackComplete = value == "1";
        else if (key == "leftover")
            result.Leftovers.push_back(Unescape(value));
        // Unknown keys are left for newer versions.
    }
    return header && status;
}

bool WriteUpdateResult(const std::wstring& path, const UpdateResult& result)
{
    const std::string text = FormatUpdateResult(result);
    const std::wstring temporary = path + L".tmp";
    HANDLE file = CreateFileW(ToExtendedLengthPath(temporary).c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, NULL) &&
                    written == text.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok)
    {
        DeleteFileW(ToExtendedLengthPath(temporary).c_str());
        return false;
    }
    return MoveFileExW(ToExtendedLengthPath(temporary).c_str(), ToExtendedLengthPath(path).c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

bool ReadUpdateResult(const std::wstring& path, UpdateResult& result)
{
    HANDLE file = CreateFileW(ToExtendedLengthPath(path).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    std::string text(64 * 1024, '\0');
    DWORD read = 0;
    const bool ok = ReadFile(file, &text[0], static_cast<DWORD>(text.size()), &read, NULL) != FALSE;
    CloseHandle(file);
    if (!ok)
        return false;
    text.resize(read);
    return ParseUpdateResult(text, result);
}

} // namespace Sally::Update
