// Copyright 2022, Qualcomm Innovation Center, Inc.
// SPDX-License-Identifier: BSL-1.0
// Author: Jarvis Huang

#pragma once

#include "ObjectWrapperBase.h"

namespace wrap {
namespace android::view {
class Display;
} // namespace android::view
} // namespace wrap

namespace wrap {
namespace android::hardware::display {
/*!
 * Wrapper for android.hardware.display.DisplayManager objects.
 */
class DisplayManager : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "android/hardware/display/DisplayManager";
    }

    /*!
     * Wrapper for the getDisplay method
     *
     * Java prototype:
     * `public Display getDisplay(int)`
     *
     * JNI signature: (I)Landroid/view/Display;
     *
     */
    android::view::Display getDisplay(int32_t displayId);

    /*!
     * Class metadata
     */
    struct Meta : public MetaBaseDroppable {
        jni::method_t getDisplay;

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

} // namespace android::hardware::display
} // namespace wrap
#include "android.hardware.display.impl.h"
