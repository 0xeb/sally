// Localized texts for the queue model's enums (ops/ has no language resources). Shared by the operation window and
// the solve-error dialog.
#pragma once

#include <string>

#include "op_queue.h"
#include "op_types.h"

// Short text for the reason an item is in an error state; empty for Problem::None.
std::wstring ProblemText(Problem problem);
// ProblemText, followed by ": details" when the item carries more detail from the worker.
std::wstring ProblemTextWithDetails(Problem problem, const std::wstring& details);
// The details of an item in an error state: its SSH error in the user's language, else the text the worker stored.
std::wstring ItemDetails(const CQueueItem& item);
// "Waiting", "Done", "Failed", ...
std::wstring ItemStateText(ItemState state);
