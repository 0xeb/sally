// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// legacy_view_to_core — frozen v107 View callback plus lifetime-stable transfer
// scratch over the live wide API. This mixed-generation TU never includes
// precomp.h.

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "compat/legacy_convert.h"
#include "compat/legacy_to_core.h"

namespace sally::compat
{
    namespace
    {

        void WINAPI DispatchLegacyColumn();
        int WINAPI DispatchLegacyIcon();

        std::mutex DispatchMutex;
        std::unordered_map<DWORD, std::weak_ptr<CLegacyViewTransferState>> ColumnOwners;
        std::unordered_map<::CPluginDataInterfaceAbstract*,
                           std::weak_ptr<CLegacyViewTransferState>>
            PluginOwners;
        std::atomic<DWORD> NextColumnToken{1};
        DWORD* LiveCustomDataSlot = nullptr;
        ::CPluginDataInterfaceAbstract** LivePluginDataSlot = nullptr;
        std::size_t TransferOwnerCount = 0;

        template <typename Char, std::size_t N>
        std::size_t BoundedLength(const Char (&text)[N])
        {
            std::size_t length = 0;
            while (length < N && text[length] != 0)
                ++length;
            return length;
        }

        template <typename Char>
        std::size_t BoundedLength(const Char* text, std::size_t capacity)
        {
            std::size_t length = 0;
            while (length < capacity && text[length] != 0)
                ++length;
            return length;
        }

        template <typename Char, std::size_t N>
        bool HasSecondString(const Char (&text)[N])
        {
            const std::size_t first = BoundedLength(text);
            if (first >= N || first + 1 >= N || text[first + 1] == 0)
                return false;
            return BoundedLength(text + first + 1, N - first - 1) < N - first - 1;
        }

        bool WidenBounded(const char* source, std::size_t capacity, bool includeSecond,
                          wchar_t* destination, std::size_t destinationCapacity)
        {
            if (source == nullptr || destination == nullptr || destinationCapacity == 0)
                return false;
            std::wmemset(destination, 0, destinationCapacity);

            const std::size_t firstLength = BoundedLength(source, capacity);
            if (firstLength == 0 || firstLength >= capacity)
            {
                SetLastError(ERROR_INVALID_DATA);
                return false;
            }
            std::wstring firstWide;
            if (!WidenPluginSpan(source, static_cast<int>(firstLength),
                                 firstWide))
                return false;
            if (firstWide.empty() || firstWide.size() + 1 > destinationCapacity)
            {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return false;
            }
            std::wmemcpy(destination, firstWide.data(), firstWide.size());

            if (!includeSecond)
                return true;
            const std::size_t secondOffset = firstLength + 1;
            if (secondOffset >= capacity)
                return false;
            const std::size_t secondLength =
                BoundedLength(source + secondOffset, capacity - secondOffset);
            if (secondLength == 0 || secondLength >= capacity - secondOffset)
            {
                SetLastError(ERROR_INVALID_DATA);
                return false;
            }
            std::wstring secondWide;
            if (!WidenPluginSpan(source + secondOffset,
                                 static_cast<int>(secondLength), secondWide))
                return false;
            const std::size_t wideOffset = firstWide.size() + 1;
            if (secondWide.empty() || wideOffset + secondWide.size() + 1 >
                                          destinationCapacity)
            {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return false;
            }
            std::wmemcpy(destination + wideOffset, secondWide.data(), secondWide.size());
            return true;
        }

        bool NarrowBounded(const wchar_t* source, std::size_t capacity,
                           bool includeSecond, char* destination,
                           std::size_t destinationCapacity)
        {
            if (source == nullptr || destination == nullptr || destinationCapacity == 0)
                return false;
            std::memset(destination, 0, destinationCapacity);

            const std::size_t firstLength = BoundedLength(source, capacity);
            if (firstLength == 0 || firstLength >= capacity)
                return false;
            NarrowResult first;
            try
            {
                first = NarrowExact(std::wstring(source, firstLength));
            }
            catch (const std::bad_alloc&)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return false;
            }
            catch (const std::length_error&)
            {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return false;
            }
            if (!first.ok || first.value.size() + 1 > destinationCapacity)
            {
                if (first.ok)
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return false;
            }
            std::memcpy(destination, first.value.data(), first.value.size());

            if (!includeSecond)
                return true;
            const std::size_t secondOffset = firstLength + 1;
            if (secondOffset >= capacity)
                return false;
            const std::size_t secondLength =
                BoundedLength(source + secondOffset, capacity - secondOffset);
            if (secondLength == 0 || secondLength >= capacity - secondOffset)
                return false;
            NarrowResult second;
            try
            {
                second = NarrowExact(
                    std::wstring(source + secondOffset, secondLength));
            }
            catch (const std::bad_alloc&)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return false;
            }
            catch (const std::length_error&)
            {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return false;
            }
            const std::size_t narrowOffset = first.value.size() + 1;
            if (!second.ok || narrowOffset + second.value.size() + 1 >
                                  destinationCapacity)
            {
                if (second.ok)
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return false;
            }
            std::memcpy(destination + narrowOffset, second.value.data(),
                        second.value.size());
            return true;
        }

    } // namespace

    class CLegacyViewTransferState
        : public std::enable_shared_from_this<CLegacyViewTransferState>
    {
    public:
        struct CColumnCallback
        {
            DWORD Token = 0;
            sdk107::FColumnGetText Callback = nullptr;
            DWORD CustomData = 0;
        };

        CLegacyViewTransferState(::CPluginDataInterfaceAbstract* wideOwner,
                                 sdk107::CPluginDataInterfaceAbstract* legacyOwner)
            : WideOwner(wideOwner), LegacyOwner(legacyOwner)
        {
        }

        ~CLegacyViewTransferState()
        {
            FreeLegacyFileData(LegacyRow);
        }

        void Register()
        {
            std::lock_guard<std::mutex> lock(DispatchMutex);
            try
            {
                const auto published =
                    PluginOwners.emplace(WideOwner, shared_from_this());
                if (!published.second)
                    published.first->second = shared_from_this();
                ++TransferOwnerCount;
                Registered = true;
            }
            catch (const std::bad_alloc&)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            }
            catch (const std::length_error&)
            {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
            }
            catch (...)
            {
                SetLastError(ERROR_GEN_FAILURE);
            }
        }

        void Unregister()
        {
            std::lock_guard<std::mutex> lock(DispatchMutex);
            if (!Registered)
                return;
            auto plugin = PluginOwners.find(WideOwner);
            if (plugin != PluginOwners.end())
            {
                std::shared_ptr<CLegacyViewTransferState> owner =
                    plugin->second.lock();
                if (owner.get() == this)
                    PluginOwners.erase(plugin);
            }
            for (const CColumnCallback& callback : Callbacks)
                ColumnOwners.erase(callback.Token);
            if (TransferOwnerCount > 0)
                --TransferOwnerCount;
            if (TransferOwnerCount == 0)
            {
                LiveCustomDataSlot = nullptr;
                LivePluginDataSlot = nullptr;
            }
            Registered = false;
        }

        void Bind(::CSalamanderViewAbstract& wideView)
        {
            wideView.GetTransferVariables(
                LiveFileData, LiveIsDir, LiveBuffer, LiveLen, LiveRowData,
                LivePluginData, LiveCustomData);

            std::lock_guard<std::mutex> lock(DispatchMutex);
            if (LiveCustomDataSlot == nullptr)
                LiveCustomDataSlot = LiveCustomData;
            if (LivePluginDataSlot == nullptr)
                LivePluginDataSlot = LivePluginData;
            CompatibleGlobals = Registered &&
                                LiveCustomDataSlot == LiveCustomData &&
                                LivePluginDataSlot == LivePluginData;
        }

        void GetTransferVariables(
            const sdk107::CFileData**& transferFileData, int*& transferIsDir,
            char*& transferBuffer, int*& transferLen, DWORD*& transferRowData,
            sdk107::CPluginDataInterfaceAbstract**& transferPluginDataIface,
            DWORD*& transferActCustomData)
        {
            transferFileData = &LegacyFileData;
            transferIsDir = &LegacyIsDir;
            transferBuffer = LegacyBuffer;
            transferLen = &LegacyLen;
            transferRowData = &LegacyRowData;
            transferPluginDataIface = &LegacyPluginData;
            transferActCustomData = &LegacyCustomData;
        }

        ::FColumnGetText WrapColumn(sdk107::FColumnGetText callback,
                                    DWORD customData, DWORD& token)
        {
            token = customData;
            if (callback == nullptr || !CompatibleGlobals)
                return nullptr;

            for (const CColumnCallback& known : Callbacks)
            {
                if (known.Callback == callback && known.CustomData == customData)
                {
                    token = known.Token;
                    return DispatchLegacyColumn;
                }
            }

            CColumnCallback record;
            record.Token = NextColumnToken.fetch_add(1);
            if (record.Token == 0)
                record.Token = NextColumnToken.fetch_add(1);
            record.Callback = callback;
            record.CustomData = customData;
            std::lock_guard<std::mutex> lock(DispatchMutex);
            try
            {
                const auto published =
                    ColumnOwners.emplace(record.Token, shared_from_this());
                if (!published.second)
                {
                    SetLastError(ERROR_ALREADY_EXISTS);
                    return nullptr;
                }
                try
                {
                    Callbacks.push_back(record);
                }
                catch (...)
                {
                    ColumnOwners.erase(published.first);
                    throw;
                }
                token = record.Token;
                return DispatchLegacyColumn;
            }
            catch (const std::bad_alloc&)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return nullptr;
            }
            catch (const std::length_error&)
            {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return nullptr;
            }
            catch (...)
            {
                SetLastError(ERROR_GEN_FAILURE);
                return nullptr;
            }
        }

        void SetIconCallback(sdk107::FGetPluginIconIndex callback)
        {
            IconCallback = callback;
        }

        bool RestoreColumn(DWORD token, sdk107::FColumnGetText& callback,
                           DWORD& customData) const
        {
            for (const CColumnCallback& known : Callbacks)
            {
                if (known.Token == token)
                {
                    callback = known.Callback;
                    customData = known.CustomData;
                    return true;
                }
            }
            return false;
        }

        void DispatchColumn(DWORD token)
        {
            const CColumnCallback* selected = nullptr;
            for (const CColumnCallback& known : Callbacks)
            {
                if (known.Token == token)
                {
                    selected = &known;
                    break;
                }
            }
            if (selected == nullptr || selected->Callback == nullptr ||
                !Prepare(selected->CustomData))
            {
                RefuseText();
                return;
            }
            selected->Callback();
            CommitText();
        }

        int DispatchIcon()
        {
            if (IconCallback == nullptr || !Prepare(0))
                return 0;
            return IconCallback();
        }

        void InvalidateColumnMirrors() { ColumnMirrors.clear(); }

        const sdk107::CColumn* MirrorColumn(int index, const ::CColumn* source)
        {
            if (source == nullptr)
            {
                ColumnMirrors.erase(index);
                return nullptr;
            }
            sdk107::CColumn converted = {};
            if (!ColumnToLegacy(*source, converted))
                return nullptr;
            const auto found = ColumnMirrors.find(index);
            if (found != ColumnMirrors.end())
            {
                *found->second = converted;
                return found->second.get();
            }
            try
            {
                auto mirror =
                    std::make_unique<sdk107::CColumn>(converted);
                const sdk107::CColumn* result = mirror.get();
                if (!ColumnMirrors.emplace(index, std::move(mirror)).second)
                {
                    SetLastError(ERROR_ALREADY_EXISTS);
                    return nullptr;
                }
                return result;
            }
            catch (const std::bad_alloc&)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return nullptr;
            }
            catch (const std::length_error&)
            {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return nullptr;
            }
            catch (...)
            {
                SetLastError(ERROR_GEN_FAILURE);
                return nullptr;
            }
        }

        bool ColumnToLive(const sdk107::CColumn& source, ::CColumn& destination)
        {
            destination = {};
            if (!WidenBounded(source.Name, COLUMN_NAME_MAX, false,
                              destination.Name, COLUMN_NAME_MAX) ||
                !WidenBounded(source.Description, COLUMN_DESCRIPTION_MAX, false,
                              destination.Description, COLUMN_DESCRIPTION_MAX))
                return false;
            destination.GetText =
                WrapColumn(source.GetText, source.CustomData, destination.CustomData);
            if (source.GetText != nullptr && destination.GetText == nullptr)
                return false;
            destination.SupportSorting = source.SupportSorting;
            destination.LeftAlignment = source.LeftAlignment;
            destination.ID = source.ID;
            destination.Width = source.Width;
            destination.FixedWidth = source.FixedWidth;
            destination.MinWidth = source.MinWidth;
            return true;
        }

    private:
        bool ColumnToLegacy(const ::CColumn& source,
                            sdk107::CColumn& destination) const
        {
            destination = {};
            if (!NarrowBounded(source.Name, COLUMN_NAME_MAX,
                               HasSecondString(source.Name), destination.Name,
                               COLUMN_NAME_MAX) ||
                !NarrowBounded(source.Description, COLUMN_DESCRIPTION_MAX,
                               HasSecondString(source.Description),
                               destination.Description,
                               COLUMN_DESCRIPTION_MAX))
                return false;

            sdk107::FColumnGetText callback = source.GetText;
            DWORD customData = source.CustomData;
            if (source.GetText == DispatchLegacyColumn &&
                !RestoreColumn(source.CustomData, callback, customData))
                return false;
            destination.GetText = callback;
            destination.CustomData = customData;
            destination.SupportSorting = source.SupportSorting;
            destination.LeftAlignment = source.LeftAlignment;
            destination.ID = source.ID;
            destination.Width = source.Width;
            destination.FixedWidth = source.FixedWidth;
            destination.MinWidth = source.MinWidth;
            return true;
        }

        bool Prepare(DWORD customData)
        {
            FreeLegacyFileData(LegacyRow);
            LegacyFileData = nullptr;
            if (LiveFileData != nullptr && *LiveFileData != nullptr)
            {
                if (!FileDataToLegacy(**LiveFileData, LegacyRow))
                    return false;
                LegacyFileData = &LegacyRow;
            }

            LegacyIsDir = LiveIsDir != nullptr ? *LiveIsDir : 0;
            LegacyLen = 0;
            LegacyRowData = LiveRowData != nullptr ? *LiveRowData : 0;
            LegacyCustomData = customData;
            LegacyPluginData = nullptr;
            if (LivePluginData != nullptr && *LivePluginData != nullptr)
            {
                if (*LivePluginData != WideOwner)
                    return false;
                LegacyPluginData = LegacyOwner;
            }
            return true;
        }

        void RefuseText()
        {
            if (LiveLen != nullptr)
                *LiveLen = 0;
        }

        void CommitText()
        {
            if (LiveRowData != nullptr)
                *LiveRowData = LegacyRowData;
            if (LiveLen == nullptr || LiveBuffer == nullptr)
                return;

            if (LegacyLen < 0 || LegacyLen > (int)TRANSFER_BUFFER_MAX)
            {
                SetLastError(LegacyLen < 0 ? ERROR_INVALID_DATA
                                           : ERROR_INSUFFICIENT_BUFFER);
                *LiveLen = 0;
                return;
            }
            if (LegacyLen == 0)
            {
                *LiveLen = 0;
                return;
            }
            const int byteCount = LegacyLen;
            std::wstring text;
            if (!WidenPluginSpan(LegacyBuffer, byteCount, text) ||
                text.size() > TRANSFER_BUFFER_MAX)
            {
                if (text.size() > TRANSFER_BUFFER_MAX)
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                *LiveLen = 0;
                return;
            }
            if (!text.empty())
                std::wmemcpy(LiveBuffer, text.data(), text.size());
            *LiveLen = static_cast<int>(text.size());
        }

        ::CPluginDataInterfaceAbstract* WideOwner;
        sdk107::CPluginDataInterfaceAbstract* LegacyOwner;
        bool CompatibleGlobals = false;
        bool Registered = false;

        const ::CFileData** LiveFileData = nullptr;
        int* LiveIsDir = nullptr;
        wchar_t* LiveBuffer = nullptr;
        int* LiveLen = nullptr;
        DWORD* LiveRowData = nullptr;
        ::CPluginDataInterfaceAbstract** LivePluginData = nullptr;
        DWORD* LiveCustomData = nullptr;

        sdk107::CFileData LegacyRow = {};
        const sdk107::CFileData* LegacyFileData = nullptr;
        int LegacyIsDir = 0;
        char LegacyBuffer[TRANSFER_BUFFER_MAX] = {};
        int LegacyLen = 0;
        DWORD LegacyRowData = 0;
        sdk107::CPluginDataInterfaceAbstract* LegacyPluginData = nullptr;
        DWORD LegacyCustomData = 0;

        sdk107::FGetPluginIconIndex IconCallback = nullptr;
        std::vector<CColumnCallback> Callbacks;
        std::unordered_map<int, std::unique_ptr<sdk107::CColumn>> ColumnMirrors;
    };

    namespace
    {

        void WINAPI DispatchLegacyColumn()
        {
            std::shared_ptr<CLegacyViewTransferState> owner;
            DWORD token = 0;
            {
                std::lock_guard<std::mutex> lock(DispatchMutex);
                if (LiveCustomDataSlot == nullptr)
                    return;
                token = *LiveCustomDataSlot;
                auto found = ColumnOwners.find(token);
                if (found != ColumnOwners.end())
                    owner = found->second.lock();
            }
            if (owner != nullptr)
                owner->DispatchColumn(token);
        }

        int WINAPI DispatchLegacyIcon()
        {
            std::shared_ptr<CLegacyViewTransferState> owner;
            {
                std::lock_guard<std::mutex> lock(DispatchMutex);
                if (LivePluginDataSlot == nullptr || *LivePluginDataSlot == nullptr)
                    return 0;
                auto found = PluginOwners.find(*LivePluginDataSlot);
                if (found != PluginOwners.end())
                    owner = found->second.lock();
            }
            return owner != nullptr ? owner->DispatchIcon() : 0;
        }

    } // namespace

    CLegacyViewTransfer::CLegacyViewTransfer(
        ::CPluginDataInterfaceAbstract* wideOwner,
        sdk107::CPluginDataInterfaceAbstract* legacyOwner)
        : State(std::make_shared<CLegacyViewTransferState>(wideOwner, legacyOwner))
    {
        State->Register();
    }

    CLegacyViewTransfer::~CLegacyViewTransfer()
    {
        State->Unregister();
    }

    const sdk107::CColumn* CLegacyViewTransfer::MirrorColumnForPlugin(
        const ::CColumn* column)
    {
        return State->MirrorColumn(-1, column);
    }

    CLegacySalamanderView::CLegacySalamanderView(
        ::CSalamanderViewAbstract& wideView, CLegacyViewTransfer& transfer)
        : WideView(wideView), Transfer(transfer.State)
    {
        Transfer->Bind(wideView);
    }

    DWORD WINAPI CLegacySalamanderView::GetViewMode()
    {
        return WideView.GetViewMode();
    }

    void WINAPI CLegacySalamanderView::SetViewMode(DWORD viewMode, DWORD validData)
    {
        WideView.SetViewMode(viewMode, validData);
        Transfer->InvalidateColumnMirrors();
    }

    void WINAPI CLegacySalamanderView::GetTransferVariables(
        const sdk107::CFileData**& transferFileData, int*& transferIsDir,
        char*& transferBuffer, int*& transferLen, DWORD*& transferRowData,
        sdk107::CPluginDataInterfaceAbstract**& transferPluginDataIface,
        DWORD*& transferActCustomData)
    {
        Transfer->GetTransferVariables(
            transferFileData, transferIsDir, transferBuffer, transferLen,
            transferRowData, transferPluginDataIface, transferActCustomData);
    }

    void WINAPI CLegacySalamanderView::SetPluginSimpleIconCallback(
        sdk107::FGetPluginIconIndex callback)
    {
        Transfer->SetIconCallback(callback);
        WideView.SetPluginSimpleIconCallback(callback != nullptr ? DispatchLegacyIcon
                                                                 : nullptr);
    }

    int WINAPI CLegacySalamanderView::GetColumnsCount()
    {
        return WideView.GetColumnsCount();
    }

    const sdk107::CColumn* WINAPI CLegacySalamanderView::GetColumn(int index)
    {
        return Transfer->MirrorColumn(index, WideView.GetColumn(index));
    }

    BOOL WINAPI CLegacySalamanderView::InsertColumn(
        int index, const sdk107::CColumn* column)
    {
        if (column == nullptr)
            return WideView.InsertColumn(index, nullptr);
        ::CColumn wide = {};
        if (!Transfer->ColumnToLive(*column, wide))
            return FALSE;
        const BOOL result = WideView.InsertColumn(index, &wide);
        Transfer->InvalidateColumnMirrors();
        return result;
    }

    BOOL WINAPI CLegacySalamanderView::InsertStandardColumn(int index, DWORD id)
    {
        const BOOL result = WideView.InsertStandardColumn(index, id);
        Transfer->InvalidateColumnMirrors();
        return result;
    }

    BOOL WINAPI CLegacySalamanderView::SetColumnName(
        int index, const char* name, const char* description)
    {
        if (name == nullptr || description == nullptr)
            return WideView.SetColumnName(index, nullptr, nullptr);

        const bool mergedExtension =
            index == 0 && WideView.IsNameColumnExtensionMerged();

        wchar_t wideName[COLUMN_NAME_MAX] = {};
        wchar_t wideDescription[COLUMN_DESCRIPTION_MAX] = {};
        if (!WidenBounded(name, COLUMN_NAME_MAX, mergedExtension, wideName,
                          COLUMN_NAME_MAX) ||
            !WidenBounded(description, COLUMN_DESCRIPTION_MAX, mergedExtension,
                          wideDescription, COLUMN_DESCRIPTION_MAX))
            return FALSE;
        const wchar_t* wideExtensionName =
            mergedExtension ? wideName + std::wcslen(wideName) + 1 : nullptr;
        const wchar_t* wideExtensionDescription =
            mergedExtension ? wideDescription + std::wcslen(wideDescription) + 1
                            : nullptr;
        const BOOL result = WideView.SetColumnName(
            index, wideName, wideDescription, wideExtensionName,
            wideExtensionDescription);
        Transfer->InvalidateColumnMirrors();
        return result;
    }

    BOOL WINAPI CLegacySalamanderView::DeleteColumn(int index)
    {
        const BOOL result = WideView.DeleteColumn(index);
        Transfer->InvalidateColumnMirrors();
        return result;
    }

} // namespace sally::compat
