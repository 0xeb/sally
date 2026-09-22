// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// legacy_general_objects_to_core — General-allocated frozen v107 helper
// objects over their live counterparts.

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "compat/legacy_to_core.h"

#include "compat/legacy_convert.h"

namespace sally::compat
{
    namespace
    {

        template <class TLegacy, class TWide, class TWrapper>
        class COwnedWrappers
        {
        public:
            template <class... TArgs>
            TLegacy* Publish(TWide* wide, TArgs&&... args)
            {
                if (wide == nullptr)
                    return nullptr;
                const auto found = ByWide.find(wide);
                if (found != ByWide.end())
                    return found->second.get();

                try
                {
                    auto wrapper = std::make_unique<TWrapper>(
                        *wide, std::forward<TArgs>(args)...);
                    TLegacy* legacy = wrapper.get();
                    const auto published =
                        ByWide.emplace(wide, std::move(wrapper));
                    if (!published.second)
                        return published.first->second.get();
                    try
                    {
                        if (!ByLegacy.emplace(legacy, wide).second)
                        {
                            ByWide.erase(published.first);
                            SetLastError(ERROR_ALREADY_EXISTS);
                            return nullptr;
                        }
                    }
                    catch (...)
                    {
                        ByWide.erase(published.first);
                        throw;
                    }
                    return legacy;
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

            TWide* Find(TLegacy* legacy) const
            {
                if (legacy == nullptr)
                    return nullptr;
                const auto found = ByLegacy.find(legacy);
                return found != ByLegacy.end() ? found->second : nullptr;
            }

            TWide* Retire(TLegacy* legacy)
            {
                const auto found = ByLegacy.find(legacy);
                if (found == ByLegacy.end())
                    return nullptr;
                TWide* wide = found->second;
                ByLegacy.erase(found);
                ByWide.erase(wide);
                return wide;
            }

        private:
            std::unordered_map<TWide*, std::unique_ptr<TWrapper>> ByWide;
            std::unordered_map<TLegacy*, TWide*> ByLegacy;
        };

        int LegacyBytePosition(const std::string& legacy, int widePosition)
        {
            if (widePosition <= 0)
                return std::max(widePosition, 0);
            std::wstring wide;
            if (!WidenPluginText(legacy.c_str(), wide))
                return widePosition;
            const std::size_t count = std::min<std::size_t>(
                static_cast<std::size_t>(widePosition), wide.size());
            NarrowResult prefix;
            try
            {
                prefix = NarrowExact(wide.substr(0, count));
            }
            catch (const std::bad_alloc&)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return widePosition;
            }
            catch (const std::length_error&)
            {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return widePosition;
            }
            return prefix.ok ? static_cast<int>(prefix.value.size()) : widePosition;
        }

    } // namespace

    CLegacySalamanderMaskGroup::CLegacySalamanderMaskGroup(::CSalamanderMaskGroup& wideMaskGroup)
        : WideMaskGroup(wideMaskGroup)
    {
    }

    void WINAPI CLegacySalamanderMaskGroup::SetMasksString(const char* masks, BOOL extendedMode)
    {
        const char* legacy = masks != nullptr ? masks : "";
        std::wstring wide;
        if (!WidenPluginText(legacy, wide))
            return;
        std::string staged;
        try
        {
            staged = legacy;
        }
        catch (const std::bad_alloc&)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return;
        }
        catch (const std::length_error&)
        {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return;
        }
        WideMaskGroup.SetMasksString(wide.c_str(), extendedMode);
        LegacyMasks.swap(staged);
    }

    void WINAPI CLegacySalamanderMaskGroup::GetMasksString(char* buffer)
    {
        if (buffer == nullptr)
            return;
        const std::wstring wide = SPLGetMasksStringOwned(&WideMaskGroup);
        const NarrowResult narrow = NarrowExact(wide);
        if (!narrow.ok || narrow.value.size() >= MAX_GROUPMASK)
        {
            buffer[0] = 0;
            return;
        }
        std::memcpy(buffer, narrow.value.c_str(), narrow.value.size() + 1);
    }

    BOOL WINAPI CLegacySalamanderMaskGroup::GetExtendedMode()
    {
        return WideMaskGroup.GetExtendedMode();
    }

    BOOL WINAPI CLegacySalamanderMaskGroup::PrepareMasks(int& errorPos)
    {
        const BOOL result = WideMaskGroup.PrepareMasks(errorPos);
        if (!result)
            errorPos = LegacyBytePosition(LegacyMasks, errorPos);
        return result;
    }

    BOOL WINAPI CLegacySalamanderMaskGroup::AgreeMasks(const char* fileName, const char* fileExt)
    {
        std::wstring wideName;
        std::wstring wideExt;
        if ((fileName != nullptr && !WidenPluginText(fileName, wideName)) ||
            (fileExt != nullptr && !WidenPluginText(fileExt, wideExt)))
            return FALSE;
        return WideMaskGroup.AgreeMasks(
            fileName != nullptr ? wideName.c_str() : nullptr,
            fileExt != nullptr ? wideExt.c_str() : nullptr);
    }

    CLegacySalamanderBMSearchData::CLegacySalamanderBMSearchData(::CSalamanderBMSearchData& wideSearchData)
        : WideSearchData(wideSearchData)
    {
    }

    void WINAPI CLegacySalamanderBMSearchData::Set(const char* pattern, WORD flags)
    {
        WideSearchData.Set(pattern, flags);
    }

    void WINAPI CLegacySalamanderBMSearchData::Set(const char* pattern, const int length, WORD flags)
    {
        WideSearchData.Set(pattern, length, flags);
    }

    void WINAPI CLegacySalamanderBMSearchData::SetFlags(WORD flags)
    {
        WideSearchData.SetFlags(flags);
    }

    int WINAPI CLegacySalamanderBMSearchData::GetLength() const
    {
        return WideSearchData.GetLength();
    }

    const char* WINAPI CLegacySalamanderBMSearchData::GetPattern() const
    {
        return WideSearchData.GetPattern();
    }

    BOOL WINAPI CLegacySalamanderBMSearchData::IsGood() const
    {
        return WideSearchData.IsGood();
    }

    int WINAPI CLegacySalamanderBMSearchData::SearchForward(const char* text, int length, int start)
    {
        return WideSearchData.SearchForward(text, length, start);
    }

    int WINAPI CLegacySalamanderBMSearchData::SearchBackward(const char* text, int length)
    {
        return WideSearchData.SearchBackward(text, length);
    }

    CLegacySalamanderREGEXPSearchData::CLegacySalamanderREGEXPSearchData(::CSalamanderREGEXPSearchData& wideSearchData)
        : WideSearchData(wideSearchData)
    {
    }

    BOOL WINAPI CLegacySalamanderREGEXPSearchData::Set(const char* pattern, WORD flags)
    {
        return WideSearchData.Set(pattern, flags);
    }

    BOOL WINAPI CLegacySalamanderREGEXPSearchData::SetFlags(WORD flags)
    {
        return WideSearchData.SetFlags(flags);
    }

    const char* WINAPI CLegacySalamanderREGEXPSearchData::GetLastErrorText() const
    {
        return WideSearchData.GetLastErrorText();
    }

    const char* WINAPI CLegacySalamanderREGEXPSearchData::GetPattern() const
    {
        return WideSearchData.GetPattern();
    }

    BOOL WINAPI CLegacySalamanderREGEXPSearchData::SetLine(const char* start, const char* end)
    {
        return WideSearchData.SetLine(start, end);
    }

    int WINAPI CLegacySalamanderREGEXPSearchData::SearchForward(int start, int& foundLen)
    {
        return WideSearchData.SearchForward(start, foundLen);
    }

    int WINAPI CLegacySalamanderREGEXPSearchData::SearchBackward(int length, int& foundLen)
    {
        return WideSearchData.SearchBackward(length, foundLen);
    }

    CLegacySalamanderMD5::CLegacySalamanderMD5(::CSalamanderMD5& wideMD5)
        : WideMD5(wideMD5)
    {
    }

    void WINAPI CLegacySalamanderMD5::Init()
    {
        WideMD5.Init();
    }

    void WINAPI CLegacySalamanderMD5::Update(const void* input, DWORD inputLength)
    {
        WideMD5.Update(input, inputLength);
    }

    void WINAPI CLegacySalamanderMD5::Finalize()
    {
        WideMD5.Finalize();
    }

    void WINAPI CLegacySalamanderMD5::GetDigest(void* dest)
    {
        WideMD5.GetDigest(dest);
    }

    class CLegacyGeneralObjectOwner::CState
    {
    public:
        COwnedWrappers<sdk107::CSalamanderMaskGroup, ::CSalamanderMaskGroup, CLegacySalamanderMaskGroup> MaskGroups;
        COwnedWrappers<sdk107::CSalamanderBMSearchData, ::CSalamanderBMSearchData, CLegacySalamanderBMSearchData> BMSearchData;
        COwnedWrappers<sdk107::CSalamanderREGEXPSearchData, ::CSalamanderREGEXPSearchData, CLegacySalamanderREGEXPSearchData> REGEXPSearchData;
        COwnedWrappers<sdk107::CSalamanderMD5, ::CSalamanderMD5, CLegacySalamanderMD5> MD5;
        COwnedWrappers<sdk107::CSalamanderDirectoryAbstract, ::CSalamanderDirectoryAbstract, CLegacySalamanderDirectory> Directories;
    };

    CLegacyGeneralObjectOwner::CLegacyGeneralObjectOwner()
        : State(std::make_unique<CState>())
    {
    }

    CLegacyGeneralObjectOwner::~CLegacyGeneralObjectOwner() = default;

    sdk107::CSalamanderMaskGroup* CLegacyGeneralObjectOwner::PublishMaskGroup(::CSalamanderMaskGroup* wide)
    {
        return State->MaskGroups.Publish(wide);
    }

    ::CSalamanderMaskGroup* CLegacyGeneralObjectOwner::FindMaskGroup(sdk107::CSalamanderMaskGroup* legacy) const
    {
        return State->MaskGroups.Find(legacy);
    }

    ::CSalamanderMaskGroup* CLegacyGeneralObjectOwner::RetireMaskGroup(sdk107::CSalamanderMaskGroup* legacy)
    {
        return State->MaskGroups.Retire(legacy);
    }

    sdk107::CSalamanderBMSearchData* CLegacyGeneralObjectOwner::PublishBMSearchData(::CSalamanderBMSearchData* wide)
    {
        return State->BMSearchData.Publish(wide);
    }

    ::CSalamanderBMSearchData* CLegacyGeneralObjectOwner::FindBMSearchData(sdk107::CSalamanderBMSearchData* legacy) const
    {
        return State->BMSearchData.Find(legacy);
    }

    ::CSalamanderBMSearchData* CLegacyGeneralObjectOwner::RetireBMSearchData(sdk107::CSalamanderBMSearchData* legacy)
    {
        return State->BMSearchData.Retire(legacy);
    }

    sdk107::CSalamanderREGEXPSearchData* CLegacyGeneralObjectOwner::PublishREGEXPSearchData(::CSalamanderREGEXPSearchData* wide)
    {
        return State->REGEXPSearchData.Publish(wide);
    }

    ::CSalamanderREGEXPSearchData* CLegacyGeneralObjectOwner::FindREGEXPSearchData(sdk107::CSalamanderREGEXPSearchData* legacy) const
    {
        return State->REGEXPSearchData.Find(legacy);
    }

    ::CSalamanderREGEXPSearchData* CLegacyGeneralObjectOwner::RetireREGEXPSearchData(sdk107::CSalamanderREGEXPSearchData* legacy)
    {
        return State->REGEXPSearchData.Retire(legacy);
    }

    sdk107::CSalamanderMD5* CLegacyGeneralObjectOwner::PublishMD5(::CSalamanderMD5* wide)
    {
        return State->MD5.Publish(wide);
    }

    ::CSalamanderMD5* CLegacyGeneralObjectOwner::FindMD5(sdk107::CSalamanderMD5* legacy) const
    {
        return State->MD5.Find(legacy);
    }

    ::CSalamanderMD5* CLegacyGeneralObjectOwner::RetireMD5(sdk107::CSalamanderMD5* legacy)
    {
        return State->MD5.Retire(legacy);
    }

    sdk107::CSalamanderDirectoryAbstract* CLegacyGeneralObjectOwner::PublishDirectory(::CSalamanderDirectoryAbstract* wide, CLegacyPluginDataResolver& pluginDataResolver, int builtForVersion)
    {
        return State->Directories.Publish(wide, pluginDataResolver, builtForVersion);
    }

    ::CSalamanderDirectoryAbstract* CLegacyGeneralObjectOwner::FindDirectory(sdk107::CSalamanderDirectoryAbstract* legacy) const
    {
        return State->Directories.Find(legacy);
    }

    ::CSalamanderDirectoryAbstract* CLegacyGeneralObjectOwner::RetireDirectory(sdk107::CSalamanderDirectoryAbstract* legacy)
    {
        return State->Directories.Retire(legacy);
    }

} // namespace sally::compat
