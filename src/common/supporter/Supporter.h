// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Sally::Supporter
{
    struct PublicKey
    {
        std::string Id;
        std::string Purpose;
        std::array<unsigned char, 32> Bytes{};
    };
    enum class Status
    {
        Missing,
        Invalid,
        NotYetValid,
        Active,
        Expired
    };
    struct Certificate
    {
        Status State = Status::Missing;
        std::string Token, Holder, KeyId, Id, Error;
        uint64_t IssuedAt = 0, ValidFrom = 0, ValidUntil = 0;
        bool Authentic() const { return State == Status::Active || State == Status::Expired || State == Status::NotYetValid; }
    };
    struct Preferences
    {
        std::string Token;
        bool ShowNameInTitle = false;
        bool InvitationsEnabled = true;
        std::string DismissedRelease;
    };
    bool ValidUtf8(std::string_view value, bool holder = false);
    std::wstring Utf8ToUtf16(std::string_view value);
    bool ValidId(std::string_view value, size_t maximum = 32);
    bool DecodeBase64(std::string_view value, std::vector<unsigned char>& bytes);
    std::string EncodeBase64(const unsigned char* bytes, size_t size);
    bool VerifySignature(std::string_view message, std::string_view signature, std::string_view id,
                         std::string_view purpose, const std::vector<PublicKey>& keys);
    Certificate VerifyCertificate(std::string_view input, uint64_t now, const std::vector<PublicKey>& keys, bool file = false);
    std::string CanonicalVersion(std::string_view value);
    bool EvaluateInvitation(Status status, bool newer, std::string_view version, const Preferences& preferences);
    std::string TitleHolder(std::string_view holder);
    const std::vector<PublicKey>& BuiltinPublicKeys();
} // namespace Sally::Supporter
