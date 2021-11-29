// Copyright 2020-2021, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#include "android.content.h"

namespace wrap {
namespace android::content {
Context::Meta::Meta(bool deferDrop)
    : MetaBaseDroppable(Context::getTypeName()),
      DISPLAY_SERVICE(classRef(), "DISPLAY_SERVICE"),
      WINDOW_SERVICE(classRef(), "WINDOW_SERVICE"),
      getPackageManager(classRef().getMethod(
          "getPackageManager", "()Landroid/content/pm/PackageManager;")),
      getContentResolver(classRef().getMethod(
          "getContentResolver", "()Landroid/content/ContentResolver;")),
      getApplicationContext(classRef().getMethod(
          "getApplicationContext", "()Landroid/content/Context;")),
      getClassLoader(
          classRef().getMethod("getClassLoader", "()Ljava/lang/ClassLoader;")),
      startActivity(
          classRef().getMethod("startActivity", "(Landroid/content/Intent;)V")),
      startActivity1(classRef().getMethod(
          "startActivity", "(Landroid/content/Intent;Landroid/os/Bundle;)V")),
      createPackageContext(classRef().getMethod(
          "createPackageContext",
          "(Ljava/lang/String;I)Landroid/content/Context;")) {
    if (!deferDrop) {
        MetaBaseDroppable::dropClassRef();
    }
}
ContentUris::Meta::Meta(bool deferDrop)
    : MetaBaseDroppable(ContentUris::getTypeName()),
      appendId(classRef().getStaticMethod(
          "appendId",
          "(Landroid/net/Uri$Builder;J)Landroid/net/Uri$Builder;")) {
    if (!deferDrop) {
        MetaBaseDroppable::dropClassRef();
    }
}
ComponentName::Meta::Meta()
    : MetaBase(ComponentName::getTypeName()),
      init(classRef().getMethod("<init>",
                                "(Ljava/lang/String;Ljava/lang/String;)V")),
      init1(classRef().getMethod(
          "<init>", "(Landroid/content/Context;Ljava/lang/String;)V")),
      init2(classRef().getMethod(
          "<init>", "(Landroid/content/Context;Ljava/lang/Class;)V")),
      init3(classRef().getMethod("<init>", "(Landroid/os/Parcel;)V")) {}
Intent::Meta::Meta()
    : MetaBase(Intent::getTypeName()),
      FLAG_ACTIVITY_NEW_TASK(classRef(), "FLAG_ACTIVITY_NEW_TASK"),
      init(classRef().getMethod("<init>", "()V")),
      init1(classRef().getMethod("<init>", "(Landroid/content/Intent;)V")),
      init2(classRef().getMethod("<init>", "(Ljava/lang/String;)V")),
      init3(classRef().getMethod("<init>",
                                 "(Ljava/lang/String;Landroid/net/Uri;)V")),
      init4(classRef().getMethod(
          "<init>", "(Landroid/content/Context;Ljava/lang/Class;)V")),
      init5(classRef().getMethod("<init>",
                                 "(Ljava/lang/String;Landroid/net/Uri;Landroid/"
                                 "content/Context;Ljava/lang/Class;)V")),
      setFlags(
          classRef().getMethod("setFlags", "(I)Landroid/content/Intent;")) {}
ContentResolver::Meta::Meta()
    : MetaBaseDroppable(ContentResolver::getTypeName()),
      query(classRef().getMethod(
          "query",
          "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/"
          "String;Ljava/lang/String;)Landroid/database/Cursor;")),
      query1(classRef().getMethod(
          "query", "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/"
                   "String;[Ljava/lang/String;Ljava/lang/String;Landroid/os/"
                   "CancellationSignal;)Landroid/database/Cursor;")),
      query2(classRef().getMethod(
          "query",
          "(Landroid/net/Uri;[Ljava/lang/String;Landroid/os/Bundle;Landroid/os/"
          "CancellationSignal;)Landroid/database/Cursor;")) {
    MetaBaseDroppable::dropClassRef();
}
} // namespace android::content
} // namespace wrap
