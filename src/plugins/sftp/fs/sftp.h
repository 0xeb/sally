// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Plugin-wide state owned by sftp.cpp.

extern HINSTANCE DLLInstance; // handle to this DLL
extern HINSTANCE HLanguage;   // module with the language-dependent resources (this DLL)

// Salamander general interface - valid from startup until the plugin shuts down
extern CSalamanderGeneralAbstract* SalamanderGeneral;
// interface that provides customized Windows controls used in Salamander
extern CSalamanderGUIAbstract* SalamanderGUI;

// FS name assigned by Salamander (it might not stay "sftp" because Salamander can adjust a clashing name)
extern std::wstring AssignedFSName;

// loads a string from the language-dependent resources
std::wstring LangStr(int resID);
