// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// The update window. --check asks GitHub for the latest release and shows it: the installed and
// the latest version, the release notes, and Install and Restart for a newer release, which
// downloads and verifies the package (common/update/ReleaseDownload.h), unpacks it, closes Sally
// and leaves the rest to --apply. --report shows, once, how the last update went. The window is
// laid out in code, so that one layout serves every language of texts.rc2.

#include <windows.h>
#include "SallyUrls.h"
#include <shellapi.h>
#include <richedit.h>
#include <commctrl.h>
#include <set>
#include "supporter/Win32SupporterUi.h"
#include "update/Win32DisplayContent.h"
#include "BuiltinLanguages.h"

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#include "ExtendedLengthPath.h"
#include "salupdate.h"
#include "salupdate.rh"
#include "ui_style.h"
#include "Win32HttpsClient.h"
#include "Win32TextCodec.h"
#include "update/GitHubRelease.h"
#include "update/ReleaseChecksums.h"
#include "update/ReleaseDownload.h"
#include "update/ReleaseNotesText.h"
#include "update/UpdatePackage.h"
#include "update/UpdateProcesses.h"
#include "update/UpdaterCommand.h"

using namespace Sally::Update;

namespace salupdate
{

namespace
{

std::wstring WindowClass = L"SallyUpdateWindow";
// One update window per session: a second check brings the first one forward.
const wchar_t* const kWindowMutex = L"Local\\SallyUpdateWindow";
const wchar_t* const kReleasesPage = Sally::Urls::Releases;
const size_t kMaxNotesChars = 64 * 1024;

// Install and Restart keeps the control id CheckVer's button had.
enum ControlId
{
    kIdInstall = 104,
    kIdReleasePage = 105,
    kIdHeadline = 200,
    kIdInstalled,
    kIdLatest,
    kIdNotes = 204, // One reading surface; retired separate headings/panel IDs stay unused.
    kIdStatus,
    kIdSupport = 211, kIdSupporter,
    kIdCheckUpdates = 214,
};

enum : UINT
{
    WM_APP_CHECKED = WM_APP + 1, // wParam: TRUE when the feed answered with a release
    WM_APP_PROGRESS,             // wParam: Stage, lParam: percent or -1
    WM_APP_CONTENT_READY,
    WM_APP_PREPARED,             // wParam: TRUE when the release is downloaded, verified and unpacked
};

enum Stage
{
    kStageDownloading,
    kStageVerifying,
    kStagePreparing,
};

enum class View
{
    Checking,
    CheckFailed,
    UpToDate,
    Newer,
    Report,
};

struct Session
{
    std::wstring InstallRoot;
    std::wstring InstalledVersion; // "1.0.37"
    std::wstring FeedUrl;
    bool Automatic = false;
    bool Offline = false, HasBundle = false, SupportActions = true;
    bool LayingOut = false;
    uint64_t NextContentTime = 0;
    std::wstring Profile;
    std::string Tag = "en-US";
    DisplayContent Bundle;
    MarkdownDisplay NotesDisplay, MaintainerDisplay;
    ReadingDisplay Reading;
    std::shared_ptr<DisplayContentSession> Content;

    View Current = View::Checking;

    ReleaseCheckResult Release;
    ReleaseFailure CheckFailure;

    // Install and Restart; the worker writes InstallError before it posts WM_APP_PREPARED.
    std::wstring WorkFolder;
    std::wstring Staging;
    std::wstring Version; // "1.0.38"
    std::wstring Arch;
    std::wstring InstallError;
    std::atomic<bool> Cancel{false};
    bool Installing = false;
    bool InstallStopped = false;
    int LayoutKind = -1;
    int MinimumHeight = 220;

    std::wstring ReportText;
    std::wstring ReportPage;

    HWND Window = NULL;
    HFONT Font = NULL;
    HFONT Bold = NULL;
    int Dpi = 96;
    bool Placed = false;
};

// The process has one window; the session lives until the process ends, so that a worker still
// running after the window closed never touches freed memory.
Session TheSession;

std::wstring Widen(const std::string& utf8)
{
    std::wstring wide;
    Win32DecodeText(CP_UTF8, utf8, wide);
    return wide;
}

std::string Narrow(const std::wstring& wide)
{
    std::string utf8;
    Win32EncodeText(CP_UTF8, wide, utf8);
    return utf8;
}

std::wstring PlainVersion(const std::string& tag)
{
    const std::wstring text = Widen(tag);
    return !text.empty() && (text[0] == L'v' || text[0] == L'V') ? text.substr(1) : text;
}

// Only Sally's own pages on GitHub are opened; a release feed is not trusted with more.
void OpenPage(const std::wstring& url)
{
    const wchar_t* const prefix = L"https://github.com/";
    const int length = static_cast<int>(wcslen(prefix));
    if (url.size() <= static_cast<size_t>(length) ||
        CompareStringOrdinal(url.c_str(), length, prefix, length, TRUE) != CSTR_EQUAL ||
        url.find_first_of(L" \"<>") != std::wstring::npos)
        return;
    ShellExecuteW(TheSession.Window, L"open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

std::wstring FailureText(const ReleaseFailure& failure)
{
    switch (failure.What)
    {
    case ReleaseFailure::Kind::Network:
        return failure.Network.Code != 0 ? Win32NetworkErrorText(failure.Network.Code, Language) : failure.Network.Text;
    case ReleaseFailure::Kind::BadFeed:
        return Text(IDS_UPDATER_BAD_FEED, Widen(failure.Detail).c_str());
    case ReleaseFailure::Kind::NoChecksum:
        return Text(IDS_UPDATER_NO_CHECKSUM, Widen(TheSession.Release.PrimaryAssetName).c_str());
    case ReleaseFailure::Kind::ChecksumMismatch:
        return Text(IDS_UPDATER_DOWNLOAD_MISMATCH);
    case ReleaseFailure::Kind::Incomplete:
        return Text(IDS_UPDATER_DOWNLOAD_INCOMPLETE);
    case ReleaseFailure::Kind::WriteFailed:
        return SystemErrorText(failure.Code);
    default:
        return std::wstring();
    }
}

bool SameFolder(const std::wstring& a, const std::wstring& b)
{
    return IsPathInsideFolder(a, b) && IsPathInsideFolder(b, a);
}

// The unpacked releases an update left in the work folder (v<version>-<arch>, manual).
void RemoveStagingFolders(const std::wstring& workFolder)
{
    WIN32_FIND_DATAW find;
    HANDLE handle = FindFirstFileExW(Sally::ToExtendedLengthPath(workFolder + L"\\*").c_str(), FindExInfoBasic, &find,
                                     FindExSearchNameMatch, NULL, 0);
    if (handle == INVALID_HANDLE_VALUE)
        return;
    std::vector<std::wstring> folders;
    do
    {
        const std::wstring name = find.cFileName;
        if ((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 || name == L"." || name == L"..")
            continue;
        if (name[0] == L'v' || name == L"manual")
            folders.push_back(workFolder + L"\\" + name);
    } while (FindNextFileW(handle, &find));
    FindClose(handle);
    for (const std::wstring& folder : folders)
        RemoveTree(folder);
}

// The package is written next to the release it is unpacked into.
class FilePackageSink final : public IPackageSink
{
public:
    FilePackageSink(std::wstring path, HWND notify) : Path(std::move(path)), Notify(notify) {}
    ~FilePackageSink() override { Close(); }

    bool Open(DWORD& error)
    {
        File = CreateFileW(Sally::ToExtendedLengthPath(Path).c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
        if (File == INVALID_HANDLE_VALUE)
        {
            error = GetLastError();
            return false;
        }
        return true;
    }
    bool Write(const BYTE* data, DWORD size, DWORD& error) override
    {
        DWORD written = 0;
        if (!WriteFile(File, data, size, &written, NULL) || written != size)
        {
            error = GetLastError() != 0 ? GetLastError() : ERROR_WRITE_FAULT;
            return false;
        }
        return true;
    }
    bool Finish(std::string& sha256, DWORD& error) override
    {
        if (!FlushFileBuffers(File))
        {
            error = GetLastError();
            return false;
        }
        Close();
        PostMessageW(Notify, WM_APP_PROGRESS, kStageVerifying, -1);
        return ComputeFileSha256(Path, sha256, error);
    }
    void Discard() override
    {
        Close();
        DeleteFileW(Sally::ToExtendedLengthPath(Path).c_str());
    }

private:
    void Close()
    {
        if (File != INVALID_HANDLE_VALUE)
        {
            CloseHandle(File);
            File = INVALID_HANDLE_VALUE;
        }
    }

    std::wstring Path;
    HWND Notify;
    HANDLE File = INVALID_HANDLE_VALUE;
};

DWORD WINAPI CheckWorker(void*)
{
    Session& s = TheSession;
    Win32HttpsClient client(&s.Cancel);
    const bool ok =
        FetchLatestRelease(client, s.FeedUrl, Narrow(s.InstalledVersion), BuildPlatform(), s.Release, s.CheckFailure);
    PostMessageW(s.Window, WM_APP_CHECKED, ok ? TRUE : FALSE, 0);
    return 0;
}

DWORD WINAPI InstallWorker(void*)
{
    Session& s = TheSession;
    bool ok = false;
    std::wstring error;
    CreateDirectoryW(Sally::ToExtendedLengthPath(s.Staging).c_str(), NULL);
    const std::wstring package = s.Staging + L"\\" + Widen(s.Release.PrimaryAssetName);
    Log(L"downloading " + Widen(s.Release.PrimaryUrl) + L" -> " + package);
    FilePackageSink sink(package, s.Window);
    DWORD openError = 0;
    if (!sink.Open(openError))
    {
        error = package + L"\n" + SystemErrorText(openError);
    }
    else
    {
        Win32HttpsClient client(&s.Cancel);
        int reported = -1;
        std::string sha256;
        ReleaseFailure failure;
        const bool downloaded = DownloadReleasePackage(
            client, s.Release, sink,
            [&](unsigned long long received, unsigned long long total) {
                const int percent = total > 0 ? static_cast<int>(received * 100 / total) : -1;
                if (percent != reported)
                {
                    reported = percent;
                    PostMessageW(s.Window, WM_APP_PROGRESS, kStageDownloading, percent);
                }
            },
            sha256, failure);
        if (!downloaded)
        {
            error = FailureText(failure);
            Log(L"download failed: " + error);
        }
        else
        {
            PostMessageW(s.Window, WM_APP_PROGRESS, kStagePreparing, -1);
            UpdaterCommand prepare;
            prepare.Mode = UpdaterMode::Prepare;
            prepare.Package = package;
            prepare.Sha256 = Widen(sha256);
            prepare.Staging = s.Staging;
            prepare.Version = s.Version;
            prepare.Arch = s.Arch;
            UpdateResult result;
            ok = PrepareRelease(prepare, result);
            if (!ok)
            {
                error = result.Message;
                if (!result.FailedPath.empty())
                    error += L"\n" + result.FailedPath;
                if (result.Code != 0)
                    error += L"\n" + SystemErrorText(result.Code);
            }
            DeleteFileW(Sally::ToExtendedLengthPath(package).c_str());
        }
    }
    s.InstallError = error;
    PostMessageW(s.Window, WM_APP_PREPARED, ok ? TRUE : FALSE, 0);
    return 0;
}

bool StartWorker(LPTHREAD_START_ROUTINE routine)
{
    HANDLE thread = CreateThread(NULL, 0, routine, NULL, 0, NULL);
    if (thread == NULL)
        return false;
    CloseHandle(thread);
    return true;
}

// --- the window -------------------------------------------------------------------------------

int Px(int value)
{
    return MulDiv(value, TheSession.Dpi, 96);
}

HWND Control(int id)
{
    return GetDlgItem(TheSession.Window, id);
}

void SetText(int id, const std::wstring& text)
{
    SetWindowTextW(Control(id), text.c_str());
}

std::wstring GetText(HWND window)
{
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    text.resize(static_cast<size_t>(GetWindowTextW(window, &text[0], static_cast<int>(text.size()))));
    return text;
}

// The height a text takes when it wraps at width.
int TextHeight(HFONT font, const std::wstring& text, int width)
{
    HDC dc = GetDC(TheSession.Window);
    HGDIOBJ old = SelectObject(dc, font);
    RECT rect = {0, 0, width, 0};
    DrawTextW(dc, text.empty() ? L" " : text.c_str(), -1, &rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(TheSession.Window, dc);
    return rect.bottom - rect.top;
}

int ButtonWidth(int id)
{
    HDC dc = GetDC(TheSession.Window);
    HGDIOBJ old = SelectObject(dc, TheSession.Font);
    std::wstring text = GetText(Control(id));
    std::wstring shown;
    for (wchar_t ch : text)
        if (ch != L'&')
            shown.push_back(ch);
    SIZE size = {};
    GetTextExtentPoint32W(dc, shown.c_str(), static_cast<int>(shown.size()), &size);
    SelectObject(dc, old);
    ReleaseDC(TheSession.Window, dc);
    return (std::max)(static_cast<int>(size.cx) + Px(24), Px(90));
}

void Place(int id, bool visible, int x, int y, int width, int height)
{
    HWND control = Control(id);
    ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
    if (visible)
        SetWindowPos(control, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

std::wstring T(UINT id) { return Sally::Supporter::SupporterText(id, Language); }

void Layout()
{
    Session& s = TheSession;
    if (s.LayingOut || !s.Window) return;
    s.LayingOut = true;
    const bool release = (s.Current == View::Newer || s.Current == View::UpToDate) && !s.Installing && !s.InstallStopped;
    const bool readingVisible = release && s.Reading.Content.Visible;
    const int kind = release ? (readingVisible ? 2 : 1) : 0;
    const bool contentSizeChanged = !s.Placed || s.LayoutKind != kind;
    if (contentSizeChanged)
    {
        s.MinimumHeight = kind == 2 ? 480 : 220;
        POINT cursor{}; GetCursorPos(&cursor);
        MONITORINFO monitor = {sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor);
        const RECT& area = monitor.rcWork;
        int width = (std::min)(Px(720), static_cast<int>(area.right - area.left) - Px(12));
        int height = (std::min)(Px(kind == 2 ? 600 : 260), static_cast<int>(area.bottom - area.top) - Px(20));
        s.LayoutKind = kind;
        SetWindowPos(s.Window, nullptr, area.left + (area.right - area.left - width) / 2,
            area.top + (area.bottom - area.top - height) / 3, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        s.Placed = true;
    }
    RECT client{}; GetClientRect(s.Window, &client);
    const int margin = Px(12), width = (std::max)(Px(100), static_cast<int>(client.right) - 2 * margin);
    const int line = TextHeight(s.Font, L"Ag", width), buttonHeight = (std::max)(Px(26), line + Px(10));
    int y = margin;
    int headline = TextHeight(s.Bold, GetText(Control(kIdHeadline)), width);
    Place(kIdHeadline, true, margin, y, width, headline); y += headline + Px(8);
    Place(kIdInstalled, release, margin, y, width, line); if (release) y += line + Px(3);
    Place(kIdLatest, release && !s.Offline, margin, y, width, line); if (release && !s.Offline) y += line + Px(6);
    bool status = !GetText(Control(kIdStatus)).empty();
    int statusHeight = status ? TextHeight(s.Font, GetText(Control(kIdStatus)), width) + Px(4) : 0;
    const bool support = release && s.SupportActions;
    const bool supportFits = ButtonWidth(kIdSupport) + ButtonWidth(kIdSupporter) + Px(8) <= width;
    const int supportRows = support ? (supportFits ? 1 : 2) : 0;
    const bool install = s.Current == View::Newer && !s.Offline;
    const bool page = s.Current != View::Checking && !(s.Current == View::Report && s.ReportPage.empty());
    std::vector<int> footer;
    if (release && s.Offline) footer.push_back(kIdCheckUpdates);
    else if (install) footer.push_back(kIdInstall);
    if (page) footer.push_back(kIdReleasePage);
    footer.push_back(IDCANCEL);
    int footerWidth = Px(8) * static_cast<int>(footer.size() - 1);
    for (int id : footer) footerWidth += ButtonWidth(id);
    const bool footerFits = footerWidth <= width;
    const int footerRows = footerFits ? 1 : static_cast<int>(footer.size());
    int reserved = statusHeight + (supportRows + footerRows) * (buttonHeight + Px(6)) + margin;
    int reading = readingVisible ? (std::max)(0, static_cast<int>(client.bottom) - y - reserved - Px(6)) : 0;
    Place(kIdNotes, readingVisible, margin, y, width, reading);
    if (readingVisible) y += reading + Px(6);
    Place(kIdStatus, status, margin, y, width, statusHeight); y += statusHeight;
    const int supportWidth = ButtonWidth(kIdSupport) + (width - Px(8) - ButtonWidth(kIdSupport) - ButtonWidth(kIdSupporter)) / 2;
    Place(kIdSupport, support, margin, y, supportFits ? supportWidth : width, buttonHeight);
    Place(kIdSupporter, support, supportFits ? margin + supportWidth + Px(8) : margin,
        supportFits ? y : y + buttonHeight + Px(6), supportFits ? width - supportWidth - Px(8) : width, buttonHeight);
    y += supportRows * (buttonHeight + Px(6));
    const int footerIds[] = {kIdInstall, kIdReleasePage, kIdCheckUpdates, IDCANCEL};
    for (int id : footerIds)
        if (std::find(footer.begin(), footer.end(), id) == footer.end()) ShowWindow(Control(id), SW_HIDE);
    const int columns = static_cast<int>(footer.size());
    int footerX = margin;
    for (int i = 0; i < columns; ++i)
    {
        const int buttonWidth = footerFits ? ButtonWidth(footer[i]) + (width - footerWidth) / columns : width;
        Place(footer[i], true, footerFits ? footerX : margin,
            footerFits ? y : y + i * (buttonHeight + Px(6)), buttonWidth, buttonHeight);
        footerX += buttonWidth + Px(8);
    }
    if (contentSizeChanged && kind == 1)
    {
        RECT outer{}; GetWindowRect(s.Window, &outer);
        MONITORINFO monitor = {sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(s.Window, MONITOR_DEFAULTTONEAREST), &monitor);
        const int chrome = static_cast<int>(outer.bottom - outer.top - client.bottom);
        const int needed = y + footerRows * (buttonHeight + Px(6)) + margin + chrome;
        const int height = (std::min)((std::max)(Px(220), needed), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top) - Px(20));
        s.MinimumHeight = MulDiv(height, 96, s.Dpi);
        SetWindowPos(s.Window, nullptr, 0, 0, outer.right - outer.left, height,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    EnableWindow(Control(kIdInstall), install && s.Release.CanInstall && !s.Installing);
    int defaultId = install && s.Release.CanInstall && !s.Installing ? kIdInstall : IDCANCEL;
    SendMessageW(Control(kIdInstall), BM_SETSTYLE, defaultId == kIdInstall ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE);
    SendMessageW(Control(IDCANCEL), BM_SETSTYLE, defaultId == IDCANCEL ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE);
    s.LayingOut = false;
    InvalidateRect(s.Window, nullptr, TRUE);
}

void Render(const ReadingDisplay& previous, const ReadingDisplay& current)
{
    const auto& display = current.Content;
    HWND control = Control(kIdNotes);
    CHARRANGE selection{}; POINT scroll{};
    SendMessageW(control, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    SendMessageW(control, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
    POINTL origin{};
    const auto first = SendMessageW(control, EM_CHARFROMPOS, 0, reinterpret_cast<LPARAM>(&origin));
    POINTL oldAnchor{};
    SendMessageW(control, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&oldAnchor), first);
    SendMessageW(control, WM_SETREDRAW, FALSE, 0);
    SETTEXTEX set = {ST_DEFAULT, 1200};
    SendMessageW(control, EM_SETTEXTEX, reinterpret_cast<WPARAM>(&set), reinterpret_cast<LPARAM>(display.Text.c_str()));
    CHARFORMAT2W format{}; format.cbSize = sizeof(format);
    format.dwMask = CFM_BOLD | CFM_ITALIC | CFM_LINK | CFM_COLOR | CFM_FACE;
    format.crTextColor = GetSysColor(COLOR_WINDOWTEXT);
    LOGFONTW font{}; GetObjectW(TheSession.Font, sizeof(font), &font);
    wcscpy_s(format.szFaceName, font.lfFaceName);
    SendMessageW(control, EM_SETSEL, 0, -1);
    SendMessageW(control, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
    for (const auto& run : display.Runs)
    {
        CHARRANGE range = {static_cast<LONG>(run.Start), static_cast<LONG>(run.Start + run.Length)};
        SendMessageW(control, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
        CHARFORMAT2W style = format;
        style.dwEffects = (run.Bold ? CFE_BOLD : 0) | (run.Italic ? CFE_ITALIC : 0) | (!run.Link.empty() ? CFE_LINK : 0);
        if (run.Code) wcscpy_s(style.szFaceName, L"Consolas");
        if (!run.Link.empty()) style.crTextColor = GetSysColor(COLOR_HOTLIGHT);
        SendMessageW(control, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&style));
    }
    selection.cpMin = static_cast<LONG>(RemapReadingPosition(previous, current, (std::max)(0L, selection.cpMin)));
    selection.cpMax = static_cast<LONG>(RemapReadingPosition(previous, current, (std::max)(0L, selection.cpMax)));
    SendMessageW(control, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    SendMessageW(control, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
    POINTL newAnchor{};
    const auto mappedFirst = RemapReadingPosition(previous, current, static_cast<size_t>((std::max)(LRESULT{0}, first)));
    SendMessageW(control, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&newAnchor), static_cast<LPARAM>(mappedFirst));
    scroll.y = (std::max)(0L, scroll.y + newAnchor.y - oldAnchor.y);
    SendMessageW(control, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
    SendMessageW(control, WM_SETREDRAW, TRUE, 0); InvalidateRect(control, nullptr, TRUE);
}

void RefreshContent()
{
    Session& s = TheSession;
    const uint64_t now = Sally::Supporter::NowUtc();
    DisplayContent release = s.HasBundle ? s.Bundle : DisplayContent{}, live;
    bool hasLive = false;
    if (s.Content)
    {
        std::lock_guard<std::mutex> lock(s.Content->Mutex);
        if (s.Content->HasRelease) release = s.Content->Release;
        if (s.Content->HasLive) { live = s.Content->Live; hasLive = true; }
    }
    std::string target = Sally::Supporter::CanonicalVersion(s.Offline ? Narrow(s.InstalledVersion) : s.Release.LatestVersion);
    auto notes = SelectReleaseNotes(release, s.Tag, s.Offline ? "" : s.Release.ReleaseNotes);
    s.NotesDisplay = BuildMarkdownDisplay(notes.Markdown);
    std::string maintainer;
    for (const auto& notice : ResolveNotices(release, hasLive ? &live : nullptr, target, s.Tag, now))
    {
        if (!maintainer.empty()) maintainer += "\n\n";
        if (!notice.Title.empty()) maintainer += "## " + notice.Title + "\n\n";
        maintainer += notice.Markdown;
    }
    s.MaintainerDisplay = BuildMarkdownDisplay(maintainer);
    auto indicator = [&](MarkdownDisplay& display)
    {
        if (!display.Truncated) return;
        std::wstring suffix = L"\r" + T(IDS_SUP_TRUNCATED);
        if (display.Text.size() + suffix.size() > 65536)
        {
            display.Text.resize(65536 - suffix.size());
            if (!display.Text.empty() && display.Text.back() >= 0xd800 && display.Text.back() <= 0xdbff) display.Text.pop_back();
        }
        for (auto& run : display.Runs) run.Length = run.Start < display.Text.size() ? (std::min)(run.Length, display.Text.size() - run.Start) : 0;
        display.Text += suffix;
    };
    indicator(s.NotesDisplay); indicator(s.MaintainerDisplay);
    auto reading = BuildReadingDisplay(s.NotesDisplay, Text(IDS_UPDATER_NOTES_LABEL), s.MaintainerDisplay, T(IDS_SUP_MAINTAINER));
    Render(s.Reading, reading);
    s.Reading = std::move(reading);
    auto preferences = Sally::Supporter::ReadSupporterState(s.Profile, *GetWin32Registry());
    auto certificate = Sally::Supporter::VerifyCertificate(preferences.Token, now, Sally::Supporter::BuiltinPublicKeys());
    s.SupportActions = certificate.State != Sally::Supporter::Status::Active && certificate.State != Sally::Supporter::Status::NotYetValid;
    SetText(kIdSupporter, T(IDS_SUP_ENTER));
    s.NextContentTime = 0;
    auto boundary = [&](uint64_t value) { if (value > now && (!s.NextContentTime || value < s.NextContentTime)) s.NextContentTime = value; };
    boundary(certificate.ValidFrom); boundary(certificate.ValidUntil);
    if (hasLive) boundary(live.Until);
    for (const auto* model : {&release, &live}) for (const auto& notice : model->Notices) { boundary(notice.From); boundary(notice.Until); }
    std::wstring status;
    if (!s.NotesDisplay.Visible && !s.MaintainerDisplay.Visible) status = T(IDS_SUP_NO_NOTES);
    if (!s.Offline && s.Release.UpdateAvailable && !s.Release.CanInstall) status = Text(IDS_UPDATER_NOT_INSTALLABLE);
    SetText(kIdStatus, status);
    Layout();
}

void ShowLinks(HWND source, POINT position)
{
    std::set<std::string> seen; std::vector<std::string> links;
    const auto& display = TheSession.Reading.Content;
    for (const auto& run : display.Runs)
        if (!run.Link.empty() && ValidContentLink(run.Link) && seen.insert(run.Link).second && links.size() < 128) links.push_back(run.Link);
    if (links.empty()) return;
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < links.size(); ++i)
    {
        std::wstring label = Widen(links[i]).substr(0, 160);
        for (size_t at = 0; (at = label.find(L'&', at)) != std::wstring::npos; at += 2) label.insert(at, 1, L'&');
        AppendMenuW(menu, MF_STRING, static_cast<UINT_PTR>(i + 1), label.c_str());
    }
    if (position.x == -1 && position.y == -1)
    {
        RECT rect{}; GetWindowRect(source, &rect);
        position = {rect.left + Px(8), rect.top + Px(8)};
    }
    UINT item = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, position.x, position.y, 0, TheSession.Window, nullptr);
    DestroyMenu(menu);
    if (item && item <= links.size() && ValidContentLink(links[item - 1])) ShellExecuteW(TheSession.Window, L"open", Widen(links[item - 1]).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT CALLBACK ContentWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR subclass, DWORD_PTR)
{
    if (message == WM_CONTEXTMENU)
    {
        ShowLinks(window, {static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))});
        return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, ContentWindowProc, subclass);
    return DefSubclassProc(window, message, wParam, lParam);
}

void SetStatus(const std::wstring& text)
{
    SetText(kIdStatus, text);
}

void ShowRelease()
{
    Session& s = TheSession;
    const ReleaseCheckResult& release = s.Release;
    s.Current = release.UpdateAvailable ? View::Newer : View::UpToDate;
    s.InstallStopped = false;
    SetText(kIdHeadline, Text(release.UpdateAvailable ? IDS_UPDATER_NEWER : IDS_UPDATER_LATEST));
    SetText(kIdInstalled, Text(IDS_UPDATER_INSTALLED_VERSION, s.InstalledVersion.c_str()));
    SetText(kIdLatest, Text(IDS_UPDATER_RELEASE_VERSION, PlainVersion(release.LatestVersion).c_str()));
    s.HasBundle = Sally::Supporter::CanonicalVersion(release.LatestVersion) == Sally::Supporter::CanonicalVersion(Narrow(s.InstalledVersion)) &&
        ReadBundledDisplayContent(s.InstallRoot, Sally::Supporter::CanonicalVersion(release.LatestVersion), s.Bundle);
    SetStatus(release.UpdateAvailable && !release.CanInstall ? Text(IDS_UPDATER_NOT_INSTALLABLE) : std::wstring());
    RefreshContent();
    if (!s.Content)
    {
        s.Content = std::make_shared<DisplayContentSession>();
        s.Content->Start(s.Window, WM_APP_CONTENT_READY, Sally::Supporter::CanonicalVersion(release.LatestVersion),
            Widen(release.MetadataUrl), Widen(release.MetadataSignatureUrl), UpdateWorkFolder());
    }
    Layout();
}

void ShowCheckFailure()
{
    Session& s = TheSession;
    s.Current = View::CheckFailed;
    SetText(kIdHeadline, Text(IDS_UPDATER_CHECK_FAILED, FailureText(s.CheckFailure).c_str()));
    Layout();
}

void StartInstall()
{
    Session& s = TheSession;
    if (s.Installing || !s.Release.CanInstall)
        return;
    s.WorkFolder = UpdateWorkFolder();
    if (s.WorkFolder.empty())
    {
        SetStatus(Text(IDS_UPDATER_STOPPED, L"%LOCALAPPDATA%"));
        return;
    }
    s.Version = PlainVersion(s.Release.LatestVersion);
    s.Arch = Widen(GetPlatformLabel(BuildPlatform()));
    s.Staging = StagingFolderFor(s.WorkFolder, s.Version, s.Arch);
    s.Cancel = false;
    s.Installing = true;
    s.InstallStopped = false;
    if (s.Content) s.Content->Cancel = true;
    SetStatus(Text(IDS_UPDATER_DOWNLOADING, Widen(s.Release.PrimaryAssetName).c_str()));
    Layout();
    if (!StartWorker(InstallWorker))
    {
        s.Installing = false;
        s.InstallStopped = true;
        SetStatus(Text(IDS_UPDATER_STOPPED, SystemErrorText(GetLastError()).c_str()));
        Layout();
    }
}

// The release is unpacked: close Sally and let the copy of the updater beside it install it.
void FinishInstall(bool prepared)
{
    Session& s = TheSession;
    auto stop = [&](const std::wstring& status) {
        s.Installing = false;
        s.InstallStopped = true;
        SetStatus(status);
        Layout();
    };
    if (!prepared)
    {
        stop(s.Cancel || s.InstallError.empty() ? std::wstring() : Text(IDS_UPDATER_STOPPED, s.InstallError.c_str()));
        return;
    }
    const ProgramsInFolder running = FindProgramsInFolder(s.InstallRoot);
    if (!running.Others.empty())
    {
        Tell(Text(IDS_UPDATER_OTHERS_RUNNING, running.Others.c_str()), MB_ICONWARNING, s.Window);
        stop(std::wstring());
        return;
    }
    const size_t sallys = running.SallyPids.size();
    if (sallys > 0)
    {
        const std::wstring question = sallys == 1 ? Text(IDS_UPDATER_CONFIRM_INSTALL, s.Version.c_str())
                                                  : Text(IDS_UPDATER_CONFIRM_INSTALL_OTHERS, s.Version.c_str(),
                                                         static_cast<int>(sallys - 1));
        if (!Ask(question, s.Window))
        {
            stop(std::wstring());
            return;
        }
    }
    UpdaterCommand apply;
    apply.Mode = UpdaterMode::Apply;
    apply.Staging = s.Staging;
    apply.InstallRoot = s.InstallRoot;
    apply.WaitPids = running.SallyPids;
    apply.WaitPids.push_back(GetCurrentProcessId());
    apply.FromVersion = s.InstalledVersion;
    apply.ToVersion = s.Version;
    apply.ResultFile = UpdateResultPath(s.WorkFolder);
    DWORD error = 0;
    if (!StartApply(apply, s.Staging + L"\\salupdate.exe", error))
    {
        stop(Text(IDS_UPDATER_STOPPED, SystemErrorText(error).c_str()));
        return;
    }
    Log(L"installing " + s.Version + L" over " + s.InstalledVersion + L", closing Sally (" +
        std::to_wstring(sallys) + L")");
    CloseSallyWindows(running.SallyPids);
    DestroyWindow(s.Window);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    Session& s = TheSession;
    switch (message)
    {
    case WM_SIZE:
        if (s.Placed) Layout();
        return 0;
    case WM_GETMINMAXINFO:
    {
        auto info = reinterpret_cast<MINMAXINFO*>(lParam); info->ptMinTrackSize = {Px(420), Px(s.MinimumHeight)}; return 0;
    }
    case WM_TIMER:
        if (!s.Installing && !s.InstallStopped && s.NextContentTime && Sally::Supporter::NowUtc() >= s.NextContentTime) RefreshContent();
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE && !s.Installing && !s.InstallStopped && (s.Current == View::Newer || s.Current == View::UpToDate)) RefreshContent();
        break;
    case WM_APP_CONTENT_READY:
        if (!s.Installing && !s.InstallStopped && s.Content && !s.Content->Cancel) RefreshContent();
        return 0;
    case WM_NOTIFY:
    {
        auto header = reinterpret_cast<NMHDR*>(lParam);
        if (header && header->code == EN_LINK)
        {
            auto link = reinterpret_cast<ENLINK*>(lParam);
            if (link->msg == WM_LBUTTONUP)
            {
                const auto& display = s.Reading.Content;
                for (const auto& run : display.Runs)
                    if (!run.Link.empty() && static_cast<size_t>(link->chrg.cpMin) >= run.Start && static_cast<size_t>(link->chrg.cpMin) < run.Start + run.Length && ValidContentLink(run.Link))
                    { ShellExecuteW(window, L"open", Widen(run.Link).c_str(), nullptr, nullptr, SW_SHOWNORMAL); break; }
            }
            return 0;
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case kIdSupport: Sally::Supporter::OpenSupportPage(window); return 0;
        case kIdSupporter:
            Sally::Supporter::ShowSupporterDialog(window, Language, s.Profile, *GetWin32Registry(), true); RefreshContent(); return 0;
        case kIdCheckUpdates:
        {
            UpdaterCommand command; command.Mode = UpdaterMode::Check; command.InstallRoot = s.InstallRoot;
            auto arguments = FormatUpdaterCommand(command);
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", ModulePath().c_str(), arguments.c_str(), nullptr, SW_SHOWNORMAL)) > 32) DestroyWindow(window);
            return 0;
        }
        case kIdInstall:
            StartInstall();
            return 0;
        case kIdReleasePage:
            if (s.Current == View::Report)
                OpenPage(s.ReportPage);
            else
                OpenPage(Widen(s.Release.ReleasePageUrl).empty() ? kReleasesPage : Widen(s.Release.ReleasePageUrl));
            return 0;
        case IDCANCEL:
            SendMessageW(window, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case DM_GETDEFID:
        return MAKELRESULT(IsWindowVisible(Control(kIdInstall)) && IsWindowEnabled(Control(kIdInstall)) ? kIdInstall
                                                                                                           : IDCANCEL,
                           DC_HASDEFID);
    case WM_APP_CHECKED:
        if (wParam)
            ShowRelease();
        else
            ShowCheckFailure();
        return 0;
    case WM_APP_PROGRESS:
        if (!s.Installing)
            return 0;
        if (wParam == kStageDownloading)
        {
            std::wstring status = Text(IDS_UPDATER_DOWNLOADING, Widen(s.Release.PrimaryAssetName).c_str());
            if (static_cast<int>(lParam) >= 0)
                status += L" " + std::to_wstring(static_cast<int>(lParam)) + L" %";
            SetStatus(status);
        }
        else if (wParam == kStageVerifying)
            SetStatus(Text(IDS_UPDATER_VERIFYING));
        else
            SetStatus(Text(IDS_UPDATER_PREPARING, s.Version.c_str()));
        return 0;
    case WM_APP_PREPARED:
        FinishInstall(wParam != FALSE);
        return 0;
    case WM_CTLCOLORSTATIC:
        if (reinterpret_cast<HWND>(lParam) == Control(kIdNotes))
        {
            SetBkColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOW));
            SetTextColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        break;
    case WM_CLOSE:
        if (s.Content) s.Content->Cancel = true;
        s.Cancel = true; // a download or check under way stops at its next read
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, 1);
        if (s.Content) s.Content->Cancel = true;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND AddControl(const wchar_t* className, int id, DWORD style, DWORD exStyle, const std::wstring& text, HFONT font)
{
    HWND control = CreateWindowExW(exStyle, className, text.c_str(), WS_CHILD | style, 0, 0, 10, 10,
                                   TheSession.Window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   GetModuleHandleW(NULL), NULL);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    return control;
}

bool CreateUpdateWindow()
{
    Session& s = TheSession;
    WNDCLASSEXW windowClass = {sizeof(windowClass)};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(NULL);
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    windowClass.lpszClassName = WindowClass.c_str();
    windowClass.hIcon = LoadIconW(windowClass.hInstance, MAKEINTRESOURCEW(IDI_UPDATER));
    windowClass.hIconSm = windowClass.hIcon;
    RegisterClassExW(&windowClass);

    HDC screen = GetDC(NULL);
    s.Dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(NULL, screen);
    LOGFONTW font = DialogFontForDpi(s.Dpi);
    s.Font = CreateFontIndirectW(&font);
    LOGFONTW bold = font;
    bold.lfWeight = FW_BOLD;
    s.Bold = CreateFontIndirectW(&bold);

    s.Window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, WindowClass.c_str(), Text(IDS_UPDATER_TITLE).c_str(),
                               WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                               Px(600), Px(400), NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (s.Window == NULL)
        return false;
    SendMessageW(s.Window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(windowClass.hIcon));
    SendMessageW(s.Window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(windowClass.hIconSm));
    const DWORD label = SS_LEFT | SS_NOPREFIX;
    AddControl(L"STATIC", kIdHeadline, label, 0, std::wstring(), s.Bold);
    AddControl(L"STATIC", kIdInstalled, label, 0, std::wstring(), s.Font);
    AddControl(L"STATIC", kIdLatest, label, 0, std::wstring(), s.Font);
    LoadLibraryW(L"Msftedit.dll"); // OS Rich Edit stays loaded for the process lifetime
    AddControl(MSFTEDIT_CLASS, kIdNotes, WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
               WS_EX_CLIENTEDGE, std::wstring(), s.Font);
    SendMessageW(Control(kIdNotes), EM_SETBKGNDCOLOR, 0, GetSysColor(COLOR_WINDOW));
    SendMessageW(Control(kIdNotes), EM_SETEVENTMASK, 0, ENM_LINK);
    SendMessageW(Control(kIdNotes), EM_AUTOURLDETECT, FALSE, 0);
    SendMessageW(Control(kIdNotes), EM_SETLIMITTEXT, ReadingTextLimit, 0);
    if (!SetWindowSubclass(Control(kIdNotes), ContentWindowProc, 1, 0)) return false;
    AddControl(L"BUTTON", kIdSupport, WS_TABSTOP, 0, T(IDS_SUP_SUPPORT), s.Font);
    AddControl(L"BUTTON", kIdSupporter, WS_TABSTOP, 0, T(IDS_SUP_ENTER), s.Font);
    AddControl(L"BUTTON", kIdCheckUpdates, WS_TABSTOP, 0, T(IDS_SUP_CHECK_UPDATES), s.Font);
    AddControl(L"STATIC", kIdStatus, label, 0, std::wstring(), s.Font);
    AddControl(L"BUTTON", kIdInstall, WS_TABSTOP | BS_PUSHBUTTON, 0, Text(IDS_UPDATER_INSTALL_BUTTON), s.Font);
    AddControl(L"BUTTON", kIdReleasePage, WS_TABSTOP | BS_PUSHBUTTON, 0, Text(IDS_UPDATER_RELEASE_PAGE_BUTTON),
               s.Font);
    AddControl(L"BUTTON", IDCANCEL, WS_TABSTOP | BS_PUSHBUTTON, 0, Text(IDS_UPDATER_CLOSE_BUTTON), s.Font);
    // Date boundaries apply equally to offline bundled and online content.
    SetTimer(s.Window, 1, 1000, nullptr);
    return true;
}

void ShowUpdateWindow(bool activate)
{
    Session& s = TheSession;
    if (activate)
    {
        ShowWindow(s.Window, SW_SHOWNORMAL);
        SetForegroundWindow(s.Window);
        SetFocus(Control(IsWindowEnabled(Control(kIdInstall)) && IsWindowVisible(Control(kIdInstall)) ? kIdInstall
                                                                                                         : IDCANCEL));
    }
    else
    {
        // Sally's scheduled check does not take the keyboard from what the user is doing.
        ShowWindow(s.Window, SW_SHOWNOACTIVATE);
        FLASHWINFO flash = {sizeof(flash)};
        flash.hwnd = s.Window;
        flash.dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG;
        FlashWindowEx(&flash);
    }
}

void RunMessageLoop()
{
    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0)
    {
        // Handle link-menu keys before dialog/Rich Edit navigation can consume them.
        if (message.hwnd == Control(kIdNotes))
        {
            if (message.message == WM_KEYDOWN && message.wParam == VK_TAB)
            {
                SetFocus(GetNextDlgTabItem(TheSession.Window, message.hwnd, (GetKeyState(VK_SHIFT) & 0x8000) != 0));
                continue;
            }
            if ((message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) &&
                (message.wParam == VK_APPS || (message.wParam == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000))))
            {
                ShowLinks(message.hwnd, {-1, -1});
                continue;
            }
            if (message.message == WM_KEYUP && message.wParam == VK_APPS) continue;
        }
        if (TheSession.Window != NULL && IsWindow(TheSession.Window) &&
            IsDialogMessageW(TheSession.Window, &message))
            continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

// The Sally folder of the command, or of this updater; empty (after telling) when there is none.
std::wstring ResolveInstallRoot(const UpdaterCommand& command)
{
    std::wstring install = command.InstallRoot.empty() ? InstallRootOfUpdater(ModulePath()) : command.InstallRoot;
    while (!install.empty() && (install.back() == L'\\' || install.back() == L'/'))
        install.pop_back();
    if (install.empty() ||
        GetFileAttributesW(Sally::ToExtendedLengthPath(install + L"\\sally.exe").c_str()) == INVALID_FILE_ATTRIBUTES)
        return std::wstring();
    return install;
}

} // namespace

// --check [--auto]: the update window.
int CheckForUpdates(const UpdaterCommand& command)
{
    Session& s = TheSession;
    s.Automatic = command.Auto;
    s.Offline = command.Mode == UpdaterMode::WhatsNew;
    s.Profile = Sally::Supporter::ResolveProfileRoot();
    for (const auto& language : sally::languages::kBuiltinLanguages) if (language.LangId == Language) s.Tag = Narrow(language.Tag);
    s.InstallRoot = ResolveInstallRoot(command);
    if (s.InstallRoot.empty())
    {
        if (!s.Automatic) Tell(Text(IDS_UPDATER_NOT_IN_SALLY, ModulePath().c_str()), MB_ICONERROR);
        return 2;
    }
    auto identity = DisplayWindowIdentity(s.InstallRoot, s.Profile, s.Offline);
    WindowClass = L"SallyUpdateWindow." + identity;
    HANDLE single = CreateMutexW(nullptr, FALSE, (L"Local\\SallyUpdateWindow." + identity).c_str());
    struct MutexCloser { HANDLE Handle; ~MutexCloser() { if (Handle) CloseHandle(Handle); } } lifetime{single};
    if (single && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND other = FindWindowW(WindowClass.c_str(), nullptr);
        if (!s.Automatic && other) { if (IsIconic(other)) ShowWindow(other, SW_RESTORE); SetForegroundWindow(other); }
        return 0;
    }
    const std::wstring work = UpdateWorkFolder();
    if (!work.empty())
        LogPath = work + L"\\salupdate.log";
    std::wstring fileVersion;
    ReadFileVersionText(s.InstallRoot + L"\\sally.exe", fileVersion);
    s.InstalledVersion = ReleaseVersion(fileVersion);
    s.FeedUrl = command.FeedUrl.empty() ? std::wstring(kDefaultReleaseFeedUrl) : command.FeedUrl;
    Log(std::wstring(s.Automatic ? L"scheduled check" : L"check") + L" of " + s.InstallRoot + L" (" +
        s.InstalledVersion + L") at " + s.FeedUrl);

    if (s.Offline)
    {
        if (!CreateUpdateWindow()) return 2;
        s.Current = View::UpToDate;
        s.HasBundle = ReadBundledDisplayContent(s.InstallRoot, Sally::Supporter::CanonicalVersion(Narrow(s.InstalledVersion)), s.Bundle);
        SetWindowTextW(s.Window, (T(IDS_SUP_WHATS_NEW) + L" — Sally " + s.InstalledVersion).c_str());
        SetText(kIdHeadline, T(IDS_SUP_WHATS_NEW));
        SetText(kIdInstalled, Text(IDS_UPDATER_INSTALLED_VERSION, s.InstalledVersion.c_str()));
        RefreshContent(); ShowUpdateWindow(true); RunMessageLoop(); return 0;
    }
    if (s.Automatic)
    {
        // Nothing is shown unless there is a newer release.
        Win32HttpsClient client(&s.Cancel);
        if (!FetchLatestRelease(client, s.FeedUrl, Narrow(s.InstalledVersion), BuildPlatform(), s.Release,
                                s.CheckFailure))
        {
            Log(L"check failed: " + FailureText(s.CheckFailure));
            return 2;
        }
        Log(L"latest release " + Widen(s.Release.LatestVersion));
        if (!s.Release.UpdateAvailable)
            return 0;
        if (!CreateUpdateWindow())
            return 2;
        ShowRelease();
        ShowUpdateWindow(false);
        RunMessageLoop();
        return 1;
    }

    if (!CreateUpdateWindow())
        return 2;
    s.Current = View::Checking;
    SetText(kIdHeadline, Text(IDS_UPDATER_CHECKING));
    Layout();
    ShowUpdateWindow(true);
    if (!StartWorker(CheckWorker))
    {
        s.CheckFailure.What = ReleaseFailure::Kind::Network;
        s.CheckFailure.Network.Code = GetLastError();
        ShowCheckFailure();
    }
    RunMessageLoop();
    return 0;
}

// --report: how the update that left the result file went. The file is claimed by renaming it,
// so that when two Sallys start at once only one of them shows it.
int ReportUpdate(const UpdaterCommand& command)
{
    Session& s = TheSession;
    const std::wstring work = ParentFolder(command.ResultFile);
    LogPath = work + L"\\salupdate.log";
    const std::wstring claimed = work + L"\\result." + std::to_wstring(GetCurrentProcessId()) + L".shown";
    if (!MoveFileExW(Sally::ToExtendedLengthPath(command.ResultFile).c_str(),
                     Sally::ToExtendedLengthPath(claimed).c_str(), 0))
        return 0; // no result, or another Sally reports it
    UpdateResult result;
    const bool read = ReadUpdateResult(claimed, result);
    if (read && !command.InstallRoot.empty() && !SameFolder(result.InstallRoot, command.InstallRoot))
    {
        // Another Sally folder's update: leave it for that Sally.
        MoveFileExW(Sally::ToExtendedLengthPath(claimed).c_str(),
                    Sally::ToExtendedLengthPath(command.ResultFile).c_str(), 0);
        return 0;
    }
    if (read && result.Status != UpdateStatus::Prepared)
    {
        std::wstring detail = result.Message;
        if (!result.FailedPath.empty())
            detail += L"\n" + result.FailedPath;
        if (result.Code != 0)
            detail += L"\n" + SystemErrorText(result.Code);
        switch (result.Status)
        {
        case UpdateStatus::Updated:
            s.ReportText = Text(IDS_UPDATER_DONE, result.FromVersion.c_str(), result.ToVersion.c_str());
            s.ReportPage = std::wstring(kReleasesPage) + L"/tag/v" + result.ToVersion;
            break;
        case UpdateStatus::Aborted:
            s.ReportText = Text(IDS_UPDATER_ABORTED, result.ToVersion.c_str());
            break;
        default:
            s.ReportText = result.RollbackComplete
                               ? Text(IDS_UPDATER_FAILED, result.ToVersion.c_str(), detail.c_str())
                               : Text(IDS_UPDATER_INCOMPLETE, result.ToVersion.c_str(), detail.c_str());
            break;
        }
        Log(L"reporting: " + s.ReportText);
        if (CreateUpdateWindow())
        {
            s.Current = View::Report;
            SetText(kIdHeadline, s.ReportText);
            Layout();
            ShowUpdateWindow(true);
            RunMessageLoop();
        }
    }
    DeleteFileW(Sally::ToExtendedLengthPath(claimed).c_str());
    RemoveStagingFolders(work);
    return 0;
}

} // namespace salupdate
