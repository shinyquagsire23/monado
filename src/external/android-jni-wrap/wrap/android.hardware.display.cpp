// Copyright 2022, Qualcomm Innovation Center, Inc.
// SPDX-License-Identifier: BSL-1.0
// Author: Jarvis Huang

#include "android.hardware.display.h"

namespace wrap {
namespace android::hardware::display {
DisplayManager::Meta::Meta()
    : MetaBaseDroppable(DisplayManager::getTypeName()),
      getDisplay(classRef().getMethod("getDisplay", "(I)Landroid/view/Display;")) {
    MetaBaseDroppable::dropClassRef();
}
} // namespace android::hardware::display
} // namespace wrap
