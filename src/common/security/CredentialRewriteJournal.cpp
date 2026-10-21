// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/security/CredentialRewriteJournal.h"
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace sally::security
{
    namespace
    {
        constexpr DWORD JournalMagic = 0x314A4353; // SCJ1, little endian
        constexpr DWORD JournalVersion = 1;
        constexpr size_t MaxValueBytes = (std::numeric_limits<DWORD>::max)();
        bool ValidAuthority(const CredentialAuthority& authority)
        {
            return authority.Protected == !authority.Verifier.empty();
        }
        bool ValidShape(const CredentialJournalDocument& document)
        {
            if (!ValidAuthority(document.Before) || !ValidAuthority(document.After))
                return false;
            if (!document.Pending && (document.Before.Protected || !document.Before.Verifier.empty()))
                return false;
            std::set<CipherFingerprint> aliases;
            for (const auto& record : document.Records)
            {
                if (record.Aliases.empty() || record.After.empty() ||
                    (document.Pending ? record.Before.empty() : !record.Before.empty()))
                    return false;
                for (const auto& alias : record.Aliases)
                    if (!aliases.insert(alias).second)
                        return false;
            }
            return true;
        }
        class Writer
        {
        public:
            CredentialBytes Bytes;
            bool Raw(const BYTE* data, size_t size)
            {
                if (size > MaxValueBytes - Bytes.size())
                    return false;
                if (size != 0)
                    Bytes.insert(Bytes.end(), data, data + size);
                return true;
            }
            bool U32(size_t value)
            {
                if (value > MaxValueBytes)
                    return false;
                BYTE bytes[4];
                for (unsigned i = 0; i < 4; ++i)
                    bytes[i] = static_cast<BYTE>(value >> (8 * i));
                return Raw(bytes, sizeof(bytes));
            }
            bool Blob(const CredentialBytes& value) { return U32(value.size()) && Raw(value.data(), value.size()); }
            bool Auth(const CredentialAuthority& authority) { return U32(authority.Protected ? 1 : 0) && Blob(authority.Verifier); }
        };
        class Reader
        {
            const CredentialBytes& Bytes;
            size_t Position = 0;

        public:
            explicit Reader(const CredentialBytes& bytes) : Bytes(bytes) {}
            size_t Remaining() const { return Bytes.size() - Position; }
            bool U32(DWORD& value)
            {
                if (Remaining() < 4)
                    return false;
                value = 0;
                for (unsigned i = 0; i < 4; ++i)
                    value |= static_cast<DWORD>(Bytes[Position++]) << (8 * i);
                return true;
            }
            bool Blob(CredentialBytes& value)
            {
                DWORD size;
                if (!U32(size) || size > Remaining())
                    return false;
                value.assign(Bytes.begin() + Position, Bytes.begin() + Position + size);
                Position += size;
                return true;
            }
            bool Auth(CredentialAuthority& authority)
            {
                DWORD enabled;
                if (!U32(enabled) || enabled > 1 || !Blob(authority.Verifier))
                    return false;
                authority.Protected = enabled != 0;
                return ValidAuthority(authority);
            }
            bool Digest(CipherFingerprint& fingerprint)
            {
                if (Remaining() < fingerprint.size())
                    return false;
                std::copy_n(Bytes.begin() + Position, fingerprint.size(), fingerprint.begin());
                Position += fingerprint.size();
                return true;
            }
        };
    } // namespace

    RegistryResult EncodeCredentialJournal(const CredentialJournalDocument& document, CredentialBytes& bytes)
    {
        try
        {
            if (!ValidShape(document))
                return RegistryResult::Error(ERROR_INVALID_DATA);
            Writer writer;
            if (!writer.U32(JournalMagic) || !writer.U32(JournalVersion) || !writer.U32(document.Pending ? 1 : 0) ||
                !writer.Auth(document.Before) || !writer.Auth(document.After) || !writer.U32(document.Records.size()))
                return RegistryResult::Error(ERROR_FILE_TOO_LARGE);
            for (const auto& record : document.Records)
            {
                if (!writer.U32(record.Aliases.size()))
                    return RegistryResult::Error(ERROR_FILE_TOO_LARGE);
                for (const auto& alias : record.Aliases)
                    if (!writer.Raw(alias.data(), alias.size()))
                        return RegistryResult::Error(ERROR_FILE_TOO_LARGE);
                if (!writer.Blob(record.Before) || !writer.Blob(record.After))
                    return RegistryResult::Error(ERROR_FILE_TOO_LARGE);
            }
            bytes.swap(writer.Bytes);
            return RegistryResult::Ok();
        }
        catch (const std::bad_alloc&)
        {
            return RegistryResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        }
        catch (const std::length_error&)
        {
            return RegistryResult::Error(ERROR_FILE_TOO_LARGE);
        }
    }

    RegistryResult DecodeCredentialJournal(const CredentialBytes& bytes, CredentialJournalDocument& document)
    {
        try
        {
            Reader reader(bytes);
            CredentialJournalDocument staged;
            DWORD magic, version, pending, count;
            if (!reader.U32(magic) || magic != JournalMagic || !reader.U32(version) || version != JournalVersion ||
                !reader.U32(pending) || pending > 1 || !reader.Auth(staged.Before) || !reader.Auth(staged.After) ||
                !reader.U32(count) || count > reader.Remaining() / 45)
                return RegistryResult::Error(ERROR_INVALID_DATA);
            staged.Pending = pending != 0;
            staged.Records.resize(count);
            for (auto& record : staged.Records)
            {
                DWORD aliasCount;
                if (!reader.U32(aliasCount) || aliasCount == 0 || aliasCount > reader.Remaining() / 32)
                    return RegistryResult::Error(ERROR_INVALID_DATA);
                record.Aliases.resize(aliasCount);
                for (auto& alias : record.Aliases)
                    if (!reader.Digest(alias))
                        return RegistryResult::Error(ERROR_INVALID_DATA);
                if (!reader.Blob(record.Before) || !reader.Blob(record.After))
                    return RegistryResult::Error(ERROR_INVALID_DATA);
            }
            if (reader.Remaining() != 0 || !ValidShape(staged))
                return RegistryResult::Error(ERROR_INVALID_DATA);
            document = std::move(staged);
            return RegistryResult::Ok();
        }
        catch (const std::bad_alloc&)
        {
            return RegistryResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        }
        catch (const std::length_error&)
        {
            return RegistryResult::Error(ERROR_INVALID_DATA);
        }
    }

    RegistryResult CredentialRewriteJournal::Load(const CredentialAuthority& legacyAuthority)
    {
        try
        {
            Loaded = false;
            CredentialJournalSnapshot snapshot;
            const auto read = Store.Read(snapshot);
            if (!read.success)
                return read;
            CredentialJournalDocument staged;
            if (snapshot.Present)
            {
                const auto decoded = DecodeCredentialJournal(snapshot.Bytes, staged);
                if (!decoded.success)
                    return decoded;
            }
            else
            {
                if (!snapshot.Bytes.empty() || !ValidAuthority(legacyAuthority))
                    return RegistryResult::Error(ERROR_INVALID_DATA);
                staged.After = legacyAuthority;
            }
            Expected = std::move(snapshot);
            Document = std::move(staged);
            UncertainWrite.clear();
            ReadAfter = !Document.Pending;
            Loaded = true;
            return RegistryResult::Ok();
        }
        catch (const std::bad_alloc&)
        {
            return RegistryResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        }
        catch (const std::length_error&)
        {
            return RegistryResult::Error(ERROR_FILE_TOO_LARGE);
        }
    }

    RegistryResult CredentialRewriteJournal::CheckRevision()
    {
        try
        {
            if (!Loaded)
                return RegistryResult::Error(ERROR_INVALID_STATE);
            CredentialJournalSnapshot observed;
            const auto read = Store.Read(observed);
            if (!read.success)
                return read;
            if (observed.Present == Expected.Present && observed.Bytes == Expected.Bytes)
                return RegistryResult::Ok();
            // A failed flush can have published our complete value. Accept only that
            // exact attempted value; never adopt a different writer's revision.
            if (observed.Present && !UncertainWrite.empty() && observed.Bytes == UncertainWrite)
            {
                Expected = std::move(observed);
                UncertainWrite.clear();
                return RegistryResult::Ok();
            }
            return RegistryResult::Error(ERROR_REVISION_MISMATCH);
        }
        catch (const std::bad_alloc&)
        {
            return RegistryResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        }
        catch (const std::length_error&)
        {
            return RegistryResult::Error(ERROR_FILE_TOO_LARGE);
        }
    }

    RegistryResult CredentialRewriteJournal::Persist(CredentialJournalDocument&& candidate, bool readAfter)
    {
        CredentialBytes payload;
        const auto encoded = EncodeCredentialJournal(candidate, payload);
        if (!encoded.success)
            return encoded;
        const auto current = CheckRevision();
        if (!current.success)
            return current;
        const auto written = Store.CompareAndWrite(Expected, payload);
        if (!written.success)
        {
            UncertainWrite = std::move(payload);
            return written;
        }
        Expected.Present = true;
        Expected.Bytes = std::move(payload);
        UncertainWrite.clear();
        Document = std::move(candidate);
        ReadAfter = readAfter;
        return RegistryResult::Ok();
    }

    const CredentialAuthority& CredentialRewriteJournal::Authority() const
    {
        return Document.Pending && !ReadAfter ? Document.Before : Document.After;
    }
    const CredentialBytes& CredentialRewriteJournal::ReadableCipher(const CredentialRewrite& record) const
    {
        return Document.Pending && !ReadAfter ? record.Before : record.After;
    }
    const CredentialBytes* CredentialRewriteJournal::Resolve(const CipherFingerprint& fingerprint) const
    {
        for (const auto& record : Document.Records)
            if (std::find(record.Aliases.begin(), record.Aliases.end(), fingerprint) != record.Aliases.end())
                return &ReadableCipher(record);
        return nullptr;
    }

    RegistryResult CredentialRewriteJournal::Begin(const CredentialAuthority& target, std::vector<CredentialRewrite> prepared)
    {
        try
        {
            if (!Loaded)
                return RegistryResult::Error(ERROR_INVALID_STATE);
            // Every existing alias must survive the next generation. The caller
            // re-encrypts canonical ciphertext before entering this transition.
            for (const auto& previous : Document.Records)
            {
                for (const auto& alias : previous.Aliases)
                {
                    bool retained = false;
                    for (const auto& next : prepared)
                        if (std::find(next.Aliases.begin(), next.Aliases.end(), alias) != next.Aliases.end() &&
                            next.Before == ReadableCipher(previous))
                            retained = true;
                    if (!retained)
                        return RegistryResult::Error(ERROR_INVALID_DATA);
                }
            }
            CredentialJournalDocument staged;
            staged.Pending = true;
            staged.Before = Authority();
            staged.After = target;
            staged.Records = std::move(prepared);
            return Persist(std::move(staged), true);
        }
        catch (const std::bad_alloc&)
        {
            return RegistryResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        }
        catch (const std::length_error&)
        {
            return RegistryResult::Error(ERROR_FILE_TOO_LARGE);
        }
    }

    RegistryResult CredentialRewriteJournal::Record(std::vector<CipherFingerprint> aliases,
                                                    const CredentialBytes& before, const CredentialBytes& after)
    {
        try
        {
            if (!Loaded || !Document.Pending || !ReadAfter || aliases.empty())
                return RegistryResult::Error(ERROR_INVALID_STATE);
            CredentialJournalDocument staged = Document;
            CredentialRewrite combined;
            combined.Aliases = aliases;
            combined.Before = before;
            combined.After = after;
            bool inheritedBefore = false;
            auto record = staged.Records.begin();
            while (record != staged.Records.end())
            {
                bool matches = false;
                for (const auto& alias : aliases)
                    matches = matches || std::find(record->Aliases.begin(), record->Aliases.end(), alias) != record->Aliases.end();
                if (!matches)
                {
                    ++record;
                    continue;
                }
                if (!inheritedBefore)
                {
                    combined.Before = record->Before;
                    inheritedBefore = true;
                }
                combined.Aliases.insert(combined.Aliases.end(), record->Aliases.begin(), record->Aliases.end());
                record = staged.Records.erase(record);
            }
            std::sort(combined.Aliases.begin(), combined.Aliases.end());
            combined.Aliases.erase(std::unique(combined.Aliases.begin(), combined.Aliases.end()), combined.Aliases.end());
            staged.Records.push_back(std::move(combined));
            return Persist(std::move(staged), true);
        }
        catch (const std::bad_alloc&)
        {
            return RegistryResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        }
        catch (const std::length_error&)
        {
            return RegistryResult::Error(ERROR_FILE_TOO_LARGE);
        }
    }

    RegistryResult CredentialRewriteJournal::Commit(const CredentialAuthority& authority)
    {
        try
        {
            if (!Loaded || !ValidAuthority(authority) || authority.Protected != Authority().Protected)
                return RegistryResult::Error(ERROR_INVALID_STATE);
            CredentialJournalDocument staged;
            staged.After = authority; // may upgrade the verifier for the same key
            for (const auto& record : Document.Records)
            {
                CredentialRewrite current;
                current.Aliases = record.Aliases;
                current.After = ReadableCipher(record);
                staged.Records.push_back(std::move(current));
            }
            return Persist(std::move(staged), true);
        }
        catch (const std::bad_alloc&)
        {
            return RegistryResult::Error(ERROR_NOT_ENOUGH_MEMORY);
        }
        catch (const std::length_error&)
        {
            return RegistryResult::Error(ERROR_FILE_TOO_LARGE);
        }
    }
    RegistryResult CredentialRewriteJournal::Abort()
    {
        if (!Loaded)
            return RegistryResult::Error(ERROR_INVALID_STATE);
        if (!Document.Pending)
            return RegistryResult::Ok();
        ReadAfter = false;
        return Commit(Authority());
    }
} // namespace sally::security
