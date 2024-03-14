/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

#ifndef __BTIF_SS_INTERFCAE__
#define __BTIF_SS_INTERFCAE__

#include<string>
#include "btif_ss_transport.h"
#include <thread>
#include <queue>
#include <map>
#include <mutex>
#include <memory>
#include <errno.h>
#include <pthread.h>
#include <condition_variable>
#include <hardware/bluetooth.h>
#include "btif_common.h"
#include <base/bind.h>
#include <base/run_loop.h>
#include <base/threading/thread.h>
#include "osi/include/thread.h"
#include "osi/include/alarm.h"

#define BT_ENABLE_STATUS_SUCCESS 1
#define BT_ENABLE_STATUS_FAILURE 0
#define MSG_SIZE_MIN 8
#define MSG_SIZE_MAX 4096
#define SSR_CH_MAX_SIZE 8192
#define SSR_CH_MIN_SIZE 4
#define MSG_PROTO_OFFSET 6
#define GLINK_IDLE_TIMEOUT 1100
#define GLINK_SSR_DUMP_RX_ALARM_TIMEOUT 1000

#define WAKE_LOCK_FILE    "/sys/power/wake_lock"
#define WAKE_UNLOCK_FILE  "/sys/power/wake_unlock"
#define SS_GLINK_LOCK_STR "ss_glink_lock"
#define SSR_DUMP_EXTN ".bin"
#define SS_LOGS_TS           "%.04d-%.02d-%.02d_%.02d-%.02d-%.02d"
#define BT_SSR_DATA_PATH "/data/misc/bluetooth/ramdump_bt_fw_crashdump_"
#define SS_SSR_DUMP_PATH BT_SSR_DATA_PATH SS_LOGS_TS SSR_DUMP_EXTN
#define SS_SSR_DUMP_PATH_WITHOUT_TIME BT_SSR_DATA_PATH SSR_DUMP_EXTN
#define SS_SOC_DUMP_PATH_BUF_SIZE 255
static int total_dump_size;

static bool isTxTimeout;
static bool isRxTimeout;
static bool isWakelockAcquired;
static bool isScanlockAcquired;
static FILE *ssr_fptr = NULL;

struct TxData
{
  std::string msg;
};

base::MessageLoop* get_ss_alarm_message_loop();

struct ThreadMsg;

typedef struct SsCb
{
  uint16_t num_bytes;
  uint8_t *payload;
}tBTIF_SS_Cback;
typedef std::map<const char*, ss_profile_callback> ProfileCallbackMap;

class BluetoothSSInterface {
public:
    static BluetoothSSInterface* getInstance();
    void encodeEnableMsg(std::string& msgStr);
    void encodeDisableMsg(std::string& msgStr);
    void encodeStartDiscoveryMsg(std::string& msgStr);
    void encodeCancelDiscoveryMsg(std::string& msgStr);
    void postTxMsg(std::string msgStr);
    int postDataChTxMsg(std::string msgStr, int fd);
    int postLeDataChTxMsg(std::string msgStr);
    int postObexDataChTxMsg(std::string msgStr);
    void registerCallbacks(const char* profile_id, ss_profile_callback profile_cb);
    void deregisterCallbacks(const char* profile_id);
    //api to acquire or release glink wakelock
    static void ssGlinkWakeLockAcquireOrRelease(bool isScanOrInquiry, bool lockRequest);
    void cleanup();
private:
    BluetoothSSInterface();
    void parseRxData(int msg_id, tBTIF_SS_Cback ssCback);
    void processBatchMsg(uint8_t buffer[]);

    //for ctrl channel
    std::unique_ptr<std::thread> rx_thread;
    void processRx();
    bool running_ctrl_ch_;

    //for data channel
    std::unique_ptr<std::thread> data_ch_rx_thread;
    void processDataChRx();
    bool running_data_ch_;

    //for le data channel
    std::unique_ptr<std::thread> le_data_ch_rx_thread;
    void processLeDataChRx();
    bool running_le_data_ch_;

    //for obex data channel
    std::unique_ptr<std::thread> obex_data_ch_rx_thread;
    void processObexDataChRx();
    bool running_obex_data_ch_;

    //for ssr data channel
    std::unique_ptr<std::thread> ssr_data_ch_rx_thread;
    void processSsrDataChRx();
    bool running_ssr_data_ch_;
protected:
    ~BluetoothSSInterface();
};
#endif //__BTIF_SS_INTERFCAE__
