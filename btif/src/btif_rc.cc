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
 *  Filename:      btif_rc.cc
 *
 *  Description:   Bluetooth AVRC implementation
 *
 *****************************************************************************/

#define LOG_TAG "bt_btif_avrc"

#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <hardware/bluetooth.h>
#include <hardware/bt_rc_ext.h>

#include <hardware/bt_vendor_rc.h>
#include "btif/include/btif_config.h"

#include "log/log.h"

#include "protobuf/proto/avrcp_tg.pb.h"

#include "btif_ss_interface.h"
#include "btif/protobuf/include/proto_message_ids.h"

#include "btif_common.h"

#define BTRC_MAX_APP_SETTINGS       8
#define BTRC_MAX_ATTR_STR_LEN       (1 << 16)
#define AVRC_UID_SIZE 8

BluetoothSSInterface *avrcBTSSInterface = NULL;

#define HAL_AVRC_CBACK(P_CB, P_CBACK, ...)                              \
  do {                                                             \
    if ((P_CB) && (P_CB)->P_CBACK) {                               \
      ALOGE("%s: HAL %s->%s", __func__, #P_CB, #P_CBACK); \
      (P_CB)->P_CBACK(__VA_ARGS__);                                \
    } else {                                                       \
      ASSERTC(0, "Callback is NULL", 0);                           \
    }                                                              \
  } while (0)

/*****************************************************************************
 *  Static variables
 *****************************************************************************/

static btrc_callbacks_t* bt_rc_callbacks = NULL;

int absolute_volume_informed_to_app = 0;

/*****************************************************************************
 *  Functions
 *****************************************************************************/
void btif_avrc_ss_callback(uint16_t event, char* p_param);

/*******************************************************************************
 * Function        btrc_bld_and_snd_message
 *
 * Description     builds ss message
 *
 * Returns
 *
 ******************************************************************************/
static inline void btrc_bld_and_snd_message(uint16_t msgid, uint16_t len, uint16_t mode, 
                                            std::string payload) {
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
  if(avrcBTSSInterface != NULL) {
    avrcBTSSInterface->postTxMsg(msgStr);
  } else {
    ALOGI("%s avrcBTSSInterface is NULL ", __func__);
  }

}


/*******************************************************************************
 *  AVRCP API Functions
 ******************************************************************************/

/*******************************************************************************
 *
 * Function         init
 *
 * Description      Initializes the AVRC interface
 *
 * Returns          bt_status_t
 *
 ******************************************************************************/
static bt_status_t init(btrc_callbacks_t* callbacks, int max_connections) {
  
  ALOGI("%s", __func__);
  std::string str_msg;
  bt_rc_callbacks = callbacks;
  ss_avrcp_tg_init _ss_avrcp_tg_init;
  _ss_avrcp_tg_init.set_max_connections(max_connections);
  _ss_avrcp_tg_init.set_supported_features(BTRC_FEAT_METADATA | BTRC_FEAT_ABSOLUTE_VOLUME);
  _ss_avrcp_tg_init.set_supported_events(BTRC_EVT_PLAY_STATUS_CHANGED |
                                         BTRC_EVT_TRACK_CHANGE | BTRC_EVT_AVAL_PLAYER_CHANGE |
                                         BTRC_EVT_ADDR_PLAYER_CHANGE | BTRC_EVT_UIDS_CHANGED);
  _ss_avrcp_tg_init.SerializeToString(&str_msg);
  if(avrcBTSSInterface == NULL) {
    avrcBTSSInterface = BluetoothSSInterface::getInstance();
    if(avrcBTSSInterface == NULL) {
      ALOGI("%s BluetoothSSInterface::getInstance() FAILED", __func__);
    } else {
      ALOGI("%s BluetoothSSInterface::getInstance() SUCCESS", __func__);
    }
  } else {
    ALOGI("%s avrcBTSSInterface already created", __func__);
  }

  if(avrcBTSSInterface != NULL) {
    ALOGI("%s: Registering AVRC Profile callback with ss_interface", __func__);
    avrcBTSSInterface->registerCallbacks(BT_PROFILE_AV_RC_ID, btif_avrc_ss_callback);
  }

  btrc_bld_and_snd_message(BT_AVRC_INIT, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}


/***************************************************************************
 *
 * Function         get_play_status_rsp
 *
 * Description      Returns the current play status.
 *                      This method is called in response to
 *                      GetPlayStatus request.
 *
 * Returns          bt_status_t
 *
 **************************************************************************/
static bt_status_t get_play_status_rsp(RawAddress* bd_addr,
                                       btrc_play_status_t play_status,
                                       uint32_t song_len, uint32_t song_pos) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_get_play_status_rsp _ss_get_play_status_rsp;
  _ss_get_play_status_rsp.set_bd_addr(ToRawString(bd_addr));
  _ss_get_play_status_rsp.set_play_status((ss_btrc_play_status_t) play_status);
  _ss_get_play_status_rsp.set_song_len(song_len);
  _ss_get_play_status_rsp.set_song_pos(song_pos);
  _ss_get_play_status_rsp.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_GET_PLAY_STATUS_RSP, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

/**************************************************************************
**
** Function         list_player_app_attr_rsp
**
** Description      ListPlayerApplicationSettingAttributes (PDU ID: 0x11)
**                  This method is callled in response to PDU 0x11
**
** Returns          bt_status_t
**
****************************************************************************/
static bt_status_t  list_player_app_attr_rsp(RawAddress* bd_addr, int num_attr, btrc_player_attr_t *p_attrs)
{
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_list_player_app_attr_rsp _ss_list_player_app_attr_rsp;
  _ss_list_player_app_attr_rsp.set_bd_addr(ToRawString(bd_addr));
  _ss_list_player_app_attr_rsp.set_num_attr(num_attr);
  for(int i = 0; i < num_attr; i++) {
    _ss_list_player_app_attr_rsp.add_p_attrs((ss_btrc_player_attr_t) p_attrs[i]);
  }
  _ss_list_player_app_attr_rsp.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_LIST_PLAYER_APP_ATTR_RSP, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

/**********************************************************************
**
** Function list_player_app_value_rsp
**
** Description      ListPlayerApplicationSettingValues (PDU ID: 0x12)
                    This method is called in response to PDU 0x12
************************************************************************/
static bt_status_t  list_player_app_value_rsp(RawAddress* bd_addr, int num_val, uint8_t *value)
{
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_list_player_app_value_rsp _ss_list_player_app_value_rsp;
  _ss_list_player_app_value_rsp.set_bd_addr(ToRawString(bd_addr));
  _ss_list_player_app_value_rsp.set_num_val(num_val);
  _ss_list_player_app_value_rsp.set_p_vals(std::to_string(*value));
  _ss_list_player_app_value_rsp.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_LIST_PLAYER_APP_VALUE_RSP, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

/**********************************************************************
**
** Function  get_player_app_value_rsp
**
** Description  This methos is called in response to PDU ID 0x13
**
***********************************************************************/
static bt_status_t get_player_app_value_rsp(RawAddress* bd_addr, btrc_player_settings_t *p_vals)
{
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_get_player_app_value_rsp _ss_get_player_app_value_rsp;
  _ss_get_player_app_value_rsp.set_bd_addr(ToRawString(bd_addr));
  ss_btrc_player_settings_t *holder = _ss_get_player_app_value_rsp.add_p_vals(); 
  holder->set_num_attr(p_vals->num_attr);
  std::string str_holder;
  for(int i = 0; i < BTRC_MAX_APP_SETTINGS; i++){
    str_holder += std::to_string(p_vals->attr_ids[i]);
  }
  holder->set_attr_ids(str_holder);
  str_holder = "";
  for(int i = 0; i < BTRC_MAX_APP_SETTINGS; i++){
    str_holder += std::to_string(p_vals->attr_values[i]);
  }
  holder->set_attr_values(str_holder);

  _ss_get_player_app_value_rsp.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_GET_PLAYER_APP_VALUE_RSP, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

/********************************************************************
**
** Function     set_player_app_value_rsp
**
** Description  This method is called in response to
**              application value
**
** Return       bt_staus_t
**
*******************************************************************/
static bt_status_t set_player_app_value_rsp (RawAddress* bd_addr, btrc_status_t rsp_status)
{
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_set_player_app_value_rsp _ss_set_player_app_value_rsp;
  _ss_set_player_app_value_rsp.set_bd_addr(ToRawString(bd_addr));
  _ss_set_player_app_value_rsp.set_rsp_status((ss_btrc_status_t) rsp_status);
  _ss_set_player_app_value_rsp.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_SET_PLAYER_APP_VALUE_RSP, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

/********************************************************************
**
** Function      get_player_app_attr_text_rsp
**
** Description   This method is called in response to get player
**               applicaton attribute text response
**
**
*******************************************************************/
static bt_status_t get_player_app_attr_text_rsp(RawAddress* bd_addr, int num_attr,
                                                btrc_player_setting_text_t *p_attrs)
{
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_get_player_app_attr_text_rsp _ss_get_player_app_attr_text_rsp;
  _ss_get_player_app_attr_text_rsp.set_bd_addr(ToRawString(bd_addr));
  _ss_get_player_app_attr_text_rsp.set_num_attr(num_attr);
  ss_btrc_player_setting_text_t *holder = _ss_get_player_app_attr_text_rsp.add_p_attrs();
  for(int i = 0; i <  num_attr; i++) {
    holder[i].set_id(p_attrs[i].id);
    std::string holder_str;
    for(int j = 0; j < BTRC_MAX_ATTR_STR_LEN; j++)
      holder_str += p_attrs[i].text[j];
    holder[i].set_text(holder_str);
  }
  _ss_get_player_app_attr_text_rsp.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_GET_PLAYER_APP_ATTR_TEXT_RSP, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

/********************************************************************
**
** Function      get_player_app_value_text_rsp
**
** Description   This method is called in response to Player application
**               value text
**
** Return        bt_status_t
**
*******************************************************************/
static bt_status_t get_player_app_value_text_rsp(RawAddress* bd_addr, int num_attr, btrc_player_setting_text_t *p_attrs)
{
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_get_player_app_value_text_rsp _ss_get_player_app_value_text_rsp;
  _ss_get_player_app_value_text_rsp.set_bd_addr(ToRawString(bd_addr));
  _ss_get_player_app_value_text_rsp.set_num_val(num_attr);
  ss_btrc_player_setting_text_t *holder = _ss_get_player_app_value_text_rsp.add_p_vals();
  for(int i = 0; i <  num_attr; i++) {
    holder[i].set_id(p_attrs[i].id);
    std::string holder_str;
    for(int j = 0; j < BTRC_MAX_ATTR_STR_LEN; j++)
      holder_str += p_attrs[i].text[j];
    holder[i].set_text(holder_str);
  }
  _ss_get_player_app_value_text_rsp.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_GET_PLAYER_APP_VALUE_TEXT_RSP, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}


/***************************************************************************
 *
 * Function         get_element_attr_rsp
 *
 * Description      Returns the current songs' element attributes
 *                      in text.
 *
 * Returns          bt_status_t
 *
 **************************************************************************/
static bt_status_t get_element_attr_rsp(RawAddress* bd_addr, uint8_t num_attr,
                                        btrc_element_attr_val_t* p_attrs) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_get_element_attr_rsp _ss_get_element_attr_rsp;
  _ss_get_element_attr_rsp.set_bd_addr(ToRawString(bd_addr));
  _ss_get_element_attr_rsp.set_num_attr(num_attr);
  ALOGI("get_element_attr_rsp num_attr %d", num_attr);
  for(int i = 0; i < num_attr; i++) {
    ss_btrc_element_attr_val_t *holder = _ss_get_element_attr_rsp.add_p_attrs();
    holder->set_attr_id(p_attrs[i].attr_id);
    ALOGI("ss_btrc_element_attr_val_t attr_id %d", holder->attr_id());
    char *test = (char *)p_attrs[i].text;
    holder->set_text(test, strlen(test));
    ALOGI("ss_btrc_element_attr_val_t text %s len %d", holder->text().c_str(), strlen(holder->text().c_str()));
  }
  _ss_get_element_attr_rsp.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_GET_ELEMENT_ATTR_RSP, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         register_notification_rsp
 *
 * Description      Response to the register notification request.
 *
 * Returns          bt_status_t
 *
 **************************************************************************/
static bt_status_t register_notification_rsp(
    btrc_event_id_t event_id, btrc_notification_type_t type,
    btrc_register_notification_t* p_param,
    RawAddress *bd_addr) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_register_notification_rsp _ss_register_notification_rsp;
  _ss_register_notification_rsp.set_event_id((ss_btrc_event_id_t) event_id);
  _ss_register_notification_rsp.set_type((ss_btrc_notification_type_t) type);
  _ss_register_notification_rsp.set_bd_addr(ToRawString(bd_addr));
  ss_btrc_register_notification_t *holder = new ss_btrc_register_notification_t();
  ALOGI("EVENT ID %d", _ss_register_notification_rsp.event_id());
  ALOGI("NOTIFICATION TYPE %d", _ss_register_notification_rsp.type());
      switch (_ss_register_notification_rsp.event_id()) {
       case BTRC_EVT_PLAY_STATUS_CHANGED: {
        ALOGI("BTRC_EVT_PLAY_STATUS_CHANGED");
        holder->set_play_status((ss_btrc_play_status_t) p_param->play_status);
        ALOGI("PLAY STATUS %d", holder->play_status());
        _ss_register_notification_rsp.set_allocated_p_param(holder);
        break;
       }
      case BTRC_EVT_TRACK_CHANGE: {
        ALOGI("BTRC_EVT_TRACK_CHANGE");
        uint8_t track[BTRC_UID_SIZE];
        for (int uid_idx = 0; uid_idx < BTRC_UID_SIZE; ++uid_idx) {
          track[uid_idx] = (uint8_t) (p_param->track[uid_idx]);
          ALOGI("track[%d]: %d", uid_idx, track[uid_idx]);
        }
        holder->set_track(track, BTRC_UID_SIZE);
        _ss_register_notification_rsp.set_allocated_p_param(holder);
        break;
      }
      case BTRC_EVT_APP_SETTINGS_CHANGED: {
        ALOGI("BTRC_EVT_APP_SETTINGS_CHANGED IGNORE");
        return BT_STATUS_UNHANDLED;
      }
      case BTRC_EVT_PLAY_POS_CHANGED: {
        ALOGI("BTRC_EVT_PLAY_POS_CHANGED IGNORE");
        return BT_STATUS_UNHANDLED;
      }
      case BTRC_EVT_AVAL_PLAYER_CHANGE: {
        ALOGI("BTRC_EVT_AVAL_PLAYER_CHANGE");
        holder->set_play_status((ss_btrc_play_status_t)1);
        ALOGI("BTRC_EVT_AVAL_PLAYER_CHANGE play_status %d", holder->play_status());
        _ss_register_notification_rsp.set_allocated_p_param(holder);
        break;
      }
      case BTRC_EVT_ADDR_PLAYER_CHANGE: {
        ALOGI("BTRC_EVT_ADDR_PLAYER_CHANGE IGNORE");
        return BT_STATUS_UNHANDLED;
      }
      case BTRC_EVT_UIDS_CHANGED: {
        ALOGI("BTRC_EVT_UIDS_CHANGED");
        ss_btrc_uids_changed_t *uids_holder = new ss_btrc_uids_changed_t();
        uids_holder->set_type(1);
        uids_holder->set_uid_counter(p_param->uids_changed.uid_counter);
        holder->set_allocated_uids_changed(uids_holder);
        _ss_register_notification_rsp.set_allocated_p_param(holder);
        break;
      }
      case BTRC_EVT_NOW_PLAYING_CONTENT_CHANGED: {
        ALOGI("BTRC_EVT_NOW_PLAYING_CONTENT_CHANGED IGNORE");
        return BT_STATUS_UNHANDLED;
      }
      case BTRC_EVT_VOL_CHANGED: {
        ALOGI("BTRC_EVT_VOL_CHANGED");
        holder->set_play_status((ss_btrc_play_status_t) 1);
        ALOGI("VOL_CHANGED play_status %d", holder->play_status());
        _ss_register_notification_rsp.set_allocated_p_param(holder);
        break;
      }
      default: {
        BTIF_TRACE_WARNING("%s: Unhandled event ID: 0x%x", __func__, event_id);
        return BT_STATUS_UNHANDLED;
      }
  }
  _ss_register_notification_rsp.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_REGISTER_NOTIFICATION_RSP, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         get_folder_items_list_rsp
 *
 * Description      Returns the list of media items in current folder along with
 *                  requested attributes. This is called in response to
 *                  GetFolderItems request.
 *
 * Returns          bt_status_t
 *                      BT_STATUS_NOT_READY - when RC is not connected.
 *                      BT_STATUS_SUCCESS   - always if RC is connected
 *                      BT_STATUS_UNHANDLED - when rsp is not pending for
 *                                            get_folder_items_list PDU
 *
 **************************************************************************/
static bt_status_t get_folder_items_list_rsp(RawAddress* bd_addr,
                                             btrc_status_t rsp_status,
                                             uint16_t uid_counter,
                                             uint16_t num_items,
                                             btrc_folder_items_t* p_items) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_get_folder_items_list_rsp _ss_get_folder_items_list_rsp;
  _ss_get_folder_items_list_rsp.set_bd_addr(ToRawString(bd_addr));
  _ss_get_folder_items_list_rsp.set_rsp_status((ss_btrc_status_t) rsp_status);
  _ss_get_folder_items_list_rsp.set_uid_counter(uid_counter);
  _ss_get_folder_items_list_rsp.set_num_items(num_items);
  ss_btrc_folder_items_t *holder = _ss_get_folder_items_list_rsp.add_p_items();
  for(int i = 0; i < num_items; i++) {
    holder[i].set_item_type(p_items[i].item_type);
  }
  _ss_get_folder_items_list_rsp.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_GET_FOLDER_ITEMS_LIST, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         set_addressed_player_rsp
 *
 * Description      Response to set the addressed player for specified media
 *                  player based on id in the media player list.
 *
 * Returns          bt_status_t
 *                      BT_STATUS_NOT_READY - when RC is not connected.
 *                      BT_STATUS_SUCCESS   - always if RC is connected
 *
 **************************************************************************/
static bt_status_t set_addressed_player_rsp(RawAddress* bd_addr,
                                            btrc_status_t rsp_status) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_set_addressed_player_rsp _ss_set_addressed_player_rsp;
  _ss_set_addressed_player_rsp.set_bd_addr(ToRawString(bd_addr));
  _ss_set_addressed_player_rsp.set_rsp_status((ss_btrc_status_t) rsp_status);
  _ss_set_addressed_player_rsp.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_SET_ADDRESSED_PLAYER, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         set_browsed_player_rsp
 *
 * Description      Response to set the browsed player command which contains
 *                  current browsed path of the media player. By default,
 *                  current_path = root and folder_depth = 0 for
 *                  every set_browsed_player request.
 *
 * Returns          bt_status_t
 *                      BT_STATUS_NOT_READY - when RC is not connected.
 *                      BT_STATUS_SUCCESS   - if RC is connected and reponse
 *                                            sent successfully
 *                      BT_STATUS_UNHANDLED - when rsp is not pending for
 *                                            set_browsed_player PDU
 *
 **************************************************************************/
static bt_status_t set_browsed_player_rsp(RawAddress* bd_addr,
                                          btrc_status_t rsp_status,
                                          uint32_t num_items,
                                          uint16_t charset_id,
                                          uint8_t folder_depth,
                                          btrc_br_folder_name_t* p_folders) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_set_browsed_player_rsp _ss_set_browsed_player_rsp;
  _ss_set_browsed_player_rsp.set_bd_addr(ToRawString(bd_addr));
  _ss_set_browsed_player_rsp.set_rsp_status((ss_btrc_status_t) rsp_status);
  _ss_set_browsed_player_rsp.set_num_items(num_items);
  _ss_set_browsed_player_rsp.set_charset_id(charset_id);
  _ss_set_browsed_player_rsp.set_folder_depth(folder_depth);
  ss_btrc_br_folder_name_t *holder = _ss_set_browsed_player_rsp.add_p_folders();
  for(uint32_t i = 0; i < num_items; i++){
    holder[i].set_str_len(p_folders[i].str_len);
  }
  _ss_set_browsed_player_rsp.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_SET_BROWSED_PLAYER_RSP, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

/*******************************************************************************
 *
 * Function         change_path_rsp
 *
 * Description      Response to the change path command which
 *                  contains number of items in the changed path.
 *
 * Returns          bt_status_t
 *                      BT_STATUS_NOT_READY - when RC is not connected.
 *                      BT_STATUS_SUCCESS   - always if RC is connected
 *
 **************************************************************************/
static bt_status_t change_path_rsp(RawAddress* bd_addr,
                                   btrc_status_t rsp_status,
                                   uint32_t num_items) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_change_path_rsp _ss_change_path_rsp;
  _ss_change_path_rsp.set_bd_addr(ToRawString(bd_addr));
  _ss_change_path_rsp.set_rsp_status((ss_btrc_status_t) rsp_status);
  _ss_change_path_rsp.set_num_items(num_items);
  _ss_change_path_rsp.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_CHANGE_PATH_RSP, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         search_rsp
 *
 * Description      Response to search a string from media content command.
 *
 * Returns          bt_status_t
 *                      BT_STATUS_NOT_READY - when RC is not connected.
 *                      BT_STATUS_SUCCESS   - always if RC is connected
 *
 **************************************************************************/
static bt_status_t search_rsp(RawAddress* bd_addr, btrc_status_t rsp_status,
                              uint32_t uid_counter, uint32_t num_items) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_search_rsp _ss_search_rsp;
  _ss_search_rsp.set_bd_addr(ToRawString(bd_addr));
  _ss_search_rsp.set_rsp_status((ss_btrc_status_t) rsp_status);
  _ss_search_rsp.set_uid_counter(uid_counter);
  _ss_search_rsp.set_num_items(num_items);
  _ss_search_rsp.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_SEARCH_RSP, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}
/***************************************************************************
 *
 * Function         get_item_attr_rsp
 *
 * Description      Response to the get item's attributes command which
 *                  contains number of attributes and values list in text.
 *
 * Returns          bt_status_t
 *                      BT_STATUS_NOT_READY - when RC is not connected.
 *                      BT_STATUS_SUCCESS   - always if RC is connected
 *
 **************************************************************************/
static bt_status_t get_item_attr_rsp(RawAddress* bd_addr,
                                     btrc_status_t rsp_status, uint8_t num_attr,
                                     btrc_element_attr_val_t* p_attrs) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_get_item_attr_rsp _ss_get_item_attr_rsp;
  _ss_get_item_attr_rsp.set_bd_addr(ToRawString(bd_addr));
  _ss_get_item_attr_rsp.set_rsp_status((ss_btrc_status_t) rsp_status);
  _ss_get_item_attr_rsp.set_num_attr(num_attr);
  ss_btrc_element_attr_val_t *holder = _ss_get_item_attr_rsp.add_p_attrs();
  for(int i = 0; i <  num_attr; i++) {
    holder[i].set_attr_id(p_attrs[i].attr_id);
    std::string holder_str;
    for(int j = 0; j < BTRC_MAX_ATTR_STR_LEN; j++)
      holder_str += p_attrs[i].text[j];
    holder[i].set_text(holder_str);
  }
  _ss_get_item_attr_rsp.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_GET_ITEM_ATTR_RSP, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         add_to_now_playing_rsp
 *
 * Description      Response to command for adding speciafied media item
 *                  to Now Playing queue.
 *
 * Returns          bt_status_t
 *                      BT_STATUS_NOT_READY - when RC is not connected.
 *                      BT_STATUS_SUCCESS   - always if RC is connected
 *
 **************************************************************************/
static bt_status_t add_to_now_playing_rsp(RawAddress* bd_addr,
                                          btrc_status_t rsp_status) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_add_to_now_playing_rsp _ss_add_to_now_playing_rsp;
  _ss_add_to_now_playing_rsp.set_bd_addr(ToRawString(bd_addr));
  _ss_add_to_now_playing_rsp.set_rsp_status((ss_btrc_status_t) rsp_status);
  _ss_add_to_now_playing_rsp.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_ADD_TO_NOW_PLAYING_RSP, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         play_item_rsp
 *
 * Description      Response to command for playing the specified media item.
 *
 * Returns          bt_status_t
 *                      BT_STATUS_NOT_READY - when RC is not connected.
 *                      BT_STATUS_SUCCESS   - always if RC is connected
 *
 **************************************************************************/
static bt_status_t play_item_rsp(RawAddress* bd_addr,
                                 btrc_status_t rsp_status) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_play_item_rsp _ss_play_item_rsp;
  _ss_play_item_rsp.set_bd_addr(ToRawString(bd_addr));
  _ss_play_item_rsp.set_rsp_status((ss_btrc_status_t) rsp_status);
  _ss_play_item_rsp.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_PLAY_ITEM_RSP, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         get_total_num_of_items_rsp
 *
 * Description      response to command to get the Number of Items
 *                  in the selected folder at the selected scope
 *
 * Returns          bt_status_t
 *                      BT_STATUS_NOT_READY - when RC is not connected.
 *                      BT_STATUS_SUCCESS   - always if RC is connected
 *
 **************************************************************************/
static bt_status_t get_total_num_of_items_rsp(RawAddress* bd_addr,
                                              btrc_status_t rsp_status,
                                              uint32_t uid_counter,
                                              uint32_t num_items) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_get_total_num_of_items_rsp _ss_get_total_num_of_items_rsp;
  _ss_get_total_num_of_items_rsp.set_bd_addr(ToRawString(bd_addr));
  _ss_get_total_num_of_items_rsp.set_rsp_status((ss_btrc_status_t) rsp_status);
  _ss_get_total_num_of_items_rsp.set_uid_counter(uid_counter);
  _ss_get_total_num_of_items_rsp.set_num_items(num_items);
  _ss_get_total_num_of_items_rsp.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_GET_TOTAL_NUM_OF_ITEMS_RSP, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

/***************************************************************************
 *
 * Function         set_volume
 *
 * Description      Send current volume setting to remote side.
 *                  Support limited to SetAbsoluteVolume
 *                  This can be enhanced to support Relative Volume (AVRCP 1.0).
 *                  With RelateVolume, we will send VOLUME_UP/VOLUME_DOWN
 *                  as opposed to absolute volume level
 * volume: Should be in the range 0-127. bit7 is reseved and cannot be set
 *
 * Returns          bt_status_t
 *
 **************************************************************************/
static bt_status_t set_volume(uint8_t volume, RawAddress*bd_addr) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_set_volume _ss_set_volume;
  _ss_set_volume.set_bd_addr(ToRawString(bd_addr));
  _ss_set_volume.set_volume(volume);
  ALOGI("%s volume %d", __func__, volume);
  _ss_set_volume.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_SET_VOLUME, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}


/***************************************************************************
 *
 * Function         cleanup
 *
 * Description      Closes the AVRC interface
 *
 * Returns          void
 *
 **************************************************************************/
static void cleanup() {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_avrcp_tg_cleanup _ss_avrcp_tg_cleanup;
  _ss_avrcp_tg_cleanup.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_CLEANUP, str_msg.length(), PROTO_ENC_DEC, str_msg);  
  if(avrcBTSSInterface != NULL) {
     ALOGI("%s Deregistering avrcBTSSInterface callback ", __func__);
    avrcBTSSInterface->deregisterCallbacks(BT_PROFILE_AV_RC_ID);
    avrcBTSSInterface = NULL;
  }
  if(bt_rc_callbacks) {
    ALOGI("%s: setting call backs to NULL", __func__);
    bt_rc_callbacks = NULL;
  }
}

/**********************************************************************
 *
 * Function        is_device_active_in_handoff
 *
 * Description     Check if this is the active device during hand-off
 *                 If the multicast is disabled when connected to more
 *                 than one device and the active playing device is
 *                 different or device to start playback is different
 *                 then fail this condition.
 * Return          BT_STATUS_SUCCESS if active BT_STATUS_FAIL otherwise
 *
 ********************************************************************/
static bt_status_t is_device_active_in_handoff(RawAddress *bd_addr) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_is_device_active_in_handoff _ss_is_device_active_in_handoff;
  _ss_is_device_active_in_handoff.set_bd_addr(ToRawString(bd_addr));
  _ss_is_device_active_in_handoff.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_IS_DEVICE_ACTIVE_IN_HANDOFF, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;

}

static bt_status_t update_play_status_to_stack(btrc_play_status_t play_state) {
  ALOGI("%s", __func__);
  std::string str_msg;
  ss_update_play_status_to_stack _ss_update_play_status_to_stack;
  _ss_update_play_status_to_stack.set_play_status((ss_btrc_play_status_t) play_state);
  _ss_update_play_status_to_stack.SerializeToString(&str_msg);
  btrc_bld_and_snd_message(BT_AVRC_UPDATE_PLAY_STATUS_TO_STACK, str_msg.length(), PROTO_ENC_DEC, str_msg);
  return BT_STATUS_SUCCESS;
}

static const btrc_interface_t bt_rc_interface = {
    sizeof(bt_rc_interface),
    init,
    get_play_status_rsp,
    list_player_app_attr_rsp,     /* list_player_app_attr_rsp */
    list_player_app_value_rsp,    /* list_player_app_value_rsp */
    get_player_app_value_rsp,     /* get_player_app_value_rsp PDU 0x13*/
    get_player_app_attr_text_rsp, /* get_player_app_attr_text_rsp */
    get_player_app_value_text_rsp,/* get_player_app_value_text_rsp */
    get_element_attr_rsp,
    set_player_app_value_rsp,     /* set_player_app_value_rsp */
    register_notification_rsp,
    set_volume,
    set_addressed_player_rsp,
    set_browsed_player_rsp,
    get_folder_items_list_rsp,
    change_path_rsp,
    get_item_attr_rsp,
    play_item_rsp,
    get_total_num_of_items_rsp,
    search_rsp,
    add_to_now_playing_rsp,
    is_device_active_in_handoff,
    update_play_status_to_stack,
    cleanup,
};

void btif_avrc_ss_callback(uint16_t event, char* p_param) {
  ALOGI("%s", __func__);
  std::string resBufferString;
  tBTIF_SS_Cback* cb_data = (tBTIF_SS_Cback*) p_param;
  uint16_t msg_id = cb_data->payload[0] + (((int)(cb_data->payload[1]))<<8);
  uint16_t length = cb_data->payload[2] + (((int)(cb_data->payload[3]))<<8);
  uint16_t proto_enc = 0;
  if(length > 0) {
    proto_enc = cb_data->payload[4] + (((int)(cb_data->payload[5]))<<8);
    resBufferString.assign((char *)((cb_data->payload)+ MSG_PROTO_OFFSET), length);
    free(cb_data->payload);
  }
  ALOGI("[%s]::msg_id is :: %X , Proto length: %d and Proto Encoded Value %d",__func__,
         msg_id, length, proto_enc);
   switch (event) {
    case BT_AVRC_REMOTE_FEATURES_CB : {
        ALOGI("BT_AVRC_REMOTE_FEATURES_CB ");
        ss_btrc_remote_features_callback _ss_btrc_remote_features_callback;
        _ss_btrc_remote_features_callback.ParseFromString(resBufferString);
        uint8_t* addr = (uint8_t*)_ss_btrc_remote_features_callback.bd_addr().c_str();
        RawAddress *bd_addr = (RawAddress*)addr;
        ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
        ALOGI("%s: features: %d %d", __func__, _ss_btrc_remote_features_callback.has_features(), _ss_btrc_remote_features_callback.features());
        if (!is_valid_bd_addr(bd_addr)) return;

        btrc_remote_features_t holder = (btrc_remote_features_t) BTRC_FEAT_NONE;
        ALOGI("BTRC_FEAT_NONE SUPPORTED");
        int remote_features = _ss_btrc_remote_features_callback.features();
        if(remote_features & BTRC_FEAT_BROWSE) {
          holder = (btrc_remote_features_t) (holder | BTRC_FEAT_BROWSE);
          ALOGI("BTRC_FEAT_BROWSE SUPPORTED");
        }
        if(remote_features & BTRC_FEAT_ABSOLUTE_VOLUME) {
          holder = (btrc_remote_features_t) (holder | BTRC_FEAT_ABSOLUTE_VOLUME);
          ALOGI("BTRC_FEAT_ABSOLUTE_VOLUME SUPPORTED");
        }
        if(remote_features & BTRC_FEAT_METADATA) {
          holder = (btrc_remote_features_t) (holder | BTRC_FEAT_METADATA);
          ALOGI("BTRC_FEAT_METADATA SUPPORTED");
        }
        ALOGI("%s: holder features: %d", __func__, holder);
        HAL_AVRC_CBACK(bt_rc_callbacks, remote_features_cb, bd_addr, holder);
        if(holder & BTRC_FEAT_ABSOLUTE_VOLUME) {
          absolute_volume_informed_to_app = 1;
          ALOGI("REGISTER NOTIFICATION BTRC_EVT_VOL_CHANGED BT_AVRC_REMOTE_FEATURES_CB");
          register_notification_rsp(BTRC_EVT_VOL_CHANGED, BTRC_NOTIFICATION_TYPE_NOTIFY, 0, bd_addr);
        }
        break;
      }
    case BT_AVRC_GET_PLAY_STATUS_CB : {
      ALOGI("BT_AVRC_GET_PLAY_STATUS_CB ");
      ss_btrc_get_play_status_callback _ss_btrc_get_play_status_callback;
      _ss_btrc_get_play_status_callback.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)_ss_btrc_get_play_status_callback.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_AVRC_CBACK(bt_rc_callbacks, get_play_status_cb, bd_addr);
      break;
    }
    case BT_AVRC_LIST_PLAYER_APP_ATTR_CB : {
      ALOGI("BT_AVRC_LIST_PLAYER_APP_ATTR_CB IGNORE");
      break;
    }
    case BT_AVRC_LIST_PLAYER_APP_VALUES_CB : {
      ALOGI("BT_AVRC_LIST_PLAYER_APP_VALUES_CB IGNORE");
      break;
    }
    case BT_AVRC_GET_PLAYER_APP_VALUE_CB : {
      ALOGI("BT_AVRC_GET_PLAYER_APP_VALUE_CB IGNORE");
      break;
    }
    case BT_AVRC_GET_PLAYER_APP_ATTRS_TEXT_CB : {
      ALOGI("BT_AVRC_GET_PLAYER_APP_ATTRS_TEXT_CB IGNORE");
      break;
    }
    case BT_AVRC_GET_PLAYER_APP_VALUES_TEXT_CB : {
      ALOGI("BT_AVRC_GET_PLAYER_APP_VALUES_TEXT_CB IGNORE");
      break;
    }
    case BT_AVRC_SET_PLAYER_APP_VALUE_CB : {
      ALOGI("BT_AVRC_SET_PLAYER_APP_VALUE_CB IGNORE");
      break;
    }
    case BT_AVRC_GET_ELEMENT_ATTR_CB : {
      ALOGI("BT_AVRC_GET_ELEMENT_ATTR_CB");
      ss_btrc_get_element_attr_callback _ss_btrc_get_element_attr_callback;
      _ss_btrc_get_element_attr_callback.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)_ss_btrc_get_element_attr_callback.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
      if (!is_valid_bd_addr(bd_addr)) return;

      int num_attr = _ss_btrc_get_element_attr_callback.num_attr();
      ALOGI("%s: num_attr: %d", __func__, num_attr);
      btrc_media_attr_t data_holder[num_attr];
      int flag = 0;
      for(int i = 0; i < num_attr; i++)  {
          ss_btrc_media_attr_t_msg test_holder = _ss_btrc_get_element_attr_callback.p_attrs(i);
          if(8 == (btrc_media_attr_t) test_holder.attr())
          {
             flag = 1;
             continue;
          }
          data_holder[i] = (btrc_media_attr_t) test_holder.attr();
          ALOGI("%s: i : %d attr : %d", __func__, i, data_holder[i]);
      }
      if(flag == 1) num_attr = num_attr - 1;
      HAL_AVRC_CBACK(bt_rc_callbacks, get_element_attr_cb, num_attr,
                    data_holder, bd_addr);
      break;
    }
    case BT_AVRC_REGISTER_NOTIFICATION_CB : {
      ALOGI("BT_AVRC_REGISTER_NOTIFICATION_CB");
      ss_btrc_register_notification_callback _ss_btrc_register_notification_callback;
      _ss_btrc_register_notification_callback.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)_ss_btrc_register_notification_callback.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
      ALOGI("%d EVENT ID", (btrc_event_id_t)_ss_btrc_register_notification_callback.event_id());
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_AVRC_CBACK(bt_rc_callbacks, register_notification_cb,
                (btrc_event_id_t)_ss_btrc_register_notification_callback.event_id(),
                _ss_btrc_register_notification_callback.param(), bd_addr);
      break;
    }
    case BT_AVRC_VOLUME_CHANGE_CB : {
      ALOGI("BT_AVRC_VOLUME_CHANGE_CB");
      ss_btrc_volume_change_callback _ss_btrc_volume_change_callback;
      _ss_btrc_volume_change_callback.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)_ss_btrc_volume_change_callback.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
      ALOGI("%s: volume: %d", __func__, _ss_btrc_volume_change_callback.volume());
      ALOGI("%s: ctype: %d", __func__, _ss_btrc_volume_change_callback.ctype());
      if (!is_valid_bd_addr(bd_addr)) return;

      if(_ss_btrc_volume_change_callback.ctype() == BTRC_NOTIFICATION_TYPE_CHANGED) {
        ALOGI("REGISTER NOTIFICATION BTRC_EVT_VOL_CHANGED BTRC_NOTIFICATION_TYPE_CHANGED");
        if (absolute_volume_informed_to_app == 0) {
         ALOGI("SET ABSOLUTE VOLUME FEATURE");
         absolute_volume_informed_to_app = 1;
         HAL_AVRC_CBACK(bt_rc_callbacks, remote_features_cb, bd_addr, (btrc_remote_features_t)3);
        }
        register_notification_rsp(BTRC_EVT_VOL_CHANGED, BTRC_NOTIFICATION_TYPE_NOTIFY, 0, bd_addr);
      }
      int app_ctype = 10;
      if(_ss_btrc_volume_change_callback.ctype() == BTRC_NOTIFICATION_TYPE_CHANGED)
        app_ctype = 13;
      else if(_ss_btrc_volume_change_callback.ctype() == BTRC_NOTIFICATION_TYPE_INTERIM)
        app_ctype = 15;
      else if(_ss_btrc_volume_change_callback.ctype() == 9)
        app_ctype = 9;
      HAL_AVRC_CBACK(bt_rc_callbacks, volume_change_cb, _ss_btrc_volume_change_callback.volume(),
                app_ctype, bd_addr);
      break;
    }
    case BT_AVRC_PASSTHROUGH_CMD_CB : {
      ALOGI("BT_AVRC_PASSTHROUGH_CMD_CB");
      ss_btrc_passthrough_cmd_callback _ss_btrc_passthrough_cmd_callback;
      _ss_btrc_passthrough_cmd_callback.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)_ss_btrc_passthrough_cmd_callback.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
      ALOGI("%s: id: %d", __func__, _ss_btrc_passthrough_cmd_callback.id());
      ALOGI("%s: key_state: %d", __func__, _ss_btrc_passthrough_cmd_callback.key_state());
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_AVRC_CBACK(bt_rc_callbacks, passthrough_cmd_cb, _ss_btrc_passthrough_cmd_callback.id(),
                _ss_btrc_passthrough_cmd_callback.key_state(), bd_addr);
      break;
    }
    case BT_AVRC_SET_ADDRESSED_PLAYER_CB : {
      ALOGI("BT_AVRC_SET_ADDRESSED_PLAYER_CB");
      ss_btrc_set_addressed_player_callback _ss_btrc_set_addressed_player_callback;
      _ss_btrc_set_addressed_player_callback.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)_ss_btrc_set_addressed_player_callback.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_AVRC_CBACK(bt_rc_callbacks, set_addressed_player_cb, _ss_btrc_set_addressed_player_callback.player_id(),
                      bd_addr);
      break;
    }
    case BT_AVRC_SET_BROWSED_PLAYER_CB : {
      ALOGI("BT_AVRC_SET_BROWSED_PLAYER_CB");
      ss_btrc_set_browsed_player_callback _ss_btrc_set_browsed_player_callback;
      _ss_btrc_set_browsed_player_callback.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)_ss_btrc_set_browsed_player_callback.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_AVRC_CBACK(bt_rc_callbacks, set_browsed_player_cb, _ss_btrc_set_browsed_player_callback.player_id(),
                      bd_addr);
      break;
    }
    case BT_AVRC_GET_FOLDER_ITEMS_CB : {
      ALOGI("BT_AVRC_GET_FOLDER_ITEMS_CB");
      ss_btrc_get_folder_items_callback _ss_btrc_get_folder_items_callback;
      _ss_btrc_get_folder_items_callback.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)_ss_btrc_get_folder_items_callback.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
      if (!is_valid_bd_addr(bd_addr)) return;

      int num_attr = _ss_btrc_get_folder_items_callback.num_attr();
      unsigned int *data_holder = (unsigned int*) malloc(num_attr * sizeof(unsigned int));
      google::protobuf::RepeatedField<::google::protobuf::uint32> proto_holder = _ss_btrc_get_folder_items_callback.p_attr_ids();
      for(int i = 0; i < num_attr; i++)  {
          data_holder[i] = (int) proto_holder[i];
      }
      HAL_AVRC_CBACK(bt_rc_callbacks, get_folder_items_cb, _ss_btrc_get_folder_items_callback.scope(),
                      _ss_btrc_get_folder_items_callback.start_item(), _ss_btrc_get_folder_items_callback.end_item(),
                      _ss_btrc_get_folder_items_callback.num_attr(), data_holder,
                      bd_addr);
      break;
    }
    case BT_AVRC_CHANGE_PATH_CB : {
      ALOGI("BT_AVRC_CHANGE_PATH_CB");
      ss_btrc_change_path_callback _ss_btrc_change_path_callback;
      _ss_btrc_change_path_callback.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)_ss_btrc_change_path_callback.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
      if (!is_valid_bd_addr(bd_addr)) return;

      std::string proto_holder = _ss_btrc_change_path_callback.folder_uid();
      unsigned char *data_holder = (unsigned char*)malloc(AVRC_UID_SIZE * sizeof(unsigned char));
      for(int i = 0; i < AVRC_UID_SIZE; i++)  {
        data_holder[i] = (unsigned char)proto_holder[i];
      }
      HAL_AVRC_CBACK(bt_rc_callbacks, change_path_cb, _ss_btrc_change_path_callback.direction(),
                      data_holder, bd_addr);
      break;
    }
    case BT_AVRC_GET_ITEM_ATTR_CB : {
      ALOGI("BT_AVRC_GET_ITEM_ATTR_CB");
      ss_btrc_get_item_attr_callback _ss_btrc_get_item_attr_callback;
      _ss_btrc_get_item_attr_callback.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)_ss_btrc_get_item_attr_callback.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
      if (!is_valid_bd_addr(bd_addr)) return;

      int num_attr = _ss_btrc_get_item_attr_callback.num_attr();
      btrc_media_attr_t *data_holder = (btrc_media_attr_t*) malloc(num_attr * sizeof(btrc_media_attr_t));
      ::google::protobuf::RepeatedField<int> proto_holder = _ss_btrc_get_item_attr_callback.p_attrs();
      for(int i = 0; i < num_attr; i++)  {
          data_holder[i] = (btrc_media_attr_t) proto_holder[i];
      }
      std::string uid_proto_holder = _ss_btrc_get_item_attr_callback.uid();
      unsigned char *uid_data_holder = (unsigned char*)malloc(_ss_btrc_get_item_attr_callback.uid_counter() * sizeof(unsigned char));
      for(unsigned int i = 0; i < _ss_btrc_get_item_attr_callback.uid_counter(); i++)  {
          uid_data_holder[i] = (unsigned char) uid_proto_holder[i];
      }
      HAL_AVRC_CBACK(bt_rc_callbacks, get_item_attr_cb, _ss_btrc_get_item_attr_callback.scope(),
                      uid_data_holder, _ss_btrc_get_item_attr_callback.uid_counter(),
                      _ss_btrc_get_item_attr_callback.num_attr(), data_holder,
                      bd_addr);
      break;
    }
    case BT_AVRC_PLAY_ITEM_CB : {
      ALOGI("BT_AVRC_PLAY_ITEM_CB");
      ss_btrc_play_item_callback _ss_btrc_play_item_callback;
      _ss_btrc_play_item_callback.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)_ss_btrc_play_item_callback.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
      if (!is_valid_bd_addr(bd_addr)) return;

      std::string proto_holder = _ss_btrc_play_item_callback.uid();
      unsigned char *data_holder = (unsigned char*)malloc(_ss_btrc_play_item_callback.uid_counter() * sizeof(unsigned char));
      for(unsigned int i = 0; i < _ss_btrc_play_item_callback.uid_counter(); i++)  {
          data_holder[i] = (unsigned char) proto_holder[i];
      }
      HAL_AVRC_CBACK(bt_rc_callbacks, play_item_cb, _ss_btrc_play_item_callback.scope(),
                      _ss_btrc_play_item_callback.uid_counter(), data_holder, bd_addr);
      break;
    }
    case BT_AVRC_GET_TOTAL_NUM_OF_ITEMS_CB : {
      ALOGI("BT_AVRC_GET_TOTAL_NUM_OF_ITEMS_CB");
      ss_btrc_get_total_num_of_items_callback _ss_btrc_get_total_num_of_items_callback;
      _ss_btrc_get_total_num_of_items_callback.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)_ss_btrc_get_total_num_of_items_callback.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
      if (!is_valid_bd_addr(bd_addr)) return;

      HAL_AVRC_CBACK(bt_rc_callbacks, get_total_num_of_items_cb, _ss_btrc_get_total_num_of_items_callback.scope(),
                      bd_addr);
      break;
    }
    case BT_AVRC_SEARCH_CB : {
      ALOGI("BT_AVRC_SEARCH_CB");
      ss_btrc_search_callback _ss_btrc_search_callback;
      _ss_btrc_search_callback.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)_ss_btrc_search_callback.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
      if (!is_valid_bd_addr(bd_addr)) return;

      std::string proto_holder = _ss_btrc_search_callback.p_str();
      unsigned char *data_holder = (unsigned char*)malloc(_ss_btrc_search_callback.str_len() * sizeof(unsigned char));
      for(unsigned int i = 0; i < _ss_btrc_search_callback.str_len(); i++)  {
          data_holder[i] = (unsigned char) proto_holder[i];
      }
      HAL_AVRC_CBACK(bt_rc_callbacks, search_cb, _ss_btrc_search_callback.charset_id(),
                    _ss_btrc_search_callback.str_len(), data_holder, bd_addr);
      break;
    }
    case BT_AVRC_ADD_TO_NOW_PLAYING_CB : {
      ALOGI("BT_AVRC_ADD_TO_NOW_PLAYING_CB");
      ss_btrc_add_to_now_playing_callback _ss_btrc_add_to_now_playing_callback;
      _ss_btrc_add_to_now_playing_callback.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)_ss_btrc_add_to_now_playing_callback.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
      if (!is_valid_bd_addr(bd_addr)) return;

      std::string proto_holder = _ss_btrc_add_to_now_playing_callback.uid();
      unsigned char *data_holder = (unsigned char*)malloc(_ss_btrc_add_to_now_playing_callback.uid_counter() * sizeof(unsigned char));
      for(unsigned int i = 0; i < _ss_btrc_add_to_now_playing_callback.uid_counter(); i++)  {
          data_holder[i] = (unsigned char) proto_holder[i];
      }
      HAL_AVRC_CBACK(bt_rc_callbacks, add_to_now_playing_cb, _ss_btrc_add_to_now_playing_callback.scope(),
                    data_holder, _ss_btrc_add_to_now_playing_callback.uid_counter(),
                    bd_addr);
      break;
    }
    case BT_AVRC_CONNECTION_STATE_CB : {
      ALOGI("BT_AVRC_CONNECTION_STATE_CB");
      ss_btrc_connection_state_callback _ss_btrc_connection_state_callback;
      _ss_btrc_connection_state_callback.ParseFromString(resBufferString);
      uint8_t* addr = (uint8_t*)_ss_btrc_connection_state_callback.bd_addr().c_str();
      RawAddress *bd_addr = (RawAddress*)addr;
      ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
      if (!is_valid_bd_addr(bd_addr)) return;

      if(*bd_addr == RawAddress::kEmpty) {
        ALOGI("bd_addr NULL");
        break;
      }
      absolute_volume_informed_to_app = 0;
      HAL_AVRC_CBACK(bt_rc_callbacks, connection_state_cb, _ss_btrc_connection_state_callback.rc_connect(),
                     _ss_btrc_connection_state_callback.bt_connect(), bd_addr);
      break;
    }
    default:  {
      ALOGI("DEFAULT UNDEFINED");
      break;
    }

   }
}

/*******************************************************************************
 *
 * Function         btif_rc_get_interface
 *
 * Description      Get the AVRCP Target callback interface
 *
 * Returns          btrc_interface_t
 *
 ******************************************************************************/
const btrc_interface_t* btif_rc_get_interface(void) {
  ALOGI("%s", __func__);
  return &bt_rc_interface;
}
