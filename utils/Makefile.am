# sources and intermediate files are separated

AM_CFLAGS = -Wno-gnu-variable-sized-type-not-at-end \
        -Wundef \
        -Wstrict-prototypes \
        -Wno-trigraphs \
        -std=c++11 \
        -Wall \
        -Wextra \
        -Wno-typedef-redefinition \
        -Wno-unused-parameter \
        -DHAS_NO_BDROID_BUILDCFG

AM_CPPFLAGS= \
        -Iinclude \
        -I../btcore/include \
        -I../stack/include \
        -I../utils/include \
        -I../include \
        -I../types \
        -I../ \
        -I$(WORKSPACE)/vendor/qcom/opensource/bluetooth/hal/include \
        -I$(bdroid_C_INCLUDES)

if MDM_ROME
AM_CFLAGS += -DBT_SOC_TYPE_ROME
AM_CPPFLAGS += -DBT_SOC_TYPE_ROME
else
if MDM_PRONTO
AM_CFLAGS += -DHCI_USE_MCT
AM_CPPFLAGS += -DHCI_USE_MCT
endif
endif

if MDM_CHEROKEE
AM_CFLAGS += -DBT_SOC_TYPE_CHEROKEE
AM_CPPFLAGS += -DBT_SOC_TYPE_CHEROKEE
endif

cpp_sources = \
        src/bt_utils.cc

common_LDFLAGS = -lpthread
common_CFLAGS =  $(AM_CFLAGS) -fPIC -DHAS_NO_BDROID_BUILDCFG

library_includedir = $(pkgincludedir)
lib_LTLIBRARIES = libbtutils.la
libbtutils_la_CC = @CC@
libbtutils_la_SOURCES = $(cpp_sources)
libbtutils_la_CPPFLAGS =$(AM_CPPFLAGS) -std=c++11 -Wall -DHAS_NO_BDROID_BUILDCFG -fPIC
libbtutils_la_CFLAGS = $(common_CFLAGS)
libbtutils_la_LDFLAGS = $(common_LDFLAGS) -static
