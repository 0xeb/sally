#include "shell_quote.h"

std::string QuoteForPosixShell(const std::string& text)
{
    std::string quoted = "'";
    for (const char c : text)
    {
        if (c == '\'')
            quoted += "'\\''";
        else
            quoted += c;
    }
    quoted += '\'';
    return quoted;
}

namespace
{
// A backslash at the very end would join the closing "}" to the user's line (backslash-newline is a line continuation).
// One extra space makes it an escaped space, which is harmless.
std::string EndedSafely(const std::string& command)
{
    size_t backslashes = 0;
    while (backslashes < command.size() && command[command.size() - 1 - backslashes] == '\\')
        ++backslashes;
    return backslashes % 2 == 1 ? command + " " : command;
}
} // namespace

std::string BuildRemoteCommand(const std::string& currentDirRaw, const std::string& userCommand)
{
    if (currentDirRaw.empty())
        return userCommand;
    return "cd -- " + QuoteForPosixShell(currentDirRaw) + " && { " + EndedSafely(userCommand) + "\n}";
}
