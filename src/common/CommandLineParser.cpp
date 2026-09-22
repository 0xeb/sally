// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// CommandLineParser — see CommandLineParser.h.

#ifdef SALLY_COMMAND_LINE_PARSER_STANDALONE
#define NOMINMAX
#include <windows.h>
#else
#include "precomp.h"
#endif

#include "common/CommandLineParser.h"

#include <new>

namespace sally::cmdline
{
namespace
{

// The recognised set as a table rather than a switch ladder: one place to read,
// and the tests can walk it.
struct OptionSpec
{
    const wchar_t* letter;
    Option option;
    bool takesValue;
};

const OptionSpec kOptions[] = {
    {L"l", Option::LeftPath, true},
    {L"r", Option::RightPath, true},
    {L"a", Option::ActivePath, true},
    {L"aj", Option::ActiveJumpListPath, true},
    {L"c", Option::ConfigFile, true},
    {L"i", Option::IconIndex, true},
    {L"t", Option::TitlePrefix, true},
    {L"o", Option::ForceOnlyOneInstance, false},
    {L"p", Option::ActivatePanel, true},
    {L"run_notepad", Option::RunNotepad, true},
};

bool IsSwitchPrefix(wchar_t c) { return c == L'-'; }

} // namespace

Option ClassifyToken(const std::wstring& token)
{
    if (token.size() < 2 || !IsSwitchPrefix(token[0]))
        return Option::Unknown; // not a switch at all
    for (const OptionSpec& spec : kOptions)
        if (_wcsicmp(token.c_str() + 1, spec.letter) == 0)
            return spec.option;
    return Option::Unknown;
}

bool OptionTakesValue(Option option)
{
    for (const OptionSpec& spec : kOptions)
        if (spec.option == option)
            return spec.takesValue;
    return false;
}

CommandLineRequest Parse(const std::vector<std::wstring>& tokens)
{
    CommandLineRequest result;

    // Index 0 is the program path by argv convention.
    for (std::size_t i = 1; i < tokens.size(); i++)
    {
        const std::wstring& token = tokens[i];
        if (token.empty())
            continue;

        const bool looksLikeSwitch = token.size() >= 2 && IsSwitchPrefix(token[0]);
        if (!looksLikeSwitch)
        {
            result.errors.push_back(L"unexpected argument: " + token);
            continue;
        }

        const Option option = ClassifyToken(token);
        if (option == Option::Unknown)
        {
            result.errors.push_back(L"unknown option: " + token);
            continue; // keep parsing: one typo must not discard the rest
        }

        if (!OptionTakesValue(option))
        {
            if (option == Option::ForceOnlyOneInstance)
                result.forceOnlyOneInstance = true;
            continue;
        }

        // Value-taking options: the next token is the value, whatever it looks
        // like. A path may legitimately begin with '-', so it is NOT re-examined
        // as a switch - only its absence is an error.
        if (i + 1 >= tokens.size())
        {
            result.errors.push_back(L"option " + token + L" requires a value");
            continue;
        }
        const std::wstring& value = tokens[++i];
        switch (option)
        {
        case Option::LeftPath:
            result.leftPath = value;
            break;
        case Option::RightPath:
            result.rightPath = value;
            break;
        case Option::ActivePath:
            result.activePath = value;
            result.activePathUsesHotPath = false;
            break;
        case Option::ActiveJumpListPath:
            result.activePath = value;
            result.activePathUsesHotPath = true;
            break;
        case Option::ConfigFile:
            result.configFile = value;
            result.configFileSpecified = true;
            break;
        case Option::IconIndex:
            if (value.size() == 1 && value[0] >= L'0' && value[0] <= L'3')
            {
                result.setMainWindowIconIndex = true;
                result.mainWindowIconIndex = value[0] - L'0';
            }
            break;
        case Option::TitlePrefix:
            result.titlePrefixSpecified = true;
            if (!value.empty())
            {
                result.setTitlePrefix = true;
                result.titlePrefix = value;
            }
            break;
        case Option::ActivatePanel:
            if (value.size() == 1 && value[0] >= L'0' && value[0] <= L'2')
                result.activatePanel = value[0] - L'0';
            break;
        case Option::RunNotepad:
            result.runNotepadPath = value;
            break;
        default:
            break;
        }
    }

    return result;
}

CommandLineRequest ParseRawCommandLine(const wchar_t* commandLine)
{
    try
    {
        std::vector<std::wstring> tokens;
        if (commandLine != nullptr && commandLine[0] != L'\0')
        {
            int count = 0;
            wchar_t** argv = CommandLineToArgvW(commandLine, &count);
            if (argv == nullptr)
            {
                CommandLineRequest failed;
                failed.errors.push_back(L"unable to tokenize command line");
                return failed;
            }
            try
            {
                tokens.reserve((std::size_t)count);
                for (int i = 0; i < count; i++)
                    tokens.emplace_back(argv[i]);
            }
            catch (...)
            {
                LocalFree(argv);
                throw;
            }
            LocalFree(argv);
        }
        return Parse(tokens);
    }
    catch (const std::bad_alloc&)
    {
        CommandLineRequest failed;
        try
        {
            failed.errors.push_back(L"not enough memory to parse command line");
        }
        catch (const std::bad_alloc&)
        {
        }
        return failed;
    }
}

} // namespace sally::cmdline
