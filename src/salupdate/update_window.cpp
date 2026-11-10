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
#include <initguid.h>
#include <oleacc.h>
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
#include "ApplicationAppearance.h"
#include "plugins/shared/plugindarkmode.h"
#include "Win32HttpsClient.h"
#include "Win32TextCodec.h"
#include "update/GitHubRelease.h"
#include "update/ReleaseChecksums.h"
#include "update/ReleaseDownload.h"
#include "update/ReleaseNotesText.h"
#include "update/RateLimit.h"
#include "update/ReleaseNotesChoice.h"
#include "update/UpdatePackage.h"
#include "update/UpdateProcesses.h"
#include "update/UpdaterCommand.h"
#include "update/UpdateAccess.h"

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
    kIdCheckUpdates = 214, kIdInvitation,
    kIdNotesVersion, kIdNotesLabel, kIdBenefit, kIdDetails,
};

enum : UINT
{
    WM_APP_CHECKED = WM_APP + 1, // wParam: TRUE when the feed answered with a release
    WM_APP_PROGRESS,             // wParam: Stage, lParam: percent or -1
    WM_APP_CONTENT_READY,
    WM_APP_APPEARANCE,
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
    bool SuccessfulReport = false;
    HWND AppearanceOwner = nullptr;
    Sally::Appearance::Snapshot Appearance;
    bool Offline = false, HasBundle = false;
    Sally::Supporter::Certificate Certificate;
    UpdateAccess Access;
    std::shared_ptr<ManagedUpdateAttempt> Attempt;
    bool LayingOut = false;
    uint64_t NextContentTime = 0;
    std::wstring Profile;
    std::string Tag = "en-US";
    DisplayContent Bundle; // Installed-version content is retained across network checks.
    ReleaseNotesChoice NotesChoice;
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
    HFONT Heading = NULL;
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

// GitHub limits anonymous checks per network address. Say so, with the local time it
// accepts them again when it said, instead of a bare "HTTP 403".
std::wstring RateLimitText(unsigned long long retryAfterSeconds)
{
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER at{};
    at.LowPart = now.dwLowDateTime;
    at.HighPart = now.dwHighDateTime;
    at.QuadPart = RetryShownAt(at.QuadPart, retryAfterSeconds);
    if (at.QuadPart == 0)
        return Text(IDS_UPDATER_RATE_LIMITED);
    const FILETIME utc{at.LowPart, at.HighPart};
    SYSTEMTIME universal{}, local{};
    if (!FileTimeToSystemTime(&utc, &universal) || !SystemTimeToTzSpecificLocalTime(NULL, &universal, &local))
        return Text(IDS_UPDATER_RATE_LIMITED);
    const int length = GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, NULL, NULL, 0);
    if (length <= 0)
        return Text(IDS_UPDATER_RATE_LIMITED);
    std::wstring time(static_cast<size_t>(length), L'\0');
    if (GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, NULL, &time[0], length) <= 0)
        return Text(IDS_UPDATER_RATE_LIMITED);
    time.resize(static_cast<size_t>(length - 1));
    return Text(IDS_UPDATER_RATE_LIMITED_UNTIL, time.c_str());
}

std::wstring FailureText(const ReleaseFailure& failure)
{
    switch (failure.What)
    {
    case ReleaseFailure::Kind::Network:
        if (failure.Network.RateLimited)
            return RateLimitText(failure.Network.RetryAfterSeconds);
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

struct InstallWork
{
    std::shared_ptr<ManagedUpdateAttempt> Attempt;
    std::wstring Staging, Version;
    HWND Window;
};

DWORD WINAPI InstallWorker(void* parameter)
{
    std::unique_ptr<InstallWork> work(static_cast<InstallWork*>(parameter));
    if (!work) return 0;
    const auto attempt = work->Attempt;
    RunManagedUpdate(attempt, [&](const ManagedUpdateAttempt& accepted) {
        ManagedUpdateResult outcome;
        // No staging, sink opening, checksum or package request precedes admission.
        CreateDirectoryW(Sally::ToExtendedLengthPath(work->Staging).c_str(), NULL);
        const std::wstring package = work->Staging + L"\\" + Widen(accepted.Release.PrimaryAssetName);
        FilePackageSink sink(package, work->Window);
        DWORD openError = 0;
        if (!sink.Open(openError))
            outcome.Error = package + L"\n" + SystemErrorText(openError);
        else
        {
            Win32HttpsClient client(&TheSession.Cancel);
            int reported = -1;
            std::string sha256;
            ReleaseFailure failure;
            const bool downloaded = DownloadReleasePackage(client, accepted.Release, sink,
                [&](unsigned long long received, unsigned long long total) {
                    const int percent = total > 0 ? static_cast<int>(received * 100 / total) : -1;
                    if (percent != reported) { reported = percent; PostMessageW(work->Window, WM_APP_PROGRESS, kStageDownloading, percent); }
                }, sha256, failure);
            if (!downloaded) outcome.Error = FailureText(failure);
            else
            {
                PostMessageW(work->Window, WM_APP_PROGRESS, kStagePreparing, -1);
                UpdaterCommand prepare;
                prepare.Mode = UpdaterMode::Prepare;
                prepare.Package = package; prepare.Sha256 = Widen(sha256);
                prepare.Staging = work->Staging; prepare.Version = work->Version; prepare.Arch = accepted.Arch;
                UpdateResult result;
                outcome.Prepared = PrepareRelease(prepare, result);
                if (!outcome.Prepared)
                {
                    outcome.Error = result.Message;
                    if (!result.FailedPath.empty()) outcome.Error += L"\n" + result.FailedPath;
                    if (result.Code != 0) outcome.Error += L"\n" + SystemErrorText(result.Code);
                }
                DeleteFileW(Sally::ToExtendedLengthPath(package).c_str());
            }
        }
        return outcome;
    });
    if (attempt) PostMessageW(work->Window, WM_APP_PREPARED, static_cast<WPARAM>(attempt->Id), 0);
    return 0;
}

bool StartWorker(LPTHREAD_START_ROUTINE routine, void* parameter = nullptr)
{
    HANDLE thread = CreateThread(NULL, 0, routine, parameter, 0, NULL);
    if (thread == NULL) return false;
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
    if (visible)
        SetWindowPos(control, NULL, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
}

std::wstring T(UINT id) { return Sally::Supporter::SupporterText(id, Language); }

int DefaultAction()
{
    if (TheSession.Current == View::CheckFailed && IsWindowVisible(Control(kIdCheckUpdates)) && IsWindowEnabled(Control(kIdCheckUpdates))) return kIdCheckUpdates;
    for (int id : std::initializer_list<int>{kIdInstall, kIdReleasePage, kIdCheckUpdates, IDCANCEL})
        if (IsWindowVisible(Control(id)) && IsWindowEnabled(Control(id))) return id;
    return IDCANCEL;
}


void Layout()
{
    Session& s = TheSession;
    if (s.LayingOut || !s.Window) return;
    s.LayingOut = true;
    const HWND previousFocus = GetFocus();
    const bool ownedFocus = previousFocus && IsChild(s.Window, previousFocus);
    const bool release = s.Current != View::Report && !s.Installing && !s.InstallStopped;
    const bool checked = s.Current == View::Newer || s.Current == View::UpToDate;
    const bool readingVisible = release && s.Reading.Content.Visible;
    const int kind = s.SuccessfulReport && s.Current == View::Report ? 3 : release ? (readingVisible ? 2 : 1) : 0;
    const bool firstPlacement = !s.Placed;
    const bool contentSizeChanged = firstPlacement || s.LayoutKind != kind;
    if (contentSizeChanged)
    {
        s.MinimumHeight = kind == 2 ? 480 : kind == 3 ? 80 : 220;
        POINT cursor{}; GetCursorPos(&cursor);
        MONITORINFO monitor = {sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY), &monitor);
        const RECT& area = monitor.rcWork;
        const int fontLine = TextHeight(s.Font, L"Ag", 1000);
        const int preferredWidth = (std::max)(Px(kind == 3 ? 560 : 700), fontLine * 32);
        const int preferredHeight = kind == 2 ? (std::max)(Px(600), fontLine * 18) : Px(kind == 3 ? 180 : 260);
        int width = (std::min)(preferredWidth, static_cast<int>(area.right - area.left) - Px(12));
        int height = (std::min)(preferredHeight, static_cast<int>(area.bottom - area.top) - Px(20));
        s.LayoutKind = kind;
        RECT previous{}; GetWindowRect(s.Window, &previous);
        SetWindowPos(s.Window, nullptr, firstPlacement ? area.left + (area.right - area.left - width) / 2 : previous.left,
            firstPlacement ? area.top + (area.bottom - area.top - height) / 3 : previous.top,
            firstPlacement ? width : previous.right - previous.left, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        s.Placed = true;
    }
    RECT client{}; GetClientRect(s.Window, &client);
    const int margin = Px(24), width = (std::max)(Px(100), static_cast<int>(client.right) - 2 * margin);
    const int line = TextHeight(s.Font, L"Ag", width), buttonHeight = (std::max)(Px(26), line + Px(10));
    int y = margin;
    int headline = TextHeight(s.Heading, GetText(Control(kIdHeadline)), width);
    Place(kIdHeadline, true, margin, y, width, headline); y += headline + Px(8);
    const bool versions = release && checked && !s.Offline;
    const int installedWidth = ButtonWidth(kIdInstalled), latestWidth = ButtonWidth(kIdLatest);
    const bool inlineVersions = versions && installedWidth + latestWidth + Px(16) <= width;
    Place(kIdInstalled, release, margin, y, inlineVersions ? installedWidth : width, line);
    if (release && !inlineVersions) y += line + Px(3);
    Place(kIdLatest, versions, inlineVersions ? margin + installedWidth + Px(16) : margin, y,
        inlineVersions ? width - installedWidth - Px(16) : width, line);
    if (versions || inlineVersions) y += line + Px(6);
    const bool notesChoice = SendMessageW(Control(kIdNotesVersion), CB_GETCOUNT, 0, 0) > 1;
    const int gap = Px(8), labelWidth = ButtonWidth(kIdNotesLabel);
    Place(kIdNotesLabel, release && notesChoice, margin, y + Px(5), labelWidth, line);
    Place(kIdNotesVersion, release && notesChoice, margin + labelWidth + gap, y,
        (std::max)(Px(80), width - labelWidth - gap), buttonHeight * 8);
    if (release && notesChoice) y += buttonHeight + Px(8);
    bool status = !GetText(Control(kIdStatus)).empty();
    int statusHeight = status ? TextHeight(s.Font, GetText(Control(kIdStatus)), width) + Px(4) : 0;
    const bool support = release;
    const bool active = s.Certificate.State == Sally::Supporter::Status::Active;
    const bool future = s.Certificate.State == Sally::Supporter::Status::NotYetValid;
    const int headingHeight = support ? TextHeight(s.Bold, GetText(Control(kIdBenefit)), width) + Px(6) : 0;
    const int invitationHeight = support ? TextHeight(s.Font, GetText(Control(kIdInvitation)), width) + Px(10) : 0;
    const bool install = s.Current == View::Newer && !s.Offline;
    const bool page = release && !active && (install || s.Current == View::CheckFailed);
    std::vector<int> links;
    if (support && !active && !future) links.push_back(kIdSupport);
    if (support && !active) links.push_back(kIdSupporter);
    if (support && s.Certificate.Authentic()) links.push_back(kIdDetails);
    std::vector<RECT> linkBounds;
    int linkX = 0, linkY = 0, rowHeight = 0;
    for (int id : links)
    {
        auto text = GetText(Control(id));
        if (text.size() >= 7) text = text.substr(3, text.size() - 7);
        for (size_t at = 0; (at = text.find(L"&amp;", at)) != std::wstring::npos; ++at) text.replace(at, 5, L"&");
        HDC dc = GetDC(s.Window); auto old = SelectObject(dc, s.Font); SIZE extent{};
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &extent);
        SelectObject(dc, old); ReleaseDC(s.Window, dc);
        const int linkWidth = (std::min)(width, static_cast<int>(extent.cx) + Px(4));
        if (linkX && linkX + linkWidth > width) { linkX = 0; linkY += rowHeight + Px(6); rowHeight = 0; }
        SIZE ideal{}; SendMessageW(Control(id), LM_GETIDEALSIZE, linkWidth, reinterpret_cast<LPARAM>(&ideal));
        const int linkHeight = (std::max)(line + Px(4), static_cast<int>(ideal.cy));
        linkBounds.push_back({linkX, linkY, linkWidth, linkHeight});
        linkX += linkWidth + Px(16); rowHeight = (std::max)(rowHeight, linkHeight);
    }
    const int linksHeight = links.empty() ? 0 : linkY + rowHeight + Px(12);
    std::vector<int> footer;
    if (install) footer.push_back(kIdInstall);
    if (page) footer.push_back(kIdReleasePage);
    if (release && s.Current != View::Checking && !install)
    {
        SetText(kIdCheckUpdates, T(s.Current == View::CheckFailed ? IDS_SUP_TRY_AGAIN : IDS_SUP_CHECK_UPDATES));
        footer.insert(footer.begin(), kIdCheckUpdates);
    }
    footer.push_back(IDCANCEL);
    int footerWidth = Px(8) * static_cast<int>(footer.size() - 1);
    for (int id : footer) footerWidth += ButtonWidth(id);
    const bool footerFits = footerWidth <= width;
    const int footerRows = footerFits ? 1 : static_cast<int>(footer.size());
    int reserved = headingHeight + invitationHeight + linksHeight + statusHeight + footerRows * (buttonHeight + Px(6)) + margin;
    // State/text changes can add rows without changing the reading-surface kind.
    // Measure those rows before placing the content and footer, also after a resize.
    RECT outer{}; GetWindowRect(s.Window, &outer);
    MONITORINFO monitor = {sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(s.Window, MONITOR_DEFAULTTONEAREST), &monitor);
    const int chrome = static_cast<int>(outer.bottom - outer.top - client.bottom);
    const int needed = y + reserved + Px(6) + (readingVisible ? (std::max)(Px(80), line * 3) : 0) + chrome;
    const int minimum = (std::min)(needed, static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top) - Px(20));
    s.MinimumHeight = MulDiv(minimum, 96, s.Dpi);
    const int readingBaseline = (std::max)(Px(207), line * 8);
    const int preferred = (std::min)(y + reserved + Px(6) + readingBaseline + chrome,
        static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top) - Px(20));
    const int height = contentSizeChanged ? (kind == 2 ? (std::max)(minimum, preferred) : minimum)
                       : (std::max)(minimum, static_cast<int>(outer.bottom - outer.top));
    if (height != outer.bottom - outer.top)
    {
        SetWindowPos(s.Window, nullptr, 0, 0, outer.right - outer.left, height,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        GetClientRect(s.Window, &client);
    }
    int reading = readingVisible ? (std::max)(0, static_cast<int>(client.bottom) - y - reserved - Px(6)) : 0;
    Place(kIdNotes, readingVisible, margin, y, width, reading);
    if (readingVisible) y += reading + Px(6);
    Place(kIdStatus, status, margin, y, width, statusHeight); y += statusHeight;
    Place(kIdBenefit, support, margin, y, width, headingHeight); y += headingHeight;
    Place(kIdInvitation, support, margin, y, width, invitationHeight); y += invitationHeight;
    for (int id : {kIdSupport, kIdSupporter, kIdDetails}) ShowWindow(Control(id), SW_HIDE);
    for (size_t i = 0; i < links.size(); ++i)
    {
        const auto& bounds = linkBounds[i];
        Place(links[i], true, margin + bounds.left, y + bounds.top, bounds.right, bounds.bottom);
    }
    y += linksHeight;
    for (int id : std::initializer_list<int>{kIdInstall, kIdReleasePage, kIdCheckUpdates, IDCANCEL})
        if (std::find(footer.begin(), footer.end(), id) == footer.end()) ShowWindow(Control(id), SW_HIDE);
    int footerX = footerFits ? margin + width - footerWidth : margin;
    for (size_t i = 0; i < footer.size(); ++i)
    {
        const int buttonWidth = footerFits ? ButtonWidth(footer[i]) : width;
        Place(footer[i], true, footerFits ? footerX : margin,
            footerFits ? y : y + static_cast<int>(i) * (buttonHeight + Px(6)), buttonWidth, buttonHeight);
        footerX += buttonWidth + Px(8);
    }
    EnableWindow(Control(kIdInstall), install && s.Access.Allowed() && !s.Installing);
    const int defaultId = s.Current == View::CheckFailed ? kIdCheckUpdates : install && s.Access.Allowed() ? kIdInstall : page ? kIdReleasePage :
        release && s.Current != View::Checking ? kIdCheckUpdates : IDCANCEL;
    for (int id : std::initializer_list<int>{kIdInstall, kIdReleasePage, kIdCheckUpdates, IDCANCEL})
        SendMessageW(Control(id), BM_SETSTYLE, id == defaultId ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE);
    s.LayingOut = false;
    if (ownedFocus && (!IsWindowVisible(previousFocus) || !IsWindowEnabled(previousFocus))) SetFocus(Control(DefaultAction()));
    RedrawWindow(s.Window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
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
    format.dwMask = CFM_BOLD | CFM_ITALIC | CFM_LINK | CFM_COLOR | CFM_FACE | CFM_SIZE;
    PluginDarkModeColors colors{}; const bool dark = PluginDarkMode_GetColors(&colors);
    SendMessageW(control, EM_SETBKGNDCOLOR, 0, dark ? colors.InputBackground : GetSysColor(COLOR_WINDOW));
    format.crTextColor = dark ? colors.InputText : GetSysColor(COLOR_WINDOWTEXT);
    LOGFONTW font{}; GetObjectW(TheSession.Font, sizeof(font), &font);
    format.yHeight = MulDiv(font.lfHeight < 0 ? -font.lfHeight : font.lfHeight, 1440, TheSession.Dpi);
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
        if (!run.Link.empty()) style.crTextColor = PluginDarkMode_GetLinkColor();
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

void RefreshSupportState()
{
    Session& s = TheSession;
    const uint64_t now = Sally::Supporter::NowUtc();
    auto preferences = Sally::Supporter::ReadSupporterState(s.Profile, *GetWin32Registry());
    auto certificate = Sally::Supporter::VerifyCertificate(preferences.Token, now, Sally::Supporter::BuiltinPublicKeys());
    if (s.Certificate.State != certificate.State) s.LayoutKind = -1;
    s.Certificate = certificate;
    s.Access = EvaluateUpdateAccess(certificate, now, s.Release.UpdateAvailable, s.Release.CanInstall);
    const bool active = certificate.State == Sally::Supporter::Status::Active;
    UINT heading = active ? IDS_SUP_AUTO_ACTIVE : certificate.State == Sally::Supporter::Status::Expired ? IDS_SUP_AUTO_RENEW_TITLE : IDS_SUP_AUTO_TITLE;
    UINT explanation = certificate.State == Sally::Supporter::Status::Expired ? IDS_SUP_AUTO_EXPIRED_HINT :
        certificate.State == Sally::Supporter::Status::NotYetValid ? IDS_SUP_AUTO_FUTURE_HINT :
        certificate.State == Sally::Supporter::Status::Invalid ? IDS_SUP_AUTO_INVALID_HINT : IDS_SUP_AUTO_HINT;
    auto title = T(heading), hint = T(explanation);
    if (certificate.State == Sally::Supporter::Status::NotYetValid)
    {
        title = T(IDS_SUP_AUTO_START); title.replace(title.find(L"%s"), 2, Sally::Supporter::SupporterUtcDate(certificate.ValidFrom));
    }
    if (active)
    {
        hint = T(IDS_SUP_AUTO_END); hint.replace(hint.find(L"%s"), 2, Sally::Supporter::SupporterUtcDate(certificate.ValidUntil));
    }
    SetText(kIdBenefit, title); SetText(kIdInvitation, hint);
    // Expose the same unavailable reason to assistive technology as to sighted users.
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IAccPropServices* accessibility = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&accessibility))))
    {
        const auto description = active && !s.Release.CanInstall && s.Release.UpdateAvailable ? Text(IDS_UPDATER_NOT_INSTALLABLE) : title + L" " + hint;
        accessibility->SetHwndPropStr(Control(kIdInstall), OBJID_CLIENT, CHILDID_SELF, PROPID_ACC_DESCRIPTION, description.c_str());
        accessibility->Release();
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    auto link = [&](int id, UINT resource) {
        auto label = T(resource);
        for (size_t at = 0; (at = label.find(L'&', at)) != std::wstring::npos; at += 5) label.replace(at, 1, L"&amp;");
        SetText(id, L"<a>" + label + L"</a>");
    };
    link(kIdSupport, certificate.State == Sally::Supporter::Status::Expired ? IDS_SUP_AUTO_RENEW : IDS_SUP_AUTO_SUPPORT);
    link(kIdSupporter, IDS_SUP_ENTER); link(kIdDetails, IDS_SUP_DETAILS);
    SetText(kIdReleasePage, T(IDS_SUP_DOWNLOAD_MANUAL));
    s.NextContentTime = 0;
    auto boundary = [&](uint64_t value) { if (value > now && (!s.NextContentTime || value < s.NextContentTime)) s.NextContentTime = value; };
    boundary(certificate.ValidFrom); boundary(certificate.ValidUntil);
}

void RefreshContent()
{
    Session& s = TheSession;
    const uint64_t now = Sally::Supporter::NowUtc();
    const bool installed = s.NotesChoice.IsInstalled();
    const bool checked = s.Current == View::Newer || s.Current == View::UpToDate;
    const bool matchingRelease = checked && Sally::Supporter::CanonicalVersion(s.Release.LatestVersion) == Sally::Supporter::CanonicalVersion(Narrow(s.InstalledVersion));
    DisplayContent release = (installed || matchingRelease) && s.HasBundle ? s.Bundle : DisplayContent{}, live;
    bool hasLive = false;
    if (s.Content)
    {
        std::lock_guard<std::mutex> lock(s.Content->Mutex);
        if (s.Content->HasRelease && (!installed || matchingRelease)) release = s.Content->Release;
        if (s.Content->HasLive) { live = s.Content->Live; hasLive = true; }
    }
    std::string target = Sally::Supporter::CanonicalVersion(installed ? Narrow(s.InstalledVersion) : s.Release.LatestVersion);
    auto notes = SelectReleaseNotes(release, s.Tag, checked && (!installed || matchingRelease) ? s.Release.ReleaseNotes : "");
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
    RefreshSupportState();
    auto boundary = [&](uint64_t value) { if (value > now && (!s.NextContentTime || value < s.NextContentTime)) s.NextContentTime = value; };
    if (hasLive) boundary(live.Until);
    for (const auto* model : {&release, &live}) for (const auto& notice : model->Notices) { boundary(notice.From); boundary(notice.Until); }
    std::wstring status;
    if (!s.NotesDisplay.Visible && !s.MaintainerDisplay.Visible) status = T(IDS_SUP_NO_NOTES);
    if (checked && s.Release.UpdateAvailable && !s.Release.CanInstall) status = Text(IDS_UPDATER_NOT_INSTALLABLE);
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
    const bool initialDefault = GetFocus() == Control(IDCANCEL) || GetFocus() == Control(kIdCheckUpdates);
    s.Current = release.UpdateAvailable ? View::Newer : View::UpToDate;
    s.InstallStopped = false;
    s.LayoutKind = -1;
    SetText(kIdHeadline, release.UpdateAvailable ? Text(IDS_UPDATER_NEWER, PlainVersion(release.LatestVersion).c_str()) : Text(IDS_UPDATER_LATEST));
    SetText(kIdInstalled, Text(IDS_UPDATER_INSTALLED_VERSION, s.InstalledVersion.c_str()));
    SetText(kIdLatest, Text(IDS_UPDATER_RELEASE_VERSION, PlainVersion(release.LatestVersion).c_str()));
    s.NotesChoice.CompleteCheck(release.UpdateAvailable);
    SendMessageW(Control(kIdNotesVersion), CB_RESETCONTENT, 0, 0);
    if (release.UpdateAvailable)
    {
        auto latest = T(IDS_SUP_NOTES_LATEST); latest.replace(latest.find(L"%s"), 2, PlainVersion(release.LatestVersion));
        SendMessageW(Control(kIdNotesVersion), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(latest.c_str()));
    }
    auto installed = T(IDS_SUP_NOTES_INSTALLED); installed.replace(installed.find(L"%s"), 2, s.InstalledVersion);
    SendMessageW(Control(kIdNotesVersion), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(installed.c_str()));
    SendMessageW(Control(kIdNotesVersion), CB_SETCURSEL, 0, 0);
    EnableWindow(Control(kIdNotesVersion), release.UpdateAvailable);
    SetStatus(release.UpdateAvailable && !release.CanInstall ? Text(IDS_UPDATER_NOT_INSTALLABLE) : std::wstring());
    RefreshContent();
    if (initialDefault) SetFocus(Control(DefaultAction()));
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
    const bool initialDefault = GetFocus() == Control(IDCANCEL) || GetFocus() == Control(kIdCheckUpdates);
    s.Current = View::CheckFailed;
    SetText(kIdHeadline, Text(IDS_UPDATER_CHECK_FAILED, FailureText(s.CheckFailure).c_str()));
    RefreshContent();
    if (initialDefault) SetFocus(Control(DefaultAction()));
}

void BeginCheck()
{
    Session& s = TheSession;
    if (s.Installing || s.Current == View::Checking) return;
    if (s.Content) { s.Content->Cancel = true; s.Content.reset(); }
    s.NotesChoice.BeginCheck();
    s.Current = View::Checking;
    s.InstallStopped = false;
    s.Offline = false;
    SetWindowTextW(s.Window, Text(IDS_UPDATER_TITLE).c_str());
    SetText(kIdCheckUpdates, T(IDS_SUP_RECHECK));
    SendMessageW(Control(kIdNotesVersion), CB_RESETCONTENT, 0, 0);
    auto label = T(IDS_SUP_NOTES_INSTALLED); label.replace(label.find(L"%s"), 2, s.InstalledVersion);
    SendMessageW(Control(kIdNotesVersion), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    SendMessageW(Control(kIdNotesVersion), CB_SETCURSEL, 0, 0);
    EnableWindow(Control(kIdNotesVersion), FALSE);
    SetText(kIdHeadline, Text(IDS_UPDATER_CHECKING));
    SetText(kIdInstalled, Text(IDS_UPDATER_INSTALLED_VERSION, s.InstalledVersion.c_str()));
    RefreshContent();
    if (!StartWorker(CheckWorker))
    {
        s.CheckFailure = ReleaseFailure(); // not an earlier check's rate limit
        s.CheckFailure.What = ReleaseFailure::Kind::Network;
        s.CheckFailure.Network.Code = GetLastError();
        ShowCheckFailure();
    }
}

void StartInstall()
{
    Session& s = TheSession;
    if (s.Installing || s.Current != View::Newer || s.Offline) return;
    s.Attempt = ManagedUpdateAttempt::AcceptForProfile(*GetWin32Registry(), s.Profile, Sally::Supporter::NowUtc(),
        Sally::Supporter::BuiltinPublicKeys(), s.Release, s.InstallRoot, Widen(GetPlatformLabel(BuildPlatform())));
    if (!s.Attempt) { RefreshContent(); return; }
    s.WorkFolder = UpdateWorkFolder();
    if (s.WorkFolder.empty())
    {
        SetStatus(Text(IDS_UPDATER_STOPPED, L"%LOCALAPPDATA%"));
        return;
    }
    s.Version = PlainVersion(s.Attempt->Release.LatestVersion);
    s.Arch = s.Attempt->Arch;
    s.Staging = StagingFolderFor(s.WorkFolder, s.Version, s.Arch);
    s.Cancel = false;
    s.Installing = true;
    s.InstallStopped = false;
    if (s.Content) s.Content->Cancel = true;
    SetStatus(Text(IDS_UPDATER_DOWNLOADING, Widen(s.Release.PrimaryAssetName).c_str()));
    Layout();
    auto work = std::make_unique<InstallWork>(InstallWork{s.Attempt, s.Staging, s.Version, s.Window});
    if (StartWorker(InstallWorker, work.get())) work.release();
    else
    {
        s.Attempt->Cancel();
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
    const auto accepted = s.Attempt;
    const HWND owner = s.Window;
    auto live = [&] { return !s.Cancel && s.Installing && s.Attempt == accepted && accepted && s.Profile == accepted->Profile && s.InstallRoot == accepted->InstallRoot && s.Window == owner && IsWindow(owner); };
    if (!live()) return;
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
    // Modal confirmation pumps messages: closure/cancellation wins over its eventual answer.
    if (!live()) return;
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

void ApplyAppearance(const Sally::Appearance::Snapshot& value)
{
    Session& s = TheSession;
    s.Appearance = value;
    Sally::Appearance::SetWindowIcon(s.Window, GetModuleHandleW(nullptr), value.Icon);
    LOGFONTW font = Sally::Appearance::FontForDpi(value, s.Dpi);
    HFONT normal = CreateFontIndirectW(&font);
    font.lfWeight = FW_BOLD;
    HFONT bold = CreateFontIndirectW(&font);
    font.lfHeight = MulDiv(font.lfHeight, 18, 13);
    HFONT heading = CreateFontIndirectW(&font);
    if (!normal || !bold || !heading) { if (normal) DeleteObject(normal); if (bold) DeleteObject(bold); if (heading) DeleteObject(heading); return; }
    HFONT old = s.Font, oldBold = s.Bold, oldHeading = s.Heading;
    s.Font = normal; s.Bold = bold; s.Heading = heading;
    EnumChildWindows(s.Window, [](HWND child, LPARAM) -> BOOL
    {
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(
            GetDlgCtrlID(child) == kIdHeadline ? TheSession.Heading : GetDlgCtrlID(child) == kIdBenefit ? TheSession.Bold : TheSession.Font), TRUE);
        return TRUE;
    }, 0);
    // Render preserves reading selection/scroll and reapplies rich-text font runs.
    Render(s.Reading, s.Reading);
    if (old) DeleteObject(old);
    if (oldBold) DeleteObject(oldBold);
    if (oldHeading) DeleteObject(oldHeading);
    PluginDarkMode_ApplyTitleBar(s.Window); PluginDarkMode_ApplyListTreeThemeRecursive(s.Window);
    s.LayoutKind = -1;
    Layout();
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    Session& s = TheSession;
    switch (message)
    {
    case WM_APP_APPEARANCE:
        Sally::Appearance::Request(window, s.AppearanceOwner);
        return 0;
    case WM_COPYDATA:
    {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (data && data->dwData == Sally::Appearance::CopyDataId)
        {
            Sally::Appearance::Snapshot value;
            if (!Sally::Appearance::Decode(data->lpData, data->cbData, value))
            { Log(L"appearance snapshot rejected: invalid payload"); return FALSE; }
            if (!Sally::Appearance::Matches(value, s.Profile, s.InstallRoot))
            { Log(L"appearance snapshot rejected: different profile or installation"); return FALSE; }
            Log(L"appearance snapshot icon " + std::to_wstring(value.Icon));
            ApplyAppearance(value);
            return TRUE;
        }
        break;
    }
    case WM_DPICHANGED:
    {
        s.Dpi = HIWORD(wParam);
        const auto bounds = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window, nullptr, bounds->left, bounds->top, bounds->right - bounds->left, bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
        ApplyAppearance(s.Appearance); return 0;
    }
    case WM_SETTINGCHANGE:
        PluginDarkMode_OnSettingChange(lParam);
        [[fallthrough]];
    case WM_SYSCOLORCHANGE:
    case WM_THEMECHANGED:
        PluginDarkMode_ApplyTitleBar(window); PluginDarkMode_ApplyListTreeThemeRecursive(window);
        Render(s.Reading, s.Reading); Layout(); return 0;
    case WM_SIZE:
        if (s.Placed) Layout();
        return 0;
    case WM_GETMINMAXINFO:
    {
        auto info = reinterpret_cast<MINMAXINFO*>(lParam); info->ptMinTrackSize = {Px(420), Px(s.MinimumHeight)}; return 0;
    }
    case WM_TIMER:
        if (!s.Installing && !s.InstallStopped && s.NextContentTime && Sally::Supporter::NowUtc() >= s.NextContentTime)
        { if (s.Current == View::Report) { RefreshSupportState(); s.LayoutKind = -1; Layout(); } else RefreshContent(); }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE && s.Current == View::Report && s.SuccessfulReport)
        { RefreshSupportState(); s.LayoutKind = -1; Layout(); }
        if (LOWORD(wParam) != WA_INACTIVE && !s.Installing && !s.InstallStopped && s.Current != View::Report) RefreshContent();
        break;
    case WM_APP_CONTENT_READY:
        if (!s.Installing && !s.InstallStopped && s.Content && !s.Content->Cancel) RefreshContent();
        return 0;
    case WM_NOTIFY:
    {
        auto header = reinterpret_cast<NMHDR*>(lParam);
        if (header && header->code == NM_CUSTOMDRAW && (header->idFrom == kIdSupport || header->idFrom == kIdSupporter || header->idFrom == kIdDetails)) return PluginDarkMode_DrawLink(lParam);
        if (header && (header->code == NM_CLICK || header->code == NM_RETURN) &&
            (header->idFrom == kIdSupport || header->idFrom == kIdSupporter || header->idFrom == kIdDetails))
        {
            SendMessageW(window, WM_COMMAND, header->idFrom, 0);
            SetFocus(Control(DefaultAction()));
            return 0;
        }
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
        case kIdSupport:
            Sally::Supporter::OpenSupportPage(window);
            if (s.Current == View::Report) { RefreshSupportState(); Layout(); } else RefreshContent();
            return 0;
        case kIdDetails:
        case kIdSupporter:
            Sally::Supporter::ShowSupporterDialog(window, Language, s.Profile, *GetWin32Registry(), LOWORD(wParam) == kIdSupporter);
            if (s.Current == View::Report) { RefreshSupportState(); s.LayoutKind = -1; Layout(); } else RefreshContent();
            return 0;
        case kIdCheckUpdates:
            BeginCheck();
            return 0;
        case kIdNotesVersion:
            if (HIWORD(wParam) == CBN_SELCHANGE && s.NotesChoice.CanSelectLatest())
            {
                s.NotesChoice.SelectInstalled(SendMessageW(Control(kIdNotesVersion), CB_GETCURSEL, 0, 0) == 1);
                s.Reading = {};
                RefreshContent();
                SendMessageW(Control(kIdNotes), EM_SETSEL, 0, 0);
                POINT origin{};
                SendMessageW(Control(kIdNotes), EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&origin));
            }
            return 0;
        case kIdInstall:
            StartInstall();
            return 0;
        case kIdReleasePage:
            if (s.Current == View::Report)
                OpenPage(s.ReportPage);
            else
                // The free update path always targets the offered release, even while
                // the reading selector is showing installed-version notes.
                OpenPage(Widen(s.Release.ReleasePageUrl).empty() ? kReleasesPage : Widen(s.Release.ReleasePageUrl));
            return 0;
        case IDCANCEL:
            SendMessageW(window, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case DM_GETDEFID:
        return MAKELRESULT(DefaultAction(), DC_HASDEFID);
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
    {
        ManagedUpdateResult result;
        if (s.Installing && s.Attempt && s.Attempt->TakeCompletion(static_cast<uint64_t>(wParam), result))
        {
            s.InstallError = std::move(result.Error);
            FinishInstall(result.Prepared);
        }
        return 0;
    }
    case WM_ERASEBKGND:
    { RECT bounds{}; GetClientRect(window, &bounds); if (PluginDarkMode_FillDialogBackground(reinterpret_cast<HDC>(wParam), &bounds)) return TRUE; break; }
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    { auto brush = PluginDarkMode_GetDialogCtlColorBrush(message, reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam)); if (brush) return reinterpret_cast<LRESULT>(brush); break; }
    case WM_CTLCOLORSTATIC:
        if (auto brush = PluginDarkMode_GetDialogCtlColorBrush(message, reinterpret_cast<HDC>(wParam), reinterpret_cast<HWND>(lParam))) return reinterpret_cast<LRESULT>(brush);
        if (reinterpret_cast<HWND>(lParam) == Control(kIdNotes))
        {
            SetBkColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOW));
            SetTextColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOWTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        break;
    case WM_CLOSE:
        if (s.Content) s.Content->Cancel = true;
        if (s.Attempt) s.Attempt->Cancel();
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
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_LINK_CLASS};
    if (!InitCommonControlsEx(&controls)) return false;
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
    if (s.Profile.empty()) s.Profile = Sally::Supporter::ResolveProfileRoot();
    PluginDarkMode_SetProfileRoot(s.Profile.c_str()); PluginDarkMode_Initialize();
    s.Appearance = Sally::Appearance::Read(s.Profile, s.InstallRoot, *GetWin32Registry());
    LOGFONTW font = Sally::Appearance::FontForDpi(s.Appearance, s.Dpi);
    s.Font = CreateFontIndirectW(&font);
    LOGFONTW bold = font;
    bold.lfWeight = FW_BOLD;
    s.Bold = CreateFontIndirectW(&bold);
    bold.lfHeight = MulDiv(bold.lfHeight, 18, 13); s.Heading = CreateFontIndirectW(&bold);

    s.Window = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, WindowClass.c_str(), Text(IDS_UPDATER_TITLE).c_str(),
                               WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                               Px(600), Px(400), NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (s.Window == NULL)
        return false;
    Sally::Appearance::SetWindowIcon(s.Window, GetModuleHandleW(nullptr), s.Appearance.Icon);
    const DWORD label = SS_LEFT | SS_NOPREFIX;
    AddControl(L"STATIC", kIdHeadline, label, 0, std::wstring(), s.Heading);
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
    AddControl(L"STATIC", kIdInvitation, label, 0, T(IDS_SUP_INVITATION), s.Font);
    AddControl(L"STATIC", kIdBenefit, label, 0, std::wstring(), s.Bold);
    AddControl(WC_LINK, kIdSupport, WS_TABSTOP, 0, std::wstring(), s.Font);
    AddControl(WC_LINK, kIdSupporter, WS_TABSTOP, 0, std::wstring(), s.Font);
    AddControl(WC_LINK, kIdDetails, WS_TABSTOP, 0, std::wstring(), s.Font);
    if (!Control(kIdSupport) || !Control(kIdSupporter) || !Control(kIdDetails)) { DestroyWindow(s.Window); s.Window = nullptr; return false; }
    AddControl(L"STATIC", kIdNotesLabel, label, 0, T(IDS_SUP_RELEASE_NOTES), s.Font);
    AddControl(WC_COMBOBOXW, kIdNotesVersion, WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, 0, L"", s.Font);
    AddControl(L"BUTTON", kIdCheckUpdates, WS_TABSTOP, 0, T(s.Offline ? IDS_SUP_CHECK_UPDATES : IDS_SUP_RECHECK), s.Font);
    AddControl(L"STATIC", kIdStatus, label, 0, std::wstring(), s.Font);
    AddControl(L"BUTTON", kIdInstall, WS_TABSTOP | BS_PUSHBUTTON, 0, Text(IDS_UPDATER_INSTALL_BUTTON), s.Font);
    AddControl(L"BUTTON", kIdReleasePage, WS_TABSTOP | BS_PUSHBUTTON, 0, Text(IDS_UPDATER_RELEASE_PAGE_BUTTON),
               s.Font);
    AddControl(L"BUTTON", IDCANCEL, WS_TABSTOP | BS_PUSHBUTTON, 0, Text(IDS_UPDATER_CLOSE_BUTTON), s.Font);
    PluginDarkMode_ApplyTitleBar(s.Window); PluginDarkMode_ApplyListTreeThemeRecursive(s.Window);
    // Date boundaries apply equally to offline bundled and online content.
    SetTimer(s.Window, 1, 1000, nullptr);
    // Request after initialization, when the normal message loop can receive the reply.
    PostMessageW(s.Window, WM_APP_APPEARANCE, 0, 0);
    return true;
}

void ShowUpdateWindow(bool activate)
{
    Session& s = TheSession;
    if (activate)
    {
        ShowWindow(s.Window, SW_SHOWNORMAL);
        SetForegroundWindow(s.Window);
        SetFocus(Control(DefaultAction()));
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
    s.AppearanceOwner = reinterpret_cast<HWND>(static_cast<UINT_PTR>(command.AppearanceOwner));
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
    s.HasBundle = ReadBundledDisplayContent(s.InstallRoot, Sally::Supporter::CanonicalVersion(Narrow(s.InstalledVersion)), s.Bundle);
    Log(std::wstring(s.Automatic ? L"scheduled check" : L"check") + L" of " + s.InstallRoot + L" (" +
        s.InstalledVersion + L") at " + s.FeedUrl);

    if (s.Offline)
    {
        if (!CreateUpdateWindow()) return 2;
        s.Current = View::UpToDate;
        auto label = T(IDS_SUP_NOTES_INSTALLED); label.replace(label.find(L"%s"), 2, s.InstalledVersion);
        SendMessageW(Control(kIdNotesVersion), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        SendMessageW(Control(kIdNotesVersion), CB_SETCURSEL, 0, 0);
        EnableWindow(Control(kIdNotesVersion), FALSE);
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
            Log(L"check failed: " + FailureText(s.CheckFailure) +
                (s.CheckFailure.Network.Status != 0 ? L" (" + s.CheckFailure.Network.Text + L")" : std::wstring()));
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
    s.Current = View::CheckFailed; // BeginCheck owns the initial and retry transitions.
    BeginCheck();
    ShowUpdateWindow(true);
    RunMessageLoop();
    return 0;
}

// --report: how the update that left the result file went. The file is claimed by renaming it,
// so that when two Sallys start at once only one of them shows it.
int ReportUpdate(const UpdaterCommand& command)
{
    Session& s = TheSession;
    s.AppearanceOwner = reinterpret_cast<HWND>(static_cast<UINT_PTR>(command.AppearanceOwner));
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
        s.InstallRoot = result.InstallRoot;
        s.SuccessfulReport = result.Status == UpdateStatus::Updated;
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
            RefreshSupportState();
            Layout();
            ShowUpdateWindow(true);
            RunMessageLoop();
        }
    }
    DeleteFileW(Sally::ToExtendedLengthPath(claimed).c_str());
    RemoveStagingFolders(work);
    return 0;
}

Sally::Appearance::Snapshot CurrentApplicationAppearance()
{
    if (!TheSession.Appearance.Profile.empty()) return TheSession.Appearance;
    return Sally::Appearance::Read(Sally::Supporter::ResolveProfileRoot(),
        Sally::Appearance::InstallIdentity(true), *GetWin32Registry());
}

} // namespace salupdate
