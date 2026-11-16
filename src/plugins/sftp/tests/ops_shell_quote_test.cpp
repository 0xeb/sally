#include <gtest/gtest.h>

#include "exec_text.h"
#include "shell_quote.h"

TEST(ShellQuote, PlainTextIsWrappedInSingleQuotes)
{
    EXPECT_EQ(QuoteForPosixShell("abc"), "'abc'");
    EXPECT_EQ(QuoteForPosixShell(""), "''");
}

TEST(ShellQuote, SpacesDollarAndBackticksStayLiteral)
{
    EXPECT_EQ(QuoteForPosixShell("a b"), "'a b'");
    EXPECT_EQ(QuoteForPosixShell("$HOME `id` $(id) \\n \"x\""), "'$HOME `id` $(id) \\n \"x\"'");
}

TEST(ShellQuote, EmbeddedSingleQuoteBecomesCloseEscapeOpen)
{
    EXPECT_EQ(QuoteForPosixShell("it's"), "'it'\\''s'");
    EXPECT_EQ(QuoteForPosixShell("'"), "''\\'''");
    EXPECT_EQ(QuoteForPosixShell("''"), "''\\'''\\'''");
}

TEST(ShellQuote, NonAsciiRawBytesAreKept)
{
    const std::string raw = "caf\xE9 \xC3\xA9 \xFF";
    EXPECT_EQ(QuoteForPosixShell(raw), "'" + raw + "'");
}

TEST(ShellQuote, RemoteCommandChangesDirectoryFirstAndGroupsTheCommand)
{
    EXPECT_EQ(BuildRemoteCommand("/home/test", "ls -la"), "cd -- '/home/test' && { ls -la\n}");
    EXPECT_EQ(BuildRemoteCommand("/", "a || b"), "cd -- '/' && { a || b\n}");
    EXPECT_EQ(BuildRemoteCommand("/", "x; y"), "cd -- '/' && { x; y\n}");
}

TEST(ShellQuote, ATrailingCommentCannotSwallowTheClosingBrace)
{
    EXPECT_EQ(BuildRemoteCommand("/d", "ls # note"), "cd -- '/d' && { ls # note\n}");
}

TEST(ShellQuote, ATrailingBackslashCannotJoinTheClosingBrace)
{
    // one backslash: a space after it makes an escaped space instead of a line continuation
    EXPECT_EQ(BuildRemoteCommand("/d", "echo a\\"), "cd -- '/d' && { echo a\\ \n}");
    // two backslashes are an escaped backslash already
    EXPECT_EQ(BuildRemoteCommand("/d", "echo a\\\\"), "cd -- '/d' && { echo a\\\\\n}");
    EXPECT_EQ(BuildRemoteCommand("/d", "echo a\\\\\\"), "cd -- '/d' && { echo a\\\\\\ \n}");
}

TEST(ShellQuote, DirectoryIsQuotedButTheCommandIsNot)
{
    EXPECT_EQ(BuildRemoteCommand("/a b/it's", "echo \"$X\" | wc -l; exit 3"),
              "cd -- '/a b/it'\\''s' && { echo \"$X\" | wc -l; exit 3\n}");
}

TEST(ShellQuote, DirectoryStartingWithADashIsNotAnOption)
{
    EXPECT_EQ(BuildRemoteCommand("-weird", "pwd"), "cd -- '-weird' && { pwd\n}");
}

TEST(ShellQuote, EmptyDirectoryMeansNoCdAndNoGroup)
{
    EXPECT_EQ(BuildRemoteCommand("", "pwd"), "pwd");
}

TEST(ExecText, LineEndsBecomeCrLf)
{
    EXPECT_EQ(OutputToEditText(L"a\nb"), L"a\r\nb");
    EXPECT_EQ(OutputToEditText(L"a\r\nb\r\n"), L"a\r\nb\r\n");
    EXPECT_EQ(OutputToEditText(L"a\rb"), L"a\r\nb");
    EXPECT_EQ(OutputToEditText(L"\n\n"), L"\r\n\r\n");
}

TEST(ExecText, ControlCharactersAreReplacedButTabStays)
{
    const std::wstring withNul(L"a\0b", 3);
    EXPECT_EQ(OutputToEditText(withNul), L"a\xFFFD" L"b");
    EXPECT_EQ(OutputToEditText(L"\x1b[0m"), L"\xFFFD[0m");
    EXPECT_EQ(OutputToEditText(L"a\tb"), L"a\tb");
    EXPECT_EQ(OutputToEditText(L""), L"");
}

TEST(ExecText, TruncateForDisplayCutsWithAnEllipsis)
{
    EXPECT_EQ(TruncateForDisplay(L"abcdef", 10), L"abcdef");
    EXPECT_EQ(TruncateForDisplay(L"abcdef", 6), L"abcdef");
    EXPECT_EQ(TruncateForDisplay(L"abcdefg", 6), L"abc...");
    EXPECT_EQ(TruncateForDisplay(L"", 6), L"");
}
