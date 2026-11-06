// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// Panel tabs. A panel shows its active tab; the other tabs are saved states. Switching
// saves the panel into the outgoing tab (location, view, sort, filter, focus, top index,
// selection, its Back/Forward history) and loads the incoming tab through the normal path change
// (CPathHistoryItem::Execute), so plugins only ever see an ordinary path change. A plugin file
// system left by a tab is parked by its plugin (detached) and reattached when the tab returns.

#include "precomp.h"

#include "cfgdlg.h"
#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "filesbox.h"
#include "stswnd.h"
#include "tabwnd.h"
#include "dialogs.h"
#include "ui/IPrompter.h"

using namespace sally::tabs;

namespace
{

BOOL SameLocation(const TabRecord& a, const TabRecord& b)
{
    if (a.Kind != b.Kind)
        return FALSE;
    if (a.Kind == LocationKind::PluginFS)
        return a.Primary == b.Primary && a.Secondary == b.Secondary;
    if (StrICmpW(a.Primary.c_str(), b.Primary.c_str()) != 0)
        return FALSE;
    // paths inside an archive are case-sensitive, as in CPathHistoryItem::IsTheSamePath
    return a.Kind == LocationKind::Archive ? a.Secondary == b.Secondary
                                           : StrICmpW(a.Secondary.c_str(), b.Secondary.c_str()) == 0;
}

BOOL SameSettings(const TabRecord& a, const TabRecord& b)
{
    return a.ViewTemplate == b.ViewTemplate && a.SortType == b.SortType && a.ReverseSort == b.ReverseSort &&
           a.FilterEnabled == b.FilterEnabled && a.Filter == b.Filter;
}

CPluginFSInterfaceEncapsulation* FindDetached(CPluginFSInterfaceAbstract* fs)
{
    if (fs == NULL || MainWindow == NULL || MainWindow->DetachedFSList == NULL)
        return NULL;
    CDetachedFSList* list = MainWindow->DetachedFSList;
    for (int i = 0; i < list->Count; i++)
    {
        if (list->At(i)->Contains(fs))
            return list->At(i);
    }
    return NULL;
}

// Closes a parked (detached) plugin file system and removes it from the detached list, as
// CMainWindow::CanUnloadPlugin does. Returns FALSE when the user chose to keep it.
BOOL CloseParkedFS(HWND parent, CPluginFSInterfaceAbstract* fs)
{
    if (fs == NULL || MainWindow == NULL || MainWindow->DetachedFSList == NULL)
        return TRUE;
    CDetachedFSList* list = MainWindow->DetachedFSList;
    for (int i = 0; i < list->Count; i++)
    {
        if (!list->At(i)->Contains(fs))
            continue;
        if (!MainWindow->CloseDetachedFS(parent, list->At(i)))
            return FALSE;
        list->Delete(i);
        if (!list->IsGood())
            list->ResetState();
        return TRUE;
    }
    return TRUE; // no longer parked
}

} // namespace

void CFilesWindow::CaptureActiveTab()
{
    CALL_STACK_MESSAGE1("CFilesWindow::CaptureActiveTab()");
    TabRecord& record = Tabs.ActiveTab();
    if (Is(ptZIPArchive))
    {
        record.Kind = LocationKind::Archive;
        record.Primary = GetZIPArchive();
        record.Secondary = GetZIPPath();
    }
    else if (Is(ptPluginFS))
    {
        record.Kind = LocationKind::PluginFS;
        record.Primary = GetPluginFS()->GetPluginFSName();
        std::wstring userPart;
        if (GetPluginFS()->NotEmpty() && GetPluginFS()->GetCurrentPathW(userPart))
            record.Secondary = userPart;
        else
            record.Secondary.clear();
    }
    else
    {
        record.Kind = LocationKind::Disk;
        record.Primary = GetPathW();
        record.Secondary.clear();
    }
    record.ViewTemplate = GetViewTemplateIndex();
    record.SortType = (int)SortType;
    record.ReverseSort = ReverseSort != FALSE;
    record.FilterEnabled = FilterEnabled != FALSE;
    record.Filter = Filter.GetMasksString();
    record.FocusedName.clear();
    record.TopIndex = 0;
    if (ListBox != NULL && ListBox->HWindow != NULL && Dirs != NULL && Files != NULL)
    {
        const int index = GetCaretIndex();
        if (index >= 0 && index < Dirs->Count + Files->Count)
            record.FocusedName = index < Dirs->Count ? Dirs->At(index).Name : Files->At(index - Dirs->Count).Name;
        record.TopIndex = ListBox->GetTopIndex();
    }
    record.Dormant = false;
}

BOOL CFilesWindow::SwitchToTab(std::size_t index)
{
    CALL_STACK_MESSAGE2("CFilesWindow::SwitchToTab(%d)", (int)index);
    if (index >= Tabs.Count() || index == Tabs.Active() || SwitchingTab)
        return FALSE;
    SwitchingTab = TRUE;
    CancelUI(); // quick search and quick rename belong to the tab being left
    RefreshPathHistoryData(); // A's history keeps its focus and top index

    // the outgoing tab A: the panel's state, its selection and history
    CaptureActiveTab();
    const TabRecord outgoing = Tabs.ActiveTab();
    CPanelTabRuntime& out = TabRuntime[outgoing.Id];
    out.SelectedDirs.clear();
    out.SelectedFiles.clear();
    for (int i = 0; i < Dirs->Count; i++)
        if (Dirs->At(i).Selected && wcscmp(Dirs->At(i).Name, L"..") != 0)
            out.SelectedDirs.push_back(Dirs->At(i).Name);
    for (int i = 0; i < Files->Count; i++)
        if (Files->At(i).Selected)
            out.SelectedFiles.push_back(Files->At(i).Name);
    out.ParkedFS = Is(ptPluginFS) ? GetPluginFS()->GetInterface() : NULL;
    out.History = PathHistory;

    // the incoming tab B
    const TabRecord incoming = Tabs.At(index);
    CPanelTabRuntime& in = TabRuntime[incoming.Id];
    if (in.History == NULL)
        in.History = new CPathHistory();
    PathHistory = in.History;
    in.History = NULL; // the active tab's history is PathHistory

    // Leaving a folder by switching tabs is not "working" in it: no Alt+F12 entry (D6).
    const BOOL userWorked = UserWorkedOnThisPath;
    UserWorkedOnThisPath = FALSE;

    BOOL ok = TRUE;
    if (SameLocation(outgoing, incoming))
    {
        // A duplicate or a new tab at the same folder: only its settings and selection differ.
        if (!SameSettings(outgoing, incoming))
            ApplyTabSettings(incoming, TRUE);
    }
    else
    {
        ApplyTabSettings(incoming, FALSE); // the path change lists and sorts with them
        CPathHistoryItem item((int)incoming.Kind, incoming.Primary.c_str(), incoming.Secondary.c_str(), NULL,
                              in.ParkedFS);
        item.ChangeData(incoming.TopIndex, incoming.FocusedName.empty() ? NULL : incoming.FocusedName.c_str());
        ok = item.Execute(this);
    }

    if (!ok)
    {
        // The panel could not leave A (for example the user cancelled repacking an edited
        // archive): A stays active with its own history and settings.
        in.History = PathHistory;
        PathHistory = out.History;
        out.History = NULL;
        UserWorkedOnThisPath = userWorked;
        ApplyTabSettings(outgoing, TRUE);
        SwitchingTab = FALSE;
        RepaintTabStrip();
        RunPendingTabCommand();
        return FALSE;
    }

    // A's file system is still remembered only if its plugin parked it (detached).
    if (out.ParkedFS != NULL && FindDetached(out.ParkedFS) == NULL)
        out.ParkedFS = NULL;
    in.ParkedFS = NULL; // B's file system, if any, is in the panel now
    if (Is(ptPluginFS))
    {
        // Execute may have reattached a file system another tab had parked (same server): it now
        // belongs to this tab only, so closing that other tab must not close it.
        CPluginFSInterfaceAbstract* current = GetPluginFS()->GetInterface();
        for (auto& entry : TabRuntime)
            if (entry.second.ParkedFS == current)
                entry.second.ParkedFS = NULL;
    }
    Tabs.Activate(index);

    // B's selection comes back where B is (not after a vanished folder was shortened to its parent).
    CaptureActiveTab();
    if (SameLocation(Tabs.ActiveTab(), incoming))
    {
        CNames names;
        names.SetCaseSensitive(IsCaseSensitive());
        for (const std::wstring& name : in.SelectedDirs)
            names.Add(TRUE, name.c_str());
        for (const std::wstring& name : in.SelectedFiles)
            names.Add(FALSE, name.c_str());
        names.Sort();
        const int count = Dirs->Count + Files->Count;
        BOOL changed = FALSE;
        for (int i = 0; i < count; i++)
        {
            const BOOL isDir = i < Dirs->Count;
            CFileData* file = isDir ? &Dirs->At(i) : &Files->At(i - Dirs->Count);
            const BOOL want = names.GetCount() > 0 && names.Contains(isDir, file->Name);
            if ((file->Selected != 0) != (want != 0))
            {
                SetSel(want, file);
                changed = TRUE;
            }
        }
        if (changed)
        {
            RepaintListBox(DRAWFLAG_DIRTY_ONLY | DRAWFLAG_SKIP_VISTEST);
            PostMessage(HWindow, WM_USER_SELCHANGED, 0, 0);
        }
    }
    in.SelectedDirs.clear();
    in.SelectedFiles.clear();

    SwitchingTab = FALSE;
    UpdateTabStrip();
    IdleRefreshStates = TRUE; // menus and toolbars follow the new location
    RunPendingTabCommand();
    return TRUE;
}

void CFilesWindow::ApplyTabSettings(const TabRecord& record, BOOL refresh)
{
    CALL_STACK_MESSAGE2("CFilesWindow::ApplyTabSettings(%d)", refresh);
    // No refresh from SelectViewTemplate: a listing always follows, either the path change
    // (refresh == FALSE) or the refresh posted below (the last argument says just that).
    if (record.ViewTemplate != GetViewTemplateIndex())
        SelectViewTemplate(record.ViewTemplate, FALSE, FALSE, VALID_DATA_ALL, FALSE, TRUE);
    SortType = (CSortType)record.SortType;
    ReverseSort = record.ReverseSort ? TRUE : FALSE;
    FilterEnabled = record.FilterEnabled ? TRUE : FALSE;
    Filter.SetMasksString(record.Filter.empty() ? L"*.*" : record.Filter.c_str());
    int errorPos;
    if (!Filter.PrepareMasks(errorPos))
    {
        Filter.SetMasksString(L"*.*");
        Filter.PrepareMasks(errorPos);
    }
    UpdateFilterSymbol();
    if (refresh)
    {
        HANDLES(EnterCriticalSection(&TimeCounterSection));
        int t1 = MyTimeCounter++;
        HANDLES(LeaveCriticalSection(&TimeCounterSection));
        PostMessage(HWindow, WM_USER_REFRESH_DIR, 0, t1); // re-list with the view, sort and filter
    }
}

void CFilesWindow::NewTab(BOOL duplicate)
{
    CALL_STACK_MESSAGE2("CFilesWindow::NewTab(%d)", duplicate);
    if (SwitchingTab)
        return; // a dialog of the path change pumped this message
    if (Tabs.IsFull())
    {
        gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE),
                            FormatStrW(LoadStrW(IDS_TAB_LIMIT), (int)PanelTabSet::kMaxTabs).c_str());
        return;
    }
    CaptureActiveTab();
    TabRecord record = Tabs.ActiveTab();
    record.Title.clear(); // a new tab shows its folder's name
    const std::size_t at = Tabs.InsertAfterActive(record, Configuration.TabsOpenAtEnd != 0);
    if (at == PanelTabSet::npos)
        return;
    CPanelTabRuntime& runtime = TabRuntime[Tabs.At(at).Id];
    runtime.History = new CPathHistory();
    if (duplicate)
    {
        runtime.History->CopyFrom(*PathHistory);
        for (int i = 0; i < Dirs->Count; i++)
            if (Dirs->At(i).Selected && wcscmp(Dirs->At(i).Name, L"..") != 0)
                runtime.SelectedDirs.push_back(Dirs->At(i).Name);
        for (int i = 0; i < Files->Count; i++)
            if (Files->At(i).Selected)
                runtime.SelectedFiles.push_back(Files->At(i).Name);
    }
    else
    {
        // a new tab starts its history at its folder
        runtime.History->AddPath((int)record.Kind, record.Primary.c_str(), record.Secondary.c_str(),
                                 Is(ptPluginFS) ? GetPluginFS()->GetInterface() : NULL,
                                 Is(ptPluginFS) ? GetPluginFS() : NULL);
    }
    if (!SwitchToTab(at))
        UpdateTabStrip();
}

void CFilesWindow::OpenPathInNewTab(const wchar_t* path)
{
    CALL_STACK_MESSAGE2("CFilesWindow::OpenPathInNewTab(%ls)", path);
    if (path == NULL || *path == 0 || SwitchingTab)
        return;
    if (Tabs.IsFull())
    {
        gPrompter->ShowInfo(LoadStrW(IDS_INFOTITLE),
                            FormatStrW(LoadStrW(IDS_TAB_LIMIT), (int)PanelTabSet::kMaxTabs).c_str());
        return;
    }
    CaptureActiveTab();
    TabRecord record = Tabs.ActiveTab(); // the new tab keeps this tab's view, sort and filter
    record.Kind = LocationKind::Disk;
    record.Primary = path;
    record.Secondary.clear();
    record.FocusedName.clear();
    record.TopIndex = 0;
    record.Title.clear();
    const std::size_t at = Tabs.InsertAfterActive(record, Configuration.TabsOpenAtEnd != 0);
    if (at == PanelTabSet::npos)
        return;
    const std::uint32_t id = Tabs.At(at).Id;
    TabRuntime[id].History = new CPathHistory();
    if (!SwitchToTab(at))
    {
        // the folder could not be opened from here: drop the tab again, not onto the reopen stack
        auto runtime = TabRuntime.find(id);
        if (runtime != TabRuntime.end())
        {
            delete runtime->second.History;
            TabRuntime.erase(runtime);
        }
        Tabs.Discard(Tabs.IndexOf(id));
        UpdateTabStrip();
    }
}

void CFilesWindow::CloseTab(std::size_t index)
{
    CALL_STACK_MESSAGE2("CFilesWindow::CloseTab(%d)", (int)index);
    if (index >= Tabs.Count() || Tabs.Count() <= 1 || Tabs.At(index).Locked || SwitchingTab)
        return;
    const std::uint32_t id = Tabs.At(index).Id;
    if (index == Tabs.Active())
    {
        const std::size_t neighbour = index + 1 < Tabs.Count() ? index + 1 : index - 1;
        if (!SwitchToTab(neighbour))
            return; // the panel could not leave the tab: it stays open
        index = Tabs.IndexOf(id);
    }
    auto runtime = TabRuntime.find(id);
    if (runtime != TabRuntime.end())
    {
        // Close the tab's parked file system first; the plugin may ask, as when closing it from
        // Alt+F1. If the user keeps it, the tab stays open with it.
        if (FindDetached(runtime->second.ParkedFS) != NULL &&
            !CloseParkedFS(HWindow, runtime->second.ParkedFS))
        {
            UpdateTabStrip();
            return;
        }
        runtime = TabRuntime.find(id); // closing the file system cleared it from every tab
        if (runtime != TabRuntime.end())
        {
            if (runtime->second.History != NULL)
                delete runtime->second.History;
            TabRuntime.erase(runtime);
        }
    }
    index = Tabs.IndexOf(id);
    Tabs.Close(index);
    UpdateTabStrip();
}

void CFilesWindow::RenameTab(std::size_t index)
{
    CALL_STACK_MESSAGE2("CFilesWindow::RenameTab(%d)", (int)index);
    if (index >= Tabs.Count() || SwitchingTab)
        return;
    if (index == Tabs.Active())
        CaptureActiveTab();
    const std::uint32_t id = Tabs.At(index).Id; // the dialog pumps messages: find the tab again after it
    TabRecord automatic = Tabs.At(index);
    automatic.Title.clear();
    const std::wstring locationName = DisplayTitle(automatic);
    std::wstring name = Tabs.At(index).Title.empty() ? locationName : Tabs.At(index).Title;
    const std::wstring title = LoadStrOwned(IDS_TAB_RENAME_TITLE);
    CTruncatedString subject;
    subject.SetW(LoadStrOwned(IDS_TAB_RENAME_PROMPT).c_str(), NULL);
    CCopyMoveDialog dlg(HWindow, name, title.c_str(), &subject, CM_TAB_RENAME, NULL, 0, FALSE);
    if (dlg.Execute() != IDOK)
        return;
    index = Tabs.IndexOf(id);
    if (index == PanelTabSet::npos)
        return;
    Tabs.Rename(index, name);
    // the location's own name is no name of its own: the tab keeps following its folder
    if (Tabs.At(index).Title == locationName)
        Tabs.Rename(index, std::wstring());
    UpdateTabStrip();
}

void CFilesWindow::ToggleTabLock(std::size_t index)
{
    if (index >= Tabs.Count() || SwitchingTab)
        return;
    Tabs.SetLocked(index, !Tabs.At(index).Locked);
    RepaintTabStrip();
}

void CFilesWindow::MoveTab(std::size_t from, std::size_t to)
{
    if (!SwitchingTab && Tabs.Move(from, to))
        UpdateTabStrip();
}

void CFilesWindow::MoveTabToGap(std::size_t from, std::size_t gap)
{
    if (!SwitchingTab && Tabs.MoveToGap(from, gap) != PanelTabSet::npos)
        UpdateTabStrip();
}

BOOL CFilesWindow::IsFSParkedByTab(CPluginFSInterfaceAbstract* fs)
{
    if (fs == NULL)
        return FALSE;
    for (const auto& entry : TabRuntime)
        if (entry.second.ParkedFS == fs)
            return TRUE;
    return FALSE;
}

BOOL TranslateTabKeyProfile(CMainWindow* mainWindow, const MSG& msg)
{
    if (!Configuration.TabKeysTotalCommander || mainWindow == NULL || msg.message != WM_KEYDOWN ||
        !mainWindow->CaptionIsActive || mainWindow->EditMode || mainWindow->QuickRenameWindowActive() ||
        mainWindow->HasLockedUI() || mainWindow->HDisabledKeyboard != NULL || mainWindow->DragMode ||
        GetCapture() != NULL)
        return FALSE;
    if ((GetKeyState(VK_CONTROL) & 0x8000) == 0 || (GetKeyState(VK_MENU) & 0x8000) != 0)
        return FALSE;
    CFilesWindow* panel = mainWindow->GetActivePanel();
    if (panel == NULL || msg.hwnd != panel->GetListBoxHWND())
        return FALSE; // dialogs, viewers and the command line keep their own keys
    const BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const BOOL repeat = (msg.lParam & 0x40000000) != 0;
    int command = 0;
    if (msg.wParam == 'T' && !shift)
        command = CM_TAB_NEW;
    else if (msg.wParam == 'W' && !shift)
        command = CM_TAB_CLOSE;
    else if (msg.wParam == VK_TAB)
    {
        if (panel->Tabs.Count() < 2)
            return FALSE; // one tab: Ctrl+Tab still reaches the command line
        command = shift ? CM_TAB_PREV : CM_TAB_NEXT;
    }
    if (command == 0)
        return FALSE;
    if (!repeat || command == CM_TAB_NEXT || command == CM_TAB_PREV) // holding Ctrl+T/W does not repeat
        SendMessage(mainWindow->HWindow, WM_COMMAND, command, 0);
    return TRUE;
}

void CFilesWindow::RunPendingTabCommand()
{
    const int command = PendingTabCommand;
    PendingTabCommand = 0;
    if (command != 0 && MainWindow != NULL && MainWindow->HWindow != NULL)
        PostMessage(MainWindow->HWindow, WM_COMMAND, command, 0); // through the active panel, after this switch
}

void CFilesWindow::TabCommand(int command)
{
    CALL_STACK_MESSAGE2("CFilesWindow::TabCommand(%d)", command);
    if (SwitchingTab)
    {
        // a key pressed while the panel is still leaving a tab (a slow archive or network folder):
        // the latest one runs when the switch ends
        PendingTabCommand = command;
        return;
    }
    switch (command)
    {
    case CM_TAB_NEW:
        NewTab(FALSE);
        break;

    case CM_TAB_DUPLICATE:
        NewTab(TRUE);
        break;

    case CM_TAB_CLOSE:
        CloseTab(Tabs.Active());
        break;

    case CM_TAB_CLOSEOTHERS:
    {
        const std::uint32_t keep = Tabs.ActiveTab().Id;
        for (std::size_t i = Tabs.Count(); i-- > 0;)
        {
            if (Tabs.At(i).Id != keep)
                CloseTab(i);
        }
        break;
    }

    case CM_TAB_REOPEN:
    {
        const std::size_t at = Tabs.ReopenClosed();
        if (at != PanelTabSet::npos)
        {
            CPanelTabRuntime& runtime = TabRuntime[Tabs.At(at).Id];
            if (runtime.History == NULL)
                runtime.History = new CPathHistory();
            if (!SwitchToTab(at))
                UpdateTabStrip();
        }
        break;
    }

    case CM_TAB_NEXT:
        if (Tabs.Count() > 1)
            SwitchToTab(Tabs.NextIndex());
        break;

    case CM_TAB_RENAME:
        RenameTab(Tabs.Active());
        break;

    case CM_TAB_LOCK:
        ToggleTabLock(Tabs.Active());
        break;

    case CM_TAB_MOVELEFT:
        if (Tabs.Active() > 0)
            MoveTab(Tabs.Active(), Tabs.Active() - 1);
        break;

    case CM_TAB_MOVERIGHT:
        if (Tabs.Active() + 1 < Tabs.Count())
            MoveTab(Tabs.Active(), Tabs.Active() + 1);
        break;

    case CM_TAB_PREV:
        if (Tabs.Count() > 1)
            SwitchToTab(Tabs.PrevIndex());
        break;

    case CM_TAB_OPENFOCUSED:
    {
        if (!Is(ptDisk))
            break;
        const int index = GetCaretIndex();
        if (index < 0 || index >= Dirs->Count)
            break; // a folder must be focused
        const wchar_t* name = Dirs->At(index).Name;
        if (wcscmp(name, L"..") == 0)
            break;
        std::wstring path = GetPathW();
        if (!path.empty() && path.back() != L'\\')
            path += L'\\';
        path += name;
        OpenPathInNewTab(path.c_str());
        break;
    }

    case CM_TAB_TOGGLEBAR:
    {
        Configuration.ShowTabBar = !Configuration.ShowTabBar;
        if (MainWindow != NULL)
        {
            MainWindow->LeftPanel->UpdateTabStrip();
            MainWindow->RightPanel->UpdateTabStrip();
        }
        break;
    }
    }
}

BOOL CFilesWindow::WantsTabStrip()
{
    return Configuration.ShowTabBar || Tabs.Count() > 1;
}

int CFilesWindow::GetTabStripHeight()
{
    return TabStrip != NULL && TabStrip->HWindow != NULL ? TabStrip->GetNeededHeight() : 0;
}

void CFilesWindow::RepaintTabStrip()
{
    if (TabStrip != NULL && TabStrip->HWindow != NULL)
        TabStrip->Repaint();
}

void CFilesWindow::UpdateTabStrip()
{
    if (TabStrip != NULL)
        TabStrip->CancelDrag(); // the tabs changed under a drag (a key closed or moved one)
    CALL_STACK_MESSAGE1("CFilesWindow::UpdateTabStrip()");
    if (TabStrip == NULL || HWindow == NULL)
        return;
    const BOOL want = WantsTabStrip();
    const BOOL shown = TabStrip->HWindow != NULL;
    if (want == shown)
    {
        RepaintTabStrip();
        return;
    }
    if (shown)
        TabStrip->DestroyWindow();
    else if (!TabStrip->Create(CTabWindow::WindowClassName(), L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, HWindow,
                               (HMENU)IDC_TABSTRIP, HInstance, TabStrip))
        TRACE_E("Unable to create the tab strip.");
    RECT r;
    GetClientRect(HWindow, &r);
    SendMessage(HWindow, WM_SIZE, SIZE_RESTORED, MAKELONG(r.right - r.left, r.bottom - r.top));
    if (TabStrip->HWindow != NULL)
        ShowWindow(TabStrip->HWindow, SW_SHOWNA);
    InvalidateRect(HWindow, NULL, TRUE);
    if (MainWindow != NULL)
        MainWindow->LayoutWindows(); // the middle toolbar starts below the strips
}

void CFilesWindow::ClearPluginFSFromTabs(CPluginFSInterfaceAbstract* fs)
{
    for (auto& entry : TabRuntime)
    {
        if (entry.second.History != NULL)
            entry.second.History->ClearPluginFSFromHistory(fs);
        if (entry.second.ParkedFS == fs)
            entry.second.ParkedFS = NULL;
    }
}

void CFilesWindow::SetTabsFromConfig(std::vector<TabRecord> records, std::size_t active, BOOL navigateActive)
{
    CALL_STACK_MESSAGE1("CFilesWindow::SetTabsFromConfig()");
    if (!records.empty())
    {
        if (active >= records.size())
        {
            // the saved active tab was unreadable: the panel's location becomes a tab of its own
            if (records.size() >= PanelTabSet::kMaxTabs)
                records.pop_back();
            TabRecord current = records.back();
            current.Title.clear();
            current.Locked = false;
            current.Kind = LocationKind::Disk;
            current.Primary.clear();
            current.Secondary.clear();
            current.FocusedName.clear();
            current.TopIndex = 0;
            current.Dormant = false;
            records.push_back(current);
            active = records.size() - 1;
            navigateActive = FALSE; // nothing saved to go back to; CaptureActiveTab fills it in
        }
        const TabRecord savedActive = records[active];
        Tabs.Replace(std::move(records), active);
        // The panel is already at its saved disk path (or where the command line put it). An
        // active tab that was inside an archive goes back there.
        if (navigateActive && savedActive.Kind == LocationKind::Archive && Is(ptDisk))
        {
            CPathHistoryItem item((int)savedActive.Kind, savedActive.Primary.c_str(), savedActive.Secondary.c_str(),
                                  NULL, NULL);
            item.ChangeData(savedActive.TopIndex,
                            savedActive.FocusedName.empty() ? NULL : savedActive.FocusedName.c_str());
            SwitchingTab = TRUE; // the active record is captured once the path is set
            item.Execute(this);
            SwitchingTab = FALSE;
        }
        CaptureActiveTab();
        for (std::size_t i = 0; i < Tabs.Count(); i++)
        {
            if (i != Tabs.Active())
                TabRuntime[Tabs.At(i).Id].History = new CPathHistory();
        }
    }
    UpdateTabStrip();
}

std::vector<TabRecord> CFilesWindow::GetTabsForConfig(std::size_t& active)
{
    if (!SwitchingTab)
        CaptureActiveTab();
    active = Tabs.Active();
    return Tabs.Records();
}

void CFilesWindow::FreeTabRuntime(BOOL closeParkedFS)
{
    for (auto& entry : TabRuntime)
    {
        if (entry.second.History != NULL && entry.second.History != PathHistory)
            delete entry.second.History;
        entry.second.History = NULL;
        if (closeParkedFS && entry.second.ParkedFS != NULL)
        {
            CPluginFSInterfaceAbstract* parked = entry.second.ParkedFS;
            entry.second.ParkedFS = NULL;
            CloseParkedFS(HWindow, parked);
        }
    }
    TabRuntime.clear();
}
