#include "session_log.h"

#include <algorithm>
#include <climits>

#include <windows.h>

#include "global_config.h"

namespace
{
const size_t kLineBreakChars = 2; // "\r\n"

// "12:34:56.789 " in local time
std::wstring TimeStamp()
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t text[32];
    swprintf_s(text, L"%02u:%02u:%02u.%03u ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return text;
}

// A log line is one line: line breaks inside it would break the "one line per entry" size accounting.
std::wstring OneLine(std::wstring text)
{
    std::replace(text.begin(), text.end(), L'\r', L' ');
    std::replace(text.begin(), text.end(), L'\n', L' ');
    return text;
}
} // namespace

// ---- CSessionLogStore -----------------------------------------------------------------------------------------

CSessionLogStore::CLog* CSessionLogStore::Find(unsigned id)
{
    for (CLog& log : logs_)
        if (log.info.id == id)
            return &log;
    return nullptr;
}

const CSessionLogStore::CLog* CSessionLogStore::Find(unsigned id) const
{
    for (const CLog& log : logs_)
        if (log.info.id == id)
            return &log;
    return nullptr;
}

unsigned CSessionLogStore::Create(const CSessionLogTitle& title)
{
    std::lock_guard<std::mutex> guard(lock_);
    CLog log;
    log.info.id = nextId_++;
    log.info.title = title;
    logs_.push_back(std::move(log));
    Changed();
    return logs_.back().info.id;
}

void CSessionLogStore::Append(unsigned id, const std::wstring& line, size_t maxChars)
{
    std::lock_guard<std::mutex> guard(lock_);
    CLog* log = Find(id);
    if (log == nullptr)
        return;
    log->lines.push_back(line);
    log->info.chars += line.size() + kLineBreakChars;
    while (log->info.chars > maxChars && !log->lines.empty())
    {
        log->info.chars -= log->lines.front().size() + kLineBreakChars;
        log->lines.pop_front();
    }
    Changed();
}

void CSessionLogStore::Close(unsigned id, int closedKept)
{
    std::lock_guard<std::mutex> guard(lock_);
    CLog* log = Find(id);
    if (log == nullptr || !log->info.open)
        return;
    log->info.open = false;
    log->closedAt = ++closeCounter_;

    // keep the 'closedKept' most recently closed logs, remove the others
    std::vector<unsigned long> closedOrder;
    for (const CLog& l : logs_)
        if (!l.info.open)
            closedOrder.push_back(l.closedAt);
    const size_t keep = closedKept < 0 ? 0 : static_cast<size_t>(closedKept);
    if (closedOrder.size() > keep)
    {
        std::sort(closedOrder.begin(), closedOrder.end());
        // keep == 0: every closed log goes
        const unsigned long firstKept = keep == 0 ? ULONG_MAX : closedOrder[closedOrder.size() - keep];
        logs_.erase(std::remove_if(logs_.begin(), logs_.end(),
                                   [&](const CLog& l) { return !l.info.open && l.closedAt < firstKept; }),
                    logs_.end());
    }
    Changed();
}

void CSessionLogStore::Clear(unsigned id)
{
    std::lock_guard<std::mutex> guard(lock_);
    CLog* log = Find(id);
    if (log == nullptr)
        return;
    log->lines.clear();
    log->info.chars = 0;
    Changed();
}

std::vector<CSessionLogInfo> CSessionLogStore::List() const
{
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<CSessionLogInfo> result;
    result.reserve(logs_.size());
    for (const CLog& log : logs_)
        result.push_back(log.info);
    return result;
}

bool CSessionLogStore::GetText(unsigned id, std::wstring& text) const
{
    std::lock_guard<std::mutex> guard(lock_);
    const CLog* log = Find(id);
    text.clear();
    if (log == nullptr)
        return false;
    text.reserve(log->info.chars);
    for (const std::wstring& line : log->lines)
    {
        text += line;
        text += L"\r\n";
    }
    return true;
}

bool CSessionLogStore::GetInfo(unsigned id, CSessionLogInfo& info) const
{
    std::lock_guard<std::mutex> guard(lock_);
    const CLog* log = Find(id);
    if (log == nullptr)
        return false;
    info = log->info;
    return true;
}

CSessionLogStore& SessionLogStore()
{
    static CSessionLogStore store;
    return store;
}

// ---- CSessionLogSink ------------------------------------------------------------------------------------------

CSessionLogSink::CSessionLogSink(CSessionLogStore& store) : store_(store)
{
}

CSessionLogSink::~CSessionLogSink()
{
    End();
}

void CSessionLogSink::Begin(const CSessionLogTitle& title)
{
    if (id_.load() != 0)
    {
        if (title == title_)
            return;
        End();
    }
    title_ = title;
    id_ = store_.Create(title);
}

void CSessionLogSink::End()
{
    const unsigned id = id_.exchange(0);
    if (id != 0)
        store_.Close(id, GlobalConfigStore().Get().closedLogsKept);
}

void CSessionLogSink::Line(SshLogLevel level, const std::wstring& text)
{
    const unsigned id = id_.load();
    if (id == 0)
        return;
    const CGlobalConfig config = GlobalConfigStore().Get();
    if (static_cast<int>(level) > static_cast<int>(config.logLevel)) // Off (0) lets nothing through
        return;
    store_.Append(id, TimeStamp() + OneLine(text), static_cast<size_t>(config.logMaxKiB) * 1024);
}

void CSessionLogSink::Log(SshLogLevel level, const std::string& utf8Line)
{
    if (id_.load() == 0)
        return;
    Line(level, CNameCodec::Utf8().Decode(utf8Line));
}

std::wstring SessionLogTarget(const std::wstring& user, const std::wstring& host, int port)
{
    return user + L"@" + host + L":" + std::to_wstring(port);
}

CSessionLogTitle PanelLogTitle(const std::wstring& target)
{
    CSessionLogTitle title;
    title.kind = CSessionLogTitle::Kind::Panel;
    title.target = target;
    return title;
}

CSessionLogTitle WorkerLogTitle(const std::wstring& target, uint64_t operation, int worker)
{
    CSessionLogTitle title;
    title.kind = CSessionLogTitle::Kind::Worker;
    title.target = target;
    title.operation = operation;
    title.worker = worker;
    return title;
}
