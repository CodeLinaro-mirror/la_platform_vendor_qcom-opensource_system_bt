/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "btif_ss_advertiser.h"
#include <utils/Log.h>
#include <map>
#include <string>
#include <vector>
#include <hardware/bluetooth.h>
#include <hardware/bt_gatt.h>
#include <hardware/ble_advertiser.h>
#include "ble_advertiser.h"
#include "btif/protobuf/include/proto_message_ids.h"
#include "btif_ss_interface.h"
#include "protobuf/proto/advertiser.pb.h"
#include "btif_util.h"

using namespace std;
using base::Bind;

BluetoothSSInterface* madvSSInterface = NULL;
map<int, BleAdvertiserInterface::IdTxPowerStatusCallback> IdTxPowStatusCbMap;
BleAdvertiserInterface::IdStatusCallback gIdStatusCb;
map<uint32_t, BleAdvertiserInterface::ParametersCallback> ParametersCbMap;
map<uint32_t, BleAdvertiserInterface::StatusCallback> SetDataCbMap;
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
    madvSSInterface->registerCallbacks(BT_PROFILE_GATT_ID,
                                       btif_advertiser_ss_callback);
  }
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
}

int AdvertiserSingleStackProto::postTxMessage(std::string msgStr) {
    if (madvSSInterface != NULL) {
        madvSSInterface->postTxMsg(msgStr);
    } else {
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
  std::string encoded_bytes;
  ss_ble_start_advertising_set startAdvSet;
  ss_advertising_parameters* params = startAdvSet.mutable_parameters();
  ss_periodic_advertising_parameters* perodic_params =
      startAdvSet.mutable_periodicparameters();
  IdTxPowStatusCbMap.insert(
      pair<int, BleAdvertiserInterface::IdTxPowerStatusCallback>(reg_id, Cb));
  gIdStatusCb = TimeoutCb;
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
  for (uint8_t a = 0; a < advertise_data.size(); a++) {
    startAdvSet.add_advertisedata(advertise_data[a]);
  }

  /*Populating Scan Response data*/
  for (uint8_t a = 0; a < scan_response_data.size(); a++) {
    startAdvSet.add_scanresponse(scan_response_data[a]);
  }

  /*Populating Periodic Advertising Parameters*/
  perodic_params->set_enable(periodic_params.enable);
  perodic_params->set_mininterval(periodic_params.min_interval);
  perodic_params->set_maxinterval(periodic_params.max_interval);
  perodic_params->set_periodicadvertisingproperties(
      periodic_params.periodic_advertising_properties);

  /*Populating Scan Response data*/
  for (uint8_t a = 0; a < periodic_data.size(); a++) {
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
  std::string encoded_bytes;
  ss_ble_enable_advertising_set enableAdvSet;
  EnableCbMap.insert(pair<uint32_t, BleAdvertiserInterface::StatusCallback>(
      advertiser_id, Cb));
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
  std::string encoded_bytes;
  ss_ble_set_data setData;
  SetDataCbMap.insert(pair<uint32_t, BleAdvertiserInterface::StatusCallback>(
      advertiser_id, Cb));
  setData.set_advertiserid(advertiser_id);
  setData.set_scanrespdata(scan_resp_data);

  /*Populating adv data*/
  for (uint8_t a = 0; a < data.size(); a++) {
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
  std::string encoded_bytes;
  ss_ble_set_periodic_advertising_data setPeriodicAdvData;
  PeriodicAdvDataCbMap.insert(
      pair<uint32_t, BleAdvertiserInterface::StatusCallback>(advertiser_id,
                                                             Cb));
  setPeriodicAdvData.set_advertiserid(advertiser_id);

  /*Populating adv data*/
  for (uint8_t a = 0; a < data.size(); a++) {
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
        ALOGD("\n reg_id: %d ", status);
      }
      for (auto pair : IdTxPowStatusCbMap) {
        if (reg_id == pair.first) {
          do_in_jni_thread(Bind(pair.second, advertiser_id, tx_power, status));
          break;
        }
      }
      break;
    }
    case BT_LE_ADVERTISING_ENABLED_EVENT: {
      ALOGD("BT_LE_ADVERTISING_ENABLED_EVENT");
      ss_ble_on_advertising_enabled_event onAdvEn;
      uint32_t advertiser_id = 0;
      bool enable = 0;
      uint32_t status = 0;
      bool flag = 0;
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
          flag = 1;
          break;
        }
      }
      if ((!flag) && (enable == false)) {
        do_in_jni_thread(Bind(gIdStatusCb, advertiser_id, status));
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
      for (auto pair : SetDataCbMap) {
        if (advertiser_id == pair.first) {
          do_in_jni_thread(Bind(pair.second, status));
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
      for (auto pair : SetDataCbMap) {
        if (advertiser_id == pair.first) {
          do_in_jni_thread(Bind(pair.second, status));
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

