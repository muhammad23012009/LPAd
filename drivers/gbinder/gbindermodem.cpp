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

#include "gbindermodem.h"
#include "gbinderinterface.h"
#include "gbinder.hpp"

#include <iostream>

constexpr const char* HIDL_RADIO_CONFIG_IFACE = "android.hardware.radio.config@1.0::IRadioConfig";
constexpr const char* HIDL_RADIO_CONFIG_RESPONSE_IFACE = "android.hardware.radio.config@1.0::IRadioConfigResponse";

constexpr const char* AIDL_RADIO_CONFIG_IFACE = "android.hardware.radio.config.IRadioConfig";
constexpr const char* AIDL_RADIO_CONFIG_RESPONSE_IFACE = "android.hardware.radio.config.IRadioConfigResponse";

constexpr uint8_t T_MASK = 0b00001111;
constexpr uint8_t T_GLOBAL_IDENTIFIER = 15;

GMainLoop* g_binderLoop = nullptr;
std::vector<SlotInfo> g_slotStatus;
bool g_slotStatusReceived = false;

GBinderLocalReply* radioConfigResponseHandler(GBinderLocalObject *obj, GBinderRemoteRequest *req, guint code,
                                                          guint flags, int *status, void *user_data)
{
    GBinderModem* self = static_cast<GBinderModem*>(user_data);
    GBinderReader reader;
    int error, serial, type;
    gbinder_remote_request_init_reader(req, &reader);

    if (self->m_aidl) {
        binder_read_parcelable_size(&reader);
        gbinder_reader_read_int32(&reader, &type);
        gbinder_reader_read_int32(&reader, &serial);
        gbinder_reader_read_int32(&reader, &error);
    } else {
        const struct radio_response_info *resp = gbinder_reader_read_hidl_struct(&reader, struct radio_response_info);
        error = resp->error;
        serial = resp->serial;
    }

    std::cout << "Received radio config response: code=" << code << ", serial=" << serial << ", error=" << error << std::endl;

    if (error != 0) {
        std::cerr << "Error in radio config response: " << error << std::endl;
    }

    if (code == HIDL_RADIO_CONFIG_GET_SIM_SLOT_STATUS_RESPONSE || code == AIDL_RADIO_CONFIG_GET_SIM_SLOT_STATUS_RESPONSE)
    {
        std::vector<SlotInfo> slots;

        if (self->m_aidl)
        {
            binder_read_parcelable_size(&reader);
            int slotCount;
            gbinder_reader_read_int32(&reader, &slotCount);

            for (int i = 0; i < slotCount; ++i)
            {
                SlotInfo slot;
                std::vector<PortInfo> ports;
                int cardState, portsCount, mepMode;
                char* atr;
                char* eid;

                slot.slotId = i;

                gbinder_reader_read_int32(&reader, &cardState);
                atr = gbinder_reader_read_string16(&reader);
                eid = gbinder_reader_read_string16(&reader);
                gbinder_reader_read_int32(&reader, &portsCount);

                // Read the ports
                for (int i = 0; i < portsCount; ++i)
                {
                    PortInfo port;
                    int logicalSlotId;
                    gboolean currentPort;

                    char* iccid = gbinder_reader_read_string16(&reader);
                    gbinder_reader_read_int32(&reader, &logicalSlotId);
                    gbinder_reader_read_bool(&reader, &currentPort);

                    port.portId = logicalSlotId;
                    port.portType = (eid && strlen(eid) > 0) ? PortType::PORT_TYPE_EUICC : PortType::PORT_TYPE_UICC;
                    port.currentPort = currentPort;
                    ports.push_back(port);
                }

                gbinder_reader_read_int32(&reader, &mepMode);

                slot.ports = ports;
                slots.push_back(slot);
            }
        }
        else
        {
            gsize slotCount;
            const struct SimSlotStatus* slotArr = gbinder_reader_read_hidl_type_vec(&reader, struct SimSlotStatus, &slotCount);
            for (gsize i = 0; i < slotCount; ++i)
            {
                SlotInfo slot;
                slot.slotId = i;
                std::vector<PortInfo> ports;
                std::vector<uint8_t> atr;
                std::string atrHex = slotArr[i].atr.data.str ? std::string(slotArr[i].atr.data.str, slotArr[i].atr.len) : "";
                bool euiccSupported = false;

                atr.reserve(atrHex.length() / 2);

                for (auto i = 0; i < atrHex.length(); i += 2)
                {
                    std::string byteString = atrHex.substr(i, 2);
                    uint8_t byte = static_cast<uint8_t>(std::stoi(byteString, nullptr, 16));
                    atr.push_back(byte);
                }

                int index = 1;
                while (index < atr.size())
                {
                    uint8_t tai, tbi, tci;
                    uint8_t tdi = atr[index];
                    uint8_t type = tdi & T_MASK;
                    bool hasTAi = false, hasTBi = false, hasTCi = false;

                    std::cout << "TD" << index << ": " << std::hex << static_cast<int>(tdi) << std::dec << std::endl;
                    std::cout << "Type: " << static_cast<int>(type) << std::endl;

                    // Has TAi
                    if ((tdi | 0xEF) == 0xFF)
                    {
                        tai = atr[++index];
                        hasTAi = true;
                    }

                    // Has TBi
                    if ((tdi | 0xDF) == 0xFF)
                    {
                        tbi = atr[++index];
                        hasTBi = true;

                        if (type == T_GLOBAL_IDENTIFIER)
                        {
                            uint8_t gID = atr[index];
                            // Check if eUICC is supported
                            euiccSupported = (gID & (1 << 1) && (gID & (1 << 7)));
                        }
                    }

                    // Has TCi
                    if ((tdi | 0xBF) == 0xFF)
                    {
                        tci = atr[++index];
                        hasTCi = true;
                    }

                    // Has TDi
                    if ((tdi | 0x7F) == 0xFF)
                    {
                        index++;
                    }
                    else
                    {
                        std::cout << "ran out of interface bytes" << std::endl;
                        break;
                    }
                }

                PortInfo port;
                port.portId = slotArr[i].logicalSlotId;
                port.portType = euiccSupported ? PortType::PORT_TYPE_EUICC : PortType::PORT_TYPE_UICC;
                port.currentPort = true; // The single port is always the current port
                ports.push_back(port);

                slot.ports = ports;
                slots.push_back(slot);
            }
        }
        g_slotStatusReceived = true;
        g_slotStatus = slots;
    }

    g_main_loop_quit(g_binderLoop);
    return nullptr;
}

GBinderModem::GBinderModem(std::shared_ptr<GBinderServiceManager> sm, GMainLoop* loop, bool aidl):
  m_sm(sm),
  m_loop(loop),
  m_aidl(aidl)
{
    std::string fqname, iface, responseIface;
    int status = 0;

    g_binderLoop = loop;

    if (aidl)
    {
        fqname = std::string(AIDL_RADIO_CONFIG_IFACE) + "/default";
        iface = AIDL_RADIO_CONFIG_IFACE;
        responseIface = AIDL_RADIO_CONFIG_RESPONSE_IFACE;
    }
    else
    {
        fqname = std::string(HIDL_RADIO_CONFIG_IFACE) + "/default";
        iface = HIDL_RADIO_CONFIG_IFACE;
        responseIface = HIDL_RADIO_CONFIG_RESPONSE_IFACE;
    }

    std::cout << "Connecting to " << fqname << "..." << std::endl;

    GContextAcquire contextAcquire(loop);

    m_remote = gbinder_remote_object_ref(gbinder_servicemanager_get_service_sync(m_sm.get(), fqname.c_str(), nullptr));
    m_client = gbinder_client_new(m_remote, iface.c_str());

    auto responseCallback = gbinder_servicemanager_new_local_object(m_sm.get(), responseIface.c_str(), radioConfigResponseHandler, this);
    if (m_aidl)
        gbinder_local_object_set_stability(responseCallback, GBINDER_STABILITY_VINTF);

    auto request = gbinder_client_new_request(m_client);
    GBinderWriter writer;
    gbinder_local_request_init_writer(request, &writer);
    gbinder_writer_append_local_object(&writer, responseCallback);
    gbinder_writer_append_local_object(&writer, nullptr);
    gbinder_client_transact_sync_reply(m_client, aidl ? AIDL_RADIO_CONFIG_SET_RESPONSE_FUNCTIONS : HIDL_RADIO_CONFIG_SET_RESPONSE_FUNCTIONS, request, &status);
    gbinder_local_request_unref(request);

    g_slotStatusReceived = false;

    request = gbinder_client_new_request(m_client);
    gbinder_local_request_init_writer(request, &writer);
    gbinder_writer_append_int32(&writer, 1000);
    gbinder_client_transact_sync_reply(m_client, aidl ? AIDL_RADIO_CONFIG_GET_SIM_SLOT_STATUS : HIDL_RADIO_CONFIG_GET_SIM_SLOT_STATUS, request, &status);
    gbinder_local_request_unref(request);

    while (!g_slotStatusReceived) {
        g_main_loop_run(m_loop);
    }

    m_slotStatus = g_slotStatus;

    // Create the gbinder interfaces for each eUICC slot
    for (const auto& slot : m_slotStatus)
    {
        for (const auto& port : slot.ports)
        {
            if (port.portType == PortType::PORT_TYPE_EUICC && port.currentPort)
            {
                auto euiccInterface = std::make_shared<GBinderInterface>(m_sm, m_loop, slot.slotId, m_aidl);
                m_euiccInterfaces.push_back(euiccInterface);
            }
        }
    }
}

GBinderModem::MEPMode GBinderModem::supportedMEPMode() const
{
    if (!m_aidl)
        // Definitely not
        return MEPMode::NONE;

    return MEPMode::NONE;
}

std::vector<SlotInfo> GBinderModem::physicalSlots() const
{
    return m_slotStatus;
}

void GBinderModem::setPortMapping(int slotId, int portId)
{
}

std::vector<std::shared_ptr<EuiccInterface>> GBinderModem::euiccInterfaces() const
{
    return m_euiccInterfaces;
}

void GBinderModem::addEuiccInterfacesChangedCallbacks(std::function<void(std::shared_ptr<EuiccInterface>)> addedCallback, std::function<void(int)> removedCallback)
{
    // Not implemented for gbinder modem
}
