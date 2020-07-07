# Copyright 2020, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0


###
# Generator

function(proto_gen output)
	add_custom_command(OUTPUT ${output}
		COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/proto.py
			${CMAKE_CURRENT_SOURCE_DIR}/proto.json
			${output}
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/proto.py
			${CMAKE_CURRENT_SOURCE_DIR}/ipcproto/common.py
			${CMAKE_CURRENT_SOURCE_DIR}/proto.json
			)
endfunction(proto_gen)

proto_gen(${CMAKE_CURRENT_BINARY_DIR}/ipc_protocol_generated.h)
proto_gen(${CMAKE_CURRENT_BINARY_DIR}/ipc_client_generated.h)
proto_gen(${CMAKE_CURRENT_BINARY_DIR}/ipc_client_generated.c)
proto_gen(${CMAKE_CURRENT_BINARY_DIR}/ipc_server_generated.h)
proto_gen(${CMAKE_CURRENT_BINARY_DIR}/ipc_server_generated.c)

set(IPC_COMMON_SOURCES
	${CMAKE_CURRENT_BINARY_DIR}/ipc_protocol_generated.h
	ipc_utils.c
	ipc_utils.h)
###
# Client

add_library(ipc_client STATIC
	${CMAKE_CURRENT_BINARY_DIR}/ipc_client_generated.c
	${CMAKE_CURRENT_BINARY_DIR}/ipc_client_generated.h
	${IPC_COMMON_SOURCES}
	ipc_client.h
	ipc_client_compositor.c
	ipc_client_device.c
	ipc_client_hmd.c
	ipc_client_instance.c
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


###
# Server

add_library(ipc_server STATIC
	${CMAKE_CURRENT_BINARY_DIR}/ipc_server_generated.c
	${CMAKE_CURRENT_BINARY_DIR}/ipc_server_generated.h
	${IPC_COMMON_SOURCES}
	ipc_server.h
	ipc_server_client.c
	ipc_server_process.c
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
