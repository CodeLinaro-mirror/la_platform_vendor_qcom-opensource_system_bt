/******************************************************************************
 *
 *  Copyright 2016 The Android Open Source Project
 *  Copyright 2009-2012 Broadcom Corporation
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
 ******************************************************************************/

#define LOG_TAG "bt_btif_a2dp"

#include <stdbool.h>

#include "audio_a2dp_hw/include/audio_a2dp_hw.h"
#include "audio_hal_interface/a2dp_encoding.h"
#include "bt_common.h"
#include "bta_av_api.h"
#include "btif_a2dp.h"
#include "btif_a2dp_audio_interface.h"
#include "btif_a2dp_control.h"
#include "btif_a2dp_sink.h"
#include "btif_a2dp_source.h"
#include "btif_av.h"
#include "btif_av_co.h"
#include "btif_hf.h"
#include "btif_util.h"
#include "osi/include/log.h"
#include "uipc.h"

void btif_a2dp_on_idle(const RawAddress& peer_addr) {
  LOG_INFO(LOG_TAG, "%s: ## ON A2DP IDLE ## peer_sep = %d", __func__,
           btif_av_get_peer_sep(peer_addr));

  // Don't stop decoder when it is not the active peer
  if (btif_av_get_peer_sep(peer_addr) == AVDT_TSEP_SNK) {
      btif_a2dp_source_on_idle(peer_addr);
  } else if (btif_av_get_peer_sep(peer_addr) == AVDT_TSEP_SRC) {
    LOG_INFO(LOG_TAG, "%s: ## ON A2DP IDLE ## peer_addr %s, active addr %s, peer_sep = %d", __func__,
             peer_addr.ToString().c_str(), btif_av_sink_active_peer().ToString().c_str(), btif_av_get_peer_sep(peer_addr));
    if (btif_av_sink_active_peer() == peer_addr || btif_av_sink_active_peer().IsEmpty()) {
      btif_a2dp_sink_on_idle();
    }
  }
}

bool btif_a2dp_on_started(const RawAddress& peer_addr, tBTA_AV_START* p_av_start) {
  LOG(INFO) << __func__ << ": ## ON A2DP STARTED ## peer " << peer_addr << " p_av_start:" << p_av_start;

  if (p_av_start == NULL) {
    tA2DP_CTRL_ACK status = A2DP_CTRL_ACK_SUCCESS;
    if (!bluetooth::headset::IsCallIdle()) {
      LOG(ERROR) << __func__ << ": peer " << peer_addr << " call in progress, do not start A2DP stream";
      status = A2DP_CTRL_ACK_INCALL_FAILURE;
    }
    /* just ack back a local start request, do not start the media encoder since
     * this is not for BTA_AV_START_EVT. */
    if (bluetooth::audio::a2dp::is_hal_2_0_enabled()) {
      bluetooth::audio::a2dp::ack_stream_started(status);
    } else if (btif_av_is_a2dp_offload_enabled()) {
      btif_a2dp_audio_on_started(status);
    } else {
      btif_a2dp_command_ack(uipc_get_ch_from_address(peer_addr, UIPC_CTRL_CH), status);
    }
    return true;
  }

  LOG(INFO) << __func__ << ": peer " << peer_addr << " status:" << +p_av_start->status
            << " suspending:" << logbool(p_av_start->suspending) << " initiator:" << logbool(p_av_start->initiator);

  if (p_av_start->status == BTA_AV_SUCCESS) {
    if (p_av_start->suspending) {
      LOG(WARNING) << __func__ << ": peer " << peer_addr << " A2DP is suspending and ignores the started event";
      return false;
    }
    if (btif_av_is_a2dp_offload_enabled()) {
      btif_av_stream_start_offload(peer_addr);
    } else if (bluetooth::audio::a2dp::is_hal_2_0_enabled()) {
      if (btif_av_get_peer_sep(peer_addr) == AVDT_TSEP_SNK) {
        /* Start the media encoder to do the SW audio stream */
        btif_a2dp_source_start_audio_req(peer_addr);
      }
      if (p_av_start->initiator) {
        bluetooth::audio::a2dp::ack_stream_started(A2DP_CTRL_ACK_SUCCESS);
        return true;
      }
    } else {
      if (p_av_start->initiator) {
        btif_a2dp_command_ack(uipc_get_ch_from_address(peer_addr, UIPC_CTRL_CH), A2DP_CTRL_ACK_SUCCESS);
        return true;
      }
      /* media task is auto-started upon UIPC connection of a2dp audiopath */
    }
  } else if (p_av_start->initiator) {
    LOG(ERROR) << __func__ << ": peer " << peer_addr << " A2DP start request failed: status = " << +p_av_start->status;
    if (bluetooth::audio::a2dp::is_hal_2_0_enabled()) {
      bluetooth::audio::a2dp::ack_stream_started(A2DP_CTRL_ACK_FAILURE);
    } else if (btif_av_is_a2dp_offload_enabled()) {
      btif_a2dp_audio_on_started(p_av_start->status);
    } else {
      btif_a2dp_command_ack(uipc_get_ch_from_address(peer_addr, UIPC_CTRL_CH), A2DP_CTRL_ACK_FAILURE);
    }
    return true;
  }
  return false;
}

void btif_a2dp_on_stopped(const RawAddress& peer_addr, tBTA_AV_SUSPEND* p_av_suspend) {
  LOG_INFO(LOG_TAG, "%s: ## ON A2DP STOPPED ## p_av_suspend=%p", __func__,
           p_av_suspend);
  // Don't stop decoder when it is not the active peer
  if (btif_av_get_peer_sep(peer_addr) == AVDT_TSEP_SRC && btif_av_sink_active_peer() == peer_addr) {
    btif_a2dp_sink_on_stopped(p_av_suspend);
    return;
  }
  if (bluetooth::audio::a2dp::is_hal_2_0_enabled() ||
      !btif_av_is_a2dp_offload_enabled()) {
    btif_a2dp_source_on_stopped(peer_addr, p_av_suspend);
  } else if (p_av_suspend != NULL) {
    btif_a2dp_audio_on_stopped(p_av_suspend->status);
  }
}

void btif_a2dp_on_suspended(const RawAddress& peer_addr, tBTA_AV_SUSPEND* p_av_suspend) {
  LOG_INFO(LOG_TAG, "%s: ## ON A2DP SUSPENDED ## p_av_suspend=%p", __func__,
           p_av_suspend);
  // Don't stop decoder when it is not the active peer
  if (btif_av_get_peer_sep(peer_addr) == AVDT_TSEP_SRC && btif_av_sink_active_peer() == peer_addr) {
    btif_a2dp_sink_on_suspended(p_av_suspend);
    return;
  }
  if (bluetooth::audio::a2dp::is_hal_2_0_enabled() ||
      !btif_av_is_a2dp_offload_enabled()) {
    btif_a2dp_source_on_suspended(peer_addr, p_av_suspend);
  } else if (p_av_suspend != NULL) {
    btif_a2dp_audio_on_suspended(p_av_suspend->status);
  }
}

void btif_a2dp_on_offload_started(const RawAddress& peer_addr,
                                  tBTA_AV_STATUS status) {
  tA2DP_CTRL_ACK ack;
  LOG_INFO(LOG_TAG, "%s: peer %s status %d", __func__,
           peer_addr.ToString().c_str(), status);

  switch (status) {
    case BTA_AV_SUCCESS:
      ack = A2DP_CTRL_ACK_SUCCESS;
      break;
    case BTA_AV_FAIL_RESOURCES:
      LOG_ERROR(LOG_TAG, "%s: peer %s FAILED UNSUPPORTED", __func__,
                peer_addr.ToString().c_str());
      ack = A2DP_CTRL_ACK_UNSUPPORTED;
      break;
    default:
      LOG_ERROR(LOG_TAG, "%s: peer %s FAILED: status = %d", __func__,
                peer_addr.ToString().c_str(), status);
      ack = A2DP_CTRL_ACK_FAILURE;
      break;
  }
  if (btif_av_is_a2dp_offload_enabled()) {
    if (ack != BTA_AV_SUCCESS && btif_av_stream_started_ready(peer_addr)) {
      // Offload request will return with failure from btif_av sm if
      // suspend is triggered for remote start. Disconnect only if SoC
      // returned failure for offload VSC
      LOG_ERROR(LOG_TAG, "%s: peer %s offload start failed", __func__,
                peer_addr.ToString().c_str());
      btif_av_src_disconnect_sink(peer_addr);
    }
  }
  if (bluetooth::audio::a2dp::is_hal_2_0_enabled()) {
    bluetooth::audio::a2dp::ack_stream_started(ack);
  } else {
    btif_a2dp_command_ack(uipc_get_ch_from_address(peer_addr, UIPC_CTRL_CH), ack);
    btif_a2dp_audio_on_started(status);
  }
}

void btif_debug_a2dp_dump(int fd) {
  btif_a2dp_source_debug_dump(fd);
  btif_a2dp_sink_debug_dump(fd);
  btif_a2dp_codec_debug_dump(fd);
}
