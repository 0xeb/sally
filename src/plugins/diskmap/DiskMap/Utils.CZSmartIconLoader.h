// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Utils.CZIconLoader.h"

class CZSmartIconLoader
{
protected:
    HWND _owner;
    UINT _msg;

    CWorkerThread* _worker;

    volatile int _requestedIconId;
    volatile DWORD _requestedFlags;
    std::wstring _requestedFilename;
    volatile LPVOID _requestedlParam;

    static DWORD_PTR WINAPI WorkerThreadProc(CWorkerThread* mythread, LPVOID lpParam)
    {
        CZSmartIconLoader* self = (CZSmartIconLoader*)lpParam;
        self->WorkerThread();
        return 0;
    }

    void WorkerThread()
    {
        int currentIconId = 0;

        DWORD iconFlags = 0;
        LPVOID iconlParam = NULL;
        std::wstring iconFile;

        while (true)
        {
            bool loadNewIcon = false;
            this->_worker->GetLock()->Enter();
            loadNewIcon = (currentIconId != this->_requestedIconId);
            if (loadNewIcon)
            {
                try
                {
                    iconFile = this->_requestedFilename;
                    iconFlags = this->_requestedFlags;
                    iconlParam = this->_requestedlParam;
                    currentIconId = this->_requestedIconId;
                }
                catch (...)
                {
                    loadNewIcon = false;
                }
            }
            this->_worker->GetLock()->Leave();

            if (this->_worker->Aborting())
                break;

            if (loadNewIcon)
            {
                HICON hicon = CZIconLoader::LoadIconSync(iconFile.c_str(), iconFlags);
                PostMessage(this->_owner, this->_msg, (WPARAM)hicon, (LPARAM)iconlParam);
            }

            if (this->_worker->Aborting())
                break;

            Sleep(50);

            if (this->_worker->Aborting())
                break;
        }
    }

public:
    CZSmartIconLoader(HWND owner, UINT msg)
    {
        _owner = owner;
        _msg = msg;
        _requestedIconId = 0;
        new CWorkerThread(&_worker,
                          CZSmartIconLoader::WorkerThreadProc,
                          this,
                          0,
                          0,
                          0,
                          FALSE);
    }

    ~CZSmartIconLoader()
    {
        delete this->_worker;
    }
    void PlanLoadIcon(CZFile* file, DWORD flags)
    {
        std::wstring requestedFilename;
        file->GetFullName(requestedFilename);
        this->_worker->GetLock()->Enter();
        this->_requestedIconId++;
        this->_requestedFlags = flags;
        this->_requestedlParam = file;
        this->_requestedFilename.swap(requestedFilename);
        this->_worker->GetLock()->Leave();
    }
    /*
	void PlanLoadIcon(CZString const *path, DWORD flags, HWND owner, UINT msg, LPVOID lParam)	
	{
		CZIconLoader *self = new CZIconLoader(path->GetString(), flags);
		return CZIconLoader::BeginAsyncLoadIcon(self, owner, msg, lParam);
	}*/
};
