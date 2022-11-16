// Copyright 2022, Magic Leap, Inc.
// Copyright 2020-2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Server mainloop details on Windows.
 * @author Julian Petrov <jpetrov@magicleap.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup ipc_server
 */

#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "xrt/xrt_device.h"
#include "xrt/xrt_instance.h"
#include "xrt/xrt_compositor.h"
#include "xrt/xrt_config_have.h"
#include "xrt/xrt_config_os.h"

#include "os/os_time.h"
#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_debug.h"
#include "util/u_trace_marker.h"
#include "util/u_file.h"
#include "util/u_windows.h"

#include "shared/ipc_shmem.h"
#include "server/ipc_server.h"

#include <conio.h>
#include <sddl.h>


/*
 *
 * Helpers.
 *
 */

#define ERROR_STR(BUF, ERR) (u_winerror(BUF, ARRAY_SIZE(BUF), ERR, true))

DEBUG_GET_ONCE_BOOL_OPTION(relaxed, "IPC_RELAXED_CONNECTION_SECURITY", false)


/*
 *
 * Static functions.
 *
 */

static bool
create_pipe_instance(struct ipc_server_mainloop *ml, bool first)
{
	SECURITY_ATTRIBUTES sa{};
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = nullptr;
	sa.bInheritHandle = FALSE;

	/*
	 * Change the pipe's DACL to allow other users access.
	 *
	 * https://learn.microsoft.com/en-us/windows/win32/secbp/creating-a-dacl
	 * https://learn.microsoft.com/en-us/windows/win32/secauthz/sid-strings
	 */
	const TCHAR *str =               //
	    TEXT("D:")                   // Discretionary ACL
	    TEXT("(D;OICI;GA;;;BG)")     // Guest: deny
	    TEXT("(D;OICI;GA;;;AN)")     // Anonymous: deny
	    TEXT("(A;OICI;GRGWGX;;;AC)") // UWP/AppContainer packages: read/write/execute
	    TEXT("(A;OICI;GRGWGX;;;AU)") // Authenticated user: read/write/execute
	    TEXT("(A;OICI;GA;;;BA)");    // Administrator: full control

	BOOL bret = ConvertStringSecurityDescriptorToSecurityDescriptor( //
	    str,                                                         // StringSecurityDescriptor
	    SDDL_REVISION_1,                                             // StringSDRevision
	    &sa.lpSecurityDescriptor,                                    // SecurityDescriptor
	    NULL);                                                       // SecurityDescriptorSize
	if (!bret) {
		DWORD err = GetLastError();
		char buffer[1024];
		U_LOG_E("ConvertStringSecurityDescriptorToSecurityDescriptor: %u %s", err, ERROR_STR(buffer, err));
	}

	LPSECURITY_ATTRIBUTES lpsa = nullptr;
	if (debug_get_bool_option_relaxed()) {
		U_LOG_W("Using relax security permissions on pipe");
		lpsa = &sa;
	}

	DWORD dwOpenMode = PIPE_ACCESS_DUPLEX;
	DWORD dwPipeMode = PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS;

	if (first) {
		dwOpenMode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
	}

	ml->pipe_handle = CreateNamedPipeA( //
	    ml->pipe_name,                  //
	    dwOpenMode,                     //
	    dwPipeMode,                     //
	    IPC_MAX_CLIENTS,                //
	    IPC_BUF_SIZE,                   //
	    IPC_BUF_SIZE,                   //
	    0,                              //
	    lpsa);                          //

	if (sa.lpSecurityDescriptor != nullptr) {
		// Need to free the security descriptor.
		LocalFree(sa.lpSecurityDescriptor);
		sa.lpSecurityDescriptor = nullptr;
	}

	if (ml->pipe_handle != INVALID_HANDLE_VALUE) {
		return true;
	}

	DWORD err = GetLastError();
	if (err == ERROR_PIPE_BUSY) {
		U_LOG_W("CreateNamedPipeA failed: %d %s An existing client must disconnect first!", err,
		        ipc_winerror(err));
	} else {
		U_LOG_E("CreateNamedPipeA failed: %d %s", err, ipc_winerror(err));
	}

	return false;
}

static void
create_another_pipe_instance(struct ipc_server *vs, struct ipc_server_mainloop *ml)
{
	if (!create_pipe_instance(ml, false)) {
		ipc_server_handle_failure(vs);
	}
}

static void
handle_connected_client(struct ipc_server *vs, struct ipc_server_mainloop *ml)
{
	DWORD mode = PIPE_READMODE_MESSAGE | PIPE_WAIT;
	BOOL bRet;

	bRet = SetNamedPipeHandleState(ml->pipe_handle, &mode, nullptr, nullptr);
	if (bRet) {
		ipc_server_start_client_listener_thread(vs, ml->pipe_handle);
		create_another_pipe_instance(vs, ml);
		return;
	}

	DWORD err = GetLastError();
	U_LOG_E("SetNamedPipeHandleState(PIPE_READMODE_MESSAGE | PIPE_WAIT) failed: %d %s", err, ipc_winerror(err));
	ipc_server_handle_failure(vs);
}


/*
 *
 * Exported functions
 *
 */

void
ipc_server_mainloop_poll(struct ipc_server *vs, struct ipc_server_mainloop *ml)
{
	IPC_TRACE_MARKER();

	if (_kbhit()) {
		U_LOG_E("console input! exiting...");
		ipc_server_handle_shutdown_signal(vs);
		return;
	}

	if (!ml->pipe_handle) {
		create_another_pipe_instance(vs, ml);
	}
	if (!ml->pipe_handle) {
		return; // Errors already logged.
	}

	if (ConnectNamedPipe(ml->pipe_handle, nullptr)) {
		DWORD err = GetLastError();
		U_LOG_E("ConnectNamedPipe unexpected return TRUE treating as failure: %d %s", err, ipc_winerror(err));
		ipc_server_handle_failure(vs);
		return;
	}

	switch (DWORD err = GetLastError()) {
	case ERROR_PIPE_LISTENING: return;
	case ERROR_PIPE_CONNECTED: handle_connected_client(vs, ml); return;
	default:
		U_LOG_E("ConnectNamedPipe failed: %d %s", err, ipc_winerror(err));
		ipc_server_handle_failure(vs);
		return;
	}
}

int
ipc_server_mainloop_init(struct ipc_server_mainloop *ml)
{
	IPC_TRACE_MARKER();

	ml->pipe_handle = INVALID_HANDLE_VALUE;
	ml->pipe_name = nullptr;

	constexpr char pipe_prefix[] = "\\\\.\\pipe\\";
	constexpr int prefix_len = sizeof(pipe_prefix) - 1;
	char pipe_name[MAX_PATH + prefix_len];
	strcpy(pipe_name, pipe_prefix);

	if (u_file_get_path_in_runtime_dir(XRT_IPC_MSG_SOCK_FILENAME, pipe_name + prefix_len, MAX_PATH) == -1) {
		U_LOG_E("u_file_get_path_in_runtime_dir failed!");
		return -1;
	}

	ml->pipe_name = _strdup(pipe_name);
	if (ml->pipe_name == nullptr) {
		U_LOG_E("_strdup(\"%s\") failed!", pipe_name);
		goto err;
	}

	if (!create_pipe_instance(ml, true)) {
		U_LOG_E("CreateNamedPipeA \"%s\" first instance failed, see above.", ml->pipe_name);
		goto err;
	}

	return 0;

err:
	ipc_server_mainloop_deinit(ml);

	return -1;
}

void
ipc_server_mainloop_deinit(struct ipc_server_mainloop *ml)
{
	IPC_TRACE_MARKER();

	if (ml->pipe_handle != INVALID_HANDLE_VALUE) {
		CloseHandle(ml->pipe_handle);
		ml->pipe_handle = INVALID_HANDLE_VALUE;
	}
	if (ml->pipe_name) {
		free(ml->pipe_name);
		ml->pipe_name = nullptr;
	}
}
