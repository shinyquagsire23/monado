// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include "android.content.h"
#include <string>

namespace wrap {
namespace android::app {

inline jni::Object Activity::getSystemService(std::string const &name) {
    assert(!isNull());
    return object().call<jni::Object>(Meta::data().getSystemService, name);
}

inline void
Activity::setVrModeEnabled(bool enabled,
                           content::ComponentName const &requestedComponent) {
    assert(!isNull());
    return object().call<void>(Meta::data().setVrModeEnabled, enabled,
                               requestedComponent.object());
}
} // namespace android::app
} // namespace wrap
