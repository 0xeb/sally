#include "new_name.h"

NewNameCheck CheckNewName(const std::wstring& name)
{
    if (name.empty())
        return NewNameCheck::Empty;
    if (name == L"." || name == L".." || name.find(L'/') != std::wstring::npos ||
        name.find(L'\0') != std::wstring::npos)
        return NewNameCheck::Invalid;
    return NewNameCheck::Ok;
}
