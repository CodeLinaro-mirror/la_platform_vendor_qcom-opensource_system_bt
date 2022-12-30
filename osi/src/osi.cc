/******************************************************************************
 *
 *  Copyright (C) 2016 Google, Inc.
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

#define LOG_TAG "bt_osi_rand"

#include <base/logging.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "osi/include/log.h"
#include "osi/include/osi.h"
#include "bt_trace.h"
#include "bt_types.h"

#define RANDOM_PATH "/dev/urandom"

/* LayerIDs for BTA, currently everything maps onto appl_trace_level */
static const char* const bt_layer_tags[] = {
    "bt_btif",
    "bt_usb",
    "bt_serial",
    "bt_socket",
    "bt_rs232",
    "bt_lc",
    "bt_lm",
    "bt_hci",
    "bt_l2cap",
    "bt_rfcomm",
    "bt_sdp",
    "bt_tcs",
    "bt_obex",
    "bt_btm",
    "bt_gap",
    "UNUSED",
    "UNUSED",
    "bt_icp",
    "bt_hsp2",
    "bt_spp",
    "bt_ctp",
    "bt_bpp",
    "bt_hcrp",
    "bt_ftp",
    "bt_opp",
    "bt_btu",
    "bt_gki_deprecated",
    "bt_bnep",
    "bt_pan",
    "bt_hfp",
    "bt_hid",
    "bt_bip",
    "bt_avp",
    "bt_a2d",
    "bt_sap",
    "bt_amp",
    "bt_mca_deprecated",
    "bt_att",
    "bt_smp",
    "bt_nfc",
    "bt_nci",
    "bt_idep",
    "bt_ndep",
    "bt_llcp",
    "bt_rw",
    "bt_ce",
    "bt_snep",
    "bt_ndef",
    "bt_nfa",
 };

int osi_rand(void) {
  int rand;
  int rand_fd = open(RANDOM_PATH, O_RDONLY);

  if (rand_fd == INVALID_FD) {
    LOG_ERROR(LOG_TAG, "%s can't open rand fd %s: %s ", __func__, RANDOM_PATH,
              strerror(errno));
    CHECK(rand_fd != INVALID_FD);
  }

  ssize_t read_bytes = read(rand_fd, &rand, sizeof(rand));
  close(rand_fd);

  CHECK(read_bytes == sizeof(rand));

  if (rand < 0) rand = -rand;

  return rand;
}

void vnd_LogMsg(uint32_t trace_set_mask, const char *fmt_str, ...) {
  int trace_layer = TRACE_GET_LAYER(trace_set_mask);
  const char *tag;
  if (trace_layer >= TRACE_LAYER_MAX_NUM)
    trace_layer = 0;

  tag = bt_layer_tags[trace_layer];

  va_list ap;
  va_start(ap, fmt_str);
  //if(logger_interface)
    //logger_interface->send_log_msg(tag, fmt_str, ap);
  va_end(ap);
}
