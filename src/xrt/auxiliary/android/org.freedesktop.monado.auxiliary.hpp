// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Partially-generated wrapper for the
 * `org.freedesktop.monado.auxiliary` Java package.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_android
 */

#pragma once

#include "wrap/ObjectWrapperBase.h"

namespace wrap {
namespace android::app {
	class Activity;
} // namespace android::app

namespace android::content {
	class Context;
} // namespace android::content

namespace android::view {
	class SurfaceHolder;
	class WindowManager_LayoutParams;
} // namespace android::view

namespace org::freedesktop::monado::auxiliary {
	class MonadoView;
} // namespace org::freedesktop::monado::auxiliary

} // namespace wrap


namespace wrap {
namespace org::freedesktop::monado::auxiliary {
	/*!
	 * Wrapper for org.freedesktop.monado.auxiliary.MonadoView objects.
	 */
	class MonadoView : public ObjectWrapperBase
	{
	public:
		using ObjectWrapperBase::ObjectWrapperBase;
		static constexpr const char *
		getTypeName() noexcept
		{
			return "org/freedesktop/monado/auxiliary/MonadoView";
		}

		static constexpr const char *
		getFullyQualifiedTypeName() noexcept
		{
			return "org.freedesktop.monado.auxiliary.MonadoView";
		}

		/*!
		 * Wrapper for the attachToWindow static method
		 *
		 * Java prototype:
		 * `public static org.freedesktop.monado.auxiliary.MonadoView attachToActivity(android.content.Context,
		 * long, android.view.WindowManager.LayoutParams);`
		 *
		 * JNI signature:
		 * (Landroid/content/Context;JLandroid/view/WindowManager$LayoutParams;)Lorg/freedesktop/monado/auxiliary/MonadoView;
		 *
		 */
		static MonadoView
		attachToWindow(android::content::Context const &displayContext,
		               void *nativePointer,
		               android::view::WindowManager_LayoutParams const &lp);

		/*!
		 * Wrapper for the removeFromWindow static method
		 *
		 * Java prototype:
		 * `public static void removeFromWindow(android.content.Context,
		 * org.freedesktop.monado.auxiliary.MonadoView, int);`
		 *
		 * JNI signature: (Landroid/content/Context;Lorg/freedesktop/monado/auxiliary/MonadoView;I)V
		 *
		 */
		static void
		removeFromWindow(MonadoView const &view);

		/*!
		 * Wrapper for the getDisplayMetrics static method
		 *
		 * Java prototype:
		 * `public static android.util.DisplayMetrics getDisplayMetrics(android.content.Context);`
		 *
		 * JNI signature: (Landroid/content/Context;)Landroid/util/DisplayMetrics;
		 *
		 */
		static jni::Object
		getDisplayMetrics(android::content::Context const &context);

		/*!
		 * Wrapper for the getDisplayRefreshRate static method
		 *
		 * Java prototype:
		 * `public static float getDisplayRefreshRate(android.content.Context);`
		 *
		 * JNI signature: (Landroid/content/Context;)F;
		 *
		 */
		static float
		getDisplayRefreshRate(android::content::Context const &context);

		/*!
		 * Wrapper for the getNativePointer method
		 *
		 * Java prototype:
		 * `public long getNativePointer();`
		 *
		 * JNI signature: ()J
		 *
		 */
		void *
		getNativePointer();

		/*!
		 * Wrapper for the markAsDiscardedByNative method
		 *
		 * Java prototype:
		 * `public void markAsDiscardedByNative();`
		 *
		 * JNI signature: ()V
		 *
		 */
		void
		markAsDiscardedByNative();

		/*!
		 * Wrapper for the waitGetSurfaceHolder method
		 *
		 * Java prototype:
		 * `public android.view.SurfaceHolder waitGetSurfaceHolder(int);`
		 *
		 * JNI signature: (I)Landroid/view/SurfaceHolder;
		 *
		 */
		android::view::SurfaceHolder
		waitGetSurfaceHolder(int32_t wait_ms);

		/*!
		 * Initialize the static metadata of this wrapper with a known
		 * (non-null) Java class.
		 */
		static void
		staticInitClass(jni::jclass clazz)
		{
			Meta::data(clazz);
		}

		/*!
		 * Class metadata
		 */
		struct Meta : public MetaBase
		{
			jni::method_t attachToWindow;
			jni::method_t removeFromWindow;
			jni::method_t getDisplayMetrics;
			jni::method_t getDisplayRefreshRate;
			jni::method_t getNativePointer;
			jni::method_t markAsDiscardedByNative;
			jni::method_t waitGetSurfaceHolder;

			/*!
			 * Singleton accessor
			 */
			static Meta &
			data(jni::jclass clazz = nullptr)
			{
				static Meta instance{clazz};
				return instance;
			}

		private:
			explicit Meta(jni::jclass clazz);
		};
	};

} // namespace org::freedesktop::monado::auxiliary
} // namespace wrap
#include "org.freedesktop.monado.auxiliary.impl.hpp"
