/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "btif_ss_gatt_client.h"
#include "protobuf/proto/gatt_client.pb.h"
#include "btif/protobuf/include/proto_message_ids.h"
#include "osi/include/log.h"
#include "btif_common.h"
#include "btif_api.h"
#include <string>
#include <map>
#include <hardware/bluetooth.h>
#include <hardware/bt_gatt.h>
#include <base/bind.h>
#include "btif_util.h"
#include "raw_address.h"
#include "btif_ss_interface.h"

using namespace singlestack::proto::gattclient;

/*******************************************************************************
  *  Static variables
******************************************************************************/
BluetoothSSInterface* ss_gatt_client_interface = NULL;

std::map<RawAddress,
        base::Callback<void(uint8_t tx_phy, uint8_t rx_phy, uint8_t status)>> ReadPhyCbMap;
std::map<RawAddress, uint32_t> connectedDevices;

/*******************************************************************************
  *  Externs
******************************************************************************/
extern const btgatt_callbacks_t* bt_gatt_callbacks;

/*******************************************************************************
  *  Functions
******************************************************************************/
void btif_ss_gatt_client_init() {
  ALOGI("%s ", __func__);
  if (ss_gatt_client_interface == NULL) {
    ss_gatt_client_interface = BluetoothSSInterface::getInstance();
    if (ss_gatt_client_interface == NULL) {
      ALOGE("%s single stack interface Initialization failed", __func__);
    }
  } else {
    ALOGI("single stack interface is already created");
  }
  if (ss_gatt_client_interface != NULL) {
    ALOGI("%s: registering Gatt (Client) profile callback with ss_interface", __func__);
    ss_gatt_client_interface->registerCallbacks(BT_PROFILE_ID_GATTC,
                                       btif_ss_gatt_client_callback);
  }
}

void btif_ss_gatt_client_deinit() {
  ALOGI("%s ", __func__);
  if (ss_gatt_client_interface != NULL) {
    ss_gatt_client_interface->deregisterCallbacks(BT_PROFILE_ID_GATTC);
  }
  if (ss_gatt_client_interface == NULL) {
    ALOGE("single stack interface is already null");
  } else {
    ss_gatt_client_interface = NULL;
  }
}

bt_status_t gatt_client_single_stack_proto::postTxMessage(std::string msgStr) {
  ALOGD("%s ", __func__);
  /* Write to glink */
  if (ss_gatt_client_interface != NULL) {
    ss_gatt_client_interface->postTxMsg(msgStr);
  } else {
    return BT_STATUS_FAIL;
  }
  return BT_STATUS_SUCCESS;
}


bt_status_t gatt_client_single_stack_proto::registerClient(const bluetooth::Uuid& bt_uuid,
                                    bool eatt_supported) {
    std::string msgStr;

    ALOGD(" %s ", __func__);
    ss_gatt_client_register ss_gatt_client_register_;
    ss_gatt_client_register_.set_appuuid(bt_uuid.ToString());
    ss_gatt_client_register_.set_eattsupported(eatt_supported);

    if(!ss_gatt_client_register_.SerializeToString(&msgStr)) {
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_REGISTER, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::connect(uint32_t client_if, const RawAddress& bd_addr,
                                    bool is_direct, int transport,
                                    bool opportunistic, int phy) {
    std::string msgStr;

    ALOGD(" %s ", __func__);
    ALOGV("client if %lu ", (unsigned long)client_if);
    ss_gatt_client_connect ss_gatt_client_connect_;
    ss_gatt_client_connect_.set_clientif(client_if);
    ss_gatt_client_connect_.set_address(ToRawString(&bd_addr).c_str());
    ss_gatt_client_connect_.set_isdirect(is_direct);
    ss_gatt_client_connect_.set_transport(transport);
    ss_gatt_client_connect_.set_opportunistic(opportunistic);
    ss_gatt_client_connect_.set_initphy(phy);

    if(!ss_gatt_client_connect_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_CONNECT, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::disconnect(uint32_t client_if, const RawAddress& bd_addr,
                                    uint32_t conn_id) {
    std::string msgStr;
    ALOGD(" %s ", __func__);
    ss_gatt_client_disconnect ss_gatt_client_disconnect_;
    ss_gatt_client_disconnect_.set_clientif(client_if);
    ss_gatt_client_disconnect_.set_address(ToRawString(&bd_addr).c_str());
    ss_gatt_client_disconnect_.set_connid(conn_id);

    if(!ss_gatt_client_disconnect_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_DISCONNECT, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::deregisterClient(uint32_t client_if) {
    std::string msgStr;
    ALOGD(" %s ", __func__);
    ss_gatt_client_deregister ss_gatt_client_deregister_;
    ss_gatt_client_deregister_.set_clientif(client_if);

    if(!ss_gatt_client_deregister_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_DEREGISTER, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::searchServices(uint32_t conn_id,
                                    const bluetooth::Uuid& filter_uuid) {
    std::string msgStr;
    ALOGD(" %s ", __func__);
    ss_gatt_client_search_services ss_gatt_client_search_services_;
    ss_gatt_client_search_services_.set_connid(conn_id);
    ss_gatt_client_search_services_.set_filteruuid(bluetooth::Uuid::kEmpty.ToString());

    if(!ss_gatt_client_search_services_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_SEARCH_SERVICES, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::discoverServicesByUuid(uint32_t conn_id,
                                    const bluetooth::Uuid uuid) {
    std::string msgStr;
    ALOGD(" %s ", __func__);
    ss_gatt_client_search_services ss_gatt_client_search_services_;
    ss_gatt_client_search_services_.set_connid(conn_id);
    ss_gatt_client_search_services_.set_filteruuid(uuid.ToString());

    if(!ss_gatt_client_search_services_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_SEARCH_SERVICES_BY_UUID, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::refreshServices(uint32_t client_if,
                                            const RawAddress& bd_addr) {
    std::string msgStr;
    ALOGD(" %s ", __func__);
    ss_gatt_client_refresh_services ss_gatt_client_refresh_services_;
    ss_gatt_client_refresh_services_.set_clientif(client_if);
    ss_gatt_client_refresh_services_.set_address(ToRawString(&bd_addr).c_str());

    if(!ss_gatt_client_refresh_services_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_REFRESH, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::readCharacteristicValue(uint32_t conn_id, uint32_t handle,
                                    int auth_req) {
    std::string msgStr;
    ALOGD(" %s ", __func__);
    ss_gatt_client_read_char_desc ss_gatt_client_read_char_desc_;
    ss_gatt_client_read_char_desc_.set_connid(conn_id);
    ss_gatt_client_read_char_desc_.set_attrhdl(handle);
    ss_gatt_client_read_char_desc_.set_authreq(auth_req);

    if(!ss_gatt_client_read_char_desc_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_READ_CHAR, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::readCharacteristicValueUsingUuid(uint32_t conn_id,
                                    const bluetooth::Uuid& attr_uuid, uint32_t start_handle,
                                    uint32_t end_handle, int auth_req) {
    std::string msgStr;
    ALOGD(" %s ", __func__);
    ss_gatt_client_read_char_by_uuid ss_gatt_client_read_char_by_uuid_;
    ss_gatt_client_read_char_by_uuid_.set_connid(conn_id);
    ss_gatt_client_read_char_by_uuid_.set_attruuid(attr_uuid.ToString());
    ss_gatt_client_read_char_by_uuid_.set_starthdl(start_handle);
    ss_gatt_client_read_char_by_uuid_.set_endhdl(end_handle);
    ss_gatt_client_read_char_by_uuid_.set_authreq(auth_req);

    if(!ss_gatt_client_read_char_by_uuid_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_READ_CHAR_BY_UUID, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::readDescriptorValue(uint32_t conn_id, uint32_t handle,
                                            int auth_req) {
    std::string msgStr;
    ALOGD(" %s ", __func__);
    ss_gatt_client_read_char_desc ss_gatt_client_read_char_desc_;
    ss_gatt_client_read_char_desc_.set_connid(conn_id);
    ss_gatt_client_read_char_desc_.set_attrhdl(handle);
    ss_gatt_client_read_char_desc_.set_authreq(auth_req);

    if(!ss_gatt_client_read_char_desc_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_READ_DESC, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::getGattDb(uint32_t conn_id) {
    std::string msgStr;
    ALOGD(" %s ", __func__);
    ss_gatt_client_get_gatt_db ss_gatt_client_get_gatt_db_;
    ss_gatt_client_get_gatt_db_.set_connid(conn_id);

    if(!ss_gatt_client_get_gatt_db_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_GET_GATT_DB, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::writeCharacteristicValue(uint32_t conn_id, uint32_t handle,
                                    int write_type, int auth_req,
                                    const std::vector<uint8_t>& value) {
    std::string msgStr;
    ALOGD(" %s ", __func__);
    std::string strValue(value.begin(), value.end());
    ss_gatt_client_write_char_desc ss_gatt_client_write_char_desc_;
    ss_gatt_client_write_char_desc_.set_connid(conn_id);
    ss_gatt_client_write_char_desc_.set_attrhdl(handle);
    ss_gatt_client_write_char_desc_.set_writetype(write_type);
    ss_gatt_client_write_char_desc_.set_authreq(auth_req);
    ss_gatt_client_write_char_desc_.set_value(strValue);
    ss_gatt_client_write_char_desc_.set_valuelen(strValue.length());

    if(!ss_gatt_client_write_char_desc_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_WRITE_CHAR, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::writeDescriptorValue(uint32_t conn_id, uint32_t handle,
                                    int auth_req, const std::vector<uint8_t>& value) {
    std::string msgStr;
    ALOGD(" %s ", __func__);
    std::string strValue(value.begin(), value.end());
    ss_gatt_client_write_char_desc ss_gatt_client_write_char_desc_;
    ss_gatt_client_write_char_desc_.set_connid(conn_id);
    ss_gatt_client_write_char_desc_.set_attrhdl(handle);
    ss_gatt_client_write_char_desc_.set_writetype(0);
    ss_gatt_client_write_char_desc_.set_authreq(auth_req);
    ss_gatt_client_write_char_desc_.set_value(strValue);
    ss_gatt_client_write_char_desc_.set_valuelen(strValue.length());

    if(!ss_gatt_client_write_char_desc_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_WRITE_DESC, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::executeWrite(uint32_t conn_id, int execute) {
    std::string msgStr;
    ALOGD(" %s ", __func__);
    ss_gatt_client_execute_write ss_gatt_client_execute_write_;
    ss_gatt_client_execute_write_.set_connid(conn_id);
    ss_gatt_client_execute_write_.set_execute(execute);

    if(!ss_gatt_client_execute_write_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_EXEC_WRITE, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::registerNotifications(uint32_t client_if,
                                    const RawAddress& bd_addr, uint32_t handle) {
    std::string msgStr;
    ALOGD(" %s ", __func__);
    ss_gatt_client_reg_dereg_notifications ss_gatt_client_reg_dereg_notifications_;
    ss_gatt_client_reg_dereg_notifications_.set_clientif(client_if);
    ss_gatt_client_reg_dereg_notifications_.set_address(ToRawString(&bd_addr).c_str());
    ss_gatt_client_reg_dereg_notifications_.set_attrhdl(handle);

    if(!ss_gatt_client_reg_dereg_notifications_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_REGISTER_NOTIFICATIONS, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::deregisterNotifications(uint32_t client_if,
                                    const RawAddress& bd_addr, uint32_t handle) {
    std::string msgStr;
    ALOGD(" %s ", __func__);
    ss_gatt_client_reg_dereg_notifications ss_gatt_client_reg_dereg_notifications_;
    ss_gatt_client_reg_dereg_notifications_.set_clientif(client_if);
    ss_gatt_client_reg_dereg_notifications_.set_address(ToRawString(&bd_addr).c_str());
    ss_gatt_client_reg_dereg_notifications_.set_attrhdl(handle);

    if(!ss_gatt_client_reg_dereg_notifications_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_DEREGISTER_NOTIFICATIONS, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}
bt_status_t gatt_client_single_stack_proto::readRemoteRssi(uint32_t client_if,
                                            const RawAddress& bd_addr) {
    std::string msgStr;
    ALOGD(" %s ", __func__);
    ss_gatt_client_read_remote_rssi ss_gatt_client_read_remote_rssi_;
    ss_gatt_client_read_remote_rssi_.set_clientif(client_if);
    ss_gatt_client_read_remote_rssi_.set_address(ToRawString(&bd_addr).c_str());

    if(!ss_gatt_client_read_remote_rssi_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_READ_RSSI, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::configureMtu(uint32_t conn_id, uint32_t mtu) {
    std::string msgStr;
    ALOGD(" %s ", __func__);
    ss_gatt_client_mtu_update ss_gatt_client_mtu_update_;
    ss_gatt_client_mtu_update_.set_connid(conn_id);
    ss_gatt_client_mtu_update_.set_mtu(mtu);

    if(!ss_gatt_client_mtu_update_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_CONFIGURE_MTU, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::connParamUpdate(const RawAddress& bd_addr,
                                    uint32_t min_interval, uint32_t max_interval,
                                    uint32_t latency, uint32_t timeout,
                                    uint32_t min_ce_len, uint32_t max_ce_len) {
    std::string msgStr;
    ALOGD(" %s ", __func__);
    ss_gatt_client_conn_param_update ss_gatt_client_conn_param_update_;
    ss_gatt_client_conn_param_update_.set_address(ToRawString(&bd_addr).c_str());
    ss_gatt_client_conn_param_update_.set_mininterval(min_interval);
    ss_gatt_client_conn_param_update_.set_maxinterval(max_interval);
    ss_gatt_client_conn_param_update_.set_latency(latency);
    ss_gatt_client_conn_param_update_.set_conntimeout(timeout);
    ss_gatt_client_conn_param_update_.set_mincelen(min_ce_len);
    ss_gatt_client_conn_param_update_.set_maxcelen(max_ce_len);

    if(!ss_gatt_client_conn_param_update_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_CONN_PARAM_UPDATE, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::setPhy(const RawAddress& bd_addr, int tx_phy,
                                   int rx_phy, int phy_options) {
    std::string msgStr;
    ALOGD(" %s ", __func__);
    ss_gatt_client_set_phy ss_gatt_client_set_phy_;
    ss_gatt_client_set_phy_.set_address(ToRawString(&bd_addr).c_str());
    ss_gatt_client_set_phy_.set_txphy(tx_phy);
    ss_gatt_client_set_phy_.set_rxphy(rx_phy);
    ss_gatt_client_set_phy_.set_phyoptions(phy_options);

    if(!ss_gatt_client_set_phy_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_SET_PHY, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    return status;
}

bt_status_t gatt_client_single_stack_proto::readPhy(const RawAddress& bd_addr,
    base::Callback<void(uint8_t tx_phy, uint8_t rx_phy, uint8_t status)> cb) {
    std::string msgStr;
    uint32_t client_if;
    ALOGD(" %s ", __func__);
    for (auto conn : connectedDevices) {
        if (bd_addr == conn.first) {
          client_if = conn.second;
          break;
        }
    }
    ss_gatt_client_read_phy ss_gatt_client_read_phy_;
    ss_gatt_client_read_phy_.set_address(ToRawString(&bd_addr).c_str());
    ss_gatt_client_read_phy_.set_clientif(client_if);

    if(!ss_gatt_client_read_phy_.SerializeToString(&msgStr)){
        ALOGE(" %s: failed to serialize ", __func__);
    }
    uint16_t encoded_len = msgStr.length();
    std::string packet = FormTxPacket(BT_LE_GATT_CLIENT_READ_PHY, PROTO_ENC_DEC,
                                   encoded_len, msgStr);
    bt_status_t status = postTxMessage(packet);
    if(status == BT_STATUS_SUCCESS) {
        ReadPhyCbMap.insert(
        std::pair<RawAddress,
        base::Callback<void(uint8_t tx_phy, uint8_t rx_phy, uint8_t status)> >(bd_addr, cb));
    }
    return status;
}

/*******************************************************************************
  *  Callbacks
******************************************************************************/
void process_gatt_client_registered_event(std::string resBufferString) {
    int status;
    uint32_t client_if;
    bluetooth::Uuid app_uuid;

    ss_gatt_client_registered_event on_ss_gatt_client_registered;
    on_ss_gatt_client_registered.ParseFromString(resBufferString);

    if (on_ss_gatt_client_registered.has_status()) {
      status = on_ss_gatt_client_registered.status();
      ALOGD("\n status: 0x%d ", status);
    }
    if (on_ss_gatt_client_registered.has_clientif()) {
      client_if = on_ss_gatt_client_registered.clientif();
      ALOGD("\n client_if: %lu ", (unsigned long)client_if);
    }
    if (on_ss_gatt_client_registered.has_appuuid()) {
      app_uuid = bluetooth::Uuid::FromString(on_ss_gatt_client_registered.appuuid());
      ALOGD("\n app_uuid: %s ", app_uuid.ToString().c_str());
    }
    HAL_CBACK(bt_gatt_callbacks, client->register_client_cb, status,
                client_if, app_uuid);
}

void process_gatt_client_connected_event(std::string resBufferString) {
    int status;
    uint32_t conn_id;
    uint32_t client_if;
    RawAddress* bd_address = nullptr;

    ss_gatt_client_conn_state_change_event on_ss_gatt_client_conn_state_change;
    on_ss_gatt_client_conn_state_change.ParseFromString(resBufferString);

    if (on_ss_gatt_client_conn_state_change.has_status()) {
        status = on_ss_gatt_client_conn_state_change.status();
        ALOGD("\n status: 0x%d ", status);
    }

    if (on_ss_gatt_client_conn_state_change.has_clientif()) {
        client_if = on_ss_gatt_client_conn_state_change.clientif();
        ALOGD("\n client_if: %lu ", (unsigned long)client_if);
    }

    if (on_ss_gatt_client_conn_state_change.has_connid()) {
        conn_id = on_ss_gatt_client_conn_state_change.connid();
        ALOGD("\n conn_id: %d ", conn_id);
    }

    if (on_ss_gatt_client_conn_state_change.has_address()) {
        uint8_t* addr = (uint8_t*)on_ss_gatt_client_conn_state_change.address().c_str();
        bd_address = (RawAddress*)addr;
        ALOGD("\n address: %s ", bd_address->ToString().c_str());
    }

    if (bd_address != nullptr) {
        connectedDevices.insert(std::pair<RawAddress, uint32_t>(*bd_address, client_if));
        HAL_CBACK(bt_gatt_callbacks, client->open_cb, conn_id, status,
                client_if, *bd_address);
    } else {
        ALOGE("bd_address is null");
    }
}

void process_gatt_client_disconnected_event(std::string resBufferString) {
    int status;
    uint32_t conn_id;
    uint32_t client_if;
    RawAddress* address = nullptr;

    ss_gatt_client_conn_state_change_event on_ss_gatt_client_conn_state_change;
    on_ss_gatt_client_conn_state_change.ParseFromString(resBufferString);

    if (on_ss_gatt_client_conn_state_change.has_status()) {
      status = on_ss_gatt_client_conn_state_change.status();
      ALOGD("\n status: 0x%d ", status);
    }
    if (on_ss_gatt_client_conn_state_change.has_clientif()) {
      client_if = on_ss_gatt_client_conn_state_change.clientif();
      ALOGD("\n client_if: %lu ", (unsigned long)client_if);
    }
    if (on_ss_gatt_client_conn_state_change.has_connid()) {
      conn_id = on_ss_gatt_client_conn_state_change.connid();
      ALOGD("\n conn_id: %d ", conn_id);
    }
    if (on_ss_gatt_client_conn_state_change.has_address()) {
       uint8_t* addr = (uint8_t*)on_ss_gatt_client_conn_state_change.address().c_str();
       address = (RawAddress*)addr;
       ALOGD("\n address: %s ", address->ToString().c_str());
    }
    connectedDevices.erase(*address);
    HAL_CBACK(bt_gatt_callbacks, client->close_cb, conn_id, status,
                client_if, *address);
}

void process_gatt_client_read_phy_event(std::string resBufferString) {
    uint8_t status;
    uint32_t client_if;
    uint8_t tx_phy;
    uint8_t rx_phy;
    RawAddress* address = nullptr;

    ss_gatt_client_phy_read_event on_ss_gatt_client_phy_read_event;
    on_ss_gatt_client_phy_read_event.ParseFromString(resBufferString);

    if (on_ss_gatt_client_phy_read_event.has_status()) {
      status = on_ss_gatt_client_phy_read_event.status();
      ALOGD("\n status: 0x%d ", status);
    }
    if (on_ss_gatt_client_phy_read_event.has_clientif()) {
      client_if = on_ss_gatt_client_phy_read_event.clientif();
      ALOGD("\n client_if: %lu ", (unsigned long)client_if);
    }
    if (on_ss_gatt_client_phy_read_event.has_txphy()) {
      tx_phy = on_ss_gatt_client_phy_read_event.txphy();
      ALOGD("\n tx phy: %d ", tx_phy);
    }
    if (on_ss_gatt_client_phy_read_event.has_rxphy()) {
      rx_phy = on_ss_gatt_client_phy_read_event.rxphy();
      ALOGD("\n rx phy: %d ", rx_phy);
    }
    if (on_ss_gatt_client_phy_read_event.has_address()) {
        uint8_t* addr = (uint8_t*)on_ss_gatt_client_phy_read_event.address().c_str();
       address = (RawAddress*)addr;
       ALOGD("\n address: %s ", address->ToString().c_str());
    }
    for (auto pair : ReadPhyCbMap) {
        if (*address == pair.first) {
          do_in_jni_thread(Bind(pair.second, tx_phy, rx_phy, status));
          ReadPhyCbMap.erase(*address);
          break;
        }
    }
}

void process_gatt_client_search_completed_event(std::string resBufferString) {
    int status;
    uint32_t conn_id;

    ss_gatt_client_search_completed_event on_ss_gatt_client_search_completed_event;
    on_ss_gatt_client_search_completed_event.ParseFromString(resBufferString);

    if (on_ss_gatt_client_search_completed_event.has_status()) {
      status = on_ss_gatt_client_search_completed_event.status();
      ALOGD("\n status: 0x%d ", status);
    }
    if (on_ss_gatt_client_search_completed_event.has_connid()) {
      conn_id = on_ss_gatt_client_search_completed_event.connid();
      ALOGD("\n conn id: %lu ", (unsigned long)conn_id);
    }
    HAL_CBACK(bt_gatt_callbacks, client->search_complete_cb, conn_id, status);
}

void process_gatt_client_reg_dereg_notif_event(std::string resBufferString) {
    int status;
    uint32_t conn_id;
    uint8_t registered;
    uint32_t attr_hdl;

    ss_gatt_client_notifi_reg_dereg_event on_ss_gatt_client_notifi_reg_dereg_event;
    on_ss_gatt_client_notifi_reg_dereg_event.ParseFromString(resBufferString);

    if (on_ss_gatt_client_notifi_reg_dereg_event.has_status()) {
      status = on_ss_gatt_client_notifi_reg_dereg_event.status();
      ALOGD("\n status: 0x%d ", status);
    }
    if (on_ss_gatt_client_notifi_reg_dereg_event.has_connid()) {
      conn_id = on_ss_gatt_client_notifi_reg_dereg_event.connid();
      ALOGD("\n conn id: %lu ", (unsigned long)conn_id);
    }
    if (on_ss_gatt_client_notifi_reg_dereg_event.has_registered()) {
      registered = on_ss_gatt_client_notifi_reg_dereg_event.registered();
      ALOGD("\n registered: %d ", registered);
    }
    if (on_ss_gatt_client_notifi_reg_dereg_event.has_attrhdl()) {
      attr_hdl = on_ss_gatt_client_notifi_reg_dereg_event.attrhdl();
      ALOGD("\n attr_hdl: %lu ", (unsigned long)attr_hdl);
    }
    HAL_CBACK(bt_gatt_callbacks, client->register_for_notification_cb,
                conn_id, registered, status, attr_hdl);
}

void process_gatt_client_notify_event(std::string resBufferString) {
    btgatt_notify_params_t data;
    RawAddress* address = nullptr;
    uint32_t conn_id;

    ss_gatt_client_on_notify_event on_ss_gatt_client_on_notify_event;
    on_ss_gatt_client_on_notify_event.ParseFromString(resBufferString);

    if (on_ss_gatt_client_on_notify_event.has_address()) {
        uint8_t* addr = (uint8_t*)on_ss_gatt_client_on_notify_event.address().c_str();
        address = (RawAddress*)addr;
        memcpy(&(data.bda), address, RawAddress::kLength);
        ALOGD("\n address: %s ", data.bda.ToString().c_str());
    }
    if (on_ss_gatt_client_on_notify_event.has_connid()) {
      conn_id = on_ss_gatt_client_on_notify_event.connid();
      ALOGD("\n conn id: %lu ", (unsigned long)conn_id);
    }
    if (on_ss_gatt_client_on_notify_event.has_attrhdl()) {
      data.handle = on_ss_gatt_client_on_notify_event.attrhdl();
      ALOGD("\n handle: %d ", data.handle);
    }
    if (on_ss_gatt_client_on_notify_event.has_valuelen()) {
      data.len = on_ss_gatt_client_on_notify_event.valuelen();
      ALOGD("\n len: %d ", data.len);
    }
    if (on_ss_gatt_client_on_notify_event.has_isnotify()) {
      data.is_notify = on_ss_gatt_client_on_notify_event.isnotify();
      ALOGD("\n is_notify: %d ", data.is_notify);
    }
    if (on_ss_gatt_client_on_notify_event.has_value()) {
        if(data.len > 0 && data.len <= BTGATT_MAX_ATTR_LEN) {
            memcpy(data.value, on_ss_gatt_client_on_notify_event.value().c_str(), data.len);
        }
    }
    HAL_CBACK(bt_gatt_callbacks, client->notify_cb, conn_id, data);
}

void process_gatt_client_read_char_event(std::string resBufferString) {
    btgatt_read_params_t data;
    uint32_t conn_id;

    ss_gatt_client_read_char_desc_event on_ss_gatt_client_read_char_desc_event;
    on_ss_gatt_client_read_char_desc_event.ParseFromString(resBufferString);

    if (on_ss_gatt_client_read_char_desc_event.has_status()) {
      data.status = on_ss_gatt_client_read_char_desc_event.status();
      ALOGD("\n status: 0x%d ", data.status);
    }
    if (on_ss_gatt_client_read_char_desc_event.has_connid()) {
      conn_id = on_ss_gatt_client_read_char_desc_event.connid();
      ALOGD("\n conn id: %lu ", (unsigned long)conn_id);
    }
    if (on_ss_gatt_client_read_char_desc_event.has_attrhdl()) {
      data.handle = on_ss_gatt_client_read_char_desc_event.attrhdl();
      ALOGD("\n handle: %d ", data.handle);
    }
    if (on_ss_gatt_client_read_char_desc_event.has_valuelen()) {
      data.value.len = on_ss_gatt_client_read_char_desc_event.valuelen();
      ALOGD("\n len: %d ", data.value.len);
    }
    if (on_ss_gatt_client_read_char_desc_event.has_valuetype()) {
      data.value_type = on_ss_gatt_client_read_char_desc_event.valuetype();
      ALOGD("\n valuetype: %d ", data.value_type);
    }
    if (on_ss_gatt_client_read_char_desc_event.has_value()) {
        if(data.value.len > 0 && data.value.len <= BTGATT_MAX_ATTR_LEN) {
            memcpy(data.value.value, on_ss_gatt_client_read_char_desc_event.value().c_str(),
                    data.value.len);
            ALOGD("\n data.value: %s ", data.value.value);
            ALOGD("\n value: %s ", on_ss_gatt_client_read_char_desc_event.value().c_str());
        }
    }
    HAL_CBACK(bt_gatt_callbacks, client->read_characteristic_cb,
                conn_id, data.status, &data);
}

void process_gatt_client_write_char_event(std::string resBufferString) {
    int status;
    uint32_t conn_id;
    uint32_t attr_hdl;
    uint32_t value_len;
    uint8_t value[BTGATT_MAX_ATTR_LEN];

    ss_gatt_client_write_char_desc_event on_ss_gatt_client_write_char_desc_event;
    on_ss_gatt_client_write_char_desc_event.ParseFromString(resBufferString);

    if (on_ss_gatt_client_write_char_desc_event.has_status()) {
      status = on_ss_gatt_client_write_char_desc_event.status();
      ALOGD("\n status: 0x%d ", status);
    }
    if (on_ss_gatt_client_write_char_desc_event.has_connid()) {
      conn_id = on_ss_gatt_client_write_char_desc_event.connid();
      ALOGD("\n conn id: %lu ", (unsigned long)conn_id);
    }
    if (on_ss_gatt_client_write_char_desc_event.has_attrhdl()) {
      attr_hdl = on_ss_gatt_client_write_char_desc_event.attrhdl();
      ALOGD("\n handle:%lu ", (unsigned long)attr_hdl);
    }
    if (on_ss_gatt_client_write_char_desc_event.has_valuelen()) {
      value_len = on_ss_gatt_client_write_char_desc_event.valuelen();
      ALOGD("\n value_len: %lu ", (unsigned long)value_len);
    }
    if (on_ss_gatt_client_write_char_desc_event.has_value()) {
        memcpy(value, on_ss_gatt_client_write_char_desc_event.value().c_str(), value_len);
        ALOGD("\n data.value: %s ", value);
        ALOGD("\n value: %s ", on_ss_gatt_client_write_char_desc_event.value().c_str());
    }
    HAL_CBACK(bt_gatt_callbacks, client->write_characteristic_cb,
                conn_id, status, attr_hdl, value_len, value);
}

void process_gatt_client_exec_write_event(std::string resBufferString) {
    int status;
    uint32_t conn_id;

    ss_gatt_client_execute_write_event on_ss_gatt_client_execute_write_event;
    on_ss_gatt_client_execute_write_event.ParseFromString(resBufferString);

    if (on_ss_gatt_client_execute_write_event.has_status()) {
      status = on_ss_gatt_client_execute_write_event.status();
      ALOGD("\n status: 0x%d ", status);
    }
    if (on_ss_gatt_client_execute_write_event.has_connid()) {
      conn_id = on_ss_gatt_client_execute_write_event.connid();
      ALOGD("\n conn id: %lu ", (unsigned long)conn_id);
    }
    HAL_CBACK(bt_gatt_callbacks, client->execute_write_cb,
                conn_id, status);
}

void process_gatt_client_read_desc_event(std::string resBufferString) {
    btgatt_read_params_t data;
    uint32_t conn_id;

    ss_gatt_client_read_char_desc_event on_ss_gatt_client_read_char_desc_event;
    on_ss_gatt_client_read_char_desc_event.ParseFromString(resBufferString);

    if (on_ss_gatt_client_read_char_desc_event.has_status()) {
      data.status = on_ss_gatt_client_read_char_desc_event.status();
      ALOGD("\n status: 0x%d ", data.status);
    }
    if (on_ss_gatt_client_read_char_desc_event.has_connid()) {
      conn_id = on_ss_gatt_client_read_char_desc_event.connid();
      ALOGD("\n conn id: %lu ", (unsigned long)conn_id);
    }
    if (on_ss_gatt_client_read_char_desc_event.has_attrhdl()) {
      data.handle = on_ss_gatt_client_read_char_desc_event.attrhdl();
      ALOGD("\n handle: %d ", data.handle);
    }
    if (on_ss_gatt_client_read_char_desc_event.has_valuelen()) {
      data.value.len = on_ss_gatt_client_read_char_desc_event.valuelen();
      ALOGD("\n len: %d ", data.value.len);
    }
    if (on_ss_gatt_client_read_char_desc_event.has_valuetype()) {
      data.value_type = on_ss_gatt_client_read_char_desc_event.valuetype();
      ALOGD("\n valuetype: %d ", data.value_type);
    }
    if (on_ss_gatt_client_read_char_desc_event.has_value()) {
       if(data.value.len > 0 && data.value.len <= BTGATT_MAX_ATTR_LEN) {
            memcpy(data.value.value, on_ss_gatt_client_read_char_desc_event.value().c_str(),
                    data.value.len);
            ALOGD("\n data.value: %s ", data.value.value);
            ALOGD("\n value: %s ", on_ss_gatt_client_read_char_desc_event.value().c_str());
       }
    }
    HAL_CBACK(bt_gatt_callbacks, client->read_descriptor_cb,
                conn_id, data.status, data);
}

void process_gatt_client_write_desc_event(std::string resBufferString) {
    int status;
    uint32_t conn_id;
    uint32_t attr_hdl;
    uint32_t value_len;
    uint8_t value[BTGATT_MAX_ATTR_LEN];

    ss_gatt_client_write_char_desc_event on_ss_gatt_client_write_char_desc_event;
    on_ss_gatt_client_write_char_desc_event.ParseFromString(resBufferString);

    if (on_ss_gatt_client_write_char_desc_event.has_status()) {
      status = on_ss_gatt_client_write_char_desc_event.status();
      ALOGD("\n status: 0x%d ", status);
    }
    if (on_ss_gatt_client_write_char_desc_event.has_connid()) {
      conn_id = on_ss_gatt_client_write_char_desc_event.connid();
      ALOGD("\n conn id:  %lu ",(unsigned long)conn_id);
    }
    if (on_ss_gatt_client_write_char_desc_event.has_attrhdl()) {
      attr_hdl = on_ss_gatt_client_write_char_desc_event.attrhdl();
      ALOGD("\n handle: %lu ",(unsigned long)attr_hdl);
    }
    if (on_ss_gatt_client_write_char_desc_event.has_valuelen()) {
      value_len = on_ss_gatt_client_write_char_desc_event.valuelen();
      ALOGD("\n value_len:  %lu ",(unsigned long)value_len);
    }
    if (on_ss_gatt_client_write_char_desc_event.has_value()) {
        memcpy(value, on_ss_gatt_client_write_char_desc_event.value().c_str(), value_len);
        ALOGD("\n data.value: %s ", value);
        ALOGD("\n value: %s ", on_ss_gatt_client_write_char_desc_event.value().c_str());
    }
    HAL_CBACK(bt_gatt_callbacks, client->write_descriptor_cb,
                conn_id, status, attr_hdl, value_len, value);
}

void process_gatt_client_read_rssi_event(std::string resBufferString) {
    int status;
    uint32_t rssi;
    uint32_t client_if;
    RawAddress* address = nullptr;

    ss_gatt_client_read_remote_rssi_event on_ss_gatt_client_read_remote_rssi_event;
    on_ss_gatt_client_read_remote_rssi_event.ParseFromString(resBufferString);

    if (on_ss_gatt_client_read_remote_rssi_event.has_status()) {
      status = on_ss_gatt_client_read_remote_rssi_event.status();
      ALOGD("\n status: 0x%d ", status);
    }
    if (on_ss_gatt_client_read_remote_rssi_event.has_clientif()) {
      client_if = on_ss_gatt_client_read_remote_rssi_event.clientif();
      ALOGD("\n client_if: %lu ", (unsigned long)client_if);
    }
    if (on_ss_gatt_client_read_remote_rssi_event.has_rssi()) {
      rssi = on_ss_gatt_client_read_remote_rssi_event.rssi();
      ALOGD("\n rssi:  %lu ",(unsigned long)rssi);
    }
    if (on_ss_gatt_client_read_remote_rssi_event.has_address()) {
      uint8_t* addr = (uint8_t*)on_ss_gatt_client_read_remote_rssi_event.address().c_str();
       address = (RawAddress*)addr;
       ALOGD("\n address: %s ", address->ToString().c_str());
    }
    HAL_CBACK(bt_gatt_callbacks, client->read_remote_rssi_cb, client_if, *address,
                rssi, status);
}

void process_gatt_client_phy_updated_event(std::string resBufferString) {
    int status;
    uint32_t conn_id;
    uint8_t tx_phy;
    uint8_t rx_phy;

    ss_gatt_client_phy_updated_event on_ss_gatt_client_phy_updated_event;
    on_ss_gatt_client_phy_updated_event.ParseFromString(resBufferString);

    if (on_ss_gatt_client_phy_updated_event.has_status()) {
      status = on_ss_gatt_client_phy_updated_event.status();
      ALOGD("\n status: 0x%d ", status);
    }
    if (on_ss_gatt_client_phy_updated_event.has_connid()) {
      conn_id = on_ss_gatt_client_phy_updated_event.connid();
      ALOGD("\n conn_id: %lu ",(unsigned long)conn_id);
    }
    if (on_ss_gatt_client_phy_updated_event.has_txphy()) {
      tx_phy = on_ss_gatt_client_phy_updated_event.txphy();
      ALOGD("\n tx_phy: %d ", tx_phy);
    }
    if (on_ss_gatt_client_phy_updated_event.has_rxphy()) {
      rx_phy = on_ss_gatt_client_phy_updated_event.rxphy();
      ALOGD("\n rx_phy: %d ", rx_phy);
    }
    HAL_CBACK(bt_gatt_callbacks, client->phy_updated_cb, conn_id, tx_phy,
                rx_phy, status);
}

void process_gatt_client_mtu_updated_event(std::string resBufferString) {
    int status;
    uint32_t conn_id;
    uint32_t mtu;

    ss_gatt_client_mtu_update_event on_ss_gatt_client_mtu_update_event;
    on_ss_gatt_client_mtu_update_event.ParseFromString(resBufferString);

    if (on_ss_gatt_client_mtu_update_event.has_status()) {
      status = on_ss_gatt_client_mtu_update_event.status();
      ALOGD("\n status: 0x%d ", status);
    }
    if (on_ss_gatt_client_mtu_update_event.has_connid()) {
      conn_id = on_ss_gatt_client_mtu_update_event.connid();
      ALOGD("\n conn id: %lu ",(unsigned long)conn_id);
    }
    if (on_ss_gatt_client_mtu_update_event.has_mtu()) {
      mtu = on_ss_gatt_client_mtu_update_event.mtu();
      ALOGD("\n mtu: %lu ",(unsigned long)mtu);
    }
    HAL_CBACK(bt_gatt_callbacks, client->configure_mtu_cb, conn_id, status, mtu);
}

void process_gatt_client_congestion_event(std::string resBufferString) {
    uint8_t congested;
    uint32_t conn_id;

    ss_gatt_client_congestion_event on_ss_gatt_client_congestion_event;
    on_ss_gatt_client_congestion_event.ParseFromString(resBufferString);

    if (on_ss_gatt_client_congestion_event.has_congested()) {
      congested = on_ss_gatt_client_congestion_event.congested();
      ALOGD("\n congested: 0x%d ", congested);
    }
    if (on_ss_gatt_client_congestion_event.has_connid()) {
      conn_id = on_ss_gatt_client_congestion_event.connid();
      ALOGD("\n conn id: %lu ",(unsigned long)conn_id);
    }
    HAL_CBACK(bt_gatt_callbacks, client->congestion_cb, conn_id, congested);
}

void process_gatt_client_conn_updated_event(std::string resBufferString) {
    uint32_t conn_id;
    uint32_t interval;
    uint32_t latency;
    uint32_t connTimeout;
    int status;

    ss_gatt_client_conn_param_updated_event on_ss_gatt_client_conn_param_updated_event;
    on_ss_gatt_client_conn_param_updated_event.ParseFromString(resBufferString);

    if (on_ss_gatt_client_conn_param_updated_event.has_status()) {
      status = on_ss_gatt_client_conn_param_updated_event.status();
      ALOGD("\n status: 0x%d ", status);
    }
    if (on_ss_gatt_client_conn_param_updated_event.has_connid()) {
      conn_id = on_ss_gatt_client_conn_param_updated_event.connid();
      ALOGD("\n conn id: %lu ",(unsigned long)conn_id);
    }
    if (on_ss_gatt_client_conn_param_updated_event.has_interval()) {
      interval = on_ss_gatt_client_conn_param_updated_event.interval();
      ALOGD("\n interval: %lu ",(unsigned long)interval);
    }
    if (on_ss_gatt_client_conn_param_updated_event.has_latency()) {
      latency = on_ss_gatt_client_conn_param_updated_event.latency();
      ALOGD("\n latency: %lu ",(unsigned long)latency);
    }
    if (on_ss_gatt_client_conn_param_updated_event.has_conntimeout()) {
      connTimeout = on_ss_gatt_client_conn_param_updated_event.conntimeout();
      ALOGD("\n connTimeout: %lu ",(unsigned long)connTimeout);
    }
    HAL_CBACK(bt_gatt_callbacks, client->conn_updated_cb, conn_id, interval,
                latency, connTimeout, status);
}

void process_gatt_client_srvc_changed_event(std::string resBufferString) {
    uint32_t conn_id;

    ss_gatt_client_srvc_changed_event on_ss_gatt_client_srvc_changed_event;
    on_ss_gatt_client_srvc_changed_event.ParseFromString(resBufferString);

    if (on_ss_gatt_client_srvc_changed_event.has_connid()) {
      conn_id = on_ss_gatt_client_srvc_changed_event.connid();
      ALOGD("\n conn id: %lu ",(unsigned long)conn_id);
    }
    HAL_CBACK(bt_gatt_callbacks, client->service_changed_cb, conn_id);
}

void process_gatt_client_get_gatt_db_event(std::string resBufferString) {
    uint32_t count;
    uint32_t conn_id;
    btgatt_db_element_t* db = NULL;

    ss_gatt_client_get_gatt_db_event ss_gattc_db_event;
    ss_gattc_db_event.ParseFromString(resBufferString);

    if (ss_gattc_db_event.has_count()) {
      count = ss_gattc_db_event.count();
      ALOGD("\n count: %lu ", (unsigned long)count);
    }
    if (ss_gattc_db_event.has_connid()) {
      conn_id = ss_gattc_db_event.connid();
      ALOGD("\n conn id: %lu ",(unsigned long)conn_id);
    }
    size_t db_size = ss_gattc_db_event.ss_gatt_db_element_().size();
    if(db_size == 0) {
        ALOGD("\n db size 0");
        return;
    }
    void* buffer = osi_malloc(db_size * sizeof(btgatt_db_element_t));
    btgatt_db_element_t* curr_db_attr = (btgatt_db_element_t*)buffer;
    for(uint32_t i = 0; i < count; i++) {
        curr_db_attr->id = ss_gattc_db_event.ss_gatt_db_element_(i).id();
        ALOGD("\n id: %d ",curr_db_attr->id);
        curr_db_attr->uuid = bluetooth::Uuid::FromString(
                            ss_gattc_db_event.ss_gatt_db_element_(i).uuid());
        ALOGD("\n uuid: %s ",curr_db_attr->uuid.ToString().c_str());
        curr_db_attr->type = static_cast<bt_gatt_db_attribute_type_t>(
                            ss_gattc_db_event.ss_gatt_db_element_(i).type());
        ALOGD("\n type: %d ",curr_db_attr->type);
        curr_db_attr->attribute_handle =
                                ss_gattc_db_event.ss_gatt_db_element_(i).attributehandle();
        ALOGD("\n attribute_handle: %d ",curr_db_attr->attribute_handle);
        curr_db_attr->start_handle = ss_gattc_db_event.ss_gatt_db_element_(i).starthandle();
        ALOGD("\n start_handle: %d ",curr_db_attr->start_handle);
        curr_db_attr->end_handle = ss_gattc_db_event.ss_gatt_db_element_(i).endhandle();
        ALOGD("\n end_handle: %d ",curr_db_attr->end_handle);
        curr_db_attr->properties = ss_gattc_db_event.ss_gatt_db_element_(i).properties();
        ALOGD("\n properties: %d ",curr_db_attr->properties);
        curr_db_attr->extended_properties =
                                ss_gattc_db_event.ss_gatt_db_element_(i).extendedproperties();
        ALOGD("\n extended_properties: %d ",curr_db_attr->extended_properties);
        curr_db_attr->permissions = ss_gattc_db_event.ss_gatt_db_element_(i).permissions();
        ALOGD("\n permissions: %d ",curr_db_attr->permissions);

        curr_db_attr++;
    }
    db = (btgatt_db_element_t*)buffer;

    HAL_CBACK(bt_gatt_callbacks, client->get_gatt_db_cb, conn_id, db, count);
    osi_free(buffer);
}

void btif_ss_gatt_client_callback(uint16_t event, char* p_param) {
  ALOGD("btif_ss_gatt_client_callback :: event is :: %d", event);
  std::string resBufferString;
  tBTIF_SS_Cback* cb_data = (tBTIF_SS_Cback*)p_param;
  uint16_t MSG_ID = (cb_data->payload[0] | (((int)(cb_data->payload[1])) << 8));
  uint16_t length = (cb_data->payload[2] | (((int)(cb_data->payload[3])) << 8));
  uint16_t proto_ec = 0;
  if (length > 0) {
    proto_ec = (cb_data->payload[4] | (((int)(cb_data->payload[5])) << 8));
    char resBuffer[length];
    int j = 0;
    for (int i = MSG_PROTO_OFFSET; i < (length + MSG_PROTO_OFFSET); i++) {
      resBuffer[j] = (char)cb_data->payload[i];
      j++;
    }

    resBufferString.assign(resBuffer, length);
    free(cb_data->payload);
  }
  ALOGI("Sending signal on Conditional variable from GATT Client");
  ss_gatt_client_interface->setIsSignalSent(true);
  pthread_mutex_lock(&BluetoothSSInterface::ss_cback_mutex);
  pthread_cond_signal(&BluetoothSSInterface::ss_cback_cond_var);
  pthread_mutex_unlock(&BluetoothSSInterface::ss_cback_mutex);
  ALOGD("MSG_ID is :: %X , Proto length: %d and Proto Encoded Value %d", MSG_ID,
        length, proto_ec);
  switch (event) {
    case BT_LE_GATT_CLIENT_REGISTERED_EVENT: {
      ALOGV("BT_LE_GATT_CLIENT_REGISTERED_EVENT");
      process_gatt_client_registered_event(resBufferString);
      break;
    }
    case BT_LE_GATT_CLIENT_CONNECTED_EVENT: {
      ALOGV("BT_LE_GATT_CLIENT_CONNECTED_EVENT");
      process_gatt_client_connected_event(resBufferString);
      break;
    }
    case BT_LE_GATT_CLIENT_READ_PHY_EVENT: {
      ALOGV("BT_LE_GATT_CLIENT_READ_PHY_EVENT");
      process_gatt_client_read_phy_event(resBufferString);
      break;
    }
    case BT_LE_GATT_CLIENT_DISCONNECTED_EVENT: {
       ALOGV("BT_LE_GATT_CLIENT_DISCONNECTED_EVENT");
       process_gatt_client_disconnected_event(resBufferString);
       break;
    }
    case BT_LE_GATT_CLIENT_SEARCH_COMPLETED_EVENT: {
       ALOGV("BT_LE_GATT_CLIENT_SEARCH_COMPLETED_EVENT");
       process_gatt_client_search_completed_event(resBufferString);
       break;
    }
    case BT_LE_GATT_CLIENT_REG_DEREG_NOTIFICATIONS_EVENT: {
       ALOGV("BT_LE_GATT_CLIENT_REG_DEREG_NOTIFICATIONS_EVENT");
       process_gatt_client_reg_dereg_notif_event(resBufferString);
       break;
    }
    case BT_LE_GATT_CLIENT_NOTIFY_EVENT: {
       ALOGV("BT_LE_GATT_CLIENT_NOTIFY_EVENT");
       process_gatt_client_notify_event(resBufferString);
       break;
    }
    case BT_LE_GATT_CLIENT_READ_CHAR_EVENT: {
       ALOGV("BT_LE_GATT_CLIENT_READ_CHAR_EVENT");
       process_gatt_client_read_char_event(resBufferString);
       break;
    }
    case BT_LE_GATT_CLIENT_WRITE_CHAR_EVENT: {
       ALOGV("BT_LE_GATT_CLIENT_WRITE_CHAR_EVENT");
       process_gatt_client_write_char_event(resBufferString);
       break;
    }
    case BT_LE_GATT_CLIENT_EXEC_WRITE_EVENT: {
       ALOGV("BT_LE_GATT_CLIENT_EXEC_WRITE_EVENT");
       process_gatt_client_exec_write_event(resBufferString);
       break;
    }
    case BT_LE_GATT_CLIENT_READ_DESC_EVENT: {
       ALOGV("BT_LE_GATT_CLIENT_READ_DESC_EVENT");
       process_gatt_client_read_desc_event(resBufferString);
       break;
    }
    case BT_LE_GATT_CLIENT_WRITE_DESC_EVENT: {
       ALOGV("BT_LE_GATT_CLIENT_WRITE_DESC_EVENT");
       process_gatt_client_write_desc_event(resBufferString);
       break;
    }
    case BT_LE_GATT_CLIENT_READ_RSSI_EVENT: {
       ALOGV("BT_LE_GATT_CLIENT_READ_RSSI_EVENT");
       process_gatt_client_read_rssi_event(resBufferString);
       break;
    }
    case BT_LE_GATT_CLIENT_MTU_UPDATED_EVENT: {
       ALOGV("BT_LE_GATT_CLIENT_MTU_UPDATED_EVENT");
       process_gatt_client_mtu_updated_event(resBufferString);
       break;
    }
    case BT_LE_GATT_CLIENT_CONGESTION_EVENT: {
       ALOGV("BT_LE_GATT_CLIENT_CONGESTION_EVENT");
       process_gatt_client_congestion_event(resBufferString);
       break;
    }
    case BT_LE_GATT_CLIENT_GET_GATT_DB_EVENT: {
       ALOGV("BT_LE_GATT_CLIENT_GET_GATT_DB_EVENT");
       process_gatt_client_get_gatt_db_event(resBufferString);
       break;
    }
    case BT_LE_GATT_CLIENT_PHY_UPDATED_EVENT: {
       ALOGV("BT_LE_GATT_CLIENT_PHY_UPDATED_EVENT");
       process_gatt_client_phy_updated_event(resBufferString);
       break;
    }
    case BT_LE_GATT_CLIENT_CONN_PARAM_UPDATED_EVENT: {
       ALOGV("BT_LE_GATT_CLIENT_CONN_PARAM_UPDATED_EVENT");
       process_gatt_client_conn_updated_event(resBufferString);
       break;
    }
    case BT_LE_GATT_CLIENT_SERVICE_CHANGED_EVENT: {
       ALOGV("BT_LE_GATT_CLIENT_SERVICE_CHANGED_EVENT");
       process_gatt_client_srvc_changed_event(resBufferString);
       break;
    }
    default: {
        ALOGD("Gatt Client - unknown msg id");
       break;
    }
  }
}
