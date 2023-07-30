// Copyright 2022, Qualcomm Innovation Center, Inc.
// SPDX-License-Identifier: BSL-1.0
// Author: Jarvis Huang

#include "java.io.h"

namespace wrap {
namespace java::io {
File::Meta::Meta()
    : MetaBase(File::getTypeName()),
      getAbsolutePath(
          classRef().getMethod("getAbsolutePath", "()Ljava/lang/String;")) {}
} // namespace java::io
} // namespace wrap
