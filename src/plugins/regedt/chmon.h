// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

enum CChangeMonitorThreadState
{
    cmtIdle,
    cmtWaitingForChange
};
enum CActionEvent
{
    aeNoAction,
    aeSetPath,
    aeFinish,
    aeCancel
};

class CChangeMonitor;

class CChangeMonitorThread : public CThread
{
    CChangeMonitorThreadState State;
    TIndirectArray<CPluginFSInterface> ConnectedFS;
    int Root;
    std::wstring Key;
    HANDLE ActionEvent;
    CActionEvent Action;
    HANDLE RegistryEvent;
    CChangeMonitor& Monitor;
    int IgnoreChanges;

public:
    CChangeMonitorThread(CChangeMonitor& monitor);
    ~CChangeMonitorThread();
    virtual unsigned Body();

    friend class CChangeMonitor;
};

class CChangeMonitor
{
    TIndirectArray<CChangeMonitorThread> Threads;
    CCS CS;

public:
    CChangeMonitor();
    ~CChangeMonitor();
    void AddPath(int root, const wchar_t* key, CPluginFSInterface* fs);
    void Cancel(CPluginFSInterface* fs);
    void Stop();
    void IgnoreNextRootChange(int root);

    friend CChangeMonitorThread;
};

extern CChangeMonitor ChangeMonitor;
