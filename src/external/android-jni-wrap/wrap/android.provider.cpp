// Copyright 2020-2023, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#include "android.provider.h"

namespace wrap {
namespace android::provider {
Settings::Meta::Meta()
    : MetaBase(Settings::getTypeName()),
      ACTION_VR_LISTENER_SETTINGS(classRef(), "ACTION_VR_LISTENER_SETTINGS"),
      canDrawOverlays(classRef().getStaticMethod(
          "canDrawOverlays", "(Landroid/content/Context;)Z")) {}
} // namespace android::provider
} // namespace wrap
