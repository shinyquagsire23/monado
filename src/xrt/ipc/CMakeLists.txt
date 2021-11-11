# Copyright 2020-2021, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

###
# Generator

foreach(
	fn
	ipc_protocol_generated.h
	ipc_client_generated.h
	ipc_client_generated.c
	ipc_server_generated.h
	ipc_server_generated.c
	)
	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${fn}"
		COMMAND
			${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/shared/proto.py
			${CMAKE_CURRENT_SOURCE_DIR}/shared/proto.json
			"${CMAKE_CURRENT_BINARY_DIR}/${fn}"
		VERBATIM
		DEPENDS
			${CMAKE_CURRENT_SOURCE_DIR}/shared/proto.py
			${CMAKE_CURRENT_SOURCE_DIR}/shared/ipcproto/common.py
			${CMAKE_CURRENT_SOURCE_DIR}/shared/proto.json
		COMMENT "Generating ${fn} from protocol JSON description"
		)
endforeach()

set(IPC_COMMON_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/ipc_protocol_generated.h
    shared/ipc_shmem.c
    shared/ipc_shmem.h
    shared/ipc_utils.c
    shared/ipc_utils.h
	)

add_library(ipc_shared STATIC ${IPC_COMMON_SOURCES})
target_include_directories(
	ipc_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
	)
target_link_libraries(ipc_shared PRIVATE aux_util)

if(RT_LIBRARY)
	target_link_libraries(ipc_shared PUBLIC ${RT_LIBRARY})
endif()

###
# Client

add_library(
	ipc_client STATIC
	${CMAKE_CURRENT_BINARY_DIR}/ipc_client_generated.c
	${CMAKE_CURRENT_BINARY_DIR}/ipc_client_generated.h
	client/ipc_client.h
	client/ipc_client_compositor.c
	client/ipc_client_device.c
	client/ipc_client_hmd.c
	client/ipc_client_instance.c
	)
target_include_directories(
	ipc_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}
	)
target_link_libraries(ipc_client PRIVATE aux_util ipc_shared)

###
# Server

add_library(
	ipc_server STATIC
	${CMAKE_CURRENT_BINARY_DIR}/ipc_server_generated.c
	${CMAKE_CURRENT_BINARY_DIR}/ipc_server_generated.h
	server/ipc_server.h
	server/ipc_server_handler.c
	server/ipc_server_per_client_thread.c
	server/ipc_server_process.c
	)
target_include_directories(
	ipc_server
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
	PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
	)
target_link_libraries(ipc_server PRIVATE aux_util ipc_shared)

if(XRT_HAVE_SYSTEMD)
	target_include_directories(ipc_server PRIVATE ${SYSTEMD_INCLUDE_DIRS})
	target_link_libraries(ipc_server PRIVATE ${SYSTEMD_LIBRARIES})
endif()

if(ANDROID)
	add_library(
		ipc_android STATIC
		android/ipc_client_android.cpp
		android/ipc_client_android.h
		android/org.freedesktop.monado.ipc.cpp
		android/org.freedesktop.monado.ipc.hpp
		android/org.freedesktop.monado.ipc.impl.hpp
		)
	target_link_libraries(
		ipc_android PUBLIC xrt-external-jni-wrap xrt-external-jnipp aux_android
		)
	target_sources(
		ipc_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/server/ipc_server_mainloop_android.c
		)
	target_link_libraries(
		ipc_shared
		PUBLIC ${ANDROID_LIBRARY}
		PRIVATE aux_android ipc_android
		)
elseif(XRT_HAVE_LINUX)
	target_sources(
		ipc_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/server/ipc_server_mainloop_linux.c
		)
endif()
