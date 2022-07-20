// Copyright 2022, Magic Leap, Inc.
// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  IPC util helpers on Windows, for internal use only
 * @author Julian Petrov <jpetrov@magicleap.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Pete Black <pblack@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc_shared
 */

#include "xrt/xrt_config_os.h"

#include "util/u_windows.h"
#include "util/u_logging.h"

#include "shared/ipc_utils.h"
#include "shared/ipc_protocol.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include <vector>


/*
 *
 * Logging
 *
 */

#define IPC_TRACE(d, ...) U_LOG_IFL_T(d->log_level, __VA_ARGS__)
#define IPC_DEBUG(d, ...) U_LOG_IFL_D(d->log_level, __VA_ARGS__)
#define IPC_INFO(d, ...) U_LOG_IFL_I(d->log_level, __VA_ARGS__)
#define IPC_WARN(d, ...) U_LOG_IFL_W(d->log_level, __VA_ARGS__)
#define IPC_ERROR(d, ...) U_LOG_IFL_E(d->log_level, __VA_ARGS__)

const char *
ipc_winerror(DWORD err)
{
	static char s_buf[4096]; // N.B. Not thread-safe. If needed, use a thread var
	return u_winerror(s_buf, sizeof(s_buf), err, false);
}

void
ipc_message_channel_close(struct ipc_message_channel *imc)
{
	if (imc->ipc_handle != INVALID_HANDLE_VALUE) {
		CloseHandle(imc->ipc_handle);
		imc->ipc_handle = INVALID_HANDLE_VALUE;
	}
}

xrt_result_t
ipc_send(struct ipc_message_channel *imc, const void *data, size_t size)
{
	DWORD len;
	if (!WriteFile(imc->ipc_handle, data, DWORD(size), &len, NULL)) {
		DWORD err = GetLastError();
		IPC_ERROR(imc, "WriteFile on pipe %p failed: %d %s", imc->ipc_handle, err, ipc_winerror(err));
		return XRT_ERROR_IPC_FAILURE;
	}
	return XRT_SUCCESS;
}

xrt_result_t
ipc_receive(struct ipc_message_channel *imc, void *out_data, size_t size)
{
	DWORD len;
	if (!ReadFile(imc->ipc_handle, out_data, DWORD(size), &len, NULL)) {
		DWORD err = GetLastError();
		IPC_ERROR(imc, "ReadFile from pipe %p failed: %d %s", imc->ipc_handle, err, ipc_winerror(err));
		return XRT_ERROR_IPC_FAILURE;
	}
	return XRT_SUCCESS;
}

xrt_result_t
ipc_receive_fds(
    struct ipc_message_channel *imc, void *out_data, size_t size, HANDLE *out_handles, uint32_t handle_count)
{
	auto rc = ipc_receive(imc, out_data, size);
	if (rc != XRT_SUCCESS) {
		return rc;
	}
	return ipc_receive(imc, out_handles, handle_count * sizeof(*out_handles));
}

HANDLE
open_target_process_dup_handle(struct ipc_message_channel *imc)
{
	DWORD flags;
	if (!GetNamedPipeInfo(imc->ipc_handle, &flags, NULL, NULL, NULL)) {
		DWORD err = GetLastError();
		IPC_ERROR(imc, "GetNamedPipeInfo(%p) failed: %d %s", imc->ipc_handle, err, ipc_winerror(err));
		return NULL;
	}
	ULONG pid;
	if (flags & PIPE_SERVER_END) {
		if (!GetNamedPipeClientProcessId(imc->ipc_handle, &pid)) {
			DWORD err = GetLastError();
			IPC_ERROR(imc, "GetNamedPipeClientProcessId(%p) failed: %d %s", imc->ipc_handle, err,
			          ipc_winerror(err));
			return NULL;
		}
	} else {
		if (!GetNamedPipeServerProcessId(imc->ipc_handle, &pid)) {
			DWORD err = GetLastError();
			IPC_ERROR(imc, "GetNamedPipeServerProcessId(%p) failed: %d %s", imc->ipc_handle, err,
			          ipc_winerror(err));
			return NULL;
		}
	}
	HANDLE h = OpenProcess(PROCESS_DUP_HANDLE, false, pid);
	if (!h) {
		DWORD err = GetLastError();
		IPC_ERROR(imc, "OpenProcess(PROCESS_DUP_HANDLE, pid %d) failed: %d %s", pid, err, ipc_winerror(err));
	}
	return h;
}

xrt_result_t
ipc_send_fds(
    struct ipc_message_channel *imc, const void *data, size_t size, const HANDLE *handles, uint32_t handle_count)
{
	auto rc = ipc_send(imc, data, size);
	if (rc != XRT_SUCCESS) {
		return rc;
	}
	if (!handle_count) {
		return ipc_send(imc, nullptr, 0);
	}
	HANDLE target_process = open_target_process_dup_handle(imc);
	if (!target_process) {
		DWORD err = GetLastError();
		IPC_ERROR(imc, "open_target_process_dup_handle failed: %d %s", err, ipc_winerror(err));
		return XRT_ERROR_IPC_FAILURE;
	}

	HANDLE current_process = GetCurrentProcess();
	std::vector<HANDLE> v;
	v.reserve(handle_count);
	for (uint32_t i = 0; i < handle_count; i++) {
		HANDLE handle;
		if ((size_t)handles[i] & 1) {
			// This handle cannot be duplicated.
			handle = handles[i];
		} else if (!DuplicateHandle(current_process, handles[i], target_process, &handle, 0, false,
		                            DUPLICATE_SAME_ACCESS)) {
			DWORD err = GetLastError();
			IPC_ERROR(imc, "DuplicateHandle(%p) failed: %d %s", handles[i], err, ipc_winerror(err));
			CloseHandle(target_process);
			return XRT_ERROR_IPC_FAILURE;
		}
		v.push_back(handle);
	}
	CloseHandle(target_process);
	return ipc_send(imc, v.data(), v.size() * sizeof(*v.data()));
}

xrt_result_t
ipc_receive_handles_graphics_sync(struct ipc_message_channel *imc,
                                  void *out_data,
                                  size_t size,
                                  xrt_graphics_sync_handle_t *out_handles,
                                  uint32_t handle_count)
{
	return ipc_receive_fds(imc, out_data, size, out_handles, handle_count);
}

xrt_result_t
ipc_send_handles_graphics_sync(struct ipc_message_channel *imc,
                               const void *data,
                               size_t size,
                               const xrt_graphics_sync_handle_t *handles,
                               uint32_t handle_count)
{
	return ipc_send_fds(imc, data, size, handles, handle_count);
}

xrt_result_t
ipc_receive_handles_graphics_buffer(struct ipc_message_channel *imc,
                                    void *out_data,
                                    size_t size,
                                    xrt_graphics_buffer_handle_t *out_handles,
                                    uint32_t handle_count)
{
	return ipc_receive_fds(imc, out_data, size, out_handles, handle_count);
}

xrt_result_t
ipc_send_handles_graphics_buffer(struct ipc_message_channel *imc,
                                 const void *data,
                                 size_t size,
                                 const xrt_graphics_buffer_handle_t *handles,
                                 uint32_t handle_count)
{
	return ipc_send_fds(imc, data, size, handles, handle_count);
}
