// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include "ieviewer.h"
#include "ieviewer.rh"
#include "ieviewer.rh2"
#include "lang\lang.rh"
#include "dbg.h"

#include "markdown.h"

#include <shlobj.h>

// plugin interface object, its methods are called from Salamander
CPluginInterface PluginInterface;
// CPluginInterface portion for the viewer
CPluginInterfaceForViewer InterfaceForViewer;

const wchar_t* WINDOW_CLASSNAME = L"Web Viewer Class";
ATOM AtomObject = 0;                                               // window "property" with a pointer to the object
CViewerWindowQueue CViewerMainWindow::ViewerWindowQueue;           // list of all viewer windows
CThreadQueue CViewerMainWindow::ThreadQueue("WebViewer Viewers");  // list of all window threads

HINSTANCE DLLInstance = NULL; // handle to the SPL module - language-independent resources
HINSTANCE HLanguage = NULL;   // handle to the SLG module - language-dependent resources

int ConfigVersion = 0;           // 0 - default, 1 - SS 1.6 beta 3, 2 - SS 1.6 beta 4, 3 - SS 2.5 beta 1, 4 - AS 3.1 beta 1
#define CURRENT_CONFIG_VERSION 4 // AS 3.1 beta 1
const char* CONFIG_VERSION = "Version";

// Salamander general interface - valid from startup until the plugin shuts down
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;

// variable definition for "dbg.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;

// variable definition for "spl_com.h"
int SalamanderVersion = 0;

//
// ****************************************************************************
// DllMain
//

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
        DLLInstance = hinstDLL;
    return TRUE; // DLL can be loaded
}

//
// ****************************************************************************
// LoadStr
//

char* LoadStr(int resID)
{
    return SalamanderGeneral->LoadStr(HLanguage, resID);
}

//
// ****************************************************************************
// SalamanderPluginGetReqVer
//

int WINAPI SalamanderPluginGetReqVer()
{
    return LAST_VERSION_OF_SALAMANDER;
}

//
// ****************************************************************************
// SalamanderPluginEntry
//

CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander)
{
    // configure SalamanderDebug for "dbg.h"
    SalamanderDebug = salamander->GetSalamanderDebug();
    // configure SalamanderVersion for "spl_com.h"
    SalamanderVersion = salamander->GetVersion();

    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

    // this plugin is built for the current Salamander version and newer - perform a check
    if (SalamanderVersion < LAST_VERSION_OF_SALAMANDER)
    { // reject older versions
        MessageBox(salamander->GetParentWindow(),
                   REQUIRE_LAST_VERSION_OF_SALAMANDER,
                   "Web Viewer" /* neprekladat! */, MB_OK | MB_ICONERROR);
        return NULL;
    }

    // let it load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), "Web Viewer" /* neprekladat! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain the general Salamander interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();

    if (!InitViewer())
        return NULL; // error

    // configure the basic plugin information
    salamander->SetBasicPluginData(LoadStr(IDS_PLUGINNAME),
                                   FUNCTION_LOADSAVECONFIGURATION | FUNCTION_VIEWER,
                                   VERSINFO_VERSION_NO_PLATFORM,
                                   VERSINFO_COPYRIGHT,
                                   LoadStr(IDS_PLUGIN_DESCRIPTION),
                                   "IEVIEWER");

    salamander->SetPluginHomePageURL("www.altap.cz");

    return &PluginInterface;
}

//
// ****************************************************************************
// CPluginInterface
//

void CPluginInterface::About(HWND parent)
{
    char buf[1000];
    _snprintf_s(buf, _TRUNCATE,
                "%s " VERSINFO_VERSION "\n\n" VERSINFO_COPYRIGHT "\n\n"
                "%s",
                LoadStr(IDS_PLUGINNAME),
                LoadStr(IDS_PLUGIN_DESCRIPTION));
    SalamanderGeneral->SalMessageBox(parent, buf, LoadStr(IDS_ABOUT), MB_OK | MB_ICONINFORMATION);
}

BOOL CPluginInterface::Release(HWND parent, BOOL force)
{
    CALL_STACK_MESSAGE2("CPluginInterface::Release(, %d)", force);
    BOOL ret = CViewerMainWindow::ViewerWindowQueue.Empty();
    if (!ret)
    {
        ret = CViewerMainWindow::ViewerWindowQueue.CloseAllWindows(force) || force;
    }
    if (ret)
    {
        if (!CViewerMainWindow::ThreadQueue.KillAll(force) && !force)
            ret = FALSE;
        else
            ReleaseViewer();
    }
    return ret;
}

void CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::LoadConfiguration(, ,)");
    if (regKey != NULL) // load from the registry
    {
        if (!registry->GetValue(regKey, CONFIG_VERSION, REG_DWORD, &ConfigVersion, sizeof(DWORD)))
        {
            ConfigVersion = CURRENT_CONFIG_VERSION;
        }
    }
    else // default configuration
    {
        ConfigVersion = 0;
    }
}

void CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    DWORD v = CURRENT_CONFIG_VERSION;
    registry->SetValue(regKey, CONFIG_VERSION, REG_DWORD, &v, sizeof(DWORD));
}

const char* MARKDOWN_EXTENSIONS = "*.md;*.mdown;*.markdown";

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    char buff[1000];
    sprintf_s(buff, "*.htm;*.html;*.xml;*.mht;%s", MARKDOWN_EXTENSIONS);

    salamander->AddViewer(buff, FALSE);

    if (ConfigVersion < 2) // before SS 1.6 beta 4
    {
        salamander->AddViewer("*.xml", TRUE);
        salamander->ForceRemoveViewer("*.jpg");
        salamander->ForceRemoveViewer("*.gif");
    }

    if (ConfigVersion < 3) // before SS 2.5 beta 1
    {
        salamander->AddViewer("*.mht", TRUE);
    }

    if (ConfigVersion < 4) // before AD 3.1 beta 1
    {
        salamander->AddViewer(MARKDOWN_EXTENSIONS, TRUE);
    }
}

CPluginInterfaceForViewerAbstract*
CPluginInterface::GetInterfaceForViewer()
{
    return &InterfaceForViewer;
}

//***********************************************************************************
//
// CWebView2Host
//

// Get the user data folder for WebView2 cache
static std::wstring GetWebView2UserDataFolder()
{
    wchar_t* appDataPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &appDataPath)))
    {
        std::wstring path(appDataPath);
        CoTaskMemFree(appDataPath);
        path += L"\\Salamander\\WebView2";
        return path;
    }
    return L"";
}

bool CWebView2Host::Create(HWND hwndParent)
{
    CALL_STACK_MESSAGE1("CWebView2Host::Create()");
    m_hwndParent = hwndParent;

    // WebView2 requires COM initialized on this thread (STA)
    if (FAILED(OleInitialize(NULL)))
    {
        TRACE_E("OleInitialize failed");
        return false;
    }

    std::wstring userDataFolder = GetWebView2UserDataFolder();
    BOOL ready = FALSE;
    HRESULT initResult = S_OK;

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, // browserExecutableFolder (use installed Edge)
        userDataFolder.empty() ? nullptr : userDataFolder.c_str(),
        nullptr, // options
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, &ready, &initResult](HRESULT result, ICoreWebView2Environment* env) -> HRESULT
            {
                if (FAILED(result) || env == nullptr)
                {
                    TRACE_E("CreateCoreWebView2Environment failed");
                    initResult = result;
                    ready = TRUE;
                    return result;
                }

                m_environment = env;

                env->CreateCoreWebView2Controller(
                    m_hwndParent,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [this, &ready, &initResult](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT
                        {
                            if (FAILED(result) || controller == nullptr)
                            {
                                TRACE_E("CreateCoreWebView2Controller failed");
                                initResult = result;
                                ready = TRUE;
                                return result;
                            }

                            m_controller = controller;
                            controller->get_CoreWebView2(&m_webview);

                            // Make the controller visible
                            m_controller->put_IsVisible(TRUE);

                            // Fit the WebView to the parent window
                            RECT bounds;
                            GetClientRect(m_hwndParent, &bounds);
                            m_controller->put_Bounds(bounds);

                            // Configure settings
                            ComPtr<ICoreWebView2Settings> settings;
                            m_webview->get_Settings(&settings);
                            if (settings)
                            {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_IsZoomControlEnabled(TRUE);
                            }

                            ready = TRUE;
                            return S_OK;
                        })
                        .Get());

                return S_OK;
            })
            .Get());

    if (FAILED(hr))
    {
        TRACE_E("CreateCoreWebView2EnvironmentWithOptions failed");
        return false;
    }

    // Pump messages until WebView2 initialization completes
    while (!ready)
    {
        DWORD dwResult = MsgWaitForMultipleObjectsEx(0, nullptr, 1000, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (dwResult == WAIT_OBJECT_0)
        {
            MSG msg;
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

    if (FAILED(initResult) || m_webview == nullptr)
        return false;

    // Register accelerator key handler — WebView2 swallows keyboard input,
    // so we must use this event to intercept Escape, Ctrl+R, etc.
    EventRegistrationToken accelToken;
    m_controller->add_AcceleratorKeyPressed(
        Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
            [this](ICoreWebView2Controller* sender,
                   ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT
            {
                COREWEBVIEW2_KEY_EVENT_KIND kind;
                args->get_KeyEventKind(&kind);
                if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN &&
                    kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN)
                    return S_OK;

                UINT key;
                args->get_VirtualKey(&key);

                if (key == VK_ESCAPE)
                {
                    args->put_Handled(TRUE);
                    PostMessage(m_hwndParent, WM_CLOSE, 0, 0);
                    return S_OK;
                }

                if (key == 'R' && (GetKeyState(VK_CONTROL) & 0x8000) != 0)
                {
                    args->put_Handled(TRUE);
                    // Post a custom message to the parent to trigger refresh
                    // (avoid doing heavy work inside the callback)
                    PostMessage(m_hwndParent, WM_APP + 1, 0, 0);
                    return S_OK;
                }

                return S_OK;
            })
            .Get(),
        &accelToken);

    return true;
}

static void SafeOleUninitialize()
{
    __try
    {
        OleUninitialize();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void CWebView2Host::Close()
{
    CALL_STACK_MESSAGE1("CWebView2Host::Close()");
    m_webview = nullptr;
    if (m_controller)
    {
        m_controller->Close();
        m_controller = nullptr;
    }
    m_environment = nullptr;

    // Match the OleInitialize in Create()
    SafeOleUninitialize();
}

void CWebView2Host::Navigate(const std::wstring& url)
{
    if (m_webview)
        m_webview->Navigate(url.c_str());
}

void CWebView2Host::NavigateToString(const std::string& htmlContent)
{
    if (m_webview)
    {
        // WebView2::NavigateToString expects a wide string
        int len = MultiByteToWideChar(CP_UTF8, 0, htmlContent.c_str(), (int)htmlContent.size(), nullptr, 0);
        std::wstring wide(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, htmlContent.c_str(), (int)htmlContent.size(), wide.data(), len);
        m_webview->NavigateToString(wide.c_str());
    }
}

void CWebView2Host::Resize(int width, int height)
{
    if (m_controller)
    {
        RECT bounds = {0, 0, width, height};
        m_controller->put_Bounds(bounds);
    }
}

//***********************************************************************************
//
// Thread message loop and ViewFile
//

struct CTVData
{
    BOOL AlwaysOnTop;
    std::wstring Name;
    std::string HtmlContent; // Non-empty for Markdown (pre-rendered HTML)
    int Left, Top, Width, Height;
    UINT ShowCmd;
    BOOL ReturnLock;
    HANDLE* Lock;
    BOOL* LockOwner;
    BOOL Success;
    HANDLE Continue;
};

// Convert ANSI path to wide string
static std::wstring AnsiToWide(const char* ansi)
{
    int len = MultiByteToWideChar(CP_ACP, 0, ansi, -1, nullptr, 0);
    if (len <= 0)
        return {};
    std::wstring wide(len - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, ansi, -1, wide.data(), len);
    return wide;
}

// Convert a local file path to a file:// URL
static std::wstring PathToFileUrl(const std::wstring& path)
{
    std::wstring url = L"file:///";
    for (wchar_t c : path)
    {
        if (c == L'\\')
            url += L'/';
        else
            url += c;
    }
    return url;
}

// Build a title string from the file path
static std::wstring MakeWindowTitle(const std::wstring& filePath, const char* pluginName)
{
    std::wstring title = filePath;
    title += L" - ";
    // Convert plugin name to wide
    int len = MultiByteToWideChar(CP_ACP, 0, pluginName, -1, nullptr, 0);
    if (len > 0)
    {
        std::wstring wideName(len - 1, L'\0');
        MultiByteToWideChar(CP_ACP, 0, pluginName, -1, wideName.data(), len);
        title += wideName;
    }
    return title;
}

unsigned WINAPI ThreadViewerMessageLoop(void* param)
{
    CALL_STACK_MESSAGE1("ThreadViewerMessageLoop(Version 2.00)");
    SetThreadNameInVCAndTrace("WebViewLoop");
    TRACE_I("Begin");

    CTVData* data = (CTVData*)param;

    CViewerMainWindow* window = new CViewerMainWindow;
    if (window != NULL)
    {
        if (data->ReturnLock)
        {
            *data->Lock = window->GetLock();
            *data->LockOwner = TRUE;
        }
        CALL_STACK_MESSAGE1("ThreadViewerMessageLoop::CreateWindowEx");
        if ((!data->ReturnLock || *data->Lock != NULL) &&
            CreateWindowExW(data->AlwaysOnTop ? WS_EX_TOPMOST : 0,
                            WINDOW_CLASSNAME,
                            MakeWindowTitle(data->Name, LoadStr(IDS_PLUGINNAME)).c_str(),
                            WS_OVERLAPPEDWINDOW,
                            data->Left,
                            data->Top,
                            data->Width,
                            data->Height,
                            NULL,
                            NULL,
                            DLLInstance,
                            window) != NULL)
        {
            SendMessage(window->HWindow, WM_SETICON, ICON_BIG,
                        (LPARAM)LoadIcon(DLLInstance, MAKEINTRESOURCE(IDI_IEVIEWER)));
            SendMessage(window->HWindow, WM_SETICON, ICON_SMALL,
                        (LPARAM)LoadIcon(DLLInstance, MAKEINTRESOURCE(IDI_IEVIEWER)));
            CALL_STACK_MESSAGE1("ThreadViewerMessageLoop::ShowWindow");
            ShowWindow(window->HWindow, data->ShowCmd);
            SetForegroundWindow(window->HWindow);
            UpdateWindow(window->HWindow);

            // Re-apply WebView2 bounds now that the window is shown and sized
            RECT rc;
            GetClientRect(window->HWindow, &rc);
            window->m_viewer.Resize(rc.right - rc.left, rc.bottom - rc.top);

            data->Success = TRUE;
        }
        else
        {
            CALL_STACK_MESSAGE1("ThreadViewerMessageLoop::delete-window");
            if (data->ReturnLock && *data->Lock != NULL)
                CloseHandle(*data->Lock);
            delete window;
            window = NULL;
        }
    }

    CALL_STACK_MESSAGE1("ThreadViewerMessageLoop::SetEvent");
    std::wstring name = data->Name;
    std::string htmlContent = std::move(data->HtmlContent);
    BOOL openFile = data->Success;
    SetEvent(data->Continue); // let the main thread continue; data are invalid from this point
    data = NULL;

    // if everything succeeded, open the requested file in the viewer
    if (window != NULL && openFile)
    {
        CALL_STACK_MESSAGE1("ThreadViewerMessageLoop::Navigate");
        if (!htmlContent.empty())
        {
            window->m_viewer.SetMarkdownPath(name);
            window->m_viewer.SetLastHtml(htmlContent);
            window->m_viewer.NavigateToString(htmlContent);
        }
        else
        {
            window->m_viewer.Navigate(PathToFileUrl(name));
        }

        CALL_STACK_MESSAGE1("ThreadViewerMessageLoop::message-loop");
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    CALL_STACK_MESSAGE1("ThreadViewerMessageLoop::message_loop done");
    delete window;

    TRACE_I("End");
    return 0;
}

enum FileFormatEnum
{
    ffeHTML,
    ffeMarkdown
};

FileFormatEnum GetFileFormat(const char* name)
{
    FileFormatEnum ret = ffeHTML;
    CSalamanderMaskGroup* masks = SalamanderGeneral->AllocSalamanderMaskGroup();
    if (masks != NULL)
    {
        masks->SetMasksString(MARKDOWN_EXTENSIONS, FALSE);
        int err;
        if (masks->PrepareMasks(err) && masks->AgreeMasks(name, NULL))
            ret = ffeMarkdown;
        SalamanderGeneral->FreeSalamanderMaskGroup(masks);
    }
    return ret;
}

BOOL CPluginInterfaceForViewer::ViewFile(const char* name, int left, int top, int width,
                                         int height, UINT showCmd, BOOL alwaysOnTop,
                                         BOOL returnLock, HANDLE* lock, BOOL* lockOwner,
                                         CSalamanderPluginViewerData* viewerData,
                                         int enumFilesSourceUID, int enumFilesCurrentIndex)
{
    CALL_STACK_MESSAGE11("CPluginInterfaceForViewer::ViewFile(%s, %d, %d, %d, %d, "
                         "0x%X, %d, %d, , , , %d, %d)",
                         name, left, top, width, height,
                         showCmd, alwaysOnTop, returnLock, enumFilesSourceUID, enumFilesCurrentIndex);

    FileFormatEnum fileFormat = GetFileFormat(name);
    std::wstring wideName = AnsiToWide(name);

    CTVData data;
    data.AlwaysOnTop = alwaysOnTop;
    data.Name = wideName;
    if (fileFormat == ffeMarkdown)
        data.HtmlContent = ConvertMarkdownToHTML(wideName);
    data.Left = left;
    data.Top = top;
    data.Width = width;
    data.Height = height;
    data.ShowCmd = showCmd;
    data.ReturnLock = returnLock;
    data.Lock = lock;
    data.LockOwner = lockOwner;
    data.Success = FALSE;
    data.Continue = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (data.Continue == NULL)
    {
        TRACE_E("Failed to create the Continue event.");
        return FALSE;
    }

    if (CViewerMainWindow::ThreadQueue.StartThread(ThreadViewerMessageLoop, &data))
    {
        // wait until the thread processes the passed data and returns results
        WaitForSingleObject(data.Continue, INFINITE);
    }
    else
        data.Success = FALSE;
    CloseHandle(data.Continue);

    if (!data.Success)
    {
        SalamanderGeneral->SalMessageBox(NULL, LoadStr(IDS_UNABLETOOPENIE), LoadStr(IDS_ERRORTITLE),
                                         MB_ICONEXCLAMATION | MB_OK | MB_SETFOREGROUND);
    }

    return data.Success;
}

//
// ****************************************************************************
// InitViewer & ReleaseViewer
//

BOOL InitViewer()
{
    CALL_STACK_MESSAGE1("InitViewer()");
    AtomObject = GlobalAddAtomW(L"object handle");
    if (AtomObject == 0)
    {
        TRACE_E("GlobalAddAtom has failed");
        return FALSE;
    }

    WNDCLASSW wc = {};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = CViewerMainWindow::ViewerMainWindowProc;
    wc.hInstance = DLLInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = WINDOW_CLASSNAME;
    if (RegisterClassW(&wc) == 0)
    {
        TRACE_E("RegisterClass has failed");
        return FALSE;
    }
    return TRUE;
}

void ReleaseViewer()
{
    CALL_STACK_MESSAGE1("ReleaseViewer()");
    if (AtomObject != 0)
        GlobalDeleteAtom(AtomObject);
    if (!UnregisterClassW(WINDOW_CLASSNAME, DLLInstance))
        TRACE_E("UnregisterClass(WINDOW_CLASSNAME) has failed");
}

//
// ****************************************************************************
// CViewerWindowQueue
//

CViewerWindowQueue::~CViewerWindowQueue()
{
    if (!Empty())
        TRACE_E("A viewer window remained open!");
    CViewerWindowQueueItem* last;
    CViewerWindowQueueItem* item = Head;
    while (item != NULL)
    {
        last = item;
        item = item->Next;
        delete last;
    }
}

BOOL CViewerWindowQueue::Add(CViewerWindowQueueItem* item)
{
    CALL_STACK_MESSAGE1("CViewerWindowQueue::Add()");
    CS.Enter();
    if (item != NULL)
    {
        item->Next = Head;
        Head = item;
        CS.Leave();
        return TRUE;
    }
    CS.Leave();
    return FALSE;
}

void CViewerWindowQueue::Remove(HWND hWindow)
{
    CALL_STACK_MESSAGE1("CViewerWindowQueue::Remove()");
    CS.Enter();
    CViewerWindowQueueItem* last = NULL;
    CViewerWindowQueueItem* item = Head;
    while (item != NULL)
    {
        if (item->HWindow == hWindow) // found, remove it
        {
            if (last != NULL)
                last->Next = item->Next;
            else
                Head = item->Next;
            delete item;
            CS.Leave();
            return;
        }
        last = item;
        item = item->Next;
    }
    CS.Leave();
}

BOOL CViewerWindowQueue::Empty()
{
    BOOL e;
    CS.Enter();
    e = Head == NULL;
    CS.Leave();
    return e;
}

BOOL CViewerWindowQueue::CloseAllWindows(BOOL force, int waitTime, int forceWaitTime)
{
    CALL_STACK_MESSAGE4("CViewerWindowQueue::CloseAllWindows(%d, %d, %d)", force, waitTime, forceWaitTime);
    CS.Enter();
    CViewerWindowQueueItem* item = Head;
    while (item != NULL)
    {
        PostMessage(item->HWindow, WM_CLOSE, 0, 0);
        item = item->Next;
    }
    CS.Leave();

    DWORD ti = GetTickCount();
    DWORD w = force ? forceWaitTime : waitTime;
    while ((w == INFINITE || w > 0) && !Empty())
    {
        DWORD t = GetTickCount() - ti;
        if (w == INFINITE || t < w)
        {
            if (w == INFINITE || 50 < w - t)
                Sleep(50);
            else
            {
                Sleep(w - t);
                break;
            }
        }
        else
            break;
    }
    return force || Empty();
}

//
// ****************************************************************************
// CViewerMainWindow
//

CViewerMainWindow::CViewerMainWindow()
{
    HWindow = NULL;
    Lock = NULL;
}

LRESULT
CViewerMainWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CViewerMainWindow::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_CREATE:
    {
        if (!m_viewer.Create(HWindow))
            return -1;
        return 0;
    }

    case WM_DESTROY:
    {
        TRACE_I("CViewerMainWindow::WindowProc WM_DESTROY");
        if (Lock != NULL)
        {
            SetEvent(Lock);
            Lock = NULL;
        }
        TRACE_I("CViewerMainWindow::WindowProc m_viewer.Close()");
        m_viewer.Close();
        TRACE_I("CViewerMainWindow::WindowProc PostQuitMessage");
        PostQuitMessage(0);
        break;
    }

    case WM_SETFOCUS:
    {
        // WebView2 manages its own focus; just tell the controller
        if (m_viewer.GetController())
            m_viewer.GetController()->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        return 0;
    }

    case WM_ACTIVATE:
    {
        if (!LOWORD(wParam))
        {
            SalamanderGeneral->SkipOneActivateRefresh();
        }
        break;
    }

    case WM_SIZE:
    {
        m_viewer.Resize(LOWORD(lParam), HIWORD(lParam));
        break;
    }

    case WM_APP + 1: // Ctrl+R refresh (posted by AcceleratorKeyPressed handler)
    {
        const std::wstring& mdPath = m_viewer.GetMarkdownPath();
        if (!mdPath.empty())
        {
            std::string html = ConvertMarkdownToHTML(mdPath);
            if (!html.empty())
            {
                m_viewer.SetLastHtml(html);
                m_viewer.NavigateToString(html);
            }
        }
        else if (m_viewer.GetWebView())
        {
            m_viewer.GetWebView()->Reload();
        }
        return 0;
    }

    }
    return DefWindowProcW(HWindow, uMsg, wParam, lParam);
}

HANDLE
CViewerMainWindow::GetLock()
{
    if (Lock == NULL)
        Lock = CreateEvent(NULL, FALSE, FALSE, NULL);
    return Lock;
}

// ****************************************************************************
// static function CViewerMainWindow::ViewerMainWindowProc for all messages of all viewer
// windows, distributes messages to individual viewer windows

LRESULT CALLBACK
CViewerMainWindow::ViewerMainWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE5("CViewerMainWindow::ViewerMainWindowProc(0x%p, 0x%X, 0x%IX, 0x%IX)", hwnd, uMsg, wParam, lParam);
    CViewerMainWindow* wnd;
    switch (uMsg)
    {
    case WM_CREATE: // first message - attach the object to the window
    {
        wnd = (CViewerMainWindow*)((CREATESTRUCT*)lParam)->lpCreateParams;
        if (wnd == NULL)
        {
            TRACE_E("Error while creating the window.");
            return FALSE;
        }
        else
        {
            wnd->HWindow = hwnd;
            SetPropW(hwnd, (LPCWSTR)(ULONG_PTR)AtomObject, (HANDLE)wnd);
            ViewerWindowQueue.Add(new CViewerWindowQueueItem(wnd->HWindow));
        }
        break;
    }

    case WM_DESTROY: // last message - detach the object from the window
    {
        wnd = (CViewerMainWindow*)GetPropW(hwnd, (LPCWSTR)(ULONG_PTR)AtomObject);
        if (wnd != NULL)
        {
            LRESULT res = wnd->WindowProc(uMsg, wParam, lParam);
            ViewerWindowQueue.Remove(hwnd);
            RemovePropW(hwnd, (LPCWSTR)(ULONG_PTR)AtomObject);
            if (res == 0)
                return 0;
            wnd = NULL;
        }
        break;
    }

    default:
    {
        wnd = (CViewerMainWindow*)GetPropW(hwnd, (LPCWSTR)(ULONG_PTR)AtomObject);
    }
    }
    if (wnd != NULL)
        return wnd->WindowProc(uMsg, wParam, lParam);
    else
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
