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

#include <hardware/bluetooth.h>
#include "bt_trace.h"
#include "btif_cap_source.h"
#include "btif_ahim.h"
#include "btif_cap.h"

#if AHIM_ENABLED


void btif_cap_process_request(tA2DP_CTRL_CMD cmd)
{
  tA2DP_CTRL_ACK status = A2DP_CTRL_ACK_FAILURE;
  // update pending command
  btif_ahim_update_pending_command(cmd);

  BTIF_TRACE_IMP("%s: cmd %u", __func__, cmd);

  switch (cmd) {
    case A2DP_CTRL_CMD_START:
    {
      // TODO: check if the call is active from btif_apm
      if (0) {
        status = A2DP_CTRL_ACK_INCALL_FAILURE;
      }
      else {
        // CAP is in right state
        status = A2DP_CTRL_ACK_PENDING;
        btif_cap_stream_start();
      }

      btif_ahim_ack_stream_started(status);
      break;
    }

    case A2DP_CTRL_CMD_SUSPEND:
    {
        status = A2DP_CTRL_ACK_PENDING;

      btif_cap_stream_suspend();

      btif_ahim_ack_stream_suspended(status);
      break;
    }
    case A2DP_CTRL_CMD_STOP:
    {
      status = A2DP_CTRL_ACK_PENDING;

      btif_cap_stream_stop();

      btif_ahim_ack_stream_suspended(status);
      break;
    }
    default:
      APPL_TRACE_ERROR("%s: unsupported command %d", __func__, cmd);
      break;
  }
}


void btif_cap_handle_event(uint16_t event, char* p_param)
{

  switch(event) {
    case BTIF_CAP_PROCESS_HIDL_REQ_EVT:
      btif_cap_process_request((tA2DP_CTRL_CMD ) *p_param);
      break;
    default:
      BTIF_TRACE_IMP("%s: unhandled event", __func__);
      break;
  }
}

bt_status_t btif_cap_source_setup_codec() {
  APPL_TRACE_EVENT("%s", __func__);

  bt_status_t status = BT_STATUS_FAIL;

  // TODO: call CAP API to setup codec

  APPL_TRACE_EVENT("%s ## setup_codec ##", __func__);
  btif_ahim_setup_codec(CAP);

  // TODO: check the status
  return status;
}

bool btif_cap_source_start_session(const RawAddress& peer_address) {
  bt_status_t status = BT_STATUS_FAIL;
  APPL_TRACE_DEBUG("%s: starting session for BD addr %s",__func__,
        peer_address.ToString().c_str());

  // initialize hal.  TODO: check if NULL is handled or thread needs to be created
  btif_ahim_init_hal(nullptr, CAP);

  status = btif_cap_source_setup_codec();

  btif_ahim_start_session();

  return true;
}

bool btif_cap_source_end_session(const RawAddress& peer_address) {
  APPL_TRACE_DEBUG("%s: starting session for BD addr %s",__func__,
        peer_address.ToString().c_str());

  btif_ahim_end_session();

  return true;
}

bool btif_cap_source_restart_session(const RawAddress& old_peer_address,
                                      const RawAddress& new_peer_address) {
  bool is_streaming = btif_ahim_is_streaming();
  SessionType session_type = btif_ahim_get_session_type();

  APPL_TRACE_IMP("%s: old_peer_address=%s, new_peer_address=%s, is_streaming=%d ",
      __func__, old_peer_address.ToString().c_str(),
    new_peer_address.ToString().c_str(), is_streaming);

   // TODO: do we need to check for new empty address
  //CHECK(!new_peer_address.IsEmpty());

  // If the old active peer was valid or if session is not
  // unknown, end the old session.
  if (!old_peer_address.IsEmpty() ||
    session_type != SessionType::UNKNOWN) {
    btif_cap_source_end_session(old_peer_address);
  }

  btif_cap_source_start_session(new_peer_address);

  return true;
}

void btif_cap_source_command_ack(tA2DP_CTRL_CMD cmd, tA2DP_CTRL_ACK status) {
  switch (cmd) {
    case A2DP_CTRL_CMD_START:
      btif_ahim_ack_stream_started(status);
      break;
    case A2DP_CTRL_CMD_SUSPEND:
    case A2DP_CTRL_CMD_STOP:
      btif_ahim_ack_stream_suspended(status);
      break;
    default:
      break;
  }
}

void btif_cap_source_on_stopped(tA2DP_CTRL_ACK status) {
  APPL_TRACE_EVENT("%s: status %u", __func__, status);

  btif_ahim_ack_stream_suspended(status);

  btif_ahim_reset_pending_command();
}

void btif_cap_source_on_suspended(tA2DP_CTRL_ACK status) {
  APPL_TRACE_EVENT("%s: status %u", __func__, status);

  btif_ahim_ack_stream_suspended(status);

  btif_ahim_reset_pending_command();
}

bool btif_cap_on_started(tA2DP_CTRL_ACK status) {
  APPL_TRACE_EVENT("%s: status %u", __func__, status);
  bool retval = false;

  if(0/* TODO: check if call is in progress*/) {
    APPL_TRACE_WARNING("%s: call in progress, sending failure", __func__);
    btif_ahim_ack_stream_started(A2DP_CTRL_ACK_INCALL_FAILURE);
  }
  else {
    btif_ahim_ack_stream_started(status);
    retval = true;
  }

  btif_ahim_reset_pending_command();
  return retval;
}


#endif // AHIM_ENABLED
