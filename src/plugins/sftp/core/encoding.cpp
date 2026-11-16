#include "encoding.h"

#include <climits>

namespace
{
std::wstring ToWide(UINT codePage, const std::string& raw)
{
    if (raw.empty() || raw.size() > INT_MAX)
        return std::wstring();
    const int length = MultiByteToWideChar(codePage, 0, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
    if (length <= 0)
        return std::wstring();
    std::wstring result(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(codePage, 0, raw.data(), static_cast<int>(raw.size()), &result[0], length) != length)
        return std::wstring();
    return result;
}
} // namespace

bool IsValidUtf8(const std::string& bytes)
{
    if (bytes.empty())
        return true;
    if (bytes.size() > INT_MAX)
        return false;
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0) > 0;
}

std::wstring CNameCodec::Decode(const std::string& raw) const
{
    switch (mode_)
    {
    case Mode::Utf8:
        return ToWide(CP_UTF8, raw); // flags 0: invalid bytes become U+FFFD
    case Mode::CodePage:
        return ToWide(codePage_, raw);
    case Mode::Auto:
        return ToWide(IsValidUtf8(raw) ? CP_UTF8 : CP_ACP, raw);
    }
    return std::wstring();
}

std::string CNameCodec::Encode(const std::wstring& text, bool* usedDefaultChar) const
{
    if (usedDefaultChar != nullptr)
        *usedDefaultChar = false;
    if (text.empty() || text.size() > INT_MAX)
        return std::string();
    const UINT codePage = mode_ == Mode::CodePage ? codePage_ : CP_UTF8;
    // UTF-8 conversions require no default char
    const char* defaultChar = codePage == CP_UTF8 ? nullptr : "?";
    const int inLength = static_cast<int>(text.size());
    const int length = WideCharToMultiByte(codePage, 0, text.data(), inLength, nullptr, 0, nullptr, nullptr);
    if (length <= 0)
        return std::string();
    std::string result(static_cast<size_t>(length), '\0');
    BOOL usedDefault = FALSE;
    if (WideCharToMultiByte(codePage, 0, text.data(), inLength, &result[0], length, defaultChar,
                            defaultChar != nullptr ? &usedDefault : nullptr) != length)
        return std::string();
    if (usedDefaultChar != nullptr)
        *usedDefaultChar = usedDefault != FALSE;
    return result;
}
