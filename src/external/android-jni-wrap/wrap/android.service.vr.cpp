// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#include "android.service.vr.h"

namespace wrap {
namespace android::service::vr {
VrListenerService::Meta::Meta()
    : MetaBase(VrListenerService::getTypeName()),
      isVrModePackageEnabled(classRef().getStaticMethod(
          "isVrModePackageEnabled",
          "(Landroid/content/Context;Landroid/content/ComponentName;)Z")) {}
} // namespace android::service::vr
} // namespace wrap
