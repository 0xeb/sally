// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "markdown_document.h"

#include <cstring>
#include <sstream>

static std::string GetMarkdownThemeCss(bool useDarkTheme)
{
    const char* colorScheme = useDarkTheme ? "dark" : "light";
    const char* pageBg = useDarkTheme ? "#0d1117" : "#ffffff";
    const char* surfaceBg = useDarkTheme ? "#0d1117" : "#ffffff";
    const char* text = useDarkTheme ? "#c9d1d9" : "#24292e";
    const char* muted = useDarkTheme ? "#8b949e" : "#57606a";
    const char* link = useDarkTheme ? "#58a6ff" : "#0969da";
    const char* border = useDarkTheme ? "#30363d" : "#d0d7de";
    const char* codeBg = useDarkTheme ? "#161b22" : "#f6f8fa";
    const char* codeText = useDarkTheme ? "#c9d1d9" : "#24292e";
    const char* quoteBorder = useDarkTheme ? "#3b434b" : "#d0d7de";
    const char* quoteText = useDarkTheme ? "#8b949e" : "#57606a";
    const char* tableAltBg = useDarkTheme ? "#161b22" : "#f6f8fa";
    const char* hr = useDarkTheme ? "#30363d" : "#d0d7de";

    std::ostringstream css;
    css
        << "html {\n"
        << "  color-scheme: " << colorScheme << ";\n"
        << "}\n\n"
        << ":root {\n"
        << "  --md-page-bg: " << pageBg << ";\n"
        << "  --md-surface-bg: " << surfaceBg << ";\n"
        << "  --md-text: " << text << ";\n"
        << "  --md-muted: " << muted << ";\n"
        << "  --md-link: " << link << ";\n"
        << "  --md-border: " << border << ";\n"
        << "  --md-code-bg: " << codeBg << ";\n"
        << "  --md-code-text: " << codeText << ";\n"
        << "  --md-quote-border: " << quoteBorder << ";\n"
        << "  --md-quote-text: " << quoteText << ";\n"
        << "  --md-table-alt-bg: " << tableAltBg << ";\n"
        << "  --md-hr: " << hr << ";\n"
        << "}\n\n"
        << "html,\n"
        << "body {\n"
        << "  margin: 0;\n"
        << "  padding: 0;\n"
        << "  background-color: var(--md-page-bg);\n"
        << "  color: var(--md-text);\n"
        << "}\n\n"
        << ".markdown-body {\n"
        << "  box-sizing: border-box;\n"
        << "  min-height: 100vh;\n"
        << "  margin: 0 auto;\n"
        << "  padding: 32px;\n"
        << "  background-color: var(--md-surface-bg);\n"
        << "  color: var(--md-text);\n"
        << "}\n\n"
        << "@media (max-width: 767px) {\n"
        << "  .markdown-body {\n"
        << "    padding: 16px;\n"
        << "  }\n"
        << "}\n\n"
        << ".markdown-body,\n"
        << ".markdown-body p,\n"
        << ".markdown-body li,\n"
        << ".markdown-body ul,\n"
        << ".markdown-body ol,\n"
        << ".markdown-body dl,\n"
        << ".markdown-body td,\n"
        << ".markdown-body th,\n"
        << ".markdown-body strong {\n"
        << "  color: var(--md-text);\n"
        << "}\n\n"
        << ".markdown-body h1,\n"
        << ".markdown-body h2,\n"
        << ".markdown-body h3,\n"
        << ".markdown-body h4,\n"
        << ".markdown-body h5,\n"
        << ".markdown-body h6 {\n"
        << "  color: var(--md-text);\n"
        << "}\n\n"
        << ".markdown-body h1,\n"
        << ".markdown-body h2 {\n"
        << "  border-bottom-color: var(--md-border);\n"
        << "}\n\n"
        << ".markdown-body a {\n"
        << "  color: var(--md-link);\n"
        << "}\n\n"
        << ".markdown-body hr {\n"
        << "  background-color: var(--md-hr) !important;\n"
        << "}\n\n"
        << ".markdown-body blockquote {\n"
        << "  color: var(--md-quote-text);\n"
        << "  border-left-color: var(--md-quote-border);\n"
        << "}\n\n"
        << ".markdown-body table td,\n"
        << ".markdown-body table th {\n"
        << "  border-color: var(--md-border);\n"
        << "}\n\n"
        << ".markdown-body table tr {\n"
        << "  background-color: var(--md-surface-bg);\n"
        << "  border-top-color: var(--md-border);\n"
        << "}\n\n"
        << ".markdown-body table tr:nth-child(2n) {\n"
        << "  background-color: var(--md-table-alt-bg);\n"
        << "}\n\n"
        << ".markdown-body code,\n"
        << ".markdown-body pre,\n"
        << ".markdown-body .highlight pre {\n"
        << "  background-color: var(--md-code-bg);\n"
        << "  color: var(--md-code-text);\n"
        << "}\n\n"
        << ".markdown-body pre > code {\n"
        << "  background-color: transparent;\n"
        << "}\n\n"
        << ".markdown-body img {\n"
        << "  background-color: transparent;\n"
        << "}\n\n"
        << ".markdown-body .pl-c {\n"
        << "  color: var(--md-muted);\n"
        << "}\n";

    return css.str();
}

std::string BuildMarkdownHtmlDocument(const std::string& baseHref,
                                      const std::string& markdownCss,
                                      const std::string& renderedHtml,
                                      bool useDarkTheme)
{
    std::string themeCss = GetMarkdownThemeCss(useDarkTheme);
    const char* colorScheme = useDarkTheme ? "dark" : "light";

    std::string result;
    result.reserve(markdownCss.size() + renderedHtml.size() + themeCss.size() + 1024);
    result += "<!DOCTYPE html><html lang=\"en\" dir=\"ltr\"><head><meta charset=\"utf-8\">\n";
    result += "<meta name=\"color-scheme\" content=\"";
    result += colorScheme;
    result += "\">\n";
    result += "<base href=\"";
    result += baseHref;
    result += "\">\n<style>\n";
    result += markdownCss;
    result += "\n</style>\n<style>\n";
    result += themeCss;
    result += "\n</style></head><body><article class=\"markdown-body\">\n";
    result += renderedHtml;
    result += "</article></body></html>\n";
    return result;
}
