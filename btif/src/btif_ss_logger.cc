/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

#define LOG_TAG "btif_ss_logger"

#include <fcntl.h>
#include <errno.h>
#include <utils/Log.h>
#include <cutils/properties.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <iomanip>

#include "btif_ss_logger.h"


void btif_ss_logger::SS_SetTimestampForDump()
{
  time_t t = time(NULL);
  struct tm *tm = localtime(&t);
  if (tm) {
    time_year = tm->tm_year + 1900;
    time_month = tm->tm_mon+ 1;
    time_day = tm->tm_mday;
    time_hour = tm->tm_hour;
    time_min = tm->tm_min;
    time_sec = tm->tm_sec;
  }
}



void btif_ss_logger :: SS_GetCrashDumpFileName(char* path)
{
  snprintf(path, SOC_DUMP_PATH_BUF_SIZE, SOC_DUMP_PATH, time_year, time_month, time_day, time_hour, time_min, time_sec);
  btif_ss_logger:: dump_files.push_back(std::string(path));
}

void btif_ss_logger :: SS_SaveSlateMemDump(uint8_t *eventBuf, uint16_t packet_len, PrimaryReasonCode reason)
{
  unsigned short seq_num = 0;
  static unsigned short seq_num_cnt = 0;
  static unsigned int dump_size = 0, total_size = 0;
  static char *temp_buf, *p, path[SOC_DUMP_PATH_BUF_SIZE + 1] = {'\0'};
  static int dump_fd = -1;
  char nullBuff[255] = { 0 };
  uint8_t *dump_ptr = NULL;

  dump_ptr = &eventBuf[7];
  seq_num = eventBuf[4] | (eventBuf[5] << 8);
  packet_len -= 7;
  long bytes_written = -1;

  ALOGV("%s: LOG_BT_MESSAGE_TYPE_MEM_DUMP (%d) ", __func__, seq_num);

  if ((seq_num != seq_num_cnt) && seq_num != BT_SSR_EVT_SEQ_STOP) {
    ALOGE("%s: current sequence number: %d, expected seq num: %d ", __func__,
          seq_num, seq_num_cnt);
  }
  if (seq_num == 0x0000) {
    SS_SetTimestampForDump();
    dump_size = (unsigned int)
                (eventBuf[7] | (eventBuf[8] << 8) | (eventBuf[9] << 16) | (eventBuf[10] << 24));
    dump_ptr = &eventBuf[11];
    total_size = seq_num_cnt = 0;
    packet_len -= 4;

    //memset(path, 0, SOC_DUMP_PATH_BUF_SIZE);
    /* first pack has total ram dump size (4 bytes) */
    ALOGD("%s: Crash Dump Start - total Size: %d ", __func__, dump_size);
    p = temp_buf = (char*)malloc(dump_size);
    if (p != NULL) {
      memset(p, 0, dump_size);
    }
    else {
      ALOGE("Failed to allocate mem for the crash dump size: %d", dump_size);
    }

    SS_GetCrashDumpFileName(path);
    dump_fd = open(path, O_CREAT | O_SYNC | O_WRONLY,  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (dump_fd < 0) {
      ALOGE("%s: File open (%s) failed: errno: %d", __func__, path, errno);
      seq_num_cnt++;
      return;
    }
    ALOGV("%s: File Open (%s) successfully ", __func__, path);
  }

  if (dump_fd >= 0) {
    for (; (seq_num > seq_num_cnt) && (seq_num != BT_SSR_EVT_SEQ_STOP); seq_num_cnt++) {
      ALOGE("%s: controller missed packet : %d, write null (%d) into file ",
            __func__, seq_num_cnt, packet_len);

      if (p != NULL) {
        memcpy(temp_buf, nullBuff, packet_len);
        temp_buf = temp_buf + packet_len;
      }
    }

    if (p != NULL) {
      memcpy(temp_buf, dump_ptr, packet_len);
      temp_buf = temp_buf + packet_len;
    }
    total_size += packet_len;
  }

  seq_num_cnt++;
  if (seq_num == BT_SSR_EVT_SEQ_STOP && p != NULL) {
    ALOGE("Writing crash dump of size %d bytes", total_size);
    bytes_written = write(dump_fd, p, total_size);
    if (bytes_written != (long)total_size)
      ALOGE("%s: Error writing crash dump: %ld (%s)", __func__, bytes_written, strerror(errno));

    free(p);
    temp_buf = NULL;
    p = NULL;
    seq_num_cnt = 0;
  }

  if (seq_num == BT_SSR_EVT_SEQ_STOP) {
    if (dump_fd >= 0) {
      if ( fsync(dump_fd) < 0 ) {
        ALOGE("%s: File flush (%s) failed: %s, errno: %d", __func__,
              path, strerror(errno), errno);
      }
      close(dump_fd);
      dump_fd = -1;

      ALOGI("%s: Write crashdump successfully : \n"
            "\t\tFile: %s\n\t\tdump_size: %d\n\t\twritten_size: %ld",
            __func__, path, dump_size, bytes_written);
    }
  }
}
