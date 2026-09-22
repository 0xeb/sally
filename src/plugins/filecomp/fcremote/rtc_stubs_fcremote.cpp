// SPDX-FileCopyrightText: 2025-2026 Elias Bachaalany
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stddef.h>

extern "C" {

#pragma optimize("", off)
void* __cdecl memcpy(void* destination, const void* source, size_t count)
{
    unsigned char* out = static_cast<unsigned char*>(destination);
    const unsigned char* in = static_cast<const unsigned char*>(source);
    while (count-- != 0)
        *out++ = *in++;
    return destination;
}
#pragma optimize("", on)

}

#include "../../../common/rtc_stubs.cpp"
