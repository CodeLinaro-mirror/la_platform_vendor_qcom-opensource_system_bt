/******************************************************************************
 * Copyright (C) 2017-2018, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 Redistribution and use in source and binary forms, with or without
modification, are permitted (subject to the limitations in the
disclaimer below) provided that the following conditions are met:
   * Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
   * Redistributions in binary form must reproduce the above
     copyright notice, this list of conditions and the following
     disclaimer in the documentation and/or other materials provided
     with the distribution.
   * Neither the name of The Linux Foundation nor the names of its
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
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************************************************************
 *
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
 *  Filename:      btif_hf.c
 *
 *  Description:   Handsfree Profile Bluetooth Interface
 *
 *
 ******************************************************************************/

#define LOG_TAG "bt_btif_hf"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <bta/include/bta_ag_api.h>
#include <hardware/bluetooth.h>
#include <hardware/bluetooth_headset_callbacks.h>
#include <hardware/bluetooth_headset_interface.h>
#include <hardware/bt_hf.h>
#include <log/log.h>
#include "btif_ss_interface.h"


#ifdef BT_LPM_SUPPORTED
#include "btif_lpm.h"
#endif

#include "btif_hf.h"

#include "btif/protobuf/include/proto_message_ids.h"
#ifdef ADV_AUDIO_FEATURE
#include <hardware/bt_apm.h>
#endif

#include "protobuf/proto/hf.pb.h"

#include "btif_common.h"

namespace bluetooth {
namespace headset {

/*******************************************************************************
 *  Constants & Macros
 ******************************************************************************/
#ifndef BTIF_HSAG_SERVICE_NAME
#define BTIF_HSAG_SERVICE_NAME ("Headset Gateway")
#endif

#ifndef BTIF_HFAG_SERVICE_NAME
#define BTIF_HFAG_SERVICE_NAME ("Handsfree Gateway")
#endif

#ifndef BTIF_HF_SERVICE_NAMES
#define BTIF_HF_SERVICE_NAMES \
  { BTIF_HSAG_SERVICE_NAME, BTIF_HFAG_SERVICE_NAME }
#endif

/* HF features supported at runtime */
static uint32_t btif_hf_features = BTIF_HF_FEATURES;
static uint32_t btif_hf_peer_feat;

#define BTIF_HF_CALL_END_TIMEOUT 6

#define BTIF_HF_INVALID_IDX (-1)

/* keep track if SCO allowed for AG */
bool btif_is_sco_allowed = true;

BluetoothSSInterface *agBTSSInterface = NULL;

/*******************************************************************************
 *  Local type definitions
 ******************************************************************************/

/*******************************************************************************
 *  Static variables
 ******************************************************************************/
static Callbacks* bt_hf_callbacks = NULL;

#define ASSERTC(cond, msg, val)                                              \
   do {                                                                       \
     if (!(cond)) {                                                           \
       ALOGI("### ASSERT : %s %s line %d %s (%d) ###", __FILE__, \
                 __func__, __LINE__, (msg), (val));                           \
     }                                                                        \
   } while (0)

#define CHECK_BTHF_INIT()                                             \
  do {                                                                \
    if (bt_hf_callbacks == NULL) {                                    \
      BTIF_TRACE_WARNING("BTHF: %s: BTHF not initialized", __func__); \
      return BT_STATUS_NOT_READY;                                     \
    } else {                                                          \
      BTIF_TRACE_EVENT("BTHF: %s", __func__);                         \
    }                                                                 \
  } while (0)

#define HAL_HF_CBACK(P_CB, P_CBACK, ...)                \
  do {                                                  \
    if (P_CB != NULL ) {                                \
      ALOGE("HAL %s->%s", #P_CB, #P_CBACK);             \
      (P_CB)->P_CBACK(__VA_ARGS__);                     \
    } else {                                            \
      ASSERTC(0, "Callback is NULL", 0);                \
    }                                                   \
  } while (0)

/*******************************************************************************
 *  Static functions
 ******************************************************************************/

/*******************************************************************************
 *  Externs
 ******************************************************************************/
/* By default, even though codec negotiation is enabled, we will not use WBS as
 * the default
 * codec unless this variable is set to true.
 */
#ifndef BTIF_HF_WBS_PREFERRED
#define BTIF_HF_WBS_PREFERRED true
#endif

class HeadsetInterface : Interface {
 public:
  static Interface* GetInstance() {
    static Interface* instance = new HeadsetInterface();
    return instance;
  }
  bt_status_t Init(Callbacks* callbacks, int max_hf_clients,
                   bool inband_ringing_enabled) override;
  bt_status_t Connect(RawAddress* bd_addr) override;
  bt_status_t Disconnect(RawAddress* bd_addr) override;
  bt_status_t ConnectAudio(RawAddress* bd_addr, bool force_cvsd) override;
  bt_status_t DisconnectAudio(RawAddress* bd_addr) override;
  bt_status_t isNoiseReductionSupported(RawAddress* bd_addr) override;
  bt_status_t isVoiceRecognitionSupported(RawAddress* bd_addr) override;
  bt_status_t StartVoiceRecognition(RawAddress* bd_addr) override;
  bt_status_t StopVoiceRecognition(RawAddress* bd_addr) override;
  bt_status_t VolumeControl(bthf_volume_type_t type, int volume,
                            RawAddress* bd_addr) override;
  bt_status_t DeviceStatusNotification(bthf_network_state_t ntk_state,
                                       bthf_service_type_t svc_type, int signal,
                                       int batt_chg, RawAddress* bd_addr) override;
  bt_status_t CopsResponse(const char* cops, RawAddress* bd_addr) override;
  bt_status_t CindResponse(int svc, int num_active, int num_held,
                           bthf_call_state_t call_setup_state, int signal,
                           int roam, int batt_chg,
                           RawAddress* bd_addr) override;
  bt_status_t FormattedAtResponse(const char* rsp,
                                  RawAddress* bd_addr) override;
  bt_status_t AtResponse(bthf_at_response_t response_code, int error_code,
                         RawAddress* bd_addr) override;
  bt_status_t ClccResponse(int index, bthf_call_direction_t dir,
                           bthf_call_state_t state, bthf_call_mode_t mode,
                           bthf_call_mpty_type_t mpty, const char* number,
                           bthf_call_addrtype_t type,
                           RawAddress* bd_addr) override;
  bt_status_t PhoneStateChange(int num_active, int num_held,
                               bthf_call_state_t call_setup_state,
                               const char* number, bthf_call_addrtype_t type,
                               const char* name, RawAddress* bd_addr) override;
  bt_status_t SetScoOffloadEnabled(bool value) override;

  void Cleanup() override;
  bt_status_t SetScoAllowed(bool value) override;
  bt_status_t SendBsir(bool value, RawAddress* bd_addr) override;
  bt_status_t SetActiveDevice(RawAddress* active_device_addr) override;
};

/*******************************************************************************
 *
 * Function        bthf_bld_and_snd_message
 *
 * Description     builds ss message
 *
 * Returns
 *
 ******************************************************************************/
static inline void bthf_bld_and_snd_message(uint16_t msgid, uint16_t len, uint16_t mode,
                                    std::string payload) {
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
  if (agBTSSInterface != NULL) {
     agBTSSInterface->postTxMsg(msgStr);
  } else {
     ALOGE("%s ss interface is null",__func__);
  }
}
/*******************************************************************************
 *
 * Function         Init
 *
 * Description     initializes the hf interface
 *
 * Returns         bt_status_t
 *
 ******************************************************************************/
bt_status_t HeadsetInterface::Init(Callbacks* callbacks, int max_hf_clients,
                                   bool inband_ringing_enabled) {
  ALOGI("%s: inband_ringing_enabled %d", __func__, inband_ringing_enabled);
  std::string str_msg;

  if (inband_ringing_enabled) {
    btif_hf_features |= BT_AG_FEAT_INBAND;
  } else {
    btif_hf_features &= ~BT_AG_FEAT_INBAND;
  }
  ALOGI("%s: max_hf_clients %d btif_hf_features %u", __func__, max_hf_clients,
                   btif_hf_features );
  bt_hf_callbacks = callbacks;
  ss_Init _ss_init;
  _ss_init.set_max_hf_clients(max_hf_clients);
  _ss_init.set_features(btif_hf_features);
  _ss_init.SerializeToString(&str_msg);

  if(agBTSSInterface == NULL){
    agBTSSInterface = BluetoothSSInterface::getInstance();
    if (agBTSSInterface == NULL) {
        ALOGE("%s single stack interface Initialization failed",__func__);
    } else {
        ALOGI("%s single stack interface Initialization success",__func__);
    }
  } else {
        ALOGI("%s: single stack interface is already created",__func__);
  }

  if (agBTSSInterface != NULL) {
     ALOGI("%s: registering Headset profile callback with ss_interface", __func__);
     agBTSSInterface->registerCallbacks(BT_PROFILE_HANDSFREE_ID, btif_hf_ss_callback);
  }
  bthf_bld_and_snd_message(BT_HF_INIT, str_msg.length(), PROTO_ENC_DEC, str_msg);

  return BT_STATUS_SUCCESS;
}

bt_status_t HeadsetInterface::Connect(RawAddress* bd_addr) {
  ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
  std::string str_msg;
  ss_Connect _ss_connect;
  _ss_connect.set_bd_addr(ToRawString(bd_addr));
  _ss_connect.SerializeToString(&str_msg);
  ALOGI("%s: sending bd_addr: %s", __func__, _ss_connect.bd_addr().c_str());
  bthf_bld_and_snd_message(BT_HF_CONNECT,str_msg.length(),
                    PROTO_ENC_DEC, str_msg );



  return BT_STATUS_SUCCESS;
}

bool btif_hf_is_call_vr_idle() {
  return true;
}
/*******************************************************************************
 *
 * Function         Disconnect
 *
 * Description      disconnect from headset
 *
 * Returns         bt_status_t
 *
 ******************************************************************************/
bt_status_t HeadsetInterface::Disconnect(RawAddress* bd_addr) {
  ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
  std::string str_msg;
  ss_Disconnect _ss_disconnect;
  _ss_disconnect.set_bd_addr(ToRawString(bd_addr));
  _ss_disconnect.SerializeToString(&str_msg);
  ALOGI("%s: sending bd_addr: %s", __func__, _ss_disconnect.bd_addr().c_str());
  bthf_bld_and_snd_message(BT_HF_DISCONNECT,str_msg.length(),
                    PROTO_ENC_DEC, str_msg );

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         ConnectAudio
 *
 * Description     create an audio connection
 *
 * Returns         bt_status_t
 *
 ******************************************************************************/
bt_status_t HeadsetInterface::ConnectAudio(RawAddress* bd_addr, bool force_cvsd) {
  ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
  std::string str_msg;
  ss_ConnectAudio _ss_connect_audio;
  _ss_connect_audio.set_bd_addr(ToRawString(bd_addr));
  _ss_connect_audio.SerializeToString(&str_msg);

  bthf_bld_and_snd_message(BT_HF_CONNECT_AUDIO,str_msg.length(),
                   PROTO_ENC_DEC, str_msg );

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         DisconnectAudio
 *
 * Description      close the audio connection
 *
 * Returns         bt_status_t
 *
 ******************************************************************************/
bt_status_t HeadsetInterface::DisconnectAudio(RawAddress* bd_addr) {
  ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
  std::string str_msg;
  ss_DisconnectAudio _ss_disconnect_audio;
  _ss_disconnect_audio.set_bd_addr(ToRawString(bd_addr));
  _ss_disconnect_audio.SerializeToString(&str_msg);

  bthf_bld_and_snd_message(BT_HF_DISCONNECT_AUDIO,str_msg.length(),
                   PROTO_ENC_DEC, str_msg );

  return BT_STATUS_SUCCESS;
}

bt_status_t HeadsetInterface::isNoiseReductionSupported(RawAddress* bd_addr) {
  ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
  if ( btif_hf_peer_feat & BT_AG_PEER_FEAT_ECNR ) {
   /* std::string str_msg;
    ss_isNoiseReductionSupported _ss_isNoiseReductionSupported;
    _ss_isNoiseReductionSupported.set_bd_addr(ToRawString(bd_addr));
    _ss_isNoiseReductionSupported.SerializeToString(&str_msg);

    bthf_bld_and_snd_message(BT_HF_IS_NOISE_REDUCTION_SUPPORTED, str_msg.length(),
                     PROTO_ENC_DEC, str_msg ); */

    return BT_STATUS_SUCCESS;
  }
  ALOGE("%s: feature not supporting", __func__);
  return BT_STATUS_UNSUPPORTED;
}

bt_status_t HeadsetInterface::isVoiceRecognitionSupported(RawAddress* bd_addr) {
  ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
  if ( btif_hf_peer_feat & BT_AG_PEER_FEAT_VREC ) {
    /*std::string str_msg;
    ss_isVoiceRecognitionSupported _ss_isVoiceRecognitionSupported;
    _ss_isVoiceRecognitionSupported.set_bd_addr(ToRawString(bd_addr));
    _ss_isVoiceRecognitionSupported.SerializeToString(&str_msg);

    bthf_bld_and_snd_message(BT_HF_IS_VOICE_RECOGNITION_SUPPORTED, str_msg.length(),
                     PROTO_ENC_DEC, str_msg );*/

    return BT_STATUS_SUCCESS;
  }
  ALOGE("%s: feature not supporting", __func__);
  return BT_STATUS_UNSUPPORTED;
}

/*******************************************************************************
 *
 * Function         StartVoiceRecognition
 *
 * Description      start voice recognition
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
bt_status_t HeadsetInterface::StartVoiceRecognition(RawAddress* bd_addr) {
  ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
  if ( btif_hf_peer_feat & BT_AG_PEER_FEAT_VREC ) {
    std::string str_msg;
    ss_StartVoiceRecognition _ss_StartVoiceRecognition;
    _ss_StartVoiceRecognition.set_bd_addr(ToRawString(bd_addr));
    _ss_StartVoiceRecognition.SerializeToString(&str_msg);

    bthf_bld_and_snd_message(BT_HF_START_VOICE_RECOGNITION, str_msg.length(),
                     PROTO_ENC_DEC, str_msg );

    return BT_STATUS_SUCCESS;
  }
  ALOGE("%s: feature not supporting", __func__);
  return BT_STATUS_UNSUPPORTED;
}


/*******************************************************************************
 *
 * Function         StopVoiceRecognition
 *
 * Description      stop voice recognition
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
bt_status_t HeadsetInterface::StopVoiceRecognition(RawAddress* bd_addr) {
  ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
  if ( btif_hf_peer_feat & BT_AG_PEER_FEAT_VREC ) {
    std::string str_msg;
    ss_StopVoiceRecognition _ss_StopVoiceRecognition;
    _ss_StopVoiceRecognition.set_bd_addr(ToRawString(bd_addr));
    _ss_StopVoiceRecognition.SerializeToString(&str_msg);

    bthf_bld_and_snd_message(BT_HF_STOP_VOICE_RECOGNITION, str_msg.length(),
                     PROTO_ENC_DEC, str_msg );
    return BT_STATUS_SUCCESS;
  }
  ALOGE("%s: feature not supporting", __func__);
  return BT_STATUS_UNSUPPORTED;
}


/*******************************************************************************
 *
 * Function         VolumeControl
 *
 * Description      volume control
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
bt_status_t HeadsetInterface::VolumeControl(bthf_volume_type_t type, int volume,
                                  RawAddress* bd_addr) {
  ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
  std::string str_msg;
  ss_VolumeControl _ss_VolumeControl;
  _ss_VolumeControl.set_type((ss_bthf_volume_type_t) type);
  _ss_VolumeControl.set_bd_addr(ToRawString(bd_addr));
  _ss_VolumeControl.set_volume(volume);
  _ss_VolumeControl.SerializeToString(&str_msg);
  ALOGI("%s: vol %d type %d", __func__, _ss_VolumeControl.volume(), _ss_VolumeControl.type());
  bthf_bld_and_snd_message(BT_HF_VOLUME_CONTROL, str_msg.length(),
                   PROTO_ENC_DEC, str_msg );

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         DeviceStatusNotification
 *
 * Description      Combined device status change notification
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
bt_status_t HeadsetInterface::DeviceStatusNotification(
                                bthf_network_state_t ntk_state,
                                bthf_service_type_t svc_type,
                                int signal, int batt_chg, RawAddress* bd_addr) {
  ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
  std::string str_msg;
  ss_DeviceStatusNotification _ss_DeviceStatusNotification;
  _ss_DeviceStatusNotification.set_ntk_state((ss_bthf_network_state_t) ntk_state);
  _ss_DeviceStatusNotification.set_svc_type((ss_bthf_service_type_t) svc_type);
  _ss_DeviceStatusNotification.set_signal(signal);
  _ss_DeviceStatusNotification.set_batt_chg(batt_chg);
  _ss_DeviceStatusNotification.set_bd_addr(ToRawString(bd_addr));
  _ss_DeviceStatusNotification.SerializeToString(&str_msg);

  ALOGI("%s: state %d type %d signal %d batt %d", __func__, _ss_DeviceStatusNotification.ntk_state(), _ss_DeviceStatusNotification.svc_type(),
        _ss_DeviceStatusNotification.signal(), _ss_DeviceStatusNotification.batt_chg());
  bthf_bld_and_snd_message(BT_HF_DEVICE_STATUS_NOTFICATION, str_msg.length(),
                   PROTO_ENC_DEC, str_msg );

  return BT_STATUS_SUCCESS;
}


/*******************************************************************************
 *
 * Function         CopsResponse
 *
 * Description      Response for COPS command
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
bt_status_t HeadsetInterface::CopsResponse(const char* cops, RawAddress* bd_addr) {
  ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
  std::string str_msg;
  ss_CopsResponse _ss_CopsResponse;
  if (cops != NULL) {
     _ss_CopsResponse.set_cops(cops);
     ALOGI("%s: cops %s", __func__, cops);
  } else {
    _ss_CopsResponse.set_cops("");
  }
  _ss_CopsResponse.set_bd_addr(ToRawString(bd_addr));
  _ss_CopsResponse.SerializeToString(&str_msg);

  bthf_bld_and_snd_message(BT_HF_COPS_RESPONSE, str_msg.length(),
                   PROTO_ENC_DEC, str_msg );

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         cind_response
 *
 * Description      Response for CIND command
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
bt_status_t HeadsetInterface::CindResponse(int svc, int num_active, int num_held,
                                 bthf_call_state_t call_setup_state, int signal,
                                 int roam, int batt_chg, RawAddress* bd_addr) {
  ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
  std::string str_msg;
  ss_CindResponse _ss_CindResponse;
  _ss_CindResponse.set_svc(svc);
  _ss_CindResponse.set_num_active(num_active);
  _ss_CindResponse.set_num_held(num_held);
  _ss_CindResponse.set_call_setup_state((ss_bthf_call_state_t) call_setup_state);
  _ss_CindResponse.set_signal(signal);
  _ss_CindResponse.set_roam(roam);
  _ss_CindResponse.set_batt_chg(batt_chg);
  _ss_CindResponse.set_bd_addr(ToRawString(bd_addr));
  _ss_CindResponse.SerializeToString(&str_msg);

  ALOGI("%s: svc %d active %d held %d state %d signal %d roam %d batt %d", __func__, _ss_CindResponse.svc(), 
           _ss_CindResponse.num_active(),_ss_CindResponse.num_held(),
          _ss_CindResponse.call_setup_state(), _ss_CindResponse.signal(), _ss_CindResponse.roam(),
           _ss_CindResponse.batt_chg());
  bthf_bld_and_snd_message(BT_HF_CIND_RESPONSE, str_msg.length(),
                   PROTO_ENC_DEC, str_msg );

  return BT_STATUS_SUCCESS;
}

bt_status_t HeadsetInterface::SetScoAllowed(bool value) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_SetScoAllowed _ss_SetScoAllowed;
  _ss_SetScoAllowed.set_value(value);
  _ss_SetScoAllowed.SerializeToString(&str_msg);

   bthf_bld_and_snd_message(BT_HF_SET_SCO_ALLOWED, str_msg.length(),
                   PROTO_ENC_DEC, str_msg );

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         FormattedAtResponse
 *
 * Description      Pre-formatted AT response, typically in response to unknown
 *                  AT cmd
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
bt_status_t HeadsetInterface::FormattedAtResponse(const char* rsp,
                                        RawAddress* bd_addr) {
  ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
  std::string str_msg;
  ss_FormattedAtResponse _ss_FormattedAtResponse;
  if  (rsp != NULL) {
    _ss_FormattedAtResponse.set_rsp(rsp);
    ALOGI("%s: rsp %s", __func__, rsp);
  } else {
    _ss_FormattedAtResponse.set_rsp("");
  }
  _ss_FormattedAtResponse.set_bd_addr(ToRawString(bd_addr));
  _ss_FormattedAtResponse.SerializeToString(&str_msg);

  bthf_bld_and_snd_message(BT_HF_FORMATTED_AT_RESPONSE, str_msg.length(),
                   PROTO_ENC_DEC, str_msg );

  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         AtResponse
 *
 * Description      ok/error response
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
bt_status_t HeadsetInterface::AtResponse(bthf_at_response_t response_code,
                               int error_code, RawAddress* bd_addr) {
  ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
  std::string str_msg;
  ss_AtResponse _ss_AtResponse;
  _ss_AtResponse.set_response_code((ss_bthf_at_response_t)response_code);
  _ss_AtResponse.set_error_code(error_code);
  _ss_AtResponse.set_bd_addr(ToRawString(bd_addr));
  _ss_AtResponse.SerializeToString(&str_msg);
  bthf_bld_and_snd_message(BT_HF_AT_RESPONSE, str_msg.length(),
                   PROTO_ENC_DEC, str_msg );
  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         clcc_response
 *
 * Description      response for CLCC command
 *                  Can be iteratively called for each call index. Call index
 *                  of 0 will be treated as NULL termination (Completes
 *                  response)
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
bt_status_t HeadsetInterface::ClccResponse(int index, bthf_call_direction_t dir,
                                 bthf_call_state_t state, bthf_call_mode_t mode,
                                 bthf_call_mpty_type_t mpty, const char* number,
                                 bthf_call_addrtype_t type,
                                 RawAddress* bd_addr) {
  ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
  std::string str_msg;
  ss_ClccResponse _ss_ClccResponse;
  _ss_ClccResponse.set_index(index);
  _ss_ClccResponse.set_dir((ss_bthf_call_direction_t)dir);
  _ss_ClccResponse.set_state((ss_bthf_call_state_t)state);
  _ss_ClccResponse.set_mode((ss_bthf_call_mode_t)mode);
  _ss_ClccResponse.set_mpty((ss_bthf_call_mpty_type_t)mpty);
  if (number != NULL) {
    _ss_ClccResponse.set_number(number);
    ALOGI("%s: number %s", __func__, number);
  } else {
    _ss_ClccResponse.set_number("");
  }
  _ss_ClccResponse.set_type((ss_bthf_call_addrtype_t)type);
  _ss_ClccResponse.set_bd_addr(ToRawString(bd_addr));
  _ss_ClccResponse.SerializeToString(&str_msg);

  bthf_bld_and_snd_message(BT_HF_CLCC_RESPONSE, str_msg.length(),
                   PROTO_ENC_DEC, str_msg );

  return BT_STATUS_SUCCESS;
}
/*******************************************************************************
 *
 * Function         PhoneStateChange
 *
 * Description      notify of a call state change
 *                  number & type: valid only for incoming & waiting call
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/

bt_status_t HeadsetInterface::PhoneStateChange(
    int num_active, int num_held, bthf_call_state_t call_setup_state,
    const char* number, bthf_call_addrtype_t type, const char* name,
    RawAddress* bd_addr) {
  ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
  std::string str_msg;
  ss_PhoneStateChange _ss_PhoneStateChange;
  _ss_PhoneStateChange.set_num_active(num_active);
  _ss_PhoneStateChange.set_num_held(num_held);
  _ss_PhoneStateChange.set_call_setup_state((ss_bthf_call_state_t) call_setup_state);
  if (number != NULL) {
    _ss_PhoneStateChange.set_number(number);
    ALOGI("%s: number %s", __func__, number);
  } else {
    _ss_PhoneStateChange.set_number("");
  }
  _ss_PhoneStateChange.set_type((ss_bthf_call_addrtype_t) type);
  if (name != NULL) {
    _ss_PhoneStateChange.set_name(name);
    ALOGI("%s: name %s", __func__, name);
  } else {
    _ss_PhoneStateChange.set_name("");
  }
  ALOGI("%s: active %d held %d state %d type %d ", __func__,_ss_PhoneStateChange.num_active(), 
  _ss_PhoneStateChange.num_held(), _ss_PhoneStateChange.call_setup_state(), _ss_PhoneStateChange.type());
  _ss_PhoneStateChange.set_bd_addr(ToRawString(bd_addr));
  _ss_PhoneStateChange.SerializeToString(&str_msg);

  bthf_bld_and_snd_message(BT_HF_PHONE_STATE_CHANGE, str_msg.length(),
                   PROTO_ENC_DEC, str_msg );

  return BT_STATUS_SUCCESS;
}

bt_status_t HeadsetInterface::SetScoOffloadEnabled(bool value) {
    return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         Cleanup
 *
 * Description      Closes the HF interface
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
void HeadsetInterface::Cleanup(void) {
  ALOGI("%s", __func__);
  std::string str_msg;
  bthf_bld_and_snd_message(BT_HF_CLEANUP, str_msg.length(),
                   PROTO_NONE, str_msg );

  if (agBTSSInterface != NULL) {
     ALOGI("%s: deregistering Headset profile callback", __func__);
     agBTSSInterface->deregisterCallbacks(BT_PROFILE_HANDSFREE_ID);
     agBTSSInterface = NULL;
  }

  if (bt_hf_callbacks) {
    ALOGI("%s: setting call backs to NULL",__func__);
    bt_hf_callbacks = NULL;
  }
}

bt_status_t HeadsetInterface::SetActiveDevice(RawAddress* active_device_addr) {
  ALOGI("%s", __func__);
  return BT_STATUS_SUCCESS;
}

bt_status_t HeadsetInterface::SendBsir(bool value, RawAddress* bd_addr) {
  ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
  std::string str_msg;
  ss_SendBsir _ss_SendBsir;
  _ss_SendBsir.set_bd_addr(ToRawString(bd_addr));
  _ss_SendBsir.SerializeToString(&str_msg);

  bthf_bld_and_snd_message(BT_HF_SEND_BSIR, str_msg.length(),
                   PROTO_ENC_DEC, str_msg );

  return BT_STATUS_SUCCESS;
}

bt_status_t btif_hf_check_if_sco_connected() {
  ALOGI("%s", __func__);
  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         GetInterface
 *
 * Description      Get the hf callback interface
 *
 * Returns          (HeadsetInterface)Interface*
 *
 ******************************************************************************/
Interface* GetInterface() {
  VLOG(0) << __func__;
  return HeadsetInterface::GetInstance();
}

/*******************************************************************************
 *
 * Function         btif_hf_ss_callback
 *
 * Description      validate msg id and parse the callback info to jni
 *
 * Returns
 *
 ******************************************************************************/
void btif_hf_ss_callback(uint16_t event, char* p_param) {
  ALOGI("%s", __func__);
  std::string resBufferString;
  tBTIF_SS_Cback* cb_data = (tBTIF_SS_Cback*) p_param;
  uint16_t msg_id = cb_data->payload[0] + (((int)(cb_data->payload[1]))<<8);
  uint16_t length = cb_data->payload[2] + (((int)(cb_data->payload[3]))<<8);
  uint16_t proto_enc = 0;
  if( length > 0) {
    proto_enc = cb_data->payload[4] + (((int)(cb_data->payload[5]))<<8);
    resBufferString.assign((char *)((cb_data->payload)+ MSG_PROTO_OFFSET), length);
    free (cb_data->payload);
  }
  ALOGI("Sending signal on Conditional variable from HF");
  agBTSSInterface->setIsSignalSent(true);
  pthread_mutex_lock(&BluetoothSSInterface::ss_cback_mutex);
  pthread_cond_signal(&BluetoothSSInterface::ss_cback_cond_var);
  pthread_mutex_unlock(&BluetoothSSInterface::ss_cback_mutex);
  ALOGI("[%s]::msg_id is :: %X , Proto length: %d and Proto Encoded Value %d",__func__,
         msg_id, length, proto_enc);
  switch (event) {
    case BT_HF_CONN_STATE_CB : {
       ss_ConnectionStateCallback connectionStateCb;
       connectionStateCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)connectionStateCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] connectionStateCb address: %s",__func__, bd_addr->ToString().c_str());
       ALOGI("[%s] connectionStateCb state:%d",__func__, connectionStateCb.state());
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, ConnectionStateCallback,
            (bthf_connection_state_t) connectionStateCb.state(),
                    bd_addr);

       break;
      }
    case BT_HF_AUDIO_STATE_CB : {
       ss_AudioStateCallback audioStateCb;
       audioStateCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)audioStateCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] audioStateCb address: %s",__func__, bd_addr->ToString().c_str());
       ALOGI("[%s] audioStateCb state:%d",__func__, audioStateCb.state());
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, AudioStateCallback,
            (bthf_audio_state_t) audioStateCb.state(),
                    bd_addr);
       break;
      }
    case BT_HF_VOICE_RECOGNITION_CB : {
       ss_VoiceRecognitionCallback voiceRecogCb;
       voiceRecogCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)voiceRecogCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] voiceRecogCb address: %s",__func__, bd_addr->ToString().c_str());
       ALOGI("[%s] voiceRecogCb state:%d",__func__, voiceRecogCb.state());
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, VoiceRecognitionCallback,
            (bthf_vr_state_t) voiceRecogCb.state(),
                    bd_addr);
       break;
      }
    case BT_HF_ANSWER_CALL_CB : {
       ss_AnswerCallCallback ansCallCb;
       ansCallCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)ansCallCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] ansCallCb address: %s",__func__, bd_addr->ToString().c_str());
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, AnswerCallCallback, bd_addr);
       break;
      }
    case BT_HF_HANGUP_CALL_CB : {
       ss_HangupCallCallback hangupCallCb;
       hangupCallCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)hangupCallCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] hangupCallCb address: %s",__func__, bd_addr->ToString().c_str());
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, HangupCallCallback, bd_addr);
       break;
      }
    case BT_HF_VOL_CONTROL_CB : {
       ss_VolumeControlCallback volControlCb;
       volControlCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)volControlCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] volControlCb address: %s",__func__, bd_addr->ToString().c_str());
       ALOGI("[%s] volControlCb type:%d volume:%d",__func__, volControlCb.type(),
               volControlCb.volume() );
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, VolumeControlCallback,
            (bthf_volume_type_t) volControlCb.type(),
            (int) volControlCb.volume(),
                    bd_addr);
       break;
      }
    case BT_HF_DIAL_CALL_CB : {
       ss_DialCallCallback dialCallCb;
       dialCallCb.ParseFromString(resBufferString);
       char *number = (char *)(dialCallCb.number()).c_str();
       uint8_t* addr = (uint8_t*)dialCallCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] dialCallCb address: %s",__func__, bd_addr->ToString().c_str());
       ALOGI("[%s] dialCallCb number:%s",__func__, number);
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, DialCallCallback, number,
                    bd_addr);
       break;
      }
    case BT_HF_DTMF_CMD_CB : {
       ss_DtmfCmdCallback dtmfCmdCb;
       dtmfCmdCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)dtmfCmdCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] dtmfCmdCb address: %s",__func__, bd_addr->ToString().c_str());
       ALOGI("[%s] dtmfCmdCb tone:%d",__func__, dtmfCmdCb.tone());
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, DtmfCmdCallback,
            (int) dtmfCmdCb.tone(),
                    bd_addr);
       break;
      }
    case BT_HF_NOISE_REDUCTION_CB : {
       ss_NoiseReductionCallback noiceReductionCb;
       noiceReductionCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)noiceReductionCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] noiceReductionCb address: %s",__func__, bd_addr->ToString().c_str());
       ALOGI("[%s] noiceReductionCb nrec:%d",__func__, noiceReductionCb.nrec());
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, NoiseReductionCallback,
            (bthf_nrec_t) noiceReductionCb.nrec(),
                    bd_addr);

       break;
      }
    case BT_HF_WBS_CB : {
       ss_WbsCallback wbsCb;
       wbsCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)wbsCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] wbsCb address: %s",__func__, bd_addr->ToString().c_str());
       ALOGI("[%s] wbsCb wbs:%d",__func__, wbsCb.wbs());
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, WbsCallback,
            (bthf_wbs_config_t) wbsCb.wbs(),
                    bd_addr);
       break;
      }
    case BT_HF_AT_CHLD_CB : {
       ss_AtChldCallback atChldCb;
       atChldCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)atChldCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] atChldCb address: %s",__func__, bd_addr->ToString().c_str());
       ALOGI("[%s] atChldCb chld:%d",__func__, atChldCb.chld());
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, AtChldCallback,
            (bthf_chld_type_t) atChldCb.chld(),
                    bd_addr);
       break;
      }
    case BT_HF_AT_CNUM_CB : {
       ss_AtCnumCallback atCnumCb;
       atCnumCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)atCnumCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] atCnumCb address: %s",__func__, bd_addr->ToString().c_str());
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, AtCnumCallback, bd_addr);
       break;
      }
    case BT_HF_AT_CIND_CB : {
       ss_AtCindCallback atCindCb;
       atCindCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)atCindCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] atCindCb address: %s",__func__, bd_addr->ToString().c_str());
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, AtCindCallback, bd_addr);
       break;
      }
    case BT_HF_AT_COPS_CB : {
       ss_AtCopsCallback atCopsCb;
       atCopsCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)atCopsCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] atCopsCb address: %s",__func__, bd_addr->ToString().c_str());
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, AtCopsCallback, bd_addr);
       break;
      }
    case BT_HF_AT_CLCC_CB : {
       ss_AtClccCallback atClccCb;
       atClccCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)atClccCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] atClccCb address: %s",__func__, bd_addr->ToString().c_str());
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, AtClccCallback, bd_addr);
       break;
      }
    case BT_HF_UNKNOWN_AT_CB : {
       ss_UnknownAtCallback unknownAtCb;
       unknownAtCb.ParseFromString(resBufferString);
       char *at_string = (char *)(unknownAtCb.at_string()).c_str();
       uint8_t* addr = (uint8_t*)unknownAtCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] unknownAtCb address: %s",__func__, bd_addr->ToString().c_str());
       ALOGI("[%s] unknownAtCb at_string:%s",__func__, at_string);
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, UnknownAtCallback, at_string,
                    bd_addr);

       break;
      }
    case BT_HF_KEY_PRESSED_CB : {
       ss_KeyPressedCallback keyPressedCb;
       keyPressedCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)keyPressedCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] keyPressedCb address: %s",__func__, bd_addr->ToString().c_str());
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, KeyPressedCallback, bd_addr);
       break;
      }
    case BT_HF_AT_BIND_CB : {
       ss_AtBindCallback atBindCb;
       atBindCb.ParseFromString(resBufferString);
       char *at_string = (char *)(atBindCb.at_string()).c_str();
       uint8_t* addr = (uint8_t*)atBindCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] atBindCb address: %s",__func__, bd_addr->ToString().c_str());
       ALOGI("[%s] atBindCb at_string:%s",__func__, at_string);
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, AtBindCallback, at_string,
                    bd_addr);

       break;
      }
    case BT_HF_AT_BIEV_CB : {
       ss_AtBievCallback atBievCb;
       atBievCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)atBievCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] atBievCb address: %s",__func__, bd_addr->ToString().c_str());
       ALOGI("[%s] atBievCb ind_id:%d ind_value:%d ",__func__, atBievCb.ind_id(), atBievCb.ind_value());
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, AtBievCallback,
            (bthf_hf_ind_type_t) atBievCb.ind_id(),
            (int) atBievCb.ind_value(),
                    bd_addr);
       break;
      }
    case BT_HF_AT_BIA_CB : {
       ss_AtBiaCallback atBiaCb;
       atBiaCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)atBiaCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       ALOGI("[%s] atBiaCb address: %s",__func__, bd_addr->ToString().c_str());
       ALOGI("[%s] atBiaCb service:%d roam:%d signal:%d battery:%d",__func__,
             atBiaCb.service(), atBiaCb.roam(), atBiaCb.signal(),
             atBiaCb.battery());
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_HF_CBACK(bt_hf_callbacks, AtBiaCallback, atBiaCb.service(),
                    atBiaCb.roam(), atBiaCb.signal(), atBiaCb.battery(),
                    bd_addr);

       break;
      }
    case BT_HF_PEER_FEAT_CB : {
        ss_PeerFeatCallback peerFeatCb;
        peerFeatCb.ParseFromString(resBufferString);
        uint8_t* addr = (uint8_t*)peerFeatCb.bd_addr().c_str();
        RawAddress *bd_addr = (RawAddress*)addr;
        ALOGI("[%s] peerFeatCb address: %s feat %d",__func__, bd_addr->ToString().c_str(), peerFeatCb.peer_feat());
        if (!is_valid_bd_addr(bd_addr)) return;

        btif_hf_peer_feat = peerFeatCb.peer_feat();


      break;
      }
    default: {
       ALOGI("[%s]:: msg id %X :: unknow",__func__, msg_id);
       break;
      }
   }
}

}  // namespace headset
}  // namespace bluetooth
