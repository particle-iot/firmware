/*
 * Copyright (c) 2018 Particle Industries, Inc.  All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cellular_hal.h"

#include "network/cellular_network_manager.h"
#include "network/cellular_ncp_client.h"
#include "network/ncp.h"
#include "ifapi.h"

#include "system_network.h" // FIXME: For network_interface_index

#include "scope_guard.h"
#include "endian.h"
#include "check.h"

namespace {

using namespace particle;

} // unnamed

int cellular_on(void* reserved) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

int cellular_init(void* reserved) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

int cellular_off(void* reserved) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

int cellular_register(void* reserved) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

int cellular_pdp_activate(CellularCredentials* connect, void* reserved) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

int cellular_pdp_deactivate(void* reserved) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

int cellular_gprs_attach(CellularCredentials* connect, void* reserved) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

int cellular_gprs_detach(void* reserved) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

int cellular_fetch_ipconfig(CellularConfig* conf, void* reserved) {
    if_t iface = nullptr;
    CHECK(if_get_by_index(NETWORK_INTERFACE_CELLULAR, &iface));
    CHECK_TRUE(iface, SYSTEM_ERROR_INVALID_STATE);
    unsigned flags = 0;
    CHECK(if_get_flags(iface, &flags));
    CHECK_TRUE((flags & IFF_UP) && (flags & IFF_LOWER_UP), SYSTEM_ERROR_INVALID_STATE);
    // IP address
    if_addrs* ifAddrList = nullptr;
    CHECK(if_get_addrs(iface, &ifAddrList));
    SCOPE_GUARD({
        if_free_if_addrs(ifAddrList);
    });
    if_addr* ifAddr = nullptr;
    for (if_addrs* i = ifAddrList; i; i = i->next) {
        if (i->if_addr->addr->sa_family == AF_INET) { // Skip non-IPv4 addresses
            ifAddr = i->if_addr;
            break;
        }
    }
    auto sockAddr = (const sockaddr_in*)ifAddr->addr;
    CHECK_TRUE(sockAddr, SYSTEM_ERROR_INVALID_STATE);
    static_assert(sizeof(conf->nw.aucIP.ipv4) == sizeof(sockAddr->sin_addr), "");
    memcpy(&conf->nw.aucIP.ipv4, &sockAddr->sin_addr, sizeof(sockAddr->sin_addr));
    conf->nw.aucIP.ipv4 = reverseByteOrder(conf->nw.aucIP.ipv4);
    conf->nw.aucIP.v = 4;
    // Subnet mask
    sockAddr = (const sockaddr_in*)ifAddr->netmask;
    CHECK_TRUE(sockAddr, SYSTEM_ERROR_INVALID_STATE);
    memcpy(&conf->nw.aucSubnetMask.ipv4, &sockAddr->sin_addr, sizeof(sockAddr->sin_addr));
    conf->nw.aucSubnetMask.ipv4 = reverseByteOrder(conf->nw.aucSubnetMask.ipv4);
    conf->nw.aucSubnetMask.v = 4;
    // Peer address
    sockAddr = (const sockaddr_in*)ifAddr->peeraddr;
    CHECK_TRUE(sockAddr, SYSTEM_ERROR_INVALID_STATE);
    memcpy(&conf->nw.aucDefaultGateway.ipv4, &sockAddr->sin_addr, sizeof(sockAddr->sin_addr));
    conf->nw.aucDefaultGateway.ipv4 = reverseByteOrder(conf->nw.aucDefaultGateway.ipv4);
    conf->nw.aucDefaultGateway.v = 4;
    return 0;
}

int cellular_device_info(CellularDevice* info, void* reserved) {
    const auto mgr = cellularNetworkManager();
    CHECK_TRUE(mgr, SYSTEM_ERROR_UNKNOWN);
    const auto client = mgr->ncpClient();
    CHECK_TRUE(client, SYSTEM_ERROR_UNKNOWN);
    CHECK(client->getIccid(info->iccid, sizeof(info->iccid)));
    CHECK(client->getImei(info->imei, sizeof(info->imei)));
    return 0;
}

int cellular_credentials_set(const char* apn, const char* user, const char* password, void* reserved) {
    const auto mgr = cellularNetworkManager();
    CHECK_TRUE(mgr, SYSTEM_ERROR_UNKNOWN);
    auto sim = particle::SimType::INTERNAL;
    CHECK(mgr->getActiveSim(&sim));
    auto cred = CellularNetworkConfig().apn(apn).user(user).password(password);
    CHECK(mgr->setNetworkConfig(sim, std::move(cred)));
    return 0;
}

CellularCredentials* cellular_credentials_get(void* reserved) {
    // TODO: Copy the settings to a storage provided by the calling code
    static CellularCredentials cred;
    static CellularNetworkConfig conf;
    const auto mgr = cellularNetworkManager();
    if (!mgr) {
        return nullptr;
    }
    auto sim = particle::SimType::INTERNAL;
    CHECK_RETURN(mgr->getActiveSim(&sim), nullptr);
    CHECK_RETURN(mgr->getNetworkConfig(sim, &conf), nullptr);
    cred.apn = conf.hasApn() ? conf.apn() : "";
    cred.username = conf.hasUser() ? conf.user() : "";
    cred.password = conf.hasPassword() ? conf.password() : "";
    return &cred;
}

bool cellular_sim_ready(void* reserved) {
    return false;
}

void cellular_cancel(bool cancel, bool calledFromISR, void* reserved) {
}

int cellular_signal(CellularSignalHal* signal, cellular_signal_t* reserved) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

int cellular_command(_CALLBACKPTR_MDM cb, void* param, system_tick_t timeout_ms, const char* format, ...) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

int cellular_data_usage_set(CellularDataHal* data, void* reserved) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

int cellular_data_usage_get(CellularDataHal* data, void* reserved) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

int cellular_sms_received_handler_set(_CELLULAR_SMS_CB_MDM cb, void* data, void* reserved) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

void HAL_USART3_Handler_Impl(void* reserved) {
}

int cellular_pause(void* reserved) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

int cellular_resume(void* reserved) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

int cellular_imsi_to_network_provider(void* reserved) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

CellularNetProvData cellular_network_provider_data_get(void* reserved) {
    return CellularNetProvData();
}

int cellular_lock(void* reserved) {
    const auto mgr = cellularNetworkManager();
    CHECK_TRUE(mgr, SYSTEM_ERROR_UNKNOWN);
    const auto client = mgr->ncpClient();
    CHECK_TRUE(client, SYSTEM_ERROR_UNKNOWN);
    client->lock();
    return 0;
}

void cellular_unlock(void* reserved) {
    const auto mgr = cellularNetworkManager();
    if (mgr) {
        const auto client = mgr->ncpClient();
        if (client) {
            client->unlock();
        }
    }
}

void cellular_set_power_mode(int mode, void* reserved) {
}

int cellular_band_select_set(MDM_BandSelect* bands, void* reserved) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

int cellular_band_select_get(MDM_BandSelect* bands, void* reserved) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

int cellular_band_available_get(MDM_BandSelect* bands, void* reserved) {
    return SYSTEM_ERROR_NOT_SUPPORTED;
}

int cellular_set_active_sim(int simType, void* reserved) {
    const auto mgr = cellularNetworkManager();
    CHECK_TRUE(mgr, SYSTEM_ERROR_UNKNOWN);
    auto sim = particle::SimType::INTERNAL;
    if (simType == SIM_TYPE_EXTERNAL) {
        sim = particle::SimType::EXTERNAL;
    }
    CHECK(mgr->setActiveSim(sim));
    return 0;
}

int cellular_get_active_sim(int* simType, void* reserved) {
    const auto mgr = cellularNetworkManager();
    CHECK_TRUE(mgr, SYSTEM_ERROR_UNKNOWN);
    auto sim = particle::SimType::INTERNAL;
    CHECK(mgr->getActiveSim(&sim));
    if (sim == particle::SimType::EXTERNAL) {
        *simType = SIM_TYPE_EXTERNAL;
    } else {
        *simType = SIM_TYPE_INTERNAL;
    }
    return 0;
}
