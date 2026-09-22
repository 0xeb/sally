// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <windows.h>
#include <shlobj.h>

namespace sally::clipboard
{
struct HDropSelection
{
    std::wstring SourcePath;
    std::vector<std::wstring> Names;
};

// Decodes a double-null-terminated clipboard string list from its bounded global-memory span.
// The legacy narrow form is process-ACP by the shell clipboard contract.
bool TryDecodeClipboardStringList(const void* data, std::size_t dataSize, bool wide,
                                  std::vector<std::wstring>& strings) noexcept;

// Decodes the complete bounded DROPFILES list without applying same-directory policy.
bool TryDecodeHDropPaths(const DROPFILES* data, std::size_t dataSize,
                         std::vector<std::wstring>& paths) noexcept;

// Decodes the external DROPFILES A/W format pair into one UTF-16 selection.
// Returns false for malformed data or for paths that do not share one directory.
bool TryParseHDropSelection(const DROPFILES* data, std::size_t dataSize,
                            HDropSelection& selection) noexcept;

// Returns the one complete path carried by a single-item drop. Multi-item and
// malformed payloads are rejected.
bool TryGetSingleHDropPath(const DROPFILES* data, std::size_t dataSize,
                           std::wstring& path) noexcept;
} // namespace sally::clipboard
