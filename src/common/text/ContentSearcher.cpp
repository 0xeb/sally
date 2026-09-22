// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// ContentSearcher — see ContentSearcher.h. UI-free by construction: this TU must
// keep compiling without precomp.h so tests can build it directly.

#ifdef SALLY_CONTENT_SEARCHER_STANDALONE
#define NOMINMAX
#include <windows.h>
#else
#include "precomp.h"
#endif

#include "common/text/ContentSearcher.h"

#include <vector>

namespace sally::text
{
namespace
{

// Decode a content window to wide text, keeping a raw byte offset per wide
// character so a match can be reported at the offset Find and the viewer need.
struct DecodedWindow
{
    std::wstring text;
    std::vector<std::int64_t> rawOffsets; // one per wchar_t in 'text'
};

DecodedWindow DecodeUtf16(const std::uint8_t* data, std::size_t size,
                          std::int64_t baseOffset, bool bigEndian)
{
    DecodedWindow out;
    for (std::size_t i = 0; i + 1 < size; i += 2)
    {
        const std::uint16_t unit = bigEndian
                                       ? (std::uint16_t)((data[i] << 8) | data[i + 1])
                                       : (std::uint16_t)((data[i + 1] << 8) | data[i]);
        out.text.push_back((wchar_t)unit);
        out.rawOffsets.push_back(baseOffset + (std::int64_t)i);
    }
    return out;
}

DecodedWindow DecodeUtf8(const std::uint8_t* data, std::size_t size,
                         std::int64_t baseOffset)
{
    DecodedWindow out;
    std::size_t i = 0;
    while (i < size)
    {
        std::size_t length = 1;
        const std::uint8_t lead = data[i];
        if (lead >= 0xF0)
            length = 4;
        else if (lead >= 0xE0)
            length = 3;
        else if (lead >= 0xC0)
            length = 2;
        if (i + length > size)
            break; // incomplete sequence at the window edge

        // MultiByteToWideChar one sequence at a time keeps the offset mapping
        // exact, which a bulk conversion would lose. Sequences are 1-4 bytes,
        // so this is not a hot-loop concern next to the file I/O.
        wchar_t decoded[2] = {};
        const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         (const char*)(data + i), (int)length,
                                         decoded, 2);
        if (n <= 0)
        {
            // Detection samples only the beginning of a file. Preserve access
            // to valid text after a later malformed byte and resynchronize at
            // the next byte, just as a replacement decoder would.
            out.text.push_back(L'\xFFFD');
            out.rawOffsets.push_back(baseOffset + (std::int64_t)i);
            ++i;
            continue;
        }
        for (int k = 0; k < n; k++)
        {
            out.text.push_back(decoded[k]);
            // Both halves of a surrogate pair map to the start of the sequence
            // that produced them: that is the offset a caller can seek to.
            out.rawOffsets.push_back(baseOffset + (std::int64_t)i);
        }
        i += length;
    }
    return out;
}

// Case fold for the legacy BYTE path, through the active code page.
//
// Folding only 'a'-'z' is an ASCII rule, and these are ACP bytes: the engine this
// replaced folded through a LowerCase[] table built with CharLowerA, so a
// case-insensitive search matched accented letters. ASCII-only folding silently
// dropped that - "Ä" stopped matching "ä" on every Western code page.
const unsigned char* AcpUpperTable()
{
    static const unsigned char* table = [] {
        static unsigned char buffer[256];
        for (int i = 0; i < 256; i++)
            buffer[i] = static_cast<unsigned char>(i);
        // Index 0 must stay 0; CharUpperBuffA would stop at the NUL otherwise.
        CharUpperBuffA(reinterpret_cast<LPSTR>(buffer + 1), 255);
        return buffer;
    }();
    return table;
}

// Byte search for the legacy path: straightforward and identical in semantics to
// what the old engine did for representable needles. 'from' lets the caller
// resume past a hit that whole-word filtering rejected.
std::size_t FindBytes(const std::uint8_t* haystack, std::size_t haystackSize,
                      const std::string& needle, bool caseSensitive,
                      bool* outFound, std::size_t from = 0)
{
    *outFound = false;
    if (needle.empty())
    {
        *outFound = true;
        return from <= haystackSize ? from : haystackSize;
    }
    if (needle.size() > haystackSize || from > haystackSize - needle.size())
        return 0;

    const unsigned char* upper = caseSensitive ? nullptr : AcpUpperTable();
    for (std::size_t i = from; i + needle.size() <= haystackSize; i++)
    {
        bool match = true;
        for (std::size_t k = 0; k < needle.size(); k++)
        {
            unsigned char a = haystack[i + k];
            unsigned char b = static_cast<unsigned char>(needle[k]);
            if (upper != nullptr)
            {
                a = upper[a];
                b = upper[b];
            }
            if (a != b)
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            *outFound = true;
            return i;
        }
    }
    return 0;
}

bool WholeWordAt(const std::wstring& text, std::size_t pos, std::size_t length)
{
    if (pos > 0 && IsWordCharacterW(text[pos - 1]))
        return false;
    const std::size_t end = pos + length;
    if (end < text.size() && IsWordCharacterW(text[end]))
        return false;
    return true;
}

} // namespace

bool IsWordCharacterW(wchar_t c)
{
    // '_' counts as a word character, matching the byte-table behaviour the old
    // engine had for the ASCII range. Everything the locale calls alphanumeric
    // counts too, which is the part the 256-entry table could not do: it called
    // every character outside the active code page a NON-word character, so
    // whole-word matching was broken for every non-Latin script.
    if (c == L'_')
        return true;
    return iswalnum((wint_t)c) != 0;
}

bool NeedleIsRepresentableInAnsi(const std::wstring& needle, std::string* ansiOut)
{
    if (needle.empty())
    {
        if (ansiOut != nullptr)
            ansiOut->clear();
        return true;
    }

    BOOL usedDefault = FALSE;
    const int needed = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
                                          needle.c_str(), (int)needle.size(),
                                          nullptr, 0, nullptr, nullptr);
    if (needed <= 0)
        return false;

    std::string narrow((std::size_t)needed, '\0');
    const int written = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
                                           needle.c_str(), (int)needle.size(),
                                           &narrow[0], needed, "?", &usedDefault);
    if (written <= 0 || usedDefault)
        return false;

    // Round-trip to be certain: best-fit is disabled above, but a code page can
    // still map two different characters onto one byte.
    const int back = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS,
                                        narrow.c_str(), written, nullptr, 0);
    if (back <= 0)
        return false;
    std::wstring round((std::size_t)back, L'\0');
    MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, narrow.c_str(), written,
                        &round[0], back);
    if (round != needle)
        return false;

    if (ansiOut != nullptr)
        *ansiOut = narrow;
    return true;
}

SearchResult SearchContent(const std::uint8_t* content, std::size_t contentSize,
                           const std::wstring& needle, const SearchOptions& options)
{
    SearchResult result;
    if (needle.empty())
    {
        result.outcome = SearchOutcome::Found;
        result.rawOffset = 0;
        return result;
    }
    if (content == nullptr || contentSize == 0)
        return result;

    const DetectionResult detected = Detect(content, contentSize, options.detection);
    result.encoding = detected.encoding;

    if (detected.IsDecoded())
    {
        const std::uint8_t* body = content + detected.textOffset;
        const std::size_t bodySize = contentSize - (std::size_t)detected.textOffset;

        DecodedWindow window;
        if (detected.encoding == Encoding::Utf8)
            window = DecodeUtf8(body, bodySize, detected.textOffset);
        else
            window = DecodeUtf16(body, bodySize, detected.textOffset,
                                 detected.encoding == Encoding::Utf16Be);

        const std::wstring haystack = options.caseSensitive
                                          ? window.text
                                          : Fold(window.text, FoldMode::Invariant);
        const std::wstring pattern = options.caseSensitive
                                         ? needle
                                         : Fold(needle, FoldMode::Invariant);

        // Folding is not guaranteed length-preserving. When it changed the length
        // the offset mapping no longer lines up, so fall back to searching the
        // unfolded text case-sensitively rather than report a wrong offset.
        const bool mappingIntact = haystack.size() == window.text.size();
        const std::wstring& searchIn = mappingIntact ? haystack : window.text;
        const std::wstring& searchFor = mappingIntact ? pattern : needle;

        std::size_t from = 0;
        while (true)
        {
            const std::size_t pos = searchIn.find(searchFor, from);
            if (pos == std::wstring::npos)
                break;
            if (!options.wholeWords || WholeWordAt(searchIn, pos, searchFor.size()))
            {
                result.outcome = SearchOutcome::Found;
                result.rawOffset = pos < window.rawOffsets.size()
                                       ? window.rawOffsets[pos]
                                       : detected.textOffset;
                return result;
            }
            from = pos + 1;
        }
        return result; // NotFound in decoded content
    }

    // Legacy byte content: the needle must be representable, or the question has
    // no answer in this encoding.
    std::string needleA;
    if (!NeedleIsRepresentableInAnsi(needle, &needleA))
    {
        result.outcome = SearchOutcome::NoMatchPossible;
        return result;
    }

    // Retry past a rejected hit, exactly as the decoded path above does. Filtering
    // only the FIRST byte match and then giving up meant a file whose first
    // occurrence sat inside a longer word ("foobar") reported no match even when a
    // standalone one ("foo") followed later.
    std::size_t at = 0;
    std::size_t from = 0;
    for (;;)
    {
        bool found = false;
        at = FindBytes(content, contentSize, needleA, options.caseSensitive, &found, from);
        if (!found)
            return result;
        if (!options.wholeWords)
            break;

        // Word boundaries on the byte path, ASCII semantics as before.
        const bool leftOk = at == 0 || !(isalnum((unsigned char)content[at - 1]) ||
                                         content[at - 1] == '_');
        const std::size_t end = at + needleA.size();
        const bool rightOk = end >= contentSize || !(isalnum((unsigned char)content[end]) ||
                                                     content[end] == '_');
        if (leftOk && rightOk)
            break;
        from = at + 1;
    }

    result.outcome = SearchOutcome::Found;
    result.rawOffset = (std::int64_t)at;
    return result;
}

} // namespace sally::text
