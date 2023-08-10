/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef BTIF_SS_SCANNER_H
#define BTIF_SS_SCANNER_H

#include "hardware/ble_scanner.h"

#include <string>
#include <vector>

class ScannerSingleStackProto {
 public:
  bool RegisterScanner(const bluetooth::Uuid& scan_uuid,
                       BleScannerInterface::RegisterCallback cb);
  bool UnRegisterScanner(int scanner_id);
  bool StartScanning(bool start);
  bool ScanFilterClear(int client_if, int filter_index,
                       BleScannerInterface::FilterConfigCallback Cb);
  bool ScanFilterEnable(int client_if, bool enable,
                        BleScannerInterface::EnableCallback Cb);
  bool ScanFilterAdd(int client_if, int filter_index,
                     std::vector<ApcfCommand> filters,
                     BleScannerInterface::FilterConfigCallback Cb);
  bool ScanFilterParamSetup(
      uint8_t client_if, uint8_t action, uint8_t filt_index,
      std::unique_ptr<btgatt_filt_param_setup_t> filt_param,
      BleScannerInterface::FilterParamSetupCallback Cb);
  bool SetScanParameters(int client_if, int scan_phy,
                         std::vector<uint32_t> scan_interval,
                         std::vector<uint32_t> scan_window,
                         BleScannerInterface::Callback Cb);
  bool BatchscanConfigStorage(int client_if, int batch_scan_full_max,
                              int batch_scan_trunc_max,
                              int batch_scan_notify_threshold,
                              BleScannerInterface::Callback Cb);
  bool BatchscanEnable(int client_if, int scan_mode, int scan_interval,
                       int scan_window, int addr_type, int discard_rule,
                       BleScannerInterface::Callback Cb);
  bool BatchscanDisable(int client_if, BleScannerInterface::Callback Cb);
  bool BatchscanReadReports(int client_if, int scan_mode);
  int postTxMessage(std::string msgStr);
};
void btif_scan_ss_init();
void btif_scanner_ss_callback(uint16_t event, char* p_param);
#endif /* BTIF_SS_SCANNER_H*/
