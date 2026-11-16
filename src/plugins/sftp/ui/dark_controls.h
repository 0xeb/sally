#pragma once

// Dark mode painting for common controls that the shared dialog helper leaves light.
// Every function does nothing in light mode. The dialog applies the theme at creation (no live theme change).

// Paints the header of a list view (report mode) dark. Simplified: no sort arrows, no images and no hot/pressed
// states are drawn (only the text, the alignment and the dividers).
void ApplyDarkListHeader(HWND listView);

// Paints a tab control (the tab strip only; the pages are child dialogs) dark.
void ApplyDarkTabControl(HWND tabControl);

// Paints every group box (caption and frame), check box and radio button in the dialog (or tab page) light-on-dark.
// Call after the controls exist.
void ApplyDarkButtons(HWND dialog);
