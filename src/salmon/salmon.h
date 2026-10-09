// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

extern HINSTANCE HLanguage;
extern std::wstring BugReportPath; // the path ends with a trailing backslash when nonempty
extern std::wstring CrashReportName;
struct CBugReport
{
    std::wstring Name;
};
extern std::vector<CBugReport> BugReports;

extern CSalmonSharedMemory* SalmonSharedMemory;

std::wstring LoadStr(int resID, HINSTANCE hInstance);
// Compatibility alias retained for call sites widened before LoadStr itself became wide.
std::wstring LoadStrW(int resID, HINSTANCE hInstance);
char* GetErrorText(DWORD error);
std::wstring FormatText(const wchar_t* format, ...);
BOOL GetCurrentModulePath(std::wstring& path);

void OpenFolder(HWND hWnd, const wchar_t* szDir);

// Opens a document or web address with its default program.
BOOL OpenWithShell(HWND hWnd, const wchar_t* target);

// Reads the text bug report of 'report'; FALSE when it has none.
BOOL ReadBugReportText(const CBugReport& report, std::string& text);

BOOL RestartSalamander(HWND hParent);

BOOL CleanBugReportsDirectory(BOOL keep7ZipArchives);

// returns TRUE if a bug report exists
// sets the LatestBugReport global to the most recent name
BOOL GetBugReportNames();

int GetUniqueBugReportCount();

std::wstring GetReportBaseName(const wchar_t* targetPath, const wchar_t* shortName, DWORD64 uid, SYSTEMTIME lt);

BOOL SaveDescription();

BOOL CompresBugReports();

extern BOOL AppIsBusy;
