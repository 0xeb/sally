// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// The update's file operations on the real file system, with long paths.

#pragma once

#include "update/UpdateFiles.h"

namespace Sally::Update
{

class Win32UpdateFileOps : public IUpdateFileOps
{
public:
    PathKind Query(const std::wstring& path) override;
    bool List(const std::wstring& root, std::vector<std::wstring>& files, std::vector<std::wstring>& folders,
              DWORD& error) override;
    bool Rename(const std::wstring& from, const std::wstring& to, DWORD& error) override;
    bool Copy(const std::wstring& from, const std::wstring& to, DWORD& error) override;
    bool RemoveFile(const std::wstring& path, DWORD& error) override;
    bool CreateFolder(const std::wstring& path, DWORD& error) override;
    bool RemoveFolder(const std::wstring& path, DWORD& error) override;
    bool AppendLine(const std::wstring& path, const std::string& utf8Line, DWORD& error) override;
    bool ReadText(const std::wstring& path, std::string& utf8, DWORD& error) override;
};

} // namespace Sally::Update
