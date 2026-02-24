// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

#include <string>
#include <wrl.h>
#include <WebView2.h>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;

BOOL InitViewer();
void ReleaseViewer();

extern HINSTANCE DLLInstance;
extern HINSTANCE HLanguage;
extern CSalamanderGeneralAbstract* SalamanderGeneral;

//
// ****************************************************************************
// CPluginInterface
//

class CPluginInterfaceForViewer : public CPluginInterfaceForViewerAbstract
{
public:
    virtual BOOL WINAPI ViewFile(const char* name, int left, int top, int width, int height,
                                 UINT showCmd, BOOL alwaysOnTop, BOOL returnLock, HANDLE* lock,
                                 BOOL* lockOwner, CSalamanderPluginViewerData* viewerData,
                                 int enumFilesSourceUID, int enumFilesCurrentIndex);
    virtual BOOL WINAPI CanViewFile(const char* name) { return TRUE; }
};

class CPluginInterface : public CPluginInterfaceAbstract
{
public:
    virtual void WINAPI About(HWND parent);

    virtual BOOL WINAPI Release(HWND parent, BOOL force);

    virtual void WINAPI LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry);
    virtual void WINAPI SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry);
    virtual void WINAPI Configuration(HWND parent) {}

    virtual void WINAPI Connect(HWND parent, CSalamanderConnectAbstract* salamander);

    virtual void WINAPI ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData) { return; }

    virtual CPluginInterfaceForArchiverAbstract* WINAPI GetInterfaceForArchiver() { return NULL; }
    virtual CPluginInterfaceForViewerAbstract* WINAPI GetInterfaceForViewer();
    virtual CPluginInterfaceForMenuExtAbstract* WINAPI GetInterfaceForMenuExt() { return NULL; }
    virtual CPluginInterfaceForFSAbstract* WINAPI GetInterfaceForFS() { return NULL; }
    virtual CPluginInterfaceForThumbLoaderAbstract* WINAPI GetInterfaceForThumbLoader() { return NULL; }

    virtual void WINAPI Event(int event, DWORD param) {}
    virtual void WINAPI ClearHistory(HWND parent) {}
    virtual void WINAPI AcceptChangeOnPathNotification(const char* path, BOOL includingSubdirs) {}

    virtual void WINAPI PasswordManagerEvent(HWND parent, int event) {}
};

//***********************************************************************************
//
// CWebView2Host
//
// Wraps a WebView2 browser control in a parent window.
//

class CWebView2Host
{
    ComPtr<ICoreWebView2Environment> m_environment;
    ComPtr<ICoreWebView2Controller> m_controller;
    ComPtr<ICoreWebView2> m_webview;
    HWND m_hwndParent = nullptr;

    // For Ctrl+R refresh of markdown files
    std::wstring m_markdownPath;
    std::string m_lastHtml;

public:
    // Synchronously creates WebView2 (pumps messages until ready).
    // Returns true on success.
    bool Create(HWND hwndParent);

    // Releases all COM objects and closes the controller.
    void Close();

    // Navigate to a file:// URL.
    void Navigate(const std::wstring& url);

    // Navigate to in-memory HTML content (for Markdown).
    void NavigateToString(const std::string& htmlContent);

    // Resize the WebView2 control to fill the parent.
    void Resize(int width, int height);

    // Store markdown path for Ctrl+R refresh.
    void SetMarkdownPath(const std::wstring& path) { m_markdownPath = path; }
    const std::wstring& GetMarkdownPath() const { return m_markdownPath; }

    // Store the last HTML for refresh.
    void SetLastHtml(const std::string& html) { m_lastHtml = html; }
    const std::string& GetLastHtml() const { return m_lastHtml; }

    // Get the underlying webview for event hookup.
    ICoreWebView2* GetWebView() const { return m_webview.Get(); }
    ICoreWebView2Controller* GetController() const { return m_controller.Get(); }

    bool IsReady() const { return m_webview != nullptr; }
};

//
// ****************************************************************************
// CViewerMainWindow
//

struct CViewerWindowQueueItem
{
    HWND HWindow;
    CViewerWindowQueueItem* Next;

    CViewerWindowQueueItem(HWND hWindow)
    {
        HWindow = hWindow;
        Next = NULL;
    }
};

class CViewerWindowQueue
{
protected:
    CViewerWindowQueueItem* Head;

    struct CCS // access from multiple threads -> synchronization required
    {
        CRITICAL_SECTION cs;

        CCS() { InitializeCriticalSection(&cs); }
        ~CCS() { DeleteCriticalSection(&cs); }

        void Enter() { EnterCriticalSection(&cs); }
        void Leave() { LeaveCriticalSection(&cs); }
    } CS;

public:
    CViewerWindowQueue() { Head = NULL; }
    ~CViewerWindowQueue();

    BOOL Add(CViewerWindowQueueItem* item); // add an item to the queue, returns success
    void Remove(HWND hWindow);              // remove an item from the queue
    BOOL Empty();                           // returns TRUE if the queue is empty

    // broadcast WM_CLOSE, then wait for an empty queue
    BOOL CloseAllWindows(BOOL force, int waitTime = 1000, int forceWaitTime = 0);
};

class CViewerMainWindow
{
public:
    HWND HWindow;                                    // viewer window handle
    HANDLE Lock;                                     // 'lock' object or NULL
    static CViewerWindowQueue ViewerWindowQueue;     // list of all viewer windows
    static CThreadQueue ThreadQueue;                 // list of all window threads

    CWebView2Host m_viewer;

public:
    CViewerMainWindow();

    HANDLE GetLock();

    static LRESULT CALLBACK ViewerMainWindowProc(HWND hwnd, UINT uMsg,
                                                 WPARAM wParam, LPARAM lParam);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};
