// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"
#include <ntddscsi.h>

#include "cfgdlg.h"
#include "plugins.h"
#include "fileswnd.h"
#include "mainwnd.h"
#include "salinflt.h"

// ************************************************************************************************************************
//
// RegenEnvironmentVariables
//

// Windows Explorer dokaze realtime regenerovat env. promenne, jakmile je nekdo zmeni pres control panel nebo
// v registry a rozesle notifikaci WM_SETTINGCHANGE / lParam == "Environment".
// Regeneraci provadi promoci nedokumentovane funkce SHELL32.DLL / RegenerateUserEnvironment, ktera sestavi
// env. promenne pro novy proces. Roky jsme tuto funkci pouzivali, ale pri pruzkumu problemu nahlaseneho
// na foru https://forum.altap.cz/viewtopic.php?f=2&t=6188 se ukazalo, ze pro Salamandera neni funkce idealni.
// Ma dva problemy: pri zavolani z x86 procesu na x64 Windows zahodi nekolik podstatnych promennych:
// "CommonProgramFiles(x86)", "CommonProgramW6432", "ProgramFiles(x86)", "ProgramW6432".
// Druhy problem je, ze zahodi promenne, ktere proces podedil pri spousteni. V pripade Windows Explorer
// ani jedna vec neni problem, protoze ten je pod x64 Win vzdy x64 a zaroven nededi zadne specialni promenne,
// protoze ho nespousti uzivatel, ale system.
//
// Naprogramovani vlastniho RegenerateUserEnvironment se jevi jako problematicka, protoze je potreba
// vytahnout z nekolika mist v registry data, expandovat je, mergovat cesty, atd. Zaroven lze predpokladat,
// ze se funkce bude lisit dle verze Windows. Tento postup pouziva FAR jako reakci na WM_SETTINGCHANGE.
//
// Jako optimalni reseni vidime pouziti systemove RegenerateUserEnvironment chytrejsim zpusobem.
// Pri spusteni procesu vytahnout env. promenne pomoci GetEnvironmentStrings() API. Nasledne zavolat
// RegenerateUserEnvironment() (trva 4ms, takze neni problem) a opet vytahnout env. promenne.
// Zjistit diference. Dostaneme seznam promennych, ktere zmizely nebo pribyly nebou se zmenily.

BOOL EnvVariablesDifferencesFound = FALSE; // protection against premature regeneration until we have found differences

typedef WINSHELLAPI BOOL(WINAPI* FT_RegenerateUserEnvironment)(
    void** prevEnv,
    BOOL setCurrentEnv);

BOOL RegenerateUserEnvironment()
{
    CALL_STACK_MESSAGE1("RegenerateUserEnvironment()");

    // undocumented API, found by stepping through NT4
    FT_RegenerateUserEnvironment proc = (FT_RegenerateUserEnvironment)GetProcAddress(Shell32DLL, "RegenerateUserEnvironment"); // undocumented
    if (proc == NULL)
    {
        TRACE_E("Cannot find RegenerateUserEnvironment export in the SHELL32.DLL!");
        return FALSE;
    }

    void* prevEnv;
    if (!proc(&prevEnv, TRUE))
    {
        TRACE_E("RegenerateUserEnvironment failed");
        return FALSE;
    }

    return TRUE;
}

#define ENVVARTYPE_NONE 0
#define ENVVARTYPE_ADD 1 // if variable does not exist in array after reload, we add it
#define ENVVARTYPE_DEL 2 // if variable exists in array after reload, we remove it

struct CEnvVariable
{
    char* Name;        // allocated variable, originally NAME=VAL\0 rewritten to NAME\0VAL\0
    const char* Value; // non-allocated variable, just a pointer into Name buffer to value VAL (after original equals sign)
    DWORD Type;        // 0
};

class CEnvVariables
{
protected:
    TDirectArray<CEnvVariable> Variables;
    BOOL Sorted;

public:
    CEnvVariables() : Variables(20, 10)
    {
        Sorted = FALSE;
    }

    ~CEnvVariables()
    {
        Clean();
    }

    // fills array based on data returned from GetEnvironmentStrings() API
    void LoadFromProcess();

    // fills array based on 'oldVars' and 'newVars'
    void FindDifferences(CEnvVariables* oldVars, CEnvVariables* newVars);

    // applies differences 'diffVars' to our process (adds / removes items)
    // WARNING: does not modify the object's array, only uses it as current snapshot to compare differences against
    void ApplyDifferencesToCurrentProcess(CEnvVariables* diffVars);

protected:
    void Clean()
    {
        for (int i = 0; i < Variables.Count; i++)
            free(Variables[i].Name);
        Variables.DestroyMembers();
        Sorted = FALSE;
    }

    // sorts array case insensitive by name
    void QuickSort(int left, int right);

    // if item 'name' is found in array, returns its index; otherwise returns -1;
    // assumes array is alphabetically sorted, uses binary search
    int FindItemIndex(const char* name);

    // adds copy of item to array, sets Type
    void AddVarCopy(const CEnvVariable* var, DWORD type);
};

void CEnvVariables::QuickSort(int left, int right)
{
    int i = left, j = right;
    const char* pivot = Variables[(i + j) / 2].Name;

    do
    {
        while (StrICmp(Variables[i].Name, pivot) < 0 && i < right)
            i++;
        while (StrICmp(pivot, Variables[j].Name) < 0 && j > left)
            j--;

        if (i <= j)
        {
            CEnvVariable swap = Variables[i];
            Variables[i] = Variables[j];
            Variables[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    if (left < j)
        QuickSort(left, j);
    if (i < right)
        QuickSort(i, right);

    Sorted = TRUE;
}

int CEnvVariables::FindItemIndex(const char* name)
{
    if (!Sorted)
    {
        TRACE_C("CEnvVariables::FindItemIndex() Array is not sorted!");
        return -1;
    }

    int left = 0;
    int right = Variables.Count - 1;
    while (left < right)
    {
        int index = (left + right) / 2;
        int cmp = StrICmp(Variables[index].Name, name);
        if (cmp == 0)
            return index;
        else if (cmp > 0)
            right = index - 1;
        else
            left = index + 1;
    }
    return -1;
}

void CEnvVariables::AddVarCopy(const CEnvVariable* var, DWORD type)
{
    CEnvVariable newVar;
    int len = (int)strlen(var->Name) + 1 + (int)strlen(var->Value) + 1;
    newVar.Name = (char*)malloc(len);
    strcpy(newVar.Name, var->Name);
    strcpy(newVar.Name + strlen(newVar.Name) + 1, var->Value);
    newVar.Value = newVar.Name + strlen(newVar.Name) + 1;
    newVar.Type = type;
    Variables.Add(newVar);
}

void CEnvVariables::LoadFromProcess()
{
    CALL_STACK_MESSAGE1("CEnvVariables::LoadFromProcess()");

    // discard current elements in array
    Clean();

    char* vars = GetEnvironmentStrings();
    char* p = vars;
    while (*p != 0)
    {
        char* begin = p;
        while (*p != 0)
            p++;
        // if this is not current dir for drives, save found item to array
        // We ignore:
        // =::=::\
    // =C:=C:\Program Files (x86)\Microsoft Visual Studio 9.0\Common7\IDE
        // =E:=E:\Source\salamand\vcproj
        if (*begin != '=')
        {
            CEnvVariable envVar;
            ZeroMemory(&envVar, sizeof(envVar));
            envVar.Name = DupStr(begin);
            char* value = envVar.Name;
            while (*value != 0 && *value != '=')
                value++;
            if (*value == '=')
            {
                *value = 0;
                value++;
            }
            envVar.Value = value;
            Variables.Add(envVar);
        }
        p++;
    }

    FreeEnvironmentStrings(vars);

    // note: array returned from GetEnvironmentStrings() looks sorted, but when setting env. variables new variables are added to the end,
    // so we sort to be able to compare and search
    if (Variables.Count > 1)
        QuickSort(0, Variables.Count - 1);

    //  for (int i = 0; i < Variables.Count; i++)
    //    TRACE_I(Variables[i].Name);
}

void CEnvVariables::FindDifferences(CEnvVariables* oldVars, CEnvVariables* newVars)
{
    CALL_STACK_MESSAGE1("CEnvVariables::FindDifferences()");

    if (!oldVars->Sorted || !newVars->Sorted)
    {
        TRACE_C("CEnvVariables::FindItemIndex() Array is not sorted!");
        return;
    }

    // discard current elements in array
    Clean();

    //  compare arrays oldVars and newVars and store differences
    int oldIndex = 0;
    int newIndex = 0;
    while (oldIndex < oldVars->Variables.Count || newIndex < newVars->Variables.Count)
    {
        const CEnvVariable* oldVar = oldIndex < oldVars->Variables.Count ? &oldVars->Variables[oldIndex] : NULL;
        const CEnvVariable* newVar = newIndex < newVars->Variables.Count ? &newVars->Variables[newIndex] : NULL;
        int cmp = oldVar == NULL ? 1 : newVar == NULL ? -1
                                                      : StrICmp(oldVar->Name, newVar->Name);
        if (cmp < 0)
        {
            AddVarCopy(oldVar, ENVVARTYPE_ADD);
            //      TRACE_I("ADD: "<<oldVar->Name<<" = "<<oldVar->Value);
            oldIndex++;
        }
        else
        {
            if (cmp > 0)
            {
                //        AddVarCopy(newVar, ENVVARTYPE_DEL);  // we came to the decision that it's better not to delete anything... Petr+Honza
                //        TRACE_I("DEL: "<<newVar->Name<<" = "<<newVar->Value);
                newIndex++;
            }
            else
            {
                // we ignore differences for now, for example in PATH, etc
                //        if (strcmp(oldVar->Value, newVar->Value) != 0)
                //          TRACE_I("DIFF: " << oldVar->Name << " = "<<oldVar->Value<<" : "<<newVar->Value);
                //        else
                //          TRACE_I("SAME: " << oldVar->Name << " = "<<oldVar->Value);
                oldIndex++;
                newIndex++;
            }
        }
    }
}

void CEnvVariables::ApplyDifferencesToCurrentProcess(CEnvVariables* diffVars)
{
    for (int i = 0; i < diffVars->Variables.Count; i++)
    {
        const CEnvVariable* var = &diffVars->Variables[i];
        if (FindItemIndex(var->Name) == -1)
            SetEnvironmentVariable(var->Name, var->Type == ENVVARTYPE_ADD ? var->Value : NULL);
    }
#ifndef _WIN64
    // HACK: working around a bug that MS made and haven't fixed yet (according to some statement on the web)
    // occurs with x86 processes running on x64 Windows, where reload incorrectly sets the value to AMD64
    SetEnvironmentVariable("PROCESSOR_ARCHITECTURE", "x86");
#endif // _WIN64
}

CEnvVariables EnvVariablesDiff;

void InitEnvironmentVariablesDifferences()
{
    CALL_STACK_MESSAGE1("InitEnvironmentVariablesDifferences()");

    // save initial state of env. variables
    CEnvVariables oldVars;
    oldVars.LoadFromProcess();

    // ask system for reload, which discards some variables
    RegenerateUserEnvironment();

    // extract current state of variables
    CEnvVariables newVars;
    newVars.LoadFromProcess();

    // compare old and new version of variables and save resulting DIFF to EnvVariablesDiff
    EnvVariablesDiff.FindDifferences(&oldVars, &newVars);

    // based on new state and differences, change our process's variables
    newVars.ApplyDifferencesToCurrentProcess(&EnvVariablesDiff);

    EnvVariablesDifferencesFound = TRUE;
}

void RegenEnvironmentVariables()
{
    CALL_STACK_MESSAGE1("RegenEnvironmentVariables()");

    if (!EnvVariablesDifferencesFound)
    {
        TRACE_E("RegenEnvironmentVariables() regeneration not enabled, call init!");
        return;
    }

    // ask system to reload env. variables
    RegenerateUserEnvironment();

    // extract their current state
    CEnvVariables newVars;
    newVars.LoadFromProcess();

    // based on it, using differences captured at Salamander startup, we 'patch' our process
    newVars.ApplyDifferencesToCurrentProcess(&EnvVariablesDiff);
}

//************************************************************************************************************************
//
// IsPathOnSSD
//
// Inspiration: http://stackoverflow.com/questions/23363115/detecting-ssd-in-windows/33359142#33359142
//              http://nyaruru.hatenablog.com/entry/2012/09/29/063829
//

// does not require administrator rights
BOOL QueryVolumeTRIM(const char* volume, BOOL* trim)
{
    BOOL ret = FALSE;
    HANDLE hVolume = HANDLES(CreateFile(volume, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
    if (hVolume != INVALID_HANDLE_VALUE)
    {
        STORAGE_PROPERTY_QUERY spqTrim;
        spqTrim.PropertyId = (STORAGE_PROPERTY_ID)StorageDeviceTrimProperty;
        spqTrim.QueryType = PropertyStandardQuery;
        DWORD bytesReturned = 0;
        DEVICE_TRIM_DESCRIPTOR dtd = {0};
        if (DeviceIoControl(hVolume, IOCTL_STORAGE_QUERY_PROPERTY,
                            &spqTrim, sizeof(spqTrim), &dtd, sizeof(dtd), &bytesReturned, NULL) &&
            bytesReturned == sizeof(dtd))
        {
            *trim = (dtd.TrimEnabled != 0);
            ret = TRUE;
        }
        else
        {
            int err = ::GetLastError();
            TRACE_I("QueryVolumeTRIM(): DeviceIoControl failed. Err=" << err);
        }
        HANDLES(CloseHandle(hVolume));
    }
    return ret;
}

// does not require administrator rights
BOOL QueryVolumeSeekPenalty(const char* volume, BOOL* seekPenalty)
{
    BOOL ret = FALSE;
    HANDLE hVolume = HANDLES(CreateFile(volume, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
    if (hVolume != INVALID_HANDLE_VALUE)
    {
        STORAGE_PROPERTY_QUERY spqSeekP;
        spqSeekP.PropertyId = (STORAGE_PROPERTY_ID)StorageDeviceSeekPenaltyProperty;
        spqSeekP.QueryType = PropertyStandardQuery;
        DWORD bytesReturned = 0;
        DEVICE_SEEK_PENALTY_DESCRIPTOR dspd = {0};
        if (DeviceIoControl(hVolume, IOCTL_STORAGE_QUERY_PROPERTY,
                            &spqSeekP, sizeof(spqSeekP), &dspd, sizeof(dspd), &bytesReturned, NULL) &&
            bytesReturned == sizeof(dspd))
        {
            *seekPenalty = (dspd.IncursSeekPenalty != 0);
            ret = TRUE;
        }
        else
        {
            int err = ::GetLastError();
            TRACE_I("QueryVolumeSeekPenalty(): DeviceIoControl failed. Err=" << err);
        }
        HANDLES(CloseHandle(hVolume));
    }
    return ret;
}

// requires admin rights to run
// for SSD should return value *rpm == 1
BOOL QueryVolumeATARPM(const char* volume, WORD* rpm)
{
    BOOL ret = FALSE;
    HANDLE hVolume = HANDLES_Q(CreateFile(volume, GENERIC_READ | GENERIC_WRITE,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                          OPEN_EXISTING, 0, NULL));
    if (hVolume != INVALID_HANDLE_VALUE)
    {
        struct ATAIdentifyDeviceQuery
        {
            ATA_PASS_THROUGH_EX header;
            WORD data[256];
        };

        ATAIdentifyDeviceQuery id_query = {};
        memset(&id_query, 0, sizeof(id_query));

        id_query.header.Length = sizeof(id_query.header);
        id_query.header.AtaFlags = ATA_FLAGS_DATA_IN;
        id_query.header.DataTransferLength = sizeof(id_query.data);
        id_query.header.TimeOutValue = 3;                                                     // timeout in seconds
        id_query.header.DataBufferOffset = (DWORD)((BYTE*)&id_query.data - (BYTE*)&id_query); //offsetof(ATAIdentifyDeviceQuery, data[0]);
        id_query.header.CurrentTaskFile[6] = 0xec;                                            // ATA IDENTIFY DEVICE command
        DWORD bytesReturned = 0;
        if (DeviceIoControl(hVolume, IOCTL_ATA_PASS_THROUGH,
                            &id_query, sizeof(id_query), &id_query, sizeof(id_query), &bytesReturned, NULL) &&
            bytesReturned == sizeof(id_query))
        {
//Index of nominal media rotation rate
//SOURCE: http://www.t13.org/documents/UploadedDocuments/docs2009/d2015r1a-ATAATAPI_Command_Set_-_2_ACS-2.pdf
//          7.18.7.81 Word 217
//QUOTE: Word 217 indicates the nominal media rotation rate of the device and is defined in table:
//          Value           Description
//          --------------------------------
//          0000h           Rate not reported
//          0001h           Non-rotating media (e.g., solid state device)
//          0002h-0400h     Reserved
//          0401h-FFFEh     Nominal media rotation rate in rotations per minute (rpm)
//                                  (e.g., 7 200 rpm = 1C20h)
//          FFFFh           Reserved
#define NominalMediaRotRateWordIndex 217
            *rpm = id_query.data[NominalMediaRotRateWordIndex];
            ret = TRUE;
        }
        else
        {
            int err = ::GetLastError();
            TRACE_I("QueryVolumeATARPM(): DeviceIoControl failed. Err=" << err);
        }
        HANDLES(CloseHandle(hVolume));
    }
    return ret;
}

BOOL IsPathOnSSD(const char* path)
{
    BOOL isSSD = FALSE;

    char guidPath[MAX_PATH];
    guidPath[0] = 0;
    if (GetResolvedPathMountPointAndGUID(path, NULL, guidPath))
    {
        SalPathRemoveBackslash(guidPath); // the following CreateFile was bothered by the backslash after volume
        BOOL trim = FALSE;
        if (QueryVolumeTRIM(guidPath, &trim))
            TRACE_I("QueryVolumeTRIM: " << trim);
        BOOL seekPenalty = TRUE;
        if (QueryVolumeSeekPenalty(guidPath, &seekPenalty))
            TRACE_I("QueryVolumeSeekPenalty: " << seekPenalty);
        WORD rpm = 0;
        if (RunningAsAdmin)
        {
            if (QueryVolumeATARPM(guidPath, &rpm))
                TRACE_I("QueryVolumeATARPM: " << rpm);
        }
        return trim || !seekPenalty || rpm == 1;
    }
    return FALSE;
}

BOOL GetResolvedPathMountPointAndGUID(const char* path, char* mountPoint, char* guidPath)
{
    char resolvedPath[MAX_PATH];
    strcpy(resolvedPath, path);
    ResolveSubsts(resolvedPath);
    char rootPath[MAX_PATH];
    GetRootPath(rootPath, resolvedPath);
    BOOL remotePath = TRUE;
    if (!IsUNCPath(rootPath) && GetDriveType(rootPath) == DRIVE_FIXED) // reparse points make sense to look for only on fixed disks
    {
        BOOL cutPathIsPossible = TRUE;
        char netPath[MAX_PATH];
        netPath[0] = 0;
        ResolveLocalPathWithReparsePoints(resolvedPath, path, &cutPathIsPossible, NULL, NULL, NULL, NULL, netPath);
        remotePath = netPath[0] != 0;

        // for GetVolumeNameForVolumeMountPoint we need root
        if (cutPathIsPossible)
        {
            GetRootPath(rootPath, resolvedPath);
            strcpy(resolvedPath, rootPath);
        }
    }
    else
        strcpy(resolvedPath, rootPath); // for non-DRIVE_FIXED disks we take root path, GetVolumeNameForVolumeMountPoint needs mount point and searching for it by gradually shortening the path seems too time-consuming for now (at least for network paths + for cards there shouldn't be mount points in subdirectories, right?)
    // GUID can be obtained even for non-DRIVE_FIXED disks, for example card readers
    // according to https://msdn.microsoft.com/en-us/library/windows/desktop/aa364996%28v=vs.85%29.aspx there is no support for DRIVE_REMOTE yet,
    // but that could potentially come too
    char guidP[MAX_PATH];
    SalPathAddBackslash(resolvedPath, MAX_PATH); // GetVolumeNameForVolumeMountPoint requires backslash at the end
    if (GetVolumeNameForVolumeMountPoint(resolvedPath, guidP, sizeof(guidP)))
    {
        if (mountPoint != NULL)
            strcpy(mountPoint, resolvedPath);
        if (guidPath != NULL)
        {
            SalPathAddBackslash(guidP, sizeof(guidP));
            strcpy(guidPath, guidP);
        }
        return TRUE;
    }
    else
    {
        if (!remotePath) // for network paths it currently returns errors normally = we won't report it, we won't annoy users
        {
            DWORD err = GetLastError();
            TRACE_E("GetResolvedPathMountPointAndGUID(): GetVolumeNameForVolumeMountPoint() failed: " << GetErrorText(err));
        }
    }
    return FALSE;
}
