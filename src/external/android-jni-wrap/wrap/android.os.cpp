// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#include "android.os.h"

namespace wrap {
namespace android::os {
BaseBundle::Meta::Meta()
    : MetaBaseDroppable(BaseBundle::getTypeName()),
      containsKey(classRef().getMethod("containsKey", "(Ljava/lang/String;)Z")),
      getString(classRef().getMethod("getString",
                                     "(Ljava/lang/String;)Ljava/lang/String;")),
      getString1(classRef().getMethod(
          "getString",
          "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;")) {
    MetaBaseDroppable::dropClassRef();
}
Bundle::Meta::Meta() : MetaBaseDroppable(Bundle::getTypeName()) {
    MetaBaseDroppable::dropClassRef();
}
ParcelFileDescriptor::Meta::Meta()
    : MetaBaseDroppable(ParcelFileDescriptor::getTypeName()),
      adoptFd(classRef().getStaticMethod(
          "adoptFd", "(I)Landroid/os/ParcelFileDescriptor;")),
      getFd(classRef().getMethod("getFd", "()I")),
      detachFd(classRef().getMethod("detachFd", "()I")),
      close(classRef().getMethod("close", "()V")),
      checkError(classRef().getMethod("checkError", "()V")) {
    MetaBaseDroppable::dropClassRef();
}
} // namespace android::os
} // namespace wrap
