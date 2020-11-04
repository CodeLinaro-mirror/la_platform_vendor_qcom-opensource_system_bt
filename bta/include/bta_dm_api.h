/******************************************************************************
 *
 *  Copyright (C) 2006-2012 Broadcom Corporation
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
 *  This is the interface file for device mananger functions.
 *
 ******************************************************************************/
#ifndef BTA_DM_API_H
#define BTA_DM_API_H

#include "stack/include/bt_types.h"
#include "bta/dm/bta_dm_int.h"

// Brings connection to active mode
void bta_dm_pm_active(const RawAddress& peer_addr);

extern bool is_remote_dev_le_support(const RawAddress remote_bdaddr);
extern void bta_lea_update_bond_db(RawAddress p_bd_addr, uint8_t transport);
extern bool is_le_audio_service(bluetooth::Uuid uuid);
extern int bta_is_lea_valid_bdaddr(RawAddress p_bd_addr);
extern bool bta_is_le_audio_supported(RawAddress p_bd_addr);
extern bool bta_lea_is_dumo_device(RawAddress p_bd_addr);
extern RawAddress bta_lea_get_id_addr(RawAddress p_bd_addr);
extern tBTA_DEV_PAIRING_CB* bta_get_lea_pair_cb(RawAddress peer_addr);
extern bool bta_lea_addr_match(RawAddress p_bd_addr);
extern void bta_dm_reset_lea_pairing_info(RawAddress p_addr);
extern bool bta_lea_is_le_pairing(RawAddress p_bd_addr);
extern bool bta_is_remote_support_lea(RawAddress p_addr);
extern bool bta_lea_is_identity_addr_match(RawAddress p_addr);
extern void bta_dm_lea_disc_complete(RawAddress p_bd_addr);
extern void bta_dm_csis_disc_complete(RawAddress p_bd_addr, bool status);
#endif /* BTA_DM_API_H */
