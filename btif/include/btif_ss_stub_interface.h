/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */
#ifndef __BTIF_SS_STUB_INTERFCAE__
#define __BTIF_SS_STUB_INTERFCAE__

#ifdef SS_STUB_ENABLED

#include<string>
#include <thread>
#include <queue>
#include <mutex>
#include "btif_ss_interface.h"
#include <condition_variable>

#define BT_ENABLE_STATUS_SUCCESS 1
#define BT_ENABLE_STATUS_FAILURE 0

class BluetoothSSStubInterface {
public:
    BluetoothSSStubInterface();
    ~BluetoothSSStubInterface();
    static BluetoothSSStubInterface* getInstance();
    void postTxMsg(std::string msgStr);
private:
    BluetoothSSStubInterface(const BluetoothSSStubInterface&) = delete;
    BluetoothSSStubInterface& operator=(const BluetoothSSStubInterface&) = delete;
    static BluetoothSSStubInterface* instance;
    bool running_;
    std::unique_ptr<std::thread> rx_thread;
    std::unique_ptr<std::thread> tx_thread;
    std::queue<std::shared_ptr<ThreadMsg>> tx_queue;
    std::queue<std::shared_ptr<ThreadMsg>> rx_queue;
    std::mutex tx_mutex;
    std::condition_variable tx_cv;
    std::mutex rx_mutex;
    std::condition_variable rx_cv;
    void processTx();
    void exitTxThread();

    void processRx();
    void sendDummyCallback(int msg_id,std::string res_buffer);
    void sendDummydeviceCallback();
    void sendDummyAdapterPropCallback();
    void PrintEnBytes(const char* en_char, uint8_t len);
    int FormRxPacket(uint16_t msg_id, uint16_t proto_enc, uint16_t encode_len, std::string& encoded_bytes);

};
#endif //SS_STUB_ENABLED
#endif //__BTIF_SS_STUB_INTERFCAE__
