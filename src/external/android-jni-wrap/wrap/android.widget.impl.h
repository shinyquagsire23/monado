// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include "android.content.h"

namespace wrap {
namespace android::widget {
inline int32_t Toast::LENGTH_LONG() {
    return get(Meta::data().LENGTH_LONG, Meta::data().clazz());
}

inline int32_t Toast::LENGTH_SHORT() {
    return get(Meta::data().LENGTH_SHORT, Meta::data().clazz());
}

inline void Toast::show() const {
    assert(!isNull());
    return object().call<void>(Meta::data().show);
}

inline Toast Toast::makeText(content::Context const &context,
                             std::string const &stringParam, int32_t duration) {
    return Toast(Meta::data().clazz().call<jni::Object>(
        Meta::data().makeText, context.object(), stringParam, duration));
}

inline Toast Toast::makeText(content::Context &context, int32_t resId,
                             int32_t duration) {
    return Toast(Meta::data().clazz().call<jni::Object>(
        Meta::data().makeText1, context.object(), resId, duration));
}
} // namespace android::widget
} // namespace wrap
