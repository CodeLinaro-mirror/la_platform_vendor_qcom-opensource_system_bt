/******************************************************************************
 *
 *  Copyright 1999-2012 Broadcom Corporation
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

/******************************************************************************
 *
 *  This file contains functions for BLE whitelist operation.
 *
 ******************************************************************************/

#include <base/logging.h>
#include <unordered_map>

#include "bt_types.h"
#include "btm_int.h"
#include "btu.h"
#include "device/include/controller.h"
#include "hcimsgs.h"
#include "l2c_int.h"
#include "osi/include/allocator.h"
#include "osi/include/osi.h"
#include "ble_advertiser.h"

extern void btm_send_hci_create_connection(
    uint16_t scan_int, uint16_t scan_win, uint8_t init_filter_policy,
    uint8_t addr_type_peer, const RawAddress& bda_peer, uint8_t addr_type_own,
    uint16_t conn_int_min, uint16_t conn_int_max, uint16_t conn_latency,
    uint16_t conn_timeout, uint16_t min_ce_len, uint16_t max_ce_len,
    uint8_t phy);
extern void btm_ble_create_conn_cancel();
void wl_remove_complete(uint8_t* p_data, uint16_t /* evt_len */);

// Unfortunately (for now?) we have to maintain a copy of the device whitelist
// on the host to determine if a device is pending to be connected or not. This
// controls whether the host should keep trying to scan for whitelisted
// peripherals or not.
// TODO: Move all of this to controller/le/background_list or similar?
typedef struct BackgroundConnection {
  RawAddress address;
  uint8_t addr_type;
  bool in_controller_wl;
  uint8_t addr_type_in_wl;
  bool pending_removal;
  std::vector<uint8_t> adv_ids;
} background_connection_t;

struct BgConnHash {
  std::size_t operator()(const RawAddress& x) const {
    const uint8_t* a = x.address;
    return a[0] ^ (a[1] << 8) ^ (a[2] << 16) ^ (a[3] << 24) ^ a[4] ^
           (a[5] << 8);
  }
};

static void background_connection_remove_device(uint8_t background_role,
                                                uint8_t adv_handle,
                                                const RawAddress& address);
static void btm_suspend_wl_activity(tBTM_BLE_WL_STATE wl_state);
static void btm_resume_wl_activity(tBTM_BLE_WL_STATE wl_state);

static std::unordered_map<RawAddress, background_connection_t, BgConnHash>
    background_initiators;

static std::unordered_map<RawAddress, background_connection_t, BgConnHash>
    background_advertisers;

static void background_connection_add(uint8_t background_role, uint8_t adv_handle,
                                      uint8_t addr_type, const RawAddress& address) {
  std::unordered_map<RawAddress, background_connection_t, BgConnHash>
      background_map;
  BTM_TRACE_EVENT("%s", __func__);
  if (background_role == BLE_BG_INITIATOR) {
    background_map = background_initiators;
  } else if (background_role == BLE_BG_ADVERTISER) {
    background_map = background_advertisers;
  }

  auto map_iter = background_map.find(address);
  if (map_iter == background_map.end()) {
    BTM_TRACE_EVENT("%s::Add device entry", __func__);
    std::vector<uint8_t> adv_ids;
    if (background_role == BLE_BG_ADVERTISER) {
      adv_ids.push_back(adv_handle);
    }
    background_map[address] =
        background_connection_t{address, addr_type, false, 0, false, adv_ids};
  } else {
    BTM_TRACE_EVENT("%s::Update device entry", __func__);
    background_connection_t* connection = &map_iter->second;
    connection->addr_type = addr_type;
    connection->pending_removal = false;
    if ((background_role == BLE_BG_ADVERTISER) &&
        std::find(connection->adv_ids.begin(), connection->adv_ids.end(), adv_handle)
          == connection->adv_ids.end())
      connection->adv_ids.push_back(adv_handle);
  }
  if (background_role == BLE_BG_INITIATOR) {
    background_initiators = background_map;
  } else if (background_role == BLE_BG_ADVERTISER) {
    background_advertisers = background_map;
  }
}

static void background_connection_remove(uint8_t background_role, uint8_t adv_handle,
                                         const RawAddress& address) {
  VLOG(1) << __func__;
  if (address.IsEmpty() && (background_role == BLE_BG_ADVERTISER)) {
    for (auto& map_el : background_advertisers) {
      background_connection_remove_device(background_role, adv_handle, map_el.first);
    }
  } else {
    background_connection_remove_device(background_role, adv_handle, address);
  }
}

static void background_connection_remove_device(uint8_t background_role,
                                                uint8_t adv_handle,
                                                const RawAddress& address) {
  BTM_TRACE_EVENT("%s", __func__);
  std::unordered_map<RawAddress, background_connection_t, BgConnHash>
      background_map, background_map_other;

  if (background_role == BLE_BG_INITIATOR) {
    background_map = background_initiators;
    background_map_other = background_advertisers;
  } else if (background_role == BLE_BG_ADVERTISER) {
    background_map = background_advertisers;
    background_map_other = background_initiators;
  }

  auto map_iter = background_map.find(address);
  if (map_iter != background_map.end()) {
    if (background_role == BLE_BG_ADVERTISER) {
      background_connection_t* connection = &map_iter->second;
      auto iter_adv_id =
          std::find(connection->adv_ids.begin(), connection->adv_ids.end(), adv_handle);
      if (iter_adv_id != connection->adv_ids.end()) {
        BTM_TRACE_EVENT("%s Found the adv handle in list, erasing it from list", __func__);
        connection->adv_ids.erase(iter_adv_id);
      }

      if (!connection->adv_ids.empty() || connection->adv_ids.size() > 0) {
        BTM_TRACE_EVENT("%s:: Not removing dev as other apps are still interested", __func__);
        return;
      }
    }
    if (map_iter->second.in_controller_wl) {
      auto map_iter_other = background_map_other.find(address);
        if (map_iter_other != background_map_other.end()) {
          /*Only when the other hashmap hasnot added the device to whitelist OR other hash map
          has this BD addr waiting to be removed(ie remove device already done for this BD addr
          in this hashmap)*/
          if (!map_iter_other->second.in_controller_wl || map_iter_other->second.pending_removal) {
            map_iter->second.pending_removal = true;
          }
        } else {
          map_iter->second.pending_removal = true;
        }
    } else {
      BTM_TRACE_EVENT("%s:: Erasing entry", __func__);
      background_map.erase(map_iter);
    }
  }
  if (background_role == BLE_BG_INITIATOR) {
    background_initiators = background_map;
  } else if (background_role == BLE_BG_ADVERTISER) {
    background_advertisers = background_map;
  }
}
static void background_connections_clear() {
  background_initiators.clear();
  background_advertisers.clear();
}

static bool background_connections_pending(uint8_t background_role) {
  std::unordered_map<RawAddress, background_connection_t, BgConnHash> background_map;
  if (background_role == BLE_BG_INITIATOR) {
    background_map = background_initiators;
  } else if (background_role == BLE_BG_ADVERTISER) {
    background_map = background_advertisers;
  }
  for (auto& map_el : background_map) {
    background_connection_t* connection = &map_el.second;

    if ((background_role == BLE_BG_INITIATOR) && connection->pending_removal) continue;
    const bool connected =
        BTM_IsAclConnectionUp(connection->address, BT_TRANSPORT_LE);
    if (!connected) {
      return true;
    }
  }
  return false;
}

static int background_connections_count() {
  int count = 0;
  for (auto& map_el : background_initiators) {
    if (!map_el.second.pending_removal) ++count;
  }
  for (auto& map_el : background_advertisers) {
    if (!map_el.second.pending_removal) ++count;
  }
  return count;
}

/*******************************************************************************
 *
 * Function         btm_update_scanner_filter_policy
 *
 * Description      This function updates the filter policy of scanner
 ******************************************************************************/
void btm_update_scanner_filter_policy(tBTM_BLE_SFP scan_policy) {
  tBTM_BLE_INQ_CB* p_inq = &btm_cb.ble_ctr_cb.inq_var;

  uint32_t scan_interval =
      !p_inq->scan_interval ? BTM_BLE_GAP_DISC_SCAN_INT : p_inq->scan_interval;
  uint32_t scan_window =
      !p_inq->scan_window ? BTM_BLE_GAP_DISC_SCAN_WIN : p_inq->scan_window;

  BTM_TRACE_EVENT("%s", __func__);

  p_inq->sfp = scan_policy;
  p_inq->scan_type = p_inq->scan_type == BTM_BLE_SCAN_MODE_NONE
                         ? BTM_BLE_SCAN_MODE_ACTI
                         : p_inq->scan_type;

  btm_send_hci_set_scan_params(
      p_inq->scan_type, (uint16_t)scan_interval, (uint16_t)scan_window,
      btm_cb.ble_ctr_cb.addr_mgnt_cb.own_addr_type, scan_policy);
}

/*******************************************************************************
 *
 * Function         btm_ble_bgconn_cancel_if_disconnected
 *
 * Description      If a device has been disconnected, it must be re-added to
 *                  the white list. If needed, this function cancels a pending
 *                  initiate command in order to trigger restart of the initiate
 *                  command which in turn updates the white list.
 *
 * Parameters       bd_addr: updated device
 *
 ******************************************************************************/
void btm_ble_bgconn_cancel_if_disconnected(const RawAddress& bd_addr) {
  if (btm_ble_get_conn_st() != BLE_CONNECTING) return;

  auto map_it = background_initiators.find(bd_addr);
  if (map_it != background_initiators.end()) {
    background_connection_t* connection = &map_it->second;
    if (!connection->in_controller_wl && !connection->pending_removal &&
        !BTM_IsAclConnectionUp(bd_addr, BT_TRANSPORT_LE)) {
      btm_ble_stop_auto_conn();
    }
  }
}

bool BTM_BackgroundConnectAddressKnown(const RawAddress& address) {
  tBTM_SEC_DEV_REC* p_dev_rec = btm_find_dev(address);

  //  not a known device, or a classic device, we assume public address
  if (p_dev_rec == NULL || (p_dev_rec->device_type & BT_DEVICE_TYPE_BLE) == 0)
    return true;

  // bonded device with identity address known
  if (!p_dev_rec->ble.identity_addr.IsEmpty()) {
    return true;
  }

  // Public address, Random Static, or Random Non-Resolvable Address known
  if (p_dev_rec->ble.ble_addr_type == BLE_ADDR_PUBLIC ||
      !BTM_BLE_IS_RESOLVE_BDA(address)) {
    return true;
  }

  // Only Resolvable Private Address (RPA) is known, we don't allow it into
  // the background connection procedure.
  return false;
}

/*******************************************************************************
 *
 * Function         btm_add_dev_to_controller
 *
 * Description      This function load the device into controller white list
 ******************************************************************************/
bool btm_add_dev_to_controller(bool to_add, const RawAddress& bd_addr,
                               uint8_t background_role, uint8_t adv_handle) {
  tBTM_SEC_DEV_REC* p_dev_rec = btm_find_dev(bd_addr);

  BTM_TRACE_EVENT("%s", __func__);

  if (p_dev_rec != NULL && p_dev_rec->device_type & BT_DEVICE_TYPE_BLE) {
    if (to_add) {
      if (p_dev_rec->ble.identity_addr != bd_addr &&
          !p_dev_rec->ble.identity_addr.IsEmpty()) {
        background_connection_add(background_role, adv_handle, p_dev_rec->ble.identity_addr_type,
                                  p_dev_rec->ble.identity_addr);
      } else {
        background_connection_add(background_role, adv_handle, p_dev_rec->ble.ble_addr_type, bd_addr);

        if (p_dev_rec->ble.ble_addr_type == BLE_ADDR_RANDOM &&
            BTM_BLE_IS_RESOLVE_BDA(bd_addr)) {
          LOG(INFO) << __func__ << " addig RPA into white list";
        }
      }

      p_dev_rec->ble.in_controller_list |= BTM_WHITE_LIST_BIT;
    } else {
      if (!p_dev_rec->ble.identity_addr.IsEmpty() &&
          p_dev_rec->ble.identity_addr != bd_addr) {
        background_connection_remove(background_role,adv_handle, p_dev_rec->ble.identity_addr);
      } else {
        background_connection_remove(background_role, adv_handle,bd_addr);

        if (p_dev_rec->ble.ble_addr_type == BLE_ADDR_RANDOM &&
            BTM_BLE_IS_RESOLVE_BDA(bd_addr)) {
          LOG(INFO) << __func__ << " removing RPA from white list";
        }
      }

      p_dev_rec->ble.in_controller_list &= ~BTM_WHITE_LIST_BIT;
    }
  } else {
    /* not a known device, i.e. attempt to connect to device never seen before
     */
    if (to_add)
      background_connection_add(background_role, adv_handle, BLE_ADDR_PUBLIC, bd_addr);
    else
      background_connection_remove(background_role, adv_handle, bd_addr);
  }

  return true;
}

/** White list add complete */
void wl_add_complete(uint8_t* p_data, uint16_t /* evt_len */) {
  uint8_t status;
  STREAM_TO_UINT8(status, p_data);
  VLOG(2) << __func__ << ": status=" << loghex(status);
}

/** White list element remove complete */
void wl_remove_complete(uint8_t* p_data, uint16_t /* evt_len */) {
  uint8_t status;
  STREAM_TO_UINT8(status, p_data);
  VLOG(2) << __func__ << ": status=" << loghex(status);
}

/*******************************************************************************
 *
 * Function         btm_execute_wl_dev_operation
 *
 * Description      execute the pending whitelist device operation (loading or
 *                                                                  removing)
 ******************************************************************************/
bool btm_execute_wl_dev_operation(uint8_t background_role) {
  std::unordered_map<RawAddress, background_connection_t, BgConnHash> background_map;
  BTM_TRACE_EVENT("%s ", __func__);
  if (background_role == BLE_BG_INITIATOR) {
    background_map = background_initiators;
  } else if (background_role == BLE_BG_ADVERTISER) {
    background_map = background_advertisers;
  }
  // handle removals first to avoid filling up controller's white list
  for (auto map_it = background_map.begin();
      map_it != background_map.end();) {
    background_connection_t* connection = &map_it->second;
    if (connection->pending_removal) {
      btsnd_hcic_ble_remove_from_white_list(
          connection->addr_type_in_wl, connection->address,
          base::BindOnce(&wl_remove_complete));
      map_it = background_map.erase(map_it);
    } else
      ++map_it;
  }
  for (auto& map_el : background_map) {
    background_connection_t* connection = &map_el.second;
    const bool connected =
        BTM_IsAclConnectionUp(connection->address, BT_TRANSPORT_LE);
    if (!connection->in_controller_wl && !connected) {
      btsnd_hcic_ble_add_white_list(connection->addr_type, connection->address,
                                    base::BindOnce(&wl_add_complete));
      connection->in_controller_wl = true;
      connection->addr_type_in_wl = connection->addr_type;
    } else if (connection->in_controller_wl && connected) {
      /* Bluetooth Core 4.2 as well as ESR08 disallows more than one
         connection between two LE addresses. Not all controllers handle this
         correctly, therefore we must make sure connected devices are not in
         the white list when bg connection attempt is active. */
      btsnd_hcic_ble_remove_from_white_list(
          connection->addr_type_in_wl, connection->address,
          base::BindOnce(&wl_remove_complete));
      connection->in_controller_wl = false;
    }
  }
  if (background_role == BLE_BG_INITIATOR) {
    background_initiators = background_map;
  } else if (background_role == BLE_BG_ADVERTISER) {
    background_advertisers = background_map;
  }
  return true;
}

/*******************************************************************************
 *
 * Function         btm_update_dev_to_white_list
 *
 * Description      This function adds or removes a device into/from
 *                  the white list.
 *
 ******************************************************************************/
bool btm_update_dev_to_white_list(bool to_add, const RawAddress& bd_addr,
                                  uint8_t background_role, uint8_t adv_handle) {
  tBTM_BLE_CB* p_cb = &btm_cb.ble_ctr_cb;

  BTM_TRACE_EVENT("%s", __func__);
  if (to_add &&
      background_connections_count() ==
          controller_get_interface()->get_ble_white_list_size()) {
    BTM_TRACE_ERROR("%s Whitelist full, unable to add device", __func__);
    return false;
  }

  btm_suspend_wl_activity(p_cb->wl_state);
  btm_add_dev_to_controller(to_add, bd_addr, background_role, adv_handle);
  btm_resume_wl_activity(p_cb->wl_state);
  return true;
}

/*******************************************************************************
 *
 * Function         btm_ble_white_list_init
 *
 * Description      Initialize white list size
 *
 ******************************************************************************/
void btm_ble_white_list_init(uint8_t white_list_size) {
  BTM_TRACE_DEBUG("%s white_list_size = %d", __func__, white_list_size);
}

uint8_t BTM_GetWhiteListSize() {
  const controller_t* controller = controller_get_interface();
  if (!controller->supports_ble()) {
    return 0;
  }
  return controller->get_ble_white_list_size();
}

bool BTM_SetLeConnectionModeToFast() {
  VLOG(2) << __func__;
  tBTM_BLE_CB* p_cb = &btm_cb.ble_ctr_cb;
  if ((p_cb->scan_int == BTM_BLE_SCAN_PARAM_UNDEF &&
       p_cb->scan_win == BTM_BLE_SCAN_PARAM_UNDEF) ||
      (p_cb->scan_int == BTM_BLE_SCAN_SLOW_INT_1 &&
       p_cb->scan_win == BTM_BLE_SCAN_SLOW_WIN_1)) {
    p_cb->scan_int = BTM_BLE_SCAN_FAST_INT;
    p_cb->scan_win = BTM_BLE_SCAN_FAST_WIN;
    return true;
  }
  return false;
}

void BTM_SetLeConnectionModeToSlow() {
  VLOG(2) << __func__;
  tBTM_BLE_CB* p_cb = &btm_cb.ble_ctr_cb;
  if ((p_cb->scan_int == BTM_BLE_SCAN_PARAM_UNDEF &&
       p_cb->scan_win == BTM_BLE_SCAN_PARAM_UNDEF) ||
      (p_cb->scan_int == BTM_BLE_SCAN_FAST_INT &&
       p_cb->scan_win == BTM_BLE_SCAN_FAST_WIN)) {
    p_cb->scan_int = BTM_BLE_SCAN_SLOW_INT_1;
    p_cb->scan_win = BTM_BLE_SCAN_SLOW_WIN_1;
  }
}

/** This function is to start auto connection procedure */
bool btm_ble_start_auto_conn() {
  BTM_TRACE_EVENT("%s", __func__);
  tBTM_BLE_CB* p_cb = &btm_cb.ble_ctr_cb;
  bool exec = true;
  uint16_t scan_int;
  uint16_t scan_win;
  uint8_t own_addr_type = p_cb->addr_mgnt_cb.own_addr_type;
  uint8_t peer_addr_type = BLE_ADDR_PUBLIC;
  bool is_background_initiator = false;
  bool is_background_advertiser = false;

  uint8_t phy = PHY_LE_1M;
  if (controller_get_interface()->supports_ble_2m_phy()) phy |= PHY_LE_2M;
  if (controller_get_interface()->supports_ble_coded_phy()) phy |= PHY_LE_CODED;

  if (!btm_ble_topology_check(BTM_BLE_STATE_INIT)) {
    LOG(INFO) << "initate background connection fail, topology limitation";
    return false;
  }

  if (p_cb->conn_state == BLE_CONN_IDLE && background_connections_pending(BLE_BG_INITIATOR)
      && l2cu_can_allocate_lcb()) {
      btm_execute_wl_dev_operation(BLE_BG_INITIATOR);
      is_background_initiator = true;
  }
  if (p_cb->conn_state == BLE_CONN_IDLE && background_connections_pending(BLE_BG_ADVERTISER)) {
      btm_execute_wl_dev_operation(BLE_BG_ADVERTISER);
      is_background_advertiser = true;
  } else {
    exec = false;
  }

  p_cb->wl_state |= BTM_BLE_WL_INIT;

#if (BLE_PRIVACY_SPT == TRUE)
  btm_ble_enable_resolving_list_for_platform(BTM_BLE_RL_INIT);
#endif

  if(is_background_initiator) {
    BTM_TRACE_EVENT("%s, start connecting", __func__);
    scan_int = (p_cb->scan_int == BTM_BLE_SCAN_PARAM_UNDEF)
                   ? BTM_BLE_SCAN_SLOW_INT_1
                   : p_cb->scan_int;
    scan_win = (p_cb->scan_win == BTM_BLE_SCAN_PARAM_UNDEF)
                   ? BTM_BLE_SCAN_SLOW_WIN_1
                   : p_cb->scan_win;

#if (BLE_PRIVACY_SPT == TRUE)
    if (btm_cb.ble_ctr_cb.rl_state != BTM_BLE_RL_IDLE &&
        controller_get_interface()->supports_ble_privacy()) {
      own_addr_type |= BLE_ADDR_TYPE_ID_BIT;
      peer_addr_type |= BLE_ADDR_TYPE_ID_BIT;
    }
#endif

    btm_send_hci_create_connection(
                scan_int,                       /* uint16_t scan_int      */
                scan_win,                       /* uint16_t scan_win      */
                0x01,                           /* uint8_t white_list     */
                peer_addr_type,                 /* uint8_t addr_type_peer */
                RawAddress::kEmpty,             /* BD_ADDR bda_peer     */
                own_addr_type,                  /* uint8_t addr_type_own */
                BTM_BLE_CONN_INT_MIN_DEF,       /* uint16_t conn_int_min  */
                BTM_BLE_CONN_INT_MAX_DEF,       /* uint16_t conn_int_max  */
                BTM_BLE_CONN_SLAVE_LATENCY_DEF, /* uint16_t conn_latency  */
                BTM_BLE_CONN_TIMEOUT_DEF,       /* uint16_t conn_timeout  */
                0,                              /* uint16_t min_len       */
                0,                              /* uint16_t max_len       */
                phy);

    p_cb->conn_state = BLE_CONNECTING;
  }
  //Resume advs
  if (is_background_advertiser) {
    BleAdvertisingManager::Get()->Resume();
  }
  return exec;
}

/** This function is to stop auto connection procedure */
bool btm_ble_stop_auto_conn() {
  tBTM_BLE_CB* p_cb = &btm_cb.ble_ctr_cb;
  BTM_TRACE_EVENT("%s, conn_state %d ", __func__, p_cb->conn_state);

  if (p_cb->conn_state != BLE_CONNECTING) {
    BTM_TRACE_DEBUG("conn_st = %d, not in auto conn state, cannot stop",
                    p_cb->conn_state);
    return false;
  }
  btm_ble_create_conn_cancel();

  p_cb->wl_state &= ~BTM_BLE_WL_INIT;

  if (BleAdvertisingManager::IsInitialized()) {
    BleAdvertisingManager::Get()->Suspend();
  }
  return true;
}

/*******************************************************************************
 *
 * Function         btm_suspend_wl_activity
 *
 * Description      This function is to suspend white list related activity
 *
 * Returns          none.
 *
 ******************************************************************************/
static void btm_suspend_wl_activity(tBTM_BLE_WL_STATE wl_state) {
  if ((wl_state & BTM_BLE_WL_INIT) || (BleAdvertisingManager::IsInitialized()
      && BleAdvertisingManager::Get()->IsWhiteListAdvActive())) {
    btm_ble_stop_auto_conn();
  }
}
/*******************************************************************************
 *
 * Function         btm_resume_wl_activity
 *
 * Description      This function is to resume white list related activity
 *
 * Returns          none.
 *
 ******************************************************************************/
static void btm_resume_wl_activity(tBTM_BLE_WL_STATE wl_state) {
  btm_ble_resume_bg_conn();
}

/*******************************************************************************
 *
 * Function         btm_ble_suspend_bg_conn
 *
 * Description      This function is to suspend an active background connection
 *                  procedure.
 *
 * Parameters       none.
 *
 * Returns          none.
 *
 ******************************************************************************/
bool btm_ble_suspend_bg_conn(void) {
  BTM_TRACE_EVENT("%s", __func__);
  if (btm_cb.ble_ctr_cb.bg_conn_type == BTM_BLE_CONN_AUTO) {
    return btm_ble_stop_auto_conn();
  }
  return false;
}

/*******************************************************************************
 *
 * Function         btm_ble_resume_bg_conn
 *
 * Description      This function is to resume a background auto connection
 *                  procedure.
 *
 * Parameters       none.
 *
 * Returns          none.
 *
 ******************************************************************************/
bool btm_ble_resume_bg_conn(void) {
  BTM_TRACE_EVENT("%s", __func__);
  tBTM_BLE_CB* p_cb = &btm_cb.ble_ctr_cb;
  if (p_cb->conn_state != BLE_CONNECTING || (BleAdvertisingManager::IsInitialized()
      && BleAdvertisingManager::Get()->IsWhiteListAdvActive())) {
    return btm_ble_start_auto_conn();
  }
  return false;
}

/** Adds the device into white list. Returns false if white list is full and
 * device can't be added, true otherwise. */
bool BTM_WhiteListAdd(const RawAddress& address) {
  BTM_TRACE_EVENT("%s", __func__);

  if (background_connections_count() ==
      controller_get_interface()->get_ble_white_list_size()) {
    BTM_TRACE_ERROR("%s Whitelist full, unable to add device", __func__);
    return false;
  }

  if (btm_cb.ble_ctr_cb.wl_state & BTM_BLE_WL_INIT) {
    btm_ble_stop_auto_conn();
  }
  btm_add_dev_to_controller(true, address, BLE_BG_INITIATOR, -1);
  btm_ble_resume_bg_conn();
  return true;
}

/** Removes the device from white list */
void BTM_WhiteListRemove(const RawAddress& address) {
  BTM_TRACE_EVENT("%s", __func__);
  if (btm_cb.ble_ctr_cb.wl_state & BTM_BLE_WL_INIT) {
    btm_ble_stop_auto_conn();
  }
  btm_add_dev_to_controller(false, address, BLE_BG_INITIATOR, -1);
  btm_execute_wl_dev_operation(BLE_BG_INITIATOR);
}

/** clear white list complete */
void wl_clear_complete(uint8_t* p_data, uint16_t /* evt_len */) {
  uint8_t status;
  STREAM_TO_UINT8(status, p_data);
  VLOG(2) << __func__ << ": status=" << loghex(status);
}

/** Clear the whitelist, end any pending whitelist connections */
void BTM_WhiteListClear() {
  VLOG(1) << __func__;
  if (!controller_get_interface()->supports_ble()) return;
  btm_ble_stop_auto_conn();
  btsnd_hcic_ble_clear_white_list(base::BindOnce(&wl_clear_complete));
  background_connections_clear();
}
