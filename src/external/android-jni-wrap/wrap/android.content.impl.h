// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>
// Inline implementations: do not include on its own!

#pragma once

#include "android.content.pm.h"
#include "android.database.h"
#include "android.net.h"
#include "android.os.h"
#include "java.lang.h"
#include <string>

namespace wrap {
namespace android::content {
inline std::string Context::DISPLAY_SERVICE() {
    auto &data = Meta::data(true);
    auto ret = get(data.DISPLAY_SERVICE, data.clazz());
    data.dropClassRef();
    return ret;
}

inline std::string Context::WINDOW_SERVICE() {
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

inline ContentResolver Context::getContentResolver() const {
    assert(!isNull());
    return ContentResolver(
        object().call<jni::Object>(Meta::data().getContentResolver));
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

inline net::Uri_Builder ContentUris::appendId(net::Uri_Builder &uri_Builder,
                                              long long longParam) {
    auto &data = Meta::data(true);
    auto ret = net::Uri_Builder(data.clazz().call<jni::Object>(
        data.appendId, uri_Builder.object(), longParam));
    data.dropClassRef();
    return ret;
}

inline ComponentName ComponentName::construct(std::string const &pkg,
                                              std::string const &className) {
    return ComponentName(
        Meta::data().clazz().newInstance(Meta::data().init, pkg, className));
}

inline ComponentName ComponentName::construct(Context const &context,
                                              std::string const &className) {
    return ComponentName(Meta::data().clazz().newInstance(
        Meta::data().init1, context.object(), className));
}

inline ComponentName ComponentName::construct(Context const &context,
                                              java::lang::Class const &cls) {
    return ComponentName(Meta::data().clazz().newInstance(
        Meta::data().init2, context.object(), cls.object()));
}

inline ComponentName ComponentName::construct(jni::Object const &parcel) {
    return ComponentName(
        Meta::data().clazz().newInstance(Meta::data().init3, parcel));
}

inline int32_t Intent::FLAG_ACTIVITY_NEW_TASK() {
    return get(Meta::data().FLAG_ACTIVITY_NEW_TASK, Meta::data().clazz());
}

inline Intent Intent::construct() {
    return Intent(Meta::data().clazz().newInstance(Meta::data().init));
}

inline Intent Intent::construct(Intent const &intent) {
    return Intent(
        Meta::data().clazz().newInstance(Meta::data().init1, intent.object()));
}

inline Intent Intent::construct(std::string const &action) {
    return Intent(Meta::data().clazz().newInstance(Meta::data().init2, action));
}

inline Intent Intent::construct(std::string const &action,
                                net::Uri const &uri) {
    return Intent(Meta::data().clazz().newInstance(Meta::data().init3, action,
                                                   uri.object()));
}

inline Intent Intent::construct(Context const &context,
                                java::lang::Class const &classParam) {
    return Intent(Meta::data().clazz().newInstance(
        Meta::data().init4, context.object(), classParam.object()));
}

inline Intent Intent::construct(std::string const &action, net::Uri const &uri,
                                Context const &context,
                                java::lang::Class const &classParam) {
    return Intent(Meta::data().clazz().newInstance(
        Meta::data().init5, action, uri.object(), context.object(),
        classParam.object()));
}

inline Intent Intent::setFlags(int32_t flags) {
    assert(!isNull());
    return Intent(object().call<jni::Object>(Meta::data().setFlags, flags));
}

inline database::Cursor ContentResolver::query(
    net::Uri const &uri, jni::Array<std::string> const &projection,
    std::string const &selection, jni::Array<std::string> const &selectionArgs,
    std::string const &sortOrder) {
    assert(!isNull());
    return database::Cursor(
        object().call<jni::Object>(Meta::data().query, uri.object(), projection,
                                   selection, selectionArgs, sortOrder));
}

inline database::Cursor
ContentResolver::query(net::Uri const &uri,
                       jni::Array<std::string> const &projection) {
    assert(!isNull());
    return database::Cursor(
        object().call<jni::Object>(Meta::data().query, uri.object(), projection,
                                   nullptr, nullptr, nullptr));
}

inline database::Cursor ContentResolver::query(
    net::Uri const &uri, jni::Array<std::string> const &projection,
    std::string const &selection, jni::Array<std::string> const &selectionArgs,
    std::string const &sortOrder, jni::Object const &cancellationSignal) {
    assert(!isNull());
    return database::Cursor(object().call<jni::Object>(
        Meta::data().query1, uri.object(), projection, selection, selectionArgs,
        sortOrder, cancellationSignal));
}

inline database::Cursor ContentResolver::query(
    net::Uri const &uri, jni::Array<std::string> const &projection,
    os::Bundle const &queryArgs, jni::Object const &cancellationSignal) {
    assert(!isNull());
    return database::Cursor(object().call<jni::Object>(
        Meta::data().query2, uri.object(), projection, queryArgs.object(),
        cancellationSignal));
}

} // namespace android::content
} // namespace wrap
