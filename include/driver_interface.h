/*
 * This file is part of lpaD (https://github.com/muhammad23012009/lpaD)
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

#ifndef DRIVER_INTERFACE_H
#define DRIVER_INTERFACE_H

#include <string>
#include <cstdint>
#include <memory>
#include <span>

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

class DriverInterface
{
public:
    using Uint8Ptr = std::unique_ptr<uint8_t, decltype(&std::free)>;
    static Uint8Ptr make_uint8_ptr(uint8_t* ptr) {
        return Uint8Ptr(ptr, &std::free);
    }

    static bool usable() {
        return false;
    }

    // Quirk for drivers that need to drop the logical channel after a refresh command. If returns true, the LPA will call waitForRefresh, which will block until the driver is ready again
    virtual bool needsChannelDrop() = 0;
    virtual void setupRefresh() = 0;
    virtual void waitForRefresh() = 0;

    virtual int connect() = 0;
    virtual void disconnect() = 0;
    virtual int logicalChannelOpen(const uint8_t* aid, uint8_t aid_len) = 0;
    virtual void logicalChannelClose(int channel) = 0;
    virtual std::pair<Uint8Ptr, size_t> transmit(std::pair<Uint8Ptr, size_t>) = 0;
};

#endif
