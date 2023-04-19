/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */
#define LOG_TAG "btif_ss_interface"
#include "btif_ss_interface.h"
#include "protobuf/proto/dm.pb.h"
#include "protobuf/include/proto_message_ids.h"
#include "osi/include/log.h"
#include "btif_api.h"
#include <base/bind.h>
#include <utils/Log.h>
#include <limits.h>

using namespace std;

#define MSG_EXIT_THREAD			1
#define MSG_WRITE_PROTO		  2


BluetoothSSTransport* gSSTransportCtrl = NULL;
BluetoothSSTransport* gSSTransportData = NULL;
BluetoothSSTransport* gSSTransportLeData = NULL;

thread_t* ctrl_tx_thread;
static base::MessageLoop* message_loop_ctrl_tx_ = NULL;
static base::RunLoop* run_loop_ctrl_tx_ = NULL;

thread_t* data_tx_thread;
static base::MessageLoop* message_loop_data_tx_ = NULL;
static base::RunLoop* run_loop_data_tx_ = NULL;

thread_t* le_data_tx_thread;
static base::MessageLoop* message_loop_le_data_tx_ = NULL;
static base::RunLoop* run_loop_le_data_tx_ = NULL;

ProfileCallbackMap gProfileCallbackMap;
thread_t* alarm_thread;
static base::MessageLoop* message_loop_alarm_ = NULL;
static base::RunLoop* run_loop_alarm_ = NULL;
thread_t* data_logging_thread;
static base::MessageLoop* message_loop_data_logging_ = NULL;
static base::RunLoop* run_loop_data_logging_ = NULL;

alarm_t *tx_thread_timeout;
alarm_t *rx_thread_timeout;

BluetoothSSInterface* BluetoothSSInterface::getInstance() {
    static BluetoothSSInterface instance;
    return &instance;
}

base::MessageLoop* get_ss_alarm_message_loop() { return message_loop_alarm_; }

static bt_status_t do_in_ctrl_tx_thread(const base::Location& from_here,
    const base::Closure& task) {
  if (!message_loop_ctrl_tx_ || !message_loop_ctrl_tx_->task_runner().get()) {
    ALOGI("%s: Dropped message, message_loop not initialized yet",__func__);
    return BT_STATUS_FAIL;
  }

  if (message_loop_ctrl_tx_->task_runner()->PostTask(from_here, task)) {
    return BT_STATUS_SUCCESS;
  }

  ALOGI("%s: Post task to task runner failed!",__func__);
  return BT_STATUS_FAIL;
}


static bt_status_t do_in_ctrl_tx_thread(const base::Closure& task) {
  return do_in_ctrl_tx_thread(FROM_HERE, task);
}

static void txThreadTimeout(void* data) {
  ALOGI("%s()", __func__);
  isTxTimeout = true;
}

static void rxThreadTimeout(void* data) {
  ALOGI("%s()", __func__);
  isRxTimeout = true;
}

static void run_message_loop_for_ctrl_tx(UNUSED_ATTR void* context) {
  ALOGI("run_message_loop_for_ctrl_tx started");

  message_loop_ctrl_tx_ = new base::MessageLoop(base::MessageLoop::Type::TYPE_DEFAULT);
  run_loop_ctrl_tx_ = new base::RunLoop();

  run_loop_ctrl_tx_->Run();

  delete message_loop_ctrl_tx_;
  message_loop_ctrl_tx_ = NULL;

  delete run_loop_ctrl_tx_;
  run_loop_ctrl_tx_ = NULL;

  ALOGI("run_message_loop_for_ctrl_tx finished");
}

static bt_status_t do_in_data_tx_thread(const base::Location& from_here,
    const base::Closure& task) {
  if (!message_loop_data_tx_ || !message_loop_data_tx_->task_runner().get()) {
    ALOGI("%s: Dropped message, message_loop not initialized yet",__func__);
    return BT_STATUS_FAIL;
  }

  if (message_loop_data_tx_->task_runner()->PostTask(from_here, task)) {
    return BT_STATUS_SUCCESS;
  }

  ALOGI("%s: Post task to task runner failed!",__func__);
  return BT_STATUS_FAIL;
}


static bt_status_t do_in_data_tx_thread(const base::Closure& task) {
  return do_in_data_tx_thread(FROM_HERE, task);
}


static void run_message_loop_for_data_tx(UNUSED_ATTR void* context) {
  ALOGI("run_message_loop_for_data_tx started");

  message_loop_data_tx_ = new base::MessageLoop(base::MessageLoop::Type::TYPE_DEFAULT);
  run_loop_data_tx_ = new base::RunLoop();

  run_loop_data_tx_->Run();

  delete message_loop_data_tx_;
  message_loop_data_tx_ = NULL;

  delete run_loop_data_tx_;
  run_loop_data_tx_ = NULL;

  ALOGI("run_message_loop_for_data_tx finished");
}

static bt_status_t do_in_le_data_tx_thread(const base::Location& from_here,
    const base::Closure& task) {
  if (!message_loop_le_data_tx_ || !message_loop_le_data_tx_->task_runner().get()) {
    ALOGI("%s: Dropped message, message_loop not initialized yet",__func__);
    return BT_STATUS_FAIL;
  }

  if (message_loop_le_data_tx_->task_runner()->PostTask(from_here, task)) {
    return BT_STATUS_SUCCESS;
  }

  ALOGI("%s: Post task to task runner failed!",__func__);
  return BT_STATUS_FAIL;
}


static bt_status_t do_in_le_data_tx_thread(const base::Closure& task) {
  return do_in_le_data_tx_thread(FROM_HERE, task);
}


static void run_message_loop_for_le_data_tx(UNUSED_ATTR void* context) {
  ALOGI("run_message_loop_for_le_data_tx started");

  message_loop_le_data_tx_ = new base::MessageLoop(base::MessageLoop::Type::TYPE_DEFAULT);
  run_loop_le_data_tx_ = new base::RunLoop();

  run_loop_le_data_tx_->Run();

  delete message_loop_le_data_tx_;
  message_loop_le_data_tx_ = NULL;

  delete run_loop_le_data_tx_;
  run_loop_le_data_tx_ = NULL;

  ALOGI("run_message_loop_for_le_data_tx finished");
}

static void run_message_loop_for_alarm(UNUSED_ATTR void* context) {
  ALOGI("run_message_loop_for_alarm started");

  message_loop_alarm_ = new base::MessageLoop(base::MessageLoop::Type::TYPE_DEFAULT);
  run_loop_alarm_ = new base::RunLoop();

  run_loop_alarm_->Run();

  delete message_loop_alarm_;
  message_loop_alarm_ = NULL;

  delete run_loop_alarm_;
  run_loop_alarm_ = NULL;

  ALOGI("run_message_loop_for_alarm finished");
}

static void run_message_loop_for_data_logging(UNUSED_ATTR void* context) {
  ALOGI("run_message_loop_for_data_logging started");

  message_loop_data_logging_ = new base::MessageLoop(base::MessageLoop::Type::TYPE_DEFAULT);
  run_loop_data_logging_ = new base::RunLoop();

  run_loop_data_logging_->Run();

  delete message_loop_data_logging_;
  message_loop_data_logging_ = NULL;

  delete run_loop_data_logging_;
  run_loop_data_logging_ = NULL;

  ALOGI("run_message_loop_for_data_logging finished");
}


static bt_status_t do_in_data_logging_thread(const base::Location& from_here,
    const base::Closure& task) {
  if (!message_loop_data_logging_ || !message_loop_data_logging_->task_runner().get()) {
    ALOGI("%s: Dropped message, message_loop not initialized yet",__func__);
    return BT_STATUS_FAIL;
  }

  if (message_loop_data_logging_->task_runner()->PostTask(from_here, task)) {
    return BT_STATUS_SUCCESS;
  }

  ALOGI("%s: Post task to task runner failed!",__func__);
  return BT_STATUS_FAIL;
}


static bt_status_t do_in_data_logging_thread(const base::Closure& task) {
  return do_in_data_logging_thread(FROM_HERE, task);
}
BluetoothSSInterface::BluetoothSSInterface() {
    ALOGI("BluetoothSSInterface constructor");
    gSSTransportCtrl = new BluetoothSSTransport();
    ALOGI("BluetoothSSInterface calling open for control channel");
    int rsltfd = gSSTransportCtrl->open(BT_SS_CTRL_CH);
    ALOGI("BluetoothSSInterface finish open for ctrl rstlfd is :: %d",rsltfd);
    if (rsltfd <= 0) {
        ALOGI("open failed");
    } else {
        running_ = true;
    }
    gSSTransportData = new BluetoothSSTransport();
    ALOGI("BluetoothSSInterface calling open for data channel");
    rsltfd = gSSTransportData->open(BT_SS_DATA_CH);
    ALOGI("BluetoothSSInterface finish open for data rstlfd is :: %d",rsltfd);
    if (rsltfd <= 0) {
        ALOGI("open failed");
    } else {
        running_data_ch_ = true;
    }
    gSSTransportLeData = new BluetoothSSTransport();
    ALOGI("BluetoothSSInterface calling open for le data channel");
    rsltfd = gSSTransportLeData->open(BT_SS_LE_DATA_CH);
    ALOGI("BluetoothSSInterface finish open for le data rstlfd is :: %d",rsltfd);
    if (rsltfd <= 0) {
        ALOGI("open failed");
    } else {
        running_le_data_ch_ = true;
    }
    isTxTimeout = false;
    isRxTimeout = false;
    //message loop for alarm
    alarm_thread = thread_new_sized("alarm_thread", 1024);
    thread_post(alarm_thread, run_message_loop_for_alarm, nullptr);

    tx_thread_timeout = alarm_new("glink_ctrl_tx_timeout_alarm");
    rx_thread_timeout = alarm_new("glink_ctrl_rx_timeout_alarm");

    //threads for ctrl channel
    if(!rx_thread){
        rx_thread = std::unique_ptr<std::thread>(new std::thread(&BluetoothSSInterface::processRx, this));
        alarm_set_on_mloop(rx_thread_timeout, GLINK_TX_RX_ALARM_TIMEOUT,
                    rxThreadTimeout, NULL);
    }
    ctrl_tx_thread = thread_new_sized("ctrl_tx_thread", 1024);
    thread_post(ctrl_tx_thread, run_message_loop_for_ctrl_tx, nullptr);
    alarm_set_on_mloop(tx_thread_timeout, GLINK_TX_RX_ALARM_TIMEOUT,
                    txThreadTimeout, NULL);

    //threads for data channel
    if(!data_ch_rx_thread){
        data_ch_rx_thread = std::unique_ptr<std::thread>(new std::thread(&BluetoothSSInterface::processDataChRx, this));
    }
    data_tx_thread = thread_new_sized("data_tx_thread", 1024);
    thread_post(data_tx_thread, run_message_loop_for_data_tx, nullptr);

    //threads for le data channel
    if(!le_data_ch_rx_thread){
        le_data_ch_rx_thread = std::unique_ptr<std::thread>(new std::thread(&BluetoothSSInterface::processLeDataChRx, this));
    }
    le_data_tx_thread = thread_new_sized("le_data_tx_thread", 1024);
    thread_post(le_data_tx_thread, run_message_loop_for_le_data_tx, nullptr);

    gProfileCallbackMap.clear();//Clearing profile callback map
    //threads for GLINK data logging in File
    data_logging_thread = thread_new_sized("data_logging_thread", 1024);
    thread_post(data_logging_thread, run_message_loop_for_data_logging, nullptr);
}

BluetoothSSInterface::~BluetoothSSInterface() {
    ALOGI("BluetoothSSInterface destructor");
    //for ctrl ch
    if (run_loop_ctrl_tx_ && message_loop_ctrl_tx_) {
      message_loop_ctrl_tx_->task_runner()->PostTask(FROM_HERE,
          run_loop_ctrl_tx_->QuitClosure());
    }
    gSSTransportCtrl->close();
    running_ = false;
    delete gSSTransportCtrl;
    gSSTransportCtrl = NULL;
    //for data ch
    if (run_loop_data_tx_ && message_loop_data_tx_) {
      message_loop_data_tx_->task_runner()->PostTask(FROM_HERE,
          run_loop_data_tx_->QuitClosure());
    }
    gSSTransportData->close();
    running_data_ch_ = false;
    delete gSSTransportData;
    gSSTransportData = NULL;
    //for le data ch
    if (run_loop_le_data_tx_ && message_loop_le_data_tx_) {
      message_loop_le_data_tx_->task_runner()->PostTask(FROM_HERE,
          run_loop_le_data_tx_->QuitClosure());
    }
    gSSTransportLeData->close();
    running_le_data_ch_ = false;
    delete gSSTransportLeData;
    gSSTransportLeData = NULL;

    if (run_loop_alarm_ && message_loop_alarm_) {
      message_loop_alarm_->task_runner()->PostTask(FROM_HERE,
          run_loop_alarm_->QuitClosure());
    }
    gProfileCallbackMap.clear();//Clearing profile callback map
    if (alarm_is_scheduled(rx_thread_timeout)) {
      ALOGI("%s(): rx_thread_timeout() scheduled", __func__);
      alarm_cancel(rx_thread_timeout);
      alarm_free(rx_thread_timeout);
    } else {
      ALOGI("%s(): rx_thread_timeout() is not scheduled", __func__);
    }
    if (alarm_is_scheduled(tx_thread_timeout)) {
      ALOGI("%s(): tx_thread_timeout() scheduled", __func__);
      alarm_cancel(tx_thread_timeout);
      alarm_free(tx_thread_timeout);
    } else {
      ALOGI("%s(): tx_thread_timeout() is not scheduled", __func__);
    }
    if (run_loop_data_logging_ && message_loop_data_logging_) {
      message_loop_data_logging_->task_runner()->PostTask(FROM_HERE,
          run_loop_data_logging_->QuitClosure());
    }
}

void processDataLogging(uint8_t *msgStr, size_t buflen, const char *msgtyp) {
    gSSTransportCtrl->file_write(msgStr,buflen,msgtyp);
}
void processTx(std::string msgStr) {
  ALOGI("%s: msg : %s and length: %d", __func__, msgStr.c_str(), msgStr.length());
  const  char *msgType="Tx";
  uint8_t *tmpBuf = (uint8_t*)msgStr.c_str();
  size_t bytes_written = 0;
  if (alarm_is_scheduled(tx_thread_timeout)) {
    ALOGI("%s(): tx_thread_timeout() scheduled", __func__);
    alarm_cancel(tx_thread_timeout);
  }
  alarm_set_on_mloop(tx_thread_timeout, GLINK_TX_RX_ALARM_TIMEOUT,
      txThreadTimeout, NULL);
  if (log_level >= SS_BT_TRACE_LEVEL_GLINK) {
    do_in_data_logging_thread(base::Bind(processDataLogging, tmpBuf,msgStr.length(),msgType));
  }
  gSSTransportCtrl->write(tmpBuf,msgStr.length(),&bytes_written);
  ALOGI("%s: CTRL_CH: write payload bytes_written=%d", __func__, (int)bytes_written);
}

int processDataTx(std::string msgStr) {
  ALOGI("%s: msg : %s and length: %d", __func__, msgStr.c_str(), msgStr.length());
  uint8_t *tmpBuf = (uint8_t*)msgStr.c_str();
  size_t bytes_written = 0;
  int result = -1;
  int retry_count = 0;
  do {
        result = gSSTransportData->write(tmpBuf,msgStr.length(),&bytes_written);
        if(result == 0){
          ALOGI("%s: Glink write success",__func__);
          break;
        }else if(result == -1){
          retry_count++;
          ALOGI("%s: Glink write failure...retrying...retry count is :: %d",__func__,retry_count);
          switch (retry_count) {
            case 1 ... 4: {
              break;
            }
            case 5 ... 7: {
              usleep(10000);
              break;
            }
            case 8 ... INT_MAX:{
              usleep(25000);
              break;
            }
         }
         continue;
        }else{
          ALOGI("%s: Glink write failure status unknown",__func__);
          break;
        }
  }while(true);
  return bytes_written;
}

int processLeDataTx(std::string msgStr) {
  ALOGI("%s: msg : %s and length: %d", __func__, msgStr.c_str(), msgStr.length());
  const  char *msgType="Tx";
  uint8_t *tmpBuf = (uint8_t*)msgStr.c_str();
  size_t bytes_written = 0;
  if (log_level >= SS_BT_TRACE_LEVEL_GLINK) {
    do_in_data_logging_thread(base::Bind(processDataLogging, tmpBuf,msgStr.length(),msgType));
  }
  int result = -1;
  int retry_count = 0;
  do {
        result = gSSTransportLeData->write(tmpBuf,msgStr.length(),&bytes_written);
        if(result == 0){
          ALOGI("%s: Glink write success",__func__);
          break;
        }else if(result == -1){
          retry_count++;
          ALOGI("%s: Glink write failure...retrying...retry count is :: %d",__func__,retry_count);
          if(retry_count > 3){
            usleep(50000);
          }
          continue;
        }else{
          ALOGI("%s: Glink write failure status unknown",__func__);
          break;
        }
  }while(true);
  ALOGI("%s: LE_DATA_CH: write payload bytes_written=%d", __func__, (int)bytes_written);
  return bytes_written;
}

void BluetoothSSInterface::postTxMsg(std::string msgStr) {
  ALOGI("postTxMsg with msg : %s and length %d", msgStr.c_str(), msgStr.length());
  if (msgStr.length() > MSG_SIZE_MAX) {
    ALOGE("ERROR:: Application trying to write the data more than the size configured for GLINK. PROTO SIZE: %d", msgStr.length());
    return;
  }
  do_in_ctrl_tx_thread(base::Bind(processTx, msgStr));
}

int BluetoothSSInterface::postDataChTxMsg(std::string msgStr) {
  ALOGI("postDataChTxMsg with msg : %s and length %d", msgStr.c_str(), msgStr.length());
  //do_in_data_tx_thread(base::Bind(processDataTx, msgStr));
  return processDataTx(msgStr);
}

int BluetoothSSInterface::postLeDataChTxMsg(std::string msgStr) {
  ALOGI("postLeDataChTxMsg with msg : %s and length %d", msgStr.c_str(), msgStr.length());
  //do_in_data_tx_thread(base::Bind(processLeDataTx, msgStr));
  return processLeDataTx(msgStr);
}

void BluetoothSSInterface::registerCallbacks(const char* profile_id, ss_profile_callback profile_cb) {
    ALOGD("registerCallbacks profile ID: %s",profile_id);
    std::pair<std::map<const char*,ss_profile_callback>::iterator,bool> status;
    status = gProfileCallbackMap.insert(std::pair<const char*,ss_profile_callback>(profile_id, profile_cb));
    if (status.second==false) {
        ALOGE("profile ID already inserted: %s",profile_id);
    }
}

void BluetoothSSInterface::deregisterCallbacks(const char* profile_id) {
    ALOGD("deregisterCallbacks profile ID: %s",profile_id);
    std::map<const char*,ss_profile_callback>::iterator it;
    it=gProfileCallbackMap.find(profile_id);
    if (it != gProfileCallbackMap.end()) {
        ALOGD("%s deregistered profile ID: %s",__func__, profile_id);
        gProfileCallbackMap.erase(it);
    } else {
        ALOGE("%s: profile ID not found in ProfileCallbackMap: %s",__func__, profile_id);
    }
}

void BluetoothSSInterface::processRx() {
    ALOGD("BluetoothSSInterface processRx :: running_ is :: %d",running_);
    uint8_t *readBuffer = (uint8_t *)malloc(MSG_SIZE_MAX*sizeof(uint8_t));
    const char *msgType="Rx";
    if (readBuffer == NULL) {
      ALOGE("%s: readBuffer malloc failed",__func__);
      return;
    }
    while (running_) {
        int rcPoll = gSSTransportCtrl->poll(-1);
        if (-1 == rcPoll) {
            ALOGI("Poll Failure");
        }
        if (alarm_is_scheduled(rx_thread_timeout)) {
          ALOGI("%s(): rx_thread_timeout() scheduled", __func__);
          alarm_cancel(rx_thread_timeout);
        }
        alarm_set_on_mloop(rx_thread_timeout, GLINK_TX_RX_ALARM_TIMEOUT,
            rxThreadTimeout, NULL);
        int num = gSSTransportCtrl->read(readBuffer, MSG_SIZE_MAX*sizeof(uint8_t));
        if (log_level >= SS_BT_TRACE_LEVEL_GLINK) {
          do_in_data_logging_thread(base::Bind(processDataLogging, readBuffer, num, msgType));
        }
        ALOGI("num of bytes read from stream is :: %d",num);
        if(num < MSG_SIZE_MIN) {
            ALOGE("Slate response is too short ::  %d",num);
        }else {
            uint16_t MSG_ID = readBuffer[0] + (((int)(readBuffer[1]))<<8);
            tBTIF_SS_Cback ss_cback;
            memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
            ss_cback.payload = (uint8_t *)malloc(num*sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
            if (ss_cback.payload == NULL) {
              ALOGE("%s: payload malloc failed",__func__);
              continue;
            }
            memcpy(ss_cback.payload, readBuffer, (num * sizeof(uint8_t)) );
            parseRxData(MSG_ID, ss_cback);
        }
    }
    free(readBuffer);
}

void BluetoothSSInterface::processDataChRx() {
    ALOGE("BluetoothSSInterface processRx :: running_data_ch_ is :: %d",running_data_ch_);
    uint8_t *readBuffer = (uint8_t *)malloc(MSG_SIZE_MAX*sizeof(uint8_t));
    if (readBuffer == NULL) {
      ALOGE("%s: readBuffer malloc failed",__func__);
      return;
    }
    while (running_data_ch_) {
        int rcPoll = gSSTransportData->poll(-1);
        if (-1 == rcPoll) {
            ALOGI("Poll Failure");
        }
        int num = gSSTransportData->read(readBuffer, MSG_SIZE_MAX*sizeof(uint8_t));
        ALOGI("num of bytes read from stream is :: %d",num);
        if(num < MSG_SIZE_MIN) {
            ALOGE("Slate response is too short ::  %d",num);
        } else {
            uint16_t MSG_ID = readBuffer[0] + (((int)(readBuffer[1]))<<8);
            uint16_t length = readBuffer[2] + (((int)(readBuffer[3]))<<8);
            if (length > (num - MSG_PROTO_OFFSET)) {
              ALOGE("Length is greater than the buffer received. Buffer Size: %d and length: %d",num, length);
              continue;
            }
            tBTIF_SS_Cback ss_cback;
            memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
            ss_cback.payload = (uint8_t *)malloc(num*sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
            if (ss_cback.payload == NULL) {
              ALOGE("%s: payload malloc failed",__func__);
              continue;
            }
            memcpy(ss_cback.payload, readBuffer, (num * sizeof(uint8_t)) );
            parseRxData(MSG_ID, ss_cback);
        }
    }
    free(readBuffer);
}

void BluetoothSSInterface::processLeDataChRx() {
    ALOGE("BluetoothSSInterface processRx :: running_le_data_ch_ is :: %d",running_data_ch_);
    uint8_t *readBuffer = (uint8_t *)malloc(MSG_SIZE_MAX*sizeof(uint8_t));
    if (readBuffer == NULL) {
      ALOGE("%s: readBuffer malloc failed",__func__);
      return;
    }
    while (running_le_data_ch_) {
        int rcPoll = gSSTransportLeData->poll(-1);
        if (-1 == rcPoll) {
            ALOGI("Poll Failure");
        }
        int num = gSSTransportLeData->read(readBuffer, MSG_SIZE_MAX*sizeof(uint8_t));
        ALOGI("num of bytes read from stream is :: %d",num);
        if(num < MSG_SIZE_MIN) {
            ALOGE("Slate response is too short ::  %d",num);
        } else {
            uint16_t MSG_ID = readBuffer[0] + (((int)(readBuffer[1]))<<8);
            uint16_t length = readBuffer[2] + (((int)(readBuffer[3]))<<8);
            if (length > (num - MSG_PROTO_OFFSET)) {
              ALOGE("Length is greater than the buffer received. Buffer Size: %d and length: %d",num, length);
              continue;
            }
            tBTIF_SS_Cback ss_cback;
            memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
            ss_cback.payload = (uint8_t *)malloc(num*sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
            if (ss_cback.payload == NULL) {
              ALOGE("%s: payload malloc failed",__func__);
              continue;
            }
            memcpy(ss_cback.payload, readBuffer, (num * sizeof(uint8_t)) );
            parseRxData(MSG_ID, ss_cback);
        }
    }
    free(readBuffer);
}

void BluetoothSSInterface::parseRxData(int msg_id, tBTIF_SS_Cback ss_cback) {
    ALOGI("parseRxData msg_id is :: %X",msg_id);
    switch (msg_id) {
        case BT_DM_EVT_START ... BT_DM_EVT_MAX: {
          auto it = gProfileCallbackMap.find(BT_PROFILE_DM_ID);
          if (it != gProfileCallbackMap.end()) {
            ALOGI("%s: Sending callback to DM", __func__);
            btif_transfer_context(it->second, msg_id, (char*)&ss_cback, sizeof(ss_cback),NULL);
          } else {
            ALOGE("%s: callback not registered for DM", __func__);
          }
          break;
        }
        case BT_SDP_EVT_START ... BT_SDP_EVT_MAX: {
          auto it = gProfileCallbackMap.find(BT_PROFILE_SDP_CLIENT_ID);
          if (it != gProfileCallbackMap.end()) {
            ALOGI("%s: Sending callback to SDP", __func__);
            btif_transfer_context(it->second, msg_id, (char*)&ss_cback, sizeof(ss_cback),NULL);
          } else {
            ALOGE("%s: callback not registered for SDP", __func__);
          }
          break;
        }
        case BT_RFCOMM_EVT_START ... BT_RFCOMM_EVT_MAX: {
          auto it = gProfileCallbackMap.find(BT_PROFILE_SOCKETS_ID);
          if (it != gProfileCallbackMap.end()) {
            ALOGI("%s: Sending callback to RFCOMM", __func__);
            btif_transfer_context(it->second, msg_id, (char*)&ss_cback, sizeof(ss_cback),NULL);
          } else {
            ALOGE("%s: callback not registered for RFCOMM", __func__);
          }
          break;
        }
        case BT_GATT_EVT_START ... BT_GATT_EVT_MAX: {
          auto it = gProfileCallbackMap.find(BT_PROFILE_GATT_ID);
          if (it != gProfileCallbackMap.end()) {
            ALOGI("%s: Sending callback to GATT", __func__);
            btif_transfer_context(it->second, msg_id, (char*)&ss_cback, sizeof(ss_cback),NULL);
          } else {
            ALOGE("%s: callback not registered for GATT", __func__);
          }
          break;
        }
        case BT_AV_EVT_START ... BT_AV_EVT_MAX: {
          auto it = gProfileCallbackMap.find(BT_PROFILE_ADVANCED_AUDIO_ID);
          if (it != gProfileCallbackMap.end()) {
            ALOGI("%s: Sending callback to AV", __func__);
            btif_transfer_context(it->second, msg_id, (char*)&ss_cback, sizeof(ss_cback),NULL);
          } else {
            ALOGE("%s: callback not registered for AV", __func__);
          }
          break;
        }
        case BT_AVRCP_EVT_START ... BT_AVRCP_EVT_MAX: {
          auto it = gProfileCallbackMap.find(BT_PROFILE_AV_RC_ID);
          if (it != gProfileCallbackMap.end()) {
            ALOGI("%s: Sending callback to AVRCP", __func__);
            btif_transfer_context(it->second, msg_id, (char*)&ss_cback, sizeof(ss_cback),NULL);
          } else {
            ALOGE("%s: callback not registered for AVRCP", __func__);
          }
          break;
        }
        case BT_HFP_EVT_START ... BT_HFP_EVT_MAX: {
          auto it = gProfileCallbackMap.find(BT_PROFILE_HANDSFREE_CLIENT_ID);
          if (it != gProfileCallbackMap.end()) {
            ALOGI("%s: Sending callback to BT_PROFILE_HANDSFREE_CLIENT_ID", __func__);
            btif_transfer_context(it->second, msg_id, (char*)&ss_cback, sizeof(ss_cback),NULL);
          } else {
            ALOGE("%s: callback not registered for BT_PROFILE_HANDSFREE_CLIENT_ID", __func__);
          }
          break;
        }
        case BT_HFAG_EVT_START ... BT_HFAG_EVT_MAX: {
          auto it = gProfileCallbackMap.find(BT_PROFILE_HANDSFREE_ID);
          if (it != gProfileCallbackMap.end()) {
            ALOGI("%s: Sending callback to BT_PROFILE_HANDSFREE_ID", __func__);
            btif_transfer_context(it->second, msg_id, (char*)&ss_cback, sizeof(ss_cback),NULL);
          } else {
            ALOGE("%s: callback not registered for BT_PROFILE_HANDSFREE_ID", __func__);
          }
          break;
        }
        default:
            ALOGI("msg_id : %d Not matching with any group",msg_id);
            if (ss_cback.payload != NULL) {
              free(ss_cback.payload);
              ss_cback.payload = NULL;
            }
        break;
    }
}
