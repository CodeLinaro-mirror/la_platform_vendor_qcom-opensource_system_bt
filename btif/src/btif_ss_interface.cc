/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

#define LOG_TAG "btif_ss_interface"
#include "btif_ss_interface.h"
#include "btif_sock_rfc.h"
#include "protobuf/proto/dm.pb.h"
#include "protobuf/include/proto_message_ids.h"
#include "osi/include/log.h"
#include "btif_gatt.h"
#include "btif_api.h"
#include "btif_gatt.h"
#include "btif_ss_logger.h"
#include <base/bind.h>
#include <utils/Log.h>
#include <limits.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/ioctl.h>

using namespace std;

#define MSG_EXIT_THREAD			1
#define MSG_WRITE_PROTO		  2


BluetoothSSTransport* gSSTransportCtrl = NULL;
BluetoothSSTransport* gSSTransportData = NULL;
BluetoothSSTransport* gSSTransportLeData = NULL;
BluetoothSSTransport* gSSTransportSsrData = NULL;
BluetoothSSTransport* gSSTransportObexData = NULL;

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
alarm_t *rx_ssr_dump_thread_timeout;

pthread_mutex_t tx_threads_mutex;
pthread_mutex_t rx_threads_mutex;
pthread_mutex_t wakelock_mutex;

BluetoothSSInterface* BluetoothSSInterface::getInstance() {
    static BluetoothSSInterface instance;
    return &instance;
}

base::MessageLoop* get_ss_alarm_message_loop() { return message_loop_alarm_; }

static bt_status_t do_in_ctrl_tx_thread(const base::Location& from_here,
    const base::Closure& task) {
  if (!message_loop_ctrl_tx_ || !message_loop_ctrl_tx_->task_runner().get()) {
    ALOGE("%s: Dropped message, message_loop not initialized yet",__func__);
    return BT_STATUS_FAIL;
  }

  if (message_loop_ctrl_tx_->task_runner()->PostTask(from_here, task)) {
    return BT_STATUS_SUCCESS;
  }

  ALOGE("%s: Post task to task runner failed!",__func__);
  return BT_STATUS_FAIL;
}


static bt_status_t do_in_ctrl_tx_thread(const base::Closure& task) {
  return do_in_ctrl_tx_thread(FROM_HERE, task);
}

static void txThreadTimeout(void* data) {
  ALOGI("%s()", __func__);
  isTxTimeout = true;
  if (isRxTimeout) {
    ALOGI("%s() RX Threads are already timedout, so releasing wakelock for GLINK", __func__);
    BluetoothSSInterface::ssGlinkWakeLockAcquireOrRelease(false, false);
  }
}

static void rxThreadTimeout(void* data) {
  ALOGI("%s()", __func__);
  isRxTimeout = true;
  if (isTxTimeout) {
    ALOGI("%s() TX Threads are already timedout, so releasing wakelock for GLINK", __func__);
    BluetoothSSInterface::ssGlinkWakeLockAcquireOrRelease(false, false);
  }
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
    ALOGE("%s: Dropped message, message_loop not initialized yet",__func__);
    return BT_STATUS_FAIL;
  }

  if (message_loop_data_tx_->task_runner()->PostTask(from_here, task)) {
    return BT_STATUS_SUCCESS;
  }

  ALOGE("%s: Post task to task runner failed!",__func__);
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
    ALOGE("%s: Dropped message, message_loop not initialized yet",__func__);
    return BT_STATUS_FAIL;
  }

  if (message_loop_le_data_tx_->task_runner()->PostTask(from_here, task)) {
    return BT_STATUS_SUCCESS;
  }

  ALOGE("%s: Post task to task runner failed!",__func__);
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
    ALOGE("%s: Dropped message, message_loop not initialized yet",__func__);
    return BT_STATUS_FAIL;
  }

  if (message_loop_data_logging_->task_runner()->PostTask(from_here, task)) {
    return BT_STATUS_SUCCESS;
  }

  ALOGE("%s: Post task to task runner failed!",__func__);
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
        ALOGE("open failed");
        running_ctrl_ch_ = false;
    } else {
        running_ctrl_ch_ = true;
    }
    gSSTransportData = new BluetoothSSTransport();
    ALOGI("BluetoothSSInterface calling open for data channel");
    rsltfd = gSSTransportData->open(BT_SS_DATA_CH);
    ALOGI("BluetoothSSInterface finish open for data rstlfd is :: %d",rsltfd);
    if (rsltfd <= 0) {
        ALOGE("open failed");
        running_data_ch_ = false;
    } else {
        running_data_ch_ = true;
    }
    gSSTransportLeData = new BluetoothSSTransport();
    ALOGI("BluetoothSSInterface calling open for le data channel");
    rsltfd = gSSTransportLeData->open(BT_SS_LE_DATA_CH);
    ALOGI("BluetoothSSInterface finish open for le data rstlfd is :: %d",rsltfd);
    if (rsltfd <= 0) {
        ALOGE("open failed");
        running_le_data_ch_ = false;
    } else {
        running_le_data_ch_ = true;
    }
    gSSTransportObexData = new BluetoothSSTransport();
    ALOGI("BluetoothSSInterface calling open for obex data channel");
    rsltfd = gSSTransportObexData->open(BT_SS_OBEX_DATA_CH);
    ALOGI("BluetoothSSInterface finish open for obex data rstlfd is :: %d",rsltfd);
    if (rsltfd <= 0) {
      ALOGE("open failed");
      running_obex_data_ch_ = false;
    } else {
      running_obex_data_ch_ = true;
    }

    gSSTransportSsrData = new BluetoothSSTransport();
    ALOGI("BluetoothSSInterface calling open for ssr data channel");
    rsltfd = gSSTransportSsrData->open(BT_SS_SSR_DATA_CH);
    ALOGI("BluetoothSSInterface finish open for ssr data rstlfd is :: %d",rsltfd);
    if (rsltfd <= 0) {
      ALOGE("open failed");
      running_ssr_data_ch_ = false;
    }
    else {
      running_ssr_data_ch_ = true;
    }

    isTxTimeout = false;
    isRxTimeout = false;
    isWakelockAcquired = false;
    isScanlockAcquired = false;
    pthread_mutex_init(&tx_threads_mutex, NULL);
    pthread_mutex_init(&rx_threads_mutex, NULL);
    pthread_mutex_init(&wakelock_mutex, NULL);

    //message loop for alarm
    alarm_thread = thread_new_sized("alarm_thread", 1024);
    thread_post(alarm_thread, run_message_loop_for_alarm, nullptr);

    tx_thread_timeout = alarm_new("glink_ctrl_tx_timeout_alarm");
    rx_thread_timeout = alarm_new("glink_ctrl_rx_timeout_alarm");
    rx_ssr_dump_thread_timeout = alarm_new("glink_ctrl_rx_ssr_dump_timeout_alarm");

    //threads for ctrl channel
    if(!rx_thread){
        rx_thread = std::unique_ptr<std::thread>(new std::thread(&BluetoothSSInterface::processRx, this));
        alarm_set_on_mloop(rx_thread_timeout, GLINK_IDLE_TIMEOUT,
                    rxThreadTimeout, NULL);
        rx_thread->detach();
    }
    ctrl_tx_thread = thread_new_sized("ctrl_tx_thread", 1024);
    thread_post(ctrl_tx_thread, run_message_loop_for_ctrl_tx, nullptr);
    alarm_set_on_mloop(tx_thread_timeout, GLINK_IDLE_TIMEOUT,
                    txThreadTimeout, NULL);

    //threads for data channel
    if(!data_ch_rx_thread){
        data_ch_rx_thread = std::unique_ptr<std::thread>(new std::thread(&BluetoothSSInterface::processDataChRx, this));
        data_ch_rx_thread->detach();
    }

    //thread for obex data
    if(!obex_data_ch_rx_thread){
      obex_data_ch_rx_thread = std::unique_ptr<std::thread>(new std::thread(&BluetoothSSInterface::processObexDataChRx, this));
      obex_data_ch_rx_thread->detach();
    }

    //thread for ssr dump
    if(!ssr_data_ch_rx_thread){
        ssr_data_ch_rx_thread = std::unique_ptr<std::thread>(new std::thread(&BluetoothSSInterface::processSsrDataChRx, this));
        ssr_data_ch_rx_thread->detach();
    }

    data_tx_thread = thread_new_sized("data_tx_thread", 1024);
    thread_post(data_tx_thread, run_message_loop_for_data_tx, nullptr);

    //threads for le data channel
    if(!le_data_ch_rx_thread){
        le_data_ch_rx_thread = std::unique_ptr<std::thread>(new std::thread(&BluetoothSSInterface::processLeDataChRx, this));
        le_data_ch_rx_thread->detach();
    }
    le_data_tx_thread = thread_new_sized("le_data_tx_thread", 1024);
    thread_post(le_data_tx_thread, run_message_loop_for_le_data_tx, nullptr);

    gProfileCallbackMap.clear();//Clearing profile callback map
    //threads for GLINK data logging in File
    data_logging_thread = thread_new_sized("data_logging_thread", 1024);
    thread_post(data_logging_thread, run_message_loop_for_data_logging, nullptr);
}

BluetoothSSInterface::~BluetoothSSInterface(){
  ALOGI("BluetoothSSInterface destructor");
}

void BluetoothSSInterface::cleanup() {
    ALOGI("BluetoothSSInterface cleanup start");
    BluetoothSSInterface::ssGlinkWakeLockAcquireOrRelease(false, false);

    // Stop Alarms
    ALOGI("BluetoothSSInterface free alarms");
    alarm_free(rx_thread_timeout);
    alarm_free(tx_thread_timeout);
    alarm_free(rx_ssr_dump_thread_timeout);
    /*if (alarm_is_scheduled(rx_thread_timeout)) {
      ALOGI("%s(): rx_thread_timeout() scheduled", __func__);
      alarm_cancel(rx_thread_timeout);
      alarm_free(rx_thread_timeout);
    } else {
      ALOGI("%s(): rx_thread_timeout() is not scheduled", __func__);
    }

    if (alarm_is_scheduled(rx_ssr_dump_thread_timeout)) {
      ALOGI("%s(): rx_ssr_dump_thread_timeout() scheduled", __func__);
      alarm_cancel(rx_ssr_dump_thread_timeout);
      alarm_free(rx_ssr_dump_thread_timeout);
    }
    else {
      ALOGI("%s(): rx_ssr_dump_thread_timeout() is not scheduled", __func__);
    }

    if (alarm_is_scheduled(tx_thread_timeout)) {
      ALOGI("%s(): tx_thread_timeout() scheduled", __func__);
      alarm_cancel(tx_thread_timeout);
      alarm_free(tx_thread_timeout);
    } else {
      ALOGI("%s(): tx_thread_timeout() is not scheduled", __func__);
    }*/

    //Cleanup Ctrl Ch
    if (run_loop_ctrl_tx_ && message_loop_ctrl_tx_) {
      message_loop_ctrl_tx_->task_runner()->PostTask(FROM_HERE,
          run_loop_ctrl_tx_->QuitClosure());
    }

    thread_free(ctrl_tx_thread);
    ctrl_tx_thread = NULL;
    if (gSSTransportCtrl != NULL) {
      gSSTransportCtrl->close();
      running_ctrl_ch_ = false;
      delete gSSTransportCtrl;
      gSSTransportCtrl = NULL;
    }

    //Cleanup Data Tx Ch
    if (run_loop_data_tx_ && message_loop_data_tx_) {
      message_loop_data_tx_->task_runner()->PostTask(FROM_HERE,
          run_loop_data_tx_->QuitClosure());
    }

    thread_free(data_tx_thread);
    data_tx_thread = NULL;

    if (gSSTransportData != NULL) {
      gSSTransportData->close();
      running_data_ch_ = false;
      delete gSSTransportData;
      gSSTransportData = NULL;
    }

    //Cleanup Le Data Tx Ch
    if (run_loop_le_data_tx_ && message_loop_le_data_tx_) {
      message_loop_le_data_tx_->task_runner()->PostTask(FROM_HERE,
          run_loop_le_data_tx_->QuitClosure());
    }

    thread_free(le_data_tx_thread);
    le_data_tx_thread = NULL;

    if(gSSTransportLeData != NULL) {
      gSSTransportLeData->close();
      running_le_data_ch_ = false;
      delete gSSTransportLeData;
      gSSTransportLeData = NULL;
    }

    //Cleanup OBEX Data Ch
    if (gSSTransportObexData != NULL) {
      gSSTransportObexData->close();
      running_obex_data_ch_ = false;
      delete gSSTransportObexData;
      gSSTransportObexData = NULL;
    }

    //Cleanup SSR Data Ch
    if (gSSTransportSsrData != NULL) {
      gSSTransportSsrData->close();
      running_ssr_data_ch_ = false;
      delete gSSTransportSsrData;
      gSSTransportSsrData = NULL;
    }

    //Cleanup Data Logging Thread
    if (run_loop_data_logging_ && message_loop_data_logging_) {
      message_loop_data_logging_->task_runner()->PostTask(FROM_HERE,
          run_loop_data_logging_->QuitClosure());
    }

    thread_free(data_logging_thread);
    data_logging_thread = NULL;

    //Cleanup profile callback map
    gProfileCallbackMap.clear();

    //Cleanup Alarm Loop
    if (run_loop_alarm_ && message_loop_alarm_) {
      message_loop_alarm_->task_runner()->PostTask(FROM_HERE,
          run_loop_alarm_->QuitClosure());
    }

    thread_free(alarm_thread);
    alarm_thread = NULL;

    //Cleanup wakelock mutex
    isTxTimeout = false;
    isRxTimeout = false;
    isWakelockAcquired = false;
    isScanlockAcquired = false;
    pthread_mutex_destroy(&tx_threads_mutex);
    pthread_mutex_destroy(&rx_threads_mutex);
    pthread_mutex_destroy(&wakelock_mutex);

    ALOGI("BluetoothSSInterface cleanup end");
}

void processDataLogging(uint8_t *msgStr, size_t buflen, const char *msgtyp) {
    gSSTransportCtrl->file_write(msgStr,buflen,msgtyp);
}
void processTx(std::string msgStr) {
  const  char *msgType="Tx";
  uint8_t *tmpBuf = (uint8_t*)msgStr.c_str();
  uint16_t MSG_ID = tmpBuf [0] + (((int)(tmpBuf [1]))<<8);
  ALOGD("%s: msg id : %d and length %d", __func__, MSG_ID, msgStr.length());
  size_t bytes_written = 0;
  pthread_mutex_lock(&tx_threads_mutex);
  if (alarm_is_scheduled(tx_thread_timeout)) {
    ALOGD("%s(): tx_thread_timeout() scheduled", __func__);
    alarm_cancel(tx_thread_timeout);
  }
  isTxTimeout = false;
  alarm_set_on_mloop(tx_thread_timeout, GLINK_IDLE_TIMEOUT,
      txThreadTimeout, NULL);
  pthread_mutex_unlock(&tx_threads_mutex);
  BluetoothSSInterface::ssGlinkWakeLockAcquireOrRelease(false, true);
  if (log_level >= SS_BT_TRACE_LEVEL_GLINK) {
    do_in_data_logging_thread(base::Bind(processDataLogging, tmpBuf,msgStr.length(),msgType));
  }
  gSSTransportCtrl->write(tmpBuf,msgStr.length(),&bytes_written);
  ALOGD("%s: CTRL_CH: write payload bytes_written=%d", __func__, (int)bytes_written);
}

int processDataTx(std::string msgStr, int fd) {
  uint8_t *tmpBuf = (uint8_t*)msgStr.c_str();
  uint16_t MSG_ID = tmpBuf [0] + (((int)(tmpBuf [1]))<<8);
  ALOGD("%s: msg id : %d and length %d", __func__, MSG_ID, msgStr.length());
  size_t bytes_written = 0;
  int result = -1;
  int retry_count = 0;
  do {
        pthread_mutex_lock(&tx_threads_mutex);
        if (alarm_is_scheduled(tx_thread_timeout)) {
          ALOGI("%s(): tx_thread_timeout() scheduled", __func__);
          alarm_cancel(tx_thread_timeout);
        }
        isTxTimeout = false;
        alarm_set_on_mloop(tx_thread_timeout, GLINK_IDLE_TIMEOUT, txThreadTimeout, NULL);
        pthread_mutex_unlock(&tx_threads_mutex);
        BluetoothSSInterface::ssGlinkWakeLockAcquireOrRelease(false, true);
        rfc_slot_t* slot = find_rfc_slot_by_fd(fd);
        if(!slot){
          ALOGI("%s slot already cleaned up", __func__);
          return result;
        }
        result = gSSTransportData->write(tmpBuf,msgStr.length(),&bytes_written);
        if(result == 0){
          ALOGI("%s: Glink write success",__func__);
          break;
        }else if(result == -1){
          retry_count++;
          ALOGE("%s: Glink write failure...retrying...retry count is :: %d",__func__,retry_count);
          switch (retry_count) {
            case 1 ... 4: {
              break;
            }
            case 5 ... 7: {
              usleep(10000);
              break;
            }
            case 8 ... 500:{
              usleep(25000);
              break;
            }
            case 501 ... INT_MAX:{
              ALOGI("%s: retried 500 times, unblocking the tx loop",__func__);
              return result;
            }
         }
         continue;
        }else{
          ALOGE("%s: Glink write failure status unknown",__func__);
          break;
        }
  }while(true);
  return bytes_written;
}

int processLeDataTx(std::string msgStr) {
  const  char *msgType="Tx";
  uint8_t *tmpBuf = (uint8_t*)msgStr.c_str();
  uint16_t MSG_ID = tmpBuf [0] + (((int)(tmpBuf [1]))<<8);
  ALOGD("%s: msg id : %d and length %d", __func__, MSG_ID, msgStr.length());

  size_t bytes_written = 0;
  if (log_level >= SS_BT_TRACE_LEVEL_GLINK) {
    do_in_data_logging_thread(base::Bind(processDataLogging, tmpBuf,msgStr.length(),msgType));
  }
  int result = -1;
  int retry_count = 0;
  do {
        pthread_mutex_lock(&tx_threads_mutex);
        if (alarm_is_scheduled(tx_thread_timeout)) {
          ALOGI("%s(): tx_thread_timeout() scheduled", __func__);
          alarm_cancel(tx_thread_timeout);
        }
        isTxTimeout = false;
        alarm_set_on_mloop(tx_thread_timeout, GLINK_IDLE_TIMEOUT, txThreadTimeout, NULL);
        pthread_mutex_unlock(&tx_threads_mutex);
        BluetoothSSInterface::ssGlinkWakeLockAcquireOrRelease(false, true);
        result = gSSTransportLeData->write(tmpBuf,msgStr.length(),&bytes_written);
        if(result == 0){
          ALOGI("%s: Glink write success",__func__);
          break;
        }else if(result == -1){
          retry_count++;
          ALOGE("%s: Glink write failure...retrying...retry count is :: %d",__func__,retry_count);
          if(retry_count > 3){
            usleep(50000);
          }
          continue;
        }else{
          ALOGE("%s: Glink write failure status unknown",__func__);
          break;
        }
  }while(true);
  ALOGI("%s: LE_DATA_CH: write payload bytes_written=%d", __func__, (int)bytes_written);
  return bytes_written;
}

int processObexDataTx(std::string msgStr) {
  const  char *msgType="Tx";
  uint8_t *tmpBuf = (uint8_t*)msgStr.c_str();
  uint16_t MSG_ID = tmpBuf [0] + (((int)(tmpBuf [1]))<<8);
  ALOGD("%s: msg id : %d and length %d", __func__, MSG_ID, msgStr.length());

  size_t bytes_written = 0;
  if (log_level >= SS_BT_TRACE_LEVEL_GLINK) {
    do_in_data_logging_thread(base::Bind(processDataLogging, tmpBuf,msgStr.length(),msgType));
  }
  int result = -1;
  int retry_count = 0;
  do {
        pthread_mutex_lock(&tx_threads_mutex);
        if (alarm_is_scheduled(tx_thread_timeout)) {
          ALOGI("%s(): tx_thread_timeout() scheduled", __func__);
          alarm_cancel(tx_thread_timeout);
        }
        isTxTimeout = false;
        alarm_set_on_mloop(tx_thread_timeout, GLINK_IDLE_TIMEOUT, txThreadTimeout, NULL);
        pthread_mutex_unlock(&tx_threads_mutex);
        BluetoothSSInterface::ssGlinkWakeLockAcquireOrRelease(false, true);
        result = gSSTransportObexData->write(tmpBuf,msgStr.length(),&bytes_written);
        if(result == 0){
          ALOGI("%s: Glink write success",__func__);
          break;
        }else if(result == -1){
          retry_count++;
          ALOGE("%s: Glink write failure...retrying...retry count is :: %d",__func__,retry_count);
          if(retry_count > 3){
            usleep(50000);
          }
          continue;
        }else{
          ALOGE("%s: Glink write failure status unknown",__func__);
          break;
        }
  }while(true);
  ALOGI("%s: OBEX_DATA_CH: write payload bytes_written=%d", __func__, (int)bytes_written);
  return bytes_written;
}

void BluetoothSSInterface::postTxMsg(std::string msgStr) {
  uint8_t *tmpBuf = (uint8_t*)msgStr.c_str();
  uint16_t MSG_ID = tmpBuf [0] + (((int)(tmpBuf [1]))<<8);
  ALOGD("postTxMsg with msg id : %d and length %d", MSG_ID, msgStr.length());
  if (msgStr.length() > MSG_SIZE_MAX) {
    ALOGE("ERROR:: Application trying to write the data more than the size configured for GLINK. PROTO SIZE: %d", msgStr.length());
    return;
  }
  do_in_ctrl_tx_thread(base::Bind(processTx, msgStr));
}

int BluetoothSSInterface::postDataChTxMsg(std::string msgStr, int fd) {
  uint8_t *tmpBuf = (uint8_t*)msgStr.c_str();
  uint16_t MSG_ID = tmpBuf [0] + (((int)(tmpBuf [1]))<<8);
  ALOGI("postDataChTxMsg with msg id : %d and length %d", MSG_ID, msgStr.length());
  //do_in_data_tx_thread(base::Bind(processDataTx, msgStr));
  return processDataTx(msgStr,fd);
}

int BluetoothSSInterface::postLeDataChTxMsg(std::string msgStr) {
  uint8_t *tmpBuf = (uint8_t*)msgStr.c_str();
  uint16_t MSG_ID = tmpBuf [0] + (((int)(tmpBuf [1]))<<8);
  ALOGI("postLeDataChTxMsg with msg id : %d and length %d", MSG_ID, msgStr.length());
  //do_in_data_tx_thread(base::Bind(processLeDataTx, msgStr));
  return processLeDataTx(msgStr);
}

int BluetoothSSInterface::postObexDataChTxMsg(std::string msgStr) {
  uint8_t *tmpBuf = (uint8_t*)msgStr.c_str();
  uint16_t MSG_ID = tmpBuf [0] + (((int)(tmpBuf [1]))<<8);
  ALOGI("postObexDataChTxMsg with msg id : %d and length %d", MSG_ID, msgStr.length());
  return processObexDataTx(msgStr);
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

void BluetoothSSInterface::ssGlinkWakeLockAcquireOrRelease(bool isScanOrInquiry, bool lockRequest) {
  ALOGD("ssGlinkWakeLockAcquireOrRelease ScanOrInquiry %d, lockRequest %d",isScanOrInquiry, lockRequest);
  pthread_mutex_lock(&wakelock_mutex);
  if (isScanOrInquiry && lockRequest) {
    isScanlockAcquired = true;
  } else if (isScanOrInquiry && !lockRequest) {
    isScanlockAcquired = false;
    pthread_mutex_unlock(&wakelock_mutex);
    return;
  } else {
    //do nothing
  }
  if (isScanlockAcquired && !isScanOrInquiry) {
    ALOGI("%s Wakelock acquired from Scan or Inquiry, Ignoring other request untill they release",__func__);
    pthread_mutex_unlock(&wakelock_mutex);
    return;
  }
  int result = -1;
  int fd = -1;
  if (isWakelockAcquired && lockRequest) {
    ALOGI("%s Wakelock already acquired",__func__);
    pthread_mutex_unlock(&wakelock_mutex);
    return;
  } else if (!isWakelockAcquired && !lockRequest) {
    ALOGI("%s Wakelock already released",__func__);
    pthread_mutex_unlock(&wakelock_mutex);
    return;
  } else {
    do
    {
      if (lockRequest == true) {
        isWakelockAcquired = true;
        fd = ::open(WAKE_LOCK_FILE, O_WRONLY|O_APPEND);
      } else {
        isWakelockAcquired = false;
        fd = ::open(WAKE_UNLOCK_FILE, O_WRONLY|O_APPEND);
      }
      if (fd < 0) {
        ALOGE("wakeLockAcquireOrRelease: open error: %d, %s \n",errno, strerror (errno));
        result = -2;
        break;
      }
      result = ::write(fd, SS_GLINK_LOCK_STR, strlen(SS_GLINK_LOCK_STR));
      if (result != (int) strlen (SS_GLINK_LOCK_STR)) {
        ALOGE("wakeLockAcquireOrRelease: write error: %d, %s \n", errno, strerror (errno));
        result = -4;
        break;
      }
      result = 0;
    } while (0);
    if (fd >= 0) {
      ::close (fd);
      fd = -1;
    }
    if (result != 0) {
      ALOGE("wakeLockAcquireOrRelease: failed for lock/unlock %d, result = %d \n", lockRequest, result);
    } else {
      if (lockRequest) {
        ALOGI("%s Wakelock acquired",__func__);
      } else {
        ALOGI("%s Wakelock released",__func__);
      }
    }
  }
  pthread_mutex_unlock(&wakelock_mutex);
}

void BluetoothSSInterface::processRx() {
    ALOGD("BluetoothSSInterface processRx :: running_ctrl_ch_ is :: %d",running_ctrl_ch_);
    uint8_t *readBuffer = (uint8_t *)malloc(MSG_SIZE_MAX*sizeof(uint8_t));
    const char *msgType="Rx";
    if (readBuffer == NULL) {
      ALOGE("%s: readBuffer malloc failed",__func__);
      return;
    }
    while (running_ctrl_ch_) {
        int rcPoll = gSSTransportCtrl->poll(-1);
        if (-1 == rcPoll) {
            ALOGE("Poll Failure");
            break;
        }
        pthread_mutex_lock(&rx_threads_mutex);
        if (alarm_is_scheduled(rx_thread_timeout)) {
          ALOGD("%s(): rx_thread_timeout() scheduled", __func__);
          alarm_cancel(rx_thread_timeout);
        }
        isRxTimeout = false;
        alarm_set_on_mloop(rx_thread_timeout, GLINK_IDLE_TIMEOUT,
            rxThreadTimeout, NULL);
        pthread_mutex_unlock(&rx_threads_mutex);
        ssGlinkWakeLockAcquireOrRelease(false, true);
        int num = gSSTransportCtrl->read(readBuffer, MSG_SIZE_MAX*sizeof(uint8_t));
        if (log_level >= SS_BT_TRACE_LEVEL_GLINK) {
          do_in_data_logging_thread(base::Bind(processDataLogging, readBuffer, num, msgType));
        }
        ALOGI("ss_bt_ctrl num of bytes read from stream is :: %d",num);
        if(num < MSG_SIZE_MIN) {
            ALOGE("Slate response is too short ::  %d",num);
        }else {
            uint16_t MSG_ID = readBuffer[0] + (((int)(readBuffer[1]))<<8);
            if (MSG_ID == BT_DM_BATCH_MSG) {
              processBatchMsg(readBuffer);
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

void BluetoothSSInterface::processDataChRx() {
    ALOGE("BluetoothSSInterface processRx :: running_data_ch_ is :: %d",running_data_ch_);
    uint8_t *readBuffer = (uint8_t *)malloc(MSG_SIZE_MAX*sizeof(uint8_t));
    const char *msgType="Rx";
    if (readBuffer == NULL) {
      ALOGE("%s: readBuffer malloc failed",__func__);
      return;
    }
    while (running_data_ch_) {
        int rcPoll = gSSTransportData->poll(-1);
        if (-1 == rcPoll) {
            ALOGE("Poll Failure");
            break;
        }
        pthread_mutex_lock(&rx_threads_mutex);
        if (alarm_is_scheduled(rx_thread_timeout)) {
          ALOGI("%s(): rx_thread_timeout() scheduled", __func__);
          alarm_cancel(rx_thread_timeout);
        }
        isRxTimeout = false;
        alarm_set_on_mloop(rx_thread_timeout, GLINK_IDLE_TIMEOUT,
            rxThreadTimeout, NULL);
        pthread_mutex_unlock(&rx_threads_mutex);
        ssGlinkWakeLockAcquireOrRelease(false, true);
        int num = gSSTransportData->read(readBuffer, MSG_SIZE_MAX*sizeof(uint8_t));
        if (log_level >= SS_BT_TRACE_LEVEL_GLINK) {
          do_in_data_logging_thread(base::Bind(processDataLogging, readBuffer, num, msgType));
        }
        ALOGI("ss_bt_data num of bytes read from stream is :: %d",num);
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
    const char *msgType="Rx";
    if (readBuffer == NULL) {
      ALOGE("%s: readBuffer malloc failed",__func__);
      return;
    }
    while (running_le_data_ch_) {
        int rcPoll = gSSTransportLeData->poll(-1);
        if (-1 == rcPoll) {
            ALOGE("Poll Failure");
            break;
        }
        pthread_mutex_lock(&rx_threads_mutex);
        if (alarm_is_scheduled(rx_thread_timeout)) {
          ALOGI("%s(): rx_thread_timeout() scheduled", __func__);
          alarm_cancel(rx_thread_timeout);
        }
        isRxTimeout = false;
        alarm_set_on_mloop(rx_thread_timeout, GLINK_IDLE_TIMEOUT,
            rxThreadTimeout, NULL);
        pthread_mutex_unlock(&rx_threads_mutex);
        ssGlinkWakeLockAcquireOrRelease(false, true);
        int num = gSSTransportLeData->read(readBuffer, MSG_SIZE_MAX*sizeof(uint8_t));
        if (log_level >= SS_BT_TRACE_LEVEL_GLINK) {
          do_in_data_logging_thread(base::Bind(processDataLogging, readBuffer, num, msgType));
        }
        ALOGI("ss_bt_le_data num of bytes read from stream is :: %d",num);
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

void BluetoothSSInterface::processObexDataChRx() {
ALOGE("BluetoothSSInterface processRx :: running_obex_data_ch_ is :: %d",running_obex_data_ch_);
    //btif_ss_logger btif_ss_logger;
    uint8_t *readBuffer = (uint8_t *)malloc(MSG_SIZE_MAX*sizeof(uint8_t));
    if (readBuffer == NULL) {
      ALOGE("%s: readBuffer malloc failed",__func__);
      return;
    }
    while (running_obex_data_ch_) {
        int rcPoll = gSSTransportObexData->poll(-1);
        if (-1 == rcPoll) {
            ALOGI("Poll Failure");
            break;
        }
        pthread_mutex_lock(&rx_threads_mutex);
        if (alarm_is_scheduled(rx_thread_timeout)) {
          ALOGI("%s(): rx_thread_timeout() scheduled", __func__);
          alarm_cancel(rx_thread_timeout);
        }
        isRxTimeout = false;
        alarm_set_on_mloop(rx_thread_timeout, GLINK_IDLE_TIMEOUT,
            rxThreadTimeout, NULL);
        pthread_mutex_unlock(&rx_threads_mutex);
        ssGlinkWakeLockAcquireOrRelease(false, true);
        int num = gSSTransportObexData->read(readBuffer, MSG_SIZE_MAX*sizeof(uint8_t));
        ALOGI("ss_bt_obex_data num of bytes read from stream is :: %d",num);
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

void BluetoothSSInterface::processSsrDataChRx() {
    ALOGE("BluetoothSSInterface processRx :: running_ssr_data_ch_ is :: %d",running_ssr_data_ch_);
    //btif_ss_logger btif_ss_logger;
    uint8_t *readBuffer = (uint8_t *)malloc(SSR_CH_MAX_SIZE*sizeof(uint8_t));
    if (readBuffer == NULL) {
      ALOGE("%s: readBuffer malloc failed",__func__);
      return;
    }
    while (running_ssr_data_ch_) {
        int rcPoll = gSSTransportSsrData->poll(-1);
        if (-1 == rcPoll) {
            ALOGI("Poll Failure");
            break;
        }
        pthread_mutex_lock(&rx_threads_mutex);
        if (alarm_is_scheduled(rx_thread_timeout)) {
          ALOGI("%s(): rx_thread_timeout() scheduled", __func__);
          alarm_cancel(rx_thread_timeout);
        }
        isRxTimeout = false;
        alarm_set_on_mloop(rx_thread_timeout, GLINK_IDLE_TIMEOUT, rxThreadTimeout, NULL);
        pthread_mutex_unlock(&rx_threads_mutex);
        ssGlinkWakeLockAcquireOrRelease(false, true);
        int num = gSSTransportSsrData->read(readBuffer, SSR_CH_MAX_SIZE*sizeof(uint8_t));
        ALOGI("ss_bt_ssr_data num of bytes read from stream is :: %d",num);
        if(num < SSR_CH_MIN_SIZE) {
            ALOGE("Slate response is too short ::  %d",num);
        }
        else {
          tBTIF_SS_Cback ss_cback;
          memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
          ss_cback.num_bytes = num;
          ss_cback.payload = (uint8_t *)malloc(num*sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
          if (ss_cback.payload == NULL) {
            ALOGE("%s: payload malloc failed",__func__);
            continue;
          }
          memcpy(ss_cback.payload, readBuffer, (num * sizeof(uint8_t)) );
          parseRxData(BT_DM_SSR_CB, ss_cback);
        }
    }
    free(readBuffer);
}

void BluetoothSSInterface::parseRxData(int msg_id, tBTIF_SS_Cback ss_cback) {
    ALOGD("parseRxData msg_id is :: %X",msg_id);
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
        case BT_LE_ADV_API_START ... BT_ADV_EVT_MAX: {
          auto it = gProfileCallbackMap.find(BT_PROFILE_ID_ADV);
	  if (it != gProfileCallbackMap.end()) {
            ALOGI("%s: Sending callback to Adv", __func__);
            btif_transfer_context(it->second, msg_id, (char*)&ss_cback, sizeof(ss_cback),NULL);
          } else {
            ALOGE("%s: callback not registered for Adv", __func__);
	  }
	  break;
	}
        case BT_LE_SCAN_API_START ... BT_LE_SCAN_EVT_MAX: {
          auto it = gProfileCallbackMap.find(BT_PROFILE_ID_SCAN);
          if (it != gProfileCallbackMap.end()) {
            ALOGI("%s: Sending callback to BLE SCAN ", __func__);
            btif_transfer_context(it->second, msg_id, (char*)&ss_cback, sizeof(ss_cback),NULL);
          } else {
            ALOGE("%s: callback not registered for BLE SCAN", __func__);
          }
          break;
        }
        case BT_LE_GATT_CLIENT_EVT_START ... BT_LE_GATT_CLIENT_EVT_MAX: {
          auto it = gProfileCallbackMap.find(BT_PROFILE_ID_GATTC);
          if (it != gProfileCallbackMap.end()) {
            ALOGD("%s: Sending callback to GATT", __func__);
            btif_transfer_context(it->second, msg_id, (char*)&ss_cback, sizeof(ss_cback),NULL);
          } else {
            ALOGE("%s: callback not registered for GATT", __func__);
          }
          break;
        }
        case BT_LE_GATT_SERVER_EVT_START ... BT_LE_GATT_SERVER_EVT_MAX: {
          auto it = gProfileCallbackMap.find(BT_PROFILE_ID_GATTS);
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
            ALOGE("msg_id : %d Not matching with any group",msg_id);
            if (ss_cback.payload != NULL) {
              free(ss_cback.payload);
              ss_cback.payload = NULL;
            }
        break;
    }
}
void BluetoothSSInterface::processBatchMsg(uint8_t buffer[]) {
  ALOGD("processBatchMsg");
  int total_length_processed = MAX_LENGTH_WITH_PROTO_NONE;
  uint16_t msg_count = buffer[2] + (((int)(buffer[3]))<<8);
  uint16_t length = buffer[4] + (((int)(buffer[5]))<<8);
  if (msg_count <= 0) {
    ALOGD("No messages to process. Count is 0");
    return;
  }
  while (msg_count > 0 && length > 0) {
    vector<uint8_t> batch_msgs(buffer + total_length_processed, buffer + length + total_length_processed);
    uint16_t MSG_ID = batch_msgs[0] + (((int)(batch_msgs[1]))<<8);
    uint16_t payload_length = batch_msgs[2] + (((int)(batch_msgs[3]))<<8);
    uint16_t msg_length = payload_length + MAX_LENGTH_WITH_PROTO_NONE;
    tBTIF_SS_Cback ss_cback;
    memset(&ss_cback, 0, sizeof(tBTIF_SS_Cback));
    ss_cback.payload = (uint8_t *)malloc(msg_length * sizeof(uint8_t)); //This memory should be released from each profile after done with the processing
    if (ss_cback.payload == NULL) {
      ALOGE("%s: payload malloc failed",__func__);
      continue;
    }
    memcpy(ss_cback.payload, batch_msgs.data(), (msg_length * sizeof(uint8_t)) );
    parseRxData(MSG_ID, ss_cback);
    msg_count--;
    length = length - msg_length;
    total_length_processed = total_length_processed + msg_length;
  }
}
