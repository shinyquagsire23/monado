// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Out-of-line implementations for partially-generated wrapper for the
 * `org.freedesktop.monado.ipc` Java package.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_android
 */

#include "org.freedesktop.monado.ipc.hpp"

namespace wrap {
namespace org::freedesktop::monado::ipc {
	Client::Meta::Meta(jni::jclass clazz)
	    : MetaBase(Client::getTypeName(), clazz), monado(classRef(), "monado"), failed(classRef(), "failed"),
	      init(classRef().getMethod("<init>", "(J)V")),
	      markAsDiscardedByNative(classRef().getMethod("markAsDiscardedByNative", "()V")),
	      blockingConnect(classRef().getMethod("blockingConnect", "(Landroid/content/Context;Ljava/lang/String;)I"))
	{}
	IMonado::Meta::Meta(jni::jclass clazz)
	    : MetaBase(IMonado::getTypeName(), clazz),
	      passAppSurface(classRef().getMethod("passAppSurface", "(Landroid/view/Surface;)V"))
	{}
} // namespace org::freedesktop::monado::ipc
} // namespace wrap
