// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

// Build the final HTML document for Markdown content, including page-level
// theme CSS so WebView2 paints a readable canvas that matches Sally's chosen theme.
std::string BuildMarkdownHtmlDocument(const std::string& baseHref,
                                      const std::string& markdownCss,
                                      const std::string& renderedHtml,
                                      bool useDarkTheme);
