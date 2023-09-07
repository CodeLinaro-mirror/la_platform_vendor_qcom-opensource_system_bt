/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef BTIF_SS_ADVERTISER_H
#define BTIF_SS_ADVERTISER_H

#include <hardware/ble_advertiser.h>
#include <string>
#include <vector>
#include "ble_advertiser.h"

class AdvertiserSingleStackProto {
 public:
  bool BleStartAdvertingSet(
      BleAdvertiserInterface::IdTxPowerStatusCallback Cb,
      const AdvertiseParameters& adv_param,
      const std::vector<uint8_t>& advertise_data,
      const std::vector<uint8_t>& advertise_data_enc,
      const std::vector<uint8_t>& scan_response_data,
      const std::vector<uint8_t>& scan_response_data_enc,
      const PeriodicAdvertisingParameters& periodic_params,
      const std::vector<uint8_t>& periodic_data,
      const std::vector<uint8_t>& periodic_data_enc, int duration,
      int max_ext_adv_events, const std::vector<uint8_t>& enc_key_value,
      int reg_id, BleAdvertiserInterface::IdStatusCallback TimeoutCb);
  bool BleStopAdvertisingSet(int advertiser_id);
  int postTxMessage(std::string msgStr);
  bool BleSetAdvertisingParameters(
      BleAdvertiserInterface::ParametersCallback Cb, int advertiser_id,
      const AdvertiseParameters& adv_param);
  bool BleGetOwnAddress(BleAdvertiserInterface::GetAddressCallback Cb,
                        int advertiser_id);
  bool BleEnableAdvertisingSet(
      BleAdvertiserInterface::StatusCallback Cb, int advertiser_id, bool enable,
      int duration, int max_ext_adv_events,
      BleAdvertiserInterface::StatusCallback TimeoutCb);
  bool BleSetData(BleAdvertiserInterface::StatusCallback Cb, int advertiser_id,
                  bool scan_resp_data, const std::vector<uint8_t>& data, const std::vector<uint8_t>& data_enc);
  bool BleSetPeriodicAdvertisingParameters(
      BleAdvertiserInterface::StatusCallback Cb, int advertiser_id,
      const PeriodicAdvertisingParameters& periodic_params);
  bool BlesetPeriodicAdvertisingData(BleAdvertiserInterface::StatusCallback Cb,
                                     int advertiser_id,
                                     const std::vector<uint8_t>& data, const std::vector<uint8_t>& data_enc);
  bool BleSetPeriodicAdvertisingEnable(
      BleAdvertiserInterface::StatusCallback Cb, int advertiser_id,
      bool enable/* , bool include_adi */);
};
void btif_adv_ss_init();
void btif_advertiser_ss_callback(uint16_t event, char* p_param);
#endif /* BTIF_SS_ADVERTISER_H*/
