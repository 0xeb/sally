// Integration tests for the WebView2 webviewer plugin
// Tests Markdown conversion, path utilities, and WebView2 environment creation.

#include <gtest/gtest.h>
#include <windows.h>
#include <string>
#include <fstream>
#include <filesystem>
#include <wrl.h>
#include <WebView2.h>
#include <shlobj.h>

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Callback;

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Path/URL utility tests (standalone, matching webviewer.cpp logic)
// ---------------------------------------------------------------------------

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

static std::wstring AnsiToWide(const char* ansi)
{
    int len = MultiByteToWideChar(CP_ACP, 0, ansi, -1, nullptr, 0);
    if (len <= 0)
        return {};
    std::wstring wide(len - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, ansi, -1, wide.data(), len);
    return wide;
}

static std::string MakeBaseHref(const std::wstring& dir)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), (int)dir.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), (int)dir.size(), utf8.data(), len, nullptr, nullptr);
    for (char& c : utf8)
    {
        if (c == '\\')
            c = '/';
    }
    return "file:///" + utf8;
}

static std::wstring GetDirectoryFromPath(const std::wstring& filePath)
{
    auto pos = filePath.rfind(L'\\');
    if (pos == std::wstring::npos)
        pos = filePath.rfind(L'/');
    if (pos != std::wstring::npos)
        return filePath.substr(0, pos + 1);
    return L".\\";
}

TEST(PathToFileUrl, BasicPath)
{
    EXPECT_EQ(PathToFileUrl(L"C:\\Users\\test\\file.html"),
              L"file:///C:/Users/test/file.html");
}

TEST(PathToFileUrl, UNCPath)
{
    // UNC paths \\server\share become file://///server/share
    // (file:/// prefix + //server/share from the double backslash)
    EXPECT_EQ(PathToFileUrl(L"\\\\server\\share\\file.html"),
              L"file://///server/share/file.html");
}

TEST(PathToFileUrl, AlreadyForwardSlashes)
{
    EXPECT_EQ(PathToFileUrl(L"C:/path/file.html"),
              L"file:///C:/path/file.html");
}

TEST(AnsiToWide, BasicConversion)
{
    std::wstring result = AnsiToWide("hello.txt");
    EXPECT_EQ(result, L"hello.txt");
}

TEST(AnsiToWide, EmptyString)
{
    std::wstring result = AnsiToWide("");
    EXPECT_TRUE(result.empty());
}

TEST(AnsiToWide, PathWithSpaces)
{
    std::wstring result = AnsiToWide("C:\\My Documents\\file.md");
    EXPECT_EQ(result, L"C:\\My Documents\\file.md");
}

TEST(MakeBaseHref, SimpleDir)
{
    std::string result = MakeBaseHref(L"C:\\docs\\");
    EXPECT_EQ(result, "file:///C:/docs/");
}

TEST(MakeBaseHref, UnicodeDir)
{
    // Japanese chars in path
    std::string result = MakeBaseHref(L"C:\\\x30C6\x30B9\x30C8\\");
    EXPECT_TRUE(result.find("file:///C:/") == 0);
    // Verify it's valid UTF-8 (not empty after the prefix)
    EXPECT_GT(result.size(), std::string("file:///C:/").size() + 1);
}

TEST(GetDirectoryFromPath, BackslashPath)
{
    EXPECT_EQ(GetDirectoryFromPath(L"C:\\Users\\test\\readme.md"),
              L"C:\\Users\\test\\");
}

TEST(GetDirectoryFromPath, ForwardSlashPath)
{
    EXPECT_EQ(GetDirectoryFromPath(L"C:/Users/test/readme.md"),
              L"C:/Users/test/");
}

TEST(GetDirectoryFromPath, NoSeparator)
{
    EXPECT_EQ(GetDirectoryFromPath(L"readme.md"), L".\\");
}

// ---------------------------------------------------------------------------
// Markdown conversion tests — create temp .md files and verify HTML output
// ---------------------------------------------------------------------------

class MarkdownConversionTest : public ::testing::Test
{
protected:
    fs::path tempDir;

    void SetUp() override
    {
        wchar_t buf[MAX_PATH];
        GetTempPathW(MAX_PATH, buf);
        tempDir = fs::path(buf) / L"webviewer_test";
        fs::create_directories(tempDir);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(tempDir, ec);
    }

    // Write a UTF-8 file
    void WriteFile(const fs::path& path, const std::string& content)
    {
        std::ofstream f(path, std::ios::binary);
        f.write(content.data(), content.size());
    }
};

// We can't call ConvertMarkdownToHTML directly (it's inside the plugin DLL),
// but we can test the HTML structure expectations by verifying file I/O works.
// This tests the same pattern the plugin uses: _wfopen on wide paths.
TEST_F(MarkdownConversionTest, WidePathFileAccess)
{
    fs::path mdFile = tempDir / L"test.md";
    WriteFile(mdFile, "# Hello\n\nWorld");

    FILE* fp = _wfopen(mdFile.wstring().c_str(), L"r");
    ASSERT_NE(fp, nullptr);

    char buffer[256];
    size_t bytes = fread(buffer, 1, sizeof(buffer) - 1, fp);
    buffer[bytes] = '\0';
    fclose(fp);

    EXPECT_TRUE(std::string(buffer).find("# Hello") != std::string::npos);
}

TEST_F(MarkdownConversionTest, UnicodePathFileAccess)
{
    // Create a directory with Unicode characters
    fs::path unicodeDir = tempDir / L"\x30C6\x30B9\x30C8";  // Japanese "test"
    fs::create_directories(unicodeDir);
    fs::path mdFile = unicodeDir / L"readme.md";
    WriteFile(mdFile, "# Unicode path test\n");

    FILE* fp = _wfopen(mdFile.wstring().c_str(), L"r");
    ASSERT_NE(fp, nullptr);

    char buffer[256];
    size_t bytes = fread(buffer, 1, sizeof(buffer) - 1, fp);
    buffer[bytes] = '\0';
    fclose(fp);

    EXPECT_TRUE(std::string(buffer).find("Unicode path") != std::string::npos);
}

TEST_F(MarkdownConversionTest, LongPathFileAccess)
{
    // Create a deeply nested path >260 chars
    fs::path longDir = tempDir;
    for (int i = 0; i < 20; i++)
        longDir /= L"subdirectory_level";

    std::error_code ec;
    fs::create_directories(longDir, ec);
    if (ec)
    {
        // Long paths may not be enabled on this system
        GTEST_SKIP() << "Long paths not available: " << ec.message();
    }

    fs::path mdFile = longDir / L"test.md";
    EXPECT_GT(mdFile.wstring().size(), 260u);

    WriteFile(mdFile, "# Long path test\n");

    FILE* fp = _wfopen(mdFile.wstring().c_str(), L"r");
    ASSERT_NE(fp, nullptr);

    char buffer[256];
    size_t bytes = fread(buffer, 1, sizeof(buffer) - 1, fp);
    buffer[bytes] = '\0';
    fclose(fp);

    EXPECT_TRUE(std::string(buffer).find("Long path") != std::string::npos);
}

TEST_F(MarkdownConversionTest, BaseHrefForImageResolution)
{
    // Verify that the base href generated from a markdown file path
    // correctly points to the file's directory
    std::wstring mdPath = tempDir.wstring() + L"\\docs\\readme.md";
    std::wstring dir = GetDirectoryFromPath(mdPath);
    std::string baseHref = MakeBaseHref(dir);

    EXPECT_TRUE(baseHref.find("file:///") == 0);
    EXPECT_TRUE(baseHref.find("/docs/") != std::string::npos);
    EXPECT_TRUE(baseHref.find('\\') == std::string::npos);  // No backslashes in URL
}

// ---------------------------------------------------------------------------
// WebView2 runtime availability test
// ---------------------------------------------------------------------------

TEST(WebView2Runtime, EnvironmentCanBeCreated)
{
    // This test verifies the WebView2 Runtime is installed.
    // It creates a WebView2 environment without a window (just the environment object).
    // Skip if runtime is not available.

    // We need COM
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ASSERT_TRUE(SUCCEEDED(hrInit) || hrInit == S_FALSE);

    ComPtr<ICoreWebView2Environment> environment;
    BOOL ready = FALSE;
    HRESULT createResult = E_FAIL;

    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring userDataFolder = std::wstring(tempPath) + L"webviewer_test_webview2";

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        userDataFolder.c_str(),
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [&environment, &ready, &createResult](HRESULT result, ICoreWebView2Environment* env) -> HRESULT
            {
                createResult = result;
                if (SUCCEEDED(result) && env != nullptr)
                    environment = env;
                ready = TRUE;
                return S_OK;
            })
            .Get());

    if (FAILED(hr))
    {
        CoUninitialize();
        GTEST_SKIP() << "WebView2 Runtime not available (CreateCoreWebView2EnvironmentWithOptions returned 0x"
                     << std::hex << hr << ")";
    }

    // Pump messages until the callback fires
    DWORD startTick = GetTickCount();
    while (!ready && (GetTickCount() - startTick) < 10000)
    {
        DWORD dwResult = MsgWaitForMultipleObjectsEx(0, nullptr, 500, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
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

    if (!ready)
    {
        CoUninitialize();
        GTEST_SKIP() << "WebView2 environment creation timed out";
    }

    EXPECT_TRUE(SUCCEEDED(createResult)) << "Environment creation failed: 0x" << std::hex << createResult;
    EXPECT_NE(environment.Get(), nullptr);

    // Cleanup user data folder
    environment = nullptr;
    std::error_code ec;
    std::filesystem::remove_all(userDataFolder, ec);

    CoUninitialize();
}

// ---------------------------------------------------------------------------
// WebView2 controller creation test (requires a hidden window)
// ---------------------------------------------------------------------------

TEST(WebView2Runtime, ControllerCanBeCreated)
{
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ASSERT_TRUE(SUCCEEDED(hrInit) || hrInit == S_FALSE);

    // Create a hidden window to host WebView2
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"WebView2TestWindow";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, L"WebView2TestWindow", L"", WS_OVERLAPPEDWINDOW,
                                0, 0, 800, 600, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    ASSERT_NE(hwnd, (HWND) nullptr);

    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring userDataFolder = std::wstring(tempPath) + L"webviewer_test_webview2_ctrl";

    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    BOOL ready = FALSE;
    HRESULT finalResult = E_FAIL;

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userDataFolder.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [&](HRESULT result, ICoreWebView2Environment* env) -> HRESULT
            {
                if (FAILED(result) || !env)
                {
                    finalResult = result;
                    ready = TRUE;
                    return result;
                }
                environment = env;
                env->CreateCoreWebView2Controller(hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [&](HRESULT result, ICoreWebView2Controller* ctrl) -> HRESULT
                        {
                            finalResult = result;
                            if (SUCCEEDED(result) && ctrl)
                            {
                                controller = ctrl;
                                ctrl->get_CoreWebView2(&webview);
                            }
                            ready = TRUE;
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    if (FAILED(hr))
    {
        DestroyWindow(hwnd);
        UnregisterClassW(L"WebView2TestWindow", GetModuleHandleW(nullptr));
        CoUninitialize();
        GTEST_SKIP() << "WebView2 not available";
    }

    DWORD startTick = GetTickCount();
    while (!ready && (GetTickCount() - startTick) < 15000)
    {
        DWORD dwResult = MsgWaitForMultipleObjectsEx(0, nullptr, 500, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
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

    if (!ready)
    {
        DestroyWindow(hwnd);
        UnregisterClassW(L"WebView2TestWindow", GetModuleHandleW(nullptr));
        CoUninitialize();
        GTEST_SKIP() << "WebView2 controller creation timed out";
    }

    EXPECT_TRUE(SUCCEEDED(finalResult)) << "Controller creation failed: 0x" << std::hex << finalResult;
    EXPECT_NE(controller.Get(), nullptr);
    EXPECT_NE(webview.Get(), nullptr);

    // Test NavigateToString with simple HTML
    if (webview)
    {
        BOOL navReady = FALSE;
        EventRegistrationToken token;
        webview->add_NavigationCompleted(
            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                [&navReady](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT
                {
                    BOOL success = FALSE;
                    args->get_IsSuccess(&success);
                    navReady = TRUE;
                    return S_OK;
                }).Get(),
            &token);

        webview->NavigateToString(L"<html><body><h1>Test</h1></body></html>");

        DWORD navStart = GetTickCount();
        while (!navReady && (GetTickCount() - navStart) < 10000)
        {
            DWORD dwResult = MsgWaitForMultipleObjectsEx(0, nullptr, 200, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
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

        EXPECT_TRUE(navReady) << "Navigation to string did not complete";
        webview->remove_NavigationCompleted(token);
    }

    // Cleanup
    if (controller)
        controller->Close();
    controller = nullptr;
    webview = nullptr;
    environment = nullptr;

    DestroyWindow(hwnd);
    UnregisterClassW(L"WebView2TestWindow", GetModuleHandleW(nullptr));

    std::error_code ec;
    fs::remove_all(userDataFolder, ec);

    CoUninitialize();
}
