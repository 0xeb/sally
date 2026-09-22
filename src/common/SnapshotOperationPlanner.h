// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// SnapshotOperationPlanner — UI-free operation DTOs from CSelectionSnapshot.
//
// This is the planning seam between the panel/UI snapshot and the legacy
// COperations worker script. Tests can exercise the same path/name decisions
// that production uses without linking the full worker/UI surface.

#pragma once

#include "CBuildConfig.h"
#include "CSelectionSnapshot.h"

#include <string>
#include <utility>

namespace sally::operation_planner
{

enum class PlannedOperationKind
{
    CopyFile,
    MoveFile,
    DeleteFile,
    ConvertFile,
    CopyDirectory,
    MoveDirectory,
    DeleteDirectory,
    ConvertDirectory,
    ChangeAttrsFile,      // P5
    ChangeAttrsDirectory, // P5
    ChangeCaseFile,       // P5
    ChangeCaseDirectory,  // P5
    CountSizeFile,
    CountSizeDirectory,
};

struct CPlannedSnapshotItem
{
    EActionType Action = EActionType::Copy;
    PlannedOperationKind Kind = PlannedOperationKind::CopyFile;
    bool IsDir = false;

    // Operation paths and leaf names are Unicode-only.
    std::wstring SourceParentW;
    std::wstring TargetParentW;

    std::wstring ItemNameW;
    std::wstring TargetNameW;

    std::wstring SourcePathW;
    std::wstring TargetPathW;

    unsigned __int64 Size = 0;
    DWORD Attr = 0;
    FILETIME LastWrite = {};

    bool HasTarget() const
    {
        return Kind == PlannedOperationKind::CopyFile ||
               Kind == PlannedOperationKind::MoveFile ||
               Kind == PlannedOperationKind::CopyDirectory ||
               Kind == PlannedOperationKind::MoveDirectory;
    }

    bool IsFile() const
    {
        return !IsDir;
    }
};

using CPlannedFileOperation = CPlannedSnapshotItem;

inline bool IsDefaultMask(const std::wstring& mask)
{
    return mask.empty() || mask == L"*.*";
}

inline std::wstring JoinPathW(const std::wstring& dir, const std::wstring& name)
{
    if (dir.empty())
        return name;
    if (name.empty())
        return dir;
    std::wstring out = dir;
    if (out.back() != L'\\')
        out.push_back(L'\\');
    out += name;
    return out;
}

inline PlannedOperationKind PlannedKindFor(EActionType action, bool isDir)
{
    if (action == EActionType::Copy)
        return isDir ? PlannedOperationKind::CopyDirectory : PlannedOperationKind::CopyFile;
    if (action == EActionType::Move)
        return isDir ? PlannedOperationKind::MoveDirectory : PlannedOperationKind::MoveFile;
    if (action == EActionType::Convert || action == EActionType::RecursiveConvert)
        return isDir ? PlannedOperationKind::ConvertDirectory : PlannedOperationKind::ConvertFile;
    if (action == EActionType::ChangeAttrs)
        return isDir ? PlannedOperationKind::ChangeAttrsDirectory : PlannedOperationKind::ChangeAttrsFile;
    if (action == EActionType::ChangeCase)
        return isDir ? PlannedOperationKind::ChangeCaseDirectory : PlannedOperationKind::ChangeCaseFile;
    if (action == EActionType::CountSize)
        return isDir ? PlannedOperationKind::CountSizeDirectory : PlannedOperationKind::CountSizeFile;
    return isDir ? PlannedOperationKind::DeleteDirectory : PlannedOperationKind::DeleteFile;
}

inline bool TryPlanChildItem(EActionType action,
                             const std::wstring& sourceParentW,
                             const std::wstring& targetParentW,
                             const std::wstring& itemNameW,
                             const std::wstring& targetNameW,
                             bool isDir,
                             unsigned __int64 size,
                             DWORD attr,
                             FILETIME lastWrite,
                             CPlannedSnapshotItem& plan)
{
    plan = CPlannedSnapshotItem{};
    if (action != EActionType::Copy &&
        action != EActionType::Move &&
        action != EActionType::Delete &&
        action != EActionType::Convert &&
        action != EActionType::RecursiveConvert &&
        action != EActionType::ChangeAttrs && // P5
        action != EActionType::ChangeCase &&  // P5
        action != EActionType::CountSize)
    {
        return false;
    }
    if (sourceParentW.empty() || itemNameW.empty())
    {
        return false;
    }

    const bool needsTarget = (action == EActionType::Copy || action == EActionType::Move);
    if (needsTarget &&
        (targetParentW.empty() || targetNameW.empty()))
    {
        return false;
    }

    plan.Action = action;
    plan.Kind = PlannedKindFor(action, isDir);
    plan.IsDir = isDir;
    plan.SourceParentW = sourceParentW;
    plan.TargetParentW = targetParentW;
    plan.ItemNameW = itemNameW;
    plan.TargetNameW = needsTarget ? targetNameW : itemNameW;
    plan.SourcePathW = JoinPathW(plan.SourceParentW, plan.ItemNameW);
    if (plan.HasTarget())
    {
        plan.TargetPathW = JoinPathW(plan.TargetParentW, plan.TargetNameW);
    }
    plan.Size = size;
    plan.Attr = attr;
    plan.LastWrite = lastWrite;

    return true;
}

inline bool TryPlanSnapshotItem(const CSelectionSnapshot& snapshot,
                                const CBuildConfig& config,
                                const CSnapshotItem& item,
                                CPlannedSnapshotItem& plan)
{
    plan = CPlannedSnapshotItem{};

    if (snapshot.Action != EActionType::Copy &&
        snapshot.Action != EActionType::Move &&
        snapshot.Action != EActionType::Delete &&
        snapshot.Action != EActionType::Convert &&
        snapshot.Action != EActionType::RecursiveConvert &&
        snapshot.Action != EActionType::ChangeAttrs && // P5 (file-only)
        snapshot.Action != EActionType::ChangeCase &&  // P5 (file-only)
        snapshot.Action != EActionType::CountSize)
    {
        return false;
    }

    std::wstring sourcePathW = snapshot.SourcePathW;
    if (sourcePathW.empty())
        return false;

    // Main2 absorption: a top-level item may live under its own
    // parent (multi-directory drops). Empty = inherit the snapshot source.
    if (!item.SourceParentW.empty())
    {
        sourcePathW = item.SourceParentW;
    }

    std::wstring targetPathW;
    if (snapshot.Action == EActionType::Copy || snapshot.Action == EActionType::Move)
    {
        targetPathW = snapshot.TargetPathW;
        if (targetPathW.empty())
            return false;
        if (!IsDefaultMask(snapshot.Mask) && !config.EnableExplicitTargetNames)
            return false;
    }

    const std::wstring& itemNameW = item.NameW;
    if (itemNameW.empty())
        return false;

    std::wstring targetNameW;
    if (snapshot.Action == EActionType::Copy || snapshot.Action == EActionType::Move)
    {
        targetNameW = item.HasTargetName ? item.TargetNameW : itemNameW;

        if (!IsDefaultMask(snapshot.Mask) &&
            (!item.HasTargetName || targetNameW.empty()))
        {
            return false;
        }
    }
    else
    {
        targetNameW = itemNameW;
    }

    return TryPlanChildItem(snapshot.Action,
                            sourcePathW, targetPathW,
                            itemNameW, targetNameW,
                            item.IsDir,
                            item.Size, item.Attr, item.LastWrite,
                            plan);
}

inline bool TryPlanFileOperation(const CSelectionSnapshot& snapshot,
                                 const CBuildConfig& config,
                                 const CSnapshotItem& item,
                                 CPlannedFileOperation& plan)
{
    CPlannedSnapshotItem itemPlan;
    if (!TryPlanSnapshotItem(snapshot, config, item, itemPlan) || itemPlan.IsDir)
        return false;
    plan = std::move(itemPlan);
    return true;
}

} // namespace sally::operation_planner
