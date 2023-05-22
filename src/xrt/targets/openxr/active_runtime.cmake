# Copyright 2019, Benjamin Saunders <ben.e.saunders@gmail.com>
# Copyright 2019-2023, Collabora, Ltd.
# SPDX-License-Identifier: BSL-1.0

set(MANIFEST_RELATIVE_DIR @MANIFEST_RELATIVE_DIR@)
set(XR_API_MAJOR @XR_API_MAJOR@)
set(RUNTIME_TARGET @RUNTIME_TARGET@)
set(CURRENT_BIN_DIR @CMAKE_CURRENT_BINARY_DIR@)
set(CMAKE_INSTALL_SYSCONFDIR @CMAKE_INSTALL_SYSCONFDIR@)

execute_process(COMMAND "${CMAKE_COMMAND}" -E rm -f "${CURRENT_BIN_DIR}/active_runtime.json")
execute_process(
	COMMAND
		"${CMAKE_COMMAND}" -E create_symlink
		"${CMAKE_INSTALL_PREFIX}/${MANIFEST_RELATIVE_DIR}/${RUNTIME_TARGET}.json"
		"${CURRENT_BIN_DIR}/active_runtime.json"
	)
file(
	INSTALL
	DESTINATION "/${CMAKE_INSTALL_SYSCONFDIR}/xdg/openxr/${XR_API_MAJOR}"
	TYPE FILE FILES "${CURRENT_BIN_DIR}/active_runtime.json"
	)
