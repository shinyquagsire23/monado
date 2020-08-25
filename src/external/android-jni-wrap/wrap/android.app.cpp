// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#include "android.app.h"

namespace wrap {
namespace android::app {
Service::Meta::Meta() : MetaBaseDroppable(Service::getTypeName()) {
    MetaBaseDroppable::dropClassRef();
}
Activity::Meta::Meta()
    : MetaBaseDroppable(Activity::getTypeName()),
      getSystemService(classRef().getMethod(
          "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;")),
      setVrModeEnabled(classRef().getMethod(
          "setVrModeEnabled", "(ZLandroid/content/ComponentName;)V")) {
    MetaBaseDroppable::dropClassRef();
}
} // namespace android::app
} // namespace wrap
