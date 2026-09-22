// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <windows.h>
#include <shlobj.h>
//
// Initialize GUIDs (should be done only and at-least once per DLL/EXE)
//
#pragma data_seg(".text")
#include <initguid.h>
#include <shlguid.h>
#define INITGUID
#include "lstrfix.h"
#include "shexreg.h"
#include "registry_names.h"
#pragma data_seg()

#define NOHANDLES(function) function // obrana proti zanaseni maker HANDLES do zdrojaku pomoci CheckHnd

//const char *SHEXREG_OPENSALAMANDER = "ServantSalamander";                                // salshext.dll (Sal 2.5 beta 1)
//const char *SHEXREG_OPENSALAMANDER_DESCR = "Shell Extension for Servant Salamander";     // salshext.dll (Sal 2.5 beta 1)
//const char *SHEXREG_OPENSALAMANDER = "ServantSalamander25";                              // salexten.dll - 2.5 beta 2 az RC1
//const char *SHEXREG_OPENSALAMANDER_DESCR = "Shell Extension for Servant Salamander 2.5"; // salexten.dll - 2.5 beta 2 az RC1
//const char* SHEXREG_OPENSALAMANDER = "AltapSalamanderVer" SALSHEXT_SHAREDNAMESAPPENDIX;  // salexten.dll - do 4.0
static const wchar_t SHEXREG_OPENSALAMANDER[] = L"SallyVer" L"S100";
#ifdef INSIDE_SALAMANDER
#include "versinfo.rh2"
#endif // INSIDE_SALAMANDER

#ifdef ENABLE_SH_MENU_EXT

//
// ============================================= spolecna cast
//
/*
const char *SHELLEXT_ROOT_REG = SAL_REG_KEY_SHELL_EXTENSION_ROOT_A;
const char *SHELLEXT_CONTEXTMENU = "Context Menu";
const char *SHELLEXT_VERSION = "Version";

const char *SHELLEXT_CM_NAME = "Name";
const char *SHELLEXT_CM_ONEFILE = "One File";
const char *SHELLEXT_CM_ONEDIRECTORY = "One Directory";
const char *SHELLEXT_CM_MOREFILES = "More Files";
const char *SHELLEXT_CM_MOREDIRECTORIES = "More Directories";
const char *SHELLEXT_CM_AND = "Logical AND";

const char *SHELLEXT_CM_SUBMENU = "Show In Submenu";
const char *SHELLEXT_CM_SUBMENUNAME = "Submenu Name";
// Wide sibling for the RegQueryValueExW/RegSetValueExW calls below. Same
// string: a registry value name is the same value whichever API form addresses it, so this
// is a spelling for the W calls and not a second setting.
static const wchar_t *SHELLEXT_CM_SUBMENUNAME_W = L"Submenu Name";

BOOL ShellExtConfigSubmenu = FALSE;
// wchar_t[SEC_SUBMENUNAME_MAX], matching the declaration in shexreg.h.
// This was `char ShellExtConfigSubmenuName[] = "..."` - a 20-BYTE object - while the header
// (widened by an earlier sweep) promised wchar_t[100], i.e. 200 bytes. dialogse.cpp writes
// through the header's view with ti.EditLine(..., SEC_SUBMENUNAME_MAX), so this was a 10x
// buffer overflow into whatever globals followed. Nothing diagnosed it: array extents are
// not checked across translation units.
wchar_t ShellExtConfigSubmenuName[SEC_SUBMENUNAME_MAX] = L"&Servant Salamander";

CShellExtConfigItem *ShellExtConfigFirst = NULL;
DWORD ShellExtConfigVersion = 0;


void 
SECClearItem(CShellExtConfigItem *item)
{
  item->Name[0] = 0;
  item->OneFile = TRUE;
  item->OneDirectory = TRUE;
  item->MoreFiles = TRUE;
  item->MoreDirectories = TRUE;
  item->LogicalAnd = FALSE;

  item->Next = NULL;
}

BOOL 
SECLoadRegistry()
{
  char key[MAX_PATH];
  HKEY hKey;
  LONG res;
  DWORD gettedType;
  DWORD bufferSize;
  DWORD version;
  BOOL reRead = TRUE;

  lstrcpy(key, SHELLEXT_ROOT_REG);
  lstrcat(key, "\\");
  lstrcat(key, SHELLEXT_CONTEXTMENU);

  // otevru klic Shell Extensions
  res = NOHANDLES(RegOpenKeyEx(HKEY_CURRENT_USER, key, 0, KEY_READ, &hKey));
  if (res != ERROR_SUCCESS) return FALSE;

  // zkontroluju verzi - je potreba nacist znovu data?
  bufferSize = sizeof(DWORD);
  res = SalRegQueryValueEx(hKey, SHELLEXT_VERSION, 0, &gettedType, (BYTE *)&version, &bufferSize);
  if (res == ERROR_SUCCESS)
  {
    // po prvni spusteni nacitam vzdy data
    if (ShellExtConfigVersion != 0 && ShellExtConfigVersion == version)
      reRead = FALSE;
    else
      ShellExtConfigVersion = version;
  }
  else
    ShellExtConfigVersion = 0;

  // je-li treba, nactu data
  if (reRead)
  {
    HKEY hItemKey;
    int i = 1;

//    MessageBox(NULL, "loading registry", "shellext.dll", MB_OK);
    // zrusim drzena data
    SECDeleteAllItems();

    // a nactu nova
    lstrcpy(key, "1");
    while (NOHANDLES(RegOpenKeyEx(hKey, key, 0, KEY_READ, &hItemKey)) == ERROR_SUCCESS) 
    {
      // ted vytvorim a nactu jednu polozku
      CShellExtConfigItem *item;
      if (SECAddItem(&item) != -1)
      {
        bufferSize = SEC_NAMEMAX;
        SalRegQueryValueEx(hItemKey, SHELLEXT_CM_NAME, 0, &gettedType, (BYTE *)item->Name, &bufferSize);
        bufferSize = sizeof(BOOL);
        SalRegQueryValueEx(hItemKey, SHELLEXT_CM_ONEFILE, 0, &gettedType, (BYTE *)&item->OneFile, &bufferSize);
        SalRegQueryValueEx(hItemKey, SHELLEXT_CM_ONEDIRECTORY, 0, &gettedType, (BYTE *)&item->OneDirectory, &bufferSize);
        SalRegQueryValueEx(hItemKey, SHELLEXT_CM_MOREFILES, 0, &gettedType, (BYTE *)&item->MoreFiles, &bufferSize);
        SalRegQueryValueEx(hItemKey, SHELLEXT_CM_MOREDIRECTORIES, 0, &gettedType, (BYTE *)&item->MoreDirectories, &bufferSize);
        SalRegQueryValueEx(hItemKey, SHELLEXT_CM_AND, 0, &gettedType, (BYTE *)&item->LogicalAnd, &bufferSize);
      }
      NOHANDLES(RegCloseKey(hItemKey));
      wsprintf(key, "%d", ++i);
    }

    // nactu jednotlive promenne konfigurace
    bufferSize = sizeof(BOOL);
    SalRegQueryValueEx(hKey, SHELLEXT_CM_SUBMENU, 0, &gettedType, (BYTE *)&ShellExtConfigSubmenu, &bufferSize);
    // RegQueryValueExW explicitly: the buffer is wide now, and the unsuffixed
    // form is the A one in this build (no UNICODE), which would fill a wchar_t array with
    // ANSI bytes. sizeof() is still right - the registry APIs count BYTES either way.
    bufferSize = sizeof(ShellExtConfigSubmenuName);
    RegQueryValueExW(hKey, SHELLEXT_CM_SUBMENUNAME_W, 0, &gettedType, (BYTE *)ShellExtConfigSubmenuName, &bufferSize);
  }

  NOHANDLES(RegCloseKey(hKey));
  return TRUE;
}

CShellExtConfigItem*
SECGetItem(int index)
{
  CShellExtConfigItem *iterator = ShellExtConfigFirst;

  int i = 0;
  while (iterator != NULL && i != index)
  {
    iterator = iterator->Next;
    i++;
  }

  return iterator;
}

BOOL
SECGetItemIndex(UINT cmd, int *index)
{
  CShellExtConfigItem *iterator = ShellExtConfigFirst;

  int i = 0;
  while (iterator != NULL && iterator->Cmd != cmd)
  {
    iterator = iterator->Next;
    i++;
  }

  if (iterator == NULL)
  {
    return FALSE;
  }
  
  *index = i;
  return TRUE;
}


// vytahne nazev polozky 
const wchar_t *
SECGetName(int index)
{
  CShellExtConfigItem *item = SECGetItem(index);

  if (item == NULL)
    return L"";

  return item->Name;
}

// vyhodi ze seznamu vsechny polozky
void 
SECDeleteAllItems()
{
  CShellExtConfigItem *iterator = ShellExtConfigFirst;

  while (iterator != NULL)
  {
    CShellExtConfigItem *tmp = iterator;
    iterator = iterator->Next;
    NOHANDLES(GlobalFree(tmp));
  }

  ShellExtConfigFirst = NULL;
}

int 
SECAddItem(CShellExtConfigItem **refItem)
{
  CShellExtConfigItem *item;
  CShellExtConfigItem *iterator = ShellExtConfigFirst;
  int index;

  // naalokuju polozku
  item = (CShellExtConfigItem*)NOHANDLES(GlobalAlloc(GMEM_FIXED, sizeof(CShellExtConfigItem)));

  if (refItem != NULL)
    *refItem = item;

  if (item == NULL)
    return -1;

  // inicializace promennych
  SECClearItem(item);

  // pripojim ji na konec seznamu
  index = 0;
  if (ShellExtConfigFirst == NULL)
  {
    ShellExtConfigFirst = item;
  }
  else
  {
    index++;
    while (iterator->Next != NULL)
    {
      iterator = iterator->Next;
      index++;
    }
    iterator->Next = item;
  }

  // vratim jeji index
  return index;
}
*/

#endif // ENABLE_SH_MENU_EXT

BOOL MyClearKey(HKEY key, REGSAM regView)
{
    wchar_t name[256]; // maximum registry key-name length, including terminator
    HKEY subKey;

    while (RegEnumKeyW(key, 0, name, ARRAYSIZE(name)) == ERROR_SUCCESS)
    {
        if (NOHANDLES(RegOpenKeyExW(key, name, 0, KEY_READ | KEY_WRITE | regView, &subKey)) == ERROR_SUCCESS)
        {
            BOOL ret = MyClearKey(subKey, regView);
            NOHANDLES(RegCloseKey(subKey));
            if (!ret || RegDeleteKeyW(key, name) != ERROR_SUCCESS)
                return FALSE;
        }
        else
            return FALSE;
    }

    DWORD size = ARRAYSIZE(name);
    while (RegEnumValueW(key, 0, name, &size, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
        if (RegDeleteValueW(key, name) != ERROR_SUCCESS)
            break;
        else
            size = ARRAYSIZE(name);

    return TRUE;
}

BOOL MyDeleteKey(HKEY key, const wchar_t* keyName, REGSAM regView)
{
    HKEY delKey;
    if (NOHANDLES(RegOpenKeyExW(key, keyName, 0, KEY_READ | KEY_WRITE | regView, &delKey)) == ERROR_SUCCESS)
    {
        MyClearKey(delKey, regView);
        NOHANDLES(RegCloseKey(delKey));
    }
    if (NOHANDLES(RegOpenKeyExW(key, NULL, 0, KEY_READ | KEY_WRITE | regView, &delKey)) == ERROR_SUCCESS)
    {
        BOOL ret = RegDeleteKeyW(delKey, keyName) == ERROR_SUCCESS;
        NOHANDLES(RegCloseKey(delKey));
        return ret;
    }
    else
        return FALSE;
}

//
// ============================================= pouze SalShExt
//

#ifndef INSIDE_SALAMANDER

HRESULT DllUnregisterServerBody(REGSAM regView)
{
    wchar_t key[1024]; // fixed registry paths below are short; avoid CRT stack probes in salext
    wchar_t shellExtIID[64];
    HKEY hKey;

    StringFromGUID2(&CLSID_ShellExtension, shellExtIID, ARRAYSIZE(shellExtIID));

    wsprintfW(key, L"CLSID\\%s", shellExtIID);
    MyDeleteKey(HKEY_CLASSES_ROOT, key, regView);
    wsprintfW(key, SAL_REG_FMT_SOFTWARE_CLASSES_CLSID_W, shellExtIID);
    MyDeleteKey(HKEY_CURRENT_USER, key, regView);
    wsprintfW(key, L"directory\\shellex\\CopyHookHandlers\\%s", SHEXREG_OPENSALAMANDER);
    MyDeleteKey(HKEY_CLASSES_ROOT, key, regView);
    wsprintfW(key, SAL_REG_FMT_SOFTWARE_CLASSES_DIRECTORY_COPY_HOOK_W, SHEXREG_OPENSALAMANDER);
    MyDeleteKey(HKEY_CURRENT_USER, key, regView);

#ifdef ENABLE_SH_MENU_EXT

    wsprintf(key, "*\\shellex\\ContextMenuHandlers\\%s", SHEXREG_OPENSALAMANDER);
    MyDeleteKey(HKEY_CLASSES_ROOT, key, regView);
    wsprintf(key, SAL_REG_FMT_SOFTWARE_CLASSES_STAR_CONTEXT_MENU_A, SHEXREG_OPENSALAMANDER);
    MyDeleteKey(HKEY_CURRENT_USER, key, regView);
    wsprintf(key, "Directory\\shellex\\ContextMenuHandlers\\%s", SHEXREG_OPENSALAMANDER);
    MyDeleteKey(HKEY_CLASSES_ROOT, key, regView);
    wsprintf(key, SAL_REG_FMT_SOFTWARE_CLASSES_DIRECTORY_CONTEXT_MENU_A, SHEXREG_OPENSALAMANDER);
    MyDeleteKey(HKEY_CURRENT_USER, key, regView);

#endif // ENABLE_SH_MENU_EXT

    lstrcpyW(key, SAL_REG_KEY_SHELL_EXT_APPROVED_W);
    if (NOHANDLES(RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_READ | KEY_WRITE | regView, &hKey)) == ERROR_SUCCESS)
    {
        RegDeleteValueW(hKey, shellExtIID);
        NOHANDLES(RegCloseKey(hKey));
    }
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    return DllUnregisterServerBody(0);
}

// slouzi pro odregistrovani shell extensiony "druhe platformy" (x86 pro x64 a naopak),
// pouziva remove.exe a setup.exe (pri upgradech odinstalovava predchozi verzi)
STDAPI DllUnregisterServerOtherPlatform()
{
#ifdef _WIN64
    return DllUnregisterServerBody(KEY_WOW64_32KEY);
#else  // _WIN64
    return DllUnregisterServerBody(KEY_WOW64_64KEY);
#endif // _WIN64
}

#endif // INSIDE_SALAMANDER

//
// ============================================= pouze Open Salamander
//

#ifdef INSIDE_SALAMANDER

BOOL MyCreateKey(HKEY hKey, const wchar_t* name, HKEY* createdKey, REGSAM regView)
{
    DWORD createType; // info jestli byl klic vytvoren nebo jen otevren
    LONG res = NOHANDLES(RegCreateKeyExW(hKey, name, 0, NULL, REG_OPTION_NON_VOLATILE,
                                         KEY_READ | KEY_WRITE | regView, NULL, createdKey,
                                         &createType));
    return res == ERROR_SUCCESS;
}

// nase varianta funkce RegQueryValueEx, narozdil od API varianty zajistuje
// pridani null-terminatoru pro typy REG_SZ, REG_MULTI_SZ a REG_EXPAND_SZ
// POZOR: pri zjistovani potrebne velikosti bufferu vraci o jeden nebo dva (dva
//        jen u REG_MULTI_SZ) znaky vic pro pripad, ze by string bylo potreba
//        zakoncit nulou/nulami
#ifdef ENABLE_SH_MENU_EXT
LONG SalRegQueryValueEx(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved,
                        LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData);
#endif // ENABLE_SH_MENU_EXT

static BOOL SetJoinedWideText(wchar_t** value, const wchar_t* first,
                              const wchar_t* second, const wchar_t* third)
{
    SIZE_T firstLength = first != NULL ? wcslen(first) : 0;
    SIZE_T secondLength = second != NULL ? wcslen(second) : 0;
    SIZE_T thirdLength = third != NULL ? wcslen(third) : 0;
    SIZE_T maxChars = ((SIZE_T)-1) / sizeof(wchar_t);
    SIZE_T length;
    wchar_t* joined;
    wchar_t* write;

    if (value == NULL || firstLength > maxChars - 1 ||
        secondLength > maxChars - firstLength - 1 ||
        thirdLength > maxChars - firstLength - secondLength - 1)
        return FALSE;
    length = firstLength + secondLength + thirdLength;
    joined = (wchar_t*)HeapAlloc(GetProcessHeap(), 0,
                                 (length + 1) * sizeof(wchar_t));
    if (joined == NULL)
        return FALSE;

    write = joined;
    if (firstLength != 0)
    {
        memcpy(write, first, firstLength * sizeof(wchar_t));
        write += firstLength;
    }
    if (secondLength != 0)
    {
        memcpy(write, second, secondLength * sizeof(wchar_t));
        write += secondLength;
    }
    if (thirdLength != 0)
    {
        memcpy(write, third, thirdLength * sizeof(wchar_t));
        write += thirdLength;
    }
    *write = L'\0';

    if (*value != NULL)
        HeapFree(GetProcessHeap(), 0, *value);
    *value = joined;
    return TRUE;
}

BOOL MyGetStringValueW(HKEY hKey, const wchar_t* name, wchar_t** value)
{
    DWORD type = 0;
    DWORD bytes = 0;
    LONG res;

    if (value == NULL)
        return FALSE;
    res = RegQueryValueExW(hKey, name, 0, &type, NULL, &bytes);
    if (res != ERROR_SUCCESS || type != REG_SZ)
        return FALSE;

    while (bytes <= MAXDWORD - sizeof(wchar_t))
    {
        wchar_t* buffer = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                              bytes + sizeof(wchar_t));
        DWORD readBytes = bytes;
        if (buffer == NULL)
            return FALSE;
        res = RegQueryValueExW(hKey, name, 0, &type, (BYTE*)buffer, &readBytes);
        if (res == ERROR_MORE_DATA)
        {
            HeapFree(GetProcessHeap(), 0, buffer);
            bytes = readBytes > bytes ? readBytes : bytes + sizeof(wchar_t);
            continue;
        }
        if (res != ERROR_SUCCESS || type != REG_SZ || readBytes > bytes ||
            readBytes % sizeof(wchar_t) != 0)
        {
            HeapFree(GetProcessHeap(), 0, buffer);
            return FALSE;
        }

        buffer[readBytes / sizeof(wchar_t)] = L'\0';
        if (*value != NULL)
            HeapFree(GetProcessHeap(), 0, *value);
        *value = buffer;
        return TRUE;
    }
    return FALSE;
}

BOOL CheckVersionOfDLL(const wchar_t* name)
{
    typedef HRESULT(STDAPICALLTYPE * FDllCheckVersion)(REFCLSID rclsid);
    BOOL ok = FALSE;
    HMODULE dll = LoadLibraryW(name);
    if (dll != NULL)
    {
        FDllCheckVersion DllCheckVersion = (FDllCheckVersion)GetProcAddress(dll, "DllCheckVersion"); // nas export
        if (DllCheckVersion != NULL && DllCheckVersion(&CLSID_ShellExtension) == S_OK)
            ok = TRUE; // verze souboru je OK
        FreeLibrary(dll);
    }
    return ok;
}

// Info pro UNINSTALL: (rutina pro uninstall je implementovana v DllUnregisterServer())
// - od verze 2.5 RC2 uz se nepouziva: - delete souboru z TEMPu (default value v HKEY_CLASSES_ROOT\CLSID\{C78B6131-F3EA-11D2-94A1-00E0292A01E3}\InProcServer32) (pocitat s tim, ze nemusi jit smazat hned - umet naplanovat po rebootu masiny)
// - od verze 3.0 B1: pocitat s tim, ze utils\salextx86.dll a salextx64.dll nemusi jit smazat hned - umet naplanovat po rebootu masiny
// - smazat HKEY_CLASSES_ROOT\CLSID\{C78B61??-F3EA-11D2-94A1-00E0292A01E3} (aktualni CLSID je v CLSID_ShellExtension)
// - smazat HKEY_CLASSES_ROOT\Directory\shellex\CopyHookHandlers\AltapSalamander?? (aktualni jmeno klice je v SHEXREG_OPENSALAMANDER)
// - smazat v klici HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved
//   hodnotu {C78B61??-F3EA-11D2-94A1-00E0292A01E3} (aktualni CLSID je v CLSID_ShellExtension)
// - je-li definovano makro ENABLE_SH_MENU_EXT:
//   - smazat HKEY_CLASSES_ROOT\*\shellex\ContextMenuHandlers\AltapSalamander?? (aktualni jmeno klice je v SHEXREG_OPENSALAMANDER)
//   - smazat HKEY_CLASSES_ROOT\Directory\shellex\ContextMenuHandlers\AltapSalamander?? (aktualni jmeno klice je v SHEXREG_OPENSALAMANDER)
// - vse co bylo receno o klici HKEY_CLASSES_ROOT je potreba zkusit smazat tez z klice
//   HKEY_CURRENT_USER\Software\Classes (vyuziva se pokud user nema prava pro zapis do
//   HKEY_CLASSES_ROOT)
BOOL SECRegisterToRegistry(const wchar_t* shellExtensionPath, BOOL doNotLoadDLL, REGSAM regView)
{
    HKEY hKey;
    wchar_t shellExtIID[64];
    wchar_t* key = NULL;
    wchar_t* shellExtPath = NULL;
    const wchar_t* str;
    BOOL registered;
    HKEY classesKey = NULL;

    if (!doNotLoadDLL && !CheckVersionOfDLL(shellExtensionPath))
        return FALSE;

    StringFromGUID2(&CLSID_ShellExtension, shellExtIID, ARRAYSIZE(shellExtIID));

    // zjistime jestli uz je nase shell extensiona registrovana, pripadne kde je jeji DLL a jestli je to spravna verze
    registered = FALSE;
    if (!SetJoinedWideText(&key, L"CLSID\\", shellExtIID, L"\\InProcServer32"))
        goto REG_CLEANUP;
    if (NOHANDLES(RegOpenKeyExW(HKEY_CLASSES_ROOT, key, 0, KEY_READ | regView, &hKey)) == ERROR_SUCCESS)
    {
        if (MyGetStringValueW(hKey, NULL /* default value */, &shellExtPath))
        {
            DWORD attrs = GetFileAttributesW(shellExtPath);
            if ((doNotLoadDLL && attrs != INVALID_FILE_ATTRIBUTES &&
                 (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) || // kdyz ho nemuzu loadit, aspon overim, ze existuje
                (!doNotLoadDLL && CheckVersionOfDLL(shellExtPath))) // jinak ho naloadim a zjistim od nej jeho verzi
            {
                registered = TRUE; // DLL je registrovane + je to spravna verze DLL
            }
        }
        NOHANDLES(RegCloseKey(hKey));
    }

    if (!registered)
    {
        classesKey = HKEY_CLASSES_ROOT;

    REG_TRY_AGAIN:

#ifdef ENABLE_SH_MENU_EXT

        if (!SetJoinedWideText(&key, L"*\\shellex\\ContextMenuHandlers\\",
                               SHEXREG_OPENSALAMANDER, NULL))
            goto REG_CLEANUP;
        if (MyCreateKey(classesKey, key, &hKey, regView))
        {
            RegSetValueExW(hKey, NULL, 0, REG_SZ, (BYTE*)shellExtIID,
                           (lstrlenW(shellExtIID) + 1) * sizeof(wchar_t));
            NOHANDLES(RegCloseKey(hKey));
        }
        // else;  // chybu otevirani klice pod HKEY_CLASSES_ROOT resime az dale (zde uz by bylo zbytecne)

        if (!SetJoinedWideText(&key, L"Directory\\shellex\\ContextMenuHandlers\\",
                               SHEXREG_OPENSALAMANDER, NULL))
            goto REG_CLEANUP;
        if (MyCreateKey(classesKey, key, &hKey, regView))
        {
            RegSetValueExW(hKey, NULL, 0, REG_SZ, (BYTE*)shellExtIID,
                           (lstrlenW(shellExtIID) + 1) * sizeof(wchar_t));
            NOHANDLES(RegCloseKey(hKey));
        }

#endif // ENABLE_SH_MENU_EXT

        if (!SetJoinedWideText(&key, L"CLSID\\", shellExtIID, NULL))
            goto REG_CLEANUP;
        if (MyCreateKey(classesKey, key, &hKey, regView))
        {
            wchar_t descrBuf[200];
#ifdef _WIN64
            wsprintfW(descrBuf, L"Shell Extension (%s) for Sally %hs", (regView & KEY_WOW64_32KEY) ? L"x86" : L"x64", VERSINFO_VERSION);
#else  // _WIN64
            wsprintfW(descrBuf, L"Shell Extension (%s) for Sally %hs", (regView & KEY_WOW64_64KEY) ? L"x64" : L"x86", VERSINFO_VERSION);
#endif // _WIN64
            RegSetValueExW(hKey, NULL, 0, REG_SZ, (BYTE*)descrBuf, (lstrlenW(descrBuf) + 1) * sizeof(wchar_t));
            NOHANDLES(RegCloseKey(hKey));
        }
        else
        {
            if (classesKey == HKEY_CLASSES_ROOT)
            {
                if (NOHANDLES(RegOpenKeyExW(HKEY_CURRENT_USER, SAL_REG_KEY_SOFTWARE_CLASSES_W, 0,
                                            KEY_READ | KEY_WRITE | regView, &classesKey)) == ERROR_SUCCESS)
                {
                    if (MyCreateKey(classesKey, L"CLSID", &hKey, regView))
                        NOHANDLES(RegCloseKey(hKey)); // klic "CLSID" ta tomto miste nemusi existovat, vytvorime si ho
                    goto REG_TRY_AGAIN;
                }
            }
            if (classesKey != HKEY_CLASSES_ROOT)
            {
                NOHANDLES(RegCloseKey(classesKey));
                classesKey = NULL;
            }
            goto REG_CLEANUP;
        }

        if (!SetJoinedWideText(&key, L"CLSID\\", shellExtIID, L"\\InProcServer32"))
            goto REG_CLEANUP;
        if (MyCreateKey(classesKey, key, &hKey, regView))
        {
            RegSetValueExW(hKey, NULL, 0, REG_SZ,
                           (BYTE*)shellExtensionPath, (lstrlenW(shellExtensionPath) + 1) * sizeof(wchar_t));
            str = L"Apartment";
            RegSetValueExW(hKey, SAL_REG_VALUE_THREADING_MODEL_W, 0, REG_SZ, (BYTE*)str, (lstrlenW(str) + 1) * sizeof(wchar_t));
            NOHANDLES(RegCloseKey(hKey));
        }

        if (!SetJoinedWideText(&key, L"directory\\shellex\\CopyHookHandlers\\",
                               SHEXREG_OPENSALAMANDER, NULL))
            goto REG_CLEANUP;
        if (MyCreateKey(classesKey, key, &hKey, regView))
        {
            RegSetValueExW(hKey, NULL, 0, REG_SZ,
                           (BYTE*)shellExtIID, (lstrlenW(shellExtIID) + 1) * sizeof(wchar_t));
            NOHANDLES(RegCloseKey(hKey));
        }

        // bez "As Admin" je tohle "dead code", aspon pod Vista+
        if (!SetJoinedWideText(&key, SAL_REG_KEY_SHELL_EXT_APPROVED_W, NULL, NULL))
            goto REG_CLEANUP;
        if (MyCreateKey(HKEY_LOCAL_MACHINE, key, &hKey, regView))
        {
            wchar_t descrBuf[200];
#ifdef _WIN64
            wsprintfW(descrBuf, L"Shell Extension (%s) for Sally %hs", (regView & KEY_WOW64_32KEY) ? L"x86" : L"x64", VERSINFO_VERSION);
#else  // _WIN64
            wsprintfW(descrBuf, L"Shell Extension (%s) for Sally %hs", (regView & KEY_WOW64_64KEY) ? L"x64" : L"x86", VERSINFO_VERSION);
#endif // _WIN64
            RegSetValueExW(hKey, shellExtIID, 0, REG_SZ, (BYTE*)descrBuf, (lstrlenW(descrBuf) + 1) * sizeof(wchar_t));
            NOHANDLES(RegCloseKey(hKey));
        }
        if (classesKey != HKEY_CLASSES_ROOT)
        {
            NOHANDLES(RegCloseKey(classesKey));
            classesKey = NULL;
        }

        // tohle by melo shell informovat o tom, ze je potreba reloadnout shell extensiony
        // (ovsem pro copy-hook to nefunguje; tak snad bude aspon pro menu)
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, 0, 0);
    }
    registered = TRUE;

REG_CLEANUP:
    if (classesKey != NULL && classesKey != HKEY_CLASSES_ROOT)
        NOHANDLES(RegCloseKey(classesKey));
    if (shellExtPath != NULL)
        HeapFree(GetProcessHeap(), 0, shellExtPath);
    if (key != NULL)
        HeapFree(GetProcessHeap(), 0, key);
    return registered;
}

#ifdef ENABLE_SH_MENU_EXT

BOOL SECSaveRegistry()
{
    HKEY hKey;
    char key[sizeof(SHELLEXT_ROOT_REG) + sizeof(SHELLEXT_CONTEXTMENU) + 16];
    DWORD bufferSize;
    DWORD gettedType;

    lstrcpy(key, SHELLEXT_ROOT_REG);
    lstrcat(key, "\\");
    lstrcat(key, SHELLEXT_CONTEXTMENU);

    if (MyCreateKey(HKEY_CURRENT_USER, key, &hKey))
    {
        DWORD version;
        int res;
        int i = 1;
        CShellExtConfigItem* iterator = ShellExtConfigFirst;

        // zvetsim cislo verze o jednicku
        ShellExtConfigVersion = 0;
        bufferSize = sizeof(DWORD);
        res = SalRegQueryValueEx(hKey, SHELLEXT_VERSION, 0, &gettedType, (BYTE*)&version, &bufferSize);
        if (res == ERROR_SUCCESS)
            ShellExtConfigVersion = version;
        ShellExtConfigVersion++;

        // podrezeme stavajici polozky v registry
        MyClearKey(hKey, 0);

        RegSetValueEx(hKey, SHELLEXT_VERSION, 0, REG_DWORD,
                      (BYTE*)&ShellExtConfigVersion, sizeof(DWORD));

        while (iterator != NULL)
        {
            HKEY hItemKey;
            wsprintf(key, "%d", i);

            if (MyCreateKey(hKey, key, &hItemKey))
            {
                RegSetValueEx(hItemKey, SHELLEXT_CM_NAME, 0, REG_SZ,
                              (CONST BYTE*)iterator->Name, strlen(iterator->Name) + 1);
                RegSetValueEx(hItemKey, SHELLEXT_CM_ONEFILE, 0, REG_DWORD, (BYTE*)&iterator->OneFile, sizeof(BOOL));
                RegSetValueEx(hItemKey, SHELLEXT_CM_ONEDIRECTORY, 0, REG_DWORD, (BYTE*)&iterator->OneDirectory, sizeof(BOOL));
                RegSetValueEx(hItemKey, SHELLEXT_CM_MOREFILES, 0, REG_DWORD, (BYTE*)&iterator->MoreFiles, sizeof(BOOL));
                RegSetValueEx(hItemKey, SHELLEXT_CM_MOREDIRECTORIES, 0, REG_DWORD, (BYTE*)&iterator->MoreDirectories, sizeof(BOOL));
                RegSetValueEx(hItemKey, SHELLEXT_CM_AND, 0, REG_DWORD, (BYTE*)&iterator->LogicalAnd, sizeof(BOOL));

                NOHANDLES(RegCloseKey(hItemKey));
            }
            else
            {
                NOHANDLES(RegCloseKey(hKey));
                return FALSE;
            }

            iterator = iterator->Next;
            i++;
        }

        RegSetValueEx(hKey, SHELLEXT_VERSION, 0, REG_DWORD,
                      (BYTE*)&ShellExtConfigVersion, sizeof(DWORD));

        // ulozim jednotlive promenne konfigurace
        RegSetValueEx(hKey, SHELLEXT_CM_SUBMENU, 0, REG_DWORD, (BYTE*)&ShellExtConfigSubmenu, sizeof(BOOL));
        // W form + a BYTE length computed from wcslen. REG_SZ is stored as
        // Unicode by Windows either way, so a value previously written through the A form
        // reads back identically here - no migration needed.
        RegSetValueExW(hKey, SHELLEXT_CM_SUBMENUNAME_W, 0, REG_SZ, (BYTE*)ShellExtConfigSubmenuName,
                       (DWORD)((wcslen(ShellExtConfigSubmenuName) + 1) * sizeof(wchar_t)));

        NOHANDLES(RegCloseKey(hKey));
        return TRUE;
    }
    return FALSE;
}

int SECGetCount()
{
    int count = 0;
    CShellExtConfigItem* iterator = ShellExtConfigFirst;

    while (iterator != NULL)
    {
        iterator = iterator->Next;
        count++;
    }

    return count;
}

BOOL SECDeleteItem(int index)
{
    CShellExtConfigItem* item;
    CShellExtConfigItem* next;

    item = SECGetItem(index);
    if (item == NULL)
        return FALSE;

    next = item->Next;
    if (index > 0)
        SECGetItem(index - 1)->Next = next;
    else
        ShellExtConfigFirst = next;

    NOHANDLES(GlobalFree(item));

    return TRUE;
}

// prohodi dve polozky v seznamu
BOOL SECSwapItems(int index1, int index2)
{
    CShellExtConfigItem* item1;
    CShellExtConfigItem* item2;
    CShellExtConfigItem* next1;
    CShellExtConfigItem* next2;
    CShellExtConfigItem tmp;

    item1 = SECGetItem(index1);
    item2 = SECGetItem(index2);

    if (item1 == NULL || item2 == NULL)
        return FALSE;

    next1 = item1->Next;
    next2 = item2->Next;

    tmp = *item1;
    *item1 = *item2;
    *item2 = tmp;

    item1->Next = next1;
    item2->Next = next2;
    return TRUE;
}

// nastavi nazev polozky
BOOL SECSetName(int index, const wchar_t* name)
{
    CShellExtConfigItem* item = SECGetItem(index);

    if (item == NULL)
        return FALSE;

    lstrcpyn(item->Name, name, SEC_NAMEMAX);

    return TRUE;
}

#endif // ENABLE_SH_MENU_EXT

#endif //INSIDE_SALAMANDER
