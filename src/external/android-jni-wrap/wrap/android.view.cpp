// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#include "android.view.h"

namespace wrap {
namespace android::view {
Display::Meta::Meta()
    : MetaBaseDroppable(Display::getTypeName()),
      getRealSize(
          classRef().getMethod("getRealSize", "(Landroid/graphics/Point;)V")),
      getRealMetrics(classRef().getMethod("getRealMetrics",
                                          "(Landroid/util/DisplayMetrics;)V")) {
    MetaBaseDroppable::dropClassRef();
}
Surface::Meta::Meta()
    : MetaBaseDroppable(Surface::getTypeName()),
      isValid(classRef().getMethod("isValid", "()Z")) {
    MetaBaseDroppable::dropClassRef();
}
SurfaceHolder::Meta::Meta()
    : MetaBaseDroppable(SurfaceHolder::getTypeName()),
      getSurface(
          classRef().getMethod("getSurface", "()Landroid/view/Surface;")) {
    MetaBaseDroppable::dropClassRef();
}
} // namespace android::view
} // namespace wrap
