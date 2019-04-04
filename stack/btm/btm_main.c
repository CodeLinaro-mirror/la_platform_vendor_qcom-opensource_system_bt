/******************************************************************************
 *
 *  Copyright (C) 2002-2012 Broadcom Corporation
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
 *  This file contains the definition of the btm control block when
 *  BTM_DYNAMIC_MEMORY is used.
 *
 ******************************************************************************/

#include "bt_types.h"
#include "bt_target.h"
#include <string.h>
#include "btm_int.h"

/* Global BTM control block structure
*/
#if BTM_DYNAMIC_MEMORY == FALSE
tBTM_CB  btm_cb;
#endif

/*******************************************************************************
**
** Function         btm_init
**
** Description      This function is called at BTM startup to allocate the
**                  control block (if using dynamic memory), and initializes the
**                  tracing level.  It then initializes the various components of
**                  btm.
**
** Returns          void
**
*******************************************************************************/
void btm_init (void)
{
    /* All fields are cleared; nonzero fields are reinitialized in appropriate function */
    memset(&btm_cb, 0, sizeof(tBTM_CB));

#if defined(BTM_INITIAL_TRACE_LEVEL)
    btm_cb.trace_level = BTM_INITIAL_TRACE_LEVEL;
#else
    btm_cb.trace_level = BT_TRACE_LEVEL_NONE;    /* No traces */
#endif
    /* Initialize BTM component structures */
    btm_inq_db_init();                  /* Inquiry Database and Structures */
    btm_acl_init();                     /* ACL Database and Structures */
    btm_sec_init(BTM_SEC_MODE_SP);      /* Security Manager Database and Structures */
#if BTM_SCO_INCLUDED == TRUE
    btm_sco_init();                     /* SCO Database and Structures (If included) */
#endif

    btm_dev_init();                     /* Device Manager Structures & HCI_Reset */
}



void btm_free (void)
{
    BT_HDR  *p_buf;
    tBTM_INQUIRY_VAR_ST *p_inq = &btm_cb.btm_inq_vars;

    while((p_buf = (BT_HDR *)GKI_dequeue (&btm_cb.page_queue)) != NULL)
        GKI_freebuf((void *)p_buf);

    while((p_buf = (BT_HDR *)GKI_dequeue (&btm_cb.sec_pending_q)) != NULL)
        GKI_freebuf((void *)p_buf);

    if (p_inq->p_bd_db)
    {
        GKI_freebuf(p_inq->p_bd_db);
        p_inq->p_bd_db = NULL;
    }

#if BTM_SCO_HCI_INCLUDED == TRUE
    {
        int i;
        for (i = 0; i < BTM_MAX_SCO_LINKS; i++) {
            while((p_buf = (BT_HDR *)GKI_dequeue (&btm_cb.sco_cb.sco_db[i].xmit_data_q)) != NULL)
                GKI_freebuf((void *)p_buf);
        }
    }
#endif
}
