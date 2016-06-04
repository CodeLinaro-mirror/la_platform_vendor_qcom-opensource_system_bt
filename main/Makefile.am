# sources and intermediate files are separated

AM_CFLAGS = -Wall \
        -Wundef \
        -Wstrict-prototypes \
        -Wno-trigraphs \
        -std=c99 \
        -DHAS_NO_BDROID_BUILDCFG

AM_CPPFLAGS= \
        -I../ \
        -I../bta/include \
        -I../bta/sys \
        -I../bta/dm \
        -I../btcore/include \
        -I../osi/include \
        -I../gki/common \
        -I../gki/ulinux \
        -I../include \
        -I../stack/include \
        -I../stack/l2cap \
        -I../stack/a2dp \
        -I../stack/btm \
        -I../stack/avdt \
        -I../hcis \
        -I../hcis/include \
        -I../hcis/patchram \
        -I../udrv/include \
        -I../btif/include \
        -I../btif/co \
        -I../hci/include\
        -I../vnd/include \
        -I../brcm/include \
        -I../embdrv/sbc/encoder/include \
        -I../embdrv/sbc/decoder/include \
        -I../audio_a2dp_hw \
        -I../utils/include \
        -I${WORKSPACE}/external/tinyxml2 \
        -I${WORKSPACE}/external/zlib

# HAL layer
c_sources = \
        ../btif/src/bluetooth.c

# platform specific
c_sources+= \
        bte_main.c \
        bte_init.c \
        bte_logmsg.c \
        bte_conf.c \
        stack_config.c

# BTIF
c_sources += \
        ../btif/src/btif_config.c \
        ../btif/src/btif_core.c \
        ../btif/src/btif_debug.c \
        ../btif/src/btif_debug_btsnoop.c \
        ../btif/src/btif_debug_conn.c \
        ../btif/src/btif_dm.c \
        ../btif/src/btif_gatt.c \
        ../btif/src/btif_gatt_client.c \
        ../btif/src/btif_gatt_multi_adv_util.c \
        ../btif/src/btif_gatt_server.c \
        ../btif/src/btif_gatt_test.c \
        ../btif/src/btif_gatt_util.c \
        ../btif/src/btif_sdp.c \
        ../btif/src/btif_pan.c \
        ../btif/src/btif_profile_queue.c \
        ../btif/src/btif_sm.c \
        ../btif/src/btif_sock.c \
        ../btif/src/btif_sock_rfc.c \
        ../btif/src/btif_sock_l2cap.c \
        ../btif/src/btif_sock_sdp.c \
        ../btif/src/btif_sock_thread.c \
        ../btif/src/btif_sdp_server.c \
        ../btif/src/btif_sock_util.c \
        ../btif/src/btif_stack_log.c \
        ../btif/src/btif_storage.c \
        ../btif/src/btif_util.c \
        ../btif/src/stack_manager.c

#if ($(BOARD_USES_WIPOWER), true)
#c_sources += \
#   ../wipowerif/src/wipower.c
#endif

# callouts
c_sources+= \
        ../btif/co/bta_dm_co.c \
        ../btif/co/bta_pan_co.c \
        ../btif/co/bta_gattc_co.c \
        ../btif/co/bta_gatts_co.c

c_sources+= \
        ../udrv/ulinux/uipc.c

common_LDFLAGS = -ldl -lrt -lresolv ../hci/libbt-hci.la \
        ../utils/libbtutils.la ../osi/libbtosi.la ../gki/libbtgki.la \
        ../stack/libbtstack.la ../bta/libbtbta.la ../btcore/libbtcore.la \
        ../profile/libbtprofile.la ../device/libbtdevice.la
        common_CFLAGS =  $(AM_CFLAGS) -fPIC -D_GNU_SOURCE $(Z_LIB)

library_includedir = $(pkgincludedir)
lib_LTLIBRARIES = libbluetoothdefault.la
libbluetoothdefault_la_CC = @CC@
libbluetoothdefault_la_SOURCES = $(c_sources)
libbluetoothdefault_la_CPPFLAGS = $(AM_CPPFLAGS)
libbluetoothdefault_la_CFLAGS = $(common_CFLAGS)
libbluetoothdefault_la_LDFLAGS = $(common_LDFLAGS)
