// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#include "android.widget.h"

namespace wrap {
namespace android::widget {
Toast::Meta::Meta()
    : MetaBase(Toast::getTypeName()), LENGTH_LONG(classRef(), "LENGTH_LONG"),
      LENGTH_SHORT(classRef(), "LENGTH_SHORT"),
      show(classRef().getMethod("show", "()V")),
      makeText(classRef().getStaticMethod(
          "makeText", "(Landroid/content/Context;Ljava/lang/"
                      "CharSequence;I)Landroid/widget/Toast;")),
      makeText1(classRef().getStaticMethod(
          "makeText", "(Landroid/content/Context;II)Landroid/widget/Toast;")) {}
} // namespace android::widget
} // namespace wrap
