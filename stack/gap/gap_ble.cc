/******************************************************************************
 *
 *  Copyright (C) 2017 The Android Open Source Project
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
 *  Changes from Qualcomm Innovation Center are provided under the following license:
 *
 *  Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted (subject to the limitations in the
 *  disclaimer below) provided that the following conditions are met:
 *
 *  Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *
 *  Redistributions in binary form must reproduce the above
 *  copyright notice, this list of conditions and the following
 *  disclaimer in the documentation and/or other materials provided
 *  with the distribution.
 *
 *  Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *  contributors may be used to endorse or promote products derived
 *  from this software without specific prior written permission.
 *
 *  NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 *  GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 *  HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 *  ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 *  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 *  OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 *  IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
 *
 ******************************************************************************/

#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <string.h>
#include <array>
#include <list>
#include <queue>
#include "gap_api.h"
#include "gatt_api.h"
#include "btif_storage.h"
#include "bta_gatt_api.h"
#include "btif_storage.h"
#include "btif_config.h"
#include "gatt_int.h"
#include "btif_config.h"
#include "stack_config.h"

#define GAP_ENC_KEY_CONNECTING 1      /* wait for connection */
#define GAP_ENC_KEY_CHARACTERISTIC 2  /* Read for Enc Key Material Characteristic */
#define GAP_ENC_KEY_CCCD 3            /* Discover CCCD */
#define GAP_ENC_KEY_CONFIG_CCCD 4     /* Write CCCD */

using base::StringPrintf;
using bluetooth::Uuid;

typedef struct {
  uint16_t uuid;
  uint8_t op;
  uint16_t handle;
  uint8_t disc_type;
  Uuid char_uuid;
  uint16_t s_handle;
  tGAP_BLE_CMPL_CBACK* p_cback;
} tGAP_REQUEST;

typedef struct {
  RawAddress bda;
  tGAP_BLE_CMPL_CBACK* p_cback;
  uint16_t conn_id;
  uint16_t cl_op_uuid;
  bool connected;
  std::queue<tGAP_REQUEST> requests;
  uint8_t enc_key_stage;
  uint16_t curr_enc_key_char_handle;
  uint16_t curr_cccd_handle;
  uint8_t enc_key_result;
  Uuid char_uuid;
  std::vector<uint16_t> enc_key_char_handles;
  bool is_enc_key_info_in_progress;
} tGAP_CLCB;

typedef struct {
  uint16_t handle;
  uint16_t uuid;
  tGAP_BLE_ATTR_VALUE attr_value;
} tGAP_ATTR;

void server_attr_request_cback(uint16_t, uint32_t, tGATTS_REQ_TYPE,
                               tGATTS_DATA*);
void client_connect_cback(tGATT_IF, const RawAddress&, uint16_t, bool,
                          tGATT_DISCONN_REASON, tGATT_TRANSPORT);
void client_cmpl_cback(uint16_t, tGATTC_OPTYPE, tGATT_STATUS,
                       tGATT_CL_COMPLETE*, uint32_t trans_id);
void client_disc_res_cback(uint16_t, tGATT_DISC_TYPE, tGATT_DISC_RES*);
void client_disc_cmpl_cback(uint16_t, tGATT_DISC_TYPE, tGATT_STATUS);

static void gap_cl_get_enc_key_info(tGAP_CLCB*);

tGATT_CBACK gap_cback = {client_connect_cback,
                         client_cmpl_cback,
                         client_disc_res_cback,
                         client_disc_cmpl_cback,
                         server_attr_request_cback,
                         NULL,
                         NULL,
                         NULL,
                         NULL,
                         NULL};

constexpr int GAP_CHAR_DEV_NAME_SIZE = BD_NAME_LEN;
constexpr int GAP_MAX_CHAR_NUM = 6;
/* Length of the Encrypted Data Key Material Key(16) + IV(8) */
constexpr int ENCR_KEY_MATERIAL_LEN = 24;

std::vector<tGAP_CLCB> gap_clcbs;
std::vector<std::pair<RawAddress, uint16_t>> bda_and_conn_id;
/* LE GAP attribute database */
std::array<tGAP_ATTR, GAP_MAX_CHAR_NUM> gatt_attr;
tGATT_IF gatt_if;

/** returns LCB with macthing bd address, or nullptr */
tGAP_CLCB* find_clcb_by_bd_addr(const RawAddress& bda) {
  for (auto& cb : gap_clcbs)
    if (cb.bda == bda) return &cb;

  return nullptr;
}

/** returns LCB with macthing connection ID, or nullptr if not found  */
tGAP_CLCB* ble_find_clcb_by_conn_id(uint16_t conn_id) {
  for (auto& cb : gap_clcbs)
    if (cb.connected && cb.conn_id == conn_id) return &cb;

  return nullptr;
}

/** allocates a GAP connection link control block */
tGAP_CLCB* clcb_alloc(const RawAddress& bda) {
  gap_clcbs.emplace_back();
  tGAP_CLCB& cb = gap_clcbs.back();
  cb.bda = bda;
  return &cb;
}

/** The function clean up the pending request queue in GAP */
void clcb_dealloc(tGAP_CLCB& clcb) {
  // put last element into place of current element, and remove last one - just
  // fast remove.
  for (auto it = gap_clcbs.begin(); it != gap_clcbs.end(); it++) {
    if (it->conn_id == clcb.conn_id) {
      auto last_one = std::prev(gap_clcbs.end());
      *it = *last_one;
      gap_clcbs.erase(last_one);
      return;
    }
  }
}

/** GAP Attributes Database Request callback */
tGATT_STATUS read_attr_value(uint16_t handle, tGATT_VALUE* p_value,
                             bool is_long) {
  uint8_t* p = p_value->value;
  uint16_t offset = p_value->offset;
  uint8_t* p_dev_name = NULL;
  uint8_t* p_encr_material = p;
  uint8_t* p_temp = p_encr_material;
  for (const tGAP_ATTR& db_attr : gatt_attr) {
    const tGAP_BLE_ATTR_VALUE& attr_value = db_attr.attr_value;
    if (handle == db_attr.handle) {

      if (db_attr.uuid != GATT_UUID_GAP_DEVICE_NAME &&
         db_attr.uuid != GATT_UUID_GAP_ENC_KEY_MATERIAL && is_long){
        return GATT_NOT_LONG;
      }

      switch (db_attr.uuid) {
        case GATT_UUID_GAP_DEVICE_NAME:
          BTM_ReadLocalDeviceName((char**)&p_dev_name);
          if (strlen((char*)p_dev_name) > GATT_MAX_ATTR_LEN)
            p_value->len = GATT_MAX_ATTR_LEN;
          else
            p_value->len = (uint16_t)strlen((char*)p_dev_name);

          if (offset > p_value->len)
            return GATT_INVALID_OFFSET;
          else {
            p_value->len -= offset;
            p_dev_name += offset;
            ARRAY_TO_STREAM(p, p_dev_name, p_value->len);
            DVLOG(1) << "GATT_UUID_GAP_DEVICE_NAME len=" << +p_value->len;
          }
          break;

        case GATT_UUID_GAP_ICON:
          UINT16_TO_STREAM(p, attr_value.icon);
          p_value->len = 2;
          break;

        case GATT_UUID_GAP_PREF_CONN_PARAM:
          UINT16_TO_STREAM(p, attr_value.conn_param.int_min); /* int_min */
          UINT16_TO_STREAM(p, attr_value.conn_param.int_max); /* int_max */
          UINT16_TO_STREAM(p, attr_value.conn_param.latency); /* latency */
          UINT16_TO_STREAM(p, attr_value.conn_param.sp_tout); /* sp_tout */
          p_value->len = 8;
          break;

        /* address resolution */
        case GATT_UUID_GAP_CENTRAL_ADDR_RESOL:
          UINT8_TO_STREAM(p, attr_value.addr_resolution);
          p_value->len = 1;
          break;
        /* Encrypted Data Key Material*/
        case GATT_UUID_GAP_ENC_KEY_MATERIAL:
          if (stack_config_get_interface()->get_pts_enable_authorization_encr_data_key()) {
            return GATT_INSUF_AUTHORIZATION;
          }
          /* Sending the Encrypted Data Key Material in Little Endian */
          REVERSE_ARRAY_TO_STREAM(p_encr_material, attr_value.encr_material.session_key,ENC_KEY_LEN);
          REVERSE_ARRAY_TO_STREAM(p_encr_material, attr_value.encr_material.init_vector,ENC_IV_LEN);
          p_value->len = ENCR_KEY_MATERIAL_LEN;
          for(unsigned int i = 0; i < ENCR_KEY_MATERIAL_LEN; i++) {
            uint8_t temp = (*(p_temp+i));
          }
          if (offset > p_value->len){
            LOG(ERROR) << __func__ << " GATT_INVALID_OFFSET";
            return GATT_INVALID_OFFSET;
          } else {
            p_value->len -= offset;
            p_temp += offset;
            ARRAY_TO_STREAM(p, p_temp, p_value->len);
          }
          break;

      }
      return GATT_SUCCESS;
    }
  }
  return GATT_NOT_FOUND;
}

/** GAP Attributes Database Read/Read Blob Request process */
tGATT_STATUS proc_read(tGATTS_REQ_TYPE, tGATT_READ_REQ* p_data,
                       tGATTS_RSP* p_rsp) {
  if (p_data->is_long) p_rsp->attr_value.offset = p_data->offset;

  p_rsp->attr_value.handle = p_data->handle;

  return read_attr_value(p_data->handle, &p_rsp->attr_value, p_data->is_long);
}

/** GAP ATT server process a write request */
uint8_t proc_write_req(tGATTS_REQ_TYPE, tGATT_WRITE_REQ* p_data, uint16_t conn_id) {
  uint8_t tcb_idx = GATT_GET_TCB_IDX(conn_id);
  tGATT_TCB &tcb = gatt_cb.tcb[tcb_idx];
  for (const auto& db_addr : gatt_attr){
    if (p_data->handle == db_addr.handle){
      if (db_addr.uuid == GATT_UUID_CHAR_CLIENT_CONFIG){
        uint8_t value = 0;
        uint8_t *p = p_data->value;
        STREAM_TO_UINT8(value, p);
        btif_storage_set_encr_data_cccd(tcb.peer_bda, value);
        btif_config_save();
      }
      return GATT_SUCCESS;
    }
  }
  return GATT_NOT_FOUND;
}

/** GAP ATT server attribute access request callback */
void server_attr_request_cback(uint16_t conn_id, uint32_t trans_id,
                               tGATTS_REQ_TYPE type, tGATTS_DATA* p_data) {
  uint8_t status = GATT_INVALID_PDU;
  bool ignore = false;

  DVLOG(1) << StringPrintf("%s: recv type (0x%02x)", __func__, type);

  tGATTS_RSP rsp_msg;
  memset(&rsp_msg, 0, sizeof(tGATTS_RSP));

  switch (type) {
    case GATTS_REQ_TYPE_READ_CHARACTERISTIC:
    case GATTS_REQ_TYPE_READ_DESCRIPTOR:
      status = proc_read(type, &p_data->read_req, &rsp_msg);
      break;

    case GATTS_REQ_TYPE_WRITE_CHARACTERISTIC:
    case GATTS_REQ_TYPE_WRITE_DESCRIPTOR:
      if (!p_data->write_req.need_rsp) ignore = true;
      status = proc_write_req(type, &p_data->write_req, conn_id);
      break;

    case GATTS_REQ_TYPE_WRITE_EXEC:
      ignore = true;
      DVLOG(1) << "Ignore GATTS_REQ_TYPE_WRITE_EXEC";
      break;

    case GATTS_REQ_TYPE_MTU:
      DVLOG(1) << "Get MTU exchange new mtu size: " << +p_data->mtu;
      ignore = true;
      break;

    default:
      DVLOG(1) << StringPrintf("Unknown/unexpected LE GAP ATT request: 0x%02x",
                               type);
      break;
  }

  if (!ignore) GATTS_SendRsp(conn_id, trans_id, status, &rsp_msg);
}

/**
 * utility function to send a Discover req for CCCD descriptor.
 * Returns true if discovery started, else false if GAP is busy.
 */
bool send_cl_disc_request(tGAP_CLCB& clcb) {
  if (!clcb.requests.size()) {
    return false;
  }

  tGAP_REQUEST& req = clcb.requests.front();
  uint8_t disc_type = req.disc_type;
  Uuid char_uuid = req.char_uuid;
  clcb.requests.pop();

  uint16_t s_handle = (clcb.curr_enc_key_char_handle + 1);
  uint16_t e_handle = (clcb.curr_enc_key_char_handle + 2);

  GATTC_Discover(clcb.conn_id, req.disc_type, s_handle,
                 e_handle, char_uuid);
  return true;
}

/**
 * utility function to send a read request for a GAP charactersitic.
 * Returns true if read started, else false if GAP is busy.
 */
bool send_cl_read_request(tGAP_CLCB& clcb) {
  VLOG(1) << __func__;
  if (!clcb.requests.size()) {
    return false;
  }

  tGAP_REQUEST& req = clcb.requests.front();
  clcb.p_cback = req.p_cback;
  uint16_t uuid = req.uuid;
  clcb.requests.pop();

  tGATT_READ_PARAM param;
  memset(&param, 0, sizeof(tGATT_READ_PARAM));

  param.service.uuid = Uuid::From16Bit(uuid);
  param.service.s_handle = 1;
  param.service.e_handle = 0xFFFF;
  param.service.auth_req = 0;

  VLOG(1) << __func__ << ": req handle: " << +req.handle;
  if (uuid == GATT_UUID_GAP_ENC_KEY_MATERIAL) {
    param.service.s_handle = req.handle;
    param.service.e_handle = 0xFFFF;
  }

  if (GATTC_Read(clcb.conn_id, GATT_READ_BY_TYPE, &param) == GATT_SUCCESS) {
    clcb.cl_op_uuid = uuid;
  }

  return true;
}

/**
 * utility function to send a write request for a CCCD descriptor.
 * Returns true if read started, else false if GAP is busy.
 */
bool send_cl_write_request(tGAP_CLCB& clcb) {
  if (!clcb.requests.size()) {
    return false;
  }

  tGAP_REQUEST& req = clcb.requests.front();
  clcb.p_cback = req.p_cback;
  uint16_t handle = req.handle;
  clcb.requests.pop();

  tGATT_VALUE ccc_value;
  memset(&ccc_value, 0, sizeof(tGATT_VALUE));
  ccc_value.handle = handle;
  ccc_value.len = 2;
  ccc_value.value[0] = GATT_CLT_CONFIG_INDICATION;
  GATTC_Write(clcb.conn_id, GATT_WRITE, &ccc_value);

  return true;
}

bool send_cl_request(tGAP_CLCB& clcb) {
  if (!clcb.requests.size()) {
    return false;
  }

  tGAP_REQUEST& req = clcb.requests.front();
  uint8_t op = req.op;
  if (op == GATTC_OPTYPE_READ) {
    send_cl_read_request(clcb);
  } else if (op == GATTC_OPTYPE_WRITE) {
    send_cl_write_request(clcb);
  } else if (op == GATTC_OPTYPE_DISCOVERY) {
    send_cl_disc_request(clcb);
  }

  return true;
}

/** GAP client operation complete callback */
void cl_op_cmpl(tGAP_CLCB& clcb, bool status, uint16_t len, uint8_t* p_name) {
  tGAP_BLE_CMPL_CBACK* p_cback = clcb.p_cback;
  uint16_t op = clcb.cl_op_uuid;
  RawAddress bda = clcb.bda;

  VLOG(1) << StringPrintf("%s: status: %d", __func__, status);

  clcb.cl_op_uuid = 0;
  clcb.p_cback = NULL;

  if (p_cback) {
    DVLOG(1) << __func__ << ": calling";
    (*p_cback)(status, clcb.bda, len, (char*)p_name);
  }

  /* if no further activity is requested in callback, drop the link */
  if (clcb.connected) {
    if (!send_cl_request(clcb) && (clcb.enc_key_stage <= GAP_ENC_KEY_CONNECTING)) {
      VLOG(1) << __func__ << " Calling GATT_Disconnect:";
      GATT_Disconnect(clcb.conn_id);
      clcb_dealloc(clcb);
    }
  }
}

/** Client connection callback */
void client_connect_cback(tGATT_IF, const RawAddress& bda, uint16_t conn_id,
                          bool connected, tGATT_DISCONN_REASON reason,
                          tGATT_TRANSPORT) {
  tGAP_CLCB* p_clcb = find_clcb_by_bd_addr(bda);
  bda_and_conn_id.emplace_back(std::make_pair(bda,conn_id));
  if (p_clcb == NULL) {
    return;
  }
  if (connected) {
    p_clcb->conn_id = conn_id;
    p_clcb->connected = true;
    /* start operation is pending */
    send_cl_request(*p_clcb);
  } else {
    p_clcb->connected = false;
    cl_op_cmpl(*p_clcb, false, 0, NULL);
    /* clean up clcb */
    clcb_dealloc(*p_clcb);
  }
}

/*******************************************************************************
 *
 * Function         client_disc_res_cback
 *
 * Description      Gatt profile discovery result callback
 *
 * Returns          void
 *
 ******************************************************************************/
void client_disc_res_cback(uint16_t conn_id, tGATT_DISC_TYPE disc_type,
                           tGATT_DISC_RES* p_data) {
  VLOG(1) << __func__;
  tGAP_CLCB* p_clcb = ble_find_clcb_by_conn_id(conn_id);

  if (p_clcb == NULL) return;

  VLOG(1) << __func__ << " disc type:" << +disc_type
          << " enc_key_stage:" << +p_clcb->enc_key_stage;
  switch (disc_type) {
    case GATT_DISC_CHAR_DSCPT: /* stage 3 */
      if (p_data->type == Uuid::From16Bit(GATT_UUID_CHAR_CLIENT_CONFIG)) {
        p_clcb->curr_cccd_handle = p_data->handle;
        p_clcb->enc_key_stage++;
        p_clcb->enc_key_result++;
        VLOG(1) << __func__ << " curr_cccd_handle:" << +p_clcb->curr_cccd_handle;
      }
      break;
  }
}

/*******************************************************************************
 *
 * Function         client_disc_cmpl_cback
 *
 * Description      Gatt profile discovery complete callback
 *
 * Returns          void
 *
 ******************************************************************************/
void client_disc_cmpl_cback(uint16_t conn_id, tGATT_DISC_TYPE disc_type,
                                   tGATT_STATUS status) {
  VLOG(1) << __func__ << " status:" << +status;
  tGAP_CLCB* p_clcb = ble_find_clcb_by_conn_id(conn_id);

  if (p_clcb == NULL) return;

  VLOG(1) << __func__ << " enc_key_stage::" << +p_clcb->enc_key_stage;

  if (status != GATT_SUCCESS || p_clcb->enc_key_result == 0) {
    LOG(WARNING) << __func__
                 << ": Unable to register for enc key material indication ";
    p_clcb->is_enc_key_info_in_progress = false;
    return;
  }

  if (p_clcb->enc_key_stage > GAP_ENC_KEY_CONNECTING) {
    p_clcb->enc_key_result = 0;
    gap_cl_get_enc_key_info(p_clcb);
  }
}

/** Client operation complete callback */
void client_cmpl_cback(uint16_t conn_id, tGATTC_OPTYPE op, tGATT_STATUS status,
                       tGATT_CL_COMPLETE* p_data, uint32_t trans_id) {
  tGAP_CLCB* p_clcb = ble_find_clcb_by_conn_id(conn_id);
  uint16_t op_type;
  uint16_t min, max, latency, tout;
  uint16_t len;
  uint8_t* pp;
  uint16_t handle = 0;

  if (p_clcb == NULL) return;

  op_type = p_clcb->cl_op_uuid;

  DVLOG(1) << StringPrintf(
      "%s: - op_code: 0x%02x  status: 0x%02x  read_type: 0x%04x", __func__, op,
      status, op_type);

  if (status != GATT_SUCCESS) {
    cl_op_cmpl(*p_clcb, false, 0, NULL);
    return;
  }

  pp = p_data->att_value.value;
  if (op == GATTC_OPTYPE_READ) {
    switch (op_type) {
      case GATT_UUID_GAP_PREF_CONN_PARAM:
        /* Extract the peripheral preferred connection parameters and save them */
        STREAM_TO_UINT16(min, pp);
        STREAM_TO_UINT16(max, pp);
        STREAM_TO_UINT16(latency, pp);
        STREAM_TO_UINT16(tout, pp);

        BTM_BleSetPrefConnParams(p_clcb->bda, min, max, latency, tout);
        /* release the connection here */
        cl_op_cmpl(*p_clcb, true, 0, NULL);
        break;

      case GATT_UUID_GAP_DEVICE_NAME:
        len = (uint16_t)strlen((char*)pp);
        if (len > GAP_CHAR_DEV_NAME_SIZE) len = GAP_CHAR_DEV_NAME_SIZE;
        cl_op_cmpl(*p_clcb, true, len, pp);
        break;

      case GATT_UUID_GAP_CENTRAL_ADDR_RESOL:
        cl_op_cmpl(*p_clcb, true, 1, pp);
        break;

      case GATT_UUID_GAP_ENC_KEY_MATERIAL:
        p_clcb->curr_enc_key_char_handle = p_data->att_value.handle;
        p_clcb->enc_key_char_handles.push_back(p_data->att_value.handle);
        len = (uint16_t)strlen((char*)pp);
        cl_op_cmpl(*p_clcb, true, len, pp);
        break;
    }
  } else if (op == GATTC_OPTYPE_WRITE) {
    cl_op_cmpl(*p_clcb, true, 0, NULL);
  } else if (op == GATTC_OPTYPE_INDICATION) {
    LOG(INFO) << __func__ << " Received Indication in GAP profile";
    handle = p_data->att_value.handle;

    if (std::find(p_clcb->enc_key_char_handles.begin(), p_clcb->enc_key_char_handles.end(), handle)
        != p_clcb->enc_key_char_handles.end()) {
      LOG(INFO) << __func__ << " Received Indication for Enc key material char in GAP profile";
      GAP_BleGetEncKeyMaterialInfo(p_clcb->bda, BT_TRANSPORT_LE);
    }
  }
}

bool accept_client_operation(const RawAddress& peer_bda, uint16_t uuid,
                             uint16_t handle, uint8_t op, uint8_t disc_type,
                             tGAP_BLE_CMPL_CBACK* p_cback) {
  tGAP_CLCB* p_clcb = find_clcb_by_bd_addr(peer_bda);
  if (p_clcb == NULL) {
    p_clcb = clcb_alloc(peer_bda);
  }

  VLOG(1) << __func__ << ": BDA: " << peer_bda << " :op: " << +op
          << " :disc_type:" << +disc_type
          << StringPrintf(" cl_op_uuid: 0x%04x", uuid)
          << " start_handle: " << +handle;

  if (GATT_GetConnIdIfConnected(gatt_if, peer_bda, &p_clcb->conn_id,
                                BT_TRANSPORT_LE))
    p_clcb->connected = true;

  if (!GATT_Connect(gatt_if, p_clcb->bda, true, BT_TRANSPORT_LE, true))
    return false;

  if (op == GATTC_OPTYPE_READ) {
    /* enqueue the read request */
    p_clcb->requests.push({.uuid = uuid, .handle = handle, .p_cback = p_cback});
  } else if (op == GATTC_OPTYPE_WRITE) {
    /* enqueue the write request */
    p_clcb->requests.push({.handle = handle, .p_cback = p_cback});
  } else if (op == GATTC_OPTYPE_DISCOVERY) {
    /* enqueue the disc request */
    p_clcb->requests.push({.disc_type = disc_type, .p_cback = p_cback});
  }

  if (p_clcb->connected && p_clcb->cl_op_uuid == 0) {
    if (op == GATTC_OPTYPE_READ) {
      return send_cl_read_request(*p_clcb);
    } else if (op == GATTC_OPTYPE_WRITE) {
      return send_cl_write_request(*p_clcb);
    } else if (op == GATTC_OPTYPE_DISCOVERY) {
      return send_cl_disc_request(*p_clcb);
    } else {
      return true;
    }
  } else {/* wait for connection up or pending operation to finish */
    return true;
  }
}

/*******************************************************************************
 *
 * Function         btm_ble_att_db_init
 *
 * Description      GAP ATT database initalization.
 *
 * Returns          void.
 *
 ******************************************************************************/
void gap_attr_db_init(void) {
  uint16_t service_handle;

  /* Fill our internal UUID with a fixed pattern 0x82 */
  std::array<uint8_t, Uuid::kNumBytes128> tmp;
  tmp.fill(0x82);
  Uuid app_uuid = Uuid::From128BitBE(tmp);
  gatt_attr.fill({});

  gatt_if = GATT_Register(app_uuid, &gap_cback, false);

  GATT_StartIf(gatt_if);

  Uuid svc_uuid = Uuid::From16Bit(UUID_SERVCLASS_GAP_SERVER);
  Uuid name_uuid = Uuid::From16Bit(GATT_UUID_GAP_DEVICE_NAME);
  Uuid icon_uuid = Uuid::From16Bit(GATT_UUID_GAP_ICON);
  Uuid addr_res_uuid = Uuid::From16Bit(GATT_UUID_GAP_CENTRAL_ADDR_RESOL);
  Uuid encr_data_uuid = Uuid::From16Bit(GATT_UUID_GAP_ENC_KEY_MATERIAL);
  Uuid cccd_uuid = Uuid::From16Bit(GATT_UUID_CHAR_CLIENT_CONFIG);

  btgatt_db_element_t service[] = {
    {.type = BTGATT_DB_PRIMARY_SERVICE, .uuid = svc_uuid},
    {.type = BTGATT_DB_CHARACTERISTIC,
     .uuid = name_uuid,
     .properties = GATT_CHAR_PROP_BIT_READ,
     .permissions = GATT_PERM_READ},
    {.type = BTGATT_DB_CHARACTERISTIC,
     .uuid = icon_uuid,
     .properties = GATT_CHAR_PROP_BIT_READ,
     .permissions = GATT_PERM_READ},
    {.type = BTGATT_DB_CHARACTERISTIC,
     .uuid = addr_res_uuid,
     .properties = GATT_CHAR_PROP_BIT_READ,
     .permissions = GATT_PERM_READ},
    {.type = BTGATT_DB_CHARACTERISTIC,
    .uuid = encr_data_uuid,
    .properties = GATT_CHAR_PROP_BIT_READ | GATT_CHAR_PROP_BIT_INDICATE,
    .permissions = GATT_READ_AUTH_REQUIRED},
    {.type = BTGATT_DB_DESCRIPTOR,
    .uuid = cccd_uuid,
    .permissions = (GATT_PERM_READ | GATT_PERM_WRITE)}
#if (BTM_PERIPHERAL_ENABLED == TRUE) /* Only needed for peripheral testing */
    ,
    {.type = BTGATT_DB_CHARACTERISTIC,
     .uuid = Uuid::From16Bit(GATT_UUID_GAP_PREF_CONN_PARAM),
     .properties = GATT_CHAR_PROP_BIT_READ,
     .permissions = GATT_PERM_READ}
#endif
  };

  /* Add a GAP service */
  GATTS_AddService(gatt_if, service,
                   sizeof(service) / sizeof(btgatt_db_element_t));
  service_handle = service[0].attribute_handle;

  DVLOG(1) << __func__ << ": service_handle = " << +service_handle;

  gatt_attr[0].uuid = GATT_UUID_GAP_DEVICE_NAME;
  gatt_attr[0].handle = service[1].attribute_handle;

  gatt_attr[1].uuid = GATT_UUID_GAP_ICON;
  gatt_attr[1].handle = service[2].attribute_handle;

  gatt_attr[2].uuid = GATT_UUID_GAP_CENTRAL_ADDR_RESOL;
  gatt_attr[2].handle = service[3].attribute_handle;
  gatt_attr[2].attr_value.addr_resolution = 0;

  gatt_attr[3].uuid = GATT_UUID_GAP_ENC_KEY_MATERIAL;
  gatt_attr[3].handle = service[4].attribute_handle;
  gatt_attr[4].uuid = GATT_UUID_CHAR_CLIENT_CONFIG;
  gatt_attr[4].handle = service[5].attribute_handle;


#if (BTM_PERIPHERAL_ENABLED == TRUE) /*  Only needed for peripheral testing */

  gatt_attr[5].uuid = GATT_UUID_GAP_PREF_CONN_PARAM;
  gatt_attr[5].attr_value.conn_param.int_max = GAP_PREFER_CONN_INT_MAX; /* 6 */
  gatt_attr[5].attr_value.conn_param.int_min = GAP_PREFER_CONN_INT_MIN; /* 0 */
  gatt_attr[5].attr_value.conn_param.latency = GAP_PREFER_CONN_LATENCY; /* 0 */
  gatt_attr[5].attr_value.conn_param.sp_tout =
      GAP_PREFER_CONN_SP_TOUT; /* 2000 */
  gatt_attr[5].handle = service[6].attribute_handle;
#endif
}

/* Function to check if Indications are required*/
void gap_chk_encr_data(std::pair<RawAddress,uint16_t> bda_conn_id_pair, tGAP_ATTR db_attr){
  VLOG(1) << __func__;
  uint8_t encr_data_cccd = btif_storage_get_encr_data_cccd(bda_conn_id_pair.first);
  if(encr_data_cccd != GATT_CHAR_CLIENT_CONFIG_INDICTION){
    VLOG(1) << __func__ << "Discard Encr DataKey - CCCD disabled " << encr_data_cccd;
    return;
  }
  else{
    uint16_t conn_id =  bda_conn_id_pair.second;
    if (conn_id == GATT_INVALID_CONN_ID) {
      LOG(ERROR) << "Unable to find conn_id for " << bda_conn_id_pair.first;
      return;
    }
    uint8_t encr_material[ENCR_KEY_MATERIAL_LEN];
    memcpy(encr_material, db_attr.attr_value.encr_material.session_key, 16);
    memcpy(encr_material+16, db_attr.attr_value.encr_material.init_vector, 8);
    GATTS_HandleValueIndication(conn_id, db_attr.handle, sizeof(encr_material), encr_material);
  }
}

/*******************************************************************************
 *
 * Function         GAP_BleAttrDBUpdate
 *
 * Description      GAP ATT database update.
 *
 ******************************************************************************/
void GAP_BleAttrDBUpdate(uint16_t attr_uuid, tGAP_BLE_ATTR_VALUE* p_value) {
  DVLOG(1) << StringPrintf("%s: attr_uuid=0x%04x", __func__, attr_uuid);

  for (tGAP_ATTR& db_attr : gatt_attr) {
    if (db_attr.uuid == attr_uuid) {
      DVLOG(1) << StringPrintf("Found attr_uuid=0x%04x", attr_uuid);

      switch (attr_uuid) {
        case GATT_UUID_GAP_ICON:
          db_attr.attr_value.icon = p_value->icon;
          break;

        case GATT_UUID_GAP_PREF_CONN_PARAM:
          memcpy((void*)&db_attr.attr_value.conn_param,
                 (const void*)&p_value->conn_param,
                 sizeof(tGAP_BLE_PREF_PARAM));
          break;

        case GATT_UUID_GAP_DEVICE_NAME:
          BTM_SetLocalDeviceName((char*)p_value->p_dev_name);
          break;

        case GATT_UUID_GAP_CENTRAL_ADDR_RESOL:
          db_attr.attr_value.addr_resolution = p_value->addr_resolution;
          break;

        case GATT_UUID_GAP_ENC_KEY_MATERIAL:
          std::array<uint8_t,ENCR_KEY_MATERIAL_LEN> prev_encr_material;
          std::array<uint8_t,ENCR_KEY_MATERIAL_LEN> new_encr_material;
          std::move(std::begin(db_attr.attr_value.encr_material.session_key),
              std::end(db_attr.attr_value.encr_material.session_key), prev_encr_material.begin());
          std::move(std::begin(db_attr.attr_value.encr_material.init_vector),
              std::end(db_attr.attr_value.encr_material.init_vector),prev_encr_material.begin()+16);
          std::move(std::begin(p_value->encr_material.session_key),
              std::end(p_value->encr_material.session_key), new_encr_material.begin());
          std::move(std::begin(p_value->encr_material.init_vector),
              std::end(p_value->encr_material.init_vector), new_encr_material.begin() + 16);
          memcpy((void*)&db_attr.attr_value.encr_material,
                 (const void*)&p_value->encr_material,
                 sizeof(tGAP_BLE_ENCR_DATAKEY));
          if (prev_encr_material != new_encr_material){
            for(unsigned int i = 0; i < bda_and_conn_id.size(); i++){
              auto temp = bda_and_conn_id[i];
              VLOG(1) << __func__ << " temp.first " << temp.first <<" temp.second "<< temp.second;
              gap_chk_encr_data(temp, db_attr);
            }
          }
          break;
      }
      break;
    }
  }

  return;
}

/*******************************************************************************
 *
 * Function         GAP_BleReadPeerPrefConnParams
 *
 * Description      Start a process to read a connected peripheral's preferred
 *                  connection parameters
 *
 * Returns          true if read started, else false if GAP is busy
 *
 ******************************************************************************/
bool GAP_BleReadPeerPrefConnParams(const RawAddress& peer_bda) {
  return accept_client_operation(peer_bda, GATT_UUID_GAP_PREF_CONN_PARAM, 0,
                                 GATTC_OPTYPE_READ, 0, NULL);
}

/*******************************************************************************
 *
 * Function         GAP_BleReadPeerDevName
 *
 * Description      Start a process to read a connected peripheral's device
 *                  name.
 *
 * Returns          true if request accepted
 *
 ******************************************************************************/
bool GAP_BleReadPeerDevName(const RawAddress& peer_bda,
                            tGAP_BLE_CMPL_CBACK* p_cback) {
  return accept_client_operation(peer_bda, GATT_UUID_GAP_DEVICE_NAME, 0,
                                 GATTC_OPTYPE_READ, 0, p_cback);
}

/*******************************************************************************
 *
 * Function         GAP_BleReadPeerAddressResolutionCap
 *
 * Description      Start a process to read peer address resolution capability
 *
 * Returns          true if request accepted
 *
 ******************************************************************************/
bool GAP_BleReadPeerAddressResolutionCap(const RawAddress& peer_bda,
                                         tGAP_BLE_CMPL_CBACK* p_cback) {
  return accept_client_operation(peer_bda, GATT_UUID_GAP_CENTRAL_ADDR_RESOL,
                                 0, GATTC_OPTYPE_READ, 0, p_cback);
}

/*******************************************************************************
 *
 * Function         GAP_BleCancelReadPeerDevName
 *
 * Description      Cancel reading a peripheral's device name.
 *
 * Returns          true if request accepted
 *
 ******************************************************************************/
bool GAP_BleCancelReadPeerDevName(const RawAddress& peer_bda) {
  tGAP_CLCB* p_clcb = find_clcb_by_bd_addr(peer_bda);

  DVLOG(1) << __func__ << ": BDA: " << peer_bda
           << StringPrintf(" cl_op_uuid: 0x%04x",
                           (p_clcb == NULL) ? 0 : p_clcb->cl_op_uuid);

  if (p_clcb == NULL) {
    LOG(ERROR) << "Cannot cancel current op is not get dev name";
    return false;
  }

  if (!p_clcb->connected) {
    if (!GATT_CancelConnect(gatt_if, peer_bda, true)) {
      LOG(ERROR) << "Cannot cancel where No connection id";
      return false;
    }
  }

  cl_op_cmpl(*p_clcb, false, 0, NULL);

  return (true);
}

/*******************************************************************************
 *
 * Function         GAP_BleReadEncrKeyMaterial
 *
 * Description      Read Encrypted Data Key Material from Database
 *
 * Returns          returns tGAP_BLE_ATTR_VALUE
 *
 ******************************************************************************/
tGAP_BLE_ATTR_VALUE GAP_BleReadEncrKeyMaterial(){
  return gatt_attr[3].attr_value;
}

void btm_ble_config_cccd_enc_key_cmpl(bool status, const RawAddress& bda,
                                      uint16_t length, char* p_name) {
  VLOG(1) << __func__ << ": BDA: " << bda;
  tGAP_CLCB* p_clcb = find_clcb_by_bd_addr(bda);

  if (p_clcb == NULL) {
    VLOG(1) << __func__ << ": p_clcb is NULL: ";
    return;
  }

  p_clcb->enc_key_result = 0;

  VLOG(1) << __func__ << ": curr enc key char handle: " << +p_clcb->curr_enc_key_char_handle;
  std::vector<uint16_t>::iterator it, it_next;
  it = std::find (p_clcb->enc_key_char_handles.begin(),
           p_clcb->enc_key_char_handles.end(), p_clcb->curr_enc_key_char_handle);

  //Check if next enc key char handle available, then discover and configure CCCD for it
  if (it != p_clcb->enc_key_char_handles.end()) {
    it_next = std::next(it,1);
    if ((it_next) != p_clcb->enc_key_char_handles.end()) {
      p_clcb->enc_key_stage = GAP_ENC_KEY_CCCD;
      p_clcb->curr_enc_key_char_handle = *(it_next);
      gap_cl_get_enc_key_info(p_clcb);
    } else {
      VLOG(1) << __func__ << ": next enc key char handle is NOT available:";
      p_clcb->is_enc_key_info_in_progress = false;
    }
  }
}

/*******************************************************************************
 *
 * Function         GAP_BleConfigCccdForKeyMaterial
 *
 * Description      Start configuration of CCC Descriptor of Encryption Key
 *                  Material characteristic for indications
 *
 * Returns          true if read started, else false if GAP is busy
 *
 ******************************************************************************/
bool GAP_BleConfigCccdForKeyMaterial(const RawAddress& peer_bda, uint16_t handle,
                                     tGAP_BLE_CMPL_CBACK* p_cback) {
  VLOG(1) << __func__;
  return accept_client_operation(peer_bda, GATT_UUID_CHAR_CLIENT_CONFIG, handle,
                                 GATTC_OPTYPE_WRITE, 0, p_cback);
}

/*******************************************************************************
 *
 * Function         GAP_BleDiscEncKeyMaterialCCCD
 *
 * Description      Start discvoery of CCC Descriptor of Encryption Key
 *                  Material characteristic
 *
 * Returns          true if read started, else false if GAP is busy
 *
 ******************************************************************************/
bool GAP_BleDiscEncKeyMaterialCCCD(const RawAddress& peer_bda) {
  VLOG(1) << __func__ << ": peer_bda: " << peer_bda;

  return accept_client_operation(peer_bda, 0, 0, GATTC_OPTYPE_DISCOVERY,
                                 GATT_DISC_CHAR_DSCPT, NULL);
}

void btm_ble_read_enc_key_cmpl(bool status, const RawAddress& bda,
                               uint16_t length, char* p_data) {
  VLOG(1) << __func__ << ": BDA: " << bda << " status:" << +status;

  uint16_t conn_id;
  char prev_data[1024];
  std::string new_data;
  std::vector<char> new_char_value;
  int data_len = 0;
  tGAP_CLCB* p_clcb = find_clcb_by_bd_addr(bda);

  if (p_clcb == NULL) {
    VLOG(1) << __func__ << ": p_clcb is NULL: ";
    return;
  }

  if (!status) {
    VLOG(1) << __func__ << ": status is false: ";
    /*Status false(error) implies that read of enc key chars are completed,
    * Go to next stage(desc) now.*/
    if (p_clcb->enc_key_char_handles.empty()) {
      VLOG(1) << __func__ << ": enc_key_char_handles empty: ";
      p_clcb->is_enc_key_info_in_progress = false;
      return;
    }
    p_clcb->enc_key_stage++;

    //Pick first enc key char handle, and find CCCD desc for it to configure
    if (!p_clcb->enc_key_char_handles.empty()) {
      p_clcb->curr_enc_key_char_handle = p_clcb->enc_key_char_handles[0];
    }

    gap_cl_get_enc_key_info(p_clcb);
    return;
  }

  conn_id = p_clcb->conn_id;
  VLOG(1) << __func__ << ": curr_enc_key_char_handle: " << p_clcb->curr_enc_key_char_handle;
  /* Begin Parsing the Encrypted Data Key Material for the Key & IV
     The Scanner Receives the Encrypted Data Material in Little Endian.
     Meaning first 16 bytes is the Key and the last 8 are the IV. */
  char reversekeyiv[ENC_KEY_MATERIAL_LEN];
  for (int i = ENC_KEY_MATERIAL_LEN-1; i >= 0; i--) {
    if (i >= ENC_KEY_LEN) { /* Last 8 bytes is IV storing in Big Endian */
      reversekeyiv[((ENC_KEY_MATERIAL_LEN-1) - i)+ENC_KEY_LEN] = p_data[i];
    } else { /* First 16 bytes is Key. Storing in Big Endian */
      reversekeyiv[(ENC_KEY_LEN-1) - i] = p_data[i];
    }
  }
  p_data = reversekeyiv;
  size_t len = btif_config_get_bin_length(bda.ToString().c_str(), "ENC_KEY_MATERIAL");
  if (len > 0) {
    if (btif_storage_get_enc_key_material((RawAddress*)&bda, (uint8_t*)&prev_data[0], (int*)&len)
         == BT_STATUS_SUCCESS) {
      if (length > 0) {
        uint8_t concat_arr[length+len];
        uint8_t *p_concat_arr = &concat_arr[0];
        memcpy(concat_arr, prev_data, len);
        int j= len;
        for (int i=0; i< length; i++) {
          concat_arr[j++] = p_data[i];
        }

        btif_storage_remove_enc_key_material(&bda);
        btif_storage_set_enc_key_material((RawAddress*)&bda, concat_arr, (length+len));
      }
    }
  } else {
    VLOG(1) << __func__ << ": prev enc key char value NOT available: ";
    if (p_data) {
      btif_storage_set_enc_key_material((RawAddress*)&bda, (uint8_t*)p_data, length);
    }
  }

  /*Dont increment p_clcb->enc_key_stage as we want to be in Read enc key material char
   * state to read multiple other enc key material char values and concatenate them*/
  gap_cl_get_enc_key_info(p_clcb);
}

/*******************************************************************************
 *
 * Function         GAP_BleReadEncKeyMaterial
 *
 * Description      Start a process to read a connected peripheral's Encryption
 *                  Key material characteristic
 *
 * Returns          true if read started, else false if GAP is busy
 *
 ******************************************************************************/
bool GAP_BleReadEncKeyMaterial(const RawAddress& peer_bda, uint16_t handle,
                               tGAP_BLE_CMPL_CBACK* p_cback) {
  VLOG(1) << __func__;
  return accept_client_operation(peer_bda, GATT_UUID_GAP_ENC_KEY_MATERIAL,
                                 handle, GATTC_OPTYPE_READ, 0, p_cback);
}

/*******************************************************************************
 *
 * Function         gap_cl_get_enc_key_info
 *
 * Description      get and configure encryption key material characteristic
 *
 * Returns          void
 *
 ******************************************************************************/
static void gap_cl_get_enc_key_info(tGAP_CLCB* p_clcb) {
  VLOG(1) << __func__ << ": stage: " << +p_clcb->enc_key_stage;

  switch (p_clcb->enc_key_stage) {
    case GAP_ENC_KEY_CHARACTERISTIC: /* Read Enc Key Material Char */
      GAP_BleReadEncKeyMaterial(p_clcb->bda, (p_clcb->curr_enc_key_char_handle+1),
                                btm_ble_read_enc_key_cmpl);
      break;

    case GAP_ENC_KEY_CCCD: /* Discover CCCD */
      GAP_BleDiscEncKeyMaterialCCCD(p_clcb->bda);
      break;

    case GAP_ENC_KEY_CONFIG_CCCD: /* Config cccd */
       GAP_BleConfigCccdForKeyMaterial(p_clcb->bda, p_clcb->curr_cccd_handle,
                                       btm_ble_config_cccd_enc_key_cmpl);
       break;
  }
}

/*******************************************************************************
 *
 * Function         GAP_BleGetEncKeyMaterialInfo
 *
 * Description      Get Encryption Key Material information characteristic value
 *                  from remote device
 *
 * Returns          none
 *
 ******************************************************************************/
void GAP_BleGetEncKeyMaterialInfo(const RawAddress& remote_bda,
                                  tBT_TRANSPORT transport) {
  tGAP_CLCB* p_clcb = find_clcb_by_bd_addr(remote_bda);
  VLOG(1) << __func__;
  if (p_clcb == NULL) {
    p_clcb = clcb_alloc(remote_bda);
  }

  if (p_clcb == NULL) return;

  if (p_clcb->is_enc_key_info_in_progress) {
    VLOG(1) << __func__ << "Enc key info already in progress";
    return;
  }

  btif_storage_remove_enc_key_material(&remote_bda);

  p_clcb->is_enc_key_info_in_progress = true;
  p_clcb->enc_key_stage = GAP_ENC_KEY_CONNECTING;
  p_clcb->curr_enc_key_char_handle = 0;
  p_clcb->curr_cccd_handle = 0;
  p_clcb->enc_key_char_handles.clear();

  p_clcb->enc_key_stage++;
  gap_cl_get_enc_key_info(p_clcb);
}
