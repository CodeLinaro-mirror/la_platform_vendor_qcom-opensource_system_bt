/******************************************************************************
 *  Copyright (C) 2016, The Linux Foundation. All rights reserved.
 *
 *  Not a Contribution
 *****************************************************************************/
/*****************************************************************************
 *  Copyright (C) 2009-2012 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/
/*      bthost_ipc.c
 *
 *  Description:   Implements IPC interface between HAL and BT host
 *
 *****************************************************************************/

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cutils/str_parms.h>
#include <cutils/sockets.h>
#include <cutils/properties.h>
#include <system/audio.h>
#include <hardware/audio.h>

#include <hardware/hardware.h>
#include "bthost_ipc.h"
#include "bt_utils.h"


#define LOG_TAG "bthost_ipc"
#include "osi/include/log.h"


static int bt_split_a2dp_enabled = 0;
/*****************************************************************************
**  Constants & Macros
******************************************************************************/
#define STREAM_START_MAX_RETRY_COUNT 50
#define CTRL_CHAN_RETRY_COUNT 3
#define USEC_PER_SEC 1000000L

#define CASE_RETURN_STR(const) case const: return #const;

#define FNLOG()             LOG_VERBOSE("%s", __FUNCTION__);
#define DEBUG(fmt, ...)     LOG_VERBOSE("%s: " fmt,__FUNCTION__, ## __VA_ARGS__)
#define INFO(fmt, ...)      LOG_INFO("%s: " fmt,__FUNCTION__, ## __VA_ARGS__)
#define ERROR(fmt, ...)     LOG_ERROR("%s: " fmt,__FUNCTION__, ## __VA_ARGS__)

#define ASSERTC(cond, msg, val) if (!(cond)) {ERROR("### ASSERT : %s line %d %s (%d) ###", __FILE__, __LINE__, msg, val);}

/*****************************************************************************
**  Local type definitions
******************************************************************************/

struct a2dp_stream_common audio_stream;

/*****************************************************************************
**  Static functions
******************************************************************************/

audio_sbc_encoder_config sbc_codec;
audio_aptx_encoder_config aptx_codec;
/*****************************************************************************
**  Externs
******************************************************************************/

/*****************************************************************************
**  Functions
******************************************************************************/

/*****************************************************************************
**   Miscellaneous helper functions
******************************************************************************/
static const char* dump_a2dp_ctrl_event(char event)
{
    switch(event)
    {
        CASE_RETURN_STR(A2DP_CTRL_CMD_NONE)
        CASE_RETURN_STR(A2DP_CTRL_CMD_CHECK_READY)
        CASE_RETURN_STR(A2DP_CTRL_CMD_START)
        CASE_RETURN_STR(A2DP_CTRL_CMD_STOP)
        CASE_RETURN_STR(A2DP_CTRL_CMD_SUSPEND)
        CASE_RETURN_STR(A2DP_CTRL_CMD_OFFLOAD_SUPPORTED)
        CASE_RETURN_STR(A2DP_CTRL_CMD_OFFLOAD_NOT_SUPPORTED)
        CASE_RETURN_STR(A2DP_CTRL_CMD_CHECK_STREAM_STARTED)
        CASE_RETURN_STR(A2DP_CTRL_GET_CODEC_CONFIG)
        CASE_RETURN_STR(A2DP_CTRL_GET_MULTICAST_STATUS)
        default:
            return "UNKNOWN MSG ID";
    }
}

/* logs timestamp with microsec precision
   pprev is optional in case a dedicated diff is required */
static void ts_log(char *tag, int val, struct timespec *pprev_opt)
{
    struct timespec now;
    static struct timespec prev = {0,0};
    unsigned long long now_us;
    unsigned long long diff_us;
    UNUSED(tag);
    UNUSED(val);

    clock_gettime(CLOCK_MONOTONIC, &now);

    now_us = now.tv_sec*USEC_PER_SEC + now.tv_nsec/1000;

    if (pprev_opt)
    {
        diff_us = (now.tv_sec - prev.tv_sec) * USEC_PER_SEC + (now.tv_nsec - prev.tv_nsec)/1000;
        *pprev_opt = now;
        DEBUG("[%s] ts %08lld, *diff %08lld, val %d", tag, now_us, diff_us, val);
    }
    else
    {
        diff_us = (now.tv_sec - prev.tv_sec) * USEC_PER_SEC + (now.tv_nsec - prev.tv_nsec)/1000;
        prev = now;
        DEBUG("[%s] ts %08lld, diff %08lld, val %d", tag, now_us, diff_us, val);
    }
}


static const char* dump_a2dp_hal_state(int event)
{
    switch(event)
    {
        CASE_RETURN_STR(AUDIO_A2DP_STATE_STARTING)
        CASE_RETURN_STR(AUDIO_A2DP_STATE_STARTED)
        CASE_RETURN_STR(AUDIO_A2DP_STATE_STOPPING)
        CASE_RETURN_STR(AUDIO_A2DP_STATE_STOPPED)
        CASE_RETURN_STR(AUDIO_A2DP_STATE_SUSPENDED)
        CASE_RETURN_STR(AUDIO_A2DP_STATE_STANDBY)
        default:
            return "UNKNOWN STATE ID";
    }
}
static void* a2dp_codec_parser(uint8_t *codec_cfg, audio_format_t *codec_type)
{
    char byte,len;
    uint8_t *p_cfg = codec_cfg;
    INFO("%s",__func__);
    if (codec_cfg[CODEC_OFFSET] == CODEC_TYPE_SBC)
    {
        memset(&sbc_codec,0,sizeof(audio_sbc_encoder_config));
        p_cfg++;
        len = *p_cfg++;
        p_cfg++;//skip media type
        len--;
        p_cfg++;
        len--;
        byte = *p_cfg++;
        len--;
        switch (byte & A2D_SBC_FREQ_MASK)
        {
            case A2D_SBC_SAMP_FREQ_48:
                 sbc_codec.sampling_rate = 48000;
                 break;
            case A2D_SBC_SAMP_FREQ_44:
                 sbc_codec.sampling_rate = 44100;
                 break;
            case A2D_SBC_SAMP_FREQ_32:
                 sbc_codec.sampling_rate = 3200;
                 break;
            case A2D_SBC_SAMP_FREQ_16:
                 sbc_codec.sampling_rate = 16000;
                 break;
            default:
                 ERROR("Unkown sampling rate");
        }

        switch (byte & A2D_SBC_CHN_MASK)
        {
            case A2D_SBC_CH_MD_JOINT:
                 sbc_codec.channels = 3;
                 break;
            case A2D_SBC_CH_MD_STEREO:
                 sbc_codec.channels = 2;
                 break;
            case A2D_SBC_CH_MD_DUAL:
                 sbc_codec.channels = 1;
                 break;
            case A2D_SBC_CH_MD_MONO:
                 sbc_codec.channels = 0;
                 break;
            default:
                 ERROR("Unknow channel mode");
        }
        byte = *p_cfg++;
        len--;
        switch (byte & A2D_SBC_BLK_MASK)
        {
            case A2D_SBC_BLOCKS_16:
                sbc_codec.blk_len = 16;
                break;
            case A2D_SBC_BLOCKS_12:
                sbc_codec.blk_len = 12;
                break;
            case A2D_SBC_BLOCKS_8:
                sbc_codec.blk_len = 8;
                break;
            case A2D_SBC_BLOCKS_4:
                sbc_codec.blk_len = 4;
                break;
            default:
                ERROR("Unknown block length");
        }

        switch (byte & A2D_SBC_SUBBAND_MASK)
        {
            case A2D_SBC_SUBBAND_8:
                sbc_codec.subband = 8;
                break;
            case A2D_SBC_SUBBAND_4:
                sbc_codec.subband = 4;
                break;
            default:
                ERROR("Unknown subband");
        }
        switch (byte & A2D_SBC_ALLOC_MASK)
        {
            case A2D_SBC_ALLOC_MD_L:
                sbc_codec.alloc = 1;
                break;
            case A2D_SBC_ALLOC_MD_S:
                sbc_codec.alloc = 2;
            default:
                ERROR("Unknown alloc method");
        }
        sbc_codec.min_bitpool = *p_cfg++;
        len--;
        sbc_codec.max_bitpool = *p_cfg++;
        len--;
        if (len == 0)
            INFO("Copied codec config");
        sbc_codec.mtu = *p_cfg++;
        sbc_codec.mtu |= (*p_cfg++ << 8);
        sbc_codec.bitrate = *p_cfg++;
        sbc_codec.bitrate |= (*p_cfg++ << 8);
        sbc_codec.bitrate |= (*p_cfg++ << 16);
        sbc_codec.bitrate |= (*p_cfg++ << 24);
        *codec_type = AUDIO_FORMAT_SBC;
        INFO("Done copying full codec config");
        return ((void *)(&sbc_codec));
    }
    else if (codec_cfg[CODEC_OFFSET] == NON_A2DP_CODEC_TYPE)
    {
        if (codec_cfg[VENDOR_ID_OFFSET] == VENDOR_APTX &&
            codec_cfg[CODEC_ID_OFFSET] == APTX_CODEC_ID)
        {
            INFO("AptX-classic codec");
            *codec_type = AUDIO_FORMAT_APTX;
        }
        if (codec_cfg[VENDOR_ID_OFFSET] == VENDOR_APTX_HD &&
            codec_cfg[CODEC_ID_OFFSET] == APTX_HD_CODEC_ID)
        {
            INFO("AptX-HD codec");
            *codec_type = AUDIO_FORMAT_APTX_HD;
        }
        memset(&aptx_codec,0,sizeof(audio_aptx_encoder_config));
        aptx_codec.dev_idx = *p_cfg++;
        len = *p_cfg++;//LOSC
        p_cfg++; // Skip media type
        len--;
        aptx_codec.codec_type = *p_cfg++;
        len--;
        aptx_codec.vendor_id = *p_cfg++;
        aptx_codec.vendor_id |= (*p_cfg++ << 8);
        aptx_codec.vendor_id |= (*p_cfg++ << 16);
        aptx_codec.vendor_id |= (*p_cfg++ << 24);
        len -= 4;
        aptx_codec.codec_id = *p_cfg++;
        aptx_codec.codec_id |= (*p_cfg++ << 8);
        len--;
        byte = *p_cfg++;
        len--;
        switch (byte & A2D_APTX_SAMP_FREQ_MASK)
        {
            case A2D_APTX_SAMP_FREQ_48:
                 aptx_codec.sampling_rate = 48000;
                 break;
            case A2D_APTX_SAMP_FREQ_44:
                 aptx_codec.sampling_rate = 44100;
                 break;
            default:
                 ERROR("Unknown sampling rate");
        }
        switch (byte & A2D_APTX_CHAN_MASK)
        {
            case A2D_APTX_CHAN_STEREO:
                 aptx_codec.chnl = 2;
                 break;
            case A2D_APTX_CHAN_MONO:
                 aptx_codec.chnl = 1;
                 break;
            default:
                 ERROR("Unknown channel mode");
        }
        if (len == 0)
            INFO("Codec config copied");
        aptx_codec.mtu = *p_cfg++;
        aptx_codec.mtu |= (*p_cfg++ << 8);

        aptx_codec.bitrate = *p_cfg++;
        aptx_codec.bitrate |= (*p_cfg++ << 8);
        aptx_codec.bitrate |= (*p_cfg++ << 16);
        aptx_codec.bitrate |= (*p_cfg++ << 24);
        aptx_codec.cp = *p_cfg;
        return ((void *)&aptx_codec);
    }
    return -1;
}
/*****************************************************************************
**
**   bluedroid stack adaptation
**
*****************************************************************************/

static int skt_connect(char *path, size_t buffer_sz)
{
    int ret;
    int skt_fd;
    struct sockaddr_un remote;
    int len;

    INFO("connect to %s (sz %zu)", path, buffer_sz);

    skt_fd = socket(AF_LOCAL, SOCK_STREAM, 0);

    if(socket_local_client_connect(skt_fd, path,
            ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM) < 0)
    {
        ERROR("failed to connect (%s)", strerror(errno));
        close(skt_fd);
        return -1;
    }

    len = buffer_sz;
    ret = setsockopt(skt_fd, SOL_SOCKET, SO_SNDBUF, (char*)&len, (int)sizeof(len));

    /* only issue warning if failed */
    if (ret < 0)
        ERROR("setsockopt failed (%s)", strerror(errno));

    ret = setsockopt(skt_fd, SOL_SOCKET, SO_RCVBUF, (char*)&len, (int)sizeof(len));

    /* only issue warning if failed */
    if (ret < 0)
        ERROR("setsockopt failed (%s)", strerror(errno));

    INFO("connected to stack fd = %d", skt_fd);

    return skt_fd;
}

static int skt_read(int fd, void *p, size_t len)
{
    int read;
    struct pollfd pfd;
    struct timespec ts;

    FNLOG();

    ts_log("skt_read recv", len, NULL);

    if ((read = recv(fd, p, len, MSG_NOSIGNAL)) == -1)
    {
        ERROR("write failed with errno=%d\n", errno);
        return -1;
    }

    return read;
}

static int skt_write(int fd, const void *p, size_t len)
{
    int sent;
    struct pollfd pfd;

    FNLOG();

    pfd.fd = fd;
    pfd.events = POLLOUT;

    /* poll for 500 ms */

    /* send time out */
    if (poll(&pfd, 1, 500) == 0)
        return 0;

    ts_log("skt_write", len, NULL);

    if ((sent = send(fd, p, len, MSG_NOSIGNAL)) == -1)
    {
        ERROR("write failed with errno=%d\n", errno);
        return -1;
    }

    return sent;
}

static int skt_disconnect(int fd)
{
    INFO("fd %d", fd);

    if (fd != AUDIO_SKT_DISCONNECTED)
    {
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
    return 0;
}



/*****************************************************************************
**
**  AUDIO CONTROL PATH
**
*****************************************************************************/

int a2dp_ctrl_receive(struct a2dp_stream_common *common, void* buffer, int length)
{
    int ret = recv(common->ctrl_fd, buffer, length, MSG_NOSIGNAL);
    if (ret < 0)
    {
        ERROR("ack failed (%s)", strerror(errno));
        if (errno == EINTR)
        {
            /* retry again */
            ret = recv(common->ctrl_fd, buffer, length, MSG_NOSIGNAL);
            if (ret < 0)
            {
               ERROR("ack failed (%s)", strerror(errno));
               skt_disconnect(common->ctrl_fd);
               common->ctrl_fd = AUDIO_SKT_DISCONNECTED;
               return -1;
            }
        }
        else
        {
               skt_disconnect(common->ctrl_fd);
               common->ctrl_fd = AUDIO_SKT_DISCONNECTED;
               return -1;

        }
    }
    INFO("a2dp_ctrl_receive ret = %d",ret);
    return ret;
}

int a2dp_command(struct a2dp_stream_common *common, char cmd)
{
    char ack;

    INFO("A2DP COMMAND %s", dump_a2dp_ctrl_event(cmd));

    /* send command */
    if (send(common->ctrl_fd, &cmd, 1, MSG_NOSIGNAL) == -1)
    {
        ERROR("cmd failed (%s)", strerror(errno));
        skt_disconnect(common->ctrl_fd);
        common->ctrl_fd = AUDIO_SKT_DISCONNECTED;
        return -1;
    }

    /* wait for ack byte */
    if (a2dp_ctrl_receive(common, &ack, 1) < 0)
        return -1;

    INFO("A2DP COMMAND %s DONE STATUS %d", dump_a2dp_ctrl_event(cmd), ack);

    if (ack == A2DP_CTRL_ACK_INCALL_FAILURE)
        return ack;
    if (ack != A2DP_CTRL_ACK_SUCCESS)
        return -1;

    return 0;
}

int check_a2dp_ready(struct a2dp_stream_common *common)
{
    INFO("state %s", dump_a2dp_hal_state(common->state));
    if (a2dp_command(common, A2DP_CTRL_CMD_CHECK_READY) < 0)
    {
        ERROR("check a2dp ready failed");
        return -1;
    }
    return 0;
}

int a2dp_read_audio_config(struct a2dp_stream_common *common)
{
    char cmd = A2DP_CTRL_GET_AUDIO_CONFIG;
    uint32_t sample_rate;
    uint8_t channel_count;

    if (a2dp_command(common, A2DP_CTRL_GET_AUDIO_CONFIG) < 0)
    {
        ERROR("check a2dp ready failed");
        return -1;
    }

    if (a2dp_ctrl_receive(common, &sample_rate, 4) < 0)
        return -1;
    if (a2dp_ctrl_receive(common, &channel_count, 1) < 0)
        return -1;

    common->cfg.channel_flags = (channel_count == 1 ? AUDIO_CHANNEL_IN_MONO : AUDIO_CHANNEL_IN_STEREO);
    common->cfg.format = AUDIO_STREAM_DEFAULT_FORMAT;
    common->cfg.rate = sample_rate;

    INFO("got config %d %d", common->cfg.format, common->cfg.rate);

    return 0;
}

int a2dp_read_codec_config(struct a2dp_stream_common *common,uint8_t idx)
{
    char cmd[2],ack;
    int i,len = 0;
    uint8_t *p_codec_cfg = common->codec_cfg;
    cmd[0] = A2DP_CTRL_GET_CODEC_CONFIG;
    cmd[1] = idx;
    INFO("%s",__func__);
    memset(p_codec_cfg,0,20);
    INFO("%s",__func__);

    if (send(common->ctrl_fd, cmd, 2,  MSG_NOSIGNAL) == -1)
    {
        ERROR("cmd failed (%s)", strerror(errno));
        skt_disconnect(common->ctrl_fd);
        common->ctrl_fd = AUDIO_SKT_DISCONNECTED;
        return -1;
    }

    if (a2dp_ctrl_receive(common, &ack, 1) < 0)
        return -1;

    if (ack != A2DP_CTRL_ACK_SUCCESS)
    {
        ERROR("%s: Failed to get ack",__func__);
        return -1;
    }
    if (a2dp_ctrl_receive(common, &len, 1) < 0)
        return -1;
    if (a2dp_ctrl_receive(common, p_codec_cfg, len) < 0)
        return -1;

    INFO("got codec config");
    p_codec_cfg = common->codec_cfg;

    for (i=0;i<len;i++)
         INFO("code_config[%d] = %d ", i,*p_codec_cfg++);

    return 0;
}

int a2dp_get_multicast_status(struct a2dp_stream_common *common, uint8_t *mcast_status,
                               uint8_t *num_dev)
{
    char cmd = A2DP_CTRL_GET_MULTICAST_STATUS;
    INFO("%s",__func__);
    if (a2dp_command(common,A2DP_CTRL_GET_MULTICAST_STATUS) < 0)
    {
        ERROR("check a2dp ready failed");
        return -1;
    }
    INFO("a2dp_get_multicast_status acked fd = %d",common->ctrl_fd);
    if (a2dp_ctrl_receive(common, mcast_status, 1) < 0)
        return -1;
    if (a2dp_ctrl_receive(common, num_dev, 1) < 0)
        return -1;
    INFO("%s: multicast status = %d, num_dev = %d",__func__,*mcast_status,*num_dev);
    return 0;
}
void a2dp_open_ctrl_path(struct a2dp_stream_common *common)
{
    int i;

    /* retry logic to catch any timing variations on control channel */
    for (i = 0; i < CTRL_CHAN_RETRY_COUNT; i++)
    {
        /* connect control channel if not already connected */
        if ((common->ctrl_fd = skt_connect(A2DP_CTRL_PATH, common->buffer_sz)) > 0)
        {
            /* success, now check if stack is ready */
            if (check_a2dp_ready(common) == 0)
                break;

            ERROR("error : a2dp not ready, wait 250 ms and retry");
            usleep(250000);
            skt_disconnect(common->ctrl_fd);
            common->ctrl_fd = AUDIO_SKT_DISCONNECTED;
        }

        /* ctrl channel not ready, wait a bit */
        usleep(250000);
    }
}

/*****************************************************************************
**
** AUDIO DATA PATH
**
*****************************************************************************/

void a2dp_stream_common_init(struct a2dp_stream_common *common)
{
    pthread_mutexattr_t lock_attr;

    FNLOG();

    pthread_mutexattr_init(&lock_attr);
    pthread_mutexattr_settype(&lock_attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&common->lock, &lock_attr);

    common->ctrl_fd = AUDIO_SKT_DISCONNECTED;
    common->audio_fd = AUDIO_SKT_DISCONNECTED;
    common->state = AUDIO_A2DP_STATE_STOPPED;

    /* manages max capacity of socket pipe */
    common->buffer_sz = AUDIO_STREAM_OUTPUT_BUFFER_SZ;
    bt_split_a2dp_enabled = false;
}

int start_audio_datapath(struct a2dp_stream_common *common)
{
    INFO("state %d", common->state);

    #ifdef BT_AUDIO_SYSTRACE_LOG
    char trace_buf[512];
    #endif

    INFO("state %s", dump_a2dp_hal_state(common->state));

    if (common->ctrl_fd == AUDIO_SKT_DISCONNECTED) {
        INFO("%s AUDIO_SKT_DISCONNECTED", __func__);
        return -1;
    }

    int oldstate = common->state;
    common->state = AUDIO_A2DP_STATE_STARTING;

    int a2dp_status = a2dp_command(common, A2DP_CTRL_CMD_START);
    #ifdef BT_AUDIO_SYSTRACE_LOG
    snprintf(trace_buf, 32, "start_audio_data_path:");
    if (PERF_SYSTRACE)
    {
        ATRACE_BEGIN(trace_buf);
    }
    #endif


    #ifdef BT_AUDIO_SYSTRACE_LOG
    if (PERF_SYSTRACE)
    {
        ATRACE_END();
    }
    #endif
    if (a2dp_status < 0)
    {
        ERROR("%s Audiopath start failed (status %d)", __func__, a2dp_status);

        common->state = oldstate;
        return -1;
    }
    else if (a2dp_status == A2DP_CTRL_ACK_INCALL_FAILURE)
    {
        ERROR("%s Audiopath start failed - in call, move to suspended", __func__);
        common->state = oldstate;
        return -1;
    }
    if (!bt_split_a2dp_enabled)
    {
        /* connect socket if not yet connected */
        if (common->audio_fd == AUDIO_SKT_DISCONNECTED)
        {
            common->audio_fd = skt_connect(A2DP_DATA_PATH, common->buffer_sz);
            if (common->audio_fd < 0)
            {
                common->state = oldstate;
                return -1;
            }

            common->state = AUDIO_A2DP_STATE_STARTED;
        }
    }
    else
    {
        common->state = AUDIO_A2DP_STATE_STARTED;
    }

    return 0;
}

int stop_audio_datapath(struct a2dp_stream_common *common)
{
    int oldstate = common->state;

    INFO("state %s", dump_a2dp_hal_state(common->state));

    if (common->ctrl_fd == AUDIO_SKT_DISCONNECTED)
         return -1;

    /* prevent any stray output writes from autostarting the stream
       while stopping audiopath */
    common->state = AUDIO_A2DP_STATE_STOPPING;

    if (a2dp_command(common, A2DP_CTRL_CMD_STOP) < 0)
    {
        ERROR("audiopath stop failed");
        common->state = oldstate;
        return -1;
    }

    common->state = AUDIO_A2DP_STATE_STOPPED;

    if (!bt_split_a2dp_enabled)
    {
        /* disconnect audio path */
        skt_disconnect(common->audio_fd);
        common->audio_fd = AUDIO_SKT_DISCONNECTED;
    }

    return 0;
}

int suspend_audio_datapath(struct a2dp_stream_common *common, bool standby)
{
    INFO("state %s", dump_a2dp_hal_state(common->state));

    if (common->ctrl_fd == AUDIO_SKT_DISCONNECTED)
         return -1;

    if (common->state == AUDIO_A2DP_STATE_STOPPING)
        return -1;

    if (a2dp_command(common, A2DP_CTRL_CMD_SUSPEND) < 0)
        return -1;

    if (standby)
        common->state = AUDIO_A2DP_STATE_STANDBY;
    else
        common->state = AUDIO_A2DP_STATE_SUSPENDED;

    if (!bt_split_a2dp_enabled)
    {
        /* disconnect audio path */
        skt_disconnect(common->audio_fd);

        common->audio_fd = AUDIO_SKT_DISCONNECTED;
    }

    return 0;
}

int check_a2dp_stream_started(struct a2dp_stream_common *common)
{
   if (a2dp_command(common, A2DP_CTRL_CMD_CHECK_STREAM_STARTED) < 0)
   {
       INFO("Btif not in stream state");
       return -1;
   }
   return 0;
}

int audio_open_ctrl_path()
{
    INFO("%s",__func__);
    a2dp_open_ctrl_path(&audio_stream);
    if (audio_stream.ctrl_fd != AUDIO_SKT_DISCONNECTED)
    {
        INFO("control path opend successfull");
        return 0;
    }
    else
        INFO("control path opend successfull");
    return -1;
}

int audio_start_stream()
{
    int i;
    INFO("%s: state = %s",__func__,dump_a2dp_hal_state(audio_stream.state));

    if (audio_stream.state == AUDIO_A2DP_STATE_SUSPENDED)
    {
        INFO("stream suspended");
        return -1;
    }

    for (i = 0; i < STREAM_START_MAX_RETRY_COUNT; i++)
    {
        if (audio_stream.ctrl_fd == AUDIO_SKT_DISCONNECTED)
        {
            INFO("control path is disconnected");
            break;
        }
        if (start_audio_datapath(&audio_stream) == 0)
            break;
        INFO("%s: a2dp stream not started,wait 100mse & retry");
        usleep(100000);
    }
    if (audio_stream.state != AUDIO_A2DP_STATE_STARTED)
    {
        ERROR("Failed to start a2dp stream");
        return -1;
    }
    return 0;
}

int audio_stream_open()
{
    INFO("%s",__func__);
    a2dp_stream_common_init(&audio_stream);
    a2dp_open_ctrl_path(&audio_stream);
    bt_split_a2dp_enabled = true;
    if (audio_stream.ctrl_fd != AUDIO_SKT_DISCONNECTED)
    {
        INFO("control path open successful");
        /*Delay to ensure Headset is in proper state when START is initiated
        from DUT immediately after the connection due to ongoing music playback. */
        usleep(250000);
        a2dp_command(&audio_stream,A2DP_CTRL_CMD_OFFLOAD_SUPPORTED);
        return 0;
    }
    else
        INFO("control path open failed");

    return -1;
}

int audio_stream_close()
{
    INFO("%s",__func__);

    if (audio_stream.state == AUDIO_A2DP_STATE_STARTED ||
        audio_stream.state == AUDIO_A2DP_STATE_STOPPING)
    {
        INFO("%s: Suspending audio stream",__func__);
        suspend_audio_datapath(&audio_stream,true);
    }

    skt_disconnect(audio_stream.ctrl_fd);
    audio_stream.ctrl_fd = AUDIO_SKT_DISCONNECTED;
    return 0;
}
int audio_stop_stream()
{
    INFO("%s",__func__);
    if (suspend_audio_datapath(&audio_stream, true) == 0)
    {
        INFO("audio start stream successful");
        return 0;
    }
    return -1;
}

int audio_suspend_stream()
{
    INFO("%s",__func__);
    if (suspend_audio_datapath(&audio_stream, false) == 0)
    {
        INFO("audio start stream successful");
        return 0;
    }
    return -1;
}

void audio_handoff_triggered()
{
    INFO("%s state = %s",__func__,dump_a2dp_hal_state(audio_stream.state));
    if (audio_stream.state != AUDIO_A2DP_STATE_STOPPED ||
        audio_stream.state != AUDIO_A2DP_STATE_STOPPING)
    {
        audio_stream.state = AUDIO_A2DP_STATE_STOPPED;
    }
}

void clear_a2dpsuspend_flag()
{
    INFO("%s: state = %s",__func__,dump_a2dp_hal_state(audio_stream.state));
    if (audio_stream.state == AUDIO_A2DP_STATE_SUSPENDED)
        audio_stream.state = AUDIO_A2DP_STATE_STOPPED;
}

void * audio_get_codec_config(uint8_t *multicast_status, uint8_t *num_dev,
                              audio_format_t *codec_type)
{
    char *p_common_cfg = &audio_stream.codec_cfg[0];
    int i;

    INFO("%s: state = %s",__func__,dump_a2dp_hal_state(audio_stream.state));

    a2dp_get_multicast_status(&audio_stream, multicast_status,num_dev);

    DEBUG("got multicast status = %d dev = %d",*multicast_status,*num_dev);
    if (a2dp_read_codec_config(&audio_stream, 0) == 0)
    {
        return (a2dp_codec_parser(&audio_stream.codec_cfg[0], codec_type));
    }
    return NULL;
}

void* audio_get_next_codec_config(uint8_t idx, audio_format_t *codec_type)
{
    INFO("%s",__func__);
    if (a2dp_read_codec_config(&audio_stream,idx) == 0)
    {
        return a2dp_codec_parser(&audio_stream.codec_cfg[0], codec_type);
    }
    return NULL;
}
//Entry point for dynamic lib
const bt_host_ipc_interface_t BTHOST_IPC_INTERFACE = {
    sizeof(bt_host_ipc_interface_t),
    a2dp_open_ctrl_path,
    a2dp_stream_common_init,
    start_audio_datapath,
    suspend_audio_datapath,
    stop_audio_datapath,
    check_a2dp_stream_started,
    check_a2dp_ready,
    a2dp_read_audio_config,
    skt_read,
    skt_write,
    skt_disconnect,
    a2dp_command,
    audio_stream_open,
    audio_stream_close,
    audio_start_stream,
    audio_stop_stream,
    audio_suspend_stream,
    audio_get_codec_config,
    audio_handoff_triggered,
    clear_a2dpsuspend_flag,
    audio_get_next_codec_config
};
