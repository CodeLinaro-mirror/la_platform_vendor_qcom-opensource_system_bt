# sources and intermediate files are separated


AM_CFLAGS = -Wall \
        -Wundef \
        -Wstrict-prototypes \
        -Wno-trigraphs \
        -std=c++11 \
        -DHAS_NO_BDROID_BUILDCFG

AM_CPPFLAGS= \
        -I. \
        -I../ \
        -I../include \
        -I../types \
        -I../utils/include \
        -I../audio_a2dp_hw/include \
        -I${WORKSPACE}/system/media/audio/include \
        -I${WORKSPACE}/hardware/libhardware/include \
        -I$(bdroid_C_INCLUDES)

cpp_sources = \
        src/audio_a2dp_hw.cc \
        src/audio_a2dp_hw_utils.cc

common_LDFLAGS = -lpthread  -lrt ../osi/libbtosi.la
common_CFLAGS =  $(AM_CFLAGS) -fPIC -D_GNU_SOURCE

library_includedir = $(pkgincludedir)
lib_LTLIBRARIES = libaudioa2dpdefault.la
libaudioa2dpdefault_la_CC = @CC@

libaudioa2dpdefault_la_SOURCES = $(cpp_sources)
libaudioa2dpdefault_la_CPPFLAGS =$(AM_CPPFLAGS)  $(common_CFLAGS)
libaudioa2dpdefault_la_CFLAGS = $(common_CFLAGS)
libaudioa2dpdefault_la_LDFLAGS = $(common_LDFLAGS)

########BT_HOST_IPC########

ipc_sources = \
           src/bthost_ipc.cc

library_includedir = $(pkgincludedir)
lib_LTLIBRARIES +=  libbthost_if.la
libbthost_if_la_SOURCES = $(ipc_sources)
libbthost_if_la_CPPFLAGS =$(AM_CPPFLAGS)    $(common_CFLAGS)
libbthost_if_la_CFLAGS = $(common_CFLAGS)
libbthost_if_la_LDFLAGS = $(common_LDFLAGS) -avoid-version

