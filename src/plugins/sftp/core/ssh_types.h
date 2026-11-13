// Plain types shared by the SSH/SFTP core: errors, site settings, directory entries, callbacks.
// No Sally or UI headers here; strings are UTF-8 (messages) or raw bytes (remote names).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "encoding.h"
#include "secret.h"

enum class SshErrorKind
{
    None,             // no error
    Cancelled,        // the user or the plugin cancelled the operation
    ConnectFailed,    // DNS, TCP connect, handshake or connection lost
    Timeout,          // no answer within the site's timeout
    HostKeyRejected,  // the user (or a missing callback) refused the host key
    HostKeyChanged,   // known key differs and was not accepted
    AuthFailed,       // no offered authentication method succeeded
    KeyFileError,     // key file missing, unreadable, corrupt or wrong passphrase
    NoSftp,           // the server has no SFTP subsystem
    NotFound,         // SFTP: no such file or path
    PermissionDenied, // SFTP: permission denied
    Exists,           // SFTP: target already exists
    NotEmpty,         // SFTP: directory not empty
    Unsupported,      // operation not supported by the server
    Protocol,         // unexpected protocol data
    Io,               // local or remote I/O failure (disk full, write failed, ...)
    Other             // anything else; see message
};

// Identifies an error text so the UI layer can show it in the user's language. core/ only knows the ids; the texts
// (patterns with %1, %2, ...) live in the plugin's string table and are looked up in ui/error_text.cpp. The English text stays in
// CSshError::message as the log text and as the fallback for ids without a table entry.
// Each id lists its arguments (CSshError::args, UTF-8).
enum class SshMsg
{
    None,           // no id: only the English message is available
    ResolveFailed,  // %1 host, %2 Winsock error number (the pattern says "Winsock error %2")
    ResolveTimeout, // %1 host, %2 time limit in seconds (plain number)
    ConnectTimeout, // %1 host:port, %2 time limit in seconds (plain number)
    SessionNotOpen, // no arguments
    ConnectFailed,  // %1 host:port, %2 Winsock error number
    NoHostGiven,    // no arguments
    NoHostKey,      // no arguments
    KnownHostsUnchecked, // %1 host
    HostKeyChanged,  // %1 host, %2 fingerprint
    HostKeyRejected, // %1 host, %2 fingerprint
    NoKnownHostsFile,       // no arguments
    KnownHostsWriteFailed,  // %1 file path
    KnownHostsReplaceFailed, // %1 file path
    SftpNeedsAuth,   // no arguments
    NoSftpSubsystem, // no arguments
    BrokenAfterTimeout, // no arguments
    BrokenAfterCancel,  // no arguments
    BrokenAfterError,   // no arguments
    AuthCancelled,      // no arguments
    AuthNeedsConnection, // no arguments
    PasswordExpired,    // no arguments
    AuthFailed,         // %1 user, %2 methods the server offers, %3 methods tried
    AuthFailedNoCredentials, // %1 user, %2 methods the server offers
    KeyFileUnusable,    // %1 key file, %2 reason (English; used only when a .ppk error has no text of its own)
    KeyFileWrongPassphrase, // %1 key file
    KeyFilePathInvalid, // %1 key file path
    KeyFileOpenFailed,  // %1 key file, %2 Windows error number
    KeyFileBadSize,     // %1 key file
    KeyFileReadFailed,  // %1 key file, %2 Windows error number
    // .ppk problems; %1 is the key file (not set by ConvertPpk: added by the authentication code), further arguments as listed
    PpkDecryptFailed,          // %1 key file
    PpkUnsupportedEncryption,  // %1 key file, %2 encryption name
    PpkUnsupportedKdf,         // %1 key file, %2 key derivation name
    PpkUnsupportedKeyType,     // %1 key file, %2 key type name
    PpkUnsupportedOldFormat,   // %1 key file
    PpkNotPuttyFile,           // %1 key file
    PpkDamaged,                // %1 key file (all other "invalid / truncated / incomplete" cases)
    // SFTP actions: "Cannot <do something with> '%1': %2". %1 is the remote path (UTF-8, as shown). ui/error_text composes %2 (the
    // reason) from CSshError::sftpStatus (or the timeout kind); for a failure of the connection itself, args[1] is the
    // English text of libssh2 and is used instead.
    SftpList,        // list a directory, read a directory
    SftpReadInfo,    // attributes of an entry, size, resolve a path, read a link, free space
    SftpReadFile,    // open for download, read
    SftpWriteFile,   // open for upload, write, finish writing
    SftpCreate,      // create a directory or a link
    SftpDelete,      // delete a file or a directory
    SftpRename,      // rename or move; %1 is the source path
    SftpChangeAttrs, // permissions, time
    // Other complete sentences
    SftpNotOpen,          // no arguments
    ResumeFailed,         // %1 remote path
    ServerAcceptedNoData, // %1 remote path
    AlreadyExists,        // %1 path
    ReplaceDirNotEmpty,   // %1 path of the target
    ReplaceDirUnreadable, // %1 path of the target
    LocalSetPosition,     // %1 Windows error number (local file errors of a transfer)
    LocalWrite,           // %1 Windows error number
    LocalGetSize,         // %1 Windows error number
    LocalReadTime,        // %1 Windows error number
    LocalRead,            // %1 Windows error number
    // "<step>: %1": %1 is the English text of libssh2, after a timeout the localized "no answer" reason
    HandshakeFailed,      // %1 see above
    SftpSubsystemFailed,  // %1 English text of libssh2
    AuthInterrupted,      // %1 English text of libssh2
    AuthMethodsQueryFailed, // %1 English text of libssh2
    ExecChannelFailed,    // %1 English text of libssh2
    ExecStartFailed,      // %1 English text of libssh2
    ExecReadFailed,       // %1 English text of libssh2
    ExecFinishFailed,     // %1 English text of libssh2
    NoAnswerTimeout,      // %1 time limit in seconds (plain number)
    ExecIdleTimeout,      // %1 idle limit in seconds (plain number)
    InvalidServerData,    // %1 directory path (invalid entry name or too many entries)
    NotConnected,         // no arguments (a panel request without a connection)
    LocalFileCreateFailed, // %1 Windows error number (the .part file of a panel download)
    LocalFileRenameFailed, // %1 Windows error number (the .part file could not get its final name)
    Count_                // not an id: the number of ids (tests check that each one has a text)
};

struct CSshError
{
    SshErrorKind kind = SshErrorKind::None;
    int libssh2Code = 0;          // LIBSSH2_ERROR_* (0 = none)
    unsigned long sftpStatus = 0; // LIBSSH2_FX_* when libssh2Code is LIBSSH2_ERROR_SFTP_PROTOCOL
    std::string message;          // UTF-8, English: for logs and as the fallback for display
    SshMsg msgId = SshMsg::None;  // translatable text of this error, None = use message
    std::vector<std::string> args; // UTF-8 values for %1, %2, ... of the translated text

    bool IsError() const { return kind != SshErrorKind::None; }
};

// Connection settings of one site. Movable, not copyable (CSecret has no implicit copy).
struct CSshSite
{
    std::string host;
    int port = 22;
    std::string user;
    CSecret password;
    std::string keyFile; // path, UTF-8; empty = none
    CSecret passphrase;
    bool useAgent = false;
    bool compression = false;
    int timeoutSeconds = 30;
    CNameCodec nameCodec; // how remote names are decoded for display / encoded for new names
};

// One directory entry. The raw name is what every later operation must use.
struct CSftpEntry
{
    std::string rawName; // bytes exactly as the server sent them
    uint64_t size = 0;
    int64_t mtime = 0;        // Unix time, seconds
    int64_t atime = 0;        // Unix time, seconds (0 = not reported)
    uint32_t permissions = 0; // st_mode bits
    bool hasPermissions = false; // the server reported the permissions (0 alone can also be a real mode)
    std::string owner;        // may be empty (numeric ids or not reported)
    std::string group;
    bool isDir = false;
    bool isLink = false;
    bool linkTargetIsDir = false; // valid when isLink
};

// File system size of a remote path (the "statvfs@openssh.com" SFTP extension), all in bytes.
struct CSftpVfs
{
    uint64_t bytesTotal = 0;           // f_blocks * f_frsize
    uint64_t bytesFree = 0;            // f_bfree * f_frsize (free for root too)
    uint64_t bytesAvailableToUser = 0; // f_bavail * f_frsize (what a normal user can still write)
    uint64_t files = 0;                // inodes
    uint64_t filesFree = 0;
    bool readOnly = false;
};

// How CSshSession::Exec runs a command.
struct CExecOptions
{
    size_t maxOutputBytes = 1024 * 1024; // more output is read and thrown away (CExecResult::truncated)
    // Gives up (kind Timeout, the session becomes broken) when the server sends nothing for this long. 0 = wait until
    // the command ends or the caller cancels: a quiet long-running command is not an error.
    int idleTimeoutSeconds = 0;
};

// Result of CSshSession::Exec. A command that ran and failed is still a successful Exec: look at exitStatus.
struct CExecResult
{
    std::string output;     // stdout and stderr in the order they arrived (raw bytes; decode with the site codec)
    int exitStatus = 0;     // 0 when the server reported none
    std::string exitSignal; // name of the signal that ended the command ("TERM", ...); empty = ended normally
    bool truncated = false; // the output was longer than maxOutputBytes
};

// What the server told at connect time. libssh2 gives the identification banner only: it does not expose the SFTP
// protocol version the server answered (it speaks version 3 at most) nor the list of SFTP extensions.
struct CSshServerInfo
{
    std::string banner;          // e.g. "SSH-2.0-OpenSSH_9.6"; empty when not known
    int sftpVersionSupported = 0; // highest SFTP version libssh2 speaks (3); the server's own version is not reported
};

// Algorithms the handshake negotiated; empty text = not connected.
struct CSshMethods
{
    std::string kex;
    std::string hostKey;
    std::string cipherClientToServer;
    std::string cipherServerToClient;
    std::string macClientToServer;
    std::string macServerToClient;
    std::string compressionClientToServer;
    std::string compressionServerToClient;
};

// False for a name that cannot be one entry of a directory listing: empty, "." or "..", or containing '/' or NUL.
// (A server could send such names to make a client write outside the folder it asked for.) Not a check for local
// Windows file names.
bool IsValidRemoteEntryName(const std::string& name);

// How important a log line is. A log shows the lines up to the level the user chose (Error < Normal < Verbose).
enum class SshLogLevel
{
    Error = 1,   // something failed
    Normal = 2,  // connect, host key, login, SFTP session, disconnect
    Verbose = 3  // every SFTP call
};

enum class HostKeyDecision
{
    Reject,
    AcceptOnce,
    AcceptAndSave
};

// What a Prompt callback asks for. The core sends no user-facing text of its own: the UI words KeyPassphrase itself.
enum class PromptKind
{
    KeyboardInteractive, // name/instruction/promptText are the server's own texts
    KeyPassphrase        // promptText is the key file (UTF-8); name and instruction are empty
};

// Implemented by the layer above (UI or tests). Called on the thread that owns the session.
class ISshCallbacks
{
public:
    virtual ~ISshCallbacks() = default;

    // Unknown (changed == false) or changed (changed == true) host key. fingerprintSha256 is the OpenSSH form "SHA256:...".
    virtual HostKeyDecision VerifyHostKey(const std::string& host, int port, const std::string& keyType,
                                          const std::string& fingerprintSha256, bool changed) = 0;

    // Keyboard-interactive prompt or passphrase request. Returns false when the user cancels.
    virtual bool Prompt(PromptKind kind, const std::string& name, const std::string& instruction, const std::string& promptText,
                        bool echo, CSecret& answer) = 0;

    // Transfer progress; return false to cancel. total may be 0 when unknown.
    virtual bool Progress(uint64_t done, uint64_t total) = 0;
};

// Receives the session's log lines (set with CSshSession::SetLogSink). Called on the thread that owns the session, so an
// implementation that is read from other threads must lock. C#: like an ILogger passed to the class.
// The text is UTF-8, one line, English. It never contains a password, a passphrase or an answer to a prompt.
class ISshLogSink
{
public:
    virtual ~ISshLogSink() = default;
    virtual void Log(SshLogLevel level, const std::string& utf8Line) = 0;
};
