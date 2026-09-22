// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "peviewer.h"
#include "pefile.h"
#include "dump_res.h"
#include "peviewer_text.h"

std::string GetName(LPVOID ptr)
{
    const USHORT len = *(USHORT*)ptr;
    std::string name;
    if (!EncodePeviewerResourceName(std::wstring_view(MakePtr(LPWSTR, ptr, 2), len), name))
        return std::string();
    return name;
}

char* MakeSpace(char* buffer, int length)
{
    char* iterator = buffer;
    while (iterator < buffer + length)
        *iterator++ = ' ';
    buffer[length] = 0;
    return buffer;
}

std::string GetType(DWORD id, int level)
{
    if (level == 1)
        return std::string();
    const char* str = NULL;
    std::string localeName;
    if (level == 0)
    {
        switch (id)
        {
        case (ULONG_PTR)RT_CURSOR:
            str = "Hardware-dependent cursor resource";
            break;
        case (ULONG_PTR)RT_BITMAP:
            str = "Bitmap resource";
            break;
        case (ULONG_PTR)RT_ICON:
            str = "Hardware-dependent icon resource";
            break;
        case (ULONG_PTR)RT_MENU:
            str = "Menu resource";
            break;
        case (ULONG_PTR)RT_DIALOG:
            str = "Dialog box";
            break;
        case (ULONG_PTR)RT_STRING:
            str = "String-table entry";
            break;
        case (ULONG_PTR)RT_FONTDIR:
            str = "Font directory resource";
            break;
        case (ULONG_PTR)RT_FONT:
            str = "Font resource";
            break;
        case (ULONG_PTR)RT_ACCELERATOR:
            str = "Accelerator table";
            break;
        case (ULONG_PTR)RT_RCDATA:
            str = "Application-defined resource (raw data)";
            break;
        case (ULONG_PTR)RT_MESSAGETABLE:
            str = "Message-table entry";
            break;
        case (ULONG_PTR)RT_GROUP_CURSOR:
            str = "Hardware-independent cursor resource";
            break;
        case (ULONG_PTR)RT_GROUP_ICON:
            str = "Hardware-independent icon resource";
            break;
        case (ULONG_PTR)RT_VERSION:
            str = "Version resource";
            break;
        case (ULONG_PTR)RT_DLGINCLUDE:
            str = "Dialog include";
            break;
        case (ULONG_PTR)RT_PLUGPLAY:
            str = "Plug and play resource";
            break;
        case (ULONG_PTR)RT_VXD:
            str = "VXD";
            break;
        case (ULONG_PTR)RT_ANICURSOR:
            str = "Animated cursor";
            break;
        case (ULONG_PTR)RT_ANIICON:
            str = "Animated icon";
            break;
        case (ULONG_PTR)RT_HTML:
            str = "HTML document";
            break;
        case (ULONG_PTR)RT_MANIFEST:
            str = "Manifest";
            break;
        }
    }
    else
    {
        if (id && GetPeviewerLocaleLanguageText(static_cast<LANGID>(id), localeName))
            str = localeName.c_str();
    }
    if (str)
        return std::string(" (") + str + ")";
    return std::string();
}

BOOL DumpResDirectory(LPVOID lpFile, DWORD fileSize, CFileStream* outStream,
                      DWORD pointerToRawData, DWORD virtAddr, DWORD dirRootOffset,
                      DWORD dirOffset, int level)
{
    PIMAGE_RESOURCE_DIRECTORY rsdirh = MakePtr(PIMAGE_RESOURCE_DIRECTORY, lpFile, dirOffset);
    UINT width = 31;

    char buf[100];
    int space;
    const char* head;
    switch (level)
    {
    case 0:
        head = "%sTypes Directory Table:\n";
        space = 2;
        break;
    case 1:
        head = "%sName Directory Table:\n";
        space = 6;
        break;
    case 2:
        head = "%sLanguages Directory Table:\n";
        space = 10;
        break;
    }
    outStream->fprintf(head, MakeSpace(buf, space));
    space += 2;
    outStream->fprintf("%s%-*s0x%08X\n", MakeSpace(buf, space), width, "Characteristics:", rsdirh->Characteristics);
    std::string timestamp;
    FormatPeviewerTimestamp(rsdirh->TimeDateStamp, timestamp);
    outStream->fprintf("%s%-*s0x%08X%s\n", MakeSpace(buf, space), width, "Time Date Stamp:",
                       rsdirh->TimeDateStamp, timestamp.c_str());
    outStream->fprintf("%s%-*s%u.%02u\n", MakeSpace(buf, space), width, "Version:", rsdirh->MajorVersion, rsdirh->MinorVersion);
    outStream->fprintf("%s%-*s%u\n", MakeSpace(buf, space), width, "Number of Name Entries:", rsdirh->NumberOfNamedEntries);
    outStream->fprintf("%s%-*s%u\n", MakeSpace(buf, space), width, "Number of ID Entries:", rsdirh->NumberOfIdEntries);

    PIMAGE_RESOURCE_DIRECTORY_ENTRY rsdir = MakePtr(PIMAGE_RESOURCE_DIRECTORY_ENTRY, rsdirh,
                                                    sizeof(IMAGE_RESOURCE_DIRECTORY));
    int n = rsdirh->NumberOfNamedEntries;
    int j;
    for (j = 0; j < 2; j++)
    {
        int i;
        for (i = 0; i < n; i++, rsdir++)
        {
            if (j == 0)
            {
                const char* s;
                switch (level)
                {
                case 0:
                    s = "Type:";
                    break;
                case 1:
                    s = "Name:";
                    break;
                case 2:
                    s = "Language Name:";
                    break;
                }
                const std::string name = GetName(MakePtr(LPVOID, lpFile, dirRootOffset + rsdir->NameOffset));
                outStream->fprintf("%s%s %s\n", MakeSpace(buf, space), s, name.c_str());
            }
            else
            {
                const char* s;
                switch (level)
                {
                case 0:
                    s = "Type:";
                    break;
                case 1:
                    s = "ID:";
                    break;
                case 2:
                    s = "Language ID:";
                    break;
                }
                const std::string type = GetType(rsdir->Id, level);
                outStream->fprintf("%s%s %u%s\n", MakeSpace(buf, space), s, rsdir->Id, type.c_str());
            }
            if (level < 2)
            {
                space += 2;
                outStream->fprintf("%s%-*s0x%08X\n", MakeSpace(buf, space), width, "Subdirectory RVA:", rsdir->OffsetToDirectory);
                if (!DumpResDirectory(lpFile, fileSize, outStream, pointerToRawData, virtAddr, dirRootOffset, rsdir->OffsetToDirectory + dirRootOffset,
                                      level + 1))
                    return FALSE;
            }
            else
            {
                space += 2;
                outStream->fprintf("%s%-*s0x%08X\n", MakeSpace(buf, space), width, "Data Entry RVA:", rsdir->OffsetToData);
                //outStream->fprintf("%sData Entry:\n", MakeSpace(buf, space));
                PIMAGE_RESOURCE_DATA_ENTRY rsdata = MakePtr(PIMAGE_RESOURCE_DATA_ENTRY, lpFile,
                                                            rsdir->OffsetToData + dirRootOffset);
                outStream->fprintf("%s%-*s0x%08X\n", MakeSpace(buf, space), width, "Offset To Data:", rsdata->OffsetToData);
                outStream->fprintf("%s%-*s0x%08X\n", MakeSpace(buf, space), width, "Offset In File:", rsdata->OffsetToData - virtAddr + pointerToRawData);
                outStream->fprintf("%s%-*s0x%08X (%u)\n", MakeSpace(buf, space), width, "Size:", rsdata->Size, rsdata->Size);
                outStream->fprintf("%s%-*s0x%08X\n", MakeSpace(buf, space), width, "Code Page:", rsdata->CodePage);
                outStream->fprintf("%s%-*s0x%08X\n", MakeSpace(buf, space), width, "Reserved:", rsdata->Reserved);
            }
            space -= 2;
        }
        n = rsdirh->NumberOfIdEntries;
    }

    return TRUE;
}

void CResourceDirectoryDumper::DumpCore(CFileStream* outStream)
{
    PIMAGE_FILE_HEADER pPEFileHeader = m_peFile.GetPEFileHeader();
    PIMAGE_OPTIONAL_HEADER32 poh = m_peFile.GetPEOptionalHeader();
    IMAGE_SECTION_HEADER rsrcSection;

    if (m_peFile.ImageFileType(NULL) != IMAGE_NT_SIGNATURE)
        return;

    // Find the .rsrc section
    if (!m_peFile.GetSectionHdrByName(&rsrcSection, ".rsrc"))
        return;

    DWORD pointerToRawData = rsrcSection.PointerToRawData;
    DWORD virtAddr = rsrcSection.VirtualAddress;
    DWORD dirOffset = pointerToRawData + (!m_peFile.Is64Bit() ? poh->DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].VirtualAddress : ((PIMAGE_OPTIONAL_HEADER64)poh)->DataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE].VirtualAddress) - virtAddr;

    outStream->fprintf("Resource Directory:\n");
    DumpResDirectory(m_peFile.GetMappingBaseAddress(), m_peFile.GetMappingSize(),
                     outStream, pointerToRawData,
                     virtAddr, dirOffset, dirOffset, 0);
}

const _TCHAR* CResourceDirectoryDumper::GetExceptionMessage()
{
    return L"Resource Directory is corrupted.";
}
