/******************************************************************************
 *
 *  Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 ******************************************************************************/

#include <hardware/bt_hf_vendor.h>
#include "bt_types.h"

typedef struct {
  void (*p_cback)(uint16_t event, uint16_t sco_handle);
  uint8_t pkt_size;
  uint16_t cb_event;
}tBTUI_SCO_CODEC_CFG;

typedef struct {
  bool sco_hci;
}tBTUI_CB;

typedef struct {
  bool sco_use_mic;
  tBTUI_SCO_CODEC_CFG codec_cfg;
}tBTUI_CFG;

extern tBTUI_CB btui_cb;
extern tBTUI_CFG btui_cfg;

void btui_sco_codec_open(tBTUI_SCO_CODEC_CFG *cfg);
void btui_sco_codec_start(uint16_t handle);
void btui_sco_codec_close(void);
void btui_register_sco_data_path(sco_data_rx receiver, sco_data_tx transmitter);
void btui_sco_codec_inqdata(BT_HDR* p_buf);
void btui_sco_codec_readbuf(BT_HDR** p_buf);



