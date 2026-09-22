// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <ShObjIdl.h>
#include "versinfo.rh2"
#include "jumplist.h"
#include "mainwnd.h"
#include "common/IPathService.h"
//#include "propvarutil.h"

//#pragma comment(lib,"Shlwapi.lib")

/* JUMP LIST DOC
http://blogs.windows.com/windows/archive/b/developers/archive/2009/06/22/developing-for-the-windows-7-taskbar-jump-into-jump-lists-part-1.aspx
http://blogs.windows.com/windows/archive/b/developers/archive/2009/06/25/developing-for-the-windows-7-taskbar-jump-into-jump-lists-part-2.aspx
http://blogs.windows.com/windows/archive/b/developers/archive/2009/07/02/developing-for-the-windows-7-taskbar-jump-into-jump-lists-part-3.aspx
http://msdn.microsoft.com/en-us/library/dd378460%28v=VS.85%29.aspx#custom_jump_lists
*/

DEFINE_PROPERTYKEY(PKEY_Title, 0xF29F85E0, 0x4FF9, 0x1068, 0xAB, 0x91, 0x08, 0x00, 0x2B, 0x27, 0xB3, 0xD9, 2);
DEFINE_PROPERTYKEY(PKEY_AppUserModel_IsDestListSeparator, 0x9F4C2855, 0x9F79, 0x4B39, 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3, 6);

// wide: path/name are now the genuine wide hot-path values (see AddTasksToList
// below); this whole function switched to IShellLinkW (unqualified IShellLink/IID_PPV_ARGS(&ret)
// resolved to the ANSI interface, since core code never defines UNICODE - same defect class as
// the shellsup.cpp shortcut-resolution fix) and GetModuleFileNameW (no wide value was
// ever captured before, the same shape as the SVG icon-path fix) so a non-ASCII hot-path
// name/path, or a Sally install path with
// non-ASCII characters, no longer silently mangles the jump-list entry.
HRESULT CreateShellLink(const wchar_t* path, const wchar_t* name, IShellLinkW** psl)
{
    const std::wstring params = std::wstring(L"-AJ \"") + path + L"\"";
    if (params.length() < INFOTIPSIZE) // length limit for W2K+ when using SetArguments
    {
        HRESULT hres;
        IShellLinkW* ret = NULL;
        hres = CoCreateInstance(CLSID_ShellLink, NULL,
                                CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, (LPVOID*)&ret);
        if (SUCCEEDED(hres))
        {
            std::wstring pathName;
            if (gPathService == NULL || !gPathService->GetModuleFileName(NULL, pathName).success)
            {
                ret->Release();
                return E_FAIL;
            }

            // Set path, parameters, icon and description.
            ret->SetPath(pathName.c_str());
            ret->SetArguments(params.c_str());
            wchar_t desc[MAX_PATH]; // kept as wchar_t[] - SetDescription API limits to MAX_PATH+1
            lstrcpynW(desc, path, _countof(desc));
            if (wcslen(path) >= _countof(desc))
                wcscpy(desc + _countof(desc) - 4, L"..."); // indicates the path has been truncated
            ret->SetDescription(desc);                     // MAX_PATH+1 is the limit (at least on Windows 7 where I'm testing now); longer = the jump list won't show at all
            ret->SetIconLocation(L"shell32.dll", -319);    // this icon exists from Windows XP onwards

            // To set the link title, we require the property store of the link.
            IPropertyStore* pPS;
            hres = ret->QueryInterface(IID_PPV_ARGS(&pPS));
            if (SUCCEEDED(hres))
            {
                PROPVARIANT pv;
                PropVariantInit(&pv);
                pv.vt = VT_LPWSTR;
                pv.pwszVal = (LPWSTR)name;
                pPS->SetValue(PKEY_Title, pv);
                pPS->Commit();
                pPS->Release();
            }
            else
                TRACE_E("CreateShellLink: QueryInterface(IPropertyStore) failed!");
        }
        else
            TRACE_E("CreateShellLink: CoCreateInstance(CLSID_ShellLink) failed!");

        *psl = ret;
        return hres;
    }
    else
    {
        TRACE_E("CreateShellLink: too long hot-path!");
        *psl = NULL;
        return E_FAIL;
    }
}

// Builds the collection of task items and adds them to the Task section of the Jump List.  All tasks
// should be added to the canonical "Tasks" category by calling ICustomDestinationList::AddUserTasks.
HRESULT AddTasksToList(ICustomDestinationList* pcdl)
{
    IObjectCollection* poc;
    HRESULT hr = CoCreateInstance(CLSID_EnumerableObjectCollection, NULL, CLSCTX_INPROC, IID_PPV_ARGS(&poc));
    if (SUCCEEDED(hr))
    {
        IShellLinkW* psl;

        int count = 0;
        for (int i = 0; i < HOT_PATHS_COUNT; i++)
        {
            if (MainWindow->HotPaths.GetVisible(i))
            {
                // wide: GetNameW/GetPathW return the stored truth directly - the
                // narrow GetName/GetPath pair is documented LOSSY BY CONSTRUCTION for a Unicode
                // hot path (mainwnd.h) and this was the last un-migrated caller.
                const std::wstring& name = MainWindow->HotPaths.GetNameW(i);
                const std::wstring& path = MainWindow->HotPaths.GetPathW(i);
                if (!name.empty() && !path.empty())
                {
                    hr = CreateShellLink(path.c_str(), name.c_str(), &psl);
                    if (SUCCEEDED(hr))
                    {
                        hr = poc->AddObject(psl);
                        if (SUCCEEDED(hr))
                            count++;
                        else
                            TRACE_E("AddTasksToList: AddObject() failed!");
                        psl->Release();
                    }
                }
            }
        }

        //if (SUCCEEDED(hr))
        //{
        //    hr = CreateSeparatorLink(&psl);
        //    if (SUCCEEDED(hr))
        //    {
        //        hr = poc->AddObject(psl);
        //        psl->Release();
        //    }
        //}

        if (SUCCEEDED(hr) && count > 0)
        {
            IObjectArray* poa;
            hr = poc->QueryInterface(IID_PPV_ARGS(&poa));
            if (SUCCEEDED(hr))
            {
                // Add the tasks to the Jump List. Tasks always appear in the canonical "Tasks"
                // category that is displayed at the bottom of the Jump List, after all other
                // categories.
                hr = pcdl->AddUserTasks(poa);
                if (!SUCCEEDED(hr))
                    TRACE_E("AddTasksToList: AddUserTasks failed!");
                poa->Release();
            }
            else
                TRACE_E("AddTasksToList: QueryInterface(IObjectArray) failed!");
        }
        poc->Release();
    }
    else
        TRACE_E("AddTasksToList: CoCreateInstance(CLSID_EnumerableObjectCollection) failed!");
    return hr;
}

void CreateJumpList()
{
    ICustomDestinationList* pcdl;
    HRESULT hr = CoCreateInstance(
        CLSID_DestinationList,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pcdl));
    if (SUCCEEDED(hr))
    {
        // Use the process-default application ID for this Jump List.
        {
            UINT uMaxSlots;
            IObjectArray* poaRemoved;
            hr = pcdl->BeginList(
                &uMaxSlots,
                IID_PPV_ARGS(&poaRemoved));
            if (SUCCEEDED(hr))
            {
                hr = AddTasksToList(pcdl);
                if (SUCCEEDED(hr))
                {
                    hr = pcdl->CommitList();
                    if (!SUCCEEDED(hr))
                        TRACE_E("CreateJumpList: CommitList() failed!");
                }
                poaRemoved->Release();
            }
            else
                TRACE_E("CreateJumpList: BeginList() failed!");
        }
    }
    else
        TRACE_E("CreateJumpList: CoCreateInstance(CLSID_DestinationList) failed!");
}
