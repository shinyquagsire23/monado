// Copyright 2022, Qualcomm Innovation Center, Inc.
// SPDX-License-Identifier: BSL-1.0
// Author: Jarvis Huang
// Inline implementations: do not include on its own!

#pragma once

#include "android.view.h"

namespace wrap {
namespace android::hardware::display {
inline android::view::Display DisplayManager::getDisplay(int32_t displayId) {
    assert(!isNull());
    return android::view::Display(
            object().call<jni::Object>(Meta::data().getDisplay, displayId));
}
} // namespace android::hardware::display
} // namespace wrap
