// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Inline implementations for partially-generated wrapper for the
 * `org.freedesktop.monado.ipc` Java package - do not include on its own!
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup ipc_android
 */

#pragma once

#include "wrap/android.content.h"
#include "wrap/android.view.h"
#include <string>

namespace wrap {
namespace org::freedesktop::monado::ipc {
	inline IMonado
	Client::getMonado() const
	{
		assert(!isNull());
		return get(Meta::data().monado, object());
	}

	inline bool
	Client::getFailed() const
	{
		assert(!isNull());
		return get(Meta::data().failed, object());
	}

	inline Client
	Client::construct(void *nativePointer)
	{
		return Client(Meta::data().clazz().newInstance(
		    Meta::data().init, static_cast<long long>(reinterpret_cast<intptr_t>(nativePointer))));
	}

	inline void
	Client::markAsDiscardedByNative()
	{
		assert(!isNull());
		return object().call<void>(Meta::data().markAsDiscardedByNative);
	}

	inline int32_t
	Client::blockingConnect(android::content::Context const &context, std::string const &packageName)
	{
		assert(!isNull());
		return object().call<int32_t>(Meta::data().blockingConnect, context.object(), packageName);
	}
	inline void
	IMonado::passAppSurface(android::view::Surface const &surface)
	{
		assert(!isNull());
		return object().call<void>(Meta::data().passAppSurface, surface.object());
	}
} // namespace org::freedesktop::monado::ipc
} // namespace wrap
