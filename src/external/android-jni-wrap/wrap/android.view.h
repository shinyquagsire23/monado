// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include "ObjectWrapperBase.h"

namespace wrap {
namespace android::graphics {
class Point;
} // namespace android::graphics

namespace android::util {
class DisplayMetrics;
} // namespace android::util

namespace android::view {
class Surface;
} // namespace android::view

} // namespace wrap

namespace wrap {
namespace android::view {
/*!
 * Wrapper for android.view.Display objects.
 */
class Display : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "android/view/Display";
    }

    /*!
     * Wrapper for the getRealSize method
     *
     * Java prototype:
     * `public void getRealSize(android.graphics.Point);`
     *
     * JNI signature: (Landroid/graphics/Point;)V
     *
     */
    void getRealSize(graphics::Point &out_size);

    /*!
     * Wrapper for the getRealMetrics method
     *
     * Java prototype:
     * `public void getRealMetrics(android.util.DisplayMetrics);`
     *
     * JNI signature: (Landroid/util/DisplayMetrics;)V
     *
     */
    void getRealMetrics(util::DisplayMetrics &out_displayMetrics);

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        jni::method_t getRealSize;
        jni::method_t getRealMetrics;

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
/*!
 * Wrapper for android.view.Surface objects.
 */
class Surface : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "android/view/Surface";
    }

    /*!
     * Wrapper for the isValid method
     *
     * Java prototype:
     * `public boolean isValid();`
     *
     * JNI signature: ()Z
     *
     */
    bool isValid() const;

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        jni::method_t isValid;

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
/*!
 * Wrapper for android.view.SurfaceHolder objects.
 */
class SurfaceHolder : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "android/view/SurfaceHolder";
    }

    /*!
     * Wrapper for the getSurface method
     *
     * Java prototype:
     * `public abstract android.view.Surface getSurface();`
     *
     * JNI signature: ()Landroid/view/Surface;
     *
     */
    Surface getSurface();

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        jni::method_t getSurface;

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
} // namespace android::view
} // namespace wrap
#include "android.view.impl.h"
