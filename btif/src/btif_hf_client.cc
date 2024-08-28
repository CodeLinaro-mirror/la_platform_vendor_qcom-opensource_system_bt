/******************************************************************************
 *
 *  Copyright (c) 2014 The Android Open Source Project
 *  Copyright (C) 2009-2012 Broadcom Corporation
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

/*******************************************************************************
 *
 *  Filename:      btif_hf_client.c
 *
 *  Description:   Handsfree Profile (HF role) Bluetooth Interface
 *
 *  Notes:
 *  a) Lifecycle of a control block
 *  Control block handles the lifecycle for a particular remote device's
 *  connection. The connection can go via the classic phases but more
 *  importantly there's only two messages from BTA that affect this.
 *  BTA_HF_CLIENT_OPEN_EVT and BTA_HF_CLIENT_CLOSE_EVT. Since the API between
 *  BTIF and BTA is controlled entirely by handles it's important to know where
 *  the handles are created and destroyed. Handles can be created at two
 *  locations:
 *  -- While connect() is called from BTIF. This is an outgoing connection
 *  -- While accepting an incoming connection (see BTA_HF_CLIENT_OPEN_EVT
 *  handling).
 *
 *  The destruction or rather reuse of handles can be done when
 *  BTA_HF_CLIENT_CLOSE_EVT is called. Refer to the event handling for details
 *  of this.
 *
 ******************************************************************************/

#define LOG_TAG "bt_btif_hfc"

#include <stdlib.h>
#include <string.h>

#include <hardware/bluetooth.h>
#include <hardware/bt_hf_client.h>

#include "bt_target.h"
#include "log/log.h"

#include "btif_ss_interface.h"
#include "btif/protobuf/include/proto_message_ids.h"
#include "protobuf/proto/hf_client.pb.h"

#include "btif_common.h"
namespace hf_client_proto = headsetClient::synergy::SynergyProto;

/*******************************************************************************
 *  Constants & Macros
 ******************************************************************************/

#ifndef BTIF_HF_CLIENT_SERVICE_NAME
#define BTIF_HF_CLIENT_SERVICE_NAME ("Handsfree")
#endif

#define BTIF_HF_CLIENT_PEER_INBAND 0x00000008    /* In-band ring tone */

/**
 * Executes HF CLIENT CALLBACKS in btif context
 */
void btif_hf_client_ss_callback(uint16_t event, char* payload);

/*******************************************************************************
 *  Static variables
 ******************************************************************************/
/* HF features supported at runtime */
static uint32_t btif_hf_client_features = BTIF_HF_CLIENT_FEATURES;
static bthf_client_callbacks_t* bt_hf_client_callbacks = NULL;
BluetoothSSInterface *hfBTSSInterface = NULL;

#define HAL_CL_CBACK(P_CB, P_CBACK, ...)    \
  do {                                      \
    if ((P_CB) && (P_CB)->P_CBACK) {        \
      ALOGI("HAL %s->%s", #P_CB, #P_CBACK); \
      (P_CB)->P_CBACK(__VA_ARGS__);         \
    }                                       \
  } while(0)

/*******************************************************************************
 *  Static functions
 ******************************************************************************/
static inline void bthf_client_Build_Post_Msg(uint16_t msgid, uint16_t len,
                uint16_t mode, std::string payload) {
  ALOGI("%s", __func__);
  uint8_t bld_msg[MAX_LENGTH_WITH_PROTO_NONE];
  char ss_hdr[MAX_LENGTH_WITH_PROTO_NONE];
  bld_msg[0] = msgid & 0xff;
  bld_msg[1] = (msgid >> 8);
  bld_msg[2] = len & 0xff;
  bld_msg[3] = (len >> 8);
  bld_msg[4] = mode & 0xff;
  bld_msg[5] = (mode >> 8);
  memcpy(ss_hdr, (char *) bld_msg, MAX_LENGTH_WITH_PROTO_NONE);
  std::string msgStr( ss_hdr, MAX_LENGTH_WITH_PROTO_NONE);
  if (len) {
    msgStr.append(payload);
  }
  hfBTSSInterface->postTxMsg(msgStr);
}

/*******************************************************************************
 *
 * Function         send_at_cmd
 *
 * Description      Send requested AT command to rempte device.
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
static bt_status_t send_at_cmd(const RawAddress* bd_addr, int cmd, int val1,
                               int val2, const char* arg) {
  ALOGI("%s", __func__);
  if (bd_addr == NULL) return BT_STATUS_FAIL;
  std::string payload;
  hf_client_proto::ss_send_at_cmd _send_at_cmd;
  _send_at_cmd.set_bd_addr(ToRawString(bd_addr));
  _send_at_cmd.set_cmd(cmd);
  _send_at_cmd.set_val1(val1);
  _send_at_cmd.set_val2(val2);

  if (arg == NULL) {
    _send_at_cmd.set_arg("");
  } else {
    _send_at_cmd.set_arg(arg);
  }

  _send_at_cmd.SerializeToString(&payload);

  bthf_client_Build_Post_Msg(BT_HF_CLIENT_SEND_AT_CMD, payload.length(), PROTO_ENC_DEC,
                  payload);

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function        init
 *
 * Description     initializes the hf interface
 *
 * Returns         bt_status_t
 *
 ******************************************************************************/
static bt_status_t init(bthf_client_callbacks_t* callbacks) {
  ALOGI("%s", __func__);
  bt_hf_client_callbacks = callbacks;
  std::string payload;
  hf_client_proto::ss_init _init;
  _init.set_features(btif_hf_client_features);
  _init.SerializeToString(&payload);

  if (hfBTSSInterface == NULL){
    hfBTSSInterface = BluetoothSSInterface::getInstance();
    if (hfBTSSInterface == NULL) {
      ALOGI("%s single stack interface Initialization failed",__func__);
    } else {
      ALOGI("%s single stack interface Initialization success",__func__);
    }
  } else {
    ALOGI("%s: single stack interface is already created",__func__);
  }

  if (hfBTSSInterface != NULL) {
    ALOGI("%s: registering Headset client profile callback with ss_interface", __func__);
    hfBTSSInterface->registerCallbacks(BT_PROFILE_HANDSFREE_CLIENT_ID, btif_hf_client_ss_callback);
  }

  bthf_client_Build_Post_Msg(BT_HF_CLIENT_INIT, payload.length(),
                  PROTO_ENC_DEC, payload);

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         connect
 *
 * Description     connect to audio gateway
 *
 * Returns         bt_status_t
 *
 ******************************************************************************/
static bt_status_t connect(const RawAddress* bd_addr) {
  ALOGI("%s", __func__);
  if (bd_addr == NULL) return BT_STATUS_FAIL;
  std::string payload;
  hf_client_proto::ss_Connect _connect;

  _connect.set_bd_addr(ToRawString(bd_addr));
  _connect.SerializeToString(&payload);

  bthf_client_Build_Post_Msg(BT_HF_CLIENT_CONNECT, payload.length(),
                  PROTO_ENC_DEC, payload);

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         disconnect
 *
 * Description      disconnect from audio gateway
 *
 * Returns         bt_status_t
 *
 ******************************************************************************/
static bt_status_t disconnect(const RawAddress* bd_addr) {
  ALOGI("%s", __func__);
  if (bd_addr == NULL) return BT_STATUS_FAIL;
  std::string payload;
  hf_client_proto::ss_Disconnect _disconnect;

  _disconnect.set_bd_addr(ToRawString(bd_addr));
  _disconnect.SerializeToString(&payload);

  bthf_client_Build_Post_Msg(BT_HF_CLIENT_DISCONNECT, payload.length(),
                  PROTO_ENC_DEC, payload);

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         connect_audio
 *
 * Description     create an audio connection
 *
 * Returns         bt_status_t
 *
 ******************************************************************************/
static bt_status_t connect_audio(const RawAddress* bd_addr) {
  ALOGI("%s", __func__);
  if (bd_addr == NULL) return BT_STATUS_FAIL;

  std::string payload;
  hf_client_proto::ss_Connect_audio _connect_audio;

  _connect_audio.set_bd_addr(ToRawString(bd_addr));
  _connect_audio.SerializeToString(&payload);

  bthf_client_Build_Post_Msg(BT_HF_CLIENT_CONNECT_AUDIO, payload.length(),
                  PROTO_ENC_DEC, payload);

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         disconnect_audio
 *
 * Description      close the audio connection
 *
 * Returns         bt_status_t
 *
 ******************************************************************************/
static bt_status_t disconnect_audio(const RawAddress* bd_addr) {
  ALOGI("%s", __func__);
  if (bd_addr == NULL) return BT_STATUS_FAIL;

  std::string payload;
  hf_client_proto::ss_Disconnect_audio _disconnect_audio;
  _disconnect_audio.set_bd_addr(ToRawString(bd_addr));
  _disconnect_audio.SerializeToString(&payload);

  bthf_client_Build_Post_Msg(BT_HF_CLIENT_DISCONNECT_AUDIO,
                  payload.length(), PROTO_ENC_DEC, payload);

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         start_voice_recognition
 *
 * Description      start voice recognition
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
static bt_status_t start_voice_recognition(const RawAddress* bd_addr) {
  ALOGI("%s", __func__);
  if (bd_addr == NULL ) return BT_STATUS_FAIL;

  std::string payload;
  hf_client_proto::ss_start_voice_recognition _start_vr;

  _start_vr.set_bd_addr(ToRawString(bd_addr));
  _start_vr.SerializeToString(&payload);

  bthf_client_Build_Post_Msg(BT_HF_CLIENT_START_VOICE_RECOGNITION, payload.length(),
                  PROTO_ENC_DEC, payload);
  return BT_STATUS_UNSUPPORTED;
}

/*******************************************************************************
 *
 * Function         stop_voice_recognition
 *
 * Description      stop voice recognition
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
static bt_status_t stop_voice_recognition(const RawAddress* bd_addr) {
  ALOGI("%s", __func__);
  if (bd_addr == NULL ) return BT_STATUS_FAIL;

  std::string payload;
  hf_client_proto::ss_stop_voice_recognition _stop_vr;

  _stop_vr.set_bd_addr(ToRawString(bd_addr));
  _stop_vr.SerializeToString(&payload);

  bthf_client_Build_Post_Msg(BT_HF_CLIENT_STOP_VOICE_RECOGNITION, payload.length(),
                  PROTO_ENC_DEC, payload);

  return BT_STATUS_UNSUPPORTED;
}

/*******************************************************************************
 *
 * Function         volume_control
 *
 * Description      volume control
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
static bt_status_t volume_control(const RawAddress* bd_addr,
                                  bthf_client_volume_type_t type, int volume) {
  ALOGI("%s", __func__);
  if (bd_addr == NULL ) return BT_STATUS_FAIL;

  std::string payload;
  hf_client_proto::ss_volume_control _volume_control;

  _volume_control.set_bd_addr(ToRawString(bd_addr));
  _volume_control.set_type((hf_client_proto::ss_bthf_client_volume_type_t)type);
  _volume_control.set_volume(volume);
  _volume_control.SerializeToString(&payload);

  bthf_client_Build_Post_Msg(BT_HF_CLIENT_VOLUME_CONTROL, payload.length(),
                  PROTO_ENC_DEC, payload);
  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         dial
 *
 * Description      place a call
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
static bt_status_t dial(const RawAddress* bd_addr,
                        const char* number) {
  ALOGI("%s", __func__);
  if (bd_addr == NULL ) return BT_STATUS_FAIL;

  std::string payload;
  hf_client_proto::ss_dial _dial;

  _dial.set_bd_addr(ToRawString(bd_addr));
  _dial.set_number(number);
  _dial.SerializeToString(&payload);

  bthf_client_Build_Post_Msg(BT_HF_CLIENT_DIAL_NUM, payload.length(),
                  PROTO_ENC_DEC, payload);
  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         dial_memory
 *
 * Description      place a call with number specified by location (speed dial)
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
static bt_status_t dial_memory(const RawAddress* bd_addr, int location) {
  ALOGI("%s", __func__);
  if (bd_addr == NULL ) return BT_STATUS_FAIL;
  std::string payload;
  hf_client_proto::ss_dial_memory _dial_memory;

  _dial_memory.set_bd_addr(ToRawString(bd_addr));
  _dial_memory.set_location(location);
  _dial_memory.SerializeToString(&payload);

  bthf_client_Build_Post_Msg(BT_HF_CLIENT_DIAL_MEMORY, payload.length(),
                  PROTO_ENC_DEC, payload);

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         handle_call_action
 *
 * Description      handle specified call related action
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
static bt_status_t handle_call_action(const RawAddress* bd_addr,
                                      bthf_client_call_action_t action,
                                      int idx) {
  ALOGI("%s", __func__);
  if (bd_addr == NULL) return BT_STATUS_FAIL;

  std::string payload;
  hf_client_proto::ss_handle_call_action _handle_call_action;

  _handle_call_action.set_bd_addr(ToRawString(bd_addr));
  _handle_call_action.set_action((hf_client_proto::ss_bthf_client_call_action_t)action);
  _handle_call_action.set_idx(idx);
  _handle_call_action.SerializeToString(&payload);

  bthf_client_Build_Post_Msg(BT_HF_CLIENT_CALL_HANDLE, payload.length(),
                  PROTO_ENC_DEC, payload);

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         query_current_calls
 *
 * Description      query list of current calls
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
static bt_status_t query_current_calls(const RawAddress* bd_addr) {
  ALOGI("%s", __func__);
  if (bd_addr == NULL) return BT_STATUS_FAIL;

  std::string payload;
  hf_client_proto::ss_query_current_calls _query_calls;

  _query_calls.set_bd_addr(ToRawString(bd_addr));
  _query_calls.SerializeToString(&payload);

  bthf_client_Build_Post_Msg(BT_HF_CLIENT_QUERY_CURRENT_CALLS, payload.length(),
                  PROTO_ENC_DEC, payload);

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         query_current_operator_name
 *
 * Description      query current selected operator name
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
static bt_status_t query_current_operator_name(const RawAddress* bd_addr) {
  ALOGI("%s", __func__);
  if (bd_addr == NULL) return BT_STATUS_FAIL;

  std::string payload;
  hf_client_proto::ss_query_current_operator_name _query_con;

  _query_con.set_bd_addr(ToRawString(bd_addr));
  _query_con.SerializeToString(&payload);

  bthf_client_Build_Post_Msg(BT_HF_CLIENT_QUERY_CURRENT_OPERATOR, payload.length(),
                  PROTO_ENC_DEC, payload);

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         retieve_subscriber_info
 *
 * Description      retrieve subscriber number information
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
static bt_status_t retrieve_subscriber_info(const RawAddress* bd_addr) {
  ALOGI("%s", __func__);
  if (bd_addr == NULL) return BT_STATUS_FAIL;

  std::string payload;
  hf_client_proto::ss_retrieve_subscriber_info _retrive_sinfo;

  _retrive_sinfo.set_bd_addr(ToRawString(bd_addr));
  _retrive_sinfo.SerializeToString(&payload);

  bthf_client_Build_Post_Msg(BT_HF_CLIENT_RETRIVE_SUBSCRIBER_INFO, payload.length(),
                  PROTO_ENC_DEC, payload);

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         send_dtmf
 *
 * Description      send dtmf
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
static bt_status_t send_dtmf(const RawAddress* bd_addr, char code) {
  ALOGI("%s", __func__);
  if (bd_addr == NULL) return BT_STATUS_FAIL;

  std::string payload;
  hf_client_proto::ss_send_dtmf _send_dtmf;

  _send_dtmf.set_bd_addr(ToRawString(bd_addr));
  _send_dtmf.set_code(&code);
  _send_dtmf.SerializeToString(&payload);

  bthf_client_Build_Post_Msg(BT_HF_CLIENT_SEND_DTMF_CODE, payload.length(),
                  PROTO_ENC_DEC, payload);

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         request_last_voice_tag_number
 *
 * Description      Request number from AG for VR purposes
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
static bt_status_t request_last_voice_tag_number(const RawAddress* bd_addr) {
  ALOGI("%s", __func__);
  if (bd_addr == NULL) return BT_STATUS_FAIL;

  std::string payload;
  hf_client_proto::ss_request_last_voice_tag_number _request_lvtn;

  _request_lvtn.set_bd_addr(ToRawString(bd_addr));
  _request_lvtn.SerializeToString(&payload);

  bthf_client_Build_Post_Msg(BT_HF_CLIENT_GET_VOICETAG_NUM, payload.length(),
                  PROTO_ENC_DEC, payload);
 
  return BT_STATUS_SUCCESS;
}


/*******************************************************************************
 *
 * Function         cleanup
 *
 * Description      Closes the HF interface
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
static void cleanup(void) {
  ALOGI("%s", __func__);
  std::string payload;

  if (hfBTSSInterface != NULL) {
    bthf_client_Build_Post_Msg(BT_HF_CLIENT_DEINIT,
                    payload.length(), PROTO_NONE, payload);

    ALOGI("%s: deregistering Headset client profile callback", __func__);
    hfBTSSInterface->deregisterCallbacks(BT_PROFILE_HANDSFREE_CLIENT_ID);
    hfBTSSInterface = NULL;
  }

  if (bt_hf_client_callbacks) {
    ALOGI("%s: setting call backs to NULL", __func__);
    bt_hf_client_callbacks = NULL;
  }
}

static const bthf_client_interface_t bthfClientInterface = {
    sizeof(bthf_client_interface_t),
    .init = init,
    .connect = connect,
    .disconnect = disconnect,
    .connect_audio = connect_audio,
    .disconnect_audio = disconnect_audio,
    .start_voice_recognition = start_voice_recognition,
    .stop_voice_recognition = stop_voice_recognition,
    .volume_control = volume_control,
    .dial = dial,
    .dial_memory = dial_memory,
    .handle_call_action = handle_call_action,
    .query_current_calls = query_current_calls,
    .query_current_operator_name = query_current_operator_name,
    .retrieve_subscriber_info = retrieve_subscriber_info,
    .send_dtmf = send_dtmf,
    .request_last_voice_tag_number = request_last_voice_tag_number,
    .cleanup = cleanup,
    .send_at_cmd = send_at_cmd,
};

/*******************************************************************************
 *
 * Function         btif_hf_client_ss_callback
 *
 * Description      Executes HF CLIENT CALLBACKS in btif context
 *
 * Returns          void
 *
 ******************************************************************************/
void btif_hf_client_ss_callback(uint16_t event, char* payload) {
  ALOGI("%s", __func__);
  std::string resBufferString;
  tBTIF_SS_Cback* cb_data = (tBTIF_SS_Cback*)payload;
  uint16_t msg_id = cb_data->payload[0] + (((int)(cb_data->payload[1]))<<8);
  uint16_t length = cb_data->payload[2] + (((int)(cb_data->payload[3]))<<8);
  uint16_t proto_enc = 0;

  if( length > 0) {
    proto_enc = cb_data->payload[4] + (((int)(cb_data->payload[5]))<<8);
    char resBuffer[length];
    memcpy(resBuffer, (char *) ((cb_data->payload)+ MSG_PROTO_OFFSET) , length);
    resBufferString.assign(resBuffer, length);
  }
  free(cb_data->payload);

  switch (event) {
    case BT_HF_CLIENT_CONN_STATE_CB: {
      ALOGI("%s BT_HF_CLIENT_CONN_STATE_CB ", __func__);
      hf_client_proto :: ss_bthf_client_connection_state_callback connectionStateCB;
      connectionStateCB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)connectionStateCB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, connection_state_cb, bd_addr,
                      (bthf_client_connection_state_t)connectionStateCB.state(),
                      (int)connectionStateCB.peer_feat(), (int)connectionStateCB.chld_feat());

      if ((int)connectionStateCB.peer_feat() & BTIF_HF_CLIENT_PEER_INBAND) {
          HAL_CL_CBACK(bt_hf_client_callbacks, in_band_ring_tone_cb,
          bd_addr, BTHF_CLIENT_IN_BAND_RINGTONE_PROVIDED);
      }
      break;
    }

    case BT_HF_CLIENT_CALL_CB: {
      ALOGI("%s BT_HF_CLIENT_CALL_CB ", __func__);
      hf_client_proto :: ss_bthf_client_call_callback callCB;
      callCB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)callCB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, call_cb, bd_addr,
                (bthf_client_call_t)callCB.call());
      break;
    }

    case BT_HF_CLIENT_CALLSETUP_CB: {
      ALOGI("%s BT_HF_CLIENT_CALLSETUP_CB ", __func__);
      hf_client_proto :: ss_bthf_client_callsetup_callback callSetupCB;
      callSetupCB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)callSetupCB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, callsetup_cb, bd_addr,
                (bthf_client_callsetup_t)callSetupCB.callsetup());
      break;
    }

    case BT_HF_CLIENT_CALLHELD_CB : {
      ALOGI("%s BT_HF_CLIENT_CALLHELD_CB", __func__);
      hf_client_proto :: ss_bthf_client_callheld_callback callHeldCB;
      callHeldCB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)callHeldCB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, callheld_cb, bd_addr,
                (bthf_client_callheld_t)callHeldCB.callheld());
      break;
    }

    case BT_HF_CLIENT_NETWORK_STATE_CB: {
      ALOGI("%s BT_HF_CLIENT_NETWORK_STATE_CB", __func__);
      hf_client_proto :: ss_bthf_client_network_state_callback networkStateCB;
      networkStateCB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)networkStateCB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, network_state_cb, bd_addr,
                (bthf_client_network_state_t)networkStateCB.state());
      break;
    }

    case BT_HF_CLIENT_NETWORK_SIGNAL_CB: {
      ALOGI("%s BT_HF_CLIENT_NETWORK_SIGNAL_CB", __func__);
      hf_client_proto :: ss_bthf_client_network_signal_callback networkSignalCB;
      networkSignalCB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)networkSignalCB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, network_signal_cb, bd_addr,
                      networkSignalCB.signal_strength());
      break;
    }

    case BT_HF_CLIENT_NETWORK_ROAM_CB: {
      ALOGI("%s BT_HF_CLIENT_NETWORK_ROAM_CB", __func__);
      hf_client_proto :: ss_bthf_client_network_roaming_callback networkRoamCB;
      networkRoamCB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)networkRoamCB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, network_roaming_cb, bd_addr,
                (bthf_client_service_type_t)networkRoamCB.type());
      break;
    }

    case BT_HF_CLIENT_BATTERY_LEVEL_CB: {
      ALOGI("%s BT_HF_CLIENT_BATTERY_LEVEL_CB", __func__);
      hf_client_proto :: ss_bthf_client_battery_level_callback batteryLevelCB;
      batteryLevelCB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)batteryLevelCB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, battery_level_cb, bd_addr,
                      batteryLevelCB.battery_level());
      break;
    }

    case BT_HF_CLIENT_VOLUME_CHANGE_CB: {
      ALOGI("%s BT_HF_CLIENT_VOLUME_CHANGE_CB", __func__);
      hf_client_proto :: ss_bthf_client_volume_change_callback volumeChangeCB;
      volumeChangeCB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)volumeChangeCB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, volume_change_cb, bd_addr,
                      (bthf_client_volume_type_t)volumeChangeCB.type(),
                      volumeChangeCB.volume());
      break;
    }

    case BT_HF_CLIENT_VOICE_RECOGNITION_CB: {
      ALOGI("%s BT_HF_CLIENT_VOICE_RECOGNITION_CB", __func__);
      hf_client_proto :: ss_bthf_client_vr_cmd_callback vrCB;
      vrCB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)vrCB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, vr_cmd_cb, bd_addr,
                (bthf_client_vr_state_t)vrCB.state());
      break;
    }

    case BT_HF_CLIENT_CURRENT_OPERATOR_CB: {
      ALOGI("%s BT_HF_CLIENT_CURRENT_OPERATOR_CB", __func__);
      hf_client_proto :: ss_bthf_client_current_operator_callback  curOperatorCB;
      curOperatorCB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)curOperatorCB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, current_operator_cb, bd_addr,
                curOperatorCB.name().c_str());
      break;
    }

    case BT_HF_CLIENT_CLIP_CB : {
      ALOGI("%s BT_HF_CLIENT_CLIP_CB", __func__);
      hf_client_proto :: ss_bthf_client_clip_callback clipCB;
      clipCB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)clipCB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, clip_cb, bd_addr,
                clipCB.number().c_str(),
                clipCB.type(),
                clipCB.has_alpha() ? (clipCB.alpha().c_str() ?
                                       clipCB.alpha().c_str() : NULL) : NULL);
      break;
    }

    case BT_HF_CLIENT_LAST_VOICE_TAG_NUM_CB: {
      ALOGI("%s BT_HF_CLIENT_LAST_VOICE_TAG_NUM_CB", __func__);
      hf_client_proto :: ss_bthf_client_last_voice_tag_number_callback lvtnCB;
      lvtnCB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)lvtnCB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, last_voice_tag_number_callback,
                bd_addr, lvtnCB.number().c_str());
      break;
    }

    case BT_HF_CLIENT_CALL_WAITING_CB: {
      ALOGI("%s BT_HF_CLIENT_CALL_WAITING_CB", __func__);
      hf_client_proto :: ss_bthf_client_call_waiting_callback cwCB;
      cwCB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)cwCB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, call_waiting_cb, bd_addr,
                cwCB.number().c_str());
      break;
    }

    case BT_HF_CLIENT_CMD_COMPLETE_CB : {
      ALOGI("%s BT_HF_CLIENT_CMD_COMPLETE_CB", __func__);
      hf_client_proto :: ss_bthf_client_cmd_complete_callback ccCB;
      ccCB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)ccCB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, cmd_complete_cb, bd_addr,
                (bthf_client_cmd_complete_t)ccCB.type(),
                ccCB.cme());
      break;
    }

    case BT_HF_CLIENT_CURRENT_CALLS_CB: {
      ALOGI("%s BT_HF_CLIENT_CURRENT_CALLS_CB", __func__);
      hf_client_proto :: ss_bthf_client_current_calls  clcc_CB;
      clcc_CB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)clcc_CB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, current_calls_cb, bd_addr,
                clcc_CB.index(),
                clcc_CB.dir() ? BTHF_CLIENT_CALL_DIRECTION_INCOMING
                                 : BTHF_CLIENT_CALL_DIRECTION_OUTGOING,
                (bthf_client_call_state_t)clcc_CB.state(),
                clcc_CB.mpty() ? BTHF_CLIENT_CALL_MPTY_TYPE_MULTI
                                  : BTHF_CLIENT_CALL_MPTY_TYPE_SINGLE,
                clcc_CB.number().c_str() ? clcc_CB.number().c_str() : NULL,
                clcc_CB.type(),
                clcc_CB.has_alpha() ? (clcc_CB.alpha().c_str() ?
                                   clcc_CB.alpha().c_str() : NULL) : NULL);
      break;
    }

    case BT_HF_CLIENT_SUBSCRIBER_INFO_CB: {
      ALOGI("%s BT_HF_CLIENT_SUBSCRIBER_INFO_CB", __func__);
      hf_client_proto :: ss_bthf_client_subscriber_info_callback subInfo_CB;
      subInfo_CB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)subInfo_CB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, subscriber_info_cb, bd_addr,
                   subInfo_CB.name().c_str(),(bthf_client_subscriber_service_type_t)subInfo_CB.type());
      break;
    }

    case BT_HF_CLIENT_RESP_HOLD_CB: {
      ALOGI("%s BT_HF_CLIENT_RESP_HOLD_CB", __func__);
      hf_client_proto :: ss_bthf_client_resp_and_hold_callback respHold_CB;
      respHold_CB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)respHold_CB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      if ((bthf_client_resp_and_hold_t)respHold_CB.resp_and_hold() <=
                      BTRH_CLIENT_RESP_AND_HOLD_REJECT) {
        HAL_CL_CBACK(bt_hf_client_callbacks, resp_and_hold_cb, bd_addr,
                  (bthf_client_resp_and_hold_t)respHold_CB.resp_and_hold());
      }
      break;
    }

    case BT_HF_CLIENT_INBAND_RINGTONE_CB: {
      ALOGI("%s BT_HF_CLIENT_INBAND_RINGTONE_CB", __func__);
      hf_client_proto :: ss_bthf_client_in_band_ring_tone_callback  inbandRing_CB;
      inbandRing_CB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)inbandRing_CB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      if ((bthf_client_in_band_ring_state_t )inbandRing_CB.state() != 0) {
        HAL_CL_CBACK(bt_hf_client_callbacks, in_band_ring_tone_cb, bd_addr,
                  BTHF_CLIENT_IN_BAND_RINGTONE_PROVIDED);
      } else {
        HAL_CL_CBACK(bt_hf_client_callbacks, in_band_ring_tone_cb, bd_addr,
                  BTHF_CLIENT_IN_BAND_RINGTONE_NOT_PROVIDED);
      }
      break;
    }

    case BT_HF_CLIENT_AUDIO_STATE_CB: {
      ALOGI("%s BT_HF_CLIENT_AUDIO_STATE_CB", __func__);
      hf_client_proto :: ss_bthf_client_audio_state_callback  audioState_CB;
      audioState_CB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)audioState_CB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, audio_state_cb, bd_addr,
                (bthf_client_audio_state_t)audioState_CB.state());
      break;
    }

    case BT_HF_CLIENT_RING_IND_CB: {
      ALOGI("%s BT_HF_CLIENT_RING_IND_CB", __func__);
      hf_client_proto :: ss_bthf_client_ring_indication_callback  ring_CB;
      ring_CB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)ring_CB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, ring_indication_cb, bd_addr);
      break;
    }

    case BT_HF_CLIENT_UNKNOWN_EVT_CB: {
      ALOGI("%s BT_HF_CLIENT_UNKNOWN_EVT_CB", __func__);
      hf_client_proto :: ss_bthf_client_unknown_event_callback unknown_CB;
      unknown_CB.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)unknown_CB.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_CL_CBACK(bt_hf_client_callbacks, unknown_event_cb, bd_addr,
                unknown_CB.unknow_event().c_str());
      break;
    }
    default: {
      ALOGI("%s: Unhandled event: %d", __func__, event);
      break;
    }
  }
}

/*******************************************************************************
 *
 * Function         btif_hf_client_get_interface
 *
 * Description      Get the hf callback interface
 *
 * Returns          bthf_client_interface_t
 *
 ******************************************************************************/
const bthf_client_interface_t* btif_hf_client_get_interface(void) {
  ALOGI("%s", __func__);
  return &bthfClientInterface;
}
