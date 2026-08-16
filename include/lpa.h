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

#ifndef LPA_H
#define LPA_H

#include <iostream>
#include <string>
#include <functional>
#include <map>

#include "driver_interface.h"

extern "C" {
#include <euicc.h>
}

extern "C" {
#include <es8p.h>
#include <es9p.h>
#include <es10c_ex.h>
#include <es10c.h>
}

struct EuiccInfo
{
    std::string osVersion;
    int availableMemory;
};

struct EuiccProfile
{
    std::string profileName;
    std::string serviceProviderName;
    std::string nickname;
    std::string iccid;
    bool enabled;
};

class LPA
{
public:
    using ProfileConfirmationCallback = std::function<bool(EuiccProfile)>;

    LPA();

    // Returns a string representing the EID of the eUICC
    std::string getEid();

    // Returns information about the eUICC including OS version, available size, etc
    EuiccInfo getEuiccInfo();

    // Returns an array of all profiles installed on the eUICC.
    std::vector<EuiccProfile> getProfiles();

    // Enables a profile on the eUICC.
    void enableProfile(const std::string& iccid);

    void disableProfile(const std::string& iccid);

    // Start an install operation for a profile. Takes a callback for installation confirmation
    void installProfile(const std::string& smdp, const std::string& activationCode, const std::string& confirmationCode, ProfileConfirmationCallback callback);

    void removeProfile(const std::string& iccid);

    int meow(int a, int b, int c);

    void addProfileChangedCallback(std::function<void(EuiccProfile)> callback) {
        m_profileChangedCallback = callback;
    }

private:
    class EuiccLockGuard
    {
    public:
        EuiccLockGuard(euicc_ctx* ctx) : m_ctx(ctx)
        {
            if (m_ctx)
            {
                euicc_init(m_ctx);
            }
        }

        ~EuiccLockGuard()
        {
            if (m_ctx)
            {
                euicc_fini(m_ctx);
            }
        }
    
        euicc_ctx* m_ctx;
    };

    class ES10bGuard
    {
    public:
        ES10bGuard(euicc_ctx* ctx):
          m_ctx(ctx)
        {}

        ~ES10bGuard()
        {
            std::cout << "ES10bGuard destructor called" << std::endl;
            if (m_ctx)
            {
                es10b_cancel_session(m_ctx, ES10B_CANCEL_SESSION_REASON_ENDUSERREJECTION);
            }
        }

    private:
        euicc_ctx* m_ctx;
    };

    class ES9pGuard
    {
    public:
        ES9pGuard(euicc_ctx* ctx):
          m_ctx(ctx)
        {}

        ~ES9pGuard()
        {
            std::cout << "ES9pGuard destructor called" << std::endl;
            if (m_ctx)
            {
                es9p_cancel_session(m_ctx);
            }
        }
    private:
        euicc_ctx* m_ctx;
    };

    class ES8pGuard
    {
    public:
        ES8pGuard(es8p_metadata** metadata):
          m_metadata(metadata)
        {}

        ~ES8pGuard()
        {
            std::cout << "ES8pGuard destructor called" << std::endl;
            if (m_metadata)
            {
                es8p_metadata_free(m_metadata);
            }
        }
    private:
        es8p_metadata** m_metadata;
    };

    void processNotifications();

    bool m_esimsCached = false;
    std::function<void(EuiccProfile)> m_profileChangedCallback;
    // Map of ICCID to EuiccProfiles
    std::map<std::string, EuiccProfile> m_profiles;
    euicc_ctx* m_ctx;
    DriverInterface* m_driver;
};

#endif
