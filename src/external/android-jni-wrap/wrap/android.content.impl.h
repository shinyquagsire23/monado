// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include "android.content.pm.h"
#include "android.os.h"
#include "java.lang.h"
#include <string>

namespace wrap {
namespace android::content {
inline std::string Context::DISPLAY_SERVICE() {
    // Defer dropping the class ref to avoid having to do two class lookups
    // by name for this static field. Instead, drop it before we return.
    auto &data = Meta::data(true);
    auto ret = get(data.DISPLAY_SERVICE, data.clazz());
    data.dropClassRef();
    return ret;
}

inline std::string Context::WINDOW_SERVICE() {
    // Defer dropping the class ref to avoid having to do two class lookups
    // by name for this static field. Instead, drop it before we return.
    auto &data = Meta::data(true);
    auto ret = get(data.WINDOW_SERVICE, data.clazz());
    data.dropClassRef();
    return ret;
}

inline pm::PackageManager Context::getPackageManager() {
    assert(!isNull());
    return pm::PackageManager(
        object().call<jni::Object>(Meta::data().getPackageManager));
}

inline Context Context::getApplicationContext() {
    assert(!isNull());
    return Context(
        object().call<jni::Object>(Meta::data().getApplicationContext));
}

inline java::lang::ClassLoader Context::getClassLoader() {
    assert(!isNull());
    return java::lang::ClassLoader(
        object().call<jni::Object>(Meta::data().getClassLoader));
}

inline void Context::startActivity(Intent const &intent) {
    assert(!isNull());
    return object().call<void>(Meta::data().startActivity, intent.object());
}

inline void Context::startActivity(Intent const &intent,
                                   os::Bundle const &bundle) {
    assert(!isNull());
    return object().call<void>(Meta::data().startActivity1, intent.object(),
                               bundle.object());
}

inline Context Context::createPackageContext(std::string const &packageName,
                                             int32_t flags) {
    assert(!isNull());
    return Context(object().call<jni::Object>(Meta::data().createPackageContext,
                                              packageName, flags));
}
inline ComponentName ComponentName::construct(std::string const &pkg,
                                              std::string const &cls) {
    return ComponentName(
        Meta::data().clazz().newInstance(Meta::data().init, pkg, cls));
}

inline ComponentName ComponentName::construct(Context const &pkg,
                                              std::string const &cls) {
    return ComponentName(Meta::data().clazz().newInstance(Meta::data().init1,
                                                          pkg.object(), cls));
}

inline ComponentName ComponentName::construct(Context const &pkg,
                                              java::lang::Class const &cls) {
    return ComponentName(Meta::data().clazz().newInstance(
        Meta::data().init2, pkg.object(), cls.object()));
}

inline ComponentName ComponentName::construct(jni::Object const &parcel) {
    return ComponentName(
        Meta::data().clazz().newInstance(Meta::data().init3, parcel));
}
inline int32_t Intent::FLAG_ACTIVITY_NEW_TASK() {
    return get(Meta::data().FLAG_ACTIVITY_NEW_TASK, Meta::data().clazz());
}

#if 0
// disabled because of zero-length array warning in jnipp
inline Intent Intent::construct() {
    return Intent(Meta::data().clazz().newInstance(Meta::data().init));
}
#endif

inline Intent Intent::construct(Intent &intent) {
    return Intent(
        Meta::data().clazz().newInstance(Meta::data().init1, intent.object()));
}

inline Intent Intent::construct(std::string const &action) {
    return Intent(Meta::data().clazz().newInstance(Meta::data().init2, action));
}

inline Intent Intent::construct(std::string const &action,
                                jni::Object const &uri) {
    return Intent(
        Meta::data().clazz().newInstance(Meta::data().init3, action, uri));
}

inline Intent Intent::construct(Context const &context,
                                java::lang::Class const &classParam) {
    return Intent(Meta::data().clazz().newInstance(
        Meta::data().init4, context.object(), classParam.object()));
}

inline Intent Intent::construct(std::string const &action,
                                jni::Object const &uri, Context const &context,
                                java::lang::Class const &classParam) {
    return Intent(Meta::data().clazz().newInstance(Meta::data().init5, action,
                                                   uri, context.object(),
                                                   classParam.object()));
}

inline Intent Intent::setFlags(int32_t flags) {
    assert(!isNull());
    return Intent(object().call<jni::Object>(Meta::data().setFlags, flags));
}
} // namespace android::content
} // namespace wrap
