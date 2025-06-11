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
 *  Changes from Qualcomm Innovation Center are provided under the following license:
 *  Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear.
 *
 ******************************************************************************/

/*******************************************************************************
 *
 *  Filename:      btif_sock.h
 *
 *  Description:   Bluetooth socket Interface
 *
 ******************************************************************************/

#ifndef BTIF_SOCK_RFC_H
#define BTIF_SOCK_RFC_H

#include "btif_uid.h"
#include <hardware/vendor_socket.h>
#include "osi/include/list.h"

using bluetooth::Uuid;

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
  int type;
} rfc_slot_t;

bt_status_t btsock_rfc_init(int handle, uid_set_t* set);
void btsock_rfc_cleanup();
bt_status_t btsock_rfc_listen(const char* name, const bluetooth::Uuid* uuid,
                              int channel, int* sock_fd, int flags,
                              int app_uid, int type);
bt_status_t btsock_rfc_connect(const RawAddress* bd_addr,
                               const bluetooth::Uuid* uuid, int channel,
                               int* sock_fd, int flags, int app_uid, int type);
void btsock_rfc_signaled(int fd, int type, int flags, uint32_t user_id);

bt_status_t btsock_rfc_get_sockopt(int channel, btsock_option_type_t option_name,
                                            void *option_value, int *option_len);
bt_status_t btsock_rfc_set_sockopt(int channel, btsock_option_type_t option_name,
                                            void *option_value, int option_len);

void btif_rfcomm_ss_callback(uint16_t event, char* p_param);

rfc_slot_t* find_rfc_slot_by_fd(int fd);
rfc_slot_t* find_rfc_slot_by_scn(int scn);
#endif
