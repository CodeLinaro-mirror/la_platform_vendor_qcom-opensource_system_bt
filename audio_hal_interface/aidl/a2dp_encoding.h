/*
 * Copyright 2022 The Android Open Source Project
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
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.

    * Redistribution and use in source and binary forms, with or without
      modification, are permitted (subject to the limitations in the
      disclaimer below) provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
*/

#pragma once

#include <vector>

#include "a2dp_sbc_constants.h"
#include "audio_a2dp_hw/include/audio_a2dp_hw.h"
#include "btif_a2dp_source.h"
#include "btif_av.h"
#include "btif_av_co.h"
#include "btif_hf.h"
#include "avdt_api.h"
#include "osi/include/thread.h"
#include "osi/include/log.h"
#include "osi/include/properties.h"
#include "audio_aidl_interfaces.h"
//#include "types/raw_address.h"
namespace bluetooth {
namespace audio {
namespace aidl {
namespace a2dp {

using ::aidl::android::hardware::bluetooth::audio::SessionType;

bool update_codec_offloading_capabilities(
    const std::vector<btav_a2dp_codec_config_t>& framework_preference);

/***
 * Check if new bluetooth_audio is enabled
 ***/
bool is_hal_enabled();

/***
 * Check if new bluetooth_audio aidl is available
 ***/
bool is_aidl_hal_available();


/***
 * Check if new bluetooth_audio is running with offloading encoders
 ***/
bool is_hal_offloading();

/***
 * Initialize BluetoothAudio HAL: openProvider
 ***/
bool init(thread_t* message_loop);

/***
 * Clean up BluetoothAudio HAL
 ***/
void cleanup();

/***
 * Set up the codec into BluetoothAudio HAL
 ***/
bool setup_codec();

/***
 * Send command to the BluetoothAudio HAL: StartSession, EndSession,
 * StreamStarted, StreamSuspended
 ***/
void start_session();
void end_session();
void ack_stream_started(const tA2DP_CTRL_ACK& status);
void ack_stream_suspended(const tA2DP_CTRL_ACK& status);

/***
 * Read from the FMQ of BluetoothAudio HAL
 ***/
size_t read(uint8_t* p_buf, uint32_t len);

/***
 * Update A2DP delay report to BluetoothAudio HAL
 ***/
void set_remote_delay(uint16_t delay_report);

void update_session_params(uint8_t param);
SessionType get_session_type();

bool is_restart_session_needed();

tA2DP_CTRL_CMD GetPendingCmd();

void ResetPendingCmd();

}  // namespace a2dp
}  // namespace aidl
}  // namespace audio
}  // namespace bluetooth
