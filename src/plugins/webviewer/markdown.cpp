// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cmark-gfm.h"
#include "cmark-gfm-core-extensions.h"
#include "registry.h"

#include "webviewer.h"
#include "dbg.h"

#include "markdown.h"

static std::string LoadMarkdownCSS()
{
    wchar_t pathBuf[MAX_PATH];
    if (GetModuleFileNameW(DLLInstance, pathBuf, MAX_PATH) == 0)
    {
        TRACE_E("GetModuleFileNameW() failed");
        return {};
    }
    std::wstring path(pathBuf);
    auto pos = path.rfind(L'\\');
    if (pos == std::wstring::npos)
    {
        TRACE_E("Backslash not found in module path");
        return {};
    }
    std::wstring dir = path.substr(0, pos + 1);

    // Try custom.css first, fall back to githubmd.css
    std::wstring cssPath = dir + L"css\\custom.css";
    FILE* fp = _wfopen(cssPath.c_str(), L"r");
    if (fp == nullptr)
    {
        cssPath = dir + L"css\\githubmd.css";
        fp = _wfopen(cssPath.c_str(), L"r");
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

static std::wstring GetDirectoryFromPath(const std::wstring& filePath)
{
    auto pos = filePath.rfind(L'\\');
    if (pos == std::wstring::npos)
        pos = filePath.rfind(L'/');
    if (pos != std::wstring::npos)
        return filePath.substr(0, pos + 1);
    return L".\\";
}

// Convert a directory path to a file:// URL for the <base> tag.
// Backslashes become forward slashes, and the URL is properly formatted.
static std::string MakeBaseHref(const std::wstring& dir)
{
    // Convert wide to UTF-8
    int len = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), (int)dir.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), (int)dir.size(), utf8.data(), len, nullptr, nullptr);

    // Replace backslashes with forward slashes
    for (char& c : utf8)
    {
        if (c == '\\')
            c = '/';
    }

    return "file:///" + utf8;
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

    FILE* fp = _wfopen(filePath.c_str(), L"r");
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

    // Build the complete HTML document
    std::string css = LoadMarkdownCSS();
    std::wstring dir = GetDirectoryFromPath(filePath);
    std::string baseHref = MakeBaseHref(dir);

    std::string result;
    result.reserve(css.size() + strlen(html) + 512);
    result += "<!DOCTYPE html><html lang=\"en\" dir=\"ltr\"><head><meta charset=\"utf-8\">\n";
    result += "<base href=\"";
    result += baseHref;
    result += "\">\n<style>\n";
    result += css;
    result += "\n</style></head><body><article class=\"markdown-body\">\n";
    result += html;
    result += "</article></body></html>\n";

    free(html);
    cmark_release_plugins();

    return result;
}
