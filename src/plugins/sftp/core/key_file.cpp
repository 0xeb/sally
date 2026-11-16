#include "key_file.h"

#include <windows.h>

#include <cstring>
#include <vector>

namespace
{
constexpr LONGLONG kMaxKeyFileBytes = 1024 * 1024;

std::wstring ToWide(const std::string& utf8)
{
    if (utf8.empty())
        return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0)
        return std::wstring();
    std::wstring wide(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), &wide[0], n);
    return wide;
}
} // namespace

namespace
{
void SetKeyError(CSshError& error, SshMsg msgId, std::vector<std::string> args, const std::string& message)
{
    error = CSshError();
    error.kind = SshErrorKind::KeyFileError;
    error.message = message;
    error.msgId = msgId;
    error.args = std::move(args);
}
} // namespace

bool ReadKeyFile(const std::string& utf8Path, CSecret& out, CSshError& error)
{
    out.clear();
    const std::wstring path = ToWide(utf8Path);
    if (path.empty())
    {
        SetKeyError(error, SshMsg::KeyFilePathInvalid, {utf8Path},
                    "The key file path is empty or not valid text: " + utf8Path);
        return false;
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        const std::string winError = std::to_string(GetLastError());
        SetKeyError(error, SshMsg::KeyFileOpenFailed, {utf8Path, winError},
                    "Cannot open the key file '" + utf8Path + "' (Windows error " + winError + ")");
        return false;
    }
    LARGE_INTEGER size = {};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= kMaxKeyFileBytes;
    if (!ok)
    {
        SetKeyError(error, SshMsg::KeyFileBadSize, {utf8Path}, "The key file '" + utf8Path + "' is empty or too large");
    }
    else
    {
        out.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = ReadFile(file, out.data(), static_cast<DWORD>(size.QuadPart), &read, nullptr) && read == size.QuadPart;
        if (!ok)
        {
            const std::string winError = std::to_string(GetLastError());
            SetKeyError(error, SshMsg::KeyFileReadFailed, {utf8Path, winError},
                        "Cannot read the key file '" + utf8Path + "' (Windows error " + winError + ")");
        }
    }
    CloseHandle(file);
    if (!ok)
        out.clear();
    return ok;
}

bool ReadKeyFile(const std::string& utf8Path, CSecret& out, std::string& why)
{
    CSshError error;
    if (ReadKeyFile(utf8Path, out, error))
        return true;
    why = error.message;
    return false;
}

bool IsPuttyKey(const CSecret& key)
{
    static const char kMagic[] = "PuTTY-User-Key-File";
    return key.size() >= sizeof(kMagic) - 1 && memcmp(key.data(), kMagic, sizeof(kMagic) - 1) == 0;
}
