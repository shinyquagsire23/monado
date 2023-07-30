// Copyright 2020-2021, Collabora, Ltd.
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
     * Getter for the DEFAULT_DISPLAY static field value
     *
     * Java prototype:
     * `public static final int DEFAULT_DISPLAY;`
     *
     * JNI signature: I
     *
     */
    static int32_t DEFAULT_DISPLAY();

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
     * Wrapper for the getDisplayId method
     *
     * Java prototype:
     * `public int getDisplayId();`
     *
     * JNI signature: ()I
     *
     */
    int32_t getDisplayId();

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        impl::StaticFieldId<int32_t> DEFAULT_DISPLAY;
        jni::method_t getRealSize;
        jni::method_t getRealMetrics;
        jni::method_t getDisplayId;

        /*!
         * Singleton accessor
         */
        static Meta &data(bool deferDrop = false) {
            static Meta instance{deferDrop};
            return instance;
        }

      private:
        Meta(bool deferDrop);
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
            static Meta instance{};
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
            static Meta instance{};
            return instance;
        }

      private:
        Meta();
    };
};

/*!
 * Wrapper for android.view.WindowManager objects.
 */
class WindowManager : public ObjectWrapperBase {
public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "android/view/WindowManager";
    }

    /*!
     * Wrapper for the getDefaultDisplay method
     *
     * Java prototype:
     * `public abstract android.view.Display getDefaultDisplay();`
     *
     * JNI signature: ()Landroid/view/Display;
     *
     */
    Display getDefaultDisplay();

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        jni::method_t getDefaultDisplay;

        /*!
         * Singleton accessor
         */
        static Meta &data() {
            static Meta instance{};
            return instance;
        }

    private:
        Meta();
    };
};

/*!
 * Wrapper for android.view.WindowManager objects.
 */
class WindowManager_LayoutParams : public ObjectWrapperBase {
public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "android/view/WindowManager$LayoutParams";
    }

    /*!
     * Getter for the TYPE_APPLICATION static field value
     *
     * Java prototype:
     * `public static final int TYPE_APPLICATION;`
     *
     * JNI signature: I
     *
     */
    static int32_t TYPE_APPLICATION();

    /*!
     * Getter for the TYPE_APPLICATION_OVERLAY static field value
     *
     * Java prototype:
     * `public static final int TYPE_APPLICATION_OVERLAY;`
     *
     * JNI signature: I
     *
     */
    static int32_t TYPE_APPLICATION_OVERLAY();

    /*!
     * Getter for the FLAG_FULLSCREEN static field value
     *
     * Java prototype:
     * `public static final int FLAG_FULLSCREEN;`
     *
     * JNI signature: I
     *
     */
    static int32_t FLAG_FULLSCREEN();

    /*!
     * Getter for the FLAG_NOT_FOCUSABLE static field value
     *
     * Java prototype:
     * `public static final int FLAG_NOT_FOCUSABLE;`
     *
     * JNI signature: I
     *
     */
    static int32_t FLAG_NOT_FOCUSABLE();

    /*!
     * Getter for the FLAG_NOT_TOUCHABLE static field value
     *
     * Java prototype:
     * `public static final int FLAG_NOT_TOUCHABLE;`
     *
     * JNI signature: I
     *
     */
    static int32_t FLAG_NOT_TOUCHABLE();

    /*!
     * Wrapper for a constructor
     *
     * Java prototype:
     * `public android.view.WindowManager$LayoutParams();`
     *
     * JNI signature: ()V
     *
     */
    static WindowManager_LayoutParams construct();

    /*!
     * Wrapper for a constructor
     *
     * Java prototype:
     * `public android.view.WindowManager$LayoutParams(int);`
     *
     * JNI signature: (I)V
     *
     */
    static WindowManager_LayoutParams construct(int32_t type);

    /*!
     * Wrapper for a constructor
     *
     * Java prototype:
     * `public android.view.WindowManager$LayoutParams(int, int);`
     *
     * JNI signature: (II)V
     *
     */
    static WindowManager_LayoutParams construct(int32_t type, int32_t flags);

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        impl::StaticFieldId<int32_t> TYPE_APPLICATION;
        impl::StaticFieldId<int32_t> TYPE_APPLICATION_OVERLAY;
        impl::StaticFieldId<int32_t> FLAG_FULLSCREEN;
        impl::StaticFieldId<int32_t> FLAG_NOT_FOCUSABLE;
        impl::StaticFieldId<int32_t> FLAG_NOT_TOUCHABLE;
        jni::method_t init;
        jni::method_t init1;
        jni::method_t init2;

        /*!
         * Singleton accessor
         */
        static Meta &data(bool deferDrop = false) {
            static Meta instance{deferDrop};
            return instance;
        }

    private:
        Meta(bool deferDrop);
    };
};

} // namespace android::view
} // namespace wrap
#include "android.view.impl.h"
