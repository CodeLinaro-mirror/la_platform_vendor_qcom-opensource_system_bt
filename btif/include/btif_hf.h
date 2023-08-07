/******************************************************************************
 *
 *  Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 *  Not a Contribution.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted (subject to the limitations in the
 *  disclaimer below) provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 *  * Neither the name of The Linux Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

#ifndef BTIF_HF_H
#define BTIF_HF_H

#include <stdbool.h>
#include <hardware/bluetooth_headset_interface.h>
/* AG feature masks */
#define BT_AG_FEAT_3WAY 0x00000001   /* Three-way calling */
#define BT_AG_FEAT_ECNR 0x00000002   /* Echo cancellation/noise reduction */
#define BT_AG_FEAT_VREC 0x00000004   /* Voice recognition */
#define BT_AG_FEAT_INBAND 0x00000008 /* In-band ring tone */
#define BT_AG_FEAT_VTAG 0x00000010   /* Attach a phone number to a voice tag */
#define BT_AG_FEAT_REJECT 0x00000020 /* Ability to reject incoming call */
#define BT_AG_FEAT_ECS 0x00000040    /* Enhanced Call Status */
#define BT_AG_FEAT_ECC 0x00000080    /* Enhanced Call Control */
#define BT_AG_FEAT_EXTERR 0x00000100 /* Extended error codes */
#define BT_AG_FEAT_CODEC 0x00000200  /* Codec Negotiation */

/* Valid feature bit mask for HFP 1.6 (and below) */
#define HFP_1_6_FEAT_MASK 0x000003FF

/* HFP 1.7+ */
#define BT_AG_FEAT_HF_IND 0x00000400 /* HF Indicators */
#define BT_AG_FEAT_ESCO 0x00000800   /* eSCO S4 (and T2) setting supported */

/* HFP 1.8+ */
#define BT_AG_FEAT_ENHC_VREC 0x00001000 /* Voice enhanced recognition status*/
#define BT_AG_FEAT_VREC_TEXT 0x00002000 /* Voice recognition text*/

/* HFP 1.8+ for SDP supported features*/
#define BT_AG_SDP_FEAT_ENHC_VREC 0x00000040 /* Voice enhanced recognition status*/
#define BT_AG_SDP_FEAT_VREC_TEXT 0x00000080 /* Voice recognition text*/

#define BT_AG_FEAT_BTRH 0x00010000    /* CCAP incoming call hold */
#define BT_AG_FEAT_UNAT 0x00020000    /* Pass unknown AT commands to app */
#define BT_AG_FEAT_NOSCO 0x00040000   /* No SCO control performed by BTA AG */
#define BT_AG_FEAT_NO_ESCO 0x00080000 /* Do not allow or use eSCO */
#define BT_AG_FEAT_VOIP 0x00100000    /* VoIP call */

/* HFP peer features */
#define BT_AG_PEER_FEAT_ECNR 0x0001   /* Echo cancellation/noise reduction */
#define BT_AG_PEER_FEAT_3WAY 0x0002   /* Call waiting and three-way calling */
#define BT_AG_PEER_FEAT_CLI 0x0004    /* Caller ID presentation capability */
#define BT_AG_PEER_FEAT_VREC 0x0008   /* Voice recognition activation */
#define BT_AG_PEER_FEAT_VOL 0x0010    /* Remote volume control */
#define BT_AG_PEER_FEAT_ECS 0x0020    /* Enhanced Call Status */
#define BT_AG_PEER_FEAT_ECC 0x0040    /* Enhanced Call Control */
#define BT_AG_PEER_FEAT_CODEC 0x0080  /* Codec Negotiation */
#define BT_AG_PEER_FEAT_HF_IND 0x0100 /* HF Indicators */
#define BT_AG_PEER_FEAT_ESCO 0x0200   /* eSCO S4 (and T2) setting supported */
#define BT_AG_PEER_FEAT_ENHC_VREC 0x0400 /* Voice enhanced recognition */
#define BT_AG_PEER_FEAT_VREC_TEXT 0x0800 /* Voice recognition text */
/* Pass unknown AT command responses to application */
#define BT_AG_PEER_FEAT_UNAT 0x1000
#define BT_AG_PEER_FEAT_VOIP 0x2000 /* VoIP call */

namespace bluetooth {
namespace headset {

Interface* GetInterface();

//#include "bta_ag_api.h"

/* Number of BTIF-HF control blocks */
typedef uint16_t tBTA_AG_PEER_FEAT;

extern uint16_t btif_max_hf_clients;

/* BTIF-HF control block to map bdaddr to BTA handle */
typedef struct _btif_hf_cb {
  uint16_t handle;
  RawAddress connected_bda;
  bthf_connection_state_t state;
  bthf_vr_state_t vr_state;
  tBTA_AG_PEER_FEAT peer_feat;
  int num_active;
  int num_held;
  struct timespec call_end_timestamp;
  struct timespec connected_timestamp;
  bthf_call_state_t call_setup_state;
  bthf_audio_state_t audio_state;
  tBTA_SERVICE_ID service_id;
#if (TWS_AG_ENABLED == TRUE)
  uint8_t twsp_state;
#endif
} btif_hf_cb_t;

// Check whether there is a Hands-Free call in progress.
// Returns true if no call is in progress.
bool btif_hf_is_call_idle(void);
bool btif_hf_is_call_vr_idle(void);
bt_status_t btif_hf_execute_service(bool b_enable);
bt_status_t btif_hf_check_if_sco_connected();
bool is_connected(RawAddress* bd_addr);
void btif_in_hf_generic_evt(uint16_t event, char* p_param);
#ifdef ADV_AUDIO_FEATURE
void btif_ag_result(uint16_t enum_value, char* param);
#endif

void btif_hf_ss_callback(uint16_t event, char* p_param);

}  // namespace headset
}  // na

#endif /* BTIF_HF_H */
