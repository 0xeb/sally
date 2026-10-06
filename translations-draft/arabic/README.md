<!--
SPDX-FileCopyrightText: 2026 Elias Bachaalany
SPDX-License-Identifier: GPL-2.0-or-later
-->

# Arabic (draft)

An unfinished Arabic translation of Sally (`sally.slt`) and the Check Version plugin
(`checkver.slt`). It is kept in the old `.slt` archive format, which nothing builds any
more, so it is not part of Sally.

Before it could ship it would need converting to `src/lang/ar-SA/lang.rc` (and
`src/plugins/<plugin>/lang/ar-SA/lang.rc` for each translated plugin), structurally
identical to the English resources, plus right-to-left layout work. See
[Translations](../../doc/DEV.md#translations) for how languages are built into Sally.
