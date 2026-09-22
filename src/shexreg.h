// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "shexreg_ipc_names.h"
#include "shexreg_ipc_protocol.h"

#ifdef __cplusplus
extern "C"
{
#endif

#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression

    // #define ENABLE_SH_MENU_EXT     // define this macro to include the context menu handler (not just the copy hook)

    //
    // Shell Extensions Configuration
    //

    //
    // ============================================= shared section
    //

    //
    // included in Open Salamander for configuration and inter-process communication
    // and simultaneously compiled into SHELLEXT.DLL for presentation and coordination
    //

    //
    // The class ID of this Shell extension class.
    //
    // salshext.dll (Servant Salamander 2.5 beta 1) class id:        c78b6130-f3ea-11d2-94a1-00e0292a01e3
    // salexten.dll (Servant Salamander 2.5 beta 2 through RC1) class id: c78b6131-f3ea-11d2-94a1-00e0292a01e3 (copied to the TEMP directory and shared between multiple Salamander builds)
    // salamext.dll (Servant Salamander 2.5 RC2) class id:           c78b6132-f3ea-11d2-94a1-00e0292a01e3 (first version kept in the installation; each Salamander release ships with its own shell extension)
    // salamext.dll (Servant Salamander 2.5 RC3) class id:           c78b6133-f3ea-11d2-94a1-00e0292a01e3
    // salamext.dll (Altap Salamander 2.5 RC3) class id:             c78b6134-f3ea-11d2-94a1-00e0292a01e3
    // salamext.dll (Altap Salamander 2.5) class id:                 c78b6135-f3ea-11d2-94a1-00e0292a01e3
    // salamext.dll (Altap Salamander 2.51) class id:                c78b6136-f3ea-11d2-94a1-00e0292a01e3
    // salamext.dll (Altap Salamander 2.52 beta 1) class id:         c78b6137-f3ea-11d2-94a1-00e0292a01e3
    // salamext.dll (Altap Salamander 2.52 beta 1) class id:         c78b6138-f3ea-11d2-94a1-00e0292a01e3 (mutex-creation changes broke compatibility with older versions)
    // salamext.dll (Altap Salamander 2.52 beta 1) class id:         c78b6139-f3ea-11d2-94a1-00e0292a01e3 (creating mutexes with restricted permissions prevented older Salamanders—for example 2.51—from opening them, so the mutex, shared-memory, etc. names changed)
    // salamext.dll (Altap Salamander 2.52 beta 2) class id:         c78b613a-f3ea-11d2-94a1-00e0292a01e3
    // salamext.dll (Altap Salamander 2.52) class id:                c78b613b-f3ea-11d2-94a1-00e0292a01e3
    // salamext.dll (Altap Salamander 2.53 beta 1) class id:         c78b613c-f3ea-11d2-94a1-00e0292a01e3
    // salamext.dll (Altap Salamander 2.53) class id:                c78b613d-f3ea-11d2-94a1-00e0292a01e3 (unused—we ultimately released 2.53 beta 2 instead)
    // salamext.dll (Altap Salamander 2.53 beta 2) class id:         c78b613e-f3ea-11d2-94a1-00e0292a01e3
    // salamext.dll (Altap Salamander 2.53) class id:                c78b613f-f3ea-11d2-94a1-00e0292a01e3
    // salamext.dll (Altap Salamander 2.54) class id:                c78b6140-f3ea-11d2-94a1-00e0292a01e3
    // salamext.dll (Altap Salamander 2.55 beta 1) class id:         c78b6141-f3ea-11d2-94a1-00e0292a01e3
    // salextx86.dll + salextx64.dll (Salamander 3.0 beta 1) class id: c78b6142-f3ea-11d2-94a1-00e0292a01e3 (first build that ships x86 and x64 versions together)
    // salextx86.dll + salextx64.dll (Salamander 3.0 beta 2) class id: c78b6143-f3ea-11d2-94a1-00e0292a01e3
    // salextx86.dll + salextx64.dll (Salamander 3.0 beta 3) class id: c78b6144-f3ea-11d2-94a1-00e0292a01e3
    // salextx86.dll + salextx64.dll (Salamander 3.0 beta 4) class id: c78b6145-f3ea-11d2-94a1-00e0292a01e3
    // salextx86.dll + salextx64.dll (Salamander 3.0) class id:        c78b6146-f3ea-11d2-94a1-00e0292a01e3
    // salextx86.dll + salextx64.dll (Salamander 3.01) class id:       c78b6147-f3ea-11d2-94a1-00e0292a01e3
    // salextx86.dll + salextx64.dll (Salamander 3.02) class id:       c78b6148-f3ea-11d2-94a1-00e0292a01e3
    // salextx86.dll + salextx64.dll (Salamander 3.03) class id:       c78b6149-f3ea-11d2-94a1-00e0292a01e3
    // salextx86.dll + salextx64.dll (Salamander 3.04) class id:       c78b614a-f3ea-11d2-94a1-00e0292a01e3
    // salextx86.dll + salextx64.dll (Salamander 3.05) class id:       c78b614b-f3ea-11d2-94a1-00e0292a01e3
    // salextx86.dll + salextx64.dll (Salamander 3.06) class id:       c78b614c-f3ea-11d2-94a1-00e0292a01e3
    // salextx86.dll + salextx64.dll (Salamander 3.07) class id:       c78b614d-f3ea-11d2-94a1-00e0292a01e3
    // salextx86.dll + salextx64.dll (Salamander 3.08) class id:       c78b614e-f3ea-11d2-94a1-00e0292a01e3
    // salextx86.dll + salextx64.dll (Salamander 4.0) class id:        c78b614f-f3ea-11d2-94a1-00e0292a01e3
    // salextx64.dll (Sally 1.0) class id:                           c78b6150-f3ea-11d2-94a1-00e0292a01e3
    //

    DEFINE_GUID(CLSID_ShellExtension, 0xc78b6150L, 0xf3ea, 0x11d2, 0x94, 0xa1, 0x00, 0xe0, 0x29, 0x2a, 0x01, 0xe3);

    //
    // suffix appended to keep the shell-extension registry name unique (SHEXREG_OPENSALAMANDER)
    //
    // Servant Salamander 2.5 RC2                - 25RC2
    // Servant Salamander 2.5 RC3                - 25RC3
    // Altap Salamander 2.5 RC3                  - 25RC3
    // Altap Salamander 2.5                      - 25
    // Altap Salamander 2.51                     - 251
    // Altap Salamander 2.52 beta 1              - 252B1
    // Altap Salamander 2.52 beta 1              - 252B1a  (mutex creation/open adjustments caused incompatibility with older builds)
    // Altap Salamander 2.52 beta 1              - 252B1b  (mutexes with restricted permissions prevented older Salamanders, e.g. 2.51, from opening them, so the mutex/shared-memory names changed)
    // Altap Salamander 2.52 beta 2              - 252B2
    // Altap Salamander 2.52                     - 252
    // Altap Salamander 2.53 beta 1              - 253B1
    // Altap Salamander 2.53                     - 253     (unused—we ultimately shipped 2.53 beta 2)
    // Altap Salamander 2.53 beta 2              - 253B2
    // Altap Salamander 2.53                     - 253R
    // Altap Salamander 2.54                     - 254
    // Altap Salamander 2.55 beta 1              - 255B1   (unused—we eventually released 3.0 beta 1 instead)
    // Altap Salamander 3.0 beta 1               - 300B1
    // Altap Salamander 3.0 beta 2               - 300B2
    // Altap Salamander 3.0 beta 3               - 300B3
    // Altap Salamander 3.0 beta 4               - 300B4
    // Altap Salamander 3.0                      - 300B5   (left as "3.0 beta 5" by mistake; we released 3.0)
    // Altap Salamander 3.1 beta 1 (unreleased)  - 310B1
    // Altap Salamander 3.01                     - 301
    // Altap Salamander 3.1 beta 1 (unreleased)  - 310B1_2 (second attempt to ship "3.1 beta 1")
    // Altap Salamander 3.02                     - 302
    // Altap Salamander 3.1 beta 1 (unreleased)  - 310B1_3 (third attempt to ship "3.1 beta 1")
    // Altap Salamander 3.03                     - 303
    // Altap Salamander 3.1 beta 1 (unreleased)  - 310B1_4 (fourth attempt to ship "3.1 beta 1")
    // Altap Salamander 3.04                     - 304
    // Altap Salamander 3.1 beta 1               - 310B1_5 (fifth attempt to ship "3.1 beta 1")
    // Altap Salamander 3.05                     - 305
    // Altap Salamander 3.1 beta 1               - 310B1_6 (sixth attempt to ship "3.1 beta 1")
    // Altap Salamander 3.06                     - 306
    // Altap Salamander 3.1 beta 1               - 310B1_7 (seventh attempt to ship "3.1 beta 1")
    // Altap Salamander 3.07                     - 307
    // Altap Salamander 4.0 beta 1               - 400B1
    // Altap Salamander 3.08                     - 308
    // Altap Salamander 4.0 beta 1               - 400B1_2 (second attempt to ship "4.0 beta 1")
    // Altap Salamander 4.0                      - 400
    // Open Salamander 5.0                       - 500
    // Sally 1.0                                 - S100

#define SALSHEXT_SHAREDNAMESAPPENDIX "S100"

#ifdef ENABLE_SH_MENU_EXT

#define SEC_NAMEMAX 400

#define SEC_SUBMENUNAME_MAX 100

    typedef struct CShellExtConfigItem CShellExtConfigItem;

    // base item representing a single entry in the context menu
    // these entries are stored in a singly linked list
    struct CShellExtConfigItem
    {
        wchar_t Name[SEC_NAMEMAX]; // caption shown for the item in the context menu
        // conditions that determine whether the item appears in the context menu
        BOOL OneFile;
        BOOL OneDirectory;
        BOOL MoreFiles;
        BOOL MoreDirectories;
        BOOL LogicalAnd;

        // runtime-only data
        CShellExtConfigItem* Next; // next item; NULL marks the end of the list
        UINT Cmd;                  // used to look the item up during InvokeCommand
    };

    // resets the item
    void SECClearItem(CShellExtConfigItem* item);

    // head of the list
    extern CShellExtConfigItem* ShellExtConfigFirst;

    // configuration flags
    extern BOOL ShellExtConfigSubmenu;
    extern wchar_t ShellExtConfigSubmenuName[SEC_SUBMENUNAME_MAX];

    // current configuration version
    // increase this number by one when saving a new revision
    // when loading, verify the version and reload the registry if it differs from the one in use
    extern DWORD ShellExtConfigVersion;

    // returns the item at the specified index
    CShellExtConfigItem* SECGetItem(int index);

    // if an item with matching Cmd is found, fills index and returns TRUE; otherwise returns FALSE
    BOOL SECGetItemIndex(UINT cmd, int* index);

    BOOL SECLoadRegistry();

    // retrieves the item name
    const wchar_t* SECGetName(int index);

    // removes all items from the list
    void SECDeleteAllItems();

    // returns the index of the created item or -1 on error
    // refItem receives a pointer to the inserted item and may be NULL
    int SECAddItem(CShellExtConfigItem** refItem);

#endif // ENABLE_SH_MENU_EXT

    //
    // Data and constants for communication between Salamander and SalamExt
    //

// IMPORTANT: do not change these constants; older versions rely on them
#define WM_USER_SALSHEXT_PASTE WM_APP + 139      // [postMsgIndex, 0] - SalamExt requests execution of the Paste command
#define WM_USER_SALSHEXT_TRYRELDATA WM_APP + 143 // [0, 0] - SalamExt reports that paste data were unlocked (see CSalShExtSharedMem::BlockPasteDataRelease); if nothing else protects the data we let them be released
#define WM_USER_SALSHEXT_CAPTUREPAYLOAD WM_APP + 144 // [0, 0] - copy hook asks Sally to capture a v7 response payload while its exact-size mapping is alive

#define SALSHEXT_NONE 0
#define SALSHEXT_COPY 1
#define SALSHEXT_MOVE 2

// v7's fixed cross-bitness control record is declared in shexreg_ipc_protocol.h. Semantic
// UTF-16 strings live only in exact-size payload mappings referenced by numeric metadata.

    //
    // ============================================= Altap Salamander only
    //

#ifdef INSIDE_SALAMANDER

    // writes the registry entries required by the library
    // parameters: path to the library, whether to skip loading the DLL when verifying its version,
    // and the registry view (0, 32-bit, or 64-bit) that should be updated
    BOOL SECRegisterToRegistry(const wchar_t* shellExtensionPath, BOOL doNotLoadDLL, REGSAM regView);

#ifdef __cplusplus
} // extern "C"
#endif

#ifdef ENABLE_SH_MENU_EXT

// saves the configuration to the registry
BOOL SECSaveRegistry();

// returns the number of items in the list
int SECGetCount();

// removes an item from the list
BOOL SECDeleteItem(int index);

// swaps two items in the list
BOOL SECSwapItems(int index1, int index2);

// sets an item name
BOOL SECSetName(int index, const wchar_t* name);

#endif // ENABLE_SH_MENU_EXT

#endif //INSIDE_SALAMANDER
