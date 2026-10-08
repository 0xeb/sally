// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <tchar.h>

#include <mutex>
#include <ostream>
#include <string>
#include <vector>

#include "versinfo.rh2" // before spl_vers.h, which builds the version strings from it
#include "spl_com.h"
#include "spl_base.h"
#include "spl_gen.h"
#include "spl_menu.h"
#include "spl_vers.h"
#include "spl_thum.h"
#include "spl_view.h"
#include "spl_gui.h"
#include "dbg.h"
#include "arraylt.h"
#include "winliblt.h"

#include "adapter_actions.h"
#include "configuration.h"
#include "configuration_dialog.h"
#include "engine/wic_engine.h"
#include "pictview.rh2"
#include "lang/lang.rh"
#include "thumbnail.h"
#include "viewer.h"
#include "plugin_services.h"
#include "codec_masks.h"
#include "thumbnail_update.h"


namespace
{

const wchar_t* PluginNameEN = L"PictView"; // do not translate
const wchar_t* WicImageMasks = L"*.bmp;*.dib;*.gif;*.ico;*.jpg;*.jpeg;*.jpe;*.jfif;*.png;*.tif;*.tiff;*.wdp;*.jxr;*.dds";

// Extensions the closed-engine PictView registered that Windows Imaging Component cannot decode.
// Configurations from that PictView (version 22 and older) drop them on upgrade.
const wchar_t* const RetiredViewerMasks[] = {
    L"*.psp*", L"*.dtx", L"*.nef", L"*.crw", L"*.eps", L"*.ept", L"*.ai", L"*.raf", L"*.mov", L"*.hpi",
    L"*.pntg", L"*.thumb", L"*.wbmp", L"*.ani", L"*.clk", L"*.mbm", L"*.thm", L"*.zno", L"*.mng",
    L"*.st", L"*.cals", L"*.itiff", L"*.macp", L"*.mpnt", L"*.paint", L"*.pict", L"*.2bp",
    L"*.stw", L"*.sun", L"*.tga", L"*.udi", L"*.web", L"*.wpg", L"*.xar", L"*.zbr", L"*.zmf", L"*.bw",
    L"*.psd", L"*.pyx", L"*.qfx", L"*.ras", L"*.rgb", L"*.rle", L"*.sam", L"*.scx", L"*.sep", L"*.sgi", L"*.ska",
    L"*.pat", L"*.pbm", L"*.pc2", L"*.pcd", L"*.pct", L"*.pcx", L"*.pgm", L"*.pic", L"*.pnm", L"*.ppm",
    L"*.jff", L"*.jif", L"*.jmx", L"*.lbm", L"*.mac", L"*.mil", L"*.msp", L"*.ofx", L"*.pan",
    L"*.flc", L"*.fli", L"*.gem", L"*.ham", L"*.hmr", L"*.hrz", L"*.icn", L"*.iff", L"*.img",
    L"*.cdt", L"*.cel", L"*.clp", L"*.cit", L"*.cmx", L"*.cot", L"*.cpt", L"*.cur", L"*.cut", L"*.dcx",
    L"*.82i", L"*.83i", L"*.85i", L"*.86i", L"*.89i", L"*.92i", L"*.awd", L"*.bmi", L"*.cal", L"*.cdr",
    L"*.arw", L"*.blp", L"*.cr2", L"*.dng", L"*.orf", L"*.pef",
};
constexpr int InternalFocusSourceCommand = 910001;
constexpr int PluginMenuPasteClipboardCommand = 910002;
constexpr int PluginMenuScreenCaptureCommand = 910003;
constexpr int PluginMenuRefreshThumbnailsCommand = 910004;
CSalamanderGeneralAbstract* SalamanderGeneral = nullptr;
HINSTANCE DllInstance = nullptr;
std::mutex PendingFocusMutex;
std::wstring PendingFocusPath;

HINSTANCE PluginInstance()
{
    if (DllInstance != nullptr)
        return DllInstance;
    return GetModuleHandleW(nullptr);
}

std::wstring LoadPluginString(int id)
{
    if (SalamanderGeneral != nullptr)
        return SPLLoadStrOwned(SalamanderGeneral, PluginInstance(), id);

    // No host attached: the module's own (English) string table.
    const wchar_t* text = nullptr;
    const int length = LoadStringW(PluginInstance(), static_cast<UINT>(id), reinterpret_cast<LPWSTR>(&text), 0);
    return length > 0 && text != nullptr ? std::wstring(text, static_cast<size_t>(length)) : std::wstring();
}

bool ClipboardContainsSupportedImage()
{
    if (pictview::Services().ClipboardHasImage)
        return pictview::Services().ClipboardHasImage();

    return IsClipboardFormatAvailable(CF_DIBV5) ||
           IsClipboardFormatAvailable(CF_DIB) ||
           IsClipboardFormatAvailable(CF_BITMAP);
}

bool RefreshSourcePanelThumbnails()
{
    if (pictview::Services().RefreshPanelPath)
    {
        pictview::Services().RefreshPanelPath(PANEL_SOURCE, TRUE, FALSE);
        return true;
    }

    if (SalamanderGeneral == nullptr)
        return false;

    SalamanderGeneral->RefreshPanelPath(PANEL_SOURCE, TRUE);
    return true;
}

// ------------------------------------------------------------------------------------------
// Update Thumbnail

std::vector<std::wstring> SourcePanelFiles()
{
    if (pictview::Services().SourcePanelFiles)
        return pictview::Services().SourcePanelFiles();

    std::vector<std::wstring> files;
    std::wstring panelPath;
    int type = 0;
    if (SalamanderGeneral == nullptr || !SPLGetPanelPathOwned(SalamanderGeneral, PANEL_SOURCE, panelPath, &type) ||
        type != PATH_TYPE_WINDOWS)
        return files; // only Windows paths (local or network) hold files to rewrite
    auto add = [&files, &panelPath](const CFileData* file) {
        std::wstring path = panelPath;
        SPLSalPathAppendOwned(path, file->Name);
        files.push_back(path);
    };
    int selectedFiles = 0;
    int selectedDirectories = 0;
    SalamanderGeneral->GetPanelSelection(PANEL_SOURCE, &selectedFiles, &selectedDirectories);
    BOOL isDirectory = FALSE;
    if (selectedFiles == 0 && selectedDirectories == 0)
    {
        const CFileData* focused = SalamanderGeneral->GetPanelFocusedItem(PANEL_SOURCE, &isDirectory);
        if (focused != nullptr && !isDirectory)
            add(focused);
        return files;
    }
    int index = 0;
    while (const CFileData* file = SalamanderGeneral->GetPanelSelectedItem(PANEL_SOURCE, &index, &isDirectory))
    {
        if (!isDirectory)
            add(file);
    }
    return files;
}

bool ConfirmThumbnailUpdate(HWND parent, bool& dontAskAgain)
{
    dontAskAgain = false;
    if (pictview::Services().ConfirmThumbnailUpdate)
        return pictview::Services().ConfirmThumbnailUpdate(parent, dontAskAgain);
    if (SalamanderGeneral == nullptr)
        return false;
    const std::wstring text = LoadPluginString(IDS_REGENERATE_THUMB_WARNING);
    const std::wstring caption = LoadPluginString(IDS_PLUGINNAME);
    const std::wstring checkText = LoadPluginString(IDS_DONT_SHOW_AGAIN_UTH);
    BOOL checked = FALSE;
    MSGBOXEX_PARAMS params = {};
    params.HParent = parent;
    params.Text = text.c_str();
    params.Caption = caption.c_str();
    params.Flags = MSGBOXEX_YESNO | MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT;
    params.CheckBoxText = checkText.c_str();
    params.CheckBoxValue = &checked;
    const int answer = SalamanderGeneral->SalMessageBoxEx(&params);
    dontAskAgain = checked != FALSE;
    return answer == IDYES;
}

std::wstring FileNameOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// The old messages carry the decoder's text as %hs; it is UTF-16 here.
std::wstring OpenFailedText(const std::wstring& detail)
{
    std::wstring format = LoadPluginString(IDS_ERROR_OPENING_CONTINUE);
    const size_t code = format.find(L"(%d) ");
    if (code != std::wstring::npos)
        format.erase(code, 5); // the old engine's error number; the detail has the HRESULT
    const size_t text = format.find(L"%hs");
    if (text != std::wstring::npos)
        format.replace(text, 3, detail);
    return format;
}

pictview::ThumbnailUpdateAnswer AnswerFromDialog(int result)
{
    switch (result)
    {
    case DIALOG_YES:
        return pictview::ThumbnailUpdateAnswer::Yes;
    case DIALOG_ALL:
        return pictview::ThumbnailUpdateAnswer::All;
    case DIALOG_SKIPALL:
        return pictview::ThumbnailUpdateAnswer::SkipAll;
    case DIALOG_CANCEL:
        return pictview::ThumbnailUpdateAnswer::Cancel;
    default: // DIALOG_SKIP, DIALOG_NO
        return pictview::ThumbnailUpdateAnswer::Skip;
    }
}

pictview::ThumbnailUpdateAnswer AskThumbnailUpdate(HWND parent, pictview::ThumbnailUpdateQuestion question, const std::wstring& path,
                                                   const std::wstring& detail, size_t fileCount)
{
    if (pictview::Services().AskThumbnailUpdate)
        return pictview::Services().AskThumbnailUpdate(question, path, detail, fileCount);
    if (SalamanderGeneral == nullptr)
        return pictview::ThumbnailUpdateAnswer::Cancel;

    const std::wstring name = FileNameOf(path);
    const std::wstring title = LoadPluginString(IDS_ERRORTITLE);
    using Question = pictview::ThumbnailUpdateQuestion;
    switch (question)
    {
    case Question::OpenFailed:
        return AnswerFromDialog(SalamanderGeneral->DialogError(parent, BUTTONS_SKIPCANCEL, name.c_str(), OpenFailedText(detail).c_str(),
                                                               title.c_str()));
    case Question::NotJpeg:
        return AnswerFromDialog(SalamanderGeneral->DialogError(parent, BUTTONS_SKIPCANCEL, name.c_str(),
                                                               LoadPluginString(IDS_NOT_JPEG_FILE).c_str(), title.c_str()));
    case Question::NoExif:
        return AnswerFromDialog(SalamanderGeneral->DialogQuestion(parent, fileCount > 1 ? BUTTONS_YESALLSKIPCANCEL : BUTTONS_YESNOCANCEL,
                                                                  name.c_str(), LoadPluginString(IDS_NOT_EXIF_CREATE_JFXX).c_str(),
                                                                  title.c_str()));
    case Question::ReadOnly:
        return AnswerFromDialog(SalamanderGeneral->DialogQuestion(parent, BUTTONS_YESALLSKIPCANCEL, name.c_str(),
                                                                  LoadPluginString(IDS_READ_ONLY_MODIFY).c_str(), title.c_str()));
    case Question::WriteFailed:
    default:
    {
        std::wstring text = LoadPluginString(IDS_REGENERATE_THUMB_WRITEFILE);
        const size_t placeholder = text.find(L"%s");
        if (placeholder != std::wstring::npos)
            text.replace(placeholder, 2, name);
        if (!detail.empty())
            text += L"\n\n" + detail;
        return AnswerFromDialog(SalamanderGeneral->DialogError(parent, BUTTONS_SKIPCANCEL, name.c_str(), text.c_str(), title.c_str()));
    }
    }
}

void ShowPluginMessage(const std::wstring& text)
{
    if (pictview::Services().ShowMessage)
    {
        pictview::Services().ShowMessage(text);
        return;
    }
    if (SalamanderGeneral != nullptr)
        SalamanderGeneral->ShowMessageBox(text.c_str(), LoadPluginString(IDS_PLUGINNAME).c_str(), MSGBOX_INFO);
}

// "Only 2 of 5 files were processed. The other files were not modified."
std::wstring ThumbnailSummaryText(size_t updated, size_t total)
{
    const std::wstring format = LoadPluginString(IDS_N_OF_N_FILES_PROCESSED);
    std::wstring text;
    if (SalamanderGeneral != nullptr)
    {
        CQuadWord counts[2] = {CQuadWord(static_cast<DWORD>(updated), 0), CQuadWord(static_cast<DWORD>(total - updated), 0)};
        text = SPLExpandPluralStringOwned(SalamanderGeneral, format.c_str(), 2, counts);
    }
    if (text.empty())
        text = pictview::ExpandPluralText(format.c_str(), updated);
    for (const size_t value : {updated, total})
    {
        const size_t placeholder = text.find(L"%d");
        if (placeholder != std::wstring::npos)
            text.replace(placeholder, 2, std::to_wstring(value));
    }
    return text;
}

void NotifyFolderChanged(const std::wstring& directory)
{
    if (pictview::Services().PathChanged)
        pictview::Services().PathChanged(directory);
    else if (SalamanderGeneral != nullptr)
        SalamanderGeneral->PostChangeOnPathNotification(directory.c_str(), FALSE);
}

void UpdateThumbnails(CSalamanderForOperationsAbstract* salamander, HWND parent)
{
    pictview::ViewerPreferences preferences = pictview::GetViewerPreferences();
    if (preferences.ConfirmThumbnailUpdate)
    {
        bool dontAskAgain = false;
        const bool proceed = ConfirmThumbnailUpdate(parent, dontAskAgain);
        if (dontAskAgain)
        {
            preferences.ConfirmThumbnailUpdate = false;
            pictview::SetViewerPreferences(preferences);
        }
        if (!proceed)
            return;
    }

    const std::vector<std::wstring> files = SourcePanelFiles();
    if (files.empty())
    {
        ShowPluginMessage(LoadPluginString(IDS_NO_FILES_FOUND));
        return;
    }

    if (salamander != nullptr)
    {
        salamander->OpenProgressDialog(LoadPluginString(IDS_REGENERATE_THUMBNAIL_TITLE).c_str(), FALSE, parent, FALSE);
        salamander->ProgressSetTotalSize(CQuadWord(static_cast<DWORD>(files.size()), 0), CQuadWord(-1, -1));
    }
    const HWND questionParent = salamander != nullptr ? salamander->ProgressGetHWND() : parent;
    pictview::ThumbnailUpdateHost host;
    host.Progress = [salamander](size_t index, size_t, const std::wstring& path) {
        if (salamander == nullptr)
            return true;
        salamander->ProgressDialogAddText(FileNameOf(path).c_str(), TRUE);
        return salamander->ProgressSetSize(CQuadWord(static_cast<DWORD>(index), 0), CQuadWord(-1, -1), TRUE) != FALSE;
    };
    host.Ask = [questionParent, count = files.size()](pictview::ThumbnailUpdateQuestion question, const std::wstring& path,
                                                      const std::wstring& detail) {
        return AskThumbnailUpdate(questionParent, question, path, detail, count);
    };
    host.PathChanged = NotifyFolderChanged;
    const pictview::ThumbnailUpdateSummary summary = pictview::UpdateJpegThumbnails(files, host);
    if (salamander != nullptr)
        salamander->CloseProgressDialog();

    if (summary.Updated < summary.Total)
        ShowPluginMessage(summary.Updated == 0 ? LoadPluginString(IDS_NO_FILES_FOUND) : ThumbnailSummaryText(summary.Updated, summary.Total));
}

void InstallPluginMenuIcon(CSalamanderConnectAbstract* salamander)
{
    HBITMAP hBmp = static_cast<HBITMAP>(LoadImageW(PluginInstance(),
                                                   MAKEINTRESOURCEW(IDB_PICTVIEW),
                                                   IMAGE_BITMAP,
                                                   16,
                                                   16,
                                                   LR_DEFAULTCOLOR));
    if (hBmp == nullptr)
        return;

    salamander->SetBitmapWithIcons(hBmp);
    DeleteObject(hBmp);
    salamander->SetPluginIcon(0);
    salamander->SetPluginMenuAndToolbarIcon(0);
}

void ViewerPlacementFromParent(HWND parent, int& left, int& top, int& width, int& height, UINT& showCmd)
{
    left = CW_USEDEFAULT;
    top = CW_USEDEFAULT;
    width = CW_USEDEFAULT;
    height = CW_USEDEFAULT;
    showCmd = SW_SHOWNORMAL;

    if (parent == nullptr || !IsWindow(parent))
        return;

    RECT rect = {};
    if (GetWindowRect(parent, &rect))
    {
        left = rect.left;
        top = rect.top;
        const int parentWidth = rect.right - rect.left;
        const int parentHeight = rect.bottom - rect.top;
        if (parentWidth > 0)
            width = parentWidth;
        if (parentHeight > 0)
            height = parentHeight;
    }

    if (IsZoomed(parent))
        showCmd = SW_MAXIMIZE;
}

bool SelectWholeNameOnRenameFromHost()
{
    if (pictview::Services().SelectWholeNameOnRename)
        return pictview::Services().SelectWholeNameOnRename();

    BOOL selectWholeName = TRUE;
    if (SalamanderGeneral != nullptr)
        SalamanderGeneral->GetConfigParameter(SALCFG_SELECTWHOLENAME, &selectWholeName, sizeof(selectWholeName), nullptr);
    return selectWholeName != FALSE;
}

bool AlwaysOnTopFromHost()
{
    if (pictview::Services().AlwaysOnTopForViewer)
        return pictview::Services().AlwaysOnTopForViewer();

    BOOL alwaysOnTop = FALSE;
    if (SalamanderGeneral != nullptr)
        SalamanderGeneral->GetConfigParameter(SALCFG_ALWAYSONTOP, &alwaysOnTop, sizeof(alwaysOnTop), nullptr);
    return alwaysOnTop != FALSE;
}

bool SaveHistoryFromHost()
{
    if (pictview::Services().SaveHistory)
        return pictview::Services().SaveHistory();

    BOOL saveHistory = TRUE;
    if (SalamanderGeneral != nullptr)
        SalamanderGeneral->GetConfigParameter(SALCFG_SAVEHISTORY, &saveHistory, sizeof(saveHistory), nullptr);
    return saveHistory != FALSE;
}

std::wstring TakePendingFocusPath()
{
    std::lock_guard<std::mutex> lock(PendingFocusMutex);
    std::wstring path;
    path.swap(PendingFocusPath);
    return path;
}

bool FindSourceFileForViewer(void* context,
                             int sourceUID,
                             int currentIndex,
                             const wchar_t* currentPath,
                             pictview::ViewerSourceNavigationMode mode,
                             pictview::ViewerSourceFile& file,
                             bool& noMoreFiles,
                             bool& sourceBusy)
{
    noMoreFiles = false;
    sourceBusy = false;

    CSalamanderGeneralAbstract* salamander = static_cast<CSalamanderGeneralAbstract*>(context);
    if (salamander == nullptr || sourceUID == -1)
        return false;

    pictview::ImageEngine engine;
    if (!engine.IsAvailable())
        return false;

    const bool previous = mode == pictview::ViewerSourceNavigationMode::Previous ||
                          mode == pictview::ViewerSourceNavigationMode::PreviousSelected ||
                          mode == pictview::ViewerSourceNavigationMode::Last;
    const bool preferSelected = mode == pictview::ViewerSourceNavigationMode::PreviousSelected ||
                                mode == pictview::ViewerSourceNavigationMode::NextSelected;
    const bool fromSourceEnd = mode == pictview::ViewerSourceNavigationMode::First ||
                               mode == pictview::ViewerSourceNavigationMode::Last;

    int candidateIndex = fromSourceEnd ? -1 : currentIndex;
    std::wstring lastPath = currentPath != nullptr ? currentPath : L"";
    for (;;)
    {
        std::wstring candidatePath;
        BOOL noMore = FALSE;
        BOOL busy = FALSE;
        const BOOL ok = SPLGetAdjacentFileNameForViewerOwned(salamander,
                                                             previous ? TRUE : FALSE,
                                                             sourceUID,
                                                             &candidateIndex,
                                                             lastPath.c_str(),
                                                             preferSelected ? TRUE : FALSE,
                                                             TRUE,
                                                             candidatePath,
                                                             &noMore,
                                                             &busy);

        noMoreFiles = noMore != FALSE;
        sourceBusy = busy != FALSE;
        if (!ok)
            return false;

        if (preferSelected)
        {
            BOOL isSelected = FALSE;
            BOOL selectionBusy = FALSE;
            if (!salamander->IsFileNameForViewerSelected(sourceUID,
                                                         candidateIndex,
                                                         candidatePath.c_str(),
                                                         &isSelected,
                                                         &selectionBusy))
            {
                sourceBusy = selectionBusy != FALSE;
                return false;
            }
            if (isSelected == FALSE)
            {
                lastPath = std::move(candidatePath);
                if (lastPath.empty())
                    return false;
                continue;
            }
        }

        if (!candidatePath.empty())
        {
            pictview::OpenDocumentResult probe = engine.OpenPath(candidatePath.c_str());
            if (probe.Succeeded())
            {
                file.Path = std::move(candidatePath);
                file.SourceIndex = candidateIndex;
                return true;
            }
        }

        lastPath = std::move(candidatePath);
        if (lastPath.empty())
            return false;
    }
}

bool ToggleSourceSelectionForViewer(void* context,
                                    int sourceUID,
                                    int currentIndex,
                                    const wchar_t* currentPath,
                                    bool& selected,
                                    bool& sourceBusy)
{
    selected = false;
    sourceBusy = false;

    CSalamanderGeneralAbstract* salamander = static_cast<CSalamanderGeneralAbstract*>(context);
    if (salamander == nullptr || sourceUID == -1 || currentPath == nullptr || currentPath[0] == L'\0')
        return false;

    BOOL isSelected = FALSE;
    BOOL busy = FALSE;
    if (!salamander->IsFileNameForViewerSelected(sourceUID,
                                                 currentIndex,
                                                 currentPath,
                                                 &isSelected,
                                                 &busy))
    {
        sourceBusy = busy != FALSE;
        return false;
    }

    const BOOL nextSelected = isSelected == FALSE ? TRUE : FALSE;
    busy = FALSE;
    if (!salamander->SetSelectionOnFileNameForViewer(sourceUID,
                                                     currentIndex,
                                                     currentPath,
                                                     nextSelected,
                                                     &busy))
    {
        sourceBusy = busy != FALSE;
        return false;
    }

    selected = nextSelected != FALSE;
    return true;
}

bool FocusSourceFileForViewer(void* context,
                              const wchar_t* currentPath,
                              bool& sourceBusy)
{
    sourceBusy = false;

    CSalamanderGeneralAbstract* salamander = static_cast<CSalamanderGeneralAbstract*>(context);
    if (salamander == nullptr || currentPath == nullptr || currentPath[0] == L'\0')
        return false;

    if (!salamander->SalamanderIsNotBusy(nullptr))
    {
        sourceBusy = true;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(PendingFocusMutex);
        PendingFocusPath = currentPath;
    }

    salamander->PostMenuExtCommand(InternalFocusSourceCommand, TRUE);
    return true;
}

class CPluginInterfaceForViewer : public CPluginInterfaceForViewerAbstract
{
public:
    BOOL WINAPI ViewFile(const wchar_t* name, int left, int top, int width, int height,
                         UINT showCmd, BOOL alwaysOnTop, BOOL returnLock, HANDLE* lock,
                         BOOL* lockOwner, CSalamanderPluginViewerData* viewerData,
                         int enumFilesSourceUID, int enumFilesCurrentIndex) override
    {
        (void)viewerData;

        if (name == nullptr || name[0] == L'\0')
            return FALSE;
        const std::wstring wideName = name;

        pictview::ViewerSourceNavigation sourceNavigation;
        sourceNavigation.Context = SalamanderGeneral;
        sourceNavigation.SourceUID = enumFilesSourceUID;
        sourceNavigation.CurrentIndex = enumFilesCurrentIndex;
        sourceNavigation.CurrentPath = wideName;
        sourceNavigation.FindFile = FindSourceFileForViewer;
        sourceNavigation.ToggleSelection = ToggleSourceSelectionForViewer;
        sourceNavigation.FocusCurrentFile = FocusSourceFileForViewer;

        pictview::ViewerHostActions hostActions;
        hostActions.Context = SalamanderGeneral;
        hostActions.SelectWholeNameOnRename = SelectWholeNameOnRenameFromHost();
        hostActions.DeleteFile = pictview::DeleteFileForViewer;
        hostActions.RenameFile = pictview::RenameFileForViewer;
        hostActions.CopyFile = pictview::CopyFileForViewer;

        if (pictview::Services().OpenViewer)
        {
            return pictview::Services().OpenViewer(wideName, left, top, width, height, showCmd, alwaysOnTop, returnLock, lock,
                                                   lockOwner, hostActions, sourceNavigation);
        }

        return pictview::OpenViewerWindow(wideName, left, top, width, height, showCmd,
                                           alwaysOnTop, returnLock, lock, lockOwner,
                                           hostActions, sourceNavigation);
    }

    BOOL WINAPI CanViewFile(const wchar_t* name) override
    {
        if (name == nullptr || name[0] == L'\0')
            return FALSE;

        pictview::ImageEngine engine;
        pictview::OpenDocumentResult result = engine.OpenPath(name);
        return result.Succeeded() ? TRUE : FALSE;
    }
};

// Extensions of the installed WIC decoders that the built-in masks do not cover.
std::vector<std::wstring> InstalledCodecExtensions()
{
    std::vector<std::wstring> lists;
    if (pictview::Services().DecoderFileExtensions)
        lists = pictview::Services().DecoderFileExtensions();
    else
    {
        pictview::ImageEngine engine;
        if (engine.IsAvailable())
            lists = engine.DecoderFileExtensions();
    }
    return pictview::ExtensionsBeyondMasks(pictview::ParseDecoderExtensions(lists), WicImageMasks);
}

class CPluginInterfaceForMenuExt : public CPluginInterfaceForMenuExtAbstract
{
public:
    DWORD WINAPI GetMenuItemState(int id, DWORD eventMask) override
    {
        (void)eventMask;
        if (id == PluginMenuPasteClipboardCommand)
            return ClipboardContainsSupportedImage() ? MENU_ITEM_STATE_ENABLED : 0;
        return 0;
    }

    BOOL WINAPI ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander, HWND parent,
                                int id, DWORD eventMask) override
    {
        (void)salamander;
        (void)eventMask;

        if (id == PluginMenuPasteClipboardCommand)
        {
            if (!ClipboardContainsSupportedImage())
                return FALSE;

            int left = CW_USEDEFAULT;
            int top = CW_USEDEFAULT;
            int width = CW_USEDEFAULT;
            int height = CW_USEDEFAULT;
            UINT showCmd = SW_SHOWNORMAL;
            ViewerPlacementFromParent(parent, left, top, width, height, showCmd);
            const BOOL alwaysOnTop = AlwaysOnTopFromHost() ? TRUE : FALSE;
            if (pictview::Services().LaunchMenuViewer)
                return pictview::Services().LaunchMenuViewer(id, left, top, width, height, showCmd, alwaysOnTop);
            return pictview::OpenClipboardViewerWindow(left, top, width, height, showCmd, alwaysOnTop);
        }

        if (id == PluginMenuScreenCaptureCommand)
        {
            int left = CW_USEDEFAULT;
            int top = CW_USEDEFAULT;
            int width = CW_USEDEFAULT;
            int height = CW_USEDEFAULT;
            UINT showCmd = SW_SHOWNORMAL;
            ViewerPlacementFromParent(parent, left, top, width, height, showCmd);
            const BOOL alwaysOnTop = AlwaysOnTopFromHost() ? TRUE : FALSE;
            if (pictview::Services().LaunchMenuViewer)
                return pictview::Services().LaunchMenuViewer(id, left, top, width, height, showCmd, alwaysOnTop);
            return pictview::OpenScreenCaptureViewerWindow(left, top, width, height, showCmd, alwaysOnTop);
        }

        if (id == PluginMenuRefreshThumbnailsCommand)
        {
            UpdateThumbnails(salamander, parent);
            return FALSE; // the files changed are announced folder by folder
        }

        if (id != InternalFocusSourceCommand)
            return FALSE;

        std::wstring focusPath = TakePendingFocusPath();
        if (focusPath.empty() || SalamanderGeneral == nullptr)
            return FALSE;

        std::wstring fileName;
        if (SPLCutDirectoryOwned(SalamanderGeneral, focusPath, &fileName))
        {
            SalamanderGeneral->SkipOneActivateRefresh();
            SalamanderGeneral->FocusNameInPanel(PANEL_SOURCE, focusPath.c_str(), fileName.c_str());
        }

        return FALSE;
    }

    BOOL WINAPI HelpForMenuItem(HWND parent, int id) override
    {
        DWORD topic = 0;
        if (id == PluginMenuPasteClipboardCommand)
            topic = IDH_VIEWBMPFROMCLIP;
        else if (id == PluginMenuScreenCaptureCommand)
            topic = IDH_SCREENCAPTURE;
        else if (id == PluginMenuRefreshThumbnailsCommand)
            topic = IDH_UPDATETHUMBNAIL;
        if (topic == 0)
            return FALSE;
        if (SalamanderGeneral != nullptr)
            SalamanderGeneral->OpenHtmlHelp(parent, HHCDisplayContext, topic, FALSE);
        return TRUE;
    }

    void WINAPI BuildMenu(HWND parent, CSalamanderBuildMenuAbstract* salamander) override
    {
        (void)parent;
        (void)salamander;
    }
};

class CPluginInterfaceForThumbLoader : public CPluginInterfaceForThumbLoaderAbstract
{
public:
    BOOL WINAPI LoadThumbnail(const wchar_t* filename, int thumbWidth, int thumbHeight,
                              CSalamanderThumbnailMakerAbstract* thumbMaker,
                              BOOL fastThumbnail) override
    {
        if (filename == nullptr || filename[0] == L'\0')
            return FALSE;

        pictview::ThumbnailLoadOptions options;
        if (SalamanderGeneral != nullptr) // panel colours may be read from any thread
            options.Background = SalamanderGeneral->GetCurrentColor(SALCOL_ITEM_BK_NORMAL);
        const pictview::ViewerPreferences preferences = pictview::GetViewerPreferences();
        options.IgnoreEmbeddedThumbnails = preferences.IgnoreThumbnails;
        options.ApplyExifOrientation = preferences.AutoRotate;
        options.MaxSourceMegapixels = preferences.MaxThumbImgSize >= 0 ? static_cast<uint32_t>(preferences.MaxThumbImgSize) : 90u;
        return pictview::LoadThumbnailFromPath(filename, thumbWidth, thumbHeight,
                                                thumbMaker, fastThumbnail, options);
    }
};

class CPluginInterface : public CPluginInterfaceAbstract
{
public:
    void WINAPI About(HWND parent) override
    {
        pictview::ShowAboutDialog(parent);
    }

    BOOL WINAPI Release(HWND parent, BOOL force) override
    {
        // Viewers open and the unload not forced: ask first, as the old PictView did.
        if (!force && pictview::AnyViewerWindowOpen() &&
            pictview::ViewerMessageBox(parent, LoadPluginString(IDS_SOME_WINS_OPENED).c_str(), MB_YESNO | MB_ICONQUESTION) != IDYES)
        {
            return FALSE;
        }
        if (!pictview::CloseAllViewerWindows(force != FALSE))
            return FALSE;
        pictview::ReleaseViewer();
        ReleaseWinLib(PluginInstance());
        return TRUE;
    }

    void WINAPI LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry) override
    {
        (void)parent;
        pictview::LoadViewerPreferencesConfiguration(regKey, registry);
        pictview::LoadCopyToConfiguration(regKey, registry);
        pictview::LoadRecentHistoryConfiguration(regKey, registry);
        pictview::LoadHistogramConfiguration(regKey, registry);
        pictview::LoadMetadataDetailsConfiguration(regKey, registry);
    }

    void WINAPI SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry) override
    {
        (void)parent;
        pictview::SaveViewerPreferencesConfiguration(regKey, registry);
        pictview::SaveCopyToConfiguration(regKey, registry);
        pictview::SaveRecentHistoryConfiguration(regKey, registry, SaveHistoryFromHost());
        pictview::SaveHistogramConfiguration(regKey, registry);
        pictview::SaveMetadataDetailsConfiguration(regKey, registry);
    }

    void WINAPI Configuration(HWND parent) override
    {
        pictview::EditViewerPreferences(parent, PluginInstance());
    }

    void WINAPI Connect(HWND parent, CSalamanderConnectAbstract* salamander) override
    {
        (void)parent;
        InstallPluginMenuIcon(salamander);
        salamander->AddMenuItem(-1,
                                LoadPluginString(IDS_VIEW_BMP_IN_CLIPBOARD).c_str(),
                                SALHOTKEY('B', HOTKEYF_CONTROL | HOTKEYF_SHIFT),
                                PluginMenuPasteClipboardCommand,
                                TRUE,
                                MENU_EVENT_TRUE,
                                MENU_EVENT_TRUE,
                                MENU_SKILLLEVEL_ALL);
        salamander->AddMenuItem(-1,
                                LoadPluginString(IDS_PLUGINSMENU_SCREEN_CAPTURE).c_str(),
                                0,
                                PluginMenuScreenCaptureCommand,
                                FALSE,
                                MENU_EVENT_TRUE,
                                MENU_EVENT_TRUE,
                                MENU_SKILLLEVEL_ALL);
        salamander->AddMenuItem(-1,
                                LoadPluginString(IDS_PLUGINSMENU_REGENERATE_THUMBNAIL).c_str(),
                                0,
                                PluginMenuRefreshThumbnailsCommand,
                                FALSE,
                                MENU_EVENT_FILE_FOCUSED | MENU_EVENT_FILES_SELECTED,
                                MENU_EVENT_TRUE,
                                MENU_SKILLLEVEL_ALL);
        salamander->AddViewer(WicImageMasks, FALSE);
        if (pictview::LoadedConfigurationVersion() < 23) // older PictView, or PNG owned by Web Viewer
        {
            salamander->AddViewer(WicImageMasks, TRUE);
            for (const wchar_t* mask : RetiredViewerMasks)
                salamander->ForceRemoveViewer(mask);
        }

        // Formats the installed codecs add (HEIF, AVIF, WebP, camera RAW, ...): each joins the
        // viewer list once, when it first appears; thumbnails follow whatever is installed.
        const std::vector<std::wstring> codecExtensions = InstalledCodecExtensions();
        const std::vector<std::wstring> offered = pictview::GetOfferedCodecExtensions();
        const std::vector<std::wstring> fresh = pictview::ExtensionsNotOffered(codecExtensions, offered);
        if (!fresh.empty())
        {
            salamander->AddViewer(pictview::MasksFromExtensions(fresh).c_str(), TRUE);
            std::vector<std::wstring> updated = offered;
            updated.insert(updated.end(), fresh.begin(), fresh.end());
            pictview::SetOfferedCodecExtensions(updated);
        }
        pictview::SetAdditionalOpenMasks(pictview::MasksFromExtensions(codecExtensions));
        std::wstring thumbnailMasks = WicImageMasks;
        if (!codecExtensions.empty())
            thumbnailMasks += L";" + pictview::MasksFromExtensions(codecExtensions);
        salamander->SetThumbnailLoader(thumbnailMasks.c_str());
    }

    void WINAPI ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData) override
    {
        (void)pluginData;
    }

    CPluginInterfaceForArchiverAbstract* WINAPI GetInterfaceForArchiver() override { return nullptr; }
    CPluginInterfaceForViewerAbstract* WINAPI GetInterfaceForViewer() override { return &m_viewer; }
    CPluginInterfaceForMenuExtAbstract* WINAPI GetInterfaceForMenuExt() override { return &m_menuExt; }
    CPluginInterfaceForFSAbstract* WINAPI GetInterfaceForFS() override { return nullptr; }
    CPluginInterfaceForThumbLoaderAbstract* WINAPI GetInterfaceForThumbLoader() override { return &m_thumbLoader; }

    void WINAPI Event(int event, DWORD param) override
    {
        (void)param;
        switch (event)
        {
        case PLUGINEVENT_COLORSCHANGED:
            pictview::NotifyViewerHostEvent(pictview::ViewerHostEvent::ColorsChanged);
            break;

        case PLUGINEVENT_SETTINGCHANGE:
            pictview::NotifyViewerHostEvent(pictview::ViewerHostEvent::SettingsChanged);
            break;

        case PLUGINEVENT_CONFIGURATIONCHANGED:
            pictview::NotifyViewerHostEvent(pictview::ViewerHostEvent::ConfigurationChanged);
            break;
        }
    }

    void WINAPI ClearHistory(HWND parent) override
    {
        (void)parent;
        pictview::ClearRecentHistory();
    }

    void WINAPI AcceptChangeOnPathNotification(const wchar_t* path, BOOL includingSubdirs) override
    {
        (void)path;
        (void)includingSubdirs;
    }

    void WINAPI PasswordManagerEvent(HWND parent, int event) override
    {
        (void)parent;
        (void)event;
    }

private:
    CPluginInterfaceForViewer m_viewer;
    CPluginInterfaceForMenuExt m_menuExt;
    CPluginInterfaceForThumbLoader m_thumbLoader;
};

CPluginInterface PluginInterface;

} // namespace


BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
        DllInstance = hinstDLL;
    (void)lpvReserved;
    return TRUE;
}

int WINAPI SalamanderPluginGetReqVer()
{
    return LAST_VERSION_OF_SALAMANDER;
}

CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander)
{
    if (salamander == nullptr)
        return nullptr;

    if (salamander->GetVersion() < LAST_VERSION_OF_SALAMANDER)
    {
        MessageBoxW(salamander->GetParentWindow(), _CRT_WIDE(REQUIRE_LAST_VERSION_OF_SALAMANDER),
                    PluginNameEN, MB_OK | MB_ICONERROR);
        return nullptr;
    }

    SalamanderDebug = salamander->GetSalamanderDebug();
    SalamanderVersion = salamander->GetVersion();
    SalamanderGeneral = salamander->GetSalamanderGeneral();
    if (SalamanderGeneral != nullptr && !InitializeWinLib(PluginNameEN, PluginInstance()))
        return nullptr;
    if (SalamanderGeneral != nullptr)
        SetupWinLibHelp([](HWND window, UINT helpId) {
            if (SalamanderGeneral != nullptr)
                SalamanderGeneral->OpenHtmlHelp(window, HHCDisplayContext, helpId, FALSE);
        });
    if (SalamanderGeneral != nullptr)
    {
        SalamanderGeneral->SetHelpFileName(L"pictview.chm");
        pictview::SetViewerMessageBox([](HWND parent, const wchar_t* text, const wchar_t* caption, UINT type) {
            return SalamanderGeneral->SalMessageBox(parent, text, caption, type);
        });
    }
    if (!pictview::InitializeViewer(PluginInstance(), SalamanderGeneral, salamander->GetSalamanderGUI()))
        return nullptr;
    pictview::SetThumbnailSettingsChangedHandler([] { RefreshSourcePanelThumbnails(); });

    salamander->SetBasicPluginData(LoadPluginString(IDS_PLUGINNAME).c_str(),
                                   FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION | FUNCTION_VIEWER,
                                   L"" VERSINFO_VERSION_NO_PLATFORM,
                                   VERSINFO_COPYRIGHT,
                                   LoadPluginString(IDS_PLUGIN_DESCRIPTION).c_str(),
                                   L"PictView" /* do not translate! */);
    salamander->SetPluginHomePageURL(L"https://sally-filemanager.app/");
    return &PluginInterface;
}
