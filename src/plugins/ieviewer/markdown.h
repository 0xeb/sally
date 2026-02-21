// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

#include <string>

// Convert a Markdown file to a complete HTML document string.
// Returns the HTML as UTF-8, or empty string on failure.
// filePath is a wide string supporting long paths.
std::string ConvertMarkdownToHTML(const std::wstring& filePath);
