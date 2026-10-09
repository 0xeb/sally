// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace pictview
{

struct ViewerSourceFile
{
    std::wstring Path;
    int SourceIndex = -1;
};

enum class ViewerSourceNavigationMode
{
    Previous,
    Next,
    PreviousSelected,
    NextSelected,
    First,
    Last,
};

// The list the viewed file came from (a Sally panel or the Find window): stepping through its
// files and selecting them there. Each call reports sourceBusy when Sally cannot answer now.
struct ViewerSourceNavigation
{
    void* Context = nullptr;
    int SourceUID = -1;
    int CurrentIndex = -1;
    std::wstring CurrentPath;
    bool (*FindFile)(void* context,
                     int sourceUID,
                     int currentIndex,
                     const wchar_t* currentPath,
                     ViewerSourceNavigationMode mode,
                     ViewerSourceFile& file,
                     bool& noMoreFiles,
                     bool& sourceBusy) = nullptr;
    // Whether the file is selected in the source.
    bool (*QuerySelection)(void* context,
                           int sourceUID,
                           int currentIndex,
                           const wchar_t* currentPath,
                           bool& selected,
                           bool& sourceBusy) = nullptr;
    // Selects the file in the source when it is not, unselects it when it is.
    bool (*ToggleSelection)(void* context,
                            int sourceUID,
                            int currentIndex,
                            const wchar_t* currentPath,
                            bool& selected,
                            bool& sourceBusy) = nullptr;
    bool (*FocusCurrentFile)(void* context,
                             const wchar_t* currentPath,
                             bool& sourceBusy) = nullptr;
};

} // namespace pictview
