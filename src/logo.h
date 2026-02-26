// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

//*****************************************************************************
//
// Splash Screen
//

BOOL SplashScreenOpen();                           // opens splash screen window
void SplashScreenCloseIfExist();                   // if window exists, closes it
BOOL ExistSplashScreen();                          // detects existence of splash screen window
HWND GetSplashScreenHandle();                      // returns handle of splash screen, or NULL if it doesn't exist
void IfExistSetSplashScreenText(const char* text); // if it exists, sets text that will be displayed immediately
