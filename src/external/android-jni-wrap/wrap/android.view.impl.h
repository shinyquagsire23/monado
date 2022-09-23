// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>
// Inline implementations: do not include on its own!

#pragma once

#include "android.graphics.h"
#include "android.util.h"

namespace wrap {
namespace android::view {
inline int32_t Display::DEFAULT_DISPLAY() {
    auto &data = Meta::data(true);
    auto ret = get(data.DEFAULT_DISPLAY, data.clazz());
    data.dropClassRef();
    return ret;
}

inline void Display::getRealSize(graphics::Point &out_size) {
    assert(!isNull());
    return object().call<void>(Meta::data().getRealSize, out_size.object());
}

inline void Display::getRealMetrics(util::DisplayMetrics &out_displayMetrics) {
    assert(!isNull());
    return object().call<void>(Meta::data().getRealMetrics,
                               out_displayMetrics.object());
}

inline int32_t Display::getDisplayId() {
    assert(!isNull());
    return object().call<int32_t>(Meta::data().getDisplayId);
}

inline bool Surface::isValid() const {
    assert(!isNull());
    return object().call<bool>(Meta::data().isValid);
}

inline Surface SurfaceHolder::getSurface() {
    assert(!isNull());
    return Surface(object().call<jni::Object>(Meta::data().getSurface));
}

inline Display WindowManager::getDefaultDisplay() {
    assert(!isNull());
    return Display(object().call<jni::Object>(Meta::data().getDefaultDisplay));
}

inline int32_t WindowManager_LayoutParams::TYPE_APPLICATION() {
    auto &data = Meta::data(true);
    auto ret = get(data.TYPE_APPLICATION, data.clazz());
    data.dropClassRef();
    return ret;
}

inline int32_t WindowManager_LayoutParams::TYPE_APPLICATION_OVERLAY() {
    auto &data = Meta::data(true);
    auto ret = get(data.TYPE_APPLICATION_OVERLAY, data.clazz());
    data.dropClassRef();
    return ret;
}

inline int32_t WindowManager_LayoutParams::FLAG_FULLSCREEN() {
    auto &data = Meta::data(true);
    auto ret = get(data.FLAG_FULLSCREEN, data.clazz());
    data.dropClassRef();
    return ret;
}

inline int32_t WindowManager_LayoutParams::FLAG_NOT_FOCUSABLE() {
    auto &data = Meta::data(true);
    auto ret = get(data.FLAG_NOT_FOCUSABLE, data.clazz());
    data.dropClassRef();
    return ret;
}

inline int32_t WindowManager_LayoutParams::FLAG_NOT_TOUCHABLE() {
    auto &data = Meta::data(true);
    auto ret = get(data.FLAG_NOT_TOUCHABLE, data.clazz());
    data.dropClassRef();
    return ret;
}

inline WindowManager_LayoutParams WindowManager_LayoutParams::construct() {
    return WindowManager_LayoutParams(
            Meta::data().clazz().newInstance(Meta::data().init));
}

inline WindowManager_LayoutParams WindowManager_LayoutParams::construct(int32_t type) {
    return WindowManager_LayoutParams(
            Meta::data().clazz().newInstance(Meta::data().init1, type));
}

inline WindowManager_LayoutParams WindowManager_LayoutParams::construct(int32_t type, int32_t flags) {
    return WindowManager_LayoutParams(
            Meta::data().clazz().newInstance(Meta::data().init2, type, flags));
}

} // namespace android::view
} // namespace wrap
