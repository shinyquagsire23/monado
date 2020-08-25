// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include "ObjectWrapperBase.h"

namespace wrap {
namespace android::content {
class Context;
} // namespace android::content

namespace android::widget {
class Toast;
} // namespace android::widget

} // namespace wrap

namespace wrap {
namespace android::widget {
/*!
 * Wrapper for android.widget.Toast objects.
 */
class Toast : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "android/widget/Toast";
    }

    /*!
     * Getter for the LENGTH_LONG static field value
     *
     * Java prototype:
     * `public static final int LENGTH_LONG;`
     *
     * JNI signature: I
     *
     */
    static int32_t LENGTH_LONG();

    /*!
     * Getter for the LENGTH_SHORT static field value
     *
     * Java prototype:
     * `public static final int LENGTH_SHORT;`
     *
     * JNI signature: I
     *
     */
    static int32_t LENGTH_SHORT();

    /*!
     * Wrapper for the show method
     *
     * Java prototype:
     * `public void show();`
     *
     * JNI signature: ()V
     *
     */
    void show() const;

    /*!
     * Wrapper for the makeText static method
     *
     * Java prototype:
     * `public static android.widget.Toast makeText(android.content.Context,
     * java.lang.CharSequence, int);`
     *
     * JNI signature:
     * (Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;
     *
     */
    static Toast makeText(content::Context const &context,
                          std::string const &stringParam, int32_t duration);

    /*!
     * Wrapper for the makeText static method
     *
     * Java prototype:
     * `public static android.widget.Toast makeText(android.content.Context,
     * int, int) throws android.content.res.Resources$NotFoundException;`
     *
     * JNI signature: (Landroid/content/Context;II)Landroid/widget/Toast;
     *
     */
    static Toast makeText(content::Context &context, int32_t resId,
                          int32_t duration);

    /*!
     * Class metadata
     */
    struct Meta : public MetaBase {
        impl::StaticFieldId<int32_t> LENGTH_LONG;
        impl::StaticFieldId<int32_t> LENGTH_SHORT;
        jni::method_t show;
        jni::method_t makeText;
        jni::method_t makeText1;

        /*!
         * Singleton accessor
         */
        static Meta &data() {
            static Meta instance;
            return instance;
        }

      private:
        Meta();
    };
};
} // namespace android::widget
} // namespace wrap
#include "android.widget.impl.h"
