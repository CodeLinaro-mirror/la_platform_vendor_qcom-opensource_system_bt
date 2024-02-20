/******************************************************************************
 *  Copyright (C) 2017, The Linux Foundation. All rights reserved.
 *  Not a Contribution.
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted (subject to the limitations in the
 *  disclaimer below) provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of The Linux Foundation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

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
 *  IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/
/******************************************************************************
 *  Copyright (C) 2009-2016 Broadcom Corporation
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
 ******************************************************************************/
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.

    * Redistribution and use in source and binary forms, with or without
      modification, are permitted (subject to the limitations in the
      disclaimer below) provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
*/

/* Changes from Qualcomm Innovation Center are provided under the following license:
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

#define LOG_TAG "btif_av"

#include "btif_av.h"
#include <base/logging.h>
#include <string.h>
#include <hardware/bluetooth.h>
#include <hardware/bt_av.h>
#include "bt_common.h"
#include "btif_a2dp_audio_interface.h"
#include "audio_hal_interface/aidl/a2dp_encoding.h"
#include "a2dp_codec_api.h"
#include "osi/include/properties.h"

#ifdef ADV_AUDIO_FEATURE
#include "btif_bap_broadcast.h"
#endif

#include "btif_ss_interface.h"
// proto specific
#include "btif/protobuf/include/proto_message_ids.h"
#include "protobuf/proto/a2dp.pb.h"

#define A2DP_AAC_DEFAULT_OFFLOAD_BITRATE 165000

/****************************************************************************
 * global variables
 ****************************************************************************/
static BluetoothSSInterface *btSSInterface = nullptr;

/****************************************************************************
 *  Proto definations
 ****************************************************************************/
namespace a2dp_proto = a2dp::synergy::SynergyProto;

/*****************************************************************************
 *  Local type definitions
 *****************************************************************************/
#define MAX_CONNS 2
const uint8_t INVALID_INDEX = -1;
uint8_t index;
btif_a2dp_codec_config_callback_t codec_config[MAX_CONNS];
void btif_av_ss_callback(uint16_t event, char* p_param);
std::string ToRawString(const RawAddress& bt_addr);
bool btif_av_is_split_a2dp_enabled();
bool isA2dpPlaying();
uint8_t get_available_index(void);
a2dp_proto::ss_btav_a2dp_codec_channel_mode_t getProtoChMode(
  btav_a2dp_codec_index_t, btav_a2dp_codec_channel_mode_t);

/*****************************************************************************
 *  Static variables
 *****************************************************************************/
static bt_status_t start_aidl_a2dp_session(const RawAddress& bd_addr);
static btav_source_callbacks_t* bt_av_src_callbacks = NULL;
//todo: should store codec info during init ?
static std::vector<btav_a2dp_codec_config_t> offload_enabled_codecs_config_;
static std::vector<btav_a2dp_codec_config_t> codec_priorities_;
static RawAddress active_device_ = RawAddress::kEmpty;
static tA2DP_CTRL_CMD pending_cmd = A2DP_CTRL_CMD_NONE;
static btav_audio_state_t play_state = BTAV_AUDIO_STATE_STOPPED;


/*******************************************************************************
 * Function        btav_bld_and_snd_message
 *
 * Description     builds ss message
 *
 * Returns
 ******************************************************************************/
static inline void btav_bld_and_snd_message(uint16_t msgid, uint16_t len,
                        uint16_t mode, std::string payload) {
  ALOGI("%s: %s, msgid=%d", LOG_TAG, __func__, msgid);
  uint8_t bld_msg[MAX_LENGTH_WITH_PROTO_NONE];
  char ss_hdr[MAX_LENGTH_WITH_PROTO_NONE];

  // lets have a boundary check here
  if( msgid < BT_AV_EVT_START || msgid  >=  BT_AV_API_MAX ) {
    BTIF_TRACE_ERROR("%s: Invalid msgid. Bailing out", __func__);
    return;
  }
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
  if (btSSInterface) {
    btSSInterface->postTxMsg(msgStr);
  } else {
    ALOGE("%s: btSSInterface is NULL", LOG_TAG);
  }
}

static bt_status_t init_src(
    btav_source_callbacks_t* callbacks,
    int max_connected_audio_devices,
    const std::vector<btav_a2dp_codec_config_t> &codec_priorities,
    const std::vector<btav_a2dp_codec_config_t> &offload_enabled_codecs) {
  ALOGI("%s: %s", LOG_TAG, __func__);

  std::string str_msg;
  a2dp_proto::ss_init msg_init;
  int codec_cnt = 0;

  int aac_offload_set = 0;
  bt_av_src_callbacks = callbacks;
  for(uint8_t i=0; i < codec_priorities.size(); i++) {
    if(codec_priorities[i].codec_priority != BTAV_A2DP_CODEC_PRIORITY_DISABLED) {
      ALOGI("%s: %s",__func__,codec_priorities[i].ToString().c_str());
      a2dp_proto::ss_btav_a2dp_codec_config_t* a =
                                          msg_init.add_defaultcodecconfig();
      if((a2dp_proto::ss_btav_a2dp_codec_index_t) codec_priorities[i].codec_type == a2dp_proto::SS_BTAV_A2DP_CODEC_INDEX_SOURCE_AAC) {
        aac_offload_set = 1;
        ALOGI("%s: %s aac_offload_set %d", LOG_TAG, __func__, aac_offload_set);
      }
      a->set_codec_type((a2dp_proto::ss_btav_a2dp_codec_index_t) codec_priorities[i].codec_type);
      a->set_codec_priority((a2dp_proto::ss_btav_a2dp_codec_priority_t) codec_priorities[i].codec_priority);
      a->set_sample_rate((a2dp_proto::ss_btav_a2dp_codec_sample_rate_t) codec_priorities[i].sample_rate);
      a->set_bits_per_sample((a2dp_proto::ss_btav_a2dp_codec_bits_per_sample_t)
              codec_priorities[i].bits_per_sample);
      //ALOGI("ch_mode: %d", getProtoChMode(codec_priorities[i].codec_type, codec_priorities[i].channel_mode));
      a->set_channel_mode(getProtoChMode(codec_priorities[i].codec_type, codec_priorities[i].channel_mode));
      a->set_codec_specific_1(codec_priorities[i].codec_specific_1);
      a->set_codec_specific_2(codec_priorities[i].codec_specific_2);
      a->set_codec_specific_3(codec_priorities[i].codec_specific_3);
      a->set_codec_specific_4(codec_priorities[i].codec_specific_4);
      codec_cnt++ ;
    }
  }
  ALOGI("codec count: %d", codec_cnt);
  // for now we support at max 2 codec, SBC and AAC
  // to-do:read supported codec from persist property
  a2dp_proto::ss_btav_a2dp_supported_codec_config_t* supported_config = new a2dp_proto::ss_btav_a2dp_supported_codec_config_t();

  a2dp_proto::ss_btav_a2dp_supported_sbc_config *msg_sbc_config = new a2dp_proto::ss_btav_a2dp_supported_sbc_config();

  msg_sbc_config->set_sample_rate(a2dp_proto::samp_freq_44);
  msg_sbc_config->set_bits_per_sample(a2dp_proto::SS_BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16);
  msg_sbc_config->set_channel_mode(a2dp_proto::ch_md_mono | a2dp_proto::ch_md_joint);
  msg_sbc_config->set_num_subbands(a2dp_proto::subband_8);
  msg_sbc_config->set_alloc_method(a2dp_proto::alloc_md_l);
  msg_sbc_config->set_block_length(a2dp_proto::blocks_4 |
    a2dp_proto::blocks_8 | a2dp_proto::blocks_12 | a2dp_proto::blocks_16);

  msg_sbc_config->set_min_bitpool(2);
  msg_sbc_config->set_max_bitpool(53);

  supported_config->set_allocated_offloadsbccapability(msg_sbc_config);

  if(aac_offload_set) {
    a2dp_proto::ss_btav_a2dp_supported_aac_config *msg_aac_config = new a2dp_proto::ss_btav_a2dp_supported_aac_config();
   msg_aac_config->set_object_type(a2dp_proto::object_type_mpeg2_lc);
  msg_aac_config->set_sample_rate(a2dp_proto::samp_freq_44);
  msg_aac_config->set_channel_mode(a2dp_proto::ch_md_stereo);
  bool vbr_enabled = false;
  char value[PROPERTY_VALUE_MAX] = {'\0'};
  property_get("persist.vendor.qcom.bluetooth.aac_vbr_ctl.enabled", value, "false");
  if (!(strcmp(value,"true"))) {
    ALOGI("%s: AAC VBR is enabled for this target", __func__);
    vbr_enabled = true;
  }
  msg_aac_config->set_vbr_supported(vbr_enabled);
  msg_aac_config->set_bit_rate(A2DP_AAC_DEFAULT_OFFLOAD_BITRATE);
  msg_aac_config->set_bits_per_sample(a2dp_proto::SS_BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16);
  supported_config->set_allocated_offloadaaccapability(msg_aac_config);
  }
  msg_init.set_maxdevice(max_connected_audio_devices);
  msg_init.set_config_count(codec_cnt);
  msg_init.set_allocated_allsupportedoffloadcap(supported_config);
  msg_init.SerializeToString(&str_msg);
  ALOGI("has_allsupportedoffloadcap %d, has_offloadaaccapability %d, has_offloadsbccapability %d",
                  msg_init.has_allsupportedoffloadcap(), supported_config->has_offloadaaccapability(),
                  supported_config->has_offloadsbccapability());
  // register callback with stack
  btSSInterface = BluetoothSSInterface::getInstance();
  if(btSSInterface != NULL){
    ALOGI("%s: registering callback with stack", LOG_TAG);
    btSSInterface->registerCallbacks(BT_PROFILE_ADVANCED_AUDIO_ID, btif_av_ss_callback);
  } else {
    ALOGE("%s: stack interface Initialization failed",LOG_TAG);
  }

  /*ALOGI("%s: offload_enabled_codecs.size()=%d", __func__, offload_enabled_codecs.size());
  for(uint8_t i=0; i < offload_enabled_codecs.size() ; i++) {
    ALOGI(offload_enabled_codecs[i].ToString().c_str());
  }*/

  //Update enabled offload codec at BT-Audio HAL
  //to-do: Should enable based on codec_cnt
  bluetooth::audio::aidl::a2dp::update_codec_offloading_capabilities(
      offload_enabled_codecs);

  btav_bld_and_snd_message(BT_AV_INIT, str_msg.length(),
    PROTO_ENC_DEC, str_msg);

  memset(codec_config, 0, (sizeof(codec_config[0]) * MAX_CONNS));

  return BT_STATUS_SUCCESS;
}

static bt_status_t src_connect_sink(const RawAddress& bd_addr) {
  ALOGI("%s: address = %s", __func__, bd_addr.ToString().c_str());

  std::string str_msg;
  a2dp_proto::ss_connect msg_connect ;

  msg_connect.set_address(ToRawString(bd_addr));
  /* to-do: ismandatorycodecpreferred must be queried on settings side
  * set false for now
  */
  msg_connect.set_ismandatorycodecpreferred(false);
  msg_connect.SerializeToString(&str_msg);

  btav_bld_and_snd_message(BT_AV_CONNECT, str_msg.length(),
    PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

static bt_status_t src_disconnect_sink(const RawAddress& bd_addr) {
  ALOGI("%s: address:%s", __func__, bd_addr.ToString().c_str());

  std::string str_msg;
  a2dp_proto::ss_disconnect msg_disconnect ;

  msg_disconnect.set_address(ToRawString(bd_addr));
  msg_disconnect.SerializeToString(&str_msg);
  btav_bld_and_snd_message(BT_AV_DISCONNECT, str_msg.length(),
                                    PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         set_silence_device
 *
 * Description      Sets the connected device silence state
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
static bt_status_t set_silence_device(const RawAddress& bd_addr, bool silence) {
  ALOGI("%s silence = %d address=%s", __func__, silence,
         bd_addr.ToString().c_str());

  std::string str_msg;
  a2dp_proto::ss_set_silence_device msg_set_silence ;

  msg_set_silence.set_address(ToRawString(bd_addr));
  msg_set_silence.set_silence(silence);
  msg_set_silence.SerializeToString(&str_msg);
  btav_bld_and_snd_message(BT_AV_SILENCE, str_msg.length(),
    PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS ;
}

/*******************************************************************************
 *
 * Function         set_active_device
 *
 * Description      Tears down the AV signalling channel with the remote headset
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
static bt_status_t set_active_device(const RawAddress& bd_addr) {
  ALOGI("%s: active_device=%s, address=%s", __func__, active_device_.ToString().c_str(),
                  bd_addr.ToString().c_str());
  bt_status_t status = BT_STATUS_FAIL;

  if (active_device_ == bd_addr) {
      return BT_STATUS_SUCCESS;
  }

  std::string str_msg;
  a2dp_proto::ss_set_active_device msg_active_device;

  if (bd_addr == RawAddress::kEmpty) {
      /* 1. SetActive Device -> Null */
      if (isA2dpPlaying()) {
          btif_av_handle_hidl_req(A2DP_CTRL_CMD_SUSPEND);
      }
      bluetooth::audio::aidl::a2dp::end_session();
      active_device_ = bd_addr;
      status =  BT_STATUS_SUCCESS;
  } else if (active_device_ == RawAddress::kEmpty) {
      /* 2. SetActive Null -> Device */
      active_device_ = bd_addr;
      status = start_aidl_a2dp_session(bd_addr);
  } else {
      /* 3. SetActive Device -> Device */
      // End the currently active session
      if (isA2dpPlaying()) {
          btif_av_handle_hidl_req(A2DP_CTRL_CMD_SUSPEND);
      }
      bluetooth::audio::aidl::a2dp::end_session();
      active_device_ = bd_addr;
      status = start_aidl_a2dp_session(bd_addr);
  }
  if (status == BT_STATUS_FAIL) {
      active_device_ = RawAddress::kEmpty;
  }

  msg_active_device.set_address(ToRawString(active_device_));
  msg_active_device.SerializeToString(&str_msg);
  btav_bld_and_snd_message(BT_AV_ACTIVE, str_msg.length(),
                  PROTO_ENC_DEC, str_msg);
  return status;
}

static bt_status_t start_aidl_a2dp_session(const RawAddress& bd_addr) {
  //start audio session with BT Audio HAL
  if (bluetooth::audio::aidl::a2dp::init() == false) {
      ALOGE("encoding.init() returning false");
      return BT_STATUS_FAIL;
  }
  if (bluetooth::audio::aidl::a2dp::setup_codec() == false) {
      ALOGE("encoding.setup_codec() returning false");
      return BT_STATUS_FAIL;
  }
  bluetooth::audio::aidl::a2dp::start_session();
  ALOGI("bluetooth::audio::aidl::a2dp::session started");
  return BT_STATUS_SUCCESS;
}

uint8_t get_available_index(void) {
  uint8_t idx = 0;
  while(idx < MAX_CONNS) {
    if (codec_config[idx].bd_address == RawAddress::kEmpty) {
        return idx;
    }
    idx++;
  }
  return INVALID_INDEX;
}

static bt_status_t codec_config_src(const RawAddress& bd_addr,
    std::vector<btav_a2dp_codec_config_t> codec_preferences) {
  ALOGI("%s: address:%s", __func__, bd_addr.ToString().c_str());

  std::string str_msg;
  a2dp_proto::ss_config_codec msg_config_codec ;

  for(uint8_t i=0; i < codec_preferences.size() ; i++) {
    a2dp_proto::ss_btav_a2dp_codec_config_t* a  =
            msg_config_codec.add_codec_preference();
    a->set_codec_type( (a2dp_proto::ss_btav_a2dp_codec_index_t) codec_preferences[i].codec_type );
    a->set_codec_priority( (a2dp_proto::ss_btav_a2dp_codec_priority_t) codec_preferences[i].codec_priority );
    a->set_sample_rate( (a2dp_proto::ss_btav_a2dp_codec_sample_rate_t) codec_preferences[i].sample_rate );
    a->set_bits_per_sample( (a2dp_proto::ss_btav_a2dp_codec_bits_per_sample_t)  codec_preferences[i].bits_per_sample );
    a->set_channel_mode( (a2dp_proto::ss_btav_a2dp_codec_channel_mode_t) codec_preferences[i].channel_mode);
    a->set_codec_specific_1(codec_preferences[i].codec_specific_1);
    a->set_codec_specific_2(codec_preferences[i].codec_specific_2);
    a->set_codec_specific_3(codec_preferences[i].codec_specific_3);
    a->set_codec_specific_4(codec_preferences[i].codec_specific_4);
  }

  msg_config_codec.set_config_count(codec_preferences.size());
  msg_config_codec.set_address(ToRawString(bd_addr));
  msg_config_codec.SerializeToString(&str_msg);
  btav_bld_and_snd_message(BT_AV_CODEC_CONFIG, str_msg.length(),
    PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

static void cleanup_src(void) {
  ALOGI("%s:", __func__);

  std::string str_msg = "";
  active_device_ = RawAddress::kEmpty;// 00:00:00:00:00:00
  play_state = BTAV_AUDIO_STATE_STOPPED;

  a2dp_proto::ss_cleanup msg_cleanup;
  msg_cleanup.SerializeToString(&str_msg);
  btav_bld_and_snd_message(BT_AV_CLEANUP, str_msg.length(), PROTO_NONE, str_msg);

  if (bt_av_src_callbacks) {
    ALOGI("%s: setting call backs to NULL",__func__);
    bt_av_src_callbacks = NULL;
  }
}

static const btav_source_interface_t bt_av_src_interface = {
  sizeof(btav_source_interface_t),
  init_src,
  src_connect_sink,
  src_disconnect_sink,
  set_silence_device,
  set_active_device,
  codec_config_src,
  cleanup_src,
};

/*******************************************************************************
 *
 * Function         btif_av_get_src_interface
 *
 * Description      Get the AV callback interface for A2DP source profile
 *
 * Returns          btav_source_interface_t
 *
 ******************************************************************************/
const btav_source_interface_t* btif_av_get_src_interface(void) {
  ALOGI("%s", __func__);
  return &bt_av_src_interface;
}

/*******************************************************************************
 * Callback Function implemetation
 *******************************************************************************/
void btif_av_ss_callback(uint16_t event, char* p_param) {
  ALOGI("%s: event:%d", __func__, event);
  std::string resBufferString = "";
  tBTIF_SS_Cback* cb_data = (tBTIF_SS_Cback*) p_param;
  uint16_t msg_id = cb_data->payload[0] + (((int)(cb_data->payload[1]))<<8);
  uint16_t length = cb_data->payload[2] + (((int)(cb_data->payload[3]))<<8);
  uint16_t proto_enc = 0;
  if( length > 0) {
    proto_enc = cb_data->payload[4] + (((int)(cb_data->payload[5]))<<8);
    char resBuffer[length];
    memcpy(resBuffer, (char *) ((cb_data->payload)+ MSG_PROTO_OFFSET) , length);
    resBufferString.assign(resBuffer, length);
  }
  free (cb_data->payload);
  ALOGI("Sending signal on Conditional variable from AV");
  btSSInterface->setIsSignalSent(true);
  pthread_mutex_lock(&BluetoothSSInterface::ss_cback_mutex);
  pthread_cond_signal(&BluetoothSSInterface::ss_cback_cond_var);
  pthread_mutex_unlock(&BluetoothSSInterface::ss_cback_mutex);
  ALOGI("msg_id is :: %X , Proto length: %d and Proto Encoded Value %d",msg_id,
                    length, proto_enc);
  switch (event) {
    case BT_AV_CONN_STATE_CB:
    {
      a2dp_proto::ss_btav_connection_state_callback connStateCb;
      connStateCb.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)connStateCb.address().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      btav_connection_state_t state = (btav_connection_state_t) connStateCb.state();
      ALOGI("address:%s, state:%d ", bd_addr->ToString().c_str(), state);
      HAL_CBACK(bt_av_src_callbacks, connection_state_cb, *bd_addr, state,
           btav_error_t{.status = bt_status_t::BT_STATUS_SUCCESS, .error_code = BTA_AV_SUCCESS});

      if (state == BTAV_CONNECTION_STATE_DISCONNECTED ) {
          for (uint8_t i = 0; i < MAX_CONNS; i++) {
              if (*bd_addr == codec_config[i].bd_address) {
                  memset(&codec_config[i], 0, sizeof(codec_config[i]));
                  return;
              }
          }
      }
      // This code is used to pass PTS TC for AVDTP ABORT
      // PTS case - AVDTP/SRC/INT/SIG/SMG/BV-23-C
      if(state == BTAV_CONNECTION_STATE_CONNECTED) {
        char value[PROPERTY_VALUE_MAX] = {0};
        if ((osi_property_get("bluetooth.pts.force_a2dp_abort", value, "false")) &&
                                           (!strcmp(value, "true"))) {
          ALOGW("%s:bluetooth.pts.force_a2dp_abort true, intiate AVDT_AbortReq", __func__);
          // bd adress as "ff:ff:ff:ff:ff:ff" during disconect will intiate AVDTP ABORT
          // at slate side.
          src_disconnect_sink(RawAddress::kAny);
        }
      }
    }
    break;
    case BT_AV_AUDIO_STATE_CB:
    {
      a2dp_proto::ss_btav_audio_state_callback audioStateCb;
      audioStateCb.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)audioStateCb.address().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      btav_audio_state_t state = (btav_audio_state_t) audioStateCb.state();
      ALOGI("address:%s, state:%d ", bd_addr->ToString().c_str(), state);
      HAL_CBACK(bt_av_src_callbacks, audio_state_cb, *bd_addr, state);
      /* send ack to HAL based on play status change and then send
      * a2dp cmd (START/SUSPEND)
      */
      play_state = state;
      if(state == BTAV_AUDIO_STATE_STARTED ) {
        if(pending_cmd == A2DP_CTRL_CMD_START) {
            bluetooth::audio::aidl::a2dp::ack_stream_started(A2DP_CTRL_ACK_SUCCESS);
        } else {
            bluetooth::audio::aidl::a2dp::ack_stream_started(A2DP_CTRL_ACK_FAILURE);
        }
      } else if (state == BTAV_AUDIO_STATE_STOPPED) {
        if(pending_cmd == A2DP_CTRL_CMD_SUSPEND) {
            bluetooth::audio::aidl::a2dp::ack_stream_suspended(A2DP_CTRL_ACK_SUCCESS);
        } else {
            bluetooth::audio::aidl::a2dp::ack_stream_suspended(A2DP_CTRL_ACK_FAILURE);
        }
      } else if(state == BTAV_AUDIO_STATE_REMOTE_SUSPEND) {
          // need to handle this
      }
      //clear pending command here, Do retry ??
      pending_cmd =  A2DP_CTRL_CMD_NONE;
    }
    break;
    case BT_AV_SRC_CODEC_CONFIG_CB:
    {
      a2dp_proto::ss_btav_audio_source_config_callback codecConfigCb;

      codecConfigCb.ParseFromString(resBufferString);
      std::vector<btav_a2dp_codec_config_t> codecs_local_capabilities;
      std::vector<btav_a2dp_codec_config_t> codecs_selectable_capabilities;
      uint8_t* addr = (uint8_t*)codecConfigCb.address().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      if (!is_valid_bd_addr(bd_addr)) return;

      ALOGI("address = %s", bd_addr->ToString().c_str());
      for(int i = 0; i< codecConfigCb.codecs_local_capabilities_size(); i++){
        a2dp_proto::ss_btav_a2dp_codec_config_t* codec_cap =
                                      codecConfigCb.mutable_codecs_local_capabilities(i);
        btav_a2dp_codec_config_t codec_;
        codec_.codec_type = (btav_a2dp_codec_index_t) codec_cap->codec_type();
        codec_.codec_priority = (btav_a2dp_codec_priority_t) codec_cap->codec_priority();
        codec_.sample_rate = (btav_a2dp_codec_sample_rate_t) codec_cap->sample_rate();
        codec_.bits_per_sample = (btav_a2dp_codec_bits_per_sample_t) codec_cap->bits_per_sample();
        codec_.channel_mode = (btav_a2dp_codec_channel_mode_t) codec_cap->channel_mode();
        codec_.codec_specific_1 = codec_cap->codec_specific_1();
        codec_.codec_specific_2 = codec_cap->codec_specific_2();
        codec_.codec_specific_3 = codec_cap->codec_specific_3();
        codec_.codec_specific_4 = codec_cap->codec_specific_4();
        ALOGI("local_cap: %s", codec_.ToString().c_str());
        codecs_local_capabilities.push_back(codec_);
      }

      for(int i =0; i< codecConfigCb.codecs_selectable_capabilities_size(); i++){
        a2dp_proto::ss_btav_a2dp_codec_config_t* codec_cap =
                                      codecConfigCb.mutable_codecs_selectable_capabilities(i);
        btav_a2dp_codec_config_t codec_;
        codec_.codec_type = (btav_a2dp_codec_index_t) codec_cap->codec_type();
        codec_.codec_priority = (btav_a2dp_codec_priority_t) codec_cap->codec_priority();
        codec_.sample_rate = (btav_a2dp_codec_sample_rate_t) codec_cap->sample_rate();
        codec_.bits_per_sample = (btav_a2dp_codec_bits_per_sample_t) codec_cap->bits_per_sample();
        codec_.channel_mode = (btav_a2dp_codec_channel_mode_t) codec_cap->channel_mode();
        codec_.codec_specific_1 = codec_cap->codec_specific_1();
        codec_.codec_specific_2 = codec_cap->codec_specific_2();
        codec_.codec_specific_3 = codec_cap->codec_specific_3();
        codec_.codec_specific_4 = codec_cap->codec_specific_4();
        ALOGI("selectable_cap: %s", codec_.ToString().c_str());
        codecs_selectable_capabilities.push_back(codec_);
      }

      a2dp_proto::ss_btav_a2dp_codec_config_callback_t* codec_cap =
                                          codecConfigCb.mutable_codec_config();
      index = get_available_index();
      if (index == INVALID_INDEX){
          ALOGE("Reached the maximum connections for a2dp");
          src_disconnect_sink(*bd_addr);
          return;
      }
      codec_config[index].bd_address = *bd_addr;
      codec_config[index].codec_config_.codec_type =
                      (btav_a2dp_codec_index_t) codec_cap->mutable_codecconfig()->codec_type();
      codec_config[index].codec_config_.codec_priority =
                      (btav_a2dp_codec_priority_t) codec_cap->mutable_codecconfig()->codec_priority();
      codec_config[index].codec_config_.sample_rate =
                      (btav_a2dp_codec_sample_rate_t) codec_cap->mutable_codecconfig()->sample_rate();
      codec_config[index].codec_config_.bits_per_sample =
                      (btav_a2dp_codec_bits_per_sample_t) codec_cap->mutable_codecconfig()->bits_per_sample();
      codec_config[index].codec_config_.channel_mode =
                      (btav_a2dp_codec_channel_mode_t) codec_cap->mutable_codecconfig()->channel_mode();
      codec_config[index].codec_config_.codec_specific_1 = codec_cap->mutable_codecconfig()->codec_specific_1();
      codec_config[index].codec_config_.codec_specific_2 = codec_cap->mutable_codecconfig()->codec_specific_2();
      codec_config[index].codec_config_.codec_specific_3 = codec_cap->mutable_codecconfig()->codec_specific_3();
      codec_config[index].codec_config_.codec_specific_4 = codec_cap->mutable_codecconfig()->codec_specific_4();
      codec_config[index].alloc_method = codec_cap->alloc_method();
      codec_config[index].peer_mtu = codec_cap->peer_mtu();
      codec_config[index].is_peer_edr = codec_cap->is_peer_edr();
      codec_config[index].min_bitpool = codec_cap->min_bitpool();
      codec_config[index].max_bitpool = codec_cap->max_bitpool();
      codec_config[index].num_subbands = codec_cap->num_subbands();
      codec_config[index].block_length = codec_cap->block_length();
      ALOGI("%s: %s",__func__,codec_config[index].codec_config_.ToString().c_str());

      // to-do: should call is_restart_session_needed() in case codec gets changed
      // for active device
      // for SBC update ch_mode joint stereo as stereo to framework
      btav_a2dp_codec_config_t hal_codec_config;
      memcpy(&hal_codec_config, &codec_config[index].codec_config_, sizeof(btav_a2dp_codec_config_t));
      if((codec_config[index].codec_config_.codec_type == BTAV_A2DP_CODEC_INDEX_SOURCE_SBC)
          && codec_cap->mutable_codecconfig()->channel_mode() == a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_JOINT_STEREO) {
        ALOGI("Converting channel mode for SBC");
        hal_codec_config.channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO ;
      }
      HAL_CBACK(bt_av_src_callbacks, audio_config_cb, *bd_addr, hal_codec_config,
            codecs_local_capabilities, codecs_selectable_capabilities);
    }
    break;
    case BT_AV_DELAY_REPORT_CB:
    {
      a2dp_proto::ss_bt_delay_report_callback delayReportCb;
      delayReportCb.ParseFromString(resBufferString);
      int delay = delayReportCb.delay();
      ALOGI("delay report: %d", delay);
      bluetooth::audio::aidl::a2dp::set_remote_delay(delay);
    }
    break;
    default:
    {
      ALOGE("Unknown callback");
    }
  }
 }

btif_a2dp_codec_config_callback_t* btif_av_get_a2dp_current_codec(void) {
  ALOGI("%s", __func__);
  for (int i = 0; i < MAX_CONNS; i++) {
      if (active_device_ == codec_config[i].bd_address) {
          return &codec_config[i];
      }
  }
  return NULL;
}

void btif_av_handle_hidl_req(tA2DP_CTRL_CMD cmd){
  ALOGI("%s, cmd: %d", __func__, cmd);
  //save cmd received from BT-Audio HAL until we get ackowledge from peer
  pending_cmd = cmd;
  switch(cmd){
    case A2DP_CTRL_CMD_START:
    {
      a2dp_proto::ss_startStream msg_start_stream ;
      std::string str_msg;
      msg_start_stream.set_address(ToRawString(active_device_));
      msg_start_stream.SerializeToString(&str_msg);
      btav_bld_and_snd_message(BT_AV_START_STREAM, str_msg.length(),
                            PROTO_ENC_DEC, str_msg);
    }
    break;
    case A2DP_CTRL_CMD_STOP:
    case A2DP_CTRL_CMD_SUSPEND:
    {
      if(false == isA2dpPlaying()) {
        // case when there is ongoing call and BT called a2dpSuspend=true on MM-Audio
        ALOGI("Already in stop play state. Send Success anyway");
        bluetooth::audio::aidl::a2dp::ack_stream_suspended(A2DP_CTRL_ACK_SUCCESS);
        pending_cmd = A2DP_CTRL_CMD_NONE;
        break;
      }
      a2dp_proto::ss_stopStream msg_stop_stream ;
      std::string str_msg = "";
      msg_stop_stream.SerializeToString(&str_msg);
      btav_bld_and_snd_message(BT_AV_STOP_STREAM, str_msg.length(),
                            PROTO_NONE, str_msg);
    }
   break;
   default:
    ALOGE("Unknown HIDL cmd");
   break;
   }
 }

std::string ToRawString(const RawAddress& bt_addr) {
  std::string res;
  res = base::StringPrintf("%02x%02x%02x%02x%02x%02x", bt_addr.address[0],
                            bt_addr.address[1], bt_addr.address[2], bt_addr.address[3],
                            bt_addr.address[4], bt_addr.address[5]);
  ALOGI("%s: %s", LOG_TAG, res.c_str());
  return res;
}

bool btif_av_is_split_a2dp_enabled(){
  return true;
}

inline bool isA2dpPlaying() {
  ALOGI("%s: play_state is %d", __func__, play_state);
  return play_state == BTAV_AUDIO_STATE_STARTED;
}

a2dp_proto::ss_btav_a2dp_codec_channel_mode_t getProtoChMode(
  btav_a2dp_codec_index_t codec_type, btav_a2dp_codec_channel_mode_t ch_mode) {

  a2dp_proto::ss_btav_a2dp_codec_channel_mode_t mode;

  ALOGI("%s: codec_type: %d, ch_mode: %d", __func__, codec_type, ch_mode);

  switch(codec_type) {
    case BTAV_A2DP_CODEC_INDEX_SOURCE_SBC:
    {
      ALOGI("in SBC");
      switch(ch_mode){
        case BTAV_A2DP_CODEC_CHANNEL_MODE_MONO:
        {
          ALOGI("in SBC MONO");
          mode = a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_MONO;
        }
        break;
        case BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO:
        {
          ALOGI("in SBC ST");
          mode = a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_JOINT_STEREO;
        }
        break;
        default:
        {
          ALOGE("Unknown ch mode");
          mode = a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_NONE;
        }
      }
    }
    break;
    case BTAV_A2DP_CODEC_INDEX_SOURCE_AAC:
    {
      ALOGI("in AAC");
      switch(ch_mode){
        case BTAV_A2DP_CODEC_CHANNEL_MODE_MONO:
        {
          mode = a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_MONO;
        }
        break;
        case BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO:
        {
          mode = a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
        }
        break;
        default:
        {
          ALOGE("Unknown ch mode");
          mode = a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_NONE;
        }
      }
    }
    break;
    default:
    {
      ALOGE("Unknown codec type");
      mode =  a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_NONE;
    }
  }

  ALOGI("%s: mode: %d", __func__, mode);
  return mode;
}
