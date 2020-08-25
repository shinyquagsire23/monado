// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#include "dalvik.system.h"

namespace wrap {
namespace dalvik::system {
DexClassLoader::Meta::Meta()
    : MetaBase(DexClassLoader::getTypeName()),
      init(classRef().getMethod("<init>",
                                "(Ljava/lang/String;Ljava/lang/String;Ljava/"
                                "lang/String;Ljava/lang/ClassLoader;)V")) {}
} // namespace dalvik::system
} // namespace wrap
