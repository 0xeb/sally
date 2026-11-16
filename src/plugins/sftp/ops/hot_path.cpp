#include "hot_path.h"

namespace
{
// Index just after the root of the path: the "sftp:" prefix, the server part ("user@host:port" or "@Site") and its
// slash. A text without the slash has its whole length as the root.
int RootEnd(const wchar_t* text, int pathLen)
{
    int i = 0;
    while (i < pathLen && text[i] != L':')
        i++; // skip the FS name (the first colon is the one after it, a port colon comes later)
    if (i == pathLen)
        return 0; // no FS name: nothing to protect
    i++;
    while (i < pathLen && text[i] != L'/')
        i++; // the server part never contains '/'
    return i < pathLen ? i + 1 : pathLen;
}
} // namespace

bool NextHotPath(const wchar_t* text, int pathLen, int& offset)
{
    if (text == nullptr || offset < 0 || offset >= pathLen)
        return false;
    const int root = RootEnd(text, pathLen);
    if (offset < root)
    {
        offset = root;
        return true;
    }
    int s = offset;
    if (text[s] == L'/')
        s++; // always moves forward, also on "//"
    while (s < pathLen && text[s] != L'/')
        s++;
    offset = s;
    return true;
}

void CompleteHotPath(std::wstring& path)
{
    const size_t colon = path.find(L':');
    if (colon == std::wstring::npos || colon + 1 >= path.size())
        return;
    if (path.find(L'/', colon + 1) == std::wstring::npos)
        path += L'/';
}
