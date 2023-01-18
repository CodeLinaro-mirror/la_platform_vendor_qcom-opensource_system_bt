/******************************************************************************
 *
 *  Copyright 2007-2012 Broadcom Corporation
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
#ifndef UIPC_H
#define UIPC_H

#include <mutex>

#define UIPC_CTRL_CH true
#define UIPC_DATA_CH false

#define UIPC_CH_ID_AV_CTRL_0 0
#define UIPC_CH_ID_AV_CTRL_1 1

#define UIPC_CH_ID_AV_AUDIO_0 2
#define UIPC_CH_ID_AV_AUDIO_1 3

#define UIPC_CH_ID_AV_CTRL UIPC_CH_ID_AV_CTRL_0
#define UIPC_CH_ID_AV_AUDIO UIPC_CH_ID_AV_AUDIO_0

#define UIPC_CH_NUM 4

#define UIPC_CH_ID_ALL UIPC_CH_NUM /* used to address all the ch id at once */

#define UIPC_CH_INVALID_ID (255)

#define DEFAULT_READ_POLL_TMO_MS 100

typedef uint8_t tUIPC_CH_ID;

/* Events generated */
typedef enum {
  UIPC_OPEN_EVT = 0x0001,
  UIPC_CLOSE_EVT = 0x0002,
  UIPC_RX_DATA_EVT = 0x0004,
  UIPC_RX_DATA_READY_EVT = 0x0008,
  UIPC_TX_DATA_READY_EVT = 0x0010
} tUIPC_EVENT;

/*
 * UIPC IOCTL Requests
 */

#define UIPC_REQ_RX_FLUSH 1
#define UIPC_REG_CBACK 2
#define UIPC_REG_REMOVE_ACTIVE_READSET 3
#define UIPC_SET_READ_POLL_TMO 4

typedef void(tUIPC_RCV_CBACK)(
    tUIPC_CH_ID ch_id,
    tUIPC_EVENT event); /* points to BT_HDR which describes event type and
                           length of data; len contains the number of bytes of
                           entire message (sizeof(BT_HDR) + offset + size of
                           data) */

const char* dump_uipc_event(tUIPC_EVENT event);

typedef struct {
  int srvfd;
  int fd;
  int read_poll_tmo_ms;
  int task_evt_flags; /* event flags pending to be processed in read task */
  tUIPC_RCV_CBACK* cback;
  RawAddress peer_address;
} tUIPC_CHAN;

struct tUIPC_STATE {
  pthread_t tid; /* main thread id */
  int running;
  std::recursive_mutex mutex;

  fd_set active_set;
  fd_set read_set;
  int max_fd;
  int signal_fds[2];

  tUIPC_CHAN ch[UIPC_CH_NUM];
};

/**
 * Initialize UIPC module
 *
 * @param user User ID who uses UIPC
 */
std::unique_ptr<tUIPC_STATE> UIPC_Init();

/**
 * Open a UIPC channel
 *
 * @param ch_id Channel ID
 * @param p_cback Callback handler
 * @param socket_path Path to the socket
 * @return true on success, otherwise false
 */
bool UIPC_Open(const RawAddress& peer_address, tUIPC_STATE& uipc, tUIPC_CH_ID ch_id, tUIPC_RCV_CBACK* p_cback,
               const char* socket_path);

/**
 * Closes a channel in UIPC or the entire UIPC module
 *
 * @param ch_id Channel ID; if ch_id is UIPC_CH_ID_ALL, then cleanup UIPC
 */
void UIPC_Close(tUIPC_STATE& uipc, tUIPC_CH_ID ch_id);

/**
 * Send a message over UIPC
 *
 * @param ch_id Channel ID
 * @param msg_evt Message event type
 * @param p_buf Buffer for the message
 * @param msglen Message length
 * @return true on success, otherwise false
 */
bool UIPC_Send(tUIPC_STATE& uipc, tUIPC_CH_ID ch_id, uint16_t msg_evt,
               const uint8_t* p_buf, uint16_t msglen);

/**
 * Read a message from UIPC
 *
 * @param ch_id Channel ID
 * @param p_msg_evt Message event type
 * @param p_buf Buffer for the message
 * @param len Bytes to read
 * @return true on success, otherwise false
 */
uint32_t UIPC_Read(tUIPC_STATE& uipc, tUIPC_CH_ID ch_id, uint16_t* p_msg_evt,
                   uint8_t* p_buf, uint32_t len);

/**
 * Control the UIPC parameter
 *
 * @param ch_id Channel ID
 * @param request Request type
 * @param param Optional parameters
 * @return true on success, otherwise false
 */
bool UIPC_Ioctl(tUIPC_STATE& uipc, tUIPC_CH_ID ch_id, uint32_t request,
                void* param);
/**
 * Get a unused control channel
 *
 * @return UIPC_CH_ID_AV_CTRL_0 or UIPC_CH_ID_AV_CTRL_1 on success,
 *  otherwise UIPC_CH_INVALID_ID
 */
int uipc_get_free_ctrl_ch();

/**
 * Get peer address by control or data channel
 *
 * @param ch_id Channel ID
 * @return peer address on success, otherwise RawAddress::kEmpty
 */
const RawAddress& uipc_get_address_from_ch(int ch_id);

/**
 * Get control id by peer address
 *
 * @param peer_address Peer address
 * @param ctrl For control channel on true; For data channel on false
 * @return control or data channel on success, otherwise UIPC_CH_INVALID_ID
 */
int uipc_get_ch_from_address(const RawAddress& peer_address, bool ctrl);

#endif /* UIPC_H */
