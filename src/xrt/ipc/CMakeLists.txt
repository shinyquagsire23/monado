# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0


###
# Generator

function(proto_gen output)
	add_custom_command(OUTPUT ${output}
		COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/shared/proto.py
			${CMAKE_CURRENT_SOURCE_DIR}/shared/proto.json
			${output}
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shared/proto.py
			${CMAKE_CURRENT_SOURCE_DIR}/shared/ipcproto/common.py
			${CMAKE_CURRENT_SOURCE_DIR}/shared/proto.json
			)
endfunction(proto_gen)

proto_gen(${CMAKE_CURRENT_BINARY_DIR}/ipc_protocol_generated.h)
proto_gen(${CMAKE_CURRENT_BINARY_DIR}/ipc_client_generated.h)
proto_gen(${CMAKE_CURRENT_BINARY_DIR}/ipc_client_generated.c)
proto_gen(${CMAKE_CURRENT_BINARY_DIR}/ipc_server_generated.h)
proto_gen(${CMAKE_CURRENT_BINARY_DIR}/ipc_server_generated.c)

set(IPC_COMMON_SOURCES
	${CMAKE_CURRENT_BINARY_DIR}/ipc_protocol_generated.h
	shared/ipc_shmem.c
	shared/ipc_shmem.h
	shared/ipc_utils.c
	shared/ipc_utils.h)
###
# Client

add_library(ipc_client STATIC
	${CMAKE_CURRENT_BINARY_DIR}/ipc_client_generated.c
	${CMAKE_CURRENT_BINARY_DIR}/ipc_client_generated.h
	${IPC_COMMON_SOURCES}
	client/ipc_client.h
	client/ipc_client_compositor.c
	client/ipc_client_device.c
	client/ipc_client_hmd.c
	client/ipc_client_instance.c
	)
target_include_directories(ipc_client INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
	)
target_include_directories(ipc_client PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
	)
target_link_libraries(ipc_client PRIVATE
	aux_util
	)
if(ANDROID)
	target_link_libraries(ipc_client PRIVATE
		aux_android
		)
endif()

###
# Server

add_library(ipc_server STATIC
	${CMAKE_CURRENT_BINARY_DIR}/ipc_server_generated.c
	${CMAKE_CURRENT_BINARY_DIR}/ipc_server_generated.h
	${IPC_COMMON_SOURCES}
	server/ipc_server.h
	server/ipc_server_client.c
	server/ipc_server_process.c
	)
target_include_directories(ipc_server
	INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
	)
target_include_directories(ipc_server PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../compositor
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR}
	)
target_link_libraries(ipc_server PRIVATE
	aux_util
	)

if(RT_LIBRARY)
	target_link_libraries(ipc_client PRIVATE
		${RT_LIBRARY}
		)
	target_link_libraries(ipc_server PRIVATE
		${RT_LIBRARY}
		)
endif()

if(XRT_HAVE_SYSTEMD)
	target_include_directories(ipc_server PRIVATE
		${SYSTEMD_INCLUDE_DIRS})
	target_link_libraries(ipc_server PRIVATE
		${SYSTEMD_LIBRARIES})
endif()

if(ANDROID)
	target_link_libraries(ipc_server PUBLIC ${ANDROID_LIBRARY})
	target_link_libraries(ipc_client PUBLIC ${ANDROID_LIBRARY})
endif()
