// SPDX-FileCopyrightText: 2026 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Result of clipboard operations
struct ClipboardResult
{
    bool success;
    uint32_t errorCode;

    static ClipboardResult Ok() { return {true, 0}; }
    static ClipboardResult Error(uint32_t code) { return {false, code}; }
};

// Abstract interface for clipboard operations
// Enables Unicode support, testing via mocks, and cross-platform portability
class IClipboard
{
public:
    virtual ~IClipboard() = default;

    // Text operations (Unicode-first)
    virtual ClipboardResult SetText(const wchar_t* text) = 0;
    virtual ClipboardResult GetText(std::wstring& text) = 0;
    virtual bool HasText() = 0;

    // Check if clipboard has file drop data
    virtual bool HasFileDrop() = 0;

    // Get file paths from clipboard (for paste operations)
    // Returns list of file paths from CF_HDROP or similar format
    virtual ClipboardResult GetFilePaths(std::vector<std::wstring>& paths) = 0;

    // Clear clipboard contents
    virtual ClipboardResult Clear() = 0;

    // Check if clipboard has a specific format (platform-specific format ID)
    virtual bool HasFormat(uint32_t format) = 0;

    // Raw data operations for custom formats
    // Note: Data must remain valid until clipboard is closed
    virtual ClipboardResult SetRawData(uint32_t format, const void* data, size_t size) = 0;
    virtual ClipboardResult GetRawData(uint32_t format, std::vector<uint8_t>& data) = 0;

    // Register a custom clipboard format by name
    // Returns format ID, or 0 on failure
    virtual uint32_t RegisterFormat(const wchar_t* name) = 0;
};

// Global clipboard instance
extern IClipboard* gClipboard;

// Get the Win32 clipboard implementation
IClipboard* GetWin32Clipboard();
