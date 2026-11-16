#pragma once

// Diagnostics of the operations layer. ops/ knows nothing about Sally, so it only calls a sink that the plugin sets at
// start (fs/sftp.cpp forwards the text to TRACE_I); without a sink (unit tests) nothing happens. printf-style, ASCII text.
using OpTraceSink = void (*)(const char* text);

void SetOpTraceSink(OpTraceSink sink);
void OpTrace(const char* format, ...);
