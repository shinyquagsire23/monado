// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include "android.graphics.h"
#include "android.util.h"

namespace wrap {
namespace android::view {
inline void Display::getRealSize(graphics::Point &out_size) {
    assert(!isNull());
    return object().call<void>(Meta::data().getRealSize, out_size.object());
}

inline void Display::getRealMetrics(util::DisplayMetrics &out_displayMetrics) {
    assert(!isNull());
    return object().call<void>(Meta::data().getRealMetrics,
                               out_displayMetrics.object());
}
inline bool Surface::isValid() const {
    assert(!isNull());
    return object().call<bool>(Meta::data().isValid);
}
inline Surface SurfaceHolder::getSurface() {
    assert(!isNull());
    return Surface(object().call<jni::Object>(Meta::data().getSurface));
}
} // namespace android::view
} // namespace wrap
