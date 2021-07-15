/******************************************************************************
 *
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

/**
 * BTIF AV API functions accessed internally.
 */

#ifndef BTIF_AV_H
#define BTIF_AV_H

#include "bta_av_api.h"
#include "btif_common.h"

/**
 * When the local device is A2DP source, get the address of the active peer.
 */
std::set<RawAddress> btif_av_source_active_peers(void);

/**
 * When the local device is A2DP sink, get the address of the active peer.
 */
RawAddress btif_av_sink_active_peer(void);

/**
 * Check whether A2DP Sink is enabled.
 */
bool btif_av_is_sink_enabled(void);

/**
 * Start streaming.
 *
 * @param peer_address the peer address
 */
void btif_av_stream_start(const RawAddress& peer_address);

/**
 * Stop streaming.
 *
 * @param peer_address the peer address or RawAddress::kEmpty to stop all peers
 */
void btif_av_stream_stop(const RawAddress& peer_address);

/**
 * Suspend streaming.
 *
 * @param peer_address the peer address
 */
void btif_av_stream_suspend(const RawAddress& peer_address);

/**
 * Start offload streaming.
 *
 * @param peer_address the peer address
 */
void btif_av_stream_start_offload(const RawAddress& peer_address);

/**
 * Check whether ready to start the A2DP stream.
 *
 * @param peer_address the peer address
 */
bool btif_av_stream_ready(const RawAddress& peer_address);

/**
 * Check whether the A2DP stream is in started state and ready
 * for media start.
 *
 * @param peer_address the peer address
 */
bool btif_av_stream_started_ready(const RawAddress& peer_address);

/**
 * Check whether there is a connected peer (either Source or Sink)
 *
 * @param peer_address the peer address
 */
bool btif_av_is_connected(const RawAddress& peer_address);

/**
 * Get the Stream Endpoint Type of the Active peer.
 *
 * @param peer_address the peer address
 *
 * @return the stream endpoint type: either AVDT_TSEP_SRC or AVDT_TSEP_SNK
 */
uint8_t btif_av_get_peer_sep(const RawAddress& peer_address);

/**
 * Get the Stream Endpoint Type of the Active peer for active.
 *
 * @return the stream endpoint type: either AVDT_TSEP_SRC or AVDT_TSEP_SNK
 */
uint8_t btif_av_sink_get_peer_sep();


/**
 * Clear the remote suspended flag for the active peer.
 *
 * @param peer_address the peer address
 */
void btif_av_clear_remote_suspend_flag(const RawAddress& peer_address);

/**
 * Check whether the connected A2DP peer supports EDR.
 *
 * The value can be provided only if the remote peer is connected.
 * Otherwise, the answer will be always false.
 *
 * @param peer_address the peer address
 * @return true if the remote peer is capable of EDR
 */
bool btif_av_is_peer_edr(const RawAddress& peer_address);

/**
 * Check whether the connected A2DP peer supports 3 Mbps EDR.
 *
 * The value can be provided only if the remote peer is connected.
 * Otherwise, the answer will be always false.
 *
 * @param peer_address the peer address
 * @return true if the remote peer is capable of EDR and supports 3 Mbps
 */
bool btif_av_peer_supports_3mbps(const RawAddress& peer_address);

/**
 * Report A2DP Source Codec State for a peer.
 *
 * @param peer_address the address of the peer to report
 * @param codec_config the codec config to report
 * @param codecs_local_capabilities the codecs local capabilities to report
 * @param codecs_selectable_capabilities the codecs selectable capabilities
 * to report
 */
void btif_av_report_source_codec_state(
    const RawAddress& peer_address,
    const btav_a2dp_codec_config_t& codec_config,
    const std::vector<btav_a2dp_codec_config_t>& codecs_local_capabilities,
    const std::vector<btav_a2dp_codec_config_t>&
        codecs_selectable_capabilities);

/**
 * Initialize / shut down the A2DP Source service.
 *
 * @param enable true to enable the A2DP Source service, false to disable it
 * @return BT_STATUS_SUCCESS on success, BT_STATUS_FAIL otherwise
 */
bt_status_t btif_av_source_execute_service(bool enable);

/**
 * Initialize / shut down the A2DP Sink service.
 *
 * @param enable true to enable the A2DP Sink service, false to disable it
 * @return BT_STATUS_SUCCESS on success, BT_STATUS_FAIL otherwise
 */
bt_status_t btif_av_sink_execute_service(bool enable);

/**
 * Peer ACL disconnected.
 *
 * @param peer_address the disconnected peer address
 */
void btif_av_acl_disconnected(const RawAddress& peer_address);

/**
 * Dump debug-related information for the BTIF AV module.
 *
 * @param fd the file descriptor to use for writing the ASCII formatted
 * information
 */
void btif_debug_av_dump(int fd);

/**
 * Set the audio delay for the stream.
 *
 * @param peer_address the peer address
 * @param delay the delay to set in units of 1/10ms
 */
void btif_av_set_audio_delay(const RawAddress& peer_address, uint16_t delay);

/**
 * Reset the audio delay and count of audio bytes sent to zero.
 *
 * @param peer_address the peer address
 */
void btif_av_reset_audio_delay(const RawAddress& peer_address);

/**
 * Called to disconnect peer device when
 *  remote initiatied offload start failed
 *
 * @param peer_address to disconnect
 *
 */
void btif_av_src_disconnect_sink(const RawAddress& peer_address);

/**
 *  check A2DP offload support enabled
 *  @param  none
 */
bool btif_av_is_a2dp_offload_enabled(void);

/**
 * Check whether peer device is silenced
 *
 * @param peer_address to check
 *
 */
bool btif_av_is_peer_silenced(const RawAddress& peer_address);

/**
 * Set the specified device to be active
 *
 * @param peer_address the peer address to be active
 */
bool btif_av_sink_set_active_device(const RawAddress& peer_address);

#if (A2DP_SINK_DELAY_REPORT == TRUE)
/*******************************************************************************
**
** Function         btif_get_average_delay
**
** Description      Returns average of instantaneous delay values
**
** Returns          int64_t
*******************************************************************************/
int64_t btif_get_average_delay();
#endif

/**
 * Check whether the peer is active
 *
 * @param peer_address the peer address
 */
bool btif_av_source_is_active_peer(const RawAddress& peer_address);

#endif /* BTIF_AV_H */
