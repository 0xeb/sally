// "Change Attributes" dialog (Ctrl+F2) for the SFTP panel: a 3x3 grid of checkboxes (owner/group/others x read/write/
// execute), the octal value kept in step with the grid, the same permissions as text (rwxr-xr-x), and the option to
// apply the change recursively to files and/or directories.
//
// The dialog knows nothing about the file system: it gets the current modes as a CModeState and returns what to change.
#pragma once

#include <string>

#include "centered_dialog.h"
#include "file_mode.h"

// What the dialog starts with.
struct CChmodDialogInput
{
    std::wstring name;          // the item's name (used when itemCount is 1)
    int itemCount = 1;          // how many items are selected
    bool anyDirectory = false;  // at least one selected item is a directory (enables "Apply recursively")
    CModeState state;           // the current modes: bits all items share are checked, bits they disagree on are mixed
};

// What the user chose.
struct CChmodChoice
{
    CModeChange change;         // the bits to switch on and off (bits left mixed are not in it)
    bool recursive = false;
    bool applyToFiles = true;   // with recursive: change files
    bool applyToDirs = true;    // with recursive: change directories
};

class CChmodDialog : public CCenteredDialog
{
public:
    CChmodDialog(HWND parent, const CChmodDialogInput& input);

    // True: OK (the choice is in 'choice'). Cancel, ESC and every failure give false.
    bool Run(CChmodChoice& choice);

    void Transfer(CTransferInfo& ti) override;

protected:
    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    void InitControls();
    void OnGridClicked();
    void OnOctalChanged();
    void UpdateEnabled();
    bool OctalIsValid();

    void ReadGrid();     // checkboxes -> state_
    void WriteGrid();    // state_ -> checkboxes (and their two- or three-state style)
    void ShowOctal();    // state_ -> the octal edit
    void ShowSymbolic(); // state_ -> the rwx text

    CChmodDialogInput input_;
    CModeState state_;
    CChmodChoice choice_;
    int recursive_ = 0; // checkbox values for CTransferInfo::CheckBox
    int files_ = 1;
    int dirs_ = 1;
    bool updating_ = false; // we are writing into the controls ourselves: their notifications are not the user's
};

// Shows the dialog. True: OK.
bool ShowChmodDialog(HWND parent, const CChmodDialogInput& input, CChmodChoice& choice);
