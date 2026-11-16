#include "op_trace.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>

namespace
{
std::atomic<OpTraceSink> Sink{nullptr};
} // namespace

void SetOpTraceSink(OpTraceSink sink)
{
    Sink = sink;
}

void OpTrace(const char* format, ...)
{
    const OpTraceSink sink = Sink;
    if (sink == nullptr)
        return;
    char text[512] = {};
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    text[sizeof(text) - 1] = 0;
    sink(written < 0 ? "(trace text could not be formatted)" : text);
}
