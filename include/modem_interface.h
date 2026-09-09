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

#ifndef MODEM_INTERFACE_H
#define MODEM_INTERFACE_H

#include <functional>
#include <vector>

#include "euicc_interface.h"

struct SlotMapping
{
    int physicalSlot;
    int portId;
};

enum class PortType
{
    PORT_TYPE_UICC,
    PORT_TYPE_EUICC
};

struct PortInfo
{
    // The ID of this port. Used to map a port to a physical slot in the modem.
    int portId;

    // The type of this port.
    PortType portType;

    // Whether the port is the current port
    bool currentPort;
};

struct SlotInfo
{
    // The ID of this physical slot. Used by ports to map themselves.
    int slotId;

    // All the ports that are available to be mapped to this slot.
    std::vector<PortInfo> ports;
};

class ModemInterface
{
public:
    enum class MEPMode
    {
        NONE,
        MEP_A1,
        MEP_A2,
        MEP_B
    };

    virtual std::string modemName() const = 0;

    // Returns the supported MEP mode of the modem
    virtual MEPMode supportedMEPMode() const = 0;

    // Returns the list of physical slots available in the modem, and their ports.
    virtual std::vector<SlotInfo> physicalSlots() const = 0;

    // Map a port to a physical slot in the modem. The port is relative to the physical slot.
    // TODO: What are we supposed to do about MEP-A2?
    virtual void setPortMapping(int slotId, int portId) = 0;

    // Returns the list of eUICC interfaces available in the modem.
    // If MEP is supported, the available eUICC interfaces MUST be treated as a single interface,
    // except for enabling or disabling profiles, as according to SGP.22 3.0, section 2.12,
    //  MEP-A1: The ISD-R is selected on eSIM Port 0 only and Profiles are selected
    //          on eSIM Ports 1 and higher, with the eSIM Port being assigned by the LPA.
    //          I.e., Command Port and Target Port will always be different.
    //  MEP-A2: The ISD-R is selected on eSIM Port 0 only and Profiles are selected
    //          on eSIM Ports 1 and higher, with the eSIM Port being assigned by the eUICC.
    //          I.e., Command Port and Target Port will always be different.
    //  MEP-B: Profiles are selected on eSIM Ports 0 and higher, with the ISD-R
    //         being selectable on any of these eSIM Ports. ES10c.EnableProfile and, if
    //         CAT is initialised on the Target Port, ES10c.DisableProfile are always sent on
    //         the Target Port (i.e., Command Port and Target Port are identical). If CAT is
    //         not initialised on the Target Port, ES10c.DisableProfile can be sent on any
    //         eSIM Port. Other ES10 commands can be sent on any eSIM Port where CAT
    //         is initialised.
    // If MEP is not supported, the available eUICC interfaces will be treated as separate eUICCs.
    // TODO: Handle MEP-A1 and A2 correctly (maybe force libeuicc to use getEuiccInterfaces()[0] for A1/2?)
    virtual std::vector<std::shared_ptr<EuiccInterface>> euiccInterfaces() const = 0;

    // Remove callback has the physical slot ID.
    virtual void addEuiccInterfacesChangedCallbacks(std::function<void(std::shared_ptr<EuiccInterface>)> addedCallback, std::function<void(int)> removedCallback) = 0;
};

#endif
