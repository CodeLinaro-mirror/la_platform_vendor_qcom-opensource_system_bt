/******************************************************************************
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************/

#include "audio_a2dp_hw/include/audio_a2dp_hw.h"
#include <vendor/qti/hardware/bluetooth_audio/2.0/types.h>
#include "osi/include/thread.h"
#include "btif_av.h"
//#include "bt_target.h"

using vendor::qti::hardware::bluetooth_audio::V2_0::SessionType;
using vendor::qti::hardware::bluetooth_audio::V2_0::SessionParamType;

// TODO: This probably needs to be replaced with generic LE Audio enablement flag
#define AHIM_ENABLED 1

#define A2DP 1
#define CAP 2
#define BROADCAST 3

enum cap_evt{
  BTIF_CAP_PROCESS_HIDL_REQ_EVT = 0x1,
};

void btif_ahim_process_request(tA2DP_CTRL_CMD cmd);
void btif_ahim_update_current_profile(uint8_t profile);


bool btif_ahim_init_hal(thread_t *t, uint8_t profile);

void btif_ahim_cleanup_hal();

bool btif_ahim_is_hal_2_0_supported();

bool btif_ahim_is_hal_2_0_enabled();

bool btif_ahim_is_restart_session_needed();

void btif_ahim_update_session_params(SessionParamType param_type);

bool btif_ahim_setup_codec(uint8_t profile);

void btif_ahim_start_session();

void btif_ahim_end_session();

tA2DP_CTRL_CMD btif_ahim_get_pending_command();

void btif_ahim_reset_pending_command();

void btif_ahim_update_pending_command(tA2DP_CTRL_CMD cmd);

void btif_ahim_ack_stream_started(const tA2DP_CTRL_ACK& ack);

void btif_ahim_ack_stream_suspended(const tA2DP_CTRL_ACK& ack);

size_t btif_ahim_read(uint8_t* p_buf, uint32_t len);

void btif_ahim_set_remote_delay(uint16_t delay_report);

bool btif_ahim_is_streaming();

SessionType btif_ahim_get_session_type();


