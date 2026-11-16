#include "precomp.h"

#include "problem_text.h"

#include "error_text.h"

namespace
{
int ProblemStringId(Problem problem)
{
    switch (problem)
    {
    case Problem::None:
        return 0;
    case Problem::TargetExists:
        return IDS_OP_PROBLEM_TARGET_EXISTS;
    case Problem::InvalidLocalName:
        return IDS_OP_PROBLEM_INVALID_LOCAL_NAME;
    case Problem::InvalidRemoteName:
        return IDS_OP_PROBLEM_INVALID_REMOTE_NAME;
    case Problem::TransferFailed:
        return IDS_OP_PROBLEM_TRANSFER_FAILED;
    case Problem::ConnectionLost:
        return IDS_OP_PROBLEM_CONNECTION_LOST;
    case Problem::PermissionDenied:
        return IDS_OP_PROBLEM_PERMISSION_DENIED;
    case Problem::NotFound:
        return IDS_OP_PROBLEM_NOT_FOUND;
    case Problem::DirCreateFailed:
        return IDS_OP_PROBLEM_DIR_CREATE_FAILED;
    case Problem::DeleteFailed:
        return IDS_OP_PROBLEM_DELETE_FAILED;
    case Problem::ChmodFailed:
        return IDS_OP_PROBLEM_CHMOD_FAILED;
    case Problem::PermissionsUnknown:
        return IDS_OP_PROBLEM_PERMS_UNKNOWN;
    case Problem::SymlinkLoop:
        return IDS_OP_PROBLEM_SYMLINK_LOOP;
    case Problem::LocalIoError:
        return IDS_OP_PROBLEM_LOCAL_IO_ERROR;
    case Problem::DiskFull:
        return IDS_OP_PROBLEM_DISK_FULL;
    case Problem::Other:
        return IDS_OP_PROBLEM_OTHER;
    }
    return IDS_OP_PROBLEM_OTHER;
}
} // namespace

std::wstring ProblemText(Problem problem)
{
    const int id = ProblemStringId(problem);
    return id == 0 ? std::wstring() : LangStr(id);
}

std::wstring ProblemTextWithDetails(Problem problem, const std::wstring& details)
{
    std::wstring text = ProblemText(problem);
    if (details.empty())
        return text;
    return text.empty() ? details : text + L": " + details;
}

std::wstring ItemDetails(const CQueueItem& item)
{
    return item.sshError.IsError() ? DescribeError(item.sshError) : item.errorText;
}

std::wstring ItemStateText(ItemState state)
{
    switch (state)
    {
    case ItemState::Waiting:
        return LangStr(IDS_OP_ITEM_WAITING);
    case ItemState::Processing:
        return LangStr(IDS_OP_ITEM_PROCESSING);
    case ItemState::Delayed:
        return LangStr(IDS_OP_ITEM_DELAYED);
    case ItemState::Done:
        return LangStr(IDS_OP_ITEM_DONE);
    case ItemState::Skipped:
        return LangStr(IDS_OP_ITEM_SKIPPED);
    case ItemState::Failed:
        return LangStr(IDS_OP_ITEM_FAILED);
    case ItemState::UserInputNeeded:
        return LangStr(IDS_OP_ITEM_USERINPUT);
    case ItemState::ForcedToFail:
        return LangStr(IDS_OP_ITEM_FORCEDFAIL);
    }
    return std::wstring();
}
