// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cmark-gfm.h"
#include "cmark-gfm-core-extensions.h"
#include "registry.h"

#include "plugindarkmode.h"
#include "webviewer.h"
#include "dbg.h"

#include "markdown.h"
#include "markdown_document.h"
#include "webviewer_text.h"

static std::string LoadMarkdownCSS()
{
    std::wstring path;
    if (!SPLGetModuleFileNameOwned(DLLInstance, path))
    {
        TRACE_E("GetModuleFileNameW() failed");
        return {};
    }
    auto pos = path.rfind(L'\\');
    if (pos == std::wstring::npos)
    {
        TRACE_E("Backslash not found in module path");
        return {};
    }
    std::wstring dir = path.substr(0, pos + 1);

    // Try custom.css first, fall back to githubmd.css
    std::wstring cssPath = dir + L"css\\custom.css";
    FILE* fp = _wfopen(cssPath.c_str(), L"rb");
    if (fp == nullptr)
    {
        cssPath = dir + L"css\\githubmd.css";
        fp = _wfopen(cssPath.c_str(), L"rb");
        if (fp == nullptr)
            return {};
    }

    std::string css;
    char buffer[4096];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0)
        css.append(buffer, bytes);
    fclose(fp);
    return css;
}

static const char* extension_names[] = {
    "autolink",
    "strikethrough",
    "table",
    "tagfilter",
    "tasklist",
    nullptr,
};

std::string ConvertMarkdownToHTML(const std::wstring& filePath)
{
    cmark_gfm_core_extensions_ensure_registered();

    int options = CMARK_OPT_DEFAULT;
    cmark_parser* parser = cmark_parser_new(options);

    for (const char** it = extension_names; *it; ++it)
    {
        cmark_syntax_extension* syntax_extension = cmark_find_syntax_extension(*it);
        if (!syntax_extension)
        {
            TRACE_E("Invalid syntax extension: " << *it);
            cmark_parser_free(parser);
            cmark_release_plugins();
            return {};
        }
        cmark_parser_attach_syntax_extension(parser, syntax_extension);
    }

    FILE* fp = _wfopen(filePath.c_str(), L"rb");
    if (fp == nullptr)
    {
        TRACE_E("_wfopen failed");
        cmark_parser_free(parser);
        cmark_release_plugins();
        return {};
    }

    char buffer[10000];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0)
    {
        cmark_parser_feed(parser, buffer, bytes);
        if (bytes < sizeof(buffer))
            break;
    }
    fclose(fp);

    cmark_node* doc = cmark_parser_finish(parser);
    char* html = cmark_render_html(doc, options, nullptr);

    cmark_node_free(doc);
    cmark_parser_free(parser);

    if (html == nullptr)
    {
        cmark_release_plugins();
        return {};
    }

    // Build the complete HTML document
    std::string css = LoadMarkdownCSS();
    std::string baseHref;
    if (!WebViewerBuildMarkdownBaseHref(filePath, baseHref))
    {
        free(html);
        cmark_release_plugins();
        return {};
    }
    bool useDarkTheme = PluginDarkMode_ShouldUseDark() != FALSE;

    std::string result = BuildMarkdownHtmlDocument(baseHref, css, html, useDarkTheme);

    free(html);
    cmark_release_plugins();

    return result;
}
