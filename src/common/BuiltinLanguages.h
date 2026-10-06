// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The languages compiled into sally.exe and every plugin DLL (each one under its own
// LANGUAGE in the module's resources), and how Sally picks one of them.
//
// A language is identified three ways:
// - its LANGID, the LANGUAGE of its resources (what EnumResourceLanguages reports);
// - its BCP-47 tag, what SetProcessPreferredUILanguages takes to make the Win32 resource
//   loader pick that copy of every string, dialog and menu;
// - its persisted name, what the registry value Configuration\Language stores, e.g.
//   "czech.slg". The name is only an identifier now: there is no such file. It keeps the
//   form older Sally versions used for their language files, so one configuration works with
//   both (an older Sally reading the value still finds its czech.slg).
//
// No UI and no Win32 calls here: callers enumerate the built-in languages and query the user's
// preferred UI languages, and this header decides.

#include <windows.h>

#include <cstddef>
#include <string>
#include <vector>

namespace sally::languages
{

struct BuiltinLanguage
{
    LANGID LangId;
    const wchar_t* Tag;           // BCP-47, e.g. L"cs-CZ"
    const wchar_t* PersistedName; // Configuration\Language value, e.g. L"czech.slg"
};

// English first: it is the fallback for everything.
inline constexpr BuiltinLanguage kBuiltinLanguages[] = {
    {0x0409, L"en-US", L"english.slg"},
    {0x0405, L"cs-CZ", L"czech.slg"},
    {0x041B, L"sk-SK", L"slovak.slg"},
    {0x0407, L"de-DE", L"german.slg"},
    {0x040C, L"fr-FR", L"french.slg"},
    {0x0C0A, L"es-ES", L"spanish.slg"},
    {0x0413, L"nl-NL", L"dutch.slg"},
    {0x040E, L"hu-HU", L"hungarian.slg"},
    {0x0418, L"ro-RO", L"romanian.slg"},
    {0x0419, L"ru-RU", L"russian.slg"},
    {0x0804, L"zh-CN", L"chinesesimplified.slg"},
};

inline constexpr LANGID kEnglishLangId = 0x0409;
inline constexpr const wchar_t* kEnglishTag = L"en-US";

namespace detail
{
inline wchar_t AsciiLower(wchar_t c)
{
    return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
}

inline bool EqualsNoCase(const wchar_t* a, const wchar_t* b)
{
    if (a == nullptr || b == nullptr)
        return false;
    for (; *a != 0 && *b != 0; ++a, ++b)
    {
        if (AsciiLower(*a) != AsciiLower(*b))
            return false;
    }
    return *a == *b;
}

// "de-AT" -> "de"; "zh-Hant-TW" -> "zh"
inline std::wstring PrimarySubtag(const std::wstring& tag)
{
    std::wstring primary;
    for (wchar_t c : tag)
    {
        if (c == L'-' || c == L'_')
            break;
        primary.push_back(AsciiLower(c));
    }
    return primary;
}

// Simplified and Traditional Chinese share the primary subtag "zh" but are not
// interchangeable: only a Simplified variant (zh-CN, zh-SG, zh-Hans-*) falls back to zh-CN.
// A Traditional variant (zh-TW, zh-HK, zh-MO, zh-Hant-*) does not match any built-in language.
inline bool IsTraditionalChinese(const std::wstring& tag)
{
    std::wstring lower;
    for (wchar_t c : tag)
        lower.push_back(c == L'_' ? L'-' : AsciiLower(c));
    if (lower.compare(0, 3, L"zh-") != 0)
        return false;
    if (lower.find(L"-hant") != std::wstring::npos)
        return true;
    if (lower.find(L"-hans") != std::wstring::npos)
        return false;
    const std::wstring region = lower.substr(3);
    return region == L"tw" || region == L"hk" || region == L"mo";
}
} // namespace detail

inline const BuiltinLanguage* FindByLangId(LANGID langId)
{
    for (const BuiltinLanguage& language : kBuiltinLanguages)
    {
        if (language.LangId == langId)
            return &language;
    }
    return nullptr;
}

// Case-insensitive: "Czech.SLG" is czech. Returns 0 for an unknown or empty name.
inline LANGID LangIdFromPersistedName(const wchar_t* persistedName)
{
    for (const BuiltinLanguage& language : kBuiltinLanguages)
    {
        if (detail::EqualsNoCase(language.PersistedName, persistedName))
            return language.LangId;
    }
    return 0;
}

// Case-insensitive. Returns 0 for a tag that is not exactly one of the built-in languages.
inline LANGID LangIdFromTag(const wchar_t* tag)
{
    for (const BuiltinLanguage& language : kBuiltinLanguages)
    {
        if (detail::EqualsNoCase(language.Tag, tag))
            return language.LangId;
    }
    return 0;
}

// NULL for a LANGID that is not a built-in language.
inline const wchar_t* PersistedNameFromLangId(LANGID langId)
{
    const BuiltinLanguage* language = FindByLangId(langId);
    return language != nullptr ? language->PersistedName : nullptr;
}

// NULL for a LANGID that is not a built-in language.
inline const wchar_t* TagFromLangId(LANGID langId)
{
    const BuiltinLanguage* language = FindByLangId(langId);
    return language != nullptr ? language->Tag : nullptr;
}

inline bool Contains(const std::vector<LANGID>& available, LANGID langId)
{
    for (LANGID id : available)
    {
        if (id == langId)
            return true;
    }
    return false;
}

// Picks the language Sally starts in when the configuration names none.
// 'available' are the built-in languages the module really carries; 'userPreferredTags' are the
// user's preferred UI languages, most preferred first (GetUserPreferredUILanguages).
// The first user language Sally can show wins: for each tag in order, an exact tag match, then a
// built-in language with the same primary language (de-AT -> de-DE, en-GB -> en-US). Traditional
// Chinese does not fall back to Simplified. If no user language matches, the result is en-US.
// '*exact' (optional) is true when the result is one of the user's languages and false for the
// en-US fallback, which is when Sally lets the user choose.
inline LANGID ChooseDefault(const std::vector<LANGID>& available,
                            const std::vector<std::wstring>& userPreferredTags, bool* exact)
{
    if (exact != nullptr)
        *exact = false;
    for (const std::wstring& userTag : userPreferredTags)
    {
        if (userTag.empty())
            continue;
        const LANGID exactId = LangIdFromTag(userTag.c_str());
        if (exactId != 0 && Contains(available, exactId))
        {
            if (exact != nullptr)
                *exact = true;
            return exactId;
        }
        if (detail::IsTraditionalChinese(userTag))
            continue;
        const std::wstring primary = detail::PrimarySubtag(userTag);
        for (const BuiltinLanguage& language : kBuiltinLanguages)
        {
            if (Contains(available, language.LangId) &&
                detail::PrimarySubtag(language.Tag) == primary)
            {
                if (exact != nullptr)
                    *exact = true;
                return language.LangId;
            }
        }
    }
    return kEnglishLangId;
}

// The MUI_LANGUAGE_NAME multi-string for SetProcessPreferredUILanguages: the language's tag, then
// en-US so a resource missing from a translation falls back to English ("cs-CZ\0en-US\0\0").
// 'count' receives the number of languages in the list. An unknown LANGID gives en-US alone.
inline std::wstring PreferredUILanguagesList(LANGID langId, ULONG* count)
{
    const wchar_t* tag = TagFromLangId(langId);
    std::wstring list;
    ULONG languages = 0;
    if (tag != nullptr && langId != kEnglishLangId)
    {
        list.append(tag);
        list.push_back(L'\0');
        ++languages;
    }
    list.append(kEnglishTag);
    list.push_back(L'\0');
    ++languages;
    list.push_back(L'\0');
    if (count != nullptr)
        *count = languages;
    return list;
}

// Splits a MUI_LANGUAGE_NAME multi-string (GetUserPreferredUILanguages) into tags.
inline std::vector<std::wstring> SplitMultiString(const wchar_t* multiString, std::size_t length)
{
    std::vector<std::wstring> tags;
    if (multiString == nullptr)
        return tags;
    std::size_t start = 0;
    for (std::size_t i = 0; i < length; ++i)
    {
        if (multiString[i] == L'\0')
        {
            if (i == start)
                break; // the terminating empty string
            tags.emplace_back(multiString + start, i - start);
            start = i + 1;
        }
    }
    return tags;
}

} // namespace sally::languages
