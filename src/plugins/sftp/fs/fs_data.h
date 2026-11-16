// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string>

// What the plugin keeps for every item of a listing (CFileData::PluginData points to one of these).
// The raw name is what every later operation on the item must use; CFileData::Name is only the decoded display name.
struct CSftpFileData
{
    std::string rawName;
    uint32_t mode = 0; // st_mode bits as the server sent them
    bool modeKnown = false; // the server reported the mode
    std::wstring permissions; // "drwxr-xr-x", formatted once when the listing is built
    std::wstring owner;       // numeric user id as text (SFTP v3), may be empty
    std::wstring group;
};

// Gives Salamander the plugin-specific columns (Permissions, Owner, Group) and releases the per-item data.
// One object per listing; Salamander hands it back through CPluginInterface::ReleasePluginDataInterface.
class CPluginDataInterface : public CPluginDataInterfaceAbstract
{
public:
    virtual BOOL WINAPI CallReleaseForFiles() { return TRUE; }
    virtual BOOL WINAPI CallReleaseForDirs() { return TRUE; }
    virtual void WINAPI ReleasePluginData(CFileData& file, BOOL isDir);

    // archives only
    virtual void WINAPI GetFileDataForUpDir(const wchar_t* archivePath, CFileData& upDir) {}
    virtual BOOL WINAPI GetFileDataForNewDir(const wchar_t* dirName, CFileData& dir) { return FALSE; }

    // custom icons are not used (the listing says pitSimple)
    virtual HIMAGELIST WINAPI GetSimplePluginIcons(int iconSize) { return nullptr; }
    virtual BOOL WINAPI HasSimplePluginIcon(CFileData& file, BOOL isDir) { return TRUE; }
    virtual HICON WINAPI GetPluginIcon(const CFileData* file, int iconSize, BOOL& destroyIcon)
    {
        destroyIcon = FALSE;
        return nullptr;
    }
    virtual int WINAPI CompareFilesFromFS(const CFileData* file1, const CFileData* file2);

    virtual void WINAPI SetupView(BOOL leftPanel, CSalamanderViewAbstract* view, const wchar_t* archivePath,
                                  const CFileData* upperDir);
    virtual void WINAPI ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth);
    virtual void WINAPI ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column, int newWidth);

    // standard handling of the Information Line, size and date
    virtual BOOL WINAPI GetInfoLineContent(int panel, const CFileData* file, BOOL isDir, int selectedFiles,
                                           int selectedDirs, BOOL displaySize, const CQuadWord& selectedSize,
                                           CSalamanderStringBuffer* buffer, CSalamanderTextRangeBuffer* hotTexts)
    {
        return FALSE;
    }
    virtual BOOL WINAPI CanBeCopiedToClipboard() { return FALSE; }
    virtual BOOL WINAPI GetByteSize(const CFileData* file, BOOL isDir, CQuadWord* size) { return FALSE; }
    virtual BOOL WINAPI GetLastWriteDate(const CFileData* file, BOOL isDir, SYSTEMTIME* date) { return FALSE; }
    virtual BOOL WINAPI GetLastWriteTime(const CFileData* file, BOOL isDir, SYSTEMTIME* time) { return FALSE; }
};
