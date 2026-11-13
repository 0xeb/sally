// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "fs_data.h"
#include "listing_format.h"

namespace
{
// Salamander gives the column callbacks their input and output through its own global variables
// (CSalamanderViewAbstract::GetTransferVariables): pointers to them are cached here by SetupView.
const CFileData** TransferFileData = nullptr;
int* TransferIsDir = nullptr;
wchar_t* TransferBuffer = nullptr;
int* TransferLen = nullptr;
DWORD* TransferRowData = nullptr;
CPluginDataInterfaceAbstract** TransferPluginDataIface = nullptr;
DWORD* TransferActCustomData = nullptr;

const DWORD ColPermissions = 1; // CColumn::CustomData of our columns
const DWORD ColOwner = 2;
const DWORD ColGroup = 3;
const int ColumnCount = 3;
const int DefaultColumnWidth = 80;

// Column widths and "fixed width" flags the user set, [0] left panel, [1] right panel, per column (CustomData - 1).
// Kept for the lifetime of the plugin only (not saved to the registry yet).
int ColumnWidths[2][ColumnCount] = {{0, 0, 0}, {0, 0, 0}};
int ColumnFixed[2][ColumnCount] = {{0, 0, 0}, {0, 0, 0}};

// PluginData of the current item; NULL for the up-dir item.
const CSftpFileData* DataOf(const CFileData* file)
{
    return file != nullptr ? reinterpret_cast<const CSftpFileData*>(file->PluginData) : nullptr;
}

void SetText(const std::wstring& text)
{
    size_t len = text.size();
    if (len > TRANSFER_BUFFER_MAX)
        len = TRANSFER_BUFFER_MAX;
    if (len > 0)
        memcpy(TransferBuffer, text.data(), len * sizeof(wchar_t));
    *TransferLen = static_cast<int>(len);
}

void WINAPI GetPermissionsText()
{
    const CSftpFileData* data = DataOf(*TransferFileData);
    SetText(data != nullptr ? data->permissions : std::wstring());
}

void WINAPI GetOwnerText()
{
    const CSftpFileData* data = DataOf(*TransferFileData);
    SetText(data != nullptr ? data->owner : std::wstring());
}

void WINAPI GetGroupText()
{
    const CSftpFileData* data = DataOf(*TransferFileData);
    SetText(data != nullptr ? data->group : std::wstring());
}

void CopyText(wchar_t* target, size_t capacity, const std::wstring& text)
{
    wcsncpy_s(target, capacity, text.c_str(), _TRUNCATE);
}

void AddColumn(BOOL leftPanel, CSalamanderViewAbstract* view, int index, DWORD customData, int nameId, int descId,
               FColumnGetText getText)
{
    const int side = leftPanel ? 0 : 1;
    CColumn column;
    memset(&column, 0, sizeof(column));
    CopyText(column.Name, COLUMN_NAME_MAX, LangStr(nameId));
    CopyText(column.Description, COLUMN_DESCRIPTION_MAX, LangStr(descId));
    column.GetText = getText;
    column.CustomData = customData;
    column.SupportSorting = 0;
    column.LeftAlignment = 1;
    column.ID = COLUMN_ID_CUSTOM;
    const int width = ColumnWidths[side][customData - 1];
    column.Width = width > 0 ? width : DefaultColumnWidth;
    column.FixedWidth = ColumnFixed[side][customData - 1] ? 1 : 0;
    view->InsertColumn(index, &column);
}
} // namespace

void WINAPI
CPluginDataInterface::ReleasePluginData(CFileData& file, BOOL isDir)
{
    delete reinterpret_cast<CSftpFileData*>(file.PluginData);
    file.PluginData = 0;
}

int WINAPI
CPluginDataInterface::CompareFilesFromFS(const CFileData* file1, const CFileData* file2)
{
    const CSftpFileData* a = DataOf(file1);
    const CSftpFileData* b = DataOf(file2);
    if (a != nullptr && b != nullptr)
    {
        // by the raw bytes; two different items must never compare equal (SDK rule), so identical names fall back
        // to the address (arbitrary but stable while the listing lives)
        const int byName = CompareRawNames(a->rawName, b->rawName);
        if (byName != 0 || file1 == file2)
            return byName;
        return file1 < file2 ? -1 : 1;
    }
    if (a == nullptr && b == nullptr)
        return 0; // both are the up-dir item
    return a == nullptr ? -1 : 1;
}

void WINAPI
CPluginDataInterface::SetupView(BOOL leftPanel, CSalamanderViewAbstract* view, const wchar_t* archivePath,
                                const CFileData* upperDir)
{
    view->GetTransferVariables(TransferFileData, TransferIsDir, TransferBuffer, TransferLen, TransferRowData,
                               TransferPluginDataIface, TransferActCustomData);

    if (view->GetViewMode() != VIEW_MODE_DETAILED)
        return;

    // The Windows attributes column means nothing for a Unix server: remove it; our columns replace it.
    for (int i = view->GetColumnsCount() - 1; i >= 0; i--)
    {
        const CColumn* column = view->GetColumn(i);
        if (column != nullptr && column->ID == COLUMN_ID_ATTRIBUTES)
            view->DeleteColumn(i);
    }

    try // loading the column names allocates memory
    {
        int index = view->GetColumnsCount();
        AddColumn(leftPanel, view, index++, ColPermissions, IDS_COL_PERMISSIONS, IDS_COL_PERMISSIONS_DESC,
                  GetPermissionsText);
        AddColumn(leftPanel, view, index++, ColOwner, IDS_COL_OWNER, IDS_COL_OWNER_DESC, GetOwnerText);
        AddColumn(leftPanel, view, index++, ColGroup, IDS_COL_GROUP, IDS_COL_GROUP_DESC, GetGroupText);
    }
    catch (...)
    {
        TRACE_E("SetupView: out of memory.");
    }
}

void WINAPI
CPluginDataInterface::ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column, int newFixedWidth)
{
    if (column->CustomData < 1 || column->CustomData > ColumnCount)
        return;
    ColumnFixed[leftPanel ? 0 : 1][column->CustomData - 1] = newFixedWidth;
    if (newFixedWidth) // keep the width the column has now
        ColumnWidthWasChanged(leftPanel, column, column->Width);
}

void WINAPI
CPluginDataInterface::ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column, int newWidth)
{
    if (column->CustomData < 1 || column->CustomData > ColumnCount)
        return;
    ColumnWidths[leftPanel ? 0 : 1][column->CustomData - 1] = newWidth;
}
