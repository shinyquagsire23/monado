# Copyright 2022, Guillaume Meunier
# Copyright 2022, Patrick Nicolas
# SPDX-License-Identifier: BSL-1.0

add_library(monado-wivrn-mdnssvc STATIC mdnssvc/mdns.c mdnssvc/mdns.h mdnssvc/mdnsd.c mdnssvc/mdnssvc.h)

add_library(monado-wivrn-target STATIC target_instance_wivrn.cpp hostname.cpp mdns_publisher.cpp)

target_include_directories(monado-wivrn-target SYSTEM PRIVATE /opt/homebrew/include)
target_link_libraries(
	monado-wivrn-target
	PRIVATE
		aux_util
		drv_wivrn
		comp_main
		#PkgConfig::AVAHI
		monado-wivrn-mdnssvc
	)

add_executable(monado-wivrn main.cpp)

add_sanitizers(monado-wivrn)

target_link_libraries(
	monado-wivrn
	PRIVATE
		aux_util
		drv_wivrn
		monado-wivrn-target
	)

if(XRT_HAVE_AVAHI)
#target_link_libraries(
#	monado-wivrn
#	PRIVATE
#		PkgConfig::AVAHI
#	)
endif()

if(XRT_HAVE_LINUX)
	target_link_libraries(
	monado-wivrn
	PRIVATE
		systemd
	)
endif()

if(XRT_FEATURE_SERVICE)
	target_link_libraries(
	monado-wivrn
	PRIVATE
		ipc_server
	)
endif()