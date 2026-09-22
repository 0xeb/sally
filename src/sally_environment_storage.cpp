// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "cfgdlg.h"
#include "plugins.h"
#include "fileswnd.h"
#include "mainwnd.h"
#include "salinflt.h"
#include "common/unicode/helpers.h"
#include "common/fsutil.h" // GetRootPathW / IsUNCPathW
#include "common/IFileSystem.h"

// ************************************************************************************************************************
//
// RegenEnvironmentVariables
//

// Windows Explorer can regenerate env variables in real time as soon as someone changes them via Control Panel
// or in the registry and broadcasts WM_SETTINGCHANGE / lParam == "Environment".
// Regeneration is done via the undocumented SHELL32.DLL / RegenerateUserEnvironment function, which builds
// env variables for a new process. We used this function for years, but while investigating the issue reported
// on the forum https://forum.altap.cz/viewtopic.php?f=2&t=6188 we found it is not ideal for Salamander.
// It has two problems: when called from an x86 process on x64 Windows it drops several important variables:
// "CommonProgramFiles(x86)", "CommonProgramW6432", "ProgramFiles(x86)", "ProgramW6432".
// The second problem is that it drops variables that the process inherited on startup. For Windows Explorer
// neither issue is a problem because on x64 Windows it is always x64 and also does not inherit any special
// variables since it is not started by the user but by the system.
//
// Implementing our own RegenerateUserEnvironment seems problematic, because we would need to pull data from
// several registry locations, expand it, merge paths, etc. It can also be expected to differ by Windows version.
// FAR uses this approach as a response to WM_SETTINGCHANGE.
//
// An optimal solution is to use the system RegenerateUserEnvironment in a smarter way.
// At process start, read env variables using GetEnvironmentStrings() API. Then call
// RegenerateUserEnvironment() (takes 4ms, so no problem) and read env variables again.
// Find the differences. We get a list of variables that disappeared, appeared, or changed.

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
    wchar_t* Name;        // allocated variable, originally NAME=VAL\0 rewritten to NAME\0VAL\0
    const wchar_t* Value; // non-allocated variable, just a pointer into Name buffer to value VAL (after original equals sign)
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
    int FindItemIndex(const wchar_t* name);

    // adds copy of item to array, sets Type
    void AddVarCopy(const CEnvVariable* var, DWORD type);
};

void CEnvVariables::QuickSort(int left, int right)
{
    int i = left, j = right;
    const wchar_t* pivot = Variables[(i + j) / 2].Name;

    do
    {
        while (StrICmpW(Variables[i].Name, pivot) < 0 && i < right)
            i++;
        while (StrICmpW(pivot, Variables[j].Name) < 0 && j > left)
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

int CEnvVariables::FindItemIndex(const wchar_t* name)
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
        int cmp = StrICmpW(Variables[index].Name, name);
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
    int len = (int)wcslen(var->Name) + 1 + (int)wcslen(var->Value) + 1;
    newVar.Name = (wchar_t*)malloc(len * sizeof(wchar_t));
    wcscpy(newVar.Name, var->Name);
    wcscpy(newVar.Name + wcslen(newVar.Name) + 1, var->Value);
    newVar.Value = newVar.Name + wcslen(newVar.Name) + 1;
    newVar.Type = type;
    Variables.Add(newVar);
}

void CEnvVariables::LoadFromProcess()
{
    CALL_STACK_MESSAGE1("CEnvVariables::LoadFromProcess()");

    // discard current elements in array
    Clean();

    wchar_t* vars = GetEnvironmentStringsW();
    wchar_t* p = vars;
    while (*p != 0)
    {
        wchar_t* begin = p;
        while (*p != 0)
            p++;
        // if this is not current dir for drives, save found item to array
        // We ignore:
        // =::=::\
    // =C:=C:\Program Files (x86)\Microsoft Visual Studio 9.0\Common7\IDE
        // =E:=E:\Source\sally\vcproj
        if (*begin != '=')
        {
            CEnvVariable envVar;
            ZeroMemory(&envVar, sizeof(envVar));
            envVar.Name = DupStr(begin);
            wchar_t* value = envVar.Name;
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

    FreeEnvironmentStringsW(vars);

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
                                                      : StrICmpW(oldVar->Name, newVar->Name);
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
            SetEnvironmentVariableW(var->Name, var->Type == ENVVARTYPE_ADD ? var->Value : NULL);
    }
#ifndef _WIN64
    // HACK: working around a bug that MS made and haven't fixed yet (according to some statement on the web)
    // occurs with x86 processes running on x64 Windows, where reload incorrectly sets the value to AMD64
    SetEnvironmentVariableW(L"PROCESSOR_ARCHITECTURE", L"x86");
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

// Wide-native, on top of the ported reparse walk.
//
// The narrow form's INPUT is what was lossy: the path is narrowed on the way to
// GetResolvedPathMountPointAndGUID, so for a directory the code page cannot
// spell the GUID lookup resolved the wrong path — or none — and Sally reported
// "not an SSD" for a perfectly ordinary SSD. The GUID path it works with
// afterwards is ASCII either way.
BOOL IsPathOnSSDW(const wchar_t* path)
{
    std::wstring guidPath;
    if (!GetResolvedPathMountPointAndGUIDW(path, NULL, &guidPath))
        return FALSE;

    SalPathRemoveBackslashW(guidPath); // the following CreateFile was bothered by the backslash after volume

    bool trim = false;
    const FileResult trimResult = gFileSystem->QueryVolumeTrim(guidPath.c_str(), &trim);
    if (trimResult.success)
        TRACE_I("QueryVolumeTRIM: " << trim);
    else
        TRACE_I("QueryVolumeTRIM failed. Err=" << trimResult.errorCode);

    bool seekPenalty = true;
    const FileResult seekResult = gFileSystem->QueryVolumeSeekPenalty(guidPath.c_str(), &seekPenalty);
    if (seekResult.success)
        TRACE_I("QueryVolumeSeekPenalty: " << seekPenalty);
    else
        TRACE_I("QueryVolumeSeekPenalty failed. Err=" << seekResult.errorCode);

    WORD rpm = 0;
    if (RunningAsAdmin)
    {
        const FileResult rotationResult = gFileSystem->QueryVolumeRotationRate(guidPath.c_str(), &rpm);
        if (rotationResult.success)
            TRACE_I("QueryVolumeATARPM: " << rpm);
        else
            TRACE_I("QueryVolumeATARPM failed. Err=" << rotationResult.errorCode);
    }
    return trim || !seekPenalty || rpm == 1;
}

// The narrow IsPathOnSSD is DELETED, not kept as a wrapper: its
// only caller was the SDK forwarder in zip.cpp, which is wide now. A wrapper
// would have added an AnsiToWide to serve nobody.

// Wide-native. Outputs are std::wstring rather than caller-supplied
// buffers, which is why this one could widen while the SDK method of the same
// name cannot — an out-buffer whose width changes has no compatible shim, so
// that one waits for its callers.
static BOOL GetVolumeGuidPathW(const wchar_t* mountPoint, std::wstring& guidPath)
{
    DWORD capacity = 64;
    for (;;)
    {
        std::vector<wchar_t> buffer(capacity);
        if (GetVolumeNameForVolumeMountPointW(mountPoint, buffer.data(), capacity))
        {
            guidPath.assign(buffer.data());
            return TRUE;
        }

        const DWORD error = GetLastError();
        if (error != ERROR_FILENAME_EXCED_RANGE && error != ERROR_MORE_DATA)
            return FALSE;
        if (capacity > (MAXDWORD / 2))
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        capacity *= 2;
    }
}
BOOL GetResolvedPathMountPointAndGUIDW(const wchar_t* path, std::wstring* mountPoint, std::wstring* guidPath)
{
    std::wstring resolvedPath(path);
    ResolveSubstsW(resolvedPath);
    std::wstring rootPath = GetRootPath(resolvedPath.c_str());

    BOOL remotePath = TRUE;
    if (!IsUNCPathW(rootPath.c_str()) && GetDriveTypeW(rootPath.c_str()) == DRIVE_FIXED) // reparse points make sense to look for only on fixed disks
    {
        CLocalPathResolutionW res;
        ResolveLocalPathWithReparsePointsW(path, res);
        resolvedPath = res.ResPath;
        remotePath = !res.NetPath.empty();

        // for GetVolumeNameForVolumeMountPoint we need root
        if (res.CutResPathIsPossible)
            resolvedPath = GetRootPath(resolvedPath.c_str());
    }
    else
    {
        // for non-DRIVE_FIXED disks we take the root path: GetVolumeNameForVolumeMountPoint
        // needs a mount point, and hunting for one by gradually shortening the path is too
        // expensive here (network paths; cards should not have mount points in subdirectories)
        resolvedPath = rootPath;
    }

    // GUID can be obtained even for non-DRIVE_FIXED disks, for example card readers
    SalPathAddBackslashW(resolvedPath); // GetVolumeNameForVolumeMountPoint requires backslash at the end

    std::wstring guid;
    if (GetVolumeGuidPathW(resolvedPath.c_str(), guid))
    {
        if (mountPoint != NULL)
            *mountPoint = resolvedPath;
        if (guidPath != NULL)
        {
            SalPathAddBackslashW(guid);
            *guidPath = std::move(guid);
        }
        return TRUE;
    }

    if (!remotePath) // for network paths it currently returns errors normally = we won't report it, we won't annoy users
    {
        DWORD err = GetLastError();
        TRACE_EW(L"GetResolvedPathMountPointAndGUIDW(): GetVolumeNameForVolumeMountPoint() failed: " << GetErrorTextOwned(err).c_str());
    }
    return FALSE;
}
