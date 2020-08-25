// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

namespace wrap {
namespace android::provider {
inline std::string Settings::ACTION_VR_LISTENER_SETTINGS() {
    return get(Meta::data().ACTION_VR_LISTENER_SETTINGS, Meta::data().clazz());
}
} // namespace android::provider
} // namespace wrap
