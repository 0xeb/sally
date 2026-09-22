// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "common/text/CaseFolding.h"
#include "cfgdlg.h"
#include "plugins.h"
#include "zip.h"
#include "pack.h"

// custom packers / unpackers
const wchar_t* SALAMANDER_CPU_TITLE = L"Title";
const wchar_t* SALAMANDER_CPU_EXT = L"Ext";
const wchar_t* SALAMANDER_CPU_TYPE = L"Type";
const wchar_t* SALAMANDER_CPU_SUPLONG = L"Support Long Names";
const wchar_t* SALAMANDER_CPU_ANSILIST = L"Need ANSI List";
// custom packers only
const wchar_t* SALAMANDER_CP_EXECCOPY = L"Copy Command";
const wchar_t* SALAMANDER_CP_ARGSCOPY = L"Copy Arguments";
const wchar_t* SALAMANDER_CP_SUPMOVE = L"Support Move";
const wchar_t* SALAMANDER_CP_EXECMOVE = L"Move Command";
const wchar_t* SALAMANDER_CP_ARGSMOVE = L"Move Arguments";
// custom unpackers only
const wchar_t* SALAMANDER_CU_EXECEXTRACT = L"Extract Command";
const wchar_t* SALAMANDER_CU_ARGSEXTRACT = L"Extract Arguments";

// The pre-2.5b44 upgrade step that lowercases a custom packer's/unpacker's extension.
//
// sally::text::Fold is LCMAP_UPPERCASE - it builds a comparison KEY, not a display form - so
// using it here inverted the upgrade. The extension it produces is user-visible and is written
// back to the configuration on save: the Pack dialog started proposing "Documents.ZIP" where the
// entire purpose of this step is to turn a legacy "ZIP" into "zip".
static void LowerCaseExtensionForUpgrade(std::wstring& ext)
{
    if (!ext.empty())
        CharLowerBuffW(ext.data(), (DWORD)ext.size());
}

// conversion table for translating exe to a variable
struct SPackConvTable
{
    const wchar_t* exe;
    const wchar_t* variable;
};

SPackConvTable PackConversionTable[] = {
    {L"jar32", L"$(Jar32bitExecutable)"},
    {L"jar16", L"$(Jar16bitExecutable)"},
    {L"rar", L"$(Rar32bitExecutable)"},
    {L"arj32", L"$(Arj32bitExecutable)"},
    {L"arj", L"$(Arj16bitExecutable)"},
    {L"ace32", L"$(Ace32bitExecutable)"},
    {L"ace", L"$(Ace16bitExecutable)"},
    {L"lha", L"$(Lha16bitExecutable)"},
    {L"uc", L"$(UC216bitExecutable)"},
    {L"pkzip25", L"$(Zip32bitExecutable)"},
    {L"pkzip", L"$(Zip16bitExecutable)"},
    {L"pkunzip", L"$(Unzip16bitExecutable)"},
    {NULL, NULL}};

// order in which custom packers/unpackers were historically added
int CustomOrder[] = {0, 1, 9, 10, 2, 3, 4, 11, 5, 6, 7, 8};

// custom packer table
SPackCustomPacker CustomPackers[] = {
    // JAR32
    {{L"a \"$(ArchiveFullName)\" !\"$(ListFullName)\"", L"a -v1440 \"$(ArchiveFullName)\" !\"$(ListFullName)\""},
     {L"m \"$(ArchiveFullName)\" !\"$(ListFullName)\"", L"m -v1440 \"$(ArchiveFullName)\" !\"$(ListFullName)\""},
     {IDS_DP_JAR_E, IDS_DP_JARV_E},
     L"j",
     TRUE,
     FALSE,
     L"jar32"},
    // RAR32
    {{L"a -scol \"$(ArchiveFullName)\" @\"$(ListFullName)\"", L"a -scol -v1440 \"$(ArchiveFullName)\" @\"$(ListFullName)\""}, // since version 5.0 we must enforce the -scol switch, version 4.20 is fine; appears elsewhere and in the registry
     {L"m -scol \"$(ArchiveFullName)\" @\"$(ListFullName)\"", L"m -scol -v1440 \"$(ArchiveFullName)\" @\"$(ListFullName)\""},
     {IDS_DP_RAR_E, IDS_DP_RARV_E},
     L"rar",
     TRUE,
     FALSE,
     L"rar"},
    // ARJ16
    {{L"a -pa $(ArchiveDOSFullName) !$(ListDOSFullName)", L"a -pav1440 $(ArchiveDOSFullName) !$(ListDOSFullName)"},
     {L"m -pa $(ArchiveDOSFullName) !$(ListDOSFullName)", L"m -pav1440 $(ArchiveDOSFullName) !$(ListDOSFullName)"},
     {IDS_DP_ARJ16_E, IDS_DP_ARJ16V_E},
     L"arj",
     FALSE,
     FALSE,
     L"arj"},
    // LZH
    {{L"a -m -p -a -l1 -x1 -c $(ArchiveDOSFullName) @$(ListDOSFullName)", NULL},
     {L"m -m -p -a -l1 -x1 -c $(ArchiveDOSFullName) @$(ListDOSFullName)", NULL},
     {IDS_DP_LHA_E, -1},
     L"lzh",
     FALSE,
     FALSE,
     L"lha"},
    // UC2
    {{L"A !SYSHID=ON ##\\ $(ArchiveDOSFullName) @$(ListDOSFullName)", NULL},
     {L"AM !SYSHID=ON ##\\ $(ArchiveDOSFullName) @$(ListDOSFullName)", NULL},
     {IDS_DP_UC2_E, -1},
     L"uc2",
     FALSE,
     FALSE,
     L"uc"},
    // JAR16
    {{L"a $(ArchiveDOSFullName) !$(ListDOSFullName)", L"a -v1440 $(ArchiveDOSFullName) !$(ListDOSFullName)"},
     {L"m $(ArchiveDOSFullName) !$(ListDOSFullName)", L"m -v1440 $(ArchiveDOSFullName) !$(ListDOSFullName)"},
     {IDS_DP_JAR16_E, IDS_DP_JAR16V_E},
     L"j",
     FALSE,
     FALSE,
     L"jar16"},
    // RAR16
    {{L"a $(ArchiveDOSFullName) @$(ListDOSFullName)", L"a -v1440 $(ArchiveDOSFullName) @$(ListDOSFullName)"},
     {L"m $(ArchiveDOSFullName) @$(ListDOSFullName)", L"m -v1440 $(ArchiveDOSFullName) @$(ListDOSFullName)"},
     {IDS_DP_RAR16_E, IDS_DP_RAR16V_E},
     L"rar",
     FALSE,
     FALSE,
     L"rar"},
    // ZIP32
    {{L"-add -nozipextension -path -attr \"$(ArchiveFullName)\" @\"$(ListFullName)\"", NULL},
     {L"-add -nozipextension -move -path -attr \"$(ArchiveFullName)\" @\"$(ListFullName)\"", NULL},
     {IDS_DP_ZIP32_E, -1},
     L"zip",
     TRUE,
     TRUE,
     L"pkzip25"},
    // ZIP16
    {{L"-P -whs $(ArchiveDOSFullName) @$(ListDOSFullName)", NULL},
     {L"-m -P -whs $(ArchiveDOSFullName) @$(ListDOSFullName)", NULL},
     {IDS_DP_ZIP16_E, -1},
     L"zip",
     FALSE,
     FALSE,
     L"pkzip"},
    // ARJ32
    {{L"a -pa \"$(ArchiveFullName)\" !\"$(ListFullName)\"", L"a -pav1440 \"$(ArchiveFullName)\" !\"$(ListFullName)\""},
     {L"m -pa \"$(ArchiveFullName)\" !\"$(ListFullName)\"", L"m -pav1440 \"$(ArchiveFullName)\" !\"$(ListFullName)\""},
     {IDS_DP_ARJ32_E, IDS_DP_ARJ32V_E},
     L"arj",
     TRUE,
     FALSE,
     L"arj32"},
    // ACE32
    {{L"a \"$(ArchiveFullName)\" @\"$(ListFullName)\"", L"a -v1440 \"$(ArchiveFullName)\" @\"$(ListFullName)\""},
     {L"m \"$(ArchiveFullName)\" @\"$(ListFullName)\"", L"m -v1440 \"$(ArchiveFullName)\" @\"$(ListFullName)\""},
     {IDS_DP_ACE_E, IDS_DP_ACEV_E},
     L"ace",
     TRUE,
     TRUE,
     L"ace32"},
    // ACE16
    {{L"a $(ArchiveDOSFullName) @$(ListDOSFullName)", L"a -v1440 $(ArchiveDOSFullName) @$(ListDOSFullName)"},
     {L"m $(ArchiveDOSFullName) @$(ListDOSFullName)", L"m -v1440 $(ArchiveDOSFullName) @$(ListDOSFullName)"},
     {IDS_DP_ACE16_E, IDS_DP_ACE16V_E},
     L"ace",
     FALSE,
     FALSE,
     L"ace"},
};

// custom unpacker table
SPackCustomUnpacker CustomUnpackers[] = {
    // JAR32
    {L"x -jyc \"$(ArchiveFullName)\" !\"$(ListFullName)\"", IDS_DU_JAR_E, L"*.j", TRUE, FALSE, L"jar32"},
    // RAR32
    {L"x -scol \"$(ArchiveFullName)\" @\"$(ListFullName)\"", IDS_DU_RAR_E, L"*.rar", TRUE, FALSE, L"rar"}, // since version 5.0 we must enforce the -scol switch, version 4.20 is fine; appears elsewhere and in the registry
    // ARJ16
    {L"x -va -jyc $(ArchiveDOSFullName) !$(ListDOSFullName)", IDS_DU_ARJ16_E, L"*.arj", FALSE, FALSE, L"arj"},
    // LZH
    {L"x -a -l1 -c $(ArchiveDOSFullName) @$(ListDOSFullName)", IDS_DU_LHA_E, L"*.lzh", FALSE, FALSE, L"lha"},
    // UC2
    {L"ESF $(ArchiveDOSFullName) @$(ListDOSFullName)", IDS_DU_UC2_E, L"*.uc2", FALSE, FALSE, L"uc"},
    // JAR16
    {L"x -jyc $(ArchiveDOSFullName) !$(ListDOSFullName)", IDS_DU_JAR16_E, L"*.j", FALSE, FALSE, L"jar16"},
    // RAR16
    {L"x $(ArchiveDOSFullName) @$(ListDOSFullName)", IDS_DU_RAR16_E, L"*.rar", FALSE, FALSE, L"rar"},
    // ZIP32
    {L"-ext -nozipextension -directories -path \"$(ArchiveFullName)\" @\"$(ListFullName)\"", IDS_DU_ZIP32_E, L"*.zip;*.pk3;*.jar", TRUE, TRUE, L"pkzip25"},
    // ZIP16
    {L"-d -Jhrs $(ArchiveDOSFullName) @$(ListDOSFullName)", IDS_DU_ZIP16_E, L"*.zip", FALSE, FALSE, L"pkunzip"},
    // ARJ32
    {L"x -va -jyc \"$(ArchiveFullName)\" !\"$(ListFullName)\"", IDS_DU_ARJ32_E, L"*.arj", TRUE, FALSE, L"arj32"},
    // ACE32
    {L"x \"$(ArchiveFullName)\" @\"$(ListFullName)\"", IDS_DU_ACE_E, L"*.ace", TRUE, TRUE, L"ace32"},
    // ACE16
    {L"x $(ArchiveDOSFullName) @$(ListDOSFullName)", IDS_DU_ACE16_E, L"*.ace", FALSE, FALSE, L"ace"},
};

//
// ****************************************************************************
// CPackerConfig
//

CPackerConfig::CPackerConfig(/*BOOL disableDefaultValues*/)
    : Packers(30, 10)
{
    PreferedPacker = -1;
    Move = FALSE;
    /*
  if (!disableDefaultValues)
    AddDefault(0);
*/
}

void CPackerConfig::InitializeDefaultValues()
{
    AddDefault(0);
}

void CPackerConfig::AddDefault(int SalamVersion)
{
    // WARNING: up to version 6 the old 'Type' values were: 0 ZIP, 1 external, 2 TAR, 3 PAK

    // default values
    int index, i;
    // internal ones first to keep it sorted
    switch (SalamVersion)
    {
    case 0: // default config
    case 1: // v1.52 had no packers
        if ((index = AddPacker()) == -1)
            return;
        SetPacker(index, 0, L"ZIP (Plugin)", L"zip", TRUE);
    case 2: // added after beta1
        if ((index = AddPacker()) == -1)
            return;
        SetPacker(index, 3, L"PAK (Plugin)", L"pak", TRUE);
    case 3:  // added after beta2
    case 4:  // beta 3 but with old configuration (contains $(SpawnName))
    case 5:; // what is new in beta4?
             //      if ((index = AddPacker()) == -1) return;
             //      SetPacker(index, 2, "TAR (Plugin)", "tgz", TRUE);
    }
    // now external
    switch (SalamVersion)
    {
        // parameters format
        //BOOL SetPacker(int index, int type, const wchar_t *title, const wchar_t *ext, BOOL old,
        //               BOOL supportLongNames = FALSE, BOOL supportMove = FALSE,
        //               const wchar_t *cmdExecCopy = NULL, const wchar_t *cmdArgsCopy = NULL,
        //               const wchar_t *cmdExecMove = NULL, const wchar_t *cmdArgsMove = NULL,
        //               BOOL needANSIListFile = FALSE);

    case 0: // default config
    case 1: // v1.52 had no packers
        for (i = 0; i < 7; i++)
        {
            int idx = CustomOrder[i];
            if ((index = AddPacker()) == -1)
                return;
            SetPacker(index, 1, LoadStrW(CustomPackers[idx].Title[0]), CustomPackers[idx].Ext, TRUE,
                      CustomPackers[idx].SupLN, TRUE,
                      CustomPackers[idx].Exe, CustomPackers[idx].CopyArgs[0],
                      CustomPackers[idx].Exe, CustomPackers[idx].MoveArgs[0],
                      CustomPackers[idx].Ansi);
            if (CustomPackers[idx].CopyArgs[1] != NULL)
            {
                if ((index = AddPacker()) == -1)
                    return;
                SetPacker(index, 1, LoadStrW(CustomPackers[idx].Title[1]), CustomPackers[idx].Ext, TRUE,
                          CustomPackers[idx].SupLN, TRUE,
                          CustomPackers[idx].Exe, CustomPackers[idx].CopyArgs[1],
                          CustomPackers[idx].Exe, CustomPackers[idx].MoveArgs[1],
                          CustomPackers[idx].Ansi);
            }
        }
    case 2: // added after beta1
        for (i = 7; i < 12; i++)
        {
            int idx = CustomOrder[i];
            if ((index = AddPacker()) == -1)
                return;
            SetPacker(index, 1, LoadStrW(CustomPackers[idx].Title[0]), CustomPackers[idx].Ext, TRUE,
                      CustomPackers[idx].SupLN, TRUE,
                      CustomPackers[idx].Exe, CustomPackers[idx].CopyArgs[0],
                      CustomPackers[idx].Exe, CustomPackers[idx].MoveArgs[0],
                      CustomPackers[idx].Ansi);
            if (CustomPackers[idx].CopyArgs[1] != NULL)
            {
                if ((index = AddPacker()) == -1)
                    return;
                SetPacker(index, 1, LoadStrW(CustomPackers[idx].Title[1]), CustomPackers[idx].Ext, TRUE,
                          CustomPackers[idx].SupLN, TRUE,
                          CustomPackers[idx].Exe, CustomPackers[idx].CopyArgs[1],
                          CustomPackers[idx].Exe, CustomPackers[idx].MoveArgs[1],
                          CustomPackers[idx].Ansi);
            }
        }
    case 3: // added after beta2
    case 4: // beta 3 but with old configuration (contains $(SpawnName))
        // in older versions the $(SpawnName) variable might exist, it no longer does - we must remove it
        for (index = 0; index < GetPackersCount(); index++)
            if (GetPackerType(index) == 1)
            {
                const wchar_t* cmdC = GetPackerCmdExecCopy(index);
                const wchar_t* cmdM = GetPackerCmdExecMove(index);
                if (wcsncmp(cmdC, L"$(SpawnName) ", 13) == 0 ||
                    GetPackerSupMove(index) && wcsncmp(cmdM, L"$(SpawnName) ", 13) == 0)
                {
                    std::wstring copyCmdBuf = (wcsncmp(cmdC, L"$(SpawnName) ", 13) == 0) ? (cmdC + 13) : cmdC;
                    std::wstring copyArgBuf = GetPackerCmdArgsCopy(index);

                    std::wstring moveCmdBuf, moveArgBuf;
                    if (GetPackerSupMove(index))
                    {
                        moveCmdBuf = (wcsncmp(cmdM, L"$(SpawnName) ", 13) == 0) ? (cmdM + 13) : cmdM;
                        moveArgBuf = GetPackerCmdArgsMove(index);
                    }

                    std::wstring TitleBuf = GetPackerTitle(index);
                    std::wstring ExtBuf = GetPackerExt(index);

                    SetPacker(index, GetPackerType(index), TitleBuf.c_str(), ExtBuf.c_str(), TRUE,
                              GetPackerSupLongNames(index), GetPackerSupMove(index),
                              copyCmdBuf.c_str(), copyArgBuf.c_str(),
                              GetPackerSupMove(index) ? moveCmdBuf.c_str() : NULL,
                              GetPackerSupMove(index) ? moveArgBuf.c_str() : NULL,
                              GetPackerNeedANSIListFile(index));
                }
            }
    case 5: // beta 3 but without tar
    case 6: // what is new in beta4?
    case 7: // 1.6b6 - for correct conversion of supported plugin functions (see CPlugins::Load)
    case 8:
        // transition to using variables instead of the direct exe file name
        for (index = 0; index < GetPackersCount(); index++)
            // consider only external packers
            if (((GetPackerOldType(index) && GetPackerType(index) == 1) ||
                 (!GetPackerOldType(index) && GetPackerType(index) == CUSTOMPACKER_EXTERNAL)) &&
                GetPackerCmdExecCopy(index) != NULL && GetPackerCmdExecMove(index) != NULL)
            {
                // take the old commands
                std::wstring cmdC = GetPackerCmdExecCopy(index);
                std::wstring cmdM = GetPackerCmdExecMove(index);
                i = 0;
                BOOL found = FALSE;
                // and search the table with them
                while (PackConversionTable[i].exe != NULL)
                {
                    // compare with table entries
                    if (cmdC == PackConversionTable[i].exe || cmdM == PackConversionTable[i].exe)
                    {
                        // if we find it, replace it with the variable
                        if (cmdC == PackConversionTable[i].exe)
                        {
                            // an ugly hack because of RAR
                            if (i == 2)
                                // if it's RAR we cannot tell whether it is 16-bit or 32-bit directly, only from long-name support
                                if (GetPackerSupLongNames(index))
                                    cmdC = PackConversionTable[i].variable;
                                else
                                    cmdC = L"$(Rar16bitExecutable)";
                            else
                                // for others it's simple
                                cmdC = PackConversionTable[i].variable;
                        }
                        if (cmdM == PackConversionTable[i].exe)
                        {
                            // an ugly hack because of RAR
                            if (i == 2)
                                // if it's RAR we cannot tell whether it is 16-bit or 32-bit directly, only from long-name support
                                if (GetPackerSupLongNames(index))
                                    cmdM = PackConversionTable[i].variable;
                                else
                                    cmdM = L"$(Rar16bitExecutable)";
                            else
                                // for others it's simple
                                cmdM = PackConversionTable[i].variable;
                        }
                        found = TRUE;
                    }
                    i++;
                }
                // strings must be copied somewhere or we delete them before use
                std::wstring title = GetPackerTitle(index);
                std::wstring ext = GetPackerExt(index);
                std::wstring argsC = GetPackerCmdArgsCopy(index) ? GetPackerCmdArgsCopy(index) : L"";
                std::wstring argsM = GetPackerCmdArgsMove(index) ? GetPackerCmdArgsMove(index) : L"";

                if (found)
                    SetPacker(index, GetPackerType(index), title.c_str(), ext.c_str(), GetPackerOldType(index),
                              GetPackerSupLongNames(index), GetPackerSupMove(index),
                              cmdC.c_str(), argsC.c_str(), cmdM.c_str(), argsM.c_str(), GetPackerNeedANSIListFile(index));
            }
    case 9: // 1.6b6 - due to switching from exe name to variable in custom packers
        // enable ANSI file list for ACE32 and PKZIP25
        for (index = 0; index < GetPackersCount(); index++)
        {
            if ((GetPackerOldType(index) && GetPackerType(index) == 1) ||
                (!GetPackerOldType(index) && GetPackerType(index) == CUSTOMPACKER_EXTERNAL))
            {
                const wchar_t* s = GetPackerCmdExecCopy(index);
                if (s != NULL && (wcscmp(s, L"$(Zip32bitExecutable)") == 0 ||
                                  wcscmp(s, L"$(Ace32bitExecutable)") == 0))
                {
                    Packers[index]->NeedANSIListFile = TRUE;
                }
            }
        }
        // change "XXX (Internal)" to "XXX (Plugin)"
        // we aggressively overwrite it, because we're changing to shorter text -> simply overwriting is enough
        for (index = 0; index < GetPackersCount(); index++)
        {
            if ((GetPackerOldType(index) && GetPackerType(index) != 1) ||
                (!GetPackerOldType(index) && GetPackerType(index) != CUSTOMPACKER_EXTERNAL))
            { // take only plug-ins (not external packers)
                std::wstring& s = Packers[index]->Title;
                size_t pos = s.find(L"(Internal)");
                if (pos != std::wstring::npos && pos + 10 == s.length())
                {
                    s.replace(pos, 10, L"(Plugin)");
                }
            }
        }
    case 10: // 1.6b6 - due to renaming "XXX (Internal)" to "XXX (Plugin)" in the Pack and Unpack dialogs
             //         and due to setting the ANSI version of "list of files" for (un)packers ACE32 and PKZIP25
    case 11: // 1.6b7 - added CheckVer plugin - ensure its automatic installation
    case 12: // 2.0 - auto-disable salopen.exe + added PEViewer plugin - ensure its automatic installation
    {
        // LHA gained "-m", we must add it and if archivers-auto-config added LHA a second time
        // because "-m" did not match, remove that new entry
        const wchar_t* newLHACopyArgs = L"a -m -p -a -l1 -x1 -c $(ArchiveDOSFullName) @$(ListDOSFullName)";
        const wchar_t* newLHAMoveArgs = L"m -m -p -a -l1 -x1 -c $(ArchiveDOSFullName) @$(ListDOSFullName)";
        BOOL canDelLHA = FALSE;
        for (index = 0; index < GetPackersCount(); index++)
        {
            if ((GetPackerOldType(index) && GetPackerType(index) == 1) ||
                (!GetPackerOldType(index) && GetPackerType(index) == CUSTOMPACKER_EXTERNAL))
            {
                const wchar_t* copyEXE = GetPackerCmdExecCopy(index);
                const wchar_t* copyArgs = GetPackerCmdArgsCopy(index);
                const wchar_t* moveEXE = GetPackerCmdExecMove(index);
                const wchar_t* moveArgs = GetPackerCmdArgsMove(index);

                // check whether this is an LHA custom packer
                if (copyEXE != NULL && wcscmp(copyEXE, L"$(Lha16bitExecutable)") == 0 &&
                    moveEXE != NULL && wcscmp(moveEXE, L"$(Lha16bitExecutable)") == 0)
                {
                    // test whether this is an old LHA custom packer entry
                    if (copyArgs != NULL &&
                        wcscmp(copyArgs, L"a -p -a -l1 -x1 -c $(ArchiveDOSFullName) @$(ListDOSFullName)") == 0 &&
                        moveArgs != NULL &&
                        wcscmp(moveArgs, L"m -p -a -l1 -x1 -c $(ArchiveDOSFullName) @$(ListDOSFullName)") == 0)
                    {
                        if (canDelLHA)
                        {
                            // delete the entry, it is unnecessary (a working LHA entry already exists)
                            DeletePacker(index);
                            index--;
                        }
                        else
                        {
                            canDelLHA = TRUE;
                            // convert to new arguments (added "-m")
                            Packers[index]->CmdArgsCopy = newLHACopyArgs;
                            Packers[index]->CmdArgsMove = newLHAMoveArgs;
                        }
                    }
                    else
                    {
                        // test whether this is an entry added by archivers-auto-config for the LHA custom packer
                        if (copyArgs != NULL && wcscmp(copyArgs, newLHACopyArgs) == 0 &&
                            moveArgs != NULL && wcscmp(moveArgs, newLHAMoveArgs) == 0)
                        {
                            if (canDelLHA)
                            {
                                // delete the added entry, it is unnecessary (a working LHA entry already exists)
                                DeletePacker(index);
                                index--;
                            }
                            else
                                canDelLHA = TRUE;
                        }
                    }
                }
            }
        }
    }
    case 13: // 2.5b1 - added missing configuration conversion for custom packer - reflecting LHA change
    case 14: // 2.5b1 - New Advanced Options in the Find dialog. Switched to CFilterCriteria. Conversion of inverse mask in filters.
    case 15: // 2.5b2 - newer version to ensure plugins are loaded (upgrade registry entries)
    case 16: // 2.5b2 - added coloring of encrypted files and folders (added when loading config and in default config)
    case 17: // 2.5b2 - added *.xml mask to internal viewer settings - "force text mode"
    case 18: // 2.5b3 - for now, only for transferring plugin configuration from version 2.5b2
    case 19: // 2.5b4 - for now, only for transferring plugin configuration from version 2.5b3
    case 20: // 2.5b5 - for now, only for transferring plugin configuration from version 2.5b4
    case 21: // 2.5b6 - for now, only for transferring plugin configuration from version 2.5b5(a)
    case 22: // 2.5b6 - filters in panels -- unified into a single history
    case 23: // 2.5b6 - new panel view (Tiles)
    case 24: // 2.5b7 - for now, only for transferring plugin configuration from version 2.5b6
    case 25: // 2.5b7 - plugins: show in plugin bar -> variable moved into CPluginData
    case 26: // 2.5b8 - for now, only for transferring plugin configuration from version 2.5b7
    case 27: // 2.5b9 - for now, only for transferring plugin configuration from version 2.5b8
    case 28: // 2.5b9 - new color scheme based on the old DOS Navigator -> convert 'scheme'
    case 29: // 2.5b10 - for now, only for transferring plugin configuration from version 2.5b9
    case 30: // 2.5b11 - for now, only for transferring plugin configuration from version 2.5b10
    case 31: // 2.5b11 - introduced a Floppy section in the Drives configuration and need to force icon reading for Removable
    case 32: // 2.5b11 - Find: "Local Settings\Temporary Internet Files" is searched implicitly
    case 33: // 2.5b12 - for now, only for transferring plugin configuration from version 2.5b11
    {
        // PKZIP25 gained "-nozipextension", we must add it
        const wchar_t* newPKZIP25CopyArgs = L"-add -nozipextension -path -attr \"$(ArchiveFullName)\" @\"$(ListFullName)\"";
        const wchar_t* newPKZIP25MoveArgs = L"-add -nozipextension -move -path -attr \"$(ArchiveFullName)\" @\"$(ListFullName)\"";
        for (index = 0; index < GetPackersCount(); index++)
        {
            if ((GetPackerOldType(index) && GetPackerType(index) == 1) ||
                (!GetPackerOldType(index) && GetPackerType(index) == CUSTOMPACKER_EXTERNAL))
            {
                const wchar_t* copyEXE = GetPackerCmdExecCopy(index);
                const wchar_t* copyArgs = GetPackerCmdArgsCopy(index);
                const wchar_t* moveEXE = GetPackerCmdExecMove(index);
                const wchar_t* moveArgs = GetPackerCmdArgsMove(index);

                // check whether this is a PKZIP25 custom packer
                if (copyEXE != NULL && wcscmp(copyEXE, L"$(Zip32bitExecutable)") == 0 &&
                    moveEXE != NULL && wcscmp(moveEXE, L"$(Zip32bitExecutable)") == 0)
                {
                    // test whether this is an old PKZIP25 custom packer entry
                    if (copyArgs != NULL &&
                        wcscmp(copyArgs, L"-add -path -attr \"$(ArchiveFullName)\" @\"$(ListFullName)\"") == 0 &&
                        moveArgs != NULL &&
                        wcscmp(moveArgs, L"-add -move -path -attr \"$(ArchiveFullName)\" @\"$(ListFullName)\"") == 0)
                    {
                        // convert to new arguments (added "-nozipextension")
                        Packers[index]->CmdArgsCopy = newPKZIP25CopyArgs;
                        Packers[index]->CmdArgsMove = newPKZIP25MoveArgs;
                    }
                }
            }
        }
    }
        // case 34:   // 2.5b12 - adjustment of the external PKZIP25 packer/unpacker (external Win32 version)

    default:
        break;
    }
    if (SalamVersion > 1 && SalamVersion < 81)
    {
        // since RAR 5.0 filelists are ANSI by default instead of OEM, so we must force OEM with a switch
        const wchar_t* newRAR5CopyArgs = L"a -scol \"$(ArchiveFullName)\" @\"$(ListFullName)\"";
        const wchar_t* newRAR5MoveArgs = L"m -scol \"$(ArchiveFullName)\" @\"$(ListFullName)\"";
        const wchar_t* newRAR5CopyVolArgs = L"a -scol -v1440 \"$(ArchiveFullName)\" @\"$(ListFullName)\"";
        const wchar_t* newRAR5MoveVolArgs = L"m -scol -v1440 \"$(ArchiveFullName)\" @\"$(ListFullName)\"";
        for (index = 0; index < GetPackersCount(); index++)
        {
            if ((GetPackerOldType(index) && GetPackerType(index) == 1) ||
                (!GetPackerOldType(index) && GetPackerType(index) == CUSTOMPACKER_EXTERNAL))
            {
                const wchar_t* copyEXE = GetPackerCmdExecCopy(index);
                const wchar_t* copyArgs = GetPackerCmdArgsCopy(index);
                const wchar_t* moveEXE = GetPackerCmdExecMove(index);
                const wchar_t* moveArgs = GetPackerCmdArgsMove(index);

                // check whether this is a RAR Win32 custom packer
                if (copyEXE != NULL && wcscmp(copyEXE, L"$(Rar32bitExecutable)") == 0 &&
                    moveEXE != NULL && wcscmp(moveEXE, L"$(Rar32bitExecutable)") == 0)
                {
                    // test whether this is an old RAR Win32 custom packer entry
                    if (copyArgs != NULL &&
                        wcscmp(copyArgs, L"a \"$(ArchiveFullName)\" @\"$(ListFullName)\"") == 0 &&
                        moveArgs != NULL &&
                        wcscmp(moveArgs, L"m \"$(ArchiveFullName)\" @\"$(ListFullName)\"") == 0)
                    {
                        // convert to new arguments (added "-scol")
                        Packers[index]->CmdArgsCopy = newRAR5CopyArgs;
                        Packers[index]->CmdArgsMove = newRAR5MoveArgs;
                    }
                    if (copyArgs != NULL &&
                        wcscmp(copyArgs, L"a -v1440 \"$(ArchiveFullName)\" @\"$(ListFullName)\"") == 0 &&
                        moveArgs != NULL &&
                        wcscmp(moveArgs, L"m -v1440 \"$(ArchiveFullName)\" @\"$(ListFullName)\"") == 0)
                    {
                        // convert to new arguments (added "-scol")
                        Packers[index]->CmdArgsCopy = newRAR5CopyVolArgs;
                        Packers[index]->CmdArgsMove = newRAR5MoveVolArgs;
                    }
                }
            }
        }
    }
}

BOOL CPackerConfig::Load(CPackerConfig& src)
{
    CALL_STACK_MESSAGE1("CPackerConfig::Load()");
    Move = src.Move;
    PreferedPacker = src.PreferedPacker;

    DeleteAllPackers();
    int i;
    for (i = 0; i < src.GetPackersCount(); i++)
    {
        int index = AddPacker();
        if (index == -1)
            return FALSE;
        if (!SetPacker(index, src.GetPackerType(i), src.GetPackerTitle(i), src.GetPackerExt(i), FALSE,
                       src.GetPackerSupLongNames(i), src.GetPackerSupMove(i),
                       src.GetPackerCmdExecCopy(i), src.GetPackerCmdArgsCopy(i),
                       src.GetPackerCmdExecMove(i), src.GetPackerCmdArgsMove(i),
                       src.GetPackerNeedANSIListFile(i)))
            return FALSE;
    }
    return TRUE;
}

int CPackerConfig::AddPacker(BOOL toFirstIndex)
{
    CALL_STACK_MESSAGE2("CPackerConfig::AddPacker(%d)", toFirstIndex);
    CPackerConfigData* data = new CPackerConfigData;
    if (data == NULL)
        return -1;
    int index;
    if (toFirstIndex)
    {
        Packers.Insert(0, data);
        index = 0;
        if (PreferedPacker != -1)
            PreferedPacker++;
    }
    else
        index = Packers.Add(data);
    if (!Packers.IsGood())
    {
        delete data;
        Packers.ResetState();
        return -1;
    }
    return index;
}

/*
BOOL
CPackerConfig::SwapPackers(int index1, int index2)
{
  std::swap(Packers[index1], Packers[index2]);
  return TRUE;
}
*/

BOOL CPackerConfig::MovePacker(int srcIndex, int dstIndex)
{
    CPackerConfigData* tmp = Packers[srcIndex];
    if (srcIndex < dstIndex)
    {
        int i;
        for (i = srcIndex; i < dstIndex; i++)
            Packers[i] = Packers[i + 1];
    }
    else
    {
        int i;
        for (i = srcIndex; i > dstIndex; i--)
            Packers[i] = Packers[i - 1];
    }
    Packers[dstIndex] = tmp;
    return TRUE;
}

void CPackerConfig::DeletePacker(int index)
{
    if (PreferedPacker >= 0 && PreferedPacker < Packers.Count)
    {
        if (index < PreferedPacker)
            PreferedPacker--; // adjust index to represent the same item
        else
        {
            if (index == PreferedPacker)
                PreferedPacker = -1; // we lost the selected item
        }
    }
    Packers.Delete(index);
}

BOOL CPackerConfig::SetPacker(int index, int type, const wchar_t* title, const wchar_t* ext, BOOL old,
                              BOOL supportLongNames, BOOL supportMove,
                              const wchar_t* cmdExecCopy, const wchar_t* cmdArgsCopy,
                              const wchar_t* cmdExecMove, const wchar_t* cmdArgsMove,
                              BOOL needANSIListFile)
{
    CALL_STACK_MESSAGE13("CPackerConfig::SetPacker(%d, %d, %ls, %ls, %d, %d, %d, %ls, %ls, %ls, %ls, %d)",
                         index, type, title, ext, old, supportLongNames, supportMove,
                         cmdExecCopy, cmdArgsCopy, cmdExecMove, cmdArgsMove, needANSIListFile);
    CPackerConfigData* data = Packers[index];
    data->Destroy();
    data->Type = type;
    data->OldType = old;
    data->Title = title;
    data->Ext = ext;
    if (old && data->Type == 1 ||
        !old && data->Type == CUSTOMPACKER_EXTERNAL)
    {
        data->CmdExecCopy = cmdExecCopy ? cmdExecCopy : L"";
        data->CmdArgsCopy = cmdArgsCopy ? cmdArgsCopy : L"";
        data->SupportMove = supportMove;

        if (data->SupportMove)
        {
            data->CmdExecMove = cmdExecMove ? cmdExecMove : L"";
            data->CmdArgsMove = cmdArgsMove ? cmdArgsMove : L"";
        }
        data->SupportLongNames = supportLongNames;
        data->NeedANSIListFile = needANSIListFile;
    }

    if (data->IsValid())
    {
        if (PreferedPacker == -1)
            PreferedPacker = Packers.Count - 1;
        return TRUE;
    }
    else
    {
        Packers.Delete(index);
        return FALSE;
    }
}

BOOL CPackerConfig::SetPackerTitle(int index, const wchar_t* title)
{
    CPackerConfigData* data = Packers[index];
    data->Title = title;
    return TRUE;
}

BOOL CPackerConfig::ExecutePacker(CFilesWindow* panel, const wchar_t* zipFile, BOOL move,
                                  const wchar_t* sourcePath, SalEnumSelection2 next, void* param,
                                  SalEnumLastNameW lastNameW)
{
    CALL_STACK_MESSAGE4("CPackerConfig::ExecutePacker(, %ls, %d, %ls, , ,)",
                        zipFile, move, sourcePath);
    if (PreferedPacker >= 0 && PreferedPacker < Packers.Count)
    {
        CPackerConfigData* data = Packers[PreferedPacker];
        if (data->Type == CUSTOMPACKER_EXTERNAL)
        {
            std::wstring command;
            if (move)
            {
                if (!data->SupportMove)
                {
                    TRACE_E("Using \"Move to archive\" with packer, which does not support it !!!");
                    return FALSE;
                }
                command = data->CmdExecMove + L" " + data->CmdArgsMove;
            }
            else
            {
                command = data->CmdExecCopy + L" " + data->CmdArgsCopy;
            }
            BOOL ret = PackUniversalCompress(NULL, command.c_str(), NULL, sourcePath, FALSE,
                                             data->SupportLongNames, zipFile, sourcePath, NULL,
                                             next, param, data->NeedANSIListFile, lastNameW);
            return ret;
        }
        else
        {
            CPluginData* plugin = Plugins.Get(-data->Type - 1);
            if (plugin != NULL && plugin->SupportCustomPack)
            {
                return plugin->PackToArchive(panel, zipFile, L"", move, sourcePath, next, param);
            }
            else
                TRACE_E("Unexpected situation in CPackerConfig::ExecutePacker().");
        }
    }
    return FALSE;
}

BOOL CPackerConfig::Save(int index, HKEY hKey)
{
    DWORD d;
    BOOL ret = TRUE;
    int type = GetPackerType(index);
    d = type;
    if (ret)
        ret &= SetValueW(hKey, SALAMANDER_CPU_TYPE, REG_DWORD, &d, sizeof(d));
    if (ret)
        ret &= SetValueW(hKey, SALAMANDER_CPU_TITLE, REG_SZ, GetPackerTitle(index), -1);
    if (ret)
        ret &= SetValueW(hKey, SALAMANDER_CPU_EXT, REG_SZ, GetPackerExt(index), -1);
    if (ret && type == CUSTOMPACKER_EXTERNAL)
    {
        d = GetPackerSupLongNames(index);
        if (ret)
            ret &= SetValueW(hKey, SALAMANDER_CPU_SUPLONG, REG_DWORD, &d, sizeof(d));
        d = GetPackerNeedANSIListFile(index);
        if (ret)
            ret &= SetValueW(hKey, SALAMANDER_CPU_ANSILIST, REG_DWORD, &d, sizeof(d));
        if (ret)
            ret &= SetValueW(hKey, SALAMANDER_CP_EXECCOPY, REG_SZ, GetPackerCmdExecCopy(index), -1);
        if (ret)
            ret &= SetValueW(hKey, SALAMANDER_CP_ARGSCOPY, REG_SZ, GetPackerCmdArgsCopy(index), -1);
        d = GetPackerSupMove(index);
        if (ret)
            ret &= SetValueW(hKey, SALAMANDER_CP_SUPMOVE, REG_DWORD, &d, sizeof(d));
        if (ret && d == TRUE)
        {
            if (ret)
                ret &= SetValueW(hKey, SALAMANDER_CP_EXECMOVE, REG_SZ, GetPackerCmdExecMove(index), -1);
            if (ret)
                ret &= SetValueW(hKey, SALAMANDER_CP_ARGSMOVE, REG_SZ, GetPackerCmdArgsMove(index), -1);
        }
    }
    return ret;
}

BOOL CPackerConfig::Load(HKEY hKey)
{
    std::wstring title;
    std::wstring ext;
    DWORD type;
    DWORD suplong = FALSE;
    DWORD needANSI = FALSE;
    std::wstring execcopy;
    std::wstring argscopy;
    DWORD supmove = FALSE;
    std::wstring execmove;
    std::wstring argsmove;

    BOOL ret = TRUE;
    if (ret)
        ret &= GetValueW(hKey, SALAMANDER_CPU_TYPE, REG_DWORD, &type, sizeof(DWORD));
    if (ret)
        ret &= GetStringValueW(hKey, SALAMANDER_CPU_TITLE, title);
    if (ret)
        ret &= GetStringValueW(hKey, SALAMANDER_CPU_EXT, ext);
    if (ret && (Configuration.ConfigVersion < 6 && type == 1 ||
                Configuration.ConfigVersion >= 6 && type == CUSTOMPACKER_EXTERNAL))
    {
        if (ret)
            ret &= GetValueW(hKey, SALAMANDER_CPU_SUPLONG, REG_DWORD, &suplong, sizeof(DWORD));
        if (ret)
        {
            if (!GetValueW(hKey, SALAMANDER_CPU_ANSILIST, REG_DWORD, &needANSI, sizeof(DWORD)))
                needANSI = FALSE; // in older versions it wasn't present, assumed FALSE
        }

        if (ret)
            ret &= GetStringValueW(hKey, SALAMANDER_CP_EXECCOPY, execcopy);
        if (ret)
            ret &= GetStringValueW(hKey, SALAMANDER_CP_ARGSCOPY, argscopy);
        if (ret)
            ret &= GetValueW(hKey, SALAMANDER_CP_SUPMOVE, REG_DWORD, &supmove, sizeof(DWORD));
        if (ret && supmove == TRUE)
        {
            if (ret)
                ret &= GetStringValueW(hKey, SALAMANDER_CP_EXECMOVE, execmove);
            if (ret)
                ret &= GetStringValueW(hKey, SALAMANDER_CP_ARGSMOVE, argsmove);
        }
    }

    if (ret)
    {
        int index;
        if ((index = AddPacker()) == -1)
            return FALSE;
        if (Configuration.ConfigVersion < 44) // convert extension to lowercase
            LowerCaseExtensionForUpgrade(ext);
        ret &= SetPacker(index, (int)type, title.c_str(), ext.c_str(), Configuration.ConfigVersion < 6,
                         (BOOL)suplong, BOOL(supmove),
                         execcopy.c_str(), argscopy.c_str(),
                         execmove.c_str(), argsmove.c_str(), needANSI);
    }

    return ret;
}

//
// ****************************************************************************
// CUnpackerConfig
//

CUnpackerConfig::CUnpackerConfig(/*BOOL disableDefaultValues*/)
    : Unpackers(20, 10)
{
    PreferedUnpacker = -1;
    /*
  if (!disableDefaultValues)
    AddDefault(0);
*/
}

void CUnpackerConfig::InitializeDefaultValues()
{
    AddDefault(0);
}

void CUnpackerConfig::AddDefault(int SalamVersion)
{
    // WARNING: up to version 6 the old 'Type' values were: 0 ZIP, 1 external, 2 TAR, 3 PAK

    // convert loaded values from 1.6b1 - extensions were not masks ("EXT" -> "*.EXT")
    if (SalamVersion == 2)
    {
        int i;
        for (i = 0; i < Unpackers.Count; i++)
        {
            if (Unpackers[i]->Ext.empty())
                continue;
            std::wstring result;
            result += L"*.";
            for (const wchar_t* ptr = Unpackers[i]->Ext.c_str(); *ptr != L'\0'; ptr++)
            {
                result += *ptr;
                if (*ptr == L';')
                {
                    result += L"*.";
                }
            }
            Unpackers[i]->Ext = result;
        }
    }

    // default values
    int index, i;
    // internal ones first to keep it sorted
    switch (SalamVersion)
    {
    case 0: // default config
    case 1: // v1.52 had no packers
        if ((index = AddUnpacker()) == -1)
            return;
        SetUnpacker(index, 0, L"ZIP (Plugin)", L"*.zip", TRUE);
    case 2: // added after beta1
        // hack to add the pk3 extension to zip
        for (i = 0; i < Unpackers.Count; i++)
            if (!_wcsnicmp(Unpackers[i]->Ext.c_str(), L"*.zip", 5))
            {
                Unpackers[i]->Ext += L";*.pk3;*.jar";
                break;
            }
        // and new formats
        if ((index = AddUnpacker()) == -1)
            return;
        SetUnpacker(index, 3, L"PAK (Plugin)", L"*.pak", TRUE);
    case 3: // what was added after beta2
    case 4: // beta 3 but without the $(SpawnName) variable
    case 5: // what is new in beta4?
        if ((index = AddUnpacker()) == -1)
            return;
        SetUnpacker(index, 2, L"TAR (Plugin)", L"*.TAR;*.TGZ;*.TBZ;*.TAZ;"
                                               L"*.TAR.GZ;*.TAR.BZ;*.TAR.BZ2;*.TAR.Z;"
                                               L"*_TAR.GZ;*_TAR.BZ;*_TAR.BZ2;*_TAR.Z;"
                                               L"*_TAR_GZ;*_TAR_BZ;*_TAR_BZ2;*_TAR_Z;"
                                               L"*.TAR_GZ;*.TAR_BZ;*.TAR_BZ2;*.TAR_Z;"
                                               L"*.GZ;*.BZ;*.BZ2;*.Z;"
                                               L"*.RPM;*.CPIO",
                    TRUE);
    }
    // now external
    switch (SalamVersion)
    {
        // parameters
        //BOOL SetUnpacker(int index, int type, const wchar_t *title, const wchar_t *ext, BOOL old,
        //                 BOOL supportLongNames = FALSE,
        //                 const wchar_t *cmdExecExtract = NULL, const wchar_t *cmdArgsExtract = NULL,
        //                 BOOL needANSIListFile = FALSE);

    case 0: // default config
    case 1: // v1.52 had no packers
        for (i = 0; i < 7; i++)
        {
            int idx = CustomOrder[i];
            if ((index = AddUnpacker()) == -1)
                return;
            SetUnpacker(index, 1, LoadStrW(CustomUnpackers[idx].Title), CustomUnpackers[idx].Ext, TRUE,
                        CustomUnpackers[idx].SupLN, CustomUnpackers[idx].Exe,
                        CustomUnpackers[idx].Args, CustomUnpackers[idx].Ansi);
        }
    case 2: // what was added after beta1
        for (i = 7; i < 12; i++)
        {
            int idx = CustomOrder[i];
            if ((index = AddUnpacker()) == -1)
                return;
            SetUnpacker(index, 1, LoadStrW(CustomUnpackers[idx].Title), CustomUnpackers[idx].Ext, TRUE,
                        CustomUnpackers[idx].SupLN, CustomUnpackers[idx].Exe,
                        CustomUnpackers[idx].Args, CustomUnpackers[idx].Ansi);
        }
    case 3: // what was added after beta2
    case 4: // beta 3 but without the $(SpawnName) variable
        // in older versions the $(SpawnName) variable might exist, it no longer does - we must remove it
        for (index = 0; index < GetUnpackersCount(); index++)
            if (GetUnpackerType(index) == 1)
            {
                const wchar_t* cmd = GetUnpackerCmdExecExtract(index);
                if (wcsncmp(cmd, L"$(SpawnName) ", 13) == 0)
                {
                    std::wstring extractCmdBuf = cmd + 13;
                    std::wstring extractArgBuf = GetUnpackerCmdArgsExtract(index);
                    std::wstring TitleBuf = GetUnpackerTitle(index);
                    std::wstring ExtBuf = GetUnpackerExt(index);

                    SetUnpacker(index, GetUnpackerType(index), TitleBuf.c_str(), ExtBuf.c_str(), TRUE,
                                GetUnpackerSupLongNames(index), extractCmdBuf.c_str(), extractArgBuf.c_str(),
                                GetUnpackerNeedANSIListFile(index));
                }
            }
    case 5: // beta 3 but without tar
    case 6: // what is new in beta4?
    case 7: // 1.6b6 - for correct conversion of supported plugin functions (see CPlugins::Load)
    case 8:
        // transition to using variables instead of the direct exe file name
        for (index = 0; index < GetUnpackersCount(); index++)
            // consider only external packers
            if (((GetUnpackerOldType(index) && GetUnpackerType(index) == 1) ||
                 (!GetUnpackerOldType(index) && GetUnpackerType(index) == CUSTOMUNPACKER_EXTERNAL)) &&
                GetUnpackerCmdExecExtract(index) != NULL)
            {
                // take the old commands
                std::wstring cmd = GetUnpackerCmdExecExtract(index);
                i = 0;
                BOOL found = FALSE;
                // and search the table with it
                while (PackConversionTable[i].exe != NULL)
                {
                    // compare with table entries
                    if (cmd == PackConversionTable[i].exe)
                    {
                        // an ugly hack because of RAR
                        if (i == 2)
                            // if it's RAR we cannot tell whether it is 16-bit or 32-bit directly, only from long-name support
                            if (GetUnpackerSupLongNames(index))
                                cmd = PackConversionTable[i].variable;
                            else
                                cmd = L"$(Rar16bitExecutable)";
                        else
                            // for others it's simple
                            cmd = PackConversionTable[i].variable;
                        found = TRUE;
                    }
                    i++;
                }
                // strings must be copied somewhere or we delete them before use
                std::wstring title = GetUnpackerTitle(index);
                std::wstring ext = GetUnpackerExt(index);
                std::wstring args = GetUnpackerCmdArgsExtract(index) ? GetUnpackerCmdArgsExtract(index) : L"";

                if (found)
                    SetUnpacker(index, GetUnpackerType(index), title.c_str(), ext.c_str(), GetUnpackerOldType(index),
                                GetUnpackerSupLongNames(index), cmd.c_str(), args.c_str(),
                                GetUnpackerNeedANSIListFile(index));
            }
    case 9: // 1.6b6 - due to switching from exe name to variable in custom packers
        // enable ANSI file list for ACE32 and PKZIP25
        for (index = 0; index < GetUnpackersCount(); index++)
        {
            if ((GetUnpackerOldType(index) && GetUnpackerType(index) == 1) ||
                (!GetUnpackerOldType(index) && GetUnpackerType(index) == CUSTOMUNPACKER_EXTERNAL))
            {
                const wchar_t* s = GetUnpackerCmdExecExtract(index);
                if (s != NULL && (wcscmp(s, L"$(Zip32bitExecutable)") == 0 ||
                                  wcscmp(s, L"$(Ace32bitExecutable)") == 0))
                {
                    Unpackers[index]->NeedANSIListFile = TRUE;
                }
            }
        }
        // change "XXX (Internal)" to "XXX (Plugin)"
        // we’re doing a brutal overwrite because we’re replacing it with a shorter text -> simple overwrite is enough
        for (index = 0; index < GetUnpackersCount(); index++)
        {
            if ((GetUnpackerOldType(index) && GetUnpackerType(index) != 1) ||
                (!GetUnpackerOldType(index) && GetUnpackerType(index) != CUSTOMUNPACKER_EXTERNAL))
            { // take only plug-ins (not external unpackers)
                std::wstring& s = Unpackers[index]->Title;
                size_t pos = s.find(L"(Internal)");
                if (pos != std::wstring::npos && pos + 10 == s.length())
                {
                    s.replace(pos, 10, L"(Plugin)");
                }
            }
        }
    case 10: // 1.6b6 - due to renaming "XXX (Internal)" to "XXX (Plugin)" in the Pack and Unpack dialogs
    case 11: // 1.6b7 - added CheckVer plugin - ensure its automatic installation
    case 12: // 2.0 - auto-disable salopen.exe + added PEViewer plugin - ensure its automatic installation
    case 13: // 2.5b1 - added missing configuration conversion for custom unpacker - reflecting LHA change
    case 14: // 2.5b1 - New Advanced Options in the Find dialog. Switched to CFilterCriteria. Converted the inverse filter mask.
    case 15: // 2.5b2 - newer version so plugins load (upgrade registry entries)
    case 16: // 2.5b2 - added coloring of encrypted files and folders (added when loading config and in default config)
    case 17: // 2.5b2 - added *.xml mask to internal viewer settings - "force text mode"
    case 18: // 2.5b3 - for now, only to transfer plugin configuration from 2.5b2
    case 19: // 2.5b4 - for now, only to transfer plugin configuration from 2.5b3
    case 20: // 2.5b5 - for now, only to transfer plugin configuration from 2.5b4
    case 21: // 2.5b6 - for now, only to transfer plugin configuration from 2.5b5(a)
    case 22: // 2.5b6 - filters in panels -- unified into a single history
    case 23: // 2.5b6 - new panel view (Tiles)
    case 24: // 2.5b7 - for now, only to transfer plugin configuration from 2.5b6
    case 25: // 2.5b7 - plugins: show in plugin bar -> variable moved to CPluginData
    case 26: // 2.5b8 - for now, only to transfer plugin configuration from 2.5b7
    case 27: // 2.5b9 - for now, only to transfer plugin configuration from 2.5b8
    case 28: // 2.5b9 - new color scheme based on the old DOS Navigator -> convert 'scheme'
    case 29: // 2.5b10 - for now, only to transfer plugin configuration from 2.5b9
    case 30: // 2.5b11 - for now, only to transfer plugin configuration from 2.5b10
    case 31: // 2.5b11 - added a Floppy section in the Drives configuration and need to force icon reading for Removable drives
    case 32: // 2.5b11 - Find: "Local Settings\Temporary Internet Files" is searched implicitly
    case 33: // 2.5b12 - only to transfer plugin configuration from version 2.5b11
    {
        // PKZIP25 gained "-nozipextension -directories" and "*.pk3;*.jar", we must add it
        const wchar_t* newPKZIP25Args = L"-ext -nozipextension -directories -path \"$(ArchiveFullName)\" @\"$(ListFullName)\"";
        const wchar_t* newPKZIP25Ext = L"*.zip;*.pk3;*.jar";
        for (index = 0; index < GetUnpackersCount(); index++)
        {
            if ((GetUnpackerOldType(index) && GetUnpackerType(index) == 1) ||
                (!GetUnpackerOldType(index) && GetUnpackerType(index) == CUSTOMPACKER_EXTERNAL))
            {
                const wchar_t* extrEXE = GetUnpackerCmdExecExtract(index);
                const wchar_t* extrArgs = GetUnpackerCmdArgsExtract(index);
                const wchar_t* ext = GetUnpackerExt(index);

                // check whether this is a PKZIP25 custom unpacker
                if (extrEXE != NULL && wcscmp(extrEXE, L"$(Zip32bitExecutable)") == 0)
                {
                    // test whether this is an old PKZIP25 custom unpacker entry
                    if (extrArgs != NULL &&
                        wcscmp(extrArgs, L"-ext -path \"$(ArchiveFullName)\" @\"$(ListFullName)\"") == 0 &&
                        wcscmp(ext, L"*.zip") == 0)
                    {
                        // convert to new arguments (added "-nozipextension" + "*.pk3;*.jar")
                        Unpackers[index]->CmdArgsExtract = newPKZIP25Args;
                        Unpackers[index]->Ext = newPKZIP25Ext;
                    }
                }
            }
        }
    }
        // case 34:   // 2.5b12 - adjustment of the external PKZIP25 packer/unpacker (external Win32 version)

    default:
        break;
    }
    if (SalamVersion > 1 && SalamVersion < 81)
    {
        // since RAR 5.0 filelists are ANSI by default instead of OEM, so we must force OEM with a switch
        const wchar_t* newRAR5Args = L"x -scol \"$(ArchiveFullName)\" @\"$(ListFullName)\"";
        for (index = 0; index < GetUnpackersCount(); index++)
        {
            if ((GetUnpackerOldType(index) && GetUnpackerType(index) == 1) ||
                (!GetUnpackerOldType(index) && GetUnpackerType(index) == CUSTOMPACKER_EXTERNAL))
            {
                const wchar_t* extrEXE = GetUnpackerCmdExecExtract(index);
                const wchar_t* extrArgs = GetUnpackerCmdArgsExtract(index);
                const wchar_t* ext = GetUnpackerExt(index);

                // check whether this is a RAR Win32 custom unpacker
                if (extrEXE != NULL && wcscmp(extrEXE, L"$(Rar32bitExecutable)") == 0)
                {
                    // test whether this is an old RAR Win32 custom unpacker entry
                    if (extrArgs != NULL &&
                        wcscmp(extrArgs, L"x \"$(ArchiveFullName)\" @\"$(ListFullName)\"") == 0)
                    {
                        // convert to new arguments (added "-scol")
                        Unpackers[index]->CmdArgsExtract = newRAR5Args;
                    }
                }
            }
        }
    }
}

BOOL CUnpackerConfig::Load(CUnpackerConfig& src)
{
    PreferedUnpacker = src.PreferedUnpacker;

    DeleteAllUnpackers();
    int i;
    for (i = 0; i < src.GetUnpackersCount(); i++)
    {
        int index = AddUnpacker();
        if (index == -1)
            return FALSE;
        if (!SetUnpacker(index, src.GetUnpackerType(i), src.GetUnpackerTitle(i), src.GetUnpackerExt(i),
                         FALSE, src.GetUnpackerSupLongNames(i),
                         src.GetUnpackerCmdExecExtract(i), src.GetUnpackerCmdArgsExtract(i),
                         src.GetUnpackerNeedANSIListFile(i)))
            return FALSE;
    }
    return TRUE;
}

int CUnpackerConfig::AddUnpacker(BOOL toFirstIndex)
{
    CALL_STACK_MESSAGE2("CUnpackerConfig::AddUnpacker(%d)", toFirstIndex);
    CUnpackerConfigData* data = new CUnpackerConfigData;
    if (data == NULL)
        return -1;
    int index;
    if (toFirstIndex)
    {
        Unpackers.Insert(0, data);
        index = 0;
        if (PreferedUnpacker != -1)
            PreferedUnpacker++;
    }
    else
        index = Unpackers.Add(data);
    if (!Unpackers.IsGood())
    {
        Unpackers.ResetState();
        return -1;
    }
    return index;
}

/*
BOOL
CUnpackerConfig::SwapUnpackers(int index1, int index2)
{
  std::swap(Unpackers[index1], Unpackers[index2]);
  return TRUE;
}
*/

BOOL CUnpackerConfig::MoveUnpacker(int srcIndex, int dstIndex)
{
    CUnpackerConfigData* tmp = Unpackers[srcIndex];
    if (srcIndex < dstIndex)
    {
        int i;
        for (i = srcIndex; i < dstIndex; i++)
            Unpackers[i] = Unpackers[i + 1];
    }
    else
    {
        int i;
        for (i = srcIndex; i > dstIndex; i--)
            Unpackers[i] = Unpackers[i - 1];
    }
    Unpackers[dstIndex] = tmp;
    return TRUE;
}

void CUnpackerConfig::DeleteUnpacker(int index)
{
    if (PreferedUnpacker >= 0 && PreferedUnpacker < Unpackers.Count)
    {
        if (index < PreferedUnpacker)
            PreferedUnpacker--; // adjust index to represent the same item
        else
        {
            if (index == PreferedUnpacker)
                PreferedUnpacker = -1; // we lost the selected item
        }
    }
    Unpackers.Delete(index);
}

BOOL CUnpackerConfig::SetUnpacker(int index, int type, const wchar_t* title, const wchar_t* ext, BOOL old,
                                  BOOL supportLongNames,
                                  const wchar_t* cmdExecExtract, const wchar_t* cmdArgsExtract,
                                  BOOL needANSIListFile)
{
    CALL_STACK_MESSAGE10("CUnpackerConfig::SetUnpacker(%d, %d, %ls, %ls, %d, %d, %ls, %ls, %d)",
                         index, type, title, ext, old, supportLongNames, cmdExecExtract, cmdArgsExtract,
                         needANSIListFile);
    CUnpackerConfigData* data = Unpackers[index];
    data->Destroy();
    data->Type = type;
    data->OldType = old;
    data->Title = title;
    data->Ext = ext;
    if (old && data->Type == 1 ||
        !old && data->Type == CUSTOMUNPACKER_EXTERNAL)
    {
        data->CmdExecExtract = cmdExecExtract ? cmdExecExtract : L"";
        data->CmdArgsExtract = cmdArgsExtract ? cmdArgsExtract : L"";
        data->SupportLongNames = supportLongNames;
        data->NeedANSIListFile = needANSIListFile;
    }

    if (data->IsValid())
    {
        if (PreferedUnpacker == -1)
            PreferedUnpacker = Unpackers.Count - 1;
        return TRUE;
    }
    else
    {
        Unpackers.Delete(index);
        return FALSE;
    }
}

BOOL CUnpackerConfig::SetUnpackerTitle(int index, const wchar_t* title)
{
    CUnpackerConfigData* data = Unpackers[index];
    data->Title = title;
    return TRUE;
}

BOOL CUnpackerConfig::ExecuteUnpacker(HWND parent, CFilesWindow* panel, const wchar_t* zipFile, const wchar_t* mask,
                                      const wchar_t* targetDir, BOOL delArchiveWhenDone, CDynamicString* archiveVolumes)
{
    CALL_STACK_MESSAGE5("CUnpackerConfig::ExecuteUnpacker(, %ls, %ls, %ls, %d, )",
                        zipFile, mask, targetDir, delArchiveWhenDone);
    if (PreferedUnpacker != -1 && PreferedUnpacker < Unpackers.Count)
    {
        CUnpackerConfigData* data = Unpackers[PreferedUnpacker];
        if (data->Type == CUSTOMUNPACKER_EXTERNAL)
        {
            if (delArchiveWhenDone)
                TRACE_E("CUnpackerConfig::ExecuteUnpacker(): delArchiveWhenDone is TRUE for external archiver (unsupported, ignoring)");

            wchar_t* tmpMask = DupStr(mask);
            std::wstring command = data->CmdExecExtract + L" " + data->CmdArgsExtract;
            if (tmpMask == NULL)
            {
                TRACE_E(LOW_MEMORY);
                return FALSE;
            }

            // we must store the pointer for deallocation; it will be destroyed
            wchar_t* tmpMask2 = tmpMask;
            BOOL ret = PackUniversalUncompress(parent, command.c_str(), NULL, targetDir, FALSE, panel,
                                               data->SupportLongNames, zipFile, targetDir,
                                               NULL, PackEnumMask, &tmpMask, data->NeedANSIListFile);
            free(tmpMask2);
            return ret;
        }
        else
        {
            CPluginData* plugin = Plugins.Get(-data->Type - 1);
            if (plugin != NULL && plugin->SupportCustomUnpack)
            {
                return plugin->UnpackWholeArchive(panel, zipFile, mask, targetDir, delArchiveWhenDone, archiveVolumes);
            }
            else
                TRACE_E("Unexpected situation in CUnpackerConfig::ExecuteUnpacker().");
        }
    }
    return FALSE;
}

BOOL CUnpackerConfig::Save(int index, HKEY hKey)
{
    DWORD d;
    BOOL ret = TRUE;
    int type = GetUnpackerType(index);
    d = type;
    if (ret)
        ret &= SetValueW(hKey, SALAMANDER_CPU_TYPE, REG_DWORD, &d, sizeof(d));
    if (ret)
        ret &= SetValueW(hKey, SALAMANDER_CPU_TITLE, REG_SZ, GetUnpackerTitle(index), -1);
    if (ret)
        ret &= SetValueW(hKey, SALAMANDER_CPU_EXT, REG_SZ, GetUnpackerExt(index), -1);
    if (ret && type == CUSTOMUNPACKER_EXTERNAL)
    {
        d = GetUnpackerSupLongNames(index);
        if (ret)
            ret &= SetValueW(hKey, SALAMANDER_CPU_SUPLONG, REG_DWORD, &d, sizeof(d));
        d = GetUnpackerNeedANSIListFile(index);
        if (ret)
            ret &= SetValueW(hKey, SALAMANDER_CPU_ANSILIST, REG_DWORD, &d, sizeof(d));
        if (ret)
            ret &= SetValueW(hKey, SALAMANDER_CU_EXECEXTRACT, REG_SZ, GetUnpackerCmdExecExtract(index), -1);
        if (ret)
            ret &= SetValueW(hKey, SALAMANDER_CU_ARGSEXTRACT, REG_SZ, GetUnpackerCmdArgsExtract(index), -1);
    }
    return ret;
}

BOOL CUnpackerConfig::Load(HKEY hKey)
{
    std::wstring title;
    std::wstring ext;
    DWORD type;
    DWORD suplong = FALSE;
    DWORD needANSI = FALSE;
    std::wstring execcopy;
    std::wstring argscopy;

    BOOL ret = TRUE;
    if (ret)
        ret &= GetValueW(hKey, SALAMANDER_CPU_TYPE, REG_DWORD, &type, sizeof(DWORD));
    if (ret)
        ret &= GetStringValueW(hKey, SALAMANDER_CPU_TITLE, title);
    if (ret)
        ret &= GetStringValueW(hKey, SALAMANDER_CPU_EXT, ext);
    if (ret && (Configuration.ConfigVersion < 6 && type == 1 ||
                Configuration.ConfigVersion >= 6 && type == CUSTOMUNPACKER_EXTERNAL))
    {
        if (ret)
        {
            if (!GetValueW(hKey, SALAMANDER_CPU_ANSILIST, REG_DWORD, &needANSI, sizeof(DWORD)))
                needANSI = FALSE; // in older versions it wasn't present, assumed FALSE
        }

        if (ret)
            ret &= GetValueW(hKey, SALAMANDER_CPU_SUPLONG, REG_DWORD, &suplong, sizeof(DWORD));
        if (ret)
            ret &= GetStringValueW(hKey, SALAMANDER_CU_EXECEXTRACT, execcopy);
        if (ret)
            ret &= GetStringValueW(hKey, SALAMANDER_CU_ARGSEXTRACT, argscopy);
    }

    if (ret)
    {
        int index;
        if ((index = AddUnpacker()) == -1)
            return FALSE;
        if (Configuration.ConfigVersion < 44) // convert extensions to lowercase
            LowerCaseExtensionForUpgrade(ext);
        ret &= SetUnpacker(index, (int)type, title.c_str(), ext.c_str(), Configuration.ConfigVersion < 6,
                           (BOOL)suplong,
                           execcopy.c_str(), argscopy.c_str(), needANSI);
    }

    return ret;
}
