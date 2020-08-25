// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#include "android.provider.h"

namespace wrap {
namespace android::provider {
Settings::Meta::Meta()
    : MetaBase(Settings::getTypeName()),
      ACTION_VR_LISTENER_SETTINGS(classRef(), "ACTION_VR_LISTENER_SETTINGS") {}
} // namespace android::provider
} // namespace wrap
