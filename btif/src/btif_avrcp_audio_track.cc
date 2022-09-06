/*
 * Copyright 2015 The Android Open Source Project
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

//#define LOG_NDEBUG 0
#define LOG_TAG "bt_btif_avrcp_audio_track"

#include "btif_avrcp_audio_track.h"

#include <aaudio/AAudio.h>
#include <media/AudioTrack.h>
#include <base/logging.h>
#include <utils/StrongPointer.h>

#include "bt_target.h"
#include "osi/include/log.h"
#include "osi/include/properties.h"
#include "stack/include/a2dp_constants.h"

using namespace android;

#if (A2DP_SINK_DELAY_REPORT == TRUE)
static bool isDelayReportSupported(void);
#endif

typedef struct {
  AAudioStream* stream;
  int bitsPerSample;
  int channelCount;
  float* buffer;
  size_t bufferLength;
} BtifAvrcpAudioTrack;

#if (A2DP_SINK_DELAY_REPORT == TRUE)
typedef struct {
  android::sp<android::AudioTrack> track;
} BtifAvrcpLegacyAudioTrack;
#endif

#if (DUMP_PCM_DATA == TRUE)
FILE* outputPcmSampleFile;
char outputFilename[50] = "/data/misc/bluedroid/output_sample.pcm";
#endif

// Utilize Audiotrack instead if delayReport is supported.
// This is because AAudio does not support retrieving audio latency
#if (A2DP_SINK_DELAY_REPORT == TRUE)
static const bool delayReportSupported = isDelayReportSupported();
#endif

#if (A2DP_SINK_DELAY_REPORT == TRUE)
void* BtifAvrcpLegacyAudioTrackCreate(int trackFreq, int bits_per_sample,
                                int channelType) {
  audio_format_t format;
  switch (bits_per_sample) {
    default:
    case 16:
      format = AUDIO_FORMAT_PCM_16_BIT;
      break;
    case 24:
      format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
      break;
    case 32:
      format = AUDIO_FORMAT_PCM_32_BIT;
      break;
  }
  LOG_VERBOSE("%s Track.cpp: btCreateTrack freq %d format 0x%x channel %d ",
              __func__, trackFreq, format, channelType);

  sp<android::AudioTrack> track = new android::AudioTrack(
      AUDIO_STREAM_MUSIC, trackFreq, format, (audio_channel_mask_t)channelType,
      (size_t)0 /*frameCount*/, (audio_output_flags_t)AUDIO_OUTPUT_FLAG_DEEP_BUFFER,
      NULL /*callback_t*/, NULL /*void* user*/, 0 /*notificationFrames*/,
      AUDIO_SESSION_ALLOCATE, android::AudioTrack::TRANSFER_SYNC);
  CHECK(track != NULL);

  BtifAvrcpLegacyAudioTrack* trackHolder = new BtifAvrcpLegacyAudioTrack;
  CHECK(trackHolder != NULL);
  trackHolder->track = track;

  if (trackHolder->track->initCheck() != 0) {
    return nullptr;
  }

#if (DUMP_PCM_DATA == TRUE)
  outputPcmSampleFile = fopen(outputFilename, "ab");
  if (!outputPcmSampleFile) {
    LOG_ERROR("%s: Create file %s failed:%s", __func__, outputFilename, strerror(errno));
  }
#endif
  trackHolder->track->setVolume(1, 1);
  return (void*)trackHolder;
}
#endif

void* BtifAvrcpAudioTrackCreate(int trackFreq, int bitsPerSample,
                                int channelCount, int channelType) {
#if (A2DP_SINK_DELAY_REPORT == TRUE)
  if (delayReportSupported == true) {
    return BtifAvrcpLegacyAudioTrackCreate(trackFreq, bitsPerSample, channelType);
  }
#endif

  LOG_VERBOSE("%s Track.cpp: btCreateTrack freq %d bps %d channel %d ",
              __func__, trackFreq, bitsPerSample, channelCount);

  AAudioStreamBuilder* builder;
  AAudioStream* stream;
  aaudio_result_t result = AAudio_createStreamBuilder(&builder);
  AAudioStreamBuilder_setSampleRate(builder, trackFreq);
  AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
  AAudioStreamBuilder_setChannelCount(builder, channelCount);
  AAudioStreamBuilder_setSessionId(builder, AAUDIO_SESSION_ID_ALLOCATE);
  AAudioStreamBuilder_setPerformanceMode(builder,
                                         AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  result = AAudioStreamBuilder_openStream(builder, &stream);
  // CHECK(result == AAUDIO_OK);
  /* Return nullptr if Audio framework isn't activated */
  if (result != AAUDIO_OK) {
    return nullptr;
  }
  AAudioStreamBuilder_delete(builder);

  BtifAvrcpAudioTrack* trackHolder = new BtifAvrcpAudioTrack;
  CHECK(trackHolder != NULL);
  trackHolder->stream = stream;
  trackHolder->bitsPerSample = bitsPerSample;
  trackHolder->channelCount = channelCount;
  trackHolder->bufferLength =
      trackHolder->channelCount * AAudioStream_getBufferSizeInFrames(stream);
  trackHolder->buffer = new float[trackHolder->bufferLength]();

#if (DUMP_PCM_DATA == TRUE)
  outputPcmSampleFile = fopen(outputFilename, "ab");
  if (!outputPcmSampleFile) {
    LOG_ERROR("%s: Create file %s failed:%s", __func__, outputFilename, strerror(errno));
  }
#endif
  return (void*)trackHolder;
}

#if (A2DP_SINK_DELAY_REPORT == TRUE)
int BtifAvrcpAudioTrackLatency(void* handle) {
  if (handle == NULL) {
    LOG_ERROR("%s: handle is null!", __func__);
    return 0;
  }

#if (A2DP_SINK_DELAY_REPORT == TRUE)
  if (delayReportSupported != true) {
    LOG_INFO("%s: delay report is NOT supported!", __func__);
    return 0;
  }
#endif

  BtifAvrcpLegacyAudioTrack* trackHolder = static_cast<BtifAvrcpLegacyAudioTrack*>(handle);
  CHECK(trackHolder != NULL);
  CHECK(trackHolder->track != NULL);
  LOG_VERBOSE("%s: get latency", __func__);
  return trackHolder->track->latency();
}
#endif

#if (A2DP_SINK_DELAY_REPORT == TRUE)
void BtifAvrcpLegacyAudioTrackStart(void* handle) {
  BtifAvrcpLegacyAudioTrack* trackHolder = static_cast<BtifAvrcpLegacyAudioTrack*>(handle);
  CHECK(trackHolder != NULL);
  CHECK(trackHolder->track != NULL);
  LOG_VERBOSE("%s Track.cpp: btStartTrack", __func__);
  trackHolder->track->start();
}
#endif

void BtifAvrcpAudioTrackStart(void* handle) {
  if (handle == NULL) {
    LOG_ERROR("%s: handle is null!", __func__);
    return;
  }

#if (A2DP_SINK_DELAY_REPORT == TRUE)
  if (delayReportSupported == true) {
    return BtifAvrcpLegacyAudioTrackStart(handle);
  }
#endif

  BtifAvrcpAudioTrack* trackHolder = static_cast<BtifAvrcpAudioTrack*>(handle);
  CHECK(trackHolder != NULL);
  CHECK(trackHolder->stream != NULL);
  LOG_VERBOSE("%s Track.cpp: btStartTrack", __func__);
  AAudioStream_requestStart(trackHolder->stream);
}

#if (A2DP_SINK_DELAY_REPORT == TRUE)
void BtifAvrcpLegacyAudioTrackStop(void* handle) {
  BtifAvrcpLegacyAudioTrack* trackHolder = static_cast<BtifAvrcpLegacyAudioTrack*>(handle);
  if (trackHolder != NULL && trackHolder->track != NULL) {
    LOG_VERBOSE("%s Track.cpp: btStartTrack", __func__);
    trackHolder->track->stop();
  }
}
#endif

void BtifAvrcpAudioTrackStop(void* handle) {
  if (handle == NULL) {
    LOG_INFO("%s handle is null.", __func__);
    return;
  }

#if (A2DP_SINK_DELAY_REPORT == TRUE)
  if (delayReportSupported == true) {
    return BtifAvrcpLegacyAudioTrackStop(handle);
  }
#endif

  BtifAvrcpAudioTrack* trackHolder = static_cast<BtifAvrcpAudioTrack*>(handle);
  if (trackHolder != NULL && trackHolder->stream != NULL) {
    LOG_VERBOSE("%s Track.cpp: btStartTrack", __func__);
    AAudioStream_requestStop(trackHolder->stream);
  }
}

#if (A2DP_SINK_DELAY_REPORT == TRUE)
void BtifAvrcpLegacyAudioTrackDelete(void* handle) {
  BtifAvrcpLegacyAudioTrack* trackHolder = static_cast<BtifAvrcpLegacyAudioTrack*>(handle);
  if (trackHolder != NULL && trackHolder->track != NULL) {
    LOG_VERBOSE("%s Track.cpp: btStartTrack", __func__);
    delete trackHolder;
  }

#if (DUMP_PCM_DATA == TRUE)
  if (outputPcmSampleFile) {
    fclose(outputPcmSampleFile);
  }
  outputPcmSampleFile = NULL;
#endif
}
#endif

void BtifAvrcpAudioTrackDelete(void* handle) {
  if (handle == NULL) {
    LOG_INFO("%s handle is null.", __func__);
    return;
  }

#if (A2DP_SINK_DELAY_REPORT == TRUE)
  if (delayReportSupported == true) {
    return BtifAvrcpLegacyAudioTrackDelete(handle);
  }
#endif

  BtifAvrcpAudioTrack* trackHolder = static_cast<BtifAvrcpAudioTrack*>(handle);
  if (trackHolder != NULL && trackHolder->stream != NULL) {
    LOG_VERBOSE("%s Track.cpp: btStartTrack", __func__);
    AAudioStream_close(trackHolder->stream);
    delete trackHolder->buffer;
    delete trackHolder;
  }

#if (DUMP_PCM_DATA == TRUE)
  if (outputPcmSampleFile) {
    fclose(outputPcmSampleFile);
  }
  outputPcmSampleFile = NULL;
#endif
}

#if (A2DP_SINK_DELAY_REPORT == TRUE)
void BtifAvrcpLegacyAudioTrackPause(void* handle) {
  BtifAvrcpLegacyAudioTrack* trackHolder = static_cast<BtifAvrcpLegacyAudioTrack*>(handle);
  if (trackHolder != NULL && trackHolder->track != NULL) {
    LOG_VERBOSE("%s Track.cpp: btStartTrack", __func__);
    trackHolder->track->pause();
    trackHolder->track->flush();
  }
}
#endif

void BtifAvrcpAudioTrackPause(void* handle) {
  if (handle == NULL) {
    LOG_INFO("%s handle is null.", __func__);
    return;
  }

#if (A2DP_SINK_DELAY_REPORT == TRUE)
  if (delayReportSupported == true) {
    return BtifAvrcpLegacyAudioTrackPause(handle);
  }
#endif

  BtifAvrcpAudioTrack* trackHolder = static_cast<BtifAvrcpAudioTrack*>(handle);
  if (trackHolder != NULL && trackHolder->stream != NULL) {
    LOG_VERBOSE("%s Track.cpp: btPauseTrack", __func__);
    AAudioStream_requestPause(trackHolder->stream);
    AAudioStream_requestFlush(trackHolder->stream);
  }
}

#if (A2DP_SINK_DELAY_REPORT == TRUE)
void BtifAvrcpSetLegacyAudioTrackGain(void* handle, float gain) {
  BtifAvrcpLegacyAudioTrack* trackHolder = static_cast<BtifAvrcpLegacyAudioTrack*>(handle);
  if (trackHolder != NULL && trackHolder->track != NULL) {
    LOG_VERBOSE("%s set gain %f", __func__, gain);
    trackHolder->track->setVolume(gain);
  }
}
#endif

void BtifAvrcpSetAudioTrackGain(void* handle, float gain) {
  if (handle == NULL) {
    LOG_INFO("%s handle is null.", __func__);
    return;
  }

#if (A2DP_SINK_DELAY_REPORT == TRUE)
  if (delayReportSupported == true) {
    return BtifAvrcpSetLegacyAudioTrackGain(handle, gain);
  }
#endif

  // Does nothing right now
}

constexpr float kScaleQ15ToFloat = 1.0f / 32768.0f;
constexpr float kScaleQ23ToFloat = 1.0f / 8388608.0f;
constexpr float kScaleQ31ToFloat = 1.0f / 2147483648.0f;

static size_t sampleSizeFor(BtifAvrcpAudioTrack* trackHolder) {
  return trackHolder->bitsPerSample / 8;
}

static size_t transcodeQ15ToFloat(uint8_t* buffer, size_t length,
                                  BtifAvrcpAudioTrack* trackHolder) {
  size_t sampleSize = sampleSizeFor(trackHolder);
  size_t i = 0;
  for (; i <= length / sampleSize; i++) {
    trackHolder->buffer[i] = ((int16_t*)buffer)[i] * kScaleQ15ToFloat;
  }
  return i * sampleSize;
}

static size_t transcodeQ23ToFloat(uint8_t* buffer, size_t length,
                                  BtifAvrcpAudioTrack* trackHolder) {
  size_t sampleSize = sampleSizeFor(trackHolder);
  size_t i = 0;
  for (; i <= length / sampleSize; i++) {
    size_t offset = i * sampleSize;
    int32_t sample = *((int32_t*)(buffer + offset - 1)) & 0x00FFFFFF;
    trackHolder->buffer[i] = sample * kScaleQ23ToFloat;
  }
  return i * sampleSize;
}

static size_t transcodeQ31ToFloat(uint8_t* buffer, size_t length,
                                  BtifAvrcpAudioTrack* trackHolder) {
  size_t sampleSize = sampleSizeFor(trackHolder);
  size_t i = 0;
  for (; i <= length / sampleSize; i++) {
    trackHolder->buffer[i] = ((int32_t*)buffer)[i] * kScaleQ31ToFloat;
  }
  return i * sampleSize;
}

static size_t transcodeToPcmFloat(uint8_t* buffer, size_t length,
                                  BtifAvrcpAudioTrack* trackHolder) {
  switch (trackHolder->bitsPerSample) {
    case 16:
      return transcodeQ15ToFloat(buffer, length, trackHolder);
    case 24:
      return transcodeQ23ToFloat(buffer, length, trackHolder);
    case 32:
      return transcodeQ31ToFloat(buffer, length, trackHolder);
  }
  return -1;
}

#if (A2DP_SINK_DELAY_REPORT == TRUE)
static bool isDelayReportSupported(void) {
    char value[PROPERTY_VALUE_MAX] = {'\0'};
    osi_property_get("persist.bt.a2dp_sink.enable_delay_report", value, "true");
    if (strcmp(value, "true") == 0) {
      LOG_INFO("%s: delay report enabled for sink", __func__);
      return true;
    }
    return false;
}
#endif

constexpr int64_t kTimeoutNanos = 100 * 1000 * 1000;  // 100 ms

#if (A2DP_SINK_DELAY_REPORT == TRUE)
int BtifAvrcpLegacyAudioTrackWriteData(void* handle, void* audioBuffer,
                                 int bufferLength) {
  BtifAvrcpLegacyAudioTrack* trackHolder = static_cast<BtifAvrcpLegacyAudioTrack*>(handle);
  CHECK(trackHolder != NULL);
  CHECK(trackHolder->track != NULL);
  int retval = -1;
#if (DUMP_PCM_DATA == TRUE)
  if (outputPcmSampleFile) {
    fwrite((audioBuffer), 1, (size_t)bufferlen, outputPcmSampleFile);
  }
#endif
  retval = trackHolder->track->write(audioBuffer, (size_t)bufferLength);
  LOG_VERBOSE("%s Track.cpp: btWriteData len = %d ret = %d", __func__,
              bufferLength, retval);
  return retval;
}
#endif

int BtifAvrcpAudioTrackWriteData(void* handle, void* audioBuffer,
                                 int bufferLength) {
  if (handle == NULL) {
    LOG_ERROR("%s handle is null.", __func__);
    return 0;
  }

#if (A2DP_SINK_DELAY_REPORT == TRUE)
  if (delayReportSupported == true) {
    return BtifAvrcpLegacyAudioTrackWriteData(handle, audioBuffer, bufferLength);
  }
#endif

  BtifAvrcpAudioTrack* trackHolder = static_cast<BtifAvrcpAudioTrack*>(handle);
  CHECK(trackHolder != NULL);
  CHECK(trackHolder->stream != NULL);
  aaudio_result_t retval = -1;
#if (DUMP_PCM_DATA == TRUE)
  if (outputPcmSampleFile) {
    fwrite((audioBuffer), 1, (size_t)bufferLength, outputPcmSampleFile);
  }
#endif

  size_t sampleSize = sampleSizeFor(trackHolder);
  int transcodedCount = 0;
  do {
    transcodedCount +=
        transcodeToPcmFloat(((uint8_t*)audioBuffer) + transcodedCount,
                            bufferLength - transcodedCount, trackHolder);

    retval = AAudioStream_write(
        trackHolder->stream, trackHolder->buffer,
        transcodedCount / (sampleSize * trackHolder->channelCount),
        kTimeoutNanos);
    LOG_VERBOSE("%s Track.cpp: btWriteData len = %d ret = %d", __func__,
                bufferLength, retval);
  } while (transcodedCount < bufferLength);

  return transcodedCount;
}
