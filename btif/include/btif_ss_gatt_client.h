/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef BTIF_SS_GATT_CLIENT_H
#define BTIF_SS_GATT_CLIENT_H
#include <hardware/bluetooth.h>
#include <string.h>
#include "btif_gatt.h"

class gatt_client_single_stack_proto {
public :
    void PrintMessage(std::string msgStr);
    bt_status_t postTxMessage(std::string msgStr);
    bt_status_t registerClient(const bluetooth::Uuid& bt_uuid, bool eatt_supported);
    bt_status_t deregisterClient(uint32_t client_if);
    bt_status_t connect(uint32_t client_if, const RawAddress& bd_addr, bool is_direct, int transport, bool opportunistic, int phy);
    bt_status_t disconnect(uint32_t client_if, const RawAddress& bd_addr, uint32_t conn_id);
    bt_status_t searchServices(uint32_t conn_id, const bluetooth::Uuid& filter_uuid);
    bt_status_t discoverServicesByUuid(uint32_t conn_id, const bluetooth::Uuid uuid);
    bt_status_t refreshServices(uint32_t client_if, const RawAddress& bd_addr);
    bt_status_t readCharacteristicValue(uint32_t conn_id, uint32_t handle, int auth_req);
    bt_status_t readCharacteristicValueUsingUuid(uint32_t conn_id, const bluetooth::Uuid& attr_uuid,
        uint32_t start_handle, uint32_t end_handle, int auth_req);
    bt_status_t readDescriptorValue(uint32_t conn_id, uint32_t handle, int auth_req);
    bt_status_t writeCharacteristicValue(uint32_t conn_id, uint32_t handle,
                                int write_type, int auth_req,
                                const std::vector<uint8_t>& value);
    bt_status_t writeDescriptorValue(uint32_t conn_id, uint32_t handle,
                            int auth_req, const std::vector<uint8_t>& value);
    bt_status_t executeWrite(uint32_t conn_id, int execute);
    bt_status_t registerNotifications(uint32_t client_if,
                            const RawAddress& bd_addr, uint32_t handle);
    bt_status_t deregisterNotifications(uint32_t client_if,
                            const RawAddress& bd_addr, uint32_t handle);
    bt_status_t readRemoteRssi(uint32_t client_if, const RawAddress& bd_addr);
    bt_status_t configureMtu(uint32_t conn_id, uint32_t mtu);
    bt_status_t connParamUpdate(const RawAddress& bd_addr,
                        uint32_t min_interval, uint32_t max_interval,
                        uint32_t latency, uint32_t timeout,
                        uint32_t min_ce_len, uint32_t max_ce_len);
    bt_status_t setPhy(const RawAddress& bd_addr, int tx_phy,
                int rx_phy, int phy_options);
    bt_status_t readPhy(const RawAddress& bd_addr,
        base::Callback<void(uint8_t tx_phy, uint8_t rx_phy, uint8_t status)> cb);

    bt_status_t getGattDb(uint32_t conn_id);
};
void btif_ss_gatt_client_callback(uint16_t event, char* p_param);

#endif /* BTIF_SS_GATT_CLIENT_H */
