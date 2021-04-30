// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>
// Inline implementations: do not include on its own!

#pragma once

#include "android.content.h"

namespace wrap {
namespace android::service::vr {
inline bool VrListenerService::isVrModePackageEnabled(
    content::Context const &context,
    content::ComponentName const &componentName) {
    return Meta::data().clazz().call<bool>(Meta::data().isVrModePackageEnabled,
                                           context.object(),
                                           componentName.object());
}

} // namespace android::service::vr
} // namespace wrap
