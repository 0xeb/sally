#include "sftp_url.h"

#include "encoding.h"

namespace
{
bool IsUnreserved(unsigned char c, bool keepSlash)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
           c == '_' || c == '~' || (keepSlash && c == '/');
}

// The text as UTF-8 with everything outside the unreserved set written as %XX.
std::wstring PercentEncode(const std::wstring& text, bool keepSlash)
{
    static const wchar_t digits[] = L"0123456789ABCDEF";
    const std::string utf8 = CNameCodec::Utf8().Encode(text);
    std::wstring out;
    for (const char ch : utf8)
    {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (IsUnreserved(c, keepSlash))
            out += static_cast<wchar_t>(c);
        else
        {
            out += L'%';
            out += digits[c >> 4];
            out += digits[c & 15];
        }
    }
    return out;
}
} // namespace

std::wstring BuildSftpUrl(const std::wstring& user, const std::wstring& host, int port, const std::wstring& path)
{
    std::wstring url = L"sftp://";
    if (!user.empty())
        url += PercentEncode(user, false) + L"@";
    if (host.find(L':') != std::wstring::npos)
        url += L"[" + host + L"]"; // an IPv6 address
    else
        url += PercentEncode(host, false);
    if (port != 22)
        url += L":" + std::to_wstring(port);
    if (path.empty() || path[0] != L'/')
        url += L'/';
    url += PercentEncode(path, true);
    return url;
}
