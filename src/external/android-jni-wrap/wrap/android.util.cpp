// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#include "android.util.h"

namespace wrap {
namespace android::util {
DisplayMetrics::Meta::Meta()
    : MetaBaseDroppable(DisplayMetrics::getTypeName()),
      heightPixels(classRef(), "heightPixels"),
      widthPixels(classRef(), "widthPixels") {
    MetaBaseDroppable::dropClassRef();
}
} // namespace android::util
} // namespace wrap
