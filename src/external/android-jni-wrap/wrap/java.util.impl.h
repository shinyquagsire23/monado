// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

namespace wrap {
namespace java::util {
inline int32_t List::size() const {
    assert(!isNull());
    return object().call<int32_t>(Meta::data().size);
}

inline jni::Object List::get(int32_t index) const {
    assert(!isNull());
    return object().call<jni::Object>(Meta::data().get, index);
}
} // namespace java::util
} // namespace wrap
