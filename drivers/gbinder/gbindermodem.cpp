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

#include <algorithm>
#include <iostream>

constexpr const char* HIDL_RADIO_CONFIG_IFACE = "android.hardware.radio.config@1.0::IRadioConfig";
constexpr const char* HIDL_RADIO_CONFIG_RESPONSE_IFACE = "android.hardware.radio.config@1.0::IRadioConfigResponse";

constexpr const char* AIDL_RADIO_CONFIG_IFACE = "android.hardware.radio.config.IRadioConfig";
constexpr const char* AIDL_RADIO_CONFIG_RESPONSE_IFACE = "android.hardware.radio.config.IRadioConfigResponse";

constexpr uint8_t T_MASK = 0b00001111;
constexpr uint8_t T_GLOBAL_IDENTIFIER = 15;

GMainLoop* g_binderLoop = nullptr;
std::vector<PhysicalSlot> g_physicalSlots;
std::vector<AidlSlot> g_aidlSlots;
std::vector<int> g_slotMapping;
bool g_slotStatusReceived = false;
bool g_slotMappingChangeReceived = false;

// TODO: use getPhoneCapability to determine number of logical slots, and default to mapping logical slot0 to phys0, and logi1 to phys1 on AIDL

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
        std::vector<PhysicalSlot> slots;
        // logical slot index <-> physical slot index mapping
        std::vector<int> slotMapping = {-1, -1};

        if (self->m_aidl)
        {
            int slotIndex = 0;
            std::vector<AidlSlot> aidlSlots;
            int slotCount;

            gbinder_reader_read_int32(&reader, &slotCount);

            for (int i = 0; i < slotCount; ++i)
            {
                int cardState, portsCount, mepMode;
                char* atr;
                char* eid;

                binder_read_parcelable_size(&reader);

                gbinder_reader_read_int32(&reader, &cardState);
                atr = gbinder_reader_read_string16(&reader);
                eid = gbinder_reader_read_string16(&reader);
                gbinder_reader_read_int32(&reader, &portsCount);

                // Read the ports
                for (int j = 0; j < portsCount; ++j)
                {
                    PhysicalSlot slot;
                    AidlSlot aidlSlot;
                    int logicalSlotId;
                    int currentPort;

                    binder_read_parcelable_size(&reader);

                    char* iccid = gbinder_reader_read_string16(&reader);
                    gbinder_reader_read_int32(&reader, &logicalSlotId);
                    gbinder_reader_read_int32(&reader, &currentPort);

                    slot.slotId = slotIndex++;
                    slot.type = (eid && strlen(eid) > 0) ? SlotType::SLOT_TYPE_EUICC : SlotType::SLOT_TYPE_UICC;

                    // physical slot index
                    aidlSlot.physicalSlotId = i;
                    // port index
                    aidlSlot.portId = j;
                    // the logical slot this physical slot is mapped to
                    aidlSlot.logicalSlotId = logicalSlotId;
                    aidlSlot.type = slot.type;
                    aidlSlot.current = !!currentPort;

                    slots.push_back(slot);
                    aidlSlots.push_back(aidlSlot);

                    if (logicalSlotId >= 0 && currentPort)
                        slotMapping.insert(slotMapping.begin() + logicalSlotId, slot.slotId);
                }

                gbinder_reader_read_int32(&reader, &mepMode);
            }
            g_aidlSlots = aidlSlots;
        }
        else
        {
            gsize slotCount;
            const struct SimSlotStatus* slotArr = gbinder_reader_read_hidl_type_vec(&reader, struct SimSlotStatus, &slotCount);

            for (gsize i = 0; i < slotCount; ++i)
            {
                PhysicalSlot slot;
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

                slot.slotId = i;
                slot.type = euiccSupported ? SlotType::SLOT_TYPE_EUICC : SlotType::SLOT_TYPE_UICC;

                if (slotArr[i].logicalSlotId >= 0)
                    slotMapping.insert(slotMapping.begin() + slotArr[i].logicalSlotId, i);

                slots.push_back(slot);
            }
        }
        g_slotStatusReceived = true;
        g_physicalSlots = slots;
        g_slotMapping = slotMapping;
    }

    if (code == HIDL_RADIO_CONFIG_SET_SIM_SLOT_MAPPING_RESPONSE || code == AIDL_RADIO_CONFIG_SET_SIM_SLOT_MAPPING_RESPONSE)
    {
        g_slotMappingChangeReceived = true;
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

    m_physicalSlots = g_physicalSlots;
    m_aidlSlots = g_aidlSlots;

    for (auto i = 0; i < g_slotMapping.size(); ++i)
    {
        PhysicalSlot& slot = m_physicalSlots[g_slotMapping[i]];

        if (slot.type == SlotType::SLOT_TYPE_EUICC)
        {
            // Use the logical slot ID to create the gbinder euicc interface
            auto euiccInterface = std::make_shared<GBinderInterface>(m_sm, loop, static_cast<int>(i), m_aidl);
            m_euiccInterfaces.push_back(euiccInterface);
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

std::vector<PhysicalSlot> GBinderModem::getPhysicalSlots() const
{
    return m_physicalSlots;
}

std::vector<LogicalSlot> GBinderModem::getLogicalSlots() const
{
    std::vector<LogicalSlot> logicalSlots;

    for (auto i = 0; i < g_slotMapping.size(); ++i)
    {
        LogicalSlot logicalSlot;
        logicalSlot.slotId = static_cast<int>(i);
        logicalSlot.physicalSlotId = g_slotMapping[i];
        logicalSlots.push_back(logicalSlot);
    }

    return logicalSlots;
}

void GBinderModem::setSlotMapping(int logicalSlotId, int physicalSlotId)
{
    auto request = gbinder_client_new_request(m_client);
    GBinderWriter writer;
    gbinder_local_request_init_writer(request, &writer);
    gbinder_writer_append_int32(&writer, 1000);

    // TODO: Maybe move this logic to ModemInterface itself, and make the driver only set their slot mappings?
    // We first need to check if the requested physical slot is already mapped to a logical slot
    auto it = std::find_if(g_slotMapping.begin(), g_slotMapping.end(), [physicalSlotId](int mappedPhysicalSlotId) {
        return mappedPhysicalSlotId == physicalSlotId;
    });
    if (it != g_slotMapping.end())
    {
        // If it is, then we swap it around
        auto tmp = g_slotMapping[logicalSlotId];
        *it = tmp;
    }

    g_slotMapping[logicalSlotId] = physicalSlotId;

    if (m_aidl)
    {
        gbinder_writer_append_int32(&writer, static_cast<int>(g_slotMapping.size()));

        for (auto physicalSlotId : g_slotMapping)
        {
            gbinder_writer_append_int32(&writer, 1); // non-nullable
            auto written = gbinder_writer_bytes_written(&writer);
            gbinder_writer_append_int32(&writer, -1); // placeholder for size

            try
            {
                AidlSlot slot = m_aidlSlots.value().at(physicalSlotId);
                gbinder_writer_append_int32(&writer, slot.physicalSlotId);
                gbinder_writer_append_int32(&writer, slot.portId);
            }
            catch (const std::out_of_range& e)
            {
                gbinder_writer_append_int32(&writer, -1);
                gbinder_writer_append_int32(&writer, -1);
            }

            gbinder_writer_overwrite_int32(&writer, written, gbinder_writer_bytes_written(&writer) - written);
        }
    }
    else
    {
        gbinder_writer_append_hidl_vec(&writer, g_slotMapping.data(), static_cast<gsize>(g_slotMapping.size()), sizeof(int));
    }

    gbinder_client_transact_sync_oneway(m_client, m_aidl ? AIDL_RADIO_CONFIG_SET_SIM_SLOT_MAPPING : HIDL_RADIO_CONFIG_SET_SIM_SLOT_MAPPING, request);
    gbinder_local_request_unref(request);

    g_slotMappingChangeReceived = false;

    while (!g_slotMappingChangeReceived) {
        g_main_loop_run(m_loop);
    }
}

std::vector<std::shared_ptr<EuiccInterface>> GBinderModem::euiccInterfaces() const
{
    return m_euiccInterfaces;
}

void GBinderModem::addEuiccInterfacesChangedCallbacks(std::function<void(std::shared_ptr<EuiccInterface>)> addedCallback, std::function<void(int)> removedCallback)
{
    // Not implemented for gbinder modem
}
