// Copyright 2020-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>
// Inline implementations: do not include on its own!

#pragma once

#include "android.content.h"

namespace wrap {
namespace android::provider {
inline std::string Settings::ACTION_VR_LISTENER_SETTINGS() {
    return get(Meta::data().ACTION_VR_LISTENER_SETTINGS, Meta::data().clazz());
}

inline bool Settings::canDrawOverlays(const content::Context &context) {
    return Meta::data().clazz().call<bool>(Meta::data().canDrawOverlays,
                                           context.object());
}

} // namespace android::provider
} // namespace wrap
