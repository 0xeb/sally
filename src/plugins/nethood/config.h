// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
	Network Plugin for Open Salamander
	
	Copyright (c) 2008-2023 Milan Kase <manison@manison.cz>
	
	TODO:
	Open-source license goes here...
*/

// SuggestedFSName/HomePageUrl/SuggestedConfigKey are native-wide SDK metadata.
static const wchar_t SuggestedFSName[] = L"net";

static const wchar_t HomePageUrl[] = L"https://github.com/0xeb/sally";

// Do not translate, per the original comment. The note that used to sit here -
// "stays narrow, entry.cpp's InitializeWinLib is a genuine narrow consumer" - was already false
// when written against a wchar_t[]: winlib's InitializeWinLib is wide, and TEXT() resolved to the
// wide literal too. Both halves now agree.
static const wchar_t PluginNameEN[] = L"Network";

static const wchar_t SuggestedConfigKey[] = L"nethood";
