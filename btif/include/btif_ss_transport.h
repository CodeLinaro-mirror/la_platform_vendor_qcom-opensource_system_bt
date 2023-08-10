/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

#ifndef __BTIF_SS_TRANSPORT__
#define __BTIF_SS_TRANSPORT__

#include <string>

// TG: TODO: Needs to be added in device tree
#define BT_SS_CTRL_CH "/dev/glink_pkt_ss_bt_ctrl"
#define BT_SS_DATA_CH "/dev/glink_pkt_ss_bt_data"
#define BT_SS_LE_DATA_CH "/dev/glink_pkt_ss_bt_le_data"
#define BT_SS_SSR_DATA_CH "/dev/glink_pkt_ss_bt_ssr_data"

#define BT_DATA_PATH "/data/misc/bluetooth/glink_data.txt"
#define CASE_RETURN_STR(const) \
  case const:                  \
    return #const;

const char* dump_search_dm_msgID(uint16_t msgID);
const char* dump_search_sdp_msgID(uint16_t msgID);
const char* dump_search_rfcomm_msgID(uint16_t msgID);
const char* dump_search_gattc_msgID(uint16_t msgID);
const char* dump_search_advertiser_msgID(uint16_t msgID);

class BluetoothSSTransport {
public:
    BluetoothSSTransport();
    ~BluetoothSSTransport();
    int open(const std::string& dev);
    int close();
    int queueRxIntent(uint32_t size, uint32_t num_rx_intents);

    // timeout in seconds, negative value means infinite timeout
    int poll(int timeout);
    int read(uint8_t *data, size_t size);
    int write(uint8_t *buf, size_t buflen, size_t *bytes_written);

    //File write
    int file_write(uint8_t *buf, size_t buflen, const char *msgtyp);
    bool fileHeader=true;
    FILE *fptr=fopen(BT_DATA_PATH,"w+");
    struct timeval  now;
    struct tm*      localTime;

private:
    BluetoothSSTransport(const BluetoothSSTransport&) = delete;
    BluetoothSSTransport& operator=(const BluetoothSSTransport&) = delete;
    static BluetoothSSTransport* instance;
    static const uint32_t MAX_CONN_RETRIES = 3;
    std::string mDeviceName;
    int mFd;
};

class Wakelock {
public:
    Wakelock(const std::string& lockStr);
    ~Wakelock();
private:
    void wakeLockAcquireOrRelease (bool lockRequest);
    const std::string mLockStr;
};
#endif //__BTIF_SS_TRANSPORT__
