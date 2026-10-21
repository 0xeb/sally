// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <string>
#include "common/IPathService.h"

class CSalamanderGeneralAbstract;

struct CFilecompPathContext
{
    std::wstring BaseDirectory;
    std::wstring BaseRoot;
    std::array<std::wstring, 26> DriveDirectories;
};

std::wstring FilecompInitialDirectory(const std::wstring& file);
CFilecompPathContext CaptureFilecompPathContext(CSalamanderGeneralAbstract& general, IPathService& paths,
                                                const std::wstring& baseDirectory = {});
// This producer alone queries the panel and must run in the host's menu callback.
CFilecompPathContext CaptureFilecompSourceContext(CSalamanderGeneralAbstract& general, IPathService& paths);
bool ResolveFilecompInputPath(IPathService& paths, const CFilecompPathContext& context,
                              const std::wstring& input, std::wstring& resolved) noexcept;
bool FilecompFileExists(CSalamanderGeneralAbstract& general, const std::wstring& file) noexcept;
