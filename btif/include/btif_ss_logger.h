/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

#ifndef __BTIF_SS_LOGGER__
#define __BTIF_SS_LOGGER__

#pragma once

#include <vector>
#include <string>
#include <stack>

#define LOG_YEAR_LENGTH 4
#define LOG_TS_UNIT_LENGTH 2
#define LOG_FILE_TS_LENGTH 23
#define LOGS_EXTN ".log"
#define SNOOP_EXTN ".cfa"
#define FW_DUMP_EXTN ".bin"
#define LOGS_TS           "%.04d-%.02d-%.02d_%.02d-%.02d-%.02d"
#define LOG_COLLECTION_DIR "/data/vendor/ssrdump/"
#define SOC_DUMP_PREFIX "ss_ramdump_bt_fw_crashdump_"
#define SOC_DUMP_PATH LOG_COLLECTION_DIR SOC_DUMP_PREFIX LOGS_TS FW_DUMP_EXTN
#define SNOOP_FILE_PATH  LOG_COLLECTION_DIR SNOOP_FILE_NAME_PREFIX LOGS_TS SNOOP_EXTN

#define BT_STATE_FILE_PATH LOG_COLLECTION_DIR BT_STATE_FILE_NAME_PREFIX LOGS_TS LOGS_EXTN
#define SOC_DUMP_PATH_BUF_SIZE 255

#define MAX_BUFF_SIZE (64*1024)
#define BT_SSR_EVT_SEQ_START       0x0000
#define BT_SSR_EVT_SEQ_START_NEXT  0x0001
#define BT_SSR_EVT_SEQ_BEFORE_STOP 0xFFFE
#define BT_SSR_EVT_SEQ_STOP        0xFFFF

enum PrimaryReasonCode  {
  BT_HOST_REASON_DEFAULT_NONE  = 0x00,                         //INVALID REASON
};

class btif_ss_logger
{
 private:
  void SS_SaveSlateMemDump(uint8_t *, uint16_t, PrimaryReasonCode);
  int time_year, time_month, time_day, time_hour, time_min, time_sec;
  std::vector<std::string> dump_files;

 public:
  void SS_SetTimestampForDump();
  void SS_GetCrashDumpFileName(char*);
};
#endif //__BTIF_SS_LOGGER__
