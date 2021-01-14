// Copyright 2020, Collabora, Ltd.
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
	      attachToActivity(classRef().getStaticMethod("attachToActivity",
	                                                  "(Landroid/app/Activity;J)Lorg/freedesktop/"
	                                                  "monado/auxiliary/MonadoView;")),
	      waitGetSurfaceHolder(classRef().getMethod("waitGetSurfaceHolder", "(I)Landroid/view/SurfaceHolder;")),
	      markAsDiscardedByNative(classRef().getMethod("markAsDiscardedByNative", "()V")),
	      getDisplayMetrics(classRef().getStaticMethod("getDisplayMetrics",
	                                                   "(Landroid/app/Activity;)Landroid/util/DisplayMetrics;"))
	{}
} // namespace org::freedesktop::monado::auxiliary
} // namespace wrap
