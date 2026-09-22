// SPDX-FileCopyrightText: 2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Parses the encoded directory-name field of an FTP 257 reply. The bytes stay
// in the connection's negotiated wire domain; decoding belongs to presentation.
// Output is transactional and has no application-imposed path ceiling.
bool FTPParseWorkingDirectoryReply(std::string_view reply,
                                   std::string& directory) noexcept;

// Parses the RFC 959 passive-mode address sextet from a bounded reply span.
// Outputs are changed only when a complete, byte-sized sextet is present.
bool FTPParsePassiveReply(std::string_view reply, std::uint32_t& ip,
                          std::uint16_t& port) noexcept;

// Returns the encoded system-name token from an FTP SYST reply. The view
// aliases the caller-owned reply and therefore has neither an arbitrary
// capacity nor a second byte owner.
std::string_view FTPParseServerSystemReply(std::string_view reply) noexcept;
