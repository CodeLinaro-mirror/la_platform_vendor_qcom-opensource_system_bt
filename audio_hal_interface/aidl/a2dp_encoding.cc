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
 * Changes from Qualcomm Innovation Center are provided under the following license:
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.

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

#define LOG_TAG "a2dp_encoding"
#include "a2dp_transport.h"
#include "transport_instance.h"

#include "a2dp_encoding.h"

#include "a2dp_sbc_constants.h"
#include "btif_a2dp_source.h"
#include "btif_av.h"
#include "btif_av_co.h"
#include "btif_hf.h"
#include "client_interface.h"
#include "codec_status.h"
#include "osi/include/log.h"
#include "osi/include/properties.h"
#include "raw_address.h"
#include "a2dp_sbc.h"
#include <a2dp_vendor.h>
#include "controller.h"
#include "a2dp_vendor_ldac_constants.h"
//#include "a2dp_vendor_aptx_adaptive.h"
#include "a2dp_aac.h"
#include "btif_ahim.h"
#define AAC_SAMPLE_SIZE  1024
#define AAC_LATM_HEADER  12
#define MAX_MTU_SIZE 1024

extern uint8_t active_codec_info[AVDT_CODEC_SIZE];
extern bool is_a2dp_split_sink_enabled();
uint16_t sink_latency = 0;

namespace bluetooth {
namespace audio {
namespace aidl {
namespace a2dp {

namespace {

using ::aidl::android::hardware::bluetooth::audio::AudioConfiguration;
using ::aidl::android::hardware::bluetooth::audio::ChannelMode;
using ::aidl::android::hardware::bluetooth::audio::CodecConfiguration;
using ::aidl::android::hardware::bluetooth::audio::PcmConfiguration;
using ::aidl::android::hardware::bluetooth::audio::SessionType;

using ::aidl::android::hardware::bluetooth::audio::CodecType;
using ::aidl::android::hardware::bluetooth::audio::SbcCapabilities;
using ::aidl::android::hardware::bluetooth::audio::SbcAllocMethod;
using ::aidl::android::hardware::bluetooth::audio::SbcChannelMode;
using ::aidl::android::hardware::bluetooth::audio::SbcConfiguration;

using ::aidl::android::hardware::bluetooth::audio::AacCapabilities;
using ::aidl::android::hardware::bluetooth::audio::AacConfiguration;
using ::aidl::android::hardware::bluetooth::audio::AacObjectType;
using ::bluetooth::audio::aidl::BluetoothAudioCtrlAck;
using ::bluetooth::audio::aidl::BluetoothAudioSinkClientInterface;
using ::bluetooth::audio::aidl::codec::A2dpAacToHalConfig;
using ::bluetooth::audio::aidl::codec::A2dpAptxToHalConfig;
using ::bluetooth::audio::aidl::codec::A2dpCodecToHalBitsPerSample;
using ::bluetooth::audio::aidl::codec::A2dpCodecToHalChannelMode;
using ::bluetooth::audio::aidl::codec::A2dpCodecToHalSampleRate;
using ::bluetooth::audio::aidl::codec::A2dpLdacToHalConfig;
using ::bluetooth::audio::aidl::codec::A2dpSbcToHalConfig;
using ::bluetooth::audio::aidl::codec::A2dpAptxAdaptiveToHalConfig;

/***
 *
 * A2dpTransport functions and variables
 *
 ***/

tA2DP_CTRL_CMD A2dpTransport::a2dp_pending_cmd_ = A2DP_CTRL_CMD_NONE;
uint16_t A2dpTransport::remote_delay_report_ = 0;
CodecConfiguration codec_config_global;
PcmConfiguration pcm_config_global;
static bool is_aidl_checked = false;
static bool is_aidl_available = false;



A2dpTransport::A2dpTransport(SessionType sessionType)
    : IBluetoothSinkTransportInstance(sessionType, (AudioConfiguration){}),
      total_bytes_read_(0),
      data_position_({}) {
  a2dp_pending_cmd_ = A2DP_CTRL_CMD_NONE;
  remote_delay_report_ = 0;
}

BluetoothAudioCtrlAck A2dpTransport::StartRequest(bool is_low_latency) {
  // Check if a previous request is not finished
  tA2DP_CTRL_ACK status = A2DP_CTRL_ACK_PENDING;
  if (a2dp_pending_cmd_ == A2DP_CTRL_CMD_START) {
    LOG(INFO) << __func__ << "AIDL: A2DP_CTRL_CMD_START in progress";
    return a2dp_ack_to_bt_audio_ctrl_ack(A2DP_CTRL_ACK_PENDING);
  } else if (a2dp_pending_cmd_ != A2DP_CTRL_CMD_NONE) {
    LOG(WARNING) << __func__ << "AIDL: busy in pending_cmd=" << a2dp_pending_cmd_;
    return a2dp_ack_to_bt_audio_ctrl_ack(A2DP_CTRL_ACK_FAILURE);
  }
  a2dp_pending_cmd_ = A2DP_CTRL_CMD_START;
  btif_ahim_process_request(A2DP_CTRL_CMD_START, A2DP, TO_AIR, false);
  return a2dp_ack_to_bt_audio_ctrl_ack(status);
}

BluetoothAudioCtrlAck A2dpTransport::SuspendRequest() {
  // Previous request is not finished
  tA2DP_CTRL_ACK status = A2DP_CTRL_ACK_PENDING;
  if (a2dp_pending_cmd_ == A2DP_CTRL_CMD_SUSPEND) {
    LOG(INFO) << __func__ << "AIDL: A2DP_CTRL_CMD_SUSPEND in progress";
    return a2dp_ack_to_bt_audio_ctrl_ack(A2DP_CTRL_ACK_PENDING);
  } else if (a2dp_pending_cmd_ != A2DP_CTRL_CMD_NONE) {
    LOG(WARNING) << __func__ << "AIDL: busy in pending_cmd=" << a2dp_pending_cmd_;
    return a2dp_ack_to_bt_audio_ctrl_ack(A2DP_CTRL_ACK_FAILURE);
  }
  a2dp_pending_cmd_ = A2DP_CTRL_CMD_SUSPEND;
  btif_ahim_process_request(A2DP_CTRL_CMD_SUSPEND, A2DP, TO_AIR, false);
  return a2dp_ack_to_bt_audio_ctrl_ack(status);
}

void A2dpTransport::StopRequest() {
  btif_ahim_process_request(A2DP_CTRL_CMD_STOP, A2DP, TO_AIR, false);
}

bool A2dpTransport::GetPresentationPosition(uint64_t* remote_delay_report_ns,
                                            uint64_t* total_bytes_read,
                                            timespec* data_position) {
  *remote_delay_report_ns = remote_delay_report_ * 100000ULL;
  *total_bytes_read = total_bytes_read_;
  *data_position = data_position_;
  LOG(INFO) << __func__ << "AIDL: delay=" << remote_delay_report_
            << "/10ms, data=" << total_bytes_read_
            << " byte(s), timestamp=" << data_position_.tv_sec << "."
            << data_position_.tv_nsec << "s";
  return true;
}

void A2dpTransport::SourceMetadataChanged(
  const source_metadata_t& source_metadata) {

  auto track_count = source_metadata.track_count;
  auto tracks = source_metadata.tracks;

  LOG(INFO) << __func__ << "AIDL: " << track_count << " track(s) received";
  if (track_count == 0) {
    LOG(WARNING) << __func__ << ": Invalid number of metadata changed tracks";
    return;
  }

  auto usage = source_metadata.tracks->usage;

  LOG(INFO) << __func__ << ", content_type=" << tracks->content_type
                        << ", track_count: " << track_count
                        << ", usage: " << usage;

  btif_ahim_update_src_metadata(source_metadata);

}

void A2dpTransport::SinkMetadataChanged(
  const sink_metadata_t& sink_metadata) {

  auto track_count = sink_metadata.track_count;
  auto tracks = sink_metadata.tracks;

  LOG(INFO) << __func__ << "AIDL: " << track_count << " track(s) received";
  if (track_count == 0) {
    LOG(WARNING) << __func__ << ": Invalid number of metadata changed tracks";
    return;
  }

  auto source = sink_metadata.tracks->source;

  LOG(INFO) << __func__ << ", track_count: " << track_count
                        << ", source: " << source;
  if (is_a2dp_split_sink_enabled()) {
    sink_latency =  static_cast<uint16_t>(sink_metadata.tracks->gain);
    LOG(INFO) << __func__ << ", sink_latency: " << sink_latency;
    btif_dispatch_sm_event(BTIF_AV_SINK_OFFLOAD_SINK_LATENCY_EVT, NULL, 0);
  } else {
    btif_ahim_update_sink_metadata(sink_metadata);
  }
}

void A2dpTransport::SetLatencyMode(bool is_low_latency) {
  LOG(INFO) << __func__ << " is_low_latency: " << is_low_latency;
  btif_ahim_set_latency_mode(is_low_latency);
}

tA2DP_CTRL_CMD A2dpTransport::GetPendingCmd() const {
LOG(ERROR) << ": AIDL Is this function called";
  return a2dp_pending_cmd_;
}

void A2dpTransport::ResetPendingCmd() {
  a2dp_pending_cmd_ = A2DP_CTRL_CMD_NONE;
}

void A2dpTransport::ResetPresentationPosition() {
  remote_delay_report_ = 0;
  total_bytes_read_ = 0;
  data_position_ = {};
}

void A2dpTransport::LogBytesRead(size_t bytes_read) {
  if (bytes_read != 0) {
    total_bytes_read_ += bytes_read;
    clock_gettime(CLOCK_MONOTONIC, &data_position_);
  }
}

/***
 *
 * Global functions and variables
 *
 ***/

// delay reports from AVDTP is based on 1/10 ms (100us)
void A2dpTransport::SetRemoteDelay(uint16_t delay_report) {
  remote_delay_report_ = delay_report;
}

// Common interface to call-out into Bluetooth Audio HAL
BluetoothAudioSinkClientInterface* software_hal_interface = nullptr;
BluetoothAudioSinkClientInterface* offloading_hal_interface = nullptr;
BluetoothAudioSinkClientInterface* active_hal_interface = nullptr;
auto session_type = SessionType::UNKNOWN;

// Save the value if the remote reports its delay before this interface is
// initialized
uint16_t remote_delay = 0;

bool btaudio_a2dp_disabled = false;
bool is_configured = false;

BluetoothAudioCtrlAck a2dp_ack_to_bt_audio_ctrl_ack(tA2DP_CTRL_ACK ack) {
  switch (ack) {
    case A2DP_CTRL_ACK_SUCCESS:
      return BluetoothAudioCtrlAck::SUCCESS_FINISHED;
    case A2DP_CTRL_ACK_PENDING:
      return BluetoothAudioCtrlAck::PENDING;
    case A2DP_CTRL_ACK_INCALL_FAILURE:
      return BluetoothAudioCtrlAck::FAILURE_BUSY;
    case A2DP_CTRL_ACK_DISCONNECT_IN_PROGRESS:
      return BluetoothAudioCtrlAck::FAILURE_UNSUPPORTED;
    case A2DP_CTRL_ACK_UNSUPPORTED: /* Offloading but resource failure */
      return BluetoothAudioCtrlAck::FAILURE_UNSUPPORTED;
    case A2DP_CTRL_ACK_FAILURE:
      return BluetoothAudioCtrlAck::FAILURE;
    default:
      return BluetoothAudioCtrlAck::FAILURE;
  }
}

bool a2dp_get_selected_hal_codec_config(CodecConfiguration* codec_config, int profile) {

  if (profile == A2DP_SINK) {
    uint8_t codec_type;
    tA2DP_ENCODER_INIT_PEER_PARAMS peer_param;
    bta_av_co_get_peer_params(&peer_param);
    peer_param.peer_mtu = MAX_MTU_SIZE;
    codec_type = A2DP_GetCodecType((const uint8_t*)active_codec_info);
    LOG(INFO) << __func__ << ": codec_type" << loghex(codec_type);
    // Obtain the MTU
    codec_config->peerMtu = peer_param.peer_mtu - A2DP_HEADER_SIZE;
    if (A2DP_MEDIA_CT_SBC == codec_type) {
      LOG(INFO) << __func__ << ": SBC codec " ;
      codec_config->codecType = CodecType::SBC;
      SbcConfiguration sbc_config = {};
      tA2DP_SBC_CIE sbc_cie;

      tA2DP_STATUS a2dp_status = A2DP_ParseInfoSbc(&sbc_cie, active_codec_info, false);

      if (a2dp_status != A2DP_SUCCESS) {
        LOG(ERROR) << __func__ << ": cannot decode codec information " << a2dp_status;
        return false;
      }

      switch (sbc_cie.samp_freq) {
        case A2DP_SBC_IE_SAMP_FREQ_16:
          sbc_config.sampleRateHz = 16000;
          break;
        case A2DP_SBC_IE_SAMP_FREQ_44:
          sbc_config.sampleRateHz = 44100;
          break;
        case A2DP_SBC_IE_SAMP_FREQ_48:
          sbc_config.sampleRateHz = 48000;
          break;
        default:
          LOG(ERROR) << __func__
                     << ": Unknown SBC freq=" << sbc_cie.samp_freq;
          sbc_config.sampleRateHz = 0;
          return false;
      }

      switch (sbc_cie.ch_mode) {
        case A2DP_SBC_IE_CH_MD_JOINT:
          sbc_config.channelMode = SbcChannelMode::JOINT_STEREO;
          break;
        case A2DP_SBC_IE_CH_MD_STEREO:
         sbc_config.channelMode = SbcChannelMode::STEREO;
          break;
        case A2DP_SBC_IE_CH_MD_DUAL:
          sbc_config.channelMode = SbcChannelMode::DUAL;
          break;
        case A2DP_SBC_IE_CH_MD_MONO:
          sbc_config.channelMode = SbcChannelMode::MONO;
          break;
        default:
          LOG(ERROR) << __func__
                     << ": Unknown SBC channel_mode=" << sbc_cie.ch_mode;
           sbc_config.channelMode = SbcChannelMode::UNKNOWN;
          return false;
      }

      switch (sbc_cie.block_len) {
        case A2DP_SBC_IE_BLOCKS_4:
           sbc_config.blockLength = 4;
          break;
        case A2DP_SBC_IE_BLOCKS_8:
          sbc_config.blockLength = 8;
          break;
        case A2DP_SBC_IE_BLOCKS_12:
          sbc_config.blockLength = 12;
          break;
        case A2DP_SBC_IE_BLOCKS_16:
          sbc_config.blockLength = 16;
          break;
        default:
          LOG(ERROR) << __func__
                     << ": Unknown SBC block_length=" << sbc_cie.block_len;
          return false;
      }

      switch (sbc_cie.num_subbands) {
        case A2DP_SBC_IE_SUBBAND_4:
          sbc_config.numSubbands = 4;
          break;
        case A2DP_SBC_IE_SUBBAND_8:
          sbc_config.numSubbands = 8;
          break;
        default:
          LOG(ERROR) << __func__ << ": Unknown SBC Subbands=" << sbc_cie.num_subbands;
          return false;
      }

      switch (sbc_cie.alloc_method) {
        case A2DP_SBC_IE_ALLOC_MD_S:
          sbc_config.allocMethod = SbcAllocMethod::ALLOC_MD_S;
          break;
        case A2DP_SBC_IE_ALLOC_MD_L:
          sbc_config.allocMethod = SbcAllocMethod::ALLOC_MD_L;
          break;
        default:
          LOG(ERROR) << __func__
                     << ": Unknown SBC alloc_method=" << sbc_cie.alloc_method;
          return false;
      }

      sbc_config.minBitpool = sbc_cie.min_bitpool;
      sbc_config.maxBitpool = sbc_cie.max_bitpool;

      sbc_config.bitsPerSample = 16;
      codec_config->encodedAudioBitrate =
                    1000 * A2DP_GetOffloadBitrateSbcUsingCodecInfo(
                    active_codec_info, true);
      LOG(INFO) << __func__ << "SBC bitrate" << codec_config->encodedAudioBitrate;

      codec_config->config.set<CodecConfiguration::CodecSpecific::sbcConfig>( sbc_config);

    } else if (A2DP_MEDIA_CT_AAC == codec_type) {
      LOG(INFO) << __func__ << ": AAC codec " ;
      codec_config->codecType = CodecType::AAC;
      AacConfiguration aac_config = {};

      aac_config.objectType = AacObjectType::MPEG2_LC;;
      bool is_AAC_frame_ctrl_stack_enable =
                      controller_get_interface()->supports_aac_frame_ctl();
      uint32_t codec_based_bit_rate = 0;
      uint32_t mtu_based_bit_rate = 0;
      LOG(INFO) << __func__ << "Stack AAC frame control enabled"
                            << is_AAC_frame_ctrl_stack_enable;
      tA2DP_AAC_CIE aac_cie;
      if(!A2DP_GetAacCIE(active_codec_info, &aac_cie)) {
        LOG(ERROR) << __func__ << ": Unable to get AAC CIE";
        return false;
      }
      codec_based_bit_rate = aac_cie.bitRate;

      switch (aac_cie.sampleRate) {
        case A2DP_AAC_SAMPLING_FREQ_16000:
          aac_config.sampleRateHz = 16000;
          break;
        case A2DP_AAC_SAMPLING_FREQ_24000:
          aac_config.sampleRateHz = 24000;
          break;
        case A2DP_AAC_SAMPLING_FREQ_44100:
          aac_config.sampleRateHz = 44100;
          break;
        case A2DP_AAC_SAMPLING_FREQ_48000:
          aac_config.sampleRateHz = 48000;
          break;
        case A2DP_AAC_SAMPLING_FREQ_96000:
          aac_config.sampleRateHz = 96000;
          break;
        default:
          LOG(ERROR) << __func__
                     << ": Unknown SBC freq=" << aac_cie.sampleRate;
          aac_config.sampleRateHz = 0;
          return false;
      }

      switch (aac_cie.channelMode) {
        case A2DP_AAC_CHANNEL_MODE_MONO:
          aac_config.channelMode = ChannelMode::MONO;
          break;
        case A2DP_AAC_CHANNEL_MODE_STEREO:
          aac_config.channelMode = ChannelMode::STEREO;
          break;
        default:
          LOG(ERROR) << __func__
                     << ": Unknown AAC channel_mode=" << aac_cie.channelMode;
          aac_config.channelMode = ChannelMode::UNKNOWN;
          break;
      }

      switch (aac_cie.bits_per_sample) {
        case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16:
          aac_config.bitsPerSample = 16;
          break;
        case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24:
          aac_config.bitsPerSample = 24;
          break;
        case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32:
          aac_config.bitsPerSample = 32;
          break;
        default:
          LOG(ERROR) << __func__
                     << ": Unknown AAC bits per sample =" << aac_cie.bits_per_sample;
          aac_config.bitsPerSample = 16;
          break;
      }

      uint8_t vbr_enabled = aac_cie.variableBitRateSupport;
      switch (vbr_enabled) {
        case A2DP_AAC_VARIABLE_BIT_RATE_ENABLED:
          LOG(INFO) << __func__ << " Enabled AAC VBR =" << +vbr_enabled;
          aac_config.variableBitRateEnabled = true;
          break;
        case A2DP_AAC_VARIABLE_BIT_RATE_DISABLED:
          LOG(INFO) << __func__ << " Disabled AAC VBR =" << +vbr_enabled;
          aac_config.variableBitRateEnabled = false;
          break;
        default:
          LOG(ERROR) << __func__ << ": Unknown AAC VBR=" << +vbr_enabled;
          return false;
      }

      if (is_AAC_frame_ctrl_stack_enable) {
        if (btif_av_is_peer_edr() && (btif_av_peer_supports_3mbps() == FALSE)) {
          // This condition would be satisfied only if the remote device is
          // EDR and supports only 2 Mbps, but the effective AVDTP MTU size
          // exceeds the 2DH5 packet size.
          if (peer_param.peer_mtu > MAX_2MBPS_AVDTP_MTU) {
            peer_param.peer_mtu = MAX_2MBPS_AVDTP_MTU;
            codec_config->peerMtu = peer_param.peer_mtu - A2DP_HEADER_SIZE;
            APPL_TRACE_WARNING("%s Restricting MTU size to %d", __func__, peer_param.peer_mtu);
          }
        }
        int sample_rate = A2DP_GetTrackSampleRate(active_codec_info);
        mtu_based_bit_rate = (peer_param.peer_mtu - AAC_LATM_HEADER)
                                            * (8 * sample_rate / AAC_SAMPLE_SIZE);
        LOG(INFO) << __func__ << "sample_rate " << sample_rate;
        LOG(INFO) << __func__ << " peer_mtu " << peer_param.peer_mtu;
        LOG(INFO) << __func__ << " codec_bit_rate " << codec_based_bit_rate
                  << " MTU bitrate " << mtu_based_bit_rate;
        codec_config->encodedAudioBitrate = (codec_based_bit_rate < mtu_based_bit_rate) ?
                                             codec_based_bit_rate:mtu_based_bit_rate;
      } else {
        codec_config->encodedAudioBitrate = codec_based_bit_rate;
      }
      codec_config->config.set<CodecConfiguration::CodecSpecific::aacConfig>(aac_config);
    }
    return true;
  } else {
    A2dpCodecConfig* a2dp_config = bta_av_get_a2dp_current_codec();
    uint8_t p_codec_info[AVDT_CODEC_SIZE];
    if (a2dp_config == nullptr) {
      LOG(WARNING) << __func__ << "AIDL: failure to get A2DP codec config";
      return false;
    }
    btav_a2dp_codec_config_t current_codec = a2dp_config->getCodecConfig();
    switch (current_codec.codec_type) {
      case BTAV_A2DP_CODEC_INDEX_SOURCE_SBC:
        [[fallthrough]];
      case BTAV_A2DP_CODEC_INDEX_SINK_SBC: {
        if (!A2dpSbcToHalConfig(codec_config, a2dp_config)) {
          return false;
        }
        break;
      }
      case BTAV_A2DP_CODEC_INDEX_SOURCE_AAC:
        [[fallthrough]];
      case BTAV_A2DP_CODEC_INDEX_SINK_AAC: {
        if (!A2dpAacToHalConfig(codec_config, a2dp_config)) {
          return false;
        }
        break;
      }
      case BTAV_A2DP_CODEC_INDEX_SOURCE_APTX:
        [[fallthrough]];
      case BTAV_A2DP_CODEC_INDEX_SOURCE_APTX_HD: {
        if (!A2dpAptxToHalConfig(codec_config, a2dp_config)) {
          return false;
        }
        break;
      }
      case BTAV_A2DP_CODEC_INDEX_SOURCE_LDAC: {
        if (!A2dpLdacToHalConfig(codec_config, a2dp_config)) {
          return false;
        }
        break;
      }
      case BTAV_A2DP_CODEC_INDEX_SOURCE_APTX_ADAPTIVE: {
        if (!A2dpAptxAdaptiveToHalConfig(codec_config, a2dp_config)) {
          return false;
        }
  	  break;
      }
      case BTAV_A2DP_CODEC_INDEX_MAX:
        [[fallthrough]];
      default:
        LOG(ERROR) << __func__
                   << "aidl: Unknown codec_type=" << current_codec.codec_type;
        return false;
    }
#  if 0
    codec_config->encodedAudioBitrate = a2dp_config->getTrackBitRate();
    // Obtain the MTU
    RawAddress peer_addr = btif_av_source_active_peer();
    tA2DP_ENCODER_INIT_PEER_PARAMS peer_param;
    bta_av_co_get_peer_params(peer_addr, &peer_param);
    int effectiveMtu = bta_av_co_get_encoder_effective_frame_size();
    if (effectiveMtu > 0 && effectiveMtu < peer_param.peer_mtu) {
      codec_config->peerMtu = effectiveMtu;
    } else {
      codec_config->peerMtu = peer_param.peer_mtu;
    }
    if (current_codec.codec_type == BTAV_A2DP_CODEC_INDEX_SOURCE_SBC &&
        codec_config->config.get<CodecConfiguration::CodecSpecific::sbcConfig>()
                .maxBitpool <= A2DP_SBC_BITPOOL_MIDDLE_QUALITY) {
      codec_config->peerMtu = MAX_2MBPS_AVDTP_MTU;
    } else if (codec_config->peerMtu > MAX_3MBPS_AVDTP_MTU) {
      codec_config->peerMtu = MAX_3MBPS_AVDTP_MTU;
    }
#  endif
    tA2DP_ENCODER_INIT_PEER_PARAMS peer_param;
    bta_av_co_get_peer_params(&peer_param);
    // Obtain the MTU
    memset(p_codec_info, 0, AVDT_CODEC_SIZE);
    if (!a2dp_config->copyOutOtaCodecConfig(p_codec_info))
    {
      LOG(ERROR) << "AIDL No valid codec config";
      return false;
    }
    uint8_t codec_type;
    uint32_t bitrate = 0;
    codec_type = A2DP_GetCodecType((const uint8_t*)p_codec_info);
    codec_config->peerMtu = peer_param.peer_mtu - A2DP_HEADER_SIZE;
    if (A2DP_MEDIA_CT_SBC == codec_type) {
      bitrate = A2DP_GetOffloadBitrateSbc(a2dp_config, peer_param.is_peer_edr,
                                          (const uint8_t*)p_codec_info);
      LOG(INFO) << __func__ << "AIDL SBC bitrate" << bitrate;
      codec_config->encodedAudioBitrate = bitrate * 1000;
    }  else if (A2DP_MEDIA_CT_NON_A2DP == codec_type) {
      int samplerate = A2DP_GetTrackSampleRate(p_codec_info);
      if ((A2DP_VendorCodecGetVendorId(p_codec_info)) == A2DP_LDAC_VENDOR_ID) {
        codec_config->encodedAudioBitrate = A2DP_GetTrackBitRate(p_codec_info);
        LOG(INFO) << __func__ << "AIDL LDAC bitrate" << codec_config->encodedAudioBitrate;
      } else {
        /* BR = (Sampl_Rate * PCM_DEPTH * CHNL)/Compression_Ratio */
        int bits_per_sample = 16; // TODO
        codec_config->encodedAudioBitrate = (samplerate * bits_per_sample * 2)/4;
        LOG(INFO) << __func__ << "AIDL Aptx bitrate" << codec_config->encodedAudioBitrate;
      }
    }  else if (A2DP_MEDIA_CT_AAC == codec_type) {
      bool is_AAC_frame_ctrl_stack_enable =
                      controller_get_interface()->supports_aac_frame_ctl();
      uint32_t codec_based_bit_rate = 0;
      uint32_t mtu_based_bit_rate = 0;
      LOG(INFO) << __func__ << "AIDL Stack AAC frame control enabled"
                            << is_AAC_frame_ctrl_stack_enable;
      tA2DP_AAC_CIE aac_cie;
      if(!A2DP_GetAacCIE(p_codec_info, &aac_cie)) {
        LOG(ERROR) << __func__ << "AIDL : Unable to get AAC CIE";
        return false;
      }
      codec_based_bit_rate = aac_cie.bitRate;
      if (is_AAC_frame_ctrl_stack_enable) {
        int sample_rate = A2DP_GetTrackSampleRate(p_codec_info);
        mtu_based_bit_rate = (peer_param.peer_mtu - AAC_LATM_HEADER)
                                            * (8 * sample_rate / AAC_SAMPLE_SIZE);
        LOG(INFO) << __func__ << "aidl: sample_rate " << sample_rate;
        LOG(INFO) << __func__ << "aidl:  peer_mtu " << peer_param.peer_mtu;
        LOG(INFO) << __func__ << "aidl: codec_bit_rate " << codec_based_bit_rate
                  << " MTU bitrate " << mtu_based_bit_rate;
        codec_config->encodedAudioBitrate = (codec_based_bit_rate < mtu_based_bit_rate) ?
                                             codec_based_bit_rate:mtu_based_bit_rate;
      } else {
        codec_config->encodedAudioBitrate = codec_based_bit_rate;
      }
    }
    //codec_config_global = codec_config;
    LOG(INFO) << __func__ << "aidl: CodecConfiguration=" << codec_config->toString();
    return true;
  }
}

bool a2dp_get_selected_hal_pcm_config(PcmConfiguration* pcm_config) {
  if (pcm_config == nullptr) return false;
  A2dpCodecConfig* a2dp_codec_configs = bta_av_get_a2dp_current_codec();
  if (a2dp_codec_configs == nullptr) {
    LOG(WARNING) << __func__ << "aidl: failure to get A2DP codec config";
    *pcm_config = BluetoothAudioSinkClientInterface::kInvalidPcmConfiguration;
    return false;
  }

  btav_a2dp_codec_config_t current_codec = a2dp_codec_configs->getCodecConfig();
  pcm_config->sampleRateHz = A2dpCodecToHalSampleRate(current_codec);
  pcm_config->bitsPerSample = A2dpCodecToHalBitsPerSample(current_codec);
  pcm_config->channelMode = A2dpCodecToHalChannelMode(current_codec);
  //pcm_config_global = pcm_config;
  return (pcm_config->sampleRateHz > 0 && pcm_config->bitsPerSample > 0 &&
          pcm_config->channelMode != ChannelMode::UNKNOWN);
}

// Checking if new bluetooth_audio is supported
bool is_hal_force_disabled() {
  if (!is_configured) {
    btaudio_a2dp_disabled =
        property_get_bool(BLUETOOTH_AUDIO_HAL_PROP_DISABLED, false);
    is_configured = true;
  }
  return btaudio_a2dp_disabled;
}

}  // namespace

bool update_codec_offloading_capabilities(
    const std::vector<btav_a2dp_codec_config_t>& framework_preference) {
  return ::bluetooth::audio::aidl::codec::UpdateOffloadingCapabilities(
      framework_preference);
}

// Checking if new bluetooth_audio is enabled
bool is_hal_enabled() { return active_hal_interface != nullptr; }

void NotifyHalRestart() {
  LOG(INFO) << __func__ << "NotifyHalRestart";
  btif_ahim_process_request(A2DP_CTRL_NOTIFY_HAL_RESTART, A2DP, TO_AIR, false);
}

// Checking if new bluetooth_audio is enabled
bool is_aidl_hal_available() {
  if (is_aidl_checked) return is_aidl_available;

  is_aidl_available = BluetoothAudioClientInterface::is_aidl_available();
  is_aidl_checked = true;
  LOG(INFO) << __func__ << ": " << is_aidl_available;
  return is_aidl_available;
}

// Check if new bluetooth_audio is running with offloading encoders
bool is_hal_offloading() {
  if (!is_hal_enabled()) {
    return false;
  }
  return active_hal_interface->GetTransportInstance()->GetSessionType() ==
         SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH;
}

// Initialize BluetoothAudio HAL: openProvider
bool init(thread_t* message_loop) {
  LOG(INFO) << __func__;

  if (is_hal_force_disabled()) {
    LOG(ERROR) << __func__ << "aidl: BluetoothAudio HAL is disabled";
    return false;
  }

  if (!BluetoothAudioClientInterface::is_aidl_available()) {
    LOG(ERROR) << __func__
               << "aidl: BluetoothAudio AIDL implementation does not exist";
    return false;
  }

  auto a2dp_sink =
      new A2dpTransport(SessionType::A2DP_SOFTWARE_ENCODING_DATAPATH);
  software_hal_interface =
      new BluetoothAudioSinkClientInterface(a2dp_sink, message_loop);
  if (!software_hal_interface->IsValid()) {
    LOG(WARNING) << __func__ << "aidl:: BluetoothAudio HAL for A2DP is invalid?!";
    delete software_hal_interface;
    software_hal_interface = nullptr;
    delete a2dp_sink;
    return false;
  }

  if (btif_av_is_split_a2dp_enabled() && !is_a2dp_split_sink_enabled()) {
    a2dp_sink =
        new A2dpTransport(SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH);

    offloading_hal_interface =
        new BluetoothAudioSinkClientInterface(a2dp_sink, message_loop);
    if (!offloading_hal_interface->IsValid()) {
      LOG(FATAL) << __func__
                 << "aidl: BluetoothAudio HAL for A2DP offloading is invalid?!";
      delete offloading_hal_interface;
      offloading_hal_interface = nullptr;
      delete a2dp_sink;
      a2dp_sink = static_cast<A2dpTransport*>(
          software_hal_interface->GetTransportInstance());
      delete software_hal_interface;
      software_hal_interface = nullptr;
      delete a2dp_sink;
      return false;
    }
  } else if (is_a2dp_split_sink_enabled()) {
    a2dp_sink =
        new A2dpTransport(SessionType::A2DP_HARDWARE_OFFLOAD_DECODING_DATAPATH);

    offloading_hal_interface =
        new BluetoothAudioSinkClientInterface(a2dp_sink, message_loop);
    if (!offloading_hal_interface->IsValid()) {
      LOG(FATAL) << __func__
                 << "aidl: BluetoothAudio HAL for A2DP offloading is invalid?!";
      delete offloading_hal_interface;
      offloading_hal_interface = nullptr;
      delete a2dp_sink;
      a2dp_sink = static_cast<A2dpTransport*>(
          software_hal_interface->GetTransportInstance());
      delete software_hal_interface;
      software_hal_interface = nullptr;
      delete a2dp_sink;
      return false;
    }
  }

  active_hal_interface =
      (offloading_hal_interface != nullptr ? offloading_hal_interface
                                           : software_hal_interface);

  if (remote_delay != 0) {
    LOG(INFO) << __func__ << "aidl: restore DELAY "
              << static_cast<float>(remote_delay / 10.0) << " ms";
    static_cast<A2dpTransport*>(active_hal_interface->GetTransportInstance())
        ->SetRemoteDelay(remote_delay);
    remote_delay = 0;
  }
  return true;
}

// Clean up BluetoothAudio HAL
void cleanup() {
  if (!is_hal_enabled()) return;
  end_session();

  if (active_hal_interface != nullptr) {
    auto a2dp_sink = active_hal_interface->GetTransportInstance();
    static_cast<A2dpTransport*>(a2dp_sink)->ResetPendingCmd();
    static_cast<A2dpTransport*>(a2dp_sink)->ResetPresentationPosition();
    active_hal_interface = nullptr;
  }

  if (software_hal_interface != nullptr) {
    auto a2dp_sink = software_hal_interface->GetTransportInstance();
    delete software_hal_interface;
    software_hal_interface = nullptr;
    delete a2dp_sink;
  }

  if (offloading_hal_interface != nullptr) {
    auto a2dp_sink = offloading_hal_interface->GetTransportInstance();
    delete offloading_hal_interface;
    offloading_hal_interface = nullptr;
    delete a2dp_sink;
  }

  remote_delay = 0;
}

void update_session_params(uint8_t param) {
    if (!is_hal_enabled()) {
      LOG(ERROR) << __func__ << "aidl: BluetoothAudio HAL is not enabled";
      return ;
    }
}

SessionType get_session_type() {
    if (!is_hal_enabled()) {
      LOG(ERROR) << __func__ << "aidl: BluetoothAudio HAL is not enabled";
      return SessionType::UNKNOWN;
    }
    if (active_hal_interface) {
       return active_hal_interface->GetTransportInstance()->GetSessionType();
    } else return SessionType::UNKNOWN;
}


// check for audio feeding params are same for newly set up codec vs
// what was already set up on hidl side
bool is_restart_session_needed() {
  if (!is_hal_enabled()) {
    LOG(ERROR) << __func__ << "aidl: BluetoothAudio HAL is not enabled";
    return false;
  }
  A2dpCodecConfig* a2dp_config = bta_av_get_a2dp_current_codec();
  if (active_hal_interface->GetTransportInstance()->GetSessionType() ==
      SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH) {
      return codec::a2dp_is_audio_codec_config_params_changed_aidl(&codec_config_global, a2dp_config);
  } else if (active_hal_interface->GetTransportInstance()->GetSessionType() ==
      SessionType::A2DP_SOFTWARE_ENCODING_DATAPATH) {
      return codec::a2dp_is_audio_pcm_config_params_changed_aidl(&pcm_config_global, a2dp_config);
  }
  return true;
}

// Set up the codec into BluetoothAudio HAL
bool setup_codec(uint8_t profile) {
  bool should_codec_offloading = false;
  if (!is_hal_enabled()) {
    LOG(ERROR) << __func__ << "aidl: BluetoothAudio HAL is not enabled";
    return false;
  }
  CodecConfiguration codec_config{};
  if (!a2dp_get_selected_hal_codec_config(&codec_config, profile)) {
    LOG(ERROR) << __func__ << "aidl: Failed to get CodecConfiguration";
    return false;
  }
  //codec_config_global = codec_config;
  memcpy(&codec_config_global, &codec_config, sizeof(CodecConfiguration));
  if(is_a2dp_split_sink_enabled()) {
    should_codec_offloading = true;
  } else {
    should_codec_offloading = bluetooth::audio::aidl::codec::IsCodecOffloadingEnabled(codec_config);
  }
  if (should_codec_offloading && !is_hal_offloading()) {
    LOG(WARNING) << __func__ << "aidl: Switching BluetoothAudio HAL to Hardware";
    end_session();
    active_hal_interface = offloading_hal_interface;
  } else if (!should_codec_offloading && is_hal_offloading()) {
    LOG(WARNING) << __func__ << "aidl: Switching BluetoothAudio HAL to Software";
    end_session();
    active_hal_interface = software_hal_interface;
  }

  AudioConfiguration audio_config{};
  if (active_hal_interface && active_hal_interface->GetTransportInstance() &&
      (active_hal_interface->GetTransportInstance()->GetSessionType() ==
      SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH ||
      active_hal_interface->GetTransportInstance()->GetSessionType() ==
      SessionType::A2DP_HARDWARE_OFFLOAD_DECODING_DATAPATH)) {
    audio_config.set<AudioConfiguration::a2dpConfig>(codec_config);
  } else {
    PcmConfiguration pcm_config{};
    if (!a2dp_get_selected_hal_pcm_config(&pcm_config)) {
      LOG(ERROR) << __func__ << "aidl: Failed to get PcmConfiguration";
      return false;
    }
    pcm_config_global = pcm_config;
    audio_config.set<AudioConfiguration::pcmConfig>(pcm_config);
  }

  A2dpCodecConfig* a2dp_config = bta_av_get_a2dp_current_codec();
  if (a2dp_config && active_hal_interface) {
    btav_a2dp_codec_config_t current_codec = a2dp_config->getCodecConfig();
    if (current_codec.codec_type == BTAV_A2DP_CODEC_INDEX_SOURCE_APTX_ADAPTIVE) {
      active_hal_interface->set_low_latency_allowed(true);
    }
    else {
      active_hal_interface->set_low_latency_allowed(false);
    }
  }

  return active_hal_interface->UpdateAudioConfig(audio_config);
}

void start_session() {
  if (!is_hal_enabled()) {
    LOG(ERROR) << __func__ << "aidl: BluetoothAudio HAL is not enabled";
    return;
  }
  active_hal_interface->StartSession();
}

void end_session() {
  if (!is_hal_enabled()) {
    LOG(ERROR) << __func__ << "aidl: BluetoothAudio HAL is not enabled";
    return;
  }
  active_hal_interface->EndSession();
  static_cast<A2dpTransport*>(active_hal_interface->GetTransportInstance())
      ->ResetPendingCmd();
  static_cast<A2dpTransport*>(active_hal_interface->GetTransportInstance())
      ->ResetPresentationPosition();
}

void ack_stream_started(const tA2DP_CTRL_ACK& ack) {
  auto ctrl_ack = a2dp_ack_to_bt_audio_ctrl_ack(ack);
  LOG(INFO) << __func__ << "aidl: result=" << ctrl_ack;

  if (active_hal_interface == nullptr) return;

  auto a2dp_sink =
      static_cast<A2dpTransport*>(active_hal_interface->GetTransportInstance());
  auto pending_cmd = a2dp_sink->GetPendingCmd();
  if (pending_cmd == A2DP_CTRL_CMD_START) {
    active_hal_interface->StreamStarted(ctrl_ack);
  } else {
    LOG(WARNING) << __func__ << "aidl: pending=" << pending_cmd
                 << " ignore result=" << ctrl_ack;
    return;
  }
  if (ctrl_ack != BluetoothAudioCtrlAck::PENDING) {
    a2dp_sink->ResetPendingCmd();
  }
}

void ack_stream_suspended(const tA2DP_CTRL_ACK& ack) {
  auto ctrl_ack = a2dp_ack_to_bt_audio_ctrl_ack(ack);
  LOG(INFO) << __func__ << "aidl: result=" << ctrl_ack;

  if (active_hal_interface == nullptr) return;

  auto a2dp_sink =
      static_cast<A2dpTransport*>(active_hal_interface->GetTransportInstance());
  auto pending_cmd = a2dp_sink->GetPendingCmd();
  if (pending_cmd == A2DP_CTRL_CMD_SUSPEND) {
    active_hal_interface->StreamSuspended(ctrl_ack);
  } else if (pending_cmd == A2DP_CTRL_CMD_STOP) {
    LOG(INFO) << __func__ << "aidl: A2DP_CTRL_CMD_STOP result=" << ctrl_ack;
  } else {
    LOG(WARNING) << __func__ << "aidl: pending=" << pending_cmd
                 << " ignore result=" << ctrl_ack;
    return;
  }
  if (ctrl_ack != BluetoothAudioCtrlAck::PENDING) {
    a2dp_sink->ResetPendingCmd();
  }
}

tA2DP_CTRL_CMD GetPendingCmd() {
  LOG(ERROR) << "aidl: Is this function called";
  if(!active_hal_interface) return A2DP_CTRL_CMD_NONE;
  auto a2dp_sink =
    static_cast<A2dpTransport*>(active_hal_interface->GetTransportInstance());

  if (a2dp_sink != NULL) {
     return a2dp_sink->GetPendingCmd();
  } else {
     LOG(ERROR) << "a2dp sink is null";
     return A2DP_CTRL_CMD_NONE;
  }
}

uint16_t GetSinkLatency() {
  return sink_latency;
}

void ResetPendingCmd() {
  if(!active_hal_interface) return;
  auto a2dp_sink =
    static_cast<A2dpTransport*>(active_hal_interface->GetTransportInstance());
  return a2dp_sink->ResetPendingCmd();
}

// Read from the FMQ of BluetoothAudio HAL
size_t read(uint8_t* p_buf, uint32_t len) {
  if (!is_hal_enabled()) {
    LOG(ERROR) << __func__ << "aidl: BluetoothAudio HAL is not enabled";
    return 0;
  } else if (is_hal_offloading()) {
    LOG(ERROR) << __func__ << "aidl: session_type="
               << toString(active_hal_interface->GetTransportInstance()
                               ->GetSessionType())
               << " aidl: is not A2DP_SOFTWARE_ENCODING_DATAPATH";
    return 0;
  }
  return active_hal_interface->ReadAudioData(p_buf, len);
}

// Update A2DP delay report to BluetoothAudio HAL
void set_remote_delay(uint16_t delay_report) {
  if (!is_hal_enabled()) {
    LOG(INFO) << __func__ << "aidl: :  not ready for DelayReport "
              << static_cast<float>(delay_report / 10.0) << " ms";
    remote_delay = delay_report;
    return;
  }
  VLOG(1) << __func__ << "aidl: DELAY " << static_cast<float>(delay_report / 10.0)
          << " ms";
  static_cast<A2dpTransport*>(active_hal_interface->GetTransportInstance())
      ->SetRemoteDelay(delay_report);
}

}  // namespace a2dp
}  // namespace aidl
}  // namespace audio
}  // namespace bluetooth
