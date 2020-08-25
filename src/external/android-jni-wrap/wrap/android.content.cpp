// Copyright 2020, Collabora, Ltd.
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
} // namespace android::content
} // namespace wrap
