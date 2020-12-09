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

#include "audio_hal_interface/a2dp_encoding.h"
#include "btif_cap_source.h"
#include "btif_bap_broadcast.h"

#if AHIM_ENABLED

uint8_t cur_active_profile = CAP;

void btif_ahim_update_current_profile(uint8_t profile)
{
  switch(profile)
  {
     case A2DP:
       FALLTHROUGH;
     case CAP:
       FALLTHROUGH;
     case BROADCAST:
       cur_active_profile = profile;
       break;
     default:
       BTIF_TRACE_WARNING("%s, unsupported active profile, resetting to A2DP"
                          , __func__);
       cur_active_profile = A2DP;
       break;
  }

  BTIF_TRACE_IMP("%s: current active profile is %u", __func__, cur_active_profile);
}

void btif_ahim_process_request(tA2DP_CTRL_CMD cmd)
{
  switch(cur_active_profile)
  {
    case A2DP:
      BTIF_TRACE_IMP("%s: sending HIDL request to AV", __func__);
      btif_dispatch_sm_event(BTIF_AV_PROCESS_HIDL_REQ_EVT, (char*)&cmd, sizeof(cmd));
      break;
    case CAP:
      BTIF_TRACE_IMP("%s: sending HIDL request to CAP", __func__);
      btif_transfer_context(btif_cap_handle_event, BTIF_CAP_PROCESS_HIDL_REQ_EVT,
               (char*)&cmd, sizeof(cmd), NULL);
      break;
    case BROADCAST:
      BTIF_TRACE_IMP("%s: sending HIDL request to BAP Broadcast", __func__);
      btif_bap_ba_dispatch_sm_event(BTIF_BAP_BROADCAST_PROCESS_HIDL_REQ_EVT,
               (char*)&cmd, sizeof(cmd));
      break;
  }
}

bool btif_ahim_init_hal(thread_t *t, uint8_t profile) {
  return bluetooth::audio::a2dp::init(t, profile);
}

void btif_ahim_cleanup_hal() {
  bluetooth::audio::a2dp::cleanup();
}

bool btif_ahim_is_hal_2_0_supported() {
  return bluetooth::audio::a2dp::is_hal_2_0_supported();
}

bool btif_ahim_is_hal_2_0_enabled() {
  return bluetooth::audio::a2dp::is_hal_2_0_enabled();
}

bool btif_ahim_is_restart_session_needed() {
  return bluetooth::audio::a2dp::is_restart_session_needed();
}

void btif_ahim_update_session_params(SessionParamType param_type) {
  bluetooth::audio::a2dp::update_session_params(param_type);
}

bool btif_ahim_setup_codec(uint8_t profile) {
  return bluetooth::audio::a2dp::setup_codec(profile);
}

void btif_ahim_start_session() {
  bluetooth::audio::a2dp::start_session();
}

void btif_ahim_end_session() {
  bluetooth::audio::a2dp::end_session();
}

tA2DP_CTRL_CMD btif_ahim_get_pending_command() {
  return bluetooth::audio::a2dp::get_pending_command();
}

void btif_ahim_reset_pending_command() {
  bluetooth::audio::a2dp::reset_pending_command();
}

void btif_ahim_update_pending_command(tA2DP_CTRL_CMD cmd) {
  bluetooth::audio::a2dp::update_pending_command(cmd);
}

void btif_ahim_ack_stream_started(const tA2DP_CTRL_ACK& ack) {
  bluetooth::audio::a2dp::ack_stream_started(ack);
}

void btif_ahim_ack_stream_suspended(const tA2DP_CTRL_ACK& ack) {
  bluetooth::audio::a2dp::ack_stream_suspended(ack);
}

size_t btif_ahim_read(uint8_t* p_buf, uint32_t len) {
  return bluetooth::audio::a2dp::read(p_buf, len);
}

void btif_ahim_set_remote_delay(uint16_t delay_report) {
  bluetooth::audio::a2dp::set_remote_delay(delay_report);
}

bool btif_ahim_is_streaming() {
  return bluetooth::audio::a2dp::is_streaming();
}


SessionType btif_ahim_get_session_type() {
  return bluetooth::audio::a2dp::get_session_type();
}

#endif // #if AHIM_ENABLED
