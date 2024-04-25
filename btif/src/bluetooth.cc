/******************************************************************************
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
 *  Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
 *  Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 ******************************************************************************/

/*******************************************************************************
 *
 *  Filename:      bluetooth.c
 *
 *  Description:   Bluetooth HAL implementation
 *
 ******************************************************************************/

#define LOG_TAG "bt_btif"

#include <base/bind.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/callback.h>
#include <cutils/uevent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <hardware/avrcp/avrcp.h>
#include <hardware/bluetooth.h>
#include <hardware/bt_av.h>
#include <hardware/bt_gatt.h>
#include <hardware/bt_hd.h>
#include <hardware/bt_hf.h>
#include <hardware/bt_hearing_aid.h>
#include <hardware/bt_has.h>
#include <hardware/bt_hf_client.h>
#ifdef DIR_FINDING_FEATURE
#include <hardware/bt_atp_locator.h>
#endif
#include <hardware/bt_hh.h>
#include <hardware/bt_pan.h>
#include <hardware/bt_rc_ext.h>
#include <hardware/bt_sdp.h>
#include <hardware/bt_sock.h>
#if (SWB_ENABLED == TRUE)
#include <hardware/vendor_hf.h>
#endif
#include <hardware/vendor.h>
#include <hardware/vendor_socket.h>
#include <hardware/bt_ba.h>
#include <hardware/bt_vendor_rc.h>
#include <openssl/rand.h>
#include "bt_utils.h"
#include "bta_sys.h"
#include "bta/include/bta_has_api.h"
#include "bta/include/bta_hearing_aid_api.h"
#include "bta/include/bta_hf_client_api.h"
#include "btif/include/btif_debug_btsnoop.h"
#include "btif/include/btif_debug_conn.h"
#include "btif_a2dp.h"
#include "btif_hf.h"
#include "btif_api.h"
#include "btif_bqr.h"
#include "btif_config.h"
#include "btif_common.h"
#include "btif_sock.h"
//#include "internal_include/extra_include.h"
#include "device/include/controller.h"
#include "btif_sdp.h"
#include "btif_debug.h"
#include "btif_keystore.h"
#include "btif_storage.h"
#include "device/include/device_iot_config.h"
#include "btsnoop.h"
#include "btsnoop_mem.h"
#include "common/address_obfuscator.h"
#include "common/address_obfuscator.cc"
#include "common/os_utils.h"
#include "device/include/interop.h"
#include "osi/include/alarm.h"
#include "osi/include/allocation_tracker.h"
#include "osi/include/log.h"
#include "osi/include/metrics.h"
#include "osi/include/osi.h"
#include "osi/include/wakelock.h"
#include "stack/gatt/connection_manager.h"
#include "stack_manager.h"
#include "stack_interface.h"
#include <stdarg.h>
#include "btif_uid.h"
#include "btif_tws_plus.h"
#include <string.h>
using base::Bind;
using bluetooth::hearing_aid::HearingAidInterface;
using bluetooth::has::HasClientInterface;
#ifdef DIR_FINDING_FEATURE
using bluetooth::atp_locator::AtpLocatorInterface;
#endif
#include "btif_ss_interface.h"
#ifdef SS_STUB_ENABLED
#include "btif_ss_stub_interface.h"
#endif
#include "protobuf/proto/dm.pb.h"
#include "btif/protobuf/include/proto_message_ids.h"
#include <fcntl.h>
#include<ctime>


#define UEVENT_MSG_LEN 1024
#define BTSS_EVENT "SLATE_EVENT="
#define BTSS_EVENT_STRING_LEN 11
#define MAX_BUFF_SIZE (64)
#define BTSS_INIT_TIMEOUT  (6000)
#define BT_SSR_DUMP_TIMEOUT  (20000)
#define BT_SSR_TIMEOUT  (7000)

// For Dummy Dynamic Audio Buffer Callback
#define CODEC_TYPE_NUMBER 32
#define DEFAULT_BUFFER_TIME (MAX_PCM_FRAME_NUM_PER_TICK * 2)
#define MAXIMUM_BUFFER_TIME (MAX_PCM_FRAME_NUM_PER_TICK * 2)
#define MINIMUM_BUFFER_TIME MAX_PCM_FRAME_NUM_PER_TICK

/*******************************************************************************
 *  Static variables
 ******************************************************************************/

bt_callbacks_t* bt_hal_cbacks = NULL;
bool restricted_mode = false;
bool common_criteria_mode = false;
const int CONFIG_COMPARE_ALL_PASS = 0b11;
int common_criteria_config_compare_result = CONFIG_COMPARE_ALL_PASS;
bool is_local_device_atv = false;
bt_bond_state_t bond_state = BT_BOND_STATE_NONE;
//btif_trace_level = BT_TRACE_LEVEL_DEBUG;
BluetoothSSInterface *btSSInterface;
#ifdef SS_STUB_ENABLED
BluetoothSSStubInterface *btSSStubInterface;
#endif
static uid_set_t* uid_set = NULL;
alarm_t *ssr_dump_timeout;


// Check for a legacy address stored as a property.
static constexpr char PERSIST_BDADDR_PROPERTY[] =
    "persist.vendor.service.bt.ss.bdaddr";
static constexpr char PERSIST_LOGLEVEL_PROPERTY[] =
    "persist.vendor.service.bt.ss.loglevel";
static constexpr char PERSIST_BDNAME_PROPERTY[] =
    "persist.vendor.service.bt.ss.bdname";

#define BTSS_STATE_NODE "/sys/kernel/slate_bt_state/slate_bt_state"
SS_BTSS_State btssCurrentState = SS_BTSS_DOWN;
SS_BTSS_State btssPrevousState = SS_BTSS_DOWN;

// Declaration of thread condition variable
static pthread_cond_t btssEventCond;
// declaring mutex
static pthread_mutex_t btssEventLock;

static constexpr size_t kStringLength = sizeof("XX:XX:XX:XX:XX:XX") - 1;
static constexpr size_t kBytes = (kStringLength + 1) / 3;

std::unique_ptr<std::thread> btss_event_handler_thread;
int btss_event;

using namespace bluetooth::synergy::SynergyProto;

typedef struct {
  RawAddress bd_addr;
  bt_device_type_t dev_type;
  bt_bdname_t bd_name;
  int8_t rssi;
  uint32_t cod;
  bool in_use;
  uint32_t time_of_resp;
} tInqDB_Addr;

tInqDB_Addr btif_inq_db[40];

/*******************************************************************************
 *  Externs
 ******************************************************************************/

/* list all extended interfaces here */

/* handsfree profile - client */
extern bthf_client_interface_t* btif_hf_client_get_interface();
/* advanced audio profile */
extern btav_source_interface_t* btif_av_get_src_interface();
//extern btav_sink_interface_t* btif_av_get_sink_interface();
/*rfc l2cap*/
extern btsock_interface_t* btif_sock_get_interface();
/* hid host profile */
//extern bthh_interface_t* btif_hh_get_interface();
/* hid device profile */
//extern bthd_interface_t* btif_hd_get_interface();
/*pan*/
//extern btpan_interface_t* btif_pan_get_interface();
/* gatt */
extern const btgatt_interface_t* btif_gatt_get_interface();
/* avrc target */
extern btrc_interface_t* btif_rc_get_interface();
/* avrc controller */
extern btrc_interface_t* btif_rc_ctrl_get_interface();
/*SDP search client*/
//extern btsdp_interface_t* btif_sdp_get_interface();

/*Hearing Aid client*/
//extern HearingAidInterface* btif_hearing_aid_get_interface();

/* Hearing Access client */
extern HasClientInterface* btif_has_client_get_interface();

/* List all test interface here */
/* vendor  */
extern btvendor_interface_t *btif_vendor_get_interface();
/* vendor socket*/
extern btvendor_interface_t *btif_vendor_socket_get_interface();
#if (SWB_ENABLED == TRUE)
//extern btvendor_interface_t *btif_vendor_hf_get_interface();
#endif
/* broadcast transmitter */
//extern ba_transmitter_interface_t *btif_bat_get_interface();
//extern btrc_vendor_ctrl_interface_t *btif_rc_vendor_ctrl_get_interface();

#ifdef DIR_FINDING_FEATURE
extern AtpLocatorInterface* btif_atp_locator_get_interface();
#endif

/*******************************************************************************
 *  Functions
 ******************************************************************************/

bool interface_ready(void) { return bt_hal_cbacks != NULL; }

static bool is_profile(const char* p1, const char* p2) {
  //CHECK(p1);
  //CHECK(p2);
  return strlen(p1) == strlen(p2) && strncmp(p1, p2, strlen(p2)) == 0;
}
uint8_t log_level = SS_BT_TRACE_LEVEL_WARNING;
uint8_t appl_trace_level = 6;
uint8_t btif_trace_level = 6;
/*LOG Dummy functions added*/
void LogMsg(uint32_t trace_set_mask, const char* fmt_str, ...) {
  va_list args;
  va_start(args, fmt_str);
  vprintf(fmt_str, args);
  va_end(args);
}

tInqDB_Addr* find_inq_db(const RawAddress& p_bda);
tInqDB_Addr* inq_db_new(const RawAddress& p_bda, bt_bdname_t name, bt_device_type_t devtype, int8_t rssi, uint32_t cod );
void inq_db_clear();


/*****************************************************************************
 *
 *   BLUETOOTH HAL INTERFACE FUNCTIONS
 *
 ****************************************************************************/


const std::vector<std::string> get_allowed_bt_package_name(void);
//void handle_migration(const std::string& dst, const std::vector<std::string>& allowed_bt_package_name);

void bytes_to_string (const uint8_t* addr, char* addr_str) {
  snprintf(addr_str, kStringLength+1, "%02x:%02x:%02x:%02x:%02x:%02x", addr[0], addr[1], addr[2],
          addr[3], addr[4], addr[5]);
}

bool string_to_bytes (const char* addr_str, uint8_t* addr) {
  if (addr_str == NULL) return false;
  if (strnlen(addr_str, kStringLength) != kStringLength) return false;
  unsigned char trailing_char = '\0';

  return (sscanf(addr_str, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx%1c",
                 &addr[0], &addr[1], &addr[2], &addr[3], &addr[4], &addr[5],
                 &trailing_char) == kBytes);
}

//to make address to reverse order to match NAP
void letobd(uint8_t localAddr[6]) {
  int i;
  uint8_t temp;

  for (i = 0; i < 3; i++) {
    temp = localAddr[i];
    localAddr[i] = localAddr[5-i];
    localAddr[5-i] = temp;
  }
}

void  split_address (const char* property, bool* need_write, char* address) {
  std::string prop = property;
  ALOGI("%s: property %s",__func__, prop.c_str());
  std::size_t delimeter = prop.find(" ");
  if (delimeter == std::string::npos) {
    ALOGI("%s: delimeter not found returning",__func__);
    return;
  }
  std::string bool_val = prop.substr(0, delimeter);
  std::string address_val = prop.substr(delimeter + 1);
  std::copy(address_val.begin(), address_val.end(), address);
  if (bool_val.compare("true") == 0) {
    *need_write = true;
  } else {
    *need_write = false;
  }
  return;
}

bool get_local_address (uint8_t *local_addr) {
  char property[PROPERTY_VALUE_MAX] = { 0 };
  char addr_prop[kStringLength + 1];
  bool valid_bda = false;
  bool isWrite = false;

  ALOGD("%s", __func__);
  //Look for a previously stored BDA in property "persist.vendor.service.bt.ss.bdaddr".
  if (!valid_bda && property_get(PERSIST_BDADDR_PROPERTY, property, NULL)) {
    split_address(property, &isWrite, addr_prop);
    if (isWrite && string_to_bytes(addr_prop, local_addr)) {
      valid_bda = true;
      letobd(local_addr);
      goto exit;
    }
    if (!(string_to_bytes(addr_prop, local_addr))) {
      goto generate_address;
    }
    ALOGD("%s: BD address already present in property: Address: %s", __func__, property);
    valid_bda = false;
    goto exit;
  }

generate_address:
  /* Generate new BDA if necessary */
  if (!valid_bda) {
    char bdstr[kStringLength + 1];
    struct timespec cur_time;

    if (-1 == clock_gettime (CLOCK_MONOTONIC, &cur_time))
    {
      ALOGE("%s: clock_gettime failed\n", __func__);
    }

    srand((unsigned int)cur_time.tv_nsec);

    /* No autogen BDA. Generate one now. */
    local_addr[0] = 0x22;
    local_addr[1] = 0x22;
    local_addr[2] = (uint8_t)rand();
    local_addr[3] = (uint8_t)rand();
    local_addr[4] = (uint8_t)rand();
    local_addr[5] = (uint8_t)rand();

    /* Convert to ascii, and store as a persistent property */
    bytes_to_string(local_addr, bdstr);
    std::string prop;
    prop.append("true ");
    prop.append(bdstr);
    ALOGD("%s: No preset BDA! Generating BDA: %s for prop %s:%s", __func__,
          (char*)bdstr, PERSIST_BDADDR_PROPERTY, prop.c_str());
    if (property_set(PERSIST_BDADDR_PROPERTY, prop.c_str()) < 0) {
      ALOGE("%s: Failed to set random BDA in prop %s", __func__,
            PERSIST_BDADDR_PROPERTY);
      valid_bda = false;
    } else {
      valid_bda = true;
      letobd(local_addr);
    }
  }
exit:
  return valid_bda;
}

static int init(bt_callbacks_t* callbacks, bool start_restricted,
                bool is_common_criteria_mode, int config_compare_result,
                const char** init_flags, bool is_atv,
                const char* user_data_directory) {
  char value[PROPERTY_VALUE_MAX] = { 0 };
  property_get(PERSIST_LOGLEVEL_PROPERTY, value, "2");
  if (!strcmp(value, "0")) log_level = 0;
  else if (!strcmp(value, "1")) log_level = 1;
  else if (!strcmp(value, "2")) log_level = 2;
  else if (!strcmp(value, "3")) log_level = 3;
  else if (!strcmp(value, "4")) log_level = 4;
  else if (!strcmp(value, "5")) log_level = 5;
  else if (!strcmp(value, "6")) log_level = 6;
  else if (!strcmp(value, "7")) log_level = 7;
  else log_level = 2;
  ALOGI("QTI Single stack: %s: start restricted = %d : common criteria mode = %d,"
           " config compare result = %d, log_level %d", __func__, start_restricted, is_common_criteria_mode,
           config_compare_result, log_level);

  if (user_data_directory != nullptr) {
    //handle_migration(std::string(user_data_directory),get_allowed_bt_package_name());
  }

  if (interface_ready()) return BT_STATUS_DONE;

  //allocation_tracker_init();
  bt_hal_cbacks = callbacks;
  restricted_mode = start_restricted;
  common_criteria_mode = is_common_criteria_mode;
  common_criteria_config_compare_result = config_compare_result;
  is_local_device_atv = is_atv;
  init_external_interfaces();

  stack_manager_get_interface()->init_stack();
  btif_debug_init();

  read_btss_state();
  init_btss_event_handler();
  ssr_dump_timeout = alarm_new("ssr_dump_alarm");
  pthread_mutex_init(&btssEventLock, NULL);
  pthread_cond_init(&btssEventCond, NULL);

restart:
  if(btssCurrentState != SS_BTSS_UP) {
    int ret = 0;
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    long ns = timeout.tv_nsec + 1000000 * (BTSS_INIT_TIMEOUT%1000);
    timeout.tv_nsec = ns%1000000000;
    timeout.tv_sec += ns/1000000000 + BTSS_INIT_TIMEOUT/1000;
    // acquire a lock
    pthread_mutex_lock(&btssEventLock);
    ALOGW("Waiting for GetBtssState..!!!");
    ret = pthread_cond_timedwait(&btssEventCond, &btssEventLock, &timeout);

    if(ret == ETIMEDOUT) {
      // BTSS INIT Timed out
      ALOGE("BTSS Init timedout--btss state is::%d", btssCurrentState);
      //TODO: Need to do cleanup :timers and threads
      pthread_mutex_unlock(&btssEventLock);
      goto restart;
    }
    else if(ret == 0) {
      // BTSS INIT Successful
      ALOGW("BTSS Ready... Continue to Init");
    }
    else {
      // pthread_cond_timedwait error
      ALOGW("pthread_cond_timedwait error");
      pthread_mutex_unlock(&btssEventLock);
      goto restart;
    }
    // release lock
    pthread_mutex_unlock(&btssEventLock);
  }

  btif_ss_interface_init();
  if(btSSInterface != NULL) {
    ALOGI("%s: registering DM profile callback with ss_interface", __func__);
    btSSInterface->registerCallbacks(BT_PROFILE_DM_ID, btif_dm_ss_callback);
  }
  return BT_STATUS_SUCCESS;
}

static int enable () {
  ALOGI("%s", __func__);
  ALOGD("QTI single stack: %s", __func__);
  // Do Encoding of Enable Proto
  uint8_t enable_msg[MAX_LENGTH_WITH_PROTO_NONE];
  uint8_t addr[6];
  RawAddress *bd_addr;
  std::string protoMsg;
  uint16_t proto_encode = PROTO_NONE;

  //adding msg_id
  uint16_t msg_id = BT_DM_ENABLE;

  enable_msg[0] = msg_id & 0xff;
  enable_msg[1] = (msg_id >> 8);

  if (get_local_address(addr)) {
    char bdstr[kStringLength + 1];
    bd_addr = (RawAddress*)addr;
    ALOGI("%s: Setting BT Address to %s", __func__, bd_addr->ToString().c_str());
    ss_enable _bt_enable;
    _bt_enable.set_bd_addr(ToRawString(bd_addr).c_str());
    _bt_enable.SerializeToString(&protoMsg);
    proto_encode = PROTO_ENC_DEC;
  }
  ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());
  //adding length
  uint16_t length = protoMsg.length();
  enable_msg[2] = length & 0xff;
  enable_msg[3] = (length >> 8);
  //adding proto_encode
  enable_msg[4] = proto_encode & 0xff;
  enable_msg[5] = (proto_encode >> 8);
  char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
  memcpy(resBuffer, (char *) enable_msg, MAX_LENGTH_WITH_PROTO_NONE);
  std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
  msgStr.append(protoMsg);
#ifndef SS_STUB_ENABLED
  btSSInterface->postTxMsg(msgStr);
#else
  btSSStubInterface->postTxMsg(msgStr);
#endif
  return BT_STATUS_SUCCESS;
}

static int disable(void) {
  ALOGI("%s", __func__);
  btif_sock_cleanup();
  uint8_t disable_msg[MAX_LENGTH_WITH_PROTO_NONE];
  //adding msg_id
  uint16_t msg_id = BT_DM_DISABLE;
  disable_msg[0] = msg_id & 0xff;
  disable_msg[1] = (msg_id >> 8);
  //adding length
  uint16_t length = PAYLOAD_LENGTH_WITH_PROTO_NONE;
  disable_msg[2] = length & 0xff;
  disable_msg[3] = (length >> 8);
  //adding proto_encode
  uint16_t proto_encode = PROTO_NONE;
  disable_msg[4] = proto_encode & 0xff;
  disable_msg[5] = (proto_encode >> 8);
  char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
  memcpy(resBuffer, (char *) disable_msg, MAX_LENGTH_WITH_PROTO_NONE);
  std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
#ifndef SS_STUB_ENABLED
  btSSInterface->postTxMsg(msgStr);
#else
  btSSStubInterface->postTxMsg(msgStr);
#endif
  return BT_STATUS_SUCCESS;
}

static bool get_wbs_supported() {
  return false;
}
static bool get_swb_supported() {
  return false;
}
static void cleanup(void) {
  ALOGI("%s", __func__);
  stack_manager_get_interface()->clean_up_stack();
  if (btSSInterface != NULL) {
    btSSInterface->deregisterCallbacks(BT_PROFILE_DM_ID);
  }
  btif_ss_interface_cleanup();
}

static void ssrDumpTimeout(void* data) {
  ALOGI("%s()", __func__);
  if (btSSInterface != NULL) {
    btSSInterface->deregisterCallbacks(BT_PROFILE_DM_ID);
  }
  btif_ss_interface_cleanup();
  ALOGE(" %s : Triggering SSR ", __func__);
  do_in_jni_thread(
    FROM_HERE, Bind(
      []() {
        HAL_CBACK(bt_vendor_callbacks, ssr_vendor_cb);
      }));
}

static void ss_ssr_event_received () {
  ALOGE("%s()", __func__);
  static char path[SS_SOC_DUMP_PATH_BUF_SIZE + 1] = {'\0'};
  char property[PROPERTY_VALUE_MAX] = { 0 };
  time_t cur_t = time(NULL);
  struct tm *cur_tm = localtime(&cur_t);
  if (cur_tm) {
    snprintf(path, SS_SOC_DUMP_PATH_BUF_SIZE, SS_SSR_DUMP_PATH, cur_tm->tm_year + 1900, cur_tm->tm_mon+ 1, cur_tm->tm_mday, cur_tm->tm_hour, cur_tm->tm_min, cur_tm->tm_sec);
  } else {
	ALOGE("cur_time is NULL");
	snprintf(path, SS_SOC_DUMP_PATH_BUF_SIZE, SS_SSR_DUMP_PATH_WITHOUT_TIME);
  }
  ssr_fptr = fopen(path,"a+");
  if (alarm_is_scheduled(ssr_dump_timeout)) {
    ALOGD("%s(): ssr_dump_timeout() scheduled, so cancel it", __func__);
    alarm_cancel(ssr_dump_timeout);
  }
  alarm_set_on_mloop(ssr_dump_timeout, BT_SSR_DUMP_TIMEOUT, ssrDumpTimeout, NULL);
  if (ssr_fptr == NULL) {
    ALOGE("SSR Dump file create failed. Path :: %s", path);
  } else {
    ALOGD("SSR Dump file created in Path :: %s", path);
  }
  if (property_get(PERSIST_BDADDR_PROPERTY, property, NULL)) {
    ALOGD(" %s : Got property %s ", __func__, property);
    char addr_prop[kStringLength + 1];
    bool isWrite;
    split_address(property, &isWrite, addr_prop);
    std::string prop;
    prop.append("true ");
    prop.append(addr_prop);
    if (property_set(PERSIST_BDADDR_PROPERTY, prop.c_str()) < 0) {
      ALOGE("%s: Failed to set random BDA in prop %s", __func__,
        PERSIST_BDADDR_PROPERTY);
    }
  }
}

bool is_restricted_mode() { return restricted_mode; }
bool is_common_criteria_mode() {
  return common_criteria_mode;
}
// if common criteria mode disable, will always return
// CONFIG_COMPARE_ALL_PASS(0b11) indicate don't check config checksum.
int get_common_criteria_config_compare_result() {
  return is_common_criteria_mode() ? common_criteria_config_compare_result
                                   : CONFIG_COMPARE_ALL_PASS;
}

bool is_atv_device() { return is_local_device_atv; }

static int get_adapter_properties(void) {
  ALOGI("%s", __func__);
  uint8_t get_adap_msg[MAX_LENGTH_WITH_PROTO_NONE];
  //adding msg_id
  uint16_t msg_id = BT_DM_GET_ADAPTER_PROPERTIES;
  get_adap_msg[0] = msg_id & 0xff;
  get_adap_msg[1] = (msg_id >> 8);
  //adding length
  uint16_t length = PAYLOAD_LENGTH_WITH_PROTO_NONE;
  get_adap_msg[2] = length & 0xff;
  get_adap_msg[3] = (length >> 8);
  //adding proto_encode
  uint16_t proto_encode = PROTO_NONE;
  get_adap_msg[4] = proto_encode & 0xff;
  get_adap_msg[5] = (proto_encode >> 8);
  char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
  memcpy(resBuffer, (char *) get_adap_msg, MAX_LENGTH_WITH_PROTO_NONE);
  std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
#ifndef SS_STUB_ENABLED
  btSSInterface->postTxMsg(msgStr);
#else
  btSSStubInterface->postTxMsg(msgStr);
#endif
  return BT_STATUS_SUCCESS;
}

void sendDummyAdapterPropertyCallback(uint16_t event, char* p_param) {
  bt_property_t* prop = (bt_property_t*)p_param;
  switch (event) {
    case BT_PROPERTY_DYNAMIC_AUDIO_BUFFER:
      ALOGI("BT_PROPERTY_DYNAMIC_AUDIO_BUFFER");
      HAL_CBACK(bt_hal_cbacks, adapter_properties_cb, BT_STATUS_SUCCESS, 1, prop);
    break;

    default:
      ALOGI("Unhandled Event");
    break;
  }
}

static int get_adapter_property(bt_property_type_t type) {
  ALOGI("%s", __func__);
  if(type == BT_PROPERTY_DYNAMIC_AUDIO_BUFFER){
      // Send Dummy BT_PROPERTY_DYNAMIC_AUDIO_BUFFER adapter property callback
      char buf[512];
      bt_dynamic_audio_buffer_item_t dynamic_audio_buffer_item;
      bt_property_t prop;
      prop.val = (void*)buf;
      prop.type =   BT_PROPERTY_DYNAMIC_AUDIO_BUFFER;
      prop.len = sizeof(bt_dynamic_audio_buffer_item_t);
      for (int i = 0; i < CODEC_TYPE_NUMBER; i++) {
          dynamic_audio_buffer_item.dab_item[i] = {
          .default_buffer_time = DEFAULT_BUFFER_TIME,
          .maximum_buffer_time = MAXIMUM_BUFFER_TIME,
          .minimum_buffer_time = MINIMUM_BUFFER_TIME};
      }
      memcpy(prop.val, &dynamic_audio_buffer_item, prop.len);
      btif_transfer_context(sendDummyAdapterPropertyCallback, BT_PROPERTY_DYNAMIC_AUDIO_BUFFER, (char*)&prop, sizeof(prop), NULL);
      return BT_STATUS_SUCCESS;
  }
  uint8_t get_adaprop_msg[MAX_LENGTH_WITH_PROTO_NONE];
  //adding msg_id
  uint16_t msg_id = BT_DM_GET_ADAPTER_PROPERTY;
  get_adaprop_msg[0] = msg_id & 0xff;
  get_adaprop_msg[1] = (msg_id >> 8);

  std::string protoMsg;
  ss_get_adapter_property _get_adapter_property;
  _get_adapter_property.set_type((ss_bt_property_type_t)type);
  _get_adapter_property.SerializeToString(&protoMsg);
  ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());
  //adding length
  uint16_t length = protoMsg.length();
  get_adaprop_msg[2] = length & 0xff;
  get_adaprop_msg[3] = (length >> 8);
  //adding proto_encode
  uint16_t proto_encode = PROTO_ENC_DEC;
  get_adaprop_msg[4] = proto_encode & 0xff;
  get_adaprop_msg[5] = (proto_encode >> 8);
  char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
  memcpy(resBuffer, (char *) get_adaprop_msg, MAX_LENGTH_WITH_PROTO_NONE);
  std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
  msgStr.append(protoMsg);
#ifndef SS_STUB_ENABLED
  btSSInterface->postTxMsg(msgStr);
#else
  btSSStubInterface->postTxMsg(msgStr);
#endif
  return BT_STATUS_SUCCESS;
}

static int set_adapter_property(const bt_property_t* property) {
  ALOGI("%s", __func__);
  ALOGI("Property type - %d, Property length - %d",property->type, property->len);
  switch (property->type) {
    case BT_PROPERTY_BDNAME: {
      uint16_t name_len = 0;
      char name[PROPERTY_VALUE_MAX];
      name_len = (property->len > PROPERTY_VALUE_MAX) ? PROPERTY_VALUE_MAX : property->len;
      memcpy(name, property->val, name_len);
      name[name_len] = '\0';
      property_set(PERSIST_BDNAME_PROPERTY, (char*)name);
      ALOGI("set property name : %s", (char*)property->val);
    } break;
    case BT_PROPERTY_ADAPTER_SCAN_MODE: {
      bt_scan_mode_t mode = *(bt_scan_mode_t*)property->val;
      ALOGI("set property scan mode : %x", mode);
    } break;
    case SS_BT_PROPERTY_ADAPTER_DISCOVERY_TIMEOUT: {
      uint32_t timeout;
      std::memcpy(&timeout, property->val, sizeof(uint32_t));
      ALOGI("set property discoverable timeout : %d", timeout);
    } break;
    case BT_PROPERTY_BDADDR:
    case BT_PROPERTY_UUIDS:
    case BT_PROPERTY_ADAPTER_BONDED_DEVICES:
    case BT_PROPERTY_REMOTE_FRIENDLY_NAME:
    case BT_PROPERTY_LOCAL_IO_CAPS:
      break;
    default:
      ALOGI("set_adapter_property : invalid property type - %d",property->type);
      break;
  }

  uint8_t set_adaprop_msg[MAX_LENGTH_WITH_PROTO_NONE];
  //adding msg_id
  uint16_t msg_id = BT_DM_SET_ADAPTER_PROPERTY;
  set_adaprop_msg[0] = msg_id & 0xff;
  set_adaprop_msg[1] = (msg_id >> 8);

  std::string protoMsg;
  ss_set_adapter_property _set_adapter_property;
  ss_bt_property_t *btProperty = _set_adapter_property.mutable_property();
  btProperty->set_type((ss_bt_property_type_t)property->type);
  btProperty->set_len(property->len);
  btProperty->set_val((char*)property->val);
  _set_adapter_property.SerializeToString(&protoMsg);
  ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());
  //adding length
  uint16_t length = protoMsg.length();
  set_adaprop_msg[2] = length & 0xff;
  set_adaprop_msg[3] = (length >> 8);
  //adding proto_encode
  uint16_t proto_encode = PROTO_ENC_DEC;
  set_adaprop_msg[4] = proto_encode & 0xff;
  set_adaprop_msg[5] = (proto_encode >> 8);
  char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
  memcpy(resBuffer, (char *) set_adaprop_msg, MAX_LENGTH_WITH_PROTO_NONE);
  std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
  msgStr.append(protoMsg);
#ifndef SS_STUB_ENABLED
  btSSInterface->postTxMsg(msgStr);
#else
  btSSStubInterface->postTxMsg(msgStr);
#endif
  return BT_STATUS_SUCCESS;
}

int get_remote_device_properties(RawAddress* remote_addr) {
  ALOGI("%s", __func__);
  uint8_t get_remprop_msg[MAX_LENGTH_WITH_PROTO_NONE];
  //adding msg_id
  uint16_t msg_id = BT_DM_GET_REMOTE_DEVICE_PROPERTIES;
  get_remprop_msg[0] = msg_id & 0xff;
  get_remprop_msg[1] = (msg_id >> 8);

  std::string protoMsg;
  ss_get_remote_device_properties _get_remote_device_properties;
  _get_remote_device_properties.set_remote_addr(ToRawString(remote_addr).c_str());
  _get_remote_device_properties.SerializeToString(&protoMsg);
  ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());
  //adding length
  uint16_t length = protoMsg.length();
  get_remprop_msg[2] = length & 0xff;
  get_remprop_msg[3] = (length >> 8);
  //adding proto_encode
  uint16_t proto_encode = PROTO_ENC_DEC;
  get_remprop_msg[4] = proto_encode & 0xff;
  get_remprop_msg[5] = (proto_encode >> 8);
  char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
  memcpy(resBuffer, (char *) get_remprop_msg, MAX_LENGTH_WITH_PROTO_NONE);
  std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
  msgStr.append(protoMsg);
#ifndef SS_STUB_ENABLED
  btSSInterface->postTxMsg(msgStr);
#else
  btSSStubInterface->postTxMsg(msgStr);
#endif
  return BT_STATUS_SUCCESS;
}

int get_remote_device_property(RawAddress* remote_addr,
                               bt_property_type_t type) {
  ALOGI("%s", __func__);
  uint8_t get_remprop_msg[MAX_LENGTH_WITH_PROTO_NONE];
  //adding msg_id
  uint16_t msg_id = BT_DM_GET_REMOTE_DEVICE_PROPERTY_BY_TYPE;
  get_remprop_msg[0] = msg_id & 0xff;
  get_remprop_msg[1] = (msg_id >> 8);

  std::string protoMsg;
  ss_get_remote_device_property _get_remote_device_property;
  _get_remote_device_property.set_remote_addr(ToRawString(remote_addr).c_str());
  _get_remote_device_property.set_type((ss_bt_property_type_t)type);
  _get_remote_device_property.SerializeToString(&protoMsg);
  ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());
  //adding length
  uint16_t length = protoMsg.length();
  get_remprop_msg[2] = length & 0xff;
  get_remprop_msg[3] = (length >> 8);
  //adding proto_encode
  uint16_t proto_encode = PROTO_ENC_DEC;
  get_remprop_msg[4] = proto_encode & 0xff;
  get_remprop_msg[5] = (proto_encode >> 8);
  char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
  memcpy(resBuffer, (char *) get_remprop_msg, MAX_LENGTH_WITH_PROTO_NONE);
  std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
  msgStr.append(protoMsg);
#ifndef SS_STUB_ENABLED
  btSSInterface->postTxMsg(msgStr);
#else
  btSSStubInterface->postTxMsg(msgStr);
#endif
  return BT_STATUS_SUCCESS;
}

int set_remote_device_property(RawAddress* remote_addr,
                               const bt_property_t* property) {
  ALOGI("%s", __func__);
  uint8_t set_remprop_msg[MAX_LENGTH_WITH_PROTO_NONE];
  //adding msg_id
  uint16_t msg_id = BT_DM_SET_REMOTE_DEVICE_PROPERTIES;
  set_remprop_msg[0] = msg_id & 0xff;
  set_remprop_msg[1] = (msg_id >> 8);

  std::string protoMsg;
  ss_set_remote_device_property _set_remote_device_property;
  _set_remote_device_property.set_remote_addr(ToRawString(remote_addr).c_str());
  ss_bt_property_t *btProerty = _set_remote_device_property.mutable_property();
  btProerty->set_type((ss_bt_property_type_t)property->type);
  btProerty->set_len(property->len);
  btProerty->set_val((char*)property->val);
  _set_remote_device_property.SerializeToString(&protoMsg);
  ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());
  //adding length
  uint16_t length = protoMsg.length();
  set_remprop_msg[2] = length & 0xff;
  set_remprop_msg[3] = (length >> 8);
  //adding proto_encode
  uint16_t proto_encode = PROTO_ENC_DEC;
  set_remprop_msg[4] = proto_encode & 0xff;
  set_remprop_msg[5] = (proto_encode >> 8);
  char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
  memcpy(resBuffer, (char *) set_remprop_msg, MAX_LENGTH_WITH_PROTO_NONE);
  std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
  msgStr.append(protoMsg);
#ifndef SS_STUB_ENABLED
  btSSInterface->postTxMsg(msgStr);
#else
  btSSStubInterface->postTxMsg(msgStr);
#endif
  return BT_STATUS_SUCCESS;
}

int get_remote_service_record(const RawAddress& remote_addr,
                              const bluetooth::Uuid& uuid) {
  /* sanity check */
  if (interface_ready() == false) return BT_STATUS_NOT_READY;

  return btif_get_remote_service_record(remote_addr, uuid);
}

int get_remote_services(RawAddress* remote_addr, int /*transport*/) {
  /* sanity check */
  if (interface_ready() == false) return BT_STATUS_NOT_READY;

  return btif_dm_get_remote_services_from_app(*remote_addr);
}

static int start_discovery(void) {
  ALOGI("%s", __func__);
  if(bond_state ==  BT_BOND_STATE_BONDING) {
    ALOGI("%s, Device in bonding state, cannot do inquiry", __func__);
    return BT_STATUS_BUSY;
  }
  uint8_t disc_msg[MAX_LENGTH_WITH_PROTO_NONE];
  //adding msg_id
  uint16_t msg_id = BT_DM_START_DISCOVERY;
  disc_msg[0] = msg_id & 0xff;
  disc_msg[1] = (msg_id >> 8);
  //adding length
  uint16_t length = PAYLOAD_LENGTH_WITH_PROTO_NONE;
  disc_msg[2] = length & 0xff;
  disc_msg[3] = (length >> 8);
  //adding proto_encode
  uint16_t proto_encode = PROTO_NONE;
  disc_msg[4] = proto_encode & 0xff;
  disc_msg[5] = (proto_encode >> 8);
  char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
  memcpy(resBuffer, (char *) disc_msg, MAX_LENGTH_WITH_PROTO_NONE);
  std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
  if (btSSInterface != NULL) {
    btSSInterface->ssGlinkWakeLockAcquireOrRelease(true, true);
  }
#ifndef SS_STUB_ENABLED
  btSSInterface->postTxMsg(msgStr);
#else
  btSSStubInterface->postTxMsg(msgStr);
#endif
  return BT_STATUS_SUCCESS;
}

static int cancel_discovery(void) {
  ALOGI("%s", __func__);
  uint8_t cancel_disc_msg[MAX_LENGTH_WITH_PROTO_NONE];
  //adding msg_id
  uint16_t msg_id = BT_DM_CANCEL_DISCOVERY;
  cancel_disc_msg[0] = msg_id & 0xff;
  cancel_disc_msg[1] = (msg_id >> 8);
  //adding length
  uint16_t length = PAYLOAD_LENGTH_WITH_PROTO_NONE;
  cancel_disc_msg[2] = length & 0xff;
  cancel_disc_msg[3] = (length >> 8);
  //adding proto_encode
  uint16_t proto_encode = PROTO_NONE;
  cancel_disc_msg[4] = proto_encode & 0xff;
  cancel_disc_msg[5] = (proto_encode >> 8);
  char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
  memcpy(resBuffer, (char *) cancel_disc_msg, MAX_LENGTH_WITH_PROTO_NONE);
  std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
  if (btSSInterface != NULL) {
    btSSInterface->ssGlinkWakeLockAcquireOrRelease(true, false);
  }
#ifndef SS_STUB_ENABLED
  btSSInterface->postTxMsg(msgStr);
#else
  btSSStubInterface->postTxMsg(msgStr);
#endif
  return BT_STATUS_SUCCESS;
}

static int create_bond(const RawAddress* bd_addr, int transport) {
#if 0
  /* sanity check */
  if (interface_ready() == false) return BT_STATUS_NOT_READY;

  return btif_dm_create_bond(bd_addr, transport);
#endif

  ALOGI("%s ", __func__);
  ALOGI("%s: bd_addr: %s", __func__, bd_addr->ToString().c_str());
  if(bond_state !=  BT_BOND_STATE_NONE) {
    ALOGI("%s, Device busy, one pairing in progress", __func__);
    return BT_STATUS_BUSY;
  }
  uint8_t create_bond_msg[MAX_LENGTH_WITH_PROTO_NONE];

  uint16_t msg_id = BT_DM_CREATE_BOND;
  create_bond_msg[0] = msg_id & 0xFF;
  create_bond_msg[1] = msg_id >> 8;

  std::string protoMsg;
  ss_create_bond _create_bond;
  _create_bond.set_bd_addr(ToRawString(bd_addr).c_str());
  _create_bond.set_transport(transport);
  _create_bond.SerializeToString(&protoMsg);
  ALOGI("%s : protomsg length : %d", __func__,  protoMsg.length());

  uint16_t length = protoMsg.length();
  create_bond_msg[2] = length & 0xFF;
  create_bond_msg[3] = length >> 8;

  uint16_t proto_encode = PROTO_ENC_DEC;
  create_bond_msg[4] = proto_encode & 0xFF;
  create_bond_msg[5] = proto_encode >> 8;

  //char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
  //memcpy(resBuffer, (char *) create_bond_msg, MAX_LENGTH_WITH_PROTO_NONE);
  //std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
  std::string msgStr((char *)create_bond_msg, MAX_LENGTH_WITH_PROTO_NONE);
  msgStr.append(protoMsg);
#ifndef SS_STUB_ENABLED
  btSSInterface->postTxMsg(msgStr);
#else
  btSSStubInterface->postTxMsg(msgStr);
#endif
  return BT_STATUS_SUCCESS;
}

static int create_bond_le(const RawAddress* bd_addr, uint8_t addr_type) {
  if (!interface_ready()) return BT_STATUS_NOT_READY;

  return BT_STATUS_SUCCESS;
}

static int create_bond_out_of_band(const RawAddress* bd_addr, int transport,
                                   const bt_oob_data_t* p192_data,
                                   const bt_oob_data_t* p256_data) {
#if 0
  /* sanity check */
  if (interface_ready() == false) return BT_STATUS_NOT_READY;

  /*do_in_bta_thread(FROM_HERE, base::Bind(&btif_dm_create_bond_out_of_band, bd_addr,
                   transport, *p192_data, *p256_data));*/
  return BT_STATUS_SUCCESS;
#endif

  ALOGI("%s", __func__);
  uint8_t create_bond_out_of_bond_msg[MAX_LENGTH_WITH_PROTO_NONE];

  uint16_t msg_id = BT_DM_CREATE_BOND_OOB;
  create_bond_out_of_bond_msg[0] = msg_id & 0xFF;
  create_bond_out_of_bond_msg[1] = msg_id >> 8;

  std::string protoMsg;

  ss_create_bond_out_of_band _create_bond_out_of_band;
  _create_bond_out_of_band.set_bd_addr(ToRawString(bd_addr).c_str());
  _create_bond_out_of_band.set_transport(transport);
  ss_bt_out_of_band_data_t* _bt_out_of_band_data = _create_bond_out_of_band.mutable_oob_data();
  _bt_out_of_band_data->set_le_bt_dev_addr(ToRawString(bd_addr).c_str());
  _bt_out_of_band_data->set_c192((char *)p192_data->c);
  _bt_out_of_band_data->set_r192((char *)p192_data->r);
  _bt_out_of_band_data->set_c256((char *)p256_data->c);
  _bt_out_of_band_data->set_r256((char *)p256_data->r);
  _bt_out_of_band_data->set_sm_tk((char *)p192_data->sm_tk);
//  _bt_out_of_band_data->set_le_sc_c((uint8_t)(p192_data->le_flags));
//  _bt_out_of_band_data->set_le_sc_r((uint8_t)(p192_data->le_flags));
  _create_bond_out_of_band.SerializeToString(&protoMsg);
  ALOGI("%s : protomsg length : %d", __func__,  protoMsg.length());

  uint16_t length = protoMsg.length();
  create_bond_out_of_bond_msg[2] = length & 0xFF;
  create_bond_out_of_bond_msg[3] = length >> 8;

  uint16_t proto_encode = PROTO_ENC_DEC;
  create_bond_out_of_bond_msg[4] = proto_encode & 0xFF;
  create_bond_out_of_bond_msg[5] = proto_encode >> 8;

  std::string msgStr((char *)create_bond_out_of_bond_msg, MAX_LENGTH_WITH_PROTO_NONE);
  msgStr.append(protoMsg);
#ifndef SS_STUB_ENABLED
  btSSInterface->postTxMsg(msgStr);
#else
  btSSStubInterface->postTxMsg(msgStr);
#endif
  return BT_STATUS_SUCCESS;
}

static int generate_local_oob_data(tBT_TRANSPORT transport) {
  ALOGI("%s", __func__);
  if (!interface_ready()) return BT_STATUS_NOT_READY;

  /*do_in_bta_thread(
      FROM_HERE, Bind(&btif_dm_generate_local_oob_data, transport));*/
  return BT_STATUS_SUCCESS;
}

static int cancel_bond(const RawAddress* bd_addr) {
#if 0
  /* sanity check */
  if (interface_ready() == false) return BT_STATUS_NOT_READY;

  return btif_dm_cancel_bond(bd_addr);
#endif

  ALOGI("%s", __func__);
  uint8_t cancel_bond_msg[MAX_LENGTH_WITH_PROTO_NONE];

  uint16_t msg_id = BT_DM_CANCEL_BOND;
  cancel_bond_msg[0] = msg_id & 0xFF;
  cancel_bond_msg[1] = msg_id >> 8;

  std::string protoMsg;
  ss_cancel_bond _cancel_bond;
  _cancel_bond.set_bd_addr(ToRawString(bd_addr).c_str());
  _cancel_bond.SerializeToString(&protoMsg);
  ALOGI("%s : protomsg length : %d", __func__,  protoMsg.length());

  uint16_t length = protoMsg.length();
  cancel_bond_msg[2] = length & 0xFF;
  cancel_bond_msg[3] = length >> 8;

  uint16_t proto_encode = PROTO_ENC_DEC;
  cancel_bond_msg[4] = proto_encode & 0xFF;
  cancel_bond_msg[5] = proto_encode >> 8;

  std::string msgStr((char *)cancel_bond_msg, MAX_LENGTH_WITH_PROTO_NONE);
  msgStr.append(protoMsg);
#ifndef SS_STUB_ENABLED
  btSSInterface->postTxMsg(msgStr);
#else
  btSSStubInterface->postTxMsg(msgStr);
#endif
  return BT_STATUS_SUCCESS;
}

static int remove_bond(const RawAddress* bd_addr) {
#if 0
  if (is_restricted_mode() && !btif_storage_is_restricted_device(bd_addr))
    return BT_STATUS_SUCCESS;

  /* sanity check */
  if (interface_ready() == false) return BT_STATUS_NOT_READY;

  return btif_dm_remove_bond(bd_addr);
#endif

  ALOGI("%s", __func__);
  uint8_t remove_bond_msg[MAX_LENGTH_WITH_PROTO_NONE];

  uint16_t msg_id = BT_DM_REMOVE_BOND;
  remove_bond_msg[0] = msg_id & 0xFF;
  remove_bond_msg[1] = msg_id >> 8;

  std::string protoMsg;
  ss_remove_bond _remove_bond;
  _remove_bond.set_bd_addr(ToRawString(bd_addr).c_str());
  _remove_bond.SerializeToString(&protoMsg);
  ALOGI("%s : protomsg length : %d", __func__,  protoMsg.length());

  uint16_t length = protoMsg.length();
  remove_bond_msg[2] = length & 0xFF;
  remove_bond_msg[3] = length >> 8;

  uint16_t proto_encode = PROTO_ENC_DEC;
  remove_bond_msg[4] = proto_encode & 0xFF;
  remove_bond_msg[5] = proto_encode >> 8;

  std::string msgStr((char *)remove_bond_msg, MAX_LENGTH_WITH_PROTO_NONE);
  msgStr.append(protoMsg);
#ifndef SS_STUB_ENABLED
  btSSInterface->postTxMsg(msgStr);
#else
  btSSStubInterface->postTxMsg(msgStr);
#endif
  return BT_STATUS_SUCCESS;
}

static int get_connection_state(const RawAddress* bd_addr) {
  /* sanity check */
  if (interface_ready() == false) return 0;

  return btif_dm_get_connection_state(bd_addr);
}

static int pin_reply(const RawAddress* bd_addr, uint8_t accept, uint8_t pin_len,
                     bt_pin_code_t* pin_code) {
#if 0
  /* sanity check */
  if (interface_ready() == false) return BT_STATUS_NOT_READY;
  if (pin_code == nullptr || pin_len > PIN_CODE_LEN) return BT_STATUS_FAIL;

  return btif_dm_pin_reply(bd_addr, accept, pin_len, pin_code);
#endif

  ALOGI("%s", __func__);
  uint8_t pin_reply_msg[MAX_LENGTH_WITH_PROTO_NONE];

  uint16_t msg_id = BT_DM_PIN_REPLY;
  pin_reply_msg[0] = msg_id & 0xFF;
  pin_reply_msg[1] = msg_id >> 8;

  std::string protoMsg;
  ss_pin_reply _pin_reply;
  _pin_reply.set_bd_addr(ToRawString(bd_addr).c_str());
  _pin_reply.set_accept(accept);
  _pin_reply.set_pin_len(pin_len);
  ss_bt_pin_code_t* _bt_pin_code = _pin_reply.mutable_pin_code();
  _bt_pin_code->set_pin((char*)pin_code->pin);
  _pin_reply.SerializeToString(&protoMsg);
  ALOGI("%s : protomsg length : %d", __func__,  protoMsg.length());

  uint16_t length = protoMsg.length();
  pin_reply_msg[2] = length & 0xFF;
  pin_reply_msg[3] = length >> 8;

  uint16_t proto_encode = PROTO_ENC_DEC;
  pin_reply_msg[4] = proto_encode & 0xFF;
  pin_reply_msg[5] = proto_encode >> 8;

  std::string msgStr((char *)pin_reply_msg, MAX_LENGTH_WITH_PROTO_NONE);
  msgStr.append(protoMsg);
#ifndef SS_STUB_ENABLED
  btSSInterface->postTxMsg(msgStr);
#else
  btSSStubInterface->postTxMsg(msgStr);
#endif
  return BT_STATUS_SUCCESS;
}

static int ssp_reply(const RawAddress* bd_addr, bt_ssp_variant_t variant,
                     uint8_t accept, uint32_t passkey) {
#if 0
  /* sanity check */
  if (interface_ready() == false) return BT_STATUS_NOT_READY;

  return btif_dm_ssp_reply(bd_addr, variant, accept, passkey);
#endif

  ALOGI("%s accept = %d", __func__, accept);
  uint8_t ssp_reply_msg[MAX_LENGTH_WITH_PROTO_NONE];

  uint16_t msg_id = BT_DM_SSP_REPLY;
  ssp_reply_msg[0] = msg_id & 0xFF;
  ssp_reply_msg[1] = msg_id >> 8;

  std::string protoMsg;
  ss_ssp_reply _ss_ssp_reply;
  _ss_ssp_reply.set_bd_addr(ToRawString(bd_addr).c_str());
  _ss_ssp_reply.set_variant((ss_bt_ssp_variant_t)variant);
  _ss_ssp_reply.set_accept(accept);
  _ss_ssp_reply.set_passkey(passkey);
  _ss_ssp_reply.SerializeToString(&protoMsg);
  ALOGI("%s : protomsg length : %d", __func__,  protoMsg.length());

  uint16_t length = protoMsg.length();
  ssp_reply_msg[2] = length & 0xFF;
  ssp_reply_msg[3] = length >> 8;

  uint16_t proto_encode = PROTO_ENC_DEC;
  ssp_reply_msg[4] = proto_encode & 0xFF;
  ssp_reply_msg[5] = proto_encode >> 8;

  std::string msgStr((char *)ssp_reply_msg, MAX_LENGTH_WITH_PROTO_NONE);
  msgStr.append(protoMsg);
#ifndef SS_STUB_ENABLED
  btSSInterface->postTxMsg(msgStr);
#else
  btSSStubInterface->postTxMsg(msgStr);
#endif
  return BT_STATUS_SUCCESS;
}

static int read_energy_info() {
  if (interface_ready() == false) return BT_STATUS_NOT_READY;
  btif_dm_read_energy_info();
  return BT_STATUS_SUCCESS;
}

static int clear_event_filter() {
  return BT_STATUS_SUCCESS;
}

static void dump(int fd, const char** arguments) {
  if (arguments != NULL && arguments[0] != NULL) {
    if (strncmp(arguments[0], "--proto-bin", 11) == 0) {
     // system_bt_osi::BluetoothMetricsLogger::GetInstance()->WriteBase64(fd,
       //                                                                 true);
      return;
    }
  }
  btif_debug_conn_dump(fd);
  btif_debug_bond_event_dump(fd);
  btif_debug_a2dp_dump(fd);
  //btif_debug_config_dump(fd);
#if (BT_IOT_LOGGING_ENABLED == TRUE)
  //device_debug_iot_config_dump(fd);
#endif
  //BTA_HfClientDumpStatistics(fd);
  //wakelock_debug_dump(fd);
  //osi_allocator_debug_dump(fd);
  //alarm_debug_dump(fd);
  //HearingAid::DebugDump(fd);
  //le_audio::has::HasClient::DebugDump(fd);
  //connection_manager::dump(fd);
  bluetooth::bqr::DebugDump(fd);
#if (BTSNOOP_MEM == TRUE)
  //btif_debug_btsnoop_dump(fd);
#endif
}

static int get_remote_pbap_pce_version(const RawAddress* bd_addr) {
  return 0;
}

static bool pbap_pse_dynamic_version_upgrade_is_enabled() {
  return false;
}

static const void* get_profile_interface(const char* profile_id) {
  ALOGI("%s: id = %s", __func__, profile_id);

  /* sanity check */
  if (interface_ready() == false) return NULL;

  /* check for supported profile interfaces */
  if (is_profile(profile_id, BT_PROFILE_HANDSFREE_ID))
    return bluetooth::headset::GetInterface();

  if (is_profile(profile_id, BT_PROFILE_HANDSFREE_CLIENT_ID))
    return btif_hf_client_get_interface();

  if (is_profile(profile_id, BT_PROFILE_SOCKETS_ID))
    return btif_sock_get_interface();

  if (is_profile(profile_id, BT_PROFILE_PAN_ID))
    return NULL;//btif_pan_get_interface();

  if (is_profile(profile_id, BT_PROFILE_ADVANCED_AUDIO_ID))
    return btif_av_get_src_interface();

  if (is_profile(profile_id, BT_PROFILE_ADVANCED_AUDIO_SINK_ID))
    return NULL;//btif_av_get_sink_interface();

  if (is_profile(profile_id, BT_PROFILE_HIDHOST_ID))
    return NULL;//btif_hh_get_interface();

  if (is_profile(profile_id, BT_PROFILE_HIDDEV_ID))
    return NULL;//btif_hd_get_interface();

  if (is_profile(profile_id, BT_PROFILE_SDP_CLIENT_ID))
    return btif_sdp_get_interface();

  if (is_profile(profile_id, BT_PROFILE_GATT_ID))
    return btif_gatt_get_interface();

  if (is_profile(profile_id, BT_PROFILE_AV_RC_ID))
    return btif_rc_get_interface();

  if (is_profile(profile_id, BT_PROFILE_AV_RC_CTRL_ID))
    return btif_rc_ctrl_get_interface();

  if (is_profile(profile_id, BT_PROFILE_AV_RC_VENDOR_CTRL_ID))
    return NULL;//btif_rc_vendor_ctrl_get_interface();

  if (is_profile(profile_id, BT_PROFILE_VENDOR_ID))
    return btif_vendor_get_interface();

  if (is_profile(profile_id, BT_PROFILE_VENDOR_SOCKET_ID))
    return btif_vendor_socket_get_interface();
#if (SWB_ENABLED == TRUE)
  if (is_profile(profile_id, BT_PROFILE_VENDOR_HF_ID))
    return NULL;//btif_vendor_hf_get_interface();
#endif

  if (is_profile(profile_id, BT_PROFILE_BAT_ID))
    return NULL;//btif_bat_get_interface();

  if (is_profile(profile_id, BT_PROFILE_HEARING_AID_ID))
    return NULL;//btif_hearing_aid_get_interface();

  if (is_profile(profile_id, BT_PROFILE_HAP_CLIENT_ID))
    return NULL;//btif_has_client_get_interface();

#ifdef DIR_FINDING_FEATURE
  if (is_profile(profile_id, BT_PROFILE_ATP_LOCATOR_ID))
    return btif_atp_locator_get_interface();
#endif

  if (is_profile(profile_id, BT_KEYSTORE_ID))
    return NULL;//bluetooth::bluetooth_keystore::getBluetoothKeystoreInterface();
  return NULL;//get_external_profile_interface(profile_id);
}

int le_test_mode(uint16_t opcode, uint8_t* buf, uint8_t len) {
  ALOGI("%s", __func__);

  /* sanity check */
  if (interface_ready() == false) return BT_STATUS_NOT_READY;

  return btif_le_test_mode(opcode, buf, len);
}

static int set_os_callouts(bt_os_callouts_t* callouts) {
  //wakelock_set_os_callouts(callouts);
  return BT_STATUS_SUCCESS;
}

static void dumpMetrics(std::string* output) {
  ALOGI("%s", __func__);
}

static int config_clear(void) {
  ALOGI("%s", __func__);
  btif_stack_config_cleared();
  //return btif_config_clear() ? BT_STATUS_SUCCESS : BT_STATUS_FAIL;
  return BT_STATUS_SUCCESS;
}

static bluetooth::avrcp::ServiceInterface* get_avrcp_service(void) {
  //return bluetooth::avrcp::AvrcpService::GetServiceInterface();
  ALOGI("%s: Avrcp Interface not available", __func__);
  return NULL;
}

static std::string obfuscate_address(const RawAddress& address) {
   ALOGI("%s: address : %s", __func__, address.ToString().c_str());
   return bluetooth::common::AddressObfuscator::GetInstance()->Obfuscate(address);
}

static int get_metric_id(const RawAddress& address) {
  ALOGI("%s: not implemented", __func__);
  return 0;
}

static int set_dynamic_audio_buffer_size(int codec, int size) {
  return btif_set_dynamic_audio_buffer_size(codec, size);
}

static bool allow_low_latency_audio(bool allowed, const RawAddress& address) {
  return false;
}

static void metadata_changed(const RawAddress& remote_bd_addr, int key,
                             std::vector<uint8_t> value) {
}

static int clear_event_mask() {
  LOG_VERBOSE(LOG_TAG, "%s", __func__);
  return BT_STATUS_SUCCESS;
}

static int clear_filter_accept_list() {
  LOG_VERBOSE(LOG_TAG, "%s", __func__);
  return BT_STATUS_SUCCESS;
}

static int disconnect_all_acls() {
  LOG_VERBOSE(LOG_TAG, "%s", __func__);
  return BT_STATUS_SUCCESS;
}

static void le_rand_btif_cb(uint64_t random_number) {
  LOG_VERBOSE(LOG_TAG, "%s", __func__);
}

static int le_rand() {
  LOG_VERBOSE(LOG_TAG, "%s", __func__);
  return BT_STATUS_SUCCESS;
}

static int set_event_filter_inquiry_result_all_devices() {
  return BT_STATUS_SUCCESS;
}

static int set_default_event_mask_except(uint64_t mask, uint64_t le_mask) {
  return BT_STATUS_SUCCESS;
}

static int restore_filter_accept_list() {
  return BT_STATUS_SUCCESS;
}

static int allow_wake_by_hid() {
  if (!interface_ready()) return BT_STATUS_NOT_READY;
  return BT_STATUS_SUCCESS;
}

static int set_event_filter_connection_setup_all_devices() {
  // TODO(b/247376698) fill rest of stub
  if (!interface_ready()) return BT_STATUS_NOT_READY;
  return BT_STATUS_SUCCESS;
}

bool interop_match_addr(const char * feature_name,
                        const RawAddress* addr) {
  return false;
}

bool interop_match_name(const char * feature_name,
                        const char* name) {
  return false;
}

bool interop_match_addr_or_name(const char * feature_name,
                                const RawAddress* addr) {
  return false;
}

static void interop_database_add_remove_addr(bool do_add,
                                             const char* feature_name,
                                             const RawAddress* addr,
                                             int length) {
  return;
}

static void interop_database_add_remove_name(bool do_add,
                                             const char* feature_name,
                                             const char* name) {
  return;
}

static void read_or_set_metrics_salt() {
  ALOGI("%s", __func__);
  bluetooth::common::AddressObfuscator::Octet32 metrics_salt = {};
  size_t metrics_salt_length = metrics_salt.size();
  if (!bluetooth::common::AddressObfuscator::IsSaltValid(metrics_salt)) {
    LOG(INFO) << __func__ << ": Metrics salt is not invalid, creating new one";
    if (RAND_bytes(metrics_salt.data(), metrics_salt.size()) != 1) {
      LOG(FATAL) << __func__ << "Failed to generate salt for metrics";
    }
  }
  bluetooth::common::AddressObfuscator::GetInstance()->Initialize(metrics_salt);
}

EXPORT_SYMBOL bt_interface_t bluetoothInterface = {
    sizeof(bluetoothInterface),
    init,
    enable,
    disable,
    cleanup,
    get_adapter_properties,
    get_adapter_property,
    set_adapter_property,
    get_remote_device_properties,
    get_remote_device_property,
    set_remote_device_property,
    get_remote_service_record,
    get_remote_services,
    start_discovery,
    cancel_discovery,
    create_bond,
    create_bond_le,
    create_bond_out_of_band,
    remove_bond,
    cancel_bond,
    get_connection_state,
    pin_reply,
    ssp_reply,
    get_profile_interface,
    set_os_callouts,
    read_energy_info,
    dump,
    dumpMetrics,
    config_clear,
    interop_database_clear,
    interop_database_add,
    get_avrcp_service,
    obfuscate_address,
    get_metric_id,
    set_dynamic_audio_buffer_size,
    generate_local_oob_data,
    allow_low_latency_audio,
    clear_event_filter,
    clear_event_mask,
    clear_filter_accept_list,
    disconnect_all_acls,
    le_rand,
    set_event_filter_inquiry_result_all_devices,
    set_default_event_mask_except,
    restore_filter_accept_list,
    allow_wake_by_hid,
    set_event_filter_connection_setup_all_devices,
    get_wbs_supported,
    get_swb_supported,
    metadata_changed,
    interop_match_addr,
    interop_match_name,
    interop_match_addr_or_name,
    interop_database_add_remove_addr,
    interop_database_add_remove_name,
    get_remote_pbap_pce_version,
    pbap_pse_dynamic_version_upgrade_is_enabled,
};

void invoke_oob_data_request_cb(tBT_TRANSPORT t, bool valid, Octet16 c,
                                Octet16 r, RawAddress raw_address,
                                uint8_t address_type) {
  ALOGI("%s", __func__);
  bt_oob_data_t oob_data = {};
  /*char* local_name;
 // BTM_ReadLocalDeviceName(&local_name);
  for (int i = 0; i < BTM_MAX_LOC_BD_NAME_LEN; i++) {
    oob_data.device_name[i] = local_name[i];
  }*/

  // Set the local address
  int j = 5;
  for (int i = 0; i < 6; i++) {
    oob_data.address[i] = raw_address.address[j];
    j--;
  }
  oob_data.address[6] = address_type;

  // Each value (for C and R) is 16 octets in length
  bool c_empty = true;
  for (int i = 0; i < 16; i++) {
    // C cannot be all 0s, if so then we want to fail
    if (c[i] != 0) c_empty = false;
    oob_data.c[i] = c[i];
    // R is optional and may be empty
    oob_data.r[i] = r[i];
  }
  oob_data.is_valid = valid && !c_empty;
  // The oob_data_length is 2 octects in length.  The value includes the length
  // of itself. 16 + 16 + 2 = 34 Data 0x0022 Little Endian order 0x2200
  oob_data.oob_data_length[0] = 0;
  oob_data.oob_data_length[1] = 34;
  bt_status_t status = BT_STATUS_FAIL;
 /* bt_status_t status = do_in_jni_thread(
      FROM_HERE, Bind(
                     [](tBT_TRANSPORT t, bt_oob_data_t oob_data) {
                       HAL_CBACK(bt_hal_cbacks, generate_local_oob_data_cb, t,
                                 oob_data);
                     },
                     t, oob_data));*/
  if (status != BT_STATUS_SUCCESS) {
    ALOGI("%s: Failed to call callback!", __func__);
  }
}

void single_stack_enable_status(int status){
  if(status == true){
      ALOGI("bt enable is success...sending adapter_state_changed_callback from btif");
      do_in_jni_thread(
        FROM_HERE, Bind(
                      [](bt_state_t BT_STATE_ON) {
                        HAL_CBACK(bt_hal_cbacks, adapter_state_changed_cb, BT_STATE_ON);
                      },
                      BT_STATE_ON));
  }else{
      ALOGI("bt disable is success...sending adapter_state_changed_callback from btif");
      do_in_jni_thread(
        FROM_HERE, Bind(
                      [](bt_state_t BT_STATE_OFF) {
                        HAL_CBACK(bt_hal_cbacks, adapter_state_changed_cb, BT_STATE_OFF);
                      },
                      BT_STATE_OFF));
  }
}

void btss_uevent_handler() {
  ALOGI("%s ",__func__);
  //Start Thread for monitoring BTSS state UEvents
  int dev_fd;
  char msg[UEVENT_MSG_LEN + 2];
  int n;

  ALOGD(" :%s starting BTSS uevent handler thread", __func__);
  dev_fd = uevent_open_socket(64*1024, true);
  if (dev_fd < 0) {
    ALOGE(" %s: UEvent socket open FAILED returning error: %d", __func__, dev_fd);
    return;
  }
  ALOGD(" %s: UEvent socket open SUCCESS dev_fd:%d", __func__,dev_fd);
  while((n = uevent_kernel_multicast_recv(dev_fd, msg, UEVENT_MSG_LEN)) > 0) {
    if (n < 0 || n > UEVENT_MSG_LEN) {
      ALOGE("%s : Error in message length", __func__);
      continue;
    }
    msg[n] = '\0';
    msg[n+1] = '\0';
    char *msg_ptr=(char *)msg;

    if (strstr(msg, "slate_com_dev")) {
      char *ptr = msg_ptr;
      do {
        ptr = ptr + strlen(ptr) + 1;
      } while (*ptr);

      while (*msg_ptr) {
        if (!strncmp(msg_ptr, BTSS_EVENT, BTSS_EVENT_STRING_LEN)) {
          msg_ptr += BTSS_EVENT_STRING_LEN + 1;
          btss_event = static_cast<ss_slate_event_type>(atoi(msg_ptr));
          ALOGD(" %s : event received - btssevent(%d)", __func__,btss_event);
          switch((int)btss_event) {
            case SS_SLATE_BEFORE_POWER_UP:
              ALOGD(" %s : event received - SLATE_BEFORE_POWER_UP", __func__);
              break;
            case SS_SLATE_AFTER_POWER_UP:
              {
                ALOGD(" %s : event received - SLATE_AFTER_POWER_UP", __func__);
                btssPrevousState = btssCurrentState;
                btssCurrentState = SS_SLATE_UP;
              }
              break;
            case SS_SLATE_BEFORE_POWER_DOWN:
              {
                ALOGE(" %s : event received -SLATE_BEFORE_POWER_DOWN", __func__);
                btssPrevousState = btssCurrentState;
                btssCurrentState = SS_SLATE_DOWN;
                if (alarm_is_scheduled(ssr_dump_timeout)) {
                    ALOGD("%s(): ssr_dump_timeout() scheduled", __func__);
                    alarm_cancel(ssr_dump_timeout);
                }
                ALOGD("%s(): starting alarm timer ssr time out", __func__);
                alarm_set_on_mloop(ssr_dump_timeout, BT_SSR_TIMEOUT, ssrDumpTimeout, NULL);
              }
              break;
            case SS_SLATE_AFTER_POWER_DOWN:
              {
                ALOGE(" %s : event received - SLATE_AFTER_POWER_DOWN ", __func__);
                btssPrevousState = btssCurrentState;
                btssCurrentState = SS_SLATE_DOWN;
              }
              break;
            case SS_SLATE_BT_READY:
              {
                ALOGD(" %s : event received - SLATE_BT_READY", __func__);
                btssPrevousState = btssCurrentState;
                btssCurrentState = SS_BTSS_UP;
                pthread_cond_signal(&btssEventCond);
              }
              break;
            case SS_SLATE_BT_ERROR:
              {
                ALOGE(" %s : event received - SLATE_BT_ERROR", __func__);
                btssPrevousState = btssCurrentState;
                btssCurrentState = SS_BTSS_DOWN;
                ALOGD("Waiting for BT SSR Dump to be completed to trigger SSR..!!!");
              }
              break;
            default:
              ALOGE(" %s : event received - not Handled uevent in BT(%d) ", __func__,btss_event);
              break;
          }
        }
        while(*msg_ptr++);
      }
    }
  }
}

void init_btss_event_handler() {
  ALOGI("%s ",__func__);
  if(!btss_event_handler_thread) {
    btss_event_handler_thread = std::unique_ptr<std::thread>(new std::thread(&btss_uevent_handler));
  }
}

void read_btss_state() {
  ALOGI("%s ",__func__);
  char btss_state[10];
  int fd_state;
  fd_state = open(BTSS_STATE_NODE, O_RDONLY);
  if (fd_state < 0) {
    ALOGE("Reading BTSS state from sys class failed fd state %d", fd_state);
    return;
  }
  int nofby = read(fd_state,btss_state, sizeof(btss_state) );
  ALOGD("%s:Read BTSS state from sys class: %d ",__func__,nofby);
  if( nofby > 0) {
    ALOGD(" %s BTSS state read from %s : %s ",__func__,  BTSS_STATE_NODE,
    btss_state);
    if(!strcmp(btss_state,  "ready")) {
      //Update current state read from node
      //std::unique_lock<std::mutex> guard(state_update_mtx);
      btssPrevousState = SS_BTSS_DOWN;
      btssCurrentState = SS_BTSS_UP;
      ALOGD("BTSS state : ready ");
    } else {
      //Update current state read from node
      //std::unique_lock<std::mutex> guard(state_update_mtx);
      btssPrevousState = SS_BTSS_UP;
      btssCurrentState = SS_BTSS_DOWN;
      ALOGE("BTSS state : eror BTSS not ready at");
    }
  }
  close(fd_state);
}

void btif_ss_interface_init(){
  ALOGI("%s ",__func__);
  if(btSSInterface == NULL){
    btSSInterface = BluetoothSSInterface::getInstance();
    if (btSSInterface == NULL) {
      ALOGI("%s single stack interface Initialization failed",__func__);
    }
  }else{
    ALOGI("single stack interface is already created");
  }
#ifdef SS_STUB_ENABLED
  if(btSSStubInterface == NULL){
    btSSStubInterface = BluetoothSSStubInterface::getInstance();
    if (btSSStubInterface == NULL) {
      ALOGI("%s single stack stub interface Initialization failed",__func__);
    }
  }else{
    ALOGI("single stack stub interface is already created");
  }
#endif
}

void btif_ss_interface_cleanup(){
  if(btSSInterface == NULL){
    ALOGI("single stack interface is already null");
  }else{
    btSSInterface->cleanup();
    btSSInterface = NULL;
  }
  if (alarm_is_scheduled(ssr_dump_timeout)) {
    ALOGI("%s(): ssr_dump_timeout() scheduled", __func__);
    alarm_cancel(ssr_dump_timeout);
    alarm_free(ssr_dump_timeout);
  } else {
    ALOGI("%s(): ssr_dump_timeout() is not scheduled", __func__);
  }
#ifdef SS_STUB_ENABLED
  if(btSSStubInterface == NULL){
    ALOGI("single stack stub interface is already null");
  }else{
    btSSStubInterface = NULL;
  }
#endif
}

void btif_dm_ss_callback(uint16_t event, char* p_param) {
  ALOGI("btif_dm_ss_callback :: event is :: %X",event);
  std::string resBufferString;
  tBTIF_SS_Cback* cb_data = (tBTIF_SS_Cback*)p_param;
  if (event == BT_DM_SSR_CB) {
    if (total_dump_size == 0) {
      total_dump_size = cb_data->payload[3] + (cb_data->payload[2]<<8) + (cb_data->payload[1]<<16) + (cb_data->payload[0]<<24);
      ALOGE("TOTAL SSR DUMP SIZE :: %d",total_dump_size);
      ss_ssr_event_received();
    } else {
      int payload_length = cb_data->num_bytes;
      ALOGD("Payload length received %d", payload_length);
      if (ssr_fptr == NULL) {
        ALOGE("ssr_fptr is NULL");
      } else {
        fprintf(ssr_fptr, "%s", (char*)(cb_data->payload));
      }
      total_dump_size = total_dump_size - payload_length;
      if (total_dump_size <= 0) {
        ALOGE("SSR DUMP COMPLETED. Triggering Unlock");
        if (alarm_is_scheduled(ssr_dump_timeout)) {
          ALOGD("%s(): ssr_dump_timeout() scheduled", __func__);
          alarm_cancel(ssr_dump_timeout);
        }
        fclose(ssr_fptr);
        if (btSSInterface != NULL) {
          btSSInterface->deregisterCallbacks(BT_PROFILE_DM_ID);
        }
        btif_ss_interface_cleanup();
        ALOGE(" %s : Triggering SSR ", __func__);
        HAL_CBACK(bt_vendor_callbacks, ssr_vendor_cb);
      } else {
        ALOGE("REMAINING SSR DUMP SIZE :: %d",total_dump_size);
      }
    }
    free (cb_data->payload);
    return;
  }
  uint16_t MSG_ID = cb_data->payload[0] + (((int)(cb_data->payload[1]))<<8);
  uint16_t length = cb_data->payload[2] + (((int)(cb_data->payload[3]))<<8);
  uint16_t proto_ec = 0;
  if (length > 0) {
      proto_ec = cb_data->payload[4] + (((int)(cb_data->payload[5]))<<8);
      char resBuffer[length];
      int j = 0;
      for(int i=MSG_PROTO_OFFSET; i< (length + MSG_PROTO_OFFSET); i++){
          resBuffer[j] = (char)cb_data->payload[i];
          //ALOGI("resBuffer[%d] is :: %d",j,resBuffer[j]);
           j++;
      }
      resBufferString.assign(resBuffer, length);
      free (cb_data->payload);
  }
  ALOGI("MSG_ID is :: %X , Proto length: %d and Proto Encoded Value %d",MSG_ID, length, proto_ec);
  switch (MSG_ID) {
    case BT_DM_ADAPTER_STATE_CHANGE_CB: {
        ALOGI("Has BT_DM_ADAPTER_STATE_CHANGE_CB");
        ss_adapter_state_changed_callback adapterStateChangedCb;
        bool ret = adapterStateChangedCb.ParseFromString(resBufferString);
        if(!ret) {
          ALOGE("Unable to parse string");
          break;
        }
        char property[PROPERTY_VALUE_MAX];
        char addr_property[PROPERTY_VALUE_MAX];
        bt_property_t prop;
        bt_bdname_t bd_name;
        if(adapterStateChangedCb.has_state()) {
            ALOGI("parseRxData has_state");
            bt_state_t return_status;
            ss_bt_state_t btStateSingleStack = adapterStateChangedCb.state();
            ALOGI("parseRxData btStateSingleStack is :: %d",btStateSingleStack);
            switch((int)btStateSingleStack) {
                case BT_ENABLE_STATUS_SUCCESS:
                    if (property_get(PERSIST_BDADDR_PROPERTY, addr_property, NULL)) {
                      ALOGD(" %s : Got addr_property %s ", __func__, addr_property);
                      char addr_prop[kStringLength + 1];
                      bool isWrite;
                      split_address(addr_property, &isWrite, addr_prop);
                      if (isWrite) {
                        ALOGD(" %s :Adapter State received for first boot", __func__);
                        std::string prop;
                        prop.append("false ");
                        prop.append(addr_prop);
                        if (property_set(PERSIST_BDADDR_PROPERTY, prop.c_str()) < 0) {
                          ALOGE("%s: Failed to set random BDA in prop %s", __func__,
                              PERSIST_BDADDR_PROPERTY);
                        }
                      }
                    }
                    property_get(PERSIST_BDNAME_PROPERTY, property, NULL);
                    property[strlen(property)] = '\0';
                    return_status = BT_STATE_ON;
                    uid_set = uid_set_create();
                    btif_sock_init(uid_set);
                    read_or_set_metrics_salt();
                    if(strlen(property) != 0 ){
                      ALOGI("%s: Setting BT Name to %s", __func__, property);
                      strlcpy((char*)bd_name.name, property, sizeof(bt_bdname_t));
                      prop.val = &bd_name;
                      prop.type = BT_PROPERTY_BDNAME;;
                      prop.len = strlen(property);
                      set_adapter_property(&prop);
                    }
                break;

                case BT_ENABLE_STATUS_FAILURE:
                    return_status = BT_STATE_OFF;
                    inq_db_clear();
                break;

                default:
                    return_status = BT_STATE_OFF;
                    inq_db_clear();
                break;
            }
            HAL_CBACK(bt_hal_cbacks, adapter_state_changed_cb, return_status);
        }
        break;
    }
    case BT_DM_ADAPTER_PROPERTIES_CB: {
      ALOGI("Has BT_DM_ADAPTER_PROPERTIES_CB");
      ss_adapter_properties_callback adapterPropCb;
      bool ret = adapterPropCb.ParseFromString(resBufferString);
      if(!ret) {
          ALOGE("Unable to parse string");
          break;
      }
      bt_status_t status = BT_STATUS_FAIL;
      if (adapterPropCb.has_status()) {
        ALOGI("Has BT_DM_ADAPTER_PROPERTIES_CB: has_status");
        status = (bt_status_t)adapterPropCb.status();
      }
      if(adapterPropCb.has_num_properties()) {
        RawAddress bd_addr;
        bt_scan_mode_t mode;
        bt_bdname_t bd_name;
        uint32_t timeout;
        uint8_t* le_features;
        uint32_t cod;
        RawAddress bonded_devices[100];

        int numProp = adapterPropCb.num_properties();
        ALOGI("numProp is :: %d",numProp);
        bt_property_t properties[numProp];
        memset(properties, 0, sizeof(properties));
        for(int i=0; i<numProp; i++) {
          ss_bt_property_t prop = adapterPropCb.properties(i);
          ss_bt_property_type_t prop_type = prop.type();
          ALOGI("prop_type is :: %d",prop_type);
          if(prop_type == BT_PROPERTY_BDADDR) {
            uint8_t* addr = (uint8_t*)prop.val().c_str();
            std::string bt_address = ((RawAddress*)addr)->ToString();
            ALOGI("address is :: %s",bt_address.c_str());
            RawAddress::FromString(bt_address.c_str(), bd_addr);
            properties[i].len = RawAddress::kLength;
            properties[i].val = (void*)bd_addr.address;
            properties[i].type = BT_PROPERTY_BDADDR;
          } else if(prop_type == BT_PROPERTY_BDNAME) {
            std::string bt_name = prop.val();
            ALOGI("Name is : %s",bt_name.c_str());
            strlcpy((char*)bd_name.name, (char*)bt_name.c_str(), sizeof(bt_bdname_t));
            properties[i].len = prop.len();
            properties[i].val = &bd_name;
            properties[i].type = BT_PROPERTY_BDNAME;
          } else if(prop_type == BT_PROPERTY_UUIDS) {
            std::string uuid_str = prop.val();
            ALOGI("UUID's are : %s",uuid_str.c_str());
            const char* uuids = uuid_str.c_str();
            if(strlen(uuid_str.c_str()) != 0){
              properties[i].len = prop.len();
              properties[i].val = (void*)uuids;
              properties[i].type = BT_PROPERTY_UUIDS;
            }
          } else if(prop_type == SS_BT_PROPERTY_ADAPTER_SCAN_MODE) {
            std::string scan_mode = prop.val();
            mode = (bt_scan_mode_t)(std::stoi(scan_mode));
            ALOGI("Scan Mode : %d", mode);
            properties[i].len = sizeof(bt_scan_mode_t);
            properties[i].val = (void*)&mode;
            properties[i].type = BT_PROPERTY_ADAPTER_SCAN_MODE;
          } else if(prop_type == SS_BT_PROPERTY_ADAPTER_BONDED_DEVICES) {
            std::string bonded_dev = prop.val();
            ALOGI("Bonded devices are : %s",bonded_dev.c_str());
            if(prop.len() == 0 || strlen(bonded_dev.c_str()) == 0) {
              ALOGI("No bonded devices present");
              continue;
            }
            char* devices = const_cast<char*>(bonded_dev.c_str());
            char* temp;
            char* rest = devices;
            int count = 0;
            temp = strtok_r(devices," ",&rest);
            while(temp != NULL) {
               if(strlen(temp)!=12) {
                 ALOGI("Received wrong bddress size - %d", strlen(temp));
                 temp = strtok_r(NULL, " ",&rest);
                 continue;
               }
               RawAddress bd_addr_temp;
               for(int i=0,j=0;i<6;i++,j=j+2)
               {
                 std::string substring = (std::string(temp)).substr(j,2);
                 bd_addr_temp.address[i] = strtol(substring.c_str(), NULL, 16);
               }
               ALOGI("address is :: %s",bd_addr_temp.ToString().c_str());
               memcpy(&bonded_devices[count], &bd_addr_temp, RawAddress::kLength);
               temp = strtok_r(NULL, " ",&rest);
               count++;
            }
            properties[i].len = count* sizeof(RawAddress);
            properties[i].val = &bonded_devices;
            properties[i].type = BT_PROPERTY_ADAPTER_BONDED_DEVICES;
          } else if(prop_type == SS_BT_PROPERTY_ADAPTER_DISCOVERY_TIMEOUT) {
            std::string discovery_timeout = prop.val();
            timeout = (uint32_t)(std::stoi(discovery_timeout));
            ALOGI("Discovery timeout : %d", timeout);
            properties[i].len = sizeof(uint32_t);
            properties[i].val = (void*)&timeout;
            properties[i].type = BT_PROPERTY_ADAPTER_DISCOVERABLE_TIMEOUT;
          } else if(prop_type == SS_BT_PROPERTY_LOCAL_LE_FEATURES) {
            le_features = (uint8_t*)prop.val().c_str();
            ALOGI("LE features length %d and bt_local_le_features_t struct size %d", prop.len(), sizeof(bt_local_le_features_t));
            properties[i].len = sizeof(bt_local_le_features_t);
            properties[i].val = (void*)le_features;
            properties[i].type = BT_PROPERTY_LOCAL_LE_FEATURES;
          } else if(prop_type == BT_PROPERTY_CLASS_OF_DEVICE) {
            std::string cod_val = prop.val();
            cod  = (uint32_t)(std::stoi(cod_val));
            ALOGI("COD value : 0x%06x", cod);
            properties[i].len = sizeof(uint32_t);
            properties[i].val = (void*)&cod;
            properties[i].type = BT_PROPERTY_CLASS_OF_DEVICE;
          }
        }
        HAL_CBACK(bt_hal_cbacks, adapter_properties_cb, status, numProp, properties);
      }
      break;
    }
    case BT_DM_REMOTE_DEVICE_PROPERTIES_CB: {
      ALOGI(" BT_DM_REMOTE_DEVICE_PROPERTIES_CB");
      ss_remote_device_properties_callback remotePropCb;
      bool ret = remotePropCb.ParseFromString(resBufferString);
      if(!ret) {
          ALOGE("Unable to parse string");
          break;
      }
      bt_status_t status = BT_STATUS_FAIL;
      bt_bdname_t bd_name;
      uint32_t cod;
      RawAddress *bd_addr;
      if (remotePropCb.has_status()) {
        ALOGI("BT_DM_REMOTE_DEVICE_PROPERTIES_CB: parseRxData has_status");
        status = (bt_status_t)remotePropCb.status();
        ALOGI("BT_DM_REMOTE_DEVICE_PROPERTIES_CB: status : %d", status);
      }
      if (remotePropCb.has_bd_addr()) {
        ALOGI("BT_DM_REMOTE_DEVICE_PROPERTIES_CB: parseRxData has_remote_bd_addr");
        uint8_t* addr = (uint8_t*)remotePropCb.bd_addr().c_str();
        bd_addr = (RawAddress*)addr;
        ALOGI("BT_DM_REMOTE_DEVICE_PROPERTIES_CB : length: %d ", bd_addr->ToString().length());
        ALOGI("BT_DM_REMOTE_DEVICE_PROPERTIES_CB : address : %s", bd_addr->ToString().c_str());
      }
      if(remotePropCb.has_num_properties()) {
        RawAddress bd_addr_prop;
        bt_device_type_t dev_type;
        bt_scan_mode_t mode;
        uint32_t timeout;
        int numProp = remotePropCb.num_properties();
        bt_property_t properties[numProp];
        memset(properties, 0, sizeof(properties));
        ALOGI("BT_DM_REMOTE_DEVICE_PROPERTIES_CB: parseRxData has_num_properties");
        ALOGI("numProp is :: %d",numProp);
        for(int i=0; i<numProp; i++) {
          ss_bt_property_t prop = remotePropCb.properties(i);
          ss_bt_property_type_t prop_type = prop.type();
          ALOGI("prop_type is :: %d",prop_type);
          if(prop_type == BT_PROPERTY_BDADDR) {
            uint8_t* addr = (uint8_t*)prop.val().c_str();
            std::string bt_address = ((RawAddress*)addr)->ToString();
            ALOGI("address is :: %s",bt_address.c_str());
            RawAddress::FromString(bt_address.c_str(), bd_addr_prop);
            properties[i].len = RawAddress::kLength;
            properties[i].val = (void*)bd_addr_prop.address;
            properties[i].type = BT_PROPERTY_BDADDR;
          } else if(prop_type == BT_PROPERTY_BDNAME) {
            std::string bt_name = prop.val();
            ALOGI("Name is :: %s",bt_name.c_str());
            strlcpy((char*)bd_name.name, (char*)bt_name.c_str(), sizeof(bt_bdname_t));
            properties[i].len = prop.len();
            properties[i].val = &bd_name;
            properties[i].type = BT_PROPERTY_BDNAME;
          } else if(prop_type == BT_PROPERTY_UUIDS) {
            std::string uuid_str = prop.val();
            ALOGI("UUID's are : %s",uuid_str.c_str());
            if(bond_state == BT_BOND_STATE_BONDED) {
               bond_state =  BT_BOND_STATE_NONE;
            }
            const char* uuids = uuid_str.c_str();
            properties[i].len = prop.len();
            properties[i].val = (void*)uuids;
            properties[i].type = BT_PROPERTY_UUIDS;
          } else if(prop_type == SS_BT_PROPERTY_ADAPTER_SCAN_MODE) {
            std::string scan_mode = prop.val();
            mode = (bt_scan_mode_t)(std::stoi(scan_mode));
            ALOGI("Scan Mode : %d", mode);
            properties[i].len = prop.len();
            properties[i].val = (void*)&mode;
            properties[i].type = BT_PROPERTY_ADAPTER_SCAN_MODE;
          } else if(prop_type == SS_BT_PROPERTY_ADAPTER_BONDED_DEVICES) {
            std::string bonded_dev = prop.val();
            ALOGI("Bonded devices are : %s",bonded_dev.c_str());
            const char* devices = bonded_dev.c_str();
            properties[i].len = prop.len();
            properties[i].val = (void*)devices;
            properties[i].type = BT_PROPERTY_ADAPTER_BONDED_DEVICES;
          } else if(prop_type == SS_BT_PROPERTY_ADAPTER_DISCOVERY_TIMEOUT) {
            std::string discovery_timeout = prop.val();
            timeout = (uint32_t)(std::stoi(discovery_timeout));
            ALOGI("Discovery timeout : %d", timeout);
            properties[i].len = prop.len();
            properties[i].val = (void*)&timeout;
            properties[i].type = BT_PROPERTY_ADAPTER_DISCOVERABLE_TIMEOUT;
          } else if(prop_type == BT_PROPERTY_CLASS_OF_DEVICE) {
            std::string cod_val = prop.val();
            cod  = (uint32_t)(std::stoi(cod_val));
            ALOGI("COD value : 0x%06x", cod);
            properties[i].len = sizeof(uint32_t);
            properties[i].val = (void*)&cod;
            properties[i].type = BT_PROPERTY_CLASS_OF_DEVICE;
          }else if(prop_type == BT_PROPERTY_TYPE_OF_DEVICE) {
            std::string dev_val = prop.val();
            dev_type = (bt_device_type_t)(std::stoi(dev_val));
            ALOGI("Dev Type : %d", dev_type);
            properties[i].len = sizeof(bt_device_type_t);
            properties[i].val = (void*)&dev_type;
            properties[i].type = BT_PROPERTY_TYPE_OF_DEVICE;
          }
        }
        HAL_CBACK(bt_hal_cbacks, remote_device_properties_cb, status, bd_addr, numProp, properties);
      }
      break;
    }
    case BT_DM_DISCOVERY_STATE_CHANGE_CB: {
      ALOGI("Has BT_DM_DISCOVERY_STATE_CHANGE_CB");
      ss_discovery_state_changed_callback discoveryStateChanged;
      bool ret = discoveryStateChanged.ParseFromString(resBufferString);
      if(!ret) {
        ALOGE("Unable to parse string");
        break;
      }
      if(discoveryStateChanged.has_state()) {
        bt_discovery_state_t discovery_state;
        discovery_state = (bt_discovery_state_t)discoveryStateChanged.state();
        ALOGI("Has BT_DM_DISCOVERY_STATE_CHANGE_CB: has_state -> %d", discovery_state);
        if(discovery_state == BT_DISCOVERY_STARTED) {
          ALOGI("Discovery Started callback");
          if (btSSInterface != NULL) {
            btSSInterface->ssGlinkWakeLockAcquireOrRelease(true, true);
          }
        } else if(discovery_state == BT_DISCOVERY_STOPPED) {
          ALOGI("Discovery Stopped callback");
          if (btSSInterface != NULL) {
            btSSInterface->ssGlinkWakeLockAcquireOrRelease(true, false);
          }
        }
        HAL_CBACK(bt_hal_cbacks, discovery_state_changed_cb, discovery_state);
      } else {
        ALOGI("BT_DM_DISCOVERY_STATE_CHANGE_CB: Not have state info");
      }
      break;
    }
    case BT_DM_DEVICE_FOUND_CB: {
      ALOGI("Has BT_DM_DEVICE_FOUND_CB");
      ss_device_found_callback deviceFoundCb;
      RawAddress bd_addr;
      bt_device_type_t dev_type;
      bt_bdname_t bd_name;
      int8_t rssi;
      uint32_t cod;
      bool is_new = false;
      bool update = false;
      bool ret = deviceFoundCb.ParseFromString(resBufferString);
      if(!ret) {
        ALOGE("Unable to parse string");
        break;
      }
      if(deviceFoundCb.has_num_properties()) {
      int numProp = deviceFoundCb.num_properties();
      ALOGI("BT_DM_DEVICE_FOUND_CB: has_num_properties");
      ALOGI("numProp is :: %d",numProp);
      bt_property_t properties[numProp];
      memset(properties, 0, sizeof(properties));
      for(int i=0; i<numProp; i++) {
        ss_bt_property_t prop = deviceFoundCb.properties(i);
        ss_bt_property_type_t prop_type = prop.type();
        ALOGI("prop_type is :: %d",prop_type);
        if(prop_type == BT_PROPERTY_BDADDR) {
          uint8_t* addr = (uint8_t*)prop.val().c_str();
          std::string bt_address = ((RawAddress*)addr)->ToString();
          ALOGI("address is :: %s",bt_address.c_str());
          RawAddress::FromString(bt_address.c_str(), bd_addr);
          properties[i].len = RawAddress::kLength;
          properties[i].val = (void*)bd_addr.address;
          properties[i].type = BT_PROPERTY_BDADDR;
        } else if(prop_type == BT_PROPERTY_BDNAME) {
          std::string bt_name = prop.val();
          std::string bt_name_substr = bt_name.substr(0, prop.len());
          ALOGI("Name is : %s",bt_name_substr.c_str());
          strlcpy((char*)bd_name.name, (char*)bt_name.c_str(), sizeof(bt_bdname_t));
          properties[i].len = prop.len();
          properties[i].val = &bd_name;
          properties[i].type = BT_PROPERTY_BDNAME;
        } else if(prop_type == BT_PROPERTY_UUIDS) {
          properties[i].len = prop.len();
          properties[i].type = BT_PROPERTY_UUIDS;
        } else if(prop_type == SS_BT_PROPERTY_ADAPTER_SCAN_MODE) {
          properties[i].len = prop.len();
          properties[i].type = BT_PROPERTY_ADAPTER_SCAN_MODE;
        } else if(prop_type == SS_BT_PROPERTY_ADAPTER_BONDED_DEVICES) {
          properties[i].len = prop.len();
          properties[i].type = BT_PROPERTY_ADAPTER_BONDED_DEVICES;
        } else if(prop_type == SS_BT_PROPERTY_ADAPTER_DISCOVERY_TIMEOUT) {
          properties[i].len = prop.len();
          properties[i].type = BT_PROPERTY_ADAPTER_DISCOVERABLE_TIMEOUT;
        } else if(prop_type == BT_PROPERTY_TYPE_OF_DEVICE) {
          std::string dev_val = prop.val();
          dev_type = (bt_device_type_t)(std::stoi(dev_val));
          ALOGI("Dev Type : %d", dev_type);
          properties[i].len = sizeof(bt_device_type_t);
          properties[i].val = (void*)&dev_type;
          properties[i].type = BT_PROPERTY_TYPE_OF_DEVICE;
        } else if(prop_type == BT_PROPERTY_REMOTE_RSSI) {
          std::string rssi_val = prop.val();
          rssi = (int8_t)(std::stoi(rssi_val));
          ALOGI("RSSI value : %d", rssi);
          properties[i].len = sizeof(int8_t);
          properties[i].val = (void*)&rssi;
          properties[i].type = BT_PROPERTY_REMOTE_RSSI;
        } else if(prop_type == BT_PROPERTY_CLASS_OF_DEVICE) {
          std::string cod_val = prop.val();
          cod  = (uint32_t)(std::stoi(cod_val));
          ALOGI("COD value : 0x%06x", cod);
          properties[i].len = sizeof(uint32_t);
          properties[i].val = (void*)&cod;
          properties[i].type = BT_PROPERTY_CLASS_OF_DEVICE;
        }
      }
      tInqDB_Addr* inq_dev_found = find_inq_db(bd_addr);
      if (inq_dev_found == NULL) {
        if(strlen((char*)bd_name.name) == 0){
          ALOGI("bdname is empty. Do not create record");
          break;
        }
        ALOGI("inq_dev is not found. Create new device entry");
        tInqDB_Addr* new_inq_dev = inq_db_new(bd_addr,bd_name,dev_type,rssi,cod);
        if(new_inq_dev != NULL){
          is_new = true;
        }
      }else{
        ALOGI("inq_dev is found, check if rssi parameter update :: rssi %d inq_dev_found->rssi %d",rssi,inq_dev_found->rssi);
        if(rssi != inq_dev_found->rssi){
          update = true;
        }
      }
      if(is_new || update){
          if(strlen((char*)bd_name.name) != 0){
            ALOGI("send HAL CBACK ::: %s and %s",bd_addr.ToString().c_str(),bd_name.name);
            HAL_CBACK(bt_hal_cbacks, device_found_cb, numProp, properties);
          }
      }
      }
    break;
    }
    case BT_DM_PIN_REQUEST_CB: {
      ALOGI("Has BT_DM_PIN_REQUEST_CB");
      ss_pin_request_callback pinRequestCb;
      bool ret = pinRequestCb.ParseFromString(resBufferString);
      if(!ret) {
          ALOGE("Unable to parse string");
          break;
      }
      RawAddress *bd_addr;
      if (pinRequestCb.has_remote_bd_addr()) {
        uint8_t* addr = (uint8_t*)pinRequestCb.remote_bd_addr().c_str();
        bd_addr = (RawAddress*)addr;
        ALOGI("BT_DM_PIN_REQUEST_CB:  address: %s", bd_addr->ToString().c_str());
        ALOGI("BT_DM_PIN_REQUEST_CB: length: %d ", bd_addr->ToString().length());
      }
      bt_bdname_t bdname;
      if (pinRequestCb.has_bd_name()) {
        ALOGI("BT_DM_PIN_REQUEST_CB: parseRxData has_bd_name");
        std::string bt_name = pinRequestCb.bd_name().name();
        strlcpy((char*)bdname.name, (char*)bt_name.c_str(), bt_name.length()+1);
	ALOGI("BT_DM_PIN_REQUEST_CB: name : %s", (char*)bdname.name);
      }
      uint32_t cod;
      if(pinRequestCb.has_cod())
      {
        ALOGI("BT_DM_PIN_REQUEST_CB: parseRxData has_cod");
        cod = pinRequestCb.cod();
        ALOGI("BT_DM_PIN_REQUEST_CB: cod : 0x%06x", cod);
      }
      bool min_16_digit = false;
      if(pinRequestCb.has_min_16_digit())
      {
        ALOGI("BT_DM_PIN_REQUEST_CB: parseRxData has_min_16_digit");
        min_16_digit = pinRequestCb.min_16_digit();
	    ALOGI("BT_DM_PIN_REQUEST_CB: min_16_digit : %d", min_16_digit);
      }
      HAL_CBACK(bt_hal_cbacks, pin_request_cb, bd_addr, &bdname, cod, min_16_digit);
        break;
    }
    case BT_DM_SSP_REQUEST_CB: {
      ALOGI(" Pairing: BT_DM_SSP_REQUEST_CB");
      ss_ssp_request_callback sspRequestCb;
      bool ret = sspRequestCb.ParseFromString(resBufferString);
      if(!ret) {
          ALOGE("Unable to parse string");
          break;
      }
      uint32_t cod, passkey;
      RawAddress *bd_addr;
      bt_ssp_variant_t ssp_variant;
      RawAddress inq_db_bdaddr;
      if(sspRequestCb.has_remote_bd_addr()){
        ALOGI("BT_DM_SSP_REQUEST_CB: parseRxData has_remote_bd_addr");
        uint8_t* addr = (uint8_t*)sspRequestCb.remote_bd_addr().c_str();
        bd_addr = (RawAddress*)addr;
        ALOGI("BT_DM_SSP_REQUEST_CB: addr : %s", bd_addr->ToString().c_str());
        ALOGI("BT_DM_SSP_REQUEST_CB: length: %d ", bd_addr->ToString().length());
        std::string bt_address = bd_addr->ToString().c_str();
        RawAddress::FromString(bt_address.c_str(), inq_db_bdaddr);
      }
      bt_bdname_t bdname;
      if (sspRequestCb.has_bdname()) {
        ALOGI("BT_DM_SSP_REQUEST_CB: parseRxData has_bdname");
        std::string bt_name = sspRequestCb.bdname().name();
        strlcpy((char*)bdname.name, (char*)bt_name.c_str(), bt_name.length()+1);
        ALOGI("BT_DM_SSP_REQUEST_CB: name: %s", (char*)bdname.name);
      }
      if(sspRequestCb.has_cod()) {
        ALOGI("BT_DM_SSP_REQUEST_CB: parseRxData has_cod");
        cod = sspRequestCb.cod();
        ALOGI("BT_DM_SSP_REQUEST_CB: cod : 0x%06x", cod);
      }
      if(sspRequestCb.has_pass_key()) {
        ALOGI("BT_DM_SSP_REQUEST_CB: parseRxData has_pass_key");
        passkey = sspRequestCb.pass_key();
        ALOGI("BT_DM_SSP_REQUEST_CB: pass_key : %d", passkey);
      }
      if(sspRequestCb.has_pairing_variant()) {
        ALOGI("BT_DM_SSP_REQUEST_CB: parseRxData has_pairing_ssp_variant");
        ssp_variant = (bt_ssp_variant_t)sspRequestCb.pairing_variant();
        ALOGI("BT_DM_SSP_REQUEST_CB: ssp_variant : %d", ssp_variant);
      }

      tInqDB_Addr* inq_dev_found = find_inq_db(inq_db_bdaddr);
      if (inq_dev_found == NULL) {
        ALOGI("inq_dev is not found");
      }else{
        ALOGI("inq_dev is found, check if cod parameter update :: cod %d inq_dev_found->cod %d",cod,inq_dev_found->cod);
        if(cod != inq_dev_found->cod){
          cod = inq_dev_found->cod;
        }
      }
      ALOGI("BT_DM_SSP_REQUEST_CB: Pairing: cod: 0x%06x bd_addr: %s bdname: %s, ssp_variant: %d, passkey: %d", cod, bd_addr->ToString().c_str(), (char*)bdname.name, ssp_variant, passkey);
      HAL_CBACK(bt_hal_cbacks, ssp_request_cb, bd_addr, &bdname, cod, ssp_variant, passkey);

      break;
    }
    case BT_DM_BOND_STATE_CHANGE_CB: {
      ALOGI("BT_DM_BOND_STATE_CHANGE_CB");
      ss_bond_state_changed_callback bondStateChangedCb;
      ALOGI("BT_DM_BOND_STATE_CHANGE_CB : message str : %s ", resBufferString.c_str());
      bool ret = bondStateChangedCb.ParseFromString(resBufferString);
      if(!ret) {
          ALOGE("Unable to parse string");
          break;
      }
      bt_status_t status = BT_STATUS_FAIL;
      if (bondStateChangedCb.has_status()) {
        ALOGI("BT_DM_BOND_STATE_CHANGE_CB: parseRxData has_status");
        status = (bt_status_t)bondStateChangedCb.status();
        ALOGI("BT_DM_Bond_STATE_CHANGE_CB: status : %d", status);
      }
      RawAddress *bd_addr;
      if(bondStateChangedCb.has_remote_bd_addr()){
        ALOGI("BT_DM_BOND_STATE_CHANGE_CB: parseRxData has_remote_bd_addr");
        uint8_t* addr = (uint8_t*)bondStateChangedCb.remote_bd_addr().c_str();
        bd_addr = (RawAddress*)addr;
        ALOGI("BT_DM_BOND_STATE_CHANGE_CB : length: %d ", bd_addr->ToString().length());
        ALOGI("BT_DM_Bond_STATE_CHANGE_CB : address : %s", bd_addr->ToString().c_str());
      }
      int fail_reason;
      if(bondStateChangedCb.has_fail_reason()) {
        ALOGI("BT_DM_BOND_STATE_CHANGE_CB: parseRxData has_fail_reason");
        fail_reason = bondStateChangedCb.fail_reason();
        ALOGI("BT_DM_Bond_STATE_CHANGE_CB: fail_reason: %d", fail_reason);
      }
      if(bondStateChangedCb.has_state()) {
        ALOGI("BT_DM_BOND_STATE_CHANGE_CB: parseRxData has_state");
        bond_state = (bt_bond_state_t)bondStateChangedCb.state();
        ALOGI("BT_DM_Bond_STATE_CHANGE_CB: state : %d", bond_state);
        HAL_CBACK(bt_hal_cbacks, bond_state_changed_cb, status, bd_addr, bond_state, fail_reason);
      }
      else {
        ALOGI("BT_DM_BOND_STATE_CHANGE_CB: Not have state info");
      }
      break;
    }
    case BT_DM_ACL_STATE_CHANGE_CB: {
      ALOGI("BT_DM_ACL_STATE_CHANGE_CB");
      ss_acl_state_changed_callback aclStateChangedCb;
      bool ret = aclStateChangedCb.ParseFromString(resBufferString);
      if(!ret) {
          ALOGE("Unable to parse string");
          break;
      }
      bt_status_t status = BT_STATUS_FAIL;
      if (aclStateChangedCb.has_status()) {
        ALOGI("BT_DM_ACL_STATE_CHANGE_CB: parseRxData has_status");
        status = (bt_status_t)aclStateChangedCb.status();
        ALOGI("BT_DM_ACL_STATE_CHANGE_CB: status : %d", status);
      }
      RawAddress *bd_addr;
      if (aclStateChangedCb.has_remote_bd_addr()) {
        ALOGI("BT_DM_ACL_STATE_CHANGE_CB: parseRxData has_remote_bd_addr");
        uint8_t* addr = (uint8_t*)aclStateChangedCb.remote_bd_addr().c_str();
        bd_addr = (RawAddress*)addr;
        ALOGI("BT_DM_ACL_STATE_CHANGE_CB: bd_addr : %s", bd_addr->ToString().c_str());
      }
      uint32_t hci_reason;
      if(aclStateChangedCb.has_hci_reason()) {
        ALOGI("BT_DM_ACL_STATE_CHANGE_CB: parseRxData has_hci_reason")uint32_t;
        hci_reason = aclStateChangedCb.hci_reason();
        ALOGI("BT_DM_ACL_STATE_CHANGE_CB: hci_reason : %d", hci_reason);
      }
      uint32_t link_type;
      if(aclStateChangedCb.has_transport_link_type()) {
        link_type = aclStateChangedCb.transport_link_type();
        ALOGI("BT_DM_ACL_STATE_CHANGE_CB: transport_link_type : %d", link_type);
      }
      bt_conn_direction_t direction;
      if(aclStateChangedCb.has_direction()) {
        direction = (bt_conn_direction_t)aclStateChangedCb.direction();
        ALOGI("BT_DM_ACL_STATE_CHANGE_CB: direction : %d", direction);
      }
      uint16_t acl_handle;
      if(aclStateChangedCb.has_acl_handle()) {
        acl_handle = aclStateChangedCb.acl_handle();
        ALOGI("BT_DM_ACL_STATE_CHANGE_CB: acl_handle : %d", acl_handle);
      }

      if(aclStateChangedCb.has_state()) {
            ALOGI("BT_DM_ACL_STATE_CHANGE_CB: parseRxData has_state");
            bt_acl_state_t acl_state;
            acl_state = (bt_acl_state_t)aclStateChangedCb.state();
            ALOGI("BT_DM_ACL_STATE_CHANGE_CB: state : %d", acl_state);
            uint32_t hci_reason;
            hci_reason = aclStateChangedCb.hci_reason();
            ALOGI("BT_DM_ACL_STATE_CHANGE_CB: Pairing: status: %d bdaddr: %s, acl_state: %d, hci_reason: %d, link_type: %d, direction: %d, acl_handle: %d ", status, bd_addr->ToString().c_str(), acl_state, hci_reason, link_type, direction, acl_handle);
            HAL_CBACK(bt_hal_cbacks, acl_state_changed_cb, BT_STATUS_SUCCESS, bd_addr, acl_state, link_type, uint8_t(hci_reason), direction, acl_handle);
        }

        else {
            ALOGI("BT_DM_ACL_STATE_CHANGE_CB: Not have state info");
        }
        break;

    }
    case BT_DM_LE_ADAPTER_PROPERTIES_CB: {
      ALOGI("Has BT_DM_LE_ADAPTER_PROPERTIES_CB");
      ss_bt_local_le_features_callback leAdapterPropCb;
      bool ret = leAdapterPropCb.ParseFromString(resBufferString);
      if(!ret) {
          ALOGE("Unable to parse string");
          break;
      }
      bt_status_t status = BT_STATUS_SUCCESS;
      bt_local_le_features_t le_features;
      bt_property_t properties[1];
      memset(properties, 0, sizeof(properties));
      le_features.version_supported = leAdapterPropCb.version_supported();
      le_features.local_privacy_enabled = leAdapterPropCb.local_privacy_enabled();
      le_features.max_adv_instance = leAdapterPropCb.max_adv_instance();
      le_features.rpa_offload_supported = leAdapterPropCb.rpa_offload_supported();
      le_features.max_irk_list_size = leAdapterPropCb.max_irk_list_size();
      le_features.max_adv_filter_supported = leAdapterPropCb.max_adv_filter_supported();
      le_features.activity_energy_info_supported = leAdapterPropCb.activity_energy_info_supported();
      le_features.scan_result_storage_size = leAdapterPropCb.scan_result_storage_size();
      le_features.total_trackable_advertisers = leAdapterPropCb.total_trackable_advertisers();
      le_features.extended_scan_support = leAdapterPropCb.extended_scan_support();
      le_features.debug_logging_supported = leAdapterPropCb.debug_logging_supported();
      le_features.le_2m_phy_supported = leAdapterPropCb.le_2m_phy_supported();
      le_features.le_coded_phy_supported = leAdapterPropCb.le_coded_phy_supported();
      le_features.le_extended_advertising_supported = leAdapterPropCb.le_extended_advertising_supported();
      le_features.le_periodic_advertising_supported = leAdapterPropCb.le_periodic_advertising_supported();
      le_features.le_maximum_advertising_data_length = leAdapterPropCb.le_maximum_advertising_data_length();
      le_features.dynamic_audio_buffer_supported = leAdapterPropCb.dynamic_audio_buffer_supported();
      le_features.le_periodic_advertising_sync_transfer_sender_supported = leAdapterPropCb.le_periodic_advertising_sync_transfer_sender_supported();
      le_features.le_connected_isochronous_stream_central_supported = leAdapterPropCb.le_connected_isochronous_stream_central_supported();
      le_features.le_isochronous_broadcast_supported = leAdapterPropCb.le_isochronous_broadcast_supported();
      le_features.le_periodic_advertising_sync_transfer_recipient_supported = leAdapterPropCb.le_periodic_advertising_sync_transfer_recipient_supported();

      properties[0].len = sizeof(bt_local_le_features_t);
      properties[0].val = &le_features;
      properties[0].type = BT_PROPERTY_LOCAL_LE_FEATURES;
      HAL_CBACK(bt_hal_cbacks, adapter_properties_cb, status, 1, properties);
      break;
    }
    default : {
        ALOGI("btif_dm_ss_callback :: msg id %X :: unknow", MSG_ID);
        break;
    }
  }
}

tInqDB_Addr* find_inq_db(const RawAddress& p_bda) {
  tInqDB_Addr* p_ent = btif_inq_db;
  for (int i = 0; i < BTM_INQ_DB_SIZE; i++,p_ent++) {
    if (p_ent->bd_addr == p_bda){
      ALOGI("Inq Device Found :: %s",p_bda.ToString().c_str());
      return p_ent;
    }
  }
  ALOGI("Inq Device Not Found :: %s",p_bda.ToString().c_str());
  return (NULL);
}

tInqDB_Addr* inq_db_new(const RawAddress& p_bda, bt_bdname_t name, bt_device_type_t devtype, int8_t rssi, uint32_t cod ) {
  tInqDB_Addr* p_ent = btif_inq_db;
  tInqDB_Addr* p_old = btif_inq_db;
  uint32_t ot = 0xFFFFFFFF;
  for (int i = 0; i < BTM_INQ_DB_SIZE; i++, p_ent++) {
    if (!p_ent->in_use) {
      memset(p_ent, 0, sizeof(tInqDB_Addr));
      p_ent->bd_addr = p_bda;
      p_ent->bd_name = name;
      p_ent->dev_type = devtype;
      p_ent->rssi = rssi;
      p_ent->cod = cod;
      p_ent->in_use = true;
      p_ent->time_of_resp = time_get_os_boottime_ms();
      ALOGI("New entry created in Inq DB :: %s at location :: %d",p_bda.ToString().c_str(),i);
      return p_ent;
    }
    if (p_ent->time_of_resp < ot) {
      p_old = p_ent;
      ot = p_ent->time_of_resp;
    }
  }
  /* If here, no free entry found. Return the oldest. */
  memset(p_old, 0, sizeof(tInqDB_Addr));
  p_old->bd_addr = p_bda;
  p_old->bd_name = name;
  p_old->dev_type = devtype;
  p_old->rssi = rssi;
  p_old->cod = cod;
  p_old->in_use = true;
  p_old->time_of_resp = time_get_os_boottime_ms();
  ALOGI("updating oldest entry %s",p_bda.ToString().c_str());
  return p_old;
}

void inq_db_clear(){
  ALOGI("inq_db_clear");
  tInqDB_Addr* p_ent = btif_inq_db;
  for (int i = 0; i < BTM_INQ_DB_SIZE; i++, p_ent++) {
    if (p_ent->in_use) {
      p_ent->in_use = false;
    }
  }
}
