/*
 * Copyright 2022 The Android Open Source Project
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
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.

    * Redistribution and use in source and binary forms, with or without
      modification, are permitted (subject to the limitations in the
      disclaimer below) provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
*/

#pragma once

#include "audio_aidl_interfaces.h"
#include "transport_instance.h"

namespace bluetooth {
namespace audio {
namespace aidl {

using ::aidl::android::hardware::audio::common::SinkMetadata;
using ::aidl::android::hardware::audio::common::SourceMetadata;
using ::aidl::android::hardware::bluetooth::audio::BnBluetoothAudioPort;
using ::aidl::android::hardware::bluetooth::audio::IBluetoothAudioProvider;
using ::aidl::android::hardware::bluetooth::audio::LatencyMode;
using ::aidl::android::hardware::bluetooth::audio::PresentationPosition;

class BluetoothAudioPortImpl : public BnBluetoothAudioPort {
 public:
  BluetoothAudioPortImpl(
      IBluetoothTransportInstance* transport_instance,
      const std::shared_ptr<IBluetoothAudioProvider>& provider);

  ndk::ScopedAStatus startStream(bool is_low_latency) override;

  ndk::ScopedAStatus suspendStream() override;

  ndk::ScopedAStatus stopStream() override;

  ndk::ScopedAStatus getPresentationPosition(
      PresentationPosition* _aidl_return) override;

  ndk::ScopedAStatus updateSourceMetadata(
      const SourceMetadata& source_metadata) override;

  ndk::ScopedAStatus updateSinkMetadata(
      const SinkMetadata& sink_metadata) override;

  ndk::ScopedAStatus setLatencyMode(LatencyMode latency_mode) override;

 protected:
  virtual ~BluetoothAudioPortImpl();

  IBluetoothTransportInstance* transport_instance_;
  const std::shared_ptr<IBluetoothAudioProvider> provider_;
  PresentationPosition::TimeSpec timespec_convert_to_hal(const timespec& ts);
};

}  // namespace aidl
}  // namespace audio
}  // namespace bluetooth
