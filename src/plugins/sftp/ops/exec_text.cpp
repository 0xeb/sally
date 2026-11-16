#include "exec_text.h"

std::wstring OutputToEditText(const std::wstring& decoded)
{
    std::wstring text;
    text.reserve(decoded.size() + decoded.size() / 16);
    for (size_t i = 0; i < decoded.size(); ++i)
    {
        const wchar_t c = decoded[i];
        if (c == L'\r')
        {
            if (i + 1 < decoded.size() && decoded[i + 1] == L'\n')
                ++i; // CRLF stays one line end
            text += L"\r\n";
        }
        else if (c == L'\n')
            text += L"\r\n";
        else if (c < L' ' && c != L'\t')
            text += L'\xFFFD';
        else
            text += c;
    }
    return text;
}

std::wstring TruncateForDisplay(const std::wstring& text, size_t maxChars)
{
    if (text.size() <= maxChars || maxChars < 3)
        return text;
    return text.substr(0, maxChars - 3) + L"...";
}
