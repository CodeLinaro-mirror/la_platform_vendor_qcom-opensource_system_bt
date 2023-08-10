/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <hardware/ble_scanner.h>
#include <hardware/bluetooth.h>
#include <utils/Log.h>

#include <map>
#include <string>
#include <vector>

#include "advertise_data_parser.h"
#include "btif/protobuf/include/proto_message_ids.h"
#include "btif_dm.h"
#include "btif_gatt.h"
#include "btif_gatt_util.h"
#include "btif_ss_interface.h"
#include "btif_ss_scanner.h"
#include "btif_storage.h"
#include "btif_util.h"
#include "protobuf/proto/scan.pb.h"

using namespace std;
using base::Bind;
using base::Owned;

extern const btgatt_callbacks_t* bt_gatt_callbacks;

#define SCAN_CBACK_IN_JNI(P_CBACK, ...)                             \
  do {                                                              \
    if (bt_gatt_callbacks && bt_gatt_callbacks->scanner->P_CBACK) { \
      ALOGD("HAL bt_gatt_callbacks->client->%s", #P_CBACK);         \
      do_in_jni_thread(                                             \
          Bind(bt_gatt_callbacks->scanner->P_CBACK, __VA_ARGS__));  \
    } else {                                                        \
      ASSERTC(0, "Callback is NULL", 0);                            \
    }                                                               \
  } while (0)

BluetoothSSInterface* mScanSSInterface = NULL;

map<bluetooth::Uuid, BleScannerInterface::RegisterCallback>
    RegisterScannerCbMap;
map<uint32_t, BleScannerInterface::FilterConfigCallback> ScanFiltrClearCbMap;
map<uint32_t, BleScannerInterface::EnableCallback> ScanFiltrEnCbMap;
map<uint32_t, BleScannerInterface::FilterConfigCallback> ScanFiltrAddCbMap;
map<uint32_t, BleScannerInterface::FilterParamSetupCallback>
    ScanFiltrParamSetCbMap;
map<uint32_t, BleScannerInterface::Callback> SetScanParamCbMap;
map<uint32_t, BleScannerInterface::Callback> BatchScanCfgStorageCbMap;
map<uint32_t, BleScannerInterface::Callback> BatchScanEnCbMap;
map<uint32_t, BleScannerInterface::Callback> BatchScanDisCbMap;

void btif_scan_ss_init() {
  ALOGI("%s ", __func__);
  if (mScanSSInterface == NULL) {
    mScanSSInterface = BluetoothSSInterface::getInstance();
    if (mScanSSInterface == NULL) {
      ALOGI("%s single stack interface Initialization failed", __func__);
    }
  } else {
    ALOGI("single stack interface is already created");
  }
  if (mScanSSInterface != NULL) {
    ALOGI("%s: registering BLE Scan profile callback with ss_interface",
          __func__);
    mScanSSInterface->registerCallbacks(BT_PROFILE_ID_SCAN,
                                        btif_scanner_ss_callback);
  }
}

void btif_scan_ss_deinit() {
  if (mScanSSInterface != NULL) {
    mScanSSInterface->deregisterCallbacks(BT_PROFILE_GATT_ID);
  }
  if (mScanSSInterface == NULL) {
    ALOGI("single stack interface is already null");
  } else {
    mScanSSInterface = NULL;
  }

}

int ScannerSingleStackProto::postTxMessage(std::string msgStr) {
#ifndef SS_STUB_ENABLED
  if (mScanSSInterface != NULL) {
    mScanSSInterface->postTxMsg(msgStr);
  }
#else
  if (mScanSSStubInterface != NULL) {
    mScanSSStubInterface->postTxMsg(msgStr);
  }
#endif
  else {
    return BT_STATUS_FAIL;
  }
  return BT_STATUS_SUCCESS;
}
// all access to this variable should be done on the jni thread
std::set<RawAddress> remote_bdaddr_cache;
std::queue<RawAddress> remote_bdaddr_cache_ordered;
const size_t remote_bdaddr_cache_max_size = 1024;

void btif_address_cache_add(const RawAddress& p_bda, uint8_t addr_type) {
  // Remove the oldest entries
  while (remote_bdaddr_cache.size() >= remote_bdaddr_cache_max_size) {
    const RawAddress& raw_address = remote_bdaddr_cache_ordered.front();
    remote_bdaddr_cache.erase(raw_address);
    remote_bdaddr_cache_ordered.pop();
  }
  remote_bdaddr_cache.insert(p_bda);
  remote_bdaddr_cache_ordered.push(p_bda);
}

bool btif_address_cache_find(const RawAddress& p_bda) {
  return (remote_bdaddr_cache.find(p_bda) != remote_bdaddr_cache.end());
}

void btif_address_cache_init(void) {
  remote_bdaddr_cache.clear();
  remote_bdaddr_cache_ordered = {};
}

void bta_batch_scan_threshold_cb(tBTM_BLE_REF_VALUE ref_value) {
  SCAN_CBACK_IN_JNI(batchscan_threshold_cb, ref_value);
}

void bta_batch_scan_reports_cb(int client_id, tBTA_STATUS status,
                               uint8_t report_format, uint8_t num_records,
                               std::vector<uint8_t> data) {
  SCAN_CBACK_IN_JNI(batchscan_reports_cb, client_id, status, report_format,
                    num_records, std::move(data));
}

void bta_scan_results_cb_impl(RawAddress bd_addr, tBT_DEVICE_TYPE device_type,
                              int8_t rssi, uint8_t addr_type,
                              uint16_t ble_evt_type, uint8_t ble_primary_phy,
                              uint8_t ble_secondary_phy,
                              uint8_t ble_advertising_sid, int8_t ble_tx_power,
                              uint16_t ble_periodic_adv_int,
                              vector<uint8_t> value, RawAddress original_bda) {
  uint8_t remote_name_len;
  bt_device_type_t dev_type;
  bt_property_t properties;

  ALOGD("\naddress: %s ", bd_addr.ToString().c_str());
  ALOGD("\noriginal_bda: %s ", original_bda.ToString().c_str());
  ALOGD("\naddr_type: %d ", addr_type);
  ALOGD("\n size of value: %d ", value.size());
  for (uint8_t i = 0; i < value.size(); i++) {
    ALOGD("\n value[%d]: %d ", i, value[i]);
  }
  const uint8_t* p_eir_remote_name = AdvertiseDataParser::GetFieldByType(
      value, BTM_EIR_COMPLETE_LOCAL_NAME_TYPE, &remote_name_len);
    ALOGD("\n p_eir_remote_name: %s", p_eir_remote_name);
    ALOGD("\n remote_name_len: %d ", remote_name_len);

  if (p_eir_remote_name == NULL) {
    p_eir_remote_name = AdvertiseDataParser::GetFieldByType(
        value, BT_EIR_SHORTENED_LOCAL_NAME_TYPE, &remote_name_len);
    ALOGD("\n EIR Shortened local name");
    ALOGD("\n p_eir_remote_name: %s ", p_eir_remote_name);
    ALOGD("\n remote_name_len: %d ", remote_name_len);
  }

  if ((addr_type != BLE_ADDR_RANDOM) || (p_eir_remote_name)) {
    ALOGD("\n addr_type is public or has eir_remote_name ");
    if (!btif_address_cache_find(bd_addr)) {
    ALOGD("\n add address to the cache");
      btif_address_cache_add(bd_addr, addr_type);

      if (p_eir_remote_name) {
        if (remote_name_len > BD_NAME_LEN + 1 ||
            (remote_name_len == BD_NAME_LEN + 1 &&
             p_eir_remote_name[BD_NAME_LEN] != '\0')) {
          ALOGD(
              "bta_scan_results_cb_impl dropping invalid packet - device name "
              "too long: %d",
              remote_name_len);
          return;
        }

        bt_bdname_t bdname;
        memcpy(bdname.name, p_eir_remote_name, remote_name_len);
        if (remote_name_len < BD_NAME_LEN + 1)
          bdname.name[remote_name_len] = '\0';

        ALOGD("bta_scan_results_cb_impl BLE device name=%s len=%d dev_type=%d",
              bdname.name, remote_name_len, device_type);
        ALOGD("\n sending to dm_update_ble_remote_properties: bdname: %s ", bdname.name);
         btif_dm_update_ble_remote_properties(bd_addr, bdname.name, device_type);
      }
    }
  }

  dev_type = (bt_device_type_t)device_type;
  BTIF_STORAGE_FILL_PROPERTY(&properties, BT_PROPERTY_TYPE_OF_DEVICE,
                             sizeof(dev_type), &dev_type);
  // btif_storage_set_remote_device_property(&(bd_addr), &properties);

  // btif_storage_set_remote_addr_type(&bd_addr, addr_type);
  HAL_CBACK(bt_gatt_callbacks, scanner->scan_result_cb, ble_evt_type, addr_type,
            &bd_addr, ble_primary_phy, ble_secondary_phy, ble_advertising_sid,
            ble_tx_power, rssi, ble_periodic_adv_int, std::move(value),
            &original_bda);
}

void bta_track_adv_event_cb(tBTM_BLE_TRACK_ADV_DATA* p_track_adv_data) {
  btgatt_track_adv_info_t* btif_scan_track_cb = new btgatt_track_adv_info_t;

  BTIF_TRACE_DEBUG("%s", __func__);
  btif_gatt_move_track_adv_data(btif_scan_track_cb,
                                (btgatt_track_adv_info_t*)p_track_adv_data);

  SCAN_CBACK_IN_JNI(track_adv_event_cb, Owned(btif_scan_track_cb));
}

bool ScannerSingleStackProto::RegisterScanner(
    const bluetooth::Uuid& scan_uuid,
    BleScannerInterface::RegisterCallback Cb) {
  ALOGD("\n BLE RegisterScanner ");
#ifdef SCAN_MODULE_SS_LOGS_ENABLED
  ALOGD("\n scan_uuid : %s", scan_uuid.ToString().c_str());
#endif
  std::string encoded_bytes;
  ss_ble_register_scanner regScan;
  RegisterScannerCbMap.insert(
      pair<bluetooth::Uuid, BleScannerInterface::RegisterCallback>(scan_uuid,
                                                                   Cb));
  regScan.set_uuid(scan_uuid.ToString());
  regScan.SerializeToString(&encoded_bytes);
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_SCAN_REGISTER_SCANNER, PROTO_ENC_DEC,
                                    encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool ScannerSingleStackProto::UnRegisterScanner(int scanner_id) {
  ALOGD("\n BLE UnRegisterScanner ");
#ifdef SCAN_MODULE_SS_LOGS_ENABLED
  ALOGD("\n scanner_id : %d", scanner_id);
#endif
  std::string encoded_bytes;
  ss_ble_unregister_scanner unregScan;
  unregScan.set_scannerid(scanner_id);
  unregScan.SerializeToString(&encoded_bytes);
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_SCAN_UNREGISTER_SCANNER,
                                    PROTO_ENC_DEC, encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool ScannerSingleStackProto::StartScanning(bool start) {
  ALOGD("\n BLE StartScanning ");
#ifdef SCAN_MODULE_SS_LOGS_ENABLED
  ALOGD("\n start : %d", start);
#endif
  std::string encoded_bytes;
  ss_ble_start_scan startScan;
  startScan.set_start(start);
  startScan.SerializeToString(&encoded_bytes);
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_SCAN_START_SCANNING, PROTO_ENC_DEC,
                                    encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool ScannerSingleStackProto::ScanFilterClear(
    int client_if, int filter_index,
    BleScannerInterface::FilterConfigCallback Cb) {
  ALOGD("\n BLE Scan Filter Clear");
#ifdef SCAN_MODULE_SS_LOGS_ENABLED
  ALOGD("\n client_if : %d", client_if);
  ALOGD("\n filter_index : %d", filter_index);
#endif
  std::string encoded_bytes;
  ss_ble_scan_filter_clear scanFilterClear;
  ScanFiltrClearCbMap.insert(
      pair<uint32_t, BleScannerInterface::FilterConfigCallback>(client_if, Cb));
  scanFilterClear.set_clientif(client_if);
  scanFilterClear.set_filterindex(filter_index);
  scanFilterClear.SerializeToString(&encoded_bytes);
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_SCAN_FILTER_CLEAR, PROTO_ENC_DEC,
                                    encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool ScannerSingleStackProto::ScanFilterEnable(
    int client_if, bool enable, BleScannerInterface::EnableCallback Cb) {
  ALOGD("\n BLE Scan Filter Enable");
#ifdef SCAN_MODULE_SS_LOGS_ENABLED
  ALOGD("\n client_if : %d", client_if);
  ALOGD("\n enable : %d", enable);
#endif
  std::string encoded_bytes;
  ss_ble_scan_filter_enable scanFilterEn;
  ScanFiltrEnCbMap.insert(
      pair<uint32_t, BleScannerInterface::EnableCallback>(client_if, Cb));
  scanFilterEn.set_clientif(client_if);
  scanFilterEn.set_enable(enable);
  scanFilterEn.SerializeToString(&encoded_bytes);
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_SCAN_FILTER_ENABLE, PROTO_ENC_DEC,
                                    encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool ScannerSingleStackProto::ScanFilterAdd(
    int client_if, int filter_index, std::vector<ApcfCommand> filters,
    BleScannerInterface::FilterConfigCallback Cb) {
  ALOGD("\n BLE Scan Filter Add");
#ifdef SCAN_MODULE_SS_LOGS_ENABLED
  ALOGD("\n client_if : %d", client_if);
  ALOGD("\n filter_index : %d", filter_index);
  ALOGD("\n APCF command parameters");
#endif
  std::string encoded_bytes;
  ss_ble_scan_filter_add scanFltAdd;
  ScanFiltrAddCbMap.insert(
      pair<uint32_t, BleScannerInterface::FilterConfigCallback>(client_if, Cb));
  scanFltAdd.set_clientif(client_if);
  scanFltAdd.set_filterindex(filter_index);
  /* Populating ApcfCommand structure parameters */
  for (const ApcfCommand& flts : filters) {
    ss_ble_apcfcommand* params = scanFltAdd.add_apcfcommand();
    params->set_type(flts.type);
    params->set_address(ToRawString(&(flts.address)).c_str());
    params->set_addr_type(flts.addr_type);
    params->set_uuid(flts.uuid.ToString());
    params->set_uuid_mask(flts.uuid_mask.ToString());
    std::string strname(flts.name.begin(), flts.name.end());
    params->set_name(strname);
    params->set_company(flts.company);
    params->set_company_mask(flts.company_mask);
    std::string strdata(flts.data.begin(), flts.data.end());
    params->set_data(strdata);
    std::string strdatamask(flts.data_mask.begin(), flts.data_mask.end());
    params->set_data_mask(strdatamask);
    params->set_org_id(flts.org_id);
    params->set_tds_flags(flts.tds_flags);
    params->set_tds_flags_mask(flts.tds_flags_mask);
    params->set_group_filter_enabled(flts.group_filter_enabled);
    std::string strirk(flts.irk.begin(), flts.irk.end());
    params->set_irk(strirk);
#ifdef SCAN_MODULE_SS_LOGS_ENABLED
    ALOGD("\n type : %d", flts.type);
    ALOGD("\n address : %s", ToRawString(&(flts.address)).c_str());
    ALOGD("\n addr_type : %d", flts.addr_type);
    ALOGD("\n uuid : %s", flts.uuid.ToString().c_str());
    ALOGD("\n uuid_mask : %s", flts.uuid_mask.ToString().c_str());
    ALOGD("\n name : %s", &strname[0]);
    ALOGD("\n company : %d", flts.company);
    ALOGD("\n company_mask : %d", flts.company_mask);
    ALOGD("\n data : %s", &strdata[0]);
    ALOGD("\n data_mask : %s", &strdatamask[0]);
    ALOGD("\n org_id : %d", flts.org_id);
    ALOGD("\n tds_flags : %d", flts.tds_flags);
    ALOGD("\n tds_flags_mask : %d", flts.tds_flags_mask);
    ALOGD("\n group_filter_enabled : %d", flts.group_filter_enabled);
    ALOGD("\n irk : %s", &strirk[0]);
#endif
  }
  scanFltAdd.SerializeToString(&encoded_bytes);
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_SCAN_FILTER_ADD, PROTO_ENC_DEC,
                                    encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool ScannerSingleStackProto::ScanFilterParamSetup(
    uint8_t client_if, uint8_t action, uint8_t filt_index,
    std::unique_ptr<btgatt_filt_param_setup_t> filt_param,
    BleScannerInterface::FilterParamSetupCallback Cb) {
  ALOGD("\n BLE Scan Filter ParamSetup");
#ifdef SCAN_MODULE_SS_LOGS_ENABLED
  ALOGD("\n client_if : %d", client_if);
  ALOGD("\n action : %d", action);
  ALOGD("\n filt_index : %d", filt_index);
  if(filt_param != NULL)
  {
  ALOGD("\n bt_gatt_filt_param_setup parameters");
  ALOGD("\n feat_seln : %d", filt_param->feat_seln);
  ALOGD("\n list_logic_type : %d", filt_param->list_logic_type);
  ALOGD("\n filt_logic_type : %d", filt_param->filt_logic_type);
  ALOGD("\n rssi_high_thres : %d", filt_param->rssi_high_thres);
  ALOGD("\n rssi_low_thres : %d", filt_param->rssi_low_thres);
  ALOGD("\n dely_mode : %d", filt_param->dely_mode);
  ALOGD("\n found_timeout : %d", filt_param->found_timeout);
  ALOGD("\n lost_timeout : %d", filt_param->lost_timeout);
  ALOGD("\n found_timeout_cnt : %d", filt_param->found_timeout_cnt);
  ALOGD("\n num_of_tracking_entries : %d", filt_param->num_of_tracking_entries);
  }
#endif
  std::string encoded_bytes;
  ss_ble_scan_filter_param_setup scanFltrParam;
  ScanFiltrParamSetCbMap.insert(
      pair<uint32_t, BleScannerInterface::FilterParamSetupCallback>(client_if,
                                                                    Cb));
  ss_btgatt_filt_param_setup* params =
      scanFltrParam.mutable_btgatt_filt_param_setup();
  scanFltrParam.set_clientif(client_if);
  scanFltrParam.set_action(action);
  scanFltrParam.set_filterindex(filt_index);
  /* Populating bt_gatt_filter_param_setup structure parameters */
  if(filt_param != NULL)
  {
  params->set_feat_seln(filt_param->feat_seln);
  params->set_list_logic_type(filt_param->list_logic_type);
  params->set_filt_logic_type(filt_param->filt_logic_type);
  params->set_rssi_high_thres(filt_param->rssi_high_thres);
  params->set_rssi_low_thres(filt_param->rssi_low_thres);
  params->set_dely_mode(filt_param->dely_mode);
  params->set_found_timeout(filt_param->found_timeout);
  params->set_lost_timeout(filt_param->lost_timeout);
  params->set_found_timeout_cnt(filt_param->found_timeout_cnt);
  params->set_num_of_tracking_entries(filt_param->num_of_tracking_entries);
  }
  scanFltrParam.SerializeToString(&encoded_bytes);
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_SCAN_FILTER_PARAM_SETUP,
                                    PROTO_ENC_DEC, encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool ScannerSingleStackProto::SetScanParameters(
    int client_if, int scan_phy, std::vector<uint32_t> scan_interval,
    std::vector<uint32_t> scan_window, BleScannerInterface::Callback Cb) {
  ALOGD("\n BLE Set Scan Parameters");
#ifdef SCAN_MODULE_SS_LOGS_ENABLED
  ALOGD("\n client_if : %d", client_if);
  ALOGD("\n scan_phy : %d", scan_phy);
  for (uint8_t i = 0; i < scan_interval.size(); i++) {
    ALOGD("\n scan_interval[%d] : %d", i, scan_interval[i]);
  }
  for (uint8_t i = 0; i < scan_window.size(); i++) {
    ALOGD("\n scan_window[%d] : %d", i, scan_window[i]);
  }
#endif
  std::string encoded_bytes;
  ss_ble_set_scan_param setScanParam;
  SetScanParamCbMap.insert(
      pair<uint32_t, BleScannerInterface::Callback>(client_if, Cb));
  setScanParam.set_clientif(client_if);
  setScanParam.set_scanphy(scan_phy);
  for (uint8_t a = 0; a < scan_interval.size(); a++) {
    setScanParam.add_scaninterval(scan_interval[a]);
  }
  for (uint8_t a = 0; a < scan_window.size(); a++) {
    setScanParam.add_scanwindow(scan_window[a]);
  }
  setScanParam.SerializeToString(&encoded_bytes);
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_SCAN_SET_SCAN_PARAM, PROTO_ENC_DEC,
                                    encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool ScannerSingleStackProto::BatchscanConfigStorage(
    int client_if, int batch_scan_full_max, int batch_scan_trunc_max,
    int batch_scan_notify_threshold, BleScannerInterface::Callback Cb) {
  ALOGD("\n BLE Batch Scan Config Storage");
#ifdef SCAN_MODULE_SS_LOGS_ENABLED
  ALOGD("\n client_if : %d", client_if);
  ALOGD("\n batch_scan_full_max : %d", batch_scan_full_max);
  ALOGD("\n batch_scan_trunc_max : %d", batch_scan_trunc_max);
  ALOGD("\n batch_scan_notify_threshold : %d", batch_scan_notify_threshold);
#endif
  std::string encoded_bytes;
  ss_ble_batch_config_storage batchScanConfig;
  BatchScanCfgStorageCbMap.insert(
      pair<uint32_t, BleScannerInterface::Callback>(client_if, Cb));
  batchScanConfig.set_clientif(client_if);
  batchScanConfig.set_batch_scan_full_max(batch_scan_full_max);
  batchScanConfig.set_batch_scan_trunc_max(batch_scan_trunc_max);
  batchScanConfig.set_batch_scan_notify_threshold(batch_scan_notify_threshold);
  batchScanConfig.SerializeToString(&encoded_bytes);
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_SCAN_BATCH_SCAN_CONFIG, PROTO_ENC_DEC,
                                    encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool ScannerSingleStackProto::BatchscanEnable(
    int client_if, int scan_mode, int scan_interval, int scan_window,
    int addr_type, int discard_rule, BleScannerInterface::Callback Cb) {
  ALOGD("\n BLE Batch Scan Enable");
#ifdef SCAN_MODULE_SS_LOGS_ENABLED
  ALOGD("\n client_if : %d", client_if);
  ALOGD("\n scan_mode : %d", scan_mode);
  ALOGD("\n scan_interval : %d", scan_interval);
  ALOGD("\n scan_window : %d", scan_window);
  ALOGD("\n addr_type : %d", addr_type);
  ALOGD("\n discard_rule : %d", discard_rule);
#endif
  std::string encoded_bytes;
  ss_ble_batch_scan_enable batchScanEn;
  BatchScanEnCbMap.insert(
      pair<uint32_t, BleScannerInterface::Callback>(client_if, Cb));
  batchScanEn.set_clientif(client_if);
  batchScanEn.set_scan_mode(scan_mode);
  batchScanEn.set_scan_interval(scan_interval);
  batchScanEn.set_scan_window(scan_window);
  batchScanEn.set_addr_type(addr_type);
  batchScanEn.set_discard_rule(discard_rule);
  batchScanEn.SerializeToString(&encoded_bytes);
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_SCAN_BATCH_SCAN_ENABLE, PROTO_ENC_DEC,
                                    encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool ScannerSingleStackProto::BatchscanDisable(
    int client_if, BleScannerInterface::Callback Cb) {
  ALOGD("\n BLE Batch Scan Disable");
#ifdef SCAN_MODULE_SS_LOGS_ENABLED
  ALOGD("\n client_if : %d", client_if);
#endif
  std::string encoded_bytes;
  ss_ble_batch_scan_disable batchScanDisable;
  BatchScanDisCbMap.insert(
      pair<uint32_t, BleScannerInterface::Callback>(client_if, Cb));
  batchScanDisable.set_clientif(client_if);
  batchScanDisable.SerializeToString(&encoded_bytes);
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_SCAN_BATCH_SCAN_DISABLE,
                                    PROTO_ENC_DEC, encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

bool ScannerSingleStackProto::BatchscanReadReports(int client_if,
                                                   int scan_mode) {
  ALOGD("\n BLE Batch Scan ReadReports");
#ifdef SCAN_MODULE_SS_LOGS_ENABLED
  ALOGD("\n client_if : %d", client_if);
  ALOGD("\n scan_mode : %d", scan_mode);
#endif
  std::string encoded_bytes;
  ss_ble_batch_scan_read_reports batchScanRd;
  batchScanRd.set_clientif(client_if);
  batchScanRd.set_scan_mode(scan_mode);
  batchScanRd.SerializeToString(&encoded_bytes);
  uint16_t encoded_len = encoded_bytes.length();
  std::string packet = FormTxPacket(BT_LE_SCAN_BATCH_SCAN_READ_REPORT,
                                    PROTO_ENC_DEC, encoded_len, encoded_bytes);
  uint16_t status = postTxMessage(packet);
  if (status != BT_STATUS_SUCCESS) {
    return false;
  }
  return true;
}

/***************************CALLBACKS***********************/
void btif_scanner_ss_callback(uint16_t event, char* p_param) {
  std::string resBufferString;
  resBufferString = Rxdatapacket(event, p_param);
  switch (event) {
    case BT_LE_SCAN_REGISTER_SCANNER_EVENT: {
      ALOGD("BT_LE_SCAN_REGISTER_SCANNER_EVENT");
      ss_ble_scanner_register_event onScannerReg;
      bluetooth::Uuid scan_uuid;
      uint32_t scanner_id = 0;
      uint32_t status = 0;
      onScannerReg.ParseFromString(resBufferString);
      if (onScannerReg.has_uuid()) {
        scan_uuid = bluetooth::Uuid::FromString(onScannerReg.uuid());
        ALOGD("\n scan_uuid: %s ", scan_uuid.ToString().c_str());
      }
      if (onScannerReg.has_scannerid()) {
        scanner_id = onScannerReg.scannerid();
        ALOGD("\n scanner_id: %d ", scanner_id);
      }
      if (onScannerReg.has_status()) {
        status = onScannerReg.status();
        ALOGD("\n status: 0x%d ", status);
      }
      for (auto pair : RegisterScannerCbMap) {
        if (scan_uuid == pair.first) {
          do_in_jni_thread(Bind(pair.second, scanner_id, status));
          RegisterScannerCbMap.erase(scan_uuid);
          break;
        }
      }
      break;
    }
    case BT_LE_SCAN_FILTER_ENABLE_EVENT: {
      ALOGD("BT_LE_SCAN_FILTER_ENABLE_EVENT");
      ss_ble_scan_filter_enable_event onScanFltEn;
      uint32_t client_if = 0;
      uint32_t action = 0;
      uint32_t status = 0;
      onScanFltEn.ParseFromString(resBufferString);
      if (onScanFltEn.has_clientif()) {
        client_if = onScanFltEn.clientif();
        ALOGD("\n client_if: %d ", client_if);
      }
      if (onScanFltEn.has_action()) {
        action = onScanFltEn.action();
        ALOGD("\n action: %d ", action);
      }
      if (onScanFltEn.has_status()) {
        status = onScanFltEn.status();
        ALOGD("\n status: 0x%d ", status);
      }
      for (auto pair : ScanFiltrEnCbMap) {
        if (client_if == pair.first) {
          do_in_jni_thread(Bind(pair.second, action, status));
          ScanFiltrEnCbMap.erase(client_if);
          break;
        }
      }
      break;
    }
    case BT_LE_SCAN_FILTER_ADD_EVENT: {
      ALOGD("BT_LE_SCAN_FILTER_ADD_EVENT");
      ss_ble_scan_filter_add_event onScanFltAdd;
      uint32_t client_if = 0;
      uint32_t filter_type = 0;
      uint32_t available_space = 0;
      uint32_t action = 0;
      uint32_t status = 0;
      onScanFltAdd.ParseFromString(resBufferString);
      if (onScanFltAdd.has_clientif()) {
        client_if = onScanFltAdd.clientif();
        ALOGD("\n client_if: %d ", client_if);
      }
      if (onScanFltAdd.has_filt_type()) {
        filter_type = onScanFltAdd.filt_type();
        ALOGD("\n filter_type: %d ", filter_type);
      }
      if (onScanFltAdd.has_avbl_space()) {
        available_space = onScanFltAdd.avbl_space();
        ALOGD("\n available_space: %d ", available_space);
      }
      if (onScanFltAdd.has_action()) {
        action = onScanFltAdd.action();
        ALOGD("\n action: %d ", action);
      }
      if (onScanFltAdd.has_status()) {
        status = onScanFltAdd.status();
        ALOGD("\n status: 0x%d ", status);
      }
      for (auto pair : ScanFiltrAddCbMap) {
        if (client_if == pair.first) {
          do_in_jni_thread(
              Bind(pair.second, filter_type, available_space, action, status));
          ScanFiltrAddCbMap.erase(client_if);
          break;
        }
      }
      break;
    }
    case BT_LE_SCAN_FILTER_CLEAR_EVENT: {
      ALOGD("BT_LE_SCAN_FILTER_CLEAR_EVENT");
      ss_ble_scan_filter_clear_event onScanFltClear;
      uint32_t client_if = 0;
      uint32_t filter_type = 0;
      uint32_t available_space = 0;
      uint32_t action = 0;
      uint32_t status = 0;
      onScanFltClear.ParseFromString(resBufferString);
      if (onScanFltClear.has_clientif()) {
        client_if = onScanFltClear.clientif();
        ALOGD("\n client_if: %d ", client_if);
      }
      if (onScanFltClear.has_filt_type()) {
        filter_type = onScanFltClear.filt_type();
        ALOGD("\n filter_type: %d ", filter_type);
      }
      if (onScanFltClear.has_avbl_space()) {
        available_space = onScanFltClear.avbl_space();
        ALOGD("\n available_space: %d ", available_space);
      }
      if (onScanFltClear.has_action()) {
        action = onScanFltClear.action();
        ALOGD("\n action: %d ", action);
      }
      if (onScanFltClear.has_status()) {
        status = onScanFltClear.status();
        ALOGD("\n status: 0x%d ", status);
      }
      for (auto pair : ScanFiltrClearCbMap) {
        if (client_if == pair.first) {
          do_in_jni_thread(
              Bind(pair.second, filter_type, available_space, action, status));
          ScanFiltrClearCbMap.erase(client_if);
          break;
        }
      }
      break;
    }
    case BT_LE_SCAN_FILTER_PARAM_SETUP_EVENT: {
      ALOGD("BT_LE_SCAN_FILTER_PARAM_SETUP_EVENT");
      ss_ble_scan_filter_param_setup_event onScanFltParamSetup;
      uint32_t client_if = 0;
      uint32_t available_space = 0;
      uint32_t action = 0;
      uint32_t status = 0;
      onScanFltParamSetup.ParseFromString(resBufferString);
      if (onScanFltParamSetup.has_clientif()) {
        client_if = onScanFltParamSetup.clientif();
        ALOGD("\n client_if: %d ", client_if);
      }
      if (onScanFltParamSetup.has_avbl_space()) {
        available_space = onScanFltParamSetup.avbl_space();
        ALOGD("\n available_space: %d ", available_space);
      }
      if (onScanFltParamSetup.has_action()) {
        action = onScanFltParamSetup.action();
        ALOGD("\n action: %d ", action);
      }
      if (onScanFltParamSetup.has_status()) {
        status = onScanFltParamSetup.status();
        ALOGD("\n status: 0x%d ", status);
      }
      for (auto pair : ScanFiltrParamSetCbMap) {
        if (client_if == pair.first) {
          do_in_jni_thread(Bind(pair.second, available_space, action, status));
          ScanFiltrParamSetCbMap.erase(client_if);
          break;
        }
      }
      break;
    }
    case BT_LE_SCAN_SET_SCAN_PARAM_EVENT: {
      ALOGD("BT_LE_SCAN_SET_SCAN_PARAM_EVENT");
      ss_ble_set_scan_param_event onSetScanParam;
      uint32_t client_if = 0;
      uint32_t status = 0;
      onSetScanParam.ParseFromString(resBufferString);
      if (onSetScanParam.has_clientif()) {
        client_if = onSetScanParam.clientif();
        ALOGD("\n client_if: %d ", client_if);
      }
      if (onSetScanParam.has_status()) {
        status = onSetScanParam.status();
        ALOGD("\n status: 0x%d ", status);
      }
      for (auto pair : SetScanParamCbMap) {
        if (client_if == pair.first) {
          do_in_jni_thread(Bind(pair.second, status));
          SetScanParamCbMap.erase(client_if);
          break;
        }
      }
      break;
    }
    case BT_LE_SCAN_RESULT_EVENT: {
      ALOGD("BT_LE_SCAN_RESULT_EVENT");
      ss_ble_scan_result_event onScanResult;
      uint32_t client_if = 0;
      uint8_t device_type = BT_DEVICE_TYPE_BLE;
      uint16_t event_type = 0;
      uint8_t address_type = 0;
      RawAddress *bd_addr = nullptr;
      uint8_t primary_phy = 0;
      uint8_t secondary_phy = 0;
      uint8_t advertising_sid = 0;
      int8_t tx_power = 0;
      int8_t rssi = 0;
      uint16_t periodic_adv_int = 0;
      std::vector<uint8_t> adv_data;
      RawAddress *original_bda = nullptr;
      onScanResult.ParseFromString(resBufferString);
      if (onScanResult.has_clientif()) {
        client_if = onScanResult.clientif();
        ALOGD("\n client_if: %d ", client_if);
      }
      if (onScanResult.has_device_type()) {
        device_type = onScanResult.device_type();
        ALOGD("\n device_type: %d ", device_type);
      }
      if (onScanResult.has_event_type()) {
        event_type = onScanResult.event_type();
        ALOGD("\n event_type: %d ", event_type);
      }
      if (onScanResult.has_addr_type()) {
        address_type = onScanResult.addr_type();
        ALOGD("\n address_type: %d ", address_type);
      }
      if (onScanResult.has_bda()) {
        uint8_t* addr = (uint8_t*)onScanResult.bda().c_str();
        bd_addr = (RawAddress*)addr;
        ALOGD("\naddress: %s ", bd_addr->ToString().c_str());
      }
      if (onScanResult.has_primary_phy()) {
        primary_phy = onScanResult.primary_phy();
        ALOGD("\n primary_phy: %d ", primary_phy);
      }
      if (onScanResult.has_secondary_phy()) {
        secondary_phy = onScanResult.secondary_phy();
        ALOGD("\n secondary_phy: %d ", secondary_phy);
      }
      if (onScanResult.has_advertising_sl()) {
        advertising_sid = onScanResult.advertising_sl();
        ALOGD("\n advertising_sid: %d ", advertising_sid);
      }
      if (onScanResult.has_tx_power()) {
        tx_power = onScanResult.tx_power();
        ALOGD("\n tx_power: %d ", tx_power);
      }
      if (onScanResult.has_rssi()) {
        rssi = onScanResult.rssi();
        ALOGD("\n rssi: %d ", rssi);
      }
      if (onScanResult.has_periodic()) {
        periodic_adv_int = onScanResult.periodic();
        ALOGD("\n periodic_adv_int: %d ", periodic_adv_int);
      }
      if (onScanResult.has_adv_data()) {
        std::string s = onScanResult.adv_data();
        std::vector<uint8_t> adv_data1(s.begin(), s.end());
        for (uint16_t i = 0; i < adv_data1.size(); i++) {
          ALOGD(" adv_data1[%d]:%d ", i, adv_data1[i]);
        }
        adv_data = std::move(adv_data1);
      }
      if (onScanResult.has_original_bda()) {
        uint8_t* addr = (uint8_t*)onScanResult.original_bda().c_str();
        original_bda = (RawAddress*)addr;
        ALOGD("\n original_bda : %s ", original_bda->ToString().c_str());
      }
      if (original_bda == nullptr) {
        original_bda = bd_addr;
      }
      do_in_jni_thread(Bind(
          bta_scan_results_cb_impl, *(RawAddress *)bd_addr, device_type, rssi, address_type,
          event_type, primary_phy, secondary_phy, advertising_sid, tx_power,
          periodic_adv_int, std::move(adv_data), *(RawAddress *)original_bda));
      break;
    }
    case BT_LE_SCAN_BATCH_SCAN_CONFIG_EVENT: {
      ALOGD("BT_LE_SCAN_BATCH_SCAN_CONFIG_EVENT");
      ss_batch_config_storage_event onBatchConfig;
      uint32_t client_if = 0;
      uint32_t status = 0;
      onBatchConfig.ParseFromString(resBufferString);
      if (onBatchConfig.has_clientif()) {
        client_if = onBatchConfig.clientif();
        ALOGD("\n client_if: %d ", client_if);
      }
      if (onBatchConfig.has_status()) {
        status = onBatchConfig.status();
        ALOGD("\n status: 0x%d ", status);
      }
      for (auto pair : BatchScanCfgStorageCbMap) {
        if (client_if == pair.first) {
          do_in_jni_thread(Bind(pair.second, status));
          BatchScanCfgStorageCbMap.erase(client_if);
          break;
        }
      }
      break;
    }
    case BT_LE_SCAN_BATCH_SCAN_ENABLE_EVENT: {
      ALOGD("BT_LE_SCAN_BATCH_SCAN_ENABLE_EVENT");
      ss_batch_scan_enable_event onBatchScanEn;
      uint32_t client_if = 0;
      uint32_t status = 0;
      onBatchScanEn.ParseFromString(resBufferString);
      if (onBatchScanEn.has_clientif()) {
        client_if = onBatchScanEn.clientif();
        ALOGD("\n client_if: %d ", client_if);
      }
      if (onBatchScanEn.has_status()) {
        status = onBatchScanEn.status();
        ALOGD("\n status: 0x%d ", status);
      }
      for (auto pair : BatchScanEnCbMap) {
        if (client_if == pair.first) {
          do_in_jni_thread(Bind(pair.second, status));
          BatchScanEnCbMap.erase(client_if);
          break;
        }
      }
      break;
    }
    case BT_LE_SCAN_BATCH_SCAN_DISABLE_EVENT: {
      ALOGD("BT_LE_SCAN_BATCH_SCAN_DISABLE_EVENT");
      ss_batch_scan_disable_event onBatchScanDisable;
      uint32_t client_if = 0;
      uint32_t status = 0;
      onBatchScanDisable.ParseFromString(resBufferString);
      if (onBatchScanDisable.has_clientif()) {
        client_if = onBatchScanDisable.clientif();
        ALOGD("\n client_if: %d ", client_if);
      }
      if (onBatchScanDisable.has_status()) {
        status = onBatchScanDisable.status();
        ALOGD("\n status: 0x%d ", status);
      }
      for (auto pair : BatchScanDisCbMap) {
        if (client_if == pair.first) {
          do_in_jni_thread(Bind(pair.second, status));
          BatchScanDisCbMap.erase(client_if);
          break;
        }
      }
      break;
    }
    case BT_LE_SCAN_BATCH_SCAN_READ_REPORT_EVENT: {
      ALOGD("BT_LE_SCAN_BATCH_SCAN_READ_REPORT_EVENT");
      ss_ble_batch_scan_report_event onBatchScanReport;
      uint32_t client_if = 0;
      uint32_t status = 0;
      uint32_t report_format = 0;
      uint32_t num_records = 0;
      std::vector<uint8_t> batch_scan_data;
      onBatchScanReport.ParseFromString(resBufferString);
      if (onBatchScanReport.has_clientif()) {
        client_if = onBatchScanReport.clientif();
        ALOGD("\n client_if: %d ", client_if);
      }
      if (onBatchScanReport.has_status()) {
        status = onBatchScanReport.status();
        ALOGD("\n status: 0x%d ", status);
      }
      if (onBatchScanReport.has_report_format()) {
        report_format = onBatchScanReport.report_format();
        ALOGD("\n report_format: 0x%d ", report_format);
      }
      if (onBatchScanReport.has_num_records()) {
        num_records = onBatchScanReport.num_records();
        ALOGD("\n num_records: 0x%d ", num_records);
      }
      if (onBatchScanReport.has_data()) {
        std::string s = onBatchScanReport.data();
        std::vector<uint8_t> batch_scan_data1(s.begin(), s.end());
        for (uint16_t i = 0; i < batch_scan_data1.size(); i++) {
          ALOGD(" batch_scan_data1[%d]:%d ", i, batch_scan_data1[i]);
        }
        batch_scan_data = std::move(batch_scan_data1);
      }
      bta_batch_scan_reports_cb(client_if, status, report_format, num_records,
                                batch_scan_data);
      break;
    }
    case BT_LE_SCAN_BATCH_TRACK_ADV_EVENT: {
      ALOGD("BT_LE_SCAN_BATCH_TRACK_ADV_EVENT");
      ss_ble_track_adv_event onTrackAdv;
      btgatt_track_adv_info_t* p_track_adv_data = new btgatt_track_adv_info_t;
      std::vector<uint8_t> adv_pkt_data;
      std::vector<uint8_t> scan_rsp_data;

      if (onTrackAdv.has_track_adv_info()) {
        ss_btgatt_track_adv_info track_adv_info = onTrackAdv.track_adv_info();
        p_track_adv_data->client_if = track_adv_info.client_if();
        ALOGD("\n client_if: %d ", p_track_adv_data->client_if);
        p_track_adv_data->filt_index = track_adv_info.filt_index();
        ALOGD("\n filt_index: %d ", p_track_adv_data->filt_index);
        p_track_adv_data->advertiser_state = track_adv_info.advertiser_state();
        ALOGD("\n advertiser_state: %d ", p_track_adv_data->advertiser_state);
        p_track_adv_data->advertiser_info_present =
            track_adv_info.advertiser_info_present();
        ALOGD("\n advertiser_info_present: %d ",
              p_track_adv_data->advertiser_info_present);
        p_track_adv_data->addr_type = track_adv_info.addr_type();
        ALOGD("\n addr_type: %d ", p_track_adv_data->addr_type);
        p_track_adv_data->tx_power = track_adv_info.tx_power();
        ALOGD("\n tx_power: %d ", p_track_adv_data->tx_power);
        p_track_adv_data->rssi_value = track_adv_info.rssi_value();
        ALOGD("\n rssi_value: %d ", p_track_adv_data->rssi_value);
        p_track_adv_data->time_stamp = track_adv_info.time_stamp();
        ALOGD("\n time_stamp: %d ", p_track_adv_data->time_stamp);

        RawAddress::FromString(track_adv_info.bd_addr(),
                               p_track_adv_data->bd_addr);
        ALOGD("\n address is :: %s",
              p_track_adv_data->bd_addr.ToString().c_str());

        p_track_adv_data->adv_pkt_len = track_adv_info.adv_pkt_len();
        ALOGD("\n adv_pkt_len: %d ", p_track_adv_data->adv_pkt_len);
        std::string s = track_adv_info.p_adv_pkt_data();
        std::vector<uint8_t> adv_pkt_data(s.begin(), s.end());
        for (uint16_t i = 0; i < adv_pkt_data.size(); i++)
          ALOGD(" adv_pkt_data[%d]:%d ", i, adv_pkt_data[i]);
        if (p_track_adv_data->adv_pkt_len > 0) {
          p_track_adv_data->p_adv_pkt_data = &adv_pkt_data[0];
        }

        p_track_adv_data->scan_rsp_len = track_adv_info.scan_rsp_len();
        ALOGD("\n scan_rsp_len: %d ", p_track_adv_data->scan_rsp_len);
        s = track_adv_info.p_scan_rsp_data();
        std::vector<uint8_t> scan_rsp_data(s.begin(), s.end());
        for (uint16_t i = 0; i < scan_rsp_data.size(); i++)
          ALOGD(" scan_rsp_data[%d]:%d ", i, scan_rsp_data[i]);

        if (p_track_adv_data->scan_rsp_len > 0) {
          p_track_adv_data->p_scan_rsp_data = &scan_rsp_data[0];
        }
      }
      bta_track_adv_event_cb(p_track_adv_data);
      break;
    }
    case BT_LE_SCAN_BATCH_THRESHOLD_EVENT: {
      ALOGD("BT_LE_SCAN_BATCH_THRESHOLD_EVENT");
      ss_ble_batch_scan_threshold_event onBatchScanThres;
      uint32_t client_if = 0;
      uint32_t status = 0;
      onBatchScanThres.ParseFromString(resBufferString);
      if (onBatchScanThres.has_clientif()) {
        client_if = onBatchScanThres.clientif();
        ALOGD("\n client_if: %d ", client_if);
      }
      bta_batch_scan_threshold_cb((tBTM_BLE_REF_VALUE)client_if);
      break;
    }
    default: {
      ALOGD("btif_scanner_ss_callback :: unknown msg id");
      break;
    }
  }
}
