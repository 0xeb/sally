// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// CommandLineParser — wide, uncapped command-line parsing.
//
// The legacy parser had three hard limits baked into locals:
//   char buf[4096];    // the whole command line had to fit
//   char* argv[20];    // at most 20 tokens, silently dropping the rest
//   2 * MAX_PATH       // per-path destination buffers
// and it worked on ANSI, so a Unicode path passed on the command line arrived
// mangled through CP_ACP - which for `sally.exe D:\文件` meant opening the wrong
// directory, or none.
//
// This module is the production decision layer: tokens in, a parsed model out,
// no fixed limits anywhere and no narrowing. ParseRawCommandLine uses the OS
// tokenizer; Parse accepts owned tokens so the same decisions remain directly
// testable without spawning a process.
//
// WHAT IT DOES NOT DO: resolve relative paths, expand environment variables, or
// touch the filesystem. Those need the process environment and belong to the
// caller; keeping them out is what lets this be a pure function.

#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace sally::cmdline
{

// One recognised option, kept as data so the parser has no per-option branches
// and the recognised set is inspectable (and testable) as a table.
enum class Option
{
    LeftPath,
    RightPath,
    ActivePath,
    ActiveJumpListPath,
    ConfigFile,
    IconIndex,
    TitlePrefix,
    ForceOnlyOneInstance,
    ActivatePanel,
    RunNotepad,
    Unknown,
};

struct CommandLineRequest
{
    std::wstring leftPath;
    std::wstring rightPath;
    std::wstring activePath;
    std::wstring configFile;
    std::wstring titlePrefix;
    std::wstring runNotepadPath;

    bool activePathUsesHotPath = false;
    bool configFileSpecified = false;
    bool titlePrefixSpecified = false;
    bool setTitlePrefix = false;
    bool forceOnlyOneInstance = false;
    bool setMainWindowIconIndex = false;
    DWORD mainWindowIconIndex = 0;
    DWORD activatePanel = 0;

    // Populated only for an inter-instance activation request.
    DWORD requestUID = 0;
    DWORD requestTimestamp = 0;

    // Reserved for diagnostic clients. Production syntax has no positional
    // arguments, so bare tokens are reported as errors rather than published.
    std::vector<std::wstring> positional;

    // Human-readable problems: an unknown switch, or an option whose required
    // argument was missing. Parsing CONTINUES past an error so a typo in one
    // switch does not discard the rest of the line.
    std::vector<std::wstring> errors;

    bool ok() const { return errors.empty(); }
};

// Classify a single token. Matching is case-insensitive and deliberately uses
// only the legacy '-' prefix.
Option ClassifyToken(const std::wstring& token);

// TRUE when the option consumes the following token as its value.
bool OptionTakesValue(Option option);

// Parse tokens[1..] as a command line — index 0 is skipped as the program path,
// matching argv convention. Pass an already-tokenized vector (from
// CommandLineToArgvW in production).
CommandLineRequest Parse(const std::vector<std::wstring>& tokens);

// Convenience for callers that hold a raw command line: tokenize with the OS
// rules (CommandLineToArgvW) and parse. Declared here, defined in the .cpp so
// the pure parsing above stays free of Windows dependencies for tests.
CommandLineRequest ParseRawCommandLine(const wchar_t* commandLine);

} // namespace sally::cmdline
