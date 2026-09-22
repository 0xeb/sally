// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Utf16RegexBridge — re-encodes a UTF-16 content window to UTF-8
// so Find's byte-oriented regular-expression engine (the RegExpUtf8 twin
// already used for UTF-8-native content) can run over it, with a raw-byte
// offset table so a match position can be mapped back to where it lives in
// the actual UTF-16 file bytes.
//
// WHY THIS EXISTS: the regex engine's CR/LF line splitter assumes
// ASCII-transparent bytes. That is true for UTF-8 (already handled) but false
// for UTF-16, whose bytes are not a superset of ASCII — a stray 0x0A/0x0D low
// byte inside an unrelated code unit would look like a line break. Re-encoding
// to UTF-8 first makes the EXISTING line splitter and byte engine correct
// again, exactly as they already are for UTF-8-native content; only the
// content needs transforming, not the matching logic.
//
// SEH NOTE: find.cpp's TestFileContentAux runs inside __try/__except, which
// under /EHsc cannot host a local variable with a non-trivial destructor
// (MSVC C2712). DecodeUtf16ToUtf8 therefore writes into caller-owned
// std::string/std::vector OUTPUT PARAMETERS rather than returning by value,
// so callers can hold the storage on a long-lived, pointer-reached object
// (like CGrepData) and only ever touch it through the pointer inside the SEH
// scope — the same pattern CGrepData's own GrepText/RegExpUtf8 members
// already rely on.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sally::text
{

// Decodes as much of [data, data+size) as is complete UTF-16 code units (and,
// for surrogate pairs, both halves) into UTF-8, APPENDING to 'outUtf8'.
// 'outOffsets' grows in lockstep: outOffsets[i] is the offset (relative to
// 'data', i.e. 0-based within this window) of the UTF-16 code unit that
// produced outUtf8[i]. Every UTF-8 output byte for a surrogate pair maps back
// to the offset where the pair STARTS (the high surrogate) — the same
// per-source-unit convention ContentSearcher's own UTF-16 decoder uses.
//
// Returns the number of RAW bytes actually consumed from 'data' (a multiple
// of 2, and possibly less than 'size' when a trailing code unit doesn't fit —
// a lone odd byte, or a high surrogate with no low surrogate yet in this
// window). Callers that still have more file content should resume their next
// window at that many bytes past 'data': nothing before it needs to be seen
// again, and nothing after it was included in 'outUtf8'.
//
// A malformed sequence (a lone low surrogate, or a high surrogate followed by
// a non-low-surrogate) decodes as U+FFFD (one 3-byte UTF-8 sequence) rather
// than aborting the whole window — Find's job is to keep searching the rest
// of a mostly-valid file, not to validate it.
std::size_t DecodeUtf16ToUtf8(const std::uint8_t* data, std::size_t size, bool bigEndian,
                              std::string& outUtf8, std::vector<std::int64_t>& outOffsets);

} // namespace sally::text
