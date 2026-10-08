// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

// The Print dialog's measurements and the image's place on the printed page, without any UI:
// lengths are kept in points (1/72 inch) and shown in the unit the user picks.

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace pictview
{

enum class PrintUnits
{
    Inches,
    Centimeters,
    Millimeters,
    Points,
    Picas,
};

double PointsToUnits(double points, PrintUnits units);
double UnitsToPoints(double value, PrintUnits units);

// "12.5", "0.25": up to four decimals, trailing zeros dropped (the legacy dialog's format).
std::wstring FormatPrintNumber(double value, int decimals = 4);
// Reads a number typed with '.' or ',' as the decimal separator; false when there is none.
bool ParsePrintNumber(const std::wstring& text, double& value);

// What the user chose. Position and size are in points; Left/Top are measured from the
// printable area's top-left corner.
struct PrintSettings
{
    bool Center = true;
    bool Fit = true;
    bool KeepAspect = true;
    bool BoundingBox = false;
    bool Selection = false;
    double Left = 0;
    double Top = 0;
    double Width = 0;
    double Height = 0;
    double Scale = 100; // percent of the image's natural size
    PrintUnits Units = PrintUnits::Millimeters;
};

// The page a printer prints on (all in inches except the device values).
struct PrinterPage
{
    double PaperWidth = 0;
    double PaperHeight = 0;
    double MarginLeft = 0; // the unprintable border, from the device's physical offsets
    double MarginTop = 0;
    double MarginRight = 0;
    double MarginBottom = 0;
    int DpiX = 0;
    int DpiY = 0;
    int PrintableWidth = 0; // device pixels (HORZRES)
    int PrintableHeight = 0; // device pixels (VERTRES)

    bool Valid() const;
};

// Reads the page of a printer DC.
PrinterPage DescribePrinterPage(HDC printer);

// The image's natural printed size in points: its own resolution when it has one, else the
// printer's ('fallbackDpi').
void NaturalPrintSize(unsigned width, unsigned height, double imageDpiX, double imageDpiY, int fallbackDpiX,
                      int fallbackDpiY, double& widthPoints, double& heightPoints);

// Where the image goes, in printable-area points: Fit fills the printable area (keeping the
// aspect ratio when asked); otherwise the chosen size. Centered when Center applies (Fit without
// Keep aspect fills the area, so nothing to center), else at Left/Top.
struct PrintPlacement
{
    bool Valid = false;
    double Left = 0;
    double Top = 0;
    double Width = 0;
    double Height = 0;
};
PrintPlacement PlaceOnPage(const PrintSettings& settings, double imageWidthPoints, double imageHeightPoints,
                           double printableWidthPoints, double printableHeightPoints);

// The placement in the printer DC's device pixels.
RECT PlacementToDevice(const PrintPlacement& placement, const PrinterPage& page);

} // namespace pictview
