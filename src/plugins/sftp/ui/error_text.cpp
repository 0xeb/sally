// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "error_text.h"

#include "text_util.h"

namespace
{
// String-table id of the translatable text of a core error; 0 = no table entry (the English message is shown).
int StringIdOfMsg(SshMsg msg)
{
    switch (msg)
    {
    case SshMsg::ResolveFailed:
        return IDS_ERRMSG_RESOLVE_FAILED;
    case SshMsg::ResolveTimeout:
        return IDS_ERRMSG_RESOLVE_TIMEOUT;
    case SshMsg::ConnectTimeout:
        return IDS_ERRMSG_CONNECT_TIMEOUT;
    case SshMsg::SessionNotOpen:
        return IDS_ERRMSG_SESSION_NOT_OPEN;
    case SshMsg::ConnectFailed:
        return IDS_ERRMSG_CONNECT_FAILED;
    case SshMsg::NoHostGiven:
        return IDS_ERRMSG_NO_HOST_GIVEN;
    case SshMsg::NoHostKey:
        return IDS_ERRMSG_NO_HOST_KEY;
    case SshMsg::KnownHostsUnchecked:
        return IDS_ERRMSG_KNOWN_HOSTS_UNCHECKED;
    case SshMsg::HostKeyChanged:
        return IDS_ERRMSG_HOSTKEY_CHANGED;
    case SshMsg::HostKeyRejected:
        return IDS_ERRMSG_HOSTKEY_REJECTED;
    case SshMsg::NoKnownHostsFile:
        return IDS_ERRMSG_NO_KNOWN_HOSTS_FILE;
    case SshMsg::KnownHostsWriteFailed:
        return IDS_ERRMSG_KNOWN_HOSTS_WRITE_FAILED;
    case SshMsg::KnownHostsReplaceFailed:
        return IDS_ERRMSG_KNOWN_HOSTS_REPLACE_FAILED;
    case SshMsg::SftpNeedsAuth:
        return IDS_ERRMSG_SFTP_NEEDS_AUTH;
    case SshMsg::NoSftpSubsystem:
        return IDS_ERRMSG_NO_SFTP_SUBSYSTEM;
    case SshMsg::BrokenAfterTimeout:
        return IDS_ERRMSG_BROKEN_TIMEOUT;
    case SshMsg::BrokenAfterCancel:
        return IDS_ERRMSG_BROKEN_CANCEL;
    case SshMsg::BrokenAfterError:
        return IDS_ERRMSG_BROKEN_ERROR;
    case SshMsg::AuthCancelled:
        return IDS_ERRMSG_AUTH_CANCELLED;
    case SshMsg::AuthNeedsConnection:
        return IDS_ERRMSG_AUTH_NEEDS_CONNECTION;
    case SshMsg::PasswordExpired:
        return IDS_ERRMSG_PASSWORD_EXPIRED;
    case SshMsg::AuthFailed:
        return IDS_ERRMSG_AUTH_FAILED;
    case SshMsg::AuthFailedNoCredentials:
        return IDS_ERRMSG_AUTH_FAILED_NO_CREDENTIALS;
    case SshMsg::KeyFileUnusable:
        return IDS_ERRMSG_KEYFILE_UNUSABLE;
    case SshMsg::KeyFileWrongPassphrase:
        return IDS_ERRMSG_KEYFILE_WRONG_PASSPHRASE;
    case SshMsg::KeyFilePathInvalid:
        return IDS_ERRMSG_KEYFILE_PATH_INVALID;
    case SshMsg::KeyFileOpenFailed:
        return IDS_ERRMSG_KEYFILE_OPEN_FAILED;
    case SshMsg::KeyFileBadSize:
        return IDS_ERRMSG_KEYFILE_BAD_SIZE;
    case SshMsg::KeyFileReadFailed:
        return IDS_ERRMSG_KEYFILE_READ_FAILED;
    case SshMsg::PpkDecryptFailed:
        return IDS_ERRMSG_PPK_DECRYPT_FAILED;
    case SshMsg::PpkUnsupportedEncryption:
        return IDS_ERRMSG_PPK_UNSUPPORTED_ENCRYPTION;
    case SshMsg::PpkUnsupportedKdf:
        return IDS_ERRMSG_PPK_UNSUPPORTED_KDF;
    case SshMsg::PpkUnsupportedKeyType:
        return IDS_ERRMSG_PPK_UNSUPPORTED_KEY_TYPE;
    case SshMsg::PpkUnsupportedOldFormat:
        return IDS_ERRMSG_PPK_UNSUPPORTED_OLD_FORMAT;
    case SshMsg::PpkNotPuttyFile:
        return IDS_ERRMSG_PPK_NOT_PUTTY_FILE;
    case SshMsg::PpkDamaged:
        return IDS_ERRMSG_PPK_DAMAGED;
    case SshMsg::SftpList:
        return IDS_ERRMSG_SFTP_LIST;
    case SshMsg::SftpReadInfo:
        return IDS_ERRMSG_SFTP_READ_INFO;
    case SshMsg::SftpReadFile:
        return IDS_ERRMSG_SFTP_READ_FILE;
    case SshMsg::SftpWriteFile:
        return IDS_ERRMSG_SFTP_WRITE_FILE;
    case SshMsg::SftpCreate:
        return IDS_ERRMSG_SFTP_CREATE;
    case SshMsg::SftpDelete:
        return IDS_ERRMSG_SFTP_DELETE;
    case SshMsg::SftpRename:
        return IDS_ERRMSG_SFTP_RENAME;
    case SshMsg::SftpChangeAttrs:
        return IDS_ERRMSG_SFTP_CHANGE_ATTRS;
    case SshMsg::SftpNotOpen:
        return IDS_ERRMSG_SFTP_NOT_OPEN;
    case SshMsg::ResumeFailed:
        return IDS_ERRMSG_RESUME_FAILED;
    case SshMsg::ServerAcceptedNoData:
        return IDS_ERRMSG_SERVER_ACCEPTED_NO_DATA;
    case SshMsg::AlreadyExists:
        return IDS_ERRMSG_ALREADY_EXISTS;
    case SshMsg::ReplaceDirNotEmpty:
        return IDS_ERRMSG_REPLACE_DIR_NOT_EMPTY;
    case SshMsg::ReplaceDirUnreadable:
        return IDS_ERRMSG_REPLACE_DIR_UNREADABLE;
    case SshMsg::LocalSetPosition:
        return IDS_ERRMSG_LOCAL_SET_POSITION;
    case SshMsg::LocalWrite:
        return IDS_ERRMSG_LOCAL_WRITE;
    case SshMsg::LocalGetSize:
        return IDS_ERRMSG_LOCAL_GET_SIZE;
    case SshMsg::LocalReadTime:
        return IDS_ERRMSG_LOCAL_READ_TIME;
    case SshMsg::LocalRead:
        return IDS_ERRMSG_LOCAL_READ;
    case SshMsg::HandshakeFailed:
        return IDS_ERRMSG_HANDSHAKE_FAILED;
    case SshMsg::SftpSubsystemFailed:
        return IDS_ERRMSG_SFTP_SUBSYSTEM_FAILED;
    case SshMsg::AuthInterrupted:
        return IDS_ERRMSG_AUTH_INTERRUPTED;
    case SshMsg::AuthMethodsQueryFailed:
        return IDS_ERRMSG_AUTH_METHODS_QUERY_FAILED;
    case SshMsg::ExecChannelFailed:
        return IDS_ERRMSG_EXEC_CHANNEL_FAILED;
    case SshMsg::ExecStartFailed:
        return IDS_ERRMSG_EXEC_START_FAILED;
    case SshMsg::ExecReadFailed:
        return IDS_ERRMSG_EXEC_READ_FAILED;
    case SshMsg::ExecFinishFailed:
        return IDS_ERRMSG_EXEC_FINISH_FAILED;
    case SshMsg::NoAnswerTimeout:
        return IDS_ERRMSG_NO_ANSWER_TIMEOUT;
    case SshMsg::ExecIdleTimeout:
        return IDS_ERRMSG_EXEC_IDLE_TIMEOUT;
    case SshMsg::InvalidServerData:
        return IDS_ERRMSG_INVALID_SERVER_DATA;
    case SshMsg::NotConnected:
        return IDS_ERRMSG_NOT_CONNECTED;
    case SshMsg::LocalFileCreateFailed:
        return IDS_ERRMSG_LOCAL_FILE_CREATE_FAILED;
    case SshMsg::LocalFileRenameFailed:
        return IDS_ERRMSG_LOCAL_FILE_RENAME_FAILED;
    default:
        return 0;
    }
}

// The string of the table, or an empty string when the id has no entry or the entry is empty.
// Not LangStr/SPLLoadStr: those return the text "ERROR LOADING STRING" for a missing entry, which would hide the
// English fallback. LoadStringW with length 0 only returns the length of the read-only resource string (0 = none).
std::wstring OptionalLangStr(int id)
{
    const wchar_t* text = nullptr;
    const int length = LoadStringW(HLanguage, id, reinterpret_cast<LPWSTR>(&text), 0);
    return length > 0 ? std::wstring(text, length) : std::wstring();
}

// String-table id of the reason phrase of an SFTP action sentence; 0 for ErrorReason::None.
int StringIdOfReason(ErrorReason reason)
{
    switch (reason)
    {
    case ErrorReason::NotFound:
        return IDS_ERRMSG_REASON_NOT_FOUND;
    case ErrorReason::PermissionDenied:
        return IDS_ERRMSG_REASON_PERMISSION_DENIED;
    case ErrorReason::AlreadyExists:
        return IDS_ERRMSG_REASON_EXISTS;
    case ErrorReason::DirNotEmpty:
        return IDS_ERRMSG_REASON_DIR_NOT_EMPTY;
    case ErrorReason::NotADirectory:
        return IDS_ERRMSG_REASON_NOT_A_DIRECTORY;
    case ErrorReason::NoSpace:
        return IDS_ERRMSG_REASON_NO_SPACE;
    case ErrorReason::WriteProtected:
        return IDS_ERRMSG_REASON_WRITE_PROTECTED;
    case ErrorReason::Unsupported:
        return IDS_ERRMSG_REASON_UNSUPPORTED;
    case ErrorReason::ConnectionLost:
        return IDS_ERRMSG_REASON_CONNECTION_LOST;
    case ErrorReason::NoAnswer:
        return IDS_ERRMSG_REASON_NO_ANSWER;
    case ErrorReason::Failed:
        return IDS_ERRMSG_REASON_FAILED;
    default:
        return 0;
    }
}

// The arguments of the pattern; the choices are made in ComposeErrorArgs (ops/text_util), here only the texts are loaded.
std::vector<std::string> ArgsOfError(const CSshError& error)
{
    const int reasonId = StringIdOfReason(ReasonOfError(error));
    const std::wstring reason = reasonId != 0 ? OptionalLangStr(reasonId) : std::wstring();
    return ComposeErrorArgs(error, reason, LangStr(IDS_ERRMSG_REASON_FAILED));
}
} // namespace

std::wstring LangStrForMsg(SshMsg msg)
{
    const int id = StringIdOfMsg(msg);
    return id == 0 ? std::wstring() : OptionalLangStr(id);
}

std::wstring DescribeError(const CSshError& error)
{
    const CSshErrorLabels labels = {LangStr(IDS_ERR_UNKNOWN), LangStr(IDS_ERRMSG_SUFFIX_CODE),
                                    LangStr(IDS_ERRMSG_SUFFIX_CODE_STATUS)};
    std::wstring translated;
    if (error.msgId != SshMsg::None)
    {
        const std::wstring pattern = LangStrForMsg(error.msgId);
        if (!pattern.empty())
            translated = FormatErrorMessage(pattern, ArgsOfError(error));
    }
    else if (error.kind == SshErrorKind::Cancelled) // the English messages of a cancel carry no id
        translated = LangStr(IDS_ERRMSG_CANCELLED);
    return DescribeSshError(error, labels, translated); // empty translated text = the English message
}
