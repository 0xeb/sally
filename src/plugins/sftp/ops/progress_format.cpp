#include "progress_format.h"

#include <cstdio>

std::wstring FormatBytes(uint64_t bytes)
{
    static const wchar_t* const units[] = {L"B", L"KB", L"MB", L"GB", L"TB", L"PB"};
    wchar_t buffer[48];
    if (bytes < 1024)
    {
        swprintf(buffer, _countof(buffer), L"%llu B", static_cast<unsigned long long>(bytes));
        return buffer;
    }
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5)
    {
        value /= 1024.0;
        ++unit;
    }
    // 1023.96 KB would print as "1024.0 KB": step up when the rounded value reaches 1024
    if (value >= 1023.95 && unit < 5)
    {
        value /= 1024.0;
        ++unit;
    }
    swprintf(buffer, _countof(buffer), L"%.1f %ls", value, units[unit]);
    return buffer;
}

std::wstring FormatSpeed(double bytesPerSecond)
{
    if (!(bytesPerSecond > 0))
        return L"0 B/s";
    return FormatBytes(static_cast<uint64_t>(bytesPerSecond)) + L"/s";
}

std::wstring FormatDuration(int64_t seconds)
{
    if (seconds < 0)
        seconds = 0;
    const long long h = seconds / 3600;
    const long long m = (seconds / 60) % 60;
    const long long s = seconds % 60;
    wchar_t buffer[48];
    if (h > 0)
        swprintf(buffer, _countof(buffer), L"%lld:%02lld:%02lld", h, m, s);
    else
        swprintf(buffer, _countof(buffer), L"%lld:%02lld", m, s);
    return buffer;
}

std::wstring FormatEta(int64_t etaSeconds)
{
    return etaSeconds < 0 ? std::wstring(L"--:--") : FormatDuration(etaSeconds);
}

int PercentDone(uint64_t done, uint64_t total)
{
    if (total == 0)
        return 0;
    if (done >= total)
        return 100;
    return static_cast<int>(static_cast<double>(done) * 100.0 / static_cast<double>(total));
}

bool IsWorkOver(OperationState state)
{
    return state != OperationState::Running;
}

OperationStatus DecideStatus(OperationState state, bool paused, bool cancelRequested, bool finished)
{
    if (cancelRequested)
        return finished ? OperationStatus::Cancelled : OperationStatus::Cancelling;
    switch (state)
    {
    case OperationState::Running:
        return paused ? OperationStatus::Paused : OperationStatus::Running;
    case OperationState::Finished:
        return OperationStatus::Finished;
    case OperationState::FinishedWithSkips:
        return OperationStatus::FinishedWithSkips;
    case OperationState::FinishedWithErrors:
        return OperationStatus::FinishedWithErrors;
    case OperationState::NeedsUser:
        return OperationStatus::NeedsUser;
    }
    return OperationStatus::Running;
}

std::wstring FormatTextBar(uint64_t done, uint64_t total, int cells)
{
    if (cells < 1)
        cells = 1;
    // the filled cells follow the percentage (integer math, no overflow for big sizes)
    const int filled = PercentDone(done, total) * cells / 100;
    // U+25B0 filled slanted bar (done) and U+25B1 outlined one (rest): a clear difference but still thin; written as
    // escapes so the file stays plain ASCII
    return std::wstring(static_cast<size_t>(filled), L'\x25B0') +
           std::wstring(static_cast<size_t>(cells - filled), L'\x25B1');
}
