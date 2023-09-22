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
#include "protobuf/proto/a2dp.pb.h"
#define AAC_SAMPLE_SIZE  1024
#define AAC_LATM_HEADER  12
/*
 * SBC Codec specific parameters
 * */
#define SBC_sf16000 0
#define SBC_sf32000 1
#define SBC_sf44100 2
#define SBC_sf48000 3

#define SBC_MONO 0
#define SBC_DUAL 1
#define SBC_STEREO 2
#define SBC_JOINT_STEREO 3

#define SUBBAND_4 8
#define SUBBAND_8 4

//OFFLOAD DEFAULT_BITRATE for 48khz
#define A2DP_SBC_DEFAULT_OFFLOAD_BITRATE 345
#define A2DP_SBC_NON_EDR_OFFLOAD_MAX_RATE 237

/* Define the bitrate step when trying to match bitpool value */
#define A2DP_SBC_BITRATE_STEP 5
/* * * * * * * * * * * * * * * * * * * * * * * */

namespace a2dp_proto = a2dp::synergy::SynergyProto;

namespace bluetooth {
namespace audio {
namespace aidl {
namespace a2dp {

uint16_t sbc_calulate_offload_bitrate(btav_a2dp_codec_config_t* codec, btif_a2dp_codec_config_callback_t* peer);
uint16_t GetChannelMode(a2dp_proto::ss_btav_a2dp_codec_channel_mode_t ch_mode);
int GetSamplingFeq(btav_a2dp_codec_sample_rate_t samp_freq);
uint16_t GetNumOfChannels(a2dp_proto::ss_btav_a2dp_codec_channel_mode_t ch_mode);
uint8_t GetNumberOfBlocks(uint16_t block_len);

namespace {

using ::aidl::android::hardware::bluetooth::audio::AudioConfiguration;
using ::aidl::android::hardware::bluetooth::audio::ChannelMode;
using ::aidl::android::hardware::bluetooth::audio::CodecConfiguration;
using ::aidl::android::hardware::bluetooth::audio::PcmConfiguration;
using ::aidl::android::hardware::bluetooth::audio::SessionType;
using ::aidl::android::hardware::bluetooth::audio::SbcAllocMethod;
using ::aidl::android::hardware::bluetooth::audio::SbcCapabilities;
using ::aidl::android::hardware::bluetooth::audio::SbcChannelMode;
using ::aidl::android::hardware::bluetooth::audio::SbcConfiguration;
using ::aidl::android::hardware::bluetooth::audio::AacConfiguration;
using ::aidl::android::hardware::bluetooth::audio::AacObjectType;
using ::aidl::android::hardware::bluetooth::audio::CodecType;

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
  btif_av_handle_hidl_req(A2DP_CTRL_CMD_START);
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
  btif_av_handle_hidl_req(A2DP_CTRL_CMD_SUSPEND);
  return a2dp_ack_to_bt_audio_ctrl_ack(status);
}

void A2dpTransport::StopRequest() {
  btif_av_handle_hidl_req(A2DP_CTRL_CMD_STOP);
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

  //btif_ahim_update_src_metadata(source_metadata);

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

  //btif_ahim_update_sink_metadata(sink_metadata);

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
//auto session_type = SessionType::UNKNOWN;

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

bool a2dp_get_selected_hal_codec_config(CodecConfiguration* codec_config) {
  //A2dpCodecConfig* a2dp_config = btif_av_get_a2dp_current_codec();
  //uint8_t p_codec_info[AVDT_CODEC_SIZE];

  btif_a2dp_codec_config_callback_t* current_codec = btif_av_get_a2dp_current_codec();
  if (current_codec == NULL) {
      ALOGI("Current codec is null");
      return false;
  }
  btav_a2dp_codec_config_t* codec_conf = &current_codec->codec_config_;

  switch (codec_conf->codec_type) {
    case BTAV_A2DP_CODEC_INDEX_SOURCE_SBC:
    {
        codec_config->codecType = CodecType::SBC;
        SbcConfiguration sbc_config = {};
        sbc_config.sampleRateHz = A2dpCodecToHalSampleRate(*codec_conf);
          uint8_t channel_mode = codec_conf->channel_mode;
          switch (channel_mode) {
            case a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_JOINT_STEREO:
              sbc_config.channelMode = SbcChannelMode::JOINT_STEREO;
              ALOGI("ch mode:%hhd, %d,", sbc_config.channelMode, a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_JOINT_STEREO);
              break;
            case a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO:
              sbc_config.channelMode = SbcChannelMode::STEREO;
              break;
            case a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_DUAL:
              sbc_config.channelMode = SbcChannelMode::DUAL;
              break;
            case a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_MONO:
              sbc_config.channelMode = SbcChannelMode::MONO;
              break;
            default:
              LOG(ERROR) << __func__ << ": Unknown SBC channel_mode=" << channel_mode;
              sbc_config.channelMode = SbcChannelMode::UNKNOWN;
              return false;
        }

        sbc_config.blockLength = GetNumberOfBlocks(current_codec->block_length);
        ALOGI("block length: %d", sbc_config.blockLength);
        if(sbc_config.blockLength == -1) {
          LOG(ERROR) << __func__ << ": Unknown SBC block_length=" << sbc_config.blockLength;
          return false;
        }
        uint8_t sub_bands = current_codec->num_subbands;
        switch (sub_bands) {
            case A2DP_SBC_IE_SUBBAND_4:
              sbc_config.numSubbands = SUBBAND_4;
              break;
            case A2DP_SBC_IE_SUBBAND_8:
              sbc_config.numSubbands = SUBBAND_8;
              break;
            default:
              LOG(ERROR) << __func__ << ": Unknown SBC Subbands=" << sub_bands;
              return false;
        }
        ALOGI("numSubbands:%d sub_bands:%d", sbc_config.numSubbands,sub_bands);
          uint8_t alloc_method = current_codec->alloc_method;
          switch (alloc_method) {
            case A2DP_SBC_IE_ALLOC_MD_S:
              sbc_config.allocMethod = SbcAllocMethod::ALLOC_MD_S;
              break;
            case A2DP_SBC_IE_ALLOC_MD_L:
              sbc_config.allocMethod = SbcAllocMethod::ALLOC_MD_L;
              break;
            default:
              LOG(ERROR) << __func__ << ": Unknown SBC alloc_method=" << alloc_method;
              return false;
          }
        sbc_config.minBitpool = current_codec->min_bitpool;
        sbc_config.maxBitpool = current_codec->max_bitpool;
        sbc_config.bitsPerSample = A2dpCodecToHalBitsPerSample(*codec_conf);
        codec_config->config.set<CodecConfiguration::CodecSpecific::sbcConfig>(
        sbc_config);
      /*if (!A2dpSbcToHalConfig(codec_config, a2dp_config)) {
        return false;
      }*/
      break;
    }
    case BTAV_A2DP_CODEC_INDEX_SOURCE_AAC:
    {
      /*if (!A2dpAacToHalConfig(codec_config, a2dp_config)) {
        return false;
      }*/
      AacConfiguration aac_config = {};
      codec_config->codecType = CodecType::AAC;

      switch (codec_conf->codec_specific_1) {
        case A2DP_AAC_OBJECT_TYPE_MPEG2_LC:
          aac_config.objectType = AacObjectType::MPEG2_LC;
          break;
        case A2DP_AAC_OBJECT_TYPE_MPEG4_LC:
          aac_config.objectType = AacObjectType::MPEG4_LC;
          break;
        case A2DP_AAC_OBJECT_TYPE_MPEG4_LTP:
          aac_config.objectType = AacObjectType::MPEG4_LTP;
          break;
        case A2DP_AAC_OBJECT_TYPE_MPEG4_SCALABLE:
          aac_config.objectType = AacObjectType::MPEG4_SCALABLE;
          break;
        default:
          LOG(ERROR) << __func__ << ": Unknown AAC object_type=" <<
            codec_conf->codec_specific_1;
          return false;
      }
      ALOGI("AacObjectType::MPEG2_LC= %hhd, %lld", AacObjectType::MPEG2_LC, codec_conf->codec_specific_1);
      //aac_config.objectType = AacObjectType::MPEG2_LC;
      aac_config.sampleRateHz = A2dpCodecToHalSampleRate(*codec_conf);
      aac_config.channelMode = A2dpCodecToHalChannelMode(*codec_conf);
      uint8_t vbr_enabled = codec_conf->codec_specific_2;
      // valid values vbr_enabled: [0,1]
      if (vbr_enabled == 1 ) {
          aac_config.variableBitRateEnabled = true;
      } else {
          aac_config.variableBitRateEnabled = false;
      }
      aac_config.bitsPerSample = A2dpCodecToHalBitsPerSample(*codec_conf);
      ALOGI("AAC obj_type: %hhd, sample rate: %d, channel mode: %hhd,"
        "vbr_enabled: %d, aac_config.bitsPerSample: %d, ", aac_config.objectType,
        aac_config.sampleRateHz,aac_config.channelMode, vbr_enabled,
        aac_config.bitsPerSample);
      codec_config->config.set<CodecConfiguration::CodecSpecific::aacConfig>(
        aac_config);
      break;
    }
#if 0
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
#endif
    case BTAV_A2DP_CODEC_INDEX_MAX:
      [[fallthrough]];
    default:
      LOG(ERROR) << __func__
                 << "aidl: Unknown codec_type=" << codec_conf->codec_type;
      return false;
  }
#if 0
  codec_config->encodedAudioBitrate = a2dp_config->getTrackBitRate();
  // Obtain the MTU
  RawAddress peer_addr = btif_av_source_active_peer();
  tA2DP_ENCODER_INIT_PEER_PARAMS peer_param;
  bta_av_co_get_peer_params(peer_addr, &peer_param);
  int effectiveMtu = bta_av_co_get_encoder_effective_frame_size();
  if (effectiveMtu > 0 && effectiveMtu < current_codec.peer_mtu) {
    codec_config->peerMtu = effectiveMtu;
  } else {
    codec_config->peerMtu = current_codec.peer_mtu;
  }
  if (current_codec.codec_config_.codec_type == BTAV_A2DP_CODEC_INDEX_SOURCE_SBC &&
      codec_config->config.get<CodecConfiguration::CodecSpecific::sbcConfig>()
              .maxBitpool <= A2DP_SBC_BITPOOL_MIDDLE_QUALITY) {
    codec_config->peerMtu = MAX_2MBPS_AVDTP_MTU;
  } else if (codec_config->peerMtu > MAX_3MBPS_AVDTP_MTU) {
    codec_config->peerMtu = MAX_3MBPS_AVDTP_MTU;
  }
#endif
#if 0
  // Obtain the MTU
  memset(p_codec_info, 0, AVDT_CODEC_SIZE);
  if (!a2dp_config->copyOutOtaCodecConfig(p_codec_info))
  {
    LOG(ERROR) << "AIDL No valid codec config";
    return false;
  }
#endif
  uint32_t bitrate = 0;
  codec_config->peerMtu = current_codec->peer_mtu - A2DP_HEADER_SIZE;
  if (BTAV_A2DP_CODEC_INDEX_SOURCE_SBC == codec_conf->codec_type) {
    // to-do: set bit rate here
    bitrate = sbc_calulate_offload_bitrate(codec_conf, current_codec);
    LOG(INFO) << __func__ << " AIDL SBC bitrate:" << bitrate;
    codec_config->encodedAudioBitrate = bitrate * 1000;
  }
#if 0 // Only SBC and AAC support for now
  else if (A2DP_MEDIA_CT_NON_A2DP == codec_type) {

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
  }
#endif
  else if (BTAV_A2DP_CODEC_INDEX_SOURCE_AAC == codec_conf->codec_type) {
    /*bool is_AAC_frame_ctrl_stack_enable =
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
    }*/
    ALOGI("bitrate from stack: %lld", codec_conf->codec_specific_3);
    codec_config->encodedAudioBitrate = codec_conf->codec_specific_3;
  } else {
    ALOGE("Unsupported codec type");
  }
  //codec_config_global = codec_config;
  LOG(INFO) << __func__ << " aidl: CodecConfiguration=" << codec_config->toString();
  return true;
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
bool init(void /*thread_t* message_loop*/) {
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

  if (btif_av_is_split_a2dp_enabled()) {
    auto a2dp_sink =
        new A2dpTransport(SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH);
    offloading_hal_interface =
        new BluetoothAudioSinkClientInterface(a2dp_sink /*, message_loop*/);
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

#if 0
// to-do need to handle this if source config changed
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
#endif
// Set up the codec into BluetoothAudio HAL
bool setup_codec() {
  if (!is_hal_enabled()) {
    LOG(ERROR) << __func__ << "aidl: BluetoothAudio HAL is not enabled";
    return false;
  }
  CodecConfiguration codec_config{};
  if (!a2dp_get_selected_hal_codec_config(&codec_config)) {
    LOG(ERROR) << __func__ << "aidl: Failed to get CodecConfiguration";
    return false;
  }
  //codec_config_global = codec_config;
  memcpy(&codec_config_global, &codec_config, sizeof(CodecConfiguration));
  bool should_codec_offloading =
      bluetooth::audio::aidl::codec::IsCodecOffloadingEnabled(codec_config);
  if (should_codec_offloading && !is_hal_offloading()) {
    LOG(WARNING) << __func__ << "aidl: Switching BluetoothAudio HAL to Hardware";
    end_session();
    active_hal_interface = offloading_hal_interface;
  } else if (!should_codec_offloading && is_hal_offloading()) {
    LOG(WARNING) << __func__ << " aidl: HW encoding fails";
    //end_session();
    //active_hal_interface = software_hal_interface;
    // We landed here since codec capability didn't match. Lets return from here
    return false;
  }

  AudioConfiguration audio_config{};
  if (active_hal_interface->GetTransportInstance()->GetSessionType() ==
      SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH) {
    audio_config.set<AudioConfiguration::a2dpConfig>(codec_config);
  } /*else {
    PcmConfiguration pcm_config{};
    if (!a2dp_get_selected_hal_pcm_config(&pcm_config)) {
      LOG(ERROR) << __func__ << "aidl: Failed to get PcmConfiguration";
      return false;
    }
    pcm_config_global = pcm_config;
    audio_config.set<AudioConfiguration::pcmConfig>(pcm_config);
  }*/
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

uint16_t sbc_calulate_offload_bitrate(btav_a2dp_codec_config_t *codec, btif_a2dp_codec_config_callback_t* peer) {
  uint16_t s16SamplingFreq,sample_rate;
  int16_t s16BitPool = 0;
  int16_t s16BitRate;
  int16_t s16FrameLen;
  uint8_t protect = 0;
  int min_bitpool;
  int max_bitpool;
  uint8_t bits_per_sample;
  uint16_t s16ChannelMode, s16NumOfSubBands, s16NumOfBlocks;
  uint16_t s16AllocationMethod, s16NumOfChannels;
  uint16_t offload_bitrate;

  min_bitpool = peer->min_bitpool;
  max_bitpool = peer->max_bitpool;
  sample_rate = codec->sample_rate;
  bits_per_sample = codec->bits_per_sample;
  LOG_DEBUG(LOG_TAG, "%s: sample_rate=%u bits_per_sample=%u "
        " min_bitpool=%d max_bitpool=%d",__func__, sample_rate,
        bits_per_sample, min_bitpool, max_bitpool);

  // The codec parameters
  s16ChannelMode = GetChannelMode((a2dp_proto::ss_btav_a2dp_codec_channel_mode_t)codec->channel_mode);
  s16NumOfSubBands = peer->num_subbands;
  s16NumOfBlocks = GetNumberOfBlocks(peer->block_length);
  s16AllocationMethod = peer->alloc_method;
  s16SamplingFreq = GetSamplingFeq(codec->sample_rate);
  s16NumOfChannels = GetNumOfChannels((a2dp_proto::ss_btav_a2dp_codec_channel_mode_t)codec->channel_mode);

  if (s16SamplingFreq == SBC_sf16000)
    s16SamplingFreq = 16000;
  else if (s16SamplingFreq == SBC_sf32000)
    s16SamplingFreq = 32000;
  else if (s16SamplingFreq == SBC_sf44100)
    s16SamplingFreq = 44100;
  else
    s16SamplingFreq = 48000;

  offload_bitrate = peer->is_peer_edr ? A2DP_SBC_DEFAULT_OFFLOAD_BITRATE :
                    A2DP_SBC_NON_EDR_OFFLOAD_MAX_RATE;
  ALOGI("sCh_mode:%d, sSB:%d, sBlock:%d, sAM:%d, sSFreq:%d, sChannel:%d BR:%d",
  s16ChannelMode, s16NumOfSubBands, s16NumOfBlocks, s16AllocationMethod,
  s16SamplingFreq, s16NumOfChannels, offload_bitrate);

  do {
    if ((s16ChannelMode == SBC_JOINT_STEREO) ||
        (s16ChannelMode == SBC_STEREO)) {
      ALOGI("In JS");
      s16BitPool = (int16_t)((offload_bitrate *
                              s16NumOfSubBands * 1000 /
                              s16SamplingFreq) -
                             ((32 + (4 * s16NumOfSubBands *
                                     s16NumOfChannels) +
                               (s16ChannelMode - 2) *
                                s16NumOfSubBands)) /
                              s16NumOfBlocks);

      s16FrameLen = 4 +
                    (4 * s16NumOfSubBands *
                     s16NumOfChannels) /
                        8 +
                    (((s16ChannelMode - 2) *
                      s16NumOfSubBands) +
                     (s16NumOfBlocks * s16BitPool)) /
                        8;

      s16BitRate = (8 * s16FrameLen * s16SamplingFreq) /
                   (s16NumOfSubBands *
                    s16NumOfBlocks * 1000);

      if (s16BitRate > offload_bitrate) s16BitPool--;

      if (s16NumOfSubBands == 8)
        s16BitPool = (s16BitPool > 255) ? 255 : s16BitPool;
      else
        s16BitPool = (s16BitPool > 128) ? 128 : s16BitPool;
    } else {
      ALOGI("In MS");
      s16BitPool =
          (int16_t)(((s16NumOfSubBands *
                      offload_bitrate * 1000) /
                     (s16SamplingFreq * s16NumOfChannels)) -
                    (((32 / s16NumOfChannels) +
                      (4 * s16NumOfSubBands)) /
                     s16NumOfBlocks));

     //uint16_t m16BitPool =
     //    (s16BitPool > (16 * s16NumOfSubBands))
     //        ? (16 * s16NumOfSubBands)
     //        : s16BitPool;
    }

    if (s16BitPool < 0) s16BitPool = 0;

    LOG_DEBUG(LOG_TAG, "%s: bitpool candidate: %d (%d kbps)", __func__,
              s16BitPool, offload_bitrate);

    if (s16BitPool > max_bitpool) {
      LOG_DEBUG(LOG_TAG, "%s: computed bitpool too large (%d)", __func__,
                s16BitPool);
      /* Decrease bitrate */
      offload_bitrate -= A2DP_SBC_BITRATE_STEP;
      /* Record that we have decreased the bitrate */
      protect |= 1;
    } else if (s16BitPool < min_bitpool) {
      LOG_WARN(LOG_TAG, "%s: computed bitpool too small (%d)", __func__,
               s16BitPool);

      /* Increase bitrate */
      uint16_t previous_u16BitRate = offload_bitrate;
      offload_bitrate += A2DP_SBC_BITRATE_STEP;
      /* Record that we have increased the bitrate */
      protect |= 2;
      /* Check over-flow */
      if (offload_bitrate < previous_u16BitRate) protect |= 3;
    } else {
      break;
    }
    /* In case we have already increased and decreased the bitrate, just stop */
    if (protect == 3) {
      LOG_ERROR(LOG_TAG, "%s: could not find bitpool in range", __func__);
      break;
    }
  } while (true);
  LOG_ERROR(LOG_TAG," offload_bitrate for sbc = %d",offload_bitrate);
  return offload_bitrate;
}

uint16_t GetChannelMode(a2dp_proto::ss_btav_a2dp_codec_channel_mode_t ch_mode){
  switch (ch_mode) {
    case a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_MONO:
      return SBC_MONO;
    case a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO:
      return SBC_STEREO;
    case a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_JOINT_STEREO:
      return SBC_JOINT_STEREO;
    case a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_DUAL:
      return SBC_DUAL;
    default:
      break;
  }
  return -1;
}

int GetSamplingFeq(btav_a2dp_codec_sample_rate_t samp_freq){
  switch (samp_freq) {
    case BTAV_A2DP_CODEC_SAMPLE_RATE_16000:
      return SBC_sf16000;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_32000:
      return SBC_sf32000;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_44100:
      return SBC_sf44100;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_48000:
      return SBC_sf48000;
    default:
      break;
  }
  return -1;
}

uint16_t GetNumOfChannels(a2dp_proto::ss_btav_a2dp_codec_channel_mode_t ch_mode){
  switch (ch_mode) {
    case a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_MONO:
      return 1;
    case a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO:
    case a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_JOINT_STEREO:
    case a2dp_proto::SS_BTAV_A2DP_CODEC_CHANNEL_MODE_DUAL:
      return 2;
    default:
      break;
  }
  return -1;
}

uint8_t GetNumberOfBlocks(uint16_t block_len) {

  switch (block_len) {
    case A2DP_SBC_IE_BLOCKS_4:
      return 4;
    case A2DP_SBC_IE_BLOCKS_8:
      return 8;
    case A2DP_SBC_IE_BLOCKS_12:
      return 12;
    case A2DP_SBC_IE_BLOCKS_16:
      return 16;
    default:
      ALOGE("%s: unknown bloack length", __func__);
      break;
  }

return -1;
}

}  // namespace a2dp
}  // namespace aidl
}  // namespace audio
}  // namespace bluetooth
