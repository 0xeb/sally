// Pure helpers for the operation window: numbers to text and the decision which status the operation is in.
// Sally-free and without localized text: the UI maps OperationStatus to a string of its language.
#pragma once

#include <cstdint>
#include <string>

#include "op_queue.h"

// "0 B", "512 B", "1.5 KB", "12.3 MB" (1024 based; the unit names are the same in every language).
std::wstring FormatBytes(uint64_t bytes);
// FormatBytes + "/s"; speed <= 0 gives "0 B/s".
std::wstring FormatSpeed(double bytesPerSecond);
// "m:ss" below an hour, "h:mm:ss" above; negative values give "0:00".
std::wstring FormatDuration(int64_t seconds);
// FormatDuration, or "--:--" for an unknown time (negative).
std::wstring FormatEta(int64_t etaSeconds);
// 0..100. total == 0 gives 0; done >= total gives 100.
int PercentDone(uint64_t done, uint64_t total);
// A thin text progress bar of 'cells' characters for places that can only show text (Sally's wait window):
// filled part U+25B0 (filled bar), rest U+25B1 (outlined bar). total == 0 gives an empty bar; done >= total a full one.
std::wstring FormatTextBar(uint64_t done, uint64_t total, int cells);

// What the window says about the operation in one line.
enum class OperationStatus
{
    Running,
    Paused,
    Cancelling,         // cancel requested, the workers are still ending
    Cancelled,
    Finished,
    FinishedWithSkips,
    FinishedWithErrors,
    NeedsUser
};

// state: the queue's state; paused: COperation::IsPaused(); cancelRequested: COperation::WasCancelled();
// finished: COperation::IsFinished() (the workers have ended after a cancel).
OperationStatus DecideStatus(OperationState state, bool paused, bool cancelRequested, bool finished);

// True when the work is over for now (nothing is waiting or running): Finished, with skips/errors, or parked items.
bool IsWorkOver(OperationState state);
