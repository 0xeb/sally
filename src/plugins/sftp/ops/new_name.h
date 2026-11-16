// Check of a name typed by the user for a new remote item (F2 rename, F7 create directory). No Sally or UI headers.
#pragma once

#include <string>

enum class NewNameCheck
{
    Ok,
    Empty,   // nothing typed
    Invalid  // "." or "..", or contains '/' or a NUL: v1 accepts one plain name, never a sub-path
};

NewNameCheck CheckNewName(const std::wstring& name);
