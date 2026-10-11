/*
 * libretroshare/src/serialiser: tlvstring_bounds_test
 *
 * RetroShare Serialiser.
 *
 * Copyright 2026 by defnax.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License Version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA.
 *
 * Standalone regression for malformed TLV strings.
 *
 */

#include "serialiser/rstlvbase.h"
#include <cassert>
#include <cstdint>
#include <string>

int main()
{
    unsigned char data[9] = {0, 0, 0, 0, 0, 0, 'a', 'b', 'c'};
    for (uint32_t length = 0; length < TLV_HEADER_SIZE; ++length) {
        data[5] = length;
        uint32_t offset = 0;
        std::string value = "unchanged";
        assert(!GetTlvString(data, sizeof(data), &offset, 0, value));
        assert(offset == 0 && value == "unchanged");
    }
    data[5] = 6;
    for (uint32_t size = 0; size < 6; ++size) {
        uint32_t offset = 0;
        std::string value;
        assert(!GetTlvString(data, size, &offset, 0, value));
    }
    for (uint32_t offset : {4u, 9u, 10u, UINT32_MAX}) {
        std::string value;
        assert(!GetTlvString(data, sizeof(data), &offset, 0, value));
    }
    uint32_t offset = 0;
    std::string value = "old";
    assert(GetTlvString(data, sizeof(data), &offset, 0, value));
    assert(offset == 6 && value.empty());
    data[5] = 9;
    offset = 0;
    assert(GetTlvString(data, sizeof(data), &offset, 0, value));
    assert(offset == 9 && value == "abc");
    data[5] = 10;
    offset = 0;
    assert(!GetTlvString(data, sizeof(data), &offset, 0, value));
}
