/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#ifndef BTIF_SS_GATT_SERVER_H
#define BTIF_SS_GATT_SERVER_H
#include <hardware/bluetooth.h>
#include <hardware/bt_gatt.h>

class btif_ss_gatt_server {
public :
    bt_status_t postTxMessage(std::string msgStr);
    bt_status_t registerServer(const bluetooth::Uuid& bt_uuid, bool eatt_support);
    bt_status_t connect(int server_if, const RawAddress& bd_addr,bool is_direct, int transport);
    bt_status_t disconnect(int server_if, const RawAddress& bd_addr,int conn_id);
    bt_status_t unregisterServer(int server_if);
    bt_status_t readPhy(const RawAddress& bd_addr, base::Callback<void(uint8_t tx_phy, uint8_t rx_phy, uint8_t status)> cb);
    bt_status_t setPhy(const RawAddress& bd_addr,uint8_t tx_phy, uint8_t rx_phy,uint16_t phy_options);
    bt_status_t clearService(int server_if,int srvcHandle);
    bt_status_t sendIndicationNotification(int attribute_handle,int conn_id,int confirm, const std::vector<uint8_t> value,int server_if);
    bt_status_t sendResponse(int conn_id, int trans_id,int status,const btgatt_response_t& response);
    bt_status_t AddService(int server_if,std::vector<btgatt_db_element_t> service);
    bt_status_t stopService(int server_if,int srvcHandle);
};

void btif_server_ss_callback(uint16_t event, char* p_param);

#endif /* BTIF_SS_GATT_SERVER_H*/