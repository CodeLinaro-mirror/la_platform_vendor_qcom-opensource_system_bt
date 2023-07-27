/******************************************************************************
 *
 *  Copyright (C) 2009-2013 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  Changes from Qualcomm Innovation Center are provided under the following license:
 *  Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear.
 *
 ******************************************************************************/

/*****************************************************************************
 *
 *  Name:          btif_gatt.h
 *
 *  Description:
 *
 *****************************************************************************/

#ifndef BTIF_GATT_H
#define BTIF_GATT_H

#include "include/hardware/bt_gatt.h"

#define BT_PROFILE_ID_ADV "bleadv"
#define BT_PROFILE_ID_SCAN "blescan"
#define BT_PROFILE_ID_GATTC "gattc"
#define BT_PROFILE_ID_GATTS "gatts"
#define BTGATT_MAX_ATTR_LEN 600

extern const btgatt_client_interface_t btgattClientInterface;
extern const btgatt_server_interface_t btgattServerInterface;

BleAdvertiserInterface* get_ble_advertiser_instance();
BleScannerInterface* get_ble_scanner_instance();
void btif_ss_gatt_client_init();
void btif_ss_gatt_client_deinit();
void btif_ss_gatt_server_init();
void btif_ss_gatt_server_deinit();

void btif_ss_gattc_cback(uint8_t event, char* p_param);
#endif
