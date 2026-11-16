// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "supporter/Supporter.h"
#include "supporter/SupporterState.h"
#include "update/GitHubRelease.h"
#include <functional>
#include <memory>
#include <mutex>

namespace Sally::Update
{
enum class UpdateAccessReason { Available, NoActiveSupport, InvalidSupport, FutureSupport, ExpiredSupport, ReleaseNotInstallable };
struct UpdateAccess
{
    UpdateAccessReason Reason = UpdateAccessReason::NoActiveSupport;
    bool Allowed() const { return Reason == UpdateAccessReason::Available; }
};
UpdateAccess EvaluateUpdateAccess(const Supporter::Certificate& verified, uint64_t now, bool updateAvailable, bool canInstall);

struct ManagedUpdateResult
{
    bool Prepared = false;
    std::wstring Error;
};

// Process-local, single-use admission. It is never serialized or accepted from a command line.
// The worker owns its release snapshot; a window message cannot manufacture successful work.
class ManagedUpdateAttempt
{
public:
    static std::shared_ptr<ManagedUpdateAttempt> AcceptForProfile(IRegistry& registry, const std::wstring& profile,
        uint64_t now, const std::vector<Supporter::PublicKey>& keys, const ReleaseCheckResult& release,
        std::wstring installRoot, std::wstring arch);
    static std::shared_ptr<ManagedUpdateAttempt> Accept(const std::string& token, uint64_t now,
        const std::vector<Supporter::PublicKey>& keys, const ReleaseCheckResult& release,
        std::wstring profile, std::wstring installRoot, std::wstring arch);
    const uint64_t Id;
    const ReleaseCheckResult Release;
    const std::wstring Profile, InstallRoot, Arch;
    bool Run(const std::function<ManagedUpdateResult(const ManagedUpdateAttempt&)>& work);
    bool TakeCompletion(uint64_t id, ManagedUpdateResult& result);
    void Cancel();

private:
    ManagedUpdateAttempt(const ReleaseCheckResult& release, std::wstring profile, std::wstring installRoot, std::wstring arch);
    enum class State { Accepted, Running, Finished, Consumed, Cancelled };
    std::mutex Mutex;
    State Current = State::Accepted;
    ManagedUpdateResult Result;
};

// The factory (including staging/sink opening) is deliberately inside the admission boundary.
bool RunManagedUpdate(const std::shared_ptr<ManagedUpdateAttempt>& attempt,
    const std::function<ManagedUpdateResult(const ManagedUpdateAttempt&)>& work);
} // namespace Sally::Update
