// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Implementation exposing Android-specific IPC client code to C.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_android
 */

#include "ipc_client_android.h"

#include "org.freedesktop.monado.ipc.hpp"
#include "wrap/android.app.h"


#include "xrt/xrt_config_android.h"
#include "android/android_load_class.hpp"
#include "util/u_logging.h"

using wrap::android::app::Activity;
using wrap::org::freedesktop::monado::ipc::Client;

struct ipc_client_android
{
	ipc_client_android(jobject act) : activity(act) {}
	~ipc_client_android();

	Activity activity{};
	Client client{nullptr};
};

ipc_client_android::~ipc_client_android()
{

	// Tell Java that native code is done with this.
	try {
		if (!client.isNull()) {
			client.markAsDiscardedByNative();
		}
	} catch (std::exception const &e) {
		// Must catch and ignore any exceptions in the destructor!
		U_LOG_E("Failure while marking IPC client as discarded: %s", e.what());
	}
}

struct ipc_client_android *
ipc_client_android_create(struct _JavaVM *vm, void *activity)
{

	jni::init(vm);
	try {
		auto info = getAppInfo(XRT_ANDROID_PACKAGE, (jobject)activity);
		if (info.isNull()) {
			U_LOG_E("Could not get application info for package '%s'",
			        "org.freedesktop.monado.openxr_runtime");
			return nullptr;
		}

		auto clazz = loadClassFromPackage(info, (jobject)activity, Client::getFullyQualifiedTypeName());

		if (clazz.isNull()) {
			U_LOG_E("Could not load class '%s' from package '%s'", Client::getFullyQualifiedTypeName(),
			        XRT_ANDROID_PACKAGE);
			return nullptr;
		}

		// Teach the wrapper our class before we start to use it.
		Client::staticInitClass((jclass)clazz.object().getHandle());
		std::unique_ptr<ipc_client_android> ret = std::make_unique<ipc_client_android>((jobject)activity);

		ret->client = Client::construct(ret.get());

		return ret.release();
	} catch (std::exception const &e) {

		U_LOG_E("Could not start IPC client class: %s", e.what());
		return nullptr;
	}
}

int
ipc_client_android_blocking_connect(struct ipc_client_android *ica)
{
	try {
		int fd = ica->client.blockingConnect(ica->activity, XRT_ANDROID_PACKAGE);
		return fd;
	} catch (std::exception const &e) {
		// Must catch and ignore any exceptions in the destructor!
		U_LOG_E("Failure while connecting to IPC server: %s", e.what());
		return -1;
	}
}


void
ipc_client_android_destroy(struct ipc_client_android **ptr_ica)
{

	if (ptr_ica == NULL) {
		return;
	}
	struct ipc_client_android *ica = *ptr_ica;
	if (ica == NULL) {
		return;
	}
	delete ica;
	*ptr_ica = NULL;
}
