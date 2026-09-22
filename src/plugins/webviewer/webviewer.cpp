// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "webviewer.h"
#include "webviewer.rh"
#include "webviewer.rh2"
#include "lang\lang.rh"
#include "dbg.h"

#include "markdown.h"
#include "webviewer_text.h"

#include <shlobj.h>
#include <stdarg.h>

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

int ConfigVersion = 0;           // 0 - default, 1 - SS 1.6 beta 3, 2 - SS 1.6 beta 4, 3 - SS 2.5 beta 1, 4 - AS 3.1 beta 1, 5 - Sally (PNG/SVG)
#define CURRENT_CONFIG_VERSION 5 // Sally: PNG/SVG viewer support
const wchar_t* CONFIG_VERSION = L"Version";

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
        // wide: same call-site-local widen shape used throughout this backlog
        // (205-226).
#define WEBVIEWER_WIDEN2(x) L##x
#define WEBVIEWER_WIDEN(x) WEBVIEWER_WIDEN2(x)
        MessageBoxW(salamander->GetParentWindow(),
                    WEBVIEWER_WIDEN(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    L"Web Viewer" /* neprekladat! */, MB_OK | MB_ICONERROR);
#undef WEBVIEWER_WIDEN
#undef WEBVIEWER_WIDEN2
        return NULL;
    }

    // let it load the language module (.slg)
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), L"Web Viewer" /* neprekladat! */);
    if (HLanguage == NULL)
        return NULL;

    // obtain the general Salamander interface
    SalamanderGeneral = salamander->GetSalamanderGeneral();

    if (!InitViewer())
        return NULL; // error

    // configure the basic plugin information
    salamander->SetBasicPluginData(SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(),
                                   FUNCTION_LOADSAVECONFIGURATION | FUNCTION_VIEWER,
                                   _CRT_WIDE(VERSINFO_VERSION_NO_PLATFORM),
                                   _CRT_WIDE(VERSINFO_COPYRIGHT),
                                   SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGIN_DESCRIPTION).c_str(),
                                   L"WEBVIEWER");

    salamander->SetPluginHomePageURL(L"https://github.com/0xeb/sally");

    return &PluginInterface;
}

//
// ****************************************************************************
// CPluginInterface
//

void CPluginInterface::About(HWND parent)
{
    const std::wstring text = SPLFormatStringOwned(
        L"%ls %ls\n\n%ls\n\n%ls",
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str(),
        _CRT_WIDE(VERSINFO_VERSION), _CRT_WIDE(VERSINFO_COPYRIGHT),
        SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGIN_DESCRIPTION).c_str());
    SalamanderGeneral->SalMessageBox(parent, text.c_str(),
                                     SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ABOUT).c_str(),
                                     MB_OK | MB_ICONINFORMATION);
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

const wchar_t* MARKDOWN_EXTENSIONS = L"*.md;*.mdown;*.markdown";
const wchar_t* IMAGE_EXTENSIONS = L"*.png;*.svg";

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    std::wstring extensions = L"*.htm;*.html;*.xml;*.mht;";
    extensions += MARKDOWN_EXTENSIONS;
    extensions += L';';
    extensions += IMAGE_EXTENSIONS;
    salamander->AddViewer(extensions.c_str(), FALSE);

    if (ConfigVersion < 2) // before SS 1.6 beta 4
    {
        salamander->AddViewer(L"*.xml", TRUE);
        salamander->ForceRemoveViewer(L"*.jpg");
        salamander->ForceRemoveViewer(L"*.gif");
    }

    if (ConfigVersion < 3) // before SS 2.5 beta 1
    {
        salamander->AddViewer(L"*.mht", TRUE);
    }

    if (ConfigVersion < 4) // before AD 3.1 beta 1
    {
        salamander->AddViewer(MARKDOWN_EXTENSIONS, TRUE);
    }

    if (ConfigVersion < 5) // Sally: add PNG/SVG viewer support
    {
        salamander->AddViewer(IMAGE_EXTENSIONS, TRUE);
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

// ---------------------------------------------------------------------------------------
// DPI diagnostics for the "markdown renders at half size" report.
//
// Sally is SYSTEM-DPI aware (src/manifest.xml: <dpiAware>true</dpiAware>) and explicitly does
// not support per-monitor DPI, while the WebView2 runtime IS per-monitor aware and paints in
// physical pixels. If the monitor's scale differs from the system DPI captured at logon, the
// bounds we hand to put_Bounds and the scale WebView2 renders at disagree, and the page ends
// up covering the wrong fraction of the window (measured: exactly half at a 2x mismatch).
//
// This records the numbers so a recurrence explains itself instead of costing another round of
// screenshots. Written to %TEMP%\sally-webviewer-dpi.log.
// Debug builds only.
//
// This was unconditional: every viewer open and every resize appended to a log file in %TEMP%,
// on every user machine, forever. It exists to diagnose the half-size rendering caused by the
// system-DPI vs per-monitor-DPI mismatch, which is a development concern - a shipped build
// should not be quietly writing diagnostics to disk.
//
// Emptying the function body is NOT enough: the format strings are call ARGUMENTS, so they
// are still emitted into .rdata and shipped. Measured in the Release DLL - "DPI FIX APPLIED",
// "resize IGNORED" and the rest were all present with the body already compiled out. So the
// call sites themselves have to disappear, which needs a macro, not an empty function.
#ifdef _DEBUG
static void WebViewerDiagLogImpl(const char* fmt, ...)
{
    const DWORD required = GetTempPathW(0, NULL);
    if (required == 0)
        return;
    std::vector<wchar_t> storage(required, L'\0');
    const DWORD length = GetTempPathW(required, storage.data());
    if (length == 0 || length >= required)
        return;
    std::wstring path(storage.data(), length);
    SPLSalPathAppendOwned(path, L"sally-webviewer-dpi.log");
    FILE* f = NULL;
    if (_wfopen_s(&f, path.c_str(), L"a") != 0 || f == NULL)
        return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d  ", st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond);
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fputc(10, f); // newline
    fclose(f);
}
#define WebViewerDiagLog(...) WebViewerDiagLogImpl(__VA_ARGS__)
#else
// Release: the whole call vanishes, arguments and literals included.
#define WebViewerDiagLog(...) ((void)0)
#endif // _DEBUG

// Opt-in candidate fix: pin the controller's rasterization scale to the host's DPI so both
// sides use the same coordinate space. Enabled with SALLY_WEBVIEWER_DPI_FIX=1 so it can be
// tested against the unfixed behaviour on the machine that reproduces the bug, rather than
// shipped on a hypothesis.
static BOOL WebViewerDpiFixEnabled()
{
    DWORD capacity = GetEnvironmentVariableW(L"SALLY_WEBVIEWER_DPI_FIX", nullptr, 0);
    while (capacity != 0)
    {
        std::wstring value(capacity, L'\0');
        const DWORD length = GetEnvironmentVariableW(L"SALLY_WEBVIEWER_DPI_FIX",
                                                     value.data(), capacity);
        if (length == 0)
            return FALSE;
        if (length < capacity)
            return value[0] == L'1';
        capacity = length + 1;
    }
    return FALSE;
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

                            // --- DPI diagnostics + opt-in rasterization-scale fix ---
                            {
                                HDC hScreen = GetDC(NULL);
                                int systemDpi = hScreen != NULL ? GetDeviceCaps(hScreen, LOGPIXELSX) : 0;
                                if (hScreen != NULL)
                                    ReleaseDC(NULL, hScreen);

                                // GetDpiForWindow is Win10 1607+; resolve dynamically so this
                                // still builds and runs on older targets.
                                UINT windowDpi = 0;
                                HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
                                if (hUser32 != NULL)
                                {
                                    typedef UINT(WINAPI * PFNGETDPIFORWINDOW)(HWND);
                                    PFNGETDPIFORWINDOW pGetDpiForWindow =
                                        (PFNGETDPIFORWINDOW)GetProcAddress(hUser32, "GetDpiForWindow");
                                    if (pGetDpiForWindow != NULL)
                                        windowDpi = pGetDpiForWindow(m_hwndParent);
                                }

                                double rasterScale = 0.0;
                                ComPtr<ICoreWebView2Controller3> controller3;
                                if (SUCCEEDED(m_controller->QueryInterface(IID_ICoreWebView2Controller3,
                                                                           &controller3)) &&
                                    controller3 != nullptr)
                                {
                                    controller3->get_RasterizationScale(&rasterScale);

                                    if (WebViewerDpiFixEnabled() && windowDpi != 0)
                                    {
                                        // Make WebView2 use the SAME scale the host lays out in.
                                        const double hostScale = (double)windowDpi / 96.0;
                                        controller3->put_ShouldDetectMonitorScaleChanges(FALSE);
                                        controller3->put_RasterizationScale(hostScale);
                                        WebViewerDiagLog("DPI FIX APPLIED: rasterizationScale %.3f -> %.3f",
                                                         rasterScale, hostScale);
                                        controller3->get_RasterizationScale(&rasterScale);
                                    }
                                }

                                WebViewerDiagLog("viewer open: clientRect=%ldx%ld systemDpi=%d "
                                                 "windowDpi=%u rasterizationScale=%.3f fix=%d",
                                                 bounds.right - bounds.left, bounds.bottom - bounds.top,
                                                 systemDpi, windowDpi, rasterScale,
                                                 WebViewerDpiFixEnabled() ? 1 : 0);
                            }

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
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
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
                    PostMessageW(m_hwndParent, WM_CLOSE, 0, 0);
                    return S_OK;
                }

                if (key == 'R' && (GetKeyState(VK_CONTROL) & 0x8000) != 0)
                {
                    args->put_Handled(TRUE);
                    // Post a custom message to the parent to trigger refresh
                    // (avoid doing heavy work inside the callback)
                    PostMessageW(m_hwndParent, WM_APP + 1, 0, 0);
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
        std::wstring wideHtml;
        if (!WebViewerDecodeHtmlUtf8(htmlContent, wideHtml))
        {
            TRACE_E("Invalid UTF-8 HTML content");
            return;
        }
        m_webview->NavigateToString(wideHtml.c_str());
    }
}

void CWebView2Host::Resize(int width, int height)
{
    if (m_controller)
    {
        RECT bounds = {0, 0, width, height};
        m_controller->put_Bounds(bounds);

        // Log the first few resizes: if the page still covers only part of the window AFTER a
        // resize, the bounds are fine and the mismatch is DPI - if it snaps to fill, the initial
        // bounds were simply stale.
        static int loggedResizes = 0;
        if (loggedResizes < 8)
        {
            loggedResizes++;
            WebViewerDiagLog("resize: put_Bounds %dx%d", width, height);
        }
    }
    else
    {
        WebViewerDiagLog("resize IGNORED (controller not created yet): %dx%d", width, height);
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
    std::wstring NavigationUrl; // Non-empty for files navigated directly by WebView2
    std::string HtmlContent; // Non-empty for Markdown (pre-rendered HTML)
    int Left, Top, Width, Height;
    UINT ShowCmd;
    BOOL ReturnLock;
    HANDLE* Lock;
    BOOL* LockOwner;
    BOOL Success;
    HANDLE Continue;
};

// Build a title string from the file path
static std::wstring MakeWindowTitle(const std::wstring& filePath, const wchar_t* pluginName)
{
    std::wstring title = filePath;
    title += L" - ";
    title += pluginName;
    return title;
}

unsigned WINAPI ThreadViewerMessageLoop(void* param)
{
    CALL_STACK_MESSAGE1("ThreadViewerMessageLoop(Version 2.00)");
    SetThreadNameInVCAndTrace(L"WebViewLoop");
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
                            MakeWindowTitle(data->Name, SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_PLUGINNAME).c_str()).c_str(),
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
            SendMessageW(window->HWindow, WM_SETICON, ICON_BIG,
                         (LPARAM)LoadIconW(DLLInstance, MAKEINTRESOURCEW(IDI_WEBVIEWER)));
            SendMessageW(window->HWindow, WM_SETICON, ICON_SMALL,
                         (LPARAM)LoadIconW(DLLInstance, MAKEINTRESOURCEW(IDI_WEBVIEWER)));
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
    std::wstring navigationUrl = std::move(data->NavigationUrl);
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
            window->m_viewer.Navigate(navigationUrl);
        }

        CALL_STACK_MESSAGE1("ThreadViewerMessageLoop::message-loop");
        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
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

FileFormatEnum GetFileFormat(const wchar_t* name)
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

BOOL CPluginInterfaceForViewer::ViewFile(const wchar_t* name, int left, int top, int width,
                                         int height, UINT showCmd, BOOL alwaysOnTop,
                                         BOOL returnLock, HANDLE* lock, BOOL* lockOwner,
                                         CSalamanderPluginViewerData* viewerData,
                                         int enumFilesSourceUID, int enumFilesCurrentIndex)
{
    CALL_STACK_MESSAGE11("CPluginInterfaceForViewer::ViewFile(%ls, %d, %d, %d, %d, "
                         "0x%X, %d, %d, , , , %d, %d)",
                         name, left, top, width, height,
                         showCmd, alwaysOnTop, returnLock, enumFilesSourceUID, enumFilesCurrentIndex);

    FileFormatEnum fileFormat = GetFileFormat(name);

    CTVData data;
    data.AlwaysOnTop = alwaysOnTop;
    data.Name = name;
    if (fileFormat == ffeMarkdown)
    {
        data.HtmlContent = ConvertMarkdownToHTML(data.Name);
        if (data.HtmlContent.empty())
        {
            SalamanderGeneral->SalMessageBox(NULL,
                                             SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_UNABLETOOPENIE).c_str(),
                                             SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ERRORTITLE).c_str(),
                                             MB_ICONEXCLAMATION | MB_OK | MB_SETFOREGROUND);
            return FALSE;
        }
    }
    else if (!WebViewerBuildFileUrl(data.Name, data.NavigationUrl))
    {
        SalamanderGeneral->SalMessageBox(NULL,
                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_UNABLETOOPENIE).c_str(),
                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ERRORTITLE).c_str(),
                                         MB_ICONEXCLAMATION | MB_OK | MB_SETFOREGROUND);
        return FALSE;
    }
    data.Left = left;
    data.Top = top;
    data.Width = width;
    data.Height = height;
    data.ShowCmd = showCmd;
    data.ReturnLock = returnLock;
    data.Lock = lock;
    data.LockOwner = lockOwner;
    data.Success = FALSE;
    data.Continue = CreateEventW(NULL, FALSE, FALSE, NULL);
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
        SalamanderGeneral->SalMessageBox(NULL,
                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_UNABLETOOPENIE).c_str(),
                                         SPLLoadStrOwned(SalamanderGeneral, HLanguage, IDS_ERRORTITLE).c_str(),
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
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
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
        PostMessageW(item->HWindow, WM_CLOSE, 0, 0);
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
        Lock = CreateEventW(NULL, FALSE, FALSE, NULL);
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
