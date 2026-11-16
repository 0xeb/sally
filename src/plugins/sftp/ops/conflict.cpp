#include "conflict.h"

namespace
{
    const int kMaxRenameTries = 9999;
    const char kFilePartSuffix[] = ".filepart";

    CConflictDecision Make(ConflictAction action, uint64_t offset = 0, Problem problem = Problem::None)
    {
        CConflictDecision d;
        d.action = action;
        d.resumeOffset = offset;
        d.problem = problem;
        return d;
    }

    CConflictDecision DecideResume(const CTransferSide& source, const CTransferSide& target)
    {
        if (target.size == 0)
            return Make(ConflictAction::Transfer);
        if (target.size < source.size)
            return Make(ConflictAction::Transfer, target.size);
        if (target.size == source.size)
            return Make(ConflictAction::Skip);
        return Make(ConflictAction::Park, 0, Problem::TargetExists);
    }

    CConflictDecision DecideForced(SolveAction force, const CTransferSide& source, const CTransferSide& target)
    {
        switch (force)
        {
        case SolveAction::Skip:
            return Make(ConflictAction::Skip);
        case SolveAction::Rename:
            return Make(ConflictAction::Rename);
        case SolveAction::Resume:
            return DecideResume(source, target);
        default: // Overwrite (Retry never gets here)
            return Make(ConflictAction::Transfer);
        }
    }

    CConflictDecision DecideByPolicy(ConflictPolicy policy, const CTransferSide& source, const CTransferSide& target)
    {
        switch (policy)
        {
        case ConflictPolicy::Overwrite:
            return Make(ConflictAction::Transfer);
        case ConflictPolicy::Resume:
            return DecideResume(source, target);
        case ConflictPolicy::Skip:
            return Make(ConflictAction::Skip);
        case ConflictPolicy::OverwriteIfOlder:
            return source.mtime > target.mtime ? Make(ConflictAction::Transfer) : Make(ConflictAction::Skip);
        case ConflictPolicy::Rename:
            return Make(ConflictAction::Rename);
        default: // Ask
            return Make(ConflictAction::Park, 0, Problem::TargetExists);
        }
    }

    // Position of the extension's dot in `name`, or npos when there is none (no dot, a leading dot only, or a trailing dot).
    size_t ExtensionDot(const std::string& name)
    {
        size_t dot = name.rfind('.');
        return (dot == std::string::npos || dot == 0 || dot + 1 == name.size()) ? std::string::npos : dot;
    }

    size_t ExtensionDot(const std::wstring& name)
    {
        size_t dot = name.rfind(L'.');
        return (dot == std::wstring::npos || dot == 0 || dot + 1 == name.size()) ? std::wstring::npos : dot;
    }
} // namespace

CConflictDecision DecideConflict(ConflictPolicy policy, SolveAction forceAction, const CTransferSide& source,
                                 const CTransferSide& target)
{
    if (!target.exists)
        return Make(ConflictAction::Transfer);
    if (source.isDir && target.isDir)
        return Make(ConflictAction::Transfer);

    const bool forced = forceAction != SolveAction::Retry;
    if (source.isDir != target.isDir)
    {
        // type clash: only "skip" and "rename" make sense, everything else needs the user
        if (forced && (forceAction == SolveAction::Skip || forceAction == SolveAction::Rename))
            return DecideForced(forceAction, source, target);
        return Make(ConflictAction::Park, 0, Problem::TargetExists);
    }

    if (forced)
        return DecideForced(forceAction, source, target);
    return DecideByPolicy(policy, source, target);
}

std::wstring AutoRenameLocal(const std::wstring& name, std::function<bool(const std::wstring&)> exists)
{
    const size_t dot = ExtensionDot(name);
    const std::wstring stem = dot == std::wstring::npos ? name : name.substr(0, dot);
    const std::wstring ext = dot == std::wstring::npos ? std::wstring() : name.substr(dot);
    for (int n = 2; n <= kMaxRenameTries; n++)
    {
        std::wstring candidate = stem + L" (" + std::to_wstring(n) + L")" + ext;
        if (!exists(candidate))
            return candidate;
    }
    return std::wstring();
}

std::string AutoRenameRemote(const std::string& rawName, std::function<bool(const std::string&)> exists)
{
    const size_t dot = ExtensionDot(rawName);
    const std::string stem = dot == std::string::npos ? rawName : rawName.substr(0, dot);
    const std::string ext = dot == std::string::npos ? std::string() : rawName.substr(dot);
    for (int n = 2; n <= kMaxRenameTries; n++)
    {
        std::string candidate = stem + " (" + std::to_string(n) + ")" + ext;
        if (!exists(candidate))
            return candidate;
    }
    return std::string();
}

std::string SafeUploadName(const std::string& rawName)
{
    return rawName + kFilePartSuffix;
}

bool IsSafeUploadName(const std::string& rawName)
{
    const size_t suffixLen = sizeof(kFilePartSuffix) - 1;
    return rawName.size() > suffixLen && rawName.compare(rawName.size() - suffixLen, suffixLen, kFilePartSuffix) == 0;
}
