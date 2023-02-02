ACLOCAL_AMFLAGS = -I m4

root_etcdir  = "/etc"

systemBt_sysconfdir = $(root_etcdir)/bluetooth
systemBt_sysconf_DATA  = $(top_srcdir)/conf/bt_did.conf
systemBt_sysconf_DATA += $(top_srcdir)/conf/bt_stack.conf
systemBt_sysconf_DATA += $(top_srcdir)/../../bluetooth_ext/system_bt_ext/conf/bt_profile.conf
systemBt_sysconf_DATA += $(top_srcdir)/../../bluetooth_ext/system_bt_ext/conf/interop_database.conf

install-data-hook:
	chmod 0660 $(DESTDIR)/$(systemBt_sysconfdir)/bt_did.conf
	chmod 0660 $(DESTDIR)/$(systemBt_sysconfdir)/bt_stack.conf
	chmod 0660 $(DESTDIR)/$(systemBt_sysconfdir)/bt_profile.conf
	chmod 0660 $(DESTDIR)/$(systemBt_sysconfdir)/interop_database.conf

AM_CPPFLAGS = @LIBCHROME_CFLAGS@

SUBDIRS = hci btcore utils types osi device stack audio_a2dp_hw bta btif embdrv/sbc/decoder main

#Export Headers
systemBt_includedir       =  $(includedir)/hardware/
systemBt_include_HEADERS  =  $(top_srcdir)/include/hardware/bluetooth.h
systemBt_include_HEADERS  +=  $(top_srcdir)/include/hardware/bt_av.h
systemBt_include_HEADERS  +=  $(top_srcdir)/include/hardware/bt_gatt.h
systemBt_include_HEADERS  +=  $(top_srcdir)/include/hardware/bt_sdp.h
systemBt_include_HEADERS  +=  $(top_srcdir)/include/hardware/bt_rc.h
systemBt_include_HEADERS  +=  $(top_srcdir)/include/hardware/bt_common_types.h
systemBt_include_HEADERS  +=  $(top_srcdir)/include/hardware/bt_gatt_types.h
systemBt_include_HEADERS  +=  $(top_srcdir)/include/hardware/bt_hh.h
systemBt_include_HEADERS  +=  $(top_srcdir)/include/hardware/bt_hf_client.h
systemBt_include_HEADERS  +=  $(top_srcdir)/include/hardware/bt_pan.h
systemBt_include_HEADERS  +=  $(top_srcdir)/include/hardware/bt_hf.h
systemBt_include_HEADERS  +=  $(top_srcdir)/include/hardware/bt_gatt_client.h
systemBt_include_HEADERS  +=  $(top_srcdir)/include/hardware/bt_gatt_server.h
systemBt_include_HEADERS  +=  $(top_srcdir)/include/hardware/hardware.h
systemBt_include_HEADERS  +=  $(top_srcdir)/include/hardware/audio.h
systemBt_include_HEADERS  +=  $(top_srcdir)/include/hardware/audio_effect.h

#btservice headers
btService_includedir      =  $(includedir)/bluetooth/
btService_include_HEADERS =  $(top_srcdir)/types/bluetooth/uuid.h
btService_include_HEADERS += $(top_srcdir)/service/common/android/bluetooth/bluetooth_gatt_included_service.h
btService_include_HEADERS += $(top_srcdir)/service/common/android/bluetooth/scan_filter.h
btService_include_HEADERS += $(top_srcdir)/service/common/android/bluetooth/bluetooth_gatt_service.h
btService_include_HEADERS += $(top_srcdir)/service/common/android/bluetooth/bluetooth_gatt_characteristic.h
btService_include_HEADERS += $(top_srcdir)/service/common/android/bluetooth/bluetooth_gatt_descriptor.h
btService_include_HEADERS += $(top_srcdir)/service/common/android/bluetooth/scan_settings.h
btService_include_HEADERS += $(top_srcdir)/service/common/android/bluetooth/advertise_data.h
btService_include_HEADERS += $(top_srcdir)/service/common/android/bluetooth/advertise_settings.h
btService_include_HEADERS += $(top_srcdir)/service/common/android/bluetooth/scan_result.h

#btTypes Headers
btTypes_includedir        =  $(includedir)/
btTypes_include_HEADERS   =  $(top_srcdir)/types/raw_address.h
btTypes_include_HEADERS   +=  $(top_srcdir)/audio_a2dp_hw/include/audio_a2dp_hw.h
btTypes_include_HEADERS   +=  $(top_srcdir)/utils/include/bt_utils.h
#btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/ble_advertiser.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/btm_ble_api.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/bt_types.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/btm_api.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/hcidefs.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/sdp_api.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/sdpdefs.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/smp_api.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/smp_api_types.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/btm_api_types.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/btm_ble_api_types.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/l2c_api.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/l2cdefs.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/gattdefs.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/gatt_api.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/gap_api.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/profiles_api.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/mca_api.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/include/mca_defs.h
btTypes_include_HEADERS   +=  $(top_srcdir)/internal_include/bt_target.h
btTypes_include_HEADERS   +=  $(top_srcdir)/internal_include/bt_trace.h
btTypes_include_HEADERS   +=  $(top_srcdir)/internal_include/bt_common.h
btTypes_include_HEADERS   +=  $(top_srcdir)/include/hardware/ble_scanner.h
btTypes_include_HEADERS   +=  $(top_srcdir)/include/hardware/bt_common_types.h
btTypes_include_HEADERS   +=  $(top_srcdir)/include/hardware/bt_gatt_client.h
btTypes_include_HEADERS   +=  $(top_srcdir)/include/hardware/bt_gatt_types.h
btTypes_include_HEADERS   +=  $(top_srcdir)/include/hardware/bt_gatt_server.h
btTypes_include_HEADERS   +=  $(top_srcdir)/include/hardware/ble_advertiser.h
btTypes_include_HEADERS   +=  $(top_srcdir)/include/hardware/bluetooth.h
btTypes_include_HEADERS   +=  $(top_srcdir)/bta/include/bta_api.h
btTypes_include_HEADERS   +=  $(top_srcdir)/stack/l2cap/l2c_int.h

#exporting osi headers
osi_includedir            = $(includedir)/osi/include/
osi_include_HEADERS       = $(top_srcdir)/osi/include/hash_map_utils.h
osi_include_HEADERS       += $(top_srcdir)/osi/include/log.h
osi_include_HEADERS       += $(top_srcdir)/osi/include/osi.h
osi_include_HEADERS       += $(top_srcdir)/osi/include/allocator.h
osi_include_HEADERS       += $(top_srcdir)/osi/include/compat.h
osi_include_HEADERS       += $(top_srcdir)/osi/include/alarm.h
osi_include_HEADERS       += $(top_srcdir)/osi/include/time.h
osi_include_HEADERS       += $(top_srcdir)/osi/include/fixed_queue.h
osi_include_HEADERS       += $(top_srcdir)/osi/include/list.h

#exporting socket headers
socket_includedir         = $(includedir)/osi/include/socket_utils/
socket_include_HEADERS    = $(top_srcdir)/osi/include/socket_utils/sockets.h

#exporting misc headers
btstack_includedir        = $(includedir)/include
btstack_include_HEADERS   = $(top_srcdir)/include/bt_logger_lib.h

#exporting internal include headers
internalHeaders_includedir       =  $(includedir)/internal_include/
internalHeaders_include_HEADERS  =  $(top_srcdir)/internal_include/bt_common.h
internalHeaders_include_HEADERS  +=  $(top_srcdir)/internal_include/bt_target.h

#exporting device headers
device_includedir         = $(includedir)/device/include/
device_include_HEADERS    = $(top_srcdir)/device/include/esco_parameters.h

#exporting types headers
types_includedir          = $(includedir)/types
types_include_HEADERS     = $(top_srcdir)/types/raw_address.h

pkgconfigdir   = $(libdir)/pkgconfig
pkgconfig_DATA = system_bt.pc
EXTRA_DIST     = $(pkgconfig_DATA)
