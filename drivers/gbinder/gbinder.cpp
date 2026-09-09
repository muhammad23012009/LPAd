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

#include "gbinder.hpp"

#include <iostream>
#include <filesystem>
#include <future>
#include <format>

// TODO: get rid of this!!!
extern "C" {
#define restrict
#include <euicc/hexutil.h>
}


static bool g_openReady = false;
static bool g_transmitResponseReady = false;
static bool g_cardStatusReady = false;
static bool g_refreshReceived = false;
static bool g_cardStatusReceived = false;
static bool g_channelClosed = false;

static GMainLoop *g_binderLoop = nullptr;

bool GBinderDriver::usable()
{
    // no binder? definitely not usable then
    if (!std::filesystem::exists("/dev/binder") && !std::filesystem::exists("/dev/hwbinder"))
    {
        return false;
    }

    // todo: eventually check for HAL presence as well?
    return true;
}

GBinderDriver::GBinderDriver()
{
    std::string aidlServiceName = "android.hardware.radio.sim.IRadioSim";
    auto sm = gbinder_servicemanager_new("/dev/binder");
    bool serviceFound = false;

    auto aidlList = gbinder_servicemanager_list_sync(sm);
    for (auto service = aidlList; *service != nullptr; ++service) {
        std::cout << "Found AIDL service: " << *service << std::endl;
        if (std::string(*service).find(aidlServiceName) != std::string::npos) {
            std::cout << "Using AIDL interface for IRadioSim" << std::endl;
            serviceFound = true;
            m_aidl = true;
            m_sm = std::shared_ptr<GBinderServiceManager>(sm, gbinder_servicemanager_unref);

            break;
        }
    }

    // Now check for HIDL
    if (!serviceFound)
    {
        gbinder_servicemanager_unref(sm);
        sm = gbinder_servicemanager_new("/dev/hwbinder");
        auto hidlList = gbinder_servicemanager_list_sync(sm);

        for (auto service = hidlList; *service != nullptr; ++service) {
            std::cout << "Found HIDL service: " << *service << std::endl;
        }

        for (auto service = hidlList; *service != nullptr; ++service) {
            if (std::string(*service).find("android.hardware.radio@1.0::IRadio") != std::string::npos) {
                std::cout << "Using HIDL interface for IRadio" << std::endl;
                serviceFound = true;
                m_aidl = false;
                m_sm = std::shared_ptr<GBinderServiceManager>(sm, gbinder_servicemanager_unref);

                break;
            }
        }
    }

    if (!serviceFound) {
        std::cerr << "No usable IRadio service found" << std::endl;
        throw std::runtime_error("No usable IRadio service found");
    }

    g_binderLoop = g_main_loop_new(nullptr, FALSE);

    m_modem = std::make_shared<GBinderModem>(m_sm, g_binderLoop, m_aidl);
}

std::vector<std::shared_ptr<ModemInterface>> GBinderDriver::getModems() const
{
    return {m_modem};
}
