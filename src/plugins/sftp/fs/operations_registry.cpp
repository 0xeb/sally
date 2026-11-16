// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "operations_registry.h"

#include "../ui/operations_dialog.h"

COperationsRegistry OperationsRegistry;

bool COperationsRegistry::Launch(COperationSettings settings, CPanelConnectParams params,
                                 std::vector<CQueueItem> roots, const COperationLaunch& launch)
{
    DropClosedFinished();
    COperationWindowOptions options;
    options.source = launch.source;
    options.target = launch.target;
    options.hostName = launch.hostName;
    // Runs on a worker thread. Only Sally's thread-safe notification is called, with paths copied into the lambda: no
    // FS object is touched, so nothing can dangle.
    const std::vector<std::wstring> changed = launch.changedPaths;
    options.finished = [changed]()
    {
        for (const std::wstring& path : changed)
            SalamanderGeneral->PostChangeOnPathNotification(path.c_str(), TRUE);
    };

    CWindowedOperation made = COperationWindowHost::Create(std::move(settings), std::move(params), std::move(roots),
                                                           std::move(options));
    const uint64_t uid = list_.Add(made.operation);
    made.operation->SetNumber(uid); // names the logs of its workers
    // shown before the workers start: a question of a worker never finds a window that is not coming
    made.window->Show();
    if (!made.operation->Start())
    {
        made.window->Close();
        list_.Remove(uid);
        SalamanderGeneral->SalMessageBox(SalamanderGeneral->GetMsgBoxParent(), LangStr(IDS_ERR_OP_START).c_str(),
                                         LangStr(IDS_PLUGINNAME).c_str(), MB_OK | MB_ICONEXCLAMATION);
        return false;
    }
    CEntry entry;
    entry.window = made.window;
    entry.description = launch.target.empty() // a delete has no target
                            ? launch.source
                            : SPLFormatStringOwned(LangStr(IDS_OPS_DESCRIPTION).c_str(), launch.source.c_str(),
                                                   launch.target.c_str());
    entries_[uid] = std::move(entry);
    return true;
}

std::wstring COperationsRegistry::DescriptionOf(uint64_t uid) const
{
    const auto it = entries_.find(uid);
    return it != entries_.end() ? it->second.description : std::wstring();
}

bool COperationsRegistry::ShowWindowOf(uint64_t uid)
{
    const auto it = entries_.find(uid);
    return it != entries_.end() && it->second.window->Show();
}

// The window thread never calls into the registry: this runs on the main thread only. RemoveFinished may join the
// window thread (already ended, because the window is closed) while no lock is held.
void COperationsRegistry::DropClosedFinished()
{
    for (auto it = entries_.begin(); it != entries_.end();)
    {
        if (!it->second.window->IsWindowOpen() && list_.RemoveFinished(it->first))
            it = entries_.erase(it);
        else
            ++it;
    }
}

void COperationsRegistry::ShowDialog(HWND parent)
{
    DropClosedFinished();
    COperationsDialogCallbacks callbacks;
    callbacks.describe = [this](uint64_t uid) { return DescriptionOf(uid); };
    callbacks.showWindow = [this](uint64_t uid) { return ShowWindowOf(uid); };
    ShowOperationsDialog(parent, list_, callbacks);
}

bool COperationsRegistry::ConfirmRelease(HWND parent, BOOL force)
{
    if (force || SalamanderGeneral->IsCriticalShutdown() || !list_.HasRunning())
        return true;
    const int answer = SalamanderGeneral->SalMessageBox(parent, LangStr(IDS_OPS_RELEASE_PROMPT).c_str(),
                                                        LangStr(IDS_PLUGINNAME).c_str(), MB_YESNO | MB_ICONQUESTION);
    return answer == IDYES;
}

void COperationsRegistry::Shutdown()
{
    list_.CancelAll(); // the workers stop first, so no window is asked a question meanwhile
    for (auto& entry : entries_)
        entry.second.window->Close();
    list_.Clear(); // joins the workers and the window threads (the operations' deleters)
    entries_.clear();
}
