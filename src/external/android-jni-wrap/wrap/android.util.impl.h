// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

namespace wrap {
namespace android::util {
inline int32_t DisplayMetrics::getHeightPixels() const {
    assert(!isNull());
    return get(Meta::data().heightPixels, object());
}

inline int32_t DisplayMetrics::getWidthPixels() const {
    assert(!isNull());
    return get(Meta::data().widthPixels, object());
}
} // namespace android::util
} // namespace wrap
