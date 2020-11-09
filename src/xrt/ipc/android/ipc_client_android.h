// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Header exposing Android-specific IPC client code to C.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_android
 */

#pragma once

#include <xrt/xrt_config_os.h>

#ifdef XRT_OS_ANDROID

#ifdef __cplusplus
extern "C" {
#endif

struct _JavaVM;

/**
 * @brief Opaque structure owning the client side of an Android IPC connection.
 *
 */
struct ipc_client_android;

/**
 * @brief Create an ipc_client_android object.
 *
 * Uses org.freedesktop.monado.ipc.Client
 *
 * @param vm Java VM pointer
 * @param activity An android.app.Activity jobject, cast to `void *`.
 *
 * @return struct ipc_client_android*
 *
 * @public @memberof ipc_client_android
 */
struct ipc_client_android *
ipc_client_android_create(struct _JavaVM *vm, void *activity);

/**
 * @brief Make a blocking call to connect to an IPC server and establish socket
 * connection.
 *
 * @param ica
 * @return int file descriptor: -1 in case of error. Do not close it!
 *
 * @public @memberof ipc_client_android
 */
int
ipc_client_android_blocking_connect(struct ipc_client_android *ica);


/**
 * @brief Destroy an ipc_client_android object.
 *
 * @param ptr_ica
 *
 * @public @memberof ipc_client_android
 */
void
ipc_client_android_destroy(struct ipc_client_android **ptr_ica);

#ifdef __cplusplus
}
#endif

#endif // XRT_OS_ANDROID
