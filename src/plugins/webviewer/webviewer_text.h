// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

// File paths are semantic UTF-16 in the plugin. URLs and in-memory HTML are UTF-8 only at
// the WebView2/cmark boundary.
bool WebViewerBuildFileUrl(const std::wstring& filePath, std::wstring& url);
bool WebViewerBuildMarkdownBaseHref(const std::wstring& filePath, std::string& baseHref);
bool WebViewerDecodeHtmlUtf8(const std::string& html, std::wstring& wideHtml);

// The page shown instead of a blank one when WebView2 cannot display a file: the translated
// 'message', then what WebView2 reported ('detail') and the URL, HTML-escaped.
std::wstring WebViewerNavigationErrorHtml(const std::wstring& message, const std::wstring& detail,
                                          const std::wstring& url);
