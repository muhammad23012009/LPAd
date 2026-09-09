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

#include <lpa.h>
#include <cstring>
#include <thread>
#include <iostream>
#include <errors.h>
#include "drivers/gbinder/gbinder.hpp"

// Runs in a separate thread, the driver is invoked from the same thread as the LPA object. The driver can implement a worker thread internally
// but the driver methods are blocking and synchronous.
// Errors can be thrown with exceptions by the LPA object to propagate as DBus errors

int http_interface_transmit(struct euicc_ctx *ctx, const char *url, uint32_t *rcode, uint8_t **rx,
                                   uint32_t *rx_len, const uint8_t *tx, uint32_t tx_len, const char **h);
LPA::LPA(std::shared_ptr<EuiccInterface> euiccInterface):
  m_ctx(new euicc_ctx()),
  m_euiccInterface(euiccInterface)
{
    euicc_apdu_interface* apdu_interface = new euicc_apdu_interface();
    std::memset(apdu_interface, 0, sizeof(euicc_apdu_interface));

    euicc_http_interface* http_interface = new euicc_http_interface();
    std::memset(http_interface, 0, sizeof(euicc_http_interface));

    apdu_interface->connect = [](euicc_ctx* ctx) -> int {
        auto driver = static_cast<EuiccInterface*>(ctx->userdata);
        return driver->connect();
    };

    apdu_interface->disconnect = [](euicc_ctx* ctx) {
        auto driver = static_cast<EuiccInterface*>(ctx->userdata);
        driver->disconnect();
    };

    apdu_interface->logic_channel_open = [](euicc_ctx* ctx, const uint8_t* aid, uint8_t aid_len) -> int {
        auto driver = static_cast<EuiccInterface*>(ctx->userdata);
        auto aid_copy = (uint8_t*)malloc(aid_len);
        std::memcpy(aid_copy, aid, aid_len);

        auto ptr = EuiccInterface::make_uint8_ptr(aid_copy, aid_len);
        return driver->logicalChannelOpen(std::move(ptr));
    };

    apdu_interface->logic_channel_close = [](euicc_ctx* ctx, uint8_t channel) {
        auto driver = static_cast<EuiccInterface*>(ctx->userdata);
        driver->logicalChannelClose(channel);
    };

    apdu_interface->transmit = [](euicc_ctx* ctx, uint8_t** rx, uint32_t* rx_len, const uint8_t* tx, uint32_t tx_len) -> int {
        auto driver = static_cast<EuiccInterface*>(ctx->userdata);
        uint8_t* tx_copy = (uint8_t*)malloc(tx_len);
        std::memcpy(tx_copy, tx, tx_len);
        auto command = EuiccInterface::make_uint8_ptr(tx_copy, tx_len);
        auto response = driver->transmit(std::move(command));

        *rx_len = response.second;
        *rx = (uint8_t*)malloc(*rx_len);
        std::memcpy(*rx, response.first.get(), *rx_len);

        return 0;
    };

    http_interface->transmit = http_interface_transmit;

    m_ctx->aid = nullptr;
    m_ctx->aid_len = 0;
    m_ctx->es10x_mss = 0;
    m_ctx->userdata = m_euiccInterface.get();
    m_ctx->apdu.interface = apdu_interface;
    m_ctx->http.interface = http_interface;
    m_ctx->apdu.log_fp = stdout;
    m_ctx->http.log_fp = stdout;
}

std::string LPA::getEid()
{
    EuiccLockGuard lock(m_ctx, std::ref(m_mutex));

    char* eidPtr;

    if (es10c_get_eid(m_ctx, &eidPtr) != 0)
        throw std::runtime_error("Failed to get EID");

    auto ret = std::string(eidPtr);
    std::free(eidPtr);

    return ret;
}

EuiccInfo LPA::getEuiccInfo()
{
    EuiccLockGuard lock(m_ctx, std::ref(m_mutex));

    es10c_ex_euiccinfo2 euiccInfo;
    if (es10c_ex_get_euiccinfo2(m_ctx, &euiccInfo) != 0)
        throw std::runtime_error("Failed to get eUICC info");

    EuiccInfo info;
    info.osVersion = euiccInfo.euiccFirmwareVer ? euiccInfo.euiccFirmwareVer : "";
    info.availableMemory = euiccInfo.extCardResource.freeNonVolatileMemory;

    return info;
}

std::vector<EuiccProfile> LPA::getProfiles()
{
    if (m_esimsCached) {
        std::vector<EuiccProfile> ret;
        ret.reserve(m_profiles.size());

        for (const auto& pair : m_profiles) {
            ret.push_back(pair.second);
        }

        return ret;
    }

    EuiccLockGuard lock(m_ctx, std::ref(m_mutex));

    processNotifications();

    es10c_profile_info_list* profile_list = nullptr;
    if (es10c_get_profiles_info(m_ctx, &profile_list) != 0)
        throw std::runtime_error("Failed to get profiles info");

    for (auto current = profile_list; current != nullptr; current = current->next)
    {
        EuiccProfile profile;
        profile.iccid = current->iccid;
        profile.serviceProviderName = current->serviceProviderName ? current->serviceProviderName : "";
        profile.profileName = current->profileName ? current->profileName : "";
        profile.nickname = current->profileNickname ? current->profileNickname : "";
        profile.enabled = (current->profileState == ES10C_PROFILE_STATE_ENABLED);

        m_profiles[profile.iccid] = profile;
    }

    es10c_profile_info_list_free_all(profile_list);

    m_esimsCached = true;
    std::vector<EuiccProfile> ret;
    ret.reserve(m_profiles.size());

    for (const auto& pair : m_profiles) {
        ret.push_back(pair.second);
    }

    return ret;
}

void LPA::enableProfile(const std::string& iccid)
{
    EuiccLockGuard lock(m_ctx, std::ref(m_mutex));

    if (m_profiles[iccid].enabled)
        return;

    if (m_euiccInterface->needsChannelDrop())
    {
        m_euiccInterface->setupRefresh();
    }

    if (es10c_enable_profile(m_ctx, iccid.c_str(), 1) != 0)
        throw LPAException(LPAException::ErrorType::PROFILE_ENABLE_ERROR, "Failed to enable profile");

    if (m_euiccInterface->needsChannelDrop())
    {
        m_euiccInterface->waitForRefresh();
        m_ctx->apdu._internal.logic_channel = -1;
        euicc_init(m_ctx);
    }

    processNotifications();

    m_profiles[iccid].enabled = true;

    if (m_profileChangedCallback)
        m_profileChangedCallback(m_profiles[iccid]);
}

void LPA::disableProfile(const std::string& iccid)
{
    EuiccLockGuard lock(m_ctx, std::ref(m_mutex));

    if (!m_profiles[iccid].enabled)
        return;

    if (m_euiccInterface->needsChannelDrop())
    {
        m_euiccInterface->setupRefresh();
    }

    if (es10c_disable_profile(m_ctx, iccid.c_str(), 1) != 0)
        throw LPAException(LPAException::ErrorType::PROFILE_DISABLE_ERROR, "Failed to disable profile");

    if (m_euiccInterface->needsChannelDrop())
    {
        m_euiccInterface->waitForRefresh();
        m_ctx->apdu._internal.logic_channel = -1;
        euicc_init(m_ctx);
    }

    processNotifications();

    m_profiles[iccid].enabled = false;

    if (m_profileChangedCallback)
        m_profileChangedCallback(m_profiles[iccid]);
}

void LPA::installProfile(const std::string& smdp, const std::string& activationCode, const std::string& confirmationCode, ProfileConfirmationCallback callback)
{
    int ret = 0;
    EuiccProfile profile;
    std::string imei = "012345678901234"; // TODO: Get the actual IMEI from the device
    es10b_load_bound_profile_package_result result;
    es8p_metadata* metadata;

    EuiccLockGuard lock(m_ctx, std::ref(m_mutex));

    m_ctx->http.server_address = smdp.c_str();

    ret = es10b_get_euicc_challenge_and_info(m_ctx);
    if (ret != 0)
        throw LPAException(LPAException::ErrorType::EUICC_CHALLENGE_ERROR);

    // To cancel the session in case we throw an exception
    ES9pGuard guard(m_ctx);
    ret = es9p_initiate_authentication(m_ctx);
    if (ret != 0)
        throw LPAException(LPAException::ErrorType::AUTHENTICATION_INIT_ERROR, std::string(m_ctx->http.status.message));

    ES10bGuard es10bGuard(m_ctx);
    ret = es10b_authenticate_server(m_ctx, activationCode.c_str(), imei.c_str());
    if (ret != 0)
        throw LPAException(LPAException::ErrorType::SERVER_AUTH_ERROR);

    ret = es9p_authenticate_client(m_ctx);
    if (ret != 0)
        throw LPAException(LPAException::ErrorType::CLIENT_AUTH_ERROR, std::string(m_ctx->http.status.message));

    if (m_ctx->http._internal.prepare_download_param->b64_profileMetadata)
    {
        ES8pGuard guard(&metadata);

        es8p_metadata_parse(&metadata, m_ctx->http._internal.prepare_download_param->b64_profileMetadata);
        profile.profileName = metadata->profileName;
        profile.serviceProviderName = metadata->serviceProviderName;
        profile.iccid = metadata->iccid;
        profile.enabled = false;

        bool allowed = callback(profile);
        if (!allowed)
            throw LPAException(LPAException::ErrorType::PROFILE_INSTALL_REJECTED, "Rejected by user");
    }

    ret = es10b_prepare_download(m_ctx, confirmationCode.size() > 0 ? confirmationCode.c_str() : nullptr);
    if (ret)
        throw LPAException(LPAException::ErrorType::PROFILE_DOWNLOAD_ERROR, "Failed to prepare download");

    ret = es9p_get_bound_profile_package(m_ctx);
    if (ret)
        throw LPAException(LPAException::ErrorType::PROFILE_DOWNLOAD_ERROR, "Failed to get bound profile package");

    ret = es10b_load_bound_profile_package(m_ctx, &result);
    if (ret)
        throw LPAException(LPAException::ErrorType::PROFILE_INSTALL_ERROR, "Failed to load bound profile package");

    euicc_http_cleanup(m_ctx);

    processNotifications();

    std::cout << "Profile installed: " << profile.iccid << std::endl;

    m_profiles[profile.iccid] = profile;

    if (m_profileChangedCallback)
        m_profileChangedCallback(profile);
}

void LPA::removeProfile(const std::string& iccid)
{
    EuiccLockGuard lock(m_ctx, std::ref(m_mutex));

    if (es10c_delete_profile(m_ctx, iccid.c_str()) != 0)
        throw LPAException(LPAException::ErrorType::PROFILE_DELETE_ERROR, "Failed to delete profile");

    processNotifications();

    m_profiles.erase(iccid);
    m_esimsCached = false;
}

void LPA::processNotifications()
{
    es10b_notification_metadata_list *notifs = nullptr;

    int maxAttempts = 10;
    while (es10b_list_notification(m_ctx, &notifs) != 0 && maxAttempts-- > 0) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    for (auto *n = notifs; n != nullptr; n = n->next) {
        es10b_pending_notification pending = {0};

        if (es10b_retrieve_notifications_list(m_ctx, &pending, n->seqNumber) != 0) {
            std::cerr << "Failed to retrieve notification seq" << n->seqNumber << std::endl;
            continue;
        }

        m_ctx->http.server_address = pending.notificationAddress;
        int ret = es9p_handle_notification(m_ctx, pending.b64_PendingNotification);
        es10b_pending_notification_free(&pending);

        if (ret == 0) {
            // only remove from the chip once the server has accepted it
            es10b_remove_notification_from_list(m_ctx, n->seqNumber);
            std::cout << "Sent + cleared notification seq" << n->seqNumber
                      << "op" << n->profileManagementOperation << std::endl;
        } else {
            std::cerr << "Server rejected notification seq" << n->seqNumber
                      << "- leaving it on chip for retry" << std::endl;
        }
    }

    es10b_notification_metadata_list_free_all(notifs);
    //euicc_http_cleanup(&m_ctx);
}
