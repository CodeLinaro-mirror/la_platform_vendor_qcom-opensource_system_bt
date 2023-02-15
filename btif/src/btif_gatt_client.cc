/******************************************************************************
 *
 *  Copyright (C) 2009-2014 Broadcom Corporation
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
 *
 *  Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted (subject to the limitations in the
 *  disclaimer below) provided that the following conditions are met:
 *
 *  Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *
 *  Redistributions in binary form must reproduce the above
 *  copyright notice, this list of conditions and the following
 *  disclaimer in the documentation and/or other materials provided
 *  with the distribution.
 *
 *  Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *  contributors may be used to endorse or promote products derived
 *  from this software without specific prior written permission.
 *
 *  NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 *  GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 *  HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 *  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 *  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 *  IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
 *
 ******************************************************************************/

/*******************************************************************************
 *
 *  Filename:      btif_gatt_client.c
 *
 *  Description:   GATT client implementation
 *
 ******************************************************************************/

#define LOG_TAG "bt_btif_gattc"

#include <base/bind.h>
#include <base/callback.h>
#include <errno.h>
#include <hardware/bluetooth.h>
#include <stdlib.h>
#include <string.h>

#include "btif_common.h"
#include "btif_util.h"

#include <hardware/bt_gatt.h>

#include "btif_config.h"
#include "btif_dm.h"
#include "btif_gatt.h"
#include "btif_gatt_util.h"
#include "btif_storage.h"
#include "btif_ss_gatt_client.h"

using bluetooth::Uuid;
using std::vector;

/*******************************************************************************
 *  Constants & Macros
 ******************************************************************************/

gatt_client_single_stack_proto gattClientSingleStackProto = gatt_client_single_stack_proto();

namespace {

/*******************************************************************************
 *  Client API Functions
 ******************************************************************************/
#if (EATT_IF_SUPPORTED == TRUE)
  bt_status_t btif_gattc_register_app(const Uuid& uuid, bool eatt_support) {
    bt_status_t status;
    ALOGD("%s ", __func__);
    status = gattClientSingleStackProto.registerClient(uuid, eatt_support);
    return status;
  }
#else
 bt_status_t btif_gattc_register_app(const Uuid& uuid) {
    bt_status_t status;
    bool eatt_support = false;
    ALOGD("%s ", __func__);
    status = gattClientSingleStackProto.registerClient(uuid, eatt_support);
    return status;
  }
#endif

bt_status_t btif_gattc_unregister_app(int client_if) {
  bt_status_t status;
  ALOGD("%s ", __func__);
  status = gattClientSingleStackProto.deregisterClient(client_if);
  return status;
}

bt_status_t btif_gattc_open(int client_if, const RawAddress& bd_addr, tBLE_ADDR_TYPE addr_type,
                            bool is_direct, int transport, bool opportunistic,
                            int initiating_phys) {
  bt_status_t status;
  ALOGD("%s ", __func__);
  status = gattClientSingleStackProto.connect(client_if, bd_addr, is_direct, transport, opportunistic, initiating_phys);
  return status;
}

bt_status_t btif_gattc_close(int client_if, const RawAddress& bd_addr,
                             int conn_id) {
  bt_status_t status;
  ALOGD("%s ", __func__);
  status = gattClientSingleStackProto.disconnect(client_if, bd_addr, conn_id);
  return status;
}

bt_status_t btif_gattc_refresh(int client_if, const RawAddress& bd_addr) {
  bt_status_t status;
  ALOGD("%s ", __func__);
  status = gattClientSingleStackProto.refreshServices(client_if, bd_addr);
  return status;
}

bt_status_t btif_gattc_search_service(int conn_id, const Uuid* filter_uuid) {
 Uuid* uuid = NULL;
  bt_status_t status;
  ALOGD("%s ", __func__);
   status = gattClientSingleStackProto.searchServices(conn_id, *uuid);
   return status;
}

void btif_gattc_discover_service_by_uuid(int conn_id, const Uuid& uuid) {
   ALOGD("%s ", __func__);
   gattClientSingleStackProto.discoverServicesByUuid(conn_id, uuid);
}

bt_status_t btif_gattc_get_gatt_db(int conn_id) {
  bt_status_t status;
  ALOGD("%s ", __func__);
  status = gattClientSingleStackProto.getGattDb(conn_id);
  return status;
}

bt_status_t btif_gattc_read_char(int conn_id, uint16_t handle, int auth_req) {
  bt_status_t status;
  ALOGD("%s ", __func__);
  status = gattClientSingleStackProto.readCharacteristicValue(conn_id, (int)handle, auth_req);
  return status;
}

bt_status_t btif_gattc_read_using_char_uuid(int conn_id, const Uuid& uuid,
                                            uint16_t s_handle,
                                            uint16_t e_handle, int auth_req) {
  ALOGD("%s ", __func__);
  bt_status_t status;
  status = gattClientSingleStackProto.readCharacteristicValueUsingUuid(conn_id, uuid, s_handle,
                            e_handle, auth_req);
  return status;
}

bt_status_t btif_gattc_read_char_descr(int conn_id, uint16_t handle,
                                       int auth_req) {
  ALOGD("%s ", __func__);
  bt_status_t status;
  status = gattClientSingleStackProto.readDescriptorValue(conn_id, (int)handle, auth_req);
  return status;
}

static bt_status_t btif_gattc_write_char(int conn_id, uint16_t handle,
                                         int write_type, int auth_req,
                                         const uint8_t* val, size_t len) {
  ALOGD("%s ", __func__);
  bt_status_t status;
  std::vector<uint8_t> value(val, val + len);
  status = gattClientSingleStackProto.writeCharacteristicValue(conn_id, (int)handle, write_type, auth_req, value);
  return status;
}

static bt_status_t btif_gattc_write_char_descr(int conn_id, uint16_t handle,
                                                int auth_req, const uint8_t* val,
                                                size_t len) {
    ALOGD("%s ", __func__);
    bt_status_t status;
    std::vector<uint8_t> value(val, val + len);
    status = gattClientSingleStackProto.writeDescriptorValue(conn_id, (int)handle, auth_req, value);
    return status;
}

bt_status_t btif_gattc_execute_write(int conn_id, int execute) {
  ALOGD("%s ", __func__);
  bt_status_t status;
  status = gattClientSingleStackProto.executeWrite(conn_id, execute);
  return status;
}

bt_status_t btif_gattc_reg_for_notification(int client_if,
                                            const RawAddress& bd_addr,
                                            uint16_t handle) {
    ALOGD("%s ", __func__);
    bt_status_t status;
    status = gattClientSingleStackProto.registerNotifications(client_if, bd_addr, handle);
    return status;
}

bt_status_t btif_gattc_dereg_for_notification(int client_if,
                                              const RawAddress& bd_addr,
                                              uint16_t handle) {
    ALOGD("%s ", __func__);
    bt_status_t status;
    status = gattClientSingleStackProto.deregisterNotifications(client_if, bd_addr, handle);
    return status;

}

bt_status_t btif_gattc_read_remote_rssi(int client_if,
                                        const RawAddress& bd_addr) {
    ALOGD("%s ", __func__);
    bt_status_t status;
    status = gattClientSingleStackProto.readRemoteRssi(client_if, bd_addr);
    return status;

}

bt_status_t btif_gattc_configure_mtu(int conn_id, int mtu) {
  ALOGD("%s ", __func__);
  bt_status_t status;
  status = gattClientSingleStackProto.configureMtu(conn_id, mtu);
  return status;
}

bt_status_t btif_gattc_conn_parameter_update(const RawAddress& bd_addr,
                                             int min_interval, int max_interval,
                                             int latency, int timeout,
                                             uint16_t min_ce_len,
                                             uint16_t max_ce_len) {
  ALOGD("%s ", __func__);
  bt_status_t status;
  status = gattClientSingleStackProto.connParamUpdate(bd_addr, min_interval, max_interval,
                                latency, timeout, min_ce_len, max_ce_len);
  return status;
}

bt_status_t btif_gattc_set_preferred_phy(const RawAddress& bd_addr,
                                         uint8_t tx_phy, uint8_t rx_phy,
                                         uint16_t phy_options) {
  ALOGD("%s ", __func__);
  bt_status_t status;
  status = gattClientSingleStackProto.setPhy(bd_addr, tx_phy, rx_phy, phy_options);
  return status;
}
bt_status_t btif_gattc_read_phy(const RawAddress& bd_addr,
     base::Callback<void(uint8_t tx_phy, uint8_t rx_phy, uint8_t status)> cb) {
  ALOGD("%s ", __func__);
  bt_status_t status;
  status = gattClientSingleStackProto.readPhy(bd_addr, cb);
  return status;
}

int btif_gattc_get_device_type(const RawAddress& bd_addr) {
  int device_type = 0;

  /* ToDo: get device type from cache,
    currently not storing the device type as part of properties*/
    return device_type;
}

}  // namespace
const btgatt_client_interface_t btgattClientInterface = {
    btif_gattc_register_app,
    btif_gattc_unregister_app,
    btif_gattc_open,
    btif_gattc_close,
    btif_gattc_refresh,
    btif_gattc_search_service,
    btif_gattc_discover_service_by_uuid,
    btif_gattc_read_char,
    btif_gattc_read_using_char_uuid,
    btif_gattc_write_char,
    btif_gattc_read_char_descr,
    btif_gattc_write_char_descr,
    btif_gattc_execute_write,
    btif_gattc_reg_for_notification,
    btif_gattc_dereg_for_notification,
    btif_gattc_read_remote_rssi,
    btif_gattc_get_device_type,
    btif_gattc_configure_mtu,
    btif_gattc_conn_parameter_update,
    btif_gattc_set_preferred_phy,
    btif_gattc_read_phy,
    nullptr,
    btif_gattc_get_gatt_db,
    nullptr};
