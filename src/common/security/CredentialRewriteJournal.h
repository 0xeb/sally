// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "common/IRegistry.h"
#include <array>
#include <memory>
#include <type_traits>

namespace sally::security
{
    template <class T>
    struct WipingAllocator
    {
        using value_type = T;
        using is_always_equal = std::true_type;
        using propagate_on_container_move_assignment = std::true_type;
        WipingAllocator() noexcept = default;
        template <class U>
        WipingAllocator(const WipingAllocator<U>&) noexcept {}
        T* allocate(size_t count) { return std::allocator<T>{}.allocate(count); }
        void deallocate(T* data, size_t count) noexcept
        {
            SecureZeroMemory(data, count * sizeof(T));
            std::allocator<T>{}.deallocate(data, count);
        }
        template <class U>
        bool operator==(const WipingAllocator<U>&) const noexcept { return true; }
        template <class U>
        bool operator!=(const WipingAllocator<U>&) const noexcept { return false; }
    };
    using CredentialBytes = std::vector<BYTE, WipingAllocator<BYTE>>;
    using CipherFingerprint = std::array<BYTE, 32>;

    struct CredentialAuthority
    {
        bool Protected = false;
        CredentialBytes Verifier;
        bool operator==(const CredentialAuthority& other) const
        {
            return Protected == other.Protected && Verifier == other.Verifier;
        }
    };
    struct CredentialRewrite
    {
        std::vector<CipherFingerprint> Aliases;
        CredentialBytes Before;
        CredentialBytes After;
    };
    struct CredentialJournalDocument
    {
        bool Pending = false;
        CredentialAuthority Before;
        CredentialAuthority After;
        std::vector<CredentialRewrite> Records;
    };
    struct CredentialJournalSnapshot
    {
        bool Present = false;
        CredentialBytes Bytes;
    };

    // Compare and replacement must be serialized against other profile writers.
    // Success guarantees durability before the caller may return new ciphertext.
    // A failed flush may have exposed the complete replacement value; it must
    // never expose a partially serialized document.
    class ICredentialRewriteStore
    {
    public:
        virtual ~ICredentialRewriteStore() = default;
        virtual RegistryResult Read(CredentialJournalSnapshot& snapshot) = 0;
        virtual RegistryResult CompareAndWrite(const CredentialJournalSnapshot& expected,
                                               const CredentialBytes& replacement) = 0;
    };

    RegistryResult EncodeCredentialJournal(const CredentialJournalDocument& document, CredentialBytes& bytes);
    RegistryResult DecodeCredentialJournal(const CredentialBytes& bytes, CredentialJournalDocument& document);

    class CredentialRewriteJournal
    {
        ICredentialRewriteStore& Store;
        CredentialJournalSnapshot Expected;
        CredentialBytes UncertainWrite;
        CredentialJournalDocument Document;
        bool Loaded = false;
        bool ReadAfter = true;
        RegistryResult Persist(CredentialJournalDocument&& candidate, bool readAfter);

    public:
        explicit CredentialRewriteJournal(ICredentialRewriteStore& store) : Store(store) {}
        RegistryResult Load(const CredentialAuthority& legacyAuthority);
        RegistryResult CheckRevision();
        RegistryResult Begin(const CredentialAuthority& target, std::vector<CredentialRewrite> prepared);
        RegistryResult Record(std::vector<CipherFingerprint> aliases,
                              const CredentialBytes& before, const CredentialBytes& after);
        RegistryResult Commit(const CredentialAuthority& authority);
        RegistryResult Abort();
        const CredentialAuthority& Authority() const;
        const CredentialBytes& ReadableCipher(const CredentialRewrite& record) const;
        const CredentialBytes* Resolve(const CipherFingerprint& fingerprint) const;
        const std::vector<CredentialRewrite>& Records() const { return Document.Records; }
        bool Pending() const { return Document.Pending; }
        bool IsLoaded() const { return Loaded; }
    };
} // namespace sally::security
