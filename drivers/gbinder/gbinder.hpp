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

#ifndef GBINDER_HPP
#define GBINDER_HPP

#include <gbinder.h>
#include <glib.h>

#include <driver_interface.h>

// ref: IRadio
#define HIDL_SERVICE_SET_RESPONSE_FUNCTIONS GBINDER_FIRST_CALL_TRANSACTION
#define HIDL_SERVICE_GET_ICC_CARD_STATUS (GBINDER_FIRST_CALL_TRANSACTION + 1)
#define HIDL_SERVICE_ICC_OPEN_LOGICAL_CHANNEL (GBINDER_FIRST_CALL_TRANSACTION + 105)
#define HIDL_SERVICE_ICC_CLOSE_LOGICAL_CHANNEL (GBINDER_FIRST_CALL_TRANSACTION + 106)
#define HIDL_SERVICE_ICC_TRANSMIT_APDU_LOGICAL_CHANNEL (GBINDER_FIRST_CALL_TRANSACTION + 107)
#define HIDL_SERVICE_SET_SIM_POWER (GBINDER_FIRST_CALL_TRANSACTION + 128)

// ref: IRadioResponse
#define HIDL_SERVICE_GET_ICC_CARD_STATUS_CALLBACK GBINDER_FIRST_CALL_TRANSACTION
#define HIDL_SERVICE_ICC_OPEN_LOGICAL_CHANNEL_CALLBACK (GBINDER_FIRST_CALL_TRANSACTION + 104)
#define HIDL_SERVICE_ICC_CLOSE_LOGICAL_CHANNEL_CALLBACK (GBINDER_FIRST_CALL_TRANSACTION + 105)
#define HIDL_SERVICE_ICC_TRANSMIT_APDU_LOGICAL_CHANNEL_CALLBACK (GBINDER_FIRST_CALL_TRANSACTION + 106)
#define HIDL_SERVICE_SET_SIM_POWER_CALLBACK (GBINDER_FIRST_CALL_TRANSACTION + 127)

#define AIDL_SERVICE_SET_RESPONSE_FUNCTIONS 28
#define AIDL_SERVICE_GET_ICC_CARD_STATUS 9
#define AIDL_SERVICE_ICC_OPEN_LOGICAL_CHANNEL 15
#define AIDL_SERVICE_ICC_CLOSE_LOGICAL_CHANNEL 13
#define AIDL_SERVICE_ICC_TRANSMIT_APDU_LOGICAL_CHANNEL 17

#define AIDL_SERVICE_GET_ICC_CARD_STATUS_CALLBACK 10
#define AIDL_SERVICE_ICC_OPEN_LOGICAL_CHANNEL_CALLBACK 16
#define AIDL_SERVICE_ICC_CLOSE_LOGICAL_CHANNEL_CALLBACK 14
#define AIDL_SERVICE_ICC_TRANSMIT_APDU_LOGICAL_CHANNEL_CALLBACK 18

struct icc_io_result {
    int32_t sw1;
    int32_t sw2;
    GBinderHidlString simResponse;
};

struct sim_apdu {
    int32_t sessionId;
    int32_t cla;
    int32_t instruction;
    int32_t p1;
    int32_t p2;
    int32_t p3;
    GBinderHidlString data;
};

struct app_status {
    int32_t appType;
    int32_t appState;
    int32_t persoSubstate;          // applicable only if app_state == SUBSCRIPTION_PERSO
    GBinderHidlString aidPtr;       // aid hex string
    GBinderHidlString appLabelPtr;
    int32_t pin1Replaced;
    int32_t pin1;
    int32_t pin2;
};

struct card_status {
    int32_t cardState;
    int32_t universalPinState;
    int32_t gsmUmtsSubscriptionAppIndex;
    int32_t cdmaSubscriptionAppIndex;
    int32_t imsSubscriptionAppIndex;
    // Vector of AppState
    GBinderHidlVec applications;
};

struct radio_response_info {
    int32_t type;
    int32_t serial;
    int32_t error;
};

struct sim_refresh_result {
    int32_t type;
    int efId;
    GBinderHidlString aid;
};

class GBinderDriver : public DriverInterface
{
public:
    static bool usable();

    GBinderDriver();

    bool needsChannelDrop() override { return true; }
    void setupRefresh() override;
    void waitForRefresh() override;

    int connect() override { return 0; }
    void disconnect() override {}
    int logicalChannelOpen(const uint8_t* aid, uint8_t aid_len) override;
    void logicalChannelClose(int channel) override;
    std::pair<Uint8Ptr, size_t> transmit(std::pair<Uint8Ptr, size_t>) override;

    bool m_aidl = false;

private:
    class GContextAcquire {
    public:
        GContextAcquire(GMainLoop *loop) {
            m_context = g_main_loop_get_context(loop);
            g_main_context_acquire(m_context);
        }
        ~GContextAcquire() {
            g_main_context_release(m_context);
        }
    private:
        GMainContext *m_context;
    };

    void getCardStatus();

    GBinderServiceManager* m_sm = nullptr;
    GBinderRemoteObject* m_remote = nullptr;
    GBinderClient* m_client = nullptr;
};

#endif
