// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "ClipboardCopyMoveBridge.h"
#include "ClipboardPasteService.h"
#include "shellib.h"

namespace sally
{
    namespace clipboard
    {
        bool CreateCopyMoveData(const ClipboardFileTransfer& transfer,
                                CCopyMoveData** data) noexcept
        {
            if (data == nullptr)
                return false;
            *data = nullptr;
            if (transfer.SourcePaths.empty())
                return false;

            CCopyMoveData* records = nullptr;
            try
            {
                records = new CCopyMoveData(100, 50);
            }
            catch (const std::bad_alloc&)
            {
                return false;
            }
            if (records == nullptr)
                return false;
            records->MakeCopyOfName = transfer.MakeCopyOfName ? TRUE : FALSE;

            for (const std::wstring& path : transfer.SourcePaths)
            {
                if (path.empty())
                {
                    records->ResetState();
                    break;
                }
                CCopyMoveRecord* record = nullptr;
                try
                {
                    record = new CCopyMoveRecord(path.c_str(), nullptr);
                }
                catch (const std::bad_alloc&)
                {
                    records->ResetState();
                    break;
                }
                if (record == nullptr || !record->IsValid())
                {
                    delete record;
                    records->ResetState();
                    break;
                }
                records->Add(record);
                if (!records->IsGood())
                {
                    records->ResetState();
                    break;
                }
            }

            if (!records->IsGood() || records->Count == 0)
            {
                DestroyCopyMoveData(records);
                return false;
            }

            *data = records;
            return true;
        }
    } // namespace clipboard
} // namespace sally
