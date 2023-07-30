// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Out-of-line implementations for partially-generated wrapper for the
 * `org.freedesktop.monado.auxiliary` Java package.
 * @author Ryan Pavlik <ryan.pavlik@collabora.com>
 * @ingroup aux_android
 */

#include "org.freedesktop.monado.auxiliary.hpp"

namespace wrap {
namespace org::freedesktop::monado::auxiliary {
	MonadoView::Meta::Meta(jni::jclass clazz)
	    : MetaBase(MonadoView::getTypeName(), clazz),
	      attachToWindow(classRef().getStaticMethod(
	          "attachToWindow",
	          "(Landroid/content/Context;JLandroid/view/WindowManager$LayoutParams;)Lorg/freedesktop/monado/"
	          "auxiliary/MonadoView;")),
	      removeFromWindow(
	          classRef().getStaticMethod("removeFromWindow", "(Lorg/freedesktop/monado/auxiliary/MonadoView;)V")),
	      getDisplayMetrics(classRef().getStaticMethod("getDisplayMetrics",
	                                                   "(Landroid/content/Context;)Landroid/util/DisplayMetrics;")),
	      getDisplayRefreshRate(
	          classRef().getStaticMethod("getDisplayRefreshRate", "(Landroid/content/Context;)F")),
	      getNativePointer(classRef().getMethod("getNativePointer", "()J")),
	      markAsDiscardedByNative(classRef().getMethod("markAsDiscardedByNative", "()V")),
	      waitGetSurfaceHolder(classRef().getMethod("waitGetSurfaceHolder", "(I)Landroid/view/SurfaceHolder;"))
	{}
} // namespace org::freedesktop::monado::auxiliary
} // namespace wrap
