// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "Supporter.h"
#include <sodium.h>
#include <algorithm>
#include <cstring>
#include <limits>

namespace Sally::Supporter
{
    namespace
    {
        bool Space(unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
        bool Scalar(std::string_view text, size_t& offset, uint32_t& cp)
        {
            if (offset >= text.size())
                return false;
            unsigned char lead = static_cast<unsigned char>(text[offset++]);
            if (lead < 128)
            {
                cp = lead;
                return true;
            }
            unsigned count = lead >= 0xc2 && lead <= 0xdf ? 1 : lead >= 0xe0 && lead <= 0xef ? 2
                                                            : lead >= 0xf0 && lead <= 0xf4   ? 3
                                                                                             : 0;
            if (!count || offset + count > text.size())
                return false;
            cp = lead & ((1u << (6 - count)) - 1);
            for (unsigned i = 0; i < count; ++i)
            {
                unsigned char next = static_cast<unsigned char>(text[offset++]);
                if ((next & 0xc0) != 0x80)
                    return false;
                cp = (cp << 6) | (next & 63);
            }
            return cp >= (count == 1 ? 0x80u : count == 2 ? 0x800u
                                                          : 0x10000u) &&
                   cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff);
        }
        bool UnicodeSpace(uint32_t cp)
        {
            return cp <= 32 || cp == 0x85 || cp == 0xa0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028 || cp == 0x2029 || cp == 0x202f || cp == 0x205f || cp == 0x3000;
        }
    } // namespace
    bool ValidUtf8(std::string_view text, bool holder)
    {
        size_t pos = 0;
        bool visible = false;
        while (pos < text.size())
        {
            uint32_t cp = 0;
            if (!Scalar(text, pos, cp))
                return false;
            if (holder && (cp < 32 || (cp >= 0x7f && cp <= 0x9f) || cp == 0x61c || cp == 0x200e || cp == 0x200f ||
                           (cp >= 0x202a && cp <= 0x202e) || (cp >= 0x2066 && cp <= 0x2069) || cp == 0x2028 || cp == 0x2029 || cp == 0xfeff))
                return false;
            visible |= !UnicodeSpace(cp);
        }
        return !holder || (visible && text.size() <= 128);
    }
    bool ValidId(std::string_view id, size_t maximum)
    {
        if (id.empty() || id.size() > maximum || id.front() == '-')
            return false;
        return std::all_of(id.begin(), id.end(), [](char c)
                           { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
    }
    std::wstring Utf8ToUtf16(std::string_view value)
    {
        std::wstring result;
        size_t offset = 0;
        while (offset < value.size())
        {
            uint32_t cp = 0;
            if (!Scalar(value, offset, cp))
                return {};
            if (cp <= 0xffff)
                result += static_cast<wchar_t>(cp);
            else
            {
                cp -= 0x10000;
                result += static_cast<wchar_t>(0xd800 + (cp >> 10));
                result += static_cast<wchar_t>(0xdc00 + (cp & 1023));
            }
        }
        return result;
    }
    std::string EncodeBase64(const unsigned char* bytes, size_t size)
    {
        std::string result(sodium_base64_ENCODED_LEN(size, sodium_base64_VARIANT_URLSAFE_NO_PADDING), '\0');
        sodium_bin2base64(result.data(), result.size(), bytes, size, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
        result.resize(std::strlen(result.c_str()));
        return result;
    }
    bool DecodeBase64(std::string_view text, std::vector<unsigned char>& bytes)
    {
        bytes.clear();
        if (text.empty() || text.size() > 2048 || text.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") != std::string_view::npos)
            return false;
        bytes.resize(text.size());
        size_t length = 0;
        if (sodium_base642bin(bytes.data(), bytes.size(), text.data(), text.size(), nullptr, &length, nullptr, sodium_base64_VARIANT_URLSAFE_NO_PADDING))
        {
            bytes.clear();
            return false;
        }
        bytes.resize(length);
        if (EncodeBase64(bytes.data(), bytes.size()) != text)
        {
            bytes.clear();
            return false;
        }
        return true;
    }
    bool VerifySignature(std::string_view message, std::string_view signature, std::string_view id,
                         std::string_view purpose, const std::vector<PublicKey>& keys)
    {
        static const bool ready = sodium_init() >= 0;
        if (!ready)
            return false;
        std::vector<unsigned char> sig;
        if (!DecodeBase64(signature, sig) || sig.size() != crypto_sign_BYTES)
            return false;
        const PublicKey* selected = nullptr;
        for (const auto& key : keys)
            if (key.Id == id)
            {
                if (selected || key.Purpose != purpose)
                    return false;
                selected = &key;
            }
        return selected && crypto_sign_verify_detached(sig.data(), reinterpret_cast<const unsigned char*>(message.data()), message.size(), selected->Bytes.data()) == 0;
    }
    Certificate VerifyCertificate(std::string_view input, uint64_t now, const std::vector<PublicKey>& keys, bool file)
    {
        Certificate result;
        if (input.empty())
            return result;
        auto fail = [&](const char* error)
        { Certificate bad; bad.State = Status::Invalid; bad.Error = error; return bad; };
        if (input.size() > 4096)
            return fail("size");
        if (file)
        {
            if (!input.empty() && input.back() == '\n')
            {
                input.remove_suffix(1);
                if (!input.empty() && input.back() == '\r')
                    input.remove_suffix(1);
            }
        }
        else
        {
            while (!input.empty() && Space(input.front()))
                input.remove_prefix(1);
            while (!input.empty() && Space(input.back()))
                input.remove_suffix(1);
        }
        if (input.size() > 2048 || input.substr(0, 7) != "SALLY1.")
            return fail("envelope");
        size_t split = input.find('.', 7);
        if (split == std::string_view::npos || input.find('.', split + 1) != std::string_view::npos)
            return fail("envelope");
        std::vector<unsigned char> payload;
        if (!DecodeBase64(input.substr(7, split - 7), payload) || payload.size() < 16 || std::memcmp(payload.data(), "SALLY-SUPPORTER\0", 16))
            return fail("payload");
        size_t offset = 16;
        bool ok = true;
        auto integer = [&](size_t n) -> uint64_t
        { if (n > payload.size() - offset) { ok = false; return 0; } uint64_t value = 0; for (size_t i = 0; i < n; ++i) value |= uint64_t(payload[offset++]) << (8 * i); return value; };
        auto field = [&](size_t n) -> std::string
        { if (!ok || n > payload.size() - offset) { ok = false; return {}; } std::string value(reinterpret_cast<const char*>(payload.data() + offset), n); offset += n; return value; };
        if (integer(2) != 1)
            return fail("version");
        result.KeyId = field(static_cast<size_t>(integer(2)));
        std::string certificateId = field(16);
        result.Holder = field(static_cast<size_t>(integer(2)));
        result.IssuedAt = integer(8);
        result.ValidFrom = integer(8);
        result.ValidUntil = integer(8);
        uint64_t flags = integer(4);
        if (!ok || offset != payload.size() || !ValidId(result.KeyId) || certificateId.size() != 16 || !ValidUtf8(result.Holder, true) ||
            flags || result.IssuedAt > result.ValidFrom || result.ValidFrom >= result.ValidUntil || result.ValidUntil > 253402300799ULL)
            return fail("fields");
        std::string_view raw(reinterpret_cast<const char*>(payload.data()), payload.size());
        if (!VerifySignature(raw, input.substr(split + 1), result.KeyId, "supporter", keys))
            return fail("signature");
        static const char hex[] = "0123456789abcdef";
        for (unsigned char c : certificateId)
        {
            result.Id += hex[c >> 4];
            result.Id += hex[c & 15];
        }
        result.Token = std::string(input);
        result.State = now < result.ValidFrom ? Status::NotYetValid : now >= result.ValidUntil ? Status::Expired
                                                                                               : Status::Active;
        return result;
    }
    std::string CanonicalVersion(std::string_view value)
    {
        if (!value.empty() && (value.front() == 'v' || value.front() == 'V'))
            value.remove_prefix(1);
        std::string result;
        size_t pos = 0;
        for (unsigned part = 0; part < 3; ++part)
        {
            if (pos >= value.size() || value[pos] < '0' || value[pos] > '9')
                return {};
            uint64_t n = 0;
            while (pos < value.size() && value[pos] >= '0' && value[pos] <= '9')
            {
                n = n * 10 + value[pos++] - '0';
                if (n > UINT32_MAX)
                    return {};
            }
            if (part)
                result += '.';
            result += std::to_string(n);
            if (part < 2 && (pos >= value.size() || value[pos++] != '.'))
                return {};
        }
        return pos == value.size() ? result : std::string();
    }
    bool EvaluateInvitation(Status status, bool newer, std::string_view version, const Preferences& preferences)
    {
        std::string normalized = CanonicalVersion(version);
        return newer && !normalized.empty() && preferences.InvitationsEnabled &&
               status != Status::Active && status != Status::NotYetValid && CanonicalVersion(preferences.DismissedRelease) != normalized;
    }
    std::string TitleHolder(std::string_view holder)
    {
        size_t offset = 0, limit = 0;
        unsigned count = 0;
        while (offset < holder.size())
        {
            uint32_t cp;
            if (!Scalar(holder, offset, cp))
                return {};
            if (++count <= 39)
                limit = offset;
        }
        return count <= 40 ? std::string(holder) : std::string(holder.substr(0, limit)) + "\xe2\x80\xa6";
    }
} // namespace Sally::Supporter
