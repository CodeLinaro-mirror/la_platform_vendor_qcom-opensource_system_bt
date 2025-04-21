/******************************************************************************
 *
 *  Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 ******************************************************************************/

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include "bta_api.h"
#include "bta_ag_ui.h"
#include "hci_internals.h"

tBTUI_CB btui_cb = {
                     .sco_hci = true,
                   };
tBTUI_CFG btui_cfg = {
                       .sco_use_mic = true,
                     };

static alarm_t* sco_alarm = 0;

static sco_data_rx sco_receiver;
static sco_data_tx sco_transmitter;
static uint32_t max_sco_buffer_size = 256;

static char* sco_test_file = "/data/misc/bluetooth/sco_test.pcm";
static int sco_test_fd = -1;
static int sco_test_data_size = 0;
static void *sco_test_data;

/*******************************************************************************
 *
 * Function         btui_sco_codec_open
 *
 * Description      open the codec

 * Parameters       cfg  -  sco codec parameter
 *
 * Returns          void
 *
 ******************************************************************************/
void btui_sco_codec_open(tBTUI_SCO_CODEC_CFG *cfg)
{
  BTIF_TRACE_DEBUG("%s: enter", __func__);
  btui_cfg.codec_cfg = *cfg;
  sco_alarm = alarm_new_periodic("sco alarm");
  if (sco_alarm == NULL) {
    BTIF_TRACE_DEBUG("%s: sco_alarm is null", __func__);
  }
  sco_test_fd = open(sco_test_file, O_RDONLY);
  if (sco_test_fd >= 0) {
    struct stat s;
    fstat(sco_test_fd, &s);
    if (s.st_size > 0) {
      sco_test_data_size = s.st_size;
      sco_test_data = mmap(0, s.st_size, PROT_READ, MAP_PRIVATE, sco_test_fd, 0);
    } else {
      BTIF_TRACE_DEBUG("%s: file size %d", __func__, s.st_size);
    }
  } else {
    BTIF_TRACE_DEBUG("%s: open sco test file %s fail", __func__, sco_test_file);
  }
  return 0;
}

void sco_alarm_cb(void* context)
{
  uint16_t handle = (uint16_t) context;
  if (btui_cfg.codec_cfg.p_cback != NULL) {
    (*btui_cfg.codec_cfg.p_cback)(btui_cfg.codec_cfg.cb_event, handle);
  }
}

/*******************************************************************************
 *
 * Function         btui_sco_codec_start
 *
 * Description      start the codec

 * Parameters       handle - sco handle
 *
 * Returns          void
 *
 ******************************************************************************/
void btui_sco_codec_start(uint16_t handle)
{
  BTIF_TRACE_DEBUG("%s: enter", __func__);
  alarm_set(sco_alarm, 20, sco_alarm_cb, (void *) handle); // 20ms
  return 0;
}


/*******************************************************************************
 *
 * Function         btui_sco_codec_close
 *
 * Description      close the codec

 * Parameters
 *
 * Returns          void
 *
 ******************************************************************************/
void btui_sco_codec_close(void)
{
  BTIF_TRACE_DEBUG("%s: enter", __func__);
  btui_cfg.codec_cfg.p_cback = NULL;

  if (sco_alarm) {
    alarm_free(sco_alarm);
    sco_alarm = 0;
  }

  if(sco_test_data) {
    BTIF_TRACE_DEBUG("%s: munmap", __func__);
    munmap(sco_test_data, sco_test_data_size);
    sco_test_data = 0;
  }
  if (sco_test_fd >= 0) {
    BTIF_TRACE_DEBUG("%s: close sco test file", __func__);
    close(sco_test_fd);
    sco_test_fd = -1;
  }

  return 0;
}


void btui_register_sco_data_path(sco_data_rx receiver, sco_data_tx transmitter)
{
  BTIF_TRACE_DEBUG("%s: enter", __func__);
  sco_receiver = receiver;
  sco_transmitter = transmitter;
}

/*******************************************************************************
 *
 * Function         btui_sco_codec_inqdata
 *
 * Description      The function is called to read incoming sco data from HCI

 * Parameters       p_buf - sco audio data
 *
 * Returns          void
 *
 ******************************************************************************/
void btui_sco_codec_inqdata(BT_HDR* p_buf)
{
  uint8_t* p = (uint8_t*)(p_buf + 1) + p_buf->offset;
  uint16_t handle;
  uint8_t pkt_size = 0;

  STREAM_TO_UINT16(handle, p);
  STREAM_TO_UINT8(pkt_size, p);

  // sco data to app or audio device: p, pkt_size
  BTIF_TRACE_DEBUG("%s: incoming sco data, size = %d", __func__, pkt_size);
  if (sco_receiver != NULL) {
    BTIF_TRACE_DEBUG("send sco data to app");
    (*sco_receiver)(p, pkt_size);
  }
  return 0;
}

/*******************************************************************************
 *
 * Function         btui_sco_codec_readbuf
 *
 * Description      This function is called to reading sco audio data and send to HCI

 * Parameters       p_buf - sco audio data
 *
 * Returns          void
 *
 ******************************************************************************/
#define SCO_ENCODING_SIZE 120
#define SCO_ENCODING_FRAMES 3

// Currently just emulate audio data for test
void btui_sco_codec_readbuf(BT_HDR** p_buf)
{
  uint8_t *p;
  BT_HDR *buff;
  static int off = 0, count = 0;
  uint32_t sco_buff_size = SCO_ENCODING_SIZE;
  uint8_t* sco_buff = 0;

  if (p_buf == 0) {
    BTIF_TRACE_DEBUG("%s: p_buff is null", __func__);
    return;
  }
  if (sco_test_data == 0) {
    BTIF_TRACE_DEBUG("%s: sco test data is empty", __func__);
    *p_buf = 0;
    return;
  }
  if (off >= sco_test_data_size/SCO_ENCODING_SIZE) {
    off = 0;
  }

  if (count == SCO_ENCODING_FRAMES) {
    *p_buf = 0;
    count = 0;
    return;
  }

  if (sco_transmitter != NULL) {
    sco_buff_size = max_sco_buffer_size;
    sco_buff = osi_malloc(sco_buff_size);
    if (sco_buff == NULL) {
      BTIF_TRACE_DEBUG("sco_buff is null");
      return;
    }
    if (!(*sco_transmitter)(sco_buff, &sco_buff_size)) {
      *p_buf = 0;
      osi_free(sco_buff);
      return;
    }
  }

  buff = (BT_HDR*)osi_malloc(sizeof(BT_HDR) + sco_buff_size + HCI_SCO_PREAMBLE_SIZE);
  if (buff != 0) {
    buff->len = sco_buff_size;
    buff->offset = HCI_SCO_PREAMBLE_SIZE;
    buff->layer_specific =
    buff->event = 0;
    p = (uint8_t*)(buff + 1) + HCI_SCO_PREAMBLE_SIZE;
    if (sco_buff != NULL) {
      memcpy(p, sco_buff, sco_buff_size);
    } else {
      memcpy(p, (void*)((char *)sco_test_data + off * SCO_ENCODING_SIZE), SCO_ENCODING_SIZE);
    }
    off++;
    count++;
  } else {
    BTIF_TRACE_DEBUG("%s: buff is null", __func__);
  }
  *p_buf = buff;
  if (sco_buff != NULL) {
    osi_free(sco_buff);
  }
}

