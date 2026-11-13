// Running a command on the server (CSshSession::Exec). Every libssh2 call goes through Run/RunPtr, so the cancel flag,
// the idle timeout and the "broken session" rules are the same as for the SFTP calls.
#include <libssh2.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <string>
#include <vector>

#include "ssh_session.h"

namespace
{
constexpr size_t kChunkBytes = 32 * 1024;
constexpr int kNoTimeoutMs = INT_MAX; // about 24 days: "wait for the command"

void AppendCapped(CExecResult& out, size_t cap, const char* data, size_t size)
{
    const size_t room = cap > out.output.size() ? cap - out.output.size() : 0;
    const size_t take = (std::min)(room, size);
    out.output.append(data, take);
    if (take < size)
        out.truncated = true; // the rest is dropped, but it is still read so the command does not block on a full pipe
}

// One round of reading, called by Run until it stops returning EAGAIN: takes whatever stdout and stderr have ready and
// appends it to out.output (arrival order, one text: for a command line the user wants to see what a terminal would).
// Returns 0 when the command has closed its output, EAGAIN when the caller should wait for the socket, an error code
// otherwise. EAGAIN while cancelled makes Run notice the cancel (even a command that never stops writing can be cancelled).
int ReadPass(LIBSSH2_CHANNEL* channel, size_t cap, CExecResult& out, std::vector<char>& buffer,
             const std::atomic<bool>& cancelled)
{
    const int streams[2] = {0, SSH_EXTENDED_DATA_STDERR}; // 0 = stdout
    int emptyRoundsWithQueuedData = 0;
    // EAGAIN means "wait for the socket", which is only right when nothing is left to read here: libssh2 sends its
    // window adjustment from inside a read call, so a server that waits for it never makes the socket readable again.
    // Hence no limit on the rounds; the cancel flag is looked at in every round instead.
    for (;;)
    {
        bool gotData = false;
        for (int stream : streams)
        {
            const auto n = libssh2_channel_read_ex(channel, stream, buffer.data(), buffer.size());
            if (n > 0)
            {
                AppendCapped(out, cap, buffer.data(), static_cast<size_t>(n));
                gotData = true;
            }
            else if (n < 0 && n != LIBSSH2_ERROR_EAGAIN)
                return static_cast<int>(n);
        }
        if (cancelled.load())
            return LIBSSH2_ERROR_EAGAIN;
        if (gotData)
        {
            emptyRoundsWithQueuedData = 0;
            continue;
        }
        // Reading one stream can pull packets of the other stream off the socket into libssh2's queue and still
        // answer EAGAIN for itself: look at the queue before sleeping on the socket (a few rounds at most, in case the
        // queued data is of a kind that is neither stdout nor stderr).
        unsigned long queued = 0;
        libssh2_channel_window_read_ex(channel, &queued, nullptr);
        if (queued > 0 && ++emptyRoundsWithQueuedData < 4)
            continue;
        return libssh2_channel_eof(channel) == 1 ? 0 : LIBSSH2_ERROR_EAGAIN;
    }
}
} // namespace

bool CSshSession::Exec(const std::string& command, const CExecOptions& options, CExecResult& out)
{
    out = CExecResult();
    if (IsCancelled())
        return Fail(SshErrorKind::Cancelled, std::string());
    if (broken_)
        return FailBroken();
    if (session_ == nullptr || !authenticated_)
        return Fail(SshErrorKind::Other, SshMsg::SessionNotOpen, {}, "The SSH session is not open");

    // the command text itself is not logged: it may contain a secret
    Log(SshLogLevel::Verbose, "SSH exec (" + std::to_string(command.size()) + " bytes of command)");
    int code = 0;
    LIBSSH2_CHANNEL* channel =
        RunPtr([&] { return libssh2_channel_open_session(session_); }, timeoutMs_, code);
    if (channel == nullptr)
        return FailExec(SshMsg::ExecChannelFailed, "Cannot open a channel for the command", code);

    const bool ok = ExecOnChannel(channel, command, options, out);
    const CSshError error = error_;
    CloseChannel(channel);
    if (!ok)
        error_ = error; // closing must not replace the reason of the failure
    return ok;
}

bool CSshSession::ExecOnChannel(LIBSSH2_CHANNEL* channel, const std::string& command, const CExecOptions& options,
                                CExecResult& out)
{
    int rc = Run(
        [&]
        {
            // process_startup instead of the libssh2_channel_exec macro: the length is given, not found by strlen
            return libssh2_channel_process_startup(channel, "exec", 4, command.data(),
                                                   static_cast<unsigned int>(command.size()));
        },
        timeoutMs_);
    if (rc != 0)
        return FailExec(SshMsg::ExecStartFailed, "The server did not start the command", rc);

    const int readTimeoutMs =
        options.idleTimeoutSeconds > 0 ? (std::min)(options.idleTimeoutSeconds, INT_MAX / 1000) * 1000 : kNoTimeoutMs;
    std::vector<char> buffer(kChunkBytes);
    rc = Run([&] { return ReadPass(channel, options.maxOutputBytes, out, buffer, cancelled_); }, readTimeoutMs);
    if (rc != 0)
        return FailExecWaiting(SshMsg::ExecReadFailed, "Reading the output of the command failed", rc,
                               options.idleTimeoutSeconds);

    // The exit status and signal arrive just before the channel closes. The wait is "until the command ends" for
    // commands that close their output early and keep running.
    rc = Run([&] { return libssh2_channel_close(channel); }, timeoutMs_);
    if (rc == 0)
        rc = Run([&] { return libssh2_channel_wait_closed(channel); }, readTimeoutMs);
    if (rc != 0)
        return FailExecWaiting(SshMsg::ExecFinishFailed, "The command did not finish cleanly", rc,
                               options.idleTimeoutSeconds);

    out.exitStatus = libssh2_channel_get_exit_status(channel);
    char* signal = nullptr;
    size_t signalLength = 0;
    libssh2_channel_get_exit_signal(channel, &signal, &signalLength, nullptr, nullptr, nullptr, nullptr);
    if (signal != nullptr)
    {
        out.exitSignal.assign(signal, signalLength);
        libssh2_free(session_, signal); // libssh2 allocated it
    }
    return true;
}

// FailExec for a step that waits for the command: its timeout is the caller's idle timeout, not the site's.
bool CSshSession::FailExecWaiting(SshMsg msgId, const std::string& what, int code, int idleTimeoutSeconds)
{
    FailExec(msgId, what, code);
    if (code == LIBSSH2_ERROR_TIMEOUT && error_.kind == SshErrorKind::Timeout) // not after a cancel
    {
        error_.message = what + ": no output for " + std::to_string(idleTimeoutSeconds) + " s";
        error_.msgId = SshMsg::ExecIdleTimeout;
        error_.args = {std::to_string(idleTimeoutSeconds)};
    }
    return false;
}

// A refusal of the channel or of the exec request is "the server does not allow commands" (kind Unsupported), not a
// connection problem: the connection is fine and the SFTP part keeps working.
bool CSshSession::FailExec(SshMsg msgId, const std::string& what, int code)
{
    FailFromSession(what, code, msgId);
    const bool refused = code == LIBSSH2_ERROR_CHANNEL_REQUEST_DENIED || code == LIBSSH2_ERROR_CHANNEL_FAILURE ||
                         code == LIBSSH2_ERROR_CHANNEL_OUTOFORDER;
    if (refused && error_.kind != SshErrorKind::Cancelled)
        error_.kind = SshErrorKind::Unsupported;
    return false;
}

// Frees the channel. On a healthy session the free first closes it properly (through Run). After a cancel or on a broken
// session the socket is already shut down: plain calls fail at once and libssh2 forgets the channel.
void CSshSession::CloseChannel(LIBSSH2_CHANNEL* channel)
{
    if (channel == nullptr)
        return;
    if (IsCancelled() || broken_)
    {
        for (int i = 0; i < 10; ++i)
            if (libssh2_channel_free(channel) != LIBSSH2_ERROR_EAGAIN)
                break;
        return;
    }
    Run([channel] { return libssh2_channel_free(channel); }, timeoutMs_);
}
