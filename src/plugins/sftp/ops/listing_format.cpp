#include "listing_format.h"

#include <cwctype>
#include <vector>

namespace
{
// st_mode file type bits
const uint32_t S_FMT = 0170000;
const uint32_t S_SOCK = 0140000;
const uint32_t S_LNK = 0120000;
const uint32_t S_REG = 0100000;
const uint32_t S_BLK = 0060000;
const uint32_t S_DIR = 0040000;
const uint32_t S_CHR = 0020000;
const uint32_t S_FIFO = 0010000;

wchar_t TypeChar(uint32_t mode, bool isDir, bool isLink)
{
    switch (mode & S_FMT)
    {
    case S_SOCK:
        return L's';
    case S_LNK:
        return L'l';
    case S_REG:
        return L'-';
    case S_BLK:
        return L'b';
    case S_DIR:
        return L'd';
    case S_CHR:
        return L'c';
    case S_FIFO:
        return L'p';
    default: // no type bits reported by the server
        return isLink ? L'l' : (isDir ? L'd' : L'-');
    }
}

// One "rwx" triple. special: the setuid/setgid/sticky bit of this triple; specialChar: 's' or 't'.
void AppendTriple(std::wstring& out, uint32_t bits, bool special, wchar_t specialChar)
{
    out += (bits & 4) ? L'r' : L'-';
    out += (bits & 2) ? L'w' : L'-';
    const bool x = (bits & 1) != 0;
    if (special)
        out += x ? specialChar : static_cast<wchar_t>(towupper(specialChar));
    else
        out += x ? L'x' : L'-';
}

std::vector<std::string> SplitComponents(const std::string& path)
{
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos <= path.size())
    {
        size_t slash = path.find('/', pos);
        if (slash == std::string::npos)
            slash = path.size();
        const std::string part = path.substr(pos, slash - pos);
        if (!part.empty() && part != ".")
        {
            if (part == "..")
            {
                if (!parts.empty())
                    parts.pop_back();
            }
            else
                parts.push_back(part);
        }
        pos = slash + 1;
    }
    return parts;
}
} // namespace

std::wstring FormatPermissions(uint32_t mode, bool isDir, bool isLink)
{
    std::wstring out;
    out += TypeChar(mode, isDir, isLink);
    AppendTriple(out, (mode >> 6) & 7, (mode & 04000) != 0, L's');
    AppendTriple(out, (mode >> 3) & 7, (mode & 02000) != 0, L's');
    AppendTriple(out, mode & 7, (mode & 01000) != 0, L't');
    return out;
}

FILETIME UnixTimeToFileTime(int64_t unixSeconds)
{
    const int64_t secondsFrom1601To1970 = 11644473600LL;
    FILETIME ft = {0, 0};
    if (unixSeconds < -secondsFrom1601To1970)
        return ft;
    const uint64_t ticks = static_cast<uint64_t>(unixSeconds + secondsFrom1601To1970) * 10000000ULL; // 100 ns units
    ft.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFULL);
    ft.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
    return ft;
}

int64_t FileTimeToUnixTime(const FILETIME& fileTime)
{
    const uint64_t ticks = (static_cast<uint64_t>(fileTime.dwHighDateTime) << 32) | fileTime.dwLowDateTime;
    return static_cast<int64_t>(ticks / 10000000ULL) - 11644473600LL;
}

size_t ExtensionOffset(const std::wstring& name)
{
    const size_t dot = name.rfind(L'.');
    return dot == std::wstring::npos ? name.size() : dot + 1;
}

bool IsHiddenName(const std::string& rawName)
{
    return !rawName.empty() && rawName[0] == '.';
}

bool EntryIsDirectory(const CSftpEntry& entry)
{
    return entry.isDir || (entry.isLink && entry.linkTargetIsDir);
}

DWORD EntryAttributes(const CSftpEntry& entry)
{
    DWORD attr = 0;
    if (EntryIsDirectory(entry))
        attr |= FILE_ATTRIBUTE_DIRECTORY;
    if (entry.isLink)
        attr |= FILE_ATTRIBUTE_REPARSE_POINT;
    if (IsHiddenName(entry.rawName))
        attr |= FILE_ATTRIBUTE_HIDDEN;
    return attr != 0 ? attr : FILE_ATTRIBUTE_NORMAL;
}

int CompareRawNames(const std::string& a, const std::string& b)
{
    return a.compare(b);
}

std::string NormalizeRemotePath(const std::string& path)
{
    std::string result;
    for (const std::string& part : SplitComponents(path))
        result += "/" + part;
    return result.empty() ? "/" : result;
}

bool IsRootRemotePath(const std::string& path)
{
    return NormalizeRemotePath(path) == "/";
}

std::string ParentRemotePath(const std::string& path)
{
    const std::string normalized = NormalizeRemotePath(path);
    const size_t slash = normalized.rfind('/');
    return slash == 0 || slash == std::string::npos ? "/" : normalized.substr(0, slash);
}

std::string LastRemoteComponent(const std::string& path)
{
    const std::string normalized = NormalizeRemotePath(path);
    return normalized == "/" ? std::string() : normalized.substr(normalized.rfind('/') + 1);
}

std::string JoinRemotePath(const std::string& base, const std::string& name)
{
    return NormalizeRemotePath(base + "/" + name);
}

std::string ResolveRemotePath(const std::string& base, const std::string& input)
{
    if (input.empty())
        return NormalizeRemotePath(base);
    if (input[0] == '/')
        return NormalizeRemotePath(input);
    return JoinRemotePath(base, input);
}

std::wstring FormatWindowTitlePath(const std::wstring& prefix, const std::wstring& path, int mode)
{
    std::wstring trimmed = path;
    while (trimmed.size() > 1 && trimmed.back() == L'/')
        trimmed.pop_back();
    const size_t slash = trimmed.rfind(L'/');
    const std::wstring last = slash == std::wstring::npos ? std::wstring() : trimmed.substr(slash + 1);
    const std::wstring full = prefix + (trimmed.empty() ? std::wstring(L"/") : trimmed);
    if (last.empty())
        return full; // root
    if (mode == 1)
        return last;
    if (mode == 2 && slash > 0) // more than one component
        return prefix + L"/.../" + last;
    return full;
}

bool IsPathLevelError(SshErrorKind kind)
{
    switch (kind)
    {
    case SshErrorKind::NotFound:
    case SshErrorKind::PermissionDenied:
    case SshErrorKind::Exists:
    case SshErrorKind::NotEmpty:
    case SshErrorKind::Unsupported:
    case SshErrorKind::Protocol:
    case SshErrorKind::Io:
    case SshErrorKind::Other:
        return true;
    default:
        return false;
    }
}
