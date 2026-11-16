// Conversion of remote file names (raw bytes) to and from UTF-16.
#pragma once

#include <windows.h>

#include <string>

// True when the bytes are well-formed UTF-8 (the empty string is valid).
bool IsValidUtf8(const std::string& bytes);

// Decodes names for display and encodes names typed by the user. The raw bytes always stay with the caller and are
// what operations use, so a lossy Decode never loses data.
class CNameCodec
{
public:
    enum class Mode
    {
        Utf8,
        CodePage,
        Auto // valid UTF-8 -> UTF-8, otherwise the ANSI code page; encodes as UTF-8
    };

    CNameCodec() = default;
    static CNameCodec Utf8() { return CNameCodec(Mode::Utf8, CP_UTF8); }
    static CNameCodec CodePage(UINT codePage) { return CNameCodec(Mode::CodePage, codePage); }
    static CNameCodec Auto() { return CNameCodec(Mode::Auto, CP_UTF8); }

    Mode GetMode() const { return mode_; }

    // Invalid sequences become U+FFFD (UTF-8) or the code page's mapping; never fails or crashes.
    std::wstring Decode(const std::string& raw) const;
    // Characters the code page cannot represent become '?'. usedDefaultChar (optional) says whether that happened
    // (always false for UTF-8, which can represent everything except lone surrogates).
    std::string Encode(const std::wstring& text, bool* usedDefaultChar = nullptr) const;

private:
    CNameCodec(Mode mode, UINT codePage) : mode_(mode), codePage_(codePage) {}

    Mode mode_ = Mode::Utf8;
    UINT codePage_ = CP_UTF8;
};
