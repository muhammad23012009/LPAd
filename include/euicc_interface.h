/*
 * This file is part of LPAd (https://github.com/muhammad23012009/LPAd)
 * Copyright (c) 2026 Muhammad Asif  <thevancedgamer@mentallysanemainliners.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EUICC_INTERFACE_H
#define EUICC_INTERFACE_H

#include <cstdint>
#include <memory>

// https://stackoverflow.com/a/17261928
static int char2int(char input)
{
    if (input >= '0' && input <= '9')
        return input - '0';
    if (input >= 'A' && input <= 'F')
        return input - 'A' + 10;
    if (input >= 'a' && input <= 'f')
        return input - 'a' + 10;
    throw std::invalid_argument("Invalid input string");
}

// This function assumes src to be a zero terminated sanitized string with
// an even number of [0-9a-f] characters, and target to be sufficiently large
static void hex2bin(const char* src, char* target)
{
    while(*src && src[1])
    {
        *(target++) = char2int(*src)*16 + char2int(src[1]);
        src += 2;
    }
}

class EuiccInterface
{
public:
    using Ptr = std::unique_ptr<uint8_t>;
    using Uint8Ptr = std::pair<Ptr, size_t>;
    static Uint8Ptr make_uint8_ptr(uint8_t* ptr, size_t len) {
        return std::make_pair(std::unique_ptr<uint8_t>(ptr), len);
    }

    // The ID of the physical slot that this eUICC interface is mapped to.
    virtual int slotId() const = 0;

    // Quirk for drivers that need to drop the logical channel after a refresh command. If returns true, the LPA will call waitForRefresh, which will block until the driver is ready again
    virtual bool needsChannelDrop() = 0;
    virtual void setupRefresh() = 0;
    virtual void waitForRefresh() = 0;

    virtual int connect() = 0;
    virtual void disconnect() = 0;
    virtual int logicalChannelOpen(Uint8Ptr aid) = 0;
    virtual void logicalChannelClose(int channel) = 0;
    virtual Uint8Ptr transmit(Uint8Ptr data) = 0;
};

#endif
