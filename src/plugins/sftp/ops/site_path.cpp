#include "site_path.h"

#include <windows.h>

namespace
{
bool EqualNoCase(const std::wstring& a, const std::wstring& b)
{
    return a.size() == b.size() &&
           (a.empty() || CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                              static_cast<int>(b.size()), TRUE) == CSTR_EQUAL);
}

// "/a/b//" -> "/a/b"; "" or "///" -> "/"
std::wstring NormalizePath(std::wstring path)
{
    if (path.empty() || path[0] != L'/')
        path.insert(path.begin(), L'/');
    while (path.size() > 1 && path.back() == L'/')
        path.pop_back();
    return path;
}

// digits only, 1..65535
bool ParsePort(const std::wstring& text, int& port)
{
    if (text.empty() || text.size() > 5)
        return false;
    int value = 0;
    for (wchar_t c : text)
    {
        if (c < L'0' || c > L'9')
            return false;
        value = value * 10 + (c - L'0');
    }
    if (value < 1 || value > 65535)
        return false;
    port = value;
    return true;
}

// "host", "host:port", "[v6]" or "[v6]:port"
bool ParseHostPort(const std::wstring& text, std::wstring& host, int& port)
{
    port = 22;
    if (!text.empty() && text[0] == L'[')
    {
        size_t close = text.find(L']');
        if (close == std::wstring::npos || close == 1)
            return false;
        host = text.substr(1, close - 1);
        if (host.find_first_of(L"[]") != std::wstring::npos)
            return false;
        if (close + 1 == text.size())
            return true;
        return text[close + 1] == L':' && ParsePort(text.substr(close + 2), port);
    }
    size_t colon = text.find(L':');
    if (colon == std::wstring::npos)
        host = text;
    else
    {
        // a second ':' means an IPv6 address without brackets: ambiguous with the port, so not accepted
        if (text.find(L':', colon + 1) != std::wstring::npos || !ParsePort(text.substr(colon + 1), port))
            return false;
        host = text.substr(0, colon);
    }
    return !host.empty() && host.find_first_of(L"[]") == std::wstring::npos;
}
} // namespace

bool ParseSftpPath(const std::wstring& s, CSftpPath& out)
{
    const size_t slash = s.find(L'/');
    const std::wstring authority = s.substr(0, slash);
    const std::wstring rest = slash == std::wstring::npos ? L"/" : s.substr(slash);

    CSftpPath p;
    p.path = NormalizePath(rest);
    if (!authority.empty() && authority[0] == L'@')
    {
        p.isSite = true;
        p.siteName = authority.substr(1);
        if (p.siteName.empty())
            return false;
    }
    else
    {
        const size_t at = authority.rfind(L'@'); // the user name may contain '@'
        if (at == std::wstring::npos || at == 0)
            return false;
        p.user = authority.substr(0, at);
        if (!ParseHostPort(authority.substr(at + 1), p.host, p.port))
            return false;
    }
    out = std::move(p);
    return true;
}

std::wstring FormatSftpPath(const CSftpPath& p)
{
    std::wstring text;
    if (p.isSite)
        text = L"@" + p.siteName;
    else
    {
        text = p.user + L"@";
        if (p.host.find(L':') != std::wstring::npos)
            text += L"[" + p.host + L"]";
        else
            text += p.host;
        if (p.port != 22)
            text += L":" + std::to_wstring(p.port);
    }
    return text + NormalizePath(p.path);
}

CSftpPath RootOf(const CSftpPath& p)
{
    CSftpPath root = p;
    root.path = L"/";
    return root;
}

bool SameServer(const CSftpPath& a, const CSftpPath& b)
{
    if (a.isSite != b.isSite)
        return false;
    if (a.isSite)
        return EqualNoCase(a.siteName, b.siteName);
    return a.user == b.user && a.port == b.port && EqualNoCase(a.host, b.host);
}

bool SplitFsPath(const std::wstring& text, const std::wstring& fsName, std::wstring& userPart)
{
    if (fsName.empty() || text.size() <= fsName.size() + 1 || text[fsName.size()] != L':' ||
        !EqualNoCase(text.substr(0, fsName.size()), fsName))
        return false;
    userPart = text.substr(fsName.size() + 1);
    return true;
}

bool IsPathWithin(const std::wstring& path, const std::wstring& base, bool includingSubdirs)
{
    if (path == base)
        return true;
    if (!includingSubdirs)
        return false;
    if (base == L"/")
        return !path.empty() && path[0] == L'/';
    return path.size() > base.size() && path.compare(0, base.size(), base) == 0 && path[base.size()] == L'/';
}
