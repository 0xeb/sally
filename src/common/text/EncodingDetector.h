// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// EncodingDetector — one answer to "what encoding is this file content?"
//
// Sally had two independent detectors and one consumer with none at all:
//   * the viewer's Sally::Unicode::DetectBom — BOM sniffing only,
//   * filecomp's CTextFileReader — BOM test plus BOM-less UTF-16/UTF-8 heuristics,
//   * Find — nothing, so content search ran over raw bytes and could not see text
//     in any encoding but the active code page.
// This module is that single detector. It is new original work: the
// heuristics are re-derived here around one explicit contract rather than copied
// from either site, and the viewer's public BOM API delegates to it so its own
// suite keeps passing UNMODIFIED as the compatibility proof.
//
// DESIGN RULES
//  * BOM evidence beats heuristics; heuristics never override an explicit BOM.
//  * A heuristic must be able to say "no": failing UTF-8 validation means
//    LegacyBytes, not "probably UTF-8 with errors". Nothing here best-fits.
//  * Detection reads a bounded window. Callers with only the first block of a
//    huge file get the same answer as callers with the whole thing whenever the
//    evidence lies in that window, and Confidence says how firm the answer is.
//  * The encoding vocabulary is shared with the viewer (Sally::Unicode::
//    BomEncoding) rather than duplicated, so there is one enum in the program.

#pragma once

#include <cstddef>
#include <cstdint>

#include "common/unicode/ViewerBomText.h"

namespace sally::text
{

// The vocabulary is the viewer's, deliberately: one enum, no translation layer.
using Encoding = Sally::Unicode::BomEncoding;

enum class Confidence
{
    // An explicit byte-order mark said so. Not a guess.
    FromBom,
    // No BOM, but the bytes validate as this encoding and would be improbable
    // as anything else (e.g. a clean UTF-8 multi-byte sequence run).
    Heuristic,
    // Nothing identified the content; treat the bytes as the legacy code page.
    // This is the honest "I do not know" answer, never a silent guess.
    Fallback,
};

struct DetectionResult
{
    Encoding encoding = Encoding::LegacyBytes;
    Confidence confidence = Confidence::Fallback;
    // Byte offset at which TEXT begins: past the BOM when one was found, 0
    // otherwise. Callers must start decoding here, not at 0.
    std::int64_t textOffset = 0;

    bool IsDecoded() const { return Sally::Unicode::IsDecodedEncoding(encoding); }
};

struct DetectionOptions
{
    // How many bytes of the window the BOM-less scans may examine. Detection is
    // O(budget), so a viewer opening a 4 GB file pays for one block.
    std::size_t scanBudget = 64 * 1024;
    // Allow the BOM-less UTF-16 heuristic. Off for callers that know the content
    // is byte-oriented (hex mode, binary diff) and must not be re-interpreted.
    bool allowUtf16Heuristic = true;
    // Allow the BOM-less UTF-8 validation scan.
    bool allowUtf8Heuristic = true;
    // A window that ends mid-character is normal when reading in blocks: the
    // final incomplete sequence is then not evidence of invalidity.
    bool windowMayEndMidCharacter = true;
};

// Detect the encoding of a content window.
// 'size' may be 0 (yields the Fallback answer).
DetectionResult Detect(const std::uint8_t* data, std::size_t size,
                       const DetectionOptions& options = DetectionOptions());

// The individual judgements, exposed because callers (and tests) need to ask
// them separately — e.g. filecomp offers the user an explicit "treat as UTF-8".
//
// ValidateUtf8: strict. Rejects overlong forms, surrogates encoded as UTF-8,
// values above U+10FFFF, and truncated sequences (unless allowTruncatedTail).
// 'sawMultiByte' reports whether any sequence longer than one byte appeared,
// which is what separates "valid UTF-8" from "plain ASCII" — ASCII is valid
// UTF-8 but is not evidence of it.
bool ValidateUtf8(const std::uint8_t* data, std::size_t size,
                  bool allowTruncatedTail, bool* sawMultiByte = nullptr);

// LooksLikeUtf16: the code-unit plausibility test. Rejects U+0000, U+FFFE and
// U+FFFF, and requires lone surrogates to be absent. 'bigEndian' selects the
// byte order under test.
bool LooksLikeUtf16(const std::uint8_t* data, std::size_t size, bool bigEndian);

} // namespace sally::text
