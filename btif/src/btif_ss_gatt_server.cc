/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "btif_ss_gatt_server.h"
#include "btif_ss_stub_interface.h"
#include "protobuf/proto/gatt_server.pb.h"
#include "osi/include/log.h"
#include "btif_api.h"
#include <hardware/bt_gatt.h>
#include "btif/protobuf/include/proto_message_ids.h"
#include "btif_ss_interface.h"
#include "btif_common.h"
#include "raw_address.h"
#include <hardware/bt_common_types.h>
#include <cstring>
#include <regex>
#include "btif_gatt.h"
#include "btif_util.h"
#include <time.h>
using namespace singlestack::proto::server;
extern const btgatt_callbacks_t* bt_gatt_callbacks;
std::map<RawAddress, base::Callback<void(uint8_t tx_phy, uint8_t rx_phy, uint8_t status)>> GattSReadPhyCbMap;
std::map<RawAddress, int> GattSconnectedDevices;
std::vector<tBTIF_CONNECTION_INFO>ConnInfos;
BluetoothSSInterface* mgattsSSInterface = NULL;
std::regex pattern("..(?!$)");
#ifdef SS_STUB_ENABLED
BluetoothSSStubInterface* mserverSSStubInterface = NULL;
#endif

void btif_gatts_ss_init() {
  ALOGI("%s ", __func__);
  srand(time(NULL));
  if (mgattsSSInterface == NULL) {
    mgattsSSInterface = BluetoothSSInterface::getInstance();
    if (mgattsSSInterface == NULL) {
      ALOGI("%s single stack interface Initialization failed", __func__);
    }
  } else {
    ALOGI("single stack interface is already created");
  }

  if (mgattsSSInterface != NULL) {
    ALOGI("%s: registering DM profile callback with ss_interface", __func__);
    mgattsSSInterface->registerCallbacks(BT_PROFILE_ID_GATTs,
                                       btif_server_ss_callback);
  }
#ifdef SS_STUB_ENABLED
  if (mserverSSStubInterface == NULL) {
    mserverSSStubInterface = BluetoothSSStubInterface::getInstance();
    if (mserverSSStubInterface == NULL) {
      ALOGI("%s server single stack stub interface Initialization failed",
            __func__);
    }
  }
  else {
    ALOGI("server single stack stub interface is already created");
  }
#endif
}

void btif_gatts_ss_deinit() {
  if (mgattsSSInterface != NULL) {
    mgattsSSInterface->deregisterCallbacks(BT_PROFILE_ID_GATTs);
  }
  if (mgattsSSInterface == NULL) {
    ALOGI("single stack interface is already null");
  } else {
    mgattsSSInterface = NULL;
  }
#ifdef SS_STUB_ENABLED
  if (mserverSSStubInterface == NULL) {
    ALOGI("adv single stack stub interface is already null");
  } else {
    mserverSSStubInterface = NULL;
  }
#endif
}

bt_status_t btif_ss_gatt_server::postTxMessage(std::string msgStr) {
#ifndef SS_STUB_ENABLED
  /* Write to glink */
  if (mgattsSSInterface != NULL) {
      mgattsSSInterface->postTxMsg(msgStr);
  } else {
      return BT_STATUS_FAIL;
  }
#else
  if (mserverSSStubInterface != NULL) {
      mserverSSStubInterface->postTxMsg(msgStr);
  } else {
      return BT_STATUS_FAIL;
  }
#endif
  return BT_STATUS_SUCCESS;
}

int btif_delete_connection_info_disconnect(int serverif,int connid) {

    std::vector<tBTIF_CONNECTION_INFO>::iterator it;
    int randId;
    for(it =  ConnInfos.begin() ; it != ConnInfos.end();it++) {
        if(it->serverIf == serverif && it->connId == connid) {
            randId = it->randId;
            ConnInfos.erase(it);
            return randId;
        }
    }
    return -1;
}

void btif_delete_connection_info(int serverif) {

    std::vector<tBTIF_CONNECTION_INFO>::iterator it;
    for(it =  ConnInfos.begin() ; it != ConnInfos.end();) {
        if(it->serverIf == serverif) {
            it = ConnInfos.erase(it);
        } else {
            it++;
        }
    }
}

tBTIF_CONNECTION_INFO* btif_find_conn_info(int randId) {

    for(unsigned int i = 0 ; i < ConnInfos.size();i++) {
        if(ConnInfos[i].randId == randId) {
            return &ConnInfos[i];
        }
    }
    return NULL;
}

int btif_find_conn_id(int connid) {

    for (auto it : ConnInfos) {
        if(it.connId == connid) {
            return it.randId;
        }
    }
    return -1;
}

bt_status_t btif_ss_gatt_server::registerServer(const bluetooth::Uuid& bt_uuid, bool eatt_support) {

    std::string msgStr;
    ss_gatt_register_server ss_gatt_register_server_;

    ss_gatt_register_server_.set_uuid(bt_uuid.ToString());
    ss_gatt_register_server_.set_eatt_support(eatt_support);
    ss_gatt_register_server_.SerializeToString(&msgStr);
    //PrintEncodedBytes(msgStr);
    std::string packet = FormTxPacket(BT_LE_SERVER_REG_SERVER, PROTO_ENC_DEC,
                                 msgStr.length(), msgStr);
    return postTxMessage(packet);
}

bt_status_t btif_ss_gatt_server::connect(int server_if, const RawAddress& bd_addr,
                                   bool is_direct, int transport) {
    std::string msgStr;
    ss_gatt_server_connect ss_gatt_server_connect_;

    ss_gatt_server_connect_.set_serverif(server_if);
    ss_gatt_server_connect_.set_address(ToRawString(&bd_addr).c_str());
    ss_gatt_server_connect_.set_isdirect(is_direct);
    ss_gatt_server_connect_.set_transport(transport);
    ss_gatt_server_connect_.SerializeToString(&msgStr);
    //PrintEncodedBytes(msgStr);
    std::string packet = FormTxPacket(BT_LE_SERVER_CONNECT, PROTO_ENC_DEC,
                                 msgStr.length(), msgStr);
    return postTxMessage(packet);
}

bt_status_t btif_ss_gatt_server::disconnect(int server_if, const RawAddress& bd_addr,
                                    int conn_id) {
    std::string msgStr;
    ss_gatt_server_disconnect ss_gatt_server_disconnect_;
    tBTIF_CONNECTION_INFO *connection_info = btif_find_conn_info(conn_id);

    ss_gatt_server_disconnect_.set_serverif(server_if);
    ss_gatt_server_disconnect_.set_address(ToRawString(&bd_addr).c_str());
    if(connection_info != NULL && connection_info->serverIf == server_if)
        ss_gatt_server_disconnect_.set_connid(connection_info->connId);
    else {
        ALOGE("Invalid conn_id");
        return BT_STATUS_FAIL;
    }
    ss_gatt_server_disconnect_.SerializeToString(&msgStr);
    std::string packet = FormTxPacket(BT_LE_SERVER_DISCONNECT_SERVER , PROTO_ENC_DEC,
                                 msgStr.length(), msgStr);
    return postTxMessage(packet);
}

bt_status_t btif_ss_gatt_server::unregisterServer(int server_if) {

    std::string msgStr;
    ss_gatt_unregister_server ss_gatt_unregister_server_;

    ss_gatt_unregister_server_.set_serverif(server_if);
    ss_gatt_unregister_server_.SerializeToString(&msgStr);
    std::string packet = FormTxPacket(BT_LE_SERVER_UNREG_SERVER, PROTO_ENC_DEC,
                                 msgStr.length(), msgStr);
    btif_delete_connection_info(server_if);
    return postTxMessage(packet);
}

bt_status_t btif_ss_gatt_server::readPhy(const RawAddress& bd_addr,
     base::Callback<void(uint8_t tx_phy, uint8_t rx_phy, uint8_t status)> cb) {

    std::string msgStr;
    int server_if;
    for (auto conn : GattSconnectedDevices) {
        if (bd_addr == conn.first) {
          server_if = conn.second;
          break;
        }
    }
    ss_gatt_server_read_phy ss_gatt_server_read_phy_;
    ss_gatt_server_read_phy_.set_serverif(server_if);
    ss_gatt_server_read_phy_.set_address(ToRawString(&bd_addr).c_str());
    ss_gatt_server_read_phy_.SerializeToString(&msgStr);
    //PrintEncodedBytes(msgStr);
    std::string packet = FormTxPacket(BT_LE_SERVER_READ_PHY, PROTO_ENC_DEC,
                                 msgStr.length(), msgStr);
    GattSReadPhyCbMap.insert(std::pair<RawAddress, base::Callback<void(uint8_t tx_phy, uint8_t rx_phy, uint8_t status)> >(bd_addr, cb));
    return postTxMessage(packet);
}

bt_status_t btif_ss_gatt_server::setPhy(const RawAddress& bd_addr,
                        uint8_t tx_phy, uint8_t rx_phy,uint16_t phy_options) {

    std::string msgStr;
    ss_gatt_server_set_phy ss_gatt_server_set_phy_;

    ss_gatt_server_set_phy_.set_address(ToRawString(&bd_addr).c_str());
    ss_gatt_server_set_phy_.set_txphy(tx_phy);
    ss_gatt_server_set_phy_.set_rxphy(rx_phy);
    ss_gatt_server_set_phy_.set_phyoptions(phy_options);
    ss_gatt_server_set_phy_.SerializeToString(&msgStr);
    //PrintEncodedBytes(msgStr);
    std::string packet = FormTxPacket(BT_LE_SERVER_SET_PHY, PROTO_ENC_DEC,
                                 msgStr.length(), msgStr);
    return postTxMessage(packet);
}

bt_status_t btif_ss_gatt_server::clearService(int server_if,int srvcHandle) {

    std::string msgStr;
    ss_gatt_clear_service ss_gatt_clear_service_;
    ss_gatt_clear_service_.set_serverif(server_if);
    ss_gatt_clear_service_.set_srvchandle(srvcHandle);
    ss_gatt_clear_service_.SerializeToString(&msgStr);
    //PrintEncodedBytes(msgStr);
    std::string packet = FormTxPacket(BT_LE_SERVER_DELETE_SEVICE, PROTO_ENC_DEC,
                                 msgStr.length(), msgStr);
    return postTxMessage(packet);
}

bt_status_t btif_ss_gatt_server::sendIndicationNotification(int attribute_handle,
                         int conn_id,int confirm,const std::vector<uint8_t> value,int server_if) {

    std::string msgStr;
    ss_gatt_send_indication_notification ss_gatt_send_indication_notification_;
    tBTIF_CONNECTION_INFO *connection_info = btif_find_conn_info(conn_id);

    std::string Value(value.begin(), value.end());
    ss_gatt_send_indication_notification_.set_attrhandle(attribute_handle);
    ss_gatt_send_indication_notification_.set_value(Value);
    ss_gatt_send_indication_notification_.set_confirm(confirm);
    ss_gatt_send_indication_notification_.set_serverif(server_if);
    if(connection_info != NULL && connection_info->serverIf == server_if)
        ss_gatt_send_indication_notification_.set_connid(connection_info->connId);
    else {
        ALOGE("Invalid conn_id");
        return BT_STATUS_FAIL;
    }
    ss_gatt_send_indication_notification_.SerializeToString(&msgStr);
    //PrintEncodedBytes(msgStr);
    std::string packet = FormTxPacket(BT_LE_SERVER_SEND_INDICATION, PROTO_ENC_DEC,
                                 msgStr.length(), msgStr);
    return postTxMessage(packet);

}
bt_status_t btif_ss_gatt_server::sendResponse(int conn_id, int trans_id,int status,const btgatt_response_t& response) {
    std::string msgStr;

    ss_gatt_server_send_response ss_gatt_server_send_response_;
    ss_bt_gatt_response *ss_bt_gatt_response_ = ss_gatt_server_send_response_.mutable_btgatt_response();
    ss_bt_gatt_value *ss_bt_gatt_value_ = (*ss_bt_gatt_response_).mutable_attrvalue();

    tBTIF_CONNECTION_INFO *connection_info = btif_find_conn_info(conn_id);
    ss_bt_gatt_value_->set_value(response.attr_value.value,response.attr_value.len);
    ss_bt_gatt_value_->set_handle(response.attr_value.handle);
    ss_bt_gatt_value_->set_offset(response.attr_value.offset);
    ss_bt_gatt_value_->set_len(response.attr_value.len);
    ss_bt_gatt_value_->set_auth_req(response.attr_value.auth_req);

    ss_bt_gatt_response_->set_handle(response.handle);
    ss_gatt_server_send_response_.set_transid(trans_id);
    ss_gatt_server_send_response_.set_status(status);
    if(connection_info != NULL)
        ss_gatt_server_send_response_.set_connid(connection_info->connId);
    else {
        ALOGE("Invalid conn_id");
        return BT_STATUS_FAIL;
    }

    ss_gatt_server_send_response_.SerializeToString(&msgStr);
    //PrintEncodedBytes(msgStr);
    std::string packet = FormTxPacket(BT_LE_SERVER_SEND_RESPONSE, PROTO_ENC_DEC,
                                 msgStr.length(), msgStr);
    return postTxMessage(packet);
}

bt_status_t btif_ss_gatt_server::AddService(int server_if,std::vector<btgatt_db_element_t> service){
    std::string msgStr;
    if (service[0].uuid == bluetooth::Uuid::From16Bit(UUID_SERVCLASS_GATT_SERVER) ||
      service[0].uuid == bluetooth::Uuid::From16Bit(UUID_SERVCLASS_GAP_SERVER)) {
        HAL_CBACK(bt_gatt_callbacks, server->service_added_cb, BT_STATUS_FAIL,
              server_if, service.data(),service.size());
        return BT_STATUS_FAIL;
    }
    ss_gatt_server_add_service ss_gatt_server_add_service_;
    ss_gatt_server_add_service_.set_serverif(server_if);
    ss_gatt_server_add_service_.set_count(service.size());
    for(auto it : service) {
        ss_gatt_db_element *ss_gatt_db_element_ = ss_gatt_server_add_service_.add_gatt_db_element();
        ss_gatt_db_element_->set_uuid(it.uuid.ToString());
        ss_gatt_db_element_->set_type(it.type);
        ss_gatt_db_element_->set_id(it.id);
        ss_gatt_db_element_->set_attributehandle(it.attribute_handle);
        ss_gatt_db_element_->set_properties(it.properties);
        ss_gatt_db_element_->set_permissions(it.permissions);
        ss_gatt_db_element_->set_starthandle(it.start_handle);
        ss_gatt_db_element_->set_endhandle(it.end_handle);
        ss_gatt_db_element_->set_extended_properties(it.extended_properties);
    }
    ss_gatt_server_add_service_.SerializeToString(&msgStr);
    std::string packet = FormTxPacket(BT_LE_SERVER_ADD_SERVICE, PROTO_ENC_DEC,
                                 msgStr.length(), msgStr);
    return postTxMessage(packet);
}

bt_status_t btif_ss_gatt_server::stopService(int server_if,int srvcHandle) {
    std::string msgStr;
    ss_gatt_server_stop_service ss_gatt_server_stop_service_;
    ss_gatt_server_stop_service_.set_serverif(server_if);
    ss_gatt_server_stop_service_.set_svchandle(srvcHandle);
    ss_gatt_server_stop_service_.SerializeToString(&msgStr);
    std::string packet = FormTxPacket(BT_LE_SERVER_STOP_SERV, PROTO_ENC_DEC,
                                 msgStr.length(), msgStr);
    return postTxMessage(packet);
    
}

// Support for callback is yet to be incorporated.
void btif_server_ss_callback(uint16_t event, char* p_param) {
    ALOGD("btif_server_ss_callback :: event is :: %d", event);
    std::string resBufferString;
    tBTIF_SS_Cback* cb_data = (tBTIF_SS_Cback*)p_param;
    uint16_t MSG_ID = cb_data->payload[0] + (((int)(cb_data->payload[1])) << 8);
    uint16_t length = cb_data->payload[2] + (((int)(cb_data->payload[3])) << 8);
    uint16_t proto_ec = 0;
    if (length > 0) {
        proto_ec = cb_data->payload[4] + (((int)(cb_data->payload[5])) << 8);
        char resBuffer[length];
        int j = 0;
        for (int i = MSG_PROTO_OFFSET; i < (length + MSG_PROTO_OFFSET); i++) {
            resBuffer[j] = (char)cb_data->payload[i];
            j++;
        }
        resBufferString.assign(resBuffer, length);
        free(cb_data->payload);
    }
    ALOGD("MSG_ID is :: %X , Proto length: %d and Proto Encoded Value %d", MSG_ID,
        length, proto_ec);
    switch (event) {
        case BT_LE_SERVER_REG_SERVER_EVENT: {
            ALOGD("BT_LE_SERVER_REG_SERVER_EVENT");
            ss_gatt_server_registered_event onServerRegistered;
            int status = 0;
            int serverif = 0;
            std::string uuid ="";
            onServerRegistered.ParseFromString(resBufferString);
            if (onServerRegistered.has_status()) {
                status = onServerRegistered.status();
                ALOGD("\nstatus: %d ", status);
            }
            if (onServerRegistered.has_serverif()) {
                serverif = onServerRegistered.serverif();
                ALOGD("\nserverif: %d ", onServerRegistered.serverif());
            }
            if (onServerRegistered.has_uuid()) {
                uuid = onServerRegistered.uuid();
                ALOGD("\nuuid: %s ", onServerRegistered.uuid().c_str());
            }
            HAL_CBACK(bt_gatt_callbacks, server->register_server_cb,status,serverif,bluetooth::Uuid::FromString(uuid));
            break;
        }
        case BT_LE_SERVER_CONN_CHNG_EVENT: {
            ALOGD("BT_LE_SERVER_CONN_CHNG_EVENT");
            ss_gatt_server_connection_change_event  onConnectionChange;
            int connId = 0;
            int serverif = 0;
            bool connected = 0;
            int randId = 0;
            RawAddress* address = nullptr;
            onConnectionChange.ParseFromString(resBufferString);
            if(onConnectionChange.has_connid()) {
                connId = onConnectionChange.connid();
                ALOGD("\nconnId: %d ", connId);
            }
            if(onConnectionChange.has_serverif()) {
                serverif = onConnectionChange.serverif();
                ALOGD("\nserverif: %d ", serverif);
            }
            if(onConnectionChange.has_address()) {
                ALOGD("\naddress first: %s ", onConnectionChange.address().c_str());
                uint8_t* addr = (uint8_t*)onConnectionChange.address().c_str();
                address = (RawAddress*)addr;
                ALOGD("\naddress: %s ", address->ToString().c_str());
            }
            if(onConnectionChange.has_connected()) {
                connected = onConnectionChange.connected();
                if(connected) {
                    GattSconnectedDevices.insert(std::pair<RawAddress, int>(*address, serverif));
                    tBTIF_CONNECTION_INFO connection_info;
                    randId = connection_info.randId = rand();
                    ALOGD("\nassign serverif = %d and randID = %d ",serverif,randId);
                    connection_info.serverIf = serverif;
                    connection_info.connId = connId;
                    bool is_present = false;
                    for(auto it : ConnInfos) {
                        if( it.connId == connId && it.serverIf == serverif) {
                            is_present = true;
                            randId = it.randId;
                            break;
                        }
                    }
                    if(!is_present)
                        ConnInfos.push_back(connection_info);
                } else {
                    randId = btif_delete_connection_info_disconnect(serverif,connId);
                    GattSconnectedDevices.erase(*address);
                }
                ALOGD("\nconnected: %d ", connected);
            }
            HAL_CBACK(bt_gatt_callbacks, server->connection_cb, randId,
                 serverif, connected, *address);
            break;
        }
        case BT_LE_SERVER_SRV_ADD_EVENT: {
            ALOGD("BT_LE_SERVER_SRV_ADD_EVENT");
            ss_gatt_server_service_added_event onServiceAdd;
            int status = 0;
            int serverif = 0;
            uint16_t type;
            std::vector<btgatt_db_element_t> services;
            onServiceAdd.ParseFromString(resBufferString);
            if(onServiceAdd.has_status()) {
                status = onServiceAdd.status();
                ALOGD("\nstatus: %d ", status);
            }
            if(onServiceAdd.has_serverif()) {
                serverif = onServiceAdd.serverif();
                ALOGD("\nserverif: %d ", serverif);
            }
            for (int i = 0 ; i < onServiceAdd.gatt_db_element().size(); i++) {
                    btgatt_db_element_t service;
                    service.uuid =bluetooth::Uuid::FromString(onServiceAdd.gatt_db_element(i).uuid());
                    service.type = static_cast<bt_gatt_db_attribute_type_t>(onServiceAdd.gatt_db_element(i).type());
                    service.id = onServiceAdd.gatt_db_element(i).id();
                    service.attribute_handle = onServiceAdd.gatt_db_element(i).attributehandle();
                    service.start_handle = onServiceAdd.gatt_db_element(i).starthandle();
                    service.end_handle = onServiceAdd.gatt_db_element(i).endhandle();
                    service.properties = onServiceAdd.gatt_db_element(i).properties();
                    service.extended_properties = onServiceAdd.gatt_db_element(i).extended_properties();
                    service.permissions = onServiceAdd.gatt_db_element(i).permissions();
                    services.push_back(service);
            }
            HAL_CBACK(bt_gatt_callbacks, server->service_added_cb, status, serverif,
             services.data(), services.size());
            break;
        }
        case BT_LE_SERVER_PHY_UPDATE_EVENT: {
            ALOGD("BT_LE_SERVER_PHY_UPDATE_EVENT");
            ss_gatt_server_phy_updated_event onPhyUpdated;
            int connId = 0;
            int txphy = 0;
            int rxphy = 0;
            int status = 0;
            onPhyUpdated.ParseFromString(resBufferString);
            if(onPhyUpdated.has_connid()) {
                connId = onPhyUpdated.connid();
                ALOGD("\nconnid: %d ", connId);
            }
            if(onPhyUpdated.has_txphy()) {
                txphy = onPhyUpdated.txphy();
                ALOGD("\txphy: %d ", txphy);
            }
            if(onPhyUpdated.has_rxphy()) {
                rxphy = onPhyUpdated.rxphy();
                ALOGD("\rxphy: %d ", rxphy);
            }
            if(onPhyUpdated.has_status()) {
                status = onPhyUpdated.status();
                ALOGD("\nstatus: %d ", status);
            }
            for (auto it : ConnInfos) {
                if(it.connId == connId) {
                    ALOGD("\fetching randId: %d and serverif: %d and connId : %d", it.randId, it.serverIf, it.connId);
                    HAL_CBACK(bt_gatt_callbacks, server->phy_updated_cb,
                    it.randId, txphy,rxphy, status);
                }
            }
            break;
        }
        case BT_LE_SERVER_READ_PHY_EVENT: {
            ALOGD("BT_LE_SERVER_READ_PHY_EVENT");
            ss_gatt_server_read_phy_event onPhyRead;
            int serverIf = 0;
            RawAddress* address = nullptr;
            int txPhy = 0;
            int rxPhy = 0;
             int status = 0;
            onPhyRead.ParseFromString(resBufferString);
            if(onPhyRead.has_address()) {
                uint8_t* addr = (uint8_t*)onPhyRead.address().c_str();
                address = (RawAddress*)addr;
                ALOGD("\naddress: %s ", address->ToString().c_str());
            }
            if(onPhyRead.has_txphy()) {
                txPhy = onPhyRead.txphy();
                ALOGD("\ntxPhy : %d ", txPhy );
            }
            if(onPhyRead.has_rxphy()) {
                rxPhy = onPhyRead.rxphy();
                ALOGD("\nrxPhy: %d ", rxPhy);
            }
            if(onPhyRead.has_status()) {
                status = onPhyRead.status();
                ALOGD("\nstatus: %d ", status);
            }
            for (auto pair : GattSReadPhyCbMap) {
                if (*address == pair.first) {
                    do_in_jni_thread(Bind(pair.second, txPhy, rxPhy, status));
                    GattSReadPhyCbMap.erase(*address);
                    break;
                }
            }
            break;
        }
        case BT_LE_SERVER_SRV_DEL_EVENT: {
            ALOGD("BT_LE_SERVER_SRV_DEL_EVENT");
            int status = 0;
            int serverIf = 0;
            int srvcHandle = 0;
            ss_gatt_server_service_deleted_event onSerivceDel;
            onSerivceDel.ParseFromString(resBufferString);
            if(onSerivceDel.has_status()) {
                status = onSerivceDel.status();
                ALOGD("\nstatus: %d ", status);
            }
            if(onSerivceDel.has_serverif()) {
                serverIf = onSerivceDel.serverif();
                ALOGD("\nserverIf: %d ", serverIf);
            }
            if(onSerivceDel.has_srvchandle()) {
                srvcHandle = onSerivceDel.srvchandle();
                ALOGD("\nsrvcHandle: %d ", srvcHandle);
            }
            HAL_CBACK(bt_gatt_callbacks, server->service_deleted_cb,
                   status, serverIf,srvcHandle);
            break;
        }
        case BT_LE_SERVER_IND_SENT_EVENT: {
            ALOGD("BT_LE_SERVER_IND_SENT_EVENT");
            ss_gatt_server_notification_sent_event onNotificationSend;
            int connId = 0;
            int status = 0;
            int randId = 0;
            int serverIf = 0;
            onNotificationSend.ParseFromString(resBufferString);
            if(onNotificationSend.has_connid()) {
                connId = onNotificationSend.connid();
                ALOGD("\nconnId: %d ", connId);
            }
            if(onNotificationSend.has_status()) {
                status = onNotificationSend.status();
                ALOGD("\nstatus: %d ", status);
            }
            if(onNotificationSend.has_serverif()) {
                serverIf = onNotificationSend.serverif();
                for (auto it : ConnInfos) {
                    if(it.connId == connId && it.serverIf == serverIf) {
                        randId = it.randId;
                        break;
                    }
                }
            }
            HAL_CBACK(bt_gatt_callbacks, server->indication_sent_cb,
                 randId,status);
            break;
        }
        case BT_LE_SERVER_READ_CHAR_EVENT: {
            ALOGD("BT_LE_SERVER_READ_CHAR_EVENT");
            ss_gatt_server_read_char_desc_event onReadChar;
            RawAddress* address = nullptr;
            int connId = 0;
            int transId = 0;
            int attrhandle = 0;
            int offset = 0;
            bool islong = 0;
            onReadChar.ParseFromString(resBufferString);
            if(onReadChar.has_connid()) {
                connId = onReadChar.connid();
                ALOGD("\nconnId: %d ", connId);
            }
            if(onReadChar.has_address()) {
                uint8_t* addr = (uint8_t*)onReadChar.address().c_str();
                address = (RawAddress*)addr;
                ALOGD("\naddress: %s ", address->ToString().c_str());
            }
            if(onReadChar.has_transid()) {
                transId = onReadChar.transid();
                ALOGD("\ntransid: %d ", transId);
            }
            if(onReadChar.has_attrhandle()) {
                attrhandle = onReadChar.attrhandle();
                ALOGD("\nattrhandle: %d ", attrhandle);
            }
            if(onReadChar.has_offset()) {
                offset = onReadChar.offset();
                ALOGD("\noffset: %d ", offset);
            }
            if(onReadChar.has_islong()) {
                islong = onReadChar.islong();
                ALOGD("\nislong: %d ", islong);
            }
            int randId = btif_find_conn_id(connId);

            if (address != nullptr) {
                HAL_CBACK(bt_gatt_callbacks, server->request_read_characteristic_cb,
                     randId, transId,*address,attrhandle,offset,islong);
            } else {
                ALOGE("address is null");
            }

            break;
        }
        case BT_LE_SERVER_RSP_SENT_EVENT: {
            ALOGD("BT_LE_SERVER_RSP_SENT_EVENT");
            ss_gatt_server_response_sent_event onRespSend;
            int status = 0;
            int handle = 0;
            onRespSend.ParseFromString(resBufferString);
            if(onRespSend.has_status()) {
                status = onRespSend.status();
                ALOGD("\nstatus: %d ", status);
            }
            if(onRespSend.has_handle()) {
                handle = onRespSend.handle();
                ALOGD("\nhandle: %d ", handle);
            }
            HAL_CBACK(bt_gatt_callbacks, server->response_confirmation_cb, status,handle);
            break;
        }
        case BT_LE_SERVER_READ_DESC_EVENT: {
            ALOGD("BT_LE_SERVER_READ_DESC_EVENT");
            ss_gatt_server_read_char_desc_event onReadDesc;
            RawAddress* address = nullptr;
            int connId = 0;
            int transId = 0;
            int attrhandle = 0;
            int offset = 0;
            bool islong = 0;
            onReadDesc.ParseFromString(resBufferString);
            if(onReadDesc.has_connid()) {
                connId = onReadDesc.connid();
                ALOGD("\nconnId: %d ", connId);
            }
            if(onReadDesc.has_address()) {
                uint8_t* addr = (uint8_t*)onReadDesc.address().c_str();
                address = (RawAddress*)addr;
                ALOGD("\naddress: %s ", address->ToString().c_str());
            }
            if(onReadDesc.has_transid()) {
                transId = onReadDesc.transid();
                ALOGD("\ntransid: %d ", transId);
            }
            if(onReadDesc.has_attrhandle()) {
                attrhandle = onReadDesc.attrhandle();
                ALOGD("\nattrhandle: %d ", attrhandle);
            }
            if(onReadDesc.has_offset()) {
                offset = onReadDesc.offset();
                ALOGD("\noffset: %d ", offset);
            }
            if(onReadDesc.has_islong()) {
                islong = onReadDesc.islong();
                ALOGD("\nislong: %d ", islong);
            }
            int randId = btif_find_conn_id(connId);

            if (address != nullptr) {
                HAL_CBACK(bt_gatt_callbacks, server->request_read_descriptor_cb,
                     randId, transId,*address,attrhandle,offset,islong);
            } else {
                ALOGE("address is null");
            }

            break;
        }
        case BT_LE_SERVER_WRITE_CHAR_EVENT: {
            ALOGD("BT_LE_SERVER_WRITE_CHAR_EVENT");
            ss_gatt_server_write_char_desc_event onWriteChar;
            RawAddress* address = nullptr;
            int connId = 0;
            int transId = 0;
            int attrHandle = 0;
            int offset = 0;
            int length = 0;
            bool needRsp = 0;
            bool isprep = 0;
            uint8_t* data;
            std::vector<uint8_t> dataVector;
            onWriteChar.ParseFromString(resBufferString);
            if(onWriteChar.has_address()) {
                uint8_t* addr = (uint8_t*)onWriteChar.address().c_str();
                address = (RawAddress*)addr;
                ALOGD("\naddress: %s ", address->ToString().c_str());
            }
            if(onWriteChar.has_connid()) {
                connId = onWriteChar.connid();
                ALOGD("\nconnid: %d ", connId);
            }
            if(onWriteChar.has_transid()) {
                transId = onWriteChar.transid();
                ALOGD("\ntransId: %d ", transId);
            }
            if(onWriteChar.has_attrhandle()) {
                attrHandle = onWriteChar.attrhandle();
                ALOGD("\nattrHandle: %d ", attrHandle);
            }
            if(onWriteChar.has_offset()) {
                offset = onWriteChar.offset();
                ALOGD("\noffset: %d ", offset);
            }
            if(onWriteChar.has_length()) {
                length = onWriteChar.length();
                ALOGD("\nlength: %d ", length);
            }
            if(onWriteChar.has_needrsp()) {
                needRsp = onWriteChar.needrsp();
                ALOGD("\nneedRsp: %d ", needRsp);
            }
            if(onWriteChar.has_isprep()) {
                isprep = onWriteChar.isprep();
                ALOGD("\nisprep: %d ", isprep);
            }
            if(onWriteChar.has_data()) {
                std::string data_string = onWriteChar.data();
                std::vector<uint8_t> tempdataVector(data_string.begin(), data_string.end());
                dataVector = tempdataVector;
                data = &dataVector[0];
                int size = dataVector.size();
                ALOGD("size = %d", size);
                for (auto i = 0 ; i < size; i++) {
                    ALOGD("\n data  = %d", data[i]);
                }
            }
            int randId = btif_find_conn_id(connId);

            if (address != nullptr) {
                HAL_CBACK(bt_gatt_callbacks, server->request_write_characteristic_cb,
                      randId, transId,*address, attrHandle, offset,
                      needRsp, isprep, data, length);
            } else {
                ALOGE("bd_address is null");
            }

            break;
        }
        case BT_LE_SERVER_WRITE_DESC_EVENT: {
            ALOGD("BT_LE_SERVER_WRITE_DESC_EVENT");
            ss_gatt_server_write_char_desc_event onWriteDesc;
            RawAddress* address = nullptr;
            int connId = 0;
            int transId = 0;
            int attrHandle = 0;
            int offset = 0;
            int length = 0;
            bool needRsp = 0;
            bool isprep = 0;
            uint8_t* data;
            std::vector<uint8_t> dataVector;
            onWriteDesc.ParseFromString(resBufferString);
            if(onWriteDesc.has_address()) {
                uint8_t* addr = (uint8_t*)onWriteDesc.address().c_str();
                address = (RawAddress*)addr;
                ALOGD("\naddress: %s ", address->ToString().c_str());
            }
            if(onWriteDesc.has_connid()) {
                connId = onWriteDesc.connid();
                ALOGD("\nconnid: %d ", connId);
            }
            if(onWriteDesc.has_transid()) {
                transId = onWriteDesc.transid();
                ALOGD("\ntransId: %d ", transId);
            }
            if(onWriteDesc.has_attrhandle()) {
                attrHandle = onWriteDesc.attrhandle();
                ALOGD("\nattrHandle: %d ", attrHandle);
            }
            if(onWriteDesc.has_offset()) {
                offset = onWriteDesc.offset();
                ALOGD("\noffset: %d ", offset);
            }
            if(onWriteDesc.has_length()) {
                length = onWriteDesc.length();
                ALOGD("\nlength: %d ", length);
            }
            if(onWriteDesc.has_needrsp()) {
                needRsp = onWriteDesc.needrsp();
                ALOGD("\nneedRsp: %d ", needRsp);
            }
            if(onWriteDesc.has_isprep()) {
                isprep = onWriteDesc.isprep();
                ALOGD("\nisprep: %d ", isprep);
            }
            if(onWriteDesc.has_data()) {
                std::string data_string = onWriteDesc.data();
                std::vector<uint8_t> tempdataVector(data_string.begin(), data_string.end());
                dataVector = tempdataVector;
                data = &dataVector[0];
                int size = dataVector.size();
                ALOGD("size = %d", size);
                for (auto i = 0 ; i < size; i++) {
                    ALOGD("\n data  = %d", data[i]);
                }
            }
            int randId = btif_find_conn_id(connId);

            if (address != nullptr) {
                HAL_CBACK(bt_gatt_callbacks, server->request_write_descriptor_cb,
                      randId, transId,*address, attrHandle, offset,
                      needRsp, isprep, data, length);
            } else {
                ALOGE("bd_address is null");
            }

            break;
        }
        case BT_LE_SERVER_EXEC_WRITE_EVENT: {
            ALOGD("BT_LE_SERVER_EXEC_WRITE_EVENT");
            ss_gatt_server_execute_write_event onExecWrite;
            RawAddress* address = nullptr;
            int transId = 0;
            bool execWrite = 0;
            int connId = 0;
            int randId = 0;
            int serverIf = 0;
            onExecWrite.ParseFromString(resBufferString);
            if(onExecWrite.has_address()) {
                uint8_t* addr = (uint8_t*)onExecWrite.address().c_str();
                address = (RawAddress*)addr;
                ALOGD("\naddress: %s ", address->ToString().c_str());
            }
            if(onExecWrite.has_transid()) {
                transId = onExecWrite.transid();
                ALOGD("\ntransId: %d ", transId );
            }
            if(onExecWrite.has_execwrite()) {
                execWrite = onExecWrite.execwrite();
                ALOGD("\ndata: %d ", execWrite );
            }
            if(onExecWrite.has_connid()) {
                connId = onExecWrite.connid();
                ALOGD("\ndata: %d ", connId );
            }
            if(onExecWrite.has_serverif()) {
                serverIf = onExecWrite.serverif();
                for (auto it : ConnInfos) {
                    if(it.connId == connId && it.serverIf == serverIf) {
                        randId = it.randId;
                        break;
                    }
                }
            }
            HAL_CBACK(bt_gatt_callbacks, server->request_exec_write_cb,
                   randId, transId,*address,execWrite);
            break;
        }
        case BT_LE_SERVER_SRV_CONG_EVENT: {
            ALOGD("BT_LE_SERVER_SRV_CONG_EVENT");
            ss_gatt_server_congestion_event onServerCongestion;
            int connId = 0;
            bool congested = 0;
            onServerCongestion.ParseFromString(resBufferString);
            if(onServerCongestion.has_connid()) {
                connId = onServerCongestion.connid();
                ALOGD("\nconnId: %d ", connId );
            }
            if(onServerCongestion.has_congested()) {
                congested = onServerCongestion.congested();
                ALOGD("\ncongested: %d ", congested );
            }
            for (auto it : ConnInfos) {
                if(it.connId == connId) {
                    HAL_CBACK(bt_gatt_callbacks, server->congestion_cb,
                    it.randId,congested);
                }
            }
            break;
        }
        case BT_LE_SERVER_MTU_UPDATE_EVENT:{
            ALOGD("BT_LE_SERVER_MTU_UPDATE_EVENT");
            ss_gatt_server_mtu_update_event onServerMtuUpdate;
            int connId = 0;
            int mtu = 0;
            onServerMtuUpdate.ParseFromString(resBufferString);
            if(onServerMtuUpdate.has_connid()) {
                connId = onServerMtuUpdate.connid();
                ALOGD("\nconnID: %d ", connId );
            }
            if(onServerMtuUpdate.has_mtu()) {
                mtu = onServerMtuUpdate.mtu();
                ALOGD("\nmtu: %d ", mtu );
            }
            for (auto it : ConnInfos) {
                if(it.connId == connId) {
                    HAL_CBACK(bt_gatt_callbacks, server->mtu_changed_cb,
                    it.randId,mtu);
                }
            }
            break;
        }
        case BT_LE_SERVER_CONN_UPDATE_EVENT: {
            ALOGD("BT_LE_SERVER_CONN_UPDATE_EVENT");
            ss_gatt_server_conn_update_event onServerConnUpdate;
            int connId = 0;
            int interval = 0;
            int latency = 0;
            int timeout = 0;
            int status = 0;
            onServerConnUpdate.ParseFromString(resBufferString);
            if(onServerConnUpdate.has_connid()) {
                connId = onServerConnUpdate.connid();
                ALOGD("\nconnID: %d ", connId );
            }
            if(onServerConnUpdate.has_interval()) {
                interval = onServerConnUpdate.interval();
                ALOGD("\ninterval: %d ", interval );
            }
            if(onServerConnUpdate.has_latency()) {
                latency = onServerConnUpdate.latency();
                ALOGD("\nlatency: %d ", latency );
            }
            if(onServerConnUpdate.has_timeout()) {
                timeout = onServerConnUpdate.timeout();
                ALOGD("\ntimeout: %d ", timeout );
            }
            if(onServerConnUpdate.has_status ()) {
                status  = onServerConnUpdate.status ();
                ALOGD("\nstatus : %d ", status  );
            }
            for (auto it : ConnInfos) {
                if(it.connId == connId) {
                    HAL_CBACK(bt_gatt_callbacks, server->conn_updated_cb,
                    it.randId, interval,latency, timeout,status);
                }
            }
            break;
        }
    }
}
