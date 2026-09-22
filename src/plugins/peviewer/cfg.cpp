// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// vim: noexpandtab:sw=8:ts=8

/*
	PE File Viewer Plugin for Open Salamander

	Copyright (c) 2015-2023 Milan Kase <manison@manison.cz>
	Copyright (c) 2015-2023 Open Salamander Authors

	cfg.cpp
	Plugin configuration.
*/

#include "precomp.h"
#include "cfg.h"
#include "peviewer.rh2"
#include "pefile.h"

const CFG_DUMPER g_cfgDumpers[AVAILABLE_PE_DUMPERS] =
    {
        {L"FileType",
         IDS_DUMPER_FILETYPE,
         &CFileTypeDumper::Create},
        {L"Version",
         IDS_DUMPER_FILEVERSIONRESOURCE,
         &CFileVersionResourceDumper::Create},
        {L"FileHeader",
         IDS_DUMPER_FILEHEADER,
         &CFileHeaderDumper::Create},
        {L"OptionalFileHeader",
         IDS_DUMPER_OPTIONALFILEHEADER,
         &COptionalFileHeaderDumper::Create},
        {L"ExportTable",
         IDS_DUMPER_EXPORTTABLE,
         &CExportTableDumper::Create},
        {L"ImportTable",
         IDS_DUMPER_IMPORTTABLE,
         &CImportTableDumper::Create},
        {L"SectionTable",
         IDS_DUMPER_SECTIONTABLE,
         &CSectionTableDumper::Create},
        {L"LoadConfig",
         IDS_DUMPER_LOADCONFIG,
         &CLoadConfigDumper::Create},
        {L"DebugDirectory",
         IDS_DUMPER_DEBUGDIRECTORY,
         &CDebugDirectoryDumper::Create},
        {L"Manifest",
         IDS_DUMPER_MANIFEST,
         &CManifestResourceDumper::Create},
        {L"CorHeader",
         IDS_DUMPER_CORHEADER,
         &CCorHeaderDumper::Create},
#if WITH_COR_METADATA_DUMPER
        {L"CorMetadata",
         IDS_DUMPER_CORMETADATA,
         &CCorMetadataDumper::Create},
#endif
        {L"ResourceDirectory",
         IDS_DUMPER_RESOURCEDIRECTORY,
         &CResourceDirectoryDumper::Create},
};

CFG_CHAIN_ENTRY g_cfgChain[AVAILABLE_PE_DUMPERS];
CFG_CHAIN_ENTRY g_cfgChainDefault[AVAILABLE_PE_DUMPERS];
unsigned g_cfgChainLength;

PCWSTR GetDumperTitleStr(int nTitleId)
{
    switch (nTitleId)
    {
    case IDS_DUMPER_FILETYPE:
        return L"File type";
    case IDS_DUMPER_FILEVERSIONRESOURCE:
        return L"Version";
    case IDS_DUMPER_FILEHEADER:
        return L"File header";
    case IDS_DUMPER_OPTIONALFILEHEADER:
        return L"Optional file header";
    case IDS_DUMPER_EXPORTTABLE:
        return L"Export table";
    case IDS_DUMPER_IMPORTTABLE:
        return L"Import table";
    case IDS_DUMPER_SECTIONTABLE:
        return L"Section table";
    case IDS_DUMPER_DEBUGDIRECTORY:
        return L"Debug directory";
    case IDS_DUMPER_RESOURCEDIRECTORY:
        return L"Resource directory";
    case IDS_DUMPER_LOADCONFIG:
        return L"Load configuration";
    case IDS_DUMPER_MANIFEST:
        return L"Manifest";
    case IDS_DUMPER_CORHEADER:
        return L"CLR header";
    case IDS_DUMPER_CORMETADATA:
        return L"CLR metadata";
    }
    TRACE_C("GetDumperTitleStr(): unknown nTitleId (" << nTitleId << ")");
    return L"";
}

int FindDumperInChain(const CFG_DUMPER* pDumperCfg)
{
    int found = -1;

    for (unsigned i = 0; i < g_cfgChainLength; i++)
    {
        if (g_cfgChain[i].pDumperCfg == pDumperCfg)
        {
            found = (int)i;
            break;
        }
    }

    return found;
}

void BuildDefaultDumperChain()
{
    g_cfgChainLength = AVAILABLE_PE_DUMPERS;
    for (unsigned i = 0; i < g_cfgChainLength; i++)
    {
        g_cfgChain[i].pDumperCfg = &g_cfgDumpers[i];
    }
    memcpy(g_cfgChainDefault, g_cfgChain, sizeof(g_cfgChainDefault));
}
