/******************************************************************************
 *
 * Copyright (C) 2014 Samsung System LSI
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
 * Changes from Qualcomm Innovation Center are provided under the following license:
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 ******************************************************************************/

/*******************************************************************************
 *
 *  Filename:      btif_sdp.c
 *  Description:   SDP Bluetooth Interface.
 *                 Implements the generic message handling and search
 *                 functionality.
 *                 References btif_sdp_server.c for SDP record creation.
 *
 ******************************************************************************/

#define LOG_TAG "bt_btif_sdp"

#include <stdlib.h>
#include <string.h>

#include <hardware/bluetooth.h>
#include <hardware/bt_sdp.h>

//#include "bta_api.h"
//#include "bta_sdp_api.h"
#include "btif_common.h"
#include "btif_profile_queue.h"
#include "btif_util.h"

#include "btif_ss_interface.h"
#ifdef SS_STUB_ENABLED
#include "btif_ss_stub_interface.h"
#endif

#include "protobuf/proto/sdp.pb.h"
#include "btif/protobuf/include/proto_message_ids.h"

using bluetooth::Uuid;
using bluetooth::synergy::SynergyProto::ss_bt_sdp_search;
using bluetooth::synergy::SynergyProto::ss_sdp_search_complete_callback;

/*****************************************************************************
 *  Functions implemented in sdp_server.c
 *****************************************************************************/
bt_status_t sdp_server_init();
void sdp_server_cleanup();
bt_status_t create_sdp_record(bluetooth_sdp_record* records,
                              int* record_handles);
bt_status_t remove_sdp_record(int record_handle);
void on_create_record_event(int handle);
void on_remove_record_event(int handle);

// Utility functions:
int get_sdp_records_size(bluetooth_sdp_record* in_record, int count);
void copy_sdp_records(bluetooth_sdp_record* in_records,
                      bluetooth_sdp_record* out_records, int count);

/*****************************************************************************
 *  Static variables
 *****************************************************************************/

static btsdp_callbacks_t* bt_sdp_callbacks = NULL;
BluetoothSSInterface *btSSInterface_t;

#if 0
static void btif_sdp_search_comp_evt(uint16_t event, char* p_param) {
  tBTA_SDP_SEARCH_COMP* evt_data = (tBTA_SDP_SEARCH_COMP*)p_param;
  BTIF_TRACE_DEBUG("%s:  event = %d", __func__, event);

  if (event != BTA_SDP_SEARCH_COMP_EVT) return;

  HAL_CBACK(bt_sdp_callbacks, sdp_search_cb, (bt_status_t)evt_data->status,
            evt_data->remote_addr, evt_data->uuid, evt_data->record_count,
            evt_data->records);
}

static void sdp_search_comp_copy_cb(uint16_t event, char* p_dest, char* p_src) {
  tBTA_SDP* p_src_sdp_data = (tBTA_SDP*)p_src;
  tBTA_SDP_SEARCH_COMP* p_dest_data = (tBTA_SDP_SEARCH_COMP*)p_dest;
  tBTA_SDP_SEARCH_COMP* p_src_data = (tBTA_SDP_SEARCH_COMP*)&(p_src_sdp_data->sdp_search_comp);

  if (!p_src) return;

  if (event != BTA_SDP_SEARCH_COMP_EVT) return;

  maybe_non_aligned_memcpy(p_dest_data, p_src_data, sizeof(*p_src_data));

  copy_sdp_records(p_src_data->records, p_dest_data->records,
                   p_src_data->record_count);
}

static void sdp_dm_cback(tBTA_SDP_EVT event, tBTA_SDP* p_data,
                         void* user_data) {
  switch (event) {
    case BTA_SDP_SEARCH_COMP_EVT: {
      int size = sizeof(tBTA_SDP);
      size += get_sdp_records_size(p_data->sdp_search_comp.records,
                                   p_data->sdp_search_comp.record_count);

      /* need to deep copy the record content */
      btif_transfer_context(btif_sdp_search_comp_evt, event, (char*)p_data,
                            size, sdp_search_comp_copy_cb);
      break;
    }
    case BTA_SDP_CREATE_RECORD_USER_EVT: {
      on_create_record_event(PTR_TO_INT(user_data));
      break;
    }
    case BTA_SDP_REMOVE_RECORD_USER_EVT: {
      on_remove_record_event(PTR_TO_INT(user_data));
      break;
    }
    default:
      break;
  }
}
#endif

static bt_status_t init(btsdp_callbacks_t* callbacks) {
  ALOGI("Sdp init");
  bt_sdp_callbacks = callbacks;
  btSSInterface_t = BluetoothSSInterface::getInstance();
  if(btSSInterface_t == NULL) {
	  ALOGE("%s single stack interface Initialization failed",__func__);
  } else {
	  ALOGI("%s registering sdp profile callback",__func__);
      btSSInterface_t->registerCallbacks(BT_PROFILE_SDP_CLIENT_ID, btif_sdp_ss_callback);
  }

  return BT_STATUS_SUCCESS;
}

static bt_status_t deinit() {
  ALOGI("Sdp deinit");
  bt_sdp_callbacks = NULL;
  btSSInterface_t->deregisterCallbacks(BT_PROFILE_SDP_CLIENT_ID);

  return BT_STATUS_SUCCESS;
}

static bt_status_t search(RawAddress* bd_addr, const Uuid& uuid) {
  ALOGI("Sdp Search with bd_addr : %s and uuid : %s",
		  bd_addr->ToString().c_str(), uuid.ToString().c_str());

#ifdef SS_STUB_ENABLED
  BluetoothSSStubInterface *btSSStubInterface;
  btSSStubInterface = BluetoothSSStubInterface::getInstance();
#endif

  uint8_t sdp_search_msg[MAX_LENGTH_WITH_PROTO_NONE];
  uint16_t msg_id = BT_SDP_SEARCH;
  sdp_search_msg[0] = msg_id & 0xff;
  sdp_search_msg[1] = (msg_id >> 8);

  std::string protoMsg;
  ss_bt_sdp_search sdp_search;
  sdp_search.set_remote_addr(ToRawString(bd_addr).c_str());
  sdp_search.set_uuid(uuid.ToString().c_str());
  sdp_search.SerializeToString(&protoMsg);
  ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());

  uint16_t length = protoMsg.length();
  sdp_search_msg[2] = length & 0xff;
  sdp_search_msg[3] = (length >> 8);
  //adding proto_encode
  uint16_t proto_encode = PROTO_ENC_DEC;
  sdp_search_msg[4] = proto_encode & 0xff;
  sdp_search_msg[5] = (proto_encode >> 8);
  char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
  memcpy(resBuffer, (char *) sdp_search_msg, MAX_LENGTH_WITH_PROTO_NONE);
  std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
  msgStr.append(protoMsg);
#ifndef SS_STUB_ENABLED
  btSSInterface_t->postTxMsg(msgStr);
#else
  btSSStubInterface->postTxMsg(msgStr);
#endif

  return BT_STATUS_SUCCESS;
}

static const btsdp_interface_t sdp_if = {
    sizeof(btsdp_interface_t), init, deinit, search, create_sdp_record,
    remove_sdp_record};

const btsdp_interface_t* btif_sdp_get_interface(void) {
  ALOGI("%s", __func__);
  return &sdp_if;
}

void btif_sdp_ss_callback(uint16_t event, char* p_param) {
   ALOGI("%s", __func__);
   bt_status_t status = BT_STATUS_FAIL;
   std::string resBufferString;
   RawAddress bd_addr;
   Uuid uuid_sdp;
   int record_count = 1;
   int record_length = 0;
   uint8_t* rec_data = NULL;
   bluetooth_sdp_record records[1];
   std::string name = "BluetoothRfcommSdpRecord";
   const char* srv_name = name.c_str();

   tBTIF_SS_Cback* cb_data = (tBTIF_SS_Cback*)p_param;
   uint16_t MSG_ID = cb_data->payload[0] + (((int)(cb_data->payload[1]))<<8);
   uint16_t length = cb_data->payload[2] + (((int)(cb_data->payload[3]))<<8);
   uint16_t proto_ec = 0;
   if (length > 0) {
      proto_ec = cb_data->payload[4] + (((int)(cb_data->payload[5]))<<8);
      char resBuffer[length];
      int j = 0;
      for(int i=MSG_PROTO_OFFSET; i< (length + MSG_PROTO_OFFSET); i++){
          resBuffer[j] = (char)cb_data->payload[i];
           j++;
      }
      resBufferString.assign(resBuffer, length);
      free (cb_data->payload);
   }
   ALOGI("MSG_ID is :: %X , Proto length: %d and Proto Encoded Value %d",MSG_ID, length, proto_ec);

   if(MSG_ID == BT_SDP_SEARCH_COMPLETE_CB) {
      ALOGI("BT_SDP_SEARCH_COMPLETE_CB");
      ss_sdp_search_complete_callback sdp_cb;
      bool ret = sdp_cb.ParseFromString(resBufferString);
      if(!ret) {
        ALOGE("Unable to parse string");
      }
      if(sdp_cb.has_status()) {
         status = (bt_status_t)sdp_cb.status();
      }

      if (sdp_cb.has_remote_bd_addr()) {
        uint8_t* addr = (uint8_t*)sdp_cb.remote_bd_addr().c_str();
        std::string bt_address = ((RawAddress*)addr)->ToString();
        ALOGI("address is :: %s",bt_address.c_str());
        RawAddress::FromString(bt_address.c_str(), bd_addr);
      }

      if (sdp_cb.has_uuid()) {
       uuid_sdp = Uuid::FromString(sdp_cb.uuid());
      }

      if (sdp_cb.has_record_length()) {
       record_length = sdp_cb.record_length();
      }

      std::string data;
      if (sdp_cb.has_record_data()) {
       data = sdp_cb.record_data();
      }
      rec_data = reinterpret_cast<uint8_t*>((char*)(data.c_str()));

      records[0].hdr.type = SDP_TYPE_RAW;
      records[0].hdr.rfcomm_channel_number = -1;
      records[0].hdr.l2cap_psm = -1;
      records[0].hdr.profile_version = -1;
      records[0].hdr.service_name_length = strlen(srv_name);
      records[0].hdr.service_name = (char*)srv_name;
      records[0].hdr.user1_ptr_len = record_length;
      records[0].hdr.user1_ptr = rec_data;

      ALOGI("%s: Send SDP Search complete callback status: %d, remote address : %s, uuid : %s, record length : %d, record data : %s "           , __func__,status, bd_addr.ToString().c_str(), uuid_sdp.ToString().c_str(), record_count, data.c_str());

      HAL_CBACK(bt_sdp_callbacks, sdp_search_cb, status,
             bd_addr, uuid_sdp, record_count, records);
   }
}
/*******************************************************************************
 *
 * Function         btif_sdp_execute_service
 *
 * Description      Initializes/Shuts down the service
 *
 * Returns          BT_STATUS_SUCCESS on success, BT_STATUS_FAIL otherwise
 *
 ******************************************************************************/
#if 0
bt_status_t btif_sdp_execute_service(bool b_enable) {
  BTIF_TRACE_DEBUG("%s enable:%d", __func__, b_enable);

  if (b_enable) {
    BTA_SdpEnable(sdp_dm_cback);
  } else {
    /* This is called on BT disable so no need to extra cleanup */
  }
  return BT_STATUS_SUCCESS;
}
#endif
