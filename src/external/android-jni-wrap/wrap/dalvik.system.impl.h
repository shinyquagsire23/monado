// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>
// Inline implementations: do not include on its own!

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

inline java::lang::Class
DexClassLoader::loadClass(const std::string &name) {
    assert(!isNull());
    return java::lang::Class{object().call<jni::Object>(Meta::data().loadClass, name)};
}

} // namespace dalvik::system
} // namespace wrap
