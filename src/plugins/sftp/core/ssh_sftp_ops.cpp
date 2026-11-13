// SFTP operations without data transfer (CSshSession members). Every libssh2 call goes through Run/RunPtr.
#include <libssh2.h>
#include <libssh2_sftp.h>

#include <bcrypt.h>

#include <climits>
#include <cstring>
#include <string>
#include <vector>

#include "ssh_session.h"

namespace
{
constexpr size_t kMaxNameBytes = 4096; // buffer for one directory entry name, link target or real path
constexpr size_t kMaxListEntries = 10 * 1000 * 1000;

const char* StatusText(unsigned long status)
{
    switch (status)
    {
    case LIBSSH2_FX_EOF: return "end of file";
    case LIBSSH2_FX_NO_SUCH_FILE: return "no such file";
    case LIBSSH2_FX_PERMISSION_DENIED: return "permission denied";
    case LIBSSH2_FX_FAILURE: return "operation failed";
    case LIBSSH2_FX_BAD_MESSAGE: return "bad message";
    case LIBSSH2_FX_NO_CONNECTION: return "no connection";
    case LIBSSH2_FX_CONNECTION_LOST: return "connection lost";
    case LIBSSH2_FX_OP_UNSUPPORTED: return "operation not supported";
    case LIBSSH2_FX_INVALID_HANDLE: return "invalid handle";
    case LIBSSH2_FX_NO_SUCH_PATH: return "no such path";
    case LIBSSH2_FX_FILE_ALREADY_EXISTS: return "file already exists";
    case LIBSSH2_FX_WRITE_PROTECT: return "write protected";
    case LIBSSH2_FX_NO_MEDIA: return "no media";
    case LIBSSH2_FX_NO_SPACE_ON_FILESYSTEM: return "no space left on the file system";
    case LIBSSH2_FX_QUOTA_EXCEEDED: return "quota exceeded";
    case LIBSSH2_FX_DIR_NOT_EMPTY: return "directory not empty";
    case LIBSSH2_FX_NOT_A_DIRECTORY: return "not a directory";
    case LIBSSH2_FX_INVALID_FILENAME: return "invalid file name";
    case LIBSSH2_FX_LINK_LOOP: return "too many symbolic links";
    default: return "error";
    }
}

SshErrorKind KindFromStatus(unsigned long status)
{
    switch (status)
    {
    case LIBSSH2_FX_NO_SUCH_FILE:
    case LIBSSH2_FX_NO_SUCH_PATH: return SshErrorKind::NotFound;
    case LIBSSH2_FX_PERMISSION_DENIED:
    case LIBSSH2_FX_WRITE_PROTECT: return SshErrorKind::PermissionDenied;
    case LIBSSH2_FX_FILE_ALREADY_EXISTS: return SshErrorKind::Exists;
    case LIBSSH2_FX_DIR_NOT_EMPTY: return SshErrorKind::NotEmpty;
    case LIBSSH2_FX_OP_UNSUPPORTED: return SshErrorKind::Unsupported;
    case LIBSSH2_FX_BAD_MESSAGE: return SshErrorKind::Protocol;
    case LIBSSH2_FX_NO_CONNECTION:
    case LIBSSH2_FX_CONNECTION_LOST: return SshErrorKind::ConnectFailed;
    case LIBSSH2_FX_NO_SPACE_ON_FILESYSTEM:
    case LIBSSH2_FX_QUOTA_EXCEEDED:
    case LIBSSH2_FX_NO_MEDIA: return SshErrorKind::Io;
    default: return SshErrorKind::Other; // includes the generic FAILURE; callers refine it with a follow-up check
    }
}

void FillEntry(const LIBSSH2_SFTP_ATTRIBUTES& a, CSftpEntry& e)
{
    if ((a.flags & LIBSSH2_SFTP_ATTR_SIZE) != 0)
        e.size = a.filesize;
    if ((a.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) != 0)
    {
        e.mtime = static_cast<int64_t>(a.mtime);
        e.atime = static_cast<int64_t>(a.atime);
    }
    if ((a.flags & LIBSSH2_SFTP_ATTR_UIDGID) != 0)
    {
        e.owner = std::to_string(a.uid);
        e.group = std::to_string(a.gid);
    }
    if ((a.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) != 0)
    {
        e.permissions = static_cast<uint32_t>(a.permissions);
        e.hasPermissions = true;
        const unsigned long type = a.permissions & LIBSSH2_SFTP_S_IFMT;
        e.isDir = type == LIBSSH2_SFTP_S_IFDIR;
        e.isLink = type == LIBSSH2_SFTP_S_IFLNK;
    }
}

// A failure that is not an answer of the SFTP server about one path: transport error, timeout, cancel, broken session.
bool IsConnectionLevel(int code)
{
    return code != 0 && code != LIBSSH2_ERROR_SFTP_PROTOCOL;
}

std::string JoinPath(const std::string& dir, const std::string& name)
{
    if (dir.empty() || dir.back() == '/')
        return dir + name;
    return dir + "/" + name;
}
} // namespace

bool IsValidRemoteEntryName(const std::string& name)
{
    if (name.empty() || name == "." || name == "..")
        return false;
    return name.find('/') == std::string::npos && name.find('\0') == std::string::npos;
}

std::string CSshSession::Display(const std::string& raw) const
{
    std::wstring wide = codec_.Decode(raw);
    if (wide.empty())
        return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(len > 0 ? len : 0), '\0');
    if (len > 0)
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), &utf8[0], len, nullptr, nullptr);
    return utf8;
}

// Common start of every operation: a cancel that arrived before the call, and a missing SFTP session.
bool CSshSession::BeginSftp(const char* call, const std::string& rawPath, const std::string& rawPath2)
{
    if (IsCancelled())
        return Fail(SshErrorKind::Cancelled, std::string());
    if (broken_)
        return FailBroken();
    if (sftp_ == nullptr || session_ == nullptr)
        return Fail(SshErrorKind::Other, SshMsg::SftpNotOpen, {}, "The SFTP session is not open");
    LogVerbose(call, rawPath, rawPath2);
    return true;
}

// Error of the failed SFTP call "what" on path: the server's status for an SFTP error, the transport error otherwise.
// msgId is the action sentence ("Cannot list '%1': %2") with the path as its first argument; the reason (%2) is chosen
// from sftpStatus by the UI layer, or is the libssh2 text (second argument) when the connection itself failed.
bool CSshSession::FailSftp(SshMsg msgId, const std::string& what, const std::string& path, int code)
{
    const std::string subject = what + " '" + Display(path) + "'";
    if (code != LIBSSH2_ERROR_SFTP_PROTOCOL)
    {
        FailFromSession(subject, code, msgId, {Display(path)}); // a timeout keeps the sentence; the reason says "no answer"
        return false;
    }

    const unsigned long status = libssh2_sftp_last_error(sftp_);
    Fail(KindFromStatus(status), msgId, {Display(path)},
         subject + ": " + StatusText(status) + " (SFTP status " + std::to_string(status) + ")", code);
    error_.sftpStatus = status;
    return false;
}

// A generic FAILURE after create/rename is ambiguous (OpenSSH uses it for "exists"): when the target is there, say so.
bool CSshSession::FailExistsIfPresent(const std::string& path)
{
    if (error_.kind == SshErrorKind::Cancelled)
        return false;
    if (error_.sftpStatus != LIBSSH2_FX_FAILURE && error_.sftpStatus != LIBSSH2_FX_FILE_ALREADY_EXISTS)
        return false;
    LIBSSH2_SFTP_ATTRIBUTES attrs = {};
    if (StatRaw(path, LIBSSH2_SFTP_LSTAT, attrs) == 0)
    {
        error_.kind = SshErrorKind::Exists;
        error_.message = "'" + Display(path) + "' already exists";
        error_.msgId = SshMsg::AlreadyExists;
        error_.args = {Display(path)};
    }
    return false;
}

int CSshSession::StatRaw(const std::string& path, int statType, LIBSSH2_SFTP_ATTRIBUTES& attrs)
{
    return Run(
        [&]
        {
            return libssh2_sftp_stat_ex(sftp_, path.data(), static_cast<unsigned int>(path.size()), statType, &attrs);
        },
        timeoutMs_);
}

int CSshSession::IsDirectory(const std::string& path, bool& isDir)
{
    isDir = false;
    LIBSSH2_SFTP_ATTRIBUTES attrs = {};
    const int rc = StatRaw(path, LIBSSH2_SFTP_STAT, attrs);
    if (rc != 0)
        return rc;
    isDir = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) != 0 &&
            (attrs.permissions & LIBSSH2_SFTP_S_IFMT) == LIBSSH2_SFTP_S_IFDIR;
    return 0;
}

// Closes a handle that is no longer needed. On a healthy session the close goes through Run. After a cancel or on a
// broken session the socket is already shut down: one plain call (no waiting) makes libssh2 fail the send at once and
// forget the handle, the rest is freed by libssh2_sftp_shutdown / libssh2_session_free in Disconnect.
void CSshSession::CloseSftpHandle(LIBSSH2_SFTP_HANDLE* h)
{
    if (h == nullptr)
        return;
    if (IsCancelled() || broken_)
        libssh2_sftp_close_handle(h);
    else
        Run([h] { return libssh2_sftp_close_handle(h); }, timeoutMs_);
}

bool CSshSession::ListDir(const std::string& path, std::vector<CSftpEntry>& out)
{
    out.clear();
    if (!BeginSftp("list", path))
        return false;

    int code = 0;
    LIBSSH2_SFTP_HANDLE* dir = RunPtr(
        [&]
        {
            return libssh2_sftp_open_ex(sftp_, path.data(), static_cast<unsigned int>(path.size()), 0, 0,
                                        LIBSSH2_SFTP_OPENDIR);
        },
        timeoutMs_, code);
    if (dir == nullptr)
        return FailSftp(SshMsg::SftpList, "Cannot list", path, code);

    std::vector<char> name(kMaxNameBytes);
    bool ok = true;
    for (;;)
    {
        if (IsCancelled())
        {
            ok = Fail(SshErrorKind::Cancelled, std::string());
            break;
        }
        LIBSSH2_SFTP_ATTRIBUTES attrs = {};
        int rc = Run(
            [&] { return libssh2_sftp_readdir_ex(dir, name.data(), name.size(), nullptr, 0, &attrs); }, timeoutMs_);
        // libssh2 returns 0 at the end of the list and cannot tell it from an entry with an empty name, so an empty
        // name ends the list early (an OpenSSH server never sends one); every returned name is validated below.
        if (rc == 0)
            break;
        if (rc < 0)
        {
            ok = FailSftp(SshMsg::SftpList, "Cannot read the directory", path, rc);
            break;
        }
        CSftpEntry e;
        e.rawName.assign(name.data(), static_cast<size_t>(rc));
        if (e.rawName == "." || e.rawName == "..")
            continue;
        if (!IsValidRemoteEntryName(e.rawName))
        {
            ok = Fail(SshErrorKind::Protocol, SshMsg::InvalidServerData, {Display(path)},
                      "The server sent an invalid entry name in '" + Display(path) + "': '" + Display(e.rawName) +
                          "' (contains '/' or a NUL byte)");
            break;
        }
        if (out.size() >= kMaxListEntries)
        {
            ok = Fail(SshErrorKind::Protocol, SshMsg::InvalidServerData, {Display(path)},
                      "The directory '" + Display(path) + "' has more than " + std::to_string(kMaxListEntries) +
                          " entries");
            break;
        }
        FillEntry(attrs, e);
        out.push_back(std::move(e));
    }
    CloseSftpHandle(dir);

    // Servers that leave the permissions out of the listing, and links: a second look per entry. Only an SFTP status
    // for that one entry (a broken link, a vanished file) is tolerated; a connection-level error aborts the listing.
    for (size_t i = 0; ok && i < out.size(); ++i)
    {
        CSftpEntry& e = out[i];
        const std::string full = JoinPath(path, e.rawName);
        if (!e.hasPermissions)
        {
            LIBSSH2_SFTP_ATTRIBUTES attrs = {};
            if (IsCancelled())
                ok = Fail(SshErrorKind::Cancelled, std::string());
            else
            {
                const int rc = StatRaw(full, LIBSSH2_SFTP_LSTAT, attrs);
                if (rc == 0)
                    FillEntry(attrs, e);
                else if (IsConnectionLevel(rc))
                    ok = FailSftp(SshMsg::SftpReadInfo, "Cannot get the attributes of", full, rc);
            }
        }
        if (ok && e.isLink)
        {
            const int rc = IsDirectory(full, e.linkTargetIsDir);
            if (IsConnectionLevel(rc))
                ok = FailSftp(SshMsg::SftpReadInfo, "Cannot get the attributes of", full, rc);
        }
        if (ok && IsCancelled())
            ok = Fail(SshErrorKind::Cancelled, std::string());
    }
    if (!ok)
        out.clear();
    return ok;
}

bool CSshSession::StatCommon(const std::string& path, int statType, const char* what, CSftpEntry& out)
{
    out = CSftpEntry();
    if (!BeginSftp(statType == LIBSSH2_SFTP_LSTAT ? "lstat" : "stat", path))
        return false;
    LIBSSH2_SFTP_ATTRIBUTES attrs = {};
    int rc = StatRaw(path, statType, attrs);
    if (rc != 0)
        return FailSftp(SshMsg::SftpReadInfo, what, path, rc);
    FillEntry(attrs, out);
    if (out.isLink)
    {
        rc = IsDirectory(path, out.linkTargetIsDir);
        if (IsConnectionLevel(rc))
            return FailSftp(SshMsg::SftpReadInfo, what, path, rc);
    }
    return true;
}

bool CSshSession::Stat(const std::string& path, CSftpEntry& out)
{
    return StatCommon(path, LIBSSH2_SFTP_STAT, "Cannot get the attributes of", out);
}

bool CSshSession::LStat(const std::string& path, CSftpEntry& out)
{
    return StatCommon(path, LIBSSH2_SFTP_LSTAT, "Cannot get the attributes of", out);
}

bool CSshSession::RealPath(const std::string& path, std::string& out)
{
    out.clear();
    if (!BeginSftp("realpath", path))
        return false;
    std::vector<char> buf(kMaxNameBytes);
    int rc = Run(
        [&]
        {
            return libssh2_sftp_symlink_ex(sftp_, path.data(), static_cast<unsigned int>(path.size()), buf.data(),
                                           static_cast<unsigned int>(buf.size()), LIBSSH2_SFTP_REALPATH);
        },
        timeoutMs_);
    if (rc < 0)
        return FailSftp(SshMsg::SftpReadInfo, "Cannot resolve the path", path, rc);
    out.assign(buf.data(), static_cast<size_t>(rc));
    return true;
}

bool CSshSession::ReadLink(const std::string& path, std::string& target)
{
    target.clear();
    if (!BeginSftp("readlink", path))
        return false;
    std::vector<char> buf(kMaxNameBytes);
    int rc = Run(
        [&]
        {
            return libssh2_sftp_symlink_ex(sftp_, path.data(), static_cast<unsigned int>(path.size()), buf.data(),
                                           static_cast<unsigned int>(buf.size()), LIBSSH2_SFTP_READLINK);
        },
        timeoutMs_);
    if (rc < 0)
        return FailSftp(SshMsg::SftpReadInfo, "Cannot read the link", path, rc);
    target.assign(buf.data(), static_cast<size_t>(rc));
    return true;
}

bool CSshSession::Mkdir(const std::string& path, int mode)
{
    if (!BeginSftp("mkdir", path))
        return false;
    int rc = Run(
        [&]
        { return libssh2_sftp_mkdir_ex(sftp_, path.data(), static_cast<unsigned int>(path.size()), mode); },
        timeoutMs_);
    if (rc == 0)
        return true;
    FailSftp(SshMsg::SftpCreate, "Cannot create the directory", path, rc);
    return FailExistsIfPresent(path);
}

// 1 when the directory holds at least one entry besides "." and "..", 0 when it is empty, -1 when that cannot be
// told (cannot open, read error, connection problem).
int CSshSession::DirEntryState(const std::string& path)
{
    int code = 0;
    LIBSSH2_SFTP_HANDLE* dir = RunPtr(
        [&]
        {
            return libssh2_sftp_open_ex(sftp_, path.data(), static_cast<unsigned int>(path.size()), 0, 0,
                                        LIBSSH2_SFTP_OPENDIR);
        },
        timeoutMs_, code);
    if (dir == nullptr)
        return -1;
    std::vector<char> name(kMaxNameBytes);
    int state = 0;
    for (;;)
    {
        LIBSSH2_SFTP_ATTRIBUTES attrs = {};
        int rc = Run(
            [&] { return libssh2_sftp_readdir_ex(dir, name.data(), name.size(), nullptr, 0, &attrs); }, timeoutMs_);
        if (rc == 0)
            break;
        if (rc < 0)
        {
            state = -1;
            break;
        }
        std::string n(name.data(), static_cast<size_t>(rc));
        if (n != "." && n != "..")
        {
            state = 1;
            break;
        }
    }
    CloseSftpHandle(dir);
    return state;
}

bool CSshSession::Rmdir(const std::string& path)
{
    if (!BeginSftp("rmdir", path))
        return false;
    int rc = Run(
        [&] { return libssh2_sftp_rmdir_ex(sftp_, path.data(), static_cast<unsigned int>(path.size())); },
        timeoutMs_);
    if (rc == 0)
        return true;
    FailSftp(SshMsg::SftpDelete, "Cannot remove the directory", path, rc);
    // OpenSSH answers FAILURE for a non-empty directory: look inside to tell
    if (error_.kind == SshErrorKind::Other && error_.sftpStatus == LIBSSH2_FX_FAILURE && DirEntryState(path) == 1)
    {
        error_.kind = SshErrorKind::NotEmpty;
        error_.message = "Cannot remove the directory '" + Display(path) + "': it is not empty";
    }
    return false;
}

bool CSshSession::Remove(const std::string& path)
{
    if (!BeginSftp("remove", path))
        return false;
    int rc = Run(
        [&] { return libssh2_sftp_unlink_ex(sftp_, path.data(), static_cast<unsigned int>(path.size())); },
        timeoutMs_);
    return rc == 0 || FailSftp(SshMsg::SftpDelete, "Cannot delete", path, rc);
}

bool CSshSession::RenameRaw(const std::string& from, const std::string& to, bool overwrite)
{
    const long flags =
        overwrite ? (LIBSSH2_SFTP_RENAME_OVERWRITE | LIBSSH2_SFTP_RENAME_ATOMIC | LIBSSH2_SFTP_RENAME_NATIVE) : 0;
    int rc = Run(
        [&]
        {
            return libssh2_sftp_rename_ex(sftp_, from.data(), static_cast<unsigned int>(from.size()), to.data(),
                                          static_cast<unsigned int>(to.size()), flags);
        },
        timeoutMs_);
    return rc == 0 || FailSftp(SshMsg::SftpRename, "Cannot rename", from, rc);
}

// "." + name + ".sally-" + 16 random hex digits, in the folder of target (raw bytes; a long name is shortened).
std::string CSshSession::AsideName(const std::string& target) const
{
    std::string t = target;
    while (t.size() > 1 && t.back() == '/')
        t.pop_back();
    const size_t slash = t.find_last_of('/');
    const std::string dir = slash == std::string::npos ? std::string() : t.substr(0, slash + 1);
    std::string base = slash == std::string::npos ? t : t.substr(slash + 1);
    if (base.size() > 200)
        base.resize(200);

    unsigned char random[8] = {};
    if (BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
    {
        const ULONGLONG fallback = GetTickCount64() ^ (static_cast<ULONGLONG>(GetCurrentThreadId()) << 32);
        memcpy(random, &fallback, sizeof(random));
    }
    static const char kHex[] = "0123456789abcdef";
    std::string suffix;
    for (unsigned char b : random)
    {
        suffix += kHex[b >> 4];
        suffix += kHex[b & 15];
    }
    return dir + "." + base + ".sally-" + suffix;
}

// The overwrite fallback for SFTP v3 (see Rename in the header): the flagged rename failed because the target
// exists (or is unsupported). The old target is moved aside, the source takes its place, the aside copy is deleted;
// a failed second step puts the old target back. first is the error of the flagged rename.
bool CSshSession::RenameAside(const std::string& from, const std::string& to)
{
    const CSshError first = error_;
    const auto stopped = [this]
    {
        if (IsCancelled())
            return Fail(SshErrorKind::Cancelled, std::string());
        return broken_ ? FailBroken() : true;
    };

    LIBSSH2_SFTP_ATTRIBUTES source = {}, target = {};
    int rc = StatRaw(from, LIBSSH2_SFTP_LSTAT, source);
    if (rc != 0)
        return FailSftp(SshMsg::SftpRename, "Cannot rename", from, rc); // no source: nothing may be touched
    rc = StatRaw(to, LIBSSH2_SFTP_LSTAT, target);
    if (rc == LIBSSH2_ERROR_SFTP_PROTOCOL)
    {
        const unsigned long status = libssh2_sftp_last_error(sftp_);
        if (status == LIBSSH2_FX_NO_SUCH_FILE || status == LIBSSH2_FX_NO_SUCH_PATH)
        {
            // no target to get out of the way: the first rename failed for another reason; a plain one tells which
            if (!RenameRaw(from, to, false))
                return false;
            error_ = CSshError();
            return true;
        }
    }
    if (rc != 0)
        return FailSftp(SshMsg::SftpReadInfo, "Cannot get the attributes of", to, rc);

    std::string realFrom, realTo;
    if (RealPath(from, realFrom) && RealPath(to, realTo) && realFrom == realTo)
    {
        error_ = first; // the same file under two spellings: moving "the target" aside would move the source
        return false;
    }
    if (!stopped())
        return false;

    const bool targetIsDir = (target.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) != 0 &&
                             (target.permissions & LIBSSH2_SFTP_S_IFMT) == LIBSSH2_SFTP_S_IFDIR;
    if (targetIsDir)
    {
        const int state = DirEntryState(to);
        if (!stopped())
            return false;
        if (state != 0)
            return Fail(SshErrorKind::Exists, state == 1 ? SshMsg::ReplaceDirNotEmpty : SshMsg::ReplaceDirUnreadable,
                        {Display(to)},
                        "Cannot replace '" + Display(to) + "': it is " +
                            (state == 1 ? "a directory that is not empty" : "a directory whose content cannot be read"));
    }

    const std::string aside = AsideName(to);
    if (!RenameRaw(to, aside, false))
        return false; // the target was not moved
    if (RenameRaw(from, to, false))
    {
        const bool removed = targetIsDir ? Rmdir(aside) : Remove(aside);
        error_ = CSshError();
        if (!removed)
        {
            error_.message = "Renamed, but the replaced item is left as '" + Display(aside) + "'";
            // English only (msgId None): this is a warning on a successful rename, never shown as an error
        }
        return true;
    }

    // The source could not take the place: put the old target back
    const CSshError failure = error_;
    const bool connectionLevel = failure.libssh2Code != LIBSSH2_ERROR_SFTP_PROTOCOL;
    if (!connectionLevel && RenameRaw(aside, to, false))
    {
        error_ = failure;
        return false;
    }
    error_ = failure;
    error_.message += " (the previous target is left as '" + Display(aside) + "')";
    error_.msgId = SshMsg::None; // the translated sentence would not mention the left-over: English only
    return false;
}

// libssh2 sends the rename flags only on SFTP protocol version 5 or later; servers at version 3 (OpenSSH) get a plain
// SSH_FXP_RENAME that fails when the target exists, and posix-rename@openssh.com is not reachable. With overwrite a
// rename that failed with FAILURE / ALREADY_EXISTS / UNSUPPORTED goes to RenameAside.
bool CSshSession::Rename(const std::string& from, const std::string& to, bool overwrite)
{
    if (!BeginSftp("rename", from, to))
        return false;
    if (from == to)
        return true;
    if (RenameRaw(from, to, overwrite))
        return true;
    if (error_.kind == SshErrorKind::Cancelled || error_.libssh2Code != LIBSSH2_ERROR_SFTP_PROTOCOL)
        return false;
    if (!overwrite)
        return FailExistsIfPresent(to);
    const unsigned long status = error_.sftpStatus;
    if (status != LIBSSH2_FX_FAILURE && status != LIBSSH2_FX_FILE_ALREADY_EXISTS && status != LIBSSH2_FX_OP_UNSUPPORTED)
        return false;
    return RenameAside(from, to);
}

// libssh2 takes "what the link points to" first and the new link's path second (the order OpenSSH's server expects).
bool CSshSession::Symlink(const std::string& target, const std::string& linkPath)
{
    if (!BeginSftp("symlink", linkPath, target))
        return false;
    int rc = Run(
        [&]
        {
            return libssh2_sftp_symlink_ex(sftp_, target.data(), static_cast<unsigned int>(target.size()),
                                           const_cast<char*>(linkPath.data()),
                                           static_cast<unsigned int>(linkPath.size()), LIBSSH2_SFTP_SYMLINK);
        },
        timeoutMs_);
    if (rc == 0)
        return true;
    FailSftp(SshMsg::SftpCreate, "Cannot create the link", linkPath, rc);
    return FailExistsIfPresent(linkPath); // an existing name is "already exists", not a generic failure
}

bool CSshSession::Chmod(const std::string& path, uint32_t mode)
{
    if (!BeginSftp("chmod", path))
        return false;
    LIBSSH2_SFTP_ATTRIBUTES attrs = {};
    attrs.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
    attrs.permissions = mode & 07777;
    int rc = StatRaw(path, LIBSSH2_SFTP_SETSTAT, attrs);
    return rc == 0 || FailSftp(SshMsg::SftpChangeAttrs, "Cannot change the permissions of", path, rc);
}

bool CSshSession::Statvfs(const std::string& path, CSftpVfs& out)
{
    out = CSftpVfs();
    if (!BeginSftp("statvfs", path))
        return false;
    LIBSSH2_SFTP_STATVFS st = {};
    const int rc = Run([&] { return libssh2_sftp_statvfs(sftp_, path.data(), path.size(), &st); }, timeoutMs_);
    if (rc != 0)
        return FailSftp(SshMsg::SftpReadInfo, "Cannot read the free space of", path, rc); // OP_UNSUPPORTED becomes kind Unsupported
    out.bytesTotal = st.f_blocks * st.f_frsize;
    out.bytesFree = st.f_bfree * st.f_frsize;
    out.bytesAvailableToUser = st.f_bavail * st.f_frsize;
    out.files = st.f_files;
    out.filesFree = st.f_ffree;
    out.readOnly = (st.f_flag & LIBSSH2_SFTP_ST_RDONLY) != 0;
    return true;
}

bool CSshSession::SetTimes(const std::string& path, int64_t atime, int64_t mtime)
{
    if (!BeginSftp("settimes", path))
        return false;
    const auto fits = [](int64_t t) { return t >= 0 && static_cast<uint64_t>(t) <= ULONG_MAX; };
    if (!fits(atime) || !fits(mtime))
        return Fail(SshErrorKind::Unsupported,
                    "Cannot set the time of '" + Display(path) + "': libssh2 supports only times from 1970 to 2106");
    LIBSSH2_SFTP_ATTRIBUTES attrs = {};
    attrs.flags = LIBSSH2_SFTP_ATTR_ACMODTIME;
    attrs.atime = static_cast<unsigned long>(atime);
    attrs.mtime = static_cast<unsigned long>(mtime);
    int rc = StatRaw(path, LIBSSH2_SFTP_SETSTAT, attrs);
    return rc == 0 || FailSftp(SshMsg::SftpChangeAttrs, "Cannot set the time of", path, rc);
}

// ---- log lines --------------------------------------------------------------------------------------------------

void CSshSession::Log(SshLogLevel level, const std::string& line) const
{
    if (logSink_ != nullptr)
        logSink_->Log(level, line);
}

// One line per SFTP call, only built when somebody listens. Paths are shown decoded (like in error messages).
void CSshSession::LogVerbose(const char* call, const std::string& rawPath, const std::string& rawPath2) const
{
    if (logSink_ == nullptr)
        return;
    std::string line = std::string("SFTP ") + call;
    if (!rawPath.empty())
        line += " '" + Display(rawPath) + "'";
    if (!rawPath2.empty())
        line += " -> '" + Display(rawPath2) + "'";
    logSink_->Log(SshLogLevel::Verbose, line);
}
