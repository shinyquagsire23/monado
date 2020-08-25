// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#include "java.util.h"

namespace wrap {
namespace java::util {
List::Meta::Meta()
    : MetaBaseDroppable(List::getTypeName()),
      size(classRef().getMethod("size", "()I")),
      get(classRef().getMethod("get", "(I)Ljava/lang/Object;")) {
    MetaBaseDroppable::dropClassRef();
}
} // namespace java::util
} // namespace wrap
