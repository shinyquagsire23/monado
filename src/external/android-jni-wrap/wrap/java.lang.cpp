// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#include "java.lang.h"

namespace wrap {
namespace java::lang {
Class::Meta::Meta()
    : MetaBase(Class::getTypeName()),
      forName(classRef().getStaticMethod(
          "forName", "(Ljava/lang/String;)Ljava/lang/Class;")),
      forName1(classRef().getStaticMethod(
          "forName",
          "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;")),
      forName2(classRef().getStaticMethod(
          "forName",
          "(Ljava/lang/Module;Ljava/lang/String;)Ljava/lang/Class;")),
      getCanonicalName(
          classRef().getMethod("getCanonicalName", "()Ljava/lang/String;")) {}
ClassLoader::Meta::Meta()
    : MetaBaseDroppable(ClassLoader::getTypeName()),
      loadClass(classRef().getMethod("loadClass",
                                     "(Ljava/lang/String;)Ljava/lang/Class;")),
      findLibrary(classRef().getMethod(
          "findLibrary", "(Ljava/lang/String;)Ljava/lang/String;")) {
    MetaBaseDroppable::dropClassRef();
}
System::Meta::Meta()
    : MetaBase(System::getTypeName()),
      mapLibraryName(classRef().getStaticMethod(
          "mapLibraryName", "(Ljava/lang/String;)Ljava/lang/String;")) {}
} // namespace java::lang
} // namespace wrap
