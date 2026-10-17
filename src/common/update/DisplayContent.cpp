// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "DisplayContent.h"
#include <yyjson/yyjson.h>
#include <cmark-gfm.h>
#include <algorithm>
#include <memory>
#include <set>

namespace Sally::Update
{
    namespace
    {
        using Doc = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
        bool Structure(yyjson_val* value, unsigned depth = 0)
        {
            if (!value || depth > 64)
                return false;
            if (yyjson_is_obj(value))
            {
                std::set<std::string> names;
                size_t i, max;
                yyjson_val *key, *child;
                yyjson_obj_foreach(value, i, max, key, child)
                {
                    if (!names.emplace(yyjson_get_str(key), yyjson_get_len(key)).second || !Structure(child, depth + 1))
                        return false;
                }
            }
            else if (yyjson_is_arr(value))
            {
                size_t i, max;
                yyjson_val* child;
                yyjson_arr_foreach(value, i, max, child) if (!Structure(child, depth + 1)) return false;
            }
            return true;
        }
        Doc Read(std::string_view bytes, size_t maximum)
        {
            Doc doc(nullptr, yyjson_doc_free);
            if (bytes.empty() || bytes.size() > maximum || !Supporter::ValidUtf8(bytes))
                return doc;
            doc.reset(yyjson_read(bytes.data(), bytes.size(), 0));
            if (doc && !Structure(yyjson_doc_get_root(doc.get())))
                doc.reset();
            return doc;
        }
        bool Text(yyjson_val* value, std::string& out, size_t maximum)
        {
            if (!yyjson_is_str(value) || yyjson_get_len(value) > maximum)
                return false;
            out.assign(yyjson_get_str(value), yyjson_get_len(value));
            return out.find('\0') == std::string::npos;
        }
        bool Number(yyjson_val* obj, const char* key, uint64_t& out, bool required = false)
        {
            auto value = yyjson_obj_get(obj, key);
            if (!value)
                return !required;
            if (!yyjson_is_uint(value) || yyjson_get_uint(value) > 253402300799ULL)
                return false;
            out = yyjson_get_uint(value);
            return true;
        }
        bool Locales(yyjson_val* object, ContentLocales& out, size_t maximum)
        {
            if (!yyjson_is_obj(object))
                return false;
            size_t i, max;
            yyjson_val *key, *value;
            yyjson_obj_foreach(object, i, max, key, value)
            {
                LocalizedContent content;
                if (!yyjson_is_obj(value) || !Text(yyjson_obj_get(value, "title"), content.Title, 256) ||
                    !Text(yyjson_obj_get(value, "markdown"), content.Markdown, maximum))
                    return false;
                out.emplace(std::string(yyjson_get_str(key), yyjson_get_len(key)), std::move(content));
            }
            return true;
        }
        bool Parse(std::string_view bytes, bool live, std::string_view version, uint64_t now, DisplayContent& out)
        {
            out = {};
            auto doc = Read(bytes, 512 * 1024);
            if (!doc)
                return false;
            auto root = yyjson_doc_get_root(doc.get());
            auto schema = yyjson_obj_get(root, "schema_version");
            std::string kind;
            if (!yyjson_is_obj(root) || !yyjson_is_uint(schema) || yyjson_get_uint(schema) != 1 || !Text(yyjson_obj_get(root, "kind"), kind, 64) ||
                kind != (live ? "sally-live-messages" : "sally-release-metadata"))
                return false;
            DisplayContent model;
            if (live)
            {
                if (!Number(root, "published_at", model.Published, true) || !Number(root, "valid_until", model.Until, true) ||
                    model.Published >= model.Until || model.Until - model.Published > 7 * 86400 || model.Published > now + 300 || now >= model.Until)
                    return false;
            }
            else if (!Text(yyjson_obj_get(root, "release_version"), model.Version, 64) || Supporter::CanonicalVersion(model.Version) != model.Version ||
                     model.Version != Supporter::CanonicalVersion(version) || !Locales(yyjson_obj_get(root, "release_notes"), model.Notes, 64 * 1024))
                return false;
            auto array = yyjson_obj_get(root, "announcements");
            if (!yyjson_is_arr(array) || yyjson_arr_size(array) > 16)
                return false;
            std::set<std::string> ids;
            size_t i, max;
            yyjson_val* value;
            yyjson_arr_foreach(array, i, max, value)
            {
                Notice notice;
                if (!yyjson_is_obj(value) || !Text(yyjson_obj_get(value, "id"), notice.Id, 64) || !Supporter::ValidId(notice.Id, 64) || !ids.insert(notice.Id).second)
                    return false;
                auto target = yyjson_obj_get(value, "target_release");
                if (target && (!Text(target, notice.Target, 64) || (notice.Target != "*" && Supporter::CanonicalVersion(notice.Target) != notice.Target)))
                    return false;
                if (!Number(value, "valid_from", notice.From) || !Number(value, "valid_until", notice.Until) ||
                    (yyjson_obj_get(value, "valid_until") && notice.From >= notice.Until) || !Locales(yyjson_obj_get(value, "locales"), notice.Locales, 16 * 1024))
                    return false;
                model.Notices.push_back(std::move(notice));
            }
            out = std::move(model);
            return true;
        }
        LocalizedContent Select(const ContentLocales& locales, const std::string& tag)
        {
            auto value = locales.find(tag);
            if (value == locales.end())
                value = locales.find("en-US");
            return value == locales.end() ? LocalizedContent{} : value->second;
        }
        bool Applicable(const Notice& notice, const std::string& version, uint64_t now)
        {
            return (notice.Target == "*" || notice.Target == version) && now >= notice.From && (!notice.Until || now < notice.Until);
        }
    } // namespace
    bool ParseReleaseMetadata(std::string_view bytes, std::string_view version, DisplayContent& result) { return Parse(bytes, false, version, 0, result); }
    bool ParseLiveMessages(std::string_view bytes, uint64_t now, DisplayContent& result) { return Parse(bytes, true, {}, now, result); }
    bool VerifyContent(std::string_view bytes, std::string_view signature, const std::vector<Supporter::PublicKey>& keys)
    {
        if (bytes.empty() || bytes.size() > 512 * 1024)
            return false;
        auto doc = Read(signature, 4096);
        if (!doc)
            return false;
        auto root = yyjson_doc_get_root(doc.get());
        std::string algorithm, id, sig;
        auto schema = yyjson_obj_get(root, "schema_version");
        if (!yyjson_is_obj(root) || !yyjson_is_uint(schema) || yyjson_get_uint(schema) != 1 || !Text(yyjson_obj_get(root, "algorithm"), algorithm, 32) || algorithm != "ed25519" ||
            !Text(yyjson_obj_get(root, "key_id"), id, 32) || !Supporter::ValidId(id) || !Text(yyjson_obj_get(root, "signature"), sig, 128))
            return false;
        std::string message("SALLY-DISPLAY-CONTENT\0", 22); // 21 ASCII bytes and NUL
        message.append(bytes.data(), bytes.size());
        return Supporter::VerifySignature(message, sig, id, "content", keys);
    }
    LocalizedContent SelectReleaseNotes(const DisplayContent& model, const std::string& tag, const std::string& github)
    {
        auto selected = Select(model.Notes, tag);
        return selected.Markdown.empty() ? LocalizedContent{"", github} : selected;
    }
    std::vector<LocalizedContent> ResolveNotices(const DisplayContent& release, const DisplayContent* live, const std::string& version, const std::string& tag, uint64_t now)
    {
        std::vector<Notice> merged;
        for (const auto& notice : release.Notices)
            if (Applicable(notice, version, now))
                merged.push_back(notice);
        if (live && now < live->Until && live->Published <= now + 300)
            for (const auto& notice : live->Notices)
            {
                if (!Applicable(notice, version, now))
                    continue;
                auto prior = std::find_if(merged.begin(), merged.end(), [&](const Notice& item)
                                          { return item.Id == notice.Id; });
                if (prior == merged.end())
                    merged.push_back(notice);
                else
                    *prior = notice;
            }
        std::vector<LocalizedContent> result;
        for (const auto& notice : merged)
        {
            auto selected = Select(notice.Locales, tag);
            if (BuildMarkdownDisplay(selected.Markdown).Visible)
                result.push_back(std::move(selected));
        }
        return result;
    }
    bool ValidContentLink(std::string_view url)
    {
        if (url.size() < 9 || url.size() > 4096 || url.substr(0, 8) != "https://")
            return false;
        auto end = url.find_first_of("/?#", 8);
        auto authority = url.substr(8, end == std::string_view::npos ? end : end - 8);
        if (authority.empty() || authority.find('@') != std::string_view::npos || authority.find('%') != std::string_view::npos)
            return false;
        for (unsigned char c : url)
            if (c <= 32 || c == 127 || c == '\\' || c == '"' || c == '<' || c == '>')
                return false;
        return Supporter::ValidUtf8(url);
    }
    MarkdownDisplay BuildMarkdownDisplay(std::string_view markdown)
    {
        MarkdownDisplay result;
        if (markdown.size() > 512 * 1024 || !Supporter::ValidUtf8(markdown))
            return result;
        std::unique_ptr<cmark_node, decltype(&cmark_node_free)> root(cmark_parse_document(markdown.data(), markdown.size(), CMARK_OPT_VALIDATE_UTF8), cmark_node_free);
        if (!root)
            return result;
        auto append = [&](const std::string& text, MarkdownRun run)
        {
            if (result.Truncated)
                return;
            std::wstring wide;
            // RichEdit counts a paragraph break as one UTF-16 code unit. Normalize
            // before recording spans so links and emphasis keep their exact offsets.
            for (wchar_t c : Supporter::Utf8ToUtf16(text))
            {
                if (c == L'\n' && !wide.empty() && wide.back() == L'\r')
                    continue;
                wide += c == L'\n' ? L'\r' : c;
            }
            size_t remaining = 65536 - result.Text.size();
            if (wide.size() > remaining)
            {
                wide.resize(remaining);
                if (!wide.empty() && wide.back() >= 0xd800 && wide.back() <= 0xdbff)
                    wide.pop_back();
                result.Truncated = true;
            }
            run.Start = result.Text.size();
            run.Length = wide.size();
            for (wchar_t c : wide)
                result.Visible |= !(c == L' ' || c == L'\t' || c == L'\n' || c == L'\r' || c == 0xa0 || (c >= 0x2000 && c <= 0x200b));
            result.Text += wide;
            if (run.Length)
                result.Runs.push_back(std::move(run));
        };
        auto walk = [&](auto&& self, cmark_node* node, unsigned depth, MarkdownRun style) -> void
        {
            if (depth > 32 || result.Truncated)
            {
                result.Truncated = true;
                return;
            }
            auto type = cmark_node_get_type(node);
            if (type == CMARK_NODE_HTML_BLOCK || type == CMARK_NODE_HTML_INLINE)
                return;
            if (type == CMARK_NODE_STRONG || type == CMARK_NODE_HEADING)
                style.Bold = true;
            if (type == CMARK_NODE_EMPH)
                style.Italic = true;
            if (type == CMARK_NODE_CODE || type == CMARK_NODE_CODE_BLOCK)
                style.Code = true;
            if (type == CMARK_NODE_LINK)
            {
                const char* url = cmark_node_get_url(node);
                if (url && ValidContentLink(url))
                    style.Link = url;
            }
            if (type == CMARK_NODE_IMAGE)
                style.Link.clear();
            if (type == CMARK_NODE_ITEM)
                append("\xe2\x80\xa2 ", style);
            const char* literal = cmark_node_get_literal(node);
            if (literal && type != CMARK_NODE_CUSTOM_BLOCK && type != CMARK_NODE_CUSTOM_INLINE)
                append(literal, style);
            if (type == CMARK_NODE_SOFTBREAK || type == CMARK_NODE_LINEBREAK)
                append("\r\n", style);
            for (auto child = cmark_node_first_child(node); child; child = cmark_node_next(child))
                self(self, child, depth + 1, style);
            if (type == CMARK_NODE_PARAGRAPH || type == CMARK_NODE_HEADING || type == CMARK_NODE_ITEM || type == CMARK_NODE_CODE_BLOCK)
                append("\r\n", {});
        };
        walk(walk, root.get(), 0, {});
        return result;
    }
    ReadingDisplay BuildReadingDisplay(const MarkdownDisplay& notes, std::wstring_view notesHeading,
                                       const MarkdownDisplay& maintainer, std::wstring_view maintainerHeading)
    {
        ReadingDisplay result;
        auto append = [&](const MarkdownDisplay& source, std::wstring_view heading, size_t& start)
        {
            if (!source.Visible || source.Text.empty())
                return;
            auto& display = result.Content;
            if (!display.Text.empty())
            {
                if (display.Text.back() != L'\r') display.Text += L'\r';
                display.Text += L'\r';
            }
            start = display.Text.size();
            size_t headingSize = (std::min)(heading.size(), size_t{512});
            if (headingSize && heading[headingSize - 1] >= 0xd800 && heading[headingSize - 1] <= 0xdbff)
                --headingSize;
            if (headingSize)
            {
                display.Text.append(heading.data(), headingSize);
                MarkdownRun title;
                title.Start = start; title.Length = headingSize; title.Bold = true;
                display.Runs.push_back(std::move(title));
                display.Text += L'\r';
            }
            const size_t offset = display.Text.size();
            size_t length = (std::min)(source.Text.size(), size_t{65536});
            if (length && source.Text[length - 1] >= 0xd800 && source.Text[length - 1] <= 0xdbff)
                --length;
            display.Text.append(source.Text, 0, length);
            for (const auto& run : source.Runs)
            {
                if (run.Start >= length || !run.Length) continue;
                auto mapped = run;
                mapped.Start += offset;
                mapped.Length = (std::min)(run.Length, length - run.Start);
                display.Runs.push_back(std::move(mapped));
            }
            display.Visible = true;
            display.Truncated |= source.Truncated || headingSize != heading.size() || length != source.Text.size();
        };
        append(notes, notesHeading, result.NotesStart);
        append(maintainer, maintainerHeading, result.MaintainerStart);
        return result;
    }

    size_t RemapReadingPosition(const ReadingDisplay& previous, const ReadingDisplay& current, size_t position)
    {
        position = (std::min)(position, previous.Content.Text.size());
        const bool maintainer = previous.MaintainerStart != std::wstring::npos && position >= previous.MaintainerStart;
        const size_t oldStart = maintainer ? previous.MaintainerStart : previous.NotesStart;
        const size_t newStart = maintainer ? current.MaintainerStart : current.NotesStart;
        if (newStart == std::wstring::npos)
            return maintainer ? current.Content.Text.size() : 0;
        if (oldStart == std::wstring::npos)
            return (std::min)(position, current.Content.Text.size());
        const size_t newEnd = !maintainer && current.MaintainerStart != std::wstring::npos ? current.MaintainerStart : current.Content.Text.size();
        return newStart + (std::min)(position - oldStart, newEnd - newStart);
    }
} // namespace Sally::Update
