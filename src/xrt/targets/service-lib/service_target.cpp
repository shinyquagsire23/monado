// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Library exposing IPC server.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_android
 */

#include "jnipp.h"
#include "jni.h"

#include "wrap/android.view.h"

#include "server/ipc_server.h"
#include "server/ipc_server_mainloop_android.h"
#include "util/u_logging.h"

#include <android/native_window.h>
#include <android/native_window_jni.h>

#include "android/android_globals.h"

#include <memory>
#include <thread>

using wrap::android::view::Surface;
namespace {
struct IpcServerHelper
{
public:
	IpcServerHelper() {}

	void
	waitForStartupComplete()
	{
		std::unique_lock<std::mutex> lock{running_mutex};
		running_cond.wait(lock, [&]() { return this->startup_complete; });
	}

	void
	signalStartupComplete()
	{
		std::unique_lock<std::mutex> lock{running_mutex};
		startup_complete = true;
		running_cond.notify_all();
	}

private:
	//! Mutex for starting thread
	std::mutex running_mutex;

	//! Condition variable for starting thread
	std::condition_variable running_cond;
	bool startup_complete = false;
};
} // namespace

static struct ipc_server *server = NULL;
static IpcServerHelper *helper = nullptr;
static std::unique_ptr<std::thread> server_thread{};
static std::mutex server_thread_mutex;

static void
signalStartupCompleteTrampoline(void *data)
{
	static_cast<IpcServerHelper *>(data)->signalStartupComplete();
}

extern "C" void
Java_org_freedesktop_monado_ipc_MonadoImpl_nativeStartServer(JNIEnv *env, jobject thiz)
{
	jni::init(env);
	jni::Object monadoImpl(thiz);
	U_LOG_D("service: Called nativeThreadEntry");

	{
		// Start IPC server
		std::unique_lock lock(server_thread_mutex);
		if (!server && !server_thread) {
			helper = new IpcServerHelper();
			server_thread = std::make_unique<std::thread>(
			    []() { ipc_server_main_android(&server, signalStartupCompleteTrampoline, helper); });
			helper->waitForStartupComplete();
		}
	}
}

extern "C" JNIEXPORT void JNICALL
Java_org_freedesktop_monado_ipc_MonadoImpl_nativeWaitForServerStartup(JNIEnv *env, jobject thiz)
{
	if (server == nullptr) {
		// Should not happen.
		U_LOG_E("service: nativeWaitForServerStartup called before service started up!");
		return;
	}
	helper->waitForStartupComplete();
}

extern "C" JNIEXPORT jint JNICALL
Java_org_freedesktop_monado_ipc_MonadoImpl_nativeAddClient(JNIEnv *env, jobject thiz, int fd)
{
	jni::init(env);
	jni::Object monadoImpl(thiz);
	U_LOG_D("service: Called nativeAddClient with fd %d", fd);
	if (server == nullptr) {
		// Should not happen.
		U_LOG_E("service: nativeAddClient called before service started up!");
		return -1;
	}
	// We try pushing the fd number to the server. If and only if we get a 0 return, has the server taken ownership.
	return ipc_server_mainloop_add_fd(server, &server->ml, fd);
}

extern "C" JNIEXPORT void JNICALL
Java_org_freedesktop_monado_ipc_MonadoImpl_nativeAppSurface(JNIEnv *env, jobject thiz, jobject surface)
{
	jni::init(env);
	Surface surf(surface);
	jni::Object monadoImpl(thiz);

	ANativeWindow *nativeWindow = ANativeWindow_fromSurface(env, surface);
	android_globals_store_window((struct _ANativeWindow *)nativeWindow);
	U_LOG_D("Stored ANativeWindow: %p", (void *)nativeWindow);
}

extern "C" JNIEXPORT jint JNICALL
Java_org_freedesktop_monado_ipc_MonadoImpl_nativeShutdownServer(JNIEnv *env, jobject thiz)
{
	jni::init(env);
	jni::Object monadoImpl(thiz);
	if (server == nullptr || !server_thread) {
		// Should not happen.
		U_LOG_E("service: nativeShutdownServer called before service started up!");
		return -1;
	}

	{
		// Wait until IPC server stop
		std::unique_lock lock(server_thread_mutex);
		ipc_server_handle_shutdown_signal(server);
		server_thread->join();
		server_thread.reset(nullptr);
		delete helper;
		helper = nullptr;
		server = NULL;
	}

	return 0;
}
