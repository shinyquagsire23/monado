// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include <string>

namespace wrap {
namespace java::lang {
inline Class Class::forName(std::string &stringParam) {
    return Class(Meta::data().clazz().call<jni::Object>(Meta::data().forName,
                                                        stringParam));
}

inline Class Class::forName(std::string const &name, bool initialize,
                            jni::Object classLoader) {
    return Class(Meta::data().clazz().call<jni::Object>(
        Meta::data().forName1, name, initialize, classLoader));
}

inline Class Class::forName(jstring name, bool initialize,
                            jni::Object classLoader) {
    return Class{Meta::data().clazz().call<jni::Object>(
        Meta::data().forName, name, initialize, classLoader)};
}

inline Class Class::forName(jni::Object const &module,
                            std::string const &name) {
    return Class(Meta::data().clazz().call<jni::Object>(Meta::data().forName2,
                                                        module, name));
}

inline std::string Class::getCanonicalName() {
    assert(!isNull());
    return object().call<std::string>(Meta::data().getCanonicalName);
}

inline Class ClassLoader::loadClass(std::string const &name) {
    assert(!isNull());
    return Class(object().call<jni::Object>(Meta::data().loadClass, name));
}

inline Class ClassLoader::loadClass(jstring name) {
    assert(!isNull());
    return Class{object().call<jni::Object>(Meta::data().loadClass, name)};
}

inline std::string ClassLoader::findLibrary(std::string const &name) {
    assert(!isNull());
    return object().call<std::string>(Meta::data().findLibrary, name);
}

inline std::string System::mapLibraryName(std::string const &name) {
    return Meta::data().clazz().call<std::string>(Meta::data().mapLibraryName,
                                                  name);
}
} // namespace java::lang
} // namespace wrap
