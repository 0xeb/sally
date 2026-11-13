// Session logs: one text per connection (the panel connection, each worker of an operation), shown by the session log
// window. Sally-free. The store is shared by every thread, so all of its methods lock; its lock is a leaf lock (nothing
// else is called while it is held).
//
// C#: CSessionLogStore is a thread-safe List<Log>; CSessionLogSink is the ILogger the SSH session writes to.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "ssh_types.h"

// What a log belongs to. The words of the title ("panel", "operation", "worker") are in the string table, so the model
// keeps the parts and the window words them.
struct CSessionLogTitle
{
    enum class Kind
    {
        Panel,  // the panel connection
        Worker  // one worker of an operation
    };
    Kind kind = Kind::Panel;
    std::wstring target;     // "user@host:port"
    uint64_t operation = 0;  // Worker: the operation's number in the operations list (0 = unknown)
    int worker = 0;          // Worker: 1, 2, ...

    bool operator==(const CSessionLogTitle& o) const
    {
        return kind == o.kind && target == o.target && operation == o.operation && worker == o.worker;
    }
};

// What the window needs to list one log.
struct CSessionLogInfo
{
    unsigned id = 0; // 1, 2, 3 ... never reused
    CSessionLogTitle title;
    bool open = true; // false: the connection is closed, the text stays readable (until the retention limit drops it)
    size_t chars = 0; // size of the text, line breaks included
};

class CSessionLogStore
{
public:
    // A new open log; returns its id.
    unsigned Create(const CSessionLogTitle& title);
    // Adds one line (no line break in it; the store adds "\r\n", which is what an edit control wants). The oldest lines are
    // dropped while the text is longer than maxChars. Unknown ids (e.g. a log dropped by the retention limit) are ignored.
    void Append(unsigned id, const std::wstring& line, size_t maxChars);
    // Marks the log closed. Then only the 'closedKept' most recently closed logs stay; older closed logs are removed.
    void Close(unsigned id, int closedKept);
    // Empties the text of one log (it stays in the list).
    void Clear(unsigned id);

    std::vector<CSessionLogInfo> List() const; // oldest first
    // The whole text (lines joined with "\r\n", each line ends with one); false for an unknown id.
    bool GetText(unsigned id, std::wstring& text) const;
    bool GetInfo(unsigned id, CSessionLogInfo& info) const;

    // Grows with every change (create, line, close, clear, removal). A window polls it and reloads only when it moved.
    unsigned long ChangeCounter() const { return changes_.load(); }

private:
    struct CLog
    {
        CSessionLogInfo info;
        std::deque<std::wstring> lines; // each without line break
        unsigned long closedAt = 0;     // order of closing (retention drops the smallest first)
    };
    CLog* Find(unsigned id); // lock held
    const CLog* Find(unsigned id) const;
    void Changed() { ++changes_; }

    mutable std::mutex lock_;
    std::vector<CLog> logs_; // ordered by id
    unsigned nextId_ = 1;
    unsigned long closeCounter_ = 0;
    std::atomic<unsigned long> changes_{0};
};

// The plugin's store (a function-local static: no start-up order problems).
CSessionLogStore& SessionLogStore();

// Writes the lines of ONE connection into a log of the store: it adds the time stamp, drops lines above the level the
// user chose in the configuration, and takes the size limit from it. The session's owner thread calls Begin, End and
// Log; LogId() may be read from any thread.
class CSessionLogSink : public ISshLogSink
{
public:
    explicit CSessionLogSink(CSessionLogStore& store = SessionLogStore());
    ~CSessionLogSink() override;
    CSessionLogSink(const CSessionLogSink&) = delete;
    CSessionLogSink& operator=(const CSessionLogSink&) = delete;

    // Opens a log with this title. Does nothing when a log with the same title is already open (a reconnect continues
    // it); with another title the old log is closed first.
    void Begin(const CSessionLogTitle& title);
    // Closes the log (the text stays readable). Safe without a log.
    void End();
    // 0 when no log is open.
    unsigned LogId() const { return id_.load(); }

    // ISshLogSink: UTF-8 line from the SSH session.
    void Log(SshLogLevel level, const std::string& utf8Line) override;
    // A line from the plugin itself (operations layer).
    void Line(SshLogLevel level, const std::wstring& text);

private:
    CSessionLogStore& store_;
    std::atomic<unsigned> id_{0};
    CSessionLogTitle title_;
};

// "user@host:port" for the titles of the logs.
std::wstring SessionLogTarget(const std::wstring& user, const std::wstring& host, int port);
CSessionLogTitle PanelLogTitle(const std::wstring& target);
CSessionLogTitle WorkerLogTitle(const std::wstring& target, uint64_t operation, int worker);
