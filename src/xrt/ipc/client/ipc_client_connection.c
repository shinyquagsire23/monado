// Copyright 2020-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Just the client connection setup/teardown bits.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_client
 */

#include "os/os_threading.h"
#include "xrt/xrt_results.h"
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "xrt/xrt_instance.h"
#include "xrt/xrt_handles.h"
#include "xrt/xrt_config_os.h"
#include "xrt/xrt_config_android.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_file.h"
#include "util/u_debug.h"
#include "util/u_git_tag.h"
#include "util/u_system_helpers.h"

#include "shared/ipc_protocol.h"
#include "client/ipc_client_connection.h"

#include "ipc_client_generated.h"


#include <stdio.h>
#if !defined(XRT_OS_WINDOWS)
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include <limits.h>

#ifdef XRT_GRAPHICS_BUFFER_HANDLE_IS_AHARDWAREBUFFER
#include "android/android_ahardwarebuffer_allocator.h"
#endif

#ifdef XRT_OS_ANDROID
#include "android/android_globals.h"
#include "android/ipc_client_android.h"
#endif // XRT_OS_ANDROID

DEBUG_GET_ONCE_BOOL_OPTION(ipc_ignore_version, "IPC_IGNORE_VERSION", false)

#ifdef XRT_OS_ANDROID

static bool
ipc_client_socket_connect(struct ipc_connection *ipc_c)
{
	ipc_c->ica = ipc_client_android_create(android_globals_get_vm(), android_globals_get_activity());

	if (ipc_c->ica == NULL) {
		IPC_ERROR(ipc_c, "Client create error!");
		return false;
	}

	int socket = ipc_client_android_blocking_connect(ipc_c->ica);
	if (socket < 0) {
		IPC_ERROR(ipc_c, "Service Connect error!");
		return false;
	}
	// The ownership belongs to the Java object. Dup because the fd will be
	// closed when client destroy.
	socket = dup(socket);
	if (socket < 0) {
		IPC_ERROR(ipc_c, "Failed to dup fd with error %d!", errno);
		return false;
	}

	ipc_c->imc.ipc_handle = socket;
	ipc_c->imc.log_level = ipc_c->log_level;

	return true;
}

#elif defined(XRT_OS_WINDOWS)

#if defined(NO_XRT_SERVICE_LAUNCH) || !defined(XRT_SERVICE_EXECUTABLE)
static HANDLE
ipc_connect_pipe(struct ipc_connection *ipc_c, const char *pipe_name)
{
	HANDLE pipe_inst = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (pipe_inst == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
		IPC_ERROR(ipc_c, "Connect to %s failed: %d %s", pipe_name, err, ipc_winerror(err));
	}
	return pipe_inst;
}
#else
// N.B. quality of life fallback to try launch the XRT_SERVICE_EXECUTABLE if pipe is not found
static HANDLE
ipc_connect_pipe(struct ipc_connection *ipc_c, const char *pipe_name)
{
	HANDLE pipe_inst = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (pipe_inst != INVALID_HANDLE_VALUE) {
		return pipe_inst;
	}
	DWORD err = GetLastError();
	IPC_ERROR(ipc_c, "Connect to %s failed: %d %s", pipe_name, err, ipc_winerror(err));
	if (err != ERROR_FILE_NOT_FOUND) {
		return INVALID_HANDLE_VALUE;
	}
	IPC_INFO(ipc_c, "Trying to launch " XRT_SERVICE_EXECUTABLE "...");
	HMODULE hmod;
	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	                        (LPCSTR)ipc_connect_pipe, &hmod)) {
		IPC_ERROR(ipc_c, "GetModuleHandleExA failed: %d %s", err, ipc_winerror(err));
		return INVALID_HANDLE_VALUE;
	}
	char service_path[MAX_PATH];
	if (!GetModuleFileNameA(hmod, service_path, sizeof(service_path))) {
		IPC_ERROR(ipc_c, "GetModuleFileNameA failed: %d %s", err, ipc_winerror(err));
		return INVALID_HANDLE_VALUE;
	}
	char *p = strrchr(service_path, '\\');
	if (!p) {
		IPC_ERROR(ipc_c, "failed to parse the path %s", service_path);
		return INVALID_HANDLE_VALUE;
	}
	strcpy(p + 1, XRT_SERVICE_EXECUTABLE);
	STARTUPINFOA si = {.cb = sizeof(si)};
	PROCESS_INFORMATION pi;
	if (!CreateProcessA(NULL, service_path, NULL, NULL, false, 0, NULL, NULL, &si, &pi)) {
		*p = 0;
		p = strrchr(service_path, '\\');
		if (!p) {
			err = GetLastError();
			IPC_INFO(ipc_c, XRT_SERVICE_EXECUTABLE " not found in %s: %d %s", service_path, err,
			         ipc_winerror(err));
			return INVALID_HANDLE_VALUE;
		}
		strcpy(p + 1, "service\\" XRT_SERVICE_EXECUTABLE);
		if (!CreateProcessA(NULL, service_path, NULL, NULL, false, 0, NULL, NULL, &si, &pi)) {
			err = GetLastError();
			IPC_INFO(ipc_c, XRT_SERVICE_EXECUTABLE " not found at %s: %d %s", service_path, err,
			         ipc_winerror(err));
			return INVALID_HANDLE_VALUE;
		}
	}
	IPC_INFO(ipc_c, "Launched %s (pid %d)... Waiting for %s...", service_path, pi.dwProcessId, pipe_name);
	CloseHandle(pi.hThread);
	for (int i = 0;; i++) {
		pipe_inst = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (pipe_inst != INVALID_HANDLE_VALUE) {
			IPC_INFO(ipc_c, "Connected to %s after %d msec on try %d!", pipe_name, i * 100, i + 1);
			break;
		}
		err = GetLastError();
		if (err != ERROR_FILE_NOT_FOUND || WaitForSingleObject(pi.hProcess, 100) != WAIT_TIMEOUT) {
			IPC_ERROR(ipc_c, "Connect to %s failed: %d %s", pipe_name, err, ipc_winerror(err));
			break;
		}
	}
	CloseHandle(pi.hProcess);
	return pipe_inst;
}
#endif

static bool
ipc_client_socket_connect(struct ipc_connection *ipc_c)
{
	const char pipe_prefix[] = "\\\\.\\pipe\\";
#define prefix_len sizeof(pipe_prefix) - 1
	char pipe_name[MAX_PATH + prefix_len];
	strcpy(pipe_name, pipe_prefix);

	if (u_file_get_path_in_runtime_dir(XRT_IPC_MSG_SOCK_FILENAME, pipe_name + prefix_len, MAX_PATH) == -1) {
		U_LOG_E("u_file_get_path_in_runtime_dir failed!");
		return false;
	}

	HANDLE pipe_inst = ipc_connect_pipe(ipc_c, pipe_name);
	if (pipe_inst == INVALID_HANDLE_VALUE) {
		return false;
	}
	DWORD mode = PIPE_READMODE_MESSAGE | PIPE_WAIT;
	if (!SetNamedPipeHandleState(pipe_inst, &mode, NULL, NULL)) {
		DWORD err = GetLastError();
		IPC_ERROR(ipc_c, "SetNamedPipeHandleState(PIPE_READMODE_MESSAGE | PIPE_WAIT) failed: %d %s", err,
		          ipc_winerror(err));
		return false;
	}

	ipc_c->imc.ipc_handle = pipe_inst;
	ipc_c->imc.log_level = ipc_c->log_level;
	return true;
}

int
getpid()
{
	return GetCurrentProcessId();
}

#else
static bool
ipc_client_socket_connect(struct ipc_connection *ipc_c)
{
	struct sockaddr_un addr;
	int ret;


	// create our IPC socket

	ret = socket(PF_UNIX, SOCK_STREAM, 0);
	if (ret < 0) {
		IPC_ERROR(ipc_c, "Socket Create Error!");
		return false;
	}

	int socket = ret;

	char sock_file[PATH_MAX];

	ssize_t size = u_file_get_path_in_runtime_dir(XRT_IPC_MSG_SOCK_FILENAME, sock_file, PATH_MAX);
	if (size == -1) {
		IPC_ERROR(ipc_c, "Could not get socket file name");
		return false;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, sock_file);

	ret = connect(socket, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		IPC_ERROR(ipc_c, "Failed to connect to socket %s: %s!", sock_file, strerror(errno));
		close(socket);
		return false;
	}

	ipc_c->imc.ipc_handle = socket;
	ipc_c->imc.log_level = ipc_c->log_level;

	return true;
}
#endif


xrt_result_t
ipc_client_connection_init(struct ipc_connection *ipc_c,
                           enum u_logging_level log_level,
                           struct xrt_instance_info *i_info)
{
	U_ZERO(ipc_c);
	ipc_c->imc.ipc_handle = XRT_IPC_HANDLE_INVALID;
	ipc_c->ism_handle = XRT_SHMEM_HANDLE_INVALID;

	os_mutex_init(&ipc_c->mutex);

	ipc_c->log_level = log_level;

	if (!ipc_client_socket_connect(ipc_c)) {
		IPC_ERROR(ipc_c,
		          "Failed to connect to monado service process\n\n"
		          "###\n"
		          "#\n"
		          "# Please make sure that the service process is running\n"
		          "#\n"
		          "# It is called \"monado-service\"\n"
		          "# In build trees, it is located "
		          "\"build-dir/src/xrt/targets/service/monado-service\"\n"
		          "#\n"
		          "###");
		os_mutex_destroy(&ipc_c->mutex);
		return XRT_ERROR_IPC_FAILURE;
	}

	// get our xdev shm from the server and mmap it
	xrt_result_t xret = ipc_call_instance_get_shm_fd(ipc_c, &ipc_c->ism_handle, 1);
	if (xret != XRT_SUCCESS) {
		IPC_ERROR(ipc_c, "Failed to retrieve shm fd!");
		ipc_client_connection_fini(ipc_c);

		return xret;
	}

	struct ipc_app_state desc = {0};
	desc.info = *i_info;
	desc.pid = getpid(); // Extra info.

	xret = ipc_call_system_set_client_info(ipc_c, &desc);
	if (xret != XRT_SUCCESS) {
		IPC_ERROR(ipc_c, "Failed to set instance info!");
		ipc_client_connection_fini(ipc_c);


		return xret;
	}

	const size_t size = sizeof(struct ipc_shared_memory);

#ifdef XRT_OS_WINDOWS
	ipc_c->ism = MapViewOfFile(ipc_c->ism_handle, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, size);
#else
	const int flags = MAP_SHARED;
	const int access = PROT_READ | PROT_WRITE;

	ipc_c->ism = mmap(NULL, size, access, flags, ipc_c->ism_handle, 0);
#endif
	if (ipc_c->ism == NULL) {
		IPC_ERROR(ipc_c, "Failed to mmap shm!");
		ipc_client_connection_fini(ipc_c);
		return XRT_ERROR_IPC_FAILURE;
	}

	if (strncmp(u_git_tag, ipc_c->ism->u_git_tag, IPC_VERSION_NAME_LEN) != 0) {
		IPC_ERROR(ipc_c, "Monado client library version %s does not match service version %s", u_git_tag,
		          ipc_c->ism->u_git_tag);
		if (!debug_get_bool_option_ipc_ignore_version()) {
			IPC_ERROR(ipc_c, "Set IPC_IGNORE_VERSION=1 to ignore this version conflict");

			return XRT_ERROR_IPC_FAILURE;
		}
	}
	return XRT_SUCCESS;
}

void
ipc_client_connection_fini(struct ipc_connection *ipc_c)
{
	if (ipc_c->ism_handle != XRT_SHMEM_HANDLE_INVALID) {
		/// @todo how to tear down the shared memory?
	}
	ipc_message_channel_close(&ipc_c->imc);
	os_mutex_destroy(&ipc_c->mutex);

#ifdef XRT_OS_ANDROID
	ipc_client_android_destroy(&(ipc_c->ica));
#endif
}
