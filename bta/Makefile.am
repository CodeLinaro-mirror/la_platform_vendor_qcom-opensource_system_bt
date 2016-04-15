# sources and intermediate files are separated

AM_CFLAGS = -Wall \
        -Wundef \
        -Wstrict-prototypes \
        -Wno-trigraphs \
        -std=c99 \
        -DHAS_NO_BDROID_BUILDCFG

AM_CPPFLAGS = -Iinclude \
        -Isys \
        -Idm \
        -Ihh \
        -I../ \
        -I../btcore/include \
        -I../gki/common \
        -I../gki/ulinux \
        -I../hci/include \
        -I../include \
        -I../stack/include \
        -I../stack/btm \
        -I../osi/include \
        -I../udrv/include \
        -I../vnd/include \
        -I../utils/include

c_sources = \
        dm/bta_dm_ci.c \
        dm/bta_dm_act.c \
        dm/bta_dm_pm.c \
        dm/bta_dm_main.c \
        dm/bta_dm_cfg.c \
        dm/bta_dm_api.c \
        dm/bta_dm_sco.c \
        gatt/bta_gattc_api.c \
        gatt/bta_gatts_act.c \
        gatt/bta_gatts_main.c \
        gatt/bta_gattc_utils.c \
        gatt/bta_gattc_ci.c \
        gatt/bta_gatts_api.c \
        gatt/bta_gattc_main.c \
        gatt/bta_gattc_act.c \
        gatt/bta_gattc_cache.c \
        gatt/bta_gatts_utils.c \
        ag/bta_ag_sdp.c \
        pan/bta_pan_main.c \
        pan/bta_pan_ci.c \
        pan/bta_pan_act.c \
        pan/bta_pan_api.c \
        sdp/bta_sdp_api.c \
        sdp/bta_sdp_act.c \
        sdp/bta_sdp.c \
        sdp/bta_sdp_cfg.c \
        sys/bta_sys_main.c \
        sys/bta_sys_conn.c \
        sys/utl.c \
        jv/bta_jv_act.c \
        jv/bta_jv_cfg.c \
        jv/bta_jv_main.c \
        jv/bta_jv_api.c

common_LDFLAGS = -lpthread  -lrt ../stack/libbtstack.la
common_CFLAGS =  $(AM_CFLAGS) -fPIC -D_GNU_SOURCE

library_includedir = $(pkgincludedir)\
library_include_HEADERS = $(h_sources)
lib_LTLIBRARIES = libbtbta.la
libbtbta_la_CC = @CC@
libbtbta_la_SOURCES = $(c_sources)
libbtbta_la_CPPFLAGS =$(AM_CPPFLAGS)
libbtbta_la_CFLAGS = $(common_CFLAGS)
libbtbta_la_LDFLAGS := $(common_LDFLAGS) -static
