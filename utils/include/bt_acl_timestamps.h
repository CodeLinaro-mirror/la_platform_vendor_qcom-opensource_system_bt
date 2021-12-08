/******************************************************************************
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************/

#pragma once
/*******************************************************************************
**  Functions
********************************************************************************/
/*******************************************************************************
 **
 ** Function         bt_acl_init_timestamps_info
 **
 ** Description      int the variables for timestamps
 **
 ** Returns          Nothing
 **
 *******************************************************************************/
void bt_acl_init_timestamps_info(void);

/*******************************************************************************
 **
 ** Function         bt_acl_save_acl_timestamps
 **
 ** Description      save the timestamps for each ACL packet
 **
 ** Returns          void
 **
 *******************************************************************************/
void bt_acl_save_acl_timestamps(BT_HDR *packet);

/*******************************************************************************
 **
 ** Function         bt_acl_update_lcid
 **
 ** Description      save the lcid info for AVDTP stream
 **
 ** Returns          Nothing
 **
 *******************************************************************************/
void bt_acl_update_lcid(uint16_t handle, uint16_t scid, uint16_t dcid);

/*******************************************************************************
 **
 ** Function         bt_acl_init_timestamps_by_handle
 **
 ** Description      init the timestamps info by handle.
 **
 ** Returns          None
 **
 *******************************************************************************/
void bt_acl_init_timestamps_by_handle(uint16_t handle);

/*******************************************************************************
 **
 ** Function         bt_acl_remove_timestamps_by_handle
 **
 ** Description      remove the timestamps info by handle.
 **
 ** Returns          None
 **
 *******************************************************************************/
void bt_acl_remove_timestamps_by_handle(uint16_t handle);

/*******************************************************************************
 **
 ** Function         bt_acl_calc_timestamps_by_handle
 **
 ** Description      calculate the timestamps info by handle.
 **
 ** Returns          None
 **
 *******************************************************************************/
void bt_acl_calc_timestamps_by_handle(uint16_t handle, uint8_t num);

/*******************************************************************************
 **
 ** Function         bt_acl_set_flush_occur_status
 **
 ** Description      set the f/w flush occur status.
 **
 ** Returns          None
 **
 *******************************************************************************/
void bt_acl_set_flush_occur_status(uint16_t handle, bool status);
