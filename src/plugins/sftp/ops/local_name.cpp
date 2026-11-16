#include "local_name.h"

#include <windows.h>

#include <cwctype>

namespace
{
    const size_t kMaxNameLength = 255;

    bool IsReservedDeviceName(const std::wstring& name)
    {
        // the device name is the part before the first dot; Windows also ignores spaces before that dot ("CON .txt")
        std::wstring stem = name.substr(0, name.find(L'.'));
        while (!stem.empty() && stem.back() == L' ')
            stem.pop_back();
        if (stem.empty())
            return false;
        for (wchar_t& c : stem)
            c = static_cast<wchar_t>(towupper(c));

        if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL")
            return true;
        if (stem.size() == 4 && (stem.compare(0, 3, L"COM") == 0 || stem.compare(0, 3, L"LPT") == 0))
            return stem[3] >= L'1' && stem[3] <= L'9';
        return false;
    }

    bool StartsWith(const std::wstring& text, const wchar_t* prefix)
    {
        return text.compare(0, wcslen(prefix), prefix) == 0;
    }

    bool IsDriveAbsolute(const std::wstring& path)
    {
        return path.size() >= 3 && iswalpha(path[0]) && path[1] == L':' && path[2] == L'\\';
    }
} // namespace

bool IsValidLocalName(const std::wstring& name)
{
    if (name.empty() || name.size() > kMaxNameLength || name == L"." || name == L"..")
        return false;
    for (wchar_t c : name)
    {
        if (c < 0x20 || wcschr(L"<>:\"/\\|?*", c) != nullptr)
            return false;
    }
    if (name.back() == L'.' || name.back() == L' ')
        return false;
    return !IsReservedDeviceName(name);
}

std::wstring MakeLocalPath(const std::wstring& dir, const std::wstring& name)
{
    std::wstring path = dir;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
        path += L'\\';
    path += name;
    return ExtendedLocalPath(path);
}

std::wstring ExtendedLocalPath(const std::wstring& path)
{
    // CreateDirectory fails from 248 characters on (MAX_PATH minus room for an 8.3 file name), although a file path
    // may have 259: the prefix starts there, so a deep folder can be created too.
    constexpr size_t kPrefixFrom = 248;
    if (path.size() < kPrefixFrom || StartsWith(path, L"\\\\?\\") || StartsWith(path, L"\\\\.\\"))
        return path;
    if (StartsWith(path, L"\\\\"))
        return L"\\\\?\\UNC\\" + path.substr(2);
    if (IsDriveAbsolute(path))
        return L"\\\\?\\" + path;
    return path; // relative: the prefix needs an absolute path, so leave it to the caller
}

std::wstring DecodeRemoteNameToLocal(const CNameCodec& codec, const std::string& rawName, bool* lossy)
{
    std::wstring decoded = codec.Decode(rawName);
    if (lossy != nullptr)
    {
        const bool hasReplacement = decoded.find(static_cast<wchar_t>(0xFFFD)) != std::wstring::npos;
        const bool rawHasReplacement = rawName.find("\xEF\xBF\xBD") != std::string::npos;
        *lossy = hasReplacement && !rawHasReplacement;
    }
    return decoded;
}
