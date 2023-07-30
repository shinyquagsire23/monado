// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#include "android.view.h"

namespace wrap {
namespace android::view {
Display::Meta::Meta(bool deferDrop)
    : MetaBaseDroppable(Display::getTypeName()),
      DEFAULT_DISPLAY(classRef(), "DEFAULT_DISPLAY"),
      getRealSize(
          classRef().getMethod("getRealSize", "(Landroid/graphics/Point;)V")),
      getRealMetrics(classRef().getMethod("getRealMetrics",
                                          "(Landroid/util/DisplayMetrics;)V")),
      getDisplayId(classRef().getMethod("getDisplayId", "()I")) {
    if (!deferDrop) {
        MetaBaseDroppable::dropClassRef();
    }
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
WindowManager::Meta::Meta()
        : MetaBaseDroppable(WindowManager::getTypeName()),
          getDefaultDisplay(
                  classRef().getMethod("getDefaultDisplay", "()Landroid/view/Display;")) {
    MetaBaseDroppable::dropClassRef();
}
WindowManager_LayoutParams::Meta::Meta(bool deferDrop)
        : MetaBaseDroppable(WindowManager_LayoutParams::getTypeName()),
          TYPE_APPLICATION(classRef(), "TYPE_APPLICATION"),
          TYPE_APPLICATION_OVERLAY(classRef(), "TYPE_APPLICATION_OVERLAY"),
          FLAG_FULLSCREEN(classRef(), "FLAG_FULLSCREEN"),
          FLAG_NOT_FOCUSABLE(classRef(), "FLAG_NOT_FOCUSABLE"),
          FLAG_NOT_TOUCHABLE(classRef(), "FLAG_NOT_TOUCHABLE"),
          init(classRef().getMethod("<init>", "()V")),
          init1(classRef().getMethod("<init>", "(I)V")),
          init2(classRef().getMethod("<init>", "(II)V")) {
    if (!deferDrop) {
        MetaBaseDroppable::dropClassRef();
    }
}
} // namespace android::view
} // namespace wrap
