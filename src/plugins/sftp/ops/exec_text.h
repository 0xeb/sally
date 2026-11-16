// Turns the decoded output of a remote command into text for a Windows multiline edit control. Pure string work.
#pragma once

#include <string>

// Line ends (LF, CRLF and a lone CR, which progress bars use) become CRLF. A NUL or another control character that an
// edit control cannot show (everything below space except tab) becomes U+FFFD, so the text is never cut at a NUL and
// escape sequences cannot garble the dialog.
std::wstring OutputToEditText(const std::wstring& decoded);

// 'text' cut to at most maxChars characters, with "..." as the last three when it was longer (for a message box).
std::wstring TruncateForDisplay(const std::wstring& text, size_t maxChars);
