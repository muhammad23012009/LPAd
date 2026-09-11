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

#ifndef GBINDERMODEM_H
#define GBINDERMODEM_H

#include <optional>

#include <gbinder.h>
#include <modem_interface.h>

// Same code for response too
#define AIDL_RADIO_CONFIG_GET_SIM_SLOT_STATUS           4
#define AIDL_RADIO_CONFIG_GET_SIM_SLOT_STATUS_RESPONSE  AIDL_RADIO_CONFIG_GET_SIM_SLOT_STATUS
#define AIDL_RADIO_CONFIG_SET_RESPONSE_FUNCTIONS        7

#define AIDL_RADIO_CONFIG_SET_SIM_SLOT_MAPPING          8
#define AIDL_RADIO_CONFIG_SET_SIM_SLOT_MAPPING_RESPONSE     7

#define HIDL_RADIO_CONFIG_SET_RESPONSE_FUNCTIONS        GBINDER_FIRST_CALL_TRANSACTION
#define HIDL_RADIO_CONFIG_GET_SIM_SLOT_STATUS           (GBINDER_FIRST_CALL_TRANSACTION + 1)
#define HIDL_RADIO_CONFIG_GET_SIM_SLOT_STATUS_RESPONSE  (GBINDER_FIRST_CALL_TRANSACTION)

#define HIDL_RADIO_CONFIG_SET_SIM_SLOT_MAPPING          (GBINDER_FIRST_CALL_TRANSACTION + 2)
#define HIDL_RADIO_CONFIG_SET_SIM_SLOT_MAPPING_RESPONSE     (GBINDER_FIRST_CALL_TRANSACTION + 1)

// HIDL IRadioConfig@1.0 struct
struct SimSlotStatus
{
    int32_t cardState;
    int32_t slotState;
    GBinderHidlString atr;
    uint32_t logicalSlotId;
    GBinderHidlString iccid;
};

struct AidlSlot
{
    int physicalSlotId;
    int portId;
    int logicalSlotId;
    SlotType type;
    bool current;
};

class GBinderModem : public ModemInterface
{
public:
    GBinderModem(std::shared_ptr<GBinderServiceManager> sm, GMainLoop* loop, bool aidl);
    ~GBinderModem() = default;

    std::string modemName() const override { return "gbinder0"; }

    MEPMode supportedMEPMode() const override;

    std::vector<PhysicalSlot> getPhysicalSlots() const override;

    std::vector<LogicalSlot> getLogicalSlots() const override;

    void setSlotMapping(int logicalSlotId, int physicalSlotId) override;

    std::vector<std::shared_ptr<EuiccInterface>> euiccInterfaces() const override;

    void addEuiccInterfacesChangedCallbacks(std::function<void(std::shared_ptr<EuiccInterface>)> addedCallback, std::function<void(int)> removedCallback) override;

    bool m_aidl;

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

    std::shared_ptr<GBinderServiceManager> m_sm;
    GBinderRemoteObject* m_remote = nullptr;
    GBinderClient* m_client = nullptr;
    GMainLoop* m_loop;

    // Used to lookup the actual (physical_slot_id, port_id) pair from a PhysicalSlot struct with its index in the vector returned by getPhysicalSlots()
    std::optional<std::vector<AidlSlot>> m_aidlSlots;

    std::vector<PhysicalSlot> m_physicalSlots;

    std::vector<std::shared_ptr<EuiccInterface>> m_euiccInterfaces;
};

#endif
