// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include "java.lang.h"
#include <string>

namespace wrap {
namespace dalvik::system {
inline DexClassLoader
DexClassLoader::construct(std::string const &searchPath,
                          std::string const &nativeSearchPath,
                          jni::Object parentClassLoader) {
    return DexClassLoader{
        Meta::data().clazz().newInstance(Meta::data().init, searchPath, "",
                                         nativeSearchPath, parentClassLoader)};
}
} // namespace dalvik::system
} // namespace wrap
