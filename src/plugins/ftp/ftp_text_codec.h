// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#include <cstddef>
#include <string>
#include <string_view>

enum class CFtpTextDecodeStatus
{
    Success,
    InvalidInput,
    Failure,
};

enum class CFtpTextCompareStatus
{
    Equal,
    NotEqual,
    Failure,
};

enum class CFtpTextEncodeStatus
{
    Success,
    InvalidInput,
    Failure,
};

class CFtpTextCodec
{
public:
    CFtpTextCodec(BOOL utf8Negotiated, UINT legacyCodePage);

    UINT GetCodePage() const;
    BOOL IsValid() const;

    // Conversion is transactional: output is changed only on success.
    BOOL Decode(const char* bytes, size_t length, std::wstring& text) const noexcept;
    // Listing ENTRY NAMES only. A server that never negotiated UTF8 can serve any bytes it
    // likes; refusing them failed the whole parse rule, so one undecodable name made the entire
    // directory unbrowsable ("unknown listing format"). pre-unicode never consulted an encoding
    // here. Never fails; the raw bytes are preserved separately by StoreRawName.
    void DecodeName(const char* bytes, size_t length, std::wstring& text) const noexcept;
    CFtpTextDecodeStatus DecodeForComparison(const char* bytes, size_t length,
                                             std::wstring& text) const noexcept;
    BOOL Encode(const wchar_t* text, size_t length, std::string& bytes) const noexcept;
    CFtpTextEncodeStatus EncodeWithStatus(const wchar_t* text, size_t length,
                                          std::string& bytes) const noexcept;
    // Upload TARGET NAMES only. These name a file the server is about to create, so a
    // best-fit substitution can at worst produce an oddly spelled new file - it cannot
    // address an existing one, unlike RNTO/DELE. pre-unicode had no encode step here at
    // all and enqueued every selected file; refusing one name discarded the whole upload
    // queue silently. Never fails.
    BOOL EncodeUploadName(const wchar_t* text, size_t length, std::string& bytes) const noexcept;

private:
    BOOL Utf8Negotiated;
    UINT LegacyCodePage;
};

// Named constructors for the two non-negotiated byte domains used by FTP.
// Direct GetACP/CP_UTF8 selection stays inside this codec module.
UINT FtpLocalTextCodePage() noexcept;
CFtpTextCodec FtpLocalTextCodec() noexcept;
CFtpTextCodec FtpUtf8TextCodec() noexcept;

// FTP still has local configuration/library surfaces inherited from the
// original ACP plugin. They are distinct from negotiated server bytes and
// therefore use this explicit, exact local-text boundary.
BOOL FtpDecodeLocalText(const char* bytes, std::wstring& text) noexcept;
BOOL FtpDecodeLocalText(std::string_view bytes, std::wstring& text) noexcept;
BOOL FtpEncodeLocalText(const wchar_t* text, std::string& bytes) noexcept;

// Encodes display text for an inherited byte log. If exact ACP encoding is not
// possible, publishes the supplied ASCII/local-byte fallback instead.
BOOL FtpEncodeLocalTextForByteLog(const wchar_t* text, std::string_view fallback,
                                  std::string& bytes) noexcept;

// Maps a byte offset produced by an inherited local-text parser to the UTF-16
// control offset used by the UI. The offset must end on a valid character boundary.
BOOL FtpLocalByteOffsetToUtf16(std::string_view bytes, size_t byteOffset,
                               size_t& utf16Offset) noexcept;

// Keeps only complete lines from an interrupted encoded listing and wipes the
// discarded tail before releasing its logical ownership.
void FtpTrimIncompleteListingBytes(std::string& listing) noexcept;

// Produces the ASCII DNS form used by SOCKS/HTTP and byte-oriented diagnostic adapters.
// Literal ASCII is preserved; non-ASCII labels are converted with Windows IDNA.
BOOL FtpEncodeNetworkHost(const wchar_t* host, std::string& asciiHost) noexcept;

// Encodes semantic proxy credentials at the SOCKS/HTTP adapter using the
// inherited local code page. SOCKS5's one-byte length fields are enforced on
// the resulting byte counts. Both outputs are published together on success.
BOOL FtpEncodeProxyCredentials(const wchar_t* user, const wchar_t* password,
                               BOOL enforceSocks5Limit,
                               std::string& userBytes,
                               std::string& passwordBytes,
                               DWORD& error) noexcept;

// Builds exact byte-domain proxy protocol messages without fixed-capacity
// scratch storage. Output is changed only on success. SOCKS length fields are
// enforced by refusing unrepresentable values rather than truncating them.
BOOL FtpBuildSocks4Request(BYTE request, DWORD hostIP, unsigned short hostPort,
                           std::string_view asciiHost, std::string_view user,
                           BOOL socks4A, std::string& requestBytes) noexcept;
BOOL FtpBuildSocks5Login(std::string_view user, std::string_view password,
                         std::string& requestBytes) noexcept;
BOOL FtpBuildSocks5Request(BYTE request, DWORD hostIP, unsigned short hostPort,
                           std::string_view asciiHost, std::string& requestBytes) noexcept;
BOOL FtpBuildHttpConnectRequest(std::string_view asciiHost, unsigned short hostPort,
                                std::string_view user, std::string_view password,
                                BOOL includeCredentials, std::string& requestBytes) noexcept;

// Stores an exact protocol-byte span without text conversion or fixed-capacity
// truncation. Publication is transactional.
BOOL FtpStoreProtocolBytes(std::string_view bytes, std::string& output) noexcept;

// Stores inherited ACP/local-configuration bytes without mislabelling them as
// negotiated FTP wire data. Publication is transactional.
BOOL FtpStoreLocalTextBytes(std::string_view bytes, std::string& output) noexcept;

// Compares two inherited local-text byte strings after one explicit ACP decode.
BOOL FtpEqualLocalTextNoCase(std::string_view first, std::string_view second) noexcept;
CFtpTextCompareStatus FtpCompareLocalTextNoCase(std::string_view first,
                                                std::string_view second) noexcept;

// Parser string operands are decoded according to their actual owner before
// semantic comparison. These helpers keep character counts distinct from byte
// counts and report conversion/allocation failures instead of treating them as
// inequality.
CFtpTextCompareStatus FtpCompareWideText(std::wstring_view first,
                                         std::wstring_view second,
                                         BOOL ignoreCase) noexcept;
CFtpTextCompareStatus FtpContainsWideTextNoCase(std::wstring_view text,
                                                std::wstring_view sample) noexcept;
CFtpTextCompareStatus FtpEndsWithWideTextNoCase(std::wstring_view text,
                                                std::wstring_view suffix) noexcept;

// Matches a Unicode prefix against explicitly encoded bytes. On success,
// matchedBytes is the exact number of source bytes consumed (which need not
// equal the UTF-16 character count).
CFtpTextCompareStatus FtpMatchEncodedPrefixNoCase(const CFtpTextCodec& codec,
                                                   std::string_view bytes,
                                                   std::wstring_view prefix,
                                                   size_t& matchedBytes) noexcept;
BOOL FtpEncodedCharacterByteLength(const CFtpTextCodec& codec,
                                   std::string_view bytes,
                                   size_t& characterBytes) noexcept;
BOOL FtpAdvanceEncodedCharacters(const CFtpTextCodec& codec,
                                 std::string_view bytes,
                                 size_t characterCount,
                                 size_t& consumedBytes) noexcept;
BOOL FtpRetreatEncodedCharacters(const CFtpTextCodec& codec,
                                 std::string_view bytesBeforeCursor,
                                 size_t characterCount,
                                 size_t& remainingBytes) noexcept;
CFtpTextCompareStatus FtpFindEncodedTextNoCase(const CFtpTextCodec& codec,
                                               std::string_view bytes,
                                               std::wstring_view sample,
                                               size_t& matchOffset,
                                               size_t& matchBytes) noexcept;
BOOL FtpEncodedTextStartsWithAlpha(const CFtpTextCodec& codec,
                                   std::string_view bytes,
                                   BOOL& startsWithAlpha) noexcept;
BOOL FtpEncodedTextStartsWithAlphaNumeric(const CFtpTextCodec& codec,
                                          std::string_view bytes,
                                          BOOL& startsWithAlphaNumeric) noexcept;

// FTP command/proxy grammar is ASCII bytes, not local presentation text.
BOOL FtpEqualAsciiTokenNoCase(std::string_view first,
                              std::string_view second) noexcept;

// Decodes a local-config server-type name and decorates user-defined names
// without imposing a presentation capacity. Publication is transactional.
BOOL FtpFormatLocalTypeName(std::string_view encodedName, std::wstring_view userDefinedSuffix,
                            std::wstring& text) noexcept;

// Serializes the persisted server-type column byte grammar without a fixed
// record capacity. Nullable fields retain the legacy "\\0" representation and
// publication is transactional.
BOOL FtpSerializeServerTypeColumnRecord(BOOL visible, const char* id, int nameID,
                                        const char* name, int descriptionID,
                                        const char* description, int type,
                                        const char* emptyValue, BOOL leftAlignment,
                                        DWORD fixedWidth, int width, BOOL ignoreWidths,
                                        std::string& record) noexcept;

// Stores dynamic semantic UTF-16 without exposing mutable storage ownership.
// Publication is transactional.
BOOL FtpStoreWideText(std::wstring_view text, std::wstring& output) noexcept;

// Stores a bounded reply-text byte span, stopping at an embedded NUL and
// normalizing lone LF to CRLF. Publication is transactional.
BOOL FtpStoreReplyTextBytes(std::string_view bytes, std::string& output) noexcept;

// Normalizes a dynamically owned local error line for byte-oriented logging.
// Publication is transactional and preserves arbitrarily long content.
BOOL FtpNormalizeLogLine(std::string& text) noexcept;

// Normalizes dynamically owned semantic worker-error text without imposing a
// presentation capacity. CR/LF become spaces and trailing spaces/periods are
// removed in place.
void FtpNormalizeWorkerError(std::wstring& text) noexcept;

// Formats one semantic UTF-16 argument into a wide resource format without a
// fixed output buffer. Publication is transactional.
BOOL FtpFormatWideText(const wchar_t* format, std::wstring_view argument,
                       std::wstring& text) noexcept;

// Presentation-only decoder for negotiated server bytes. Invalid input is
// represented explicitly instead of being confused with an allocation error.
BOOL FtpDecodeServerTextForPresentation(const CFtpTextCodec& codec,
                                        std::string_view bytes,
                                        std::wstring& text) noexcept;

// Decodes two negotiated server-byte spans and formats them for a wide UI
// message without fixed storage. Publication is transactional.
BOOL FtpFormatServerReplyMessage(const CFtpTextCodec& codec, const wchar_t* format,
                                 std::string_view subjectBytes,
                                 std::string_view replyBytes,
                                 std::wstring& message) noexcept;

// One encoding decision per FTP session. FTP commands and replies remain byte
// transport; text is decoded/encoded exactly once at this boundary.
class CFtpSessionTextPolicy
{
public:
    explicit CFtpSessionTextPolicy(UINT legacyCodePage);

    void ResetForConnection();
    void ApplyUtf8OptionsReply(int replyCode);

    BOOL IsUtf8Negotiated() const;
    UINT GetLegacyCodePage() const;
    CFtpTextCodec GetCodec() const;

private:
    BOOL Utf8Negotiated;
    UINT LegacyCodePage;
};

size_t FtpUtf8Length(const wchar_t* text, size_t length);
size_t FtpFindLogTrimPrefix(const wchar_t* text, size_t length, size_t maxUtf8Bytes,
                            size_t* skippedLines);

// Wide counterparts of ftputils.h's FTPConvertHexEscapeSequences / FTPAddHexEscapeSequences.
//
// They live beside the local-text boundary rather than in ftputils.cpp because the byte half
// of the job is exactly that boundary and nothing more: percent-encoding encodes BYTES, so a
// run of %XX decodes through FtpDecodeLocalText, while the literal text on either side of it
// is semantic UTF-16 and is copied through untouched.
//
// Reaching the narrow forms meant narrowing the WHOLE path to the ACP first, exactly, which
// refused any path the code page could not spell - ordinary traffic on a server that
// negotiated OPTS UTF8 ON. Both callers of that refusal return FALSE without a message, so
// Copy Path on a directory named in Chinese appeared to do nothing at all.

// Decodes percent-encoded byte runs; literal Unicode text remains UTF-16. Transactional.
BOOL FTPConvertHexEscapeSequencesW(std::wstring& text) noexcept;

// Doubles a '%' that would otherwise read as an escape introducer into "%25". The entire
// escaping alphabet is ASCII, so this consults no code page and cannot refuse. Transactional.
BOOL FTPAddHexEscapeSequencesW(std::wstring& text) noexcept;
