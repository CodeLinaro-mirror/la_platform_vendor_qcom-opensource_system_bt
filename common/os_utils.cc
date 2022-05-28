/*
 * Copyright 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef OS_ANDROID
#include <private/android_filesystem_config.h>
#include <unistd.h>
#endif

#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include "gd/common/init_flags.h"

using ::bluetooth::common::InitFlags;

bool is_bluetooth_uid() {
#ifdef OS_ANDROID
  return getuid() == AID_BLUETOOTH;
#else
  return false;
#endif
}

int get_adapter_index() {
  return InitFlags::GetAdapterIndex();
}

bool is_default_bluetooth() {
  int hci_adapter = get_adapter_index();
  return hci_adapter == 0;
}

bool create_folder(const char* path_name) {
  if (access(path_name, F_OK) < 0) {
    if (mkdir(path_name, S_IRWXU | S_IRWXG | S_IRWXO) < 0) {
      return false;
    }
  }
  return true;
}
