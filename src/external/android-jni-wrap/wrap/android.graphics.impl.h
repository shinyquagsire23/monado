// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

namespace wrap {
namespace android::graphics {
inline int32_t Point::getX() const {
    assert(!isNull());
    return get(Meta::data().x, object());
}

inline int32_t Point::getY() const {
    assert(!isNull());
    return get(Meta::data().y, object());
}
} // namespace android::graphics
} // namespace wrap
