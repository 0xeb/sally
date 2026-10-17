// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <string_view>

enum class SvgBackground : unsigned { Checkerboard, Light, Dark };

inline SvgBackground ValidateSvgBackground(unsigned value)
{
    return value <= static_cast<unsigned>(SvgBackground::Dark) ? static_cast<SvgBackground>(value) : SvgBackground::Checkerboard;
}

inline bool IsSvgFile(std::wstring_view name)
{
    if (name.size() < 4)
        return false;
    const auto ext = name.substr(name.size() - 4);
    return ext[0] == L'.' && (ext[1] == L's' || ext[1] == L'S') &&
           (ext[2] == L'v' || ext[2] == L'V') && (ext[3] == L'g' || ext[3] == L'G');
}

// Change the viewer canvas, not the SVG artwork or the source file.
inline std::wstring SvgBackgroundScript(SvgBackground mode)
{
    std::wstring script = LR"JS((()=>{const r=document.documentElement,ns='http://www.w3.org/2000/svg';
if(!r||r.localName!=='svg'||r.namespaceURI!==ns)return false;
if(!window.__sallySvgCanvas){const s=getComputedStyle(r);
window.__sallySvgCanvas={image:s.backgroundImage,color:s.backgroundColor};}
const base=window.__sallySvgCanvas;
// Opaque authored CSS backgrounds, like opaque SVG rectangles, remain opaque.
const c=base.color.match(/^rgba?\((.+)\)$/);
if(!c&&base.color!=='transparent')return true;
if(c&&c[1].split(',').length===3)return true;
if(c&&Number(c[1].split(',')[3])>0)return true;
)JS";
    // CSS backgrounds do not alter SVG drawing/filter/compositing semantics.
    const wchar_t* background = mode == SvgBackground::Light ? L"#ffffff" :
        mode == SvgBackground::Dark ? L"#243247" :
        L"conic-gradient(#888 25%,#b0b0b0 0 50%,#888 0 75%,#b0b0b0 0) 0 0 / 16px 16px";
    script += L"r.style.background=(base.image==='none'?'':base.image+', ')+'";
    script += background;
    script += L"';return true;})()";
    return script;
}
