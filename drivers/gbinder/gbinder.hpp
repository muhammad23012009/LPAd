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

#ifndef GBINDER_HPP
#define GBINDER_HPP

#include <gbinder.h>
#include <glib.h>

#include <driver_interface.h>

#include "gbindermodem.h"

struct radio_response_info {
    int32_t type;
    int32_t serial;
    int32_t error;
};

static gsize binder_read_parcelable_size(GBinderReader* reader) {
    guint32 non_null = 0, payload_size = 0;
    if (gbinder_reader_read_uint32(reader, &non_null) && non_null &&
        gbinder_reader_read_uint32(reader, &payload_size) &&
        payload_size >= sizeof(payload_size)) {
        return payload_size - sizeof(payload_size);
    }
    return 0;
}

class GBinderDriver : public DriverInterface
{
public:
    static bool usable();

    GBinderDriver();

    std::string driverName() const override { return "GBinder"; }
    std::vector<std::shared_ptr<ModemInterface>> getModems() const override;

private:
    std::shared_ptr<GBinderModem> m_modem;
    bool m_aidl = false;

    std::shared_ptr<GBinderServiceManager> m_sm;

    GBinderRemoteObject* m_remote = nullptr;
    GBinderClient* m_client = nullptr;
};

#endif
