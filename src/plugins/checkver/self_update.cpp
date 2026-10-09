// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <wininet.h>

#include <functional>
#include <set>
#include <vector>

#include "checkver.h"
#include "checkver.rh"
#include "checkver.rh2"
#include "checkver_text.h"
#include "lang\lang.rh"
#include "self_update.h"

#include "update/ReleaseChecksums.h"
#include "update/UpdatePackage.h"
#include "update/UpdateProcesses.h"
#include "update/UpdaterCommand.h"

namespace
{

const wchar_t* const kDefaultFeedUrl = L"https://api.github.com/repos/0xeb/sally/releases/latest";
const wchar_t* const kAgentName = L"Sally Updater";
const wchar_t* const kSallyMainWindowClass = L"SalamanderMainWindowVer25";
// A release package is about 12 MB; anything far beyond that is not one.
const unsigned long long kMaxPackageBytes = 300ull * 1024 * 1024;
const unsigned long long kMaxChecksumsBytes = 64 * 1024;
const DWORD kPrepareTimeoutMs = 10 * 60 * 1000;

std::wstring FeedUrl = kDefaultFeedUrl;

struct InstallJob
{
    HWND Dialog = NULL;
    checkver::ReleaseCheckResult Release;
    std::wstring InstallRoot;
    std::wstring WorkFolder;
    std::wstring Staging;
    std::wstring Arch;
    std::wstring Version;
    std::wstring FromVersion;
};

HANDLE InstallThread = NULL;
volatile LONG InstallAbandoned = 0;
InstallJob ReadyJob; // the job whose release is unpacked, owned by the dialog thread

std::wstring Widen(const std::string& text)
{
    return checkver::Utf8ToWideOrEmpty(text);
}

void PostLog(HWND dialog, const std::wstring& line)
{
    if (InstallAbandoned)
        return;
    std::wstring* copy = new (std::nothrow) std::wstring(line);
    if (copy != NULL && !PostMessage(dialog, WM_USER_INSTALL_LOG, 0, reinterpret_cast<LPARAM>(copy)))
        delete copy;
}

std::wstring Format(int stringId, const std::wstring& a)
{
    return SPLFormatStringOwned(LangStr(stringId).c_str(), a.c_str());
}

std::wstring Format(int stringId, const std::wstring& a, const std::wstring& b)
{
    return SPLFormatStringOwned(LangStr(stringId).c_str(), a.c_str(), b.c_str());
}

std::wstring InstallFolder()
{
    std::wstring path;
    if (!SPLGetModuleFileNameOwned(NULL, path))
        return std::wstring();
    const size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

std::wstring FileName(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring ErrorText(DWORD error)
{
    wchar_t* allocated = NULL;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    HMODULE wininet = GetModuleHandleW(L"wininet.dll");
    if (error >= INTERNET_ERROR_BASE && error <= INTERNET_ERROR_LAST && wininet != NULL)
        flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD count = FormatMessageW(flags, wininet, error, 0, reinterpret_cast<wchar_t*>(&allocated), 0, NULL);
    std::wstring text = count > 0 && allocated != NULL ? std::wstring(allocated, count) : std::wstring();
    if (allocated != NULL)
        LocalFree(allocated);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' ' || text.back() == L'.'))
        text.pop_back();
    if (text.empty())
        text = L"error " + std::to_wstring(error);
    return text;
}

// One HTTPS GET; the body goes to sink, at most maxBytes of it.
bool HttpsGet(const std::wstring& url, unsigned long long maxBytes,
              const std::function<bool(const BYTE*, DWORD)>& sink, std::wstring& error)
{
    if (url.compare(0, 8, L"https://") != 0)
    {
        error = L"not an HTTPS address: " + url;
        return false;
    }
    HINTERNET session = InternetOpenW(kAgentName, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (session == NULL)
    {
        error = ErrorText(GetLastError());
        return false;
    }
    HINTERNET request = InternetOpenUrlW(session, url.c_str(), NULL, 0,
                                         INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_DONT_CACHE |
                                             INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI,
                                         0);
    bool ok = request != NULL;
    if (!ok)
        error = ErrorText(GetLastError());
    if (ok)
    {
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (!HttpQueryInfoW(request, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &statusSize, NULL) ||
            status < 200 || status >= 300)
        {
            error = L"HTTP " + std::to_wstring(status);
            ok = false;
        }
    }
    unsigned long long total = 0;
    std::vector<BYTE> buffer(64 * 1024);
    while (ok)
    {
        if (InstallAbandoned)
        {
            error = LangStr(IDS_INET_ABORTED);
            ok = false;
            break;
        }
        DWORD read = 0;
        if (!InternetReadFile(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read))
        {
            error = ErrorText(GetLastError());
            ok = false;
            break;
        }
        if (read == 0)
            break;
        total += read;
        if (total > maxBytes)
        {
            error = L"the download is larger than expected";
            ok = false;
            break;
        }
        if (!sink(buffer.data(), read))
        {
            error = ErrorText(GetLastError());
            ok = false;
        }
    }
    if (request != NULL)
        InternetCloseHandle(request);
    InternetCloseHandle(session);
    return ok;
}

bool Fail(const InstallJob& job, const std::wstring& reason)
{
    PostLog(job.Dialog, Format(IDS_UPDATE_FAILED, reason));
    return false;
}

bool RunPrepare(const InstallJob& job, const std::wstring& package, const std::string& sha256)
{
    Sally::Update::UpdaterCommand command;
    command.Mode = Sally::Update::UpdaterMode::Prepare;
    command.Package = package;
    command.Sha256 = Widen(sha256);
    command.Staging = job.Staging;
    command.Version = job.Version;
    command.Arch = job.Arch;
    command.ResultFile = job.Staging + L"\\prepare.txt";
    DeleteFileW(command.ResultFile.c_str());

    const std::wstring updater = job.InstallRoot + L"\\utils\\salupdate.exe";
    std::wstring commandLine =
        Sally::Update::QuoteCommandLineArgument(updater) + L" " + Sally::Update::FormatUpdaterCommand(command);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(updater.c_str(), &commandLine[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup,
                        &process))
        return Fail(job, Format(IDS_UPDATE_CANNOT_START, updater) + L" " + ErrorText(GetLastError()));
    CloseHandle(process.hThread);
    const DWORD wait = WaitForSingleObject(process.hProcess, kPrepareTimeoutMs);
    if (wait != WAIT_OBJECT_0)
        TerminateProcess(process.hProcess, 1);
    CloseHandle(process.hProcess);

    Sally::Update::UpdateResult result;
    if (!Sally::Update::ReadUpdateResult(command.ResultFile, result))
        return Fail(job, Format(IDS_UPDATE_CANNOT_START, updater));
    if (result.Status != Sally::Update::UpdateStatus::Prepared)
        return Fail(job, result.Message);
    return true;
}

bool RunInstallJob(const InstallJob& job)
{
    const checkver::ReleaseCheckResult& release = job.Release;
    const std::wstring assetName = Widen(release.PrimaryAssetName);

    // The checksums the release publishes; the package must match its line.
    std::string sumsText;
    std::wstring error;
    if (!HttpsGet(Widen(release.ChecksumsUrl), kMaxChecksumsBytes,
                  [&](const BYTE* data, DWORD size) {
                      sumsText.append(reinterpret_cast<const char*>(data), size);
                      return true;
                  },
                  error))
        return Fail(job, Format(IDS_INET_READ_FAILED, error));
    std::vector<Sally::Update::ChecksumEntry> sums;
    std::string parseError;
    const Sally::Update::ChecksumEntry* entry = NULL;
    if (Sally::Update::ParseSha256Sums(sumsText, sums, parseError))
        entry = Sally::Update::FindChecksum(sums, release.PrimaryAssetName);
    if (entry == NULL)
        return Fail(job, Format(IDS_UPDATE_NO_CHECKSUM, assetName));
    std::string digest;
    if (!release.PrimaryAssetDigest.empty() &&
        (!Sally::Update::ParseGitHubSha256Digest(release.PrimaryAssetDigest, digest) || digest != entry->Sha256))
        return Fail(job, LangStr(IDS_UPDATE_CHECKSUM_MISMATCH));

    CreateDirectoryW(job.Staging.c_str(), NULL);
    const std::wstring package = job.Staging + L"\\" + assetName;
    PostLog(job.Dialog, Format(IDS_UPDATE_DOWNLOADING, assetName));
    HANDLE file = CreateFileW(package.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return Fail(job, package + L": " + ErrorText(GetLastError()));
    const unsigned long long limit =
        release.PrimaryAssetSize > 0 && release.PrimaryAssetSize < kMaxPackageBytes ? release.PrimaryAssetSize
                                                                                    : kMaxPackageBytes;
    unsigned long long received = 0;
    int reportedQuarter = 0;
    const bool downloaded = HttpsGet(Widen(release.PrimaryUrl), limit,
                                     [&](const BYTE* data, DWORD size) {
                                         DWORD written = 0;
                                         if (!WriteFile(file, data, size, &written, NULL) || written != size)
                                             return false;
                                         received += size;
                                         if (release.PrimaryAssetSize > 0)
                                         {
                                             const int quarter = static_cast<int>(received * 4 / release.PrimaryAssetSize);
                                             if (quarter > reportedQuarter && quarter < 4)
                                             {
                                                 reportedQuarter = quarter;
                                                 PostLog(job.Dialog, L"   " + std::to_wstring(quarter * 25) + L" %");
                                             }
                                         }
                                         return true;
                                     },
                                     error);
    CloseHandle(file);
    if (!downloaded)
    {
        DeleteFileW(package.c_str());
        return Fail(job, Format(IDS_INET_READ_FAILED, error));
    }
    if (release.PrimaryAssetSize > 0 && received != release.PrimaryAssetSize)
    {
        DeleteFileW(package.c_str());
        return Fail(job, Format(IDS_INET_READ_FAILED, L"the download is incomplete"));
    }

    PostLog(job.Dialog, LangStr(IDS_UPDATE_VERIFYING));
    std::string actual;
    DWORD hashError = 0;
    if (!Sally::Update::ComputeFileSha256(package, actual, hashError) || actual != entry->Sha256)
    {
        DeleteFileW(package.c_str());
        return Fail(job, LangStr(IDS_UPDATE_CHECKSUM_MISMATCH));
    }

    PostLog(job.Dialog, Format(IDS_UPDATE_PREPARING, job.Version));
    if (!RunPrepare(job, package, entry->Sha256))
        return false;
    DeleteFileW(package.c_str());
    PostLog(job.Dialog, Format(IDS_UPDATE_READY, job.Version));
    return true;
}

DWORD WINAPI InstallThreadProc(void* param)
{
    InstallJob* job = static_cast<InstallJob*>(param);
    // Keep the plugin loaded while this thread runs.
    std::wstring modulePath;
    HINSTANCE lock = SPLGetModuleFileNameOwned(DLLInstance, modulePath) ? LoadLibraryW(modulePath.c_str()) : NULL;
    BOOL ready = FALSE;
    try
    {
        ready = RunInstallJob(*job) ? TRUE : FALSE;
    }
    catch (...)
    {
        ready = FALSE;
    }
    if (!InstallAbandoned)
        PostMessage(job->Dialog, WM_USER_INSTALL_READY, ready, 0);
    delete job;
    if (lock != NULL)
        FreeLibraryAndExitThread(lock, 0);
    return 0;
}

struct WindowSearch
{
    std::set<DWORD> Pids;
    std::vector<HWND> Windows;
};

BOOL CALLBACK CollectSallyWindows(HWND window, LPARAM param)
{
    WindowSearch* search = reinterpret_cast<WindowSearch*>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (search->Pids.count(pid) == 0)
        return TRUE;
    wchar_t className[64];
    if (GetClassNameW(window, className, _countof(className)) > 0 && wcscmp(className, kSallyMainWindowClass) == 0)
        search->Windows.push_back(window);
    return TRUE;
}

bool SameFolder(const std::wstring& a, const std::wstring& b)
{
    return Sally::Update::IsPathInsideFolder(a, b) && Sally::Update::IsPathInsideFolder(b, a);
}

void RemoveStagingFolders(const std::wstring& workFolder)
{
    WIN32_FIND_DATAW find;
    HANDLE handle = FindFirstFileW((workFolder + L"\\v*").c_str(), &find);
    if (handle == INVALID_HANDLE_VALUE)
        return;
    do
    {
        if ((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            (find.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            continue;
        std::wstring from = workFolder + L"\\" + find.cFileName;
        from.push_back(L'\0');
        SHFILEOPSTRUCTW op = {};
        op.wFunc = FO_DELETE;
        op.pFrom = from.c_str();
        op.fFlags = FOF_NO_UI;
        SHFileOperationW(&op);
    } while (FindNextFileW(handle, &find));
    FindClose(handle);
}

} // namespace

BOOL CloseSallyForUpdate = FALSE;

const wchar_t* GetReleaseFeedUrl()
{
    return FeedUrl.c_str();
}

void SetReleaseFeedUrl(const std::wstring& url)
{
    // Another feed (a mirror) must still be HTTPS.
    FeedUrl = url.compare(0, 8, L"https://") == 0 ? url : std::wstring(kDefaultFeedUrl);
}

BOOL IsReleaseInstallRunning()
{
    if (InstallThread == NULL)
        return FALSE;
    if (WaitForSingleObject(InstallThread, 0) == WAIT_TIMEOUT)
        return TRUE;
    CloseHandle(InstallThread);
    InstallThread = NULL;
    return FALSE;
}

void AbandonReleaseInstall()
{
    InterlockedExchange(&InstallAbandoned, 1);
    if (InstallThread != NULL)
    {
        CloseHandle(InstallThread);
        InstallThread = NULL;
    }
}

BOOL StartReleaseInstall(HWND dialog, const checkver::ReleaseCheckResult& release)
{
    if (IsReleaseInstallRunning() || !release.CanInstall)
        return FALSE;
    InstallJob* job = new (std::nothrow) InstallJob();
    if (job == NULL)
        return FALSE;
    job->Dialog = dialog;
    job->Release = release;
    job->InstallRoot = InstallFolder();
    job->WorkFolder = Sally::Update::UpdateWorkFolder();
    job->Arch = Widen(checkver::GetPlatformLabel(
#ifdef _WIN64
#ifdef _M_ARM64
        checkver::GitHubAssetPlatform::ARM64
#else
        checkver::GitHubAssetPlatform::X64
#endif
#else
        checkver::GitHubAssetPlatform::X86
#endif
        ));
    // "1.0.36", as the release notes and the About box write it, not the tag's "v1.0.36"
    auto plain = [](const std::string& tag) {
        const std::wstring text = Widen(tag);
        return !text.empty() && (text[0] == L'v' || text[0] == L'V') ? text.substr(1) : text;
    };
    job->Version = plain(release.LatestVersion);
    job->FromVersion = plain(release.InstalledVersion);
    if (job->InstallRoot.empty() || job->WorkFolder.empty())
    {
        delete job;
        return FALSE;
    }
    job->Staging = Sally::Update::StagingFolderFor(job->WorkFolder, job->Version, job->Arch);
    ReadyJob = *job;
    InterlockedExchange(&InstallAbandoned, 0);
    DWORD id = 0;
    InstallThread = CreateThread(NULL, 0, InstallThreadProc, job, 0, &id);
    if (InstallThread == NULL)
    {
        delete job;
        return FALSE;
    }
    return TRUE;
}

void FinishReleaseInstall(HWND dialog, BOOL ready)
{
    // The worker posted its last message and is ending; let it, so the buttons come back.
    if (InstallThread != NULL)
    {
        WaitForSingleObject(InstallThread, 10000);
        IsReleaseInstallRunning();
    }
    if (!ready)
        return;
    const InstallJob& job = ReadyJob;
    const std::wstring title = LangStr(IDS_PLUGINNAME);

    // Every program started from this folder must end before its files can be replaced.
    std::set<DWORD> otherSallys;
    std::wstring others;
    for (const Sally::Update::RunningProcess& process :
         Sally::Update::FindProcessesInFolder(job.InstallRoot, GetCurrentProcessId()))
    {
        const std::wstring name = FileName(process.Image);
        if (CompareStringOrdinal(name.c_str(), -1, L"sally.exe", -1, TRUE) == CSTR_EQUAL)
            otherSallys.insert(process.Pid);
        // Every Sally keeps a bug reporter (utils\salmon.exe) waiting beside it, which ends with
        // its Sally; the updater waits for it.
        else if (CompareStringOrdinal(name.c_str(), -1, L"salmon.exe", -1, TRUE) != CSTR_EQUAL)
            others += L"\n" + process.Image;
    }
    if (!others.empty())
    {
        SalGeneral->SalMessageBox(dialog, Format(IDS_UPDATE_OTHERS_RUNNING, others).c_str(), title.c_str(),
                                  MB_OK | MB_ICONINFORMATION);
        return;
    }
    const std::wstring question =
        otherSallys.empty() ? Format(IDS_UPDATE_CONFIRM, job.Version)
                            : SPLFormatStringOwned(LangStr(IDS_UPDATE_CONFIRM_OTHERS).c_str(), job.Version.c_str(),
                                                   static_cast<int>(otherSallys.size()));
    if (SalGeneral->SalMessageBox(dialog, question.c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    Sally::Update::UpdaterCommand command;
    command.Mode = Sally::Update::UpdaterMode::Apply;
    command.Staging = job.Staging;
    command.InstallRoot = job.InstallRoot;
    command.WaitPids.push_back(GetCurrentProcessId());
    command.WaitPids.insert(command.WaitPids.end(), otherSallys.begin(), otherSallys.end());
    command.FromVersion = job.FromVersion;
    command.ToVersion = job.Version;
    command.ResultFile = Sally::Update::UpdateResultPath(job.WorkFolder);
    DeleteFileW(command.ResultFile.c_str());

    const std::wstring updater = job.Staging + L"\\salupdate.exe";
    std::wstring commandLine =
        Sally::Update::QuoteCommandLineArgument(updater) + L" " + Sally::Update::FormatUpdaterCommand(command);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(updater.c_str(), &commandLine[0], NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL,
                        job.Staging.c_str(), &startup, &process))
    {
        const std::wstring message = Format(IDS_UPDATE_CANNOT_START, updater) + L"\n\n" + ErrorText(GetLastError());
        SalGeneral->SalMessageBox(dialog, message.c_str(), title.c_str(), MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    // The next Sally loads this plugin to report the outcome.
    SalGeneral->SetFlagLoadOnSalamanderStart(TRUE);

    WindowSearch search;
    search.Pids = otherSallys;
    EnumWindows(CollectSallyWindows, reinterpret_cast<LPARAM>(&search));
    for (HWND window : search.Windows)
        PostMessage(window, WM_CLOSE, 0, 0);

    // Close this window first, then Sally (see ThreadMessageLoopBody), so that closing Sally
    // does not stop to ask about an open Check Version window.
    CloseSallyForUpdate = TRUE;
    PostMessage(dialog, WM_COMMAND, IDCANCEL, 0);
}

BOOL HasPendingUpdateResult()
{
    const std::wstring work = Sally::Update::UpdateWorkFolder();
    Sally::Update::UpdateResult result;
    return !work.empty() && Sally::Update::ReadUpdateResult(Sally::Update::UpdateResultPath(work), result) &&
           SameFolder(result.InstallRoot, InstallFolder());
}

BOOL TakeUpdateResult(std::vector<std::wstring>& reportLines, BOOL& failed)
{
    reportLines.clear();
    failed = FALSE;
    const std::wstring work = Sally::Update::UpdateWorkFolder();
    if (work.empty())
        return FALSE;
    const std::wstring path = Sally::Update::UpdateResultPath(work);
    Sally::Update::UpdateResult result;
    if (!Sally::Update::ReadUpdateResult(path, result) || !SameFolder(result.InstallRoot, InstallFolder()))
        return FALSE;
    switch (result.Status)
    {
    case Sally::Update::UpdateStatus::Updated:
        reportLines.push_back(Format(IDS_UPDATE_DONE, result.FromVersion, result.ToVersion));
        break;
    case Sally::Update::UpdateStatus::Aborted:
        reportLines.push_back(Format(IDS_UPDATE_ABORTED, result.ToVersion));
        failed = TRUE;
        break;
    default:
        reportLines.push_back(result.RollbackComplete ? Format(IDS_UPDATE_ROLLED_BACK, result.ToVersion, result.Message)
                                                      : Format(IDS_UPDATE_INCOMPLETE, result.ToVersion, result.Message));
        failed = TRUE;
        break;
    }
    // The log does not wrap: the file and the system's reason go on lines of their own.
    if (failed && !result.FailedPath.empty())
        reportLines.push_back(L"   " + result.FailedPath);
    if (failed && result.Code != 0)
        reportLines.push_back(L"   " + ErrorText(result.Code));
    DeleteFileW(path.c_str());
    RemoveStagingFolders(work);
    return TRUE;
}
