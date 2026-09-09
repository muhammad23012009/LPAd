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

#include "gbinderinterface.h"
#include "gbinder.hpp"

#include <format>
#include <future>
#include <iostream>

extern "C" {
#define restrict
#include <euicc/hexutil.h>
}

constexpr const char* HIDL_SERVICE_IFACE                = "android.hardware.radio@1.0::IRadio";
constexpr const char* HIDL_SERVICE_IFACE_CALLBACK       = "android.hardware.radio@1.0::IRadioResponse";
constexpr const char* HIDL_SERVICE_IFACE_INDICATIONS    = "android.hardware.radio@1.0::IRadioIndication";

constexpr const char* AIDL_SIM_IFACE       = "android.hardware.radio.sim.IRadioSim";
constexpr const char* AIDL_SIM_RESPONSE    = "android.hardware.radio.sim.IRadioSimResponse";
constexpr const char* AIDL_SIM_INDICATION  = "android.hardware.radio.sim.IRadioSimIndication";

static const GBinderWriterField sim_apdu_f[] = {GBINDER_WRITER_FIELD_HIDL_STRING(struct sim_apdu, data),
                                                GBINDER_WRITER_FIELD_END()};

static const GBinderWriterType sim_apdu_t = {GBINDER_WRITER_STRUCT_NAME_AND_SIZE(struct sim_apdu), sim_apdu_f};

static int g_channelId = -1;
static icc_io_result g_lastIccIoResult = {0};

static bool g_openReady = false;
static bool g_channelClosed = false;
static bool g_transmitResponseReady = false;
static bool g_refreshReceived = false;
static bool g_cardStatusReady = false;
static bool g_cardStatusReceived = false;
static GMainLoop *g_binderLoop = nullptr;

GBinderLocalReply* radioResponseHandler(GBinderLocalObject *obj, GBinderRemoteRequest *req, guint code,
                                                          guint flags, int *status, void *user_data)
{
    GBinderInterface* self = static_cast<GBinderInterface*>(user_data);
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
        type = resp->type;
        std::cout << "Received radio response. Type: " << type << ", Serial: " << serial << ", Error: " << error << std::endl;
        std::cout << "Transaction code: " << code << ", Flags: " << flags << std::endl;
    }

    if (error != 0) {
        std::cerr << "Error in radio response. Error code: " << error << std::endl;
    }

    if (code == HIDL_SERVICE_ICC_OPEN_LOGICAL_CHANNEL_CALLBACK || code == AIDL_SERVICE_ICC_OPEN_LOGICAL_CHANNEL_CALLBACK) {
        std::cout << "Received response for IRadio::iccOpenLogicalChannel1" << std::endl;
        gbinder_reader_read_int32(&reader, &g_channelId);
        g_openReady = true;
        if (error != 0)
            g_channelId = -1;

    } else if (code == HIDL_SERVICE_ICC_TRANSMIT_APDU_LOGICAL_CHANNEL_CALLBACK || code == AIDL_SERVICE_ICC_TRANSMIT_APDU_LOGICAL_CHANNEL_CALLBACK) {
        if (self->m_aidl) {
            binder_read_parcelable_size(&reader);
            int sw1, sw2;
            gbinder_reader_read_int32(&reader, &sw1);
            gbinder_reader_read_int32(&reader, &sw2);
            char* hex = gbinder_reader_read_string16(&reader);
            g_lastIccIoResult.sw1 = sw1;
            g_lastIccIoResult.sw2 = sw2;
            g_lastIccIoResult.simResponse.data.str = hex;
            g_lastIccIoResult.simResponse.len = strlen(hex);
            g_lastIccIoResult.simResponse.owns_buffer = TRUE;
            g_transmitResponseReady = true;
            std::cout << "Received response for Radio::iccTransmitApduLogicalChannel, hex: " << g_lastIccIoResult.simResponse.data.str << std::endl;
        } else {
            const icc_io_result *icc_io_res = gbinder_reader_read_hidl_struct(&reader, struct icc_io_result);
            g_lastIccIoResult.sw1 = icc_io_res->sw1;
            g_lastIccIoResult.sw2 = icc_io_res->sw2;
            g_lastIccIoResult.simResponse.data.str = strndup(icc_io_res->simResponse.data.str, icc_io_res->simResponse.len);
            g_lastIccIoResult.simResponse.len = icc_io_res->simResponse.len;
            g_lastIccIoResult.simResponse.owns_buffer = TRUE;
            g_transmitResponseReady = true;
            std::cout << "Received response for IRadio::iccTransmitApduLogicalChannel, hex: " << g_lastIccIoResult.simResponse.data.str << std::endl;
        }

    } else if (code == HIDL_SERVICE_ICC_CLOSE_LOGICAL_CHANNEL_CALLBACK || code == AIDL_SERVICE_ICC_CLOSE_LOGICAL_CHANNEL_CALLBACK) {
        std::cout << "Received response for IRadio::iccCloseLogicalChannel" << std::endl;
        g_channelClosed = true;

    } else if (code == HIDL_SERVICE_GET_ICC_CARD_STATUS_CALLBACK || code == AIDL_SERVICE_GET_ICC_CARD_STATUS_CALLBACK) {
        int cardState = 0;
        std::cout << "Received response for IRadio::getIccCardStatus" << std::endl;
        if (self->m_aidl) {
            binder_read_parcelable_size(&reader);
            gbinder_reader_read_int32(&reader, &cardState);
        } else {
            const card_status* cardStatus = gbinder_reader_read_hidl_struct(&reader, struct card_status);
            cardState = cardStatus->cardState;
        }

        g_cardStatusReceived = true;

        if (cardState == 1) {
            std::cout << "Card state is present. Card state: " << cardState << std::endl;
            g_cardStatusReady = true;
            // Doesn't hurt to do this
            g_refreshReceived = true;
        } else {
            std::cout << "Card state is not present. Card state: " << cardState << std::endl;
        }

    } else {
        std::cerr << "Received unknown radio response code: " << code << std::endl;
        return nullptr;
    }

    g_main_loop_quit(g_binderLoop);
    return nullptr;
}

GBinderLocalReply* radioIndicationHandler(GBinderLocalObject *obj, GBinderRemoteRequest *req, guint code,
                                                         guint flags, int *status, void *user_data)
{
    GBinderInterface *self = static_cast<GBinderInterface *>(user_data);
    GBinderReader reader;
    gbinder_remote_request_init_reader(req, &reader);

    std::cout << "Received radio indication. Code: " << code << ", Flags: " << flags << std::endl;

    // SIM state change indication
    // code 19 is simStateChanged on HIDL, code 6 is simStateChanged on AIDL
    if (code == 19 || code == 6) {
        std::cout << "sim state changed" << std::endl;
        // If we're still waiting on simRefresh, just wake up and let it query the card status instead
        g_refreshReceived = true;
        g_main_loop_quit(g_binderLoop);
    }

    // SIM refresh indication
    // code 17 is simRefresh on HIDL, code 5 is simRefresh on AIDL
    if (code == 17 || code == 5) {
        int indicationType = 0;
        gbinder_reader_read_int32(&reader, &indicationType);
        const sim_refresh_result* refreshResult = gbinder_reader_read_hidl_struct(&reader, struct sim_refresh_result);
        // SIM_INIT == 1 / SIM_RESET == 2
        if (refreshResult->type == 1 || refreshResult->type == 2) {
            // Wake up the waiting thread and let it query the card status
            g_refreshReceived = true;
            std::cout << "SIM refresh indication received. Type: " << refreshResult->type << ", EF ID: " << refreshResult->efId << std::endl;
            g_main_loop_quit(g_binderLoop);
            // We will wake up the wait_for_refresh() function in sim state change signal
        }
    }

    return nullptr;
}

GBinderInterface::GBinderInterface(std::shared_ptr<GBinderServiceManager> sm, GMainLoop* loop, int slotId, bool aidl):
  m_sm(sm),
  m_loop(loop),
  m_slotId(slotId),
  m_aidl(aidl)
{
    std::cout << "Creating gbinder interface for slot " << slotId << std::endl;
    g_binderLoop = loop;
}

void GBinderInterface::setupRefresh()
{
    g_refreshReceived = false;
    g_cardStatusReady = false;
    g_cardStatusReceived = false;
}

void GBinderInterface::waitForRefresh()
{
    std::cout << "Waiting for SIM refresh indication..." << std::endl;

    while (!g_refreshReceived) {
        g_main_loop_run(m_loop);
    }

    g_refreshReceived = false;

    std::cout << "SIM refresh indication received. Querying card status..." << std::endl;

    // Now check for the card status itself
    while (!g_cardStatusReady) {
        getCardStatus();
        g_main_loop_run(m_loop);

        if (!g_cardStatusReady) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    std::cout << "Card status is ready after refresh." << std::endl;
}

int GBinderInterface::connect()
{
    return 0;
}

void GBinderInterface::disconnect()
{
}

int GBinderInterface::logicalChannelOpen(Uint8Ptr data)
{
    g_channelClosed = false;

    auto future = std::async(std::launch::async, [this, data = std::move(data)]() -> int {
        GContextAcquire contextAcquire(m_loop);

        std::string simIface = m_aidl ? AIDL_SIM_IFACE : HIDL_SERVICE_IFACE;
        std::string simResponse = m_aidl ? AIDL_SIM_RESPONSE : HIDL_SERVICE_IFACE_CALLBACK;
        std::string simIndication = m_aidl ? AIDL_SIM_INDICATION : HIDL_SERVICE_IFACE_INDICATIONS;
        std::string fqname = simIface + "/slot" + std::to_string(m_slotId + 1);
        int status;

        m_remote = gbinder_remote_object_ref(gbinder_servicemanager_get_service_sync(m_sm.get(), fqname.c_str(), &status));
        if (!m_remote) {
            return -1;
        }

        m_client = gbinder_client_new(m_remote, simIface.c_str());
        if (!m_client) {
            gbinder_remote_object_unref(m_remote);
            return -1;
        }

        auto responseCallback = gbinder_servicemanager_new_local_object(m_sm.get(), simResponse.c_str(), radioResponseHandler, this);
        auto indicationCallback = gbinder_servicemanager_new_local_object(m_sm.get(), simIndication.c_str(), radioIndicationHandler, this);
        if (m_aidl) {
            gbinder_local_object_set_stability(responseCallback, GBINDER_STABILITY_VINTF);
            gbinder_local_object_set_stability(indicationCallback, GBINDER_STABILITY_VINTF);
        }

        auto request = gbinder_client_new_request(m_client);
        GBinderWriter writer;
        gbinder_local_request_init_writer(request, &writer);
        gbinder_writer_append_local_object(&writer, responseCallback);
        gbinder_writer_append_local_object(&writer, indicationCallback);
        gbinder_client_transact_sync_reply(m_client, m_aidl ? AIDL_SERVICE_SET_RESPONSE_FUNCTIONS : HIDL_SERVICE_SET_RESPONSE_FUNCTIONS, request, &status);
        gbinder_local_request_unref(request);

        if (status < 0) {
            // Handle error
            gbinder_client_unref(m_client);
            gbinder_remote_object_unref(m_remote);
            return -1;
        }

        // Now, try to open the AID
        std::string aid_hex;
        for (size_t i = 0; i < data.second; ++i) {
            aid_hex += std::format("{:02X}", data.first.get()[i]);
        }

        request = gbinder_client_new_request(m_client);
        gbinder_local_request_init_writer(request, &writer);
        gbinder_writer_append_int32(&writer, 1000);

        if (m_aidl) {
            gbinder_writer_append_string16(&writer, aid_hex.c_str());
        } else {
            gbinder_writer_append_hidl_string_copy(&writer, aid_hex.c_str());
        }

        gbinder_writer_append_int32(&writer, 0);
        status = gbinder_client_transact_sync_oneway(m_client, m_aidl ? AIDL_SERVICE_ICC_OPEN_LOGICAL_CHANNEL : HIDL_SERVICE_ICC_OPEN_LOGICAL_CHANNEL, request);
        gbinder_local_request_unref(request);

        if (status < 0) {
            std::cerr << "Failed to call IRadio::iccOpenLogicalChannel: " << status << std::endl;
            gbinder_client_unref(m_client);
            gbinder_remote_object_unref(m_remote);
            return -1;
        }

        while (!g_openReady) {
            g_main_loop_run(m_loop);
        }

        std::cout << "Logical channel opened with ID: " << g_channelId << std::endl;

        return g_channelId;
    });

    return future.get();
}

void GBinderInterface::logicalChannelClose(int channel)
{
    if (g_channelId < 0) {
        std::cerr << "No logical channel to close." << std::endl;
        return;
    }

    auto future = std::async(std::launch::async, [this]() {
        GContextAcquire contextAcquire(m_loop);

        auto request = gbinder_client_new_request(m_client);
        GBinderWriter writer;
        gbinder_local_request_init_writer(request, &writer);
        gbinder_writer_append_int32(&writer, 1000);
        gbinder_writer_append_int32(&writer, g_channelId);
        gbinder_client_transact_sync_oneway(m_client, m_aidl ? AIDL_SERVICE_ICC_CLOSE_LOGICAL_CHANNEL : HIDL_SERVICE_ICC_CLOSE_LOGICAL_CHANNEL, request);
        gbinder_local_request_unref(request);

        g_channelId = -1;
        g_openReady = false;

        while (!g_channelClosed) {
            g_main_loop_run(m_loop);
        }
    });

    future.get();
}

GBinderInterface::Uint8Ptr GBinderInterface::transmit(Uint8Ptr command)
{
    g_transmitResponseReady = false;

    auto future = std::async(std::launch::async, [this, command = std::move(command)]() -> Uint8Ptr {
        GContextAcquire contextAcquire(m_loop);

        auto request = gbinder_client_new_request(m_client);
        GBinderWriter writer;
        gbinder_local_request_init_writer(request, &writer);
        gbinder_writer_append_int32(&writer, 1000);

        std::cout << "Transmitting APDU command of length: " << command.second << std::endl;

        // TODO: Drop this
        uint8_t* tx = command.first.get();
        uint8_t tx_hex[4096] = {0};
        euicc_hexutil_bin2hex((char *)tx_hex, 4096, &tx[5], command.second - 5);

        if (m_aidl) {
            // AIDL parcelable
            gbinder_writer_append_int32(&writer, 1); // nonnull
            auto start = gbinder_writer_bytes_written(&writer);
            gbinder_writer_append_int32(&writer, -1); // placeholder for size
            gbinder_writer_append_int32(&writer, g_channelId);   // sessionId
            gbinder_writer_append_int32(&writer, tx[0]);         // cla
            gbinder_writer_append_int32(&writer, tx[1]);         // instruction
            gbinder_writer_append_int32(&writer, tx[2]);         // p1
            gbinder_writer_append_int32(&writer, tx[3]);         // p2
            gbinder_writer_append_int32(&writer, tx[4]);         // p3
            gbinder_writer_append_string16(&writer, (char*)tx_hex);     // data (String16)
            gbinder_writer_append_bool(&writer, FALSE);          // isEs10 / trailing field

            // fill the parcelable size field
            gbinder_writer_overwrite_int32(&writer, start, gbinder_writer_bytes_written(&writer) - start);
        } else {
            sim_apdu apdu = {
                .sessionId = g_channelId,
                .cla = tx[0],
                .instruction = tx[1],
                .p1 = tx[2],
                .p2 = tx[3],
                .p3 = tx[4],
                .data =
                {
                    .data = {.str = (char*)tx_hex},
                    .len = static_cast<guint32>(strlen((char*)tx_hex) + 1),
                    .owns_buffer = FALSE,
                },
            };

            gbinder_writer_append_struct(&writer, &apdu, &sim_apdu_t, NULL);
        }

        std::cout << "m_client: " << m_client << std::endl;

        int status = gbinder_client_transact_sync_oneway(m_client, m_aidl ? AIDL_SERVICE_ICC_TRANSMIT_APDU_LOGICAL_CHANNEL : HIDL_SERVICE_ICC_TRANSMIT_APDU_LOGICAL_CHANNEL, request);
        gbinder_local_request_unref(request);

        std::cout << "Waiting for response from IRadio::iccTransmitApduLogicalChannel..." << std::endl;

        while (!g_transmitResponseReady) {
            g_main_loop_run(m_loop);
        }

        size_t response_len = g_lastIccIoResult.simResponse.len / 2 + 2;
        uint8_t* response_bytes = (uint8_t*)std::malloc(response_len);

        try {
            hex2bin(g_lastIccIoResult.simResponse.data.str, (char*)response_bytes);
        } catch (const std::invalid_argument& e) {
            std::cerr << "Error converting hex to binary: " << e.what() << std::endl;
            std::free(response_bytes);
            return make_uint8_ptr(nullptr, size_t{0});
        }

        response_bytes[response_len - 2] = static_cast<uint8_t>(g_lastIccIoResult.sw1);
        response_bytes[response_len - 1] = static_cast<uint8_t>(g_lastIccIoResult.sw2);

        std::free((void*)g_lastIccIoResult.simResponse.data.str);

        return make_uint8_ptr(response_bytes, response_len);
    });

    return future.get();
}

void GBinderInterface::getCardStatus()
{
    g_cardStatusReceived = false;

    auto request = gbinder_client_new_request(m_client);
    GBinderWriter writer;
    gbinder_local_request_init_writer(request, &writer);
    gbinder_writer_append_int32(&writer, 1000);
    gbinder_client_transact_sync_oneway(m_client, m_aidl ? AIDL_SERVICE_GET_ICC_CARD_STATUS : HIDL_SERVICE_GET_ICC_CARD_STATUS, request);
    gbinder_local_request_unref(request);
}
