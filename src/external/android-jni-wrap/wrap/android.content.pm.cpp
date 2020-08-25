// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#include "android.content.pm.h"

namespace wrap {
namespace android::content::pm {
PackageItemInfo::Meta::Meta()
    : MetaBaseDroppable(PackageItemInfo::getTypeName()),
      metaData(classRef(), "metaData"), name(classRef(), "name"),
      packageName(classRef(), "packageName") {
    MetaBaseDroppable::dropClassRef();
}
ComponentInfo::Meta::Meta() : MetaBaseDroppable(ComponentInfo::getTypeName()) {
    MetaBaseDroppable::dropClassRef();
}
ServiceInfo::Meta::Meta() : MetaBaseDroppable(ServiceInfo::getTypeName()) {
    MetaBaseDroppable::dropClassRef();
}
ApplicationInfo::Meta::Meta()
    : MetaBaseDroppable(ApplicationInfo::getTypeName()),
      nativeLibraryDir(classRef(), "nativeLibraryDir"),
      publicSourceDir(classRef(), "publicSourceDir") {
    MetaBaseDroppable::dropClassRef();
}
PackageInfo::Meta::Meta()
    : MetaBaseDroppable(PackageInfo::getTypeName()),
      applicationInfo(classRef(), "applicationInfo"),
      packageName(classRef(), "packageName") {
    MetaBaseDroppable::dropClassRef();
}
ResolveInfo::Meta::Meta()
    : MetaBaseDroppable(ResolveInfo::getTypeName()),
      serviceInfo(classRef(), "serviceInfo") {
    MetaBaseDroppable::dropClassRef();
}
PackageManager::Meta::Meta()
    : MetaBaseDroppable(PackageManager::getTypeName()),
      getPackageInfo(classRef().getMethod(
          "getPackageInfo",
          "(Ljava/lang/String;I)Landroid/content/pm/PackageInfo;")),
      getPackageInfo1(classRef().getMethod(
          "getPackageInfo", "(Landroid/content/pm/VersionedPackage;I)Landroid/"
                            "content/pm/PackageInfo;")),
      getApplicationInfo(classRef().getMethod(
          "getApplicationInfo",
          "(Ljava/lang/String;I)Landroid/content/pm/ApplicationInfo;")),
      queryIntentServices(
          classRef().getMethod("queryIntentServices",
                               "(Landroid/content/Intent;I)Ljava/util/List;")) {
    MetaBaseDroppable::dropClassRef();
}
} // namespace android::content::pm
} // namespace wrap
