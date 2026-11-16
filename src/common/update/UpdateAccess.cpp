// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#include "update/UpdateAccess.h"
#include <atomic>
#include <utility>

namespace Sally::Update
{
UpdateAccess EvaluateUpdateAccess(const Supporter::Certificate& certificate, uint64_t now, bool updateAvailable, bool canInstall)
{
    using Supporter::Status;
    if (!certificate.Authentic())
        return {certificate.State == Status::Missing ? UpdateAccessReason::NoActiveSupport : UpdateAccessReason::InvalidSupport};
    if (now < certificate.ValidFrom) return {UpdateAccessReason::FutureSupport};
    if (now >= certificate.ValidUntil) return {UpdateAccessReason::ExpiredSupport};
    return {updateAvailable && canInstall ? UpdateAccessReason::Available : UpdateAccessReason::ReleaseNotInstallable};
}

namespace { std::atomic<uint64_t> NextAttempt{0}; }
ManagedUpdateAttempt::ManagedUpdateAttempt(const ReleaseCheckResult& release, std::wstring profile, std::wstring installRoot, std::wstring arch)
    : Id(++NextAttempt), Release(release), Profile(std::move(profile)), InstallRoot(std::move(installRoot)), Arch(std::move(arch)) {}

std::shared_ptr<ManagedUpdateAttempt> ManagedUpdateAttempt::Accept(const std::string& token, uint64_t now,
    const std::vector<Supporter::PublicKey>& keys, const ReleaseCheckResult& release,
    std::wstring profile, std::wstring installRoot, std::wstring arch)
{
    const auto certificate = Supporter::VerifyCertificate(token, now, keys);
    if (!EvaluateUpdateAccess(certificate, now, release.UpdateAvailable, release.CanInstall).Allowed()) return {};
    return std::shared_ptr<ManagedUpdateAttempt>(new ManagedUpdateAttempt(release, std::move(profile), std::move(installRoot), std::move(arch)));
}

std::shared_ptr<ManagedUpdateAttempt> ManagedUpdateAttempt::AcceptForProfile(IRegistry& registry, const std::wstring& profile,
    uint64_t now, const std::vector<Supporter::PublicKey>& keys, const ReleaseCheckResult& release,
    std::wstring installRoot, std::wstring arch)
{
    return Accept(Supporter::ReadSupporterState(profile, registry).Token, now, keys, release, profile, std::move(installRoot), std::move(arch));
}

bool ManagedUpdateAttempt::Run(const std::function<ManagedUpdateResult(const ManagedUpdateAttempt&)>& work)
{
    {
        std::lock_guard<std::mutex> lock(Mutex);
        if (Current != State::Accepted) return false;
        Current = State::Running;
    }
    ManagedUpdateResult result;
    try { result = work(*this); }
    catch (...) { result.Prepared = false; }
    std::lock_guard<std::mutex> lock(Mutex);
    if (Current != State::Cancelled) { Result = std::move(result); Current = State::Finished; }
    return true;
}
bool ManagedUpdateAttempt::TakeCompletion(uint64_t id, ManagedUpdateResult& result)
{
    std::lock_guard<std::mutex> lock(Mutex);
    if (id != Id || Current != State::Finished) return false;
    result = Result; Current = State::Consumed; return true;
}
void ManagedUpdateAttempt::Cancel()
{
    std::lock_guard<std::mutex> lock(Mutex);
    Current = State::Cancelled;
}
bool RunManagedUpdate(const std::shared_ptr<ManagedUpdateAttempt>& attempt,
    const std::function<ManagedUpdateResult(const ManagedUpdateAttempt&)>& work)
{
    return attempt && attempt->Run(work);
}
} // namespace Sally::Update
