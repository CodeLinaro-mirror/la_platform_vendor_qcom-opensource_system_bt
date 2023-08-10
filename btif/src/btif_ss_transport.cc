/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

#define LOG_TAG "BluetoothSSTransport"
#include "btif_ss_transport.h"
#include "osi/include/log.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <unistd.h>

#include <iostream>
#include <sstream>
#include <iomanip>

#include "btif/protobuf/include/proto_message_ids.h"
#define PKT_IOCTL_MAGIC (0xC3)
#define PKT_IOCTL_QUEUE_RX_INTENT \
  _IOW(PKT_IOCTL_MAGIC, 0, unsigned int)
#define WAKE_LOCK_FILE    "/sys/power/wake_lock"
#define WAKE_UNLOCK_FILE  "/sys/power/wake_unlock"

const char* dump_search_dm_msgID(uint16_t msgID) {
  switch (msgID) {
    CASE_RETURN_STR(BT_DM_ENABLE)
    CASE_RETURN_STR(BT_DM_DISABLE)
    CASE_RETURN_STR(BT_DM_CLEANUP)
    CASE_RETURN_STR(BT_DM_GET_ADAPTER_PROPERTIES)
    CASE_RETURN_STR(BT_DM_GET_ADAPTER_PROPERTY)
    CASE_RETURN_STR(BT_DM_GET_ADAPTER_PROPERTY_NAME)
    CASE_RETURN_STR(BT_DM_SET_ADAPTER_PROPERTY)
    CASE_RETURN_STR(BT_DM_GET_REMOTE_DEVICE_PROPERTIES)
    CASE_RETURN_STR(BT_DM_GET_REMOTE_DEVICE_PROPERTY_BY_TYPE)
    CASE_RETURN_STR(BT_DM_SET_REMOTE_DEVICE_PROPERTIES)
    CASE_RETURN_STR(BT_DM_GET_REMOTE_SRV_RECORD)
    CASE_RETURN_STR(BT_DM_GET_REMOTE_SERVICES)
    CASE_RETURN_STR(BT_DM_START_DISCOVERY)
    CASE_RETURN_STR(BT_DM_CANCEL_DISCOVERY)
    CASE_RETURN_STR(BT_DM_CREATE_BOND)
    CASE_RETURN_STR(BT_DM_CREATE_BOND_OOB)
    CASE_RETURN_STR(BT_DM_REMOVE_BOND)
    CASE_RETURN_STR(BT_DM_CANCEL_BOND)
    CASE_RETURN_STR(BT_DM_GET_CONNECTION_STATE)
    CASE_RETURN_STR(BT_DM_PIN_REPLY)
    CASE_RETURN_STR(BT_DM_SSP_REPLY)
    CASE_RETURN_STR(BT_DM_ADAPTER_STATE_CHANGE_CB)
    CASE_RETURN_STR(BT_DM_ADAPTER_PROPERTIES_CB)
    CASE_RETURN_STR(BT_DM_REMOTE_DEVICE_PROPERTIES_CB)
    CASE_RETURN_STR(BT_DM_DISCOVERY_STATE_CHANGE_CB)
    CASE_RETURN_STR(BT_DM_DEVICE_FOUND_CB)
    CASE_RETURN_STR(BT_DM_PIN_REQUEST_CB)
    CASE_RETURN_STR(BT_DM_SSP_REQUEST_CB)
    CASE_RETURN_STR(BT_DM_BOND_STATE_CHANGE_CB)
    CASE_RETURN_STR(BT_DM_ACL_STATE_CHANGE_CB)
    CASE_RETURN_STR(BT_DM_LE_ADAPTER_PROPERTIES_CB)
    CASE_RETURN_STR(BT_DM_API_MAX)
    default:
      return "DM_UNKNOWN_MSG_ID";
  }
}

const char* dump_search_sdp_msgID(uint16_t msgID) {
  switch (msgID) {
    CASE_RETURN_STR(BT_SDP_SEARCH)
    CASE_RETURN_STR(BT_SDP_SEARCH_COMPLETE_CB)
    CASE_RETURN_STR(BT_SDP_API_MAX)
    default:
      return "SDP_UNKNOWN_MSG_ID";
  }
}

const char* dump_search_rfcomm_msgID(uint16_t msgID) {
  switch (msgID) {
    CASE_RETURN_STR(BT_RFCOMM_CREATE_SOCKET)
    CASE_RETURN_STR(BT_RFCOMM_CONNECT_SOCKET)
    CASE_RETURN_STR(BT_RFCOMM_WRITE_SOCKET_DATA)
    CASE_RETURN_STR(BT_RFCOMM_SCN_CB)
    CASE_RETURN_STR(BT_RFCOMM_SRV_OPEN_CB)
    CASE_RETURN_STR(BT_RFCOMM_CLIENT_CONNECT_CB)
    CASE_RETURN_STR(BT_RFCOMM_SOCKET_DATA_CB)
    CASE_RETURN_STR(BT_RFCOMM_API_MAX)
    default:
      return "RFCOMM_UNKNOWN_MSG_ID";
  }
}

const char* dump_search_gattc_msgID(uint16_t msgID) {
  switch (msgID) {
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_REGISTER)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_DEREGISTER)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_CONNECT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_DISCONNECT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_REFRESH)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_SEARCH_SERVICES)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_SEARCH_SERVICES_BY_UUID)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_READ_CHAR)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_READ_CHAR_BY_UUID)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_WRITE_CHAR)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_READ_DESC)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_WRITE_DESC)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_EXEC_WRITE)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_REGISTER_NOTIFICATIONS)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_DEREGISTER_NOTIFICATIONS)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_READ_RSSI)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_CONFIGURE_MTU)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_CONN_PARAM_UPDATE)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_SET_PHY)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_READ_PHY)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_GET_GATT_DB)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_REGISTERED_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_CONNECTED_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_DISCONNECTED_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_SEARCH_COMPLETED_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_REG_DEREG_NOTIFICATIONS_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_NOTIFY_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_READ_CHAR_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_WRITE_CHAR_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_EXEC_WRITE_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_READ_DESC_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_WRITE_DESC_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_READ_RSSI_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_MTU_UPDATED_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_CONGESTION_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_GET_GATT_DB_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_PHY_UPDATED_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_CONN_PARAM_UPDATED_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_SERVICE_CHANGED_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_READ_PHY_EVENT)
    CASE_RETURN_STR(BT_LE_GATT_CLIENT_EVT_MAX)
    default:
      return "LE_GATTC_UNKNOWN_MSG_ID";
  }
}
const char* dump_search_gatts_msgID(uint16_t msgID) {
  switch (msgID) {
    CASE_RETURN_STR(BT_LE_SERVER_REG_SERVER)
    CASE_RETURN_STR(BT_LE_SERVER_CONNECT)
    CASE_RETURN_STR(BT_LE_SERVER_ADD_SERVICE)
    CASE_RETURN_STR(BT_LE_SERVER_UNREG_SERVER)
    CASE_RETURN_STR(BT_LE_SERVER_DISCONNECT_SERVER)
    CASE_RETURN_STR(BT_LE_SERVER_SET_PHY)
    CASE_RETURN_STR(BT_LE_SERVER_READ_PHY)
    CASE_RETURN_STR(BT_LE_SERVER_DELETE_SEVICE)
    CASE_RETURN_STR(BT_LE_SERVER_SEND_INDICATION)
    CASE_RETURN_STR(BT_LE_SERVER_STOP_SERV)
    CASE_RETURN_STR(BT_LE_SERVER_SEND_RESPONSE)
    CASE_RETURN_STR(BT_LE_SERVER_REG_SERVER_EVENT)
    CASE_RETURN_STR(BT_LE_SERVER_CONN_CHNG_EVENT)
    CASE_RETURN_STR(BT_LE_SERVER_SRV_ADD_EVENT)
    CASE_RETURN_STR(BT_LE_SERVER_PHY_UPDATE_EVENT)
    CASE_RETURN_STR(BT_LE_SERVER_READ_PHY_EVENT)
    CASE_RETURN_STR(BT_LE_SERVER_SRV_DEL_EVENT)
    CASE_RETURN_STR(BT_LE_SERVER_IND_SENT_EVENT)
    CASE_RETURN_STR(BT_LE_SERVER_READ_CHAR_EVENT)
    CASE_RETURN_STR(BT_LE_SERVER_RSP_SENT_EVENT)
    CASE_RETURN_STR(BT_LE_SERVER_READ_DESC_EVENT)
    CASE_RETURN_STR(BT_LE_SERVER_WRITE_CHAR_EVENT)
    CASE_RETURN_STR(BT_LE_SERVER_WRITE_DESC_EVENT)
    CASE_RETURN_STR(BT_LE_SERVER_EXEC_WRITE_EVENT)
    CASE_RETURN_STR(BT_LE_SERVER_SRV_CONG_EVENT)
    CASE_RETURN_STR(BT_LE_SERVER_STOP_SERVICE_EVENT)
    CASE_RETURN_STR(BT_LE_SERVER_MTU_UPDATE_EVENT)
    CASE_RETURN_STR(BT_LE_SERVER_CONN_UPDATE_EVENT)
    default:
      return "LE_GATTS_UNKNOWN_MSG_ID";
    }
}
const char* dump_search_advertiser_msgID(uint16_t msgID) {
  switch (msgID) {
    CASE_RETURN_STR(BT_LE_ADV_REG_ADVERTISER)
    CASE_RETURN_STR(BT_LE_ADV_SET_PARAM)
    CASE_RETURN_STR(BT_LE_ADV_SET_DATA)
    CASE_RETURN_STR(BT_LE_ADV_ENABLE)
    CASE_RETURN_STR(BT_LE_ADV_UNREG)
    CASE_RETURN_STR(BT_LE_ADV_START_ADV)
    CASE_RETURN_STR(BT_LE_ADV_START_ADV_SET)
    CASE_RETURN_STR(BT_LE_ADV_SET_PERIODIC_ADV_PARAM)
    CASE_RETURN_STR(BT_LE_ADV_SET_PERIODIC_ADV_DATA)
    CASE_RETURN_STR(BT_LE_ADV_SET_PERIODIC_ADV_ENABLE)
    CASE_RETURN_STR(BT_LE_ADV_STOP_ALL_ADVERTISEMENTS)
    CASE_RETURN_STR(BT_LE_GET_OWN_ADDRESS)
    CASE_RETURN_STR(BT_LE_ADVERTISING_EVENT_START)
    CASE_RETURN_STR(BT_LE_ADVERTISING_SET_STARTED_EVENT)
    CASE_RETURN_STR(BT_LE_ADVERTISING_ENABLED_EVENT)
    CASE_RETURN_STR(BT_LE_ADVERTISING_DATA_SET_EVENT)
    CASE_RETURN_STR(BT_LE_SCAN_RESP_DATA_SET_EVENT)
    CASE_RETURN_STR(BT_LE_ADV_PARAM_UPDATED_EVENT)
    CASE_RETURN_STR(BT_LE_PERIODIC_ADV_PARAM_UPDATED_EVENT)
    CASE_RETURN_STR(BT_LE_PERIODIC_ADVERTISING_DATA_SET_EVENT)
    CASE_RETURN_STR(BT_LE_PERIODIC_ADVERTISING_ENABLED_EVENT)
    CASE_RETURN_STR(BT_LE_OWN_ADDRESS_READ_EVENT)
    CASE_RETURN_STR(BT_ADV_API_MAX)
    default:
      return "LE_ADV_UNKNOWN_MSG_ID";
  }
}

const char* dump_search_scanner_msgID(uint16_t msgID) {
  switch (msgID) {
    CASE_RETURN_STR(BT_LE_SCAN_REGISTER_SCANNER)
    CASE_RETURN_STR(BT_LE_SCAN_FILTER_ENABLE)
    CASE_RETURN_STR(BT_LE_SCAN_FILTER_ADD)
    CASE_RETURN_STR(BT_LE_SCAN_FILTER_PARAM_SETUP)
    CASE_RETURN_STR(BT_LE_SCAN_FILTER_CLEAR)
    CASE_RETURN_STR(BT_LE_SCAN_SET_SCAN_PARAM)
    CASE_RETURN_STR(BT_LE_SCAN_START_SCANNING)
    CASE_RETURN_STR(BT_LE_SCAN_BATCH_SCAN_CONFIG)
    CASE_RETURN_STR(BT_LE_SCAN_BATCH_SCAN_ENABLE)
    CASE_RETURN_STR(BT_LE_SCAN_BATCH_SCAN_DISABLE)
    CASE_RETURN_STR(BT_LE_SCAN_BATCH_SCAN_READ_REPORT)
    CASE_RETURN_STR(BT_LE_SCAN_UNREGISTER_SCANNER)
    CASE_RETURN_STR(BT_LE_SCAN_REGISTER_SCANNER_EVENT)
    CASE_RETURN_STR(BT_LE_SCAN_FILTER_ENABLE_EVENT)
    CASE_RETURN_STR(BT_LE_SCAN_FILTER_ADD_EVENT)
    CASE_RETURN_STR(BT_LE_SCAN_FILTER_CLEAR_EVENT)
    CASE_RETURN_STR(BT_LE_SCAN_FILTER_PARAM_SETUP_EVENT)
    CASE_RETURN_STR(BT_LE_SCAN_SET_SCAN_PARAM_EVENT)
    CASE_RETURN_STR(BT_LE_SCAN_RESULT_EVENT)
    CASE_RETURN_STR(BT_LE_SCAN_BATCH_SCAN_CONFIG_EVENT)
    CASE_RETURN_STR(BT_LE_SCAN_BATCH_SCAN_ENABLE_EVENT)
    CASE_RETURN_STR(BT_LE_SCAN_BATCH_SCAN_DISABLE_EVENT)
    CASE_RETURN_STR(BT_LE_SCAN_BATCH_SCAN_READ_REPORT_EVENT)
    CASE_RETURN_STR(BT_LE_SCAN_BATCH_TRACK_ADV_EVENT)
    CASE_RETURN_STR(BT_LE_SCAN_BATCH_THRESHOLD_EVENT)
    CASE_RETURN_STR(BT_LE_SCAN_API_MAX)
    default:
      return "LE_SCAN_UNKNOWN_MSG_ID";
  }
}

BluetoothSSTransport::BluetoothSSTransport() : mFd(-1) {
    ALOGI("BluetoothSSTransport ctor");
}

BluetoothSSTransport::~BluetoothSSTransport() {
    ALOGI("BluetoothSSTransport dtor");
    if (-1 != mFd) {
        ::close(mFd);
        mFd = -1;
    }
}

int BluetoothSSTransport::open(const std::string& dev) {
    int tries = MAX_CONN_RETRIES;
    mDeviceName = dev;
    ALOGI("open '%s'", mDeviceName.c_str());

    do {
        mFd = ::open(mDeviceName.c_str(), O_RDWR);
        --tries;

        if (mFd > 0) {
            ALOGI("open: %s: success fd=%d", dev.c_str(), mFd);
            break;
        }

        ALOGE("open: %s: open error(%s)", dev.c_str(), strerror(errno));

        if (-ETIMEDOUT == errno) {
            ALOGE("open: %s: ETIMEDOUT", dev.c_str());
            sleep(1);
        } else {
            ALOGE("open: %s: giving up", dev.c_str());
            break;
        }

    } while(-ETIMEDOUT == errno && tries > 0 );

    return mFd;
}

int BluetoothSSTransport::close() {
    ALOGI("close");
    if (mFd >= 0)  {
        ::close(mFd);
        mFd = -1;
    }
    return 0;
}

int BluetoothSSTransport::queueRxIntent(
  uint32_t size, uint32_t num_rx_intents) {
  int rc = 0;

  if (mFd < 0) {
      ALOGE("queueRxIntent failed as fd is invalid");
      return -1;
  }

  for (uint32_t i = 0; i < num_rx_intents; ++i) {
      rc = ioctl(mFd, PKT_IOCTL_QUEUE_RX_INTENT, &size);
      if (rc < 0) {
          ALOGE("queueRxIntent: fd[%d]: (error %s)", mFd, strerror(errno));
      }
  }

  return rc;
}

int BluetoothSSTransport::poll(int timeout) {
  ssize_t rc = 0;
  struct pollfd poll_fd;

  // wait for Rx data available in fd, for 'timeout' secs
  poll_fd.fd = mFd;
  poll_fd.events = POLLIN | POLLHUP;

  rc = ::poll(&poll_fd, 1, timeout * 1000);

  if(rc > 0) {
    if (poll_fd.revents & POLLIN) {
      return 0;
    }

    if (poll_fd.revents & POLLHUP) {
      ALOGE("poll: SSR detected");
    }
  }

  return -1;
}

int BluetoothSSTransport::file_write(uint8_t *buf, size_t buflen, const char *msgtyp){

    uint16_t MSG_ID = buf[0] + (((int)(buf[1]))<<8);
    const char *tmpBuf =NULL;

    if(MSG_ID >= BT_DM_EVT_START &&  MSG_ID <= BT_DM_EVT_MAX)
        tmpBuf = dump_search_dm_msgID(MSG_ID);
    else if(MSG_ID >= BT_SDP_EVT_START && MSG_ID <= BT_SDP_EVT_MAX)
        tmpBuf = dump_search_sdp_msgID(MSG_ID);
    else if(MSG_ID >= BT_RFCOMM_EVT_START && MSG_ID <= BT_RFCOMM_EVT_MAX)
        tmpBuf = dump_search_rfcomm_msgID(MSG_ID);
    else if(MSG_ID >= BT_LE_ADV_API_START && MSG_ID <= BT_ADV_EVT_MAX)
        tmpBuf = dump_search_advertiser_msgID(MSG_ID);
    else if(MSG_ID >= BT_LE_SCAN_API_START && MSG_ID <= BT_LE_SCAN_EVT_MAX)
        tmpBuf = dump_search_scanner_msgID(MSG_ID);
    else if(MSG_ID >= BT_LE_GATT_CLIENT_API_START && MSG_ID <= BT_LE_GATT_CLIENT_EVT_MAX)
        tmpBuf = dump_search_gattc_msgID(MSG_ID);
    else if(MSG_ID >= BT_LE_SERVER_SERVER_START && MSG_ID <= BT_LE_GATT_SERVER_EVT_MAX)
        tmpBuf = dump_search_gatts_msgID(MSG_ID);

    if (fptr == NULL)
    {
        ALOGE("The file does not exist");
        return -1;
    } else {
        fptr=fopen(BT_DATA_PATH, "a+");
        if(fileHeader)
        {
            fileHeader=false;
            fprintf(fptr,"/*******************************************************************************||G-LINK DATA||*******************************************************************************/\n");
        }
        gettimeofday(&now, NULL);
        localTime = localtime(&now.tv_sec);
        fprintf(fptr,"%.4d-%.2d-%.2d %.2d:%.2d:%.2d.%.3ld %s %44s ",1900 + localTime->tm_year,1 + localTime->tm_mon,localTime->tm_mday,localTime->tm_hour, localTime->tm_min, localTime->tm_sec,now.tv_usec / 1000, msgtyp , tmpBuf );
        for (int i=0; i< (int)buflen; ++i){
            int ret1=fprintf(fptr,"%.2x",(int)buf[i]);
        }
        fprintf(fptr,"\n");
        fclose(fptr);
    }
    return buflen;
}
int BluetoothSSTransport::read(uint8_t *data, size_t size) {
  ssize_t rc = 0;

  if (mFd < 0) {
    return -1;
  }

  rc = ::read(mFd, data, size);
  if (rc < 0) {
    if (errno != EAGAIN) {
      ALOGE("read: Read error: %s, rc %d", strerror(errno), (int)rc);
      return -1;
    }
  }
  else if (rc == 0) {
    ALOGE("read: Zero length packet received or hardware connection went off");
  }
  // For debugging. Comment out later
  {
    std::ostringstream hstr;
    for (int i=0; i< rc; ++i)
    {
      hstr  << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(data[i]);
    }

    ALOGD("read string: read [%s]", hstr.str().c_str());

    for(int i=1; i<=8;i++)
    {
      ALOGD("last bytes=%d",data[size-i]);
    }
  }
  ALOGD("read: read %d of %d bytes", (int)rc, (int)size);
  return rc;
}

int BluetoothSSTransport::write(uint8_t *buf, size_t buflen, size_t *bytes_written) {
  size_t bytes_written_out = 0;
  int rc = 0;

  if (bytes_written) {
    *bytes_written = 0;
  }

  if (mFd < 0) {
    ALOGE("write: invalid fd");
    return -1;
  }

  if (buflen <= 0) {
    ALOGE("write: nothing to do");
    return 0;
  }

  while (bytes_written_out < buflen) {
    ALOGI("write: writing %d bytes", (int)(buflen-bytes_written_out));
    rc = ::write (mFd, buf+bytes_written_out, buflen-bytes_written_out);
    ALOGI("write: wrote %d bytes", rc);
    if (rc < 0) {
      ALOGE("write: Write returned failure %d errno=%d", rc, errno);
      return -1;
    }

    // For debugging. Comment out later
    {
      int offset = bytes_written_out;
      std::ostringstream hstr;
      for (int i=0; i< rc; ++i)
      {
        hstr  << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(buf[i+offset]);
      }

      ALOGD("write: wrote [%s]", hstr.str().c_str());
    }

    bytes_written_out += rc;
    ALOGD("write: total written %d bytes", bytes_written_out);
  };

  if (bytes_written) {
    *bytes_written = bytes_written_out;
  }
  return 0;
}

Wakelock::Wakelock(const std::string& lockStr) :
  mLockStr(!lockStr.empty() ? lockStr : "BtOffloadDefault") {
    ALOGI("Wakelock lockStr=%s acquire", mLockStr.c_str());
    wakeLockAcquireOrRelease(true);
}

Wakelock::~Wakelock() {
    ALOGI("Wakelock lockStr=%s release", mLockStr.c_str());
    wakeLockAcquireOrRelease(false);
}

void Wakelock::wakeLockAcquireOrRelease (bool lockRequest) {
    int result = -1;
    int fd = -1;
    do {
        // TG: TODO sepolicy for wakefile
        if (lockRequest == true) {
            fd = ::open(WAKE_LOCK_FILE, O_WRONLY|O_APPEND);
        }
        else {
            fd = ::open(WAKE_UNLOCK_FILE, O_WRONLY|O_APPEND);
        }
        if (fd < 0) {
            ALOGE("wakeLockAcquireOrRelease: open error: %d, %s",
                  errno, strerror(errno));
            result = -2;
            break;
        }

        result = ::write(fd, mLockStr.c_str(), mLockStr.length());
        if (result != (int)mLockStr.length()) {
            ALOGE("wakeLockAcquireOrRelease: write error: %d, %s \n",
                  errno, strerror (errno));
            result = -4;
            break;
        }
        ALOGI("wakeLockAcquireOrRelease: req=%d lockStr=%s, len=%d",
              lockRequest, mLockStr.c_str(), (int)mLockStr.length());
        result = 0;
    } while (0);
    if (fd >= 0) {
        ::close (fd);
        fd = -1;
    }
    if (result != 0) {
        ALOGE("wakeLockAcquireOrRelease: failed for lock/unlock %d, result = %d \n",
              lockRequest, result);
    }
}
