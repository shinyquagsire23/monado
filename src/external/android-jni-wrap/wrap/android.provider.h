// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
// Author: Ryan Pavlik <ryan.pavlik@collabora.com>

#pragma once

#include "ObjectWrapperBase.h"
#include <string>

namespace wrap {
namespace android::provider {
/*!
 * Wrapper for android.provider.Settings objects.
 */
class Settings : public ObjectWrapperBase {
  public:
    using ObjectWrapperBase::ObjectWrapperBase;
    static constexpr const char *getTypeName() noexcept {
        return "android/provider/Settings";
    }

    /*!
     * Getter for the ACTION_VR_LISTENER_SETTINGS static field value
     *
     * Java prototype:
     * `public static final java.lang.String ACTION_VR_LISTENER_SETTINGS;`
     *
     * JNI signature: Ljava/lang/String;
     *
     */
    static std::string ACTION_VR_LISTENER_SETTINGS();

    /*!
     * Class metadata
     */
    struct Meta : public MetaBase {
        impl::StaticFieldId<std::string> ACTION_VR_LISTENER_SETTINGS;

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
} // namespace android::provider
} // namespace wrap
#include "android.provider.impl.h"
