// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// According to http://www.microsoft.com/resources/documentation/windows/xp/all/proddocs/en-us/label.mspx?mfr=true
// we need 32 characters for NTFS, 11 characters for FAT
#define MAX_VOLNAME 100
#define MAX_FSNAME 50

template <typename CHAR>
struct VolumeDetails
{
    std::basic_string<CHAR> MountPoint; // Path the volume is mounted on, e.g "C:\"
    std::basic_string<CHAR> GUIDPath;   // Unique identifier of the volume (valid only on W2k and higher)
    std::basic_string<CHAR> FSName;     // The name of the volume file system, e.g. NTFS, FAT32, FAT
    std::basic_string<CHAR> VolumeName; // The name (label) assigned to the volume by the user
    VolumeType Type;              // Type of the volume (fixed, removable etc.)
    CQuadWord BytesTotal;         // The total size of the volume (may not be accurate if quota is in effect)
    CQuadWord BytesFree;          // The total free space on the volume
};

template <typename CHAR>
class VolumeListing : public TIndirectArray<VolumeDetails<CHAR>>
{
public:
    VolumeListing() : TIndirectArray<VolumeDetails<CHAR>>(5, 5) {}
};

template <typename CHAR>
struct DiskRecord
{
    DiskRecord() : DiskName(NULL) { VolumeName[0] = 0; }
    ~DiskRecord()
    {
        if (DiskName)
            delete[] DiskName;
    }

    CHAR VolumeName[MAX_VOLNAME];
    CHAR* DiskName;
};

template <typename CHAR>
class DiskRecArray : public TIndirectArray<DiskRecord<CHAR>>
{
public:
    DiskRecArray() : TIndirectArray<DiskRecord<CHAR>>(5, 5) {}
};

template <typename CHAR>
struct VolumeRecord
{
    VolumeRecord() : MountPoints(5, 5), Root(NULL) { VolumeName[0] = 0; }
    ~VolumeRecord() { erase(); }

    void erase()
    {
        VolumeName[0] = 0;
        if (Root)
            delete[] Root;
        while (MountPoints.Count)
        {
            delete[] MountPoints[MountPoints.Count - 1];
            MountPoints.Delete(MountPoints.Count - 1);
        }
    }

    CHAR VolumeName[MAX_VOLNAME];
    CHAR* Root;
    TDirectArray<CHAR*> MountPoints;
};

template <typename CHAR>
class VolumeRecArray : public TIndirectArray<VolumeRecord<CHAR>>
{
public:
    VolumeRecArray() : TIndirectArray<VolumeRecord<CHAR>>(5, 5) {}
};

template <typename CHAR>
BOOL GetLocalDiskDrives(DiskRecArray<CHAR>& disks)
{
    BOOL ret = TRUE;
    if (OS<CHAR>::OS_GetLogicalDriveStringsExists() &&
        OS<CHAR>::OS_GetVolumeNameForVolumeMountPointExists())
    {
        // create initial list of disks in the system
        size_t bufsize = 64;
        CHAR* buffer = NULL;
        DWORD len;
        do
        {
            bufsize *= 2;
            if (buffer != NULL)
                delete[] buffer;
            buffer = new CHAR[bufsize];
            if (buffer != NULL)
                len = OS<CHAR>::OS_GetLogicalDriveStrings(bufsize, buffer);
            else
            {
                String<CHAR>::Error(IDS_UNDELETE, IDS_LOWMEM);
                ret = FALSE;
                break;
            }
        } while (len >= bufsize);

        if (ret)
        {
            CHAR* disk = buffer;
            while (disk && *disk)
            {
                DiskRecord<CHAR>* rec = new DiskRecord<CHAR>;
                if (rec != NULL)
                {
                    rec->DiskName = String<CHAR>::NewStr(disk);
                    rec->VolumeName[0] = 0;
                    // there is no point in adding disks without volume name
                    if (!OS<CHAR>::OS_GetVolumeNameForVolumeMountPoint(disk, rec->VolumeName, MAX_VOLNAME) ||
                        rec->VolumeName[0] == 0)
                    {
                        delete rec;
                    }
                    else
                    {
                        disks.Add(rec);
                        if (!disks.IsGood())
                        {
                            String<CHAR>::Error(IDS_UNDELETE, IDS_LOWMEM);
                            disks.ResetState();
                            delete rec;
                            ret = FALSE;
                            break;
                        }
                    }
                }
                else
                {
                    String<CHAR>::Error(IDS_UNDELETE, IDS_LOWMEM);
                }

                disk = disk + String<CHAR>::StrLen(disk) + 1;
            }
            delete[] buffer;
        }
    }
    return ret;
}

template <typename CHAR>
BOOL GetVolumePathForVolumeName(const CHAR* volumeName, const VolumeRecArray<CHAR>& volumes,
                                std::basic_string<CHAR>& pathName)
{
    pathName.clear();
    if (OS<CHAR>::OS_GetVolumePathNamesForVolumeNameExists())
    {
        size_t capacity = 64;
        const size_t limit = static_cast<size_t>((std::numeric_limits<DWORD>::max)());
        for (;;)
        {
            std::vector<CHAR> buffer(capacity, 0);
            DWORD required = 0;
            if (OS<CHAR>::OS_GetVolumePathNamesForVolumeName(
                    volumeName, buffer.data(), static_cast<DWORD>(buffer.size()), &required))
            {
                pathName.assign(buffer.data());
                return TRUE;
            }
            if (GetLastError() != ERROR_MORE_DATA)
                return FALSE;
            size_t next = required > capacity ? static_cast<size_t>(required) : capacity * 2;
            if (next <= capacity || next > limit)
                return FALSE;
            capacity = next;
        }
    }

    for (int i = 0; i < volumes.Count; ++i)
    {
        if (!String<CHAR>::StrCmp(volumeName, const_cast<VolumeRecArray<CHAR>&>(volumes)[i]->VolumeName))
        {
            const CHAR* root = const_cast<VolumeRecArray<CHAR>&>(volumes)[i]->Root;
            if (root != NULL)
                pathName.assign(root);
            return root != NULL;
        }
    }
    return FALSE;
}

BOOL GetDiskFreeSpace95(const wchar_t* path, LPDWORD lpSectorsPerCluster,
                        LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters,
                        LPDWORD lpTotalNumberOfClusters);
BOOL GetDiskFreeSpace95Aux(const wchar_t* path, LPDWORD lpSectorsPerCluster,
                           LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters,
                           LPDWORD lpTotalNumberOfClusters);

template <typename CHAR>
void GetVolumeDetails(const CHAR* rootPath, int volumeType, const VolumeRecArray<CHAR>& volumes,
                      DiskRecArray<CHAR>& disks, std::basic_string<CHAR>& volumeName,
                      std::basic_string<CHAR>& volumeFSName, CQuadWord& bytesTotal,
                      CQuadWord& bytesFree, std::basic_string<CHAR>* pathName = NULL)
{
    CALL_STACK_MESSAGE1("CConnectDialog::GetVolumeDetails()");

    if (pathName)
    {
        GetVolumePathForVolumeName(rootPath, volumes, *pathName);
    }

    // For floppies, skip check of FS type and free space, it's annoying as it access the medium
    BOOL skipCheck = FALSE;
    if (volumeType == DRIVE_REMOVABLE && IsVolumeFloppy(disks, rootPath))
        skipCheck = TRUE;

    bytesTotal.Set(-1, -1);
    bytesFree.Set(-1, -1);

    if (!skipCheck)
    {
        DWORD volumeSerial, volumeComponentLen, volumeFlags;
        size_t capacity = 64;
        const size_t limit = static_cast<size_t>((std::numeric_limits<DWORD>::max)());
        for (;;)
        {
            std::vector<CHAR> volumeBuffer(capacity, 0);
            std::vector<CHAR> fsBuffer(capacity, 0);
            if (OS<CHAR>::OS_GetVolumeInfo(rootPath, volumeBuffer.data(),
                                           static_cast<DWORD>(volumeBuffer.size()),
                                           &volumeSerial, &volumeComponentLen, &volumeFlags,
                                           fsBuffer.data(), static_cast<DWORD>(fsBuffer.size())))
            {
                volumeName.assign(volumeBuffer.data());
                volumeFSName.assign(fsBuffer.data());
                break;
            }
            if ((GetLastError() != ERROR_MORE_DATA && GetLastError() != ERROR_FILENAME_EXCED_RANGE) ||
                capacity > limit / 2)
            {
                volumeName.clear();
                volumeFSName.clear();
                break;
            }
            capacity *= 2;
        }
        if (OS<CHAR>::OS_GetDiskFreeSpaceExExists())
        {
            ULARGE_INTEGER available, total, free;
            if (OS<CHAR>::OS_GetDiskFreeSpaceEx(rootPath, &available, &total, &free))
            {
                bytesTotal.Set(total.LowPart, total.HighPart);
                bytesFree.Set(free.LowPart, free.HighPart);
            }
        }
        else
        {
            DWORD a;
            DWORD b;
            DWORD c;
            DWORD d;
            BOOL ret;

            if (!String<CHAR>::StrICmp(volumeFSName.c_str(), CVolume<CHAR>::STRING_FAT32))
                ret = GetDiskFreeSpace95(rootPath, &a, &b, &c, &d);
            else
                ret = GetDiskFreeSpace95Aux(rootPath, &a, &b, &c, &d);

            if (ret)
            {
                bytesFree = CQuadWord(a, 0) * CQuadWord(b, 0) * CQuadWord(c, 0);
                bytesTotal = CQuadWord(a, 0) * CQuadWord(b, 0) * CQuadWord(d, 0);
            }
        }
    }
    else
    {
        volumeName.clear();
        volumeFSName.clear();
    }

    // strip trailing slash from mount points
    size_t l;
    if (pathName && (l = pathName->size()) > 3)
    {
        if ((*pathName)[l - 1] == (CHAR)'\\')
            pathName->resize(l - 1);
    }
}

template <typename CHAR>
BOOL IsVolumeFloppy(DiskRecArray<CHAR>& disks, const CHAR* volumeName)
{
    BOOL isFloppy = FALSE;
    const CHAR* removableDriveDiskName = NULL;
    if (volumeName[0] != 0 &&
        volumeName[1] == ':' &&
        volumeName[2] == '\\' &&
        volumeName[3] == 0)
    {
        removableDriveDiskName = volumeName;
    }
    else
    {
        for (int j = 0; j < disks.Count; ++j)
        {
            if (String<CHAR>::StrCmp(disks[j]->VolumeName, volumeName) == 0)
            {
                removableDriveDiskName = disks[j]->DiskName;
                if (removableDriveDiskName[0] == 0 ||
                    removableDriveDiskName[1] != ':' ||
                    removableDriveDiskName[2] != '\\' ||
                    removableDriveDiskName[3] != 0)
                {
                    removableDriveDiskName = NULL;
                }
                else
                    break;
            }
        }
    }
    if (removableDriveDiskName != NULL)
    {
        DWORD formFactor = OS<CHAR>::OS_GetDriveFormFactor(removableDriveDiskName);
        if (formFactor == 350 || formFactor == 525 || formFactor == 800)
            isFloppy = TRUE;
    }
    return isFloppy;
}

template <typename CHAR>
HANDLE FindFirstVolumeOwned(std::basic_string<CHAR>& volumeName)
{
    size_t capacity = 64;
    const size_t limit = static_cast<size_t>((std::numeric_limits<DWORD>::max)());
    for (;;)
    {
        std::vector<CHAR> buffer(capacity, 0);
        HANDLE handle = OS<CHAR>::OS_FindFirstVolume(
            buffer.data(), static_cast<DWORD>(buffer.size()));
        if (handle != INVALID_HANDLE_VALUE)
        {
            volumeName.assign(buffer.data());
            return handle;
        }
        if (GetLastError() != ERROR_FILENAME_EXCED_RANGE || capacity > limit / 2)
            return INVALID_HANDLE_VALUE;
        capacity *= 2;
    }
}

template <typename CHAR>
BOOL FindNextVolumeOwned(HANDLE handle, std::basic_string<CHAR>& volumeName)
{
    size_t capacity = 64;
    const size_t limit = static_cast<size_t>((std::numeric_limits<DWORD>::max)());
    for (;;)
    {
        std::vector<CHAR> buffer(capacity, 0);
        if (OS<CHAR>::OS_FindNextVolume(handle, buffer.data(),
                                        static_cast<DWORD>(buffer.size())))
        {
            volumeName.assign(buffer.data());
            return TRUE;
        }
        if (GetLastError() != ERROR_FILENAME_EXCED_RANGE || capacity > limit / 2)
            return FALSE;
        capacity *= 2;
    }
}

template <typename CHAR>
HANDLE FindFirstVolumeMountPointOwned(const CHAR* volumeName,
                                      std::basic_string<CHAR>& mountPoint)
{
    size_t capacity = 64;
    const size_t limit = static_cast<size_t>((std::numeric_limits<DWORD>::max)());
    for (;;)
    {
        std::vector<CHAR> buffer(capacity, 0);
        HANDLE handle = OS<CHAR>::OS_FindFirstVolumeMountPoint(
            volumeName, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (handle != INVALID_HANDLE_VALUE)
        {
            mountPoint.assign(buffer.data());
            return handle;
        }
        if (GetLastError() != ERROR_FILENAME_EXCED_RANGE || capacity > limit / 2)
            return INVALID_HANDLE_VALUE;
        capacity *= 2;
    }
}

template <typename CHAR>
BOOL FindNextVolumeMountPointOwned(HANDLE handle, std::basic_string<CHAR>& mountPoint)
{
    size_t capacity = 64;
    const size_t limit = static_cast<size_t>((std::numeric_limits<DWORD>::max)());
    for (;;)
    {
        std::vector<CHAR> buffer(capacity, 0);
        if (OS<CHAR>::OS_FindNextVolumeMountPoint(handle, buffer.data(),
                                                  static_cast<DWORD>(buffer.size())))
        {
            mountPoint.assign(buffer.data());
            return TRUE;
        }
        if (GetLastError() != ERROR_FILENAME_EXCED_RANGE || capacity > limit / 2)
            return FALSE;
        capacity *= 2;
    }
}

template <typename CHAR>
void EnumerateAllVolumes(VolumeRecArray<CHAR>& volumes, DiskRecArray<CHAR>& disks)
{
    if (OS<CHAR>::OS_VolumeEnumExists() &&
        OS<CHAR>::OS_VolumeMountPointEnumExists() &&
        OS<CHAR>::OS_GetVolumeNameForVolumeMountPointExists())
    {
        // list all volumes along with their mount points
        CHAR volumeName[MAX_VOLNAME];
        HANDLE hVol = OS<CHAR>::OS_FindFirstVolume(volumeName, MAX_VOLNAME);
        if (hVol != INVALID_HANDLE_VALUE)
        {
            do
            {
                // For floppies, skip check of FS type and free space, it's annoying as it access the medium
                int type = OS<CHAR>::OS_GetVolumeType(volumeName);
                BOOL skipCheck = FALSE;
                if (type == DRIVE_REMOVABLE && IsVolumeFloppy(disks, volumeName))
                    skipCheck = TRUE;

                VolumeRecord<CHAR>* record = new VolumeRecord<CHAR>;
                if (record != NULL)
                {
                    String<CHAR>::StrCpy(record->VolumeName, volumeName);
                    if (!skipCheck)
                    {
                        std::basic_string<CHAR> mntPoint;
                        HANDLE hMntPt = FindFirstVolumeMountPointOwned(volumeName, mntPoint);
                        if (hMntPt != INVALID_HANDLE_VALUE)
                        {
                            do
                            {
                                CHAR* newStr = String<CHAR>::NewStr(mntPoint.c_str());
                                if (newStr != NULL)
                                {
                                    record->MountPoints.Add(newStr);
                                    if (!record->MountPoints.IsGood())
                                    {
                                        String<CHAR>::Error(IDS_UNDELETE, IDS_LOWMEM);
                                        record->MountPoints.ResetState();
                                        delete[] newStr;
                                    }
                                }
                            } while (FindNextVolumeMountPointOwned(hMntPt, mntPoint));
                            OS<CHAR>::OS_FindVolumeMountPointClose(hMntPt);
                        }
                    }
                    volumes.Add(record);
                    if (!volumes.IsGood())
                    {
                        String<CHAR>::Error(IDS_UNDELETE, IDS_LOWMEM);
                        volumes.ResetState();
                        delete record;
                    }
                }
                else
                {
                    String<CHAR>::Error(IDS_UNDELETE, IDS_LOWMEM);
                }
            } while (OS<CHAR>::OS_FindNextVolume(hVol, volumeName, MAX_VOLNAME));
            OS<CHAR>::OS_FindVolumeClose(hVol);
        }

        // now, we need to find volumes to disks, and also add mount point to the list of disks
        // and do this until we find all relations we can
        BOOL updated = TRUE;
        while (updated)
        {
            updated = FALSE;

            for (int i = 0; i < volumes.Count; ++i)
            {
                // if no root yet, try to find one
                if (!volumes[i]->Root)
                {
                    int j;
                    for (j = 0; j < disks.Count; ++j)
                    {
                        if (!String<CHAR>::StrCmp(volumes[i]->VolumeName, disks[j]->VolumeName))
                        {
                            // volume identified,
                            volumes[i]->Root = String<CHAR>::NewStr(disks[j]->DiskName);
                            if (volumes[i]->Root == NULL)
                            {
                                String<CHAR>::Error(IDS_UNDELETE, IDS_LOWMEM);
                            }
                            updated = TRUE;
                            break;
                        }
                    }

                    // now look for mount points
                    if (volumes[i]->Root)
                    {
                        for (j = 0; j < volumes[i]->MountPoints.Count; ++j)
                        {
                            DiskRecord<CHAR>* rec = new DiskRecord<CHAR>;
                            if (rec != NULL)
                            {
                                rec->DiskName = new CHAR[String<CHAR>::StrLen(volumes[i]->Root) +
                                                         String<CHAR>::StrLen(volumes[i]->MountPoints[j]) + 1];
                                if (rec->DiskName != NULL)
                                {
                                    String<CHAR>::StrCpy(rec->DiskName, volumes[i]->Root);
                                    String<CHAR>::StrCat(rec->DiskName, volumes[i]->MountPoints[j]);
                                    OS<CHAR>::OS_GetVolumeNameForVolumeMountPoint(rec->DiskName, rec->VolumeName, MAX_VOLNAME);
                                    disks.Add(rec);
                                    if (!disks.IsGood())
                                    {
                                        String<CHAR>::Error(IDS_UNDELETE, IDS_LOWMEM);
                                        disks.ResetState();
                                        delete rec;
                                    }
                                }
                                else
                                {
                                    String<CHAR>::Error(IDS_UNDELETE, IDS_LOWMEM);
                                    delete rec;
                                }
                            }
                            else
                            {
                                String<CHAR>::Error(IDS_UNDELETE, IDS_LOWMEM);
                            }
                        }
                    }
                }
            }
        }
    }
}

template <typename CHAR>
DWORD GetVolumeListing(VolumeListing<CHAR>& listing)
{
    // get a list of disks connected to the system
    DiskRecArray<CHAR> disks;
    GetLocalDiskDrives(disks);

    // if we got list of volumes, try to assign mount points to all of them
    VolumeRecArray<CHAR> volumes;
    if (!OS<CHAR>::OS_GetVolumePathNamesForVolumeNameExists())
        EnumerateAllVolumes(volumes, disks);

    // enumerate all available volumes
    DWORD err = ERROR_NO_MORE_FILES;
    if (OS<CHAR>::OS_VolumeEnumExists())
    {
        std::basic_string<CHAR> guidPath;

        HANDLE volEnum = FindFirstVolumeOwned(guidPath);
        while (volEnum != INVALID_HANDLE_VALUE)
        {
            VolumeDetails<CHAR>* volumeDetails = new VolumeDetails<CHAR>;
            if (volumeDetails == NULL)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                break;
            }
            volumeDetails->Type = OS<CHAR>::OS_GetVolumeType(guidPath.c_str());

            // consider only fixed and removable drives
            if (volumeDetails->Type == VT_DRIVE_FIXED ||
                volumeDetails->Type == VT_DRIVE_REMOVABLE)
            {
                volumeDetails->GUIDPath = guidPath;
                GetVolumeDetails(guidPath.c_str(), volumeDetails->Type, volumes, disks,
                                 volumeDetails->VolumeName, volumeDetails->FSName,
                                 volumeDetails->BytesTotal, volumeDetails->BytesFree,
                                 &volumeDetails->MountPoint);
                listing.Add(volumeDetails);
                if (!listing.IsGood())
                {
                    listing.ResetState();
                    delete volumeDetails;
                    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                    break;
                }
            }
            else
                delete volumeDetails;

            if (!FindNextVolumeOwned(volEnum, guidPath))
                break;
        }
        err = GetLastError();
        if (INVALID_HANDLE_VALUE != volEnum)
            OS<CHAR>::OS_FindVolumeClose(volEnum);
        if (ERROR_NO_MORE_FILES == err)
            err = ERROR_SUCCESS;
    }
    else
    {
        // old approach using GetLogicalDrives()
        DWORD drives = GetLogicalDrives();
        CHAR root[4];
        String<CHAR>::StrCpy(root, CVolume<CHAR>::STRING_ROOT);

        unsigned long mask = 1;
        for (CHAR i = 'A'; i <= 'Z'; i++, mask <<= 1)
        {
            if (drives & mask)
            {
                root[0] = i;
                VolumeDetails<CHAR>* volumeDetails = new VolumeDetails<CHAR>;
                if (volumeDetails == NULL)
                {
                    err = ERROR_NOT_ENOUGH_MEMORY;
                    break;
                }
                volumeDetails->Type = OS<CHAR>::OS_GetVolumeType(root);
                if (volumeDetails->Type == VT_DRIVE_FIXED ||
                    volumeDetails->Type == VT_DRIVE_REMOVABLE)
                {
                    volumeDetails->MountPoint.assign(root);
                    GetVolumeDetails<CHAR>(root, volumeDetails->Type, volumes, disks,
                                           volumeDetails->VolumeName, volumeDetails->FSName,
                                           volumeDetails->BytesTotal, volumeDetails->BytesFree);
                    listing.Add(volumeDetails);
                    if (!listing.IsGood())
                    {
                        listing.ResetState();
                        delete volumeDetails;
                        err = ERROR_NOT_ENOUGH_MEMORY;
                        break;
                    }
                }
                else
                    delete volumeDetails;
            }
        }
        if (ERROR_NO_MORE_FILES == err)
            err = ERROR_SUCCESS;
    }

    return err;
}
