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
 *  Filename:      btif_gatt_server.c
 *
 *  Description:   GATT server implementation
 *
 ******************************************************************************/

#define LOG_TAG "bt_btif_gatt"

#include <base/bind.h>
#include <base/callback.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hardware/bluetooth.h>
#include <hardware/bt_gatt.h>

#include "btif_common.h"
#include "btif_util.h"

#include "internal_include/bt_common.h"
//#include "internal_include/extra_include.h"
#include "bta_api.h"
#include "bta_closure_api.h"
#include "bta_gatt_api.h"
#include "btif_config.h"
#include "btif_dm.h"
#include "btif_gatt.h"
#include "btif_gatt_util.h"
#include "btif_storage.h"
#include "osi/include/log.h"
#include "btif_ss_gatt_server.h"
using base::Bind;
using base::Owned;
using bluetooth::Uuid;
using std::vector;

/*******************************************************************************
 *  Constants & Macros
 ******************************************************************************/


/*******************************************************************************
 *  Static variables
 ******************************************************************************/

//extern const btgatt_callbacks_t* bt_gatt_callbacks;

btif_ss_gatt_server gatt_server_single_stack_proto;

/*******************************************************************************
 *  Static functions
 ******************************************************************************/

/*******************************************************************************
 *  Server API Functions
 ******************************************************************************/
#if (EATT_IF_SUPPORTED == TRUE)
  static bt_status_t btif_gatts_register_app(const Uuid& bt_uuid, bool eatt_support) {
    return gatt_server_single_stack_proto.registerServer(bt_uuid, eatt_support);
  }
#else
  static bt_status_t btif_gatts_register_app(const Uuid& bt_uuid) {

    bool eatt_support = false;
    return gatt_server_single_stack_proto.registerServer(bt_uuid, eatt_support);
  }
#endif

static bt_status_t btif_gatts_unregister_app(int server_if) {
  return gatt_server_single_stack_proto.unregisterServer(server_if);
}

static bt_status_t btif_gatts_open(int server_if, const RawAddress& bd_addr,
                                   bool is_direct, int transport) {
  return gatt_server_single_stack_proto.connect(server_if, bd_addr, is_direct, transport);
}

static bt_status_t btif_gatts_close(int server_if, const RawAddress& bd_addr,
                                    int conn_id) {
  return gatt_server_single_stack_proto.disconnect(server_if, bd_addr, conn_id);

}
static bt_status_t btif_gatts_add_service(int server_if,
                                          const btgatt_db_element_t* service,
                                          size_t service_count) {
  return gatt_server_single_stack_proto. AddService(server_if, std::vector(service, service + service_count));
}

static bt_status_t btif_gatts_stop_service(int server_if, int service_handle) {
  return gatt_server_single_stack_proto.stopService(server_if, service_handle);
}

static bt_status_t btif_gatts_delete_service(int server_if,
                                             int service_handle) {
  return gatt_server_single_stack_proto.clearService(server_if, service_handle);
}

static bt_status_t btif_gatts_send_indication(int server_if,
                                              int attribute_handle, int conn_id,
                                              int confirm, const uint8_t* value,
                                              size_t length) {

  if (length > BTGATT_MAX_ATTR_LEN) length = BTGATT_MAX_ATTR_LEN;
  return gatt_server_single_stack_proto.sendIndicationNotification(attribute_handle,
                         conn_id,confirm,std::vector(value, value + length), server_if);
}

static bt_status_t btif_gatts_send_response(int conn_id, int trans_id,
                                            int status,
                                            const btgatt_response_t& response) {
  return gatt_server_single_stack_proto.sendResponse(conn_id, trans_id, status, response);
}

static bt_status_t btif_gatts_set_preferred_phy(const RawAddress& bd_addr,
                                                uint8_t tx_phy, uint8_t rx_phy,
                                                uint16_t phy_options) {
  return gatt_server_single_stack_proto.setPhy(bd_addr, tx_phy, rx_phy, phy_options);
}

static bt_status_t btif_gatts_read_phy(
     const RawAddress& bd_addr,
     base::Callback<void(uint8_t tx_phy, uint8_t rx_phy, uint8_t status)> cb) {
    return gatt_server_single_stack_proto.readPhy(bd_addr, cb);
}
const btgatt_server_interface_t btgattServerInterface = {
    btif_gatts_register_app,   btif_gatts_unregister_app,
    btif_gatts_open,           btif_gatts_close,
    btif_gatts_add_service,    btif_gatts_stop_service,
    btif_gatts_delete_service, btif_gatts_send_indication,
    btif_gatts_send_response,  btif_gatts_set_preferred_phy,
    btif_gatts_read_phy};
