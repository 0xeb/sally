#include "precomp.h"

#include "info_list_dialog.h"
#include "progress_format.h"
#include "properties_dialog.h"

namespace
{
void Add(std::vector<CInfoRow>& rows, int labelId, std::wstring value)
{
    rows.push_back({LangStr(labelId), std::move(value)});
}

// Unix seconds as local date and time in the user's format; "unknown" for 0 or an impossible value.
std::wstring FormatUnixTime(int64_t unixSeconds)
{
    if (unixSeconds <= 0)
        return LangStr(IDS_INFO_UNKNOWN);
    // Unix seconds -> FILETIME (100 ns since 1601) -> local time
    ULARGE_INTEGER ticks;
    ticks.QuadPart = (static_cast<unsigned long long>(unixSeconds) + 11644473600ull) * 10000000ull;
    FILETIME utc = {ticks.LowPart, ticks.HighPart};
    SYSTEMTIME utcTime, local;
    if (!FileTimeToSystemTime(&utc, &utcTime) || !SystemTimeToTzSpecificLocalTime(nullptr, &utcTime, &local))
        return LangStr(IDS_INFO_UNKNOWN);
    wchar_t date[64] = L"";
    wchar_t time[64] = L"";
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, date, 64);
    GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &local, nullptr, time, 64);
    return std::wstring(date) + L" " + time;
}

std::wstring TypeText(const CPropertiesInput& in)
{
    if (in.isLink)
        return LangStr(in.isDir ? IDS_PROP_TYPE_DIR_LINK : IDS_PROP_TYPE_LINK);
    return LangStr(in.isDir ? IDS_PROP_TYPE_DIR : IDS_PROP_TYPE_FILE);
}

// "0644  (rw-r--r--)". When the items disagree about some bits, those show as '?' in the rwx text and the octal part is
// left out.
std::wstring PermissionsText(uint32_t modeBits, uint32_t mixedBits)
{
    const std::wstring rwx = FormatSymbolic(modeBits, mixedBits);
    if (mixedBits != 0)
        return rwx + L"  (" + LangStr(IDS_PROP_MIXED) + L")";
    return FormatOctal(modeBits) + L"  (" + rwx + L")";
}

std::wstring OrUnknown(const std::wstring& text)
{
    return text.empty() ? LangStr(IDS_INFO_UNKNOWN) : text;
}

std::wstring SizeText(uint64_t bytes)
{
    return SPLFormatStringOwned(LangStr(IDS_PROP_SIZE_VALUE).c_str(), FormatBytes(bytes).c_str(),
                                static_cast<unsigned long long>(bytes));
}

void AddOneItem(std::vector<CInfoRow>& rows, const CPropertiesInput& in)
{
    Add(rows, IDS_PROP_NAME, in.name);
    Add(rows, IDS_PROP_LOCATION, in.location);
    Add(rows, IDS_PROP_TYPE, TypeText(in));
    if (in.hasLinkTarget)
        Add(rows, IDS_PROP_LINK_TARGET, in.linkTarget);
    if (!in.isDir)
        Add(rows, IDS_PROP_SIZE, SizeText(in.size));
    Add(rows, IDS_PROP_MODIFIED, FormatUnixTime(in.mtime));
    Add(rows, IDS_PROP_ACCESSED, FormatUnixTime(in.atime));
    Add(rows, IDS_PROP_OWNER, OrUnknown(in.owner));
    Add(rows, IDS_PROP_GROUP, OrUnknown(in.group));
    Add(rows, IDS_PROP_PERMISSIONS,
        in.mode != 0 ? PermissionsText(PermissionBitsOf(in.mode), 0) : LangStr(IDS_INFO_UNKNOWN));
}

void AddSeveralItems(std::vector<CInfoRow>& rows, const CPropertiesInput& in)
{
    const CSelectionSummary& s = in.summary;
    Add(rows, IDS_PROP_LOCATION, in.location);
    Add(rows, IDS_PROP_CONTENTS, SPLFormatStringOwned(LangStr(IDS_PROP_CONTENTS_VALUE).c_str(), s.files, s.dirs));
    if (s.links > 0)
        Add(rows, IDS_PROP_LINKS, std::to_wstring(s.links));
    Add(rows, IDS_PROP_TOTAL_SIZE, SizeText(s.totalSize));
    Add(rows, IDS_PROP_PERMISSIONS,
        s.modeKnown ? PermissionsText(ShownMode(s.modeState), s.modeState.mixed) : LangStr(IDS_INFO_UNKNOWN));
}
} // namespace

void ShowPropertiesDialog(HWND parent, const CPropertiesInput& input)
{
    try
    {
        std::vector<CInfoRow> rows;
        std::wstring title;
        if (input.itemCount == 1)
        {
            AddOneItem(rows, input);
            title = SPLFormatStringOwned(LangStr(IDS_PROP_TITLE_ONE).c_str(), input.name.c_str());
        }
        else
        {
            AddSeveralItems(rows, input);
            title = SPLFormatStringOwned(LangStr(IDS_PROP_TITLE_MANY).c_str(), input.itemCount);
        }
        CInfoListDialog dialog(parent, IDD_FSPROPERTIES, IDH_PROPERTIES, title, std::move(rows));
        dialog.Run();
    }
    catch (...)
    {
        TRACE_E("ShowPropertiesDialog: unexpected exception.");
    }
}
