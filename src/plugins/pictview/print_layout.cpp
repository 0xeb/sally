// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include "print_layout.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace pictview
{
namespace
{

double PointsPerUnit(PrintUnits units)
{
    switch (units)
    {
    case PrintUnits::Inches:
        return 72.0;
    case PrintUnits::Centimeters:
        return 72.0 / 2.54;
    case PrintUnits::Millimeters:
        return 72.0 / 25.4;
    case PrintUnits::Picas:
        return 12.0;
    case PrintUnits::Points:
    default:
        return 1.0;
    }
}

} // namespace

double PointsToUnits(double points, PrintUnits units)
{
    return points / PointsPerUnit(units);
}

double UnitsToPoints(double value, PrintUnits units)
{
    return value * PointsPerUnit(units);
}

std::wstring FormatPrintNumber(double value, int decimals)
{
    if (!std::isfinite(value))
        value = 0;
    wchar_t text[64] = {};
    swprintf(text, sizeof(text) / sizeof(text[0]), L"%.*f", std::clamp(decimals, 0, 8), value);
    std::wstring result = text;
    if (result.find(L'.') != std::wstring::npos)
    {
        while (!result.empty() && result.back() == L'0')
            result.pop_back();
        if (!result.empty() && result.back() == L'.')
            result.pop_back();
    }
    if (result == L"-0")
        result = L"0";
    return result;
}

bool ParsePrintNumber(const std::wstring& text, double& value)
{
    std::wstring normalized = text;
    std::replace(normalized.begin(), normalized.end(), L',', L'.');
    wchar_t* end = nullptr;
    const double parsed = wcstod(normalized.c_str(), &end);
    if (end == normalized.c_str() || !std::isfinite(parsed))
        return false;
    value = parsed;
    return true;
}

bool PrinterPage::Valid() const
{
    return PaperWidth > 0 && PaperHeight > 0 && DpiX > 0 && DpiY > 0 && PrintableWidth > 0 && PrintableHeight > 0;
}

PrinterPage DescribePrinterPage(HDC printer)
{
    PrinterPage page;
    if (printer == nullptr)
        return page;
    page.DpiX = GetDeviceCaps(printer, LOGPIXELSX);
    page.DpiY = GetDeviceCaps(printer, LOGPIXELSY);
    page.PrintableWidth = GetDeviceCaps(printer, HORZRES);
    page.PrintableHeight = GetDeviceCaps(printer, VERTRES);
    if (page.DpiX <= 0 || page.DpiY <= 0)
        return PrinterPage();
    const int physicalWidth = GetDeviceCaps(printer, PHYSICALWIDTH);
    const int physicalHeight = GetDeviceCaps(printer, PHYSICALHEIGHT);
    const int offsetX = GetDeviceCaps(printer, PHYSICALOFFSETX);
    const int offsetY = GetDeviceCaps(printer, PHYSICALOFFSETY);
    // Drivers without physical sizes (some virtual printers) print the whole area.
    const int paperWidth = physicalWidth > 0 ? physicalWidth : page.PrintableWidth;
    const int paperHeight = physicalHeight > 0 ? physicalHeight : page.PrintableHeight;
    page.PaperWidth = static_cast<double>(paperWidth) / page.DpiX;
    page.PaperHeight = static_cast<double>(paperHeight) / page.DpiY;
    page.MarginLeft = static_cast<double>(offsetX) / page.DpiX;
    page.MarginTop = static_cast<double>(offsetY) / page.DpiY;
    page.MarginRight = static_cast<double>((std::max)(0, paperWidth - offsetX - page.PrintableWidth)) / page.DpiX;
    page.MarginBottom = static_cast<double>((std::max)(0, paperHeight - offsetY - page.PrintableHeight)) / page.DpiY;
    return page;
}

void NaturalPrintSize(unsigned width, unsigned height, double imageDpiX, double imageDpiY, int fallbackDpiX,
                      int fallbackDpiY, double& widthPoints, double& heightPoints)
{
    const double dpiX = imageDpiX > 0 ? imageDpiX : (fallbackDpiX > 0 ? fallbackDpiX : 96);
    const double dpiY = imageDpiY > 0 ? imageDpiY : (fallbackDpiY > 0 ? fallbackDpiY : 96);
    widthPoints = static_cast<double>(width) / dpiX * 72.0;
    heightPoints = static_cast<double>(height) / dpiY * 72.0;
}

PrintPlacement PlaceOnPage(const PrintSettings& settings, double imageWidthPoints, double imageHeightPoints,
                           double printableWidthPoints, double printableHeightPoints)
{
    PrintPlacement placement;
    if (!(imageWidthPoints > 0) || !(imageHeightPoints > 0) || !(printableWidthPoints > 0) || !(printableHeightPoints > 0))
        return placement;

    if (settings.Fit)
    {
        placement.Width = printableWidthPoints;
        placement.Height = printableHeightPoints;
        if (settings.KeepAspect)
        {
            const double scale = (std::min)(printableWidthPoints / imageWidthPoints, printableHeightPoints / imageHeightPoints);
            placement.Width = imageWidthPoints * scale;
            placement.Height = imageHeightPoints * scale;
        }
    }
    else
    {
        placement.Width = settings.Width;
        placement.Height = settings.Height;
    }
    if (!(placement.Width > 0) || !(placement.Height > 0))
        return placement;

    if (!settings.Fit || settings.KeepAspect)
    {
        if (settings.Center)
        {
            placement.Left = (printableWidthPoints - placement.Width) / 2;
            placement.Top = (printableHeightPoints - placement.Height) / 2;
        }
        else
        {
            placement.Left = settings.Left;
            placement.Top = settings.Top;
        }
    }
    placement.Valid = true;
    return placement;
}

RECT PlacementToDevice(const PrintPlacement& placement, const PrinterPage& page)
{
    const double x = page.DpiX / 72.0;
    const double y = page.DpiY / 72.0;
    RECT rect = {};
    rect.left = static_cast<LONG>(std::lround(placement.Left * x));
    rect.top = static_cast<LONG>(std::lround(placement.Top * y));
    rect.right = static_cast<LONG>(std::lround((placement.Left + placement.Width) * x));
    rect.bottom = static_cast<LONG>(std::lround((placement.Top + placement.Height) * y));
    if (rect.right <= rect.left)
        rect.right = rect.left + 1;
    if (rect.bottom <= rect.top)
        rect.bottom = rect.top + 1;
    return rect;
}

} // namespace pictview
