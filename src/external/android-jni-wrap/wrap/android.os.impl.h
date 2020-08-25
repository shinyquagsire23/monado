// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include <string>

namespace wrap {
namespace android::os {
inline bool BaseBundle::containsKey(std::string const &key) {
    assert(!isNull());
    return object().call<bool>(Meta::data().containsKey, key);
}

inline std::string BaseBundle::getString(std::string const &key) {
    assert(!isNull());
    return object().call<std::string>(Meta::data().getString, key);
}

inline std::string BaseBundle::getString(std::string const &key,
                                         std::string const &defaultValue) {
    assert(!isNull());
    return object().call<std::string>(Meta::data().getString1, key,
                                      defaultValue);
}

inline ParcelFileDescriptor ParcelFileDescriptor::adoptFd(int fd) {
    return ParcelFileDescriptor(
        Meta::data().clazz().call<jni::Object>(Meta::data().adoptFd, fd));
}

inline int ParcelFileDescriptor::getFd() const {
    assert(!isNull());
    return object().call<int>(Meta::data().getFd);
}

inline int ParcelFileDescriptor::detachFd() {
    assert(!isNull());
    return object().call<int>(Meta::data().detachFd);
}

inline void ParcelFileDescriptor::close() {
    assert(!isNull());
    return object().call<void>(Meta::data().close);
}

inline void ParcelFileDescriptor::checkError() const {
    assert(!isNull());
    return object().call<void>(Meta::data().checkError);
}
} // namespace android::os
} // namespace wrap
