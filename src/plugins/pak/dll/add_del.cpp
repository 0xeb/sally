// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression

#include "texts.rh2"
#include "array2.h"
#include "pakiface.h"
#include "pak_dll.h"
#include "../spl/pak_text.h"

#include <cstring>

// ****************************************************************************
//
// CDelRegion
//

CDelRegion::CDelRegion(const char* descript, DWORD offset, DWORD size, const char* fileName)
    : Description(descript != nullptr ? descript : ""),
      Offset(offset),
      Size(size),
      FileName(fileName != nullptr ? fileName : ""),
      HasFileName(fileName != nullptr)
{
}

// ****************************************************************************
//
// CPakIface
//

BOOL CPakIface::MarkForDelete()
{
    std::string descr;
    if (!GetName(PakDir[DirPos].FileName, descr))
        return FALSE;

    CDelRegion* region = nullptr;
    try
    {
        const std::string rawFileName(
            PakDir[DirPos].FileName,
            strnlen(PakDir[DirPos].FileName, sizeof(PakDir[DirPos].FileName)));
        region = new CDelRegion(descr.c_str(), PakDir[DirPos].Offset, PakDir[DirPos].Size,
                                rawFileName.c_str());
    }
    catch (...)
    {
        return HandleError(0, IDS_PAK_LOWMEMORY);
    }
    if (!region)
        return HandleError(0, IDS_PAK_LOWMEMORY);

    if (region->Size && DelRegions.Add(region) ||
        !region->Size && ZeroSizedFiles.Add(region))
        return TRUE;
    else
    {
        delete region;
        return HandleError(0, IDS_PAK_LOWMEMORY);
    }
}

BOOL CPakIface::GetDelProgressTotal(unsigned* progressSize)
{
    if (DelRegions.Count == 0 && ZeroSizedFiles.Count == 0)
    {
        *progressSize = 0;
        return TRUE;
    }

    CDelRegion* region;
    DWORD smallestOffs = Header.DirOffset;
    DWORD size = 0;

    int i;
    for (i = 0; i < DelRegions.Count; i++)
    {
        region = DelRegions[i];
        size += region->Size;
        if (region->Offset < smallestOffs)
            smallestOffs = region->Offset;
    }

    *progressSize = PakSize - size - smallestOffs - DirSize * sizeof(CPackEntry) +
                    DelRegions.Count + 1 + ZeroSizedFiles.Count;
    return TRUE;
}

//#define REGION_OFFSET(i) (((CDelRegion *)(*regions)[i])->Offset)

void Sort(int left, int right, TIndirectArray2<CDelRegion>& regions)
{
    int i = left, j = right;
    DWORD pivotOffset = regions[(i + j) / 2]->Offset;
    do
    {
        while (regions[i]->Offset < pivotOffset && i < right)
            i++;
        while (pivotOffset < regions[j]->Offset && j > left)
            j--;
        if (i <= j)
        {
            CDelRegion* tmp = regions[i];
            regions[i] = regions[j];
            regions[j] = tmp;
            i++;
            j--;
        }
    } while (i <= j); //do they have to match?
    if (left < j)
        Sort(left, j, regions);
    if (i < right)
        Sort(i, right, regions);
}

static bool RawPakNameEquals(const std::string& name, const char* rawName, size_t capacity)
{
    const size_t rawLength = strnlen(rawName, capacity);
    return name.size() == rawLength &&
           std::memcmp(name.data(), rawName, rawLength) == 0;
}

void CPakIface::UpdateDir(CDelRegion* region, DWORD topOffset, DWORD delta)
{
    if (region->HasFileName)
    {
        unsigned i;
        for (i = 0; i < DirSize; i++)
        {
            if (RawPakNameEquals(region->FileName, PakDir[i].FileName,
                                 sizeof(PakDir[i].FileName)))
            {
                memmove(&PakDir[i], &PakDir[i + 1],
                        (DirSize - i - 1) * sizeof(CPackEntry));
                --DirSize;
                break;
            }
        }
    }
    unsigned i;
    for (i = 0; i < DirSize; i++)
    {
        if (PakDir[i].Offset > region->Offset && PakDir[i].Offset < topOffset)
            PakDir[i].Offset -= delta;
    }
}

BOOL CPakIface::DeleteZeroSized()
{
    CDelRegion* region;

    int i;
    for (i = 0; i < ZeroSizedFiles.Count; i++)
    {
        region = ZeroSizedFiles[i];
        if (!Callbacks->DelNotify(region->Description.c_str(), 1))
            return FALSE;
        unsigned j;
        for (j = 0; j < DirSize; j++)
        {
            if (RawPakNameEquals(region->FileName, PakDir[j].FileName,
                                 sizeof(PakDir[j].FileName)))
            {
                memmove(&PakDir[j], &PakDir[j + 1], (DirSize - j - 1) * sizeof(CPackEntry));
                --DirSize;
                break;
            }
        }
        if (!Callbacks->AddProgress(1))
            return FALSE;
    }
    return TRUE;
}

BOOL CPakIface::MoveData(DWORD writePos, DWORD readPos, DWORD size, BOOL* userBreak)
{
    char buffer[IOBUFSIZE];
    DWORD read;

    while (size)
    {
        read = size > IOBUFSIZE ? IOBUFSIZE : size;
        if (!SafeSeek(PakFile, readPos) ||
            !SafeRead(PakFile, buffer, read) ||
            !SafeSeek(PakFile, writePos) ||
            !SafeWrite(PakFile, buffer, read))
            return FALSE;
        if (!Callbacks->AddProgress(read))
            *userBreak = TRUE;
        size -= read;
        writePos += read;
        readPos += read;
    }
    return TRUE;
}

BOOL CPakIface::DeleteFiles(BOOL* needOptim)
{
    *needOptim = FALSE;

    if (DelRegions.Count == 0 && ZeroSizedFiles.Count == 0)
        return TRUE;

    if (ZeroSizedFiles.Count)
    {
        BOOL ret = DeleteZeroSized();
        ZeroSizedFiles.Destroy();
        if (!ret)
            return FALSE;
    }

    // DelNotify is the legacy byte callback used for progress labels. Encode this one
    // localized label explicitly and transactionally at that boundary.
    std::string centralDirName;
    if (EncodePakText(LangStr(IDS_PAK_CENTRDIR), centralDirName) != PakTextStatus::Success)
    {
        DelRegions.Destroy();
        return HandleError(0, IDS_PAK_LOWMEMORY);
    }
    CDelRegion* region = nullptr;
    try
    {
        region = new CDelRegion(centralDirName.c_str(), Header.DirOffset, Header.DirSize, NULL);
    }
    catch (...)
    {
        DelRegions.Destroy();
        return HandleError(0, IDS_PAK_LOWMEMORY);
    }
    if (!region)
    {
        DelRegions.Destroy();
        return HandleError(0, IDS_PAK_LOWMEMORY);
    }
    if (!DelRegions.Add(region))
    {
        delete region;
        DelRegions.Destroy();
        return HandleError(0, IDS_PAK_LOWMEMORY);
    }

    Sort(0, DelRegions.Count - 1, DelRegions);

    BOOL ub = FALSE;
    DWORD rpos;
    DWORD size;
    DWORD wpos = DelRegions[0]->Offset;
    int i;
    for (i = 0; i < DelRegions.Count; i++)
    {
        region = DelRegions[i];

        rpos = region->Offset + region->Size;
        if (i + 1 < DelRegions.Count)
            size = DelRegions[i + 1]->Offset - rpos;
        else
            size = PakSize - rpos;
        if (!Callbacks->DelNotify(region->Description.c_str(), size + 1))
            break;
        if (!MoveData(wpos, rpos, size, &ub))
            break;
        UpdateDir(region, rpos + size, rpos - wpos);
        wpos += size;
        if (!ub)
            ub = !Callbacks->AddProgress(1);
        if (ub)
        {
            i++;
            break;
        }
    }
    if (i == DelRegions.Count)
    {
        Header.DirOffset = wpos;
    }
    else
    {
        if (i > 0)
            *needOptim = TRUE;
    }

    Header.DirSize = DirSize * sizeof(CPackEntry);
    if (!SafeSeek(PakFile, Header.DirOffset))
        return FALSE;
    if (!SafeWrite(PakFile, PakDir, Header.DirSize))
        return FALSE;
    SetEndOfFile(PakFile);
    if (!SafeSeek(PakFile, 0))
        return FALSE;
    if (!SafeWrite(PakFile, &Header, sizeof(CPackHeader)))
        return FALSE;
    PakSize = Header.DirOffset + Header.DirSize;

    BOOL ret;
    if (ub || i < DelRegions.Count)
        ret = FALSE;
    DelRegions.Destroy();
    return ret;
}

BOOL CPakIface::StartAdding(unsigned count)
{
    if (EmptyPak)
        AddPos = sizeof(CPackHeader);
    else
    {
        AddPos = Header.DirOffset + Header.DirSize < PakSize ? PakSize : Header.DirOffset;
    }
    void* p = realloc(PakDir, (DirSize + count) * sizeof(CPackEntry));
    if (!p)
        return HandleError(0, IDS_PAK_LOWMEMORY);
    PakDir = (CPackEntry*)p;
    return TRUE;
}

BOOL CPakIface::AddFile(const char* fileName, DWORD size)
{
    if (fileName == nullptr)
        return HandleError(0, IDS_PAK_INVLAIDDATA);
    char buffer[IOBUFSIZE];
    std::string storedName;
    try
    {
        for (const char* source = fileName; *source != '\0'; ++source)
        {
            if (*source == '\\')
            {
                storedName.push_back('/');
                continue;
            }
            if (strncmp(source, PAK_UPDIR, lstrlenA(PAK_UPDIR)) == 0)
            {
                storedName += "..";
                source += lstrlenA(PAK_UPDIR) - 1;
                continue;
            }
            storedName.push_back(*source);
        }
    }
    catch (...)
    {
        return HandleError(0, IDS_PAK_LOWMEMORY);
    }
    if (storedName.size() >= sizeof(PakDir[DirSize].FileName))
        return HandleError(0, IDS_PAK_TOOLONGNAME);

    std::memset(PakDir[DirSize].FileName, 0, sizeof(PakDir[DirSize].FileName));
    std::memcpy(PakDir[DirSize].FileName, storedName.data(), storedName.size());
    PakDir[DirSize].Offset = AddPos;
    PakDir[DirSize].Size = size;

    if (!SafeSeek(PakFile, AddPos))
        return FALSE;
    DWORD left = size;
    int read;
    while (left)
    {
        read = left > IOBUFSIZE ? IOBUFSIZE : left;
        if (!Callbacks->Read(buffer, read))
            return FALSE;
        if (!SafeWrite(PakFile, buffer, read))
            return FALSE;
        if (!Callbacks->AddProgress(read))
            return FALSE;
        left -= read;
    }

    AddPos += size;
    DirSize++;
    return TRUE;
}

BOOL CPakIface::FinalizeAdding()
{
    Header.Pack = 0x4b434150;
    Header.DirSize = DirSize * sizeof(CPackEntry);
    Header.DirOffset = AddPos;
    if (!SafeSeek(PakFile, Header.DirOffset))
        return FALSE;
    if (!SafeWrite(PakFile, PakDir, Header.DirSize))
        return FALSE;
    SetEndOfFile(PakFile);
    if (!SafeSeek(PakFile, 0))
        return FALSE;
    if (!SafeWrite(PakFile, &Header, sizeof(CPackHeader)))
        return FALSE;
    PakSize = Header.DirOffset + Header.DirSize;
    return TRUE;
}

void CPakIface::GetOptimizedState(DWORD* pakSize, DWORD* validData)
{
    if (EmptyPak)
    {
        *pakSize = 0;
        *validData = 0;
        return;
    }
    DWORD valid = 0;
    valid += sizeof(CPackHeader);
    valid += DirSize * sizeof(CPackEntry);
    unsigned i;
    for (i = 0; i < DirSize; i++)
    {
        valid += PakDir[i].Size;
    }
    *pakSize = PakSize;
    *validData = valid;
}

//#define FILE_OFFSET(i) (((CPackEntry *)(*files)[i])->Offset)

void SortFiles(int left, int right, TIndirectArray2<CPackEntry>& files)
{
    int i = left, j = right;
    DWORD pivotOffset = files[(i + j) / 2]->Offset;
    do
    {
        while (files[i]->Offset < pivotOffset && i < right)
            i++;
        while (pivotOffset < files[j]->Offset && j > left)
            j--;
        if (i <= j)
        {
            CPackEntry* tmp = files[i];
            files[i] = files[j];
            files[j] = tmp;
            i++;
            j--;
        }
    } while (i <= j); // Do they need to match?
    if (left < j)
        SortFiles(left, j, files);
    if (i < right)
        SortFiles(i, right, files);
}

BOOL CPakIface::InitOptimalization(unsigned* progressTotal)
{
    TIndirectArray2<CPackEntry> a(256, FALSE);
    CPackEntry header;
    CPackEntry pakEnd;

    header.Offset = 0;
    header.Size = sizeof(CPackHeader);
    pakEnd.Offset = PakSize;
    pakEnd.Size = 0;
    if (!a.Add(&header) || !a.Add(&pakEnd))
        return HandleError(0, IDS_PAK_LOWMEMORY);

    unsigned u;
    for (u = 0; u < DirSize; u++)
    {
        if (PakDir[u].Size && !a.Add(PakDir + u))
            HandleError(0, IDS_PAK_LOWMEMORY);
    }

    SortFiles(0, a.Count - 1, a);

    DWORD lastEnd = 0;
    CDelRegion* region;
    CPackEntry* file;
    DWORD prog = PakSize;
    int i;
    for (i = 0; i < a.Count; i++)
    {
        file = a[(int)i];
        if (lastEnd < file->Offset)
        {
            region = new CDelRegion(NULL, lastEnd, file->Offset - lastEnd, NULL);
            if (!region)
            {
                DelRegions.Destroy();
                return HandleError(0, IDS_PAK_LOWMEMORY);
            }
            if (!DelRegions.Add(region))
            {
                delete region;
                DelRegions.Destroy();
                return HandleError(0, IDS_PAK_LOWMEMORY);
            }
            prog -= region->Size;
        }
        lastEnd = file->Offset + file->Size;
    }
    Sort(0, DelRegions.Count - 1, DelRegions);
    *progressTotal = prog - DelRegions[0]->Offset;
    return TRUE;
}

BOOL CPakIface::OptimizePak()
{
    if (DelRegions.Count == 0)
        return TRUE;

    CDelRegion* region;
    BOOL ub = FALSE;
    DWORD rpos;
    DWORD size;
    DWORD wpos = DelRegions[0]->Offset;
    int i;
    for (i = 0; i < DelRegions.Count; i++)
    {
        region = DelRegions[i];

        rpos = region->Offset + region->Size;
        if (i + 1 < DelRegions.Count)
            size = DelRegions[i + 1]->Offset - rpos;
        else
            size = PakSize - rpos;
        if (!MoveData(wpos, rpos, size, &ub))
            break;
        UpdateDir(region, rpos + size, rpos - wpos);
        wpos += size;
        if (ub)
        {
            i++;
            break;
        }
    }
    if (i == DelRegions.Count)
    {
        Header.DirOffset = wpos;
    }

    if (!SafeSeek(PakFile, Header.DirOffset))
        return FALSE;
    if (!SafeWrite(PakFile, PakDir, Header.DirSize))
        return FALSE;
    SetEndOfFile(PakFile);
    if (!SafeSeek(PakFile, 0))
        return FALSE;
    if (!SafeWrite(PakFile, &Header, sizeof(CPackHeader)))
        return FALSE;
    PakSize = Header.DirOffset + Header.DirSize;

    BOOL ret;
    if (ub || i < DelRegions.Count)
        ret = FALSE;
    else
        ret = TRUE;
    DelRegions.Destroy();
    return ret;
}
