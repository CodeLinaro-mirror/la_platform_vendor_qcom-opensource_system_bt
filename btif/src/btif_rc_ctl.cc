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
/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*
 ​​​​​Changes from Qualcomm Innovation Center are provided under the following license:
 Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 SPDX-License-Identifier: BSD-3-Clause-Clear
*/
/*****************************************************************************
 *
 *  Filename:      btif_rc_ctl.cc
 *
 *  Description:   Bluetooth AVRC controller implementation
 *
 *****************************************************************************/
#define LOG_TAG "bt_btif_avrc_ctl"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <hardware/bluetooth.h>
//#include "raw_address.h"
#include <hardware/bt_rc_ext.h>

#include "protobuf/proto/avrcp_tg.pb.h"
#include <log/log.h>

#include "btif_common.h"

#ifdef SS_STUB_ENABLED
#include "btif_ss_stub_interface.h"
#else
#include "btif_ss_interface.h"
#endif

#include "btif/protobuf/include/proto_message_ids.h"

#define AVRC_UID_SIZE 8
#define AVRC_ITEM_PLAYER 0x01
#define AVRC_ITEM_FOLDER 0x02
#define AVRC_ITEM_MEDIA 0x03


#define HAL_CTRL_CBACK(P_CB, P_CBACK, ...)                \
  do {                                                  \
    if (P_CB != NULL ) {                                \
      ALOGE("HAL %s->%s", #P_CB, #P_CBACK);             \
      (P_CB)->P_CBACK(__VA_ARGS__);                     \
    } else {                                            \
      ALOGI("### ASSERT :%s Callback is NULL", __func__);  \
    }                                                   \
  } while (0)

static btrc_ctrl_callbacks_t* bt_rc_ctrl_callbacks = NULL;

#ifdef SS_STUB_ENABLED
static BluetoothSSStubInterface* btRcSsInterface = NULL;
#else
static BluetoothSSInterface* btRcSsInterface = NULL;
#endif

/*******************************************************************************
 *
 * Function        btrc_ctrl_bld_and_snd_msg
 *
 * Description     builds ss message
 *
 * Returns
 *
 ******************************************************************************/
static inline void btrc_ctrl_bld_and_snd_msg(uint16_t msgid, uint16_t len, uint16_t mode,
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
  if (btRcSsInterface != NULL) {
     btRcSsInterface->postTxMsg(msgStr);
  } else {
     ALOGE("%s ss interface is null",__func__);
  }

}

/*******************************************************************************
 *
 * Function         init_ctrl
 *
 * Description      Initializes the AVRC interface
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
static bt_status_t init_ctrl(btrc_ctrl_callbacks_t* callbacks) {

  ALOGI("%s", __func__);
  if(btRcSsInterface == NULL){
#ifdef SS_STUB_ENABLED
    btRcSsInterface = BluetoothSSStubInterface::getInstance();
#else
    btRcSsInterface = BluetoothSSInterface::getInstance();
#endif
    if (btRcSsInterface == NULL) {
      ALOGI("%s single stack interface Initialization failed",__func__);
    }
  } else {
    ALOGI("single stack interface is already created");
  }

  std::string str_msg;
  ss_ctrl_init _ss_ctrl_init;
  _ss_ctrl_init.SerializeToString(&str_msg);
  bt_rc_ctrl_callbacks = callbacks;
  btrc_ctrl_bld_and_snd_msg(BT_RC_CTRL_INIT, str_msg.length(), PROTO_NONE, str_msg);

  return BT_STATUS_SUCCESS;
}


/***************************************************************************
 *
 * Function         send_passthrough_cmd
 *
 * Description      Send Pass-Through command
 *
 * Returns          void
 *
 **************************************************************************/
static bt_status_t send_passthrough_cmd(RawAddress* bd_addr, uint8_t key_code,
                                        uint8_t key_state) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_send_pass_through_cmd _ss_send_pass_through_cmd;
  _ss_send_pass_through_cmd.set_bd_addr(ToRawString(bd_addr));
  _ss_send_pass_through_cmd.set_key_code(key_code);
  _ss_send_pass_through_cmd.set_key_state(key_state);
  _ss_send_pass_through_cmd.SerializeToString(&str_msg);

  btrc_ctrl_bld_and_snd_msg(BT_RC_CTRL_SEND_PASS_THRU_CMD,str_msg.length(),
                    PROTO_ENC_DEC, str_msg );

  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         send_groupnavigation_cmd
 *
 * Description      Send Pass-Through command
 *
 * Returns          void
 *
 **************************************************************************/
static bt_status_t send_groupnavigation_cmd(RawAddress* bd_addr,
                                            uint8_t key_code,
                                            uint8_t key_state) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_send_group_navigation_cmd _ss_send_group_navigation_cmd;
  _ss_send_group_navigation_cmd.set_bd_addr(ToRawString(bd_addr));
  _ss_send_group_navigation_cmd.set_key_code(key_code);
  _ss_send_group_navigation_cmd.set_key_state(key_state);
  _ss_send_group_navigation_cmd.SerializeToString(&str_msg);

  btrc_ctrl_bld_and_snd_msg(BT_RC_CTRL_SEND_GROUP_NAV_CMD,str_msg.length(),
                    PROTO_ENC_DEC, str_msg );
  return BT_STATUS_SUCCESS;
}


/***************************************************************************
 *
 * Function         change_player_app_setting
 *
 * Description      Set current values of Player Attributes
 *
 * Returns          void
 *
 **************************************************************************/
static bt_status_t change_player_app_setting(RawAddress* bd_addr,
                                             uint8_t num_attrib,
                                             uint8_t* attrib_ids,
                                             uint8_t* attrib_vals) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_set_player_app_setting_cmd _ss_set_player_app_setting_cmd;
  _ss_set_player_app_setting_cmd.set_bd_addr(ToRawString(bd_addr));
  _ss_set_player_app_setting_cmd.set_num_attrib(num_attrib);
  _ss_set_player_app_setting_cmd.set_attrib_ids(attrib_ids, num_attrib);
  _ss_set_player_app_setting_cmd.set_attrib_vals(attrib_vals, num_attrib);
  _ss_set_player_app_setting_cmd.SerializeToString(&str_msg);

  btrc_ctrl_bld_and_snd_msg(BT_RC_CTRL_SET_PLAYER_APP_SETTING_CMD,str_msg.length(),
                    PROTO_ENC_DEC, str_msg );
  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         play_item_cmd
 *
 * Description      Play the item specified by UID & scope
 *
 * Returns          void
 *
 **************************************************************************/
static bt_status_t play_item_cmd(RawAddress* bd_addr, uint8_t scope,
                                 uint8_t* uid, uint16_t uid_counter) {

  ALOGI("%s", __func__);
  std::string str_msg;
  ss_play_item_cmd _ss_play_item_cmd;
  _ss_play_item_cmd.set_bd_addr(ToRawString(bd_addr));
  _ss_play_item_cmd.set_scope(scope);
  _ss_play_item_cmd.set_uid(uid, AVRC_UID_SIZE);
  _ss_play_item_cmd.set_uid_counter(uid_counter);
  _ss_play_item_cmd.SerializeToString(&str_msg);

  btrc_ctrl_bld_and_snd_msg(BT_RC_CTRL_PLAY_ITEM_CMD, str_msg.length(),
                    PROTO_ENC_DEC, str_msg );
  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         get_playback_state_cmd
 *
 * Description      Fetch the current playback state for the device
 *
 * Returns          BT_STATUS_SUCCESS if command issued successfully otherwise
 *                  BT_STATUS_FAIL.
 *
 **************************************************************************/
static bt_status_t get_playback_state_cmd(RawAddress* bd_addr) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_get_playback_state_cmd _ss_get_playback_state_cmd;
  _ss_get_playback_state_cmd.set_bd_addr(ToRawString(bd_addr));
  _ss_get_playback_state_cmd.SerializeToString(&str_msg);

  btrc_ctrl_bld_and_snd_msg(BT_RC_CTRL_GET_PLAYBACK_STATE_CMD, str_msg.length(),
                    PROTO_ENC_DEC, str_msg );
  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         get_now_playing_list_cmd
 *
 * Description      Fetch the now playing list
 *
 * Paramters        start_item: First item to fetch (0 to fetch from beganning)
 *                  end_item: Last item to fetch (0xff to fetch until end)
 *
 * Returns          BT_STATUS_SUCCESS if command issued successfully otherwise
 *                  BT_STATUS_FAIL.
 *
 **************************************************************************/
static bt_status_t get_now_playing_list_cmd(RawAddress* bd_addr,
                                            uint8_t start_item,
                                            uint8_t num_items) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_get_now_playing_list_cmd _ss_get_now_playing_list_cmd;
  _ss_get_now_playing_list_cmd.set_bd_addr(ToRawString(bd_addr));
  _ss_get_now_playing_list_cmd.set_start(start_item);
  _ss_get_now_playing_list_cmd.set_items(num_items);
  _ss_get_now_playing_list_cmd.SerializeToString(&str_msg);

  btrc_ctrl_bld_and_snd_msg(BT_RC_CTRL_GET_NOW_PLAYING_LIST_CMD, str_msg.length(),
                    PROTO_ENC_DEC, str_msg );

  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         get_folder_list_cmd
 *
 * Description      Fetch the currently selected folder list
 *
 * Paramters        start_item: First item to fetch (0 to fetch from beganning)
 *                  end_item: Last item to fetch (0xff to fetch until end)
 *
 * Returns          BT_STATUS_SUCCESS if command issued successfully otherwise
 *                  BT_STATUS_FAIL.
 *
 **************************************************************************/
static bt_status_t get_folder_list_cmd(RawAddress* bd_addr, uint8_t start_item,
                                       uint8_t num_items) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_get_folder_list_cmd _ss_get_folder_list_cmd;
  _ss_get_folder_list_cmd.set_bd_addr(ToRawString(bd_addr));
  _ss_get_folder_list_cmd.set_start(start_item);
  _ss_get_folder_list_cmd.set_items(num_items);
  _ss_get_folder_list_cmd.SerializeToString(&str_msg);

  btrc_ctrl_bld_and_snd_msg(BT_RC_CTRL_GET_FOLDER_LIST_CMD, str_msg.length(),
                    PROTO_ENC_DEC, str_msg );

  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         get_player_list_cmd
 *
 * Description      Fetch the player list
 *
 * Paramters        start_item: First item to fetch (0 to fetch from beganning)
 *                  end_item: Last item to fetch (0xff to fetch until end)
 *
 * Returns          BT_STATUS_SUCCESS if command issued successfully otherwise
 *                  BT_STATUS_FAIL.
 *
 **************************************************************************/
static bt_status_t get_player_list_cmd(RawAddress* bd_addr, uint8_t start_item,
                                       uint8_t num_items) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_get_player_list_cmd _ss_get_player_list_cmd;
  _ss_get_player_list_cmd.set_bd_addr(ToRawString(bd_addr));
  _ss_get_player_list_cmd.set_start(start_item);
  _ss_get_player_list_cmd.set_items(num_items);
  _ss_get_player_list_cmd.SerializeToString(&str_msg);

  btrc_ctrl_bld_and_snd_msg(BT_RC_CTRL_GET_PLAYER_LIST_CMD, str_msg.length(),
                    PROTO_ENC_DEC, str_msg );
  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         change_folder_path_cmd
 *
 * Description      Change the folder.
 *
 * Paramters        direction: Direction (Up/Down) to change folder
 *                  uid: The UID of folder to move to
 *                  start_item: First item to fetch (0 to fetch from beganning)
 *                  end_item: Last item to fetch (0xff to fetch until end)
 *
 * Returns          BT_STATUS_SUCCESS if command issued successfully otherwise
 *                  BT_STATUS_FAIL.
 *
 **************************************************************************/
static bt_status_t change_folder_path_cmd(RawAddress* bd_addr,
                                          uint8_t direction, uint8_t* uid) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_change_folder_path_cmd _ss_change_folder_path_cmd;
  _ss_change_folder_path_cmd.set_bd_addr(ToRawString(bd_addr));
  _ss_change_folder_path_cmd.set_direction(direction);
  _ss_change_folder_path_cmd.set_uid(uid, AVRC_UID_SIZE);
  _ss_change_folder_path_cmd.SerializeToString(&str_msg);

  btrc_ctrl_bld_and_snd_msg(BT_RC_CTRL_CHANGE_FOLDER_PATH_CMD, str_msg.length(),
                    PROTO_ENC_DEC, str_msg );
  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         set_browsed_player_cmd
 *
 * Description      Change the browsed player.
 *
 * Paramters        id: The UID of player to move to
 *
 * Returns          BT_STATUS_SUCCESS if command issued successfully otherwise
 *                  BT_STATUS_FAIL.
 *
 **************************************************************************/
static bt_status_t set_browsed_player_cmd(RawAddress* bd_addr, uint16_t id) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_set_browsed_player_cmd _ss_set_browsed_player_cmd;
  _ss_set_browsed_player_cmd.set_bd_addr(ToRawString(bd_addr));
  _ss_set_browsed_player_cmd.set_player_id(id);
  _ss_set_browsed_player_cmd.SerializeToString(&str_msg);

  btrc_ctrl_bld_and_snd_msg(BT_RC_CTRL_SET_BROWSED_PLAYER_CMD, str_msg.length(),
                    PROTO_ENC_DEC, str_msg );
  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 **
 ** Function         set_addressed_player_cmd
 **
 ** Description      Change the addressed player.
 **
 ** Paramters        id: The UID of player to move to
 **
 ** Returns          BT_STATUS_SUCCESS if command issued successfully otherwise
 **                  BT_STATUS_FAIL.
 **
 ***************************************************************************/
static bt_status_t set_addressed_player_cmd(RawAddress* bd_addr, uint16_t id) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_set_addressed_player_cmd _ss_set_addressed_player_cmd;
  _ss_set_addressed_player_cmd.set_bd_addr(ToRawString(bd_addr));
  _ss_set_addressed_player_cmd.set_player_id(id);
  _ss_set_addressed_player_cmd.SerializeToString(&str_msg);

  btrc_ctrl_bld_and_snd_msg(BT_RC_CTRL_SET_ADDRESSES_PLAYER_CMD, str_msg.length(),
                    PROTO_ENC_DEC, str_msg );
  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         set_volume_rsp
 *
 * Description      Rsp for SetAbsoluteVolume Command
 *
 * Returns          void
 *
 **************************************************************************/
static bt_status_t set_volume_rsp(RawAddress* bd_addr, uint8_t abs_vol,
                                  uint8_t label) {
  ALOGI("%s abs_vol %d label %d", __func__, abs_vol, label);
  std::string str_msg;
  ss_set_volume_rsp _ss_set_volume_rsp;
  _ss_set_volume_rsp.set_bd_addr(ToRawString(bd_addr));
  _ss_set_volume_rsp.set_abs_vol(abs_vol);
  _ss_set_volume_rsp.set_label(label);
  _ss_set_volume_rsp.SerializeToString(&str_msg);

  btrc_ctrl_bld_and_snd_msg(BT_RC_CTRL_SET_VOLUME_RSP, str_msg.length(),
                    PROTO_ENC_DEC, str_msg );
  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         send_register_abs_vol_rsp
 *
 * Description      Rsp for Notification of Absolute Volume
 *
 * Returns          void
 *
 **************************************************************************/
static bt_status_t volume_change_notification_rsp(
    RawAddress* bd_addr, btrc_notification_type_t rsp_type, uint8_t abs_vol,
    uint8_t label) {

  ALOGI("%s", __func__);
  std::string str_msg;
  ss_register_abs_vol_rsp _ss_register_abs_vol_rsp;
  _ss_register_abs_vol_rsp.set_bd_addr(ToRawString(bd_addr));
  _ss_register_abs_vol_rsp.set_rsp_type((ss_btrc_notification_type_t) rsp_type);
  _ss_register_abs_vol_rsp.set_abs_vol(abs_vol);
  _ss_register_abs_vol_rsp.set_label(label);
  _ss_register_abs_vol_rsp.SerializeToString(&str_msg);

  btrc_ctrl_bld_and_snd_msg(BT_RC_CTRL_REGISTER_ABS_VOL_RSP, str_msg.length(),
                    PROTO_ENC_DEC, str_msg );
  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         cleanup_ctrl
 *
 * Description      Closes the AVRC Controller interface
 *
 * Returns          void
 *
 **************************************************************************/
static void cleanup_ctrl() {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_ctrl_cleanup _ss_ctrl_cleanup;
  _ss_ctrl_cleanup.SerializeToString(&str_msg);
  btrc_ctrl_bld_and_snd_msg(BT_RC_CTRL_CLEANUP, str_msg.length(),
                            PROTO_NONE, str_msg);
  if(btRcSsInterface != NULL) {
    ALOGI("%s Deregistering btRcSsInterface callback ", __func__);
    btRcSsInterface->deregisterCallbacks(BT_PROFILE_AV_RC_CTRL_ID);
    btRcSsInterface = NULL;
  }
  if(bt_rc_ctrl_callbacks) {
    ALOGI("%s: setting call backs to NULL", __func__);
    bt_rc_ctrl_callbacks = NULL;
  }
}



static const btrc_ctrl_interface_t bt_rc_ctrl_interface = {
    sizeof(bt_rc_ctrl_interface),
    init_ctrl,
    send_passthrough_cmd,
    send_groupnavigation_cmd,
    change_player_app_setting,
    play_item_cmd,
    get_playback_state_cmd,
    get_now_playing_list_cmd,
    get_folder_list_cmd,
    get_player_list_cmd,
    change_folder_path_cmd,
    set_browsed_player_cmd,
    set_addressed_player_cmd,
    set_volume_rsp,
    volume_change_notification_rsp,
    cleanup_ctrl,
};


/*******************************************************************************
 *
 * Function         btif_rc_ctrl_get_interface
 *
 * Description      Get the AVRCP Controller callback interface
 *
 * Returns          btrc_ctrl_interface_t
 *
 ******************************************************************************/
const btrc_ctrl_interface_t* btif_rc_ctrl_get_interface(void) {
  BTIF_TRACE_EVENT("%s: ", __func__);
  return &bt_rc_ctrl_interface;
}


/*******************************************************************************
 *
 * Function         btif_rc_ctrl_ss_callback
 *
 * Description      validate msg id and parse the callback info to jni
 *
 * Returns
 *
 ******************************************************************************/
void btif_rc_ctrl_ss_callback(uint16_t event, char* p_param) {
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
  ALOGI("Sending signal on Conditional variable from RC CTRL");
  btRcSsInterface->setIsSignalSent(true);
  pthread_mutex_lock(&BluetoothSSInterface::ss_cback_mutex);
  pthread_cond_signal(&BluetoothSSInterface::ss_cback_cond_var);
  pthread_mutex_unlock(&BluetoothSSInterface::ss_cback_mutex);
  ALOGI("[%s]::msg_id is :: %X , Proto length: %d and Proto Encoded Value %d",__func__,
         msg_id, length, proto_enc);
  switch (event) {
    case BT_RC_CTRL_PASS_THRU_RSP_CB : {
        ALOGI("BT_RC_CTRL_PASS_THRU_RSP_CB ");
        ss_btrc_passthrough_rsp_callback passThruRspCb;
        passThruRspCb.ParseFromString(resBufferString);
        uint8_t* addr = (uint8_t*)passThruRspCb.bd_addr().c_str();
        RawAddress *bd_addr = (RawAddress*)addr;
        if (!is_valid_bd_addr(bd_addr)) return;

        HAL_CTRL_CBACK(bt_rc_ctrl_callbacks, passthrough_rsp_cb, bd_addr,
                      passThruRspCb.id(), passThruRspCb.key_state());

        break;
       }
    case BT_RC_CTRL_GROUP_NAV_RSP_CB : {
        ss_btrc_groupnavigation_rsp_callback grpNavRspCb;
        grpNavRspCb.ParseFromString(resBufferString);

        HAL_CTRL_CBACK(bt_rc_ctrl_callbacks, groupnavigation_rsp_cb,
                      grpNavRspCb.id(), grpNavRspCb.key_state());

        break;
       }
    case BT_RC_CTRL_CONN_STATE_CB : {
        ALOGI("BT_RC_CTRL_CONN_STATE_CB ");
        ss_btrc_connection_state_callback connStateCb;
        connStateCb.ParseFromString(resBufferString);
        uint8_t* addr = (uint8_t*)connStateCb.bd_addr().c_str();
        RawAddress *bd_addr = (RawAddress*)addr;
        ALOGI("[%s] address: %s",__func__, bd_addr->ToString().c_str());
        if (!is_valid_bd_addr(bd_addr)) return;

        HAL_CTRL_CBACK(bt_rc_ctrl_callbacks, connection_state_cb,
                  connStateCb.rc_connect(), connStateCb.bt_connect(),
                        bd_addr);
        break;
       }
    case BT_RC_CTRL_GET_RC_FEAT_CB : {
        ALOGI("BT_RC_CTRL_GET_RC_FEAT_CB ");
        ss_btrc_ctrl_getrcfeatures_callback getRcFeatCb;
        getRcFeatCb.ParseFromString(resBufferString);
        uint8_t* addr = (uint8_t*)getRcFeatCb.bd_addr().c_str();
        RawAddress *bd_addr = (RawAddress*)addr;
        if (!is_valid_bd_addr(bd_addr)) return;

        HAL_CTRL_CBACK(bt_rc_ctrl_callbacks, getrcfeatures_cb, bd_addr,
                       getRcFeatCb.features());
        break;
       }
    case BT_RC_CTRL_SET_PLAYER_APP_SETTING_RSP_CB : {
       ss_btrc_ctrl_setplayerapplicationsetting_rsp_callback setPlayerAppSettingRspCb;
       setPlayerAppSettingRspCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)setPlayerAppSettingRspCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_CTRL_CBACK(bt_rc_ctrl_callbacks, setplayerappsetting_rsp_cb, bd_addr,
                       setPlayerAppSettingRspCb.accepted());
        break;
      }
    case BT_RC_CTRL_PLAYER_APPLICATION_SETTING_CB : {
       ss_btrc_ctrl_playerapplicationsetting_callback playerAppSettingCb;
       playerAppSettingCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)playerAppSettingCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       if (!is_valid_bd_addr(bd_addr)) return;

       int attr_len = playerAppSettingCb.app_attrs_size();
       btrc_player_app_attr_t app_attrs[attr_len];
       //btrc_player_app_ext_attr_t ext_attrs;
       //TODO:
       //ss_btrc_player_app_attr_t ss_attr[playerAppSettingCb.app_attrs_size()];
       for (int i=0; i<attr_len; i++) {
         ss_btrc_player_app_attr_t ss_attrs = playerAppSettingCb.app_attrs(i);
         app_attrs[i].attr_id = (uint8_t)ss_attrs.attr_id();
         app_attrs[i].num_val = (uint8_t) ss_attrs.num_val();
         memcpy(app_attrs[i].attr_val, (uint8_t *) ss_attrs.attr_val().c_str(),
         ss_attrs.attr_val().length());
       }
       /*ss_btrc_player_app_ext_attr_t ss_ext_attrs = playerAppSettingCb.ext_attrs(0);
       ext_attrs.attr_id = (uint8_t) ss_ext_attrs.attr_id();
       ext_attrs.charset_id = (uint16_t) ss_ext_attrs.charset_id();
       ext_attrs.str_len = (uint16_t) ss_ext_attrs.str_len();
       ext_attrs.num_val = (uint8_t) ss_ext_attrs.num_val();*/
       HAL_CTRL_CBACK(bt_rc_ctrl_callbacks, playerapplicationsetting_cb, bd_addr,
                      (uint8_t)playerAppSettingCb.num_attr(), app_attrs, 0, NULL);

        break;
       }
    case BT_RC_CTRL_PLAYER_APPLICATION_SETTING_CHANGED_CB : {
       ss_btrc_ctrl_playerapplicationsetting_changed_callback playerAppSettingChangedCb;
       btrc_player_settings_t player_settings;
       playerAppSettingChangedCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)playerAppSettingChangedCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       if (!is_valid_bd_addr(bd_addr)) return;

       ss_btrc_player_settings_t settings = playerAppSettingChangedCb.p_vals(0);
       player_settings.num_attr = (uint8_t)settings.num_attr();

       memcpy(player_settings.attr_ids, (uint8_t *)settings.attr_ids().c_str(),
          settings.attr_ids().length() );
       memcpy(player_settings.attr_values, (uint8_t *)settings.attr_values().c_str(),
          settings.attr_values().length() );

       HAL_CTRL_CBACK(bt_rc_ctrl_callbacks, playerapplicationsetting_changed_cb, bd_addr,
                      &player_settings);

        break;
       }
    case BT_RC_CTRL_SET_ABSVOl_CMD_CB : {
       ALOGI("BT_RC_CTRL_SET_ABSVOl_CMD_CB ");
       ss_btrc_ctrl_setabsvol_cmd_callback setAbsVolCmdCb;
       setAbsVolCmdCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)setAbsVolCmdCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_CTRL_CBACK(bt_rc_ctrl_callbacks, setabsvol_cmd_cb, bd_addr,
                      setAbsVolCmdCb.abs_vol(), setAbsVolCmdCb.label());
        break;
       }
    case BT_RC_CTRL_REGISTER_NOTIFICATION_ABSVOL_CB : {
       ALOGI("BT_RC_CTRL_REGISTER_NOTIFICATION_ABSVOL_CB ");
       ss_btrc_ctrl_registernotification_abs_vol_callback regNotifAbsVolCb;
       regNotifAbsVolCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)regNotifAbsVolCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_CTRL_CBACK(bt_rc_ctrl_callbacks, registernotification_absvol_cb,
                      bd_addr, regNotifAbsVolCb.label());
        break;
       }
    case BT_RC_CTRL_TRACK_CHANGED_CB : {
       ss_btrc_ctrl_track_changed_callback trackChangedCb;
       trackChangedCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)trackChangedCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       if (!is_valid_bd_addr(bd_addr)) return;

       int attr_len = trackChangedCb.p_attrs_size();
       btrc_element_attr_val_t elem_attr[attr_len];
      //TODO:
       for (int i=0; i< attr_len; i++) {
        const ss_btrc_element_attr_val_t _ss_elem_attr = trackChangedCb.p_attrs(i);
        elem_attr[i].attr_id = _ss_elem_attr.attr_id();
        std::string text = _ss_elem_attr.text();
        memset(elem_attr[i].text, 0, BTRC_MAX_ATTR_STR_LEN);
        memcpy(elem_attr[i].text, text.c_str(), text.length());
       }

       HAL_CTRL_CBACK(bt_rc_ctrl_callbacks, track_changed_cb, bd_addr,
                      trackChangedCb.num_attr(), elem_attr);
        break;
       }
    case BT_RC_CTRL_PLAY_POSITION_CHANGED_CB : {
       ss_btrc_ctrl_play_position_changed_callback playPosChangedCb;
       playPosChangedCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)playPosChangedCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_CTRL_CBACK(bt_rc_ctrl_callbacks, play_position_changed_cb, bd_addr,
                      playPosChangedCb.song_len(), playPosChangedCb.song_pos());
        break;
       }
    case BT_RC_CTRL_PLAY_STATUS_CHANGED_CB: {
       ss_btrc_ctrl_play_status_changed_callback playStatusChangedCb;
       playStatusChangedCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)playStatusChangedCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_CTRL_CBACK(bt_rc_ctrl_callbacks, play_status_changed_cb, bd_addr,
                      (btrc_play_status_t) playStatusChangedCb.play_status());
        break;
       }
    case BT_RC_CTRL_GET_FOLDERS_ITEM_CB : {
       ss_btrc_ctrl_get_folder_items_callback getFolderItemsCb;
       getFolderItemsCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)getFolderItemsCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       if (!is_valid_bd_addr(bd_addr)) return;

       int items_size = getFolderItemsCb.folder_items_size();
       btrc_folder_items_t folder_items[items_size];
       //TODO:
       //if (getFolderItemsCb.folder_items_size()) {
        for (int i=0; i< items_size; i++ ) {
         const ss_btrc_folder_items_t item= getFolderItemsCb.folder_items(i);
         switch (item.item_type() ) {
          case AVRC_ITEM_PLAYER : {
             const ss_btrc_item_player_t play = item.player();
             btrc_item_player_t item_player;
             item_player.player_id = (uint16_t) play.player_id();
             item_player.major_type = (uint8_t) play.major_type();
             item_player.sub_type = (uint32_t) play.sub_type();
             item_player.play_status = (uint8_t) play.play_status();
             item_player.charset_id = (uint16_t) play.charset_id();
             std::string feat = play.features();
             memset(item_player.features, 0, BTRC_FEATURE_BIT_MASK_SIZE);
             memcpy(item_player.features, feat.c_str(), feat.length());
             std::string name = play.name();
             memset(item_player.name, 0, BTRC_MAX_ATTR_STR_LEN);
             memcpy(item_player.name, name.c_str(), name.length());

             folder_items[i].player = item_player;
             break;
          }
          case AVRC_ITEM_FOLDER :{
            const ss_btrc_item_folder_t fol = item.folder();
            btrc_item_folder_t item_folder;
            item_folder.type = (uint8_t)fol.type();
            item_folder.playable = (uint8_t) fol.playable();
            item_folder.charset_id = (uint16_t) fol.charset_id();
            memset(item_folder.uid, 0 , BTRC_UID_SIZE);
            memcpy(item_folder.uid, (uint8_t *)fol.uid().c_str(), fol.uid().length());
            memset(item_folder.name, 0 , BTRC_MAX_ATTR_STR_LEN);
            memcpy(item_folder.name, (uint8_t *)fol.name().c_str(), fol.name().length());

            folder_items[i].folder = item_folder;
            break;

          }
          case AVRC_ITEM_MEDIA : {
            const ss_btrc_item_media_t media = item.media();
            btrc_item_media_t item_media;
            item_media.type = (uint8_t) media.type();
            item_media.charset_id = (uint16_t) media.charset_id();
            memset(item_media.uid, 0 , BTRC_UID_SIZE);
            memcpy(item_media.uid, (uint8_t *)media.uid().c_str(), media.uid().length());
            memset(item_media.name, 0 , BTRC_MAX_ATTR_STR_LEN);
            memcpy(item_media.name, (uint8_t *)media.name().c_str(), media.name().length());
            item_media.num_attrs = (int) media.num_attrs();

            item_media.p_attrs = (btrc_element_attr_val_t*) malloc(
              item_media.num_attrs * sizeof(btrc_element_attr_val_t));

            for(int j = 0; j < item_media.num_attrs ; j++) {
              const ss_btrc_element_attr_val_t elem_attr = media.p_attrs(j);
              item_media.p_attrs[j].attr_id = elem_attr.attr_id();
              memcpy(item_media.p_attrs[j].text,(uint8_t *) elem_attr.text().c_str(),
                       elem_attr.text().length());
            }

            folder_items[i].media = item_media;
            break;
          }
         }
       }
       HAL_CTRL_CBACK(bt_rc_ctrl_callbacks, get_folder_items_cb, bd_addr,
                      (btrc_status_t)getFolderItemsCb.status(), folder_items,
                      getFolderItemsCb.count() );

        break;
      }
    case BT_RC_CTRL_CHANGE_FOLDER_PATH_CB : {
       ss_btrc_ctrl_change_path_callback changePathCb;
       changePathCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)changePathCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_CTRL_CBACK(bt_rc_ctrl_callbacks, change_folder_path_cb, bd_addr,
                      changePathCb.count());
        break;
      }
    case BT_RC_CTRL_SET_BROWSED_PLAYER_CB : {
       ss_btrc_ctrl_set_browsed_player_callback setBrowPlayerCb;
       setBrowPlayerCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)setBrowPlayerCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_CTRL_CBACK(bt_rc_ctrl_callbacks, set_browsed_player_cb, bd_addr,
                      setBrowPlayerCb.num_items(), setBrowPlayerCb.depth());
        break;
      }
    case BT_RC_CTRL_SET_ADDRESSED_PLAYER_CB : {
       ss_btrc_ctrl_set_addressed_player_callback setAddrPlayerCb;
       setAddrPlayerCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)setAddrPlayerCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_CTRL_CBACK(bt_rc_ctrl_callbacks, set_addressed_player_cb, bd_addr,
                      setAddrPlayerCb.status());
        break;
      }
    case BT_RC_CTRL_AVAILABLE_PLAYER_CHANGED_CB : {
       ss_btrc_ctrl_available_player_changed_callback availablePlayerChangedCb;
       availablePlayerChangedCb.ParseFromString(resBufferString);
       uint8_t* addr = (uint8_t*)availablePlayerChangedCb.bd_addr().c_str();
       RawAddress *bd_addr = (RawAddress*)addr;
       if (!is_valid_bd_addr(bd_addr)) return;

       HAL_CTRL_CBACK(bt_rc_ctrl_callbacks, available_player_changed_cb, bd_addr);
        break;
     }
    default: {
       ALOGI("[%s]:: msg id %X :: unknow",__func__, msg_id);
       break;
      }
  }
}
