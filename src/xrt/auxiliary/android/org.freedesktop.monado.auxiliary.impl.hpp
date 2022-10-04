// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Inline implementations for partially-generated wrapper for the
 * `org.freedesktop.monado.auxiliary` Java package - do not include on its own!
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_android
 */

#pragma once

#include "wrap/android.app.h"
#include "wrap/android.content.h"
#include "wrap/android.view.h"


namespace wrap {
namespace org::freedesktop::monado::auxiliary {
	inline MonadoView
	MonadoView::attachToWindow(android::content::Context const &displayContext,
	                           void *nativePointer,
	                           wrap::android::view::WindowManager_LayoutParams const &lp)
	{
		return MonadoView(Meta::data().clazz().call<jni::Object>(
		    Meta::data().attachToWindow, displayContext.object(),
		    static_cast<long long>(reinterpret_cast<uintptr_t>(nativePointer)), lp.object()));
	}

	inline void
	MonadoView::removeFromWindow(const MonadoView &view)
	{
		Meta::data().clazz().call<void>(Meta::data().removeFromWindow, view.object());
	}

	inline jni::Object
	MonadoView::getDisplayMetrics(android::content::Context const &context)
	{
		return Meta::data().clazz().call<jni::Object>(Meta::data().getDisplayMetrics, context.object());
	}

	inline float
	MonadoView::getDisplayRefreshRate(android::content::Context const &context)
	{
		return Meta::data().clazz().call<float>(Meta::data().getDisplayRefreshRate, context.object());
	}

	inline void *
	MonadoView::getNativePointer()
	{
		assert(!isNull());
		return reinterpret_cast<void *>(
		    static_cast<intptr_t>(object().call<long long>(Meta::data().getNativePointer)));
	}

	inline void
	MonadoView::markAsDiscardedByNative()
	{
		assert(!isNull());
		return object().call<void>(Meta::data().markAsDiscardedByNative);
	}

	inline android::view::SurfaceHolder
	MonadoView::waitGetSurfaceHolder(int32_t wait_ms)
	{
		assert(!isNull());
		return android::view::SurfaceHolder(
		    object().call<jni::Object>(Meta::data().waitGetSurfaceHolder, wait_ms));
	}

} // namespace org::freedesktop::monado::auxiliary
} // namespace wrap
