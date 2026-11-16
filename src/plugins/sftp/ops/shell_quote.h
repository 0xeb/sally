// Building the shell text for the command line of the panel (ExecuteCommandLine). Pure string work, no Sally, no libssh2.
// Assumes a POSIX shell on the server (sh, bash, ...), as the design does.
#pragma once

#include <string>

// 'text' as one shell word in single quotes. Inside single quotes nothing is special except the quote itself, which is
// written as '\'' (close the quote, an escaped quote, open again). Raw bytes are kept as they are.
std::string QuoteForPosixShell(const std::string& text);

// "cd -- '<dir>' && { <userCommand>\n}". The braces make the WHOLE user line depend on the cd ("rm a; rm b" does not
// run "rm b" in the login directory when the cd fails); the newline before "}" keeps a trailing "# comment" of the
// user from swallowing the brace. The exit status is the one of the last command (an "exit 3" still ends the shell
// with 3). The directory is quoted (it is data); the command is NOT (the user types shell
// syntax: pipes, redirects, variables). "--" so that a directory starting with '-' is not read as an option. An empty
// directory means "the login directory": then the command is returned unchanged, without any cd.
std::string BuildRemoteCommand(const std::string& currentDirRaw, const std::string& userCommand);
