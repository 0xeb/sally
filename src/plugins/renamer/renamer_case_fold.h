// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The renamer's search subject and search pattern are both UTF-8 (see renamer_text.h),
// while the Spencer regexp core and BMSearch are byte engines, so neither can case-fold
// a multi-byte code point on its own.
//
// The fold that solves this is not renamer-specific: common/regexp.cpp needs exactly the
// same thing for Find's UTF-8 arm. The implementation therefore lives in
// common/text/Utf8CaseFold.h and this header is the renamer's alias for it, so the two
// engines cannot drift apart.

#include "common/text/Utf8CaseFold.h"

namespace renamer
{

using sally::text::FoldScope;
using sally::text::FoldUtf8Preserving;

} // namespace renamer
