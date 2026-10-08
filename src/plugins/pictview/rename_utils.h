// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace pictview
{

void TrimRenameFileName(std::wstring& name);
bool RenamePatternContainsInvalidCharacter(const std::wstring& pattern);
bool FileNameContainsInvalidRenameCharacter(const std::wstring& name);

} // namespace pictview
