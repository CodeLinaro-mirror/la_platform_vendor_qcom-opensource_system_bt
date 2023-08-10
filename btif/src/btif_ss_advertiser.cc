/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */


#include <hardware/ble_advertiser.h>
#include <hardware/bluetooth.h>
#include <hardware/bt_gatt.h>
#include <utils/Log.h>
#include <map>
#include <string>
#include <vector>
#include "ble_advertiser.h"
#include "btif_ss_advertiser.h"
#include "btif/protobuf/include/proto_message_ids.h"
#include "btif_ss_interface.h"
#include "protobuf/proto/advertiser.pb.h"
#include "btif_util.h"
#include "btif_gatt.h"
#ifdef SS_STUB_ENABLED
#include "btif_ss_stub_interface.h"
#endif
using namespace std;
using base::Bind;

BluetoothSSInterface* madvSSInterface = NULL;
#ifdef SS_STUB_ENABLED
BluetoothSSStubInterface* madvSSStubInterface = NULL;
#endif
map<int, uint32_t> RegIdAdvIdMap;
map<int, BleAdvertiserInterface::IdTxPowerStatusCallback> IdTxPowStatusCbMap;
map<int, BleAdvertiserInterface::IdStatusCallback> IdStatusTimeoutCbMap;
map<uint32_t, BleAdvertiserInterface::ParametersCallback> ParametersCbMap;
map<uint32_t, BleAdvertiserInterface::StatusCallback> SetAdvDataCbMap;
map<uint32_t, BleAdvertiserInterface::StatusCallback> SetScanRespDataCbMap;
map<uint32_t, BleAdvertiserInterface::StatusCallback> EnableCbMap;
map<uint32_t, BleAdvertiserInterface::StatusCallback> TimeoutCbMap;
map<uint32_t, BleAdvertiserInterface::StatusCallback> PeriodicAdvParamCbMap;
map<uint32_t, BleAdvertiserInterface::StatusCallback> PeriodicAdvDataCbMap;
map<uint32_t, BleAdvertiserInterface::StatusCallback> PeriodicAdvEnCbMap;
map<uint32_t, BleAdvertiserInterface::GetAddressCallback> GetAddressCbMap;

void btif_adv_ss_init() {
  ALOGI("%s ", __func__);
  if (madvSSInterface == NULL) {
    madvSSInterface = BluetoothSSInterface::getInstance();
    if (madvSSInterface == NULL) {
      ALOGI("%s single stack interface Initialization failed", __func__);
    }
  } else {
    ALOGI("single stack interface is already created");
  }
  if (madvSSInterface != NULL) {
    ALOGI("%s: registering DM profile callback with ss_interface", __func__);
    madvSSInterface->registerCallbacks(BT_PROFILE_ID_ADV,
                                       btif_advertiser_ss_callback);
  }
#ifdef SS_STUB_ENABLED
  if (madvSSStubInterface == NULL) {
    madvSSStubInterface = BluetoothSSStubInterface::getInstance();
    if (madvSSStubInterface == NULL) {
      ALOGI("%s adv single stack stub interface Initialization failed",
            __func__);
    }
  } else {
    ALOGI("adv single stack stub interface is already created");
  }
#endif
}

void btif_adv_ss_deinit() {
  if (madvSSInterface != NULL) {
    madvSSInterface->deregisterCallbacks(BT_PROFILE_GATT_ID);
  }
  if (madvSSInterface == NULL) {
    ALOGI("single stack interface is already null");
  } else {
    madvSSInterface = NULL;
  }
#ifdef SS_STUB_ENABLED
  if (madvSSStubInterface == NULL) {
    ALOGI("adv single stack stub interface is already null");
  } else {
    madvSSStubInterface = NULL;
  }
#endif
}

int AdvertiserSingleStackProto::postTxMessage(std::string msgStr) {
#ifndef SS_STUB_ENABLED
    if (madvSSInterface != NULL) {
        madvSSInterface->postTxMsg(msgStr);
    }
#else
    if (madvSSStubInterface != NULL) {
        madvSSStubInterface->postTxMsg(msgStr);
    }
#endif
    else {
        return BT_STATUS_FAIL;
    }
    return BT_STATUS_SUCCESS;
}
bool AdvertiserSingleStackProto::BleStartAdvertingSet(
    BleAdvertiserInterface::IdTxPowerStatusCallback Cb,
    const AdvertiseParameters& adv_param,
    const std::vector<uint8_t>& advertise_data,
    const std::vector<uint8_t>& scan_response_data,
    const PeriodicAdvertisingParameters& periodic_params,
    const std::vector<uint8_t>& periodic_data, int duration,
    int max_ext_adv_events, int reg_id,
    BleAdvertiserInterface::IdStatusCallback TimeoutCb) {
  ALOGD("\n BLE Start Advertising Set ");
#ifdef ADV_MODULE_SS_LOGS_ENABLED
  ALOGD("\n Advertising Event Properties : %d",
        adv_param.advertising_event_properties);
  ALOGD("\n Min interval : %d", adv_param.min_interval);
  ALOGD("\n Max interval : %d", adv_param.max_interval);
  ALOGD("\n channel Map : %d", adv_param.channel_map);
  ALOGD("\n Tx Power : %d", adv_param.tx_power);
  ALOGD("\n Primary Advertising Phy : %d", adv_param.primary_advertising_phy);
  ALOGD("\n Secondary Advertising Phy  : %d",
        adv_param.secondary_advertising_phy);
  ALOGD("\n scan_request_notification_enable : %d",
        adv_param.scan_request_notification_enable);
  for (uint16_t i = 0; i < advertise_data.size(); i++) {
    ALOGD("\n advertise_data[%d] : %d", i, advertise_data[i]);
  }
  for (uint16_t i = 0; i < scan_response_data.size(); i++) {
    ALOGD("\n scan_response_data[%d] : %d", i, scan_response_data[i]);
  }

  ALOGD("\n Periodic advertising enable : %d", periodic_params.enable);
  ALOGD("\n Periodic advertising Min interval : %d",
        periodic_params.min_interval);
  ALOGD("\n Periodic advertising Max interval : %d",
        periodic_params.max_interval);
  ALOGD("\n Periodic advertising Event Properties: %d",
        periodic_params.periodic_advertising_properties);

  for (uint16_t i = 0; i < periodic_data.size(); i++) {
    ALOGD("\n periodic_data[%d] : %d", i, periodic_data[i]);
  }

  ALOGD("\n duration : %d", duration);
  ALOGD("\n max_ext_adv_events : %d", max_ext_adv_events);
  ALOGD("\n reg_id : %d", reg_id);
#endif
  std::string encoded_bytes;
  ss_ble_start_advertising_set startAdvSet;
  ss_advertising_parameters* params = startAdvSet.mutable_parameters();
  ss_periodic_advertising_parameters* perodic_params =
      startAdvSet.mutable_periodicparameters();
  IdTxPowStatusCbMap.insert(
      pair<int, BleAdvertiserInterface::IdTxPowerStatusCallback>(reg_id, Cb));
  IdStatusTimeoutCbMap.insert(
      pair<int, BleAdvertiserInterface::IdStatusCallback>(reg_id, TimeoutCb));
  /*Populating Advertising Parameters*/
  params->set_advertisingeventproperties(
      adv_param.advertising_event_properties);
  params->set_mininterval(adv_param.min_interval);
  params->set_maxinterval(adv_param.max_interval);
  params->set_channelmap(adv_param.channel_map);
  params->set_txpower(adv_param.tx_power);
  params->set_primaryadvertisingphy(adv_param.primary_advertising_phy);
  params->set_secondaryadvertisingphy(adv_param.secondary_advertising_phy);
  params->set_scanrequestnotificationenable(
      adv_param.scan_request_notification_enable);

  /*Populating Advertising data*/
  for (uint16_t a = 0; a < advertise_data.size(); a++) {
    startAdvSet.add_advertisedata(advertise_data[a]);
  }

  /*Populating Scan Response data*/
  for (uint16_t a = 0; a < scan_response_data.size(); a++) {
    startAdvSet.add_scanresponse(scan_response_data[a]);
  }

  /*Populating Periodic Advertising Parameters*/
  perodic_params->set_enable(periodic_params.enable);
  perodic_params->set_mininterval(periodic_params.min_interval);
  perodic_params->set_maxinterval(periodic_params.max_interval);
  perodic_params->set_periodicadvertisingproperties(
      periodic_params.periodic_advertising_properties);

  /*Populating Scan Response data*/
  for (uint16_t a = 0; a < periodic_data.size(); a++) {
    startAdvSet.add_periodicdata(periodic_data[a]);
  }

  startAdvSet.set_duration(duration);
  startAdvSet.set_maxextadvevents(max_ext_adv_events);
  startAdvSet.set_regid(reg_id);

  startAdvSet.SerializeToString(&encoded_bytes);
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_ADV_START_ADV_SET, PROTO_ENC_DEC,
                                 encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool AdvertiserSingleStackProto::BleGetOwnAddress(
    BleAdvertiserInterface::GetAddressCallback Cb, int advertiser_id) {
  ALOGD("\n Get Own address ");
#ifdef ADV_MODULE_SS_LOGS_ENABLED
  ALOGD("\n advertiser_id : %d", advertiser_id);
#endif
  std::string encoded_bytes;
  ss_ble_get_own_address getOwnAdd;
  GetAddressCbMap.insert(
      pair<uint32_t, BleAdvertiserInterface::GetAddressCallback>(advertiser_id,
                                                                 Cb));
  getOwnAdd.set_advertiserid(advertiser_id);
  getOwnAdd.SerializeToString(&encoded_bytes);
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_GET_OWN_ADDRESS, PROTO_ENC_DEC,
                                 encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool AdvertiserSingleStackProto::BleStopAdvertisingSet(int advertiser_id) {
  ALOGD("\n Stop Advertising set ");
#ifdef ADV_MODULE_SS_LOGS_ENABLED
  ALOGD("\n advertiser_id : %d", advertiser_id);
#endif
  std::string encoded_bytes;
  ss_ble_stop_advertising_set stopAdvSet;
  stopAdvSet.set_advertiserid(advertiser_id);
  stopAdvSet.SerializeToString(&encoded_bytes);
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet =
      FormTxPacket(BT_LE_ADV_UNREG, PROTO_ENC_DEC, encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool AdvertiserSingleStackProto::BleEnableAdvertisingSet(
    BleAdvertiserInterface::StatusCallback Cb, int advertiser_id, bool enable,
    int duration, int max_ext_adv_events,
    BleAdvertiserInterface::StatusCallback TimeoutCb) {
  ALOGD("\n Enable Advertising Set ");
#ifdef ADV_MODULE_SS_LOGS_ENABLED
  ALOGD("\n advertiser_id : %d", advertiser_id);
  ALOGD("\n Advertising enable : %d", enable);
  ALOGD("\n duration : %d", duration);
  ALOGD("\n max_ext_adv_events : %d", max_ext_adv_events);
#endif
  std::string encoded_bytes;
  ss_ble_enable_advertising_set enableAdvSet;
  EnableCbMap.insert(pair<uint32_t, BleAdvertiserInterface::StatusCallback>(
      advertiser_id, Cb));
  TimeoutCbMap.insert(pair<uint32_t, BleAdvertiserInterface::StatusCallback>(
      advertiser_id, TimeoutCb));
  enableAdvSet.set_advertiserid(advertiser_id);
  enableAdvSet.set_enable(enable);
  enableAdvSet.set_duration(duration);
  enableAdvSet.set_maxextadvevents(max_ext_adv_events);
  enableAdvSet.SerializeToString(&encoded_bytes);
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet =
      FormTxPacket(BT_LE_ADV_ENABLE, PROTO_ENC_DEC, encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool AdvertiserSingleStackProto::BleSetData(
    BleAdvertiserInterface::StatusCallback Cb, int advertiser_id,
    bool scan_resp_data, const std::vector<uint8_t>& data) {
  ALOGD("\n BLE Set Adv or Scan Resp Data ");
#ifdef ADV_MODULE_SS_LOGS_ENABLED
  ALOGD("\n advertiser_id : %d", advertiser_id);
  ALOGD("\n scan_resp_data enable : %d", scan_resp_data);
  for (uint16_t i = 0; i < data.size(); i++) {
    ALOGD("\n data[%d] : %d", i, data[i]);
  }
#endif

  std::string encoded_bytes;
  ss_ble_set_data setData;
  if (!scan_resp_data) {
  SetAdvDataCbMap.insert(pair<uint32_t, BleAdvertiserInterface::StatusCallback>(
      advertiser_id, Cb));
  } else {
  SetScanRespDataCbMap.insert(pair<uint32_t, BleAdvertiserInterface::StatusCallback>(
      advertiser_id, Cb));
  }
  setData.set_advertiserid(advertiser_id);
  setData.set_scanrespdata(scan_resp_data);

  /*Populating adv data*/
  for (uint16_t a = 0; a < data.size(); a++) {
    setData.add_advdata(data[a]);
  }

  setData.SerializeToString(&encoded_bytes);
  // adding length
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_ADV_SET_DATA, PROTO_ENC_DEC, encoded_len,
                                 encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool AdvertiserSingleStackProto::BleSetAdvertisingParameters(
    BleAdvertiserInterface::ParametersCallback Cb, int advertiser_id,
    const AdvertiseParameters& adv_param) {
  ALOGD("\n BLE Set Advertising Parameters");

#ifdef ADV_MODULE_SS_LOGS_ENABLED
  ALOGD("\n advertiser_id : %d", advertiser_id);
  ALOGD("\n Advertising Event Properties : %d",
        adv_param.advertising_event_properties);
  ALOGD("\n Min interval : %d", adv_param.min_interval);
  ALOGD("\n Max interval : %d", adv_param.max_interval);
  ALOGD("\n channel Map : %d", adv_param.channel_map);
  ALOGD("\n Tx Power : %d", adv_param.tx_power);
  ALOGD("\n Primary Advertising Phy : %d", adv_param.primary_advertising_phy);
  ALOGD("\n Secondary Advertising Phy  : %d",
        adv_param.secondary_advertising_phy);
  ALOGD("\n scan_request_notification_enable : %d",
        adv_param.scan_request_notification_enable);
#endif
  std::string encoded_bytes;
  ss_ble_set_advertising_parameters advSet;
  ss_advertising_parameters* params = advSet.mutable_parameters();

  ParametersCbMap.insert(
      pair<int, BleAdvertiserInterface::ParametersCallback>(advertiser_id, Cb));
  advSet.set_advertiserid(advertiser_id);

  /*Populating Advertising Parameters*/
  params->set_advertisingeventproperties(
      adv_param.advertising_event_properties);
  params->set_mininterval(adv_param.min_interval);
  params->set_maxinterval(adv_param.max_interval);
  params->set_channelmap(adv_param.channel_map);
  params->set_txpower(adv_param.tx_power);
  params->set_primaryadvertisingphy(adv_param.primary_advertising_phy);
  params->set_secondaryadvertisingphy(adv_param.secondary_advertising_phy);
  params->set_scanrequestnotificationenable(
      adv_param.scan_request_notification_enable);

  advSet.SerializeToString(&encoded_bytes);
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_ADV_SET_PARAM, PROTO_ENC_DEC,
                                 encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool AdvertiserSingleStackProto::BleSetPeriodicAdvertisingParameters(
    BleAdvertiserInterface::StatusCallback Cb, int advertiser_id,
    const PeriodicAdvertisingParameters& periodic_params) {
  ALOGD("\n BLE Set Periodic Advertising Parameters");
  std::string encoded_bytes;
  ss_ble_set_periodic_advertising_parameters setPeriodicAdvParam;
  ss_periodic_advertising_parameters* perodic_params =
      setPeriodicAdvParam.mutable_parameters();

#ifdef ADV_MODULE_SS_LOGS_ENABLED
  ALOGD("\n advertiser_id : %d", advertiser_id);
  ALOGD("\n Periodic advertising enable : %d", periodic_params.enable);
  ALOGD("\n Periodic advertising Min interval : %d",
        periodic_params.min_interval);
  ALOGD("\n Periodic advertising Max interval : %d",
        periodic_params.max_interval);
  ALOGD("\n Periodic advertising Event Properties: %d",
        periodic_params.periodic_advertising_properties);
#endif

  setPeriodicAdvParam.set_advertiserid(advertiser_id);
  PeriodicAdvParamCbMap.insert(
      pair<uint32_t, BleAdvertiserInterface::StatusCallback>(advertiser_id,
                                                             Cb));
  /*Populating Periodic Advertising Parameters*/
  perodic_params->set_enable(periodic_params.enable);
  perodic_params->set_mininterval(periodic_params.min_interval);
  perodic_params->set_maxinterval(periodic_params.max_interval);
  perodic_params->set_periodicadvertisingproperties(
      periodic_params.periodic_advertising_properties);

  setPeriodicAdvParam.SerializeToString(&encoded_bytes);
  // adding length
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_ADV_SET_PERIODIC_ADV_PARAM,
                                 PROTO_ENC_DEC, encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool AdvertiserSingleStackProto::BlesetPeriodicAdvertisingData(
    BleAdvertiserInterface::StatusCallback Cb, int advertiser_id,
    const std::vector<uint8_t>& data) {
  ALOGD("\n BLE Set Periodic Advertising Data ");
#ifdef ADV_MODULE_SS_LOGS_ENABLED
  ALOGD("\n advertiser_id : %d", advertiser_id);
  for (uint16_t i = 0; i < data.size(); i++) {
    ALOGD("\n data[%d] : %d", i, data[i]);
  }
#endif
  std::string encoded_bytes;
  ss_ble_set_periodic_advertising_data setPeriodicAdvData;
  PeriodicAdvDataCbMap.insert(
      pair<uint32_t, BleAdvertiserInterface::StatusCallback>(advertiser_id,
                                                             Cb));
  setPeriodicAdvData.set_advertiserid(advertiser_id);

  /*Populating adv data*/
  for (uint16_t a = 0; a < data.size(); a++) {
    setPeriodicAdvData.add_data(data[a]);
  }

  setPeriodicAdvData.SerializeToString(&encoded_bytes);
  // adding length
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_ADV_SET_PERIODIC_ADV_DATA, PROTO_ENC_DEC,
                                 encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool AdvertiserSingleStackProto::BleSetPeriodicAdvertisingEnable(
    BleAdvertiserInterface::StatusCallback Cb, int advertiser_id, bool enable) {
  ALOGD("\n BLE Set Periodic Advertising Enable");
#ifdef ADV_MODULE_SS_LOGS_ENABLED
  ALOGD("\n advertiser_id : %d", advertiser_id);
  ALOGD("\n Periodic Advertising enable : %d", enable);
#endif
  std::string encoded_bytes;
  ss_ble_set_periodic_advertising_enable setPeriodicAdvEn;
  PeriodicAdvEnCbMap.insert(
      pair<uint32_t, BleAdvertiserInterface::StatusCallback>(advertiser_id,
                                                             Cb));
  setPeriodicAdvEn.set_advertiserid(advertiser_id);
  setPeriodicAdvEn.set_enable(enable);
  setPeriodicAdvEn.SerializeToString(&encoded_bytes);
  // adding length
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_ADV_SET_PERIODIC_ADV_ENABLE,
                                 PROTO_ENC_DEC, encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

/***************************CALLBACKS***********************/
void btif_advertiser_ss_callback(uint16_t event, char* p_param) {
  std::string resBufferString;
  resBufferString = Rxdatapacket(event,p_param);
  switch (event) {
    case BT_LE_ADVERTISING_SET_STARTED_EVENT: {
      ALOGD("BT_LE_ADVERTISING_SET_STARTED_EVENT");
      ss_ble_on_advertising_set_started_event onAdvSetStarted;
      int32_t reg_id = 0;
      uint32_t advertiser_id = 0;
      int32_t tx_power = 0;
      uint32_t status = 0;
      onAdvSetStarted.ParseFromString(resBufferString);
      if (onAdvSetStarted.has_advertiserid()) {
        advertiser_id = onAdvSetStarted.advertiserid();
        ALOGD("\nadvertiserid: %d ", advertiser_id);
      }
      if (onAdvSetStarted.has_txpower()) {
        tx_power = onAdvSetStarted.txpower();
        ALOGD("\n tx_power: %d ", tx_power);
      }
      if (onAdvSetStarted.has_status()) {
        status = onAdvSetStarted.status();
        ALOGD("\n status: 0x%d ", status);
      }
      if (onAdvSetStarted.has_regid()) {
        reg_id = onAdvSetStarted.regid();
        ALOGD("\n reg_id: %d ", reg_id);
      }
      RegIdAdvIdMap.insert(pair<int , uint32_t>(reg_id, advertiser_id));
      for (auto pair : IdTxPowStatusCbMap) {
        if (reg_id == pair.first) {
          do_in_jni_thread(Bind(pair.second, advertiser_id, tx_power, status));
          IdTxPowStatusCbMap.erase(reg_id);
          break;
        }
      }
      break;
    }
    case BT_LE_ADVERTISING_ENABLED_EVENT: {
      ALOGD("BT_LE_ADVERTISING_ENABLED_EVENT");
      ss_ble_on_advertising_enabled_event onAdvEn;
      uint32_t advertiser_id = 0;
      int reg_id = 0;
      bool enable = 0;
      uint32_t status = 0;
      bool callback_found = false;
      onAdvEn.ParseFromString(resBufferString);
      if (onAdvEn.has_advertiserid()) {
        advertiser_id = onAdvEn.advertiserid();
        ALOGD("\n advertiserid: %d ", advertiser_id);
      }
      if (onAdvEn.has_enable()) {
        enable = onAdvEn.enable();
        ALOGD("\n enable: %d ", enable);
      }
      if (onAdvEn.has_status()) {
        status = onAdvEn.status();
        ALOGD("\n status: 0x%d ", status);
      }

      for (auto pair : EnableCbMap) {
        if (advertiser_id == pair.first) {
          do_in_jni_thread(Bind(pair.second, status));
          EnableCbMap.erase(advertiser_id);
          callback_found = true;
          break;
        }
      }

      if (!callback_found && enable == false) {
        for (auto pair : TimeoutCbMap) {
          if (advertiser_id == pair.first) {
            do_in_jni_thread(Bind(pair.second, status));
            TimeoutCbMap.erase(advertiser_id);
            callback_found = true;
            break;
          }
        }
      }

      if (!callback_found && enable == false) {
        for (auto pair : RegIdAdvIdMap) {
          if (advertiser_id == pair.second) {
            reg_id = pair.first;
            break;
          }
        }

        auto it = IdStatusTimeoutCbMap.find(reg_id);
        if (it != IdStatusTimeoutCbMap.end()) {
          do_in_jni_thread(Bind(it->second, advertiser_id, status));
          IdStatusTimeoutCbMap.erase(it);
        }
      }

     break;
    }
    case BT_LE_ADVERTISING_DATA_SET_EVENT: {
      ALOGD("BT_LE_ADVERTISING_DATA_SET_EVENT");
      ss_ble_on_advertising_data_set_event onAdvDataSet;
      uint32_t advertiser_id = 0;
      uint32_t status = 0;
      onAdvDataSet.ParseFromString(resBufferString);
      if (onAdvDataSet.has_advertiserid()) {
        advertiser_id = onAdvDataSet.advertiserid();
        ALOGD("\n advertiserid: %d ", advertiser_id);
      }
      if (onAdvDataSet.has_status()) {
        status = onAdvDataSet.status();
        ALOGD("\n status: 0x%d ", status);
      }
      for (auto pair : SetAdvDataCbMap) {
        if (advertiser_id == pair.first) {
          do_in_jni_thread(Bind(pair.second, status));
          SetAdvDataCbMap.erase(advertiser_id);
          break;
        }
      }
      break;
    }
    case BT_LE_SCAN_RESP_DATA_SET_EVENT: {
      ALOGD("BT_LE_SCAN_RESP_DATA_SET_EVENT");
      ss_ble_on_scan_resp_data_event onScanRespData;
      uint32_t advertiser_id = 0;
      uint32_t status = 0;
      onScanRespData.ParseFromString(resBufferString);
      if (onScanRespData.has_advertiserid()) {
        advertiser_id = onScanRespData.advertiserid();
        ALOGD("\n advertiserid: %d ", advertiser_id);
      }
      if (onScanRespData.has_status()) {
        status = onScanRespData.status();
        ALOGD("\n status: 0x%d ", status);
      }
      for (auto pair : SetScanRespDataCbMap) {
        if (advertiser_id == pair.first) {
          do_in_jni_thread(Bind(pair.second, status));
          SetScanRespDataCbMap.erase(advertiser_id);
          break;
        }
      }
      break;
    }
    case BT_LE_ADV_PARAM_UPDATED_EVENT: {
      ALOGD("BT_LE_ADV_PARAM_UPDATED_EVENT");
      ss_ble_on_advertising_parameters_updated_event onAdvParam;
      uint32_t advertiser_id = 0;
      int32_t tx_power = 0;
      uint32_t status = 0;
      onAdvParam.ParseFromString(resBufferString);
      if (onAdvParam.has_advertiserid()) {
        advertiser_id = onAdvParam.advertiserid();
        ALOGD("\n advertiserid: %d ", advertiser_id);
      }
      if (onAdvParam.has_txpower()) {
        tx_power = onAdvParam.txpower();
        ALOGD("\n tx_power: %d ", tx_power);
      }

      if (onAdvParam.has_status()) {
        status = onAdvParam.status();
        ALOGD("\n status: 0x%x ", status);
      }
      for (auto pair : ParametersCbMap) {
        if (advertiser_id == pair.first) {
          do_in_jni_thread(Bind(pair.second, status, tx_power));
          ParametersCbMap.erase(advertiser_id);
          break;
        }
      }
      break;
    }
    case BT_LE_PERIODIC_ADV_PARAM_UPDATED_EVENT: {
      ALOGD("BT_LE_PERIODIC_ADV_PARAM_UPDATED_EVENT");
      ss_ble_on_periodic_advertising_parameters_updated_event
          onPeriodicAdvParam;
      uint32_t advertiser_id = 0;
      uint32_t status = 0;
      onPeriodicAdvParam.ParseFromString(resBufferString);
      if (onPeriodicAdvParam.has_advertiserid()) {
        advertiser_id = onPeriodicAdvParam.advertiserid();
        ALOGD("\nadvertiserid: 0x%d ", advertiser_id);
      }
      if (onPeriodicAdvParam.has_status()) {
        status = onPeriodicAdvParam.status();
        ALOGD("\n status: 0x%x ", status);
      }
      for (auto pair : PeriodicAdvParamCbMap) {
        if (advertiser_id == pair.first) {
          do_in_jni_thread(Bind(pair.second, status));
          PeriodicAdvParamCbMap.erase(advertiser_id);
          break;
        }
      }
      break;
    }
    case BT_LE_PERIODIC_ADVERTISING_DATA_SET_EVENT: {
      ALOGD("BT_LE_PERIODIC_ADVERTISING_DATA_SET_EVENT");
      ss_ble_on_periodic_advertising_data_set_event onPeriodicAdvDataSet;
      uint32_t advertiser_id = 0;
      uint32_t status = 0;
      onPeriodicAdvDataSet.ParseFromString(resBufferString);
      if (onPeriodicAdvDataSet.has_advertiserid()) {
        advertiser_id = onPeriodicAdvDataSet.advertiserid();
        ALOGD("\nadvertiserid: 0x%d ", advertiser_id);
      }
      if (onPeriodicAdvDataSet.has_status()) {
        status = onPeriodicAdvDataSet.status();
        ALOGD("\n status: 0x%x ", status);
      }
      for (auto pair : PeriodicAdvDataCbMap) {
        if (advertiser_id == pair.first) {
          do_in_jni_thread(Bind(pair.second, status));
          PeriodicAdvDataCbMap.erase(advertiser_id);
          break;
        }
      }
      break;
    }
    case BT_LE_PERIODIC_ADVERTISING_ENABLED_EVENT: {
      ALOGD("BT_LE_PERIODIC_ADVERTISING_ENABLED_EVENT");
      ss_ble_on_periodic_advertising_enabled_event onPeriodicAdvEn;
      uint32_t advertiser_id = 0;
      bool enable = 0;
      uint32_t status = 0;
      onPeriodicAdvEn.ParseFromString(resBufferString);
      if (onPeriodicAdvEn.has_advertiserid()) {
        advertiser_id = onPeriodicAdvEn.advertiserid();
        ALOGD("\nadvertiserid: 0x%d ", advertiser_id);
      }
      if (onPeriodicAdvEn.has_enable()) {
        enable = onPeriodicAdvEn.enable();
        ALOGD("\n enable: %d ", enable);
      }

      if (onPeriodicAdvEn.has_status()) {
        status = onPeriodicAdvEn.status();
        ALOGD("\n status: 0x%x ", status);
      }
      for (auto pair : PeriodicAdvEnCbMap) {
        if (advertiser_id == pair.first) {
          do_in_jni_thread(Bind(pair.second, status));
          PeriodicAdvEnCbMap.erase(advertiser_id);
          break;
        }
      }
      break;
    }
    case BT_LE_OWN_ADDRESS_READ_EVENT: {
      ALOGD("BT_LE_OWN_ADDRESS_READ_EVENT");
      ss_ble_on_own_address_read_event onOwnAddRead;
      uint32_t advertiser_id = 0;
      uint32_t addr_type = 0;
      RawAddress bd_addr;
      onOwnAddRead.ParseFromString(resBufferString);
      if (onOwnAddRead.has_advertiserid()) {
        advertiser_id = onOwnAddRead.advertiserid();
        ALOGD("\nadvertiserid: 0x%d ", advertiser_id);
      }
      if (onOwnAddRead.has_addresstype()) {
        addr_type = onOwnAddRead.addresstype();
        ALOGD("\n addr_type: %d ", addr_type);
      }

      if (onOwnAddRead.has_address()) {
        RawAddress::FromString(onOwnAddRead.address(), bd_addr);
        ALOGD("\n address is :: %s", bd_addr.ToString().c_str());
      }
      for (auto pair : GetAddressCbMap) {
        if (advertiser_id == pair.first) {
          do_in_jni_thread(Bind(pair.second, addr_type, bd_addr));
          GetAddressCbMap.erase(advertiser_id);
          break;
        }
      }
      break;
    }
    default: {
      ALOGD("btif_advertiser_ss_callback :: unknown msg id");
      break;
    }
  }
}
