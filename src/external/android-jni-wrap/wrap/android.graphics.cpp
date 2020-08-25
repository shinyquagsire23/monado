// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#include "android.graphics.h"

namespace wrap {
namespace android::graphics {
Point::Meta::Meta()
    : MetaBaseDroppable(Point::getTypeName()), x(classRef(), "x"),
      y(classRef(), "y") {
    MetaBaseDroppable::dropClassRef();
}
} // namespace android::graphics
} // namespace wrap
