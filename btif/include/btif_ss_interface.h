#ifndef __BTIF_SS_INTERFCAE__
#define __BTIF_SS_INTERFCAE__
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */
#include<string>
#include "btif_ss_transport.h"
#include <thread>
#include <queue>
#include <map>
#include <mutex>
#include <memory>
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
#define MSG_SIZE_MAX 1280
#define MSG_PROTO_OFFSET 6
#define GLINK_TX_RX_ALARM_TIMEOUT 1000

static bool isTxTimeout;
static bool isRxTimeout;

struct TxData
{
  std::string msg;
};

base::MessageLoop* get_ss_alarm_message_loop();

struct ThreadMsg;

typedef struct SsCb
{
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
    int postDataChTxMsg(std::string msgStr);
    void registerCallbacks(const char* profile_id, ss_profile_callback profile_cb);
    void deregisterCallbacks(const char* profile_id);
private:
    BluetoothSSInterface();
    ~BluetoothSSInterface();
    void parseRxData(int msg_id, tBTIF_SS_Cback ssCback);

    //for ctrl channel
    std::unique_ptr<std::thread> rx_thread;
    void processRx();
    bool running_;

    //for data channel
    std::unique_ptr<std::thread> data_ch_rx_thread;
    void processDataChRx();
    bool running_data_ch_;

};
#endif //__BTIF_SS_INTERFCAE__
