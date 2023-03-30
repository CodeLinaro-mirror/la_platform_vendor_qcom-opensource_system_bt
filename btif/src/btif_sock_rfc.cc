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
 * Changes from Qualcomm Innovation Center are provided under the following license:
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 ******************************************************************************/

#define LOG_TAG "bt_btif_sock_rfcomm"

//#include <base/logging.h>
#include <errno.h>
#include <features.h>
#include <pthread.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <mutex>

#include <hardware/bluetooth.h>
#include <hardware/bt_sock.h>

#include "internal_include/bt_common.h"
#include "internal_include/bt_target.h"
//#include "internal_include/extra_include.h"
#include "bta_api.h"
#include "bta_jv_api.h"
#include "bta_jv_co.h"
#include "btif_common.h"
#include "btif_sock_sdp.h"
#include "btif_sock_thread.h"
#include "btif_sock_util.h"
#include "btif_uid.h"
#include "btif_util.h"
#include "btm_api.h"
#include "btm_int.h"
#include "btu.h"
#include "hcimsgs.h"
#include "osi/include/compat.h"
#include "osi/include/list.h"
#include "osi/include/log.h"
#include "osi/include/osi.h"
#include "port_api.h"
#include "sdp_api.h"
#include <hardware/vendor_socket.h>

/* The JV interface can have only one user, hence we need to call a few
 * L2CAP functions from this file. */
#include "btif_sock_l2cap.h"
#include "btif_ss_interface.h"
#ifdef SS_STUB_ENABLED
#include "btif_ss_stub_interface.h"
#endif
#include "protobuf/proto/dm.pb.h"
#include "protobuf/proto/rfcomm.pb.h"
#include "btif/protobuf/include/proto_message_ids.h"

#define MODEM_SIGNAL_DTRDSR        0x01
#define MODEM_SIGNAL_RTSCTS        0x02
#define MODEM_SIGNAL_RI            0x04
#define MODEM_SIGNAL_DCD           0x08

#define INVALID_SCN -1

using bluetooth::Uuid;

// Maximum number of RFCOMM channels (1-30 inclusive).
#define MAX_RFC_CHANNEL 30

// Maximum number of devices we can have an RFCOMM connection with.
#define MAX_RFC_SESSION 7
BluetoothSSInterface *gBTSSInterface = NULL;
#ifdef SS_STUB_ENABLED
BluetoothSSStubInterface *gBTSSStubInterface;
#endif

typedef enum {
  SENT_FAILED,
  SENT_NONE,
  SENT_PARTIAL,
  SENT_ALL,
} sent_status_t;

void btif_rfcomm_ss_callback(uint16_t event, char* p_param);
using namespace bluetooth::synergy::SynergyProto;
static void ss_srv_rfc_connect(int fd, const RawAddress* addr, int channel,
                              int status, int mtu);
static void ss_cli_rfc_connect(int fd, const RawAddress* addr, int channel,
                              int status, int mtu);
int ss_rfc_data_outgoing_size(int fd, int* size);
int ss_rfc_data_outgoing(int fd, uint8_t* buf, int size);
void ss_rfc_data_write_done(int fd, uint64_t length);

typedef struct {
  int outgoing_congest : 1;
  int pending_sdp_request : 1;
  int doing_sdp_request : 1;
  int server : 1;
  int connected : 1;
  int closing : 1;
} flags_t;

typedef struct {
  flags_t f;
  uint32_t id;  // Non-zero indicates a valid (in-use) slot.
  int security;
  int scn;  // Server channel number
  int scn_notified;
  RawAddress addr;
  int is_service_uuid_valid;
  Uuid service_uuid;
  char service_name[256];
  int fd;
  int app_fd;   // Temporary storage for the half of the socketpair that's sent
                // back to upper layers.
  int app_uid;  // UID of the app for which this socket was created.
  int mtu;
  uint8_t* packet;
  int sdp_handle;
  int rfc_handle;
  int rfc_port_handle;
  int role;
  list_t* incoming_queue;
  int new_srv_fd;
  bool is_server;
} rfc_slot_t;

struct PendingData
{
  PendingData(std::string m, int i, int j) { data = m; len = i; offset = j;}
  std::string data;
  int len;
  int offset;
};

bool ss_flush_incoming_que_on_wr_signal(rfc_slot_t* slot);
static sent_status_t ss_send_data_to_app(int fd, PendingData* p_data);

static rfc_slot_t rfc_slots[MAX_RFC_CHANNEL];
static uint32_t rfc_slot_id;
static volatile int pth = -1;  // poll thread handle
static std::recursive_mutex slot_lock;
static uid_set_t* uid_set = NULL;

static rfc_slot_t* find_free_slot(void);
static rfc_slot_t* find_rfc_slot_by_fd(int fd);
static rfc_slot_t* find_rfc_slot_by_scn(int scn);
static void cleanup_rfc_slot(rfc_slot_t* rs);
//static void jv_dm_cback(tBTA_JV_EVT event, tBTA_JV* p_data, uint32_t id);
//static uint32_t rfcomm_cback(tBTA_JV_EVT event, tBTA_JV* p_data,
//                           uint32_t rfcomm_slot_id);
static bool send_app_scn(rfc_slot_t* rs);

static bool is_init_done(void) { return pth != -1; }

bt_status_t btsock_rfc_init(int poll_thread_handle, uid_set_t* set) {

  pth = poll_thread_handle;
  uid_set = set;

  memset(rfc_slots, 0, sizeof(rfc_slots));
  for (size_t i = 0; i < ARRAY_SIZE(rfc_slots); ++i) {
    rfc_slots[i].scn = -1;
    rfc_slots[i].sdp_handle = 0;
    rfc_slots[i].fd = INVALID_FD;
    rfc_slots[i].app_fd = INVALID_FD;
    rfc_slots[i].incoming_queue = list_new(osi_free);
    CHECK(rfc_slots[i].incoming_queue != NULL);
  }
#if 0
  BTA_JvEnable(jv_dm_cback);
#endif
  BTIF_TRACE_DEBUG("%s", __func__);
  if(gBTSSInterface == NULL){
    gBTSSInterface = BluetoothSSInterface::getInstance();
    if (gBTSSInterface == NULL) {
        ALOGI("%s single stack interface Initialization failed",__func__);
    } else {
        ALOGI("%s registering rfcomm profile callback",__func__);
        gBTSSInterface->registerCallbacks(BT_PROFILE_SOCKETS_ID, btif_rfcomm_ss_callback);
    }
  }else{
	  ALOGI("%s: single stack interface is already created",__func__);
  }
#ifdef SS_STUB_ENABLED
  if(gBTSSStubInterface == NULL){
    gBTSSStubInterface = BluetoothSSStubInterface::getInstance();
    if (gBTSSStubInterface == NULL) {
        ALOGI("%s single stack stub interface Initialization failed",__func__);
    }
  }else{
	  ALOGI("%s: single stack stub interface is already created",__func__);
  }
#endif
  return BT_STATUS_SUCCESS;
}

void btsock_rfc_cleanup(void) {
  BTIF_TRACE_DEBUG("%s", __func__);
  if (gBTSSInterface != NULL) {
    gBTSSInterface->deregisterCallbacks(BT_PROFILE_SOCKETS_ID);
  }
  gBTSSInterface = NULL;

  pth = -1;
#if 0
  BTA_JvDisable();
#endif
  std::unique_lock<std::recursive_mutex> lock(slot_lock);
  for (size_t i = 0; i < ARRAY_SIZE(rfc_slots); ++i) {
    if (rfc_slots[i].id) cleanup_rfc_slot(&rfc_slots[i]);
    list_free(rfc_slots[i].incoming_queue);
    rfc_slots[i].incoming_queue = NULL;
  }
  uid_set = NULL;

}

void btif_rfcomm_ss_callback(uint16_t event, char* p_param) {
  ALOGI("btif_rfcomm_ss_callback :: event is :: %X",event);
  std::string resBufferString;
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
  switch (MSG_ID) {
    case BT_RFCOMM_SCN_CB: {
      ALOGI("%s: BT_RFCOMM_SCN_CB",__func__);
      ss_rfcomm_scn_callback rfcommScnCb;
      rfcommScnCb.ParseFromString(resBufferString);
      if (rfcommScnCb.has_sock_fd()) {
        uint32_t sock_fd = rfcommScnCb.sock_fd();
        ALOGI("%s: Recieved sock FD: %d",__func__, sock_fd);
        if(rfcommScnCb.has_scn()){
          uint32_t scn = rfcommScnCb.scn();
          ALOGI("%s: Recieved scn: %d",__func__, scn);
          std::unique_lock<std::recursive_mutex> lock(slot_lock);
          rfc_slot_t* rs = find_rfc_slot_by_fd((int)sock_fd);
          int new_scn = scn;

          if (rs && (new_scn != 0)) {
            rs->scn = new_scn;
            rs->id = new_scn;
            // Send channel ID to java layer
            if (!send_app_scn(rs)) {
              // closed
              APPL_TRACE_DEBUG("send_app_scn() failed, close rs->id:%d", rs->id);
              cleanup_rfc_slot(rs);
            }else {
              if (rs->is_service_uuid_valid == true) {
                // We already have data for SDP record, create it (RFC-only
                // profiles)
                //BTA_JvCreateRecordByUser(rs->id);
              } else {
                APPL_TRACE_DEBUG(
                "is_service_uuid_valid==false - don't set SDP-record, "
                "just start the RFCOMM server",
                rs->id);
                // now start the rfcomm server after sdp & channel # assigned
                //BTA_JvRfcommStartServer(rs->security, rs->role, rs->scn,
                //                    MAX_RFC_SESSION, rfcomm_cback, rs->id);
              }
            }
          }else if (rs) {
              APPL_TRACE_ERROR(
              "jv_dm_cback: Error: allocate channel %d, slot found:%p", rs->scn,
              rs);
              cleanup_rfc_slot(rs);
          }
        }
      }
      break;
    }
    case BT_RFCOMM_SRV_OPEN_CB: {
      ALOGI("%s: BT_RFCOMM_SRV_OPEN_CB",__func__);
	    RawAddress *bd_addr;
      uint32_t channel = 0;
      uint32_t sock_fd = 0;
      uint32_t mtu = 0;
      uint32_t status = 0;
      ss_rfcomm_srv_open_callback rfcommSrvOpenCb;
      rfcommSrvOpenCb.ParseFromString(resBufferString);
      if (rfcommSrvOpenCb.has_sock_fd()) {
        sock_fd = rfcommSrvOpenCb.sock_fd();
        ALOGI("%s: Recieved sock FD: %d",__func__, sock_fd);
        if(rfcommSrvOpenCb.has_channel()){
          channel = rfcommSrvOpenCb.channel();
          ALOGI("%s: Recieved scn: %d",__func__, channel);
        }
      }
      if (rfcommSrvOpenCb.has_addr()) {
        uint8_t* addr = (uint8_t*)rfcommSrvOpenCb.addr().c_str();
        bd_addr = (RawAddress*)addr;
      }
      if (rfcommSrvOpenCb.has_tx_mtu()) {
        mtu = rfcommSrvOpenCb.tx_mtu();
        ALOGI("tx mtu is :: %d",mtu);
      }
      if (rfcommSrvOpenCb.has_status()) {
        status = rfcommSrvOpenCb.status();
        ALOGI("status is :: %d",status);
      }
      ss_srv_rfc_connect(sock_fd, bd_addr, channel, status, mtu);
      break;
    }
    case BT_RFCOMM_CLIENT_CONNECT_CB: {
      ALOGI("%s: BT_RFCOMM_CLIENT_CONNECT_CB",__func__);
	  RawAddress bd_addr;
      uint32_t channel = 0;
      uint32_t sock_fd = 0;
      uint32_t mtu = 0;
      uint32_t status = 0;
      ss_rfcomm_cli_connect_callback rfcommCliConnCb;
      rfcommCliConnCb.ParseFromString(resBufferString);
      if (rfcommCliConnCb.has_sock_fd()) {
        sock_fd = rfcommCliConnCb.sock_fd();
        ALOGI("%s: Recieved sock FD: %d",__func__, sock_fd);
        if(rfcommCliConnCb.has_channel()) {
          channel = rfcommCliConnCb.channel();
          ALOGI("%s: Recieved scn: %d",__func__, channel);
        }
      }
      if (rfcommCliConnCb.has_addr()) {
        std::string bt_address = rfcommCliConnCb.addr();
        ALOGI("address is :: %s",bt_address.c_str());
        RawAddress::FromString(bt_address.c_str(), bd_addr);
      }
      if (rfcommCliConnCb.has_tx_mtu()) {
        mtu = rfcommCliConnCb.tx_mtu();
        ALOGI("tx mtu is :: %d",mtu);
      }
      if (rfcommCliConnCb.has_status()) {
        status = rfcommCliConnCb.status();
        ALOGI("status is :: %d",status);
      }
      ss_cli_rfc_connect(sock_fd, &bd_addr, channel, status, mtu);
      break;
    }
    case BT_RFCOMM_SOCKET_DATA_CB: {
      ALOGI("%s: BT_RFCOMM_SOCKET_DATA_CB",__func__);
	    int fd = -1;
      int len = 0;
      int app_uid = -1;
      uint64_t bytes_rx = 0;
      uint8_t *data;
      ss_rfcomm_data_callback rfcommDataCb;
      rfcommDataCb.ParseFromString(resBufferString);
      if (rfcommDataCb.has_channel()) {
        ALOGI("%s: Recieved channel: %d",__func__, (int)rfcommDataCb.channel());
        rfc_slot_t* slot = find_rfc_slot_by_scn((int)rfcommDataCb.channel());
        if (!slot){
          ALOGI("%s: RFC Slot is unavailable/closed",__func__);
          return;
        }
        fd = slot->fd;
        if (fd != -1) {
          if (rfcommDataCb.has_data()) {
            std::string data_string = rfcommDataCb.data();
            ALOGI("%s: Length received from slate :: %d",__func__,rfcommDataCb.data_len());
            ALOGI("%s: Recieved length: %d and data: %s",__func__, data_string.length(),data_string.c_str());
            bytes_rx = data_string.length();
            app_uid = slot->app_uid;
            PendingData *p_data = new PendingData(data_string,data_string.length(),0);
            if (list_is_empty(slot->incoming_queue)) {
              switch(ss_send_data_to_app(fd, p_data)){
                case SENT_NONE:
                case SENT_PARTIAL:
                ALOGI("%s: SENT_NONE or SENT_PARTIAL",__func__);
                // monitor the fd to get callback when app is ready to receive data
                list_append(slot->incoming_queue, p_data);
                btsock_thread_add_fd(pth, slot->fd, BTSOCK_RFCOMM, SOCK_THREAD_FD_WR,
                              slot->id);
                break;

                case SENT_ALL:
                  ALOGI("%s: SENT_ALL",__func__);
                break;

                case SENT_FAILED:
                  ALOGI("%s: SENT_FAILED",__func__);
                break;
              }
            }else{
                list_append(slot->incoming_queue, p_data);
            }
          }
        } else {
          ALOGE("channel not found :: %d", rfcommDataCb.channel());
        }
      }
      uid_set_add_rx(uid_set, app_uid, bytes_rx);
      break;
    }
    case BT_RFCOMM_DISCONNECT_SOCKET_CB:{
      ALOGI("%s: BT_RFCOMM_DISCONNECT_SOCKET_CB",__func__);
      uint32_t channel = 0;
      uint32_t sock_fd = 0;
      ss_rfcomm_disconnect_callback rfcommDisconnectCb;
      rfcommDisconnectCb.ParseFromString(resBufferString);
      if (rfcommDisconnectCb.has_channel()) {
        channel = rfcommDisconnectCb.channel();
        ALOGI("%s: Recieved scn: %d",__func__, channel);
      }

      if(rfcommDisconnectCb.has_sock_fd()){
        sock_fd = rfcommDisconnectCb.sock_fd();
        ALOGI("%s: Recieved sock FD: %d",__func__, sock_fd);
      }
      rfc_slot_t* slot = find_rfc_slot_by_scn((int)channel);
      cleanup_rfc_slot(slot);
    }
    break;
    default : {
      ALOGI("btif_rfcomm_ss_callback :: msg id %X :: unknow", MSG_ID);
      break;
    }
  }
}

static rfc_slot_t* find_free_slot(void) {
  for (size_t i = 0; i < ARRAY_SIZE(rfc_slots); ++i)
    if (rfc_slots[i].fd == INVALID_FD) return &rfc_slots[i];
  return NULL;
}

static rfc_slot_t* find_rfc_slot_by_id(uint32_t id) {
  CHECK(id != 0);

  for (size_t i = 0; i < ARRAY_SIZE(rfc_slots); ++i)
    if (rfc_slots[i].id == id) return &rfc_slots[i];

  LOG_ERROR(LOG_TAG, "%s unable to find RFCOMM slot id: %u", __func__, id);
  return NULL;
}

static rfc_slot_t* find_rfc_slot_by_fd(int fd){
  CHECK(fd != 0);

  for (size_t i = 0; i < ARRAY_SIZE(rfc_slots); ++i)
    if (rfc_slots[i].fd == fd) return &rfc_slots[i];

  LOG_ERROR(LOG_TAG, "%s unable to find RFCOMM slot fd: %u", __func__, fd);
  return NULL;
}

#if 0
static rfc_slot_t* find_rfc_slot_by_pending_sdp(void) {
  uint32_t min_id = UINT32_MAX;
  int slot = -1;
  for (size_t i = 0; i < ARRAY_SIZE(rfc_slots); ++i)
    if (rfc_slots[i].id && rfc_slots[i].f.pending_sdp_request &&
        rfc_slots[i].id < min_id) {
      min_id = rfc_slots[i].id;
      slot = i;
    }

  return (slot == -1) ? NULL : &rfc_slots[slot];
}
#endif

static bool is_requesting_sdp(void) {
  for (size_t i = 0; i < ARRAY_SIZE(rfc_slots); ++i)
    if (rfc_slots[i].id && rfc_slots[i].f.doing_sdp_request) return true;
  return false;
}

static rfc_slot_t* alloc_rfc_slot(const RawAddress* addr, const char* name,
                                  const Uuid& uuid, int channel, int flags,
                                  bool server) {
  int security = 0;
  if (flags & BTSOCK_FLAG_ENCRYPT)
    security |= server ? BTM_SEC_IN_ENCRYPT : BTM_SEC_OUT_ENCRYPT;
  if (flags & BTSOCK_FLAG_AUTH)
    security |= server ? BTM_SEC_IN_AUTHENTICATE : BTM_SEC_OUT_AUTHENTICATE;
  if (flags & BTSOCK_FLAG_AUTH_MITM)
    security |= server ? BTM_SEC_IN_MITM : BTM_SEC_OUT_MITM;
  if (flags & BTSOCK_FLAG_AUTH_16_DIGIT)
    security |= BTM_SEC_IN_MIN_16_DIGIT_PIN;

  rfc_slot_t* slot = find_free_slot();
  if (!slot) {
    LOG_ERROR(LOG_TAG, "%s unable to find free RFCOMM slot.", __func__);
    return NULL;
  }

  int fds[2] = {INVALID_FD, INVALID_FD};
  if (socketpair(AF_LOCAL, SOCK_STREAM, 0, fds) == -1) {
    LOG_ERROR(LOG_TAG, "%s error creating socketpair: %s", __func__,
              strerror(errno));
    return NULL;
  }

  ALOGI("alloc_rfc_slot fds[0] is :: %d fds[1] is :: %d",fds[0],fds[1]);
  // Increment slot id and make sure we don't use id=0.
  if (UINT32_MAX == rfc_slot_id) {
    rfc_slot_id = 1;
  } else {
    ++rfc_slot_id;
  }

  slot->fd = fds[0];
  slot->app_fd = fds[1];
  slot->security = security;
  slot->scn = channel;
  slot->app_uid = -1;

  slot->is_service_uuid_valid = !uuid.IsEmpty();
  slot->service_uuid = uuid;

  if (name && *name) {
    strlcpy(slot->service_name, name, sizeof(slot->service_name));
  } else {
    memset(slot->service_name, 0, sizeof(slot->service_name));
  }
  if (addr) slot->addr = *addr;

  slot->id = rfc_slot_id;
  slot->f.server = server;

  return slot;
}

static rfc_slot_t* create_srv_accept_rfc_slot(rfc_slot_t* srv_rs,
                                              const RawAddress* addr,
                                              int open_handle,
                                              int new_listen_handle) {
  rfc_slot_t* accept_rs = alloc_rfc_slot(
      addr, srv_rs->service_name, srv_rs->service_uuid, srv_rs->scn, 0, false);
  if (!accept_rs) {
    LOG_ERROR(LOG_TAG, "%s unable to allocate RFCOMM slot.", __func__);
    return NULL;
  }

  accept_rs->f.server = false;
  accept_rs->f.connected = true;
  accept_rs->security = srv_rs->security;
  accept_rs->mtu = srv_rs->mtu;
  accept_rs->role = srv_rs->role;
  accept_rs->rfc_handle = open_handle;
  accept_rs->rfc_port_handle = -1;//BTA_JvRfcommGetPortHdl(open_handle);
  accept_rs->app_uid = srv_rs->app_uid;
  accept_rs->new_srv_fd = srv_rs->fd;
  accept_rs->is_server = true;

  srv_rs->rfc_handle = new_listen_handle;
  srv_rs->rfc_port_handle = -1;//BTA_JvRfcommGetPortHdl(new_listen_handle);

  //CHECK(accept_rs->rfc_port_handle != srv_rs->rfc_port_handle);

  // now swap the slot id
  uint32_t new_listen_id = accept_rs->id;
  accept_rs->id = srv_rs->id;
  srv_rs->id = new_listen_id;
  srv_rs->scn = 0;

  return accept_rs;
}

bt_status_t btsock_rfc_listen(const char* service_name,
                              const Uuid* service_uuid, int channel,
                              int* sock_fd, int flags, int app_uid) {
  CHECK(sock_fd != NULL);
  CHECK((service_uuid != NULL) ||
        (channel >= 1 && channel <= MAX_RFC_CHANNEL) ||
        ((flags & BTSOCK_FLAG_NO_SDP) != 0));

  BTIF_TRACE_DEBUG("%s", __func__);

  *sock_fd = INVALID_FD;

  // TODO(sharvil): not sure that this check makes sense; seems like a logic
  // error to call
  // functions on RFCOMM sockets before initializing the module. Probably should
  // be an assert.
  // if (!is_init_done()){
  //   return BT_STATUS_NOT_READY;
  // }
  if ((flags & BTSOCK_FLAG_NO_SDP) == 0) {
    if (!service_uuid || service_uuid->IsEmpty()) {
        //if (!is_reserved_rfc_channel(channel)) {
      APPL_TRACE_DEBUG(
          "%s: service_uuid not set AND BTSOCK_FLAG_NO_SDP is not set - "
          "changing to SPP",
          __func__);
            service_uuid =
                &UUID_SPP;  // Use serial port profile to listen to specified channel
         //}
    } else {
      // Check the service_uuid. overwrite the channel # if reserved
      int reserved_channel = 0;//get_reserved_rfc_channel(*service_uuid);
      if (reserved_channel > 0) {
        channel = reserved_channel;
      }
    }
  }
  if (!service_uuid) {
    LOG_ERROR(LOG_TAG, "%s service_uuid is NULL.", __func__);
    return BT_STATUS_FAIL;
  }

  std::unique_lock<std::recursive_mutex> lock(slot_lock);
  
  rfc_slot_t* slot =
      alloc_rfc_slot(NULL, service_name, *service_uuid, channel, flags, true);

  if(!slot){
    LOG_ERROR(LOG_TAG, "%s unable to allocate RFCOMM slot.", __func__);
    return BT_STATUS_FAIL;
  }

  *sock_fd = slot->app_fd;
  slot->app_fd = INVALID_FD;  // Drop our reference to the fd.
  slot->app_uid = app_uid;
  btsock_thread_add_fd(pth, slot->fd, BTSOCK_RFCOMM, SOCK_THREAD_FD_EXCEPTION,
                       slot->id);

  /*Sending to SS*/
  uint8_t set_remprop_msg[MAX_LENGTH_WITH_PROTO_NONE];
  //adding msg_id
  uint16_t msg_id = BT_RFCOMM_CREATE_SOCKET;
  set_remprop_msg[0] = msg_id & 0xff;
  set_remprop_msg[1] = (msg_id >> 8);
  std::string protoMsg;
  ss_create_socket_channel createSocketCh;
  if ((flags & BTSOCK_FLAG_LE_COC) == 0) {
    createSocketCh.set_service_name(service_name);
  } else {
    createSocketCh.set_service_name("");
  }
  createSocketCh.set_service_uuid(service_uuid->ToString());
  createSocketCh.set_channel(channel);
  createSocketCh.set_flags(flags);
  createSocketCh.set_sock_fd(slot->fd);
  createSocketCh.SerializeToString(&protoMsg);

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
  ALOGI("%s: BT_RFCOMM_CREATE_SOCKET length: %d and data: %s",__func__, msgStr.length(),msgStr.c_str());
#ifndef SS_STUB_ENABLED
  gBTSSInterface->postTxMsg(msgStr);
#else
  gBTSSStubInterface->postTxMsg(msgStr);
#endif
  return BT_STATUS_SUCCESS;
}

bt_status_t btsock_rfc_connect(const RawAddress* bd_addr,
                               const Uuid* service_uuid, int channel,
                               int* sock_fd, int flags, int app_uid) {

  CHECK(sock_fd != NULL);
  CHECK((service_uuid != NULL) || (channel >= 1 && channel <= MAX_RFC_CHANNEL));
  BTIF_TRACE_DEBUG("%s", __func__);

  *sock_fd = INVALID_FD;

  // TODO(sharvil): not sure that this check makes sense; seems like a logic
  // error to call
  // functions on RFCOMM sockets before initializing the module. Probably should
  // be an assert.
  // if (!is_init_done()) return BT_STATUS_NOT_READY;

  std::unique_lock<std::recursive_mutex> lock(slot_lock);

  rfc_slot_t* slot =
      alloc_rfc_slot(bd_addr, NULL, *service_uuid, channel, flags, false);
  if (!slot) {
    LOG_ERROR(LOG_TAG, "%s unable to allocate RFCOMM slot.", __func__);
    return BT_STATUS_FAIL;
  }

  // if (!service_uuid || service_uuid->IsEmpty()) {
  //   tBTA_JV_STATUS ret =
  //       BTA_JvRfcommConnect(slot->security, slot->role, slot->scn, slot->addr,
  //                           rfcomm_cback, slot->id);
  //   if (ret != BTA_JV_SUCCESS) {
  //     LOG_ERROR(LOG_TAG, "%s unable to initiate RFCOMM connection: %d",
  //               __func__, ret);
  //     cleanup_rfc_slot(slot);
  //     return BT_STATUS_FAIL;
  //   }

  //   if (!send_app_scn(slot)) {
  //     LOG_ERROR(LOG_TAG, "%s unable to send channel number.", __func__);
  //     cleanup_rfc_slot(slot);
  //     return BT_STATUS_FAIL;
  //   }
  // } else {
  //   if (!is_requesting_sdp()) {
  //     BTA_JvStartDiscovery(*bd_addr, 1, service_uuid, slot->id);
  //     slot->f.pending_sdp_request = false;
  //     slot->f.doing_sdp_request = true;
  //   } else {
  //     slot->f.pending_sdp_request = true;
  //     slot->f.doing_sdp_request = false;
  //   }
  // }

  *sock_fd = slot->app_fd;    // Transfer ownership of fd to caller.
  slot->app_fd = INVALID_FD;  // Drop our reference to the fd.
  slot->app_uid = app_uid;
  btsock_thread_add_fd(pth, slot->fd, BTSOCK_RFCOMM, SOCK_THREAD_FD_RD,
                       slot->id);

  /*Sending connect request to SS*/
  uint8_t set_remprop_msg[MAX_LENGTH_WITH_PROTO_NONE];
  //adding msg_id
  uint16_t msg_id = BT_RFCOMM_CONNECT_SOCKET;
  set_remprop_msg[0] = msg_id & 0xff;
  set_remprop_msg[1] = (msg_id >> 8);

  std::string protoMsg;
  ss_connect_socket connSocketCh;
  connSocketCh.set_bd_addr(ToRawString(bd_addr).c_str());
  connSocketCh.set_service_uuid(service_uuid->ToString());
  connSocketCh.set_channel(channel);
  connSocketCh.set_flags(flags);
  connSocketCh.set_sock_fd(slot->fd);
  connSocketCh.SerializeToString(&protoMsg);

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
  ALOGI("%s: BT_RFCOMM_CONNECT_SOCKET length: %d and data: %s",__func__, msgStr.length(),msgStr.c_str());
#ifndef SS_STUB_ENABLED
  gBTSSInterface->postTxMsg(msgStr);
#else
  gBTSSStubInterface->postTxMsg(msgStr);
#endif

  return BT_STATUS_SUCCESS;
}

#if 0
static int create_server_sdp_record(rfc_slot_t* slot) {
  if (slot->scn == 0) {
    return false;
  }
  slot->sdp_handle =
      add_rfc_sdp_rec(slot->service_name, slot->service_uuid, slot->scn);
  return (slot->sdp_handle > 0);
}
#endif

static void free_rfc_slot_scn(rfc_slot_t* slot) {
  if (slot->scn <= 0) return;

  if (slot->f.server && !slot->f.closing && slot->rfc_handle) {
    //BTA_JvRfcommStopServer(slot->rfc_handle, slot->id);
    slot->rfc_handle = 0;
  }

 // if (slot->f.server) BTM_FreeSCN(slot->scn);
  slot->scn = 0;
}

static void cleanup_rfc_slot(rfc_slot_t* slot) {
  if(slot){
    ALOGI("%s: slot->fd is :: %d , slot->scn is :: %d , slot->new_srv_fd is :: %d , slot->is_server is :: %d",__func__,
    slot->fd, slot->scn,slot->new_srv_fd,slot->is_server);
  }

  if(((slot->new_srv_fd == 0 && !slot->is_server) ||
    (slot->new_srv_fd != 0 && slot->is_server)) && slot->scn != 0){
    /*Sending disconnect request*/
    uint8_t disconnect_socket[MAX_LENGTH_WITH_PROTO_NONE];
    //adding msg_id
    uint16_t msg_id = BT_RFCOMM_DISCONNECT_SOCKET;
    disconnect_socket[0] = msg_id & 0xff;
    disconnect_socket[1] = (msg_id >> 8);

    std::string protoMsg;
    ss_disconnect_socket disconnSocketCh;
    disconnSocketCh.set_channel(slot->scn);
    disconnSocketCh.set_sock_fd(slot->fd);
    disconnSocketCh.SerializeToString(&protoMsg);

    ALOGI("%s: protoMsg length is %d", __func__, protoMsg.length());
    //adding length
    uint16_t length = protoMsg.length();
    disconnect_socket[2] = length & 0xff;
    disconnect_socket[3] = (length >> 8);
    //adding proto_encode
    uint16_t proto_encode = PROTO_ENC_DEC;
    disconnect_socket[4] = proto_encode & 0xff;
    disconnect_socket[5] = (proto_encode >> 8);
    char resBuffer[MAX_LENGTH_WITH_PROTO_NONE];
    memcpy(resBuffer, (char *) disconnect_socket, MAX_LENGTH_WITH_PROTO_NONE);
    std::string msgStr(resBuffer, MAX_LENGTH_WITH_PROTO_NONE);
    msgStr.append(protoMsg);
    ALOGI("%s: BT_RFCOMM_DISCONNECT_SOCKET length: %d and data: %s",__func__, msgStr.length(),msgStr.c_str());
  #ifndef SS_STUB_ENABLED
    gBTSSInterface->postTxMsg(msgStr);
  #else
    gBTSSStubInterface->postTxMsg(msgStr);
  #endif
  }

  if (slot->fd != INVALID_FD) {
    shutdown(slot->fd, SHUT_RDWR);
    close(slot->fd);
    slot->fd = INVALID_FD;
  }

  if (slot->app_fd != INVALID_FD) {
    close(slot->app_fd);
    slot->app_fd = INVALID_FD;
  }

  if (slot->sdp_handle > 0) {
    //del_rfc_sdp_rec(slot->sdp_handle);
    slot->sdp_handle = 0;
  }

  if (slot->rfc_handle && !slot->f.closing && !slot->f.server) {
   // BTA_JvRfcommClose(slot->rfc_handle, slot->id);
    slot->rfc_handle = 0;
  }

  free_rfc_slot_scn(slot);
  list_clear(slot->incoming_queue);

  slot->rfc_port_handle = 0;
  memset(&slot->f, 0, sizeof(slot->f));
  slot->id = 0;
  slot->scn_notified = false;
}


static bool send_app_scn(rfc_slot_t* slot) {
  if (slot->scn_notified == true) {
    // already send, just return success.
    return true;
  }
  slot->scn_notified = true;
  return sock_send_all(slot->fd, (const uint8_t*)&slot->scn,
                       sizeof(slot->scn)) == sizeof(slot->scn);
}

static bool send_app_connect_signal(int fd, const RawAddress* addr, int channel,
                                    int status, int send_fd, int tx_mtu) {
  sock_connect_signal_t cs;
  cs.size = sizeof(cs);
  cs.bd_addr = *addr;
  cs.channel = channel;
  cs.status = status;
  cs.max_rx_packet_size = tx_mtu;
  cs.max_tx_packet_size = 0;  // not used for RFCOMM
  if (send_fd == INVALID_FD)
    return sock_send_all(fd, (const uint8_t*)&cs, sizeof(cs)) == sizeof(cs);

  return sock_send_fd(fd, (const uint8_t*)&cs, sizeof(cs), send_fd) ==
         sizeof(cs);
}

static void ss_srv_rfc_connect (int fd, const RawAddress* addr, int channel,
                              int status, int mtu) {
  std::unique_lock<std::recursive_mutex> lock(slot_lock);
  rfc_slot_t* accept_rs;
  rfc_slot_t* srv_rs = find_rfc_slot_by_scn(channel);
  if (!srv_rs) return;

  srv_rs->mtu = mtu;
  accept_rs = create_srv_accept_rfc_slot(
    srv_rs, addr, channel, -1);
  if (!accept_rs) return;

  // Start monitoring the socket.
  btsock_thread_add_fd(pth, srv_rs->fd, BTSOCK_RFCOMM, SOCK_THREAD_FD_EXCEPTION,
                       srv_rs->id);
  btsock_thread_add_fd(pth, accept_rs->fd, BTSOCK_RFCOMM, SOCK_THREAD_FD_RD,
                       accept_rs->id);

  send_app_connect_signal(srv_rs->fd, &accept_rs->addr, srv_rs->scn, 0,
                          accept_rs->app_fd, mtu);

  accept_rs->app_fd =
      INVALID_FD;  // Ownership of the application fd has been transferred.
}

static void ss_cli_rfc_connect (int fd, const RawAddress* addr, int channel,
                              int status, int mtu) {
  std::unique_lock<std::recursive_mutex> lock(slot_lock);
  rfc_slot_t* client_rs = find_rfc_slot_by_scn(channel);
  client_rs->mtu = mtu;
  /*if (status != PORT_OPEN_SUCCUESS) {
    LOG_ERROR(LOG_TAG, "%s slate returned failure Status : %d",__func__, status);
    return;
  }*/

  if (send_app_connect_signal(fd, addr, channel, 0, -1, mtu)) {
    LOG_DEBUG (LOG_TAG, "sent send_app_connect_signal");
    client_rs->f.connected = true;
  } else {
    LOG_ERROR(LOG_TAG, "%s unable to send connect completion signal to caller.",
              __func__);
  }
}

#if 0
static void on_cl_rfc_init(tBTA_JV_RFCOMM_CL_INIT* p_init, uint32_t id) {
  std::unique_lock<std::recursive_mutex> lock(slot_lock);
  rfc_slot_t* slot = find_rfc_slot_by_id(id);
  if (!slot) return;

  if (p_init->status == BTA_JV_SUCCESS) {
    slot->rfc_handle = p_init->handle;
  } else {
    cleanup_rfc_slot(slot);
  }
}

static void on_srv_rfc_listen_started(tBTA_JV_RFCOMM_START* p_start,
                                      uint32_t id) {
  std::unique_lock<std::recursive_mutex> lock(slot_lock);
  rfc_slot_t* slot = find_rfc_slot_by_id(id);
  if (!slot) return;

  if (p_start->status == BTA_JV_SUCCESS) {
    slot->rfc_handle = p_start->handle;
  } else {
    cleanup_rfc_slot(slot);
  }
}

static uint32_t on_srv_rfc_connect(tBTA_JV_RFCOMM_SRV_OPEN* p_open,
                                   uint32_t id) {
  std::unique_lock<std::recursive_mutex> lock(slot_lock);
  rfc_slot_t* accept_rs;
  rfc_slot_t* srv_rs = find_rfc_slot_by_id(id);
  if (!srv_rs) return 0;

  accept_rs = create_srv_accept_rfc_slot(
      srv_rs, &p_open->rem_bda, p_open->handle, p_open->new_listen_handle);
  if (!accept_rs) return 0;

  // Start monitoring the socket.
  btsock_thread_add_fd(pth, srv_rs->fd, BTSOCK_RFCOMM, SOCK_THREAD_FD_EXCEPTION,
                       srv_rs->id);
  btsock_thread_add_fd(pth, accept_rs->fd, BTSOCK_RFCOMM, SOCK_THREAD_FD_RD,
                       accept_rs->id);
  LOG_DEBUG(LOG_TAG, "%s  mtu = %d ", __func__,p_open->mtu);
  send_app_connect_signal(srv_rs->fd, &accept_rs->addr, srv_rs->scn, 0,
                          accept_rs->app_fd, p_open->mtu);
  accept_rs->app_fd =
      INVALID_FD;  // Ownership of the application fd has been transferred.
  return srv_rs->id;
}

static void on_cli_rfc_connect(tBTA_JV_RFCOMM_OPEN* p_open, uint32_t id) {
  std::unique_lock<std::recursive_mutex> lock(slot_lock);
  rfc_slot_t* slot = find_rfc_slot_by_id(id);
  if (!slot) return;

  if (p_open->status != BTA_JV_SUCCESS) {
    cleanup_rfc_slot(slot);
    return;
  }

  slot->rfc_port_handle = BTA_JvRfcommGetPortHdl(p_open->handle);
  slot->addr = p_open->rem_bda;
  LOG_DEBUG(LOG_TAG, "%s  mtu = %d ", __func__,p_open->mtu);
  if (send_app_connect_signal(slot->fd, &slot->addr, slot->scn, 0, -1, p_open->mtu)) {
    slot->f.connected = true;
  } else {
    LOG_ERROR(LOG_TAG, "%s unable to send connect completion signal to caller.",
              __func__);
  }
}

static void on_rfc_close(UNUSED_ATTR tBTA_JV_RFCOMM_CLOSE* p_close,
                         uint32_t id) {
  std::unique_lock<std::recursive_mutex> lock(slot_lock);

  // rfc_handle already closed when receiving rfcomm close event from stack.
  rfc_slot_t* slot = find_rfc_slot_by_id(id);
  if (slot) cleanup_rfc_slot(slot);
}

static void on_rfc_write_done(tBTA_JV_RFCOMM_WRITE* p, uint32_t id) {
  if (p->status != BTA_JV_SUCCESS) {
    LOG_ERROR(LOG_TAG, "%s error writing to RFCOMM socket with slot %u.",
              __func__, p->req_id);
    return;
  }

  int app_uid = -1;
  std::unique_lock<std::recursive_mutex> lock(slot_lock);

  rfc_slot_t* slot = find_rfc_slot_by_id(id);
  if (slot) {
    app_uid = slot->app_uid;
    if (!slot->f.outgoing_congest) {
      btsock_thread_add_fd(pth, slot->fd, BTSOCK_RFCOMM, SOCK_THREAD_FD_RD,
                           slot->id);
    }
  }

  uid_set_add_tx(uid_set, app_uid, p->len);
}

static void on_rfc_outgoing_congest(tBTA_JV_RFCOMM_CONG* p, uint32_t id) {
  std::unique_lock<std::recursive_mutex> lock(slot_lock);

  rfc_slot_t* slot = find_rfc_slot_by_id(id);
  if (slot) {
    slot->f.outgoing_congest = p->cong ? 1 : 0;
    if (!slot->f.outgoing_congest)
      btsock_thread_add_fd(pth, slot->fd, BTSOCK_RFCOMM, SOCK_THREAD_FD_RD,
                           slot->id);
  }
}

static uint32_t rfcomm_cback(tBTA_JV_EVT event, tBTA_JV* p_data,
                             uint32_t rfcomm_slot_id) {
  uint32_t id = 0;

  switch (event) {
    case BTA_JV_RFCOMM_START_EVT:
      on_srv_rfc_listen_started(&p_data->rfc_start, rfcomm_slot_id);
      break;

    case BTA_JV_RFCOMM_CL_INIT_EVT:
      on_cl_rfc_init(&p_data->rfc_cl_init, rfcomm_slot_id);
      break;

    case BTA_JV_RFCOMM_OPEN_EVT:
      BTA_JvSetPmProfile(p_data->rfc_open.handle, BTA_JV_PM_ID_1,
                         BTA_JV_CONN_OPEN);
      on_cli_rfc_connect(&p_data->rfc_open, rfcomm_slot_id);
      break;

    case BTA_JV_RFCOMM_SRV_OPEN_EVT:
      BTA_JvSetPmProfile(p_data->rfc_srv_open.handle, BTA_JV_PM_ALL,
                         BTA_JV_CONN_OPEN);
      id = on_srv_rfc_connect(&p_data->rfc_srv_open, rfcomm_slot_id);
      if (id == 0) {
        LOG(ERROR) << __func__ << " Failed to assign new slot";
      }
      break;

    case BTA_JV_RFCOMM_CLOSE_EVT:
      APPL_TRACE_DEBUG("BTA_JV_RFCOMM_CLOSE_EVT: rfcomm_slot_id:%d",
                       rfcomm_slot_id);
      on_rfc_close(&p_data->rfc_close, rfcomm_slot_id);
      break;

    case BTA_JV_RFCOMM_WRITE_EVT:
      on_rfc_write_done(&p_data->rfc_write, rfcomm_slot_id);
      break;

    case BTA_JV_RFCOMM_CONG_EVT:
      on_rfc_outgoing_congest(&p_data->rfc_cong, rfcomm_slot_id);
      break;

    case BTA_JV_RFCOMM_DATA_IND_EVT:
      // Unused.
      break;

    default:
      LOG_ERROR(LOG_TAG, "%s unhandled event %d, slot id: %u", __func__, event,
                rfcomm_slot_id);
      break;
  }
  return id;
}

static void jv_dm_cback(tBTA_JV_EVT event, tBTA_JV* p_data, uint32_t id) {
  switch (event) {
    case BTA_JV_GET_SCN_EVT: {
      std::unique_lock<std::recursive_mutex> lock(slot_lock);
      rfc_slot_t* rs = find_rfc_slot_by_id(id);
      int new_scn = p_data->scn;

      if (rs && (new_scn != 0)) {
        rs->scn = new_scn;
        /* BTA_JvCreateRecordByUser will only create a record if a UUID is
         * specified,
         * else it just allocate a RFC channel and start the RFCOMM thread -
         * needed
         * for the java
         * layer to get a RFCOMM channel.
         * If uuid is null the create_sdp_record() will be called from Java when
         * it
         * has received the RFCOMM and L2CAP channel numbers through the
         * sockets.*/

        // Send channel ID to java layer
        if (!send_app_scn(rs)) {
          // closed
          APPL_TRACE_DEBUG("send_app_scn() failed, close rs->id:%d", rs->id);
          cleanup_rfc_slot(rs);
        } else {
          if (rs->is_service_uuid_valid == true) {
            // We already have data for SDP record, create it (RFC-only
            // profiles)
            BTA_JvCreateRecordByUser(rs->id);
          } else {
            APPL_TRACE_DEBUG(
                "is_service_uuid_valid==false - don't set SDP-record, "
                "just start the RFCOMM server",
                rs->id);
            // now start the rfcomm server after sdp & channel # assigned
            BTA_JvRfcommStartServer(rs->security, rs->role, rs->scn,
                                    MAX_RFC_SESSION, rfcomm_cback, rs->id);
          }
        }
      } else if (rs) {
        APPL_TRACE_ERROR(
            "jv_dm_cback: Error: allocate channel %d, slot found:%p", rs->scn,
            rs);
        cleanup_rfc_slot(rs);
      }
      break;
    }
    case BTA_JV_GET_PSM_EVT: {
      APPL_TRACE_DEBUG("Received PSM: 0x%04x", p_data->psm);
      on_l2cap_psm_assigned(id, p_data->psm);
      break;
    }
    case BTA_JV_CREATE_RECORD_EVT: {
      std::unique_lock<std::recursive_mutex> lock(slot_lock);
      rfc_slot_t* slot = find_rfc_slot_by_id(id);

      if (slot && create_server_sdp_record(slot)) {
        // Start the rfcomm server after sdp & channel # assigned.
        BTA_JvRfcommStartServer(slot->security, slot->role, slot->scn,
                                MAX_RFC_SESSION, rfcomm_cback, slot->id);
      } else if (slot) {
        APPL_TRACE_ERROR("jv_dm_cback: cannot start server, slot found:%p",
                         slot);
        cleanup_rfc_slot(slot);
      }
      break;
    }

    case BTA_JV_DISCOVERY_COMP_EVT: {
      std::unique_lock<std::recursive_mutex> lock(slot_lock);
      rfc_slot_t* slot = find_rfc_slot_by_id(id);
      if (p_data->disc_comp.status == BTA_JV_SUCCESS && p_data->disc_comp.scn) {
        if (slot && slot->f.doing_sdp_request) {
          // Establish the connection if we successfully looked up a channel
          // number to connect to.
          if (BTA_JvRfcommConnect(slot->security, slot->role,
                                  p_data->disc_comp.scn, slot->addr,
                                  rfcomm_cback, slot->id) == BTA_JV_SUCCESS) {
            slot->scn = p_data->disc_comp.scn;
            slot->f.doing_sdp_request = false;
            if (!send_app_scn(slot)) cleanup_rfc_slot(slot);
          } else {
            cleanup_rfc_slot(slot);
          }
        } else if (slot) {
          // TODO(sharvil): this is really a logic error and we should probably
          // assert.
          LOG_ERROR(LOG_TAG,
                    "%s SDP response returned but RFCOMM slot %d did not "
                    "request SDP record.",
                    __func__, id);
        }
      } else if (slot) {
        cleanup_rfc_slot(slot);
      }

      // Find the next slot that needs to perform an SDP request and service it.
      slot = find_rfc_slot_by_pending_sdp();
      if (slot) {
        BTA_JvStartDiscovery(slot->addr, 1, &slot->service_uuid, slot->id);
        slot->f.pending_sdp_request = false;
        slot->f.doing_sdp_request = true;
      }
      break;
    }

    default:
      APPL_TRACE_DEBUG("unhandled event:%d, slot id:%d", event, id);
      break;
  }
}
#endif

static sent_status_t send_data_to_app(int fd, BT_HDR* p_buf) {
  if (p_buf->len == 0) return SENT_ALL;

  ssize_t sent;
  OSI_NO_INTR(
      sent = send(fd, p_buf->data + p_buf->offset, p_buf->len, MSG_DONTWAIT));

  if (sent == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return SENT_NONE;
    LOG_ERROR(LOG_TAG, "%s error writing RFCOMM data back to app: %s", __func__,
              strerror(errno));
    return SENT_FAILED;
  }

  if (sent == 0) return SENT_FAILED;

  if (sent == p_buf->len) return SENT_ALL;

  p_buf->offset += sent;
  p_buf->len -= sent;
  return SENT_PARTIAL;
}

static sent_status_t ss_send_data_to_app(int fd, PendingData* p_buf) {
  rfc_slot_t* slot = find_rfc_slot_by_fd(fd);
  if (!slot){
    ALOGI("%s: RFC Slot is unavailable/closed",__func__);
    return SENT_FAILED;
  }
  if (p_buf->len == 0) return SENT_ALL;
  uint8_t* data = (uint8_t*)p_buf->data.c_str();
  ssize_t sent;
  OSI_NO_INTR(sent = send(fd, data + p_buf->offset, p_buf->len, MSG_DONTWAIT));
  if (sent == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK){
      LOG_DEBUG (LOG_TAG, "Sent None :: errno is :: %s",strerror(errno));
      return SENT_NONE;
    }
    LOG_ERROR(LOG_TAG, "%s error writing RFCOMM data back to app: %s", __func__,
              strerror(errno));
    return SENT_FAILED;
  }
  if (sent == 0) return SENT_FAILED;

  if (sent == p_buf->len) return SENT_ALL;

  p_buf->offset += sent;
  p_buf->len -= sent;
  return SENT_PARTIAL;
}

static bool flush_incoming_que_on_wr_signal(rfc_slot_t* slot) {
  while (!list_is_empty(slot->incoming_queue)) {
    BT_HDR* p_buf = (BT_HDR*)list_front(slot->incoming_queue);
    switch (send_data_to_app(slot->fd, p_buf)) {
      case SENT_NONE:
      case SENT_PARTIAL:
        // monitor the fd to get callback when app is ready to receive data
        btsock_thread_add_fd(pth, slot->fd, BTSOCK_RFCOMM, SOCK_THREAD_FD_WR,
                             slot->id);
        return true;

      case SENT_ALL:
        list_remove(slot->incoming_queue, p_buf);
        break;

      case SENT_FAILED:
        list_remove(slot->incoming_queue, p_buf);
        return false;
    }
  }

  // app is ready to receive data, tell stack to start the data flow
  // fix me: need a jv flow control api to serialize the call in stack
  APPL_TRACE_DEBUG(
      "enable data flow, rfc_handle:0x%x, rfc_port_handle:0x%x, user_id:%d",
      slot->rfc_handle, slot->rfc_port_handle, slot->id);
  //PORT_FlowControl_MaxCredit(slot->rfc_port_handle, true);
  return true;
}

void btsock_rfc_signaled(UNUSED_ATTR int fd, int type, int flags, uint32_t user_id) {
  bool need_close = false;
  std::unique_lock<std::recursive_mutex> lock(slot_lock);
  int size = 0;
  int channel;
  rfc_slot_t* slot = find_rfc_slot_by_fd(fd);
  if (!slot) return;
  channel = slot->scn;
  int new_srv_fd = slot->new_srv_fd;
  ALOGI("new_srv_fd is :: %d",new_srv_fd);
  if (flags & SOCK_THREAD_FD_RD){
    if(slot->f.connected){
    ALOGI("Data available from App on FD :: %d and channel :: %d and new_srv_fd is %d and slot->mtu is %d",fd,channel,new_srv_fd,slot->mtu);
    if (ss_rfc_data_outgoing_size(fd, &size)) {
        ALOGI("%s fd is :: %d size is :: %d",__func__,fd,size);
        uint8_t data[size];
        if (ss_rfc_data_outgoing(fd, data, size)) {
          std::string data_string((char*)data,size);
          int max_data_size_glink = slot->mtu;
          ALOGI("%s num_of_iterations expected is :: %d",__func__,data_string.size()/max_data_size_glink + 1);
          int iterations = 0;
          for(unsigned i=0; i<data_string.size(); i+=max_data_size_glink){
            iterations++;
            std::string data_string_sub = data_string.substr(i,max_data_size_glink);
            /*Sending data write to SS*/
            uint8_t set_remprop_msg[MAX_LENGTH_WITH_PROTO_NONE];
            //adding msg_id
            uint16_t msg_id = BT_RFCOMM_WRITE_SOCKET_DATA;
            set_remprop_msg[0] = msg_id & 0xff;
            set_remprop_msg[1] = (msg_id >> 8);
            std::string protoMsg;
            ss_write_rfcomm_data rfcommData;
            if(slot->is_server){
              rfcommData.set_sock_fd(new_srv_fd);
            }else{
              rfcommData.set_sock_fd(fd);
            }
            rfcommData.set_channel(channel);
            rfcommData.set_data_len(data_string_sub.size());
            rfcommData.set_data(data_string_sub);
            rfcommData.SerializeToString(&protoMsg);
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
            ALOGI("%s: BT_RFCOMM_WRITE_SOCKET_DATA proto length: %d and payload length: %d",__func__, msgStr.size(), data_string_sub.size());
          #ifndef SS_STUB_ENABLED
            if (type == BTSOCK_L2CAP || type ==  BTSOCK_L2CAP_LE) {
              int result = gBTSSInterface->postLeDataChTxMsg(msgStr);
              ALOGI("%s: LE Data Write, result is :: %d",__func__,result);
            } else {
              int result = gBTSSInterface->postDataChTxMsg(msgStr);
              ALOGI("%s: result is :: %d",__func__,result);
            }
          #else
            gBTSSStubInterface->postTxMsg(msgStr);
          #endif
          }
          ALOGI("%s: total iterations is :: %d",__func__, iterations);
          ss_rfc_data_write_done(fd,data_string.size());
        } else {
          ALOGE("%s: ss_rfc_data_outgoing returned fail",__func__);
        }
      }else {
        ALOGE("%s: ss_rfc_data_outgoing_size returned fail",__func__);
      }
    }else{
      ALOGI("socket signaled for read while disconnected fd %d",new_srv_fd);
      need_close = true;
    }
  }

  if (flags & SOCK_THREAD_FD_WR) {
    if(slot->f.connected){
      ALOGI("App is ready to receive more data");
      ss_flush_incoming_que_on_wr_signal(slot);
    }else{
      ALOGI("socket signaled for write while disconnected fd %d",new_srv_fd);
      need_close = true;
    }
  }

  if (need_close || (flags & SOCK_THREAD_FD_EXCEPTION)) {
    // Clean up if there's no data pending.
    int size = 0;
    if (need_close || ioctl(slot->fd, FIONREAD, &size) != 0 || !size)
      cleanup_rfc_slot(slot);
  }
}

bool ss_flush_incoming_que_on_wr_signal(rfc_slot_t* slot){
  ALOGI("%s: ss_flush_incoming_que_on_wr_signal :: slot->fd is :: %d :: list_length :: %d",__func__,slot->fd,list_length(slot->incoming_queue));
  while (!list_is_empty(slot->incoming_queue)) {
    PendingData* p_buf = (PendingData*)list_front(slot->incoming_queue);
    switch (ss_send_data_to_app(slot->fd, p_buf)) {
      case SENT_NONE:
      case SENT_PARTIAL:
        ALOGI("%s: SENT_NONE or SENT_PARTIAL",__func__);
        // monitor the fd to get callback when app is ready to receive data
        btsock_thread_add_fd(pth, slot->fd, BTSOCK_RFCOMM, SOCK_THREAD_FD_WR,
                             slot->id);
        return true;

      case SENT_ALL:
        ALOGI("%s: SENT_ALL",__func__);
        list_remove(slot->incoming_queue, p_buf);
        break;

      case SENT_FAILED:
        ALOGI("%s: SENT_FAILED",__func__);
        list_remove(slot->incoming_queue, p_buf);
        return false;
    }
  }
  return true;
}

int ss_rfc_data_outgoing_size(int fd, int* size) {
  *size = 0;
  std::unique_lock<std::recursive_mutex> lock(slot_lock);
  rfc_slot_t* slot = find_rfc_slot_by_fd(fd);
  if (!slot) return false;

  if (ioctl(fd, FIONREAD, size) != 0) {
    LOG_ERROR(LOG_TAG,
              "%s unable to determine bytes remaining to be read on fd %d: %s",
              __func__, fd, strerror(errno));
    cleanup_rfc_slot(slot);
    return false;
  }
  return true;
}

int ss_rfc_data_outgoing(int fd, uint8_t* buf, int size) {
  std::unique_lock<std::recursive_mutex> lock(slot_lock);
  rfc_slot_t* slot = find_rfc_slot_by_fd(fd);
  if (!slot) return false;

  ssize_t received;
  OSI_NO_INTR(received = recv(fd, buf, size, 0));
  ALOGI("%s received is :: %d and size is :: %d",__func__,received,size);
  if (received != size) {
    LOG_ERROR(LOG_TAG, "%s error receiving RFCOMM data from app: %s", __func__,
              strerror(errno));
    cleanup_rfc_slot(slot);
    return false;
  }
  return true;
}

void ss_rfc_data_write_done(int fd, uint64_t length){
  int app_uid = -1;
  std::unique_lock<std::recursive_mutex> lock(slot_lock);
  rfc_slot_t* slot = find_rfc_slot_by_fd(fd);
  if (slot) {
    app_uid = slot->app_uid;
    if (!slot->f.outgoing_congest) {
      btsock_thread_add_fd(pth, slot->fd, BTSOCK_RFCOMM, SOCK_THREAD_FD_RD,
                           slot->id);
    }
  }
  uid_set_add_tx(uid_set, app_uid, length);
}

int bta_co_rfc_data_incoming(uint32_t id, BT_HDR* p_buf) {
  int app_uid = -1;
  uint64_t bytes_rx = 0;
  int ret = 0;
  std::unique_lock<std::recursive_mutex> lock(slot_lock);
  rfc_slot_t* slot = find_rfc_slot_by_id(id);
  if (!slot) return 0;

  app_uid = slot->app_uid;
  bytes_rx = p_buf->len;

  if (list_is_empty(slot->incoming_queue)) {
    switch (send_data_to_app(slot->fd, p_buf)) {
      case SENT_NONE:
      case SENT_PARTIAL:
        list_append(slot->incoming_queue, p_buf);
        btsock_thread_add_fd(pth, slot->fd, BTSOCK_RFCOMM, SOCK_THREAD_FD_WR,
                             slot->id);
        break;

      case SENT_ALL:
        osi_free(p_buf);
        ret = 1;  // Enable data flow.
        break;

      case SENT_FAILED:
        osi_free(p_buf);
        cleanup_rfc_slot(slot);
        break;
    }
  } else {
    list_append(slot->incoming_queue, p_buf);
  }

  uid_set_add_rx(uid_set, app_uid, bytes_rx);

  return ret;  // Return 0 to disable data flow.
}

int bta_co_rfc_data_outgoing_size(uint32_t id, int* size) {
  *size = 0;
  std::unique_lock<std::recursive_mutex> lock(slot_lock);
  rfc_slot_t* slot = find_rfc_slot_by_id(id);
  if (!slot) return false;

  if (ioctl(slot->fd, FIONREAD, size) != 0) {
    LOG_ERROR(LOG_TAG,
              "%s unable to determine bytes remaining to be read on fd %d: %s",
              __func__, slot->fd, strerror(errno));
    cleanup_rfc_slot(slot);
    return false;
  }

  return true;
}

int bta_co_rfc_data_outgoing(uint32_t id, uint8_t* buf, uint16_t size) {
  std::unique_lock<std::recursive_mutex> lock(slot_lock);
  rfc_slot_t* slot = find_rfc_slot_by_id(id);
  if (!slot) return false;

  ssize_t received;
  OSI_NO_INTR(received = recv(slot->fd, buf, size, 0));

  if (received != size) {
    LOG_ERROR(LOG_TAG, "%s error receiving RFCOMM data from app: %s", __func__,
              strerror(errno));
    cleanup_rfc_slot(slot);
    return false;
  }

  return true;
}

static rfc_slot_t* find_rfc_slot_by_scn(int scn)
{
    int i;
    if(scn > 0)
    {
        /* traverse it from the last entry, as incase of
         * server two entries will exist with the same scn
         * and the later entry is valid
         */
        for(i = MAX_RFC_CHANNEL-1; i >= 0; i--)
        {
            if(rfc_slots[i].scn == scn)
            {
                if(rfc_slots[i].id)
                    return &rfc_slots[i];
            }
        }
    }
    return NULL;
}

#if 0
bt_status_t btsock_rfc_get_sockopt(int channel, btsock_option_type_t option_name,
                                            void *option_value, int *option_len)
{
    bt_status_t status = BT_STATUS_FAIL;

    APPL_TRACE_DEBUG("btsock_rfc_get_sockopt channel is %d ", channel);
    if((channel < 1) || (channel > 30) || (option_value == NULL) || (option_len == NULL))
    {
        APPL_TRACE_ERROR("invalid rfc channel:%d or option_value:%p, option_len:%p",
                                             channel, option_value, option_len);
        return BT_STATUS_PARM_INVALID;
    }
    rfc_slot_t* rs = find_rfc_slot_by_scn(channel);
    if((rs) && ((option_name == BTSOCK_OPT_GET_MODEM_BITS)))
    {
        if(PORT_SUCCESS == PORT_GetModemStatus(rs->rfc_port_handle, (uint8_t  *)option_value))
        {
            *option_len = sizeof(uint8_t);
            status = BT_STATUS_SUCCESS;
        }
    }
    return status;
}


bt_status_t btsock_rfc_set_sockopt(int channel, btsock_option_type_t option_name,
                                            void *option_value, int option_len)
{
    bt_status_t status = BT_STATUS_FAIL;

    APPL_TRACE_DEBUG("btsock_rfc_get_sockopt channel is %d ", channel);
    if((channel < 1) || (channel > 30) || (option_value == NULL) || (option_len <= 0)
                     || (option_len > (int)sizeof(uint8_t)))
    {
        APPL_TRACE_ERROR("invalid rfc channel:%d or option_value:%p, option_len:%d",
                                        channel, option_value, option_len);
        return BT_STATUS_PARM_INVALID;
    }
    rfc_slot_t* rs = find_rfc_slot_by_scn(channel);
    if((rs) && ((option_name == BTSOCK_OPT_SET_MODEM_BITS)))
    {
        if((*((uint8_t *)option_value)) & MODEM_SIGNAL_DTRDSR)
        {
            if(PORT_SUCCESS != PORT_Control(rs->rfc_port_handle, PORT_SET_DTRDSR))
                return status;
        }
        if((*((uint8_t *)option_value)) & MODEM_SIGNAL_RTSCTS)
        {
            if(PORT_SUCCESS != PORT_Control(rs->rfc_port_handle, PORT_SET_CTSRTS))
                return status;
        }
        if((*((uint8_t *)option_value)) & MODEM_SIGNAL_RI)
        {
            if(PORT_SUCCESS != PORT_Control(rs->rfc_port_handle, PORT_SET_RI))
                return status;
        }
        if((*((uint8_t *)option_value)) & MODEM_SIGNAL_DCD)
        {
            if(PORT_SUCCESS != PORT_Control(rs->rfc_port_handle, PORT_SET_DCD))
                return status;
        }
        status = BT_STATUS_SUCCESS;
    }
    else if((rs) && ((option_name == BTSOCK_OPT_CLR_MODEM_BITS)))
    {
        if((*((uint8_t *)option_value)) & MODEM_SIGNAL_DTRDSR)
        {
            if(PORT_SUCCESS != PORT_Control(rs->rfc_port_handle, PORT_CLR_DTRDSR))
                return status;
        }
        if((*((uint8_t *)option_value)) & MODEM_SIGNAL_RTSCTS)
        {
            if(PORT_SUCCESS != PORT_Control(rs->rfc_port_handle, PORT_CLR_CTSRTS))
                return status;
        }
        if((*((uint8_t *)option_value)) & MODEM_SIGNAL_RI)
        {
            if(PORT_SUCCESS != PORT_Control(rs->rfc_port_handle, PORT_CLR_RI))
                return status;
        }
        if((*((uint8_t *)option_value)) & MODEM_SIGNAL_DCD)
        {
            if(PORT_SUCCESS != PORT_Control(rs->rfc_port_handle, PORT_CLR_DCD))
                return status;
        }
        status = BT_STATUS_SUCCESS;
    }

    return status;
}
#endif
